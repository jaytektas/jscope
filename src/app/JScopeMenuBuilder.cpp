// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JScopeMenuBuilder.h"

#include <j/core/Dialog.h>
#include "JScopeApp.h"

#include "JScopeActions.h"
#include "scope/JScopeLog.h"
#include "ui/JTraceView.h"

#include <j/core/MainThreadDispatcher.h>

#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

namespace {

// JAppWindow's menu bar stores raw JMenu pointers, so the menus must outlive the
// builder call. They live here for the process lifetime, which is exactly as long
// as the window that references them.
std::vector<std::unique_ptr<JMenu>>& menuStore() {
    static std::vector<std::unique_ptr<JMenu>> store;
    return store;
}

// The View menu's dock entries, kept so their ticks can be corrected. A dock can
// also be closed by its own X or dragged out and dropped nowhere, and a tick that
// disagrees with the window is worse than no tick at all.
struct JDockMenuEntry { JMenuItem* item; JScopeDockToggle toggle; };
std::vector<JDockMenuEntry>& dockItems() {
    static std::vector<JDockMenuEntry> items;
    return items;
}

JMenu* newMenu(const std::string& title) {
    menuStore().push_back(std::make_unique<JMenu>(title));
    return menuStore().back().get();
}

std::string timestampedName(const char* prefix, const char* extension) {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    // Same call, opposite argument order, different name on each platform.
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", &tm);
    return std::string(prefix) + "-" + buf + "." + extension;
}

// The most recently modified capture in the default directory. A file dialog is
// the eventual answer, but "reopen what I just recorded" is the case that
// actually comes up on a bench, and it needs no dialog at all.
std::string newestCapture() {
    namespace fs = std::filesystem;
    const fs::path dir = JScopeApp::defaultCaptureDir();
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return {};

    fs::path best;
    fs::file_time_type bestTime{};
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".jscope") continue;
        const auto when = fs::last_write_time(e.path(), ec);
        if (best.empty() || when > bestTime) { best = e.path(); bestTime = when; }
    }
    return best.string();
}

// The Device menu, kept so its device list can be rebuilt in place.
JMenu*& deviceMenu() {
    static JMenu* m = nullptr;
    return m;
}

// Rebuild the WHOLE Device menu — fixed entries included. Enumerating on demand
// rather than at startup is what lets a scope plugged in later be found.
//
// The whole menu, because JMenu offers clear() and nothing finer: there is no
// way to drop the instrument entries and keep Scan above them.
//
// No Reconnect item. Picking the instrument from the list below does the same
// thing and says which one, where Reconnect only ever meant "the last one" --
// a second way to do the same thing, with less information in it.
// Appending only the instruments, which is what this did before, meant every
// scan added another copy of every device already listed.

// THE INSTRUMENT'S OWN SETTINGS, whatever they happen to be.
//
// Built from what the driver publishes, so the shell never learns one scope's
// vocabulary. A device with no settings of its own publishes none and the menu
// is hidden entirely rather than shown empty.
//
// Rebuilt on open and after every change, because the instrument is allowed to
// substitute a value it prefers and the tick has to follow what it did, not what
// was asked.
JMenu*& instrumentMenu() {
    static JMenu* menu = nullptr;
    return menu;
}

// The submenus this menu owns. Separate from menuStore(), which lives for the
// life of the process: these are rebuilt every time a setting changes, and
// pushing each rebuild's worth into a store that is never emptied would grow
// without bound.
std::vector<std::unique_ptr<JMenu>>& instrumentSubmenus() {
    static std::vector<std::unique_ptr<JMenu>> store;
    return store;
}

void rebuildInstrumentMenu(JSceneGraph& graph, JScopeApp& app) {
    JMenu* menu = instrumentMenu();
    if (!menu) return;

    // Items first, THEN the submenus they point at. The other order would leave
    // each item holding a pointer to a menu that had just been freed.
    menu->clear();
    instrumentSubmenus().clear();

    JScopeDriver* d = app.session().driver();
    const std::vector<JScopeOption> options = d ? d->instrumentOptions()
                                                : std::vector<JScopeOption>{};
    if (options.empty()) {
        // Honest rather than empty: an instrument with no settings of its own is
        // not a menu that failed to load.
        menu->add(graph, "No settings on this instrument");
        return;
    }

    for (const JScopeOption& o : options) {
        instrumentSubmenus().push_back(std::make_unique<JMenu>(o.label));
        JMenu* sub = instrumentSubmenus().back().get();

        for (const std::string& v : o.values) {
            // The instrument's own spelling, with the current one marked. No
            // translation in either direction: what it answered is what it takes.
            const std::string label = (v == o.current ? "* " : "   ") + v;
            const std::string id = o.id;
            sub->add(graph, label)->onTriggered.connect([&app, &graph, id, v] {
                if (JScopeDriver* drv = app.session().driver())
                    drv->setInstrumentOption(id, v);
                // Deferred for the reason the device menu defers: this handler is
                // inside an item the rebuild is about to destroy.
                JMainThreadDispatcher::instance().post([&app, &graph] {
                    rebuildInstrumentMenu(graph, app);
                });
            });
        }
        menu->add(graph, o.label, {}, sub);
    }
}

