// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the display presentation: one stated pipeline from an image to what a viewer paints.
//
// The pipeline is a composition. Promotion and the output are this file's own walks, because no other
// operation reads every representation into a Float32 working image with its invalid samples marked, or
// packs one into bytes. The colour balance, the statistics, the stretch, the saturation and the
// brightness and contrast are the published operations of their own domains, called on the working image.
//
// A working sample is held to the unit interval as it is promoted, which is all a display shows, so the automatic
// stretch measures what it will present. An invalid sample is a quiet NaN in the working image, and a working image
// holding any is described by
// the finite-only rule; one holding none is all-valid, which is what lets the stretch take its compiled
// path. The stretch and the pointwise executor both judge validity by the pixel, so while a working image
// holds an invalid sample they run one colour plane at a time: a pixel whose red is invalid still has a
// green to show. An analysis alone over a Float32 source that balances nothing promotes no working image: the
// robust statistics read the stored samples where they lie, through the finite-only rule and a hold to the unit
// interval, which is the promotion's own arithmetic, so the figures are the working image's bit for bit without
// the copy a measurement would otherwise make and release. An eight- or sixteen-bit source that balances nothing is
// measured from how often each of its codes occurs, each code promoted once, before anything is promoted: an analysis
// alone then promotes nothing either, and a presentation stretches each row as it promotes it.

#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>

#include "kernels/reference/MidtonesTransferRow.h"
#include "kernels/reference/AlgorithmKernels.h"

#include <AfterNightEngine/Algorithms/Color/ColorAdjustment.h>
#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Algorithms/Photometry/ColorCalibrationApply.h>
#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseCanonicalization.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace an
{
namespace
{

constexpr const char* kPlanOperation = "an::planDisplayPresentation";
constexpr const char* kPreviewPlanOperation = "an::planRealtimePreviewDisplayPresentation";
constexpr const char* kLayoutOperation = "an::displayPresentationLayout";
constexpr const char* kPreviewLayoutOperation = "an::realtimePreviewDisplayPresentationLayout";
constexpr const char* kAnalyzeOperation = "an::analyzeDisplayPresentation";
constexpr const char* kPresentOperation = "an::presentImage";
constexpr const char* kPreviewOperation = "an::presentImageRealtimePreview";

/// The working image's representation: four bytes a sample.
constexpr std::uint32_t kWorkingSampleBytes = sizeof(float);

/// Rows of the byte outputs are aligned to this, which a toolkit image built over the buffer expects.
constexpr std::uint64_t kByteOutputRowAlignment = 4u;

/// The levels `PresentationPromotion::Quantized16` holds a sample to, less one.
constexpr double kQuantizedLevels = 65535.0;

/// Adding and subtracting this rounds a double in `[0, 2^51)` to the nearest integer, ties to even, in
/// the default rounding mode - the rounding the engine's own conversion applies.
constexpr double kRoundingShift = 6755399441055744.0;

[[nodiscard]] Status presentationFailure(ErrorCode code, const char* message, const char* operation)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

[[nodiscard]] bool unitInterval(const PresentationInterval& interval) noexcept
{
    return std::isfinite(interval.low) && std::isfinite(interval.high) && interval.low >= 0.0 && interval.high <= 1.0 &&
           interval.low <= interval.high;
}

[[nodiscard]] Status checkRequest(const DisplayPresentationRequest& request, const char* operation)
{
    if (!isPresentationPromotion(request.promotion) || !isPresentationStretch(request.stretch) ||
        !isPresentationOutput(request.output) || !isPresentationInvalidPixels(request.invalidPixels) ||
        !isSaturationBackgroundLocation(request.saturationBackgroundLocation))
    {
        return presentationFailure(ErrorCode::InvalidArgument, "a presentation enumerator is outside its enumeration",
                                   operation);
    }
    if (!unitInterval(request.neutralizationBackground) || !unitInterval(request.calibrationBackground) ||
        !unitInterval(request.calibrationWhite))
    {
        return presentationFailure(ErrorCode::InvalidArgument,
                                   "a colour-balance interval is not finite, ordered and within the unit interval",
                                   operation);
    }
    if (!std::isfinite(request.shadowsClipping) || !std::isfinite(request.targetBackground) ||
        request.targetBackground < 0.0 || request.targetBackground > 1.0)
    {
        return presentationFailure(
            ErrorCode::InvalidArgument,
            "the automatic stretch needs a finite clipping and a target within the unit interval", operation);
    }
    if (!std::isfinite(request.saturation) || !std::isfinite(request.saturationBackgroundFactor))
    {
        return presentationFailure(ErrorCode::InvalidArgument,
                                   "the saturation and its background factor must be finite", operation);
    }
    if (!std::isfinite(request.brightness) || request.brightness < -1.0 || request.brightness > 1.0)
    {
        return presentationFailure(ErrorCode::InvalidArgument, "the brightness must be finite and within [-1, 1]",
                                   operation);
    }
    if (!std::isfinite(request.contrast) || !(request.contrast > 0.0))
    {
        return presentationFailure(ErrorCode::InvalidArgument, "the contrast must be a finite positive factor",
                                   operation);
    }
    return Status::success();
}

/// What a source is, once checked.
struct SourceShape
{
    /// The resident representation.
    SampleType sampleType = SampleType::Float32;

    /// The pixel dimensions.
    std::uint32_t width = 0u;

    /// The pixel dimensions.
    std::uint32_t height = 0u;

    /// Every channel, alpha included.
    std::uint32_t channels = 0u;

    /// The channels that carry colour or grey: one or three.
    std::uint32_t colorChannels = 0u;
};

/// The channels a schema presents and how many of them carry colour, or nothing for a schema this
/// presentation does not show.
[[nodiscard]] std::optional<SourceShape> shapeOf(const ImageDescriptor& descriptor) noexcept
{
    SourceShape shape;
    shape.sampleType = descriptor.sampleType();
    shape.width = descriptor.extent().width();
    shape.height = descriptor.extent().height();
    shape.channels = descriptor.extent().channelCount();
    switch (descriptor.channelSchema())
    {
    case ChannelSchema::Mono:
        shape.colorChannels = 1u;
        return shape.channels == 1u ? std::optional<SourceShape>(shape) : std::nullopt;
    case ChannelSchema::Rgb:
        shape.colorChannels = 3u;
        return shape.channels == 3u ? std::optional<SourceShape>(shape) : std::nullopt;
    case ChannelSchema::Rgba:
        shape.colorChannels = 3u;
        return shape.channels == 4u ? std::optional<SourceShape>(shape) : std::nullopt;
    default:
        return std::nullopt;
    }
}

/// An extent built against limits that admit exactly it, for a dimension already checked elsewhere.
[[nodiscard]] std::optional<ImageExtent> extentOf(std::uint32_t width, std::uint32_t height,
                                                  std::uint32_t channels) noexcept
{
    const EngineLimits admitting{.maxImageWidth = width,
                                 .maxImageHeight = height,
                                 .maxChannelCount = channels,
                                 .maxImageBytes = (std::numeric_limits<std::uint64_t>::max)(),
                                 .memoryBudgetBytes = (std::numeric_limits<std::uint64_t>::max)()};
    return ImageExtent::create(width, height, channels, admitting);
}

/// How many presented samples a dimension of `size` samples subsampled by `factor` holds.
[[nodiscard]] constexpr std::uint32_t subsampledSize(std::uint32_t size, std::uint32_t factor) noexcept
{
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(size) + factor - 1u) / factor);
}

/// The layout request of what a presentation writes.
[[nodiscard]] Result<ImageLayoutRequest> outputLayoutRequest(const DisplayPresentationRequest& request,
                                                             const SourceShape& shape, std::uint32_t factor,
                                                             const char* operation)
{
    const std::uint32_t width = subsampledSize(shape.width, factor);
    const std::uint32_t height = subsampledSize(shape.height, factor);
    std::uint32_t channels = shape.channels;
    std::uint32_t elementBytes = 1u;
    std::uint32_t samplesPerPixel = shape.channels;
    std::uint32_t planes = 1u;
    std::uint64_t alignment = kByteOutputRowAlignment;
    if (request.output == PresentationOutput::Rgba8 || request.output == PresentationOutput::Bgra8)
    {
        channels = 4u;
        samplesPerPixel = 4u;
    }
    else if (request.output == PresentationOutput::WorkingFloat32)
    {
        elementBytes = kWorkingSampleBytes;
        samplesPerPixel = 1u;
        planes = shape.channels;
        alignment = kDefaultBufferAlignmentBytes;
    }
    const std::optional<ImageExtent> extent = extentOf(width, height, channels);
    if (!extent.has_value())
    {
        return presentationFailure(ErrorCode::InvalidArgument, "the presented extent cannot be described", operation);
    }
    return ImageLayoutRequest{.extent = *extent,
                              .elementSizeBytes = elementBytes,
                              .samplesPerPixel = samplesPerPixel,
                              .planeCount = planes,
                              .rowAlignmentBytes = alignment};
}

[[nodiscard]] bool alignedFor(const void* samples, std::uint32_t elementBytes) noexcept
{
    return reinterpret_cast<std::uintptr_t>(samples) % elementBytes == 0u;
}

/// Checks the source a presentation reads and says what it is.
[[nodiscard]] Result<SourceShape> checkSource(const DisplayPresentationSource& source, const char* operation)
{
    if (source.descriptor == nullptr || source.samples == nullptr || source.layout == nullptr)
    {
        return presentationFailure(ErrorCode::InvalidArgument, "a presentation source has a null member", operation);
    }
    const ImageDescriptor& descriptor = *source.descriptor;
    const ImageLayout& layout = *source.layout;
    const std::optional<SourceShape> shape = shapeOf(descriptor);
    if (!shape.has_value())
    {
        return presentationFailure(ErrorCode::IncompatibleArguments,
                                   "a presentation shows grey, RGB or RGBA; convert another schema first", operation);
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(descriptor.sampleType());
    if (!(layout.extent() == descriptor.extent()) || layout.elementSizeBytes() != elementBytes ||
        layout.planeCount() * layout.samplesPerPixel() != shape->channels || !alignedFor(source.samples, elementBytes))
    {
        return presentationFailure(ErrorCode::InvalidArgument,
                                   "the presentation source's layout or pointer does not describe its descriptor",
                                   operation);
    }
    const bool floating =
        descriptor.sampleType() == SampleType::Float32 || descriptor.sampleType() == SampleType::Float64;
    if (floating && (descriptor.numericDomain() == NumericDomain::RawSensorCode ||
                     descriptor.numericDomain() == NumericDomain::PhysicalScaled))
    {
        return presentationFailure(ErrorCode::IncompatibleArguments,
                                   "a floating sensor code or physical quantity has no full scale to present; rescale "
                                   "it to a normalized domain first",
                                   operation);
    }
    const KernelValidityView& validity = source.validity;
    if (!validity.descriptor.needsMask())
    {
        if (validity.mask != nullptr || validity.maskPlane != 0u)
        {
            return presentationFailure(ErrorCode::InvalidArgument, "the validity view supplies an unused mask",
                                       operation);
        }
        return *shape;
    }
    if (validity.mask == nullptr)
    {
        return presentationFailure(ErrorCode::InvalidArgument, "an explicit-mask source supplies no mask", operation);
    }
    const bool perChannel = validity.mask->planeCount() == shape->channels && shape->channels > 1u;
    if (validity.mask->width() != shape->width || validity.mask->height() != shape->height ||
        (!perChannel && validity.maskPlane >= validity.mask->planeCount()))
    {
        return presentationFailure(ErrorCode::IncompatibleArguments, "the validity mask does not cover the source",
                                   operation);
    }
    return *shape;
}

/// Checks the destination against what the presentation writes.
[[nodiscard]] Status checkDestination(const DisplayPresentationSource& source, const ImageLayoutRequest& expected,
                                      const void* destination, const ImageLayout& layout, const char* operation)
{
    if (destination == nullptr || !alignedFor(destination, expected.elementSizeBytes) ||
        layout.elementSizeBytes() != expected.elementSizeBytes)
    {
        return presentationFailure(ErrorCode::InvalidArgument,
                                   "the presentation destination is null or does not describe the output's samples",
                                   operation);
    }
    if (!(layout.extent() == expected.extent) || layout.samplesPerPixel() != expected.samplesPerPixel ||
        layout.planeCount() != expected.planeCount)
    {
        return presentationFailure(ErrorCode::IncompatibleArguments,
                                   "the presentation destination has another extent or arrangement than the output",
                                   operation);
    }
    if (regionsOverlap(static_cast<const std::uint8_t*>(source.samples), source.layout->totalBytes(),
                       static_cast<const std::uint8_t*>(destination), layout.totalBytes()))
    {
        return presentationFailure(ErrorCode::IncompatibleArguments, "the presentation destination overlaps its source",
                                   operation);
    }
    if (source.validity.mask != nullptr &&
        regionsOverlap(source.validity.mask->data(), source.validity.mask->spannedByteCount(),
                       static_cast<const std::uint8_t*>(destination), layout.totalBytes()))
    {
        return presentationFailure(ErrorCode::InvalidArgument,
                                   "the validity mask overlaps the presentation destination", operation);
    }
    return Status::success();
}

[[nodiscard]] bool finiteValues(std::span<const double> values) noexcept
{
    return std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); });
}

