// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the layer a viewer paints over an image to show its mask.
//
// One walk over the rows. A row is taken a run of pixels at a time: each channel of the mask - and, for a
// multiply, of the image - is read into a run of doubles through its source's validity, as a fraction of full
// scale by the engine's own conversion, and each pixel's four bytes are formed from the runs and rounded once.
// A one-channel eight-bit mask with no validity rule to apply has 256 possible pixels, so it is written from a
// table of them built once per call. The runs and the table live on the stack, so the operation takes no
// memory of its own.

#include <AfterNightEngine/Algorithms/Masks/MaskOverlay.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
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
#include <cstring>
#include <optional>
#include <type_traits>

namespace an
{
namespace
{

constexpr const char* kPlanOperation = "an::planMaskOverlay";
constexpr const char* kLayoutOperation = "an::maskOverlayLayout";
constexpr const char* kRenderOperation = "an::renderMaskOverlay";

/// How many pixels of a row one pass reads into its runs. Seven runs of doubles at this length stay well
/// inside a core's first-level cache and well inside a worker's stack.
constexpr std::uint32_t kRunPixels = 256u;

/// The bytes of one overlay pixel.
constexpr std::uint32_t kPixelBytes = 4u;

/// The boundary each overlay row starts on.
constexpr std::uint64_t kRowAlignmentBytes = 4u;

/// One and a half times two to the fifty-second: adding and subtracting it rounds a double below two to the
/// fifty-first to the nearest integer with ties to even, in the default rounding mode.
constexpr double kRoundingShift = 6755399441055744.0;

[[nodiscard]] Status overlayFailure(ErrorCode code, const char* message, const char* operation, const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// Whether a description has as many channels as an overlay reads: one or three.
[[nodiscard]] constexpr bool overlayChannels(std::uint32_t channels) noexcept
{
    return channels == 1u || channels == 3u;
}

/// Whether a domain is read as a fraction of full scale.
[[nodiscard]] constexpr bool normalizedDomain(NumericDomain domain) noexcept
{
    return domain == NumericDomain::NormalizedLinear || domain == NumericDomain::EncodedNonlinear;
}

/// A value of the unit interval as a byte: times 255, rounded to nearest with ties to even; below zero or not
/// a number is zero, above one is 255.
[[nodiscard]] std::uint8_t toByte(double value) noexcept
{
    if (!(value > 0.0))
    {
        return 0u;
    }
    if (value >= 1.0)
    {
        return 255u;
    }
    return static_cast<std::uint8_t>(((value * 255.0) + kRoundingShift) - kRoundingShift);
}

/// A mask value held to the unit interval, with a value that is not a number read as zero.
[[nodiscard]] constexpr double unitMask(double value) noexcept
{
    return value > 0.0 ? (value < 1.0 ? value : 1.0) : 0.0;
}

[[nodiscard]] bool alignedFor(const void* samples, std::uint32_t elementBytes) noexcept
{
    return elementBytes != 0u && reinterpret_cast<std::uintptr_t>(samples) % elementBytes == 0u;
}

/// Whether a source's members describe one another, and its mask what its rule needs.
[[nodiscard]] Status checkSource(const MaskOverlaySource& source, const char* parameter)
{
    if (source.descriptor == nullptr || source.samples == nullptr || source.layout == nullptr)
    {
        return overlayFailure(ErrorCode::InvalidArgument, "an overlay source has a null member", kRenderOperation,
                              parameter);
    }
    const ImageDescriptor& descriptor = *source.descriptor;
    const ImageLayout& layout = *source.layout;
    const std::uint32_t elementBytes = sampleTypeSizeBytes(descriptor.sampleType());
    if (!(layout.extent() == descriptor.extent()) || layout.elementSizeBytes() != elementBytes ||
        layout.planeCount() * layout.samplesPerPixel() != descriptor.extent().channelCount() ||
        !alignedFor(source.samples, elementBytes))
    {
        return overlayFailure(ErrorCode::InvalidArgument,
                              "an overlay source's layout or pointer does not describe its descriptor",
                              kRenderOperation, parameter);
    }
    if (!overlayChannels(descriptor.extent().channelCount()))
    {
        return overlayFailure(ErrorCode::IncompatibleArguments, "an overlay reads one channel or three",
                              kRenderOperation, parameter);
    }
    if (!normalizedDomain(descriptor.numericDomain()))
    {
        return overlayFailure(ErrorCode::IncompatibleArguments,
                              "an overlay reads its samples as fractions of full scale, so their domain is a "
                              "normalized one; restate a sensor code or physical quantity on purpose first",
                              kRenderOperation, parameter);
    }
    const KernelValidityView& validity = source.validity;
    if (!validity.descriptor.needsMask())
    {
        if (validity.mask != nullptr || validity.maskPlane != 0u)
        {
            return overlayFailure(ErrorCode::InvalidArgument, "the validity view supplies an unused mask",
                                  kRenderOperation, parameter);
        }
        return Status::success();
    }
    if (validity.mask == nullptr)
    {
        return overlayFailure(ErrorCode::InvalidArgument, "an explicit-mask source supplies no mask", kRenderOperation,
                              parameter);
    }
    const std::uint32_t channels = descriptor.extent().channelCount();
    const bool perChannel = validity.mask->planeCount() == channels && channels > 1u;
    if (validity.mask->width() != descriptor.extent().width() ||
        validity.mask->height() != descriptor.extent().height() ||
        (!perChannel && validity.maskPlane >= validity.mask->planeCount()))
    {
        return overlayFailure(ErrorCode::IncompatibleArguments, "the validity mask does not cover its source",
                              kRenderOperation, parameter);
    }
    return Status::success();
}

/// Whether a source's samples or mask share memory with the destination.
[[nodiscard]] Status checkSeparate(const MaskOverlaySource& source, const std::uint8_t* destination,
                                   std::uint64_t destinationBytes, const char* parameter)
{
    if (regionsOverlap(static_cast<const std::uint8_t*>(source.samples), source.layout->totalBytes(), destination,
                       destinationBytes))
    {
        return overlayFailure(ErrorCode::IncompatibleArguments, "the overlay destination overlaps a source",
                              kRenderOperation, parameter);
    }
    if (source.validity.mask != nullptr &&
        regionsOverlap(source.validity.mask->data(), source.validity.mask->spannedByteCount(), destination,
                       destinationBytes))
    {
        return overlayFailure(ErrorCode::InvalidArgument, "a validity mask overlaps the overlay destination",
                              kRenderOperation, parameter);
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
    const MaskOverlaySource* source = nullptr;

    /// An integer sample as a fraction of full scale.
    ConversionPolicy policy{.domain = DomainMapping::FullScale};

    /// Whether the mask gives every channel its own plane.
    bool perChannelMask = false;

    /// Whether any sample can be invalid: a value rule other than all-valid, or a mask.
    bool ruled = false;

    /// Reads a run of one channel of one row as a fraction of full scale, and which samples mean something;
    /// a sample that does not clears its pixel's flag, and one that does leaves it.
    ///
    /// @param channel which channel.
    /// @param y the row.
    /// @param x0 the first pixel of the run.
    /// @param count how many pixels, at most `kRunPixels`.
    /// @param values where the values go.
    /// @param valid each pixel's flag.
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
                if (ruled && !(sampleIsValidByValue(validity.descriptor, raw) &&
                               (validity.mask == nullptr || maskBit(*validity.mask, maskPlane, x0 + index, y))))
                {
                    valid[index] = false;
                }
            }
        });
    }
};

