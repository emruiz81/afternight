// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the channel scope: one plane of a colour image in a named encoding, extracted,
// processed and recomposed.
//
// The colour work is the colour transform's own: the source's red, green and blue are promoted into a
// planar working image, `convertImageColor` turns it into the scope's encoding in place, and after the
// caller's operation it turns it back in place. What this file adds is the plumbing every client would
// otherwise write for every processor - which plane, in which units, with which samples meaning nothing,
// and how the result is written back - in three walks over the rows: the promotion, the preparation of
// the plane the operation reads, and the recomposition. A stored channel needs none of the colour work
// and is copied, or read where it lies when the source is planar.
//
// A preparation is the first half of that, the promotion and the conversion and the plane the operation
// reads, made once into planes of its own; a call handed it runs the operation into a plane of the scope's
// and converts back from that plane and the preparation's other two where they lie.

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/DescriptorError.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/color/ColorTransformPlanes.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <type_traits>

namespace an
{
namespace
{

constexpr const char* kPlanOperation = "an::planChannelScope";
constexpr const char* kDescriptorOperation = "an::channelScopePlaneDescriptor";
constexpr const char* kLayoutOperation = "an::channelScopePlaneLayout";
constexpr const char* kExtractOperation = "an::extractChannelScope";
constexpr const char* kApplyOperation = "an::applyChannelScope";
constexpr const char* kPrepareOperation = "an::prepareChannelScope";
constexpr const char* kEffectOperation = "an::channelScopeEffect";

/// The caller's operation is called across this boundary, so nothing it throws unwinds through the scope.
constexpr ExternalBoundary kOperationBoundary{"ChannelScopeOperation", kApplyOperation, ErrorDomain::Algorithms};

/// The colour channels a scope converts: red, green and blue. An alpha channel is the scope's to carry.
constexpr std::uint32_t kColorChannels = 3u;

[[nodiscard]] Status scopeFailure(ErrorCode code, const char* message, const char* operation, const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// Whether a scope is one of the three stored channels.
[[nodiscard]] constexpr bool storedChannel(ChannelScope scope) noexcept
{
    return scope == ChannelScope::Red || scope == ChannelScope::Green || scope == ChannelScope::Blue;
}

/// Whether a scope is a component of another encoding than the stored one.
[[nodiscard]] constexpr bool encodedComponent(ChannelScope scope) noexcept
{
    return isChannelScope(scope) && scope != ChannelScope::Whole && !storedChannel(scope);
}

/// The representation a component of another encoding is held in.
[[nodiscard]] constexpr SampleType workingTypeOf(SampleType source) noexcept
{
    return source == SampleType::Float64 ? SampleType::Float64 : SampleType::Float32;
}

/// The encoding a scope's plane is a component of, built from what the request states about the source.
///
/// Every member the scope's schema does not read stays at its neutral default, which `checkColorEncoding`
/// requires.
[[nodiscard]] ColorEncoding scopeEncodingOf(const ChannelScopeRequest& request) noexcept
{
    const ColorEncoding& source = request.sourceEncoding;
    const bool unit = request.units == ChannelScopeUnits::UnitInterval;
    ColorEncoding encoding{};
    encoding.schema = channelScopeEncoding(request.scope);
    encoding.whitePoint = source.whitePoint;
    switch (encoding.schema)
    {
    case ChannelSchema::Lab:
    case ChannelSchema::Lch:
        encoding.perceptual = unit ? PerceptualEncoding::UnitPacked : PerceptualEncoding::NaturalUnits;
        break;
    case ChannelSchema::Hsl:
    case ChannelSchema::Hsv:
    case ChannelSchema::Hsi:
        encoding.primaries = source.primaries;
        encoding.transfer = source.transfer;
        encoding.transferGamma = source.transferGamma;
        encoding.hue = unit ? HueConvention::TurnFraction : HueConvention::Degrees;
        break;
    case ChannelSchema::YCbCr:
        encoding.primaries = source.primaries;
        encoding.transfer = source.transfer;
        encoding.transferGamma = source.transferGamma;
        encoding.yCbCr = request.yCbCr;
        break;
    default:
        break;
    }
    return encoding;
}

/// What a component's plane is multiplied by when it is handed over, and divided by when it comes back.
///
/// One everywhere but the tristimulus scopes in the unit interval, which divide each component by the
/// white's, so the white is one in all three.
[[nodiscard]] double componentScaleOf(const ChannelScopeRequest& request) noexcept
{
    if (request.units != ChannelScopeUnits::UnitInterval || channelScopeEncoding(request.scope) != ChannelSchema::Xyz)
    {
        return 1.0;
    }
    const std::array<double, 3u> white = whitePointTristimulus(request.sourceEncoding.whitePoint);
    return 1.0 / white[channelScopeComponent(request.scope)];
}

/// Whether a source's description is one this scope reads.
[[nodiscard]] Status checkDescription(const ChannelScopeRequest& request, const ImageDescriptor& descriptor,
                                      const char* operation)
{
    if (request.scope == ChannelScope::Whole)
    {
        return Status::success();
    }
    const ChannelSchema schema = descriptor.channelSchema();
    if (schema != ChannelSchema::Rgb && schema != ChannelSchema::Rgba)
    {
        return scopeFailure(ErrorCode::IncompatibleArguments,
                            "a channel or colour component is taken from red, green and blue; any other image's only "
                            "scope is the whole image",
                            operation, "source");
    }
    if (!encodedComponent(request.scope))
    {
        return Status::success();
    }
    const NumericDomain domain = descriptor.numericDomain();
    const bool floating = isFloatingSampleType(descriptor.sampleType());
    if (domain == NumericDomain::RawSensorCode || (floating && domain == NumericDomain::PhysicalScaled))
    {
        return scopeFailure(ErrorCode::IncompatibleArguments,
                            "a colour component is converted from fractions of full scale; a sensor code or a "
                            "floating physical quantity has none, so rescale it to a normalized domain first",
                            operation, "source");
    }
    const std::optional<ColorSpace> stated = colorSpaceOfEncoding(request.sourceEncoding);
    if (descriptor.colorSpace() != ColorSpace::Unspecified && stated.has_value() && descriptor.colorSpace() != *stated)
    {
        return scopeFailure(ErrorCode::IncompatibleArguments,
                            "the source states a colour basis the request's encoding does not name, so the conversion "
                            "would read values that mean something else",
                            operation, "source");
    }
    return Status::success();
}

/// Builds a checked descriptor, or the failure naming the rule that refused it.
[[nodiscard]] Result<ImageDescriptor> describe(const ImageDescriptorRequest& request, const char* operation)
{
    const std::optional<ImageDescriptor> made = ImageDescriptor::create(request);
    if (!made.has_value())
    {
        return Status::failure(ErrorCode::InvalidArgument,
                               StatusDetail{.message = descriptorErrorName(ImageDescriptor::check(request)),
                                            .operation = operation,
                                            .domain = ErrorDomain::Algorithms,
                                            .context = {StatusContextField::ofText(ContextKey::Parameter, "source")}});
    }
    return *made;
}

/// An extent built against limits that admit exactly it, for dimensions already checked elsewhere.
[[nodiscard]] std::optional<ImageExtent> admittedExtent(std::uint32_t width, std::uint32_t height,
                                                        std::uint32_t channels) noexcept
{
    const EngineLimits admitting{.maxImageWidth = width,
                                 .maxImageHeight = height,
                                 .maxChannelCount = channels,
                                 .maxImageBytes = (std::numeric_limits<std::uint64_t>::max)(),
                                 .memoryBudgetBytes = (std::numeric_limits<std::uint64_t>::max)()};
    return ImageExtent::create(width, height, channels, admitting);
}

/// Limits that admit any geometry the engine can address, for a layout over memory this file allocates.
[[nodiscard]] EngineLimits admittingLimits(const ImageExtent& extent) noexcept
{
    return EngineLimits{.maxImageWidth = extent.width(),
                        .maxImageHeight = extent.height(),
                        .maxChannelCount = extent.channelCount(),
                        .maxImageBytes = (std::numeric_limits<std::uint64_t>::max)(),
                        .memoryBudgetBytes = (std::numeric_limits<std::uint64_t>::max)()};
}

/// The description of one plane of a source.
[[nodiscard]] Result<ImageDescriptor> planeDescriptorOf(const ChannelScopeRequest& request,
                                                        const ImageDescriptor& source, const char* operation)
{
    if (request.scope == ChannelScope::Whole)
    {
        return scopeFailure(ErrorCode::IncompatibleArguments, "the whole image is not one plane", operation, "plan");
    }
    const Status described = checkDescription(request, source, operation);
    if (described.failed())
    {
        return described;
    }
    const std::optional<ImageExtent> extent = admittedExtent(source.extent().width(), source.extent().height(), 1u);
    if (!extent.has_value())
    {
        return scopeFailure(ErrorCode::InvalidArgument, "the plane would have a shape no image can have", operation,
                            "source");
    }
    ImageDescriptorRequest plane = source.toRequest();
    plane.extent = *extent;
    plane.channelSchema = ChannelSchema::Mono;
    plane.colorSpace = ColorSpace::Unspecified;
    if (encodedComponent(request.scope))
    {
        plane.sampleType = workingTypeOf(source.sampleType());
        plane.numericDomain =
            numericDomainOfEncoding(scopeEncodingOf(request)).value_or(NumericDomain::EncodedNonlinear);
    }
    return describe(plane, operation);
}

/// The layout request of one plane of a source.
[[nodiscard]] ImageLayoutRequest planeLayoutRequestOf(const ImageDescriptor& plane) noexcept
{
    return ImageLayoutRequest{.extent = plane.extent(),
                              .elementSizeBytes = sampleTypeSizeBytes(plane.sampleType()),
                              .samplesPerPixel = 1u,
                              .planeCount = 1u,
                              .rowAlignmentBytes = kDefaultBufferAlignmentBytes};
}

[[nodiscard]] bool alignedFor(const void* samples, std::uint32_t elementBytes) noexcept
{
    return elementBytes != 0u && reinterpret_cast<std::uintptr_t>(samples) % elementBytes == 0u;
}

/// Whether a source's members describe one another, and its mask what its rule needs.
[[nodiscard]] Status checkSource(const ChannelScopeSource& source, const char* operation)
{
    if (source.descriptor == nullptr || source.samples == nullptr || source.layout == nullptr)
    {
        return scopeFailure(ErrorCode::InvalidArgument, "a channel scope source has a null member", operation,
                            "source");
    }
    const ImageDescriptor& descriptor = *source.descriptor;
    const ImageLayout& layout = *source.layout;
    const std::uint32_t elementBytes = sampleTypeSizeBytes(descriptor.sampleType());
    if (!(layout.extent() == descriptor.extent()) || layout.elementSizeBytes() != elementBytes ||
        layout.planeCount() * layout.samplesPerPixel() != descriptor.extent().channelCount() ||
        !alignedFor(source.samples, elementBytes))
    {
        return scopeFailure(ErrorCode::InvalidArgument,
                            "a channel scope source's layout or pointer does not describe its descriptor", operation,
                            "source");
    }
    const KernelValidityView& validity = source.validity;
    if (!validity.descriptor.needsMask())
    {
        if (validity.mask != nullptr || validity.maskPlane != 0u)
        {
            return scopeFailure(ErrorCode::InvalidArgument, "the validity view supplies an unused mask", operation,
                                "source");
        }
        return Status::success();
    }
    if (validity.mask == nullptr)
    {
        return scopeFailure(ErrorCode::InvalidArgument, "an explicit-mask source supplies no mask", operation,
                            "source");
    }
    const std::uint32_t channels = descriptor.extent().channelCount();
    const bool perChannel = validity.mask->planeCount() == channels && channels > 1u;
    if (validity.mask->width() != descriptor.extent().width() ||
        validity.mask->height() != descriptor.extent().height() ||
        (!perChannel && validity.maskPlane >= validity.mask->planeCount()))
    {
        return scopeFailure(ErrorCode::IncompatibleArguments, "the validity mask does not cover the source", operation,
                            "source");
    }
    return Status::success();
}

/// Whether a destination is laid out as it must be and shares no memory with the source or its mask.
[[nodiscard]] Status checkDestination(const ChannelScopeSource& source, std::uint32_t channels,
                                      std::uint32_t elementBytes, const void* destination,
                                      const ImageLayout& destinationLayout, const char* operation)
{
    if (destination == nullptr || !alignedFor(destination, elementBytes) ||
        destinationLayout.elementSizeBytes() != elementBytes)
    {
        return scopeFailure(ErrorCode::InvalidArgument,
                            "the channel scope destination is null or does not describe its samples", operation,
                            "destination");
    }
    const ImageExtent& extent = source.descriptor->extent();
    if (destinationLayout.extent().width() != extent.width() ||
        destinationLayout.extent().height() != extent.height() || destinationLayout.extent().channelCount() != channels)
    {
        return scopeFailure(ErrorCode::IncompatibleArguments,
                            "the channel scope destination has another extent than what is written", operation,
                            "destinationLayout");
    }
    const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(destination);
    if (regionsOverlap(static_cast<const std::uint8_t*>(source.samples), source.layout->totalBytes(), bytes,
                       destinationLayout.totalBytes()))
    {
        return scopeFailure(ErrorCode::IncompatibleArguments, "the channel scope destination overlaps its source",
                            operation, "destination");
    }
    if (source.validity.mask != nullptr &&
        regionsOverlap(source.validity.mask->data(), source.validity.mask->spannedByteCount(), bytes,
                       destinationLayout.totalBytes()))
    {
        return scopeFailure(ErrorCode::InvalidArgument, "the validity mask overlaps the channel scope destination",
                            operation, "destination");
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

/// How a source's samples are addressed and judged, resolved once.
struct SourceAccess
{
    /// The source, borrowed for the call.
    const ChannelScopeSource* source = nullptr;

    /// Whether the mask gives every channel its own plane.
    bool perChannelMask = false;

    /// Whether any sample can be invalid: a value rule other than all-valid, or a mask.
    bool ruled = false;

    /// The first byte of one channel's row.
    [[nodiscard]] const std::uint8_t* row(std::uint32_t channel, std::uint32_t y) const noexcept
    {
        return static_cast<const std::uint8_t*>(source->samples) +
               plane_detail::channelRowByteOffset(*source->layout, channel, y);
    }

    /// Whether every colour sample of one pixel means something.
    template <typename SampleT> [[nodiscard]] bool pixelValid(std::uint32_t x, std::uint32_t y) const noexcept
    {
        if (!ruled)
        {
            return true;
        }
        const KernelValidityView& validity = source->validity;
        const std::uint32_t stride = source->layout->samplesPerPixel();
        for (std::uint32_t channel = 0u; channel < kColorChannels; ++channel)
        {
            const SampleT value =
                reinterpret_cast<const SampleT*>(row(channel, y))[static_cast<std::size_t>(x) * stride];
            if (!sampleIsValidByValue(validity.descriptor, value))
            {
                return false;
            }
            if (validity.mask != nullptr &&
                !maskBit(*validity.mask, perChannelMask ? channel : validity.maskPlane, x, y))
            {
                return false;
            }
        }
        return true;
    }
};

[[nodiscard]] SourceAccess accessOf(const ChannelScopeSource& source) noexcept
{
    SourceAccess access;
    access.source = &source;
    const std::uint32_t channels = source.descriptor->extent().channelCount();
    access.perChannelMask =
        source.validity.mask != nullptr && source.validity.mask->planeCount() == channels && channels > 1u;
    access.ruled = source.validity.descriptor.rule() != ValidityRule::AllValid || source.validity.mask != nullptr;
    return access;
}

/// What one row costs a walk, for the grain policy.
[[nodiscard]] TaskCost rowCost(std::uint32_t width, std::uint32_t channels, std::uint32_t bytes) noexcept
{
    return TaskCost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * bytes * 2u,
                    .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 4u};
}

/// Copies one channel's row between two images of one representation, each in any arrangement.
template <typename SampleT>
void copyChannelRow(const std::uint8_t* source, const ImageLayout& sourceLayout, std::uint32_t sourceChannel,
                    std::uint8_t* destination, const ImageLayout& destinationLayout, std::uint32_t destinationChannel,
                    std::uint32_t y) noexcept
{
    const SampleT* const from =
        reinterpret_cast<const SampleT*>(source + plane_detail::channelRowByteOffset(sourceLayout, sourceChannel, y));
    SampleT* const to = reinterpret_cast<SampleT*>(
        destination + plane_detail::channelRowByteOffset(destinationLayout, destinationChannel, y));
    const std::uint32_t width = sourceLayout.extent().width();
    const std::size_t fromStride = sourceLayout.samplesPerPixel();
    const std::size_t toStride = destinationLayout.samplesPerPixel();
    if (fromStride == 1u && toStride == 1u)
    {
        std::memcpy(to, from, static_cast<std::size_t>(width) * sizeof(SampleT));
        return;
    }
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        to[x * toStride] = from[x * fromStride];
    }
}

/// Copies channels between two images of one representation and extent: every channel `skip` is not,
/// each to the same index; `skip` past the channel count copies them all.
[[nodiscard]] Status copyChannels(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                  const ImageLayout& sourceLayout, void* destination,
                                  const ImageLayout& destinationLayout, std::uint32_t skip)
{
    const std::uint32_t channels = sourceLayout.extent().channelCount();
    const std::uint8_t* const from = static_cast<const std::uint8_t*>(source);
    std::uint8_t* const to = static_cast<std::uint8_t*>(destination);
    return parallelFor(execution, sourceLayout.extent().height(),
                       rowCost(sourceLayout.extent().width(), channels, sampleTypeSizeBytes(sampleType)),
                       [&](TaskRange range) -> Status {
                           // A checked descriptor names one of the eight representations, so the dispatch runs.
                           (void)dispatchSampleType(sampleType, [&]<typename SampleT>() noexcept {
                               for (std::uint64_t y = range.begin; y < range.end; ++y)
                               {
                                   for (std::uint32_t channel = 0u; channel < channels; ++channel)
                                   {
                                       if (channel != skip)
                                       {
                                           copyChannelRow<SampleT>(from, sourceLayout, channel, to, destinationLayout,
                                                                   channel, static_cast<std::uint32_t>(y));
                                       }
                                   }
                               }
                           });
                           return Status::success();
                       });
}

/// Copies one channel of an image into a one-plane image of the same representation.
[[nodiscard]] Status copyOneChannel(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                    const ImageLayout& sourceLayout, std::uint32_t channel, void* destination,
                                    const ImageLayout& destinationLayout)
{
    const std::uint8_t* const from = static_cast<const std::uint8_t*>(source);
    std::uint8_t* const to = static_cast<std::uint8_t*>(destination);
    return parallelFor(execution, sourceLayout.extent().height(),
                       rowCost(sourceLayout.extent().width(), 1u, sampleTypeSizeBytes(sampleType)),
                       [&](TaskRange range) -> Status {
                           (void)dispatchSampleType(sampleType, [&]<typename SampleT>() noexcept {
                               for (std::uint64_t y = range.begin; y < range.end; ++y)
                               {
                                   copyChannelRow<SampleT>(from, sourceLayout, channel, to, destinationLayout, 0u,
                                                           static_cast<std::uint32_t>(y));
                               }
                           });
                           return Status::success();
                       });
}

/// Copies a one-plane image into one channel of another of the same representation.
[[nodiscard]] Status copyIntoChannel(const ExecutionContext& execution, SampleType sampleType, const void* plane,
                                     const ImageLayout& planeLayout, void* destination,
                                     const ImageLayout& destinationLayout, std::uint32_t channel)
{
    const std::uint8_t* const from = static_cast<const std::uint8_t*>(plane);
    std::uint8_t* const to = static_cast<std::uint8_t*>(destination);
    return parallelFor(execution, planeLayout.extent().height(),
                       rowCost(planeLayout.extent().width(), 1u, sampleTypeSizeBytes(sampleType)),
                       [&](TaskRange range) -> Status {
                           (void)dispatchSampleType(sampleType, [&]<typename SampleT>() noexcept {
                               for (std::uint64_t y = range.begin; y < range.end; ++y)
                               {
                                   copyChannelRow<SampleT>(from, planeLayout, 0u, to, destinationLayout, channel,
                                                           static_cast<std::uint32_t>(y));
                               }
                           });
                           return Status::success();
                       });
}

/// Planes of one representation laid out by one planar layout, wherever their memory lies.
struct PlaneSet
{
    /// The first byte of the first plane.
    std::uint8_t* base = nullptr;

    /// Their geometry.
    std::optional<ImageLayout> layout;

    /// The first byte of one plane.
    [[nodiscard]] std::uint8_t* plane(std::uint32_t index) const noexcept
    {
        return base + static_cast<std::uint64_t>(index) * layout->planeStrideBytes();
    }
};

/// Context memory and the planes it holds.
struct WorkingBlock : PlaneSet
{
    /// The memory, returned to the execution's resource when this goes.
    plane_detail::ScopedContextMemory memory;
};

/// The layout of a planar block of `planes` planes of one representation: what the scope works in, and what a
/// preparation keeps, so a preparation's planes lie as the scope's own would.
[[nodiscard]] Result<ImageLayout> blockLayoutOf(SampleType sampleType, std::uint32_t width, std::uint32_t height,
                                                std::uint32_t planes, const char* operation)
{
    const std::optional<ImageExtent> extent = admittedExtent(width, height, planes);
    if (!extent.has_value())
    {
        return scopeFailure(ErrorCode::InvalidArgument, "the working image would have a shape no image can have",
                            operation, "source");
    }
    const std::optional<ImageLayout> layout =
        ImageLayout::create(ImageLayoutRequest{.extent = *extent,
                                               .elementSizeBytes = sampleTypeSizeBytes(sampleType),
                                               .samplesPerPixel = 1u,
                                               .planeCount = planes,
                                               .rowAlignmentBytes = kDefaultBufferAlignmentBytes},
                            admittingLimits(*extent));
    if (!layout.has_value())
    {
        return scopeFailure(ErrorCode::InvalidArgument, "the working image's layout cannot be described", operation,
                            "source");
    }
    return *layout;
}

/// Takes a planar block of `planes` planes of one representation from the execution's memory resource.
[[nodiscard]] Result<WorkingBlock> takeBlock(const ExecutionContext& execution, SampleType sampleType,
                                             std::uint32_t width, std::uint32_t height, std::uint32_t planes,
                                             const char* operation)
{
    const Result<ImageLayout> layout = blockLayoutOf(sampleType, width, height, planes, operation);
    if (!layout.hasValue())
    {
        return layout.status();
    }
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, layout->totalBytes(), kDefaultBufferAlignmentBytes, operation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    WorkingBlock block;
    block.memory = std::move(*memory);
    block.base = static_cast<std::uint8_t*>(block.memory.data());
    block.layout = *layout;
    return block;
}

/// The layout of one plane of a planar layout, with its row stride.
[[nodiscard]] std::optional<ImageLayout> onePlaneOf(const ImageLayout& layout) noexcept
{
    const std::optional<ImageExtent> extent = admittedExtent(layout.extent().width(), layout.extent().height(), 1u);
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    const ImageLayoutRequest request{.extent = *extent,
                                     .elementSizeBytes = layout.elementSizeBytes(),
                                     .samplesPerPixel = 1u,
                                     .planeCount = 1u,
                                     .rowAlignmentBytes = layout.elementSizeBytes()};
    const std::uint64_t planeBytes = layout.rowStrideBytes() * (static_cast<std::uint64_t>(extent->height()) - 1u) +
                                     static_cast<std::uint64_t>(extent->width()) * layout.elementSizeBytes();
    return ImageLayout::createWithStrides(request, layout.rowStrideBytes(), planeBytes, admittingLimits(*extent));
}

/// Promotes the source's red, green and blue into the working block's first three planes and counts the
/// pixels with no colour to convert.
template <typename WorkT>
[[nodiscard]] Status promoteColor(const ExecutionContext& execution, const SourceAccess& access, const PlaneSet& block,
                                  std::atomic<std::uint64_t>& invalidPixels)
{
    const ChannelScopeSource& source = *access.source;
    const std::uint32_t width = source.descriptor->extent().width();
    const std::uint32_t stride = source.layout->samplesPerPixel();
    const ConversionPolicy policy{};
    return parallelFor(
        execution, source.descriptor->extent().height(),
        rowCost(width, kColorChannels, sampleTypeSizeBytes(source.descriptor->sampleType())),
        [&](TaskRange range) -> Status {
            std::uint64_t invalid = 0u;
            (void)dispatchSampleType(source.descriptor->sampleType(), [&]<typename SampleT>() noexcept {
                for (std::uint64_t row = range.begin; row < range.end; ++row)
                {
                    const std::uint32_t y = static_cast<std::uint32_t>(row);
                    for (std::uint32_t channel = 0u; channel < kColorChannels; ++channel)
                    {
                        const SampleT* const from = reinterpret_cast<const SampleT*>(access.row(channel, y));
                        WorkT* const to = reinterpret_cast<WorkT*>(
                            block.plane(channel) + static_cast<std::uint64_t>(y) * block.layout->rowStrideBytes());
                        for (std::uint32_t x = 0u; x < width; ++x)
                        {
                            to[x] =
                                convertSample<SampleT, WorkT>(from[static_cast<std::size_t>(x) * stride], policy).value;
                        }
                    }
                    if (access.ruled)
                    {
                        for (std::uint32_t x = 0u; x < width; ++x)
                        {
                            invalid += access.pixelValid<SampleT>(x, y) ? 0u : 1u;
                        }
                    }
                }
            });
            invalidPixels.fetch_add(invalid, std::memory_order_relaxed);
            return Status::success();
        });
}

/// Writes the plane an operation reads: the component in its units, a quiet NaN where the pixel has no
/// colour. Reads the component from the working block's plane `component` and writes `plane`, which may
/// be that very plane.
template <typename WorkT>
[[nodiscard]] Status preparePlane(const ExecutionContext& execution, const SourceAccess& access, const PlaneSet& block,
                                  std::uint32_t component, double scale, std::uint8_t* plane,
                                  const ImageLayout& planeLayout)
{
    const ChannelScopeSource& source = *access.source;
    const std::uint32_t width = source.descriptor->extent().width();
    return parallelFor(
        execution, source.descriptor->extent().height(), rowCost(width, 1u, sizeof(WorkT)),
        [&](TaskRange range) -> Status {
            (void)dispatchSampleType(source.descriptor->sampleType(), [&]<typename SampleT>() noexcept {
                for (std::uint64_t row = range.begin; row < range.end; ++row)
                {
                    const std::uint32_t y = static_cast<std::uint32_t>(row);
                    const WorkT* const from = reinterpret_cast<const WorkT*>(
                        block.plane(component) + static_cast<std::uint64_t>(y) * block.layout->rowStrideBytes());
                    WorkT* const to =
                        reinterpret_cast<WorkT*>(plane + static_cast<std::uint64_t>(y) * planeLayout.rowStrideBytes());
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        WorkT value = from[x];
                        if (scale != 1.0)
                        {
                            value = static_cast<WorkT>(static_cast<double>(value) * scale);
                        }
                        if (access.ruled && !access.pixelValid<SampleT>(x, y))
                        {
                            value = std::numeric_limits<WorkT>::quiet_NaN();
                        }
                        to[x] = value;
                    }
                }
            });
            return Status::success();
        });
}

/// Writes a processed plane back into the working block's plane `component`, undoing its units' scale.
template <typename WorkT>
[[nodiscard]] Status restorePlane(const ExecutionContext& execution, const PlaneSet& block, std::uint32_t component,
                                  double scale, const std::uint8_t* plane, const ImageLayout& planeLayout)
{
    const std::uint32_t width = planeLayout.extent().width();
    return parallelFor(
        execution, planeLayout.extent().height(), rowCost(width, 1u, sizeof(WorkT)), [&](TaskRange range) -> Status {
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                const WorkT* const from = reinterpret_cast<const WorkT*>(plane + y * planeLayout.rowStrideBytes());
                WorkT* const to = reinterpret_cast<WorkT*>(block.plane(component) + y * block.layout->rowStrideBytes());
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    to[x] = scale == 1.0 ? from[x] : static_cast<WorkT>(static_cast<double>(from[x]) / scale);
                }
            }
            return Status::success();
        });
}

