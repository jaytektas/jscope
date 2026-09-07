// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>

// The .jscope container.
//
// A JSON header (human-inspectable, via the framework's JJson) describing the
// device and its settings, then length-prefixed frame records, then an index.
// CSV was the obvious alternative and cannot hold a megapoint deep capture
// usefully; a CSV export of the visible window stays available for interop.
//
// Frames are appended as they are produced, so recording is O(1) and survives a
// crash. THE FOOTER MAY BE ABSENT: a file whose recording was interrupted is a
// normal case, not a corrupt one, and the reader rebuilds the index by scanning
// rather than refusing to open it. That is required behaviour and it is tested.

inline namespace jf {

struct JCaptureFormat {
    static constexpr char     kMagic[8]       = { 'J','S','C','O','P','E','0','1' };
    static constexpr uint32_t kFrameMagic     = 0x304D5246;   // 'FRM0' little-endian
    static constexpr uint32_t kIndexMagic     = 0x30584449;   // 'IDX0'
    static constexpr char     kFooterMagic[8] = { 'J','S','C','P','E','N','D','\0' };

    static constexpr uint32_t kVersion = 1;

    // Reserved for a future compressed payload; readers must reject flags they
    // do not understand rather than misinterpret the bytes.
    static constexpr uint32_t kFlagNone = 0;

    struct JFileHeader {
        char     magic[8];
        uint32_t headerBytes;    // size of this struct
        uint32_t version;
        uint32_t flags;
        uint32_t jsonBytes;      // JSON metadata immediately follows
    };

    struct JFrameRecord {
        uint32_t magic;          // kFrameMagic
        uint32_t headerBytes;    // sizeof(JScopeFrameHeader)
        uint32_t payloadBytes;   // channelCount * sampleCount * sizeof(int16_t)
    };

    struct JIndexEntry {
        uint64_t fileOffset;
        double   timestampSeconds;
        uint64_t startSampleIndex;
    };

    struct JFooter {
        char     magic[8];
        uint64_t indexOffset;
        uint64_t frameCount;
    };
};

} // inline namespace jf