[[nodiscard]] SourceReader readerOf(const MaskOverlaySource& source) noexcept
{
    SourceReader reader;
    reader.source = &source;
    const std::uint32_t channels = source.descriptor->extent().channelCount();
    reader.perChannelMask =
        source.validity.mask != nullptr && source.validity.mask->planeCount() == channels && channels > 1u;
    reader.ruled = source.validity.descriptor.rule() != ValidityRule::AllValid || source.validity.mask != nullptr;
    return reader;
}

/// Writes one pixel's four bytes.
inline void writePixel(std::uint8_t* pixel, std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                       std::uint8_t alpha) noexcept
{
    pixel[0] = red;
    pixel[1] = green;
    pixel[2] = blue;
    pixel[3] = alpha;
}

/// What a mask sample of the unit interval shows as: a tint's alpha, or the value replace shows.
[[nodiscard]] std::uint8_t shownByte(MaskOverlayMode mode, double shown) noexcept
{
    return mode == MaskOverlayMode::Tint ? toByte(1.0 - shown) : toByte(shown);
}

/// Writes the overlay of the rows in one range, from runs of each source's channels.
void overlayRows(const MaskOverlayRequest& request, const SourceReader& mask, const SourceReader* image,
                 std::uint8_t* destination, const ImageLayout& destinationLayout, TaskRange range,
                 std::uint64_t& valid) noexcept
{
    const std::uint32_t width = destinationLayout.extent().width();
    const std::uint32_t maskChannels = mask.source->descriptor->extent().channelCount();
    const std::uint32_t imageChannels = image == nullptr ? 0u : image->source->descriptor->extent().channelCount();

    std::array<std::array<double, kRunPixels>, 3u> maskValues{};
    std::array<std::array<double, kRunPixels>, 3u> imageValues{};
    std::array<bool, kRunPixels> pixelValid{};

    for (std::uint64_t row = range.begin; row < range.end; ++row)
    {
        const std::uint32_t y = static_cast<std::uint32_t>(row);
        std::uint8_t* const out = destination + static_cast<std::uint64_t>(y) * destinationLayout.rowStrideBytes();
        for (std::uint32_t x0 = 0u; x0 < width; x0 += kRunPixels)
        {
            const std::uint32_t count = (std::min)(kRunPixels, width - x0);
            std::fill_n(pixelValid.begin(), count, true);
            for (std::uint32_t channel = 0u; channel < maskChannels; ++channel)
            {
                mask.read(channel, y, x0, count, maskValues[channel].data(), pixelValid.data());
                for (std::uint32_t index = 0u; index < count; ++index)
                {
                    const double held = unitMask(maskValues[channel][index]);
                    maskValues[channel][index] = request.invertMask ? 1.0 - held : held;
                }
            }
            for (std::uint32_t channel = 0u; channel < imageChannels; ++channel)
            {
                image->read(channel, y, x0, count, imageValues[channel].data(), pixelValid.data());
            }
            for (std::uint32_t index = 0u; index < count; ++index)
            {
                std::uint8_t* const pixel = out + static_cast<std::uint64_t>(x0 + index) * kPixelBytes;
                if (!pixelValid[index])
                {
                    const MaskOverlayColor& invalid = request.invalidPixel;
                    writePixel(pixel, invalid.red, invalid.green, invalid.blue, invalid.alpha);
                    continue;
                }
                ++valid;
                switch (request.mode)
                {
                case MaskOverlayMode::Tint: {
                    double shown = maskValues[0][index];
                    for (std::uint32_t channel = 1u; channel < maskChannels; ++channel)
                    {
                        shown *= maskValues[channel][index];
                    }
                    writePixel(pixel, request.tint.red, request.tint.green, request.tint.blue, toByte(1.0 - shown));
                    break;
                }
                case MaskOverlayMode::Replace: {
                    const std::uint8_t red = toByte(maskValues[0][index]);
                    if (maskChannels == 1u)
                    {
                        writePixel(pixel, red, red, red, 255u);
                    }
                    else
                    {
                        writePixel(pixel, red, toByte(maskValues[1][index]), toByte(maskValues[2][index]), 255u);
                    }
                    break;
                }
                case MaskOverlayMode::Multiply: {
                    std::array<std::uint8_t, 3u> shown{};
                    for (std::uint32_t channel = 0u; channel < imageChannels; ++channel)
                    {
                        const std::uint32_t maskChannel = maskChannels == 1u ? 0u : channel;
                        shown[channel] = toByte(imageValues[channel][index] * maskValues[maskChannel][index]);
                    }
                    if (imageChannels == 1u)
                    {
                        writePixel(pixel, shown[0], shown[0], shown[0], 255u);
                    }
                    else
                    {
                        writePixel(pixel, shown[0], shown[1], shown[2], 255u);
                    }
                    break;
                }
                }
            }
        }
    }
}