/// Writes the recomposed image: red, green and blue narrowed from the working block, alpha copied, and a
/// pixel with no colour copied whole from the source.
template <typename WorkT>
[[nodiscard]] Status writeRecomposed(const ExecutionContext& execution, const SourceAccess& access,
                                     const PlaneSet& block, std::uint8_t* destination,
                                     const ImageLayout& destinationLayout)
{
    const ChannelScopeSource& source = *access.source;
    const std::uint32_t width = source.descriptor->extent().width();
    const std::uint32_t channels = source.descriptor->extent().channelCount();
    const std::uint32_t sourceStride = source.layout->samplesPerPixel();
    const std::uint32_t destinationStride = destinationLayout.samplesPerPixel();
    const ConversionPolicy policy{};
    return parallelFor(
        execution, source.descriptor->extent().height(),
        rowCost(width, channels, sampleTypeSizeBytes(source.descriptor->sampleType())), [&](TaskRange range) -> Status {
            (void)dispatchSampleType(source.descriptor->sampleType(), [&]<typename SampleT>() noexcept {
                for (std::uint64_t row = range.begin; row < range.end; ++row)
                {
                    const std::uint32_t y = static_cast<std::uint32_t>(row);
                    for (std::uint32_t channel = 0u; channel < channels; ++channel)
                    {
                        const SampleT* const from = reinterpret_cast<const SampleT*>(access.row(channel, y));
                        SampleT* const to = reinterpret_cast<SampleT*>(
                            destination + plane_detail::channelRowByteOffset(destinationLayout, channel, y));
                        if (channel >= kColorChannels)
                        {
                            for (std::uint32_t x = 0u; x < width; ++x)
                            {
                                to[static_cast<std::size_t>(x) * destinationStride] =
                                    from[static_cast<std::size_t>(x) * sourceStride];
                            }
                            continue;
                        }
                        const WorkT* const working = reinterpret_cast<const WorkT*>(
                            block.plane(channel) + static_cast<std::uint64_t>(y) * block.layout->rowStrideBytes());
                        for (std::uint32_t x = 0u; x < width; ++x)
                        {
                            to[static_cast<std::size_t>(x) * destinationStride] =
                                access.ruled && !access.pixelValid<SampleT>(x, y)
                                    ? from[static_cast<std::size_t>(x) * sourceStride]
                                    : convertSample<WorkT, SampleT>(working[x], policy).value;
                        }
                    }
                }
            });
            return Status::success();
        });
}

