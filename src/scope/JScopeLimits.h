// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstddef>
#include <cstdint>

// Compile-time capacities for the scope HAL. These are the largest a frame or a
// device may be, not what any particular device provides — a driver reports its
// real numbers through JScopeCapabilities. They exist so that frame storage can
// be a fixed-size array and the acquisition thread never allocates.

inline namespace jf {

struct JScopeLimits {
    // The Hantek 1008C has 8; nothing on the bench has more. Raising this costs
    // only header space in JScopeFrameHeader.
    static constexpr uint8_t kMaxChannels = 8;

    // Guard rail on pool provisioning: a DSO deep-memory record is millions of
    // samples per channel, and a pool of them can exhaust RAM. JScopeFramePool
    // refuses to provision beyond this without an explicit budget override.
    static constexpr size_t kDefaultPoolBudgetBytes = 256u * 1024u * 1024u;

    // Ring depth. Four is enough to absorb a stalled compositor frame without
    // making a deep-memory pool enormous.
    static constexpr size_t kDefaultPoolDepth = 4;
};

} // inline namespace jf
