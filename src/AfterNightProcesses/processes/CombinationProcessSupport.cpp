// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The combination processes' shared helpers: the combined metadata and its processing-state cards, the binding of a
/// leased image for an engine composition, the second image a run reads under its own lease, and the new image a
/// composition writes into the run's session.

#include "processes/CombinationProcessSupport.h"

#include "processes/MaskProcessUtils.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessMetadataUtils.h"
#include "session/ImageReads.h"

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/IO/CombinedMetadata.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace process_utils
{

namespace
{

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

[[nodiscard]] std::optional<an::ImageDescriptor> descriptorOf(const an::consumer::AnyImage& image)
{
    return std::visit([](const auto& typed) { return typed.descriptor(); }, image);
}

[[nodiscard]] const an::ImageMetadata& metadataOf(const an::consumer::AnyImage& image)
{
    return std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, image);
}

/// The meaning a combination's result carries: the base's, every sample meaning something, and no linear encoding
/// claimed when the result is nonlinear.
[[nodiscard]] an::Result<an::ImageSemantics> resultSemantics(const an::ImageSemantics& base,
                                                             const an::RecordedLinearity linearity)
{
    an::ImageSemanticsRequest meaning = base.toRequest();
    meaning.validity = an::ValidityDescriptor::allValid();
    if (linearity == an::RecordedLinearity::Nonlinear && meaning.transferFunction == an::TransferFunction::Linear)
    {
        meaning.transferFunction = an::TransferFunction::Unspecified;
        meaning.gammaExponent.reset();
    }
    return an::ImageSemantics::create(meaning);
}

/// What the cards say of a result, decided from the record and the sources.
[[nodiscard]] an::RecordedLinearity decidedLinearity(const CombinationRecord& record,
                                                     std::span<const an::ImageMetadata* const> sources)
{
    switch (record.linearity)
    {
    case CombinationLinearity::Nonlinear:
        return an::RecordedLinearity::Nonlinear;
    case CombinationLinearity::Linear:
        return an::RecordedLinearity::Linear;
    case CombinationLinearity::AsSources:
        if (allSourcesDeclaredLinear(sources))
        {
            return an::RecordedLinearity::Linear;
        }
        if (anySourceDeclaredNonlinear(sources))
        {
            return an::RecordedLinearity::Nonlinear;
        }
        return an::RecordedLinearity::Unknown;
    case CombinationLinearity::Unrecorded:
    default:
        return an::RecordedLinearity::Unknown;
    }
}

/// The cards with the processing state the record states, or as they are when it states none.
[[nodiscard]] an::Result<an::CodecMetadata> cardsWithState(const an::CodecMetadata& cards,
                                                           const an::ImageDescriptor& described,
                                                           const CombinationRecord& record,
                                                           const an::RecordedLinearity linearity)
{
    const an::ProcessingStateRecord read = an::readProcessingStateCards(cards);
    switch (linearity)
    {
    case an::RecordedLinearity::Nonlinear:
        return an::withProcessingStateCards(cards, an::nonlinearState(read, described, record.stateOperation));
    case an::RecordedLinearity::Linear:
        return an::withProcessingStateCards(
            cards,
            an::linearState(read, an::linearChannelBasisOf(described, read.channelBasis), record.stateOperation));
    case an::RecordedLinearity::Unknown:
    default:
        return cards;
    }
}

/// Creates an image of a representation, runs the body over its planar samples and adopts it into the session.
template <typename SampleT>
[[nodiscard]] an::Result<an::consumer::ImageAsset>
composeTyped(const an::ui::ExecutionScope& scope, const an::ImageDescriptor& described, an::ImageMetadata&& metadata,
             const CompositionBody& body)
{
    an::Result<an::Image<SampleT>> created =
        an::Image<SampleT>::create(scope.engine(), described, an::StorageArrangement::Planar,
                                   an::StorageInitialization::Zeroed, std::move(metadata));
    if (!created.hasValue())
    {
        return created.status();
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = created->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the composed image holds nothing");
        }
        const an::ImageLayout layout = access->view().layout();
        if (an::Status written = body(access->view().data(), layout); written.failed())
        {
            return written;
        }
    }
    return an::consumer::ImageAsset::adopt(scope.session(), an::consumer::AnyImage(std::move(*created)));
}

