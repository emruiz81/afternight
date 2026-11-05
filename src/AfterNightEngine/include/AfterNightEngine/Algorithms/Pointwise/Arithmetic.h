// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Planned arithmetic and affine image operations.
///
/// The operations in this header produce `PointwiseProgram`s instead of running a second image loop.
/// A caller may execute one plan immediately with `executePointwiseProgram`, or join it to the
/// programs before and after it with `fusePointwisePrograms`, so arithmetic, exposure and contrast
/// run in the same single image pass as every other pointwise step rather than one pass each.
///
/// **Arithmetic is performed in the image's stated numeric domain.** These plans accept floating
/// working images only: a normalized Float32 sample is already a normalized value, a raw-code float
/// is already a code value, and no operation guesses from the representation. Integer storage
/// converts once to a working image or uses a tone table whose mapping is explicit; it is never
/// silently treated as either raw or normalized here.
///
/// **Nothing is clamped unless a clamp is asked for.** A caller that needs a range composes a `Clamp`
/// step or states one in `buildScaleBiasProgram`, and a rescale of a constant interval has a named
/// answer instead of a division by zero.
///
/// Every builder here checks its own coefficients, then that the representation is Float32 or
/// Float64 (`ErrorCode::IncompatibleArguments` otherwise) and the domain is one this build knows
/// (`ErrorCode::InvalidArgument` otherwise), and then passes what the pointwise builder refuses -
/// a channel count outside one through `kMaxPointwiseChannels` or a precision the working-image
/// rule refuses - through as its `ErrorCode::InvalidArgument`.

#pragma once

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <optional>

namespace an
{

/// The representation, domain and arithmetic width shared by one planned pointwise operation.
struct PointwisePlanShape
{
    /// Float32 by default; only Float32 and Float64 are accepted by the arithmetic planners.
    SampleType sampleType = SampleType::Float32;

    /// What the source values mean. Constants and operands are expressed in this domain.
    NumericDomain numericDomain = NumericDomain::NormalizedLinear;

    /// Channels per pixel, from one through `kMaxPointwiseChannels`.
    std::uint32_t channelCount = 1u;

    /// Width of the operation's arithmetic.
    ComputePrecision precision = ComputePrecision::Float32;

    /// Why Float64 is required. Empty for Float32 and required for Float64.
    std::optional<Float64Justification> precisionJustification{};

    /// What the result promises about precision.
    OutputPrecisionContract outputContract = OutputPrecisionContract::NormalizedOrLossy;

    /// Whether an adjacent scale and bias may contract into one fused multiply-add.
    bool allowContraction = false;
};

/// One two-image arithmetic operation.
enum class BinaryArithmeticOperation : std::uint8_t
{
    /// `left + right * rightScale`.
    Add = 0u,

    /// `left - right * rightScale`.
    Subtract = 1u,

    /// `left * right * rightScale`.
    Multiply = 2u,

    /// `left / right * rightScale`, with IEC 60559 zero and non-finite behavior.
    Divide = 3u,

    /// The lesser value, as `fmin` chooses it: when exactly one operand is not a number, the other
    /// one wins.
    Minimum = 4u,

    /// The greater value, as `fmax` chooses it: when exactly one operand is not a number, the other
    /// one wins.
    Maximum = 5u
};

/// Whether a value names a binary arithmetic operation this build supports.
///
/// @param operation the value to inspect.
/// @return true for every supported operation.
[[nodiscard]] constexpr bool isBinaryArithmeticOperation(BinaryArithmeticOperation operation) noexcept
{
    return static_cast<std::uint8_t>(operation) <= static_cast<std::uint8_t>(BinaryArithmeticOperation::Maximum);
}

/// A finite inclusive clamp interval.
struct ClampInterval
{
    /// Inclusive lower bound.
    double low = 0.0;

    /// Inclusive upper bound, at least `low`.
    double high = 1.0;
};

/// How rescaling a constant input interval is resolved.
enum class ConstantRescalePolicy : std::uint8_t
{
    /// Fill with `RescaleRequest::outputLow`, whichever side of `outputHigh` it lies on.
    MapToLower = 0u,

    /// Fill with the midpoint of the two output endpoints.
    MapToMidpoint = 1u,

    /// Fill with `RescaleRequest::outputHigh`, whichever side of `outputLow` it lies on.
    MapToUpper = 2u,

