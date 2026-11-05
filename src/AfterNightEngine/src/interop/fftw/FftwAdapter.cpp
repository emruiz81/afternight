// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Interop/FftwAdapter.h>

#include <AfterNightEngine/Core/ImageExtent.h>

// The two entry points that turn a plan's own numbers back into a checked engine geometry. They are
// here rather than beside the constant expressions in the header for the same reason the OpenCV
// adapter's are: building an extent and a layout reaches the whole geometry stack, and a header that
// did that would charge every translation unit describing an array for it.
//
// No FFTW header is included here: describing an array needs none. `FftwRealTransform.cpp` beside
// this file is the one unit that includes the library, to plan and execute the engine's own
// transforms, and the include rules let a unit under interop/fftw include FFTW and no other external
// library.

namespace an
{
namespace
{

/// The largest value FFTW's `int` dimensions and strides can hold.
constexpr std::uint64_t kMaxFftwIndex = 0x7fffffffu;

/// How many reals one row of a described array actually holds, as opposed to how far apart the rows
/// are.
///
/// @param description the array to measure.
/// @return the count, which is `n1` for a real array and twice that for an interleaved complex one.
[[nodiscard]] std::uint64_t rowRealsOf(const FftwArrayDescription& description) noexcept
{
    const auto columns = static_cast<std::uint64_t>(description.n1);
    return description.kind == FftwArrayKind::InterleavedComplex ? columns * 2u : columns;
}

} // namespace

BridgeRefusal adoptFftwArrayCheck(const FftwArrayDescription& description,
                                  const ExternalBufferContract& contract) noexcept
{
    // An FFTW array is always writable through its own pointer - a plan is free to destroy its
    // input - so the mutability rule can never refuse this direction.
    const BridgeRefusal claim = checkExternalBufferContract(contract, true);
    if (claim != BridgeRefusal::None)
    {
        return claim;
    }
    if (!isFftwPrecision(description.precision) || !isFftwArrayKind(description.kind))
    {
        return BridgeRefusal::UnknownContract;
    }
    if (description.data == nullptr)
    {
        return BridgeRefusal::NoSamples;
    }
    if (description.n0 <= 0 || description.n1 <= 0 || static_cast<std::uint64_t>(description.n0) > kMaxFftwIndex ||
        static_cast<std::uint64_t>(description.n1) > kMaxFftwIndex)
    {
        return BridgeRefusal::ExtentExceedsLibraryIndex;
    }
    if (description.rowStrideReals <= 0)
    {
        return BridgeRefusal::RowStrideNotRepresentable;
    }
    if (static_cast<std::uint64_t>(description.rowStrideReals) < rowRealsOf(description))
    {
        return BridgeRefusal::RowStrideNotRepresentable;
    }
    if (!description.alignmentSatisfied)
    {
        return BridgeRefusal::AlignmentInsufficient;
    }
    return BridgeRefusal::None;
}

std::optional<ImageLayout> fftwArrayLayout(const FftwArrayDescription& description, const EngineLimits& limits) noexcept
{
    if (!isFftwPrecision(description.precision) || !isFftwArrayKind(description.kind))
    {
        return std::nullopt;
    }
    if (description.n0 <= 0 || description.n1 <= 0 || description.rowStrideReals <= 0)
    {
        return std::nullopt;
    }

    const std::optional<SampleType> type = fftwSampleTypeOf(description.precision);
    if (!type.has_value())
    {
        return std::nullopt;
    }
    const std::uint64_t rowReals = rowRealsOf(description);
    if (rowReals == 0u || rowReals > kMaxFftwIndex || static_cast<std::uint64_t>(description.rowStrideReals) < rowReals)
    {
        return std::nullopt;
    }

    // A view over a complex array describes its reals, because that is what a typed pointer can
    // reach: the engine has no complex sample type, and inventing one here would put a second
    // representation into the image model for the benefit of one library.
    const std::optional<ImageExtent> extent = ImageExtent::create(
        static_cast<std::uint32_t>(rowReals), static_cast<std::uint32_t>(description.n0), 1u, limits);
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
        static_cast<std::uint64_t>(description.rowStrideReals) * static_cast<std::uint64_t>(elementSize);
    const std::uint64_t planeStrideBytes = rowStrideBytes * static_cast<std::uint64_t>(description.n0);
    return ImageLayout::createWithStrides(request, rowStrideBytes, planeStrideBytes, limits);
}

} // namespace an
