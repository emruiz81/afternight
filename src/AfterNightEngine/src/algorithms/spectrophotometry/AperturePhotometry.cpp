// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - aperture photometry over a checked three-channel frame, one star or a batch.

#include <AfterNightEngine/Algorithms/Spectrophotometry/AperturePhotometry.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/spectrophotometry/SpectrophotometryInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace an
{
namespace
{

using spectrophotometry_detail::failure;
using spectrophotometry_detail::limitFailure;

constexpr const char* kSingleOperation = "an::measureAperturePhotometry";
constexpr const char* kBatchOperation = "an::measureAperturePhotometryBatch";
constexpr std::uint32_t kColorChannelCount = 3u;
/// The floor under a flux variance, so a noiseless measurement keeps a positive uncertainty and a
/// signal-to-noise ratio that is a number.
constexpr double kMinimumVariance = 1.0e-24;

/// The most samples one channel of one annulus can hold, for the scratch bound.
[[nodiscard]] std::size_t annulusCapacity(const double outerRadius) noexcept
{
    const auto side = static_cast<std::size_t>(std::ceil(outerRadius) * 2.0 + 3.0);
    return side * side;
}

/// The neighbours of one target: an explicit list and, in a batch, the other targets.
struct NeighborAccess
{
    /// The explicit neighbours.
    std::span<const ApertureNeighbor> neighbors{};

    /// The batch's targets, all of which but `selfIndex` are neighbours; empty outside a batch.
    std::span<const ApertureTarget> targets{};

    /// Which target is being measured, and is therefore not its own neighbour.
    std::size_t selfIndex = 0u;
};

/// Per-worker scratch: three channel arrays of annulus samples.
struct AnnulusScratch
{
    /// One array per channel.
    std::array<double*, kColorChannelCount> samples{};

    /// How many samples each array can hold.
    std::size_t capacity = 0u;
};

/// The radii one target is measured with.
struct ApertureGeometry
{
    /// The aperture radius, in pixels.
    double aperture = 0.0;

    /// The annulus inner radius.
    double inner = 0.0;

    /// The annulus outer radius.
    double outer = 0.0;
};

/// Whether every field of a request is inside its stated range.
///
/// @param request the request.
/// @param operation the entry point speaking.
/// @return success, or the first field that refused.
[[nodiscard]] Status validateRequest(const AperturePhotometryRequest& request, const char* const operation)
{
    const auto positive = [](const double value) { return std::isfinite(value) && value > 0.0; };
    if (!isApertureRadiusPolicy(request.radiusPolicy) || !isAnnulusRadiusPolicy(request.annulusPolicy))
    {
        return failure(ErrorCode::InvalidArgument, "the radius policies are ones this build implements", operation);
    }
    if (!positive(request.apertureRadiusPixels))
    {
        return failure(ErrorCode::InvalidArgument, "the aperture radius is finite and positive", operation);
    }
    if (!positive(request.minimumApertureRadiusPixels) || !std::isfinite(request.maximumApertureRadiusPixels) ||
        request.maximumApertureRadiusPixels < request.minimumApertureRadiusPixels)
    {
        return failure(ErrorCode::InvalidArgument, "the aperture radius bounds are finite, positive and ordered",
                       operation);
    }
    if (request.radiusPolicy == ApertureRadiusPolicy::FullWidthScaledClamped && !positive(request.fullWidthRadiusScale))
    {
        return failure(ErrorCode::InvalidArgument, "the full-width radius scale is finite and positive", operation);
    }
    if (!std::isfinite(request.annulusInnerRadiusPixels) ||
        request.annulusInnerRadiusPixels <= request.apertureRadiusPixels)
    {
        return failure(ErrorCode::InvalidArgument, "the annulus starts outside the aperture", operation);
    }
    if (!std::isfinite(request.annulusOuterRadiusPixels) ||
        request.annulusOuterRadiusPixels <= request.annulusInnerRadiusPixels ||
        request.annulusOuterRadiusPixels > kMaxApertureAnnulusOuterRadiusPixels)
    {
        return failure(ErrorCode::InvalidArgument,
                       "the annulus outer radius exceeds the inner and stays within the cap", operation);
    }
    if (request.annulusPolicy == AnnulusRadiusPolicy::FullWidthScaled &&
        (!positive(request.annulusInnerFullWidthScale) || !positive(request.minimumAnnulusInnerRadiusPixels) ||
         !positive(request.annulusWidthPixels)))
    {
        return failure(ErrorCode::InvalidArgument, "the scaled annulus parameters are finite and positive", operation);
    }
    if (!positive(request.annulusClipSigmas))
    {
        return failure(ErrorCode::InvalidArgument, "the annulus clipping width is finite and positive", operation);
    }
    if (!std::isfinite(request.neighborMaskRadiusPixels) || request.neighborMaskRadiusPixels < 0.0)
    {
        return failure(ErrorCode::InvalidArgument, "the neighbour mask radius is finite and not negative", operation);
    }
    if (!std::isfinite(request.minimumSignalToNoise) || request.minimumSignalToNoise < 0.0)
    {
        return failure(ErrorCode::InvalidArgument, "the signal-to-noise floor is finite and not negative", operation);
    }
    for (const std::optional<double>& threshold : request.saturationThresholds)
    {
        if (threshold.has_value() && !std::isfinite(*threshold))
        {
            return failure(ErrorCode::InvalidArgument, "a saturation threshold is finite when present", operation);
        }
    }
    return Status::success();
}

/// Whether the frame is one this operation measures.
///
/// @param frame the checked frame.
/// @param operation the entry point speaking.
/// @return success, or the refusal.
[[nodiscard]] Status checkFrame(const StarFrameView& frame, const char* const operation)
{
    if (!frame.valid())
    {
        return failure(ErrorCode::InvalidArgument, "aperture photometry needs a checked frame", operation);
    }
    if (!spectrophotometry_detail::isRgbFrame(frame))
    {
        return failure(ErrorCode::Unsupported, "aperture photometry needs a three-channel RGB frame", operation);
    }
    return Status::success();
}

/// The largest annulus outer radius a request can produce.
///
/// @param request the request.
/// @return the fixed outer radius, or the cap under a scaled policy.
[[nodiscard]] double maximumOuterRadius(const AperturePhotometryRequest& request) noexcept
{
    return request.annulusPolicy == AnnulusRadiusPolicy::FixedPixels ? request.annulusOuterRadiusPixels
                                                                     : kMaxApertureAnnulusOuterRadiusPixels;
}

/// The radii one target is measured with, or a geometry refusal.
///
/// @param target the star.
/// @param request the policies.
/// @param geometry receives the radii.
/// @return true when the geometry is measurable.
[[nodiscard]] bool resolveGeometry(const ApertureTarget& target, const AperturePhotometryRequest& request,
                                   ApertureGeometry& geometry) noexcept
{
    const bool needsWidth = request.radiusPolicy == ApertureRadiusPolicy::FullWidthScaledClamped ||
                            request.annulusPolicy == AnnulusRadiusPolicy::FullWidthScaled;
    if (needsWidth && (!std::isfinite(target.fullWidthPixels) || target.fullWidthPixels <= 0.0))
    {
        return false;
    }
    geometry.aperture = request.radiusPolicy == ApertureRadiusPolicy::FixedPixels
                            ? request.apertureRadiusPixels
                            : std::clamp(target.fullWidthPixels * request.fullWidthRadiusScale,
                                         request.minimumApertureRadiusPixels, request.maximumApertureRadiusPixels);
    if (request.annulusPolicy == AnnulusRadiusPolicy::FixedPixels)
    {
        geometry.inner = request.annulusInnerRadiusPixels;
        geometry.outer = request.annulusOuterRadiusPixels;
    }
    else
    {
        geometry.inner = (std::max)(request.minimumAnnulusInnerRadiusPixels,
                                    target.fullWidthPixels * request.annulusInnerFullWidthScale);
        geometry.outer = geometry.inner + request.annulusWidthPixels;
    }
    return geometry.inner > geometry.aperture && geometry.outer > geometry.inner &&
           geometry.outer <= kMaxApertureAnnulusOuterRadiusPixels;
}

/// The mask radius of one neighbour.
///
/// @param stated the neighbour's own radius, or absent.
/// @param request the default.
/// @return the radius in pixels.
[[nodiscard]] double neighborRadius(const std::optional<double>& stated,
                                    const AperturePhotometryRequest& request) noexcept
{
    return stated.has_value() && std::isfinite(*stated) && *stated > 0.0 ? *stated : request.neighborMaskRadiusPixels;
}

/// Calls a body for every neighbour of a target.
///
/// @tparam BodyT a callable taking a position and a mask radius and returning whether to continue.
/// @param access the neighbours.
/// @param request the default mask radius.
/// @param body what to call.
template <typename BodyT>
void forEachNeighbor(const NeighborAccess& access, const AperturePhotometryRequest& request, BodyT&& body)
{
    for (const ApertureNeighbor& neighbor : access.neighbors)
    {
        if (!body(neighbor.position, neighborRadius(neighbor.maskRadiusPixels, request)))
        {
            return;
        }
    }
    for (std::size_t index = 0u; index < access.targets.size(); ++index)
    {
        if (index == access.selfIndex)
        {
            continue;
        }
        if (!body(access.targets[index].position, request.neighborMaskRadiusPixels))
        {
            return;
        }
    }
}

/// Whether any neighbour's mask covers a pixel centre.
///
/// @param access the neighbours.
/// @param request the default mask radius.
/// @param x the pixel centre's column.
/// @param y the pixel centre's row.
/// @return true when masked.
[[nodiscard]] bool neighborMasks(const NeighborAccess& access, const AperturePhotometryRequest& request, const double x,
                                 const double y)
{
    bool masked = false;
    forEachNeighbor(access, request, [&](const PixelCoordinate& position, const double radius) {
        if (std::isfinite(position.x) && std::isfinite(position.y))
        {
            const double dx = position.x - x;
            const double dy = position.y - y;
            if (dx * dx + dy * dy <= radius * radius)
            {
                masked = true;
                return false;
            }
        }
        return true;
    });
    return masked;
}

/// How many neighbours' masks reach the aperture.
///
/// @param access the neighbours.
/// @param request the default mask radius.
/// @param target the star.
/// @param apertureRadius the aperture radius.
/// @return the count.
[[nodiscard]] std::uint32_t overlappingNeighbors(const NeighborAccess& access, const AperturePhotometryRequest& request,
                                                 const ApertureTarget& target, const double apertureRadius)
{
    std::uint32_t count = 0u;
    forEachNeighbor(access, request, [&](const PixelCoordinate& position, const double radius) {
        if (std::isfinite(position.x) && std::isfinite(position.y) &&
            std::hypot(position.x - target.position.x, position.y - target.position.y) <= apertureRadius + radius)
        {
            ++count;
        }
        return true;
    });
    return count;
}

/// The fraction of one pixel inside the aperture.
///
/// A pixel entirely inside or outside is decided by its centre distance and the half-diagonal; one
/// the circle crosses is sub-sampled on a fixed grid.
///
/// @param centerX the aperture centre's column.
/// @param centerY the aperture centre's row.
/// @param radius the aperture radius.
/// @param pixelX the pixel centre's column.
/// @param pixelY the pixel centre's row.
/// @param partial whether edge pixels are fractional.
/// @return the weight in `[0, 1]`.
[[nodiscard]] double pixelWeight(const double centerX, const double centerY, const double radius, const double pixelX,
                                 const double pixelY, const bool partial) noexcept
{
    const double distance = std::hypot(pixelX - centerX, pixelY - centerY);
    if (!partial)
    {
        return distance <= radius ? 1.0 : 0.0;
    }
    constexpr double kHalfDiagonal = 0.70710678118654752;
    if (distance + kHalfDiagonal <= radius)
    {
        return 1.0;
    }
    if (distance - kHalfDiagonal >= radius)
    {
        return 0.0;
    }
    std::uint32_t inside = 0u;
    const double step = 1.0 / static_cast<double>(kAperturePartialPixelSubsamples);
    for (std::uint32_t sampleY = 0u; sampleY < kAperturePartialPixelSubsamples; ++sampleY)
    {
        const double y = pixelY - 0.5 + (static_cast<double>(sampleY) + 0.5) * step;
        for (std::uint32_t sampleX = 0u; sampleX < kAperturePartialPixelSubsamples; ++sampleX)
        {
            const double x = pixelX - 0.5 + (static_cast<double>(sampleX) + 0.5) * step;
            const double dx = x - centerX;
            const double dy = y - centerY;
            if (dx * dx + dy * dy <= radius * radius)
            {
                ++inside;
            }
        }
    }
    return static_cast<double>(inside) /
           static_cast<double>(kAperturePartialPixelSubsamples * kAperturePartialPixelSubsamples);
}

/// The exact median of a non-empty range this call may reorder.
///
/// @param values the values.
/// @return the median.
[[nodiscard]] double medianOf(const std::span<double> values) noexcept
{
    const std::size_t middle = values.size() / 2u;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    double result = values[middle];
    if ((values.size() % 2u) == 0u)
    {
        const auto lower = std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
        result = (*lower + result) * 0.5;
    }
    return result;
}

/// The mean and sample variance of a range.
///
/// @param values the values.
/// @param mean receives the mean.
/// @param variance receives the sample variance, zero for fewer than two values.
void meanAndVariance(const std::span<const double> values, double& mean, double& variance) noexcept
{
    double sum = 0.0;
    for (const double value : values)
    {
        sum += value;
    }
    mean = values.empty() ? 0.0 : sum / static_cast<double>(values.size());
    variance = 0.0;
    if (values.size() < 2u)
    {
        return;
    }
    double squares = 0.0;
    for (const double value : values)
    {
        const double delta = value - mean;
        squares += delta * delta;
    }
    variance = squares / static_cast<double>(values.size() - 1u);
}

/// Clips a sample set once about its mean, in place.
///
/// @param values the samples, compacted in place.
/// @param count how many are meaningful; updated.
/// @param sigmas how many standard deviations are retained.
void clipInPlace(double* const values, std::size_t& count, const double sigmas) noexcept
{
    if (count < 3u)
    {
        return;
    }
    double mean = 0.0;
    double variance = 0.0;
    meanAndVariance(std::span<const double>{values, count}, mean, variance);
    const double deviation = std::sqrt(variance);
    if (!std::isfinite(deviation) || deviation <= 0.0)
    {
        return;
    }
    const double limit = sigmas * deviation;
    std::size_t kept = 0u;
    for (std::size_t index = 0u; index < count; ++index)
    {
        if (std::abs(values[index] - mean) <= limit)
        {
            values[kept++] = values[index];
        }
    }
    if (kept > 0u)
    {
        count = kept;
    }
}

/// Measures one target with one representation.
///
/// @tparam SampleT the frame's representation.
/// @param frame the checked frame.
/// @param target the star.
/// @param access its neighbours.
/// @param request the policies.
/// @param scratch this worker's annulus arrays.
/// @param out receives the measurement.
template <typename SampleT>
void measureTyped(const StarFrameView& frame, const ApertureTarget& target, const NeighborAccess& access,
                  const AperturePhotometryRequest& request, const AnnulusScratch& scratch,
                  AperturePhotometryMeasurement& out)
{
    out = AperturePhotometryMeasurement{};
    out.position = target.position;
    ApertureGeometry geometry{};
    if (!std::isfinite(target.position.x) || !std::isfinite(target.position.y) ||
        !resolveGeometry(target, request, geometry))
    {
        out.rejection = ApertureRejectionReason::InvalidGeometry;
        return;
    }
    out.apertureRadiusPixels = geometry.aperture;
    out.annulusInnerRadiusPixels = geometry.inner;
    out.annulusOuterRadiusPixels = geometry.outer;

    const ImageLayout& layout = frame.layout();
    const auto width = static_cast<double>(layout.extent().width());
    const auto height = static_cast<double>(layout.extent().height());
    const double x = target.position.x;
    const double y = target.position.y;
    if (request.rejectEdgeClipped && (x - geometry.outer < 0.0 || y - geometry.outer < 0.0 ||
                                      x + geometry.outer > width || y + geometry.outer > height))
    {
        out.rejection = ApertureRejectionReason::EdgeClipped;
        return;
    }
    out.overlappingNeighborCount = overlappingNeighbors(access, request, target, geometry.aperture);
    if (request.rejectBlends && out.overlappingNeighborCount > 0u)
    {
        out.rejection = ApertureRejectionReason::Blended;
        return;
    }

    const std::array<spectrophotometry_detail::ChannelReader<SampleT>, kColorChannelCount> readers{
        spectrophotometry_detail::ChannelReader<SampleT>{frame, 0u},
        spectrophotometry_detail::ChannelReader<SampleT>{frame, 1u},
        spectrophotometry_detail::ChannelReader<SampleT>{frame, 2u}};
    const auto clampRow = [&](const double value, const double extent) {
        return static_cast<std::int64_t>(std::clamp(std::floor(value), -1.0, extent));
    };
    const std::int64_t minimumX = (std::max)(std::int64_t{0}, clampRow(x - geometry.outer - 1.0, width));
    const std::int64_t maximumX =
        (std::min)(static_cast<std::int64_t>(width) - 1, clampRow(x + geometry.outer + 1.0, width));
    const std::int64_t minimumY = (std::max)(std::int64_t{0}, clampRow(y - geometry.outer - 1.0, height));
    const std::int64_t maximumY =
        (std::min)(static_cast<std::int64_t>(height) - 1, clampRow(y + geometry.outer + 1.0, height));
    const double innerSquared = geometry.inner * geometry.inner;
    const double outerSquared = geometry.outer * geometry.outer;

    std::array<std::size_t, kColorChannelCount> counts{};
    for (std::int64_t row = minimumY; row <= maximumY; ++row)
    {
        const double pixelY = static_cast<double>(row) + 0.5;
        for (std::int64_t column = minimumX; column <= maximumX; ++column)
        {
            const double pixelX = static_cast<double>(column) + 0.5;
            const double dx = pixelX - x;
            const double dy = pixelY - y;
            const double squared = dx * dx + dy * dy;
            if (squared < innerSquared || squared > outerSquared || neighborMasks(access, request, pixelX, pixelY))
            {
                continue;
            }
            for (std::uint32_t channel = 0u; channel < kColorChannelCount; ++channel)
            {
                double value = 0.0;
                if (counts[channel] < scratch.capacity &&
                    readers[channel].read(static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(row), value) &&
                    std::isfinite(value))
                {
                    scratch.samples[channel][counts[channel]++] = value;
                }
            }
        }
    }

    std::array<double, kColorChannelCount> backgroundVariance{};
    for (std::uint32_t channel = 0u; channel < kColorChannelCount; ++channel)
    {
        clipInPlace(scratch.samples[channel], counts[channel], request.annulusClipSigmas);
        if (counts[channel] < kMinimumApertureBackgroundSamples)
        {
            out.rejection = ApertureRejectionReason::InsufficientBackgroundSamples;
            return;
        }
        double mean = 0.0;
        meanAndVariance(std::span<const double>{scratch.samples[channel], counts[channel]}, mean,
                        backgroundVariance[channel]);
        out.channels[channel].localBackground = medianOf(std::span<double>{scratch.samples[channel], counts[channel]});
    }
    out.backgroundSampleCount = static_cast<std::uint32_t>(counts[0]);

    const std::int64_t apertureMinimumX = (std::max)(std::int64_t{0}, clampRow(x - geometry.aperture - 1.0, width));
    const std::int64_t apertureMaximumX =
        (std::min)(static_cast<std::int64_t>(width) - 1, clampRow(x + geometry.aperture + 1.0, width));
    const std::int64_t apertureMinimumY = (std::max)(std::int64_t{0}, clampRow(y - geometry.aperture - 1.0, height));
    const std::int64_t apertureMaximumY =
        (std::min)(static_cast<std::int64_t>(height) - 1, clampRow(y + geometry.aperture + 1.0, height));
    std::array<double, kColorChannelCount> sums{};
    double area = 0.0;
    for (std::int64_t row = apertureMinimumY; row <= apertureMaximumY; ++row)
    {
        const double pixelY = static_cast<double>(row) + 0.5;
        for (std::int64_t column = apertureMinimumX; column <= apertureMaximumX; ++column)
        {
            const double pixelX = static_cast<double>(column) + 0.5;
            const double weight = pixelWeight(x, y, geometry.aperture, pixelX, pixelY, request.usePartialPixels);
            if (weight <= 0.0)
            {
                continue;
            }
            std::array<double, kColorChannelCount> values{};
            bool admitted = true;
            for (std::uint32_t channel = 0u; channel < kColorChannelCount && admitted; ++channel)
            {
                admitted = readers[channel].read(static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(row),
                                                 values[channel]);
                if (admitted && !std::isfinite(values[channel]))
                {
                    out.rejection = ApertureRejectionReason::BadPixel;
                    return;
                }
            }
            if (!admitted)
            {
                if (request.rejectBadPixels)
                {
                    out.rejection = ApertureRejectionReason::BadPixel;
                    return;
                }
                continue;
            }
            area += weight;
            for (std::uint32_t channel = 0u; channel < kColorChannelCount; ++channel)
            {
                const std::optional<double>& threshold = request.saturationThresholds[channel];
                if (threshold.has_value() && values[channel] >= *threshold)
                {
                    out.channels[channel].saturated = true;
                }
                sums[channel] += values[channel] * weight;
            }
        }
    }
    out.apertureArea = area;
    if (request.rejectSaturated)
    {
        for (const ApertureChannelMeasurement& channel : out.channels)
        {
            if (channel.saturated)
            {
                out.rejection = ApertureRejectionReason::Saturated;
                return;
            }
        }
    }
    for (std::uint32_t channel = 0u; channel < kColorChannelCount; ++channel)
    {
        ApertureChannelMeasurement& measurement = out.channels[channel];
        measurement.flux = sums[channel] - measurement.localBackground * area;
        const auto samples = static_cast<double>(counts[channel]);
        const double variance = (std::max)(sums[channel], 0.0) + area * backgroundVariance[channel] +
                                (area * area * backgroundVariance[channel]) / samples;
        measurement.uncertainty = std::sqrt((std::max)(variance, kMinimumVariance));
        measurement.signalToNoise = measurement.flux / measurement.uncertainty;
    }
    if (request.minimumSignalToNoise > 0.0)
    {
        for (const ApertureChannelMeasurement& channel : out.channels)
        {
            if (!std::isfinite(channel.signalToNoise) || channel.signalToNoise < request.minimumSignalToNoise)
            {
                out.rejection = ApertureRejectionReason::LowSignalToNoise;
                return;
            }
        }
    }
    out.rejection = ApertureRejectionReason::None;
}

/// Measures one target with whichever representation the frame holds.
///
/// @param frame the checked frame.
/// @param target the star.
/// @param access its neighbours.
/// @param request the policies.
/// @param scratch this worker's annulus arrays.
/// @param out receives the measurement.
void measureAny(const StarFrameView& frame, const ApertureTarget& target, const NeighborAccess& access,
                const AperturePhotometryRequest& request, const AnnulusScratch& scratch,
                AperturePhotometryMeasurement& out)
{
    // A checked frame's descriptor names one of the eight representations, so the dispatch always runs.
    (void)dispatchSampleType(frame.descriptor().sampleType(), [&]<typename SampleT>() {
        measureTyped<SampleT>(frame, target, access, request, scratch, out);
    });
}

/// Points one worker's scratch at its slice of a block.
///
/// @param block the block.
/// @param slot the worker.
/// @param capacity samples per channel.
/// @return the scratch.
[[nodiscard]] AnnulusScratch scratchFor(double* const block, const std::uint32_t slot,
                                        const std::size_t capacity) noexcept
{
    AnnulusScratch scratch{};
    scratch.capacity = capacity;
    double* const base = block + static_cast<std::size_t>(slot) * capacity * kColorChannelCount;
    for (std::uint32_t channel = 0u; channel < kColorChannelCount; ++channel)
    {
        scratch.samples[channel] = base + static_cast<std::size_t>(channel) * capacity;
    }
    return scratch;
}

} // namespace

const char* apertureRejectionReasonName(const ApertureRejectionReason reason) noexcept
{
    switch (reason)
    {
    case ApertureRejectionReason::None:
        return "None";
    case ApertureRejectionReason::InvalidGeometry:
        return "InvalidGeometry";
    case ApertureRejectionReason::EdgeClipped:
        return "EdgeClipped";
    case ApertureRejectionReason::Saturated:
        return "Saturated";
    case ApertureRejectionReason::LowSignalToNoise:
        return "LowSignalToNoise";
    case ApertureRejectionReason::Blended:
        return "Blended";
    case ApertureRejectionReason::BadPixel:
        return "BadPixel";
    case ApertureRejectionReason::InsufficientBackgroundSamples:
        return "InsufficientBackgroundSamples";
    }
    return "UnknownApertureRejectionReason";
}

Result<AperturePhotometryMeasurement>
measureAperturePhotometry(const ExecutionContext& execution, const StarFrameView& frame, const ApertureTarget& target,
                          const std::span<const ApertureNeighbor> neighbors, const AperturePhotometryRequest& request)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "aperture photometry needs an execution context", kSingleOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (const Status checked = checkFrame(frame, kSingleOperation); checked.failed())
    {
        return checked;
    }
    if (const Status valid = validateRequest(request, kSingleOperation); valid.failed())
    {
        return valid;
    }
    if (!std::isfinite(target.position.x) || !std::isfinite(target.position.y))
    {
        return failure(ErrorCode::InvalidArgument, "the target position is finite", kSingleOperation);
    }
    const std::size_t capacity = annulusCapacity(maximumOuterRadius(request));
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, static_cast<std::uint64_t>(capacity) * kColorChannelCount * sizeof(double), alignof(double),
        kSingleOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    AperturePhotometryMeasurement measurement{};
    measureAny(frame, target, NeighborAccess{.neighbors = neighbors}, request,
               scratchFor(static_cast<double*>(memory->data()), 0u, capacity), measurement);
    return measurement;
}

