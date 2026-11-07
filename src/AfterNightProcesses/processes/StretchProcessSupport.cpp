// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The stretch dialogs' working image, their published result and its metadata, and the whole-image statistics a
/// screen transfer is derived from.

#include "stdafx.hpp"

#include "processes/StretchProcessSupport.h"

#include "widgets/HistogramComputation.h"

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

namespace an::ui::processes
{

namespace
{

/// A failure the stretch dialogs decided, which no engine call produced.
[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// The representation an image of a variant alternative holds.
template <typename ImageT> using SampleOf = typename std::remove_cvref_t<ImageT>::Sample;

/// Whether a representation is a signed integer, which is read from its smallest value to its largest.
[[nodiscard]] constexpr bool isSignedInteger(an::SampleType type) noexcept
{
    return type == an::SampleType::Int8 || type == an::SampleType::Int16 || type == an::SampleType::Int32;
}

/// A signed integer's smallest value and the width of its range, from its smallest value to its largest.
struct SignedRange
{
    double smallest = 0.0;
    double width = 1.0;
};

[[nodiscard]] constexpr SignedRange signedRangeOf(an::SampleType type) noexcept
{
    switch (type)
    {
    case an::SampleType::Int8:
        return SignedRange{.smallest = -128.0, .width = 255.0};
    case an::SampleType::Int16:
        return SignedRange{.smallest = -32768.0, .width = 65535.0};
    case an::SampleType::Int32:
        return SignedRange{.smallest = -2147483648.0, .width = 4294967295.0};
    default:
        return SignedRange{};
    }
}

/// Runs a scale and a bias from working samples into working samples, every sample of every channel; the two may be
/// the same samples.
[[nodiscard]] an::Status scaleSamples(const an::ExecutionContext& execution, an::SampleType working, const void* from,
                                      const an::ImageLayout& fromLayout, void* to, const an::ImageLayout& toLayout,
                                      double scale, double bias)
{
    an::PointwisePlanShape shape;
    shape.sampleType = working;
    shape.numericDomain = an::NumericDomain::NormalizedLinear;
    shape.channelCount = fromLayout.extent().channelCount();
    if (working == an::SampleType::Float64)
    {
        shape.precision = an::ComputePrecision::Float64;
        shape.precisionJustification = an::Float64Justification::StoredInFloat64;
    }
    const an::Result<an::PointwiseProgram> program = an::buildScaleBiasProgram(shape, scale, bias);
    if (!program.hasValue())
    {
        return program.status();
    }
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = from, .layout = &fromLayout}};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        execution, *program, sources, an::PointwiseMutableImageView{.samples = to, .layout = &toLayout},
        an::KernelValidityView{});
    return ran.hasValue() ? an::Status::success() : ran.status();
}

/// The working copy of a typed image, in `WorkT`.
template <typename WorkT, typename SampleT>
[[nodiscard]] an::Result<an::consumer::AnyImage>
workingCopyAs(const an::EngineContext& engine, const an::ExecutionContext& execution, const an::Image<SampleT>& image)
{
    const std::optional<an::ImageDescriptor> descriptor = image.descriptor();
    const auto view = image.view();
    if (!descriptor.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    // The working samples are fractions of full scale, whatever the image's own samples count.
    an::ImageDescriptorRequest request = descriptor->toRequest();
    request.sampleType = an::Image<WorkT>::kSampleType;
    request.numericDomain = an::NumericDomain::NormalizedLinear;
    const std::optional<an::ImageDescriptor> working = an::ImageDescriptor::create(request);
    if (!working.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the image has no description as fractions of full scale");
    }
    an::Result<an::Image<WorkT>> copy = an::Image<WorkT>::createDestination(
        engine, *working, an::storageArrangementOf(view->layout()), an::StorageInitialization::Uninitialized, image);
    if (!copy.hasValue())
    {
        return copy.status();
    }
    {
        const std::optional<an::ImageWriteAccess<WorkT>> access = copy->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the working copy holds nothing");
        }
        const an::Status converted =
            readAsFractions(execution, an::Image<SampleT>::kSampleType, view->data(), view->layout(),
                            an::Image<WorkT>::kSampleType, access->view().data(), access->view().layout());
        if (converted.failed())
        {
            return converted;
        }
    }
    return an::consumer::AnyImage(std::move(*copy));
}

/// The result image in the source's representation, made from a working image or a rectangle of it.
template <typename ResultT>
[[nodiscard]] an::Result<an::consumer::AnyImage>
resultFrom(const an::EngineContext& engine, const an::ExecutionContext& execution, const an::ImageDescriptor& described,
           an::ImageMetadata metadata, const an::consumer::AnyImage& working, const QRect& from)
{
    return std::visit(
        [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            using WorkT = SampleOf<decltype(typed)>;
            const auto whole = typed.view();
            if (!whole.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the stretched image holds nothing");
            }
            std::optional<an::ImageView<const WorkT>> read = whole;
            if (!from.isEmpty())
            {
                if (from.x() < 0 || from.y() < 0)
                {
                    return refusal(an::ErrorCode::IncompatibleArguments,
                                   "the rectangle of the stretched image begins outside it");
                }
                read =
                    whole->subview(static_cast<std::uint32_t>(from.x()), static_cast<std::uint32_t>(from.y()),
                                   static_cast<std::uint32_t>(from.width()), static_cast<std::uint32_t>(from.height()));
                if (!read.has_value())
                {
                    return refusal(an::ErrorCode::IncompatibleArguments,
                                   "the rectangle does not lie inside the stretched image");
                }
            }
            if (read->width() != described.extent().width() || read->height() != described.extent().height() ||
                read->channelCount() != described.extent().channelCount())
            {
                return refusal(an::ErrorCode::IncompatibleArguments,
                               "the stretched samples are not of the source's size");
            }
            an::Result<an::Image<ResultT>> result =
                an::Image<ResultT>::create(engine, described, an::storageArrangementOf(read->layout()),
                                           an::StorageInitialization::Uninitialized, std::move(metadata));
            if (!result.hasValue())
            {
                return result.status();
            }
            {
                const std::optional<an::ImageWriteAccess<ResultT>> access = result->write();
                if (!access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the result holds nothing");
                }
                // A result already in the image's representation is copied as it is; fractions are written back.
                an::ConversionCounters counters;
                const an::Status converted =
                    std::is_floating_point_v<WorkT>
                        ? writeFromFractions(engine, execution, described, an::Image<WorkT>::kSampleType, read->data(),
                                             read->layout(), access->view().data(), access->view().layout())
                        : an::convertImageSamples(
                              execution, an::Image<WorkT>::kSampleType, read->data(), read->layout(),
                              an::Image<ResultT>::kSampleType, access->view().data(), access->view().layout(),
                              an::ConversionPolicy{.domain = an::DomainMapping::CodeValue}, counters);
                if (converted.failed())
                {
                    return converted;
                }
            }
            return an::consumer::AnyImage(std::move(*result));
        },
        working);
}

