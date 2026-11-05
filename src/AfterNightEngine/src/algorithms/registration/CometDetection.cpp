// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - comet centre measurement and blind moving-object detection.
//
// Centre refinement measures one region about a seed: the median of the region's border samples is
// the background and 1.4826 times their median absolute deviation the noise; the signal-weighted
// centroid of the samples more than one noise above background is the centre, and a compact,
// unsaturated peak is refined further by a three-point Gaussian fit on each axis. Moving-object
// detection subtracts a per-pixel temporal median from every frame, keeps the eight-connected
// components of what rises above a robust threshold, and links components across frames by
// constant-velocity hypotheses drawn from every pair of them. Every centroid weights a sample at
// its centre, index plus one half.

#include <AfterNightEngine/Algorithms/Registration/CometDetection.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/registration/CometInternal.h"
#include "algorithms/registration/RegistrationInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace an
{
namespace
{

constexpr const char* kCenterOperation = "an::refineCometCenter";
constexpr const char* kDetectionOperation = "an::detectMovingCometCandidates";
/// The ratio of a Gaussian's full width at half maximum to its standard deviation, `2 sqrt(2 ln 2)`.
constexpr double kGaussianFullWidthFactor = 2.3548200450309493;

/// The factor that turns a median absolute deviation into a Gaussian standard deviation.
constexpr double kRobustSigmaFactor = 1.4826;

/// The median of a non-empty list, reordering it.
///
/// @tparam VectorT a random-access container of doubles.
/// @param values the list, partially sorted on return.
/// @return the middle value, or the mean of the two middle values for an even count.
template <typename VectorT> [[nodiscard]] double medianInPlace(VectorT& values)
{
    const std::size_t middle = values.size() / 2u;
    auto middleValue = values.begin() + static_cast<std::ptrdiff_t>(middle);
    std::nth_element(values.begin(), middleValue, values.end());
    if ((values.size() & 1u) != 0u)
    {
        return *middleValue;
    }
    const auto lower = (std::max_element)(values.begin(), middleValue);
    return 0.5 * (*lower + *middleValue);
}

/// A frame's validity, narrowed to the mask plane one channel reads.
///
/// @param frame the frame.
/// @param channel the channel.
/// @return the validity, whose mask plane is the channel's, or zero for a shared mask.
[[nodiscard]] KernelValidityView validityForChannel(const StarFrameView& frame, const std::uint32_t channel) noexcept
{
    KernelValidityView validity = frame.validity();
    if (validity.mask != nullptr)
    {
        validity.maskPlane = validity.mask->isShared() ? 0u : channel;
    }
    return validity;
}

/// Describes one channel of a frame for the plane-source checks.
///
/// @param frame the frame.
/// @param channel the channel.
/// @return the channel's plane and position within a pixel.
[[nodiscard]] KernelPlaneSource sourceForChannel(const StarFrameView& frame, const std::uint32_t channel) noexcept
{
    return KernelPlaneSource{.sampleType = frame.descriptor().sampleType(),
                             .samples = frame.samples(),
                             .layout = &frame.layout(),
                             .plane = channel / frame.layout().samplesPerPixel(),
                             .sampleInPixel = channel % frame.layout().samplesPerPixel()};
}

/// Reads one channel of a checked frame sample by sample, under the frame's validity.
///
/// A sample is admitted only when the validity rule accepts its value and, when the frame carries
/// a mask, its mask bit is set; the caller then decides what a non-finite admitted value means.
///
/// @tparam SampleT the frame's resident representation.
template <typename SampleT> class FrameReader
{
public:
    /// Binds to one channel of a frame whose layout has already been checked.
    ///
    /// @param frame the frame, which must outlive the reader.
    /// @param channel the channel to read.
    FrameReader(const StarFrameView& frame, const std::uint32_t channel) noexcept
        : m_validity(frame.validity().descriptor),
          m_valueRuleAcceptsAll(m_validity.rule() == ValidityRule::AllValid ||
                                m_validity.rule() == ValidityRule::ExplicitMask)
    {
        const ImageLayout& layout = frame.layout();
        const std::uint32_t plane = channel / layout.samplesPerPixel();
        const std::uint32_t sampleInPixel = channel % layout.samplesPerPixel();
        const auto* const bytes = static_cast<const std::uint8_t*>(frame.samples());
        const std::uint64_t offset = static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() +
                                     static_cast<std::uint64_t>(sampleInPixel) * sizeof(SampleT);
        m_samples = reinterpret_cast<const SampleT*>(bytes + offset);
        m_rowStride = layout.rowStrideBytes() / sizeof(SampleT);
        m_sampleStride = layout.samplesPerPixel();

        const KernelValidityView& validity = frame.validity();
        if (validity.mask != nullptr)
        {
            const ValidityMaskView<const std::uint8_t>& mask = *validity.mask;
            const std::uint32_t maskPlane = mask.isShared() ? 0u : channel;
            m_mask = mask.data() + static_cast<std::uint64_t>(maskPlane) * mask.planeStrideBytes();
            m_maskRowStride = mask.rowStrideBytes();
        }
    }

    /// Reads one sample.
    ///
    /// @param x the column, inside the frame.
    /// @param y the row, inside the frame.
    /// @param value the sample widened to double, written only when it is admitted.
    /// @return true when the sample is admitted.
    [[nodiscard]] bool read(const std::uint32_t x, const std::uint32_t y, double& value) const noexcept
    {
        const SampleT sample =
            m_samples[static_cast<std::uint64_t>(y) * m_rowStride + static_cast<std::uint64_t>(x) * m_sampleStride];
        if (!m_valueRuleAcceptsAll && !sampleIsValidByValue(m_validity, sample))
        {
            return false;
        }
        if (m_mask != nullptr)
        {
            const std::uint64_t maskOffset = static_cast<std::uint64_t>(y) * m_maskRowStride + x / 8u;
            if ((m_mask[maskOffset] & static_cast<std::uint8_t>(1u << (x % 8u))) == 0u)
            {
                return false;
            }
        }
        value = static_cast<double>(sample);
        return true;
    }

private:
    /// The channel's first sample.
    const SampleT* m_samples = nullptr;

    /// Samples from one row to the next.
    std::uint64_t m_rowStride = 0u;

    /// Samples from one pixel to the next along a row.
    std::uint32_t m_sampleStride = 0u;

    /// The frame's validity rule.
    ValidityDescriptor m_validity{};

    /// The channel's mask plane, or null when the frame has no mask.
    const std::uint8_t* m_mask = nullptr;

    /// Bytes from one mask row to the next.
    std::uint64_t m_maskRowStride = 0u;

    /// Whether the rule admits every value, so the per-sample value test can be skipped.
    bool m_valueRuleAcceptsAll = false;
};

/// Checks a centre refinement's execution, frame, seed and options.
///
/// @param execution the operation.
/// @param frame the frame to measure.
/// @param seed the starting centre, in engine pixel coordinates.
/// @param options the region, thresholds and channel.
/// @return success; `ErrorCode::NotInitialized` for an empty execution; `ErrorCode::InvalidArgument`
///         for an unchecked frame, a seed that is not finite or an option out of range;
///         `ErrorCode::OutOfRange` for a seed outside the span of pixel centres; a plane-source
///         failure; or the cancellation.
[[nodiscard]] Status validateCenter(const ExecutionContext& execution, const StarFrameView& frame,
                                    const PixelCoordinate& seed, const CometCenterRefinementOptions& options)
{
    if (!execution.valid())
    {
        return comet_internal::failure(ErrorCode::NotInitialized, "a centre refinement needs an execution context",
                                       kCenterOperation);
    }
    const Status stopped = execution.checkCancellation();
    if (stopped.failed())
    {
        return stopped;
    }
    if (!frame.valid())
    {
        return comet_internal::failure(ErrorCode::InvalidArgument, "a centre refinement needs a checked frame",
                                       kCenterOperation);
    }
    if (!comet_internal::finitePixel(seed))
    {
        return comet_internal::failure(ErrorCode::InvalidArgument, "the supplied centre is finite", kCenterOperation);
    }
    if (options.roiRadius > kMaximumCometCenterRadius || !std::isfinite(options.minimumSignalToNoise) ||
        options.minimumSignalToNoise <= 0.0 || !std::isfinite(options.maximumCompactFullWidth) ||
        options.maximumCompactFullWidth <= 0.0 ||
        (std::isnan(options.saturationLevel) ||
         options.saturationLevel == -(std::numeric_limits<double>::infinity)()) ||
        options.channelIndex >= frame.descriptor().extent().channelCount())
    {
        return comet_internal::failure(ErrorCode::InvalidArgument,
                                       "centre options have a supported radius, positive thresholds and channel",
                                       kCenterOperation);
    }
    const std::uint32_t width = frame.descriptor().extent().width();
    const std::uint32_t height = frame.descriptor().extent().height();
    if (seed.x < 0.5 || seed.x > static_cast<double>(width) - 0.5 || seed.y < 0.5 ||
        seed.y > static_cast<double>(height) - 0.5)
    {
        return comet_internal::failure(ErrorCode::OutOfRange, "the supplied centre lies inside the frame",
                                       kCenterOperation);
    }
    const KernelPlaneSource source = sourceForChannel(frame, options.channelIndex);
    const KernelValidityView validity = validityForChannel(frame, options.channelIndex);
    return plane_detail::checkPlaneSource(execution, source, validity, kCenterOperation);
}

/// Refines one comet centre in a frame of one representation.
///
/// The region is the square of radius `max(1, roiRadius)` about the pixel holding the seed,
/// clipped to the frame. The background is the median of the valid samples on the region's
/// border - of every valid sample when the border has none - and the noise is 1.4826 times their
/// median absolute deviation, floored at 1e-6. A result below `minimumSignalToNoise`, or with no
/// sample more than one noise above background, keeps the seed. Otherwise the centre is the
/// signal-weighted centroid of those samples, and their second moments give the full width at half
/// maximum, an aperture radius of at least two pixels and the ellipticity. When the region is
/// unsaturated, that width is within `maximumCompactFullWidth`, the brightest sample is not on the
/// region's border, and it and its four neighbours are all above background, a three-point Gaussian
/// (log-parabola) fit on each axis replaces the centroid, provided both offsets are within one
/// pixel.
///
/// @tparam SampleT the frame's resident representation.
/// @param execution the operation, for memory and cancellation.
/// @param frame the checked frame.
/// @param seed the starting centre, in engine pixel coordinates.
/// @param options the region, thresholds and channel.
/// @return the measurement; `ErrorCode::EmptySelection` when the region holds no valid sample;
///         `ErrorCode::InvalidData` for an admitted sample that is not finite; or the cancellation.
template <typename SampleT>
[[nodiscard]] Result<CometCenterRefinementResult> refineTyped(const ExecutionContext& execution,
                                                              const StarFrameView& frame, const PixelCoordinate& seed,
                                                              const CometCenterRefinementOptions& options)
{
    registration_internal::ExecutionMemory memory(*execution.memoryResource());
    const FrameReader<SampleT> reader(frame, options.channelIndex);
    const std::uint32_t width = frame.descriptor().extent().width();
    const std::uint32_t height = frame.descriptor().extent().height();
    const std::uint32_t radius = (std::max)(1u, options.roiRadius);
    const std::int64_t seedColumn = static_cast<std::int64_t>(std::floor(seed.x));
    const std::int64_t seedRow = static_cast<std::int64_t>(std::floor(seed.y));
    const std::uint32_t minimumX =
        static_cast<std::uint32_t>((std::max<std::int64_t>)(0, seedColumn - static_cast<std::int64_t>(radius)));
    const std::uint32_t minimumY =
        static_cast<std::uint32_t>((std::max<std::int64_t>)(0, seedRow - static_cast<std::int64_t>(radius)));
    const std::uint32_t maximumX = static_cast<std::uint32_t>(
        (std::min<std::int64_t>)(static_cast<std::int64_t>(width) - 1, seedColumn + static_cast<std::int64_t>(radius)));
    const std::uint32_t maximumY = static_cast<std::uint32_t>(
        (std::min<std::int64_t>)(static_cast<std::int64_t>(height) - 1, seedRow + static_cast<std::int64_t>(radius)));
    const std::uint32_t regionWidth = maximumX - minimumX + 1u;
    const std::uint32_t regionHeight = maximumY - minimumY + 1u;
    const std::size_t regionSize = static_cast<std::size_t>(regionWidth) * regionHeight;
    const double missing = (std::numeric_limits<double>::quiet_NaN)();
    std::pmr::vector<double> region(regionSize, missing, &memory);
    std::pmr::vector<double> robust(&memory);
    robust.reserve(2u * static_cast<std::size_t>(regionWidth + regionHeight));

    std::size_t selected = 0u;
    for (std::uint32_t y = minimumY; y <= maximumY; ++y)
    {
        const Status checkpoint = execution.checkCancellation();
        if (checkpoint.failed())
        {
            return checkpoint;
        }
        for (std::uint32_t x = minimumX; x <= maximumX; ++x)
        {
            double value = 0.0;
            if (!reader.read(x, y, value))
            {
                continue;
            }
            if (!std::isfinite(value))
            {
                return comet_internal::failure(ErrorCode::InvalidData, "an admitted centre-refinement sample is finite",
                                               kCenterOperation);
            }
            region[static_cast<std::size_t>(y - minimumY) * regionWidth + (x - minimumX)] = value;
            ++selected;
            if (x == minimumX || x == maximumX || y == minimumY || y == maximumY)
            {
                robust.push_back(value);
            }
        }
    }
    if (selected == 0u)
    {
        return comet_internal::failure(ErrorCode::EmptySelection,
                                       "the centre region contains at least one valid sample", kCenterOperation);
    }
    if (robust.empty())
    {
        for (const double value : region)
        {
            if (std::isfinite(value))
            {
                robust.push_back(value);
            }
        }
    }
    const double background = medianInPlace(robust);
    for (double& value : robust)
    {
        value = std::abs(value - background);
    }
    const double noise = (std::max)(kRobustSigmaFactor * medianInPlace(robust), 1.0e-6);

    double totalWeight = 0.0;
    double weightedX = 0.0;
    double weightedY = 0.0;
    double peakSignal = 0.0;
    std::uint32_t peakX = minimumX;
    std::uint32_t peakY = minimumY;
    bool saturated = false;
    for (std::uint32_t localY = 0u; localY < regionHeight; ++localY)
    {
        for (std::uint32_t localX = 0u; localX < regionWidth; ++localX)
        {
            const double value = region[static_cast<std::size_t>(localY) * regionWidth + localX];
            if (!std::isfinite(value))
            {
                continue;
            }
            saturated = saturated || value >= options.saturationLevel;
            const double signal = (std::max)(0.0, value - background);
            if (signal > peakSignal)
            {
                peakSignal = signal;
                peakX = minimumX + localX;
                peakY = minimumY + localY;
            }
            if (signal > noise)
            {
                totalWeight += signal;
                weightedX += signal * (static_cast<double>(minimumX + localX) + 0.5);
                weightedY += signal * (static_cast<double>(minimumY + localY) + 0.5);
            }
        }
    }

    CometCenterRefinementResult result;
    result.center = seed;
    result.weightedCenter = seed;
    result.gaussianCenter = seed;
    result.localBackground = background;
    result.localNoise = noise;
    result.signalToNoise = peakSignal / noise;
    result.saturated = saturated;
    if (!(totalWeight > 0.0) || result.signalToNoise < options.minimumSignalToNoise)
    {
        return result;
    }

    result.outcome = CometCenterOutcome::Refined;
    result.weightedCenter = {.x = weightedX / totalWeight, .y = weightedY / totalWeight};
    result.center = result.weightedCenter;
    result.gaussianCenter = result.weightedCenter;
    result.method = CometCenterMethod::WeightedCentroid;
    double varianceX = 0.0;
    double varianceY = 0.0;
    double covariance = 0.0;
    for (std::uint32_t localY = 0u; localY < regionHeight; ++localY)
    {
        for (std::uint32_t localX = 0u; localX < regionWidth; ++localX)
        {
            const double value = region[static_cast<std::size_t>(localY) * regionWidth + localX];
            if (!std::isfinite(value))
            {
                continue;
            }
            const double signal = (std::max)(0.0, value - background);
            if (signal <= noise)
            {
                continue;
            }
            const double dx = static_cast<double>(minimumX + localX) + 0.5 - result.weightedCenter.x;
            const double dy = static_cast<double>(minimumY + localY) + 0.5 - result.weightedCenter.y;
            varianceX += signal * dx * dx;
            varianceY += signal * dy * dy;
            covariance += signal * dx * dy;
        }
    }
    varianceX /= totalWeight;
    varianceY /= totalWeight;
    covariance /= totalWeight;
    const double trace = varianceX + varianceY;
    const double determinant = varianceX * varianceY - covariance * covariance;
    const double discriminant = (std::max)(0.0, trace * trace * 0.25 - determinant);
    const double lambdaMajor = trace * 0.5 + std::sqrt(discriminant);
    const double lambdaMinor = (std::max)(0.0, trace * 0.5 - std::sqrt(discriminant));
    const double meanSigma = std::sqrt(0.5 * (varianceX + varianceY));
    result.fullWidthAtHalfMaximumPixels = kGaussianFullWidthFactor * meanSigma;
    result.apertureRadiusPixels = (std::max)(2.0, 2.0 * meanSigma);
    result.ellipticity = lambdaMajor > 1.0e-12 ? 1.0 - std::sqrt((std::max)(0.0, lambdaMinor) / lambdaMajor) : 0.0;

    const auto residualAt = [&](const std::uint32_t x, const std::uint32_t y) {
        const double value = region[static_cast<std::size_t>(y - minimumY) * regionWidth + (x - minimumX)];
        return std::isfinite(value) ? value - background : -1.0;
    };
    if (!saturated && result.fullWidthAtHalfMaximumPixels <= options.maximumCompactFullWidth && peakX > minimumX &&
        peakX < maximumX && peakY > minimumY && peakY < maximumY)
    {
        const double centerValue = residualAt(peakX, peakY);
        const double left = residualAt(peakX - 1u, peakY);
        const double right = residualAt(peakX + 1u, peakY);
        const double above = residualAt(peakX, peakY - 1u);
        const double below = residualAt(peakX, peakY + 1u);
        if (centerValue > 0.0 && left > 0.0 && right > 0.0 && above > 0.0 && below > 0.0)
        {
            const auto offset = [](const double before, const double center, const double after) {
                const double l = std::log(before);
                const double c = std::log(center);
                const double r = std::log(after);
                const double denominator = l - 2.0 * c + r;
                if (std::abs(denominator) <= 1.0e-12)
                {
                    return (std::numeric_limits<double>::quiet_NaN)();
                }
                const double value = 0.5 * (l - r) / denominator;
                return std::isfinite(value) && std::abs(value) <= 1.0 ? value
                                                                      : (std::numeric_limits<double>::quiet_NaN)();
            };
            const double xOffset = offset(left, centerValue, right);
            const double yOffset = offset(above, centerValue, below);
            if (std::isfinite(xOffset) && std::isfinite(yOffset))
            {
                result.gaussianCenter = {.x = static_cast<double>(peakX) + 0.5 + xOffset,
                                         .y = static_cast<double>(peakY) + 0.5 + yOffset};
                result.center = result.gaussianCenter;
                result.method = CometCenterMethod::GaussianPsf;
            }
        }
    }
    return result;
}

/// One retained connected component of one frame.
struct Blob
{
    /// The frame's position in the time-ordered available sequence.
    std::uint32_t frame = 0u;

    /// The component as a detection.
    CometMovingObjectPoint point{};
};

/// A constant-velocity line fitted through one candidate's detections.
struct CandidateFit
{
    /// Horizontal position at the first detection's midpoint, in pixels.
    double x0 = 0.0;

    /// Horizontal velocity, in pixels per second.
    double vx = 0.0;

    /// Vertical position at the first detection's midpoint, in pixels.
    double y0 = 0.0;

    /// Vertical velocity, in pixels per second.
    double vy = 0.0;

    /// The root-mean-square distance from the line, in pixels.
    double rootMeanSquare = 0.0;

    /// The largest distance from the line, in pixels.
    double maximumResidual = 0.0;

    /// The largest distance between two of the detections, in pixels.
    double motion = 0.0;

    /// Whether the fit exists: false for fewer than two detections or no spread in time.
    bool usable = false;
};

/// Fits both axes of a candidate as straight lines in time by ordinary least squares.
///
/// @param points the detections, the first of which sets the time origin.
/// @return the fit, whose `usable` is false when it does not exist.
[[nodiscard]] CandidateFit fitCandidate(const std::vector<CometMovingObjectPoint>& points) noexcept
{
    CandidateFit result;
    if (points.size() < 2u)
    {
        return result;
    }
    const std::int64_t origin = points.front().midpoint.nanosecondsSinceEpoch;
    long double sumT = 0.0L;
    long double sumTT = 0.0L;
    long double sumX = 0.0L;
    long double sumTX = 0.0L;
    long double sumY = 0.0L;
    long double sumTY = 0.0L;
    for (const CometMovingObjectPoint& point : points)
    {
        const long double t = (static_cast<long double>(point.midpoint.nanosecondsSinceEpoch) - origin) / 1.0e9L;
        sumT += t;
        sumTT += t * t;
        sumX += point.position.x;
        sumTX += t * point.position.x;
        sumY += point.position.y;
        sumTY += t * point.position.y;
    }
    const long double count = static_cast<long double>(points.size());
    const long double denominator = count * sumTT - sumT * sumT;
    if (std::abs(denominator) <= 1.0e-18L)
    {
        return result;
    }
    result.vx = static_cast<double>((count * sumTX - sumT * sumX) / denominator);
    result.vy = static_cast<double>((count * sumTY - sumT * sumY) / denominator);
    result.x0 = static_cast<double>((sumX - result.vx * sumT) / count);
    result.y0 = static_cast<double>((sumY - result.vy * sumT) / count);
    long double squared = 0.0L;
    for (std::size_t index = 0u; index < points.size(); ++index)
    {
        const double t = static_cast<double>(
            (static_cast<long double>(points[index].midpoint.nanosecondsSinceEpoch) - origin) / 1.0e9L);
        const double dx = points[index].position.x - (result.x0 + result.vx * t);
        const double dy = points[index].position.y - (result.y0 + result.vy * t);
        result.maximumResidual = (std::max)(result.maximumResidual, std::hypot(dx, dy));
        squared += static_cast<long double>(dx) * dx + static_cast<long double>(dy) * dy;
    }
    result.rootMeanSquare = std::sqrt(static_cast<double>(squared / count));
    for (std::size_t first = 0u; first < points.size(); ++first)
    {
        for (std::size_t second = first + 1u; second < points.size(); ++second)
        {
            result.motion = (std::max)(result.motion, std::hypot(points[first].position.x - points[second].position.x,
                                                                 points[first].position.y - points[second].position.y));
        }
    }
    result.usable = std::isfinite(result.rootMeanSquare) && std::isfinite(result.motion);
    return result;
}

/// Whether two candidates are the same track found from two different pairs.
///
/// @param left one candidate.
/// @param right the other.
/// @return true when they hold equally many detections and every detection of `left` is within one
///         pixel of a detection of `right` in the same frame.
[[nodiscard]] bool sameCandidate(const CometMovingObjectCandidate& left,
                                 const CometMovingObjectCandidate& right) noexcept
{
    if (left.points.size() != right.points.size())
    {
        return false;
    }
    const auto close = [](const CometMovingObjectPoint& a, const CometMovingObjectPoint& b) {
        return a.frameId == b.frameId && std::hypot(a.position.x - b.position.x, a.position.y - b.position.y) <= 1.0;
    };
    return std::all_of(left.points.begin(), left.points.end(), [&](const CometMovingObjectPoint& point) {
        return std::any_of(right.points.begin(), right.points.end(),
                           [&](const CometMovingObjectPoint& other) { return close(point, other); });
    });
}

/// Checks a detection request and lists the frames it will use.
///
/// A frame whose view is empty is skipped, or fails the request when it is required. The frames
/// used must share descriptor and arrangement and have distinct midpoints, and they are listed in
/// time order, frame identity breaking ties.
///
/// @param execution the operation.
/// @param frames the caller's frames.
/// @param options the thresholds, areas, channel and result bound.
/// @param available receives the indices, into `frames`, of the frames used.
/// @return success; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::LimitExceeded` above the frame or candidate ceiling;
///         `ErrorCode::InvalidArgument` for an option out of range, a repeated identity, a missing
///         required frame, repeated midpoints or a minimum detection count above the frames used;
///         `ErrorCode::EmptySelection` for fewer than two frames used;
///         `ErrorCode::IncompatibleArguments` for frames that differ in geometry, representation
///         or arrangement; a plane-source failure; or the cancellation.
[[nodiscard]] Status validateDetection(const ExecutionContext& execution,
                                       const std::span<const CometDetectionFrame> frames,
                                       const CometMovingObjectDetectionOptions& options,
                                       std::pmr::vector<std::size_t>& available)
{
    if (!execution.valid())
    {
        return comet_internal::failure(ErrorCode::NotInitialized, "moving-object detection needs an execution context",
                                       kDetectionOperation);
    }
    const Status stopped = execution.checkCancellation();
    if (stopped.failed())
    {
        return stopped;
    }
    if (frames.size() > kMaximumCometDetectionFrames)
    {
        return comet_internal::failure(ErrorCode::LimitExceeded, "the frame set exceeds the moving-object ceiling",
                                       kDetectionOperation);
    }
    if (!std::isfinite(options.minimumPositiveResidual) || options.minimumPositiveResidual < 0.0 ||
        !std::isfinite(options.detectionSigma) || options.detectionSigma <= 0.0 ||
        !std::isfinite(options.minimumIntegratedSignalToNoise) || options.minimumIntegratedSignalToNoise <= 0.0 ||
        options.minimumComponentArea == 0u || options.maximumComponentArea < options.minimumComponentArea ||
        !std::isfinite(options.maximumTrackResidualPixels) || options.maximumTrackResidualPixels <= 0.0 ||
        !std::isfinite(options.minimumMotionPixels) || options.minimumMotionPixels < 0.0)
    {
        return comet_internal::failure(ErrorCode::InvalidArgument,
                                       "moving-object thresholds, areas and result ceiling are valid",
                                       kDetectionOperation);
    }
    if (options.maximumCandidates > kMaximumCometCandidates)
    {
        return comet_internal::failure(ErrorCode::LimitExceeded,
                                       "the requested candidate count exceeds the moving-object ceiling",
                                       kDetectionOperation);
    }
    for (std::size_t index = 0u; index < frames.size(); ++index)
    {
        for (std::size_t earlier = 0u; earlier < index; ++earlier)
        {
            if (frames[earlier].frameId == frames[index].frameId)
            {
                return comet_internal::failure(ErrorCode::InvalidArgument, "moving-object frame identities are unique",
                                               kDetectionOperation);
            }
        }
        if (!frames[index].frame.valid())
        {
            if (frames[index].required)
            {
                return comet_internal::failure(ErrorCode::InvalidArgument,
                                               "a required moving-object frame is available", kDetectionOperation);
            }
            continue;
        }
        available.push_back(index);
    }
    if (available.size() < 2u)
    {
        return comet_internal::failure(ErrorCode::EmptySelection,
                                       "moving-object detection needs at least two available frames",
                                       kDetectionOperation);
    }
    const std::uint32_t minimumDetections = (std::max)(2u, options.minimumDetections);
    if (minimumDetections > available.size())
    {
        return comet_internal::failure(ErrorCode::InvalidArgument,
                                       "the minimum detections fit inside the available sequence", kDetectionOperation);
    }
    const StarFrameView& first = frames[available.front()].frame;
    if (options.channelIndex >= first.descriptor().extent().channelCount())
    {
        return comet_internal::failure(ErrorCode::InvalidArgument, "the searched channel exists in every frame",
                                       kDetectionOperation);
    }
    for (std::size_t position = 0u; position < available.size(); ++position)
    {
        const StarFrameView& frame = frames[available[position]].frame;
        if (frame.descriptor() != first.descriptor() ||
            frame.layout().samplesPerPixel() != first.layout().samplesPerPixel() ||
            frame.layout().planeCount() != first.layout().planeCount())
        {
            return comet_internal::failure(ErrorCode::IncompatibleArguments,
                                           "moving-object frames share geometry, representation and arrangement",
                                           kDetectionOperation);
        }
        for (std::size_t earlier = 0u; earlier < position; ++earlier)
        {
            if (frames[available[earlier]].midpoint == frames[available[position]].midpoint)
            {
                return comet_internal::failure(ErrorCode::InvalidArgument,
                                               "available frames have distinct exposure midpoints",
                                               kDetectionOperation);
            }
        }
        const KernelPlaneSource source = sourceForChannel(frame, options.channelIndex);
        const KernelValidityView validity = validityForChannel(frame, options.channelIndex);
        const Status sourceReady = plane_detail::checkPlaneSource(execution, source, validity, kDetectionOperation);
        if (sourceReady.failed())
        {
            return sourceReady;
        }
    }
    std::sort(available.begin(), available.end(), [&](const std::size_t left, const std::size_t right) {
        const CometDetectionFrame& a = frames[left];
        const CometDetectionFrame& b = frames[right];
        if (a.midpoint.nanosecondsSinceEpoch != b.midpoint.nanosecondsSinceEpoch)
        {
            return a.midpoint.nanosecondsSinceEpoch < b.midpoint.nanosecondsSinceEpoch;
        }
        return a.frameId < b.frameId;
    });
    return Status::success();
}

/// Detects moving-object candidates in frames of one representation.
///
/// The background is the per-pixel median of every frame's admitted sample. In each frame the
/// positive difference from it is kept where it exceeds `minimumPositiveResidual` and
/// `detectionSigma` times the frame's noise - 1.4826 times the median absolute difference - and
/// its eight-connected components are measured: area, summed signal, signal-weighted centroid and
/// integrated signal-to-noise, the sum over the noise times the square root of the area. A
/// component within the area bounds and above `minimumIntegratedSignalToNoise` is kept, at most
/// `kMaximumCometComponentsPerFrame` per frame, strongest first.
///
/// Every pair of kept components in different frames defines a constant velocity; each frame
/// contributes its nearest component within `maximumTrackResidualPixels` of the predicted
/// position. A candidate with enough detections whose line fit stays within that residual - and
/// within one and a half times it at worst - and moves at least `minimumMotionPixels` is scored,
/// merged with a same-track candidate by keeping the higher score, and the survivors are sorted by
/// score. The linking costs roughly the cube of the kept components times the frame count, which
/// the per-frame cap bounds.
///
/// @tparam SampleT the frames' resident representation.
/// @param execution the operation, for cancellation.
/// @param frames the caller's frames.
/// @param available the frames used, in time order.
/// @param options the thresholds, areas, channel and result bound.
/// @param memory the engine-context allocator the working vectors draw from.
/// @return the candidates and their diagnostics; `ErrorCode::InvalidData` for an admitted sample
///         that is not finite; or the cancellation.
template <typename SampleT>
[[nodiscard]] Result<CometMovingObjectDetectionResult>
detectTyped(const ExecutionContext& execution, const std::span<const CometDetectionFrame> frames,
            const std::span<const std::size_t> available, const CometMovingObjectDetectionOptions& options,
            registration_internal::ExecutionMemory& memory)
{
    const std::uint32_t width = frames[available.front()].frame.descriptor().extent().width();
    const std::uint32_t height = frames[available.front()].frame.descriptor().extent().height();
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    std::pmr::vector<FrameReader<SampleT>> readers(&memory);
    readers.reserve(available.size());
    for (const std::size_t index : available)
    {
        readers.emplace_back(frames[index].frame, options.channelIndex);
    }
    const double missing = (std::numeric_limits<double>::quiet_NaN)();
    std::pmr::vector<double> background(pixelCount, missing, &memory);
    std::pmr::vector<double> temporal(&memory);
    temporal.reserve(available.size());
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        const Status checkpoint = execution.checkCancellation();
        if (checkpoint.failed())
        {
            return checkpoint;
        }
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            temporal.clear();
            for (const FrameReader<SampleT>& reader : readers)
            {
                double value = 0.0;
                if (reader.read(x, y, value))
                {
                    if (!std::isfinite(value))
                    {
                        return comet_internal::failure(
                            ErrorCode::InvalidData, "an admitted moving-object sample is finite", kDetectionOperation);
                    }
                    temporal.push_back(value);
                }
            }
            if (!temporal.empty())
            {
                background[static_cast<std::size_t>(y) * width + x] = medianInPlace(temporal);
            }
        }
    }

    std::pmr::vector<Blob> blobs(&memory);
    blobs.reserve(available.size() * 16u);
    std::pmr::vector<double> residual(pixelCount, 0.0, &memory);
    std::pmr::vector<double> absolute(&memory);
    absolute.reserve(pixelCount);
    std::pmr::vector<std::uint8_t> visited(pixelCount, 0u, &memory);
    std::pmr::vector<std::size_t> stack(&memory);
    stack.reserve((std::min<std::size_t>)(pixelCount, options.maximumComponentArea + 1024u));
    std::uint64_t componentsExamined = 0u;
    CometMovingObjectDetectionResult result;
    result.framesExamined = static_cast<std::uint32_t>(available.size());
    result.frameDiagnostics.reserve(available.size());
    for (std::uint32_t frameOrdinal = 0u; frameOrdinal < available.size(); ++frameOrdinal)
    {
        const Status checkpoint = execution.checkCancellation();
        if (checkpoint.failed())
        {
            return checkpoint;
        }
        std::fill(residual.begin(), residual.end(), 0.0);
        std::fill(visited.begin(), visited.end(), 0u);
        absolute.clear();
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const Status rowCheckpoint = execution.checkCancellation();
            if (rowCheckpoint.failed())
            {
                return rowCheckpoint;
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const std::size_t pixel = static_cast<std::size_t>(y) * width + x;
                double value = 0.0;
                if (std::isfinite(background[pixel]) && readers[frameOrdinal].read(x, y, value))
                {
                    const double difference = value - background[pixel];
                    absolute.push_back(std::abs(difference));
                    residual[pixel] = difference > 0.0 ? difference : 0.0;
                }
            }
        }
        const double noise =
            absolute.empty() ? 1.0e-6 : (std::max)(kRobustSigmaFactor * medianInPlace(absolute), 1.0e-6);
        const double threshold = (std::max)(options.minimumPositiveResidual, options.detectionSigma * noise);
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const Status rowCheckpoint = execution.checkCancellation();
            if (rowCheckpoint.failed())
            {
                return rowCheckpoint;
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const std::size_t pixel = static_cast<std::size_t>(y) * width + x;
                if (!(residual[pixel] > threshold))
                {
                    residual[pixel] = 0.0;
                }
            }
        }

        std::pmr::vector<Blob> frameBlobs(&memory);
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const Status rowCheckpoint = execution.checkCancellation();
            if (rowCheckpoint.failed())
            {
                return rowCheckpoint;
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const std::size_t start = static_cast<std::size_t>(y) * width + x;
                if (visited[start] != 0u || !(residual[start] > 0.0))
                {
                    continue;
                }
                stack.clear();
                stack.push_back(start);
                visited[start] = 1u;
                std::uint64_t area = 0u;
                double sum = 0.0;
                double weightedX = 0.0;
                double weightedY = 0.0;
                std::uint32_t pixelsSinceCheckpoint = 0u;
                while (!stack.empty())
                {
                    const std::size_t pixel = stack.back();
                    stack.pop_back();
                    const std::uint32_t currentX = static_cast<std::uint32_t>(pixel % width);
                    const std::uint32_t currentY = static_cast<std::uint32_t>(pixel / width);
                    const double weight = residual[pixel];
                    ++area;
                    sum += weight;
                    weightedX += weight * (static_cast<double>(currentX) + 0.5);
                    weightedY += weight * (static_cast<double>(currentY) + 0.5);
                    const std::uint32_t fromX = currentX == 0u ? 0u : currentX - 1u;
                    const std::uint32_t toX = (std::min)(width - 1u, currentX + 1u);
                    const std::uint32_t fromY = currentY == 0u ? 0u : currentY - 1u;
                    const std::uint32_t toY = (std::min)(height - 1u, currentY + 1u);
                    for (std::uint32_t neighbourY = fromY; neighbourY <= toY; ++neighbourY)
                    {
                        for (std::uint32_t neighbourX = fromX; neighbourX <= toX; ++neighbourX)
                        {
                            const std::size_t neighbour = static_cast<std::size_t>(neighbourY) * width + neighbourX;
                            if (visited[neighbour] == 0u && residual[neighbour] > 0.0)
                            {
                                visited[neighbour] = 1u;
                                stack.push_back(neighbour);
                            }
                        }
                    }
                    ++pixelsSinceCheckpoint;
                    if (pixelsSinceCheckpoint == 4096u)
                    {
                        const Status componentCheckpoint = execution.checkCancellation();
                        if (componentCheckpoint.failed())
                        {
                            return componentCheckpoint;
                        }
                        pixelsSinceCheckpoint = 0u;
                    }
                }
                const double integratedSignalToNoise = sum / (noise * std::sqrt(static_cast<double>(area)));
                if (area < options.minimumComponentArea || area > options.maximumComponentArea || !(sum > 0.0) ||
                    !std::isfinite(integratedSignalToNoise) ||
                    integratedSignalToNoise < options.minimumIntegratedSignalToNoise)
                {
                    continue;
                }
                ++componentsExamined;
                frameBlobs.push_back({.frame = frameOrdinal,
                                      .point = {.frameId = frames[available[frameOrdinal]].frameId,
                                                .midpoint = frames[available[frameOrdinal]].midpoint,
                                                .position = {.x = weightedX / sum, .y = weightedY / sum},
                                                .integratedSignal = sum,
                                                .integratedSignalToNoise = integratedSignalToNoise,
                                                .area = static_cast<std::uint32_t>(area)}});
            }
        }
        std::sort(frameBlobs.begin(), frameBlobs.end(), [](const Blob& left, const Blob& right) {
            if (left.point.integratedSignalToNoise != right.point.integratedSignalToNoise)
            {
                return left.point.integratedSignalToNoise > right.point.integratedSignalToNoise;
            }
            if (left.point.position.y != right.point.position.y)
            {
                return left.point.position.y < right.point.position.y;
            }
            return left.point.position.x < right.point.position.x;
        });
        if (frameBlobs.size() > kMaximumCometComponentsPerFrame)
        {
            frameBlobs.resize(kMaximumCometComponentsPerFrame);
        }
        result.frameDiagnostics.push_back({.frameId = frames[available[frameOrdinal]].frameId,
                                           .componentsRetained = static_cast<std::uint32_t>(frameBlobs.size())});
        blobs.insert(blobs.end(), frameBlobs.begin(), frameBlobs.end());
    }

    result.componentsExamined = componentsExamined;
    const std::uint32_t minimumDetections = (std::max)(2u, options.minimumDetections);
    for (std::size_t first = 0u; first < blobs.size(); ++first)
    {
        for (std::size_t second = first + 1u; second < blobs.size(); ++second)
        {
            const Status linkCheckpoint = execution.checkCancellation();
            if (linkCheckpoint.failed())
            {
                return linkCheckpoint;
            }
            if (blobs[first].frame == blobs[second].frame)
            {
                continue;
            }
            const long double deltaSeconds =
                (static_cast<long double>(blobs[second].point.midpoint.nanosecondsSinceEpoch) -
                 blobs[first].point.midpoint.nanosecondsSinceEpoch) /
                1.0e9L;
            if (deltaSeconds == 0.0L)
            {
                continue;
            }
            const double velocityX =
                (blobs[second].point.position.x - blobs[first].point.position.x) / static_cast<double>(deltaSeconds);
            const double velocityY =
                (blobs[second].point.position.y - blobs[first].point.position.y) / static_cast<double>(deltaSeconds);
            CometMovingObjectCandidate candidate;
            candidate.points.reserve(available.size());
            for (std::uint32_t frameOrdinal = 0u; frameOrdinal < available.size(); ++frameOrdinal)
            {
                const UtcInstant time = frames[available[frameOrdinal]].midpoint;
                const double seconds = static_cast<double>((static_cast<long double>(time.nanosecondsSinceEpoch) -
                                                            blobs[first].point.midpoint.nanosecondsSinceEpoch) /
                                                           1.0e9L);
                const PixelCoordinate predicted{.x = blobs[first].point.position.x + velocityX * seconds,
                                                .y = blobs[first].point.position.y + velocityY * seconds};
                const Blob* closest = nullptr;
                double closestDistance = options.maximumTrackResidualPixels;
                for (const Blob& blob : blobs)
                {
                    if (blob.frame != frameOrdinal)
                    {
                        continue;
                    }
                    const double distance =
                        std::hypot(blob.point.position.x - predicted.x, blob.point.position.y - predicted.y);
                    if (distance <= closestDistance)
                    {
                        closestDistance = distance;
                        closest = &blob;
                    }
                }
                if (closest != nullptr)
                {
                    candidate.points.push_back(closest->point);
                }
            }
            if (candidate.points.size() < minimumDetections)
            {
                continue;
            }
            std::sort(candidate.points.begin(), candidate.points.end(), [](const auto& left, const auto& right) {
                if (left.midpoint.nanosecondsSinceEpoch != right.midpoint.nanosecondsSinceEpoch)
                {
                    return left.midpoint.nanosecondsSinceEpoch < right.midpoint.nanosecondsSinceEpoch;
                }
                return left.frameId < right.frameId;
            });
            const CandidateFit fitted = fitCandidate(candidate.points);
            if (!fitted.usable || fitted.rootMeanSquare > options.maximumTrackResidualPixels ||
                fitted.maximumResidual > options.maximumTrackResidualPixels * 1.5 ||
                fitted.motion < options.minimumMotionPixels)
            {
                continue;
            }
            double meanSignal = 0.0;
            for (const CometMovingObjectPoint& point : candidate.points)
            {
                meanSignal += point.integratedSignalToNoise;
            }
            meanSignal /= static_cast<double>(candidate.points.size());
            candidate.rootMeanSquareResidualPixels = fitted.rootMeanSquare;
            candidate.motionPixels = fitted.motion;
            candidate.meanIntegratedSignalToNoise = meanSignal;
            candidate.persistence =
                static_cast<double>(candidate.points.size()) / static_cast<double>(available.size());
            candidate.score =
                candidate.persistence * 100.0 + meanSignal * 2.0 + fitted.motion * 0.5 - fitted.rootMeanSquare * 10.0;
            const auto duplicate = std::find_if(result.candidates.begin(), result.candidates.end(),
                                                [&](const auto& other) { return sameCandidate(candidate, other); });
            if (duplicate == result.candidates.end())
            {
                result.candidates.push_back(std::move(candidate));
            }
            else if (candidate.score > duplicate->score)
            {
                *duplicate = std::move(candidate);
            }
        }
        const Status checkpoint = execution.checkCancellation();
        if (checkpoint.failed())
        {
            return checkpoint;
        }
    }
    std::sort(result.candidates.begin(), result.candidates.end(), [](const auto& left, const auto& right) {
        if (left.score != right.score)
        {
            return left.score > right.score;
        }
        if (left.points.size() != right.points.size())
        {
            return left.points.size() > right.points.size();
        }
        return left.points.front().frameId < right.points.front().frameId;
    });
    const std::uint32_t maximumCandidates =
        options.maximumCandidates == 0u ? kMaximumCometCandidates : options.maximumCandidates;
    if (result.candidates.size() > maximumCandidates)
    {
        result.candidates.resize(maximumCandidates);
    }
    result.peakWorkingBytes = memory.peakBytes();
    return result;
}

} // namespace