/// The validity view of a plane of another encoding: finite-only when any pixel can have been marked.
[[nodiscard]] KernelValidityView encodedPlaneValidity(const SourceAccess& access) noexcept
{
    if (!access.ruled)
    {
        return KernelValidityView{};
    }
    return KernelValidityView{
        .descriptor = ValidityDescriptor::create(ValidityDescriptorRequest{.rule = ValidityRule::FiniteOnly})
                          .value_or(ValidityDescriptor{})};
}

/// The validity view of a stored channel: the source's own rule and the mask plane of that channel.
[[nodiscard]] KernelValidityView storedPlaneValidity(const SourceAccess& access, std::uint32_t channel) noexcept
{
    const KernelValidityView& validity = access.source->validity;
    return KernelValidityView{.descriptor = validity.descriptor,
                              .mask = validity.mask,
                              .maskPlane = access.perChannelMask ? channel : validity.maskPlane};
}

/// Runs the caller's operation across the boundary, and then checks the execution was not stopped.
[[nodiscard]] Status runOperation(const ExecutionContext& execution, ChannelScopeOperation& operation,
                                  const ChannelScopeInput& input, const ChannelScopeOutput& output)
{
    Status returned = Status::success();
    const Status thrown =
        callAcrossBoundary(kOperationBoundary, [&] { returned = operation.run(execution, input, output); });
    if (thrown.failed())
    {
        return thrown;
    }
    if (returned.failed())
    {
        return returned;
    }
    return execution.checkCancellation();
}