/// A planar copy of an image in another representation, carrying the stated metadata.
template <typename DestT, typename SourceT>
[[nodiscard]] an::Result<an::Image<DestT>>
convertedCopy(const an::ui::ExecutionScope& scope, const an::Image<SourceT>& image,
              const an::ImageDescriptor& described, an::ImageMetadata metadata)
{
    const std::optional<an::ImageView<const SourceT>> view = image.view();
    if (!view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    an::Result<an::Image<DestT>> created =
        an::Image<DestT>::create(scope.engine(), described, an::StorageArrangement::Planar,
                                 an::StorageInitialization::Uninitialized, std::move(metadata));
    if (!created.hasValue())
    {
        return created.status();
    }
    std::optional<an::ImageWriteAccess<DestT>> access = created->write();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the copy holds nothing");
    }
    if constexpr (std::is_same_v<DestT, SourceT>)
    {
        if (an::Status copied = an::copyImage(scope.execution(), *view, access->view()); copied.failed())
        {
            return copied;
        }
    }
    else
    {
        an::ConversionCounters counters;
        if (an::Status converted = an::convertImageSamples(
                scope.execution(), an::kSampleTypeOf<SourceT>, view->data(), view->layout(), an::kSampleTypeOf<DestT>,
                access->view().data(), access->view().layout(), an::ConversionPolicy{}, counters);
            converted.failed())
        {
            return converted;
        }
    }
    access.reset();
    return created;
}

} // namespace

an::Result<an::ImageMetadata> combinedResultMetadata(std::span<const an::ImageMetadata* const> sources,
                                                     const an::OperationEffect& effect,
                                                     const an::ImageDescriptor& described,
                                                     const CombinationRecord& record)
{
    if (sources.empty() || sources.front() == nullptr)
    {
        return refusal(an::ErrorCode::InvalidArgument, "a combination has at least one source");
    }
    const an::RecordedLinearity linearity = decidedLinearity(record, sources);
    an::Result<an::ImageSemantics> semantics = resultSemantics(sources.front()->semantics(), linearity);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    // A combination's record keeps the cards every source states identically - and the first source's world
    // coordinates whole, while its solution still describes the result - whatever card effect the composition declares
    // for a single image: the identical-cards rule is what makes the kept cards describe the result, so a combination
    // never inherits a single image's withdrawal of them.
    an::OperationEffect combining = effect;
    combining.codecMetadata = an::CodecMetadataEffect::Preserves;
    an::Result<an::ImageMetadata> combined =
        combinedProcessMetadata(sources, combining, record.engineOperation, record.detail, std::move(*semantics));
    if (!combined.hasValue())
    {
        return combined.status();
    }
    an::Result<an::CodecMetadata> cards = cardsWithState(combined->codec(), described, record, linearity);
    if (!cards.hasValue())
    {
        return cards.status();
    }
    return an::ImageMetadata::create(combined->acquisition(), combined->semantics(), combined->astrometry(),
                                     combined->provenance(), std::move(*cards), combined->outputIntent());
}

an::Result<an::ImageMetadata> recombinedResultMetadata(const an::ImageMetadata& base,
                                                       const an::ImageDescriptor& described,
                                                       const CombinationRecord& record)
{
    const an::RecordedLinearity linearity = decidedLinearity(record, std::span<const an::ImageMetadata* const>{});
    const an::RecordedLinearity stated =
        record.linearity == CombinationLinearity::Nonlinear ? an::RecordedLinearity::Nonlinear : linearity;
    an::Result<an::ImageSemantics> semantics = resultSemantics(base.semantics(), stated);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    an::Result<an::ProvenanceEntry> entry = an::ProvenanceEntry::create(record.engineOperation, record.detail);
    if (!entry.hasValue())
    {
        return entry.status();
    }
    an::Result<an::Provenance> lineage = base.provenance().appended(*entry);
    if (!lineage.hasValue())
    {
        return lineage.status();
    }
    an::Result<an::CodecMetadata> cards = cardsWithState(base.codec(), described, record, stated);
    if (!cards.hasValue())
    {
        return cards.status();
    }
    return an::ImageMetadata::create(base.acquisition(), std::move(*semantics), base.astrometry(), std::move(*lineage),
                                     std::move(*cards), base.outputIntent());
}