/// Whether a mask is read through the eight-bit table: one channel, a byte representation, and no rule that
/// can reject a sample.
[[nodiscard]] bool tabulated(const MaskOverlayRequest& request, const MaskOverlaySource& mask) noexcept
{
    const SampleType type = mask.descriptor->sampleType();
    return request.mode != MaskOverlayMode::Multiply && mask.descriptor->extent().channelCount() == 1u &&
           (type == SampleType::UInt8 || type == SampleType::Int8) &&
           mask.validity.descriptor.rule() == ValidityRule::AllValid && mask.validity.mask == nullptr;
}

/// The 256 pixels an eight-bit one-channel mask can show, indexed by the sample's bits as an unsigned byte.
template <typename SampleT>
[[nodiscard]] std::array<std::array<std::uint8_t, kPixelBytes>, 256u>
pixelTable(const MaskOverlayRequest& request) noexcept
{
    std::array<std::array<std::uint8_t, kPixelBytes>, 256u> table{};
    const ConversionPolicy policy{.domain = DomainMapping::FullScale};
    for (std::uint32_t bits = 0u; bits < 256u; ++bits)
    {
        const SampleT raw = std::bit_cast<SampleT>(static_cast<std::uint8_t>(bits));
        const double held = unitMask(convertSample<SampleT, double>(raw, policy).value);
        const double shown = request.invertMask ? 1.0 - held : held;
        const std::uint8_t value = shownByte(request.mode, shown);
        table[bits] =
            request.mode == MaskOverlayMode::Tint
                ? std::array<std::uint8_t, kPixelBytes>{request.tint.red, request.tint.green, request.tint.blue, value}
                : std::array<std::uint8_t, kPixelBytes>{value, value, value, 255u};
    }
    return table;
}