/// Everything a scope checks before it touches a sample.
[[nodiscard]] Status checkCall(const ExecutionContext& execution, const ChannelScopePlan& plan,
                               const ChannelScopeSource& source, const char* operation)
{
    if (!execution.valid())
    {
        return scopeFailure(ErrorCode::NotInitialized, "a channel scope was given an empty execution context",
                            operation, "execution");
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
    const Status checked = checkSource(source, operation);
    if (checked.failed())
    {
        return checked;
    }
    return checkDescription(plan.request(), *source.descriptor, operation);
}

/// The layout of the working block's three colour planes.
[[nodiscard]] std::optional<ImageLayout> colorPlanesOf(const PlaneSet& block) noexcept
{
    const std::optional<ImageExtent> extent =
        admittedExtent(block.layout->extent().width(), block.layout->extent().height(), kColorChannels);
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    return ImageLayout::createWithStrides(ImageLayoutRequest{.extent = *extent,
                                                             .elementSizeBytes = block.layout->elementSizeBytes(),
                                                             .samplesPerPixel = 1u,
                                                             .planeCount = kColorChannels,
                                                             .rowAlignmentBytes = block.layout->elementSizeBytes()},
                                          block.layout->rowStrideBytes(), block.layout->planeStrideBytes(),
                                          admittingLimits(*extent));
}

/// Counts the pixels with no colour to convert, for a source converted where it lies.
[[nodiscard]] Status countInvalidPixels(const ExecutionContext& execution, const SourceAccess& access,
                                        std::atomic<std::uint64_t>& invalidPixels)
{
    const ChannelScopeSource& source = *access.source;
    const std::uint32_t width = source.descriptor->extent().width();
    return parallelFor(execution, source.descriptor->extent().height(),
                       rowCost(width, kColorChannels, sampleTypeSizeBytes(source.descriptor->sampleType())),
                       [&](TaskRange range) -> Status {
                           std::uint64_t invalid = 0u;
                           (void)dispatchSampleType(source.descriptor->sampleType(), [&]<typename SampleT>() noexcept {
                               for (std::uint64_t row = range.begin; row < range.end; ++row)
                               {
                                   for (std::uint32_t x = 0u; x < width; ++x)
                                   {
                                       invalid +=
                                           access.pixelValid<SampleT>(x, static_cast<std::uint32_t>(row)) ? 0u : 1u;
                                   }
                               }
                           });
                           invalidPixels.fetch_add(invalid, std::memory_order_relaxed);
                           return Status::success();
                       });
}

/// Whether the colour transform can read the source, or write the destination, where it lies: the source holds
/// red, green and blue alone, in the working representation.
template <typename WorkT> [[nodiscard]] bool convertsInPlace(const ChannelScopeSource& source) noexcept
{
    return source.descriptor->sampleType() == kSampleTypeOf<WorkT> &&
           source.descriptor->extent().channelCount() == kColorChannels;
}

/// Converts the source, in the working block's first three planes, into the scope's encoding: straight from the
/// source when it is red, green and blue in the working representation, through a promotion otherwise. Counts the
/// pixels with no colour to convert.
template <typename WorkT>
[[nodiscard]] Status convertToEncoding(const ExecutionContext& execution, const ChannelScopePlan& plan,
                                       const SourceAccess& access, const PlaneSet& block,
                                       std::atomic<std::uint64_t>& invalidPixels, const char* operation)
{
    const std::optional<ImageLayout> colorLayout = colorPlanesOf(block);
    if (!colorLayout.has_value())
    {
        return scopeFailure(ErrorCode::Internal, "the working image's colour planes cannot be described", operation,
                            "source");
    }
    const ChannelScopeSource& source = *access.source;
    if (convertsInPlace<WorkT>(source))
    {
        if (access.ruled)
        {
            const Status counted = countInvalidPixels(execution, access, invalidPixels);
            if (counted.failed())
            {
                return counted;
            }
        }
        return convertImageColor(execution, *plan.forward(), kSampleTypeOf<WorkT>, source.samples, *source.layout,
                                 block.plane(0u), *colorLayout);
    }
    const Status promoted = promoteColor<WorkT>(execution, access, block, invalidPixels);
    if (promoted.failed())
    {
        return promoted;
    }
    return convertImageColor(execution, *plan.forward(), kSampleTypeOf<WorkT>, block.plane(0u), *colorLayout,
                             block.plane(0u), *colorLayout);
}

/// Extracts, processes and recomposes a component of another encoding.
///
/// The block holds the encoding's three planes and a fourth, the copy of the component the operation reads; the
/// operation writes the component's own plane, which therefore starts as that copy and needs no writing back.
template <typename WorkT>
[[nodiscard]] Result<ChannelScopeResult>
applyEncoded(const ExecutionContext& execution, const ChannelScopePlan& plan, const ChannelScopeSource& source,
             ChannelScopeOperation& operation, std::uint8_t* destination, const ImageLayout& destinationLayout)
{
    const ChannelScopeRequest& request = plan.request();
    const SourceAccess access = accessOf(source);
    const std::uint32_t width = source.descriptor->extent().width();
    const std::uint32_t height = source.descriptor->extent().height();
    Result<WorkingBlock> block =
        takeBlock(execution, kSampleTypeOf<WorkT>, width, height, kColorChannels + 1u, kApplyOperation);
    if (!block.hasValue())
    {
        return block.status();
    }
    const std::optional<ImageLayout> planeLayout = onePlaneOf(*block->layout);
    const std::optional<ImageLayout> colorLayout = colorPlanesOf(*block);
    const Result<ImageDescriptor> planeDescriptor = planeDescriptorOf(request, *source.descriptor, kApplyOperation);
    if (!planeLayout.has_value() || !colorLayout.has_value() || !planeDescriptor.hasValue())
    {
        return planeDescriptor.hasValue() ? scopeFailure(ErrorCode::Internal, "the working planes cannot be described",
                                                         kApplyOperation, "source")
                                          : planeDescriptor.status();
    }

    const std::uint32_t component = channelScopeComponent(request.scope);
    const double scale = componentScaleOf(request);
    std::atomic<std::uint64_t> invalidPixels{0u};
    const Status converted = convertToEncoding<WorkT>(execution, plan, access, *block, invalidPixels, kApplyOperation);
    if (converted.failed())
    {
        return converted;
    }
    // The component in its units and with its pixels of no colour marked, in place; there is nothing to do when
    // there is neither a scale nor a rule.
    if (access.ruled || scale != 1.0)
    {
        const Status prepared =
            preparePlane<WorkT>(execution, access, *block, component, scale, block->plane(component), *planeLayout);
        if (prepared.failed())
        {
            return prepared;
        }
    }
    const Status copied = copyOneChannel(execution, kSampleTypeOf<WorkT>, block->plane(component), *planeLayout, 0u,
                                         block->plane(kColorChannels), *planeLayout);
    if (copied.failed())
    {
        return copied;
    }

    const ChannelScopeInput input{.descriptor = &*planeDescriptor,
                                  .samples = block->plane(kColorChannels),
                                  .layout = &*planeLayout,
                                  .validity = encodedPlaneValidity(access)};
    const ChannelScopeOutput output{
        .descriptor = &*planeDescriptor, .samples = block->plane(component), .layout = &*planeLayout};
    const Status ran = runOperation(execution, operation, input, output);
    if (ran.failed())
    {
        return ran;
    }

    if (scale != 1.0)
    {
        const Status restored =
            restorePlane<WorkT>(execution, *block, component, scale, block->plane(component), *planeLayout);
        if (restored.failed())
        {
            return restored;
        }
    }
    const std::uint64_t invalid = invalidPixels.load(std::memory_order_relaxed);
    // With every pixel converted and the source red, green and blue in the working representation, the inverse
    // conversion writes the destination itself.
    if (invalid == 0u && convertsInPlace<WorkT>(source))
    {
        const Status back = convertImageColor(execution, *plan.inverse(), kSampleTypeOf<WorkT>, block->plane(0u),
                                              *colorLayout, destination, destinationLayout);
        if (back.failed())
        {
            return back;
        }
        return ChannelScopeResult{.scope = request.scope, .invalidPixels = 0u};
    }
    const Status back = convertImageColor(execution, *plan.inverse(), kSampleTypeOf<WorkT>, block->plane(0u),
                                          *colorLayout, block->plane(0u), *colorLayout);
    if (back.failed())
    {
        return back;
    }
    const Status written = writeRecomposed<WorkT>(execution, access, *block, destination, destinationLayout);
    if (written.failed())
    {
        return written;
    }
    return ChannelScopeResult{.scope = request.scope, .invalidPixels = invalid};
}

/// Extracts, processes and recomposes a stored channel. The output plane starts as a copy of the channel unless the
/// operation writes every sample of it, and the other channels are copied unless the destination holds them already.
[[nodiscard]] Result<ChannelScopeResult> applyStored(const ExecutionContext& execution, const ChannelScopePlan& plan,
                                                     const ChannelScopeSource& source, ChannelScopeOperation& operation,
                                                     std::uint8_t* destination, const ImageLayout& destinationLayout,
                                                     ChannelScopeDestination holds)
{
    const ChannelScopeRequest& request = plan.request();
    const SourceAccess access = accessOf(source);
    const SampleType sampleType = source.descriptor->sampleType();
    const ImageLayout& sourceLayout = *source.layout;
    const std::uint32_t channel = channelScopeComponent(request.scope);
    // A planar source's channel is read where it lies; otherwise it is copied into a plane of its own.
    const bool inPlace = sourceLayout.samplesPerPixel() == 1u;
    Result<WorkingBlock> block = takeBlock(execution, sampleType, sourceLayout.extent().width(),
                                           sourceLayout.extent().height(), inPlace ? 1u : 2u, kApplyOperation);
    if (!block.hasValue())
    {
        return block.status();
    }
    const std::optional<ImageLayout> scratchLayout = onePlaneOf(*block->layout);
    const std::optional<ImageLayout> viewLayout = inPlace ? onePlaneOf(sourceLayout) : scratchLayout;
    const Result<ImageDescriptor> planeDescriptor = planeDescriptorOf(request, *source.descriptor, kApplyOperation);
    if (!scratchLayout.has_value() || !viewLayout.has_value() || !planeDescriptor.hasValue())
    {
        return planeDescriptor.hasValue() ? scopeFailure(ErrorCode::Internal, "the channel's plane cannot be described",
                                                         kApplyOperation, "source")
                                          : planeDescriptor.status();
    }
    const void* inputSamples = nullptr;
    if (inPlace)
    {
        inputSamples = static_cast<const std::uint8_t*>(source.samples) +
                       static_cast<std::uint64_t>(channel) * sourceLayout.planeStrideBytes();
    }
    else
    {
        const Status copied = copyOneChannel(execution, sampleType, source.samples, sourceLayout, channel,
                                             block->plane(1u), *scratchLayout);
        if (copied.failed())
        {
            return copied;
        }
        inputSamples = block->plane(1u);
    }
    if (!operation.writesEveryOutputSample())
    {
        const Status seeded =
            copyOneChannel(execution, sampleType, inputSamples, *viewLayout, 0u, block->plane(0u), *scratchLayout);
        if (seeded.failed())
        {
            return seeded;
        }
    }

    const ChannelScopeInput input{.descriptor = &*planeDescriptor,
                                  .samples = inputSamples,
                                  .layout = &*viewLayout,
                                  .validity = storedPlaneValidity(access, channel)};
    const ChannelScopeOutput output{
        .descriptor = &*planeDescriptor, .samples = block->plane(0u), .layout = &*scratchLayout};
    const Status ran = runOperation(execution, operation, input, output);
    if (ran.failed())
    {
        return ran;
    }

    if (holds != ChannelScopeDestination::HoldsSource)
    {
        const Status others =
            copyChannels(execution, sampleType, source.samples, sourceLayout, destination, destinationLayout, channel);
        if (others.failed())
        {
            return others;
        }
    }
    const Status written = copyIntoChannel(execution, sampleType, block->plane(0u), *scratchLayout, destination,
                                           destinationLayout, channel);
    if (written.failed())
    {
        return written;
    }
    return ChannelScopeResult{.scope = request.scope, .invalidPixels = 0u};
}

/// Runs the operation over the whole image, whose output is the destination. The destination becomes a copy of the
/// source first only when it holds something else and the operation may leave a sample alone.
[[nodiscard]] Result<ChannelScopeResult> applyWhole(const ExecutionContext& execution, const ChannelScopeSource& source,
                                                    ChannelScopeOperation& operation, std::uint8_t* destination,
                                                    const ImageLayout& destinationLayout, ChannelScopeDestination holds)
{
    if (holds != ChannelScopeDestination::HoldsSource && !operation.writesEveryOutputSample())
    {
        const std::uint32_t everyChannel = source.descriptor->extent().channelCount();
        const Status copied = copyChannels(execution, source.descriptor->sampleType(), source.samples, *source.layout,
                                           destination, destinationLayout, everyChannel);
        if (copied.failed())
        {
            return copied;
        }
    }
    const ChannelScopeInput input{.descriptor = source.descriptor,
                                  .samples = source.samples,
                                  .layout = source.layout,
                                  .validity = source.validity};
    const ChannelScopeOutput output{
        .descriptor = source.descriptor, .samples = destination, .layout = &destinationLayout};
    const Status ran = runOperation(execution, operation, input, output);
    if (ran.failed())
    {
        return ran;
    }
    return ChannelScopeResult{.scope = ChannelScope::Whole, .invalidPixels = 0u};
}

/// Writes a component of another encoding into a caller's plane.
template <typename WorkT>
[[nodiscard]] Result<ChannelScopeResult> extractEncoded(const ExecutionContext& execution, const ChannelScopePlan& plan,
                                                        const ChannelScopeSource& source, std::uint8_t* destination,
                                                        const ImageLayout& destinationLayout)
{
    const SourceAccess access = accessOf(source);
    Result<WorkingBlock> block = takeBlock(execution, kSampleTypeOf<WorkT>, source.descriptor->extent().width(),
                                           source.descriptor->extent().height(), kColorChannels, kExtractOperation);
    if (!block.hasValue())
    {
        return block.status();
    }
    std::atomic<std::uint64_t> invalidPixels{0u};
    const Status converted =
        convertToEncoding<WorkT>(execution, plan, access, *block, invalidPixels, kExtractOperation);
    if (converted.failed())
    {
        return converted;
    }
    const Status prepared = preparePlane<WorkT>(execution, access, *block, channelScopeComponent(plan.request().scope),
                                                componentScaleOf(plan.request()), destination, destinationLayout);
    if (prepared.failed())
    {
        return prepared;
    }
    return ChannelScopeResult{.scope = plan.request().scope,
                              .invalidPixels = invalidPixels.load(std::memory_order_relaxed)};
}

/// Whether two requests ask for the same scope of the same source encoding in the same units.
[[nodiscard]] bool sameRequest(const ChannelScopeRequest& left, const ChannelScopeRequest& right)
{
    const ColorEncoding& one = left.sourceEncoding;
    const ColorEncoding& other = right.sourceEncoding;
    return left.scope == right.scope && left.units == right.units && left.yCbCr == right.yCbCr &&
           one.schema == other.schema && one.primaries == other.primaries && one.transfer == other.transfer &&
           one.transferGamma == other.transferGamma && one.whitePoint == other.whitePoint &&
           one.perceptual == other.perceptual && one.yCbCr == other.yCbCr && one.hue == other.hue;
}

/// Whether the preparation a source carries was made for this plan, at the source's size and in its working
/// representation.
[[nodiscard]] Status checkPreparation(const ChannelScopePlan& plan, const ChannelScopeSource& source,
                                      const char* operation)
{
    const ChannelScopePreparation& prepared = *source.prepared;
    if (!prepared.valid())
    {
        return scopeFailure(ErrorCode::IncompatibleArguments, "the source carries a preparation that holds nothing",
                            operation, "source");
    }
    if (!sameRequest(prepared.request(), plan.request()))
    {
        return scopeFailure(ErrorCode::IncompatibleArguments,
                            "the source carries a preparation made for another scope, source encoding or units",
                            operation, "source");
    }
    const ImageExtent& extent = source.descriptor->extent();
    if (prepared.width() != extent.width() || prepared.height() != extent.height() ||
        prepared.sampleType() != workingTypeOf(source.descriptor->sampleType()))
    {
        return scopeFailure(ErrorCode::IncompatibleArguments,
                            "the source carries a preparation of another size or working representation", operation,
                            "source");
    }
    return Status::success();
}

/// A preparation's planes, for reading.
[[nodiscard]] PlaneSet planesOf(const ChannelScopePreparation& prepared) noexcept
{
    return PlaneSet{.base = const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(prepared.samples())),
                    .layout = prepared.layout()};
}

