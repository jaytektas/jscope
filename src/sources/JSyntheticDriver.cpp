#include "JSyntheticDriver.h"
#include "scope/JScopeDriverRegistry.h"
#include "scope/JScopeLog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

inline namespace jf {

namespace {

// The synthetic instrument's own specification. These are device facts, not
// visual constants — a synthetic scope is still a scope with a stated ADC width
// and a stated set of ranges.
constexpr uint8_t  kChannels          = 8;
constexpr uint8_t  kAdcBits           = 12;
constexpr int32_t  kCountsMin         = 0;
constexpr int32_t  kCountsMax         = 4095;
constexpr float    kCountsMidscale    = 2048.0f;
constexpr uint8_t  kVerticalDivisions = 8;
constexpr uint8_t  kHorizontalDivisions = 10;
constexpr uint32_t kRecordLength      = 4096;
constexpr double   kTwoPi             = 6.283185307179586476925286766559;

// Volts across the full ADC span at 1 V/div, i.e. verticalDivisions volts.
constexpr double kFullScaleDivisions = static_cast<double>(kVerticalDivisions);

const double kVoltsPerDiv[] = {
    0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5,
    1.0,   2.0,   5.0,   10.0
};

const double kSecondsPerDiv[] = {
    1.0e-6, 2.0e-6, 5.0e-6, 1.0e-5, 2.0e-5, 5.0e-5,
    1.0e-4, 2.0e-4, 5.0e-4, 1.0e-3, 2.0e-3, 5.0e-3,
    1.0e-2, 2.0e-2, 5.0e-2, 1.0e-1, 2.0e-1, 5.0e-1, 1.0
};

const double kStreamRates[] = { 10.0, 50.0, 100.0, 440.0, 1000.0, 5000.0 };

// Pick the supported step nearest a request, so the UI always ends up showing a
// value the "device" really has.
double nearestStep(const double* steps, size_t n, double want) {
    double best = steps[0];
    double bestErr = std::abs(std::log(want / steps[0]));
    for (size_t i = 1; i < n; ++i) {
        const double err = std::abs(std::log(want / steps[i]));
        if (err < bestErr) { bestErr = err; best = steps[i]; }
    }
    return best;
}

} // namespace

JSyntheticDriver::JSyntheticDriver() {
    m_caps.driverId          = m_driverId;
    m_caps.model             = "Synthetic source";
    m_caps.serialNumber      = "SYNTH-0001";
    m_caps.adcBits           = kAdcBits;
    m_caps.countsMin         = kCountsMin;
    m_caps.countsMax         = kCountsMax;
    m_caps.verticalDivisions = kVerticalDivisions;
    m_caps.horizontalDivisions = kHorizontalDivisions;
    m_caps.maxSimultaneousChannels = kChannels;

    for (uint8_t i = 0; i < kChannels; ++i) {
        JScopeChannelCaps c;
        c.label       = "CH" + std::to_string(i + 1);
        c.voltsPerDiv.assign(std::begin(kVoltsPerDiv), std::end(kVoltsPerDiv));
        c.probeRatios = { 1.0, 10.0, 100.0 };
        c.couplings   = { JScopeCoupling::DC, JScopeCoupling::AC, JScopeCoupling::Ground };
        c.offsetRangeVolts  = 40.0;
        c.canInvert         = true;
        c.canBandwidthLimit = true;
        m_caps.channels.push_back(std::move(c));
    }

    m_caps.acquisitionModes = jScopeAcquisitionModeBit(JScopeAcquisitionMode::Windowed)
                            | jScopeAcquisitionModeBit(JScopeAcquisitionMode::Streaming);
    m_caps.secondsPerDiv.assign(std::begin(kSecondsPerDiv), std::end(kSecondsPerDiv));
    m_caps.streamSampleRates.assign(std::begin(kStreamRates), std::end(kStreamRates));
    m_caps.memoryDepths = { 1024, 4096, 16384, 65536 };

    m_caps.triggerModes = jScopeTriggerModeBit(JScopeTriggerMode::Auto)
                        | jScopeTriggerModeBit(JScopeTriggerMode::Normal)
                        | jScopeTriggerModeBit(JScopeTriggerMode::Single);
    m_caps.hasHardwareTrigger  = false;   // it is software, and says so
    m_caps.hasTriggerPosition  = true;
    m_caps.hasForceTrigger     = true;
    m_caps.hasAutoset          = true;

    // A distinguishable signal per channel, so an eight-trace display is
    // immediately readable and a wrong channel mapping is obvious on sight.
    for (uint8_t i = 0; i < kChannels; ++i) {
        JSyntheticChannel s;
        s.waveform       = static_cast<JSyntheticWaveform>(i % 4);   // Sine..Ramp
        s.amplitudeVolts = 1.0 + 0.25 * i;
        s.frequencyHz    = 1000.0 * (1 << (i % 3));                  // 1k, 2k, 4k
        s.phaseRadians   = (kTwoPi / kChannels) * i;
        s.noiseVolts     = 0.0;
        m_signals[i] = s;

        m_channels[i].enabled     = (i < 4);   // four on by default; eight is a wall
        m_channels[i].voltsPerDiv = 1.0;
        m_channels[i].offsetVolts = 0.0;
    }

    m_timebase.mode          = JScopeAcquisitionMode::Windowed;
    m_timebase.secondsPerDiv = 2.0e-4;
    m_timebase.recordLength  = kRecordLength;
    m_timebase.sampleRate    = 440.0;

    JLOGC(JScopeLog::kSynth, JLogLevel::Debug)
        << "constructed: " << static_cast<int>(kChannels) << " channels, "
        << static_cast<int>(kAdcBits) << "-bit, "
        << m_caps.secondsPerDiv.size() << " timebase steps";
}

JSyntheticDriver::~JSyntheticDriver() { close(); }

std::vector<JScopeDeviceInfo> JSyntheticDriver::enumerate() {
    JScopeDeviceInfo info;
    info.driverId    = "synthetic";
    info.simulated   = true;      // generated, not measured
    info.displayName = "Synthetic source (no hardware)";
    return { info };
}

bool JSyntheticDriver::open(const JScopeDeviceInfo& device) {
    if (m_open) {
        JLOGC(JScopeLog::kSynth, JLogLevel::Warn) << "open() on an already-open driver";
        return true;
    }
    JLOGC(JScopeLog::kSynth, JLogLevel::Info) << "opening '" << device.displayName << "'";
    m_pool.provision(kChannels, static_cast<uint32_t>(m_caps.memoryDepths.back()));
    m_open = true;
    _setState(JScopeState::Idle);
    return true;
}

void JSyntheticDriver::close() {
    if (!m_open) return;
    JLOGC(JScopeLog::kSynth, JLogLevel::Info) << "closing";
    stop();
    m_open = false;
    _setState(JScopeState::Closed);
}

bool JSyntheticDriver::applyChannel(uint8_t ch, const JScopeChannelConfig& cfg) {
    if (ch >= kChannels) {
        JLOGC(JScopeLog::kSynth, JLogLevel::Error) << "applyChannel: no channel " << int(ch);
        return false;
    }
    std::lock_guard<std::mutex> lk(m_cfgMutex);
    JScopeChannelConfig q = cfg;
    q.voltsPerDiv = nearestStep(kVoltsPerDiv, std::size(kVoltsPerDiv), cfg.voltsPerDiv);
    const double limit = m_caps.channels[ch].offsetRangeVolts;
    q.offsetVolts = std::clamp(cfg.offsetVolts, -limit, limit);
    m_channels[ch] = q;

    JLOGC(JScopeLog::kConfig, JLogLevel::Debug)
        << "CH" << int(ch + 1) << " enabled=" << q.enabled
        << " V/div=" << q.voltsPerDiv << " (asked " << cfg.voltsPerDiv << ")"
        << " offset=" << q.offsetVolts << "V"
        << " coupling=" << jScopeCouplingName(q.coupling)
        << " probe=x" << q.probeRatio << (q.inverted ? " inverted" : "");
    return true;
}

bool JSyntheticDriver::applyTimebase(const JScopeTimebaseConfig& cfg) {
    std::lock_guard<std::mutex> lk(m_cfgMutex);
    JScopeTimebaseConfig q = cfg;
    q.secondsPerDiv   = nearestStep(kSecondsPerDiv, std::size(kSecondsPerDiv), cfg.secondsPerDiv);
    q.sampleRate      = nearestStep(kStreamRates,   std::size(kStreamRates),   cfg.sampleRate);
    q.triggerPosition = std::clamp(cfg.triggerPosition, 0.0, 1.0);
    if (q.recordLength == 0) q.recordLength = kRecordLength;
    m_timebase = q;

    JLOGC(JScopeLog::kConfig, JLogLevel::Debug)
        << "timebase mode=" << jScopeAcquisitionModeName(q.mode)
        << " s/div=" << q.secondsPerDiv << " (asked " << cfg.secondsPerDiv << ")"
        << " record=" << q.recordLength
        << " trigPos=" << q.triggerPosition
        << " streamRate=" << q.sampleRate << "Sa/s";
    return true;
}

bool JSyntheticDriver::applyTrigger(const JScopeTriggerConfig& cfg) {
    std::lock_guard<std::mutex> lk(m_cfgMutex);
    JScopeTriggerConfig q = cfg;
    if (q.sourceChannel >= kChannels) q.sourceChannel = 0;
    m_trigger = q;

    JLOGC(JScopeLog::kConfig, JLogLevel::Debug)
        << "trigger mode=" << jScopeTriggerModeName(q.mode)
        << " slope=" << jScopeTriggerSlopeName(q.slope)
        << " source=CH" << int(q.sourceChannel + 1)
        << " level=" << q.levelVolts << "V"
        << " autoTimeout=" << q.autoTimeoutSeconds << "s";
    return true;
}

const JScopeChannelConfig& JSyntheticDriver::channelConfig(uint8_t ch) const {
    std::lock_guard<std::mutex> lk(m_cfgMutex);
    return m_channels[ch < kChannels ? ch : 0];
}

void JSyntheticDriver::setSignal(uint8_t ch, const JSyntheticChannel& sig) {
    if (ch >= kChannels) return;
    std::lock_guard<std::mutex> lk(m_cfgMutex);
    m_signals[ch] = sig;
    JLOGC(JScopeLog::kSynth, JLogLevel::Debug)
        << "CH" << int(ch + 1) << " signal=" << jSyntheticWaveformName(sig.waveform)
        << " " << sig.amplitudeVolts << "Vp @ " << sig.frequencyHz << "Hz"
        << " offset=" << sig.offsetVolts << "V noise=" << sig.noiseVolts << "V";
}

JSyntheticChannel JSyntheticDriver::signal(uint8_t ch) const {
    std::lock_guard<std::mutex> lk(m_cfgMutex);
    return m_signals[ch < kChannels ? ch : 0];
}

bool JSyntheticDriver::start(JScopeAcquisitionMode mode) {
    if (!m_open) {
        JLOGC(JScopeLog::kSynth, JLogLevel::Error) << "start() on a closed driver";
        return false;
    }
    if (m_running.load()) {
        JLOGC(JScopeLog::kSynth, JLogLevel::Debug) << "start(): already running";
        return true;
    }

    // Reap a thread that retired itself — a completed single shot clears
    // m_running but leaves the thread joinable, and assigning over a joinable
    // std::thread calls std::terminate.
    if (m_thread.joinable()) m_thread.join();
    if (!jScopeAcquisitionModeSupported(m_caps.acquisitionModes, mode)) {
        _postError("acquisition mode not supported");
        return false;
    }

    m_mode.store(mode);
    m_singleShot.store(false);
    m_sequence    = 0;
    m_streamIndex = 0;
    m_phaseTime   = 0.0;
    m_pool.resetDropped();
    m_startTime = std::chrono::steady_clock::now();
    m_running.store(true);
    m_thread = std::thread(&JSyntheticDriver::_runLoop, this);

    JLOGC(JScopeLog::kSynth, JLogLevel::Info)
        << "started, mode=" << jScopeAcquisitionModeName(mode);
    _setState(mode == JScopeAcquisitionMode::Streaming ? JScopeState::Running : JScopeState::Armed);
    return true;
}

bool JSyntheticDriver::single() {
    if (!start(m_timebase.mode)) return false;
    m_singleShot.store(true);
    JLOGC(JScopeLog::kSynth, JLogLevel::Info) << "single shot armed";
    return true;
}

void JSyntheticDriver::stop() {
    // Clear the flag AND join unconditionally: a completed single shot retires
    // the loop itself, leaving the flag false but the thread joinable, and
    // destroying a joinable std::thread calls std::terminate.
    m_running.store(false);
    if (!m_thread.joinable()) return;
    m_thread.join();
    JLOGC(JScopeLog::kSynth, JLogLevel::Info)
        << "stopped after " << m_sequence << " frame(s), " << m_pool.dropped() << " dropped";
    _setState(m_open ? JScopeState::Stopped : JScopeState::Closed);
}

bool JSyntheticDriver::forceTrigger() {
    if (!m_running.load()) return false;
    m_forceTrigger.store(true);
    JLOGC(JScopeLog::kSynth, JLogLevel::Debug) << "force trigger";
    return true;
}

bool JSyntheticDriver::autoset() {
    // Fit the largest enabled signal to the graticule and centre the trigger,
    // which is what the button means. Real work, not a placeholder.
    std::lock_guard<std::mutex> lk(m_cfgMutex);
    for (uint8_t i = 0; i < kChannels; ++i) {
        if (!m_channels[i].enabled) continue;
        const double vpp  = 2.0 * m_signals[i].amplitudeVolts;
        const double want = vpp / (kFullScaleDivisions * 0.75);   // fill three quarters
        m_channels[i].voltsPerDiv = nearestStep(kVoltsPerDiv, std::size(kVoltsPerDiv), want);
        m_channels[i].offsetVolts = -m_signals[i].offsetVolts;
    }
    const uint8_t src = m_trigger.sourceChannel;
    m_trigger.levelVolts = m_signals[src].offsetVolts;
    const double want = 2.5 / m_signals[src].frequencyHz / kHorizontalDivisions;  // ~2.5 cycles
    m_timebase.secondsPerDiv = nearestStep(kSecondsPerDiv, std::size(kSecondsPerDiv), want);

    JLOGC(JScopeLog::kSynth, JLogLevel::Info)
        << "autoset: s/div=" << m_timebase.secondsPerDiv
        << " trigger=" << m_trigger.levelVolts << "V on CH" << int(src + 1);
    return true;
}

// ---- acquisition thread ----------------------------------------------------

uint32_t JSyntheticDriver::_recordLength() const {
    return m_timebase.recordLength ? m_timebase.recordLength : kRecordLength;
}

double JSyntheticDriver::_signalVolts(uint8_t ch, double t) const {
    const JSyntheticChannel& s = m_signals[ch];

    // Work in NORMALISED CYCLES, never radians, and never round-trip through
    // 2*pi. Multiplying a fraction by 2*pi and dividing it back does not return
    // the same number, and every shape below except the sine branches on a
    // comparison against that fraction. Sample times land exactly on those
    // boundaries in practice — at 2 kHz on this timebase, sample s sits at cycle
    // s/1024, so s = 512 is precisely 0.5, the square's duty edge — and a
    // round-tripped 0.5 lands a few ulps either side of it at random. The branch
    // then flips between frames, moving an edge by one sample: the twitch shows
    // up on exactly the two shapes that have a discontinuity, and on no others.
    //
    // Reducing before the transcendental also stops precision decaying as the
    // driver runs: frequency * t grows without bound, and every bit spent on the
    // integer part is a bit lost from the fraction that determines the value.
    double frac = std::fmod(s.frequencyHz * t + s.phaseRadians / kTwoPi, 1.0);
    if (frac < 0.0) frac += 1.0;

    double v = 0.0;
    switch (s.waveform) {
        case JSyntheticWaveform::Sine:
            v = std::sin(kTwoPi * frac);
            break;
        case JSyntheticWaveform::Square: {
            // Rising edge across [0, e), high to dutyCycle, falling edge across
            // [dutyCycle, dutyCycle + e), low after.
            const double e = std::max(1.0e-9, s.edgeFraction);
            if (frac < e)                        v = -1.0 + 2.0 * (frac / e);
            else if (frac < s.dutyCycle)         v =  1.0;
            else if (frac < s.dutyCycle + e)     v =  1.0 - 2.0 * ((frac - s.dutyCycle) / e);
            else                                 v = -1.0;
            break;
        }
        case JSyntheticWaveform::Triangle:
            v = (frac < 0.5) ? (4.0 * frac - 1.0) : (3.0 - 4.0 * frac);
            break;
        case JSyntheticWaveform::Ramp: {
            // Rise across [0, 1-e), flyback across the final e.
            const double e = std::max(1.0e-9, s.edgeFraction);
            const double rise = 1.0 - e;
            if (frac < rise) v = 2.0 * (frac / rise) - 1.0;
            else             v = 1.0 - 2.0 * ((frac - rise) / e);
            break;
        }
        case JSyntheticWaveform::Noise:
        case JSyntheticWaveform::Dc:
            v = 0.0;
            break;
    }
    return v * s.amplitudeVolts + s.offsetVolts;
}

double JSyntheticDriver::_sampleVolts(uint8_t ch, double t) {
    double v = _signalVolts(ch, t);
    const double noise = m_signals[ch].noiseVolts;
    if (noise > 0.0) {
        std::uniform_real_distribution<double> d(-noise, noise);
        v += d(m_rng);
    }
    return v;
}

int16_t JSyntheticDriver::_voltsToCounts(uint8_t ch, double volts) const {
    // The synthetic ADC spans verticalDivisions * V/div, centred at midscale —
    // the same relationship a real front end has, so clipping looks right.
    const JScopeChannelConfig& c = m_channels[ch];
    const double fullScale = kFullScaleDivisions * c.voltsPerDiv;
    const double counts = kCountsMidscale
                        + ((volts + c.offsetVolts) / fullScale) * (kCountsMax - kCountsMin);

    // ROUND, never truncate. A cast truncates toward zero, which puts the
    // quantisation boundary exactly ON each integer — and a signal sitting at a
    // whole code (a sine's DC level lands precisely on midscale) then straddles
    // that boundary, because std::sin returns a residual of order 1e-16 that
    // differs with the absolute time argument. The code flips between 2047 and
    // 2048 from frame to frame, and on a shallow-slope trace that one-count step
    // moves where the min/max envelope's extremes fall — a visible twitch on the
    // vertical edges. Rounding puts the boundary at .5, half a code away from
    // where any signal actually sits, so the residual cannot reach it.
    return static_cast<int16_t>(std::lround(std::clamp(counts,
                                                       static_cast<double>(kCountsMin),
                                                       static_cast<double>(kCountsMax))));
}

std::optional<double> JSyntheticDriver::_findTriggerTime(double after) {
    // Caller holds m_cfgMutex.
    const uint8_t ch = m_trigger.sourceChannel;
    const double  f  = m_signals[ch].frequencyHz;
    if (f <= 0.0) return std::nullopt;

    const double period = 1.0 / f;
    const double level  = m_trigger.levelVolts;

    // Search the CLEAN signal, not the noisy sample. Two reasons, and the first
    // is a correctness one: _sampleVolts draws fresh noise per call, so searching
    // it would find the crossing of a noise realisation that no displayed sample
    // ever contains — the trigger point would not correspond to the waveform on
    // screen at all. The second is that a synthetic source exists to be the known,
    // stable reference the UI and the measurement tests are developed against, and
    // a reference that will not hold still is a worse reference. Real trigger
    // jitter belongs to real hardware, where it is unavoidable rather than
    // manufactured.
    //
    // Walk one period at fine resolution looking for a crossing on the requested
    // slope. Shape-agnostic on purpose: it works for the square and ramp exactly
    // as it does for the sine, and it will keep working for any shape added later.
    constexpr int kSearchSteps = 512;
    const double  step = period / kSearchSteps;

    // Start from the beginning of the period at or after `after`, so successive
    // frames advance by whole periods and the displayed waveform stands still.
    const double periodStart = std::floor(after / period) * period;

    for (int pass = 0; pass < 2; ++pass) {            // this period, then the next
        const double base = periodStart + pass * period;
        double prev = _signalVolts(ch, base);
        for (int i = 1; i <= kSearchSteps; ++i) {
            const double t   = base + i * step;
            const double cur = _signalVolts(ch, t);

            const bool rising  = (prev <  level && cur >= level);
            const bool falling = (prev >= level && cur <  level);
            const bool match =
                (m_trigger.slope == JScopeTriggerSlope::Rising  && rising)  ||
                (m_trigger.slope == JScopeTriggerSlope::Falling && falling) ||
                (m_trigger.slope == JScopeTriggerSlope::Either  && (rising || falling));

            if (match && t >= after) {
                // Linear interpolation across the bracketing step, so the trigger
                // point is sub-step accurate and the trace does not shimmer by a
                // fraction of a sample between frames.
                const double denom = (cur - prev);
                const double frac  = (denom != 0.0) ? (level - prev) / denom : 0.0;
                return t - step + frac * step;
            }
            prev = cur;
        }
    }
    return std::nullopt;
}

void JSyntheticDriver::_runLoop() {
    JLOGC(JScopeLog::kSynth, JLogLevel::Debug) << "acquisition thread up";
    while (m_running.load(std::memory_order_acquire)) {
        if (m_mode.load() == JScopeAcquisitionMode::Streaming) _produceStreaming();
        else                                                   _produceWindowed();

        if (m_singleShot.load() && m_sequence > 0) {
            JLOGC(JScopeLog::kSynth, JLogLevel::Info) << "single shot complete";
            m_running.store(false);
            _setState(JScopeState::Stopped);
            break;
        }
    }
    JLOGC(JScopeLog::kSynth, JLogLevel::Debug) << "acquisition thread down";
}

void JSyntheticDriver::_produceWindowed() {
    JScopeFrame* f = m_pool.acquire();
    if (!f) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); return; }

    uint32_t             record;
    double               dt;
    double               trigPos;
    JScopeTriggerConfig  trig;
    std::array<JScopeChannelConfig, JScopeLimits::kMaxChannels> chans;
    {
        std::lock_guard<std::mutex> lk(m_cfgMutex);
        record  = _recordLength();
        dt      = (m_timebase.secondsPerDiv * kHorizontalDivisions) / record;
        trigPos = m_timebase.triggerPosition;
        trig    = m_trigger;
        chans   = m_channels;
    }

    uint8_t planes = 0;
    for (uint8_t i = 0; i < kChannels; ++i) if (chans[i].enabled) ++planes;
    if (planes == 0) { m_pool.release(f); std::this_thread::sleep_for(std::chrono::milliseconds(20)); return; }

    if (!f->shape(planes, record)) {
        m_pool.release(f);
        _postError("record length exceeds the provisioned frame capacity");
        m_running.store(false);
        return;
    }

    // Find the trigger, and sweep so that the crossing lands at trigPos. This is
    // what makes the display STAND STILL: without it every frame starts at a
    // different phase and the trace jitters horizontally, which reads as
    // flickering edges on anything with a fast transition.
    const bool forced = m_forceTrigger.exchange(false);
    double tTrigger = 0.0;
    bool   didTrigger = false;
    {
        std::lock_guard<std::mutex> lk(m_cfgMutex);
        if (forced) {
            tTrigger = m_phaseTime;               // sweep from wherever we are
        } else if (std::optional<double> t = _findTriggerTime(m_lastTriggerTime)) {
            tTrigger   = *t;
            didTrigger = true;
        } else if (trig.mode == JScopeTriggerMode::Auto) {
            // Auto means "sweep anyway rather than leave the screen blank", and
            // that is exactly what happens when the level is never crossed.
            tTrigger = m_phaseTime;
            JLOGC(JScopeLog::kSynth, JLogLevel::Trace)
                << "auto sweep: level " << trig.levelVolts << "V never crossed on CH"
                << int(trig.sourceChannel + 1);
        } else {
            // Normal and Single genuinely wait. Nothing is published, the state
            // stays Armed, and the last acquisition remains on screen.
            m_pool.release(f);
            _setState(JScopeState::Armed);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return;
        }
        // A triggered sweep must advance by a WHOLE number of source periods, or
        // the displayed phase changes and the trace twitches sideways. Logged as a
        // ratio so any frame that does not is obvious in the trace log.
        if (didTrigger) {
            const double period = 1.0 / std::max(1.0, m_signals[trig.sourceChannel].frequencyHz);
            const double delta  = tTrigger - m_prevTriggerTime;
            char buf[160];
            std::snprintf(buf, sizeof buf, "trigger t=%.12f delta=%.12f periods=%.9f",
                          tTrigger, delta, m_prevTriggerTime > 0.0 ? delta / period : 0.0);
            JLOGC(JScopeLog::kSynth, JLogLevel::Trace) << buf;
            m_prevTriggerTime = tTrigger;
            m_lastTriggerTime = tTrigger + 0.5 * period;
        } else {
            m_lastTriggerTime = tTrigger;
        }
    }

    const double tStart = tTrigger - trigPos * dt * record;

    uint8_t p = 0;
    {
        // Held only while sampling: _sampleVolts reads m_signals and
        // _voltsToCounts reads m_channels. Never held across _publish().
        std::lock_guard<std::mutex> lk(m_cfgMutex);
        for (uint8_t i = 0; i < kChannels; ++i) {
            if (!chans[i].enabled) continue;
            f->header.channelIds[p]       = i;
            f->header.voltsPerDiv[p]      = static_cast<float>(chans[i].voltsPerDiv);
            f->header.zeroOffsetCounts[p] = kCountsMidscale;
            f->header.countsToVolts[p]    = static_cast<float>(
                (kFullScaleDivisions * chans[i].voltsPerDiv) / (kCountsMax - kCountsMin));
            int16_t* out = f->plane(p);
            for (uint32_t s = 0; s < record; ++s)
                out[s] = _voltsToCounts(i, _sampleVolts(i, tStart + s * dt));
            ++p;
        }
    }

    f->header.sequence           = m_sequence++;
    f->header.startSampleIndex   = 0;
    f->header.timestampSeconds   = std::chrono::duration<double>(
                                       std::chrono::steady_clock::now() - m_startTime).count();
    f->header.sampleInterval     = dt;
    f->header.triggerSampleIndex = static_cast<int32_t>(trigPos * record);
    f->header.triggered          = didTrigger;
    f->header.mode               = JScopeAcquisitionMode::Windowed;

    JLOGC(JScopeLog::kFrames, JLogLevel::Trace)
        << "windowed frame " << f->header.sequence << ": " << int(planes) << "x" << record
        << " dt=" << dt << "s trig@" << f->header.triggerSampleIndex
        << (forced ? " (forced)" : "");

    _publish(f);
    _setState(JScopeState::Triggered);

    // m_phaseTime is only the free-running reference for Auto sweeps and forced
    // triggers; a triggered sweep is positioned by _findTriggerTime, not by this.
    m_phaseTime += dt * record;
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
}

