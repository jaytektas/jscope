#include "JCursorPanel.h"

#include "JValueFormat.h"
#include "scope/JScopeLog.h"

#include <memory>
#include <vector>

inline namespace jf {

namespace {
constexpr int kReadoutColumns = 6;
}

JCursorPanel::JCursorPanel(JSceneGraph& graph, JCursorModel& cursors)
    : JContainer(graph), m_graph(graph), m_cursors(cursors) {
    const JScopeTheme& t = JScopeTheme::current();
    // Same reasoning as the measurement panel: a short, wide dock wants columns.
    setLayoutMode(JLayoutMode::Grid);
    setColumns(kReadoutColumns);
    setGap(t.panelGap);
    setPadding(JEdges(t.panelPadding));
}

void JCursorPanel::rebuild(const JScopeCapabilities& caps) {
    m_xEnable = nullptr; m_yEnable = nullptr; m_yChannel = nullptr;
    m_x1 = m_x2 = m_dx = m_freq = m_y1 = m_y2 = m_dy = nullptr;
    clear();

    const JScopeTheme& t = JScopeTheme::current();
    auto row = [&](const char* label) {
        add(std::make_unique<JLabel>(m_graph, label, t.panelLabelWidth, t.panelRowHeight));
    };
    auto readout = [&]() {
        return add(std::make_unique<JLabel>(m_graph, JValueFormat::invalid(),
                                            t.panelFieldWidth, t.panelRowHeight));
    };

    row("");
    m_xEnable = add(std::make_unique<JCheckBox>(m_graph, "X cursors",
                                                t.panelFieldWidth, t.panelRowHeight));
    m_xEnable->onStateChanged.connect([this](bool on) {
        if (m_syncing) return;
        m_cursors.setXEnabled(on);
        JLOGC(JScopeLog::kMeasure, JLogLevel::Debug) << "X cursors " << (on ? "on" : "off");
        onCursorsChanged.emit();
    });

    row("X1");    m_x1   = readout();
    row("X2");    m_x2   = readout();
    row("dX");    m_dx   = readout();
    row("1/dX");  m_freq = readout();

    row("");
    m_yEnable = add(std::make_unique<JCheckBox>(m_graph, "Y cursors",
                                                t.panelFieldWidth, t.panelRowHeight));
    m_yEnable->onStateChanged.connect([this](bool on) {
        if (m_syncing) return;
        m_cursors.setYEnabled(on);
        JLOGC(JScopeLog::kMeasure, JLogLevel::Debug) << "Y cursors " << (on ? "on" : "off");
        onCursorsChanged.emit();
    });

    // Volts mean a different screen position per channel, so a Y readout without
    // a stated reference channel would be ambiguous.
    std::vector<std::string> names;
    for (const JScopeChannelCaps& c : caps.channels) names.push_back(c.label);
    row("Y ref");
    m_yChannel = add(std::make_unique<JComboBox>(m_graph, std::move(names),
                                                 t.panelFieldWidth, t.panelRowHeight));
    m_syncing = true;
    m_yChannel->setCurrentIndex(m_cursors.yChannel());
    m_syncing = false;

    m_yChannel->onIndexChanged.connect([this](int i) {
        if (m_syncing || i < 0) return;
        m_cursors.setYChannel(static_cast<uint8_t>(i));
        onCursorsChanged.emit();
    });

    row("Y1");  m_y1 = readout();
    row("Y2");  m_y2 = readout();
    row("dY");  m_dy = readout();

    const size_t rows = (children().size() + kReadoutColumns - 1) / kReadoutColumns;
    JRect box = bounds();
    box.height = t.panelPadding * 2.0f + rows * t.panelRowHeight
               + (rows > 0 ? (rows - 1) * t.panelGap : 0.0f);
    setBounds(box);

    JLOGC(JScopeLog::kMeasure, JLogLevel::Info) << "cursor panel rebuilt";
    invalidate();
}

void JCursorPanel::update() {
    m_syncing = true;
    if (m_xEnable)  m_xEnable->setChecked(m_cursors.xEnabled());
    if (m_yEnable)  m_yEnable->setChecked(m_cursors.yEnabled());
    if (m_yChannel) m_yChannel->setCurrentIndex(m_cursors.yChannel());

    const bool x = m_cursors.xEnabled();
    if (m_x1) m_x1->setText(x ? JValueFormat::seconds(m_cursors.x1()) : JValueFormat::invalid());
    if (m_x2) m_x2->setText(x ? JValueFormat::seconds(m_cursors.x2()) : JValueFormat::invalid());
    if (m_dx) m_dx->setText(x ? JValueFormat::seconds(m_cursors.deltaX()) : JValueFormat::invalid());
    if (m_freq)
        m_freq->setText((x && m_cursors.haveFrequency())
                            ? JValueFormat::hertz(m_cursors.frequency())
                            : JValueFormat::invalid());

    const bool y = m_cursors.yEnabled();
    if (m_y1) m_y1->setText(y ? JValueFormat::volts(m_cursors.y1()) : JValueFormat::invalid());
    if (m_y2) m_y2->setText(y ? JValueFormat::volts(m_cursors.y2()) : JValueFormat::invalid());
    if (m_dy) m_dy->setText(y ? JValueFormat::volts(m_cursors.deltaY()) : JValueFormat::invalid());

    m_syncing = false;
}

} // inline namespace jf
