// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Apply Stretch: the autostretch the view shows, made the image's own; and which images a picture of shows stretched.
///
/// The view presents its image through the engine's display presentation into display bytes; Apply Stretch runs the
/// same presentation, through the production plan and with the same request, into a Float32 working image, and brings
/// that back to the image's own representation at full scale. It is an edit of the view: computed on a worker under the
/// document's writer, recorded in the history before the view shows it, and shown only while the request is current.
///
/// A picture of an image that is not a view - a recent file's thumbnail, a history step's preview - shows linear data
/// through the same stretch, each colour channel stretched on its own, and anything else as it is.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>
#include <AfterNightEngine/Core/Result.h>

#include <stop_token>

namespace an
{
class EngineContext;
class ExecutionContext;
class ImageMetadata;
class ValidityMask;
} // namespace an

namespace an::ui
{

/// What Apply Stretch makes permanent: the autostretch the view shows, with its neutralization, calibration and
/// adjustments.
struct AutostretchApplySettings
{
    double targetBackground = an::kDisplayTargetBackground;
    double shadowsClipping = an::kDisplayShadowsClipping;
    bool linkedChannels = false;
    bool neutralizeBackground = false;
    bool colorCalibration = false;
    double saturation = 0.0;
    double saturationBackground = 1.0;
    int brightness = 0;
    int contrast = 0;

    /// The image is a sequence frame, which selects the saturation path the view uses for sequences.
    bool sequenceFrame = false;
};

/// The presentation request Apply Stretch runs: the view's autostretch request with the same settings, into a Float32
/// working image.
///
/// @param settings the stretch.
/// @return the request.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::DisplayPresentationRequest
autostretchApplyRequest(const AutostretchApplySettings& settings) noexcept;

/// The stretch Apply Stretch makes, under an execution the caller holds, into a Float32 working image of fractions of
/// full scale: what each frame of a sequence export is stretched through. The image is not changed.
///
/// @param engine the engine context the execution runs in, which owns the stretched image.
/// @param execution the caller's admitted execution.
/// @param image the image.
/// @param validity the image's explicit validity mask, or null.
/// @param settings the stretch.
/// @return the stretched Float32 image, or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::AnyImage>
autostretchFrame(const an::EngineContext& engine, const an::ExecutionContext& execution,
                 const an::consumer::AnyImage& image, const an::ValidityMask* validity,
                 const AutostretchApplySettings& settings);

/// A stretched copy of an image, computed as the view computes its autostretch display, in the image's own
/// representation; the image is not changed.
///
/// @param session the process's session.
/// @param image the image, read for the call - under the writer that will replace it, when the view edits.
/// @param validity the image's explicit validity mask, or null.
/// @param settings the stretch.
/// @param stop the edit's stop.
/// @return the stretched copy as a new asset; `ErrorCode::Cancelled` when stopped; or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset>
applyAutostretchToCopy(const an::consumer::EngineSession& session, const an::consumer::AnyImage& image,
                       const an::ValidityMask* validity, const AutostretchApplySettings& settings,
                       std::stop_token stop);

/// Whether an image is linear data, which a picture of it shows stretched: its processing-state cards record it linear,
/// or it is a camera RAW frame, whose codes are linear.
///
/// @param metadata the image's metadata.
/// @return true for linear data.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool isLinearData(const an::ImageMetadata& metadata);

} // namespace an::ui
