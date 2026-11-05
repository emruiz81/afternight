// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Domain-checked power, gamma, curve and lookup-table planning.
///
/// A tone transform is planned once and lowered into the same `PointwiseProgram` as arithmetic. A
/// narrow unsigned integer gets an exact table over every code value; floating working images get a
/// sampled curve or the vector-math power step. The plan reports that choice and its error contract,
/// so a faster approximation is never an unnamed implementation detail.
///
/// Every tone plan reads and writes a normalized linear or an encoded nonlinear domain and refuses
/// raw codes and physical quantities, and a floating lookup states what it does outside its
/// interval before an index is ever formed.
///
/// One entry point is not production arithmetic: `buildRealtimePreviewGammaProgram` builds a coarse
/// gamma table for a display control's preview, and its plan runs only through
/// `executeRealtimePreviewGamma`. The path is separate and opaque so that its coarser numeric
/// contract serves a disposable display and never reaches a processed image.
///
/// Refusals follow one pattern across the builders: `ErrorCode::IncompatibleArguments` for a domain
/// outside the two tone domains or a request its representation cannot take,
/// `ErrorCode::Unsupported` for a representation other than Float32, Float64, UInt8 and UInt16 and
/// for a negative exponent over integer storage, `ErrorCode::InvalidArgument` for a malformed
/// coefficient, point or table, and the pointwise builder's `ErrorCode::InvalidArgument` for a
/// channel count or precision it refuses. Each builder names its exceptions to the pattern.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/MathKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace an
{

class ExecutionContext;
struct KernelValidityView;
struct PointwiseExecutionResult;
struct PointwiseImageView;
struct PointwiseMutableImageView;

/// Representation, domain and arithmetic declarations shared by one tone plan.
struct TonePlanShape
{
    /// Resident representation. Power and curves support Float32, Float64, UInt8 and UInt16.
    SampleType sampleType = SampleType::Float32;

    /// Meaning of the input values. Tone operations accept normalized linear or encoded nonlinear
    /// domains and refuse raw codes and physical quantities.
    NumericDomain inputDomain = NumericDomain::NormalizedLinear;

    /// Meaning of the output values after successful execution; also normalized linear or encoded
    /// nonlinear.
    NumericDomain outputDomain = NumericDomain::EncodedNonlinear;

    /// Channels per pixel, from one through `kMaxPointwiseChannels`.
    std::uint32_t channelCount = 1u;

    /// Arithmetic width. Float64 requires its approved justification.
    ComputePrecision precision = ComputePrecision::Float32;

    /// Why Float64 is required, empty for Float32.
    std::optional<Float64Justification> precisionJustification{};

    /// Precision promise used by the common working-image rule.
    OutputPrecisionContract outputContract = OutputPrecisionContract::NormalizedOrLossy;
};

/// Whether a gamma value encodes or decodes a normalized signal.
enum class GammaDirection : std::uint8_t
{
    /// Raises the input to `1 / gamma` and writes an encoded nonlinear domain.
    Encode = 0u,

    /// Raises the input to `gamma` and writes a normalized linear domain.
    Decode = 1u
};

/// Whether a value names a gamma direction this build supports.
///
/// @param direction the value to inspect.
/// @return true for both supported directions.
[[nodiscard]] constexpr bool isGammaDirection(GammaDirection direction) noexcept
{
    return direction == GammaDirection::Encode || direction == GammaDirection::Decode;
}

/// A power/gamma program and the planned transcendental provider.
class PowerProgramPlan
{
public:
    /// Takes ownership of a validated power program and its provider declaration.
    ///
    /// @param program the executable program.
    /// @param provider exact table, vector approximation or scalar library.
    AFTERNIGHTENGINE_API PowerProgramPlan(PointwiseProgram program, MathProvider provider) noexcept;

    /// The immutable program to execute or compose.
    ///
    /// @return the owned program.
    [[nodiscard]] AFTERNIGHTENGINE_API const PointwiseProgram& program() const noexcept;

    /// Provider selected by the plan.
    ///
    /// A forced reference backend narrows a Float32 vector approximation to the scalar library at
    /// execution; an exact integer table remains exact under every backend. Even under a compiled
    /// backend, the samples the compiled run does not reach - a row's tail past its last whole
    /// vector, and every sample when the validity rule is not all-valid - are computed by the scalar
    /// library, so the provider names the accuracy the result is held to rather than the routine
    /// every sample took.
    ///
    /// @return the planned provider.
    [[nodiscard]] AFTERNIGHTENGINE_API MathProvider provider() const noexcept;

private:
    PointwiseProgram m_program;
    MathProvider m_provider;
};

/// One point through which a piecewise curve passes.
struct CurveControlPoint
{
    /// Input coordinate.
    double input = 0.0;

    /// Output coordinate.
    double output = 0.0;
};

/// How the analytic curve joins its control points.
///
/// The last three are the GNU Scientific Library's interpolation rules of those names, reproduced
/// rather than approximated, so a curve a person drew against one of them keeps its shape. They
/// differ in what they optimize and a caller should choose deliberately:
/// `NaturalCubic` is the smoothest - it is the only one here whose second derivative is continuous;
/// `Akima` does not propagate a local wiggle past its two neighbours on either side; `Steffen` and
/// `MonotoneCubic` both guarantee that an extremum lands only on a control point, by two different
/// slope limits, and are the only two that cannot leave the interval their control points lie in.
/// `NaturalCubic` and `Akima` can both overshoot it, and a curve lowered into a program is held
/// inside it - see `buildCurveProgram`.
enum class CurveInterpolation : std::uint8_t
{
    /// A straight segment between each adjacent pair.
    Linear = 0u,

    /// A shape-preserving cubic Hermite spline with monotone tangents.
    MonotoneCubic = 1u,

    /// The twice-differentiable cubic spline whose second derivative vanishes at both ends.
    ///
    /// Needs `kMinimumNaturalCubicCurvePoints` control points.
    NaturalCubic = 2u,

    /// Akima's rule: each node's derivative weighted by its neighbouring slope differences.
    ///
    /// Needs `kMinimumAkimaCurvePoints` control points, which is what its two-neighbours-each-side
    /// weighting reads.
    Akima = 3u,

    /// Steffen's rule: a parabolic derivative limited to what keeps each segment monotone.
    ///
    /// Needs `kMinimumSteffenCurvePoints` control points.
    Steffen = 4u
};

/// The fewest control points `CurveInterpolation::NaturalCubic` is defined over.
inline constexpr std::size_t kMinimumNaturalCubicCurvePoints = 3u;

/// The fewest control points `CurveInterpolation::Akima` is defined over.
inline constexpr std::size_t kMinimumAkimaCurvePoints = 5u;

/// The fewest control points `CurveInterpolation::Steffen` is defined over.
inline constexpr std::size_t kMinimumSteffenCurvePoints = 3u;

/// Whether a value names a curve interpolation this build supports.
///
/// @param interpolation the value to inspect.
/// @return true for every supported interpolation.
[[nodiscard]] constexpr bool isCurveInterpolation(CurveInterpolation interpolation) noexcept
{
    return static_cast<std::uint8_t>(interpolation) <= static_cast<std::uint8_t>(CurveInterpolation::Steffen);
}

/// The fewest control points an interpolation is defined over.
///
/// @param interpolation which rule joins the points.
/// @return two for the local rules, or the stated minimum of a rule that reads further.
[[nodiscard]] constexpr std::size_t minimumCurvePoints(CurveInterpolation interpolation) noexcept
{
    switch (interpolation)
    {
    case CurveInterpolation::NaturalCubic:
        return kMinimumNaturalCubicCurvePoints;
    case CurveInterpolation::Akima:
        return kMinimumAkimaCurvePoints;
    case CurveInterpolation::Steffen:
        return kMinimumSteffenCurvePoints;
    case CurveInterpolation::Linear:
    case CurveInterpolation::MonotoneCubic:
        break;
    }
    return 2u;
}

/// How a continuous curve is represented in a pointwise program.
enum class CurveApproximation : std::uint8_t
{
    /// One exact entry per UInt8 or UInt16 code; refused for floating input.
    ExactAtResidentValues = 0u,

    /// A finite table sampled uniformly and read through nearest-neighbour lookup; refused for
    /// integer input.
    SampledNearest = 1u,

    /// A finite table sampled uniformly and read through linear interpolation; refused for integer
    /// input.
    SampledLinear = 2u
};

/// Whether a value names a curve approximation this build supports.
///
/// @param approximation the value to inspect.
/// @return true for every supported approximation.
[[nodiscard]] constexpr bool isCurveApproximation(CurveApproximation approximation) noexcept
{
    return static_cast<std::uint8_t>(approximation) <= static_cast<std::uint8_t>(CurveApproximation::SampledLinear);
}

/// Entries in the real-time-preview gamma lookup.
///
/// 1,024 uniformly spaced entries. Keeping the count here makes that choice, made for speed,
/// inspectable and keeps it out of production gamma.
inline constexpr std::uint32_t kRealtimePreviewGammaTableEntries = 1024u;

/// What a curve or lookup table does outside its input interval.
enum class CurveDomainPolicy : std::uint8_t
{
    /// Uses the nearest endpoint. The safe default: a value outside the unit interval never forms an
    /// index outside the table.
    Clamp = 0u,

    /// Refuses the first outside value with `ErrorCode::OutOfRange`.
    Reject = 1u,

    /// Continues the first or last segment linearly, so a result may leave the unit interval.
    LinearExtrapolation = 2u
};

/// Whether a value names a curve-domain policy this build supports.
///
/// @param policy the value to inspect.
/// @return true for every supported policy.
[[nodiscard]] constexpr bool isCurveDomainPolicy(CurveDomainPolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(CurveDomainPolicy::LinearExtrapolation);
}

/// A piecewise curve and how it is lowered into a program.
struct CurveProgramRequest
{
    /// At least `minimumCurvePoints(interpolation)` finite points with strictly increasing input
    /// coordinates and outputs in `[0, 1]`, borrowed for the call.
    std::span<const CurveControlPoint> points{};

    /// How the control points are joined.
    CurveInterpolation interpolation = CurveInterpolation::MonotoneCubic;

    /// Exact integer table, nearest sampled table or linearly sampled table.
    CurveApproximation approximation = CurveApproximation::SampledLinear;

    /// Entries in a sampled floating table, from two through `kMaxMathTableEntries`, spaced evenly
    /// from the first control point's input to the last's. Ignored by the exact resident-value
    /// mode, whose representation decides the count.
    std::uint32_t tableEntries = 4096u;

    /// What values outside the first and last control-point inputs do.
    CurveDomainPolicy domainPolicy = CurveDomainPolicy::Clamp;
};

/// A curve/LUT program and its measured approximation report.
class CurveProgramPlan
{
public:
    /// Takes ownership of a validated program and its accuracy report.
    ///
    /// @param program the executable program.
    /// @param approximation the named lowering mode.
    /// @param tableEntries entries held by the program.
    /// @param maximumAbsoluteError largest sampled error against the analytic curve.
    AFTERNIGHTENGINE_API CurveProgramPlan(PointwiseProgram program, CurveApproximation approximation,
                                          std::uint32_t tableEntries, double maximumAbsoluteError) noexcept;

    /// The immutable program to execute or compose.
    ///
    /// @return the owned program.
    [[nodiscard]] AFTERNIGHTENGINE_API const PointwiseProgram& program() const noexcept;

    /// How the curve was lowered.
    ///
    /// @return the named approximation.
    [[nodiscard]] AFTERNIGHTENGINE_API CurveApproximation approximation() const noexcept;

    /// Number of table entries held by the program.
    ///
    /// @return the entry count.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t tableEntries() const noexcept;

    /// Largest absolute deviation found between table evaluation and the analytic curve.
    ///
    /// The check probes every table interval at its quarter points. It also carries the largest
    /// amount any entry was held inside the normalized interval: an interpolation that is not
    /// shape-preserving can leave that interval between two control points, and a clamped entry is
    /// as far from the analytic curve as a sampled one. An exact resident-value table of a
    /// shape-preserving curve reports zero, because every value the representation can hold was
    /// evaluated directly and none of them moved.
    ///
    /// @return the measured absolute error.
    [[nodiscard]] AFTERNIGHTENGINE_API double maximumAbsoluteError() const noexcept;

private:
    PointwiseProgram m_program;
    CurveApproximation m_approximation;
    std::uint32_t m_tableEntries;
    double m_maximumAbsoluteError;
};

/// An opaque coarse-gamma plan that can run only through the real-time-preview executor.
///
/// The underlying lower-neighbour pointwise program is deliberately not exposed. This keeps the fast
/// approximation available to an interactive display control without making it composable into a
/// production processing chain.
class RealtimePreviewGammaProgramPlan
{
public:
    /// Copies a reusable preview plan and its owned table.
    RealtimePreviewGammaProgramPlan(const RealtimePreviewGammaProgramPlan&) = default;

    /// Moves a reusable preview plan and its owned table.
    ///
    /// The source remains valid but its state is unspecified.
    RealtimePreviewGammaProgramPlan(RealtimePreviewGammaProgramPlan&&) noexcept = default;

    /// Copies a preview plan over this one.
    ///
    /// @return this plan.
    RealtimePreviewGammaProgramPlan& operator=(const RealtimePreviewGammaProgramPlan&) = default;

    /// Moves a preview plan over this one.
    ///
    /// The source remains valid but its state is unspecified.
    ///
    /// @return this plan.
    RealtimePreviewGammaProgramPlan& operator=(RealtimePreviewGammaProgramPlan&&) noexcept = default;

    /// Releases the owned preview table.
    ~RealtimePreviewGammaProgramPlan() = default;

    /// Number of entries held by the lookup.
    ///
    /// @return `kRealtimePreviewGammaTableEntries` for every successfully built plan.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t tableEntries() const noexcept;

    /// Largest measured absolute deviation from the analytic gamma transfer.
    ///
    /// The largest distance of an entry from the transfer over the interval it serves, which the
    /// rising transfer reaches at the interval's end. This is an accuracy report for a disposable
    /// display preview, not the production engine's numeric promise.
    ///
    /// @return the measured maximum absolute error.
    [[nodiscard]] AFTERNIGHTENGINE_API double maximumAbsoluteError() const noexcept;

private:
    friend AFTERNIGHTENGINE_API Result<RealtimePreviewGammaProgramPlan>
    buildRealtimePreviewGammaProgram(const TonePlanShape&, double, GammaDirection);
    friend AFTERNIGHTENGINE_API Result<PointwiseExecutionResult>
    executeRealtimePreviewGamma(const ExecutionContext&, const RealtimePreviewGammaProgramPlan&, PointwiseImageView,
                                PointwiseMutableImageView, const KernelValidityView&);

    RealtimePreviewGammaProgramPlan(PointwiseProgram program, std::uint32_t tableEntries,
                                    double maximumAbsoluteError) noexcept;

    PointwiseProgram m_program;
    std::uint32_t m_tableEntries;
    double m_maximumAbsoluteError;
};

/// A caller-supplied sampled lookup table.
struct LookupProgramRequest
{
    /// From two through `kMaxMathTableEntries` finite output values in `[0, 1]`, borrowed for the
    /// call.
    std::span<const double> entries{};

    /// Input represented by the first entry.
    double inputLow = 0.0;

    /// Input represented by the last entry, greater than `inputLow`.
    double inputHigh = 1.0;

    /// How values between entries are resolved for floating input. `Lower` is refused here because
    /// it belongs only to the opaque real-time-preview gamma plan.
    PointwiseCurveSampling sampling = PointwiseCurveSampling::Linear;

    /// What values outside the input interval do.
    CurveDomainPolicy domainPolicy = CurveDomainPolicy::Clamp;
};

/// Builds a domain-checked power program.
///
/// The exponent must be finite. Input and output domains must each be normalized linear or encoded
/// nonlinear. UInt8 and UInt16 input becomes an exact full-code-space table, each entry the
/// reference Float32 power of the code's normalized value scaled back to a code coordinate. Float32
/// storage computed in Float32 uses the approved vector approximation unless execution forces the
/// reference backend; any other floating shape uses the scalar library. Other resident
/// representations are refused rather than interpreted.
///
/// @param shape representation, domains, channels and precision.
/// @param exponent finite exponent applied directly.
/// @return the program and provider; `ErrorCode::InvalidArgument` for an exponent that is not
///         finite; `ErrorCode::Unsupported` for a negative exponent over integer storage, whose zero
///         code has no finite result; or a shape failure as this header's opening describes.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PowerProgramPlan> buildPowerProgram(const TonePlanShape& shape,
                                                                              double exponent);