const char* cometCenterMethodName(const CometCenterMethod method) noexcept
{
    switch (method)
    {
    case CometCenterMethod::RoiCenter:
        return "RoiCenter";
    case CometCenterMethod::WeightedCentroid:
        return "WeightedCentroid";
    case CometCenterMethod::GaussianPsf:
        return "GaussianPsf";
    }
    return "UnknownCometCenterMethod";
}

Result<CometCenterRefinementResult> refineCometCenter(const ExecutionContext& execution, const StarFrameView& frame,
                                                      const PixelCoordinate& seed,
                                                      const CometCenterRefinementOptions& options)
{
    const Status ready = validateCenter(execution, frame, seed, options);
    if (ready.failed())
    {
        return ready;
    }
    try
    {
        Result<CometCenterRefinementResult> result = comet_internal::failure(
            ErrorCode::Internal, "the resident representation was not dispatched", kCenterOperation);
        const bool dispatched = dispatchSampleType(frame.descriptor().sampleType(), [&]<typename SampleT>() {
            result = refineTyped<SampleT>(execution, frame, seed, options);
        });
        if (!dispatched)
        {
            return comet_internal::failure(ErrorCode::Unsupported, "the frame representation is not resident",
                                           kCenterOperation);
        }
        return result;
    }
    catch (const registration_internal::ContextAllocationFailure& refused)
    {
        return refused.status();
    }
    catch (const std::bad_alloc&)
    {
        return comet_internal::failure(ErrorCode::AllocationFailed, "centre-refinement storage could not be allocated",
                                       kCenterOperation);
    }
}

