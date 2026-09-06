#pragma once

#include <cstdint>
#include <string>

inline namespace jf {

// One enumerated USB device. Mirrors JSerialPortInfo deliberately: this whole
// subsystem is shaped to be promotable into the framework's j/io alongside it.
struct JUsbDeviceInfo {
    uint16_t    vendorId{0};
    uint16_t    productId{0};
    uint8_t     busNumber{0};
    uint8_t     deviceAddress{0};

    // "1-2.1" — the physical port path. The stable handle for reopening the SAME
    // unit, because a bus address changes on every replug and a serial number is
    // not always present or unique. Both scopes on this bench are identified by
    // VID/PID, and the 1008C's product string says "YDJ-2088" with no mention of
    // Hantek at all, so matching on strings is not an option.
    std::string portPath;

    std::string manufacturer;
    std::string product;
    std::string serialNumber;

    bool operator==(const JUsbDeviceInfo& o) const {
        return vendorId == o.vendorId && productId == o.productId &&
               portPath == o.portPath;
    }
};

} // inline namespace jf
