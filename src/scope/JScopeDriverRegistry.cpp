// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JScopeDriverRegistry.h"
#include "JScopeDriver.h"
#include "JScopeLog.h"

inline namespace jf {

JScopeDriverRegistry& JScopeDriverRegistry::instance() {
    static JScopeDriverRegistry inst;
    return inst;
}

void JScopeDriverRegistry::add(JScopeDriverInfo info) {
    JLOGC(JScopeLog::kScope, JLogLevel::Info)
        << "driver registered: " << info.id << " (" << info.displayName << ")";
    m_drivers.push_back(std::move(info));
}

const JScopeDriverInfo* JScopeDriverRegistry::find(const std::string& id) const {
    for (const auto& d : m_drivers)
        if (d.id == id) return &d;
    return nullptr;
}

std::vector<JScopeDeviceInfo> JScopeDriverRegistry::enumerateAll() const {
    std::vector<JScopeDeviceInfo> out;
    for (const auto& d : m_drivers) {
        if (!d.enumerate) continue;
        std::vector<JScopeDeviceInfo> found = d.enumerate();
        JLOGC(JScopeLog::kScope, JLogLevel::Debug)
            << "enumerate " << d.id << ": " << found.size() << " device(s)";
        for (auto& dev : found) {
            dev.driverId = d.id;
            out.push_back(std::move(dev));
        }
    }
    JLOGC(JScopeLog::kScope, JLogLevel::Info) << "enumerated " << out.size() << " device(s) total";
    return out;
}

std::unique_ptr<JScopeDriver> JScopeDriverRegistry::create(const std::string& driverId) const {
    const JScopeDriverInfo* info = find(driverId);
    if (!info || !info->create) {
        JLOGC(JScopeLog::kScope, JLogLevel::Error) << "no such driver: '" << driverId << "'";
        return nullptr;
    }
    JLOGC(JScopeLog::kScope, JLogLevel::Info) << "creating driver '" << driverId << "'";
    return info->create();
}

} // inline namespace jf
