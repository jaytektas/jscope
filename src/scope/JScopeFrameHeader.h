#pragma once

#include "JScopeAcquisitionMode.h"
#include "JScopeLimits.h"
#include <cstdint>

// Everything about one acquisition except the samples. Fixed size and trivially
// copyable, with explicit widths, because it is written to disk verbatim by
// JCaptureWriter and read back by JCaptureReader.
//
// The counts->volts transform travels WITH the frame rather than being looked up
// from the current channel settings. That is what makes a capture still
// interpretable after the knobs have moved on, and what lets a recalibration be
// re-applied to old data instead of being baked in irreversibly.

inline namespace jf {

struct JScopeFrameHeader {
    uint64_t sequence{0};             // monotonic per driver, per start(); gaps mean drops
    uint64_t startSampleIndex{0};     // Streaming: samples since start. Windowed: 0
    double   timestampSeconds{0.0};   // steady clock, since start()
    double   sampleInterval{0.0};     // seconds per sample, per channel
    uint32_t sampleCount{0};          // PER CHANNEL
    uint8_t  channelCount{0};         // planes actually present in this frame
    uint8_t  channelIds[JScopeLimits::kMaxChannels]{};   // plane i -> device channel id
    int32_t  triggerSampleIndex{-1};  // -1 when untriggered (all of Streaming)
    bool     triggered{false};
    JScopeAcquisitionMode mode{JScopeAcquisitionMode::Windowed};

    // Per-plane affine: volts = (count - zeroOffsetCounts[i]) * countsToVolts[i]
    float countsToVolts   [JScopeLimits::kMaxChannels]{};
    float zeroOffsetCounts[JScopeLimits::kMaxChannels]{};
    float voltsPerDiv     [JScopeLimits::kMaxChannels]{};

    double durationSeconds() const { return sampleInterval * static_cast<double>(sampleCount); }
};

} // inline namespace jf
