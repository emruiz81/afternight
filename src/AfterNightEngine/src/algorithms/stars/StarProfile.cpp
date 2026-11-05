// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - naming, ordering and summarising a frame's detected stars.
//
// Nothing here reads a pixel. What it does is turn a list of measured stars into the few numbers a
// consumer ranks frames by, and it does so in a way that does not depend on the order the detector
// happened to produce them in: every figure is a median taken by value, and the ordering is stable.

#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "algorithms/stars/StarsInternal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace an
{

namespace
{

constexpr const char* kSummarizeOperation = "an::summarizeStarField";
constexpr const char* kSortOperation = "an::sortStars";

using stars_detail::failure;
using stars_detail::medianInPlace;

/// Whether a star's fitted widths are usable at all.
[[nodiscard]] bool hasUsableWidths(const DetectedStar& star) noexcept
{
    return std::isfinite(star.psf.fullWidthX) && std::isfinite(star.psf.fullWidthY) && star.psf.fullWidthX > 0.0 &&
           star.psf.fullWidthY > 0.0 && std::isfinite(star.metrics.meanFullWidth) && star.metrics.meanFullWidth > 0.0;
}

/// The roundness a star contributes to the frame's median, or a negative number for none.
///
/// The recorded metric when it lies in the interval a roundness can lie in, otherwise the ratio of
/// the fitted widths under the same test: two steps, because a star whose metric was never filled in
/// still has widths that say what its shape was.
[[nodiscard]] double contributedRoundness(const DetectedStar& star) noexcept
{
    if (star.metrics.roundness > 0.0 && star.metrics.roundness <= 1.0)
    {
        return star.metrics.roundness;
    }
    if (star.psf.fullWidthX > 0.0 && star.psf.fullWidthY > 0.0)
    {
        const double ratio = star.psf.fullWidthY / star.psf.fullWidthX;
        if (ratio > 0.0 && ratio <= 1.0)
        {
            return ratio;
        }
    }
    return -1.0;
}

/// Orders two optional values so that an absent one is last ascending.
///
/// @return -1 when `left` sorts first, 1 when `right` does, 0 when neither decides.
[[nodiscard]] int compareOptional(const std::optional<double>& left, const std::optional<double>& right) noexcept
{
    if (left.has_value() && right.has_value())
    {
        if (*left < *right)
        {
            return -1;
        }
        return *right < *left ? 1 : 0;
    }
    if (left.has_value())
    {
        return -1;
    }
    return right.has_value() ? 1 : 0;
}

/// Orders two doubles.
[[nodiscard]] int compareValue(double left, double right) noexcept
{
    if (left < right)
    {
        return -1;
    }
    return right < left ? 1 : 0;
}

/// Orders two stars on one key, ignoring direction.
[[nodiscard]] int compareOnKey(const DetectedStar& left, const DetectedStar& right, StarSortKey key) noexcept
{
    switch (key)
    {
    case StarSortKey::PositionX:
        return compareValue(left.positionX, right.positionX);
    case StarSortKey::PositionY:
        return compareValue(left.positionY, right.positionY);
    case StarSortKey::Background:
        return compareValue(left.psf.background, right.psf.background);
    case StarSortKey::Amplitude:
        return compareValue(left.psf.amplitude, right.psf.amplitude);
    case StarSortKey::CentreX:
        return compareValue(left.psf.centreX, right.psf.centreX);
    case StarSortKey::CentreY:
        return compareValue(left.psf.centreY, right.psf.centreY);
    case StarSortKey::ScaleX:
        return compareValue(left.psf.scaleX, right.psf.scaleX);
    case StarSortKey::ScaleY:
        return compareValue(left.psf.scaleY, right.psf.scaleY);
    case StarSortKey::FullWidthX:
        return compareValue(left.psf.fullWidthX, right.psf.fullWidthX);
    case StarSortKey::FullWidthY:
        return compareValue(left.psf.fullWidthY, right.psf.fullWidthY);
    case StarSortKey::Angle:
        return compareValue(left.psf.angleDegrees, right.psf.angleDegrees);
    case StarSortKey::ResidualRootMeanSquare:
        return compareValue(left.psf.residualRootMeanSquare, right.psf.residualRootMeanSquare);
    case StarSortKey::SaturationLevel:
        return compareValue(left.psf.saturationLevel, right.psf.saturationLevel);
    case StarSortKey::BoxRadius:
        return compareValue(static_cast<double>(left.psf.boxRadius), static_cast<double>(right.psf.boxRadius));
    case StarSortKey::Saturated:
        return compareValue(left.psf.saturated ? 1.0 : 0.0, right.psf.saturated ? 1.0 : 0.0);
    case StarSortKey::Beta:
        return compareOptional(left.psf.beta, right.psf.beta);
    case StarSortKey::Model:
        return compareValue(static_cast<double>(static_cast<std::uint8_t>(left.psf.model)),
                            static_cast<double>(static_cast<std::uint8_t>(right.psf.model)));
    case StarSortKey::InstrumentalMagnitude:
        return compareOptional(left.instrumentalMagnitude, right.instrumentalMagnitude);
    case StarSortKey::SignalToNoise:
        return compareOptional(left.signalToNoise, right.signalToNoise);
    case StarSortKey::Channel:
        return compareValue(static_cast<double>(left.channelIndex), static_cast<double>(right.channelIndex));
    case StarSortKey::Roundness:
        return compareValue(left.metrics.roundness, right.metrics.roundness);
    case StarSortKey::Circularity:
        return compareValue(left.metrics.circularity, right.metrics.circularity);
    case StarSortKey::MeanFullWidth:
        return compareValue(left.metrics.meanFullWidth, right.metrics.meanFullWidth);
    }
    return 0;
}

} // namespace

const char* psfModelName(PsfModel model) noexcept
{
    switch (model)
    {
    case PsfModel::Gaussian:
        return "Gaussian";
    case PsfModel::MoffatFixedBeta:
        return "MoffatFixedBeta";
    case PsfModel::MoffatFreeBeta:
        return "MoffatFreeBeta";
    }
    return "UnknownPsfModel";
}

const char* starSortKeyName(StarSortKey key) noexcept
{
    switch (key)
    {
    case StarSortKey::PositionX:
        return "PositionX";
    case StarSortKey::PositionY:
        return "PositionY";
    case StarSortKey::Background:
        return "Background";
    case StarSortKey::Amplitude:
        return "Amplitude";
    case StarSortKey::CentreX:
        return "CentreX";
    case StarSortKey::CentreY:
        return "CentreY";
    case StarSortKey::ScaleX:
        return "ScaleX";
    case StarSortKey::ScaleY:
        return "ScaleY";
    case StarSortKey::FullWidthX:
        return "FullWidthX";
    case StarSortKey::FullWidthY:
        return "FullWidthY";
    case StarSortKey::Angle:
        return "Angle";
    case StarSortKey::ResidualRootMeanSquare:
        return "ResidualRootMeanSquare";
    case StarSortKey::SaturationLevel:
        return "SaturationLevel";
    case StarSortKey::BoxRadius:
        return "BoxRadius";
    case StarSortKey::Saturated:
        return "Saturated";
    case StarSortKey::Beta:
        return "Beta";
    case StarSortKey::Model:
        return "Model";
    case StarSortKey::InstrumentalMagnitude:
        return "InstrumentalMagnitude";
    case StarSortKey::SignalToNoise:
        return "SignalToNoise";
    case StarSortKey::Channel:
        return "Channel";
    case StarSortKey::Roundness:
        return "Roundness";
    case StarSortKey::Circularity:
        return "Circularity";
    case StarSortKey::MeanFullWidth:
        return "MeanFullWidth";
    }
    return "UnknownStarSortKey";
}

Status sortStars(std::span<DetectedStar> stars, StarSortKey key, StarSortDirection direction)
{
    if (!isStarSortKey(key))
    {
        return failure(kSortOperation, ErrorCode::InvalidArgument, "the sort key is outside the enumeration");
    }
    if (direction != StarSortDirection::Ascending && direction != StarSortDirection::Descending)
    {
        return failure(kSortOperation, ErrorCode::InvalidArgument, "the sort direction is outside the enumeration");
    }
    if (stars.size() < 2u)
    {
        return Status::success();
    }
    const bool ascending = direction == StarSortDirection::Ascending;
    std::stable_sort(stars.begin(), stars.end(), [key, ascending](const DetectedStar& left, const DetectedStar& right) {
        const int order = compareOnKey(left, right, key);
        return ascending ? order < 0 : order > 0;
    });
    return Status::success();
}

Result<StarFieldSummary> summarizeStarField(const ExecutionContext& execution, std::span<const DetectedStar> stars,
                                            const StarChannelStatistics& channel)
{
    if (!execution.valid())
    {
        return failure(kSummarizeOperation, ErrorCode::NotInitialized, "the execution context holds nothing");
    }
    if (stars.size() > kMaxStarsPerFrame)
    {
        return failure(kSummarizeOperation, ErrorCode::LimitExceeded, "more stars than one frame may carry");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }

    StarFieldSummary summary{};
    summary.channel = channel;
    summary.starCount = static_cast<std::uint32_t>(stars.size());
    if (stars.empty())
    {
        return summary;
    }

    // Four runs of at most one double per star, taken from the engine context so that a caller's
    // memory budget accounts for a summary the same way it accounts for the detection that
    // produced the list.
    const std::uint64_t runBytes = static_cast<std::uint64_t>(stars.size()) * sizeof(double);
    const std::uint64_t alignedRun =
        (runBytes + kDefaultBufferAlignmentBytes - 1u) & ~(kDefaultBufferAlignmentBytes - 1u);
    const std::uint64_t totalBytes = alignedRun * 4u;
    MemoryResource* const memory = execution.memoryResource();
    Result<void*> taken = memory->allocate(totalBytes, kDefaultBufferAlignmentBytes);
    if (!taken.hasValue())
    {
        return taken.status();
    }
    auto* const block = static_cast<std::byte*>(*taken);
    auto* const widths = reinterpret_cast<double*>(block);
    auto* const qualityWidths = reinterpret_cast<double*>(block + alignedRun);
    auto* const roundnesses = reinterpret_cast<double*>(block + alignedRun * 2u);
    auto* const ratios = reinterpret_cast<double*>(block + alignedRun * 3u);

    std::size_t widthCount = 0u;
    std::size_t qualityCount = 0u;
    std::size_t roundnessCount = 0u;
    std::size_t qualityRoundnessCount = 0u;
    std::uint32_t saturated = 0u;
    for (const DetectedStar& star : stars)
    {
        if (star.psf.saturated)
        {
            ++saturated;
        }
        if (hasUsableWidths(star))
        {
            widths[widthCount++] = star.metrics.meanFullWidth;
        }
        const double roundness = contributedRoundness(star);
        if (roundness > 0.0)
        {
            roundnesses[roundnessCount++] = roundness;
        }
        // The frame-quality figure deliberately excludes the saturated stars: their cores are
        // clipped, so their widths measure the clipping rather than the seeing.
        if (!star.psf.saturated && hasUsableWidths(star))
        {
            qualityWidths[qualityCount++] = star.metrics.meanFullWidth;
            if (roundness > 0.0)
            {
                ratios[qualityRoundnessCount++] = roundness;
            }
        }
    }

    if (widthCount > 0u)
    {
        summary.medianFullWidth = medianInPlace(widths, widthCount);
    }
    if (roundnessCount > 0u)
    {
        summary.medianRoundness = medianInPlace(roundnesses, roundnessCount);
    }
    if (qualityCount > 0u)
    {
        const double medianWidth = medianInPlace(qualityWidths, qualityCount);
        const double medianRatio = qualityRoundnessCount > 0u ? medianInPlace(ratios, qualityRoundnessCount) : 1.0;
        if (medianWidth > 0.0)
        {
            summary.quality = static_cast<double>(qualityCount) * medianRatio / medianWidth;
        }
    }

    // The signal-to-noise run is ranked on its own pass so that it can reuse the storage the
    // quality ratios have finished with.
    if (channel.backgroundNoise > 0.0)
    {
        std::size_t signalCount = 0u;
        for (const DetectedStar& star : stars)
        {
            if (star.psf.saturated || !(star.psf.amplitude > 0.0))
            {
                continue;
            }
            ratios[signalCount++] = star.psf.amplitude / channel.backgroundNoise;
        }
        if (signalCount > 0u)
        {
            summary.medianSignalToNoise = medianInPlace(ratios, signalCount);
        }
    }

    if (summary.medianFullWidth.has_value())
    {
        std::size_t arcsecondCount = 0u;
        for (const DetectedStar& star : stars)
        {
            if (!hasUsableWidths(star) || !star.psf.fullWidthXArcseconds.has_value() ||
                !star.psf.fullWidthYArcseconds.has_value())
            {
                continue;
            }
            widths[arcsecondCount++] = 0.5 * (*star.psf.fullWidthXArcseconds + *star.psf.fullWidthYArcseconds);
        }
        if (arcsecondCount > 0u)
        {
            summary.medianFullWidthArcseconds = medianInPlace(widths, arcsecondCount);
        }
    }

    summary.saturatedStarCount = saturated;
    memory->deallocate(*taken, totalBytes, kDefaultBufferAlignmentBytes);
    return summary;
}

} // namespace an