/// The robust statistics of every channel of a floating image, as they are.
template <typename SampleT>
[[nodiscard]] an::Result<ScreenTransferStatistics>
measureFloating(const an::ExecutionContext& execution, const an::Image<SampleT>& image, const an::ValidityMask* mask)
{
    const auto view = image.view();
    if (!view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
    const an::KernelValidityView validity = stretchValidity(image.metadata(), mask, maskView);
    const an::StatisticsSelection selection{.validity = validity.descriptor, .mask = validity.mask};
    an::RobustStatisticsRequest request;
    request.medianAbsoluteDeviation = true;
    ScreenTransferStatistics statistics;
    for (std::uint32_t channel = 0u; channel < view->channelCount(); ++channel)
    {
        const an::Result<an::RobustStatistics> robust = an::computeImageRobustStatistics(
            execution, an::Image<SampleT>::kSampleType, view->data(), view->layout(), channel, selection, request);
        if (!robust.hasValue())
        {
            return robust.status();
        }
        statistics.channels.push_back(an::StretchChannelStatistics{
            .median = robust->median, .medianAbsoluteDeviation = robust->medianAbsoluteDeviation});
    }
    return statistics;
}

} // namespace

an::KernelValidityView stretchValidity(const an::ImageMetadata& metadata, const an::ValidityMask* mask,
                                       std::optional<an::ValidityMaskView<const std::uint8_t>>& maskView)
{
    an::KernelValidityView validity;
    validity.descriptor = metadata.semantics().validity();
    maskView.reset();
    if (mask != nullptr && validity.descriptor.rule() == an::ValidityRule::ExplicitMask)
    {
        maskView = mask->view();
        validity.mask = maskView.has_value() ? &*maskView : nullptr;
    }
    return validity;
}

an::Result<an::consumer::AnyImage> makeStretchWorkingCopy(const an::EngineContext& engine,
                                                          const an::ExecutionContext& execution,
                                                          const an::consumer::AnyImage& image)
{
    return std::visit(
        [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            using SampleT = SampleOf<decltype(typed)>;
            if constexpr (std::is_same_v<SampleT, double> || std::is_same_v<SampleT, std::uint32_t> ||
                          std::is_same_v<SampleT, std::int32_t>)
            {
                return workingCopyAs<double>(engine, execution, typed);
            }
            else
            {
                return workingCopyAs<float>(engine, execution, typed);
            }
        },
        image);
}

namespace
{

/// The channels of an image of `channels` channels that a stretch request does not change.
[[nodiscard]] std::vector<std::uint32_t> channelsLeftAlone(const an::StretchImageRequest& request,
                                                           std::uint32_t channels)
{
    std::vector<std::uint32_t> left;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const bool changed = request.colorMode == an::StretchColorMode::Independent
                                 ? channel < request.channelEnabled.size() && request.channelEnabled[channel]
                                 : channel < 3u;
        if (!changed)
        {
            left.push_back(channel);
        }
    }
    return left;
}

/// Where one channel's samples of a row of a view start, and the step between them, whatever the view's arrangement.
template <typename SampleT>
[[nodiscard]] std::optional<std::pair<SampleT*, std::uint32_t>> channelRowOf(const an::ImageView<SampleT>& view,
                                                                             std::uint32_t channel, std::uint32_t y)
{
    const std::uint32_t perPixel = view.samplesPerPixel();
    const std::optional<std::span<SampleT>> row = view.row(channel / perPixel, y);
    if (!row.has_value())
    {
        return std::nullopt;
    }
    return std::pair<SampleT*, std::uint32_t>{row->data() + channel % perPixel, perPixel};
}

/// The description of one channel held aside, in the working image's representation.
template <typename SampleT>
[[nodiscard]] std::optional<an::ImageDescriptor> heldChannelDescriptor(const an::EngineContext& engine,
                                                                       const an::ImageView<SampleT>& view)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(view.width(), view.height(), 1u, engine.limits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    return an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                                  .sampleType = an::Image<SampleT>::kSampleType,
                                                                  .channelSchema = an::ChannelSchema::Mono,
                                                                  .colorSpace = an::ColorSpace::Unspecified,
                                                                  .numericDomain = an::NumericDomain::NormalizedLinear,
                                                                  .cfa = an::CfaDescriptor{},
                                                                  .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                                  .pixelAspectRatio = 1.0});
}

