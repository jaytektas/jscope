#include "JUsbDevice.h"

#include "JUsbContext.h"
#include "scope/JScopeLog.h"

#include <libusb-1.0/libusb.h>

#include <cstring>
#include <string>

inline namespace jf {

struct JUsbDevice::Impl {
    libusb_device_handle* handle{nullptr};
    JUsbDeviceInfo        info;
    std::string           lastError;
    int                   claimedInterface{-1};
    uint8_t               epIn{0};
    uint8_t               epOut{0};
    uint16_t              maxPacket{64};

    void fail(const char* what, int rc) {
        lastError = std::string(what) + ": " + libusb_strerror(static_cast<libusb_error>(rc));
        JLOGC(JScopeLog::kUsb, JLogLevel::Error) << lastError;
    }
};

JUsbDevice::JUsbDevice() : m_impl(std::make_unique<Impl>()) {}
JUsbDevice::~JUsbDevice() { close(); }

bool JUsbDevice::open(const JUsbDeviceInfo& info) {
    close();

    auto* ctx = static_cast<libusb_context*>(JUsbContext::instance().nativeContext());
    if (!ctx) {
        m_impl->lastError = "libusb is not initialised";
        return false;
    }

    libusb_device** list = nullptr;
    const ssize_t count = libusb_get_device_list(ctx, &list);
    if (count < 0) {
        m_impl->fail("libusb_get_device_list", static_cast<int>(count));
        return false;
    }

    // Match on the PORT PATH when there is one. Two identical scopes on one bench
    // have the same VID/PID, and a bus address changes on every replug — the port
    // path is the only handle that means "this physical unit".
    libusb_device* found = nullptr;
    for (ssize_t i = 0; i < count && !found; ++i) {
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
        if (desc.idVendor != info.vendorId || desc.idProduct != info.productId) continue;

        if (!info.portPath.empty()) {
            uint8_t ports[8]{};
            const int n = libusb_get_port_numbers(list[i], ports, sizeof ports);
            std::string path = std::to_string(libusb_get_bus_number(list[i]));
            for (int k = 0; k < n; ++k) path += (k == 0 ? "-" : ".") + std::to_string(ports[k]);
            if (path != info.portPath) continue;
        }
        found = list[i];
    }

    if (!found) {
        m_impl->lastError = "device not found on the bus";
        JLOGC(JScopeLog::kUsb, JLogLevel::Warn)
            << "cannot find " << info.portPath << " (" << info.product << ")";
        libusb_free_device_list(list, 1);
        return false;
    }

    const int rc = libusb_open(found, &m_impl->handle);
    libusb_free_device_list(list, 1);
    if (rc != 0) {
        m_impl->fail("libusb_open", rc);
        // The overwhelmingly common cause, and worth saying plainly rather than
        // leaving the user with "Access denied".
        if (rc == LIBUSB_ERROR_ACCESS)
            m_impl->lastError += " — a udev rule granting access to this VID/PID is needed";
        return false;
    }

    m_impl->info = info;
    JLOGC(JScopeLog::kUsb, JLogLevel::Info)
        << "opened " << info.portPath << " (" << info.product << ")";
    return true;
}

void JUsbDevice::close() {
    if (!m_impl->handle) return;
    releaseInterface();
    libusb_close(m_impl->handle);
    m_impl->handle = nullptr;
    JLOGC(JScopeLog::kUsb, JLogLevel::Info) << "closed " << m_impl->info.portPath;
}

bool JUsbDevice::isOpen() const { return m_impl->handle != nullptr; }

bool JUsbDevice::claimInterface(int interfaceNumber, bool autoDetachKernelDriver) {
    if (!m_impl->handle) { m_impl->lastError = "no device is open"; return false; }

    // The DSO2D15 is held by the kernel's usbtmc driver, so without this it
    // cannot be claimed at all. The 1008C is vendor-specific and unbound, so for
    // it this is a no-op.
    if (autoDetachKernelDriver)
        libusb_set_auto_detach_kernel_driver(m_impl->handle, 1);

    const int rc = libusb_claim_interface(m_impl->handle, interfaceNumber);
    if (rc != 0) {
        m_impl->fail("libusb_claim_interface", rc);
        if (rc == LIBUSB_ERROR_BUSY)
            m_impl->lastError += " — another program or a kernel driver holds this interface";
        return false;
    }
    m_impl->claimedInterface = interfaceNumber;

    // Discover the endpoints by DIRECTION. The 1008C's bulk OUT is 0x02 and the
    // DSO2D15's is 0x01; assuming either would work on one device and fail
    // silently on the other.
    m_impl->epIn = m_impl->epOut = 0;
    libusb_config_descriptor* cfg = nullptr;
    libusb_device* dev = libusb_get_device(m_impl->handle);
    if (libusb_get_active_config_descriptor(dev, &cfg) == 0 && cfg) {
        for (uint8_t i = 0; i < cfg->bNumInterfaces; ++i) {
            const libusb_interface& itf = cfg->interface[i];
            for (int a = 0; a < itf.num_altsetting; ++a) {
                const libusb_interface_descriptor& alt = itf.altsetting[a];
                if (alt.bInterfaceNumber != interfaceNumber) continue;
                for (uint8_t e = 0; e < alt.bNumEndpoints; ++e) {
                    const libusb_endpoint_descriptor& ep = alt.endpoint[e];
                    if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK)
                            != LIBUSB_TRANSFER_TYPE_BULK) continue;
                    if (ep.bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                        if (!m_impl->epIn) m_impl->epIn = ep.bEndpointAddress;
                    } else {
                        if (!m_impl->epOut) m_impl->epOut = ep.bEndpointAddress;
                    }
                    if (ep.wMaxPacketSize) m_impl->maxPacket = ep.wMaxPacketSize;
                }
            }
        }
        libusb_free_config_descriptor(cfg);
    }

    JLOGC(JScopeLog::kUsb, JLogLevel::Info)
        << "claimed interface " << interfaceNumber
        << ": bulk IN 0x" << std::hex << int(m_impl->epIn)
        << " OUT 0x" << int(m_impl->epOut) << std::dec
        << ", max packet " << m_impl->maxPacket;

    if (!m_impl->epIn || !m_impl->epOut) {
        m_impl->lastError = "the claimed interface has no bulk endpoint pair";
        JLOGC(JScopeLog::kUsb, JLogLevel::Error) << m_impl->lastError;
        return false;
    }
    return true;
}

