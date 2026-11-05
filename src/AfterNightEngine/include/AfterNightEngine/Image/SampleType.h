// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What one sample of an image is, as a fact about bytes rather than meaning.
///
/// Eight representations, and no ninth. That number is a decision rather than an omission: every
/// representation multiplies the conversion matrix, which has to be complete and exhaustively tested
/// at extrema, adjacent values, half-way values, out-of-range values, odd tails and noncanonical
/// strides, and it multiplies every storage-generic kernel besides. Sixty-four-bit integer files
/// exist and are decoded into `Float64` with an exactness check before the conversion, rather than
/// admitted here as a ninth and tenth type; a representation outside the eight is carried at the
/// boundary as bytes and a descriptor and refused by the processing model until a conversion for it
/// exists.
///
/// What a sample *means* is not here. Whether the number is a fraction of full scale, a sensor code
/// or a physical quantity is the numeric domain's question, and whether it is red is the channel
/// schema's. This header answers only how many bytes the sample takes and what kind of number is in
/// them - which is what an allocation size, a saturation bound and a byte-order swap need, and all
/// three are wrong in silence if they are answered anywhere else as well.

#pragma once

#include <AfterNightEngine/Core/Api.h>

#include <bit>
#include <cstddef>
#include <cstdint>

namespace an
{

/// How one sample of an image is represented in memory.
///
/// These eight are the resident representations: what an `Image` may store and what the conversion
/// matrix maps between. Storage covering all eight does not mean every algorithm accepts all eight -
/// each operation declares the combinations it supports - and it does not mean every file format's
/// representation is one of them.
enum class SampleType : std::uint8_t
{
    /// Unsigned 8-bit integer. Display data, masks, and the smallest camera output.
    UInt8,

    /// Signed 8-bit integer. Rare as acquisition data; reachable through file formats that define
    /// it and through difference images.
    Int8,

    /// Unsigned 16-bit integer. The representation most astronomical cameras produce and the one
    /// most of this engine's real data arrives in.
    UInt16,

    /// Signed 16-bit integer. `BITPIX = 16` in FITS is signed, so this is what a file says before
    /// any offset convention is applied to it.
    Int16,

    /// Unsigned 32-bit integer. High-bit-depth intermediates and accumulations that must stay
    /// exact.
    UInt32,

    /// Signed 32-bit integer. `BITPIX = 32` in FITS, and difference images that keep full integer
    /// range.
    Int32,

    /// Binary32 floating point. The default arithmetic type for the 8- and 16-bit integer
    /// representations and for itself, and the working representation of most of the engine.
    Float32,

