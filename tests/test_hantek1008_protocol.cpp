#include "drivers/JHantek1008Protocol.h"
#include "drivers/JHantek1008Tables.h"
#include "support/JTestReport.h"
#include "support/JUsbTranscriptTransport.h"

#include <string>
#include <vector>

using namespace jf;

namespace {

// Timing zeroed: the delays are part of the protocol on real hardware, but they
// are the reference's seconds and there is no device here to observe them. A
// test that slept through them would take a minute to check byte sequences.
JHantek1008Protocol::JTiming instantTiming() {
    JHantek1008Protocol::JTiming t;
    t.beforeWrite = t.beforeRead = t.afterReset = 0.0;
    t.vscaleSettle = t.statusB5Settle = t.acquisitionSettle = 0.0;
    t.burstArmSettle = t.readyPollInterval = 0.0;
    return t;
}

// Every command is [opcode][parameters]. Checking the exact bytes is the whole
// point: this protocol is undocumented vendor magic transcribed from a Python
// reference, and "it ran without erroring" says nothing about whether the right
// bytes went out in the right order.
void testCommandFraming(JTestReport& r) {
    JUsbTranscriptTransport t;
    JHantek1008Protocol p(t, 0x02, 0x81);
    p.setTiming(instantTiming());

    t.queueEchoedReply(JHantek1008Tables::kPing);
    r.check(p.ping(), "ping succeeds");
    r.check(t.wroteExactly(0, { 0xf3 }), "ping is a bare 0xf3: " + t.writeHex(0));

    t.reset();
    t.queueEchoedReply(JHantek1008Tables::kSetTriggerLevel);
    r.check(p.setTriggerLevel(2048), "trigger level is accepted");
    // BIG-endian, unlike the samples, which are little-endian. Getting this
    // backwards would put the trigger at 0x0008 instead of 0x0800.
    r.check(t.wroteExactly(0, { 0xab, 0x08, 0x00 }),
            "trigger level 2048 is big-endian 08 00: " + t.writeHex(0));

    t.reset();
    t.queueEchoedReply(JHantek1008Tables::kSetTrigger);
    p.setTrigger(3, /*rising=*/false);
    r.check(t.wroteExactly(0, { 0xc1, 0x03, 0x01 }),
            "falling slope is 1, rising is 0: " + t.writeHex(0));

    t.reset();
    t.queueEchoedReply(JHantek1008Tables::kSetTimeDiv);
    p.setTimeDivId(18);
    r.check(t.wroteExactly(0, { 0xa3, 0x12 }), "time/div takes its id: " + t.writeHex(0));
}

void testActiveChannels(JTestReport& r) {
    JUsbTranscriptTransport t;
    JHantek1008Protocol p(t, 0x02, 0x81);
    p.setTiming(instantTiming());

    t.queueEchoedReply(JHantek1008Tables::kSetActiveChannelCount);
    t.queueEchoedReply(JHantek1008Tables::kSetActiveChannelMap);
    r.check(p.setActiveChannels({ 0, 1, 4 }), "setting active channels succeeds");

    // Two commands: a count, then an eight-byte map. Sending only one leaves the
    // device sampling a different number of channels than the map says.
    r.check(t.writes().size() == 2, "it takes two commands, not one");
    r.check(t.wroteExactly(0, { 0xa0, 0x03 }), "0xa0 carries the COUNT: " + t.writeHex(0));
    r.check(t.wroteExactly(1, { 0xaa, 1, 1, 0, 0, 1, 0, 0, 0 }),
            "0xaa carries an eight-byte map: " + t.writeHex(1));

    t.reset();
    r.check(!p.setActiveChannels({}), "no active channels is refused");
}

void testVerticalScales(JTestReport& r) {
    JUsbTranscriptTransport t;
    JHantek1008Protocol p(t, 0x02, 0x81);
    p.setTiming(instantTiming());

    t.queueEchoedReply(JHantek1008Tables::kSetVerticalScale);
    p.setVerticalScales({ 1.0, 0.125, 0.02 });
    // Always eight ids, whatever was passed: the command has a fixed shape and a
    // short one would leave five channels unset.
    r.check(t.wroteExactly(0, { 0xa2, 3, 2, 1, 3, 3, 3, 3, 3 }),
            "all eight scale ids are sent, defaulting to the widest: " + t.writeHex(0));
}

// An echo mismatch means the device is out of step, and continuing would send
// the next command into a conversation that has already gone wrong.
void testEchoMismatchIsDetected(JTestReport& r) {
    JUsbTranscriptTransport t;
    JHantek1008Protocol p(t, 0x02, 0x81);
    p.setTiming(instantTiming());

    t.queueReply({ 0x99 });                    // the wrong echo
    r.check(!p.ping(), "a wrong echo fails the command");
    r.check(p.lastError().find("echo") != std::string::npos,
            "and says so: " + p.lastError());
}

void testTransportFailurePropagates(JTestReport& r) {
    JUsbTranscriptTransport t;
    JHantek1008Protocol p(t, 0x02, 0x81);
    p.setTiming(instantTiming());
    t.setFailing(true);

    r.check(!p.ping(), "a transport failure fails the command");
    r.check(!p.lastError().empty(), "with a reason");
    r.check(!p.setActiveChannels({ 0 }), "and a compound command stops at the first failure");
}

// 0xa5 0x5a answers 0..3; 2 and 3 mean the record is ready.
void testReadyPoll(JTestReport& r) {
    JUsbTranscriptTransport t;
    JHantek1008Protocol p(t, 0x02, 0x81);
    p.setTiming(instantTiming());

    t.queueEchoedReply(JHantek1008Tables::kReadyPoll, { 0x00 });   // not yet
    t.queueEchoedReply(JHantek1008Tables::kPing);                  // the retry's ping
    t.queueEchoedReply(JHantek1008Tables::kReadyPoll, { 0x01 });   // still not
    t.queueEchoedReply(JHantek1008Tables::kPing);
    t.queueEchoedReply(JHantek1008Tables::kReadyPoll, { 0x02 });   // ready
    r.check(p.waitReady(10), "polling continues until the device reports ready");
    r.check(t.wroteExactly(0, { 0xa5, 0x5a }), "the poll is 0xa5 0x5a: " + t.writeHex(0));

    t.reset();
    for (int i = 0; i < 40; ++i) {
        t.queueEchoedReply(JHantek1008Tables::kReadyPoll, { 0x00 });
        t.queueEchoedReply(JHantek1008Tables::kPing);
    }
    r.check(!p.waitReady(5), "a device that never becomes ready gives up rather than hanging");
}

// Burst readout: 0xc6 gives a byte count, then ceil(count / 64) reads of 0xa6.
void testBurstReadout(JTestReport& r) {
    JUsbTranscriptTransport t;
    JHantek1008Protocol p(t, 0x02, 0x81);
    p.setTiming(instantTiming());

    // 100 bytes: two 64-byte reads, trimmed back to 100.
    t.queueReply({ 0x00, 0x64 });                       // big-endian length
    std::vector<uint8_t> chunk(64, 0xAB);
    t.queueReply(chunk);
    t.queueReply(chunk);

    std::vector<uint8_t> out;
    r.check(p.readBurstHalf(JHantek1008Tables::kBurstHalfA, out), "a burst half reads");
    r.check(t.wroteExactly(0, { 0xc6, 0x02 }), "the length query is 0xc6 + half");
    r.check(t.wroteExactly(1, { 0xa6, 0x02 }), "each chunk read is 0xa6 + half");
    r.check(t.writes().size() == 3, "100 bytes takes two 64-byte reads");
    r.check(out.size() == 100,
            "the result is trimmed to the announced length, not left padded to 128");
}

void testRollReadout(JTestReport& r) {
    JUsbTranscriptTransport t;
    JHantek1008Protocol p(t, 0x02, 0x81);
    p.setTiming(instantTiming());

    t.queueReply({ 0x01, 0x20 });                  // 288 bytes waiting
    uint16_t length = 0;
    r.check(p.rollReadyLength(length), "the roll length query succeeds");
    r.check(length == 288, "the length is big-endian: 01 20 is 288");
    r.check(t.wroteExactly(0, { 0xc7 }), "the query is a bare 0xc7");

    t.reset();
    for (int i = 0; i < 5; ++i) t.queueReply(std::vector<uint8_t>(64, 0x11));
    std::vector<uint8_t> out;
    r.check(p.readRollBytes(288, out), "reading the announced bytes succeeds");
    r.check(out.size() == 288,
            "a final partial packet is trimmed — the padding would otherwise become samples");
    r.check(t.wroteExactly(0, { 0xc8 }), "each read is a bare 0xc8");
}

// The initialisation sequence is the highest-risk part of this driver: opaque,
// undocumented, and only known to work as a whole. This asserts the exact
// opcode order against the reference.
void testInitialisationSequence(JTestReport& r) {
    JUsbTranscriptTransport t;
    JHantek1008Protocol p(t, 0x02, 0x81);
    p.setTiming(instantTiming());

    // Echo everything; the status blobs are read without an echo and the
    // transport returns zeros for them, which is exactly how an unprogrammed
    // status command should behave.
    for (int i = 0; i < 64; ++i) t.queueReply(std::vector<uint8_t>(64, 0x00));

    // Phase 1 needs real echoes for the commands that expect one.
    t.reset();
    auto echo = [&t](uint8_t op, std::vector<uint8_t> payload = {}) {
        t.queueEchoedReply(op, std::move(payload));
    };
    echo(0xb0); echo(0xb0); echo(0xf3);
    echo(0xb9); echo(0xb7); echo(0xbb);
    t.queueReply(std::vector<uint8_t>(64, 0));   // b5
    t.queueReply(std::vector<uint8_t>(64, 0));   // b6
    t.queueReply({ 0, 0 });                      // e5
    t.queueReply(std::vector<uint8_t>(64, 0));   // f7
    t.queueReply(std::vector<uint8_t>(64, 0));   // f8
    t.queueReply(std::vector<uint8_t>(56, 0));   // fa
    echo(0xf5);
    echo(0xa0); echo(0xaa);                      // active channels
    echo(0xa3);                                  // time/div
    echo(0xc1);                                  // trigger
    echo(0xa7, { 0x00 });
    echo(0xac);

    r.check(p.initialisePhase1(), "phase 1 completes");

    const std::vector<uint8_t> expected = {
        0xb0, 0xb0, 0xf3,                    // reset, reset, ping
        0xb9, 0xb7, 0xbb,                    // generator speed, enable, switch
        0xb5, 0xb6, 0xe5, 0xf7, 0xf8, 0xfa,  // status and per-unit calibration
        0xf5,
        0xa0, 0xaa,                          // all eight channels
        0xa3,                                // 500 us/div, the reference's default
        0xc1,                                // trigger on channel 0, rising
        0xa7, 0xac,
    };
    const auto got = t.opcodes();
    bool matches = (got.size() == expected.size());
    size_t firstDiff = 0;
    for (size_t i = 0; matches && i < got.size(); ++i)
        if (got[i] != expected[i]) { matches = false; firstDiff = i; }

    if (!matches) {
        std::string detail = " (got " + std::to_string(got.size()) + " commands, expected "
                           + std::to_string(expected.size());
        if (firstDiff < got.size() && firstDiff < expected.size()) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "; first difference at %zu: 0x%02x vs 0x%02x",
                          firstDiff, got[firstDiff], expected[firstDiff]);
            detail += buf;
        }
        detail += ")";
        r.check(false, "phase 1 emits the reference's exact opcode sequence" + detail);
    } else {
        r.check(true, "phase 1 emits the reference's exact opcode sequence");
    }

    // The two resets really are two: the device needs the first, a pause, then
    // the second, and sending one would leave it unready in a way that only
    // shows up later.
    r.check(got.size() >= 2 && got[0] == 0xb0 && got[1] == 0xb0,
            "the reset is sent twice, as the reference does");
}

