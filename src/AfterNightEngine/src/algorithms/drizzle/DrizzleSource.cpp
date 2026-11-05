// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - row windows over borrowed drizzle frames, and the resident reader built on
// them.
//
// A window is the same bytes seen through a narrower layout: the row and plane strides are kept,
// the extent loses rows, and the span starts at the first requested row. Nothing is copied, so a
// window stays valid exactly as long as the frame it was cut from. The resident reader checks every
// frame once, at creation, by cutting its full window.

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleSource.h>

#include <AfterNightEngine/Runtime/ExecutionContext.h>

namespace an
{
namespace
{
Status failure(ErrorCode code, const char* message)
{
    return Status::failure(
        code,
        StatusDetail{.message = message, .operation = "an::drizzleSourceRows", .domain = ErrorDomain::Algorithms});
}

Result<DrizzleSampleBand> rowsOf(const DrizzleSampleBand& frame, std::uint32_t first, std::uint32_t rows)
{
    const auto& layout = frame.layout;
    const auto& extent = layout.extent();
    if (rows == 0u || first >= extent.height() || rows > extent.height() - first)
        return failure(ErrorCode::OutOfRange, "source rows lie outside the resident band");
    const auto bytes = sampleTypeSizeBytes(frame.sampleType);
    if (bytes == 0u || layout.elementSizeBytes() != bytes || frame.samples.data() == nullptr ||
        frame.samples.size() < layout.totalBytes() ||
        reinterpret_cast<std::uintptr_t>(frame.samples.data()) % bytes != 0u)
        return failure(ErrorCode::InvalidArgument,
                       "source samples are short, misaligned or have the wrong element size");
    // A window can only narrow this already checked layout. Reapplying process defaults here
    // would refuse a source accepted under the caller's explicitly raised geometry limits.
    const EngineLimits limits{extent.width(), extent.height(), extent.channelCount(), layout.totalBytes(),
                              layout.totalBytes()};
    const auto cropped = ImageExtent::create(extent.width(), rows, extent.channelCount(), limits);
    if (!cropped)
        return failure(ErrorCode::LimitExceeded, "source window extent cannot be represented");
    const auto window =
        ImageLayout::createWithStrides({*cropped, bytes, layout.samplesPerPixel(), layout.planeCount(), 1u},
                                       layout.rowStrideBytes(), layout.planeStrideBytes(), limits);
    if (!window)
        return failure(ErrorCode::LimitExceeded, "source window strides cannot be represented");
    const auto skipped = static_cast<std::size_t>(first * layout.rowStrideBytes());
    if (skipped > frame.samples.size() || window->totalBytes() > frame.samples.size() - skipped)
        return failure(ErrorCode::InvalidArgument, "source window exceeds readable storage");
    return DrizzleSampleBand{*window, frame.samples.subspan(skipped, window->totalBytes()), frame.sampleType};
}
} // namespace

Result<DrizzleFrameData> drizzleSourceRows(const DrizzleFrameData& frame, std::uint32_t firstRow, std::uint32_t rows)
{
    auto pixels = rowsOf(frame.pixels, firstRow, rows);
    if (!pixels.hasValue())
        return pixels.status();
    DrizzleFrameData result{*pixels};
    if (frame.mask)
    {
        const auto& mask = *frame.mask;
        if (mask.extent() != frame.pixels.layout.extent())
            return failure(ErrorCode::IncompatibleArguments, "source mask must cover exactly every source plane");
        const auto skipped = static_cast<std::size_t>(firstRow * mask.rowStrideBytes());
        result.mask = ValidityMaskView<const std::uint8_t>::create(
            {mask.data() + skipped, static_cast<std::size_t>(mask.spannedByteCount()) - skipped},
            pixels->layout.extent(), mask.rowStrideBytes(), mask.planeStrideBytes());
        if (!result.mask)
            return failure(ErrorCode::InvalidArgument, "source mask window cannot be represented");
    }
    for (const auto pair :
         {std::pair{&frame.pixelWeights, &result.pixelWeights}, std::pair{&frame.flatWeights, &result.flatWeights}})
    {
        if (!pair.first->has_value())
            continue;
        const auto& extent = (**pair.first).layout.extent();
        const auto& source = frame.pixels.layout.extent();
        if (extent.width() != source.width() || extent.height() != source.height() ||
            (extent.channelCount() != 1u && extent.channelCount() != source.channelCount()))
            return failure(ErrorCode::IncompatibleArguments, "auxiliary weights must cover the source rows and planes");
        auto cropped = rowsOf(**pair.first, firstRow, rows);
        if (!cropped.hasValue())
            return cropped.status();
        *pair.second = *cropped;
    }
    return result;
}

Result<ResidentDrizzleFrameSource> ResidentDrizzleFrameSource::create(std::span<const DrizzleFrameData> frames,
                                                                      DrizzleSourceAccess access)
{
    if (frames.empty())
        return failure(ErrorCode::EmptySelection, "a resident reader needs at least one frame");
    if (frames.size() > kMaxDrizzleFrames)
        return failure(ErrorCode::LimitExceeded, "too many resident drizzle frames");
    if (access != DrizzleSourceAccess::WholeFrame && access != DrizzleSourceAccess::Region)
        return failure(ErrorCode::InvalidArgument, "unknown drizzle source access");
    for (const auto& frame : frames)
    {
        if (frame.pixels.layout.extent() != frames.front().pixels.layout.extent() ||
            frame.pixels.sampleType != frames.front().pixels.sampleType)
            return failure(ErrorCode::IncompatibleArguments,
                           "resident frames have different representations or extents");
        const auto checked = drizzleSourceRows(frame, 0u, frame.pixels.layout.extent().height());
        if (!checked.hasValue())
            return checked.status();
    }
    return ResidentDrizzleFrameSource{frames, access};
}

Result<DrizzleFrameData> ResidentDrizzleFrameSource::read(const ExecutionContext& execution, std::uint32_t frameIndex,
                                                          std::uint32_t firstRow, std::uint32_t rows,
                                                          const DrizzleReadBuffers& buffers)
{
    static_cast<void>(buffers);
    if (!execution.valid())
        return failure(ErrorCode::NotInitialized, "resident read requires an initialized execution");
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    if (frameIndex >= m_frames.size())
        return failure(ErrorCode::OutOfRange, "frame index lies outside the source");
    const auto& frame = m_frames[frameIndex];
    if (m_access == DrizzleSourceAccess::WholeFrame &&
        (firstRow != 0u || rows != frame.pixels.layout.extent().height()))
        return failure(ErrorCode::InvalidArgument, "whole-frame reader requires a complete frame request");
    return drizzleSourceRows(frame, firstRow, rows);
}

} // namespace an
