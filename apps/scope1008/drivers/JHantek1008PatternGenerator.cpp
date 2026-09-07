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
    // The whole buffer. The chunking that reaches it was captured off the OEM and
    // is implemented, so this is now the device's real limit rather than the size
    // of a single packet.
    m_caps.maxPatternLength = JHantek1008Tables::kMaxPatternLength;
    m_caps.minRpm           = JHantek1008Tables::kMinGeneratorRpm;
    // The absolute ceiling, for a pattern short enough that the step rate is not
    // what stops it. maxRpm() gives the one that applies to the pattern loaded.
    m_caps.maxRpm           = JHantek1008Tables::kMaxEncodableRpm;

    m_pattern.assign(std::begin(kDefaultPattern), std::end(kDefaultPattern));
    m_requestedRpm = kDefaultRpm;
    m_actualRpm    = achievableRpm(kDefaultRpm);
}

uint32_t JHantek1008PatternGenerator::maxRpm() const {
    return JHantek1008Tables::maxRpmFor(static_cast<uint32_t>(m_pattern.size()));
}

uint32_t JHantek1008PatternGenerator::achievableRpm(uint32_t rpm) const {
    if (rpm < m_caps.minRpm || rpm > maxRpm() || m_pattern.empty()) return 0;
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

    {
        std::lock_guard<std::mutex> lk(m_deviceMutex);
        m_pattern = pattern;
        // The step rate is per revolution, so a new step count means a new pulse
        // length for the SAME speed.
        m_actualRpm = achievableRpm(m_requestedRpm);
        m_dirty     = true;
    }
    if (m_onChanged) m_onChanged();
    return true;
}

bool JHantek1008PatternGenerator::setRpm(uint32_t rpm) {
    // The ceiling depends on the pattern loaded now, not on a fixed capability:
    // a longer pattern needs more steps per revolution and the device has a
    // minimum time per step.
    if (rpm < m_caps.minRpm || rpm > maxRpm()) {
        m_lastError = "speed outside " + std::to_string(m_caps.minRpm) + ".." +
                      std::to_string(maxRpm()) + " rpm for a " +
                      std::to_string(m_pattern.size()) + "-step pattern";
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(m_deviceMutex);
        m_requestedRpm = rpm;
        m_actualRpm    = achievableRpm(rpm);
        if (m_actualRpm == 0) {
            m_lastError = "that speed needs less than one clock tick per step";
            return false;
        }
        m_dirty = true;
    }
    if (m_onChanged) m_onChanged();
    return true;
}

bool JHantek1008PatternGenerator::setOutputEnabled(bool on) {
    {
        std::lock_guard<std::mutex> lk(m_deviceMutex);
        m_outputEnabled = on;
        m_dirty         = true;
    }
    if (m_onChanged) m_onChanged();
    return true;
}

bool JHantek1008PatternGenerator::flush() {
    std::lock_guard<std::mutex> lk(m_deviceMutex);
    if (!m_protocol) return false;
    m_dirty = false;   // cleared first: a failed send is not retried in a tight loop

    // ORDER MATTERS AND THE SEQUENCE IS NEVER ABANDONED HALFWAY. The pattern write
    // is three commands, and stopping between them leaves the device waiting for
    // the rest -- which is what turned one bad reply into a pipe that timed out on
    // everything afterwards. Each step's result is kept and the sequence runs to
    // the end regardless.
    const bool pattern = _sendPattern();
    const bool speed   = _sendSpeed();
    const bool output  = m_protocol->setGeneratorOutput(m_outputEnabled);
    if (!output) m_lastError = m_protocol->lastError();

    if (pattern && speed && output) {
        JLOGC(JScopeLog::kScope, JLogLevel::Info)
            << "generator: " << m_pattern.size() << " steps at " << m_actualRpm
            << " rpm, output " << (m_outputEnabled ? "on" : "off");
        return true;
    }
    JLOGC(JScopeLog::kScope, JLogLevel::Warn)
        << "generator not fully applied (pattern " << pattern << " speed " << speed
        << " output " << output << "): " << m_lastError;
    return false;
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
