// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEnginePythonAdapter - the reverse direction of the buffer bridge: judging a buffer
// Python owns as engine samples, and the layout that addresses them.
//
// A three-axis buffer is read as planes, rows and columns or as rows, columns and samples, as the
// caller states, because its fields cannot say which; a two-axis buffer is one plane either way. The
// rules run in a fixed order so a buffer with two problems always reports the same one. Nothing here
// includes Python: the description is the adapter's own, so this builds the same in both
// configurations.

#include <AfterNightEnginePythonAdapter/PythonBufferBridge.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Interop/ExternalBuffer.h>

#include <cstdint>
#include <optional>

namespace an
{
namespace
{

/// The largest value an engine extent or layout field can hold, as a buffer's own signed index.
constexpr std::int64_t kMaxEngineIndex = 0xffffffff;

/// How a three-axis buffer's axes are read, once the caller has said which arrangement it means.
struct AxisReading
{
    /// How many planes the buffer describes.
    std::int64_t planes;

    /// How many rows each plane holds.
    std::int64_t height;

    /// How many pixels each row holds.
    std::int64_t width;

    /// How many of a pixel's samples are adjacent.
    std::int64_t samplesPerPixel;

    /// The byte distance between the first samples of consecutive rows.
    std::int64_t rowStride;

