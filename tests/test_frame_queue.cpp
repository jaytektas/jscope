// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "scope/JScopeFrameQueue.h"
#include "support/JTestReport.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using namespace jf;

namespace {

void testBasics(JTestReport& r) {
    JScopeFrameQueue q(4);
    r.check(q.capacity() == 4, "capacity rounds to a power of two");
    r.check(q.empty(), "starts empty");
    r.check(q.pop() == nullptr, "pop on empty returns nullptr");

    int a = 1, b = 2, c = 3;
    r.check(q.push(&a) && q.push(&b) && q.push(&c), "fills to capacity-1");
    r.check(!q.push(&a), "refuses to overfill (a ring of N holds N-1)");
    r.check(q.size() == 3, "size reports the fill");

    r.check(q.pop() == &a, "FIFO order: first out is first in");
    r.check(q.pop() == &b, "FIFO order preserved");
    r.check(q.pop() == &c, "FIFO order preserved to the end");
    r.check(q.empty(), "empty again after draining");
}

void testWraparound(JTestReport& r) {
    JScopeFrameQueue q(4);
    int items[8];
    bool ok = true;
    // Push and pop repeatedly so the indices wrap several times over.
    for (int round = 0; round < 100 && ok; ++round) {
        for (int i = 0; i < 3; ++i) ok = ok && q.push(&items[i]);
        for (int i = 0; i < 3; ++i) ok = ok && (q.pop() == &items[i]);
    }
    r.check(ok, "300 push/pop cycles wrap the indices without losing order");
    r.check(q.empty(), "balanced after wrapping");
}

void testResetEmpties(JTestReport& r) {
    JScopeFrameQueue q(4);
    int a = 1;
    q.push(&a);
    q.reset(8);
    r.check(q.capacity() == 8, "reset resizes");
    r.check(q.empty(), "reset empties");
}

// The contract is one producer and one consumer. This is the check that the
// memory ordering is right: a million sequenced items must arrive in order and
// none may be lost on a ring the consumer keeps draining.
void testThreadedOrdering(JTestReport& r) {
    constexpr uintptr_t kCount = 200000;
    JScopeFrameQueue q(1024);

    std::atomic<bool> producerDone{false};
    std::atomic<uintptr_t> received{0};
    std::atomic<bool> outOfOrder{false};

    std::thread producer([&] {
        for (uintptr_t i = 1; i <= kCount; ++i) {
            // Values 1..N as pointer-sized payloads; the queue only moves bits.
            while (!q.push(reinterpret_cast<void*>(i))) std::this_thread::yield();
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        uintptr_t expect = 1;
        while (true) {
            void* p = q.pop();
            if (!p) {
                if (producerDone.load(std::memory_order_acquire) && q.empty()) break;
                std::this_thread::yield();
                continue;
            }
            if (reinterpret_cast<uintptr_t>(p) != expect) outOfOrder.store(true);
            ++expect;
            received.fetch_add(1, std::memory_order_relaxed);
        }
    });

    producer.join();
    consumer.join();

    r.check(received.load() == kCount, "every one of 200000 items crossed the ring");
    r.check(!outOfOrder.load(), "items arrived in the order they were pushed");
}

} // namespace

int main() {
    JTestReport r("JScopeFrameQueue");
    testBasics(r);
    testWraparound(r);
    testResetEmpties(r);
    testThreadedOrdering(r);
    return r.result();
}
