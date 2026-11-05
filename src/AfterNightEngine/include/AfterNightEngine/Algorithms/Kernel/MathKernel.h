// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The transcendental functions, who evaluates them, and how far the answer
/// may be from the scalar one.
///
/// `pow`, `exp` and `log` have no instruction. Something has to provide them, and the engine has
/// three providers rather than one because the right answer depends on what the samples are:
///
/// **An exact table, where the input is a narrow integer.** A sixteen-bit sample has 65536 possible
/// values, so a table of function values indexed by code value is exact by construction and is read
/// with no arithmetic at all. It is the fastest and the most accurate of the three, and it applies
/// to the tone curve, gamma and levels work that calls these functions most.
///
/// **A vector approximation, where the samples are Float32.** Two polynomials - one exponential and
/// one logarithm - are compiled once per instruction set, and everything else is composed from them.
/// They are approximations, and the accepted distance from the scalar answer is stated below as a
/// number rather than left as a preference.
///
/// **The scalar library, where accuracy matters more than throughput.** Float64 arithmetic, and any
/// operation whose contract needs the platform's own rounding, calls libm one sample at a time. It
/// is also the authoritative reference the other two are measured against.
///
/// `selectMathProvider` is where that choice is made once, so an operation states what its samples
/// are instead of each one deciding again. The choice is reported rather than assumed: an execution
/// result carries the provider that ran.
///
/// **Only three functions exist here, and the fourth is composed.** One exponential and one
/// logarithm are the only polynomials compiled per instruction set; `pow` is `exp(g * log(x))`, and
/// a function added later is expected to be derived the same way unless the derivation cannot hold
/// the accuracy its caller needs. That is a size decision as much as an accuracy one: every
/// polynomial compiled here exists once per rung of the instruction-set ladder.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/PlaneView.h>

#include <cstdint>
#include <span>

namespace an
{

/// One transcendental function this engine evaluates.
///
/// The set is small on purpose and grows when an operation needs a member, not in anticipation of
/// one. Each member is a numeric contract with a stated domain, stated behavior at every
/// exceptional input, and a stated distance from the scalar reference.
enum class MathFunction : std::uint8_t
{
    /// No function. The default and the answer for an unknown value.
    Unknown = 0u,

    /// The natural exponential, `e` raised to the sample.
    Exp = 1u,

    /// The natural logarithm of the sample.
    Log = 2u,

    /// The sample raised to a stated exponent, on a non-negative base.
    ///
    /// Deliberately narrower than `std::pow`: this is the transfer-function shape - gamma, tone
    /// curves, display encodings - where the base is a non-negative intensity. A negative base is
    /// outside the domain and produces a quiet not-a-number rather than the signed result
    /// `std::pow` gives for an integral exponent. The scalar reference applies the same rule, so
    /// the narrowing is one contract rather than a difference between two implementations.
    Pow = 3u
};

/// Whether a value names one of the functions this engine evaluates.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param function the value to test.
/// @return true for every member except `MathFunction::Unknown`.
[[nodiscard]] constexpr bool isMathFunction(MathFunction function) noexcept
{
    return static_cast<std::uint8_t>(function) >= static_cast<std::uint8_t>(MathFunction::Exp) &&
           static_cast<std::uint8_t>(function) <= static_cast<std::uint8_t>(MathFunction::Pow);
}

/// The function's stable name for diagnostics, manifests, and tests.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param function the function to name.
/// @return the enumerator spelling, or `"UnknownMathFunction"` for `Unknown` and values outside the
///         enumeration. The returned string has static storage duration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* mathFunctionName(MathFunction function) noexcept;

/// Which of the three implementations evaluates a function.
enum class MathProvider : std::uint8_t
{
    /// No provider. The default and the answer for a request that names no function.
    Unknown = 0u,

    /// A table of function values, one per code value of a narrow integer representation.
    ///
    /// Exact by construction and read without arithmetic. `buildMathFunctionTable` fills one.
    ExactTable = 1u,

    /// The polynomials compiled for the operation's instruction set.
    ///
    /// Float32 samples, and the accuracy tier below.
    VectorApproximation = 2u,

    /// The platform's scalar math library, evaluated in Float64 and narrowed once.
    ///
    /// The authoritative reference, and what a Float64 operation gets.
    ScalarLibrary = 3u
};

/// The provider's stable name for diagnostics, manifests, and tests.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param provider the provider to name.
/// @return the enumerator spelling, or `"UnknownMathProvider"` for `Unknown` and values outside the
///         enumeration. The returned string has static storage duration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* mathProviderName(MathProvider provider) noexcept;

/// The widest table this engine builds, which is one entry per sixteen-bit code value.
///
/// A table wider than this stops being a lookup and starts being a cache miss per sample, and no
/// representation this engine stores has more code values that a transfer function is applied to
/// directly. A thirty-two-bit input is evaluated rather than tabulated.
inline constexpr std::uint32_t kMaxMathTableEntries = 65536u;

/// What one operation states about the samples it is about to evaluate a function over.
struct MathProviderRequest
{
    /// Which function the operation applies.
    MathFunction function = MathFunction::Unknown;

