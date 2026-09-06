#pragma once

#include "scope/JScopeFrame.h"

#include <cmath>
#include <cstdint>

// Analytic waveforms whose measurements are known in closed form, built straight
// into a JScopeFrame. The point is that every assertion in test_measurements can
// be checked against arithmetic rather than against a previous run's output —
// a golden-value test would happily lock in a wrong answer.

inline namespace jf {

class JWaveformFixture {
public:
    // A 12-bit device centred at midscale, which is what the synthetic driver
    // models and close enough to both real instruments to be representative.
    static constexpr float  kZeroOffsetCounts = 2048.0f;
    static constexpr float  kCountsToVolts    = 0.001f;      // 1 mV per count
    static constexpr int    kCountsMin        = 0;
    static constexpr int    kCountsMax        = 4095;

    static int16_t toCounts(double volts) {
        const double c = kZeroOffsetCounts + volts / kCountsToVolts;
        return static_cast<int16_t>(std::lround(std::clamp(c,
                    static_cast<double>(kCountsMin), static_cast<double>(kCountsMax))));
    }

    static void prepare(JScopeFrame& f, uint32_t samples, double sampleInterval) {
        f.provision(1, samples);
        f.shape(1, samples);
        f.header.sampleCount       = samples;
        f.header.channelCount      = 1;
        f.header.channelIds[0]     = 0;
        f.header.sampleInterval    = sampleInterval;
        f.header.countsToVolts[0]  = kCountsToVolts;
        f.header.zeroOffsetCounts[0] = kZeroOffsetCounts;
        f.header.triggerSampleIndex = -1;
    }

    // Vpp = 2*amplitude, Vrms = amplitude/sqrt(2), Vavg = offset.
    static void sine(JScopeFrame& f, uint32_t samples, double sampleInterval,
                     double frequencyHz, double amplitudeVolts, double offsetVolts = 0.0) {
        prepare(f, samples, sampleInterval);
        int16_t* p = f.plane(0);
        for (uint32_t i = 0; i < samples; ++i) {
            const double t = i * sampleInterval;
            p[i] = toCounts(amplitudeVolts * std::sin(2.0 * M_PI * frequencyHz * t)
                            + offsetVolts);
        }
    }

    // Ideal square with a finite edge, as any real signal has. Vrms for a 50%
    // duty square about zero is the amplitude itself.
    static void square(JScopeFrame& f, uint32_t samples, double sampleInterval,
                       double frequencyHz, double amplitudeVolts, double duty = 0.5,
                       double edgeFraction = 0.001, double offsetVolts = 0.0) {
        prepare(f, samples, sampleInterval);
        int16_t* p = f.plane(0);
        const double e = std::max(1.0e-9, edgeFraction);
        for (uint32_t i = 0; i < samples; ++i) {
            double frac = std::fmod(frequencyHz * i * sampleInterval, 1.0);
            if (frac < 0) frac += 1.0;
            double v;
            if (frac < e)               v = -1.0 + 2.0 * (frac / e);
            else if (frac < duty)       v =  1.0;
            else if (frac < duty + e)   v =  1.0 - 2.0 * ((frac - duty) / e);
            else                        v = -1.0;
            p[i] = toCounts(v * amplitudeVolts + offsetVolts);
        }
    }

    // A trapezoid with a CONSTRUCTED rise time, so the measured 10-90 value has
    // a known right answer: riseFraction of a period from base to top means the
    // 10-90 portion is 0.8 * riseFraction * period.
    static void trapezoid(JScopeFrame& f, uint32_t samples, double sampleInterval,
                          double frequencyHz, double amplitudeVolts, double riseFraction) {
        prepare(f, samples, sampleInterval);
        int16_t* p = f.plane(0);
        const double r = riseFraction;
        for (uint32_t i = 0; i < samples; ++i) {
            double frac = std::fmod(frequencyHz * i * sampleInterval, 1.0);
            if (frac < 0) frac += 1.0;
            double v;
            if (frac < r)             v = -1.0 + 2.0 * (frac / r);          // rising
            else if (frac < 0.5)      v =  1.0;                             // top
            else if (frac < 0.5 + r)  v =  1.0 - 2.0 * ((frac - 0.5) / r);  // falling
            else                      v = -1.0;                             // base
            p[i] = toCounts(v * amplitudeVolts);
        }
    }

    static void dc(JScopeFrame& f, uint32_t samples, double sampleInterval, double volts) {
        prepare(f, samples, sampleInterval);
        int16_t* p = f.plane(0);
        for (uint32_t i = 0; i < samples; ++i) p[i] = toCounts(volts);
    }
};

} // inline namespace jf
