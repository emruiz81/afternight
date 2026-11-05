// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Finding a sensor's defective pixels, and replacing what they recorded.
///
/// A hot pixel reads high whatever the sky held and a cold one reads low, so both are found the same
/// way: a sample implausibly far from the frame's own centre. The distance that counts as implausible
/// is stated per polarity as a multiple of the frame's spread, the two polarities are enabled
/// independently, and a polarity that is not asked for simply has no threshold: an absent option is
/// absent, a present one is a number that means something, and no value of any field is a flag.
///
/// **Detection and correction are separate calls, because they run on separate frames.** The product
/// workflow detects on a master dark - where a defect is not hidden under a star - and corrects
/// every light frame of the session at the positions the dark named. What crosses from one call to
/// the other is therefore a value: one packed bit-plane per polarity per channel, in the engine's
/// own `Image/ValidityMaskView.h` shape, where a set bit selects a sample for replacement. The maps
/// are caller-owned memory, so what a detection costs to keep is a number the caller allocated -
/// one bit per pixel per polarity - rather than a list that grows with how defective the sensor is.
/// `scanImageDeviantPixels` is the counting form of the same detection, for the preview that decides
/// whether a sigma is reasonable before anything is allocated.
///
/// **The thresholds are derived from the frame in the frame's own units.** A channel's centre is its
/// exact median and its spread is its population standard deviation, both taken over the samples the
/// validity rule selects; the cold threshold is `median - coldSigma * deviation` and the hot one is
/// `median + hotSigma * deviation`, narrowed once to Float32, which is the domain the family's
/// working planes compare in. Nothing clamps a threshold to a normalized range, because the samples
/// are not required to live in one: a threshold beyond every sample simply selects nothing. A sample
/// at exactly a threshold is deviant, on both sides.
///
/// **A replacement is the median of the surviving neighbours, and never invents a structure.** A
/// marked sample becomes the median of the window around it - the centre itself excluded, a window
/// truncated at the image edge rather than filled by a border rule, a neighbour the validity rule
/// rejects left out - taken from the source as it stands, so two adjacent defects each read the
/// other's original sample rather than each other's repairs. An even surviving count resolves under
/// the stated rule, defaulting to the mean of the two middle samples, which is the family's
/// exact-median definition. A hot pixel's window and a cold one's have separate stated radii,
/// because a cold pixel - most often a cluster, not a point - is repaired from a wider
/// neighbourhood, and that asymmetry is a decision this surface keeps visible rather than a constant
/// it buries. On a mosaiced frame, `CosmeticSiteSpacing::SameCfaFilter` steps two sites per axis and
/// doubles the reach, so the window
/// ranks only samples that saw the centre's own colour and holds as many of them as the unmosaiced
/// window holds pixels.
///
/// The arrangement, precision, validity, aliasing and write-back rules are the neighbourhood
/// family's, stated in `Algorithms/Spatial/SpatialFilter.h`: any of the eight resident
/// representations on either side, Float32 working planes with the promise checked before a sample
/// is read, an invalid centre never replaced and never counted, and a destination that must be a
/// separate image.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <cstdint>
#include <optional>
#include <span>

namespace an
{

/// Which sites around a marked sample its replacement window ranks.
enum class CosmeticSiteSpacing : std::uint8_t
{
    /// Every pixel within the radius. The window for an unmosaiced frame.
    EveryPixel = 0u,

