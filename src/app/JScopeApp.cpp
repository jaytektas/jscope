#include "JScopeApp.h"

#include "JScopeWindowIdentity.h"
#include "JScopeMenuBuilder.h"
#include "JScopeShortcuts.h"
#include "JScopeToolBarBuilder.h"

#include "capture/JCsvExport.h"
#include "sources/JReplayDriver.h"
#include "scope/JScopeDriverRegistry.h"
#include "scope/JScopeLog.h"
#include "scope/JScopeSweepLabel.h"
#include "ui/JScopeTheme.h"

#include <cstdlib>
#include <filesystem>
#include <vector>
#include <sstream>

inline namespace jf {

namespace {
constexpr const char* kWindowTitle = "JScope";
constexpr uint32_t    kWindowWidth  = 1400;
constexpr uint32_t    kWindowHeight = 860;
// How long a refusal stays in the status bar. Long enough to read, short enough
// not to bury the live readout.
constexpr int         kRefusalNoticeMs = 3000;
// Long enough to cover the 1008C's bring-up, which is what the message is about.
constexpr int         kConnectNoticeMs = 30000;
// How often the main thread checks for a termination signal. Frame-paced, so it
// costs nothing when the loop is already awake and does not spin when it is not.
constexpr float       kShutdownPollMs  = 100.0f;
}

std::atomic<bool> JScopeApp::s_terminate{false};

void JScopeApp::requestTerminate() { s_terminate.store(true, std::memory_order_release); }

std::string JScopeApp::defaultCaptureDir() {
    if (const char* home = std::getenv("HOME"))
        return std::string(home) + "/jscope-captures";
    return "jscope-captures";
}

std::string JScopeApp::defaultSettingsPath() {
    // ONE FILE PER APPLICATION, named after the application.
    //
    // There is an application per instrument now, and they do not share a setup.
    // Sharing one file meant the 1008C application inherited "the instrument
    // used last time: dso2d15" from its sibling and then reported that scope
    // missing from the bus -- while it sat there plugged in, belonging to the
    // other application, whose driver this one does not even link.
    const std::string name = s_applicationName.empty() ? "jscope" : s_applicationName;
    if (const char* home = std::getenv("HOME"))
        return std::string(home) + "/.config/jscope/" + name + ".json";
    return name + "-settings.json";
}

std::string JScopeApp::s_applicationName;

void JScopeApp::setApplicationName(std::string name) {
    s_applicationName = std::move(name);
}

std::string JScopeApp::s_preferredDriver;

void JScopeApp::preferDriver(std::string driverId) {
    s_preferredDriver = std::move(driverId);
}

JScopeApp::JScopeApp(std::string settingsPath) : m_settings(std::move(settingsPath)) {
    JLOGC(JScopeLog::kUi, JLogLevel::Info) << "starting " << kWindowTitle;

    m_window = std::make_unique<JAppWindow>(kWindowTitle, kWindowWidth, kWindowHeight);

    // Before anything else uses the window: the desktop reads WM_CLASS when the
    // window first appears, and JFramework's default makes every application it
    // builds look like the same one.
    applyWindowIdentity(*m_window);
    if (!m_window->valid()) {
        JLOGC(JScopeLog::kUi, JLogLevel::Error) << "window / GPU HAL init failed";
        return;
    }

    JScopeTheme::reseedFromStyle();

    m_traceView = std::make_unique<JTraceView>(m_app.sceneGraph());
    m_window->setCentralWidget(m_traceView.get());

    m_docks = std::make_unique<JScopeDockLayout>(*m_window, m_app.sceneGraph(), m_actions,
                                                 m_traceView->cursors());

    JScopeMenuBuilder::build(*m_window, m_app.sceneGraph(), *this);
    JScopeToolBarBuilder::build(*m_window, *this);
    JScopeShortcuts::install(*m_window, *this);

    _wireSession();
    _wireActions();
    _wireFrameTiming();
    _wireShutdown();
    _wireMeasurements();

    // A live status line, so drops and state changes are visible without
    // anyone having to ask for them.
    m_window->setLiveStatus([this] {
        std::ostringstream ss;
        // A device that cannot acquire has no run state worth reporting. Showing
        // the driver's last one read as "Triggered" beside a permanently empty
        // graticule and a frame count of zero, which is three things on screen
        // disagreeing with each other.
        const JScopeDriver* d = m_session.driver();
        const bool canAcquire = d && d->capabilities().acquisitionModes != 0;
        ss << m_statusDevice << "  |  "
           << (canAcquire ? m_statusState : std::string("no acquisition"))
           << "  |  frames " << m_session.framesReceived();
        const uint64_t dropped = m_session.framesDropped();
        if (dropped > 0) ss << "  |  dropped " << dropped;
        return ss.str();
    });

    JLOGC(JScopeLog::kUi, JLogLevel::Info) << "window and shell constructed";
}

JScopeApp::~JScopeApp() {
    JLOGC(JScopeLog::kUi, JLogLevel::Info) << "shutting down";
    // Save before closing: once the driver is gone there is nothing to read the
    // settings back out of.
    if (JScopeDriver* d = m_session.driver()) m_settings.save(*d, m_traceView->viewState());
    m_session.close();
}

bool JScopeApp::valid() const { return m_window && m_window->valid(); }

int JScopeApp::run() {
    if (!valid()) return -1;
    JLOGC(JScopeLog::kUi, JLogLevel::Info) << "entering the run loop";
    return m_window->run();
}

// Render cost, reported once a second rather than once a frame. A scope draws
// two vertices per pixel column per channel, so the vertex count scales with the
// window width and the channel count but NOT with the record length — this is
// the number that proves the decimator is doing its job, and it is worth being
// able to see it at any time rather than only when something feels slow.
void JScopeApp::_wireFrameTiming() {
    m_window->onFrameTiming = [this](const JAppWindow::JFrameTiming& t) {
        // The window has now drawn a frame, so the bus can be looked at.
        if (m_deviceSelectionPending) {
            m_deviceSelectionPending = false;
            _selectDevice();
        }

        // Cheap enough to do every frame (five comparisons) and it is the only way
        // the ticks stay honest: a dock can be closed by its own button or dropped
        // nowhere, and neither route goes through the menu.
        JScopeMenuBuilder::syncViewMenu(*m_docks);

        ++m_frameCount;
        m_frameMsAccum   += t.totalMs;
        m_buildMsAccum   += t.buildMs;
        m_submitMsAccum  += t.submitMs;
        if (t.totalMs > m_frameMsWorst) m_frameMsWorst = t.totalMs;

        const auto now = std::chrono::steady_clock::now();
        if (now - m_lastTimingReport < std::chrono::seconds(1)) return;

        const double seconds = std::chrono::duration<double>(now - m_lastTimingReport).count();
        JLOGC(JScopeLog::kTrace, JLogLevel::Debug)
            << "render: " << static_cast<int>(m_frameCount / seconds) << " fps"
            << "  total " << (m_frameMsAccum  / m_frameCount) << "ms"
            << "  (build " << (m_buildMsAccum  / m_frameCount) << "ms"
            << " + submit " << (m_submitMsAccum / m_frameCount) << "ms)"
            << "  worst " << m_frameMsWorst << "ms"
            << "  draws " << t.drawCommands
            << "  |  trace paint " << m_traceView->paintMs() << "ms"
            << " flush " << m_traceView->flushMs() << "ms"
            << " verts " << m_traceView->vertices();

        m_lastTimingReport = now;
        m_frameCount    = 0;
        m_frameMsAccum  = 0.0;
        m_buildMsAccum  = 0.0;
        m_submitMsAccum = 0.0;
        m_frameMsWorst  = 0.0;
    };
}

// A setting changed and the driver has told us what it really applied. Every
// panel and the trace view refresh from the READ-BACK, never from the request,
// so a control cannot end up showing a value the instrument refused.
// Persisting only from the destructor is not enough for a bench tool: a window
// closed normally unwinds fine, but a terminal Ctrl-C or a SIGTERM does not, and
// losing a carefully dialled-in setup to that is exactly the kind of small
// betrayal an instrument should not commit. So the settings are written on the
// close request as well, and a termination signal is turned into one.
// Measurements recompute when the FRAME changes or when a cursor moves, and at
// no other time. Recomputing every render would redo thirteen measurements over
// a whole record for a repaint that changed nothing.
bool JScopeApp::startRecording(const std::string& path) {
    JScopeDriver* d = m_session.driver();
    if (!d) {
        m_window->showStatus("nothing to record — no device is open", kRefusalNoticeMs);
        return false;
    }
    if (m_writer.isOpen()) stopRecording();

    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), ec);

    if (!m_writer.open(path, JCaptureWriter::metaFrom(*d))) return false;
    m_window->showStatus("recording to " + path, kRefusalNoticeMs);
    return true;
}