/// Checks a caller's analysis against the plan and the source it is to present.
[[nodiscard]] Status checkAnalysis(const DisplayPresentationAnalysis& analysis,
                                   const DisplayPresentationRequest& request, std::uint32_t colorChannels,
                                   bool production, const char* operation)
{
    if (analysis.colorChannels != colorChannels || analysis.promotion != request.promotion ||
        analysis.neutralized != request.neutralize || analysis.calibrated != request.calibrate)
    {
        return presentationFailure(ErrorCode::IncompatibleArguments,
                                   "the analysis was taken under another promotion, balance or channel count",
                                   operation);
    }
    if (production && analysis.realtimePreview)
    {
        return presentationFailure(ErrorCode::IncompatibleArguments,
                                   "a real-time preview's analysis cannot reach a production presentation", operation);
    }
    if (request.stretch == PresentationStretch::Automatic && !analysis.measured)
    {
        return presentationFailure(ErrorCode::IncompatibleArguments,
                                   "an automatic stretch needs an analysis that measured its statistics", operation);
    }
    const bool gainsPositive = std::all_of(analysis.calibrationGains.begin(), analysis.calibrationGains.end(),
                                           [](double gain) { return std::isfinite(gain) && gain > 0.0; });
    const bool deviationsValid =
        std::all_of(analysis.medianAbsoluteDeviations.begin(), analysis.medianAbsoluteDeviations.end(),
                    [](double deviation) { return std::isfinite(deviation) && deviation >= 0.0; });
    if (!finiteValues(analysis.neutralizationOffsets) || !finiteValues(analysis.calibrationOffsets) ||
        !finiteValues(analysis.medians) || !gainsPositive || !deviationsValid)
    {
        return presentationFailure(ErrorCode::InvalidArgument, "the analysis holds a value outside its range",
                                   operation);
    }
    return Status::success();
}

/// One block of working memory from the execution's engine context, returned on every exit path.
class WorkingBlock
{
public:
    WorkingBlock() noexcept = default;
    WorkingBlock(const WorkingBlock&) = delete;
    WorkingBlock& operator=(const WorkingBlock&) = delete;

    ~WorkingBlock()
    {
        if (m_pointer != nullptr)
        {
            m_memory->deallocate(m_pointer, m_bytes, kDefaultBufferAlignmentBytes);
        }
    }

    /// Takes `bytes` aligned for the widest vector from the execution's memory resource.
    [[nodiscard]] Status take(const ExecutionContext& execution, std::uint64_t bytes, const char* operation)
    {
        MemoryResource* memory = execution.memoryResource();
        if (memory == nullptr)
        {
            return presentationFailure(ErrorCode::NotInitialized, "the execution has no memory resource", operation);
        }
        Result<void*> allocation = memory->allocate(bytes, kDefaultBufferAlignmentBytes);
        if (!allocation.hasValue())
        {
            return allocation.status();
        }
        m_memory = memory;
        m_pointer = *allocation;
        m_bytes = bytes;
        return Status::success();
    }

    [[nodiscard]] std::uint8_t* bytes() const noexcept
    {
        return static_cast<std::uint8_t*>(m_pointer);
    }

private:
    MemoryResource* m_memory = nullptr;
    void* m_pointer = nullptr;
    std::uint64_t m_bytes = 0u;
};

/// The planar Float32 working image and the views of it the composed operations take.
struct WorkingImage
{
    /// The first byte of the first plane.
    std::uint8_t* samples = nullptr;

    /// Every plane, alpha included.
    std::optional<ImageLayout> layout;

    /// The colour planes alone, over the same strides.
    std::optional<ImageLayout> colorLayout;

    /// One plane, over the same strides.
    std::optional<ImageLayout> planeLayout;

    /// The rule the working samples follow: all valid while none is marked, finite-only once one is.
    ValidityDescriptor validity{};

    /// The address of one plane.
    [[nodiscard]] std::uint8_t* plane(std::uint32_t channel) const noexcept
    {
        return samples + static_cast<std::uint64_t>(channel) * layout->planeStrideBytes();
    }

    /// One row of one plane.
    [[nodiscard]] float* row(std::uint32_t channel, std::uint32_t y) const noexcept
    {
        return reinterpret_cast<float*>(plane(channel) + static_cast<std::uint64_t>(y) * layout->rowStrideBytes());
    }
};

/// Views of the working image with fewer planes over the same strides.
[[nodiscard]] Status describeWorkingViews(WorkingImage& working, std::uint32_t colorChannels,
                                          const EngineLimits& limits, const char* operation)
{
    const ImageLayout& whole = *working.layout;
    const auto restricted = [&](std::uint32_t planes) -> std::optional<ImageLayout> {
        const std::optional<ImageExtent> extent = extentOf(whole.extent().width(), whole.extent().height(), planes);
        if (!extent.has_value())
        {
            return std::nullopt;
        }
        return ImageLayout::createWithStrides(ImageLayoutRequest{.extent = *extent,
                                                                 .elementSizeBytes = kWorkingSampleBytes,
                                                                 .samplesPerPixel = 1u,
                                                                 .planeCount = planes,
                                                                 .rowAlignmentBytes = kWorkingSampleBytes},
                                              whole.rowStrideBytes(), whole.planeStrideBytes(), limits);
    };
    working.colorLayout = restricted(colorChannels);
    working.planeLayout = restricted(1u);
    if (!working.colorLayout.has_value() || !working.planeLayout.has_value())
    {
        return presentationFailure(ErrorCode::InvalidArgument, "the working image's planes cannot be described",
                                   operation);
    }
    return Status::success();
}

[[nodiscard]] double roundHalfEven(double value) noexcept
{
    return (value + kRoundingShift) - kRoundingShift;
}

[[nodiscard]] float quantize16(float value) noexcept
{
    const double clamped = std::clamp(static_cast<double>(value), 0.0, 1.0);
    return static_cast<float>(roundHalfEven(clamped * kQuantizedLevels) / kQuantizedLevels);
}

/// A working sample as a byte: clamped to the unit interval, scaled by 255 and rounded to nearest with
/// ties to even, with not-a-number as zero - the engine's conversion of Float32 to UInt8.
[[nodiscard]] std::uint8_t toByte(float value) noexcept
{
    if (!(value > 0.0f))
    {
        return 0u;
    }
    if (value >= 1.0f)
    {
        return 255u;
    }
    return static_cast<std::uint8_t>(roundHalfEven(static_cast<double>(value) * 255.0));
}

/// `toByte` for a working sample already known to be a number within the unit interval, which needs neither guard.
[[nodiscard]] std::uint8_t unitToByte(float value) noexcept
{
    return static_cast<std::uint8_t>(static_cast<std::int32_t>(roundHalfEven(static_cast<double>(value) * 255.0)));
}

/// The largest finite Float32 value: a sample whose magnitude exceeds it is not finite.
constexpr float kLargestFloat = (std::numeric_limits<float>::max)();

/// The quiet NaN that marks an invalid working sample.
constexpr float kQuietNaN = std::numeric_limits<float>::quiet_NaN();

/// Pixels of a row converted to bytes at a time, one channel after another, before they are interleaved.
constexpr std::uint32_t kByteChunk = 512u;

// The shared row body of the eight- and sixteen-bit codes: the scalar authority in this translation unit.
#include "kernels/reference/PresentationRow.inl"

/// Whether a representation is an integer of eight or sixteen bits.
template <typename SampleT> inline constexpr bool kNarrowInteger = std::is_integral_v<SampleT> && sizeof(SampleT) <= 2u;

/// The byte an integer sample of eight or sixteen bits presents linearly as: its fraction of full scale, held to
/// the unit interval and rounded to nearest - what promoting it to Float32 and converting that to a byte gives, in
/// integer arithmetic. Every one of these maxima is odd, so no quotient is a tie and rounding half up is rounding half
/// to even.
template <typename SampleT>
    requires kNarrowInteger<SampleT>
[[nodiscard]] constexpr std::uint8_t narrowCodeToByte(SampleT raw) noexcept
{
    if constexpr (std::is_same_v<SampleT, std::uint8_t> || std::is_same_v<SampleT, std::uint16_t>)
    {
        return presentedByteOfCode(raw);
    }
    else
    {
        constexpr std::uint32_t kMaximum = (std::numeric_limits<SampleT>::max)();
        if constexpr (std::is_signed_v<SampleT>)
        {
            if (raw <= 0)
            {
                return 0u;
            }
        }
        return static_cast<std::uint8_t>((static_cast<std::uint32_t>(raw) * 255u + kMaximum / 2u) / kMaximum);
    }
}

[[nodiscard]] bool maskBit(const ValidityMaskView<const std::uint8_t>& mask, std::uint32_t plane, std::uint32_t x,
                           std::uint32_t y) noexcept
{
    const std::uint64_t offset = static_cast<std::uint64_t>(plane) * mask.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * mask.rowStrideBytes() + x / 8u;
    return (mask.data()[offset] & static_cast<std::uint8_t>(1u << (x % 8u))) != 0u;
}

