// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Every ordered pair of the eight representations, converted by the rules.
///
/// This is the scalar reference: the complete source-by-destination matrix, written once as a
/// template so that sixty-four pairs are one body rather than sixty-four, and written to be *right*
/// rather than fast. It is the oracle a compiled backend is compared against, which is what makes it
/// worth having at all: a conformance run that has nothing to compare against is a run that proves a
/// kernel agrees with itself.
///
/// **Everything goes through `double`.** Not because the arithmetic needs the width but because
/// `double` represents every value of all six integer representations exactly, so the scaling and
/// the clamp cannot themselves introduce the error they are there to control. The same conversion in
/// `float` would multiply by `float(UINT32_MAX)`, which is `4294967296.0f` - one past the largest
/// value the destination holds - so converting full scale would be undefined behaviour.
///
/// **The order is clamp, then round.** Both orders are defensible and they differ nowhere except at
/// the boundary, and the boundary is where an out-of-range float on x86 becomes the most negative
/// integer. Clamping the real value first means the rounding never sees a number the destination
/// cannot hold.
///
/// **A conversion says what it met.** Whether a source sample was not finite and whether a value was
/// clamped are counted for every run, unconditionally. That is a decision about what an engine should
/// be able to answer afterwards rather than a debugging aid: a calibration divide is *required* to
/// produce NaN where a flat holds a zero, so NaN reaches an export by design, and "why are there
/// black pixels there" should not need the conversion run again to answer.
///
/// What is not here is a view, a stride, a thread or a backend. A run of samples in, a run of samples
/// out, and nothing that knows what an image is - so this header can be included by a kernel, by the
/// compiled backend it is the oracle for, and by a test, none of which want each other's
/// dependencies. `Image/ImageConversion.h` is where the same rules are applied to whole images.

