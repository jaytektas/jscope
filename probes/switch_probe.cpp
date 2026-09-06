// Reproduce a device SWITCH the way the Device menu does it: one instrument open
// and running, then another chosen, which closes the first and opens the second
// on the connect worker.
//
// Built with AddressSanitizer this is the tool for the crash the bench saw —
// glibc aborting inside JScopeFramePool::provision with "pthread_mutex_lock:
// assertion failed: mutex->__data.__owner == 0". An allocation is where heap
// corruption surfaces, not where it happens.
#include "scope/JScopeSession.h"
#include "scope/JScopeDriverRegistry.h"
#include "scope/JScopeLog.h"

#include <j/core/MainThreadDispatcher.h>
#include <j/core/Log.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

using namespace jf;

namespace { constexpr const char* kCat = "probe"; }

int main(int argc, char** argv) {
    JLog::instance().setGlobalLevel(JLogLevel::Info);

    std::string first = "hantek-1008c", second = "synthetic";
    int runMs = 2000;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--from") == 0 && i + 1 < argc) first  = argv[++i];
        if (std::strcmp(argv[i], "--to")   == 0 && i + 1 < argc) second = argv[++i];
        if (std::strcmp(argv[i], "--run")  == 0 && i + 1 < argc) runMs  = std::atoi(argv[++i]);
    }

    const auto find = [](const std::string& id) {
        JScopeDeviceInfo out;
        for (const auto& d : JScopeDriverRegistry::instance().enumerateAll())
            if (d.driverId == id) out = d;
        return out;
    };

    const JScopeDeviceInfo a = find(first), b = find(second);
    if (a.driverId.empty() || b.driverId.empty()) {
        JLOGC(kCat, JLogLevel::Error) << "need both '" << first << "' and '" << second << "'";
        return 1;
    }

    JScopeSession s;
    JLOGC(kCat, JLogLevel::Info) << "opening " << a.displayName;
    if (!s.open(a)) { JLOGC(kCat, JLogLevel::Error) << "first open failed"; return 1; }

    s.driver()->start(s.driver()->timebaseConfig().mode);
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(runMs);
    while (std::chrono::steady_clock::now() < until) {
        JMainThreadDispatcher::instance().drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    JLOGC(kCat, JLogLevel::Info) << "switching to " << b.displayName;
    bool done = false;
    s.openAsync(b, [&](bool ok) {
        JLOGC(kCat, JLogLevel::Info) << "switch callback ok=" << ok;
        done = true;
    });
    for (int i = 0; i < 3000 && !done; ++i) {
        JMainThreadDispatcher::instance().drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    JMainThreadDispatcher::instance().drain();

    JLOGC(kCat, JLogLevel::Info) << "survived the switch, open=" << s.isOpen();
    s.close();
    return done ? 0 : 1;
}
