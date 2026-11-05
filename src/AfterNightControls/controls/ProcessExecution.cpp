// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The process framework's engine work around a body: the destination that starts as a copy of the source, the channel
/// scope a run snapshotted, the modulation and the mask, the conversions of what a run reads that the next run reads
/// again, and the preview area's crop, composite base and composite.

#include "controls/ProcessExecution.h"

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace an::ui
{

namespace
{

/// A failure the framework decided, which no engine call produced.
[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// The sample type of a typed image.
template <typename ImageT> using SampleOf = typename std::remove_cvref_t<ImageT>::Sample;

/// What a crop of the preview area does to an image: the engine's geometry effect, with the validity rule kept, because
/// every sample of the crop is one of the source's and a mask is cropped with them.
inline constexpr an::OperationEffect kRegionCropEffect{.description = an::DescriptionEffect::Restates,
                                                       .solution = an::SolutionEffect::Invalidates,
                                                       .codecMetadata = an::CodecMetadataEffect::Drops,
                                                       .outputIntent = an::OutputIntentEffect::Withdraws,
                                                       .validity = {.policy = an::ValidityPolicy::Preserve}};

/// The colour image's selector, in the order it is shown.
constexpr std::array<ProcessChannelChoice, 21u> kChannelChoices{{
    {an::ChannelScope::Whole, "RGB", "rgb", false},
    {an::ChannelScope::Red, "Red", "red_circle", false},
    {an::ChannelScope::Green, "Green", "green_circle", false},
    {an::ChannelScope::Blue, "Blue", "blue_circle", false},
    {an::ChannelScope::CieLightness, "CIE L* (Lightness)", "cielab", true},
    {an::ChannelScope::CieA, "CIE a*", "cielab", false},
    {an::ChannelScope::CieB, "CIE b*", "cielab", false},
    {an::ChannelScope::CieChroma, "CIE c*", "cielab", false},
    {an::ChannelScope::CieHue, "CIE h*", "cielab", false},
    {an::ChannelScope::CieX, "CIE X", "cielab", true},
    {an::ChannelScope::CieY, "CIE Y (Luminance)", "cielab", false},
    {an::ChannelScope::CieZ, "CIE Z", "cielab", false},
    {an::ChannelScope::HslHue, "HSL Hue", "hsl", true},
    {an::ChannelScope::HslSaturation, "HSL Saturation", "hsl", false},
    {an::ChannelScope::HslLightness, "HSL Lightness", "hsl", false},
    {an::ChannelScope::HsvHue, "HSV Hue", "hsv", true},
    {an::ChannelScope::HsvSaturation, "HSV Saturation", "hsv", false},
    {an::ChannelScope::HsvValue, "HSV Value", "hsv", false},
    {an::ChannelScope::YCbCrLuma, "YCbCr Luma", "cielab", true},
    {an::ChannelScope::YCbCrBlueChroma, "YCbCr Blue Chroma", "cielab", false},
    {an::ChannelScope::YCbCrRedChroma, "YCbCr Red Chroma", "cielab", false},
}};

/// Whether a description holds red, green and blue to choose a channel or component of.
[[nodiscard]] bool isColour(const an::ImageDescriptor& descriptor) noexcept
{
    return descriptor.channelSchema() == an::ChannelSchema::Rgb ||
           descriptor.channelSchema() == an::ChannelSchema::Rgba;
}

/// The description an image holds, or nothing.
[[nodiscard]] std::optional<an::ImageDescriptor> descriptorOf(const an::consumer::AnyImage& image)
{
    return std::visit([](const auto& typed) { return typed.descriptor(); }, image);
}

/// A view of a packed mask for reading, or nothing for no mask.
[[nodiscard]] std::optional<an::ValidityMaskView<const std::uint8_t>> maskViewOf(const an::ValidityMask* mask)
{
    if (mask == nullptr)
    {
        return std::nullopt;
    }
    return mask->view();
}

/// The layout of one plane of a planar image, at the image's own strides.
[[nodiscard]] an::Result<an::ImageLayout> planeLayoutOf(const an::ImageLayout& layout, const an::EngineLimits& limits)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(layout.extent().width(), layout.extent().height(), 1u, limits);
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "a plane of the image cannot be described");
    }
    const std::optional<an::ImageLayout> plane = an::ImageLayout::createWithStrides(
        an::ImageLayoutRequest{.extent = *extent,
                               .elementSizeBytes = layout.elementSizeBytes(),
                               .samplesPerPixel = 1u,
                               .planeCount = 1u,
                               .rowAlignmentBytes = 1u},
        layout.rowStrideBytes(), layout.rowStrideBytes() * static_cast<std::uint64_t>(layout.extent().height()),
        limits);
    if (!plane.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "a plane of the image cannot be laid out");
    }
    return *plane;
}

/// The first byte of one plane of a planar image.
template <typename ByteT>
[[nodiscard]] ByteT* planeStart(ByteT* data, const an::ImageLayout& layout, std::uint32_t plane)
{
    return data + static_cast<std::size_t>(layout.planeStrideBytes()) * plane;
}

