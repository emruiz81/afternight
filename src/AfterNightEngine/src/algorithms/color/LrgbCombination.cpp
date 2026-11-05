// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a colour image's chrominance combined with a luminance image's lightness.
//
// One walk, a chunk of a row and one stage at a time: the colour converted to CIE L*a*b* and back through the colour
// conversion's own stage arithmetic (`kernels/reference/ColorStages.h`), the luminance and the chroma stretched by the
// scalar midtones transfer, a* and b* moved along their own hue to the new chroma, the lightness blended and the
// result held and weighed - each pixel computed in double and written once in Float32, Float64 when the result is
// Float64. The walk runs over working planes the shared reader fills, written in place and narrowed by the shared
// writer; when both images and the result are Float32 and every sample is selected it runs straight from the images
// into the destination, because the working planes would hold exactly what the images hold.

#include <AfterNightEngine/Algorithms/Color/LrgbCombination.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/ColorStages.h"
#include "kernels/reference/CompositionPlanes.h"
#include "runtime/TaskPartition.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace an
{
namespace
{

using color_stage_detail::ChunkPlanes;
using color_stage_detail::convertResolved;
using color_stage_detail::forEachChunkPixel;
using color_stage_detail::kColorChunk;
using color_stage_detail::ResolvedColorConversion;
using color_stage_detail::ResolvedPath;
using composition_detail::CompositionSource;
using composition_detail::WorkingBlock;
using composition_detail::WorkingPlanes;

constexpr const char* kPlanOperation = "an::planLrgbCombination";
constexpr const char* kLayoutOperation = "an::lrgbCombinationLayout";
constexpr const char* kCombineOperation = "an::combineLrgb";

/// The colour planes and the luminance plane of the working image.
constexpr std::uint32_t kColorPlanes = 3u;

/// How many row ranges each worker is offered. A row is long and alike - several transcendental calls a pixel - so
/// the scheduler's usual four a worker leaves the last ranges of a busy host to a few workers; the colour
/// conversion's tight loop found the same and offers sixteen.
constexpr std::uint32_t kLongRowChunksPerWorker = 16u;

[[nodiscard]] Status lrgbFailure(ErrorCode code, const char* message, const char* operation, const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

[[nodiscard]] constexpr bool normalizedDomain(NumericDomain domain) noexcept
{
    return domain == NumericDomain::NormalizedLinear || domain == NumericDomain::EncodedNonlinear;
}

[[nodiscard]] bool unitParameter(double value) noexcept
{
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

[[nodiscard]] CompositionSource compositionOf(const LrgbCombinationSource& source) noexcept
{
    return CompositionSource{.descriptor = source.descriptor,
                             .samples = source.samples,
                             .layout = source.layout,
                             .validity = source.validity};
}

[[nodiscard]] Status checkRequest(const LrgbCombinationRequest& request, const char* operation)
{
    if (!unitParameter(request.lightnessWeight) || !unitParameter(request.lightnessMidtones) ||
        !unitParameter(request.chromaMidtones))
    {
        return lrgbFailure(ErrorCode::InvalidArgument,
                           "the lightness weight and the midtones balances must be finite and within the unit interval",
                           operation, "request");
    }
    for (const double weight : request.channelWeights)
    {
        if (!std::isfinite(weight) || weight < 0.0)
        {
            return lrgbFailure(ErrorCode::InvalidArgument, "a channel weight must be finite and not negative",
                               operation, "request");
        }
    }
    if (request.colorEncoding.schema != ChannelSchema::Rgb ||
        checkColorEncoding(request.colorEncoding) != ColorEncodingError::None)
    {
        return lrgbFailure(ErrorCode::InvalidArgument, "the colour image's encoding is invalid or not RGB", operation,
                           "request");
    }
    if (!isSampleType(request.outputSampleType))
    {
        return lrgbFailure(ErrorCode::InvalidArgument, "the result's representation is outside its enumeration",
                           operation, "request");
    }
    return Status::success();
}

/// The CIE L*a*b* encoding, under the colour image's white, the colour image is decomposed into.
[[nodiscard]] ColorEncoding cieLab(const ColorEncoding& rgb) noexcept
{
    ColorEncoding lab;
    lab.schema = ChannelSchema::Lab;
    lab.whitePoint = rgb.whitePoint;
    return lab;
}

/// The midtones transfer between zero and one, by the published scalar evaluator's arithmetic: zero at or below
/// zero, one at or above one, and between them the rise over the rise and the balance times the distance to one.
[[nodiscard]] double screenTransfer(double value, double midtones) noexcept
{
    if (value <= 0.0)
    {
        return 0.0;
    }
    if (value >= 1.0)
    {
        return 1.0;
    }
    const double rise = (1.0 - midtones) * value;
    return rise / (midtones * (1.0 - value) + rise);
}

/// Where a walk reads the colour image and the luminance image and writes the result: the sources themselves, or the
/// working planes, written in place.
template <typename WorkingT> struct LrgbPlanes
{
    const void* color = nullptr;
    const ImageLayout* colorLayout = nullptr;
    const void* luminance = nullptr;
    const ImageLayout* luminanceLayout = nullptr;
    void* result = nullptr;
    const ImageLayout* resultLayout = nullptr;
};

/// One LRGB combination walked a chunk of a row and one stage at a time.
///
/// A valid pixel's colour becomes CIE L*a*b* through the colour conversion's own stages; its lightness over a hundred
/// is replaced by the luminance or blended with it, the luminance stretched first; its a* and b* are moved along
/// their own hue to the chroma's stretched fraction of two hundred; and the colour is recomposed, held to the unit
/// interval, weighed and held again - every value in double, written once in the working representation. A pixel
/// `valid` rejects is left as it is, which in the working planes is the colour image's samples. A source holding a
/// sample that is not finite is refused when `checkFinite` asks.
template <typename WorkingT, bool FixedSequences>
[[nodiscard]] Status combineWalk(const ExecutionContext& execution, const LrgbCombinationRequest& request,
                                 const ResolvedColorConversion& forward, const ResolvedColorConversion& reverse,
                                 const LrgbPlanes<WorkingT>& planes, const std::uint8_t* valid, bool checkFinite)
{
    using composition_detail::channelRowOf;
    constexpr ResolvedPath kForward = FixedSequences ? ResolvedPath::ToLab : ResolvedPath::General;
    const std::uint32_t width = planes.resultLayout->extent().width();
    const std::uint32_t height = planes.resultLayout->extent().height();
    const std::size_t colorStep = planes.colorLayout->samplesPerPixel();
    const std::size_t luminanceStep = planes.luminanceLayout->samplesPerPixel();
    const std::size_t resultStep = planes.resultLayout->samplesPerPixel();
    const double weight = request.lightnessWeight;
    const bool stretchLightness = request.lightnessMidtones != 0.5;
    const bool stretchChroma = request.chromaMidtones != 0.5;
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * 7u * sizeof(WorkingT),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * 200u};
    return parallelForInChunks(execution, height, cost, kLongRowChunksPerWorker, [&](TaskRange range) -> Status {
        std::array<const WorkingT*, kColorPlanes> colorRows{};
        std::array<WorkingT*, kColorPlanes> resultRows{};
        ChunkPlanes<3u> chunk{};
        std::array<double, kColorChunk> luminanceChunk{};
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            for (std::uint32_t channel = 0u; channel < kColorPlanes; ++channel)
            {
                colorRows[channel] = channelRowOf<WorkingT>(planes.color, *planes.colorLayout, channel, y);
                resultRows[channel] = channelRowOf<WorkingT>(planes.result, *planes.resultLayout, channel, y);
            }
            const WorkingT* const luminanceRow =
                channelRowOf<WorkingT>(planes.luminance, *planes.luminanceLayout, 0u, y);
            const std::uint8_t* const rowValid =
                valid != nullptr ? valid + static_cast<std::uint64_t>(y) * width : nullptr;
            if (checkFinite)
            {
                std::uint32_t nonFinite = 0u;
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    nonFinite |= composition_detail::nonFiniteBit(colorRows[0][x * colorStep]) |
                                 composition_detail::nonFiniteBit(colorRows[1][x * colorStep]) |
                                 composition_detail::nonFiniteBit(colorRows[2][x * colorStep]) |
                                 composition_detail::nonFiniteBit(luminanceRow[x * luminanceStep]);
                }
                if (nonFinite != 0u)
                {
                    return lrgbFailure(ErrorCode::InvalidData,
                                       "a source holds a sample that is not finite in a pixel its validity admits",
                                       kCombineOperation, "source");
                }
            }
            for (std::uint32_t begin = 0u; begin < width; begin += kColorChunk)
            {
                const std::uint32_t count = (std::min)(kColorChunk, width - begin);
                for (std::uint32_t channel = 0u; channel < kColorPlanes; ++channel)
                {
                    for (std::uint32_t index = 0u; index < count; ++index)
                    {
                        chunk[channel][index] = static_cast<double>(
                            colorRows[channel][(static_cast<std::size_t>(begin) + index) * colorStep]);
                    }
                }
                for (std::uint32_t index = 0u; index < count; ++index)
                {
                    luminanceChunk[index] =
                        static_cast<double>(luminanceRow[(static_cast<std::size_t>(begin) + index) * luminanceStep]);
                }

                forEachChunkPixel(chunk, count,
                                  [&](std::array<double, 3u>& pixel) { convertResolved<kForward>(forward, pixel); });
                for (std::uint32_t index = 0u; index < count; ++index)
                {
                    if (stretchChroma)
                    {
                        const double chroma = std::hypot(chunk[1][index], chunk[2][index]);
                        const double stretched = 200.0 * screenTransfer(chroma / 200.0, request.chromaMidtones);
                        const double scale = chroma > 0.0 ? stretched / chroma : 0.0;
                        chunk[1][index] *= scale;
                        chunk[2][index] *= scale;
                    }
                    double supplied = luminanceChunk[index];
                    if (stretchLightness)
                    {
                        supplied = screenTransfer(supplied, request.lightnessMidtones);
                    }
                    const double lightness =
                        weight == 1.0 ? supplied : (chunk[0][index] / 100.0) * (1.0 - weight) + supplied * weight;
                    chunk[0][index] = 100.0 * lightness;
                }

                // Back to RGB, then held, weighed and held again. The fixed sequence stops at linear RGB and leaves
                // the curve to the hold, which needs no curve for a channel out of gamut.
                forEachChunkPixel(chunk, count, [&](std::array<double, 3u>& pixel) {
                    if constexpr (FixedSequences)
                    {
                        color_stage_detail::labToXyz(reverse, pixel);
                        pixel = color_stage_detail::applyMatrix(reverse.fromXyz, pixel);
                    }
                    else
                    {
                        convertResolved<ResolvedPath::General>(reverse, pixel);
                    }
                });
                for (std::uint32_t channel = 0u; channel < kColorPlanes; ++channel)
                {
                    const double channelWeight = request.channelWeights[channel];
                    for (std::uint32_t index = 0u; index < count; ++index)
                    {
                        const std::size_t x = static_cast<std::size_t>(begin) + index;
                        if (rowValid != nullptr && rowValid[x] == 0u)
                        {
                            continue;
                        }
                        double value = 0.0;
                        if constexpr (FixedSequences)
                        {
                            const double linear = chunk[channel][index];
                            const std::optional<double> held =
                                color_stage_detail::clampedEncodingWithoutCurve(reverse.encode, linear);
                            value = held.has_value() ? *held
                                                     : std::clamp(color_stage_detail::encodeTransfer(
                                                                      reverse.encode, reverse.encodeGamma, linear),
                                                                  0.0, 1.0);
                        }
                        else
                        {
                            value = std::clamp(chunk[channel][index], 0.0, 1.0);
                        }
                        if (channelWeight != 1.0)
                        {
                            value = std::clamp(value * channelWeight, 0.0, 1.0);
                        }
                        resultRows[channel][x * resultStep] = static_cast<WorkingT>(value);
                    }
                }
            }
        }
        return Status::success();
    });
}

/// Chooses the walk for the resolved conversions' fixed sequences.
template <typename WorkingT>
[[nodiscard]] Status combineChunked(const ExecutionContext& execution, const LrgbCombinationRequest& request,
                                    const ResolvedColorConversion& forward, const ResolvedColorConversion& reverse,
                                    const LrgbPlanes<WorkingT>& planes, const std::uint8_t* valid, bool checkFinite)
{
    if (forward.path == ResolvedPath::ToLab && reverse.path == ResolvedPath::FromLab)
    {
        return combineWalk<WorkingT, true>(execution, request, forward, reverse, planes, valid, checkFinite);
    }
    return combineWalk<WorkingT, false>(execution, request, forward, reverse, planes, valid, checkFinite);
}

} // namespace

