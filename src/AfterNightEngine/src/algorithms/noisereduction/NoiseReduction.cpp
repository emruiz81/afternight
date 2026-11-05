// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the four native noise-reduction operations.
//
// Each of them decides that a sample is wrong and replaces it. What they share, and what this file
// exists to keep them sharing, is where that decision is made: on the stored numbers, under the
// neighbourhood family's own border and validity rules, with the count of what moved reported.
//
// Nothing here implements a border, and the only rank written here is the banding suppression's
// median of one line, which uses the interpolation the engine's own rank uses. The impulse rejector
// asks the order filter for its neighbourhood median and the banding suppression asks
// `selectPlaneOrderStatistics` for a channel's background, so one edge rule and one definition of a
// median serve the engine. What is written here is the arithmetic that follows: two Gaussians
// multiplied together, a green ceiling, a line offset, and a relative deviation.
//
// Two rows have a compiled run in the CPU dispatch table. The bilateral interior differs from its
// scalar loop only in the tier of the range exponential, so the two answers agree closely rather
// than to the bit; the two neutral chromatic ceilings are exact in either width, so their compiled
// and scalar rows store identical bits.

#include <AfterNightEngine/Algorithms/NoiseReduction/NoiseReduction.h>

#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Algorithms/Spatial/OrderFilter.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/AlgorithmKernels.h"
#include "kernels/reference/ReferenceKernelInternal.h"
#include "kernels/reference/SpatialPlane.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace an
{
namespace
{

using spatial_detail::SpatialGeometry;
using spatial_detail::SpatialPlaneBuffer;

constexpr const char* kBilateralPlanOperation = "an::planBilateralFilter";
constexpr const char* kBilateralOperation = "an::filterImageBilateral";
constexpr const char* kChromaticPlanOperation = "an::planChromaticNoiseReduction";
constexpr const char* kChromaticOperation = "an::reduceImageChromaticNoise";
constexpr const char* kBandingOperation = "an::suppressImageBanding";
constexpr const char* kImpulseOperation = "an::rejectImageImpulseNoise";

/// One failure from this file, spelled the way the neighbourhood family spells one.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status noiseFailure(ErrorCode code, const char* operation, const char* message)
{
    return spatial_detail::spatialFailure(code, operation, message);
}

/// One failure carrying what was asked for and what was allowed.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @param requested the number the caller supplied.
/// @param limit the number the contract allows.
/// @return the failure, carrying both counts.
[[nodiscard]] Status noiseLimitFailure(ErrorCode code, const char* operation, const char* message,
                                       std::uint64_t requested, std::uint64_t limit)
{
    return spatial_detail::spatialLimitFailure(code, operation, message, requested, limit);
}

/// How much work one row of a plane pass is.
///
/// @param width how many samples one row holds.
/// @param perSample how many operations each sample costs.
/// @return the cost.
[[nodiscard]] TaskCost rowCost(std::uint32_t width, std::uint64_t perSample) noexcept
{
    const std::uint64_t samples = static_cast<std::uint64_t>(width);
    return TaskCost{.bytesPerItem = samples * sizeof(float), .operationsPerItem = samples * perSample};
}

/// Where one channel's first sample of one row sits in a possibly interleaved image.
///
/// @param layout the image layout.
/// @param channel which channel.
/// @param row which row.
/// @return the byte offset from the image base.
[[nodiscard]] std::uint64_t channelRowByteOffset(const ImageLayout& layout, std::uint32_t channel,
                                                 std::uint64_t row) noexcept
{
    const std::uint32_t plane = channel / layout.samplesPerPixel();
    const std::uint32_t withinPixel = channel % layout.samplesPerPixel();
    return static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() + row * layout.rowStrideBytes() +
           static_cast<std::uint64_t>(withinPixel) * layout.elementSizeBytes();
}

/// Whether a policy states an output promise and a write-back this family can honour.
///
/// @param output the policy.
/// @param operation the entry point the caller is inside.
/// @return success, or the first reason it cannot be honoured.
[[nodiscard]] Status checkOutputPolicy(const SpatialOutputPolicy& output, const char* operation)
{
    if (!isOutputPrecisionContract(output.contract))
    {
        return noiseFailure(ErrorCode::InvalidArgument, operation, "the output contract is not one this engine knows");
    }
    const Status policy = checkConversionPolicy(output.writeBack, operation);
    if (policy.failed())
    {
        return policy;
    }
    if (output.writeBack.domain != DomainMapping::CodeValue)
    {
        return noiseFailure(ErrorCode::InvalidArgument, operation,
                            "a noise reduction carries its samples across as the numbers they are; a full-scale "
                            "domain mapping would make it a filter and a domain change at once");
    }
    return Status::success();
}

/// Whether a border policy names a rule this family implements and finite substitutes.
///
/// @param borders the policy.
/// @param operation the entry point the caller is inside.
/// @return success, or why the policy is not usable.
[[nodiscard]] Status checkBorders(const StencilBorderPolicy& borders, const char* operation)
{
    if (borders.border == KernelBorderMode::NotApplicable || !isKernelBorderMode(borders.border))
    {
        return noiseFailure(ErrorCode::InvalidArgument, operation,
                            "the border must be one of the modes that supply a neighbour");
    }
    if (!std::isfinite(borders.constantBorder) || !std::isfinite(borders.invalidNeighbor))
    {
        return noiseFailure(ErrorCode::InvalidArgument, operation,
                            "the border and invalid-neighbour substitutes must be finite");
    }
    return Status::success();
}

/// One entry of a bilateral window: where a neighbour is, and how much distance alone allows it.
struct BilateralTap
{
    /// Column offset from the centre.
    std::int32_t dx = 0;

    /// Row offset from the centre.
    std::int32_t dy = 0;

    /// The spatial half of the weight, which does not depend on any sample.
    float weight = 0.0f;
};

/// How many window positions of a square of this radius lie inside its inscribed disc.
///
/// @param radius half the window.
/// @return the count, which always includes the centre.
[[nodiscard]] std::uint32_t discSamples(std::uint32_t radius) noexcept
{
    const std::int64_t limit = static_cast<std::int64_t>(radius) * static_cast<std::int64_t>(radius);
    std::uint32_t count = 0u;
    for (std::int64_t dy = -static_cast<std::int64_t>(radius); dy <= static_cast<std::int64_t>(radius); ++dy)
    {
        for (std::int64_t dx = -static_cast<std::int64_t>(radius); dx <= static_cast<std::int64_t>(radius); ++dx)
        {
            if (dx * dx + dy * dy <= limit)
            {
                ++count;
            }
        }
    }
    return count;
}

/// One bilateral output sample under the full border and validity rules.
///
/// The window is resolved one neighbour at a time: a neighbour outside the plane is answered by
/// the border rule and one the validity rule rejects by the invalid-neighbour substitute. The
/// weights are Float32 - a weight is a computed value - and the sums are Float64, which is the
/// arithmetic contract of the whole neighbourhood family rather than a choice this operation makes
/// for itself.
///
/// @param base the plane's first sample.
/// @param rowStride samples from one row to the next.
/// @param width samples per row.
/// @param height rows.
/// @param x the centre column.
/// @param y the centre row.
/// @param centre the centre sample.
/// @param borders what a neighbour outside the plane or rejected reads as.
/// @param accepts the validity rule.
/// @param taps the whole window, spatial weights included.
/// @param rangeScale the negated reciprocal of twice the range variance.
/// @return the filtered sample.
[[nodiscard]] float bilateralSampleGeneral(const float* base, std::int64_t rowStride, std::int64_t width,
                                           std::int64_t height, std::uint32_t x, std::uint32_t y, float centre,
                                           const StencilBorderPolicy& borders,
                                           const reference_detail::ValidityAccessor& accepts,
                                           std::span<const BilateralTap> taps, float rangeScale) noexcept
{
    double weighted = 0.0;
    double weights = 0.0;
    for (const BilateralTap& tap : taps)
    {
        const std::int64_t sampleX = kernelBorderIndex(static_cast<std::int64_t>(x) + tap.dx, width, borders.border);
        const std::int64_t sampleY = kernelBorderIndex(static_cast<std::int64_t>(y) + tap.dy, height, borders.border);
        float neighbour = borders.constantBorder;
        if (sampleX != width && sampleY != height)
        {
            const auto column = static_cast<std::uint32_t>(sampleX);
            const auto line = static_cast<std::uint32_t>(sampleY);
            const float stored = base[sampleY * rowStride + sampleX];
            neighbour = accepts.accepts(stored, column, line) ? stored : borders.invalidNeighbor;
        }
        const float difference = neighbour - centre;
        const float weight = tap.weight * std::exp(difference * difference * rangeScale);
        weighted += static_cast<double>(weight) * static_cast<double>(neighbour);
        weights += static_cast<double>(weight);
    }
    // The centre is inside the disc with both weights at one, so the sum is never zero and the
    // division needs no case of its own.
    return static_cast<float>(weighted / weights);
}

/// One interior bilateral output sample, whose every neighbour is inside the plane and valid.
///
/// The scalar authority for the compiled row run: the same taps in the same order, the range
/// exponential through the platform library.
///
/// @param centrePtr the centre sample's address.
/// @param tapOffsets each neighbour's displacement from the centre, in samples.
/// @param tapWeights each tap's spatial weight.
/// @param rangeScale the negated reciprocal of twice the range variance.
/// @return the filtered sample.
[[nodiscard]] float bilateralSampleInterior(const float* centrePtr, std::span<const std::int64_t> tapOffsets,
                                            std::span<const float> tapWeights, float rangeScale) noexcept
{
    const float centre = *centrePtr;
    double weighted = 0.0;
    double weights = 0.0;
    for (std::size_t tap = 0u; tap < tapOffsets.size(); ++tap)
    {
        const float neighbour = centrePtr[tapOffsets[tap]];
        const float difference = neighbour - centre;
        const float weight = tapWeights[tap] * std::exp(difference * difference * rangeScale);
        weighted += static_cast<double>(weight) * static_cast<double>(neighbour);
        weights += static_cast<double>(weight);
    }
    return static_cast<float>(weighted / weights);
}

/// Fills a copy of one plane bordered by `radius` samples on every side, so that every window of
/// the copy is inside it.
///
/// Each bordered sample is what the border rule says for its coordinate: a rule that answers an
/// outside coordinate with an inside one reads that sample, and one that answers with no
/// coordinate reads the constant. This is the resolution `bilateralSampleGeneral` performs per
/// tap, performed once per bordered sample instead of once per tap per output.
///
/// @param execution the operation policy, for the workers and the cancellation checkpoint.
/// @param source the plane to copy.
/// @param borders the rule.
/// @param radius the border width in samples.
/// @param bordered the copy, `width + 2 * radius` by `height + 2 * radius` samples, packed.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status fillBorderedPlane(const ExecutionContext& execution, PlaneView<const float> source,
                                       const StencilBorderPolicy& borders, std::uint32_t radius,
                                       std::span<float> bordered)
{
    const std::int64_t width = static_cast<std::int64_t>(source.width());
    const std::int64_t height = static_cast<std::int64_t>(source.height());
    const std::int64_t margin = radius;
    const std::int64_t borderedWidth = width + 2 * margin;
    const std::int64_t borderedHeight = height + 2 * margin;
    const float* const base = source.data();
    const std::int64_t rowStride = static_cast<std::int64_t>(source.rowStrideBytes() / sizeof(float));
    return parallelFor(execution, static_cast<std::uint64_t>(borderedHeight),
                       rowCost(static_cast<std::uint32_t>(borderedWidth), 1u), [&](TaskRange range) {
                           for (std::uint64_t row = range.begin; row < range.end; ++row)
                           {
                               float* const out = bordered.data() + static_cast<std::int64_t>(row) * borderedWidth;
                               const std::int64_t sourceY =
                                   kernelBorderIndex(static_cast<std::int64_t>(row) - margin, height, borders.border);
                               if (sourceY == height)
                               {
                                   std::fill_n(out, borderedWidth, borders.constantBorder);
                                   continue;
                               }
                               const float* const in = base + sourceY * rowStride;
                               for (std::int64_t column = 0; column < margin; ++column)
                               {
                                   const std::int64_t left = kernelBorderIndex(column - margin, width, borders.border);
                                   out[column] = left == width ? borders.constantBorder : in[left];
                                   const std::int64_t right = kernelBorderIndex(width + column, width, borders.border);
                                   out[margin + width + column] = right == width ? borders.constantBorder : in[right];
                               }
                               std::copy_n(in, width, out + margin);
                           }
                           return Status::success();
                       });
}

/// Runs a bilateral filter between two Float32 planes.
///
/// Every output is the sum over its own window in tap order, so its bytes do not depend on which
/// worker reached it. A plane whose every sample is valid is read through `bordered`, a copy that
/// carries the border rule already applied, so every output - the border pixels included - reads
/// a window that is inside the copy and goes through the execution's compiled row run where its
/// table has one, which evaluates the range exponential for a vector of adjacent outputs at a time
/// and keeps their sums in registers; without one it is summed one tap at a time through the
/// platform exponential. A plane with a validity rule or a mask, or one whose copy the context
/// refused, resolves each neighbour of each output under the full border and validity rules.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the plane to read.
/// @param destination the equally shaped plane to write, disjoint from the source.
/// @param validity the rule and optional mask deciding which samples mean something.
/// @param plan the validated window and sigmas.
/// @param taps the precomputed disc, spatial weights included.
/// @param tapOffsets each tap's displacement in samples of the bordered copy, written here from
///        its width.
/// @param tapWeights each tap's spatial weight, in the same order.
/// @param bordered storage for the bordered copy, `width + 2 * radius` by `height + 2 * radius`
///        samples, or empty when the context refused it.
/// @param rowRun the execution's compiled row run, or null for the scalar interior.
/// @param written how many destination samples were produced, added to whatever it holds.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status bilateralPlane(const ExecutionContext& execution, PlaneView<const float> source,
                                    PlaneView<float> destination, const KernelValidityView& validity,
                                    const BilateralPlan& plan, std::span<const BilateralTap> taps,
                                    std::span<std::int64_t> tapOffsets, std::span<const float> tapWeights,
                                    std::span<float> bordered, BilateralRowRunFn rowRun, std::uint64_t& written)
{
    const std::int64_t width = static_cast<std::int64_t>(source.width());
    const std::int64_t height = static_cast<std::int64_t>(source.height());
    const std::uint32_t radius = plan.radius();
    const StencilBorderPolicy borders = plan.request().borders;
    const float rangeScale = static_cast<float>(-0.5 / (plan.request().rangeSigma * plan.request().rangeSigma));
    const reference_detail::ValidityAccessor accepts(validity);
    const bool everySampleValid = validity.mask == nullptr && validity.descriptor.rule() == ValidityRule::AllValid;

    if (everySampleValid && !bordered.empty())
    {
        const std::int64_t margin = radius;
        const std::int64_t borderedWidth = width + 2 * margin;
        const Status filled = fillBorderedPlane(execution, source, borders, radius, bordered);
        if (filled.failed())
        {
            return filled;
        }
        for (std::size_t tap = 0u; tap < taps.size(); ++tap)
        {
            tapOffsets[tap] = static_cast<std::int64_t>(taps[tap].dy) * borderedWidth + taps[tap].dx;
        }
        const std::span<const std::int64_t> offsets{tapOffsets.data(), tapOffsets.size()};
        const Status walked =
            parallelFor(execution, source.height(), rowCost(source.width(), taps.size() * 8u), [&](TaskRange range) {
                for (std::uint64_t row = range.begin; row < range.end; ++row)
                {
                    const float* const centrePtr =
                        bordered.data() + (static_cast<std::int64_t>(row) + margin) * borderedWidth + margin;
                    const std::span<float> output = *destination.row(static_cast<std::uint32_t>(row));
                    if (rowRun != nullptr)
                    {
                        rowRun(centrePtr, static_cast<std::uint64_t>(width), offsets.data(), tapWeights.data(),
                               static_cast<std::uint32_t>(offsets.size()), rangeScale, output.data());
                        continue;
                    }
                    for (std::int64_t x = 0; x < width; ++x)
                    {
                        output[static_cast<std::size_t>(x)] =
                            bilateralSampleInterior(centrePtr + x, offsets, tapWeights, rangeScale);
                    }
                }
                return Status::success();
            });
        if (walked.failed())
        {
            return walked;
        }
        written += static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
        return Status::success();
    }

    const float* const base = source.data();
    const std::int64_t rowStride = static_cast<std::int64_t>(source.rowStrideBytes() / sizeof(float));
    std::atomic<std::uint64_t> produced{0u};
    const Status walked =
        parallelFor(execution, source.height(), rowCost(source.width(), taps.size() * 8u), [&](TaskRange range) {
            std::uint64_t local = 0u;
            for (std::uint64_t row = range.begin; row < range.end; ++row)
            {
                const auto y = static_cast<std::uint32_t>(row);
                const std::span<const float> input = *source.row(y);
                const std::span<float> output = *destination.row(y);
                for (std::uint32_t x = 0u; x < static_cast<std::uint32_t>(width); ++x)
                {
                    const float centre = input[x];
                    if (!everySampleValid && !accepts.accepts(centre, x, y))
                    {
                        continue;
                    }
                    output[x] = bilateralSampleGeneral(base, rowStride, width, height, x, y, centre, borders, accepts,
                                                       taps, rangeScale);
                    ++local;
                }
            }
            produced.fetch_add(local, std::memory_order_relaxed);
            return Status::success();
        });
    if (walked.failed())
    {
        return walked;
    }
    written += produced.load(std::memory_order_relaxed);
    return Status::success();
}

