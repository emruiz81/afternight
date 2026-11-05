// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QObject>
#include <QPoint>
#include <QModelIndexList>
#include <verdigris/wobjectdefs.h>

#include "processes/ImageIntegration/model/FrameType.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"

class FrameTreeView;
class IntegrationDataModel;
class QMenu;
class QAction;
class QUndoStack;

class AFTERNIGHTPROCESSES_API TreeContextMenuHandler : public QObject
{
    W_OBJECT(TreeContextMenuHandler)

public:
    explicit TreeContextMenuHandler(IntegrationDataModel* model, QObject* parent = nullptr,
                                    QUndoStack* undoStack = nullptr);
    ~TreeContextMenuHandler() override;

    void showContextMenu(FrameTreeView* treeView, FrameType frameType, const QPoint& globalPos,
                         const QModelIndexList& selectedIndices);

    // For testing
    [[nodiscard]] QMenu* createContextMenu(FrameTreeView* treeView, FrameType frameType,
                                           const QModelIndexList& selectedIndices);

signals:
    void removeFramesRequested(FrameType type, const QStringList& paths) W_SIGNAL(removeFramesRequested, type, paths);
    void frameTypeChangeRequested(const QStringList& paths, FrameType newType)
        W_SIGNAL(frameTypeChangeRequested, paths, newType);
    void sessionChangeRequested(const QStringList& paths) W_SIGNAL(sessionChangeRequested, paths);
    void objectChangeRequested(const QStringList& paths) W_SIGNAL(objectChangeRequested, paths);
    void customTagChangeRequested(const QStringList& paths) W_SIGNAL(customTagChangeRequested, paths);
    void masterStatusChangeRequested(const QString& path, bool isMaster)
        W_SIGNAL(masterStatusChangeRequested, path, isMaster);
    void referenceFrameRequested(const QString& path) W_SIGNAL(referenceFrameRequested, path);
    void blinkRequested(FrameType frameType, const QStringList& paths) W_SIGNAL(blinkRequested, frameType, paths);
    // Navigation helpers
    // - Calibration: works for any frame type, and for single group selection.
    // - Registration/Integration: only meaningful for Lights and single-frame selection.
    void showInCalibrationTableRequested(FrameType frameType, const QString& groupKey)
        W_SIGNAL(showInCalibrationTableRequested, frameType, groupKey);
    void showInRegistrationTableRequested(const QString& framePath)
        W_SIGNAL(showInRegistrationTableRequested, framePath);
    void showInIntegrationTableRequested(const QString& framePath) W_SIGNAL(showInIntegrationTableRequested, framePath);
    void collapseAllRequested() W_SIGNAL(collapseAllRequested);
    void expandAllRequested() W_SIGNAL(expandAllRequested);

private:
    void buildActionsForFrameType(QMenu& menu, FrameType type, const QModelIndexList& selectedIndices);
    void buildActionsForGroup(QMenu& menu, FrameType type, const QModelIndexList& selectedIndices);
    void buildActionsForMixedSelection(QMenu& menu, FrameType type, const QModelIndexList& selectedIndices);

    IntegrationDataModel* m_model = nullptr;
    QUndoStack* m_undoStack = nullptr;
};
