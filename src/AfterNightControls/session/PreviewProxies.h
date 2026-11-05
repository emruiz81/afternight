// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What an interactive frame of a real-time preview reads instead of its target: the target reduced to what the
/// preview view paints, which the process's body runs over, and the target sampled point by point, which the frame's
/// histogram is counted on; and the few of each a dialog keeps.
///
/// A drag shows a frame of a seventy-million-sample target in a view that paints a few hundred thousand pixels. A body
/// that maps every pixel by the same rule wherever it lies (`PreviewScaling::Decimated`) gives, over the target reduced
/// by the view's factor, what its full result would show at that factor; the settled frame that follows the drag runs
/// it over the real image again.
///
/// **A reduction is never measured.** Averaging a block shrinks the noise a median absolute deviation or a histogram's
/// width measures, so a frame's histogram is counted on the target sampled one pixel per block, never averaged, and a
/// body that measures its source takes the target's measurement on a run over either (`ProcessRun::readsViewOfTarget`).
///
/// **Both are assets of their own.** Each is a new asset of the session, with an identity and a revision that are never
/// the target's, so a cache keyed by a source's revision is never handed one for the target. Each is immutable once
/// made and kept by the revision it was made from and its factor, so a change of the target - another revision - never
/// finds a stale one.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/Result.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace an
{
class EngineContext;
class ExecutionContext;
} // namespace an

namespace an::ui
{

/// The target reduced for an interactive frame: each pixel the Float64 mean of the valid samples of a factor-by-factor
/// block, returned to the target's own representation - the engine's integer average, as the display proxy reduces -
/// with partial blocks at the right and bottom edges left out. It has the target's description over the reduced extent
/// and the target's metadata, its solution withdrawn and its codec's cards dropped as a resample's are; a mask the
/// target states is reduced with it, a block with any valid sample valid.
///
/// @param session the session the reduction belongs to.
/// @param engine the engine context of the frame's execution.
/// @param execution the frame's admitted execution.
/// @param source the target, under a read lease held for the call.
/// @param factor the reduction, from two to the target's smaller side.
/// @return the reduction, a new asset; `ErrorCode::OutOfRange` for a factor outside that range;
///         `ErrorCode::Unsupported` for a mosaic, whose filters no block mean keeps; or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset>
reduceForPreview(const an::consumer::EngineSession& session, const an::EngineContext& engine,
                 const an::ExecutionContext& execution, const an::consumer::AssetReadLease& source,
                 std::uint32_t factor);

/// The target sampled for a frame's histogram: the top left pixel of every factor-by-factor block as it is, never
/// averaged, so every distribution of the target holds to sampling noise. It has the target's description over the
/// sampled extent and its metadata, as `reduceForPreview` states them; a mask the target states is sampled with it.
///
/// @param session the session the sample belongs to.
/// @param engine the engine context of the frame's execution.
/// @param execution the frame's admitted execution.
/// @param source the target, under a read lease held for the call.
/// @param factor one pixel of how many each way, from one to the target's smaller side.
/// @return the sample, a new asset; `ErrorCode::OutOfRange` for a factor outside that range;
///         `ErrorCode::Unsupported` for a mosaic; or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset>
sampleForPreview(const an::consumer::EngineSession& session, const an::EngineContext& engine,
                 const an::ExecutionContext& execution, const an::consumer::AssetReadLease& source,
                 std::uint32_t factor);

/// Every code of an eight- or sixteen-bit target in order: one row of them in each of its channels, with the target's
/// description over that extent and its metadata, every sample valid. A body that maps each channel's samples alone,
/// run over it, maps every code once, which is all the histogram of its result over the whole target needs.
///
/// @param session the session the row belongs to.
/// @param engine the engine context of the frame's execution.
/// @param source the target, under a read lease held for the call.
/// @return the row, a new asset; `ErrorCode::Unsupported` for a representation of more than sixteen bits, a floating
///         one or a mosaic; or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset>
codeRampOf(const an::consumer::EngineSession& session, const an::EngineContext& engine,
           const an::consumer::AssetReadLease& source);

/// The factor a histogram's sample of an image is taken at: the largest that leaves it a million pixels or more, and
/// one for an image of fewer.
///
/// @param width the image's width.
/// @param height the image's height.
/// @return the factor, at least one.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::uint32_t previewSampleFactor(std::uint32_t width,
                                                                       std::uint32_t height) noexcept;