/// Processes and recomposes a component of another encoding from the source's preparation.
///
/// The operation reads the preparation's component where it lies and writes the component's plane of the scope's
/// block, which starts as that component unless the operation writes every sample of it; the conversion back reads
/// that plane and the preparation's other two where they lie, so neither is copied.
template <typename WorkT>
[[nodiscard]] Result<ChannelScopeResult>
applyPrepared(const ExecutionContext& execution, const ChannelScopePlan& plan, const ChannelScopeSource& source,
              ChannelScopeOperation& operation, std::uint8_t* destination, const ImageLayout& destinationLayout)
{
    const ChannelScopeRequest& request = plan.request();
    const ChannelScopePreparation& prepared = *source.prepared;
    const SourceAccess access = accessOf(source);
    const std::uint32_t component = channelScopeComponent(request.scope);
    const double scale = componentScaleOf(request);
    const std::uint64_t invalid = prepared.invalidPixels();
    // With every pixel converted and the source red, green and blue in the working representation, the conversion
    // back writes the destination itself, and the block holds no plane past the operation's.
    const bool direct = invalid == 0u && convertsInPlace<WorkT>(source);
    Result<WorkingBlock> block =
        takeBlock(execution, kSampleTypeOf<WorkT>, source.descriptor->extent().width(),
                  source.descriptor->extent().height(), direct ? component + 1u : kColorChannels, kApplyOperation);
    if (!block.hasValue())
    {
        return block.status();
    }
    const PlaneSet kept = planesOf(prepared);
    const std::optional<ImageLayout> planeLayout = onePlaneOf(*block->layout);
    const std::optional<ImageLayout> keptLayout =
        kept.layout.has_value() ? onePlaneOf(*kept.layout) : std::optional<ImageLayout>{};
    const Result<ImageDescriptor> planeDescriptor = planeDescriptorOf(request, *source.descriptor, kApplyOperation);
    if (!planeLayout.has_value() || !keptLayout.has_value() || !planeDescriptor.hasValue() ||
        keptLayout->rowStrideBytes() != planeLayout->rowStrideBytes())
    {
        return planeDescriptor.hasValue() ? scopeFailure(ErrorCode::Internal, "the working planes cannot be described",
                                                         kApplyOperation, "source")
                                          : planeDescriptor.status();
    }

    std::uint8_t* const output = block->plane(component);
    const std::uint8_t* const input = kept.plane(component);
    if (!operation.writesEveryOutputSample())
    {
        const Status seeded =
            copyOneChannel(execution, kSampleTypeOf<WorkT>, input, *keptLayout, 0u, output, *planeLayout);
        if (seeded.failed())
        {
            return seeded;
        }
    }
    const ChannelScopeInput in{.descriptor = &*planeDescriptor,
                               .samples = input,
                               .layout = &*keptLayout,
                               .validity = encodedPlaneValidity(access)};
    const ChannelScopeOutput out{.descriptor = &*planeDescriptor, .samples = output, .layout = &*planeLayout};
    const Status ran = runOperation(execution, operation, in, out);
    if (ran.failed())
    {
        return ran;
    }
    if (scale != 1.0)
    {
        const Status restored = restorePlane<WorkT>(execution, *block, component, scale, output, *planeLayout);
        if (restored.failed())
        {
            return restored;
        }
    }

    std::array<const void*, kColorChannels> planes{kept.plane(0u), kept.plane(1u), kept.plane(2u)};
    planes[component] = output;
    if (direct)
    {
        const Status back = color_detail::convertImageColorFromPlanes(
            execution, *plan.inverse(), kSampleTypeOf<WorkT>, planes, *planeLayout, destination, destinationLayout);
        if (back.failed())
        {
            return back;
        }
        return ChannelScopeResult{.scope = request.scope, .invalidPixels = 0u};
    }
    const std::optional<ImageLayout> colorLayout = colorPlanesOf(*block);
    if (!colorLayout.has_value())
    {
        return scopeFailure(ErrorCode::Internal, "the working image's colour planes cannot be described",
                            kApplyOperation, "source");
    }
    const Status back = color_detail::convertImageColorFromPlanes(execution, *plan.inverse(), kSampleTypeOf<WorkT>,
                                                                  planes, *planeLayout, block->plane(0u), *colorLayout);
    if (back.failed())
    {
        return back;
    }
    const Status written = writeRecomposed<WorkT>(execution, access, *block, destination, destinationLayout);
    if (written.failed())
    {
        return written;
    }
    return ChannelScopeResult{.scope = request.scope, .invalidPixels = invalid};
}

} // namespace

