// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a comet stacked twice and put back together.
//
// The split reads a floating frame where it lies, judging each pixel by every channel's rule and mask as it
// walks, and reads an integer frame once into working planes of the layers' representation with every rejected
// pixel flagged; either way it takes each channel's sky as an exact median of the annulus's valid samples - or
// of the channel's when the annulus holds too few - and writes both layers in one walk. Outside the feathered
// circle's bounding box the weight is zero without a distance being taken, and every pixel then goes through
// the same two expressions the product wrote, so a pixel far from the comet comes out exactly as the product's
// arithmetic leaves it, the sign of a zero included.
//
// Reading in place takes no memory the size of the frame. A frame-sized block taken, faulted in and given back
// on every split cost more than the split's arithmetic, and more the more workers the process ran on, since
// pages its threads touched are given back on every processor those threads ran on.
//
// The mask is written a row at a time, each aperture touching only the rows and columns its box spans, and
// the larger weight kept where two overlap; the order the apertures are drawn in changes nothing, because
// rounding to Float32 keeps the order of two weights. The blend reads the star stack, the comet stack and the
// mask straight from their storage, and maps only the pixels the mask weighs above zero through the two
// registrations.

#include <AfterNightEngine/Algorithms/Stacking/CometComposite.h>

#include <AfterNightEngine/Algorithms/Registration/CometTracking.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/CompositionPlanes.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>

