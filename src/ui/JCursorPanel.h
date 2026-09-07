// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JScopeTheme.h"
#include "measure/JCursorModel.h"
#include "scope/JScopeCapabilities.h"

#include <j/core/JCheckBox.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>

inline namespace jf {

// Cursor enables and readouts: X1, X2, dX, 1/dX, Y1, Y2, dY.
//
// 1/dX earns its place because placing two X cursors on successive edges to read
// off a frequency is the single most common thing cursors get used for.
class JCursorPanel : public JContainer {
public:
    JCursorPanel(JSceneGraph& graph, JCursorModel& cursors);

    void rebuild(const JScopeCapabilities& caps);
    void update();

    // The user toggled a cursor set or changed the Y reference channel.
    JSignal<> onCursorsChanged;

private:
    JSceneGraph&  m_graph;
    JCursorModel& m_cursors;

    JCheckBox* m_xEnable{nullptr};
    JCheckBox* m_yEnable{nullptr};
    JComboBox* m_yChannel{nullptr};
    JLabel*    m_x1{nullptr};
    JLabel*    m_x2{nullptr};
    JLabel*    m_dx{nullptr};
    JLabel*    m_freq{nullptr};
    JLabel*    m_y1{nullptr};
    JLabel*    m_y2{nullptr};
    JLabel*    m_dy{nullptr};

    bool m_syncing{false};
};

} // inline namespace jf
