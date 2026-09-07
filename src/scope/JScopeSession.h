// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JScopeDriver.h"
#include <j/concurrent/WorkerThread.h>
#include <j/core/FrameTimer.h>
#include <functional>
#include <atomic>
#include <j/core/Signal.h>
#include <memory>
#include <string>
#include <vector>

// The main thread's handle on one open device.
//
// It owns the driver, drains its ready ring once per displayed frame, and hands
// the newest acquisition to whoever is listening. The drain is paced by a
// JFrameTimer rather than a JTimer because this is UI work ticked inside the
// render loop: no second thread, and no clock skew against the compositor.
//
// Draining per FRAME rather than per acquisition is the point. A driver may
// publish faster than the display refreshes; the session takes everything
// waiting, keeps the newest, and repaints once. That is why the driver's wake
// signal is coalesced and why the ring drops oldest rather than blocking.

inline namespace jf {

class JScopeSession {
public:
    JScopeSession();
    ~JScopeSession();

    JScopeSession(const JScopeSession&)            = delete;
    JScopeSession& operator=(const JScopeSession&) = delete;

    // Create the driver named by device.driverId, open it, and start draining.
    // BLOCKS for as long as the instrument's bring-up takes, which for the
    // 1008C is three vertical ranges of 500 averaged samples with protocol
    // delays between transfers — about twenty seconds. Only call it where a
    // frozen UI is impossible, which in practice means never from a handler.
    bool open(const JScopeDeviceInfo& device);

    // The same, with the instrument's bring-up on a worker thread and `done`
    // delivered on the main thread. This is what a menu selection must use: the
    // synchronous form locks the window for the whole bring-up, and if the
    // device is not actually there, for the USB timeouts on top of it.
    //
    // Refuses, calling done(false), if a connect is already in flight.
    void openAsync(const JScopeDeviceInfo& device, std::function<void(bool)> done);
    bool isConnecting() const { return m_connecting; }

    // Run one long instrument operation off the UI thread, with `done` delivered
    // on the main thread once it has finished.
    //
    // For anything that drives the device for longer than a frame. Autoset is
    // the example that forced it: it stops the acquisition, sweeps the vertical
    // ranges and takes a capture on each, which is seconds of USB with sleeps
    // between transfers — and inline that is seconds of a window that does not
    // repaint.
    //
    // Refuses, returning false, while a connect or another operation is already
    // in flight. The driver is not reentrant and the acquisition thread is
    // already using it.
    bool runOnDevice(std::function<void()> work, std::function<void()> done);
    bool isBusy() const { return m_connecting || m_deviceBusy; }
    void close();
    bool isOpen() const { return m_driver && m_driver->isOpen(); }

    JScopeDriver*       driver()       { return m_driver.get(); }
    const JScopeDriver* driver() const { return m_driver.get(); }

    // The open driver as a JReplayDriver, or nullptr for anything else. The
    // playback verbs are not on JScopeDriver because no real instrument has
    // them, so this is how the transport bar reaches them — and its being null
    // is how the UI knows not to show one.
    class JReplayDriver* replay();

    // The most recent acquisition, or nullptr before the first one arrives. Owned
    // by the session; valid until the next drain.
    const JScopeFrame* latestFrame() const { return m_hasFrame ? &m_latest : nullptr; }

    uint64_t framesReceived() const { return m_received; }
    uint64_t framesDropped()  const { return m_driver ? m_driver->framesDropped() : 0; }

    // A new frame is on display. Fired at most once per displayed frame, however
    // many acquisitions arrived in between.
    JSignal<const JScopeFrame&> onFrame;
    JSignal<std::string>        onError;
    JSignal<JScopeState>        onStateChanged;

    // A connect has started. Carries the device's display name, so the window
    // can say what it is waiting for rather than simply stopping.
    JSignal<std::string>        onConnecting;

    // Drain now rather than waiting for the tick. Used by tests, which have no
    // frame loop to tick them.
    size_t drain();

private:
    void _startTicking();
    void _stopTicking();

    // Shared by open() and openAsync(): everything after the instrument has
    // answered. Runs on the main thread in both cases.
    bool _createDriver(const JScopeDeviceInfo& device);
    void _wireDriverSignals();
    void _finishOpen(const JScopeDeviceInfo& device);

    std::unique_ptr<JScopeDriver> m_driver;
    std::unique_ptr<JFrameTimer>  m_tick;

    // The device worker. One thread, created with the session and reused, so a
    // bring-up that takes twenty seconds — or an autoset that takes several —
    // does it somewhere other than the UI.
    // The driver being opened lives in m_driver throughout — m_connecting is
    // what stops anything else touching it while the worker has it.
    JWorkerThread     m_deviceWorker;
    std::atomic<bool> m_connecting{false};
    std::atomic<bool> m_deviceBusy{false};

    // Bumped by close(). A worker posts its completion back to the main thread,
    // and that continuation can still be sitting in the dispatcher's queue when
    // the session is closed — it would then run against a driver that has been
    // destroyed, or against the next device's. Continuations carry the epoch
    // they were posted under and do nothing if it has moved on.
    std::atomic<uint64_t> m_epoch{0};

    JScopeFrame m_latest;
    bool        m_hasFrame{false};
    uint64_t    m_received{0};
    uint64_t    m_lastSequence{0};
};

} // inline namespace jf