void JScopeApp::stopRecording() {
    if (!m_writer.isOpen()) return;
    const uint64_t n = m_writer.framesWritten();
    m_writer.close();
    m_window->showStatus("recorded " + std::to_string(n) + " frames", kRefusalNoticeMs);
}

bool JScopeApp::openCapture(const std::string& path) {
    stopRecording();

    JScopeDeviceInfo info;
    info.driverId    = "replay";
    info.displayName = path;
    info.path        = path;
    if (!openDevice(info)) {
        m_window->setNotice("Cannot open capture", path);
        return false;
    }
    if (JReplayDriver* rp = m_session.replay()) {
        if (rp->wasRecovered())
            m_window->setNotice("Recovered capture",
                                "This recording did not close cleanly; its index was "
                                "rebuilt by scanning.");
    }
    return true;
}

bool JScopeApp::exportCsv(const std::string& path) {
    const JScopeFrame* f = m_session.latestFrame();
    if (!f) {
        m_window->showStatus("nothing to export — no frame on display", kRefusalNoticeMs);
        return false;
    }

    // The cursor window when the X cursors are on, the whole frame otherwise —
    // the same rule the measurements use, so an export matches the readouts.
    size_t first = 0, count = 0;
    m_traceView->cursors().sampleWindow(f->header.sampleInterval,
                                        f->header.sampleCount, first, count);

    const std::string model = m_session.driver()
                            ? m_session.driver()->capabilities().model : "unknown";
    if (!JCsvExport::write(path, *f, model, first, count)) {
        m_window->setNotice("CSV export failed", path);
        return false;
    }
    m_window->showStatus("exported " + path, kRefusalNoticeMs);
    return true;
}