/// The presented samples of one source row, read through the source's validity and promoted.
///
/// Every n-th sample of every n-th row, one channel at a time: a sample the source's rule, its mask or its value
/// rejects is a quiet NaN, and every other one is converted as the engine converts a sample to Float32 at full
/// scale, held to the unit interval, and held to sixteen-bit levels when the promotion asks.
template <ResidentSample SampleT> class SourceRow
{
public:
    /// A reader over one source.
    SourceRow(const DisplayPresentationSource& source, const SourceShape& shape, std::uint32_t factor,
              PresentationPromotion promotion) noexcept
        : m_source(source), m_channels(shape.channels), m_factor(factor),
          m_samplesPerPixel(source.layout->samplesPerPixel()),
          m_step(static_cast<std::uint64_t>(factor) * source.layout->samplesPerPixel()),
          m_perChannelMask(source.validity.mask != nullptr && source.validity.mask->planeCount() == shape.channels &&
                           shape.channels > 1u),
          m_ruled(source.validity.descriptor.rule() != ValidityRule::AllValid || source.validity.mask != nullptr),
          m_quantized(promotion == PresentationPromotion::Quantized16)
    {
    }

    /// Points the reader at the source row output row `outputY` presents.
    void select(std::uint32_t outputY) noexcept
    {
        const ImageLayout& layout = *m_source.layout;
        const auto* base = static_cast<const std::uint8_t*>(m_source.samples);
        m_y = outputY * m_factor;
        for (std::uint32_t channel = 0u; channel < m_channels; ++channel)
        {
            const std::uint32_t plane = channel / m_samplesPerPixel;
            m_rows[channel] =
                reinterpret_cast<const SampleT*>(base + static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() +
                                                 static_cast<std::uint64_t>(m_y) * layout.rowStrideBytes()) +
                channel % m_samplesPerPixel;
        }
    }

    /// The working sample of one channel at output column `x` of the selected row.
    [[nodiscard]] float sample(std::uint32_t channel, std::uint32_t x) const noexcept
    {
        const SampleT raw = m_rows[channel][static_cast<std::uint64_t>(x) * m_step];
        bool valid = true;
        if (m_ruled)
        {
            const KernelValidityView& validity = m_source.validity;
            valid = sampleIsValidByValue(validity.descriptor, raw) &&
                    (validity.mask == nullptr ||
                     maskBit(*validity.mask, m_perChannelMask ? channel : validity.maskPlane, x * m_factor, m_y));
        }
        if (!valid)
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return promoteValue(raw);
    }

    /// A sample no rule or mask rejects, promoted: a quiet NaN when it is not finite.
    [[nodiscard]] float promoteValue(SampleT raw) const noexcept
    {
        if constexpr (std::is_floating_point_v<SampleT>)
        {
            if (!sampleIsFinite(raw))
            {
                return std::numeric_limits<float>::quiet_NaN();
            }
        }
        float value = 0.0f;
        if constexpr (std::is_same_v<SampleT, float>)
        {
            value = raw;
        }
        else
        {
            value = convertSample<SampleT, float>(raw, ConversionPolicy{}).value;
        }
        // Held to what a display can show, so a statistic of a sample below black is one of black.
        value = std::clamp(value, 0.0f, 1.0f);
        return m_quantized ? quantize16(value) : value;
    }

    /// Whether a rule or a mask can reject a sample, so that only `sample` may read one.
    [[nodiscard]] bool ruled() const noexcept
    {
        return m_ruled;
    }

    /// Whether samples are held to sixteen-bit levels.
    [[nodiscard]] bool quantized() const noexcept
    {
        return m_quantized;
    }

    /// The first sample of one channel in the selected row.
    [[nodiscard]] const SampleT* row(std::uint32_t channel) const noexcept
    {
        return m_rows[channel];
    }

    /// Elements between two presented samples of one channel in a row.
    [[nodiscard]] std::uint64_t step() const noexcept
    {
        return m_step;
    }

    /// Elements a row holds from one channel's first sample on, which is as far as a read of that channel may reach.
    [[nodiscard]] std::uint64_t rowSamples(std::uint32_t channel) const noexcept
    {
        return static_cast<std::uint64_t>(m_source.layout->extent().width()) * m_samplesPerPixel -
               channel % m_samplesPerPixel;
    }

private:
    /// The source read.
    const DisplayPresentationSource& m_source;

    /// Its channels, alpha included.
    std::uint32_t m_channels;

    /// Every this-many-th sample and row is presented.
    std::uint32_t m_factor;

    /// Samples one pixel of a plane holds.
    std::uint32_t m_samplesPerPixel;

    /// Elements between two presented samples of one channel in a row.
    std::uint64_t m_step;

    /// Whether the mask has a plane per channel.
    bool m_perChannelMask;

    /// Whether any sample can be invalid by rule or mask.
    bool m_ruled;

    /// Whether samples are held to sixteen-bit levels.
    bool m_quantized;

    /// The source row selected.
    std::uint32_t m_y = 0u;

    /// Each channel's first sample in the selected row.
    std::array<const SampleT*, 4u> m_rows{};
};

/// Reads one presented row of every channel, marking each invalid sample.
///
/// @param reader the source's reader, which this points at the row.
/// @param shape the source's shape.
/// @param outputs where each channel's promoted row goes.
/// @param width the working image's width.
/// @param outputY the row.
/// @return how many of the row's samples it marked invalid.
template <ResidentSample SampleT>
[[nodiscard]] std::uint64_t promoteRow(SourceRow<SampleT>& reader, const SourceShape& shape,
                                       const std::array<float*, 4u>& outputs, std::uint32_t width,
                                       std::uint32_t outputY) noexcept
{
    std::uint64_t invalid = 0u;
    reader.select(outputY);
    for (std::uint32_t channel = 0u; channel < shape.channels; ++channel)
    {
        float* output = outputs[channel];
        if (reader.ruled())
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const float value = reader.sample(channel, x);
                output[x] = value;
                invalid += std::isnan(value) ? 1u : 0u;
            }
            continue;
        }
        const SampleT* input = reader.row(channel);
        const std::uint64_t step = reader.step();
        if (step == 1u)
        {
            bool copied = false;
            if constexpr (std::is_same_v<SampleT, float>)
            {
                if (!reader.quantized())
                {
                    // The promotion of a Float32 sample without its branches, so the row vectorizes: held to the unit
                    // interval, and a quiet NaN when it is not finite.
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        const float value = input[x];
                        const float held = (std::min)((std::max)(value, 0.0f), 1.0f);
                        output[x] = std::abs(value) <= kLargestFloat ? held : kQuietNaN;
                    }
                    copied = true;
                }
            }
            if (!copied)
            {
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    output[x] = reader.promoteValue(input[x]);
                }
            }
        }
        else
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                output[x] = reader.promoteValue(input[x * step]);
            }
        }
        if constexpr (std::is_floating_point_v<SampleT>)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                invalid += std::isnan(output[x]) ? 1u : 0u;
            }
        }
    }
    return invalid;
}

/// Reads every presented sample of one representation into the working image, marking each invalid one.
template <ResidentSample SampleT>
[[nodiscard]] Status promoteTyped(const ExecutionContext& execution, const DisplayPresentationSource& source,
                                  const SourceShape& shape, std::uint32_t factor, PresentationPromotion promotion,
                                  const WorkingImage& working, std::atomic<std::uint64_t>& invalidSamples)
{
    const std::uint32_t width = working.layout->extent().width();
    const std::uint32_t height = working.layout->extent().height();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * shape.channels * (sizeof(SampleT) + 4u),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * shape.channels * 4u};
    return parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        SourceRow<SampleT> reader(source, shape, factor, promotion);
        std::uint64_t localInvalid = 0u;
        for (std::uint64_t row = range.begin; row < range.end; ++row)
        {
            const auto outputY = static_cast<std::uint32_t>(row);
            std::array<float*, 4u> outputs{};
            for (std::uint32_t channel = 0u; channel < shape.channels; ++channel)
            {
                outputs[channel] = working.row(channel, outputY);
            }
            localInvalid += promoteRow(reader, shape, outputs, width, outputY);
        }
        invalidSamples.fetch_add(localInvalid, std::memory_order_relaxed);
        return Status::success();
    });
}

/// Promotes every presented sample and stretches each row while it is still in cache, for a presentation whose
/// stretch is the only step after its promotion.
///
/// A row's colour channels are promoted into scratch rows of the worker's own and written into the working image
/// through the channel's transfer, as `stretchImage` writes a row of an all-valid image - with streaming stores when
/// `stream` says the working image is too large to stay in the caches - and an alpha row is promoted where it lies.
/// The rows are the ones a promotion and then the stretch write while every working sample is valid; one invalid
/// sample anywhere decides the stretch for the whole image, so a row that holds one stops the stretching here, and the
/// answer is false: the caller promotes and stretches again as two passes.
///
/// @return whether every working sample was valid, the working image then holding the stretched presentation.
template <ResidentSample SampleT>
[[nodiscard]] Result<bool>
promoteAndStretchTyped(const ExecutionContext& execution, const DisplayPresentationSource& source,
                       const SourceShape& shape, std::uint32_t factor, PresentationPromotion promotion,
                       const WorkingImage& working, const std::array<MidtonesTransferParameters, 3u>& transfer,
                       bool stream)
{
    const std::uint32_t width = working.layout->extent().width();
    const std::uint32_t height = working.layout->extent().height();
    std::array<MidtonesTransferCoefficients, 3u> plans{};
    for (std::uint32_t channel = 0u; channel < shape.colorChannels; ++channel)
    {
        plans[channel] = planScreenTransferChannel(transfer[channel]);
    }
    const MidtonesTransferFloat32RunFn compiled = midtonesTransferFloat32RunFor(execution);
    std::atomic<bool> invalidSeen{false};
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * shape.channels * (sizeof(SampleT) + 4u),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * shape.channels * 12u};
    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        SourceRow<SampleT> reader(source, shape, factor, promotion);
        WorkingBlock scratch;
        const Status taken =
            scratch.take(execution, static_cast<std::uint64_t>(width) * shape.colorChannels * kWorkingSampleBytes,
                         kPresentOperation);
        if (taken.failed())
        {
            return taken;
        }
        float* const promoted = reinterpret_cast<float*>(scratch.bytes());
        for (std::uint64_t row = range.begin; row < range.end; ++row)
        {
            if (invalidSeen.load(std::memory_order_relaxed))
            {
                return Status::success();
            }
            const auto outputY = static_cast<std::uint32_t>(row);
            std::array<float*, 4u> outputs{};
            for (std::uint32_t channel = 0u; channel < shape.channels; ++channel)
            {
                outputs[channel] = channel < shape.colorChannels ? promoted + static_cast<std::size_t>(channel) * width
                                                                 : working.row(channel, outputY);
            }
            if (promoteRow(reader, shape, outputs, width, outputY) != 0u)
            {
                invalidSeen.store(true, std::memory_order_relaxed);
                return Status::success();
            }
            for (std::uint32_t channel = 0u; channel < shape.colorChannels; ++channel)
            {
                transferScreenTransferRow(compiled, plans[channel], outputs[channel], working.row(channel, outputY),
                                          width, stream);
            }
        }
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    return !invalidSeen.load(std::memory_order_relaxed);
}

