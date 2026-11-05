// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The numerical core of drawing a mask by hand: a brush tip, dabs placed along a stroke, the dabs
/// accumulated into a stroke plane, and a shape's edge feathered.
///
/// A mask is drawn on coverage planes - one Float32 sample per pixel in the unit interval. The tools
/// that draw it, the canvas, the undo history and the vector shapes belong to the application; what
/// belongs here is every number those tools compute over a plane, so that two tools cannot disagree
/// about what a brush of a given size is or where its dabs fall.
///
/// **The tip.** `planBrush` turns a diameter, a hardness and a shape into a square tip of
/// `BrushPlan::tipSide()` samples - the diameter rounded up to a whole number of pixels and then to an
/// odd one, so the tip has a centre sample. A sample's distance from the centre is Euclidean for a round
/// tip and the larger axis offset for a square one; within the hardness times the radius it is fully
/// covered, beyond the radius not at all, and between the two it falls off as
/// `(exp(-4 t^2) - exp(-4)) / (1 - exp(-4))` with `t` the distance's fraction of the way out. A hardness
/// of 0.999 or more is a hard tip with no fall-off. The tip peaks at one and is not normalized: a dab
/// covers, it does not distribute.
///
/// **Dabs along a stroke.** The application keeps a stroke's points; each new point closes a segment
/// between the previous point and itself, and the segment is a uniform Catmull-Rom curve through the
/// point before the segment, its two ends and the point after - which the application does not have
/// yet, so `makeBrushSegment` extrapolates it. `placeBrushSegmentDabs` places dabs at equal steps of the
/// curve parameter, as many as the segment's estimated length divided by the brush's spacing rounded up,
/// the last on the segment's end and none on its start, which the previous segment placed. A dab sits on
/// the pixel nearest its point: the tip is not resampled between pixels. A degenerate segment - a stroke's
/// first point, or a point repeated - places one dab on its end.
///
/// **Dabs into a stroke plane.** `stampBrushDabs` screens every dab into the stroke plane, the tip times
/// the brush's flow times the pressure: `stroke = 1 - (1 - stroke) * (1 - tip * strength)`. The stroke
/// plane only grows. What the stroke then does to the mask is `Algorithms/Pointwise/LayerBlend.h`'s,
/// once per pixel, against the mask as it was before the stroke.
///
/// **A feathered shape.** The application rasterizes a shape into a coverage plane; `featherCoverage`
/// blurs it with the Gaussian whose standard deviation is half the feather width - never below half a
/// pixel - using the engine's exact separable Gaussian. A width whose Gaussian is wider than the engine's
/// largest stencil is blurred in several passes whose variances add up to it.
///
/// **Numbers.** Every coverage is read as a number in the unit interval: below zero or not a number reads
/// as zero, above one as one. The tip, the dab positions and the stamp are Float32 arithmetic in a fixed
/// order, so the same brush and the same points give the same samples.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/PlaneView.h>

#include <cstdint>
#include <span>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// How a sample's distance from the tip's centre is measured.
enum class BrushTipShape : std::uint8_t
{
    /// The Euclidean distance: a disc.
    Round = 0u,

    /// The larger of the two axis offsets: a square.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Square = 1u
};

/// Whether a value is one of the tip shapes this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param shape the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isBrushTipShape(BrushTipShape shape) noexcept
{
    return static_cast<std::uint8_t>(shape) <= static_cast<std::uint8_t>(BrushTipShape::Square);
}

/// The widest brush a plan accepts, in pixels.
inline constexpr float kMaxBrushDiameter = 1024.0f;

/// The closest spacing a plan accepts, as a fraction of the diameter.
inline constexpr float kMinimumBrushSpacing = 0.01f;

/// The most dabs one segment may place. A segment needing more is refused rather than walked.
inline constexpr std::uint32_t kMaxBrushSegmentDabs = 1u << 20u;

/// The largest magnitude a stroke point's coordinate may have, in pixels: two to the twenty-fourth, the
/// last power of two at which a Float32 coordinate still resolves a whole pixel, and far enough inside a
/// 32-bit integer that a curve overshooting its points still lands on one.
inline constexpr double kMaxBrushCoordinate = 16777216.0;

/// Everything a brush is, stated once.
///
/// A plain aggregate whose defaults are a hard round brush twenty pixels wide, dabbed every quarter of
/// its diameter at full flow. Every field is checked and none is clamped for the caller.
struct BrushRequest
{
    /// The tip's width in pixels: finite, at least one and at most `kMaxBrushDiameter`.
    float diameter = 20.0f;

