// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a starless image and its stars put back together.
//
// A composition over working planes of the result's width: the starless image, the stars and the mask are read
// into Float32 planes - Float64 when the result is Float64 - with every pixel any of them rejects flagged; the
// stars' edges are tightened by the published spatial filter; and one walk forms each sample's sum, its blend over
// the starless image and its pass-through where a pixel is invalid, in double, before the shared writer narrows the
// result once.
//
// When every image and the result are Float32 and every sample is selected, the working planes would hold exactly
// what the images hold, and one walk runs straight from the images into the destination: the edge tightening's own
// arithmetic over each sample's neighbourhood, then the same sum and blend. `StarMaskRecombinationTest` holds the two
// routes to one answer.

#include <AfterNightEngine/Algorithms/Stars/StarMaskRecombination.h>

#include <AfterNightEngine/Algorithms/Spatial/GeneralFilter.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/CompositionPlanes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace an
{
namespace
{

using composition_detail::CompositionSource;
using composition_detail::WorkingBlock;
using composition_detail::WorkingPlanes;

constexpr const char* kPlanOperation = "an::planStarMaskRecombination";
constexpr const char* kLayoutOperation = "an::starMaskRecombinationLayout";
constexpr const char* kRecombineOperation = "an::recombineStarMask";

[[nodiscard]] Status recombinationFailure(ErrorCode code, const char* message, const char* operation,
                                          const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

[[nodiscard]] constexpr bool recombinationChannels(std::uint32_t channels) noexcept
{
    return channels == 1u || channels == 3u;
}

[[nodiscard]] constexpr bool normalizedDomain(NumericDomain domain) noexcept
{
    return domain == NumericDomain::NormalizedLinear || domain == NumericDomain::EncodedNonlinear;
}

/// A representation's place in the promotion order.
[[nodiscard]] constexpr int promotionRank(SampleType type) noexcept
{
    switch (type)
    {
    case SampleType::Int8:
        return 0;
    case SampleType::UInt8:
        return 1;
    case SampleType::Int16:
        return 2;
    case SampleType::UInt16:
        return 3;
    case SampleType::Int32:
        return 4;
    case SampleType::UInt32:
        return 5;
    case SampleType::Float32:
        return 6;
    case SampleType::Float64:
        return 7;
    }
    return -1;
}

[[nodiscard]] CompositionSource compositionOf(const StarMaskRecombinationSource& source) noexcept
{
    return CompositionSource{.descriptor = source.descriptor,
                             .samples = source.samples,
                             .layout = source.layout,
                             .validity = source.validity};
}

[[nodiscard]] Status checkRequest(const StarMaskRecombinationRequest& request, const char* operation)
{
    if (!std::isfinite(request.edgeTightening) || request.edgeTightening < 0.0)
    {
        return recombinationFailure(ErrorCode::InvalidArgument, "the edge tightening must be finite and not negative",
                                    operation, "request");
    }
    if (request.outputSampleType.has_value() && !isSampleType(*request.outputSampleType))
    {
        return recombinationFailure(ErrorCode::InvalidArgument,
                                    "the result's representation is outside its enumeration", operation, "request");
    }
    return Status::success();
}

[[nodiscard]] Status checkImages(const ImageDescriptor& starless, const ImageDescriptor& stars, const char* operation)
{
    if (!recombinationChannels(starless.extent().channelCount()))
    {
        return recombinationFailure(ErrorCode::IncompatibleArguments, "a recombination reads one channel or three",
                                    operation, "starless");
    }
    if (!(stars.extent() == starless.extent()))
    {
        return recombinationFailure(ErrorCode::IncompatibleArguments,
                                    "the stars are not the starless image's width, height and channels", operation,
                                    "stars");
    }
    return Status::success();
}

/// The edge tightening a recombination asks of the published filter: the request's amount, clamped to the unit
/// interval in a normalized domain and to the widest Float32 otherwise.
[[nodiscard]] Result<EdgeTightenPlan> edgePlanFor(const StarMaskRecombinationRequest& request, bool heldAtOne)
{
    const double widest = static_cast<double>(std::numeric_limits<float>::max());
    return planEdgeTighten(EdgeTightenRequest{
        .amount = request.edgeTightening, .lowest = heldAtOne ? 0.0 : -widest, .highest = heldAtOne ? 1.0 : widest});
}

/// One walk forming every sample of the result in the starless image's working planes.
template <typename WorkingT>
[[nodiscard]] Status combine(const ExecutionContext& execution, const WorkingPlanes& starless,
                             const WorkingPlanes& stars, const WorkingPlanes* mask, const std::uint8_t* valid,
                             bool heldAtOne, bool invert)
{
    const std::uint32_t width = starless.layout->extent().width();
    const std::uint32_t channels = starless.channels();
    const std::uint32_t maskChannels = mask != nullptr ? mask->channels() : 0u;
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * 4u * sizeof(WorkingT),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 6u};
    return parallelFor(execution, starless.layout->extent().height(), cost, [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const auto row = static_cast<std::uint32_t>(y);
            const std::uint8_t* const rowValid = valid != nullptr ? valid + y * width : nullptr;
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                WorkingT* const base = starless.row<WorkingT>(channel, row);
                const WorkingT* const added = stars.row<WorkingT>(channel, row);
                const WorkingT* const weights =
                    mask != nullptr ? mask->row<WorkingT>(channel < maskChannels ? channel : 0u, row) : nullptr;
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    if (rowValid != nullptr && rowValid[x] == 0u)
                    {
                        continue;
                    }
                    const double starlessValue = static_cast<double>(base[x]);
                    double sum = starlessValue + static_cast<double>(added[x]);
                    if (heldAtOne)
                    {
                        sum = (std::min)(sum, 1.0);
                    }
                    if (weights == nullptr)
                    {
                        base[x] = static_cast<WorkingT>(sum);
                        continue;
                    }
                    const double raw = static_cast<double>(weights[x]);
                    double weight = raw > 0.0 ? (raw < 1.0 ? raw : 1.0) : 0.0;
                    if (invert)
                    {
                        weight = 1.0 - weight;
                    }
                    base[x] = static_cast<WorkingT>(starlessValue * (1.0 - weight) + sum * weight);
                }
            }
        }
        return Status::success();
    });
}

