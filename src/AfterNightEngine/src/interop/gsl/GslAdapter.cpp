// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Interop/GslAdapter.h>

#include <AfterNightEngine/Core/ImageExtent.h>

// The two entry points that turn a matrix's own fields back into a checked engine geometry. They
// are here rather than beside the constant expressions in the header for the same reason the other
// two adapters' are: building an extent and a layout reaches the whole geometry stack, and a header
// that did that would charge every translation unit describing a matrix for it.
//
// Describing a matrix needs no GSL header. StudentDistribution in this directory reaches the
// library itself; the include rules allow an adapter under interop/gsl to include GSL and no other
// external library.

namespace an
{
namespace
{

/// The largest extent an engine image may have in one dimension, as a `size_t` comparison.
///
/// GSL indexes with `size_t`, so nothing about a matrix's own type refuses a large one; what
/// refuses it is the engine's own extent, and this is the ceiling that check would apply anyway,
/// applied before a cast that could truncate it.
constexpr std::size_t kMaxEngineDimension = static_cast<std::size_t>(0xffffffffu);

} // namespace

BridgeRefusal adoptGslMatrixCheck(const GslMatrixDescription& description,
                                  const ExternalBufferContract& contract) noexcept
{
    // A GSL matrix is always writable through its own pointer, so the mutability rule can never
    // refuse this direction; what the contract still decides is the two domains and whose memory
    // outlives whose.
    const BridgeRefusal claim = checkExternalBufferContract(contract, true);
    if (claim != BridgeRefusal::None)
    {
        return claim;
    }
    if (!isGslPrecision(description.precision))
    {
        return BridgeRefusal::UnknownContract;
    }
    if (description.data == nullptr)
    {
        return BridgeRefusal::NoSamples;
    }
    if (description.size1 == 0u || description.size2 == 0u || description.size1 > kMaxEngineDimension ||
        description.size2 > kMaxEngineDimension)
    {
        return BridgeRefusal::ExtentExceedsLibraryIndex;
    }
    if (description.tda < description.size2)
    {
        return BridgeRefusal::RowStrideNotRepresentable;
    }
    return BridgeRefusal::None;
}

std::optional<ImageLayout> gslMatrixLayout(const GslMatrixDescription& description, const EngineLimits& limits) noexcept
{
    if (!isGslPrecision(description.precision))
    {
        return std::nullopt;
    }
    if (description.size1 == 0u || description.size2 == 0u || description.size1 > kMaxEngineDimension ||
        description.size2 > kMaxEngineDimension || description.tda < description.size2)
    {
        return std::nullopt;
    }

    const std::optional<SampleType> type = gslSampleTypeOf(description.precision);
    if (!type.has_value())
    {
        return std::nullopt;
    }

    // Columns are the fast dimension in a row-major matrix, so `size2` is the width and `size1` is
    // the height. Getting that pair the wrong way round is the single most common mistake made with
    // this structure, and it produces a layout that is valid, wrong, and silent.
    const std::optional<ImageExtent> extent = ImageExtent::create(
        static_cast<std::uint32_t>(description.size2), static_cast<std::uint32_t>(description.size1), 1u, limits);
    if (!extent.has_value())
    {
        return std::nullopt;
    }

    const std::uint32_t elementSize = sampleTypeSizeBytes(*type);
    const ImageLayoutRequest request{.extent = *extent,
                                     .elementSizeBytes = elementSize,
                                     .samplesPerPixel = 1u,
                                     .planeCount = 1u,
                                     .rowAlignmentBytes = 1u};
    const std::uint64_t rowStrideBytes =
        static_cast<std::uint64_t>(description.tda) * static_cast<std::uint64_t>(elementSize);
    const std::uint64_t planeStrideBytes = rowStrideBytes * static_cast<std::uint64_t>(description.size1);
    return ImageLayout::createWithStrides(request, rowStrideBytes, planeStrideBytes, limits);
}

} // namespace an
