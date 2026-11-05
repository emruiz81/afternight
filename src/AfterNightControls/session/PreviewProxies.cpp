// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The reductions and samples of a target an interactive frame reads, and the few a dialog keeps.

#include "session/PreviewProxies.h"

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace an::ui
{
namespace
{

/// What a reduction or a sample of a target does to it: a new extent stated, the solution withdrawn, the codec's cards
/// dropped and an output decision withdrawn, as a resample's are, with the validity rule kept, because every sample of
/// it is one of the target's or a mean of them and a mask is reduced or sampled with them.
inline constexpr an::OperationEffect kPreviewProxyEffect{.description = an::DescriptionEffect::Restates,
                                                         .solution = an::SolutionEffect::Invalidates,
                                                         .codecMetadata = an::CodecMetadataEffect::Drops,
                                                         .outputIntent = an::OutputIntentEffect::Withdraws,
                                                         .validity = {.policy = an::ValidityPolicy::Preserve}};

[[nodiscard]] an::Status failure(an::ErrorCode code, const char* message)
{
    return an::Status::failure(code, message);
}

/// The target's description over another extent, or nothing when it cannot be stated.
[[nodiscard]] std::optional<an::ImageDescriptor> resized(const an::ImageDescriptor& target, std::uint32_t width,
                                                         std::uint32_t height, const an::EngineLimits& limits)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, target.extent().channelCount(), limits);
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    an::ImageDescriptorRequest request = target.toRequest();
    request.extent = *extent;
    return an::ImageDescriptor::create(request);
}

/// States the change a reduction or a sample made on what it made, and takes it over as an asset with its mask. A mask
/// the target did not state - a reduction of a target under a value rule left a block with nothing valid, whose mean
/// cannot say so under that rule, since a zero is finite and may be in range - is stated by the proxy's own rule.
template <typename SampleT>
[[nodiscard]] an::Result<an::consumer::ImageAsset> adoptProxy(const an::consumer::EngineSession& session,
                                                              an::Image<SampleT>&& made,
                                                              std::optional<an::ValidityMask> mask, const char* detail)
{
    an::Result<an::MetadataTransition> change = an::operationTransition(
        kPreviewProxyEffect, an::OperationOutcome{.operation = "an::ui::previewProxy", .detail = detail});
    if (!change.hasValue())
    {
        return change.status();
    }
    if (mask.has_value() && made.metadata().semantics().validity().rule() != an::ValidityRule::ExplicitMask)
    {
        an::ImageSemanticsRequest restated = made.metadata().semantics().toRequest();
        restated.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
        an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(restated);
        if (!semantics.hasValue())
        {
            return semantics.status();
        }
        change->semantics = std::move(*semantics);
    }
    if (an::Status stated = made.transition(*change); stated.failed())
    {
        return stated;
    }
    return an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(made)), std::move(mask));
}

} // namespace

