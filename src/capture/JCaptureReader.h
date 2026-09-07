// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JCaptureFormat.h"
#include "JCaptureMeta.h"

#include <cstdint>
#include <memory>
#include <string>

inline namespace jf {

class JScopeFrame;

// Reads a .jscope file.
//
// A file with no footer opens normally. Recording appends as it goes, so an
// interrupted capture — a crash, a full disk, a pulled cable — has frames but no
// index, and refusing to open it would throw away exactly the recording someone
// most wants to look at. The index is rebuilt by scanning instead.
class JCaptureReader {
public:
    JCaptureReader();
    ~JCaptureReader();

    JCaptureReader(const JCaptureReader&)            = delete;
    JCaptureReader& operator=(const JCaptureReader&) = delete;

    bool open(const std::string& path);
    void close();
    bool isOpen() const;

    const JCaptureMeta& meta() const;
    uint64_t frameCount() const;

    // True when the index had to be rebuilt because the footer was missing —
    // worth telling the user, since it means the recording did not close.
    bool wasRecovered() const;

    // Read into a caller-provided frame, provisioning it if needed. Reusing one
    // frame across a scrub is what keeps replay allocation-free.
    bool readFrame(uint64_t index, JScopeFrame& out);

    // The frame at or before a timestamp — a binary search over the index, which
    // is what makes scrubbing a long capture instant.
    uint64_t frameAtTime(double seconds) const;

    double startTime() const;
    double endTime() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // inline namespace jf