namespace an
{
namespace
{

using composition_detail::channelRowOf;
using composition_detail::CompositionSource;
using composition_detail::WorkingBlock;
using composition_detail::WorkingPlanes;

constexpr const char* kResolveOperation = "an::resolveCometApertureRadii";
constexpr const char* kCircleOperation = "an::cometApertureCircleOf";
constexpr const char* kSplitPlanOperation = "an::planCometApertureSplit";
constexpr const char* kSplitLayoutOperation = "an::cometApertureSplitLayout";
constexpr const char* kSplitOperation = "an::splitCometAperture";
constexpr const char* kMaskOperation = "an::renderCometMask";
constexpr const char* kBlendPlanOperation = "an::planCometCompositeBlend";
constexpr const char* kBlendOperation = "an::blendCometComposite";

/// The product's radius from a measured full width: one and a half widths, never less than two pixels.
constexpr double kFullWidthRadiusScale = 1.5;
constexpr double kFullWidthRadiusFloor = 2.0;

/// The product's split feather: the larger of one pixel and this fraction of the radius.
constexpr double kSplitFeatherFraction = 0.35;
constexpr double kSplitFeatherFloor = 1.0;

/// The product's mask feather: the larger of two pixels and this fraction of the radius.
constexpr double kMaskFeatherFraction = 0.45;
constexpr double kMaskFeatherFloor = 2.0;

/// The product's sky annulus: from the larger of `kAnnulusInnerScale` radii and the radius plus
/// `kAnnulusInnerMargin`, out to the larger of that plus `kAnnulusWidthFloor` and `kAnnulusOuterScale` radii.
constexpr double kAnnulusInnerScale = 1.5;
constexpr double kAnnulusInnerMargin = 1.0;
constexpr double kAnnulusWidthFloor = 2.0;
constexpr double kAnnulusOuterScale = 2.5;

/// The drizzle model's enlargement range.
constexpr double kMinimumScale = 1.0;
constexpr double kMaximumScale = 4.0;

[[nodiscard]] Status compositeFailure(ErrorCode code, const char* message, const char* operation, const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

[[nodiscard]] bool positiveFinite(const std::optional<double>& value) noexcept
{
    return value.has_value() && std::isfinite(*value) && *value > 0.0;
}

[[nodiscard]] bool usableScale(double scale) noexcept
{
    return std::isfinite(scale) && scale >= kMinimumScale && scale <= kMaximumScale;
}

[[nodiscard]] bool finitePoint(const PixelCoordinate& point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

/// Whether a circle has a finite centre and a finite, positive radius.
[[nodiscard]] bool usableCircle(const CometApertureCircle& circle) noexcept
{
    return finitePoint(circle.center) && std::isfinite(circle.radius) && circle.radius > 0.0;
}

/// The product's feathered circle: one within the radius, falling linearly to zero across the feather.
[[nodiscard]] double featherWeight(double distance, double radius, double feather) noexcept
{
    if (distance <= radius)
    {
        return 1.0;
    }
    if (feather <= 0.0 || distance >= radius + feather)
    {
        return 0.0;
    }
    return 1.0 - ((distance - radius) / feather);
}

[[nodiscard]] CompositionSource compositionOf(const CometCompositeSource& source) noexcept
{
    return CompositionSource{.descriptor = source.descriptor,
                             .samples = source.samples,
                             .layout = source.layout,
                             .validity = source.validity};
}

[[nodiscard]] bool floating(SampleType type) noexcept
{
    return type == SampleType::Float32 || type == SampleType::Float64;
}

/// The exact median of a non-empty range, reordering it: the middle value, or the mean of the two middle
/// values for an even count, as the product took it.
template <typename ValueT> [[nodiscard]] double medianOf(ValueT* values, std::uint64_t count) noexcept
{
    const std::uint64_t middle = count / 2u;
    std::nth_element(values, values + middle, values + count);
    const double upper = static_cast<double>(values[middle]);
    if ((count % 2u) != 0u)
    {
        return upper;
    }
    const double lower = static_cast<double>(*(std::max_element)(values, values + middle));
    return 0.5 * (lower + upper);
}

/// The half-open run of pixel indices along one axis whose centres can lie within `reach` of `centre`.
struct PixelRun
{
    std::uint32_t begin = 0u;
    std::uint32_t end = 0u;
};

[[nodiscard]] PixelRun runWithin(double centre, double reach, std::uint32_t extent) noexcept
{
    // Pixel `i` has its centre at `i + 0.5`; one pixel of slack either side keeps the run a superset of the
    // pixels the exact distance test admits.
    const double first = std::floor(centre - reach - 0.5) - 1.0;
    const double last = std::ceil(centre + reach - 0.5) + 1.0;
    const double limit = static_cast<double>(extent);
    PixelRun run;
    run.begin = first <= 0.0 ? 0u : (first >= limit ? extent : static_cast<std::uint32_t>(first));
    run.end = last < 0.0 ? 0u : (last >= limit - 1.0 ? extent : static_cast<std::uint32_t>(last) + 1u);
    if (run.end < run.begin)
    {
        run.end = run.begin;
    }
    return run;
}

/// The most pixels of one row a split walks at a time, so that their flags fit a buffer on the stack.
constexpr std::uint32_t kSplitChunkPixels = 4096u;

/// Whether one bit of a packed validity mask is set.
[[nodiscard]] bool maskBit(const ValidityMaskView<const std::uint8_t>& mask, std::uint32_t plane, std::uint32_t x,
                           std::uint32_t y) noexcept
{
    const std::uint64_t offset = static_cast<std::uint64_t>(plane) * mask.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * mask.rowStrideBytes() + x / 8u;
    return (mask.data()[offset] & static_cast<std::uint8_t>(1u << (x % 8u))) != 0u;
}

/// A split's samples in the working planes an integer frame was read into, every rejected pixel flagged.
template <typename WorkingT> class WorkingSamples
{
public:
    /// Whether `flags` needs a caller's buffer to answer.
    static constexpr bool kFlagsNeedScratch = false;

    /// The planes and their flags.
    ///
    /// @param planes the working planes, borrowed for the split.
    /// @param valid one flag per pixel, row-major, zero where the pixel is rejected; null when none can be.
    WorkingSamples(const WorkingPlanes& planes, const std::uint8_t* valid) noexcept
        : m_planes(planes), m_valid(valid), m_width(planes.layout->extent().width())
    {
    }

    /// Whether some pixel may be rejected.
    [[nodiscard]] bool ruled() const noexcept
    {
        return m_valid != nullptr;
    }

    /// The first sample of one channel's row.
    [[nodiscard]] const WorkingT* row(std::uint32_t channel, std::uint32_t y) const noexcept
    {
        return m_planes.row<WorkingT>(channel, y);
    }

    /// How many samples apart one row's pixels are.
    [[nodiscard]] std::size_t stride() const noexcept
    {
        return 1u;
    }

    /// The working sample at `index` of a row.
    [[nodiscard]] static WorkingT value(const WorkingT* row, std::size_t index) noexcept
    {
        return row[index];
    }

    /// The flags of the pixels `[first, last)` of row `y`, element `x - first` zero where pixel `x` is rejected;
    /// null when no pixel can be.
    [[nodiscard]] const std::uint8_t* flags(std::uint32_t y, std::uint32_t first, std::uint32_t last,
                                            std::uint8_t* scratch) const noexcept
    {
        (void)last;
        (void)scratch;
        return m_valid != nullptr ? m_valid + static_cast<std::uint64_t>(y) * m_width + first : nullptr;
    }

private:
    /// The working planes.
    const WorkingPlanes& m_planes;

    /// The pixels' flags, or null.
    const std::uint8_t* m_valid;

    /// Pixels per row.
    std::uint32_t m_width;
};

/// A split's samples where a floating frame holds them, each read as the working planes would hold it.
///
/// A pixel is valid where every channel's own rule and mask admit its sample, which is how the working planes
/// flag one.
template <typename SourceT, typename WorkingT> class InPlaceSamples
{
public:
    /// Whether `flags` needs a caller's buffer to answer.
    static constexpr bool kFlagsNeedScratch = true;

    /// The frame, borrowed for the split.
    ///
    /// @param frame a checked floating source.
    explicit InPlaceSamples(const CometCompositeSource& frame) noexcept
        : m_frame(frame), m_channels(frame.descriptor->extent().channelCount()),
          m_stride(frame.layout->samplesPerPixel()),
          m_ruled(frame.validity.descriptor.rule() != ValidityRule::AllValid || frame.validity.mask != nullptr),
          m_ownPlanes(frame.validity.mask != nullptr && frame.validity.mask->planeCount() == m_channels &&
                      m_channels > 1u)
    {
    }

    /// Whether some pixel may be rejected.
    [[nodiscard]] bool ruled() const noexcept
    {
        return m_ruled;
    }

    /// The first sample of one channel's row.
    [[nodiscard]] const SourceT* row(std::uint32_t channel, std::uint32_t y) const noexcept
    {
        return channelRowOf<SourceT>(m_frame.samples, *m_frame.layout, channel, y);
    }

    /// How many samples apart one row's pixels are.
    [[nodiscard]] std::size_t stride() const noexcept
    {
        return m_stride;
    }

    /// The sample at `index` of a row in the working representation, as reading it into working planes converts it.
    [[nodiscard]] static WorkingT value(const SourceT* row, std::size_t index) noexcept
    {
        return static_cast<WorkingT>(static_cast<double>(row[index]));
    }

    /// Whether one channel's own rule and mask admit its sample `raw` at `(x, y)`.
    [[nodiscard]] bool admits(std::uint32_t channel, SourceT raw, std::uint32_t x, std::uint32_t y) const noexcept
    {
        const KernelValidityView& validity = m_frame.validity;
        return sampleIsValidByValue(validity.descriptor, raw) &&
               (validity.mask == nullptr || maskBit(*validity.mask, m_ownPlanes ? channel : validity.maskPlane, x, y));
    }

    /// The flags of the pixels `[first, last)` of row `y`, written into `scratch`: element `x - first` zero where
    /// some channel rejects pixel `x`. Null when no pixel can be rejected.
    [[nodiscard]] const std::uint8_t* flags(std::uint32_t y, std::uint32_t first, std::uint32_t last,
                                            std::uint8_t* scratch) const noexcept
    {
        if (!m_ruled)
        {
            return nullptr;
        }
        std::fill(scratch, scratch + (last - first), std::uint8_t{1u});
        for (std::uint32_t channel = 0u; channel < m_channels; ++channel)
        {
            const SourceT* const samples = row(channel, y);
            for (std::uint32_t x = first; x < last; ++x)
            {
                if (!admits(channel, samples[static_cast<std::size_t>(x) * m_stride], x, y))
                {
                    scratch[x - first] = 0u;
                }
            }
        }
        return scratch;
    }

private:
    /// The frame.
    const CometCompositeSource& m_frame;

    /// Its channel count.
    std::uint32_t m_channels;

    /// How many samples apart one row's pixels are.
    std::size_t m_stride;

    /// Whether a rule or a mask can reject a sample.
    bool m_ruled;

    /// Whether the mask gives every channel a plane of its own.
    bool m_ownPlanes;
};

/// Checks a floating frame read in place as reading it into working planes checks it: every sample its channel
/// admits is finite. Counts the pixels every channel admits.
///
/// @return success; `ErrorCode::InvalidData` for a sample that is not finite where its channel admits it; or the
///         execution's stop.
template <typename SourceT, typename WorkingT>
[[nodiscard]] Status checkInPlace(const ExecutionContext& execution, const InPlaceSamples<SourceT, WorkingT>& samples,
                                  std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                  std::uint64_t& accepted)
{
    std::atomic<bool> finite{true};
    std::atomic<std::uint64_t> admitted{0u};
    const std::size_t stride = samples.stride();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(SourceT),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels};
    const Status ran = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::array<std::uint8_t, kSplitChunkPixels> scratch{};
        std::uint64_t admittedHere = 0u;
        bool finiteHere = true;
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const auto y = static_cast<std::uint32_t>(y64);
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                const SourceT* const row = samples.row(channel, y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const SourceT raw = row[static_cast<std::size_t>(x) * stride];
                    if (!std::isfinite(static_cast<double>(raw)) &&
                        (!samples.ruled() || samples.admits(channel, raw, x, y)))
                    {
                        finiteHere = false;
                    }
                }
            }
            if (!samples.ruled())
            {
                admittedHere += width;
                continue;
            }
            for (std::uint32_t first = 0u; first < width; first += kSplitChunkPixels)
            {
                const std::uint32_t last = (std::min)(width, first + kSplitChunkPixels);
                const std::uint8_t* const rowValid = samples.flags(y, first, last, scratch.data());
                for (std::uint32_t x = first; x < last; ++x)
                {
                    admittedHere += rowValid[x - first];
                }
            }
        }
        admitted.fetch_add(admittedHere, std::memory_order_relaxed);
        if (!finiteHere)
        {
            finite.store(false, std::memory_order_relaxed);
        }
        return Status::success();
    });
    if (ran.failed())
    {
        return ran;
    }
    if (!finite.load(std::memory_order_relaxed))
    {
        return compositeFailure(ErrorCode::InvalidData,
                                "a source holds a sample that is not finite in a pixel its validity admits",
                                kSplitOperation, "source");
    }
    accepted = admitted.load(std::memory_order_relaxed);
    return Status::success();
}