    /// Only the sites sharing the centre's filter of a two-by-two mosaic: two sites per step in
    /// each axis, reaching twice the stated radius, so the window holds as many same-filter samples
    /// as `EveryPixel` holds pixels. Ranking a red site's neighbours across all four filters would
    /// repair a defect with a colour average, which is the mistake this spacing exists to refuse.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    SameCfaFilter = 1u
};

/// Whether a value names a site spacing this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param spacing the value to inspect.
/// @return true for every implemented spacing.
[[nodiscard]] constexpr bool isCosmeticSiteSpacing(CosmeticSiteSpacing spacing) noexcept
{
    return static_cast<std::uint8_t>(spacing) <= static_cast<std::uint8_t>(CosmeticSiteSpacing::SameCfaFilter);
}

/// The spacing's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param spacing the spacing to name. A value outside the enumeration is named
///        `"UnknownCosmeticSiteSpacing"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* cosmeticSiteSpacingName(CosmeticSiteSpacing spacing) noexcept;

/// Which deviant samples a detection looks for, and how far from the centre one has to be.
///
/// Each polarity is enabled by being present. An absent polarity has no threshold, selects nothing
/// and writes nothing - there is no disabled sentinel value, because a sentinel is a number that
/// means something to one reader and nothing to the next. A detection with neither polarity present
/// is a request to find nothing and is refused as such.
struct CosmeticDetectionOptions
{
    /// How many deviations below the median a sample must sit to be a cold pixel, or absent to look
    /// for none. Must be finite and not negative when present; zero marks everything at or below
    /// the median that the hot threshold has not already marked.
    std::optional<double> coldSigma{};

    /// How many deviations above the median a sample must sit to be a hot pixel, or absent to look
    /// for none. Must be finite and not negative when present.
    std::optional<double> hotSigma{};
};

/// What one channel's detection derived and found.
///
/// The record a caller reads to judge a sigma before committing to it: the centre and spread the
/// thresholds came from, the thresholds themselves in the image's own units, and how many samples
/// each one selected. A threshold is absent exactly when its polarity was not asked for.
struct CosmeticChannelThresholds
{
    /// The channel's exact median over the samples the validity rule selects.
    double median = 0.0;

    /// The channel's population standard deviation over the same samples.
    double deviation = 0.0;

    /// The value at or below which a sample is cold, or absent when cold detection was not asked
    /// for. Derived in Float64 and narrowed once, because Float32 is the domain the comparison is
    /// made in.
    std::optional<float> coldThreshold{};

    /// The value at or above which a sample is hot, or absent when hot detection was not asked for.
    std::optional<float> hotThreshold{};

    /// How many of the channel's selected samples the cold threshold marked.
    std::uint64_t coldCount = 0u;

    /// How many of the channel's selected samples the hot threshold marked.
    std::uint64_t hotCount = 0u;
};

/// What one detection considered and found, across every channel.
struct CosmeticScanResult
{
    /// Sample selection and writes, summed over the channels. `selection.total` is pixels times
    /// channels, `selection.valid` how many of them the validity rule admitted, and
    /// `outputsWritten` how many map bits were set - which is zero for the counting scan, whose
    /// whole point is to write nothing.
    KernelRunResult run{};

    /// How many channels were scanned, which is the source's channel count.
    std::uint32_t channelCount = 0u;

    /// Samples marked cold across every channel.
    std::uint64_t coldCount = 0u;

    /// Samples marked hot across every channel.
    std::uint64_t hotCount = 0u;

    /// How many times the operation walked the whole image: the statistics passes that derived each
    /// channel's thresholds, plus one direct scan per channel.
    std::uint64_t imagePasses = 0u;

    /// The most context memory held at once, which is the statistics kernels' bounded scratch. The
    /// scan itself reads the samples where they lie and allocates nothing, whatever the
    /// representation and arrangement.
    std::uint64_t workingBytes = 0u;

    /// Workers the operation could actually use, never above its admitted concurrency.
    std::uint32_t plannedWorkers = 0u;

    /// The width the threshold comparison is made at, which this family reports as
    /// `ComputePrecision::Float32`.
    ComputePrecision precision = ComputePrecision::Float32;
};

/// What a caller asks a correction for.
struct CosmeticCorrectionRequest
{
    /// Half the replacement window for a hot sample, in sites. Must be at least one; the reach in
    /// pixels is doubled under `CosmeticSiteSpacing::SameCfaFilter` and must stay within
    /// `kMaxStencilRadius`.
    std::uint32_t hotRadius = 1u;