/// Whether a recombination can run straight from its images into the destination: every image and the result
/// Float32 and every sample selected, so that the working planes would hold exactly what the images hold.
[[nodiscard]] bool runsDirect(SampleType outputType, const CompositionSource& base, const CompositionSource& added,
                              const CompositionSource* weights) noexcept
{
    const auto plain = [](const CompositionSource& source) {
        return source.descriptor->sampleType() == SampleType::Float32 &&
               !composition_detail::compositionSourceRuled(source);
    };
    return outputType == SampleType::Float32 && plain(base) && plain(added) && (weights == nullptr || plain(*weights));
}

/// The published edge tightening's numbers for one plan, read once.
struct EdgeArithmetic
{
    double amount = 0.0;
    double offset = 0.0;
    double lowest = 0.0;
    double highest = 1.0;
    double border = 0.0;
};

/// The tightened stars sample at one position, by the published edge tightening's arithmetic over a neighbourhood
/// whose every sample is selected: each of the four directional responses is a double sum of its taps in the
/// stencil's order narrowed to Float32 - a zero tap adds nothing but the sign of a zero sum, which the magnitude
/// discards - the four magnitudes are summed in Float32 in the filter's order, and the sum is scaled, subtracted,
/// offset and clamped as the filter does. `neighbourhood` holds the rows above, at and below, left to right.
[[nodiscard]] float tightenedSample(const std::array<double, 9u>& n, const EdgeArithmetic& edge) noexcept
{
    double first = 0.0;
    first += n[0];
    first += 2.0 * n[1];
    first += n[2];
    first += -1.0 * n[6];
    first += -2.0 * n[7];
    first += -1.0 * n[8];
    double second = 0.0;
    second += -1.0 * n[0];
    second += n[2];
    second += -2.0 * n[3];
    second += 2.0 * n[5];
    second += -1.0 * n[6];
    second += n[8];
    double third = 0.0;
    third += 2.0 * n[0];
    third += n[1];
    third += n[3];
    third += -1.0 * n[5];
    third += -1.0 * n[7];
    third += -2.0 * n[8];
    double fourth = 0.0;
    fourth += n[1];
    fourth += 2.0 * n[2];
    fourth += -1.0 * n[3];
    fourth += n[5];
    fourth += -2.0 * n[6];
    fourth += -1.0 * n[7];
    float magnitude = std::abs(static_cast<float>(first));
    magnitude += std::abs(static_cast<float>(second));
    magnitude += std::abs(static_cast<float>(third));
    const double edgeValue = 0.05 * static_cast<double>(magnitude + std::abs(static_cast<float>(fourth)));
    const double value = n[4] - edge.amount * edgeValue + edge.offset;
    return static_cast<float>((std::clamp)(value, edge.lowest, edge.highest));
}