template <typename SampleT>
[[nodiscard]] an::Status stretchKeeping(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                        const an::ImageView<SampleT>& view, const an::StretchImageRequest& request,
                                        const an::KernelValidityView& validity)
{
    const std::vector<std::uint32_t> left = channelsLeftAlone(request, view.channelCount());
    std::vector<an::Image<SampleT>> held;
    held.reserve(left.size());
    if (!left.empty())
    {
        const std::optional<an::ImageDescriptor> described = heldChannelDescriptor(engine, view);
        if (!described.has_value())
        {
            return refusal(an::ErrorCode::InvalidArgument, "the channels a stretch leaves alone cannot be held");
        }
        for (const std::uint32_t channel : left)
        {
            an::Result<an::Image<SampleT>> plane = an::Image<SampleT>::create(
                engine, *described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized);
            if (!plane.hasValue())
            {
                return plane.status();
            }
            {
                const std::optional<an::ImageWriteAccess<SampleT>> access = plane->write();
                if (!access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "a held channel holds nothing");
                }
                for (std::uint32_t y = 0u; y < view.height(); ++y)
                {
                    const auto from = channelRowOf(view, channel, y);
                    const std::optional<std::span<SampleT>> to = access->view().row(0u, y);
                    if (!from.has_value() || !to.has_value())
                    {
                        return refusal(an::ErrorCode::Internal, "a channel's row is out of the image");
                    }
                    for (std::uint32_t x = 0u; x < view.width(); ++x)
                    {
                        SampleT& sample = from->first[static_cast<std::size_t>(x) * from->second];
                        (*to)[x] = sample;
                        sample = SampleT{0};
                    }
                }
            }
            held.push_back(std::move(*plane));
        }
    }
    const an::Status stretched = an::stretchImage(execution, an::Image<SampleT>::kSampleType, view.data(),
                                                  view.layout(), view.data(), view.layout(), request, validity)
                                     .status();
    // What was held goes back whether or not the stretch ran, so a refusal leaves those channels as they were given.
    for (std::size_t index = 0u; index < left.size(); ++index)
    {
        const auto heldView = held[index].view();
        if (!heldView.has_value())
        {
            return refusal(an::ErrorCode::Internal, "a held channel holds nothing");
        }
        for (std::uint32_t y = 0u; y < view.height(); ++y)
        {
            const auto to = channelRowOf(view, left[index], y);
            const auto from = heldView->row(0u, y);
            if (!to.has_value() || !from.has_value())
            {
                return refusal(an::ErrorCode::Internal, "a channel's row is out of the image");
            }
            for (std::uint32_t x = 0u; x < view.width(); ++x)
            {
                to->first[static_cast<std::size_t>(x) * to->second] = (*from)[x];
            }
        }
    }
    return stretched;
}

} // namespace

an::Status stretchWorkingImage(const an::EngineContext& engine, const an::ExecutionContext& execution,
                               an::consumer::AnyImage& working, const an::StretchImageRequest& request,
                               const an::KernelValidityView& validity)
{
    return std::visit(
        [&](auto& typed) -> an::Status {
            using SampleT = SampleOf<decltype(typed)>;
            if constexpr (!std::is_floating_point_v<SampleT>)
            {
                return refusal(an::ErrorCode::Internal, "the stretch's working image is not floating");
            }
            else
            {
                const std::optional<an::ImageWriteAccess<SampleT>> access = typed.write();
                if (!access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the working image holds nothing");
                }
                return stretchKeeping(engine, execution, access->view(), request, validity);
            }
        },
        working);
}

namespace
{

/// What a stretch states of an image of this metadata and description, as one transition: what the engine operation
/// declares it changes, the meaning no longer claiming the encoding it had, the lineage entry, and the cards the
/// operation leaves restated with the product's own record of the samples - nonlinear, by this stretch.
[[nodiscard]] an::Result<an::MetadataTransition>
stretchTransition(const an::ImageMetadata& metadata, const an::ImageDescriptor& descriptor, const StretchRecord& record)
{
    // The samples no longer stand for what they measured, and nothing is claimed about the curve that took them there.
    an::ImageSemanticsRequest meaning = metadata.semantics().toRequest();
    meaning.transferFunction = an::TransferFunction::Unspecified;
    meaning.gammaExponent.reset();
    const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    an::Result<an::MetadataTransition> change = an::operationTransition(
        record.effect,
        an::OperationOutcome{.operation = record.operation, .detail = record.detail, .semantics = *semantics});
    if (!change.hasValue())
    {
        return change.status();
    }
    // The cards the operation leaves - none when it drops them - with the product's own record of what its samples
    // are: nonlinear, by this stretch. They are stated whole, so the transition says everything the stretch changes.
    const an::CodecMetadata left = change->clearCodecMetadata ? an::CodecMetadata{} : metadata.codec();
    const an::ProcessingStateRecord state =
        an::nonlinearState(an::readProcessingStateCards(left), descriptor, record.processingOperation);
    an::Result<an::CodecMetadata> codec = an::withProcessingStateCards(left, state);
    if (!codec.hasValue())
    {
        return codec.status();
    }
    change->clearCodecMetadata = false;
    change->codec = std::move(*codec);
    return change;
}

} // namespace

an::Result<an::ImageMetadata> stretchedMetadata(const an::ImageMetadata& metadata,
                                                const an::ImageDescriptor& descriptor, const StretchRecord& record)
{
    const an::Result<an::MetadataTransition> change = stretchTransition(metadata, descriptor, record);
    if (!change.hasValue())
    {
        return change.status();
    }
    return an::applyMetadataTransition(metadata, *change);
}

namespace
{

/// Makes the destination a stretched image of the source: the result, with a copy of the source's validity.
[[nodiscard]] an::Status publishStretched(const an::consumer::EngineSession& session, const ProcessSource& source,
                                          an::consumer::AnyImage&& result, ProcessDestination& destination)
{
    std::optional<an::ValidityMask> mask;
    if (const an::ValidityMask* validity = source.validity(); validity != nullptr)
    {
        an::Result<an::ValidityMask> copied = validity->clone();
        if (!copied.hasValue())
        {
            return copied.status();
        }
        mask = std::move(*copied);
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, std::move(result), std::move(mask));
    if (!asset.hasValue())
    {
        return asset.status();
    }
    return destination.replace(std::move(*asset));
}

/// Whether a stretch of a representation can be looked up by its codes: the unsigned eight- and sixteen-bit ones, read
/// as fractions of their full scale and written back to the nearest code.
template <typename SampleT>
inline constexpr bool kStretchLooksUp = std::is_same_v<SampleT, std::uint8_t> || std::is_same_v<SampleT, std::uint16_t>;

/// The layout of one plane of a planar image, at the image's own strides.
[[nodiscard]] std::optional<an::ImageLayout> planeLayoutOf(const an::ImageLayout& layout,
                                                           const an::EngineLimits& limits)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(layout.extent().width(), layout.extent().height(), 1u, limits);
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    return an::ImageLayout::createWithStrides(
        an::ImageLayoutRequest{.extent = *extent,
                               .elementSizeBytes = layout.elementSizeBytes(),
                               .samplesPerPixel = 1u,
                               .planeCount = 1u,
                               .rowAlignmentBytes = 1u},
        layout.rowStrideBytes(), layout.rowStrideBytes() * static_cast<std::uint64_t>(layout.extent().height()),
        limits);
}

