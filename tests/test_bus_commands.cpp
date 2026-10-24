// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The AI bus's verbs against the synthetic driver: every instrument setting the
// window has is reachable as a line of text, the reply carries the read-back,
// a refusal says why, and measurements and edges come off a real frame.

#include "app/JScopeActions.h"
#include "app/JScopeBusCommands.h"
#include "scope/JScopeDriver.h"
#include "scope/JScopeSession.h"
#include "support/JTestReport.h"

#include <j/config/Json.h>
#include <j/core/MainThreadDispatcher.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>

using namespace jf;

namespace {

struct JFixture {
    JScopeSession     session;
    JScopeActions     actions{session};
    JScopeBusCommands bus{session, actions};

    bool open() {
        JScopeDeviceInfo info;
        info.driverId    = "synthetic";
        info.displayName = "Synthetic source";
        return session.open(info);
    }
    JJson call(const std::string& line) {
        std::string reply;
        if (!bus.handle(line, reply)) return JJson(nullptr);
        return JJson::parse(reply);
    }
    // No render loop here, so the frames are pulled by hand: the dispatcher for
    // the driver's wake, the session for the drain.
    bool waitForFrame() {
        const uint64_t before = session.framesReceived();
        for (int i = 0; i < 2000; ++i) {
            JMainThreadDispatcher::instance().drain();
            session.drain();
            if (session.framesReceived() > before && session.latestFrame()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }
};

bool ok(const JJson& j) { return j.isObject() && j["ok"].isBool() && j["ok"].boolean(); }

void testStatusAndSettings(JTestReport& r) {
    JFixture f;
    r.check(f.open(), "the synthetic driver opens");

    const JJson help = f.call("help");
    r.check(ok(help) && help["verbs"].size() == JScopeBusCommands::verbs().size(), "help lists every verb");
    r.check(f.call("no-such-verb").isNull(), "an unknown verb is left to the caller");

    const JJson st = f.call("status");
    r.check(ok(st), "status answers");
    r.check(st["device"]["driver"].str() == "synthetic", "status names the driver");
    r.check(st["channels"].size() == f.session.driver()->capabilities().channelCount(),
            "status carries every channel");

    const JJson caps = f.call("caps");
    r.check(ok(caps) && caps["channels"].size() > 0, "caps lists the channels and their steps");

    const JJson ch = f.call("channel 2 vdiv=0.037 coupling=ac invert=on");
    r.check(ok(ch), "a channel edit is accepted");
    const double applied = ch["channel"]["vdiv"].number<double>();
    r.check(applied != 0.037 && applied == f.session.driver()->channelConfig(1).voltsPerDiv,
            "the reply is the read-back the driver quantised to, not the request");
    r.check(f.session.driver()->channelConfig(1).inverted, "invert reached the driver");
    r.check(ch["channel"]["coupling"].str() == "ac" ||
            f.session.driver()->capabilities().channels[1].couplings.empty(),
            "coupling reached the driver where it is selectable");

    const JJson bad = f.call("channel 9 on");
    r.check(!ok(bad) && !bad["error"].str().empty(), "a channel the instrument lacks is refused, with a reason");
    r.check(!ok(f.call("channel 1 vdiv=lots")), "a malformed value is refused, not guessed at");

    const JJson tb = f.call("timebase sdiv=0.002 pos=0.25");
    r.check(ok(tb), "timebase edits are accepted");
    r.check(tb["timebase"]["pos"].number<double>() == f.session.driver()->timebaseConfig().triggerPosition,
            "trigger position reads back");

    const JJson tr = f.call("trigger mode=normal slope=falling source=2 level=0.5 timeout=0.2");
    r.check(ok(tr), "trigger edits are accepted");
    r.check(f.session.driver()->triggerConfig().sourceChannel == 1, "source 2 is device channel 1");
    r.check(tr["trigger"]["source"].number<double>() == 2.0, "...and reads back 1-based, as the screen labels it");

    r.check(!ok(f.call("record start /tmp/x.jscope")), "a verb whose hook is absent says so");
}

void testAcquisitionAndFrames(JTestReport& r) {
    JFixture f;
    f.open();
    f.call("trigger mode=auto");
    const JJson run = f.call("run");
    r.check(ok(run), "run starts acquisition");
    r.check(f.waitForFrame(), "a frame arrives");

    const JJson fr = f.call("frame");
    r.check(ok(fr) && fr["samples"].number<double>() > 0, "frame describes the record");

    // CH2 of the synthetic source is a square wave: Freq and Duty are defined.
    const JJson m = f.call("measure 2");
    r.check(ok(m) && m["measurements"]["CH2"]["Vpp"].isNumber(), "measure gives Vpp on CH2");

    const JJson e = f.call("edges 2 max=50");
    r.check(ok(e), "edges answers");
    const JJson& c2 = e["channels"]["CH2"];
    const double rising = c2["rising"].number<double>(), falling = c2["falling"].number<double>();
    r.check(rising > 0 && falling > 0, "a square wave has rising and falling edges");
    r.check(c2["edges"].size() <= 50, "max= caps the inline list");
    r.check(c2["rising_interval"]["count"].number<double>() == rising - 1, "intervals are between successive edges");

    const std::string path = "/tmp/jscope_bus_test_edges.csv";
    const JJson ef = f.call("edges 2 polarity=rising file=" + path);
    std::ifstream in(path);
    std::string header;
    std::getline(in, header);
    r.check(ok(ef) && header == "channel,time_s,edge", "file= writes every edge as CSV");
    std::remove(path.c_str());

    const std::string csv = "/tmp/jscope_bus_test_frame.csv";
    r.check(ok(f.call("frame save " + csv)) && std::ifstream(csv).good(), "frame save writes the record");
    std::remove(csv.c_str());

    const JJson s = f.call("frame samples 2 0 10");
    r.check(ok(s) && s["volts"].size() == 10, "frame samples returns volts inline");

    r.check(ok(f.call("stop")), "stop answers");
    const JJson st = f.call("status");
    r.check(st["state"].str() == "stopped" || st["state"].str() == "idle", "status reports the stop");
}

void testNoDevice(JTestReport& r) {
    JScopeSession     session;
    JScopeActions     actions{session};
    JScopeBusCommands bus{session, actions};
    std::string reply;
    r.check(bus.handle("status", reply) && JJson::parse(reply)["state"].str() == "closed",
            "status with nothing open says so rather than failing");
    r.check(bus.handle("run", reply) && !JJson::parse(reply)["ok"].boolean(), "run with nothing open is refused");
    r.check(bus.handle("measure", reply) && !JJson::parse(reply)["ok"].boolean(), "measure with no frame is refused");
}

} // namespace

int main() {
    JTestReport r("JScopeBusCommands");
    testStatusAndSettings(r);
    testAcquisitionAndFrames(r);
    testNoDevice(r);
    return r.result();
}
