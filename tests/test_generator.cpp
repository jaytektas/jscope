#include "scope/JCrankWheel.h"
#include "scope/JGeneratorSignal.h"
#include "drivers/JHantek1008PatternGenerator.h"
#include "drivers/JHantek1008Protocol.h"
#include "drivers/JHantek1008Tables.h"
#include "support/JTestReport.h"
#include "support/JUsbTranscriptTransport.h"

#include <mutex>
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

// THE BUG THIS EXISTS TO PREVENT.
//
// The generator used to write to the device from whichever thread moved the
// control. The acquisition thread holds no lock while it runs a capture -- it
// copies the configuration and releases -- so those writes landed inside another
// transaction: a pattern write during a running capture read back 0x1c, a byte of
// somebody else's answer, and every read afterwards timed out.
//
// The rule is that a setter records and nothing more. It is checked by counting
// bytes on the wire, because that is the thing that was wrong; asserting on a
// dirty flag would pass just as happily with the sends put back.
void testSettersDoNotTouchTheDevice(JTestReport& r) {
    JUsbTranscriptTransport t;
    JHantek1008Protocol p(t, 0x02, 0x81);
    std::mutex deviceMutex;
    JHantek1008PatternGenerator g(deviceMutex);
    g.attach(&p);

    r.check(g.setRpm(900),                       "a speed is accepted");
    r.check(g.setPattern({ 0x01, 0x00 }),        "a pattern is accepted");
    r.check(g.setOutputEnabled(true),            "an output switch is accepted");
    r.check(t.writes().empty(),
            "and NOTHING went to the device: the caller's thread does not own the pipe");
    r.check(g.isDirty(), "the change is remembered as pending instead");

    // flush() is what sends, and it is called only where the pipe is owned.
    for (uint8_t op : { JHantek1008Tables::kGeneratorEnable, JHantek1008Tables::kGeneratorLength,
                        JHantek1008Tables::kGeneratorWaveform, JHantek1008Tables::kGeneratorSpeed,
                        JHantek1008Tables::kGeneratorEnable, JHantek1008Tables::kGeneratorSwitch })
        t.queueEchoedReply(op);

    r.check(g.flush(), "flush sends the whole state");
    r.check(!t.writes().empty(), "and only then do bytes appear on the wire");
    r.check(!g.isDirty(), "after which nothing is pending");
}

// A pattern write is three commands. Stopping between them leaves the device
// waiting for the rest, which is how one bad reply became a pipe that timed out
// on everything afterwards.
void testFlushDoesNotAbandonASequence(JTestReport& r) {
    JUsbTranscriptTransport t;
    JHantek1008Protocol p(t, 0x02, 0x81);
    std::mutex deviceMutex;
    JHantek1008PatternGenerator g(deviceMutex);
    g.attach(&p);

    // Answer the first command wrongly, so the pattern write fails at its start.
    t.queueEchoedReply(JHantek1008Tables::kGeneratorSpeed);   // wrong echo for 0xb7
    for (int i = 0; i < 8; ++i) t.queueEchoedReply(JHantek1008Tables::kGeneratorSwitch);

    const bool ok = g.flush();
    r.check(!ok, "a refused command is reported as a failure");
    r.check(t.writes().size() > 1,
            "but the sequence still runs to the end rather than leaving the device mid-command");
}

