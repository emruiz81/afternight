// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// The executor a front end runs: every stage in the workflow's order, committing each run through the archiver.

#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"

#include <memory>

namespace ImageIntegration
{

/// The executor with the nine stages in order - the three calibration masters, the lights' calibration,
/// registration, alignment, normalization and drizzle, and their stack - and the archiver as each run's archive.
///
/// @return the executor.
[[nodiscard]] std::unique_ptr<WorkflowExecutor> createDefaultWorkflowExecutor();

/// Gives an executor the default executor's nine stages, in order, and the archiver as each run's archive.
///
/// @param executor an executor with no stages.
AFTERNIGHTPROCESSES_API void composeDefaultWorkflowStages(WorkflowExecutor& executor);

} // namespace ImageIntegration
