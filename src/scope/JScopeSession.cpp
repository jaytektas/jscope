// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JScopeSession.h"

#include <j/core/MainThreadDispatcher.h>
#include "JScopeDriverRegistry.h"
#include "sources/JReplayDriver.h"
#include "JScopeLog.h"

#include <algorithm>

inline namespace jf {

namespace {
// Drain cadence. Not a visual constant — it is the rate at which the UI thread
// is willing to look at the ring, and it is deliberately faster than a 60 Hz
// refresh so a frame is never a whole tick stale.
constexpr int kDrainIntervalMs = 8;
}

JScopeSession::JScopeSession() = default;

JReplayDriver* JScopeSession::replay() {
    if (!m_driver || m_driver->driverId() != "replay") return nullptr;
    return static_cast<JReplayDriver*>(m_driver.get());
}

JScopeSession::~JScopeSession() { close(); }

bool JScopeSession::open(const JScopeDeviceInfo& device) {
    if (!_createDriver(device)) return false;

    if (!m_driver->open(device)) {
        JLOGC(JScopeLog::kScope, JLogLevel::Error) << "driver refused to open the device";
        onError.emit("failed to open " + device.displayName);
        m_driver.reset();
        return false;
    }

    _finishOpen(device);
    return true;
}

void JScopeSession::openAsync(const JScopeDeviceInfo& device, std::function<void(bool)> done) {
    if (m_connecting.load()) {
        JLOGC(JScopeLog::kScope, JLogLevel::Warn)
            << "already connecting; ignoring a second request for '" << device.displayName << "'";
        if (done) done(false);
        return;
    }

    if (!_createDriver(device)) { if (done) done(false); return; }

    m_connecting.store(true);
    onConnecting.emit(device.displayName);

    // The driver is created on this thread and OPENED on the worker. Nothing
    // else may touch it until the worker is finished, which is what
    // isConnecting() is for — driver() would otherwise hand out a half-open
    // instrument to a panel rebuild.
    JScopeDriver* raw = m_driver.get();
    const uint64_t epoch = m_epoch.load();
    m_deviceWorker.post([this, raw, device, epoch, done = std::move(done)]() mutable {
        const bool ok = raw->open(device);
        JMainThreadDispatcher::instance().post([this, ok, device, epoch, done = std::move(done)] {
            // close() ran while this was in the queue: the driver it refers to
            // is already gone, and close() has reset the flags itself.
            if (epoch != m_epoch.load()) { if (done) done(false); return; }
            m_connecting.store(false);
            if (ok) {
                _finishOpen(device);
            } else {
                JLOGC(JScopeLog::kScope, JLogLevel::Error)
                    << "driver refused to open the device";
                m_driver.reset();
                onError.emit("failed to open " + device.displayName);
            }
            if (done) done(ok);
        });
    });
}

bool JScopeSession::runOnDevice(std::function<void()> work, std::function<void()> done) {
    if (!m_driver || isBusy()) {
        JLOGC(JScopeLog::kScope, JLogLevel::Warn)
            << "device is busy; refusing another operation";
        return false;
    }
    m_deviceBusy.store(true);
    const uint64_t epoch = m_epoch.load();
    m_deviceWorker.post([this, work = std::move(work), done = std::move(done), epoch]() mutable {
        work();
        JMainThreadDispatcher::instance().post([this, done = std::move(done), epoch] {
            if (epoch != m_epoch.load()) return;   // closed under us; see close()
            m_deviceBusy.store(false);
            if (done) done();
        });
    });
    return true;
}

bool JScopeSession::_createDriver(const JScopeDeviceInfo& device) {
    close();

    JLOGC(JScopeLog::kScope, JLogLevel::Info)
        << "session opening '" << device.displayName << "' via driver '" << device.driverId << "'";

    m_driver = JScopeDriverRegistry::instance().create(device.driverId);
    if (!m_driver) {
        onError.emit("no driver registered for '" + device.driverId + "'");
        return false;
    }
    return true;
}

void JScopeSession::_wireDriverSignals() {
    // Connected only once the driver is open AND we are back on the main thread.
    //
    // A driver's open() emits: JSyntheticDriver::open ends in _setState, which
    // fires onStateChanged synchronously. That was always safe because open()
    // was always called on the main thread. It is not safe on the connect
    // worker, and connecting these before handing the driver over meant a
    // state change during open() reached the widgets from the wrong thread —
    // glibc aborted with "pthread_mutex_lock: assertion failed:
    // mutex->__data.__owner == 0" the first time a device was chosen from the
    // Device menu.
    //
    // Nothing is lost by connecting late. A signal emitted DURING open()
    // describes a driver the session has not adopted yet, and whether the open
    // worked is reported by openAsync's callback rather than by these.
    m_driver->onError.connect([this](std::string m) { onError.emit(std::move(m)); });
    m_driver->onStateChanged.connect([this](JScopeState s) { onStateChanged.emit(s); });
    m_driver->onFramesAvailable.connect([this] { drain(); });
}

void JScopeSession::_finishOpen(const JScopeDeviceInfo&) {
    _wireDriverSignals();

    m_received     = 0;
    m_lastSequence = 0;
    m_hasFrame     = false;
    _startTicking();

    JLOGC(JScopeLog::kScope, JLogLevel::Info)
        << "session open: " << m_driver->capabilities().model
        << " (" << int(m_driver->capabilities().channelCount()) << " channels, "
        << int(m_driver->capabilities().adcBits) << "-bit)";
}

void JScopeSession::close() {
    // A connect or an autoset may be IN FLIGHT on the device worker at this
    // moment, inside driver->open(). openAsync hands the worker a bare pointer
    // to m_driver on the promise that nothing else touches it until the worker
    // is finished — and this is the path that used to break that promise:
    // resetting m_driver here freed the transport the worker was still writing
    // to, and the fault landed in JUsbTmcSession::write.
    //
    // Closing the window while a wedged DSO2D15 was being opened was enough to
    // hit it, because that open sits in two five-second bulk timeouts.
    //
    // Draining first is what makes the teardown ordered. The bare pointer stays
    // valid for as long as the worker holds it, since m_driver is not reset
    // until after the wait. When nothing is in flight — the ordinary case —
    // waitForIdle returns immediately and this costs nothing.
    ++m_epoch;
    m_deviceWorker.waitForIdle();
    m_connecting.store(false);
    m_deviceBusy.store(false);

    if (!m_driver) return;
    JLOGC(JScopeLog::kScope, JLogLevel::Info)
        << "session closing after " << m_received << " frame(s)";
    _stopTicking();
    m_driver->close();
    m_driver.reset();
    m_hasFrame = false;
}

void JScopeSession::_startTicking() {
    m_tick = std::make_unique<JFrameTimer>(kDrainIntervalMs, /*singleShot=*/false);
    m_tick->timeout.connect([this] { drain(); });
    m_tick->start();
    JLOGC(JScopeLog::kScope, JLogLevel::Debug)
        << "drain tick started at " << kDrainIntervalMs << "ms";
}

void JScopeSession::_stopTicking() {
    if (!m_tick) return;
    m_tick->stop();
    m_tick.reset();
}

size_t JScopeSession::drain() {
    if (!m_driver) return 0;

    JScopeFramePool& pool = m_driver->pool();
    size_t taken = 0;
    bool   got   = false;

    // Take everything waiting, but keep only the newest. A scope shows the
    // latest acquisition; the intermediate ones are already history by the time
    // the compositor could have shown them.
    while (JScopeFrame* f = pool.tryPopReady()) {
        ++taken;
        ++m_received;

        if (m_latest.maxChannels() < f->header.channelCount ||
            m_latest.maxSamples()  < f->header.sampleCount) {
            m_latest.provision(std::max<uint8_t>(f->header.channelCount, f->maxChannels()),
                               f->header.sampleCount);
            JLOGC(JScopeLog::kFrames, JLogLevel::Debug)
                << "session frame provisioned for " << f->header.sampleCount << " samples/ch";
        }
        m_latest.shape(f->header.channelCount, f->header.sampleCount);
        m_latest.copyHeaderAndSamplesFrom(*f);

        pool.release(f);
        got = true;
    }

    if (!got) return 0;

    // A gap in the sequence means the ring dropped, and that must be visible
    // rather than silent.
    if (m_hasFrame && m_latest.header.sequence > m_lastSequence + taken) {
        JLOGC(JScopeLog::kFrames, JLogLevel::Debug)
            << "sequence gap: " << m_lastSequence << " -> " << m_latest.header.sequence
            << " (" << m_driver->framesDropped() << " dropped in total)";
    }
    m_lastSequence = m_latest.header.sequence;
    m_hasFrame     = true;

    onFrame.emit(m_latest);
    return taken;
}

} // inline namespace jf