// Roll mode's 0xa3 must carry a ROLL RATE id, not a ns/div id. The device
// stalls its bulk OUT endpoint otherwise, and the reference carries a comment
// saying exactly that. It cost a bench session to rediscover, so it is pinned
// here where it costs nothing.
void testRollModeStartOrder(JTestReport& r) {
    JUsbTranscriptTransport t;
    JHantek1008Protocol p(t, 0x02, 0x81);
    p.setTiming(instantTiming());

    t.queueEchoedReply(0xa3);
    t.queueEchoedReply(0xf3);
    t.queueEchoedReply(0xa4);
    t.queueEchoedReply(0xc0);
    t.queueEchoedReply(0xc2);

    const uint8_t rateId = JHantek1008Tables::rollRateIdFor(440.0);
    r.check(p.startRollMode(rateId), "roll mode starts");
    r.check(rateId == 0x18, "440 Sa/s is roll rate id 0x18");

    const std::vector<uint8_t> expected = { 0xa3, 0xf3, 0xa4, 0xc0, 0xc2 };
    r.check(t.opcodes() == expected,
            "roll starts with the rate id, a ping, the mode, then arm");
    r.check(t.wroteExactly(0, { 0xa3, 0x18 }),
            "0xa3 carries the ROLL RATE id, not a ns/div id: " + t.writeHex(0));
    r.check(t.wroteExactly(2, { 0xa4, 0x02 }), "0xa4 selects roll mode: " + t.writeHex(2));
}