// ---- The preparation.

ChannelScopePreparation::ChannelScopePreparation() noexcept = default;

ChannelScopePreparation::~ChannelScopePreparation() = default;

ChannelScopePreparation::ChannelScopePreparation(ChannelScopePreparation&& other) noexcept = default;

ChannelScopePreparation& ChannelScopePreparation::operator=(ChannelScopePreparation&& other) noexcept = default;

bool ChannelScopePreparation::valid() const noexcept
{
    return m_planes.valid();
}

const void* ChannelScopePreparation::samples() const noexcept
{
    return m_planes.valid() ? m_planes.data() : nullptr;
}

std::optional<ImageLayout> ChannelScopePreparation::layout() const noexcept
{
    return m_planes.valid() ? m_planes.layout() : std::optional<ImageLayout>{};
}

std::uint64_t ChannelScopePreparation::byteCount() const noexcept
{
    return m_planes.valid() ? m_planes.capacityBytes() : 0u;
}

Result<ChannelScopePreparation> prepareChannelScope(const EngineContext& engine, const ExecutionContext& execution,
                                                    const ChannelScopePlan& plan, const ChannelScopeSource& source)
{
    ChannelScopeSource converted = source;
    converted.prepared = nullptr;
    const Status checked = checkCall(execution, plan, converted, kPrepareOperation);
    if (checked.failed())
    {
        return checked;
    }
    const ChannelScopeRequest& request = plan.request();
    if (!encodedComponent(request.scope))
    {
        return scopeFailure(ErrorCode::IncompatibleArguments,
                            "the whole image and a stored channel convert nothing, so there is nothing to prepare",
                            kPrepareOperation, "plan");
    }
    const SampleType working = workingTypeOf(converted.descriptor->sampleType());
    const std::uint32_t width = converted.descriptor->extent().width();
    const std::uint32_t height = converted.descriptor->extent().height();
    const Result<ImageLayout> layout = blockLayoutOf(working, width, height, kColorChannels, kPrepareOperation);
    if (!layout.hasValue())
    {
        return layout.status();
    }
    Result<ImageStorageBytes> storage =
        ImageStorageBytes::create(engine, *layout, StorageInitialization::Uninitialized, kDefaultBufferAlignmentBytes);
    if (!storage.hasValue())
    {
        return storage.status();
    }

    // The block the unprepared call converts into, laid out as it lays its own out: the encoding's three planes, the
    // component's put into its units with its pixels of no colour marked.
    const PlaneSet planes{.base = static_cast<std::uint8_t*>(storage->data()), .layout = *layout};
    const SourceAccess access = accessOf(converted);
    const std::uint32_t component = channelScopeComponent(request.scope);
    const double scale = componentScaleOf(request);
    std::atomic<std::uint64_t> invalidPixels{0u};
    const auto convert = [&]<typename WorkT>() -> Status {
        const Status forward =
            convertToEncoding<WorkT>(execution, plan, access, planes, invalidPixels, kPrepareOperation);
        if (forward.failed() || (!access.ruled && scale == 1.0))
        {
            return forward;
        }
        const std::optional<ImageLayout> planeLayout = onePlaneOf(*planes.layout);
        if (!planeLayout.has_value())
        {
            return scopeFailure(ErrorCode::Internal, "the working planes cannot be described", kPrepareOperation,
                                "source");
        }
        return preparePlane<WorkT>(execution, access, planes, component, scale, planes.plane(component), *planeLayout);
    };
    const Status made =
        working == SampleType::Float64 ? convert.template operator()<double>() : convert.template operator()<float>();
    if (made.failed())
    {
        return made;
    }
    ChannelScopePreparation prepared;
    prepared.m_request = request;
    prepared.m_width = width;
    prepared.m_height = height;
    prepared.m_sampleType = working;
    prepared.m_invalidPixels = invalidPixels.load(std::memory_order_relaxed);
    prepared.m_planes = std::move(*storage);
    return prepared;
}

