// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The runtime backend's engine-side support: failures as text, the session's codecs and admission, SDK images in
/// compact planar storage, reading and writing files with the SDK's cards, and registration matrices between the SDK's
/// pixel convention and the engine's.

#include "RuntimeEngine.h"

#include "RuntimeSeat.h"

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecCapabilities.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecMetadataPolicy.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/JpegCodec.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/IO/PngCodec.h>
#include <AfterNightEngine/IO/RawCodec.h>
#include <AfterNightEngine/IO/TiffCodec.h>
#include <AfterNightEngine/IO/XisfCodec.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <span>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

namespace an::sdk
{

namespace
{

/// The half-pixel shift between the SDK's registration convention, which puts the first pixel's centre at zero, and
/// the engine's, which puts it at one half.
constexpr double kPixelCentreOffset = 0.5;

[[nodiscard]] std::string lowerCopy(std::string_view text)
{
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lower;
}

/// The row-major product `T(dx, dy) * matrix * T(-dx, -dy)`: the same map in a frame whose origin moved by (dx, dy).
[[nodiscard]] std::array<double, 9> conjugatedByShift(const std::array<double, 9>& matrix, const double dx,
                                                      const double dy)
{
    // matrix * T(-d): the third column gains -dx times the first and -dy times the second.
    std::array<double, 9> right = matrix;
    for (std::size_t row = 0u; row < 3u; ++row)
    {
        const std::size_t base = row * 3u;
        right[base + 2u] = matrix[base + 2u] - dx * matrix[base] - dy * matrix[base + 1u];
    }
    // T(d) * right: the two coordinate rows gain d times the bottom row.
    std::array<double, 9> conjugated = right;
    for (std::size_t column = 0u; column < 3u; ++column)
    {
        conjugated[column] = right[column] + dx * right[6u + column];
        conjugated[3u + column] = right[3u + column] + dy * right[6u + column];
    }
    return conjugated;
}

/// The representations a format holds that a write may fall back to when it does not hold the image's own, nearest
/// first: a floating image goes to floating and then to the deepest integer a raster format holds, an integer image
/// to the other signedness of its width and then to the raster depths.
[[nodiscard]] std::vector<an::SampleType> writeCandidates(const an::SampleType type)
{
    switch (type)
    {
    case an::SampleType::Float32:
    case an::SampleType::Float64:
        return {type, an::SampleType::Float32, an::SampleType::Float64, an::SampleType::UInt16, an::SampleType::UInt8};
    case an::SampleType::Int8:
        return {type, an::SampleType::UInt8, an::SampleType::UInt16, an::SampleType::Float32};
    case an::SampleType::Int16:
        return {type, an::SampleType::UInt16, an::SampleType::UInt8, an::SampleType::Float32};
    case an::SampleType::Int32:
        return {type, an::SampleType::UInt32, an::SampleType::Float64, an::SampleType::UInt16, an::SampleType::UInt8};
    case an::SampleType::UInt32:
        return {type, an::SampleType::Int32, an::SampleType::Float64, an::SampleType::UInt16, an::SampleType::UInt8};
    case an::SampleType::UInt16:
        return {type, an::SampleType::Int16, an::SampleType::UInt8, an::SampleType::Float32};
    case an::SampleType::UInt8:
        return {type, an::SampleType::Int8, an::SampleType::UInt16, an::SampleType::Float32};
    }
    return {type};
}

/// The compression a format is written with.
struct Compression
{
    an::OutputCompression method = an::OutputCompression::None;
    std::optional<std::uint32_t> level{};
};

[[nodiscard]] Compression compressionFor(const an::CodecFormat format, const WriteOptions& options)
{
    switch (format)
    {
    case an::CodecFormat::Tiff:
        return Compression{.method = options.tiffCompression};
    case an::CodecFormat::Png:
        return Compression{.method = an::OutputCompression::Deflate, .level = options.pngCompressionLevel};
    case an::CodecFormat::Jpeg:
        return Compression{.method = an::OutputCompression::Jpeg, .level = options.jpegQuality};
    default:
        return Compression{};
    }
}

/// A frame of compact planar samples taken over as an engine image of an SDK description, swapping a foreign byte
/// order.
template <an::ResidentSample SampleT>
[[nodiscard]] an::Result<an::Image<SampleT>>
importFrame(const an::consumer::OperationScope& scope, const an::RawFrame& frame, const an::ImageDescriptor& descriptor)
{
    const std::optional<an::RawFrameDescriptor> frameDescriptor = frame.descriptor();
    const std::optional<an::ImageLayout> frameLayout = frame.layout();
    if (!frameDescriptor.has_value() || !frameLayout.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the decoded frame holds nothing");
    }
    an::Result<an::Image<SampleT>> image =
        createCompactImage<SampleT>(scope.engine(), descriptor, an::StorageInitialization::Uninitialized);
    if (!image.hasValue())
    {
        return image.status();
    }
    std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the decoded image holds nothing");
    }
    an::Status copied = an::repackImageSamples(scope.execution(), an::Image<SampleT>::kSampleType, frame.bytes().data(),
                                               *frameLayout, access->view().data(), access->view().layout());
    const bool foreignOrder = frameDescriptor->byteOrder() != an::ByteOrder::Native &&
                              frameDescriptor->byteOrder() != an::nativeByteOrder() &&
                              an::isByteOrderMeaningful(an::Image<SampleT>::kSampleType);
    if (copied.ok() && foreignOrder)
    {
        copied = an::swapImageByteOrder(scope.execution(), an::Image<SampleT>::kSampleType, access->view().data(),
                                        access->view().layout());
    }
    if (copied.failed())
    {
        return copied;
    }
    return image;
}

/// Whether a representation is a signed integer, which the SDK reads counted from its smallest code.
[[nodiscard]] bool isSignedIntegerType(const an::SampleType type) noexcept
{
    return type == an::SampleType::Int8 || type == an::SampleType::Int16 || type == an::SampleType::Int32;
}

/// Calls `body(double&)` for every sample of a compact planar Float64 image.
template <typename BodyT> void forEachSample(const an::ImageView<double>& view, BodyT&& body)
{
    for (std::uint32_t plane = 0u; plane < view.planeCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < view.height(); ++y)
        {
            const std::optional<std::span<double>> row = view.row(plane, y);
            if (row.has_value())
            {
                for (double& value : *row)
                {
                    body(value);
                }
            }
        }
    }
}

