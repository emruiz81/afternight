// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the photometric and spectrophotometric colour calibrations share: the facts a window reads off its target, the
/// working image the engine calibrates, the way back to the image's own representation, and the record a calibration
/// leaves in an image's metadata.
///
/// **The engine calibrates a floating frame described as linear RGB.** Both calibrations take three-channel samples in
/// Float32 or Float64 whose numeric domain is linear and whose colour basis is `ColorSpace::LinearRgb`. A decoded file
/// rarely states a basis - a FITS colour image is `ColorSpace::Unspecified` - so the working image restates it: the
/// window has already decided, from what the image declares, that its samples may be calibrated. Its samples are the
/// image's as fractions of full scale - an unsigned integer a fraction of its largest value, a signed one counted from
/// its smallest - and they go back to the image's representation to the nearest value, saturated.
///
/// **A calibration's record is the engine's transition and the colour-calibration cards.** The engine states what the
/// result means - colour calibrated, with a lineage entry naming the recipe, the identities and the numbers - and the
/// product writes the colour-calibration cards beside it (`ANCCAL`, the gains and offsets, the identities and the
/// recipe's version), so a saved file records the calibration and the spectrophotometric readiness can refuse an image
/// that is already calibrated.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ProcessExecution.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <QRect>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>

namespace an
{
class EngineContext;
class ExecutionContext;
class StarFrameView;
struct StatisticsRegion;
} // namespace an

namespace an::ui::processes
{

/// Why a target may not be colour calibrated, as the windows refuse it.
enum class CalibrationTargetProblem : std::uint8_t
{
    /// The target may be calibrated.
    None,

    /// No image is shown.
    NoImage,

    /// The image is not three-channel RGB.
    NotRgb,

    /// The image's filter names a narrowband filter, which a broadband colour model does not describe.
    Narrowband,

    /// The image states no world-coordinate solution, so its stars cannot be found in a catalog.
    NotPlateSolved,

