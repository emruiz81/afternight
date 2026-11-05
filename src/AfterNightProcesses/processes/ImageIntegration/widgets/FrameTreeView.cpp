// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FrameTreeView.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/model/GroupingEngine.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "theme/AnDesignTokens.h"
#include "theme/PainterGuard.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMenu>
#include <QFileInfo>
#include <QHeaderView>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPaintEvent>
#include <QPointer>
#include <QCoreApplication>
#include <QStyleOption>

#include <algorithm>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(FrameTreeView)

class FrameTreeView::Impl
{
public:
    FrameTreeView* q;
    GroupTreeModel* model = nullptr;
    FrameType frameType = FrameType::Light;
    GroupingOptions groupingOptions;
    QSet<QString> expandedNodeIds;

    // Multi-selection checkbox toggle support.
    bool pendingCheckboxToggle = false;
    QString pendingCheckboxPath;
    int pendingOldCheckState = -1;
    QStringList pendingSelectedPaths;
    bool applyingBulkCheckboxState = false;

    bool didInitialExpand = false;
    Qt::KeyboardModifiers lastDropModifiers = Qt::NoModifier;

    // Drag and drop compatibility tracking
    bool draggingInternal = false;
    std::vector<FrameInfo> draggedFrames;

    explicit Impl(FrameTreeView* parent) : q(parent) {}
};

FrameTreeView::FrameTreeView(QWidget* parent) : QTreeView(parent), m_impl(std::make_unique<Impl>(this))
{
    setAcceptDrops(true);
    setDragEnabled(true);
    setDropIndicatorShown(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setContextMenuPolicy(Qt::DefaultContextMenu);

    // Default appearance
    setAlternatingRowColors(true);
    setUniformRowHeights(true);

    setIconSize(QSize(32, 32));
}

FrameTreeView::~FrameTreeView()
{
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);
}

void FrameTreeView::mousePressEvent(QMouseEvent* event)
{
    m_impl->pendingCheckboxToggle = false;
    m_impl->pendingCheckboxPath.clear();
    m_impl->pendingSelectedPaths.clear();

    if (event && event->button() == Qt::LeftButton && m_impl->model)
    {
        const QModelIndex idx = indexAt(event->pos());
        if (idx.isValid() && idx.column() == GroupTreeModel::ColName && m_impl->model->isFrame(idx))
        {
            const QVariant cs = m_impl->model->data(idx, Qt::CheckStateRole);
            if ((m_impl->model->flags(idx) & Qt::ItemIsUserCheckable))
            {
                const FrameId id = m_impl->model->getFrameId(idx);
                if (id.isValid())
                {
                    m_impl->pendingCheckboxToggle = true;
                    m_impl->pendingCheckboxPath = id.canonicalPath();
                    m_impl->pendingOldCheckState = cs.isValid() ? cs.toInt() : -1;
                    m_impl->pendingSelectedPaths = selectedFilePaths();
                    if (!m_impl->pendingSelectedPaths.contains(m_impl->pendingCheckboxPath))
                    {
                        m_impl->pendingSelectedPaths << m_impl->pendingCheckboxPath;
                    }
                }
            }
        }
    }

    QTreeView::mousePressEvent(event);
}

void FrameTreeView::mouseReleaseEvent(QMouseEvent* event)
{
    QTreeView::mouseReleaseEvent(event);

    if (!m_impl->pendingCheckboxToggle || !m_impl->model)
    {
        return;
    }

    const QModelIndex toggledIdx = m_impl->model->findIndexForPath(m_impl->pendingCheckboxPath);
    if (!toggledIdx.isValid())
    {
        return;
    }

    const int newCheckState = m_impl->model->data(toggledIdx, Qt::CheckStateRole).toInt();
    if (m_impl->pendingOldCheckState != -1 && newCheckState == m_impl->pendingOldCheckState)
    {
        // Click was not a checkbox toggle.
        m_impl->pendingCheckboxToggle = false;
        return;
    }

    // Apply the same check state to all selected frames (and frames under selected groups).
    QStringList targetPaths = m_impl->pendingSelectedPaths;
    m_impl->pendingCheckboxToggle = false;
    if (targetPaths.size() <= 1)
    {
        targetPaths = selectedFilePaths();
        if (!m_impl->pendingCheckboxPath.isEmpty() && !targetPaths.contains(m_impl->pendingCheckboxPath))
        {
            targetPaths << m_impl->pendingCheckboxPath;
        }
    }

    if (targetPaths.size() <= 1)
    {
        return;
    }

    // Exclude attribute is the inverse of checkbox checked state.
    const bool excluded = (newCheckState != Qt::Checked);
    m_impl->model->setFramesExcluded(targetPaths, excluded);
}