/// The description of a decoded or computed image restated in the normalized linear domain, which every SDK image is.
[[nodiscard]] an::Result<an::ImageDescriptor> normalizedDescriptorOf(const an::ImageDescriptor& descriptor)
{
    an::ImageDescriptorRequest request = descriptor.toRequest();
    request.numericDomain = an::NumericDomain::NormalizedLinear;
    const std::optional<an::ImageDescriptor> restated = an::ImageDescriptor::create(request);
    if (!restated.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the image has no description as fractions of full scale");
    }
    return *restated;
}

} // namespace

std::string describeFailure(const an::Status& status)
{
    std::string text = std::string(an::errorCodeName(status.code())) + ": " + status.message();
    const an::Status root = status.rootCause();
    if (root.failed() && std::strcmp(root.message(), status.message()) != 0)
    {
        text += " (because: " + std::string(root.message()) + ")";
    }
    return text;
}

void raiseFailure(const std::string_view context, const an::Status& status)
{
    throw std::runtime_error(std::string(context) + ": " + describeFailure(status));
}

an::Status refusal(const an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

an::Result<std::shared_ptr<const an::CodecRegistry>> createRuntimeCodecRegistry()
{
    const std::array<an::Result<std::shared_ptr<const an::ImageCodec>>, 6u> adapters{
        an::createFitsCodec(), an::createXisfCodec(), an::createTiffCodec(),
        an::createPngCodec(),  an::createJpegCodec(), an::createRawCodec()};
    std::vector<std::shared_ptr<const an::ImageCodec>> codecs;
    codecs.reserve(adapters.size());
    for (const an::Result<std::shared_ptr<const an::ImageCodec>>& adapter : adapters)
    {
        if (!adapter.hasValue())
        {
            return adapter.status();
        }
        codecs.push_back(*adapter);
    }
    return an::CodecRegistry::create(codecs);
}

bool isSupportedImagePath(const std::string_view path) noexcept
{
    const an::CodecFormat format = an::codecFormatFromPath(path);
    return format != an::CodecFormat::Unknown && format != an::CodecFormat::Dump;
}

std::shared_ptr<an::consumer::EngineSession> requireSession()
{
    std::shared_ptr<an::consumer::EngineSession> session = seatSession();
    if (!session)
    {
        throw std::runtime_error("The runtime backend is not initialized: its host calls an_runtime_backend_initialize "
                                 "before it runs Python");
    }
    return session;
}

an::consumer::OperationScope admitOperation(const an::consumer::EngineSession& session,
                                            const an::consumer::OperationPurpose purpose,
                                            const std::string_view context)
{
    an::consumer::OperationRequest request;
    request.purpose = purpose;
    return require(session.admit(request), context);
}

std::optional<an::ChannelSchema> sdkChannelSchema(const std::uint32_t channels) noexcept
{
    switch (channels)
    {
    case 1u:
        return an::ChannelSchema::Mono;
    case 3u:
        return an::ChannelSchema::Rgb;
    case 4u:
        return an::ChannelSchema::Rgba;
    default:
        return std::nullopt;
    }
}

const char* sdkSampleTypeName(const an::SampleType type) noexcept
{
    switch (type)
    {
    case an::SampleType::Int8:
        return "int8";
    case an::SampleType::UInt8:
        return "uint8";
    case an::SampleType::Int16:
        return "int16";
    case an::SampleType::UInt16:
        return "uint16";
    case an::SampleType::Int32:
        return "int32";
    case an::SampleType::UInt32:
        return "uint32";
    case an::SampleType::Float32:
        return "float32";
    case an::SampleType::Float64:
        return "float64";
    }
    return "unknown";
}

std::optional<an::SampleType> sampleTypeFromSdkName(const std::string_view name)
{
    const std::string lower = lowerCopy(name);
    if (lower == "int8" || lower == "sint8")
    {
        return an::SampleType::Int8;
    }
    if (lower == "uint8")
    {
        return an::SampleType::UInt8;
    }
    if (lower == "int16" || lower == "sint16")
    {
        return an::SampleType::Int16;
    }
    if (lower == "uint16")
    {
        return an::SampleType::UInt16;
    }
    if (lower == "int32" || lower == "sint32")
    {
        return an::SampleType::Int32;
    }
    if (lower == "uint32")
    {
        return an::SampleType::UInt32;
    }
    if (lower == "float" || lower == "float32")
    {
        return an::SampleType::Float32;
    }
    if (lower == "double" || lower == "float64")
    {
        return an::SampleType::Float64;
    }
    return std::nullopt;
}

const char* sdkPixelFormatName(const an::ChannelSchema schema) noexcept
{
    switch (schema)
    {
    case an::ChannelSchema::Mono:
        return "gray";
    case an::ChannelSchema::Rgb:
        return "rgb";
    case an::ChannelSchema::Rgba:
        return "rgba";
    case an::ChannelSchema::Xyz:
        return "ciexyz";
    case an::ChannelSchema::Lab:
        return "cielab";
    case an::ChannelSchema::Lch:
        return "cielch";
    case an::ChannelSchema::Hsl:
        return "hsl";
    case an::ChannelSchema::Hsv:
        return "hsv";
    case an::ChannelSchema::Hsi:
        return "hsi";
    case an::ChannelSchema::YCbCr:
        return "ycbcr";
    default:
        return "unknown";
    }
}

an::Result<an::ImageDescriptor> describeSdkImage(const an::EngineLimits& limits, const std::uint32_t width,
                                                 const std::uint32_t height, const std::uint32_t channels,
                                                 const an::SampleType type)
{
    const std::optional<an::ChannelSchema> schema = sdkChannelSchema(channels);
    if (!schema.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "Runtime images currently support 1, 3, or 4 channels only");
    }
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, channels, limits);
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::LimitExceeded, "an image of " + std::to_string(width) + " x " +
                                                         std::to_string(height) + " x " + std::to_string(channels) +
                                                         " samples is outside the engine's limits");
    }
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = type,
                                                               .channelSchema = *schema,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = an::CfaDescriptor{},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    if (!descriptor.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the image has no description");
    }
    return *descriptor;
}