/// Looks the samples of planes up in a table of every code's answer, the planes read where they lie and written to
/// the result's.
///
/// @param table each code's answer as a fraction of full scale, whose code the lookup writes.
/// @param planes how many planes the layouts hold.
[[nodiscard]] an::Status lookUpCodes(const an::ExecutionContext& execution, an::SampleType sampleType,
                                     std::span<const double> table, std::uint32_t planes, const void* from,
                                     const an::ImageLayout& fromLayout, void* to, const an::ImageLayout& toLayout)
{
    an::TonePlanShape shape;
    shape.sampleType = sampleType;
    shape.channelCount = planes;
    const an::Result<an::CurveProgramPlan> lookup =
        an::buildLookupProgram(shape, an::LookupProgramRequest{.entries = table});
    if (!lookup.hasValue())
    {
        return lookup.status();
    }
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = from, .layout = &fromLayout}};
    return an::executePointwiseProgram(execution, lookup->program(), sources,
                                       an::PointwiseMutableImageView{.samples = to, .layout = &toLayout},
                                       an::KernelValidityView{})
        .status();
}

/// Stretches an image as one lookup of its codes, as `publishStretchOfSource` describes: the working copy's own steps
/// over one row of every code in every channel, then each plane looked up in its channel's answers - every plane at
/// once when the channels answer alike.
template <typename SampleT>
[[nodiscard]] an::Result<an::consumer::AnyImage>
stretchByCodeLookup(const an::EngineContext& engine, const an::ExecutionContext& execution,
                    const an::Image<SampleT>& image, const an::StretchImageRequest& request,
                    const an::KernelValidityView& validity, an::ImageMetadata metadata)
{
    constexpr std::uint32_t kCodes = std::uint32_t{1} << (8u * sizeof(SampleT));
    const std::optional<an::ImageDescriptor> described = image.descriptor();
    const auto view = image.view();
    if (!described.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    const std::uint32_t channels = described->extent().channelCount();

    // One row holding every code in every channel, described and arranged as the image is.
    const std::optional<an::ImageExtent> rowExtent = an::ImageExtent::create(kCodes, 1u, channels, engine.limits());
    if (!rowExtent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the row of every code cannot be described");
    }
    an::ImageDescriptorRequest rowRequest = described->toRequest();
    rowRequest.extent = *rowExtent;
    rowRequest.cfa = an::CfaDescriptor{};
    const std::optional<an::ImageDescriptor> rowDescribed = an::ImageDescriptor::create(rowRequest);
    if (!rowDescribed.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the row of every code cannot be described");
    }
    an::Result<an::Image<SampleT>> codes = an::Image<SampleT>::create(
        engine, *rowDescribed, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized);
    if (!codes.hasValue())
    {
        return codes.status();
    }
    {
        const std::optional<an::ImageWriteAccess<SampleT>> access = codes->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the row of every code holds nothing");
        }
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            const std::optional<std::span<SampleT>> row = access->view().row(channel, 0u);
            if (!row.has_value())
            {
                return refusal(an::ErrorCode::Internal, "a channel's row is out of the image");
            }
            for (std::uint32_t code = 0u; code < kCodes; ++code)
            {
                (*row)[code] = static_cast<SampleT>(code);
            }
        }
    }

    // Every code's answer, by the very steps the image's samples would take: read as fractions, stretched under the
    // image's own rule, and written back to the nearest code.
    an::Result<an::consumer::AnyImage> working =
        makeStretchWorkingCopy(engine, execution, an::consumer::AnyImage(std::move(*codes)));
    if (!working.hasValue())
    {
        return working.status();
    }
    const an::Status stretched = stretchWorkingImage(engine, execution, *working, request,
                                                     an::KernelValidityView{.descriptor = validity.descriptor});
    if (stretched.failed())
    {
        return stretched;
    }
    an::Result<an::Image<SampleT>> answers = an::Image<SampleT>::create(
        engine, *rowDescribed, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized);
    if (!answers.hasValue())
    {
        return answers.status();
    }
    {
        const std::optional<an::ImageWriteAccess<SampleT>> access = answers->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the answers hold nothing");
        }
        const an::Status written = std::visit(
            [&](const auto& typed) -> an::Status {
                using WorkT = SampleOf<decltype(typed)>;
                if constexpr (!std::is_floating_point_v<WorkT>)
                {
                    return refusal(an::ErrorCode::Internal, "the stretch's working row is not floating");
                }
                else
                {
                    const auto read = typed.view();
                    if (!read.has_value())
                    {
                        return refusal(an::ErrorCode::NotInitialized, "the stretched row holds nothing");
                    }
                    return writeFromFractions(engine, execution, *rowDescribed, an::Image<WorkT>::kSampleType,
                                              read->data(), read->layout(), access->view().data(),
                                              access->view().layout());
                }
            },
            *working);
        if (written.failed())
        {
            return written;
        }
    }

    // Each channel's answers as the lookup reads them: fractions of full scale, whose code it writes back.
    const auto answered = answers->view();
    if (!answered.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the answers hold nothing");
    }
    std::vector<std::vector<double>> tables(channels, std::vector<double>(kCodes));
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const auto row = answered->row(channel, 0u);
        if (!row.has_value())
        {
            return refusal(an::ErrorCode::Internal, "a channel's row is out of the image");
        }
        for (std::uint32_t code = 0u; code < kCodes; ++code)
        {
            tables[channel][code] = static_cast<double>((*row)[code]) / static_cast<double>(kCodes - 1u);
        }
    }
    const bool alike = std::all_of(tables.begin() + 1, tables.end(),
                                   [&](const std::vector<double>& table) { return table == tables.front(); });

    an::Result<an::Image<SampleT>> result =
        an::Image<SampleT>::create(engine, *described, an::StorageArrangement::Planar,
                                   an::StorageInitialization::Uninitialized, std::move(metadata));
    if (!result.hasValue())
    {
        return result.status();
    }
    {
        const std::optional<an::ImageWriteAccess<SampleT>> access = result->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the result holds nothing");
        }
        constexpr an::SampleType kStored = an::Image<SampleT>::kSampleType;
        if (alike)
        {
            const an::Status looked = lookUpCodes(execution, kStored, tables.front(), channels, view->data(),
                                                  view->layout(), access->view().data(), access->view().layout());
            if (looked.failed())
            {
                return looked;
            }
        }
        else
        {
            const std::optional<an::ImageLayout> fromPlane = planeLayoutOf(view->layout(), engine.limits());
            const std::optional<an::ImageLayout> toPlane = planeLayoutOf(access->view().layout(), engine.limits());
            if (!fromPlane.has_value() || !toPlane.has_value())
            {
                return refusal(an::ErrorCode::InvalidArgument, "a plane of the image cannot be laid out");
            }
            const auto* from = reinterpret_cast<const std::byte*>(view->data());
            auto* to = reinterpret_cast<std::byte*>(access->view().data());
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                const an::Status looked = lookUpCodes(
                    execution, kStored, tables[channel], 1u, from + channel * view->layout().planeStrideBytes(),
                    *fromPlane, to + channel * access->view().layout().planeStrideBytes(), *toPlane);
                if (looked.failed())
                {
                    return looked;
                }
            }
        }
    }
    return an::consumer::AnyImage(std::move(*result));
}

} // namespace

