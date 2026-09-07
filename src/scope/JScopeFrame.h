// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JScopeFrameHeader.h"
#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <vector>

// One acquisition: a header plus PLANAR raw counts.
//
// Counts, not volts. The 1008C is 12-bit and the DSO2D15 8-bit, so int16_t holds
// either losslessly at half the bandwidth of float. Storing volts would double
// the memory, bake calibration irreversibly into every saved capture, and throw
// away the raw counts a scope user sometimes wants to see.
//
// Planar, not interleaved. Every consumer — decimator, measurement engine,
// renderer — walks one channel at a time. The 1008C's wire format is interleaved
// (and in roll mode carries a phantom ninth channel); the driver de-interleaves
// once, on the acquisition thread, which is where the cost belongs.
//
// Capacity is provisioned once by JScopeFramePool while the driver is stopped.
// resize() within that capacity never allocates, which is the invariant that
// keeps the acquisition thread allocation-free.

inline namespace jf {

class JScopeFrame {
public:
    JScopeFrame() = default;

    JScopeFrame(const JScopeFrame&)            = delete;
    JScopeFrame& operator=(const JScopeFrame&) = delete;

    // Reserve room for the largest frame this driver will produce. Allocates;
    // call only while stopped.
    void provision(uint8_t maxChannels, uint32_t maxSamplesPerChannel) {
        m_maxChannels = maxChannels;
        m_maxSamples  = maxSamplesPerChannel;
        m_samples.assign(static_cast<size_t>(maxChannels) * maxSamplesPerChannel, 0);
    }

    // Shape this frame for one acquisition. Never allocates when within the
    // provisioned capacity; returns false (and shapes nothing) when it is not.
    bool shape(uint8_t channelCount, uint32_t sampleCount) {
        if (channelCount > m_maxChannels || sampleCount > m_maxSamples) return false;
        header.channelCount = channelCount;
        header.sampleCount  = sampleCount;
        return true;
    }

    JScopeFrameHeader header{};

    // Plane i is header.channelIds[i]'s samples, header.sampleCount of them.
    int16_t*       plane(uint8_t i)       { return m_samples.data() + planeOffset(i); }
    const int16_t* plane(uint8_t i) const { return m_samples.data() + planeOffset(i); }

    uint8_t  maxChannels() const { return m_maxChannels; }
    uint32_t maxSamples()  const { return m_maxSamples; }
    size_t   capacity()    const { return m_samples.size(); }

    // volts for one sample of plane i, using the transform carried in the header.
    float voltsAt(uint8_t i, uint32_t s) const {
        return (static_cast<float>(plane(i)[s]) - header.zeroOffsetCounts[i]) * header.countsToVolts[i];
    }

    void copyHeaderAndSamplesFrom(const JScopeFrame& src) {
        header = src.header;
        for (uint8_t i = 0; i < src.header.channelCount; ++i)
            std::copy(src.plane(i), src.plane(i) + src.header.sampleCount, plane(i));
    }

private:
    size_t planeOffset(uint8_t i) const {
        return static_cast<size_t>(i) * static_cast<size_t>(m_maxSamples);
    }

    std::vector<int16_t> m_samples;
    uint8_t              m_maxChannels{0};
    uint32_t             m_maxSamples{0};
};

} // inline namespace jf
