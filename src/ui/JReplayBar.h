#pragma once

#include "JScopeTheme.h"

#include <j/core/JButton.h>
#include <j/core/JCheckBox.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>
#include <j/core/JSlider.h>

#include <cstdint>

inline namespace jf {

class JReplayDriver;

// Transport for a capture: play, pause, step, scrub, rate, loop.
//
// Exists only while a replay is open. These verbs are not on JScopeDriver because
// no real instrument has them, so the bar is created against a JReplayDriver
// directly and the dock is hidden when there is not one.
class JReplayBar : public JContainer {
public:
    JReplayBar(JSceneGraph& graph);

    // Bind to a capture, or to nothing. Rebuilds the scrub range for the new
    // frame count.
    void attach(JReplayDriver* driver);
    bool isAttached() const { return m_driver != nullptr; }

    // Follow the driver's position, without echoing back as a seek.
    void setPosition(uint64_t frame);

private:
    void _build();
    void _updateReadout();

    JSceneGraph&   m_graph;
    JReplayDriver* m_driver{nullptr};

    JButton*   m_playPause{nullptr};
    JButton*   m_stepBack{nullptr};
    JButton*   m_stepFwd{nullptr};
    JSlider*   m_scrub{nullptr};
    JComboBox* m_rate{nullptr};
    JCheckBox* m_loop{nullptr};
    JLabel*    m_readout{nullptr};

    bool m_syncing{false};
};

} // inline namespace jf
