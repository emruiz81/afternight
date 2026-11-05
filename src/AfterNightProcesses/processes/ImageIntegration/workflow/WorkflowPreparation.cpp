// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowPreparation.h"

#include "processes/ImageIntegration/core/CometStackingMode.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"

#include <AfterNightPlatform/Logging.h>
#include <AfterNightPlatform/SystemMemory.h>

#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

namespace ImageIntegration
{
namespace
{

[[nodiscard]] qint64 elapsedMilliseconds(const std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

// The preparation's messages keep the translation context of the front ends they came from.
[[nodiscard]] QString trImageIntegrationProcess(const char* sourceText)
{
    return QCoreApplication::translate("ImageIntegrationProcess", sourceText);
}

void report(const WorkflowPreparationProgress& progress, const int percent, const char* text)
{
    if (progress)
    {
        progress(percent, trImageIntegrationProcess(text));
    }
}

void throwIfCancelled(const WorkflowPreparationCancelled& cancelled)
{
    if (cancelled && cancelled())
    {
        throw std::runtime_error("Workflow preparation cancelled");
    }
}

void ensureCalibrationStageSelectionConsistency(WorkflowManifest& manifest)
{
    const QString calibrationStageName = QStringLiteral("LightsCalibration");
    manifest.stageSequence.removeAll(calibrationStageName);

    if (manifest.lightsCalibration.empty() || manifest.calibrateLightsInMemory)
    {
        return;
    }

    const int registrationIndex = manifest.stageSequence.indexOf(QStringLiteral("LightsRegistration"));
    if (registrationIndex >= 0)
    {
        manifest.stageSequence.insert(registrationIndex, calibrationStageName);
    }
    else
    {
        manifest.stageSequence.append(calibrationStageName);
    }
}

[[nodiscard]] bool isDrizzleEnabled(const WorkflowManifest::IntegrationGroup& group)
{
    return group.drizzle.has_value() && group.drizzle->enabled;
}

[[nodiscard]] bool cometNormalizationRequiresRegisteredOutputs(const WorkflowManifest::IntegrationGroup& group)
{
    if (!ImageIntegration::usesCometStackingMode(group.stacking.cometStackingMode) ||
        group.stacking.normalizationMode == NormalizationMode::None)
    {
        return false;
    }

    const bool pure_comet_local_drizzle = isDrizzleEnabled(group) &&
                                          group.stacking.normalizationMode == NormalizationMode::Local &&
                                          group.stacking.localNormalization.enabled &&
                                          ImageIntegration::usesCometOnlyStackingMode(group.stacking.cometStackingMode);
    return !pure_comet_local_drizzle;
}

[[nodiscard]] bool isNonDrizzleCometStackingGroup(const WorkflowManifest::IntegrationGroup& group)
{
    return !isDrizzleEnabled(group) && ImageIntegration::usesCometStackingMode(group.stacking.cometStackingMode);
}

[[nodiscard]] bool hasNonDrizzleCometStackingGroup(const WorkflowManifest& manifest)
{
    return std::any_of(
        manifest.integrationGroups.begin(), manifest.integrationGroups.end(),
        [](const WorkflowManifest::IntegrationGroup& group) { return isNonDrizzleCometStackingGroup(group); });
}

[[nodiscard]] bool hasCometNormalizationGroupRequiringRegisteredOutputs(const WorkflowManifest& manifest)
{
    return std::any_of(manifest.integrationGroups.begin(), manifest.integrationGroups.end(),
                       [](const WorkflowManifest::IntegrationGroup& group) {
                           return cometNormalizationRequiresRegisteredOutputs(group);
                       });
}

void ensureStageBefore(QStringList& sequence, const QString& stage, const QString& before)
{
    sequence.removeAll(stage);
    const int before_index = sequence.indexOf(before);
    if (before_index >= 0)
    {
        sequence.insert(before_index, stage);
        return;
    }

    sequence.append(stage);
}

void ensureCometStackingStageDependencies(WorkflowManifest& manifest)
{
    if (manifest.stageSequence.isEmpty())
    {
        return;
    }

    const bool normalization_consumes_comet_outputs =
        manifest.stageSequence.contains(QStringLiteral("LightsNormalization")) &&
        hasCometNormalizationGroupRequiringRegisteredOutputs(manifest);
    const bool stacking_consumes_comet_outputs =
        manifest.stageSequence.contains(QStringLiteral("LightsStacking")) && hasNonDrizzleCometStackingGroup(manifest);
    if (!normalization_consumes_comet_outputs && !stacking_consumes_comet_outputs)
    {
        return;
    }

    ensureStageBefore(manifest.stageSequence, QStringLiteral("LightsAlignment"),
                      normalization_consumes_comet_outputs ? QStringLiteral("LightsNormalization")
                                                           : QStringLiteral("LightsStacking"));
}

void applyLowMemoryCalibrationFallback(WorkflowManifest& manifest,
                                       const std::optional<std::uint64_t> availablePhysicalBytes)
{
    if (!manifest.calibrateLightsInMemory || manifest.lightsCalibration.empty())
    {
        return;
    }

    try
    {
        const std::uint64_t avail = availablePhysicalBytes.has_value()
                                        ? *availablePhysicalBytes
                                        : an::platform::querySystemMemory().availablePhysicalBytes;
        constexpr uint64_t kMinAvailForInMemory = 2ull * 1024ull * 1024ull * 1024ull; // 2 GiB
        if (avail < kMinAvailForInMemory)
        {
            LOG_WARNING("WorkflowExecutionController: available RAM is low ({} MB). "
                        "Disabling in-memory calibration for this run.",
                        static_cast<unsigned long long>(avail / (1024ull * 1024ull)));
            manifest.calibrateLightsInMemory = false;
        }
    }
    catch (const std::exception&)
    {
        // If memory info isn't available, keep the selected mode.
    }
}

} // namespace

WorkflowManifest buildFullWorkflowManifest(const IntegrationDataModel& model, const FullWorkflowOptions& options)
{
    ManifestBuilder builder(model);
    builder.setReprocessExisting(options.reprocessExisting);
    builder.setIncludeBiasFrames(true);
    builder.setIncludeDarkFrames(true);
    builder.setIncludeFlatFrames(true);
    builder.setIncludeLightFrames(true);

    WorkflowManifest manifest = builder.build();
    manifest.maxParallelCores = options.maxParallelCores;
    manifest.combineRgbImages = options.combineRgbImages;
    return manifest;
}

void normalizeManifestForExecution(WorkflowManifest& manifest,
                                   const std::optional<std::uint64_t> availablePhysicalBytes)
{
    applyLowMemoryCalibrationFallback(manifest, availablePhysicalBytes);
    ensureCalibrationStageSelectionConsistency(manifest);
    ensureCometStackingStageDependencies(manifest);
}

void clearIncrementalSelections(WorkflowManifest& manifest)
{
    manifest.incrementalBiasGroups.clear();
    manifest.incrementalDarkGroups.clear();
    manifest.incrementalFlatGroups.clear();
    manifest.incrementalRegistrationGroups.clear();
    manifest.incrementalIntegrationGroups.clear();
    manifest.dirtyGroups.clear();
}

void applyIncrementalPlanToManifest(WorkflowManifest& manifest, const ExecutionPlan& plan)
{
    manifest.incrementalBiasGroups = plan.biasGroupsToProcess;
    manifest.incrementalDarkGroups = plan.darkGroupsToProcess;
    manifest.incrementalFlatGroups = plan.flatGroupsToProcess;
    manifest.incrementalRegistrationGroups = plan.registrationGroupsToProcess;
    manifest.incrementalIntegrationGroups = plan.integrationGroupsToProcess;

    manifest.dirtyGroups.clear();
    manifest.dirtyGroups.reserve(plan.integrationGroupsToProcess.size());
    for (const auto& key : plan.integrationGroupsToProcess)
    {
        manifest.dirtyGroups.push_back(key.key);
    }
}

PreparedWorkflowExecution
preparePlannedWorkflowExecution(const std::function<WorkflowManifest()>& buildManifest,
                                const std::optional<IntegrationGroupKey>& forcedIntegrationGroup,
                                const WorkflowPreparationCancelled& cancelled,
                                const WorkflowPreparationProgress& progress, const FrameHeaderReader& headerReader)
{
    report(progress, 0, "Building workflow manifest…");

    PreparedWorkflowExecution prepared;
    prepared.preparationTiming.captured = true;
    const auto user_visible_start = std::chrono::steady_clock::now();
    bool manifest_built = false;
    try
    {
        throwIfCancelled(cancelled);
        WorkflowManifest manifest = buildManifest();
        normalizeManifestForExecution(manifest);
        prepared.preparationTiming.manifestBuildMs = elapsedMilliseconds(user_visible_start);
        manifest_built = true;

        const auto plan_and_validation_start = std::chrono::steady_clock::now();
        try
        {
            report(progress, 25, "Checking reusable workflow artifacts…");
            const auto preparation_snapshot = ExecutionPreparationSnapshot::build(
                manifest, [&cancelled]() { return cancelled && cancelled(); }, headerReader);
            throwIfCancelled(cancelled);

            ExecutionPlanner planner;
            ExecutionPlan plan = planner.buildPlan(manifest, *preparation_snapshot);
            report(progress, 60, "Applying the execution plan…");

            if (plan.mode == ExecutionMode::Incremental && forcedIntegrationGroup.has_value())
            {
                plan.ensureIntegrationGroupsToProcess({*forcedIntegrationGroup});
            }

            if (plan.mode == ExecutionMode::Incremental)
            {
                manifest.executionMode = ExecutionMode::Incremental;
                applyIncrementalPlanToManifest(manifest, plan);
            }
            else
            {
                manifest.executionMode = ExecutionMode::Full;
                clearIncrementalSelections(manifest);
            }

            report(progress, 75, "Validating the prepared workflow…");

            prepared.validation = manifest.validate(headerReader);
            report(progress, 90, "Building execution plan details…");
            throwIfCancelled(cancelled);
            prepared.decisions = buildExecutionPlanDecisions(manifest, plan, *preparation_snapshot);
            throwIfCancelled(cancelled);
            prepared.preparationSnapshot = preparation_snapshot;
            prepared.manifest = std::move(manifest);
            prepared.plan = std::move(plan);
            prepared.preparationTiming.planAndValidationMs = elapsedMilliseconds(plan_and_validation_start);
        }
        catch (...)
        {
            prepared.preparationTiming.planAndValidationMs = elapsedMilliseconds(plan_and_validation_start);
            throw;
        }
        prepared.success = true;
    }
    catch (const std::exception& e)
    {
        if (!manifest_built)
        {
            prepared.preparationTiming.manifestBuildMs = elapsedMilliseconds(user_visible_start);
        }
        prepared.errorMessage = QString::fromUtf8(e.what());
    }
    catch (...)
    {
        if (!manifest_built)
        {
            prepared.preparationTiming.manifestBuildMs = elapsedMilliseconds(user_visible_start);
        }
        prepared.errorMessage = trImageIntegrationProcess("Workflow preparation failed.");
    }

    report(progress, 100, "Execution plan ready.");
    return prepared;
}

PreparedWorkflowExecution prepareDirectWorkflowExecution(const std::function<WorkflowManifest()>& buildManifest,
                                                         const WorkflowPreparationCancelled& cancelled,
                                                         const FrameHeaderReader& headerReader)
{
    PreparedWorkflowExecution prepared;
    prepared.preparationTiming.captured = true;
    const auto user_visible_start = std::chrono::steady_clock::now();
    bool manifest_built = false;
    try
    {
        prepared.manifest = buildManifest();
        prepared.preparationTiming.manifestBuildMs = elapsedMilliseconds(user_visible_start);
        manifest_built = true;
        throwIfCancelled(cancelled);

        const auto plan_and_validation_start = std::chrono::steady_clock::now();
        try
        {
            normalizeManifestForExecution(prepared.manifest);
            prepared.preparationSnapshot = ExecutionPreparationSnapshot::build(
                prepared.manifest, [&cancelled]() { return cancelled && cancelled(); }, headerReader);
            prepared.validation = prepared.manifest.validate(headerReader);
            prepared.preparationTiming.planAndValidationMs = elapsedMilliseconds(plan_and_validation_start);
        }
        catch (...)
        {
            prepared.preparationTiming.planAndValidationMs = elapsedMilliseconds(plan_and_validation_start);
            throw;
        }
        prepared.success = true;
    }
    catch (const std::exception& e)
    {
        if (!manifest_built)
        {
            prepared.preparationTiming.manifestBuildMs = elapsedMilliseconds(user_visible_start);
        }
        prepared.errorMessage = QString::fromUtf8(e.what());
    }
    catch (...)
    {
        if (!manifest_built)
        {
            prepared.preparationTiming.manifestBuildMs = elapsedMilliseconds(user_visible_start);
        }
        prepared.errorMessage = trImageIntegrationProcess("Workflow preparation failed.");
    }
    return prepared;
}

PreparedWorkflowDisposition plannedDisposition(const PreparedWorkflowExecution& prepared)
{
    if (!prepared.success)
    {
        return PreparedWorkflowDisposition::Failed;
    }
    if (prepared.plan.mode == ExecutionMode::Incremental && prepared.plan.isEmpty())
    {
        return PreparedWorkflowDisposition::NothingToProcess;
    }
    if (prepared.validation.hasErrors())
    {
        return PreparedWorkflowDisposition::Invalid;
    }
    const WorkflowManifest& manifest = prepared.manifest;
    const bool is_empty = manifest.integrationGroups.empty() && manifest.registrationGroups.empty() &&
                          (!manifest.biasStage.has_value() || manifest.biasStage->groups.empty()) &&
                          (!manifest.darkStage.has_value() || manifest.darkStage->groups.empty()) &&
                          (!manifest.flatStage.has_value() || manifest.flatStage->groups.empty());
    return is_empty ? PreparedWorkflowDisposition::NoGroups : PreparedWorkflowDisposition::Ready;
}

PreparedWorkflowDisposition directDisposition(const PreparedWorkflowExecution& prepared)
{
    if (!prepared.success)
    {
        return PreparedWorkflowDisposition::Failed;
    }
    return prepared.validation.hasErrors() ? PreparedWorkflowDisposition::Invalid : PreparedWorkflowDisposition::Ready;
}

} // namespace ImageIntegration
