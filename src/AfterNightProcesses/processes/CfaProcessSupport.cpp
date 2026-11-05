// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The filter-array operations the Split CFA, Merge CFA and Debayer windows run over one image.

#include "stdafx.hpp"

#include "processes/CfaProcessSupport.h"

#include "app/PlateSolveTypes.h"
#include "controls/ImageViewFiles.h"
#include "processes/ChannelActions.h"
#include "processes/MaskProcessUtils.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Demosaic/CfaChannels.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, an::StatusDetail{.message = std::move(message)});
}

/// What joining four planes into a mosaic does to the image it writes: the counterpart of the engine's split - a new
/// description and the same meaning, no solution, the cards and the output decision kept, each sample's validity the
/// one it came from. The cards stating a solution are not kept: the mosaic starts from `withoutSolutionCards`.
constexpr an::OperationEffect kCfaJoinEffect{.samples = an::SampleEffect::Writes,
                                             .description = an::DescriptionEffect::Restates,
                                             .semantics = an::SemanticEffect::Restates,
                                             .solution = an::SolutionEffect::Invalidates,
                                             .codecMetadata = an::CodecMetadataEffect::Preserves,
                                             .outputIntent = an::OutputIntentEffect::Preserves,
                                             .validity = {.policy = an::ValidityPolicy::Preserve}};

/// The sample type of a typed image.
template <typename ImageT> using SampleOf = typename std::remove_cvref_t<ImageT>::Sample;

/// The metadata an image whose pixel grid a filter-array change replaced starts from: the source's, without the
/// world-coordinate cards and the solver's card. No solution describes the new grid, and a solution is read from the
/// cards, so a split plane or a joined mosaic keeps the source's other cards and none that state one - as every other
/// geometry change drops the cards of a solution it does not move.
[[nodiscard]] an::Result<an::ImageMetadata> withoutSolutionCards(const an::ImageMetadata& metadata)
{
    const an::Result<an::CodecMetadata> withoutSolution = an::withoutWcsCards(metadata.codec());
    if (!withoutSolution.hasValue())
    {
        return withoutSolution.status();
    }
    std::vector<an::CodecCard> kept;
    kept.reserve(withoutSolution->cards().size());
    for (const an::CodecCard& card : withoutSolution->cards())
    {
        if (card.origin() != an::CodecNamespace::Fits || card.keyword() != an::ui::kPlateSolveSolverKeyword)
        {
            kept.push_back(card);
        }
    }
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(withoutSolution->sourceEncoding(), std::move(kept));
    if (!codec.hasValue())
    {
        return codec.status();
    }
    return an::ImageMetadata::create(metadata.acquisition(), metadata.semantics(), metadata.astrometry(),
                                     metadata.provenance(), std::move(*codec), metadata.outputIntent());
}

/// A planar layout without row padding, so that one plane of a multi-plane layout is laid out as a one-plane one.
[[nodiscard]] an::Result<an::ImageLayout> tightLayout(const an::EngineContext& engine, std::uint32_t width,
                                                      std::uint32_t height, std::uint32_t planes,
                                                      std::uint32_t elementBytes)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, planes, engine.limits());
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::OutOfRange, "the planes cannot be described");
    }
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = elementBytes,
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = planes,
                                                       .rowAlignmentBytes = 1u},
                                engine.limits());
    if (!layout.has_value())
    {
        return refusal(an::ErrorCode::OutOfRange, "the planes cannot be laid out");
    }
    return *layout;
}

/// Refuses a source whose validity is stated in a mask: the filter-array windows carry no mask through their planes.
[[nodiscard]] an::Status refuseMasked(const an::ui::ProcessSource& source, const char* what)
{
    if (source.validity() != nullptr)
    {
        return refusal(an::ErrorCode::Unsupported,
                       std::string("an image whose valid samples are stated in a mask cannot be ") + what);
    }
    return an::Status::success();
}

/// Refuses, before any work, a mosaic the narrowband extractions cannot take: one with a sample that is not valid,
/// which the extraction's record cannot state.
[[nodiscard]] an::Status refuseUnlessAllValid(const an::ui::ProcessSource& source)
{
    if (source.metadata().semantics().validity().rule() != an::ValidityRule::AllValid)
    {
        return refusal(an::ErrorCode::Unsupported,
                       "an image that states some of its samples invalid cannot have a narrowband channel extracted");
    }
    return an::Status::success();
}