/// Builds a pure gamma encode or decode program.
///
/// Encoding requires normalized-linear input and writes encoded-nonlinear output; decoding requires
/// the reverse. The caller's shape must state those domains consistently. A gamma must be positive
/// and finite.
///
/// @param shape representation, domains, channels and precision.
/// @param gamma positive pure-gamma value.
/// @param direction encode with `1 / gamma` or decode with `gamma`.
/// @return the program and provider; `ErrorCode::InvalidArgument` for a gamma that is not positive
///         and finite or an unknown direction; `ErrorCode::IncompatibleArguments` when the shape's
///         domains disagree with the direction; or any failure `buildPowerProgram` names.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PowerProgramPlan> buildGammaProgram(const TonePlanShape& shape, double gamma,
                                                                              GammaDirection direction);

/// Builds the coarse gamma lookup for a real-time preview control.
///
/// This is deliberately a separate entry point from `buildGammaProgram`: the production builder
/// applies the engine's ordinary bounded-accuracy implementation, while this one builds a
/// 1,024-entry, lower-neighbour, ten-bit-quantized lookup. The plan is intended only for
/// disposable display previews and must not be used to produce a processed image that a caller
/// saves or feeds into another algorithm.
///
/// The table is built once and may be reused for successive preview frames. Planning evaluates the
/// analytic curve but execution performs no transcendental operation. Inputs outside `[0, 1]` are
/// clamped to the nearest endpoint.
///
/// @param shape representation, domains, channels and precision. Float32 and Float64 preview
///        working images are accepted; integer previews continue to use the exact resident table
///        from `buildGammaProgram`.
/// @param gamma positive pure-gamma value.
/// @param direction encode with `1 / gamma` or decode with `gamma`.
/// @return the opaque preview-only plan and its measured maximum absolute error;
///         `ErrorCode::InvalidArgument` for a gamma that is not positive and finite or an unknown
///         direction; `ErrorCode::IncompatibleArguments` when the domains disagree with the
///         direction or the representation is not floating; or a shape failure as this header's
///         opening describes.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RealtimePreviewGammaProgramPlan>
buildRealtimePreviewGammaProgram(const TonePlanShape& shape, double gamma, GammaDirection direction);

