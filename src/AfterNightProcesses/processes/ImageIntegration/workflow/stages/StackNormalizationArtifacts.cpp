// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A group's normalization coefficients as the stack consumes them.

#include "stdafx.hpp"
#include "StackNormalizationArtifacts.h"

#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>
#include <unordered_map>

namespace ImageIntegration
{

namespace
{

[[nodiscard]] QString identityOf(const QString& frame)
{
    try
    {
        return FrameId(frame).identityKey();
    }
    catch (...)
    {
        return {};
    }
}

/// Whether coefficients measured on one drizzle canvas apply on another: the analysis plane's origin and extent, the
/// scale left out, as `DrizzleAnalysisPlaneRecord` states.
[[nodiscard]] bool sameAnalysisPlane(const std::optional<DrizzleAnalysisPlaneRecord>& measured,
                                     const std::optional<DrizzleAnalysisPlaneRecord>& read) noexcept
{
    if (!measured.has_value() || !read.has_value())
    {
        return measured.has_value() == read.has_value();
    }
    return measured->originX == read->originX && measured->originY == read->originY && measured->width == read->width &&
           measured->height == read->height;
}

/// Restates every post-local match of a document against one frame's, whose own becomes the identity: each match is
/// followed by the inverse of that frame's. Every method writes `sample * scale + offset`, so a restated match keeps
/// its method - a multiplicative one's offset stays zero, an additive one's scale one.
void restatePostLocalMatches(an::LocalNormalizationDocument& document, const std::size_t reference)
{
    if (!document.frames[reference].global.has_value())
    {
        return;
    }
    const an::FrameNormalization by = *document.frames[reference].global;
    for (std::size_t frame = 0; frame < document.frames.size(); ++frame)
    {
        std::optional<an::FrameNormalization>& match = document.frames[frame].global;
        if (!match.has_value())
        {
            continue;
        }
        for (std::uint32_t channel = 0; channel < match->channelCount; ++channel)
        {
            an::NormalizationChannelCoefficients& coefficients = match->channels[channel];
            if (frame == reference)
            {
                coefficients = an::NormalizationChannelCoefficients{};
                continue;
            }
            const an::NormalizationChannelCoefficients& inverse = by.channels[channel];
            coefficients.offset = (coefficients.offset - inverse.offset) / inverse.scale;
            coefficients.scale /= inverse.scale;
        }
    }
}

[[nodiscard]] NormalizationDocumentRead unreadable(const QString& message)
{
    NormalizationDocumentRead result;
    result.failure = NormalizationDocumentFailure::Unreadable;
    result.message = message;
    return result;
}

} // namespace

NormalizationDocumentRead readStackNormalizationCoefficients(const QString& artifactPath,
                                                             const WorkflowManifest::IntegrationGroup& group,
                                                             const std::vector<FrameId>& referenceFrames)
{
    QFile file(normalizationSidecarPath(artifactPath));
    if (!file.exists())
    {
        return unreadable(QStringLiteral("the sidecar beside them is missing"));
    }
    QByteArray bytes;
    QString error;
    if (!schema::readMetadataSidecarBounded(file, bytes, &error))
    {
        return unreadable(QStringLiteral("the sidecar beside them cannot be read: %1").arg(error));
    }
    QJsonParseError parseError{};
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
    {
        return unreadable(QStringLiteral("the sidecar beside them is not readable"));
    }
    const QJsonObject sidecar = json.object();
    if (sidecar.value(QStringLiteral("schemaVersion")).toInt() != schema::kNormalizationArtifactSchemaVersion ||
        !schema::statesCurrentArtifactFamily(sidecar, schema::ArtifactFamily::NormalizationCoefficients))
    {
        return unreadable(QStringLiteral("the sidecar beside them was written by another version"));
    }
    if (sidecar.value(QStringLiteral("integrationGroupKey")).toString() != group.groupKey.toString())
    {
        return unreadable(QStringLiteral("the sidecar beside them belongs to another group"));
    }
    // The comet products the coefficients were measured from are inputs the sidecar states; every setting is the
    // group's as the stack runs it.
    const QString settingsFingerprint = computeNormalizationSettingsFingerprint(
        group, referenceFrames, sidecar.value(QStringLiteral("cometProductsFingerprint")).toString());
    if (sidecar.value(QStringLiteral("settingsFingerprint")).toString() != settingsFingerprint)
    {
        NormalizationDocumentRead result;
        result.failure = NormalizationDocumentFailure::OtherSettings;
        result.message = QStringLiteral("they were measured under settings other than the group's");
        return result;
    }
    return readNormalizationCoefficients(
        artifactPath, normalizationArtifactIdentity(sidecar.value(QStringLiteral("inputsFingerprint")).toString(),
                                                    settingsFingerprint));
}

std::optional<StackNormalizationInputs> stackNormalizationInputsFor(const NormalizationCoefficientSet& set,
                                                                    const StackNormalizationExpectation& expectation,
                                                                    QString& reason)
{
    if (const QString refused = checkNormalizationCoefficientSet(set); !refused.isEmpty())
    {
        reason = QStringLiteral("The coefficients are refused: %1.").arg(refused);
        return std::nullopt;
    }
    if (set.plane != expectation.plane)
    {
        reason = QStringLiteral("The coefficients were measured on the %1 and the stack reads the %2.")
                     .arg(QLatin1String(normalizationMeasurementPlaneKey(set.plane)),
                          QLatin1String(normalizationMeasurementPlaneKey(expectation.plane)));
        return std::nullopt;
    }
    if (set.channelCount != expectation.channelCount || set.width != expectation.width ||
        set.height != expectation.height)
    {
        reason =
            QStringLiteral("The coefficients describe %1x%2 frames of %3 channel(s) and the stack reads %4x%5 of %6.")
                .arg(set.width)
                .arg(set.height)
                .arg(set.channelCount)
                .arg(expectation.width)
                .arg(expectation.height)
                .arg(expectation.channelCount);
        return std::nullopt;
    }
    if (!sameAnalysisPlane(set.drizzlePlane, expectation.drizzlePlane))
    {
        reason = QStringLiteral("The coefficients were measured on another drizzle canvas.");
        return std::nullopt;
    }
    if (expectation.logicalFrames.empty())
    {
        reason = QStringLiteral("The stack reads no frame.");
        return std::nullopt;
    }
    if (expectation.referenceFrame >= expectation.logicalFrames.size())
    {
        reason = QStringLiteral("The stack's reference is not one of the frames it reads.");
        return std::nullopt;
    }

    std::unordered_map<QString, std::size_t> indexByIdentity;
    for (std::size_t index = 0; index < set.logicalFrames.size(); ++index)
    {
        indexByIdentity.emplace(identityOf(set.logicalFrames[index]), index);
    }
    std::vector<std::size_t> selected;
    selected.reserve(expectation.logicalFrames.size());
    for (const QString& frame : expectation.logicalFrames)
    {
        const auto found = indexByIdentity.find(identityOf(frame));
        if (found == indexByIdentity.end())
        {
            reason = QStringLiteral("The coefficients were not measured for %1.").arg(frame);
            return std::nullopt;
        }
        selected.push_back(found->second);
    }

    StackNormalizationInputs inputs;
    inputs.referenceFrame = expectation.referenceFrame;
    if (set.mode == NormalizationCoefficientMode::Global)
    {
        inputs.global.reserve(selected.size());
        for (const std::size_t index : selected)
        {
            inputs.global.push_back(set.global[index]);
        }
        return inputs;
    }

    an::LocalNormalizationDocument local;
    local.request = set.local->request;
    local.shape = set.local->shape;
    local.validity = set.local->validity;
    local.referenceBackground = set.local->referenceBackground;
    local.frames.reserve(selected.size());
    for (std::size_t position = 0; position < selected.size(); ++position)
    {
        an::LocalNormalizationFrameCoefficients record = set.local->frames[selected[position]];
        record.frameIndex = static_cast<std::uint32_t>(position);
        if (set.local->referenceIndex.has_value() && selected[position] == *set.local->referenceIndex)
        {
            local.referenceIndex = static_cast<std::uint32_t>(position);
        }
        local.frames.push_back(std::move(record));
    }
    if (local.referenceIndex.has_value())
    {
        // The engine applies the document over the light it names as the stack's own reference.
        inputs.referenceFrame = *local.referenceIndex;
    }
    else
    {
        // No light the stack reads is the document's reference, and the engine matches the stack's reference by the
        // identity after its surface: every match is restated against that light's.
        restatePostLocalMatches(local, inputs.referenceFrame);
    }
    inputs.local = std::move(local);
    return inputs;
}

} // namespace ImageIntegration
