// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file ManifestDiff.h
 * @brief Comparison utilities for detecting manifest changes.
 *
 * Provides structures and functions for comparing two workflow manifests
 * to determine which stages need re-execution when inputs change.
 *
 * A light integration group's settings are compared stage by stage: every stacking setting is assigned to the light
 * stages whose work depends on it (`stackingSettingReaders`), so a change of the stacking method concerns the stack
 * alone and a change of a normalization setting the normalization. The plan re-runs a stage whose settings changed and
 * every stage that reads what it makes.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "WorkflowManifest.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <initializer_list>
#include <span>
#include <vector>

/// The stages of a light integration group after its registration that a change of its settings can re-run, in the
/// order a run reaches them.
enum class LightStage : unsigned char
{
    Alignment,
    Normalization,
    Drizzle,
    Stacking
};

/// A set of light stages.
class LightStages
{
public:
    constexpr LightStages() noexcept = default;

    constexpr LightStages(const std::initializer_list<LightStage> stages) noexcept
    {
        for (const LightStage stage : stages)
        {
            m_bits = static_cast<unsigned char>(m_bits | bit(stage));
        }
    }

    [[nodiscard]] constexpr bool contains(const LightStage stage) const noexcept
    {
        return (m_bits & bit(stage)) != 0u;
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return m_bits == 0u;
    }

    /// This set and `stage`.
    [[nodiscard]] constexpr LightStages with(const LightStage stage) const noexcept
    {
        LightStages result = *this;
        result.m_bits = static_cast<unsigned char>(result.m_bits | bit(stage));
        return result;
    }

    [[nodiscard]] constexpr bool operator==(const LightStages& other) const noexcept = default;

private:
    [[nodiscard]] static constexpr unsigned char bit(const LightStage stage) noexcept
    {
        return static_cast<unsigned char>(1u << static_cast<unsigned>(stage));
    }

    unsigned char m_bits = 0u;
};

/// What changed among the settings each light stage reads, stage by stage, in the words a plan states it in. A stage
/// with no reason reads none of what changed.
struct AFTERNIGHTPROCESSES_API LightStageReasons
{
    QStringList alignment;
    QStringList normalization;
    QStringList drizzle;
    QStringList stacking;

    [[nodiscard]] const QStringList& of(LightStage stage) const noexcept;

    /// Adds `reason` to each stage of `stages` that does not state it yet.
    void add(LightStages stages, const QString& reason);

    [[nodiscard]] bool any() const noexcept;

    /// The stages that read something that changed.
    [[nodiscard]] LightStages stages() const noexcept;
};

/// A stacking setting and the light stages whose work depends on it: a stage that reads it, or whose product it
/// changes. A setting no stage depends on states none.
struct StackingSettingReaders
{
    /// The field's name in StackingSettings; a field of a settings record it holds after the record's name and a dot.
    const char* name;
    LightStages stages;
    /// What a plan says when it changed.
    const char* change;
    /// Whether two settings hold the same value of it.
    bool (*same)(const StackingSettings& lhs, const StackingSettings& rhs);
};

/// Every field of StackingSettings, and of each settings record it holds, with the light stages that read it.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::span<const StackingSettingReaders> stackingSettingReaders() noexcept;

/// What changed between two stacking settings, stage by stage.
[[nodiscard]] AFTERNIGHTPROCESSES_API LightStageReasons changedStackingSettings(const StackingSettings& previous,
                                                                                const StackingSettings& current);

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

        /// The group's lights, or the registration group they are assigned to: every stage reads them.
        bool lightFilesChanged = false;
        bool calibrationAssignmentsChanged = false;

        /// What changed among the settings each stage reads: the stacking settings, the normalization's reference
        /// frames and whether colour masters are combined. A stage whose product another stage reads re-runs that
        /// stage too, which the plan decides.
        LightStageReasons settingsChanged;

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