void JScopeApp::_wireMeasurements() {
    auto recompute = [this] {
        const JScopeFrame* f = m_session.latestFrame();
        if (!f) return;

        // The cursor window when the X cursors are on, the whole record when they
        // are not — one code path, so "measure between cursors" cannot drift away
        // from "measure the record".
        JMeasurementWindow w;
        size_t first = 0, count = 0;
        if (m_traceView->cursors().sampleWindow(f->header.sampleInterval,
                                                f->header.sampleCount, first, count)) {
            w.first = first;
            w.count = count;
        }
        m_docks->measurements().update(*f, w);
        m_docks->cursors().update();
    };

    m_session.onFrame.connect([recompute](const JScopeFrame&) { recompute(); });
    m_traceView->onCursorsChanged.connect(recompute);
    m_docks->cursors().onCursorsChanged.connect([this, recompute] {
        // Switching cursors on for the first time puts them somewhere useful
        // rather than stacked on the left edge at zero.
        if (m_traceView->cursors().xEnabled() && m_traceView->cursors().x1() == 0.0
            && m_traceView->cursors().x2() == 0.0)
            m_traceView->resetCursorsToFrame();
        m_traceView->invalidate();
        recompute();
    });
    m_docks->measurements().onChannelChanged.connect([recompute](uint8_t) { recompute(); });

}

