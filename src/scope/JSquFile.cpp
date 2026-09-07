// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JSquFile.h"

#include <cstring>
#include <fstream>

inline namespace jf {

namespace {

// "HTK-DSO" as UTF-16LE, then a NUL word: sixteen bytes.
constexpr char kMagic[] = "HTK-DSO";
constexpr size_t kMagicBytes = 16;
constexpr size_t kOffChannels = 0x18;
constexpr size_t kOffPulses   = 0x1A;

// The eight bytes at 0x10 are the double 2.0 in every file seen. Written back
// unchanged rather than invented: it is not understood, and a file the OEM will
// not open is worse than one carrying a constant nobody has explained.
constexpr uint8_t kUnknown0x10[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40 };

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
void     writeU16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xff);
    p[1] = static_cast<uint8_t>(v >> 8);
}

} // namespace

bool JSquFile::read(const std::string& path, JPattern& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { error = "cannot open " + path; return false; }

    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (raw.size() < kHeaderBytes) { error = "too short to hold a header"; return false; }

    for (size_t i = 0; i < std::strlen(kMagic); ++i)
        if (raw[i * 2] != static_cast<uint8_t>(kMagic[i]) || raw[i * 2 + 1] != 0) {
            error = "not a HTK-DSO file";
            return false;
        }

    const uint16_t channels = readU16(raw.data() + kOffChannels);
    const uint16_t pulses   = readU16(raw.data() + kOffPulses);
    if (pulses == 0 || pulses > kMaxPulses) {
        error = "pulse count out of range: " + std::to_string(pulses);
        return false;
    }
    // The body is two bytes per pulse. A file that says more pulses than it
    // carries is truncated, and reading past it would invent a pattern.
    if (raw.size() < kHeaderBytes + size_t(pulses) * 2) {
        error = "file is shorter than its " + std::to_string(pulses) + " pulses";
        return false;
    }

    out.channels = channels ? channels : 8;
    out.pulses.resize(pulses);
    for (uint16_t i = 0; i < pulses; ++i)
        // Low byte only: the high byte has been zero in every file seen, and the
        // wire takes eight outputs.
        out.pulses[i] = raw[kHeaderBytes + size_t(i) * 2];
    return true;
}

bool JSquFile::write(const std::string& path, const JPattern& pattern, std::string& error) {
    if (pattern.pulses.empty() || pattern.pulses.size() > kMaxPulses) {
        error = "a pattern of " + std::to_string(pattern.pulses.size()) + " pulses cannot be written";
        return false;
    }

    std::vector<uint8_t> raw(kHeaderBytes + pattern.pulses.size() * 2, 0);
    for (size_t i = 0; i < std::strlen(kMagic); ++i) raw[i * 2] = static_cast<uint8_t>(kMagic[i]);
    std::memcpy(raw.data() + 0x10, kUnknown0x10, sizeof kUnknown0x10);
    writeU16(raw.data() + kOffChannels, pattern.channels ? pattern.channels : 8);
    writeU16(raw.data() + kOffPulses, static_cast<uint16_t>(pattern.pulses.size()));
    for (size_t i = 0; i < pattern.pulses.size(); ++i)
        raw[kHeaderBytes + i * 2] = pattern.pulses[i];

    std::ofstream f(path, std::ios::binary);
    if (!f) { error = "cannot write " + path; return false; }
    f.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (!f) { error = "write failed"; return false; }
    return true;
}

} // inline namespace jf
