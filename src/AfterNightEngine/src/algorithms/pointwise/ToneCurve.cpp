// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - power, gamma, curve and sampled lookup planning.
//
// Every builder here plans and does not execute: it validates the shape and the request, evaluates
// whatever analytic function it needs once, in double, and hands the result to the pointwise
// builder as a power step, an exact code table or a sampled curve. A curve is joined by cubic
// Hermite segments whose end derivatives come from the chosen rule: the monotone rule below, or the
// natural-cubic, Akima and Steffen rules in `core/SplineTangents.h`. The real-time preview gamma is
// the one plan that leaves this file opaque, and its executor is the private door in
// `PointwiseExecutionInternal.h`.

#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/pointwise/PointwiseExecutionInternal.h"
#include "core/SplineTangents.h"
#include "kernels/reference/AlgorithmKernels.h"
#include "kernels/reference/CurveTableKernels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::buildToneProgram";

[[nodiscard]] Status toneFailure(ErrorCode code, const char* message)
{
    return Status::failure(
        code, StatusDetail{.message = message, .operation = kOperation, .domain = ErrorDomain::Algorithms});
}

[[nodiscard]] bool isToneDomain(NumericDomain domain) noexcept
{
    return domain == NumericDomain::NormalizedLinear || domain == NumericDomain::EncodedNonlinear;
}

[[nodiscard]] std::uint32_t exactEntriesFor(SampleType sampleType) noexcept
{
    switch (sampleType)
    {
    case SampleType::UInt8:
        return 256u;
    case SampleType::UInt16:
        return 65536u;
    case SampleType::Int8:
    case SampleType::Int16:
    case SampleType::UInt32:
    case SampleType::Int32:
    case SampleType::Float32:
    case SampleType::Float64:
        return 0u;
    }
    return 0u;
}

[[nodiscard]] Status validateShape(const TonePlanShape& shape)
{
    if (!isSampleType(shape.sampleType) || !isToneDomain(shape.inputDomain) || !isToneDomain(shape.outputDomain))
    {
        return toneFailure(ErrorCode::IncompatibleArguments,
                           "tone operations require a supported representation and normalized or encoded domains");
    }
    if (shape.sampleType != SampleType::Float32 && shape.sampleType != SampleType::Float64 &&
        exactEntriesFor(shape.sampleType) == 0u)
    {
        return toneFailure(ErrorCode::Unsupported,
                           "tone operations support floating working images and exact UInt8 or UInt16 tables");
    }
    return Status::success();
}

[[nodiscard]] PointwiseProgramBuilder configuredBuilder(const TonePlanShape& shape)
{
    PointwiseProgramBuilder builder;
    builder.channels(shape.channelCount)
        .sources(1u)
        .readingSamples(shape.sampleType)
        .readingDomain(shape.inputDomain)
        .writingDomain(shape.outputDomain)
        .promising(shape.outputContract)
        .computingIn(shape.precision, shape.precisionJustification);
    return builder;
}

struct CurveState
{
    std::vector<CurveControlPoint> points;

    /// Two per segment, the derivative at its left end then at its right.
    ///
    /// Per segment rather than per node because Akima's rule, as the GNU Scientific Library
    /// implements it, leaves a segment whose weight denominator vanished straight at its own
    /// secant, and the derivative that leaves at the shared node need not be the next segment's.
    std::vector<double> segmentTangents;
    CurveInterpolation interpolation = CurveInterpolation::Linear;
    CurveDomainPolicy domainPolicy = CurveDomainPolicy::Clamp;
};

[[nodiscard]] Status validateCurve(std::span<const CurveControlPoint> points, CurveInterpolation interpolation,
                                   CurveDomainPolicy domainPolicy)
{
    if (points.size() < 2u || !isCurveInterpolation(interpolation) || !isCurveDomainPolicy(domainPolicy))
    {
        return toneFailure(ErrorCode::InvalidArgument,
                           "a curve needs two points and known interpolation and domain policies");
    }
    if (points.size() < minimumCurvePoints(interpolation))
    {
        return toneFailure(ErrorCode::InvalidArgument,
                           "this curve interpolation reads further than its own segment and needs more control "
                           "points than the curve has");
    }
    for (std::size_t index = 0u; index < points.size(); ++index)
    {
        if (!std::isfinite(points[index].input) || !std::isfinite(points[index].output) || points[index].output < 0.0 ||
            points[index].output > 1.0 || (index != 0u && !(points[index - 1u].input < points[index].input)))
        {
            return toneFailure(ErrorCode::InvalidArgument,
                               "curve inputs must be finite and increasing and outputs must lie in [0, 1]");
        }
    }
    return Status::success();
}

// The monotone cubic's derivative at one control point. At an interior point it is the weighted
// harmonic mean of the two adjacent secants that Fritsch and Butland give for a monotone piecewise
// cubic, with the weights SciPy's `PchipInterpolator` uses, and zero where the secants differ in
// sign or either is flat, so an extremum lands on the control point. At either end it is the end
// segment's secant rather than a three-point estimate.
[[nodiscard]] double curveTangent(std::span<const CurveControlPoint> points, std::size_t index) noexcept
{
    const std::size_t previous = index == 0u ? 0u : index - 1u;
    const std::size_t next = index + 1u == points.size() ? points.size() - 2u : index;
    const double previousWidth = points[previous + 1u].input - points[previous].input;
    const double nextWidth = points[next + 1u].input - points[next].input;
    const double previousSecant = (points[previous + 1u].output - points[previous].output) / previousWidth;
    const double nextSecant = (points[next + 1u].output - points[next].output) / nextWidth;
    if (index == 0u || index + 1u == points.size())
    {
        return previousSecant;
    }
    if (previousSecant == 0.0 || nextSecant == 0.0 || std::signbit(previousSecant) != std::signbit(nextSecant))
    {
        return 0.0;
    }
    const double previousWeight = 2.0 * nextWidth + previousWidth;
    const double nextWeight = nextWidth + 2.0 * previousWidth;
    return (previousWeight + nextWeight) / (previousWeight / previousSecant + nextWeight / nextSecant);
}

