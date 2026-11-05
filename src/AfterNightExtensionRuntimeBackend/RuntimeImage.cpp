// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The embedded module's `_RuntimeImage` and the writable NumPy export protocol.

#include "RuntimeImage.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace an::sdk
{

namespace
{

/// What an array's shape says about the image it holds.
struct ArrayShape
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t channels = 0u;

    /// Whether the channel axis is the last one: (height, width, channels).
    bool channelsLast = true;
};

[[nodiscard]] ArrayShape arrayShapeOf(const py::buffer_info& info)
{
    const auto checkedSize = [](const py::ssize_t value) {
        if (value <= 0 || value > static_cast<py::ssize_t>(UINT32_MAX))
        {
            throw std::runtime_error("NumPy array dimensions must be positive and fit 32 bits");
        }
        return static_cast<std::uint32_t>(value);
    };
    if (info.ndim == 2)
    {
        return ArrayShape{.width = checkedSize(info.shape[1]), .height = checkedSize(info.shape[0]), .channels = 1u};
    }
    if (info.ndim != 3)
    {
        throw std::runtime_error("Runtime images require a 2D mono or 3D color NumPy array");
    }
    const auto isChannelCount = [](const py::ssize_t value) { return value == 1 || value == 3 || value == 4; };
    if (isChannelCount(info.shape[2]))
    {
        return ArrayShape{.width = checkedSize(info.shape[1]),
                          .height = checkedSize(info.shape[0]),
                          .channels = static_cast<std::uint32_t>(info.shape[2]),
                          .channelsLast = true};
    }
    if (isChannelCount(info.shape[0]))
    {
        return ArrayShape{.width = checkedSize(info.shape[2]),
                          .height = checkedSize(info.shape[1]),
                          .channels = static_cast<std::uint32_t>(info.shape[0]),
                          .channelsLast = false};
    }
    throw std::runtime_error("3D NumPy arrays must use (H, W, C) or (C, H, W) layout with 1, 3, or 4 channels");
}

[[nodiscard]] an::SampleType sampleTypeOfArray(const py::array& array)
{
    const py::dtype dtype = array.dtype();
    if (dtype.is(py::dtype::of<std::int8_t>()))
    {
        return an::SampleType::Int8;
    }
    if (dtype.is(py::dtype::of<std::uint8_t>()))
    {
        return an::SampleType::UInt8;
    }
    if (dtype.is(py::dtype::of<std::int16_t>()))
    {
        return an::SampleType::Int16;
    }
    if (dtype.is(py::dtype::of<std::uint16_t>()))
    {
        return an::SampleType::UInt16;
    }
    if (dtype.is(py::dtype::of<std::int32_t>()))
    {
        return an::SampleType::Int32;
    }
    if (dtype.is(py::dtype::of<std::uint32_t>()))
    {
        return an::SampleType::UInt32;
    }
    if (dtype.is(py::dtype::of<float>()))
    {
        return an::SampleType::Float32;
    }
    if (dtype.is(py::dtype::of<double>()))
    {
        return an::SampleType::Float64;
    }
    throw std::runtime_error("NumPy arrays must use int8/uint8/int16/uint16/int32/uint32/float32/float64");
}

[[nodiscard]] py::dtype numpyDtypeOf(const an::SampleType type)
{
    switch (type)
    {
    case an::SampleType::Int8:
        return py::dtype::of<std::int8_t>();
    case an::SampleType::UInt8:
        return py::dtype::of<std::uint8_t>();
    case an::SampleType::Int16:
        return py::dtype::of<std::int16_t>();
    case an::SampleType::UInt16:
        return py::dtype::of<std::uint16_t>();
    case an::SampleType::Int32:
        return py::dtype::of<std::int32_t>();
    case an::SampleType::UInt32:
        return py::dtype::of<std::uint32_t>();
    case an::SampleType::Float32:
        return py::dtype::of<float>();
    case an::SampleType::Float64:
        return py::dtype::of<double>();
    }
    throw std::runtime_error("Unsupported runtime-backend pixel type");
}

/// Copies an array's samples, at whatever strides it has, into compact planar storage.
template <an::ResidentSample SampleT>
void copyArraySamples(const py::buffer_info& info, const ArrayShape& shape, SampleT* destination)
{
    const auto* base = static_cast<const char*>(info.ptr);
    const std::size_t width = shape.width;
    const std::size_t height = shape.height;
    for (std::size_t channel = 0u; channel < shape.channels; ++channel)
    {
        for (std::size_t y = 0u; y < height; ++y)
        {
            SampleT* row = destination + (channel * height + y) * width;
            py::ssize_t rowOffset = 0;
            py::ssize_t columnStride = 0;
            if (info.ndim == 2)
            {
                rowOffset = static_cast<py::ssize_t>(y) * info.strides[0];
                columnStride = info.strides[1];
            }
            else if (shape.channelsLast)
            {
                rowOffset =
                    static_cast<py::ssize_t>(y) * info.strides[0] + static_cast<py::ssize_t>(channel) * info.strides[2];
                columnStride = info.strides[1];
            }
            else
            {
                rowOffset =
                    static_cast<py::ssize_t>(channel) * info.strides[0] + static_cast<py::ssize_t>(y) * info.strides[1];
                columnStride = info.strides[2];
            }
            const char* source = base + rowOffset;
            if (columnStride == static_cast<py::ssize_t>(sizeof(SampleT)))
            {
                std::memcpy(row, source, width * sizeof(SampleT));
                continue;
            }
            for (std::size_t x = 0u; x < width; ++x)
            {
                std::memcpy(&row[x], source + static_cast<py::ssize_t>(x) * columnStride, sizeof(SampleT));
            }
        }
    }
}

[[nodiscard]] std::string lowerCopy(std::string_view text)
{
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lower;
}

} // namespace

