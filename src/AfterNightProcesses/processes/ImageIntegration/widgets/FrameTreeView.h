// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file FrameTreeView.h
 * @brief Custom tree view for displaying grouped frames.
 *
 * Provides drag-and-drop file import, multi-selection, context menus,
 * and persistent expand/collapse state for frame group hierarchies.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include <QTreeView>
#include <verdigris/wobjectdefs.h>

#include "processes/ImageIntegration/model/FrameType.h"
#include "processes/ImageIntegration/model/GroupingOptions.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"

class GroupTreeModel;

/**
 * @class FrameTreeView
 * @brief Tree view widget for displaying and interacting with frame groups.
 *
 * Features:
 * - Drag-and-drop: accepts image file drops, emits filesDropped()
 * - Selection: tracks selected files and groups, emits frameSelectionChanged()
 * - Context menu: right-click support via contextMenuRequested()
 * - Master frames: toggle master status via masterStatusToggleRequested()
 * - State persistence: save/restore expanded nodes across model reloads
 *
 * Works with GroupTreeModel to display hierarchical frame organization.
 *
 * Uses pimpl idiom for ABI stability and fast compilation.
 */
class AFTERNIGHTPROCESSES_API FrameTreeView : public QTreeView
{
    W_OBJECT(FrameTreeView)

public:
    explicit FrameTreeView(QWidget* parent = nullptr);
    ~FrameTreeView() override;

    void setFrameType(FrameType type);
    void setGroupingOptions(const GroupingOptions& options);
    void setGroupTreeModel(GroupTreeModel* model);

    QStringList selectedFilePaths() const;
    QList<GroupKey> selectedGroups() const;
    bool hasSelectedMasterFrames() const;
    [[nodiscard]] Qt::KeyboardModifiers lastDropModifiers() const;

    void saveExpandedState();
    void restoreExpandedState();

    // Attempt to perform initial expansion based on current model size.
    void maybePerformInitialExpand(int attempts = 0);

    // Signals
    void filesDropped(const QStringList& paths) W_SIGNAL(filesDropped, paths);
    void framesMoved(const QStringList& paths) W_SIGNAL(framesMoved, paths);
    void frameSelectionChanged() W_SIGNAL(frameSelectionChanged);
    void contextMenuRequested(const QPoint& pos, const QStringList& selectedPaths)
        W_SIGNAL(contextMenuRequested, pos, selectedPaths);
    void masterStatusToggleRequested(const QStringList& paths) W_SIGNAL(masterStatusToggleRequested, paths);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void selectionChanged(const QItemSelection& selected, const QItemSelection& deselected) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void applyGroupingColumnVisibility();
    void applyColumnSizing();

    class Impl;
    std::unique_ptr<Impl> m_impl;
};