an::Result<std::unique_ptr<BoundImage>> BoundImage::bind(const an::ui::ProcessSource& source,
                                                         const PhysicalReading physical)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    std::unique_ptr<BoundImage> bound(new BoundImage());
    bound->m_descriptor = *described;
    const bool physicalAsFractions = physical == PhysicalReading::AsFractions &&
                                     described->numericDomain() == an::NumericDomain::PhysicalScaled &&
                                     !an::isIntegerSampleType(described->sampleType());
    if (described->numericDomain() == an::NumericDomain::RawSensorCode || physicalAsFractions)
    {
        // A frame decoded in its sensor's codes, and a floating file where the composition reads fractions, is read
        // as fractions of full scale; the samples are the same memory.
        an::ImageDescriptorRequest fractions = described->toRequest();
        fractions.numericDomain = an::NumericDomain::NormalizedLinear;
        const std::optional<an::ImageDescriptor> restated = an::ImageDescriptor::create(fractions);
        if (!restated.has_value())
        {
            return refusal(an::ErrorCode::InvalidArgument, "the image cannot be read as fractions of full scale");
        }
        bound->m_descriptor = *restated;
    }
    bound->m_metadata = &source.metadata();
    bound->m_rule = source.metadata().semantics().validity();
    const bool located = std::visit(
        [&](const auto& typed) {
            const auto view = typed.view();
            if (!view.has_value())
            {
                return false;
            }
            bound->m_layout = view->layout();
            bound->m_samples = view->data();
            return true;
        },
        source.image());
    if (!located)
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    if (source.validity() != nullptr)
    {
        bound->m_mask = source.validity()->view();
    }
    return bound;
}

const an::ImageDescriptor& BoundImage::descriptor() const noexcept
{
    return *m_descriptor;
}

const an::ImageLayout& BoundImage::layout() const noexcept
{
    return *m_layout;
}

const void* BoundImage::samples() const noexcept
{
    return m_samples;
}

an::KernelValidityView BoundImage::validity() const noexcept
{
    an::KernelValidityView view;
    view.descriptor = m_rule;
    view.mask = m_mask.has_value() ? &*m_mask : nullptr;
    view.maskPlane = 0u;
    return view;
}

const an::ImageMetadata& BoundImage::metadata() const noexcept
{
    return *m_metadata;
}

an::Result<LeasedImage> LeasedImage::read(const an::ui::ExecutionScope& scope, const an::consumer::ImageAsset& asset,
                                          const an::ui::ProcessRun& run, const QRect& previewArea,
                                          const an::ImageDescriptor& target)
{
    LeasedImage leased;
    // Through the lease the run took before it was admitted, when it named the image.
    an::Result<an::consumer::AssetReadLease> lease = an::ui::readBeside(scope, asset, scope.execution().stopToken());
    if (!lease.hasValue())
    {
        return lease.status();
    }
    leased.m_lease.emplace(std::move(*lease));
    const std::optional<an::ImageDescriptor> described = descriptorOf(leased.m_lease->image());
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    // A run over a part of the target reads the target cropped to it - the preview area, or the part a frame over what
    // the preview view shows runs over; the image beside it is cropped the same way, unless it is already the part's
    // size.
    const QRect part = !run.part.isEmpty() ? run.part : (run.isForPreview() ? previewArea : QRect());
    if (!part.isEmpty() && !sameSize(*described, target) && part.width() == static_cast<int>(target.extent().width()) &&
        part.height() == static_cast<int>(target.extent().height()))
    {
        an::Result<an::consumer::ImageAsset> cropped =
            an::ui::cropProcessRegion(scope.session(), scope.engine(), scope.execution(), leased.m_lease->image(),
                                      leased.m_lease->validity(), part);
        if (!cropped.hasValue())
        {
            return cropped.status();
        }
        leased.m_cropped.emplace(std::move(*cropped));
        an::Result<an::consumer::AssetReadLease> croppedLease = leased.m_cropped->read(scope.execution().stopToken());
        if (!croppedLease.hasValue())
        {
            return croppedLease.status();
        }
        leased.m_croppedLease.emplace(std::move(*croppedLease));
    }
    return leased;
}

an::ui::ProcessSource LeasedImage::source() const noexcept
{
    if (m_croppedLease.has_value())
    {
        return an::ui::ProcessSource::of(*m_croppedLease);
    }
    return an::ui::ProcessSource::of(*m_lease);
}

std::optional<an::ImageDescriptor> LeasedImage::descriptor() const
{
    return source().descriptor();
}

bool sameSize(const an::ImageDescriptor& left, const an::ImageDescriptor& right) noexcept
{
    return left.extent().width() == right.extent().width() && left.extent().height() == right.extent().height();
}