/// The natural-unit CIE Lab encoding under one white point.
///
/// @param whitePoint the illuminant the RGB encoding is referred to.
/// @return the encoding.
[[nodiscard]] ColorEncoding naturalLab(ChromaticWhitePoint whitePoint) noexcept
{
    return ColorEncoding{.schema = ChannelSchema::Lab,
                         .primaries = RgbPrimaries::Srgb,
                         .transfer = ColorTransferFunction::Linear,
                         .whitePoint = whitePoint,
                         .perceptual = PerceptualEncoding::NaturalUnits};
}

/// What the green channel of one pixel becomes.
///
/// @param method which ceiling green is held to.
/// @param red the first channel, in the image's own units.
/// @param green the second channel.
/// @param blue the third channel.
/// @param amount how much of the unsuppressed green the mask methods keep.
/// @param whiteLevel the value that means fully bright.
/// @return the suppressed green, in the image's own units.
[[nodiscard]] double suppressGreen(ChromaticNoiseMethod method, double red, double green, double blue, double amount,
                                   double whiteLevel) noexcept
{
    switch (method)
    {
    case ChromaticNoiseMethod::AverageNeutral:
        return (std::min)(green, 0.5 * (red + blue));
    case ChromaticNoiseMethod::MaximumNeutral:
        return (std::min)(green, (std::max)(red, blue));
    case ChromaticNoiseMethod::MaximumMask: {
        const double mask = (std::max)(red, blue) / whiteLevel;
        return green * ((1.0 - amount) * (1.0 - mask) + mask);
    }
    case ChromaticNoiseMethod::AdditiveMask: {
        const double mask = (std::min)(whiteLevel, red + blue) / whiteLevel;
        return green * ((1.0 - amount) * (1.0 - mask) + mask);
    }
    }
    return green;
}