/// Calls `walk` with a value of the source's representation, which names its type.
template <typename WalkT> [[nodiscard]] Status dispatchSource(SampleType sampleType, WalkT&& walk)
{
    switch (sampleType)
    {
    case SampleType::UInt8:
        return walk(std::uint8_t{});
    case SampleType::Int8:
        return walk(std::int8_t{});
    case SampleType::UInt16:
        return walk(std::uint16_t{});
    case SampleType::Int16:
        return walk(std::int16_t{});
    case SampleType::UInt32:
        return walk(std::uint32_t{});
    case SampleType::Int32:
        return walk(std::int32_t{});
    case SampleType::Float32:
        return walk(float{});
    case SampleType::Float64:
        return walk(double{});
    }
    return presentationFailure(ErrorCode::InvalidArgument, "the presentation source's representation is unknown",
                               kPresentOperation);
}

[[nodiscard]] Status promote(const ExecutionContext& execution, const DisplayPresentationSource& source,
                             const SourceShape& shape, std::uint32_t factor, PresentationPromotion promotion,
                             const WorkingImage& working, std::atomic<std::uint64_t>& invalidSamples)
{
    return dispatchSource(shape.sampleType, [&](auto tag) {
        return promoteTyped<decltype(tag)>(execution, source, shape, factor, promotion, working, invalidSamples);
    });
}