Result<LrgbCombinationPlan> planLrgbCombination(const LrgbCombinationRequest& request)
{
    const Status checked = checkRequest(request, kPlanOperation);
    if (checked.failed())
    {
        return checked;
    }
    return LrgbCombinationPlan(request);
}

Result<ImageLayoutRequest> lrgbCombinationLayout(const LrgbCombinationPlan& plan, const ImageDescriptor& color)
{
    if (color.extent().channelCount() != kColorPlanes)
    {
        return lrgbFailure(ErrorCode::IncompatibleArguments, "an LRGB combination reads a three-channel colour image",
                           kLayoutOperation, "color");
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(plan.request().outputSampleType);
    return ImageLayoutRequest{.extent = color.extent(),
                              .elementSizeBytes = elementBytes,
                              .samplesPerPixel = 1u,
                              .planeCount = kColorPlanes,
                              .rowAlignmentBytes = elementBytes};
}

Result<LrgbCombinationResult> combineLrgb(const ExecutionContext& execution, const LrgbCombinationPlan& plan,
                                          const LrgbCombinationSource& color, const LrgbCombinationSource& luminance,
                                          void* destination, const ImageLayout& destinationLayout)
{
    if (!execution.valid())
    {
        return lrgbFailure(ErrorCode::NotInitialized, "an LRGB combination was given an empty execution context",
                           kCombineOperation, "execution");
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    const Result<BackendSelection> backend = selectBackend(execution);
    if (!backend.hasValue())
    {
        return backend.status();
    }
    const LrgbCombinationRequest& request = plan.request();
    const CompositionSource colorSource = compositionOf(color);
    const CompositionSource luminanceSource = compositionOf(luminance);
    for (const auto& [source, parameter] : {std::pair{&colorSource, "color"}, std::pair{&luminanceSource, "luminance"}})
    {
        const Status checked = composition_detail::checkCompositionSource(*source, kCombineOperation, parameter);
        if (checked.failed())
        {
            return checked;
        }
        if (!normalizedDomain(source->descriptor->numericDomain()))
        {
            return lrgbFailure(ErrorCode::IncompatibleArguments,
                               "an LRGB combination reads fractions of full scale, so its sources' domain is a "
                               "normalized one",
                               kCombineOperation, parameter);
        }
    }
    const ImageExtent& extent = color.descriptor->extent();
    if (extent.channelCount() != kColorPlanes)
    {
        return lrgbFailure(ErrorCode::IncompatibleArguments, "an LRGB combination reads a three-channel colour image",
                           kCombineOperation, "color");
    }
    const ImageExtent& luminanceExtent = luminance.descriptor->extent();
    if (luminanceExtent.channelCount() != 1u || luminanceExtent.width() != extent.width() ||
        luminanceExtent.height() != extent.height())
    {
        return lrgbFailure(ErrorCode::IncompatibleArguments,
                           "the luminance image is not one channel of the colour image's width and height",
                           kCombineOperation, "luminance");
    }
    const Status destinationChecked = composition_detail::checkCompositionDestination(
        request.outputSampleType, destination, destinationLayout, extent, kCombineOperation);
    if (destinationChecked.failed())
    {
        return destinationChecked;
    }
    for (const auto& [source, parameter] : {std::pair{&colorSource, "color"}, std::pair{&luminanceSource, "luminance"}})
    {
        const Status separate = composition_detail::checkCompositionSeparate(
            *source, destination, destinationLayout.totalBytes(), kCombineOperation, parameter);
        if (separate.failed())
        {
            return separate;
        }
    }
    const Result<ColorConversionPlan> forward =
        planColorConversion(request.colorEncoding, cieLab(request.colorEncoding));
    if (!forward.hasValue())
    {
        return forward.status();
    }
    const Result<ColorConversionPlan> reverse =
        planColorConversion(cieLab(request.colorEncoding), request.colorEncoding);
    if (!reverse.hasValue())
    {
        return reverse.status();
    }

    const std::optional<ResolvedColorConversion> resolvedForward = color_stage_detail::resolveColorConversion(*forward);
    const std::optional<ResolvedColorConversion> resolvedReverse = color_stage_detail::resolveColorConversion(*reverse);
    if (!resolvedForward.has_value() || !resolvedReverse.has_value())
    {
        return lrgbFailure(ErrorCode::Internal, "the colour plans hold a stage the combination does not restate",
                           kCombineOperation, "request");
    }
    const std::uint32_t width = extent.width();
    const std::uint32_t height = extent.height();
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    if (request.outputSampleType == SampleType::Float32 && color.descriptor->sampleType() == SampleType::Float32 &&
        luminance.descriptor->sampleType() == SampleType::Float32 &&
        !composition_detail::compositionSourceRuled(colorSource) &&
        !composition_detail::compositionSourceRuled(luminanceSource))
    {
        const LrgbPlanes<float> planes{.color = color.samples,
                                       .colorLayout = color.layout,
                                       .luminance = luminance.samples,
                                       .luminanceLayout = luminance.layout,
                                       .result = destination,
                                       .resultLayout = &destinationLayout};
        const Status walked =
            combineChunked<float>(execution, request, *resolvedForward, *resolvedReverse, planes, nullptr, true);
        if (walked.failed())
        {
            return walked;
        }
        LrgbCombinationResult result;
        result.run.selection.total = pixels;
        result.run.selection.valid = pixels;
        result.run.outputsWritten = pixels * kColorPlanes;
        return result;
    }

    const SampleType workingType =
        request.outputSampleType == SampleType::Float64 ? SampleType::Float64 : SampleType::Float32;
    WorkingBlock colorBlock;
    WorkingBlock luminanceBlock;
    WorkingBlock validBlock;
    const Result<WorkingPlanes> colorPlanes = composition_detail::takeWorkingPlanes(
        execution, colorBlock, width, height, kColorPlanes, workingType, kCombineOperation);
    if (!colorPlanes.hasValue())
    {
        return colorPlanes.status();
    }
    const Result<WorkingPlanes> luminancePlane = composition_detail::takeWorkingPlanes(
        execution, luminanceBlock, width, height, 1u, workingType, kCombineOperation);
    if (!luminancePlane.hasValue())
    {
        return luminancePlane.status();
    }
    std::uint8_t* valid = nullptr;
    if (composition_detail::compositionSourceRuled(colorSource) ||
        composition_detail::compositionSourceRuled(luminanceSource))
    {
        const Status taken = validBlock.take(execution, pixels, kCombineOperation);
        if (taken.failed())
        {
            return taken;
        }
        valid = validBlock.bytes();
        std::memset(valid, 1, static_cast<std::size_t>(pixels));
    }
    Status step = composition_detail::readWorking(execution, colorSource, *colorPlanes, 0u, valid, kCombineOperation);
    if (step.ok())
    {
        step =
            composition_detail::readWorking(execution, luminanceSource, *luminancePlane, 0u, valid, kCombineOperation);
    }
    if (step.ok())
    {
        if (workingType == SampleType::Float64)
        {
            const LrgbPlanes<double> planes{.color = colorPlanes->samples,
                                            .colorLayout = &*colorPlanes->layout,
                                            .luminance = luminancePlane->samples,
                                            .luminanceLayout = &*luminancePlane->layout,
                                            .result = colorPlanes->samples,
                                            .resultLayout = &*colorPlanes->layout};
            step = combineChunked<double>(execution, request, *resolvedForward, *resolvedReverse, planes, valid, false);
        }
        else
        {
            const LrgbPlanes<float> planes{.color = colorPlanes->samples,
                                           .colorLayout = &*colorPlanes->layout,
                                           .luminance = luminancePlane->samples,
                                           .luminanceLayout = &*luminancePlane->layout,
                                           .result = colorPlanes->samples,
                                           .resultLayout = &*colorPlanes->layout};
            step = combineChunked<float>(execution, request, *resolvedForward, *resolvedReverse, planes, valid, false);
        }
    }
    if (step.ok())
    {
        step = composition_detail::writeWorking(execution, *colorPlanes, request.outputSampleType, destination,
                                                destinationLayout, kCombineOperation);
    }
    if (step.failed())
    {
        return step;
    }
    LrgbCombinationResult result;
    result.run.selection.total = pixels;
    result.run.selection.valid = pixels;
    if (valid != nullptr)
    {
        std::uint64_t accepted = 0u;
        for (std::uint64_t index = 0u; index < pixels; ++index)
        {
            accepted += valid[index];
        }
        result.run.selection.valid = accepted;
    }
    result.run.outputsWritten = pixels * kColorPlanes;
    return result;
}

} // namespace an
