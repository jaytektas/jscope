// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JCsvExport.h"

#include "scope/JScopeFrame.h"
#include "scope/JScopeLog.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>

inline namespace jf {

namespace {

// Fast fixed-precision float-to-string for CSV output. Replaces std::ostringstream
// / operator<< with std::to_chars, which avoids locale overhead and heap allocation.
// Writes exactly 9 significant digits (matching the original setprecision(9)).
inline char* writeFloat(char* out, double v) {
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, 9);
    (void)ec;
    char* p = buf;
    while (p < ptr) *out++ = *p++;
    return out;
}

// Write a time value (sample index × sample interval) to the buffer.
inline char* writeTime(char* out, size_t index, double sampleInterval) {
    return writeFloat(out, index * sampleInterval);
}

// Write a voltage value to the buffer.
inline char* writeVoltage(char* out, double v) {
    return writeFloat(out, v);
}

} // namespace

bool JCsvExport::write(const std::string& path, const JScopeFrame& frame,
                       const std::string& deviceModel, size_t first, size_t count) {
    std::ofstream f(path);
    if (!f) {
        JLOGC(JScopeLog::kCapture, JLogLevel::Error)
            << "cannot open '" << path << "' for CSV export";
        return false;
    }

    const size_t total = frame.header.sampleCount;
    const size_t begin = std::min(first, total);
    const size_t n     = (count == 0) ? (total - begin) : std::min(count, total - begin);

    // A commented header, in the shape csvexport.py already emits, so the two
    // tools' output can be read by the same scripts.
    f << "# HEADER\n"
      << "# device: " << deviceModel << "\n"
      << "# channels: " << static_cast<int>(frame.header.channelCount) << "\n"
      << "# samples: " << n << "\n"
      << "# sample interval: " << frame.header.sampleInterval << " s\n"
      << "# sample rate: "
      << (frame.header.sampleInterval > 0.0 ? 1.0 / frame.header.sampleInterval : 0.0)
      << " Hz\n"
      << "# trigger index: " << frame.header.triggerSampleIndex << "\n";
    for (uint8_t p = 0; p < frame.header.channelCount; ++p)
        f << "# ch" << static_cast<int>(frame.header.channelIds[p] + 1)
          << ": countsToVolts=" << frame.header.countsToVolts[p]
          << " zeroOffsetCounts=" << frame.header.zeroOffsetCounts[p]
          << " voltsPerDiv=" << frame.header.voltsPerDiv[p] << "\n";
    f << "# DATA\n";

    f << "time_s";
    for (uint8_t p = 0; p < frame.header.channelCount; ++p)
        f << ",ch" << static_cast<int>(frame.header.channelIds[p] + 1) << "_V";
    f << "\n";

    // Fast path: use std::to_chars for every numeric value instead of
    // operator<<.  For a 4096-sample × 8-channel export this avoids
    // ~32 000 locale-aware floating-point formatting calls.
    char buf[128];  // generous: 9-digit float + comma + newline + padding
    for (size_t i = 0; i < n; ++i) {
        const size_t s = begin + i;
        char* p = buf;
        p = writeTime(p, s, frame.header.sampleInterval);
        *p++ = '\n';
        f.write(buf, static_cast<std::streamsize>(p - buf));

        for (uint8_t ch = 0; ch < frame.header.channelCount; ++ch) {
            p = buf;
            *p++ = ',';
            p = writeVoltage(p, frame.voltsAt(ch, static_cast<uint32_t>(s)));
            *p++ = '\n';
            f.write(buf, static_cast<std::streamsize>(p - buf));
        }
    }

    if (!f) {
        JLOGC(JScopeLog::kCapture, JLogLevel::Error) << "CSV export to '" << path << "' failed";
        return false;
    }
    JLOGC(JScopeLog::kCapture, JLogLevel::Info)
        << "exported " << n << " samples x " << int(frame.header.channelCount)
        << " channels to '" << path << "'";
    return true;
}

} // inline namespace jf