/// `promoteAndStretchTyped` for the source's representation.
[[nodiscard]] Result<bool> promoteAndStretch(const ExecutionContext& execution, const DisplayPresentationSource& source,
                                             const SourceShape& shape, std::uint32_t factor,
                                             PresentationPromotion promotion, const WorkingImage& working,
                                             const std::array<MidtonesTransferParameters, 3u>& transfer, bool stream)
{
    bool stretched = false;
    const Status walked = dispatchSource(shape.sampleType, [&](auto tag) -> Status {
        const Result<bool> answer = promoteAndStretchTyped<decltype(tag)>(execution, source, shape, factor, promotion,
                                                                          working, transfer, stream);
        if (!answer.hasValue())
        {
            return answer.status();
        }
        stretched = *answer;
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    return stretched;
}

[[nodiscard]] ValidityDescriptor rangeRule(const PresentationInterval& interval) noexcept
{
    return ValidityDescriptor::create(ValidityDescriptorRequest{.rule = ValidityRule::DomainRange,
                                                                .lowestValid = interval.low,
                                                                .highestValid = interval.high})
        .value_or(ValidityDescriptor{});
}

[[nodiscard]] ValidityDescriptor finiteRule() noexcept
{
    return ValidityDescriptor::create(ValidityDescriptorRequest{.rule = ValidityRule::FiniteOnly})
        .value_or(ValidityDescriptor{});
}

/// One colour channel's median under a selection of the working image.
[[nodiscard]] Result<double> channelMedian(const ExecutionContext& execution, const WorkingImage& working,
                                           std::uint32_t channel, const ValidityDescriptor& rule)
{
    const Result<RobustStatistics> statistics =
        computeImageRobustStatistics(execution, SampleType::Float32, working.samples, *working.colorLayout, channel,
                                     StatisticsSelection{.validity = rule}, RobustStatisticsRequest{});
    if (!statistics.hasValue())
    {
        return statistics.status();
    }
    return statistics->median;
}

/// Applies gains and offsets to the colour planes, holding every result in the unit interval.
[[nodiscard]] Status applyBalance(const ExecutionContext& execution, const WorkingImage& working,
                                  const ColorChannelGains& gains, const ColorChannelOffsets& offsets,
                                  const char* operation)
{
    const ImageLayout& layout = *working.colorLayout;
    const std::optional<ImageDescriptor> descriptor =
        ImageDescriptor::create(ImageDescriptorRequest{.extent = layout.extent(),
                                                       .sampleType = SampleType::Float32,
                                                       .channelSchema = ChannelSchema::Rgb,
                                                       .colorSpace = ColorSpace::Unspecified,
                                                       .numericDomain = NumericDomain::NormalizedLinear,
                                                       .cfa = {},
                                                       .coordinateOrigin = CoordinateOrigin::TopLeft,
                                                       .pixelAspectRatio = 1.0});
    if (!descriptor.has_value())
    {
        return presentationFailure(ErrorCode::InvalidArgument, "the working image cannot be described as colour",
                                   operation);
    }
    const Result<StarFrameView> frame = StarFrameView::create(working.samples, layout.totalBytes(), layout, *descriptor,
                                                              KernelValidityView{.descriptor = working.validity});
    if (!frame.hasValue())
    {
        return frame.status();
    }
    const Result<ColorCalibrationApplyResult> applied = applyColorCalibration(
        execution, *frame, working.samples, layout.totalBytes(),
        ColorCalibrationApplyRequest{
            .gains = gains, .offsets = offsets, .clipping = ColorCalibrationClipping::ClampToPixelRange});
    return applied.hasValue() ? Status::success() : applied.status();
}

/// Neutralization: each channel's background median inside the interval, moved onto their mean.
[[nodiscard]] Result<ColorChannelOffsets> measureNeutralization(const ExecutionContext& execution,
                                                                const WorkingImage& working,
                                                                const PresentationInterval& background)
{
    ColorChannelOffsets levels{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        const Result<double> level = channelMedian(execution, working, channel, rangeRule(background));
        if (!level.hasValue())
        {
            return level.status();
        }
        levels[channel] = *level;
    }
    return colorCalibrationOffsets({1.0, 1.0, 1.0}, levels, ColorCalibrationBackgroundMode::SharedPedestal,
                                   meanBackgroundPedestal(levels));
}

/// What calibration found: the gains about each background and the offsets that keep it there.
struct CalibrationTerms
{
    ColorChannelGains gains{1.0, 1.0, 1.0};
    ColorChannelOffsets offsets{0.0, 0.0, 0.0};
};

/// Calibration: each channel's white mean and background median, and the gain that brings each
/// channel's white above its background to the strongest channel's.
[[nodiscard]] Result<CalibrationTerms> measureCalibration(const ExecutionContext& execution,
                                                          const WorkingImage& working,
                                                          const DisplayPresentationRequest& request,
                                                          const char* operation)
{
    std::array<double, 3u> aboveBackground{};
    std::array<double, 3u> background{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        const Result<BasicStatistics> white =
            computeImageBasicStatistics(execution, SampleType::Float32, working.samples, *working.colorLayout, channel,
                                        StatisticsSelection{.validity = rangeRule(request.calibrationWhite)});
        if (!white.hasValue())
        {
            return white.status();
        }
        const Result<double> level =
            channelMedian(execution, working, channel, rangeRule(request.calibrationBackground));
        if (!level.hasValue())
        {
            return level.status();
        }
        background[channel] = *level;
        aboveBackground[channel] = std::abs(white->mean - *level);
    }
    const auto strongest = static_cast<std::size_t>(
        std::distance(aboveBackground.begin(), std::max_element(aboveBackground.begin(), aboveBackground.end())));
    CalibrationTerms terms;
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        if (!(aboveBackground[channel] > 0.0))
        {
            return presentationFailure(ErrorCode::InvalidData,
                                       "a channel's white does not stand above its background, so it has no gain",
                                       operation);
        }
        terms.gains[channel] = aboveBackground[strongest] / aboveBackground[channel];
        terms.offsets[channel] = background[channel] * (1.0 - terms.gains[channel]);
    }
    return terms;
}

/// Each colour channel's median and median absolute deviation; a channel with no valid sample has none.
[[nodiscard]] Status measureStatistics(const ExecutionContext& execution, const WorkingImage& working,
                                       std::uint32_t colorChannels, DisplayPresentationAnalysis& analysis)
{
    for (std::uint32_t channel = 0u; channel < colorChannels; ++channel)
    {
        const Result<RobustStatistics> statistics =
            computeImageRobustStatistics(execution, SampleType::Float32, working.samples, *working.colorLayout, channel,
                                         StatisticsSelection{.validity = working.validity},
                                         RobustStatisticsRequest{.medianAbsoluteDeviation = true});
        if (!statistics.hasValue())
        {
            if (statistics.status().code() != ErrorCode::EmptySelection)
            {
                return statistics.status();
            }
            continue;
        }
        analysis.medians[channel] = statistics->median;
        analysis.medianAbsoluteDeviations[channel] = statistics->medianAbsoluteDeviation;
        analysis.measuredSamples[channel] = statistics->selection.valid;
    }
    analysis.measured = true;
    return Status::success();
}

/// Whether an analysis alone measures the source where it lies rather than a promoted working image.
///
/// The gradient promotion of a Float32 sample is the sample itself held to the unit interval, with one that is not
/// finite, or that its rule rejects, marked invalid - which is what the robust statistics read through a finite-only
/// rule, or the source's own range rule with finite ends, and a hold to the unit interval. Another representation is
/// converted as it is promoted, a quantized promotion rounds it, and a mask or the zero rule admits a non-finite
/// sample the promotion marks invalid, so each of those measures the working image.
///
/// @param request the presentation, which balances nothing and stretches automatically for a direct measurement.
/// @param shape the source's shape.
/// @param source the source and its validity.
/// @param factor the subsampling, one for a direct measurement.
/// @return true when the statistics are measured over the source.
[[nodiscard]] bool measuresSourceDirectly(const DisplayPresentationRequest& request, const SourceShape& shape,
                                          const DisplayPresentationSource& source, std::uint32_t factor) noexcept
{
    if (factor != 1u || shape.sampleType != SampleType::Float32 ||
        request.promotion != PresentationPromotion::Gradient || request.stretch != PresentationStretch::Automatic ||
        request.neutralize || request.calibrate || source.validity.mask != nullptr)
    {
        return false;
    }
    const ValidityDescriptor& rule = source.validity.descriptor;
    switch (rule.rule())
    {
    case ValidityRule::AllValid:
    case ValidityRule::FiniteOnly:
        return true;
    case ValidityRule::DomainRange: {
        const std::optional<double> low = rule.lowestValid();
        const std::optional<double> high = rule.highestValid();
        return low.has_value() && high.has_value() && std::isfinite(*low) && std::isfinite(*high);
    }
    default:
        return false;
    }
}

/// Each colour channel's median and median absolute deviation measured over the source where it lies, every
/// admitted sample held to the unit interval as the promotion holds it; a channel with no valid sample has none.
///
/// @param execution the operation.
/// @param source the Float32 source `measuresSourceDirectly` accepted.
/// @param colorChannels how many colour channels to measure.
/// @param analysis where the statistics go.
/// @return success, or the first failure.
[[nodiscard]] Status measureSourceStatistics(const ExecutionContext& execution, const DisplayPresentationSource& source,
                                             std::uint32_t colorChannels, DisplayPresentationAnalysis& analysis)
{
    // A non-finite sample is invalid under the promotion whatever the rule, which the finite-only rule states; a range
    // rule with finite ends excludes it on its own.
    const ValidityDescriptor rule =
        source.validity.descriptor.rule() == ValidityRule::DomainRange ? source.validity.descriptor : finiteRule();
    for (std::uint32_t channel = 0u; channel < colorChannels; ++channel)
    {
        const Result<RobustStatistics> statistics = computeImageRobustStatistics(
            execution, SampleType::Float32, source.samples, *source.layout, channel,
            StatisticsSelection{.validity = rule},
            RobustStatisticsRequest{.medianAbsoluteDeviation = true,
                                    .hold = KernelValueWindow{.lowest = 0.0, .highest = 1.0}});
        if (!statistics.hasValue())
        {
            if (statistics.status().code() != ErrorCode::EmptySelection)
            {
                return statistics.status();
            }
            continue;
        }
        analysis.medians[channel] = statistics->median;
        analysis.medianAbsoluteDeviations[channel] = statistics->medianAbsoluteDeviation;
        analysis.measuredSamples[channel] = statistics->selection.valid;
    }
    analysis.measured = true;
    return Status::success();
}

/// Whether a presentation measures its source from how often each of its codes occurs rather than from a working image.
///
/// The promotion of an eight- or sixteen-bit sample - gradient or quantized - is a function of its code alone, and so
/// is whether the source's rule admits it, so its medians and deviations are those of a table of at most 65,536 values
/// weighted by how often each code occurs among the samples the rule and the mask admit. That is the whole source
/// read, so a subsampled preview measures its working image instead, and so does a balance, which measures the
/// working image after it has changed it.
///
/// @param request the presentation, which balances nothing and stretches automatically for a measurement from codes.
/// @param shape the source's shape.
/// @param factor the subsampling, one for a measurement from codes.
/// @return true when the statistics are measured from the codes.
[[nodiscard]] bool measuresFromCodes(const DisplayPresentationRequest& request, const SourceShape& shape,
                                     std::uint32_t factor) noexcept
{
    const bool narrow = shape.sampleType == SampleType::UInt8 || shape.sampleType == SampleType::Int8 ||
                        shape.sampleType == SampleType::UInt16 || shape.sampleType == SampleType::Int16;
    return narrow && factor == 1u && request.stretch == PresentationStretch::Automatic && !request.neutralize &&
           !request.calibrate;
}

/// One colour channel's median and median absolute deviation from how often each of its codes occurs; a channel with
/// no admitted sample has none.
///
/// The codes are counted in one read of the channel, through the source's rule and its mask, one bin per code. Each
/// code is then promoted once, as a promotion reads a sample of it, and since a promotion never decreases as the code
/// rises, the running counts in code order place every rank: the median is the selection's own interpolation between
/// the two ranks about the middle, and the deviations from it are met in ascending order by a walk outward from it -
/// the promoted values below it fall as the walk moves down and those above it rise as it moves up, so the nearer of
/// the two fronts is always the next. Each deviation is the selection's own difference of a promoted value and the
/// median, so the figures are those a selection over the promoted working image gives, bit for bit.
///
/// @tparam SampleT an eight- or sixteen-bit integer representation.
/// @param execution the operation.
/// @param source the source, of that representation.
/// @param shape its shape.
/// @param promotion how a sample becomes a working sample.
/// @param channel the colour channel to measure.
/// @param counts `2^bits` counters for the channel's codes, overwritten.
/// @param analysis where the channel's statistics go.
/// @return success, or the count's failure.
template <ResidentSample SampleT>
[[nodiscard]] Status measureChannelFromCodes(const ExecutionContext& execution, const DisplayPresentationSource& source,
                                             const SourceShape& shape, PresentationPromotion promotion,
                                             std::uint32_t channel, std::uint64_t* counts,
                                             DisplayPresentationAnalysis& analysis)
{
    constexpr std::uint64_t kCodes = std::uint64_t{1} << (8u * sizeof(SampleT));
    constexpr std::int64_t kLowestCode = static_cast<std::int64_t>((std::numeric_limits<SampleT>::min)());
    const ImageLayout& layout = *source.layout;
    const std::uint32_t samplesPerPixel = layout.samplesPerPixel();
    const bool perChannelMask =
        source.validity.mask != nullptr && source.validity.mask->planeCount() == shape.channels && shape.channels > 1u;
    // Over the representation's full scale, one bin a code: the bins are the codes in ascending order.
    const Result<PlaneHistogramResult> counted =
        accumulatePlaneHistogram(execution,
                                 KernelPlaneSource{.sampleType = shape.sampleType,
                                                   .samples = source.samples,
                                                   .layout = &layout,
                                                   .plane = channel / samplesPerPixel,
                                                   .sampleInPixel = channel % samplesPerPixel},
                                 std::span<std::uint64_t>{counts, static_cast<std::size_t>(kCodes)},
                                 PlaneHistogramRequest{.domain = HistogramDomain::RepresentationFullScale},
                                 KernelValidityView{.descriptor = source.validity.descriptor,
                                                    .mask = source.validity.mask,
                                                    .maskPlane = perChannelMask ? channel : source.validity.maskPlane});
    if (!counted.hasValue())
    {
        return counted.status();
    }
    // Each code's count becomes the running count of the admitted samples up to and including it.
    std::uint64_t count = 0u;
    for (std::uint64_t code = 0u; code < kCodes; ++code)
    {
        count += counts[code];
        counts[code] = count;
    }
    if (count == 0u)
    {
        return Status::success();
    }
    const SourceRow<SampleT> reader(source, shape, 1u, promotion);
    const auto valueOf = [&](std::uint64_t code) {
        return static_cast<double>(
            reader.promoteValue(static_cast<SampleT>(static_cast<std::int64_t>(code) + kLowestCode)));
    };
    const auto countOf = [&](std::uint64_t code) { return counts[code] - (code == 0u ? 0u : counts[code - 1u]); };
    const auto valueAtRank = [&](std::uint64_t rank) {
        return valueOf(static_cast<std::uint64_t>(std::upper_bound(counts, counts + kCodes, rank) - counts));
    };
    // The position and the interpolation the selection gives the quantile at one half.
    const double position = 0.5 * static_cast<double>(count - 1u);
    const double floored = std::floor(position);
    const auto lower = static_cast<std::uint64_t>(floored);
    const double fraction = position - floored;
    const auto halfOf = [&](const auto& rankValue) {
        const double first = rankValue(lower);
        if (fraction == 0.0)
        {
            return first;
        }
        return first + fraction * (rankValue(lower + 1u) - first);
    };
    const double median = halfOf(valueAtRank);

    // The first code whose promoted value lies above the median: every code before it holds a value at or below it.
    std::uint64_t first = 0u;
    std::uint64_t past = kCodes;
    while (first < past)
    {
        const std::uint64_t middle = first + (past - first) / 2u;
        if (valueOf(middle) > median)
        {
            past = middle;
        }
        else
        {
            first = middle + 1u;
        }
    }
    std::uint64_t upward = first;
    std::uint64_t downward = first;
    std::uint64_t passed = 0u;
    double front = 0.0;
    const auto deviationAtRank = [&](std::uint64_t rank) {
        while (passed <= rank)
        {
            const double below =
                downward > 0u ? std::abs(valueOf(downward - 1u) - median) : std::numeric_limits<double>::infinity();
            const double above =
                upward < kCodes ? std::abs(valueOf(upward) - median) : std::numeric_limits<double>::infinity();
            if (below <= above)
            {
                --downward;
                passed += countOf(downward);
                front = below;
            }
            else
            {
                passed += countOf(upward);
                ++upward;
                front = above;
            }
        }
        return front;
    };
    analysis.medians[channel] = median;
    analysis.medianAbsoluteDeviations[channel] = halfOf(deviationAtRank);
    analysis.measuredSamples[channel] = count;
    return Status::success();
}

/// Each colour channel's median and median absolute deviation from how often each code of an eight- or sixteen-bit
/// source occurs, through a table of counters taken once from the execution's memory.
///
/// @param execution the operation.
/// @param source the source `measuresFromCodes` accepted.
/// @param shape its shape.
/// @param promotion how a sample becomes a working sample.
/// @param analysis where the statistics go.
/// @param operation the entry point a failure belongs to.
/// @return success, or the first failure.
[[nodiscard]] Status measureStatisticsFromCodes(const ExecutionContext& execution,
                                                const DisplayPresentationSource& source, const SourceShape& shape,
                                                PresentationPromotion promotion, DisplayPresentationAnalysis& analysis,
                                                const char* operation)
{
    const Status measured = dispatchSource(shape.sampleType, [&](auto tag) -> Status {
        using SampleT = decltype(tag);
        if constexpr (kNarrowInteger<SampleT>)
        {
            constexpr std::uint64_t kCodes = std::uint64_t{1} << (8u * sizeof(SampleT));
            WorkingBlock counts;
            const Status taken = counts.take(execution, kCodes * sizeof(std::uint64_t), operation);
            if (taken.failed())
            {
                return taken;
            }
            for (std::uint32_t channel = 0u; channel < shape.colorChannels; ++channel)
            {
                const Status channelMeasured =
                    measureChannelFromCodes<SampleT>(execution, source, shape, promotion, channel,
                                                     reinterpret_cast<std::uint64_t*>(counts.bytes()), analysis);
                if (channelMeasured.failed())
                {
                    return channelMeasured;
                }
            }
            return Status::success();
        }
        else
        {
            return presentationFailure(ErrorCode::Internal, "only an eight- or sixteen-bit source has codes to count",
                                       operation);
        }
    });
    if (measured.failed())
    {
        return measured;
    }
    analysis.measured = true;
    return Status::success();
}

/// The transfer each colour channel takes, from the analysis's statistics.
[[nodiscard]] Result<std::array<MidtonesTransferParameters, 3u>>
deriveTransfer(const DisplayPresentationRequest& request, const DisplayPresentationAnalysis& analysis)
{
    std::array<MidtonesTransferParameters, 3u> transfer{};
    if (request.stretch == PresentationStretch::Linear)
    {
        return transfer;
    }
    std::array<StretchChannelStatistics, 3u> statistics{};
    std::array<std::uint32_t, 3u> channelOf{};
    std::size_t measured = 0u;
    for (std::uint32_t channel = 0u; channel < analysis.colorChannels; ++channel)
    {
        if (analysis.measuredSamples[channel] == 0u)
        {
            continue;
        }
        statistics[measured] = StretchChannelStatistics{
            .median = analysis.medians[channel], .medianAbsoluteDeviation = analysis.medianAbsoluteDeviations[channel]};
        channelOf[measured] = channel;
        ++measured;
    }
    if (measured == 0u)
    {
        return transfer;
    }
    const std::span<const StretchChannelStatistics> taken(statistics.data(), measured);
    if (request.linked && analysis.colorChannels > 1u)
    {
        const Result<MidtonesTransferParameters> linked =
            deriveLinkedAutoStretch(taken, request.shadowsClipping, request.targetBackground);
        if (!linked.hasValue())
        {
            return linked.status();
        }
        transfer.fill(*linked);
        return transfer;
    }
    std::array<MidtonesTransferParameters, 3u> derived{};
    const Status unlinked = deriveUnlinkedAutoStretch(taken, request.shadowsClipping, request.targetBackground,
                                                      std::span<MidtonesTransferParameters>(derived.data(), measured));
    if (unlinked.failed())
    {
        return unlinked;
    }
    for (std::size_t index = 0u; index < measured; ++index)
    {
        transfer[channelOf[index]] = derived[index];
    }
    return transfer;
}

/// The address and one-plane layout of a colour plane, as the composed operations take an image.
[[nodiscard]] PointwiseMutableImageView planeView(const WorkingImage& working, std::uint32_t channel) noexcept
{
    return PointwiseMutableImageView{.samples = working.plane(channel), .layout = &*working.planeLayout};
}

/// Applies the transfer to the colour planes: all at once while every sample is valid, one plane at a time
/// otherwise, because the stretch leaves a pixel with any invalid channel untouched.
[[nodiscard]] Status applyTransfer(const ExecutionContext& execution, const WorkingImage& working,
                                   std::uint32_t colorChannels,
                                   const std::array<MidtonesTransferParameters, 3u>& transfer)
{
    StretchImageRequest stretch{};
    stretch.stretch.function = StretchFunction::ScreenTransfer;
    stretch.stretch.screenTransfer = transfer;
    stretch.colorMode = StretchColorMode::Independent;
    stretch.channelEnabled = {true, true, true, false};
    if (working.validity.rule() == ValidityRule::AllValid)
    {
        const Result<StretchImageResult> stretched =
            stretchImage(execution, SampleType::Float32, working.samples, *working.colorLayout, working.samples,
                         *working.colorLayout, stretch, KernelValidityView{});
        return stretched.hasValue() ? Status::success() : stretched.status();
    }
    for (std::uint32_t channel = 0u; channel < colorChannels; ++channel)
    {
        StretchImageRequest single = stretch;
        single.stretch.screenTransfer = {transfer[channel], transfer[channel], transfer[channel]};
        const PointwiseMutableImageView plane = planeView(working, channel);
        const Result<StretchImageResult> stretched =
            stretchImage(execution, SampleType::Float32, plane.samples, *plane.layout, plane.samples, *plane.layout,
                         single, KernelValidityView{.descriptor = working.validity});
        if (!stretched.hasValue())
        {
            return stretched.status();
        }
    }
    return Status::success();
}

/// The saturation step; returns the threshold it used, or zero when there was nothing to measure.
[[nodiscard]] Result<double> applySaturation(const ExecutionContext& execution, const WorkingImage& working,
                                             const DisplayPresentationRequest& request)
{
    const Result<double> derived = deriveSaturationEnhancementBackground(
        execution, SampleType::Float32, working.samples, *working.colorLayout, request.saturationBackgroundFactor,
        request.saturationBackgroundLocation, StatisticsSelection{.validity = working.validity});
    if (!derived.hasValue())
    {
        if (derived.status().code() == ErrorCode::EmptySelection)
        {
            return 0.0;
        }
        return derived.status();
    }
    const double background = std::clamp(*derived, 0.0, 1.0);
    const Result<ColorAdjustmentResult> enhanced = enhanceImageSaturation(
        execution, SampleType::Float32, working.samples, *working.colorLayout, working.samples, *working.colorLayout,
        SaturationEnhancementParameters{.coefficient = request.saturation,
                                        .background = background,
                                        .hueMinimumDegrees = 0.0,
                                        .hueMaximumDegrees = 360.0},
        KernelValidityView{.descriptor = working.validity});
    if (!enhanced.hasValue())
    {
        return enhanced.status();
    }
    return background;
}

/// Brightness then contrast, each followed by a clamp to the unit interval, one colour plane at a time.
[[nodiscard]] Status applyBrightnessContrast(const ExecutionContext& execution, const WorkingImage& working,
                                             std::uint32_t colorChannels, const DisplayPresentationRequest& request)
{
    const PointwisePlanShape shape{};
    const ClampInterval unit{.low = 0.0, .high = 1.0};
    std::vector<PointwiseProgram> steps;
    const auto append = [&steps](Result<PointwiseProgram> step) -> Status {
        if (!step.hasValue())
        {
            return step.status();
        }
        steps.push_back(std::move(*step));
        return Status::success();
    };
    Status built = append(buildBrightnessProgram(shape, request.brightness));
    built = built.failed() ? built : append(buildClampProgram(shape, unit));
    built = built.failed() ? built : append(buildContrastProgram(shape, request.contrast, 0.5));
    built = built.failed() ? built : append(buildClampProgram(shape, unit));
    if (built.failed())
    {
        return built;
    }
    const Result<PointwiseProgram> program = fusePointwisePrograms(steps);
    if (!program.hasValue())
    {
        return program.status();
    }
    for (std::uint32_t channel = 0u; channel < colorChannels; ++channel)
    {
        const PointwiseMutableImageView plane = planeView(working, channel);
        const std::array<PointwiseImageView, 1u> sources{
            PointwiseImageView{.samples = plane.samples, .layout = plane.layout}};
        const Result<PointwiseExecutionResult> executed = executePointwiseProgram(
            execution, *program, sources, plane, KernelValidityView{.descriptor = working.validity});
        if (!executed.hasValue())
        {
            return executed.status();
        }
    }
    return Status::success();
}

/// Whether an output is four bytes per pixel, colour and alpha, in either order.
[[nodiscard]] constexpr bool fourByteOutput(PresentationOutput output) noexcept
{
    return output == PresentationOutput::Rgba8 || output == PresentationOutput::Bgra8;
}

/// How one pixel becomes output bytes: colour in its first one or three samples and alpha in its fourth.
class PixelWriter
{
public:
    /// A writer for one presentation's output.
    PixelWriter(const SourceShape& shape, const DisplayPresentationRequest& request,
                std::uint32_t outputChannels) noexcept
        : m_colorChannels(shape.colorChannels), m_channels(shape.channels), m_outputChannels(outputChannels),
          m_rgba(fourByteOutput(request.output)), m_bgra(request.output == PresentationOutput::Bgra8),
          m_transparent(request.invalidPixels == PresentationInvalidPixels::Transparent &&
                        (fourByteOutput(request.output) || shape.channels == 4u))
    {
    }

    /// Writes one pixel's bytes.
    ///
    /// @return whether a colour sample of the pixel was invalid.
    [[nodiscard]] bool write(const std::array<float, 4u>& pixel, std::uint8_t* written) const noexcept
    {
        bool invalid = false;
        for (std::uint32_t channel = 0u; channel < m_colorChannels; ++channel)
        {
            invalid = invalid || std::isnan(pixel[channel]);
        }
        if (invalid && m_transparent)
        {
            std::fill(written, written + m_outputChannels, std::uint8_t{0u});
            return true;
        }
        if (m_bgra)
        {
            // Premultiplied in the working sample: an opaque pixel's colour is multiplied by one, which leaves it
            // exactly as it was, and no colour byte can stand above its alpha byte.
            const std::uint32_t green = m_colorChannels == 3u ? 1u : 0u;
            const std::uint32_t blue = m_colorChannels == 3u ? 2u : 0u;
            const float alpha = pixel[3];
            written[0] = toByte(pixel[blue] * alpha);
            written[1] = toByte(pixel[green] * alpha);
            written[2] = toByte(pixel[0] * alpha);
            written[3] = toByte(alpha);
            return invalid;
        }
        if (m_rgba)
        {
            const std::uint32_t green = m_colorChannels == 3u ? 1u : 0u;
            const std::uint32_t blue = m_colorChannels == 3u ? 2u : 0u;
            written[0] = toByte(pixel[0]);
            written[1] = toByte(pixel[green]);
            written[2] = toByte(pixel[blue]);
            written[3] = toByte(pixel[3]);
            return invalid;
        }
        for (std::uint32_t channel = 0u; channel < m_channels; ++channel)
        {
            written[channel] = toByte(channel < m_colorChannels ? pixel[channel] : pixel[3]);
        }
        return invalid;
    }

private:
    /// Colour or grey channels.
    std::uint32_t m_colorChannels;

    /// Every channel, alpha included.
    std::uint32_t m_channels;

    /// Bytes one output pixel holds.
    std::uint32_t m_outputChannels;

    /// Whether the output is four bytes, colour and alpha.
    bool m_rgba;

    /// Whether those four are blue, green, red and alpha, the colour premultiplied.
    bool m_bgra;

    /// Whether a pixel with an invalid colour sample is written entirely as zero.
    bool m_transparent;
};

/// Writes the working image into the byte output and counts the pixels with an invalid colour sample.
[[nodiscard]] Status packBytes(const ExecutionContext& execution, const WorkingImage& working, const SourceShape& shape,
                               const DisplayPresentationRequest& request, void* destination,
                               const ImageLayout& destinationLayout, std::atomic<std::uint64_t>& invalidPixels)
{
    const std::uint32_t width = working.layout->extent().width();
    const std::uint32_t height = working.layout->extent().height();
    const std::uint32_t outputChannels = destinationLayout.samplesPerPixel();
    const PixelWriter writer(shape, request, outputChannels);
    auto* base = static_cast<std::uint8_t*>(destination);
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * (shape.channels * 4u + outputChannels),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * outputChannels * 4u};
    // A working image with no invalid sample holds only values within the unit interval, which every step
    // leaves there, so its bytes need neither guard.
    const bool whole = working.validity.rule() == ValidityRule::AllValid && shape.channels != 4u;
    const bool rgba = request.output == PresentationOutput::Rgba8;
    const bool bgra = request.output == PresentationOutput::Bgra8;
    return parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::uint64_t localInvalid = 0u;
        for (std::uint64_t row = range.begin; row < range.end; ++row)
        {
            const auto y = static_cast<std::uint32_t>(row);
            std::array<const float*, 4u> planes{};
            for (std::uint32_t channel = 0u; channel < shape.channels; ++channel)
            {
                planes[channel] = working.row(channel, y);
            }
            std::uint8_t* output = base + static_cast<std::uint64_t>(y) * destinationLayout.rowStrideBytes();
            if (whole)
            {
                // Each channel of a chunk is converted on its own, which vectorizes, and then interleaved.
                std::array<std::array<std::uint8_t, kByteChunk>, 3u> chunk{};
                for (std::uint32_t first = 0u; first < width; first += kByteChunk)
                {
                    const std::uint32_t count = (std::min)(kByteChunk, width - first);
                    for (std::uint32_t channel = 0u; channel < shape.colorChannels; ++channel)
                    {
                        const float* plane = planes[channel] + first;
                        std::uint8_t* bytes = chunk[channel].data();
                        for (std::uint32_t x = 0u; x < count; ++x)
                        {
                            bytes[x] = unitToByte(plane[x]);
                        }
                    }
                    const std::uint8_t* red = chunk[0].data();
                    const std::uint8_t* green = chunk[shape.colorChannels == 3u ? 1u : 0u].data();
                    const std::uint8_t* blue = chunk[shape.colorChannels == 3u ? 2u : 0u].data();
                    std::uint8_t* written = output + static_cast<std::uint64_t>(first) * outputChannels;
                    if (rgba)
                    {
                        for (std::uint32_t x = 0u; x < count; ++x)
                        {
                            written[x * 4u + 0u] = red[x];
                            written[x * 4u + 1u] = green[x];
                            written[x * 4u + 2u] = blue[x];
                            written[x * 4u + 3u] = 255u;
                        }
                    }
                    else if (bgra)
                    {
                        // Every pixel here is opaque, so premultiplying leaves its colour as it is.
                        for (std::uint32_t x = 0u; x < count; ++x)
                        {
                            written[x * 4u + 0u] = blue[x];
                            written[x * 4u + 1u] = green[x];
                            written[x * 4u + 2u] = red[x];
                            written[x * 4u + 3u] = 255u;
                        }
                    }
                    else if (shape.channels == 3u)
                    {
                        for (std::uint32_t x = 0u; x < count; ++x)
                        {
                            written[x * 3u + 0u] = red[x];
                            written[x * 3u + 1u] = green[x];
                            written[x * 3u + 2u] = blue[x];
                        }
                    }
                    else
                    {
                        std::copy(red, red + count, written);
                    }
                }
                continue;
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                std::array<float, 4u> pixel{0.0f, 0.0f, 0.0f, 1.0f};
                for (std::uint32_t channel = 0u; channel < shape.colorChannels; ++channel)
                {
                    pixel[channel] = planes[channel][x];
                }
                if (shape.channels == 4u)
                {
                    pixel[3] = planes[3][x];
                }
                localInvalid += writer.write(pixel, output + static_cast<std::uint64_t>(x) * outputChannels) ? 1u : 0u;
            }
        }
        invalidPixels.fetch_add(localInvalid, std::memory_order_relaxed);
        return Status::success();
    });
}

