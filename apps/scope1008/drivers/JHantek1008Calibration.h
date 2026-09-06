#pragma once

#include "JHantek1008Tables.h"

#include <array>
#include <cstdint>
#include <string>

inline namespace jf {

// Per-channel, per-range zero offsets.
//
// The 1008C's ADC does not sit at exactly midscale with no input: each channel
// has its own resting count, and it differs per vertical range. The device is
// asked for them at initialisation by capturing a burst on each of the three
// ranges with the inputs at rest and averaging — twenty-four numbers in all.
//
// Without them a channel reads a constant offset that looks exactly like a real
// DC component on the signal, which on an automotive scope is the difference
// between "this sensor has a 200 mV bias" and "this channel does".
//
// They are persisted, because measuring them takes three burst captures and
// re-doing that on every connect would make opening the device slow for no
// reason. A stored set is used when it matches this unit's serial number.
class JHantek1008Calibration {
public:
    static constexpr uint8_t kChannels = JHantek1008Tables::kChannelCount;
    static constexpr uint8_t kRanges   = 3;

    void setZeroOffset(uint8_t rangeId, uint8_t channel, double counts);
    double zeroOffset(uint8_t rangeId, uint8_t channel) const;

    // By vertical scale factor rather than by wire id, which is how the rest of
    // the driver thinks about ranges.
    double zeroOffsetForVScale(double vscale, uint8_t channel) const;

    bool isComplete() const { return m_complete; }
    void markComplete() { m_complete = true; }
    void clear();

    // Persisted next to the settings, keyed by serial number: offsets belong to
    // a physical unit, and applying one 1008C's to another would be worse than
    // having none.
    bool save(const std::string& path, const std::string& serialNumber) const;
    bool load(const std::string& path, const std::string& serialNumber);

private:
    std::array<std::array<double, kChannels>, kRanges> m_offsets{};
    bool m_complete{false};
};

} // inline namespace jf
