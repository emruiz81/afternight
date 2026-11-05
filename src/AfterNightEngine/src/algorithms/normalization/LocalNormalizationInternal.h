// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - what the local-normalization stages share and nothing else sees.
//
// The five implementation files of this domain - plan, sampling, model, apply and document - read
// frames the same way, place nodes the same way and build failures the same way. Those answers are
// written once here. Nothing in this header is exported and nothing outside
// `algorithms/normalization` includes it.

#pragma once

#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

/// The helpers the local-normalization implementation files share: failure builders, frame reading
/// under the validity claim, node placement, deterministic sampling orders and the robust medians.
namespace an::local_normalization_detail
{

/// The seed every deterministic sampling order is derived from, with the geometry mixed in.
inline constexpr std::uint64_t kSamplingSeed = 0x9E3779B97F4A7C15ull;

/// The multiplier that separates one node's sampling order from its neighbours'.
inline constexpr std::uint64_t kNodeSeedMultiplier = 0xBF58476D1CE4E5B9ull;

/// The multiplier that separates one expansion step's order from another's.
inline constexpr std::uint64_t kExpansionSeedMultiplier = 0xD6E8FEB86659FD93ull;

/// The multiplier that separates one channel's scale-grid order from another's.
inline constexpr std::uint64_t kChannelSeedMultiplier = 0x94D049BB133111EBull;

/// The linear congruential multiplier the residual samplers step with.
inline constexpr std::uint64_t kLcgMultiplier = 6364136223846793005ull;

/// The linear congruential increment the residual samplers step with.
inline constexpr std::uint64_t kLcgIncrement = 1442695040888963407ull;

/// A failure of this domain with no context field.
///
/// @param operation the entry point, spelled as it is in the source.
/// @param code why.
/// @param message a sentence for a person.
/// @return the status.
[[nodiscard]] inline Status failure(const char* operation, ErrorCode code, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// A failure of this domain naming the parameter at fault.
///
/// @param operation the entry point, spelled as it is in the source.
/// @param code why.
/// @param message a sentence for a person.
/// @param parameter the parameter's name.
/// @return the status.
[[nodiscard]] inline Status parameterFailure(const char* operation, ErrorCode code, const char* message,
                                             const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// A range failure carrying the value and the limit it crossed.
///
/// @param operation the entry point, spelled as it is in the source.
/// @param message a sentence for a person.
/// @param parameter the parameter's name.
/// @param requested the value.
/// @param limit the bound.
/// @return the status.
[[nodiscard]] inline Status rangeFailure(const char* operation, const char* message, const char* parameter,
                                         std::uint64_t requested, std::uint64_t limit)
{
    return Status::failure(ErrorCode::OutOfRange,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter),
                                                    StatusContextField::ofCount(ContextKey::Requested, requested),
                                                    StatusContextField::ofCount(ContextKey::Limit, limit)}});
}

/// Where one sample of a frame sits.
///
/// @tparam SampleT the resident floating representation.
/// @param frame the checked frame.
/// @param x the column.
/// @param y the row.
/// @param channel the channel.
/// @return the sample's address.
template <typename SampleT>
[[nodiscard]] inline const SampleT* sampleAddress(const NormalizationFrameView& frame, std::uint32_t x, std::uint32_t y,
                                                  std::uint32_t channel) noexcept
{
    const ImageLayout& layout = frame.layout();
    const std::uint32_t plane = channel / layout.samplesPerPixel();
    const std::uint32_t inPixel = channel % layout.samplesPerPixel();
    const auto* base = static_cast<const std::byte*>(frame.samples());
    const std::uint64_t offset = static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * layout.rowStrideBytes() +
                                 (static_cast<std::uint64_t>(x) * layout.samplesPerPixel() + inPixel) * sizeof(SampleT);
    return reinterpret_cast<const SampleT*>(base + offset);
}

