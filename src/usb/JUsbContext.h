#pragma once

#include "JUsbDeviceInfo.h"

#include <cstdint>
#include <memory>
#include <vector>

inline namespace jf {

// Owns the libusb context and enumerates devices.
//
// libusb.h appears nowhere in this header — it lives entirely in the .cpp behind
// an opaque Impl, the same discipline JSerialPort applies to termios. That is
// what keeps this subsystem promotable into the framework's j/io, where a public
// header carrying a platform type would be a rule violation.
class JUsbContext {
public:
    static JUsbContext& instance();

    JUsbContext(const JUsbContext&)            = delete;
    JUsbContext& operator=(const JUsbContext&) = delete;

    bool isValid() const;

    // Every device on the bus, or only those matching a VID/PID. Returns an
    // EMPTY vector when nothing matches — a normal result on a bench with
    // nothing plugged in, and never an error.
    std::vector<JUsbDeviceInfo> enumerateAll() const;
    std::vector<JUsbDeviceInfo> enumerate(uint16_t vendorId, uint16_t productId) const;

    const std::string& lastError() const;

    // Opaque handle so JUsbDevice can reach the context without either of them
    // exposing libusb.
    void* nativeContext() const;

private:
    JUsbContext();
    ~JUsbContext();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // inline namespace jf
