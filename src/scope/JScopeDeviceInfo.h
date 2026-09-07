// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>
#include <string>

// How a device is named to JScopeDriver::open(). Covers both a USB unit and a
// file-backed source, because JReplayDriver is opened the same way everything
// else is — that is what makes the whole UI testable with nothing plugged in.

inline namespace jf {

struct JScopeDeviceInfo {
    std::string driverId;        // which driver claims it
    std::string displayName;     // what the device dialog shows

    // USB identity. portPath ("1-2.1") is the stable handle for reopening the
    // same physical unit, since a bus address changes on every replug.
    uint16_t    vendorId{0};
    uint16_t    productId{0};
    uint8_t     busNumber{0};
    uint8_t     deviceAddress{0};
    std::string portPath;
    std::string serialNumber;

    // NOT REAL HARDWARE — a generated source or a file being replayed.
    //
    // Said by the driver that enumerated it, because the application has no
    // business knowing driver names. It used to order candidates by testing
    // driverId against "synthetic" and "replay", which meant adding a simulated
    // source anywhere else would silently be treated as an instrument, and the
    // app knew things about specific drivers that the interface is there to
    // keep out of it.
    bool simulated{false};

    // File-backed sources (replay).
    std::string path;
};

} // inline namespace jf
