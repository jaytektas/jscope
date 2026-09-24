// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JScopeActions.h"
#include "JScopeDockLayout.h"
#include "JScopeSettings.h"
#include "capture/JCaptureWriter.h"
#include "scope/JScopeSession.h"
#include "ui/JCentreDockHost.h"
#include "ui/JPulseGridEditor.h"
#include "ui/JTraceView.h"

#include <j/app/JAppUpdater.h>
#include <j/app/JAppWindow.h>
#include <j/platform/JUdevRule.h>
#include <j/core/GenesisComponents.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

// The application: window, session, and the wiring between them.
//
// Deliberately several small classes rather than one with several sections —
// studio-jf's main.cpp reached 304 KB and that is the mistake this structure
// exists to avoid. JScopeApp owns and connects; JScopeMenuBuilder,
// JScopeToolBarBuilder and JScopeShortcuts each build one thing and hand back
// what they built.

inline namespace jf {

class JScopeApp {
public:
    // settingsPath defaults to the user's config; a test or a second instance can
    // point it elsewhere so it never disturbs the real one.
    // Open this driver id at startup instead of choosing one. Set before the
    // application is constructed. Exists so an instrument that auto-select would
    // never pick — one that cannot acquire, such as the DSO2D15 — can still be
    // put on screen and looked at.
    static void preferDriver(std::string driverId);

    explicit JScopeApp(std::string settingsPath = defaultSettingsPath());
    ~JScopeApp();

    static std::string defaultSettingsPath();

    // Names the settings file, so each application keeps its own. Set before the
    // app is constructed; see defaultSettingsPath for why they must not share.
    static void setApplicationName(std::string name);

    // The udev rule this application's instrument needs on Linux, and the USB ID it covers, so a copy
    // with no install step (an AppImage) can put it in place itself when that device is refused. The
    // shell knows no instrument; the application says which rule is its. Set before the app is built.
    static void setUsbRule(JUdevRule rule, uint16_t vendorId, uint16_t productId);

    // Async-signal-safe: sets a flag the app polls on the main thread. Call this
    // from a signal handler; never touch the window from one.
    static void requestTerminate();

    static std::string s_preferredDriver;
    static std::string s_applicationName;
    static std::optional<JUdevRule> s_usbRule;
    static uint16_t s_usbRuleVendorId, s_usbRuleProductId;

    JScopeApp(const JScopeApp&)            = delete;
    JScopeApp& operator=(const JScopeApp&) = delete;

    bool valid() const;
    int  run();

    // Open the first device the registry can see, preferring real hardware and
    // falling back to the synthetic source — so the window is never blank and
    // the app is useful with nothing plugged in.
    bool openBestAvailableDevice();

    // Choose and open a device WITHOUT blocking, so the window is on screen
    // first. Returns immediately; the work happens on the session's device
    // worker and the result arrives on the main thread.
    //
    // Startup used to probe the bus before the run loop began, so if the
    // instrument was busy — another jscope, or the OEM harness holding it — the
    // application spent the USB timeouts with no window at all and looked like
    // it had failed to launch.
    // Arms device selection to run once the window has PAINTED, rather than
    // doing it here. See the definition for why.
    void beginDeviceSelection();
    bool openDevice(const JScopeDeviceInfo& device);

    // Everything openDevice does once the session is open: panels, settings,
    // view sync and run. Separate so the auto-select probe can adopt the session
    // it already has rather than reopening the device.
    void _adoptOpenSession(const JScopeDeviceInfo& device);
    void _connectTo(const JScopeDeviceInfo& device);

    // Devices visible right now. The menu is rebuilt from this each time it is
    // opened, so a scope plugged in after startup appears without a restart.
    std::vector<JScopeDeviceInfo> availableDevices() const;

    const JScopeDeviceInfo& currentDevice() const { return m_currentDevice; }

    JAppWindow&      window()    { return *m_window; }
    JScopeSession&   session()   { return m_session; }
    JTraceView&      traceView() { return *m_traceView; }
    JCentreDockHost& centre()    { return *m_centre; }
    JScopeActions&    actions() { return m_actions; }
    JScopeDockLayout& docks()   { return *m_docks; }
    JAppUpdater&      updater() { return *m_updater; }

    // ---- capture ----
    bool startRecording(const std::string& path);
    void stopRecording();
    bool isRecording() const { return m_writer.isOpen(); }

    // Open a .jscope capture as the current device. Closes whatever is open.
    bool openCapture(const std::string& path);

    // Export the frame on display, or the cursor window of it, as CSV.
    bool exportCsv(const std::string& path);

    // Where captures and exports go when the user does not choose. Kept beside
    // the settings so a bench machine has one obvious place to look.
    static std::string defaultCaptureDir();

private:
    void _wireSession();
    void _wireActions();
    void _wireShutdown();
    void _wireMeasurements();
    void _wireFrameTiming();
    void _selectDevice();
    // Offer to install the USB rule for `device`, then run `then` whatever the answer. False, and nothing
    // asked, when the rule does not cover this device, is installed, or was already offered this session.
    bool _offerUsbRule(const JScopeDeviceInfo& device, std::function<void()> then);
    bool m_usbRuleOffered{false};

    // Set by beginDeviceSelection, cleared by the first frame that runs it.
    bool m_deviceSelectionPending{false};
    void _syncViewFromDriver();
    void _updateStatus();

    JGuiApplication                   m_app;
    std::unique_ptr<JAppWindow>       m_window;
    std::unique_ptr<JTraceView>       m_traceView;

    // The centre is a dock host rather than a bare trace: the live trace and the
    // generator's own output are two views of the same instrument and belong side
    // by side, tabbed or split, rather than one of them being exiled to a panel.
    std::unique_ptr<JCentreDockHost>  m_centre;
    std::unique_ptr<JDockWidget>      m_scopeDock;
    std::unique_ptr<JPulseGridEditor> m_generatorTrace;
    std::unique_ptr<JDockWidget>      m_generatorTraceDock;
    void _refreshGeneratorTrace();
    JScopeSession                     m_session;
    JScopeActions                     m_actions{m_session};
    std::unique_ptr<JScopeDockLayout> m_docks;
    JScopeSettings                    m_settings;
    JCaptureWriter                    m_writer;
    std::unique_ptr<JFrameTimer>      m_shutdownPoll;
    // jscope's own updates, from its GitHub releases: checked at startup, on Help > Check for Updates,
    // and installed as the window closes.
    std::unique_ptr<JAppUpdater>      m_updater;
    static std::atomic<bool>          s_terminate;

    JScopeDeviceInfo m_currentDevice;
    std::string m_statusDevice{"no device"};
    std::string m_statusState{"closed"};

    // Render-cost accounting, reported once a second.
    std::chrono::steady_clock::time_point m_lastTimingReport{std::chrono::steady_clock::now()};
    uint64_t m_frameCount{0};
    double   m_frameMsAccum{0.0};
    double   m_buildMsAccum{0.0};
    double   m_submitMsAccum{0.0};
    double   m_frameMsWorst{0.0};
};

} // inline namespace jf