    /// The fraction of the radius the tip covers fully: finite and within the unit interval.
    float hardness = 1.0f;

    /// How distance from the centre is measured.
    BrushTipShape shape = BrushTipShape::Round;

    /// The distance between dabs, as a fraction of the diameter: finite, at least
    /// `kMinimumBrushSpacing` and at most one. A spacing below one pixel is one pixel.
    float spacing = 0.25f;

    /// How much of the tip one dab lays down: finite and within the unit interval.
    float flow = 1.0f;
};

/// A checked brush.
///
/// Built only by `planBrush`, so holding one means the request was accepted. Copyable; it holds the
/// request and the two numbers derived from it.
class BrushPlan
{
public:
    /// What was asked for.
    ///
    /// @return the checked request.
    [[nodiscard]] const BrushRequest& request() const noexcept
    {
        return m_request;
    }

    /// How many samples wide and high the tip is.
    ///
    /// @return the diameter rounded up to a whole number and then up to an odd one.
    [[nodiscard]] std::uint32_t tipSide() const noexcept
    {
        return m_tipSide;
    }

    /// The distance between dabs, in pixels.
    ///
    /// @return the diameter times the spacing, and never less than one.
    [[nodiscard]] float spacingPixels() const noexcept
    {
        return m_spacingPixels;
    }

private:
    /// Records a checked request and what it derives.
    ///
    /// @param request the request `planBrush` accepted.
    /// @param tipSide the tip's side in samples.
    /// @param spacingPixels the distance between dabs in pixels.
    BrushPlan(const BrushRequest& request, std::uint32_t tipSide, float spacingPixels) noexcept
        : m_request(request), m_tipSide(tipSide), m_spacingPixels(spacingPixels)
    {
    }

    /// The planner is the only constructor's caller.
    friend AFTERNIGHTENGINE_API Result<BrushPlan> planBrush(const BrushRequest& request);

    /// The checked request.
    BrushRequest m_request;

    /// The tip's side in samples.
    std::uint32_t m_tipSide = 1u;

    /// The distance between dabs in pixels.
    float m_spacingPixels = 1.0f;
};

/// Checks a request and makes the plan.
///
/// Thread-safe and allocation-free apart from a failure it describes.
///
/// @param request the diameter, hardness, shape, spacing and flow.
/// @return the plan; or `ErrorCode::InvalidArgument` for a field that is not a number in its range or a
///         shape outside its enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API Result<BrushPlan> planBrush(const BrushRequest& request);

/// Writes a brush's tip.
///
/// Row-major, the centre sample at the middle of the middle row, each sample the coverage at its own
/// integer offset from the centre. Every sample is written.
///
/// Thread-safe with respect to the plan; allocation-free apart from a failure it describes.
///
/// @param plan the brush.
/// @param tip where the tip goes: `BrushPlan::tipSide()` samples wide and high, one sample per pixel.
/// @return success; or `ErrorCode::IncompatibleArguments` for a plane of another size or with more than
///         one sample per pixel.
[[nodiscard]] AFTERNIGHTENGINE_API Status writeBrushTip(const BrushPlan& plan, PlaneView<float> tip);

/// A point of a stroke, in the plane's pixel coordinates: the pixel at column `x` and row `y` is
/// centred on the integers `x` and `y`.
struct BrushPoint
{
    /// The column coordinate.
    double x = 0.0;

    /// The row coordinate.
    double y = 0.0;
};

/// One segment of a stroke, as the four control points of a uniform Catmull-Rom curve.
///
/// The curve runs from `start` to `end`; `before` and `after` shape its tangents there.
struct BrushSegment
{
    /// The stroke point before the segment, or `start` for a stroke's first segment.
    BrushPoint before;

    /// Where the segment begins: the previous stroke point.
    BrushPoint start;

    /// Where the segment ends: the stroke point that closed it.
    BrushPoint end;

    /// The point after the segment, which a stroke being drawn does not have yet.
    BrushPoint after;
};

/// The segment a new stroke point closes.
///
/// The point after the segment is extrapolated from its two ends, `end + (end - start)`, so a segment
/// can be dabbed the moment its end arrives.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param before the stroke point before `start`, or `start` itself when `start` is the stroke's first.
/// @param start the previous stroke point.
/// @param end the new one.
/// @return the segment.
[[nodiscard]] constexpr BrushSegment makeBrushSegment(BrushPoint before, BrushPoint start, BrushPoint end) noexcept
{
    return BrushSegment{.before = before,
                        .start = start,
                        .end = end,
                        .after = BrushPoint{.x = end.x + (end.x - start.x), .y = end.y + (end.y - start.y)}};
}

