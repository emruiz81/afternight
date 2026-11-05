// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The named decisions a sample conversion makes, none of them left to a cast.
///
/// Converting a sample from one representation to another is five decisions, and a C++ cast silently
/// makes all five badly. It truncates rather than rounding, which biases every conversion downward by
/// half a code value and turns a stacked sequence's noise into a systematic offset. It leaves an
/// out-of-range float undefined, which on x86 becomes the most negative integer and then a narrowing
/// pack of that, which is not the saturation anybody intended. It gives a NaN whatever the hardware
/// gives it - which happens to agree with this engine's rule for an unsigned destination and to be
/// wrong for a signed one, and behaviour that is accidentally right in one case and wrong in the next
/// is worth a few instructions to state. It says nothing about whether the number is a fraction of
/// full scale or a sensor code. And it knows nothing about the offset convention a signed FITS or
/// TIFF boundary carries.
///
/// So each of those is an enumerator here, every one of them has a default that is the approved
/// answer, and a conversion is a policy plus a source and a destination rather than a cast plus a
/// comment.
///
/// The defaults are the ones measured and approved rather than the ones a compiler would give:
///
/// * **Round to nearest, ties to even.** The hardware's own rounding mode, so it is one instruction
///   in a vector path; a real library call in MSVC scalar code, which is why the scalar reference is
///   slower than the compiled backend and why that does not argue against the rule.
/// * **Saturate, clamping the real value before the conversion rather than the integer after it.**
/// * **NaN becomes zero, positive infinity the destination's maximum, negative infinity its
///   minimum** - written down rather than inherited, because the engine's own calibration divide
///   produces NaN by design where a flat holds a zero, and that NaN reaches a save path.
/// * **Integer and floating full scale correspond**: an 8-bit sample divides by 255 and a 16-bit one
///   by 65535, not by 256 and 65536. The consequence worth stating is that the round trip is the
///   identity - every 8- and 16-bit value converted to Float32 and back returns the code value it
///   started as, which is what makes a fused kernel's output bit-identical to an unfused one's.
/// * **Two's complement**, the value as the type holds it. The offset-binary convention exists at
///   file boundaries and is asked for by name there.
///
/// **Counting is unconditional.** Every conversion reports how many non-finite inputs it met and how
/// many values it clamped, always, because "the exported frame has black pixels where the flat was
/// zero" is a support question the engine should be able to answer without a re-run. It costs a
/// compare and an add per sample in a path that already reads and writes one.
///
/// Where an image's validity rule and this policy disagree about what to do with a non-finite sample,
/// the validity rule wins: `NonFinitePolicy::Reject` is how a caller that has consulted
/// `Image/ValidityDescriptor.h` says so.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>

#include <cstddef>
#include <cstdint>

namespace an
{

/// How a real value becomes a whole one.
///
/// Applies only where the destination is an integer representation. A floating destination keeps
/// whatever the source's magnitude rounds to under IEEE arithmetic, which is not a choice this
/// enumeration can make.
enum class RoundingMode : std::uint8_t
{
    /// To the nearest whole value, and to the even one when a value is exactly halfway. The
    /// default, the hardware's own mode, and the only one of the five with no bias at all.
    NearestEven,

    /// To the nearest whole value, and away from zero when a value is exactly halfway. What several
    /// file formats mandate, and what a reader expecting "round half up" means.
    NearestAwayFromZero,

    /// Towards zero, which is what a bare C++ cast does. Offered so that a caller reproducing
    /// another tool's output can ask for it by name; never a default here, because it biases every
    /// conversion of a positive image downward by half a code value.
    TowardZero,

    /// Towards negative infinity.
    Floor,

    /// Towards positive infinity.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Ceiling
};

/// What happens to a value the destination cannot hold.
enum class RangePolicy : std::uint8_t
{
    /// The value becomes the nearest one the destination can hold. The default. The clamp is
    /// applied to the real value before the conversion, never to the whole value after it, because
    /// an out-of-range conversion has already lost the sign by then.
    Saturate,

    /// The conversion refuses, and says which sample it refused at. What a caller that must not
    /// silently lose magnitude asks for.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Reject
};

/// What happens to a source value that is not a finite number.
enum class NonFinitePolicy : std::uint8_t
{
    /// Not-a-number becomes zero, positive infinity becomes the destination's largest value and
    /// negative infinity its smallest. The default, and a stated mapping rather than the
    /// hardware's.
    ///
    /// A floating destination holds all three exactly, so this policy leaves them as they are
    /// there; the mapping is what an integer destination does.
    MapToRange,

