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

    QTimer* m_persistDebounce = nullptr;
    QTimer* m_previewClampDebounce = nullptr;
    QByteArray m_savedSplitterState;
    QByteArray m_savedHeaderState;

    QToolButton* m_removeSelectedButton = nullptr;
    QToolButton* m_removeAllButton = nullptr;
};
