// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <memory>
#include "JScopeAcquisitionMode.h"
#include "JScopeCapabilities.h"
#include "JScopeOption.h"
#include "JScopeChannelConfig.h"
#include "JScopeDeviceInfo.h"
#include "JScopeFramePool.h"
#include "JScopeLog.h"
#include "JScopeState.h"
#include "JScopeTimebaseConfig.h"
#include "JScopeTriggerConfig.h"

#include <j/core/MainThreadDispatcher.h>
#include <j/core/Signal.h>

#include <atomic>
#include <string>

// A source of oscilloscope frames.
//
// THREADING. Every public method below is MAIN-THREAD ONLY. An implementation
// owns exactly one acquisition thread, which is the sole producer into pool();
// the main thread is the sole consumer. The acquisition thread never touches a
// widget, JSettings or the registry, and never emits one of these signals
// directly — the protected post*/setState helpers marshal through
// JMainThreadDispatcher first, which is the contract JSerialPort already
// honours and the reason a driver can be connected straight to a widget.
//
// CONFIGURATION IS A REQUEST. applyChannel/Timebase/Trigger quantise to a step
// the hardware supports and return true if anything was applied; what was
// actually applied is read back through the const getters. The UI re-reads after
// every apply, so a knob cannot silently disagree with the instrument.
//
// HONESTY. Anything a device cannot do is false or empty in capabilities(), and
// the UI disables the control. A driver never emulates a missing feature behind
// the caller's back — except where this header says otherwise, in one place:
// Auto/Normal/Single are host-side policy when the hardware has no sweep mode,
// because "what happens when no trigger arrives" is a decision someone has to
// make and the instrument declining to make it does not remove the need.