/// Writes the overlay of the rows in one range from the eight-bit table.
void tabulatedRows(const std::array<std::array<std::uint8_t, kPixelBytes>, 256u>& table, const MaskOverlaySource& mask,
                   std::uint8_t* destination, const ImageLayout& destinationLayout, TaskRange range) noexcept
{
    const std::uint32_t width = destinationLayout.extent().width();
    const ImageLayout& layout = *mask.layout;
    const std::uint32_t stride = layout.samplesPerPixel();
    for (std::uint64_t row = range.begin; row < range.end; ++row)
    {
        const std::uint32_t y = static_cast<std::uint32_t>(row);
        const std::uint8_t* const samples =
            static_cast<const std::uint8_t*>(mask.samples) + plane_detail::channelRowByteOffset(layout, 0u, y);
        std::uint8_t* const out = destination + static_cast<std::uint64_t>(y) * destinationLayout.rowStrideBytes();
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            std::memcpy(out + static_cast<std::size_t>(x) * kPixelBytes,
                        table[samples[static_cast<std::size_t>(x) * stride]].data(), kPixelBytes);
        }
    }
}

} // namespace

Result<MaskOverlayPlan> planMaskOverlay(const MaskOverlayRequest& request)
{
    if (!isMaskOverlayMode(request.mode))
    {
        return overlayFailure(ErrorCode::InvalidArgument, "the overlay mode is outside its enumeration", kPlanOperation,
                              "mode");
    }
    return MaskOverlayPlan(request);
}

Result<ImageLayoutRequest> maskOverlayLayout(const MaskOverlayPlan& plan, const ImageDescriptor& mask)
{
    (void)plan;
    if (!overlayChannels(mask.extent().channelCount()))
    {
        return overlayFailure(ErrorCode::IncompatibleArguments, "an overlay reads one channel or three",
                              kLayoutOperation, "mask");
    }
    const std::optional<ImageExtent> extent =
        ImageExtent::create(mask.extent().width(), mask.extent().height(), kPixelBytes, defaultEngineLimits());
    if (!extent.has_value())
    {
        return overlayFailure(ErrorCode::IncompatibleArguments, "the overlay would have a shape no layer can have",
                              kLayoutOperation, "mask");
    }
    return ImageLayoutRequest{.extent = *extent,
                              .elementSizeBytes = 1u,
                              .samplesPerPixel = kPixelBytes,
                              .planeCount = 1u,
                              .rowAlignmentBytes = kRowAlignmentBytes};
}

