// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JReplayDriver.h"

#include "scope/JScopeDriverRegistry.h"
#include "scope/JScopeLog.h"

#include <algorithm>
#include <chrono>
#include <cmath>

inline namespace jf {

namespace {
// A frame is emitted at its recorded interval; below this the wait is not worth
// sleeping for and the loop simply proceeds.
constexpr double kMinSleepSeconds = 0.001;
// Cap the emit rate so a capture of very short frames cannot spin the CPU.
constexpr double kMaxFramesPerSecond = 240.0;
}

JReplayDriver::JReplayDriver() = default;
JReplayDriver::~JReplayDriver() { close(); }

void JReplayDriver::_buildCapabilitiesFromMeta() {
    const JCaptureMeta& m = m_reader.meta();

    m_caps = JScopeCapabilities{};
    m_caps.driverId            = m_driverId;
    m_caps.model               = m.model.empty() ? "Capture" : (m.model + " (replay)");
    m_caps.serialNumber        = m.serialNumber;
    m_caps.adcBits             = m.adcBits;
    m_caps.countsMin           = m.countsMin;
    m_caps.countsMax           = m.countsMax;
    m_caps.verticalDivisions   = m.verticalDivisions;
    m_caps.horizontalDivisions = m.horizontalDivisions;
    m_caps.maxSimultaneousChannels = m.channelCount;

    for (uint8_t c = 0; c < m.channelCount; ++c) {
        JScopeChannelCaps ch;
        ch.label = (c < m.channelLabels.size()) ? m.channelLabels[c]
                                                : ("CH" + std::to_string(c + 1));
        // The steps the ORIGINAL device offered, so the panel shows that
        // instrument's ranges. Replay cannot change them, but a reader deserves
        // to see what was available when the capture was taken.
        if (c < m.voltsPerDivSteps.size() && !m.voltsPerDivSteps[c].empty())
            ch.voltsPerDiv = m.voltsPerDivSteps[c];
        else if (c < m.channels.size())
            ch.voltsPerDiv = { m.channels[c].voltsPerDiv };

        // Single-element lists: the panels then offer no choice, which is the
        // truth — nothing about a recording can be re-dialled.
        ch.probeRatios = { (c < m.channels.size()) ? m.channels[c].probeRatio : 1.0 };
        ch.couplings   = {};
        ch.offsetRangeVolts  = 0.0;
        ch.canInvert         = false;
        ch.canBandwidthLimit = false;
        m_caps.channels.push_back(std::move(ch));
    }

    m_caps.acquisitionModes = jScopeAcquisitionModeBit(m.timebase.mode);
    m_caps.secondsPerDiv    = { m.timebase.secondsPerDiv };
    if (m.timebase.mode == JScopeAcquisitionMode::Streaming)
        m_caps.streamSampleRates = { m.timebase.sampleRate };
    m_caps.memoryDepths                = { m.timebase.recordLength };
    m_caps.deviceDeterminedRecordLength = true;   // a recording's length is fixed

    // A recording is not triggerable. Saying so keeps the trigger panel honest
    // rather than showing controls that cannot do anything.
    m_caps.triggerModes       = 0;
    m_caps.hasHardwareTrigger = false;
    m_caps.hasTriggerPosition = false;
    m_caps.hasForceTrigger    = false;
    m_caps.hasAutoset         = false;

    m_channels = m.channels;
    m_channels.resize(m.channelCount);
    m_timebase = m.timebase;
    m_trigger  = m.trigger;
}

bool JReplayDriver::open(const JScopeDeviceInfo& device) {
    close();

    const std::string& path = device.path.empty() ? device.displayName : device.path;
    if (!m_reader.open(path)) {
        _postError("cannot open capture '" + path + "'");
        return false;
    }
    if (m_reader.frameCount() == 0) {
        _postError("'" + path + "' contains no frames");
        m_reader.close();
        return false;
    }

    _buildCapabilitiesFromMeta();

    // Provision from the first frame's real shape rather than from the metadata,
    // which describes the requested settings and not necessarily what arrived.
    if (!m_reader.readFrame(0, m_scratch)) {
        _postError("cannot read the first frame of '" + path + "'");
        m_reader.close();
        return false;
    }
    m_pool.provision(std::max<uint8_t>(m_scratch.header.channelCount, 1),
                     m_scratch.header.sampleCount);

    m_position.store(0);
    m_open = true;
    _setState(JScopeState::Idle);

    JLOGC(JScopeLog::kReplay, JLogLevel::Info)
        << "replaying '" << path << "': " << m_reader.frameCount() << " frames, "
        << int(m_caps.channelCount()) << " channels, recorded on "
        << m_reader.meta().model
        << (m_reader.wasRecovered() ? "  [index recovered — recording did not close]" : "");
    return true;
}

void JReplayDriver::close() {
    if (!m_open) return;
    stop();
    m_reader.close();
    m_open = false;
    _setState(JScopeState::Closed);
}

// Nothing about a recording can be changed. Refusing is the honest answer, and
// the capability lists already stop the UI from asking.
bool JReplayDriver::applyChannel(uint8_t, const JScopeChannelConfig&) { return false; }
bool JReplayDriver::applyTimebase(const JScopeTimebaseConfig&)        { return false; }
bool JReplayDriver::applyTrigger(const JScopeTriggerConfig&)          { return false; }

const JScopeChannelConfig& JReplayDriver::channelConfig(uint8_t ch) const {
    std::lock_guard<std::mutex> lk(m_cfgMutex);
    static const JScopeChannelConfig fallback{};
    return (ch < m_channels.size()) ? m_channels[ch] : fallback;
}

uint64_t JReplayDriver::frameCount() const { return m_reader.frameCount(); }

bool JReplayDriver::start(JScopeAcquisitionMode) {
    if (!m_open) return false;
    if (m_running.load()) return true;

    // Reap a thread that retired itself — reaching the end of a non-looping
    // capture clears m_running but leaves the thread joinable, and assigning
    // over a joinable std::thread calls std::terminate.
    if (m_thread.joinable()) m_thread.join();

    m_singleShot.store(false);
    m_running.store(true);
    m_thread = std::thread(&JReplayDriver::_runLoop, this);
    _setState(JScopeState::Running);
    JLOGC(JScopeLog::kReplay, JLogLevel::Info)
        << "playback started at frame " << m_position.load() << ", rate x" << m_rate.load();
    return true;
}

bool JReplayDriver::single() {
    if (!m_open) return false;
    // One frame at the current position, without starting the loop — which is
    // what a scrub is.
    _emitFrame(m_position.load());
    _setState(JScopeState::Stopped);
    return true;
}

void JReplayDriver::stop() {
    // Clear the flag AND join unconditionally. The loop can retire itself — at
    // the end of a non-looping capture, or after a single shot — which leaves the
    // flag already false but the thread still joinable. Returning early on the
    // flag then skips the join, and destroying a joinable std::thread calls
    // std::terminate.
    m_running.store(false);
    if (!m_thread.joinable()) return;
    m_thread.join();
    _setState(m_open ? JScopeState::Stopped : JScopeState::Closed);
    JLOGC(JScopeLog::kReplay, JLogLevel::Info)
        << "playback stopped at frame " << m_position.load();
}

void JReplayDriver::seek(uint64_t frameIndex) {
    const uint64_t count = m_reader.frameCount();
    if (count == 0) return;
    const uint64_t clamped = std::min(frameIndex, count - 1);
    m_position.store(clamped);

    // Show the frame that was sought to immediately, whether or not playback is
    // running — otherwise dragging a scrub bar while paused shows nothing.
    _emitFrame(clamped);
    JLOGC(JScopeLog::kReplay, JLogLevel::Debug) << "seek to frame " << clamped;
}

void JReplayDriver::step(int64_t delta) {
    const uint64_t count = m_reader.frameCount();
    if (count == 0) return;
    const int64_t next = static_cast<int64_t>(m_position.load()) + delta;
    seek(static_cast<uint64_t>(std::clamp<int64_t>(next, 0,
                                                   static_cast<int64_t>(count) - 1)));
}

void JReplayDriver::setRate(double multiplier) {
    const double r = std::clamp(multiplier, 0.05, 50.0);
    m_rate.store(r);
    JLOGC(JScopeLog::kReplay, JLogLevel::Debug) << "playback rate x" << r;
}

void JReplayDriver::_emitFrame(uint64_t index) {
    if (!m_reader.readFrame(index, m_scratch)) return;

    JScopeFrame* f = m_pool.acquire();
    if (!f) return;
    if (!f->shape(m_scratch.header.channelCount, m_scratch.header.sampleCount)) {
        m_pool.release(f);
        return;
    }
    f->copyHeaderAndSamplesFrom(m_scratch);
    _publish(f);

    JMainThreadDispatcher::instance().post(
        [this, index] { onPositionChanged.emit(index); });
}

void JReplayDriver::_runLoop() {
    JLOGC(JScopeLog::kReplay, JLogLevel::Debug) << "playback thread up";
    auto lastEmit = std::chrono::steady_clock::now();

    while (m_running.load(std::memory_order_acquire)) {
        const uint64_t count = m_reader.frameCount();
        if (count == 0) break;

        const uint64_t pos = m_position.load();
        _emitFrame(pos);

        if (m_singleShot.load()) { m_running.store(false); break; }

        // Advance, wrapping or stopping at the end.
        const uint64_t next = pos + 1;
        if (next >= count) {
            if (!m_looping.load()) {
                JLOGC(JScopeLog::kReplay, JLogLevel::Info) << "playback reached the end";
                m_running.store(false);
                _setState(JScopeState::Stopped);
                break;
            }
            m_position.store(0);
        } else {
            m_position.store(next);
        }

        // Pace at the RECORDED interval scaled by the rate, so a replay runs at
        // the speed the signal actually happened rather than as fast as the disk
        // will go.
        double interval = m_scratch.header.durationSeconds() / std::max(0.05, m_rate.load());
        interval = std::max(interval, 1.0 / kMaxFramesPerSecond);

        const auto target = lastEmit + std::chrono::duration<double>(interval);
        const auto now    = std::chrono::steady_clock::now();
        if (target > now) {
            const double wait = std::chrono::duration<double>(target - now).count();
            if (wait > kMinSleepSeconds)
                std::this_thread::sleep_for(std::chrono::duration<double>(wait));
        }
        lastEmit = std::chrono::steady_clock::now();
    }
    JLOGC(JScopeLog::kReplay, JLogLevel::Debug) << "playback thread down";
}

} // inline namespace jf

J_REGISTER_SCOPE_DRIVER(JReplayDriver, "replay", "Capture replay",
                        &jf::JReplayDriver::enumerate)