    /// Leave every input value unchanged, through a scale by one and a bias of zero; a negative zero
    /// becomes positive zero.
    Preserve = 3u,

    /// Refuse the plan with `ErrorCode::EmptySelection`.
    Reject = 4u
};

/// Whether a value names a constant-image policy this build supports.
///
/// @param policy the value to inspect.
/// @return true for every supported policy.
[[nodiscard]] constexpr bool isConstantRescalePolicy(ConstantRescalePolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(ConstantRescalePolicy::Reject);
}

/// What kind of rescale plan was produced.
enum class RescaleDisposition : std::uint8_t
{
    /// A non-constant interval became an affine transform.
    Affine = 0u,

    /// A constant interval became a stated constant output. The program multiplies by zero and adds
    /// the constant, so a sample that is not finite becomes a not-a-number rather than the constant.
    ConstantMapped = 1u,

    /// A constant interval was deliberately preserved.
    ConstantPreserved = 2u
};

/// A rescale request with an explicit answer for a constant input interval, which would otherwise
/// divide by zero.
struct RescaleRequest
{
    /// Smallest selected input value.
    double inputLow = 0.0;

    /// Largest selected input value, at least `inputLow`.
    double inputHigh = 1.0;

    /// Value `inputLow` maps to when the input interval is not constant.
    double outputLow = 0.0;

    /// Value `inputHigh` maps to when the input interval is not constant. May be below `outputLow`.
    double outputHigh = 1.0;

    /// What happens when `inputLow == inputHigh`.
    ConstantRescalePolicy constantPolicy = ConstantRescalePolicy::MapToLower;

    /// Whether results are clamped to the closed interval between the two output endpoints. Read
    /// only for a non-constant interval.
    bool clampOutput = true;
};

/// A rescale program and how its input interval was resolved: affinely, or by the constant policy.
class RescalePlan
{
public:
    /// Takes ownership of an already validated program and its outcome.
    ///
    /// @param program the executable pointwise chain.
    /// @param disposition how the input interval was resolved.
    /// @param constantOutput the mapped value for `ConstantMapped`, empty otherwise.
    AFTERNIGHTENGINE_API RescalePlan(PointwiseProgram program, RescaleDisposition disposition,
                                     std::optional<double> constantOutput) noexcept;

    /// The immutable program to execute or compose.
    ///
    /// @return the owned program.
    [[nodiscard]] AFTERNIGHTENGINE_API const PointwiseProgram& program() const noexcept;

    /// Whether this is an affine, mapped-constant or preserved-constant result.
    ///
    /// @return the disposition.
    [[nodiscard]] AFTERNIGHTENGINE_API RescaleDisposition disposition() const noexcept;

    /// The constant written by a mapped-constant result.
    ///
    /// @return the value, or no value for affine and preserved plans.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<double> constantOutput() const noexcept;

private:
    PointwiseProgram m_program;
    RescaleDisposition m_disposition;
    std::optional<double> m_constantOutput;
};

/// Builds a two-source add, subtract, multiply, divide, minimum or maximum program.
///
/// Both sources have the same floating representation, shape and stated numeric domain. No
/// normalization and no result clamp is implicit. `rightScale` is ignored by minimum and maximum
/// and must be one for them, so a mistakenly supplied scale is refused instead of discarded.
///
/// @param shape the shared representation, domain, channels and arithmetic width.
/// @param operation which binary operation to perform.
/// @param rightScale finite scalar applied as documented by the operation.
/// @return the two-source program; `ErrorCode::InvalidArgument` for an unknown operation, a
///         `rightScale` that is not finite, or one other than one for minimum or maximum; or a shape
///         failure as this header's opening describes.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PointwiseProgram>
buildBinaryArithmeticProgram(const PointwisePlanShape& shape, BinaryArithmeticOperation operation,
                             double rightScale = 1.0);

/// Builds a two-source constant-weight blend.
///
/// Computes `left * (1 - weight) + right * weight` without an implicit clamp.
///
/// @param shape the shared floating shape and domain.
/// @param weight finite right-source weight in `[0, 1]`.
/// @return the two-source program; `ErrorCode::OutOfRange` when the weight is not finite or lies
///         outside its interval; or a shape failure as this header's opening describes.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PointwiseProgram> buildBlendProgram(const PointwisePlanShape& shape,
                                                                              double weight);

