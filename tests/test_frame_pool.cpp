// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "scope/JScopeFramePool.h"
#include "support/JTestReport.h"

using namespace jf;

namespace {

void testProvision(JTestReport& r) {
    JScopeFramePool pool;
    const size_t depth = pool.provision(4, 1024, 6);
    r.check(depth == 6, "provisions the requested depth when it fits the budget");
    r.check(pool.depth() == 6, "holds that many frames");
    r.check(pool.freeCount() == 6, "all frames start free");
    r.check(pool.readyCount() == 0, "nothing is ready yet");
    r.check(pool.dropped() == 0, "no drops on a fresh pool");
}

// A DSO deep record is 8 Mpts x 2 ch x 2 B = 32 MB. A naive depth of 8 would be
// a quarter of a gigabyte, so the pool must clamp itself rather than trust the
// caller's arithmetic.
void testBudgetClamp(JTestReport& r) {
    JScopeFramePool pool;
    const size_t frameBytes = static_cast<size_t>(2) * 1000000 * sizeof(int16_t);
    const size_t budget     = frameBytes * 3;
    const size_t depth      = pool.provision(2, 1000000, 16, budget);
    r.check(depth == 3, "depth is clamped to what the budget affords");
    r.check(depth >= 2, "never clamps below two — one in flight, one being filled");
}

void testAcquirePublishRelease(JTestReport& r) {
    JScopeFramePool pool;
    pool.provision(2, 64, 4);

    JScopeFrame* f = pool.acquire();
    r.check(f != nullptr, "acquire hands out a frame");
    r.check(pool.freeCount() == 3, "acquire takes it off the free ring");

    r.check(f->shape(2, 64), "shape within the provisioned capacity succeeds");
    r.check(!f->shape(3, 64), "shape beyond the channel capacity is refused");
    r.check(!f->shape(2, 65), "shape beyond the sample capacity is refused");

    pool.publish(f);
    r.check(pool.readyCount() == 1, "publish moves it to the ready ring");

    JScopeFrame* got = pool.tryPopReady();
    r.check(got == f, "the consumer gets back the same frame");
    pool.release(got);
    r.check(pool.freeCount() == 4, "release returns it to the free ring");
}

// Backpressure is drop-oldest and must never block: a stalled UI cannot be
// allowed to stall USB. The drop is counted so it is visible, not silent.
void testDropOldest(JTestReport& r) {
    JScopeFramePool pool;
    const size_t depth = pool.provision(1, 16, 3);

    std::vector<JScopeFrame*> published;
    for (size_t i = 0; i < depth; ++i) {
        JScopeFrame* f = pool.acquire();
        f->shape(1, 16);
        f->header.sequence = i;
        pool.publish(f);
        published.push_back(f);
    }
    r.check(pool.freeCount() == 0, "pool is dry with every frame published");

    JScopeFrame* extra = pool.acquire();
    r.check(extra != nullptr, "acquire on a dry pool still returns a frame");
    r.check(pool.dropped() == 1, "and counts exactly one drop");
    r.check(extra == published.front(), "the frame reclaimed is the OLDEST published one");

    pool.resetDropped();
    r.check(pool.dropped() == 0, "the drop counter resets");
}

// The whole point of the pool: steady-state acquisition allocates nothing.
void testNoSteadyStateAllocation(JTestReport& r) {
    JScopeFramePool pool;
    pool.provision(4, 2048, 4);

    JScopeFrame* probe = pool.acquire();
    const size_t capacityBefore = probe->capacity();
    pool.publish(probe);
    pool.release(pool.tryPopReady());

    for (int i = 0; i < 100000; ++i) {
        JScopeFrame* f = pool.acquire();
        f->shape(4, 2048);
        pool.publish(f);
        pool.release(pool.tryPopReady());
    }

    JScopeFrame* after = pool.acquire();
    r.check(after->capacity() == capacityBefore,
            "100000 acquire/publish/release cycles leave capacity untouched");
    pool.release(after);
}

void testVoltsTransform(JTestReport& r) {
    JScopeFramePool pool;
    pool.provision(1, 8, 2);
    JScopeFrame* f = pool.acquire();
    f->shape(1, 8);
    f->header.zeroOffsetCounts[0] = 2048.0f;
    f->header.countsToVolts[0]    = 0.001f;
    f->plane(0)[0] = 2048;
    f->plane(0)[1] = 3048;
    f->plane(0)[2] = 1048;
    r.check(f->voltsAt(0, 0) == 0.0f,   "a count at the zero offset is 0 V");
    r.check(f->voltsAt(0, 1) == 1.0f,   "+1000 counts at 1 mV/count is +1 V");
    r.check(f->voltsAt(0, 2) == -1.0f,  "-1000 counts is -1 V");
    pool.release(f);
}

} // namespace

int main() {
    JTestReport r("JScopeFramePool");
    testProvision(r);
    testBudgetClamp(r);
    testAcquirePublishRelease(r);
    testDropOldest(r);
    testNoSteadyStateAllocation(r);
    testVoltsTransform(r);
    return r.result();
}
