// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

#include <QSet>
#include <QStringList>

#include <vector>

class QDir;

namespace ImageIntegration
{

void openIntegratedOutputsForManifest(const QDir& outputDir,
                                      const std::vector<WorkflowManifest::IntegrationGroup>& integrationGroups,
                                      bool openIntegratedFiles, const QSet<QString>& skippedIntegrationGroupKeys = {},
                                      const QStringList& extraOutputFiles = {});
void openIntegratedOutputFiles(const QStringList& outputFiles, bool openIntegratedFiles);

} // namespace ImageIntegration