// ---- ExportedAsset.

ExportedAsset::ExportedAsset(std::shared_ptr<an::consumer::ImageAsset> asset) noexcept : m_asset(std::move(asset)) {}

an::consumer::ImageAsset& ExportedAsset::asset() const noexcept
{
    return *m_asset;
}

void ExportedAsset::exportStarted() noexcept
{
    m_exports.fetch_add(1u, std::memory_order_acq_rel);
}

void ExportedAsset::exportReleased() noexcept
{
    if (m_exports.fetch_sub(1u, std::memory_order_acq_rel) != 1u)
    {
        return;
    }
    // The last array is gone: what NumPy wrote is published as a new pixel generation now, so nothing keyed by the
    // revision read while the array lived can be taken for the samples. A reader holding the asset at this moment
    // leaves the move owed to the next engine call.
    m_generationOwed.store(true, std::memory_order_release);
    an::Result<an::consumer::AssetWriteLease> lease = m_asset->tryWrite();
    if (lease.hasValue())
    {
        lease->visitWrite([](auto&&) {});
        lease->release();
        m_generationOwed.store(false, std::memory_order_release);
    }
}

std::size_t ExportedAsset::writableExports() const noexcept
{
    return m_exports.load(std::memory_order_acquire);
}

bool ExportedAsset::externallyMutable() const noexcept
{
    return m_exports.load(std::memory_order_acquire) != 0u || m_generationOwed.load(std::memory_order_acquire);
}

void ExportedAsset::settleExternalWrites(const std::string_view context)
{
    if (!externallyMutable())
    {
        return;
    }
    an::consumer::AssetWriteLease lease = require(m_asset->write(), context);
    lease.visitWrite([](auto&&) {});
    lease.release();
    if (m_exports.load(std::memory_order_acquire) == 0u)
    {
        m_generationOwed.store(false, std::memory_order_release);
    }
}

an::consumer::AssetReadLease ExportedAsset::readForEngine(const std::string_view context)
{
    settleExternalWrites(context);
    return require(m_asset->read(), context);
}

an::consumer::AssetWriteLease ExportedAsset::writeForEngine(const std::string_view context)
{
    m_generationOwed.store(false, std::memory_order_release);
    return require(m_asset->write(), context);
}

// ---- RuntimeImage.

RuntimeImage::RuntimeImage(std::shared_ptr<ExportedAsset> asset, SdkMetadata metadata)
    : m_asset(std::move(asset)), m_metadata(std::move(metadata))
{
}