an::Result<an::consumer::ImageAsset> reduceForPreview(const an::consumer::EngineSession& session,
                                                      const an::EngineContext& engine,
                                                      const an::ExecutionContext& execution,
                                                      const an::consumer::AssetReadLease& source,
                                                      const std::uint32_t factor)
{
    if (!source.valid())
    {
        return failure(an::ErrorCode::NotInitialized, "a preview's reduction is made of no image");
    }
    an::Result<an::consumer::ImageAsset> made =
        failure(an::ErrorCode::Internal, "the preview's reduction was not made");
    source.visit([&](const auto& image) {
        using SampleT = typename std::remove_cvref_t<decltype(image)>::Sample;
        const std::optional<an::ImageDescriptor> stored = image.descriptor();
        const auto view = image.view();
        if (!stored.has_value() || !view.has_value())
        {
            made = failure(an::ErrorCode::NotInitialized, "the image holds nothing to reduce");
            return;
        }
        if (stored->isMosaiced())
        {
            made = failure(an::ErrorCode::Unsupported, "a mosaic's filters are not kept by a reduction of it");
            return;
        }
        if (factor < 2u || factor > (std::min)(stored->extent().width(), stored->extent().height()))
        {
            made = failure(an::ErrorCode::OutOfRange,
                           "a preview's reduction is by at least two and leaves a pixel each way");
            return;
        }
        const an::Result<an::GeometryPlan> plan = an::planIntegerResample(
            *stored, an::IntegerResampleRequest{.factor = factor,
                                                .direction = an::IntegerResampleDirection::Reduce,
                                                .reduction = an::IntegerReduction::Average,
                                                .remainder = an::IntegerRemainderPolicy::DiscardPartialEdges});
        if (!plan.hasValue())
        {
            made = plan.status();
            return;
        }
        // A block with no valid sample is written nowhere, so the reduction starts zeroed.
        an::Result<an::Image<SampleT>> reduced =
            an::Image<SampleT>::createDestination(engine, plan->destinationDescriptor(), an::StorageArrangement::Planar,
                                                  an::StorageInitialization::Zeroed, image);
        if (!reduced.hasValue())
        {
            made = reduced.status();
            return;
        }
        an::Result<an::ValidityMask> written = an::ValidityMask::create(engine, plan->destinationDescriptor().extent());
        if (!written.hasValue())
        {
            made = written.status();
            return;
        }
        std::optional<an::ValidityMaskView<std::uint8_t>> writtenView = written->view();
        std::optional<an::ValidityMaskView<const std::uint8_t>> sourceMask;
        an::GeometrySelection selection{.validity = image.metadata().semantics().validity()};
        if (const an::ValidityMask* validity = source.validity(); validity != nullptr)
        {
            sourceMask = validity->view();
            selection.mask = sourceMask.has_value() ? &*sourceMask : nullptr;
        }
        bool everyBlockWritten = true;
        {
            std::optional<an::ImageWriteAccess<SampleT>> access = reduced->write();
            if (!access.has_value() || !writtenView.has_value())
            {
                made = failure(an::ErrorCode::NotInitialized, "the preview's reduction holds nothing");
                return;
            }
            const an::Result<an::GeometryResult> ran = an::transformImageGeometry(
                execution, an::kSampleTypeOf<SampleT>, view->data(), view->layout(), access->view().data(),
                access->view().layout(), *plan, selection, *writtenView);
            if (!ran.hasValue())
            {
                made = ran.status();
                return;
            }
            everyBlockWritten = ran->run.outputsWritten == ran->run.selection.total;
        }
        // The mask the reduction wrote is carried where the target states a mask, and where a block of a target under a
        // value rule had nothing valid - its zero would read as valid under that rule; a reduction every block of which
        // had a valid sample keeps the target's rule and carries none.
        std::optional<an::ValidityMask> mask;
        if (source.validity() != nullptr || !everyBlockWritten)
        {
            mask.emplace(std::move(*written));
        }
        made = adoptProxy(session, std::move(*reduced), std::move(mask), "the target reduced for a preview frame");
    });
    return made;
}

an::Result<an::consumer::ImageAsset> sampleForPreview(const an::consumer::EngineSession& session,
                                                      const an::EngineContext& engine,
                                                      const an::ExecutionContext& execution,
                                                      const an::consumer::AssetReadLease& source,
                                                      const std::uint32_t factor)
{
    (void)execution;
    if (!source.valid())
    {
        return failure(an::ErrorCode::NotInitialized, "a preview's sample is taken of no image");
    }
    an::Result<an::consumer::ImageAsset> made = failure(an::ErrorCode::Internal, "the preview's sample was not taken");
    source.visit([&](const auto& image) {
        using SampleT = typename std::remove_cvref_t<decltype(image)>::Sample;
        const std::optional<an::ImageDescriptor> stored = image.descriptor();
        const auto view = image.view();
        if (!stored.has_value() || !view.has_value())
        {
            made = failure(an::ErrorCode::NotInitialized, "the image holds nothing to sample");
            return;
        }
        if (stored->isMosaiced())
        {
            made = failure(an::ErrorCode::Unsupported, "a mosaic's filters are not kept by a sample of it");
            return;
        }
        const std::uint32_t width = stored->extent().width();
        const std::uint32_t height = stored->extent().height();
        if (factor < 1u || factor > (std::min)(width, height))
        {
            made = failure(an::ErrorCode::OutOfRange, "a preview's sample leaves a pixel each way");
            return;
        }
        const std::uint32_t sampledWidth = width / factor;
        const std::uint32_t sampledHeight = height / factor;
        const std::optional<an::ImageDescriptor> described =
            resized(*stored, sampledWidth, sampledHeight, engine.limits());
        if (!described.has_value())
        {
            made = failure(an::ErrorCode::IncompatibleArguments, "the image's sample cannot be described");
            return;
        }
        an::Result<an::Image<SampleT>> sampled = an::Image<SampleT>::createDestination(
            engine, *described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, image);
        if (!sampled.hasValue())
        {
            made = sampled.status();
            return;
        }
        {
            std::optional<an::ImageWriteAccess<SampleT>> access = sampled->write();
            if (!access.has_value())
            {
                made = failure(an::ErrorCode::NotInitialized, "the preview's sample holds nothing");
                return;
            }
            // The top left pixel of every block, read where it lies, a plane per channel or every channel in one.
            const std::uint32_t channels = stored->extent().channelCount();
            const std::uint32_t perPixel = view->samplesPerPixel();
            if (!(perPixel == 1u && view->planeCount() == channels) &&
                !(view->planeCount() == 1u && perPixel == channels))
            {
                made = failure(an::ErrorCode::Unsupported, "the image's channels are neither planar nor interleaved");
                return;
            }
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                const std::uint32_t plane = perPixel == 1u ? channel : 0u;
                const std::uint32_t within = perPixel == 1u ? 0u : channel;
                for (std::uint32_t y = 0u; y < sampledHeight; ++y)
                {
                    const std::optional<std::span<const SampleT>> from = view->row(plane, y * factor);
                    const std::optional<std::span<SampleT>> to = access->view().row(channel, y);
                    if (!from.has_value() || !to.has_value())
                    {
                        made = failure(an::ErrorCode::Internal, "a row of the image or its sample is missing");
                        return;
                    }
                    for (std::uint32_t x = 0u; x < sampledWidth; ++x)
                    {
                        (*to)[x] = (*from)[static_cast<std::size_t>(x) * factor * perPixel + within];
                    }
                }
            }
        }
        std::optional<an::ValidityMask> mask;
        if (const an::ValidityMask* validity = source.validity(); validity != nullptr)
        {
            const std::optional<an::ValidityMaskView<const std::uint8_t>> from = validity->view();
            if (!from.has_value())
            {
                made = failure(an::ErrorCode::NotInitialized, "the image's mask holds nothing");
                return;
            }
            const std::optional<an::ImageExtent> extent =
                an::ImageExtent::create(sampledWidth, sampledHeight, from->planeCount(), engine.limits());
            if (!extent.has_value())
            {
                made = failure(an::ErrorCode::IncompatibleArguments, "the image's mask cannot be sampled");
                return;
            }
            an::Result<an::ValidityMask> sampledMask = an::ValidityMask::create(engine, *extent);
            if (!sampledMask.hasValue())
            {
                made = sampledMask.status();
                return;
            }
            const std::optional<an::ValidityMaskView<std::uint8_t>> to = sampledMask->view();
            if (!to.has_value())
            {
                made = failure(an::ErrorCode::NotInitialized, "the sample's mask holds nothing");
                return;
            }
            for (std::uint32_t plane = 0u; plane < from->planeCount(); ++plane)
            {
                for (std::uint32_t y = 0u; y < sampledHeight; ++y)
                {
                    for (std::uint32_t x = 0u; x < sampledWidth; ++x)
                    {
                        (void)to->setValid(plane, x, y, from->test(plane, x * factor, y * factor).value_or(false));
                    }
                }
            }
            mask.emplace(std::move(*sampledMask));
        }
        made =
            adoptProxy(session, std::move(*sampled), std::move(mask), "the target sampled for a preview's histogram");
    });
    return made;
}

