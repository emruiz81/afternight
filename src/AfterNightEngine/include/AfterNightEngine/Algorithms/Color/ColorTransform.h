// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Colour stated as an encoding, and a transform between two of them.
///
/// **A colour transform is between two stated encodings and nothing is inferred.** `ColorEncoding`
/// says which channels, in whose primaries, under which white point, through which transfer
/// function, and - for the schemas that have one - which packing convention. `planColorConversion`
/// turns a pair of encodings into an ordered list of stages, refusing the pairs that describe no
/// transform, and the plan is what executes. An encoding that says its samples are already linear is
/// not decoded, because it said so.
///
/// **The connection space is XYZ under the encoding's own white point, normalized so that the
/// reference white has a luminance of one.** Two encodings whose white points differ are joined by
/// one stated Bradford adaptation rather than by the caller having supplied matching numbers.
///
/// **A production transform computes in double and narrows once**, exactly as
/// `Image/SampleConversion.h` does
/// and for the same reason: the cube roots, powers and arc tangents in these stages have no
/// meaningful single-precision variant, and computing the chain in double costs nothing that matters
/// beside the transcendental itself. There is therefore no production precision selector here and no Float64
/// justification to state - the width is not a choice this operation offers. What *is* a choice is
/// the resident representation, and only Float32 and Float64 are accepted: a colour transform is
/// nonlinear, so integer storage converts once through the explicit conversion surface first.
///
/// **A transfer function is applied to the magnitude and the sign is restored.** An out-of-gamut
/// negative is a legitimate intermediate in a wide working space, and raising it to a fractional
/// power would turn it into a not-a-number. Nothing here clamps, for the reason
/// arithmetic does not: a caller that needs its results inside a display interval composes the clamp
/// it wants and can see what it cost.
///
/// **Coarse powers serve a real-time preview, behind a name of their own.**
/// `planRealtimePreviewColorConversion` owns the three 1,024-entry lower-neighbour tables used for
/// sRGB transfer and CIELAB compression. Only the matching preview execution functions consume that
/// plan; ordinary `convertColorPixel` and `convertImageColor` never do. The approximation therefore
/// remains available for responsive controls without becoming the pixels a caller saves.
///
/// **A channel matrix is a pointwise step, not a second image loop.** `buildChannelMatrixProgram`
/// lowers an arbitrary square matrix into the fusable pointwise program, and a conversion between two
/// RGB encodings reports the composed matrix it would use, so a caller may run the whole of a
/// primaries change inside a chain it was already executing.

#pragma once

#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named here rather than included, for the reason `Image/ChannelTransfer.h` names it: a caller that
/// only plans a conversion should not inherit the runtime's whole include closure to do it.
class ExecutionContext;

/// How an RGB encoding's numbers relate to the light they stand for.
///
/// Stated by the image rather than assumed by the transform: the sRGB curve applied unconditionally
/// is correct for a display-referred frame and wrong for every linear one.
enum class ColorTransferFunction : std::uint8_t
{
    /// The values are already proportional to light. Nothing is applied.
    Linear = 0u,

    /// The IEC 61966-2-1 sRGB curve: a linear segment below the break and a 2.4 power above it.
    Srgb = 1u,

    /// The ITU-R BT.709 opto-electronic transfer function, whose break and slope differ from sRGB's.
    Rec709 = 2u,

    /// A pure power law with the encoding's stated exponent and no linear segment.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    PureGamma = 3u
};

/// Whether a value names a transfer function this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param transfer the value to inspect.
/// @return true for every implemented transfer function.
[[nodiscard]] constexpr bool isColorTransferFunction(ColorTransferFunction transfer) noexcept
{
    return static_cast<std::uint8_t>(transfer) <= static_cast<std::uint8_t>(ColorTransferFunction::PureGamma);
}

/// Which red, green and blue an RGB encoding means.
///
/// A closed set of published chromaticities rather than caller-supplied ones, because every entry
/// here is a colour space this engine has to name in a file header or a codec anyway, and a set that
/// can be extended by a caller is a set no descriptor can compare two images in.
enum class RgbPrimaries : std::uint8_t
{
    /// IEC 61966-2-1 sRGB, whose primaries are also ITU-R BT.709's. D65.
    Srgb = 0u,

    /// Adobe RGB (1998), which widens the green primary. D65.
    AdobeRgb1998 = 1u,

    /// Display P3, the DCI-P3 primaries under a D65 white.
    DisplayP3 = 2u,

    /// ITU-R BT.2020, the ultra-high-definition primaries. D65.
    Rec2020 = 3u,

    /// ROMM RGB, usually called ProPhoto. The only entry here whose native white is D50.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    ProPhoto = 4u
};

/// Whether a value names primaries this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param primaries the value to inspect.
/// @return true for every known set of primaries.
[[nodiscard]] constexpr bool isRgbPrimaries(RgbPrimaries primaries) noexcept
{
    return static_cast<std::uint8_t>(primaries) <= static_cast<std::uint8_t>(RgbPrimaries::ProPhoto);
}

/// Which illuminant an encoding's white is.
///
/// Two, because two are what the primaries above and the perceptual space below actually use. A
/// third would need its own chromaticity and its own adaptation evidence, and inventing one nothing
/// produces would be a value no image ever carries.
enum class ChromaticWhitePoint : std::uint8_t
{
    /// CIE standard illuminant D50, the printing and ICC profile-connection white.
    D50 = 0u,

