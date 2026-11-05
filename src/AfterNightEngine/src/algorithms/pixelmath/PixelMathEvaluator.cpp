// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the scalar meaning of a compiled expression, one pixel at a time.
//
// The published `evaluatePixelMathPixel` checks the invocation and then calls `runPixelMathProgram`,
// which the image walk calls directly after checking once; there is one interpreter, so the oracle
// and the walk cannot disagree. The interpreter is a stack machine over the program's instructions:
// a push reads a constant, a sample, a variable, a statistic, a coordinate, a random draw or a colour
// component; an operator or a call pops its operands and pushes one value; a store copies the top
// into a variable; and the two jumps are how the conditional evaluates only its chosen branch. Every
// value is a double, and a comparison or a logical operator answers one or zero.
//
// The compiler computed the stack depth exactly, so a push past it, a pop below zero or a final
// stack that does not hold exactly one value is `ErrorCode::Internal` - a program this build did not
// produce - rather than a pixel's answer. The random draw's index is the count of draws this pixel
// has already made, which is what keys two `random()` calls in one expression apart.

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathEvaluator.h>

#include "algorithms/pixelmath/PixelMathInterpreter.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::evaluatePixelMathPixel";
constexpr const char* kPlanOperation = "an::planPixelMathColors";

[[nodiscard]] Status evaluationFailure(ErrorCode code, const char* message)
{
    return Status::failure(
        code, StatusDetail{.message = message, .operation = kOperation, .domain = ErrorDomain::Algorithms});
}

/// Where one sample of one channel of one image sits, in bytes from the first, whatever arrangement
/// the image is stored in.
[[nodiscard]] std::uint64_t channelSampleOffset(const ImageLayout& layout, std::uint32_t channel, std::uint32_t x,
                                                std::uint32_t y) noexcept
{
    const std::uint32_t plane = channel / layout.samplesPerPixel();
    const std::uint32_t within = channel % layout.samplesPerPixel();
    return static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() +
           static_cast<std::uint64_t>(y) * layout.rowStrideBytes() +
           (static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(layout.samplesPerPixel()) +
            static_cast<std::uint64_t>(within)) *
               static_cast<std::uint64_t>(layout.elementSizeBytes());
}

/// One sample as its own code value, or zero for a coordinate or channel the image does not have.
[[nodiscard]] double readSample(const PixelMathImageView& image, std::uint32_t channel, std::uint32_t x,
                                std::uint32_t y) noexcept
{
    const ImageLayout& layout = *image.layout;
    if (x >= layout.extent().width() || y >= layout.extent().height() || channel >= layout.extent().channelCount())
    {
        return 0.0;
    }
    const std::uint8_t* const at =
        static_cast<const std::uint8_t*>(image.samples) + channelSampleOffset(layout, channel, x, y);
    switch (image.sampleType)
    {
    case SampleType::UInt8:
        return static_cast<double>(*reinterpret_cast<const std::uint8_t*>(at));
    case SampleType::Int8:
        return static_cast<double>(*reinterpret_cast<const std::int8_t*>(at));
    case SampleType::UInt16:
        return static_cast<double>(*reinterpret_cast<const std::uint16_t*>(at));
    case SampleType::Int16:
        return static_cast<double>(*reinterpret_cast<const std::int16_t*>(at));
    case SampleType::UInt32:
        return static_cast<double>(*reinterpret_cast<const std::uint32_t*>(at));
    case SampleType::Int32:
        return static_cast<double>(*reinterpret_cast<const std::int32_t*>(at));
    case SampleType::Float32:
        return static_cast<double>(*reinterpret_cast<const float*>(at));
    case SampleType::Float64:
        return *reinterpret_cast<const double*>(at);
    }
    return 0.0;
}

/// Which channel one sample read means at this site: the one it names, the only one a
/// single-channel image has, or otherwise the channel being written.
[[nodiscard]] std::uint32_t channelOf(const PixelMathSampleRead& read, const PixelMathImageView& image,
                                      std::uint32_t writtenChannel) noexcept
{
    if (read.selectsChannel)
    {
        return read.channel;
    }
    if (image.layout->extent().channelCount() == 1u)
    {
        return 0u;
    }
    return writtenChannel;
}

// The scalar functions, in the spellings the language uses.

/// The factorial of the argument truncated to an integer: not-a-number below zero, and infinity
/// above 170, where the answer no longer fits a double.
[[nodiscard]] double factorial(double value) noexcept
{
    if (!(value >= 0.0))
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (value > 170.0)
    {
        return std::numeric_limits<double>::infinity();
    }
    const std::uint64_t count = static_cast<std::uint64_t>(value);
    double result = 1.0;
    for (std::uint64_t index = 2u; index <= count; ++index)
    {
        result *= static_cast<double>(index);
    }
    return result;
}

/// How many ways `r` things can be chosen from `n`, both truncated to integers: not-a-number for a
/// negative argument or `r` above `n`, and infinity once a partial result overflows.
[[nodiscard]] double combinations(double n, double r) noexcept
{
    if (!(n >= 0.0) || !(r >= 0.0) || n < r)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const std::uint64_t whole = static_cast<std::uint64_t>(n);
    std::uint64_t taken = static_cast<std::uint64_t>(r);
    if (taken > whole / 2u)
    {
        taken = whole - taken;
    }
    // Multiplying and dividing in step makes the i-th partial the binomial coefficient of
    // `whole - taken + i` over `i`, an integer, so the result is exact while every partial product
    // stays below two to the fifty-third, where a double stops representing every integer.
    double result = 1.0;
    for (std::uint64_t index = 1u; index <= taken; ++index)
    {
        result = result * static_cast<double>(whole - taken + index) / static_cast<double>(index);
        if (!std::isfinite(result))
        {
            return std::numeric_limits<double>::infinity();
        }
    }
    return std::round(result);
}

/// How many ordered ways `r` things can be chosen from `n`: the combinations times the factorial
/// of `r`, with both functions' edge answers.
[[nodiscard]] double permutations(double n, double r) noexcept
{
    return combinations(n, r) * factorial(r);
}

/// The midtone transfer function `((m - 1) x) / ((2m - 1) x - m)`, which maps zero to zero, one to
/// one and `m` to one half. Neither argument is clamped.
[[nodiscard]] double midtoneTransfer(double midtone, double value) noexcept
{
    return ((midtone - 1.0) * value) / (((2.0 * midtone - 1.0) * value) - midtone);
}

/// The value held between two bounds.
[[nodiscard]] double clampValue(double value, double low, double high) noexcept
{
    // Written as a minimum then a maximum so that bounds given the wrong way round produce the lower
    // one rather than reaching the standard clamp's undefined behaviour.
    return (std::max)(low, (std::min)(value, high));
}