void FrameTreeView::applyColumnSizing()
{
    if (!m_impl->model)
    {
        return;
    }

    auto* hdr = header();
    if (!hdr)
    {
        return;
    }

    // Desired UX:
    // - File/Group column should never feel cramped -> Stretch.
    // - Other visible metadata columns should size to their contents.
    hdr->setStretchLastSection(false);
    hdr->setSectionResizeMode(GroupTreeModel::ColName, QHeaderView::Stretch);

    const auto setToContentsIfVisible = [this, hdr](int col) {
        if (!isColumnHidden(col))
        {
            hdr->setSectionResizeMode(col, QHeaderView::ResizeToContents);
        }
    };

    setToContentsIfVisible(GroupTreeModel::ColResolution);
    setToContentsIfVisible(GroupTreeModel::ColFilter);
    setToContentsIfVisible(GroupTreeModel::ColObject);
    setToContentsIfVisible(GroupTreeModel::ColSession);
    setToContentsIfVisible(GroupTreeModel::ColISO);
    setToContentsIfVisible(GroupTreeModel::ColExposure);
    setToContentsIfVisible(GroupTreeModel::ColTag);

    // Give Qt a hint for initial sizes (ResizeToContents may need a pass).
    for (int c = 0; c < GroupTreeModel::ColCount; ++c)
    {
        if (!isColumnHidden(c) && c != GroupTreeModel::ColName)
        {
            resizeColumnToContents(c);
        }
    }
}

void FrameTreeView::applyGroupingColumnVisibility()
{
    if (!m_impl->model)
    {
        return;
    }

    // Columns that are used for grouping become redundant in the tree view.
    // (Flat is the default display mode, so this must not be gated on Hierarchical.)
    setColumnHidden(GroupTreeModel::ColResolution, m_impl->groupingOptions.byResolution);
    setColumnHidden(GroupTreeModel::ColFilter, m_impl->groupingOptions.byFilter);
    setColumnHidden(GroupTreeModel::ColObject, m_impl->groupingOptions.byObject);
    setColumnHidden(GroupTreeModel::ColSession, m_impl->groupingOptions.bySession);
    setColumnHidden(GroupTreeModel::ColISO, m_impl->groupingOptions.byIsoGain);
    setColumnHidden(GroupTreeModel::ColExposure, m_impl->groupingOptions.byExposure);
    setColumnHidden(GroupTreeModel::ColTag, m_impl->groupingOptions.byCustomTag);
}

void FrameTreeView::setFrameType(FrameType type)
{
    m_impl->frameType = type;
}

void FrameTreeView::setGroupingOptions(const GroupingOptions& options)
{
    m_impl->groupingOptions = options;
    applyGroupingColumnVisibility();
}