py::array RuntimeImage::toNumpy() const
{
    an::consumer::AssetReadLease lease = m_asset->readForEngine("to_numpy()");
    an::SampleType type = an::SampleType::Float32;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t channels = 0u;
    const void* samples = nullptr;
    bool compact = false;
    lease.visit([&](const auto& typed) {
        const auto view = typed.view();
        if (!view.has_value())
        {
            return;
        }
        type = std::remove_cvref_t<decltype(typed)>::kSampleType;
        width = view->width();
        height = view->height();
        channels = view->channelCount();
        samples = view->data();
        compact = isCompactPlanar(view->layout());
    });
    if (samples == nullptr)
    {
        throw std::runtime_error("Runtime image is not initialized");
    }
    if (!compact)
    {
        throw std::runtime_error("to_numpy(): the runtime image is not held in compact planar storage");
    }
    // The samples stay where they are for as long as the asset lives, which the array's base keeps it doing; the
    // lease is only for reading where they are.
    lease.release();

    const auto item = static_cast<py::ssize_t>(an::sampleTypeSizeBytes(type));
    const auto w = static_cast<py::ssize_t>(width);
    const auto h = static_cast<py::ssize_t>(height);
    std::vector<py::ssize_t> shape;
    std::vector<py::ssize_t> strides;
    if (channels <= 1u)
    {
        shape = {h, w};
        strides = {w * item, item};
    }
    else
    {
        shape = {h, w, static_cast<py::ssize_t>(channels)};
        strides = {w * item, item, w * h * item};
    }
    auto* owner = new std::shared_ptr<ExportedAsset>(m_asset);
    m_asset->exportStarted();
    py::capsule base(owner, [](void* value) {
        auto* held = static_cast<std::shared_ptr<ExportedAsset>*>(value);
        (*held)->exportReleased();
        delete held;
    });
    return py::array(numpyDtypeOf(type), shape, strides, const_cast<void*>(samples), base);
}

void RuntimeImage::fromNumpy(const py::array& array)
{
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();
    an::consumer::OperationScope scope =
        admitOperation(*session, an::consumer::OperationPurpose::Apply, "_RuntimeImage.from_numpy()");
    an::consumer::AnyImage image = imageFromArray(scope, array);
    m_asset = std::make_shared<ExportedAsset>(adoptAsset(*session, std::move(image), "_RuntimeImage.from_numpy()"));
}

py::dict RuntimeImage::metadata() const
{
    py::dict result;
    for (const an::CodecCard& card : m_metadata.cards)
    {
        if (card.keyword().empty())
        {
            continue;
        }
        if (card.keyword().rfind("Exif.", 0) == 0)
        {
            result[py::str(card.keyword())] = py::str(card.value());
            continue;
        }
        if (isReservedMetadataKey(card.keyword()))
        {
            continue;
        }
        result[py::str(card.keyword())] = py::str(cardText(card));
    }
    if (!m_metadata.fileName.empty())
    {
        result[py::str("FILENAME")] = py::str(m_metadata.fileName);
    }
    return result;
}

py::dict RuntimeImage::properties() const
{
    py::dict result;
    const std::optional<an::ImageDescriptor> descriptor = m_asset->asset().descriptor();
    if (!descriptor.has_value())
    {
        return result;
    }
    result[py::str("width")] = py::int_(descriptor->extent().width());
    result[py::str("height")] = py::int_(descriptor->extent().height());
    result[py::str("channels")] = py::int_(descriptor->extent().channelCount());
    result[py::str("pixel_type")] = py::str(sdkSampleTypeName(descriptor->sampleType()));
    result[py::str("pixel_format")] = py::str(sdkPixelFormatName(descriptor->channelSchema()));
    result[py::str("backend")] = py::str("runtime");
    if (!m_metadata.fileName.empty())
    {
        result[py::str("file_name")] = py::str(m_metadata.fileName);
    }
    return result;
}

void RuntimeImage::setMetadata(const std::string& key, const py::object& value)
{
    if (key.empty())
    {
        throw std::runtime_error("Metadata key cannot be empty");
    }
    if (isReservedMetadataKey(key))
    {
        throw std::runtime_error("Metadata key is reserved and cannot be changed: " + key);
    }
    if (lowerCopy(key) == "filename")
    {
        m_metadata.fileName = value.is_none() ? std::string{} : py::str(value).cast<std::string>();
        return;
    }
    require(setHeaderCard(m_metadata, key, lexicalCardValue(value)), "_RuntimeImage.set_metadata()");
}

std::shared_ptr<RuntimeImage> RuntimeImage::clone() const
{
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();
    an::consumer::AssetReadLease lease = m_asset->readForEngine("_RuntimeImage.clone()");
    an::consumer::ImageAsset copy = require(lease.snapshot(), "_RuntimeImage.clone()");
    return std::make_shared<RuntimeImage>(
        std::make_shared<ExportedAsset>(std::make_shared<an::consumer::ImageAsset>(std::move(copy))), m_metadata);
}