template <typename SampleT>
[[nodiscard]] an::Result<std::vector<an::consumer::ImageAsset>> splitTyped(const an::ui::ExecutionScope& scope,
                                                                           const an::Image<SampleT>& mosaic)
{
    const std::optional<an::ImageDescriptor> described = mosaic.descriptor();
    const auto view = mosaic.view();
    if (!described.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    const an::Result<an::ImageDescriptor> planeDescriptor = an::measuredBayerChannelDescriptor(*described);
    if (!planeDescriptor.hasValue())
    {
        return planeDescriptor.status();
    }
    const std::uint32_t width = planeDescriptor->extent().width();
    const std::uint32_t height = planeDescriptor->extent().height();
    const an::Result<an::ImageLayout> planes = tightLayout(scope.engine(), width, height, 4u, sizeof(SampleT));
    const an::Result<an::ImageLayout> one = tightLayout(scope.engine(), width, height, 1u, sizeof(SampleT));
    if (!planes.hasValue() || !one.hasValue())
    {
        return planes.hasValue() ? one.status() : planes.status();
    }
    an::Result<an::ImageStorage<SampleT>> split =
        an::ImageStorage<SampleT>::create(scope.engine(), *planes, an::StorageInitialization::Uninitialized);
    if (!split.hasValue())
    {
        return split.status();
    }
    const an::Result<an::ImageMetadata> planeMetadata = withoutSolutionCards(mosaic.metadata());
    if (!planeMetadata.hasValue())
    {
        return planeMetadata.status();
    }
    an::CfaChannelSelection selection;
    selection.validity = mosaic.metadata().semantics().validity();
    const an::Result<an::CfaChannelResult> ran =
        an::splitCfaBlockPlanes(scope.execution(), an::kSampleTypeOf<SampleT>, view->data(), view->layout(),
                                described->cfa(), split->data(), *planes, selection);
    if (!ran.hasValue())
    {
        return ran.status();
    }
    std::vector<an::consumer::ImageAsset> made;
    for (std::uint32_t plane = 0u; plane < 4u; ++plane)
    {
        an::Result<an::Image<SampleT>> image =
            an::Image<SampleT>::create(scope.engine(), *planeDescriptor, an::StorageArrangement::Planar,
                                       an::StorageInitialization::Uninitialized, *planeMetadata);
        if (!image.hasValue())
        {
            return image.status();
        }
        {
            std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
            if (!access.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "a plane holds nothing");
            }
            const SampleT* first =
                split->data() + static_cast<std::size_t>(plane) * planes->planeStrideBytes() / sizeof(SampleT);
            if (an::Status copied = an::repackImageSamples(scope.execution(), an::kSampleTypeOf<SampleT>, first, *one,
                                                           access->view().data(), access->view().layout());
                copied.failed())
            {
                return copied;
            }
        }
        const an::Result<an::MetadataTransition> change = an::operationTransition(
            an::kCfaSplitEffect, an::OperationOutcome{.operation = "an::splitCfaBlockPlanes",
                                                      .detail = "Split CFA, plane " + std::to_string(plane),
                                                      .semantics = mosaic.metadata().semantics()});
        if (!change.hasValue())
        {
            return change.status();
        }
        if (an::Status stated = image->transition(*change); stated.failed())
        {
            return stated;
        }
        an::Result<an::consumer::ImageAsset> asset =
            an::consumer::ImageAsset::adopt(scope.session(), an::consumer::AnyImage(std::move(*image)));
        if (!asset.hasValue())
        {
            return asset.status();
        }
        made.push_back(std::move(*asset));
    }
    return made;
}

