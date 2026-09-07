#pragma once

#include <cstdint>
#include <string>
#include <vector>

inline namespace jf {

// THE OEM'S GENERATOR PATTERN FILE (.squ).
//
// Decoded from a file the OEM saved, not from documentation. A 46-byte header
// then one 16-bit word per pulse, low byte being the channel bitmask -- the same
// one-bit-per-output arrangement the wire uses, widened to 16 bits on disk.
//
//     0x00  16  UTF-16LE "HTK-DSO\0"   the magic the .amr reference waveforms share
//     0x10   8  double 2.0             unexplained; 2.0 is also the cycle's two
//                                      crank revolutions, which may be coincidence
//     0x18   2  u16 LE  channel count
//     0x1A   2  u16 LE  pulse count
//     0x1C  18  zeros
//     0x2E   N x u16 LE, one per pulse
//
// Reading and writing the OEM's own format means patterns move between the two
// applications rather than each keeping its own dialect, which matters for a file
// somebody has already built a test around.
class JSquFile {
public:
    struct JPattern {
        std::vector<uint8_t> pulses;      // one byte per pulse, bit i = output i
        uint16_t             channels{8};
    };

    static bool read(const std::string& path, JPattern& out, std::string& error);
    static bool write(const std::string& path, const JPattern& pattern, std::string& error);

    // Exposed for the tests, and so the header's shape is stated once.
    static constexpr size_t   kHeaderBytes   = 0x2E;
    static constexpr uint16_t kMaxPulses     = 1440;
};

} // inline namespace jf
