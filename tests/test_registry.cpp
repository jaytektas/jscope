// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "sources/JSyntheticDriver.h"
#include "scope/JScopeDriverRegistry.h"
#include "support/JTestReport.h"

using namespace jf;

namespace {

// The registry is populated by file-scope initialisers in the driver sources.
// This test exists as much to prove the OBJECT-library linkage as the lookup:
// if the drivers were in a STATIC archive the linker would drop them and this
// would find nothing.
void testSelfRegistration(JTestReport& r) {
    const auto& drivers = JScopeDriverRegistry::instance().drivers();
    r.check(!drivers.empty(), "at least one driver self-registered");
    r.check(JScopeDriverRegistry::instance().find("synthetic") != nullptr,
            "the synthetic driver registered itself");
    r.check(JScopeDriverRegistry::instance().find("no-such-driver") == nullptr,
            "an unknown id is not found");
}

void testCreate(JTestReport& r) {
    auto d = JScopeDriverRegistry::instance().create("synthetic");
    r.check(d != nullptr, "create() builds a registered driver");
    r.check(d && d->driverId() == "synthetic", "the driver knows its own id");
    r.check(JScopeDriverRegistry::instance().create("no-such-driver") == nullptr,
            "create() on an unknown id returns nullptr rather than throwing");
}

// Enumeration with nothing attached is a normal result. A test machine has no
// scope on it, and reporting that as an error would make the suite unrunnable.
void testEnumerateWithoutHardware(JTestReport& r) {
    const auto devices = JScopeDriverRegistry::instance().enumerateAll();
    r.check(!devices.empty(), "the synthetic source is always enumerable");
    bool tagged = true;
    for (const auto& dev : devices)
        if (dev.driverId.empty()) tagged = false;
    r.check(tagged, "every enumerated device is tagged with its driver id");
}

// Capability lists are what the UI builds itself from, so an inconsistency here
// becomes a control that offers a choice the device does not have.
void testCapabilitiesAreCoherent(JTestReport& r) {
    auto d = JScopeDriverRegistry::instance().create("synthetic");
    JScopeDeviceInfo info;
    info.driverId = "synthetic";
    r.check(d->open(info), "the synthetic driver opens");

    const JScopeCapabilities& caps = d->capabilities();
    r.check(caps.channelCount() > 0, "reports at least one channel");
    r.check(caps.adcBits > 0, "reports an ADC width");
    r.check(caps.countsMax > caps.countsMin, "the count range is non-empty");
    r.check(caps.verticalDivisions > 0 && caps.horizontalDivisions > 0,
            "states the graticule its V/div and s/div are defined against");

    bool sorted = true, nonEmpty = true;
    for (const auto& c : caps.channels) {
        if (c.voltsPerDiv.empty()) nonEmpty = false;
        for (size_t i = 1; i < c.voltsPerDiv.size(); ++i)
            if (c.voltsPerDiv[i] <= c.voltsPerDiv[i - 1]) sorted = false;
    }
    r.check(nonEmpty, "every channel offers at least one V/div step");
    r.check(sorted, "V/div steps are strictly ascending");

    for (size_t i = 1; i < caps.secondsPerDiv.size(); ++i)
        if (caps.secondsPerDiv[i] <= caps.secondsPerDiv[i - 1]) sorted = false;
    r.check(sorted, "timebase steps are strictly ascending");

    // A flag claiming support must be backed by an option list, or the UI will
    // enable a control with nothing in it.
    const bool streams = jScopeAcquisitionModeSupported(
        caps.acquisitionModes, JScopeAcquisitionMode::Streaming);
    r.check(!streams || !caps.streamSampleRates.empty(),
            "claiming Streaming implies a non-empty sample-rate list");

    r.check(caps.generator.kind == JScopeGeneratorKind::None,
            "a device with no generator says so, and generator() is null");
    r.check(d->generator() == nullptr, "generator() matches the capability");

    d->close();
}

// Requests are quantised to a supported step, and the getter must reflect what
// was really applied — otherwise a knob can silently disagree with the hardware.
void testConfigQuantisation(JTestReport& r) {
    auto d = JScopeDriverRegistry::instance().create("synthetic");
    JScopeDeviceInfo info;
    info.driverId = "synthetic";
    d->open(info);

    JScopeChannelConfig cfg;
    cfg.voltsPerDiv = 0.037;             // not a step on any scope
    cfg.offsetVolts = 1000.0;            // far beyond the range
    r.check(d->applyChannel(0, cfg), "applyChannel accepts an off-step request");

    const JScopeChannelConfig& got = d->channelConfig(0);
    const auto& steps = d->capabilities().channels[0].voltsPerDiv;
    bool onAStep = false;
    for (double s : steps) if (s == got.voltsPerDiv) onAStep = true;
    r.check(onAStep, "the applied V/div is one the device actually has");
    r.check(got.offsetVolts <= d->capabilities().channels[0].offsetRangeVolts,
            "an out-of-range offset is clamped, not accepted");

    r.check(!d->applyChannel(200, cfg), "a nonexistent channel is refused");
    d->close();
}

} // namespace

int main() {
    JTestReport r("JScopeDriverRegistry");
    testSelfRegistration(r);
    testCreate(r);
    testEnumerateWithoutHardware(r);
    testCapabilitiesAreCoherent(r);
    testConfigQuantisation(r);
    return r.result();
}
