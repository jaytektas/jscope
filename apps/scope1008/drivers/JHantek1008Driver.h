#pragma once

#include "JHantek1008Calibration.h"
#include "JHantek1008PatternGenerator.h"
#include "JHantek1008Protocol.h"
#include "JHantek1008Tables.h"
#include "scope/JScopeDriver.h"
#include "usb/JUsbDevice.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <chrono>
#include <thread>
#include <vector>

inline namespace jf {

// The Hantek 1008C: eight channels, 12-bit, vendor-specific bulk USB.
//
// Two acquisition modes, and they are genuinely different instruments'
// behaviour rather than variations on one:
//
//   Streaming (roll) — a continuous 1/16..440 Sa/s/ch with no trigger at all.
//   Windowed (burst) — a triggered record of roughly 25 samples per division.
//
// The device has NO SWEEP MODE. Auto, Normal and Single are implemented here as
// host-side policy around the 0xa5 ready poll, because "what happens when no
// trigger arrives" is a decision someone has to make and the instrument
// declining to make it does not remove the need.
//
// It also has no coupling control, no vertical offset and no autoset, and
// capabilities() says so rather than the UI offering controls that do nothing.
class JHantek1008Driver : public JScopeDriver {
public:
    JHantek1008Driver();
    ~JHantek1008Driver() override;

    static std::vector<JScopeDeviceInfo> enumerate();

    const std::string&        driverId() const override { return m_driverId; }
    bool                      open(const JScopeDeviceInfo& device) override;
    void                      close() override;

    // The eight digital outputs. Always present -- it is part of the instrument,
    // not an accessory -- and usable before a device is open, where it simply
    // remembers what it was told.
    JScopeGenerator*       generator() override       { return &m_generator; }
    const JScopeGenerator* generator() const override { return &m_generator; }
    bool                      isOpen() const override { return m_open; }
    const JScopeCapabilities& capabilities() const override { return m_caps; }

    bool applyChannel (uint8_t ch, const JScopeChannelConfig& cfg) override;
    bool applyTimebase(const JScopeTimebaseConfig& cfg) override;
    bool applyTrigger (const JScopeTriggerConfig& cfg) override;

    const JScopeChannelConfig&  channelConfig(uint8_t ch) const override;
    const JScopeTimebaseConfig& timebaseConfig() const override { return m_timebase; }
    const JScopeTriggerConfig&  triggerConfig()  const override { return m_trigger; }

    bool start(JScopeAcquisitionMode mode) override;
    bool single() override;
    void stop() override;
    bool forceTrigger() override { return false; }   // the hardware has none

    // Host-side: measure on the widest range, then choose the smallest that does
    // not clip. The hardware has no autoset and the ranges are coarse enough
    // that choosing by hand is a trap — the middle range clips a 0-5 V signal
    // into a clean-looking flat-topped square.
    bool autoset() override;

    // Where the per-unit zero offsets are cached. Set before open().
    void setCalibrationPath(std::string path) { m_calibrationPath = std::move(path); }

    // The measured offsets, for a bench probe to print and compare against the
    // Python reference's on the same unit.
    const JHantek1008Calibration& calibration() const { return m_calibration; }

private:
    void _buildCapabilities();
    bool _measureZeroOffsets();
    bool _applyConfigToDevice();
    void _runLoop();
    bool _acquireBurst();
    bool _acquireRoll();
    std::vector<uint8_t> _activeChannels() const;
    std::vector<double>  _perChannelVScale() const;
    void _fillFrameHeader(JScopeFrame& f, const std::vector<uint8_t>& active,
                          double sampleInterval, int32_t triggerIndex, bool triggered);

    std::string        m_driverId{"hantek-1008c"};
    JScopeCapabilities m_caps;
    bool               m_open{false};

    JUsbDevice                            m_usb;
    std::unique_ptr<JHantek1008Protocol>  m_protocol;
    JHantek1008Calibration                m_calibration;
    std::string                           m_calibrationPath;

    mutable std::mutex m_cfgMutex;
    // Declared after the mutex it borrows, so it is destroyed before it.
    JHantek1008PatternGenerator m_generator{m_cfgMutex};
    std::array<JScopeChannelConfig, JHantek1008Tables::kChannelCount> m_channels;
    JScopeTimebaseConfig m_timebase;
    JScopeTriggerConfig  m_trigger;
    bool                 m_configDirty{true};

    std::thread       m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_singleShot{false};
    std::atomic<JScopeAcquisitionMode> m_mode{JScopeAcquisitionMode::Streaming};

    uint64_t m_sequence{0};
    uint64_t m_streamIndex{0};
    std::chrono::steady_clock::time_point m_startTime;

    // Reused across acquisitions so the thread does not allocate per frame.
    std::vector<uint8_t>  m_rawBytes;
    std::vector<uint16_t> m_shorts;

    // Last logged size of each burst half, so a change is reported once rather
    // than on every capture.
    size_t m_lastHalfBytes[2]{0, 0};

    // Roll mode hands back whatever has accumulated since the last poll, which
    // at these rates is a handful of samples. Publishing each one as a frame
    // gives a display that flickers and measurements with nothing to work on, so
    // chunks are accumulated per channel until there is a useful record.
    std::array<std::vector<int16_t>, JHantek1008Tables::kChannelCount> m_rollAccumulator;
    std::chrono::steady_clock::time_point m_rollWindowStart;
    bool m_rollWindowOpen{false};
};

} // inline namespace jf
