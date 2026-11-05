// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Counts what a histogram widget draws, through the engine, under the caller's execution.
///
/// A widget never counts: the execution, and the engine context it leases, belong to whoever owns the image, which
/// counts on a worker and hands the widget the result. Every representation is read through its display range
/// (`widgets/DisplayNormalization.h`), and only the samples the image's validity admits are counted.
#pragma once

#include "AfterNightControlsExport.h"
#include "widgets/HistogramDisplayData.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <vector>

namespace an
{
class EngineContext;
class ExecutionContext;
} // namespace an

namespace an::ui
{

/// Counts every channel of an image into the bins a resolution names.
///
/// A sample is read as a fraction of its representation's display range. Of `n` bins, bin `i` holds the fractions in
/// `[i / (n - 1), (i + 1) / (n - 1))` and the last one full scale alone; a fraction outside the unit interval is
/// counted in the end bin it is nearest, and one at or beyond either end of the interval is also counted as clipped at
/// that end. A sample that is not a number enters no bin. A channel whose selection admits nothing has empty counts.
///
/// @param execution the operation the counting runs under.
/// @param sampleType what the samples are.
/// @param samples the first sample of the image.
/// @param layout where the samples are.
/// @param resolution how many bins.
/// @param selection which samples count: the image's validity and its mask.
/// @return the counts, or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<HistogramDisplayData>
computeHistogramDisplayData(const an::ExecutionContext& execution, an::SampleType sampleType, const void* samples,
                            const an::ImageLayout& layout, HistogramResolution resolution,
                            const an::StatisticsSelection& selection = {});

/// Counts every channel of an image read under its lease, over the samples its validity admits.
///
/// @param execution the operation the counting runs under.
/// @param image the image, read for the call.
/// @param resolution how many bins.
/// @return the counts; `ErrorCode::NotInitialized` for an image that holds nothing; or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<HistogramDisplayData>
computeHistogramDisplayData(const an::ExecutionContext& execution, const an::consumer::AssetReadLease& image,
                            HistogramResolution resolution);

/// Every code's count in each channel of an eight- or sixteen-bit image: what the histogram of a per-channel mapping of
/// the image is remapped from, without counting a sample of the mapped image.
struct CodeHistogram
{
    /// The representation whose codes were counted.
    an::SampleType sampleType = an::SampleType::UInt16;

    /// Each channel's counts, one for every code from the representation's smallest to its largest, over the samples
    /// the image's validity admitted.
    std::vector<std::vector<std::uint64_t>> channels;
};

/// How many codes a representation has whose codes are counted one by one: 256 for an eight-bit integer, 65,536 for a
/// sixteen-bit one.
///
/// @param sampleType the representation.
/// @return the count; zero for a representation of more than sixteen bits or a floating one.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::uint32_t codeCountOf(an::SampleType sampleType) noexcept;

/// Counts every code of every channel of an eight- or sixteen-bit image read under its lease, over the samples its
/// validity admits.
///
/// @param execution the operation the counting runs under.
/// @param image the image, read for the call.
/// @return the counts; `ErrorCode::Unsupported` for a representation `codeCountOf` names no count for;
///         `ErrorCode::NotInitialized` for an image that holds nothing; or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<CodeHistogram> countCodes(const an::ExecutionContext& execution,
                                                                          const an::consumer::AssetReadLease& image);

/// The histogram of an image mapped channel by channel, from the counts of its codes: every code's count in the bin its
/// image falls in, the bins and the clipped ends exactly as `computeHistogramDisplayData` decides them for the mapped
/// image itself, which is not read.
///
/// @param engine the engine context, for the row of codes the bins are found over.
/// @param execution the operation the remapping runs under.
/// @param source every code's count in each channel of the image that was mapped.
/// @param mapped every code mapped: one row of them in order in each channel, as the mapping wrote them, in the same
///        representation; read for the call.
/// @param resolution how many bins.
/// @return the counts; `ErrorCode::IncompatibleArguments` for a mapped image of another representation, channel count
///         or extent than the codes; or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<HistogramDisplayData>
remapHistogramDisplayData(const an::EngineContext& engine, const an::ExecutionContext& execution,
                          const CodeHistogram& source, const an::consumer::AssetReadLease& mapped,
                          HistogramResolution resolution);

/// The histogram of an image a part of which was replaced, from counts alone: the whole image's, less the part's, plus
/// what replaced it - every bin, both clipped ends, the samples that are not a number and those counted. Counts are
/// sums over samples, so this is exactly the count of the image with the part replaced when all three were counted at
/// one resolution, the replacement over the same channels.
///
/// @param whole the counts of the whole image.
/// @param part the counts of the part replaced, which are among the whole image's.
/// @param replacement the counts of what replaced it.
/// @return the counts; `ErrorCode::IncompatibleArguments` for counts of other channel or bin counts, or a part with
///         more in any bin than the whole image.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<HistogramDisplayData>
replacedPartHistogram(const HistogramDisplayData& whole, const HistogramDisplayData& part,
                      const HistogramDisplayData& replacement);

} // namespace an::ui
