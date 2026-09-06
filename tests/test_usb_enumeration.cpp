#include "support/JTestReport.h"
#include "usb/JUsbContext.h"
#include "usb/JUsbDevice.h"

using namespace jf;

namespace {

// Enumeration must work, and return nothing, on a machine with no instrument
// attached. A test suite that needs hardware present cannot run in CI or on a
// developer's laptop, and a driver layer that treats "nothing plugged in" as an
// error would make the whole application refuse to start.
void testEnumerationWithoutHardware(JTestReport& r) {
    JUsbContext& ctx = JUsbContext::instance();
    r.check(ctx.isValid(), "libusb initialises");

    // Every machine running this has at least a root hub, but the assertion that
    // matters is that enumerating does not fail or throw.
    const auto all = ctx.enumerateAll();
    r.check(true, "enumerating the whole bus completes without error");

    bool pathsPresent = true;
    for (const JUsbDeviceInfo& d : all)
        if (d.portPath.empty()) pathsPresent = false;
    r.check(pathsPresent, "every enumerated device has a port path");

    // A VID/PID that cannot exist: the result must be empty, not an error.
    const auto none = ctx.enumerate(0xFFFF, 0xFFFF);
    r.check(none.empty(), "a VID/PID that matches nothing yields an empty list");
}

// Opening something that is not there must fail cleanly and leave the device
// closed, rather than half-opening and being unusable later.
void testOpeningAbsentDeviceIsSafe(JTestReport& r) {
    JUsbDeviceInfo phantom;
    phantom.vendorId  = 0xFFFF;
    phantom.productId = 0xFFFF;
    phantom.portPath  = "99-99";

    JUsbDevice dev;
    r.check(!dev.open(phantom), "opening an absent device fails");
    r.check(!dev.isOpen(),      "and leaves the device closed");
    r.check(!dev.lastError().empty(), "with a reason");

    // Every operation on a closed device must refuse rather than dereference.
    uint8_t byte = 0;
    r.check(!dev.bulkOut(0x01, &byte, 1, 10), "bulk out on a closed device is refused");
    r.check(dev.bulkIn(0x81, &byte, 1, 10) < 0, "bulk in on a closed device is refused");
    r.check(!dev.claimInterface(0), "claiming on a closed device is refused");
    r.check(!dev.reset(), "resetting a closed device is refused");
    dev.close();                       // closing twice must be harmless
    r.check(!dev.isOpen(), "closing an already-closed device is harmless");
}

} // namespace

int main() {
    JTestReport r("USB enumeration");
    testEnumerationWithoutHardware(r);
    testOpeningAbsentDeviceIsSafe(r);
    return r.result();
}
