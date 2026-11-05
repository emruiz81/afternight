// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The SDK calls over single images: making one from NumPy, reading and writing files, converting its representation,
/// a linear transform, extracting and composing channels - and the private session capsule the full host borrows the
/// session through.

#include "RuntimeBindings.h"

#include "RuntimeSessionCapsule.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ChannelTransfer.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace an::sdk
{

namespace
{

/// The extent and representation an asset was last published with.
[[nodiscard]] an::ImageDescriptor descriptorOf(const RuntimeImage& image)
{
    const std::optional<an::ImageDescriptor> descriptor = image.asset()->asset().descriptor();
    if (!descriptor.has_value())
    {
        throw std::runtime_error("Runtime image is not initialized");
    }
    return *descriptor;
}

/// Writes `destination[i] = source[i] * scale + offset` into an image in place, the arithmetic in double and each
/// result held to the destination's range and rounded to the nearest value.
template <an::ResidentSample SampleT>
void writeLinearTransform(const an::Image<SampleT>& source, an::ImageWriteAccess<SampleT>& destination,
                          const double scale, const double offset)
{
    const auto view = source.view();
    if (!view.has_value())
    {
        throw std::runtime_error("Runtime image channel buffer is null");
    }
    const an::ConversionPolicy policy{.domain = an::DomainMapping::CodeValue};
    for (std::uint32_t plane = 0u; plane < view->planeCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < view->height(); ++y)
        {
            const std::optional<std::span<const SampleT>> in = view->row(plane, y);
            const std::optional<std::span<SampleT>> out = destination.view().row(plane, y);
            if (!in.has_value() || !out.has_value() || in->size() != out->size())
            {
                throw std::runtime_error("apply_linear_transform(): a row of the images is missing");
            }
            for (std::size_t x = 0u; x < out->size(); ++x)
            {
                const double transformed = static_cast<double>((*in)[x]) * scale + offset;
                (*out)[x] = an::convertSample<double, SampleT>(transformed, policy).value;
            }
        }
    }
}

/// A compact grey image of one channel of an image.
[[nodiscard]] an::consumer::AnyImage channelOf(const an::consumer::OperationScope& scope,
                                               const an::consumer::AnyImage& image, const std::uint32_t channel)
{
    return std::visit(
        [&](const auto& typed) -> an::consumer::AnyImage {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            const auto view = typed.view();
            if (!view.has_value())
            {
                throw std::runtime_error("Runtime image is not initialized");
            }
            const an::ImageDescriptor descriptor =
                require(describeSdkImage(scope.engine().limits(), view->width(), view->height(), 1u,
                                         an::Image<SampleT>::kSampleType),
                        "extract_channel()");
            an::Image<SampleT> plane = require(
                createCompactImage<SampleT>(scope.engine(), descriptor, an::StorageInitialization::Uninitialized),
                "extract_channel()");
            {
                std::optional<an::ImageWriteAccess<SampleT>> access = plane.write();
                if (!access.has_value())
                {
                    throw std::runtime_error("Runtime image channel buffer is null");
                }
                require(an::extractImageChannel(scope.execution(), an::Image<SampleT>::kSampleType, view->data(),
                                                view->layout(), channel, access->view().data(),
                                                access->view().layout()),
                        "extract_channel()");
            }
            return an::consumer::AnyImage(std::move(plane));
        },
        image);
}

} // namespace

std::string lowerName(const std::string_view name)
{
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lower;
}

py::object dictValueOrNone(const py::dict& values, const char* key)
{
    const py::str pyKey(key);
    return values.contains(pyKey) ? py::reinterpret_borrow<py::object>(values[pyKey]) : py::none();
}