template <typename SampleT>
[[nodiscard]] an::Result<an::consumer::ImageAsset> joinTyped(const an::ui::ExecutionScope& scope,
                                                             const std::array<const an::Image<SampleT>*, 4u>& planes,
                                                             const an::CfaDescriptor& cfa)
{
    const std::optional<an::ImageDescriptor> first = planes[0]->descriptor();
    if (!first.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "a plane holds nothing");
    }
    const std::uint32_t width = first->extent().width();
    const std::uint32_t height = first->extent().height();
    const an::Result<an::ImageLayout> layout4 = tightLayout(scope.engine(), width, height, 4u, sizeof(SampleT));
    const an::Result<an::ImageLayout> one = tightLayout(scope.engine(), width, height, 1u, sizeof(SampleT));
    if (!layout4.hasValue() || !one.hasValue())
    {
        return layout4.hasValue() ? one.status() : layout4.status();
    }
    an::Result<an::ImageStorage<SampleT>> joined =
        an::ImageStorage<SampleT>::create(scope.engine(), *layout4, an::StorageInitialization::Uninitialized);
    if (!joined.hasValue())
    {
        return joined.status();
    }
    for (std::uint32_t plane = 0u; plane < 4u; ++plane)
    {
        const auto view = planes[plane]->view();
        if (!view.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "a plane holds nothing");
        }
        SampleT* target =
            joined->data() + static_cast<std::size_t>(plane) * layout4->planeStrideBytes() / sizeof(SampleT);
        if (an::Status copied = an::repackImageSamples(scope.execution(), an::kSampleTypeOf<SampleT>, view->data(),
                                                       view->layout(), target, *one);
            copied.failed())
        {
            return copied;
        }
    }
    an::ImageDescriptorRequest request = first->toRequest();
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width * 2u, height * 2u, 1u, scope.engine().limits());
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::OutOfRange, "the mosaic cannot be described");
    }
    request.extent = *extent;
    request.channelSchema = an::ChannelSchema::Mono;
    request.colorSpace = an::ColorSpace::Unspecified;
    request.cfa = cfa;
    const std::optional<an::ImageDescriptor> mosaicDescriptor = an::ImageDescriptor::create(request);
    if (!mosaicDescriptor.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the planes cannot be described as a mosaic");
    }
    const an::Result<an::ImageMetadata> mosaicMetadata = withoutSolutionCards(planes[0]->metadata());
    if (!mosaicMetadata.hasValue())
    {
        return mosaicMetadata.status();
    }
    an::Result<an::Image<SampleT>> mosaic =
        an::Image<SampleT>::create(scope.engine(), *mosaicDescriptor, an::StorageArrangement::Planar,
                                   an::StorageInitialization::Zeroed, *mosaicMetadata);
    if (!mosaic.hasValue())
    {
        return mosaic.status();
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = mosaic->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the mosaic holds nothing");
        }
        an::CfaChannelSelection selection;
        selection.validity = planes[0]->metadata().semantics().validity();
        const an::Result<an::CfaChannelResult> ran =
            an::joinCfaBlockPlanes(scope.execution(), an::kSampleTypeOf<SampleT>, joined->data(), *layout4, cfa,
                                   access->view().data(), access->view().layout(), selection);
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }
    const an::Result<an::MetadataTransition> change = an::operationTransition(
        kCfaJoinEffect, an::OperationOutcome{.operation = "an::joinCfaBlockPlanes",
                                             .detail = std::string("Merge CFA, ") + an::cfaPatternName(cfa.pattern()),
                                             .semantics = planes[0]->metadata().semantics()});
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status stated = mosaic->transition(*change); stated.failed())
    {
        return stated;
    }
    return an::consumer::ImageAsset::adopt(scope.session(), an::consumer::AnyImage(std::move(*mosaic)));
}