void JUsbDevice::releaseInterface() {
    if (!m_impl->handle || m_impl->claimedInterface < 0) return;
    libusb_release_interface(m_impl->handle, m_impl->claimedInterface);
    m_impl->claimedInterface = -1;
}

bool JUsbDevice::reset() {
    if (!m_impl->handle) return false;
    const int rc = libusb_reset_device(m_impl->handle);
    if (rc != 0) { m_impl->fail("libusb_reset_device", rc); return false; }
    JLOGC(JScopeLog::kUsb, JLogLevel::Warn) << "device reset";
    return true;
}

bool JUsbDevice::bulkOut(uint8_t endpoint, const uint8_t* data, size_t length,
                         unsigned timeoutMs) {
    if (!m_impl->handle) { m_impl->lastError = "no device is open"; return false; }

    JLog::instance().hexDump(JLogLevel::Trace, JScopeLog::kUsbBulk, "[tx]", data, length);

    int transferred = 0;
    const int rc = libusb_bulk_transfer(m_impl->handle, endpoint,
                                        const_cast<unsigned char*>(data),
                                        static_cast<int>(length), &transferred,
                                        timeoutMs);
    if (rc != 0) { m_impl->fail("bulk out", rc); return false; }
    if (transferred != static_cast<int>(length)) {
        m_impl->lastError = "short bulk write";
        JLOGC(JScopeLog::kUsb, JLogLevel::Error)
            << "short bulk write: " << transferred << " of " << length << " bytes";
        return false;
    }
    return true;
}