    /// Half the replacement window for a cold sample, in sites. Wider by default because a cold
    /// defect is more often a cluster than a point, and a window that fits inside the cluster ranks
    /// only other defects.
    std::uint32_t coldRadius = 2u;

    /// Which sites around a marked sample the window ranks.
    CosmeticSiteSpacing spacing = CosmeticSiteSpacing::EveryPixel;

    /// Which rule resolves the median of an even surviving-neighbour count.
    ///
    /// The default averages the two middle samples, in Float64 narrowed once - the family's exact
    /// median. `QuantileInterpolation::NearestRank` takes the higher middle instead and
    /// `QuantileInterpolation::LowerRank` the lower one, for a caller that wants every repair to be
    /// a value some neighbour actually recorded.
    QuantileInterpolation interpolation = QuantileInterpolation::Linear;

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// What one correction did.
struct CosmeticCorrectionResult
{
    /// The counts and the plan every neighbourhood filter reports. `run.outputsWritten` counts
    /// every destination sample the operation produced - the untouched majority carried across and
    /// the marked few replaced - and the radii report the widest reach in pixels a window actually
    /// had.
    SpatialFilterResult filter{};

    /// Marked cold samples replaced by a rank of their window.
    std::uint64_t coldReplaced = 0u;

    /// Marked hot samples replaced by a rank of their window.
    std::uint64_t hotReplaced = 0u;

    /// Marked samples left holding their source value because no window sample survived - every
    /// neighbour outside the image or rejected by the validity rule. Counted rather than treated as
    /// a failure, because a defect in an image's extreme corner is the image's shape, not the
    /// caller's mistake.
    std::uint64_t unresolved = 0u;

    /// How many samples the hot window holds when nothing truncates it, the centre excluded.
    std::uint32_t hotWindowSamples = 0u;