/// Measures every channel's sky, writing the result's background fields.
template <typename WorkingT, typename SamplesT>
[[nodiscard]] Status measureBackgrounds(const ExecutionContext& execution, const SamplesT& samples, std::uint32_t width,
                                        std::uint32_t height, std::uint32_t channels,
                                        const CometApertureCircle& aperture, CometApertureSplitResult& result)
{
    const double radius = aperture.radius;
    const double inner = (std::max)(radius * kAnnulusInnerScale, radius + kAnnulusInnerMargin);
    const double outer = (std::max)(inner + kAnnulusWidthFloor, radius * kAnnulusOuterScale);
    const double inner2 = inner * inner;
    const double outer2 = outer * outer;
    const PixelRun columns = runWithin(aperture.center.x, outer, width);
    const PixelRun rows = runWithin(aperture.center.y, outer, height);
    const std::uint64_t boxPixels = static_cast<std::uint64_t>(columns.end - columns.begin) * (rows.end - rows.begin);
    const std::size_t stride = samples.stride();

    WorkingBlock annulusBlock;
    const Status takenAnnulus =
        annulusBlock.take(execution, (std::max)(boxPixels, std::uint64_t{1u}) * sizeof(WorkingT), kSplitOperation);
    if (takenAnnulus.failed())
    {
        return takenAnnulus;
    }
    result.workingBytes += (std::max)(boxPixels, std::uint64_t{1u}) * sizeof(WorkingT);
    auto* const annulus = reinterpret_cast<WorkingT*>(annulusBlock.bytes());
    WorkingBlock flagBlock;
    std::uint8_t* scratch = nullptr;
    if constexpr (SamplesT::kFlagsNeedScratch)
    {
        if (samples.ruled())
        {
            const Status takenFlags = flagBlock.take(execution, width, kSplitOperation);
            if (takenFlags.failed())
            {
                return takenFlags;
            }
            result.workingBytes += width;
            scratch = flagBlock.bytes();
        }
    }
    WorkingBlock wholeBlock;
    WorkingT* whole = nullptr;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        std::uint64_t count = 0u;
        for (std::uint32_t y = rows.begin; y < rows.end; ++y)
        {
            const auto* const row = samples.row(channel, y);
            const std::uint8_t* const rowValid = samples.flags(y, columns.begin, columns.end, scratch);
            const double dy = (static_cast<double>(y) + 0.5) - aperture.center.y;
            for (std::uint32_t x = columns.begin; x < columns.end; ++x)
            {
                if (rowValid != nullptr && rowValid[x - columns.begin] == 0u)
                {
                    continue;
                }
                const double dx = (static_cast<double>(x) + 0.5) - aperture.center.x;
                const double distance2 = dx * dx + dy * dy;
                if (distance2 >= inner2 && distance2 <= outer2)
                {
                    annulus[count++] = SamplesT::value(row, static_cast<std::size_t>(x) * stride);
                }
            }
        }
        result.annulusSamples[channel] = count;
        if (count >= kMinimumCometBackgroundSamples)
        {
            result.background[channel] = medianOf(annulus, count);
            continue;
        }
        // Too few samples around the aperture: the whole channel's median, as the product fell back to.
        if (whole == nullptr)
        {
            const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
            const Status takenWhole = wholeBlock.take(execution, pixels * sizeof(WorkingT), kSplitOperation);
            if (takenWhole.failed())
            {
                return takenWhole;
            }
            result.workingBytes += pixels * sizeof(WorkingT);
            whole = reinterpret_cast<WorkingT*>(wholeBlock.bytes());
        }
        std::uint64_t wholeCount = 0u;
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const auto* const row = samples.row(channel, y);
            const std::uint8_t* const rowValid = samples.flags(y, 0u, width, scratch);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (rowValid == nullptr || rowValid[x] != 0u)
                {
                    whole[wholeCount++] = SamplesT::value(row, static_cast<std::size_t>(x) * stride);
                }
            }
        }
        if (wholeCount == 0u)
        {
            return compositeFailure(ErrorCode::EmptySelection, "a channel holds no valid sample to take the sky from",
                                    kSplitOperation, "frame");
        }
        result.background[channel] = medianOf(whole, wholeCount);
        result.wholeChannelBackground[channel] = true;
        const Status cancelled = execution.checkCancellation();
        if (cancelled.failed())
        {
            return cancelled;
        }
    }
    return Status::success();
}

