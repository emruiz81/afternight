// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What an image's numbers mean, split into the three questions it takes.
///
/// A pixel type and a pixel format cannot answer all of this: with only those, an algorithm has to infer a numeric
/// domain from a C++ element type, and a stretched image and a linear one are the same object with the same claims
/// about itself. Three vocabularies answer it instead, because there are three independent questions:
///
///   * which channels are these - one intensity, red/green/blue, hue/saturation/lightness;
///   * in what basis are they expressed - scene-referred RGB primaries, tristimulus, perceptual;
///   * and what do the magnitudes mean - a fraction of full scale, a sensor code, a physical value.
///
/// Keeping them apart is only worth the three enumerations because of the combinations it lets the
/// descriptor refuse. Lab channels in a display RGB basis, a colorimetric basis over uncalibrated
/// sensor codes and nonlinearly encoded tristimulus values are each a contradiction that one
/// combined enumeration would have had to either enumerate or admit. The rules that say so are
/// stated here, beside the values they judge, so that a later change arguing with one of them argues
/// with a documented reason rather than with a condition buried in a constructor.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <optional>

namespace an
{

/// What an image's channels are.
///
/// The count is part of the answer for every schema but one: three channels are not RGBA and four
/// are not RGB, so a schema and an extent that disagree describe no image. `Named` is the exception,
/// and exists because a narrowband stack's channels are whatever the filters were.
///
/// A mosaiced sensor frame is `Mono`: one recorded plane, with the colour filter array describing
/// which filter covered each pixel of it. It is not a colour schema until it is demosaiced.
enum class ChannelSchema : std::uint8_t
{
    /// One channel of plain intensity. A monochrome frame, a luminance plane, a mask, or a sensor
    /// frame before its mosaic is resolved.
    Mono,

    /// Red, green and blue, in that order.
    Rgb,

    /// Red, green, blue and alpha, in that order. The only schema of these that carries alpha.
    Rgba,

    /// CIE XYZ tristimulus values, in that order.
    Xyz,

    /// CIE L*a*b*, in that order.
    Lab,

    /// CIE L*C*h, the cylindrical form of Lab, in that order.
    Lch,

    /// Hue, saturation and lightness, in that order.
    Hsl,

    /// Hue, saturation and value, in that order.
    Hsv,

    /// Hue, saturation and intensity, in that order.
    Hsi,

    /// Luma and the two chroma difference channels, in that order.
    YCbCr,

    /// Channels whose meaning is carried outside this vocabulary - a narrowband stack's filters, a
    /// survey's bands. The one schema whose channel count is a property of the image.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Named
};

/// In what basis an image's colour channels are expressed.
///
/// Separate from the schema because the same three channels can be red, green and blue in more than
/// one set of primaries, and separate from the numeric domain because whether the values are linear
/// in light is a property of the encoding rather than of the basis.
enum class ColorSpace : std::uint8_t
{
    /// No colorimetric basis is claimed. What an uncalibrated sensor frame has, before anything
    /// establishes what its red channel is red in.
    Unspecified,

    /// Scene-referred RGB primaries: the values stand in fixed proportion to light arriving at the
    /// sensor.
    LinearRgb,

    /// Display-referred RGB primaries: the values stand for what a display should emit.
    DisplayRgb,

    /// CIE tristimulus values.
    CieXyz,

    /// The CIE perceptual space, whether the channels are held as Lab or as its cylindrical LCh
    /// form.
    CieLab,

    /// Narrowband emission lines mapped onto colour channels - an SHO or HOO palette. Not a
    /// colorimetric basis at all: the mapping is chosen for what it shows, and calling it one would
    /// let a colour transform treat a hydrogen line as if it were red light.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    NarrowbandMapping
};

/// What an image's magnitudes mean.
///
/// Stated rather than read off the C++ element type, which cannot tell a linear stack from a
/// stretched preview.
enum class NumericDomain : std::uint8_t
{
    /// A fraction of full scale, linear in the quantity recorded. The integer representations mean
    /// their whole range maps to that fraction; the floating ones mean the usual zero-to-one.
    NormalizedLinear,

    /// A fraction of full scale through a nonlinear encoding - a gamma curve, a display transfer
    /// function, a stretch. Which encoding is not stated here: it belongs with the colour transforms
    /// that apply and remove it, and no algorithm in this engine may assume one from this value.
    EncodedNonlinear,

