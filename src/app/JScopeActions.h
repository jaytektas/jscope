// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "scope/JScopeAcquisitionMode.h"
#include "scope/JScopeChannelConfig.h"
#include "scope/JScopeTimebaseConfig.h"
#include "scope/JPatternGenerator.h"
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

    // ---- the signal generator -------------------------------------------------
    // All refuse, with a reason, on a device whose capabilities declare no
    // generator — the panel is not built in that case, but an action must not
    // depend on a panel's absence for its correctness.
    bool setGeneratorOutput (bool on);
    bool setGeneratorRpm    (uint32_t rpm);
    bool setGeneratorPattern(const std::vector<uint8_t>& pattern);

    // SEND THE PATTERN TO THE DEVICE, as a deliberate act. Editing a cell no
    // longer writes to the instrument: at 1440 pulses a pattern is 26 commands,
    // and issuing those on every click would put a burst of USB traffic behind
    // each stroke of the pencil. The OEM has a Download for the same reason.
    bool downloadGeneratorPattern();

    // The OEM's own .squ files, so a pattern built in either application opens in
    // the other.
    bool loadGeneratorPattern(const std::string& path);
    bool saveGeneratorPattern(const std::string& path) const;

    // The generator the open device publishes, or nullptr. Panels read their
    // current state through this rather than keeping a copy that could drift.
    JPatternGenerator*       patternGenerator();
    const JPatternGenerator* patternGenerator() const;

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
