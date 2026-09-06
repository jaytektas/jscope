#include "scope/JCrankWheel.h"
#include "drivers/JHantek1008Tables.h"
#include "support/JTestReport.h"

#include <vector>

using namespace jf;

namespace {

// The wheel is what the user actually sets, so its translation into steps is the
// part that decides what appears on a probe. All of it is arithmetic, none of it
// needs a device, and getting it wrong produces a plausible-looking signal that
// is simply the wrong wheel — which is the kind of bug a bench test never catches
// because the trace looks fine.
void testWheelPattern(JTestReport& r) {
    // A tooth is a high step then a low step: that pair is what gives the sensor
    // an edge on each side of the tooth.
    JCrankWheel w{ 3, 0, 0x01 };
    const auto p = w.pattern();
    r.check(p.size() == 6, "three teeth occupy six steps");
    r.check(p == std::vector<uint8_t>({ 0x01, 0x00, 0x01, 0x00, 0x01, 0x00 }),
            "each tooth is one step high then one low");

    // The gap is what makes a crank signal identifiable — the missing teeth are
    // consecutive and at the end, so the pattern has one long low period.
    JCrankWheel gapped{ 4, 2, 0x01 };
    const auto g = gapped.pattern();
    r.check(g.size() == 8, "the wheel keeps its full circumference when teeth are missing");
    r.check(g == std::vector<uint8_t>({ 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 }),
            "missing teeth leave a gap rather than shortening the revolution");

    // The mask is which lines carry the wheel, and it goes through untouched.
    JCrankWheel multi{ 2, 0, 0b1010'0001 };
    r.check(multi.pattern()[0] == 0b1010'0001, "every selected output carries the tooth");
    r.check(multi.pattern()[1] == 0x00,        "and all of them fall together");

    // A wheel with every tooth removed would emit nothing at all, which looks
    // exactly like the generator being switched off. One tooth always survives.
    JCrankWheel stripped{ 3, 99, 0x01 };
    const auto s = stripped.pattern();
    r.check(s.size() == 6, "an over-stripped wheel keeps its size");
    r.check(s[0] == 0x01, "and keeps one tooth, so it is still visibly running");

    r.check(JCrankWheel{ 0, 0, 0x01 }.pattern().empty(), "a wheel with no teeth has no pattern");
}

// The wheel has to fit what the driver can actually write, which is one packet.
void testWheelFitsTheDevice(JTestReport& r) {
    const uint32_t budget = JHantek1008Tables::kMaxPatternPerPacket;
    const uint32_t maxTeeth = JCrankWheel::maxTeethIn(budget);

    r.check(maxTeeth == 31, "62 steps is 31 teeth");
    r.check(JCrankWheel::stepsFor(maxTeeth) <= budget,
            "the largest wheel fits inside one packet");
    r.check(JCrankWheel::stepsFor(maxTeeth + 1) > budget,
            "and one tooth more would not");

    JCrankWheel widest{ maxTeeth, 2, 0x01 };
    r.check(widest.pattern().size() <= budget,
            "the widest wheel the panel offers is a pattern the protocol will accept");

    r.check(JCrankWheel::maxTeethIn(1) == 0, "a budget below one tooth holds no wheel");
}

// Speed depends on the wheel, because one revolution is one pass of the pattern.
// This is the trap the OEM's separate "Set Speed"/"Real Speed" readouts exist for.
void testSpeedFollowsTheWheel(JTestReport& r) {
    using T = JHantek1008Tables;

    const uint32_t small = JCrankWheel::stepsFor(8);
    const uint32_t big   = JCrankWheel::stepsFor(31);
    r.check(T::pulseLengthFor(600, big) < T::pulseLengthFor(600, small),
            "a wheel with more teeth needs a shorter pulse to turn at the same speed");

    // Quantisation is a function of how SHORT the pulse is, not of the speed as
    // such. At an idle the pulse is tens of thousands of ticks and the rounding is
    // invisible, so both wheels land exactly -- asserting a difference there would
    // be asserting something untrue.
    const uint32_t idle = 1234;
    r.check(T::rpmForPulseLength(T::pulseLengthFor(idle, small), small) == idle &&
            T::rpmForPulseLength(T::pulseLengthFor(idle, big), big) == idle,
            "at idle speeds the rounding is below one rpm on any wheel");

    // Wind it up until a pulse is only a few hundred ticks and the wheel starts to
    // decide what is reachable. THIS is why the achievable speed has to be re-read
    // when the wheel changes rather than remembered from when it was set.
    const uint32_t fast = 100000;
    const uint32_t a = T::rpmForPulseLength(T::pulseLengthFor(fast, small), small);
    const uint32_t b = T::rpmForPulseLength(T::pulseLengthFor(fast, big), big);
    r.check(a != 0 && b != 0, "both wheels can run at that speed");
    r.check(a != b, "but land on different achievable speeds, so it must be re-read");
    r.check(a == fast, "the 16-step wheel divides exactly at that speed");
    r.check(b != fast, "the 62-step one does not, and saying otherwise would misreport it");
}

} // namespace

int main() {
    JTestReport r("generator");
    testWheelPattern(r);
    testWheelFitsTheDevice(r);
    testSpeedFollowsTheWheel(r);
    return r.result();
}