/// One table entry, held inside the normalized interval the output domain promises.
///
/// Every control point is inside it, but a rule that is not shape-preserving can leave it between
/// two of them, and a program that wrote such a value would break the promise its output domain
/// carries. The distance it is moved is reported rather than absorbed.
///
/// @param value the analytic curve's value.
/// @param clampedBy raised to the largest amount any entry has been moved.
/// @return the value, held inside [0, 1].
[[nodiscard]] double normalizedCurveEntry(const double value, double& clampedBy) noexcept
{
    const double held = (std::min)((std::max)(value, 0.0), 1.0);
    clampedBy = (std::max)(clampedBy, std::abs(held - value));
    return held;
}

[[nodiscard]] Result<CurveState> prepareCurve(std::span<const CurveControlPoint> points,
                                              CurveInterpolation interpolation, CurveDomainPolicy domainPolicy)
{
    const Status valid = validateCurve(points, interpolation, domainPolicy);
    if (valid.failed())
    {
        return valid;
    }

    CurveState state;
    state.points.assign(points.begin(), points.end());
    state.interpolation = interpolation;
    state.domainPolicy = domainPolicy;
    if (interpolation == CurveInterpolation::Linear)
    {
        return state;
    }

    const std::size_t count = points.size();
    const std::size_t segments = count - 1u;
    state.segmentTangents.resize(2u * segments, 0.0);

    if (interpolation == CurveInterpolation::MonotoneCubic)
    {
        for (std::size_t segment = 0u; segment < segments; ++segment)
        {
            state.segmentTangents[2u * segment] = curveTangent(points, segment);
            state.segmentTangents[2u * segment + 1u] = curveTangent(points, segment + 1u);
        }
        return state;
    }

    std::vector<double> inputs(count, 0.0);
    std::vector<double> outputs(count, 0.0);
    for (std::size_t index = 0u; index < count; ++index)
    {
        inputs[index] = points[index].input;
        outputs[index] = points[index].output;
    }

    if (interpolation == CurveInterpolation::Akima)
    {
        std::vector<double> slopes(akimaSlopeDoubles(count), 0.0);
        akimaFillExtendedSlopes(inputs, outputs, slopes);
        for (std::size_t segment = 0u; segment < segments; ++segment)
        {
            const SegmentTangents tangents = akimaSegmentTangents(slopes, segment);
            state.segmentTangents[2u * segment] = tangents.left;
            state.segmentTangents[2u * segment + 1u] = tangents.right;
        }
        return state;
    }

    std::vector<double> nodeTangents(count, 0.0);
    if (interpolation == CurveInterpolation::Steffen)
    {
        steffenTangents(inputs, outputs, nodeTangents);
    }
    else
    {
        std::vector<double> scratch(naturalCubicScratchDoubles(count), 0.0);
        naturalCubicTangents(inputs, outputs, nodeTangents, scratch);
    }
    for (std::size_t segment = 0u; segment < segments; ++segment)
    {
        state.segmentTangents[2u * segment] = nodeTangents[segment];
        state.segmentTangents[2u * segment + 1u] = nodeTangents[segment + 1u];
    }
    return state;
}

// One validated curve at one input. A not-a-number stays one; an input outside the control points is
// refused, clamped to the end value, or extended along the end segment's secant, as the policy
// says; an input inside is found by binary search and evaluated on its segment, linearly or through
// the cubic Hermite basis with the two derivatives `tangentsOf` supplies for that segment.
template <typename TangentsOf>
[[nodiscard]] Result<double> evaluateValidated(std::span<const CurveControlPoint> points,
                                               CurveInterpolation interpolation, CurveDomainPolicy domainPolicy,
                                               double input, TangentsOf tangentsOf)
{
    if (std::isnan(input))
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const CurveControlPoint& first = points.front();
    const CurveControlPoint& last = points.back();
    if (input < first.input || input > last.input)
    {
        if (domainPolicy == CurveDomainPolicy::Reject)
        {
            return toneFailure(ErrorCode::OutOfRange, "a curve input lies outside its declared domain");
        }
        if (domainPolicy == CurveDomainPolicy::Clamp)
        {
            return input < first.input ? first.output : last.output;
        }
        const CurveControlPoint& edge = input < first.input ? first : last;
        const CurveControlPoint& adjacent = input < first.input ? points[1u] : points[points.size() - 2u];
        const double slope = (edge.output - adjacent.output) / (edge.input - adjacent.input);
        return edge.output + (input - edge.input) * slope;
    }

    std::size_t lower = 0u;
    if (input <= first.input)
    {
        lower = 0u;
    }
    else if (input >= last.input)
    {
        lower = points.size() - 2u;
    }
    else
    {
        const auto upper =
            std::upper_bound(points.begin(), points.end(), input,
                             [](double value, const CurveControlPoint& point) { return value < point.input; });
        lower = static_cast<std::size_t>(upper - points.begin() - 1);
    }
    const CurveControlPoint& left = points[lower];
    const CurveControlPoint& right = points[lower + 1u];
    const double width = right.input - left.input;
    const double position = (input - left.input) / width;
    if (interpolation == CurveInterpolation::Linear)
    {
        return left.output + (right.output - left.output) * position;
    }

    const double position2 = position * position;
    const double position3 = position2 * position;
    const double h00 = 2.0 * position3 - 3.0 * position2 + 1.0;
    const double h10 = position3 - 2.0 * position2 + position;
    const double h01 = -2.0 * position3 + 3.0 * position2;
    const double h11 = position3 - position2;
    const SegmentTangents tangents = tangentsOf(lower);
    return h00 * left.output + h10 * width * tangents.left + h01 * right.output + h11 * width * tangents.right;
}

[[nodiscard]] Result<double> evaluatePrepared(const CurveState& curve, double input)
{
    return evaluateValidated(curve.points, curve.interpolation, curve.domainPolicy, input,
                             [&curve](std::size_t segment) {
                                 return SegmentTangents{curve.segmentTangents[2u * segment],
                                                        curve.segmentTangents[2u * segment + 1u], false};
                             });
}

[[nodiscard]] PointwiseCurveDomainPolicy toPointwisePolicy(CurveDomainPolicy policy) noexcept
{
    switch (policy)
    {
    case CurveDomainPolicy::Clamp:
        return PointwiseCurveDomainPolicy::Clamp;
    case CurveDomainPolicy::Reject:
        return PointwiseCurveDomainPolicy::Reject;
    case CurveDomainPolicy::LinearExtrapolation:
        return PointwiseCurveDomainPolicy::LinearExtrapolation;
    }
    return PointwiseCurveDomainPolicy::Clamp;
}