void FrameTreeView::setGroupTreeModel(GroupTreeModel* model)
{
    // Disconnect from old model if any
    if (m_impl->model)
    {
        disconnect(m_impl->model, nullptr, this, nullptr);
    }

    m_impl->model = model;
    setModel(model);

    // Connect to model reset to expand all groups by default
    if (m_impl->model)
    {
        connect(m_impl->model, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
            // Preserve expanded state across model rebuilds (bulk include/exclude triggers reset).
            saveExpandedState();
        });

        // If the user toggles a checkbox on one selected frame, mirror it to all selected frames.
        // This triggers on the actual CheckStateRole change (more robust than pixel-perfect clicking).
        connect(m_impl->model, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex& topLeft, const QModelIndex& bottomRight, const QList<int>& roles) {
                    (void)bottomRight;

                    if (!m_impl->model || m_impl->applyingBulkCheckboxState)
                    {
                        return;
                    }

                    if (!roles.isEmpty() && !roles.contains(Qt::CheckStateRole))
                    {
                        return;
                    }

                    // Primary path: user click (we snapshot selection on mouse press).
                    if (m_impl->pendingCheckboxToggle)
                    {
                        const QModelIndex toggledIdx = m_impl->model->findIndexForPath(m_impl->pendingCheckboxPath);
                        if (!toggledIdx.isValid())
                        {
                            m_impl->pendingCheckboxToggle = false;
                            return;
                        }

                        const int newCheckState = m_impl->model->data(toggledIdx, Qt::CheckStateRole).toInt();
                        if (m_impl->pendingOldCheckState != -1 && newCheckState == m_impl->pendingOldCheckState)
                        {
                            return;
                        }

                        const QStringList targetPaths = m_impl->pendingSelectedPaths;
                        m_impl->pendingCheckboxToggle = false;
                        if (targetPaths.size() <= 1)
                        {
                            return;
                        }

                        // Exclude attribute is the inverse of checkbox checked state.
                        const bool excluded = (newCheckState != Qt::Checked);

                        // Avoid re-applying the toggled item; it's already updated.
                        QStringList bulkPaths;
                        bulkPaths.reserve(targetPaths.size());
                        for (const auto& path : targetPaths)
                        {
                            if (!m_impl->pendingCheckboxPath.isEmpty() && path == m_impl->pendingCheckboxPath)
                            {
                                continue;
                            }
                            bulkPaths << path;
                        }
                        if (bulkPaths.isEmpty())
                        {
                            return;
                        }

                        m_impl->applyingBulkCheckboxState = true;
                        m_impl->model->setFramesExcluded(bulkPaths, excluded);
                        m_impl->applyingBulkCheckboxState = false;
                        return;
                    }

                    // Fallback path: programmatic setData() while multiple frames are selected (tests rely on this).
                    if (!selectionModel())
                    {
                        return;
                    }

                    const QModelIndex candidate = topLeft.sibling(topLeft.row(), GroupTreeModel::ColName);
                    if (!candidate.isValid() || !m_impl->model->isFrame(candidate))
                    {
                        return;
                    }

                    const QStringList targetPaths = selectedFilePaths();
                    if (targetPaths.size() <= 1)
                    {
                        return;
                    }

                    const FrameId toggledId = m_impl->model->getFrameId(candidate);
                    if (!toggledId.isValid())
                    {
                        return;
                    }

                    const QString toggledPath = toggledId.canonicalPath();
                    if (!targetPaths.contains(toggledPath))
                    {
                        return;
                    }

                    const int newCheckState = m_impl->model->data(candidate, Qt::CheckStateRole).toInt();
                    const bool excluded = (newCheckState != Qt::Checked);

                    QStringList bulkPaths;
                    bulkPaths.reserve(targetPaths.size());
                    for (const auto& path : targetPaths)
                    {
                        if (path == toggledPath)
                        {
                            continue;
                        }
                        bulkPaths << path;
                    }
                    if (bulkPaths.isEmpty())
                    {
                        return;
                    }

                    m_impl->applyingBulkCheckboxState = true;
                    m_impl->model->setFramesExcluded(bulkPaths, excluded);
                    m_impl->applyingBulkCheckboxState = false;
                });

        connect(
            m_impl->model, &QAbstractItemModel::modelReset, this,
            [this]() {
                // First load: expand everything for discoverability.
                // Subsequent resets (e.g. bulk checkbox updates): restore previous expansion to avoid lag.
                if (!m_impl->didInitialExpand)
                {
                    // Defer counting/expansion to the next event loop cycle. Some models populate
                    // child rows after modelReset has fired; deferring allows accurate frame counts.
                    QTimer::singleShot(0, this, [this]() { maybePerformInitialExpand(); });
                }
                else
                {
                    restoreExpandedState();

                    // If there is no saved expansion state, fall back to the auto-expand policy.
                    if (m_impl->expandedNodeIds.isEmpty())
                    {
                        QTimer::singleShot(0, this, [this]() { maybePerformInitialExpand(); });
                    }
                }

                applyGroupingColumnVisibility();
                applyColumnSizing();
            },
            Qt::QueuedConnection);

        connect(m_impl->model, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex& parent, int, int) {
            if (m_impl->didInitialExpand)
            {
                return;
            }

            if (parent.isValid())
            {
                return;
            }

            QTimer::singleShot(0, this, [this]() { maybePerformInitialExpand(); });
        });

        connect(m_impl->model, &GroupTreeModel::displayModeChanged, this,
                [this]() { applyGroupingColumnVisibility(); });

        // If the model is already populated when attached (e.g. process/session restores frames),
        // we won't necessarily get a modelReset/rowsInserted signal here. Kick the initial
        // auto-expand policy once the event loop spins.
        QTimer::singleShot(0, this, [this]() { maybePerformInitialExpand(); });

        applyGroupingColumnVisibility();
        applyColumnSizing();
    }
}