    /// CIE standard illuminant D65, the display white.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    D65 = 1u
};

/// Whether a value names a white point this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param whitePoint the value to inspect.
/// @return true for both known white points.
[[nodiscard]] constexpr bool isChromaticWhitePoint(ChromaticWhitePoint whitePoint) noexcept
{
    return whitePoint == ChromaticWhitePoint::D50 || whitePoint == ChromaticWhitePoint::D65;
}

/// In what units a perceptual encoding's channels are held.
///
/// Lab lightness runs to a hundred and its two opponent channels are signed, so a convention that
/// puts them in the unit interval exists wherever the samples had to fit one. Both are real and both
/// are asked for by name; nothing here infers which one a buffer holds.
enum class PerceptualEncoding : std::uint8_t
{
    /// The CIE units themselves: `L` in `[0, 100]`, `a` and `b` about `[-128, 127]`, chroma from
    /// zero, and hue in degrees over `[0, 360)`.
    NaturalUnits = 0u,

    /// Every component packed into the unit interval: `L / 100`, `(a + 128) / 255`, `(b + 128) / 255`,
    /// `C / 200`, and `(h + 180) / 360` over a hue measured in `[-180, 180)`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    UnitPacked = 1u
};

/// Whether a value names a perceptual packing this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param encoding the value to inspect.
/// @return true for both packings.
[[nodiscard]] constexpr bool isPerceptualEncoding(PerceptualEncoding encoding) noexcept
{
    return encoding == PerceptualEncoding::NaturalUnits || encoding == PerceptualEncoding::UnitPacked;
}

/// Which luma weights a luma-and-chroma encoding uses.
///
/// Both are full range - the chroma channels are centred on one half and the luma covers the whole
/// interval - because a studio-range variant is a quantisation convention that belongs with the
/// codec that produced it rather than with a resident image.
enum class YCbCrVariant : std::uint8_t
{
    /// The ITU-R BT.601 weights, which is what JFIF uses.
    Rec601FullRange = 0u,

    /// The ITU-R BT.709 weights.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Rec709FullRange = 1u
};

/// Whether a value names a luma-chroma variant this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param variant the value to inspect.
/// @return true for both variants.
[[nodiscard]] constexpr bool isYCbCrVariant(YCbCrVariant variant) noexcept
{
    return variant == YCbCrVariant::Rec601FullRange || variant == YCbCrVariant::Rec709FullRange;
}

/// In what unit a cylindrical RGB encoding's hue channel is measured.
///
/// The two conventions in circulation, and the reason to name one: a hue of `0.5` and a hue of
/// `0.5` mean opposite colours under the two of them, and neither is recoverable from the number.
enum class HueConvention : std::uint8_t
{
    /// A fraction of one turn over `[0, 1)`.
    TurnFraction = 0u,

    /// Degrees over `[0, 360)`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Degrees = 1u
};

/// Whether a value names a hue convention this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param convention the value to inspect.
/// @return true for both conventions.
[[nodiscard]] constexpr bool isHueConvention(HueConvention convention) noexcept
{
    return convention == HueConvention::TurnFraction || convention == HueConvention::Degrees;
}

/// Why an encoding describes no image.
///
/// A vocabulary rather than a type with behaviour, in the shape `Image/DescriptorError.h` uses: the
/// `checkColorEncoding` entry point returns one of these and the caller that only wants the answer
/// asks whether it is `None`.
enum class ColorEncodingError : std::uint8_t
{
    /// The encoding describes an image.
    None = 0u,

    /// The channel schema is not one of the enumerators.
    UnknownChannelSchema = 1u,

    /// The channel schema is one this transform family does not carry. `Mono` has no colour and
    /// `Named` has no fixed meaning, so neither can be an end of a colour transform.
    UnsupportedChannelSchema = 2u,

    /// The primaries are not one of the enumerators.
    UnknownRgbPrimaries = 3u,

    /// The transfer function is not one of the enumerators.
    UnknownTransferFunction = 4u,

    /// The white point is not one of the enumerators.
    UnknownWhitePoint = 5u,

    /// The perceptual packing is not one of the enumerators.
    UnknownPerceptualEncoding = 6u,

    /// The luma-chroma variant is not one of the enumerators.
    UnknownYCbCrVariant = 7u,

    /// The hue convention is not one of the enumerators.
    UnknownHueConvention = 8u,

    /// A pure power law was asked for and no exponent was given.
    TransferGammaMissing = 9u,

    /// An exponent was given for a transfer function that is not a pure power law. It would be a
    /// number recorded beside a curve that does not read it.
    TransferGammaNotAllowed = 10u,

    /// The exponent is not a positive finite number.
    TransferGammaNotPositive = 11u,

    /// Primaries were named for a schema that has none. Tristimulus and perceptual channels are
    /// defined by the white point alone.
    PrimariesNotAllowed = 12u,

    /// A transfer function was named for a schema that has none. The nonlinearity of a perceptual
    /// space is part of the space, not an encoding applied to it.
    TransferNotAllowed = 13u,

    /// The stated white point is not the one the stated primaries are defined under.
    WhitePointDisagreesWithPrimaries = 14u,