inline namespace jf {

class JScopeGenerator;

class JScopeDriver {
public:
    // Dropping the last strong reference to the life token is what makes any
    // callback still queued on the main thread a no-op. See _post* below.
    virtual ~JScopeDriver() { m_life.reset(); }

    JScopeDriver(const JScopeDriver&)            = delete;
    JScopeDriver& operator=(const JScopeDriver&) = delete;

    // ---- signals: always delivered on the MAIN thread -----------------------
    JSignal<std::string> onError;        // human-readable; recoverable or fatal
    JSignal<>            onDisconnect;   // the device went away
    JSignal<JScopeState> onStateChanged;
    JSignal<>            onFramesAvailable;  // coalesced wake; see _postFrameWake

    // ---- identity and lifetime ---------------------------------------------
    virtual const std::string& driverId() const = 0;
    virtual bool open(const JScopeDeviceInfo& device) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual const JScopeCapabilities& capabilities() const = 0;

    // ---- configuration ------------------------------------------------------
    virtual bool applyChannel (uint8_t ch, const JScopeChannelConfig& cfg) = 0;
    virtual bool applyTimebase(const JScopeTimebaseConfig& cfg)            = 0;
    virtual bool applyTrigger (const JScopeTriggerConfig& cfg)             = 0;

    virtual const JScopeChannelConfig&  channelConfig(uint8_t ch) const = 0;
    virtual const JScopeTimebaseConfig& timebaseConfig()          const = 0;
    virtual const JScopeTriggerConfig&  triggerConfig()           const = 0;

    // ---- acquisition control ------------------------------------------------
    virtual bool start(JScopeAcquisitionMode mode) = 0;
    virtual bool single() = 0;
    virtual void stop()   = 0;
    // Whether the sweep now in progress was asked to be a SINGLE one. Not the
    // same question as the trigger mode: pressing Single takes one shot whatever
    // the mode says, and the display has to report what is actually happening
    // rather than the policy that would otherwise apply.
    virtual bool singleShotPending() const { return false; }

    virtual bool forceTrigger() = 0;   // false when !capabilities().hasForceTrigger
    virtual bool autoset()      = 0;   // false when !capabilities().hasAutoset

    JScopeState state() const { return m_state.load(std::memory_order_acquire); }

    // ---- frame delivery -----------------------------------------------------
    JScopeFramePool&       pool()       { return m_pool; }
    const JScopeFramePool& pool() const { return m_pool; }
    uint64_t framesDropped() const { return m_pool.dropped(); }

    // ---- optional sub-device ------------------------------------------------
    // nullptr unless capabilities().generator.kind says otherwise. A separate
    // interface rather than extra verbs here: a function generator is a
    // different instrument that happens to share a USB endpoint.
    // ---- what the INSTRUMENT says about itself ------------------------------
    //
    // For a display that mirrors the scope rather than describing what this
    // application would have chosen. A bench instrument knows whether it is
    // triggered, armed or stopped, and what acquisition it is running; that is
    // its answer to give, not ours to infer from the sweep mode we asked for.
    //
    // Empty means the device reports nothing, which is the honest answer for an
    // instrument with no front panel of its own. The display then falls back to
    // what the application knows. Nothing here is ever WRITTEN -- these are
    // read-backs, and a driver that cannot read them says so by staying empty.
    virtual std::string triggerStatusText() const { return {}; }   // TRIG'd, AUTO, STOP
    virtual std::string acquisitionText()   const { return {}; }   // NORMal, AVG 4, PEAK

    // ---- settings the instrument itself has ---------------------------------
    //
    // Published by the driver so the application can build a menu for them
    // without knowing which scope it is talking to. See JScopeOption.
    //
    // Read at connect, and CHANGED ONLY WHEN THE USER ASKS. Connecting must
    // never write any of this: the setup already on the instrument is the one
    // its owner made.
    virtual std::vector<JScopeOption> instrumentOptions() const { return {}; }
    virtual bool setInstrumentOption(const std::string& id, const std::string& value) {
        (void)id; (void)value;
        return false;
    }

    virtual JScopeGenerator*       generator()       { return nullptr; }
    virtual const JScopeGenerator* generator() const { return nullptr; }

protected:
    JScopeDriver() = default;

    // ---- producer-side helpers: ACQUISITION THREAD ONLY ---------------------

    // Publish a filled frame and wake the consumer. At most ONE wake is ever
    // outstanding, so dispatcher traffic is bounded by the display rate rather
    // than the acquisition rate — the difference between a scope that stays
    // responsive at 10 kframes/s and one that drowns its own UI thread.
    void _publish(JScopeFrame* f) {
        if (!f) return;
        m_pool.publish(f);
        _postFrameWake();
    }

    // EVERY main-thread post from a driver goes through here.
    //
    // A driver posts callbacks that capture `this` and runs them later, on the
    // main thread, when the dispatcher drains. Closing a device destroys the
    // driver — and the dispatcher has no way to cancel what is already queued,
    // so a callback posted moments earlier then ran against freed memory.
    //
    // On the bench that was: the 1008C running and publishing frames, the user
    // picking another instrument from the Device menu, close() destroying the
    // driver with a frame wake still queued, and the next drain writing through
    // a dangling this. AddressSanitizer named it exactly —
    //   heap-use-after-free ... in JScopeDriver::_postFrameWake()::{lambda()}
    // — and the visible symptom was the process aborting later inside a 4 MB
    // allocation with "pthread_mutex_lock: assertion failed", because a
    // corrupted heap surfaces where it is next used rather than where it was
    // damaged.
    //
    // The token is held weakly by the callback. Destroying the driver drops the
    // last strong reference, and anything still queued finds it expired and
    // returns. Destruction and draining both happen on the main thread, so
    // there is no window between the two.
    template <typename F>
    void _postGuarded(F&& fn) {
        JMainThreadDispatcher::instance().post(
            [guard = std::weak_ptr<const void>(m_life), f = std::forward<F>(fn)]() mutable {
                if (guard.expired()) return;
                f();
            });
    }

    void _postError(std::string message) {
        JLOGC(JScopeLog::kScope, JLogLevel::Error) << driverId() << ": " << message;
        _postGuarded([this, m = std::move(message)]() mutable { onError.emit(std::move(m)); });
    }

    void _postDisconnect() {
        JLOGC(JScopeLog::kScope, JLogLevel::Warn) << driverId() << ": device disconnected";
        _postGuarded([this] { onDisconnect.emit(); });
    }

    // Safe from either thread.
    void _setState(JScopeState s) {
        const JScopeState prev = m_state.exchange(s, std::memory_order_acq_rel);
        if (prev == s) return;
        JLOGC(JScopeLog::kScope, JLogLevel::Debug)
            << driverId() << ": " << jScopeStateName(prev) << " -> " << jScopeStateName(s);
        _postGuarded([this, s] { onStateChanged.emit(s); });
    }

    JScopeFramePool m_pool;

private:
    // Alive for exactly as long as this driver is. Never dereferenced — only its
    // expiry is asked about.
    std::shared_ptr<const void> m_life{ std::make_shared<char>() };

    void _postFrameWake() {
        if (m_wakePending.exchange(true, std::memory_order_acq_rel)) return;
        _postGuarded([this] {
            m_wakePending.store(false, std::memory_order_release);
            onFramesAvailable.emit();
        });
    }

    std::atomic<JScopeState> m_state{JScopeState::Closed};
    std::atomic<bool>        m_wakePending{false};
};

} // inline namespace jf