/// One row of the plain green suppression in Float32, for the two ceilings exact in either width.
///
/// A minimum or a maximum is exact in any width; a sum of two Float32 samples is exact in Float64
/// and rounds once in Float32, and halving commutes with that rounding; and a clamp moves nothing.
/// So for the two neutral ceilings this loop stores the bits the Float64 loop stores, short of
/// subnormal sums, and having no call and no widening in it, it is the loop the compiler
/// vectorizes. The mask ceilings multiply by an amount and stay in the Float64 loop.
///
/// @tparam MethodV `ChromaticNoiseMethod::AverageNeutral` or `ChromaticNoiseMethod::MaximumNeutral`.
/// @param red the first channel's row.
/// @param green the second channel's row.
/// @param blue the third channel's row.
/// @param outRed where the first channel goes.
/// @param outGreen where the held second channel goes.
/// @param outBlue where the third channel goes.
/// @param width samples per row.
/// @param whiteLevel the value that means fully bright, exactly representable in Float32.
/// @return how many stored samples differ from their inputs.
template <ChromaticNoiseMethod MethodV>
[[nodiscard]] std::uint64_t suppressRowExact(const float* red, const float* green, const float* blue, float* outRed,
                                             float* outGreen, float* outBlue, std::uint32_t width,
                                             float whiteLevel) noexcept
{
    static_assert(MethodV == ChromaticNoiseMethod::AverageNeutral || MethodV == ChromaticNoiseMethod::MaximumNeutral);
    // Spelled as comparisons rather than through the library minimum, maximum and clamp so that
    // the compiler emits the branch-free vector selects a random picture needs; the selections
    // keep the library ordering, including its answer for a not-a-number, so the bits agree.
    const auto lower = [](float value, float ceiling) noexcept { return ceiling < value ? ceiling : value; };
    const auto upper = [](float value, float floor) noexcept { return value < floor ? floor : value; };
    std::uint32_t changed = 0u;
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        const float r = red[x];
        const float g = green[x];
        const float b = blue[x];
        const float ceiling = MethodV == ChromaticNoiseMethod::AverageNeutral ? 0.5f * (r + b) : upper(r, b);
        const float held = lower(g, ceiling);
        const float storedRed = lower(upper(r, 0.0f), whiteLevel);
        const float storedGreen = lower(upper(held, 0.0f), whiteLevel);
        const float storedBlue = lower(upper(b, 0.0f), whiteLevel);
        changed += static_cast<std::uint32_t>(storedRed != r) + static_cast<std::uint32_t>(storedGreen != g) +
                   static_cast<std::uint32_t>(storedBlue != b);
        outRed[x] = storedRed;
        outGreen[x] = storedGreen;
        outBlue[x] = storedBlue;
    }
    return changed;
}

/// The median of a scratch buffer, under the interpolation the engine's own rank uses.
///
/// @param values the samples, reordered in place.
/// @param count how many of them there are, at least one.
/// @return the value at one half.
[[nodiscard]] double medianOf(float* values, std::size_t count) noexcept
{
    const std::size_t middle = count / 2u;
    std::nth_element(values, values + middle, values + count);
    const double upper = static_cast<double>(values[middle]);
    if (count % 2u == 1u)
    {
        return upper;
    }
    const float* const lower = std::max_element(values, values + middle);
    return 0.5 * (static_cast<double>(*lower) + upper);
}

/// One channel of an image, widened into a Float32 plane the caller may read.
///
/// Borrows the caller's storage when the channel is already an addressable Float32 plane, and
/// otherwise gathers into `working`.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation.
/// @param samples where the image samples start.
/// @param layout where they sit.
/// @param channelIndex which channel.
/// @param geometry the picture.
/// @param working the plane to gather into when the channel cannot be borrowed.
/// @param operation the name carried by any failure.
/// @param plane the resulting read-only plane, written on success.
/// @return success, or a gather failure.
[[nodiscard]] Status readChannelPlane(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                                      const ImageLayout& layout, std::uint32_t channelIndex,
                                      const SpatialGeometry& geometry, const SpatialPlaneBuffer& working,
                                      const char* operation, std::optional<PlaneView<const float>>& plane)
{
    if (!working.owned())
    {
        plane = spatial_detail::borrowSourcePlane(samples, layout, channelIndex, sampleType, geometry);
        if (!plane.has_value())
        {
            return noiseFailure(ErrorCode::InvalidArgument, operation,
                                "a channel of this image is not addressable as a plane");
        }
        return Status::success();
    }
    const Status gathered = spatial_detail::gatherChannelPlane(execution, sampleType, samples, layout, channelIndex,
                                                               working.view(), operation);
    if (gathered.failed())
    {
        return gathered;
    }
    plane = working.constView();
    return Status::success();
}

