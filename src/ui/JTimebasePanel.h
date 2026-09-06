#pragma once

#include "JScopeTheme.h"
#include "scope/JScopeCapabilities.h"

#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>

#include <vector>

inline namespace jf {

class JScopeActions;
class JScopeDriver;

// The horizontal axis: acquisition mode, seconds/div, record length, and the
// streaming sample rate.
//
// Which of those are shown depends on what the device reports. A Windowed-only
// device gets no mode selector and no sample rate; a device that decides its own
// record length gets no depth control. The panel asks capabilities() rather than
// asking which device it is.
class JTimebasePanel : public JContainer {
public:
    JTimebasePanel(JSceneGraph& graph, JScopeActions& actions);

    void rebuild(const JScopeCapabilities& caps);
    void syncFrom(const JScopeDriver& driver);

    // Sample rate and record length together determine the acquisition window,
    // so the panel shows the resulting span rather than making the user work it
    // out from two other numbers.
    void setDerivedText(const std::string& text);

private:
    JSceneGraph&   m_graph;
    JScopeActions& m_actions;

    JScopeCapabilities m_caps;

    JComboBox* m_mode{nullptr};
    JComboBox* m_secondsPerDiv{nullptr};
    JComboBox* m_recordLength{nullptr};
    JComboBox* m_sampleRate{nullptr};
    JLabel*    m_derived{nullptr};

    bool m_syncing{false};
};

} // inline namespace jf
