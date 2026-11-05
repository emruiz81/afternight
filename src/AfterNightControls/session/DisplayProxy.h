// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// An image reduced for display: the mean of each factor-by-factor block, made once per revision and factor and read by
/// every presentation of that image at that zoom.
///
/// A view zoomed out paints a few hundred thousand pixels of an image that may hold seventy million samples. Presenting
/// every sample and letting the toolkit scale the bytes made the presentation, its conversion and the scaling cost the
/// image's size; presenting every n-th sample made them cheap but aliased stars and hot pixels. A display proxy is the
/// engine's integer reduction of the image - each pixel the Float64 mean of the valid samples of a block, returned to
/// the image's own representation - at the largest factor that keeps it at least the size the view paints, so a
/// presentation of it costs the view's size and every sample contributes to what is shown.
///
/// **No statistic is measured on a proxy.** Averaging a block shrinks the noise a median absolute deviation measures -
/// reduced by seven, the deviation of a noise-dominated frame falls to about four tenths of itself - and so would
/// change an automatic stretch. A proxy is presented through an analysis of the image it was reduced from
/// (`presentForView`), never through one of its own.
///
/// **A proxy is immutable and keyed by what it was made from**: the revision of the asset it reduced and the factor. A
/// change of the image is a new revision, so a stale proxy is never found; `DisplayProxyCache` keeps the few a view
/// presents from.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace an
{
class EngineContext;
class ExecutionContext;
} // namespace an

namespace an::ui
{

/// The largest factor a display proxy of an image may be reduced by: one that leaves it at least one pixel each way.
///
/// @param width the image's width.
/// @param height the image's height.
/// @return the factor, at least one.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::uint32_t largestDisplayProxyFactor(std::uint32_t width,
                                                                             std::uint32_t height) noexcept;

/// An image reduced by an integer factor for display. Immutable once made; shared between the GUI thread and workers.
class DisplayProxy
{
public:
    /// Reduces an image: the Float64 mean of the valid samples of every factor-by-factor block, returned to the image's
    /// own representation through the engine's conversion, with partial blocks at the right and bottom edges left out.
    ///
    /// A mosaic is reduced as one grey plane - every filter of a block in its mean - which is how a mosaic shows when
    /// zoomed out. A block with no valid sample is invalid in the proxy; a proxy every block of which is valid states
    /// no mask.
    ///
    /// @param engine the engine context the proxy's samples are taken from.
    /// @param execution the reduction's admitted execution.
    /// @param source the image, under a read lease held for the call.
    /// @param factor the reduction, from two to `largestDisplayProxyFactor` of the image.
    /// @return the proxy; `ErrorCode::OutOfRange` for a factor outside that range; or what the engine refused.
    [[nodiscard]] static AFTERNIGHTCONTROLS_API an::Result<std::shared_ptr<const DisplayProxy>>
    build(const an::EngineContext& engine, const an::ExecutionContext& execution,
          const an::consumer::AssetReadLease& source, std::uint32_t factor);

    /// Releases the samples and the mask.
    AFTERNIGHTCONTROLS_API ~DisplayProxy();

    DisplayProxy(const DisplayProxy&) = delete;
    DisplayProxy& operator=(const DisplayProxy&) = delete;
    DisplayProxy(DisplayProxy&&) = delete;
    DisplayProxy& operator=(DisplayProxy&&) = delete;

    /// How many of the image's pixels each way one pixel of the proxy is the mean of.
    [[nodiscard]] std::uint32_t factor() const noexcept
    {
        return m_factor;
    }

    /// The revision of the asset the proxy was reduced from.
    [[nodiscard]] const an::consumer::AssetRevision& source() const noexcept
    {
        return m_source;
    }

    /// What the proxy is: the image's description over the reduced extent, a mosaic stated as one grey plane.
    [[nodiscard]] const an::ImageDescriptor& descriptor() const noexcept
    {
        return m_descriptor;
    }

    /// The first byte of the proxy's samples.
    [[nodiscard]] AFTERNIGHTCONTROLS_API const void* samples() const noexcept;

    /// Where the proxy's samples sit.
    [[nodiscard]] AFTERNIGHTCONTROLS_API const an::ImageLayout& layout() const noexcept;

    /// Which of the proxy's samples mean something: every one, or the bits of its mask.
    [[nodiscard]] AFTERNIGHTCONTROLS_API an::KernelValidityView validity() const noexcept;

    /// How long the reduction took.
    [[nodiscard]] std::chrono::nanoseconds reduceTime() const noexcept
    {
        return m_reduceTime;
    }

    /// How many bytes the proxy's samples and mask hold.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t byteCount() const noexcept;

private:
    struct Storage;

    DisplayProxy(std::uint32_t factor, an::consumer::AssetRevision source, an::ImageDescriptor descriptor,
                 std::unique_ptr<Storage> storage, std::chrono::nanoseconds reduceTime) noexcept;

    std::uint32_t m_factor;
    an::consumer::AssetRevision m_source;
    an::ImageDescriptor m_descriptor;
    std::unique_ptr<Storage> m_storage;
    std::chrono::nanoseconds m_reduceTime;
};

/// The display proxies one view presents from, the most recently used kept, the oldest let go first. GUI thread only;
/// what it hands out is immutable and may be read on any thread.
class DisplayProxyCache
{
public:
    /// A cache keeping at most `capacity` proxies.
    ///
    /// @param capacity the most proxies kept; zero keeps none.
    AFTERNIGHTCONTROLS_API explicit DisplayProxyCache(std::size_t capacity = 4u);

    /// The proxy of an asset's revision at a factor, if one is kept, made the most recently used.
    ///
    /// @param source the revision.
    /// @param factor the factor.
    /// @return the proxy, or null.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::shared_ptr<const DisplayProxy>
    find(const an::consumer::AssetRevision& source, std::uint32_t factor);

    /// Keeps a proxy as the most recently used, letting the least recently used go beyond the capacity. A proxy of a
    /// revision and factor already kept replaces it.
    ///
    /// @param proxy the proxy; null changes nothing.
    AFTERNIGHTCONTROLS_API void keep(std::shared_ptr<const DisplayProxy> proxy);

    /// How many proxies are kept now.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::size_t size() const noexcept;

    /// Lets every proxy go.
    AFTERNIGHTCONTROLS_API void clear() noexcept;

private:
    std::size_t m_capacity;
    std::vector<std::shared_ptr<const DisplayProxy>> m_kept;
};

} // namespace an::ui