void FrameTreeView::maybePerformInitialExpand(int attempts)
{
    if (!m_impl->model || m_impl->didInitialExpand)
    {
        return;
    }

    const int groupCount = m_impl->model->rowCount(QModelIndex());
    const int totalCount = m_impl->model->totalCount();
    const int visibleFrames = m_impl->model->visibleFrameCount();

    // No frames: don't lock in an "initial" expansion state.
    // We'll re-evaluate once frames actually exist.
    if (totalCount == 0)
    {
        return;
    }

    // If the model is still empty, defer expansion until rows arrive.
    if (groupCount == 0 && attempts < 10)
    {
        QTimer::singleShot(25, this, [this, attempts]() { maybePerformInitialExpand(attempts + 1); });
        return;
    }

    // If zero frames were observed but groups are present, the model may still be
    // populating children. Retry a few times to let asynchronous population finish.
    if (visibleFrames == 0 && totalCount > 0 && groupCount > 0 && attempts < 5)
    {
        QTimer::singleShot(25, this, [this, attempts]() { maybePerformInitialExpand(attempts + 1); });
        return;
    }

    constexpr int kAutoExpandFrameThreshold = 1000; // keep trees collapsed when more than this
    if (visibleFrames <= kAutoExpandFrameThreshold)
    {
        // If the model has very few groups, expand immediately for discoverability.
        constexpr int kAutoExpandGroupFallback = 30;
        if (groupCount <= kAutoExpandGroupFallback && groupCount > 0)
        {
            expandAll();
            m_impl->didInitialExpand = true;
            return;
        }
        expandAll();
    }
    else
    {
        collapseAll();
    }
    m_impl->didInitialExpand = true;
}

QStringList FrameTreeView::selectedFilePaths() const
{
    QStringList paths;
    if (!m_impl->model || !selectionModel())
    {
        return paths;
    }
    QSet<QString> seenPaths;

    QModelIndexList selected = selectionModel()->selectedRows();
    for (const auto& index : selected)
    {
        if (m_impl->model->isFrame(index))
        {
            FrameId id = m_impl->model->getFrameId(index);
            if (id.isValid())
            {
                const QString path = id.canonicalPath();
                if (!seenPaths.contains(path))
                {
                    seenPaths.insert(path);
                    paths << path;
                }
            }
        }
        else if (m_impl->model->isGroup(index))
        {
            // If group is selected, select all children?
            // Depends on requirement. Usually user wants files.
            int rows = m_impl->model->rowCount(index);
            for (int i = 0; i < rows; ++i)
            {
                QModelIndex child = m_impl->model->index(i, 0, index);
                FrameId id = m_impl->model->getFrameId(child);
                if (id.isValid())
                {
                    const QString path = id.canonicalPath();
                    if (!seenPaths.contains(path))
                    {
                        seenPaths.insert(path);
                        paths << path;
                    }
                }
            }
        }
    }
    return paths;
}

QList<GroupKey> FrameTreeView::selectedGroups() const
{
    QList<GroupKey> groups;
    if (!m_impl->model || !selectionModel())
    {
        return groups;
    }

    QModelIndexList selected = selectionModel()->selectedRows();
    for (const auto& index : selected)
    {
        if (m_impl->model->isGroup(index))
        {
            groups << m_impl->model->getGroupKey(index);
        }
    }
    return groups;
}

bool FrameTreeView::hasSelectedMasterFrames() const
{
    // Logic to check if any selected frame is a master
    // For now, return false or check model
    return false;
}

void FrameTreeView::saveExpandedState()
{
    m_impl->expandedNodeIds.clear();
    if (!m_impl->model)
        return;

    const auto saveRecursive = [this](auto&& self, const QModelIndex& parent) -> void {
        const int rows = m_impl->model->rowCount(parent);
        for (int row = 0; row < rows; ++row)
        {
            const QModelIndex idx = m_impl->model->index(row, 0, parent);
            if (!idx.isValid())
            {
                continue;
            }

            if (m_impl->model->isGroup(idx) && isExpanded(idx))
            {
                const QString id = m_impl->model->stableNodeId(idx);
                if (!id.isEmpty())
                {
                    m_impl->expandedNodeIds.insert(id);
                }
            }

            self(self, idx);
        }
    };

    saveRecursive(saveRecursive, QModelIndex());
}

void FrameTreeView::restoreExpandedState()
{
    if (!m_impl->model)
        return;

    const auto restoreRecursive = [this](auto&& self, const QModelIndex& parent) -> void {
        const int rows = m_impl->model->rowCount(parent);
        for (int row = 0; row < rows; ++row)
        {
            const QModelIndex idx = m_impl->model->index(row, 0, parent);
            if (!idx.isValid())
            {
                continue;
            }

            if (m_impl->model->isGroup(idx))
            {
                const QString id = m_impl->model->stableNodeId(idx);
                if (m_impl->expandedNodeIds.contains(id))
                {
                    setExpanded(idx, true);
                }
            }

            self(self, idx);
        }
    };

    restoreRecursive(restoreRecursive, QModelIndex());
}

