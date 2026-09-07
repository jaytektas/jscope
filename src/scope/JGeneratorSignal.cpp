#include "JGeneratorSignal.h"

#include <algorithm>

inline namespace jf {

void JGeneratorSignal::build(const std::vector<uint8_t>& pattern, uint8_t outputs,
                             double revolutionSeconds, uint32_t samplesPerStep) {
    m_valid = false;
    if (pattern.empty() || outputs == 0 || revolutionSeconds <= 0.0) return;

    const uint8_t  channels = std::min<uint8_t>(outputs, JScopeLimits::kMaxChannels);
    const uint32_t steps    = static_cast<uint32_t>(pattern.size());
    const uint32_t samples  = steps * std::max<uint32_t>(1, samplesPerStep);

    m_frame.provision(channels, samples);
    if (!m_frame.shape(channels, samples)) return;

    JScopeFrameHeader& h = m_frame.header;
    h.sampleCount  = samples;
    h.channelCount = channels;
    // One pass of the pattern is one revolution, so the whole frame is exactly
    // that long and the time axis reads as the cycle it represents.
    h.sampleInterval = revolutionSeconds / static_cast<double>(samples);
    // Commanded, not captured: there is no trigger because there was no
    // acquisition, and saying otherwise would dress a drawing up as a measurement.
    h.triggered          = false;
    h.triggerSampleIndex = -1;

    for (uint8_t c = 0; c < channels; ++c) {
        h.channelIds[c] = c;
        // Counts ARE volts here, one for one. A synthesised frame has no ADC to
        // describe, and inventing a scale factor would only be something else
        // that could disagree with the samples.
        h.countsToVolts[c]    = 1.0f;
        h.zeroOffsetCounts[c] = 0.0f;
        h.voltsPerDiv[c]      = static_cast<float>(kVoltsPerDiv);

        int16_t* out = m_frame.plane(c);
        const uint8_t bit = static_cast<uint8_t>(1u << c);
        for (uint32_t s = 0; s < steps; ++s) {
            const int16_t level = (pattern[s] & bit) ? static_cast<int16_t>(kHighVolts)
                                                     : static_cast<int16_t>(kLowVolts);
            const uint32_t from = s * samplesPerStep;
            std::fill(out + from, out + from + samplesPerStep, level);
        }
    }
    m_valid = true;
}

} // inline namespace jf
