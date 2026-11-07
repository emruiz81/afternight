// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the numerical core of drawing a mask by hand: brush tip, dab placement, dab
// stamping and shape feathering.
//
// The tip, the dab positions and the stamp are Float32 arithmetic written in a fixed order, so a brush and a
// stroke always give the same samples. The stamp runs on the operation's threads by rows: each range of rows
// walks every dab in stroke order and lays the rows of it that fall in the range, so a pixel takes its dabs
// in the order a serial walk would and the plane does not depend on the number of threads. The feather
// composes the engine's exact Gaussian.

#include <AfterNightEngine/Algorithms/Spatial/DrawingPrimitives.h>

#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <span>

namespace an
{
namespace
{

constexpr const char* kPlanBrushOperation = "an::planBrush";
constexpr const char* kTipOperation = "an::writeBrushTip";
constexpr const char* kCountOperation = "an::countBrushSegmentDabs";
constexpr const char* kPlaceOperation = "an::placeBrushSegmentDabs";
constexpr const char* kStampOperation = "an::stampBrushDabs";
constexpr const char* kPlanFeatherOperation = "an::planCoverageFeather";
constexpr const char* kFeatherOperation = "an::featherCoverage";

/// The exponent of the soft tip's fall-off, `exp(-4 t^2)`.
constexpr float kSoftFalloffExponent = 4.0f;

/// A hardness at or above which the tip is hard, with no fall-off at all.
constexpr float kHardTipHardness = 0.999f;

/// The narrowest fall-off band, so a hardness just below hard does not divide by nothing.
constexpr float kMinimumFalloffRange = 0.0001f;

/// An estimated segment length at or below which a segment is a point.
constexpr float kDegenerateSegmentLength = 0.001f;

/// The largest standard deviation one feather pass may have: its kernel, truncated at four standard
/// deviations, then reaches the engine's largest stencil exactly.
constexpr double kLargestPassSigma = 32.0;

[[nodiscard]] Status drawingFailure(ErrorCode code, const char* message, const char* operation, const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// A sample read as a coverage: below zero or not a number is zero, above one is one.
[[nodiscard]] inline float coverageOf(float value) noexcept
{
    return value > 0.0f ? (value < 1.0f ? value : 1.0f) : 0.0f;
}

/// Whether a value is a number from zero to one.
[[nodiscard]] constexpr bool unitInterval(float value) noexcept
{
    return value >= 0.0f && value <= 1.0f;
}

/// The bytes a one-sample-per-pixel Float32 plane's samples span, from its first sample to the end of its
/// last row.
template <typename SampleT> [[nodiscard]] std::uint64_t spannedBytes(const PlaneView<SampleT>& plane) noexcept
{
    return plane.rowStrideBytes() * (static_cast<std::uint64_t>(plane.height()) - 1u) +
           static_cast<std::uint64_t>(plane.width()) * sizeof(float);
}

/// A sample's distance from the tip's centre.
[[nodiscard]] float tipDistance(BrushTipShape shape, float dx, float dy) noexcept
{
    if (shape == BrushTipShape::Square)
    {
        return (std::max)(std::abs(dx), std::abs(dy));
    }
    return std::sqrt((dx * dx) + (dy * dy));
}

/// The tip's coverage at a distance from its centre.
[[nodiscard]] float tipCoverage(float distance, float radius, float hardness) noexcept
{
    if (distance > radius)
    {
        return 0.0f;
    }
    if (hardness >= kHardTipHardness)
    {
        return 1.0f;
    }
    const float innerRadius = radius * hardness;
    if (distance <= innerRadius)
    {
        return 1.0f;
    }
    const float falloffRange = (std::max)(radius - innerRadius, kMinimumFalloffRange);
    const float t = coverageOf((distance - innerRadius) / falloffRange);
    const float edgeValue = std::exp(-kSoftFalloffExponent);
    const float gaussian = std::exp(-(kSoftFalloffExponent * t * t));
    return coverageOf((gaussian - edgeValue) / (1.0f - edgeValue));
}

/// A segment's estimated length: the chord, and a quarter of each neighbouring chord.
[[nodiscard]] float chordLength(const BrushPoint& from, const BrushPoint& to) noexcept
{
    return std::hypot(static_cast<float>(to.x - from.x), static_cast<float>(to.y - from.y));
}

/// One coordinate of a uniform Catmull-Rom curve at a parameter.
[[nodiscard]] float catmullRom(double before, double start, double end, double after, float t) noexcept
{
    const float p0 = static_cast<float>(before);
    const float p1 = static_cast<float>(start);
    const float p2 = static_cast<float>(end);
    const float p3 = static_cast<float>(after);
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (p2 - p0) * t + ((2.0f * p0) - (5.0f * p1) + (4.0f * p2) - p3) * t2 +
                   (-p0 + (3.0f * p1) - (3.0f * p2) + p3) * t3);
}

/// Whether every coordinate of a segment is a number within the bound.
[[nodiscard]] bool segmentInBounds(const BrushSegment& segment) noexcept
{
    for (const BrushPoint& point : {segment.before, segment.start, segment.end, segment.after})
    {
        if (!(std::abs(point.x) <= kMaxBrushCoordinate) || !(std::abs(point.y) <= kMaxBrushCoordinate))
        {
            return false;
        }
    }
    return true;
}

/// How many dabs a segment places, or zero for a degenerate one, which places one on its end.
[[nodiscard]] Result<std::uint32_t> segmentSteps(const BrushPlan& plan, const BrushSegment& segment,
                                                 const char* operation)
{
    if (!segmentInBounds(segment))
    {
        return drawingFailure(ErrorCode::InvalidArgument,
                              "a stroke point's coordinate is not a number within the brush's coordinate bound",
                              operation, "segment");
    }
    const float estimatedLength = chordLength(segment.start, segment.end) +
                                  (0.25f * chordLength(segment.before, segment.start)) +
                                  (0.25f * chordLength(segment.end, segment.after));
    if (estimatedLength <= kDegenerateSegmentLength)
    {
        return std::uint32_t{0u};
    }
    const float steps = std::ceil(estimatedLength / plan.spacingPixels());
    if (!(steps <= static_cast<float>(kMaxBrushSegmentDabs)))
    {
        return Status::failure(
            ErrorCode::OutOfRange,
            StatusDetail{.message = "a segment would place more dabs than one segment may",
                         .operation = operation,
                         .domain = ErrorDomain::Algorithms,
                         .context = {StatusContextField::ofCount(ContextKey::Limit, kMaxBrushSegmentDabs)}});
    }
    return static_cast<std::uint32_t>((std::max)(1, static_cast<int>(steps)));
}

/// The pixel a point rounds to, halves away from zero.
[[nodiscard]] BrushDab dabAt(double x, double y) noexcept
{
    return BrushDab{.x = static_cast<std::int32_t>(std::lround(x)), .y = static_cast<std::int32_t>(std::lround(y))};
}

/// A dab's tip clipped to a plane: the plane's columns and rows it reaches, and where the tip starts.
struct ClippedDab
{
    /// The tip's top-left pixel on the plane, which may lie outside it.
    std::int64_t left = 0;
    std::int64_t top = 0;