/// A planar image of a representation holding another image's samples, converted at full scale when the
/// representation differs; or nothing when the image is planar in that representation already and needs no copy.
template <typename DestT, typename SourceT>
[[nodiscard]] an::Result<std::optional<an::Image<DestT>>> planarWorkingCopy(const an::EngineContext& engine,
                                                                            const an::ExecutionContext& execution,
                                                                            const an::Image<SourceT>& image)
{
    const std::optional<an::ImageDescriptor> described = image.descriptor();
    const auto view = image.view();
    if (!described.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    const bool planar = view->layout().samplesPerPixel() == 1u;
    if constexpr (std::is_same_v<SourceT, DestT>)
    {
        if (planar)
        {
            return std::optional<an::Image<DestT>>{};
        }
    }
    // A planar copy in the source's own representation first, when the source is interleaved: a conversion keeps the
    // arrangement and a repack keeps the representation.
    std::optional<an::Image<SourceT>> repacked;
    const an::Image<SourceT>* planarSource = &image;
    if (!planar)
    {
        an::Result<an::Image<SourceT>> copy = an::Image<SourceT>::createDestination(
            engine, *described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, image);
        if (!copy.hasValue())
        {
            return copy.status();
        }
        {
            std::optional<an::ImageWriteAccess<SourceT>> access = copy->write();
            if (!access.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the working copy holds nothing");
            }
            const an::Status moved =
                an::repackImageSamples(execution, an::kSampleTypeOf<SourceT>, view->data(), view->layout(),
                                       access->view().data(), access->view().layout());
            if (moved.failed())
            {
                return moved;
            }
        }
        repacked.emplace(std::move(*copy));
        planarSource = &*repacked;
    }
    if constexpr (std::is_same_v<SourceT, DestT>)
    {
        return std::optional<an::Image<DestT>>(std::move(*repacked));
    }
    else
    {
        an::ImageDescriptorRequest request = described->toRequest();
        request.sampleType = an::kSampleTypeOf<DestT>;
        const std::optional<an::ImageDescriptor> converted = an::ImageDescriptor::create(request);
        if (!converted.has_value())
        {
            return refusal(an::ErrorCode::IncompatibleArguments, "the image has no description in the representation");
        }
        an::Result<an::Image<DestT>> copy = an::Image<DestT>::createDestination(
            engine, *converted, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, image);
        if (!copy.hasValue())
        {
            return copy.status();
        }
        {
            std::optional<an::ImageWriteAccess<DestT>> access = copy->write();
            const auto planarView = planarSource->view();
            if (!access.has_value() || !planarView.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the working copy holds nothing");
            }
            an::ConversionCounters counters;
            const an::Status convertedSamples =
                an::convertImageSamples(execution, an::kSampleTypeOf<SourceT>, planarView->data(), planarView->layout(),
                                        an::kSampleTypeOf<DestT>, access->view().data(), access->view().layout(),
                                        an::ConversionPolicy{}, counters);
            if (convertedSamples.failed())
            {
                return convertedSamples;
            }
        }
        return std::optional<an::Image<DestT>>(std::move(*copy));
    }
}

/// Where a planar image's samples are read from: the image itself, the working copy made of it, or the one an earlier
/// run kept.
template <typename DestT> struct PlanarSamples
{
    std::optional<an::Image<DestT>> copy;
    std::shared_ptr<const an::consumer::AnyImage> kept;
    const std::byte* data = nullptr;
    std::optional<an::ImageLayout> layout;
    std::uint32_t channels = 0u;
};

/// A planar reading of any image in the representation `DestT`.
template <typename DestT>
[[nodiscard]] an::Result<PlanarSamples<DestT>> planarSamples(const an::EngineContext& engine,
                                                             const an::ExecutionContext& execution,
                                                             const an::consumer::AnyImage& image)
{
    return std::visit(
        [&](const auto& typed) -> an::Result<PlanarSamples<DestT>> {
            using SourceT = SampleOf<decltype(typed)>;
            an::Result<std::optional<an::Image<DestT>>> working =
                planarWorkingCopy<DestT, SourceT>(engine, execution, typed);
            if (!working.hasValue())
            {
                return working.status();
            }
            PlanarSamples<DestT> samples;
            samples.copy = std::move(*working);
            if (samples.copy.has_value())
            {
                const auto view = samples.copy->view();
                samples.data = reinterpret_cast<const std::byte*>(view->data());
                samples.layout = view->layout();
            }
            else
            {
                // The image is planar in this representation already.
                const auto view = typed.view();
                samples.data = reinterpret_cast<const std::byte*>(view->data());
                samples.layout = view->layout();
            }
            samples.channels = samples.layout->extent().channelCount();
            return samples;
        },
        image);
}

/// Whether reading an image a plane per channel in the representation `DestT` copies it.
template <typename DestT> [[nodiscard]] bool needsPlanarCopy(const an::consumer::AnyImage& image)
{
    return std::visit(
        [](const auto& typed) {
            using SourceT = SampleOf<decltype(typed)>;
            const auto view = typed.view();
            return !std::is_same_v<SourceT, DestT> || !view.has_value() || view->layout().samplesPerPixel() != 1u;
        },
        image);
}

/// `planarSamples`, read from a run's conversions when an earlier run kept the copy for the image's revision, and kept
/// there when this one makes it.
template <typename DestT>
[[nodiscard]] an::Result<PlanarSamples<DestT>>
planarSamplesKept(const an::EngineContext& engine, const an::ExecutionContext& execution,
                  const an::consumer::AnyImage& image, ProcessConversions* conversions,
                  const an::consumer::AssetRevision& revision)
{
    if (conversions == nullptr || revision.assetIdentity == 0u || !needsPlanarCopy<DestT>(image))
    {
        return planarSamples<DestT>(engine, execution, image);
    }
    const auto readFrom =
        [](std::shared_ptr<const an::consumer::AnyImage> planar) -> std::optional<PlanarSamples<DestT>> {
        const auto* typed = std::get_if<an::Image<DestT>>(planar.get());
        const auto view = typed != nullptr ? typed->view() : std::nullopt;
        if (!view.has_value() || view->layout().samplesPerPixel() != 1u)
        {
            return std::nullopt;
        }
        PlanarSamples<DestT> samples;
        samples.data = reinterpret_cast<const std::byte*>(view->data());
        samples.layout = view->layout();
        samples.channels = samples.layout->extent().channelCount();
        samples.kept = std::move(planar);
        return samples;
    };
    if (std::optional<PlanarSamples<DestT>> kept = readFrom(conversions->planar(revision, an::kSampleTypeOf<DestT>));
        kept.has_value())
    {
        return std::move(*kept);
    }
    an::Result<PlanarSamples<DestT>> made = planarSamples<DestT>(engine, execution, image);
    if (!made.hasValue() || !made->copy.has_value())
    {
        return made;
    }
    auto planar = std::make_shared<const an::consumer::AnyImage>(std::move(*made->copy));
    std::optional<PlanarSamples<DestT>> kept = readFrom(planar);
    if (!kept.has_value())
    {
        return refusal(an::ErrorCode::Internal, "the working copy is not laid out a plane per channel");
    }
    conversions->keep(ProcessConversion{.source = revision, .planar = std::move(planar)});
    return std::move(*kept);
}

/// The value full scale is in a representation, as the engine's default conversion maps a fraction of one to it.
template <typename SampleT> [[nodiscard]] double fullScaleOf() noexcept
{
    return static_cast<double>(an::convertSample<double, SampleT>(1.0, an::ConversionPolicy{}).value);
}

/// The width a program over a representation computes in, and why when it is the wider one.
struct ProgramPrecision
{
    an::ComputePrecision precision = an::ComputePrecision::Float32;
    std::optional<an::Float64Justification> justification;
    an::OutputPrecisionContract contract = an::OutputPrecisionContract::NormalizedOrLossy;
};

/// Float64 for samples Float32 cannot hold - doubles, and thirty-two-bit codes - and Float32 otherwise.
[[nodiscard]] ProgramPrecision programPrecisionFor(an::SampleType type) noexcept
{
    switch (type)
    {
    case an::SampleType::Float64:
        return {.precision = an::ComputePrecision::Float64,
                .justification = an::Float64Justification::StoredInFloat64,
                .contract = an::OutputPrecisionContract::NormalizedOrLossy};
    case an::SampleType::UInt32:
    case an::SampleType::Int32:
        return {.precision = an::ComputePrecision::Float64,
                .justification = an::Float64Justification::ExactIntegerCodeValues,
                .contract = an::OutputPrecisionContract::ExactStoredValues};
    default:
        break;
    }
    return {};
}

/// The program that blends one channel of a result toward its source: the result is source zero and the destination,
/// the source is source one and the mask, when there is one, source two.
[[nodiscard]] an::Result<an::PointwiseProgram> blendProgram(an::SampleType type, an::NumericDomain domain,
                                                            double modulation, bool masked, bool invertMask,
                                                            double fullScale)
{
    const ProgramPrecision precision = programPrecisionFor(type);
    an::PointwiseProgramBuilder builder;
    builder.channels(1u)
        .sources(masked ? 3u : 2u)
        .readingSamples(type)
        .readingDomain(domain)
        .promising(precision.contract)
        .computingIn(precision.precision, precision.justification);
    if (!masked)
    {
        // result * modulation + source * (1 - modulation)
        builder.append(an::pointwiseBlend(1u, 1.0 - modulation));
        return builder.build();
    }
    // The mask's samples are codes at the result's full scale; the weight is the mask's fraction of it.
    builder.append(an::pointwiseSubtract(1u));
    if (modulation != 1.0)
    {
        builder.append(an::pointwiseScale(modulation));
    }
    builder.append(an::pointwiseMask(2u));
    if (!invertMask)
    {
        // source + (result - source) * modulation * mask
        if (fullScale != 1.0)
        {
            builder.append(an::pointwiseScale(1.0 / fullScale));
        }
        builder.append(an::pointwiseAdd(1u));
        return builder.build();
    }
    // source + (result - source) * modulation * (1 - mask), where the result is still source plane zero.
    builder.append(an::pointwiseScale(-1.0 / fullScale));
    builder.append(an::pointwiseAdd(1u));
    builder.append(an::pointwiseAdd(0u, modulation));
    builder.append(an::pointwiseSubtract(1u, modulation));
    return builder.build();
}

/// Blends a typed result in place.
template <typename DestT>
[[nodiscard]] an::Status blendTyped(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                    const ProcessSource& source, an::consumer::AssetWriteLease& result,
                                    const ProcessBlendRequest& request)
{
    std::optional<an::ImageWriteAccess<DestT>> access = result.write<DestT>();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the result holds nothing");
    }
    const an::Image<DestT>& typedResult = std::get<an::Image<DestT>>(result.image());
    const std::optional<an::ImageDescriptor> resultDescriptor = typedResult.descriptor();
    const std::optional<an::ImageDescriptor> sourceDescriptor = source.descriptor();
    if (!resultDescriptor.has_value() || !sourceDescriptor.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the result or its source holds nothing");
    }
    const an::ImageLayout& resultLayout = access->view().layout();
    const std::uint32_t width = resultLayout.extent().width();
    const std::uint32_t height = resultLayout.extent().height();
    const std::uint32_t channels = resultLayout.extent().channelCount();
    if (sourceDescriptor->extent().width() != width || sourceDescriptor->extent().height() != height)
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the result has another size than its source");
    }
    if (sourceDescriptor->extent().channelCount() > channels)
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the result has fewer channels than its source");
    }
    if (resultLayout.samplesPerPixel() != 1u)
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the result is not laid out a plane per channel");
    }

    an::Result<PlanarSamples<DestT>> sourceSamples =
        planarSamplesKept<DestT>(engine, execution, source.image(), source.conversions(), source.revision());
    if (!sourceSamples.hasValue())
    {
        return sourceSamples.status();
    }
    std::optional<PlanarSamples<DestT>> maskSamples;
    if (request.mask != nullptr)
    {
        const std::optional<an::ImageDescriptor> maskDescriptor = descriptorOf(*request.mask);
        if (!maskDescriptor.has_value() || maskDescriptor->extent().width() != width ||
            maskDescriptor->extent().height() != height)
        {
            return refusal(an::ErrorCode::IncompatibleArguments, "the mask has another size than the image");
        }
        const std::uint32_t maskChannels = maskDescriptor->extent().channelCount();
        if (maskChannels != 1u && maskChannels != channels)
        {
            return refusal(an::ErrorCode::IncompatibleArguments,
                           "the mask has neither one channel nor one for every channel of the result");
        }
        an::Result<PlanarSamples<DestT>> converted =
            planarSamplesKept<DestT>(engine, execution, *request.mask, source.conversions(), request.maskRevision);
        if (!converted.hasValue())
        {
            return converted.status();
        }
        maskSamples.emplace(std::move(*converted));
    }

    const an::Result<an::PointwiseProgram> program =
        blendProgram(an::kSampleTypeOf<DestT>, resultDescriptor->numericDomain(), request.modulation,
                     maskSamples.has_value(), request.invertMask, fullScaleOf<DestT>());
    if (!program.hasValue())
    {
        return program.status();
    }

    const an::Result<an::ImageLayout> resultPlane = planeLayoutOf(resultLayout, engine.limits());
    const an::Result<an::ImageLayout> sourcePlane = planeLayoutOf(*sourceSamples->layout, engine.limits());
    if (!resultPlane.hasValue() || !sourcePlane.hasValue())
    {
        return resultPlane.hasValue() ? sourcePlane.status() : resultPlane.status();
    }
    std::optional<an::ImageLayout> maskPlane;
    if (maskSamples.has_value())
    {
        an::Result<an::ImageLayout> laid = planeLayoutOf(*maskSamples->layout, engine.limits());
        if (!laid.hasValue())
        {
            return laid.status();
        }
        maskPlane = *laid;
    }

    const std::optional<an::ValidityMaskView<const std::uint8_t>> resultMask = maskViewOf(result.validity());
    auto* resultBytes = reinterpret_cast<std::byte*>(access->view().data());
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        std::byte* resultChannel = planeStart(resultBytes, resultLayout, channel);
        const std::uint32_t sourceChannel = channel < sourceSamples->channels ? channel : 0u;
        std::array<an::PointwiseImageView, 3u> sources{};
        sources[0] = an::PointwiseImageView{.samples = resultChannel, .layout = &*resultPlane};
        sources[1] = an::PointwiseImageView{
            .samples = planeStart(sourceSamples->data, *sourceSamples->layout, sourceChannel), .layout = &*sourcePlane};
        std::size_t sourceCount = 2u;
        if (maskSamples.has_value())
        {
            const std::uint32_t maskChannel = maskSamples->channels == 1u ? 0u : channel;
            sources[2] = an::PointwiseImageView{
                .samples = planeStart(maskSamples->data, *maskSamples->layout, maskChannel), .layout = &*maskPlane};
            sourceCount = 3u;
        }
        an::KernelValidityView validity;
        validity.descriptor = typedResult.metadata().semantics().validity();
        validity.mask = resultMask.has_value() ? &*resultMask : nullptr;
        validity.maskPlane = resultMask.has_value() && resultMask->planeCount() > 1u ? channel : 0u;
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            execution, *program, std::span<const an::PointwiseImageView>(sources.data(), sourceCount),
            an::PointwiseMutableImageView{.samples = resultChannel, .layout = &*resultPlane}, validity);
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }
    return an::Status::success();
}

