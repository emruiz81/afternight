// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Every function this language has, stated once and immutable.
///
/// Which functions exist is a property of the language, not of the process, so the answer to "is this
/// name a function" cannot depend on when it is asked: this registry is a constant table with one row
/// per name, resolved by a lookup that allocates nothing and changes nothing.
///
/// A name may denote two functions, and that is a property of the language: `med(a, b, c)` is the
/// median of three numbers and `med(IMG)` is the median of an
/// image. They are different computations over different inputs and they are separate rows here, so
/// the resolver picks by what the argument *is* rather than by re-deriving the rule at each call
/// site. Everything else about a call - how many arguments it takes, whether the first one has to
/// name an image, whether the arguments are all evaluated - is a fact on the row rather than a check
/// somewhere else.
///
/// A row may also say that this build knows the name and cannot compute it. That is deliberately not
/// the same as an unknown name: an expression using it is not wrong, and the person who wrote it
/// needs to be told which estimator is missing rather than that they made a typo.

#pragma once

#include <AfterNightEngine/Core/Api.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace an
{

/// One callable form of this language.
///
/// A closed enumeration with one value per computation rather than per name, so two spellings of
/// one function share a value and one spelling of two functions does not. The ordering carries no
/// meaning.
enum class PixelMathFunctionId : std::uint16_t
{
    /// `abs(x)`, the magnitude.
    AbsoluteValue = 0u,

    /// `acos(x)`, in radians.
    ArcCosine,

    /// `acosh(x)`.
    ArcHyperbolicCosine,

    /// `asin(x)`, in radians.
    ArcSine,

    /// `asinh(x)`.
    ArcHyperbolicSine,

    /// `atan(x)`, in radians.
    ArcTangent,

    /// `atan2(y, x)`, in radians, quadrant-aware.
    ArcTangentOfRatio,

    /// `atanh(x)`.
    ArcHyperbolicTangent,

    /// `ceil(x)`, the smallest integer at or above `x`.
    Ceiling,

    /// `clamp(x, low, high)`, also spelled `range`.
    Clamp,

    /// `cos(x)`, `x` in radians.
    Cosine,

    /// `cosh(x)`.
    HyperbolicCosine,

    /// `e()`, the base of the natural logarithm.
    EulerNumber,

    /// `exp(x)`.
    Exponential,

    /// `fac(x)`, the factorial of the truncated argument: a not-a-number for a negative argument and
    /// infinity above 170.
    Factorial,

    /// `floor(x)`, the largest integer at or below `x`.
    Floor,

    /// `ln(x)`, the natural logarithm.
    NaturalLogarithm,

    /// `log(x)`, also spelled `log10`; the base-ten logarithm.
    CommonLogarithm,

    /// `log2(x)`.
    BinaryLogarithm,

    /// `mtf(midtone, x)`, the midtone transfer function.
    MidtoneTransfer,

    /// `ncr(n, r)`, the number of combinations.
    Combinations,

    /// `npr(n, r)`, the number of permutations.
    Permutations,

    /// `pi()`.
    Pi,

    /// `sign(x)`, one of minus one, zero and one; zero for a not-a-number.
    Sign,

    /// `sqrt(x)`.
    SquareRoot,

    /// `sin(x)`, `x` in radians.
    Sine,

    /// `sinh(x)`.
    HyperbolicSine,

    /// `tan(x)`, `x` in radians.
    Tangent,

    /// `tanh(x)`.
    HyperbolicTangent,

    /// `trunc(x)`, toward zero.
    Truncate,

    /// `max(a, b, ...)` over the argument list.
    MaximumOfArguments,

    /// `min(a, b, ...)` over the argument list.
    MinimumOfArguments,

    /// `mean(a, b, ...)` over the argument list.
    MeanOfArguments,

    /// `med(a, b, c, ...)` over the argument list.
    MedianOfArguments,

    /// `mdev(a, b, ...)`, also spelled `mad`; the median of the absolute deviations from the
    /// argument list's median.
    MedianAbsoluteDeviationOfArguments,

    /// `sdev(a, b, ...)`, the population standard deviation of the argument list.
    StandardDeviationOfArguments,

    /// `var(a, b, ...)`, the population variance of the argument list.
    VarianceOfArguments,

    /// `sum(a, b, ...)` over the argument list.
    SumOfArguments,

    /// `asqr(a, b, ...)`, the mean of the arguments' squares.
    MeanOfSquaresOfArguments,

    /// `ssqr(a, b, ...)`, the sum of the arguments' squares.
    SumOfSquaresOfArguments,

    /// `bwmv(a, b, ...)`, the biweight midvariance of the argument list.
    BiweightMidvarianceOfArguments,

    /// `min(IMG)`, the smallest selected sample of the image's current channel.
    MinimumOfImage,

    /// `max(IMG)`, the largest selected sample of the image's current channel.
    MaximumOfImage,

    /// `mean(IMG)`, the mean of the image's current channel.
    MeanOfImage,

    /// `med(IMG)`, the median of the image's current channel.
    MedianOfImage,

    /// `mdev(IMG)`, also spelled `mad`; the median absolute deviation of the image's current
    /// channel.
    MedianAbsoluteDeviationOfImage,

    /// `sdev(IMG)`, the population standard deviation of the image's current channel.
    StandardDeviationOfImage,

    /// `var(IMG)`, the population variance of the image's current channel.
    VarianceOfImage,

    /// `adev(IMG)`, the average absolute deviation from the image mean.
    AverageAbsoluteDeviationOfImage,

    /// `asqr(IMG)`, the mean of the image's squared samples.
    MeanOfSquaresOfImage,

    /// `ssqr(IMG)`, the sum of the image's squared samples.
    SumOfSquaresOfImage,

    /// `bwmv(IMG)`, the image's biweight midvariance.
    BiweightMidvarianceOfImage,

    /// `noise(IMG)`, the image's robust row-wise first-difference background noise.
    BackgroundNoiseOfImage,

    /// `w(IMG)`, also spelled `width`; the image's width in pixels.
    ImageWidth,

    /// `h(IMG)`, also spelled `height`; the image's height in pixels.
    ImageHeight,

    /// `n(IMG)`, also spelled `channels`; how many channels the image has.
    ImageChannelCount,

    /// `iscolor(IMG)`, one when the image has three or more channels.
    ImageIsColor,

    /// `CIEL(IMG)`, the pixel's lightness, packed as `PerceptualEncoding::UnitPacked` packs it.
    CieLightness,

    /// `CIEa(IMG)`, the pixel's green-red opponent channel, packed as `PerceptualEncoding::UnitPacked`
    /// packs it.
    CieGreenRed,

    /// `CIEb(IMG)`, the pixel's blue-yellow opponent channel, packed as `PerceptualEncoding::UnitPacked`
    /// packs it.
    CieBlueYellow,

    /// `CIEc(IMG)`, the pixel's chroma, packed as `PerceptualEncoding::UnitPacked` packs it.
    CieChroma,

    /// `CIEh(IMG)`, the pixel's hue as a fraction of a turn.
    CieHue,

    /// `CIEX(IMG)`, the pixel's `X` tristimulus value.
    CieX,

    /// `CIEY(IMG)`, the pixel's `Y` tristimulus value.
    CieY,

    /// `CIEZ(IMG)`, the pixel's `Z` tristimulus value.
    CieZ,

    /// `HSLh(IMG)`, the pixel's hue as a fraction of one turn.
    HslHue,

    /// `HSLs(IMG)`, the pixel's saturation in the cylindrical lightness model.
    HslSaturation,

    /// `HSLl(IMG)`, the pixel's lightness in the cylindrical lightness model.
    HslLightness,

    /// `HSVh(IMG)`, the pixel's hue as a fraction of one turn.
    HsvHue,

    /// `HSVs(IMG)`, the pixel's saturation in the cylindrical value model.
    HsvSaturation,

    /// `HSVv(IMG)`, the pixel's value in the cylindrical value model.
    HsvValue,

    /// `YCbCrY(IMG)`, the pixel's luma.
    LumaY,

    /// `YCbCrCb(IMG)`, the pixel's blue-difference chroma, centred on one half.
    ChromaBlue,

    /// `YCbCrCr(IMG)`, the pixel's red-difference chroma, centred on one half.
    ChromaRed,

    /// `c()`, also spelled `channel`; which channel is being written.
    CurrentChannel,

    /// `x()`, also spelled `xpos`; the column being written.
    CurrentColumn,

    /// `y()`, also spelled `ypos`; the row being written.
    CurrentRow,

    /// `X()`, the column divided by one less than the target's width, so zero at the first column
    /// and one at the last; zero for a one-column target.
    NormalizedColumn,

    /// `Y()`, the row divided by one less than the target's height, so zero at the first row and
    /// one at the last; zero for a one-row target.
    NormalizedRow,

    /// `inrect(x, y, width, height)`, one when the pixel is inside the rectangle and zero elsewhere.
    ///
    /// The four numbers are truncated toward zero and the rectangle holds both of its edges, so a
    /// width of `w` spans `w + 1` columns; a number that is not finite holds no pixel.
    InRectangle,

    /// `inellipse(centreX, centreY, radiusX, radiusY)`, one when the pixel is inside the ellipse: where
    /// `((x - centreX) / radiusX)^2 + ((y - centreY) / radiusY)^2` is at most one, at the pixel's column
    /// and row.
    InEllipse,

    /// `pixel(IMG, x, y)` or `pixel(IMG, x, y, channel)`, also spelled `p`; a stated sample of a
    /// bound image.
    ///
    /// The coordinates and channel are truncated toward zero, with no interpolation. A coordinate
    /// that is negative or not finite, or that lies outside the image, reads zero, and so does a
    /// channel the image does not have. Without a channel argument the channel being written is
    /// read, or the only channel of a one-channel image.
    SampleAt,

    /// `iif(test, whenTrue, whenFalse)`. Only the chosen branch is evaluated.
    Conditional,

    /// `random()`, also spelled `rand`; a value in `[0, 1)` from the execution's own stream.
    ///
    /// Deliberately the last enumerator, so a table with one entry per function needs no repeated
    /// list.
    UniformRandom
};

/// How many callable forms this build knows.
///
/// `UniformRandom` is last, which is what this depends on.
inline constexpr std::size_t kPixelMathFunctionCount =
    static_cast<std::size_t>(PixelMathFunctionId::UniformRandom) + 1u;

/// What kind of thing a function computes.
///
/// The classes are what the front end and the compiler branch on: whether a call has to be evaluated
/// per pixel, once per image, or once for the whole run is decided here rather than function by
/// function.
enum class PixelMathFunctionClass : std::uint8_t
{
    /// A constant of the language. Evaluated once.
    Constant = 0u,

    /// A pure function of its arguments' values.
    Scalar = 1u,

    /// A statistic of the whole argument list, whose length is not fixed.
    Aggregate = 2u,

    /// A statistic of a whole bound image, computed once before any pixel is written.
    ImageStatistic = 3u,

    /// A fact about a bound image's geometry. Evaluated once.
    ImageProperty = 4u,

    /// A colour component of the current pixel of a bound image.
    ColorComponent = 5u,

    /// A function of where in the destination the current pixel is.
    Position = 6u,

    /// A read of a bound image at coordinates the expression states.
    ImageSample = 7u,

    /// A choice between two expressions, only one of which is evaluated.
    Branch = 8u,

    /// A draw from the execution's random stream.
    Stochastic = 9u
};

/// Whether a value names a function class this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param functionClass the value to inspect.
/// @return true for every supported class.
[[nodiscard]] constexpr bool isPixelMathFunctionClass(PixelMathFunctionClass functionClass) noexcept
{
    return static_cast<std::uint8_t>(functionClass) <= static_cast<std::uint8_t>(PixelMathFunctionClass::Stochastic);
}

/// The value `PixelMathFunctionSignature::maximumArguments` carries for a variadic function.
inline constexpr std::uint32_t kUnboundedPixelMathArgumentCount = 0xFFFFFFFFu;

/// Everything the front end has to know about one callable form.
///
/// A plain aggregate, so the table below reads as a table.
struct PixelMathFunctionSignature
{
    /// Which computation this row is.
    PixelMathFunctionId id = PixelMathFunctionId::AbsoluteValue;

    /// What kind of thing it computes.
    PixelMathFunctionClass functionClass = PixelMathFunctionClass::Scalar;

    /// The fewest arguments it accepts.
    std::uint32_t minimumArguments = 1u;

    /// The most it accepts, or `kUnboundedPixelMathArgumentCount` when there is no maximum.
    std::uint32_t maximumArguments = 1u;

    /// Whether the first argument, when one is written, has to name an image rather than compute a
    /// number.
    bool firstArgumentNamesImage = false;

    /// Whether every argument is evaluated. False for the conditional, whose unchosen branch is
    /// not evaluated at all - which is what makes `iif(b != 0, a / b, 0)` safe to write.
    bool evaluatesEveryArgument = true;

    /// Whether this build can compute it. A row that says no is a name the language has and this
    /// build has no estimator for; every row of this build says yes.
    bool available = true;
};

/// One name and what it denotes.
struct PixelMathFunctionEntry
{
    /// The written spelling. Case-sensitive, and borrowed from a string literal that outlives any
    /// caller.
    std::string_view name{};

    /// What a call of that name computes.
    PixelMathFunctionSignature signature{};
};

/// Every name the language answers to other than as an image statistic, in ascending order of
/// spelling.
///
/// Borrowed from a table with static storage duration; the span and the names it holds outlive any
/// caller. A name that also denotes an image statistic appears here once, with its argument-list
/// form; ask `findPixelMathImageStatistic` for the other one. `adev` and `noise` have only an image
/// form and appear only in `pixelMathImageStatisticRegistry`.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @return the registry.
[[nodiscard]] AFTERNIGHTENGINE_API std::span<const PixelMathFunctionEntry> pixelMathFunctionRegistry() noexcept;

/// Every name that denotes a statistic of a whole image, in ascending order of spelling.
///
/// Borrowed from a table with static storage duration.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @return the image-statistic registry.
[[nodiscard]] AFTERNIGHTENGINE_API std::span<const PixelMathFunctionEntry> pixelMathImageStatisticRegistry() noexcept;

/// What a name denotes when it is not called as an image statistic.
///
/// The resolver asks `findPixelMathImageStatistic` first when a call's one argument names an image,
/// and this otherwise, so `w(IMG)` is answered here and `med(IMG)` is not.
///
/// Thread-safe, reentrant, and allocation-free. The returned pointer is into a table with static
/// storage duration and outlives any caller.
///
/// @param name the written spelling, compared exactly.
/// @return the signature, or null when no such function exists.
[[nodiscard]] AFTERNIGHTENGINE_API const PixelMathFunctionSignature*
findPixelMathFunction(std::string_view name) noexcept;

/// What a name denotes when it is called with exactly one image reference.
///
/// Thread-safe, reentrant, and allocation-free. The returned pointer is into a table with static
/// storage duration and outlives any caller.
///
/// @param name the written spelling, compared exactly.
/// @return the signature, or null when the name denotes no image statistic.
[[nodiscard]] AFTERNIGHTENGINE_API const PixelMathFunctionSignature*
findPixelMathImageStatistic(std::string_view name) noexcept;

/// The computation's stable name, for diagnostics and tests.
///
/// One name per computation: for a function with two spellings, the one its enumerator lists first
/// (`pixel` rather than `p`, `mdev` rather than `mad`), and for an image statistic its spelling
/// followed by `(image)`, as in `med(image)`, so it cannot be mistaken for the argument-list form.
///
/// Thread-safe, reentrant, and allocation-free. The returned pointer is a string literal that
/// outlives any caller.
///
/// @param id the computation, including values this build does not know.
/// @return the name, or "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* pixelMathFunctionIdName(PixelMathFunctionId id) noexcept;

/// Whether a name belongs to the language rather than to whoever is binding names to it.
///
/// True for every function spelling and for the three meta symbols. A caller binding an image or a
/// symbol asks this first, because a binding that shadowed `sqrt` would make an expression mean one
/// thing on one call and another on the next.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param name the spelling a caller wants to bind.
/// @return true when the language already owns it.
[[nodiscard]] AFTERNIGHTENGINE_API bool isPixelMathReservedName(std::string_view name) noexcept;

} // namespace an