/// Writes both layers of every row.
template <typename WorkingT, typename SamplesT>
[[nodiscard]] Status writeLayers(const ExecutionContext& execution, const SamplesT& samples, std::uint32_t width,
                                 std::uint32_t height, std::uint32_t channels, const CometApertureSplitPlan& plan,
                                 const CometApertureSplitResult& measured, bool finiteRule, void* starLayer,
                                 const ImageLayout* starLayout, void* cometLayer, const ImageLayout* cometLayout)
{
    const CometApertureCircle& aperture = plan.request().aperture;
    const double radius = aperture.radius;
    const double feather = plan.feather();
    const double reach = radius + feather;
    const std::size_t stride = samples.stride();
    const std::size_t starStep = starLayer != nullptr ? starLayout->samplesPerPixel() : 0u;
    const std::size_t cometStep = cometLayer != nullptr ? cometLayout->samplesPerPixel() : 0u;
    const WorkingT rejected = finiteRule ? std::numeric_limits<WorkingT>::quiet_NaN() : WorkingT{0};
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * 3u * sizeof(WorkingT),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 8u};
    return parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::array<std::uint8_t, kSplitChunkPixels> scratch{};
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const auto y = static_cast<std::uint32_t>(y64);
            const double dy = (static_cast<double>(y) + 0.5) - aperture.center.y;
            const bool rowReached = std::fabs(dy) <= reach;
            for (std::uint32_t first = 0u; first < width; first += kSplitChunkPixels)
            {
                const std::uint32_t last = (std::min)(width, first + kSplitChunkPixels);
                const std::uint8_t* const rowValid = samples.flags(y, first, last, scratch.data());
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    const auto* const in = samples.row(channel, y);
                    WorkingT* const star =
                        starLayer != nullptr ? channelRowOf<WorkingT>(starLayer, *starLayout, channel, y) : nullptr;
                    WorkingT* const comet =
                        cometLayer != nullptr ? channelRowOf<WorkingT>(cometLayer, *cometLayout, channel, y) : nullptr;
                    const double sky = measured.background[channel];
                    for (std::uint32_t x = first; x < last; ++x)
                    {
                        if (rowValid != nullptr && rowValid[x - first] == 0u)
                        {
                            if (star != nullptr)
                            {
                                star[x * starStep] = rejected;
                            }
                            if (comet != nullptr)
                            {
                                comet[x * cometStep] = rejected;
                            }
                            continue;
                        }
                        double weight = 0.0;
                        if (rowReached)
                        {
                            const double dx = (static_cast<double>(x) + 0.5) - aperture.center.x;
                            if (std::fabs(dx) <= reach)
                            {
                                weight = featherWeight(std::hypot(dx, dy), radius, feather);
                            }
                        }
                        const double source =
                            static_cast<double>(SamplesT::value(in, static_cast<std::size_t>(x) * stride));
                        const double starValue = source * (1.0 - weight) + sky * weight;
                        const double cometValue = sky + (source - starValue);
                        if (star != nullptr)
                        {
                            star[x * starStep] = static_cast<WorkingT>(starValue);
                        }
                        if (comet != nullptr)
                        {
                            comet[x * cometStep] = static_cast<WorkingT>(cometValue);
                        }
                    }
                }
            }
        }
        return Status::success();
    });
}

/// Splits a floating frame read where it lies: checked, measured and written with no working copy.
template <typename SourceT, typename WorkingT>
[[nodiscard]] Status splitInPlace(const ExecutionContext& execution, const CometCompositeSource& frame,
                                  const CometApertureSplitPlan& plan, bool finiteRule, void* starLayer,
                                  const ImageLayout* starLayout, void* cometLayer, const ImageLayout* cometLayout,
                                  CometApertureSplitResult& result, std::uint64_t& accepted)
{
    const ImageExtent& extent = frame.descriptor->extent();
    const InPlaceSamples<SourceT, WorkingT> samples(frame);
    const Status checked =
        checkInPlace(execution, samples, extent.width(), extent.height(), extent.channelCount(), accepted);
    if (checked.failed())
    {
        return checked;
    }
    const Status measured = measureBackgrounds<WorkingT>(execution, samples, extent.width(), extent.height(),
                                                         extent.channelCount(), plan.request().aperture, result);
    if (measured.failed())
    {
        return measured;
    }
    return writeLayers<WorkingT>(execution, samples, extent.width(), extent.height(), extent.channelCount(), plan,
                                 result, finiteRule, starLayer, starLayout, cometLayer, cometLayout);
}

/// Splits an integer frame through working planes of the layers' representation, every rejected pixel flagged.
template <typename WorkingT>
[[nodiscard]] Status splitThroughWorkingPlanes(const ExecutionContext& execution, const CompositionSource& source,
                                               const CometApertureSplitPlan& plan, bool finiteRule, void* starLayer,
                                               const ImageLayout* starLayout, void* cometLayer,
                                               const ImageLayout* cometLayout, CometApertureSplitResult& result,
                                               std::uint64_t& accepted)
{
    const ImageExtent& extent = source.descriptor->extent();
    const std::uint32_t width = extent.width();
    const std::uint32_t height = extent.height();
    const std::uint32_t channels = extent.channelCount();
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    WorkingBlock planesBlock;
    WorkingBlock validBlock;
    const Result<WorkingPlanes> planes = composition_detail::takeWorkingPlanes(
        execution, planesBlock, width, height, channels, plan.request().outputSampleType, kSplitOperation);
    if (!planes.hasValue())
    {
        return planes.status();
    }
    result.workingBytes += planes->layout->totalBytes();
    std::uint8_t* valid = nullptr;
    if (composition_detail::compositionSourceRuled(source))
    {
        const Status taken = validBlock.take(execution, pixels, kSplitOperation);
        if (taken.failed())
        {
            return taken;
        }
        result.workingBytes += pixels;
        valid = validBlock.bytes();
        std::memset(valid, 1, static_cast<std::size_t>(pixels));
    }
    const Status read = composition_detail::readWorking(execution, source, *planes, 0u, valid, kSplitOperation);
    if (read.failed())
    {
        return read;
    }
    const WorkingSamples<WorkingT> samples(*planes, valid);
    const Status measured =
        measureBackgrounds<WorkingT>(execution, samples, width, height, channels, plan.request().aperture, result);
    if (measured.failed())
    {
        return measured;
    }
    const Status written = writeLayers<WorkingT>(execution, samples, width, height, channels, plan, result, finiteRule,
                                                 starLayer, starLayout, cometLayer, cometLayout);
    if (written.failed())
    {
        return written;
    }
    accepted = pixels;
    if (valid != nullptr)
    {
        accepted = 0u;
        for (std::uint64_t index = 0u; index < pixels; ++index)
        {
            accepted += valid[index];
        }
    }
    return Status::success();
}

/// Whether two byte ranges share a byte.
[[nodiscard]] bool overlaps(const void* first, std::uint64_t firstBytes, const void* second,
                            std::uint64_t secondBytes) noexcept
{
    const auto a = reinterpret_cast<std::uintptr_t>(first);
    const auto b = reinterpret_cast<std::uintptr_t>(second);
    return a < b + secondBytes && b < a + firstBytes;
}

/// One floating sample of a source and whether its validity admits it.
template <typename SampleT>
[[nodiscard]] bool admitted(const CometCompositeSource& source, SampleT value, std::uint32_t plane, std::uint32_t x,
                            std::uint32_t y) noexcept
{
    if (!sampleIsValidByValue(source.validity.descriptor, value))
    {
        return false;
    }
    if (source.validity.mask == nullptr)
    {
        return true;
    }
    const std::optional<bool> bit = source.validity.mask->test(plane, x, y);
    return bit.has_value() && *bit;
}