an::Result<an::ImageLayout> compactLayoutOf(const an::ImageDescriptor& descriptor, const an::EngineLimits& limits)
{
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = descriptor.extent(),
                                                       .elementSizeBytes = static_cast<std::uint32_t>(
                                                           an::sampleTypeSizeBytes(descriptor.sampleType())),
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = descriptor.extent().channelCount(),
                                                       .rowAlignmentBytes = 1u},
                                limits);
    if (!layout.has_value())
    {
        return refusal(an::ErrorCode::LimitExceeded, "the image has no compact planar layout within the limits");
    }
    return *layout;
}

bool isCompactPlanar(const an::ImageLayout& layout) noexcept
{
    const std::uint64_t row =
        static_cast<std::uint64_t>(layout.extent().width()) * static_cast<std::uint64_t>(layout.elementSizeBytes());
    return layout.samplesPerPixel() == 1u && layout.planeCount() == layout.extent().channelCount() &&
           layout.rowStrideBytes() == row &&
           layout.planeStrideBytes() == row * static_cast<std::uint64_t>(layout.extent().height());
}

an::Result<an::consumer::AnyImage> compactImageOf(const an::consumer::OperationScope& scope,
                                                  an::consumer::AnyImage&& image)
{
    return std::visit(
        [&](auto& typed) -> an::Result<an::consumer::AnyImage> {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            const std::optional<an::ImageDescriptor> descriptor = typed.descriptor();
            const std::optional<an::ImageLayout> layout = typed.layout();
            const auto view = typed.view();
            if (!descriptor.has_value() || !layout.has_value() || !view.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
            }
            if (descriptor->numericDomain() == an::NumericDomain::NormalizedLinear && isCompactPlanar(*layout))
            {
                return an::consumer::AnyImage(std::move(typed));
            }
            an::Result<an::ImageDescriptor> normalized = normalizedDescriptorOf(*descriptor);
            if (!normalized.hasValue())
            {
                return normalized.status();
            }
            an::Result<an::Image<SampleT>> compact =
                createCompactImage<SampleT>(scope.engine(), *normalized, an::StorageInitialization::Uninitialized);
            if (!compact.hasValue())
            {
                return compact.status();
            }
            {
                std::optional<an::ImageWriteAccess<SampleT>> access = compact->write();
                if (!access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the compact copy holds nothing");
                }
                const an::Status copied =
                    an::repackImageSamples(scope.execution(), an::Image<SampleT>::kSampleType, view->data(),
                                           view->layout(), access->view().data(), access->view().layout());
                if (copied.failed())
                {
                    return copied;
                }
            }
            return an::consumer::AnyImage(std::move(*compact));
        },
        image);
}

