// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Each frame of a comet registration placed in time: the midpoint of its exposure, from the start its header or the
/// manifest states and the length of the exposure, and the frozen reference frame whose midpoint fixes where the comet
/// sits on the comet-aligned plane.
///
/// A midpoint is kept twice: as the engine's `UtcInstant`, which a track is fitted and evaluated at, and as seconds
/// since the Unix epoch, which the registration database's comet records hold.

#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/core/WorkflowSettingsRecords.h"

#include <AfterNightEngine/Algorithms/Registration/CometTracking.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>

#include <QString>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace an::ui
{
enum class CometTrackingFrameContextStatus
{
    Ready,
    Failed
};

struct CometTrackingFrameContextInput
{
    FrameId originalFrameId;
    QString refinementSourcePath;

    /// The exposure's start as the frame's header or the manifest spells it.
    QString dateObs;
    double exposureSeconds = 0.0;
    bool hasSipDistortion = false;
    QString calibrationFingerprint;
    QString chromaticCorrectionFingerprint;
};

struct CometTrackingFrameContext
{
    FrameId originalFrameId;
    QString refinementSourcePath;

    /// The exposure's midpoint in seconds since the Unix epoch, or not-a-number when it could not be placed.
    double midpointEpoch = std::numeric_limits<double>::quiet_NaN();

    /// The same midpoint, as the engine fits a track at it.
    std::optional<an::UtcInstant> midpoint;
    bool isReferenceFrame = false;
    bool hasSipDistortion = false;
    CometTrackingFrameContextStatus status = CometTrackingFrameContextStatus::Failed;
    QString outputPlaneId;
    QString calibrationFingerprint;
    QString chromaticCorrectionFingerprint;
    QString message;
};

struct CometTrackingFrameContextRequest
{
    RegistrationSettings::CometTrackingSettings cometSettings;
    FrameId resolvedReferenceFrame;
    std::vector<CometTrackingFrameContextInput> frames;
};

struct CometTrackingFrameContextResult
{
    bool succeeded = false;
    FrameId frozenReferenceFrame;
    std::vector<CometTrackingFrameContext> contexts;
    QString message;
};

/// An instant as seconds since the Unix epoch, the unit the comet records hold.
[[nodiscard]] inline double cometEpochSeconds(const an::UtcInstant instant) noexcept
{
    return static_cast<double>(instant.nanosecondsSinceEpoch) / 1.0e9;
}

/// Seconds since the Unix epoch as an instant, rounded to the nearest nanosecond; nothing for a value that is not
/// finite or does not fit.
[[nodiscard]] inline std::optional<an::UtcInstant> cometInstantOfEpochSeconds(const double seconds) noexcept
{
    const double nanoseconds = std::round(seconds * 1.0e9);
    if (!std::isfinite(nanoseconds) || std::abs(nanoseconds) >= 9.2e18)
    {
        return std::nullopt;
    }
    return an::UtcInstant{.nanosecondsSinceEpoch = static_cast<std::int64_t>(nanoseconds)};
}

[[nodiscard]] inline QString cometTrackingOutputPlaneId(const FrameId& frozen_reference_frame)
{
    return QStringLiteral("pure_comet:%1").arg(frozen_reference_frame.canonicalPath());
}

[[nodiscard]] inline CometTrackingFrameContextResult
buildCometTrackingFrameContexts(const CometTrackingFrameContextRequest& request)
{
    CometTrackingFrameContextResult result;
    if (!request.resolvedReferenceFrame.isValid())
    {
        result.message = QStringLiteral("Resolve and freeze a reference frame before comet tracking.");
        return result;
    }
    if (request.frames.empty())
    {
        result.message = QStringLiteral("No frames are available for comet tracking.");
        return result;
    }

    result.succeeded = true;
    result.frozenReferenceFrame = request.resolvedReferenceFrame;
    result.contexts.reserve(request.frames.size());

    const QString output_plane_id = cometTrackingOutputPlaneId(result.frozenReferenceFrame);

    for (const CometTrackingFrameContextInput& frame : request.frames)
    {
        CometTrackingFrameContext context;
        context.originalFrameId = frame.originalFrameId;
        context.refinementSourcePath =
            frame.refinementSourcePath.isEmpty() ? frame.originalFrameId.canonicalPath() : frame.refinementSourcePath;
        context.isReferenceFrame = frame.originalFrameId == result.frozenReferenceFrame;
        context.hasSipDistortion = frame.hasSipDistortion;
        context.outputPlaneId = output_plane_id;
        context.calibrationFingerprint = frame.calibrationFingerprint;
        context.chromaticCorrectionFingerprint = frame.chromaticCorrectionFingerprint;

        if (!frame.originalFrameId.isValid())
        {
            context.message = QStringLiteral("Frame identity is required for comet tracking.");
            result.contexts.push_back(std::move(context));
            continue;
        }

        const std::string date = frame.dateObs.trimmed().toStdString();
        const an::Result<an::UtcInstant> start = an::parseDateCard(date);
        if (date.empty() || !start.hasValue())
        {
            context.message = QStringLiteral("The exposure start '%1' is not a date and time.").arg(frame.dateObs);
            result.contexts.push_back(std::move(context));
            continue;
        }
        const an::Result<an::UtcInstant> midpoint = an::computeCometObservationMidpoint(*start, frame.exposureSeconds);
        if (!midpoint.hasValue())
        {
            context.message = QString::fromUtf8(midpoint.status().message());
            result.contexts.push_back(std::move(context));
            continue;
        }

        context.midpoint = *midpoint;
        context.midpointEpoch = cometEpochSeconds(*midpoint);
        context.status = CometTrackingFrameContextStatus::Ready;
        result.contexts.push_back(std::move(context));
    }

    return result;
}
} // namespace an::ui