    /// The image declares that its samples have been through a nonlinear encoding, which has no photometry to fit.
    DeclaredNonlinear
};

/// Whether an image is described as three-channel RGB, which both calibrations read.
///
/// @param descriptor what the image is.
/// @return true for three channels in the RGB schema.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool isCalibrationRgb(const an::ImageDescriptor& descriptor) noexcept;

/// The filter an image's acquisition record names, trimmed; empty when it names none.
///
/// @param metadata the image's metadata.
/// @return the filter name.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString calibrationFilterName(const an::ImageMetadata& metadata);

/// The camera an image's acquisition record names, trimmed; empty when it names none. The codecs project the
/// instrument card or the Exif model into the instrument name.
///
/// @param metadata the image's metadata.
/// @return the camera model.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString calibrationCameraModel(const an::ImageMetadata& metadata);

/// The world-coordinate solution an image's cards state, read as the viewer reads it; nothing when they state none.
///
/// @param metadata the image's metadata.
/// @param descriptor what the image is, for its size.
/// @return the solution, or nothing.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::WcsSolution>
calibrationSolutionOf(const an::ImageMetadata& metadata, const an::ImageDescriptor& descriptor);

/// The first reason the photometric colour calibration refuses a target: it reads three-channel RGB that is not
/// declared nonlinear, whose filter is not narrowband and whose cards state a solution.
///
/// @param descriptor what the image is, or nothing when no image is shown.
/// @param metadata the image's metadata.
/// @return the problem, or `CalibrationTargetProblem::None`.
[[nodiscard]] AFTERNIGHTPROCESSES_API CalibrationTargetProblem
photometricTargetProblem(const std::optional<an::ImageDescriptor>& descriptor, const an::ImageMetadata& metadata);

/// The processing state an image declares for a spectrophotometric calibration: the cards it carries, with what its
/// meaning and description state filled in where the cards say nothing - a linear or nonlinear transfer function, a
/// `ColorSpace::LinearRgb` basis, and a meaning that says it was colour calibrated.
///
/// @param descriptor what the image is.
/// @param metadata the image's metadata.
/// @return the state the spectrophotometric checks read.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::ProcessingStateRecord
declaredCalibrationState(const an::ImageDescriptor& descriptor, const an::ImageMetadata& metadata);

/// The working image a calibration measures and writes: Float64 for a Float64 or 32-bit integer image and Float32 for
/// any other, laid
/// out as the image is, three channels in the `ColorSpace::LinearRgb` basis and the `NumericDomain::NormalizedLinear`
/// domain, its samples the image's as fractions of full scale. It carries the image's metadata.
///
/// @param engine the engine context the copy belongs to.
/// @param execution the run's admitted execution.
/// @param image the image; three-channel RGB.
/// @return the working image; `ErrorCode::NotInitialized` for an image that holds nothing;
///         `ErrorCode::Unsupported` for one that is not three-channel RGB; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::AnyImage>
makeCalibrationWorkingImage(const an::EngineContext& engine, const an::ExecutionContext& execution,
                            const an::consumer::AnyImage& image);

/// What a calibration does to a working image: reads the checked frame and writes the calibrated samples to the
/// destination, which is the frame's own samples.
using CalibrationBody =
    std::function<an::Status(const an::StarFrameView& frame, void* destination, std::uint64_t destinationBytes)>;

/// Runs a calibration over a working image in place: builds the frame view the engine checks, under the validity the
/// image's meaning states and its mask, and hands it and the samples to `body`.
///
/// @param working the working image, from `makeCalibrationWorkingImage`.
/// @param mask the image's packed validity mask, or null.
/// @param body the calibration.
/// @return what `body` returned; or what building the frame view refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status
calibrateWorkingImage(an::consumer::AnyImage& working, const an::ValidityMask* mask, const CalibrationBody& body);

/// The statistics region of a rectangle a window states, or nothing for an empty one.
///
/// @param rectangle the rectangle, in the image's pixels.
/// @return the region.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::StatisticsRegion> calibrationRegion(const QRect& rectangle);

/// What a calibrated image's metadata is: the source's, with the engine's transition applied - the meaning colour
/// calibrated and the lineage entry - and the colour calibration written as its cards, with the processing state that
/// records it. The record's input linearity and channel basis are the source's recorded state where the record states
/// none: the card bridge reads them from the cards.
///
/// @param source the source's metadata.
/// @param change the transition the calibration's result states.
/// @param record what the calibration was, for the cards.
/// @return the metadata; or what the transition or the card bridge refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ImageMetadata>
calibratedMetadata(const an::ImageMetadata& source, const an::MetadataTransition& change,
                   const an::ColorCalibrationRecord& record);

/// The calibrated image in the source's representation: the working samples brought back at full scale, to the
/// nearest value and saturated, with the source's description and validity and the metadata given, as a new asset of
/// the session.
///
/// @param session the session the result belongs to.
/// @param engine the engine context of the run's execution.
/// @param execution the run's admitted execution.
/// @param source what the run read.
/// @param working the calibrated working image, of the source's size.
/// @param metadata the result's metadata.
/// @return the asset; `ErrorCode::IncompatibleArguments` for a working image of another size; or what the conversion
///         or the adoption refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
calibratedAsset(const an::consumer::EngineSession& session, const an::EngineContext& engine,
                const an::ExecutionContext& execution, const ProcessSource& source,
                const an::consumer::AnyImage& working, an::ImageMetadata metadata);

/// The evidence a person's attestation of an image's state names: the asset the person looked at and the revision of
/// it they attested.
///
/// @param revision the revision the person attested.
/// @return the evidence, as the attestation card carries it.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString attestedRevisionText(const an::consumer::AssetRevision& revision);

/// An image's metadata after a person attests its samples are linear RGB: the processing state recorded as their
/// attestation - the file or view they looked at, the revision of it, who and when - and the meaning's transfer
/// function stated linear, the change recorded.
///
/// @param metadata what the image has.
/// @param sourcePath the file the image came from, or `view:` and the view's name for one that came from none.
/// @param revision the revision of the asset the person attested.
/// @param timestampUtc when, as ISO 8601 text.
/// @return the metadata; or what the card bridge, the semantics or the lineage refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ImageMetadata>
attestedLinearMetadata(const an::ImageMetadata& metadata, const QString& sourcePath,
                       const an::consumer::AssetRevision& revision, const QString& timestampUtc);

} // namespace an::ui::processes