/// Crops a typed image and its explicit mask under an admitted operation.
template <typename SampleT>
[[nodiscard]] an::Result<std::pair<an::consumer::AnyImage, std::optional<an::ValidityMask>>>
cropTyped(const an::EngineContext& engine, const an::ExecutionContext& execution, const an::Image<SampleT>& image,
          const an::ValidityMask* validity, const an::CropRequest& crop)
{
    using Cropped = std::pair<an::consumer::AnyImage, std::optional<an::ValidityMask>>;
    const std::optional<an::ImageDescriptor> stored = image.descriptor();
    const auto view = image.view();
    if (!stored.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    const an::Result<an::GeometryPlan> plan = an::planCrop(*stored, crop);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<an::Image<SampleT>> result =
        an::Image<SampleT>::createDestination(engine, plan->destinationDescriptor(), an::StorageArrangement::Planar,
                                              an::StorageInitialization::Zeroed, image);
    if (!result.hasValue())
    {
        return result.status();
    }
    an::Result<an::ValidityMask> written = an::ValidityMask::create(engine, plan->destinationDescriptor().extent());
    if (!written.hasValue())
    {
        return written.status();
    }
    std::optional<an::ValidityMaskView<std::uint8_t>> writtenView = written->view();
    if (!writtenView.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the crop's mask holds nothing");
    }
    const std::optional<an::ValidityMaskView<const std::uint8_t>> sourceMask = maskViewOf(validity);
    an::GeometrySelection selection;
    selection.validity = image.metadata().semantics().validity();
    selection.mask = sourceMask.has_value() ? &*sourceMask : nullptr;
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = result->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the crop holds nothing");
        }
        const an::Result<an::GeometryResult> ran =
            an::transformImageGeometry(execution, an::kSampleTypeOf<SampleT>, view->data(), view->layout(),
                                       access->view().data(), access->view().layout(), *plan, selection, *writtenView);
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }
    const an::Result<an::MetadataTransition> change =
        an::operationTransition(kRegionCropEffect, an::OperationOutcome{.operation = "an::transformImageGeometry",
                                                                        .detail = "the process preview area"});
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status stated = result->transition(*change); stated.failed())
    {
        return stated;
    }
    // The mask the crop wrote is the source's validity only where the source stated one; an image every sample of which
    // is valid keeps that rule and carries no mask.
    std::optional<an::ValidityMask> croppedMask;
    if (validity != nullptr)
    {
        croppedMask.emplace(std::move(*written));
    }
    return Cropped{an::consumer::AnyImage(std::move(*result)), std::move(croppedMask)};
}

/// One plane copied from one image to another.
struct PlaneMove
{
    /// The plane read.
    std::uint32_t from = 0u;

    /// The plane written.
    std::uint32_t to = 0u;
};

/// Copies planes of a planar image into planes of another planar image of the same representation and size.
template <typename SampleT>
[[nodiscard]] an::Status copyPlanes(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                    const an::ImageView<const SampleT>& source,
                                    const an::ImageView<SampleT>& destination, std::span<const PlaneMove> moves)
{
    const an::Result<an::ImageLayout> sourcePlane = planeLayoutOf(source.layout(), engine.limits());
    const an::Result<an::ImageLayout> destinationPlane = planeLayoutOf(destination.layout(), engine.limits());
    if (!sourcePlane.hasValue() || !destinationPlane.hasValue())
    {
        return sourcePlane.hasValue() ? destinationPlane.status() : sourcePlane.status();
    }
    const auto* sourceBytes = reinterpret_cast<const std::byte*>(source.data());
    auto* destinationBytes = reinterpret_cast<std::byte*>(destination.data());
    for (const PlaneMove& move : moves)
    {
        const an::Status copied = an::repackImageSamples(
            execution, an::kSampleTypeOf<SampleT>, planeStart(sourceBytes, source.layout(), move.from), *sourcePlane,
            planeStart(destinationBytes, destination.layout(), move.to), *destinationPlane);
        if (copied.failed())
        {
            return copied;
        }
    }
    return an::Status::success();
}

