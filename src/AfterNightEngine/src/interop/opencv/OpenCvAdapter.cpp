// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Interop/OpenCvAdapter.h>

#include <AfterNightEngine/Core/ImageExtent.h>

// The three entry points a matrix cannot answer from a header alone, and the reason they are here
// rather than beside the constant expressions next to them: each one builds a checked engine
// geometry, which means reaching the extent and layout constructors, and a header that did that
// would make every translation unit describing a matrix pay for the whole geometry stack.
//
// No OpenCV header is included, here or anywhere in this engine. This directory is where one would
// go when an algorithm needs the library itself, and the include rules already say that an adapter
// under interop/opencv may include OpenCV and nothing else. What this file needs is arithmetic over
// numbers a matrix reports about itself, and those are integers.

namespace an
{
namespace
{

/// The largest value a matrix's `int` dimensions can hold.
constexpr std::uint64_t kMaxMatIndex = 0x7fffffffu;

} // namespace

BridgeRefusal adoptOpenCvMatCheck(const OpenCvMatDescription& description, const ExternalBufferContract& contract,
                                  const OpenCvTypeLayout& typeLayout) noexcept
{
    // A matrix is always writable through its own pointer, so the mutability rule can never refuse
    // this direction; what the contract still decides is the two domains and whose memory outlives
    // whose.
    const BridgeRefusal claim = checkExternalBufferContract(contract, true);
    if (claim != BridgeRefusal::None)
    {
        return claim;
    }
    if (!isOpenCvTypeLayout(typeLayout))
    {
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    if (description.data == nullptr)
    {
        return BridgeRefusal::NoSamples;
    }
    if (description.rows <= 0 || description.cols <= 0 || static_cast<std::uint64_t>(description.rows) > kMaxMatIndex ||
        static_cast<std::uint64_t>(description.cols) > kMaxMatIndex)
    {
        return BridgeRefusal::ExtentExceedsLibraryIndex;
    }

    const std::optional<OpenCvDepth> depth = openCvDepthOfTypeCode(description.type, typeLayout);
    if (!depth.has_value())
    {
        return BridgeRefusal::SampleTypeNotRepresentable;
    }
    const std::optional<SampleType> type = openCvSampleTypeOf(*depth);
    if (!type.has_value())
    {
        return BridgeRefusal::SampleTypeNotRepresentable;
    }
    const std::optional<std::int32_t> channels = openCvChannelsOfTypeCode(description.type, typeLayout);
    if (!channels.has_value())
    {
        return BridgeRefusal::ChannelCountNotRepresentable;
    }

    // A step that is not a whole number of samples puts every row after the first at an address no
    // typed pointer reaches. OpenCV allows it - a submatrix of an odd-width parent has one - and the
    // engine's own layout refuses it, so it is named here rather than reported as a stride that was
    // simply too small.
    const std::uint32_t elementSize = sampleTypeSizeBytes(*type);
    const auto stepBytes = static_cast<std::uint64_t>(description.stepBytes);
    if (elementSize == 0u || stepBytes % static_cast<std::uint64_t>(elementSize) != 0u)
    {
        return BridgeRefusal::RowStrideNotRepresentable;
    }
    const std::uint64_t minimumRow = static_cast<std::uint64_t>(description.cols) *
                                     static_cast<std::uint64_t>(*channels) * static_cast<std::uint64_t>(elementSize);
    if (stepBytes < minimumRow)
    {
        return BridgeRefusal::RowStrideNotRepresentable;
    }
    return BridgeRefusal::None;
}

std::optional<SampleType> openCvMatSampleType(const OpenCvMatDescription& description,
                                              const OpenCvTypeLayout& typeLayout) noexcept
{
    const std::optional<OpenCvDepth> depth = openCvDepthOfTypeCode(description.type, typeLayout);
    if (!depth.has_value())
    {
        return std::nullopt;
    }
    return openCvSampleTypeOf(*depth);
}

std::optional<ImageLayout> openCvMatLayout(const OpenCvMatDescription& description, const OpenCvTypeLayout& typeLayout,
                                           const EngineLimits& limits) noexcept
{
    const std::optional<SampleType> type = openCvMatSampleType(description, typeLayout);
    const std::optional<std::int32_t> channels = openCvChannelsOfTypeCode(description.type, typeLayout);
    if (!type.has_value() || !channels.has_value() || description.rows <= 0 || description.cols <= 0)
    {
        return std::nullopt;
    }

    const std::optional<ImageExtent> extent =
        ImageExtent::create(static_cast<std::uint32_t>(description.cols), static_cast<std::uint32_t>(description.rows),
                            static_cast<std::uint32_t>(*channels), limits);
    if (!extent.has_value())
    {
        return std::nullopt;
    }

    // One interleaved plane, which is what a matrix's memory is: every sample of a pixel adjacent,
    // and one buffer for the whole image. Alignment is stated as one because the matrix's base
    // address belongs to OpenCV's allocator and the engine is describing it, not requiring it.
    const ImageLayoutRequest request{.extent = *extent,
                                     .elementSizeBytes = sampleTypeSizeBytes(*type),
                                     .samplesPerPixel = static_cast<std::uint32_t>(*channels),
                                     .planeCount = 1u,
                                     .rowAlignmentBytes = 1u};
    const auto rowStrideBytes = static_cast<std::uint64_t>(description.stepBytes);
    const std::uint64_t planeStrideBytes = rowStrideBytes * static_cast<std::uint64_t>(description.rows);
    return ImageLayout::createWithStrides(request, rowStrideBytes, planeStrideBytes, limits);
}

} // namespace an
