// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The default executor's composition: the nine stages in the workflow's order and the archiver.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/DefaultWorkflowStages.h"

#include "processes/ImageIntegration/workflow/WorkflowArchiver.h"
#include "processes/ImageIntegration/workflow/stages/BiasStackingStage.h"
#include "processes/ImageIntegration/workflow/stages/DarkStackingStage.h"
#include "processes/ImageIntegration/workflow/stages/FlatStackingStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsAlignmentStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsCalibrationStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsDrizzleStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsNormalizationStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsRegistrationStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsStackingStage.h"

namespace ImageIntegration
{

void composeDefaultWorkflowStages(WorkflowExecutor& executor)
{
    executor.addStage(std::make_unique<BiasStackingStage>());
    executor.addStage(std::make_unique<DarkStackingStage>());
    executor.addStage(std::make_unique<FlatStackingStage>());
    executor.addStage(std::make_unique<LightsCalibrationStage>());
    executor.addStage(std::make_unique<LightsRegistrationStage>());
    executor.addStage(std::make_unique<LightsAlignmentStage>());
    executor.addStage(std::make_unique<LightsNormalizationStage>());
    executor.addStage(std::make_unique<LightsDrizzleStage>());
    executor.addStage(std::make_unique<LightsStackingStage>());
    executor.setRunArchiveFactory(&makeWorkflowArchiverRunArchive);
}

} // namespace ImageIntegration