[[nodiscard]] double sampledValue(std::span<const double> entries, double low, double high,
                                  PointwiseCurveSampling sampling, double input) noexcept
{
    const double position = (input - low) / (high - low) * static_cast<double>(entries.size() - 1u);
    if (sampling == PointwiseCurveSampling::Nearest)
    {
        const std::size_t index = static_cast<std::size_t>(
            (std::min)((std::max)(std::floor(position + 0.5), 0.0), static_cast<double>(entries.size() - 1u)));
        return entries[index];
    }
    if (sampling == PointwiseCurveSampling::Lower)
    {
        const double bounded = (std::min)((std::max)(position, 0.0), static_cast<double>(entries.size() - 1u));
        return entries[static_cast<std::size_t>(std::floor(bounded))];
    }
    const double bounded = (std::min)((std::max)(position, 0.0), static_cast<double>(entries.size() - 1u));
    const std::size_t lower = (std::min)(static_cast<std::size_t>(std::floor(bounded)), entries.size() - 2u);
    const double fraction = bounded - static_cast<double>(lower);
    return entries[lower] + (entries[lower + 1u] - entries[lower]) * fraction;
}

// A validated table of normalized outputs as a one-step program. Over UInt8 or UInt16 storage it
// becomes an exact lookup whose entries are scaled to code coordinates, so the executor narrows
// once without a second full-scale conversion; over floating storage it becomes a sampled curve
// that keeps its interval, sampling and domain policy.
[[nodiscard]] Result<PointwiseProgram> lowerTableProgram(const TonePlanShape& shape, std::span<const double> entries,
                                                         double inputLow, double inputHigh,
                                                         PointwiseCurveSampling sampling,
                                                         CurveDomainPolicy domainPolicy)
{
    const Status valid = validateShape(shape);
    if (valid.failed())
    {
        return valid;
    }
    if (entries.size() < 2u || entries.size() > kMaxMathTableEntries || !std::isfinite(inputLow) ||
        !std::isfinite(inputHigh) || !(inputLow < inputHigh) || !isPointwiseCurveSampling(sampling) ||
        !isCurveDomainPolicy(domainPolicy))
    {
        return toneFailure(ErrorCode::InvalidArgument, "a lookup table and its domain policies are malformed");
    }
    if (sampling == PointwiseCurveSampling::Lower && domainPolicy != CurveDomainPolicy::Clamp)
    {
        return toneFailure(ErrorCode::IncompatibleArguments,
                           "a lower-entry preview lookup clamps outside its table; it has no segment to extrapolate");
    }
    for (const double entry : entries)
    {
        if (!std::isfinite(entry) || entry < 0.0 || entry > 1.0)
        {
            return toneFailure(ErrorCode::InvalidArgument,
                               "a tone lookup table must contain finite normalized output values");
        }
    }

    PointwiseProgramBuilder builder = configuredBuilder(shape);
    const std::uint32_t exactEntries = exactEntriesFor(shape.sampleType);
    if (exactEntries != 0u)
    {
        if (entries.size() != exactEntries || inputLow != 0.0 || inputHigh != 1.0 ||
            sampling != PointwiseCurveSampling::Linear)
        {
            return toneFailure(ErrorCode::IncompatibleArguments,
                               "an integer tone lookup needs one entry per code over the normalized [0, 1] domain");
        }
        const double codeMaximum = static_cast<double>(exactEntries - 1u);
        std::vector<double> codeEntries(entries.size(), 0.0);
        for (std::size_t index = 0u; index < entries.size(); ++index)
        {
            codeEntries[index] = entries[index] * codeMaximum;
        }
        const std::uint32_t table = builder.addTable(codeEntries);
        builder.append(pointwiseLut(table));
    }
    else
    {
        const std::uint32_t table = builder.addTable(entries);
        builder.append(pointwiseSampledCurve(table, inputLow, inputHigh, sampling, toPointwisePolicy(domainPolicy)));
    }
    Result<PointwiseProgram> program = builder.build();
    if (!program.hasValue())
    {
        return program.status();
    }
    return program;
}

[[nodiscard]] Result<CurveProgramPlan> lowerTable(const TonePlanShape& shape, std::span<const double> entries,
                                                  double inputLow, double inputHigh, PointwiseCurveSampling sampling,
                                                  CurveDomainPolicy domainPolicy, CurveApproximation approximation,
                                                  double maximumError)
{
    Result<PointwiseProgram> program = lowerTableProgram(shape, entries, inputLow, inputHigh, sampling, domainPolicy);
    if (!program.hasValue())
    {
        return program.status();
    }
    return CurveProgramPlan(std::move(*program), approximation, static_cast<std::uint32_t>(entries.size()),
                            maximumError);
}

/// A curve's table, made by the planner and held inside [0, 1] - an integer table's entries already scaled to code
/// coordinates where the planner made them - lowered without copying it: the program `lowerTable` makes from the
/// same entries, with the table taken over rather than copied and checked again, because a planner's table is half a
/// mebibyte and the planner made every entry inside the interval.
[[nodiscard]] Result<CurveProgramPlan> lowerCurveTable(const TonePlanShape& shape, std::vector<double>&& entries,
                                                       double inputLow, double inputHigh,
                                                       PointwiseCurveSampling sampling, CurveDomainPolicy domainPolicy,
                                                       CurveApproximation approximation, double maximumError)
{
    const std::uint32_t count = static_cast<std::uint32_t>(entries.size());
    PointwiseProgramBuilder builder = configuredBuilder(shape);
    const std::uint32_t exactEntries = exactEntriesFor(shape.sampleType);
    if (exactEntries != 0u)
    {
        // An integer table's entries arrive as code coordinates, as `lowerTableProgram` scales them.
        const std::uint32_t table = builder.addTable(std::move(entries));
        builder.append(pointwiseLut(table));
    }
    else
    {
        const std::uint32_t table = builder.addTable(std::move(entries));
        builder.append(pointwiseSampledCurve(table, inputLow, inputHigh, sampling, toPointwisePolicy(domainPolicy)));
    }
    Result<PointwiseProgram> program = builder.release();
    if (!program.hasValue())
    {
        return program.status();
    }
    return CurveProgramPlan(std::move(*program), approximation, count, maximumError);
}