/// A linear presentation with nothing to measure and nothing to adjust: every presented sample goes from the source
/// to its bytes through the same promotion and the same conversion, with no working image between them.
template <ResidentSample SampleT>
[[nodiscard]] Status presentDirectTyped(const ExecutionContext& execution, const DisplayPresentationSource& source,
                                        const SourceShape& shape, std::uint32_t factor,
                                        const DisplayPresentationRequest& request, void* destination,
                                        const ImageLayout& destinationLayout, std::atomic<std::uint64_t>& invalidPixels)
{
    const std::uint32_t width = destinationLayout.extent().width();
    const std::uint32_t height = destinationLayout.extent().height();
    const std::uint32_t outputChannels = destinationLayout.samplesPerPixel();
    const PixelWriter writer(shape, request, outputChannels);
    auto* base = static_cast<std::uint8_t*>(destination);
    // An eight- or sixteen-bit code presented alone has a row compiled for the execution's instruction set, or none.
    PresentationCodeRowRunFn compiledRow = nullptr;
    if constexpr (std::is_same_v<SampleT, std::uint8_t> || std::is_same_v<SampleT, std::uint16_t>)
    {
        compiledRow = presentationCodeRowRunFor(execution, ResidentSampleTraits<SampleT>::kSampleType);
    }
    const TaskCost cost{.bytesPerItem =
                            static_cast<std::uint64_t>(width) * (shape.channels * sizeof(SampleT) + outputChannels),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * outputChannels * 4u};
    return parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        SourceRow<SampleT> reader(source, shape, factor, request.promotion);
        // With no rule or mask and no alpha, a narrow integer whose promotion changes nothing goes from its code to
        // its byte in integer arithmetic, and a floating sample is promoted and converted with nothing else read.
        constexpr bool kFloating = std::is_floating_point_v<SampleT>;
        bool narrowExact = false;
        if constexpr (kNarrowInteger<SampleT>)
        {
            narrowExact = !reader.quantized() || std::is_unsigned_v<SampleT>;
        }
        const bool transparent =
            request.invalidPixels == PresentationInvalidPixels::Transparent && fourByteOutput(request.output);
        const bool tight = !reader.ruled() && shape.channels != 4u && (narrowExact || (kFloating && !transparent));
        // Four bytes per pixel in either order; a tight pixel is opaque, so premultiplying changes no colour byte.
        const bool rgba = fourByteOutput(request.output);
        const bool bgra = request.output == PresentationOutput::Bgra8;
        const auto byteOf = [&](SampleT raw, bool& invalid) -> std::uint8_t {
            if constexpr (kNarrowInteger<SampleT>)
            {
                if (narrowExact)
                {
                    return narrowCodeToByte(raw);
                }
            }
            const float value = reader.promoteValue(raw);
            invalid = invalid || std::isnan(value);
            return toByte(value);
        };
        std::uint64_t localInvalid = 0u;
        for (std::uint64_t row = range.begin; row < range.end; ++row)
        {
            const auto y = static_cast<std::uint32_t>(row);
            reader.select(y);
            std::uint8_t* output = base + static_cast<std::uint64_t>(y) * destinationLayout.rowStrideBytes();
            if (tight)
            {
                const std::uint64_t step = reader.step();
                const SampleT* red = reader.row(0u);
                if constexpr (kNarrowInteger<SampleT>)
                {
                    if (narrowExact && shape.colorChannels == 1u && !rgba)
                    {
                        if constexpr (std::is_same_v<SampleT, std::uint8_t> || std::is_same_v<SampleT, std::uint16_t>)
                        {
                            if (compiledRow != nullptr)
                            {
                                compiledRow(red, reader.rowSamples(0u), step, output, width);
                            }
                            else
                            {
                                presentCodeRow(red, step, output, 0u, width);
                            }
                        }
                        else
                        {
                            for (std::uint32_t x = 0u; x < width; ++x)
                            {
                                output[x] = narrowCodeToByte(red[x * step]);
                            }
                        }
                        continue;
                    }
                }
                if (shape.colorChannels == 1u)
                {
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        bool invalid = false;
                        const std::uint8_t grey = byteOf(red[x * step], invalid);
                        localInvalid += invalid ? 1u : 0u;
                        if (rgba)
                        {
                            output[x * 4u + 0u] = grey;
                            output[x * 4u + 1u] = grey;
                            output[x * 4u + 2u] = grey;
                            output[x * 4u + 3u] = 255u;
                        }
                        else
                        {
                            output[x] = grey;
                        }
                    }
                    continue;
                }
                const SampleT* green = reader.row(1u);
                const SampleT* blue = reader.row(2u);
                const SampleT* first = bgra ? blue : red;
                const SampleT* third = bgra ? red : blue;
                const std::uint32_t stride = rgba ? 4u : 3u;
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    bool invalid = false;
                    std::uint8_t* written = output + static_cast<std::uint64_t>(x) * stride;
                    written[0] = byteOf(first[x * step], invalid);
                    written[1] = byteOf(green[x * step], invalid);
                    written[2] = byteOf(third[x * step], invalid);
                    if (rgba)
                    {
                        written[3] = 255u;
                    }
                    localInvalid += invalid ? 1u : 0u;
                }
                continue;
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                std::array<float, 4u> pixel{0.0f, 0.0f, 0.0f, 1.0f};
                for (std::uint32_t channel = 0u; channel < shape.colorChannels; ++channel)
                {
                    pixel[channel] = reader.sample(channel, x);
                }
                if (shape.channels == 4u)
                {
                    pixel[3] = reader.sample(3u, x);
                }
                localInvalid += writer.write(pixel, output + static_cast<std::uint64_t>(x) * outputChannels) ? 1u : 0u;
            }
        }
        invalidPixels.fetch_add(localInvalid, std::memory_order_relaxed);
        return Status::success();
    });
}