    /// A perceptual packing was named for a schema that is not held in one.
    PerceptualEncodingNotAllowed = 15u,

    /// A luma-chroma variant was named for a schema that is not luma and chroma.
    YCbCrVariantNotAllowed = 16u,

    /// A hue convention was named for a schema that has no hue channel.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    HueConventionNotAllowed = 17u
};

/// Whether a value is one of the reasons this build reports.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param error the value to inspect.
/// @return true for every reason, `None` included.
[[nodiscard]] constexpr bool isColorEncodingError(ColorEncodingError error) noexcept
{
    return static_cast<std::uint8_t>(error) <= static_cast<std::uint8_t>(ColorEncodingError::HueConventionNotAllowed);
}

/// The reason's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param error the reason to name. A value outside the enumeration is named
///        `"UnknownColorEncodingError"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* colorEncodingErrorName(ColorEncodingError error) noexcept;

/// What one image's colour numbers are.
///
/// A plain aggregate with defaults, so a caller writes only what it is claiming, and the defaults
/// describe a linear sRGB image - the working space most of this engine produces.
///
/// Every member is read by some schemas and not by others, and a member a schema does not read must
/// be left at the neutral value shown here. That is the same rule `Algorithms/Pointwise/PointwiseOp.h`
/// applies to a step's coefficients, and it is here for the same reason: an encoding that carries a
/// set of primaries its schema ignores is an encoding two readers will describe differently.
struct ColorEncoding
{
    /// Which channels. `Mono` and `Named` are refused: neither names a colour this family converts.
    ChannelSchema schema = ChannelSchema::Rgb;

    /// Which red, green and blue. Read by `Rgb`, `Rgba`, `Hsl`, `Hsv`, `Hsi` and `YCbCr`; neutral
    /// for `Xyz`, `Lab` and `Lch`.
    RgbPrimaries primaries = RgbPrimaries::Srgb;

    /// How the numbers relate to light. Read by the same schemas as `primaries`; neutral for `Xyz`,
    /// `Lab` and `Lch`, whose relation to light is the space itself.
    ColorTransferFunction transfer = ColorTransferFunction::Linear;

    /// The exponent of a pure power law. Required by `ColorTransferFunction::PureGamma`, refused by
    /// every other transfer function, and must be positive and finite.
    std::optional<double> transferGamma{};

    /// Which illuminant the encoding's white is. For an RGB-family schema this must be the white the
    /// stated primaries are defined under, which is what stops a descriptor disagreeing with itself;
    /// for `Xyz`, `Lab` and `Lch` it is the whole of the answer.
    ChromaticWhitePoint whitePoint = ChromaticWhitePoint::D65;

    /// In what units the perceptual channels are held. Read by `Lab` and `Lch`; neutral elsewhere.
    PerceptualEncoding perceptual = PerceptualEncoding::NaturalUnits;

    /// Which luma weights. Read by `YCbCr`; neutral elsewhere.
    YCbCrVariant yCbCr = YCbCrVariant::Rec601FullRange;

    /// In what unit the hue channel is measured. Read by `Hsl`, `Hsv` and `Hsi`; neutral elsewhere.
    HueConvention hue = HueConvention::TurnFraction;

    /// Two encodings are equal when every member is equal.
    friend constexpr bool operator==(const ColorEncoding& left, const ColorEncoding& right) noexcept = default;
};

/// Whether an encoding describes an image, and if not, why not.
///
/// The order is a contract: every value is checked for membership of its own enumeration first, so a
/// combination rule is never applied to a value this build cannot interpret, and the combination
/// rules then follow in the order the members are declared.
///
/// Thread-safe and allocation-free.
///
/// @param encoding the encoding to inspect.
/// @return `ColorEncodingError::None` when the encoding describes an image; otherwise the first rule
///         that refused.
[[nodiscard]] AFTERNIGHTENGINE_API ColorEncodingError checkColorEncoding(const ColorEncoding& encoding) noexcept;

/// Which illuminant a set of primaries is defined under.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param primaries the primaries to ask about.
/// @return the white point; D65 for a value outside the enumeration, which `checkColorEncoding`
///         refuses before this is consulted.
[[nodiscard]] constexpr ChromaticWhitePoint whitePointOfPrimaries(RgbPrimaries primaries) noexcept
{
    return primaries == RgbPrimaries::ProPhoto ? ChromaticWhitePoint::D50 : ChromaticWhitePoint::D65;
}

/// The reference white's tristimulus values, normalized so that its luminance is one.
///
/// The standard's own tabulated triples rather than values derived from a rounded chromaticity pair,
/// because every published conversion - the sRGB matrix included - is stated against these.
///
/// Thread-safe and allocation-free.
///
/// @param whitePoint which illuminant. A value outside the enumeration answers D65's.
/// @return the `X`, `Y` and `Z` of the white, with `Y` exactly one.
[[nodiscard]] AFTERNIGHTENGINE_API std::array<double, 3u>
whitePointTristimulus(ChromaticWhitePoint whitePoint) noexcept;

