// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JScopeAiBus.h"

#include "JScopeApp.h"

#include "scope/JReferenceSignal.h"
#include "scope/JScopeLimits.h"
#include "scope/JScopeLog.h"

#include <j/core/JAiBus.h>

#include <cctype>
#include <cstdlib>
#include <sstream>

inline namespace jf {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool number(const std::string& s, double& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return end && *end == '\0';
}

bool onOff(const std::string& s, bool& v) {
    const std::string l = lower(s);
    if (l == "on" || l == "1" || l == "true")   { v = true;  return true; }
    if (l == "off" || l == "0" || l == "false") { v = false; return true; }
    return false;
}

JJson deviceJson(const JScopeDeviceInfo& d, size_t index) {
    JJson e = JJson::object();
    e["index"]     = JJson(static_cast<double>(index));
    e["driver"]    = JJson(d.driverId);
    e["name"]      = JJson(d.displayName);
    e["serial"]    = JJson(d.serialNumber);
    e["port"]      = JJson(d.portPath);
    e["simulated"] = JJson(d.simulated);
    if (!d.path.empty()) e["path"] = JJson(d.path);
    return e;
}

} // namespace

JScopeAiBus::JScopeAiBus(JScopeApp& app) : m_app(app) {
    m_commands = std::make_unique<JScopeBusCommands>(m_app.session(), m_app.actions(), _hooks());
}

void JScopeAiBus::install() {
    if (!JAiBus::instance().enabled()) return;
    // Generic widget actions (focus, click, set_value) are the bus's own; this
    // answers only the actions addressed at the application (id 0, `call`).
    JAiBus::instance().onAction = [this](uint32_t id, const std::string& action) -> int {
        if (id != 0) return 0;
        std::string reply;
        if (!m_commands->handle(action, reply)) {
            JAiBus::instance().reply("{\"ok\":false,\"error\":\"unknown verb (try help)\"}");
            return 2;
        }
        JAiBus::instance().reply(reply);
        return reply.rfind("{\"ok\":true", 0) == 0 ? 1 : 2;   // 2: answered, and the answer is a refusal
    };
    JLOGC(JScopeLog::kUi, JLogLevel::Info) << "AI bus: jscope verbs installed (jf-busctl call help)";
}