/// Runs the coarse gamma lookup for a disposable real-time preview.
///
/// This is the only image executor that accepts the opaque preview plan. The ordinary
/// `executePointwiseProgram` entry point rejects its lower-neighbour approximation, so the plan
/// cannot be inserted into a production processing chain. Source and destination must be equally
/// shaped planar images in the representation and channel count supplied to the builder. Exact
/// in-place execution is supported; any other overlap is refused. Cancellation may leave a
/// partially written preview, which the caller discards.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the immutable preview plan.
/// @param source the single borrowed preview source.
/// @param destination the equally shaped writable preview destination.
/// @param validity the source validity declaration and optional packed mask.
/// @return execution and conversion counts, or any failure `executePointwiseProgram` names other
///         than the refusal of a lower-neighbour curve.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PointwiseExecutionResult>
executeRealtimePreviewGamma(const ExecutionContext& execution, const RealtimePreviewGammaProgramPlan& plan,
                            PointwiseImageView source, PointwiseMutableImageView destination,
                            const KernelValidityView& validity);

/// Evaluates a piecewise curve once under its explicit outside-domain policy.
///
/// This is the independent scalar meaning used to build and test tables. It validates the complete
/// point sequence on every call and is intended for planning and oracles, not an image loop.
///
/// @param points two or more finite points with strictly increasing inputs.
/// @param interpolation how the points are joined.
/// @param domainPolicy clamp, reject or extrapolate outside the covered interval.
/// @param input value to evaluate; a not-a-number propagates.
/// @return the analytic output, which `CurveInterpolation::NaturalCubic` and
///         `CurveInterpolation::Akima` may place outside [0, 1] between two control points, and
///         `CurveDomainPolicy::LinearExtrapolation` beyond the end ones; `ErrorCode::InvalidArgument`
///         for malformed points, an output outside [0, 1], an unknown interpolation or policy, or
///         fewer points than `minimumCurvePoints(interpolation)`; and `ErrorCode::OutOfRange` for
///         an outside input under `Reject`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> evaluateCurve(std::span<const CurveControlPoint> points,
                                                                CurveInterpolation interpolation,
                                                                CurveDomainPolicy domainPolicy, double input);

