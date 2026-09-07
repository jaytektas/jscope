// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "capture/JCaptureReader.h"
#include "scope/JScopeDriver.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

inline namespace jf {

// Plays a .jscope capture back through the same JScopeDriver interface real
// hardware uses.
//
// Capabilities are RECONSTRUCTED FROM THE FILE, so replaying a 1008C capture
// shows the 1008C's channels and its volts/div steps rather than a generic set.
// Settings that replay cannot change are reported as single-element lists, so
// the panels naturally offer no choice rather than offering one that does
// nothing.
//
// Playback verbs — seek, rate, loop, step — are NOT on JScopeDriver, because no
// real instrument has them. They live here and are reached through
// JScopeSession::replay(), which returns nullptr for anything else; the replay
// transport bar exists only when it does not.
class JReplayDriver : public JScopeDriver {
public:
    JReplayDriver();
    ~JReplayDriver() override;

    // Captures are opened explicitly by the user, so there is nothing to
    // enumerate. Returning empty keeps it out of the device list without needing
    // a special case in the registry.
    static std::vector<JScopeDeviceInfo> enumerate() { return {}; }

    const std::string&        driverId() const override { return m_driverId; }
    bool                      open(const JScopeDeviceInfo& device) override;
    void                      close() override;
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
    bool forceTrigger() override { return false; }   // a recording cannot be triggered
    bool autoset() override      { return false; }   // nor auto-scaled

    // ---- playback transport: replay only ------------------------------------
    uint64_t frameCount() const;
    uint64_t position() const { return m_position.load(); }
    void     seek(uint64_t frameIndex);
    void     step(int64_t delta);
    void     setRate(double multiplier);
    double   rate() const { return m_rate.load(); }
    void     setLooping(bool on) { m_looping.store(on); }
    bool     looping() const { return m_looping.load(); }

    const JCaptureMeta& meta() const { return m_reader.meta(); }
    bool wasRecovered() const { return m_reader.wasRecovered(); }

    // Position changed, so a transport bar can follow. Main thread.
    JSignal<uint64_t> onPositionChanged;

private:
    void _runLoop();
    void _emitFrame(uint64_t index);
    void _buildCapabilitiesFromMeta();

    std::string        m_driverId{"replay"};
    JScopeCapabilities m_caps;
    JCaptureReader     m_reader;
    bool               m_open{false};

    mutable std::mutex               m_cfgMutex;
    std::vector<JScopeChannelConfig> m_channels;
    JScopeTimebaseConfig             m_timebase;
    JScopeTriggerConfig              m_trigger;

    std::thread           m_thread;
    std::atomic<bool>     m_running{false};
    std::atomic<bool>     m_singleShot{false};
    std::atomic<uint64_t> m_position{0};
    std::atomic<double>   m_rate{1.0};
    std::atomic<bool>     m_looping{true};

    JScopeFrame m_scratch;      // read target, reused so a scrub allocates nothing
};

} // inline namespace jf