JScopeBusHooks JScopeAiBus::_hooks() {
    JScopeApp& app = m_app;
    JScopeBusHooks h;

    // ---- capture ----
    h.startRecording  = [&app](const std::string& path) { return app.startRecording(path); };
    h.stopRecording   = [&app] { app.stopRecording(); };
    h.recordingStatus = [&app] {
        JJson o = JJson::object();
        o["recording"] = JJson(app.isRecording());
        if (app.isRecording()) {
            o["path"]   = JJson(app.recordingPath());
            o["frames"] = JJson(static_cast<double>(app.framesRecorded()));
        }
        return o;
    };
    h.openCapture = [&app](const std::string& path) { return app.openCapture(path); };
    h.exportCsv   = [&app](const std::string& path) { return app.exportCsv(path); };
    h.captureDir  = [] { return JScopeApp::defaultCaptureDir(); };

    // ---- measurement window ----
    h.cursorWindow = [&app](double dt, uint32_t n, size_t& first, size_t& count) {
        return app.traceView().cursors().sampleWindow(dt, n, first, count);
    };

    // ---- cursors ----
    h.cursors = [&app] {
        const JCursorModel& c = app.traceView().cursors();
        JJson o = JJson::object();
        o["x"]   = JJson(c.xEnabled());
        o["y"]   = JJson(c.yEnabled());
        o["x1"]  = JJson(c.x1());
        o["x2"]  = JJson(c.x2());
        o["dx"]  = JJson(c.deltaX());
        if (c.haveFrequency()) o["frequency"] = JJson(c.frequency());
        o["y1"]  = JJson(c.y1());
        o["y2"]  = JJson(c.y2());
        o["dy"]  = JJson(c.deltaY());
        o["ych"] = JJson(static_cast<double>(c.yChannel() + 1));
        return o;
    };
    h.setCursors = [&app](const std::string& args, std::string& why) {
        JCursorModel& c = app.traceView().cursors();
        std::istringstream in(args);
        for (std::string tok; in >> tok;) {
            const size_t eq = tok.find('=');
            const std::string k = lower(tok.substr(0, eq));
            const std::string v = eq == std::string::npos ? std::string() : tok.substr(eq + 1);
            double x = 0.0; bool b = false;
            if (k == "reset")                      app.traceView().resetCursorsToFrame();
            else if (k == "x" && onOff(v, b))      c.setXEnabled(b);
            else if (k == "y" && onOff(v, b))      c.setYEnabled(b);
            else if (k == "x1" && number(v, x))    c.setX(x, c.x2());
            else if (k == "x2" && number(v, x))    c.setX(c.x1(), x);
            else if (k == "y1" && number(v, x))    c.setY(x, c.y2());
            else if (k == "y2" && number(v, x))    c.setY(c.y1(), x);
            else if (k == "ych" && number(v, x) && x >= 1.0 && x <= JScopeLimits::kMaxChannels)
                c.setYChannel(static_cast<uint8_t>(x - 1.0));
            else { why = "unknown cursor setting '" + tok + "'"; return false; }
        }
        // The measurements and the cursor panel follow the same signal a drag
        // on the trace raises, so the readouts agree with what the bus set.
        app.traceView().onCursorsChanged.emit();
        app.window().requestRedraw();
        return true;
    };

    // ---- view ----
    h.view = [&app] {
        JTraceView& t = app.traceView();
        JJson o = JJson::object();
        o["start"]     = JJson(t.viewStartFraction());
        o["span"]      = JJson(t.viewSpanFraction());
        o["reference"] = JJson(std::string(jReferenceSignalName(t.reference())));
        JJson refs = JJson::array();
        for (JReferenceSignal s : kReferenceSignals) refs.arr().push_back(JJson(std::string(jReferenceSignalName(s))));
        o["references"] = refs;
        JJson pos = JJson::object();
        if (const JScopeDriver* d = app.session().driver())
            for (uint8_t ch = 0; ch < d->capabilities().channelCount(); ++ch)
                pos["CH" + std::to_string(ch + 1)] = JJson(t.channelPosition(ch));
        o["positions"] = pos;
        return o;
    };
    h.setView = [&app](const std::string& args, std::string& why) {
        JTraceView& t = app.traceView();
        // A reference name has spaces in it ("Crankshaft (inductive)"), so
        // reference= takes the rest of the line.
        const size_t r = lower(args).find("reference=");
        const std::string head = r == std::string::npos ? args : args.substr(0, r);
        std::istringstream in(head);
        double start = t.viewStartFraction(), span = t.viewSpanFraction();
        bool window = false;
        for (std::string tok; in >> tok;) {
            const size_t eq = tok.find('=');
            const std::string k = lower(tok.substr(0, eq));
            const std::string v = eq == std::string::npos ? std::string() : tok.substr(eq + 1);
            double x = 0.0;
            if (k == "reset") { t.resetViewWindow(); start = t.viewStartFraction(); span = t.viewSpanFraction(); }
            else if (k == "start" && number(v, x)) { start = x; window = true; }
            else if (k == "span" && number(v, x))  { span = x;  window = true; }
            else if (k == "position") {
                const size_t colon = v.find(':');
                double ch = 0.0, volts = 0.0;
                if (colon == std::string::npos || !number(v.substr(0, colon), ch) ||
                    !number(v.substr(colon + 1), volts) || ch < 1.0 || ch > JScopeLimits::kMaxChannels) {
                    why = "position=<n>:<volts>";
                    return false;
                }
                t.setChannelPosition(static_cast<uint8_t>(ch - 1.0), volts);
            } else { why = "unknown view setting '" + tok + "'"; return false; }
        }
        if (window) t.setViewWindow(start, span);
        if (r != std::string::npos) {
            const std::string want = lower(args.substr(r + std::string("reference=").size()));
            bool found = false;
            for (JReferenceSignal s : kReferenceSignals)
                if (lower(jReferenceSignalName(s)).rfind(want, 0) == 0) { t.setReference(s); found = true; break; }
            if (!found) { why = "no reference called '" + want + "'"; return false; }
        }
        app.window().requestRedraw();
        return true;
    };

    // ---- docks ----
    h.docks = [&app] {
        JJson l = JJson::array();
        for (const JScopeDockToggle& t : app.docks().dockToggles()) {
            JJson e = JJson::object();
            e["title"]   = JJson(std::string(t.title));
            e["visible"] = JJson(JScopeDockLayout::isDockVisible(t));
            l.arr().push_back(e);
        }
        return l;
    };
    h.setDock = [&app](const std::string& title, bool on) {
        for (const JScopeDockToggle& t : app.docks().dockToggles())
            if (lower(t.title) == lower(title)) {
                app.docks().setDockVisible(t, on);
                app.window().requestRedraw();
                return true;
            }
        return false;
    };

    // ---- devices ----
    h.devices = [&app] {
        JJson l = JJson::array();
        const auto devs = app.availableDevices();
        for (size_t i = 0; i < devs.size(); ++i) l.arr().push_back(deviceJson(devs[i], i));
        return l;
    };
    h.openDevice = [&app](const std::string& which, std::string& why) {
        const auto devs = app.availableDevices();
        double idx = 0.0;
        if (number(which, idx)) {
            if (idx < 0.0 || idx >= static_cast<double>(devs.size())) { why = "no device " + which; return false; }
            return app.openDevice(devs[static_cast<size_t>(idx)]);
        }
        for (const JScopeDeviceInfo& d : devs)
            if (lower(d.driverId) == lower(which)) return app.openDevice(d);
        why = "no device with driver '" + which + "' on the bus";
        return false;
    };
    h.currentDevice = [&app] { return deviceJson(app.currentDevice(), 0); };

    // ---- the window ----
    h.screenshot = [] {
        JAppWindow::s_captureRequest.store(true);
        return std::string(JAppWindow::s_capturePath);
    };
    h.checkUpdates = [&app] { app.updater().check(true); };
    h.quit         = [&app] { app.window().requestClose(); };
    return h;
}

} // inline namespace jf