WriteOptions parseSaveOptions(const py::object& value)
{
    WriteOptions options;
    if (value.is_none())
    {
        return options;
    }
    const py::dict values = value.cast<py::dict>();
    const py::object compression = dictValueOrNone(values, "tiff_compression");
    if (!compression.is_none())
    {
        const std::string name = lowerName(compression.cast<std::string>());
        if (name == "none")
        {
            options.tiffCompression = an::OutputCompression::None;
        }
        else if (name == "lzw")
        {
            options.tiffCompression = an::OutputCompression::Lzw;
        }
        else if (name == "zip" || name == "deflate")
        {
            options.tiffCompression = an::OutputCompression::Deflate;
        }
        else if (name == "jpeg")
        {
            options.tiffCompression = an::OutputCompression::Jpeg;
        }
        else
        {
            throw std::runtime_error("Unsupported TIFF compression: " + compression.cast<std::string>() +
                                     " (none, lzw, zip or jpeg)");
        }
    }
    const py::object quality = dictValueOrNone(values, "jpeg_quality");
    if (!quality.is_none())
    {
        const int asked = quality.cast<int>();
        if (asked < 1 || asked > 100)
        {
            throw std::runtime_error("jpeg_quality must be between 1 and 100");
        }
        options.jpegQuality = static_cast<std::uint32_t>(asked);
    }
    const py::object level = dictValueOrNone(values, "png_compression_level");
    if (!level.is_none())
    {
        const int asked = level.cast<int>();
        if (asked < 0 || asked > 9)
        {
            throw std::runtime_error("png_compression_level must be between 0 and 9");
        }
        options.pngCompressionLevel = static_cast<std::uint32_t>(asked);
    }
    assignIfPresent(values, "embed_exif", options.embedExif);
    assignIfPresent(values, "embed_processing_history", options.embedProcessingHistory);
    const py::object history = dictValueOrNone(values, "processing_history");
    if (!history.is_none())
    {
        options.processingHistory.clear();
        for (const py::handle item : history.cast<py::sequence>())
        {
            options.processingHistory.push_back(py::str(item).cast<std::string>());
        }
    }

    static const std::unordered_set<std::string> knownKeys = {"tiff_compression",         "jpeg_quality",
                                                              "png_compression_level",    "embed_exif",
                                                              "embed_processing_history", "processing_history"};
    for (const auto item : values)
    {
        const std::string key = py::str(item.first).cast<std::string>();
        if (knownKeys.find(key) == knownKeys.end())
        {
            if (PyErr_WarnFormat(PyExc_UserWarning, 1, "Unknown save option key: '%s'", key.c_str()) != 0)
            {
                throw py::error_already_set();
            }
        }
    }
    return options;
}

py::list availablePixelTypes()
{
    py::list result;
    for (const char* name : {"int8", "uint8", "int16", "uint16", "int32", "uint32", "float32", "float64"})
    {
        result.append(py::str(name));
    }
    return result;
}

std::shared_ptr<RuntimeImage> imageFromNumpyCall(const py::array& array, const py::object& metadata)
{
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();
    an::consumer::OperationScope scope =
        admitOperation(*session, an::consumer::OperationPurpose::Apply, "image_from_numpy()");
    an::consumer::AnyImage image = imageFromArray(scope, array);
    SdkMetadata sdkMetadata;
    if (!metadata.is_none())
    {
        applyMetadataDict(metadata.cast<py::dict>(), sdkMetadata);
    }
    return runtimeImageOf(*session, scope, std::move(image), std::move(sdkMetadata), "image_from_numpy()");
}

std::shared_ptr<RuntimeImage> loadImageCall(const std::string& path, const bool loadMetadata)
{
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();
    an::consumer::OperationScope scope =
        admitOperation(*session, an::consumer::OperationPurpose::Apply, "load_image()");
    ReadImage read = require(readImageFile(scope, path, loadMetadata), "load_image() could not read '" + path + "'");
    return runtimeImageOf(*session, scope, std::move(read.image), std::move(read.metadata), "load_image()");
}

void saveImageCall(const std::shared_ptr<RuntimeImage>& image, const std::string& path, const py::object& options)
{
    const std::shared_ptr<RuntimeImage>& target = requireRuntimeImage(image, "image");
    if (!isSupportedImagePath(path))
    {
        throw std::runtime_error("File extension not supported by any codec");
    }
    const WriteOptions parsed = parseSaveOptions(options);
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();
    target->sdkMetadata().fileName = path;
    an::consumer::AssetReadLease lease = target->asset()->readForEngine("save_image()");
    an::consumer::OperationScope scope =
        admitOperation(*session, an::consumer::OperationPurpose::Apply, "save_image()");
    require(writeImageFile(scope, lease.image(), target->sdkMetadata(), path, parsed),
            "save_image() could not write '" + path + "'");
}

