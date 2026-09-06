#include "JScopeSettings.h"

#include "JScopeActions.h"
#include "scope/JScopeDriver.h"
#include "scope/JScopeLog.h"

#include <j/config/Settings.h>

#include <filesystem>

inline namespace jf {

namespace {
constexpr const char* kSavedMarker = "saved";
}

JScopeSettings::JScopeSettings(std::string path) : m_path(std::move(path)) {}

std::string JScopeSettings::_key(const std::string& driverId, const std::string& leaf) {
    return "scope." + driverId + "." + leaf;
}

std::string JScopeSettings::_channelKey(const std::string& driverId, uint8_t ch,
                                        const std::string& leaf) {
    return "scope." + driverId + ".ch" + std::to_string(ch) + "." + leaf;
}

// The file is read once. Re-reading per call was how the old code kept another
// driver's section from being clobbered; holding the object does the same thing
// and, unlike a stack-local, is still alive when JSettings::set's deferred
// onChange emit reaches the dispatcher.
JSettings& JScopeSettings::_settings() const {
    if (!m_loaded) {
        m_settings.setPath(m_path).loadJson();
        m_loaded = true;
    }
    return m_settings;
}

void JScopeSettings::save(const JScopeDriver& driver, const JScopeViewState& view) {
    JSettings& s = _settings();

    const std::string id = driver.driverId();
    const JScopeTimebaseConfig& tb = driver.timebaseConfig();
    const JScopeTriggerConfig&  tr = driver.triggerConfig();

    s.set(_key(id, kSavedMarker),        true);
    s.set(_key(id, "mode"),              static_cast<int>(tb.mode));
    s.set(_key(id, "secondsPerDiv"),     tb.secondsPerDiv);
    s.set(_key(id, "recordLength"),      static_cast<int>(tb.recordLength));
    s.set(_key(id, "sampleRate"),        tb.sampleRate);
    s.set(_key(id, "triggerPosition"),   tb.triggerPosition);
    s.set(_key(id, "triggerMode"),       static_cast<int>(tr.mode));
    s.set(_key(id, "triggerSlope"),      static_cast<int>(tr.slope));
    s.set(_key(id, "triggerSource"),     static_cast<int>(tr.sourceChannel));
    s.set(_key(id, "triggerLevel"),      tr.levelVolts);

    for (uint8_t c = 0; c < driver.capabilities().channelCount(); ++c) {
        const JScopeChannelConfig& cfg = driver.channelConfig(c);
        s.set(_channelKey(id, c, "enabled"),     cfg.enabled);
        s.set(_channelKey(id, c, "voltsPerDiv"), cfg.voltsPerDiv);
        s.set(_channelKey(id, c, "offsetVolts"), cfg.offsetVolts);
        s.set(_channelKey(id, c, "coupling"),    static_cast<int>(cfg.coupling));
        s.set(_channelKey(id, c, "probeRatio"),  cfg.probeRatio);
        s.set(_channelKey(id, c, "inverted"),    cfg.inverted);
    }

    for (uint8_t c = 0; c < driver.capabilities().channelCount(); ++c)
        s.set(_channelKey(id, c, "positionVolts"), view.positionVolts[c]);

    s.set(_key(id, "cursorXEnabled"), view.cursorXEnabled);
    s.set(_key(id, "cursorYEnabled"), view.cursorYEnabled);
    s.set(_key(id, "cursorX1"),       view.cursorX1);
    s.set(_key(id, "cursorX2"),       view.cursorX2);
    s.set(_key(id, "cursorY1"),       view.cursorY1);
    s.set(_key(id, "cursorY2"),       view.cursorY2);
    s.set(_key(id, "cursorYChannel"), static_cast<int>(view.cursorYChannel));

    if (s.saveJson())
        JLOGC(JScopeLog::kUi, JLogLevel::Info)
            << "settings saved for '" << id << "' to " << m_path;
    else
        JLOGC(JScopeLog::kUi, JLogLevel::Warn)
            << "could not write settings to " << m_path;
}

bool JScopeSettings::restore(const JScopeDriver& driver, JScopeActions& actions) {
    // loadJson() is a no-op on a missing file, so absence shows up as the
    // driver's marker key being absent — checked just below. A missing settings
    // file is the normal first run, not a failure.
    JSettings& s = _settings();

    const std::string id = driver.driverId();
    if (!s.get<bool>(_key(id, kSavedMarker), false)) {
        JLOGC(JScopeLog::kUi, JLogLevel::Info)
            << "no stored settings for driver '" << id << "' — using device defaults";
        return false;
    }

    // Restore through JScopeActions, not straight at the driver: a stored value
    // this device cannot do must be quantised or refused exactly as a typed one
    // would be. Settings files outlive driver changes and get carried between
    // machines with different instruments attached.
    JScopeTimebaseConfig tb = driver.timebaseConfig();
    tb.mode            = static_cast<JScopeAcquisitionMode>(
                             s.get<int>(_key(id, "mode"), static_cast<int>(tb.mode)));
    tb.secondsPerDiv   = s.get<double>(_key(id, "secondsPerDiv"), tb.secondsPerDiv);
    tb.recordLength    = static_cast<uint32_t>(
                             s.get<int>(_key(id, "recordLength"),
                                        static_cast<int>(tb.recordLength)));
    tb.sampleRate      = s.get<double>(_key(id, "sampleRate"), tb.sampleRate);
    tb.triggerPosition = s.get<double>(_key(id, "triggerPosition"), tb.triggerPosition);
    actions.applyTimebase(tb);

    JScopeTriggerConfig tr = driver.triggerConfig();
    tr.mode          = static_cast<JScopeTriggerMode>(
                           s.get<int>(_key(id, "triggerMode"), static_cast<int>(tr.mode)));
    tr.slope         = static_cast<JScopeTriggerSlope>(
                           s.get<int>(_key(id, "triggerSlope"), static_cast<int>(tr.slope)));
    tr.sourceChannel = static_cast<uint8_t>(
                           s.get<int>(_key(id, "triggerSource"), tr.sourceChannel));
    tr.levelVolts    = s.get<double>(_key(id, "triggerLevel"), tr.levelVolts);
    actions.applyTrigger(tr);

    for (uint8_t c = 0; c < driver.capabilities().channelCount(); ++c) {
        JScopeChannelConfig cfg = driver.channelConfig(c);
        cfg.enabled     = s.get<bool>  (_channelKey(id, c, "enabled"),     cfg.enabled);
        cfg.voltsPerDiv = s.get<double>(_channelKey(id, c, "voltsPerDiv"), cfg.voltsPerDiv);
        cfg.offsetVolts = s.get<double>(_channelKey(id, c, "offsetVolts"), cfg.offsetVolts);
        cfg.coupling    = static_cast<JScopeCoupling>(
                              s.get<int>(_channelKey(id, c, "coupling"),
                                         static_cast<int>(cfg.coupling)));
        cfg.probeRatio  = s.get<double>(_channelKey(id, c, "probeRatio"),  cfg.probeRatio);
        cfg.inverted    = s.get<bool>  (_channelKey(id, c, "inverted"),    cfg.inverted);
        actions.applyChannel(c, cfg);
    }

    JLOGC(JScopeLog::kUi, JLogLevel::Info)
        << "settings restored for '" << id << "' from " << m_path;
    return true;
}

JScopeViewState JScopeSettings::restoreView(const JScopeDriver& driver) const {
    JScopeViewState v;

    JSettings& s = _settings();

    const std::string id = driver.driverId();
    if (!s.get<bool>(_key(id, kSavedMarker), false)) return v;

    for (uint8_t c = 0; c < driver.capabilities().channelCount(); ++c)
        v.positionVolts[c] = s.get<double>(_channelKey(id, c, "positionVolts"), 0.0);

    // Every default is the struct's own.
    v.cursorXEnabled = s.get<bool>  (_key(id, "cursorXEnabled"), v.cursorXEnabled);
    v.cursorYEnabled = s.get<bool>  (_key(id, "cursorYEnabled"), v.cursorYEnabled);
    v.cursorX1       = s.get<double>(_key(id, "cursorX1"),       v.cursorX1);
    v.cursorX2       = s.get<double>(_key(id, "cursorX2"),       v.cursorX2);
    v.cursorY1       = s.get<double>(_key(id, "cursorY1"),       v.cursorY1);
    v.cursorY2       = s.get<double>(_key(id, "cursorY2"),       v.cursorY2);
    v.cursorYChannel = static_cast<uint8_t>(
        s.get<int>(_key(id, "cursorYChannel"), v.cursorYChannel));

    JLOGC(JScopeLog::kUi, JLogLevel::Info) << "view state restored for '" << id << "'";
    return v;
}



// ---- the last instrument that opened -------------------------------------
//
// Kept OUTSIDE the per-driver sections, because it is a statement about which
// driver to reach for first and so cannot live under any one of them.
namespace {
constexpr const char* kLastDriver = "scope.lastDevice.driverId";
constexpr const char* kLastPort   = "scope.lastDevice.portPath";
constexpr const char* kLastSerial = "scope.lastDevice.serialNumber";
}

void JScopeSettings::rememberDevice(const JScopeDeviceInfo& device) {
    JSettings& s = _settings();
    s.set(kLastDriver, device.driverId);
    s.set(kLastPort,   device.portPath);
    s.set(kLastSerial, device.serialNumber);
    s.saveJson();
}

JScopeDeviceInfo JScopeSettings::lastDevice() const {
    JSettings& s = _settings();
    JScopeDeviceInfo d;
    d.driverId     = s.get<std::string>(kLastDriver, std::string{});
    d.portPath     = s.get<std::string>(kLastPort,   std::string{});
    d.serialNumber = s.get<std::string>(kLastSerial, std::string{});
    return d;
}

} // inline namespace jf
