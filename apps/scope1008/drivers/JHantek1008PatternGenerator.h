#pragma once

#include "JHantek1008Protocol.h"
#include "scope/JPatternGenerator.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

inline namespace jf {

// The 1008C's eight digital outputs, as a JPatternGenerator.
//
// IT NEVER TALKS TO THE DEVICE FROM THE CALLER'S THREAD. Setting a value records
// it and reports a change; the bytes go out later, from whichever thread owns the
// bulk pipe. This is not caution, it is the only thing that works: the acquisition
// thread holds no lock while it runs a capture -- it copies the configuration and
// releases -- so a write issued from the UI thread lands in the middle of another
// transaction. It did. A pattern write during a running capture read back 0x1c,
// which is a byte of somebody else's answer, and the pipe never recovered.
//
// This is the contract CLAUDE.md already states for every other control: the UI
// thread never touches USB. Channel and timebase changes obey it by storing a
// value and letting the acquisition thread apply it; so does this now.
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

    // These RECORD and return true. They no longer report whether the device
    // accepted anything, because at the moment they are called nothing has been
    // sent -- see flush(), and lastError() after it.
    bool setOutputEnabled(bool on) override;
    bool isOutputEnabled() const override { return m_outputEnabled; }

    bool setPattern(const std::vector<uint8_t>& pattern) override;
    const std::vector<uint8_t>& pattern() const override { return m_pattern; }

    bool     setRpm(uint32_t rpm) override;
    uint32_t requestedRpm() const override { return m_requestedRpm; }
    uint32_t actualRpm() const override    { return m_actualRpm; }
    uint32_t achievableRpm(uint32_t rpm) const override;
    uint32_t maxRpm() const override;

    // THE PROTOCOL ONLY EXISTS WHILE THE DEVICE IS OPEN, and this outlives it, so
    // the driver hands it over on open and takes it away on close. Detached, the
    // generator is a set of remembered settings -- which is what lets it be set up
    // before a scope is plugged in, and what makes those settings survive a
    // reconnect. Null is the honest way to say "there is nothing to talk to";
    // a separate ready flag beside a pointer would just be able to disagree with it.
    void attach(JHantek1008Protocol* protocol) { m_protocol = protocol; }

    // Whether anything has been changed since the last flush.
    bool isDirty() const { return m_dirty; }

    // SEND. Called only from a context that owns the device: the acquisition
    // thread between captures, or any thread while acquisition is stopped. Writes
    // the whole state rather than a delta -- the device keeps nothing across a
    // replug, and three short commands are not worth tracking deltas for.
    bool flush();

    // Told when something changed, so the driver can decide where flush() runs.
    void setChangeHandler(std::function<void()> handler) { m_onChanged = std::move(handler); }

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
    bool                        m_dirty{true};   // nothing has been sent yet
    std::function<void()>       m_onChanged;
    std::string                 m_lastError;
};

} // inline namespace jf
