// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How a registered group's lights reach the stack.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/stages/TransformOnReadRouting.h"

#include "processes/ImageIntegration/core/CometStackingMode.h"

#include <QFileInfo>

#include <algorithm>
#include <utility>

namespace ImageIntegration
{

namespace
{

[[nodiscard]] TransformOnReadRouting materialized(const char* reason, QString detail)
{
    return TransformOnReadRouting{.route = RegisteredInputRoute::MaterializedRegisteredImages,
                                  .reason = QString::fromLatin1(reason),
                                  .detail = std::move(detail)};
}

[[nodiscard]] bool registrationGroupExists(const WorkflowManifest& manifest,
                                           const WorkflowManifest::IntegrationGroup& group)
{
    return std::any_of(manifest.registrationGroups.begin(), manifest.registrationGroups.end(),
                       [&group](const WorkflowManifest::RegistrationGroup& registration) {
                           return registration.groupKey == group.registrationGroupKey;
                       });
}

} // namespace

TransformOnReadRouting decideLightsTransformOnRead(const WorkflowManifest& manifest,
                                                   const WorkflowManifest::IntegrationGroup& group)
{
    if (group.registrationGroupKey.toString().trimmed().isEmpty() || !registrationGroupExists(manifest, group))
    {
        return materialized("registration_data_missing", QStringLiteral("The group has no registration."));
    }
    if (group.drizzle.has_value() && group.drizzle->enabled)
    {
        return materialized("drizzle", QStringLiteral("A drizzled stack places its lights itself."));
    }
    if (usesCometStackingMode(group.stacking.cometStackingMode))
    {
        return materialized("comet_product", QStringLiteral("A comet stack reads the comet-aligned images."));
    }
    if (manifest.reprocessExisting || manifest.executionMode != ExecutionMode::Full)
    {
        return materialized("resume_checkpoint_recovery",
                            QStringLiteral("An incremental or reprocessing run keeps the registered images it may "
                                           "resume from."));
    }
    if (group.stacking.registeredOutputPolicy != RegisteredOutputPolicy::Discardable)
    {
        return materialized("registered_output_retention", QStringLiteral("The group keeps its registered images."));
    }
    return TransformOnReadRouting{
        .route = RegisteredInputRoute::TransformOnRead,
        .reason = QStringLiteral("registration_validation_deferred"),
        .detail = QStringLiteral("The settings allow the lights to be warped as the stack reads them; the "
                                 "registration is checked when it is known.")};
}

TransformOnReadRouting confirmLightsTransformOnReadFor(const WorkflowManifest& manifest,
                                                       const WorkflowManifest::IntegrationGroup& group,
                                                       const std::vector<RegisteredLightRecord>& lights,
                                                       const QString& referenceInputPath)
{
    const TransformOnReadRouting planned = decideLightsTransformOnRead(manifest, group);
    if (!planned.usesTransformOnRead())
    {
        return planned;
    }
    if (lights.size() < 2u)
    {
        return materialized("registration_identity_invalid", QStringLiteral("Fewer than two lights are registered."));
    }
    bool referenceFound = false;
    for (const RegisteredLightRecord& light : lights)
    {
        if (light.transformJson.trimmed().isEmpty())
        {
            return materialized("registration_identity_invalid",
                                QStringLiteral("%1 has no registration transform.").arg(light.frame.canonicalPath()));
        }
        if (light.inputPath.trimmed().isEmpty() || !QFileInfo::exists(light.inputPath))
        {
            return materialized(
                "registration_identity_invalid",
                QStringLiteral("The file %1 is read from is missing.").arg(light.frame.canonicalPath()));
        }
        if (QFileInfo(light.inputPath) == QFileInfo(referenceInputPath))
        {
            referenceFound = true;
        }
    }
    if (!referenceFound)
    {
        return materialized("registration_identity_invalid",
                            QStringLiteral("The registration's reference is not among the group's lights."));
    }
    return TransformOnReadRouting{.route = RegisteredInputRoute::TransformOnRead,
                                  .reason = QStringLiteral("eligible"),
                                  .detail = QStringLiteral("Every light is registered and is warped as it is read.")};
}

} // namespace ImageIntegration