/// How many channels an image of this encoding has.
///
/// Thread-safe and allocation-free.
///
/// @param encoding the encoding to measure.
/// @return four for `Rgba`, three for every other schema this family carries, and an empty optional
///         for an encoding `checkColorEncoding` refuses.
[[nodiscard]] AFTERNIGHTENGINE_API std::optional<std::uint32_t>
channelCountOfEncoding(const ColorEncoding& encoding) noexcept;

/// Which colour basis a descriptor of this encoding names.
///
/// The bridge between what a colour transform states and what an image's description carries: an
/// RGB-family encoding whose transfer is linear is expressed in linear RGB primaries, and one whose
/// transfer is any of the curves is display-referred.
///
/// Thread-safe and allocation-free.
///
/// @param encoding the encoding to describe.
/// @return the basis, or an empty optional for an encoding `checkColorEncoding` refuses.
[[nodiscard]] AFTERNIGHTENGINE_API std::optional<ColorSpace>
colorSpaceOfEncoding(const ColorEncoding& encoding) noexcept;

/// What a descriptor of this encoding says its magnitudes mean.
///
/// Thread-safe and allocation-free.
///
/// @param encoding the encoding to describe.
/// @return `NumericDomain::NormalizedLinear` for linear RGB and tristimulus values,
///         `NumericDomain::EncodedNonlinear` for a curve-encoded RGB family and for the perceptual
///         spaces, or an empty optional for an encoding `checkColorEncoding` refuses.
[[nodiscard]] AFTERNIGHTENGINE_API std::optional<NumericDomain>
numericDomainOfEncoding(const ColorEncoding& encoding) noexcept;

/// Three by three coefficients in row-major order.
///
/// A plain aggregate rather than a matrix type with behaviour: what this engine does with a colour
/// matrix is multiply a pixel by it, compose two of them, and invert one, and all three are free
/// functions below. The default is the identity, so a matrix nobody filled in changes nothing.
struct ColorMatrix3
{
    /// Nine coefficients, row by row, so that `coefficients[row * 3 + column]` multiplies the input
    /// channel `column` into the output channel `row`.
    std::array<double, 9u> coefficients{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};

    /// Two matrices are equal when every coefficient is equal.
    friend constexpr bool operator==(const ColorMatrix3& left, const ColorMatrix3& right) noexcept = default;
};

/// The product of two matrices, applying the right one first.
///
/// Thread-safe and allocation-free.
///
/// @param left the matrix applied second.
/// @param right the matrix applied first.
/// @return the composed matrix.
[[nodiscard]] AFTERNIGHTENGINE_API ColorMatrix3 multiplyColorMatrices(const ColorMatrix3& left,
                                                                      const ColorMatrix3& right) noexcept;

/// One pixel through one matrix.
///
/// Thread-safe and allocation-free.
///
/// @param matrix the coefficients.
/// @param channels the three input channels.
/// @return the three output channels.
[[nodiscard]] AFTERNIGHTENGINE_API std::array<double, 3u>
applyColorMatrix(const ColorMatrix3& matrix, const std::array<double, 3u>& channels) noexcept;

/// The inverse of a matrix, where it has one.
///
/// The inverse is computed rather than tabulated, which is what makes a forward and a reverse
/// conversion exact inverses of one another; two matrices each tabulated to six or seven digits are
/// not.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param matrix the coefficients to invert.
/// @return the inverse, or `ErrorCode::InvalidArgument` when a coefficient is not finite or the
///         matrix is singular.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ColorMatrix3> invertColorMatrix(const ColorMatrix3& matrix);

/// The matrix taking linear RGB of these primaries to XYZ under their own white point.
///
/// Derived from the published chromaticities rather than transcribed, so it and its inverse agree to
/// the last bit the arithmetic allows.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param primaries which red, green and blue.
/// @return the matrix, or `ErrorCode::InvalidArgument` for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ColorMatrix3> rgbToXyzMatrix(RgbPrimaries primaries);

/// The matrix taking XYZ under these primaries' white point to their linear RGB.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param primaries which red, green and blue.
/// @return the inverse of `rgbToXyzMatrix`, or `ErrorCode::InvalidArgument` for a value outside the
///         enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ColorMatrix3> xyzToRgbMatrix(RgbPrimaries primaries);

/// The Bradford adaptation taking XYZ under one white to XYZ under another.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param from the white the values are under.
/// @param to the white they should be under.
/// @return the matrix, which is the identity when the two whites are the same; or
///         `ErrorCode::InvalidArgument` for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ColorMatrix3> chromaticAdaptationMatrix(ChromaticWhitePoint from,
                                                                                  ChromaticWhitePoint to);

/// One step of a planned colour conversion.
///
/// The list a plan carries is what it will run, in order, and is readable by a caller that wants to
/// know whether a conversion it asked for costs a transcendental or a matrix.
enum class ColorStage : std::uint8_t
{
    /// Removes the source encoding's transfer function, leaving values proportional to light.
    DecodeTransfer = 0u,

    /// Applies the destination encoding's transfer function.
    EncodeTransfer = 1u,

    /// Multiplies linear RGB by the source primaries' matrix, giving XYZ under the source white.
    LinearRgbToXyz = 2u,

    /// Multiplies XYZ by the destination primaries' inverse matrix, giving linear RGB.
    XyzToLinearRgb = 3u,

    /// Adapts XYZ from the source white to the destination white.
    AdaptWhitePoint = 4u,

