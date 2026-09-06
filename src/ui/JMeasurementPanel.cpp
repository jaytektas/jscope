#include "JMeasurementPanel.h"

#include "JValueFormat.h"
#include "scope/JScopeFrame.h"
#include "scope/JScopeLog.h"

#include <memory>

inline namespace jf {

namespace {
// Three label/value pairs per row.
constexpr int kReadoutColumns = 6;
}

JMeasurementPanel::JMeasurementPanel(JSceneGraph& graph)
    : JContainer(graph), m_graph(graph) {
    const JScopeTheme& t = JScopeTheme::current();
    // Grid rather than Form: this panel lives in the short, wide bottom dock, and
    // thirteen readouts in a single column would run off the bottom of it. Six
    // columns is three label/value pairs per row.
    setLayoutMode(JLayoutMode::Grid);
    setColumns(kReadoutColumns);
    setGap(t.panelGap);
    setPadding(JEdges(t.panelPadding));
}

void JMeasurementPanel::rebuild(const JScopeCapabilities& caps) {
    m_channelSelect = nullptr;
    m_values.fill(nullptr);
    clear();

    const JScopeTheme& t = JScopeTheme::current();

    std::vector<std::string> names;
    for (const JScopeChannelCaps& c : caps.channels) names.push_back(c.label);

    add(std::make_unique<JLabel>(m_graph, "Channel", t.panelLabelWidth, t.panelRowHeight));
    m_channelSelect = add(std::make_unique<JComboBox>(m_graph, std::move(names),
                                                      t.panelFieldWidth, t.panelRowHeight));
    // A freshly built combo has no selection, so it renders blank while the panel
    // is in fact measuring channel 0. Say which channel it is.
    m_syncing = true;
    m_channelSelect->setCurrentIndex(m_channel);
    m_syncing = false;

    m_channelSelect->onIndexChanged.connect([this](int i) {
        if (m_syncing || i < 0) return;
        m_channel = static_cast<uint8_t>(i);
        JLOGC(JScopeLog::kMeasure, JLogLevel::Debug)
            << "measuring CH" << int(m_channel + 1);
        onChannelChanged.emit(m_channel);
    });

    for (int k = 0; k < static_cast<int>(JMeasurementKind::Count_); ++k) {
        const auto kind = static_cast<JMeasurementKind>(k);
        add(std::make_unique<JLabel>(m_graph, jMeasurementKindName(kind),
                                     t.panelLabelWidth, t.panelRowHeight));
        m_values[static_cast<size_t>(k)] =
            add(std::make_unique<JLabel>(m_graph, JValueFormat::invalid(),
                                         t.panelFieldWidth, t.panelRowHeight));
    }

    // Same content-sizing rule as the channel strips: a JContainer keeps its
    // constructed height and clips its children to it.
    const size_t rows = (children().size() + kReadoutColumns - 1) / kReadoutColumns;
    JRect box = bounds();
    box.height = t.panelPadding * 2.0f + rows * t.panelRowHeight
               + (rows > 0 ? (rows - 1) * t.panelGap : 0.0f);
    setBounds(box);

    JLOGC(JScopeLog::kMeasure, JLogLevel::Info)
        << "measurement panel rebuilt: " << int(JMeasurementKind::Count_)
        << " readouts over " << int(caps.channelCount()) << " channel(s)";
    invalidate();
}

void JMeasurementPanel::update(const JScopeFrame& frame, JMeasurementWindow window) {
    // The channel selector is by DEVICE channel id, but a frame carries only the
    // planes that are enabled — so find the plane holding this channel rather
    // than assuming plane index equals channel id. Measuring plane 0 when CH1 is
    // switched off would silently report a different channel's numbers.
    int plane = -1;
    for (uint8_t p = 0; p < frame.header.channelCount; ++p)
        if (frame.header.channelIds[p] == m_channel) { plane = p; break; }

    if (plane < 0) {
        for (JLabel* l : m_values) if (l) l->setText(JValueFormat::invalid());
        return;
    }

    const auto results = JMeasurementEngine::measureAll(frame, static_cast<uint8_t>(plane),
                                                        window);
    for (const JMeasurementResult& m : results) {
        JLabel* l = m_values[static_cast<size_t>(m.kind)];
        if (!l) continue;
        l->setText(m.valid ? JValueFormat::measurement(m.kind, m.value)
                           : JValueFormat::invalid());
    }
}

} // inline namespace jf
