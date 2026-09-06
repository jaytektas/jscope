#include "JReplayBar.h"

#include "JValueFormat.h"
#include "sources/JReplayDriver.h"
#include "scope/JScopeLog.h"

#include <cstdio>
#include <memory>
#include <vector>

inline namespace jf {

namespace {
// Playback speeds offered. Slower than real time matters more than faster on a
// scope capture: the interesting frame is usually the one that went past too
// quickly to see.
const double kRates[]     = { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0 };
const char*  kRateNames[] = { "x0.1", "x0.25", "x0.5", "x1", "x2", "x5", "x10" };
constexpr int kDefaultRateIndex = 3;      // x1
constexpr int kBarColumns = 4;
}

JReplayBar::JReplayBar(JSceneGraph& graph) : JContainer(graph), m_graph(graph) {
    const JScopeTheme& t = JScopeTheme::current();
    setLayoutMode(JLayoutMode::Grid);
    setColumns(kBarColumns);
    setGap(t.panelGap);
    setPadding(JEdges(t.panelPadding));
    _build();
}

void JReplayBar::_build() {
    const JScopeTheme& t = JScopeTheme::current();

    m_playPause = add(std::make_unique<JButton>(m_graph, "Play"));
    m_playPause->onClicked.connect([this] {
        if (!m_driver) return;
        const bool running = (m_driver->state() == JScopeState::Running);
        if (running) m_driver->stop();
        else         m_driver->start(m_driver->timebaseConfig().mode);
        m_playPause->setLabel(running ? "Play" : "Pause");
    });

    m_stepBack = add(std::make_unique<JButton>(m_graph, "<"));
    m_stepBack->onClicked.connect([this] { if (m_driver) m_driver->step(-1); });

    m_stepFwd = add(std::make_unique<JButton>(m_graph, ">"));
    m_stepFwd->onClicked.connect([this] { if (m_driver) m_driver->step(1); });

    m_loop = add(std::make_unique<JCheckBox>(m_graph, "Loop",
                                             t.panelFieldWidth, t.panelRowHeight));
    m_loop->setChecked(true);
    m_loop->onStateChanged.connect([this](bool on) {
        if (m_driver && !m_syncing) m_driver->setLooping(on);
    });

    m_scrub = add(std::make_unique<JSlider>(m_graph));
    m_scrub->onValueChanged.connect([this](float v) {
        if (!m_driver || m_syncing) return;
        const uint64_t count = m_driver->frameCount();
        if (count == 0) return;
        // The slider is normalised 0..1; the capture is a frame count.
        m_driver->seek(static_cast<uint64_t>(v * static_cast<float>(count - 1) + 0.5f));
    });

    std::vector<std::string> rates(std::begin(kRateNames), std::end(kRateNames));
    m_rate = add(std::make_unique<JComboBox>(m_graph, std::move(rates),
                                             t.panelFieldWidth, t.panelRowHeight));
    m_rate->setCurrentIndex(kDefaultRateIndex);
    m_rate->onIndexChanged.connect([this](int i) {
        if (!m_driver || m_syncing || i < 0 ||
            i >= static_cast<int>(std::size(kRates))) return;
        m_driver->setRate(kRates[i]);
    });

    m_readout = add(std::make_unique<JLabel>(m_graph, "—",
                                             t.panelFieldWidth, t.panelRowHeight));

    const size_t rows = (children().size() + kBarColumns - 1) / kBarColumns;
    JRect box = bounds();
    box.height = t.panelPadding * 2.0f + rows * t.panelRowHeight
               + (rows > 0 ? (rows - 1) * t.panelGap : 0.0f);
    setBounds(box);
}

void JReplayBar::attach(JReplayDriver* driver) {
    m_driver = driver;
    m_syncing = true;
    if (m_driver) {
        m_loop->setChecked(m_driver->looping());
        m_rate->setCurrentIndex(kDefaultRateIndex);
        m_driver->setRate(kRates[kDefaultRateIndex]);
        m_scrub->setValue(0.0f);
        JLOGC(JScopeLog::kReplay, JLogLevel::Info)
            << "replay bar attached: " << m_driver->frameCount() << " frames";
    } else {
        m_scrub->setValue(0.0f);
        m_readout->setText("—");
    }
    m_syncing = false;
    _updateReadout();
    invalidate();
}

void JReplayBar::setPosition(uint64_t frame) {
    if (!m_driver) return;
    const uint64_t count = m_driver->frameCount();
    if (count == 0) return;

    // Programmatic, so it must not echo back as a seek.
    m_syncing = true;
    m_scrub->setValue(static_cast<float>(frame) / static_cast<float>(count - 1));
    m_syncing = false;
    _updateReadout();
}

void JReplayBar::_updateReadout() {
    if (!m_readout) return;
    if (!m_driver) { m_readout->setText("—"); return; }

    char buf[64];
    std::snprintf(buf, sizeof buf, "%llu / %llu",
                  static_cast<unsigned long long>(m_driver->position() + 1),
                  static_cast<unsigned long long>(m_driver->frameCount()));
    m_readout->setText(buf);
}

} // inline namespace jf