/// What a preview proxy was made as.
enum class PreviewProxyKind : std::uint8_t
{
    /// `reduceForPreview`: block means, what a body runs over.
    Reduced,

    /// `sampleForPreview`: one pixel per block, what a histogram is counted on.
    Sampled,

    /// A part of it as it is, cut by the engine's crop: what a body over the preview area, or over the region the
    /// preview view shows at full scale, runs over.
    Cropped
};

/// A rectangle of an image, in its own pixels: the part a crop was cut from.
struct PreviewPart
{
    std::uint32_t x = 0u;
    std::uint32_t y = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;

    /// Field-wise equality.
    [[nodiscard]] friend constexpr bool operator==(const PreviewPart& left,
                                                   const PreviewPart& right) noexcept = default;
};

/// The reductions, samples and crops one dialog's frames read, the most recently used kept and the oldest let go
/// first, each filed under the revision of the asset it was made from, its kind, and its factor or the part it was cut
/// from. GUI thread only; what it hands out is immutable and may be read on any thread.
class PreviewProxyCache
{
public:
    /// A cache keeping at most `capacity` entries.
    ///
    /// @param capacity the most entries kept; zero keeps none.
    AFTERNIGHTCONTROLS_API explicit PreviewProxyCache(std::size_t capacity = 8u);

    /// The proxy made of a revision as a kind at a factor, if one is kept, made the most recently used.
    ///
    /// @param source the revision it was made from.
    /// @param kind what it was made as.
    /// @param factor its factor.
    /// @return the proxy, or null.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::shared_ptr<const an::consumer::ImageAsset>
    find(const an::consumer::AssetRevision& source, PreviewProxyKind kind, std::uint32_t factor);

    /// Keeps a proxy as the most recently used, letting the least recently used go beyond the capacity. One already
    /// kept under the same revision, kind and factor is replaced.
    ///
    /// @param source the revision it was made from.
    /// @param kind what it was made as.
    /// @param factor its factor.
    /// @param proxy the proxy; null changes nothing.
    AFTERNIGHTCONTROLS_API void keep(const an::consumer::AssetRevision& source, PreviewProxyKind kind,
                                     std::uint32_t factor, std::shared_ptr<const an::consumer::ImageAsset> proxy);

    /// The crop cut from a part of a revision, if one is kept, made the most recently used.
    ///
    /// @param source the revision it was cut from.
    /// @param part the part.
    /// @return the crop, or null.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::shared_ptr<const an::consumer::ImageAsset>
    findCrop(const an::consumer::AssetRevision& source, const PreviewPart& part);

    /// Keeps a crop as the most recently used, as `keep` keeps a proxy. One already kept of the same revision and part
    /// is replaced.
    ///
    /// @param source the revision it was cut from.
    /// @param part the part.
    /// @param crop the crop; null changes nothing.
    AFTERNIGHTCONTROLS_API void keepCrop(const an::consumer::AssetRevision& source, const PreviewPart& part,
                                         std::shared_ptr<const an::consumer::ImageAsset> crop);

    /// How many entries are kept now.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::size_t size() const noexcept;

    /// Lets every entry go.
    AFTERNIGHTCONTROLS_API void clear() noexcept;

private:
    struct Entry
    {
        an::consumer::AssetRevision source;
        PreviewProxyKind kind = PreviewProxyKind::Reduced;
        std::uint32_t factor = 1u;
        PreviewPart part{};
        std::shared_ptr<const an::consumer::ImageAsset> proxy;
    };

    /// The kept entry filed under a key, made the most recently used, or null.
    std::shared_ptr<const an::consumer::ImageAsset> findEntry(const an::consumer::AssetRevision& source,
                                                              PreviewProxyKind kind, std::uint32_t factor,
                                                              const PreviewPart& part);

    /// Keeps an entry as the most recently used, replacing one of the same key.
    void keepEntry(Entry entry);

    std::size_t m_capacity;
    std::vector<Entry> m_kept;
};

} // namespace an::ui
