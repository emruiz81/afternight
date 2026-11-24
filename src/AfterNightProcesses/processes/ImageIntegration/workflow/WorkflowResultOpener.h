// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Opening what a run wrote - its masters and the maps beside them - in views of the run's: they land in a workspace
/// and the person stays where they are, on the run's page or elsewhere.

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

#include <QSet>
#include <QStringList>

#include <vector>

class QDir;

namespace ImageIntegration
{

/// What the end of a run opens, in the order it opens: a set for each integration group - its master when
/// `openIntegratedFiles` asks for it, then the maps the artifact records say were written with that master, which
/// open whatever `openIntegratedFiles` says - and, when it asks, each added output that exists as a set of its own.
///
/// @param outputDir the folder the run wrote to.
/// @param integrationGroups the run's integration groups.
/// @param openIntegratedFiles whether the masters and the added outputs open.
/// @param skippedIntegrationGroupKeys groups left out: the channels a combined color master stands for.
/// @param extraOutputFiles outputs that are no group's, such as a combined color master.
/// @return the sets, none empty; the views of a set are docked beside its first.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<QStringList>
integratedOutputSets(const QDir& outputDir, const std::vector<WorkflowManifest::IntegrationGroup>& integrationGroups,
                     bool openIntegratedFiles, const QSet<QString>& skippedIntegrationGroupKeys = {},
                     const QStringList& extraOutputFiles = {});

/// Opens `integratedOutputSets` one set after another, each in views of the run's.
AFTERNIGHTPROCESSES_API void
openIntegratedOutputsForManifest(const QDir& outputDir,
                                 const std::vector<WorkflowManifest::IntegrationGroup>& integrationGroups,
                                 bool openIntegratedFiles, const QSet<QString>& skippedIntegrationGroupKeys = {},
                                 const QStringList& extraOutputFiles = {});

/// Opens each output that exists, one after another and each on its own, in views of the run's; nothing when
/// `openIntegratedFiles` is false.
AFTERNIGHTPROCESSES_API void openIntegratedOutputFiles(const QStringList& outputFiles, bool openIntegratedFiles);

} // namespace ImageIntegration
