// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the scalar meaning of a pointwise chain, one pixel at a time.
//
// Written to be read rather than to be fast. Everything that will ever execute one of these chains
// is defined as producing what this produces, so the only property this code needs is that its
// behaviour is obvious from looking at it: one loop over the steps, one switch, one arithmetic
// expression per step, in the order the chain names them.
//
// The width is a template parameter and the program picks which instantiation runs, because the
// declared width is part of what a program means. Evaluating a Float32 chain in double would
// produce answers no executor of that chain will ever produce, which would make this a worse oracle
// than no oracle at all.

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseEvaluator.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/PlaneView.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace an
{
namespace
{

/// Whether two spans of samples share any storage.
[[nodiscard]] bool spansOverlap(std::span<const double> left, std::span<const double> right) noexcept
{
    if (left.empty() || right.empty())
    {
        return false;
    }
    return regionsOverlap(left.data(), static_cast<std::uint64_t>(left.size_bytes()), right.data(),
                          static_cast<std::uint64_t>(right.size_bytes()));
}

/// Whether `left comparison right` holds at one width. Every relation but `NotEqual` is false for a
/// not-a-number, as IEC 60559 comparison defines it.
template <typename ComputeT>
[[nodiscard]] bool compareValues(ComputeT left, ComputeT right, PointwiseComparison comparison) noexcept
{
    switch (comparison)
    {
    case PointwiseComparison::Less:
        return left < right;
    case PointwiseComparison::LessOrEqual:
        return left <= right;
    case PointwiseComparison::Equal:
        return left == right;
    case PointwiseComparison::NotEqual:
        return left != right;
    case PointwiseComparison::GreaterOrEqual:
        return left >= right;
    case PointwiseComparison::Greater:
        return left > right;
    }
    return false;
}

/// One sampled-curve evaluation at the program's declared arithmetic width.
///
/// The value becomes a fractional table position first. A not-a-number stays one and never becomes
/// an index; a position outside the table is refused, moved to the nearer end, or left for the
/// linear branch to extrapolate along the end segment, as the step's domain policy says.
template <typename ComputeT>
[[nodiscard]] Result<ComputeT> evaluateSampledCurve(const PointwiseProgram& program, const PointwiseOp& op,
                                                    ComputeT value)
{
    const std::span<const double> entries = program.table(op.table);
    ComputeT position = (value - static_cast<ComputeT>(op.low)) /
                        (static_cast<ComputeT>(op.high) - static_cast<ComputeT>(op.low)) *
                        static_cast<ComputeT>(entries.size() - 1u);
    const ComputeT last = static_cast<ComputeT>(entries.size() - 1u);
    if (!(position >= static_cast<ComputeT>(0)) || position > last)
    {
        if (std::isnan(position))
        {
            return std::numeric_limits<ComputeT>::quiet_NaN();
        }
        if (op.domainPolicy == PointwiseCurveDomainPolicy::Reject)
        {
            return Status::failure(
                ErrorCode::OutOfRange,
                StatusDetail{.message = "a sampled curve was given a value outside its declared domain",
                             .operation = "evaluatePointwisePixel",
                             .domain = ErrorDomain::Algorithms});
        }
        if (op.domainPolicy == PointwiseCurveDomainPolicy::Clamp)
        {
            position = position < static_cast<ComputeT>(0) ? static_cast<ComputeT>(0) : last;
        }
    }

    if (op.sampling == PointwiseCurveSampling::Nearest)
    {
        const ComputeT rounded = std::floor(position + static_cast<ComputeT>(0.5));
        const std::size_t index = static_cast<std::size_t>((std::min)((std::max)(rounded, ComputeT{0}), last));
        return static_cast<ComputeT>(entries[index]);
    }
    if (op.sampling == PointwiseCurveSampling::Lower)
    {
        const ComputeT bounded = (std::min)((std::max)(position, ComputeT{0}), last);
        const std::size_t index = static_cast<std::size_t>(std::floor(bounded));
        return static_cast<ComputeT>(entries[index]);
    }

    std::ptrdiff_t lower = 0;
    if (position <= static_cast<ComputeT>(0))
    {
        lower = 0;
    }
    else if (position >= last)
    {
        lower = static_cast<std::ptrdiff_t>(entries.size() - 2u);
    }
    else
    {
        lower = static_cast<std::ptrdiff_t>(std::floor(position));
    }
    const ComputeT fraction = position - static_cast<ComputeT>(lower);
    const ComputeT first = static_cast<ComputeT>(entries[static_cast<std::size_t>(lower)]);
    const ComputeT second = static_cast<ComputeT>(entries[static_cast<std::size_t>(lower + 1)]);
    if (first == second)
    {
        return first;
    }
    return first + (second - first) * fraction;
}

/// One pixel through one chain, at one width.
///
/// The working pixel starts as the primary source's channels converted to `ComputeT`; each step
/// rewrites it in place, reading any other plane from `sources`, which still hold the original
/// values. A scale immediately followed by a bias becomes one `std::fma` only when the program
/// allows contraction. The output is written only once every step has succeeded.
template <typename ComputeT>
[[nodiscard]] Status evaluateAs(const PointwiseProgram& program, std::span<const double> sources,
                                std::span<double> output)
{
    const std::size_t channels = program.channelCount();
    std::array<ComputeT, kMaxPointwiseChannels> pixel{};
    std::array<ComputeT, kMaxPointwiseChannels> mixed{};

    for (std::size_t channel = 0u; channel < channels; ++channel)
    {
        pixel[channel] = static_cast<ComputeT>(sources[channel]);
    }

    const std::span<const PointwiseOp> steps = program.steps();
    for (std::size_t step = 0u; step < steps.size(); ++step)
    {
        const PointwiseOp& op = steps[step];
        switch (op.kind)
        {
        case PointwiseOpKind::Scale:
            if (program.contractionAllowed() && step + 1u < steps.size() &&
                steps[step + 1u].kind == PointwiseOpKind::Bias)
            {
                const ComputeT factor = static_cast<ComputeT>(op.factor);
                const ComputeT offset = static_cast<ComputeT>(steps[step + 1u].offset);
                for (std::size_t channel = 0u; channel < channels; ++channel)
                {
                    pixel[channel] = std::fma(pixel[channel], factor, offset);
                }
                ++step;
                break;
            }
            for (std::size_t channel = 0u; channel < channels; ++channel)
            {
                pixel[channel] = pixel[channel] * static_cast<ComputeT>(op.factor);
            }
            break;

        case PointwiseOpKind::Bias:
            for (std::size_t channel = 0u; channel < channels; ++channel)
            {
                pixel[channel] = pixel[channel] + static_cast<ComputeT>(op.offset);
            }
            break;

        case PointwiseOpKind::Clamp:
            for (std::size_t channel = 0u; channel < channels; ++channel)
            {
                // Written as two one-sided comparisons rather than as a min of a max, so that a
                // value that is not a number satisfies neither and is left exactly as it was.
                if (pixel[channel] < static_cast<ComputeT>(op.low))
                {
                    pixel[channel] = static_cast<ComputeT>(op.low);
                }
                else if (pixel[channel] > static_cast<ComputeT>(op.high))
                {
                    pixel[channel] = static_cast<ComputeT>(op.high);
                }
            }
            break;

        case PointwiseOpKind::Gamma:
            for (std::size_t channel = 0u; channel < channels; ++channel)
            {
                // A negative base is not a number at every exponent, as the compiled run answers it; std::pow would
                // answer a real power at a whole-number exponent, and a row would differ between its vector part
                // and its tail.
                pixel[channel] = pixel[channel] < static_cast<ComputeT>(0)
                                     ? std::numeric_limits<ComputeT>::quiet_NaN()
                                     : std::pow(pixel[channel], static_cast<ComputeT>(op.factor));
            }
            break;

        case PointwiseOpKind::Lut: {
            const std::span<const double> entries = program.table(op.table);
            for (std::size_t channel = 0u; channel < channels; ++channel)
            {
                const double index = sources[channel];
                if (!(index >= 0.0) || index != std::floor(index) || static_cast<std::size_t>(index) >= entries.size())
                {
                    return Status::failure(
                        ErrorCode::OutOfRange,
                        StatusDetail{.message = "a lookup step was given a sample that is not one of its table's "
                                                "indices; the samples do not match the representation the program "
                                                "was built for",
                                     .operation = "evaluatePointwisePixel",
                                     .domain = ErrorDomain::Algorithms,
                                     .context = {StatusContextField::ofCount(ContextKey::Available, entries.size())}});
                }
                pixel[channel] = static_cast<ComputeT>(entries[static_cast<std::size_t>(index)]);
            }
            break;
        }

        case PointwiseOpKind::ChannelMatrix: {
            const std::span<const double> coefficients = program.table(op.table);
            for (std::size_t row = 0u; row < channels; ++row)
            {
                ComputeT sum = static_cast<ComputeT>(0);
                for (std::size_t column = 0u; column < channels; ++column)
                {
                    sum = sum + pixel[column] * static_cast<ComputeT>(coefficients[row * channels + column]);
                }
                mixed[row] = sum;
            }
            for (std::size_t channel = 0u; channel < channels; ++channel)
            {
                pixel[channel] = mixed[channel];
            }
            break;
        }

        case PointwiseOpKind::Mask:
            for (std::size_t channel = 0u; channel < channels; ++channel)
            {
                pixel[channel] = pixel[channel] * static_cast<ComputeT>(sources[op.operand * channels + channel]);
            }
            break;

        case PointwiseOpKind::Blend:
            for (std::size_t channel = 0u; channel < channels; ++channel)
            {
                const ComputeT toward = static_cast<ComputeT>(sources[op.operand * channels + channel]);
                const ComputeT weight = static_cast<ComputeT>(op.factor);
                pixel[channel] = pixel[channel] * (static_cast<ComputeT>(1) - weight) + toward * weight;
            }
            break;

        case PointwiseOpKind::Add:
        case PointwiseOpKind::Subtract:
        case PointwiseOpKind::Multiply:
        case PointwiseOpKind::Divide:
        case PointwiseOpKind::Minimum:
        case PointwiseOpKind::Maximum:
        case PointwiseOpKind::Compare:
            for (std::size_t channel = 0u; channel < channels; ++channel)
            {
                const ComputeT operand = static_cast<ComputeT>(sources[op.operand * channels + channel]);
                const ComputeT factor = static_cast<ComputeT>(op.factor);
                switch (op.kind)
                {
                case PointwiseOpKind::Add:
                    pixel[channel] = pixel[channel] + operand * factor;
                    break;
                case PointwiseOpKind::Subtract:
                    pixel[channel] = pixel[channel] - operand * factor;
                    break;
                case PointwiseOpKind::Multiply:
                    pixel[channel] = pixel[channel] * operand * factor;
                    break;
                case PointwiseOpKind::Divide:
                    pixel[channel] = pixel[channel] / operand * factor;
                    break;
                case PointwiseOpKind::Minimum:
                    pixel[channel] = std::fmin(pixel[channel], operand);
                    break;
                case PointwiseOpKind::Maximum:
                    pixel[channel] = std::fmax(pixel[channel], operand);
                    break;
                case PointwiseOpKind::Compare:
                    pixel[channel] = compareValues(pixel[channel], operand, op.comparison) ? static_cast<ComputeT>(1)
                                                                                           : static_cast<ComputeT>(0);
                    break;
                default:
                    break;
                }
            }
            break;

        case PointwiseOpKind::SampledCurve:
            for (std::size_t channel = 0u; channel < channels; ++channel)
            {
                const Result<ComputeT> curved = evaluateSampledCurve(program, op, pixel[channel]);
                if (!curved.hasValue())
                {
                    return curved.status();
                }
                pixel[channel] = *curved;
            }
            break;

        case PointwiseOpKind::Unknown:
            return Status::failure(ErrorCode::Internal,
                                   StatusDetail{.message = "a validated program holds a step this build cannot "
                                                           "execute, which is a defect in its validation",
                                                .operation = "evaluatePointwisePixel",
                                                .domain = ErrorDomain::Algorithms});
        }
    }

    for (std::size_t channel = 0u; channel < channels; ++channel)
    {
        output[channel] = static_cast<double>(pixel[channel]);
    }
    return Status::success();
}

} // namespace

Status evaluatePointwisePixel(const PointwiseProgram& program, std::span<const double> sources,
                              std::span<double> output)
{
    const std::size_t channels = program.channelCount();
    const std::size_t expectedSources = channels * program.sourceCount();
    if (sources.size() != expectedSources || output.size() != channels)
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{.message = "the pixel spans are not the sizes this program reads and writes",
                         .operation = "evaluatePointwisePixel",
                         .domain = ErrorDomain::Algorithms,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, sources.size()),
                                     StatusContextField::ofCount(ContextKey::Available, expectedSources)}});
    }
    if (spansOverlap(sources, std::span<const double>(output)))
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{.message = "the output pixel shares storage with the sources, so a step would read a value "
                                    "an earlier step had already replaced",
                         .operation = "evaluatePointwisePixel",
                         .domain = ErrorDomain::Algorithms});
    }

    return program.precision() == ComputePrecision::Float64 ? evaluateAs<double>(program, sources, output)
                                                            : evaluateAs<float>(program, sources, output);
}

} // namespace an
