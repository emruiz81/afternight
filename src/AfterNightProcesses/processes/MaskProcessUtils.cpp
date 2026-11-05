// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The mask, star and background processes' shared work: derived images in a run's session, a colour image's
/// lightness, and the extra images an Apply stages for its window.

#include "stdafx.hpp"

#include "processes/MaskProcessUtils.h"
#include "processes/PointwiseProcessUtils.h"

#include "app/AppContextHelpers.h"
#include "controls/IAppContext.h"
#include "controls/ProcessExecution.h"
#include "session/ImageDocument.h"

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Algorithms/Masks/MaskOperations.h>
#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <array>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace process_utils
{

namespace
{

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// Creates an image of a representation and adopts it into the run's session.
template <typename SampleT>
[[nodiscard]] an::Result<an::consumer::ImageAsset>
adoptNew(const an::ui::ExecutionScope& scope, const an::ImageDescriptor& described, an::ImageMetadata&& metadata)
{
    an::Result<an::Image<SampleT>> created =
        an::Image<SampleT>::create(scope.engine(), described, an::StorageArrangement::Planar,
                                   an::StorageInitialization::Zeroed, std::move(metadata));
    if (!created.hasValue())
    {
        return created.status();
    }
    return an::consumer::ImageAsset::adopt(scope.session(), an::consumer::AnyImage(std::move(*created)));
}

} // namespace

bool describesColourImage(const an::ImageDescriptor& described) noexcept
{
    return described.channelSchema() == an::ChannelSchema::Rgb || described.channelSchema() == an::ChannelSchema::Rgba;
}

an::Result<an::ImageDescriptor> derivedImageDescriptor(const an::ImageDescriptor& source, std::uint32_t channels,
                                                       an::SampleType sampleType, const an::EngineLimits& limits)
{
    if (channels != 1u && channels != 3u)
    {
        return refusal(an::ErrorCode::InvalidArgument, "a derived image is grey or colour");
    }
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(source.extent().width(), source.extent().height(), channels, limits);
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the source's size cannot hold the derived image");
    }
    const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(
        an::ImageDescriptorRequest{.extent = *extent,
                                   .sampleType = sampleType,
                                   .channelSchema = channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb,
                                   .colorSpace = channels == 1u ? an::ColorSpace::Unspecified : source.colorSpace(),
                                   .numericDomain = an::NumericDomain::NormalizedLinear,
                                   .cfa = an::CfaDescriptor{},
                                   .coordinateOrigin = source.coordinateOrigin(),
                                   .pixelAspectRatio = source.pixelAspectRatio()});
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the derived image cannot be described");
    }
    return *described;
}

an::Result<an::ImageMetadata> derivedImageMetadata(const an::ImageMetadata& source,
                                                   const an::ImageDescriptor& described,
                                                   const std::string& stateOperation)
{
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(an::ImageSemanticsRequest{});
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    an::CodecMetadata cards;
    if (!stateOperation.empty())
    {
        an::Result<an::CodecMetadata> stated = an::withProcessingStateCards(
            an::CodecMetadata{},
            an::nonlinearState(an::readProcessingStateCards(an::CodecMetadata{}), described, stateOperation));
        if (!stated.hasValue())
        {
            return stated.status();
        }
        cards = std::move(*stated);
    }
    return an::ImageMetadata::create(source.acquisition(), std::move(*semantics), source.astrometry(),
                                     source.provenance(), std::move(cards), std::nullopt);
}

an::Result<an::consumer::ImageAsset> createDerivedImage(const an::ui::ExecutionScope& scope,
                                                        const an::ImageDescriptor& described,
                                                        an::ImageMetadata metadata)
{
    switch (described.sampleType())
    {
    case an::SampleType::UInt8:
        return adoptNew<std::uint8_t>(scope, described, std::move(metadata));
    case an::SampleType::Int8:
        return adoptNew<std::int8_t>(scope, described, std::move(metadata));
    case an::SampleType::UInt16:
        return adoptNew<std::uint16_t>(scope, described, std::move(metadata));
    case an::SampleType::Int16:
        return adoptNew<std::int16_t>(scope, described, std::move(metadata));
    case an::SampleType::UInt32:
        return adoptNew<std::uint32_t>(scope, described, std::move(metadata));
    case an::SampleType::Int32:
        return adoptNew<std::int32_t>(scope, described, std::move(metadata));
    case an::SampleType::Float32:
        return adoptNew<float>(scope, described, std::move(metadata));
    case an::SampleType::Float64:
        return adoptNew<double>(scope, described, std::move(metadata));
    }
    return refusal(an::ErrorCode::InvalidArgument, "the derived image's representation is not one an image holds");
}

