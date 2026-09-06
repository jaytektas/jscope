#pragma once

#include "JHantek1008Tables.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// The pure part of the 1008C protocol: turning wire bytes into samples.
//
// Free functions with no USB, no threads and no device — which is what lets the
// hardest-to-verify half of this driver be written and tested before the
// instrument is even plugged in, and checked against values computed by hand
// from the Python reference rather than against a previous run.

inline namespace jf {

class JHantek1008Codec {
public:
    // Little-endian 16-bit samples. Odd trailing bytes are impossible on the
    // wire and are dropped rather than read past the end.
    static std::vector<uint16_t> toShorts(const uint8_t* data, size_t length);

    // De-interleave into planes.
    //
    // The wire format is channel-major-per-sample: s0c0 s0c1 ... s0cN s1c0 ...
    // The STRIDE is not the active channel count — the device pads it (see
    // JHantek1008Tables::burstStrideFor and rollStrideFor), and the padding
    // lanes are discarded.
    //
    // Getting the stride wrong does not fail loudly. It produces a plausible
    // waveform belonging to a rotation of channels: three channels read with a
    // stride of three turned a 1 kHz square into a convincing 66.7 kHz. So the
    // stride is passed in explicitly rather than derived from a flag that cannot
    // express the rule.
    //
    // `out` is written as [plane][sample]; planes are in ascending channel order.
    // Returns the number of complete samples per channel.
    static size_t deinterleave(const uint16_t* shorts, size_t count,
                               uint8_t activeChannelCount, uint8_t wireStride,
                               int16_t* out, size_t outStridePerPlane);

    // Samples per channel a given number of shorts holds, at that wire stride.
    static size_t samplesPerChannel(size_t shortCount, uint8_t wireStride);

    // volts = (raw - zeroOffset) * kVoltsPerCount * vscale
    static double toVolts(double raw, double zeroOffsetCounts, double vscale) {
        return (raw - zeroOffsetCounts) * JHantek1008Tables::kVoltsPerCount * vscale;
    }

    // The scale factor a frame header carries, so the rest of the application
    // never needs to know what a vscale is.
    static float countsToVolts(double vscale) {
        return static_cast<float>(JHantek1008Tables::kVoltsPerCount * vscale);
    }

    // The eight-byte channel map the 0xaa command takes.
    static std::vector<uint8_t> channelMap(const std::vector<uint8_t>& activeChannels);
};

} // inline namespace jf
