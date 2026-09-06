#pragma once

#include "JScopeDeviceInfo.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

class JScopeDriver;

// One driver's registration record: how to find its devices and how to make one.
// Keeping enumeration and construction as separate function objects is what a
// future dlopen-based loader would populate, so nothing about the shape of this
// forecloses dynamic drivers later.
struct JScopeDriverInfo {
    std::string id;             // "hantek-1008c", "dso2d15", "synthetic", "replay"
    std::string displayName;    // "Hantek 1008C"

    // Devices this driver can currently claim. Must return an empty vector — not
    // an error — when nothing is attached.
    std::function<std::vector<JScopeDeviceInfo>()> enumerate;

    std::function<std::unique_ptr<JScopeDriver>()> create;
};

} // inline namespace jf