/// Raises one zeroed plane of a planar image to a constant through a one-step program over itself.
template <typename SampleT>
[[nodiscard]] an::Status fillPlane(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                   const an::ImageView<SampleT>& image, std::uint32_t plane, double value,
                                   an::NumericDomain domain)
{
    const an::Result<an::ImageLayout> layout = planeLayoutOf(image.layout(), engine.limits());
    if (!layout.hasValue())
    {
        return layout.status();
    }
    const ProgramPrecision precision = programPrecisionFor(an::kSampleTypeOf<SampleT>);
    an::PointwiseProgramBuilder builder;
    builder.channels(1u)
        .readingSamples(an::kSampleTypeOf<SampleT>)
        .readingDomain(domain)
        .promising(precision.contract)
        .computingIn(precision.precision, precision.justification)
        .append(an::pointwiseBias(value));
    const an::Result<an::PointwiseProgram> program = builder.build();
    if (!program.hasValue())
    {
        return program.status();
    }
    std::byte* start = planeStart(reinterpret_cast<std::byte*>(image.data()), image.layout(), plane);
    const std::array<an::PointwiseImageView, 1u> sources{an::PointwiseImageView{.samples = start, .layout = &*layout}};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        execution, *program, sources, an::PointwiseMutableImageView{.samples = start, .layout = &*layout},
        an::KernelValidityView{});
    return ran.hasValue() ? an::Status::success() : ran.status();
}

/// Writes the luminance of the first three planes of a planar image into the one plane of another, with the Rec. 601
/// weights 0.2989, 0.5870 and 0.1140.
template <typename SampleT>
[[nodiscard]] an::Status writeLuminance(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                        const an::ImageView<const SampleT>& colour, const an::ImageView<SampleT>& grey,
                                        an::NumericDomain domain)
{
    const an::Result<an::ImageLayout> colourPlane = planeLayoutOf(colour.layout(), engine.limits());
    const an::Result<an::ImageLayout> greyPlane = planeLayoutOf(grey.layout(), engine.limits());
    if (!colourPlane.hasValue() || !greyPlane.hasValue())
    {
        return colourPlane.hasValue() ? greyPlane.status() : colourPlane.status();
    }
    const ProgramPrecision precision = programPrecisionFor(an::kSampleTypeOf<SampleT>);
    an::PointwiseProgramBuilder builder;
    builder.channels(1u)
        .sources(3u)
        .readingSamples(an::kSampleTypeOf<SampleT>)
        .readingDomain(domain)
        .promising(precision.contract)
        .computingIn(precision.precision, precision.justification)
        .append(an::pointwiseScale(0.2989))
        .append(an::pointwiseAdd(1u, 0.5870))
        .append(an::pointwiseAdd(2u, 0.1140));
    const an::Result<an::PointwiseProgram> program = builder.build();
    if (!program.hasValue())
    {
        return program.status();
    }
    const auto* colourBytes = reinterpret_cast<const std::byte*>(colour.data());
    const std::array<an::PointwiseImageView, 3u> sources{
        an::PointwiseImageView{.samples = planeStart(colourBytes, colour.layout(), 0u), .layout = &*colourPlane},
        an::PointwiseImageView{.samples = planeStart(colourBytes, colour.layout(), 1u), .layout = &*colourPlane},
        an::PointwiseImageView{.samples = planeStart(colourBytes, colour.layout(), 2u), .layout = &*colourPlane}};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        execution, *program, sources, an::PointwiseMutableImageView{.samples = grey.data(), .layout = &*greyPlane},
        an::KernelValidityView{});
    return ran.hasValue() ? an::Status::success() : ran.status();
}

/// The image's own planes rearranged to the base's channel count, in the image's representation: copied, replicated
/// from grey, reduced to luminance, with an opaque alpha added or an alpha dropped.
template <typename SourceT>
[[nodiscard]] an::Result<an::Image<SourceT>>
rearrangeChannels(const an::EngineContext& engine, const an::ExecutionContext& execution,
                  const an::Image<SourceT>& image, const an::ImageDescriptor& described, an::NumericDomain domain)
{
    an::Result<std::optional<an::Image<SourceT>>> planar =
        planarWorkingCopy<SourceT, SourceT>(engine, execution, image);
    if (!planar.hasValue())
    {
        return planar.status();
    }
    const an::Image<SourceT>& planarImage = planar->has_value() ? **planar : image;
    const auto view = planarImage.view();
    const std::uint32_t from = view->channelCount();
    const std::uint32_t to = described.extent().channelCount();

    an::ImageDescriptorRequest request = described.toRequest();
    request.sampleType = an::kSampleTypeOf<SourceT>;
    const std::optional<an::ImageDescriptor> rearrangedDescriptor = an::ImageDescriptor::create(request);
    if (!rearrangedDescriptor.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments,
                       "the base has no description in the image's representation");
    }
    // An alpha the image has not is raised from zero, so its plane starts zeroed.
    const an::StorageInitialization initialization =
        to == 4u && from != 4u ? an::StorageInitialization::Zeroed : an::StorageInitialization::Uninitialized;
    an::Result<an::Image<SourceT>> result = an::Image<SourceT>::createDestination(
        engine, *rearrangedDescriptor, an::StorageArrangement::Planar, initialization, image);
    if (!result.hasValue())
    {
        return result.status();
    }
    std::optional<an::ImageWriteAccess<SourceT>> access = result->write();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the base holds nothing");
    }
    const an::ImageView<SourceT>& out = access->view();
    an::Status done = an::Status::success();
    if (from == to)
    {
        std::vector<PlaneMove> moves;
        for (std::uint32_t plane = 0u; plane < to; ++plane)
        {
            moves.push_back(PlaneMove{.from = plane, .to = plane});
        }
        done = copyPlanes<SourceT>(engine, execution, *view, out, moves);
    }
    else if (from == 1u && to >= 3u)
    {
        constexpr std::array<PlaneMove, 3u> kGrey{{{0u, 0u}, {0u, 1u}, {0u, 2u}}};
        done = copyPlanes<SourceT>(engine, execution, *view, out, kGrey);
    }
    else if (from >= 3u && to == 1u)
    {
        done = writeLuminance<SourceT>(engine, execution, *view, out, domain);
    }
    else if (from >= 3u && to >= 3u)
    {
        constexpr std::array<PlaneMove, 3u> kColour{{{0u, 0u}, {1u, 1u}, {2u, 2u}}};
        done = copyPlanes<SourceT>(engine, execution, *view, out, kColour);
    }
    else
    {
        done = refusal(an::ErrorCode::IncompatibleArguments, "the base cannot be made with that many channels");
    }
    if (done.ok() && to == 4u && from != 4u)
    {
        // An alpha the patches have and the image has not is opaque.
        done = fillPlane<SourceT>(engine, execution, out, 3u, fullScaleOf<SourceT>(), domain);
    }
    if (done.failed())
    {
        return done;
    }
    access.reset();
    return std::move(*result);
}

} // namespace

const char* processRunKindName(ProcessRunKind kind) noexcept
{
    switch (kind)
    {
    case ProcessRunKind::Preview:
        return "preview";
    case ProcessRunKind::Apply:
        return "apply";
    case ProcessRunKind::NewImage:
        return "new image";
    case ProcessRunKind::Snapshot:
        return "snapshot";
    case ProcessRunKind::SequenceFrame:
        return "sequence frame";
    }
    return "unknown";
}

const char* previewFrameKindName(PreviewFrameKind kind) noexcept
{
    switch (kind)
    {
    case PreviewFrameKind::Settled:
        return "settled";
    case PreviewFrameKind::Interactive:
        return "interactive";
    }
    return "unknown";
}