    /// How many samples the cold window holds when nothing truncates it, the centre excluded.
    std::uint32_t coldWindowSamples = 0u;
};

/// Counts the deviant samples of an image without materialising anything.
///
/// The preview half of a detection: it derives each channel's thresholds and counts what they
/// select, so a caller can judge a sigma - or show a person the consequence of one - before any map
/// is allocated. It writes nothing anywhere.
///
/// Per channel it takes the exact median and the population standard deviation of the samples the
/// validity rule selects, derives the enabled thresholds, and then scans the channel once, reading
/// the samples where they lie in any of the eight representations and any arrangement. A sample at
/// or below the cold threshold is cold; at or above the hot threshold, hot; one that meets both,
/// which can happen only when the thresholds meet or cross, is hot alone. A sample the validity
/// rule rejects is neither, and is not part of the statistics either. A selected sample that is
/// not finite is refused, because neither a median nor a deviation is defined over it - state a
/// validity rule that rejects it.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only
/// read. Runs on the threads the operation was admitted and checks cancellation at chunk
/// boundaries.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are.
/// @param source where the image's samples start. Must not be null.
/// @param sourceLayout where they sit. Its element size must be that representation's.
/// @param options which polarities to look for, and how far out each threshold sits.
/// @param channelThresholds where each channel's derived thresholds and counts go: empty to decline
///        them, otherwise at least one record per channel, of which the first `channelCount` are
///        written.
/// @param selection which samples mean something.
/// @return the counts; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a null buffer, a representation this build does not
///         know, a layout that does not describe it, a non-finite or negative sigma, a detection
///         with neither polarity present, or a selection whose mask disagrees with its rule;
///         `ErrorCode::IncompatibleArguments` for a non-empty threshold span shorter than the
///         channel count, carrying both counts; `ErrorCode::InvalidData` for a selected sample that
///         is not finite; `ErrorCode::EmptySelection` when a channel has no selected sample; or
///         an execution, mask, memory-budget or cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CosmeticScanResult>
scanImageDeviantPixels(const ExecutionContext& execution, SampleType sampleType, const void* source,
                       const ImageLayout& sourceLayout, const CosmeticDetectionOptions& options,
                       std::span<CosmeticChannelThresholds> channelThresholds = {},
                       const SpatialSelection& selection = {});

/// Finds the deviant samples of an image and records them in caller-owned packed maps.
///
/// The detection is `scanImageDeviantPixels` exactly - the same statistics, the same thresholds,
/// the same comparisons - with the found samples written down. Each enabled polarity requires a
/// map: a packed bit-plane per channel in the image's extent, where a set bit selects a sample for
/// replacement and the bits past the last pixel of a row are held at zero. Every bit of a required
/// map is written, so whatever the memory held before does not survive into the answer. A map for a
/// polarity that is not enabled is refused rather than ignored, because a caller that allocated one
/// believed it would be filled.
///
/// The maps are the value that crosses from a master dark to the session's light frames: detect
/// once where defects are visible, then hand the same maps to `correctImageDeviantPixels` for every
/// frame of the same geometry.
///
/// Thread-safe with respect to `execution`, the source and the selection. The maps are written
/// concurrently by the operation's workers, whole rows at a time, and must not be read or changed
/// by the caller until the call returns.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are.
/// @param source where the image's samples start. Must not be null.
/// @param sourceLayout where they sit. Its element size must be that representation's.
/// @param options which polarities to look for, and how far out each threshold sits.
/// @param coldMap where the cold samples are recorded: required exactly when `options.coldSigma` is
///        present, with the image's width and height and one bit-plane per channel. The view is
///        borrowed for the call; the bytes it describes are the caller's.
/// @param hotMap where the hot samples are recorded, under the same contract, required exactly when
///        `options.hotSigma` is present.
/// @param channelThresholds where each channel's derived thresholds and counts go: empty to decline
///        them, otherwise at least one record per channel.
/// @param selection which samples mean something.
/// @return the counts, with `run.outputsWritten` the bits set across both maps; everything
///         `scanImageDeviantPixels` reports; or `ErrorCode::IncompatibleArguments` for a map absent
///         for an enabled polarity, present for a disabled one, or not shaped as one bit-plane per
///         channel of this image.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CosmeticScanResult>
mapImageDeviantPixels(const ExecutionContext& execution, SampleType sampleType, const void* source,
                      const ImageLayout& sourceLayout, const CosmeticDetectionOptions& options,
                      const ValidityMaskView<std::uint8_t>* coldMap, const ValidityMaskView<std::uint8_t>* hotMap,
                      std::span<CosmeticChannelThresholds> channelThresholds = {},
                      const SpatialSelection& selection = {});

/// Replaces an image's marked samples with a rank of their neighbours, into a separate destination.
///
/// The destination becomes the source with the marked samples repaired: every unmarked sample is
/// carried across as the number it is, and every marked one whose centre the validity rule accepts
/// becomes the requested rank of its window - the centre excluded, the window truncated at the
/// image edge, a rejected neighbour left out, and every surviving sample read from the source as it
/// stands, so one defect's repair never reads another's. A marked sample whose window keeps nothing
/// is carried across unchanged and counted as unresolved. A centre the validity rule rejects leaves
/// its destination sample untouched, marked or not, exactly as the family rule states.
///
/// The maps say which samples are marked and nothing else; they are what a detection produced, over
/// this frame or over the session's master dark. A sample marked in both maps is repaired as cold,
/// whose window is the wider one. Working memory is the family's at most two planes plus one window
/// buffer per chunk of rows, so the cost does not grow with how defective the sensor is.
///
/// Thread-safe with respect to `execution`, the source, the maps and the selection, all of which
/// are only read. Runs on the threads the operation was admitted, checks cancellation at chunk
/// boundaries, and is not failure-atomic: a cancelled run may leave part of the destination
/// written.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source image's samples start. Must not be null.
/// @param sourceLayout where they sit. Its element size must be that representation's.
/// @param destination where the repaired samples go. Must not be null, and must not overlap the
///        source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param coldMap which samples are cold, as one bit-plane per channel in the image's extent, or
///        null when no cold map exists. Borrowed for the call.
/// @param hotMap which samples are hot, under the same contract, or null. At least one of the two
///        must be present, because a correction with neither is a copy the caller did not mean to
///        ask for.
/// @param request the window radii, spacing, rank rule and output policy.
/// @param selection which samples mean something.
/// @return the counts; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a null buffer, a representation this build does not
///         know, a layout that does not describe it, a radius of zero, an unknown spacing,
///         interpolation or selection, or a write-back policy naming `DomainMapping::FullScale`;
///         `ErrorCode::IncompatibleArguments` when the two images are not the same picture, when
///         they overlap, when the promise cannot be kept in Float32, when both maps are absent, or
///         when a present map is not shaped as one bit-plane per channel of this image;
///         `ErrorCode::OutOfRange` for a window reaching past `kMaxStencilRadius` or holding more
///         than `kMaxOrderWindowSamples` samples, carrying the request and the limit;
///         `ErrorCode::InvalidData` for a window sample that is not finite under a rule that
///         accepts it; or an execution, mask, memory-budget or cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CosmeticCorrectionResult>
correctImageDeviantPixels(const ExecutionContext& execution, SampleType sampleType, const void* source,
                          const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                          const ValidityMaskView<const std::uint8_t>* coldMap,
                          const ValidityMaskView<const std::uint8_t>* hotMap, const CosmeticCorrectionRequest& request,
                          const SpatialSelection& selection = {});

/// Counts the deviant samples of a checked typed image view.
///
/// The typed spelling of `scanImageDeviantPixels`, which owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param options which polarities to look for, and how far out each threshold sits.
/// @param channelThresholds where each channel's derived thresholds and counts go, or empty.
/// @param selection which samples mean something.
/// @return whatever `scanImageDeviantPixels` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<CosmeticScanResult>
scanDeviantPixels(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                  const CosmeticDetectionOptions& options, std::span<CosmeticChannelThresholds> channelThresholds = {},
                  const SpatialSelection& selection = {})
{
    return scanImageDeviantPixels(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), options,
                                  channelThresholds, selection);
}

/// Finds the deviant samples of a checked typed image view and records them in caller-owned maps.
///
/// The typed spelling of `mapImageDeviantPixels`, which owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param options which polarities to look for, and how far out each threshold sits.
/// @param coldMap where the cold samples are recorded, required exactly when cold detection is
///        enabled.
/// @param hotMap where the hot samples are recorded, required exactly when hot detection is
///        enabled.
/// @param channelThresholds where each channel's derived thresholds and counts go, or empty.
/// @param selection which samples mean something.
/// @return whatever `mapImageDeviantPixels` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<CosmeticScanResult>
mapDeviantPixels(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                 const CosmeticDetectionOptions& options, const ValidityMaskView<std::uint8_t>* coldMap,
                 const ValidityMaskView<std::uint8_t>* hotMap,
                 std::span<CosmeticChannelThresholds> channelThresholds = {}, const SpatialSelection& selection = {})
{
    return mapImageDeviantPixels(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), options, coldMap,
                                 hotMap, channelThresholds, selection);
}

/// Replaces a checked typed image view's marked samples with a rank of their neighbours.
///
/// The typed spelling of `correctImageDeviantPixels`, which owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param coldMap which samples are cold, or null when no cold map exists.
/// @param hotMap which samples are hot, or null. At least one of the two must be present.
/// @param request the window radii, spacing, rank rule and output policy.
/// @param selection which samples mean something.
/// @return whatever `correctImageDeviantPixels` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<CosmeticCorrectionResult>
correctDeviantPixels(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                     const ImageView<SampleT>& destination, const ValidityMaskView<const std::uint8_t>* coldMap,
                     const ValidityMaskView<const std::uint8_t>* hotMap, const CosmeticCorrectionRequest& request,
                     const SpatialSelection& selection = {})
{
    return correctImageDeviantPixels(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(),
                                     destination.data(), destination.layout(), coldMap, hotMap, request, selection);
}

} // namespace an