an::Result<an::consumer::AnyImage> convertedImageOf(const an::consumer::OperationScope& scope,
                                                    const an::consumer::AnyImage& image, const an::SampleType type)
{
    return std::visit(
        [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            using SourceT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            const std::optional<an::ImageDescriptor> descriptor = typed.descriptor();
            const auto view = typed.view();
            if (!descriptor.has_value() || !view.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
            }
            an::Result<an::ImageDescriptor> normalized = normalizedDescriptorOf(*descriptor);
            if (!normalized.hasValue())
            {
                return normalized.status();
            }
            an::ImageDescriptorRequest request = normalized->toRequest();
            request.sampleType = type;
            const std::optional<an::ImageDescriptor> converted = an::ImageDescriptor::create(request);
            if (!converted.has_value())
            {
                return refusal(an::ErrorCode::IncompatibleArguments,
                               std::string("the image has no description as ") + sdkSampleTypeName(type));
            }
            // The SDK reads a signed integer as a fraction counted from its smallest code, as the product does, and the
            // engine's full-scale mapping counts it from zero. A conversion that has a signed side therefore passes
            // through double fractions, the signed side read or written as code values about that offset; every
            // other conversion is the engine's at full scale.
            const bool sourceSigned = isSignedIntegerType(an::Image<SourceT>::kSampleType);
            const bool destinationSigned = isSignedIntegerType(type);
            const bool offset = (sourceSigned || destinationSigned) && an::Image<SourceT>::kSampleType != type;
            std::optional<an::Image<double>> fractions;
            if (offset)
            {
                an::ImageDescriptorRequest fractionRequest = normalized->toRequest();
                fractionRequest.sampleType = an::SampleType::Float64;
                const std::optional<an::ImageDescriptor> fractionDescriptor =
                    an::ImageDescriptor::create(fractionRequest);
                if (!fractionDescriptor.has_value())
                {
                    return refusal(an::ErrorCode::IncompatibleArguments, "the image has no description as fractions");
                }
                an::Result<an::Image<double>> made = createCompactImage<double>(
                    scope.engine(), *fractionDescriptor, an::StorageInitialization::Uninitialized);
                if (!made.hasValue())
                {
                    return made.status();
                }
                std::optional<an::ImageWriteAccess<double>> access = made->write();
                an::ConversionCounters counters;
                const an::Status read = an::convertImageSamples(
                    scope.execution(), an::Image<SourceT>::kSampleType, view->data(), view->layout(),
                    an::SampleType::Float64, access->view().data(), access->view().layout(),
                    sourceSigned ? an::ConversionPolicy{.domain = an::DomainMapping::CodeValue}
                                 : an::ConversionPolicy{},
                    counters);
                if (read.failed())
                {
                    return read;
                }
                if constexpr (std::is_integral_v<SourceT> && std::is_signed_v<SourceT>)
                {
                    const double lowest = static_cast<double>(std::numeric_limits<SourceT>::lowest());
                    const double range = static_cast<double>(std::numeric_limits<SourceT>::max()) - lowest;
                    forEachSample(access->view(), [&](double& value) { value = (value - lowest) / range; });
                }
                fractions = std::move(*made);
            }
            an::Result<an::consumer::AnyImage> result =
                refusal(an::ErrorCode::Internal, "no representation was converted to");
            (void)an::dispatchSampleType(type, [&]<typename DestinationT>() {
                an::Result<an::Image<DestinationT>> destination = createCompactImage<DestinationT>(
                    scope.engine(), *converted, an::StorageInitialization::Uninitialized);
                if (!destination.hasValue())
                {
                    result = destination.status();
                    return;
                }
                {
                    std::optional<an::ImageWriteAccess<DestinationT>> access = destination->write();
                    if (!access.has_value())
                    {
                        result = refusal(an::ErrorCode::NotInitialized, "the converted image holds nothing");
                        return;
                    }
                    an::ConversionCounters counters;
                    an::Status done = an::Status::success();
                    if (fractions.has_value())
                    {
                        std::optional<an::ImageWriteAccess<double>> working = fractions->write();
                        an::ConversionPolicy policy;
                        if constexpr (std::is_integral_v<DestinationT> && std::is_signed_v<DestinationT>)
                        {
                            const double lowest = static_cast<double>(std::numeric_limits<DestinationT>::lowest());
                            const double range = static_cast<double>(std::numeric_limits<DestinationT>::max()) - lowest;
                            forEachSample(working->view(), [&](double& value) { value = lowest + value * range; });
                            policy.domain = an::DomainMapping::CodeValue;
                        }
                        done =
                            an::convertImageSamples(scope.execution(), an::SampleType::Float64, working->view().data(),
                                                    working->view().layout(), type, access->view().data(),
                                                    access->view().layout(), policy, counters);
                    }
                    else
                    {
                        done = an::convertImageSamples(scope.execution(), descriptor->sampleType(), view->data(),
                                                       view->layout(), type, access->view().data(),
                                                       access->view().layout(), an::ConversionPolicy{}, counters);
                    }
                    if (done.failed())
                    {
                        result = done;
                        return;
                    }
                }
                result = an::consumer::AnyImage(std::move(*destination));
            });
            return result;
        },
        image);
}