void JScopeApp::_wireShutdown() {
    m_window->onCloseRequest = [this] {
        if (JScopeDriver* d = m_session.driver()) m_settings.save(*d, m_traceView->viewState());
        return true;                      // never veto; this only records state
    };

    // A signal handler can do almost nothing safely, so it sets a flag and this
    // frame-paced poll turns it into an ordinary close on the main thread.
    m_shutdownPoll = std::make_unique<JFrameTimer>(kShutdownPollMs, /*singleShot=*/false);
    m_shutdownPoll->timeout.connect([this] {
        if (!s_terminate.load(std::memory_order_acquire)) return;
        JLOGC(JScopeLog::kUi, JLogLevel::Info) << "termination signal — closing cleanly";
        m_window->requestClose();
    });
    m_shutdownPoll->start();
}

void JScopeApp::_wireActions() {
    m_actions.onConfigChanged.connect([this] {
        if (JScopeDriver* d = m_session.driver()) {
            m_docks->syncFrom(*d);
            _syncViewFromDriver();
        }
    });

    // A long instrument operation no longer blocks the UI, so without this there
    // would be nothing on screen to say one is running.
    m_actions.onBusyChanged.connect([this](bool busy) {
        if (busy) m_window->showStatus("Working on the instrument...", kConnectNoticeMs);
        else      m_window->showStatus("", 1);
    });

    m_actions.onRefused.connect([this](std::string reason) {
        JLOGC(JScopeLog::kUi, JLogLevel::Warn) << "refused: " << reason;
        m_window->showStatus(reason, kRefusalNoticeMs);
    });
}

void JScopeApp::_wireSession() {
    m_session.onFrame.connect([this](const JScopeFrame& f) {
        m_traceView->setFrame(f);
        // The writer copies into its own buffer and writes on a worker thread, so
        // recording costs the display path one memcpy and no disk latency.
        if (m_writer.isOpen()) m_writer.write(f);
    });

    m_writer.onError.connect([this](std::string message) {
        JLOGC(JScopeLog::kCapture, JLogLevel::Error) << message;
        m_window->setNotice("Recording problem", message);
    });

    m_session.onError.connect([this](std::string message) {
        JLOGC(JScopeLog::kUi, JLogLevel::Error) << "session error: " << message;
        m_window->setNotice("Scope error", message);
    });

    // Dragging the trigger marker goes to the instrument through the same path
    // as the Trigger panel, so the two can never disagree about the level.
    m_traceView->onTriggerPositionDragged.connect([this](double fraction) {
        m_actions.setTriggerPosition(fraction);
    });

    m_traceView->onTriggerLevelDragged.connect([this](double volts) {
        m_actions.setTriggerLevel(volts);
    });

    m_session.onStateChanged.connect([this](JScopeState s) {
        m_statusState = jScopeStateName(s);
        JLOGC(JScopeLog::kUi, JLogLevel::Debug) << "state -> " << m_statusState;
        // Armed/Triggered is what a user watches while probing, so the trigger
        // panel follows the state as well as the status bar.
        if (JScopeDriver* d = m_session.driver()) {
            m_docks->trigger().syncFrom(*d);
            // And so does the legend on the trace. Without this, stopping left
            // "Auto" written across the graticule for as long as the window
            // stayed open: the state changed, the status bar followed it, and the
            // one readout sitting on top of the waveform did not.
            _syncViewFromDriver();
        }
        m_window->requestRedraw();
    });
}

