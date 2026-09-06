#include "JScopeShortcuts.h"
#include "JScopeApp.h"
#include "JScopeActions.h"
#include "scope/JScopeLog.h"
#include "ui/JTraceView.h"

inline namespace jf {

void JScopeShortcuts::install(JAppWindow& window, JScopeApp& app) {
    window.onKey = [&window, &app](const JKeyEvent& ke) {
        // Key events arrive for release as well as press; acting on both would
        // fire every shortcut twice.
        if (!ke.pressed) return;

        JScopeActions& a = app.actions();
        switch (ke.key) {
            case JKeyEvent::JKey::Space: a.toggleRunStop();  break;
            case JKeyEvent::JKey::S:     a.single();         break;
            case JKeyEvent::JKey::F:     a.forceTrigger();   break;
            case JKeyEvent::JKey::A:     a.autoset();        break;
            case JKeyEvent::JKey::Z:     app.traceView().resetViewWindow(); break;
            default: break;
        }
        window.requestRedraw();
    };

    JLOGC(JScopeLog::kUi, JLogLevel::Debug)
        << "shortcuts installed: Space=run/stop S=single F=force A=autoset Z=reset zoom";
}

} // inline namespace jf