const char* previewScalingName(PreviewScaling scaling) noexcept
{
    switch (scaling)
    {
    case PreviewScaling::FullOnly:
        return "full only";
    case PreviewScaling::Decimated:
        return "decimated";
    case PreviewScaling::NativeRegion:
        return "native region";
    }
    return "unknown";
}

// ---- ProcessSource.

ProcessSource::ProcessSource(const an::consumer::AnyImage& image, const an::ValidityMask* validity,
                             an::consumer::AssetRevision revision) noexcept
    : m_image(&image), m_validity(validity), m_revision(revision)
{
}

ProcessSource ProcessSource::of(const an::consumer::AssetReadLease& lease) noexcept
{
    return ProcessSource(lease.image(), lease.validity(), lease.revision());
}

ProcessSource ProcessSource::of(const an::consumer::AssetWriteLease& lease) noexcept
{
    return ProcessSource(lease.image(), lease.validity(), lease.revision());
}

const an::consumer::AnyImage& ProcessSource::image() const noexcept
{
    return *m_image;
}

const an::ValidityMask* ProcessSource::validity() const noexcept
{
    return m_validity;
}

an::consumer::AssetRevision ProcessSource::revision() const noexcept
{
    return m_revision;
}

std::optional<an::ImageDescriptor> ProcessSource::descriptor() const
{
    return descriptorOf(*m_image);
}

const an::ImageMetadata& ProcessSource::metadata() const noexcept
{
    return std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, *m_image);
}

ProcessSource ProcessSource::reusing(ProcessConversions* conversions) const noexcept
{
    ProcessSource reused = *this;
    reused.m_conversions = conversions;
    return reused;
}

ProcessConversions* ProcessSource::conversions() const noexcept
{
    return m_conversions;
}

// ---- The conversions a run reads and makes.

std::uint64_t ProcessConversion::byteCount() const noexcept
{
    std::uint64_t bytes = prepared ? prepared->byteCount() : 0u;
    if (planar)
    {
        bytes += std::visit(
            [](const auto& typed) -> std::uint64_t {
                const auto view = typed.view();
                return view.has_value() ? view->layout().totalBytes() : 0u;
            },
            *planar);
    }
    return bytes;
}

namespace
{

/// Whether a revision is one an asset held before another of its revisions: the same asset, no generation past the
/// other's, and not that revision itself.
[[nodiscard]] bool isOlderRevision(const an::consumer::AssetRevision& revision,
                                   const an::consumer::AssetRevision& than) noexcept
{
    return revision.assetIdentity == than.assetIdentity && revision != than &&
           revision.pixelGeneration <= than.pixelGeneration && revision.semanticGeneration <= than.semanticGeneration &&
           revision.validityGeneration <= than.validityGeneration;
}

/// Whether two scope requests ask for the same scope of the same source encoding in the same units.
[[nodiscard]] bool sameScopeRequest(const an::ChannelScopeRequest& left, const an::ChannelScopeRequest& right)
{
    const an::ColorEncoding& one = left.sourceEncoding;
    const an::ColorEncoding& other = right.sourceEncoding;
    return left.scope == right.scope && left.units == right.units && left.yCbCr == right.yCbCr &&
           one.schema == other.schema && one.primaries == other.primaries && one.transfer == other.transfer &&
           one.transferGamma == other.transferGamma && one.whitePoint == other.whitePoint &&
           one.perceptual == other.perceptual && one.yCbCr == other.yCbCr && one.hue == other.hue;
}

/// Whether a conversion holds anything a run can read.
[[nodiscard]] bool holdsConversion(const ProcessConversion& conversion) noexcept
{
    return conversion.source.assetIdentity != 0u && (conversion.prepared != nullptr || conversion.planar != nullptr);
}

/// Whether two conversions of one revision convert it the same way: two forward halves, or two planar images of one
/// representation.
[[nodiscard]] bool sameKind(const ProcessConversion& left, const ProcessConversion& right)
{
    if (left.prepared && right.prepared)
    {
        return true;
    }
    return left.planar && right.planar &&
           an::consumer::sampleTypeOf(*left.planar) == an::consumer::sampleTypeOf(*right.planar);
}

} // namespace

ProcessConversions::ProcessConversions(const an::EngineContext& engine, std::vector<ProcessConversion> kept)
    : m_engine(&engine), m_kept(std::move(kept))
{
}

const an::EngineContext& ProcessConversions::engine() const noexcept
{
    return *m_engine;
}

std::shared_ptr<const an::ChannelScopePreparation>
ProcessConversions::prepared(const an::consumer::AssetRevision& source, const an::ChannelScopeRequest& request)
{
    const std::lock_guard lock(m_lock);
    for (const ProcessConversion& conversion : m_kept)
    {
        if (conversion.prepared && conversion.source == source &&
            sameScopeRequest(conversion.prepared->request(), request))
        {
            ++m_scopesReused;
            noteReused(conversion);
            return conversion.prepared;
        }
    }
    return nullptr;
}

std::shared_ptr<const an::consumer::AnyImage> ProcessConversions::planar(const an::consumer::AssetRevision& source,
                                                                         an::SampleType sampleType)
{
    const std::lock_guard lock(m_lock);
    for (const ProcessConversion& conversion : m_kept)
    {
        if (conversion.planar && conversion.source == source &&
            an::consumer::sampleTypeOf(*conversion.planar) == sampleType)
        {
            ++m_planarReused;
            noteReused(conversion);
            return conversion.planar;
        }
    }
    return nullptr;
}

void ProcessConversions::noteReused(const ProcessConversion& conversion)
{
    const bool noted = std::any_of(m_reused.begin(), m_reused.end(), [&](const ProcessConversion& reused) {
        return reused.prepared == conversion.prepared && reused.planar == conversion.planar;
    });
    if (!noted)
    {
        m_reused.push_back(conversion);
    }
}

std::vector<ProcessConversion> ProcessConversions::reused() const
{
    const std::lock_guard lock(m_lock);
    return m_reused;
}

void ProcessConversions::keep(ProcessConversion conversion)
{
    if (!holdsConversion(conversion))
    {
        return;
    }
    const std::lock_guard lock(m_lock);
    m_made.push_back(conversion);
    m_kept.push_back(std::move(conversion));
}

std::vector<ProcessConversion> ProcessConversions::made() const
{
    const std::lock_guard lock(m_lock);
    return m_made;
}

std::uint64_t ProcessConversions::scopesPrepared() const
{
    const std::lock_guard lock(m_lock);
    return static_cast<std::uint64_t>(std::count_if(
        m_made.begin(), m_made.end(), [](const ProcessConversion& made) { return made.prepared != nullptr; }));
}

std::uint64_t ProcessConversions::scopesReused() const
{
    const std::lock_guard lock(m_lock);
    return m_scopesReused;
}

std::uint64_t ProcessConversions::planarMade() const
{
    const std::lock_guard lock(m_lock);
    return static_cast<std::uint64_t>(std::count_if(
        m_made.begin(), m_made.end(), [](const ProcessConversion& made) { return made.planar != nullptr; }));
}

std::uint64_t ProcessConversions::planarReused() const
{
    const std::lock_guard lock(m_lock);
    return m_planarReused;
}

ProcessConversionCache::ProcessConversionCache(std::size_t capacity) : m_capacity(capacity) {}

std::vector<ProcessConversion> ProcessConversionCache::kept() const
{
    return m_kept;
}