an::Status publishStretchResult(const an::consumer::EngineSession& session, const an::EngineContext& engine,
                                const an::ExecutionContext& execution, const ProcessSource& source,
                                const an::consumer::AnyImage& working, const QRect& from, const StretchRecord& record,
                                ProcessDestination& destination)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the source holds nothing");
    }
    if (destination.valid() && &working == &destination.asset().image())
    {
        // The body wrote the stretch where the run writes: those samples are the result, already in the source's
        // representation and with its validity, and only what the stretch states of them is left to state - where
        // they are, with nothing of them copied.
        if (!from.isEmpty())
        {
            return refusal(an::ErrorCode::IncompatibleArguments,
                           "a rectangle of the destination's own samples cannot be the destination");
        }
        const an::Result<an::MetadataTransition> change = stretchTransition(
            std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, working),
            *described, record);
        if (!change.hasValue())
        {
            return change.status();
        }
        return destination.asset().transition(*change);
    }
    an::Result<an::ImageMetadata> metadata = stretchedMetadata(source.metadata(), *described, record);
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    an::Result<an::consumer::AnyImage> result = std::visit(
        [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            using ResultT = SampleOf<decltype(typed)>;
            return resultFrom<ResultT>(engine, execution, *described, std::move(*metadata), working, from);
        },
        source.image());
    if (!result.hasValue())
    {
        return result.status();
    }
    return publishStretched(session, source, std::move(*result), destination);
}

bool stretchLooksUpCodes(const an::consumer::AnyImage& image, const an::StretchImageRequest& request,
                         const an::KernelValidityView& validity)
{
    const an::ValidityRule rule = validity.descriptor.rule();
    if ((rule != an::ValidityRule::AllValid && rule != an::ValidityRule::FiniteOnly) || validity.mask != nullptr)
    {
        return false;
    }
    return std::visit(
        [&](const auto& typed) {
            using SampleT = SampleOf<decltype(typed)>;
            if constexpr (!kStretchLooksUp<SampleT>)
            {
                return false;
            }
            else
            {
                constexpr std::uint64_t kCodes = std::uint64_t{1} << (8u * sizeof(SampleT));
                const auto view = typed.view();
                if (!view.has_value() || view->layout().samplesPerPixel() != 1u)
                {
                    return false;
                }
                // The engine's own test of a stretch that takes each channel alone.
                const std::uint32_t channels = view->channelCount();
                const bool independent = request.stretch.function == an::StretchFunction::Linear ||
                                         request.colorMode == an::StretchColorMode::Independent || channels == 1u;
                return independent && channels <= 4u &&
                       static_cast<std::uint64_t>(view->width()) * view->height() >= kCodes;
            }
        },
        image);
}

an::Status publishStretchOfSource(const an::consumer::EngineSession& session, const an::EngineContext& engine,
                                  const an::ExecutionContext& execution, const ProcessSource& source,
                                  const an::StretchImageRequest& request, const an::KernelValidityView& validity,
                                  const StretchRecord& record, ProcessDestination& destination)
{
    if (stretchLooksUpCodes(source.image(), request, validity))
    {
        const std::optional<an::ImageDescriptor> described = source.descriptor();
        if (!described.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the source holds nothing");
        }
        an::Result<an::ImageMetadata> metadata = stretchedMetadata(source.metadata(), *described, record);
        if (!metadata.hasValue())
        {
            return metadata.status();
        }
        an::Result<an::consumer::AnyImage> looked = std::visit(
            [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
                using SampleT = SampleOf<decltype(typed)>;
                if constexpr (kStretchLooksUp<SampleT>)
                {
                    return stretchByCodeLookup<SampleT>(engine, execution, typed, request, validity,
                                                        std::move(*metadata));
                }
                else
                {
                    return refusal(an::ErrorCode::Internal, "the source's codes cannot be looked up");
                }
            },
            source.image());
        if (!looked.hasValue())
        {
            return looked.status();
        }
        return publishStretched(session, source, std::move(*looked), destination);
    }
    an::Result<an::consumer::AnyImage> working = makeStretchWorkingCopy(engine, execution, source.image());
    if (!working.hasValue())
    {
        return working.status();
    }
    const an::Status stretched = stretchWorkingImage(engine, execution, *working, request, validity);
    if (stretched.failed())
    {
        return stretched;
    }
    return publishStretchResult(session, engine, execution, source, *working, QRect(), record, destination);
}

an::Result<ScreenTransferStatistics> measureScreenTransferStatistics(const an::EngineContext& engine,
                                                                     const an::ExecutionContext& execution,
                                                                     const an::consumer::AnyImage& image,
                                                                     const an::ValidityMask* mask)
{
    return std::visit(
        [&](const auto& typed) -> an::Result<ScreenTransferStatistics> {
            using SampleT = SampleOf<decltype(typed)>;
            if constexpr (std::is_floating_point_v<SampleT>)
            {
                return measureFloating(execution, typed, mask);
            }
            else
            {
                // An integer's statistics are taken of the fractions of full scale its stretch runs over.
                an::Result<an::consumer::AnyImage> working = makeStretchWorkingCopy(engine, execution, image);
                if (!working.hasValue())
                {
                    return working.status();
                }
                return std::visit(
                    [&](const auto& copy) -> an::Result<ScreenTransferStatistics> {
                        using WorkT = SampleOf<decltype(copy)>;
                        if constexpr (!std::is_floating_point_v<WorkT>)
                        {
                            return refusal(an::ErrorCode::Internal, "an integer image's working copy is not floating");
                        }
                        else
                        {
                            return measureFloating(execution, copy, mask);
                        }
                    },
                    *working);
            }
        },
        image);
}