void FrameTreeView::dragEnterEvent(QDragEnterEvent* event)
{
    QTreeView::dragEnterEvent(event);

    m_impl->draggingInternal = false;
    m_impl->draggedFrames.clear();

    if (event->mimeData()->hasFormat(QStringLiteral("application/x-afternight-frame-id")))
    {
        QByteArray encodedData = event->mimeData()->data(QStringLiteral("application/x-afternight-frame-id"));
        QDataStream stream(&encodedData, QIODevice::ReadOnly);
        QStringList paths;
        while (!stream.atEnd())
        {
            QString path;
            stream >> path;
            paths << path;
        }

        if (m_impl->model && m_impl->model->dataModel())
        {
            IntegrationDataModel* dataModel = m_impl->model->dataModel();
            const auto& frameCollection = dataModel->frames(m_impl->frameType);

            bool allInternal = !paths.isEmpty();
            for (const QString& path : paths)
            {
                auto frame_opt = frameCollection.findById(FrameId(path));
                if (frame_opt.has_value())
                {
                    m_impl->draggedFrames.push_back(*frame_opt);
                }
                else
                {
                    allInternal = false;
                    break;
                }
            }

            if (allInternal)
            {
                m_impl->draggingInternal = true;
            }
        }
        event->acceptProposedAction();
    }
    else if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
    }
}

void FrameTreeView::dragMoveEvent(QDragMoveEvent* event)
{
    QTreeView::dragMoveEvent(event);

    if (m_impl->draggingInternal && !m_impl->draggedFrames.empty())
    {
        const QModelIndex targetIdx = indexAt(event->position().toPoint());
        if (targetIdx.isValid() && m_impl->model)
        {
            QModelIndex targetGroupIdx = targetIdx;
            if (m_impl->model->isFrame(targetIdx))
            {
                targetGroupIdx = m_impl->model->parent(targetIdx);
            }

            if (m_impl->model->isGroup(targetGroupIdx))
            {
                const GroupKey targetGroupKey = m_impl->model->getGroupKey(targetGroupIdx);
                GroupingEngine engine;

                bool allCompatible = true;
                for (const auto& frame : m_impl->draggedFrames)
                {
                    if (!engine.checkCompatibilityForMove(frame, targetGroupKey, m_impl->frameType,
                                                          m_impl->groupingOptions))
                    {
                        allCompatible = false;
                        break;
                    }
                }

                if (allCompatible)
                {
                    event->acceptProposedAction();
                }
                else
                {
                    // By ignoring the event here, Qt will show the 'not allowed' cursor.
                    event->ignore();
                }
                return;
            }
        }

        // If not over a group, we still allow the drop (it might do nothing or trigger fallback).
        event->acceptProposedAction();
        return;
    }

    if (event->mimeData()->hasUrls() ||
        event->mimeData()->hasFormat(QStringLiteral("application/x-afternight-frame-id")))
    {
        event->acceptProposedAction();
    }
    else
    {
        event->ignore();
    }
}

