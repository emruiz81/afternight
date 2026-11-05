// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/model/GroupingKeys.h"

#include <QMap>
#include <QSet>
#include <QString>

#include <optional>
#include <vector>

/**
 * @enum DirtyReason
 * @brief Categorizes why a component became dirty.
 */
enum class DirtyReason
{
    Initial, ///< First run, no previous state
    GroupingOptionsChanged, ///< Frame grouping changed
    StackingSettingsChanged, ///< Stacking method/rejection changed
    RegistrationSettingsChanged, ///< Star detection or transformation changed
    CalibrationChanged, ///< Calibration assignments changed
    UpstreamDirty, ///< A dependency was marked dirty
    ManualReprocess, ///< User requested reprocess
    ArtifactMissing, ///< Output file doesn't exist
    ArtifactStale ///< Output file exists but settings hash differs
};

/**
 * @struct DirtyComponent
 * @brief Tracks a single dirty workflow component.
 */
struct DirtyComponent
{
    QString componentId; ///< e.g., "Bias|6144x4128|800ISO" or "Reg|6144x4128|M31"
    DirtyReason reason;
    QString details; ///< Human-readable explanation
    std::optional<QString> upstreamCause; ///< If reason is UpstreamDirty
};

/**
 * @class DirtyStateTracker
 * @brief Manages dirty state for all workflow components.
 */
class AFTERNIGHTPROCESSES_API DirtyStateTracker
{
public:
    // NOTE: Not thread-safe. Intended for single-threaded planning/execution planning usage.
    DirtyStateTracker() = default;

    // Mark specific components dirty
    void markBiasGroupDirty(const GroupKey& key, DirtyReason reason, const QString& details = {});
    void markDarkGroupDirty(const GroupKey& key, DirtyReason reason, const QString& details = {});
    void markFlatGroupDirty(const GroupKey& key, DirtyReason reason, const QString& details = {});
    void markRegistrationGroupDirty(const GroupKey& key, DirtyReason reason, const QString& details = {});
    void markIntegrationGroupDirty(const IntegrationGroupKey& key, DirtyReason reason, const QString& details = {});

    // Propagate dirty state downstream
    void propagateBiasDirtyToConsumers(const GroupKey& biasKey, const std::vector<GroupKey>& darkGroups,
                                       const std::vector<GroupKey>& flatGroups,
                                       const std::vector<GroupKey>& registrationGroups);

    void propagateDarkDirtyToConsumers(const GroupKey& darkKey, const std::vector<GroupKey>& flatGroups,
                                       const std::vector<GroupKey>& registrationGroups);

    void propagateFlatDirtyToConsumers(const GroupKey& flatKey, const std::vector<GroupKey>& registrationGroups);

    void propagateRegistrationDirtyToIntegration(const GroupKey& registrationKey,
                                                 const std::vector<IntegrationGroupKey>& integrationGroups);

    // Query dirty state
    [[nodiscard]] bool isBiasGroupDirty(const GroupKey& key) const;
    [[nodiscard]] bool isDarkGroupDirty(const GroupKey& key) const;
    [[nodiscard]] bool isFlatGroupDirty(const GroupKey& key) const;
    [[nodiscard]] bool isRegistrationGroupDirty(const GroupKey& key) const;
    [[nodiscard]] bool isIntegrationGroupDirty(const IntegrationGroupKey& key) const;

    // Get all dirty components for a stage
    [[nodiscard]] std::vector<GroupKey> dirtyBiasGroups() const;
    [[nodiscard]] std::vector<GroupKey> dirtyDarkGroups() const;
    [[nodiscard]] std::vector<GroupKey> dirtyFlatGroups() const;
    [[nodiscard]] std::vector<GroupKey> dirtyRegistrationGroups() const;
    [[nodiscard]] std::vector<IntegrationGroupKey> dirtyIntegrationGroups() const;

    // Get dirty component details (for logging)
    [[nodiscard]] std::optional<DirtyComponent> getDirtyDetails(const QString& componentId) const;
    [[nodiscard]] std::vector<DirtyComponent> allDirtyComponents() const;

    // Clear state
    void clear();
    void clearBiasGroups();
    void clearDarkGroups();
    void clearFlatGroups();
    void clearRegistrationGroups();
    void clearIntegrationGroups();

    // Summary for logging
    [[nodiscard]] QString toSummaryString() const;

private:
    void markDirty(const QString& id, DirtyReason reason, const QString& details,
                   const std::optional<QString>& upstream = std::nullopt);
    [[nodiscard]] bool isDirty(const QString& id) const;

    QMap<QString, DirtyComponent> m_dirtyComponents;
};
