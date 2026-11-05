// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/core/RegistrationWorkState.h"

#include <QSqlDatabase>
#include <QString>

#include <unordered_set>
#include <vector>

class ExecutionPreparationSnapshot;

struct RegistrationGroupWorkAnalysis
{
    WorkflowManifest::RegistrationGroup group;
    RegistrationWorkAnalysis analysis;
};

[[nodiscard]] AFTERNIGHTPROCESSES_API RegistrationWorkAnalysis
analyzeRegistrationWork(QSqlDatabase& db, const WorkflowManifest::RegistrationGroup& group);
[[nodiscard]] AFTERNIGHTPROCESSES_API RegistrationWorkAnalysis analyzeRegistrationWork(
    QSqlDatabase& db, const WorkflowManifest& manifest, const WorkflowManifest::RegistrationGroup& group);
[[nodiscard]] AFTERNIGHTPROCESSES_API RegistrationWorkAnalysis
analyzeRegistrationWork(const ExecutionPreparationSnapshot& snapshot, const RegistrationWorkCurrentState& currentState);
[[nodiscard]] AFTERNIGHTPROCESSES_API RegistrationWorkAnalysis
analyzeRegistrationWork(const ExecutionPreparationSnapshot& snapshot, const WorkflowManifest::RegistrationGroup& group);
[[nodiscard]] AFTERNIGHTPROCESSES_API RegistrationWorkAnalysis
analyzeRegistrationWork(const ExecutionPreparationSnapshot& snapshot, const WorkflowManifest& manifest,
                        const WorkflowManifest::RegistrationGroup& group);
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<RegistrationGroupWorkAnalysis>
analyzeRegistrationGroupsWork(QSqlDatabase& db, const std::vector<WorkflowManifest::RegistrationGroup>& groups);
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<RegistrationGroupWorkAnalysis>
analyzeRegistrationGroupsWork(const ExecutionPreparationSnapshot& snapshot,
                              const std::vector<WorkflowManifest::RegistrationGroup>& groups);
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<RegistrationGroupWorkAnalysis>
analyzeRegistrationGroupsWork(const ExecutionPreparationSnapshot& snapshot, const WorkflowManifest& manifest);
