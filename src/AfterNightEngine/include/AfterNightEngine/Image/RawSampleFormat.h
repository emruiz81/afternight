// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What one sample is at a boundary, including the ones this engine cannot store.
///
/// `Image/SampleType.h` names the eight representations an image may be held in. A boundary meets
/// more than eight: a file format defines a 64-bit integer image, a camera writes twelve bits packed
/// into two bytes, a library hands over a colour index. Those exist, they can be read, and none of
/// them is a resident representation - so a decoder needs a way to say what it has that does not
/// require the processing model to grow a type for it.
///
/// This is that way, and it is deliberately two numbers rather than a longer enumeration. A sample
/// is a number of bytes and a kind of value, and whether the pair happens to be one of the eight is
/// *derived* rather than stated: `residentSampleTypeOf` answers it from a closed table, so a raw
/// format and a sample type can never disagree about the same bytes. Four bytes of signed integer
/// are `SampleType::Int32` and always will be; eight bytes of signed integer are nothing this engine
/// stores, and that is exactly the fact a caller needs before it decides whether a conversion is
/// required or whether the frame can be adopted as it stands.
///
/// `RawSampleClass::Opaque` is the escape for bytes whose numeric meaning this engine does not know.
/// Such a frame can be described, carried, measured and written back out unchanged; what it cannot
/// do is quietly become a processing image, because no conversion exists for a representation nobody
/// specified.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <optional>

namespace an
{

/// What kind of value the bytes of one raw sample hold.
///
/// Coarser than a representation on purpose. It says what an arithmetic reading of the bytes would
/// mean, which together with a byte count is all that is needed to recognise the resident
/// representations and to refuse everything else by name rather than by silence.
enum class RawSampleClass : std::uint8_t
{
    /// A non-negative integer, most significant bit included in the magnitude.
    UnsignedInteger,

    /// A two's-complement signed integer.
    SignedInteger,

    /// An IEEE 754 binary floating-point number of the stated width.
    FloatingPoint,

    /// Bytes whose numeric meaning this engine does not know - a packed sub-byte format carried in
    /// a wider container, a palette index, a compressed tile. They can be carried and written back
    /// unchanged; nothing may compute with them until something says what they mean.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Opaque
};

/// The largest sample this engine will describe at a boundary, in bytes.
///
/// Sixteen covers everything a file format in this domain defines, including a double-precision
/// complex pair carried as one sample, and refuses a byte count that came from a misread header
/// before anything multiplies it by a pixel count.
inline constexpr std::uint32_t kMaxRawSampleSizeBytes = 16u;

/// What one sample is, at a boundary this engine did not choose the representation of.
///
/// A plain aggregate of the two facts a boundary always knows: how wide a sample is, and what kind
/// of number it holds. Both are required, and a size of zero is what an unfilled request looks
/// like, which is why it is refused rather than defaulted.
struct RawSampleFormat
{
    /// How many bytes one sample occupies. A power of two, at least one, at most
    /// `kMaxRawSampleSizeBytes`. Sub-byte formats are described by the container they arrive in,
    /// with `RawSampleClass::Opaque` saying that the container is not the value.
    std::uint32_t sizeBytes;

    /// What kind of value those bytes hold.
    RawSampleClass sampleClass;

