// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JTimebasePanel.h"

#include "JScopeFormat.h"

#include "app/JScopeActions.h"
#include "scope/JScopeDriver.h"
#include "scope/JScopeLog.h"

#include <algorithm>
#include <cstdio>
#include <memory>

inline namespace jf {

namespace {

// Engineering notation across the whole range a timebase spans: 500 ns, 20 us,
// 1 ms, 200 ms. Presentation, so it lives in the UI layer.
std::string formatSeconds(double s) { return jScopeFormatSeconds(s); }

std::string formatRate(double hz) {
    char buf[32];
    if (hz >= 1.0e6)      std::snprintf(buf, sizeof buf, "%g MSa/s", hz / 1.0e6);
    else if (hz >= 1.0e3) std::snprintf(buf, sizeof buf, "%g kSa/s", hz / 1.0e3);
    else                  std::snprintf(buf, sizeof buf, "%g Sa/s",  hz);
    return buf;
}

std::string formatDepth(uint32_t n) {
    char buf[32];
    if (n >= 1000000u)   std::snprintf(buf, sizeof buf, "%g Mpts", n / 1.0e6);
    else if (n >= 1000u) std::snprintf(buf, sizeof buf, "%g kpts", n / 1.0e3);
    else                 std::snprintf(buf, sizeof buf, "%u pts",  n);
    return buf;
}

} // namespace

JTimebasePanel::JTimebasePanel(JSceneGraph& graph, JScopeActions& actions)
    : JContainer(graph), m_graph(graph), m_actions(actions) {
    const JScopeTheme& t = JScopeTheme::current();
    // Form mode: label | field, with the label column auto-sized. Four unlabelled
    // dropdowns of short strings would be indistinguishable from one another.
    setLayoutMode(JLayoutMode::Form);
    setGap(t.panelGap);
    setPadding(JEdges(t.panelPadding));
}

void JTimebasePanel::rebuild(const JScopeCapabilities& caps) {
    m_caps = caps;
    m_mode = nullptr; m_secondsPerDiv = nullptr;
    m_recordLength = nullptr; m_sampleRate = nullptr; m_derived = nullptr;
    clear();

    const JScopeTheme& t = JScopeTheme::current();

    // A mode selector only earns its place when there is a choice to make.
    const bool windowed  = jScopeAcquisitionModeSupported(caps.acquisitionModes,
                                                          JScopeAcquisitionMode::Windowed);
    const bool streaming = jScopeAcquisitionModeSupported(caps.acquisitionModes,
                                                          JScopeAcquisitionMode::Streaming);
    if (windowed && streaming) {
        add(std::make_unique<JLabel>(m_graph, "Mode", t.panelLabelWidth, t.panelRowHeight));
        m_mode = add(std::make_unique<JComboBox>(m_graph,
            std::vector<std::string>{ jScopeAcquisitionModeName(JScopeAcquisitionMode::Windowed),
                                      jScopeAcquisitionModeName(JScopeAcquisitionMode::Streaming) },
            t.panelFieldWidth, t.panelRowHeight));
        m_mode->onIndexChanged.connect([this](int i) {
            if (m_syncing) return;
            m_actions.setAcquisitionMode(i == 1 ? JScopeAcquisitionMode::Streaming
                                                : JScopeAcquisitionMode::Windowed);
        });
    }

    if (!caps.secondsPerDiv.empty()) {
        std::vector<std::string> items;
        for (double s : caps.secondsPerDiv) items.push_back(formatSeconds(s));
        add(std::make_unique<JLabel>(m_graph, "s/div", t.panelLabelWidth, t.panelRowHeight));
        m_secondsPerDiv = add(std::make_unique<JComboBox>(m_graph, std::move(items),
                                                          t.panelFieldWidth, t.panelRowHeight));
        m_secondsPerDiv->onIndexChanged.connect([this](int i) {
            if (m_syncing || i < 0 || i >= static_cast<int>(m_caps.secondsPerDiv.size())) return;
            m_actions.setSecondsPerDiv(m_caps.secondsPerDiv[i]);
        });
    }

    // A device that picks its own record length gets no depth control — offering
    // one would be offering a choice it does not have.
    if (!caps.memoryDepths.empty() && !caps.deviceDeterminedRecordLength) {
        std::vector<std::string> items;
        for (uint32_t n : caps.memoryDepths) items.push_back(formatDepth(n));
        add(std::make_unique<JLabel>(m_graph, "Depth", t.panelLabelWidth, t.panelRowHeight));
        m_recordLength = add(std::make_unique<JComboBox>(m_graph, std::move(items),
                                                         t.panelFieldWidth, t.panelRowHeight));
        m_recordLength->onIndexChanged.connect([this](int i) {
            if (m_syncing || i < 0 || i >= static_cast<int>(m_caps.memoryDepths.size())) return;
            m_actions.setRecordLength(m_caps.memoryDepths[i]);
        });
    }

    if (streaming && !caps.streamSampleRates.empty()) {
        std::vector<std::string> items;
        for (double r : caps.streamSampleRates) items.push_back(formatRate(r));
        add(std::make_unique<JLabel>(m_graph, "Rate", t.panelLabelWidth, t.panelRowHeight));
        m_sampleRate = add(std::make_unique<JComboBox>(m_graph, std::move(items),
                                                       t.panelFieldWidth, t.panelRowHeight));
        m_sampleRate->onIndexChanged.connect([this](int i) {
            if (m_syncing || i < 0 || i >= static_cast<int>(m_caps.streamSampleRates.size())) return;
            m_actions.setStreamRate(m_caps.streamSampleRates[i]);
        });
    }

    add(std::make_unique<JLabel>(m_graph, "Window", t.panelLabelWidth, t.panelRowHeight));
        m_derived = add(std::make_unique<JLabel>(m_graph, "", t.panelFieldWidth, t.panelRowHeight));

    JLOGC(JScopeLog::kUi, JLogLevel::Info)
        << "timebase panel rebuilt:" << (m_mode ? " mode" : "")
        << (m_secondsPerDiv ? " s/div" : "") << (m_recordLength ? " depth" : "")
        << (m_sampleRate ? " rate" : "");
    invalidate();
}

void JTimebasePanel::syncFrom(const JScopeDriver& driver) {
    const JScopeTimebaseConfig& cfg = driver.timebaseConfig();
    m_syncing = true;

    if (m_mode)
        m_mode->setCurrentIndex(cfg.mode == JScopeAcquisitionMode::Streaming ? 1 : 0);

    if (m_secondsPerDiv) {
        const auto it = std::find(m_caps.secondsPerDiv.begin(), m_caps.secondsPerDiv.end(),
                                  cfg.secondsPerDiv);
        if (it != m_caps.secondsPerDiv.end())
            m_secondsPerDiv->setCurrentIndex(
                static_cast<int>(it - m_caps.secondsPerDiv.begin()));
    }
    if (m_recordLength) {
        const auto it = std::find(m_caps.memoryDepths.begin(), m_caps.memoryDepths.end(),
                                  cfg.recordLength);
        if (it != m_caps.memoryDepths.end())
            m_recordLength->setCurrentIndex(static_cast<int>(it - m_caps.memoryDepths.begin()));
    }
    if (m_sampleRate) {
        const auto it = std::find(m_caps.streamSampleRates.begin(),
                                  m_caps.streamSampleRates.end(), cfg.sampleRate);
        if (it != m_caps.streamSampleRates.end())
            m_sampleRate->setCurrentIndex(
                static_cast<int>(it - m_caps.streamSampleRates.begin()));
    }

    // The span the current settings actually acquire — the number the user cares
    // about, derived rather than left to be worked out from two others.
    if (m_derived) {
        if (cfg.mode == JScopeAcquisitionMode::Streaming) {
            m_derived->setText(formatRate(cfg.sampleRate) + " continuous");
        } else {
            const double span = cfg.secondsPerDiv * m_caps.horizontalDivisions;
            const double dt   = cfg.recordLength ? span / cfg.recordLength : 0.0;
            m_derived->setText(formatSeconds(span) + " span, "
                               + (dt > 0.0 ? formatRate(1.0 / dt) : std::string("-")));
        }
    }

    m_syncing = false;
}

void JTimebasePanel::setDerivedText(const std::string& text) {
    if (m_derived) m_derived->setText(text);
}

} // inline namespace jf
