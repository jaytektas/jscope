#include "JHantek1008PatternGenerator.h"

#include "JHantek1008Tables.h"
#include "scope/JScopeLog.h"

#include <j/core/Log.h>

#include <algorithm>

inline namespace jf {

namespace {

// A square wave on output 1: one step closed, one step open, repeating.
//
// The reference demonstrates the encoding with F0 0F F0 0F -- every output
// switching each step, channels 1-4 starting low and 5-8 high. This is the same
// idea reduced to a single line, which is what somebody looking for a signal on a
// probe wants to see first. It is a starting point to edit, not a simulation of
// anything: a real crank wheel is built by setting the steps.
constexpr uint8_t kDefaultPattern[] = { 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00 };

// 600 rpm is a plausible engine idle, and with the default eight-step pattern it
// lands on a pulse length the clock can hit exactly -- so the first thing shown
// is not an example of the quantisation.
constexpr uint32_t kDefaultRpm = 600;

} // namespace

JHantek1008PatternGenerator::JHantek1008PatternGenerator(std::mutex& deviceMutex)
    : m_deviceMutex(deviceMutex) {
    m_caps.kind             = JScopeGeneratorKind::DigitalPattern;
    m_caps.patternOutputs   = JHantek1008Tables::kPatternOutputs;
    // The device holds 1440 steps, but a pattern is written in ONE 64-byte packet
    // and nobody has seen the chunking that would be needed beyond that. The
    // smaller number is the one this driver can actually honour, so it is the one
    // published -- a capability that overstates what works is worse than a modest
    // one, because the UI builds itself from this.
    m_caps.maxPatternLength = JHantek1008Tables::kMaxPatternPerPacket;
    m_caps.minRpm           = JHantek1008Tables::kMinGeneratorRpm;
    m_caps.maxRpm           = JHantek1008Tables::kMaxGeneratorRpm;

    m_pattern.assign(std::begin(kDefaultPattern), std::end(kDefaultPattern));
    m_requestedRpm = kDefaultRpm;
    m_actualRpm    = achievableRpm(kDefaultRpm);
}

uint32_t JHantek1008PatternGenerator::achievableRpm(uint32_t rpm) const {
    if (rpm < m_caps.minRpm || rpm > m_caps.maxRpm || m_pattern.empty()) return 0;
    const uint32_t steps = static_cast<uint32_t>(m_pattern.size());
    const uint32_t pulse = JHantek1008Tables::pulseLengthFor(rpm, steps);
    if (pulse == 0) return 0;   // faster than one tick per step
    return JHantek1008Tables::rpmForPulseLength(pulse, steps);
}

bool JHantek1008PatternGenerator::setPattern(const std::vector<uint8_t>& pattern) {
    if (pattern.empty()) {
        m_lastError = "a pattern needs at least one step";
        return false;
    }
    if (pattern.size() > m_caps.maxPatternLength) {
        m_lastError = "pattern is longer than " + std::to_string(m_caps.maxPatternLength) + " steps";
        return false;
    }

    std::lock_guard<std::mutex> lk(m_deviceMutex);
    m_pattern   = pattern;
    // The step rate is per revolution, so a new step count means a new pulse
    // length for the SAME speed. Recomputed here, sent below.
    m_actualRpm = achievableRpm(m_requestedRpm);

    if (!m_protocol) return true;   // remembered; applied when the device opens
    return _sendPattern() && _sendSpeed();
}

bool JHantek1008PatternGenerator::setRpm(uint32_t rpm) {
    if (rpm < m_caps.minRpm || rpm > m_caps.maxRpm) {
        m_lastError = "speed outside " + std::to_string(m_caps.minRpm) + ".." +
                      std::to_string(m_caps.maxRpm) + " rpm";
        return false;
    }

    std::lock_guard<std::mutex> lk(m_deviceMutex);
    m_requestedRpm = rpm;
    m_actualRpm    = achievableRpm(rpm);
    if (m_actualRpm == 0) {
        m_lastError = "that speed needs less than one clock tick per step";
        return false;
    }

    if (!m_protocol) return true;
    return _sendSpeed();
}

bool JHantek1008PatternGenerator::setOutputEnabled(bool on) {
    std::lock_guard<std::mutex> lk(m_deviceMutex);
    m_outputEnabled = on;
    if (!m_protocol) return true;

    // Turning on sends the pattern and speed first. The device keeps whatever it
    // was last given, which after a replug is nothing at all, and switching on a
    // stale or absent pattern puts an unexpected signal on eight wires that are
    // probably connected to something.
    if (on && !(_sendPattern() && _sendSpeed())) return false;

    if (!m_protocol->setGeneratorOutput(on)) {
        m_lastError = m_protocol->lastError();
        return false;
    }
    JLOGC(JScopeLog::kScope, JLogLevel::Info)
        << "generator output " << (on ? "on" : "off")
        << " — " << m_pattern.size() << " steps at " << m_actualRpm << " rpm";
    return true;
}

bool JHantek1008PatternGenerator::reapply() {
    std::lock_guard<std::mutex> lk(m_deviceMutex);
    if (!m_protocol) return false;
    if (!_sendPattern() || !_sendSpeed()) return false;
    if (!m_protocol->setGeneratorOutput(m_outputEnabled)) {
        m_lastError = m_protocol->lastError();
        return false;
    }
    return true;
}

bool JHantek1008PatternGenerator::_sendPattern() {
    if (m_protocol->setGeneratorPattern(m_pattern)) return true;
    m_lastError = m_protocol->lastError();
    return false;
}

bool JHantek1008PatternGenerator::_sendSpeed() {
    const uint32_t pulse =
        JHantek1008Tables::pulseLengthFor(m_requestedRpm, static_cast<uint32_t>(m_pattern.size()));
    if (m_protocol->setGeneratorPulseLength(pulse)) return true;
    m_lastError = m_protocol->lastError();
    return false;
}

} // inline namespace jf