/// Where one sample of writable storage sits.
///
/// @tparam SampleT the resident floating representation.
/// @param samples where the storage starts.
/// @param layout where its samples sit.
/// @param x the column.
/// @param y the row.
/// @param channel the channel.
/// @return the sample's address.
template <typename SampleT>
[[nodiscard]] inline SampleT* sampleAddress(void* samples, const ImageLayout& layout, std::uint32_t x, std::uint32_t y,
                                            std::uint32_t channel) noexcept
{
    const std::uint32_t plane = channel / layout.samplesPerPixel();
    const std::uint32_t inPixel = channel % layout.samplesPerPixel();
    auto* base = static_cast<std::byte*>(samples);
    const std::uint64_t offset = static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * layout.rowStrideBytes() +
                                 (static_cast<std::uint64_t>(x) * layout.samplesPerPixel() + inPixel) * sizeof(SampleT);
    return reinterpret_cast<SampleT*>(base + offset);
}

/// Whether one sample takes part under the frame's validity claim.
///
/// @tparam SampleT the resident floating representation.
/// @param frame the checked frame.
/// @param value the sample.
/// @param x the column.
/// @param y the row.
/// @param channel the channel.
/// @return true when the value rule and, if there is one, the mask admit it.
template <typename SampleT>
[[nodiscard]] inline bool sampleParticipates(const NormalizationFrameView& frame, SampleT value, std::uint32_t x,
                                             std::uint32_t y, std::uint32_t channel) noexcept
{
    if (!sampleIsValidByValue(frame.validity().descriptor, value))
    {
        return false;
    }
    if (!frame.validity().descriptor.needsMask())
    {
        return true;
    }
    const ValidityMaskView<const std::uint8_t>& mask = *frame.validity().mask;
    const std::uint32_t plane = mask.isShared() ? 0u : channel;
    return mask.test(plane, x, y).value_or(false);
}

/// Reads admitted samples of one frame as Float64, whatever the frame's representation.
///
/// The representation is resolved once at construction rather than per sample; the per-sample
/// cost is one predictable branch, which the sampling stages - a few thousand reads per node -
/// can afford and the apply, which reads every sample, does not and does not pay.
class FrameReader
{
public:
    /// Binds a checked frame.
    ///
    /// @param frame the frame, which must outlive this reader.
    explicit FrameReader(const NormalizationFrameView& frame) noexcept
        : m_frame(&frame), m_double(frame.descriptor().sampleType() == SampleType::Float64)
    {
    }

    /// Reads one sample if it takes part.
    ///
    /// @param x the column.
    /// @param y the row.
    /// @param channel the channel.
    /// @param out receives the value when it takes part.
    /// @return true when the sample takes part and `out` was written.
    [[nodiscard]] bool read(std::uint32_t x, std::uint32_t y, std::uint32_t channel, double& out) const noexcept
    {
        if (m_double)
        {
            const double value = *sampleAddress<double>(*m_frame, x, y, channel);
            if (!sampleParticipates<double>(*m_frame, value, x, y, channel))
            {
                return false;
            }
            out = value;
            return true;
        }
        const float value = *sampleAddress<float>(*m_frame, x, y, channel);
        if (!sampleParticipates<float>(*m_frame, value, x, y, channel))
        {
            return false;
        }
        out = static_cast<double>(value);
        return true;
    }

private:
    /// The frame being read.
    const NormalizationFrameView* m_frame;

    /// Whether its samples are Float64.
    bool m_double;
};

/// The pixel coordinate a node sits on along one axis.
///
/// @param node the node index along the axis.
/// @param gridSize how many cells the axis has.
/// @param extent how many pixels the axis has.
/// @return the coordinate, below `extent`.
[[nodiscard]] inline std::uint32_t nodePosition(std::uint32_t node, std::uint32_t gridSize,
                                                std::uint32_t extent) noexcept
{
    if (extent <= 1u)
    {
        return 0u;
    }
    const double t = static_cast<double>(node) / static_cast<double>(gridSize);
    const double coordinate = t * static_cast<double>(extent - 1u);
    const auto rounded = static_cast<std::uint32_t>(std::llround(coordinate));
    return rounded < extent ? rounded : extent - 1u;
}

