// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The display proxy: an image reduced by the engine's integer average, and the few a view keeps.

#include "session/DisplayProxy.h"

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <type_traits>
#include <utility>

namespace an::ui
{

/// The proxy's samples and, when a block had no valid sample, its mask.
struct DisplayProxy::Storage
{
    explicit Storage(an::consumer::AnyImage reduced) : image(std::move(reduced)) {}

    an::consumer::AnyImage image;
    const void* samples = nullptr;
    std::optional<an::ImageLayout> layout{};
    std::optional<an::ValidityMask> mask{};
    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView{};
};

std::uint32_t largestDisplayProxyFactor(std::uint32_t width, std::uint32_t height) noexcept
{
    return (std::max)(1u, (std::min)(width, height));
}

DisplayProxy::DisplayProxy(std::uint32_t factor, an::consumer::AssetRevision source, an::ImageDescriptor descriptor,
                           std::unique_ptr<Storage> storage, std::chrono::nanoseconds reduceTime) noexcept
    : m_factor(factor), m_source(source), m_descriptor(descriptor), m_storage(std::move(storage)),
      m_reduceTime(reduceTime)
{
}

DisplayProxy::~DisplayProxy() = default;

const void* DisplayProxy::samples() const noexcept
{
    return m_storage->samples;
}

const an::ImageLayout& DisplayProxy::layout() const noexcept
{
    return *m_storage->layout;
}

an::KernelValidityView DisplayProxy::validity() const noexcept
{
    if (!m_storage->maskView.has_value())
    {
        return an::KernelValidityView{};
    }
    return an::KernelValidityView{.descriptor =
                                      *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask}),
                                  .mask = &*m_storage->maskView};
}

std::uint64_t DisplayProxy::byteCount() const noexcept
{
    const std::uint64_t samples = m_storage->layout.has_value() ? m_storage->layout->totalBytes() : 0u;
    const std::uint64_t mask = m_storage->maskView.has_value() ? m_storage->maskView->spannedByteCount() : 0u;
    return samples + mask;
}