/// Converts and writes the selected answers of one noise-reduction channel.
///
/// Unlike the spatial family's ordinary scatter, selection may depend on the other channels or on
/// a line-level decision. The exact stored value is compared with the exact source representation,
/// so a Float32 change that rounds away is not reported as a changed sample.
///
/// @tparam SelectT a callable taking `(x, y)` and returning whether this sample is written.
/// @param execution the operation policy and engine-context lease.
/// @param filtered the Float32 answers.
/// @param sampleType the source and destination representation.
/// @param source the original source storage.
/// @param sourceLayout its layout.
/// @param destination the destination storage.
/// @param destinationLayout its layout.
/// @param channel which channel to write.
/// @param conversion how the answer becomes a stored value.
/// @param operation the name carried by a failure.
/// @param selected which locations may be written.
/// @param changed exact stored-value changes, added on success.
/// @param written samples written, added on success.
/// @return success, or an execution, cancellation or representation failure.
template <typename SelectT>
[[nodiscard]] Status
scatterSelectedChannel(const ExecutionContext& execution, PlaneView<const float> filtered, SampleType sampleType,
                       const void* source, const ImageLayout& sourceLayout, void* destination,
                       const ImageLayout& destinationLayout, std::uint32_t channel, const ConversionPolicy& conversion,
                       const char* operation, SelectT&& selected, std::uint64_t& changed, std::uint64_t& written)
{
    Status dispatched = Status::success();
    std::atomic<std::uint64_t> changedCount{0u};
    std::atomic<std::uint64_t> writtenCount{0u};
    const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        dispatched = parallelFor(execution, filtered.height(), rowCost(filtered.width(), 5u), [&](TaskRange range) {
            std::uint64_t localChanged = 0u;
            std::uint64_t localWritten = 0u;
            for (std::uint64_t row = range.begin; row < range.end; ++row)
            {
                const auto* const from = reinterpret_cast<const SampleT*>(
                    static_cast<const std::uint8_t*>(source) + channelRowByteOffset(sourceLayout, channel, row));
                auto* const to = reinterpret_cast<SampleT*>(static_cast<std::uint8_t*>(destination) +
                                                            channelRowByteOffset(destinationLayout, channel, row));
                const std::span<const float> answer = *filtered.row(static_cast<std::uint32_t>(row));
                for (std::uint32_t x = 0u; x < filtered.width(); ++x)
                {
                    if (!selected(x, static_cast<std::uint32_t>(row)))
                    {
                        continue;
                    }
                    const SampleT stored = convertSample<float, SampleT>(answer[x], conversion).value;
                    const std::size_t sourceIndex = static_cast<std::size_t>(x) * sourceLayout.samplesPerPixel();
                    const std::size_t destinationIndex =
                        static_cast<std::size_t>(x) * destinationLayout.samplesPerPixel();
                    localChanged += stored != from[sourceIndex] ? 1u : 0u;
                    to[destinationIndex] = stored;
                    ++localWritten;
                }
            }
            changedCount.fetch_add(localChanged, std::memory_order_relaxed);
            writtenCount.fetch_add(localWritten, std::memory_order_relaxed);
            return Status::success();
        });
    });
    if (!known)
    {
        return noiseFailure(ErrorCode::InvalidArgument, operation,
                            "the representation is not one of the eight this engine stores");
    }
    if (dispatched.ok())
    {
        changed += changedCount.load(std::memory_order_relaxed);
        written += writtenCount.load(std::memory_order_relaxed);
    }
    return dispatched;
}

} // namespace

BilateralPlan::BilateralPlan(const BilateralRequest& request) noexcept : m_request(request)
{
    const double wanted = (std::max)(1.0, std::floor(kBilateralSpatialTruncation * request.spatialSigma + 0.5));
    const std::uint32_t derived =
        request.radius != 0u
            ? request.radius
            : (!std::isfinite(wanted) || wanted > static_cast<double>((std::numeric_limits<std::uint32_t>::max)())
                   ? (std::numeric_limits<std::uint32_t>::max)()
                   : static_cast<std::uint32_t>(wanted));
    m_radius = derived;
    m_windowSamples = derived <= kMaxBilateralRadius ? discSamples(derived) : 0u;
}

const BilateralRequest& BilateralPlan::request() const noexcept
{
    return m_request;
}

std::uint32_t BilateralPlan::radius() const noexcept
{
    return m_radius;
}

std::uint32_t BilateralPlan::windowSamples() const noexcept
{
    return m_windowSamples;
}

std::uint64_t BilateralPlan::imagePasses() const noexcept
{
    return 1u;
}

Result<BilateralPlan> planBilateralFilter(const BilateralRequest& request)
{
    if (!std::isfinite(request.spatialSigma) || request.spatialSigma <= kMinimumBilateralSigma ||
        !std::isfinite(request.rangeSigma) || request.rangeSigma <= kMinimumBilateralSigma)
    {
        return noiseFailure(ErrorCode::InvalidArgument, kBilateralPlanOperation,
                            "a bilateral filter needs a finite spatial and range sigma above the stated minimum");
    }
    const Status borders = checkBorders(request.borders, kBilateralPlanOperation);
    if (borders.failed())
    {
        return borders;
    }
    const Status output = checkOutputPolicy(request.output, kBilateralPlanOperation);
    if (output.failed())
    {
        return output;
    }
    const BilateralPlan plan{request};
    if (plan.radius() > kMaxBilateralRadius)
    {
        return noiseLimitFailure(ErrorCode::OutOfRange, kBilateralPlanOperation,
                                 "the stated or derived bilateral window exceeds the stated maximum radius",
                                 plan.radius(), kMaxBilateralRadius);
    }
    return plan;
}

Result<NoiseReductionResult> filterImageBilateral(const ExecutionContext& execution, SampleType sampleType,
                                                  const void* source, const ImageLayout& sourceLayout,
                                                  void* destination, const ImageLayout& destinationLayout,
                                                  const BilateralPlan& plan, const SpatialSelection& selection)
{
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           plan.request().output, selection, kBilateralOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    const Result<BilateralPlan> revalidated = planBilateralFilter(plan.request());
    if (!revalidated.hasValue())
    {
        return revalidated.status();
    }
    if (revalidated->radius() != plan.radius())
    {
        return noiseFailure(ErrorCode::InvalidArgument, kBilateralOperation,
                            "the plan was not produced by planBilateralFilter");
    }

    const std::uint32_t radius = plan.radius();
    const std::uint32_t tapCount = plan.windowSamples();
    // The window as taps, then the same taps as the sample offsets and spatial weights the interior
    // run reads side by side; the offsets are stamped per plane from its row stride.
    const std::uint64_t windowBytes =
        static_cast<std::uint64_t>(tapCount) * (sizeof(BilateralTap) + sizeof(std::int64_t) + sizeof(float));
    Result<plane_detail::ScopedContextMemory> window =
        plane_detail::ScopedContextMemory::take(execution, windowBytes, alignof(std::int64_t), kBilateralOperation);
    if (!window.hasValue())
    {
        return window.status();
    }
    auto* const tapOffsets = static_cast<std::int64_t*>(window->data());
    auto* const tapWeights = reinterpret_cast<float*>(tapOffsets + tapCount);
    auto* const taps = reinterpret_cast<BilateralTap*>(tapWeights + tapCount);
    const double spatialScale = -0.5 / (plan.request().spatialSigma * plan.request().spatialSigma);
    const std::int64_t limit = static_cast<std::int64_t>(radius) * static_cast<std::int64_t>(radius);
    std::uint32_t filled = 0u;
    for (std::int64_t dy = -static_cast<std::int64_t>(radius); dy <= static_cast<std::int64_t>(radius); ++dy)
    {
        for (std::int64_t dx = -static_cast<std::int64_t>(radius); dx <= static_cast<std::int64_t>(radius); ++dx)
        {
            const std::int64_t squared = dx * dx + dy * dy;
            if (squared > limit)
            {
                continue;
            }
            taps[filled] =
                BilateralTap{.dx = static_cast<std::int32_t>(dx),
                             .dy = static_cast<std::int32_t>(dy),
                             .weight = static_cast<float>(std::exp(static_cast<double>(squared) * spatialScale))};
            tapWeights[filled] = taps[filled].weight;
            tapOffsets[filled] = 0;
            ++filled;
        }
    }
    const BilateralRowRunFn rowRun = bilateralRowRunFor(execution);

    // The bordered copy a plane under the all-valid rule is read through, taken once and reused
    // for every plane. A refusal is not a failure of the operation, which then resolves each
    // neighbour of each output per tap and reports nothing more than the window.
    const std::uint64_t borderedSamples = (static_cast<std::uint64_t>(geometry.width) + 2u * radius) *
                                          (static_cast<std::uint64_t>(geometry.height) + 2u * radius);
    plane_detail::ScopedContextMemory borderedBlock;
    if (Result<plane_detail::ScopedContextMemory> taken = plane_detail::ScopedContextMemory::take(
            execution, borderedSamples * sizeof(float), kDefaultBufferAlignmentBytes, kBilateralOperation);
        taken.hasValue())
    {
        borderedBlock = std::move(*taken);
    }
    const std::span<float> bordered{static_cast<float*>(borderedBlock.data()),
                                    borderedBlock.data() != nullptr ? static_cast<std::size_t>(borderedSamples) : 0u};

    NoiseReductionResult result{};
    SpatialFilterResult& filter = result.filter;
    filter.horizontalRadius = radius;
    filter.verticalRadius = radius;
    filter.imagePasses = plan.imagePasses();
    filter.precision = ComputePrecision::Float32;
    filter.plannedWorkers = execution.admittedConcurrency();
    std::uint64_t written = 0u;
    const Status filtered = spatial_detail::filterEveryChannel(
        execution, sampleType, source, sourceLayout, destination, destinationLayout, selection,
        plan.request().output.writeBack, geometry, kBilateralOperation, filter,
        [&](PlaneView<const float> input, PlaneView<float> output, const KernelValidityView& validity) -> Status {
            return bilateralPlane(execution, input, output, validity, plan, std::span<const BilateralTap>{taps, filled},
                                  std::span<std::int64_t>{tapOffsets, filled},
                                  std::span<const float>{tapWeights, filled}, bordered, rowRun, written);
        },
        &result.samplesChanged);
    if (filtered.failed())
    {
        return filtered;
    }
    filter.workingBytes += window->bytes() + borderedBlock.bytes();
    const std::uint64_t samples = static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount;
    filter.run.selection = ValidityCount{.valid = written, .total = samples};
    filter.run.outputsWritten = written;
    return result;
}

