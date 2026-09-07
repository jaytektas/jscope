#include "JHantek1008Protocol.h"

#include "JHantek1008Codec.h"
#include "scope/JScopeLog.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

inline namespace jf {

namespace {

// Generator commands used during initialisation. The reference sets a speed and
// switches the generator off as part of bringing the device up; whether that is
// required or merely what the Windows software happens to do is unknown, so it
// is reproduced.
constexpr uint32_t kInitGeneratorRpm      = 300000;
constexpr uint32_t kGeneratorBitsPerWave  = 8;
constexpr uint32_t kGeneratorClock        = 360000000;

// Opaque payloads the reference sends verbatim. Their meaning is not known; they
// are named for where they appear rather than for what they do, because guessing
// at a name would be worse than admitting ignorance.
constexpr uint8_t kConfigAcInit1[]  = { 0x01, 0xf4, 0x00, 0x09, 0xc5, 0x00, 0x09, 0xc5 };
constexpr uint8_t kConfigAcInit3a[] = { 0x00, 0xc8, 0x00, 0x02, 0xbd, 0x00, 0x02, 0xbd };
constexpr uint8_t kConfigAcInit3b[] = { 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x05, 0x79 };

std::vector<uint8_t> toVector(const uint8_t* p, size_t n) { return { p, p + n }; }

} // namespace

JHantek1008Protocol::JHantek1008Protocol(JUsbTransport& transport,
                                         uint8_t endpointOut, uint8_t endpointIn)
    : m_transport(transport), m_epOut(endpointOut), m_epIn(endpointIn) {}

void JHantek1008Protocol::_sleep(double seconds) const {
    if (seconds <= 0.0) return;
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

bool JHantek1008Protocol::send(uint8_t opcode, const std::vector<uint8_t>& parameters,
                               size_t responseLength, bool echoExpected,
                               std::vector<uint8_t>* response,
                               double secondsBeforeRead) {
    std::vector<uint8_t> message;
    message.reserve(1 + parameters.size());
    message.push_back(opcode);
    message.insert(message.end(), parameters.begin(), parameters.end());

    _sleep(m_timing.beforeWrite);
    if (!m_transport.bulkOut(m_epOut, message.data(), message.size(), m_timing.timeoutMs)) {
        char msg[128];
        std::snprintf(msg, sizeof msg, "write of command 0x%02x failed: %s",
                      opcode, m_transport.lastError().c_str());
        m_lastError = msg;
        return false;
    }

    const size_t want = responseLength + (echoExpected ? 1 : 0);
    if (want == 0) return true;

    _sleep(secondsBeforeRead >= 0.0 ? secondsBeforeRead : m_timing.beforeRead);

    std::vector<uint8_t> buffer(want);
    const int got = m_transport.bulkIn(m_epIn, buffer.data(), want, m_timing.timeoutMs);
    if (got < 0) {
        m_lastError = "read after command failed: " + m_transport.lastError();
        return false;
    }
    buffer.resize(static_cast<size_t>(got));

    if (echoExpected) {
        if (buffer.empty() || buffer[0] != opcode) {
            char msg[96];
            std::snprintf(msg, sizeof msg,
                          "command 0x%02x was not echoed (got 0x%02x, %d bytes)",
                          opcode, buffer.empty() ? 0 : buffer[0], got);
            m_lastError = msg;
            JLOGC(JScopeLog::kHantek, JLogLevel::Error) << m_lastError;
            return false;
        }
        buffer.erase(buffer.begin());
    }
    if (response) *response = std::move(buffer);
    return true;
}

bool JHantek1008Protocol::ping(double secondsBeforeStart) {
    _sleep(secondsBeforeStart);
    return send(JHantek1008Tables::kPing);
}

bool JHantek1008Protocol::setActiveChannels(const std::vector<uint8_t>& channels) {
    if (channels.empty()) { m_lastError = "at least one channel must be active"; return false; }
    if (!send(JHantek1008Tables::kSetActiveChannelCount,
              { static_cast<uint8_t>(channels.size()) })) return false;
    return send(JHantek1008Tables::kSetActiveChannelMap,
                JHantek1008Codec::channelMap(channels));
}

bool JHantek1008Protocol::setVerticalScales(const std::vector<double>& perChannelVScale) {
    std::vector<uint8_t> ids;
    ids.reserve(JHantek1008Tables::kChannelCount);
    for (uint8_t c = 0; c < JHantek1008Tables::kChannelCount; ++c)
        ids.push_back(JHantek1008Tables::vscaleId(
            c < perChannelVScale.size() ? perChannelVScale[c] : 1.0));
    return send(JHantek1008Tables::kSetVerticalScale, ids, 0, true, nullptr,
                m_timing.vscaleSettle);
}

bool JHantek1008Protocol::setRecordLength(uint16_t samples) {
    if (samples == 0 || samples > JHantek1008Tables::kMaxRecordSamples) return false;
    const uint16_t bytes = static_cast<uint16_t>(samples * 2);
    return send(JHantek1008Tables::kSetRecordLength,
                { static_cast<uint8_t>(bytes >> 8), static_cast<uint8_t>(bytes & 0xff) });
}

bool JHantek1008Protocol::setHorizontalTriggerPosition(uint8_t timeDivId,
                                                       uint8_t activeChannels,
                                                       uint8_t percent) {
    // Odd channel counts are rounded up, the same padding the burst interleave
    // uses — the device works in pairs of lanes.
    uint8_t nch = activeChannels;
    if (nch == 3 || nch == 5 || nch == 7) ++nch;
    if (nch == 0) return false;

    // Below code 16 the trigger is forced to the centre; the device offers no
    // horizontal position there.
    if (timeDivId <= 15) percent = 50;
    if (percent > 100) percent = 100;

    const double   scale  = JHantek1008Tables::kTimeScaleForId(timeDivId);
    const uint16_t record = JHantek1008Tables::kRecordLenForId(timeDivId);

    const uint32_t pre = 2u * ((static_cast<uint32_t>(record) / nch) * percent / 100u);

    // Truncate THEN add one, in that order: the +1 is a counter bias, not a
    // rounding step, and reversing them shifts the sweep by a sample.
    const auto counter = [&](uint32_t share) -> uint32_t {
        return static_cast<uint32_t>(share * record / 100.0 * scale) + 1u;
    };
    const uint32_t delayB = counter(percent);
    const uint32_t delayC = counter(100u - percent);

    return send(JHantek1008Tables::kConfigAc, {
        static_cast<uint8_t>((pre    >> 8) & 0xff), static_cast<uint8_t>( pre         & 0xff),
        static_cast<uint8_t>((delayB >> 16) & 0xff), static_cast<uint8_t>((delayB >> 8) & 0xff),
        static_cast<uint8_t>( delayB        & 0xff),
        static_cast<uint8_t>((delayC >> 16) & 0xff), static_cast<uint8_t>((delayC >> 8) & 0xff),
        static_cast<uint8_t>( delayC        & 0xff) });
}

bool JHantek1008Protocol::setTimeDivId(uint8_t id) {
    return send(JHantek1008Tables::kSetTimeDiv, { id });
}

bool JHantek1008Protocol::setTrigger(uint8_t sourceChannel, bool rising) {
    return send(JHantek1008Tables::kSetTrigger,
                { sourceChannel, static_cast<uint8_t>(rising ? 0 : 1) });
}

bool JHantek1008Protocol::setTriggerLevel(uint16_t level) {
    // Big-endian, unlike the samples, which are little-endian.
    return send(JHantek1008Tables::kSetTriggerLevel,
                { static_cast<uint8_t>(level >> 8), static_cast<uint8_t>(level & 0xff) });
}

bool JHantek1008Protocol::setGeneratorOutput(bool on) {
    if (!send(JHantek1008Tables::kGeneratorEnable, { 0x00 })) return false;
    return send(JHantek1008Tables::kGeneratorSwitch, { 0x08, static_cast<uint8_t>(on ? 0x01 : 0x00) });
}

bool JHantek1008Protocol::setGeneratorPulseLength(uint32_t pulseLength) {
    if (pulseLength == 0) {
        m_lastError = "generator pulse length of zero would divide by zero on the device";
        return false;
    }
    // Little-endian here. The trigger level and record length are big-endian on
    // the same wire, which is not a mistake in either place -- this device simply
    // is not consistent, and each command's order is the one observed for it.
    return send(JHantek1008Tables::kGeneratorSpeed,
                { 0x01,
                  static_cast<uint8_t>(pulseLength & 0xff),
                  static_cast<uint8_t>((pulseLength >> 8) & 0xff),
                  static_cast<uint8_t>((pulseLength >> 16) & 0xff),
                  static_cast<uint8_t>((pulseLength >> 24) & 0xff) });
}

bool JHantek1008Protocol::setGeneratorPattern(const std::vector<uint8_t>& pattern) {
    if (pattern.empty()) {
        m_lastError = "an empty pattern has no steps to play";
        return false;
    }
    if (pattern.size() > JHantek1008Tables::kMaxPatternLength) {
        m_lastError = "pattern longer than the device holds (" +
                      std::to_string(JHantek1008Tables::kMaxPatternLength) + " pulses)";
        return false;
    }

    if (!send(JHantek1008Tables::kGeneratorEnable, { 0x00 })) return false;

    const uint16_t length = static_cast<uint16_t>(pattern.size());
    if (!send(JHantek1008Tables::kGeneratorLength,
              { static_cast<uint8_t>(length & 0xff), static_cast<uint8_t>(length >> 8) }))
        return false;

    // A RUN OF CHUNKS, indexed from one. Each packet is the opcode, the chunk
    // number, then a fixed 62 bytes: the device is told the real length above and
    // handed full packets regardless, so a short tail is zero padded rather than
    // sent short. The OEM writing 1440 pulses sends exactly this -- 24 packets,
    // 0x01 to 0x18, the last carrying 14 real bytes.
    const size_t chunkSize = JHantek1008Tables::kPatternBytesPerChunk;
    const size_t chunks    = (pattern.size() + chunkSize - 1) / chunkSize;
    for (size_t c = 0; c < chunks; ++c) {
        const size_t from = c * chunkSize;
        const size_t take = std::min(chunkSize, pattern.size() - from);

        std::vector<uint8_t> payload;
        payload.reserve(1 + chunkSize);
        payload.push_back(static_cast<uint8_t>(c + 1));   // chunk index, 1-based
        payload.insert(payload.end(), pattern.begin() + from, pattern.begin() + from + take);
        payload.resize(1 + chunkSize, 0x00);
        if (!send(JHantek1008Tables::kGeneratorWaveform, payload)) return false;
    }
    return true;
}

bool JHantek1008Protocol::startAcquisition(uint8_t mode) {
    return send(JHantek1008Tables::kStartAcquisition, { mode });
}

bool JHantek1008Protocol::forceTrigger() {
    return send(JHantek1008Tables::kAcquisitionGo);
}

bool JHantek1008Protocol::arm(bool forceTrigger) {
    if (!send(JHantek1008Tables::kAcquisitionArm)) return false;
    if (!forceTrigger) return true;
    return send(JHantek1008Tables::kAcquisitionGo);
}

bool JHantek1008Protocol::startRollMode(uint8_t rollRateId) {
    // Order matters and is not obvious: the rate id goes through 0xa3 BEFORE the
    // mode is selected, and the device stalls its bulk OUT endpoint if 0xa3 last
    // carried a ns/div id instead. A pipe error on the first roll command is
    // always this.
    if (!setTimeDivId(rollRateId)) return false;
    if (!ping(0.0100)) return false;
    if (!startAcquisition(JHantek1008Tables::kModeRoll)) return false;
    // Roll has no trigger to wait for — it streams — so it always goes.
    return arm(true);
}

bool JHantek1008Protocol::startBurstCapture(bool forceTrigger) {
    // EXACTLY what Scope.exe sends between one capture and the next, taken from
    // a usbmon capture of the OEM software driving this instrument:
    //
    //     e4 01, e6 01, f3, e4 01, e6 01, a4 01, c0
    //
    // and nothing else. No a3, no c1, no a7, no ac — configuration is sent when
    // it CHANGES and then left alone.
    //
    // Re-sending the timebase here, which is what this used to do, is what
    // cleared the device's trigger selection on every capture and made the
    // trigger source appear to do nothing. Re-sending 0xc1 papered over that;
    // not sending 0xa3 removes the cause.
    std::vector<uint8_t> blob;
    send(JHantek1008Tables::kStatusE4, { 0x01 });
    send(JHantek1008Tables::kStatusE6, { 0x01 }, 10, false, &blob);
    send(JHantek1008Tables::kStartWatch, {}, 1, true, &blob);
    send(JHantek1008Tables::kStatusE4, { 0x01 });
    send(JHantek1008Tables::kStatusE6, { 0x01 }, 10, false, &blob);

    if (!send(JHantek1008Tables::kStartAcquisition, { JHantek1008Tables::kModeBurst },
              0, true, nullptr, m_timing.burstArmSettle)) return false;
    return arm(forceTrigger);
}

bool JHantek1008Protocol::waitReady(int attempts) {
    for (int i = 0; i < attempts; ++i) {
        std::vector<uint8_t> reply;
        if (!send(JHantek1008Tables::kReadyPoll,
                  { JHantek1008Tables::kReadyPollArgument }, 1, true, &reply)) return false;
        if (reply.empty()) continue;
        // 2 or 3 mean the record is ready; 0 and 1 mean keep waiting.
        if (reply[0] == 2 || reply[0] == 3) return true;
        _sleep(m_timing.readyPollInterval);
        // 0xf3 between polls, which is what Scope.exe does. A ping here was a
        // guess; this is what the instrument is actually kept alive with.
        std::vector<uint8_t> watch;
        send(JHantek1008Tables::kStartWatch, {}, 1, true, &watch);
    }
    m_lastError = "the device never reported a ready record";
    JLOGC(JScopeLog::kHantek, JLogLevel::Warn) << m_lastError;
    return false;
}

bool JHantek1008Protocol::readBurstHalf(uint8_t half, std::vector<uint8_t>& out) {
    std::vector<uint8_t> lengthReply;
    if (!send(JHantek1008Tables::kBurstLength, { half }, 2, false, &lengthReply)) return false;
    if (lengthReply.size() < 2) { m_lastError = "short reply to the burst length query"; return false; }

    const size_t length = (static_cast<size_t>(lengthReply[0]) << 8) | lengthReply[1];
    const size_t chunks = (length + JHantek1008Tables::kMaxPacketSize - 1)
                        / JHantek1008Tables::kMaxPacketSize;

    out.clear();
    out.reserve(length);
    for (size_t i = 0; i < chunks; ++i) {
        std::vector<uint8_t> chunk;
        if (!send(JHantek1008Tables::kReadBurstChunk, { half },
                  JHantek1008Tables::kMaxPacketSize, false, &chunk)) return false;
        out.insert(out.end(), chunk.begin(), chunk.end());
    }
    // The last chunk is padded to a full packet; the stated length is the truth.
    if (out.size() > length) out.resize(length);
    return true;
}

bool JHantek1008Protocol::rollReadyLength(uint16_t& lengthOut) {
    std::vector<uint8_t> reply;
    if (!send(JHantek1008Tables::kRollReadyLength, {}, 2, false, &reply)) return false;
    if (reply.size() < 2) { m_lastError = "short reply to the roll length query"; return false; }
    lengthOut = static_cast<uint16_t>((static_cast<uint16_t>(reply[0]) << 8) | reply[1]);
    return true;
}

bool JHantek1008Protocol::readRollBytes(size_t length, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(length);
    size_t remaining = length;
    while (remaining > 0) {
        std::vector<uint8_t> chunk;
        if (!send(JHantek1008Tables::kReadRollChunk, {},
                  JHantek1008Tables::kMaxPacketSize, false, &chunk)) return false;
        // A final partial read is padded to a full packet; trim to what was
        // actually announced or the trailing zeros become samples.
        if (remaining < chunk.size()) chunk.resize(remaining);
        remaining -= chunk.size();
        out.insert(out.end(), chunk.begin(), chunk.end());
        if (chunk.empty()) break;
    }
    return true;
}

bool JHantek1008Protocol::initialisePhase1() {
    JLOGC(JScopeLog::kHantek, JLogLevel::Info) << "init phase 1: bringing the device up";

    if (!send(JHantek1008Tables::kResetB0)) return false;
    _sleep(m_timing.afterReset);
    if (!send(JHantek1008Tables::kResetB0)) return false;
    if (!ping()) return false;

    // The reference sets a generator speed and switches the generator off here.
    // Whether the device needs it or the Windows software merely does it is not
    // known, so it is reproduced.
    const uint32_t pulseLength = static_cast<uint32_t>(
        ((8.0 * kGeneratorClock) / kGeneratorBitsPerWave) / kInitGeneratorRpm);
    if (!send(JHantek1008Tables::kGeneratorSpeed,
              { 0x01,
                static_cast<uint8_t>(pulseLength & 0xff),
                static_cast<uint8_t>((pulseLength >> 8) & 0xff),
                static_cast<uint8_t>((pulseLength >> 16) & 0xff),
                static_cast<uint8_t>((pulseLength >> 24) & 0xff) })) return false;
    if (!send(JHantek1008Tables::kGeneratorEnable, { 0x00 })) return false;
    if (!send(JHantek1008Tables::kGeneratorSwitch, { 0x08, 0x00 })) return false;

    // Status and per-unit calibration blobs. Logged, never asserted on: these
    // bytes differ between units, and a driver that checked them would work on
    // one 1008C and reject the next.
    std::vector<uint8_t> blob;
    send(JHantek1008Tables::kStatusB5, {}, 64, false, &blob, m_timing.statusB5Settle);
    JLog::instance().hexDump(JLogLevel::Debug, JScopeLog::kHantek, "[b5 status]",
                             blob.data(), blob.size());
    send(JHantek1008Tables::kStatusB6, {}, 64, false, &blob);
    JLog::instance().hexDump(JLogLevel::Debug, JScopeLog::kHantek, "[b6 status]",
                             blob.data(), blob.size());
    send(JHantek1008Tables::kStatusE5, {}, 2,  false, &blob);
    send(JHantek1008Tables::kStatusF7, {}, 64, false, &blob);
    JLog::instance().hexDump(JLogLevel::Debug, JScopeLog::kHantek, "[f7 calibration]",
                             blob.data(), blob.size());
    send(JHantek1008Tables::kStatusF8, {}, 64, false, &blob);
    send(JHantek1008Tables::kStatusFa, {}, 56, false, &blob);

    if (!send(JHantek1008Tables::kStatusF5, {}, 0, true, nullptr,
              m_timing.vscaleSettle)) return false;

    // All eight channels, the Windows software's default timebase, a default
    // trigger — then two more opaque payloads.
    std::vector<uint8_t> all(JHantek1008Tables::kChannelCount);
    for (uint8_t c = 0; c < JHantek1008Tables::kChannelCount; ++c) all[c] = c;
    if (!setActiveChannels(all)) return false;
    if (!setTimeDivId(JHantek1008Tables::nsPerDivIdFor(500.0e-6))) return false;
    if (!setTrigger(0, true)) return false;

    send(JHantek1008Tables::kStatusA7, { 0x00, 0x00 }, 1, true, &blob);
    return send(JHantek1008Tables::kConfigAc,
                toVector(kConfigAcInit1, sizeof kConfigAcInit1));
}

bool JHantek1008Protocol::initialisePhase3(const std::vector<uint8_t>& activeChannels,
                                           const std::vector<double>& vscales,
                                           uint8_t timeDivId, uint16_t recordSamples,
                                           uint8_t triggerChannel, bool triggerRising,
                                           uint16_t triggerLevel, uint8_t triggerPercent) {
    JLOGC(JScopeLog::kHantek, JLogLevel::Info) << "init phase 3: applying the configuration";

    if (!send(JHantek1008Tables::kStatusF6, {}, 0, true, nullptr,
              m_timing.vscaleSettle)) return false;

    std::vector<uint8_t> blob;
    send(JHantek1008Tables::kStatusE5, {}, 2,  false, &blob);
    send(JHantek1008Tables::kStatusF7, {}, 64, false, &blob);
    send(JHantek1008Tables::kStatusF8, {}, 64, false, &blob);
    send(JHantek1008Tables::kStatusFa, {}, 56, false, &blob);

    if (!setTimeDivId(timeDivId)) return false;
    if (!send(JHantek1008Tables::kConfigAc,
              toVector(kConfigAcInit3a, sizeof kConfigAcInit3a))) return false;
    if (!send(JHantek1008Tables::kStatusE4, { 0x01 })) return false;
    send(JHantek1008Tables::kStatusE6, { 0x01 }, 10, false, &blob);

    // Scope.exe always configures in this order: channel count, channel map,
    // vertical scales, record length, then sample rate. setActiveChannels sends
    // the first two; the record length has to precede the rate because the
    // device derives its capture window from both.
    if (!ping()) return false;
    if (!setActiveChannels(activeChannels)) return false;
    if (!setVerticalScales(vscales)) return false;
    if (!setRecordLength(recordSamples)) return false;
    if (!setTimeDivId(timeDivId)) return false;
    if (!setTrigger(triggerChannel, triggerRising)) return false;
    if (!setTriggerLevel(triggerLevel)) return false;

    send(JHantek1008Tables::kStatusA7, { 0x00, 0x00 }, 1, true, &blob);

    // The sweep window, COMPUTED for this timebase and channel count rather than
    // replayed as a captured blob. It belongs here, with the rest of the
    // configuration, because Scope.exe sends 0xac when settings change and never
    // per capture — see startBurstCapture.
    if (!setHorizontalTriggerPosition(timeDivId, static_cast<uint8_t>(activeChannels.size()),
                                      triggerPercent)) return false;
    if (!setTriggerLevel(triggerLevel)) return false;

    send(JHantek1008Tables::kStatusE9, {}, 2, false, &blob);
    return true;
}

bool JHantek1008Protocol::initialise(const std::vector<uint8_t>& activeChannels,
                                     const std::vector<double>& vscales,
                                     uint8_t timeDivId, uint8_t triggerChannel,
                                     bool triggerRising, uint16_t triggerLevel) {
    if (!initialisePhase1()) return false;
    // Centre trigger: the one-shot path is used where nothing has asked for a
    // horizontal position.
    return initialisePhase3(activeChannels, vscales, timeDivId,
                            JHantek1008Tables::kDefaultRecordSamples, triggerChannel,
                            triggerRising, triggerLevel, 50);
}

} // inline namespace jf
