#pragma once

#include "JScopeDriverInfo.h"
#include <memory>
#include <string>
#include <vector>

// The set of drivers compiled into this binary.
//
// Drivers self-register from a file-scope initialiser via
// J_REGISTER_SCOPE_DRIVER, which is why their sources must live in an OBJECT
// library and never a STATIC one: a static archive drops any object nothing
// references, and the drivers would silently vanish from the link. studio-jf's
// CMakeLists documents the same trap for its widgets.
//
// Dynamic .so loading is deliberately not here. JFramework has no plugin
// mechanism, and building one before a second-party driver exists would be
// speculative. The id-keyed enumerate/create pair above is exactly what such a
// loader would populate, so this is not a dead end.
//
// MAIN THREAD ONLY, like JDockRegistry.

inline namespace jf {

class JScopeDriverRegistry {
public:
    static JScopeDriverRegistry& instance();

    void add(JScopeDriverInfo info);

    const std::vector<JScopeDriverInfo>& drivers() const { return m_drivers; }
    const JScopeDriverInfo* find(const std::string& id) const;

    // Every device every registered driver can currently see. Returns an empty
    // vector when nothing is attached — that is a normal result, not an error.
    std::vector<JScopeDeviceInfo> enumerateAll() const;

    // Instantiate the driver named by device.driverId. nullptr if unknown.
    std::unique_ptr<JScopeDriver> create(const std::string& driverId) const;

private:
    JScopeDriverRegistry() = default;
    std::vector<JScopeDriverInfo> m_drivers;
};

// Place at file scope in a driver's .cpp:
//   J_REGISTER_SCOPE_DRIVER(JSyntheticDriver, "synthetic", "Synthetic source",
//                           JSyntheticDriver::enumerate);
#define J_REGISTER_SCOPE_DRIVER(Type, Id, Name, EnumerateFn)                       \
    namespace {                                                                    \
    const bool k_jScopeDriverRegistered_##Type = [] {                              \
        ::jf::JScopeDriverInfo info;                                               \
        info.id          = (Id);                                                   \
        info.displayName = (Name);                                                 \
        info.enumerate   = (EnumerateFn);                                          \
        info.create      = [] { return std::unique_ptr<::jf::JScopeDriver>(new Type()); }; \
        ::jf::JScopeDriverRegistry::instance().add(std::move(info));                \
        return true;                                                               \
    }();                                                                           \
    }

} // inline namespace jf