    /// Turns XYZ into `L`, `a` and `b` in CIE units under the destination white.
    XyzToLab = 5u,

    /// Turns `L`, `a` and `b` in CIE units under the source white into XYZ.
    LabToXyz = 6u,

    /// Turns `a` and `b` into chroma and a hue in degrees.
    LabToLch = 7u,

    /// Turns chroma and a hue in degrees back into `a` and `b`.
    LchToLab = 8u,

    /// Puts the destination's perceptual channels into its stated packing.
    PackPerceptual = 9u,

    /// Takes the source's perceptual channels out of its stated packing.
    UnpackPerceptual = 10u,

    /// Turns RGB into hue, saturation and lightness.
    RgbToHsl = 11u,

    /// Turns hue, saturation and lightness into RGB.
    HslToRgb = 12u,

    /// Turns RGB into hue, saturation and value.
    RgbToHsv = 13u,

    /// Turns hue, saturation and value into RGB.
    HsvToRgb = 14u,

    /// Turns RGB into hue, saturation and intensity.
    RgbToHsi = 15u,

    /// Turns hue, saturation and intensity into RGB.
    HsiToRgb = 16u,

    /// Turns RGB into luma and two chroma differences.
    RgbToYCbCr = 17u,

    /// Turns luma and two chroma differences into RGB.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    YCbCrToRgb = 18u
};

/// Whether a value names a stage this build runs.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param stage the value to inspect.
/// @return true for every stage.
[[nodiscard]] constexpr bool isColorStage(ColorStage stage) noexcept
{
    return static_cast<std::uint8_t>(stage) <= static_cast<std::uint8_t>(ColorStage::YCbCrToRgb);
}

/// The stage's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param stage the stage to name. A value outside the enumeration is named `"UnknownColorStage"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* colorStageName(ColorStage stage) noexcept;

/// The capacity of a plan's stage list, a bound on the chain a conversion between two encodings can
/// need.
///
/// It counts every kind of step either end can contribute - unpack, cylindrical-to-RGB, decode, to
/// XYZ, adapt, from XYZ, encode, RGB-to-cylindrical, and the two perceptual steps - which is more
/// than one chain uses, because each end is either perceptual or RGB-family and never both; the
/// longest chain `planColorConversion` builds has seven stages. A fixed bound is why a plan holds its
/// stages in an array and allocates nothing.
inline constexpr std::uint32_t kMaxColorStages = 10u;

/// A validated conversion between two encodings.
///
/// Trivially copyable and allocation-free: the stages live in a fixed array and the matrices are
/// nine doubles each, so a plan may be built once and handed to as many executions as a caller
/// likes.
class ColorConversionPlan
{
public:
    /// Builds the plan from pieces `planColorConversion` has already validated.
    ///
    /// Public for the reason `PowerProgramPlan`'s constructor is: the free builder below is where
    /// the rules live, and this only records what it decided.
    ///
    /// @param source what the input's numbers are.
    /// @param destination what the output's numbers will be.
    /// @param stages the ordered steps, at most `kMaxColorStages` of them.
    /// @param toXyz the source primaries' matrix, or the identity when no stage reads it.
    /// @param adapt the white-point adaptation, or the identity when no stage reads it.
    /// @param fromXyz the destination primaries' inverse matrix, or the identity when no stage reads
    ///        it.
    /// @param linearRgb the composed source-linear-RGB to destination-linear-RGB matrix, or no value
    ///        when either end is not an RGB-family schema.
    AFTERNIGHTENGINE_API ColorConversionPlan(const ColorEncoding& source, const ColorEncoding& destination,
                                             std::span<const ColorStage> stages, const ColorMatrix3& toXyz,
                                             const ColorMatrix3& adapt, const ColorMatrix3& fromXyz,
                                             std::optional<ColorMatrix3> linearRgb) noexcept;

    /// What the input's numbers are.
    ///
    /// @return the source encoding.
    [[nodiscard]] AFTERNIGHTENGINE_API const ColorEncoding& source() const noexcept;

    /// What the output's numbers will be.
    ///
    /// @return the destination encoding.
    [[nodiscard]] AFTERNIGHTENGINE_API const ColorEncoding& destination() const noexcept;

    /// The ordered steps this plan runs.
    ///
    /// @return the stages, borrowed from this plan, in execution order. Empty for a plan that copies
    ///         its input, which `isIdentity` describes.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const ColorStage> stages() const noexcept;

    /// Whether this plan is a copy that changes no number.
    ///
    /// A plan has no stages when its two encodings are the same `Rgb`, `Rgba`, `Xyz` or natural-unit
    /// `Lab` encoding. Two equal cylindrical, luma-chroma, `Lch` or unit-packed encodings are not an
    /// identity: the plan still runs out to the family's canonical value and back.
    ///
    /// @return true when the plan has no stages.
    [[nodiscard]] AFTERNIGHTENGINE_API bool isIdentity() const noexcept;

    /// How many channels the source has.
    ///
    /// @return three, or four for `Rgba`.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t sourceChannelCount() const noexcept;

    /// How many channels the destination has.
    ///
    /// @return three, or four for `Rgba`.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t destinationChannelCount() const noexcept;