/// Where one dab's tip centre lands: a whole pixel.
struct BrushDab
{
    /// The column.
    std::int32_t x = 0;

    /// The row.
    std::int32_t y = 0;
};

/// How many dabs a segment places.
///
/// The segment's length is estimated as the distance between its ends plus a quarter of the distances
/// to the points either side; an estimate of a thousandth of a pixel or less places one dab, and any
/// other places the estimate over `BrushPlan::spacingPixels()`, rounded up.
///
/// Thread-safe and allocation-free apart from a failure it describes.
///
/// @param plan the brush.
/// @param segment the segment.
/// @return the count, at least one; or `ErrorCode::InvalidArgument` for a coordinate that is not finite
///         or exceeds `kMaxBrushCoordinate` in magnitude; or `ErrorCode::OutOfRange` for a segment that
///         would place more than `kMaxBrushSegmentDabs`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint32_t> countBrushSegmentDabs(const BrushPlan& plan,
                                                                               const BrushSegment& segment);

/// Places a segment's dabs.
///
/// The `i`-th of `n` dabs is the curve at parameter `i / n`, for `i` from one to `n`, rounded to the
/// nearest pixel with halves away from zero; a degenerate segment places its one dab on its end.
///
/// Thread-safe and allocation-free apart from a failure it describes.
///
/// @param plan the brush.
/// @param segment the segment.
/// @param dabs where the dabs go, in stroke order; at least `countBrushSegmentDabs` of them.
/// @return how many were written; the failures of `countBrushSegmentDabs`, or
///         `ErrorCode::IncompatibleArguments` when `dabs` is too short.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint32_t>
placeBrushSegmentDabs(const BrushPlan& plan, const BrushSegment& segment, std::span<BrushDab> dabs);

/// A rectangle of pixels on a plane, which is empty when either side is zero.
struct BrushRectangle
{
    /// The left column.
    std::uint32_t x = 0u;

    /// The top row.
    std::uint32_t y = 0u;

    /// How many columns.
    std::uint32_t width = 0u;

    /// How many rows.
    std::uint32_t height = 0u;

    /// Whether the rectangle holds no pixel.
    ///
    /// @return true when either side is zero.
    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return width == 0u || height == 0u;
    }
};

/// What one stamp did.
struct BrushStampResult
{
    /// Dabs whose tip reached the plane.
    std::uint64_t dabsStamped = 0u;

    /// Samples a dab added coverage to, counted once per dab.
    std::uint64_t samplesCovered = 0u;

    /// The smallest rectangle holding every reaching dab's tip, clipped to the plane; empty when no dab
    /// reached it or the strength is zero.
    BrushRectangle dirty{};
};

/// Screens dabs of a brush's tip into a stroke plane.
///
/// Each dab's tip is laid with its centre sample on the dab's pixel and clipped to the plane, and every
/// sample it reaches becomes `1 - (1 - stroke) * (1 - tip * strength)`, with the strength the brush's
/// flow times the pressure; a tip sample whose share is zero leaves its pixel as it was. Every pixel takes
/// the dabs that reach it in the order they are given, so the plane is the same sample for sample
/// whatever the number of threads. The stroke plane's samples are read as coverage.
///
/// Thread-safe with respect to the tip, which is only read. Runs on the operation's admitted threads.
/// Allocation-free apart from a failure it describes. A stop may leave some rows with every dab and
/// others with none.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the brush.
/// @param tip the tip `writeBrushTip` wrote for the plan.
/// @param dabs the dabs, in stroke order.
/// @param pressure how hard the stroke was pressed: finite and within the unit interval; one for a
///        device that reports none.
/// @param stroke the stroke plane the dabs are added to.
/// @return what was done; `ErrorCode::NotInitialized` for an empty execution; `ErrorCode::InvalidArgument`
///         for a pressure that is not a number within the unit interval; `ErrorCode::IncompatibleArguments`
///         for a tip of another size than the plan's, a plane with more than one sample per pixel, or a
///         tip that overlaps the stroke plane; or the execution's cancellation or backend-selection
///         failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<BrushStampResult>
stampBrushDabs(const ExecutionContext& execution, const BrushPlan& plan, PlaneView<const float> tip,
               std::span<const BrushDab> dabs, float pressure, PlaneView<float> stroke);

/// The widest feather a plan accepts, in pixels.
inline constexpr float kMaxCoverageFeather = 256.0f;

/// The smallest standard deviation a feather blurs with, in pixels.
inline constexpr double kMinimumFeatherSigma = 0.5;

