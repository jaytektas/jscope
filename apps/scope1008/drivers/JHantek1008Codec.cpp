// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JHantek1008Codec.h"

#include <algorithm>

inline namespace jf {

std::vector<uint16_t> JHantek1008Codec::toShorts(const uint8_t* data, size_t length) {
    std::vector<uint16_t> out;
    if (!data || length < 2) return out;
    out.reserve(length / 2);
    // Little-endian, matching the reference's data[i] + data[i+1] * 256.
    for (size_t i = 0; i + 1 < length; i += 2)
        out.push_back(static_cast<uint16_t>(data[i] |
                      (static_cast<uint16_t>(data[i + 1]) << 8)));
    return out;
}

size_t JHantek1008Codec::samplesPerChannel(size_t shortCount, uint8_t wireStride) {
    if (wireStride == 0) return 0;
    return shortCount / wireStride;
}

size_t JHantek1008Codec::deinterleave(const uint16_t* shorts, size_t count,
                                      uint8_t activeChannelCount, uint8_t wireStride,
                                      int16_t* out, size_t outStridePerPlane) {
    if (!shorts || !out || activeChannelCount == 0 || wireStride < activeChannelCount)
        return 0;

    // Padding lanes advance the rotation without producing a plane, so they are
    // part of the stride and absent from the output.
    const size_t stride  = wireStride;
    const size_t samples = std::min(count / stride, outStridePerPlane);

    for (uint8_t c = 0; c < activeChannelCount; ++c) {
        int16_t* plane = out + static_cast<size_t>(c) * outStridePerPlane;
        for (size_t s = 0; s < samples; ++s)
            plane[s] = static_cast<int16_t>(shorts[s * stride + c]);
    }
    return samples;
}

std::vector<uint8_t> JHantek1008Codec::channelMap(const std::vector<uint8_t>& activeChannels) {
    std::vector<uint8_t> map(JHantek1008Tables::kChannelCount, 0x00);
    for (uint8_t c : activeChannels)
        if (c < JHantek1008Tables::kChannelCount) map[c] = 0x01;
    return map;
}

} // inline namespace jf
