// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>

// Input coupling for one channel. A device that cannot select coupling reports
// an empty list in JScopeCapabilities::JChannelCaps::couplings, and the UI hides
// the control rather than offering a choice that does nothing.

inline namespace jf {

enum class JScopeCoupling : uint8_t { DC, AC, Ground };

inline const char* jScopeCouplingName(JScopeCoupling c) {
    switch (c) {
        case JScopeCoupling::DC:     return "DC";
        case JScopeCoupling::AC:     return "AC";
        case JScopeCoupling::Ground: return "GND";
    }
    return "DC";
}

} // inline namespace jf