void JScopeApp::beginDeviceSelection() {
    // ARMED HERE, RUN AFTER THE FIRST PAINTED FRAME.
    //
    // Everything this does — enumerating the bus, then opening whatever answers
    // — happens before the window has drawn anything if it is called inline, and
    // the user is left looking at an empty rectangle for however long the bus
    // takes. That was twelve seconds on this machine, because a single wedged
    // instrument made every descriptor read sit out libusb's one-second timeout,
    // four scans deep.
    //
    // Enumeration is cheap now (it asks the kernel rather than the devices), but
    // cheap is not the same as bounded: the next hostile device would put the
    // delay straight back in front of the window. Running after the first frame
    // means it cannot, whatever the bus does.
    m_deviceSelectionPending = true;
}

void JScopeApp::_selectDevice() {
    const auto devices = availableDevices();

    // An explicit --device wins outright.
    if (!s_preferredDriver.empty()) {
        for (const JScopeDeviceInfo& d : devices)
            if (d.driverId == s_preferredDriver) { _connectTo(d); return; }

        m_window->setNotice("No device for --device " + s_preferredDriver,
                            "Nothing on the bus matched. Pick one from the Device menu.");
        return;
    }

    // ONE instrument is opened at startup: the one that opened last time. There
    // is deliberately no walk down the bus behind it.
    //
    // Trying every device in turn meant that a scope which is present but not
    // the one being used — a wedged DSO2D15, say — was dialled first every run
    // and its full open timeout paid before reaching the instrument on the
    // bench. It also connected to things the user had not asked for. Which
    // instrument to use is the user's choice, and once made it is remembered;
    // until then, the right behaviour is to open nothing and let them pick.
    const JScopeDeviceInfo last = m_settings.lastDevice();
    if (last.driverId.empty()) {
        JLOGC(JScopeLog::kUi, JLogLevel::Info)
            << "no remembered instrument; waiting for the user to choose one";
        m_window->setNotice("Choose an instrument",
                            "Device > " + std::to_string(devices.size()) +
                            " available. The one you pick is remembered and opened"
                            " automatically next time.");
        return;
    }

    // Match the PHYSICAL unit where possible — same serial, or same port — so a
    // scope moved to another socket is still recognised, and a different unit
    // that happens to sit at the old address is not mistaken for it.
    auto isLast = [&last](const JScopeDeviceInfo& d) {
        if (d.driverId != last.driverId) return false;
        if (!last.serialNumber.empty() && !d.serialNumber.empty())
            return d.serialNumber == last.serialNumber;
        if (!last.portPath.empty() && !d.portPath.empty())
            return d.portPath == last.portPath;
        return true;
    };

    for (const JScopeDeviceInfo& d : devices)
        if (isLast(d)) {
            JLOGC(JScopeLog::kUi, JLogLevel::Info)
                << "opening '" << d.displayName << "': the instrument used last time";
            _connectTo(d);
            return;
        }

    JLOGC(JScopeLog::kUi, JLogLevel::Info)
        << "the instrument used last time (" << last.driverId << ") is not on the bus";
    m_window->setNotice("Last instrument not found",
                        "The scope used last time is not connected. Pick one from the"
                        " Device menu, or plug it back in and use Device > Scan.");
}

// Open ONE device. A failure is reported and stops there — nothing else is tried
// on the user's behalf.
void JScopeApp::_connectTo(const JScopeDeviceInfo& device) {
    m_window->showStatus("Connecting to " + device.displayName + "...", kConnectNoticeMs);
    m_session.openAsync(device, [this, device](bool ok) {
        if (!ok) {
            m_window->setNotice("Cannot open " + device.displayName,
                                "Pick another from the Device menu, or use Device > Scan"
                                " for Devices.");
            return;
        }
        _adoptOpenSession(device);
    });
}