/// The preview gamma table from the exact power: each entry the curve at its interval's start, rounded down to a
/// multiple of one over the largest code, and the largest distance of an entry from the curve.
///
/// The curve rises, so an entry - the curve at its interval's start, rounded down - lies below the whole interval
/// and is farthest from it at the interval's end, where the next entry evaluates the curve anyway. Probing each
/// interval at eighths finds the same largest distance with nine times the evaluations.
///
/// @param exponent the power.
/// @param entries written with the table.
/// @return the largest distance.
[[nodiscard]] double exactPreviewGammaTable(double exponent,
                                            std::array<double, kRealtimePreviewGammaTableEntries>& entries) noexcept
{
    const double maximumCode = static_cast<double>(kRealtimePreviewGammaTableEntries - 1u);
    double maximumError = 0.0;
    for (std::uint32_t index = 0u; index < kRealtimePreviewGammaTableEntries; ++index)
    {
        const double input = static_cast<double>(index) / maximumCode;
        const double curve = std::pow(input, exponent);
        entries[index] = std::floor(curve * maximumCode) / maximumCode;
        if (index != 0u)
        {
            maximumError = (std::max)(maximumError, std::abs(entries[index - 1u] - curve));
        }
    }
    return maximumError;
}

/// The preview gamma table through the host's vector power, writing the bits `exactPreviewGammaTable` writes.
///
/// Every entry the vector run decides is the one the exact power gives, and the exact power decides the rest - the
/// last entry always, whose curve is exactly the largest code. The largest distance is taken from the exact power at
/// every interval whose distance through the vector power lies within the slack of the largest or was left
/// undecided, which holds the interval the exact distances peak at.
///
/// @param run the host's vector run.
/// @param exponent the power, inside the vector power's stated range.
/// @param entries written with the table.
/// @return the largest distance.
[[nodiscard]] double vectorPreviewGammaTable(PreviewGammaTableRunFn run, double exponent,
                                             std::array<double, kRealtimePreviewGammaTableEntries>& entries) noexcept
{
    const double maximumCode = static_cast<double>(kRealtimePreviewGammaTableEntries - 1u);
    std::array<double, kRealtimePreviewGammaTableEntries> curve;
    std::array<double, kRealtimePreviewGammaTableEntries> distances;
    entries[0] = std::floor(std::pow(0.0, exponent) * maximumCode) / maximumCode;
    const double largest =
        run(exponent, maximumCode, kRealtimePreviewGammaTableEntries, curve.data(), entries.data(), distances.data());
    // A curve no larger than one stands within the slack of the exact one, so two distances through it stand within
    // twice the slack of their exact values; four times it holds every interval that could be the largest.
    const double candidate = largest - 4.0 * previewGammaCurveSlack(exponent);
    double maximumError = 0.0;
    for (std::uint32_t index = 1u; index < kRealtimePreviewGammaTableEntries; ++index)
    {
        if (std::isnan(entries[index]))
        {
            const double exact = std::pow(static_cast<double>(index) / maximumCode, exponent);
            entries[index] = std::floor(exact * maximumCode) / maximumCode;
        }
        if (std::isnan(distances[index]) || distances[index] >= candidate)
        {
            const double exact = std::pow(static_cast<double>(index) / maximumCode, exponent);
            maximumError = (std::max)(maximumError, std::abs(entries[index - 1u] - exact));
        }
    }
    return maximumError;
}

} // namespace

PowerProgramPlan::PowerProgramPlan(PointwiseProgram program, MathProvider provider) noexcept
    : m_program(std::move(program)), m_provider(provider)
{
}

const PointwiseProgram& PowerProgramPlan::program() const noexcept
{
    return m_program;
}

MathProvider PowerProgramPlan::provider() const noexcept
{
    return m_provider;
}

CurveProgramPlan::CurveProgramPlan(PointwiseProgram program, CurveApproximation approximation,
                                   std::uint32_t tableEntries, double maximumAbsoluteError) noexcept
    : m_program(std::move(program)), m_approximation(approximation), m_tableEntries(tableEntries),
      m_maximumAbsoluteError(maximumAbsoluteError)
{
}

const PointwiseProgram& CurveProgramPlan::program() const noexcept
{
    return m_program;
}

CurveApproximation CurveProgramPlan::approximation() const noexcept
{
    return m_approximation;
}

std::uint32_t CurveProgramPlan::tableEntries() const noexcept
{
    return m_tableEntries;
}

double CurveProgramPlan::maximumAbsoluteError() const noexcept
{
    return m_maximumAbsoluteError;
}

RealtimePreviewGammaProgramPlan::RealtimePreviewGammaProgramPlan(PointwiseProgram program, std::uint32_t tableEntries,
                                                                 double maximumAbsoluteError) noexcept
    : m_program(std::move(program)), m_tableEntries(tableEntries), m_maximumAbsoluteError(maximumAbsoluteError)
{
}

std::uint32_t RealtimePreviewGammaProgramPlan::tableEntries() const noexcept
{
    return m_tableEntries;
}

double RealtimePreviewGammaProgramPlan::maximumAbsoluteError() const noexcept
{
    return m_maximumAbsoluteError;
}

Result<PowerProgramPlan> buildPowerProgram(const TonePlanShape& shape, double exponent)
{
    const Status valid = validateShape(shape);
    if (valid.failed())
    {
        return valid;
    }
    if (!std::isfinite(exponent))
    {
        return toneFailure(ErrorCode::InvalidArgument, "a power exponent must be finite");
    }

    PointwiseProgramBuilder builder = configuredBuilder(shape);
    MathProvider provider = MathProvider::ScalarLibrary;
    const std::uint32_t entryCount = exactEntriesFor(shape.sampleType);
    if (entryCount != 0u)
    {
        if (exponent < 0.0)
        {
            return toneFailure(ErrorCode::Unsupported,
                               "a negative power over integer storage has an infinite zero-code result; use a "
                               "floating working image so the non-finite result remains representable");
        }
        const double codeMaximum = static_cast<double>(entryCount - 1u);
        std::vector<double> entries(entryCount, 0.0);
        for (std::uint32_t code = 0u; code < entryCount; ++code)
        {
            const float input = static_cast<float>(static_cast<double>(code) / codeMaximum);
            const float output = referenceMathValue(MathFunction::Pow, input, exponent);
            entries[code] = static_cast<double>(output) * codeMaximum;
        }
        const std::uint32_t table = builder.addTable(entries);
        builder.append(pointwiseLut(table));
        provider = MathProvider::ExactTable;
    }
    else
    {
        builder.append(pointwiseGamma(exponent));
        provider = shape.sampleType == SampleType::Float32 && shape.precision == ComputePrecision::Float32
                       ? MathProvider::VectorApproximation
                       : MathProvider::ScalarLibrary;
    }
    Result<PointwiseProgram> program = builder.build();
    if (!program.hasValue())
    {
        return program.status();
    }
    return PowerProgramPlan(std::move(*program), provider);
}