const char* channelScopeName(ChannelScope scope) noexcept
{
    switch (scope)
    {
    case ChannelScope::Whole:
        return "Whole";
    case ChannelScope::Red:
        return "Red";
    case ChannelScope::Green:
        return "Green";
    case ChannelScope::Blue:
        return "Blue";
    case ChannelScope::CieLightness:
        return "CieLightness";
    case ChannelScope::CieA:
        return "CieA";
    case ChannelScope::CieB:
        return "CieB";
    case ChannelScope::CieChroma:
        return "CieChroma";
    case ChannelScope::CieHue:
        return "CieHue";
    case ChannelScope::CieX:
        return "CieX";
    case ChannelScope::CieY:
        return "CieY";
    case ChannelScope::CieZ:
        return "CieZ";
    case ChannelScope::HslHue:
        return "HslHue";
    case ChannelScope::HslSaturation:
        return "HslSaturation";
    case ChannelScope::HslLightness:
        return "HslLightness";
    case ChannelScope::HsvHue:
        return "HsvHue";
    case ChannelScope::HsvSaturation:
        return "HsvSaturation";
    case ChannelScope::HsvValue:
        return "HsvValue";
    case ChannelScope::HsiHue:
        return "HsiHue";
    case ChannelScope::HsiSaturation:
        return "HsiSaturation";
    case ChannelScope::HsiIntensity:
        return "HsiIntensity";
    case ChannelScope::YCbCrLuma:
        return "YCbCrLuma";
    case ChannelScope::YCbCrBlueChroma:
        return "YCbCrBlueChroma";
    case ChannelScope::YCbCrRedChroma:
        return "YCbCrRedChroma";
    }
    return "UnknownChannelScope";
}