    /// The resident representation the samples are read from.
    SampleType inputSample = SampleType::Float32;

    /// The width the operation's arithmetic is defined in.
    ComputePrecision precision = ComputePrecision::Float32;

    /// How many samples the operation will evaluate.
    ///
    /// It decides whether a table pays for itself: building one costs an evaluation per entry, so a
    /// frame with fewer samples than the representation has code values is cheaper to evaluate
    /// directly. Zero asks about a table that has already been built and is therefore free.
    std::uint64_t sampleCount = 0u;
};

/// Chooses which provider evaluates one operation's function.
///
/// The policy in one place, so an operation states what its samples are rather than deciding again:
///
/// - a Float64 arithmetic width or Float64 samples take the scalar library. The vector polynomials
///   are a Float32 path, and an operation that asked for the wider width asked for it because the
///   narrower answer was not good enough;
/// - an eight- or sixteen-bit input whose sample count is zero or at least its code-value count
///   takes the exact table, which is both faster and exact;
/// - anything else takes the vector approximation, which the operation's dispatch table narrows to
///   the scalar library on a build or a forced backend that has no compiled kernel.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param request what the operation is about to do.
/// @return the provider, or `MathProvider::Unknown` when the request names no known function or an
///         unknown representation.
[[nodiscard]] AFTERNIGHTENGINE_API MathProvider selectMathProvider(const MathProviderRequest& request) noexcept;

/// The accepted distance between a vector transcendental and its scalar reference, in units in the
/// last place of the Float32 result.
///
/// Stated as a number so that a future regression against it is a defect rather than a preference.
/// It covers the two compiled polynomials; a composed function carries the composition's own bound,
/// which `mathUlpBound` derives.
inline constexpr double kVectorMathUlpTier = 5.0;

/// How much of a composed function's error comes from the argument it was given.
///
/// `pow` is `exp(g * log(x))`, so the logarithm's relative error is multiplied by the magnitude of
/// `g * log(x)` before the exponential ever runs: an inaccurate argument to an exponential is an
/// inaccurate result in proportion to how large the argument is. Four units in the last place of
/// relative error in the logarithm and one rounding of the multiplication give nine units of result
/// error per unit of argument magnitude, which is what this constant is.
inline constexpr double kComposedMathUlpPerArgument = 9.0;

/// The largest distance from the scalar reference this contract permits for one evaluation.
///
/// A single number cannot express a composed function's accuracy: `pow` is exact at a base of one
/// and drifts in proportion to `|g * log(x)|`, so a fixed tolerance is either wrong near one or
/// meaningless far from it. This derives the bound from the actual argument instead, which is the
/// same discipline the reduction contract applies to an addition tree.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param function which function was evaluated.
/// @param value the sample it was evaluated at.
/// @param exponent the exponent, for `MathFunction::Pow` and ignored otherwise.
/// @return the permitted distance in units in the last place, which is `kVectorMathUlpTier` for the
///         compiled polynomials and larger for a composed function at an argument far from one.
///         Zero for an unknown function, and zero where the reference result is not a finite normal
///         number - an exceptional or subnormal result is checked by class rather than by distance.
[[nodiscard]] AFTERNIGHTENGINE_API double mathUlpBound(MathFunction function, float value, double exponent) noexcept;

/// The accuracy contract of one function, for a plan or a registry that carries one.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param function the function.
/// @return a `KernelAccuracyMetric::Ulp` contract at the accepted tier, or an exact contract for an
///         unknown function, which has no implementation to be inaccurate.
[[nodiscard]] constexpr KernelAccuracyContract mathFunctionAccuracy(MathFunction function) noexcept
{
    if (!isMathFunction(function))
    {
        return KernelAccuracyContract{};
    }
    return KernelAccuracyContract{.metric = KernelAccuracyMetric::Ulp, .maximumError = kVectorMathUlpTier};
}

/// The authoritative scalar value of one function at one sample.
///
/// Evaluated by the platform's math library in Float64 and narrowed once to Float32, which is what
/// makes it the reference rather than one more approximation: every provider is compared against
/// this, and this is what `MathProvider::ScalarLibrary` and `MathProvider::ExactTable` produce.
///
/// The exceptional inputs are part of the contract and are the same for every provider:
///
/// | input | `Exp` | `Log` | `Pow` |
/// |---|---|---|---|
/// | not a number | not a number | not a number | not a number |
/// | negative | ordinary | not a number | not a number |
/// | negative zero | one | negative infinity | negative for an odd integral exponent; otherwise as positive zero |
/// | positive zero | one | negative infinity | `+0`, `+inf` or one by the exponent's sign |
/// | positive infinity | positive infinity | positive infinity | `+inf`, `+0` or one by the sign |
/// | negative infinity | positive zero | not a number | not a number |
///
/// An exponent of zero produces one for every base inside the domain, and a base of one produces
/// one for every exponent.
///
/// At a negative-zero base and an odd integral exponent every provider keeps the sign, producing
/// negative zero for a positive exponent and negative infinity for a negative one.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param function which function to evaluate. An unknown one produces a quiet not-a-number.
/// @param value the sample.
/// @param exponent the exponent, for `MathFunction::Pow` and ignored otherwise. Must be finite; a
///        non-finite exponent produces a quiet not-a-number.
/// @return the narrowed Float32 result.
[[nodiscard]] AFTERNIGHTENGINE_API float referenceMathValue(MathFunction function, float value,
                                                            double exponent) noexcept;

/// Which function one invocation applies, and to what.
struct MathKernelParameters
{
    /// The function to apply.
    MathFunction function = MathFunction::Unknown;

