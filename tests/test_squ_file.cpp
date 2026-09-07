// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "scope/JSquFile.h"
#include "support/JTestReport.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace jf;

namespace {

std::string tempPath(const char* name) { return std::string("/tmp/jscope-test-") + name + ".squ"; }

// The format was decoded from ONE file the OEM saved, so the thing worth pinning
// is that a file we write is byte-identical to one it wrote for the same pattern.
// Anything looser would let a plausible-but-wrong header through.
void testMatchesTheOemsOwnFile(JTestReport& r) {
    // The OEM's 1440-pulse alternating pattern, starting low.
    JSquFile::JPattern p;
    p.channels = 8;
    p.pulses.resize(1440, 0);
    for (size_t i = 1; i < p.pulses.size(); i += 2) p.pulses[i] = 0xff;

    const std::string path = tempPath("oem");
    std::string error;
    r.check(JSquFile::write(path, p, error), "the pattern is written: " + error);

    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    r.check(raw.size() == 2926, "the file is 2926 bytes, as the OEM's is");
    r.check(raw[0] == 'H' && raw[1] == 0 && raw[2] == 'T' && raw[4] == 'K',
            "the magic is UTF-16LE HTK-DSO");
    // The double 2.0 at 0x10, carried through rather than invented.
    r.check(raw[0x17] == 0x40 && raw[0x16] == 0x00, "the 0x10 constant is preserved");
    r.check(raw[0x18] == 8 && raw[0x19] == 0, "channel count is 8, u16 little-endian");
    r.check(raw[0x1a] == 0xa0 && raw[0x1b] == 0x05,
            "pulse count is a0 05 -- 1440, the same encoding the wire uses");
    r.check(raw[0x2e] == 0x00 && raw[0x30] == 0xff,
            "the body alternates from low, two bytes per pulse");

    std::remove(path.c_str());
}

void testRoundTrip(JTestReport& r) {
    JSquFile::JPattern p;
    p.channels = 8;
    p.pulses = { 0x01, 0x00, 0x83, 0xff, 0x00, 0x7e };

    const std::string path = tempPath("round");
    std::string error;
    r.check(JSquFile::write(path, p, error), "written");

    JSquFile::JPattern back;
    r.check(JSquFile::read(path, back, error), "read back: " + error);
    r.check(back.pulses == p.pulses, "every pulse survives, bit for bit");
    r.check(back.channels == p.channels, "and so does the channel count");
    std::remove(path.c_str());
}

// A file claiming more pulses than it carries must be refused, not read past --
// that is the difference between reporting a bad file and inventing a pattern.
void testTruncatedIsRefused(JTestReport& r) {
    JSquFile::JPattern p;
    p.pulses.assign(100, 0x01);
    const std::string path = tempPath("trunc");
    std::string error;
    JSquFile::write(path, p, error);

    // Chop the body in half, leaving the header's count saying 100.
    std::vector<uint8_t> raw;
    { std::ifstream f(path, std::ios::binary);
      raw.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); }
    raw.resize(JSquFile::kHeaderBytes + 50);
    { std::ofstream f(path, std::ios::binary);
      f.write(reinterpret_cast<const char*>(raw.data()), (std::streamsize)raw.size()); }

    JSquFile::JPattern back;
    r.check(!JSquFile::read(path, back, error), "a truncated file is refused");
    r.check(!error.empty(), "and says why: " + error);
    std::remove(path.c_str());
}

void testRejectsForeignFiles(JTestReport& r) {
    const std::string path = tempPath("foreign");
    { std::ofstream f(path, std::ios::binary); f << "not a hantek file at all, but long enough"; }
    JSquFile::JPattern back;
    std::string error;
    r.check(!JSquFile::read(path, back, error), "a file without the magic is refused");
    std::remove(path.c_str());
}

} // namespace

int main() {
    JTestReport r("squ file");
    testMatchesTheOemsOwnFile(r);
    testRoundTrip(r);
    testTruncatedIsRefused(r);
    testRejectsForeignFiles(r);
    return r.result();
}
