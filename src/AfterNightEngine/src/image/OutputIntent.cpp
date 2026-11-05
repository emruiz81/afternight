// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/OutputIntent.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// Four combination rules here refuse things that would otherwise be quietly meaningless, and each
// of them is a fact a later reader would have had to guess.
//
// **A byte order stated for a one-byte representation is refused**, for the reason the sample
// vocabulary gives: one byte has one order, and reporting one produces two descriptions of
// identical bytes that then compare unequal.
//
// **Scale and offset belong to explicit scaling and nowhere else.** A scale carried beside
// `None` is a number that will one day be applied by something that assumes it means what it means
// beside `Explicit`, and a scale of zero is an encoding that cannot be undone.
//
// **A compression level belongs to a compression that takes one.** Deflate takes a level from 0 to 9,
// LZ4-HC one from 1 to 12, Zstandard one from 1 to 22 and JPEG one from 1 to 100; a level stated
// beside any other compression is refused for the reason a stray scale is, and one outside its
// compression's range is refused as a bad value. Byte shuffling and a compression block size are
// steps of a compression, so either stated beside none is refused for the same reason.
//
// **Rounding other than nearest is refused for a floating destination.** Rounding describes
// quantization, a floating destination does not quantize in the sense the rule is about, and an
// intent that says `Floor` into Float32 is an intent whose author expected something that will not
// happen.