an::Result<std::shared_ptr<const DisplayProxy>> DisplayProxy::build(const an::EngineContext& engine,
                                                                    const an::ExecutionContext& execution,
                                                                    const an::consumer::AssetReadLease& source,
                                                                    const std::uint32_t factor)
{
    if (!source.valid())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "a display proxy is reduced from no image");
    }
    const std::chrono::steady_clock::time_point begun = std::chrono::steady_clock::now();
    an::Result<std::shared_ptr<const DisplayProxy>> made =
        an::Status::failure(an::ErrorCode::Internal, "no display proxy was reduced");
    source.visit([&](const auto& image) {
        using SampleT = typename std::remove_cvref_t<decltype(image)>::Sample;
        const std::optional<an::ImageDescriptor> stored = image.descriptor();
        const auto view = image.view();
        if (!stored.has_value() || !view.has_value())
        {
            made = an::Status::failure(an::ErrorCode::NotInitialized, "the image holds nothing to reduce");
            return;
        }
        if (factor < 2u || factor > largestDisplayProxyFactor(stored->extent().width(), stored->extent().height()))
        {
            made = an::Status::failure(an::ErrorCode::OutOfRange,
                                       "a display proxy is reduced by at least two and leaves a pixel each way");
            return;
        }
        // A mosaic is reduced as a grey plane: a block's mean takes every filter under it.
        an::ImageDescriptorRequest flat = stored->toRequest();
        flat.cfa = an::CfaDescriptor{};
        const std::optional<an::ImageDescriptor> grey = an::ImageDescriptor::create(flat);
        if (!grey.has_value())
        {
            made = an::Status::failure(an::ErrorCode::IncompatibleArguments, "the image has no reducible description");
            return;
        }
        const an::Result<an::GeometryPlan> plan = an::planIntegerResample(
            *grey, an::IntegerResampleRequest{.factor = factor,
                                              .direction = an::IntegerResampleDirection::Reduce,
                                              .reduction = an::IntegerReduction::Average,
                                              .remainder = an::IntegerRemainderPolicy::DiscardPartialEdges});
        if (!plan.hasValue())
        {
            made = plan.status();
            return;
        }
        // Every block of an image every sample of which is valid is written; under a mask or a value rule a block with
        // nothing valid is left, so the proxy starts zeroed rather than holding whatever lay there.
        const bool everyBlockWritten = image.metadata().semantics().validity().rule() == an::ValidityRule::AllValid &&
                                       source.validity() == nullptr;
        an::Result<an::Image<SampleT>> reduced = an::Image<SampleT>::create(
            engine, plan->destinationDescriptor(), an::StorageArrangement::Planar,
            everyBlockWritten ? an::StorageInitialization::Uninitialized : an::StorageInitialization::Zeroed);
        if (!reduced.hasValue())
        {
            made = reduced.status();
            return;
        }
        an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, plan->destinationDescriptor().extent());
        if (!mask.hasValue())
        {
            made = mask.status();
            return;
        }
        std::optional<an::ValidityMaskView<std::uint8_t>> written = mask->view();
        an::GeometrySelection selection{.validity = image.metadata().semantics().validity()};
        std::optional<an::ValidityMaskView<const std::uint8_t>> sourceMask;
        if (const an::ValidityMask* validity = source.validity(); validity != nullptr)
        {
            sourceMask = validity->view();
            selection.mask = sourceMask.has_value() ? &*sourceMask : nullptr;
        }
        an::Result<an::GeometryResult> result =
            an::Status::failure(an::ErrorCode::Internal, "the image was not reduced");
        {
            std::optional<an::ImageWriteAccess<SampleT>> access = reduced->write();
            if (!access.has_value() || !written.has_value())
            {
                made = an::Status::failure(an::ErrorCode::NotInitialized, "the display proxy holds nothing");
                return;
            }
            result =
                an::transformImageGeometry(execution, an::kSampleTypeOf<SampleT>, view->data(), view->layout(),
                                           access->view().data(), access->view().layout(), *plan, selection, *written);
        }
        if (!result.hasValue())
        {
            made = result.status();
            return;
        }
        auto storage = std::make_unique<Storage>(an::consumer::AnyImage(std::move(*reduced)));
        const auto& kept = std::get<an::Image<SampleT>>(storage->image);
        const auto keptView = kept.view();
        if (!keptView.has_value())
        {
            made = an::Status::failure(an::ErrorCode::NotInitialized, "the display proxy holds nothing");
            return;
        }
        storage->samples = keptView->data();
        storage->layout = keptView->layout();
        // A proxy every block of which had a valid sample states no mask, so it is presented as an all-valid image.
        if (result->run.outputsWritten != result->run.selection.total)
        {
            storage->mask.emplace(std::move(*mask));
            storage->maskView = std::as_const(*storage->mask).view();
        }
        made = std::shared_ptr<const DisplayProxy>(new DisplayProxy(
            factor, source.revision(), plan->destinationDescriptor(), std::move(storage),
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begun)));
    });
    return made;
}

DisplayProxyCache::DisplayProxyCache(std::size_t capacity) : m_capacity(capacity) {}

std::shared_ptr<const DisplayProxy> DisplayProxyCache::find(const an::consumer::AssetRevision& source,
                                                            const std::uint32_t factor)
{
    const auto found = std::find_if(m_kept.begin(), m_kept.end(), [&](const auto& proxy) {
        return proxy->source() == source && proxy->factor() == factor;
    });
    if (found == m_kept.end())
    {
        return nullptr;
    }
    std::shared_ptr<const DisplayProxy> proxy = *found;
    m_kept.erase(found);
    m_kept.insert(m_kept.begin(), proxy);
    return proxy;
}

void DisplayProxyCache::keep(std::shared_ptr<const DisplayProxy> proxy)
{
    if (proxy == nullptr || m_capacity == 0u)
    {
        return;
    }
    std::erase_if(m_kept, [&](const auto& kept) {
        return kept->source() == proxy->source() && kept->factor() == proxy->factor();
    });
    m_kept.insert(m_kept.begin(), std::move(proxy));
    if (m_kept.size() > m_capacity)
    {
        m_kept.resize(m_capacity);
    }
}

std::size_t DisplayProxyCache::size() const noexcept
{
    return m_kept.size();
}

void DisplayProxyCache::clear() noexcept
{
    m_kept.clear();
}

} // namespace an::ui
