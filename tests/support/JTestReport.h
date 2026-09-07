// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/Log.h>
#include <cstdlib>
#include <string>

// Test scaffolding, matching JFramework/src/tests/: plain int main(), <cassert>,
// no GTest and no Catch2. Reported through JLog rather than std::cout because
// CLAUDE.md bans raw streams, and because a failing test then carries the same
// category context as the code it exercised.

inline namespace jf {

class JTestReport {
public:
    explicit JTestReport(std::string suite) : m_suite(std::move(suite)) {
        JLOGC("test", JLogLevel::Info) << "== " << m_suite << " ==";
    }

    void check(bool ok, const std::string& what) {
        ++m_total;
        if (ok) {
            JLOGC("test", JLogLevel::Info) << "  [OK] " << what;
        } else {
            ++m_failed;
            JLOGC("test", JLogLevel::Error) << "  [FAIL] " << what;
        }
    }

    // Exit code for main(): 0 only when everything passed.
    int result() const {
        if (m_failed == 0) {
            JLOGC("test", JLogLevel::Info) << "All " << m_total << " checks passed.";
            return 0;
        }
        JLOGC("test", JLogLevel::Error) << m_failed << " of " << m_total << " checks FAILED.";
        return 1;
    }

private:
    std::string m_suite;
    int m_total{0};
    int m_failed{0};
};

} // inline namespace jf