std::shared_ptr<RuntimeImage> convertPixelTypeCall(const std::shared_ptr<RuntimeImage>& image,
                                                   const std::string& pixelType)
{
    const std::shared_ptr<RuntimeImage>& source = requireRuntimeImage(image, "image");
    const std::optional<an::SampleType> type = sampleTypeFromSdkName(pixelType);
    if (!type.has_value())
    {
        throw std::runtime_error("Unsupported pixel type: " + pixelType);
    }
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();
    an::consumer::AssetReadLease lease = source->asset()->readForEngine("convert_pixel_type()");
    an::consumer::OperationScope scope =
        admitOperation(*session, an::consumer::OperationPurpose::Apply, "convert_pixel_type()");
    an::consumer::AnyImage converted = require(convertedImageOf(scope, lease.image(), *type), "convert_pixel_type()");
    lease.release();
    return runtimeImageOf(*session, scope, std::move(converted), source->sdkMetadata(), "convert_pixel_type()");
}

void applyLinearTransformCall(const std::shared_ptr<RuntimeImage>& source,
                              const std::shared_ptr<RuntimeImage>& destination, const double scale, const double offset)
{
    const std::shared_ptr<RuntimeImage>& from = requireRuntimeImage(source, "source");
    const std::shared_ptr<RuntimeImage>& to = requireRuntimeImage(destination, "destination");
    const an::ImageDescriptor fromDescriptor = descriptorOf(*from);
    const an::ImageDescriptor toDescriptor = descriptorOf(*to);
    if (fromDescriptor.extent() != toDescriptor.extent())
    {
        throw std::runtime_error(
            "apply_linear_transform() requires source and destination images to share dimensions and channels");
    }
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();

    // The source is converted to the destination's representation first, at full scale as every conversion is, and
    // the transform is then applied in the destination's own units - which is how the SDK reads the offset.
    // The conversion is a copy, so a source that is the destination itself is read before it is written.
    std::optional<an::consumer::AnyImage> converted;
    {
        an::consumer::AssetReadLease lease = from->asset()->readForEngine("apply_linear_transform()");
        an::consumer::OperationScope scope =
            admitOperation(*session, an::consumer::OperationPurpose::Apply, "apply_linear_transform()");
        converted =
            require(convertedImageOf(scope, lease.image(), toDescriptor.sampleType()), "apply_linear_transform()");
    }
    an::consumer::AssetWriteLease lease = to->asset()->writeForEngine("apply_linear_transform()");
    std::visit(
        [&](const auto& typed) {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            std::optional<an::ImageWriteAccess<SampleT>> access = lease.write<SampleT>();
            if (!access.has_value())
            {
                throw std::runtime_error("apply_linear_transform(): the destination changed representation");
            }
            writeLinearTransform<SampleT>(typed, *access, scale, offset);
        },
        *converted);
    lease.release();
    to->sdkMetadata() = from->sdkMetadata();
}

std::shared_ptr<RuntimeImage> extractChannelCall(const std::shared_ptr<RuntimeImage>& image, const int channel)
{
    const std::shared_ptr<RuntimeImage>& source = requireRuntimeImage(image, "image");
    const an::ImageDescriptor descriptor = descriptorOf(*source);
    if (channel < 0 || static_cast<std::uint32_t>(channel) >= descriptor.extent().channelCount())
    {
        throw std::runtime_error("Channel index is out of range");
    }
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();
    an::consumer::AssetReadLease lease = source->asset()->readForEngine("extract_channel()");
    an::consumer::OperationScope scope =
        admitOperation(*session, an::consumer::OperationPurpose::Apply, "extract_channel()");
    an::consumer::AnyImage plane = channelOf(scope, lease.image(), static_cast<std::uint32_t>(channel));
    lease.release();
    return runtimeImageOf(*session, scope, std::move(plane), source->sdkMetadata(), "extract_channel()");
}

py::list extractChannelsCall(const std::shared_ptr<RuntimeImage>& image)
{
    const std::shared_ptr<RuntimeImage>& source = requireRuntimeImage(image, "image");
    const std::uint32_t channels = descriptorOf(*source).extent().channelCount();
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();
    py::list result;
    an::consumer::AssetReadLease lease = source->asset()->readForEngine("extract_channels()");
    an::consumer::OperationScope scope =
        admitOperation(*session, an::consumer::OperationPurpose::Apply, "extract_channels()");
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        result.append(runtimeImageOf(*session, scope, channelOf(scope, lease.image(), channel), source->sdkMetadata(),
                                     "extract_channels()"));
    }
    return result;
}