an::Result<an::consumer::ImageAsset> composeImage(const an::ui::ExecutionScope& scope,
                                                  const an::ImageDescriptor& described, an::ImageMetadata metadata,
                                                  const CompositionBody& body)
{
    an::Result<an::consumer::ImageAsset> composed =
        refusal(an::ErrorCode::InvalidArgument, "the composed image's representation is not one an image holds");
    (void)an::dispatchSampleType(described.sampleType(), [&]<typename SampleT>() {
        composed = composeTyped<SampleT>(scope, described, std::move(metadata), body);
    });
    return composed;
}

an::Result<an::ImageDescriptor> compositionDescriptor(const an::ImageDescriptor& target, std::uint32_t channels,
                                                      an::SampleType sampleType, const an::EngineLimits& limits)
{
    if (channels != 1u && channels != 3u)
    {
        return refusal(an::ErrorCode::InvalidArgument, "a composition is grey or colour");
    }
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(target.extent().width(), target.extent().height(), channels, limits);
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the target's size cannot hold the composition");
    }
    const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(
        an::ImageDescriptorRequest{.extent = *extent,
                                   .sampleType = sampleType,
                                   .channelSchema = channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb,
                                   .colorSpace = an::ColorSpace::Unspecified,
                                   .numericDomain = an::NumericDomain::NormalizedLinear,
                                   .cfa = an::CfaDescriptor{},
                                   .coordinateOrigin = target.coordinateOrigin(),
                                   .pixelAspectRatio = target.pixelAspectRatio()});
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the composition cannot be described");
    }
    return *described;
}

an::Status restateMetadata(const an::ui::ExecutionScope& scope, an::ui::ProcessDestination& destination,
                           an::ImageMetadata metadata)
{
    if (!destination.valid())
    {
        return refusal(an::ErrorCode::NotInitialized, "the run has no result to record");
    }
    const an::consumer::AnyImage& image = destination.asset().image();
    const std::optional<an::ImageDescriptor> described = descriptorOf(image);
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the result holds nothing");
    }
    an::Result<an::consumer::AnyImage> copy = std::visit(
        [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            an::Result<an::Image<SampleT>> made =
                convertedCopy<SampleT, SampleT>(scope, typed, *described, std::move(metadata));
            if (!made.hasValue())
            {
                return made.status();
            }
            return an::consumer::AnyImage(std::move(*made));
        },
        image);
    if (!copy.hasValue())
    {
        return copy.status();
    }
    an::Result<an::consumer::ImageAsset> adopted = an::consumer::ImageAsset::adopt(scope.session(), std::move(*copy));
    if (!adopted.hasValue())
    {
        return adopted.status();
    }
    return destination.replace(std::move(*adopted));
}

an::Result<an::consumer::ImageAsset> unitFloatCopy(const an::ui::ExecutionScope& scope,
                                                   const an::ui::ProcessSource& source, an::SampleType sampleType)
{
    if (sampleType != an::SampleType::Float32 && sampleType != an::SampleType::Float64)
    {
        return refusal(an::ErrorCode::InvalidArgument, "a unit copy is Float32 or Float64");
    }
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    // A normalized copy works in fractions of full scale; its working samples are then held in the stated
    // representation.
    an::Result<NormalizedCopy> normalized = normalizedCopyOf(scope, source);
    if (!normalized.hasValue())
    {
        return normalized.status();
    }
    const an::Result<an::ImageDescriptor> working = an::describeSamplesAs(*described, sampleType);
    if (!working.hasValue())
    {
        return working.status();
    }
    an::ImageDescriptorRequest fractions = working->toRequest();
    fractions.numericDomain = an::NumericDomain::NormalizedLinear;
    const std::optional<an::ImageDescriptor> unit = an::ImageDescriptor::create(fractions);
    if (!unit.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the unit copy cannot be described");
    }
    const NormalizedSamples& samples = normalized->samples;
    const an::ImageLayout layout = samples.layout();
    an::Result<an::consumer::ImageAsset> made = refusal(an::ErrorCode::InvalidArgument, "no copy was made");
    (void)an::dispatchSampleType(sampleType, [&]<typename DestT>() {
        if constexpr (std::is_floating_point_v<DestT>)
        {
            an::ImageMetadata metadata = source.metadata();
            an::Result<an::Image<DestT>> created =
                an::Image<DestT>::create(scope.engine(), *unit, an::StorageArrangement::Planar,
                                         an::StorageInitialization::Uninitialized, std::move(metadata));
            if (!created.hasValue())
            {
                made = created.status();
                return;
            }
            {
                std::optional<an::ImageWriteAccess<DestT>> access = created->write();
                if (!access.has_value())
                {
                    made = refusal(an::ErrorCode::NotInitialized, "the unit copy holds nothing");
                    return;
                }
                an::ConversionCounters counters;
                const an::Status converted = an::convertImageSamples(
                    scope.execution(), samples.sampleType(), samples.samples(), layout, an::kSampleTypeOf<DestT>,
                    access->view().data(), access->view().layout(), an::ConversionPolicy{}, counters);
                if (converted.failed())
                {
                    made = converted;
                    return;
                }
            }
            made = an::consumer::ImageAsset::adopt(scope.session(), an::consumer::AnyImage(std::move(*created)));
        }
    });
    return made;
}