bool JScopeApp::openBestAvailableDevice() {
    const auto devices = JScopeDriverRegistry::instance().enumerateAll();
    if (devices.empty()) {
        JLOGC(JScopeLog::kUi, JLogLevel::Warn) << "no devices enumerated at all";
        return false;
    }

    // An explicit choice beats every heuristic below it.
    if (!s_preferredDriver.empty()) {
        for (const JScopeDeviceInfo& d : devices) {
            if (d.driverId != s_preferredDriver) continue;
            JLOGC(JScopeLog::kUi, JLogLevel::Info)
                << "opening '" << d.displayName << "' as asked (--device "
                << s_preferredDriver << ")";
            if (!m_session.open(d)) {
                JLOGC(JScopeLog::kUi, JLogLevel::Error) << "the requested device did not open";
                return false;
            }
            _adoptOpenSession(d);
            return true;
        }
        JLOGC(JScopeLog::kUi, JLogLevel::Warn)
            << "no device with driver '" << s_preferredDriver << "'; choosing instead";
    }

    // Prefer an instrument that can actually ACQUIRE.
    //
    // Not merely real hardware: the DSO2D15 is real and its waveform read is
    // unresolved, so opening it in preference to a working 1008C would put up a
    // blank display and look like a fault. Whether a device can acquire is only
    // knowable once it is open, so candidates are tried in turn and the first
    // that reports an acquisition mode wins.
    // Real instruments first, generated sources last: a simulated source always
    // opens, so reaching one early would end the search before any hardware was
    // tried. Which is which is stated by the driver that enumerated it — the
    // application does not know driver names.
    std::vector<JScopeDeviceInfo> candidates;
    for (const auto& d : devices) if (!d.simulated) candidates.push_back(d);
    for (const auto& d : devices) if (d.simulated)  candidates.push_back(d);

    const JScopeDeviceInfo* fallback = nullptr;
    for (const JScopeDeviceInfo& d : candidates) {
        if (!m_session.open(d)) continue;
        const bool canAcquire = m_session.driver()->capabilities().acquisitionModes != 0;
        if (canAcquire) {
            // KEEP the session that just opened. Closing it and calling
            // openDevice() re-ran the whole bring-up — for the 1008C that is
            // three vertical ranges of 500 averaged samples with protocol delays
            // between transfers, so probing and then opening cost twice a
            // twenty-second init for no reason.
            JLOGC(JScopeLog::kUi, JLogLevel::Info)
                << "auto-selected '" << d.displayName << "' out of "
                << devices.size() << " enumerated device(s)";
            _adoptOpenSession(d);
            return true;
        }
        m_session.close();
        JLOGC(JScopeLog::kUi, JLogLevel::Info)
            << "'" << d.displayName << "' cannot acquire — looking for one that can";
        if (!fallback) fallback = &d;
    }

    // Nothing can acquire. Open whatever did respond, so its settings and any
    // generator are still reachable.
    if (fallback) {
        JLOGC(JScopeLog::kUi, JLogLevel::Warn)
            << "no device can acquire; opening '" << fallback->displayName
            << "' for its settings and generator";
        return openDevice(*fallback);
    }
    return false;
}

std::vector<JScopeDeviceInfo> JScopeApp::availableDevices() const {
    return JScopeDriverRegistry::instance().enumerateAll();
}


bool JScopeApp::openDevice(const JScopeDeviceInfo& device) {
    // ASYNCHRONOUS, always. Bringing up the 1008C takes about twenty seconds,
    // and doing it inline froze the window for the whole of it — picking the
    // instrument out of the Device menu looked exactly like a crash. If the
    // device is not actually on the bus it is worse, because the USB timeouts
    // are added to it.
    //
    // Returns true meaning "a connect has started", not "the device is open".
    // The only caller that needed the stronger answer is the auto-select probe,
    // which uses the synchronous form before the window is interactive.
    // Clear first: the notice must describe THIS attempt, not the last one. A
    // stale "Cannot connect" sitting above a connection that is in progress —
    // or that then succeeds — is worse than no message at all.
    m_window->setNotice("");
    m_window->showStatus("Connecting to " + device.displayName + "...", kConnectNoticeMs);
    m_session.openAsync(device, [this, device](bool ok) {
        // Whatever happens, the connect is over: the message must not outlive it.
        // A 30-second timeout meant "Connecting to..." sat under a connected
        // instrument, contradicting the status bar beside it.
        m_window->showStatus("", 1);
        if (!ok) {
            m_window->setNotice("Cannot connect", device.displayName + " did not answer");
            return;
        }
        _adoptOpenSession(device);
    });
    return true;
}