bool readsViewOfTarget(const ProcessRun& run, const QRect& area) noexcept
{
    return run.isForPreview() && (!area.isEmpty() || run.readsViewOfTarget());
}

an::Status readAsFractions(const an::ExecutionContext& execution, an::SampleType stored, const void* samples,
                           const an::ImageLayout& layout, an::SampleType working, void* to,
                           const an::ImageLayout& toLayout)
{
    // An unsigned integer is a fraction of its largest value, which the conversion at full scale gives; a signed one is
    // counted from its smallest value, which its own value, carried across, scaled and shifted gives.
    const bool signedStored = isSignedInteger(stored);
    an::ConversionCounters counters;
    const an::Status converted = an::convertImageSamples(
        execution, stored, samples, layout, working, to, toLayout,
        an::ConversionPolicy{.domain = signedStored ? an::DomainMapping::CodeValue : an::DomainMapping::FullScale},
        counters);
    if (converted.failed() || !signedStored)
    {
        return converted;
    }
    const SignedRange range = signedRangeOf(stored);
    return scaleSamples(execution, working, to, toLayout, to, toLayout, 1.0 / range.width,
                        -range.smallest / range.width);
}

an::Status writeFromFractions(const an::EngineContext& engine, const an::ExecutionContext& execution,
                              const an::ImageDescriptor& stored, an::SampleType working, const void* from,
                              const an::ImageLayout& fromLayout, void* to, const an::ImageLayout& toLayout)
{
    an::ConversionCounters counters;
    if (!isSignedInteger(stored.sampleType()))
    {
        return an::convertImageSamples(execution, working, from, fromLayout, stored.sampleType(), to, toLayout,
                                       an::ConversionPolicy{}, counters);
    }
    // A fraction goes back to the code it counts from the signed integer's smallest value, then is carried across.
    an::ImageDescriptorRequest codesRequest = stored.toRequest();
    codesRequest.sampleType = working;
    codesRequest.numericDomain = an::NumericDomain::NormalizedLinear;
    const std::optional<an::ImageDescriptor> codesDescribed = an::ImageDescriptor::create(codesRequest);
    if (!codesDescribed.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the samples have no working description");
    }
    const SignedRange range = signedRangeOf(stored.sampleType());
    const auto viaCodes = [&]<typename WorkT>() -> an::Status {
        an::Result<an::Image<WorkT>> codes = an::Image<WorkT>::create(
            engine, *codesDescribed, an::storageArrangementOf(fromLayout), an::StorageInitialization::Uninitialized);
        if (!codes.hasValue())
        {
            return codes.status();
        }
        const std::optional<an::ImageWriteAccess<WorkT>> written = codes->write();
        if (!written.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the codes hold nothing");
        }
        const an::Status scaled = scaleSamples(execution, working, from, fromLayout, written->view().data(),
                                               written->view().layout(), range.width, range.smallest);
        if (scaled.failed())
        {
            return scaled;
        }
        return an::convertImageSamples(execution, working, written->view().data(), written->view().layout(),
                                       stored.sampleType(), to, toLayout,
                                       an::ConversionPolicy{.domain = an::DomainMapping::CodeValue}, counters);
    };
    if (working == an::SampleType::Float64)
    {
        return viaCodes.template operator()<double>();
    }
    if (working == an::SampleType::Float32)
    {
        return viaCodes.template operator()<float>();
    }
    return refusal(an::ErrorCode::IncompatibleArguments, "fractions are Float32 or Float64 samples");
}

an::Result<DialogWork> admitDialogWork(const an::consumer::EngineSession& session,
                                       const an::consumer::ImageAsset* reading)
{
    std::optional<an::consumer::AssetReadLease> lease;
    if (reading != nullptr)
    {
        an::Result<an::consumer::AssetReadLease> taken = reading->read();
        if (!taken.hasValue())
        {
            return taken.status();
        }
        lease.emplace(std::move(*taken));
    }
    an::Result<an::consumer::OperationScope> scope =
        session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    return DialogWork{.lease = std::move(lease), .scope = std::move(*scope)};
}

an::Result<std::shared_ptr<const ScreenTransferStatistics>>
screenTransferStatisticsFor(const an::EngineContext& engine, const an::ExecutionContext& execution,
                            const ProcessSource& source, const ProcessRun& run, const QRect& area,
                            const std::shared_ptr<const an::consumer::ImageAsset>& target,
                            StretchStatisticsCache<ScreenTransferStatistics>& cache)
{
    if (readsViewOfTarget(run, area))
    {
        // A preview area, or a reduction or region of the target a frame runs over, is a view of the target: the
        // target's statistics, which the framework's lease on it holds still while the view is processed.
        if (!target)
        {
            return refusal(an::ErrorCode::NotInitialized, "the preview area's image is not known");
        }
        const an::consumer::AssetRevision revision = target->revision();
        if (std::shared_ptr<const ScreenTransferStatistics> kept = cache.find(revision))
        {
            return kept;
        }
        an::Result<an::consumer::AssetReadLease> lease = target->tryRead();
        if (!lease.hasValue())
        {
            return refusal(an::ErrorCode::Cancelled, "the image changes; the preview is asked for again");
        }
        an::Result<ScreenTransferStatistics> measured =
            measureScreenTransferStatistics(engine, execution, lease->image(), lease->validity());
        if (!measured.hasValue())
        {
            return measured.status();
        }
        auto shared = std::make_shared<const ScreenTransferStatistics>(std::move(*measured));
        cache.store(lease->revision(), shared);
        return std::shared_ptr<const ScreenTransferStatistics>(shared);
    }
    if (!run.isForSequence())
    {
        if (std::shared_ptr<const ScreenTransferStatistics> kept = cache.find(source.revision()))
        {
            return kept;
        }
    }
    an::Result<ScreenTransferStatistics> measured =
        measureScreenTransferStatistics(engine, execution, source.image(), source.validity());
    if (!measured.hasValue())
    {
        return measured.status();
    }
    auto shared = std::make_shared<const ScreenTransferStatistics>(std::move(*measured));
    if (!run.isForSequence())
    {
        cache.store(source.revision(), shared);
    }
    return std::shared_ptr<const ScreenTransferStatistics>(shared);
}

