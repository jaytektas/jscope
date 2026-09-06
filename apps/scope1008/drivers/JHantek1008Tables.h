#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Facts about the Hantek 1008C, transcribed from the reverse-engineered Python
// reference at tools/hantek1008py/hantek1008.py.
//
// These are DEVICE DATA, not visual constants: the theme rule that governs the
// UI layer does not reach them, and putting them anywhere else would separate
// them from the protocol code that is the only thing entitled to interpret them.

inline namespace jf {

struct JHantek1008Tables {
    static constexpr uint16_t kVendorId  = 0x0783;
    static constexpr uint16_t kProductId = 0x5725;
    static constexpr int      kInterfaceNumber = 0;

    static constexpr uint8_t  kChannelCount = 8;
    static constexpr uint8_t  kAdcBits      = 12;
    static constexpr int32_t  kCountsMin    = 0;
    static constexpr int32_t  kCountsMax    = 4095;
    static constexpr int32_t  kCountsSpan   = kCountsMax - kCountsMin + 1;

    // The device negotiates FULL SPEED with 64-byte packets, and every bulk read
    // is one of them. This is the number behind the throughput ceiling.
    static constexpr size_t   kMaxPacketSize = 64;

    // Volts per count at vscale 1.0. volts = (raw - zeroOffset) * kVoltsPerCount
    // * vscale, so full scale at vscale 1.0 is +/-20.48 V — an automotive range,
    // which is what this instrument is for.
    static constexpr double   kVoltsPerCount = 0.01;

    // The graticule the V/div figures below are defined against.
    static constexpr uint8_t  kVerticalDivisions   = 8;
    static constexpr uint8_t  kHorizontalDivisions = 10;

    // The reference's comment says a burst division holds "around 25 samples".
    // The unit on this bench returns 1000 samples across 10 divisions, which is
    // 100 — so this figure is NOT used to derive a time axis. The device states
    // the real record length on every capture and the interval is computed from
    // that; believing the comment put a 1 kHz signal at a steady, confident and
    // completely wrong 249.99 Hz.
    static constexpr uint32_t kReferenceSamplesPerDivClaim = 25;

    // The device has no volts/div: it has three full-scale RANGES. The HAL
    // normalises to V/div because both instruments are read on a graticule and
    // the user turns one knob — the arithmetic is stated once, here.
    static constexpr std::array<double, 3> kVScaleFactors{ 0.02, 0.125, 1.0 };

    // The largest volts/div a hardware range can show without clipping: the
    // full count span converted to volts, spread over the graticule.
    static constexpr double maxVoltsPerDivFor(double vscale) {
        return (kCountsSpan * kVoltsPerCount * vscale) / kVerticalDivisions;
    }

    // The volts/div STEPS OFFERED TO THE USER — a 1-2-5 ladder, because that is
    // what a scope has and what anyone reading a graticule expects.
    //
    // The hardware does not work in these steps. It has three ranges, whose
    // maxima are 0.1024, 0.64 and 5.12 V/div — ADC arithmetic rather than scope
    // numbers. Every modern scope resolves this the same way: coarse analogue
    // gain, then digital scaling for the steps in between. A request picks the
    // narrowest range that holds it (see vscaleForVoltsPerDiv) and the
    // difference is made up when the trace is drawn, using resolution that is
    // already in the samples.
    //
    // The ladder stops at 5 V/div because eight divisions of that is 40 V and
    // the widest range spans 40.96 V; anything larger would clip.
    // Starts at 20 mV, not 10: that is where the OEM software's own V/div
    // spinner bottoms out for this instrument, checked by stepping it down to
    // its limit with the probe at x1. The narrowest hardware range tops out at
    // 0.1024 V/div, so a 10 mV step was available but is not one Hantek offers.
    static constexpr std::array<double, 8> kVoltsPerDivSteps{
        0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0
    };

    // The narrowest hardware range that can show this volts/div without
    // clipping. Narrower ranges have finer resolution, so the narrowest that
    // fits is always the right choice.
    static constexpr double vscaleForVoltsPerDiv(double voltsPerDiv) {
        for (double f : kVScaleFactors)
            if (maxVoltsPerDivFor(f) >= voltsPerDiv) return f;
        return kVScaleFactors.back();
    }

    // Wire id for a vertical scale factor: 1-based, in the order above.
    static constexpr uint8_t vscaleId(double factor) {
        for (size_t i = 0; i < kVScaleFactors.size(); ++i)
            if (kVScaleFactors[i] == factor) return static_cast<uint8_t>(i + 1);
        return 3;                                   // 1.0, the widest range
    }

    static constexpr double vscaleForId(uint8_t id) {
        return (id >= 1 && id <= kVScaleFactors.size()) ? kVScaleFactors[id - 1] : 1.0;
    }

    // ---- roll mode -----------------------------------------------------------
    // Samples per second per channel, and the wire id that selects each.
    struct JRollRate { double rate; uint8_t id; };
    static constexpr std::array<JRollRate, 13> kRollRates{{
        { 1.0 / 16.0, 0x24 }, { 0.125, 0x23 }, { 0.25, 0x22 }, { 0.5, 0x21 },
        { 1.0,        0x20 }, { 2.0,   0x1f }, { 5.0,  0x1e }, { 11.0, 0x1d },
        { 22.0,       0x1c }, { 44.0,  0x1b }, { 88.0, 0x1a }, { 220.0, 0x19 },
        { 440.0,      0x18 },
    }};

    // With fewer channels active the device samples faster than the nominal
    // rate, by this factor. Indexed by (activeChannels - 1). Until this is
    // validated against a known-frequency source the displayed rate is NOMINAL,
    // and the UI should say so.
    static constexpr std::array<double, 8> kActualRateFactor{
        4.56, 3.03, 2.27, 1.82, 1.51, 1.30, 1.14, 1.00
    };

    // ---- burst mode ----------------------------------------------------------
    // Valid ns/div follow (1|2|5) x 10^n, and the wire id IS the index: for id i,
    // ns/div = {1,2,5}[i % 3] * 10^(i / 3), for i in 0..25.
    static constexpr uint8_t kNsPerDivIdCount = 26;

    // ---- the sweep tables -----------------------------------------------------
    // 0xa3 sets the ADC clock and 0xac sets the sweep window, and BOTH are needed:
    // 0xac carries the pre-trigger sample count and two 24-bit delay counters
    // that between them are the real 10-division sweep. Sending 0xa3 alone, with
    // 0xac left at whatever an init blob put there, is why the timebase only
    // tracked its label over part of the range.
    //
    // kTimeScaleForId is the AGGREGATE sample interval in MICROSECONDS across all
    // active channels — recovered from dsoSetHTrigPos's constant table, and
    // confirmed against a 1 kHz reference at every code where kRecordLenForId is
    // 4000: codes <=15 give 0.4167us x 4 channels = 1.667us measured 1.67, code
    // 17 gives 5.0 measured 5.05, code 18 gives 10.0 measured 10.0, code 19 gives
    // 20.0 measured 20.0.
    //
    // Codes 0..15 SHARE one constant. They are all really 166.667 us/div however
    // they are labelled, which is exactly the flat fast end measured on the
    // bench, and why anything quicker than code 17 reads a lie.
    // The codes worth offering, measured against a 1 kHz reference with the
    // sweep programmed properly:
    //
    //   0..15  one shared constant, so all sixteen are the SAME rate. Code 15
    //          stands for the group; offering the others would be sixteen menu
    //          entries that do the same thing.
    //   16     NOT honoured — reads 1.4286x fast, which is exactly C17/C16, so
    //          the device runs it at code 17's rate. Excluded.
    //   17..23 distinct and honoured.
    static constexpr std::array<uint8_t, 8> kUsableTimeDivIds{ 15, 17, 18, 19, 20, 21, 22, 23 };

    static constexpr double kTimeScaleForId(uint8_t id) {
        if (id <= 15) return 0.4166666666666667;
        switch (id) {
            case 16: return 0.8750001093750137;
            case 17: return 1.25;
            case 18: return 2.5;
            case 19: return 5.0;
            case 20: return 13.333333333333334;
            case 21: return 27.000027000027;
            case 22: return 55.94405594405595;
            case 23: return 125.99218848431397;
            default: return 0.0;               // 24+ zero both accumulators
        }
    }

    // What the OEM captures at each code. Only four differ from the default, and
    // they are the codes whose clock is off nominal — Scope.exe compensates by
    // taking proportionally fewer samples so the 10-division span still reads
    // right. It feeds the 0xac arithmetic; the firmware's own record stays 4000.
    static constexpr uint16_t kRecordLenForId(uint8_t id) {
        switch (id) {
            case 16: return 1600;
            case 20: return 3750;
            case 21: return 3703;
            case 22: return 3571;
            default: return 4000;
        }
    }

    // The per-channel sample interval in SECONDS, which is the constant scaled
    // by the padded channel count. This is the truth about the time axis and it
    // replaces deriving dt from the s/div label: the labels are honest for codes
    // 17..22 as a 10-division sweep of kRecordLenForId samples, but the firmware
    // always hands back kDefaultRecordSamples instead, so a record covers
    // 4000/N times the labelled sweep wherever N is not 4000.
    static constexpr double sampleIntervalFor(uint8_t id, uint8_t activeChannels) {
        uint8_t nch = activeChannels;
        if (nch == 3 || nch == 5 || nch == 7) ++nch;
        if (nch == 0) nch = 1;
        return kTimeScaleForId(id) * 1.0e-6 * nch;
    }

    // What one DISPLAY division actually represents, given the device hands back
    // kDefaultRecordSamples drawn across kHorizontalDivisions. Reported as the
    // timebase so the readout describes the picture rather than a label the
    // hardware does not honour.
    static constexpr double displaySecondsPerDivFor(uint8_t id) {
        return kTimeScaleForId(id) * 1.0e-6 * kDefaultRecordSamples / kHorizontalDivisions;
    }

    // Samples per capture, set with 0xa1 (dsoSetCHSample) as a BYTE count, so
    // the payload is 2x this. Scope.exe holds 0xFA0 = 4000 and sends A1 1F 40.
    //
    // Not sending it at all is why every record came back at exactly 4000
    // samples whatever the timebase: the device sits on its power-on default.
    static constexpr uint16_t kDefaultRecordSamples = 4000;
    static constexpr uint16_t kMaxRecordSamples     = 0x7FFF;   // byte count must fit 16 bits

    static constexpr double nsPerDivForId(uint8_t id) {
        const double mantissa = (id % 3 == 0) ? 1.0 : (id % 3 == 1 ? 2.0 : 5.0);
        double scale = 1.0;
        for (uint8_t i = 0; i < id / 3; ++i) scale *= 10.0;
        return mantissa * scale;
    }


    // How long one capture covers, in seconds. Independent of the channel count:
    // the per-channel interval scales UP with it and the per-channel sample count
    // scales DOWN by the same factor, so the window is always 4000 aggregate
    // samples wide.
    static constexpr double recordDurationFor(uint8_t id) {
        return kTimeScaleForId(id) * 1.0e-6 * kDefaultRecordSamples;
    }

    // The TIMEBASE THE USER SEES: an ordinary 1-2-5 ladder, not the device's own
    // odd intervals.
    //
    // s/div now means what it means on any scope — the window the graticule
    // shows — and the renderer takes that window out of whatever record the
    // device produced. The rate code stops being something the panel displays
    // and becomes an implementation detail chosen to give the window as many
    // samples as possible.
    //
    // The bottom of the ladder is 1 us/div because that is where the fastest
    // capture still puts about two samples in a division; below it there is
    // nothing to draw and offering the step would be offering a lie. The top is
    // 50 ms/div because the longest capture is 4000 x 125.99 us = 504 ms, which
    // is a 500 ms window with 4 ms to spare.
    static constexpr std::array<double, 15> kStandardSecondsPerDiv{
        1.0e-6,   2.0e-6,   5.0e-6,
        10.0e-6,  20.0e-6,  50.0e-6,
        100.0e-6, 200.0e-6, 500.0e-6,
        1.0e-3,   2.0e-3,   5.0e-3,
        10.0e-3,  20.0e-3,  50.0e-3 };

    // The code that best serves a requested window: the FASTEST sampling whose
    // capture still covers it, so the window gets as many samples as the device
    // can give it. Falls back to the longest capture when the request is longer
    // than anything available, which then draws short of the full width rather
    // than pretending.
    static uint8_t nsPerDivIdFor(double secondsPerDiv);

    // The wire id for a roll rate. Roll mode sends this through the SAME 0xa3
    // command that carries a ns/div id in burst mode, and sending the wrong kind
    // stalls the endpoint — the reference carries a comment saying exactly that,
    // and it is what a pipe error on the first roll command means.
    static uint8_t rollRateIdFor(double samplesPerSecond);

    // ---- protocol opcodes ----------------------------------------------------
    // Named where the reference gave a meaning, and left as raw hex where it did
    // not. Several of these are vendor magic whose purpose is unknown; the
    // sequence is replayed byte for byte because that is the only thing known to
    // work.
    enum JCommand : uint8_t {
        kSetActiveChannelCount = 0xa0,   // + count
        kSetRecordLength       = 0xa1,   // + byte count, 16-bit big-endian
        kSetVerticalScale      = 0xa2,   // + 8 scale ids
        kSetTimeDiv            = 0xa3,   // + ns/div id, or roll rate id
        kStartAcquisition      = 0xa4,   // + 0x01 burst, 0x02 roll
        kReadyPoll             = 0xa5,   // + 0x5a -> 0..3; 2 or 3 means ready
        kReadBurstChunk        = 0xa6,   // + 2 or 3
        kStatusA7              = 0xa7,
        kSetActiveChannelMap   = 0xaa,   // + 8 bytes, 0/1 per channel
        kSetTriggerLevel       = 0xab,   // + 2 bytes, big-endian
        kConfigAc              = 0xac,   // + 8 bytes, meaning unknown
        kResetB0               = 0xb0,
        kStatusB5              = 0xb5,
        kStatusB6              = 0xb6,
        kGeneratorEnable       = 0xb7,
        kGeneratorSpeed        = 0xb9,
        kGeneratorSwitch       = 0xbb,
        kAcquisitionArm        = 0xc0,
        kSetTrigger            = 0xc1,   // + source channel, slope
        kAcquisitionGo         = 0xc2,
        kBurstLength           = 0xc6,   // + 2 or 3 -> 2 bytes, big-endian
        kRollReadyLength       = 0xc7,   // -> 2 bytes, big-endian
        kReadRollChunk         = 0xc8,
        kStartWatch            = 0xf3,   // dsoStartWatch, sent on every arm
        kStatusE4              = 0xe4,
        kStatusE5              = 0xe5,
        kStatusE6              = 0xe6,
        kStatusE9              = 0xe9,
        kStatusF5              = 0xf5,
        kStatusF6              = 0xf6,
        kStatusF7              = 0xf7,
        kStatusF8              = 0xf8,
        kStatusFa              = 0xfa,
        kPing                  = 0xf3,
    };

    static constexpr uint8_t kReadyPollArgument = 0x5a;
    static constexpr uint8_t kBurstHalfA        = 0x02;
    static constexpr uint8_t kBurstHalfB        = 0x03;
    static constexpr uint8_t kModeBurst         = 0x01;
    static constexpr uint8_t kModeRoll          = 0x02;

    // HOW MANY LANES THE WIRE ACTUALLY CARRIES.
    //
    // Not the number of active channels, and assuming so produces a waveform
    // rather than an error. Measured on the instrument by enabling one channel
    // at a time and looking at the raw interleave:
    //
    //     active   1  2  3  4  5  6  7  8
    //     stride   1  2  4  4  6  6  8  8
    //
    // An ODD count above one is padded up to even with a lane of its own. Read
    // three channels with a stride of three and every plane becomes a rotation
    // through all four lanes: on the bench a 1 kHz square came back as a clean,
    // convincing 66.7 kHz — exactly four times the sample rate, which is the
    // signature of the mistake.
    static constexpr uint8_t burstStrideFor(uint8_t activeChannels) {
        if (activeChannels <= 1) return activeChannels;
        return static_cast<uint8_t>(activeChannels + (activeChannels % 2));
    }

    // Roll mode pads differently: it adds exactly one extra lane, carrying values
    // around 1742 whose purpose is unknown. The reference documents that one and
    // discards it.
    static constexpr uint8_t rollStrideFor(uint8_t activeChannels) {
        return static_cast<uint8_t>(activeChannels + 1);
    }
};

} // inline namespace jf
