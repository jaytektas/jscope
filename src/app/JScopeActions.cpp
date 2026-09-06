#include <memory>
#include "JScopeActions.h"

#include <algorithm>

#include "scope/JScopeDriver.h"
#include "scope/JScopeLog.h"
#include "scope/JScopeSession.h"

inline namespace jf {

bool JScopeActions::_requireDriver(const char* what) {
    if (m_session.driver()) return true;
    JLOGC(JScopeLog::kUi, JLogLevel::Warn) << what << ": no device is open";
    onRefused.emit(std::string(what) + ": no device is open");
    return false;
}

// ---- acquisition -----------------------------------------------------------

bool JScopeActions::run() {
    if (!_requireDriver("Run")) return false;
    JScopeDriver* d = m_session.driver();
    JLOGC(JScopeLog::kUi, JLogLevel::Info)
        << "Run (" << jScopeAcquisitionModeName(d->timebaseConfig().mode) << ")";
    if (!d->start(d->timebaseConfig().mode)) {
        onRefused.emit("the device refused to start");
        return false;
    }
    return true;
}

bool JScopeActions::stop() {
    if (!_requireDriver("Stop")) return false;
    JLOGC(JScopeLog::kUi, JLogLevel::Info) << "Stop";
    m_session.driver()->stop();
    return true;
}

bool JScopeActions::single() {
    if (!_requireDriver("Single")) return false;
    JLOGC(JScopeLog::kUi, JLogLevel::Info) << "Single";
    if (!m_session.driver()->single()) {
        onRefused.emit("the device refused a single shot");
        return false;
    }
    return true;
}

bool JScopeActions::forceTrigger() {
    if (!_requireDriver("Force trigger")) return false;
    JScopeDriver* d = m_session.driver();
    // The capability is the authority. Saying so is more useful than a button
    // that appears to work and does nothing.
    if (!d->capabilities().hasForceTrigger) {
        JLOGC(JScopeLog::kUi, JLogLevel::Warn) << "force trigger: not supported by this device";
        onRefused.emit("this device has no force trigger");
        return false;
    }
    JLOGC(JScopeLog::kUi, JLogLevel::Info) << "Force trigger";
    return d->forceTrigger();
}

bool JScopeActions::autoset() {
    if (!_requireDriver("Autoset")) return false;
    JScopeDriver* d = m_session.driver();
    if (!d->capabilities().hasAutoset) {
        JLOGC(JScopeLog::kUi, JLogLevel::Warn) << "autoset: not supported by this device";
        onRefused.emit("this device has no autoset");
        return false;
    }
    JLOGC(JScopeLog::kUi, JLogLevel::Info) << "Autoset";

    // OFF THE UI THREAD. Autoset stops the acquisition, sweeps the vertical
    // ranges and takes a capture on each — seconds of USB with sleeps between
    // transfers. Run inline that is seconds of a window that does not repaint,
    // which reads as a hang however correct the result turns out to be.
    auto ok = std::make_shared<bool>(false);
    if (!m_session.runOnDevice(
            [d, ok] { *ok = d->autoset(); },
            [this, ok] {
                if (*ok) onConfigChanged.emit();   // autoset moves every setting at once
                else     onRefused.emit("autoset did not complete");
                onBusyChanged.emit(false);
            })) {
        onRefused.emit("the instrument is busy");
        return false;
    }
    onBusyChanged.emit(true);
    return true;
}

bool JScopeActions::toggleRunStop() {
    if (!_requireDriver("Run/Stop")) return false;
    const JScopeState s = m_session.driver()->state();
    const bool idle = (s == JScopeState::Stopped || s == JScopeState::Idle ||
                       s == JScopeState::Error);
    return idle ? run() : stop();
}

// ---- configuration ---------------------------------------------------------

bool JScopeActions::applyChannel(uint8_t ch, const JScopeChannelConfig& cfg) {
    if (!_requireDriver("Channel setting")) return false;
    if (!m_session.driver()->applyChannel(ch, cfg)) {
        onRefused.emit("the device refused that channel setting");
        return false;
    }
    onConfigChanged.emit();
    return true;
}

bool JScopeActions::applyTimebase(const JScopeTimebaseConfig& cfg) {
    if (!_requireDriver("Timebase setting")) return false;
    if (!m_session.driver()->applyTimebase(cfg)) {
        onRefused.emit("the device refused that timebase");
        return false;
    }
    onConfigChanged.emit();
    return true;
}

bool JScopeActions::applyTrigger(const JScopeTriggerConfig& cfg) {
    if (!_requireDriver("Trigger setting")) return false;
    if (!m_session.driver()->applyTrigger(cfg)) {
        onRefused.emit("the device refused that trigger setting");
        return false;
    }
    onConfigChanged.emit();
    return true;
}

bool JScopeActions::setAcquisitionMode(JScopeAcquisitionMode mode) {
    if (!_requireDriver("Acquisition mode")) return false;
    JScopeDriver* d = m_session.driver();
    if (!jScopeAcquisitionModeSupported(d->capabilities().acquisitionModes, mode)) {
        onRefused.emit(std::string("this device does not support ")
                       + jScopeAcquisitionModeName(mode) + " acquisition");
        return false;
    }
    // Changing mode reprovisions the frame pool, which is a stopped-only
    // operation — so stop first rather than letting the driver refuse.
    const bool wasRunning = (d->state() != JScopeState::Stopped &&
                             d->state() != JScopeState::Idle);
    if (wasRunning) d->stop();

    JScopeTimebaseConfig tb = d->timebaseConfig();
    tb.mode = mode;
    const bool ok = applyTimebase(tb);
    if (wasRunning) run();
    return ok;
}

// ---- single-field convenience ----------------------------------------------
//
// Each reads the CURRENT config from the driver, edits one field, and writes it
// back, so a control never overwrites a neighbouring setting with a stale copy
// of it.

bool JScopeActions::setChannelEnabled(uint8_t ch, bool on) {
    if (!_requireDriver("Channel enable")) return false;
    JScopeChannelConfig c = m_session.driver()->channelConfig(ch);
    c.enabled = on;
    return applyChannel(ch, c);
}

bool JScopeActions::setVoltsPerDiv(uint8_t ch, double vdiv) {
    if (!_requireDriver("Volts/div")) return false;
    JScopeChannelConfig c = m_session.driver()->channelConfig(ch);
    c.voltsPerDiv = vdiv;
    return applyChannel(ch, c);
}

bool JScopeActions::setOffsetVolts(uint8_t ch, double volts) {
    if (!_requireDriver("Offset")) return false;
    JScopeChannelConfig c = m_session.driver()->channelConfig(ch);
    c.offsetVolts = volts;
    return applyChannel(ch, c);
}

bool JScopeActions::setCoupling(uint8_t ch, JScopeCoupling coupling) {
    if (!_requireDriver("Coupling")) return false;
    JScopeChannelConfig c = m_session.driver()->channelConfig(ch);
    c.coupling = coupling;
    return applyChannel(ch, c);
}

bool JScopeActions::setProbeRatio(uint8_t ch, double ratio) {
    if (!_requireDriver("Probe ratio")) return false;
    JScopeChannelConfig c = m_session.driver()->channelConfig(ch);
    c.probeRatio = ratio;
    return applyChannel(ch, c);
}

bool JScopeActions::setInverted(uint8_t ch, bool on) {
    if (!_requireDriver("Invert")) return false;
    JScopeChannelConfig c = m_session.driver()->channelConfig(ch);
    c.inverted = on;
    return applyChannel(ch, c);
}

bool JScopeActions::setTriggerPosition(double fraction) {
    if (!_requireDriver("Trigger position")) return false;
    JScopeTimebaseConfig tb = m_session.driver()->timebaseConfig();
    tb.triggerPosition = std::clamp(fraction, 0.0, 1.0);
    return applyTimebase(tb);
}

bool JScopeActions::setSecondsPerDiv(double sdiv) {
    if (!_requireDriver("Timebase")) return false;
    JScopeTimebaseConfig t = m_session.driver()->timebaseConfig();
    t.secondsPerDiv = sdiv;
    return applyTimebase(t);
}

bool JScopeActions::setRecordLength(uint32_t samples) {
    if (!_requireDriver("Record length")) return false;
    JScopeDriver* d = m_session.driver();
    // Reprovisioning the pool is stopped-only, so a depth change while running
    // has to stop first. This is why the control is disabled during a recording.
    const bool wasRunning = (d->state() != JScopeState::Stopped &&
                             d->state() != JScopeState::Idle);
    if (wasRunning) d->stop();

    JScopeTimebaseConfig t = d->timebaseConfig();
    t.recordLength = samples;
    const bool ok = applyTimebase(t);
    if (wasRunning) run();
    return ok;
}

bool JScopeActions::setStreamRate(double sampleRate) {
    if (!_requireDriver("Sample rate")) return false;
    JScopeTimebaseConfig t = m_session.driver()->timebaseConfig();
    t.sampleRate = sampleRate;
    return applyTimebase(t);
}

bool JScopeActions::setTriggerMode(JScopeTriggerMode mode) {
    if (!_requireDriver("Trigger mode")) return false;
    JScopeDriver* d = m_session.driver();
    if (!jScopeTriggerModeSupported(d->capabilities().triggerModes, mode)) {
        onRefused.emit(std::string("this device has no ")
                       + jScopeTriggerModeName(mode) + " trigger mode");
        return false;
    }
    JScopeTriggerConfig t = d->triggerConfig();
    t.mode = mode;
    return applyTrigger(t);
}

bool JScopeActions::setTriggerSlope(JScopeTriggerSlope slope) {
    if (!_requireDriver("Trigger slope")) return false;
    JScopeTriggerConfig t = m_session.driver()->triggerConfig();
    t.slope = slope;
    return applyTrigger(t);
}

bool JScopeActions::setTriggerSource(uint8_t ch) {
    if (!_requireDriver("Trigger source")) return false;
    JScopeTriggerConfig t = m_session.driver()->triggerConfig();
    t.sourceChannel = ch;
    return applyTrigger(t);
}

bool JScopeActions::setTriggerLevel(double volts) {
    if (!_requireDriver("Trigger level")) return false;
    JScopeTriggerConfig t = m_session.driver()->triggerConfig();
    t.levelVolts = volts;
    return applyTrigger(t);
}

} // inline namespace jf
