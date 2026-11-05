// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/UndoCommands.h"

#include <QStringList>
#include <vector>

class QUndoStack;

namespace ImageIntegration::blink
{

[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<FrameAttributeChange>
buildExclusionChanges(const IntegrationDataModel* model, FrameType frameType, const QStringList& excludedPaths);

[[nodiscard]] AFTERNIGHTPROCESSES_API bool applyExclusions(IntegrationDataModel* model, QUndoStack* undoStack,
                                                           FrameType frameType, const QStringList& excludedPaths);

} // namespace ImageIntegration::blink
