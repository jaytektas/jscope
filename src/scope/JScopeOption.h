#pragma once

#include <string>
#include <vector>

// A SETTING THE INSTRUMENT ITSELF HAS, described well enough for the application
// to build a menu for it without knowing which scope it is talking to.
//
// The two instruments here are not alike. The DSO2D15 is a bench scope with an
// acquisition mode, a display type, a trigger coupling and a holdoff, all of
// which it will report and accept; the 1008C has none of them, because it has no
// front panel and no firmware notion of any of it. Hard-coding either one's list
// into the shell would put one instrument's vocabulary in front of the other's
// user, which is the mistake the split was made to end.
//
// So a driver publishes what its instrument actually has, and the menu is built
// from that. A driver that publishes nothing gets no menu, which is the honest
// outcome for a device with nothing to offer.
//
// Values are the instrument's own spellings, passed back unaltered. They are
// what it answered when asked, so they are what it will accept when told.

inline namespace jf {

struct JScopeOption {
    // Opaque to the application: it is handed back verbatim to
    // setInstrumentOption. For a SCPI instrument this is the command root, but
    // nothing above the driver may assume that.
    std::string id;

    std::string label;                  // what the menu shows, e.g. "Acquisition"
    std::vector<std::string> values;    // the choices, in the instrument's words
    std::string current;                // which of them it is set to now
};

} // inline namespace jf
