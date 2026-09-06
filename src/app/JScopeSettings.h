#pragma once

#include "scope/JScopeCapabilities.h"
#include <j/config/Settings.h>

#include "scope/JScopeDeviceInfo.h"
#include "scope/JScopeViewState.h"

#include <string>

inline namespace jf {

class JScopeDriver;
class JScopeActions;

// Persists the instrument setup between runs, through the framework's JSettings.
//
// Keyed by DRIVER ID, so a 1008C setup and a DSO2D15 setup do not overwrite each
// other and each device comes back the way it was left. Restoring goes through
// JScopeActions rather than straight at the driver, so a stored value that this
// device cannot do is quantised or refused exactly as if a user had typed it —
// which matters when settings are carried between two different instruments, or
// when a saved file predates a driver change.
class JScopeSettings {
public:
    // Where the settings file lives. Passed in rather than assumed, so a test can
    // use a scratch path and never touch the user's real configuration.
    explicit JScopeSettings(std::string path);

    // The instrument's own setup, and the DISPLAY state that goes with it.
    //
    // The two are stored together but restored apart, because the display state
    // has to be applied after the view has been synced from the driver — that
    // sync resets the view window and rewrites every channel view, so anything
    // put back before it is immediately thrown away.
    void save(const JScopeDriver& driver, const JScopeViewState& view);

    // Apply what was stored for this driver. Returns false when there is nothing
    // stored for it, which is the normal first-run case and not an error.
    bool restore(const JScopeDriver& driver, JScopeActions& actions);

    // Vertical positions, zoom/pan window and cursors: the part of the setup
    // that lives in the view rather than the instrument. A trace dragged clear
    // of its neighbours is as much part of "how I left it" as the V/div that
    // made the dragging necessary. Returns defaults when nothing is stored.
    JScopeViewState restoreView(const JScopeDriver& driver) const;

    // The instrument that last opened SUCCESSFULLY, remembered across runs so
    // startup can go straight to it instead of walking the bus in enumeration
    // order. Only driverId, portPath and serialNumber are stored — enough to
    // recognise the same physical unit, and nothing that would go stale.
    void rememberDevice(const JScopeDeviceInfo& device);
    JScopeDeviceInfo lastDevice() const;

    const std::string& path() const { return m_path; }

private:
    static std::string _key(const std::string& driverId, const std::string& leaf);
    static std::string _channelKey(const std::string& driverId, uint8_t ch,
                                   const std::string& leaf);

    std::string m_path;

    // ONE JSettings, living as long as this object.
    //
    // Not a local per call, which is what it used to be. JSettings::set defers
    // its onChange emit to the main-thread dispatcher and captures `this` to do
    // it, so a JSettings created on the stack is destroyed long before that
    // lambda runs — and save() calls set twenty-odd times, queueing that many
    // emits against freed memory. It surfaced as std::bad_array_new_length
    // thrown out of JSignal::emit while the dispatcher drained, from a slot
    // vector whose length was garbage.
    mutable JSettings m_settings;
    mutable bool      m_loaded{false};

    // Reads the file once, then hands back the same object.
    JSettings& _settings() const;
};

} // inline namespace jf