/// One measured channel of a mosaic in its own representation: H-alpha or green.
template <typename SampleT>
[[nodiscard]] an::Result<an::consumer::ImageAsset> measuredTyped(const an::ui::ExecutionScope& scope,
                                                                 const an::Image<SampleT>& mosaic,
                                                                 an::NarrowbandExtractionProduct product)
{
    const std::optional<an::ImageDescriptor> described = mosaic.descriptor();
    const auto view = mosaic.view();
    if (!described.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    const an::Result<an::ImageDescriptor> channel = an::measuredBayerChannelDescriptor(*described);
    if (!channel.hasValue())
    {
        return channel.status();
    }
    an::Result<an::Image<SampleT>> image =
        an::Image<SampleT>::create(scope.engine(), *channel, an::StorageArrangement::Planar,
                                   an::StorageInitialization::Uninitialized, mosaic.metadata());
    if (!image.hasValue())
    {
        return image.status();
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the channel holds nothing");
        }
        const an::Result<an::NarrowbandExtractionResult> ran =
            product == an::NarrowbandExtractionProduct::HydrogenAlpha
                ? an::extractBayerHydrogenAlpha(scope.execution(), *described, view->data(), view->layout(),
                                                access->view().data(), access->view().layout())
                : an::extractBayerGreen(scope.execution(), *described, view->data(), view->layout(),
                                        access->view().data(), access->view().layout());
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }
    const an::Result<an::MetadataTransition> change =
        an::narrowbandExtractionTransition(mosaic.metadata().semantics(), product);
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status stated = image->transition(*change); stated.failed())
    {
        return stated;
    }
    return an::consumer::ImageAsset::adopt(scope.session(), an::consumer::AnyImage(std::move(*image)));
}

/// Narrows one floating product of the H-alpha and OIII pair into the mosaic's representation, read back the way the
/// working copy read it - an unsigned code a fraction of its largest value, a signed one counted from its smallest - to
/// the nearest code, and records the extraction. A signed product's fractions are worked into codes in place.
template <typename WorkingT, typename StoredT>
[[nodiscard]] an::Result<an::consumer::ImageAsset>
narrowedProduct(const an::ui::ExecutionScope& scope, an::Image<WorkingT>& product, const an::ImageDescriptor& stored,
                const an::ImageMetadata& metadata, an::NarrowbandExtractionScaling scaling)
{
    const std::optional<an::ImageDescriptor> working = product.descriptor();
    const auto view = product.view();
    if (!working.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the product holds nothing");
    }
    an::ImageDescriptorRequest request = working->toRequest();
    request.sampleType = stored.sampleType();
    request.numericDomain = stored.numericDomain();
    const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(request);
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the product cannot be described in the mosaic's samples");
    }
    an::Result<an::Image<StoredT>> image = an::Image<StoredT>::create(
        scope.engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, metadata);
    if (!image.hasValue())
    {
        return image.status();
    }
    constexpr bool kCountedFromSmallest = std::is_integral_v<StoredT> && std::is_signed_v<StoredT>;
    if constexpr (kCountedFromSmallest)
    {
        std::optional<an::ImageWriteAccess<WorkingT>> codes = product.write();
        if (!codes.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the product holds nothing");
        }
        const double smallest = static_cast<double>(std::numeric_limits<StoredT>::lowest());
        const double width = static_cast<double>(std::numeric_limits<StoredT>::max()) - smallest;
        if (an::Status scaled =
                process_utils::scaleAndBiasInPlace(scope.execution(), an::kSampleTypeOf<WorkingT>, codes->view().data(),
                                                   codes->view().layout(), width, smallest);
            scaled.failed())
        {
            return scaled;
        }
    }
    {
        const auto worked = product.view();
        std::optional<an::ImageWriteAccess<StoredT>> access = image->write();
        if (!worked.has_value() || !access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the product holds nothing");
        }
        an::ConversionCounters counters;
        if (an::Status converted = an::convertImageSamples(
                scope.execution(), an::kSampleTypeOf<WorkingT>, worked->data(), worked->layout(),
                an::kSampleTypeOf<StoredT>, access->view().data(), access->view().layout(),
                an::ConversionPolicy{.domain = kCountedFromSmallest ? an::DomainMapping::CodeValue
                                                                    : an::DomainMapping::FullScale},
                counters);
            converted.failed())
        {
            return converted;
        }
    }
    const an::Result<an::MetadataTransition> change = an::narrowbandExtractionTransition(
        metadata.semantics(), an::NarrowbandExtractionProduct::HydrogenAlphaAndOxygenIII, scaling);
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status stated = image->transition(*change); stated.failed())
    {
        return stated;
    }
    return an::consumer::ImageAsset::adopt(scope.session(), an::consumer::AnyImage(std::move(*image)));
}

} // namespace