void ProcessConversionCache::keep(const std::vector<ProcessConversion>& made,
                                  const std::vector<ProcessConversion>& reused)
{
    if (m_capacity == 0u)
    {
        return;
    }
    // What a run read is as recently used as what it made.
    for (const ProcessConversion& read : reused)
    {
        const auto found = std::find_if(m_kept.begin(), m_kept.end(), [&](const ProcessConversion& kept) {
            return kept.prepared == read.prepared && kept.planar == read.planar;
        });
        if (found != m_kept.end())
        {
            std::rotate(found, found + 1, m_kept.end());
        }
    }
    for (const ProcessConversion& conversion : made)
    {
        if (!holdsConversion(conversion))
        {
            continue;
        }
        // A conversion of a revision older than one kept of the same asset is never read again.
        const bool superseded = std::any_of(m_kept.begin(), m_kept.end(), [&](const ProcessConversion& kept) {
            return isOlderRevision(conversion.source, kept.source);
        });
        if (superseded)
        {
            continue;
        }
        // Nor is one of an older revision once this one is kept; one of this revision converted the same way is
        // replaced, and so is another forward half of it, so a change of channel lets the last one go.
        std::erase_if(m_kept, [&](const ProcessConversion& kept) {
            if (kept.source.assetIdentity != conversion.source.assetIdentity)
            {
                return false;
            }
            if (kept.source != conversion.source)
            {
                return isOlderRevision(kept.source, conversion.source);
            }
            return sameKind(kept, conversion);
        });
        m_kept.push_back(conversion);
        if (m_kept.size() > m_capacity)
        {
            m_kept.erase(m_kept.begin(), m_kept.begin() + static_cast<std::ptrdiff_t>(m_kept.size() - m_capacity));
        }
    }
}

std::size_t ProcessConversionCache::size() const noexcept
{
    return m_kept.size();
}

std::uint64_t ProcessConversionCache::byteCount() const noexcept
{
    std::uint64_t bytes = 0u;
    for (const ProcessConversion& kept : m_kept)
    {
        bytes += kept.byteCount();
    }
    return bytes;
}

void ProcessConversionCache::clear() noexcept
{
    m_kept.clear();
}

// ---- ProcessDestination.

ProcessDestination::ProcessDestination() noexcept = default;

ProcessDestination::~ProcessDestination()
{
    m_lease.release();
}

ProcessDestination::ProcessDestination(ProcessDestination&& other) noexcept = default;

ProcessDestination& ProcessDestination::operator=(ProcessDestination&& other) noexcept
{
    if (this != &other)
    {
        m_lease.release();
        m_lease = std::move(other.m_lease);
        m_asset = std::move(other.m_asset);
        m_holdsSource = other.m_holdsSource;
    }
    return *this;
}

ProcessDestination::ProcessDestination(an::consumer::ImageAsset&& asset, an::consumer::AssetWriteLease&& lease,
                                       bool holdsSource) noexcept
    : m_asset(std::move(asset)), m_lease(std::move(lease)), m_holdsSource(holdsSource)
{
}

namespace
{

/// An image adopted as a new asset of the run's session with a clone of the source's mask, under its write lease.
[[nodiscard]] an::Result<std::pair<an::consumer::ImageAsset, an::consumer::AssetWriteLease>>
adoptDestination(const an::consumer::EngineSession& session, an::consumer::AnyImage&& image,
                 const ProcessSource& source)
{
    std::optional<an::ValidityMask> mask;
    if (source.validity() != nullptr)
    {
        an::Result<an::ValidityMask> cloned = source.validity()->clone();
        if (!cloned.hasValue())
        {
            return cloned.status();
        }
        mask.emplace(std::move(*cloned));
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, std::move(image), std::move(mask));
    if (!asset.hasValue())
    {
        return asset.status();
    }
    // Nothing else knows of the asset yet, so its writer is free.
    an::Result<an::consumer::AssetWriteLease> lease = asset->tryWrite();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    return std::pair<an::consumer::ImageAsset, an::consumer::AssetWriteLease>(std::move(*asset), std::move(*lease));
}

/// Whether every helper reads a source where it lies: planar, under a rule that calls every sample valid, with no mask.
[[nodiscard]] bool readsWhereItLies(const ProcessSource& source)
{
    if (source.validity() != nullptr || source.metadata().semantics().validity().rule() != an::ValidityRule::AllValid)
    {
        return false;
    }
    return std::visit(
        [](const auto& typed) {
            const auto view = typed.view();
            return view.has_value() && view->layout().samplesPerPixel() == 1u;
        },
        source.image());
}

} // namespace

an::Result<ProcessDestination> ProcessDestination::copyOf(const an::consumer::EngineSession& session,
                                                          const an::EngineContext& engine,
                                                          const an::ExecutionContext& execution,
                                                          const ProcessSource& source)
{
    an::Result<an::consumer::AnyImage> copied = std::visit(
        [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            using SampleT = SampleOf<decltype(typed)>;
            const std::optional<an::ImageDescriptor> described = typed.descriptor();
            const auto view = typed.view();
            if (!described.has_value() || !view.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the source holds nothing");
            }
            an::Result<an::Image<SampleT>> copy = an::Image<SampleT>::createDestination(
                engine, *described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, typed);
            if (!copy.hasValue())
            {
                return copy.status();
            }
            {
                std::optional<an::ImageWriteAccess<SampleT>> access = copy->write();
                if (!access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the destination holds nothing");
                }
                const an::Status moved =
                    an::repackImageSamples(execution, an::kSampleTypeOf<SampleT>, view->data(), view->layout(),
                                           access->view().data(), access->view().layout());
                if (moved.failed())
                {
                    return moved;
                }
            }
            return an::consumer::AnyImage(std::move(*copy));
        },
        source.image());
    if (!copied.hasValue())
    {
        return copied.status();
    }
    an::Result<std::pair<an::consumer::ImageAsset, an::consumer::AssetWriteLease>> adopted =
        adoptDestination(session, std::move(*copied), source);
    if (!adopted.hasValue())
    {
        return adopted.status();
    }
    return ProcessDestination(std::move(adopted->first), std::move(adopted->second), true);
}

an::Result<ProcessDestination> ProcessDestination::unwrittenFor(const an::consumer::EngineSession& session,
                                                                const an::EngineContext& engine,
                                                                const ProcessSource& source)
{
    an::Result<an::consumer::AnyImage> made = std::visit(
        [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            using SampleT = SampleOf<decltype(typed)>;
            const std::optional<an::ImageDescriptor> described = typed.descriptor();
            if (!described.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the source holds nothing");
            }
            an::Result<an::Image<SampleT>> image = an::Image<SampleT>::createDestination(
                engine, *described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, typed);
            if (!image.hasValue())
            {
                return image.status();
            }
            return an::consumer::AnyImage(std::move(*image));
        },
        source.image());
    if (!made.hasValue())
    {
        return made.status();
    }
    an::Result<std::pair<an::consumer::ImageAsset, an::consumer::AssetWriteLease>> adopted =
        adoptDestination(session, std::move(*made), source);
    if (!adopted.hasValue())
    {
        return adopted.status();
    }
    return ProcessDestination(std::move(adopted->first), std::move(adopted->second), false);
}

an::Result<ProcessDestination> ProcessDestination::forBody(const an::consumer::EngineSession& session,
                                                           const an::EngineContext& engine,
                                                           const an::ExecutionContext& execution,
                                                           const ProcessSource& source, ProcessBodyCoverage coverage)
{
    if (coverage == ProcessBodyCoverage::WritesEverySample && readsWhereItLies(source))
    {
        return unwrittenFor(session, engine, source);
    }
    return copyOf(session, engine, execution, source);
}

bool ProcessDestination::valid() const noexcept
{
    return m_asset.valid() && m_lease.valid();
}

bool ProcessDestination::holdsSource() const noexcept
{
    return m_holdsSource;
}

an::Status ProcessDestination::holdSource(const an::ExecutionContext& execution, const ProcessSource& source)
{
    if (!valid())
    {
        return refusal(an::ErrorCode::NotInitialized, "the run has no destination");
    }
    if (m_holdsSource)
    {
        return an::Status::success();
    }
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value() || descriptorOf(m_lease.image()) != described)
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the destination is no longer described as the source");
    }
    const an::Status copied = std::visit(
        [&](const auto& typed) -> an::Status {
            using SampleT = SampleOf<decltype(typed)>;
            const auto view = typed.view();
            std::optional<an::ImageWriteAccess<SampleT>> access = m_lease.write<SampleT>();
            if (!view.has_value() || !access.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the source or the destination holds nothing");
            }
            return an::repackImageSamples(execution, an::kSampleTypeOf<SampleT>, view->data(), view->layout(),
                                          access->view().data(), access->view().layout());
        },
        source.image());
    if (copied.failed())
    {
        return copied;
    }
    m_holdsSource = true;
    return an::Status::success();
}

