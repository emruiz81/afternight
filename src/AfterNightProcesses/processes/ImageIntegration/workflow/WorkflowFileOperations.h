// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"

#include <QFile>
#include <QFileInfo>
#include <QString>

/**
 * @brief Copy one workflow file and count its payload only after a successful copy.
 */
[[nodiscard]] inline bool copyWorkflowFile(const QString& sourcePath, const QString& destinationPath,
                                           WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    if (counters == nullptr)
    {
        return QFile::copy(sourcePath, destinationPath);
    }

    const QFileInfo sourceInfo(sourcePath);
    if (!QFile::copy(sourcePath, destinationPath))
    {
        return false;
    }

    if (sourceInfo.size() > 0)
    {
        counters->addBytesCopied(static_cast<std::uint64_t>(sourceInfo.size()));
    }
    return true;
}