    /// The number the sensor reported, in its own units, before any calibration establishes what it
    /// stands for.
    RawSensorCode,

    /// A physical quantity in stated units - electrons, electrons per second, a flux. Needs a
    /// floating representation, because the descriptor carries no scale factor for an integer to be
    /// interpreted through.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    PhysicalScaled
};

/// Whether a value is one of the schemas this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param schema the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isChannelSchema(ChannelSchema schema) noexcept
{
    return static_cast<std::uint8_t>(schema) <= static_cast<std::uint8_t>(ChannelSchema::Named);
}

/// Whether a value is one of the colour bases this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param space the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isColorSpace(ColorSpace space) noexcept
{
    return static_cast<std::uint8_t>(space) <= static_cast<std::uint8_t>(ColorSpace::NarrowbandMapping);
}

/// Whether a value is one of the domains this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param domain the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isNumericDomain(NumericDomain domain) noexcept
{
    return static_cast<std::uint8_t>(domain) <= static_cast<std::uint8_t>(NumericDomain::PhysicalScaled);
}

/// How many channels an image of this schema has, where the schema decides.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param schema the schema to measure.
/// @return the fixed channel count; or an empty optional for `Named`, whose count is a property of
///         the image, and for a value outside the enumeration, about which nothing is known.
[[nodiscard]] constexpr std::optional<std::uint32_t> channelCountOf(ChannelSchema schema) noexcept
{
    switch (schema)
    {
    case ChannelSchema::Mono:
        return 1u;
    case ChannelSchema::Rgba:
        return 4u;
    case ChannelSchema::Rgb:
    case ChannelSchema::Xyz:
    case ChannelSchema::Lab:
    case ChannelSchema::Lch:
    case ChannelSchema::Hsl:
    case ChannelSchema::Hsv:
    case ChannelSchema::Hsi:
    case ChannelSchema::YCbCr:
        return 3u;
    case ChannelSchema::Named:
        return std::nullopt;
    }
    return std::nullopt;
}

/// Whether this schema's last channel is opacity.
///
/// Asked before a channel-wise operation decides whether its last channel is data.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param schema the schema to test.
/// @return true only for `Rgba`. `Named` answers false: a named channel that happens to be called
///         alpha is not one this vocabulary has established the meaning of.
[[nodiscard]] constexpr bool schemaHasAlpha(ChannelSchema schema) noexcept
{
    return schema == ChannelSchema::Rgba;
}

/// Whether channels of this schema can be expressed in this basis.
///
/// The rules, and why each of them is a contradiction rather than an unusual choice:
///
///   * `Unspecified` is admitted by every schema that does not name its own basis. Refusing it
///     would force a caller holding an uncalibrated frame to invent a basis it does not have;
///   * `Mono` admits nothing else. One channel of intensity carries no colour, and which filter it
///     was recorded through is an acquisition fact rather than a colour basis;
///   * `Xyz`, `Lab` and `Lch` name their basis in the schema itself, so each admits that basis
///     alone and refuses even `Unspecified`, because a descriptor that names a space in one field
///     and declines to in another disagrees with itself;
///   * `Hsl`, `Hsv`, `Hsi` and `YCbCr` are computed from red, green and blue, so they admit the two
///     RGB bases. A narrowband mapping is refused because the mapping is defined onto colour
///     channels, not onto hue and saturation;
///   * `Rgb` and `Rgba` admit both RGB bases and a narrowband mapping, which is where an SHO
///     composition lives; and
///   * `Named` admits a narrowband mapping but neither RGB basis, because its channels are not
///     established as red, green and blue.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param schema the channel schema.
/// @param space the colour basis.
/// @return true when the pair can describe one image; false when it cannot, and for a value outside
///         either enumeration.
[[nodiscard]] constexpr bool isColorSpaceCompatible(ChannelSchema schema, ColorSpace space) noexcept
{
    if (!isChannelSchema(schema) || !isColorSpace(space))
    {
        return false;
    }
    switch (schema)
    {
    case ChannelSchema::Mono:
        return space == ColorSpace::Unspecified;
    case ChannelSchema::Xyz:
        return space == ColorSpace::CieXyz;
    case ChannelSchema::Lab:
    case ChannelSchema::Lch:
        return space == ColorSpace::CieLab;
    case ChannelSchema::Hsl:
    case ChannelSchema::Hsv:
    case ChannelSchema::Hsi:
    case ChannelSchema::YCbCr:
        return space == ColorSpace::Unspecified || space == ColorSpace::LinearRgb || space == ColorSpace::DisplayRgb;
    case ChannelSchema::Rgb:
    case ChannelSchema::Rgba:
        return space == ColorSpace::Unspecified || space == ColorSpace::LinearRgb || space == ColorSpace::DisplayRgb ||
               space == ColorSpace::NarrowbandMapping;
    case ChannelSchema::Named:
        return space == ColorSpace::Unspecified || space == ColorSpace::NarrowbandMapping;
    }
    return false;
}

/// Whether values in this basis can mean what this domain says they mean.
///
/// The rules, and why each of them is a contradiction:
///
///   * a raw sensor code has no colorimetric basis. The code is what the sensor reported; naming a
///     basis for it claims a calibration that has not happened, so `RawSensorCode` admits
///     `Unspecified` alone;
///   * `CieXyz` is linear in light by construction, so nonlinearly encoded tristimulus values are
///     values that were never produced; and
///   * `CieLab` is a nonlinear function of XYZ by construction, so linear or physically scaled Lab
///     is the same contradiction from the other side.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param space the colour basis.
/// @param domain what the magnitudes mean.
/// @return true when the pair can describe one image; false when it cannot, and for a value outside
///         either enumeration.
[[nodiscard]] constexpr bool isNumericDomainCompatible(ColorSpace space, NumericDomain domain) noexcept
{
    if (!isColorSpace(space) || !isNumericDomain(domain))
    {
        return false;
    }
    if (domain == NumericDomain::RawSensorCode)
    {
        return space == ColorSpace::Unspecified;
    }
    switch (space)
    {
    case ColorSpace::CieXyz:
        return domain == NumericDomain::NormalizedLinear || domain == NumericDomain::PhysicalScaled;
    case ColorSpace::CieLab:
        return domain == NumericDomain::EncodedNonlinear;
    case ColorSpace::Unspecified:
    case ColorSpace::LinearRgb:
    case ColorSpace::DisplayRgb:
    case ColorSpace::NarrowbandMapping:
        return true;
    }
    return false;
}

/// Whether a sample of this representation can carry a value in this domain.
///
/// One rule: a physically scaled value needs a floating representation. The descriptor holds no
/// scale factor - a source file's `BSCALE` and `BZERO` are codec facts about that file, not
/// resident ones - so an integer sample in a physical domain has nothing to say what quantity it
/// stands for, and the number would be a count of unspecified units.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param domain what the magnitudes mean.
/// @param type the sample representation.
/// @return true when the representation can carry the domain; false when it cannot, and for a value
///         outside either enumeration.
[[nodiscard]] constexpr bool isNumericDomainRepresentable(NumericDomain domain, SampleType type) noexcept
{
    if (!isNumericDomain(domain) || !isSampleType(type))
    {
        return false;
    }
    return domain != NumericDomain::PhysicalScaled || isFloatingSampleType(type);
}

/// The schema's own name, for a log line, a diagnostic field or the descriptor's text form.
///
/// The spelling of the enumerator: `ChannelSchema::YCbCr` is `"YCbCr"`.
///
/// Thread-safe and allocation-free.
///
/// @param schema the schema to name. A value outside the enumeration is named
///        `"UnknownChannelSchema"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* channelSchemaName(ChannelSchema schema) noexcept;

/// The basis's own name, for a log line, a diagnostic field or the descriptor's text form.
///
/// Thread-safe and allocation-free.
///
/// @param space the basis to name. A value outside the enumeration is named `"UnknownColorSpace"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* colorSpaceName(ColorSpace space) noexcept;

/// The domain's own name, for a log line, a diagnostic field or the descriptor's text form.
///
/// Thread-safe and allocation-free.
///
/// @param domain the domain to name. A value outside the enumeration is named
///        `"UnknownNumericDomain"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* numericDomainName(NumericDomain domain) noexcept;

} // namespace an