    /// Whether this plan carries an alpha channel through untouched.
    ///
    /// @return true exactly when both encodings are `Rgba`. A conversion that would have to invent
    ///         or discard opacity is refused rather than guessed, so there is no third answer.
    [[nodiscard]] AFTERNIGHTENGINE_API bool passesAlphaThrough() const noexcept;

    /// The matrix taking the source's linear RGB to the destination's linear RGB.
    ///
    /// Present whenever both encodings name an RGB channel schema, whatever their transfer
    /// functions. It is the *whole* of the conversion only when `isPureChannelMatrix` says so;
    /// otherwise the transfer stages sit around it.
    ///
    /// @return the composed matrix, or no value when either end is tristimulus, perceptual,
    ///         cylindrical or luma-chroma.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<ColorMatrix3> linearRgbMatrix() const noexcept;

    /// Whether this whole conversion is one channel matrix.
    ///
    /// The question a caller asks before lowering the conversion into a pointwise chain it is
    /// already running, rather than executing it as an image pass of its own.
    ///
    /// @return true when the plan's every stage is one of the three matrix stages.
    [[nodiscard]] AFTERNIGHTENGINE_API bool isPureChannelMatrix() const noexcept;

    /// The source primaries' matrix to XYZ.
    ///
    /// @return the matrix, or the identity when no stage reads it.
    [[nodiscard]] AFTERNIGHTENGINE_API const ColorMatrix3& rgbToXyz() const noexcept;

    /// The white-point adaptation.
    ///
    /// @return the matrix, or the identity when the two whites are the same.
    [[nodiscard]] AFTERNIGHTENGINE_API const ColorMatrix3& adaptation() const noexcept;

    /// The destination primaries' matrix from XYZ.
    ///
    /// @return the matrix, or the identity when no stage reads it.
    [[nodiscard]] AFTERNIGHTENGINE_API const ColorMatrix3& xyzToRgb() const noexcept;

private:
    /// What the input's numbers are.
    ColorEncoding m_source{};

    /// What the output's numbers will be.
    ColorEncoding m_destination{};

    /// The ordered steps, of which the first `m_stageCount` are meaningful.
    std::array<ColorStage, kMaxColorStages> m_stages{};

    /// How many of `m_stages` this plan runs.
    std::uint32_t m_stageCount = 0u;

    /// The source primaries' matrix to XYZ.
    ColorMatrix3 m_toXyz{};

    /// The white-point adaptation.
    ColorMatrix3 m_adapt{};

    /// The destination primaries' matrix from XYZ.
    ColorMatrix3 m_fromXyz{};

    /// The composed linear RGB to linear RGB matrix, when both ends are RGB-family.
    std::optional<ColorMatrix3> m_linearRgb{};
};

/// Entries in each real-time-preview colour power table.
///
/// 1,024 uniformly spaced entries, read by truncating to the lower index. The count is public so
/// benchmark and tolerance evidence can state exactly which approximation ran.
inline constexpr std::uint32_t kRealtimePreviewColorTableEntries = 1024u;

/// A colour conversion plus caller-owned real-time-preview power tables.
///
/// The plan is built once and reused across preview frames. It owns no global state and is safe to
/// read concurrently. Its three tables approximate exponents `1 / 3`, `1 / 2.4`, and `2.4` only
/// inside `[0, 1]`; outside that interval the scalar library is used.
/// Production conversion functions do not accept this type.
class RealtimePreviewColorConversionPlan
{
public:
    /// Builds the tables for an already validated production plan.
    ///
    /// Public so the free planner can return this value without hidden ownership. Callers normally
    /// use `planRealtimePreviewColorConversion`, which validates the encodings first.
    ///
    /// @param productionPlan the exact conversion whose sRGB and CIELAB power stages are approximated.
    AFTERNIGHTENGINE_API explicit RealtimePreviewColorConversionPlan(ColorConversionPlan productionPlan) noexcept;

    /// The exact conversion whose stage order and matrices this preview follows.
    ///
    /// @return the owned plan, borrowed for this preview plan's lifetime.
    [[nodiscard]] AFTERNIGHTENGINE_API const ColorConversionPlan& productionPlan() const noexcept;

    /// Evaluates the lower-neighbour approximation to `base^(1/3)`.
    ///
    /// Values from one up to but excluding `1.00001` are treated as one; values outside `[0, 1]` use the
    /// scalar library.
    ///
    /// @param base the power's base.
    /// @return the approximated or fallback value.
    [[nodiscard]] AFTERNIGHTENGINE_API double powerOneThird(double base) const noexcept;

    /// Evaluates the lower-neighbour approximation to `base^(1/2.4)`.
    ///
    /// @param base the power's base; the same endpoint and fallback rules as `powerOneThird` apply.
    /// @return the approximated or fallback value.
    [[nodiscard]] AFTERNIGHTENGINE_API double powerOneOverTwoPointFour(double base) const noexcept;

    /// Evaluates the lower-neighbour approximation to `base^2.4`.
    ///
    /// @param base the power's base; the same endpoint and fallback rules as `powerOneThird` apply.
    /// @return the approximated or fallback value.
    [[nodiscard]] AFTERNIGHTENGINE_API double powerTwoPointFour(double base) const noexcept;

