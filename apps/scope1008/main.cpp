#include "app/JScopeApp.h"
#include "scope/JScopeLog.h"

#include <j/core/Log.h>
#include <csignal>
#include <cstring>
#include <string>

using namespace jf;

namespace {

// --verbose / --trace <category> turn the category thresholds up from the
// command line, so a protocol problem can be dumped to the wire without an
// edit-rebuild cycle. Everything else the app does is logged at Info already.
void configureLogging(int argc, char** argv) {
    JLog::instance().setGlobalLevel(JLogLevel::Info);

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--verbose" || arg == "-v") {
            JLog::instance().setGlobalLevel(JLogLevel::Debug);
            JLOGC(JScopeLog::kUi, JLogLevel::Info) << "verbose logging enabled";
        } else if (arg == "--trace" && i + 1 < argc) {
            const std::string category = argv[++i];
            JLog::instance().setLevel(category, JLogLevel::Trace);
            JLOGC(JScopeLog::kUi, JLogLevel::Info)
                << "trace logging enabled for '" << category << "'";
        } else if (arg == "--device" && i + 1 < argc) {
            JScopeApp::preferDriver(argv[++i]);
        } else if (arg == "--quiet" || arg == "-q") {
            JLog::instance().setGlobalLevel(JLogLevel::Warn);
        }
    }
}

// SIGUSR1 dumps the next rendered frame to a PPM. The framework provides the
// hook and expects the application to arm it; having it here means the Vulkan
// output can be inspected on a headless box, or by a script, without attaching
// anything to the running process.
//
//   kill -USR1 $(pgrep jscope)   ->  /tmp/jscope_shot_<surface>.ppm
#if defined(SIGUSR1)
void onCaptureSignal(int) { JAppWindow::s_captureRequest.store(true); }
#endif

// Ctrl-C or a SIGTERM should close the window the same way the close button
// does, so the instrument setup is written out instead of lost.
void onTerminateSignal(int) { JScopeApp::requestTerminate(); }

} // namespace

int main(int argc, char** argv) {
    configureLogging(argc, argv);

    JAppWindow::s_capturePath = "/tmp/jscope_shot";
    // SIGUSR1 is POSIX. Windows has no user-defined signals, so the frame grab
    // simply has no trigger there; Ctrl-C and a terminate request still close
    // the window the same way the close button does.
#if defined(SIGUSR1)
    std::signal(SIGUSR1, onCaptureSignal);
#endif
    std::signal(SIGINT,  onTerminateSignal);
    std::signal(SIGTERM, onTerminateSignal);

    JScopeApp::setApplicationName("scope1008");

    JScopeApp app;
    if (!app.valid()) {
        JLOGC(JScopeLog::kUi, JLogLevel::Error) << "application failed to initialise";
        return -1;
    }

    // Asked for BEFORE the run loop but performed inside it: this returns at
    // once and the connect happens on a worker, so the window is on screen while
    // the bus is being probed. Doing it synchronously here meant a busy
    // instrument was several seconds of USB timeouts with nothing drawn, which
    // looks exactly like an application that failed to start.
    app.beginDeviceSelection();

    return app.run();
}
