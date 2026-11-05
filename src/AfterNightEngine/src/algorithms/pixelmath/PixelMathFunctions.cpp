// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the constant function table, and the two lookups that read it.
//
// Two tables, both sorted by spelling and binary-searched: every name a call or a symbol can
// denote, and the names that denote a statistic of a whole image. Several spellings are aliases of
// one function - `log` and `log10`, `mad` and `mdev`, `range` and `clamp`, `rand` and `random`,
// `p` and `pixel`, and the short and long names of the position and image-property symbols - and
// `pixelMathFunctionIdName` answers each function's one canonical spelling. `log` is the common
// logarithm; `ln` is the natural one.

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathFunctions.h>

#include <algorithm>
#include <array>

namespace an
{
namespace
{

using Id = PixelMathFunctionId;
using Class = PixelMathFunctionClass;

// The row shapes, so that the tables below read as tables rather than as sixty constructor calls
// with eight fields each. Every function of one shape differs from every other only in its name, its
// identity and its argument counts, and a shape that had to be written out per row is a shape that
// would eventually be written out wrongly.

[[nodiscard]] constexpr PixelMathFunctionEntry scalar(std::string_view name, Id id, std::uint32_t minimum,
                                                      std::uint32_t maximum) noexcept
{
    return PixelMathFunctionEntry{
        name, PixelMathFunctionSignature{
                  .id = id, .functionClass = Class::Scalar, .minimumArguments = minimum, .maximumArguments = maximum}};
}

[[nodiscard]] constexpr PixelMathFunctionEntry constant(std::string_view name, Id id) noexcept
{
    return PixelMathFunctionEntry{
        name, PixelMathFunctionSignature{
                  .id = id, .functionClass = Class::Constant, .minimumArguments = 0u, .maximumArguments = 0u}};
}

[[nodiscard]] constexpr PixelMathFunctionEntry aggregate(std::string_view name, Id id, std::uint32_t minimum) noexcept
{
    return PixelMathFunctionEntry{name,
                                  PixelMathFunctionSignature{.id = id,
                                                             .functionClass = Class::Aggregate,
                                                             .minimumArguments = minimum,
                                                             .maximumArguments = kUnboundedPixelMathArgumentCount}};
}

[[nodiscard]] constexpr PixelMathFunctionEntry position(std::string_view name, Id id) noexcept
{
    return PixelMathFunctionEntry{
        name, PixelMathFunctionSignature{
                  .id = id, .functionClass = Class::Position, .minimumArguments = 0u, .maximumArguments = 0u}};
}

[[nodiscard]] constexpr PixelMathFunctionEntry region(std::string_view name, Id id) noexcept
{
    return PixelMathFunctionEntry{
        name, PixelMathFunctionSignature{
                  .id = id, .functionClass = Class::Position, .minimumArguments = 4u, .maximumArguments = 4u}};
}

[[nodiscard]] constexpr PixelMathFunctionEntry imageProperty(std::string_view name, Id id) noexcept
{
    return PixelMathFunctionEntry{name, PixelMathFunctionSignature{.id = id,
                                                                   .functionClass = Class::ImageProperty,
                                                                   .minimumArguments = 0u,
                                                                   .maximumArguments = 1u,
                                                                   .firstArgumentNamesImage = true}};
}

[[nodiscard]] constexpr PixelMathFunctionEntry colorComponent(std::string_view name, Id id) noexcept
{
    return PixelMathFunctionEntry{name, PixelMathFunctionSignature{.id = id,
                                                                   .functionClass = Class::ColorComponent,
                                                                   .minimumArguments = 0u,
                                                                   .maximumArguments = 1u,
                                                                   .firstArgumentNamesImage = true}};
}

[[nodiscard]] constexpr PixelMathFunctionEntry sampling(std::string_view name) noexcept
{
    return PixelMathFunctionEntry{name, PixelMathFunctionSignature{.id = Id::SampleAt,
                                                                   .functionClass = Class::ImageSample,
                                                                   .minimumArguments = 3u,
                                                                   .maximumArguments = 4u,
                                                                   .firstArgumentNamesImage = true}};
}

[[nodiscard]] constexpr PixelMathFunctionEntry stochastic(std::string_view name) noexcept
{
    return PixelMathFunctionEntry{name, PixelMathFunctionSignature{.id = Id::UniformRandom,
                                                                   .functionClass = Class::Stochastic,
                                                                   .minimumArguments = 0u,
                                                                   .maximumArguments = 0u}};
}

[[nodiscard]] constexpr PixelMathFunctionEntry branch(std::string_view name) noexcept
{
    return PixelMathFunctionEntry{name, PixelMathFunctionSignature{.id = Id::Conditional,
                                                                   .functionClass = Class::Branch,
                                                                   .minimumArguments = 3u,
                                                                   .maximumArguments = 3u,
                                                                   .firstArgumentNamesImage = false,
                                                                   .evaluatesEveryArgument = false}};
}

[[nodiscard]] constexpr PixelMathFunctionEntry imageStatistic(std::string_view name, Id id, bool available) noexcept
{
    return PixelMathFunctionEntry{name, PixelMathFunctionSignature{.id = id,
                                                                   .functionClass = Class::ImageStatistic,
                                                                   .minimumArguments = 1u,
                                                                   .maximumArguments = 1u,
                                                                   .firstArgumentNamesImage = true,
                                                                   .evaluatesEveryArgument = true,
                                                                   .available = available}};
}

// Every name the language answers to when its arguments are not one image reference, in ascending
// order of spelling. The order is what the lookup binary-searches, and a suite case walks the whole
// table asserting it, so a row inserted in the wrong place fails a test rather than making one name
// unreachable.
constexpr auto kFunctions = std::to_array<PixelMathFunctionEntry>({
    colorComponent("CIEL", Id::CieLightness),
    colorComponent("CIEX", Id::CieX),
    colorComponent("CIEY", Id::CieY),
    colorComponent("CIEZ", Id::CieZ),
    colorComponent("CIEa", Id::CieGreenRed),
    colorComponent("CIEb", Id::CieBlueYellow),
    colorComponent("CIEc", Id::CieChroma),
    colorComponent("CIEh", Id::CieHue),
    colorComponent("HSLh", Id::HslHue),
    colorComponent("HSLl", Id::HslLightness),
    colorComponent("HSLs", Id::HslSaturation),
    colorComponent("HSVh", Id::HsvHue),
    colorComponent("HSVs", Id::HsvSaturation),
    colorComponent("HSVv", Id::HsvValue),
    position("X", Id::NormalizedColumn),
    position("Y", Id::NormalizedRow),
    colorComponent("YCbCrCb", Id::ChromaBlue),
    colorComponent("YCbCrCr", Id::ChromaRed),
    colorComponent("YCbCrY", Id::LumaY),
    scalar("abs", Id::AbsoluteValue, 1u, 1u),
    scalar("acos", Id::ArcCosine, 1u, 1u),
    scalar("acosh", Id::ArcHyperbolicCosine, 1u, 1u),
    scalar("asin", Id::ArcSine, 1u, 1u),
    scalar("asinh", Id::ArcHyperbolicSine, 1u, 1u),
    aggregate("asqr", Id::MeanOfSquaresOfArguments, 2u),
    scalar("atan", Id::ArcTangent, 1u, 1u),
    scalar("atan2", Id::ArcTangentOfRatio, 2u, 2u),
    scalar("atanh", Id::ArcHyperbolicTangent, 1u, 1u),
    aggregate("bwmv", Id::BiweightMidvarianceOfArguments, 2u),
    position("c", Id::CurrentChannel),
    scalar("ceil", Id::Ceiling, 1u, 1u),
    position("channel", Id::CurrentChannel),
    imageProperty("channels", Id::ImageChannelCount),
    scalar("clamp", Id::Clamp, 3u, 3u),
    scalar("cos", Id::Cosine, 1u, 1u),
    scalar("cosh", Id::HyperbolicCosine, 1u, 1u),
    constant("e", Id::EulerNumber),
    scalar("exp", Id::Exponential, 1u, 1u),
    scalar("fac", Id::Factorial, 1u, 1u),
    scalar("floor", Id::Floor, 1u, 1u),
    imageProperty("h", Id::ImageHeight),
    imageProperty("height", Id::ImageHeight),
    branch("iif"),
    region("inellipse", Id::InEllipse),
    region("inrect", Id::InRectangle),
    imageProperty("iscolor", Id::ImageIsColor),
    scalar("ln", Id::NaturalLogarithm, 1u, 1u),
    scalar("log", Id::CommonLogarithm, 1u, 1u),
    scalar("log10", Id::CommonLogarithm, 1u, 1u),
    scalar("log2", Id::BinaryLogarithm, 1u, 1u),
    aggregate("mad", Id::MedianAbsoluteDeviationOfArguments, 2u),
    aggregate("max", Id::MaximumOfArguments, 2u),
    aggregate("mdev", Id::MedianAbsoluteDeviationOfArguments, 2u),
    aggregate("mean", Id::MeanOfArguments, 2u),
    aggregate("med", Id::MedianOfArguments, 3u),
    aggregate("min", Id::MinimumOfArguments, 2u),
    scalar("mtf", Id::MidtoneTransfer, 2u, 2u),
    imageProperty("n", Id::ImageChannelCount),
    scalar("ncr", Id::Combinations, 2u, 2u),
    scalar("npr", Id::Permutations, 2u, 2u),
    sampling("p"),
    constant("pi", Id::Pi),
    sampling("pixel"),
    stochastic("rand"),
    stochastic("random"),
    scalar("range", Id::Clamp, 3u, 3u),
    aggregate("sdev", Id::StandardDeviationOfArguments, 2u),
    scalar("sign", Id::Sign, 1u, 1u),
    scalar("sin", Id::Sine, 1u, 1u),
    scalar("sinh", Id::HyperbolicSine, 1u, 1u),
    scalar("sqrt", Id::SquareRoot, 1u, 1u),
    aggregate("ssqr", Id::SumOfSquaresOfArguments, 2u),
    aggregate("sum", Id::SumOfArguments, 2u),
    scalar("tan", Id::Tangent, 1u, 1u),
    scalar("tanh", Id::HyperbolicTangent, 1u, 1u),
    scalar("trunc", Id::Truncate, 1u, 1u),
    aggregate("var", Id::VarianceOfArguments, 2u),
    imageProperty("w", Id::ImageWidth),
    imageProperty("width", Id::ImageWidth),
    position("x", Id::CurrentColumn),
    position("xpos", Id::CurrentColumn),
    position("y", Id::CurrentRow),
    position("ypos", Id::CurrentRow),
});

// Every name that denotes a statistic of a whole image, in ascending order of spelling. A name here
// is reached only when the call has exactly one argument and that argument names an image, which is
// what lets `med` be the median of three numbers and the median of a frame without either meaning
// being a special case at the call site.
//
// The advanced rows are backed by the same published statistics operation used outside PixelMath.
// In particular, the square names accumulate squares directly rather than deriving them from the
// variance.
constexpr auto kImageStatistics = std::to_array<PixelMathFunctionEntry>({
    imageStatistic("adev", Id::AverageAbsoluteDeviationOfImage, true),
    imageStatistic("asqr", Id::MeanOfSquaresOfImage, true),
    imageStatistic("bwmv", Id::BiweightMidvarianceOfImage, true),
    imageStatistic("mad", Id::MedianAbsoluteDeviationOfImage, true),
    imageStatistic("max", Id::MaximumOfImage, true),
    imageStatistic("mdev", Id::MedianAbsoluteDeviationOfImage, true),
    imageStatistic("mean", Id::MeanOfImage, true),
    imageStatistic("med", Id::MedianOfImage, true),
    imageStatistic("min", Id::MinimumOfImage, true),
    imageStatistic("noise", Id::BackgroundNoiseOfImage, true),
    imageStatistic("sdev", Id::StandardDeviationOfImage, true),
    imageStatistic("ssqr", Id::SumOfSquaresOfImage, true),
    imageStatistic("var", Id::VarianceOfImage, true),
});

[[nodiscard]] const PixelMathFunctionSignature* lookup(std::span<const PixelMathFunctionEntry> table,
                                                       std::string_view name) noexcept
{
    const auto found = std::lower_bound(
        table.begin(), table.end(), name,
        [](const PixelMathFunctionEntry& entry, std::string_view wanted) { return entry.name < wanted; });
    if (found == table.end() || found->name != name)
    {
        return nullptr;
    }
    return &found->signature;
}

} // namespace

std::span<const PixelMathFunctionEntry> pixelMathFunctionRegistry() noexcept
{
    return std::span<const PixelMathFunctionEntry>(kFunctions);
}

std::span<const PixelMathFunctionEntry> pixelMathImageStatisticRegistry() noexcept
{
    return std::span<const PixelMathFunctionEntry>(kImageStatistics);
}

const PixelMathFunctionSignature* findPixelMathFunction(std::string_view name) noexcept
{
    return lookup(pixelMathFunctionRegistry(), name);
}

const PixelMathFunctionSignature* findPixelMathImageStatistic(std::string_view name) noexcept
{
    return lookup(pixelMathImageStatisticRegistry(), name);
}

const char* pixelMathFunctionIdName(PixelMathFunctionId id) noexcept
{
    switch (id)
    {
    case Id::AbsoluteValue:
        return "abs";
    case Id::ArcCosine:
        return "acos";
    case Id::ArcHyperbolicCosine:
        return "acosh";
    case Id::ArcSine:
        return "asin";
    case Id::ArcHyperbolicSine:
        return "asinh";
    case Id::ArcTangent:
        return "atan";
    case Id::ArcTangentOfRatio:
        return "atan2";
    case Id::ArcHyperbolicTangent:
        return "atanh";
    case Id::Ceiling:
        return "ceil";
    case Id::Clamp:
        return "clamp";
    case Id::Cosine:
        return "cos";
    case Id::HyperbolicCosine:
        return "cosh";
    case Id::EulerNumber:
        return "e";
    case Id::Exponential:
        return "exp";
    case Id::Factorial:
        return "fac";
    case Id::Floor:
        return "floor";
    case Id::NaturalLogarithm:
        return "ln";
    case Id::CommonLogarithm:
        return "log";
    case Id::BinaryLogarithm:
        return "log2";
    case Id::MidtoneTransfer:
        return "mtf";
    case Id::Combinations:
        return "ncr";
    case Id::Permutations:
        return "npr";
    case Id::Pi:
        return "pi";
    case Id::Sign:
        return "sign";
    case Id::SquareRoot:
        return "sqrt";
    case Id::Sine:
        return "sin";
    case Id::HyperbolicSine:
        return "sinh";
    case Id::Tangent:
        return "tan";
    case Id::HyperbolicTangent:
        return "tanh";
    case Id::Truncate:
        return "trunc";
    case Id::MaximumOfArguments:
        return "max";
    case Id::MinimumOfArguments:
        return "min";
    case Id::MeanOfArguments:
        return "mean";
    case Id::MedianOfArguments:
        return "med";
    case Id::MedianAbsoluteDeviationOfArguments:
        return "mdev";
    case Id::StandardDeviationOfArguments:
        return "sdev";
    case Id::VarianceOfArguments:
        return "var";
    case Id::SumOfArguments:
        return "sum";
    case Id::MeanOfSquaresOfArguments:
        return "asqr";
    case Id::SumOfSquaresOfArguments:
        return "ssqr";
    case Id::BiweightMidvarianceOfArguments:
        return "bwmv";
    case Id::MinimumOfImage:
        return "min(image)";
    case Id::MaximumOfImage:
        return "max(image)";
    case Id::MeanOfImage:
        return "mean(image)";
    case Id::MedianOfImage:
        return "med(image)";
    case Id::MedianAbsoluteDeviationOfImage:
        return "mdev(image)";
    case Id::StandardDeviationOfImage:
        return "sdev(image)";
    case Id::VarianceOfImage:
        return "var(image)";
    case Id::AverageAbsoluteDeviationOfImage:
        return "adev(image)";
    case Id::MeanOfSquaresOfImage:
        return "asqr(image)";
    case Id::SumOfSquaresOfImage:
        return "ssqr(image)";
    case Id::BiweightMidvarianceOfImage:
        return "bwmv(image)";
    case Id::BackgroundNoiseOfImage:
        return "noise(image)";
    case Id::ImageWidth:
        return "w";
    case Id::ImageHeight:
        return "h";
    case Id::ImageChannelCount:
        return "n";
    case Id::ImageIsColor:
        return "iscolor";
    case Id::CieLightness:
        return "CIEL";
    case Id::CieGreenRed:
        return "CIEa";
    case Id::CieBlueYellow:
        return "CIEb";
    case Id::CieChroma:
        return "CIEc";
    case Id::CieHue:
        return "CIEh";
    case Id::CieX:
        return "CIEX";
    case Id::CieY:
        return "CIEY";
    case Id::CieZ:
        return "CIEZ";
    case Id::HslHue:
        return "HSLh";
    case Id::HslSaturation:
        return "HSLs";
    case Id::HslLightness:
        return "HSLl";
    case Id::HsvHue:
        return "HSVh";
    case Id::HsvSaturation:
        return "HSVs";
    case Id::HsvValue:
        return "HSVv";
    case Id::LumaY:
        return "YCbCrY";
    case Id::ChromaBlue:
        return "YCbCrCb";
    case Id::ChromaRed:
        return "YCbCrCr";
    case Id::CurrentChannel:
        return "c";
    case Id::CurrentColumn:
        return "x";
    case Id::CurrentRow:
        return "y";
    case Id::NormalizedColumn:
        return "X";
    case Id::NormalizedRow:
        return "Y";
    case Id::InRectangle:
        return "inrect";
    case Id::InEllipse:
        return "inellipse";
    case Id::SampleAt:
        return "pixel";
    case Id::Conditional:
        return "iif";
    case Id::UniformRandom:
        return "random";
    }
    return "unknown";
}

bool isPixelMathReservedName(std::string_view name) noexcept
{
    if (name == "$T" || name == "$A" || name == "$M")
    {
        return true;
    }
    return findPixelMathFunction(name) != nullptr || findPixelMathImageStatistic(name) != nullptr;
}

} // namespace an