Result<ChannelScopePlan> planChannelScope(const ChannelScopeRequest& request)
{
    if (!isChannelScope(request.scope) || !isChannelScopeUnits(request.units) || !isYCbCrVariant(request.yCbCr))
    {
        return scopeFailure(ErrorCode::InvalidArgument, "a channel scope enumerator is outside its enumeration",
                            kPlanOperation, "request");
    }
    const ColorEncodingError encodingError = checkColorEncoding(request.sourceEncoding);
    if (encodingError != ColorEncodingError::None)
    {
        return scopeFailure(ErrorCode::InvalidArgument, colorEncodingErrorName(encodingError), kPlanOperation,
                            "sourceEncoding");
    }
    if (request.sourceEncoding.schema != ChannelSchema::Rgb)
    {
        return scopeFailure(ErrorCode::InvalidArgument,
                            "the source's encoding names its red, green and blue, so its schema is RGB; an alpha "
                            "channel is carried by the scope",
                            kPlanOperation, "sourceEncoding");
    }
    if (!encodedComponent(request.scope))
    {
        return ChannelScopePlan(request, std::nullopt, std::nullopt);
    }
    const ColorEncoding target = scopeEncodingOf(request);
    Result<ColorConversionPlan> forward = planColorConversion(request.sourceEncoding, target);
    if (!forward.hasValue())
    {
        return forward.status();
    }
    Result<ColorConversionPlan> inverse = planColorConversion(target, request.sourceEncoding);
    if (!inverse.hasValue())
    {
        return inverse.status();
    }
    return ChannelScopePlan(request, *forward, *inverse);
}

Result<ImageDescriptor> channelScopePlaneDescriptor(const ChannelScopePlan& plan, const ImageDescriptor& source)
{
    return planeDescriptorOf(plan.request(), source, kDescriptorOperation);
}

Result<ImageLayoutRequest> channelScopePlaneLayout(const ChannelScopePlan& plan, const ImageDescriptor& source)
{
    const Result<ImageDescriptor> plane = planeDescriptorOf(plan.request(), source, kLayoutOperation);
    if (!plane.hasValue())
    {
        return plane.status();
    }
    return planeLayoutRequestOf(*plane);
}

Result<ChannelScopeResult> extractChannelScope(const ExecutionContext& execution, const ChannelScopePlan& plan,
                                               const ChannelScopeSource& source, void* destination,
                                               const ImageLayout& destinationLayout)
{
    const Status checked = checkCall(execution, plan, source, kExtractOperation);
    if (checked.failed())
    {
        return checked;
    }
    const ChannelScopeRequest& request = plan.request();
    const Result<ImageDescriptor> plane = planeDescriptorOf(request, *source.descriptor, kExtractOperation);
    if (!plane.hasValue())
    {
        return plane.status();
    }
    const Status destined = checkDestination(source, 1u, sampleTypeSizeBytes(plane->sampleType()), destination,
                                             destinationLayout, kExtractOperation);
    if (destined.failed())
    {
        return destined;
    }
    std::uint8_t* const bytes = static_cast<std::uint8_t*>(destination);
    if (source.prepared != nullptr)
    {
        // The plane the preparation holds is the one extraction converts.
        const Status prepared = checkPreparation(plan, source, kExtractOperation);
        if (prepared.failed())
        {
            return prepared;
        }
        const PlaneSet kept = planesOf(*source.prepared);
        const std::optional<ImageLayout> keptLayout = onePlaneOf(*kept.layout);
        if (!keptLayout.has_value())
        {
            return scopeFailure(ErrorCode::Internal, "the preparation's planes cannot be described", kExtractOperation,
                                "source");
        }
        const Status copied =
            copyOneChannel(execution, plane->sampleType(), kept.plane(channelScopeComponent(request.scope)),
                           *keptLayout, 0u, bytes, destinationLayout);
        if (copied.failed())
        {
            return copied;
        }
        return ChannelScopeResult{.scope = request.scope, .invalidPixels = source.prepared->invalidPixels()};
    }
    if (storedChannel(request.scope))
    {
        const Status copied = copyOneChannel(execution, source.descriptor->sampleType(), source.samples, *source.layout,
                                             channelScopeComponent(request.scope), bytes, destinationLayout);
        if (copied.failed())
        {
            return copied;
        }
        return ChannelScopeResult{.scope = request.scope, .invalidPixels = 0u};
    }
    if (plane->sampleType() == SampleType::Float64)
    {
        return extractEncoded<double>(execution, plan, source, bytes, destinationLayout);
    }
    return extractEncoded<float>(execution, plan, source, bytes, destinationLayout);
}

Result<ChannelScopeResult> applyChannelScope(const ExecutionContext& execution, const ChannelScopePlan& plan,
                                             const ChannelScopeSource& source, ChannelScopeOperation& operation,
                                             void* destination, const ImageLayout& destinationLayout)
{
    return applyChannelScope(execution, plan, source, operation, destination, destinationLayout,
                             ChannelScopeDestination::Unwritten);
}

Result<ChannelScopeResult> applyChannelScope(const ExecutionContext& execution, const ChannelScopePlan& plan,
                                             const ChannelScopeSource& source, ChannelScopeOperation& operation,
                                             void* destination, const ImageLayout& destinationLayout,
                                             ChannelScopeDestination holds)
{
    if (!isChannelScopeDestination(holds))
    {
        return scopeFailure(ErrorCode::InvalidArgument, "the destination's state is outside its enumeration",
                            kApplyOperation, "holds");
    }
    const Status checked = checkCall(execution, plan, source, kApplyOperation);
    if (checked.failed())
    {
        return checked;
    }
    const Status destined = checkDestination(source, source.descriptor->extent().channelCount(),
                                             sampleTypeSizeBytes(source.descriptor->sampleType()), destination,
                                             destinationLayout, kApplyOperation);
    if (destined.failed())
    {
        return destined;
    }
    std::uint8_t* const bytes = static_cast<std::uint8_t*>(destination);
    const ChannelScope scope = plan.request().scope;
    if (source.prepared != nullptr)
    {
        if (!encodedComponent(scope))
        {
            return scopeFailure(ErrorCode::IncompatibleArguments,
                                "a preparation converts a component of another encoding; the whole image and a stored "
                                "channel take none",
                                kApplyOperation, "source");
        }
        const Status prepared = checkPreparation(plan, source, kApplyOperation);
        if (prepared.failed())
        {
            return prepared;
        }
        if (workingTypeOf(source.descriptor->sampleType()) == SampleType::Float64)
        {
            return applyPrepared<double>(execution, plan, source, operation, bytes, destinationLayout);
        }
        return applyPrepared<float>(execution, plan, source, operation, bytes, destinationLayout);
    }
    if (scope == ChannelScope::Whole)
    {
        return applyWhole(execution, source, operation, bytes, destinationLayout, holds);
    }
    if (storedChannel(scope))
    {
        return applyStored(execution, plan, source, operation, bytes, destinationLayout, holds);
    }
    if (workingTypeOf(source.descriptor->sampleType()) == SampleType::Float64)
    {
        return applyEncoded<double>(execution, plan, source, operation, bytes, destinationLayout);
    }
    return applyEncoded<float>(execution, plan, source, operation, bytes, destinationLayout);
}

Result<OperationEffect> channelScopeEffect(const OperationEffect& operationEffect)
{
    if (checkOperationEffect(operationEffect) != OperationEffectError::None)
    {
        return scopeFailure(ErrorCode::InvalidArgument, "the operation's effect is not a consistent declaration",
                            kEffectOperation, "operationEffect");
    }
    if (operationEffect.samples != SampleEffect::Writes)
    {
        return scopeFailure(ErrorCode::IncompatibleArguments,
                            "an operation that only reads writes nothing a scope could recompose", kEffectOperation,
                            "operationEffect");
    }
    if (operationEffect.description != DescriptionEffect::Preserves)
    {
        return scopeFailure(ErrorCode::IncompatibleArguments,
                            "a scoped operation writes what it reads in the same description, because the result is "
                            "recomposed into an image described as the source",
                            kEffectOperation, "operationEffect");
    }
    return operationEffect;
}

} // namespace an