/// Counts the pixels of the working image with an invalid colour sample.
[[nodiscard]] Status countInvalidPixels(const ExecutionContext& execution, const WorkingImage& working,
                                        std::uint32_t colorChannels, std::atomic<std::uint64_t>& invalidPixels)
{
    const std::uint32_t width = working.layout->extent().width();
    const std::uint32_t height = working.layout->extent().height();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * colorChannels * 4u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * colorChannels};
    return parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::uint64_t localInvalid = 0u;
        for (std::uint64_t row = range.begin; row < range.end; ++row)
        {
            const auto y = static_cast<std::uint32_t>(row);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                bool invalid = false;
                for (std::uint32_t channel = 0u; channel < colorChannels; ++channel)
                {
                    invalid = invalid || std::isnan(working.row(channel, y)[x]);
                }
                localInvalid += invalid ? 1u : 0u;
            }
        }
        invalidPixels.fetch_add(localInvalid, std::memory_order_relaxed);
        return Status::success();
    });
}

/// The whole pipeline. With no destination it stops after measuring and returns the analysis alone.
[[nodiscard]] Result<DisplayPresentationResult>
runPresentation(const ExecutionContext& execution, const DisplayPresentationRequest& request, std::uint32_t factor,
                bool preview, const DisplayPresentationSource& source, void* destination,
                const ImageLayout* destinationLayout, const DisplayPresentationAnalysis* analysis,
                const char* operation)
{
    if (!execution.valid())
    {
        return presentationFailure(ErrorCode::NotInitialized, "a presentation was given an empty execution context",
                                   operation);
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
    const Result<SourceShape> shape = checkSource(source, operation);
    if (!shape.hasValue())
    {
        return shape.status();
    }
    if ((request.neutralize || request.calibrate) && shape->colorChannels != 3u)
    {
        return presentationFailure(ErrorCode::IncompatibleArguments,
                                   "neutralization and calibration balance three colour channels; a grey source has "
                                   "one",
                                   operation);
    }
    const Result<ImageLayoutRequest> expected = outputLayoutRequest(request, *shape, factor, operation);
    if (!expected.hasValue())
    {
        return expected.status();
    }
    if (destination != nullptr)
    {
        const Status destinationValid = checkDestination(source, *expected, destination, *destinationLayout, operation);
        if (destinationValid.failed())
        {
            return destinationValid;
        }
    }
    if (analysis != nullptr)
    {
        const Status analysisValid = checkAnalysis(*analysis, request, shape->colorChannels, !preview, operation);
        if (analysisValid.failed())
        {
            return analysisValid;
        }
    }

    const std::uint32_t width = subsampledSize(shape->width, factor);
    const std::uint32_t height = subsampledSize(shape->height, factor);
    // A linear presentation that balances and adjusts nothing needs no working image: each presented sample goes
    // straight to its bytes, through the same promotion and the same conversion the working image would apply.
    const bool direct = destination != nullptr && request.output != PresentationOutput::WorkingFloat32 &&
                        request.stretch == PresentationStretch::Linear && !request.neutralize && !request.calibrate &&
                        (request.saturation == 0.0 || shape->colorChannels == 1u) && request.brightness == 0.0 &&
                        request.contrast == 1.0;
    if (direct)
    {
        DisplayPresentationResult result;
        result.width = width;
        result.height = height;
        result.analysisReused = analysis != nullptr;
        if (analysis != nullptr)
        {
            result.analysis = *analysis;
        }
        else
        {
            result.analysis.colorChannels = shape->colorChannels;
            result.analysis.promotion = request.promotion;
            result.analysis.realtimePreview = preview;
        }
        std::atomic<std::uint64_t> invalidPixels{0u};
        const Status presented = dispatchSource(shape->sampleType, [&](auto tag) {
            return presentDirectTyped<decltype(tag)>(execution, source, *shape, factor, request, destination,
                                                     *destinationLayout, invalidPixels);
        });
        if (presented.failed())
        {
            return presented;
        }
        result.invalidPixels = invalidPixels.load();
        return result;
    }
    // An eight- or sixteen-bit source is measured from how often each of its codes occurs before anything is promoted:
    // an analysis alone then promotes nothing, and a presentation runs as one handed that analysis runs.
    std::optional<DisplayPresentationAnalysis> fromCodes;
    if (analysis == nullptr && measuresFromCodes(request, *shape, factor))
    {
        fromCodes.emplace();
        fromCodes->colorChannels = shape->colorChannels;
        fromCodes->promotion = request.promotion;
        fromCodes->realtimePreview = preview;
        const Status statistics =
            measureStatisticsFromCodes(execution, source, *shape, request.promotion, *fromCodes, operation);
        if (statistics.failed())
        {
            return statistics;
        }
        if (destination == nullptr)
        {
            DisplayPresentationResult result;
            result.width = width;
            result.height = height;
            result.analysis = *fromCodes;
            return result;
        }
    }
    const DisplayPresentationAnalysis* const through = fromCodes.has_value() ? &*fromCodes : analysis;
    // An analysis alone over a Float32 source that balances nothing measures the stored samples where they lie, each
    // held to the unit interval as the promotion would hold it, so no working image is promoted for a measurement.
    if (destination == nullptr && analysis == nullptr && measuresSourceDirectly(request, *shape, source, factor))
    {
        DisplayPresentationResult result;
        result.width = width;
        result.height = height;
        result.analysis.colorChannels = shape->colorChannels;
        result.analysis.promotion = request.promotion;
        result.analysis.realtimePreview = preview;
        const Status statistics = measureSourceStatistics(execution, source, shape->colorChannels, result.analysis);
        if (statistics.failed())
        {
            return statistics;
        }
        return result;
    }
    WorkingImage working;
    WorkingBlock block;
    if (destination != nullptr && request.output == PresentationOutput::WorkingFloat32)
    {
        working.samples = static_cast<std::uint8_t*>(destination);
        working.layout = *destinationLayout;
    }
    else
    {
        // Tightly packed rows, so the statistics read each plane as one span: a padded row takes the
        // exact median off the paths that rank a contiguous plane, about twelve times the work.
        const std::optional<ImageExtent> extent =
            ImageExtent::create(width, height, shape->channels, execution.limits());
        if (extent.has_value())
        {
            working.layout = ImageLayout::create(ImageLayoutRequest{.extent = *extent,
                                                                    .elementSizeBytes = kWorkingSampleBytes,
                                                                    .samplesPerPixel = 1u,
                                                                    .planeCount = shape->channels,
                                                                    .rowAlignmentBytes = kWorkingSampleBytes},
                                                 execution.limits());
        }
        if (!working.layout.has_value())
        {
            return presentationFailure(ErrorCode::InvalidArgument,
                                       "the working image cannot be described under the engine's limits", operation);
        }
        const Status taken = block.take(execution, working.layout->totalBytes(), operation);
        if (taken.failed())
        {
            return taken;
        }
        working.samples = block.bytes();
    }
    const Status described = describeWorkingViews(working, shape->colorChannels, execution.limits(), operation);
    if (described.failed())
    {
        return described;
    }

    std::atomic<std::uint64_t> invalidSamples{0u};
    // Over a prepared analysis that balances and adjusts nothing - the caller's, or the one the codes gave - the
    // stretch is the only step after the promotion, so each row is stretched as it is promoted, which reads and writes
    // the image once rather than twice.
    const bool stretchOnly = through != nullptr && destination != nullptr && !through->neutralized &&
                             !through->calibrated && request.stretch == PresentationStretch::Automatic &&
                             (request.saturation == 0.0 || shape->colorChannels == 1u) && request.brightness == 0.0 &&
                             request.contrast == 1.0;
    bool stretchedAsPromoted = false;
    if (stretchOnly)
    {
        const Result<std::array<MidtonesTransferParameters, 3u>> transfer = deriveTransfer(request, *through);
        if (!transfer.hasValue())
        {
            return transfer.status();
        }
        // A working image that is the caller's destination and too large for the caches is written without reading it.
        const bool stream = request.output == PresentationOutput::WorkingFloat32 &&
                            working.layout->totalBytes() >= kMidtonesTransferStreamingBytes;
        const Result<bool> stretched =
            promoteAndStretch(execution, source, *shape, factor, request.promotion, working, *transfer, stream);
        if (!stretched.hasValue())
        {
            return stretched.status();
        }
        stretchedAsPromoted = *stretched;
    }
    if (!stretchedAsPromoted)
    {
        const Status promoted = promote(execution, source, *shape, factor, request.promotion, working, invalidSamples);
        if (promoted.failed())
        {
            return promoted;
        }
    }
    working.validity = invalidSamples.load() == 0u ? ValidityDescriptor{} : finiteRule();

    DisplayPresentationResult result;
    result.width = width;
    result.height = height;
    if (through != nullptr)
    {
        result.analysis = *through;
        result.analysisReused = analysis != nullptr;
        if (through->neutralized)
        {
            const Status balanced =
                applyBalance(execution, working, {1.0, 1.0, 1.0}, through->neutralizationOffsets, operation);
            if (balanced.failed())
            {
                return balanced;
            }
        }
        if (through->calibrated)
        {
            const Status balanced =
                applyBalance(execution, working, through->calibrationGains, through->calibrationOffsets, operation);
            if (balanced.failed())
            {
                return balanced;
            }
        }
    }
    else
    {
        DisplayPresentationAnalysis& measured = result.analysis;
        measured.colorChannels = shape->colorChannels;
        measured.promotion = request.promotion;
        measured.realtimePreview = preview;
        if (request.neutralize)
        {
            const Result<ColorChannelOffsets> offsets =
                measureNeutralization(execution, working, request.neutralizationBackground);
            if (!offsets.hasValue())
            {
                return offsets.status();
            }
            const Status balanced = applyBalance(execution, working, {1.0, 1.0, 1.0}, *offsets, operation);
            if (balanced.failed())
            {
                return balanced;
            }
            measured.neutralized = true;
            measured.neutralizationOffsets = *offsets;
        }
        if (request.calibrate)
        {
            const Result<CalibrationTerms> terms = measureCalibration(execution, working, request, operation);
            if (!terms.hasValue())
            {
                return terms.status();
            }
            const Status balanced = applyBalance(execution, working, terms->gains, terms->offsets, operation);
            if (balanced.failed())
            {
                return balanced;
            }
            measured.calibrated = true;
            measured.calibrationGains = terms->gains;
            measured.calibrationOffsets = terms->offsets;
        }
        if (request.stretch == PresentationStretch::Automatic)
        {
            const Status statistics = measureStatistics(execution, working, shape->colorChannels, measured);
            if (statistics.failed())
            {
                return statistics;
            }
        }
    }
    if (destination == nullptr)
    {
        return result;
    }

    const Result<std::array<MidtonesTransferParameters, 3u>> transfer = deriveTransfer(request, result.analysis);
    if (!transfer.hasValue())
    {
        return transfer.status();
    }
    result.transfer = *transfer;
    if (request.stretch == PresentationStretch::Automatic && !stretchedAsPromoted)
    {
        const Status stretched = applyTransfer(execution, working, shape->colorChannels, *transfer);
        if (stretched.failed())
        {
            return stretched;
        }
    }
    if (request.saturation != 0.0 && shape->colorChannels == 3u)
    {
        const Result<double> background = applySaturation(execution, working, request);
        if (!background.hasValue())
        {
            return background.status();
        }
        result.saturationBackground = *background;
    }
    if (request.brightness != 0.0 || request.contrast != 1.0)
    {
        const Status adjusted = applyBrightnessContrast(execution, working, shape->colorChannels, request);
        if (adjusted.failed())
        {
            return adjusted;
        }
    }
    std::atomic<std::uint64_t> invalidPixels{0u};
    if (request.output == PresentationOutput::WorkingFloat32)
    {
        if (invalidSamples.load() != 0u)
        {
            const Status counted = countInvalidPixels(execution, working, shape->colorChannels, invalidPixels);
            if (counted.failed())
            {
                return counted;
            }
        }
    }
    else
    {
        const Status packed =
            packBytes(execution, working, *shape, request, destination, *destinationLayout, invalidPixels);
        if (packed.failed())
        {
            return packed;
        }
    }
    result.invalidPixels = invalidPixels.load();
    return result;
}

[[nodiscard]] Result<ImageLayoutRequest> layoutFor(const DisplayPresentationRequest& request,
                                                   const ImageDescriptor& source, std::uint32_t factor,
                                                   const char* operation)
{
    const std::optional<SourceShape> shape = shapeOf(source);
    if (!shape.has_value())
    {
        return presentationFailure(ErrorCode::IncompatibleArguments,
                                   "a presentation shows grey, RGB or RGBA; convert another schema first", operation);
    }
    return outputLayoutRequest(request, *shape, factor, operation);
}

} // namespace