namespace an::ui::processes
{

int representationRank(an::SampleType type) noexcept
{
    switch (type)
    {
    case an::SampleType::Int8:
        return 0;
    case an::SampleType::UInt8:
        return 1;
    case an::SampleType::Int16:
        return 2;
    case an::SampleType::UInt16:
        return 3;
    case an::SampleType::Int32:
        return 4;
    case an::SampleType::UInt32:
        return 5;
    case an::SampleType::Float32:
        return 6;
    case an::SampleType::Float64:
    default:
        return 7;
    }
}

an::Status checkBayerMosaic(const std::optional<an::ImageDescriptor>& descriptor)
{
    if (!descriptor.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the view shows no image");
    }
    if (!descriptor->isMosaiced())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the image states no colour filter array");
    }
    if (descriptor->extent().channelCount() != 1u)
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "a mosaic has one channel");
    }
    if (an::cfaBlockPlaneCount(descriptor->cfa().pattern()) != 4u)
    {
        return refusal(an::ErrorCode::Unsupported,
                       std::string("the ") + an::cfaPatternName(descriptor->cfa().pattern()) +
                           " filter array is not a two-by-two arrangement, which is all this process takes apart");
    }
    return an::Status::success();
}

std::array<std::string, 4u> mosaicPlaneNames(const an::CfaDescriptor& cfa)
{
    std::array<std::string, 4u> names;
    int greens = 0;
    for (std::uint32_t plane = 0u; plane < 4u; ++plane)
    {
        std::string colour;
        switch (cfa.colorAt(plane % 2u, plane / 2u))
        {
        case an::CfaColor::Red:
            colour = "Red";
            break;
        case an::CfaColor::Green:
            colour = "Green" + std::to_string(++greens);
            break;
        case an::CfaColor::Blue:
            colour = "Blue";
            break;
        }
        names[plane] = "CFA" + std::to_string(plane) + " - " + colour;
    }
    return names;
}

an::Result<std::vector<an::consumer::ImageAsset>> splitMosaicPlanes(const an::ui::ExecutionScope& scope,
                                                                    const an::ui::ProcessSource& source)
{
    if (an::Status mosaic = checkBayerMosaic(source.descriptor()); mosaic.failed())
    {
        return mosaic;
    }
    if (an::Status masked = refuseMasked(source, "split"); masked.failed())
    {
        return masked;
    }
    return std::visit([&](const auto& typed) { return splitTyped(scope, typed); }, source.image());
}

an::Result<an::consumer::ImageAsset> joinMosaicPlanes(const an::ui::ExecutionScope& scope,
                                                      const std::array<const an::ui::ProcessSource*, 4u>& planes,
                                                      an::CfaPattern pattern)
{
    if (an::cfaBlockPlaneCount(pattern) != 4u)
    {
        return refusal(an::ErrorCode::InvalidArgument, "a mosaic is joined under one of the four two-by-two arrays");
    }
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(pattern, 0u, 0u);
    if (!cfa.has_value())
    {
        return refusal(an::ErrorCode::Internal, "the arrangement has no description");
    }
    an::SampleType highest = an::SampleType::Int8;
    std::optional<an::ImageExtent> extent;
    for (const an::ui::ProcessSource* plane : planes)
    {
        const std::optional<an::ImageDescriptor> described = plane != nullptr ? plane->descriptor() : std::nullopt;
        if (!described.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "a plane holds nothing");
        }
        if (described->extent().channelCount() != 1u)
        {
            return refusal(an::ErrorCode::IncompatibleArguments, "every plane of a mosaic is one grey channel");
        }
        if (extent.has_value() &&
            (extent->width() != described->extent().width() || extent->height() != described->extent().height()))
        {
            return refusal(an::ErrorCode::IncompatibleArguments, "the four planes must have one width and height");
        }
        if (an::Status masked = refuseMasked(*plane, "merged"); masked.failed())
        {
            return masked;
        }
        extent = described->extent();
        if (representationRank(described->sampleType()) > representationRank(highest))
        {
            highest = described->sampleType();
        }
    }
    // Every plane in the highest representation, converted at full scale where it is another.
    std::array<std::optional<an::consumer::AnyImage>, 4u> converted;
    for (std::size_t index = 0u; index < 4u; ++index)
    {
        if (planes[index]->descriptor()->sampleType() != highest)
        {
            an::Result<an::consumer::AnyImage> copy =
                an::ui::convertViewFrame(scope.engine(), scope.execution(), planes[index]->image(), highest);
            if (!copy.hasValue())
            {
                return copy.status();
            }
            converted[index].emplace(std::move(*copy));
        }
    }
    an::Result<an::consumer::ImageAsset> joined = refusal(an::ErrorCode::Internal, "no representation was joined");
    (void)an::dispatchSampleType(highest, [&]<typename SampleT>() {
        std::array<const an::Image<SampleT>*, 4u> typed{};
        for (std::size_t index = 0u; index < 4u; ++index)
        {
            const an::consumer::AnyImage& image =
                converted[index].has_value() ? *converted[index] : planes[index]->image();
            typed[index] = std::get_if<an::Image<SampleT>>(&image);
            if (typed[index] == nullptr)
            {
                joined = refusal(an::ErrorCode::Internal, "a plane is not in the joined representation");
                return;
            }
        }
        joined = joinTyped(scope, typed, *cfa);
    });
    return joined;
}

