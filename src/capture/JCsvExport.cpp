// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JCsvExport.h"

#include "scope/JScopeFrame.h"
#include "scope/JScopeLog.h"

#include <algorithm>
#include <fstream>
#include <iomanip>

inline namespace jf {

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

    f << std::setprecision(9);
    for (size_t i = 0; i < n; ++i) {
        const size_t s = begin + i;
        f << (s * frame.header.sampleInterval);
        for (uint8_t p = 0; p < frame.header.channelCount; ++p)
            f << ',' << frame.voltsAt(p, static_cast<uint32_t>(s));
        f << '\n';
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