/// One recombination walked straight from the images into the destination, with the arithmetic of the working walk
/// - the edge tightening's, then `combine`'s.
[[nodiscard]] Status runDirect(const ExecutionContext& execution, const CompositionSource& base,
                               const CompositionSource& added, const CompositionSource* weights, void* destination,
                               const ImageLayout& destinationLayout, bool tighten, const EdgeArithmetic& edge,
                               bool heldAtOne, bool invert)
{
    using composition_detail::channelRowOf;
    const std::uint32_t width = destinationLayout.extent().width();
    const std::uint32_t height = destinationLayout.extent().height();
    const std::uint32_t channels = destinationLayout.extent().channelCount();
    const std::uint32_t maskChannels = weights != nullptr ? weights->descriptor->extent().channelCount() : 0u;
    const std::size_t starlessStep = base.layout->samplesPerPixel();
    const std::size_t starsStep = added.layout->samplesPerPixel();
    const std::size_t maskStep = weights != nullptr ? weights->layout->samplesPerPixel() : 0u;
    const std::size_t outStep = destinationLayout.samplesPerPixel();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * 4u * sizeof(float),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * (tighten ? 40u : 6u)};
    return parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                const float* const starless = channelRowOf<float>(base.samples, *base.layout, channel, y);
                const float* const stars = channelRowOf<float>(added.samples, *added.layout, channel, y);
                const float* const above =
                    y > 0u ? channelRowOf<float>(added.samples, *added.layout, channel, y - 1u) : nullptr;
                const float* const below =
                    y + 1u < height ? channelRowOf<float>(added.samples, *added.layout, channel, y + 1u) : nullptr;
                const float* const mask = weights != nullptr
                                              ? channelRowOf<float>(weights->samples, *weights->layout,
                                                                    channel < maskChannels ? channel : 0u, y)
                                              : nullptr;
                float* const out = channelRowOf<float>(destination, destinationLayout, channel, y);
                std::uint32_t nonFinite = 0u;
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    nonFinite |= composition_detail::nonFiniteBit(starless[x * starlessStep]) |
                                 composition_detail::nonFiniteBit(stars[x * starsStep]) |
                                 (mask != nullptr ? composition_detail::nonFiniteBit(mask[x * maskStep]) : 0u);
                }
                if (nonFinite != 0u)
                {
                    return recombinationFailure(ErrorCode::InvalidData,
                                                "a source holds a sample that is not finite in a pixel its validity "
                                                "admits",
                                                kRecombineOperation, "source");
                }
                const auto at = [&](const float* row, std::int64_t x) -> double {
                    return row == nullptr || x < 0 || x >= static_cast<std::int64_t>(width)
                               ? edge.border
                               : static_cast<double>(row[static_cast<std::size_t>(x) * starsStep]);
                };
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    double starsValue = static_cast<double>(stars[x * starsStep]);
                    if (tighten)
                    {
                        const std::int64_t column = x;
                        const std::array<double, 9u> neighbourhood{
                            at(above, column - 1), at(above, column), at(above, column + 1),
                            at(stars, column - 1), at(stars, column), at(stars, column + 1),
                            at(below, column - 1), at(below, column), at(below, column + 1)};
                        starsValue = static_cast<double>(tightenedSample(neighbourhood, edge));
                    }
                    const double starlessValue = static_cast<double>(starless[x * starlessStep]);
                    double sum = starlessValue + starsValue;
                    if (heldAtOne)
                    {
                        sum = (std::min)(sum, 1.0);
                    }
                    if (mask == nullptr)
                    {
                        out[x * outStep] = static_cast<float>(sum);
                        continue;
                    }
                    const double raw = static_cast<double>(mask[x * maskStep]);
                    double weight = raw > 0.0 ? (raw < 1.0 ? raw : 1.0) : 0.0;
                    if (invert)
                    {
                        weight = 1.0 - weight;
                    }
                    out[x * outStep] = static_cast<float>(starlessValue * (1.0 - weight) + sum * weight);
                }
            }
        }
        return Status::success();
    });
}

} // namespace

