// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JScopeGeneratorKind.h"
#include "JScopeGeneratorCapabilities.h"

inline namespace jf {

// What every generator has, and nothing more. A DDS and a digital pattern
// generator share only "what kind are you" and "are you on" — see
// JScopeGeneratorKind for why they are not flattened into one interface.
// Concrete vocabulary lives in JDdsGenerator / JPatternGenerator.
class JScopeGenerator {
public:
    virtual ~JScopeGenerator() = default;

    JScopeGenerator(const JScopeGenerator&)            = delete;
    JScopeGenerator& operator=(const JScopeGenerator&) = delete;

    virtual JScopeGeneratorKind kind() const = 0;
    virtual const JScopeGeneratorCapabilities& capabilities() const = 0;

    virtual bool setOutputEnabled(bool on) = 0;
    virtual bool isOutputEnabled() const = 0;

protected:
    JScopeGenerator() = default;
};

} // inline namespace jf