/// What a feather does, stated once.
struct CoverageFeatherRequest
{
    /// The feather width in pixels: finite, not negative and at most `kMaxCoverageFeather`. Zero is no
    /// feather; any other width blurs with a standard deviation of half of it, and at least
    /// `kMinimumFeatherSigma`.
    float feather = 0.0f;
};

/// A checked feather.
///
/// Built only by `planCoverageFeather`, so holding one means the request was accepted. Copyable.
class CoverageFeatherPlan
{
public:
    /// What was asked for.
    ///
    /// @return the checked request.
    [[nodiscard]] const CoverageFeatherRequest& request() const noexcept
    {
        return m_request;
    }

    /// The standard deviation of the whole blur.
    ///
    /// @return zero for no feather, and otherwise half the width and at least `kMinimumFeatherSigma`.
    [[nodiscard]] double sigma() const noexcept
    {
        return m_sigma;
    }

    /// How many Gaussian passes realize the blur.
    ///
    /// @return zero for no feather; otherwise the fewest passes of `passSigma()` whose truncated kernels
    ///         fit the engine's stencil limit.
    [[nodiscard]] std::uint32_t passes() const noexcept
    {
        return m_passes;
    }

    /// The standard deviation of each pass: `sigma()` over the square root of `passes()`, so the
    /// passes' variances add up to the whole blur's.
    ///
    /// @return the pass sigma, or zero for no feather.
    [[nodiscard]] double passSigma() const noexcept
    {
        return m_passSigma;
    }

private:
    /// Records a checked request and what it derives.
    ///
    /// @param request the request `planCoverageFeather` accepted.
    /// @param sigma the whole blur's standard deviation.
    /// @param passes how many passes.
    /// @param passSigma each pass's standard deviation.
    CoverageFeatherPlan(const CoverageFeatherRequest& request, double sigma, std::uint32_t passes,
                        double passSigma) noexcept
        : m_request(request), m_sigma(sigma), m_passes(passes), m_passSigma(passSigma)
    {
    }

    /// The planner is the only constructor's caller.
    friend AFTERNIGHTENGINE_API Result<CoverageFeatherPlan> planCoverageFeather(const CoverageFeatherRequest& request);

    /// The checked request.
    CoverageFeatherRequest m_request;

    /// The whole blur's standard deviation.
    double m_sigma = 0.0;

    /// How many passes.
    std::uint32_t m_passes = 0u;

    /// Each pass's standard deviation.
    double m_passSigma = 0.0;
};

/// Checks a request and makes the plan.
///
/// Thread-safe and allocation-free apart from a failure it describes.
///
/// @param request the feather width.
/// @return the plan; or `ErrorCode::InvalidArgument` for a width that is not a number from zero to
///         `kMaxCoverageFeather`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CoverageFeatherPlan>
planCoverageFeather(const CoverageFeatherRequest& request);

/// What one feather did.
struct CoverageFeatherResult
{
    /// Gaussian passes run: the plan's, or zero for no feather.
    std::uint32_t passes = 0u;

    /// Samples written to the destination by the last pass, which is every sample of the plane.
    std::uint64_t samplesWritten = 0u;
};

/// Feathers a coverage plane.
///
/// The coverage is read as a number in the unit interval and blurred with the plan's Gaussian, the
/// border reflected about the edge sample without repeating it; no feather writes the coverage as read.
/// Each pass's kernel is the Gaussian density sampled at whole pixels out to four standard deviations
/// and renormalized, so a uniform coverage stays uniform and every written sample is within the unit
/// interval up to the rounding of a weighted sum. The two planes have one width and one height and one
/// sample per pixel, and may not overlap.
///
/// Every pass reads and writes tightly packed rows, so the answer depends on neither plane's row
/// stride nor the number of threads. A blur therefore takes working memory from the execution: one
/// plane when it runs more than one pass or when the coverage is padded or holds a sample outside the
/// unit interval, and one more when the destination is padded.
///
/// Thread-safe with respect to the coverage, which is only read. Runs on the operation's admitted
/// threads. A stop may leave the destination partly written.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the feather.
/// @param coverage the shape's coverage.
/// @param destination where the feathered coverage goes.
/// @return what was done; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::IncompatibleArguments` for planes of different sizes or with more than one sample
///         per pixel, or planes that overlap; the working memory's refusal; or the execution's
///         cancellation or backend-selection failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CoverageFeatherResult> featherCoverage(const ExecutionContext& execution,
                                                                                 const CoverageFeatherPlan& plan,
                                                                                 PlaneView<const float> coverage,
                                                                                 PlaneView<float> destination);

} // namespace an
