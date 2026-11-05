// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file CalibrationMasterDefaults.h
 * @brief What a calibration master is called and how it is stacked by default, for the workflow and the tabs alike.
 *
 * The manifest builder and the Lite profile name a group's master and choose its stacking settings exactly as the
 * calibration panels show them, so the rules live below both rather than in the panels' utilities.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"

#include <QString>

#include <optional>
#include <vector>

// The file a generated calibration master is written to, as the workflow names it.
[[nodiscard]] QString calibrationMasterOutputFilename(CalibrationOverrideType type, const GroupKey& groupKey,
                                                      const GroupingOptions& options, FrameType frameType);

// The mean sensor temperature of the frames that state one.
[[nodiscard]] std::optional<double> averageTemperatureC(const std::vector<FrameInfo>& frames);

// Default stacking settings for calibration masters depend on frame type:
// - Flats: multiplicative normalization by default
// - Bias/Dark: normalization disabled
[[nodiscard]] AFTERNIGHTPROCESSES_API StackingSettings defaultCalibrationStackingSettings(FrameType frameType);

// Ensures calibration stacking settings obey frame-type constraints.
// Currently forces normalization disabled for Bias/Dark.
[[nodiscard]] AFTERNIGHTPROCESSES_API StackingSettings
normalizeCalibrationStackingSettings(FrameType frameType, const StackingSettings& settings);