std::shared_ptr<an::consumer::ImageAsset> adoptAsset(const an::consumer::EngineSession& session,
                                                     an::consumer::AnyImage&& image, const std::string_view context)
{
    return std::make_shared<an::consumer::ImageAsset>(
        require(an::consumer::ImageAsset::adopt(session, std::move(image)), context));
}

an::Result<ReadImage> readImageFile(const an::consumer::OperationScope& scope, const std::string& path,
                                    const bool readMetadata)
{
    an::Result<an::CodecDecodeResult> decoded = an::decodeImage(
        scope.engine(), scope.execution(), an::CodecReadRequest{.path = path, .readMetadata = readMetadata});
    if (!decoded.hasValue())
    {
        return decoded.status();
    }
    const std::optional<an::RawFrameDescriptor> frameDescriptor = decoded->frame.descriptor();
    const std::optional<an::SampleType> resident =
        frameDescriptor.has_value() ? frameDescriptor->residentSampleType() : std::nullopt;
    const std::optional<an::ImageDescriptor> stored =
        frameDescriptor.has_value() ? frameDescriptor->toImageDescriptor() : std::nullopt;
    if (!resident.has_value() || !stored.has_value())
    {
        return refusal(an::ErrorCode::Unsupported,
                       "'" + path + "' holds samples outside the eight representations the SDK reads");
    }
    an::Result<an::ImageDescriptor> descriptor = normalizedDescriptorOf(*stored);
    if (!descriptor.hasValue())
    {
        return descriptor.status();
    }
    an::Result<an::consumer::AnyImage> image = refusal(an::ErrorCode::Internal, "no representation was read");
    (void)an::dispatchSampleType(*resident, [&]<typename SampleT>() {
        an::Result<an::Image<SampleT>> typed = importFrame<SampleT>(scope, decoded->frame, *descriptor);
        image = typed.hasValue() ? an::Result<an::consumer::AnyImage>(an::consumer::AnyImage(std::move(*typed)))
                                 : an::Result<an::consumer::AnyImage>(typed.status());
    });
    if (!image.hasValue())
    {
        return image.status();
    }
    decoded->frame.release();

    ReadImage read{.image = std::move(*image), .metadata = SdkMetadata{.fileName = path}};
    if (readMetadata)
    {
        for (const an::CodecCard& card : decoded->metadata.codec().cards())
        {
            // The cards that describe how the file stored its samples describe the file, not the image the SDK
            // holds: they are written again from the samples whenever the image is.
            if (card.origin() == an::CodecNamespace::Fits && an::isStructuralFitsKeyword(card.keyword()))
            {
                continue;
            }
            read.metadata.cards.push_back(card);
        }
    }
    return read;
}