namespace
{

constexpr std::array<HistogramResolutionChoice, 11u> kResolutionChoices{{
    {"6-bit (64)", an::ui::HistogramResolution::Bits6},
    {"7-bit (128)", an::ui::HistogramResolution::Bits7},
    {"8-bit (256)", an::ui::HistogramResolution::Bits8},
    {"9-bit (512)", an::ui::HistogramResolution::Bits9},
    {"10-bit (1024)", an::ui::HistogramResolution::Bits10},
    {"12-bit (4096)", an::ui::HistogramResolution::Bits12},
    {"14-bit (16K)", an::ui::HistogramResolution::Bits14},
    {"16-bit (64K)", an::ui::HistogramResolution::Bits16},
    {"100 levels", an::ui::HistogramResolution::Levels100},
    {"1000 levels", an::ui::HistogramResolution::Levels1000},
    {"10000 levels", an::ui::HistogramResolution::Levels10000},
}};

/// The bin a value in the unit interval falls on at a histogram's resolution, rounded to the nearest.
[[nodiscard]] std::size_t binOf(double value, std::size_t bins) noexcept
{
    const double last = static_cast<double>(bins - 1u);
    return static_cast<std::size_t>(std::llround(std::clamp(value, 0.0, 1.0) * last));
}

} // namespace

std::span<const HistogramResolutionChoice> histogramResolutionChoices() noexcept
{
    return kResolutionChoices;
}

std::optional<an::ui::HistogramResolution> histogramResolutionForLabel(const QString& label)
{
    for (const HistogramResolutionChoice& choice : kResolutionChoices)
    {
        if (label == QLatin1String(choice.label))
        {
            return choice.resolution;
        }
    }
    return std::nullopt;
}

an::Result<an::ui::HistogramDisplayData>
remapHistogramThroughStretch(const an::ExecutionContext& execution, const an::ui::HistogramDisplayData& counts,
                             std::span<const an::GeneralizedStretchParameters> stretches, std::span<const bool> changed)
{
    if (stretches.empty())
    {
        return refusal(an::ErrorCode::InvalidArgument, "a histogram is remapped through no stretch");
    }
    an::ui::HistogramDisplayData remapped;
    remapped.channels.reserve(counts.channels.size());
    for (std::size_t channel = 0u; channel < counts.channels.size(); ++channel)
    {
        const an::ui::HistogramDisplayChannel& source = counts.channels[channel];
        const bool isChanged = counts.channels.size() == 1u || (channel < changed.size() && changed[channel]);
        if (!isChanged || source.bins.empty())
        {
            remapped.channels.push_back(source);
            continue;
        }
        an::ui::HistogramDisplayChannel destination;
        destination.bins.assign(source.bins.size(), 0u);
        const an::Status done = an::remapStretchHistogram(execution, source.bins, destination.bins,
                                                          stretches[(std::min)(channel, stretches.size() - 1u)]);
        if (done.failed())
        {
            return done;
        }
        destination.samples = source.samples;
        destination.notANumber = source.notANumber;
        destination.clippedLow = destination.bins.front();
        destination.clippedHigh = destination.bins.back();
        remapped.channels.push_back(std::move(destination));
    }
    return remapped;
}

ClippedSamples clippedByScreenTransfer(const an::ui::HistogramDisplayData& counts,
                                       std::span<const an::MidtonesTransferParameters> transfers,
                                       std::span<const bool> changed)
{
    ClippedSamples clipped;
    if (transfers.empty())
    {
        return clipped;
    }
    for (std::size_t channel = 0u; channel < counts.channels.size(); ++channel)
    {
        const bool isChanged = counts.channels.size() == 1u || (channel < changed.size() && changed[channel]);
        const std::vector<std::uint64_t>& bins = counts.channels[channel].bins;
        if (!isChanged || bins.empty())
        {
            continue;
        }
        const an::MidtonesTransferParameters& transfer = transfers[(std::min)(channel, transfers.size() - 1u)];
        const std::size_t shadows = binOf(transfer.shadows, bins.size());
        const std::size_t highlights = binOf(transfer.highlights, bins.size());
        for (std::size_t bin = 0u; bin < bins.size(); ++bin)
        {
            if (bin < shadows)
            {
                clipped.shadows += bins[bin];
            }
            else if (bin > highlights)
            {
                clipped.highlights += bins[bin];
            }
        }
    }
    return clipped;
}

