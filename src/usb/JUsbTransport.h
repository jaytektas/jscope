// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

inline namespace jf {

// The smallest thing a protocol needs from a USB connection.
//
// This seam exists so that JHantek1008Protocol can be tested BYTE FOR BYTE
// against a recorded conversation with no hardware attached. That matters more
// than usual here: the 1008C's initialisation is vendor magic with no
// documentation, ported from a Python reference, and the only way to know the
// port is faithful is to compare the bytes it emits against the bytes the
// reference emits. A test that needs the device present could not do that.
class JUsbTransport {
public:
    virtual ~JUsbTransport() = default;

    // Returns false on failure; lastError() says why. Blocking, with a timeout.
    virtual bool bulkOut(uint8_t endpoint, const uint8_t* data, size_t length,
                         unsigned timeoutMs) = 0;

    // Returns the number of bytes read, or -1 on failure. A short read is not a
    // failure: a device may legitimately return less than was asked for.
    virtual int bulkIn(uint8_t endpoint, uint8_t* data, size_t maxLength,
                       unsigned timeoutMs) = 0;

    virtual const std::string& lastError() const = 0;
};

} // inline namespace jf