// Everything openDevice does AFTER the session is open. Split out so the
// auto-select probe can keep the session it already opened instead of closing
// and reopening the device.
void JScopeApp::_adoptOpenSession(const JScopeDeviceInfo& device) {
    // A device opened successfully, so whatever the last failure said is now
    // stale. The notice strip is permanent by design — empty text is the only
    // thing that removes it — so nothing else was ever going to clear it, and a
    // single failed connect left "Cannot connect" on screen for the rest of the
    // session however many devices were opened afterwards.
    m_window->setNotice("");

    // Throw away the previous instrument's last frame. Nothing else did, so the
    // display kept showing it until the new device published one — and a device
    // that cannot acquire never publishes at all. Opening the DSO2D15 after the
    // synthetic source left the synthetic sawtooth on screen, on channels the
    // DSO2D15 does not even have, looking like the DSO2D15's own signal.
    m_traceView->clearFrame();

    m_currentDevice = device;
    m_statusDevice = device.displayName;

    // Remember it, so the next run goes straight here instead of working down
    // the bus in enumeration order. That order is arbitrary, and when the device
    // it happens to reach first is a wedged one, every startup pays its full
    // open timeout before arriving at the instrument actually in use.
    m_settings.rememberDevice(device);

    JScopeDriver* d = m_session.driver();
    // Panels are built from THIS device's capabilities, so opening an 8-channel
    // 1008C after a 2-channel DSO2D15 changes the controls rather than leaving
    // six dead ones behind.
    m_docks->rebuild(d->capabilities());

    // The Instrument menu is built before any device is open, so it has nothing
    // to list until one answers. Rebuild it now that this one has.
    JScopeMenuBuilder::refreshInstrumentMenu(m_app.sceneGraph(), *this);

    // The transport exists only for a capture; on a live instrument it would be
    // a control with nothing to control.
    JReplayDriver* rp = m_session.replay();
    m_docks->setReplayVisible(rp != nullptr);
    m_docks->replayBar().attach(rp);
    if (rp)
        rp->onPositionChanged.connect([this](uint64_t frame) {
            m_docks->replayBar().setPosition(frame);
        });

    // Restore before the first sync, so the panels come up showing the setup the
    // user left rather than the device's defaults and then a visible jump.
    //
    // NOT for an instrument that owns its own configuration. A bench scope's
    // knobs were set by whoever is standing at it, and pushing a stored setup
    // over them is destructive: it left a DSO2D15 at 50mV/div under a 10x probe,
    // which is a screen full of noise, every time jscope connected. For those,
    // the sync below adopts what the instrument reports instead.
    if (!d->capabilities().deviceOwnsConfiguration) {
        m_settings.restore(*d, m_actions);
    } else {
        JLOGC(JScopeLog::kUi, JLogLevel::Info)
            << device.displayName << " reports its own setup; adopting it rather than"
               " applying the stored one";
    }

    m_docks->syncFrom(*d);
    _syncViewFromDriver();

    // The zoom belongs to the DEVICE being opened, not to whatever was on screen
    // before it, so a fresh device starts at full span.
    m_traceView->resetViewWindow();

    // AFTER the sync, not with the rest of the restore: _syncViewFromDriver
    // rewrites every channel view, so display state put back before it would be
    // discarded on the way past.
    m_traceView->applyViewState(m_settings.restoreView(*d));

    // Only start an instrument that can actually acquire. The DSO2D15 connects,
    // reports its settings and drives its generator, but its waveform read is
    // not implemented — asking it to run makes the driver refuse and raises an
    // error for something that is a known, documented limit rather than a
    // fault. Say what it can do instead.
    if (d->capabilities().acquisitionModes != 0) {
        m_actions.run();
    } else {
        JLOGC(JScopeLog::kUi, JLogLevel::Info)
            << device.displayName << " cannot acquire; opened for settings and generator";
        // On the CHROME, not in the status line. A transient message scrolls away
        // and leaves an empty graticule that looks like a broken scope — which is
        // exactly how this read on the bench. The strip stays until another
        // device is opened, and says why the screen is empty.
        m_window->setNotice(device.displayName + ": no waveform display",
                            "This instrument's waveform read is not implemented. Its"
                            " settings and generator work; the trace does not.");
    }
}

