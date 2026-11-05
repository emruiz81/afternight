// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Apply Stretch over the engine's display presentation: the production plan into Float32, then the image's own
/// representation at full scale.

#include "AutostretchApply.h"

#include "widgets/ViewPresentation.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace an::ui
{

namespace
{

/// What bringing the stretched image back to its own representation does: the samples are written again and the
/// description is restated; nothing else about the image changes.
constexpr an::OperationEffect kRepresentationChangeEffect{.samples = an::SampleEffect::Writes,
                                                          .description = an::DescriptionEffect::Restates};

/// A failure Apply Stretch decided, which no engine call produced.
[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// Presents a typed image into a Float32 working image through the production plan, and records what the
/// presentation did to it.
template <typename SampleT>
[[nodiscard]] an::Result<an::Image<float>>
presentTyped(const an::EngineContext& engine, const an::ExecutionContext& execution, const an::Image<SampleT>& image,
             const an::ValidityMask* validity, const an::DisplayPresentationRequest& request)
{
    std::optional<an::ImageDescriptor> descriptor = image.descriptor();
    const auto view = image.view();
    if (!descriptor.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    // A floating image in a physical or sensor-code domain has no full scale; it is read as a fraction of one, as the
    // view shows it.
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        const an::NumericDomain domain = descriptor->numericDomain();
        if (domain == an::NumericDomain::PhysicalScaled || domain == an::NumericDomain::RawSensorCode)
        {
            an::ImageDescriptorRequest restated = descriptor->toRequest();
            restated.numericDomain = an::NumericDomain::NormalizedLinear;
            descriptor = an::ImageDescriptor::create(restated);
            if (!descriptor.has_value())
            {
                return refusal(an::ErrorCode::IncompatibleArguments,
                               "the floating image has no description as a fraction of full scale");
            }
        }
    }
    const an::Result<an::DisplayPresentationPlan> plan = an::planDisplayPresentation(request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::ImageDescriptorRequest workingRequest = descriptor->toRequest();
    workingRequest.sampleType = an::SampleType::Float32;
    workingRequest.numericDomain = an::NumericDomain::NormalizedLinear;
    const std::optional<an::ImageDescriptor> working = an::ImageDescriptor::create(workingRequest);
    if (!working.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the image has no Float32 description");
    }
    an::Result<an::Image<float>> stretched = an::Image<float>::createDestination(
        engine, *working, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, image);
    if (!stretched.hasValue())
    {
        return stretched.status();
    }
    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
    an::DisplayPresentationSource source{
        .descriptor = &*descriptor, .samples = view->data(), .layout = &view->layout()};
    source.validity.descriptor = image.metadata().semantics().validity();
    if (validity != nullptr)
    {
        maskView = validity->view();
        source.validity.mask = maskView.has_value() ? &*maskView : nullptr;
    }
    {
        const std::optional<an::ImageWriteAccess<float>> access = stretched->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the stretched image holds nothing");
        }
        const an::Result<an::DisplayPresentationResult> presented =
            an::presentImage(execution, *plan, source, access->view().data(), access->view().layout());
        if (!presented.hasValue())
        {
            return presented.status();
        }
    }
    // The samples are display data rather than measurements, so their meaning is restated; the pixel grid is the
    // image's, so its solution still describes them.
    an::ImageSemanticsRequest meaning = image.metadata().semantics().toRequest();
    meaning.transferFunction = an::TransferFunction::Unspecified;
    meaning.gammaExponent.reset();
    meaning.validity = an::kDisplayPresentationEffect.validity.produced;
    const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    const an::Result<an::MetadataTransition> change = an::operationTransition(
        an::kDisplayPresentationEffect, an::OperationOutcome{.operation = "an::presentImage",
                                                             .detail = "the view's Apply Stretch",
                                                             .semantics = *semantics});
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status recorded = stretched->transition(*change); recorded.failed())
    {
        return recorded;
    }
    return stretched;
}

/// Brings the stretched image back to a representation at full scale, to the nearest value, and records the change.
template <typename SampleT>
[[nodiscard]] an::Result<an::consumer::AnyImage> toRepresentation(const an::consumer::OperationScope& scope,
                                                                  an::Image<float>&& stretched)
{
    if constexpr (std::is_same_v<SampleT, float>)
    {
        return an::consumer::AnyImage(std::move(stretched));
    }
    else
    {
        const std::optional<an::ImageDescriptor> source = stretched.descriptor();
        const auto view = stretched.view();
        if (!source.has_value() || !view.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the stretched image holds nothing");
        }
        const an::Result<an::ImageDescriptor> described =
            an::describeSamplesAs(*source, an::Image<SampleT>::kSampleType);
        if (!described.hasValue())
        {
            return described.status();
        }
        an::Result<an::Image<SampleT>> converted =
            an::Image<SampleT>::createDestination(scope.engine(), *described, an::StorageArrangement::Planar,
                                                  an::StorageInitialization::Uninitialized, stretched);
        if (!converted.hasValue())
        {
            return converted.status();
        }
        {
            const std::optional<an::ImageWriteAccess<SampleT>> access = converted->write();
            if (!access.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the converted image holds nothing");
            }
            an::ConversionCounters counters;
            const an::Status done =
                an::convertImageSamples(scope.execution(), an::SampleType::Float32, view->data(), view->layout(),
                                        an::Image<SampleT>::kSampleType, access->view().data(), access->view().layout(),
                                        an::ConversionPolicy{}, counters);
            if (done.failed())
            {
                return done;
            }
        }
        const an::Result<an::MetadataTransition> change =
            an::operationTransition(kRepresentationChangeEffect,
                                    an::OperationOutcome{.operation = "an::convertImageSamples",
                                                         .detail = std::string("Float32 to ") +
                                                                   an::sampleTypeName(an::Image<SampleT>::kSampleType) +
                                                                   ", nearest value at full scale"});
        if (!change.hasValue())
        {
            return change.status();
        }
        if (an::Status recorded = converted->transition(*change); recorded.failed())
        {
            return recorded;
        }
        return an::consumer::AnyImage(std::move(*converted));
    }
}

} // namespace

an::DisplayPresentationRequest autostretchApplyRequest(const AutostretchApplySettings& settings) noexcept
{
    an::DisplayPresentationRequest request;
    request.promotion = an::PresentationPromotion::Gradient;
    request.stretch = an::PresentationStretch::Automatic;
    request.linked = settings.linkedChannels;
    // A balance only means something when the stretch that follows is linked; an unlinked stretch undoes it.
    request.neutralize = settings.neutralizeBackground && settings.linkedChannels;
    request.calibrate = settings.colorCalibration && settings.linkedChannels;
    request.shadowsClipping = settings.shadowsClipping;
    request.targetBackground = settings.targetBackground;
    request.saturation = settings.saturation;
    request.saturationBackgroundFactor = settings.saturationBackground;
    request.saturationBackgroundLocation =
        settings.sequenceFrame ? an::SaturationBackgroundLocation::Mean : an::SaturationBackgroundLocation::Median;
    request.brightness = presentationBrightnessOffset(settings.brightness);
    request.contrast = presentationContrastFactor(settings.contrast);
    request.output = an::PresentationOutput::WorkingFloat32;
    return request;
}

an::Result<an::consumer::AnyImage> autostretchFrame(const an::EngineContext& engine,
                                                    const an::ExecutionContext& execution,
                                                    const an::consumer::AnyImage& image,
                                                    const an::ValidityMask* validity,
                                                    const AutostretchApplySettings& settings)
{
    const an::DisplayPresentationRequest request = autostretchApplyRequest(settings);
    an::Result<an::Image<float>> stretched =
        std::visit([&](const auto& typed) { return presentTyped(engine, execution, typed, validity, request); }, image);
    if (!stretched.hasValue())
    {
        return stretched.status();
    }
    return an::consumer::AnyImage(std::move(*stretched));
}

an::Result<an::consumer::ImageAsset>
applyAutostretchToCopy(const an::consumer::EngineSession& session, const an::consumer::AnyImage& image,
                       const an::ValidityMask* validity, const AutostretchApplySettings& settings, std::stop_token stop)
{
    if (stop.stop_requested())
    {
        return refusal(an::ErrorCode::Cancelled, "the stretch was stopped");
    }
    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    const an::DisplayPresentationRequest request = autostretchApplyRequest(settings);
    an::Result<an::consumer::AnyImage> stretched = std::visit(
        [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            an::Result<an::Image<float>> presented =
                presentTyped(scope->engine(), scope->execution(), typed, validity, request);
            if (!presented.hasValue())
            {
                return presented.status();
            }
            return toRepresentation<SampleT>(*scope, std::move(*presented));
        },
        image);
    if (!stretched.hasValue())
    {
        return stretched.status();
    }
    scope->end();
    // The stretched image is display data, valid wherever it was written; an explicit mask of the source no longer
    // describes it.
    return an::consumer::ImageAsset::adopt(session, std::move(*stretched));
}

} // namespace an::ui
