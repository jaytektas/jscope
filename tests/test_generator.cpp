#include "drivers/JHantek1008PatternGenerator.h"
#include "drivers/JHantek1008Protocol.h"
#include "drivers/JHantek1008Tables.h"
#include "support/JTestReport.h"
#include "support/JUsbTranscriptTransport.h"

#include <mutex>
#include <vector>

using namespace jf;

namespace {

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

// A pattern the user lengthens must not silently keep a speed the device can no
// longer reach. This is the case the OEM shows as a moving maximum.
void testCeilingFallsAsTheWheelGrows(JTestReport& r) {
    using T = JHantek1008Tables;
    uint32_t previous = T::maxRpmFor(1);
    for (uint32_t pulses = 2; pulses <= T::kMaxPatternPerPacket; ++pulses) {
        const uint32_t now = T::maxRpmFor(pulses);
        if (now > previous) { r.check(false, "the ceiling never rises as pulses are added"); return; }
        previous = now;
    }
    r.check(true, "the ceiling falls monotonically as the pattern gains pulses");
}

} // namespace

int main() {
    JTestReport r("generator");
    testSettersDoNotTouchTheDevice(r);
    testFlushDoesNotAbandonASequence(r);
    testSpeedCeilingMatchesTheInstrument(r);
    testCeilingFallsAsTheWheelGrows(r);
    return r.result();
}