    /// The plane's columns and rows the tip reaches, half-open.
    std::int64_t columnBegin = 0;
    std::int64_t columnEnd = 0;
    std::int64_t rowBegin = 0;
    std::int64_t rowEnd = 0;

    /// Whether the tip reaches the plane at all.
    [[nodiscard]] bool reaches() const noexcept
    {
        return columnBegin < columnEnd && rowBegin < rowEnd;
    }
};

[[nodiscard]] ClippedDab clip(const BrushDab& dab, std::int64_t side, std::int64_t width, std::int64_t height) noexcept
{
    ClippedDab clipped;
    clipped.left = static_cast<std::int64_t>(dab.x) - side / 2;
    clipped.top = static_cast<std::int64_t>(dab.y) - side / 2;
    clipped.columnBegin = (std::max)(clipped.left, std::int64_t{0});
    clipped.columnEnd = (std::min)(clipped.left + side, width);
    clipped.rowBegin = (std::max)(clipped.top, std::int64_t{0});
    clipped.rowEnd = (std::min)(clipped.top + side, height);
    return clipped;
}

/// One block of working memory from the execution's engine context, returned on every exit path.
class WorkingBlock
{
public:
    WorkingBlock() noexcept = default;
    WorkingBlock(const WorkingBlock&) = delete;
    WorkingBlock& operator=(const WorkingBlock&) = delete;