Result<AperturePhotometryBatchResult> measureAperturePhotometryBatch(const ExecutionContext& execution,
                                                                     const StarFrameView& frame,
                                                                     const std::span<const ApertureTarget> targets,
                                                                     const AperturePhotometryRequest& request,
                                                                     const ApertureNeighborPolicy neighborPolicy,
                                                                     const std::span<AperturePhotometryMeasurement> out)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "aperture photometry needs an execution context", kBatchOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (const Status checked = checkFrame(frame, kBatchOperation); checked.failed())
    {
        return checked;
    }
    if (const Status valid = validateRequest(request, kBatchOperation); valid.failed())
    {
        return valid;
    }
    if (!isApertureNeighborPolicy(neighborPolicy))
    {
        return failure(ErrorCode::InvalidArgument, "the neighbour policy is one this build implements",
                       kBatchOperation);
    }
    if (targets.empty())
    {
        return failure(ErrorCode::EmptySelection, "a batch needs at least one target", kBatchOperation);
    }
    if (targets.size() > kMaxApertureTargets)
    {
        return limitFailure("the target count exceeds the stated ceiling", kBatchOperation, targets.size(),
                            kMaxApertureTargets);
    }
    if (out.size() != targets.size())
    {
        return failure(ErrorCode::IncompatibleArguments, "the output holds exactly one measurement per target",
                       kBatchOperation);
    }

    const std::size_t capacity = annulusCapacity(maximumOuterRadius(request));
    const double outer = maximumOuterRadius(request);
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(annulusCapacity(outer)) * sizeof(float) * 3u,
                        .operationsPerItem = static_cast<std::uint64_t>(annulusCapacity(outer)) * 8u};
    const std::uint32_t slots = (std::max)(1u, plannedWorkerCount(execution, targets.size(), cost));
    const std::uint64_t scratchBytes =
        static_cast<std::uint64_t>(slots) * capacity * kColorChannelCount * sizeof(double);
    const std::uint64_t counterBytes =
        static_cast<std::uint64_t>(slots) * (kApertureRejectionReasonCount + 1u) * sizeof(std::uint32_t);
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, scratchBytes + counterBytes, alignof(double), kBatchOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    double* const block = static_cast<double*>(memory->data());
    auto* const counters = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(memory->data()) + scratchBytes);
    std::fill(counters, counters + static_cast<std::size_t>(slots) * (kApertureRejectionReasonCount + 1u), 0u);

    const std::span<const ApertureTarget> neighborTargets =
        neighborPolicy == ApertureNeighborPolicy::OtherTargets ? targets : std::span<const ApertureTarget>{};
    const auto body = [&](const WorkerTask task) -> Status {
        const AnnulusScratch scratch = scratchFor(block, task.workerSlot, capacity);
        std::uint32_t* const slotCounters =
            counters + static_cast<std::size_t>(task.workerSlot) * (kApertureRejectionReasonCount + 1u);
        for (std::uint64_t index = task.range.begin; index < task.range.end; ++index)
        {
            AperturePhotometryMeasurement measurement{};
            measureAny(frame, targets[index],
                       NeighborAccess{.targets = neighborTargets, .selfIndex = static_cast<std::size_t>(index)},
                       request, scratch, measurement);
            out[index] = measurement;
            if (measurement.rejection == ApertureRejectionReason::None)
            {
                ++slotCounters[kApertureRejectionReasonCount];
            }
            else
            {
                ++slotCounters[static_cast<std::size_t>(measurement.rejection)];
            }
        }
        return Status::success();
    };
    if (const Status ran = parallelForWorkers(execution, targets.size(), cost, body); ran.failed())
    {
        return ran;
    }

    AperturePhotometryBatchResult result{};
    result.workingBytes = memory->bytes();
    for (std::uint32_t slot = 0u; slot < slots; ++slot)
    {
        const std::uint32_t* const slotCounters =
            counters + static_cast<std::size_t>(slot) * (kApertureRejectionReasonCount + 1u);
        result.acceptedCount += slotCounters[kApertureRejectionReasonCount];
        for (std::size_t reason = 1u; reason < kApertureRejectionReasonCount; ++reason)
        {
            result.rejections[reason] += slotCounters[reason];
            result.rejectedCount += slotCounters[reason];
        }
    }
    return result;
}

} // namespace an