/// The median of the values, reordering them in place: the middle value of an odd count, the mean
/// of the two middle values of an even one, and not-a-number for none.
[[nodiscard]] double medianOf(std::span<double> values) noexcept
{
    const std::size_t count = values.size();
    if (count == 0u)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const std::size_t middle = count / 2u;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    const double upper = values[middle];
    if (count % 2u != 0u)
    {
        return upper;
    }
    // The lower of the two middle ranks is the largest of what the selection left below it, exactly.
    const double lower = *std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
    return (lower + upper) * 0.5;
}

/// The arithmetic mean, summed in argument order.
[[nodiscard]] double meanOf(std::span<const double> values) noexcept
{
    double sum = 0.0;
    for (const double value : values)
    {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}

/// The population variance, dividing the squared deviations from the mean by the count.
[[nodiscard]] double varianceOf(std::span<const double> values) noexcept
{
    const double mean = meanOf(values);
    double accumulated = 0.0;
    for (const double value : values)
    {
        const double deviation = value - mean;
        accumulated += deviation * deviation;
    }
    return accumulated / static_cast<double>(values.size());
}

/// Tukey's biweight midvariance of the values around their median, with tuning constant nine
/// times their median absolute deviation: zero when that deviation is zero, and not-a-number for
/// no values or a scratch shorter than them. The scratch is overwritten; the values are not.
[[nodiscard]] double biweightMidvarianceOf(std::span<double> values, std::span<double> scratch) noexcept
{
    const std::size_t count = values.size();
    if (count == 0u || scratch.size() < count)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::copy(values.begin(), values.end(), scratch.begin());
    const double center = medianOf(scratch.subspan(0u, count));
    for (std::size_t index = 0u; index < count; ++index)
    {
        scratch[index] = std::fabs(values[index] - center);
    }
    const double deviation = medianOf(scratch.subspan(0u, count));
    if (!(deviation > 0.0))
    {
        return 0.0;
    }
    const double factor = 1.0 / (9.0 * deviation);
    double numerator = 0.0;
    double denominator = 0.0;
    for (const double value : values)
    {
        const double centered = value - center;
        const double scaled = centered * factor;
        const double squared = std::fabs(scaled) < 1.0 ? scaled * scaled : 1.0;
        const double weight = 1.0 - squared;
        const double term = centered * weight * weight;
        numerator += term * term;
        denominator += weight * (1.0 - 5.0 * squared);
    }
    return static_cast<double>(count) * (numerator / (denominator * denominator));
}

/// One infix operator. The remainder takes the dividend's sign, as `std::fmod` does, and a
/// comparison or a logical operator answers one or zero, treating any non-zero operand as true.
///
/// A template per operator so that a loop applying one operator to a run compiles to that operator
/// alone; `binaryOperation` chooses among them at run time for a single sample.
template <PixelMathBinaryOperator Op> [[nodiscard]] double binaryOperator(double left, double right) noexcept
{
    if constexpr (Op == PixelMathBinaryOperator::Add)
    {
        return left + right;
    }
    else if constexpr (Op == PixelMathBinaryOperator::Subtract)
    {
        return left - right;
    }
    else if constexpr (Op == PixelMathBinaryOperator::AbsoluteDifference)
    {
        return std::fabs(left - right);
    }
    else if constexpr (Op == PixelMathBinaryOperator::Multiply)
    {
        return left * right;
    }
    else if constexpr (Op == PixelMathBinaryOperator::Divide)
    {
        return left / right;
    }
    else if constexpr (Op == PixelMathBinaryOperator::Modulus)
    {
        return std::fmod(left, right);
    }
    else if constexpr (Op == PixelMathBinaryOperator::Power)
    {
        return std::pow(left, right);
    }
    else if constexpr (Op == PixelMathBinaryOperator::Equal)
    {
        return left == right ? 1.0 : 0.0;
    }
    else if constexpr (Op == PixelMathBinaryOperator::NotEqual)
    {
        return left != right ? 1.0 : 0.0;
    }
    else if constexpr (Op == PixelMathBinaryOperator::Less)
    {
        return left < right ? 1.0 : 0.0;
    }
    else if constexpr (Op == PixelMathBinaryOperator::LessOrEqual)
    {
        return left <= right ? 1.0 : 0.0;
    }
    else if constexpr (Op == PixelMathBinaryOperator::Greater)
    {
        return left > right ? 1.0 : 0.0;
    }
    else if constexpr (Op == PixelMathBinaryOperator::GreaterOrEqual)
    {
        return left >= right ? 1.0 : 0.0;
    }
    else if constexpr (Op == PixelMathBinaryOperator::And)
    {
        return (left != 0.0 && right != 0.0) ? 1.0 : 0.0;
    }
    else
    {
        static_assert(Op == PixelMathBinaryOperator::Or);
        return (left != 0.0 || right != 0.0) ? 1.0 : 0.0;
    }
}

/// One infix operator chosen at run time.
[[nodiscard]] double binaryOperation(PixelMathBinaryOperator op, double left, double right) noexcept
{
    switch (op)
    {
    case PixelMathBinaryOperator::Add:
        return binaryOperator<PixelMathBinaryOperator::Add>(left, right);
    case PixelMathBinaryOperator::Subtract:
        return binaryOperator<PixelMathBinaryOperator::Subtract>(left, right);
    case PixelMathBinaryOperator::AbsoluteDifference:
        return binaryOperator<PixelMathBinaryOperator::AbsoluteDifference>(left, right);
    case PixelMathBinaryOperator::Multiply:
        return binaryOperator<PixelMathBinaryOperator::Multiply>(left, right);
    case PixelMathBinaryOperator::Divide:
        return binaryOperator<PixelMathBinaryOperator::Divide>(left, right);
    case PixelMathBinaryOperator::Modulus:
        return binaryOperator<PixelMathBinaryOperator::Modulus>(left, right);
    case PixelMathBinaryOperator::Power:
        return binaryOperator<PixelMathBinaryOperator::Power>(left, right);
    case PixelMathBinaryOperator::Equal:
        return binaryOperator<PixelMathBinaryOperator::Equal>(left, right);
    case PixelMathBinaryOperator::NotEqual:
        return binaryOperator<PixelMathBinaryOperator::NotEqual>(left, right);
    case PixelMathBinaryOperator::Less:
        return binaryOperator<PixelMathBinaryOperator::Less>(left, right);
    case PixelMathBinaryOperator::LessOrEqual:
        return binaryOperator<PixelMathBinaryOperator::LessOrEqual>(left, right);
    case PixelMathBinaryOperator::Greater:
        return binaryOperator<PixelMathBinaryOperator::Greater>(left, right);
    case PixelMathBinaryOperator::GreaterOrEqual:
        return binaryOperator<PixelMathBinaryOperator::GreaterOrEqual>(left, right);
    case PixelMathBinaryOperator::And:
        return binaryOperator<PixelMathBinaryOperator::And>(left, right);
    case PixelMathBinaryOperator::Or:
        return binaryOperator<PixelMathBinaryOperator::Or>(left, right);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

/// One prefix operator. The complement is one minus the value, and the logical not answers one
/// for zero and zero for anything else. A template per operator, as the infix ones are.
template <PixelMathUnaryOperator Op> [[nodiscard]] double unaryOperator(double value) noexcept
{
    if constexpr (Op == PixelMathUnaryOperator::Plus)
    {
        return value;
    }
    else if constexpr (Op == PixelMathUnaryOperator::Negate)
    {
        return -value;
    }
    else if constexpr (Op == PixelMathUnaryOperator::Complement)
    {
        return 1.0 - value;
    }
    else
    {
        static_assert(Op == PixelMathUnaryOperator::Not);
        return value == 0.0 ? 1.0 : 0.0;
    }
}

/// One prefix operator chosen at run time.
[[nodiscard]] double unaryOperation(PixelMathUnaryOperator op, double value) noexcept
{
    switch (op)
    {
    case PixelMathUnaryOperator::Plus:
        return unaryOperator<PixelMathUnaryOperator::Plus>(value);
    case PixelMathUnaryOperator::Negate:
        return unaryOperator<PixelMathUnaryOperator::Negate>(value);
    case PixelMathUnaryOperator::Complement:
        return unaryOperator<PixelMathUnaryOperator::Complement>(value);
    case PixelMathUnaryOperator::Not:
        return unaryOperator<PixelMathUnaryOperator::Not>(value);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

/// One call's value from its arguments, which it may reorder.
///
/// `room` is the evaluation stack above the arguments, which the two deviation estimators work in;
/// what it holds on entry is never read.
///
/// @return null with `value` set, or why the call cannot be evaluated, which is a program this build
///         did not produce.
[[nodiscard]] const char* evaluateCall(PixelMathFunctionId function, std::span<double> arguments,
                                       std::span<double> room, double& value) noexcept
{
    switch (function)
    {
    case PixelMathFunctionId::AbsoluteValue:
        value = std::fabs(arguments[0]);
        break;
    case PixelMathFunctionId::ArcCosine:
        value = std::acos(arguments[0]);
        break;
    case PixelMathFunctionId::ArcHyperbolicCosine:
        value = std::acosh(arguments[0]);
        break;
    case PixelMathFunctionId::ArcSine:
        value = std::asin(arguments[0]);
        break;
    case PixelMathFunctionId::ArcHyperbolicSine:
        value = std::asinh(arguments[0]);
        break;
    case PixelMathFunctionId::ArcTangent:
        value = std::atan(arguments[0]);
        break;
    case PixelMathFunctionId::ArcTangentOfRatio:
        value = std::atan2(arguments[0], arguments[1]);
        break;
    case PixelMathFunctionId::ArcHyperbolicTangent:
        value = std::atanh(arguments[0]);
        break;
    case PixelMathFunctionId::Ceiling:
        value = std::ceil(arguments[0]);
        break;
    case PixelMathFunctionId::Clamp:
        value = clampValue(arguments[0], arguments[1], arguments[2]);
        break;
    case PixelMathFunctionId::Cosine:
        value = std::cos(arguments[0]);
        break;
    case PixelMathFunctionId::HyperbolicCosine:
        value = std::cosh(arguments[0]);
        break;
    case PixelMathFunctionId::Exponential:
        value = std::exp(arguments[0]);
        break;
    case PixelMathFunctionId::Factorial:
        value = factorial(arguments[0]);
        break;
    case PixelMathFunctionId::Floor:
        value = std::floor(arguments[0]);
        break;
    case PixelMathFunctionId::NaturalLogarithm:
        value = std::log(arguments[0]);
        break;
    case PixelMathFunctionId::CommonLogarithm:
        value = std::log10(arguments[0]);
        break;
    case PixelMathFunctionId::BinaryLogarithm:
        value = std::log2(arguments[0]);
        break;
    case PixelMathFunctionId::MidtoneTransfer:
        value = midtoneTransfer(arguments[0], arguments[1]);
        break;
    case PixelMathFunctionId::Combinations:
        value = combinations(arguments[0], arguments[1]);
        break;
    case PixelMathFunctionId::Permutations:
        value = permutations(arguments[0], arguments[1]);
        break;
    case PixelMathFunctionId::Sign:
        value = arguments[0] > 0.0 ? 1.0 : (arguments[0] < 0.0 ? -1.0 : 0.0);
        break;
    case PixelMathFunctionId::SquareRoot:
        value = std::sqrt(arguments[0]);
        break;
    case PixelMathFunctionId::Sine:
        value = std::sin(arguments[0]);
        break;
    case PixelMathFunctionId::HyperbolicSine:
        value = std::sinh(arguments[0]);
        break;
    case PixelMathFunctionId::Tangent:
        value = std::tan(arguments[0]);
        break;
    case PixelMathFunctionId::HyperbolicTangent:
        value = std::tanh(arguments[0]);
        break;
    case PixelMathFunctionId::Truncate:
        value = std::trunc(arguments[0]);
        break;
    case PixelMathFunctionId::MaximumOfArguments:
        value = *std::max_element(arguments.begin(), arguments.end());
        break;
    case PixelMathFunctionId::MinimumOfArguments:
        value = *std::min_element(arguments.begin(), arguments.end());
        break;
    case PixelMathFunctionId::MeanOfArguments:
        value = meanOf(arguments);
        break;
    case PixelMathFunctionId::SumOfArguments: {
        double accumulated = 0.0;
        for (const double argument : arguments)
        {
            accumulated += argument;
        }
        value = accumulated;
        break;
    }
    case PixelMathFunctionId::VarianceOfArguments:
        value = varianceOf(arguments);
        break;
    case PixelMathFunctionId::StandardDeviationOfArguments:
        value = std::sqrt(varianceOf(arguments));
        break;
    case PixelMathFunctionId::MeanOfSquaresOfArguments: {
        double accumulated = 0.0;
        for (const double argument : arguments)
        {
            accumulated += argument * argument;
        }
        value = accumulated / static_cast<double>(arguments.size());
        break;
    }
    case PixelMathFunctionId::SumOfSquaresOfArguments: {
        double accumulated = 0.0;
        for (const double argument : arguments)
        {
            accumulated += argument * argument;
        }
        value = accumulated;
        break;
    }
    case PixelMathFunctionId::MedianOfArguments:
        value = medianOf(arguments);
        break;
    case PixelMathFunctionId::MedianAbsoluteDeviationOfArguments: {
        if (room.size() < arguments.size())
        {
            return "a deviation estimator found no room above the arguments it was given";
        }
        const std::span<double> spare = room.subspan(0u, arguments.size());
        std::copy(arguments.begin(), arguments.end(), spare.begin());
        const double center = medianOf(spare);
        for (std::size_t index = 0u; index < arguments.size(); ++index)
        {
            spare[index] = std::fabs(arguments[index] - center);
        }
        value = medianOf(spare);
        break;
    }
    case PixelMathFunctionId::BiweightMidvarianceOfArguments: {
        if (room.size() < arguments.size())
        {
            return "a deviation estimator found no room above the arguments it was given";
        }
        value = biweightMidvarianceOf(arguments, room.subspan(0u, arguments.size()));
        break;
    }
    case PixelMathFunctionId::InRectangle: {
        // The stated corner and size are truncated toward zero and the rectangle holds both of its
        // edges, so `inrect(0, 0, 1, 0)` holds two pixels of the first row.
        // The column and the row being written follow the four stated numbers.
        if (arguments.size() != 6u)
        {
            return "a region test was not given the pixel it tests";
        }
        if (!std::isfinite(arguments[0]) || !std::isfinite(arguments[1]) || !std::isfinite(arguments[2]) ||
            !std::isfinite(arguments[3]))
        {
            value = 0.0;
            break;
        }
        const double left = std::trunc(arguments[0]);
        const double top = std::trunc(arguments[1]);
        const double column = arguments[4];
        const double row = arguments[5];
        value = column >= left && column <= left + std::trunc(arguments[2]) && row >= top &&
                        row <= top + std::trunc(arguments[3])
                    ? 1.0
                    : 0.0;
        break;
    }
    case PixelMathFunctionId::InEllipse: {
        // One where `((x - cx) / rx)^2 + ((y - cy) / ry)^2` is at most one, at the pixel being written.
        if (arguments.size() != 6u)
        {
            return "a region test was not given the pixel it tests";
        }
        const double across = (arguments[4] - arguments[0]) / arguments[2];
        const double down = (arguments[5] - arguments[1]) / arguments[3];
        value = across * across + down * down <= 1.0 ? 1.0 : 0.0;
        break;
    }
    default:
        return "a call this build cannot evaluate";
    }
    return nullptr;
}

/// What the sampling call reads: the stated sample of `image`, or zero when the stated column or row
/// is not finite or is negative.
///
/// @param image the image the call names.
/// @param arguments the column, the row and, when `count` is three, the channel.
/// @param count how many arguments the call was given.
/// @param writtenChannel the channel being written, which a call that states none reads.
[[nodiscard]] double sampleStated(const PixelMathImageView& image, const double* arguments, std::uint32_t count,
                                  std::uint32_t writtenChannel) noexcept
{
    const double column = arguments[0];
    const double row = arguments[1];
    double value = 0.0;
    if (std::isfinite(column) && std::isfinite(row) && column >= 0.0 && row >= 0.0)
    {
        std::uint32_t channel = writtenChannel;
        if (count >= 3u)
        {
            const double stated = arguments[2];
            channel = std::isfinite(stated) && stated >= 0.0 ? static_cast<std::uint32_t>(stated)
                                                             : image.layout->extent().channelCount();
        }
        else if (image.layout->extent().channelCount() == 1u)
        {
            channel = 0u;
        }
        value = readSample(image, channel, static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(row));
    }
    return value;
}

/// One colour component of one pixel of a bound image.
///
/// @return success with `value` set, or the colour transform's failure for a pixel it cannot convert.
[[nodiscard]] Status colorComponentAt(const PlannedColorRead& read, std::span<const PixelMathImageView> images,
                                      std::uint32_t x, std::uint32_t y, double& value)
{
    const PixelMathImageView& image = images[read.image];
    const std::array<double, 3u> source{readSample(image, 0u, x, y), readSample(image, 1u, x, y),
                                        readSample(image, 2u, x, y)};
    std::array<double, 4u> converted{};
    const Status done = convertColorPixel(read.plan, std::span<const double>(source),
                                          std::span<double>(converted.data(), read.plan.destinationChannelCount()));
    if (done.failed())
    {
        return done;
    }
    value = converted[read.channel];
    return Status::success();
}

/// Samples of one representation along a row, `stride` elements apart, as doubles.
template <typename SampleT>
void readSampleRow(const std::uint8_t* first, std::uint32_t stride, std::uint32_t count, double* out) noexcept
{
    const SampleT* const samples = reinterpret_cast<const SampleT*>(first);
    if (stride == 1u)
    {
        for (std::uint32_t lane = 0u; lane < count; ++lane)
        {
            out[lane] = static_cast<double>(samples[lane]);
        }
        return;
    }
    for (std::uint32_t lane = 0u; lane < count; ++lane)
    {
        out[lane] = static_cast<double>(samples[static_cast<std::size_t>(lane) * stride]);
    }
}

/// The samples of one channel of one row from column `x` on, each what `readSample` reads there:
/// zero past the last column, and zero throughout for a row or channel the image does not have.
void readSampleRun(const PixelMathImageView& image, std::uint32_t channel, std::uint32_t x, std::uint32_t y,
                   std::uint32_t count, double* out) noexcept
{
    const ImageLayout& layout = *image.layout;
    const ImageExtent extent = layout.extent();
    std::uint32_t inside = 0u;
    if (isSampleType(image.sampleType) && y < extent.height() && channel < extent.channelCount() && x < extent.width())
    {
        inside = (std::min)(count, extent.width() - x);
        const std::uint8_t* const first =
            static_cast<const std::uint8_t*>(image.samples) + channelSampleOffset(layout, channel, x, y);
        const std::uint32_t stride = layout.samplesPerPixel();
        switch (image.sampleType)
        {
        case SampleType::UInt8:
            readSampleRow<std::uint8_t>(first, stride, inside, out);
            break;
        case SampleType::Int8:
            readSampleRow<std::int8_t>(first, stride, inside, out);
            break;
        case SampleType::UInt16:
            readSampleRow<std::uint16_t>(first, stride, inside, out);
            break;
        case SampleType::Int16:
            readSampleRow<std::int16_t>(first, stride, inside, out);
            break;
        case SampleType::UInt32:
            readSampleRow<std::uint32_t>(first, stride, inside, out);
            break;
        case SampleType::Int32:
            readSampleRow<std::int32_t>(first, stride, inside, out);
            break;
        case SampleType::Float32:
            readSampleRow<float>(first, stride, inside, out);
            break;
        case SampleType::Float64:
            readSampleRow<double>(first, stride, inside, out);
            break;
        }
    }
    for (std::uint32_t lane = inside; lane < count; ++lane)
    {
        out[lane] = 0.0;
    }
}

/// One prefix operator applied to every value of a run.
template <PixelMathUnaryOperator Op> void applyUnaryToRun(double* values, std::uint32_t count) noexcept
{
    for (std::uint32_t lane = 0u; lane < count; ++lane)
    {
        values[lane] = unaryOperator<Op>(values[lane]);
    }
}

void applyUnaryToRun(PixelMathUnaryOperator op, double* values, std::uint32_t count) noexcept
{
    switch (op)
    {
    case PixelMathUnaryOperator::Plus:
        return;
    case PixelMathUnaryOperator::Negate:
        applyUnaryToRun<PixelMathUnaryOperator::Negate>(values, count);
        return;
    case PixelMathUnaryOperator::Complement:
        applyUnaryToRun<PixelMathUnaryOperator::Complement>(values, count);
        return;
    case PixelMathUnaryOperator::Not:
        applyUnaryToRun<PixelMathUnaryOperator::Not>(values, count);
        return;
    }
    for (std::uint32_t lane = 0u; lane < count; ++lane)
    {
        values[lane] = unaryOperation(op, values[lane]);
    }
}

/// One infix operator applied lane by lane, the result replacing the left operand. An operator that
/// is one arithmetic instruction or a comparison may be vectorized: each lane still computes the same
/// correctly rounded operation, and nothing is contracted.
template <PixelMathBinaryOperator Op>
void applyBinaryToRun(double* left, const double* right, std::uint32_t count) noexcept
{
    for (std::uint32_t lane = 0u; lane < count; ++lane)
    {
        left[lane] = binaryOperator<Op>(left[lane], right[lane]);
    }
}

/// An operator that calls the mathematical library, a lane at a time and never vectorized: a vector
/// library's power or remainder need not return the scalar function's bits.
template <PixelMathBinaryOperator Op>
void applyLibraryBinaryToRun(double* left, const double* right, std::uint32_t count) noexcept
{
#if defined(__clang__)
#pragma clang loop vectorize(disable)
#elif defined(_MSC_VER)
#pragma loop(no_vector)
#endif
    for (std::uint32_t lane = 0u; lane < count; ++lane)
    {
        left[lane] = binaryOperator<Op>(left[lane], right[lane]);
    }
}

void applyBinaryToRun(PixelMathBinaryOperator op, double* left, const double* right, std::uint32_t count) noexcept
{
    switch (op)
    {
    case PixelMathBinaryOperator::Add:
        applyBinaryToRun<PixelMathBinaryOperator::Add>(left, right, count);
        return;
    case PixelMathBinaryOperator::Subtract:
        applyBinaryToRun<PixelMathBinaryOperator::Subtract>(left, right, count);
        return;
    case PixelMathBinaryOperator::AbsoluteDifference:
        applyBinaryToRun<PixelMathBinaryOperator::AbsoluteDifference>(left, right, count);
        return;
    case PixelMathBinaryOperator::Multiply:
        applyBinaryToRun<PixelMathBinaryOperator::Multiply>(left, right, count);
        return;
    case PixelMathBinaryOperator::Divide:
        applyBinaryToRun<PixelMathBinaryOperator::Divide>(left, right, count);
        return;
    case PixelMathBinaryOperator::Modulus:
        applyLibraryBinaryToRun<PixelMathBinaryOperator::Modulus>(left, right, count);
        return;
    case PixelMathBinaryOperator::Power:
        applyLibraryBinaryToRun<PixelMathBinaryOperator::Power>(left, right, count);
        return;
    case PixelMathBinaryOperator::Equal:
        applyBinaryToRun<PixelMathBinaryOperator::Equal>(left, right, count);
        return;
    case PixelMathBinaryOperator::NotEqual:
        applyBinaryToRun<PixelMathBinaryOperator::NotEqual>(left, right, count);
        return;
    case PixelMathBinaryOperator::Less:
        applyBinaryToRun<PixelMathBinaryOperator::Less>(left, right, count);
        return;
    case PixelMathBinaryOperator::LessOrEqual:
        applyBinaryToRun<PixelMathBinaryOperator::LessOrEqual>(left, right, count);
        return;
    case PixelMathBinaryOperator::Greater:
        applyBinaryToRun<PixelMathBinaryOperator::Greater>(left, right, count);
        return;
    case PixelMathBinaryOperator::GreaterOrEqual:
        applyBinaryToRun<PixelMathBinaryOperator::GreaterOrEqual>(left, right, count);
        return;
    case PixelMathBinaryOperator::And:
        applyBinaryToRun<PixelMathBinaryOperator::And>(left, right, count);
        return;
    case PixelMathBinaryOperator::Or:
        applyBinaryToRun<PixelMathBinaryOperator::Or>(left, right, count);
        return;
    }
    for (std::uint32_t lane = 0u; lane < count; ++lane)
    {
        left[lane] = binaryOperation(op, left[lane], right[lane]);
    }
}

/// The encoding a bound image's samples are treated as, derived from what it says they mean.
[[nodiscard]] ColorEncoding sourceEncodingOf(NumericDomain domain) noexcept
{
    ColorEncoding encoding;
    encoding.schema = ChannelSchema::Rgb;
    encoding.primaries = RgbPrimaries::Srgb;
    encoding.transfer =
        domain == NumericDomain::EncodedNonlinear ? ColorTransferFunction::Srgb : ColorTransferFunction::Linear;
    encoding.whitePoint = ChromaticWhitePoint::D65;
    return encoding;
}

/// The encoding one colour component is measured in, and which of its channels it is.
struct ComponentTarget
{
    /// The encoding the pixel is converted into.
    ColorEncoding encoding{};

    /// Which channel of the converted pixel the component is.
    std::uint32_t channel = 0u;
};

/// Where one colour-component function reads, under the language's packings: CIE Lab and LCh as
/// `PerceptualEncoding::UnitPacked` packs them, hue as a fraction of a turn, and full-range Rec. 601
/// YCbCr.
///
/// @param component the function.
/// @return the target, or nothing for a function that is not a colour component.
[[nodiscard]] std::optional<ComponentTarget> componentTargetOf(PixelMathFunctionId component) noexcept
{
    ColorEncoding encoding;
    encoding.whitePoint = ChromaticWhitePoint::D65;
    switch (component)
    {
    case PixelMathFunctionId::CieLightness:
    case PixelMathFunctionId::CieGreenRed:
    case PixelMathFunctionId::CieBlueYellow:
        encoding.schema = ChannelSchema::Lab;
        encoding.perceptual = PerceptualEncoding::UnitPacked;
        return ComponentTarget{encoding, component == PixelMathFunctionId::CieLightness  ? 0u
                                         : component == PixelMathFunctionId::CieGreenRed ? 1u
                                                                                         : 2u};
    case PixelMathFunctionId::CieChroma:
    case PixelMathFunctionId::CieHue:
        encoding.schema = ChannelSchema::Lch;
        encoding.perceptual = PerceptualEncoding::UnitPacked;
        return ComponentTarget{encoding, component == PixelMathFunctionId::CieChroma ? 1u : 2u};
    case PixelMathFunctionId::CieX:
    case PixelMathFunctionId::CieY:
    case PixelMathFunctionId::CieZ:
        encoding.schema = ChannelSchema::Xyz;
        return ComponentTarget{encoding, component == PixelMathFunctionId::CieX   ? 0u
                                         : component == PixelMathFunctionId::CieY ? 1u
                                                                                  : 2u};
    case PixelMathFunctionId::HslHue:
    case PixelMathFunctionId::HslSaturation:
    case PixelMathFunctionId::HslLightness:
        encoding.schema = ChannelSchema::Hsl;
        encoding.primaries = RgbPrimaries::Srgb;
        encoding.transfer = ColorTransferFunction::Linear;
        encoding.hue = HueConvention::TurnFraction;
        return ComponentTarget{encoding, component == PixelMathFunctionId::HslHue          ? 0u
                                         : component == PixelMathFunctionId::HslSaturation ? 1u
                                                                                           : 2u};
    case PixelMathFunctionId::HsvHue:
    case PixelMathFunctionId::HsvSaturation:
    case PixelMathFunctionId::HsvValue:
        encoding.schema = ChannelSchema::Hsv;
        encoding.primaries = RgbPrimaries::Srgb;
        encoding.transfer = ColorTransferFunction::Linear;
        encoding.hue = HueConvention::TurnFraction;
        return ComponentTarget{encoding, component == PixelMathFunctionId::HsvHue          ? 0u
                                         : component == PixelMathFunctionId::HsvSaturation ? 1u
                                                                                           : 2u};
    case PixelMathFunctionId::LumaY:
    case PixelMathFunctionId::ChromaBlue:
    case PixelMathFunctionId::ChromaRed:
        encoding.schema = ChannelSchema::YCbCr;
        encoding.primaries = RgbPrimaries::Srgb;
        encoding.transfer = ColorTransferFunction::Linear;
        encoding.yCbCr = YCbCrVariant::Rec601FullRange;
        return ComponentTarget{encoding, component == PixelMathFunctionId::LumaY        ? 0u
                                         : component == PixelMathFunctionId::ChromaBlue ? 1u
                                                                                        : 2u};
    default:
        break;
    }
    return std::nullopt;
}

} // namespace

Result<PixelMathColorPlans> PixelMathColorPlanner::plan(const PixelMathProgram& program)
{
    PixelMathColorPlans handle;
    if (program.colorReads().empty())
    {
        return handle;
    }
    auto planned = std::make_shared<PlannedColorReads>();
    planned->reserve(program.colorReads().size());
    for (const PixelMathColorRead& read : program.colorReads())
    {
        const std::optional<ComponentTarget> target = componentTargetOf(read.component);
        if (!target.has_value() || read.image >= program.images().size())
        {
            return Status::failure(ErrorCode::Internal,
                                   StatusDetail{.message = "a colour component this build cannot plan",
                                                .operation = kPlanOperation,
                                                .domain = ErrorDomain::Algorithms});
        }
        const ColorEncoding source = sourceEncodingOf(program.images()[read.image].numericDomain);
        Result<ColorConversionPlan> conversion = planColorConversion(source, target->encoding);
        if (!conversion.hasValue())
        {
            return conversion.status();
        }
        planned->push_back(PlannedColorRead{std::move(*conversion), read.image, target->channel});
    }
    handle.m_plans = std::static_pointer_cast<const void>(std::const_pointer_cast<const PlannedColorReads>(planned));
    return handle;
}

const PlannedColorReads* PixelMathColorPlanner::read(const PixelMathColorPlans& handle) noexcept
{
    return static_cast<const PlannedColorReads*>(handle.m_plans.get());
}

std::size_t PixelMathColorPlans::count() const noexcept
{
    const PlannedColorReads* const planned = PixelMathColorPlanner::read(*this);
    return planned == nullptr ? 0u : planned->size();
}

Result<PixelMathColorPlans> planPixelMathColors(const PixelMathProgram& program)
{
    return PixelMathColorPlanner::plan(program);
}

Result<double> evaluatePixelMathPixel(const PixelMathProgram& program, std::span<const PixelMathImageView> images,
                                      std::span<const double> statistics, const PixelMathColorPlans& colorPlans,
                                      const PixelMathEvaluationSite& site, const PixelMathEvaluationScratch& scratch)
{
    const std::span<const PixelMathInstruction> instructions = program.instructions();
    if (instructions.empty())
    {
        return evaluationFailure(ErrorCode::InvalidArgument, "an empty expression program was evaluated");
    }
    if (images.size() != program.images().size())
    {
        return evaluationFailure(ErrorCode::InvalidArgument,
                                 "the number of bound images does not match the expression's image count");
    }
    if (statistics.size() != program.statistics().size())
    {
        return evaluationFailure(ErrorCode::InvalidArgument,
                                 "the number of supplied statistics does not match what the expression needs");
    }
    if (scratch.variables.size() < program.variableInitialValues().size() ||
        scratch.stack.size() < program.stackDepth())
    {
        return evaluationFailure(ErrorCode::InvalidArgument,
                                 "the evaluation scratch is smaller than the expression needs");
    }
    for (const PixelMathImageView& image : images)
    {
        if (image.samples == nullptr || image.layout == nullptr)
        {
            return evaluationFailure(ErrorCode::InvalidArgument, "a bound image has a null samples or layout pointer");
        }
    }
    const PlannedColorReads* const planned = PixelMathColorPlanner::read(colorPlans);
    if (program.colorReads().size() != (planned == nullptr ? 0u : planned->size()))
    {
        return evaluationFailure(ErrorCode::InvalidArgument,
                                 "the colour plans supplied do not match what the expression reads");
    }

    return runPixelMathProgram(program, images, statistics, planned, site, scratch);
}

Result<double> runPixelMathProgram(const PixelMathProgram& program, std::span<const PixelMathImageView> images,
                                   std::span<const double> statistics, const PlannedColorReads* planned,
                                   const PixelMathEvaluationSite& site, const PixelMathEvaluationScratch& scratch)
{
    const std::span<const PixelMathInstruction> instructions = program.instructions();
    const std::span<const double> initialValues = program.variableInitialValues();
    for (std::size_t index = 0u; index < initialValues.size(); ++index)
    {
        scratch.variables[index] = initialValues[index];
    }

    double* const stack = scratch.stack.data();
    const std::size_t capacity = scratch.stack.size();
    std::size_t top = 0u;
    std::uint64_t draws = 0u;
    const std::span<const PixelMathSampleRead> reads = program.sampleReads();
    const ImageExtent targetExtent = *program.targetExtent();

    std::size_t pc = 0u;
    while (pc < instructions.size())
    {
        const PixelMathInstruction& instruction = instructions[pc];
        switch (instruction.opcode)
        {
        case PixelMathOpcode::PushConstant:
        case PixelMathOpcode::PushSample:
        case PixelMathOpcode::PushVariable:
        case PixelMathOpcode::PushStatistic:
        case PixelMathOpcode::PushColumn:
        case PixelMathOpcode::PushRow:
        case PixelMathOpcode::PushNormalizedColumn:
        case PixelMathOpcode::PushNormalizedRow:
        case PixelMathOpcode::PushChannel:
        case PixelMathOpcode::PushRandom:
        case PixelMathOpcode::PushColorComponent:
            if (top >= capacity)
            {
                return evaluationFailure(ErrorCode::Internal, "the expression's evaluation stack overflowed");
            }
            break;
        default:
            break;
        }

        switch (instruction.opcode)
        {
        case PixelMathOpcode::PushConstant:
            stack[top++] = program.constants()[instruction.operand];
            break;
        case PixelMathOpcode::PushSample: {
            const PixelMathSampleRead& read = reads[instruction.operand];
            const PixelMathImageView& image = images[read.image];
            stack[top++] = readSample(image, channelOf(read, image, site.channel), site.x, site.y);
            break;
        }
        case PixelMathOpcode::PushVariable:
            stack[top++] = scratch.variables[instruction.operand];
            break;
        case PixelMathOpcode::PushStatistic:
            stack[top++] = statistics[instruction.operand];
            break;
        case PixelMathOpcode::PushColumn:
            stack[top++] = static_cast<double>(site.x);
            break;
        case PixelMathOpcode::PushRow:
            stack[top++] = static_cast<double>(site.y);
            break;
        case PixelMathOpcode::PushNormalizedColumn:
            stack[top++] = targetExtent.width() <= 1u
                               ? 0.0
                               : static_cast<double>(site.x) / static_cast<double>(targetExtent.width() - 1u);
            break;
        case PixelMathOpcode::PushNormalizedRow:
            stack[top++] = targetExtent.height() <= 1u
                               ? 0.0
                               : static_cast<double>(site.y) / static_cast<double>(targetExtent.height() - 1u);
            break;
        case PixelMathOpcode::PushChannel:
            stack[top++] = static_cast<double>(site.channel);
            break;
        case PixelMathOpcode::PushRandom:
            stack[top++] = pixelMathRandomDraw(site.randomSeed, site.x, site.y, site.channel, draws);
            ++draws;
            break;
        case PixelMathOpcode::PushColorComponent: {
            double value = 0.0;
            const Status done = colorComponentAt((*planned)[instruction.operand], images, site.x, site.y, value);
            if (done.failed())
            {
                return done;
            }
            stack[top++] = value;
            break;
        }
        case PixelMathOpcode::SampleImage: {
            if (top < instruction.count)
            {
                return evaluationFailure(ErrorCode::Internal, "a sampling call found too few arguments on the stack");
            }
            const double value = sampleStated(images[instruction.operand], stack + (top - instruction.count),
                                              instruction.count, site.channel);
            top -= instruction.count;
            stack[top++] = value;
            break;
        }
        case PixelMathOpcode::ApplyUnary:
            if (top == 0u)
            {
                return evaluationFailure(ErrorCode::Internal, "a prefix operator found an empty stack");
            }
            stack[top - 1u] = unaryOperation(instruction.unary, stack[top - 1u]);
            break;
        case PixelMathOpcode::ApplyBinary:
            if (top < 2u)
            {
                return evaluationFailure(ErrorCode::Internal, "an infix operator found too few values on the stack");
            }
            stack[top - 2u] = binaryOperation(instruction.binary, stack[top - 2u], stack[top - 1u]);
            --top;
            break;
        case PixelMathOpcode::CallFunction: {
            if (top < instruction.count)
            {
                return evaluationFailure(ErrorCode::Internal, "a call found too few arguments on the stack");
            }
            const std::size_t first = top - instruction.count;
            double value = 0.0;
            const char* const refused =
                evaluateCall(instruction.function, std::span<double>(stack + first, instruction.count),
                             std::span<double>(stack + top, capacity - top), value);
            if (refused != nullptr)
            {
                return evaluationFailure(ErrorCode::Internal, refused);
            }
            top = first;
            stack[top++] = value;
            break;
        }
        case PixelMathOpcode::StoreVariable:
            if (top == 0u)
            {
                return evaluationFailure(ErrorCode::Internal, "an assignment found an empty stack");
            }
            scratch.variables[instruction.operand] = stack[top - 1u];
            break;
        case PixelMathOpcode::Discard:
            if (top == 0u)
            {
                return evaluationFailure(ErrorCode::Internal, "a statement separator found an empty stack");
            }
            --top;
            break;
        case PixelMathOpcode::JumpIfZero:
            if (top == 0u)
            {
                return evaluationFailure(ErrorCode::Internal, "a branch found an empty stack");
            }
            --top;
            if (stack[top] == 0.0)
            {
                pc = instruction.operand;
                continue;
            }
            break;
        case PixelMathOpcode::Jump:
            pc = instruction.operand;
            continue;
        }
        ++pc;
    }

    if (top != 1u)
    {
        return evaluationFailure(ErrorCode::Internal, "the expression did not leave exactly one value on its stack");
    }
    return stack[0];
}

bool pixelMathProgramIsStraightLine(const PixelMathProgram& program) noexcept
{
    return std::none_of(
        program.instructions().begin(), program.instructions().end(), [](const PixelMathInstruction& instruction) {
            return instruction.opcode == PixelMathOpcode::JumpIfZero || instruction.opcode == PixelMathOpcode::Jump;
        });
}

Status runPixelMathProgramOverRun(const PixelMathProgram& program, std::span<const PixelMathImageView> images,
                                  std::span<const double> statistics, const PlannedColorReads* planned,
                                  const PixelMathEvaluationRun& run, const PixelMathRunScratch& scratch,
                                  std::span<double> values)
{
    const std::span<const PixelMathInstruction> instructions = program.instructions();
    const std::span<const double> initialValues = program.variableInitialValues();
    const std::uint32_t lanes = scratch.lanes;
    const std::uint32_t count = run.count;
    for (std::size_t index = 0u; index < initialValues.size(); ++index)
    {
        std::fill_n(scratch.variables.data() + index * lanes, count, initialValues[index]);
    }

    // Slot `s` of the stack holds lane `i` at `stack[s * lanes + i]`, so that one operator reads and
    // writes whole slots. The capacity is the single-sample stack's, which is what the overflow and
    // the deviation estimators' room are judged against.
    double* const stack = scratch.stack.data();
    const std::size_t capacity = scratch.stack.size() / lanes;
    std::size_t top = 0u;
    std::uint64_t draws = 0u;
    const std::span<const PixelMathSampleRead> reads = program.sampleReads();
    const ImageExtent targetExtent = *program.targetExtent();

    for (const PixelMathInstruction& instruction : instructions)
    {
        switch (instruction.opcode)
        {
        case PixelMathOpcode::PushConstant:
        case PixelMathOpcode::PushSample:
        case PixelMathOpcode::PushVariable:
        case PixelMathOpcode::PushStatistic:
        case PixelMathOpcode::PushColumn:
        case PixelMathOpcode::PushRow:
        case PixelMathOpcode::PushNormalizedColumn:
        case PixelMathOpcode::PushNormalizedRow:
        case PixelMathOpcode::PushChannel:
        case PixelMathOpcode::PushRandom:
        case PixelMathOpcode::PushColorComponent:
            if (top >= capacity)
            {
                return evaluationFailure(ErrorCode::Internal, "the expression's evaluation stack overflowed");
            }
            break;
        default:
            break;
        }

        double* const pushed = stack + top * lanes;
        switch (instruction.opcode)
        {
        case PixelMathOpcode::PushConstant:
            std::fill_n(pushed, count, program.constants()[instruction.operand]);
            ++top;
            break;
        case PixelMathOpcode::PushSample: {
            const PixelMathSampleRead& read = reads[instruction.operand];
            const PixelMathImageView& image = images[read.image];
            readSampleRun(image, channelOf(read, image, run.channel), run.x, run.y, count, pushed);
            ++top;
            break;
        }
        case PixelMathOpcode::PushVariable:
            std::copy_n(scratch.variables.data() + static_cast<std::size_t>(instruction.operand) * lanes, count,
                        pushed);
            ++top;
            break;
        case PixelMathOpcode::PushStatistic:
            std::fill_n(pushed, count, statistics[instruction.operand]);
            ++top;
            break;
        case PixelMathOpcode::PushColumn:
            for (std::uint32_t lane = 0u; lane < count; ++lane)
            {
                pushed[lane] = static_cast<double>(run.x + lane);
            }
            ++top;
            break;
        case PixelMathOpcode::PushRow:
            std::fill_n(pushed, count, static_cast<double>(run.y));
            ++top;
            break;
        case PixelMathOpcode::PushNormalizedColumn:
            if (targetExtent.width() <= 1u)
            {
                std::fill_n(pushed, count, 0.0);
            }
            else
            {
                const double last = static_cast<double>(targetExtent.width() - 1u);
                for (std::uint32_t lane = 0u; lane < count; ++lane)
                {
                    pushed[lane] = static_cast<double>(run.x + lane) / last;
                }
            }
            ++top;
            break;
        case PixelMathOpcode::PushNormalizedRow:
            std::fill_n(pushed, count,
                        targetExtent.height() <= 1u
                            ? 0.0
                            : static_cast<double>(run.y) / static_cast<double>(targetExtent.height() - 1u));
            ++top;
            break;
        case PixelMathOpcode::PushChannel:
            std::fill_n(pushed, count, static_cast<double>(run.channel));
            ++top;
            break;
        case PixelMathOpcode::PushRandom:
            for (std::uint32_t lane = 0u; lane < count; ++lane)
            {
                pushed[lane] = pixelMathRandomDraw(run.randomSeed, run.x + lane, run.y, run.channel, draws);
            }
            ++draws;
            ++top;
            break;
        case PixelMathOpcode::PushColorComponent: {
            const PlannedColorRead& read = (*planned)[instruction.operand];
            for (std::uint32_t lane = 0u; lane < count; ++lane)
            {
                const Status done = colorComponentAt(read, images, run.x + lane, run.y, pushed[lane]);
                if (done.failed())
                {
                    return done;
                }
            }
            ++top;
            break;
        }
        case PixelMathOpcode::SampleImage: {
            if (top < instruction.count)
            {
                return evaluationFailure(ErrorCode::Internal, "a sampling call found too few arguments on the stack");
            }
            const std::size_t first = top - instruction.count;
            const PixelMathImageView& image = images[instruction.operand];
            double* const result = stack + first * lanes;
            for (std::uint32_t lane = 0u; lane < count; ++lane)
            {
                std::array<double, 3u> arguments{};
                for (std::uint32_t argument = 0u; argument < instruction.count && argument < arguments.size();
                     ++argument)
                {
                    arguments[argument] = stack[(first + argument) * lanes + lane];
                }
                result[lane] = sampleStated(image, arguments.data(), instruction.count, run.channel);
            }
            top = first + 1u;
            break;
        }
        case PixelMathOpcode::ApplyUnary:
            if (top == 0u)
            {
                return evaluationFailure(ErrorCode::Internal, "a prefix operator found an empty stack");
            }
            applyUnaryToRun(instruction.unary, stack + (top - 1u) * lanes, count);
            break;
        case PixelMathOpcode::ApplyBinary:
            if (top < 2u)
            {
                return evaluationFailure(ErrorCode::Internal, "an infix operator found too few values on the stack");
            }
            applyBinaryToRun(instruction.binary, stack + (top - 2u) * lanes, stack + (top - 1u) * lanes, count);
            --top;
            break;
        case PixelMathOpcode::CallFunction: {
            if (top < instruction.count)
            {
                return evaluationFailure(ErrorCode::Internal, "a call found too few arguments on the stack");
            }
            // Each sample's arguments are gathered out of their slots into one contiguous list with the
            // single-sample stack's room above it, so the function sees what it would see there.
            const std::size_t first = top - instruction.count;
            const std::span<double> arguments = scratch.arguments.subspan(0u, instruction.count);
            const std::span<double> room = scratch.arguments.subspan(instruction.count, capacity - top);
            double* const result = stack + first * lanes;
            for (std::uint32_t lane = 0u; lane < count; ++lane)
            {
                for (std::uint32_t argument = 0u; argument < instruction.count; ++argument)
                {
                    arguments[argument] = stack[(first + argument) * lanes + lane];
                }
                double value = 0.0;
                const char* const refused = evaluateCall(instruction.function, arguments, room, value);
                if (refused != nullptr)
                {
                    return evaluationFailure(ErrorCode::Internal, refused);
                }
                result[lane] = value;
            }
            top = first + 1u;
            break;
        }
        case PixelMathOpcode::StoreVariable:
            if (top == 0u)
            {
                return evaluationFailure(ErrorCode::Internal, "an assignment found an empty stack");
            }
            std::copy_n(stack + (top - 1u) * lanes, count,
                        scratch.variables.data() + static_cast<std::size_t>(instruction.operand) * lanes);
            break;
        case PixelMathOpcode::Discard:
            if (top == 0u)
            {
                return evaluationFailure(ErrorCode::Internal, "a statement separator found an empty stack");
            }
            --top;
            break;
        case PixelMathOpcode::JumpIfZero:
        case PixelMathOpcode::Jump:
            return evaluationFailure(ErrorCode::Internal, "a program that branches was evaluated a run at a time");
        }
    }

    if (top != 1u)
    {
        return evaluationFailure(ErrorCode::Internal, "the expression did not leave exactly one value on its stack");
    }
    std::copy_n(stack, count, values.data());
    return Status::success();
}

} // namespace an
