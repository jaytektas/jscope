#pragma once

#include "JScopeCoupling.h"
#include "JScopeGeneratorCapabilities.h"
#include <cstdint>
#include <string>
#include <vector>

// What a device can actually do. The UI builds itself from this — no panel
// hardcodes a device's abilities, and a control whose option list is empty (or
// whose flag is false) is disabled rather than offered. A driver that cannot do
// something says so here; it never emulates the feature behind the caller's
// back, and it never pretends.

inline namespace jf {

struct JScopeChannelCaps {
    std::string                 label;             // "CH1" … "CH8"
    std::vector<double>         voltsPerDiv;       // ascending, device-supported steps
    std::vector<double>         probeRatios;       // {1.0} when the probe is fixed
    std::vector<JScopeCoupling> couplings;         // empty => not selectable
    double                      offsetRangeVolts{0.0};   // +/- about centre; 0 => no offset control
    bool                        canInvert{false};
    bool                        canBandwidthLimit{false};
};

struct JScopeCapabilities {
    std::string driverId;
    std::string model;
    std::string serialNumber;

    std::vector<JScopeChannelCaps> channels;
    uint8_t  maxSimultaneousChannels{0};
    uint8_t  adcBits{0};
    int32_t  countsMin{0};                 // full-scale count range, inclusive
    int32_t  countsMax{0};

    // The graticule the device's volts/div and seconds/div are defined against.
    // Stated rather than assumed, so nothing downstream guesses at 8x10.
    uint8_t  verticalDivisions{8};
    uint8_t  horizontalDivisions{10};

    uint32_t acquisitionModes{0};          // bitmask, jScopeAcquisitionModeBit()
    std::vector<double>   secondsPerDiv;   // Windowed steps, ascending
    std::vector<double>   streamSampleRates;  // Streaming Sa/s/ch, ascending; empty => unsupported
    std::vector<uint32_t> memoryDepths;    // empty + deviceDeterminedRecordLength => device decides
    bool     deviceDeterminedRecordLength{false};

    uint32_t triggerModes{0};              // bitmask, jScopeTriggerModeBit()
    bool     hasHardwareTrigger{false};
    bool     hasTriggerPosition{false};
    bool     hasForceTrigger{false};
    bool     hasAutoset{false};

    // The INSTRUMENT owns its configuration — it has its own front panel and reports
    // what it is set to. The app then adopts that on connect instead of pushing
    // its own remembered setup at it.
    //
    // A bench scope is not a headless box: its knobs were set by whoever is
    // standing at it, and re-applying a stored volts/div flattened that. It put
    // a DSO2D15 at 50mV/div under a 10x probe, which is a screen full of noise.
    // The 1008C has no front panel at all, so for that one the stored setup is
    // the only setup there is.
    bool     deviceOwnsConfiguration{false};

    JScopeGeneratorCapabilities generator{};

    uint8_t channelCount() const { return static_cast<uint8_t>(channels.size()); }
};

} // inline namespace jf
