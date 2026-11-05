// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file ManifestDiff.h
 * @brief Comparison utilities for detecting manifest changes.
 *
 * Provides structures and functions for comparing two workflow manifests
 * to determine which stages need re-execution when inputs change.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "WorkflowManifest.h"

#include <QJsonObject>
#include <QString>

#include <vector>

/**
 * @struct ManifestDiff
 * @brief Result of comparing two workflow manifests.
 *
 * Tracks which calibration groups and integration stages have changed
 * between an old manifest and a new one. Used for incremental processing:
 * only re-run stages whose inputs have actually changed.
 */
struct AFTERNIGHTPROCESSES_API ManifestDiff
{
    struct CalibrationGroupChange
    {
        QString frameType;
        GroupKey groupKey;
        enum class ChangeType
        {
            Added,
            Removed,
            Modified,
            Unchanged
        };
        ChangeType change = ChangeType::Unchanged;

        bool inputFilesChanged = false;
        bool stackingSettingsChanged = false;
        bool outputFilenameChanged = false;
        bool calibrationDependenciesChanged = false;
        bool calibrationFingerprintChanged = false;

        int previousInputCount = 0;
        int currentInputCount = 0;
    };
    std::vector<CalibrationGroupChange> calibrationChanges;

    struct RegistrationGroupChange
    {
        GroupKey groupKey;
        CalibrationGroupChange::ChangeType change = CalibrationGroupChange::ChangeType::Unchanged;

        bool lightFilesChanged = false;
        bool settingsChanged = false;
        bool referenceFrameChanged = false;

        int previousFrameCount = 0;
        int currentFrameCount = 0;
    };
    std::vector<RegistrationGroupChange> registrationChanges;

    struct IntegrationGroupChange
    {
        IntegrationGroupKey groupKey;
        QString outputFilename;
        CalibrationGroupChange::ChangeType change = CalibrationGroupChange::ChangeType::Unchanged;

        bool lightFilesChanged = false;
        bool stackingSettingsChanged = false;
        bool calibrationAssignmentsChanged = false;
        bool normalizationSettingsChanged = false;

        int previousFrameCount = 0;
        int currentFrameCount = 0;
    };
    std::vector<IntegrationGroupChange> integrationChanges;

    struct LightsCalibrationChange
    {
        FrameId lightPath;
        bool changed = false;
    };
    std::vector<LightsCalibrationChange> lightsCalibrationChanges;

    [[nodiscard]] bool hasChanges() const;
    [[nodiscard]] int totalAdditions() const;
    [[nodiscard]] int totalRemovals() const;
    [[nodiscard]] int totalModifications() const;

    [[nodiscard]] QString toSummaryText() const;
    [[nodiscard]] QJsonObject toJson() const;
};

[[nodiscard]] AFTERNIGHTPROCESSES_API ManifestDiff compareManifests(const WorkflowManifest& previous,
                                                                    const WorkflowManifest& current);
