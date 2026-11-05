// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the shared translation from an image statistics selection to a kernel one.

#include "algorithms/statistics/StatisticsSelectionInternal.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <cstdint>

namespace an::statistics_detail
{
namespace
{

/// One failure from this shared translation, carrying the caller's operation name.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status selectionFailure(ErrorCode code, const char* operation, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

} // namespace

Result<ResolvedSelection> resolveStatisticsSelection(SampleType sampleType, const void* samples,
                                                     const ImageLayout& layout, std::uint32_t channelIndex,
                                                     const StatisticsSelection& selection, const char* operation)
{
    if (samples == nullptr || !isSampleType(sampleType) || layout.elementSizeBytes() != sampleTypeSizeBytes(sampleType))
    {
        return selectionFailure(ErrorCode::InvalidArgument, operation,
                                "the statistics source has a null pointer, unknown sample type, or mismatched layout");
    }
    if (channelIndex >= layout.extent().channelCount())
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{
                .message = "the statistics channel is outside the source image",
                .operation = operation,
                .domain = ErrorDomain::Algorithms,
                .context = {StatusContextField::ofText(ContextKey::Parameter, "channelIndex"),
                            StatusContextField::ofCount(ContextKey::Requested, channelIndex),
                            StatusContextField::ofCount(ContextKey::Available, layout.extent().channelCount())}});
    }

    const StatisticsRegion region = selection.region.value_or(StatisticsRegion{
        .originX = 0u, .originY = 0u, .width = layout.extent().width(), .height = layout.extent().height()});
    const std::uint32_t imageWidth = layout.extent().width();
    const std::uint32_t imageHeight = layout.extent().height();
    if (region.originX > imageWidth || region.originY > imageHeight || region.width > imageWidth - region.originX ||
        region.height > imageHeight - region.originY)
    {
        return selectionFailure(ErrorCode::InvalidArgument, operation,
                                "the statistics region extends outside the source image");
    }

    std::uint32_t maskPlane = 0u;
    if (!selection.validity.needsMask())
    {
        if (selection.mask != nullptr)
        {
            return selectionFailure(ErrorCode::InvalidArgument, operation,
                                    "a packed mask was supplied for a statistics validity rule that does not use one");
        }
    }
    else
    {
        if (selection.mask == nullptr)
        {
            return selectionFailure(ErrorCode::InvalidArgument, operation,
                                    "explicit-mask statistics were given no packed mask");
        }
        if (selection.mask->width() != imageWidth || selection.mask->height() != imageHeight ||
            (selection.mask->planeCount() != 1u && selection.mask->planeCount() != layout.extent().channelCount()))
        {
            return selectionFailure(
                ErrorCode::IncompatibleArguments, operation,
                "the statistics mask does not cover the source image with one shared plane or one plane per channel");
        }
        maskPlane = selection.mask->planeCount() == 1u ? 0u : channelIndex;
    }

    return ResolvedSelection{
        .source = KernelPlaneSource{.sampleType = sampleType,
                                    .samples = samples,
                                    .layout = &layout,
                                    .plane = channelIndex / layout.samplesPerPixel(),
                                    .sampleInPixel = channelIndex % layout.samplesPerPixel()},
        .validity =
            KernelValidityView{.descriptor = selection.validity, .mask = selection.mask, .maskPlane = maskPlane},
        .region = KernelPlaneSelection{
            .originX = region.originX, .originY = region.originY, .width = region.width, .height = region.height}};
}

bool rangesOverlap(const void* first, std::uint64_t firstBytes, const void* second, std::uint64_t secondBytes) noexcept
{
    if (first == nullptr || second == nullptr || firstBytes == 0u || secondBytes == 0u)
    {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(first);
    const auto otherBegin = reinterpret_cast<std::uintptr_t>(second);
    return begin < otherBegin + secondBytes && otherBegin < begin + firstBytes;
}

} // namespace an::statistics_detail