    ~WorkingBlock()
    {
        if (m_pointer != nullptr)
        {
            m_memory->deallocate(m_pointer, m_bytes, kDefaultBufferAlignmentBytes);
        }
    }

    /// Takes `bytes` aligned for the widest vector from the execution's memory resource.
    [[nodiscard]] Status take(const ExecutionContext& execution, std::uint64_t bytes)
    {
        MemoryResource* memory = execution.memoryResource();
        if (memory == nullptr)
        {
            return drawingFailure(ErrorCode::NotInitialized, "the execution has no memory resource", kFeatherOperation,
                                  "execution");
        }
        Result<void*> allocation = memory->allocate(bytes, kDefaultBufferAlignmentBytes);
        if (!allocation.hasValue())
        {
            return allocation.status();
        }
        m_memory = memory;
        m_pointer = *allocation;
        m_bytes = bytes;
        return Status::success();
    }

    /// The block's first byte.
    [[nodiscard]] void* data() const noexcept
    {
        return m_pointer;
    }

private:
    MemoryResource* m_memory = nullptr;
    void* m_pointer = nullptr;
    std::uint64_t m_bytes = 0u;
};

/// Checks an execution and selects its backend, the steps every executor here starts with.
[[nodiscard]] Status startExecution(const ExecutionContext& execution, const char* operation)
{
    if (!execution.valid())
    {
        return drawingFailure(ErrorCode::NotInitialized, "a drawing operation was given an empty execution context",
                              operation, "execution");
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    const Result<BackendSelection> backend = selectBackend(execution);
    return backend.hasValue() ? Status::success() : backend.status();
}

} // namespace

Result<BrushPlan> planBrush(const BrushRequest& request)
{
    if (!(request.diameter >= 1.0f && request.diameter <= kMaxBrushDiameter))
    {
        return drawingFailure(ErrorCode::InvalidArgument, "a brush's diameter is a number from one pixel to the limit",
                              kPlanBrushOperation, "diameter");
    }
    if (!unitInterval(request.hardness))
    {
        return drawingFailure(ErrorCode::InvalidArgument, "a brush's hardness is a number from zero to one",
                              kPlanBrushOperation, "hardness");
    }
    if (!isBrushTipShape(request.shape))
    {
        return drawingFailure(ErrorCode::InvalidArgument, "the brush tip shape is outside its enumeration",
                              kPlanBrushOperation, "shape");
    }
    if (!(request.spacing >= kMinimumBrushSpacing && request.spacing <= 1.0f))
    {
        return drawingFailure(ErrorCode::InvalidArgument,
                              "a brush's spacing is a fraction of its diameter from one hundredth to one",
                              kPlanBrushOperation, "spacing");
    }
    if (!unitInterval(request.flow))
    {
        return drawingFailure(ErrorCode::InvalidArgument, "a brush's flow is a number from zero to one",
                              kPlanBrushOperation, "flow");
    }
    const std::uint32_t requested = (std::max)(1u, static_cast<std::uint32_t>(std::ceil(request.diameter)));
    const std::uint32_t side = requested % 2u == 0u ? requested + 1u : requested;
    const float spacingPixels = (std::max)(1.0f, request.diameter * request.spacing);
    return BrushPlan(request, side, spacingPixels);
}

Status writeBrushTip(const BrushPlan& plan, PlaneView<float> tip)
{
    const std::uint32_t side = plan.tipSide();
    if (tip.width() != side || tip.height() != side || tip.samplesPerPixel() != 1u)
    {
        return drawingFailure(ErrorCode::IncompatibleArguments,
                              "a brush tip is written to a plane of the plan's side, one sample per pixel",
                              kTipOperation, "tip");
    }
    const BrushRequest& request = plan.request();
    const float radius = (std::max)(request.diameter * 0.5f, 0.5f);
    const float centre = static_cast<float>(side - 1u) * 0.5f;
    for (std::uint32_t y = 0u; y < side; ++y)
    {
        float* const row = tip.sampleAt(0u, y);
        const float dy = static_cast<float>(y) - centre;
        for (std::uint32_t x = 0u; x < side; ++x)
        {
            const float dx = static_cast<float>(x) - centre;
            row[x] = tipCoverage(tipDistance(request.shape, dx, dy), radius, request.hardness);
        }
    }
    return Status::success();
}

Result<std::uint32_t> countBrushSegmentDabs(const BrushPlan& plan, const BrushSegment& segment)
{
    const Result<std::uint32_t> steps = segmentSteps(plan, segment, kCountOperation);
    if (!steps.hasValue())
    {
        return steps.status();
    }
    return (std::max)(*steps, 1u);
}

Result<std::uint32_t> placeBrushSegmentDabs(const BrushPlan& plan, const BrushSegment& segment,
                                            std::span<BrushDab> dabs)
{
    const Result<std::uint32_t> steps = segmentSteps(plan, segment, kPlaceOperation);
    if (!steps.hasValue())
    {
        return steps.status();
    }
    const std::uint32_t count = (std::max)(*steps, 1u);
    if (dabs.size() < count)
    {
        return drawingFailure(ErrorCode::IncompatibleArguments, "the dab span is shorter than the segment's dabs",
                              kPlaceOperation, "dabs");
    }
    if (*steps == 0u)
    {
        dabs[0] = dabAt(segment.end.x, segment.end.y);
        return std::uint32_t{1u};
    }
    for (std::uint32_t i = 1u; i <= count; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(count);
        const float x = catmullRom(segment.before.x, segment.start.x, segment.end.x, segment.after.x, t);
        const float y = catmullRom(segment.before.y, segment.start.y, segment.end.y, segment.after.y, t);
        dabs[i - 1u] = dabAt(static_cast<double>(x), static_cast<double>(y));
    }
    return count;
}

Result<BrushStampResult> stampBrushDabs(const ExecutionContext& execution, const BrushPlan& plan,
                                        PlaneView<const float> tip, std::span<const BrushDab> dabs, float pressure,
                                        PlaneView<float> stroke)
{
    const Status started = startExecution(execution, kStampOperation);
    if (started.failed())
    {
        return started;
    }
    if (!unitInterval(pressure))
    {
        return drawingFailure(ErrorCode::InvalidArgument, "a stroke's pressure is a number from zero to one",
                              kStampOperation, "pressure");
    }
    const std::uint32_t side = plan.tipSide();
    if (tip.width() != side || tip.height() != side || tip.samplesPerPixel() != 1u)
    {
        return drawingFailure(ErrorCode::IncompatibleArguments, "the tip is not the plan's side, one sample per pixel",
                              kStampOperation, "tip");
    }
    if (stroke.samplesPerPixel() != 1u)
    {
        return drawingFailure(ErrorCode::IncompatibleArguments, "a stroke plane holds one sample per pixel",
                              kStampOperation, "stroke");
    }
    if (regionsOverlap(tip.data(), spannedBytes(tip), stroke.data(), spannedBytes(stroke)))
    {
        return drawingFailure(ErrorCode::IncompatibleArguments, "the tip overlaps the stroke plane", kStampOperation,
                              "tip");
    }

    BrushStampResult result;
    const float strength = plan.request().flow * pressure;
    if (!(strength > 0.0f))
    {
        return result;
    }
    const std::int64_t width = stroke.width();
    const std::int64_t height = stroke.height();
    const std::int64_t tipSide = side;
    std::int64_t left = width;
    std::int64_t top = height;
    std::int64_t right = 0;
    std::int64_t bottom = 0;
    for (const BrushDab& dab : dabs)
    {
        const ClippedDab clipped = clip(dab, tipSide, width, height);
        if (!clipped.reaches())
        {
            continue;
        }
        ++result.dabsStamped;
        left = (std::min)(left, clipped.columnBegin);
        top = (std::min)(top, clipped.rowBegin);
        right = (std::max)(right, clipped.columnEnd);
        bottom = (std::max)(bottom, clipped.rowEnd);
    }
    if (result.dabsStamped == 0u)
    {
        return result;
    }
    result.dirty = BrushRectangle{.x = static_cast<std::uint32_t>(left),
                                  .y = static_cast<std::uint32_t>(top),
                                  .width = static_cast<std::uint32_t>(right - left),
                                  .height = static_cast<std::uint32_t>(bottom - top)};

    // A row's work is every dab that reaches it, about the stamped samples spread over the dirty rows.
    const std::uint64_t rowSamples =
        (result.dabsStamped * static_cast<std::uint64_t>(tipSide) * static_cast<std::uint64_t>(tipSide)) /
            result.dirty.height +
        1u;
    const TaskCost cost{.bytesPerItem = rowSamples * 8u, .operationsPerItem = rowSamples * 6u + dabs.size()};
    std::atomic<std::uint64_t> covered{0u};
    const Status ran = parallelFor(execution, result.dirty.height, cost, [&](TaskRange range) -> Status {
        const std::int64_t rangeBegin = top + static_cast<std::int64_t>(range.begin);
        const std::int64_t rangeEnd = top + static_cast<std::int64_t>(range.end);
        std::uint64_t laid = 0u;
        for (const BrushDab& dab : dabs)
        {
            const ClippedDab clipped = clip(dab, tipSide, width, height);
            const std::int64_t rowBegin = (std::max)(clipped.rowBegin, rangeBegin);
            const std::int64_t rowEnd = (std::min)(clipped.rowEnd, rangeEnd);
            if (!clipped.reaches() || rowBegin >= rowEnd)
            {
                continue;
            }
            for (std::int64_t y = rowBegin; y < rowEnd; ++y)
            {
                const float* const tipRow = tip.sampleAt(0u, static_cast<std::uint32_t>(y - clipped.top));
                float* const strokeRow = stroke.sampleAt(0u, static_cast<std::uint32_t>(y));
                for (std::int64_t x = clipped.columnBegin; x < clipped.columnEnd; ++x)
                {
                    const float tipAlpha = coverageOf(tipRow[x - clipped.left]) * strength;
                    if (tipAlpha <= 0.0f)
                    {
                        continue;
                    }
                    const float current = coverageOf(strokeRow[x]);
                    strokeRow[x] = 1.0f - ((1.0f - current) * (1.0f - tipAlpha));
                    ++laid;
                }
            }
        }
        covered.fetch_add(laid, std::memory_order_relaxed);
        return Status::success();
    });
    if (ran.failed())
    {
        return ran;
    }
    result.samplesCovered = covered.load(std::memory_order_relaxed);
    return result;
}

Result<CoverageFeatherPlan> planCoverageFeather(const CoverageFeatherRequest& request)
{
    if (!(request.feather >= 0.0f && request.feather <= kMaxCoverageFeather))
    {
        return drawingFailure(ErrorCode::InvalidArgument, "a feather is a width from zero pixels to the limit",
                              kPlanFeatherOperation, "feather");
    }
    if (request.feather == 0.0f)
    {
        return CoverageFeatherPlan(request, 0.0, 0u, 0.0);
    }
    const double sigma = (std::max)(kMinimumFeatherSigma, static_cast<double>(request.feather) * 0.5);
    std::uint32_t passes =
        (std::max)(1u,
                   static_cast<std::uint32_t>(std::ceil((sigma * sigma) / (kLargestPassSigma * kLargestPassSigma))));
    for (;;)
    {
        const double passSigma = sigma / std::sqrt(static_cast<double>(passes));
        const Result<GaussianPlan> gaussian =
            planGaussianBlur(GaussianRequest{.sigmaX = passSigma, .sigmaY = passSigma});
        if (gaussian.hasValue())
        {
            return CoverageFeatherPlan(request, sigma, passes, passSigma);
        }
        if (gaussian.status().code() != ErrorCode::OutOfRange)
        {
            return gaussian.status();
        }
        ++passes;
    }
}

Result<CoverageFeatherResult> featherCoverage(const ExecutionContext& execution, const CoverageFeatherPlan& plan,
                                              PlaneView<const float> coverage, PlaneView<float> destination)
{
    const Status started = startExecution(execution, kFeatherOperation);
    if (started.failed())
    {
        return started;
    }
    if (coverage.samplesPerPixel() != 1u || destination.samplesPerPixel() != 1u ||
        coverage.width() != destination.width() || coverage.height() != destination.height())
    {
        return drawingFailure(ErrorCode::IncompatibleArguments,
                              "a feather reads and writes two planes of one size, one sample per pixel",
                              kFeatherOperation, "destination");
    }
    if (regionsOverlap(coverage.data(), spannedBytes(coverage), destination.data(), spannedBytes(destination)))
    {
        return drawingFailure(ErrorCode::IncompatibleArguments, "a feather's planes overlap", kFeatherOperation,
                              "destination");
    }
    const std::uint32_t width = destination.width();
    const std::uint32_t height = destination.height();
    const TaskCost rowCost{.bytesPerItem = static_cast<std::uint64_t>(width) * 8u,
                           .operationsPerItem = static_cast<std::uint64_t>(width) * 2u};

    // Whether the coverage already reads as itself, so a blur can take it as it is.
    std::atomic<bool> clean{true};
    const Status scanned = parallelFor(execution, height, rowCost, [&](TaskRange range) -> Status {
        for (std::uint64_t row = range.begin; row < range.end && clean.load(std::memory_order_relaxed); ++row)
        {
            // The row is copied out of the answer before the loop: a range bound to the answer's
            // contents would outlive the answer.
            const std::span<const float> samples = *coverage.row(static_cast<std::uint32_t>(row));
            for (const float sample : samples)
            {
                if (!unitInterval(sample))
                {
                    clean.store(false, std::memory_order_relaxed);
                    break;
                }
            }
        }
        return Status::success();
    });
    if (scanned.failed())
    {
        return scanned;
    }

    // Writes the coverage, read as coverage, into a plane.
    const auto readInto = [&](float* target, const ImageLayout& targetLayout) -> Status {
        const PlaneView<float> plane = PlaneView<float>::createChecked(target, targetLayout);
        return parallelFor(execution, height, rowCost, [&](TaskRange range) -> Status {
            for (std::uint64_t row = range.begin; row < range.end; ++row)
            {
                const std::span<const float> from = *coverage.row(static_cast<std::uint32_t>(row));
                const std::span<float> to = *plane.row(static_cast<std::uint32_t>(row));
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    to[x] = coverageOf(from[x]);
                }
            }
            return Status::success();
        });
    };