namespace
{

/// The counts of every channel of a typed image, over the samples its validity admits.
template <typename SampleT>
[[nodiscard]] an::Result<an::ui::HistogramDisplayData>
countImage(const an::ExecutionContext& execution, const an::Image<SampleT>& image, const an::ValidityMask* mask,
           an::ui::HistogramResolution resolution)
{
    const auto view = image.view();
    if (!view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
    const an::KernelValidityView validity = stretchValidity(image.metadata(), mask, maskView);
    return an::ui::computeHistogramDisplayData(
        execution, an::Image<SampleT>::kSampleType, view->data(), view->layout(), resolution,
        an::StatisticsSelection{.validity = validity.descriptor, .mask = validity.mask});
}

/// The plane of a colour image's component, through the engine's channel scope: Float32, or Float64 for a Float64
/// image.
template <typename SampleT>
[[nodiscard]] an::Result<an::consumer::AnyImage>
componentPlane(const an::EngineContext& engine, const an::ExecutionContext& execution, const an::Image<SampleT>& image,
               const an::ValidityMask* mask, an::ChannelScope scope)
{
    const std::optional<an::ImageDescriptor> descriptor = image.descriptor();
    const auto view = image.view();
    if (!descriptor.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    // The original image may be a file's floating or sensor-code image, which states no full scale; its component is
    // taken from its samples read as fractions of full scale, as the view shows them.
    const std::optional<an::ImageDescriptor> reading = an::ui::componentReadingOf(*descriptor);
    if (!reading.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the image cannot be read as fractions of full scale");
    }
    const an::Result<an::ChannelScopePlan> plan =
        an::planChannelScope(an::ui::processChannelScopeRequest(scope, *reading));
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const an::Result<an::ImageDescriptor> described = an::channelScopePlaneDescriptor(*plan, *reading);
    if (!described.hasValue())
    {
        return described.status();
    }
    using PlaneT = std::conditional_t<std::is_same_v<SampleT, double>, double, float>;
    an::Result<an::Image<PlaneT>> plane = an::Image<PlaneT>::create(engine, *described, an::StorageArrangement::Planar,
                                                                    an::StorageInitialization::Zeroed);
    if (!plane.hasValue())
    {
        return plane.status();
    }
    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
    an::ChannelScopeSource source{.descriptor = &*reading, .samples = view->data(), .layout = &view->layout()};
    source.validity = stretchValidity(image.metadata(), mask, maskView);
    {
        const std::optional<an::ImageWriteAccess<PlaneT>> access = plane->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the component's plane holds nothing");
        }
        const an::Result<an::ChannelScopeResult> extracted =
            an::extractChannelScope(execution, *plan, source, access->view().data(), access->view().layout());
        if (!extracted.hasValue())
        {
            return extracted.status();
        }
    }
    return an::consumer::AnyImage(std::move(*plane));
}

/// A luminance of stated weights of a colour image, in every channel of a Float32 copy: a channel matrix whose rows
/// are the weights, over the image's Float32 working copy.
template <typename SampleT>
[[nodiscard]] an::Result<an::consumer::AnyImage>
luminancePlanes(const an::EngineContext& engine, const an::ExecutionContext& execution, const an::Image<SampleT>& image,
                const an::ValidityMask* mask, const std::array<double, 3u>& weights)
{
    an::Result<an::consumer::AnyImage> working = workingCopyAs<float>(engine, execution, image);
    if (!working.hasValue())
    {
        return working.status();
    }
    auto& floating = std::get<an::Image<float>>(*working);
    const std::optional<an::ImageDescriptor> descriptor = floating.descriptor();
    const auto view = floating.view();
    if (!descriptor.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the working copy holds nothing");
    }
    an::PointwiseProgramBuilder builder;
    builder.channels(3u).sources(1u).readingSamples(an::SampleType::Float32);
    const std::array<double, 9u> matrix{weights[0], weights[1], weights[2], weights[0], weights[1],
                                        weights[2], weights[0], weights[1], weights[2]};
    builder.append(an::pointwiseChannelMatrix(builder.addTable(matrix)));
    const an::Result<an::PointwiseProgram> program = builder.build();
    if (!program.hasValue())
    {
        return program.status();
    }
    an::Result<an::Image<float>> luminance = an::Image<float>::createDestination(
        engine, *descriptor, an::storageArrangementOf(view->layout()), an::StorageInitialization::Zeroed, floating);
    if (!luminance.hasValue())
    {
        return luminance.status();
    }
    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
    const an::KernelValidityView validity = stretchValidity(floating.metadata(), mask, maskView);
    {
        const std::optional<an::ImageWriteAccess<float>> access = luminance->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the luminance holds nothing");
        }
        const std::array<an::PointwiseImageView, 1u> sources{
            an::PointwiseImageView{.samples = view->data(), .layout = &view->layout()}};
        const an::Result<an::PointwiseExecutionResult> mixed = an::executePointwiseProgram(
            execution, *program, sources,
            an::PointwiseMutableImageView{.samples = access->view().data(), .layout = &access->view().layout()},
            validity);
        if (!mixed.hasValue())
        {
            return mixed.status();
        }
    }
    return an::consumer::AnyImage(std::move(*luminance));
}

} // namespace

an::Result<an::ui::HistogramDisplayData>
countStretchHistogram(const an::EngineContext& engine, const an::ExecutionContext& execution,
                      const an::consumer::AnyImage& image, const an::ValidityMask* mask,
                      const an::ui::HistogramResolution resolution, const HistogramComponent component,
                      const std::array<double, 3u>& luminanceWeights)
{
    return std::visit(
        [&](const auto& typed) -> an::Result<an::ui::HistogramDisplayData> {
            an::Result<an::ui::HistogramDisplayData> counts = countImage(execution, typed, mask, resolution);
            if (!counts.hasValue() || component == HistogramComponent::None || counts->channels.size() != 3u)
            {
                return counts;
            }
            an::Result<an::consumer::AnyImage> plane =
                component == HistogramComponent::Luminance
                    ? luminancePlanes(engine, execution, typed, mask, luminanceWeights)
                    : componentPlane(engine, execution, typed, mask,
                                     component == HistogramComponent::Lightness ? an::ChannelScope::CieLightness
                                                                                : an::ChannelScope::HsvSaturation);
            if (!plane.hasValue())
            {
                return plane.status();
            }
            // A component's pixel the image's validity rejects is not a number in the plane, and enters no bin.
            an::Result<an::ui::HistogramDisplayData> componentCounts = std::visit(
                [&](const auto& planar) { return countImage(execution, planar, nullptr, resolution); }, *plane);
            if (!componentCounts.hasValue())
            {
                return componentCounts.status();
            }
            counts->channels.push_back(std::move(componentCounts->channels.front()));
            return counts;
        },
        image);
}

an::Result<double> meanOfRegion(const an::consumer::EngineSession& session, const an::EngineContext& engine,
                                const an::ExecutionContext& execution, const an::consumer::AnyImage& image,
                                const an::ValidityMask* mask, const QRect& region)
{
    an::Result<an::consumer::ImageAsset> cropped =
        an::ui::cropProcessRegion(session, engine, execution, image, mask, region);
    if (!cropped.hasValue())
    {
        return cropped.status();
    }
    an::Result<an::consumer::AssetReadLease> lease = cropped->read();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    an::Result<an::consumer::AnyImage> working = makeStretchWorkingCopy(engine, execution, lease->image());
    if (!working.hasValue())
    {
        return working.status();
    }
    return std::visit(
        [&](const auto& typed) -> an::Result<double> {
            using SampleT = SampleOf<decltype(typed)>;
            const auto view = typed.view();
            if (!view.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the region holds nothing");
            }
            std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
            const an::KernelValidityView validity = stretchValidity(typed.metadata(), lease->validity(), maskView);
            double sum = 0.0;
            for (std::uint32_t channel = 0u; channel < view->channelCount(); ++channel)
            {
                const an::Result<an::BasicStatistics> basic = an::computeImageBasicStatistics(
                    execution, an::Image<SampleT>::kSampleType, view->data(), view->layout(), channel,
                    an::StatisticsSelection{.validity = validity.descriptor, .mask = validity.mask});
                if (!basic.hasValue())
                {
                    return basic.status();
                }
                sum += basic->mean;
            }
            return sum / static_cast<double>((std::max)(1u, view->channelCount()));
        },
        *working);
}

} // namespace an::ui::processes
