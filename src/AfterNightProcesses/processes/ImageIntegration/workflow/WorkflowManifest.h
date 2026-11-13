// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file WorkflowManifest.h
 * @brief Data structure describing a complete integration workflow.
 *
 * The WorkflowManifest is a serializable description of all stages,
 * input files, output paths, and settings for an integration run.
 * It decouples workflow definition from execution.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/ValidationResult.h"
#include "processes/ImageIntegration/workflow/FrameHeaderReader.h"
#include "app/PlateSolveTypes.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <vector>

namespace ImageIntegration
{
[[nodiscard]] AFTERNIGHTPROCESSES_API QString astrometryApiKeyHash(QString apiKey);
}

/// Execution mode for the workflow. A manifest stores it as its value, which an enumerator keeps whatever the
/// declaration order. The values 1, 3 and 4 named calibration-only, normalization-only and stacking-only modes that
/// no run ever started; they name nothing now and are not to be given to another mode, so a manifest that stores one
/// reads as a full run.
enum class ExecutionMode
{
    Full = 0, ///< Execute all stages
    RegistrationOnly = 2, ///< The calibration and registration stages the registration's inputs need
    Incremental = 5 ///< Only changed groups
};

/// Every value a manifest may store for its execution mode.
inline constexpr std::array kPersistedExecutionModes = {ExecutionMode::Full, ExecutionMode::RegistrationOnly,
                                                        ExecutionMode::Incremental};

/**
 * @struct WorkflowManifest
 * @brief Complete description of an integration workflow.
 *
 * Contains:
 * - Version number for schema evolution
 * - Output directory path
 * - CalibrationStage entries (bias, dark, flat groups)
 * - IntegrationStage entries (light groups with calibration references)
 * - Serialization support via toJson()/fromJson()
 *
 * Built by ManifestBuilder, executed by WorkflowExecutor.
 */
struct AFTERNIGHTPROCESSES_API WorkflowManifest
{
    /// The format the manifest writer writes, stated as `version`; a manifest read at another is never reused.
    static constexpr int kFormatVersion = 1;

    /// The format the manifest states: `kFormatVersion` for one this version made, zero for one read that states none.
    int version = kFormatVersion;
    QString executionId; ///< Unique ID linking all groups produced by a single workflow run
    QString outputDirectory;
    bool reprocessExisting = false;
    bool calibrateLightsInMemory = true;
    ExecutionMode executionMode = ExecutionMode::Full;
    QString executionScope = QStringLiteral("full");
    QString executionScopeKey;
    QString processLabel;
    QString processingProfileId;
    QString processingProfileLabel;
    int maxParallelCores = 0; ///< 0 = use all available cores.
    bool combineRgbImages = false; ///< Generate combined RGB masters from eligible mono R/G/B stacks.

    using PlateSolveSettings = an::ui::PlateSolveSettings;
    PlateSolveSettings plateSolve;
    QString astrometryApiKeyHash; ///< Deterministic non-secret hash used for change detection.

    // The integration groups the plan processes, as one list for the run's metadata and its windows.
    // Stage execution uses the stage-specific incremental* vectors below.
    std::vector<GroupKey> dirtyGroups;
    std::vector<GroupKey> incrementalBiasGroups;
    std::vector<GroupKey> incrementalDarkGroups;
    std::vector<GroupKey> incrementalFlatGroups;
    std::vector<GroupKey> incrementalRegistrationGroups;
    std::vector<IntegrationGroupKey> incrementalIntegrationGroups;

    struct CalibrationStage
    {
        QString frameType;
        struct Group
        {
            GroupKey groupKey;
            QString outputFilename;
            std::vector<FrameId> inputFiles;
            StackingSettings stacking;
            bool skipIfExists = false;
            std::optional<FrameId> existingMaster;
            // The masters a flat group's frames are calibrated with as they are stacked, matched as a light of the
            // flat's exposure is: darkMaster alone when a dark matches - in CCD mode too, since every master dark
            // holds its bias - and biasMaster when none does. A bias or dark group names neither: its master is
            // stacked from its frames as they are. Not used for a user-defined master.
            std::optional<FrameId> biasMaster;
            std::optional<FrameId> darkMaster;
            // The normalization a bias or dark group still states - saved before its choice was fixed at None - which
            // its master is combined without: said in the stage's notes, and kept out of `stacking`, the settings the
            // master is made with and known by, so a choice that changes nothing re-stacks nothing.
            NormalizationMode statedNormalization = NormalizationMode::None;
        };
        std::vector<Group> groups;
    };

    std::optional<CalibrationStage> biasStage;
    std::optional<CalibrationStage> darkStage;
    std::optional<CalibrationStage> flatStage;