/// The mask plane a source's channel is judged by.
[[nodiscard]] std::uint32_t maskPlaneOf(const CometCompositeSource& source, std::uint32_t channel) noexcept
{
    const std::uint32_t channels = source.descriptor->extent().channelCount();
    if (source.validity.mask != nullptr && source.validity.mask->planeCount() == channels && channels > 1u)
    {
        return channel;
    }
    return source.validity.maskPlane;
}

/// The blend's walk over one combination of the three sources' representations.
template <typename StarT, typename CometT, typename MaskT>
[[nodiscard]] Status blendWalk(const ExecutionContext& execution, const CometCompositeBlendRequest& request,
                               const CometCompositeSource& star, const CometCompositeSource& comet,
                               const CometCompositeSource& mask, void* destination,
                               const ImageLayout& destinationLayout, CometCompositeBlendResult& result)
{
    const ImageExtent& extent = star.descriptor->extent();
    const std::uint32_t width = extent.width();
    const std::uint32_t height = extent.height();
    const std::uint32_t channels = extent.channelCount();
    const std::uint32_t cometWidth = comet.descriptor->extent().width();
    const std::uint32_t cometHeight = comet.descriptor->extent().height();
    const double lastColumn = static_cast<double>(cometWidth) - 1.0;
    const double lastRow = static_cast<double>(cometHeight) - 1.0;
    const std::size_t starStep = star.layout->samplesPerPixel();
    const std::size_t cometStep = comet.layout->samplesPerPixel();
    const std::size_t maskStep = mask.layout->samplesPerPixel();
    const std::size_t outStep = destinationLayout.samplesPerPixel();
    const double scale = request.scale;
    const std::uint32_t maskPlane = maskPlaneOf(mask, 0u);
    std::atomic<std::uint64_t> weighed{0u};
    std::atomic<std::uint64_t> blended{0u};
    std::atomic<std::uint64_t> kept{0u};
    std::atomic<std::uint64_t> unmapped{0u};
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * (channels * 2u + 1u) * sizeof(StarT),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 4u};
    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::uint64_t rangeWeighed = 0u;
        std::uint64_t rangeBlended = 0u;
        std::uint64_t rangeKept = 0u;
        std::uint64_t rangeUnmapped = 0u;
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const auto y = static_cast<std::uint32_t>(y64);
            const MaskT* const weights = channelRowOf<MaskT>(mask.samples, *mask.layout, 0u, y);
            std::array<const StarT*, kMaxCometCompositeChannels> starRows{};
            std::array<StarT*, kMaxCometCompositeChannels> outRows{};
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                starRows[channel] = channelRowOf<StarT>(star.samples, *star.layout, channel, y);
                outRows[channel] = channelRowOf<StarT>(destination, destinationLayout, channel, y);
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                double weight = 0.0;
                const MaskT rawWeight = weights[x * maskStep];
                if (admitted(mask, rawWeight, maskPlane, x, y))
                {
                    if (!std::isfinite(static_cast<double>(rawWeight)))
                    {
                        return compositeFailure(ErrorCode::InvalidData,
                                                "the mask holds a sample that is not finite in a pixel its validity "
                                                "admits",
                                                kBlendOperation, "mask");
                    }
                    const double held = static_cast<double>(rawWeight);
                    weight = held > 0.0 ? (held < 1.0 ? held : 1.0) : 0.0;
                }
                bool starValid = weight > 0.0;
                for (std::uint32_t channel = 0u; channel < channels && starValid; ++channel)
                {
                    const StarT value = starRows[channel][x * starStep];
                    if (!admitted(star, value, maskPlaneOf(star, channel), x, y))
                    {
                        starValid = false;
                    }
                    else if (!std::isfinite(static_cast<double>(value)))
                    {
                        return compositeFailure(ErrorCode::InvalidData,
                                                "the star stack holds a sample that is not finite in a pixel its "
                                                "validity admits",
                                                kBlendOperation, "star");
                    }
                }
                if (!starValid)
                {
                    for (std::uint32_t channel = 0u; channel < channels; ++channel)
                    {
                        outRows[channel][x * outStep] = starRows[channel][x * starStep];
                    }
                    continue;
                }
                ++rangeWeighed;

                // The pixel's centre on the registration plane, back to the reference frame's source plane
                // and forward onto the comet stack's.
                double u = static_cast<double>(x);
                double v = static_cast<double>(y);
                const PixelCoordinate onStar{.x = (static_cast<double>(x) + 0.5) / scale,
                                             .y = (static_cast<double>(y) + 0.5) / scale};
                const Result<RegistrationInverseResult> source =
                    mapRegistrationPointInverse(request.starTransform, onStar, request.starPostTranslation);
                bool mapped = false;
                if (source.hasValue())
                {
                    const Result<PixelCoordinate> onComet =
                        mapRegistrationPoint(request.cometTransform, source->pixel, request.cometPostTranslation);
                    if (onComet.hasValue())
                    {
                        u = onComet->x * scale - 0.5;
                        v = onComet->y * scale - 0.5;
                        mapped = true;
                    }
                }
                if (!mapped)
                {
                    ++rangeUnmapped;
                }

                // The four nearest comet samples, or zero outside the rectangle their centres span.
                std::array<double, kMaxCometCompositeChannels> cometValue{};
                bool cometValid = true;
                if (std::isfinite(u) && std::isfinite(v) && u >= 0.0 && v >= 0.0 && u <= lastColumn && v <= lastRow)
                {
                    const auto x0 = static_cast<std::uint32_t>(std::floor(u));
                    const auto y0 = static_cast<std::uint32_t>(std::floor(v));
                    const std::uint32_t x1 = (std::min)(x0 + 1u, cometWidth - 1u);
                    const std::uint32_t y1 = (std::min)(y0 + 1u, cometHeight - 1u);
                    const double fx = u - static_cast<double>(x0);
                    const double fy = v - static_cast<double>(y0);
                    for (std::uint32_t channel = 0u; channel < channels && cometValid; ++channel)
                    {
                        const std::uint32_t plane = maskPlaneOf(comet, channel);
                        const CometT* const top = channelRowOf<CometT>(comet.samples, *comet.layout, channel, y0);
                        const CometT* const bottom = channelRowOf<CometT>(comet.samples, *comet.layout, channel, y1);
                        const CometT v00 = top[x0 * cometStep];
                        const CometT v10 = top[x1 * cometStep];
                        const CometT v01 = bottom[x0 * cometStep];
                        const CometT v11 = bottom[x1 * cometStep];
                        if (!admitted(comet, v00, plane, x0, y0) || !admitted(comet, v10, plane, x1, y0) ||
                            !admitted(comet, v01, plane, x0, y1) || !admitted(comet, v11, plane, x1, y1))
                        {
                            cometValid = false;
                            break;
                        }
                        if (!std::isfinite(static_cast<double>(v00)) || !std::isfinite(static_cast<double>(v10)) ||
                            !std::isfinite(static_cast<double>(v01)) || !std::isfinite(static_cast<double>(v11)))
                        {
                            return compositeFailure(ErrorCode::InvalidData,
                                                    "the comet stack holds a sample that is not finite in a pixel its "
                                                    "validity admits",
                                                    kBlendOperation, "comet");
                        }
                        const double upper = static_cast<double>(v00) * (1.0 - fx) + static_cast<double>(v10) * fx;
                        const double lower = static_cast<double>(v01) * (1.0 - fx) + static_cast<double>(v11) * fx;
                        cometValue[channel] = upper * (1.0 - fy) + lower * fy;
                    }
                }
                if (!cometValid)
                {
                    ++rangeKept;
                    for (std::uint32_t channel = 0u; channel < channels; ++channel)
                    {
                        outRows[channel][x * outStep] = starRows[channel][x * starStep];
                    }
                    continue;
                }
                ++rangeBlended;
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    const double starValue = static_cast<double>(starRows[channel][x * starStep]);
                    outRows[channel][x * outStep] =
                        static_cast<StarT>(starValue * (1.0 - weight) + cometValue[channel] * weight);
                }
            }
        }
        weighed.fetch_add(rangeWeighed, std::memory_order_relaxed);
        blended.fetch_add(rangeBlended, std::memory_order_relaxed);
        kept.fetch_add(rangeKept, std::memory_order_relaxed);
        unmapped.fetch_add(rangeUnmapped, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    result.run.selection.total = pixels;
    result.run.selection.valid = blended.load(std::memory_order_relaxed);
    result.run.outputsWritten = pixels * channels;
    result.blendedPixels = blended.load(std::memory_order_relaxed);
    result.keptPixels = kept.load(std::memory_order_relaxed);
    result.unmappedPixels = unmapped.load(std::memory_order_relaxed);
    (void)weighed;
    return Status::success();
}

} // namespace