const char* chromaticNoiseMethodName(ChromaticNoiseMethod method) noexcept
{
    switch (method)
    {
    case ChromaticNoiseMethod::AverageNeutral:
        return "AverageNeutral";
    case ChromaticNoiseMethod::MaximumNeutral:
        return "MaximumNeutral";
    case ChromaticNoiseMethod::MaximumMask:
        return "MaximumMask";
    case ChromaticNoiseMethod::AdditiveMask:
        return "AdditiveMask";
    }
    return "Unknown";
}

ChromaticNoisePlan::ChromaticNoisePlan(const ChromaticNoiseRequest& request, const ColorConversionPlan& toLab,
                                       const ColorConversionPlan& fromLab) noexcept
    : m_request(request), m_toLab(toLab), m_fromLab(fromLab)
{
}

const ChromaticNoiseRequest& ChromaticNoisePlan::request() const noexcept
{
    return m_request;
}

bool ChromaticNoisePlan::preservesLightness() const noexcept
{
    return m_request.preserveLightness;
}

const ColorConversionPlan& ChromaticNoisePlan::toLab() const noexcept
{
    return m_toLab;
}

const ColorConversionPlan& ChromaticNoisePlan::fromLab() const noexcept
{
    return m_fromLab;
}

Result<ChromaticNoisePlan> planChromaticNoiseReduction(const ChromaticNoiseRequest& request)
{
    if (!isChromaticNoiseMethod(request.method))
    {
        return noiseFailure(ErrorCode::InvalidArgument, kChromaticPlanOperation,
                            "the chromatic-noise method is not one this engine implements");
    }
    if (!std::isfinite(request.amount) || request.amount < 0.0 || request.amount > 1.0)
    {
        return noiseFailure(ErrorCode::InvalidArgument, kChromaticPlanOperation,
                            "the amount of a chromatic-noise reduction lies within the unit interval");
    }
    if (!std::isfinite(request.whiteLevel) || request.whiteLevel <= 0.0)
    {
        return noiseFailure(ErrorCode::InvalidArgument, kChromaticPlanOperation,
                            "a white level is a finite positive value in the image's own units");
    }
    const Status output = checkOutputPolicy(request.output, kChromaticPlanOperation);
    if (output.failed())
    {
        return output;
    }
    // The colour plans are built whether or not lightness is preserved, so that a plan is one shape
    // rather than two and a caller inspecting `toLab` never has to ask which case it is in. Building
    // them costs no allocation.
    ColorEncoding encoding = request.rgbEncoding;
    if (request.preserveLightness &&
        (checkColorEncoding(encoding) != ColorEncodingError::None || encoding.schema != ChannelSchema::Rgb))
    {
        return noiseFailure(ErrorCode::IncompatibleArguments, kChromaticPlanOperation,
                            "preserving lightness needs one valid three-channel RGB encoding");
    }
    if (checkColorEncoding(encoding) != ColorEncodingError::None || encoding.schema != ChannelSchema::Rgb)
    {
        encoding = ColorEncoding{.transfer = ColorTransferFunction::Srgb};
    }
    const ColorEncoding lab = naturalLab(encoding.whitePoint);
    const Result<ColorConversionPlan> toLab = planColorConversion(encoding, lab);
    if (!toLab.hasValue())
    {
        return toLab.status();
    }
    const Result<ColorConversionPlan> fromLab = planColorConversion(lab, encoding);
    if (!fromLab.hasValue())
    {
        return fromLab.status();
    }
    return ChromaticNoisePlan{request, *toLab, *fromLab};
}