Result<PowerProgramPlan> buildGammaProgram(const TonePlanShape& shape, double gamma, GammaDirection direction)
{
    if (!std::isfinite(gamma) || !(gamma > 0.0) || !isGammaDirection(direction))
    {
        return toneFailure(ErrorCode::InvalidArgument, "gamma must be positive and its direction must be known");
    }
    const bool domainsAgree = direction == GammaDirection::Encode
                                  ? shape.inputDomain == NumericDomain::NormalizedLinear &&
                                        shape.outputDomain == NumericDomain::EncodedNonlinear
                                  : shape.inputDomain == NumericDomain::EncodedNonlinear &&
                                        shape.outputDomain == NumericDomain::NormalizedLinear;
    if (!domainsAgree)
    {
        return toneFailure(ErrorCode::IncompatibleArguments,
                           "the stated input/output domains disagree with the gamma direction");
    }
    return buildPowerProgram(shape, direction == GammaDirection::Encode ? 1.0 / gamma : gamma);
}

Result<RealtimePreviewGammaProgramPlan> buildRealtimePreviewGammaProgram(const TonePlanShape& shape, double gamma,
                                                                         GammaDirection direction)
{
    if (!std::isfinite(gamma) || !(gamma > 0.0) || !isGammaDirection(direction))
    {
        return toneFailure(ErrorCode::InvalidArgument, "gamma must be positive and its direction must be known");
    }
    const bool domainsAgree = direction == GammaDirection::Encode
                                  ? shape.inputDomain == NumericDomain::NormalizedLinear &&
                                        shape.outputDomain == NumericDomain::EncodedNonlinear
                                  : shape.inputDomain == NumericDomain::EncodedNonlinear &&
                                        shape.outputDomain == NumericDomain::NormalizedLinear;
    if (!domainsAgree)
    {
        return toneFailure(ErrorCode::IncompatibleArguments,
                           "the stated input/output domains disagree with the gamma direction");
    }
    if (shape.sampleType != SampleType::Float32 && shape.sampleType != SampleType::Float64)
    {
        return toneFailure(ErrorCode::IncompatibleArguments,
                           "the coarse gamma lookup is only a real-time floating preview path; integer gamma uses "
                           "the production builder's exact resident table");
    }

    const double exponent = direction == GammaDirection::Encode ? 1.0 / gamma : gamma;
    std::array<double, kRealtimePreviewGammaTableEntries> entries{};
    // The table builds on the caller's thread, as every plan does; the host's vector power evaluates the curve where
    // its bound is stated, and the exact power decides every answer the bound leaves open.
    const PreviewGammaTableRunFn vectorRun =
        exponent >= kPreviewGammaVectorLowestExponent && exponent <= kPreviewGammaVectorHighestExponent
            ? previewGammaTableRunForHost()
            : nullptr;
    const double maximumError = vectorRun != nullptr ? vectorPreviewGammaTable(vectorRun, exponent, entries)
                                                     : exactPreviewGammaTable(exponent, entries);
    Result<PointwiseProgram> program =
        lowerTableProgram(shape, entries, 0.0, 1.0, PointwiseCurveSampling::Lower, CurveDomainPolicy::Clamp);
    if (!program.hasValue())
    {
        return program.status();
    }
    return RealtimePreviewGammaProgramPlan(std::move(*program), kRealtimePreviewGammaTableEntries, maximumError);
}

Result<PointwiseExecutionResult> executeRealtimePreviewGamma(const ExecutionContext& execution,
                                                             const RealtimePreviewGammaProgramPlan& plan,
                                                             PointwiseImageView source,
                                                             PointwiseMutableImageView destination,
                                                             const KernelValidityView& validity)
{
    const std::array<PointwiseImageView, 1u> sources{source};
    return detail::executePointwiseProgramImpl(execution, plan.m_program, sources, destination, validity, true);
}

Result<double> evaluateCurve(std::span<const CurveControlPoint> points, CurveInterpolation interpolation,
                             CurveDomainPolicy domainPolicy, double input)
{
    const Status valid = validateCurve(points, interpolation, domainPolicy);
    if (valid.failed())
    {
        return valid;
    }
    // The two local rules decide a segment's derivatives from its own neighbours, so one point costs
    // one arithmetic expression; the three that read the whole point list are prepared first, which
    // is what they cost wherever they are asked for.
    if (interpolation == CurveInterpolation::Linear || interpolation == CurveInterpolation::MonotoneCubic)
    {
        return evaluateValidated(points, interpolation, domainPolicy, input, [points](std::size_t segment) {
            return SegmentTangents{curveTangent(points, segment), curveTangent(points, segment + 1u), false};
        });
    }
    const Result<CurveState> curve = prepareCurve(points, interpolation, domainPolicy);
    if (!curve.hasValue())
    {
        return curve.status();
    }
    return evaluatePrepared(*curve, input);
}

