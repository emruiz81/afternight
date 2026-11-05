// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the difference of two images, sample by sample, written as an image.
//
// One walk over the rows. A row is taken a run of pixels at a time: each channel of each source is read
// into a run of doubles through that source's validity - an integer in a normalized or encoded domain as a
// fraction of full scale by the engine's own conversion, an integer sensor code as the code, a float as it
// is - and the measure is formed from the two runs and narrowed once into the output. The runs live on the
// stack, so the operation takes no memory of its own.

#include <AfterNightEngine/Algorithms/Pointwise/ImageDifference.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/DescriptorError.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace an
{
namespace
{

constexpr const char* kPlanOperation = "an::planImageDifference";
constexpr const char* kDescriptorOperation = "an::imageDifferenceDescriptor";
constexpr const char* kLayoutOperation = "an::imageDifferenceLayout";
constexpr const char* kComputeOperation = "an::computeImageDifference";

/// How many pixels of a row one pass reads into its runs. Three runs of doubles at this length stay
/// well inside a core's first-level cache and well inside a worker's stack.
constexpr std::uint32_t kRunPixels = 256u;

[[nodiscard]] Status differenceFailure(ErrorCode code, const char* message, const char* operation,
                                       const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// Whether a luminance can be formed over this schema: grey, or red, green and blue with or without alpha.
[[nodiscard]] constexpr bool luminanceSchema(ChannelSchema schema) noexcept
{
    return schema == ChannelSchema::Mono || schema == ChannelSchema::Rgb || schema == ChannelSchema::Rgba;
}

/// How many channels the measure writes for a source of this many.
[[nodiscard]] constexpr std::uint32_t writtenChannels(ImageDifferenceMeasure measure, std::uint32_t channels) noexcept
{
    return measure == ImageDifferenceMeasure::Absolute ? channels : 1u;
}

/// Whether two descriptions can be differenced under a plan, and if not, why.
[[nodiscard]] Status checkPair(const ImageDifferenceRequest& request, const ImageDescriptor& left,
                               const ImageDescriptor& right, const char* operation)
{
    if (!(left.extent() == right.extent()))
    {
        return differenceFailure(ErrorCode::IncompatibleArguments,
                                 "a difference compares two images of one width, height and channel count", operation,
                                 "right");
    }
    if (left.channelSchema() != right.channelSchema() || left.colorSpace() != right.colorSpace())
    {
        return differenceFailure(ErrorCode::IncompatibleArguments,
                                 "a difference compares channels that mean the same thing, so both images state one "
                                 "schema and one colour basis",
                                 operation, "right");
    }
    if (left.numericDomain() != right.numericDomain())
    {
        return differenceFailure(ErrorCode::IncompatibleArguments,
                                 "the two images state different numeric domains, so their difference would measure "
                                 "the change of domain; restate one description on purpose to compare them",
                                 operation, "right");
    }
    if (request.measure == ImageDifferenceMeasure::Luminance && !luminanceSchema(left.channelSchema()))
    {
        return differenceFailure(ErrorCode::IncompatibleArguments,
                                 "a luminance is a weighted sum of grey or of red, green and blue; convert another "
                                 "schema first",
                                 operation, "left");
    }
    return Status::success();
}

[[nodiscard]] bool alignedFor(const void* samples, std::uint32_t elementBytes) noexcept
{
    return elementBytes != 0u && reinterpret_cast<std::uintptr_t>(samples) % elementBytes == 0u;
}

/// Whether a source's members describe one another, and its mask what its rule needs.
[[nodiscard]] Status checkSource(const ImageDifferenceSource& source, const char* parameter)
{
    if (source.descriptor == nullptr || source.samples == nullptr || source.layout == nullptr)
    {
        return differenceFailure(ErrorCode::InvalidArgument, "a difference source has a null member", kComputeOperation,
                                 parameter);
    }
    const ImageDescriptor& descriptor = *source.descriptor;
    const ImageLayout& layout = *source.layout;
    const std::uint32_t elementBytes = sampleTypeSizeBytes(descriptor.sampleType());
    if (!(layout.extent() == descriptor.extent()) || layout.elementSizeBytes() != elementBytes ||
        layout.planeCount() * layout.samplesPerPixel() != descriptor.extent().channelCount() ||
        !alignedFor(source.samples, elementBytes))
    {
        return differenceFailure(ErrorCode::InvalidArgument,
                                 "a difference source's layout or pointer does not describe its descriptor",
                                 kComputeOperation, parameter);
    }
    const KernelValidityView& validity = source.validity;
    if (!validity.descriptor.needsMask())
    {
        if (validity.mask != nullptr || validity.maskPlane != 0u)
        {
            return differenceFailure(ErrorCode::InvalidArgument, "the validity view supplies an unused mask",
                                     kComputeOperation, parameter);
        }
        return Status::success();
    }
    if (validity.mask == nullptr)
    {
        return differenceFailure(ErrorCode::InvalidArgument, "an explicit-mask source supplies no mask",
                                 kComputeOperation, parameter);
    }
    const std::uint32_t channels = descriptor.extent().channelCount();
    const bool perChannel = validity.mask->planeCount() == channels && channels > 1u;
    if (validity.mask->width() != descriptor.extent().width() ||
        validity.mask->height() != descriptor.extent().height() ||
        (!perChannel && validity.maskPlane >= validity.mask->planeCount()))
    {
        return differenceFailure(ErrorCode::IncompatibleArguments, "the validity mask does not cover its source",
                                 kComputeOperation, parameter);
    }
    return Status::success();
}

/// Whether a source's samples or mask share memory with the destination.
[[nodiscard]] Status checkSeparate(const ImageDifferenceSource& source, const std::uint8_t* destination,
                                   std::uint64_t destinationBytes, const char* parameter)
{
    if (regionsOverlap(static_cast<const std::uint8_t*>(source.samples), source.layout->totalBytes(), destination,
                       destinationBytes))
    {
        return differenceFailure(ErrorCode::IncompatibleArguments, "the difference destination overlaps a source",
                                 kComputeOperation, parameter);
    }
    if (source.validity.mask != nullptr &&
        regionsOverlap(source.validity.mask->data(), source.validity.mask->spannedByteCount(), destination,
                       destinationBytes))
    {
        return differenceFailure(ErrorCode::InvalidArgument, "a validity mask overlaps the difference destination",
                                 kComputeOperation, parameter);
    }
    return Status::success();
}

/// Whether one packed mask bit is set.
[[nodiscard]] bool maskBit(const ValidityMaskView<const std::uint8_t>& mask, std::uint32_t plane, std::uint32_t x,
                           std::uint32_t y) noexcept
{
    const std::uint64_t offset = static_cast<std::uint64_t>(plane) * mask.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * mask.rowStrideBytes() + x / 8u;
    return (mask.data()[offset] & static_cast<std::uint8_t>(1u << (x % 8u))) != 0u;
}

/// One source, checked, with what reading it needs resolved once.
struct SourceReader
{
    /// The source being read, borrowed for the call.
    const ImageDifferenceSource* source = nullptr;

    /// Whether an integer sample is a fraction of full scale or a code.
    ConversionPolicy policy{};

    /// Whether the mask gives every channel its own plane.
    bool perChannelMask = false;

    /// Whether any sample can be invalid: a value rule other than all-valid, or a mask.
    bool ruled = false;

    /// Reads a run of one channel of one row as the source's domain reads it, and which samples mean something.
    ///
    /// @param channel which channel.
    /// @param y the row.
    /// @param x0 the first pixel of the run.
    /// @param count how many pixels, at most `kRunPixels`.
    /// @param values where the values go.
    /// @param valid where each sample's validity goes.
    void read(std::uint32_t channel, std::uint32_t y, std::uint32_t x0, std::uint32_t count, double* values,
              bool* valid) const noexcept
    {
        const ImageLayout& layout = *source->layout;
        const std::uint8_t* const row =
            static_cast<const std::uint8_t*>(source->samples) + plane_detail::channelRowByteOffset(layout, channel, y);
        const std::uint32_t stride = layout.samplesPerPixel();
        const KernelValidityView& validity = source->validity;
        const std::uint32_t maskPlane = perChannelMask ? channel : validity.maskPlane;
        // A checked descriptor names one of the eight representations, so the dispatch always runs.
        (void)dispatchSampleType(source->descriptor->sampleType(), [&]<typename SampleT>() noexcept {
            const SampleT* const samples =
                reinterpret_cast<const SampleT*>(row) + static_cast<std::size_t>(x0) * stride;
            for (std::uint32_t index = 0u; index < count; ++index)
            {
                const SampleT raw = samples[static_cast<std::size_t>(index) * stride];
                if constexpr (std::is_floating_point_v<SampleT>)
                {
                    values[index] = static_cast<double>(raw);
                }
                else
                {
                    values[index] = convertSample<SampleT, double>(raw, policy).value;
                }
                valid[index] =
                    !ruled || (sampleIsValidByValue(validity.descriptor, raw) &&
                               (validity.mask == nullptr || maskBit(*validity.mask, maskPlane, x0 + index, y)));
            }
        });
    }
};

[[nodiscard]] SourceReader readerOf(const ImageDifferenceSource& source) noexcept
{
    SourceReader reader;
    reader.source = &source;
    reader.policy.domain = source.descriptor->numericDomain() == NumericDomain::RawSensorCode
                               ? DomainMapping::CodeValue
                               : DomainMapping::FullScale;
    const std::uint32_t channels = source.descriptor->extent().channelCount();
    reader.perChannelMask =
        source.validity.mask != nullptr && source.validity.mask->planeCount() == channels && channels > 1u;
    reader.ruled = source.validity.descriptor.rule() != ValidityRule::AllValid || source.validity.mask != nullptr;
    return reader;
}

/// Raises a shared largest difference to a candidate, with a not-a-number winning.
void raiseLargest(std::atomic<std::uint64_t>& largest, double candidate) noexcept
{
    std::uint64_t observed = largest.load(std::memory_order_relaxed);
    for (;;)
    {
        const double current = std::bit_cast<double>(observed);
        if (std::isnan(current) || (!std::isnan(candidate) && !(candidate > current)))
        {
            return;
        }
        if (largest.compare_exchange_weak(observed, std::bit_cast<std::uint64_t>(candidate), std::memory_order_relaxed))
        {
            return;
        }
    }
}

/// What one range of rows found, added to the totals once.
struct RangeCounts
{
    /// Inputs considered and admitted.
    std::uint64_t total = 0u;

    /// Inputs admitted.
    std::uint64_t valid = 0u;

    /// The largest difference written in the range, or zero.
    double largest = 0.0;

    /// Whether a written difference was not a number.
    bool sawNaN = false;

    /// Takes one written difference into the range's largest.
    void observe(double difference) noexcept
    {
        if (std::isnan(difference))
        {
            sawNaN = true;
        }
        else if (difference > largest)
        {
            largest = difference;
        }
    }
};

/// Writes the difference of the rows in one range.
template <typename OutputT>
void differenceRows(const ImageDifferenceRequest& request, const SourceReader& left, const SourceReader& right,
                    std::uint8_t* destination, const ImageLayout& destinationLayout, std::uint32_t channels,
                    TaskRange range, RangeCounts& counts) noexcept
{
    const std::uint32_t width = destinationLayout.extent().width();
    const bool absolute = request.measure == ImageDifferenceMeasure::Absolute;
    const bool luminance = request.measure == ImageDifferenceMeasure::Luminance;
    const std::uint32_t colorChannels = luminance ? (channels == 1u ? 1u : 3u) : channels;
    std::array<double, 3u> weights = request.luminanceWeights;
    if (luminance && channels == 1u)
    {
        weights = {1.0, 0.0, 0.0};
    }

    std::array<double, kRunPixels> leftValues{};
    std::array<double, kRunPixels> rightValues{};
    std::array<bool, kRunPixels> leftValid{};
    std::array<bool, kRunPixels> rightValid{};
    std::array<double, kRunPixels> leftSum{};
    std::array<double, kRunPixels> rightSum{};
    std::array<bool, kRunPixels> pixelValid{};

    for (std::uint64_t row = range.begin; row < range.end; ++row)
    {
        const std::uint32_t y = static_cast<std::uint32_t>(row);
        for (std::uint32_t x0 = 0u; x0 < width; x0 += kRunPixels)
        {
            const std::uint32_t count = (std::min)(kRunPixels, width - x0);
            if (!absolute)
            {
                std::fill_n(leftSum.begin(), count, 0.0);
                std::fill_n(rightSum.begin(), count, 0.0);
                std::fill_n(pixelValid.begin(), count, true);
            }
            const std::uint32_t readChannels = absolute ? channels : (luminance ? colorChannels : channels);
            for (std::uint32_t channel = 0u; channel < readChannels; ++channel)
            {
                left.read(channel, y, x0, count, leftValues.data(), leftValid.data());
                right.read(channel, y, x0, count, rightValues.data(), rightValid.data());
                if (absolute)
                {
                    OutputT* const out = reinterpret_cast<OutputT*>(destination + plane_detail::channelRowByteOffset(
                                                                                      destinationLayout, channel, y)) +
                                         x0;
                    for (std::uint32_t index = 0u; index < count; ++index)
                    {
                        ++counts.total;
                        if (leftValid[index] && rightValid[index])
                        {
                            ++counts.valid;
                            const double difference = std::fabs(leftValues[index] - rightValues[index]);
                            out[index] = static_cast<OutputT>(difference);
                            counts.observe(difference);
                        }
                    }
                    continue;
                }
                const double weight = luminance ? weights[channel] : 1.0;
                for (std::uint32_t index = 0u; index < count; ++index)
                {
                    pixelValid[index] = pixelValid[index] && leftValid[index] && rightValid[index];
                    if (luminance)
                    {
                        leftSum[index] += weight * leftValues[index];
                        rightSum[index] += weight * rightValues[index];
                    }
                    else
                    {
                        leftSum[index] += std::fabs(leftValues[index] - rightValues[index]);
                    }
                }
            }
            if (absolute)
            {
                continue;
            }
            OutputT* const out =
                reinterpret_cast<OutputT*>(destination + plane_detail::channelRowByteOffset(destinationLayout, 0u, y)) +
                x0;
            const double channelCount = static_cast<double>(channels);
            for (std::uint32_t index = 0u; index < count; ++index)
            {
                ++counts.total;
                if (!pixelValid[index])
                {
                    continue;
                }
                ++counts.valid;
                const double difference =
                    luminance ? std::fabs(leftSum[index] - rightSum[index]) : leftSum[index] / channelCount;
                out[index] = static_cast<OutputT>(difference);
                counts.observe(difference);
            }
        }
    }
}

} // namespace

Result<ImageDifferencePlan> planImageDifference(const ImageDifferenceRequest& request)
{
    if (!isImageDifferenceMeasure(request.measure))
    {
        return differenceFailure(ErrorCode::InvalidArgument, "the difference measure is outside its enumeration",
                                 kPlanOperation, "measure");
    }
    double sum = 0.0;
    for (const double weight : request.luminanceWeights)
    {
        if (!std::isfinite(weight) || weight < 0.0)
        {
            return differenceFailure(ErrorCode::InvalidArgument, "a luminance weight must be finite and not negative",
                                     kPlanOperation, "luminanceWeights");
        }
        sum += weight;
    }
    if (!(sum > 0.0))
    {
        return differenceFailure(ErrorCode::InvalidArgument, "the luminance weights must sum to a positive number",
                                 kPlanOperation, "luminanceWeights");
    }
    if (request.output != SampleType::Float32 && request.output != SampleType::Float64)
    {
        return differenceFailure(ErrorCode::InvalidArgument,
                                 "a difference is written as Float32 or Float64, so nothing is rounded to a code",
                                 kPlanOperation, "output");
    }
    return ImageDifferencePlan(request);
}

Result<ImageDescriptor> imageDifferenceDescriptor(const ImageDifferencePlan& plan, const ImageDescriptor& left,
                                                  const ImageDescriptor& right)
{
    const ImageDifferenceRequest& request = plan.request();
    const Status paired = checkPair(request, left, right, kDescriptorOperation);
    if (paired.failed())
    {
        return paired;
    }
    const std::uint32_t channels = writtenChannels(request.measure, left.extent().channelCount());
    const std::optional<ImageExtent> extent =
        ImageExtent::create(left.extent().width(), left.extent().height(), channels, defaultEngineLimits());
    if (!extent.has_value())
    {
        return differenceFailure(ErrorCode::InvalidArgument, "the difference would have a shape no image can have",
                                 kDescriptorOperation, "left");
    }
    ImageDescriptorRequest described = left.toRequest();
    described.extent = *extent;
    described.sampleType = request.output;
    if (request.measure != ImageDifferenceMeasure::Absolute)
    {
        described.channelSchema = ChannelSchema::Mono;
        described.colorSpace = ColorSpace::Unspecified;
    }
    const std::optional<ImageDescriptor> made = ImageDescriptor::create(described);
    if (!made.has_value())
    {
        return Status::failure(ErrorCode::InvalidArgument,
                               StatusDetail{.message = descriptorErrorName(ImageDescriptor::check(described)),
                                            .operation = kDescriptorOperation,
                                            .domain = ErrorDomain::Algorithms,
                                            .context = {StatusContextField::ofText(ContextKey::Parameter, "left")}});
    }
    return *made;
}

Result<ImageLayoutRequest> imageDifferenceLayout(const ImageDifferencePlan& plan, const ImageDescriptor& left)
{
    const ImageDifferenceRequest& request = plan.request();
    if (request.measure == ImageDifferenceMeasure::Luminance && !luminanceSchema(left.channelSchema()))
    {
        return differenceFailure(ErrorCode::IncompatibleArguments,
                                 "a luminance is a weighted sum of grey or of red, green and blue; convert another "
                                 "schema first",
                                 kLayoutOperation, "left");
    }
    const std::uint32_t channels = writtenChannels(request.measure, left.extent().channelCount());
    const std::optional<ImageExtent> extent =
        ImageExtent::create(left.extent().width(), left.extent().height(), channels, defaultEngineLimits());
    if (!extent.has_value())
    {
        return differenceFailure(ErrorCode::IncompatibleArguments,
                                 "the difference would have a shape no image can have", kLayoutOperation, "left");
    }
    return ImageLayoutRequest{.extent = *extent,
                              .elementSizeBytes = sampleTypeSizeBytes(request.output),
                              .samplesPerPixel = 1u,
                              .planeCount = channels,
                              .rowAlignmentBytes = kDefaultBufferAlignmentBytes};
}

Result<ImageDifferenceResult> computeImageDifference(const ExecutionContext& execution, const ImageDifferencePlan& plan,
                                                     const ImageDifferenceSource& left,
                                                     const ImageDifferenceSource& right, void* destination,
                                                     const ImageLayout& destinationLayout)
{
    if (!execution.valid())
    {
        return differenceFailure(ErrorCode::NotInitialized, "a difference was given an empty execution context",
                                 kComputeOperation, "execution");
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
    const Status leftChecked = checkSource(left, "left");
    if (leftChecked.failed())
    {
        return leftChecked;
    }
    const Status rightChecked = checkSource(right, "right");
    if (rightChecked.failed())
    {
        return rightChecked;
    }
    const ImageDifferenceRequest& request = plan.request();
    const Status paired = checkPair(request, *left.descriptor, *right.descriptor, kComputeOperation);
    if (paired.failed())
    {
        return paired;
    }

    const std::uint32_t channels = left.descriptor->extent().channelCount();
    const std::uint32_t written = writtenChannels(request.measure, channels);
    const std::uint32_t outputBytes = sampleTypeSizeBytes(request.output);
    if (destination == nullptr || !alignedFor(destination, outputBytes) ||
        destinationLayout.elementSizeBytes() != outputBytes)
    {
        return differenceFailure(ErrorCode::InvalidArgument,
                                 "the difference destination is null or does not describe the output's samples",
                                 kComputeOperation, "destination");
    }
    if (destinationLayout.extent().width() != left.descriptor->extent().width() ||
        destinationLayout.extent().height() != left.descriptor->extent().height() ||
        destinationLayout.extent().channelCount() != written || destinationLayout.samplesPerPixel() != 1u ||
        destinationLayout.planeCount() != written)
    {
        return differenceFailure(ErrorCode::IncompatibleArguments,
                                 "the difference destination has another extent or arrangement than the output",
                                 kComputeOperation, "destinationLayout");
    }
    std::uint8_t* const destinationBytes = static_cast<std::uint8_t*>(destination);
    const Status leftSeparate = checkSeparate(left, destinationBytes, destinationLayout.totalBytes(), "left");
    if (leftSeparate.failed())
    {
        return leftSeparate;
    }
    const Status rightSeparate = checkSeparate(right, destinationBytes, destinationLayout.totalBytes(), "right");
    if (rightSeparate.failed())
    {
        return rightSeparate;
    }

    const SourceReader leftReader = readerOf(left);
    const SourceReader rightReader = readerOf(right);
    const std::uint32_t width = destinationLayout.extent().width();
    const std::uint64_t sourceBytes = static_cast<std::uint64_t>(sampleTypeSizeBytes(left.descriptor->sampleType())) +
                                      sampleTypeSizeBytes(right.descriptor->sampleType());
    const TaskCost cost{.bytesPerItem =
                            static_cast<std::uint64_t>(width) * (channels * sourceBytes + written * outputBytes),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 4u};

    std::atomic<std::uint64_t> total{0u};
    std::atomic<std::uint64_t> valid{0u};
    std::atomic<std::uint64_t> largest{std::bit_cast<std::uint64_t>(0.0)};
    const Status ran = parallelFor(execution, static_cast<std::uint64_t>(destinationLayout.extent().height()), cost,
                                   [&](TaskRange range) -> Status {
                                       RangeCounts counts;
                                       if (request.output == SampleType::Float32)
                                       {
                                           differenceRows<float>(request, leftReader, rightReader, destinationBytes,
                                                                 destinationLayout, channels, range, counts);
                                       }
                                       else
                                       {
                                           differenceRows<double>(request, leftReader, rightReader, destinationBytes,
                                                                  destinationLayout, channels, range, counts);
                                       }
                                       total.fetch_add(counts.total, std::memory_order_relaxed);
                                       valid.fetch_add(counts.valid, std::memory_order_relaxed);
                                       raiseLargest(largest, counts.sawNaN ? std::numeric_limits<double>::quiet_NaN()
                                                                           : counts.largest);
                                       return Status::success();
                                   });
    if (ran.failed())
    {
        return ran;
    }

    ImageDifferenceResult result;
    result.run.selection.total = total.load(std::memory_order_relaxed);
    result.run.selection.valid = valid.load(std::memory_order_relaxed);
    result.run.outputsWritten = result.run.selection.valid;
    result.largestDifference = std::bit_cast<double>(largest.load(std::memory_order_relaxed));
    return result;
}

} // namespace an
