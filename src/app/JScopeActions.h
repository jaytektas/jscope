#pragma once

#include "scope/JScopeAcquisitionMode.h"
#include "scope/JScopeChannelConfig.h"
#include "scope/JScopeTimebaseConfig.h"
#include "scope/JScopeTriggerConfig.h"

#include <j/core/Signal.h>
#include <cstdint>
#include <string>

inline namespace jf {

class JScopeSession;

// Every operation the user can perform on the instrument, in one place.
//
// The menu, the toolbar, the keyboard shortcuts and the control panels all
// reach the driver through here rather than each carrying its own copy of
// "if (driver) driver->start(...)". That is not tidiness for its own sake: the
// capability checks, the read-back after every apply, and the notification that
// keeps the trace view in step with the instrument all have to happen on every
// path, and four copies of them would be four chances to forget one.
//
// Every applyX() writes the request, then reads back what the driver actually
// accepted and emits onConfigChanged. The UI updates from the read-back, never
// from what it asked for, so a control cannot end up displaying a value the
// hardware refused.
class JScopeActions {
public:
    explicit JScopeActions(JScopeSession& session) : m_session(session) {}

    JScopeActions(const JScopeActions&)            = delete;
    JScopeActions& operator=(const JScopeActions&) = delete;

    // ---- acquisition ----
    // Each returns false when there is no device, or when the device says it
    // cannot do this. A false is reported through onRefused with a reason, so
    // pressing a button the instrument does not support says so instead of
    // appearing to do nothing.
    bool run();
    bool stop();
    bool single();
    bool forceTrigger();
    bool autoset();
    bool toggleRunStop();

    // ---- configuration ----
    bool applyChannel (uint8_t ch, const JScopeChannelConfig& cfg);
    bool applyTimebase(const JScopeTimebaseConfig& cfg);
    bool applyTrigger (const JScopeTriggerConfig& cfg);
    bool setAcquisitionMode(JScopeAcquisitionMode mode);

    // Convenience for a control that changes one field: reads the current
    // config, applies the edit, writes it back.
    bool setChannelEnabled(uint8_t ch, bool on);
    bool setVoltsPerDiv   (uint8_t ch, double vdiv);
    bool setOffsetVolts   (uint8_t ch, double volts);
    bool setCoupling      (uint8_t ch, JScopeCoupling c);
    bool setProbeRatio    (uint8_t ch, double ratio);
    bool setInverted      (uint8_t ch, bool on);
    bool setSecondsPerDiv (double sdiv);
    bool setRecordLength  (uint32_t samples);
    bool setStreamRate    (double sampleRate);
    bool setTriggerMode   (JScopeTriggerMode mode);
    bool setTriggerSlope  (JScopeTriggerSlope slope);
    bool setTriggerSource (uint8_t ch);
    bool setTriggerLevel  (double volts);
    // Where the trigger sits in the record, 0..1. On this instrument it is the
    // 0xac pre/post split; see docs/hantek1008-oem-behaviour.md.
    bool setTriggerPosition(double fraction);

    // The instrument's settings changed and the read-back is available. Panels
    // and the trace view refresh from the driver when this fires.
    JSignal<> onConfigChanged;

    // An action could not be carried out, with a human-readable reason.
    JSignal<std::string> onRefused;

    // A long instrument operation started or finished. The window uses it to say
    // what is happening, since the operation no longer blocks the UI and there
    // would otherwise be nothing on screen to show that anything is underway.
    JSignal<bool> onBusyChanged;

private:
    bool _requireDriver(const char* what);

    JScopeSession& m_session;
};

} // inline namespace jf