    /// Whether two formats describe the same sample.
    ///
    /// @param left one format.
    /// @param right the other.
    /// @return true when both fields are equal.
    [[nodiscard]] friend constexpr bool operator==(const RawSampleFormat& left,
                                                   const RawSampleFormat& right) noexcept = default;
};

/// Whether a value is one of the classes this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param sampleClass the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isRawSampleClass(RawSampleClass sampleClass) noexcept
{
    return static_cast<std::uint8_t>(sampleClass) <= static_cast<std::uint8_t>(RawSampleClass::Opaque);
}

/// Whether a byte count is one a raw sample may occupy.
///
/// Powers of two only, which is what `Core/ImageLayout.h` requires of an element size: stride
/// arithmetic that admitted three-byte samples would have to carry a divisor nothing else in the
/// engine has.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param sizeBytes the width to test.
/// @return true for 1, 2, 4, 8 and 16.
[[nodiscard]] constexpr bool isRawSampleSize(std::uint32_t sizeBytes) noexcept
{
    return sizeBytes >= 1u && sizeBytes <= kMaxRawSampleSizeBytes && (sizeBytes & (sizeBytes - 1u)) == 0u;
}

/// Whether a format describes a sample this engine can carry at a boundary.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param format the format to test.
/// @return true when the class is known and the width is admissible.
[[nodiscard]] constexpr bool isRawSampleFormat(const RawSampleFormat& format) noexcept
{
    return isRawSampleClass(format.sampleClass) && isRawSampleSize(format.sizeBytes);
}

/// The resident representation these bytes already are, when they are one of the eight.
///
/// The closed table that keeps a boundary description and a storage description from disagreeing.
/// Every pair that names a representation this engine stores returns it; every other admissible
/// pair - eight bytes of integer, two bytes of floating point, anything opaque - returns nothing,
/// which is the whole of what "outside the set" means here.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param format the boundary format to classify.
/// @return the resident representation, or an empty optional when the format is not one of them,
///         which includes every format `isRawSampleFormat` refuses.
[[nodiscard]] constexpr std::optional<SampleType> residentSampleTypeOf(const RawSampleFormat& format) noexcept
{
    if (!isRawSampleFormat(format))
    {
        return std::nullopt;
    }
    switch (format.sampleClass)
    {
    case RawSampleClass::UnsignedInteger:
        switch (format.sizeBytes)
        {
        case 1u:
            return SampleType::UInt8;
        case 2u:
            return SampleType::UInt16;
        case 4u:
            return SampleType::UInt32;
        default:
            return std::nullopt;
        }
    case RawSampleClass::SignedInteger:
        switch (format.sizeBytes)
        {
        case 1u:
            return SampleType::Int8;
        case 2u:
            return SampleType::Int16;
        case 4u:
            return SampleType::Int32;
        default:
            return std::nullopt;
        }
    case RawSampleClass::FloatingPoint:
        switch (format.sizeBytes)
        {
        case 4u:
            return SampleType::Float32;
        case 8u:
            return SampleType::Float64;
        default:
            return std::nullopt;
        }
    case RawSampleClass::Opaque:
        return std::nullopt;
    }
    return std::nullopt;
}

/// The boundary format of a resident representation.
///
/// The other direction of the same table, for an encoder that has an image and has to say what it
/// is about to write. `residentSampleTypeOf(rawSampleFormatOf(type))` is `type` for all eight,
/// which the suite asserts rather than assumes.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type the resident representation. A value outside the enumeration yields a format
///        `isRawSampleFormat` refuses, because a size of zero describes no sample.
/// @return the width and class of one sample of it.
[[nodiscard]] constexpr RawSampleFormat rawSampleFormatOf(SampleType type) noexcept
{
    if (!isSampleType(type))
    {
        return RawSampleFormat{.sizeBytes = 0u, .sampleClass = RawSampleClass::Opaque};
    }
    RawSampleClass sampleClass = RawSampleClass::UnsignedInteger;
    if (isFloatingSampleType(type))
    {
        sampleClass = RawSampleClass::FloatingPoint;
    }
    else if (isSignedSampleType(type))
    {
        sampleClass = RawSampleClass::SignedInteger;
    }
    return RawSampleFormat{.sizeBytes = sampleTypeSizeBytes(type), .sampleClass = sampleClass};
}

/// Whether byte order says anything about a sample of this format.
///
/// The same rule `Image/SampleType.h` states for the resident representations, asked of a boundary
/// format: one byte has one order, so reporting an order for it produces two descriptions of
/// identical bytes that then compare unequal.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param format the format to test.
/// @return true when one sample occupies more than one byte.
[[nodiscard]] constexpr bool isRawByteOrderMeaningful(const RawSampleFormat& format) noexcept
{
    return format.sizeBytes > 1u;
}

/// The class's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param sampleClass the class to name. A value outside the enumeration is named
///        `"UnknownRawSampleClass"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* rawSampleClassName(RawSampleClass sampleClass) noexcept;

} // namespace an
