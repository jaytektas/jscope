// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/Log.h>

// The project's logging category vocabulary, in one place so that a category is
// never invented twice with two spellings. JLog categories are hierarchical by
// dotted name, so a whole subtree retunes at once:
//
//   JLog::instance().setLevel("scope.*", JLogLevel::Debug);   // every driver
//   JLog::instance().setLevel("usb.bulk", JLogLevel::Trace);  // every byte
//
// Trace on a usb.* category turns on full hex dumps of the wire, which is the
// instrument by which an undocumented protocol gets discovered. Leave it off by
// default — a roll-mode stream at Trace writes megabytes a minute.

inline namespace jf {

struct JScopeLog {
    // --- transport ---
    static constexpr const char* kUsb      = "usb";           // enumeration, open/close, claim
    static constexpr const char* kUsbBulk  = "usb.bulk";      // raw bulk transfers (hex dumps)
    static constexpr const char* kUsbTmc   = "usb.tmc";       // USBTMC framing (hex dumps)

    // --- HAL ---
    static constexpr const char* kScope    = "scope";         // session, registry, lifecycle
    static constexpr const char* kFrames   = "scope.frames";  // pool/ring traffic, drops
    static constexpr const char* kConfig   = "scope.config";  // applied settings + quantisation

    // --- drivers ---
    static constexpr const char* kSynth    = "scope.synthetic";
    static constexpr const char* kReplay   = "scope.replay";
    static constexpr const char* kHantek   = "scope.hantek1008";
    static constexpr const char* kDso      = "scope.dso2d15";
    static constexpr const char* kScpi     = "scope.dso2d15.scpi";

    // --- app ---
    static constexpr const char* kCapture  = "capture";
    static constexpr const char* kMeasure  = "measure";
    static constexpr const char* kUi       = "ui";
    static constexpr const char* kTrace    = "ui.trace";      // render path, per-frame counts
};

} // inline namespace jf