void testBurstCaptureOrder(JTestReport& r) {
    // The per-frame sequence Scope.exe sends, in the order a usbmon capture of
    // the OEM shows it:  e4 01, e6 01, f3, e4 01, e6 01, a4 01, c0  — and c2
    // only when the trigger is being forced.
    {
        JUsbTranscriptTransport t;
        JHantek1008Protocol p(t, 0x02, 0x81);
        p.setTiming(instantTiming());

        t.queueEchoedReply(0xe4);
        t.queueReply(std::vector<uint8_t>(10, 0));     // e6, no echo
        t.queueEchoedReply(0xf3);
        t.queueEchoedReply(0xe4);
        t.queueReply(std::vector<uint8_t>(10, 0));     // e6, no echo
        t.queueEchoedReply(0xa4);
        t.queueEchoedReply(0xc0);
        t.queueEchoedReply(0xc2);

        r.check(p.startBurstCapture(true), "a forced burst capture starts");
        const std::vector<uint8_t> expected =
            { 0xe4, 0xe6, 0xf3, 0xe4, 0xe6, 0xa4, 0xc0, 0xc2 };
        r.check(t.opcodes() == expected,
                "burst follows the OEM's per-capture sequence");
        r.check(t.wroteExactly(5, { 0xa4, 0x01 }),
                "0xa4 selects burst mode: " + t.writeHex(5));

        // Nothing configures the device per frame. Re-sending 0xa3 here is what
        // cleared the trigger selection and made the trigger source look dead.
        for (uint8_t op : t.opcodes())
            r.check(op != 0xa3 && op != 0xc1,
                    "no configuration is re-sent per capture");
    }

    // A Normal sweep must NOT get 0xc2. Forcing the trigger on every capture is
    // what made the trace jump instead of standing still on the trigger edge.
    {
        JUsbTranscriptTransport t;
        JHantek1008Protocol p(t, 0x02, 0x81);
        p.setTiming(instantTiming());

        t.queueEchoedReply(0xe4);
        t.queueReply(std::vector<uint8_t>(10, 0));
        t.queueEchoedReply(0xf3);
        t.queueEchoedReply(0xe4);
        t.queueReply(std::vector<uint8_t>(10, 0));
        t.queueEchoedReply(0xa4);
        t.queueEchoedReply(0xc0);

        r.check(p.startBurstCapture(false), "an unforced burst capture starts");
        const std::vector<uint8_t> expected =
            { 0xe4, 0xe6, 0xf3, 0xe4, 0xe6, 0xa4, 0xc0 };
        r.check(t.opcodes() == expected,
                "an unforced capture arms with 0xc0 and does NOT force with 0xc2");
    }
}