void rebuildDeviceMenu(JSceneGraph& graph, JScopeApp& app) {
    JMenu* menu = deviceMenu();
    if (!menu) return;

    menu->clear();

    // DEFERRED. Rebuilding destroys every item in this menu, including the Scan
    // item whose handler is running — clearing here would free the object the
    // call is still inside. Posting it runs the rebuild on the next turn of the
    // main loop, once the handler has returned.
    menu->add(graph, "Scan for Devices")->onTriggered.connect([&app, &graph] {
        JMainThreadDispatcher::instance().post([&app, &graph] {
            rebuildDeviceMenu(graph, app);
        });
    });
    menu->addSeparator(graph);

    const auto devices = app.availableDevices();

    for (const JScopeDeviceInfo& d : devices) {
        // A tick against whichever is open, so the menu answers "what am I
        // looking at" as well as "what else is there".
        const bool current = (d.driverId == app.currentDevice().driverId &&
                              d.portPath == app.currentDevice().portPath);
        const std::string label = (current ? "* " : "   ") + d.displayName;
        menu->add(graph, label)->onTriggered.connect([&app, d] {
            JLOGC(JScopeLog::kUi, JLogLevel::Info) << "opening " << d.displayName;
            app.openDevice(d);
        });
    }

    JLOGC(JScopeLog::kUi, JLogLevel::Info)
        << "device menu: " << devices.size() << " device(s) available, "
        << menu->items().size() << " menu entries";
}

} // namespace

