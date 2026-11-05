// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A light probed and read as the integration's registration and alignment stages read it.

#include "stdafx.hpp"
#include "RegistrationFrames.h"

#include "app/DemosaicTextMaps.h"
#include "app/ObservationEpochUtils.h"
#include "app/PlateSolveTypes.h"
#include "controls/ImageViewFiles.h"
#include "processes/ImageIntegration/core/CalibrationMasterSet.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <string>
#include <utility>
#include <variant>

namespace stage_utils
{

namespace
{

/// What warping a frame onto a plane does to it: its description restated onto the plane, the cards and solution the
/// destination was built with kept, an output decision withdrawn, and its samples where the frame does not reach left
/// at zero under the source's own validity rule - the convention every stage after the alignment reads a registered
/// light by, since a published light carries no mask beside it.
constexpr an::OperationEffect kAlignmentEffect{.description = an::DescriptionEffect::Restates,
                                               .solution = an::SolutionEffect::Preserves,
                                               .codecMetadata = an::CodecMetadataEffect::Preserves,
                                               .outputIntent = an::OutputIntentEffect::Withdraws,
                                               .validity = {.policy = an::ValidityPolicy::Preserve}};

/// A failure that names the file it was about, keeping the engine's failure as its cause.
[[nodiscard]] an::Status aboutFile(an::Status cause, const QString& path, const char* what)
{
    if (cause.code() == an::ErrorCode::Cancelled || cause.code() == an::ErrorCode::DeadlineExceeded)
    {
        return cause;
    }
    const an::ErrorCode code = cause.code();
    return an::Status::failure(
        code, an::StatusDetail{.message = std::string(what) + " '" + path.toUtf8().toStdString() + "'"},
        std::move(cause));
}

} // namespace

an::Result<RegistrationFrameFacts> probeRegistrationFrame(const an::consumer::OperationScope& scope,
                                                          const an::CodecRegistry* registry, const QString& path)
{
    const std::string nativePath = path.toUtf8().toStdString();
    const an::CodecReadRequest request{.path = nativePath, .validatePayload = false};
    const an::Result<an::CodecImageInfo> probed =
        registry != nullptr ? an::probeImage(scope.engine(), scope.execution(), *registry, request)
                            : an::probeImage(scope.engine(), scope.execution(), request);
    if (!probed.hasValue())
    {
        return aboutFile(probed.status(), path, "the light could not be probed:");
    }

    RegistrationFrameFacts facts;
    facts.width = probed->descriptor.extent().width();
    facts.height = probed->descriptor.extent().height();
    facts.channels = probed->descriptor.extent().channelCount();
    facts.mosaic = probed->descriptor.isMosaiced();

    const an::AcquisitionMetadata& acquisition = probed->metadata.acquisition();
    facts.filterName = QString::fromStdString(acquisition.filterName()).trimmed();
    if (const std::optional<an::UtcInstant> start = acquisition.exposureStart(); start.has_value())
    {
        facts.dateObs = QString::fromStdString(an::formatDateCard(*start, an::CodecNamespace::Fits));
    }
    facts.exposureSeconds = acquisition.exposureSeconds();
    facts.observationEpochYear = an::ui::resolveObservationEpochYear(acquisition);

    const auto& cards = probed->metadata.codec().cards();
    for (const an::CodecCard& card : cards)
    {
        if (an::isWcsKeyword(card.keyword()))
        {
            facts.wcsCards.push_back(card);
        }
    }
    if (facts.width > 0u && facts.height > 0u)
    {
        an::Result<an::WcsSolution> solution = an::ui::readImageWcsSolution(cards, facts.width, facts.height);
        if (solution.hasValue())
        {
            facts.distortion = an::ui::processes::registrationDistortionOf(*solution);
            facts.solution = std::move(*solution);
        }
    }
    return facts;
}

an::Result<an::Image<float>> readRegistrationImage(const an::consumer::OperationScope& scope,
                                                   const an::CodecRegistry* registry, const QString& path,
                                                   const core::LightCalibration* calibration,
                                                   const an::ui::processes::RegistrationDemosaic& demosaic)
{
    an::Result<an::consumer::AnyImage> read = core::readWorkflowImage(scope, registry, path);
    if (!read.hasValue())
    {
        return aboutFile(read.status(), path, "the light could not be read:");
    }
    an::Result<an::Image<float>> frame = core::workingFractions(scope, *read);
    if (!frame.hasValue())
    {
        return aboutFile(frame.status(), path, "the light could not be widened:");
    }
    if (calibration != nullptr && calibration->calibrates())
    {
        an::Result<core::CalibratedLight> calibrated = core::calibrateLight(scope, *calibration, *frame);
        if (!calibrated.hasValue())
        {
            return aboutFile(calibrated.status(), path, "the light could not be calibrated:");
        }
        frame = an::Result<an::Image<float>>(std::move(calibrated->image));
    }
    const std::optional<an::ImageDescriptor> described = frame->descriptor();
    if (!described.has_value() || !described->isMosaiced())
    {
        return frame;
    }
    const an::Result<an::CfaDescriptor> pattern = an::ui::resolveBayerPattern(demosaic.pattern, *described);
    if (!pattern.hasValue())
    {
        return aboutFile(pattern.status(), path, "the light's filter arrangement could not be resolved:");
    }
    an::Result<an::consumer::AnyImage> colour = an::ui::demosaicViewFrame(
        scope.engine(), scope.execution(), an::consumer::AnyImage(std::move(*frame)), demosaic.method, *pattern);
    if (!colour.hasValue())
    {
        return aboutFile(colour.status(), path, "the light could not be demosaiced:");
    }
    if (auto* typed = std::get_if<an::Image<float>>(&*colour))
    {
        return std::move(*typed);
    }
    return an::Status::failure(an::ErrorCode::Internal, "the demosaiced light is not Float32");
}

an::Result<an::Image<float>>
warpRegistrationFrame(const an::consumer::OperationScope& scope, const an::Image<float>& source,
                      const an::RegistrationTransform& transform, const std::optional<an::SipDistortion>& distortion,
                      const an::RegistrationCanvas& canvas, const an::KernelInterpolation interpolation,
                      const std::optional<an::WcsSolution>& planeSolution)
{
    const std::optional<an::ImageDescriptor> descriptor = source.descriptor();
    const auto view = source.view();
    if (!descriptor.has_value() || !view.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the frame holds nothing");
    }
    const an::Result<an::RegistrationWarpPlan> plan =
        an::planRegistrationWarp(*descriptor, transform, distortion.value_or(an::SipDistortion{}),
                                 an::RegistrationWarpRequest{.width = canvas.width,
                                                             .height = canvas.height,
                                                             .canvasOrigin = canvas.origin,
                                                             .sampling = an::SamplingKernelParameters{
                                                                 .interpolation = interpolation,
                                                                 .border = an::KernelBorderMode::Constant,
                                                                 .invalid = an::SamplingInvalidPolicy::RequireAll}});
    if (!plan.hasValue())
    {
        return plan.status();
    }

    const an::ImageMetadata& sourceMetadata = source.metadata();
    an::Result<an::CodecMetadata> codec = an::withoutWcsCards(sourceMetadata.codec());
    if (codec.hasValue() && planeSolution.has_value())
    {
        codec = an::withWcsCards(*codec, *planeSolution);
    }
    if (!codec.hasValue())
    {
        return codec.status();
    }
    an::AstrometricSolution astrometry;
    if (planeSolution.has_value())
    {
        an::Result<an::AstrometricSolution> summary = planeSolution->toAstrometricSolution();
        if (!summary.hasValue())
        {
            return summary.status();
        }
        astrometry = *summary;
    }
    const an::ImageMetadata destinationMetadata =
        an::ImageMetadata::create(sourceMetadata.acquisition(), sourceMetadata.semantics(), astrometry,
                                  sourceMetadata.provenance(), std::move(*codec), sourceMetadata.outputIntent());

    an::Result<an::Image<float>> warped =
        an::Image<float>::create(scope.engine(), plan->destinationDescriptor(), an::StorageArrangement::Planar,
                                 an::StorageInitialization::Zeroed, destinationMetadata);
    if (!warped.hasValue())
    {
        return warped.status();
    }
    an::Result<an::ValidityMask> mask =
        an::ValidityMask::create(scope.engine(), plan->destinationDescriptor().extent());
    if (!mask.hasValue())
    {
        return mask.status();
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = warped->write();
        std::optional<an::ValidityMaskView<std::uint8_t>> maskView = mask->view();
        if (!access.has_value() || !maskView.has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the warped frame holds nothing");
        }
        const an::Result<an::RegistrationWarpResult> ran = an::warpRegisteredImage(
            scope.execution(), an::SampleType::Float32, view->data(), view->layout(), access->view().data(),
            access->view().layout(), *plan, an::KernelValidityView{}, *maskView);
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }
    const an::Result<an::MetadataTransition> change =
        an::operationTransition(kAlignmentEffect, an::OperationOutcome{.operation = "an::warpRegisteredImage",
                                                                       .detail = "onto the registration's plane"});
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status stated = warped->transition(plan->destinationDescriptor(), *change); stated.failed())
    {
        return stated;
    }
    return std::move(*warped);
}

} // namespace stage_utils
