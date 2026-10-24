// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JScopeBusCommands.h"

#include "JScopeActions.h"

#include "capture/JCsvExport.h"
#include "measure/JEdgeDetector.h"
#include "measure/JMeasurementEngine.h"
#include "scope/JPatternGenerator.h"
#include "scope/JScopeDriver.h"
#include "scope/JScopeLimits.h"
#include "scope/JScopeLog.h"
#include "scope/JScopeSession.h"
#include "sources/JReplayDriver.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

inline namespace jf {

namespace {

// How many edges a reply carries inline per channel unless asked for more. A
// 36-2 wheel at a few thousand rpm puts several thousand edges in a long record;
// the summary says how many there were, and `file=` takes all of them.
constexpr size_t kDefaultEdgeLimit   = 2000;
// Samples per channel `frame samples` returns inline at most.
constexpr size_t kMaxInlineSamples   = 200000;

JJson okObj() { JJson o = JJson::object(); o["ok"] = JJson(true); return o; }
JJson failObj(const std::string& why) {
    JJson o = JJson::object();
    o["ok"] = JJson(false);
    o["error"] = JJson(why);
    return o;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

// "key=value" -> (key, value); a bare word -> (word, "").
std::pair<std::string, std::string> keyValue(const std::string& tok) {
    const size_t eq = tok.find('=');
    if (eq == std::string::npos) return { lower(tok), {} };
    return { lower(tok.substr(0, eq)), tok.substr(eq + 1) };
}

bool parseNumber(const std::string& s, double& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return end && *end == '\0' && std::isfinite(v);
}

bool parseBool(const std::string& s, bool& v) {
    const std::string l = lower(s);
    if (l == "on"  || l == "1" || l == "true"  || l == "yes") { v = true;  return true; }
    if (l == "off" || l == "0" || l == "false" || l == "no")  { v = false; return true; }
    return false;
}

JJson num(double v) { return std::isfinite(v) ? JJson(v) : JJson(nullptr); }

struct JStats {
    size_t n{0};
    double mean{0.0}, min{0.0}, max{0.0};
};
JStats stats(const std::vector<double>& v) {
    JStats s;
    if (v.empty()) return s;
    s.n = v.size();
    s.min = *std::min_element(v.begin(), v.end());
    s.max = *std::max_element(v.begin(), v.end());
    double sum = 0.0;
    for (double x : v) sum += x;
    s.mean = sum / static_cast<double>(v.size());
    return s;
}
JJson statsJson(const std::vector<double>& v) {
    const JStats s = stats(v);
    JJson o = JJson::object();
    o["count"] = JJson(static_cast<double>(s.n));
    if (s.n) { o["mean"] = num(s.mean); o["min"] = num(s.min); o["max"] = num(s.max); }
    return o;
}

const char* couplingId(JScopeCoupling c) {
    switch (c) {
        case JScopeCoupling::DC:     return "dc";
        case JScopeCoupling::AC:     return "ac";
        case JScopeCoupling::Ground: return "gnd";
    }
    return "dc";
}

} // namespace

JScopeBusCommands::JScopeBusCommands(JScopeSession& session, JScopeActions& actions, JScopeBusHooks hooks)
    : m_session(session), m_actions(actions), m_hooks(std::move(hooks)) {
    m_disconnectRefusal = m_actions.onRefused.connect([this](std::string why) { m_refusal = std::move(why); });
}

JScopeBusCommands::~JScopeBusCommands() {
    if (m_disconnectRefusal) m_disconnectRefusal();
}

const std::vector<std::pair<std::string, std::string>>& JScopeBusCommands::verbs() {
    static const std::vector<std::pair<std::string, std::string>> v = {
        { "help",       "help                                   this list" },
        { "status",     "status                                 device, run state, timebase, trigger, every channel, last frame" },
        { "caps",       "caps                                   what the open instrument can do (steps, ranges, modes)" },
        { "channel",    "channel <n> [on|off] [vdiv=V] [offset=V] [coupling=dc|ac|gnd] [probe=X] [invert=on|off] [bwlimit=on|off]" },
        { "timebase",   "timebase [sdiv=S] [mode=windowed|streaming] [length=N] [rate=Sa/s] [pos=0..1]" },
        { "trigger",    "trigger [mode=auto|normal|single] [slope=rising|falling|either] [source=n] [level=V] [pos=0..1] [timeout=S]" },
        { "run",        "run | stop | single | force | autoset | toggle     acquisition, as the Acquire menu" },
        { "mode",       "mode windowed|streaming                acquisition mode" },
        { "options",    "options                                the instrument's own settings (Instrument menu)" },
        { "option",     "option <id> <value>                    set one of them" },
        { "generator",  "generator [on|off] [rpm=N] | generator load|save <path.squ> | generator download" },
        { "measure",    "measure [n...] [whole]                 Vpp Vmax Vmin Vavg Vrms Top Base Ampl Freq Period Duty Rise Fall" },
        { "edges",      "edges <n...> [level=V] [hyst=V] [polarity=rising|falling|both] [from=S] [to=S] [ref=trigger|start] [max=N] [file=path]" },
        { "frame",      "frame | frame save <path.csv> | frame samples <n> [first] [count] [step]" },
        { "export",     "export [path.csv]                      File > Export CSV (the cursor window when X cursors are on)" },
        { "record",     "record start [path] | record stop | record status" },
        { "capture",    "capture open <path.jscope>             replay a recording" },
        { "replay",     "replay [status|play|pause|seek N|step N|rate X|loop on|off]" },
        { "devices",    "devices                                instruments on the bus now" },
        { "open",       "open <index|driverId>                  connect to one (asynchronous; watch status)" },
        { "cursors",    "cursors [x=on|off] [y=on|off] [x1=S] [x2=S] [y1=V] [y2=V] [ych=n] [reset]" },
        { "view",       "view [reset] [start=F] [span=F] [reference=name] [position=n:V]" },
        { "docks",      "docks | dock <title> on|off            the View menu's panels" },
        { "screenshot", "screenshot                             the next rendered frame to a PPM" },
        { "update",     "update                                 Help > Check for Updates" },
        { "quit",       "quit                                   close the window (settings are saved)" },
    };
    return v;
}

// ---- reading ------------------------------------------------------------------

JJson JScopeBusCommands::_channel(uint8_t ch) const {
    JJson o = JJson::object();
    const JScopeDriver* d = m_session.driver();
    if (!d || ch >= d->capabilities().channelCount()) return o;
    const JScopeChannelConfig& c = d->channelConfig(ch);
    o["channel"]  = JJson(static_cast<double>(ch + 1));
    o["label"]    = JJson(d->capabilities().channels[ch].label);
    o["enabled"]  = JJson(c.enabled);
    o["vdiv"]     = num(c.voltsPerDiv);
    o["offset"]   = num(c.offsetVolts);
    o["coupling"] = JJson(std::string(couplingId(c.coupling)));
    o["probe"]    = num(c.probeRatio);
    o["invert"]   = JJson(c.inverted);
    o["bwlimit"]  = JJson(c.bandwidthLimited);
    return o;
}

JJson JScopeBusCommands::_timebase() const {
    JJson o = JJson::object();
    const JScopeDriver* d = m_session.driver();
    if (!d) return o;
    const JScopeTimebaseConfig& t = d->timebaseConfig();
    o["mode"]   = JJson(lower(jScopeAcquisitionModeName(t.mode)));
    o["sdiv"]   = num(t.secondsPerDiv);
    o["pos"]    = num(t.triggerPosition);
    o["length"] = JJson(static_cast<double>(t.recordLength));
    o["rate"]   = num(t.sampleRate);
    return o;
}

JJson JScopeBusCommands::_trigger() const {
    JJson o = JJson::object();
    const JScopeDriver* d = m_session.driver();
    if (!d) return o;
    const JScopeTriggerConfig& t = d->triggerConfig();
    o["mode"]    = JJson(lower(jScopeTriggerModeName(t.mode)));
    o["slope"]   = JJson(lower(jScopeTriggerSlopeName(t.slope)));
    o["source"]  = JJson(static_cast<double>(t.sourceChannel + 1));
    o["level"]   = num(t.levelVolts);
    o["timeout"] = num(t.autoTimeoutSeconds);
    o["pos"]     = num(d->timebaseConfig().triggerPosition);
    return o;
}

JJson JScopeBusCommands::_generator() const {
    const JPatternGenerator* g = m_actions.patternGenerator();
    if (!g) return JJson(nullptr);
    JJson o = JJson::object();
    o["output"]        = JJson(g->isOutputEnabled());
    o["rpm"]           = JJson(static_cast<double>(g->requestedRpm()));
    o["actual_rpm"]    = JJson(static_cast<double>(g->actualRpm()));
    o["max_rpm"]       = JJson(static_cast<double>(g->maxRpm()));
    o["pattern_steps"] = JJson(static_cast<double>(g->pattern().size()));
    if (!g->lastError().empty()) o["last_error"] = JJson(g->lastError());
    return o;
}

JJson JScopeBusCommands::_replay() const {
    JReplayDriver* r = m_session.replay();   // a reference member: this const method can still ask
    if (!r) return JJson(nullptr);
    JJson o = JJson::object();
    o["frames"]   = JJson(static_cast<double>(r->frameCount()));
    o["position"] = JJson(static_cast<double>(r->position()));
    o["rate"]     = num(r->rate());
    o["loop"]     = JJson(r->looping());
    o["recovered"] = JJson(r->wasRecovered());
    return o;
}

JJson JScopeBusCommands::_status() const {
    JJson o = okObj();
    const JScopeDriver* d = m_session.driver();
    JJson dev = JJson::object();
    dev["open"]       = JJson(m_session.isOpen());
    dev["connecting"] = JJson(m_session.isConnecting());
    dev["busy"]       = JJson(m_session.isBusy());
    if (d) {
        dev["driver"] = JJson(d->driverId());
        dev["model"]  = JJson(d->capabilities().model);
        dev["serial"] = JJson(d->capabilities().serialNumber);
    }
    if (m_hooks.currentDevice) dev["selected"] = m_hooks.currentDevice();
    o["device"] = dev;
    o["state"]  = JJson(lower(d ? jScopeStateName(d->state()) : jScopeStateName(JScopeState::Closed)));
    if (d) {
        o["single_pending"] = JJson(d->singleShotPending());
        if (!d->triggerStatusText().empty()) o["trigger_status"] = JJson(d->triggerStatusText());
        if (!d->acquisitionText().empty())   o["acquisition"]    = JJson(d->acquisitionText());
        o["timebase"] = _timebase();
        o["trigger"]  = _trigger();
        JJson chans = JJson::array();
        for (uint8_t ch = 0; ch < d->capabilities().channelCount(); ++ch) chans.arr().push_back(_channel(ch));
        o["channels"] = chans;
        o["generator"] = _generator();
        o["replay"]    = _replay();
    }
    JJson fr = JJson::object();
    fr["received"] = JJson(static_cast<double>(m_session.framesReceived()));
    fr["dropped"]  = JJson(static_cast<double>(m_session.framesDropped()));
    if (const JScopeFrame* f = m_session.latestFrame()) {
        fr["sequence"]        = JJson(static_cast<double>(f->header.sequence));
        fr["sample_interval"] = num(f->header.sampleInterval);
        fr["samples"]         = JJson(static_cast<double>(f->header.sampleCount));
        fr["duration"]        = num(f->header.durationSeconds());
        fr["triggered"]       = JJson(f->header.triggered);
        fr["trigger_sample"]  = JJson(static_cast<double>(f->header.triggerSampleIndex));
        JJson planes = JJson::array();
        for (uint8_t p = 0; p < f->header.channelCount; ++p)
            planes.arr().push_back(JJson(static_cast<double>(f->header.channelIds[p] + 1)));
        fr["channels"] = planes;
    }
    o["frame"] = fr;
    if (m_hooks.recordingStatus) o["recording"] = m_hooks.recordingStatus();
    if (m_hooks.statusLine) o["status_line"] = JJson(m_hooks.statusLine());
    return o;
}

JJson JScopeBusCommands::_capabilities() const {
    JJson o = okObj();
    const JScopeDriver* d = m_session.driver();
    if (!d) return failObj("no device is open");
    const JScopeCapabilities& c = d->capabilities();
    auto list = [](const auto& v) {
        JJson a = JJson::array();
        for (const auto& x : v) a.arr().push_back(JJson(static_cast<double>(x)));
        return a;
    };
    o["driver"] = JJson(c.driverId);
    o["model"]  = JJson(c.model);
    o["serial"] = JJson(c.serialNumber);
    o["adc_bits"] = JJson(static_cast<double>(c.adcBits));
    o["max_simultaneous_channels"] = JJson(static_cast<double>(c.maxSimultaneousChannels));
    o["divisions"] = JJson(std::to_string(c.horizontalDivisions) + "x" + std::to_string(c.verticalDivisions));
    JJson modes = JJson::array();
    for (JScopeAcquisitionMode m : { JScopeAcquisitionMode::Windowed, JScopeAcquisitionMode::Streaming })
        if (jScopeAcquisitionModeSupported(c.acquisitionModes, m))
            modes.arr().push_back(JJson(lower(jScopeAcquisitionModeName(m))));
    o["acquisition_modes"] = modes;
    JJson tmodes = JJson::array();
    for (JScopeTriggerMode m : { JScopeTriggerMode::Auto, JScopeTriggerMode::Normal, JScopeTriggerMode::Single })
        if (jScopeTriggerModeSupported(c.triggerModes, m))
            tmodes.arr().push_back(JJson(lower(jScopeTriggerModeName(m))));
    o["trigger_modes"] = tmodes;
    o["sdiv_steps"]    = list(c.secondsPerDiv);
    o["stream_rates"]  = list(c.streamSampleRates);
    o["memory_depths"] = list(c.memoryDepths);
    o["device_record_length"] = JJson(c.deviceDeterminedRecordLength);
    o["hardware_trigger"] = JJson(c.hasHardwareTrigger);
    o["trigger_position"] = JJson(c.hasTriggerPosition);
    o["force_trigger"]    = JJson(c.hasForceTrigger);
    o["autoset"]          = JJson(c.hasAutoset);
    JJson chans = JJson::array();
    for (uint8_t i = 0; i < c.channelCount(); ++i) {
        const JScopeChannelCaps& cc = c.channels[i];
        JJson e = JJson::object();
        e["channel"] = JJson(static_cast<double>(i + 1));
        e["label"]   = JJson(cc.label);
        e["vdiv_steps"]   = list(cc.voltsPerDiv);
        e["probe_ratios"] = list(cc.probeRatios);
        JJson cpl = JJson::array();
        for (JScopeCoupling k : cc.couplings) cpl.arr().push_back(JJson(std::string(couplingId(k))));
        e["couplings"]    = cpl;
        e["offset_range"] = num(cc.offsetRangeVolts);
        e["invert"]       = JJson(cc.canInvert);
        e["bwlimit"]      = JJson(cc.canBandwidthLimit);
        chans.arr().push_back(e);
    }
    o["channels"] = chans;
    return o;
}

// ---- helpers --------------------------------------------------------------------

int JScopeBusCommands::_planeOf(const JScopeFrame& f, uint8_t ch) {
    for (uint8_t p = 0; p < f.header.channelCount; ++p)
        if (f.header.channelIds[p] == ch) return p;
    return -1;
}

bool JScopeBusCommands::_parseChannel(const std::string& s, uint8_t& ch, std::string& why) const {
    std::string t = lower(s);
    if (t.rfind("ch", 0) == 0) t = t.substr(2);
    double v = 0.0;
    if (!parseNumber(t, v) || v < 1.0 || std::floor(v) != v) { why = "not a channel: '" + s + "'"; return false; }
    const JScopeDriver* d = m_session.driver();
    const double n = d ? d->capabilities().channelCount() : JScopeLimits::kMaxChannels;
    if (v > n) { why = "no channel " + s + " on this instrument"; return false; }
    ch = static_cast<uint8_t>(v - 1.0);
    return true;
}

bool JScopeBusCommands::_ok(bool done, std::string& why, const char* what) {
    if (!done) why = m_refusal.empty() ? std::string(what) + " was refused" : m_refusal;
    return done;
}

// ---- configuration ---------------------------------------------------------------

bool JScopeBusCommands::_doChannel(const Args& a, JJson& out, std::string& why) {
    if (!m_session.driver()) { why = "no device is open"; return false; }
    if (a.size() < 2) { why = "channel <n> [settings...]"; return false; }
    uint8_t ch = 0;
    if (!_parseChannel(a[1], ch, why)) return false;
    for (size_t i = 2; i < a.size(); ++i) {
        const auto [k, v] = keyValue(a[i]);
        double x = 0.0; bool b = false;
        bool ok = true;
        if (v.empty() && parseBool(k, b))            ok = _ok(m_actions.setChannelEnabled(ch, b), why, "enable");
        else if (k == "enabled" && parseBool(v, b))  ok = _ok(m_actions.setChannelEnabled(ch, b), why, "enable");
        else if (k == "vdiv"    && parseNumber(v, x)) ok = _ok(m_actions.setVoltsPerDiv(ch, x), why, "volts/div");
        else if (k == "offset"  && parseNumber(v, x)) ok = _ok(m_actions.setOffsetVolts(ch, x), why, "offset");
        else if (k == "probe"   && parseNumber(v, x)) ok = _ok(m_actions.setProbeRatio(ch, x), why, "probe");
        else if (k == "invert"  && parseBool(v, b))   ok = _ok(m_actions.setInverted(ch, b), why, "invert");
        else if (k == "coupling") {
            const std::string c = lower(v);
            if (c == "dc")       ok = _ok(m_actions.setCoupling(ch, JScopeCoupling::DC), why, "coupling");
            else if (c == "ac")  ok = _ok(m_actions.setCoupling(ch, JScopeCoupling::AC), why, "coupling");
            else if (c == "gnd" || c == "ground") ok = _ok(m_actions.setCoupling(ch, JScopeCoupling::Ground), why, "coupling");
            else { why = "coupling is dc, ac or gnd"; return false; }
        } else if (k == "bwlimit" && parseBool(v, b)) {
            JScopeChannelConfig cfg = m_session.driver()->channelConfig(ch);
            cfg.bandwidthLimited = b;
            ok = _ok(m_actions.applyChannel(ch, cfg), why, "bandwidth limit");
        } else { why = "unknown channel setting '" + a[i] + "'"; return false; }
        if (!ok) return false;
    }
    out["channel"] = _channel(ch);
    return true;
}

bool JScopeBusCommands::_doTimebase(const Args& a, JJson& out, std::string& why) {
    if (!m_session.driver()) { why = "no device is open"; return false; }
    for (size_t i = 1; i < a.size(); ++i) {
        const auto [k, v] = keyValue(a[i]);
        double x = 0.0;
        bool ok = true;
        if (k == "sdiv" && parseNumber(v, x))        ok = _ok(m_actions.setSecondsPerDiv(x), why, "timebase");
        else if (k == "length" && parseNumber(v, x) && x >= 0.0)
            ok = _ok(m_actions.setRecordLength(static_cast<uint32_t>(x)), why, "record length");
        else if (k == "rate" && parseNumber(v, x))   ok = _ok(m_actions.setStreamRate(x), why, "stream rate");
        else if (k == "pos" && parseNumber(v, x))    ok = _ok(m_actions.setTriggerPosition(x), why, "trigger position");
        else if (k == "mode") {
            const std::string m = lower(v);
            if (m == "windowed")       ok = _ok(m_actions.setAcquisitionMode(JScopeAcquisitionMode::Windowed), why, "mode");
            else if (m == "streaming") ok = _ok(m_actions.setAcquisitionMode(JScopeAcquisitionMode::Streaming), why, "mode");
            else { why = "mode is windowed or streaming"; return false; }
        } else { why = "unknown timebase setting '" + a[i] + "'"; return false; }
        if (!ok) return false;
    }
    out["timebase"] = _timebase();
    return true;
}

bool JScopeBusCommands::_doTrigger(const Args& a, JJson& out, std::string& why) {
    if (!m_session.driver()) { why = "no device is open"; return false; }
    for (size_t i = 1; i < a.size(); ++i) {
        const auto [k, v] = keyValue(a[i]);
        double x = 0.0;
        bool ok = true;
        const std::string l = lower(v);
        if (k == "mode") {
            if (l == "auto")        ok = _ok(m_actions.setTriggerMode(JScopeTriggerMode::Auto), why, "trigger mode");
            else if (l == "normal") ok = _ok(m_actions.setTriggerMode(JScopeTriggerMode::Normal), why, "trigger mode");
            else if (l == "single") ok = _ok(m_actions.setTriggerMode(JScopeTriggerMode::Single), why, "trigger mode");
            else { why = "trigger mode is auto, normal or single"; return false; }
        } else if (k == "slope") {
            if (l == "rising")       ok = _ok(m_actions.setTriggerSlope(JScopeTriggerSlope::Rising), why, "slope");
            else if (l == "falling") ok = _ok(m_actions.setTriggerSlope(JScopeTriggerSlope::Falling), why, "slope");
            else if (l == "either")  ok = _ok(m_actions.setTriggerSlope(JScopeTriggerSlope::Either), why, "slope");
            else { why = "slope is rising, falling or either"; return false; }
        } else if (k == "source") {
            uint8_t ch = 0;
            if (!_parseChannel(v, ch, why)) return false;
            ok = _ok(m_actions.setTriggerSource(ch), why, "trigger source");
        } else if (k == "level" && parseNumber(v, x)) ok = _ok(m_actions.setTriggerLevel(x), why, "trigger level");
        else if (k == "pos" && parseNumber(v, x))    ok = _ok(m_actions.setTriggerPosition(x), why, "trigger position");
        else if (k == "timeout" && parseNumber(v, x) && x > 0.0) {
            JScopeTriggerConfig t = m_session.driver()->triggerConfig();
            t.autoTimeoutSeconds = x;
            ok = _ok(m_actions.applyTrigger(t), why, "auto timeout");
        } else { why = "unknown trigger setting '" + a[i] + "'"; return false; }
        if (!ok) return false;
    }
    out["trigger"] = _trigger();
    return true;
}

bool JScopeBusCommands::_doAcquire(const std::string& verb, JJson& out, std::string& why) {
    bool done = false;
    if (verb == "run")          done = m_actions.run();
    else if (verb == "stop")    done = m_actions.stop();
    else if (verb == "single")  done = m_actions.single();
    else if (verb == "force")   done = m_actions.forceTrigger();
    else if (verb == "autoset") done = m_actions.autoset();
    else if (verb == "toggle")  done = m_actions.toggleRunStop();
    if (!_ok(done, why, verb.c_str())) return false;
    const JScopeDriver* d = m_session.driver();
    out["state"] = JJson(lower(d ? jScopeStateName(d->state()) : "closed"));
    if (verb == "autoset") out["note"] = JJson(std::string("autoset runs on the device worker; status shows when it is done"));
    return true;
}

bool JScopeBusCommands::_doOption(const Args& a, JJson& out, std::string& why) {
    JScopeDriver* d = m_session.driver();
    if (!d) { why = "no device is open"; return false; }
    if (a[0] == "option") {
        if (a.size() < 3) { why = "option <id> <value>"; return false; }
        std::string value = a[2];
        for (size_t i = 3; i < a.size(); ++i) value += " " + a[i];   // the instrument's words may have spaces
        if (!d->setInstrumentOption(a[1], value)) {
            why = "the instrument refused " + a[1] + " = " + value;
            return false;
        }
    }
    JJson list = JJson::array();
    for (const JScopeOption& o : d->instrumentOptions()) {
        JJson e = JJson::object();
        e["id"] = JJson(o.id);
        e["label"] = JJson(o.label);
        e["current"] = JJson(o.current);
        JJson vals = JJson::array();
        for (const std::string& v : o.values) vals.arr().push_back(JJson(v));
        e["values"] = vals;
        list.arr().push_back(e);
    }
    out["options"] = list;
    return true;
}

bool JScopeBusCommands::_doGenerator(const Args& a, JJson& out, std::string& why) {
    if (!m_actions.patternGenerator()) { why = "this device has no pattern generator"; return false; }
    for (size_t i = 1; i < a.size(); ++i) {
        const auto [k, v] = keyValue(a[i]);
        bool b = false; double x = 0.0;
        if (v.empty() && parseBool(k, b)) {
            if (!_ok(m_actions.setGeneratorOutput(b), why, "generator output")) return false;
        } else if (k == "rpm" && parseNumber(v, x) && x >= 0.0) {
            if (!_ok(m_actions.setGeneratorRpm(static_cast<uint32_t>(x)), why, "generator rpm")) return false;
        } else if (k == "download") {
            if (!_ok(m_actions.downloadGeneratorPattern(), why, "download")) return false;
        } else if ((k == "load" || k == "save") && i + 1 < a.size()) {
            const std::string path = a[++i];
            const bool done = k == "load" ? m_actions.loadGeneratorPattern(path)
                                          : m_actions.saveGeneratorPattern(path);
            if (!_ok(done, why, k.c_str())) return false;
            out["path"] = JJson(path);
        } else { why = "unknown generator setting '" + a[i] + "'"; return false; }
    }
    out["generator"] = _generator();
    return true;
}

// ---- the frame ------------------------------------------------------------------

bool JScopeBusCommands::_doMeasure(const Args& a, JJson& out, std::string& why) {
    const JScopeFrame* f = m_session.latestFrame();
    if (!f) { why = "no frame yet"; return false; }
    bool whole = false;
    std::vector<uint8_t> chans;
    for (size_t i = 1; i < a.size(); ++i) {
        if (lower(a[i]) == "whole") { whole = true; continue; }
        uint8_t ch = 0;
        if (!_parseChannel(a[i], ch, why)) return false;
        chans.push_back(ch);
    }
    if (chans.empty())
        for (uint8_t p = 0; p < f->header.channelCount; ++p) chans.push_back(f->header.channelIds[p]);

    JMeasurementWindow win;
    bool windowed = false;
    if (!whole && m_hooks.cursorWindow)
        windowed = m_hooks.cursorWindow(f->header.sampleInterval, f->header.sampleCount, win.first, win.count);
    out["window"] = JJson(std::string(windowed ? "cursors" : "whole"));
    out["sequence"] = JJson(static_cast<double>(f->header.sequence));
    JJson res = JJson::object();
    for (uint8_t ch : chans) {
        const int p = _planeOf(*f, ch);
        if (p < 0) { res["CH" + std::to_string(ch + 1)] = JJson(nullptr); continue; }
        JJson m = JJson::object();
        for (const JMeasurementResult& r : JMeasurementEngine::measureAll(*f, static_cast<uint8_t>(p), win))
            m[jMeasurementKindName(r.kind)] = r.valid ? num(r.value) : JJson(nullptr);
        res["CH" + std::to_string(ch + 1)] = m;
    }
    out["measurements"] = res;
    return true;
}

bool JScopeBusCommands::_doEdges(const Args& a, JJson& out, std::string& why) {
    const JScopeFrame* f = m_session.latestFrame();
    if (!f) { why = "no frame yet"; return false; }
    std::vector<uint8_t> chans;
    double level = std::numeric_limits<double>::quiet_NaN(), hyst = std::numeric_limits<double>::quiet_NaN();
    double from = -std::numeric_limits<double>::infinity(), to = std::numeric_limits<double>::infinity();
    std::string polarity = "both", ref = f->header.triggered ? "trigger" : "start", file;
    size_t maxEdges = kDefaultEdgeLimit;
    for (size_t i = 1; i < a.size(); ++i) {
        const auto [k, v] = keyValue(a[i]);
        double x = 0.0;
        if (v.empty()) {
            uint8_t ch = 0;
            if (!_parseChannel(a[i], ch, why)) return false;
            chans.push_back(ch);
        } else if (k == "level" && parseNumber(v, x)) level = x;
        else if (k == "hyst" && parseNumber(v, x) && x >= 0.0) hyst = x;
        else if (k == "from" && parseNumber(v, x)) from = x;
        else if (k == "to" && parseNumber(v, x)) to = x;
        else if (k == "max" && parseNumber(v, x) && x >= 0.0) maxEdges = static_cast<size_t>(x);
        else if (k == "polarity" && (lower(v) == "rising" || lower(v) == "falling" || lower(v) == "both")) polarity = lower(v);
        else if (k == "ref" && (lower(v) == "trigger" || lower(v) == "start")) ref = lower(v);
        else if (k == "file") file = v;
        else { why = "unknown edges setting '" + a[i] + "'"; return false; }
    }
    if (chans.empty()) { why = "edges <n...> — which channels?"; return false; }
    // Times are from the start of the record, or from the trigger point when
    // there is one: a scope's own t = 0.
    const double dt = f->header.sampleInterval;
    const double t0 = (ref == "trigger" && f->header.triggered && f->header.triggerSampleIndex >= 0)
                    ? dt * static_cast<double>(f->header.triggerSampleIndex) : 0.0;

    std::ofstream csv;
    if (!file.empty()) {
        csv.open(file);
        if (!csv) { why = "cannot write " + file; return false; }
        csv << "channel,time_s,edge\n";
    }

    out["reference"] = JJson(ref == "trigger" && f->header.triggered ? std::string("trigger") : std::string("start"));
    out["sample_interval"] = num(dt);
    out["sequence"] = JJson(static_cast<double>(f->header.sequence));
    JJson res = JJson::object();
    for (uint8_t ch : chans) {
        const std::string key = "CH" + std::to_string(ch + 1);
        const int p = _planeOf(*f, ch);
        if (p < 0) { res[key] = JJson(nullptr); continue; }
        const int16_t* s = f->plane(static_cast<uint8_t>(p));
        const JEdgeDetector::JLevels lv = JEdgeDetector::findLevels(s, f->header.sampleCount,
                                                                   f->header.countsToVolts[p],
                                                                   f->header.zeroOffsetCounts[p]);
        const double useLevel = std::isfinite(level) ? level : (lv.valid ? lv.mid() : 0.0);
        const double useHyst  = std::isfinite(hyst) ? hyst
                              : (lv.valid ? lv.amplitude() * JEdgeDetector::kDefaultHysteresisFraction : 0.0);
        const auto all = JEdgeDetector::findCrossings(s, f->header.sampleCount, f->header.countsToVolts[p],
                                                      f->header.zeroOffsetCounts[p], dt, useLevel, useHyst);
        JJson e = JJson::object();
        e["level"] = num(useLevel);
        e["hysteresis"] = num(useHyst);
        if (lv.valid) { e["top"] = num(lv.top); e["base"] = num(lv.base); }
        std::vector<double> rise, fall;
        JJson list = JJson::array();
        size_t listed = 0;
        for (const JEdgeDetector::JCrossing& c : all) {
            const double t = c.time - t0;
            if (t < from || t > to) continue;
            (c.rising ? rise : fall).push_back(t);
            if ((polarity == "rising" && !c.rising) || (polarity == "falling" && c.rising)) continue;
            if (csv) csv << (ch + 1) << ',' << t << ',' << (c.rising ? "rising" : "falling") << '\n';
            if (listed < maxEdges) {
                JJson x = JJson::array();
                x.arr().push_back(num(t));
                x.arr().push_back(JJson(std::string(c.rising ? "r" : "f")));
                list.arr().push_back(x);
                ++listed;
            }
        }
        auto diffs = [](const std::vector<double>& v) {
            std::vector<double> d;
            for (size_t i = 1; i < v.size(); ++i) d.push_back(v[i] - v[i - 1]);
            return d;
        };
        // How long it sits high: each rising edge to the falling edge after it.
        std::vector<double> high;
        for (double r : rise) {
            const auto it = std::upper_bound(fall.begin(), fall.end(), r);
            if (it != fall.end()) high.push_back(*it - r);
        }
        e["rising"]  = JJson(static_cast<double>(rise.size()));
        e["falling"] = JJson(static_cast<double>(fall.size()));
        e["rising_interval"]  = statsJson(diffs(rise));
        e["falling_interval"] = statsJson(diffs(fall));
        e["high_time"]        = statsJson(high);
        e["edges"] = list;
        e["listed"] = JJson(static_cast<double>(listed));
        res[key] = e;
    }
    out["channels"] = res;
    if (!file.empty()) out["file"] = JJson(file);
    return true;
}

bool JScopeBusCommands::_doFrame(const Args& a, JJson& out, std::string& why) {
    const JScopeFrame* f = m_session.latestFrame();
    if (!f) { why = "no frame yet"; return false; }
    const JScopeFrameHeader& h = f->header;
    out["sequence"]        = JJson(static_cast<double>(h.sequence));
    out["timestamp"]       = num(h.timestampSeconds);
    out["sample_interval"] = num(h.sampleInterval);
    out["samples"]         = JJson(static_cast<double>(h.sampleCount));
    out["duration"]        = num(h.durationSeconds());
    out["triggered"]       = JJson(h.triggered);
    out["trigger_sample"]  = JJson(static_cast<double>(h.triggerSampleIndex));
    out["mode"]            = JJson(lower(jScopeAcquisitionModeName(h.mode)));
    JJson planes = JJson::array();
    for (uint8_t p = 0; p < h.channelCount; ++p) {
        JJson e = JJson::object();
        e["channel"] = JJson(static_cast<double>(h.channelIds[p] + 1));
        e["vdiv"]    = num(h.voltsPerDiv[p]);
        planes.arr().push_back(e);
    }
    out["channels"] = planes;
    if (a.size() < 2) return true;
    const std::string sub = lower(a[1]);
    if (sub == "save") {
        if (a.size() < 3) { why = "frame save <path.csv>"; return false; }
        const std::string model = m_session.driver() ? m_session.driver()->capabilities().model : "unknown";
        if (!JCsvExport::write(a[2], *f, model)) { why = "cannot write " + a[2]; return false; }
        out["path"] = JJson(a[2]);
        return true;
    }
    if (sub == "samples") {
        if (a.size() < 3) { why = "frame samples <n> [first] [count] [step]"; return false; }
        uint8_t ch = 0;
        if (!_parseChannel(a[2], ch, why)) return false;
        const int p = _planeOf(*f, ch);
        if (p < 0) { why = "CH" + std::to_string(ch + 1) + " is not in this frame"; return false; }
        double first = 0.0, count = static_cast<double>(h.sampleCount), step = 1.0;
        if (a.size() > 3 && !parseNumber(a[3], first)) { why = "first is a sample index"; return false; }
        if (a.size() > 4 && !parseNumber(a[4], count)) { why = "count is a sample count"; return false; }
        if (a.size() > 5 && (!parseNumber(a[5], step) || step < 1.0)) { why = "step is 1 or more"; return false; }
        const size_t s0 = std::min(static_cast<size_t>(std::max(0.0, first)), static_cast<size_t>(h.sampleCount));
        const size_t s1 = std::min(s0 + static_cast<size_t>(std::max(0.0, count)), static_cast<size_t>(h.sampleCount));
        const size_t st = static_cast<size_t>(step);
        if ((s1 - s0) / st > kMaxInlineSamples) {
            why = "more than " + std::to_string(kMaxInlineSamples) + " samples: use a step, or frame save";
            return false;
        }
        JJson v = JJson::array();
        for (size_t i = s0; i < s1; i += st) v.arr().push_back(num(f->voltsAt(static_cast<uint8_t>(p), static_cast<uint32_t>(i))));
        out["first"] = JJson(static_cast<double>(s0));
        out["step"]  = JJson(static_cast<double>(st));
        out["volts"] = v;
        return true;
    }
    why = "frame | frame save <path> | frame samples <n> [first] [count] [step]";
    return false;
}

// ---- capture and replay ----------------------------------------------------------

bool JScopeBusCommands::_doRecord(const Args& a, JJson& out, std::string& why) {
    if (!m_hooks.startRecording || !m_hooks.recordingStatus) { why = "recording is not available here"; return false; }
    const std::string sub = a.size() > 1 ? lower(a[1]) : "status";
    if (sub == "start") {
        std::string path = a.size() > 2 ? a[2] : std::string();
        if (path.empty() && m_hooks.captureDir) {
            path = m_hooks.captureDir() + "/capture_" +
                   std::to_string(static_cast<long long>(m_session.framesReceived())) + ".jscope";
        }
        if (!m_hooks.startRecording(path)) { why = "cannot record to " + path; return false; }
    } else if (sub == "stop") {
        if (m_hooks.stopRecording) m_hooks.stopRecording();
    } else if (sub != "status") { why = "record start [path] | record stop | record status"; return false; }
    out["recording"] = m_hooks.recordingStatus();
    return true;
}

bool JScopeBusCommands::_doReplay(const Args& a, JJson& out, std::string& why) {
    JReplayDriver* r = m_session.replay();
    if (!r) { why = "no capture is open (capture open <path>)"; return false; }
    const std::string sub = a.size() > 1 ? lower(a[1]) : "status";
    double x = 0.0; bool b = false;
    if (sub == "play")       { if (!_ok(m_actions.run(), why, "play")) return false; }
    else if (sub == "pause") { if (!_ok(m_actions.stop(), why, "pause")) return false; }
    else if (sub == "seek" && a.size() > 2 && parseNumber(a[2], x) && x >= 0.0) r->seek(static_cast<uint64_t>(x));
    else if (sub == "step" && a.size() > 2 && parseNumber(a[2], x)) r->step(static_cast<int64_t>(x));
    else if (sub == "rate" && a.size() > 2 && parseNumber(a[2], x) && x > 0.0) r->setRate(x);
    else if (sub == "loop" && a.size() > 2 && parseBool(a[2], b)) r->setLooping(b);
    else if (sub != "status") { why = "replay [status|play|pause|seek N|step N|rate X|loop on|off]"; return false; }
    out["replay"] = _replay();
    return true;
}

// ---- dispatch -------------------------------------------------------------------

bool JScopeBusCommands::handle(const std::string& line, std::string& reply) {
    Args a = split(line);
    if (a.empty()) return false;
    a[0] = lower(a[0]);
    const std::string& verb = a[0];
    m_refusal.clear();

    JJson out = okObj();
    std::string why;
    bool done = true;
    auto hook = [&why](bool present, const char* what) {
        if (!present) why = std::string(what) + " is not available here";
        return present;
    };

    if (verb == "help") {
        JJson l = JJson::array();
        for (const auto& [name, syntax] : verbs()) l.arr().push_back(JJson(syntax));
        out["verbs"] = l;
    } else if (verb == "status") {
        reply = _status().dump();
        return true;
    } else if (verb == "caps") {
        reply = _capabilities().dump();
        return true;
    } else if (verb == "channel")   done = _doChannel(a, out, why);
    else if (verb == "timebase")    done = _doTimebase(a, out, why);
    else if (verb == "trigger")     done = _doTrigger(a, out, why);
    else if (verb == "run" || verb == "stop" || verb == "single" || verb == "force" ||
             verb == "autoset" || verb == "toggle") done = _doAcquire(verb, out, why);
    else if (verb == "mode") {
        Args t = { "timebase", "mode=" + (a.size() > 1 ? a[1] : std::string()) };
        done = _doTimebase(t, out, why);
    } else if (verb == "options" || verb == "option") done = _doOption(a, out, why);
    else if (verb == "generator")  done = _doGenerator(a, out, why);
    else if (verb == "measure")    done = _doMeasure(a, out, why);
    else if (verb == "edges")      done = _doEdges(a, out, why);
    else if (verb == "frame")      done = _doFrame(a, out, why);
    else if (verb == "record")     done = _doRecord(a, out, why);
    else if (verb == "replay")     done = _doReplay(a, out, why);
    else if (verb == "export") {
        done = hook(static_cast<bool>(m_hooks.exportCsv), "export");
        if (done) {
            std::string path = a.size() > 1 ? a[1] : std::string();
            if (path.empty() && m_hooks.captureDir)
                path = m_hooks.captureDir() + "/export_" +
                       std::to_string(static_cast<long long>(m_session.framesReceived())) + ".csv";
            done = m_hooks.exportCsv(path);
            if (done) out["path"] = JJson(path); else why = "cannot export to " + path;
        }
    } else if (verb == "capture") {
        if (a.size() < 3 || lower(a[1]) != "open") { why = "capture open <path.jscope>"; done = false; }
        else if ((done = hook(static_cast<bool>(m_hooks.openCapture), "capture open"))) {
            done = m_hooks.openCapture(a[2]);
            if (done) out["path"] = JJson(a[2]); else why = "cannot open " + a[2];
        }
    } else if (verb == "devices") {
        if ((done = hook(static_cast<bool>(m_hooks.devices), "devices"))) out["devices"] = m_hooks.devices();
    } else if (verb == "open") {
        if (a.size() < 2) { why = "open <index|driverId>"; done = false; }
        else if ((done = hook(static_cast<bool>(m_hooks.openDevice), "open"))) {
            done = m_hooks.openDevice(a[1], why);
            if (done) out["note"] = JJson(std::string("connecting; status shows when it is open"));
        }
    } else if (verb == "cursors") {
        if ((done = hook(static_cast<bool>(m_hooks.cursors) && static_cast<bool>(m_hooks.setCursors), "cursors"))) {
            std::string rest;
            for (size_t i = 1; i < a.size(); ++i) rest += (i > 1 ? " " : "") + a[i];
            if (!rest.empty()) done = m_hooks.setCursors(rest, why);
            if (done) out["cursors"] = m_hooks.cursors();
        }
    } else if (verb == "view") {
        if ((done = hook(static_cast<bool>(m_hooks.view) && static_cast<bool>(m_hooks.setView), "view"))) {
            std::string rest;
            for (size_t i = 1; i < a.size(); ++i) rest += (i > 1 ? " " : "") + a[i];
            if (!rest.empty()) done = m_hooks.setView(rest, why);
            if (done) out["view"] = m_hooks.view();
        }
    } else if (verb == "docks") {
        if ((done = hook(static_cast<bool>(m_hooks.docks), "docks"))) out["docks"] = m_hooks.docks();
    } else if (verb == "dock") {
        bool on = false;
        if (a.size() < 3 || !parseBool(a.back(), on)) { why = "dock <title> on|off"; done = false; }
        else if ((done = hook(static_cast<bool>(m_hooks.setDock), "dock"))) {
            std::string title;
            for (size_t i = 1; i + 1 < a.size(); ++i) title += (i > 1 ? " " : "") + a[i];
            done = m_hooks.setDock(title, on);
            if (!done) why = "no dock called '" + title + "'";
            else if (m_hooks.docks) out["docks"] = m_hooks.docks();
        }
    } else if (verb == "screenshot") {
        if ((done = hook(static_cast<bool>(m_hooks.screenshot), "screenshot")))
            out["prefix"] = JJson(m_hooks.screenshot());
    } else if (verb == "update") {
        if ((done = hook(static_cast<bool>(m_hooks.checkUpdates), "update"))) m_hooks.checkUpdates();
    } else if (verb == "quit") {
        if ((done = hook(static_cast<bool>(m_hooks.quit), "quit"))) m_hooks.quit();
    } else {
        return false;
    }

    if (!done) {
        JLOGC(JScopeLog::kUi, JLogLevel::Info) << "bus: '" << line << "' refused: " << why;
        reply = failObj(why).dump();
        return true;
    }
    JLOGC(JScopeLog::kUi, JLogLevel::Debug) << "bus: " << line;
    reply = out.dump();
    return true;
}

} // inline namespace jf