void JSyntheticDriver::_produceStreaming() {
    JScopeFrame* f = m_pool.acquire();
    if (!f) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); return; }

    double rate;
    std::array<JScopeChannelConfig, JScopeLimits::kMaxChannels> chans;
    {
        std::lock_guard<std::mutex> lk(m_cfgMutex);
        rate  = m_timebase.sampleRate;
        chans = m_channels;
    }
    if (rate <= 0.0) rate = 1.0;

    // One chunk per display interval, so the stream arrives in the granularity
    // the UI actually consumes.
    constexpr double kChunkSeconds = 0.05;
    const uint32_t   count = std::max<uint32_t>(1, static_cast<uint32_t>(rate * kChunkSeconds));
    const double     dt    = 1.0 / rate;

    uint8_t planes = 0;
    for (uint8_t i = 0; i < kChannels; ++i) if (chans[i].enabled) ++planes;
    if (planes == 0) { m_pool.release(f); std::this_thread::sleep_for(std::chrono::milliseconds(20)); return; }

    if (!f->shape(planes, count)) {
        m_pool.release(f);
        _postError("stream chunk exceeds the provisioned frame capacity");
        m_running.store(false);
        return;
    }

    uint8_t p = 0;
    {
        std::lock_guard<std::mutex> lk(m_cfgMutex);
        for (uint8_t i = 0; i < kChannels; ++i) {
            if (!chans[i].enabled) continue;
            f->header.channelIds[p]       = i;
            f->header.voltsPerDiv[p]      = static_cast<float>(chans[i].voltsPerDiv);
            f->header.zeroOffsetCounts[p] = kCountsMidscale;
            f->header.countsToVolts[p]    = static_cast<float>(
                (kFullScaleDivisions * chans[i].voltsPerDiv) / (kCountsMax - kCountsMin));
            int16_t* out = f->plane(p);
            for (uint32_t s = 0; s < count; ++s)
                out[s] = _voltsToCounts(i, _sampleVolts(i, m_phaseTime + s * dt));
            ++p;
        }
    }

    f->header.sequence           = m_sequence++;
    f->header.startSampleIndex   = m_streamIndex;
    f->header.timestampSeconds   = std::chrono::duration<double>(
                                       std::chrono::steady_clock::now() - m_startTime).count();
    f->header.sampleInterval     = dt;
    f->header.triggerSampleIndex = -1;      // streaming is never triggered
    f->header.triggered          = false;
    f->header.mode               = JScopeAcquisitionMode::Streaming;

    JLOGC(JScopeLog::kFrames, JLogLevel::Trace)
        << "stream chunk " << f->header.sequence << ": " << int(planes) << "x" << count
        << " @" << rate << "Sa/s from index " << m_streamIndex;

    m_streamIndex += count;
    m_phaseTime   += count * dt;
    _publish(f);

    std::this_thread::sleep_for(
        std::chrono::milliseconds(static_cast<int>(kChunkSeconds * 1000)));
}

} // inline namespace jf

J_REGISTER_SCOPE_DRIVER(JSyntheticDriver, "synthetic", "Synthetic source",
                        &jf::JSyntheticDriver::enumerate)