an::Status writeImageFile(const an::consumer::OperationScope& scope, const an::consumer::AnyImage& image,
                          const SdkMetadata& metadata, const std::string& path, const WriteOptions& options)
{
    const an::CodecFormat format = an::codecFormatFromPath(path);
    const an::CodecRegistry* registry = scope.engine().codecRegistry();
    const std::optional<an::CodecCapabilities> capabilities =
        (format == an::CodecFormat::Unknown || format == an::CodecFormat::Dump || registry == nullptr)
            ? std::nullopt
            : registry->capabilities(format);
    if (!capabilities.has_value() || capabilities->encodeSamples == 0u)
    {
        return refusal(an::ErrorCode::Unsupported, "File extension not supported by any codec");
    }

    const an::SampleType own = an::consumer::sampleTypeOf(image);
    std::optional<an::SampleType> written;
    for (const an::SampleType candidate :
         options.sampleType.has_value() ? std::vector<an::SampleType>{*options.sampleType} : writeCandidates(own))
    {
        if ((capabilities->encodeSamples & an::codecSampleBit(an::rawSampleFormatOf(candidate))) != 0u)
        {
            written = candidate;
            break;
        }
    }
    if (!written.has_value())
    {
        return refusal(an::ErrorCode::Unsupported, std::string(an::codecFormatName(format)) + " cannot hold " +
                                                       sdkSampleTypeName(options.sampleType.value_or(own)) +
                                                       " samples");
    }
    std::optional<an::consumer::AnyImage> converted;
    if (*written != own)
    {
        an::Result<an::consumer::AnyImage> made = convertedImageOf(scope, image, *written);
        if (!made.hasValue())
        {
            return made.status();
        }
        converted = std::move(*made);
    }
    const an::consumer::AnyImage& source = converted.has_value() ? *converted : image;

    std::vector<an::CodecCard> cards;
    for (const an::CodecCard& card : metadata.cards)
    {
        const bool exif = card.keyword().rfind("Exif.", 0) == 0;
        if (exif && !options.embedExif)
        {
            continue;
        }
        cards.push_back(card);
    }
    std::vector<an::CodecCard> history;
    if (options.embedProcessingHistory)
    {
        for (const std::string& line : options.processingHistory)
        {
            an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, "HISTORY", line, "");
            if (!card.hasValue())
            {
                return card.status();
            }
            history.push_back(std::move(*card));
        }
    }
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    if (!codec.hasValue())
    {
        return codec.status();
    }
    const Compression compression = compressionFor(format, options);
    an::Result<an::OutputIntent> intent =
        an::OutputIntent::create(an::OutputIntentRequest{.sampleType = *written,
                                                         .compression = compression.method,
                                                         .compressionLevel = compression.level,
                                                         .cards = std::move(history)});
    if (!intent.hasValue())
    {
        return intent.status();
    }

    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty())
    {
        std::error_code ignored;
        std::filesystem::create_directories(parent, ignored);
    }
    return std::visit(
        [&](const auto& typed) -> an::Status {
            const an::ImageMetadata written =
                an::ImageMetadata::create(an::AcquisitionMetadata{}, typed.metadata().semantics(),
                                          an::AstrometricSolution{}, an::Provenance{}, *codec, std::nullopt);
            an::Result<an::RawFrame> frame = an::borrowImageFrame(typed);
            if (!frame.hasValue())
            {
                return frame.status();
            }
            an::OutputRequest request;
            request.destinationPath = path;
            request.format = format;
            request.intent = &*intent;
            request.replacePolicy = an::OutputReplacePolicy::ReplaceExisting;
            const an::Result<an::OutputReport> report =
                an::writeImage(scope.engine(), scope.execution(), request, *frame, written);
            return report.hasValue() ? an::Status::success() : report.status();
        },
        source);
}