    /// The conversion refuses, and says which sample it refused at. What an image whose validity
    /// rule says a non-finite sample means nothing needs, so that the value is not quietly written
    /// out as a black pixel.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Reject
};

/// Whether the numbers stand for a fraction of full scale or for themselves.
///
/// The decision a conversion between an integer and a floating representation cannot avoid, and the
/// one a cast makes without being asked: an unsigned 16-bit sample of 65535 is either 1.0 or
/// 65535.0, and both are right for some images.
enum class DomainMapping : std::uint8_t
{
    /// Full scale corresponds. An integer's largest magnitude is 1.0, so an 8-bit sample divides by
    /// 255 and a 16-bit one by 65535, and two integer representations rescale between their own
    /// maxima. The default, and the mapping under which the integer round trip is the identity.
    FullScale,

    /// The number is carried across as the number it is. What a raw sensor code means, and what an
    /// image whose numeric domain is `NumericDomain::RawSensorCode` needs, because rescaling a
    /// sensor code by a full-scale ratio invents a calibration nobody performed.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    CodeValue
};

/// How a signed value and an unsigned representation of the same width correspond.
///
/// Applies to exactly one kind of conversion: between a signed integer and an unsigned integer of
/// the same width. Every other pair ignores it, because there is no offset convention to apply.
enum class SignedIntegerConvention : std::uint8_t
{
    /// The value as the type holds it, so the conversion saturates like any other. The default.
    TwosComplement,

    /// The unsigned code is the signed value plus half the unsigned range: `65535` unsigned is
    /// `32767` signed, and `0` unsigned is `-32768`. The convention FITS and TIFF carry at signed
    /// integer boundaries, and a bijection - no value of either type is out of the other's range,
    /// so nothing saturates.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    OffsetBinary
};

/// Whether a value is one of the rounding modes this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param rounding the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isRoundingMode(RoundingMode rounding) noexcept
{
    return static_cast<std::uint8_t>(rounding) <= static_cast<std::uint8_t>(RoundingMode::Ceiling);
}

/// Whether a value is one of the range policies this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param range the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isRangePolicy(RangePolicy range) noexcept
{
    return static_cast<std::uint8_t>(range) <= static_cast<std::uint8_t>(RangePolicy::Reject);
}

/// Whether a value is one of the non-finite policies this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param nonFinite the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isNonFinitePolicy(NonFinitePolicy nonFinite) noexcept
{
    return static_cast<std::uint8_t>(nonFinite) <= static_cast<std::uint8_t>(NonFinitePolicy::Reject);
}

/// Whether a value is one of the domain mappings this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param domain the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isDomainMapping(DomainMapping domain) noexcept
{
    return static_cast<std::uint8_t>(domain) <= static_cast<std::uint8_t>(DomainMapping::CodeValue);
}

/// Whether a value is one of the signed conventions this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param convention the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isSignedIntegerConvention(SignedIntegerConvention convention) noexcept
{
    return static_cast<std::uint8_t>(convention) <= static_cast<std::uint8_t>(SignedIntegerConvention::OffsetBinary);
}

/// The rounding mode's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param rounding the mode to name. A value outside the enumeration is named
///        `"UnknownRoundingMode"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* roundingModeName(RoundingMode rounding) noexcept;

/// The range policy's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param range the policy to name. A value outside the enumeration is named
///        `"UnknownRangePolicy"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* rangePolicyName(RangePolicy range) noexcept;

/// The non-finite policy's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param nonFinite the policy to name. A value outside the enumeration is named
///        `"UnknownNonFinitePolicy"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* nonFinitePolicyName(NonFinitePolicy nonFinite) noexcept;

/// The domain mapping's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param domain the mapping to name. A value outside the enumeration is named
///        `"UnknownDomainMapping"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* domainMappingName(DomainMapping domain) noexcept;

/// The signed convention's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param convention the convention to name. A value outside the enumeration is named
///        `"UnknownSignedIntegerConvention"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* signedIntegerConventionName(SignedIntegerConvention convention) noexcept;

/// The five decisions one conversion makes.
///
/// A plain aggregate rather than a validated class, because every field is a choice among named
/// values and the only thing that can be wrong with one is that it is not one of them. There is no
/// combination rule spanning the five, so there is nothing for a private constructor to protect;
/// `checkConversionPolicy` is where a value outside an enumeration is caught, and every entry point
/// that takes a policy calls it.
///
/// Every field carries the approved default, so a call site writes only what it is departing from.
struct ConversionPolicy
{
    /// How a real value becomes a whole one.
    RoundingMode rounding = RoundingMode::NearestEven;

