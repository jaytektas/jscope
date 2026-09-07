// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "scope/JScopeCapabilities.h"
#include "scope/JScopeChannelConfig.h"
#include "scope/JScopeTimebaseConfig.h"
#include "scope/JScopeTriggerConfig.h"

#include <string>
#include <vector>

inline namespace jf {

// What a capture records about the instrument that produced it.
//
// Enough for JReplayDriver to reconstruct a believable JScopeCapabilities, so
// replaying a 1008C capture shows the 1008C's knobs rather than a generic set —
// and enough for the numbers to still mean something a year later.
struct JCaptureMeta {
    std::string driverId;
    std::string model;
    std::string serialNumber;
    std::string operatorNote;

    // Wall clock at the start of the recording, as an ISO-8601 string. The frame
    // timestamps are a steady clock and say nothing about when this happened.
    std::string startedAtUtc;

    uint8_t  channelCount{0};
    uint8_t  adcBits{0};
    int32_t  countsMin{0};
    int32_t  countsMax{0};
    uint8_t  verticalDivisions{8};
    uint8_t  horizontalDivisions{10};

    JScopeTimebaseConfig             timebase;
    JScopeTriggerConfig              trigger;
    std::vector<JScopeChannelConfig> channels;
    std::vector<std::string>         channelLabels;

    // Per-channel V/div steps, so the replay driver can offer the same choices
    // the original device did rather than inventing a range.
    std::vector<std::vector<double>> voltsPerDivSteps;
};

} // inline namespace jf
