#include "JChannelStrip.h"

#include "JScopeFormat.h"

#include "app/JScopeActions.h"
#include "scope/JScopeLog.h"

#include <j/graphics/VectorGraphics.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstdio>

inline namespace jf {

namespace {

// Engineering notation for a volts/div step: 5 mV, 200 mV, 2 V. Formatting is
// presentation, so it lives here rather than in the HAL — the HAL deals in
// volts and nothing else.
std::string formatVoltsPerDiv(double v) { return jScopeFormatVolts(v); }

std::string formatProbe(double ratio) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "x%g", ratio);
    return buf;
}

} // namespace

JChannelStrip::JChannelStrip(JSceneGraph& graph, uint8_t channelId,
                             const JScopeChannelCaps& caps, JScopeActions& actions)
    : JContainer(graph), m_channelId(channelId), m_actions(actions), m_caps(caps) {
    const JScopeTheme& t = JScopeTheme::current();
    // Form mode is label | field, with the label column auto-sized to its widest
    // entry. Unlabelled combos would leave volts/div, coupling and probe ratio
    // indistinguishable from one another — three dropdowns of short strings.
    setLayoutMode(JLayoutMode::Form);
    setGap(t.panelGap);
    setPadding(JEdges(t.panelPadding));
    _buildRows(graph, caps);

    // Size to the rows actually built. A JContainer keeps its constructed height
    // otherwise, and since it clips its children to its own box, the controls
    // past that height are laid out and then painted away — present, reachable
    // by Tab, and invisible. How many rows there are depends on what the device
    // supports, so it cannot be a constant.
    const size_t rows = (children().size() + 1) / 2;
    const float  h = t.panelPadding * 2.0f
                   + rows * t.panelRowHeight
                   + (rows > 0 ? (rows - 1) * t.panelGap : 0.0f);
    JRect box = bounds();
    box.height = h;
    setBounds(box);

    JLOGC(JScopeLog::kUi, JLogLevel::Debug)
        << caps.label << " strip: " << rows << " rows, " << h << "px";
}

void JChannelStrip::_buildRows(JSceneGraph& graph, const JScopeChannelCaps& caps) {
    const JScopeTheme& t = JScopeTheme::current();

    // Form mode is strictly two cells per row, so a control whose own text is its
    // label still needs an empty label cell — otherwise every subsequent row is
    // shifted by one and each field ends up beside the NEXT control's label.
    add(std::make_unique<JLabel>(graph, "", t.panelLabelWidth, t.panelRowHeight));
    m_enable = add(std::make_unique<JCheckBox>(graph, caps.label, t.panelFieldWidth,
                                               t.panelRowHeight));
    m_enable->onStateChanged.connect([this](bool on) {
        if (m_syncing) return;
        m_actions.setChannelEnabled(m_channelId, on);
    });

    // Volts/div — always present; a device with one step gets a combo with one
    // entry, which is honest about there being no choice.
    if (!caps.voltsPerDiv.empty()) {
        std::vector<std::string> items;
        items.reserve(caps.voltsPerDiv.size());
        for (double v : caps.voltsPerDiv) items.push_back(formatVoltsPerDiv(v));
        // Filled again by _rebuildVoltsPerDiv once the probe is known.
        add(std::make_unique<JLabel>(graph, "V/div", t.panelLabelWidth, t.panelRowHeight));
        m_voltsPerDiv = add(std::make_unique<JComboBox>(graph, std::move(items),
                                                        t.panelFieldWidth, t.panelRowHeight));
        m_voltsPerDiv->onIndexChanged.connect([this](int i) {
            if (m_syncing || i < 0 || i >= static_cast<int>(m_caps.voltsPerDiv.size())) return;
            // Straight through. The list the driver published is in the units
            // the user is reading, so there is nothing to convert here.
            m_actions.setVoltsPerDiv(m_channelId, m_caps.voltsPerDiv[i]);
        });
    }

    // Offset only when the device actually has one. offsetRangeVolts == 0 means
    // no vertical position control, which is the 1008C's case.
    if (caps.offsetRangeVolts > 0.0) {
        add(std::make_unique<JLabel>(graph, "Offset", t.panelLabelWidth, t.panelRowHeight));
        m_offset = add(std::make_unique<JDoubleSpinBox>(graph,
                            -caps.offsetRangeVolts, caps.offsetRangeVolts, 0.0, 3,
                            t.panelFieldWidth, t.panelRowHeight));
        m_offset->setSuffix(" V");
        m_offset->onValueChanged.connect([this](double v) {
            if (m_syncing) return;
            m_actions.setOffsetVolts(m_channelId, v);
        });
    }

    // Coupling only when selectable. An empty list means the input is fixed.
    if (!caps.couplings.empty()) {
        std::vector<std::string> items;
        for (JScopeCoupling c : caps.couplings) items.push_back(jScopeCouplingName(c));
        add(std::make_unique<JLabel>(graph, "Coupling", t.panelLabelWidth, t.panelRowHeight));
        m_coupling = add(std::make_unique<JComboBox>(graph, std::move(items),
                                                     t.panelFieldWidth, t.panelRowHeight));
        m_coupling->onIndexChanged.connect([this](int i) {
            if (m_syncing || i < 0 || i >= static_cast<int>(m_caps.couplings.size())) return;
            m_actions.setCoupling(m_channelId, m_caps.couplings[i]);
        });
    }

    // A probe list of one entry is a fixed probe — no control needed.
    if (caps.probeRatios.size() > 1) {
        std::vector<std::string> items;
        for (double r : caps.probeRatios) items.push_back(formatProbe(r));
        add(std::make_unique<JLabel>(graph, "Probe", t.panelLabelWidth, t.panelRowHeight));
        m_probe = add(std::make_unique<JComboBox>(graph, std::move(items),
                                                  t.panelFieldWidth, t.panelRowHeight));
        m_probe->onIndexChanged.connect([this](int i) {
            if (m_syncing || i < 0 || i >= static_cast<int>(m_caps.probeRatios.size())) return;
            m_actions.setProbeRatio(m_channelId, m_caps.probeRatios[i]);
        });
    }

    if (caps.canInvert) {
        add(std::make_unique<JLabel>(graph, "", t.panelLabelWidth, t.panelRowHeight));
        m_invert = add(std::make_unique<JCheckBox>(graph, "Invert", t.panelFieldWidth,
                                                   t.panelRowHeight));
        m_invert->onStateChanged.connect([this](bool on) {
            if (m_syncing) return;
            m_actions.setInverted(m_channelId, on);
        });
    }

    JLOGC(JScopeLog::kUi, JLogLevel::Debug)
        << "channel strip " << caps.label << " built:"
        << (m_voltsPerDiv ? " vdiv" : "") << (m_offset ? " offset" : "")
        << (m_coupling ? " coupling" : "") << (m_probe ? " probe" : "")
        << (m_invert ? " invert" : "");
}