    /// What happens to a value the destination cannot hold.
    RangePolicy range = RangePolicy::Saturate;

    /// What happens to a source value that is not a finite number.
    NonFinitePolicy nonFinite = NonFinitePolicy::MapToRange;

    /// Whether the numbers stand for a fraction of full scale or for themselves.
    DomainMapping domain = DomainMapping::FullScale;

    /// How a signed value and an unsigned representation of the same width correspond.
    SignedIntegerConvention signedConvention = SignedIntegerConvention::TwosComplement;

    /// Two policies are equal when all five decisions are equal.
    friend constexpr bool operator==(const ConversionPolicy& left, const ConversionPolicy& right) noexcept = default;
};

/// Whether every one of a policy's five decisions is a value this build knows.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param policy the policy to check.
/// @param operation the engine operation to name in the failure. May be null, which names none.
/// @return success, or `ErrorCode::InvalidArgument` naming the first field, in declaration order,
///         that is not one of its enumerators.
[[nodiscard]] AFTERNIGHTENGINE_API Status checkConversionPolicy(const ConversionPolicy& policy, const char* operation);

/// The policy an image of this numeric domain should be converted under.
///
/// The one inference this header makes, and it makes it from a fact the image already carries rather
/// than from the samples. A fraction of full scale rescales between representations; a sensor code
/// does not, because rescaling one invents a calibration nobody performed. Every other decision keeps
/// its default, so a caller that needs a different rounding or a refusal states that field and
/// leaves this one alone.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param domain what the image's magnitudes mean. A value outside the enumeration gets the
///        defaults, because a domain this build cannot interpret is not a reason to invent a
///        mapping for it.
/// @return the policy.
[[nodiscard]] constexpr ConversionPolicy conversionPolicyFor(NumericDomain domain) noexcept
{
    ConversionPolicy policy;
    policy.domain = domain == NumericDomain::RawSensorCode ? DomainMapping::CodeValue : DomainMapping::FullScale;
    return policy;
}

/// What a conversion met on its way through, whether or not anything refused.
///
/// Three counters, all of them unconditional. They are not diagnostics a caller turns on: the cost
/// is a compare and an add in a loop that already moves a sample, and the questions they answer -
/// did this frame contain non-finite samples, did this export clip - are asked after the fact, when
/// re-running the conversion to find out is the expensive option.
///
/// A count of how many samples were rounded inexactly is deliberately absent. It needs a comparison
/// against the exact result rather than a flag falling out of the arithmetic, which is a real cost
/// in the inner loop for a number nothing has asked for.
struct ConversionCounters
{
    /// How many samples were written.
    std::uint64_t samplesConverted = 0u;

    /// How many source samples were not finite numbers. Always counted, whether the policy mapped
    /// them or refused.
    std::uint64_t nonFiniteInputs = 0u;

    /// How many source values the destination could not hold. Under `RangePolicy::Saturate` each
    /// was clamped to the nearest value it holds; under `RangePolicy::Reject` the first one is
    /// counted, not written, and the conversion stops there.
    std::uint64_t saturatedSamples = 0u;

    /// Adds another set of counts into this one.
    ///
    /// For a caller that converts in pieces, on one thread or several, and combines the per-piece
    /// counts once the pieces have finished. Saturating rather than wrapping is not needed: every
    /// counter is bounded by the number of samples in an image, which is bounded by the configured
    /// maximum.
    ///
    /// @param other the counts to add.
    constexpr void merge(const ConversionCounters& other) noexcept
    {
        samplesConverted += other.samplesConverted;
        nonFiniteInputs += other.nonFiniteInputs;
        saturatedSamples += other.saturatedSamples;
    }

    /// Two sets of counters are equal when all three counts are equal.
    friend constexpr bool operator==(const ConversionCounters& left,
                                     const ConversionCounters& right) noexcept = default;
};

} // namespace an