Result<StarMaskRecombinationPlan> planStarMaskRecombination(const StarMaskRecombinationRequest& request)
{
    const Status checked = checkRequest(request, kPlanOperation);
    if (checked.failed())
    {
        return checked;
    }
    return StarMaskRecombinationPlan(request);
}

SampleType starMaskRecombinationSampleType(const StarMaskRecombinationPlan& plan, const ImageDescriptor& starless,
                                           const ImageDescriptor& stars) noexcept
{
    if (plan.request().outputSampleType.has_value())
    {
        return *plan.request().outputSampleType;
    }
    return promotionRank(stars.sampleType()) > promotionRank(starless.sampleType()) ? stars.sampleType()
                                                                                    : starless.sampleType();
}

Result<ImageLayoutRequest> starMaskRecombinationLayout(const StarMaskRecombinationPlan& plan,
                                                       const ImageDescriptor& starless, const ImageDescriptor& stars)
{
    const Status images = checkImages(starless, stars, kLayoutOperation);
    if (images.failed())
    {
        return images;
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(starMaskRecombinationSampleType(plan, starless, stars));
    return ImageLayoutRequest{.extent = starless.extent(),
                              .elementSizeBytes = elementBytes,
                              .samplesPerPixel = 1u,
                              .planeCount = starless.extent().channelCount(),
                              .rowAlignmentBytes = elementBytes};
}

Result<StarMaskRecombinationResult>
recombineStarMask(const ExecutionContext& execution, const StarMaskRecombinationPlan& plan,
                  const StarMaskRecombinationSource& starless, const StarMaskRecombinationSource& stars,
                  const StarMaskRecombinationSource* mask, void* destination, const ImageLayout& destinationLayout)
{
    if (!execution.valid())
    {
        return recombinationFailure(ErrorCode::NotInitialized, "a recombination was given an empty execution context",
                                    kRecombineOperation, "execution");
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
    const StarMaskRecombinationRequest& request = plan.request();
    const CompositionSource base = compositionOf(starless);
    const CompositionSource added = compositionOf(stars);
    for (const auto& [source, parameter] : {std::pair{&base, "starless"}, std::pair{&added, "stars"}})
    {
        const Status checked = composition_detail::checkCompositionSource(*source, kRecombineOperation, parameter);
        if (checked.failed())
        {
            return checked;
        }
    }
    const Status images = checkImages(*starless.descriptor, *stars.descriptor, kRecombineOperation);
    if (images.failed())
    {
        return images;
    }
    const NumericDomain domain = starless.descriptor->numericDomain();
    if (stars.descriptor->numericDomain() != domain)
    {
        return recombinationFailure(ErrorCode::IncompatibleArguments,
                                    "the starless image and the stars state different numeric domains",
                                    kRecombineOperation, "stars");
    }
    if (domain == NumericDomain::RawSensorCode)
    {
        return recombinationFailure(ErrorCode::IncompatibleArguments,
                                    "a recombination does not add two sensor readings; restate them on purpose first",
                                    kRecombineOperation, "starless");
    }
    const ImageExtent& extent = starless.descriptor->extent();
    std::optional<CompositionSource> weights;
    if (mask != nullptr)
    {
        weights = compositionOf(*mask);
        const Status checked = composition_detail::checkCompositionSource(*weights, kRecombineOperation, "mask");
        if (checked.failed())
        {
            return checked;
        }
        const ImageExtent& maskExtent = mask->descriptor->extent();
        if (maskExtent.width() != extent.width() || maskExtent.height() != extent.height() ||
            (maskExtent.channelCount() != 1u && maskExtent.channelCount() != extent.channelCount()))
        {
            return recombinationFailure(ErrorCode::IncompatibleArguments,
                                        "the mask is not one channel or the images' channels at their width and height",
                                        kRecombineOperation, "mask");
        }
        if (!normalizedDomain(mask->descriptor->numericDomain()))
        {
            return recombinationFailure(ErrorCode::IncompatibleArguments,
                                        "a mask is read as a fraction of full scale, so its domain is a normalized one",
                                        kRecombineOperation, "mask");
        }
    }
    const SampleType outputType = starMaskRecombinationSampleType(plan, *starless.descriptor, *stars.descriptor);
    const Status destinationChecked = composition_detail::checkCompositionDestination(
        outputType, destination, destinationLayout, extent, kRecombineOperation);
    if (destinationChecked.failed())
    {
        return destinationChecked;
    }
    const std::uint64_t destinationBytes = destinationLayout.totalBytes();
    for (const auto& [source, parameter] :
         {std::pair{&base, "starless"}, std::pair{&added, "stars"},
          std::pair{weights.has_value() ? &*weights : static_cast<const CompositionSource*>(nullptr), "mask"}})
    {
        if (source == nullptr)
        {
            continue;
        }
        const Status separate = composition_detail::checkCompositionSeparate(*source, destination, destinationBytes,
                                                                             kRecombineOperation, parameter);
        if (separate.failed())
        {
            return separate;
        }
    }

    const std::uint32_t width = extent.width();
    const std::uint32_t height = extent.height();
    const std::uint32_t channels = extent.channelCount();
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    const bool tighten = request.edgeTightening > 0.0;
    if (runsDirect(outputType, base, added, weights.has_value() ? &*weights : nullptr))
    {
        EdgeArithmetic edge{};
        if (tighten)
        {
            const Result<EdgeTightenPlan> edgePlan = edgePlanFor(request, normalizedDomain(domain));
            if (!edgePlan.hasValue())
            {
                return edgePlan.status();
            }
            const EdgeTightenRequest& edgeRequest = edgePlan->request();
            edge = EdgeArithmetic{.amount = edgeRequest.amount,
                                  .offset = edgeRequest.offset,
                                  .lowest = edgeRequest.lowest,
                                  .highest = edgeRequest.highest,
                                  .border = static_cast<double>(edgeRequest.borders.constantBorder)};
        }
        const Status direct = runDirect(execution, base, added, weights.has_value() ? &*weights : nullptr, destination,
                                        destinationLayout, tighten, edge, normalizedDomain(domain), request.invertMask);
        if (direct.failed())
        {
            return direct;
        }
        StarMaskRecombinationResult result;
        result.run.selection.total = pixels;
        result.run.selection.valid = pixels;
        result.run.outputsWritten = pixels * channels;
        return result;
    }

    const SampleType workingType = outputType == SampleType::Float64 ? SampleType::Float64 : SampleType::Float32;
    WorkingBlock starlessBlock;
    WorkingBlock starsBlock;
    WorkingBlock tightenedBlock;
    WorkingBlock maskBlock;
    WorkingBlock validBlock;
    const Result<WorkingPlanes> starlessPlanes = composition_detail::takeWorkingPlanes(
        execution, starlessBlock, width, height, channels, workingType, kRecombineOperation);
    if (!starlessPlanes.hasValue())
    {
        return starlessPlanes.status();
    }
    const Result<WorkingPlanes> starsPlanes = composition_detail::takeWorkingPlanes(
        execution, starsBlock, width, height, channels, workingType, kRecombineOperation);
    if (!starsPlanes.hasValue())
    {
        return starsPlanes.status();
    }
    std::optional<WorkingPlanes> maskPlanes;
    if (weights.has_value())
    {
        Result<WorkingPlanes> taken = composition_detail::takeWorkingPlanes(execution, maskBlock, width, height,
                                                                            mask->descriptor->extent().channelCount(),
                                                                            workingType, kRecombineOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        maskPlanes = *taken;
    }
    std::uint8_t* valid = nullptr;
    if (composition_detail::compositionSourceRuled(base) || composition_detail::compositionSourceRuled(added) ||
        (weights.has_value() && composition_detail::compositionSourceRuled(*weights)))
    {
        const Status taken = validBlock.take(execution, pixels, kRecombineOperation);
        if (taken.failed())
        {
            return taken;
        }
        valid = validBlock.bytes();
        std::memset(valid, 1, static_cast<std::size_t>(pixels));
    }

    Status read = composition_detail::readWorking(execution, base, *starlessPlanes, 0u, valid, kRecombineOperation);
    if (read.ok())
    {
        read = composition_detail::readWorking(execution, added, *starsPlanes, 0u, valid, kRecombineOperation);
    }
    if (read.ok() && maskPlanes.has_value())
    {
        read = composition_detail::readWorking(execution, *weights, *maskPlanes, 0u, valid, kRecombineOperation);
    }
    if (read.failed())
    {
        return read;
    }

    WorkingPlanes addedPlanes = *starsPlanes;
    if (tighten)
    {
        const Result<WorkingPlanes> tightened = composition_detail::takeWorkingPlanes(
            execution, tightenedBlock, width, height, channels, workingType, kRecombineOperation);
        if (!tightened.hasValue())
        {
            return tightened.status();
        }
        const Result<EdgeTightenPlan> edgePlan = edgePlanFor(request, normalizedDomain(domain));
        if (!edgePlan.hasValue())
        {
            return edgePlan.status();
        }
        const Result<SpatialFilterResult> filtered =
            filterImageEdgeTighten(execution, workingType, starsPlanes->samples, *starsPlanes->layout,
                                   tightened->samples, *tightened->layout, *edgePlan);
        if (!filtered.hasValue())
        {
            return filtered.status();
        }
        addedPlanes = *tightened;
    }

    const bool heldAtOne = normalizedDomain(domain);
    const Status combined =
        workingType == SampleType::Float64
            ? combine<double>(execution, *starlessPlanes, addedPlanes, maskPlanes.has_value() ? &*maskPlanes : nullptr,
                              valid, heldAtOne, request.invertMask)
            : combine<float>(execution, *starlessPlanes, addedPlanes, maskPlanes.has_value() ? &*maskPlanes : nullptr,
                             valid, heldAtOne, request.invertMask);
    if (combined.failed())
    {
        return combined;
    }
    const Status written = composition_detail::writeWorking(execution, *starlessPlanes, outputType, destination,
                                                            destinationLayout, kRecombineOperation);
    if (written.failed())
    {
        return written;
    }
    StarMaskRecombinationResult result;
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
    result.run.outputsWritten = pixels * channels;
    return result;
}

} // namespace an
