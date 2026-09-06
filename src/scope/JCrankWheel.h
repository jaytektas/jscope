#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

inline namespace jf {

// A TOOTHED WHEEL, as a pattern the digital outputs can play.
//
// The 1008C's generator takes an arbitrary pattern -- one byte per step, bit i
// driving output i -- and exposing that raw would be faithful and nearly useless:
// what people connect this to is a crank or cam sensor input, and what those
// expect is a wheel with a tooth count and a gap where some teeth are missing.
// The 60-2 wheel most engines use is the canonical example.
//
// A tooth is TWO steps, high then low, because a sensor sees an edge at each side
// of it. So a wheel costs twice its tooth count in steps, and the device's step
// budget buys half as many teeth as it might appear to.
//
// Headless on purpose: this is what a wheel IS, not how it is presented, and it
// is the part worth testing.
struct JCrankWheel {
    uint32_t teeth{1};
    uint32_t missing{0};      // consecutive teeth removed, making the index gap
    uint8_t  outputMask{1};   // which of the eight lines carry it

    static constexpr uint32_t kStepsPerTooth = 2;

    // Steps a wheel of this many teeth occupies, whatever else is set.
    static constexpr uint32_t stepsFor(uint32_t toothCount) {
        return toothCount * kStepsPerTooth;
    }

    // The largest wheel that fits a given step budget. Whole teeth only -- half a
    // tooth is not something a wheel can have.
    static constexpr uint32_t maxTeethIn(uint32_t stepBudget) {
        return (stepBudget < kStepsPerTooth) ? 0 : stepBudget / kStepsPerTooth;
    }

    std::vector<uint8_t> pattern() const {
        if (teeth == 0) return {};
        // A wheel with every tooth removed emits nothing at all, which is
        // indistinguishable from the generator being off and is never what was
        // meant. One tooth always survives.
        const uint32_t gone = std::min(missing, teeth - 1);

        std::vector<uint8_t> out(stepsFor(teeth), 0x00);
        for (uint32_t i = 0; i < teeth - gone; ++i)
            out[i * kStepsPerTooth] = outputMask;   // the second step of the pair
        return out;                                 // stays low: that is the edge
    }
};

} // inline namespace jf
