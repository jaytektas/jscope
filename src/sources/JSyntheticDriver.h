// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JSyntheticChannel.h"
#include "scope/JScopeDriver.h"
#include "scope/JScopeLimits.h"

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <random>
#include <thread>

// A JScopeDriver that generates its signals in software.
//
// This is not scaffolding to be deleted once hardware works. It is the source
// the app opens when nothing is attached, the source every UI regression runs
// against, the fixture generator for the capture tests, and the only source
// whose correct answer is known in closed form — which is how the measurement
// engine gets tested at all. It implements a real software trigger, in both
// acquisition modes, so the trigger UI is genuinely exercised rather than
// merely displayed.

inline namespace jf {

class JSyntheticDriver : public JScopeDriver {
public:
    JSyntheticDriver();
    ~JSyntheticDriver() override;

    static std::vector<JScopeDeviceInfo> enumerate();

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
    bool forceTrigger() override;
    bool autoset() override;

    // ---- synthetic-only: the signal being generated ------------------------
    // Not on JScopeDriver: no real instrument can be told what waveform to
    // measure. Reached through JScopeSession::synthetic().
    void setSignal(uint8_t ch, const JSyntheticChannel& sig);
    JSyntheticChannel signal(uint8_t ch) const;

private:
    void _runLoop();
    void _produceWindowed();
    void _produceStreaming();
    // The waveform itself: a pure, deterministic function of time. Same t, same
    // volts, every call.
    double _signalVolts(uint8_t ch, double t) const;

    // What the ADC sees: the signal plus a fresh noise draw. Not deterministic,
    // and deliberately so — noise should look alive on screen.
    double _sampleVolts(uint8_t ch, double t);

    // The time of the next qualifying trigger crossing on the source channel, at
    // or after `after`. std::nullopt when the level is never crossed on the
    // requested slope — which is what separates Auto (sweep anyway) from Normal
    // (keep waiting).
    std::optional<double> _findTriggerTime(double after);
    int16_t _voltsToCounts(uint8_t ch, double volts) const;
    uint32_t _recordLength() const;

    std::string        m_driverId{"synthetic"};
    JScopeCapabilities m_caps;
    bool               m_open{false};

    mutable std::mutex                                                m_cfgMutex;
    std::array<JScopeChannelConfig, JScopeLimits::kMaxChannels>       m_channels;
    std::array<JSyntheticChannel,   JScopeLimits::kMaxChannels>       m_signals;
    JScopeTimebaseConfig                                              m_timebase;
    JScopeTriggerConfig                                               m_trigger;

    std::thread           m_thread;
    std::atomic<bool>     m_running{false};
    std::atomic<bool>     m_singleShot{false};
    std::atomic<bool>     m_forceTrigger{false};
    std::atomic<JScopeAcquisitionMode> m_mode{JScopeAcquisitionMode::Windowed};

    uint64_t m_sequence{0};
    double   m_lastTriggerTime{0.0};
    double   m_prevTriggerTime{0.0};
    uint64_t m_streamIndex{0};
    double   m_phaseTime{0.0};
    std::chrono::steady_clock::time_point m_startTime;
    std::mt19937 m_rng{0x5EED};
};

} // inline namespace jf