    CoverageFeatherResult result;
    result.samplesWritten = static_cast<std::uint64_t>(width) * height;
    const std::uint32_t passes = plan.passes();
    if (passes == 0u)
    {
        const Status copied = readInto(destination.data(), destination.layout());
        if (copied.failed())
        {
            return copied;
        }
        return result;
    }

    const Result<GaussianPlan> gaussian =
        planGaussianBlur(GaussianRequest{.sigmaX = plan.passSigma(), .sigmaY = plan.passSigma()});
    if (!gaussian.hasValue())
    {
        return gaussian.status();
    }

    // Every pass reads and writes tightly packed rows, so the blur takes one path - and gives one answer - whatever
    // the strides of the caller's planes. The last pass writes the destination when its rows are tight and a working
    // plane otherwise, which is then copied out; working backwards, the passes before it alternate with a second
    // plane. A coverage that cannot be read as it is goes first into whichever of the two the first pass reads.
    std::optional<ImageLayout> tightLayout;
    if (const std::optional<ImageExtent> extent = ImageExtent::create(width, height, 1u, defaultEngineLimits());
        extent.has_value())
    {
        tightLayout = ImageLayout::create(ImageLayoutRequest{.extent = *extent,
                                                             .elementSizeBytes = sizeof(float),
                                                             .samplesPerPixel = 1u,
                                                             .planeCount = 1u,
                                                             .rowAlignmentBytes = sizeof(float)},
                                          defaultEngineLimits());
    }
    if (!tightLayout.has_value())
    {
        return drawingFailure(ErrorCode::IncompatibleArguments, "the feather's working plane cannot be described",
                              kFeatherOperation, "destination");
    }
    const std::uint64_t tightRowBytes = static_cast<std::uint64_t>(width) * sizeof(float);
    const bool readAsIs = clean.load(std::memory_order_relaxed) && coverage.rowStrideBytes() == tightRowBytes;
    const bool tightDestination = destination.rowStrideBytes() == tightRowBytes;
    WorkingBlock lastBlock;
    WorkingBlock otherBlock;
    if (!tightDestination)
    {
        const Status taken = lastBlock.take(execution, tightLayout->totalBytes());
        if (taken.failed())
        {
            return taken;
        }
    }
    if (passes > 1u || !readAsIs)
    {
        const Status taken = otherBlock.take(execution, tightLayout->totalBytes());
        if (taken.failed())
        {
            return taken;
        }
    }
    float* const last = tightDestination ? destination.data() : static_cast<float*>(lastBlock.data());
    float* const other = static_cast<float*>(otherBlock.data());