    /// Largest sampled absolute error of the three tables against their analytic powers.
    ///
    /// Every interval is probed at eight evenly spaced positions when the plan is built. This is an
    /// accuracy report, not a production tolerance promise.
    ///
    /// @return the largest measured absolute error.
    [[nodiscard]] AFTERNIGHTENGINE_API double maximumAbsolutePowerError() const noexcept;

private:
    /// Exact stage order, encodings and matrices.
    ColorConversionPlan m_productionPlan;

    /// Lower-neighbour samples of exponent `1 / 3`.
    std::array<double, kRealtimePreviewColorTableEntries> m_oneThird{};

    /// Lower-neighbour samples of exponent `1 / 2.4`.
    std::array<double, kRealtimePreviewColorTableEntries> m_oneOverTwoPointFour{};

    /// Lower-neighbour samples of exponent `2.4`.
    std::array<double, kRealtimePreviewColorTableEntries> m_twoPointFour{};

    /// Measured worst power-table error.
    double m_maximumAbsolutePowerError = 0.0;
};

/// Plans the conversion between two stated encodings.
///
/// Both encodings are checked first, so a refusal names the encoding and the rule rather than a
/// stage that could not be built. A pair whose schemas carry different alpha - `Rgba` on one side
/// and a three-channel schema on the other - is refused, because discarding opacity and inventing it
/// are decisions with their own answers and neither belongs inside a colour transform.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param source what the input's numbers are.
/// @param destination what the output's numbers will be.
/// @return the plan; or `ErrorCode::InvalidArgument` carrying the `ColorEncodingError` that refused
///         one of the two encodings; or `ErrorCode::IncompatibleArguments` when the two schemas
///         disagree about alpha.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ColorConversionPlan> planColorConversion(const ColorEncoding& source,
                                                                                   const ColorEncoding& destination);

/// Plans a colour conversion for a disposable real-time preview.
///
/// The stage order, matrices, encoding validation and alpha rules are exactly the production
/// planner's. The returned wrapper additionally owns three coarse power tables.
/// Only sRGB transfer and the CIELAB forward compression use them; other stages remain exact.
///
/// Thread-safe after construction. Allocates only a failure it describes.
///
/// @param source what the preview input's numbers are.
/// @param destination what the preview output's numbers will be.
/// @return the preview plan, or any failure `planColorConversion` reports.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RealtimePreviewColorConversionPlan>
planRealtimePreviewColorConversion(const ColorEncoding& source, const ColorEncoding& destination);

/// Runs one plan over one pixel.
///
/// The independent scalar meaning of a conversion: this is what the image operation computes per
/// pixel, and what a test or an oracle compares against. It validates its spans on every call and is
/// not the shape to call once per pixel of a frame. One image path is not bit-identical to it: from
/// RGB through the sRGB transfer function to unit-packed `Lab` under the same white,
/// `convertImageColor` folds the white-point division into the matrix, which is the same transform
/// algebraically and can differ in the last bits.
///
/// Thread-safe and allocation-free apart from a failure it has to describe.
///
/// @param plan the validated conversion.
/// @param source exactly `plan.sourceChannelCount()` values in the source encoding.
/// @param destination exactly `plan.destinationChannelCount()` values, written on success.
/// @return success, or `ErrorCode::InvalidArgument` when either span is the wrong length.
[[nodiscard]] AFTERNIGHTENGINE_API Status convertColorPixel(const ColorConversionPlan& plan,
                                                            std::span<const double> source,
                                                            std::span<double> destination);

/// Runs one real-time-preview colour plan over one pixel.
///
/// This is the scalar meaning of the approximate path. Its span and alpha rules are the
/// production pixel conversion's; only the named power stages use the plan's lower-neighbour
/// tables. The result is for display preview only.
///
/// @param plan the validated preview conversion.
/// @param source exactly `plan.productionPlan().sourceChannelCount()` input values.
/// @param destination exactly `plan.productionPlan().destinationChannelCount()` writable values.
/// @return success, or `ErrorCode::InvalidArgument` when either span has the wrong length.
[[nodiscard]] AFTERNIGHTENGINE_API Status convertRealtimePreviewColorPixel(
    const RealtimePreviewColorConversionPlan& plan, std::span<const double> source, std::span<double> destination);

/// Runs one plan over a whole image.
///
/// The source and destination may hold their channels any way a layout can describe - one plane per
/// channel, one plane holding every channel, or a mixture - and a channel is addressed through that
/// arrangement rather than through an assumption about it. Strides may differ freely on both sides.
///
/// Exact aliasing is a supported in-place invocation, and needs the two layouts to be identical;
/// every other overlap is refused, because what a sample ended up as would otherwise depend on the
/// order the rows happened to be scheduled in.
///
/// Every stage computes in double and the result narrows once to the resident representation.
///
/// Thread-safe with respect to `execution` and the source, both of which are only read. Runs on the
/// threads the operation was admitted. Cancellation may leave part of the destination written: this
/// operation is not failure-atomic.
///
/// @param execution the operation this conversion belongs to. Must hold something.
/// @param plan the validated conversion.
/// @param sampleType what the samples are, on both sides. Float32 and Float64 only.
/// @param source where the source image's samples start. Must not be null.
/// @param sourceLayout where the source samples sit. Its channel count must be the plan's source
///        channel count and its element size that representation's.
/// @param destination where the converted samples go. Must not be null.
/// @param destinationLayout where they sit. Its channel count must be the plan's destination channel
///        count, and its width and height the source's.
/// @return success; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::UnsupportedBackend` when the operation is constrained to a backend this build
///         lacks; `ErrorCode::InvalidArgument` for a null buffer or a layout that does not describe the
///         stated representation; `ErrorCode::IncompatibleArguments` for a representation this
///         operation does not accept, a channel count or picture size that is not the plan's, or a
///         partial overlap; or `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the
///         operation was stopped.
[[nodiscard]] AFTERNIGHTENGINE_API Status convertImageColor(const ExecutionContext& execution,
                                                            const ColorConversionPlan& plan, SampleType sampleType,
                                                            const void* source, const ImageLayout& sourceLayout,
                                                            void* destination, const ImageLayout& destinationLayout);

