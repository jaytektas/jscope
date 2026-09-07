#include "JGeneratorPanel.h"

#include "app/JScopeActions.h"
#include "scope/JScopeDriver.h"
#include "scope/JScopeLog.h"

#include <algorithm>
#include <memory>
#include <string>

inline namespace jf {

namespace {

// A plain alternating square wave, which is what the OEM starts from too: every
// pulse the opposite of the last. It is a starting point to EDIT, not a
// simulation of anything -- a crank gap is made by clicking the gap in.
constexpr uint32_t kDefaultPulses = 40;

std::string rpmText(uint32_t rpm) { return std::to_string(rpm) + " rpm"; }

} // namespace

JGeneratorPanel::JGeneratorPanel(JSceneGraph& graph, JScopeActions& actions)
    : JContainer(graph), m_graph(graph), m_actions(actions) {
    const JScopeTheme& t = JScopeTheme::current();
    setLayoutMode(JLayoutMode::Form);
    setGap(t.panelGap);
    setPadding(JEdges(t.panelPadding));
}

void JGeneratorPanel::rebuild(const JScopeCapabilities& caps) {
    m_caps = caps;
    m_output = nullptr; m_rpm = nullptr; m_realRpm = nullptr; m_maxRpm = nullptr;
    m_pulses = nullptr;
    m_lines.clear();
    clear();

    if (caps.generator.kind != JScopeGeneratorKind::DigitalPattern) return;

    const JScopeTheme& t = JScopeTheme::current();
    const JScopeGeneratorCapabilities& g = caps.generator;

    // The device holds 1440 pulses; this driver writes the 62 that fit one packet,
    // and publishing the larger number would offer a length that cannot be sent.
    const uint32_t maxPulses = g.maxPatternLength;

    add(std::make_unique<JLabel>(m_graph, "Output", t.panelLabelWidth, t.panelRowHeight));
    m_output = add(std::make_unique<JCheckBox>(m_graph, "Running",
                                               t.panelFieldWidth, t.panelRowHeight));
    m_output->onStateChanged.connect([this](bool on) {
        if (m_syncing) return;
        m_actions.setGeneratorOutput(on);
    });

    add(std::make_unique<JLabel>(m_graph, "Set Speed", t.panelLabelWidth, t.panelRowHeight));
    m_rpm = add(std::make_unique<JSpinBox>(m_graph, static_cast<int>(g.minRpm),
                                           static_cast<int>(g.maxRpm),
                                           t.panelFieldWidth, t.panelRowHeight));
    m_rpm->onValueChanged.connect([this](int v) {
        if (m_syncing) return;
        m_actions.setGeneratorRpm(static_cast<uint32_t>(std::max(0, v)));
        _showSpeeds();
    });

    // Not an echo of the box above it: the device counts whole clock ticks per
    // step, so what it will actually run is a rounded version of what was asked
    // for, and the ceiling moves whenever the pulse count changes.
    add(std::make_unique<JLabel>(m_graph, "Real Speed", t.panelLabelWidth, t.panelRowHeight));
    m_realRpm = add(std::make_unique<JLabel>(m_graph, "\xE2\x80\x94", t.panelFieldWidth, t.panelRowHeight));
    // The ceiling moves with the pulse count and is worth the space, so it gets
    // its own row: appended to Real Speed it was truncated away by the field width.
    add(std::make_unique<JLabel>(m_graph, "Max Speed", t.panelLabelWidth, t.panelRowHeight));
    m_maxRpm = add(std::make_unique<JLabel>(m_graph, "-", t.panelFieldWidth, t.panelRowHeight));

    add(std::make_unique<JLabel>(m_graph, "Pulses", t.panelLabelWidth, t.panelRowHeight));
    m_pulses = add(std::make_unique<JSpinBox>(m_graph, 1, static_cast<int>(maxPulses),
                                              t.panelFieldWidth, t.panelRowHeight));
    m_pulses->onValueChanged.connect([this](int) {
        if (m_syncing) return;
        _pushPattern();
    });

    // Which lines a NEWLY GENERATED pattern is written to. Once it exists, each
    // channel is edited in the grid independently -- these only decide what the
    // starting pattern covers.
    for (uint32_t i = 0; i < g.patternOutputs; ++i) {
        const std::string label = "CH" + std::to_string(i + 1);
        if (i == 0) add(std::make_unique<JLabel>(m_graph, "Outputs",
                                                 t.panelLabelWidth, t.panelRowHeight));
        else        add(std::make_unique<JLabel>(m_graph, "", t.panelLabelWidth, t.panelRowHeight));
        JCheckBox* box = add(std::make_unique<JCheckBox>(m_graph, label,
                                                         t.panelFieldWidth, t.panelRowHeight));
        box->onStateChanged.connect([this](bool) {
            if (m_syncing) return;
            // ONLY VISIBILITY. This used to rebuild the pattern, which threw away
            // every edit the moment a line was switched on or off -- the one thing
            // an editor must never do to the thing being edited.
            onEnabledOutputsChanged.emit(enabledOutputs());
        });
        m_lines.push_back(box);
    }

    // A pattern that drives nothing would be invisible on every probe, so the
    // first line starts on.
    m_syncing = true;
    if (!m_lines.empty()) m_lines.front()->setChecked(true);
    m_pulses->setValue(static_cast<int>(std::min(kDefaultPulses, maxPulses)));
    m_syncing = false;
    onEnabledOutputsChanged.emit(enabledOutputs());

    // AND SEND IT. Those setValue calls happen with m_syncing raised, which stops a
    // programmatic update being mistaken for the user turning a control -- and
    // also means the settings just put in front of the user were never given to
    // the generator, which would go on holding its own default. The panel showed
    // one pattern while the device played another, and nothing on screen could
    // have revealed it.
    _pushPattern();
}

uint8_t JGeneratorPanel::enabledOutputs() const {
    uint8_t mask = 0;
    for (size_t i = 0; i < m_lines.size() && i < 8; ++i)
        if (m_lines[i]->isChecked()) mask |= static_cast<uint8_t>(1u << i);
    return mask;
}

// Rebuilds the pattern from scratch, which ONLY the pulse count may do: changing
// how many pulses divide the cycle changes what every column means, so carrying
// old edits across would move each event to an angle nobody chose. Switching an
// output on or off changes nothing about the pattern and must not come here.
void JGeneratorPanel::_pushPattern() {
    if (!m_pulses || !m_rpm) return;

    const uint8_t mask = enabledOutputs();

    // A fresh alternating pattern at the requested length. Regenerating rather
    // than resampling is deliberate: changing the pulse count changes what a
    // column MEANS, so carrying old edits across would move every event to an
    // angle nobody chose.
    const uint32_t count = static_cast<uint32_t>(m_pulses->value());
    std::vector<uint8_t> pattern(count, 0);
    for (uint32_t i = 0; i < count; ++i)
        if ((i % 2) == 0) pattern[i] = mask;

    m_actions.setGeneratorPattern(pattern);
    // The step count just changed, so both the achievable speed AND the CEILING
    // have moved, even though nobody touched the speed box. The device has a
    // minimum time per step, so a longer wheel simply cannot be spun as fast.
    if (const JPatternGenerator* g = m_actions.patternGenerator()) {
        const uint32_t ceiling = g->maxRpm();
        m_syncing = true;
        m_rpm->setRange(static_cast<int>(g->capabilities().minRpm), static_cast<int>(ceiling));
        if (static_cast<uint32_t>(m_rpm->value()) > ceiling)
            m_rpm->setValue(static_cast<int>(ceiling));
        m_syncing = false;
    }
    _showSpeeds();
}

void JGeneratorPanel::_showSpeeds() {
    if (!m_realRpm) return;
    const JPatternGenerator* g = m_actions.patternGenerator();
    if (!g) { m_realRpm->setText("—"); return; }
    m_realRpm->setText(rpmText(g->actualRpm()));
    if (m_maxRpm) m_maxRpm->setText(rpmText(g->maxRpm()));
}

void JGeneratorPanel::syncFrom(const JScopeDriver& driver) {
    const JScopeGenerator* base = driver.generator();
    if (!base || base->kind() != JScopeGeneratorKind::DigitalPattern || !m_output) return;
    const auto* g = static_cast<const JPatternGenerator*>(base);

    m_syncing = true;
    m_output->setChecked(g->isOutputEnabled());
    if (m_rpm) m_rpm->setValue(static_cast<int>(g->requestedRpm()));
    if (m_realRpm) m_realRpm->setText(rpmText(g->actualRpm()));
    m_syncing = false;
}

} // inline namespace jf