std::optional<double> resolveCometApertureRadius(const CometApertureMeasurement& measurement) noexcept
{
    if (positiveFinite(measurement.apertureRadius))
    {
        return *measurement.apertureRadius;
    }
    if (positiveFinite(measurement.measuredDiameter))
    {
        return *measurement.measuredDiameter * 0.5;
    }
    if (positiveFinite(measurement.measuredFullWidth))
    {
        return (std::max)(kFullWidthRadiusFloor, *measurement.measuredFullWidth * kFullWidthRadiusScale);
    }
    return std::nullopt;
}

Result<std::uint32_t> resolveCometApertureRadii(std::span<const CometApertureMeasurement> measurements,
                                                std::span<double> radii)
{
    if (radii.size() != measurements.size())
    {
        return compositeFailure(ErrorCode::IncompatibleArguments, "radii does not hold one radius per measurement",
                                kResolveOperation, "radii");
    }
    // The resolved radii are gathered at the front of the destination and ranked there, then every frame's
    // radius is written in order.
    std::uint64_t resolved = 0u;
    for (const CometApertureMeasurement& measurement : measurements)
    {
        const std::optional<double> radius = resolveCometApertureRadius(measurement);
        if (radius.has_value())
        {
            radii[static_cast<std::size_t>(resolved++)] = *radius;
        }
    }
    if (resolved == 0u)
    {
        return compositeFailure(ErrorCode::EmptySelection, "no measurement resolves a comet aperture radius",
                                kResolveOperation, "measurements");
    }
    const std::size_t middle = static_cast<std::size_t>(resolved / 2u);
    std::nth_element(radii.begin(), radii.begin() + static_cast<std::ptrdiff_t>(middle),
                     radii.begin() + static_cast<std::ptrdiff_t>(resolved));
    const double runRadius = radii[middle];
    std::uint32_t filled = 0u;
    for (std::size_t frame = 0u; frame < measurements.size(); ++frame)
    {
        const std::optional<double> radius = resolveCometApertureRadius(measurements[frame]);
        if (radius.has_value())
        {
            radii[frame] = *radius;
        }
        else
        {
            radii[frame] = runRadius;
            ++filled;
        }
    }
    return filled;
}

Result<CometApertureCircle> cometApertureCircleOf(const CometApertureProjection& projection, double sourceRadius,
                                                  double scale)
{
    if (!std::isfinite(sourceRadius) || sourceRadius <= 0.0)
    {
        return compositeFailure(ErrorCode::InvalidArgument, "the source radius must be finite and positive",
                                kCircleOperation, "sourceRadius");
    }
    if (!usableScale(scale))
    {
        return compositeFailure(ErrorCode::InvalidArgument, "the scale must be finite and between one and four",
                                kCircleOperation, "scale");
    }
    if (!finitePoint(projection.outputCenter))
    {
        return compositeFailure(ErrorCode::InvalidArgument, "the projected centre must be finite", kCircleOperation,
                                "projection");
    }
    double sum = 0.0;
    std::uint64_t count = 0u;
    for (const PixelCoordinate& point : projection.outputPolygon)
    {
        const double distance = std::hypot(point.x - projection.outputCenter.x, point.y - projection.outputCenter.y);
        if (std::isfinite(distance) && distance > 0.0)
        {
            sum += distance;
            ++count;
        }
    }
    const double radius = count > 0u ? sum / static_cast<double>(count) : sourceRadius;
    return CometApertureCircle{
        .center = {.x = projection.outputCenter.x * scale, .y = projection.outputCenter.y * scale},
        .radius = radius * scale};
}

Result<CometApertureSplitPlan> planCometApertureSplit(const CometApertureSplitRequest& request)
{
    if (!finitePoint(request.aperture.center))
    {
        return compositeFailure(ErrorCode::InvalidArgument, "the aperture's centre must be finite", kSplitPlanOperation,
                                "request.aperture");
    }
    if (!std::isfinite(request.aperture.radius) || request.aperture.radius <= 0.0)
    {
        return compositeFailure(ErrorCode::InvalidArgument, "the aperture's radius must be finite and positive",
                                kSplitPlanOperation, "request.aperture");
    }
    if (request.feather.has_value() && (!std::isfinite(*request.feather) || *request.feather < 0.0))
    {
        return compositeFailure(ErrorCode::InvalidArgument, "the feather must be finite and not negative",
                                kSplitPlanOperation, "request.feather");
    }
    if (!floating(request.outputSampleType))
    {
        return compositeFailure(ErrorCode::InvalidArgument, "the layers are written in Float32 or Float64",
                                kSplitPlanOperation, "request.outputSampleType");
    }
    const double feather =
        request.feather.value_or((std::max)(kSplitFeatherFloor, request.aperture.radius * kSplitFeatherFraction));
    return CometApertureSplitPlan(request, feather);
}