namespace
{

/// A prepared curve laid out as the curve table's runs read it.
struct CurveTableLayout
{
    std::vector<double> inputs;
    std::vector<double> outputs;
    CurveTableShape shape;
};

[[nodiscard]] CurveTableLayout curveTableLayout(const CurveState& curve, std::uint32_t entryCount, bool codeInputs)
{
    CurveTableLayout layout;
    for (const CurveControlPoint& point : curve.points)
    {
        layout.inputs.push_back(point.input);
        layout.outputs.push_back(point.output);
    }
    layout.shape = CurveTableShape{
        .inputs = layout.inputs.data(),
        .outputs = layout.outputs.data(),
        .segmentTangents = curve.interpolation == CurveInterpolation::Linear ? nullptr : curve.segmentTangents.data(),
        .pointCount = static_cast<std::uint32_t>(curve.points.size()),
        .entryCount = entryCount,
        .codeInputs = codeInputs,
        .extrapolate = curve.domainPolicy == CurveDomainPolicy::LinearExtrapolation};
    return layout;
}

/// Fills entries `begin` to `end` of a curve table and raises `heldBy` to the largest distance one was held by.
///
/// The compiled run writes the bits the scalar loop writes. It is not handed a table an input of which lies
/// outside the points' range under a policy that refuses one: the scalar loop is what reports that refusal.
[[nodiscard]] Status fillCurveEntries(const CurveState& curve, const CurveTableShape& shape, const CurveTableRuns& runs,
                                      std::uint32_t begin, std::uint32_t end, double* entries, double& heldBy)
{
    const bool insideAlways =
        !shape.codeInputs || (shape.inputs[0] <= 0.0 && shape.inputs[shape.pointCount - 1u] >= 1.0);
    if (runs.entries != nullptr && (curve.domainPolicy != CurveDomainPolicy::Reject || insideAlways))
    {
        heldBy = (std::max)(heldBy, runs.entries(shape, begin, end, entries));
        return Status::success();
    }
    for (std::uint32_t index = begin; index < end; ++index)
    {
        const Result<double> value = evaluatePrepared(curve, curveTableEntryInput(shape, index));
        if (!value.hasValue())
        {
            return value.status();
        }
        entries[index] = normalizedCurveEntry(*value, heldBy);
    }
    return Status::success();
}

/// Raises `largest` to the largest distance of a sampled table from its curve at the probes of intervals `begin` to
/// `end`, the table read under `sampling`.
[[nodiscard]] Status measureCurveTable(const CurveState& curve, const CurveTableShape& shape,
                                       const CurveTableRuns& runs, std::span<const double> entries,
                                       PointwiseCurveSampling sampling, std::uint32_t begin, std::uint32_t end,
                                       double& largest)
{
    if (runs.probes != nullptr && sampling == PointwiseCurveSampling::Linear)
    {
        largest = (std::max)(largest, runs.probes(shape, entries.data(), begin, end));
        return Status::success();
    }
    const double low = shape.inputs[0];
    const double high = shape.inputs[shape.pointCount - 1u];
    for (std::uint32_t index = begin; index < end; ++index)
    {
        for (std::uint32_t probe = 1u; probe < kCurveTableProbesPerInterval; ++probe)
        {
            const double input = curveTableProbeInput(low, high, index, probe, shape.entryCount);
            const Result<double> expected = evaluatePrepared(curve, input);
            if (!expected.hasValue())
            {
                return expected.status();
            }
            largest = (std::max)(largest, std::abs(sampledValue(entries, low, high, sampling, input) - *expected));
        }
    }
    return Status::success();
}

/// The larger of two distances, the merge of the parallel table build.
[[nodiscard]] double largerDistance(double left, double right) noexcept
{
    return (std::max)(left, right);
}

/// Runs a table fill or measurement over `count` items: in one call when no execution is given, and otherwise as a
/// reduction of chunks on the execution's workers whose largest distances are merged, which is the same answer.
template <typename Step>
[[nodiscard]] Status overCurveTable(const ExecutionContext* execution, std::uint32_t count, const TaskCost& cost,
                                    double& largest, Step step)
{
    if (execution == nullptr)
    {
        return step(0u, count, largest);
    }
    const Result<double> reduced = parallelReduce(
        *execution, count, cost, 0.0,
        [&step](TaskRange range, double& partial) -> Status {
            return step(static_cast<std::uint32_t>(range.begin), static_cast<std::uint32_t>(range.end), partial);
        },
        &largerDistance);
    if (!reduced.hasValue())
    {
        return reduced.status();
    }
    largest = (std::max)(largest, *reduced);
    return Status::success();
}

/// How much wider than the computed interval width the bound takes an interval to be, relatively.
constexpr double kIntervalWidening = 0x1.0p-30;

/// The rounding a measured distance may carry, per unit of the magnitude of a segment's terms.
constexpr double kMeasuredRounding = 0x1.0p-40;

/// How many intervals beside a curvature cut are measured whatever their bound, against rounding in locating it.
constexpr std::uint32_t kCutMargin = 2u;

/// The mark of an interval of a table not chosen for measuring; one that keeps it to the end the bound leaves out.
constexpr std::uint8_t kUnmeasured = 0u;

/// The mark of an interval the bound cannot exclude before anything is measured.
constexpr std::uint8_t kMeasureFirst = 1u;

/// The mark of an interval the bound cannot exclude once those marked first are measured.
constexpr std::uint8_t kMeasureSecond = 2u;

/// Raises `largest` to the largest distance at the probes of the intervals from `begin` to `end` marked `mark`, the
/// compiled probes run measuring each run of consecutive marked intervals.
void measureMarkedIntervals(const CurveTableShape& shape, const CurveTableRuns& runs, std::span<const double> entries,
                            std::span<const std::uint8_t> marks, std::uint8_t mark, std::uint32_t begin,
                            std::uint32_t end, double& largest)
{
    std::uint32_t index = begin;
    while (index < end)
    {
        if (marks[index] != mark)
        {
            ++index;
            continue;
        }
        std::uint32_t runEnd = index + 1u;
        while (runEnd < end && marks[runEnd] == mark)
        {
            ++runEnd;
        }
        largest = (std::max)(largest, runs.probes(shape, entries.data(), index, runEnd));
        index = runEnd;
    }
}

/// The largest distance of a linearly sampled table from its curve at the probes, measuring only the intervals that
/// can hold it; the answer is the one measuring every probe gives, bit for bit.
///
/// Between two entries a table read linearly stands from a curve whose slope is continuous by at most an eighth of
/// the interval's width squared times the largest curvature on it, and on one segment of a cubic the curvature is a
/// straight line in the input, so its largest on an interval is at an end. An interval reading an entry the unit
/// interval holds - or one at either end of it - is measured whatever its bound, because there the entry is not the
/// curve; so is one holding a control point, because the slope may turn there. The intervals at both ends of each
/// segment, where its curvature peaks, are measured next. Every other interval whose bound - widened by far more
/// than rounding can move a measured distance - stays below the largest distance measured by then cannot hold the
/// largest, and is left out; the rest are measured. A straight curve's bound is its rounding, so all of its
/// intervals are measured.
[[nodiscard]] Status measureCurveTableBounded(const CurveTableShape& shape, const CurveTableRuns& runs,
                                              std::span<const double> entries, const ExecutionContext* execution,
                                              double& largest)
{
    const std::uint32_t entryCount = shape.entryCount;
    const std::uint32_t intervals = entryCount - 1u;
    const std::uint32_t points = shape.pointCount;
    const double low = shape.inputs[0];
    const double high = shape.inputs[points - 1u];
    const double step = (high - low) / static_cast<double>(intervals);
    const double widest = step * (1.0 + kIntervalWidening);
    const auto inputAt = [&](std::uint32_t index) { return curveTableInput(low, high, index, entryCount); };
    // The first index from `first` to `last` whose `test` holds, or `last`, for a test that holds from somewhere on.
    const auto firstWhere = [](std::uint32_t first, std::uint32_t last, const auto& test) {
        while (first < last)
        {
            const std::uint32_t middle = first + (last - first) / 2u;
            if (test(middle))
            {
                last = middle;
            }
            else
            {
                first = middle + 1u;
            }
        }
        return first;
    };

    std::vector<std::uint8_t> marks(intervals, kUnmeasured);
    for (std::uint32_t index = 0u; index < intervals; ++index)
    {
        const double left = entries[index];
        const double right = entries[index + 1u];
        if (left == 0.0 || left == 1.0 || right == 0.0 || right == 1.0)
        {
            marks[index] = kMeasureFirst;
        }
    }
    // Each segment's clean intervals - those inside it from end to end - and every interval no segment holds whole.
    struct CleanRange
    {
        std::uint32_t first = 0u;
        std::uint32_t last = 0u;
    };
    std::vector<CleanRange> clean(points - 1u);
    std::uint32_t covered = 0u;
    for (std::uint32_t segment = 0u; segment + 1u < points; ++segment)
    {
        const double start = shape.inputs[segment];
        const double finish = shape.inputs[segment + 1u];
        const std::uint32_t first =
            firstWhere(0u, intervals, [&](std::uint32_t index) { return inputAt(index) >= start; });
        const std::uint32_t last =
            firstWhere(first, intervals, [&](std::uint32_t index) { return inputAt(index + 1u) > finish; });
        clean[segment] = CleanRange{.first = first, .last = (std::max)(first, last)};
        for (std::uint32_t index = covered; index < first && index < intervals; ++index)
        {
            marks[index] = kMeasureFirst;
        }
        covered = (std::max)(covered, clean[segment].last);
        if (first < last)
        {
            marks[first] = kMeasureFirst;
            marks[last - 1u] = kMeasureFirst;
        }
    }
    for (std::uint32_t index = covered; index < intervals; ++index)
    {
        marks[index] = kMeasureFirst;
    }

    const TaskCost cost{.bytesPerItem = 2u * sizeof(double) + 1u, .operationsPerItem = 150u};
    double measured = 0.0;
    Status status = overCurveTable(
        execution, intervals, cost, measured, [&](std::uint32_t begin, std::uint32_t end, double& partial) {
            measureMarkedIntervals(shape, runs, entries, marks, kMeasureFirst, begin, end, partial);
            return Status::success();
        });
    if (status.failed())
    {
        return status;
    }

    // On each segment the curve is `y0 + m0 d + c d^2 + e d^3` in the distance `d` from its left point, so its
    // curvature `2c + 6e d` reaches a level only beyond two cuts in `d`.
    bool more = false;
    for (std::uint32_t segment = 0u; segment + 1u < points; ++segment)
    {
        const CleanRange range = clean[segment];
        if (range.first + 2u >= range.last)
        {
            continue;
        }
        const double start = shape.inputs[segment];
        const double width = shape.inputs[segment + 1u] - start;
        const double y0 = shape.outputs[segment];
        const double y1 = shape.outputs[segment + 1u];
        const double secant = (y1 - y0) / width;
        const bool straight = shape.segmentTangents == nullptr;
        const double m0 = straight ? secant : shape.segmentTangents[2u * segment];
        const double m1 = straight ? secant : shape.segmentTangents[2u * segment + 1u];
        const double c = straight ? 0.0 : (3.0 * secant - 2.0 * m0 - m1) / width;
        const double e = straight ? 0.0 : (m0 + m1 - 2.0 * secant) / (width * width);
        const double magnitude = 1.0 + std::abs(y0) + std::abs(y1) + width * (std::abs(m0) + std::abs(m1));
        const double slack = kMeasuredRounding * magnitude +
                             kIntervalWidening * step * (std::abs(m0) + std::abs(m1) + 6.0 * std::abs(secant));
        const double level = (measured - slack) * 8.0 / (widest * widest) / (1.0 + kIntervalWidening);
        // Every interval below `lowCut` and from `highCut` on is measured.
        std::uint32_t lowCut = range.first;
        std::uint32_t highCut = range.last;
        if (!(level > 0.0) || !std::isfinite(level) || !std::isfinite(c) || !std::isfinite(e))
        {
            lowCut = range.last;
        }
        else if (e == 0.0)
        {
            lowCut = std::abs(2.0 * c) >= level ? range.last : range.first;
        }
        else
        {
            const double firstCut = (level - 2.0 * c) / (6.0 * e);
            const double secondCut = (-level - 2.0 * c) / (6.0 * e);
            // An interval reaches the level when its left end lies at or below the lower cut or its right end at or
            // above the upper one; in intervals from the table's start, with margins against rounding.
            const auto intervalsTo = [&](double distance) { return (start + distance - low) / step; };
            const double below = intervalsTo((std::min)(firstCut, secondCut));
            const double above = intervalsTo((std::max)(firstCut, secondCut));
            if (!std::isfinite(below) || !std::isfinite(above))
            {
                lowCut = range.last;
            }
            else
            {
                const double lowest = static_cast<double>(range.first);
                const double highest = static_cast<double>(range.last);
                lowCut = static_cast<std::uint32_t>(
                    (std::min)((std::max)(std::floor(below) + 1.0 + kCutMargin, lowest), highest));
                highCut = static_cast<std::uint32_t>(
                    (std::min)((std::max)(std::ceil(above) - 1.0 - kCutMargin, lowest), highest));
            }
        }
        for (std::uint32_t index = range.first; index < range.last; ++index)
        {
            if ((index < lowCut || index >= highCut) && marks[index] == kUnmeasured)
            {
                marks[index] = kMeasureSecond;
                more = true;
            }
        }
    }
    if (more)
    {
        status = overCurveTable(
            execution, intervals, cost, measured, [&](std::uint32_t begin, std::uint32_t end, double& partial) {
                measureMarkedIntervals(shape, runs, entries, marks, kMeasureSecond, begin, end, partial);
                return Status::success();
            });
        if (status.failed())
        {
            return status;
        }
    }
    largest = (std::max)(largest, measured);
    return Status::success();
}

/// What both `buildCurveProgram` overloads build, through `runs`, on the execution's workers when one is given.
[[nodiscard]] Result<CurveProgramPlan> buildCurveProgramWith(const TonePlanShape& shape,
                                                             const CurveProgramRequest& request,
                                                             const CurveTableRuns& runs,
                                                             const ExecutionContext* execution)
{
    const Status valid = validateShape(shape);
    if (valid.failed())
    {
        return valid;
    }
    if (!isCurveApproximation(request.approximation))
    {
        return toneFailure(ErrorCode::InvalidArgument, "the curve approximation mode is not one this build knows");
    }
    const Result<CurveState> curve = prepareCurve(request.points, request.interpolation, request.domainPolicy);
    if (!curve.hasValue())
    {
        return curve.status();
    }

    const std::uint32_t exactEntries = exactEntriesFor(shape.sampleType);
    // An entry costs one evaluation of the curve, a probe of the sampled table three more and a read of the table.
    const TaskCost entryCost{.bytesPerItem = sizeof(double), .operationsPerItem = 40u};
    const TaskCost probeCost{.bytesPerItem = 2u * sizeof(double), .operationsPerItem = 150u};
    if (request.approximation == CurveApproximation::ExactAtResidentValues)
    {
        if (exactEntries == 0u)
        {
            return toneFailure(ErrorCode::IncompatibleArguments,
                               "an exact resident-value curve requires UInt8 or UInt16 input");
        }
        const CurveTableLayout layout = curveTableLayout(*curve, exactEntries, true);
        std::vector<double> entries(exactEntries, 0.0);
        double clamped = 0.0;
        // The program reads an integer table's entries as code coordinates, so each range is scaled where it was
        // just made rather than in a pass of its own over the whole table.
        const double codeMaximum = static_cast<double>(exactEntries - 1u);
        const Status filled = overCurveTable(
            execution, exactEntries, entryCost, clamped, [&](std::uint32_t begin, std::uint32_t end, double& heldBy) {
                Status made = fillCurveEntries(*curve, layout.shape, runs, begin, end, entries.data(), heldBy);
                if (!made.failed())
                {
                    for (std::uint32_t index = begin; index < end; ++index)
                    {
                        entries[index] *= codeMaximum;
                    }
                }
                return made;
            });
        if (filled.failed())
        {
            return filled;
        }
        return lowerCurveTable(shape, std::move(entries), 0.0, 1.0, PointwiseCurveSampling::Linear,
                               request.domainPolicy, request.approximation, clamped);
    }

    if (exactEntries != 0u)
    {
        return toneFailure(ErrorCode::IncompatibleArguments,
                           "integer curves use the exact resident-value mode rather than a sampled approximation");
    }
    if (request.tableEntries < 2u || request.tableEntries > kMaxMathTableEntries)
    {
        return toneFailure(ErrorCode::OutOfRange, "a sampled curve table holds from two through 65,536 entries");
    }
    const double low = curve->points.front().input;
    const double high = curve->points.back().input;
    const CurveTableLayout layout = curveTableLayout(*curve, request.tableEntries, false);
    std::vector<double> entries(request.tableEntries, 0.0);
    double clampedBy = 0.0;
    const Status filled =
        overCurveTable(execution, request.tableEntries, entryCost, clampedBy,
                       [&](std::uint32_t begin, std::uint32_t end, double& heldBy) {
                           return fillCurveEntries(*curve, layout.shape, runs, begin, end, entries.data(), heldBy);
                       });
    if (filled.failed())
    {
        return filled;
    }

    const PointwiseCurveSampling sampling = request.approximation == CurveApproximation::SampledNearest
                                                ? PointwiseCurveSampling::Nearest
                                                : PointwiseCurveSampling::Linear;
    // The clamp is a distance from the analytic curve too, and it lands on table entries rather than
    // between them, so the probes below would not see it.
    double maximumError = clampedBy;
    // The compiled probes measure only the intervals that can hold the largest distance; the scalar loop, which is
    // the reference the compiled table is held to, measures every probe.
    const Status measured = runs.probes != nullptr && sampling == PointwiseCurveSampling::Linear
                                ? measureCurveTableBounded(layout.shape, runs, entries, execution, maximumError)
                                : overCurveTable(execution, request.tableEntries - 1u, probeCost, maximumError,
                                                 [&](std::uint32_t begin, std::uint32_t end, double& largest) {
                                                     return measureCurveTable(*curve, layout.shape, runs, entries,
                                                                              sampling, begin, end, largest);
                                                 });
    if (measured.failed())
    {
        return measured;
    }
    return lowerCurveTable(shape, std::move(entries), low, high, sampling, request.domainPolicy, request.approximation,
                           maximumError);
}

} // namespace