Result<NoiseReductionResult> reduceImageChromaticNoise(const ExecutionContext& execution, SampleType sampleType,
                                                       const void* source, const ImageLayout& sourceLayout,
                                                       void* destination, const ImageLayout& destinationLayout,
                                                       const ChromaticNoisePlan& plan,
                                                       const SpatialSelection& selection)
{
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           plan.request().output, selection, kChromaticOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    if (geometry.channelCount != 3u)
    {
        return noiseLimitFailure(ErrorCode::IncompatibleArguments, kChromaticOperation,
                                 "a chromatic-noise reduction needs the three channels it decides between",
                                 geometry.channelCount, 3u);
    }
    const Result<ChromaticNoisePlan> revalidated = planChromaticNoiseReduction(plan.request());
    if (!revalidated.hasValue())
    {
        return revalidated.status();
    }

    const bool readInPlace = spatial_detail::channelIsAddressablePlane(sampleType, sourceLayout);
    const bool writeInPlace = spatial_detail::channelIsAddressablePlane(sampleType, destinationLayout);
    std::array<SpatialPlaneBuffer, 3u> sourceWorking{};
    std::array<SpatialPlaneBuffer, 3u> destinationWorking{};
    std::uint64_t workingBytes = 0u;
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        if (!readInPlace)
        {
            Result<SpatialPlaneBuffer> taken = SpatialPlaneBuffer::own(execution, geometry, kChromaticOperation);
            if (!taken.hasValue())
            {
                return taken.status();
            }
            sourceWorking[channel] = std::move(taken).value();
            workingBytes += sourceWorking[channel].bytes();
        }
        if (!writeInPlace)
        {
            Result<SpatialPlaneBuffer> taken = SpatialPlaneBuffer::own(execution, geometry, kChromaticOperation);
            if (!taken.hasValue())
            {
                return taken.status();
            }
            destinationWorking[channel] = std::move(taken).value();
            workingBytes += destinationWorking[channel].bytes();
        }
    }

    std::array<std::optional<PlaneView<const float>>, 3u> inputs{};
    std::array<std::optional<PlaneView<float>>, 3u> outputs{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        const Status read = readChannelPlane(execution, sampleType, source, sourceLayout, channel, geometry,
                                             sourceWorking[channel], kChromaticOperation, inputs[channel]);
        if (read.failed())
        {
            return read;
        }
        outputs[channel] = writeInPlace ? spatial_detail::borrowDestinationPlane(destination, destinationLayout,
                                                                                 channel, sampleType, geometry)
                                        : destinationWorking[channel].view();
        if (!outputs[channel].has_value())
        {
            return noiseFailure(ErrorCode::InvalidArgument, kChromaticOperation,
                                "a channel of this image is not addressable as a plane");
        }
    }

    const ChromaticNoisePlan& checkedPlan = *revalidated;
    const ChromaticNoiseRequest& request = checkedPlan.request();
    const std::array<KernelValidityView, 3u> validities{spatial_detail::channelValidity(selection, 0u),
                                                        spatial_detail::channelValidity(selection, 1u),
                                                        spatial_detail::channelValidity(selection, 2u)};
    const std::array<reference_detail::ValidityAccessor, 3u> accepts{
        reference_detail::ValidityAccessor{validities[0u]}, reference_detail::ValidityAccessor{validities[1u]},
        reference_detail::ValidityAccessor{validities[2u]}};
    const std::uint32_t width = geometry.width;
    const bool preserve = checkedPlan.preservesLightness();
    // A rule that accepts every sample of every channel is answered once here rather than three
    // times per pixel: the plain suppression is then a few arithmetic operations per pixel, and
    // asking a validity rule per channel would cost more than the operation itself.
    const bool everySampleValid = selection.mask == nullptr && selection.validity.rule() == ValidityRule::AllValid;
    const ChromaticNoiseMethod method = request.method;
    const double amount = request.amount;
    const double whiteLevel = request.whiteLevel;
    // The Float32 row is the Float64 row's bits only when the white level clamps to the same value
    // in both widths, which every conventional level does; an unrepresentable one takes the
    // Float64 loop.
    const auto narrowWhiteLevel = static_cast<float>(whiteLevel);
    const bool exactInFloat32 =
        static_cast<double>(narrowWhiteLevel) == whiteLevel &&
        (method == ChromaticNoiseMethod::AverageNeutral || method == ChromaticNoiseMethod::MaximumNeutral);
    const ChromaticCeilingRunFn ceilingRun = chromaticCeilingRunFor(execution);
    std::atomic<std::uint64_t> changed{0u};
    std::atomic<std::uint64_t> accepted{0u};
    const Status walked =
        parallelFor(execution, geometry.height, rowCost(width, preserve ? 200u : 12u), [&](TaskRange range) {
            std::uint64_t localChanged = 0u;
            std::uint64_t localAccepted = 0u;
            for (std::uint64_t row = range.begin; row < range.end; ++row)
            {
                const auto y = static_cast<std::uint32_t>(row);
                const std::span<const float> red = *inputs[0]->row(y);
                const std::span<const float> green = *inputs[1]->row(y);
                const std::span<const float> blue = *inputs[2]->row(y);
                const std::span<float> outRed = *outputs[0]->row(y);
                const std::span<float> outGreen = *outputs[1]->row(y);
                const std::span<float> outBlue = *outputs[2]->row(y);
                if (everySampleValid && !preserve && exactInFloat32)
                {
                    if (ceilingRun != nullptr)
                    {
                        localChanged += ceilingRun(red.data(), green.data(), blue.data(), outRed.data(),
                                                   outGreen.data(), outBlue.data(), width, narrowWhiteLevel,
                                                   method == ChromaticNoiseMethod::AverageNeutral);
                    }
                    else
                    {
                        localChanged += method == ChromaticNoiseMethod::AverageNeutral
                                            ? suppressRowExact<ChromaticNoiseMethod::AverageNeutral>(
                                                  red.data(), green.data(), blue.data(), outRed.data(), outGreen.data(),
                                                  outBlue.data(), width, narrowWhiteLevel)
                                            : suppressRowExact<ChromaticNoiseMethod::MaximumNeutral>(
                                                  red.data(), green.data(), blue.data(), outRed.data(), outGreen.data(),
                                                  outBlue.data(), width, narrowWhiteLevel);
                    }
                    localAccepted += width;
                    continue;
                }
                if (everySampleValid && !preserve)
                {
                    // The same arithmetic as the general loop below, with nothing asked per pixel:
                    // widen, hold green to its ceiling, clamp to the image's range, narrow once.
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        const auto r = static_cast<double>(red[x]);
                        const auto g = static_cast<double>(green[x]);
                        const auto b = static_cast<double>(blue[x]);
                        const double held = suppressGreen(method, r, g, b, amount, whiteLevel);
                        const auto storedRed = static_cast<float>(std::clamp(r, 0.0, whiteLevel));
                        const auto storedGreen = static_cast<float>(std::clamp(held, 0.0, whiteLevel));
                        const auto storedBlue = static_cast<float>(std::clamp(b, 0.0, whiteLevel));
                        localChanged += storedRed != red[x] ? 1u : 0u;
                        localChanged += storedGreen != green[x] ? 1u : 0u;
                        localChanged += storedBlue != blue[x] ? 1u : 0u;
                        outRed[x] = storedRed;
                        outGreen[x] = storedGreen;
                        outBlue[x] = storedBlue;
                    }
                    localAccepted += width;
                    continue;
                }
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    if (!accepts[0u].accepts(red[x], x, y) || !accepts[1u].accepts(green[x], x, y) ||
                        !accepts[2u].accepts(blue[x], x, y))
                    {
                        continue;
                    }
                    std::array<double, 3u> rgb{static_cast<double>(red[x]), static_cast<double>(green[x]),
                                               static_cast<double>(blue[x])};
                    std::array<double, 3u> before{};
                    if (preserve)
                    {
                        for (double& channel : rgb)
                        {
                            channel /= request.whiteLevel;
                        }
                        const Status converted = convertColorPixel(checkedPlan.toLab(), rgb, before);
                        if (converted.failed())
                        {
                            return converted;
                        }
                    }
                    rgb[1] = suppressGreen(request.method, rgb[0], rgb[1], rgb[2], request.amount,
                                           preserve ? 1.0 : request.whiteLevel);
                    if (preserve)
                    {
                        std::array<double, 3u> after{};
                        Status converted = convertColorPixel(checkedPlan.toLab(), rgb, after);
                        if (converted.failed())
                        {
                            return converted;
                        }
                        after[0] = before[0];
                        converted = convertColorPixel(checkedPlan.fromLab(), after, rgb);
                        if (converted.failed())
                        {
                            return converted;
                        }
                        for (double& channel : rgb)
                        {
                            channel *= request.whiteLevel;
                        }
                    }
                    for (std::size_t channel = 0u; channel < 3u; ++channel)
                    {
                        rgb[channel] = std::clamp(rgb[channel], 0.0, request.whiteLevel);
                    }
                    const auto storedRed = static_cast<float>(rgb[0]);
                    const auto storedGreen = static_cast<float>(rgb[1]);
                    const auto storedBlue = static_cast<float>(rgb[2]);
                    if (writeInPlace)
                    {
                        localChanged += storedRed != red[x] ? 1u : 0u;
                        localChanged += storedGreen != green[x] ? 1u : 0u;
                        localChanged += storedBlue != blue[x] ? 1u : 0u;
                    }
                    outRed[x] = storedRed;
                    outGreen[x] = storedGreen;
                    outBlue[x] = storedBlue;
                    ++localAccepted;
                }
            }
            changed.fetch_add(localChanged, std::memory_order_relaxed);
            accepted.fetch_add(localAccepted, std::memory_order_relaxed);
            return Status::success();
        });
    if (walked.failed())
    {
        return walked;
    }

    if (!writeInPlace)
    {
        std::uint64_t storedChanged = 0u;
        std::uint64_t storedWritten = 0u;
        const auto selected = [&](std::uint32_t x, std::uint32_t y) noexcept {
            return accepts[0u].accepts((*inputs[0u]->row(y))[x], x, y) &&
                   accepts[1u].accepts((*inputs[1u]->row(y))[x], x, y) &&
                   accepts[2u].accepts((*inputs[2u]->row(y))[x], x, y);
        };
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            const Status scattered =
                scatterSelectedChannel(execution, destinationWorking[channel].constView(), sampleType, source,
                                       sourceLayout, destination, destinationLayout, channel, request.output.writeBack,
                                       kChromaticOperation, selected, storedChanged, storedWritten);
            if (scattered.failed())
            {
                return scattered;
            }
        }
        changed.store(storedChanged, std::memory_order_relaxed);
    }

    NoiseReductionResult result{};
    result.filter.channelCount = 3u;
    result.filter.imagePasses = 1u;
    result.filter.workingBytes = workingBytes;
    result.filter.plannedWorkers = execution.admittedConcurrency();
    result.filter.precision = ComputePrecision::Float32;
    result.filter.readInPlace = readInPlace;
    result.filter.wroteInPlace = writeInPlace;
    const std::uint64_t pixels = static_cast<std::uint64_t>(geometry.width) * geometry.height;
    result.filter.run.selection =
        ValidityCount{.valid = accepted.load(std::memory_order_relaxed) * 3u, .total = pixels * 3u};
    result.filter.run.outputsWritten = accepted.load(std::memory_order_relaxed) * 3u;
    result.samplesChanged = changed.load(std::memory_order_relaxed);
    return result;
}