/// The first coordinate of a region centred on a position and slid inward at the edges.
///
/// @param centre the position the region is centred on.
/// @param regionExtent how many pixels the region spans.
/// @param extent how many pixels the axis has.
/// @return the region's first coordinate.
[[nodiscard]] inline std::uint32_t regionOrigin(std::uint32_t centre, std::uint32_t regionExtent,
                                                std::uint32_t extent) noexcept
{
    if (regionExtent <= 1u || extent <= 1u)
    {
        return extent == 0u ? 0u : (std::min)(centre, extent - 1u);
    }
    std::uint32_t start = centre > regionExtent / 2u ? centre - regionExtent / 2u : 0u;
    if (regionExtent <= extent && start + regionExtent > extent)
    {
        start = extent - regionExtent;
    }
    return start;
}

/// The greatest common divisor of two counts.
///
/// @param a one count.
/// @param b the other.
/// @return the divisor, which is `a` when `b` is zero.
[[nodiscard]] inline std::uint64_t greatestCommonDivisor(std::uint64_t a, std::uint64_t b) noexcept
{
    while (b != 0u)
    {
        const std::uint64_t remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

/// A step through a region's positions that visits every position exactly once before repeating.
///
/// @param area how many positions the region has.
/// @param seed what the step is derived from.
/// @return a step coprime with the area, or one.
[[nodiscard]] inline std::uint64_t permutationStep(std::uint64_t area, std::uint64_t seed) noexcept
{
    if (area <= 1u)
    {
        return 1u;
    }
    std::uint64_t step = (seed >> 32u) % area;
    if (step == 0u)
    {
        step = 1u;
    }
    for (std::uint64_t attempt = 0u; attempt < area; ++attempt)
    {
        if (greatestCommonDivisor(step, area) == 1u)
        {
            return step;
        }
        ++step;
        if (step >= area)
        {
            step = 1u;
        }
    }
    return 1u;
}

/// Visits a deterministic subset of a region's positions.
///
/// @tparam VisitT a callable taking the position's index within the region.
/// @param area how many positions the region has.
/// @param count how many to visit, at most the area.
/// @param seed what the order is derived from.
/// @param visit what to do at each.
template <typename VisitT>
inline void forEachRegionPosition(std::uint64_t area, std::uint64_t count, std::uint64_t seed, VisitT&& visit)
{
    if (area == 0u || count == 0u)
    {
        return;
    }
    const std::uint64_t visits = (std::min)(count, area);
    const std::uint64_t step = permutationStep(area, seed);
    std::uint64_t index = seed % area;
    for (std::uint64_t visited = 0u; visited < visits; ++visited)
    {
        visit(index);
        index += step;
        if (index >= area)
        {
            index %= area;
        }
    }
}

/// Advances a linear congruential state.
///
/// @param state the state, advanced in place.
/// @return the new state.
[[nodiscard]] inline std::uint64_t nextLcg(std::uint64_t& state) noexcept
{
    state = state * kLcgMultiplier + kLcgIncrement;
    return state;
}

/// The median of a run, reordering it.
///
/// @param values where the run starts.
/// @param count how many values, at least one.
/// @return the median, with an even count averaging the two middle values.
[[nodiscard]] inline double medianInPlace(double* values, std::size_t count) noexcept
{
    if (count == 0u)
    {
        return 0.0;
    }
    const std::size_t upper = count / 2u;
    std::nth_element(values, values + upper, values + count);
    const double high = values[upper];
    if (count % 2u == 1u)
    {
        return high;
    }
    std::nth_element(values, values + (upper - 1u), values + upper);
    const double low = values[upper - 1u];
    return low + 0.5 * (high - low);
}

/// A robust standard deviation of a run from the median absolute deviation.
///
/// @param values the run, left as it was.
/// @param count how many values.
/// @param scratch working room of at least `count` entries.
/// @return the deviation scaled to a standard deviation, or zero for fewer than thirty-two values
///         or a zero deviation.
[[nodiscard]] inline double robustDeviation(const double* values, std::size_t count, double* scratch) noexcept
{
    if (count < 32u)
    {
        return 0.0;
    }
    std::copy(values, values + count, scratch);
    const double median = medianInPlace(scratch, count);
    if (!std::isfinite(median))
    {
        return 0.0;
    }
    for (std::size_t index = 0u; index < count; ++index)
    {
        scratch[index] = std::abs(values[index] - median);
    }
    const double deviation = medianInPlace(scratch, count);
    if (!std::isfinite(deviation) || deviation <= 0.0)
    {
        return 0.0;
    }
    return deviation * kMedianAbsoluteDeviationToSigma;
}

/// The robust median of residual ratios: the median of the ratios within six median absolute
/// deviations of their median, clamped to `[kMinLocalNormalizationScale, kMaxLocalNormalizationScale]`.
///
/// The clipped median replaces the plain one only when the clip removed something and still kept
/// `kMinLocalNormalizationScaleRatios` ratios; a zero deviation keeps the plain median.
///
/// @param ratios the ratios, left as they were.
/// @param count how many ratios.
/// @param scratch working room of at least `count` entries.
/// @return the clamped median, or one for fewer than `kMinLocalNormalizationScaleRatios` ratios or
///         a median that is not finite and positive.
[[nodiscard]] inline double robustMedianRatio(double* ratios, std::size_t count, double* scratch) noexcept
{
    if (count < kMinLocalNormalizationScaleRatios)
    {
        return 1.0;
    }
    std::copy(ratios, ratios + count, scratch);
    double median = medianInPlace(scratch, count);
    if (!std::isfinite(median) || median <= 0.0)
    {
        return 1.0;
    }
    for (std::size_t index = 0u; index < count; ++index)
    {
        scratch[index] = std::abs(ratios[index] - median);
    }
    const double deviation = medianInPlace(scratch, count);
    if (!std::isfinite(deviation) || deviation <= 0.0)
    {
        return std::clamp(median, kMinLocalNormalizationScale, kMaxLocalNormalizationScale);
    }
    const double threshold = 6.0 * deviation + 1.0e-12;
    std::size_t kept = 0u;
    for (std::size_t index = 0u; index < count; ++index)
    {
        if (std::abs(ratios[index] - median) <= threshold)
        {
            scratch[kept++] = ratios[index];
        }
    }
    if (kept >= kMinLocalNormalizationScaleRatios && kept < count)
    {
        median = medianInPlace(scratch, kept);
    }
    if (!std::isfinite(median) || median <= 0.0)
    {
        return 1.0;
    }
    return std::clamp(median, kMinLocalNormalizationScale, kMaxLocalNormalizationScale);
}

/// The seed every position order of one frame geometry is derived from.
///
/// @param shape the grid geometry.
/// @return the seed.
[[nodiscard]] inline std::uint64_t geometrySeed(const LocalNormalizationGridShape& shape) noexcept
{
    return kSamplingSeed ^ static_cast<std::uint64_t>(shape.width) ^ (static_cast<std::uint64_t>(shape.height) << 32u) ^
           (static_cast<std::uint64_t>(shape.gridSize) << 48u);
}

/// Whether a frame has the plan's geometry.
///
/// @param plan the validated plan.
/// @param frame the checked frame.
/// @return true when the extent and channel count agree.
[[nodiscard]] inline bool frameMatchesPlan(const LocalNormalizationPlan& plan,
                                           const NormalizationFrameView& frame) noexcept
{
    const ImageExtent& extent = frame.layout().extent();
    return extent.width() == plan.shape().width && extent.height() == plan.shape().height &&
           extent.channelCount() == plan.shape().channelCount;
}

/// Whether every value of a grid is finite.
///
/// @param values the grid.
/// @return true when every value is finite.
[[nodiscard]] inline bool allFinite(std::span<const double> values) noexcept
{
    for (const double value : values)
    {
        if (!std::isfinite(value))
        {
            return false;
        }
    }
    return true;
}

/// The value one grid holds at a position, with no checks.
///
/// Interpolates exactly as the row body does: the contributing node rows are mixed at the row
/// fraction first, then the mixed columns at the column fraction.
///
/// @param shape the grid's geometry.
/// @param values the grid.
/// @param bicubic whether to interpolate cubically.
/// @param channel the channel.
/// @param x the column, clamped to the frame.
/// @param y the row, clamped to the frame.
/// @return the interpolated value.
[[nodiscard]] double evaluateGridUnchecked(const LocalNormalizationGridShape& shape, std::span<const double> values,
                                           bool bicubic, std::uint32_t channel, double x, double y) noexcept;

} // namespace an::local_normalization_detail