an::Result<an::consumer::ImageAsset> extractHydrogenAlpha(const an::ui::ExecutionScope& scope,
                                                          const an::ui::ProcessSource& source)
{
    if (an::Status mosaic = checkBayerMosaic(source.descriptor()); mosaic.failed())
    {
        return mosaic;
    }
    if (an::Status valid = refuseUnlessAllValid(source); valid.failed())
    {
        return valid;
    }
    return std::visit(
        [&](const auto& typed) { return measuredTyped(scope, typed, an::NarrowbandExtractionProduct::HydrogenAlpha); },
        source.image());
}

an::Result<an::consumer::ImageAsset> extractGreen(const an::ui::ExecutionScope& scope,
                                                  const an::ui::ProcessSource& source)
{
    if (an::Status mosaic = checkBayerMosaic(source.descriptor()); mosaic.failed())
    {
        return mosaic;
    }
    if (an::Status valid = refuseUnlessAllValid(source); valid.failed())
    {
        return valid;
    }
    return std::visit(
        [&](const auto& typed) { return measuredTyped(scope, typed, an::NarrowbandExtractionProduct::Green); },
        source.image());
}

an::Result<std::vector<an::consumer::ImageAsset>>
extractHydrogenAlphaAndOxygenIII(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
                                 an::NarrowbandExtractionScaling scaling)
{
    const std::optional<an::ImageDescriptor> stored = source.descriptor();
    if (an::Status mosaic = checkBayerMosaic(stored); mosaic.failed())
    {
        return mosaic;
    }
    if (an::Status valid = refuseUnlessAllValid(source); valid.failed())
    {
        return valid;
    }
    // The pair is worked in a floating copy of the mosaic as fractions of full scale.
    an::Result<process_utils::NormalizedCopy> working = process_utils::normalizedCopyOf(scope, source);
    if (!working.hasValue())
    {
        return working.status();
    }
    an::ImageDescriptorRequest request = stored->toRequest();
    request.sampleType = working->samples.sampleType();
    request.numericDomain = an::NumericDomain::NormalizedLinear;
    const std::optional<an::ImageDescriptor> workingDescriptor = an::ImageDescriptor::create(request);
    if (!workingDescriptor.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the mosaic cannot be described in fractions");
    }
    const an::Result<an::NarrowbandExtractionPlan> plan = an::planNarrowbandExtraction(*workingDescriptor, scaling);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const an::ImageMetadata& metadata = source.metadata();
    an::Result<std::vector<an::consumer::ImageAsset>> made = refusal(an::ErrorCode::Internal, "nothing was extracted");
    const auto extract = [&]<typename WorkingT>() {
        an::Result<an::Image<WorkingT>> hydrogenAlpha =
            an::Image<WorkingT>::create(scope.engine(), plan->hydrogenAlphaDescriptor(), an::StorageArrangement::Planar,
                                        an::StorageInitialization::Uninitialized, metadata);
        an::Result<an::Image<WorkingT>> oxygenIII =
            an::Image<WorkingT>::create(scope.engine(), plan->oxygenIIIDescriptor(), an::StorageArrangement::Planar,
                                        an::StorageInitialization::Uninitialized, metadata);
        if (!hydrogenAlpha.hasValue() || !oxygenIII.hasValue())
        {
            made = hydrogenAlpha.hasValue() ? oxygenIII.status() : hydrogenAlpha.status();
            return;
        }
        {
            std::optional<an::ImageWriteAccess<WorkingT>> alpha = hydrogenAlpha->write();
            std::optional<an::ImageWriteAccess<WorkingT>> oxygen = oxygenIII->write();
            if (!alpha.has_value() || !oxygen.has_value())
            {
                made = refusal(an::ErrorCode::NotInitialized, "a product holds nothing");
                return;
            }
            const an::Result<an::NarrowbandExtractionResult> ran = an::extractBayerHydrogenAlphaAndOxygenIII(
                scope.execution(), *plan, working->samples.samples(), working->samples.layout(), alpha->view().data(),
                alpha->view().layout(), oxygen->view().data(), oxygen->view().layout());
            if (!ran.hasValue())
            {
                made = ran.status();
                return;
            }
        }
        std::vector<an::consumer::ImageAsset> products;
        for (an::Image<WorkingT>* product : {&*hydrogenAlpha, &*oxygenIII})
        {
            an::Result<an::consumer::ImageAsset> narrowed = refusal(an::ErrorCode::Internal, "nothing was narrowed");
            (void)an::dispatchSampleType(stored->sampleType(), [&]<typename StoredT>() {
                narrowed = narrowedProduct<WorkingT, StoredT>(scope, *product, *stored, metadata, scaling);
            });
            if (!narrowed.hasValue())
            {
                made = narrowed.status();
                return;
            }
            products.push_back(std::move(*narrowed));
        }
        made = std::move(products);
    };
    if (working->samples.sampleType() == an::SampleType::Float64)
    {
        extract.template operator()<double>();
    }
    else
    {
        extract.template operator()<float>();
    }
    return made;
}