/// Runs an approximate colour conversion over a disposable real-time preview image.
///
/// Geometry, representation, aliasing, scheduling and cancellation follow `convertImageColor`.
/// sRGB powers and CIELAB forward compression use the plan's 1,024-entry lower-neighbour tables;
/// no production entry point accepts this plan type.
///
/// @param execution the operation this preview conversion belongs to. Must hold something.
/// @param plan the validated real-time-preview conversion.
/// @param sampleType what the samples are, on both sides. Float32 and Float64 only.
/// @param source where the preview source starts. Must not be null.
/// @param sourceLayout where the source samples sit.
/// @param destination where the preview result goes. Must not be null.
/// @param destinationLayout where the destination samples sit and with the source's dimensions.
/// @return success, or the representation, layout, aliasing, cancellation and deadline failures
///         documented by `convertImageColor`.
[[nodiscard]] AFTERNIGHTENGINE_API Status convertRealtimePreviewImageColor(
    const ExecutionContext& execution, const RealtimePreviewColorConversionPlan& plan, SampleType sampleType,
    const void* source, const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout);

/// What an image's description becomes when this plan has run over it.
///
/// The metadata half of a colour transform, and the reason it is a function rather than a comment:
/// the schema, the basis and the numeric domain all change together, and an image whose samples were
/// converted while its description was not is an image nothing downstream can read correctly.
///
/// The source description is checked against the plan's source encoding, so a plan built for linear
/// sRGB cannot be used to re-describe an image that says it is already encoded.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param source the description the image carries now.
/// @param plan the validated conversion.
/// @return the description the converted image carries; `ErrorCode::IncompatibleArguments` when the
///         source description is not what the plan's source encoding says; or
///         `ErrorCode::InvalidArgument` when the destination description this would produce is one
///         no image can have.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageDescriptor> colorTransformedDescriptor(const ImageDescriptor& source,
                                                                                      const ColorConversionPlan& plan);

/// Lowers an arbitrary square channel matrix into a fusable pointwise program.
///
/// The general case behind the colour matrices: every channel of the output is a linear combination
/// of every channel of the input, held in the program's own table, and the step composes with
/// whatever else the chain does.
///
/// @param shape the floating representation, domain, channels and arithmetic width.
/// @param rowMajor exactly `shape.channelCount * shape.channelCount` finite coefficients, row by
///        row, borrowed for the call.
/// @return the program; `ErrorCode::InvalidArgument` when a coefficient is not finite or the numeric
///         domain is outside its enumeration; `ErrorCode::IncompatibleArguments` for a non-floating
///         representation or a coefficient count that is not the square of the channel count; or the
///         pointwise builder's precision, channel or domain refusal.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PointwiseProgram> buildChannelMatrixProgram(const PointwisePlanShape& shape,
                                                                                      std::span<const double> rowMajor);

/// Lowers a three by three colour matrix into a fusable pointwise program.
///
/// The named spelling of `buildChannelMatrixProgram` for the colour case, which fixes the channel
/// count at three and is what a plan reporting `isPureChannelMatrix` is executed through.
///
/// @param shape the floating representation, domain and arithmetic width. Its channel count must be
///        three.
/// @param matrix the coefficients.
/// @return the program, or the same failures as `buildChannelMatrixProgram`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PointwiseProgram> buildColorMatrixProgram(const PointwisePlanShape& shape,
                                                                                    const ColorMatrix3& matrix);

/// What running this plan does to the image it writes.
///
/// A declaration rather than a constant, because a plan with no stages is a copy and a plan between
/// two different encodings changes almost everything a description says. A real conversion
/// restates the description - `colorTransformedDescriptor` is where the caller gets it - and
/// restates the meaning, because the destination encoding's transfer function is what the result's
/// samples have now been through. The source header cards and any decision about how the image is
/// written are both withdrawn, since each described the encoding that has just been left behind.
///
/// The validity is preserved: a colour transform reads and writes the same pixels, and a sample that
/// meant nothing before means nothing after.
///
/// An astrometric solution survives. No stage of a colour conversion moves a pixel.
///
/// Thread-safe and allocation-free.
///
/// @param plan the conversion that is about to run, or that just did.
/// @return the declaration for that plan; every field preserved when the plan is the identity.
///         Always consistent, so `checkOperationEffect` on the result reports
///         `OperationEffectError::None`.
[[nodiscard]] AFTERNIGHTENGINE_API OperationEffect colorConversionEffect(const ColorConversionPlan& plan) noexcept;

} // namespace an
