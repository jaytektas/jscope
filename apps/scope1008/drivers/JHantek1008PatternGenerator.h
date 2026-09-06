#pragma once

#include "JHantek1008Protocol.h"
#include "scope/JPatternGenerator.h"

#include <cstdint>
#include <mutex>
#include <vector>

inline namespace jf {

// The 1008C's eight digital outputs, as a JPatternGenerator.
//
// It shares the driver's configuration mutex rather than owning one. The device
// answers one command at a time on a single bulk pipe, so a generator write
// interleaved with a channel write would corrupt both; there is one lock for the
// device and this takes part in it.
//
// SPEED DEPENDS ON THE PATTERN. The device is told a pulse length -- ticks per
// step -- and one pass of the pattern is one revolution, so the same speed needs
// a different pulse length when the pattern gets longer or shorter. Changing the
// pattern therefore re-sends the speed; forgetting to would silently change the
// RPM as a side effect of editing the wheel.
class JHantek1008PatternGenerator final : public JPatternGenerator {
public:
    explicit JHantek1008PatternGenerator(std::mutex& deviceMutex);

    const JScopeGeneratorCapabilities& capabilities() const override { return m_caps; }

    bool setOutputEnabled(bool on) override;
    bool isOutputEnabled() const override { return m_outputEnabled; }

    bool setPattern(const std::vector<uint8_t>& pattern) override;
    const std::vector<uint8_t>& pattern() const override { return m_pattern; }

    bool     setRpm(uint32_t rpm) override;
    uint32_t requestedRpm() const override { return m_requestedRpm; }
    uint32_t actualRpm() const override    { return m_actualRpm; }
    uint32_t achievableRpm(uint32_t rpm) const override;

    // THE PROTOCOL ONLY EXISTS WHILE THE DEVICE IS OPEN, and this outlives it, so
    // the driver hands it over on open and takes it away on close. Detached, the
    // generator is a set of remembered settings -- which is what lets it be set up
    // before a scope is plugged in, and what makes those settings survive a
    // reconnect. Null is the honest way to say "there is nothing to talk to";
    // a separate ready flag beside a pointer would just be able to disagree with it.
    void attach(JHantek1008Protocol* protocol) { m_protocol = protocol; }

    // Push the whole state at the device. Called once the scope is initialised,
    // because a generator configured while the device was closed has nothing to
    // configure.
    bool reapply();

    const std::string& lastError() const { return m_lastError; }

private:
    // Both callers already hold the device lock.
    bool _sendPattern();
    bool _sendSpeed();

    JHantek1008Protocol* m_protocol{nullptr};
    std::mutex&          m_deviceMutex;

    JScopeGeneratorCapabilities m_caps;
    std::vector<uint8_t>        m_pattern;
    uint32_t                    m_requestedRpm{0};
    uint32_t                    m_actualRpm{0};
    bool                        m_outputEnabled{false};
    std::string                 m_lastError;
};

} // inline namespace jf