/// Builds a two-source comparison whose result is zero or one.
///
/// A not-a-number on either side makes every relation false except `NotEqual`.
///
/// @param shape the shared floating shape and domain.
/// @param comparison relation applied as `left comparison right`.
/// @return the two-source program; `ErrorCode::InvalidArgument` for an unknown relation; or a shape
///         failure as this header's opening describes.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PointwiseProgram> buildComparisonProgram(const PointwisePlanShape& shape,
                                                                                   PointwiseComparison comparison);

/// Builds `value * scale + bias`, optionally followed by a clamp.
///
/// A scale step then a bias step, each rounded at the program's width unless `allowContraction`
/// lets them become one fused multiply-add.
///
/// @param shape the floating shape and domain.
/// @param scale finite multiplier.
/// @param bias finite addend.
/// @param clamp optional finite, ordered inclusive interval.
/// @return the one-source program; `ErrorCode::InvalidArgument` for a scale or bias that is not
///         finite or a clamp whose bounds are not finite and ordered; or a shape failure as this
///         header's opening describes.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PointwiseProgram>
buildScaleBiasProgram(const PointwisePlanShape& shape, double scale, double bias,
                      std::optional<ClampInterval> clamp = std::nullopt);

/// Builds an exposure adjustment, multiplying by two raised to `stops`.
///
/// @param shape the floating shape and domain.
/// @param stops finite exposure stops.
/// @return the one-source scale program; `ErrorCode::OutOfRange` when `stops` is not finite or two
///         raised to it overflows; or a shape failure as this header's opening describes.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PointwiseProgram> buildExposureProgram(const PointwisePlanShape& shape,
                                                                                 double stops);

/// Builds an additive brightness adjustment in the stated numeric domain.
///
/// @param shape the floating shape and domain.
/// @param amount finite value to add.
/// @return the one-source program, a scale by one then this bias; `ErrorCode::InvalidArgument` when
///         `amount` is not finite; or a shape failure as this header's opening describes.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PointwiseProgram> buildBrightnessProgram(const PointwisePlanShape& shape,
                                                                                   double amount);

/// Builds a contrast adjustment around a stated pivot.
///
/// Computes `(value - pivot) * factor + pivot` in its affine form, `value * factor + bias`, with
/// `bias = pivot - pivot * factor` computed once in double. The rounding is therefore that of a scale
/// then a bias at the program's declared width, not that of the written form, and the pivot maps
/// to itself only up to that rounding.
///
/// @param shape the floating shape and domain.
/// @param factor finite contrast multiplier.
/// @param pivot finite value left fixed by the transform.
/// @return the one-source program; `ErrorCode::InvalidArgument` when `factor` or `pivot` is not
///         finite; `ErrorCode::OutOfRange` when the bias overflows; or a shape failure as this
///         header's opening describes.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PointwiseProgram> buildContrastProgram(const PointwisePlanShape& shape,
                                                                                 double factor, double pivot);

/// Builds one inclusive clamp.
///
/// A sample that is not a number passes through unchanged, as the `Clamp` step specifies.
///
/// @param shape the floating shape and domain.
/// @param interval finite bounds with `low <= high`.
/// @return the one-source program; `ErrorCode::InvalidArgument` for bounds that are not finite and
///         ordered; or a shape failure as this header's opening describes.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PointwiseProgram> buildClampProgram(const PointwisePlanShape& shape,
                                                                              const ClampInterval& interval);

/// Builds a rescale and reports how a constant input interval was resolved.
///
/// The ordinary case maps the two input endpoints affinely, with `scale` and `bias` computed once in
/// double, and clamps to the output endpoints when `clampOutput` asks. A constant interval never
/// divides by zero: its named policy produces a constant or identity program, or returns
/// `ErrorCode::EmptySelection` before a program exists. The endpoints are the caller's measurement;
/// this reads no image.
///
/// @param shape the floating shape and domain.
/// @param request input and output intervals, the constant policy and the clamp choice.
/// @return the plan; `ErrorCode::InvalidArgument` for an endpoint that is not finite, an input
///         interval whose low end is above its high end, or an unknown policy;
///         `ErrorCode::EmptySelection` for a constant interval under `ConstantRescalePolicy::Reject`;
///         `ErrorCode::OutOfRange` when the affine map or the constant is not finite; or a shape
///         failure as this header's opening describes.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RescalePlan> buildRescaleProgram(const PointwisePlanShape& shape,
                                                                           const RescaleRequest& request);

} // namespace an