std::string cardText(const an::CodecCard& card)
{
    const std::string& value = card.value();
    if (value.size() < 2u || value.front() != '\'' || value.back() != '\'')
    {
        return value;
    }
    std::string text;
    text.reserve(value.size());
    for (std::size_t index = 1u; index + 1u < value.size(); ++index)
    {
        text.push_back(value[index]);
        if (value[index] == '\'' && index + 2u < value.size() && value[index + 1u] == '\'')
        {
            ++index;
        }
    }
    while (!text.empty() && text.back() == ' ')
    {
        text.pop_back();
    }
    return text;
}

bool isReservedMetadataKey(const std::string_view key)
{
    const std::string lower = lowerCopy(key);
    return lower == "simple" || lower == "bitpix" || lower == "bzero" || lower == "bscale" || lower == "extend" ||
           lower.rfind("naxis", 0) == 0;
}

an::Status setHeaderCard(SdkMetadata& metadata, const std::string& key, const std::string& lexicalValue)
{
    an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, key, lexicalValue, "");
    if (!card.hasValue())
    {
        return card.status();
    }
    for (an::CodecCard& existing : metadata.cards)
    {
        if (existing.keyword() == key && existing.keyword().rfind("Exif.", 0) != 0)
        {
            existing = std::move(*card);
            return an::Status::success();
        }
    }
    metadata.cards.push_back(std::move(*card));
    return an::Status::success();
}