an::SampleType compositionWorkingType(an::SampleType stored) noexcept
{
    return an::isIntegerSampleType(stored) ? normalizedWorkingType(stored) : stored;
}

PhysicalReading recombinationReading(const an::ImageDescriptor& starless, const an::ImageDescriptor& stars) noexcept
{
    const auto physicalFloat = [](const an::ImageDescriptor& described) {
        return described.numericDomain() == an::NumericDomain::PhysicalScaled &&
               !an::isIntegerSampleType(described.sampleType());
    };
    return physicalFloat(starless) && physicalFloat(stars) ? PhysicalReading::AsStated : PhysicalReading::AsFractions;
}

an::Result<std::unique_ptr<CompositionSource>> bindForComposition(const an::ui::ExecutionScope& scope,
                                                                  const an::ui::ProcessSource& source,
                                                                  an::SampleType working,
                                                                  const PhysicalReading physical)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    auto made = std::make_unique<CompositionSource>();
    if (!an::isIntegerSampleType(described->sampleType()))
    {
        an::Result<std::unique_ptr<BoundImage>> bound = BoundImage::bind(source, physical);
        if (!bound.hasValue())
        {
            return bound.status();
        }
        made->bound = std::move(*bound);
        return made;
    }
    an::Result<an::consumer::ImageAsset> copy = unitFloatCopy(scope, source, working);
    if (!copy.hasValue())
    {
        return copy.status();
    }
    made->copy.emplace(std::move(*copy));
    an::Result<an::consumer::AssetReadLease> lease = made->copy->read(scope.execution().stopToken());
    if (!lease.hasValue())
    {
        return lease.status();
    }
    made->lease.emplace(std::move(*lease));
    an::Result<std::unique_ptr<BoundImage>> bound = BoundImage::bind(an::ui::ProcessSource::of(*made->lease));
    if (!bound.hasValue())
    {
        return bound.status();
    }
    made->bound = std::move(*bound);
    return made;
}

an::Result<an::consumer::ImageAsset> composeNarrowed(const an::ui::ExecutionScope& scope,
                                                     const an::ImageDescriptor& described, an::ImageMetadata metadata,
                                                     const CompositionBody& body)
{
    if (!an::isIntegerSampleType(described.sampleType()))
    {
        return composeImage(scope, described, std::move(metadata), body);
    }
    // The image in its own representation, zeroed, then filled in fractions of full scale and narrowed back.
    an::Result<an::consumer::ImageAsset> result = composeImage(
        scope, described, std::move(metadata), [](void*, const an::ImageLayout&) { return an::Status::success(); });
    if (!result.hasValue())
    {
        return result.status();
    }
    an::Result<an::consumer::AssetWriteLease> lease = result->write(scope.execution().stopToken());
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const an::SampleType working = compositionWorkingType(described.sampleType());
    const an::Status written = std::visit(
        [&](auto& typed) -> an::Status {
            auto access = typed.write();
            if (!access.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the composed image holds nothing");
            }
            const an::ImageLayout layout = access->view().layout();
            an::Result<NormalizedSamples> samples = NormalizedSamples::open(
                scope.engine(), scope.execution(), described, access->view().data(), layout, an::KernelValidityView{});
            if (!samples.hasValue())
            {
                return samples.status();
            }
            if (samples->sampleType() != working)
            {
                return refusal(an::ErrorCode::Internal, "the working samples are not in the working representation");
            }
            if (an::Status composed = body(samples->samples(), samples->layout()); composed.failed())
            {
                return composed;
            }
            return samples->commit(scope.execution());
        },
        const_cast<an::consumer::AnyImage&>(lease->image()));
    if (written.failed())
    {
        return written;
    }
    lease->release();
    return std::move(*result);
}

an::ColorEncoding colourReadingOf(const an::ImageDescriptor& described) noexcept
{
    return an::ui::processChannelScopeRequest(an::ChannelScope::CieLightness, described).sourceEncoding;
}

} // namespace process_utils