an::consumer::AssetWriteLease& ProcessDestination::asset() noexcept
{
    return m_lease;
}

const an::consumer::AssetWriteLease& ProcessDestination::asset() const noexcept
{
    return m_lease;
}

an::Status ProcessDestination::replace(an::consumer::ImageAsset&& replacement)
{
    if (!valid() || !replacement.valid())
    {
        return refusal(an::ErrorCode::NotInitialized, "the destination or its replacement holds nothing");
    }
    an::Result<an::consumer::AssetWriteLease> lease = replacement.tryWrite();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    m_lease.release();
    m_lease = std::move(*lease);
    m_asset = std::move(replacement);
    // A replacement is the body's result, every sample of it.
    m_holdsSource = false;
    return an::Status::success();
}

an::Result<an::consumer::ImageAsset> ProcessDestination::release()
{
    if (!valid())
    {
        return refusal(an::ErrorCode::NotInitialized, "the destination holds nothing");
    }
    m_lease.release();
    return std::move(m_asset);
}

// ---- The channel selector.

std::span<const ProcessChannelChoice> processChannelChoices() noexcept
{
    return kChannelChoices;
}

std::optional<an::ChannelScope> processChannelScopeForLabel(const QString& label)
{
    if (label == QLatin1String(kProcessGreyscaleChannelLabel))
    {
        return an::ChannelScope::Whole;
    }
    for (const ProcessChannelChoice& choice : kChannelChoices)
    {
        if (label == QLatin1String(choice.label))
        {
            return choice.scope;
        }
    }
    return std::nullopt;
}

QString processChannelLabel(an::ChannelScope scope)
{
    for (const ProcessChannelChoice& choice : kChannelChoices)
    {
        if (choice.scope == scope)
        {
            return QString::fromLatin1(choice.label);
        }
    }
    return {};
}

an::ChannelScope effectiveProcessChannel(an::ChannelScope choice, const an::ImageDescriptor& source) noexcept
{
    return isColour(source) && an::isChannelScope(choice) ? choice : an::ChannelScope::Whole;
}

an::ChannelScopeRequest processChannelScopeRequest(an::ChannelScope scope, const an::ImageDescriptor& source) noexcept
{
    an::ColorEncoding encoding{};
    encoding.schema = an::ChannelSchema::Rgb;
    encoding.transfer = source.colorSpace() == an::ColorSpace::LinearRgb ? an::ColorTransferFunction::Linear
                                                                         : an::ColorTransferFunction::Srgb;
    return an::ChannelScopeRequest{
        .scope = scope, .sourceEncoding = encoding, .units = an::ChannelScopeUnits::UnitInterval};
}

an::Result<an::ChannelScopeResult> applyProcessChannelScope(const an::ExecutionContext& execution,
                                                            an::ChannelScope scope, const ProcessSource& source,
                                                            ProcessDestination& destination,
                                                            an::ChannelScopeOperation& operation)
{
    if (!destination.valid())
    {
        return refusal(an::ErrorCode::NotInitialized, "the run has no destination");
    }
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    const std::optional<an::ImageDescriptor> destinationDescribed = descriptorOf(destination.asset().image());
    if (!described.has_value() || !destinationDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the source or the destination holds nothing");
    }
    if (*destinationDescribed != *described)
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the destination is no longer described as the source");
    }
    const std::optional<an::ImageDescriptor> component = componentReadingOf(*described);
    if (!component.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments,
                       "the image has no description as fractions of full scale to take a component from");
    }
    const an::Result<an::ChannelScopePlan> plan =
        an::planChannelScope(processChannelScopeRequest(effectiveProcessChannel(scope, *component), *component));
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const std::optional<an::ValidityMaskView<const std::uint8_t>> sourceMask = maskViewOf(source.validity());
    return std::visit(
        [&](const auto& typed) -> an::Result<an::ChannelScopeResult> {
            using SampleT = SampleOf<decltype(typed)>;
            const auto view = typed.view();
            if (!view.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the source holds nothing");
            }
            // The scope borrows the layout for the call, so the one it is given outlives it.
            const an::ImageLayout layout = view->layout();
            an::ChannelScopeSource scoped;
            scoped.descriptor = &*component;
            scoped.samples = view->data();
            scoped.layout = &layout;
            scoped.validity.descriptor = typed.metadata().semantics().validity();
            scoped.validity.mask = sourceMask.has_value() ? &*sourceMask : nullptr;
            // A component of another encoding is converted once per revision of what the run reads: the forward half
            // an earlier run kept, or the one made now and kept for the runs that follow.
            std::shared_ptr<const an::ChannelScopePreparation> prepared;
            ProcessConversions* const conversions = source.conversions();
            if (conversions != nullptr && plan->forward().has_value() && source.revision().assetIdentity != 0u)
            {
                prepared = conversions->prepared(source.revision(), plan->request());
                if (!prepared)
                {
                    an::Result<an::ChannelScopePreparation> made =
                        an::prepareChannelScope(conversions->engine(), execution, *plan, scoped);
                    if (made.hasValue())
                    {
                        prepared = std::make_shared<const an::ChannelScopePreparation>(std::move(*made));
                        conversions->keep(ProcessConversion{.source = source.revision(), .prepared = prepared});
                    }
                    else if (made.status().code() != an::ErrorCode::MemoryBudgetExceeded &&
                             made.status().code() != an::ErrorCode::AllocationFailed)
                    {
                        return made.status();
                    }
                    // Planes the memory refuses are converted in the call, as a run that keeps nothing converts them.
                }
            }
            scoped.prepared = prepared.get();
            std::optional<an::ImageWriteAccess<SampleT>> access = destination.asset().write<SampleT>();
            if (!access.has_value())
            {
                return refusal(an::ErrorCode::IncompatibleArguments, "the destination holds another representation");
            }
            const an::ImageLayout destinationLayout = access->view().layout();
            return an::applyChannelScope(execution, *plan, scoped, operation, access->view().data(), destinationLayout,
                                         destination.holdsSource() ? an::ChannelScopeDestination::HoldsSource
                                                                   : an::ChannelScopeDestination::Unwritten);
        },
        source.image());
}

an::Status blendProcessResult(const an::EngineContext& engine, const an::ExecutionContext& execution,
                              const ProcessSource& source, ProcessDestination& destination,
                              const ProcessBlendRequest& request)
{
    if (!(request.modulation >= 0.0 && request.modulation <= 1.0))
    {
        return refusal(an::ErrorCode::OutOfRange, "the modulation lies outside the unit interval");
    }
    if (request.modulation == 1.0 && request.mask == nullptr)
    {
        return an::Status::success();
    }
    if (!destination.valid())
    {
        return refusal(an::ErrorCode::NotInitialized, "the run has no destination");
    }
    return std::visit(
        [&](const auto& typed) -> an::Status {
            using DestT = SampleOf<decltype(typed)>;
            return blendTyped<DestT>(engine, execution, source, destination.asset(), request);
        },
        destination.asset().image());
}

an::Result<an::consumer::ImageAsset> cropProcessRegion(const an::consumer::EngineSession& session,
                                                       const an::EngineContext& engine,
                                                       const an::ExecutionContext& execution,
                                                       const an::consumer::AnyImage& image,
                                                       const an::ValidityMask* validity, const QRect& region)
{
    if (region.isEmpty() || region.x() < 0 || region.y() < 0)
    {
        return refusal(an::ErrorCode::InvalidArgument, "the preview area is empty or begins outside the image");
    }
    const an::CropRequest crop{.x = static_cast<std::uint32_t>(region.x()),
                               .y = static_cast<std::uint32_t>(region.y()),
                               .width = static_cast<std::uint32_t>(region.width()),
                               .height = static_cast<std::uint32_t>(region.height()),
                               .bounds = an::CropBoundsPolicy::Reject};
    an::Result<std::pair<an::consumer::AnyImage, std::optional<an::ValidityMask>>> cropped =
        std::visit([&](const auto& typed) { return cropTyped(engine, execution, typed, validity, crop); }, image);
    if (!cropped.hasValue())
    {
        return cropped.status();
    }
    return an::consumer::ImageAsset::adopt(session, std::move(cropped->first), std::move(cropped->second));
}

