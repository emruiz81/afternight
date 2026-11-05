// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How an image is to be written, decided rather than inherited.
///
/// A FITS write that copies the source header forward makes a frame written after processing carry
/// its input's artifact identity. The general form of that defect is a *source* fact masquerading as
/// an *output* decision, and the general repair is this type: the destination representation, the
/// scaling, the rounding, the compression and the cards to write all come from one object that
/// nothing produces by accident.
///
/// **An intent states a decision; it converts nothing.** `encodeImage` in `IO/CodecIO.h` requires
/// the frame it is handed to hold the intent's sample type and byte order already, and refuses
/// `OutputScaling::NormalizeFullRange`, which is a data-dependent normalization the caller applies
/// before encoding. Only the FITS encoder takes an explicit scale and offset, which it writes as the
/// header's own scaling; the other encoders refuse any scaling but `OutputScaling::None`. No encoder
/// reads `rounding()`: rounding is part of the decision this object records, and applying it is the
/// work of the conversion that produced the frame.
///
/// **Absent is the default.** An image being processed has no output intent, because nothing has
/// decided how to write it. Carrying a source's encoding forward is a perfectly reasonable policy -
/// it is often exactly what a user wants - but it is then a policy that *produced* an intent, not an
/// accident of which bag a value happened to sit in.
///
/// **The card set is complete before the write begins.** FITS reserves header space ahead of the
/// pixels, because a keyword added afterwards makes the library insert a header block and relocate
/// the entire payload. So a codec computes every card it will write, reserves for them, and then
/// writes pixels; a card discovered late is a defect rather than a slow path. That is a constraint on
/// *when* output metadata is decided, and it is why the cards live in this object rather than being
/// collected as the write proceeds.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace an
{

/// How image values are mapped onto the numbers a file stores.
enum class OutputScaling : std::uint8_t
{
    /// The stored value is the image value. Every representation change is a plain conversion.
    None,

    /// The stored value is mapped through the scale and offset this intent carries: the image value
    /// is the stored value multiplied by the scale and then offset. Written that way round because
    /// it is the direction a reader applies, so a file and this object state the same relation.
    Explicit,

    /// The image's full numeric range is mapped onto the destination representation's full range.
    /// What a floating image written to sixteen bits usually wants, and never what a physically
    /// scaled one does. The mapping depends on the data, so the caller applies it before encoding:
    /// `encodeImage` refuses an intent that asks for it with `ErrorCode::IncompatibleArguments`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    NormalizeFullRange
};

/// How a value that falls between two storable numbers is resolved.
///
/// Stated by the intent rather than chosen by whichever conversion runs, because rounding is
/// visible in the output format and invisible before it: a rewrite that is two units in the last
/// place in floating point can be a whole code value after quantization.
enum class OutputRounding : std::uint8_t
{
    /// To the nearest storable value, halves away from zero.
    Nearest,

    /// Toward zero.
    TowardZero,

    /// Toward negative infinity.
    Floor,

    /// Toward positive infinity.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Ceiling
};

/// How the stored bytes are compressed.
enum class OutputCompression : std::uint8_t
{
    /// Not compressed.
    None,

    /// Rice tile compression, as FITS defines it.
    Rice,

    /// Deflate applied per tile, as FITS defines it.
    Gzip,

    /// Run-length compression for masks and integer maps, as FITS defines it.
    Plio,

    /// Deflate applied to the whole payload, as the container defines it.
    Deflate,

    /// Lempel-Ziv-Welch, as TIFF defines it.
    Lzw,

    /// The JPEG transform, as the destination container defines it. Lossy, and the only compression
    /// a JPEG file has.
    Jpeg,

    /// LZ4, as the container defines it: the fastest to write and to read.
    Lz4,

    /// LZ4 written by its high-compression encoder: smaller than `Lz4`, slower to write, as fast to read.
    Lz4Hc,

    /// Zstandard, as the container defines it.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Zstd
};

/// The checksum a file states for the stored bytes of its pixel data, which a reader verifies before it uses them.
enum class OutputChecksum : std::uint8_t
{
    /// None: nothing is verified.
    None,

    /// SHA-1.
    Sha1,

    /// SHA-256.
    Sha256,

    /// SHA-512.
    Sha512,

    /// SHA3-256, which a reader of the container may not implement.
    Sha3Digest256,

    /// SHA3-512, which a reader of the container may not implement.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Sha3Digest512
};

/// Whether a value is one of the scalings this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param scaling the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isOutputScaling(OutputScaling scaling) noexcept
{
    return static_cast<std::uint8_t>(scaling) <= static_cast<std::uint8_t>(OutputScaling::NormalizeFullRange);
}

/// Whether a value is one of the rounding rules this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param rounding the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isOutputRounding(OutputRounding rounding) noexcept
{
    return static_cast<std::uint8_t>(rounding) <= static_cast<std::uint8_t>(OutputRounding::Ceiling);
}

/// Whether a value is one of the compressions this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param compression the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isOutputCompression(OutputCompression compression) noexcept
{
    return static_cast<std::uint8_t>(compression) <= static_cast<std::uint8_t>(OutputCompression::Zstd);
}

/// Whether a value is one of the checksums this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param checksum the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isOutputChecksum(OutputChecksum checksum) noexcept
{
    return static_cast<std::uint8_t>(checksum) <= static_cast<std::uint8_t>(OutputChecksum::Sha3Digest512);
}

/// The scaling's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param scaling the scaling to name. A value outside the enumeration is named
///        `"UnknownOutputScaling"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* outputScalingName(OutputScaling scaling) noexcept;

/// The rounding rule's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param rounding the rule to name. A value outside the enumeration is named
///        `"UnknownOutputRounding"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* outputRoundingName(OutputRounding rounding) noexcept;

/// The compression's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param compression the compression to name. A value outside the enumeration is named
///        `"UnknownOutputCompression"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* outputCompressionName(OutputCompression compression) noexcept;

/// The checksum's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param checksum the checksum to name. A value outside the enumeration is named
///        `"UnknownOutputChecksum"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* outputChecksumName(OutputChecksum checksum) noexcept;

/// What a caller decides about writing an image, before any of it has been checked.
///
/// A plain aggregate with defaults for everything except the destination representation, which has
/// none: choosing what a file holds is the decision this object exists to make somebody take.
struct OutputIntentRequest
{
    /// What one stored sample is to be. One of the eight, because the engine writes what it can
    /// produce; a foreign representation is written by copying a raw frame through unchanged, which
    /// is a different path and does not go through an intent.
    SampleType sampleType;

    /// In which order the file is to hold the bytes of one sample. `ByteOrder::Native` asks for the
    /// host's, and is the only admissible value for a one-byte representation.
    ByteOrder byteOrder = ByteOrder::Native;

    /// How image values are mapped onto stored numbers.
    OutputScaling scaling = OutputScaling::None;

    /// The multiplier a reader applies to stored values. Required when the scaling is
    /// `OutputScaling::Explicit`, refused otherwise, and never zero.
    std::optional<double> scale{};

    /// The offset a reader adds after the multiplier. Required when the scaling is
    /// `OutputScaling::Explicit`, refused otherwise.
    std::optional<double> zeroOffset{};

    /// How a value between two storable numbers is resolved. Must be `OutputRounding::Nearest` for
    /// a floating destination, which does not quantize in the sense the other rules describe.
    OutputRounding rounding = OutputRounding::Nearest;

    /// How the stored bytes are compressed.
    OutputCompression compression = OutputCompression::None;

    /// How hard, or how lossily, the compression is to work, in that compression's own units: the
    /// level of `OutputCompression::Deflate` from zero to nine, of `OutputCompression::Lz4Hc` from
    /// one to twelve and of `OutputCompression::Zstd` from one to twenty-two, and the quality of
    /// `OutputCompression::Jpeg` from one to a hundred. No other compression takes one, the
    /// per-tile `Gzip` and `OutputCompression::Lz4` included. Absent asks for the destination
    /// codec's documented default, which is what a caller who has no opinion states. Refused beside a
    /// compression that takes no level, for the reason a scale beside `OutputScaling::None` is
    /// refused: a number carried there is one something will eventually apply.
    std::optional<std::uint32_t> compressionLevel{};

    /// Whether the bytes of the samples are shuffled before they are compressed - every sample's
    /// first byte, then every sample's second, and so on - which makes the codecs pay on samples of
    /// more than one byte. Absent asks for the destination codec's default; a container that has no
    /// such step refuses either value. Refused beside `OutputCompression::None`: there is nothing to
    /// shuffle for.
    std::optional<bool> byteShuffle{};

    /// The largest piece of the pixel data compressed on its own, in bytes before compression, which
    /// a reader decompresses on its own too: a region read decodes only the pieces that hold it.
    /// Absent asks for the destination codec's default; a container that divides its data into no
    /// such pieces refuses any value. Positive, and refused beside `OutputCompression::None`.
    std::optional<std::uint64_t> compressionBlockBytes{};

    /// The checksum the file states for the stored bytes of its pixel data. A container that states
    /// none refuses any other than `OutputChecksum::None`.
    OutputChecksum checksum = OutputChecksum::None;

    /// Whether the cards a source file carried are written back out. The cards this intent states
    /// take precedence over a preserved card with the same keyword, which is what "an output intent
    /// overrides" means.
    bool preserveSourceCards = true;

    /// The cards this write is to produce, complete. A card discovered after the write begins is a
    /// defect, because header space is reserved before the pixels are written.
    std::vector<CodecCard> cards{};
};

/// How an image is to be written.
///
/// There is no way to build one that contradicts itself: the constructor is private and `create` is
/// the only way in, so an intent with an explicit scaling always carries both of its numbers and
/// one without it never does.
///
/// Immutable. A change of mind about a write is a new intent, which is what makes the decision
/// reviewable at the point it was taken.
class OutputIntent
{
public:
    /// The decision stated by this request, when it is consistent.
    ///
    /// @param request what to write and how.
    /// @return the intent; `ErrorCode::InvalidArgument` for a value outside an enumeration, a scale
    ///         that is zero or not finite, an offset that is not finite, a compression level
    ///         outside its compression's range, or a compression block of no bytes;
    ///         `ErrorCode::IncompatibleArguments` for a byte order stated for a one-byte
    ///         representation, an explicit scaling missing its scale or its offset, a scale or offset
    ///         beside any other scaling, a level beside a compression that takes none, byte shuffling
    ///         or a compression block stated beside no compression, or a rounding rule other than
    ///         nearest for a floating destination; or
    ///         `ErrorCode::LimitExceeded` for more than `kMaxCodecCards` cards, carrying the count and
    ///         the bound. The first two name the field at fault.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<OutputIntent> create(const OutputIntentRequest& request);

    /// What one stored sample is to be.
    ///
    /// @return the destination representation.
    [[nodiscard]] AFTERNIGHTENGINE_API SampleType sampleType() const noexcept;

    /// In which order the file is to hold the bytes of one sample.
    ///
    /// @return the byte order. `ByteOrder::Native` for every one-byte representation.
    [[nodiscard]] AFTERNIGHTENGINE_API ByteOrder byteOrder() const noexcept;

    /// How image values are mapped onto stored numbers.
    ///
    /// @return the scaling rule.
    [[nodiscard]] AFTERNIGHTENGINE_API OutputScaling scaling() const noexcept;

    /// The multiplier a reader applies to stored values.
    ///
    /// @return the scale, or an empty optional whenever the scaling is not `OutputScaling::Explicit`.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<double> scale() const noexcept;

    /// The offset a reader adds after the multiplier.
    ///
    /// @return the offset, or an empty optional whenever the scaling is not
    ///         `OutputScaling::Explicit`.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<double> zeroOffset() const noexcept;

    /// How a value between two storable numbers is resolved.
    ///
    /// @return the rounding rule.
    [[nodiscard]] AFTERNIGHTENGINE_API OutputRounding rounding() const noexcept;

    /// How the stored bytes are compressed.
    ///
    /// @return the compression.
    [[nodiscard]] AFTERNIGHTENGINE_API OutputCompression compression() const noexcept;

    /// How hard, or how lossily, the compression is to work.
    ///
    /// @return the level in the compression's own units, or an empty optional when the caller
    ///         stated none and the destination codec's default applies.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<std::uint32_t> compressionLevel() const noexcept;

    /// Whether the bytes of the samples are shuffled before they are compressed.
    ///
    /// @return the caller's statement, or an empty optional when the destination codec's default
    ///         applies. Empty whenever the compression is `OutputCompression::None`.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<bool> byteShuffle() const noexcept;

    /// The largest piece of the pixel data compressed on its own, in bytes before compression.
    ///
    /// @return the size, positive, or an empty optional when the destination codec's default applies.
    ///         Empty whenever the compression is `OutputCompression::None`.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<std::uint64_t> compressionBlockBytes() const noexcept;

    /// The checksum the file states for the stored bytes of its pixel data.
    ///
    /// @return the checksum, `OutputChecksum::None` for none.
    [[nodiscard]] AFTERNIGHTENGINE_API OutputChecksum checksum() const noexcept;

    /// Whether the cards a source file carried are written back out.
    ///
    /// @return true when preserved cards are written wherever this intent does not override them.
    [[nodiscard]] AFTERNIGHTENGINE_API bool preserveSourceCards() const noexcept;

    /// The cards this write is to produce.
    ///
    /// @return the cards, borrowed from this intent and valid for as long as it is.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const CodecCard> cards() const noexcept;

    /// The request this intent was built from.
    ///
    /// @return every field, in a form `create` accepts.
    [[nodiscard]] AFTERNIGHTENGINE_API OutputIntentRequest toRequest() const;

    /// Whether two intents describe the same write.
    ///
    /// @param left one intent.
    /// @param right the other.
    /// @return true when every field is equal, including the cards, in order.
    [[nodiscard]] friend bool operator==(const OutputIntent& left, const OutputIntent& right) = default;

private:
    /// Builds from a request whose values have already been checked.
    ///
    /// @param request the validated decision.
    explicit OutputIntent(const OutputIntentRequest& request);

    /// The multiplier a reader applies to stored values.
    std::optional<double> m_scale{};

    /// The offset a reader adds after the multiplier.
    std::optional<double> m_zeroOffset{};

    /// The cards this write is to produce.
    std::vector<CodecCard> m_cards{};

    /// How hard, or how lossily, the compression is to work.
    std::optional<std::uint32_t> m_compressionLevel{};

    /// The largest piece of the pixel data compressed on its own.
    std::optional<std::uint64_t> m_compressionBlockBytes{};

    /// Whether the bytes of the samples are shuffled before they are compressed.
    std::optional<bool> m_byteShuffle{};

    /// The checksum the file states for the stored bytes of its pixel data.
    OutputChecksum m_checksum = OutputChecksum::None;

    /// What one stored sample is to be.
    SampleType m_sampleType = SampleType::Float32;

    /// In which order the file is to hold the bytes of one sample.
    ByteOrder m_byteOrder = ByteOrder::Native;

    /// How image values are mapped onto stored numbers.
    OutputScaling m_scaling = OutputScaling::None;

    /// How a value between two storable numbers is resolved.
    OutputRounding m_rounding = OutputRounding::Nearest;

    /// How the stored bytes are compressed.
    OutputCompression m_compression = OutputCompression::None;

    /// Whether the cards a source file carried are written back out.
    bool m_preserveSourceCards = true;
};

} // namespace an