Result<ImageLayoutRequest> cometApertureSplitLayout(const CometApertureSplitPlan& plan, const ImageDescriptor& frame)
{
    if (frame.extent().channelCount() > kMaxCometCompositeChannels)
    {
        return compositeFailure(ErrorCode::IncompatibleArguments, "a comet composite reads at most four channels",
                                kSplitLayoutOperation, "frame");
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(plan.request().outputSampleType);
    return ImageLayoutRequest{.extent = frame.extent(),
                              .elementSizeBytes = elementBytes,
                              .samplesPerPixel = 1u,
                              .planeCount = frame.extent().channelCount(),
                              .rowAlignmentBytes = elementBytes};
}

Result<CometApertureSplitResult> splitCometAperture(const ExecutionContext& execution,
                                                    const CometApertureSplitPlan& plan,
                                                    const CometCompositeSource& frame, void* starLayer,
                                                    const ImageLayout* starLayout, void* cometLayer,
                                                    const ImageLayout* cometLayout)
{
    if (!execution.valid())
    {
        return compositeFailure(ErrorCode::NotInitialized, "a split was given an empty execution context",
                                kSplitOperation, "execution");
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    const CompositionSource source = compositionOf(frame);
    const Status checked = composition_detail::checkCompositionSource(source, kSplitOperation, "frame");
    if (checked.failed())
    {
        return checked;
    }
    const ImageExtent& extent = frame.descriptor->extent();
    if (extent.channelCount() > kMaxCometCompositeChannels)
    {
        return compositeFailure(ErrorCode::IncompatibleArguments, "a comet composite reads at most four channels",
                                kSplitOperation, "frame");
    }
    const ValidityRule rule = frame.validity.descriptor.rule();
    if (rule == ValidityRule::ZeroIsNoData || rule == ValidityRule::DomainRange)
    {
        return compositeFailure(ErrorCode::Unsupported,
                                "a layer's arithmetic can land on what a zero-as-no-data or range rule rejects, so "
                                "the split cannot keep such a rule",
                                kSplitOperation, "frame");
    }
    if (starLayer == nullptr && cometLayer == nullptr)
    {
        return compositeFailure(ErrorCode::InvalidArgument, "a split writes at least one of its layers",
                                kSplitOperation, "starLayer");
    }
    const SampleType outputType = plan.request().outputSampleType;
    for (const auto& [layer, layout, parameter] :
         {std::tuple{starLayer, starLayout, "starLayout"}, std::tuple{cometLayer, cometLayout, "cometLayout"}})
    {
        if (layer == nullptr)
        {
            continue;
        }
        if (layout == nullptr)
        {
            return compositeFailure(ErrorCode::InvalidArgument, "a layer that is written needs its layout",
                                    kSplitOperation, parameter);
        }
        const Status destination =
            composition_detail::checkCompositionDestination(outputType, layer, *layout, extent, kSplitOperation);
        if (destination.failed())
        {
            return destination;
        }
        const Status separate =
            composition_detail::checkCompositionSeparate(source, layer, layout->totalBytes(), kSplitOperation, "frame");
        if (separate.failed())
        {
            return separate;
        }
    }
    if (starLayer != nullptr && cometLayer != nullptr &&
        overlaps(starLayer, starLayout->totalBytes(), cometLayer, cometLayout->totalBytes()))
    {
        return compositeFailure(ErrorCode::IncompatibleArguments, "the two layers share memory", kSplitOperation,
                                "cometLayer");
    }

    const std::uint32_t channels = extent.channelCount();
    const std::uint64_t pixels = static_cast<std::uint64_t>(extent.width()) * extent.height();
    CometApertureSplitResult result;
    const bool finiteRule = rule == ValidityRule::FiniteOnly;
    std::uint64_t accepted = pixels;
    Status written = Status::success();
    const bool wide = outputType == SampleType::Float64;
    if (frame.descriptor->sampleType() == SampleType::Float32)
    {
        written = wide ? splitInPlace<float, double>(execution, frame, plan, finiteRule, starLayer, starLayout,
                                                     cometLayer, cometLayout, result, accepted)
                       : splitInPlace<float, float>(execution, frame, plan, finiteRule, starLayer, starLayout,
                                                    cometLayer, cometLayout, result, accepted);
    }
    else if (frame.descriptor->sampleType() == SampleType::Float64)
    {
        written = wide ? splitInPlace<double, double>(execution, frame, plan, finiteRule, starLayer, starLayout,
                                                      cometLayer, cometLayout, result, accepted)
                       : splitInPlace<double, float>(execution, frame, plan, finiteRule, starLayer, starLayout,
                                                     cometLayer, cometLayout, result, accepted);
    }
    else
    {
        written = wide ? splitThroughWorkingPlanes<double>(execution, source, plan, finiteRule, starLayer, starLayout,
                                                           cometLayer, cometLayout, result, accepted)
                       : splitThroughWorkingPlanes<float>(execution, source, plan, finiteRule, starLayer, starLayout,
                                                          cometLayer, cometLayout, result, accepted);
    }
    if (written.failed())
    {
        return written;
    }
    result.run.selection.total = pixels;
    result.run.selection.valid = accepted;
    result.run.outputsWritten = pixels * channels;
    return result;
}

Result<CometMaskResult> renderCometMask(const ExecutionContext& execution,
                                        std::span<const CometApertureCircle> apertures, const CometMaskRequest& request,
                                        void* destination, const ImageLayout& destinationLayout)
{
    if (!execution.valid())
    {
        return compositeFailure(ErrorCode::NotInitialized, "a mask was given an empty execution context",
                                kMaskOperation, "execution");
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    if (destination == nullptr)
    {
        return compositeFailure(ErrorCode::InvalidArgument, "the mask's destination is null", kMaskOperation,
                                "destination");
    }
    if (destinationLayout.extent().channelCount() != 1u || destinationLayout.elementSizeBytes() != sizeof(float) ||
        reinterpret_cast<std::uintptr_t>(destination) % alignof(float) != 0u)
    {
        return compositeFailure(ErrorCode::InvalidArgument, "the mask is one channel of aligned four-byte samples",
                                kMaskOperation, "destinationLayout");
    }
    if (request.feather.has_value() && (!std::isfinite(*request.feather) || *request.feather < 0.0))
    {
        return compositeFailure(ErrorCode::InvalidArgument, "the feather must be finite and not negative",
                                kMaskOperation, "request.feather");
    }
    for (const CometApertureCircle& aperture : apertures)
    {
        if (!usableCircle(aperture))
        {
            return compositeFailure(ErrorCode::InvalidArgument,
                                    "every aperture has a finite centre and a finite, positive radius", kMaskOperation,
                                    "apertures");
        }
    }

    const std::uint32_t width = destinationLayout.extent().width();
    const std::uint32_t height = destinationLayout.extent().height();
    const std::size_t step = destinationLayout.samplesPerPixel();
    const auto featherOf = [&request](const CometApertureCircle& aperture) {
        return request.feather.value_or((std::max)(kMaskFeatherFloor, aperture.radius * kMaskFeatherFraction));
    };
    std::atomic<std::uint64_t> covered{0u};
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(float),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * (1u + apertures.size())};
    const Status drawn = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::uint64_t rangeCovered = 0u;
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const auto y = static_cast<std::uint32_t>(y64);
            float* const row = channelRowOf<float>(destination, destinationLayout, 0u, y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                row[x * step] = 0.0f;
            }
            const double dy0 = static_cast<double>(y) + 0.5;
            for (const CometApertureCircle& aperture : apertures)
            {
                const double feather = featherOf(aperture);
                const double reach = aperture.radius + feather;
                const double dy = dy0 - aperture.center.y;
                if (std::fabs(dy) > reach)
                {
                    continue;
                }
                const PixelRun columns = runWithin(aperture.center.x, reach, width);
                for (std::uint32_t x = columns.begin; x < columns.end; ++x)
                {
                    const double dx = (static_cast<double>(x) + 0.5) - aperture.center.x;
                    const double weight = featherWeight(std::hypot(dx, dy), aperture.radius, feather);
                    if (weight <= 0.0)
                    {
                        continue;
                    }
                    float& current = row[x * step];
                    current = static_cast<float>((std::max)(static_cast<double>(current), weight));
                }
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                rangeCovered += row[x * step] > 0.0f ? 1u : 0u;
            }
        }
        covered.fetch_add(rangeCovered, std::memory_order_relaxed);
        return Status::success();
    });
    if (drawn.failed())
    {
        return drawn;
    }
    return CometMaskResult{.apertures = static_cast<std::uint32_t>(apertures.size()),
                           .coveredPixels = covered.load(std::memory_order_relaxed)};
}