    // Runs `count` passes of one plan from the coverage, the last landing in the last plane.
    const auto runPasses = [&](std::uint32_t count, const GaussianPlan& passPlan) -> Status {
        // Whether pass `index` (from zero) writes the last plane: when an even number of passes follow it.
        const auto writesLast = [count](std::uint32_t index) noexcept { return (count - 1u - index) % 2u == 0u; };
        const void* input = coverage.data();
        const ImageLayout* inputLayout = &coverage.layout();
        if (!readAsIs)
        {
            float* const first = writesLast(0u) ? other : last;
            const Status copied = readInto(first, *tightLayout);
            if (copied.failed())
            {
                return copied;
            }
            input = first;
            inputLayout = &*tightLayout;
        }
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            float* const output = writesLast(index) ? last : other;
            const Result<SpatialFilterResult> blurred =
                blurImageGaussian(execution, SampleType::Float32, input, *inputLayout, output, *tightLayout, passPlan);
            if (!blurred.hasValue())
            {
                return blurred.status();
            }
            input = output;
            inputLayout = &*tightLayout;
        }
        return Status::success();
    };

    // The plan's passes are of kernels the stencil family holds. Over a plane more than twice a wider kernel's radius
    // each way, the fewest passes of kernels the compiled symmetric path takes run instead - one kernel at every
    // feather whose kernel fits the plane - each reading and writing the plane once where the plan's passes would each
    // read and write it again; the plan's passes run where the engine refuses the wider kernel: an execution without
    // the compiled path, or without the memory for its working plane.
    std::uint32_t ran = passes;
    bool wideRan = false;
    if (passes > 1u)
    {
        const std::uint32_t half = (std::min)(width, height) / 2u;
        const std::uint32_t widest = (std::min)(kMaxGaussianRadius, half == 0u ? 0u : half - 1u);
        for (std::uint32_t count = 1u; count < passes && widest > kMaxStencilRadius; ++count)
        {
            const double share = plan.sigma() / std::sqrt(static_cast<double>(count));
            const Result<GaussianPlan> wide = planGaussianBlur(GaussianRequest{.sigmaX = share, .sigmaY = share});
            if (!wide.hasValue() || (std::max)(wide->horizontalRadius(), wide->verticalRadius()) > widest)
            {
                continue;
            }
            const Status tried = runPasses(count, *wide);
            if (tried.ok())
            {
                ran = count;
                wideRan = true;
            }
            else if (tried.code() != ErrorCode::OutOfRange && tried.code() != ErrorCode::MemoryBudgetExceeded &&
                     tried.code() != ErrorCode::AllocationFailed)
            {
                return tried;
            }
            break;
        }
    }
    if (!wideRan)
    {
        const Status narrowRan = runPasses(passes, *gaussian);
        if (narrowRan.failed())
        {
            return narrowRan;
        }
    }
    if (!tightDestination)
    {
        const Status copied = parallelFor(execution, height, rowCost, [&](TaskRange range) -> Status {
            for (std::uint64_t row = range.begin; row < range.end; ++row)
            {
                const std::span<float> to = *destination.row(static_cast<std::uint32_t>(row));
                std::memcpy(to.data(), last + row * width, tightRowBytes);
            }
            return Status::success();
        });
        if (copied.failed())
        {
            return copied;
        }
    }
    result.passes = ran;
    return result;
}

} // namespace an
