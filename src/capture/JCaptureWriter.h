#pragma once

#include "JCaptureMeta.h"

#include <j/core/Signal.h>

#include <cstdint>
#include <memory>
#include <string>

inline namespace jf {

class JScopeFrame;
class JScopeDriver;

// Appends frames to a .jscope file.
//
// write() is safe to call from the ACQUISITION thread and does not touch the
// disk there: it copies the frame into a writer-owned buffer and hands that to a
// JWorkerThread. That copy is deliberate. Sharing frames between the display
// ring and the writer would need refcounting across a lock-free ring, which is
// where this kind of code goes wrong; at 440 Sa/s the copy is nothing, and at
// deep-memory rates it is far cheaper than the USB transfer that produced it.
//
// RECORDED FRAMES ARE NEVER DROPPED. The display ring drops oldest because a
// scope shows the newest acquisition, but dropping from a recording is data
// loss. The writer's queue is unbounded and disk-paced; when it grows past a
// bound it reports through onError so the UI can offer to stop rather than
// silently falling behind.
class JCaptureWriter {
public:
    JCaptureWriter();
    ~JCaptureWriter();

    JCaptureWriter(const JCaptureWriter&)            = delete;
    JCaptureWriter& operator=(const JCaptureWriter&) = delete;

    // Build the metadata a capture needs straight from an open driver.
    static JCaptureMeta metaFrom(const JScopeDriver& driver);

    bool open(const std::string& path, const JCaptureMeta& meta);
    void close();
    bool isOpen() const;

    // Acquisition-thread safe.
    void write(const JScopeFrame& frame);

    uint64_t framesWritten() const;
    uint64_t bytesWritten() const;
    size_t   pendingFrames() const;

    // Delivered on the main thread, like every other error in this application.
    JSignal<std::string> onError;

    // Queue depth past which the writer reports that the disk is not keeping up.
    static constexpr size_t kBacklogWarning = 256;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // inline namespace jf