/// Samples a piecewise curve and lowers it into a fusable program.
///
/// Output values are normalized values. For UInt8/UInt16 input the planner converts them to that
/// representation's code coordinate inside the exact table, so execution narrows once without an
/// implicit full-scale conversion.
///
/// **A table entry is held inside [0, 1]**, because that is what the output domain promises and a
/// program cannot carry a value that breaks it. `CurveInterpolation::NaturalCubic` and
/// `CurveInterpolation::Akima` can both reach outside the interval their control points lie in; the
/// amount an entry was held by is part of the plan's `maximumAbsoluteError` rather than a silent
/// correction.
///
/// An exact resident-value table evaluates the curve at every code's normalized value, so under
/// `CurveDomainPolicy::Reject` a curve whose control points do not cover `[0, 1]` fails the build.
///
/// @param shape representation, domains, channels and precision.
/// @param request control points and named approximation.
/// @return the program and accuracy report; `ErrorCode::InvalidArgument` for an unknown
///         approximation or any curve `evaluateCurve` refuses, including one with fewer points than
///         `minimumCurvePoints(request.interpolation)`; `ErrorCode::IncompatibleArguments` for an
///         exact table over floating input or a sampled one over integer input;
///         `ErrorCode::OutOfRange` for a sampled table size outside two through
///         `kMaxMathTableEntries`, or a code the `Reject` policy refuses; or a shape failure as
///         this header's opening describes.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CurveProgramPlan> buildCurveProgram(const TonePlanShape& shape,
                                                                              const CurveProgramRequest& request);