void FrameTreeView::dropEvent(QDropEvent* event)
{
    if (event)
    {
        m_impl->lastDropModifiers = event->modifiers();
    }

    // Always clear drag state when drop occurs
    m_impl->draggedFrames.clear();
    m_impl->draggingInternal = false;

    if (event->mimeData()->hasFormat(QStringLiteral("application/x-afternight-frame-id")))
    {
        QByteArray encodedData = event->mimeData()->data(QStringLiteral("application/x-afternight-frame-id"));
        QDataStream stream(&encodedData, QIODevice::ReadOnly);
        QStringList paths;
        while (!stream.atEnd())
        {
            QString path;
            stream >> path;
            paths << path;
        }

        if (paths.isEmpty())
        {
            return;
        }

        // Internal move across groups within the same tree?
        const QModelIndex targetIdx = indexAt(event->position().toPoint());
        if (targetIdx.isValid() && m_impl->model && m_impl->model->dataModel())
        {
            QModelIndex targetGroupIdx = targetIdx;
            if (m_impl->model->isFrame(targetIdx))
            {
                targetGroupIdx = m_impl->model->parent(targetIdx);
            }

            if (m_impl->model->isGroup(targetGroupIdx))
            {
                IntegrationDataModel* dataModel = m_impl->model->dataModel();
                const GroupKey targetGroupKey = m_impl->model->getGroupKey(targetGroupIdx);
                GroupingEngine engine;

                QString targetObject, targetSession;
                GroupKey::parseMetadata(targetGroupKey, m_impl->frameType, m_impl->groupingOptions, &targetObject,
                                        &targetSession);

                QStringList compatiblePaths;
                bool needsMetadataUpdate = false;
                QStringList metadataChanges;

                for (const QString& path : paths)
                {
                    FrameId id(path);
                    // Only consider frames that already belong to this tree's frame type.
                    auto frame_opt = dataModel->frames(m_impl->frameType).findById(id);
                    if (!frame_opt.has_value())
                    {
                        continue;
                    }

                    if (engine.checkCompatibilityForMove(*frame_opt, targetGroupKey, m_impl->frameType,
                                                         m_impl->groupingOptions))
                    {
                        compatiblePaths << path;

                        const bool objectDiff =
                            (frame_opt->effectiveObjectName() != targetObject && m_impl->groupingOptions.byObject);
                        const bool sessionDiff =
                            (frame_opt->effectiveSession() != targetSession && m_impl->groupingOptions.bySession);

                        if (objectDiff || sessionDiff)
                        {
                            needsMetadataUpdate = true;
                            QString change = QFileInfo(path).fileName() + QStringLiteral(": ");
                            if (objectDiff)
                            {
                                change += tr("Object '%1' -> '%2'").arg(frame_opt->effectiveObjectName(), targetObject);
                            }
                            if (objectDiff && sessionDiff)
                            {
                                change += QStringLiteral(", ");
                            }
                            if (sessionDiff)
                            {
                                change += tr("Session '%1' -> '%2'").arg(frame_opt->effectiveSession(), targetSession);
                            }
                            metadataChanges << change;
                        }
                    }
                }

                if (!compatiblePaths.isEmpty())
                {
                    if (needsMetadataUpdate)
                    {
                        QString message = tr("Moving %1 frames to a different group will change their metadata to "
                                             "match the destination group.")
                                              .arg(compatiblePaths.size());
                        message += QStringLiteral("\n\n") +
                                   tr("Target Group: %1")
                                       .arg(targetGroupKey.toDisplayString(m_impl->groupingOptions, m_impl->frameType));

                        if (metadataChanges.size() <= 10)
                        {
                            message += QStringLiteral("\n\n") + tr("Changes:") + QStringLiteral("\n") +
                                       metadataChanges.join(QStringLiteral("\n"));
                        }
                        else
                        {
                            message +=
                                QStringLiteral("\n\n") + tr("... and %1 more frames.").arg(metadataChanges.size() - 10);
                        }

                        // Asked without a nested event loop: the drop itself is declined, and the frames are moved
                        // once the person confirms.
                        event->ignore();
                        auto* box = new QMessageBox(QMessageBox::Question, tr("Confirm Move"), message,
                                                    QMessageBox::Yes | QMessageBox::No, this);
                        box->setAttribute(Qt::WA_DeleteOnClose);
                        const QPointer<GroupTreeModel> treeModel(m_impl->model);
                        const auto groupingOptions = m_impl->groupingOptions;
                        QObject::connect(box, &QDialog::finished, this,
                                         [box, treeModel, compatiblePaths, targetObject, targetSession,
                                          groupingOptions](const int result) {
                                             QMessageBox::StandardButton button =
                                                 box->standardButton(box->clickedButton());
                                             if (button == QMessageBox::NoButton)
                                             {
                                                 button = static_cast<QMessageBox::StandardButton>(result);
                                             }
                                             if (button != QMessageBox::Yes || !treeModel)
                                             {
                                                 return;
                                             }
                                             // Route the actual updates through the GroupTreeModel so undo is
                                             // recorded when available.
                                             treeModel->moveFramesToGroup(compatiblePaths, targetObject, targetSession,
                                                                          groupingOptions);
                                         });
                        box->open();
                        return;
                    }

                    event->acceptProposedAction();
                    return;
                }
            }
        }

        // Fallback: cross-tree move (e.g. Light to Dark) or drop on empty area.
        emit framesMoved(paths);
        event->acceptProposedAction();
        return;
    }

    if (event->mimeData()->hasUrls())
    {
        QStringList paths;
        for (const auto& url : event->mimeData()->urls())
        {
            if (url.isLocalFile())
            {
                paths << url.toLocalFile();
            }
        }

        if (!paths.isEmpty())
        {
            emit filesDropped(paths);
            event->acceptProposedAction();
        }
    }
}

Qt::KeyboardModifiers FrameTreeView::lastDropModifiers() const
{
    return m_impl ? m_impl->lastDropModifiers : Qt::NoModifier;
}

