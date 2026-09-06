#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

// A bounded single-producer / single-consumer ring of frame pointers.
//
// The framework has no ring buffer and no lock-free queue, and
// JMainThreadDispatcher::post() allocates a std::function plus a vector per
// call — the right cost at serial rates, the wrong one for bulk sample flow. So
// this exists.
//
// CONTRACT, and it is not negotiable: exactly one thread ever calls push(), and
// exactly one other thread ever calls pop(). Two producers or two consumers will
// corrupt it silently. In this application the producer is a driver's
// acquisition thread and the consumer is the main thread.
//
// Capacity is rounded up to a power of two so the index wrap is a mask.

inline namespace jf {

class JScopeFrameQueue {
public:
    explicit JScopeFrameQueue(size_t capacity) { reset(capacity); }

    // Resize and empty the ring. Allocates, and is safe only while neither side
    // is running — the pool calls it from provision(), which is stopped-only.
    void reset(size_t capacity) {
        size_t n = 1;
        while (n < capacity) n <<= 1;
        m_slots.assign(n, nullptr);
        m_mask = n - 1;
        m_head.store(0, std::memory_order_relaxed);
        m_tail.store(0, std::memory_order_relaxed);
    }

    JScopeFrameQueue(const JScopeFrameQueue&)            = delete;
    JScopeFrameQueue& operator=(const JScopeFrameQueue&) = delete;

    size_t capacity() const { return m_slots.size(); }

    // Producer only. False when full — the caller decides the drop policy.
    bool push(void* item) {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        const size_t next = (tail + 1) & m_mask;
        if (next == m_head.load(std::memory_order_acquire)) return false;   // full
        m_slots[tail] = item;
        m_tail.store(next, std::memory_order_release);                      // publishes the slot
        return true;
    }

    // Consumer only. nullptr when empty.
    void* pop() {
        const size_t head = m_head.load(std::memory_order_relaxed);
        if (head == m_tail.load(std::memory_order_acquire)) return nullptr; // empty
        void* item = m_slots[head];
        m_slots[head] = nullptr;
        m_head.store((head + 1) & m_mask, std::memory_order_release);
        return item;
    }

    // Approximate — the other side may move underneath. Telemetry only.
    size_t size() const {
        const size_t tail = m_tail.load(std::memory_order_acquire);
        const size_t head = m_head.load(std::memory_order_acquire);
        return (tail - head) & m_mask;
    }

    bool empty() const {
        return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire);
    }

private:
    // Separate cache lines: head and tail are written by different threads, and
    // sharing a line between them costs far more than the padding does.
    alignas(64) std::atomic<size_t> m_head{0};   // consumer writes
    alignas(64) std::atomic<size_t> m_tail{0};   // producer writes
    alignas(64) std::vector<void*>  m_slots;
    size_t                          m_mask{0};
};

} // inline namespace jf