Result<MaskOverlayResult> renderMaskOverlay(const ExecutionContext& execution, const MaskOverlayPlan& plan,
                                            const MaskOverlaySource& mask, const MaskOverlaySource* image,
                                            void* destination, const ImageLayout& destinationLayout)
{
    if (!execution.valid())
    {
        return overlayFailure(ErrorCode::NotInitialized, "an overlay was given an empty execution context",
                              kRenderOperation, "execution");
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
    const MaskOverlayRequest& request = plan.request();
    const Status maskChecked = checkSource(mask, "mask");
    if (maskChecked.failed())
    {
        return maskChecked;
    }
    const bool multiply = request.mode == MaskOverlayMode::Multiply;
    if (multiply != (image != nullptr))
    {
        return overlayFailure(ErrorCode::InvalidArgument,
                              multiply ? "a multiply overlay needs the image it shows"
                                       : "only a multiply overlay reads an image beside its mask",
                              kRenderOperation, "image");
    }
    const ImageExtent& maskExtent = mask.descriptor->extent();
    if (image != nullptr)
    {
        const Status imageChecked = checkSource(*image, "image");
        if (imageChecked.failed())
        {
            return imageChecked;
        }
        const ImageExtent& imageExtent = image->descriptor->extent();
        if (imageExtent.width() != maskExtent.width() || imageExtent.height() != maskExtent.height() ||
            (maskExtent.channelCount() == 3u && imageExtent.channelCount() != 3u))
        {
            return overlayFailure(ErrorCode::IncompatibleArguments,
                                  "a multiply's image has the mask's width and height, and three channels under a "
                                  "three-channel mask",
                                  kRenderOperation, "image");
        }
    }
    if (destination == nullptr)
    {
        return overlayFailure(ErrorCode::InvalidArgument, "the overlay destination is null", kRenderOperation,
                              "destination");
    }
    if (destinationLayout.extent().width() != maskExtent.width() ||
        destinationLayout.extent().height() != maskExtent.height() ||
        destinationLayout.extent().channelCount() != kPixelBytes || destinationLayout.elementSizeBytes() != 1u ||
        destinationLayout.samplesPerPixel() != kPixelBytes || destinationLayout.planeCount() != 1u)
    {
        return overlayFailure(ErrorCode::IncompatibleArguments,
                              "the overlay destination is not one interleaved plane of four bytes per pixel the "
                              "mask's width and height",
                              kRenderOperation, "destinationLayout");
    }
    std::uint8_t* const destinationBytes = static_cast<std::uint8_t*>(destination);
    const Status maskSeparate = checkSeparate(mask, destinationBytes, destinationLayout.totalBytes(), "mask");
    if (maskSeparate.failed())
    {
        return maskSeparate;
    }
    if (image != nullptr)
    {
        const Status imageSeparate = checkSeparate(*image, destinationBytes, destinationLayout.totalBytes(), "image");
        if (imageSeparate.failed())
        {
            return imageSeparate;
        }
    }

    const std::uint32_t width = maskExtent.width();
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * maskExtent.height();
    MaskOverlayResult result;
    result.run.selection.total = pixels;
    result.run.outputsWritten = pixels;

    if (tabulated(request, mask))
    {
        const auto table = mask.descriptor->sampleType() == SampleType::UInt8 ? pixelTable<std::uint8_t>(request)
                                                                              : pixelTable<std::int8_t>(request);
        const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * (1u + kPixelBytes),
                            .operationsPerItem = static_cast<std::uint64_t>(width)};
        const Status ran = parallelFor(execution, maskExtent.height(), cost, [&](TaskRange range) -> Status {
            tabulatedRows(table, mask, destinationBytes, destinationLayout, range);
            return Status::success();
        });
        if (ran.failed())
        {
            return ran;
        }
        result.run.selection.valid = pixels;
        return result;
    }

    const SourceReader maskReader = readerOf(mask);
    std::optional<SourceReader> imageReader;
    std::uint32_t readChannels = maskExtent.channelCount();
    std::uint64_t readBytes =
        static_cast<std::uint64_t>(sampleTypeSizeBytes(mask.descriptor->sampleType())) * maskExtent.channelCount();
    if (image != nullptr)
    {
        imageReader = readerOf(*image);
        readChannels += image->descriptor->extent().channelCount();
        readBytes += static_cast<std::uint64_t>(sampleTypeSizeBytes(image->descriptor->sampleType())) *
                     image->descriptor->extent().channelCount();
    }
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * (readBytes + kPixelBytes),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * readChannels * 6u};
    std::atomic<std::uint64_t> valid{0u};
    const SourceReader* const imageSource = imageReader.has_value() ? &*imageReader : nullptr;
    const Status ran = parallelFor(execution, maskExtent.height(), cost, [&](TaskRange range) -> Status {
        std::uint64_t rangeValid = 0u;
        overlayRows(request, maskReader, imageSource, destinationBytes, destinationLayout, range, rangeValid);
        valid.fetch_add(rangeValid, std::memory_order_relaxed);
        return Status::success();
    });
    if (ran.failed())
    {
        return ran;
    }
    result.run.selection.valid = valid.load(std::memory_order_relaxed);
    return result;
}

} // namespace an