// The generator's wire format. Every byte here came from the Python reference and
// none of it is documented, so exact-byte checks are the only thing standing
// between a refactor and eight outputs doing something unintended on a vehicle.
void testGeneratorCommands(JTestReport& r) {
    JUsbTranscriptTransport t;
    JHantek1008Protocol p(t, 0x02, 0x81);
    p.setTiming(instantTiming());

    // Output on: the 0xb7 0x00 prologue, then 0xbb 0x08 0x01.
    t.queueEchoedReply(JHantek1008Tables::kGeneratorEnable);
    t.queueEchoedReply(JHantek1008Tables::kGeneratorSwitch);
    r.check(p.setGeneratorOutput(true), "generator output on is accepted");
    r.check(t.wroteExactly(0, { 0xb7, 0x00 }), "prologue is b7 00: " + t.writeHex(0));
    r.check(t.wroteExactly(1, { 0xbb, 0x08, 0x01 }), "switch on is bb 08 01: " + t.writeHex(1));

    t.reset();
    t.queueEchoedReply(JHantek1008Tables::kGeneratorEnable);
    t.queueEchoedReply(JHantek1008Tables::kGeneratorSwitch);
    r.check(p.setGeneratorOutput(false), "generator output off is accepted");
    r.check(t.wroteExactly(1, { 0xbb, 0x08, 0x00 }), "switch off is bb 08 00: " + t.writeHex(1));

    // Speed. LITTLE-endian, where the trigger level on the same wire is big-endian.
    // 1200 = 0x000004B0, so the bytes are B0 04 00 00 -- and that is the exact
    // value the reference asserts for 300000 rpm over an 8-step pattern.
    t.reset();
    t.queueEchoedReply(JHantek1008Tables::kGeneratorSpeed);
    r.check(p.setGeneratorPulseLength(1200), "pulse length is accepted");
    r.check(t.wroteExactly(0, { 0xb9, 0x01, 0xb0, 0x04, 0x00, 0x00 }),
            "speed is b9 01 b0 04 00 00: " + t.writeHex(0));

    r.check(!p.setGeneratorPulseLength(0), "a zero pulse length is refused");

    // Pattern: prologue, length little-endian, then a RUN OF CHUNKS indexed from
    // one, each a full 62 bytes with the tail zero padded.
    t.reset();
    t.queueEchoedReply(JHantek1008Tables::kGeneratorEnable);
    t.queueEchoedReply(JHantek1008Tables::kGeneratorLength);
    t.queueEchoedReply(JHantek1008Tables::kGeneratorWaveform);
    r.check(p.setGeneratorPattern({ 0xf0, 0x0f, 0xf0, 0x0f }), "a four-step pattern is accepted");
    r.check(t.wroteExactly(1, { 0xbf, 0x04, 0x00 }), "length is bf 04 00: " + t.writeHex(1));

    std::vector<uint8_t> expected = { 0xb8, 0x01, 0xf0, 0x0f, 0xf0, 0x0f };
    expected.resize(2 + JHantek1008Tables::kPatternBytesPerChunk, 0x00);
    r.check(t.wroteExactly(2, expected),
            "the single chunk is b8 01 then 62 bytes, zero padded: " + t.writeHex(2));

    // MORE THAN ONE CHUNK. The byte after the opcode is the chunk index, and
    // treating it as a constant 0x01 is what limited the driver to 62 pulses.
    // Captured off the OEM: 1440 pulses go out as 24 packets, 0x01 to 0x18.
    t.reset();
    t.queueEchoedReply(JHantek1008Tables::kGeneratorEnable);
    t.queueEchoedReply(JHantek1008Tables::kGeneratorLength);
    for (int i = 0; i < 24; ++i) t.queueEchoedReply(JHantek1008Tables::kGeneratorWaveform);

    std::vector<uint8_t> full(JHantek1008Tables::kMaxPatternLength);
    for (size_t i = 0; i < full.size(); ++i) full[i] = static_cast<uint8_t>(i & 0xff);
    r.check(p.setGeneratorPattern(full), "the device's full 1440-pulse pattern is accepted");
    r.check(t.wroteExactly(1, { 0xbf, 0xa0, 0x05 }),
            "length is bf a0 05, little-endian 1440: " + t.writeHex(1));
    r.check(t.writes().size() == 26, "prologue, length, then 24 chunks");

    // First and last chunk indices, and the tail padded rather than sent short.
    r.check(t.writes()[2][1] == 0x01,  "the first chunk is index 0x01");
    r.check(t.writes()[25][1] == 0x18, "the last chunk is index 0x18 (24)");
    r.check(t.writes()[25].size() == 2 + JHantek1008Tables::kPatternBytesPerChunk,
            "the last chunk is a full packet even though it carries 14 real bytes");
    r.check(t.writes()[25][2 + 14] == 0x00, "and its tail is zero padded");

    r.check(!p.setGeneratorPattern({}), "an empty pattern is refused");
    r.check(!p.setGeneratorPattern(std::vector<uint8_t>(
                JHantek1008Tables::kMaxPatternLength + 1, 0x01)),
            "a pattern longer than the device holds is refused rather than truncated");
}

