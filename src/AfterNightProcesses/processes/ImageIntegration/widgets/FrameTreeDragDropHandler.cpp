// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FrameTreeDragDropHandler.h"
#include "FrameTreeView.h"

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(FrameTreeDragDropHandler)

FrameTreeDragDropHandler::FrameTreeDragDropHandler(QObject* parent) : QObject(parent) {}

FrameTreeDragDropHandler::~FrameTreeDragDropHandler() = default;

void FrameTreeDragDropHandler::registerDropTarget(FrameTreeView* targetTree, FrameType targetType)
{
    if (!targetTree)
        return;

    connect(targetTree, &FrameTreeView::framesMoved, this,
            [this, targetType](const QStringList& paths) { handleDrop(paths, targetType); });
}

void FrameTreeDragDropHandler::handleDrop(const QStringList& paths, FrameType targetType)
{
    if (paths.isEmpty())
        return;

    emit frameTypeChangeRequested(paths, targetType);
}