an::Result<an::consumer::ImageAsset> codeRampOf(const an::consumer::EngineSession& session,
                                                const an::EngineContext& engine,
                                                const an::consumer::AssetReadLease& source)
{
    if (!source.valid())
    {
        return failure(an::ErrorCode::NotInitialized, "a row of codes is made for no image");
    }
    an::Result<an::consumer::ImageAsset> made = failure(an::ErrorCode::Internal, "the row of codes was not made");
    source.visit([&](const auto& image) {
        using SampleT = typename std::remove_cvref_t<decltype(image)>::Sample;
        if constexpr (!std::is_integral_v<SampleT> || sizeof(SampleT) > 2u)
        {
            made = failure(an::ErrorCode::Unsupported, "only an eight- or sixteen-bit image has a row of every code");
        }
        else
        {
            const std::optional<an::ImageDescriptor> stored = image.descriptor();
            if (!stored.has_value())
            {
                made = failure(an::ErrorCode::NotInitialized, "the image holds nothing");
                return;
            }
            if (stored->isMosaiced())
            {
                made = failure(an::ErrorCode::Unsupported, "a mosaic's codes are not mapped channel by channel");
                return;
            }
            constexpr std::uint32_t kCodes = 1u << (8u * sizeof(SampleT));
            const std::optional<an::ImageDescriptor> described = resized(*stored, kCodes, 1u, engine.limits());
            if (!described.has_value())
            {
                made = failure(an::ErrorCode::IncompatibleArguments, "the image's codes cannot be described in a row");
                return;
            }
            an::Result<an::Image<SampleT>> row = an::Image<SampleT>::createDestination(
                engine, *described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, image);
            if (!row.hasValue())
            {
                made = row.status();
                return;
            }
            {
                std::optional<an::ImageWriteAccess<SampleT>> access = row->write();
                if (!access.has_value())
                {
                    made = failure(an::ErrorCode::NotInitialized, "the row of codes holds nothing");
                    return;
                }
                const auto lowest = static_cast<std::int64_t>(std::numeric_limits<SampleT>::lowest());
                for (std::uint32_t channel = 0u; channel < stored->extent().channelCount(); ++channel)
                {
                    const std::optional<std::span<SampleT>> to = access->view().row(channel, 0u);
                    if (!to.has_value())
                    {
                        made = failure(an::ErrorCode::Internal, "a row of codes is missing");
                        return;
                    }
                    for (std::uint32_t code = 0u; code < kCodes; ++code)
                    {
                        (*to)[code] = static_cast<SampleT>(lowest + static_cast<std::int64_t>(code));
                    }
                }
            }
            // A target that states a mask states one here too, every code valid.
            std::optional<an::ValidityMask> mask;
            if (const an::ValidityMask* validity = source.validity(); validity != nullptr)
            {
                const std::optional<an::ValidityMaskView<const std::uint8_t>> planes = validity->view();
                const std::optional<an::ImageExtent> extent = an::ImageExtent::create(
                    kCodes, 1u, planes.has_value() ? planes->planeCount() : 1u, engine.limits());
                if (!extent.has_value())
                {
                    made = failure(an::ErrorCode::IncompatibleArguments, "the row of codes' mask cannot be described");
                    return;
                }
                an::Result<an::ValidityMask> allValid = an::ValidityMask::createAllValid(engine, *extent);
                if (!allValid.hasValue())
                {
                    made = allValid.status();
                    return;
                }
                mask.emplace(std::move(*allValid));
            }
            made = adoptProxy(session, std::move(*row), std::move(mask), "every code of the target in a row");
        }
    });
    return made;
}