std::optional<an::ImageDescriptor> processCompositeBaseDescriptor(const an::ImageDescriptor& patch,
                                                                  const an::ImageDescriptor& image,
                                                                  const an::EngineLimits& limits) noexcept
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(image.extent().width(), image.extent().height(), patch.extent().channelCount(), limits);
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    an::ImageDescriptorRequest request = patch.toRequest();
    request.extent = *extent;
    return an::ImageDescriptor::create(request);
}

an::Result<an::consumer::ImageAsset>
buildProcessCompositeBase(const an::consumer::EngineSession& session, const an::EngineContext& engine,
                          const an::ExecutionContext& execution, const an::consumer::AnyImage& image,
                          const an::ValidityMask* validity, const an::ImageDescriptor& described)
{
    const std::optional<an::ImageDescriptor> imageDescriptor = descriptorOf(image);
    if (!imageDescriptor.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    if (imageDescriptor->extent().width() != described.extent().width() ||
        imageDescriptor->extent().height() != described.extent().height())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the base has another size than the image");
    }
    an::Result<an::consumer::AnyImage> base = std::visit(
        [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            using SourceT = SampleOf<decltype(typed)>;
            // The channels first, in the image's own representation; then the representation, at full scale.
            an::Result<an::Image<SourceT>> rearranged =
                rearrangeChannels<SourceT>(engine, execution, typed, described, described.numericDomain());
            if (!rearranged.hasValue())
            {
                return rearranged.status();
            }
            an::Result<an::consumer::AnyImage> converted =
                refusal(an::ErrorCode::Unsupported, "the base's representation is not one of the eight");
            const bool dispatched = an::dispatchSampleType(described.sampleType(), [&]<typename DestT>() {
                if constexpr (std::is_same_v<DestT, SourceT>)
                {
                    converted = an::consumer::AnyImage(std::move(*rearranged));
                }
                else
                {
                    an::Result<std::optional<an::Image<DestT>>> working =
                        planarWorkingCopy<DestT, SourceT>(engine, execution, *rearranged);
                    if (!working.hasValue())
                    {
                        converted = working.status();
                        return;
                    }
                    converted = an::consumer::AnyImage(std::move(**working));
                }
            });
            if (!dispatched)
            {
                return refusal(an::ErrorCode::Unsupported, "the base's representation is not one of the eight");
            }
            return converted;
        },
        image);
    if (!base.hasValue())
    {
        return base.status();
    }
    std::optional<an::ValidityMask> mask;
    if (validity != nullptr)
    {
        an::Result<an::ValidityMask> cloned = validity->clone();
        if (!cloned.hasValue())
        {
            return cloned.status();
        }
        mask.emplace(std::move(*cloned));
    }
    return an::consumer::ImageAsset::adopt(session, std::move(*base), std::move(mask));
}

an::Result<an::consumer::ImageAsset>
composeProcessRegion(const an::consumer::EngineSession& session, const an::EngineContext& engine,
                     const an::ExecutionContext& execution, const an::consumer::AnyImage& base,
                     const an::consumer::AnyImage& patch, const an::ValidityMask* patchValidity, QPoint origin)
{
    if (base.index() != patch.index())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the patch has another representation than the base");
    }
    an::Result<an::consumer::AnyImage> composite = std::visit(
        [&](const auto& typedBase) -> an::Result<an::consumer::AnyImage> {
            using SampleT = SampleOf<decltype(typedBase)>;
            const an::Image<SampleT>& typedPatch = std::get<an::Image<SampleT>>(patch);
            const std::optional<an::ImageDescriptor> patchDescriptor = typedPatch.descriptor();
            const auto patchView = typedPatch.view();
            const std::optional<an::ImageDescriptor> baseDescriptor = typedBase.descriptor();
            if (!patchDescriptor.has_value() || !patchView.has_value() || !baseDescriptor.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the base or the patch holds nothing");
            }
            const an::ImageExtent& patchExtent = patchDescriptor->extent();
            const an::ImageExtent& baseExtent = baseDescriptor->extent();
            if (patchExtent.channelCount() != baseExtent.channelCount())
            {
                return refusal(an::ErrorCode::IncompatibleArguments,
                               "the patch has another channel count than the base");
            }
            if (origin.x() < 0 || origin.y() < 0 ||
                static_cast<std::uint64_t>(origin.x()) + patchExtent.width() > baseExtent.width() ||
                static_cast<std::uint64_t>(origin.y()) + patchExtent.height() > baseExtent.height())
            {
                return refusal(an::ErrorCode::IncompatibleArguments, "the patch does not lie inside the base");
            }
            an::Result<an::Image<SampleT>> copy = typedBase.clone();
            if (!copy.hasValue())
            {
                return copy.status();
            }
            {
                std::optional<an::ImageWriteAccess<SampleT>> access = copy->write();
                if (!access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the composite holds nothing");
                }
                const an::ImageLayout& baseLayout = access->view().layout();
                // The paste is the whole patch cropped into a window of the copy: the patch's extent at the copy's own
                // strides, starting at the origin.
                const an::Result<an::GeometryPlan> plan =
                    an::planCrop(*patchDescriptor, an::CropRequest{.x = 0u,
                                                                   .y = 0u,
                                                                   .width = patchExtent.width(),
                                                                   .height = patchExtent.height(),
                                                                   .bounds = an::CropBoundsPolicy::Reject});
                if (!plan.hasValue())
                {
                    return plan.status();
                }
                const std::optional<an::ImageLayout> window = an::ImageLayout::createWithStrides(
                    an::ImageLayoutRequest{.extent = patchExtent,
                                           .elementSizeBytes = baseLayout.elementSizeBytes(),
                                           .samplesPerPixel = baseLayout.samplesPerPixel(),
                                           .planeCount = baseLayout.planeCount(),
                                           .rowAlignmentBytes = 1u},
                    baseLayout.rowStrideBytes(), baseLayout.planeStrideBytes(), engine.limits());
                const std::optional<std::uint64_t> windowOffset =
                    baseLayout.rowByteOffset(0u, static_cast<std::uint32_t>(origin.y()));
                if (!window.has_value() || !windowOffset.has_value())
                {
                    return refusal(an::ErrorCode::IncompatibleArguments, "the patch's window cannot be laid out");
                }
                auto* windowStart = reinterpret_cast<std::byte*>(access->view().data()) + *windowOffset +
                                    static_cast<std::uint64_t>(origin.x()) * baseLayout.samplesPerPixel() *
                                        baseLayout.elementSizeBytes();
                an::Result<an::ValidityMask> written = an::ValidityMask::create(engine, patchExtent);
                if (!written.hasValue())
                {
                    return written.status();
                }
                std::optional<an::ValidityMaskView<std::uint8_t>> writtenView = written->view();
                const std::optional<an::ValidityMaskView<const std::uint8_t>> patchMask = maskViewOf(patchValidity);
                an::GeometrySelection selection;
                selection.validity = typedPatch.metadata().semantics().validity();
                selection.mask = patchMask.has_value() ? &*patchMask : nullptr;
                const an::Result<an::GeometryResult> pasted = an::transformImageGeometry(
                    execution, an::kSampleTypeOf<SampleT>, patchView->data(), patchView->layout(), windowStart, *window,
                    *plan, selection, *writtenView);
                if (!pasted.hasValue())
                {
                    return pasted.status();
                }
            }
            return an::consumer::AnyImage(std::move(*copy));
        },
        base);
    if (!composite.hasValue())
    {
        return composite.status();
    }
    return an::consumer::ImageAsset::adopt(session, std::move(*composite));
}

} // namespace an::ui
