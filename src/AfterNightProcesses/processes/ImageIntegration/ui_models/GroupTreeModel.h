// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file GroupTreeModel.h
 * @brief Qt item model for displaying grouped frames in tree views.
 *
 * Provides a hierarchical model where groups are parent nodes and
 * individual frames are child nodes, suitable for QTreeView display.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include <QAbstractItemModel>
#include <QStringList>
#include <verdigris/wobjectdefs.h>

#include "processes/ImageIntegration/model/FrameType.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameTreeFilter.h"

class QUndoStack;
struct GroupingOptions;

/**
 * @class GroupTreeModel
 * @brief Hierarchical Qt model for frame groups.
 *
 * Maps IntegrationDataModel grouped views to QAbstractItemModel interface.
 * Structure:
 * - Root level: groups (by filter, exposure, etc.)
 * - Child level: individual frame entries within each group
 *
 * Supports:
 * - Master frame highlighting
 * - Exclusion status display
 * - Column data (filename, exposure, filter, etc.)
 *
 * Used by FrameTreeView to display the frame hierarchy.
 */
class AFTERNIGHTPROCESSES_API GroupTreeModel : public QAbstractItemModel
{
    W_OBJECT(GroupTreeModel)

public:
    // Column indices are part of the public contract (tests rely on them).
    enum Column
    {
        ColName = 0,
        ColResolution,
        ColFilter,
        ColObject,
        ColSession,
        ColISO,
        ColExposure,
        ColTag,
        ColCount
    };

    enum class DisplayMode
    {
        Hierarchical,
        Flat
    };
    Q_ENUM(DisplayMode)

    explicit GroupTreeModel(FrameType type, IntegrationDataModel* dataModel, QObject* parent = nullptr,
                            QUndoStack* undoStack = nullptr);
    ~GroupTreeModel() override;

    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // Drag and Drop support
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;

    // Custom API
    FrameType frameType() const;
    void setFrameType(FrameType type);

    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const;

    // Helpers
    [[nodiscard]] IntegrationDataModel* dataModel() const;
    [[nodiscard]] bool isGroup(const QModelIndex& index) const;
    [[nodiscard]] bool isFrame(const QModelIndex& index) const;
    [[nodiscard]] GroupKey getGroupKey(const QModelIndex& index) const;
    [[nodiscard]] FrameId getFrameId(const QModelIndex& index) const;
    [[nodiscard]] QString stableNodeId(const QModelIndex& index) const;

    // Search
    QModelIndex findIndexForPath(const QString& canonicalPath) const;

    // Filter/search.
    void setFilter(const FrameTreeFilter& filter);
    void clearFilter();
    [[nodiscard]] int visibleFrameCount() const;
    [[nodiscard]] int matchCount() const;
    [[nodiscard]] int totalCount() const;

    // Lights diagnostics / coverage
    void setShowOnlyCalibrationWarnings(bool enabled);
    [[nodiscard]] bool showOnlyCalibrationWarnings() const;

    // Bulk operations
    // Applies checkbox state (Excluded attribute) to many frames efficiently.
    // This is used by FrameTreeView bulk check/uncheck to avoid per-frame regrouping.
    void setFramesExcluded(const QStringList& paths, bool excluded);

    // Internal drag/drop move: update per-frame metadata to match destination group.
    // Uses the model's undo stack (if available) so the move is undoable.
    void moveFramesToGroup(const QStringList& paths, const QString& targetObject, const QString& targetSession,
                           const GroupingOptions& groupingOptions);

signals:
    void displayModeChanged(DisplayMode mode) W_SIGNAL(displayModeChanged, mode);
    void filterChanged() W_SIGNAL(filterChanged);

private:
    void onModelReset();
    void onGroupingChanged(FrameType type);
    void onFramesAdded(FrameType type, const QStringList& paths);
    void onFramesRemoved(FrameType type, const QStringList& paths);
    void onFrameUpdated(const QString& path);

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

W_REGISTER_ARGTYPE(GroupTreeModel::DisplayMode)
