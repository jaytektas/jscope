#pragma once

#include "JHantek1008Tables.h"
#include "usb/JUsbTransport.h"

#include <cstdint>
#include <string>
#include <vector>

// The 1008C's command layer.
//
// Every exchange is: write [opcode][parameters...] to the bulk OUT endpoint,
// then read a response of a stated length from bulk IN. When an echo is
// expected the first byte back must equal the opcode, and the response is what
// follows it.
//
// THE DELAYS ARE PART OF THE PROTOCOL. The reference sleeps around each
// transfer, and several commands need a specific longer wait before their
// response can be requested. Whether those are device timing or artefacts of
// Python's own latency is not known, so they are reproduced exactly and named,
// with the reference's own value beside each. They are tunable, and reducing
// them is a measurement to be made on the bench rather than a guess.
//
// The status commands' RESPONSES ARE LOGGED AND NEVER ASSERTED ON. Every
// assertion on them in the reference has been relaxed with a note that the bytes
// are per-device calibration and status — they differ between units, so a driver
// that checked them would work on one 1008C and reject the next.

inline namespace jf {

class JHantek1008Protocol {
public:
    // Timing, in seconds, transcribed from the reference.
    struct JTiming {
        double beforeWrite{0.002};        // sec_till_start
        double beforeRead{0.0};           // sec_till_response_request
        double afterReset{0.7};           // the sleep between the two 0xb0 resets
        double vscaleSettle{0.2132};      // after 0xa2
        double statusB5Settle{0.0193};    // before reading 0xb5
        double acquisitionSettle{0.0124}; // between 0xc0 and 0xc2 during calibration
        double burstArmSettle{0.015};     // before 0xa4 in burst mode
        double readyPollInterval{0.02};   // between 0xa5 attempts
        unsigned timeoutMs{1000};
    };

    JHantek1008Protocol(JUsbTransport& transport, uint8_t endpointOut, uint8_t endpointIn);

    void setTiming(const JTiming& t) { m_timing = t; }
    const JTiming& timing() const { return m_timing; }

    // One command. `responseLength` excludes the echo byte; when `echoExpected`
    // the reply is read one byte longer and the echo is verified and stripped.
    // Returns false on a transport failure or a wrong echo.
    bool send(uint8_t opcode, const std::vector<uint8_t>& parameters = {},
              size_t responseLength = 0, bool echoExpected = true,
              std::vector<uint8_t>* response = nullptr,
              double secondsBeforeRead = -1.0);

    // ---- the individual commands --------------------------------------------
    bool ping(double secondsBeforeStart = 0.0);
    bool setActiveChannels(const std::vector<uint8_t>& channels);
    bool setVerticalScales(const std::vector<double>& perChannelVScale);
    bool setTimeDivId(uint8_t id);

    // How many samples the device captures per burst, ACROSS all active
    // channels. The wire value is a byte count, so this is doubled on the way
    // out. Scope.exe sends it as part of every reconfiguration; without it the
    // device keeps whatever it powered up with.
    bool setRecordLength(uint16_t samples);

    // 0xac dsoSetHTrigPos: the pre-trigger sample count and the two 24-bit delay
    // counters that are the real sweep. `percent` is where the trigger sits in
    // the record, 0..100; the device forces it to the centre for codes <= 15.
    bool setHorizontalTriggerPosition(uint8_t timeDivId, uint8_t activeChannels,
                                      uint8_t percent);
    bool setTrigger(uint8_t sourceChannel, bool rising);
    bool setTriggerLevel(uint16_t level);
    bool startAcquisition(uint8_t mode);           // kModeBurst or kModeRoll
    // 0xc0 enables the trigger; 0xc2 FORCES one. Forcing starts the record
    // immediately and unaligned, which is the whole point in Auto — and exactly
    // what must not happen when the sweep is meant to wait for an edge.
    bool arm(bool forceTrigger);

    // 0xc2 on its own: sweep now, without waiting for the trigger condition.
    // This is what Auto does when its wait runs out.
    bool forceTrigger();

    // Roll mode's start, in the reference's exact order. The 0xa3 MUST carry a
    // roll rate id here rather than a ns/div id: the endpoint stalls otherwise,
    // and the reference carries a comment saying so.
    bool startRollMode(uint8_t rollRateId);

    // Burst mode's start. The reference issues the whole sequence per capture,
    // including the two 0xe4/0xe6 status commands it notes are "not necessarily
    // required" — reproduced because the device is undocumented and the sequence
    // is only known to work as a whole.
    // The OEM arm sequence, in Scope.exe's order. 0xa3 and 0xac must BOTH be
    // reissued here: 0xa3 sets the clock and 0xac the sweep window, and one
    // without the other leaves the sweep at whatever an init blob last wrote.
    bool startBurstCapture(bool forceTrigger);

    // Poll 0xa5 0x5a until it answers 2 or 3, which is what "the record is
    // ready" means. Returns false if it never does.
    bool waitReady(int attempts = 20);

    // Burst readout: 0xc6 gives a length, then ceil(length / 64) reads of 0xa6.
    bool readBurstHalf(uint8_t half, std::vector<uint8_t>& out);

    // Roll readout: 0xc7 gives how many bytes are waiting, 0xc8 reads 64 at a
    // time. Returns 0 when nothing is ready yet, which is normal.
    bool rollReadyLength(uint16_t& lengthOut);
    bool readRollBytes(size_t length, std::vector<uint8_t>& out);

    // The initialisation sequence, replayed byte for byte from the reference.
    bool initialise(const std::vector<uint8_t>& activeChannels,
                    const std::vector<double>& vscales,
                    uint8_t timeDivId, uint8_t triggerChannel, bool triggerRising,
                    uint16_t triggerLevel);
    // The part of it that precedes zero-offset calibration.
    bool initialisePhase1();
    // The part that follows it.
    bool initialisePhase3(const std::vector<uint8_t>& activeChannels,
                          const std::vector<double>& vscales,
                          uint8_t timeDivId, uint16_t recordSamples,
                          uint8_t triggerChannel, bool triggerRising,
                          uint16_t triggerLevel, uint8_t triggerPercent);

    const std::string& lastError() const { return m_lastError; }

private:
    void _sleep(double seconds) const;

    JUsbTransport& m_transport;
    uint8_t        m_epOut;
    uint8_t        m_epIn;
    JTiming        m_timing;
    std::string    m_lastError;
};

} // inline namespace jf
