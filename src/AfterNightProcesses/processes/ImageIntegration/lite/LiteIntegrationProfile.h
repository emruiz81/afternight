// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/model/CalibrationMatcher.h"
#include "processes/ImageIntegration/model/FrameType.h"
#include "processes/ImageIntegration/model/GroupingOptions.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

struct WorkflowManifest;

namespace ImageIntegrationLite
{

enum class NormalizationPreset
{
    None,
    GlobalAdditive,
    GlobalAdditiveScaling,
    GlobalMultiplicative,
    GlobalMultiplicativeScaling,
    Local
};

enum class CalibrationPreset
{
    Standard,
    StrictMatching,
    Adaptive
};

struct ProcessingOptions
{
    QString stackMethod = QStringLiteral("Average");
    QString rejectionMethod = QStringLiteral("Auto");
    double rejectionLow = 0.0;
    double rejectionHigh = 0.0;
    QString weighting = QStringLiteral("None");
    QString transformation = QStringLiteral("Homography");
    bool autoReference = true;
    bool autoCrop = false;
    NormalizationPreset normalizationPreset = NormalizationPreset::GlobalAdditiveScaling;
    CalibrationPreset calibrationPreset = CalibrationPreset::Standard;
    bool darkOptimization = false;
    bool flatAutoLevel = true;
    bool solveFinalMasterIfNeeded = false;
    bool drizzleEnabled = false;
    double drizzleScale = 2.0;
    double drizzlePixelFraction = 0.9;
    bool limitMemory = false;
    int maxMemoryMb = 2048;
    int maxParallelCores = 0;
};

struct DatasetSummary
{
    int biasFrames = 0;
    int darkFrames = 0;
    int flatFrames = 0;
    int lightFrames = 0;
    QStringList filters;
    QStringList objects;
    double totalLightExposureSeconds = 0.0;
};

struct RejectionParameterDefaults
{
    double low = 3.0;
    double high = 3.0;
};

[[nodiscard]] AFTERNIGHTPROCESSES_API ProcessingOptions defaultProcessingOptions();
[[nodiscard]] RejectionParameterDefaults rejectionParameterDefaults(const QString& rejectionMethod);

[[nodiscard]] AFTERNIGHTPROCESSES_API GroupingOptions sourceGroupingOptions(FrameType type);
[[nodiscard]] AFTERNIGHTPROCESSES_API GroupingOptions lightIntegrationGroupingOptions();
[[nodiscard]] CalibrationMatchingPolicy
calibrationMatchingPolicy(CalibrationPreset preset = CalibrationPreset::Standard);
[[nodiscard]] LightCalibrationSettings lightCalibrationSettings(const ProcessingOptions& options);
[[nodiscard]] AFTERNIGHTPROCESSES_API RegistrationSettings registrationSettings();
[[nodiscard]] AFTERNIGHTPROCESSES_API RegistrationSettings registrationSettings(const ProcessingOptions& options);
[[nodiscard]] AFTERNIGHTPROCESSES_API StackingSettings stackingSettings();
[[nodiscard]] AFTERNIGHTPROCESSES_API StackingSettings stackingSettings(const ProcessingOptions& options);
[[nodiscard]] AFTERNIGHTPROCESSES_API StackingSettings calibrationStackingSettings(FrameType type,
                                                                                   const ProcessingOptions& options);
[[nodiscard]] AFTERNIGHTPROCESSES_API QString darkFlatCustomTag();
[[nodiscard]] bool isDarkFlatFrame(const FrameInfo& frame);

AFTERNIGHTPROCESSES_API void applyDefaults(IntegrationDataModel& model);
AFTERNIGHTPROCESSES_API void applyDefaults(IntegrationDataModel& model, const ProcessingOptions& options);
void applyProcessingOptions(IntegrationDataModel& model, const ProcessingOptions& options);
AFTERNIGHTPROCESSES_API void autoDetectSingleFrameCalibrationMasters(IntegrationDataModel& model);
AFTERNIGHTPROCESSES_API void applyManifestMetadata(WorkflowManifest& manifest);
AFTERNIGHTPROCESSES_API void applyExecutionOptions(WorkflowManifest& manifest, const ProcessingOptions& options);
AFTERNIGHTPROCESSES_API void applyDarkFlatCalibration(WorkflowManifest& manifest, const IntegrationDataModel& model,
                                                      bool reprocessExisting);
[[nodiscard]] AFTERNIGHTPROCESSES_API DatasetSummary summarizeDataset(const IntegrationDataModel& model);
[[nodiscard]] AFTERNIGHTPROCESSES_API WorkflowValidationResult validateReadiness(const IntegrationDataModel& model);

/// The first three readiness errors, joined into one line for a banner or an exception.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString firstReadinessErrors(const WorkflowValidationResult& validation);

/// The Lite manifest of a model: the full builder without rejection maps, then Lite's metadata, execution options
/// and dark-flat calibration.
[[nodiscard]] AFTERNIGHTPROCESSES_API WorkflowManifest buildManifest(const IntegrationDataModel& model,
                                                                     const ProcessingOptions& options,
                                                                     bool reprocessExisting);

/// What the Lite process prepares from a snapshot of its model: its defaults and single-frame masters applied, its
/// readiness checked - a model that is not ready throws std::runtime_error with `firstReadinessErrors` - and its
/// manifest built.
[[nodiscard]] AFTERNIGHTPROCESSES_API WorkflowManifest prepareManifest(const QJsonObject& modelSnapshot,
                                                                       const ProcessingOptions& options,
                                                                       bool reprocessExisting);

} // namespace ImageIntegrationLite