std::uint32_t previewSampleFactor(const std::uint32_t width, const std::uint32_t height) noexcept
{
    constexpr double kSampledPixels = 1.0e6;
    const double pixels = static_cast<double>(width) * static_cast<double>(height);
    if (pixels <= kSampledPixels || width == 0u || height == 0u)
    {
        return 1u;
    }
    const auto factor = static_cast<std::uint32_t>(std::floor(std::sqrt(pixels / kSampledPixels)));
    return std::clamp<std::uint32_t>(factor, 1u, (std::min)(width, height));
}

PreviewProxyCache::PreviewProxyCache(const std::size_t capacity) : m_capacity(capacity) {}

std::shared_ptr<const an::consumer::ImageAsset> PreviewProxyCache::find(const an::consumer::AssetRevision& source,
                                                                        const PreviewProxyKind kind,
                                                                        const std::uint32_t factor)
{
    return findEntry(source, kind, factor, PreviewPart{});
}

void PreviewProxyCache::keep(const an::consumer::AssetRevision& source, const PreviewProxyKind kind,
                             const std::uint32_t factor, std::shared_ptr<const an::consumer::ImageAsset> proxy)
{
    keepEntry(
        Entry{.source = source, .kind = kind, .factor = factor, .part = PreviewPart{}, .proxy = std::move(proxy)});
}

std::shared_ptr<const an::consumer::ImageAsset> PreviewProxyCache::findCrop(const an::consumer::AssetRevision& source,
                                                                            const PreviewPart& part)
{
    return findEntry(source, PreviewProxyKind::Cropped, 1u, part);
}

void PreviewProxyCache::keepCrop(const an::consumer::AssetRevision& source, const PreviewPart& part,
                                 std::shared_ptr<const an::consumer::ImageAsset> crop)
{
    keepEntry(Entry{
        .source = source, .kind = PreviewProxyKind::Cropped, .factor = 1u, .part = part, .proxy = std::move(crop)});
}

std::shared_ptr<const an::consumer::ImageAsset> PreviewProxyCache::findEntry(const an::consumer::AssetRevision& source,
                                                                             const PreviewProxyKind kind,
                                                                             const std::uint32_t factor,
                                                                             const PreviewPart& part)
{
    const auto found = std::find_if(m_kept.begin(), m_kept.end(), [&](const Entry& entry) {
        return entry.source == source && entry.kind == kind && entry.factor == factor && entry.part == part;
    });
    if (found == m_kept.end())
    {
        return nullptr;
    }
    Entry entry = std::move(*found);
    m_kept.erase(found);
    std::shared_ptr<const an::consumer::ImageAsset> proxy = entry.proxy;
    m_kept.insert(m_kept.begin(), std::move(entry));
    return proxy;
}

void PreviewProxyCache::keepEntry(Entry entry)
{
    if (entry.proxy == nullptr || m_capacity == 0u)
    {
        return;
    }
    std::erase_if(m_kept, [&](const Entry& kept) {
        return kept.source == entry.source && kept.kind == entry.kind && kept.factor == entry.factor &&
               kept.part == entry.part;
    });
    m_kept.insert(m_kept.begin(), std::move(entry));
    if (m_kept.size() > m_capacity)
    {
        m_kept.resize(m_capacity);
    }
}

std::size_t PreviewProxyCache::size() const noexcept
{
    return m_kept.size();
}

void PreviewProxyCache::clear() noexcept
{
    m_kept.clear();
}

} // namespace an::ui
