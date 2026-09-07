#include "JHantek1008Driver.h"

#include "JHantek1008Codec.h"
#include "scope/JScopeDriverRegistry.h"
#include "scope/JScopeLog.h"
#include "usb/JUsbContext.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>

inline namespace jf {

namespace {

// Burst records hold roughly 25 samples per division across 10 divisions, but
// the device states the real length in its 0xc6 reply — this is only what the
// pool is provisioned for.
constexpr uint32_t kMaxBurstSamplesPerChannel = 4096;

// A roll chunk is whatever has accumulated; this bounds one frame.
constexpr uint32_t kMaxRollSamplesPerChannel = 4096;

// How much roll data to gather before publishing a frame.
//
// This FOLLOWS THE TIMEBASE, because otherwise the seconds/div control does
// nothing whatever in roll mode and the display shows a fixed slice of time no
// matter what the user asks for. A quarter of a second of a 1 kHz signal is 250
// cycles across the plot — perfectly faithful, and unreadable hash.
//
// Clamped at both ends: too short and the display flickers with too few samples
// for a measurement to mean anything, too long and one frame takes seconds to
// arrive.
constexpr double   kRollWindowMinSeconds = 0.02;
constexpr double   kRollWindowMaxSeconds = 2.00;
constexpr uint32_t kRollWindowMinSamples = 16;

// How long to wait for a record, on top of the time the capture itself must
// take. Host-side policy, because the hardware has no sweep mode of its own:
// Auto gives up and sweeps, Normal keeps waiting for an edge that may be rare.
//
// These are MARGINS, not totals. A capture cannot possibly be ready before the
// device has filled its record, and at the slow end that is half a second — so
// a fixed count meant giving up long before the data existed. At 50 ms/div the
// old Auto limit of four polls was 80 ms against a 504 ms record, which read
// back as "triggered=no" whatever the signal was doing, and as records that
// were mostly flat because they were collected before the sweep finished.
constexpr int kAutoReadyMargin   = 8;    // ~160 ms past the end of the record
// After a forced sweep the record is already filling; this only covers the read.
constexpr int kSweepReadyMargin  = 12;
constexpr int kNormalReadyMargin = 250;  // ~5 s, for a genuinely infrequent edge

// Polls needed to cover the capture itself, plus the caller's margin.
int readyAttemptsFor(double recordSeconds, double pollInterval, int margin) {
    if (!(pollInterval > 0.0)) return margin;
    const double sweeps = recordSeconds / pollInterval;
    return static_cast<int>(sweeps) + margin;
}

double clampTriggerLevelCounts(double volts, double vscale, double zeroOffset) {
    const double counts = zeroOffset + volts / (JHantek1008Tables::kVoltsPerCount * vscale);
    return std::clamp(counts, static_cast<double>(JHantek1008Tables::kCountsMin),
                              static_cast<double>(JHantek1008Tables::kCountsMax));
}

} // namespace

JHantek1008Driver::JHantek1008Driver() {
    _buildCapabilities();
    for (uint8_t c = 0; c < JHantek1008Tables::kChannelCount; ++c) {
        m_channels[c].enabled     = (c < 4);
        // 1 V/div: an automotive 0-5 V signal then occupies five divisions of
        // eight, which is what it should look like. The underlying hardware
        // range is chosen from this, not the other way round.
        m_channels[c].voltsPerDiv = 1.0;
        m_channels[c].coupling    = JScopeCoupling::DC;   // the only thing it has
        m_channels[c].probeRatio  = 1.0;
    }
    // WINDOWED by default, not roll.
    //
    // Roll is what this instrument is for in the end — cranking waveforms, slow
    // automotive signals — but it tops out around 810 Sa/s, so Nyquist is about
    // 400 Hz. Anything faster is not merely inaccurate: a 1 kHz square arrives
    // as roughly one sample per half cycle, the trace draws straight lines
    // between them, and the result is a clean triangle wave that looks like a
    // real signal and is not one.
    //
    // Burst samples at 200 kSa/s and resolves almost anything this instrument
    // can see, so it is the safe thing to come up in. Roll is one control away.
    m_timebase.mode          = JScopeAcquisitionMode::Windowed;
    m_timebase.sampleRate    = 440.0;
    m_timebase.secondsPerDiv = 500.0e-6;
    m_calibration.clear();
}

JHantek1008Driver::~JHantek1008Driver() { close(); }

std::vector<JScopeDeviceInfo> JHantek1008Driver::enumerate() {
    std::vector<JScopeDeviceInfo> out;
    for (const JUsbDeviceInfo& d : JUsbContext::instance().enumerate(
             JHantek1008Tables::kVendorId, JHantek1008Tables::kProductId)) {
        JScopeDeviceInfo info;
        info.driverId      = "hantek-1008c";
        // The device calls itself "YDJ-2088" with no mention of Hantek, so the
        // name shown is this driver's, not the descriptor's.
        info.displayName   = "Hantek 1008C  (" + d.portPath + ")";
        info.vendorId      = d.vendorId;
        info.productId     = d.productId;
        info.busNumber     = d.busNumber;
        info.deviceAddress = d.deviceAddress;
        info.portPath      = d.portPath;
        info.serialNumber  = d.serialNumber;
        out.push_back(std::move(info));
    }
    return out;
}

void JHantek1008Driver::_buildCapabilities() {
    m_caps = JScopeCapabilities{};
    m_caps.driverId            = m_driverId;
    m_caps.model               = "Hantek 1008C";
    m_caps.adcBits             = JHantek1008Tables::kAdcBits;
    m_caps.countsMin           = JHantek1008Tables::kCountsMin;
    m_caps.countsMax           = JHantek1008Tables::kCountsMax;
    m_caps.verticalDivisions   = JHantek1008Tables::kVerticalDivisions;
    m_caps.horizontalDivisions = JHantek1008Tables::kHorizontalDivisions;
    m_caps.maxSimultaneousChannels = JHantek1008Tables::kChannelCount;

    // A 1-2-5 ladder, as any scope has. The hardware's three ranges are coarse
    // analogue gain underneath; the steps between them are made up when the
    // trace is drawn. See JHantek1008Tables::kVoltsPerDivSteps.
    const std::vector<double> vdiv(JHantek1008Tables::kVoltsPerDivSteps.begin(),
                                   JHantek1008Tables::kVoltsPerDivSteps.end());

    for (uint8_t c = 0; c < JHantek1008Tables::kChannelCount; ++c) {
        JScopeChannelCaps ch;
        ch.label       = "CH" + std::to_string(c + 1);
        ch.voltsPerDiv = vdiv;
        // Scaling is applied host-side: the device has one front end and the
        // probe only changes what a count means. voltsPerDiv is at the PROBE
        // TIP, as it is on any scope, so the ladder the user sees is this one
        // multiplied by the ratio.
        // The attenuations the OEM software offers for this instrument, read
        // off its own probe list by stepping through it: x1 first (which the
        // static analysis of the binary missed), then the decades, then the
        // 20:1 attenuator. The current clamps it also lists are omitted — they
        // change the vertical UNIT to amps, and this app has no notion of that
        // yet, so offering them would only mislabel volts.
        ch.probeRatios = { 1.0, 10.0, 20.0, 100.0, 1000.0, 10000.0 };
        // Everything this device does NOT have, stated rather than emulated.
        ch.couplings         = {};           // no coupling control
        ch.offsetRangeVolts  = 0.0;          // no vertical position
        ch.canInvert         = false;
        ch.canBandwidthLimit = false;
        m_caps.channels.push_back(std::move(ch));
    }

    m_caps.acquisitionModes = jScopeAcquisitionModeBit(JScopeAcquisitionMode::Streaming)
                            | jScopeAcquisitionModeBit(JScopeAcquisitionMode::Windowed);

    // Only the codes that are distinct AND honoured — see kUsableTimeDivIds.
    // Offering the rest would put sixteen identical entries in the menu plus one
    // that reads plausibly and is 1.43x wrong.
    for (double sdiv : JHantek1008Tables::kStandardSecondsPerDiv)
        m_caps.secondsPerDiv.push_back(sdiv);

    for (const auto& r : JHantek1008Tables::kRollRates)
        m_caps.streamSampleRates.push_back(r.rate);
    std::sort(m_caps.streamSampleRates.begin(), m_caps.streamSampleRates.end());

    // The device decides how long a burst record is; it is stated in the reply
    // to 0xc6, not chosen by the host.
    m_caps.deviceDeterminedRecordLength = true;

    // All three sweep modes, implemented as host policy around the ready poll —
    // see the header. The hardware trigger flag is false because the DEVICE has
    // no sweep behaviour of its own.
    m_caps.triggerModes = jScopeTriggerModeBit(JScopeTriggerMode::Auto)
                        | jScopeTriggerModeBit(JScopeTriggerMode::Normal)
                        | jScopeTriggerModeBit(JScopeTriggerMode::Single);
    m_caps.hasHardwareTrigger = true;      // the trigger itself IS in hardware
    m_caps.hasTriggerPosition = false;
    m_caps.hasForceTrigger    = false;
    // Host-side, like the sweep modes: the hardware has none, but something has
    // to fit the signal to these coarse ranges and the user should not have to.
    m_caps.hasAutoset         = true;

    // The 1008C's generator is an eight-line digital pattern driven in RPM — a
    // crank simulator, not a DDS.
    //
    // Taken FROM the generator rather than restated here. The two used to
    // disagree: this said 1440 steps, which is what the device holds, while the
    // driver can only write the 62 that fit one packet — and the UI builds itself
    // from these numbers, so it would have offered a length that could not be sent.
    m_caps.generator = m_generator.capabilities();
}

std::vector<uint8_t> JHantek1008Driver::_activeChannels() const {
    std::vector<uint8_t> out;
    for (uint8_t c = 0; c < JHantek1008Tables::kChannelCount; ++c)
        if (m_channels[c].enabled) out.push_back(c);
    if (out.empty()) out.push_back(0);       // the device requires at least one
    return out;
}

std::vector<double> JHantek1008Driver::_perChannelVScale() const {
    std::vector<double> out(JHantek1008Tables::kChannelCount, 1.0);
    for (uint8_t c = 0; c < JHantek1008Tables::kChannelCount; ++c)
        // The NARROWEST range that holds this volts/div. Narrower means finer
        // resolution, so it is always the right choice, and the display makes up
        // the difference between the range and the requested step.
        // The hardware sees the tip voltage divided by the probe, so the range
        // is chosen for what actually arrives at the BNC.
        out[c] = JHantek1008Tables::vscaleForVoltsPerDiv(
                     m_channels[c].voltsPerDiv / m_channels[c].probeRatio);
    return out;
}

bool JHantek1008Driver::open(const JScopeDeviceInfo& device) {
    if (m_open) return true;

    JUsbDeviceInfo usbInfo;
    usbInfo.vendorId      = JHantek1008Tables::kVendorId;
    usbInfo.productId     = JHantek1008Tables::kProductId;
    usbInfo.portPath      = device.portPath;
    usbInfo.serialNumber  = device.serialNumber;

    if (!m_usb.open(usbInfo)) { _postError(m_usb.lastError()); return false; }
    if (!m_usb.claimInterface(JHantek1008Tables::kInterfaceNumber)) {
        _postError(m_usb.lastError());
        m_usb.close();
        return false;
    }

    m_protocol = std::make_unique<JHantek1008Protocol>(
        m_usb, m_usb.bulkOutEndpoint(), m_usb.bulkInEndpoint());
    m_generator.attach(m_protocol.get());
    m_generator.setChangeHandler([this] { _generatorChanged(); });

    m_caps.serialNumber = device.serialNumber;

    JLOGC(JScopeLog::kHantek, JLogLevel::Info)
        << "bringing up the 1008C at " << device.portPath;

    if (!m_protocol->initialisePhase1()) {
        _postError("initialisation failed: " + m_protocol->lastError());
        m_usb.close();
        return false;
    }

    // Twenty-four zero offsets. Cached per unit, because measuring them costs
    // three burst captures and doing that on every connect would make opening
    // the device slow for no reason.
    if (m_calibrationPath.empty() ||
        !m_calibration.load(m_calibrationPath, device.serialNumber)) {
        if (!_measureZeroOffsets())
            JLOGC(JScopeLog::kHantek, JLogLevel::Warn)
                << "zero-offset calibration failed — readings will carry a per-channel "
                   "DC error that looks exactly like a real signal offset";
        else if (!m_calibrationPath.empty())
            m_calibration.save(m_calibrationPath, device.serialNumber);
    }

    if (!_applyConfigToDevice()) {
        _postError("configuration failed: " + m_protocol->lastError());
        m_usb.close();
        return false;
    }

    m_pool.provision(JHantek1008Tables::kChannelCount,
                     std::max(kMaxBurstSamplesPerChannel, kMaxRollSamplesPerChannel));
    m_open = true;

    // Push the generator's remembered state. Unlike a bench scope, the 1008C holds
    // NO configuration of its own -- it has no front panel and nothing survives a
    // replug -- so the application's state is the only version there is, and
    // pushing it here is not the same thing as overwriting a user's settings.
    //
    // The output follows what it was, which is off unless it was deliberately
    // switched on: connecting a scope must not start driving eight wires.
    // Started before anything else can go quiet on the bus.
    m_keepAliveRunning.store(true);
    m_keepAlive = std::thread(&JHantek1008Driver::_keepAliveLoop, this);

    // Safe to send from here: the acquisition thread is not started yet.
    if (!m_generator.flush())
        JLOGC(JScopeLog::kHantek, JLogLevel::Warn)
            << "generator setup was not accepted: " << m_generator.lastError();

    _setState(JScopeState::Idle);
    JLOGC(JScopeLog::kHantek, JLogLevel::Info) << "1008C ready";
    return true;
}

bool JHantek1008Driver::autoset() {
    if (!m_open) return false;

    // The hardware has no autoset, so this is host-side policy — the same
    // reasoning as Auto/Normal/Single. It matters more here than on most
    // instruments: the ranges are coarse (+/-0.41, +/-2.56, +/-20.48 V) and
    // getting it wrong either buries the signal in one division or clips it
    // flat, and a clipped square looks entirely plausible.
    const bool wasRunning = m_running.load();
    if (wasRunning) stop();

    // Measure on the WIDEST range, which is the only one guaranteed not to clip
    // whatever is connected. Measuring on a range that clips would report the
    // rail as the signal's amplitude and then confirm that same range as the
    // right choice.
    std::vector<double> widest(JHantek1008Tables::kChannelCount, 1.0);
    if (!m_protocol->setVerticalScales(widest)) return false;

    // Autoset forces: it is measuring what is there, not waiting for an event.
    if (!m_protocol->startBurstCapture(/*forceTrigger=*/true)) return false;
    if (!m_protocol->waitReady()) {
        JLOGC(JScopeLog::kHantek, JLogLevel::Warn) << "autoset: no record to measure";
        if (wasRunning) start(m_timebase.mode);
        return false;
    }

    std::vector<uint8_t> a, b;
    if (!m_protocol->readBurstHalf(JHantek1008Tables::kBurstHalfA, a)) return false;
    if (!m_protocol->readBurstHalf(JHantek1008Tables::kBurstHalfB, b)) return false;
    a.insert(a.end(), b.begin(), b.end());

    const auto shorts = JHantek1008Codec::toShorts(a.data(), a.size());
    const auto active = _activeChannels();
    const uint8_t autosetStride =
        JHantek1008Tables::burstStrideFor(static_cast<uint8_t>(active.size()));
    const size_t samples = JHantek1008Codec::samplesPerChannel(shorts.size(), autosetStride);
    if (samples == 0) {
        if (wasRunning) start(m_timebase.mode);
        return false;
    }

    // Headroom, so a signal that grows slightly after the autoset does not
    // immediately start clipping.
    constexpr double kHeadroom = 1.25;

    std::string summary;
    {
        std::lock_guard<std::mutex> lk(m_cfgMutex);
        for (size_t i = 0; i < active.size(); ++i) {
            const uint8_t ch = active[i];
            const double zero = m_calibration.zeroOffsetForVScale(1.0, ch);

            double peak = 0.0;
            for (size_t sIdx = 0; sIdx < samples; ++sIdx) {
                const double volts = JHantek1008Codec::toVolts(
                    shorts[sIdx * autosetStride + i], zero, 1.0);
                peak = std::max(peak, std::abs(volts));
            }
            peak *= kHeadroom;

            // The smallest LADDER STEP whose half-screen still contains the
            // peak, so the signal fills as much of the graticule as it can.
            double chosen = JHantek1008Tables::kVoltsPerDivSteps.back();
            for (double v : JHantek1008Tables::kVoltsPerDivSteps) {
                const double halfScreen = v * JHantek1008Tables::kVerticalDivisions * 0.5;
                if (halfScreen >= peak) { chosen = v; break; }
            }
            m_channels[ch].voltsPerDiv = chosen;
            summary += "  CH" + std::to_string(ch + 1) + " peak "
                     + std::to_string(peak / kHeadroom) + "V -> "
                     + std::to_string(m_channels[ch].voltsPerDiv) + "V/div";
        }
        m_configDirty = true;
    }

    JLOGC(JScopeLog::kHantek, JLogLevel::Info) << "autoset:" << summary;
    if (!_applyConfigToDevice()) return false;
    if (wasRunning) start(m_timebase.mode);
    return true;
}

bool JHantek1008Driver::_measureZeroOffsets() {
    JLOGC(JScopeLog::kHantek, JLogLevel::Info)
        << "measuring zero offsets on all three ranges (inputs should be at rest)";

    std::vector<uint8_t> all(JHantek1008Tables::kChannelCount);
    for (uint8_t c = 0; c < JHantek1008Tables::kChannelCount; ++c) all[c] = c;

    for (uint8_t rangeId = 1; rangeId <= JHantek1008Calibration::kRanges; ++rangeId) {
        const double vscale = JHantek1008Tables::vscaleForId(rangeId);

        if (!m_protocol->ping()) return false;
        if (!m_protocol->setVerticalScales(
                std::vector<double>(JHantek1008Tables::kChannelCount, vscale))) return false;
        if (!m_protocol->startAcquisition(JHantek1008Tables::kModeBurst)) return false;
        // Calibration measures a resting input; there is no edge to wait for.
        if (!m_protocol->arm(true)) return false;
        if (!m_protocol->waitReady()) return false;

        std::vector<uint8_t> a, b;
        if (!m_protocol->readBurstHalf(JHantek1008Tables::kBurstHalfA, a)) return false;
        if (!m_protocol->readBurstHalf(JHantek1008Tables::kBurstHalfB, b)) return false;
        a.insert(a.end(), b.begin(), b.end());

        const auto shorts = JHantek1008Codec::toShorts(a.data(), a.size());
        const size_t stride = JHantek1008Tables::burstStrideFor(
            JHantek1008Tables::kChannelCount);
        const size_t samples = shorts.size() / stride;
        if (samples == 0) {
            JLOGC(JScopeLog::kHantek, JLogLevel::Warn)
                << "range " << int(rangeId) << " returned no samples";
            return false;
        }

        std::string summary;
        for (uint8_t c = 0; c < JHantek1008Tables::kChannelCount; ++c) {
            double sum = 0.0;
            for (size_t s = 0; s < samples; ++s) sum += shorts[s * stride + c];
            const double mean = sum / static_cast<double>(samples);
            m_calibration.setZeroOffset(rangeId, c, mean);
            summary += " " + std::to_string(static_cast<int>(mean + 0.5));
        }
        JLOGC(JScopeLog::kHantek, JLogLevel::Info)
            << "vscale " << vscale << " zero offsets:" << summary
            << "  (" << samples << " samples averaged)";
    }

    m_calibration.markComplete();
    return true;
}

bool JHantek1008Driver::_applyConfigToDevice() {
    std::lock_guard<std::mutex> lk(m_cfgMutex);

    const auto active  = _activeChannels();
    const auto vscales = _perChannelVScale();
    const uint8_t timeDivId = JHantek1008Tables::nsPerDivIdFor(m_timebase.secondsPerDiv);

    const uint8_t src = std::min<uint8_t>(m_trigger.sourceChannel,
                                          JHantek1008Tables::kChannelCount - 1);
    const double vscale = vscales[src];
    const double zero   = m_calibration.zeroOffsetForVScale(vscale, src);
    const uint16_t level = static_cast<uint16_t>(
        clampTriggerLevelCounts(m_trigger.levelVolts, vscale, zero));

    // recordLength 0 means "let the device decide", which for this device means
    // its power-on default — so that is what gets sent rather than nothing.
    const uint16_t record = m_timebase.recordLength > 0
        ? static_cast<uint16_t>(std::min<uint32_t>(m_timebase.recordLength,
                                                   JHantek1008Tables::kMaxRecordSamples))
        : JHantek1008Tables::kDefaultRecordSamples;

    const uint8_t percent = static_cast<uint8_t>(
        std::clamp(m_timebase.triggerPosition, 0.0, 1.0) * 100.0 + 0.5);
    const bool ok = m_protocol->initialisePhase3(
        active, vscales, timeDivId, record, src,
        m_trigger.slope != JScopeTriggerSlope::Falling, level, percent);
    if (ok) m_configDirty = false;
    return ok;
}

void JHantek1008Driver::close() {
    if (!m_open) return;
    stop();
    // Stopped before the protocol is destroyed: it pings through it.
    m_keepAliveRunning.store(false);
    if (m_keepAlive.joinable()) m_keepAlive.join();
    // Detach BEFORE the protocol dies: the generator holds a raw pointer to it and
    // would otherwise be left aimed at freed memory until the next open.
    m_generator.attach(nullptr);
    m_usb.close();
    m_protocol.reset();
    m_open = false;
    _setState(JScopeState::Closed);
    JLOGC(JScopeLog::kHantek, JLogLevel::Info) << "1008C closed";
}

// WHOSE THREAD SENDS. While the acquisition thread is running it owns the bulk
// pipe -- it holds no lock across a capture, so there is no way to interleave with
// it safely -- and the generator's bytes have to wait for it. While it is not
// running there is no other thread to collide with, and waiting would mean a
// control that does nothing until the user presses Run.
// Ping while nothing else is using the bus. Ten milliseconds is the reference's
// interval; the device wants to hear from the host and does not much care what.
void JHantek1008Driver::_keepAliveLoop() {
    JLOGC(JScopeLog::kHantek, JLogLevel::Debug) << "keep-alive up";
    while (m_keepAliveRunning.load(std::memory_order_acquire)) {
        if (!m_running.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lk(m_busMutex);
            // Re-checked under the lock: acquisition may have started while this
            // was waiting for it, and then the bus is not ours to talk on.
            if (!m_running.load(std::memory_order_acquire) && m_protocol)
                m_protocol->ping();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    JLOGC(JScopeLog::kHantek, JLogLevel::Debug) << "keep-alive down";
}

void JHantek1008Driver::_generatorChanged() {
    if (!m_open) return;
    if (m_running.load(std::memory_order_acquire)) {
        m_generatorDirty.store(true, std::memory_order_release);
        return;
    }
    std::lock_guard<std::mutex> lk(m_busMutex);
    if (!m_generator.flush())
        JLOGC(JScopeLog::kHantek, JLogLevel::Warn)
            << "generator: " << m_generator.lastError();
}

bool JHantek1008Driver::applyChannel(uint8_t ch, const JScopeChannelConfig& cfg) {
    if (ch >= JHantek1008Tables::kChannelCount) return false;
    std::lock_guard<std::mutex> lk(m_cfgMutex);

    JScopeChannelConfig q = cfg;
    // Quantise to the 1-2-5 ladder the user sees, by ratio rather than linear
    // distance — on a 1-2-5 scale 3 V is nearer 2 V than 5 V.
    const double ratio = (q.probeRatio > 0.0) ? q.probeRatio : 1.0;
    q.probeRatio = ratio;
    double best = JHantek1008Tables::kVoltsPerDivSteps.back() * ratio, bestErr = 1.0e300;
    for (double v : JHantek1008Tables::kVoltsPerDivSteps) {
        const double err = std::abs(std::log(cfg.voltsPerDiv / (v * ratio)));
        if (err < bestErr) { bestErr = err; best = v * ratio; }
    }
    q.voltsPerDiv = best;

    // PUBLISH THE LADDER THE USER WILL SEE. This device has no probe hardware —
    // a probe only changes what a count means — so the app owns the ratio and
    // the steps on offer move with it. The panel lists exactly what is here and
    // does no arithmetic of its own; the DSO2D15, which applies its own probe
    // and reports volts at the tip already, publishes an unscaled list for the
    // same reason. Two instruments, two conversions, each in its own driver.
    if (ch < m_caps.channels.size()) {
        auto& steps = m_caps.channels[ch].voltsPerDiv;
        steps.clear();
        steps.reserve(JHantek1008Tables::kVoltsPerDivSteps.size());
        for (double v : JHantek1008Tables::kVoltsPerDivSteps) steps.push_back(v * ratio);
    }
    // Refused features are reported as their only possible value rather than
    // silently stored: the caller reads back what was really applied.
    q.offsetVolts = 0.0;
    q.coupling    = JScopeCoupling::DC;
    q.inverted    = false;
    q.bandwidthLimited = false;

    m_channels[ch] = q;
    m_configDirty  = true;

    JLOGC(JScopeLog::kConfig, JLogLevel::Debug)
        << "CH" << int(ch + 1) << " enabled=" << q.enabled
        << " V/div=" << q.voltsPerDiv << " (asked " << cfg.voltsPerDiv << ")"
        << " probe=x" << q.probeRatio;
    return true;
}

bool JHantek1008Driver::applyTimebase(const JScopeTimebaseConfig& cfg) {
    std::lock_guard<std::mutex> lk(m_cfgMutex);
    JScopeTimebaseConfig q = cfg;

    // Report what a division REALLY spans, not the round number that was asked
    // for. The panel offers the standard ladder to choose from; what comes back
    // is what the chosen rate code actually gives, because the graticule shows
    // the whole record and the axis has to describe it.
    const uint8_t chosen = JHantek1008Tables::nsPerDivIdFor(cfg.secondsPerDiv);
    q.secondsPerDiv = JHantek1008Tables::displaySecondsPerDivFor(chosen);

    // Roll rates are a fixed set; nearest by ratio.
    double best = 440.0, bestErr = 1.0e300;
    for (const auto& r : JHantek1008Tables::kRollRates) {
        const double err = std::abs(std::log(cfg.sampleRate / r.rate));
        if (err < bestErr) { bestErr = err; best = r.rate; }
    }
    q.sampleRate = best;
    q.recordLength = 0;                 // the device states the real length

    m_timebase = q;
    m_configDirty = true;

    JLOGC(JScopeLog::kConfig, JLogLevel::Debug)
        << "timebase mode=" << jScopeAcquisitionModeName(q.mode)
        << " s/div=" << q.secondsPerDiv << " (asked " << cfg.secondsPerDiv << ")"
        << " rollRate=" << q.sampleRate << "Sa/s";
    return true;
}

bool JHantek1008Driver::applyTrigger(const JScopeTriggerConfig& cfg) {
    std::lock_guard<std::mutex> lk(m_cfgMutex);
    JScopeTriggerConfig q = cfg;
    if (q.sourceChannel >= JHantek1008Tables::kChannelCount) q.sourceChannel = 0;
    m_trigger = q;
    m_configDirty = true;

    JLOGC(JScopeLog::kConfig, JLogLevel::Debug)
        << "trigger mode=" << jScopeTriggerModeName(q.mode)
        << " slope=" << jScopeTriggerSlopeName(q.slope)
        << " source=CH" << int(q.sourceChannel + 1)
        << " level=" << q.levelVolts << "V";
    return true;
}

const JScopeChannelConfig& JHantek1008Driver::channelConfig(uint8_t ch) const {
    std::lock_guard<std::mutex> lk(m_cfgMutex);
    return m_channels[ch < JHantek1008Tables::kChannelCount ? ch : 0];
}

bool JHantek1008Driver::start(JScopeAcquisitionMode mode) {
    if (!m_open) return false;
    if (m_running.load()) return true;
    if (!jScopeAcquisitionModeSupported(m_caps.acquisitionModes, mode)) return false;

    // Reap a thread that retired itself. The loop exits on its own after a fatal
    // transport error or a completed single shot, which clears m_running but
    // leaves the std::thread joinable — and ASSIGNING OVER A JOINABLE THREAD
    // CALLS std::terminate. Unplug the scope, press Run, and the application
    // aborts. stop() has the mirror image of this guard.
    if (m_thread.joinable()) m_thread.join();

    m_mode.store(mode);
    m_singleShot.store(false);
    m_sequence    = 0;
    m_streamIndex = 0;
    m_pool.resetDropped();
    m_startTime = std::chrono::steady_clock::now();
    m_running.store(true);
    m_thread = std::thread(&JHantek1008Driver::_runLoop, this);

    JLOGC(JScopeLog::kHantek, JLogLevel::Info)
        << "acquisition started in " << jScopeAcquisitionModeName(mode) << " mode";
    _setState(mode == JScopeAcquisitionMode::Streaming ? JScopeState::Running
                                                       : JScopeState::Armed);
    return true;
}

bool JHantek1008Driver::single() {
    // The mode is READ under the lock and the lock is then dropped. start() joins
    // a retired acquisition thread, and that thread takes m_cfgMutex itself --
    // holding it across the join is a deadlock waiting for the timing where the
    // thread has not finished its last iteration.
    JScopeAcquisitionMode mode;
    {
        std::lock_guard<std::mutex> lk(m_cfgMutex);
        mode = m_timebase.mode;
    }
    if (!start(mode)) return false;
    m_singleShot.store(true);
    return true;
}

void JHantek1008Driver::stop() {
    // Clear the flag AND join unconditionally: the loop can retire itself after a
    // single shot or a fatal error, leaving the flag false and the thread
    // joinable, and destroying a joinable std::thread calls std::terminate.
    m_running.store(false);
    if (!m_thread.joinable()) return;
    m_thread.join();
    _setState(m_open ? JScopeState::Stopped : JScopeState::Closed);
    JLOGC(JScopeLog::kHantek, JLogLevel::Info)
        << "acquisition stopped after " << m_sequence << " frame(s), "
        << m_pool.dropped() << " dropped";
}

void JHantek1008Driver::_fillFrameHeader(JScopeFrame& f, const std::vector<uint8_t>& active,
                                         double sampleInterval, int32_t triggerIndex,
                                         bool triggered) {
    const auto vscales = _perChannelVScale();
    for (size_t i = 0; i < active.size(); ++i) {
        const uint8_t ch = active[i];
        const double vscale = vscales[ch];
        f.header.channelIds[i]       = ch;
        // The volts/div the USER chose, not the hardware range's maximum: this
        // is what the graticule is labelled with and what the trace is scaled by.
        f.header.voltsPerDiv[i]      = static_cast<float>(m_channels[ch].voltsPerDiv);
        // Scaled by the probe, so every volt downstream — trace, cursors,
        // measurements, capture — is the voltage at the TIP rather than at the
        // BNC. Without this the probe control changed the label and nothing else.
        f.header.countsToVolts[i]    = static_cast<float>(
            JHantek1008Codec::countsToVolts(vscale) * m_channels[ch].probeRatio);
        // The measured per-unit, per-range resting count. Without it every
        // channel carries a DC error indistinguishable from a real one.
        f.header.zeroOffsetCounts[i] = static_cast<float>(
            m_calibration.zeroOffsetForVScale(vscale, ch));
    }
    f.header.sequence           = m_sequence++;
    f.header.timestampSeconds   = std::chrono::duration<double>(
                                      std::chrono::steady_clock::now() - m_startTime).count();
    f.header.sampleInterval     = sampleInterval;
    f.header.triggerSampleIndex = triggerIndex;
    f.header.triggered          = triggered;
    f.header.mode               = m_mode.load();
}

bool JHantek1008Driver::_acquireBurst() {
    std::vector<uint8_t> active;
    JScopeTriggerMode    sweep;
    double               secondsPerDiv;
    {
        std::lock_guard<std::mutex> lk(m_cfgMutex);
        active        = _activeChannels();
        sweep         = m_trigger.mode;
        secondsPerDiv = m_timebase.secondsPerDiv;
    }

    const uint8_t timeDivId = JHantek1008Tables::nsPerDivIdFor(secondsPerDiv);
    if (!m_protocol->startBurstCapture(/*forceTrigger=*/false)) return false;

    // The sweep modes are host-side policy: the hardware has none. Auto gives up
    // waiting and sweeps anyway rather than leaving the screen blank; Normal and
    // Single keep waiting.
    const int attempts = readyAttemptsFor(
        JHantek1008Tables::recordDurationFor(timeDivId),
        m_protocol->timing().readyPollInterval,
        (sweep == JScopeTriggerMode::Auto) ? kAutoReadyMargin : kNormalReadyMargin);
    bool triggered = m_protocol->waitReady(attempts);
    if (!triggered && sweep != JScopeTriggerMode::Auto) {
        _setState(JScopeState::Armed);
        return true;                    // still waiting is not a failure
    }
    if (!triggered) {
        // AUTO, and nothing came. Sweep anyway so the screen is not blank — but
        // only NOW, after waiting. Sending 0xc2 up front, which is what the arm
        // sequence used to do unconditionally, starts the record immediately and
        // unaligned: the trace then jumps about however good the trigger is.
        // Measured on the crank signal, first-edge spread across five frames:
        // 19 samples at 5 ms/div forced, 2 unforced; 12 at 10.8 ms/div, 1.
        m_protocol->forceTrigger();
        m_protocol->waitReady(kSweepReadyMargin);
    }

    std::vector<uint8_t> halfB;
    if (!m_protocol->readBurstHalf(JHantek1008Tables::kBurstHalfA, m_rawBytes)) return false;
    if (!m_protocol->readBurstHalf(JHantek1008Tables::kBurstHalfB, halfB)) return false;

    // The device states each half's length itself and they are NOT guaranteed to
    // be equal, or to be a whole number of interleave groups. Joining them as
    // raw bytes and deinterleaving the result as one stream therefore rotates
    // every channel from the join onwards whenever half A does not end on a
    // group boundary — which draws as the trace changing character halfway
    // across the record, with one channel's signal appearing on another's.
    //
    // Logged on every change because it is a device fact worth seeing, and
    // because it is what distinguishes this from a signal problem.
    if (m_rawBytes.size() != m_lastHalfBytes[0] || halfB.size() != m_lastHalfBytes[1]) {
        m_lastHalfBytes[0] = m_rawBytes.size();
        m_lastHalfBytes[1] = halfB.size();
        JLOGC(JScopeLog::kHantek, JLogLevel::Info)
            << "burst halves: A=" << m_rawBytes.size() << " B=" << halfB.size()
            << " bytes  (stride " << int(JHantek1008Tables::burstStrideFor(
                                            static_cast<uint8_t>(active.size())))
            << ", A holds " << (m_rawBytes.size() / 2) << " shorts)";
    }
    const uint8_t stride =
        JHantek1008Tables::burstStrideFor(static_cast<uint8_t>(active.size()));

    // Each half is converted and TRUNCATED TO A WHOLE NUMBER OF GROUPS before
    // the next is appended, so every half begins on channel 0. Appending the raw
    // bytes and converting once is what let a ragged half A rotate the lanes for
    // the whole of half B.
    m_shorts.clear();
    for (const std::vector<uint8_t>* half : { &m_rawBytes, &halfB }) {
        const std::vector<uint16_t> s = JHantek1008Codec::toShorts(half->data(), half->size());
        const size_t whole = (s.size() / stride) * stride;
        m_shorts.insert(m_shorts.end(), s.begin(), s.begin() + whole);
    }
    const size_t samples = JHantek1008Codec::samplesPerChannel(m_shorts.size(), stride);
    if (samples == 0) return true;

    JScopeFrame* f = m_pool.acquire();
    if (!f) return true;
    if (!f->shape(static_cast<uint8_t>(active.size()),
                  static_cast<uint32_t>(std::min<size_t>(samples, f->maxSamples())))) {
        m_pool.release(f);
        return true;
    }

    JHantek1008Codec::deinterleave(m_shorts.data(), m_shorts.size(),
                                   static_cast<uint8_t>(active.size()), stride,
                                   f->plane(0), f->maxSamples());

    // Derive the interval from the RECORD the device actually returned, not from
    // an assumed samples-per-division. The reference's comment says "around 25
    // samples per div"; this unit returns 1000 samples across 10 divisions,
    // which is 100 — and taking the comment at its word put the time axis out by
    // exactly four, so a 1 kHz signal read as a rock-steady, entirely wrong
    // 249.99 Hz. The record length is stated by the device on every capture, so
    // deriving from it cannot drift.
    // From the device's own constant table, not from the s/div label. The label
    // describes a kRecordLenForId-sample sweep; the firmware always returns
    // kDefaultRecordSamples, so at the four codes where those differ the record
    // covers proportionally more time than the label claims. Deriving dt from
    // the label is what made a 1 kHz square read 1052 Hz at 5 ms/div and 1111 Hz
    // at 10 ms/div.
    const double sampleInterval = JHantek1008Tables::sampleIntervalFor(
        timeDivId, static_cast<uint8_t>(active.size()));
    // Where the trigger actually sits, which is what 0xac was told. Hardcoding
    // the middle was only right while the position could not be changed.
    double position = 0.5;
    {
        std::lock_guard<std::mutex> lk(m_cfgMutex);
        position = std::clamp(m_timebase.triggerPosition, 0.0, 1.0);
    }
    _fillFrameHeader(*f, active, sampleInterval,
                     static_cast<int32_t>(f->header.sampleCount * position), triggered);
    _publish(f);
    _setState(triggered ? JScopeState::Triggered : JScopeState::Running);
    return true;
}

bool JHantek1008Driver::_acquireRoll() {
    std::vector<uint8_t> active;
    double rate;
    double windowSeconds;
    {
        std::lock_guard<std::mutex> lk(m_cfgMutex);
        active = _activeChannels();
        rate   = m_timebase.sampleRate;
        // The span the graticule is showing, so seconds/div means the same thing
        // in roll mode as it does in a triggered one.
        windowSeconds = std::clamp(
            m_timebase.secondsPerDiv * JHantek1008Tables::kHorizontalDivisions,
            kRollWindowMinSeconds, kRollWindowMaxSeconds);
    }

    uint16_t ready = 0;
    if (!m_protocol->rollReadyLength(ready)) return false;
    if (ready == 0) {
        m_protocol->ping();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return true;                     // nothing waiting yet is normal
    }

    if (!m_protocol->readRollBytes(ready, m_rawBytes)) return false;
    m_shorts = JHantek1008Codec::toShorts(m_rawBytes.data(), m_rawBytes.size());

    const uint8_t rollStride =
        JHantek1008Tables::rollStrideFor(static_cast<uint8_t>(active.size()));
    const size_t samples = JHantek1008Codec::samplesPerChannel(m_shorts.size(), rollStride);
    if (samples == 0) return true;

    // De-interleave this poll's chunk into the accumulator rather than straight
    // into a frame.
    std::array<std::vector<int16_t>, JHantek1008Tables::kChannelCount> chunk;
    std::vector<int16_t> flat(active.size() * samples);
    JHantek1008Codec::deinterleave(m_shorts.data(), m_shorts.size(),
                                   static_cast<uint8_t>(active.size()), rollStride,
                                   flat.data(), samples);

    if (!m_rollWindowOpen) {
        for (auto& v : m_rollAccumulator) v.clear();
        m_rollWindowStart = std::chrono::steady_clock::now();
        m_rollWindowOpen  = true;
    }
    for (size_t i = 0; i < active.size(); ++i) {
        std::vector<int16_t>& dst = m_rollAccumulator[i];
        const int16_t* src = flat.data() + i * samples;
        dst.insert(dst.end(), src, src + samples);
    }

    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - m_rollWindowStart).count();
    const size_t have = m_rollAccumulator[0].size();
    if (elapsed < windowSeconds && have < kMaxRollSamplesPerChannel) return true;
    if (have < kRollWindowMinSamples) return true;

    JScopeFrame* f = m_pool.acquire();
    if (!f) { m_rollWindowOpen = false; return true; }
    const uint32_t count = static_cast<uint32_t>(std::min<size_t>(have, f->maxSamples()));
    if (!f->shape(static_cast<uint8_t>(active.size()), count)) {
        m_pool.release(f);
        m_rollWindowOpen = false;
        return true;
    }
    for (size_t i = 0; i < active.size(); ++i)
        std::copy(m_rollAccumulator[i].begin(), m_rollAccumulator[i].begin() + count,
                  f->plane(static_cast<uint8_t>(i)));

    // The interval is MEASURED over the window rather than taken from the
    // nominal rate. The device samples faster than nominal when fewer channels
    // are active, by a factor the reference tabulates but nothing has validated
    // — and a time axis derived from an unvalidated table is a time axis that
    // reports the wrong frequency with total confidence.
    const double measuredInterval = elapsed / static_cast<double>(have);
    const double nominalFactor = JHantek1008Tables::kActualRateFactor[
        std::min<size_t>(active.size(), 8) - 1];
    JLOGC(JScopeLog::kHantek, JLogLevel::Debug)
        << "roll window: " << have << " samples in " << elapsed << " s (asked "
        << windowSeconds << " s) = " << (1.0 / measuredInterval) << " Sa/s measured, "
        << (rate * nominalFactor) << " Sa/s nominal";

    _fillFrameHeader(*f, active, measuredInterval, -1, false);
    f->header.startSampleIndex = m_streamIndex;
    m_streamIndex += count;

    _publish(f);
    m_rollWindowOpen = false;
    return true;
}

void JHantek1008Driver::_runLoop() {
    JLOGC(JScopeLog::kHantek, JLogLevel::Debug) << "acquisition thread up";

    // Roll mode has to be (re)started whenever the configuration changes,
    // because reapplying the configuration sends a ns/div id through 0xa3 and
    // the device then stalls on the next roll command.
    bool modeStarted = false;

    while (m_running.load(std::memory_order_acquire)) {
        // THE BUS IS OURS FOR A WHOLE ITERATION. The keep-alive pings whenever
        // acquisition is not running, and a capture is a sequence -- arm, poll,
        // read both halves -- that a ping dropped into the middle of would desync
        // exactly the way a generator write from the UI thread once did.
        std::lock_guard<std::mutex> busLock(m_busMutex);

        // Configuration changes are applied BETWEEN acquisitions, on this thread,
        // so the UI thread never touches USB and no mutex is held across a
        // transfer.
        // The generator goes out here for the same reason and in the same place:
        // this thread owns the pipe between captures. A change made while running
        // waits for this point rather than being written underneath a capture.
        if (m_generatorDirty.exchange(false, std::memory_order_acq_rel)) {
            if (!m_generator.flush())
                JLOGC(JScopeLog::kHantek, JLogLevel::Warn)
                    << "generator: " << m_generator.lastError();
        }

        bool needsReconfigure = false;
        {
            std::lock_guard<std::mutex> lk(m_cfgMutex);
            needsReconfigure = m_configDirty;
        }
        if (needsReconfigure) {
            if (!_applyConfigToDevice()) {
                _postError("reconfiguration failed: " + m_protocol->lastError());
                break;
            }
            modeStarted = false;
        }

        if (!modeStarted && m_mode.load() == JScopeAcquisitionMode::Streaming) {
            double rate;
            {
                std::lock_guard<std::mutex> lk(m_cfgMutex);
                rate = m_timebase.sampleRate;
            }
            const uint8_t rateId = JHantek1008Tables::rollRateIdFor(rate);
            JLOGC(JScopeLog::kHantek, JLogLevel::Debug)
                << "starting roll mode at " << rate << " Sa/s (id 0x"
                << std::hex << int(rateId) << std::dec << ")";
            if (!m_protocol->startRollMode(rateId)) {
                _postError("could not start roll mode: " + m_protocol->lastError());
                break;
            }
        }
        modeStarted = true;

        const bool ok = (m_mode.load() == JScopeAcquisitionMode::Streaming)
                      ? _acquireRoll() : _acquireBurst();
        if (!ok) {
            _postError("acquisition failed: " + m_protocol->lastError());
            _setState(JScopeState::Error);
            break;
        }
        if (m_singleShot.load() && m_sequence > 0) break;
    }

    m_running.store(false);
    JLOGC(JScopeLog::kHantek, JLogLevel::Debug) << "acquisition thread down";
}

} // inline namespace jf

J_REGISTER_SCOPE_DRIVER(JHantek1008Driver, "hantek-1008c", "Hantek 1008C",
                        &jf::JHantek1008Driver::enumerate)