    /// Binary64 floating point. Stored where a file holds it, and used where an algorithm's output
    /// contract needs more than 24 bits of significand.
    ///
    /// Deliberately the last enumerator; `kSampleTypeCount` is derived from it.
    Float64
};

/// How many representations `SampleType` holds.
///
/// Derived from the last enumerator rather than written out, so it cannot drift from the
/// enumeration while `Float64` stays last. It exists so a table indexed by representation - a
/// conversion matrix row, a per-type counter - can be sized without its author restating the
/// enumeration.
inline constexpr std::size_t kSampleTypeCount = static_cast<std::size_t>(SampleType::Float64) + 1u;

/// Whether a value is one of the representations this build knows.
///
/// The question to ask about a sample type that came from a file header or from a caller compiled
/// against a different build of this library, before anything derives a size or a bound from it.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isSampleType(SampleType type) noexcept
{
    return static_cast<std::uint8_t>(type) <= static_cast<std::uint8_t>(SampleType::Float64);
}

/// How many bytes one sample of this representation occupies.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type the representation to measure.
/// @return 1, 2, 4 or 8; or 0 for a value outside the enumeration, which is a size no
///         representation has and which `ImageLayout` refuses, so an unknown type cannot become a
///         plausible allocation.
[[nodiscard]] constexpr std::uint32_t sampleTypeSizeBytes(SampleType type) noexcept
{
    switch (type)
    {
    case SampleType::UInt8:
    case SampleType::Int8:
        return 1u;
    case SampleType::UInt16:
    case SampleType::Int16:
        return 2u;
    case SampleType::UInt32:
    case SampleType::Int32:
    case SampleType::Float32:
        return 4u;
    case SampleType::Float64:
        return 8u;
    }
    return 0u;
}

/// Whether this representation holds a floating-point number.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type the representation to classify.
/// @return true for `Float32` and `Float64`; false for every integer representation and for a value
///         outside the enumeration.
[[nodiscard]] constexpr bool isFloatingSampleType(SampleType type) noexcept
{
    return type == SampleType::Float32 || type == SampleType::Float64;
}

/// Whether this representation holds an integer.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type the representation to classify.
/// @return true for the six integer representations; false for the floating ones and for a value
///         outside the enumeration, so that this and `isFloatingSampleType` partition the
///         enumeration and agree that an unknown value is neither.
[[nodiscard]] constexpr bool isIntegerSampleType(SampleType type) noexcept
{
    return isSampleType(type) && !isFloatingSampleType(type);
}

/// Whether this representation can hold a negative number.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type the representation to classify. The floating representations answer true: a
///        conversion that treats `Float32` as unsigned because it is not an `Int` is the mistake
///        this question exists to prevent.
/// @return true for the signed integer and the floating representations; false for the unsigned
///         integer ones and for a value outside the enumeration.
[[nodiscard]] constexpr bool isSignedSampleType(SampleType type) noexcept
{
    switch (type)
    {
    case SampleType::Int8:
    case SampleType::Int16:
    case SampleType::Int32:
    case SampleType::Float32:
    case SampleType::Float64:
        return true;
    case SampleType::UInt8:
    case SampleType::UInt16:
    case SampleType::UInt32:
        return false;
    }
    return false;
}

/// In which order the bytes of one sample are stored.
///
/// A resident image is always in the host's order, so this is a boundary fact: what a file holds,
/// what a decoder handed over, what an encoder must write. It is stated here beside the
/// representation it qualifies, because the two are one description of a sample and splitting them
/// across two headers is how a boundary ends up describing its bytes twice.
enum class ByteOrder : std::uint8_t
{
    /// Whatever this host uses. The order of every resident image, and the only order in which a
    /// sample may be read as its C++ type without conversion.
    Native,

    /// Least significant byte first, on every host, whatever order the host itself uses.
    Little,

    /// Most significant byte first, on every host, whatever order the host itself uses. The order
    /// FITS defines, so an unconverted FITS payload is in it on every host this engine runs on.
    Big
};

/// Whether a value is one of the orders this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param order the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isByteOrder(ByteOrder order) noexcept
{
    return static_cast<std::uint8_t>(order) <= static_cast<std::uint8_t>(ByteOrder::Big);
}

/// Which of the two stated orders this host actually uses.
///
/// The question a boundary asks to decide whether bytes need swapping at all: a payload whose order
/// equals this one is already native.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @return `ByteOrder::Little` or `ByteOrder::Big`; or `ByteOrder::Native` on a host that is
///         neither, which is a platform this engine has never been built for and where there is no
///         more specific answer to give.
[[nodiscard]] constexpr ByteOrder nativeByteOrder() noexcept
{
    if constexpr (std::endian::native == std::endian::little)
    {
        return ByteOrder::Little;
    }
    else if constexpr (std::endian::native == std::endian::big)
    {
        return ByteOrder::Big;
    }
    else
    {
        return ByteOrder::Native;
    }
}

/// Whether byte order says anything about a sample of this representation.
///
/// One byte has one order. Asking a boundary to swap single-byte samples is harmless and asking it
/// to *report* an order for them is not: it produces two descriptions of identical bytes, which
/// then compare unequal.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type the representation to test.
/// @return true when one sample occupies more than one byte; false for the 8-bit representations
///         and for a value outside the enumeration.
[[nodiscard]] constexpr bool isByteOrderMeaningful(SampleType type) noexcept
{
    return sampleTypeSizeBytes(type) > 1u;
}

/// The representation's own name, for a log line, a diagnostic field or the descriptor's text form.
///
/// The spelling of the enumerator: `SampleType::Float32` is `"Float32"`. Stable, and safe to write
/// into a machine-read field; a caller still branches on the representation itself.
///
/// Thread-safe and allocation-free.
///
/// @param type the representation to name. A value outside the enumeration is named
///        `"UnknownSampleType"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* sampleTypeName(SampleType type) noexcept;

/// The order's own name, for a log line, a diagnostic field or the descriptor's text form.
///
/// Thread-safe and allocation-free.
///
/// @param order the order to name. A value outside the enumeration is named `"UnknownByteOrder"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* byteOrderName(ByteOrder order) noexcept;

} // namespace an
