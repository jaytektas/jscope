// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <functional>
#include <string>

// What the AI bus can do that only the WINDOW knows how to do.
//
// JScopeBusCommands drives the instrument through JScopeSession and
// JScopeActions, which are headless and can be tested with the synthetic
// driver. Recording, the cursors, the view, the docks, choosing a device and
// quitting belong to JScopeApp and its widgets, so the bus reaches them through
// these. A hook left empty is answered "not available here" rather than
// pretending, which is what a test sees, and is honest about it.
//
// Every hook runs on the MAIN thread: the bus services one action per frame
// inside the render loop.

inline namespace jf {

struct JScopeBusHooks {
    // ---- capture --------------------------------------------------------------
    std::function<bool(const std::string& path)> startRecording;
    std::function<void()>                        stopRecording;
    // {recording, path, frames}
    std::function<JJson()>                       recordingStatus;
    std::function<bool(const std::string& path)> openCapture;
    // The frame on display, the cursor window of it when the X cursors are on —
    // the same rule as File > Export CSV.
    std::function<bool(const std::string& path)> exportCsv;
    std::function<std::string()>                 captureDir;

    // ---- the measurement window ---------------------------------------------
    // Fills first/count from the cursors for a record of this shape. False when
    // the X cursors are off: measure the whole record.
    std::function<bool(double sampleInterval, uint32_t sampleCount, size_t& first, size_t& count)>
        cursorWindow;

    // ---- cursors and view ----------------------------------------------------
    std::function<JJson()>                        cursors;
    // key=value edits ("x=on", "x1=0.001", "ych=5", "reset"); false + why on a bad one.
    std::function<bool(const std::string& args, std::string& why)> setCursors;
    std::function<JJson()>                        view;
    std::function<bool(const std::string& args, std::string& why)> setView;

    // ---- docks -----------------------------------------------------------------
    std::function<JJson()>                        docks;
    std::function<bool(const std::string& title, bool on)> setDock;

    // ---- devices ---------------------------------------------------------------
    std::function<JJson()>                        devices;
    // An index into devices(), or a driver id. Starts the connect; the result
    // shows up in status.
    std::function<bool(const std::string& which, std::string& why)> openDevice;
    std::function<JJson()>                        currentDevice;

    // ---- the window ------------------------------------------------------------
    std::function<std::string()> screenshot;    // returns the file prefix the next frame lands at
    std::function<void()>        checkUpdates;
    std::function<void()>        quit;
    std::function<std::string()> statusLine;
};

} // inline namespace jf