void JScopeApp::_syncViewFromDriver() {
    JScopeDriver* d = m_session.driver();
    if (!d) return;

    const JScopeCapabilities& caps = d->capabilities();
    m_traceView->setGraticule(caps.horizontalDivisions, caps.verticalDivisions);

    // The view never invents a V/div: every value comes from what the driver
    // says it actually applied.
    for (uint8_t i = 0; i < caps.channelCount(); ++i) {
        const JScopeChannelConfig& c = d->channelConfig(i);
        m_traceView->setChannelView(i, c.enabled, c.voltsPerDiv, c.offsetVolts, c.inverted,
                                    c.coupling);
    }

    // And switch OFF everything this device does not have. The view keeps a
    // fixed array of channels and only the ones a device reports were being
    // rewritten, so opening the 2-channel DSO2D15 after the 8-channel 1008C left
    // six channels still marked enabled — which drew their ground badges, and
    // their traces for as long as a frame still carried those planes. On the
    // bench that showed as four traces on an instrument that has two.
    for (uint8_t i = caps.channelCount(); i < JScopeLimits::kMaxChannels; ++i)
        m_traceView->setChannelView(i, false, 1.0, 0.0, false, JScopeCoupling::DC);

    // The trigger badge shows the instrument's own level, against the scale of
    // the channel it triggers on — and in that channel's colour, which is what
    // identifies the source.
    const JScopeTriggerConfig& tr = d->triggerConfig();
    m_traceView->setTriggerLevel(tr.levelVolts, tr.sourceChannel);

    // The on-screen legend, from the driver rather than anything the UI
    // remembers: the sweep the instrument is running, the timebase it settled
    // on, and the trigger it is armed with.
    // THE INSTRUMENT'S OWN WORDS WHERE IT HAS THEM. A bench scope knows whether
    // it is triggered, armed or stopped; that is its answer, not something to
    // infer from the sweep mode we asked it for. A device with no front panel
    // reports nothing and the sweep mode is then all there is to show.
    // The run state leads and the sweep mode only describes what happens while
    // sweeping — see jScopeSweepLabel, where the rule lives and is tested.
    std::string state = jScopeSweepLabel(d->state(), d->triggerStatusText(), tr.mode);
    const std::string acquire = d->acquisitionText();
    if (!acquire.empty() && acquire != "NORMal") state += "  " + acquire;

    m_traceView->setLegend(state, d->timebaseConfig().secondsPerDiv,
                           tr.sourceChannel, tr.levelVolts);

    // s/div is a WINDOW, not a label. The device picks whatever capture serves it
    // best; the view takes the requested window out of that capture, so the
    // graticule reads correctly whether the record is longer than the screen
    // (zoomed in) or shorter (drawn short of full width).
    if (d->timebaseConfig().mode == JScopeAcquisitionMode::Windowed)
        m_traceView->setTimeWindow(d->timebaseConfig().secondsPerDiv);

    JLOGC(JScopeLog::kUi, JLogLevel::Debug)
        << "view synced from driver: " << int(caps.channelCount()) << " channels, graticule "
        << int(caps.horizontalDivisions) << "x" << int(caps.verticalDivisions);
}

} // inline namespace jf
