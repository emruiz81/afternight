// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file ManifestBuilder.h
 * @brief Builder pattern for constructing workflow manifests.
 *
 * Provides a fluent API for creating WorkflowManifest objects
 * from IntegrationDataModel state with configurable options.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "WorkflowManifest.h"

#include <optional>

class IntegrationDataModel;

/**
 * @class ManifestBuilder
 * @brief Fluent builder for WorkflowManifest construction.
 *
 * Reads the current IntegrationDataModel state and builds a manifest
 * that describes which stages to execute and with what inputs.
 *
 * Options:
 * - setReprocessExisting(): re-run stages even if outputs exist
 * - setSkipEmptyGroups(): skip groups with no input files
 * - setInclude*Frames(): selectively enable/disable frame types
 *
 * @code
 * auto manifest = ManifestBuilder(model)
 *     .setReprocessExisting(true)
 *     .setIncludeBiasFrames(true)
 *     .build();
 * @endcode
 */
class AFTERNIGHTPROCESSES_API ManifestBuilder
{
public:
    explicit ManifestBuilder(const IntegrationDataModel& model);

    [[nodiscard]] WorkflowManifest build() const;
    [[nodiscard]] WorkflowManifest buildForIntegrationGroup(const IntegrationGroupKey& integrationGroupKey) const;

    ManifestBuilder& setReprocessExisting(bool reprocess);
    ManifestBuilder& setIncludeRejectionMaps(bool include);
    ManifestBuilder& setSkipEmptyGroups(bool skip);
    ManifestBuilder& setIncludeBiasFrames(bool include);
    ManifestBuilder& setIncludeDarkFrames(bool include);
    ManifestBuilder& setIncludeFlatFrames(bool include);
    ManifestBuilder& setIncludeLightFrames(bool include);

private:
    [[nodiscard]] WorkflowManifest buildInternal(std::optional<IntegrationGroupKey> integration_group) const;

    const IntegrationDataModel& m_model;
    bool m_reprocessExisting = false;
    bool m_includeRejectionMaps = false;
    bool m_skipEmptyGroups = true;
    bool m_includeBiasFrames = true;
    bool m_includeDarkFrames = true;
    bool m_includeFlatFrames = true;
    bool m_includeLightFrames = true;
};

/// The light a lights group's stack weighs its frames against - the light its width weighting penalizes another for
/// the stars it lacks against, and its normalization brings the others to - as the manifest names it to the stacking
/// stage: the group's normalization reference when it normalizes, the light chosen by hand or the best by the
/// registration's quality metrics, and otherwise its first light by identity, which the stage then takes.
///
/// @param model the model.
/// @param groupKey the lights group.
/// @return the reference; nothing for a group with no light its stack would read.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<FrameId>
stackingReferenceFrame(const IntegrationDataModel& model, const IntegrationGroupKey& groupKey);