/// Samples a piecewise curve and lowers it into a fusable program, on an operation's workers.
///
/// The plan is the one `buildCurveProgram(shape, request)` builds, bit for bit, for every thread count: the table's
/// entries and its distance from the curve are measured in chunks whose largest distances are merged. A preview that
/// builds its curve's plan for every request is what this is for, because a table of `kMaxMathTableEntries` is
/// measured at three points inside every interval.
///
/// @param execution the operation the table is built in; its admitted workers share the table and its stop request
///        is honoured between chunks.
/// @param shape representation, domains, channels and precision.
/// @param request control points and named approximation.
/// @return what `buildCurveProgram(shape, request)` returns; or `ErrorCode::NotInitialized` for an execution that
///         holds nothing, and `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the operation is
///         stopped while the table is built.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CurveProgramPlan>
buildCurveProgram(const ExecutionContext& execution, const TonePlanShape& shape, const CurveProgramRequest& request);

/// Lowers a caller-supplied sampled table into a fusable program.
///
/// Float input reads the table through nearest or linear sampling and the named outside-domain
/// policy. Lower-neighbour sampling is refused because it belongs only to the opaque real-time
/// preview gamma plan. UInt8 and UInt16 require exactly one entry per code, an input interval of
/// `[0, 1]`, and linear sampling; their normalized outputs are converted to code coordinates while
/// the plan is built. The plan reports a maximum absolute error of zero, because the caller's table
/// is the curve and there is nothing else to measure it against.
///
/// @param shape representation, domains, channels and precision.
/// @param request table, input interval and policies.
/// @return the program; `ErrorCode::InvalidArgument` for a table of the wrong size or holding a
///         value that is not finite or outside `[0, 1]`, an interval that is not finite and
///         increasing, or an unknown policy; `ErrorCode::IncompatibleArguments` for lower-neighbour
///         sampling or an integer table that is not one entry per code over `[0, 1]` with linear
///         sampling; or a shape failure as this header's opening describes.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CurveProgramPlan> buildLookupProgram(const TonePlanShape& shape,
                                                                               const LookupProgramRequest& request);

} // namespace an
