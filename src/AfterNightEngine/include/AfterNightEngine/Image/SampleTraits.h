// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The same eight representations, spelled as C++ types instead of as values.
///
/// `Image/SampleType.h` answers what a sample is at run time, which is what a file header, a
/// descriptor and a diagnostic need. A typed storage, a typed view and a kernel body need the other
/// spelling: the C++ type one sample actually is, decided at compile time so that the inner loop has
/// no branch in it and no `void*` anywhere near it. This header is the bridge, and it is closed in
/// both directions - eight types, eight enumerators, and nothing that maps outside either set.
///
/// A closed table rather than a trait computed from `<type_traits>` is the point. `std::is_integral`
/// would happily admit `char`, `bool`, `long long` and every extended integer a compiler offers, and
/// each of those would then be a resident image type nobody decided to support, with no conversion
/// row, no kernel coverage and no test. The primary template answering "not one of them" is what
/// makes the eight an allow-list instead of a description.
///
/// The two directions are separate tables, because a specialisation cannot be read backwards, and
/// two tables can disagree. `sampleTraitsAgree` is why they do not: it asks the run-time header the
/// same four questions about each entry, and the static assertions at the foot of this file make a
/// disagreement a compile error in the header that introduced it rather than a wrong allocation size
/// somewhere downstream.
///
/// The third spelling is the run-time crossing: code that holds a `SampleType` value and needs the type.
/// `dispatchSampleType` is the one switch that makes it, for the engine and its consumers alike, and
/// `sampleDispatchAgrees` holds it to the two tables the same way.

#pragma once

