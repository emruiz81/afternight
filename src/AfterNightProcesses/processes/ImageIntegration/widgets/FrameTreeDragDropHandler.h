// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QObject>
#include <verdigris/wobjectdefs.h>
#include "processes/ImageIntegration/model/FrameType.h"

class FrameTreeView;

class AFTERNIGHTPROCESSES_API FrameTreeDragDropHandler : public QObject
{
    W_OBJECT(FrameTreeDragDropHandler)

public:
    explicit FrameTreeDragDropHandler(QObject* parent = nullptr);
    ~FrameTreeDragDropHandler() override;

    void registerDropTarget(FrameTreeView* targetTree, FrameType targetType);

signals:
    void frameTypeChangeRequested(const QStringList& paths, FrameType newType)
        W_SIGNAL(frameTypeChangeRequested, paths, newType);

private:
    void handleDrop(const QStringList& paths, FrameType targetType);
};
