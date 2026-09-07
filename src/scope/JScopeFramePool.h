// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JScopeFrame.h"
#include "JScopeFrameQueue.h"
#include "JScopeLimits.h"
#include "JScopeLog.h"
#include <atomic>
#include <memory>
#include <vector>

// Fixed frame storage plus two SPSC rings, which together give the acquisition
// thread a place to write with no allocation and no lock.
//
//   free  (consumer -> producer)  frames available for writing
//   ready (producer -> consumer)  frames filled and published
//
// The producer takes from free, fills, and publishes to ready. The consumer
// takes from ready, uses, and returns to free. In steady state nothing is
// allocated and nothing is locked.
//
// BACKPRESSURE IS DROP-OLDEST. A scope shows the newest acquisition, not a
// backlog, so when free is dry the producer reclaims the oldest ready frame and
// counts the drop. It never blocks — a stalled UI must not be able to stall USB.
// The count is surfaced in the status bar, so a drop is visible rather than
// silent, and JScopeFrameHeader::sequence lets the consumer see exactly what it
// missed.
//
// provision() allocates and is callable ONLY while the driver is stopped. That
// restriction is the whole reason the acquisition thread can be allocation-free,
// and it is why changing memory depth mid-recording is blocked in the UI.

inline namespace jf {

class JScopeFramePool {
public:
    JScopeFramePool() : m_free(JScopeLimits::kDefaultPoolDepth),
                        m_ready(JScopeLimits::kDefaultPoolDepth) {}

    JScopeFramePool(const JScopeFramePool&)            = delete;
    JScopeFramePool& operator=(const JScopeFramePool&) = delete;

    // Size the pool for the largest frame this driver will produce. Depth is
    // clamped so that depth * frameBytes stays inside the budget: a DSO deep
    // record is 8 Mpts x 2 ch x 2 B = 32 MB, and a naive depth of 8 would be a
    // quarter of a gigabyte. Returns the depth actually provisioned.
    size_t provision(uint8_t maxChannels, uint32_t maxSamplesPerChannel,
                     size_t requestedDepth = JScopeLimits::kDefaultPoolDepth,
                     size_t budgetBytes    = JScopeLimits::kDefaultPoolBudgetBytes) {
        const size_t frameBytes = static_cast<size_t>(maxChannels) * maxSamplesPerChannel
                                * sizeof(int16_t);
        size_t depth = requestedDepth;
        if (frameBytes > 0) {
            const size_t affordable = budgetBytes / frameBytes;
            if (depth > affordable) depth = affordable;
        }
        if (depth < 2) depth = 2;   // one in flight plus one being filled

        m_frames.clear();
        m_frames.reserve(depth);
        m_free.reset(depth + 1);    // +1: a ring of N slots holds N-1 items
        m_ready.reset(depth + 1);

        for (size_t i = 0; i < depth; ++i) {
            auto f = std::make_unique<JScopeFrame>();
            f->provision(maxChannels, maxSamplesPerChannel);
            m_free.push(f.get());
            m_frames.push_back(std::move(f));
        }
        m_dropped.store(0, std::memory_order_relaxed);

        JLOGC(JScopeLog::kFrames, JLogLevel::Info)
            << "pool provisioned: depth=" << depth
            << " channels=" << static_cast<int>(maxChannels)
            << " samples/ch=" << maxSamplesPerChannel
            << " frame=" << (frameBytes / 1024) << "KiB"
            << " total=" << ((frameBytes * depth) / 1024) << "KiB"
            << (depth < requestedDepth ? "  (depth clamped by budget)" : "");
        return depth;
    }

    size_t depth() const { return m_frames.size(); }

    // ---- producer side (acquisition thread) --------------------------------

    // A frame to write into. Never blocks: when the pool is dry it reclaims the
    // oldest published frame and counts a drop. nullptr only if unprovisioned.
    JScopeFrame* acquire() {
        if (void* p = m_free.pop()) return static_cast<JScopeFrame*>(p);
        if (void* p = m_ready.pop()) {
            const uint64_t n = m_dropped.fetch_add(1, std::memory_order_relaxed) + 1;
            JLOGC(JScopeLog::kFrames, JLogLevel::Debug)
                << "pool dry — dropped oldest published frame (total dropped " << n << ")";
            return static_cast<JScopeFrame*>(p);
        }
        JLOGC(JScopeLog::kFrames, JLogLevel::Warn) << "acquire() on an unprovisioned pool";
        return nullptr;
    }

    void publish(JScopeFrame* f) {
        if (!f) return;
        if (!m_ready.push(f)) {
            // Cannot happen while one producer holds at most one frame, but a
            // lost frame is worse than a wasted branch.
            m_free.push(f);
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            JLOGC(JScopeLog::kFrames, JLogLevel::Warn) << "ready ring full on publish — frame discarded";
        }
    }

    // ---- consumer side (main thread) ---------------------------------------

    JScopeFrame* tryPopReady() { return static_cast<JScopeFrame*>(m_ready.pop()); }
    void         release(JScopeFrame* f) { if (f) m_free.push(f); }

    size_t   readyCount() const { return m_ready.size(); }
    size_t   freeCount()  const { return m_free.size(); }
    uint64_t dropped()    const { return m_dropped.load(std::memory_order_relaxed); }
    void     resetDropped() { m_dropped.store(0, std::memory_order_relaxed); }

private:
    std::vector<std::unique_ptr<JScopeFrame>> m_frames;
    JScopeFrameQueue      m_free;
    JScopeFrameQueue      m_ready;
    std::atomic<uint64_t> m_dropped{0};
};

} // inline namespace jf