std::shared_ptr<RuntimeImage> composeRgbCall(const std::shared_ptr<RuntimeImage>& red,
                                             const std::shared_ptr<RuntimeImage>& green,
                                             const std::shared_ptr<RuntimeImage>& blue)
{
    const std::array<const std::shared_ptr<RuntimeImage>*, 3u> planes{
        &requireRuntimeImage(red, "red"), &requireRuntimeImage(green, "green"), &requireRuntimeImage(blue, "blue")};
    std::array<an::ImageDescriptor, 3u> descriptors{descriptorOf(**planes[0]), descriptorOf(**planes[1]),
                                                    descriptorOf(**planes[2])};
    for (const an::ImageDescriptor& descriptor : descriptors)
    {
        if (descriptor.extent().channelCount() != 1u)
        {
            throw std::runtime_error("compose_rgb() requires mono channel images");
        }
    }
    for (const an::ImageDescriptor& descriptor : descriptors)
    {
        if (descriptor.extent().width() != descriptors[0].extent().width() ||
            descriptor.extent().height() != descriptors[0].extent().height())
        {
            throw std::runtime_error("compose_rgb() requires all channels to share the same dimensions");
        }
    }
    const an::SampleType type = descriptors[0].sampleType();
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();

    std::vector<an::consumer::AssetReadLease> leases;
    leases.reserve(3u);
    for (const auto* plane : planes)
    {
        leases.push_back((*plane)->asset()->readForEngine("compose_rgb()"));
    }
    an::consumer::OperationScope scope =
        admitOperation(*session, an::consumer::OperationPurpose::Apply, "compose_rgb()");
    // Green and blue are converted to red's representation, as the SDK always composed them.
    std::array<std::optional<an::consumer::AnyImage>, 3u> converted;
    for (std::size_t index = 1u; index < 3u; ++index)
    {
        if (descriptors[index].sampleType() != type)
        {
            converted[index] = require(convertedImageOf(scope, leases[index].image(), type), "compose_rgb()");
        }
    }
    const an::ImageDescriptor composed =
        require(describeSdkImage(scope.engine().limits(), descriptors[0].extent().width(),
                                 descriptors[0].extent().height(), 3u, type),
                "compose_rgb()");
    std::optional<an::consumer::AnyImage> result;
    (void)an::dispatchSampleType(type, [&]<typename SampleT>() {
        an::Image<SampleT> image =
            require(createCompactImage<SampleT>(scope.engine(), composed, an::StorageInitialization::Uninitialized),
                    "compose_rgb()");
        // The views are kept here: a channel source borrows its layout for the call.
        std::array<std::optional<an::ImageView<const SampleT>>, 3u> views;
        std::array<an::ChannelSource, 3u> sources{};
        for (std::size_t index = 0u; index < 3u; ++index)
        {
            const an::consumer::AnyImage& plane =
                converted[index].has_value() ? *converted[index] : leases[index].image();
            const an::Image<SampleT>* typed = std::get_if<an::Image<SampleT>>(&plane);
            if (typed != nullptr)
            {
                views[index] = typed->view();
            }
            if (!views[index].has_value())
            {
                throw std::runtime_error("compose_rgb(): a channel holds another representation");
            }
            sources[index] = an::ChannelSource{.samples = views[index]->data(), .layout = &views[index]->layout()};
        }
        {
            std::optional<an::ImageWriteAccess<SampleT>> access = image.write();
            if (!access.has_value())
            {
                throw std::runtime_error("Runtime image channel buffer is null");
            }
            require(an::composeImageChannels(scope.execution(), type, sources, access->view().data(),
                                             access->view().layout()),
                    "compose_rgb()");
        }
        result = an::consumer::AnyImage(std::move(image));
    });
    if (!result.has_value())
    {
        throw std::runtime_error("Unsupported runtime image pixel type");
    }
    leases.clear();
    return runtimeImageOf(*session, scope, std::move(*result), (*planes[0])->sdkMetadata(), "compose_rgb()");
}

py::capsule sessionCapsuleCall()
{
    auto record = std::make_unique<RuntimeSessionCapsule>();
    record->session = requireSession();
    py::capsule capsule(record.get(), kRuntimeSessionCapsuleName, [](PyObject* object) {
        delete static_cast<RuntimeSessionCapsule*>(PyCapsule_GetPointer(object, kRuntimeSessionCapsuleName));
    });
    record.release();
    return capsule;
}

} // namespace an::sdk