an::Result<std::vector<an::consumer::ImageAsset>>
debayerImage(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source, an::DemosaicMethod method,
             const an::ui::BayerPatternChoice& pattern, DebayerOutput output)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the view shows no image");
    }
    if (described->extent().channelCount() != 1u)
    {
        return refusal(an::ErrorCode::IncompatibleArguments,
                       "the image has more than one channel; only a one-channel mosaic is debayered");
    }
    const an::Result<an::CfaDescriptor> cfa = an::ui::resolveBayerPattern(pattern, *described);
    if (!cfa.hasValue())
    {
        return cfa.status();
    }
    an::Result<an::ui::ViewDemosaic> colour = an::ui::demosaicViewFrameWithValidity(
        scope.engine(), scope.execution(), source.image(), source.validity(), method, *cfa);
    if (!colour.hasValue())
    {
        return colour.status();
    }
    an::Result<an::consumer::ImageAsset> combined =
        an::consumer::ImageAsset::adopt(scope.session(), std::move(colour->image), std::move(colour->validity));
    if (!combined.hasValue())
    {
        return combined.status();
    }
    std::vector<an::consumer::ImageAsset> made;
    if (output != DebayerOutput::Combined)
    {
        const process_utils::ChannelAction* channels = process_utils::channelActionNamed("actionExtract_RGB_Channels");
        if (channels == nullptr)
        {
            return refusal(an::ErrorCode::Internal, "the channel table holds no red, green and blue extraction");
        }
        an::Result<an::consumer::AssetReadLease> lease = combined->read(scope.execution().stopToken());
        if (!lease.hasValue())
        {
            return lease.status();
        }
        an::Result<std::vector<an::consumer::ImageAsset>> separated =
            process_utils::runChannelAction(scope, an::ui::ProcessSource::of(*lease), *channels);
        lease->release();
        if (!separated.hasValue())
        {
            return separated.status();
        }
        if (output == DebayerOutput::Both)
        {
            made.push_back(std::move(*combined));
        }
        for (an::consumer::ImageAsset& channel : *separated)
        {
            made.push_back(std::move(channel));
        }
        return made;
    }
    made.push_back(std::move(*combined));
    return made;
}

} // namespace an::ui::processes
