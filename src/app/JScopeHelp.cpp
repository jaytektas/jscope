// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JScopeHelp.h"

#include "JScopeShortcuts.h"

#include <algorithm>

inline namespace jf {

std::string JScopeHelp::shortcuts() {
    // Padded to the widest key so the descriptions line up without a table widget.
    size_t width = 0;
    for (const JScopeShortcut& s : JScopeShortcuts::table())
        width = std::max(width, std::string(s.name).size());

    std::string out;
    for (const JScopeShortcut& s : JScopeShortcuts::table()) {
        std::string key = s.name;
        key.resize(width, ' ');
        out += key + "   " + s.description + "\n";
    }
    return out;
}

std::vector<JScopeHelp::JTopic> JScopeHelp::topics() {
    return {
        { "The 1008C itself",
          "This scope has no front panel and no controls. Everything about its\n"
          "setup lives in this application, and the instrument remembers none of\n"
          "it: unplug it and every channel, timebase and trigger setting is gone.\n"
          "That is why connecting pushes a configuration rather than reading one.\n"
          "\n"
          "It also watches the host. If this application stops talking to it, the\n"
          "scope leaves the USB bus and re-enumerates, which looks exactly like\n"
          "somebody pulling the cable out. A keep-alive runs whenever acquisition\n"
          "is not, which is why the connection survives a single shot.\n"
          "\n"
          "It is a full-speed device: 12 Mbit/s, 64 bytes at a time, one request\n"
          "and one answer. That ceiling, not the software, is what sets the frame\n"
          "rate on long records." },

        { "Sweeps and triggering",
          "Auto sweeps whether or not a trigger arrives, so there is always a\n"
          "trace. Normal waits for an edge and shows nothing until it gets one --\n"
          "a blank screen in Normal usually means the level is outside the signal.\n"
          "Single captures one frame and stops.\n"
          "\n"
          "The trigger itself is in hardware; the sweep modes are this\n"
          "application's policy about how long to wait for it.\n"
          "\n"
          "The word at the top left of the graticule is what the instrument is\n"
          "doing NOW, not what it is configured for: Stopped means stopped, even\n"
          "if the sweep mode still says Auto." },

        { "Probes and clamps",
          "The probe setting scales what is measured. It changes no hardware --\n"
          "this instrument has no probe circuitry -- so it must match the probe\n"
          "actually on the lead, and nothing can check that for you. A x10 probe\n"
          "read as x1 gives a trace that looks entirely reasonable and is out by\n"
          "a factor of ten.\n"
          "\n"
          "Current clamps are the same idea with different units: the clamp's\n"
          "amps-per-volt ratio turns a voltage the scope can see into the current\n"
          "it stands for." },

        { "The pulse generator",
          "Eight digital outputs playing a repeating pattern. It is a crank and\n"
          "cam simulator, not a function generator: there is no amplitude and no\n"
          "sine, only which of the eight lines are high at each step.\n"
          "\n"
          "The pattern spans 0 to 720 degrees -- two crank revolutions, one\n"
          "four-stroke cycle -- and the pulse count divides that span. An event's\n"
          "angle IS which pulse is set high, so placing one at 450 degrees means\n"
          "setting the pulse at 450/720 of the way along.\n"
          "\n"
          "EDITING DOES NOT SEND. Click cells to draw the pattern, then\n"
          "Generator > Download to Device. That is deliberate: a full pattern is\n"
          "24 USB commands, and sending them on every click would put a burst of\n"
          "traffic behind each stroke of the pencil.\n"
          "\n"
          "The maximum speed falls as the pattern gets longer, because the device\n"
          "has a minimum time per step. The panel shows that ceiling beside the\n"
          "speed, and it moves when the pulse count does.\n"
          "\n"
          "Patterns load and save as .squ files, the same ones the OEM software\n"
          "reads and writes." },

        { "When something looks wrong",
          "A trace that is a plausible shape but the wrong size is almost always\n"
          "the probe setting rather than the scope.\n"
          "\n"
          "A blank screen in Normal sweep means no trigger: check the level is\n"
          "inside the signal, and the source is the channel the signal is on.\n"
          "\n"
          "If the device disappears mid-session, the log says whether it was a\n"
          "protocol error or a USB disconnect -- they look identical on screen and\n"
          "have nothing to do with each other. Logs are under\n"
          "~/.local/share/jscope/logs, and a crash leaves a CRASH-*.log there with\n"
          "a backtrace." },
    };
}

} // inline namespace jf