Result<CometMovingObjectDetectionResult> detectMovingCometCandidates(const ExecutionContext& execution,
                                                                     const std::span<const CometDetectionFrame> frames,
                                                                     const CometMovingObjectDetectionOptions& options)
{
    try
    {
        if (!execution.valid())
        {
            return comet_internal::failure(ErrorCode::NotInitialized,
                                           "moving-object detection needs an execution context", kDetectionOperation);
        }
        registration_internal::ExecutionMemory memory(*execution.memoryResource());
        std::pmr::vector<std::size_t> available(&memory);
        available.reserve(frames.size());
        const Status ready = validateDetection(execution, frames, options, available);
        if (ready.failed())
        {
            return ready;
        }
        Result<CometMovingObjectDetectionResult> result = comet_internal::failure(
            ErrorCode::Internal, "the resident representation was not dispatched", kDetectionOperation);
        const SampleType sampleType = frames[available.front()].frame.descriptor().sampleType();
        const bool dispatched = dispatchSampleType(sampleType, [&]<typename SampleT>() {
            result = detectTyped<SampleT>(execution, frames, available, options, memory);
        });
        if (!dispatched)
        {
            return comet_internal::failure(ErrorCode::Unsupported, "the frame representation is not resident",
                                           kDetectionOperation);
        }
        return result;
    }
    catch (const registration_internal::ContextAllocationFailure& refused)
    {
        return refused.status();
    }
    catch (const std::bad_alloc&)
    {
        return comet_internal::failure(ErrorCode::AllocationFailed,
                                       "moving-object detection could not allocate its storage", kDetectionOperation);
    }
}

} // namespace an
