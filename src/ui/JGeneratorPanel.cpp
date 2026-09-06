#include "JGeneratorPanel.h"

#include "app/JScopeActions.h"
#include "scope/JScopeDriver.h"
#include "scope/JScopeLog.h"

#include <algorithm>
#include <memory>
#include <string>

inline namespace jf {

namespace {

// 60-2 is the near-universal crank wheel, but 60 teeth is 120 steps and the
// device takes 62, so the default is the same idea at the largest size that
// fits: a wheel with a gap, which is what makes the signal recognisable.
constexpr uint32_t kDefaultMissingTeeth = 2;

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
    m_output = nullptr; m_rpm = nullptr; m_realRpm = nullptr;
    m_teeth = nullptr; m_missing = nullptr;
    m_lines.clear();
    clear();

    if (caps.generator.kind != JScopeGeneratorKind::DigitalPattern) return;

    const JScopeTheme& t = JScopeTheme::current();
    const JScopeGeneratorCapabilities& g = caps.generator;

    // Whole teeth only: a half tooth is not a thing the wheel can have.
    const uint32_t maxTeeth = std::max<uint32_t>(1, JCrankWheel::maxTeethIn(g.maxPatternLength));

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
    // for, and it moves again whenever the tooth count changes.
    add(std::make_unique<JLabel>(m_graph, "Real Speed", t.panelLabelWidth, t.panelRowHeight));
    m_realRpm = add(std::make_unique<JLabel>(m_graph, "—", t.panelFieldWidth, t.panelRowHeight));

    add(std::make_unique<JLabel>(m_graph, "Teeth", t.panelLabelWidth, t.panelRowHeight));
    m_teeth = add(std::make_unique<JSpinBox>(m_graph, 1, static_cast<int>(maxTeeth),
                                             t.panelFieldWidth, t.panelRowHeight));
    m_teeth->onValueChanged.connect([this](int) {
        if (m_syncing) return;
        _pushPattern();
    });

    add(std::make_unique<JLabel>(m_graph, "Missing", t.panelLabelWidth, t.panelRowHeight));
    m_missing = add(std::make_unique<JSpinBox>(m_graph, 0, static_cast<int>(maxTeeth) - 1,
                                               t.panelFieldWidth, t.panelRowHeight));
    m_missing->onValueChanged.connect([this](int) {
        if (m_syncing) return;
        _pushPattern();
    });

    // One switch per output line. They all carry the same wheel: eight
    // independently phased wheels would be a different instrument, and this one
    // has a single step clock.
    for (uint32_t i = 0; i < g.patternOutputs; ++i) {
        const std::string label = "CH" + std::to_string(i + 1);
        if (i == 0) add(std::make_unique<JLabel>(m_graph, "Outputs",
                                                 t.panelLabelWidth, t.panelRowHeight));
        else        add(std::make_unique<JLabel>(m_graph, "", t.panelLabelWidth, t.panelRowHeight));
        JCheckBox* box = add(std::make_unique<JCheckBox>(m_graph, label,
                                                         t.panelFieldWidth, t.panelRowHeight));
        box->onStateChanged.connect([this](bool) {
            if (m_syncing) return;
            _pushPattern();
        });
        m_lines.push_back(box);
    }

    // A wheel that drives nothing would be invisible on every probe, so the first
    // line starts on.
    m_syncing = true;
    if (!m_lines.empty()) m_lines.front()->setChecked(true);
    m_teeth->setValue(static_cast<int>(maxTeeth));
    m_missing->setValue(static_cast<int>(std::min(kDefaultMissingTeeth, maxTeeth - 1)));
    m_syncing = false;
}

void JGeneratorPanel::_pushPattern() {
    if (!m_teeth || !m_missing) return;

    uint8_t mask = 0;
    for (size_t i = 0; i < m_lines.size(); ++i)
        if (m_lines[i]->isChecked()) mask |= static_cast<uint8_t>(1u << i);

    const JCrankWheel wheel{ static_cast<uint32_t>(m_teeth->value()),
                             static_cast<uint32_t>(m_missing->value()), mask };
    const auto pattern = wheel.pattern();
    if (pattern.empty()) return;

    m_actions.setGeneratorPattern(pattern);
    // The step count just changed, so the achievable speed has too, even though
    // nobody touched the speed box.
    _showSpeeds();
}

void JGeneratorPanel::_showSpeeds() {
    if (!m_realRpm) return;
    const JPatternGenerator* g = m_actions.patternGenerator();
    m_realRpm->setText(g ? rpmText(g->actualRpm()) : "—");
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
