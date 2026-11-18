// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QByteArray>
#include <QWidget>
#include <verdigris/wobjectdefs.h>

#include "processes/ImageIntegration/model/FrameType.h"
#include "processes/ImageIntegration/model/GroupingOptions.h"

class IntegrationDataModel;
class FrameTreeView;
class GroupTreeModel;
class TreeDisplayModeWidget;
class ImageViewControl;
class GroupingOptionsWidget;
class QLabel;
class QCheckBox;
class QGroupBox;
class QLineEdit;
class QSplitter;
class QTimer;
class QToolButton;
class QUndoStack;

namespace ImageIntegration
{
class FrameStatisticsDashboard;
class FrameGroupingIssuesWidget;
class FrameQuickActionsWidget;
} // namespace ImageIntegration

/**
 * @class FrameSourceTab
 * @brief Composite widget for a single frame type tab (Bias/Dark/Flat/Light).
 *
 * Contains:
 * - FrameTreeView with GroupTreeModel (left, expandable)
 * - Context panel (right) with Preview, Grouping, Stats - dynamically sized
 * - TreeDisplayModeWidget at bottom
 */
class AFTERNIGHTPROCESSES_API FrameSourceTab : public QWidget
{
    W_OBJECT(FrameSourceTab)
public:
    explicit FrameSourceTab(FrameType type, IntegrationDataModel* model, QWidget* parent = nullptr,
                            QUndoStack* undoStack = nullptr);

    void addFilesRequested(FrameType type) W_SIGNAL(addFilesRequested, type);
    void addFolderRequested(FrameType type, bool recursive) W_SIGNAL(addFolderRequested, type, recursive);
    void removeSelectedRequested() W_SIGNAL(removeSelectedRequested);
    void removeAllRequested(FrameType type) W_SIGNAL(removeAllRequested, type);

    /// Told when a person drags the aside to another width.
    void asideWidthChanged(int width) W_SIGNAL(asideWidthChanged, width);

    [[nodiscard]] FrameType frameType() const;

    [[nodiscard]] FrameTreeView* tree() const;
    [[nodiscard]] GroupTreeModel* treeModel() const;
    [[nodiscard]] TreeDisplayModeWidget* displayModeWidget() const;
    [[nodiscard]] ImageViewControl* previewPanel() const;
    [[nodiscard]] GroupingOptionsWidget* groupingOptions() const;
    [[nodiscard]] QLabel* regroupWarningLabel() const;
    [[nodiscard]] QLabel* statsLabel() const;
    [[nodiscard]] QLineEdit* searchBar() const;

    void refreshGroupingOptionsFromModel();
    void refreshQuickStats();
    void refreshFilterStats();

    /// Keeps the side panel - the preview over the grouping, the statistics, the issues and the quick actions - as an
    /// aside at the page's right: what a page of a main window shows. The aside opens at a width and a person drags
    /// the line at its left between a least and a greatest one (`asideWidthChanged`), the preview growing with it;
    /// the panel's own least width gives way, its padding becomes the aside's, and the width a person gave the panel
    /// in a window is neither applied nor overwritten.
    ///
    /// @param width the width the aside opens at.
    /// @param leastWidth the narrowest a person drags it.
    /// @param greatestWidth the widest.
    void presentSidePanelAsAside(int width, int leastWidth, int greatestWidth);

    /// Gives the aside another width, held within its least and its greatest; nothing for a panel that is no aside.
    void setAsideWidth(int width);

    /// The side panel: the preview and what scrolls under it.
    [[nodiscard]] QWidget* sidePanel() const;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void applySearchFilter();
    void applyPendingGroupingOptions();
    void restoreUiState();
    void schedulePersistUiState();
    void persistUiStateNow();
    void updatePreviewPanelSize();
    void schedulePreviewPanelAspectClamp();
    void updatePreviewPanelAspectClamp();
    void updateActionStripButtonStates();
    [[nodiscard]] int checkedFrameCount() const;

    FrameType m_type;
    IntegrationDataModel* m_model = nullptr;
    QUndoStack* m_undoStack = nullptr;

    GroupTreeModel* m_treeModel = nullptr;
    FrameTreeView* m_tree = nullptr;
    TreeDisplayModeWidget* m_displayMode = nullptr;
    ImageViewControl* m_preview = nullptr;
    GroupingOptionsWidget* m_groupingOptions = nullptr;
    QLabel* m_regroupWarningLabel = nullptr;
    QLabel* m_statsLabel = nullptr;

    ImageIntegration::FrameStatisticsDashboard* m_statisticsDashboard = nullptr;
    ImageIntegration::FrameGroupingIssuesWidget* m_issuesWidget = nullptr;
    ImageIntegration::FrameQuickActionsWidget* m_quickActionsWidget = nullptr;

    QLineEdit* m_searchBar = nullptr;
    QLabel* m_filterStatsLabel = nullptr;
    QSplitter* m_splitter = nullptr;
    QCheckBox* m_coverageToggle = nullptr;
    QTimer* m_searchDebounce = nullptr;
    QTimer* m_groupingDebounce = nullptr;
    QString m_pendingSearchText;
    GroupingOptions m_pendingGroupingOptions;
    bool m_hasPendingGroupingOptions = false;
    QString m_regroupWarningOriginalStyle;
    bool m_splitterAutoSized = false;
    bool m_splitterUserSized = false;
    /// The width the side panel has as an aside, and the least and the greatest a person drags it to; none in a
    /// window, whose panel keeps its own widths.
    int m_asideWidth = 0;
    int m_asideLeastWidth = 0;
    int m_asideGreatestWidth = 0;

    QTimer* m_persistDebounce = nullptr;
    QTimer* m_previewClampDebounce = nullptr;
    QByteArray m_savedSplitterState;
    QByteArray m_savedHeaderState;

    QToolButton* m_removeSelectedButton = nullptr;
    QToolButton* m_removeAllButton = nullptr;
};