Result<CometCompositeBlendPlan> planCometCompositeBlend(const CometCompositeBlendRequest& request)
{
    if (!finitePoint(request.starPostTranslation))
    {
        return compositeFailure(ErrorCode::InvalidArgument, "the star post translation must be finite",
                                kBlendPlanOperation, "request.starPostTranslation");
    }
    if (!finitePoint(request.cometPostTranslation))
    {
        return compositeFailure(ErrorCode::InvalidArgument, "the comet post translation must be finite",
                                kBlendPlanOperation, "request.cometPostTranslation");
    }
    if (!usableScale(request.scale))
    {
        return compositeFailure(ErrorCode::InvalidArgument, "the scale must be finite and between one and four",
                                kBlendPlanOperation, "request.scale");
    }
    return CometCompositeBlendPlan(request);
}

Result<CometCompositeBlendResult>
blendCometComposite(const ExecutionContext& execution, const CometCompositeBlendPlan& plan,
                    const CometCompositeSource& star, const CometCompositeSource& comet,
                    const CometCompositeSource& mask, void* destination, const ImageLayout& destinationLayout)
{
    if (!execution.valid())
    {
        return compositeFailure(ErrorCode::NotInitialized, "a blend was given an empty execution context",
                                kBlendOperation, "execution");
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    const CompositionSource starSource = compositionOf(star);
    const CompositionSource cometSource = compositionOf(comet);
    const CompositionSource maskSource = compositionOf(mask);
    for (const auto& [source, parameter] :
         {std::pair{&starSource, "star"}, std::pair{&cometSource, "comet"}, std::pair{&maskSource, "mask"}})
    {
        const Status checked = composition_detail::checkCompositionSource(*source, kBlendOperation, parameter);
        if (checked.failed())
        {
            return checked;
        }
    }
    for (const auto& [source, parameter] :
         {std::pair{&star, "star"}, std::pair{&comet, "comet"}, std::pair{&mask, "mask"}})
    {
        if (!floating(source->descriptor->sampleType()))
        {
            return compositeFailure(ErrorCode::IncompatibleArguments,
                                    "the stacks and the mask a composite blends are written in a floating "
                                    "representation",
                                    kBlendOperation, parameter);
        }
    }
    const ImageExtent& extent = star.descriptor->extent();
    if (extent.channelCount() > kMaxCometCompositeChannels)
    {
        return compositeFailure(ErrorCode::IncompatibleArguments, "a comet composite reads at most four channels",
                                kBlendOperation, "star");
    }
    if (comet.descriptor->extent().channelCount() != extent.channelCount())
    {
        return compositeFailure(ErrorCode::IncompatibleArguments, "the comet stack is not the star stack's channels",
                                kBlendOperation, "comet");
    }
    const ImageExtent& maskExtent = mask.descriptor->extent();
    if (maskExtent.channelCount() != 1u || maskExtent.width() != extent.width() ||
        maskExtent.height() != extent.height())
    {
        return compositeFailure(ErrorCode::IncompatibleArguments,
                                "the mask is not one channel at the star stack's width and height", kBlendOperation,
                                "mask");
    }
    const SampleType outputType = star.descriptor->sampleType();
    const Status destinationChecked = composition_detail::checkCompositionDestination(
        outputType, destination, destinationLayout, extent, kBlendOperation);
    if (destinationChecked.failed())
    {
        return destinationChecked;
    }
    const std::uint64_t destinationBytes = destinationLayout.totalBytes();
    for (const auto& [source, parameter] :
         {std::pair{&starSource, "star"}, std::pair{&cometSource, "comet"}, std::pair{&maskSource, "mask"}})
    {
        const Status separate = composition_detail::checkCompositionSeparate(*source, destination, destinationBytes,
                                                                             kBlendOperation, parameter);
        if (separate.failed())
        {
            return separate;
        }
    }

    CometCompositeBlendResult result;
    const CometCompositeBlendRequest& request = plan.request();
    const auto walk = [&]<typename StarT, typename CometT>() -> Status {
        return mask.descriptor->sampleType() == SampleType::Float64
                   ? blendWalk<StarT, CometT, double>(execution, request, star, comet, mask, destination,
                                                      destinationLayout, result)
                   : blendWalk<StarT, CometT, float>(execution, request, star, comet, mask, destination,
                                                     destinationLayout, result);
    };
    const bool starDouble = outputType == SampleType::Float64;
    const bool cometDouble = comet.descriptor->sampleType() == SampleType::Float64;
    Status walked = Status::success();
    if (starDouble)
    {
        walked = cometDouble ? walk.template operator()<double, double>() : walk.template operator()<double, float>();
    }
    else
    {
        walked = cometDouble ? walk.template operator()<float, double>() : walk.template operator()<float, float>();
    }
    if (walked.failed())
    {
        return walked;
    }
    return result;
}

} // namespace an
