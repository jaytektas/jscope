#pragma once

#include "JUsbDeviceInfo.h"
#include "JUsbTransport.h"

#include <cstdint>
#include <memory>
#include <string>

inline namespace jf {

// One claimed USB interface, with blocking bulk transfers.
//
// Synchronous, not asynchronous, and the reason is worth stating: the 1008C's
// conversation is strictly request/response with mandated delays, so it maps
// one-to-one onto the Python reference it is ported from. libusb's synchronous
// API is thread-safe and each device has its own handle. Async transfers plus a
// shared event thread would buy nothing at these rates and add a whole callback
// apparatus to get wrong.
//
// It may not stay that way. The 1008C negotiates FULL SPEED — 12 Mbps, 64-byte
// packets — and reads burst samples 64 bytes at a time, so every transaction
// costs a bus turnaround. If the measured frame rate is poor, the fix is to
// queue several reads concurrently, and this interface is shaped so that can be
// added underneath it without callers changing.
//
// Errors are return values plus lastError(), not signals: the async signal layer
// belongs to the driver that owns the acquisition thread, not to the transport.
class JUsbDevice : public JUsbTransport {
public:
    JUsbDevice();
    ~JUsbDevice() override;

    JUsbDevice(const JUsbDevice&)            = delete;
    JUsbDevice& operator=(const JUsbDevice&) = delete;

    bool open(const JUsbDeviceInfo& info);
    void close();
    bool isOpen() const;

    // Claim an interface, detaching whatever kernel driver holds it first. The
    // DSO2D15 is held by the kernel's usbtmc driver, so without the detach it
    // cannot be claimed at all.
    bool claimInterface(int interfaceNumber, bool autoDetachKernelDriver = true);
    void releaseInterface();

    bool reset();

    // Endpoint addresses discovered from the claimed interface's descriptors,
    // by DIRECTION rather than by number. The 1008C's OUT endpoint is 0x02, not
    // the 0x01 that would be assumed; hardcoding either would work on one device
    // and silently fail on the other.
    uint8_t bulkInEndpoint() const;
    uint8_t bulkOutEndpoint() const;
    uint16_t maxPacketSize() const;

    bool bulkOut(uint8_t endpoint, const uint8_t* data, size_t length,
                 unsigned timeoutMs) override;
    int  bulkIn(uint8_t endpoint, uint8_t* data, size_t maxLength,
                unsigned timeoutMs) override;

    // For USBTMC's class requests.
    int controlTransfer(uint8_t requestType, uint8_t request, uint16_t value,
                        uint16_t index, uint8_t* data, uint16_t length,
                        unsigned timeoutMs);

    bool clearHalt(uint8_t endpoint);

    // The USBTMC class's INITIATE_CLEAR / CHECK_CLEAR_STATUS pair, followed by
    // clearing the bulk-out halt. This is how a Test and Measurement device is
    // told to discard whatever it was in the middle of.
    bool usbTmcClear(int interfaceNumber = 0);

    const std::string& lastError() const override;

    const JUsbDeviceInfo& info() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // inline namespace jf