int JUsbDevice::bulkIn(uint8_t endpoint, uint8_t* data, size_t maxLength,
                       unsigned timeoutMs) {
    if (!m_impl->handle) { m_impl->lastError = "no device is open"; return -1; }

    int transferred = 0;
    const int rc = libusb_bulk_transfer(m_impl->handle, endpoint, data,
                                        static_cast<int>(maxLength), &transferred,
                                        timeoutMs);
    // A timeout that still moved bytes is a short read, not a failure — a device
    // is allowed to answer with less than was asked for.
    if (rc != 0 && !(rc == LIBUSB_ERROR_TIMEOUT && transferred > 0)) {
        m_impl->fail("bulk in", rc);
        return -1;
    }

    JLog::instance().hexDump(JLogLevel::Trace, JScopeLog::kUsbBulk, "[rx]", data,
                             static_cast<size_t>(transferred));
    return transferred;
}

int JUsbDevice::controlTransfer(uint8_t requestType, uint8_t request, uint16_t value,
                                uint16_t index, uint8_t* data, uint16_t length,
                                unsigned timeoutMs) {
    if (!m_impl->handle) { m_impl->lastError = "no device is open"; return -1; }
    const int rc = libusb_control_transfer(m_impl->handle, requestType, request, value,
                                           index, data, length, timeoutMs);
    if (rc < 0) { m_impl->fail("control transfer", rc); return -1; }
    return rc;
}

bool JUsbDevice::clearHalt(uint8_t endpoint) {
    if (!m_impl->handle) return false;
    const int rc = libusb_clear_halt(m_impl->handle, endpoint);
    if (rc != 0) { m_impl->fail("libusb_clear_halt", rc); return false; }
    JLOGC(JScopeLog::kUsb, JLogLevel::Debug)
        << "cleared halt on endpoint 0x" << std::hex << int(endpoint) << std::dec;
    return true;
}

bool JUsbDevice::usbTmcClear(int interfaceNumber) {
    if (!m_impl->handle) return false;

    // USBTMC class requests, from the specification's table of them.
    constexpr uint8_t kClassInterfaceIn = 0xA1;   // device-to-host, class, interface
    constexpr uint8_t kInitiateClear     = 5;
    constexpr uint8_t kCheckClearStatus  = 6;
    constexpr uint8_t kStatusSuccess     = 0x01;
    constexpr uint8_t kStatusPending     = 0x02;
    constexpr int     kPollAttempts      = 10;
    constexpr unsigned kRequestTimeoutMs = 1000;

    uint8_t status = 0;
    if (controlTransfer(kClassInterfaceIn, kInitiateClear, 0,
                        static_cast<uint16_t>(interfaceNumber), &status, 1,
                        kRequestTimeoutMs) < 1) return false;
    if (status != kStatusSuccess) {
        JLOGC(JScopeLog::kUsbTmc, JLogLevel::Warn)
            << "INITIATE_CLEAR was refused (status 0x" << std::hex << int(status)
            << std::dec << ")";
        return false;
    }

    // The clear is asynchronous; poll until the device says it is done.
    for (int i = 0; i < kPollAttempts; ++i) {
        uint8_t reply[2]{};
        if (controlTransfer(kClassInterfaceIn, kCheckClearStatus, 0,
                            static_cast<uint16_t>(interfaceNumber), reply, 2,
                            kRequestTimeoutMs) < 2) return false;
        if (reply[0] != kStatusPending) break;
    }

    // The bulk OUT endpoint is left halted by the clear and has to be reset, or
    // the next write fails with a pipe error.
    clearHalt(m_impl->epOut);
    JLOGC(JScopeLog::kUsbTmc, JLogLevel::Debug) << "USBTMC clear complete";
    return true;
}

uint8_t  JUsbDevice::bulkInEndpoint() const  { return m_impl->epIn; }
uint8_t  JUsbDevice::bulkOutEndpoint() const { return m_impl->epOut; }
uint16_t JUsbDevice::maxPacketSize() const   { return m_impl->maxPacket; }
const std::string& JUsbDevice::lastError() const { return m_impl->lastError; }
const JUsbDeviceInfo& JUsbDevice::info() const { return m_impl->info; }

} // inline namespace jf