Result<DisplayPresentationPlan> planDisplayPresentation(const DisplayPresentationRequest& request)
{
    const Status checked = checkRequest(request, kPlanOperation);
    if (checked.failed())
    {
        return checked;
    }
    return DisplayPresentationPlan(request);
}

Result<RealtimePreviewDisplayPresentationPlan>
planRealtimePreviewDisplayPresentation(const DisplayPresentationRequest& request, std::uint32_t subsampleFactor)
{
    if (subsampleFactor == 0u || subsampleFactor > kMaxRealtimePreviewSubsampleFactor)
    {
        return presentationFailure(ErrorCode::OutOfRange, "a preview subsample factor is outside its range",
                                   kPreviewPlanOperation);
    }
    const Status checked = checkRequest(request, kPreviewPlanOperation);
    if (checked.failed())
    {
        return checked;
    }
    return RealtimePreviewDisplayPresentationPlan(request, subsampleFactor);
}

Result<ImageLayoutRequest> displayPresentationLayout(const DisplayPresentationPlan& plan, const ImageDescriptor& source)
{
    return layoutFor(plan.request(), source, 1u, kLayoutOperation);
}

Result<ImageLayoutRequest> realtimePreviewDisplayPresentationLayout(const RealtimePreviewDisplayPresentationPlan& plan,
                                                                    const ImageDescriptor& source)
{
    return layoutFor(plan.request(), source, plan.subsampleFactor(), kPreviewLayoutOperation);
}

Result<DisplayPresentationAnalysis> analyzeDisplayPresentation(const ExecutionContext& execution,
                                                               const DisplayPresentationPlan& plan,
                                                               const DisplayPresentationSource& source)
{
    const Result<DisplayPresentationResult> result =
        runPresentation(execution, plan.request(), 1u, false, source, nullptr, nullptr, nullptr, kAnalyzeOperation);
    if (!result.hasValue())
    {
        return result.status();
    }
    return result->analysis;
}

Result<DisplayPresentationResult> presentImage(const ExecutionContext& execution, const DisplayPresentationPlan& plan,
                                               const DisplayPresentationSource& source, void* destination,
                                               const ImageLayout& destinationLayout,
                                               const DisplayPresentationAnalysis* analysis)
{
    if (destination == nullptr)
    {
        return presentationFailure(ErrorCode::InvalidArgument, "a presentation has no destination", kPresentOperation);
    }
    return runPresentation(execution, plan.request(), 1u, false, source, destination, &destinationLayout, analysis,
                           kPresentOperation);
}

Result<DisplayPresentationResult> presentImageRealtimePreview(const ExecutionContext& execution,
                                                              const RealtimePreviewDisplayPresentationPlan& plan,
                                                              const DisplayPresentationSource& source,
                                                              void* destination, const ImageLayout& destinationLayout,
                                                              const DisplayPresentationAnalysis* analysis)
{
    if (destination == nullptr)
    {
        return presentationFailure(ErrorCode::InvalidArgument, "a presentation has no destination", kPreviewOperation);
    }
    return runPresentation(execution, plan.request(), plan.subsampleFactor(), true, source, destination,
                           &destinationLayout, analysis, kPreviewOperation);
}

} // namespace an
