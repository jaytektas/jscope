// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JScopeTriggerMode.h"
#include "JScopeTriggerSlope.h"
#include <cstdint>

inline namespace jf {

struct JScopeTriggerConfig {
    JScopeTriggerMode  mode{JScopeTriggerMode::Auto};
    JScopeTriggerSlope slope{JScopeTriggerSlope::Rising};
    uint8_t            sourceChannel{0};
    double             levelVolts{0.0};

    // How long Auto waits for a trigger before sweeping regardless. Host-side
    // policy, so it applies even to a device whose hardware has no Auto mode.
    double             autoTimeoutSeconds{0.1};
};

} // inline namespace jf
