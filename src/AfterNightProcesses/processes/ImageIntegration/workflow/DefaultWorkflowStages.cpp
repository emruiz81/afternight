// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The executor a front end runs, composed by `DefaultWorkflowComposition.cpp`.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/DefaultWorkflowStages.h"

namespace ImageIntegration
{

std::unique_ptr<WorkflowExecutor> createDefaultWorkflowExecutor()
{
    auto executor = std::make_unique<WorkflowExecutor>();
    composeDefaultWorkflowStages(*executor);
    return executor;
}

} // namespace ImageIntegration