#pragma once

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace an
{

/// The largest value a representation holds, as a real number.
///
/// A `double` rather than the type's own value, because that is what the conversion arithmetic works
/// in and because every one of the six integer maxima is exactly representable there. The floating
/// representations report their own largest finite magnitude.
///
/// @tparam SampleT one of the eight resident sample types.
template <ResidentSample SampleT>
inline constexpr double kSampleMaximumOf = static_cast<double>((std::numeric_limits<SampleT>::max)());

/// The smallest value a representation holds, as a real number.
///
/// The floating representations report the negation of their largest finite magnitude rather than
/// `std::numeric_limits::min`, which is the smallest *positive* value and would make every clamp
/// wrong in a way that only shows near zero.
///
/// @tparam SampleT one of the eight resident sample types.
template <ResidentSample SampleT>
inline constexpr double kSampleMinimumOf =
    std::is_floating_point_v<SampleT> ? -static_cast<double>((std::numeric_limits<SampleT>::max)())
                                      : static_cast<double>((std::numeric_limits<SampleT>::lowest)());

/// Whether the offset-binary convention has anything to say about this ordered pair.
///
/// It applies to exactly one shape: a signed integer and an unsigned integer of the same width. That
/// is the pair a FITS or TIFF signed-integer boundary produces, and it is a bijection, so nothing
/// about it saturates or rounds. Every other pair ignores the convention entirely.
///
/// @tparam SourceT the representation converted from.
/// @tparam DestT the representation converted to.
template <ResidentSample SourceT, ResidentSample DestT>
inline constexpr bool kIsOffsetBinaryPair =
    std::is_integral_v<SourceT> && std::is_integral_v<DestT> && sizeof(SourceT) == sizeof(DestT) &&
    std::is_signed_v<SourceT> != std::is_signed_v<DestT>;

/// One converted sample, and what the conversion met producing it.
///
/// The two flags are returned rather than counted here because one sample knows nothing about a run:
/// a caller converting a row accumulates them, and a caller converting one sample reads them
/// directly. Neither of them is a failure - what a policy does about them is the run's decision.
///
/// @tparam DestT the representation converted to.
template <ResidentSample DestT> struct ConvertedSample
{
    /// The sample, always produced, whatever the flags say. A rejecting policy discards it; a
    /// mapping policy writes it.
    DestT value;

    /// Whether the source sample was not a finite number.
    bool sourceWasNonFinite;

    /// Whether the value the destination received is the nearest one it can hold rather than the
    /// one the source named.
    bool saturated;
};

/// Whether a real value is a whole one, rounded as a policy says.
///
/// Written out rather than called from the standard library because none of `std::nearbyint`,
/// `std::floor` or `std::ceil` is usable in a constant expression, and because the halfway cases are
/// the ones a rounding policy exists to decide - inheriting them from whatever a platform library
/// does is how two hosts produce two different exports of one frame.
///
/// The value must already be within a range whose magnitude fits a 64-bit integer, which the clamp
/// before it guarantees for every destination this engine has.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param value the real value to round.
/// @param rounding which way. A value outside the enumeration truncates, which is the answer that
///        adds nothing.
/// @return the rounded value, still as a real number.
[[nodiscard]] constexpr double roundReal(double value, RoundingMode rounding) noexcept
{
    const std::int64_t truncatedWhole = static_cast<std::int64_t>(value);
    const double truncated = static_cast<double>(truncatedWhole);
    const double fraction = value - truncated;
    switch (rounding)
    {
    case RoundingMode::NearestEven:
        if (fraction > 0.5 || (fraction == 0.5 && (truncatedWhole % 2) != 0))
        {
            return truncated + 1.0;
        }
        if (fraction < -0.5 || (fraction == -0.5 && (truncatedWhole % 2) != 0))
        {
            return truncated - 1.0;
        }
        return truncated;
    case RoundingMode::NearestAwayFromZero:
        if (fraction >= 0.5)
        {
            return truncated + 1.0;
        }
        if (fraction <= -0.5)
        {
            return truncated - 1.0;
        }
        return truncated;
    case RoundingMode::TowardZero:
        return truncated;
    case RoundingMode::Floor:
        return fraction < 0.0 ? truncated - 1.0 : truncated;
    case RoundingMode::Ceiling:
        return fraction > 0.0 ? truncated + 1.0 : truncated;
    }
    return truncated;
}

/// One integer re-encoded as the other signedness of the same width.
///
/// The offset-binary bijection: the unsigned code is the signed value plus half the unsigned range.
/// The addition happens in a 64-bit intermediate, never in the source type, where it would overflow
/// for exactly the values it exists to represent.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @tparam SourceT the integer representation converted from.
/// @tparam DestT the integer representation converted to, of the same width and other signedness.
/// @param value the sample to re-encode.
/// @return the re-encoded sample. Every value of either type has one, so nothing is lost.
template <ResidentSample SourceT, ResidentSample DestT>
    requires(kIsOffsetBinaryPair<SourceT, DestT>)
[[nodiscard]] constexpr DestT offsetBinaryValue(SourceT value) noexcept
{
    constexpr std::int64_t kHalfRange = static_cast<std::int64_t>(1) << (sizeof(DestT) * 8u - 1u);
    if constexpr (std::is_signed_v<SourceT>)
    {
        return static_cast<DestT>(static_cast<std::uint64_t>(static_cast<std::int64_t>(value) + kHalfRange));
    }
    else
    {
        return static_cast<DestT>(static_cast<std::int64_t>(value) - kHalfRange);
    }
}

/// Whether a sample of a floating representation is a finite number.
///
/// Written as two comparisons rather than as `std::isfinite`, which is not usable in a constant
/// expression: not-a-number fails both because every comparison against it is false, and both
/// infinities fail one of them.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @tparam SampleT one of the two floating representations.
/// @param value the sample to test.
/// @return true when the value is finite.
template <ResidentSample SampleT>
    requires(std::is_floating_point_v<SampleT>)
[[nodiscard]] constexpr bool sampleIsFinite(SampleT value) noexcept
{
    return value >= -kSampleMaximumOf<SampleT> && value <= kSampleMaximumOf<SampleT>;
}

/// One sample of one representation, as a sample of another.
///
/// The whole matrix, in one body. The order of the decisions is the contract, and it is the order
/// this engine's conversion policy was written in:
///
/// 1. an identical pair is carried across untouched, whatever the policy says, because a conversion
///    that is not one should not be able to change a value;
/// 2. the offset-binary convention, where it applies, is a bijection and is applied whole - no
///    scaling, no clamp, no rounding, because none of the three has anything to do at a re-encoding
///    of the same code;
/// 3. a source sample that is not finite takes the stated mapping, which for an integer destination
///    is zero for not-a-number and the destination's extremes for the infinities, and for a floating
///    destination is the value itself, which that destination holds exactly;
/// 4. the value is scaled if the domain mapping says the two representations' full scales
///    correspond, and carried as itself if it says the number stands for itself;
/// 5. it is clamped to what the destination can hold, as a real value and before any rounding; and
/// 6. it is rounded, if the destination is an integer.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @tparam SourceT the representation converted from.
/// @tparam DestT the representation converted to.
/// @param value the sample to convert.
/// @param policy the five decisions. A policy carrying a value outside an enumeration behaves as
///        its default would for that decision, except the rounding, which truncates toward zero
///        rather than rounding to nearest-even; `checkConversionPolicy` is what refuses one, and
///        every entry point that takes a run of samples calls it first.
/// @return the converted sample, and whether the source was non-finite and whether the value was
///         clamped.
template <ResidentSample SourceT, ResidentSample DestT>
[[nodiscard]] constexpr ConvertedSample<DestT> convertSample(SourceT value, const ConversionPolicy& policy) noexcept
{
    constexpr bool kSourceIsFloating = std::is_floating_point_v<SourceT>;
    constexpr bool kDestIsFloating = std::is_floating_point_v<DestT>;

    if constexpr (std::is_same_v<SourceT, DestT>)
    {
        bool nonFinite = false;
        if constexpr (kSourceIsFloating)
        {
            nonFinite = !sampleIsFinite(value);
        }
        return ConvertedSample<DestT>{.value = value, .sourceWasNonFinite = nonFinite, .saturated = false};
    }
    else
    {
        if constexpr (kIsOffsetBinaryPair<SourceT, DestT>)
        {
            if (policy.signedConvention == SignedIntegerConvention::OffsetBinary)
            {
                return ConvertedSample<DestT>{
                    .value = offsetBinaryValue<SourceT, DestT>(value), .sourceWasNonFinite = false, .saturated = false};
            }
        }

        if constexpr (kSourceIsFloating)
        {
            if (!sampleIsFinite(value))
            {
                if constexpr (kDestIsFloating)
                {
                    // Named rather than cast. The other floating representation holds all three
                    // exactly, so a cast would be correct - and a compiler that has constant-folded
                    // its way here reports the cast of a finite constant it can prove is out of
                    // range, which is a warning about a branch that never runs on that value.
                    DestT carried = std::numeric_limits<DestT>::quiet_NaN();
                    if (value == value)
                    {
                        carried = value > SourceT{0} ? std::numeric_limits<DestT>::infinity()
                                                     : -std::numeric_limits<DestT>::infinity();
                    }
                    return ConvertedSample<DestT>{.value = carried, .sourceWasNonFinite = true, .saturated = false};
                }
                else
                {
                    // Not-a-number is the one of the three with no direction, so it takes the value
                    // an unwritten sample would have. The infinities take the extremes they are the
                    // limit of, which for an unsigned destination makes negative infinity zero.
                    DestT mapped = static_cast<DestT>(0);
                    if (value == value)
                    {
                        mapped = value > SourceT{0} ? (std::numeric_limits<DestT>::max)()
                                                    : (std::numeric_limits<DestT>::lowest)();
                    }
                    return ConvertedSample<DestT>{.value = mapped, .sourceWasNonFinite = true, .saturated = false};
                }
            }
        }

        double real = static_cast<double>(value);
        if (policy.domain != DomainMapping::CodeValue)
        {
            if constexpr (!kSourceIsFloating && kDestIsFloating)
            {
                real /= kSampleMaximumOf<SourceT>;
            }
            else if constexpr (kSourceIsFloating && !kDestIsFloating)
            {
                real *= kSampleMaximumOf<DestT>;
            }
            else if constexpr (!kSourceIsFloating && !kDestIsFloating)
            {
                real = real * kSampleMaximumOf<DestT> / kSampleMaximumOf<SourceT>;
            }
        }

        bool saturated = false;
        if (real < kSampleMinimumOf<DestT>)
        {
            real = kSampleMinimumOf<DestT>;
            saturated = true;
        }
        else if (real > kSampleMaximumOf<DestT>)
        {
            real = kSampleMaximumOf<DestT>;
            saturated = true;
        }

        if constexpr (!kDestIsFloating)
        {
            real = roundReal(real, policy.rounding);
        }
        return ConvertedSample<DestT>{
            .value = static_cast<DestT>(real), .sourceWasNonFinite = false, .saturated = saturated};
    }
}

/// Converts one run of samples, and says what it met.
///
/// The reference walk, sample by sample, with no assumption about where the runs came from. Both
/// runs must be the same length; neither may overlap the other, which is the caller's to guarantee
/// because two spans cannot be asked whether they do without leaving a constant expression.
///
/// A rejecting policy stops at the first sample it refuses. Everything before it has been written
/// and the failure says how many, so a caller can report where in a frame the refusal happened
/// rather than only that it did.
///
/// Thread-safe with respect to anything but the destination and the counters. Allocates only a
/// failure it describes.
///
/// @tparam SourceT the representation converted from.
/// @tparam DestT the representation converted to.
/// @param source the samples to convert.
/// @param destination where they go. Must be exactly as long as `source`.
/// @param policy the five decisions.
/// @param counters what the run met, added to whatever the counters already hold - so a caller
///        walking an image row by row passes the same counters to every row.
/// @return success; `ErrorCode::InvalidArgument` when the runs are different lengths or the policy
///         names a decision this build does not know; `ErrorCode::InvalidData` when a rejecting
///         non-finite policy met a non-finite sample; or `ErrorCode::OutOfRange` when a rejecting
///         range policy met a value the destination cannot hold. Both refusals carry how many
///         samples were written before them.
template <ResidentSample SourceT, ResidentSample DestT>
[[nodiscard]] Status convertSampleRun(std::span<const SourceT> source, std::span<DestT> destination,
                                      const ConversionPolicy& policy, ConversionCounters& counters)
{
    if (source.size() != destination.size())
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{.message = "a conversion writes one sample for each it reads, and these runs differ",
                         .operation = "an::convertSampleRun",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, source.size()),
                                     StatusContextField::ofCount(ContextKey::Available, destination.size())}});
    }
    const Status decided = checkConversionPolicy(policy, "an::convertSampleRun");
    if (decided.failed())
    {
        return decided;
    }

    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        const ConvertedSample<DestT> converted = convertSample<SourceT, DestT>(source[index], policy);
        if (converted.sourceWasNonFinite)
        {
            ++counters.nonFiniteInputs;
            if (policy.nonFinite == NonFinitePolicy::Reject)
            {
                return Status::failure(
                    ErrorCode::InvalidData,
                    StatusDetail{.message = "a source sample is not a finite number and this conversion refuses one",
                                 .operation = "an::convertSampleRun",
                                 .domain = ErrorDomain::Image,
                                 .context = {StatusContextField::ofCount(ContextKey::Completed, index),
                                             StatusContextField::ofCount(ContextKey::Total, source.size())}});
            }
        }
        if (converted.saturated)
        {
            ++counters.saturatedSamples;
            if (policy.range == RangePolicy::Reject)
            {
                return Status::failure(
                    ErrorCode::OutOfRange,
                    StatusDetail{.message = "a source sample is outside what the destination holds and this "
                                            "conversion refuses one",
                                 .operation = "an::convertSampleRun",
                                 .domain = ErrorDomain::Image,
                                 .context = {StatusContextField::ofCount(ContextKey::Completed, index),
                                             StatusContextField::ofCount(ContextKey::Total, source.size())}});
            }
        }
        destination[index] = converted.value;
        ++counters.samplesConverted;
    }
    return Status::success();
}

} // namespace an