#include <AfterNightEngine/Image/SampleType.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace an
{

/// What a C++ type is, as one of this engine's resident representations.
///
/// The primary template is the answer for every type that is not one of the eight, which is why it
/// is defined rather than left incomplete: a caller may ask about any type at all, and "no" is a
/// legitimate answer rather than a compile error.
///
/// @tparam SampleT the type to classify.
template <typename SampleT> struct ResidentSampleTraits
{
    /// Whether this type is one of the eight representations an image may be stored in.
    ///
    /// False here, and true only in the eight specialisations below.
    static constexpr bool kIsResident = false;
};

/// Unsigned 8-bit samples.
template <> struct ResidentSampleTraits<std::uint8_t>
{
    /// This type is one of the eight.
    static constexpr bool kIsResident = true;

    /// The representation it is.
    static constexpr SampleType kSampleType = SampleType::UInt8;
};

/// Signed 8-bit samples.
template <> struct ResidentSampleTraits<std::int8_t>
{
    /// This type is one of the eight.
    static constexpr bool kIsResident = true;

    /// The representation it is.
    static constexpr SampleType kSampleType = SampleType::Int8;
};

/// Unsigned 16-bit samples.
template <> struct ResidentSampleTraits<std::uint16_t>
{
    /// This type is one of the eight.
    static constexpr bool kIsResident = true;

    /// The representation it is.
    static constexpr SampleType kSampleType = SampleType::UInt16;
};

/// Signed 16-bit samples.
template <> struct ResidentSampleTraits<std::int16_t>
{
    /// This type is one of the eight.
    static constexpr bool kIsResident = true;

    /// The representation it is.
    static constexpr SampleType kSampleType = SampleType::Int16;
};

/// Unsigned 32-bit samples.
template <> struct ResidentSampleTraits<std::uint32_t>
{
    /// This type is one of the eight.
    static constexpr bool kIsResident = true;

    /// The representation it is.
    static constexpr SampleType kSampleType = SampleType::UInt32;
};

/// Signed 32-bit samples.
template <> struct ResidentSampleTraits<std::int32_t>
{
    /// This type is one of the eight.
    static constexpr bool kIsResident = true;

    /// The representation it is.
    static constexpr SampleType kSampleType = SampleType::Int32;
};

/// Binary32 floating-point samples.
template <> struct ResidentSampleTraits<float>
{
    /// This type is one of the eight.
    static constexpr bool kIsResident = true;

    /// The representation it is.
    static constexpr SampleType kSampleType = SampleType::Float32;
};

/// Binary64 floating-point samples.
template <> struct ResidentSampleTraits<double>
{
    /// This type is one of the eight.
    static constexpr bool kIsResident = true;

    /// The representation it is.
    static constexpr SampleType kSampleType = SampleType::Float64;
};

/// A type an image's samples may be stored in.
///
/// Exactly the eight, unqualified. A const-qualified type is deliberately not one: storage owns
/// mutable memory, and `ImageStorage<const std::uint16_t>` would be a buffer nothing could ever
/// write, including the code that fills it.
///
/// @tparam SampleT the type to test.
template <typename SampleT>
concept ResidentSample = ResidentSampleTraits<SampleT>::kIsResident;

/// A type a view may present samples as.
///
/// The eight, and the const-qualified form of each. That is what makes `ImageView<const float>` the
/// read-only view of an `ImageView<float>` rather than a second class with a second implementation,
/// and it is why the constness of a view is carried by its sample type rather than by a flag the
/// caller has to remember to check.
///
/// @tparam SampleT the type to test.
template <typename SampleT>
concept ViewableSample = ResidentSample<std::remove_const_t<SampleT>>;

/// Which representation a C++ type is.
///
/// The type-to-value direction, usable wherever a value is wanted - a descriptor being built for a
/// typed buffer, a failure reporting what it was handed, a test naming what it is exercising.
/// Constness is stripped first, so a const view reports the same representation as the mutable one
/// it was made from.
///
/// @tparam SampleT one of the eight, or a const-qualified one of them.
template <ViewableSample SampleT>
inline constexpr SampleType kSampleTypeOf = ResidentSampleTraits<std::remove_const_t<SampleT>>::kSampleType;

/// What a representation is, as a C++ type.
///
/// The value-to-type direction. The primary template is left undefined on purpose: unlike
/// `ResidentSampleTraits`, there is no honest answer for a value outside the enumeration, and a
/// compile error naming this template is a better one than a plausible substitute type.
///
/// @tparam Representation the representation to name a type for.
template <SampleType Representation> struct SampleTypeTraits;

/// The C++ type of an unsigned 8-bit sample.
template <> struct SampleTypeTraits<SampleType::UInt8>
{
    /// The type one sample is.
    using Sample = std::uint8_t;
};

/// The C++ type of a signed 8-bit sample.
template <> struct SampleTypeTraits<SampleType::Int8>
{
    /// The type one sample is.
    using Sample = std::int8_t;
};

/// The C++ type of an unsigned 16-bit sample.
template <> struct SampleTypeTraits<SampleType::UInt16>
{
    /// The type one sample is.
    using Sample = std::uint16_t;
};

/// The C++ type of a signed 16-bit sample.
template <> struct SampleTypeTraits<SampleType::Int16>
{
    /// The type one sample is.
    using Sample = std::int16_t;
};

/// The C++ type of an unsigned 32-bit sample.
template <> struct SampleTypeTraits<SampleType::UInt32>
{
    /// The type one sample is.
    using Sample = std::uint32_t;
};

/// The C++ type of a signed 32-bit sample.
template <> struct SampleTypeTraits<SampleType::Int32>
{
    /// The type one sample is.
    using Sample = std::int32_t;
};

/// The C++ type of a binary32 sample.
template <> struct SampleTypeTraits<SampleType::Float32>
{
    /// The type one sample is.
    using Sample = float;
};

/// The C++ type of a binary64 sample.
template <> struct SampleTypeTraits<SampleType::Float64>
{
    /// The type one sample is.
    using Sample = double;
};

/// The C++ type that holds one sample of a representation.
///
/// The spelling a storage or a kernel writes: `SampleOf<SampleType::UInt16>` is `std::uint16_t`.
///
/// @tparam Representation one of the eight.
template <SampleType Representation> using SampleOf = typename SampleTypeTraits<Representation>::Sample;

/// Whether the two tables and the run-time header say the same thing about one representation.
///
/// Four questions, because four different answers are read out of these tables by code that never
/// sees the other spelling: the round trip through both directions, the size an allocation is
/// computed from, the signedness a saturation bound is derived from, and whether the type is
/// floating, which decides whether a conversion rounds. A table that drifted from
/// `Image/SampleType.h` would be wrong in exactly one of those ways and silent in the other three.
///
/// Consteval, so it is a question about this build rather than a run-time check anything pays for.
///
/// @tparam Representation the representation to verify.
/// @return true when the C++ type for this representation maps back to it, occupies the bytes
///         `sampleTypeSizeBytes` reports, and agrees about signedness and about being floating.
template <SampleType Representation> [[nodiscard]] consteval bool sampleTraitsAgree() noexcept
{
    using Sample = SampleOf<Representation>;
    return kSampleTypeOf<Sample> == Representation && sizeof(Sample) == sampleTypeSizeBytes(Representation) &&
           std::is_signed_v<Sample> == isSignedSampleType(Representation) &&
           std::is_floating_point_v<Sample> == isFloatingSampleType(Representation);
}

static_assert(sampleTraitsAgree<SampleType::UInt8>());
static_assert(sampleTraitsAgree<SampleType::Int8>());
static_assert(sampleTraitsAgree<SampleType::UInt16>());
static_assert(sampleTraitsAgree<SampleType::Int16>());
static_assert(sampleTraitsAgree<SampleType::UInt32>());
static_assert(sampleTraitsAgree<SampleType::Int32>());
static_assert(sampleTraitsAgree<SampleType::Float32>());
static_assert(sampleTraitsAgree<SampleType::Float64>());

/// Runs a body instantiated for the C++ type a run-time representation names.
///
/// The one eight-way switch between this engine's sample values and its sample types, published so that
/// neither the engine's own domains nor a consumer writes a second one. A second spelling is free to fall
/// behind the tables above - to forget a representation, or to map one to a type of the wrong width -
/// and nothing would notice until an image of that type arrived. `sampleDispatchAgrees` below asks this
/// switch the same question `sampleTraitsAgree` asks the tables, so the two cannot drift.
///
/// A template rather than an erased callable, because what it instantiates is usually the innermost loop
/// of a kernel, and an indirect call the compiler cannot see into is what the typed paths exist to avoid.
/// The body is written as a generic lambda with an explicit template parameter, `[&]<typename SampleT>()
/// { ... }`, and receives the unqualified type; what it returns is discarded, so a body that produces a
/// value writes it into something it captured.
///
/// Thread-safe as far as the body is: the body runs on the calling thread, at most once, and nothing else
/// is touched. Usable in a constant expression when the body is, and `noexcept` when every one of the
/// eight instantiations of the body is.
///
/// @tparam BodyT a callable with one type template parameter, invoked as
///         `body.template operator()<SampleT>()`.
/// @param sampleType which representation.
/// @param body what to run.
/// @return true when `sampleType` is one of the eight and the body ran; false, with nothing run, for a
///         value outside the enumeration. Not discardable: a caller that has already checked the value
///         says so with a cast to `void`, and one that has not answers the false.
template <typename BodyT>
[[nodiscard]] constexpr bool dispatchSampleType(SampleType sampleType, BodyT&& body) noexcept(
    noexcept(body.template operator()<std::uint8_t>()) && noexcept(body.template operator()<std::int8_t>()) &&
    noexcept(body.template operator()<std::uint16_t>()) && noexcept(body.template operator()<std::int16_t>()) &&
    noexcept(body.template operator()<std::uint32_t>()) && noexcept(body.template operator()<std::int32_t>()) &&
    noexcept(body.template operator()<float>()) && noexcept(body.template operator()<double>()))
{
    switch (sampleType)
    {
    case SampleType::UInt8:
        body.template operator()<std::uint8_t>();
        return true;
    case SampleType::Int8:
        body.template operator()<std::int8_t>();
        return true;
    case SampleType::UInt16:
        body.template operator()<std::uint16_t>();
        return true;
    case SampleType::Int16:
        body.template operator()<std::int16_t>();
        return true;
    case SampleType::UInt32:
        body.template operator()<std::uint32_t>();
        return true;
    case SampleType::Int32:
        body.template operator()<std::int32_t>();
        return true;
    case SampleType::Float32:
        body.template operator()<float>();
        return true;
    case SampleType::Float64:
        body.template operator()<double>();
        return true;
    }
    return false;
}

/// Whether the dispatch names, for every representation, the type the tables name - and runs nothing for a
/// value outside them.
///
/// The dispatch's half of `sampleTraitsAgree`: a switch that sent `SampleType::Int16` to `std::uint16_t`
/// would compile, allocate correctly and be wrong about every negative sample, and the static assertion
/// below makes that a compile error here instead.
///
/// Consteval, so it is a question about this build rather than a run-time check anything pays for.
///
/// @return true when each of the eight values dispatches exactly once, to the type whose `kSampleTypeOf` is
///         that value, and the first value past the enumeration dispatches to nothing.
[[nodiscard]] consteval bool sampleDispatchAgrees() noexcept
{
    for (std::size_t index = 0u; index <= kSampleTypeCount; ++index)
    {
        const SampleType value = static_cast<SampleType>(index);
        std::size_t calls = 0u;
        bool agrees = true;
        const bool ran = dispatchSampleType(value, [&]<typename SampleT>() noexcept {
            ++calls;
            agrees = agrees && kSampleTypeOf<SampleT> == value;
        });
        const bool known = index < kSampleTypeCount;
        if (ran != known || calls != (known ? 1u : 0u) || !agrees)
        {
            return false;
        }
    }
    return true;
}

static_assert(sampleDispatchAgrees());

} // namespace an