    /// The byte distance between the first samples of consecutive planes.
    std::int64_t planeStride;
};

/// Which of the axis readings this description and this arrangement describe, or nothing.
///
/// Two axes are one plane of one sample per pixel whichever arrangement is stated, because those are
/// the same bytes. Three axes are the arrangement the caller stated, and there is no way to tell one
/// from the other by looking - which is the whole reason the caller states it.
[[nodiscard]] std::optional<AxisReading> readAxes(const PythonBufferDescription& description,
                                                  StorageArrangement arrangement)
{
    if (description.axes == 2)
    {
        return AxisReading{.planes = 1,
                           .height = description.shape[0],
                           .width = description.shape[1],
                           .samplesPerPixel = 1,
                           .rowStride = description.strides[0],
                           .planeStride = description.strides[0] * description.shape[0]};
    }
    if (description.axes != 3)
    {
        return std::nullopt;
    }
    if (arrangement == StorageArrangement::Planar)
    {
        return AxisReading{.planes = description.shape[0],
                           .height = description.shape[1],
                           .width = description.shape[2],
                           .samplesPerPixel = 1,
                           .rowStride = description.strides[1],
                           .planeStride = description.strides[0]};
    }
    if (arrangement == StorageArrangement::Interleaved)
    {
        return AxisReading{.planes = 1,
                           .height = description.shape[0],
                           .width = description.shape[1],
                           .samplesPerPixel = description.shape[2],
                           .rowStride = description.strides[0],
                           .planeStride = description.strides[0] * description.shape[0]};
    }
    return std::nullopt;
}

/// The refusal a description earns on its own terms, before any layout is reconstructed.
///
/// The order is a contract, for the reason every other bridge in this engine states one: a buffer with
/// two problems reports the same one on every build, and a caller repairing them does so in a fixed
/// order.
[[nodiscard]] BridgeRefusal checkForeignBuffer(const PythonBufferDescription& description,
                                               StorageArrangement arrangement)
{
    if (description.data == nullptr)
    {
        return BridgeRefusal::NoSamples;
    }
    if (!isPythonBufferFormat(description.format) || !isStorageArrangement(arrangement))
    {
        return BridgeRefusal::UnknownContract;
    }
    if (arrangement == StorageArrangement::SemiPlanar)
    {
        // Several planes, each interleaving several of a pixel's samples. Describing it needs a
        // fourth axis, and flattening one into another would conflate a channel with a column.
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    const std::optional<SampleType> type = pythonSampleTypeOf(description.format);
    if (!type.has_value() || description.itemSizeBytes != static_cast<std::int64_t>(sampleTypeSizeBytes(*type)))
    {
        return BridgeRefusal::SampleTypeNotRepresentable;
    }
    const std::optional<AxisReading> axes = readAxes(description, arrangement);
    if (!axes.has_value())
    {
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    if (description.strides[static_cast<std::size_t>(description.axes - 1)] != description.itemSizeBytes)
    {
        // A sliced or transposed array. Contiguous in Python's terms on the axes that matter to it,
        // and not a row of samples in this engine's terms.
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    if (axes->width <= 0 || axes->height <= 0 || axes->planes <= 0 || axes->samplesPerPixel <= 0 ||
        axes->width > kMaxEngineIndex || axes->height > kMaxEngineIndex || axes->planes > kMaxEngineIndex ||
        axes->samplesPerPixel > kMaxEngineIndex || axes->planes * axes->samplesPerPixel > kMaxEngineIndex)
    {
        return BridgeRefusal::ExtentExceedsLibraryIndex;
    }
    if (description.axes == 3 && arrangement == StorageArrangement::Interleaved &&
        description.strides[1] != axes->samplesPerPixel * description.itemSizeBytes)
    {
        // A slice between pixels can leave the sample axis contiguous while putting a gap between
        // adjacent pixels. ImageLayout has no pixel-stride field, so accepting it would address the
        // second pixel at the first one's padding.
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    const std::int64_t rowBytes = axes->width * axes->samplesPerPixel * description.itemSizeBytes;
    if (axes->rowStride <= 0 || axes->rowStride < rowBytes)
    {
        // A negative stride is a reversed array, which an engine layout cannot hold; a stride below a
        // row's own bytes is rows that overlap, which is not an image.
        return BridgeRefusal::RowStrideNotRepresentable;
    }
    if (axes->planes > 1 && (axes->planeStride <= 0 || axes->planeStride < axes->rowStride * axes->height))
    {
        return BridgeRefusal::RowStrideNotRepresentable;
    }
    return BridgeRefusal::None;
}

} // namespace

BridgeRefusal adoptPythonBufferCheck(const PythonBufferDescription& description, const ExternalBufferContract& contract,
                                     StorageArrangement arrangement) noexcept
{
    // The claim first, exactly as every other adapter in this engine applies it, so a refusal about
    // what the samples mean is never hidden behind one about how they sit. The samples of a foreign
    // buffer are mutable when the buffer says they are.
    const BridgeRefusal claim = checkExternalBufferContract(contract, !description.readOnly);
    if (claim != BridgeRefusal::None)
    {
        return claim;
    }
    return checkForeignBuffer(description, arrangement);
}

std::optional<ImageLayout> pythonBufferLayout(const PythonBufferDescription& description,
                                              StorageArrangement arrangement, const EngineLimits& limits) noexcept
{
    if (checkForeignBuffer(description, arrangement) != BridgeRefusal::None)
    {
        return std::nullopt;
    }
    const std::optional<AxisReading> axes = readAxes(description, arrangement);
    const std::optional<SampleType> type = pythonSampleTypeOf(description.format);
    if (!axes.has_value() || !type.has_value())
    {
        return std::nullopt;
    }
    const std::optional<ImageExtent> extent =
        ImageExtent::create(static_cast<std::uint32_t>(axes->width), static_cast<std::uint32_t>(axes->height),
                            static_cast<std::uint32_t>(axes->planes * axes->samplesPerPixel), limits);
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    const ImageLayoutRequest request{.extent = *extent,
                                     .elementSizeBytes = sampleTypeSizeBytes(*type),
                                     .samplesPerPixel = static_cast<std::uint32_t>(axes->samplesPerPixel),
                                     .planeCount = static_cast<std::uint32_t>(axes->planes),
                                     .rowAlignmentBytes = 1u};
    return ImageLayout::createWithStrides(request, static_cast<std::uint64_t>(axes->rowStride),
                                          static_cast<std::uint64_t>(axes->planeStride), limits);
}

std::optional<SampleType> pythonBufferSampleType(const PythonBufferDescription& description) noexcept
{
    if (!isPythonBufferFormat(description.format))
    {
        return std::nullopt;
    }
    return pythonSampleTypeOf(description.format);
}

} // namespace an