an::Result<an::consumer::ImageAsset> extractLightness(const an::ui::ExecutionScope& scope,
                                                      const an::ui::ProcessSource& source,
                                                      const std::string& stateOperation)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the source holds nothing");
    }
    if (!describesColourImage(*described))
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "only a colour image has a CIE lightness of its own");
    }
    // An integer source is read as the fractions of full scale its codes stand for - unsigned a fraction of the
    // largest code, signed counted from the smallest - and its lightness is a floating image. A floating source
    // stating no full scale, as every floating file is read, is read as fractions of full scale.
    std::optional<NormalizedCopy> normalized;
    std::optional<an::ImageDescriptor> read = an::ui::componentReadingOf(*described);
    if (!read.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the source cannot be described as unit fractions");
    }
    if (an::isIntegerSampleType(described->sampleType()))
    {
        an::Result<NormalizedCopy> copy = normalizedCopyOf(scope, source);
        if (!copy.hasValue())
        {
            return copy.status();
        }
        normalized.emplace(std::move(*copy));
        const an::Result<an::ImageDescriptor> working =
            an::describeSamplesAs(*described, normalized->samples.sampleType());
        if (!working.hasValue())
        {
            return working.status();
        }
        an::ImageDescriptorRequest fractions = working->toRequest();
        fractions.numericDomain = an::NumericDomain::NormalizedLinear;
        read = an::ImageDescriptor::create(fractions);
        if (!read.has_value())
        {
            return refusal(an::ErrorCode::InvalidArgument, "the source cannot be described as unit fractions");
        }
    }
    const an::Result<an::ChannelScopePlan> plan =
        an::planChannelScope(an::ui::processChannelScopeRequest(an::ChannelScope::CieLightness, *read));
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const an::Result<an::ImageDescriptor> planeDescribed = an::channelScopePlaneDescriptor(*plan, *read);
    if (!planeDescribed.hasValue())
    {
        return planeDescribed.status();
    }
    an::Result<an::ImageMetadata> metadata = derivedImageMetadata(source.metadata(), *planeDescribed, stateOperation);
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    an::Result<an::consumer::ImageAsset> lightness = createDerivedImage(scope, *planeDescribed, std::move(*metadata));
    if (!lightness.hasValue())
    {
        return lightness.status();
    }
    an::Result<an::consumer::AssetWriteLease> lease = lightness->write();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const std::optional<an::ValidityMaskView<const std::uint8_t>> mask =
        source.validity() != nullptr ? source.validity()->view() : std::nullopt;
    auto extractFrom = [&](const an::ChannelScopeSource& scoped) -> an::Status {
        return std::visit(
            [&](auto& written) -> an::Status {
                using PlaneT = typename std::remove_cvref_t<decltype(written)>::Sample;
                const std::optional<an::ImageWriteAccess<PlaneT>> access = lease->template write<PlaneT>();
                if (!access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the lightness cannot be written");
                }
                const an::ImageLayout planeLayout = access->view().layout();
                const an::Result<an::ChannelScopeResult> result =
                    an::extractChannelScope(scope.execution(), *plan, scoped, access->view().data(), planeLayout);
                return result.hasValue() ? an::Status::success() : result.status();
            },
            lease->image());
    };
    an::Status extracted = an::Status::success();
    if (normalized.has_value())
    {
        an::ChannelScopeSource scoped;
        scoped.descriptor = &*read;
        scoped.samples = normalized->samples.samples();
        scoped.layout = &normalized->samples.layout();
        scoped.validity = normalized->samples.validity();
        extracted = extractFrom(scoped);
    }
    else
    {
        extracted = std::visit(
            [&](const auto& typed) -> an::Status {
                const auto view = typed.view();
                if (!view.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the source holds nothing");
                }
                const an::ImageLayout sourceLayout = view->layout();
                an::ChannelScopeSource scoped;
                scoped.descriptor = &*read;
                scoped.samples = view->data();
                scoped.layout = &sourceLayout;
                scoped.validity.descriptor = source.metadata().semantics().validity();
                scoped.validity.mask = mask.has_value() ? &*mask : nullptr;
                return extractFrom(scoped);
            },
            source.image());
    }
    lease->release();
    if (extracted.failed())
    {
        return extracted;
    }
    return std::move(*lightness);
}