// The speed the device can actually run, which is not the speed asked for. The
// OEM shows both for this reason, and the arithmetic is the whole reason it must.
void testGeneratorSpeedQuantisation(JTestReport& r) {
    using T = JHantek1008Tables;

    // The reference's own assertion: 300000 rpm over 8 steps is a pulse of 1200.
    r.check(T::pulseLengthFor(300000, 8) == 1200,
            "300000 rpm over 8 steps is a pulse length of 1200");

    // A speed that divides exactly comes back unchanged.
    r.check(T::rpmForPulseLength(T::pulseLengthFor(600, 8), 8) == 600,
            "600 rpm over 8 steps is exactly reachable");

    // A pulse length is whole ticks, so the round trip is a floor, not identity.
    // Asserting only the exact case would hide the very thing this models.
    bool sawQuantised = false;
    for (uint32_t rpm = 599; rpm <= 100000 && !sawQuantised; ++rpm)
        if (T::rpmForPulseLength(T::pulseLengthFor(rpm, 8), 8) != rpm) sawQuantised = true;
    r.check(sawQuantised, "some speeds are not exactly reachable, as the hardware implies");

    // Longer pattern, same revolution: the pulse length must fall.
    r.check(T::pulseLengthFor(600, 16) < T::pulseLengthFor(600, 8),
            "more steps in a revolution means a shorter pulse for the same speed");

    r.check(T::pulseLengthFor(0, 8) == 0,  "zero rpm yields no pulse length");
    r.check(T::pulseLengthFor(600, 0) == 0, "a pattern with no steps yields no pulse length");
}

void testRollRateIds(JTestReport& r) {
    r.check(JHantek1008Tables::rollRateIdFor(440.0) == 0x18, "440 Sa/s is 0x18");
    r.check(JHantek1008Tables::rollRateIdFor(1.0)   == 0x20, "1 Sa/s is 0x20");
    r.check(JHantek1008Tables::rollRateIdFor(1.0/16) == 0x24, "1/16 Sa/s is 0x24");
    r.check(JHantek1008Tables::rollRateIdFor(1000.0) == 0x18,
            "a rate above the maximum clamps to the fastest, not to nothing");
}

} // namespace

int main() {
    JTestReport r("JHantek1008Protocol");
    testCommandFraming(r);
    testGeneratorCommands(r);
    testGeneratorSpeedQuantisation(r);
    testActiveChannels(r);
    testVerticalScales(r);
    testEchoMismatchIsDetected(r);
    testTransportFailurePropagates(r);
    testReadyPoll(r);
    testBurstReadout(r);
    testRollReadout(r);
    testInitialisationSequence(r);
    testRollModeStartOrder(r);
    testBurstCaptureOrder(r);
    testRollRateIds(r);
    return r.result();
}