void JChannelStrip::syncFrom(const JScopeChannelConfig& cfg, const JScopeChannelCaps& caps) {
    // Programmatic writes must not echo back to the driver as if the user had
    // made them.
    m_syncing = true;

    if (m_enable) m_enable->setChecked(cfg.enabled);

    // THE DRIVER OWNS THE LADDER, in the units the user reads off the screen.
    //
    // This used to multiply every step by cfg.probeRatio, which is one device's
    // arithmetic done in shared UI. The two instruments do not agree on what a
    // probe means: the 1008C has no notion of one, so the app scales for it,
    // while the DSO2D15 applies its own and already reports volts at the probe
    // tip — multiplying again read ten times high. That is what the HAL is for.
    // Each driver publishes the steps it can actually offer and this just lists
    // them.
    if (m_voltsPerDiv && caps.voltsPerDiv != m_caps.voltsPerDiv) {
        m_caps.voltsPerDiv = caps.voltsPerDiv;
        std::vector<std::string> items;
        items.reserve(m_caps.voltsPerDiv.size());
        for (double v : m_caps.voltsPerDiv) items.push_back(formatVoltsPerDiv(v));
        m_voltsPerDiv->setItems(std::move(items));
        JLOGC(JScopeLog::kUi, JLogLevel::Debug)
            << "CH" << int(m_channelId + 1) << " V/div ladder republished by the driver";
    }

    if (m_voltsPerDiv && !m_caps.voltsPerDiv.empty()) {
        // Match on the value the driver reports, not the one that was requested.
        int nearest = 0;
        double bestErr = 1.0e300;
        for (size_t i = 0; i < m_caps.voltsPerDiv.size(); ++i) {
            const double err = std::abs(m_caps.voltsPerDiv[i] - cfg.voltsPerDiv);
            if (err < bestErr) { bestErr = err; nearest = static_cast<int>(i); }
        }
        m_voltsPerDiv->setCurrentIndex(nearest);
    }
    if (m_offset)   m_offset->setValue(cfg.offsetVolts);
    if (m_coupling) {
        const auto it = std::find(m_caps.couplings.begin(), m_caps.couplings.end(), cfg.coupling);
        if (it != m_caps.couplings.end())
            m_coupling->setCurrentIndex(static_cast<int>(it - m_caps.couplings.begin()));
    }
    if (m_probe) {
        const auto it = std::find(m_caps.probeRatios.begin(), m_caps.probeRatios.end(),
                                  cfg.probeRatio);
        if (it != m_caps.probeRatios.end())
            m_probe->setCurrentIndex(static_cast<int>(it - m_caps.probeRatios.begin()));
    }
    if (m_invert) m_invert->setChecked(cfg.inverted);

    m_syncing = false;
}

void JChannelStrip::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    // A colour block in the channel's trace colour, so the strip and the trace
    // are unmistakably the same channel.
    const JScopeTheme& t = JScopeTheme::current();
    const JRect b = bounds();

    JVectorCanvas canvas;
    canvas.setAntiAlias(t.chromeAntiAliasPixels);
    canvas.fillRect(b.x, b.y, t.panelSwatchSize, t.panelSwatchSize,
                    JPaint::solid(t.traceColor(m_channelId)));
    canvas.flush(buf);

    JContainer::populateRenderPrimitives(buf);
}

} // inline namespace jf
