#include "JUsbContext.h"

#include "scope/JScopeLog.h"

#include <libusb-1.0/libusb.h>

#include <cstdio>
#include <fstream>
#include <functional>
#include <string>

inline namespace jf {

namespace {

// The physical port path, e.g. "1-2.1" — bus number, then the chain of hub port
// numbers. Stable across replug in a way a bus address is not.
std::string portPathOf(libusb_device* dev) {
    uint8_t ports[8]{};
    const int n = libusb_get_port_numbers(dev, ports, sizeof ports);
    std::string path = std::to_string(libusb_get_bus_number(dev));
    for (int i = 0; i < n; ++i)
        path += (i == 0 ? "-" : ".") + std::to_string(ports[i]);
    return path;
}

// The kernel already knows every device's strings, and it will hand them over
// without the device being opened at all.
//
// This is why `lsusb` prints the whole bus instantly while this code took three
// seconds: lsusb reads sysfs and opens nothing — only `lsusb -v` opens devices.
// Naming a device by opening it is not merely slow, it is intrusive: it means
// control transfers to an instrument another process may be using, and a wedged
// one answers none of them.
//
// The directory is named by the port path, which is what portPathOf already
// builds. Absent (not Linux, or a device with no sysfs entry) returns empty and
// the caller falls back to asking the device itself.
std::string sysfsAttribute(const std::string& portPath, const char* attribute) {
    if (portPath.empty()) return {};
    std::ifstream f("/sys/bus/usb/devices/" + portPath + "/" + attribute);
    if (!f) return {};
    std::string v;
    std::getline(f, v);
    return v;
}

// How long a descriptor read is given before the device is written off.
//
// This is the whole startup cost. libusb's own helper hardcodes a ONE SECOND
// timeout, and a wedged instrument answers none of the three reads, so naming a
// single unresponsive device cost exactly 3.000s — measured, and it was the
// entire delay in front of the window. A healthy device answers a descriptor in
// microseconds, so a tenth of a second is already enormously generous.
constexpr unsigned kDescriptorTimeoutMs = 100;
constexpr uint16_t kLangIdEnglishUs     = 0x0409;

// Reading a string descriptor needs the device OPEN, and opening it can fail on
// permissions. That is not a reason to omit the device from the list — the whole
// point of enumerating is to show the user what is there and why they cannot
// reach it — so a failure leaves the string empty and enumeration continues.
//
// `answering` latches false the first time the device misses its deadline, so an
// instrument that has stopped talking costs one timeout rather than one per
// string. Written against libusb_control_transfer rather than
// libusb_get_string_descriptor_ascii because that helper gives no way to pass a
// timeout.
std::string stringDescriptor(libusb_device_handle* h, uint8_t index, bool& answering) {
    if (!h || index == 0 || !answering) return {};

    unsigned char buf[256]{};
    const int n = libusb_control_transfer(
        h, LIBUSB_ENDPOINT_IN, LIBUSB_REQUEST_GET_DESCRIPTOR,
        static_cast<uint16_t>((LIBUSB_DT_STRING << 8) | index),
        kLangIdEnglishUs, buf, sizeof buf, kDescriptorTimeoutMs);
    if (n < 2) { answering = false; return {}; }

    // bLength, bDescriptorType, then UTF-16LE. The low byte of each unit is the
    // ASCII character, which is what libusb's own helper reduces it to.
    std::string out;
    for (int i = 2; i + 1 < n; i += 2)
        if (buf[i] != 0) out.push_back(static_cast<char>(buf[i]));
    return out;
}

} // namespace

struct JUsbContext::Impl {
    libusb_context* ctx{nullptr};
    std::string     lastError;
    bool            valid{false};
};

JUsbContext::JUsbContext() : m_impl(std::make_unique<Impl>()) {
    const int rc = libusb_init(&m_impl->ctx);
    if (rc != 0) {
        m_impl->lastError = libusb_strerror(static_cast<libusb_error>(rc));
        JLOGC(JScopeLog::kUsb, JLogLevel::Error)
            << "libusb_init failed: " << m_impl->lastError;
        return;
    }
    m_impl->valid = true;
    JLOGC(JScopeLog::kUsb, JLogLevel::Info) << "libusb initialised";
}

JUsbContext::~JUsbContext() {
    if (m_impl->ctx) libusb_exit(m_impl->ctx);
}

JUsbContext& JUsbContext::instance() {
    static JUsbContext inst;
    return inst;
}

bool JUsbContext::isValid() const { return m_impl->valid; }
const std::string& JUsbContext::lastError() const { return m_impl->lastError; }
void* JUsbContext::nativeContext() const { return m_impl->ctx; }

namespace {

// One walk of the bus. `match` decides which devices are wanted, and ONLY those
// are opened.
//
// That distinction is the whole point. Reading manufacturer/product/serial means
// control transfers, which means the device has to be opened, and a device that
// is busy or wedged makes each of those sit out its timeout. Opening every
// device on the bus in order to name ones that are about to be discarded cost
// three seconds a scan on this machine — and since a scan was run per VID/PID
// pair and the whole enumeration was run twice, that was FOUR of them, twelve
// seconds, in front of the window appearing.
std::vector<JUsbDeviceInfo> walkBus(libusb_context* ctx, std::string& lastError,
                                    const std::function<bool(uint16_t, uint16_t)>& match) {
    std::vector<JUsbDeviceInfo> out;

    libusb_device** list = nullptr;
    const ssize_t count = libusb_get_device_list(ctx, &list);
    if (count < 0) {
        lastError = libusb_strerror(static_cast<libusb_error>(count));
        JLOGC(JScopeLog::kUsb, JLogLevel::Error)
            << "libusb_get_device_list failed: " << lastError;
        return out;
    }

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device* dev = list[i];
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(dev, &desc) != 0) continue;
        if (!match(desc.idVendor, desc.idProduct)) continue;

        JUsbDeviceInfo info;
        info.vendorId      = desc.idVendor;
        info.productId     = desc.idProduct;
        info.busNumber     = libusb_get_bus_number(dev);
        info.deviceAddress = libusb_get_device_address(dev);
        info.portPath      = portPathOf(dev);

        // Ask the kernel first. It costs a file read, it works on a device that
        // is busy or has stopped answering, and it touches nothing.
        info.manufacturer = sysfsAttribute(info.portPath, "manufacturer");
        info.product      = sysfsAttribute(info.portPath, "product");
        info.serialNumber = sysfsAttribute(info.portPath, "serial");

        // Only where sysfs had nothing to say — another platform, or a device it
        // does not describe — is the device itself asked, and then on a short
        // leash.
        if (info.manufacturer.empty() && info.product.empty() && info.serialNumber.empty()) {
            libusb_device_handle* h = nullptr;
            if (libusb_open(dev, &h) == 0) {
                bool answering = true;
                info.manufacturer = stringDescriptor(h, desc.iManufacturer, answering);
                info.product      = stringDescriptor(h, desc.iProduct,      answering);
                info.serialNumber = stringDescriptor(h, desc.iSerialNumber, answering);
                if (!answering)
                    JLOGC(JScopeLog::kUsb, JLogLevel::Debug)
                        << "device at " << info.portPath << " did not answer a descriptor read;"
                        << " listing it unnamed rather than waiting on it";
                libusb_close(h);
            }
        }
        out.push_back(std::move(info));
    }

    libusb_free_device_list(list, 1);
    return out;
}

} // namespace

std::vector<JUsbDeviceInfo> JUsbContext::enumerateAll() const {
    if (!m_impl->valid) return {};
    auto out = walkBus(m_impl->ctx, m_impl->lastError,
                       [](uint16_t, uint16_t) { return true; });
    JLOGC(JScopeLog::kUsb, JLogLevel::Debug) << "enumerated " << out.size() << " USB device(s)";
    return out;
}

std::vector<JUsbDeviceInfo> JUsbContext::enumerate(uint16_t vendorId,
                                                   uint16_t productId) const {
    if (!m_impl->valid) return {};
    auto out = walkBus(m_impl->ctx, m_impl->lastError,
                       [vendorId, productId](uint16_t v, uint16_t p) {
                           return v == vendorId && p == productId;
                       });

    if (!out.empty()) {
        char id[16];
        std::snprintf(id, sizeof id, "%04x:%04x", vendorId, productId);
        JLOGC(JScopeLog::kUsb, JLogLevel::Info)
            << "found " << out.size() << " device(s) matching " << id;
    }
    return out;
}

} // inline namespace jf