// Eight digital lines are only readable if they do not sit on top of each other.
// At 5 V/div a 0-5 V swing is exactly one division, so eight of them fill an
// eight-division graticule with one lane each and no overlap -- which is how the
// OEM's generator window shows them.
void testLanesStackWithoutOverlap(JTestReport& r) {
    constexpr uint8_t kDivisions = 8;
    constexpr double  kSwing     = JGeneratorSignal::kHighVolts - JGeneratorSignal::kLowVolts;

    r.check(JGeneratorSignal::kVoltsPerDiv == kSwing,
            "one output's full swing is exactly one division");

    // CH1 on top, CH8 at the bottom, in numbering order.
    const double first = JGeneratorSignal::lanePosition(0, kDivisions);
    const double last  = JGeneratorSignal::lanePosition(7, kDivisions);
    r.check(first > last, "CH1 sits above CH8");

    // Adjacent lanes are exactly one division apart: any less and they overlap,
    // any more and eight of them do not fit.
    for (uint8_t i = 0; i + 1 < kDivisions; ++i) {
        const double gap = JGeneratorSignal::lanePosition(i, kDivisions) -
                           JGeneratorSignal::lanePosition(i + 1, kDivisions);
        if (gap != JGeneratorSignal::kVoltsPerDiv) {
            r.check(false, "lanes are one division apart");
            return;
        }
    }
    r.check(true, "every adjacent pair of lanes is exactly one division apart");

    // And the whole stack lands inside the graticule rather than half off it.
    const double halfSpan = (kDivisions / 2.0) * JGeneratorSignal::kVoltsPerDiv;
    r.check(first + kSwing <= halfSpan,  "the top lane's high level is on screen");
    r.check(last >= -halfSpan,           "the bottom lane's low level is on screen");
}

// The speed ceiling, checked against the OEM's own behaviour.
//
// These numbers were read off the OEM by driving it: with the pattern at its
// full 1440 steps, asking for 67,999,999 rpm gave a Real Speed of 30,321, and at
// 10 steps it allows 4,266,282. The earlier model assumed a pulse could be one
// clock tick and so put the ceiling ~66x too high -- it would have accepted
// 750,000 rpm on a pattern the device tops out at thirty thousand for.
void testSpeedCeilingMatchesTheInstrument(JTestReport& r) {
    using T = JHantek1008Tables;

    // Within a rounding of the observed 30,321.
    const uint32_t at1440 = T::maxRpmFor(1440);
    r.check(at1440 > 30000 && at1440 < 30600,
            "a 1440-step pattern tops out near the 30,321 rpm the OEM reports");

    // The ceiling is a rate limit, so halving the pattern doubles it.
    r.check(T::maxRpmFor(720) == 2 * T::maxRpmFor(1440) ||
            T::maxRpmFor(720) == 2 * T::maxRpmFor(1440) + 1,
            "halving the pattern doubles the ceiling: it is a step-rate limit");

    // Short patterns hit the OTHER limit first -- the encoding this driver cannot
    // produce -- so the ceiling stops rising rather than running away.
    r.check(T::maxRpmFor(8) == T::kMaxEncodableRpm,
            "a short pattern is capped by the encoding, not the step rate");
    r.check(T::maxRpmFor(62) < T::kMaxEncodableRpm,
            "the longest pattern this driver can write is capped by the step rate");

    r.check(T::maxRpmFor(0) == 0, "a pattern with no steps has no speed");
}

// A wheel the user grows must not silently keep a speed the device can no longer
// reach. This is the case the OEM shows as a moving maximum.
void testCeilingFallsAsTheWheelGrows(JTestReport& r) {
    using T = JHantek1008Tables;
    uint32_t previous = T::maxRpmFor(JCrankWheel::stepsFor(1));
    for (uint32_t teeth = 2; teeth <= 31; ++teeth) {
        const uint32_t now = T::maxRpmFor(JCrankWheel::stepsFor(teeth));
        if (now > previous) { r.check(false, "the ceiling never rises as teeth are added"); return; }
        previous = now;
    }
    r.check(true, "the ceiling falls monotonically as the wheel gains teeth");
}

} // namespace

int main() {
    JTestReport r("generator");
    testWheelPattern(r);
    testWheelFitsTheDevice(r);
    testSpeedFollowsTheWheel(r);
    testSettersDoNotTouchTheDevice(r);
    testFlushDoesNotAbandonASequence(r);
    testLanesStackWithoutOverlap(r);
    testSpeedCeilingMatchesTheInstrument(r);
    testCeilingFallsAsTheWheelGrows(r);
    return r.result();
}