an::Result<an::StarDetectionResult> detectImageStars(const an::consumer::OperationScope& scope,
                                                     const an::consumer::AnyImage& image,
                                                     const an::StarDetectionRequest& request,
                                                     const std::optional<std::uint32_t> channel)
{
    std::optional<an::consumer::AnyImage> widened;
    if (an::consumer::sampleTypeOf(image) != an::SampleType::Float32)
    {
        an::Result<an::consumer::AnyImage> converted = convertedImageOf(scope, image, an::SampleType::Float32);
        if (!converted.hasValue())
        {
            return converted.status();
        }
        widened = std::move(*converted);
    }
    const auto* frame = std::get_if<an::Image<float>>(widened.has_value() ? &*widened : &image);
    const std::optional<an::ImageView<const float>> view = frame != nullptr ? frame->view() : std::nullopt;
    const std::optional<an::ImageDescriptor> descriptor = frame != nullptr ? frame->descriptor() : std::nullopt;
    if (!view.has_value() || !descriptor.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    // The frame view borrows its layout and description, so both outlive the detection here.
    const an::ImageLayout layout = view->layout();
    const an::ImageDescriptor described = *descriptor;
    an::KernelValidityView validity;
    if (const std::optional<an::ValidityDescriptor> finite =
            an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
        finite.has_value())
    {
        validity.descriptor = *finite;
    }
    an::Result<an::StarFrameView> starFrame =
        an::StarFrameView::create(view->data(), layout.totalBytes(), layout, described, validity);
    if (!starFrame.hasValue())
    {
        return starFrame.status();
    }
    an::StarDetectionRequest asked = request;
    asked.fullScale = 1.0;
    // A field no wider than the detector's margins has no star the detector could find in it, which the SDK has
    // always answered as no stars rather than as a refusal.
    const std::uint32_t margin = 4u * asked.searchRadius + 8u;
    if (!asked.region.has_value() && ((std::min)(described.extent().width(), described.extent().height()) <= margin))
    {
        return an::StarDetectionResult{};
    }
    const an::Result<an::StarDetectionPlan> plan = an::planStarDetection(asked, described, layout, channel);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::StarDetectionResult result;
    if (const an::Status found = an::detectStars(scope.execution(), *starFrame, *plan, result); found.failed())
    {
        return found;
    }
    return result;
}

an::Result<an::RegistrationTransform> engineTransformOfSdk(const an::RegistrationTransformKind kind,
                                                           const std::array<double, 9>& sdkMatrix,
                                                           const an::RegistrationPolynomial& polynomial)
{
    an::RegistrationTransformRequest request;
    request.linearKind = kind;
    request.sourceToReference = conjugatedByShift(sdkMatrix, kPixelCentreOffset, kPixelCentreOffset);
    request.polynomial = polynomial;
    if (request.polynomial.order != 0u)
    {
        request.polynomial.normalizationOrigin.x += kPixelCentreOffset;
        request.polynomial.normalizationOrigin.y += kPixelCentreOffset;
    }
    return an::RegistrationTransform::create(request);
}

std::array<double, 9> sdkMatrixOf(const an::RegistrationTransform& transform)
{
    return conjugatedByShift(transform.matrix(), -kPixelCentreOffset, -kPixelCentreOffset);
}

an::RegistrationPolynomial sdkPolynomialOf(const an::RegistrationTransform& transform)
{
    an::RegistrationPolynomial polynomial = transform.polynomial();
    if (polynomial.order != 0u)
    {
        polynomial.normalizationOrigin.x -= kPixelCentreOffset;
        polynomial.normalizationOrigin.y -= kPixelCentreOffset;
    }
    return polynomial;
}

} // namespace an::sdk