void FrameTreeView::dragLeaveEvent(QDragLeaveEvent* event)
{
    m_impl->draggedFrames.clear();
    m_impl->draggingInternal = false;
    QTreeView::dragLeaveEvent(event);
}

void FrameTreeView::contextMenuEvent(QContextMenuEvent* event)
{
    if (!event)
    {
        return;
    }

    // Match common UX: right-clicking an unselected item selects it first,
    // so the context menu matches what the user actually clicked.
    if (m_impl->model && selectionModel())
    {
        QModelIndex hit = indexAt(event->pos());
        if (!hit.isValid())
        {
            // If the user right-clicks in the empty horizontal space of a row,
            // indexAt() can return invalid. Retrying at x=0 preserves the row.
            hit = indexAt(QPoint(0, event->pos().y()));
        }

        const QModelIndex hitName = hit.isValid() ? hit.sibling(hit.row(), GroupTreeModel::ColName) : QModelIndex();
        if (hitName.isValid())
        {
            const bool hitIsGroup = m_impl->model->isGroup(hitName);

            bool selectionContainsFrames = false;
            if (hitIsGroup)
            {
                const QModelIndexList selected = selectionModel()->selectedRows(GroupTreeModel::ColName);
                for (const QModelIndex& idx : selected)
                {
                    if (m_impl->model->isFrame(idx))
                    {
                        selectionContainsFrames = true;
                        break;
                    }
                }
            }

            if (!selectionModel()->isSelected(hitName) || selectionContainsFrames)
            {
                selectionModel()->setCurrentIndex(hitName,
                                                  QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            }
        }
    }

    if (!selectionModel() || selectionModel()->selectedRows().isEmpty())
    {
        return;
    }

    // The receiver computes selected indices directly; avoid building potentially huge path lists here.
    emit contextMenuRequested(event->globalPos(), {});
}

void FrameTreeView::selectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
    QTreeView::selectionChanged(selected, deselected);
    emit frameSelectionChanged(); // Emit custom signal (conflict with base signal name resolved by signature)
}

void FrameTreeView::keyPressEvent(QKeyEvent* event)
{
    if (!event)
    {
        QTreeView::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Escape)
    {
        if (selectionModel())
        {
            selectionModel()->clearSelection();
        }
        event->accept();
        return;
    }

    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_A)
    {
        selectAll();
        event->accept();
        return;
    }

    if ((event->key() == Qt::Key_Home || event->key() == Qt::Key_End) && m_impl->model)
    {
        QModelIndex target;

        if (event->key() == Qt::Key_Home)
        {
            for (int g = 0; g < m_impl->model->rowCount(QModelIndex()); ++g)
            {
                const QModelIndex group = m_impl->model->index(g, 0, QModelIndex());
                if (!group.isValid())
                {
                    continue;
                }

                const int child_rows = m_impl->model->rowCount(group);
                if (child_rows > 0)
                {
                    target = m_impl->model->index(0, 0, group);
                    break;
                }

                if (!target.isValid())
                {
                    target = group;
                }
            }
        }
        else
        {
            for (int g = m_impl->model->rowCount(QModelIndex()) - 1; g >= 0; --g)
            {
                const QModelIndex group = m_impl->model->index(g, 0, QModelIndex());
                if (!group.isValid())
                {
                    continue;
                }

                const int child_rows = m_impl->model->rowCount(group);
                if (child_rows > 0)
                {
                    target = m_impl->model->index(child_rows - 1, 0, group);
                    break;
                }

                if (!target.isValid())
                {
                    target = group;
                }
            }
        }

        if (target.isValid() && selectionModel())
        {
            selectionModel()->setCurrentIndex(target, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            scrollTo(target);
        }

        event->accept();
        return;
    }

    QTreeView::keyPressEvent(event);
}