void JScopeMenuBuilder::build(JAppWindow& window, JSceneGraph& graph, JScopeApp& app) {
    JMenu* file = newMenu("File");

    // Timestamped default names: a bench session produces many captures and
    // being asked to name each one is friction at exactly the wrong moment.
    file->add(graph, "Start Recording")->onTriggered.connect([&app] {
        app.startRecording(JScopeApp::defaultCaptureDir() + "/" +
                           timestampedName("capture", "jscope"));
    });
    file->add(graph, "Stop Recording")->onTriggered.connect([&app] {
        app.stopRecording();
    });
    file->addSeparator(graph);
    file->add(graph, "Open Last Capture")->onTriggered.connect([&app] {
        const std::string latest = newestCapture();
        if (latest.empty())
            JLOGC(JScopeLog::kUi, JLogLevel::Warn)
                << "no captures found in " << JScopeApp::defaultCaptureDir();
        else
            app.openCapture(latest);
    });
    file->add(graph, "Export CSV")->onTriggered.connect([&app] {
        app.exportCsv(JScopeApp::defaultCaptureDir() + "/" +
                      timestampedName("export", "csv"));
    });
    file->addSeparator(graph);
    file->add(graph, "Quit")->onTriggered.connect([&window] {
        JLOGC(JScopeLog::kUi, JLogLevel::Info) << "quit from the File menu";
        window.requestClose();
    });

    // Every item goes through JScopeActions — the capability checks, the
    // read-back after an apply, and the refusal message all live there, and a
    // second copy here would be a second chance to forget one.
    JScopeActions& a = app.actions();

    JMenu* acquire = newMenu("Acquire");
    acquire->add(graph, "Run")->onTriggered.connect([&a] { a.run(); });
    acquire->add(graph, "Stop")->onTriggered.connect([&a] { a.stop(); });
    acquire->add(graph, "Single")->onTriggered.connect([&a] { a.single(); });
    acquire->addSeparator(graph);
    acquire->add(graph, "Force Trigger")->onTriggered.connect([&a] { a.forceTrigger(); });
    acquire->add(graph, "Autoset")->onTriggered.connect([&a] { a.autoset(); });

    // Device: which instrument, and how to get back to it. Rebuilt from a live
    // enumeration each time the menu is opened, so a scope plugged in after
    // startup appears without a restart — and an instrument that dropped off the
    // bus can be picked up again without one either.
    JMenu* device = newMenu("Device");
    deviceMenu() = device;
    rebuildDeviceMenu(graph, app);   // fills in Scan and the devices

    JMenu* instrument = newMenu("Instrument");
    instrumentMenu() = instrument;
    rebuildInstrumentMenu(graph, app);

    // NO TEAR-OFF MENUS. The framework lets a menu be dragged out into a window of
    // its own, and this application has no use for one: every menu here is a short
    // list acted on and dismissed, and a floating copy of it is a window to lose
    // track of rather than a tool. One global switch rather than a flag per menu,
    // so a menu added later cannot quietly arrive tearable.
    JMenuManager::instance().setTearOffEnabled(false);

    // The generator's own file and device actions, in the OEM's order and with its
    // names. Download is separate from editing on purpose -- see
    // JScopeActions::downloadGeneratorPattern.
    JMenu* generator = newMenu("Generator");
    generator->add(graph, "Load Pattern...")->onTriggered.connect([&app] {
        JDialogRequest req;
        req.kind       = JDialogRequest::JKind::OpenFile;
        req.title      = "Load generator pattern";
        req.extensions = { "squ" };
        req.onInput    = [&app](std::string path) {
            if (!path.empty()) app.actions().loadGeneratorPattern(path);
        };
        JDialogManager::instance().push(std::move(req));
    });
    generator->add(graph, "Save Pattern...")->onTriggered.connect([&app] {
        JDialogRequest req;
        req.kind       = JDialogRequest::JKind::SaveFile;
        req.title      = "Save generator pattern";
        req.extensions = { "squ" };
        req.onInput    = [&app](std::string path) {
            if (!path.empty()) app.actions().saveGeneratorPattern(path);
        };
        JDialogManager::instance().push(std::move(req));
    });
    generator->add(graph, "Download to Device")->onTriggered.connect([&app] {
        app.actions().downloadGeneratorPattern();
    });

    // Help, and an About that carries the GPL's Appropriate Legal Notices.
    //
    // Not decoration: section 5(d) asks an interactive program to show the
    // copyright, the absence of warranty, that it may be redistributed under the
    // licence, and where to read it. This is where a user would look for that
    // anyway, so it goes here rather than being printed at startup where nobody
    // running a GUI would see it.
    JMenu* help = newMenu("Help");
    help->add(graph, "About jscope")->onTriggered.connect([] {
        JDialogRequest req;
        req.kind  = JDialogRequest::JKind::Message;
        req.title = "About jscope";
        req.body  =
            "jscope " JSCOPE_VERSION "\n"
            "An oscilloscope front end for the Hantek 1008C.\n"
            "\n"
            "Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>\n"
            "\n"
            "This program comes with ABSOLUTELY NO WARRANTY.\n"
            "It is free software, and you are welcome to redistribute it under\n"
            "the terms of the GNU General Public License, version 3 or later.\n"
            "See the LICENSE file, or <https://www.gnu.org/licenses/gpl-3.0.html>.\n"
            "\n"
            "Built on JFramework. Uses libusb (LGPL-2.1-or-later);\n"
            "its source is in third_party/libusb-win.";
        JDialogManager::instance().push(std::move(req));
    });

    JMenu* view = newMenu("View");
    view->add(graph, "Reset Zoom")->onTriggered.connect([&app] {
        JLOGC(JScopeLog::kUi, JLogLevel::Info) << "reset zoom";
        app.traceView().resetViewWindow();
    });

    // One checkable entry per dock. JMenuItem::activate() flips its own checked
    // state BEFORE emitting, so the handler reads the state the user just asked
    // for rather than having to invert it here.
    for (const JScopeDockToggle& t : app.docks().dockToggles()) {
        JMenuItem* item = view->add(graph, t.title);
        item->setCheckable(true);
        item->setChecked(JScopeDockLayout::isDockVisible(t));
        item->onTriggered.connect([&app, t, item] {
            app.docks().setDockVisible(t, item->isChecked());
        });
        dockItems().push_back({ item, t });
    }

    // Listed once and added in a loop, so the count below is the list rather than a
    // number somebody has to remember to update. It said "3 menus" while adding
    // five, having been written when there were three -- the same drift that left
    // the dock layout line describing a layout the app no longer builds.
    const std::vector<JMenu*> bar = { file, acquire, device, instrument, generator, view, help };
    for (JMenu* m : bar) window.menuBar().addMenu(m);

    // COUNTED, not stated. This line claimed three menus while five were being
    // added, having been written when there were three and never revisited -- the
    // same way the dock layout line went on describing a layout that had changed.
    // A number the code derives cannot drift from what the code does.
    JLOGC(JScopeLog::kUi, JLogLevel::Debug)
        << "menu bar built: " << bar.size() << " menus, "
        << dockItems().size() << " dock toggles under View";
}


void JScopeMenuBuilder::syncViewMenu(const JScopeDockLayout& docks) {
    for (const JDockMenuEntry& e : dockItems()) {
        // Greyed rather than hidden when the open instrument has no such dock: an
        // entry that vanishes makes the menu's shape change under the cursor,
        // whereas a disabled one says the feature exists and this device lacks it.
        const bool available = docks.isDockAvailable(e.toggle);
        e.item->setEnabled(available);
        e.item->setChecked(available && JScopeDockLayout::isDockVisible(e.toggle));
    }
}

void JScopeMenuBuilder::refreshInstrumentMenu(JSceneGraph& graph, JScopeApp& app) {
    rebuildInstrumentMenu(graph, app);
}

} // inline namespace jf