namespace an
{
namespace
{

/// Whether a stated number is one a reader can apply.
///
/// @param value the number to test.
/// @return true when it is finite.
[[nodiscard]] bool isFinite(double value) noexcept
{
    return value > -std::numeric_limits<double>::infinity() && value < std::numeric_limits<double>::infinity();
}

/// The refusal for a field that cannot be what it claims.
///
/// @param message what is wrong, for a person.
/// @param parameter which field, spelled as the caller wrote it.
/// @return an `ErrorCode::InvalidArgument` failure naming the field.
[[nodiscard]] Status refuseField(const char* message, const char* parameter)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = message,
                                        .operation = "OutputIntent::create",
                                        .domain = ErrorDomain::Image,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// The refusal for two fields that cannot both be true.
///
/// @param message what cannot be, for a person.
/// @param parameter which field to point at.
/// @return an `ErrorCode::IncompatibleArguments` failure naming the field.
[[nodiscard]] Status refuseCombination(const char* message, const char* parameter)
{
    return Status::failure(ErrorCode::IncompatibleArguments,
                           StatusDetail{.message = message,
                                        .operation = "OutputIntent::create",
                                        .domain = ErrorDomain::Image,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

} // namespace

const char* outputScalingName(OutputScaling scaling) noexcept
{
    switch (scaling)
    {
    case OutputScaling::None:
        return "None";
    case OutputScaling::Explicit:
        return "Explicit";
    case OutputScaling::NormalizeFullRange:
        return "NormalizeFullRange";
    }
    return "UnknownOutputScaling";
}

const char* outputRoundingName(OutputRounding rounding) noexcept
{
    switch (rounding)
    {
    case OutputRounding::Nearest:
        return "Nearest";
    case OutputRounding::TowardZero:
        return "TowardZero";
    case OutputRounding::Floor:
        return "Floor";
    case OutputRounding::Ceiling:
        return "Ceiling";
    }
    return "UnknownOutputRounding";
}

const char* outputCompressionName(OutputCompression compression) noexcept
{
    switch (compression)
    {
    case OutputCompression::None:
        return "None";
    case OutputCompression::Rice:
        return "Rice";
    case OutputCompression::Gzip:
        return "Gzip";
    case OutputCompression::Plio:
        return "Plio";
    case OutputCompression::Deflate:
        return "Deflate";
    case OutputCompression::Lzw:
        return "Lzw";
    case OutputCompression::Jpeg:
        return "Jpeg";
    case OutputCompression::Lz4:
        return "Lz4";
    case OutputCompression::Lz4Hc:
        return "Lz4Hc";
    case OutputCompression::Zstd:
        return "Zstd";
    }
    return "UnknownOutputCompression";
}

const char* outputChecksumName(OutputChecksum checksum) noexcept
{
    switch (checksum)
    {
    case OutputChecksum::None:
        return "None";
    case OutputChecksum::Sha1:
        return "Sha1";
    case OutputChecksum::Sha256:
        return "Sha256";
    case OutputChecksum::Sha512:
        return "Sha512";
    case OutputChecksum::Sha3Digest256:
        return "Sha3Digest256";
    case OutputChecksum::Sha3Digest512:
        return "Sha3Digest512";
    }
    return "UnknownOutputChecksum";
}

OutputIntent::OutputIntent(const OutputIntentRequest& request)
    : m_scale(request.scale), m_zeroOffset(request.zeroOffset), m_cards(request.cards),
      m_compressionLevel(request.compressionLevel), m_compressionBlockBytes(request.compressionBlockBytes),
      m_byteShuffle(request.byteShuffle), m_checksum(request.checksum), m_sampleType(request.sampleType),
      m_byteOrder(request.byteOrder), m_scaling(request.scaling), m_rounding(request.rounding),
      m_compression(request.compression), m_preserveSourceCards(request.preserveSourceCards)
{
}

namespace
{

/// The range a compression's level is stated in, or nothing for one that takes no level.
///
/// @param compression the compression to ask about.
/// @return the inclusive bounds, or an empty optional when no level applies.
[[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> levelRange(OutputCompression compression) noexcept
{
    if (compression == OutputCompression::Deflate)
    {
        return std::pair<std::uint32_t, std::uint32_t>{0u, 9u};
    }
    if (compression == OutputCompression::Jpeg)
    {
        return std::pair<std::uint32_t, std::uint32_t>{1u, 100u};
    }
    if (compression == OutputCompression::Lz4Hc)
    {
        return std::pair<std::uint32_t, std::uint32_t>{1u, 12u};
    }
    if (compression == OutputCompression::Zstd)
    {
        return std::pair<std::uint32_t, std::uint32_t>{1u, 22u};
    }
    return std::nullopt;
}

} // namespace

Result<OutputIntent> OutputIntent::create(const OutputIntentRequest& request)
{
    if (!isSampleType(request.sampleType))
    {
        return refuseField("the destination representation is not one of the eight this engine writes", "sampleType");
    }
    if (!isByteOrder(request.byteOrder))
    {
        return refuseField("the destination byte order is not one this engine writes", "byteOrder");
    }
    if (!isByteOrderMeaningful(request.sampleType) && request.byteOrder != ByteOrder::Native)
    {
        return refuseCombination("a byte order was stated for a one-byte representation, which has only one",
                                 "byteOrder");
    }
    if (!isOutputScaling(request.scaling))
    {
        return refuseField("the scaling is not one this engine applies", "scaling");
    }
    if (!isOutputRounding(request.rounding))
    {
        return refuseField("the rounding rule is not one this engine applies", "rounding");
    }
    if (!isOutputCompression(request.compression))
    {
        return refuseField("the compression is not one this engine writes", "compression");
    }
    if (!isOutputChecksum(request.checksum))
    {
        return refuseField("the checksum is not one this engine writes", "checksum");
    }

    if (request.scaling == OutputScaling::Explicit)
    {
        if (!request.scale.has_value() || !request.zeroOffset.has_value())
        {
            return refuseCombination("an explicit scaling needs both its scale and its offset, or a reader cannot "
                                     "undo it",
                                     "scaling");
        }
        if (!isFinite(*request.scale) || *request.scale == 0.0)
        {
            return refuseField("a scale of zero or of no value cannot be undone", "scale");
        }
        if (!isFinite(*request.zeroOffset))
        {
            return refuseField("an offset is a finite number", "zeroOffset");
        }
    }
    else if (request.scale.has_value() || request.zeroOffset.has_value())
    {
        return refuseCombination("a scale or an offset was stated beside a scaling that does not use one; a number "
                                 "carried there would be applied as if it did",
                                 "scaling");
    }

    if (request.compressionLevel.has_value())
    {
        const auto range = levelRange(request.compression);
        if (!range.has_value())
        {
            return refuseCombination("a compression level was stated beside a compression that takes none; a number "
                                     "carried there would be applied as if it did",
                                     "compressionLevel");
        }
        if (*request.compressionLevel < range->first || *request.compressionLevel > range->second)
        {
            return refuseField("the compression level is outside the range this compression is stated in",
                               "compressionLevel");
        }
    }

    if (request.compression == OutputCompression::None && request.byteShuffle.has_value())
    {
        return refuseCombination("byte shuffling was stated beside no compression; it is a step of one", "byteShuffle");
    }
    if (request.compressionBlockBytes.has_value())
    {
        if (request.compression == OutputCompression::None)
        {
            return refuseCombination("a compression block size was stated beside no compression; it is a step of one",
                                     "compressionBlockBytes");
        }
        if (*request.compressionBlockBytes == 0u)
        {
            return refuseField("a compression block holds at least one byte", "compressionBlockBytes");
        }
    }

    if (isFloatingSampleType(request.sampleType) && request.rounding != OutputRounding::Nearest)
    {
        return refuseCombination("a rounding rule describes quantization, and a floating destination does not "
                                 "quantize the way the rule is about",
                                 "rounding");
    }
    if (request.cards.size() > kMaxCodecCards)
    {
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{.message = "more cards than one image writes",
                         .operation = "OutputIntent::create",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofCount(ContextKey::Requested,
                                                                 static_cast<std::uint64_t>(request.cards.size())),
                                     StatusContextField::ofCount(ContextKey::Limit,
                                                                 static_cast<std::uint64_t>(kMaxCodecCards))}});
    }

    return OutputIntent{request};
}

SampleType OutputIntent::sampleType() const noexcept
{
    return m_sampleType;
}

ByteOrder OutputIntent::byteOrder() const noexcept
{
    return m_byteOrder;
}

OutputScaling OutputIntent::scaling() const noexcept
{
    return m_scaling;
}

std::optional<double> OutputIntent::scale() const noexcept
{
    return m_scale;
}

std::optional<double> OutputIntent::zeroOffset() const noexcept
{
    return m_zeroOffset;
}

OutputRounding OutputIntent::rounding() const noexcept
{
    return m_rounding;
}

OutputCompression OutputIntent::compression() const noexcept
{
    return m_compression;
}

std::optional<std::uint32_t> OutputIntent::compressionLevel() const noexcept
{
    return m_compressionLevel;
}

std::optional<bool> OutputIntent::byteShuffle() const noexcept
{
    return m_byteShuffle;
}

std::optional<std::uint64_t> OutputIntent::compressionBlockBytes() const noexcept
{
    return m_compressionBlockBytes;
}

OutputChecksum OutputIntent::checksum() const noexcept
{
    return m_checksum;
}

bool OutputIntent::preserveSourceCards() const noexcept
{
    return m_preserveSourceCards;
}

std::span<const CodecCard> OutputIntent::cards() const noexcept
{
    return std::span<const CodecCard>{m_cards};
}

OutputIntentRequest OutputIntent::toRequest() const
{
    return OutputIntentRequest{.sampleType = m_sampleType,
                               .byteOrder = m_byteOrder,
                               .scaling = m_scaling,
                               .scale = m_scale,
                               .zeroOffset = m_zeroOffset,
                               .rounding = m_rounding,
                               .compression = m_compression,
                               .compressionLevel = m_compressionLevel,
                               .byteShuffle = m_byteShuffle,
                               .compressionBlockBytes = m_compressionBlockBytes,
                               .checksum = m_checksum,
                               .preserveSourceCards = m_preserveSourceCards,
                               .cards = m_cards};
}

} // namespace an