    /// The exponent for `MathFunction::Pow`, ignored by every other function. Must be finite.
    double exponent = 1.0;
};

/// The evenly spaced domain a table covers.
struct MathTableRequest
{
    /// The function and its exponent.
    MathKernelParameters parameters{};

    /// The value the first entry is evaluated at.
    double first = 0.0;

    /// The value the last entry is evaluated at. May be below `first`, which reverses the table.
    double last = 1.0;
};

/// What one table-building or plane evaluation did.
struct MathExecutionResult
{
    /// Pixel selection and writes. An invalid sample leaves its output untouched.
    KernelRunResult run{};

    /// Which implementation actually evaluated the function.
    MathProvider provider = MathProvider::Unknown;

    /// How many written results are not finite.
    ///
    /// Always a count rather than a failure: a logarithm of zero is negative infinity and an
    /// exponential of a large sample is infinite, and both are correct answers that the caller may
    /// want to know the number of.
    std::uint64_t nonFiniteOutputs = 0u;

    /// Workers the operation could actually use.
    std::uint32_t plannedWorkers = 0u;
};

/// Fills a table with the exact value of one function at each of a domain's evenly spaced points.
///
/// This is `MathProvider::ExactTable`, materialized. Entry `k` of `n` is evaluated at
/// `first + k * (last - first) / (n - 1)`, so the first and last entries sit exactly on the domain's
/// ends and a table of one entry is evaluated at `first`. Every entry comes from
/// `referenceMathValue`, which is what makes a table exact rather than one more approximation: a
/// sixteen-bit sample indexes it directly and the function is never evaluated again.
///
/// The evaluation runs on the operation's threads and observes cancellation, so a cancelled run may
/// leave a prefix of the table written.
///
/// @param execution the operation policy and engine-context lease.
/// @param request the function, its exponent, and the domain to cover.
/// @param entries the table to fill, borrowed for the call. Between one and
///        `kMaxMathTableEntries` entries.
/// @return the counts and the provider; `ErrorCode::InvalidArgument` for an unknown function or a
///         non-finite exponent or domain end; `ErrorCode::OutOfRange` for an empty table or one
///         above `kMaxMathTableEntries`; or an execution or cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MathExecutionResult>
buildMathFunctionTable(const ExecutionContext& execution, const MathTableRequest& request, std::span<float> entries);

/// Applies one function to every selected sample of a Float32 plane.
///
/// This is the Float32 half of the policy: the operation's dispatch table decides whether the
/// compiled polynomials or the scalar library evaluates it, and `MathExecutionResult::provider`
/// reports which one did. A caller whose samples are a narrow integer builds a table instead and
/// never reaches this entry point.
///
/// Exact in-place is a supported invocation and every other overlap is refused. A sample the
/// validity rule rejects leaves its destination untouched, and the samples that pass are evaluated
/// in maximal contiguous runs, so an invalid sample costs the run it interrupts rather than the
/// vectorization of the whole row. Rows are divided across the operation's threads and each sample
/// is independent, so the result does not depend on how many threads ran.
///
/// Cancellation is observed before output and once per row, so a stopped run may leave a prefix of
/// the destination written.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the single-sample input plane.
/// @param destination the equally shaped output plane, disjoint from the source or exactly it.
/// @param parameters the function and its exponent.
/// @param validity the input rule and optional mask, borrowed for the call.
/// @return the counts, the provider and the plan; `ErrorCode::InvalidArgument` for an unknown
///         function, a non-finite exponent, or partially overlapping buffers; or an execution,
///         shape, channel, mask, or cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MathExecutionResult>
applyMathFunctionPlane(const ExecutionContext& execution, PlaneView<const float> source, PlaneView<float> destination,
                       const MathKernelParameters& parameters, const KernelValidityView& validity);

} // namespace an