    struct CalibrationSettings
    {
        bool cosmeticCorrection = false;
        double sigmaHot = 3.0;
        double sigmaCold = 3.0;
        bool darkOptimization = false;
        double darkMultiplicationFactor = 1.0;
        bool flatAutoLevel = true;
        double pedestal = 0.0;
        bool subtractBiasFromDark = false; // CCD mode.
    };

    struct LightsCalibrationParams
    {
        FrameId lightPath;
        std::optional<FrameId> biasPath;
        std::optional<FrameId> darkPath;
        std::optional<FrameId> flatPath;
        CalibrationSettings settings;

        /// Whether the entry changes its light - a master or a pedestal - decided without reading a master, as
        /// `core::LightCalibration::calibrates` answers for the calibration made of it. An entry with neither leaves
        /// its light as it is, and no calibrated copy of it is written.
        [[nodiscard]] bool calibrates() const noexcept
        {
            return biasPath.has_value() || darkPath.has_value() || flatPath.has_value() || settings.pedestal > 0.0;
        }
    };
    std::vector<LightsCalibrationParams> lightsCalibration;

    /// A light group marked pre-calibrated: its lights have no calibration entry, as a group no master matched has
    /// none, so the manifest names the group and the report tells the two apart.
    struct PreCalibratedLightGroup
    {
        GroupKey groupKey;
        std::vector<FrameId> lightFiles;
    };
    std::vector<PreCalibratedLightGroup> preCalibratedLightGroups;

    /// A light the user left out of the run. The grouping drops a light excluded in the lights' list before a group
    /// is made of it, and a light excluded from one stack is dropped from that integration group alone, so no group
    /// names either: the manifest lists them here, each under the integration group it would have joined - which need
    /// not exist, since a group whose every light is left out is not made. The list is a record for the run's report.
    /// Nothing a run decides reads it: no hash, no comparison of two manifests and no stage.
    struct LeftOutLight
    {
        /// Which of the two exclusions left the light out.
        enum class Kind : unsigned char
        {
            Excluded, ///< Excluded in the lights' list: the light takes no part in the run.
            StackExcluded ///< Excluded from its group's stack: the light is still calibrated and registered.
        };

        FrameId light;
        Kind kind = Kind::Excluded;
        IntegrationGroupKey groupKey;
    };
    std::vector<LeftOutLight> leftOutLights;

    struct RegistrationGroup
    {
        GroupKey groupKey;
        QString resolution;
        QString object;
        std::vector<FrameId> lightFiles;
        RegistrationSettings settings;
        std::optional<FrameId> referenceFrame;
        // Optional per-light comet timing metadata aligned with lightFiles.
        // Empty/invalid values allow the registration stage to fall back to image metadata.
        std::vector<double> lightExposureSeconds;
        std::vector<QString> lightCaptureTimestampsUtc;
    };
    std::vector<RegistrationGroup> registrationGroups;

    struct IntegrationGroup
    {
        IntegrationGroupKey groupKey;
        QString outputFilename;
        QString resolution;
        QString object;
        QString filter;
        QString exposure;
        std::vector<FrameId> lightFiles;
        // Optional per-light exposure seconds aligned with lightFiles.
        // 0 or negative values indicate unavailable exposure metadata.
        std::vector<double> lightExposureSeconds;
        StackingSettings stacking;
        std::vector<FrameId> normalizationReferenceFrames;
        std::optional<QString> normalizationCoefficientsArtifactPath;
        enum class DrizzleMapPersistencePolicy : unsigned char
        {
            CompactDescriptorPreferred,
            DenseCheckpoint
        };
        struct DrizzleSettings
        {
            bool enabled = false;
            double scale = 2.0;
            double pixelFraction = 0.9;
            QString kernel = QStringLiteral("Square");
            bool cfaAware = false;
            QString cfaPattern = QStringLiteral("RGGB");
            DrizzleMapPersistencePolicy mapPersistencePolicy = DrizzleMapPersistencePolicy::CompactDescriptorPreferred;
        };
        std::optional<DrizzleSettings> drizzle;
        std::optional<QString> drizzlePixmapArtifactDir;
        std::optional<QString> drizzleAccumulatorArtifact;
        GroupKey registrationGroupKey;
        bool skipIfExists = false;
    };
    std::vector<IntegrationGroup> integrationGroups;

    QStringList stageSequence;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] WorkflowManifest redactedForPersistence() const;
    [[nodiscard]] static WorkflowManifest fromJson(const QJsonObject& json);

    /// Whether the manifest states the format the manifest writer writes.
    [[nodiscard]] bool isCurrentFormat() const noexcept
    {
        return version == kFormatVersion;
    }

    /// The manifest's problems. A group's frames are checked to be one size, against its declared resolution, only
    /// when a header reader is given: the executor always gives one, and a check on the GUI thread gives none, since
    /// reading every header there would block it.
    [[nodiscard]] WorkflowValidationResult validate(const ImageIntegration::FrameHeaderReader& headerReader = {}) const;
    [[nodiscard]] QString summary() const;
};