an::Status scaleAndBiasInPlace(const an::ExecutionContext& execution, an::SampleType sampleType, void* samples,
                               const an::ImageLayout& layout, double scale, double bias,
                               std::optional<an::ClampInterval> clamp)
{
    const an::Result<an::PointwiseProgram> program =
        an::buildScaleBiasProgram(normalizedPlanShape(sampleType, layout.extent().channelCount()), scale, bias, clamp);
    if (!program.hasValue())
    {
        return program.status();
    }
    const std::array<an::PointwiseImageView, 1u> sources{an::PointwiseImageView{.samples = samples, .layout = &layout}};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        execution, *program, sources, an::PointwiseMutableImageView{.samples = samples, .layout = &layout},
        an::KernelValidityView{});
    return ran.hasValue() ? an::Status::success() : ran.status();
}

an::Status holdToUnitInterval(const an::EngineContext& engine, const an::ExecutionContext& execution,
                              an::SampleType sampleType, void* samples, const an::ImageLayout& layout)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(
        layout.extent().width(), layout.extent().height(), layout.extent().channelCount(), engine.limits());
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the samples' mask cannot be described");
    }
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, *extent);
    if (!mask.hasValue())
    {
        return mask.status();
    }
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable = mask->view();
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    if (!writable.has_value() || !finite.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the samples' mask holds nothing");
    }
    if (an::Status marked = an::markImageValidity(execution, sampleType, samples, layout, *finite, *writable);
        marked.failed())
    {
        return marked;
    }
    const std::optional<an::ValidityMaskView<const std::uint8_t>> readable = std::as_const(*mask).view();
    const an::Result<an::MaskApplyResult> substituted =
        an::applyImageMask(execution, sampleType, samples, layout, *readable, 0.0);
    if (!substituted.hasValue())
    {
        return substituted.status();
    }
    return scaleAndBiasInPlace(execution, sampleType, samples, layout, 1.0, 0.0,
                               an::ClampInterval{.low = 0.0, .high = 1.0});
}

an::Result<NormalizedCopy> normalizedCopyOf(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source)
{
    an::Result<an::ui::ProcessDestination> copy =
        an::ui::ProcessDestination::copyOf(scope.session(), scope.engine(), scope.execution(), source);
    if (!copy.hasValue())
    {
        return copy.status();
    }
    NormalizedCopy made{.copy = std::move(*copy), .samples = NormalizedSamples{}};
    an::Result<NormalizedSamples> samples = openNormalizedDestination(scope, source, made.copy);
    if (!samples.hasValue())
    {
        return samples.status();
    }
    made.samples = std::move(*samples);
    return made;
}

void StagedOutputs::clear()
{
    const std::lock_guard lock(m_mutex);
    m_outputs.clear();
}

void StagedOutputs::stage(StagedOutput&& output)
{
    const std::lock_guard lock(m_mutex);
    m_outputs.push_back(std::move(output));
}

std::vector<StagedOutput> StagedOutputs::take()
{
    const std::lock_guard lock(m_mutex);
    std::vector<StagedOutput> taken = std::move(m_outputs);
    m_outputs.clear();
    return taken;
}

std::size_t openStagedOutputs(std::vector<StagedOutput>&& outputs, ImageViewControl* neighbour)
{
    std::size_t opened = 0u;
    an::ui::IViewFactory* factory = currentViewFactory();
    if (factory == nullptr)
    {
        return opened;
    }
    for (StagedOutput& output : outputs)
    {
        an::Result<an::ui::ImageDocument> document =
            an::ui::ImageDocument::create(std::move(output.asset), an::ui::DocumentNames{.displayName = output.name});
        if (!document.hasValue())
        {
            continue;
        }
        if (factory->createNewImageViewControlFromImage(nullptr, std::move(*document), nullptr, output.history,
                                                        ViewControlMode::standard, neighbour) != nullptr)
        {
            ++opened;
        }
    }
    return opened;
}

} // namespace process_utils