void FrameTreeView::paintEvent(QPaintEvent* event)
{
    // Check if model is empty
    bool isEmpty = true;
    if (m_impl->model)
    {
        const int totalCount = m_impl->model->totalCount();
        isEmpty = (totalCount == 0);
    }

    if (!isEmpty)
    {
        // Model has frames, use default tree view rendering
        QTreeView::paintEvent(event);
        return;
    }

    // Draw empty state
    QTreeView::paintEvent(event); // Draw the tree background first

    QPainter painter(viewport());
    an::ui::PainterGuard painterGuard(&painter);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    // Get viewport rectangle and compute layout based on available space
    const QRect rect = viewport()->rect();
    const auto& tokenSet = an::ui::tokens();
    const auto& spacing = tokenSet.spacing;

    // Skip drawing if viewport is too small
    const int minimumEmptyHeight = spacing.xl * 6;
    const int minimumEmptyWidth = spacing.xl * 8;
    if (rect.height() < minimumEmptyHeight || rect.width() < minimumEmptyWidth)
    {
        return;
    }

    const int centerX = rect.center().x();
    const int centerY = rect.center().y();

    // Scale icon size based on viewport height for better responsiveness
    const int minimumIconSize = spacing.xl * 2 + spacing.lg;
    const int maximumIconSize = spacing.xl * 4;
    const int iconSize = qBound(minimumIconSize, rect.height() / 4, maximumIconSize);

    // Use palette colors for theme compatibility
    const QColor iconColor = palette().color(QPalette::Mid);
    const QColor iconAccentColor = palette().color(QPalette::Midlight);
    const QColor textColor = palette().color(QPalette::Text);
    const QColor subtextColor = palette().color(QPalette::PlaceholderText);

    // Compute folder dimensions using integer arithmetic
    const int folderWidth = (iconSize * 4) / 5; // 80% of iconSize
    const int folderHeight = (iconSize * 13) / 20; // 65% of iconSize
    const int folderX = centerX - folderWidth / 2;
    const int folderY = centerY - folderHeight / 2 - iconSize / 3;

    // Tab dimensions
    const int tabWidth = (folderWidth * 2) / 5;
    const int tabHeight = (std::max)(spacing.sm, iconSize / 8);
    const int cornerRadius = (std::max)(tokenSet.radius.sm, iconSize / 16);

    // Draw folder tab
    painter.setPen(Qt::NoPen);
    painter.setBrush(iconColor);
    painter.setOpacity(0.55);

    const QRect tabRect(folderX, folderY - tabHeight + 2, tabWidth, tabHeight);
    painter.drawRoundedRect(tabRect, cornerRadius / 2, cornerRadius / 2);

    // Draw folder body
    const QRect bodyRect(folderX, folderY, folderWidth, folderHeight);
    painter.drawRoundedRect(bodyRect, cornerRadius, cornerRadius);

    // Draw plus sign with lighter color for better contrast
    painter.setBrush(iconAccentColor);
    painter.setOpacity(0.85);

    const int plusSize = (std::max)(spacing.xl - spacing.xs, folderHeight / 2);
    const int plusThickness = (std::max)(spacing.xs, plusSize / 6);
    const QPoint plusCenter = bodyRect.center();

    // Horizontal bar of plus
    const QRect hBar(plusCenter.x() - plusSize / 2, plusCenter.y() - plusThickness / 2, plusSize, plusThickness);
    painter.drawRoundedRect(hBar, plusThickness / 2, plusThickness / 2);

    // Vertical bar of plus
    const QRect vBar(plusCenter.x() - plusThickness / 2, plusCenter.y() - plusSize / 2, plusThickness, plusSize);
    painter.drawRoundedRect(vBar, plusThickness / 2, plusThickness / 2);

    // Layout spacing
    const int textGap = spacing.md;
    const int textTop = bodyRect.bottom() + textGap;

    // Draw main text
    painter.setOpacity(1.0);
    painter.setPen(textColor);
    QFont mainFont = tokenSet.typography.heading2;
    painter.setFont(mainFont);

    const QString mainText = tr("No Frames Loaded");
    const QFontMetrics mainFm(mainFont);
    const int mainTextHeight = mainFm.height();
    const QRect mainTextRect(rect.left() + spacing.lg, textTop, rect.width() - spacing.lg * 2,
                             mainTextHeight + spacing.xs);
    painter.drawText(mainTextRect, Qt::AlignHCenter | Qt::AlignTop, mainText);

    // Draw instruction text
    painter.setOpacity(0.75);
    painter.setPen(subtextColor);
    QFont subFont = tokenSet.typography.body;
    painter.setFont(subFont);
    const QFontMetrics subFm(subFont);

    const QString frameTypeName = [this]() {
        switch (m_impl->frameType)
        {
        case FrameType::Light:
            return tr("light");
        case FrameType::Dark:
            return tr("dark");
        case FrameType::Flat:
            return tr("flat");
        case FrameType::Bias:
            return tr("bias");
        default:
            return tr("calibration");
        }
    }();

    const QString instructionText =
        tr("Drag %1 files or folders here, or use Add Files / Add Folder. Choose Recursive to include subfolders.")
            .arg(frameTypeName);
    const int instructionTop = mainTextRect.bottom() + spacing.sm;
    const QRect instructionRect(rect.left() + spacing.xl, instructionTop, rect.width() - spacing.xl * 2,
                                subFm.height() * 3 + spacing.md);
    painter.drawText(instructionRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, instructionText);
}