py::dict RuntimeImage::exportState() const
{
    const an::consumer::AssetRevision revision = m_asset->asset().revision();
    py::dict result;
    result[py::str("asset")] = py::int_(revision.assetIdentity);
    result[py::str("pixel_generation")] = py::int_(revision.pixelGeneration);
    result[py::str("semantic_generation")] = py::int_(revision.semanticGeneration);
    result[py::str("writable_exports")] = py::int_(m_asset->writableExports());
    result[py::str("externally_mutable")] = py::bool_(m_asset->externallyMutable());
    return result;
}

const std::shared_ptr<ExportedAsset>& RuntimeImage::asset() const noexcept
{
    return m_asset;
}

const SdkMetadata& RuntimeImage::sdkMetadata() const noexcept
{
    return m_metadata;
}

SdkMetadata& RuntimeImage::sdkMetadata() noexcept
{
    return m_metadata;
}

// ---- Free functions.

std::shared_ptr<RuntimeImage> runtimeImageOf(const an::consumer::EngineSession& session,
                                             const an::consumer::OperationScope& scope, an::consumer::AnyImage&& image,
                                             SdkMetadata metadata, const std::string_view context)
{
    an::consumer::AnyImage compact = require(compactImageOf(scope, std::move(image)), context);
    return std::make_shared<RuntimeImage>(
        std::make_shared<ExportedAsset>(adoptAsset(session, std::move(compact), context)), std::move(metadata));
}

an::consumer::AnyImage imageFromArray(const an::consumer::OperationScope& scope, const py::array& array)
{
    const py::buffer_info info = array.request();
    const ArrayShape shape = arrayShapeOf(info);
    const an::SampleType type = sampleTypeOfArray(array);
    const an::ImageDescriptor descriptor =
        require(describeSdkImage(scope.engine().limits(), shape.width, shape.height, shape.channels, type),
                "image_from_numpy()");
    std::optional<an::consumer::AnyImage> image;
    (void)an::dispatchSampleType(type, [&]<typename SampleT>() {
        an::Image<SampleT> typed =
            require(createCompactImage<SampleT>(scope.engine(), descriptor, an::StorageInitialization::Uninitialized),
                    "image_from_numpy()");
        {
            std::optional<an::ImageWriteAccess<SampleT>> access = typed.write();
            if (!access.has_value())
            {
                throw std::runtime_error("Runtime image channel buffer is null");
            }
            copyArraySamples<SampleT>(info, shape, access->view().data());
        }
        image = an::consumer::AnyImage(std::move(typed));
    });
    if (!image.has_value())
    {
        throw std::runtime_error("Unsupported runtime image pixel type");
    }
    return std::move(*image);
}

std::string lexicalCardValue(const py::handle& value)
{
    if (value.is_none())
    {
        return {};
    }
    if (py::isinstance<py::bool_>(value))
    {
        return value.cast<bool>() ? "T" : "F";
    }
    if (py::isinstance<py::int_>(value) || py::isinstance<py::float_>(value))
    {
        return py::str(value).cast<std::string>();
    }
    const std::string text = py::str(value).cast<std::string>();
    std::string quoted = "'";
    for (const char character : text)
    {
        quoted.push_back(character);
        if (character == '\'')
        {
            quoted.push_back('\'');
        }
    }
    quoted.push_back('\'');
    return quoted;
}

void applyMetadataDict(const py::dict& values, SdkMetadata& metadata)
{
    for (const auto item : values)
    {
        const std::string key = py::str(item.first).cast<std::string>();
        if (key.empty() || isReservedMetadataKey(key))
        {
            continue;
        }
        if (lowerCopy(key) == "filename")
        {
            metadata.fileName = item.second.is_none() ? std::string{} : py::str(item.second).cast<std::string>();
            continue;
        }
        require(setHeaderCard(metadata, key, lexicalCardValue(item.second)), "metadata");
    }
}

const std::shared_ptr<RuntimeImage>& requireRuntimeImage(const std::shared_ptr<RuntimeImage>& image,
                                                         const std::string_view parameterName)
{
    if (!image)
    {
        throw std::runtime_error("Missing runtime image for parameter '" + std::string(parameterName) + "'");
    }
    return image;
}

} // namespace an::sdk