Result<NoiseReductionResult> suppressImageBanding(const ExecutionContext& execution, SampleType sampleType,
                                                  const void* source, const ImageLayout& sourceLayout,
                                                  void* destination, const ImageLayout& destinationLayout,
                                                  const BandingRequest& request, const SpatialSelection& selection)
{
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           request.output, selection, kBandingOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    if (!std::isfinite(request.amount) || request.amount < 0.0)
    {
        return noiseFailure(ErrorCode::InvalidArgument, kBandingOperation,
                            "the amount of a banding suppression is finite and not negative");
    }
    if (request.protectHighlights && (!std::isfinite(request.highlightSigma) || request.highlightSigma <= 0.0))
    {
        return noiseFailure(ErrorCode::InvalidArgument, kBandingOperation,
                            "a highlight rejection point needs a finite positive divisor");
    }

    const std::uint32_t lineCount = request.verticalBanding ? geometry.width : geometry.height;
    const std::uint32_t lineLength = request.verticalBanding ? geometry.height : geometry.width;
    const std::uint32_t concurrency = (std::max)(std::uint32_t{1}, execution.admittedConcurrency());
    const std::uint64_t slots =
        (std::min)(static_cast<std::uint64_t>(concurrency), static_cast<std::uint64_t>(lineCount));
    Result<plane_detail::ScopedContextMemory> scratch = plane_detail::ScopedContextMemory::take(
        execution, slots * lineLength * sizeof(float), kDefaultBufferAlignmentBytes, kBandingOperation);
    if (!scratch.hasValue())
    {
        return scratch.status();
    }
    Result<plane_detail::ScopedContextMemory> corrections = plane_detail::ScopedContextMemory::take(
        execution, static_cast<std::uint64_t>(lineCount) * sizeof(double), alignof(double), kBandingOperation);
    if (!corrections.hasValue())
    {
        return corrections.status();
    }
    auto* const lineBuffers = static_cast<float*>(scratch->data());
    auto* const lineOffsets = static_cast<double*>(corrections->data());
    Result<plane_detail::ScopedContextMemory> measuredLines = plane_detail::ScopedContextMemory::take(
        execution, lineCount * sizeof(std::uint8_t), alignof(std::uint8_t), kBandingOperation);
    if (!measuredLines.hasValue())
    {
        return measuredLines.status();
    }
    auto* const lineMeasured = static_cast<std::uint8_t*>(measuredLines->data());

    const bool readInPlace = spatial_detail::channelIsAddressablePlane(sampleType, sourceLayout);
    const bool writeInPlace = spatial_detail::channelIsAddressablePlane(sampleType, destinationLayout);
    SpatialPlaneBuffer sourceWorking;
    SpatialPlaneBuffer destinationWorking;
    if (!readInPlace)
    {
        Result<SpatialPlaneBuffer> taken = SpatialPlaneBuffer::own(execution, geometry, kBandingOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        sourceWorking = std::move(taken).value();
    }
    if (!writeInPlace)
    {
        Result<SpatialPlaneBuffer> taken = SpatialPlaneBuffer::own(execution, geometry, kBandingOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        destinationWorking = std::move(taken).value();
    }

    SpatialFilterResult filter{};
    filter.channelCount = geometry.channelCount;
    filter.imagePasses = 2u;
    filter.precision = ComputePrecision::Float32;
    filter.plannedWorkers = execution.admittedConcurrency();
    filter.readInPlace = readInPlace;
    filter.wroteInPlace = writeInPlace;
    filter.workingBytes = scratch->bytes() + corrections->bytes() + measuredLines->bytes() + sourceWorking.bytes() +
                          destinationWorking.bytes();
    std::uint64_t changed = 0u;
    std::uint64_t written = 0u;

    for (std::uint32_t channelIndex = 0u; channelIndex < geometry.channelCount; ++channelIndex)
    {
        std::optional<PlaneView<const float>> input;
        const Status read = readChannelPlane(execution, sampleType, source, sourceLayout, channelIndex, geometry,
                                             sourceWorking, kBandingOperation, input);
        if (read.failed())
        {
            return read;
        }
        const std::optional<PlaneView<float>> borrowed =
            writeInPlace ? spatial_detail::borrowDestinationPlane(destination, destinationLayout, channelIndex,
                                                                  sampleType, geometry)
                         : std::optional<PlaneView<float>>{destinationWorking.view()};
        if (!borrowed.has_value())
        {
            return noiseFailure(ErrorCode::InvalidArgument, kBandingOperation,
                                "a channel of this image is not addressable as a plane");
        }
        const PlaneView<float> output = *borrowed;
        const KernelValidityView validity = spatial_detail::channelValidity(selection, channelIndex);
        const reference_detail::ValidityAccessor accepts(validity);
        // The channel's own background and robust scale, from the engine's exact rank rather than
        // from a second estimator written here.
        const std::optional<ImageLayout> planeLayout = ImageLayout::createWithStrides(
            {.extent = *ImageExtent::create(input->width(), input->height(), 1u, execution.limits()),
             .elementSizeBytes = static_cast<std::uint32_t>(sizeof(float)),
             .samplesPerPixel = 1u,
             .planeCount = 1u,
             .rowAlignmentBytes = 1u},
            input->rowStrideBytes(), input->rowStrideBytes() * input->height(), execution.limits());
        if (!planeLayout.has_value())
        {
            return noiseFailure(ErrorCode::Internal, kBandingOperation,
                                "the working plane does not describe an image the rank can read");
        }
        const KernelPlaneSource channel{.sampleType = SampleType::Float32,
                                        .samples = input->data(),
                                        .layout = &*planeLayout,
                                        .plane = 0u,
                                        .sampleInPixel = 0u};
        const std::array<double, 1u> quantiles{0.5};
        std::array<double, 1u> values{};
        const Result<PlaneOrderStatistics> statistics = selectPlaneOrderStatistics(
            execution, channel, quantiles, values,
            {.interpolation = QuantileInterpolation::Linear, .medianAbsoluteDeviation = request.protectHighlights},
            validity);
        if (!statistics.hasValue())
        {
            return statistics.status();
        }
        const double background = statistics->median;
        const double reject = request.protectHighlights
                                  ? background + statistics->medianAbsoluteDeviation * kMedianAbsoluteDeviationToSigma /
                                                     request.highlightSigma
                                  : 0.0;

        for (std::uint64_t wave = 0u; wave < lineCount; wave += slots)
        {
            const std::uint64_t waveLines = (std::min)(slots, static_cast<std::uint64_t>(lineCount) - wave);
            const Status measured = parallelFor(execution, waveLines, rowCost(lineLength, 8u), [&](TaskRange range) {
                for (std::uint64_t slot = range.begin; slot < range.end; ++slot)
                {
                    const auto line = static_cast<std::uint32_t>(wave + slot);
                    float* const buffer = lineBuffers + slot * lineLength;
                    std::size_t kept = 0u;
                    for (std::uint32_t index = 0u; index < lineLength; ++index)
                    {
                        const std::uint32_t x = request.verticalBanding ? line : index;
                        const std::uint32_t y = request.verticalBanding ? index : line;
                        const float sample = (*input->row(y))[x];
                        if (!accepts.accepts(sample, x, y))
                        {
                            continue;
                        }
                        if (request.protectHighlights && static_cast<double>(sample) >= reject)
                        {
                            continue;
                        }
                        buffer[kept] = sample;
                        ++kept;
                    }
                    lineMeasured[line] = kept != 0u ? 1u : 0u;
                    lineOffsets[line] = kept != 0u ? background - medianOf(buffer, kept) : 0.0;
                }
                return Status::success();
            });
            if (measured.failed())
            {
                return measured;
            }
        }

        double smallest = 0.0;
        bool foundLine = false;
        for (std::uint32_t line = 0u; line < lineCount; ++line)
        {
            if (lineMeasured[line] == 0u)
            {
                continue;
            }
            smallest = foundLine ? (std::min)(smallest, lineOffsets[line]) : lineOffsets[line];
            foundLine = true;
        }

        std::atomic<std::uint64_t> moved{0u};
        std::atomic<std::uint64_t> produced{0u};
        const Status applied =
            parallelFor(execution, input->height(), rowCost(input->width(), 4u), [&](TaskRange range) {
                std::uint64_t localMoved = 0u;
                std::uint64_t localProduced = 0u;
                for (std::uint64_t row = range.begin; row < range.end; ++row)
                {
                    const auto y = static_cast<std::uint32_t>(row);
                    const std::span<const float> from = *input->row(y);
                    const std::span<float> to = *output.row(y);
                    for (std::uint32_t x = 0u; x < input->width(); ++x)
                    {
                        const std::uint32_t line = request.verticalBanding ? x : y;
                        if (lineMeasured[line] == 0u || !accepts.accepts(from[x], x, y))
                        {
                            continue;
                        }
                        const double correction = request.amount * (lineOffsets[line] - smallest);
                        const auto stored = static_cast<float>(static_cast<double>(from[x]) + correction);
                        localMoved += writeInPlace && stored != from[x] ? 1u : 0u;
                        to[x] = stored;
                        ++localProduced;
                    }
                }
                moved.fetch_add(localMoved, std::memory_order_relaxed);
                produced.fetch_add(localProduced, std::memory_order_relaxed);
                return Status::success();
            });
        if (applied.failed())
        {
            return applied;
        }

        if (writeInPlace)
        {
            changed += moved.load(std::memory_order_relaxed);
            written += produced.load(std::memory_order_relaxed);
        }
        else
        {
            const auto selected = [&](std::uint32_t x, std::uint32_t y) noexcept {
                const std::uint32_t line = request.verticalBanding ? x : y;
                return lineMeasured[line] != 0u && accepts.accepts((*input->row(y))[x], x, y);
            };
            const Status scattered =
                scatterSelectedChannel(execution, destinationWorking.constView(), sampleType, source, sourceLayout,
                                       destination, destinationLayout, channelIndex, request.output.writeBack,
                                       kBandingOperation, selected, changed, written);
            if (scattered.failed())
            {
                return scattered;
            }
        }
    }

    const std::uint64_t samples = static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount;
    filter.run.selection = ValidityCount{.valid = written, .total = samples};
    filter.run.outputsWritten = written;
    NoiseReductionResult result{};
    result.filter = filter;
    result.samplesChanged = changed;
    return result;
}

Result<NoiseReductionResult> rejectImageImpulseNoise(const ExecutionContext& execution, SampleType sampleType,
                                                     const void* source, const ImageLayout& sourceLayout,
                                                     void* destination, const ImageLayout& destinationLayout,
                                                     const ImpulseNoiseRequest& request,
                                                     const SpatialSelection& selection)
{
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           request.output, selection, kImpulseOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    if (request.radius == 0u)
    {
        return noiseFailure(ErrorCode::InvalidArgument, kImpulseOperation,
                            "an impulse rejection needs a neighbourhood of at least one sample either side");
    }
    if (!std::isfinite(request.highThreshold) || request.highThreshold < 0.0 || request.highThreshold >= 1.0 ||
        !std::isfinite(request.lowThreshold) || request.lowThreshold < 0.0 || request.lowThreshold >= 1.0)
    {
        return noiseFailure(ErrorCode::InvalidArgument, kImpulseOperation,
                            "a relative rejection threshold lies within the half-open unit interval");
    }
    // The truncated window is a border rule for an order statistic even though it supplies no
    // neighbour, so this is the one entry point in the file that accepts it.
    if (!isKernelBorderMode(request.borders.border))
    {
        return noiseFailure(ErrorCode::InvalidArgument, kImpulseOperation,
                            "the border is not one this engine implements");
    }
    if (request.borders.border != KernelBorderMode::NotApplicable)
    {
        const Status borders = checkBorders(request.borders, kImpulseOperation);
        if (borders.failed())
        {
            return borders;
        }
    }

    // The neighbourhood median comes from the order filter rather than from a rank written here, so
    // there is one window ceiling and one edge rule in the engine.
    const std::optional<ImageLayout> medianLayout =
        ImageLayout::create({.extent = sourceLayout.extent(),
                             .elementSizeBytes = sampleTypeSizeBytes(sampleType),
                             .samplesPerPixel = 1u,
                             .planeCount = geometry.channelCount,
                             .rowAlignmentBytes = kDefaultBufferAlignmentBytes},
                            execution.limits());
    if (!medianLayout.has_value())
    {
        return noiseFailure(ErrorCode::LimitExceeded, kImpulseOperation,
                            "the neighbourhood median of this picture is not representable as an image");
    }
    Result<plane_detail::ScopedContextMemory> medianMemory = plane_detail::ScopedContextMemory::take(
        execution, medianLayout->totalBytes(), kDefaultBufferAlignmentBytes, kImpulseOperation);
    if (!medianMemory.hasValue())
    {
        return medianMemory.status();
    }
    const Result<OrderFilterResult> ranked =
        filterImageOrderStatistic(execution, sampleType, source, sourceLayout, medianMemory->data(), *medianLayout,
                                  {.radiusX = request.radius,
                                   .radiusY = request.radius,
                                   .quantile = 0.5,
                                   .border = request.borders.border,
                                   .constantBorder = request.borders.constantBorder,
                                   .output = request.output},
                                  selection);
    if (!ranked.hasValue())
    {
        return ranked.status();
    }

    const bool readInPlace = spatial_detail::channelIsAddressablePlane(sampleType, sourceLayout);
    const bool writeInPlace = spatial_detail::channelIsAddressablePlane(sampleType, destinationLayout);
    const bool medianInPlace = sampleType == SampleType::Float32;
    SpatialPlaneBuffer sourceWorking;
    SpatialPlaneBuffer medianWorking;
    SpatialPlaneBuffer destinationWorking;
    std::uint64_t workingBytes = medianMemory->bytes() + ranked->filter.workingBytes;
    for (const std::pair<bool, SpatialPlaneBuffer*> wanted :
         {std::pair{readInPlace, &sourceWorking}, std::pair{medianInPlace, &medianWorking},
          std::pair{writeInPlace, &destinationWorking}})
    {
        if (wanted.first)
        {
            continue;
        }
        Result<SpatialPlaneBuffer> taken = SpatialPlaneBuffer::own(execution, geometry, kImpulseOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        *wanted.second = std::move(taken).value();
        workingBytes += wanted.second->bytes();
    }

    const std::uint32_t width = geometry.width;
    std::uint64_t replaced = 0u;
    std::uint64_t written = 0u;
    for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
    {
        std::optional<PlaneView<const float>> input;
        Status read = readChannelPlane(execution, sampleType, source, sourceLayout, channel, geometry, sourceWorking,
                                       kImpulseOperation, input);
        if (read.failed())
        {
            return read;
        }
        std::optional<PlaneView<const float>> ranks;
        // The median image holds the source's representation, because the order filter writes what
        // it was given; reading it as anything else is reading somebody else's bytes.
        read = readChannelPlane(execution, sampleType, medianMemory->data(), *medianLayout, channel, geometry,
                                medianWorking, kImpulseOperation, ranks);
        if (read.failed())
        {
            return read;
        }
        const std::optional<PlaneView<float>> borrowed =
            writeInPlace
                ? spatial_detail::borrowDestinationPlane(destination, destinationLayout, channel, sampleType, geometry)
                : std::optional<PlaneView<float>>{destinationWorking.view()};
        if (!borrowed.has_value())
        {
            return noiseFailure(ErrorCode::InvalidArgument, kImpulseOperation,
                                "a channel of this image is not addressable as a plane");
        }
        const PlaneView<float> output = *borrowed;
        const KernelValidityView validity = spatial_detail::channelValidity(selection, channel);
        const reference_detail::ValidityAccessor accepts(validity);
        std::atomic<std::uint64_t> moved{0u};
        std::atomic<std::uint64_t> produced{0u};
        const Status walked = parallelFor(execution, geometry.height, rowCost(width, 8u), [&](TaskRange range) {
            std::uint64_t localMoved = 0u;
            std::uint64_t localProduced = 0u;
            for (std::uint64_t row = range.begin; row < range.end; ++row)
            {
                const auto y = static_cast<std::uint32_t>(row);
                const std::span<const float> from = *input->row(y);
                const std::span<const float> median = *ranks->row(y);
                const std::span<float> to = *output.row(y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const float sample = from[x];
                    if (!accepts.accepts(sample, x, y))
                    {
                        continue;
                    }
                    const double value = static_cast<double>(sample);
                    const double rank = static_cast<double>(median[x]);
                    float answer = sample;
                    // A relative deviation needs something to be relative to: a magnitude of zero
                    // names no deviation, rather than an infinity from dividing by it.
                    if (request.rejectHigh && value > rank && value != 0.0 &&
                        (value - rank) / std::abs(value) > request.highThreshold)
                    {
                        answer = median[x];
                    }
                    else if (request.rejectLow && value < rank && rank != 0.0 &&
                             (rank - value) / std::abs(rank) > request.lowThreshold)
                    {
                        answer = median[x];
                    }
                    if (writeInPlace && answer != sample)
                    {
                        ++localMoved;
                    }
                    to[x] = answer;
                    ++localProduced;
                }
            }
            moved.fetch_add(localMoved, std::memory_order_relaxed);
            produced.fetch_add(localProduced, std::memory_order_relaxed);
            return Status::success();
        });
        if (walked.failed())
        {
            return walked;
        }
        if (writeInPlace)
        {
            replaced += moved.load(std::memory_order_relaxed);
        }
        written += produced.load(std::memory_order_relaxed);

        if (!writeInPlace)
        {
            const Status scattered = spatial_detail::scatterChannelPlane(
                execution, destinationWorking.constView(), *input, validity, sampleType, destination, destinationLayout,
                channel, request.output.writeBack, kImpulseOperation, source, &sourceLayout, &replaced);
            if (scattered.failed())
            {
                return scattered;
            }
        }
    }

    NoiseReductionResult result{};
    result.filter.channelCount = geometry.channelCount;
    result.filter.horizontalRadius = request.radius;
    result.filter.verticalRadius = request.radius;
    // The rank filter and the impulse comparison are two full-image algorithm passes.
    // Channel count and the gather/scatter needed by a layout do not multiply that contract.
    result.filter.imagePasses = 2u;
    result.filter.workingBytes = workingBytes;
    result.filter.plannedWorkers = execution.admittedConcurrency();
    result.filter.precision = ComputePrecision::Float32;
    result.filter.readInPlace = readInPlace;
    result.filter.wroteInPlace = writeInPlace;
    const std::uint64_t samples = static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount;
    result.filter.run.selection = ValidityCount{.valid = written, .total = samples};
    result.filter.run.outputsWritten = written;
    result.samplesChanged = replaced;
    return result;
}

} // namespace an