Result<CurveProgramPlan> buildCurveProgram(const TonePlanShape& shape, const CurveProgramRequest& request)
{
    return buildCurveProgramWith(shape, request, curveTableRunsForHost(), nullptr);
}

Result<CurveProgramPlan> buildCurveProgram(const ExecutionContext& execution, const TonePlanShape& shape,
                                           const CurveProgramRequest& request)
{
    if (!execution.valid())
    {
        return toneFailure(ErrorCode::NotInitialized, "the curve was given an empty execution context");
    }
    return buildCurveProgramWith(shape, request, curveTableRunsFor(execution), &execution);
}

Result<CurveProgramPlan> buildLookupProgram(const TonePlanShape& shape, const LookupProgramRequest& request)
{
    if (request.sampling == PointwiseCurveSampling::Lower)
    {
        return toneFailure(ErrorCode::IncompatibleArguments,
                           "lower-entry sampling is reserved for the opaque real-time-preview gamma plan");
    }
    const CurveApproximation approximation =
        exactEntriesFor(shape.sampleType) != 0u               ? CurveApproximation::ExactAtResidentValues
        : request.sampling == PointwiseCurveSampling::Nearest ? CurveApproximation::SampledNearest
                                                              : CurveApproximation::SampledLinear;
    return lowerTable(shape, request.entries, request.inputLow, request.inputHigh, request.sampling,
                      request.domainPolicy, approximation, 0.0);
}

} // namespace an
