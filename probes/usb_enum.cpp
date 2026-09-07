// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Lists what is on the USB bus and whether this process can actually claim it.
//
// A bench tool, not a test: it needs hardware, so it is EXCLUDE_FROM_ALL and is
// never registered with ctest. Build and run it with
//     cmake --build build --target usb_enum && ./build/usb_enum
//
// The permission line is the point. "Device present but cannot be opened" is by
// far the most common first failure with libusb, and it means the device is not
// reachable by this process — not that the driver is broken. On Linux that is a
// missing udev rule; on Windows it is a device with no WinUSB binding.
//
// Run it with --all first. "Listed by --all but cannot be opened" and "not
// listed at all" have completely different causes, and only the first is a
// permissions or driver question.

#include "usb/JUsbContext.h"
#include "usb/JUsbDevice.h"

#include <j/core/Log.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace jf;

namespace {

// The two instruments this application is for.
struct JKnownDevice { uint16_t vid, pid; const char* name; int interfaceNumber; };
constexpr JKnownDevice kKnown[] = {
    { 0x0783, 0x5725, "Hantek 1008C (vendor-specific bulk)", 0 },
    { 0x049f, 0x505e, "Hantek DSO2D15 (USBTMC)",             0 },
};

} // namespace

int main(int argc, char** argv) {
    const bool all = (argc > 1 && std::strcmp(argv[1], "--all") == 0);
    JLog::instance().setGlobalLevel(JLogLevel::Info);
    if (argc > 1 && std::strcmp(argv[1], "--trace") == 0)
        JLog::instance().setLevel("usb.*", JLogLevel::Trace);

    JUsbContext& ctx = JUsbContext::instance();
    if (!ctx.isValid()) {
        JLOGC("probe", JLogLevel::Error) << "libusb failed to initialise: " << ctx.lastError();
        return 1;
    }

    if (all) {
        JLOGC("probe", JLogLevel::Info) << "--- every device on the bus ---";
        for (const JUsbDeviceInfo& d : ctx.enumerateAll()) {
            char id[16];
            std::snprintf(id, sizeof id, "%04x:%04x", d.vendorId, d.productId);
            JLOGC("probe", JLogLevel::Info)
                << "  " << id << "  " << d.portPath
                << "  " << (d.product.empty() ? "(no product string)" : d.product)
                << (d.serialNumber.empty() ? "" : ("  sn=" + d.serialNumber));
        }
    }

    JLOGC("probe", JLogLevel::Info) << "--- instruments this build knows ---";
    int found = 0;
    for (const JKnownDevice& k : kKnown) {
        const auto matches = ctx.enumerate(k.vid, k.pid);
        char id[16];
        std::snprintf(id, sizeof id, "%04x:%04x", k.vid, k.pid);

        if (matches.empty()) {
            JLOGC("probe", JLogLevel::Info) << "  " << id << "  " << k.name << ": not present";
            continue;
        }
        ++found;
        for (const JUsbDeviceInfo& d : matches) {
            JLOGC("probe", JLogLevel::Info)
                << "  " << id << "  " << k.name << "  at " << d.portPath
                << (d.serialNumber.empty() ? "" : ("  sn=" + d.serialNumber));

            // Opening and claiming is the check that matters. Being visible on
            // the bus says nothing about being usable.
            JUsbDevice dev;
            if (!dev.open(d)) {
                JLOGC("probe", JLogLevel::Warn) << "      cannot open: " << dev.lastError();
                continue;
            }
            if (!dev.claimInterface(k.interfaceNumber)) {
                JLOGC("probe", JLogLevel::Warn)
                    << "      opened, but cannot claim interface " << k.interfaceNumber
                    << ": " << dev.lastError();
                dev.close();
                continue;
            }
            JLOGC("probe", JLogLevel::Info)
                << "      CLAIMED  bulk IN 0x" << std::hex << int(dev.bulkInEndpoint())
                << " OUT 0x" << int(dev.bulkOutEndpoint()) << std::dec
                << "  max packet " << dev.maxPacketSize();
            dev.close();
        }
    }

    // The remedy differs by platform, and naming the wrong one sends someone
    // hunting for a file that does not exist on their machine.
    if (!found)
        JLOGC("probe", JLogLevel::Info)
#if defined(_WIN32)
            << "no known instruments found — check the cable, and that WinUSB is bound to the "
               "device (Zadig). Windows will not let libusb near a device with no driver, and the "
               "1008C has no class driver of its own.";
#else
            << "no known instruments found — check the cables and the udev rules";
#endif
    return 0;
}
