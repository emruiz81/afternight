// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the argument and answer checks the sequence handles share.
//
// Each check refuses with a typed code and a `SequenceFailure` reason and never calls a backend; the
// conditions are listed on the declarations. The same position check serves a frame read and a frame
// appended, so a source and a sink agree about what a well-formed timeline is.

#include "io/sequences/SequenceInternal.h"

namespace an::sequence_detail
{
Status checkContext(std::uint64_t contextId, const ExecutionContext& execution) noexcept
{
    if (!execution.valid())
        return Status::failure(ErrorCode::NotInitialized);
    if (contextId != execution.engineContextId())
        return Status::failure(ErrorCode::IncompatibleArguments);
    return execution.checkCancellation();
}

Status checkDescription(const EngineContext& engine, const SequenceDescriptor& descriptor) noexcept
{
    if (!engine.valid())
        return Status::failure(ErrorCode::NotInitialized);
    if (const auto checked = validateSequenceDescriptor(descriptor); checked.failed())
        return checked;
    if (descriptor.fixedFrame)
    {
        const auto& frame = *descriptor.fixedFrame;
        const auto& extent = frame.extent();
        if (!ImageExtent::create(extent.width(), extent.height(), extent.channelCount(), engine.limits()) ||
            !ImageLayout::create({extent, frame.sampleFormat().sizeBytes, 1u, extent.channelCount(), 1u},
                                 engine.limits()))
            return sequenceFailure(ErrorCode::LimitExceeded, SequenceFailure::DescriptorMismatch);
    }
    return Status::success();
}

Status checkPosition(const SequenceDescriptor& descriptor, const SequencePosition& position,
                     std::uint64_t expectedIndex, std::optional<std::int64_t> previousTicks) noexcept
{
    const auto ticks = position.presentationTicks;
    const auto duration = position.durationTicks;
    if (position.index != expectedIndex || position.index == (std::numeric_limits<std::uint64_t>::max)() ||
        (descriptor.frameCount && position.index >= *descriptor.frameCount) ||
        (descriptor.timestampsRequired && !ticks) || ((ticks || duration) && !descriptor.timeBase) ||
        (ticks && (*ticks < 0 || (previousTicks && *ticks < *previousTicks) ||
                   (descriptor.durationTicks && *ticks >= *descriptor.durationTicks))) ||
        (duration && (*duration <= 0 || !ticks || *duration > (std::numeric_limits<std::int64_t>::max)() - *ticks ||
                      (descriptor.durationTicks && *duration > *descriptor.durationTicks - *ticks))))
        return sequenceFailure(ErrorCode::OutOfRange, SequenceFailure::InvalidPosition);
    return Status::success();
}

Status checkFrame(const EngineContext& engine, const SequenceDescriptor& descriptor, const SequenceFrame& frame,
                  bool source) noexcept
{
    const auto description = frame.frame.descriptor();
    const auto layout = frame.frame.layout();
    if (!frame.frame.valid() || !description || !layout ||
        (descriptor.fixedFrame && *descriptor.fixedFrame != *description))
        return sequenceFailure(ErrorCode::IncompatibleArguments, SequenceFailure::DescriptorMismatch);
    const auto& extent = description->extent();
    if (!ImageExtent::create(extent.width(), extent.height(), extent.channelCount(), engine.limits()) ||
        ImageLayout::checkWithStrides(
            {extent, description->sampleFormat().sizeBytes, layout->samplesPerPixel(), layout->planeCount(), 1u},
            layout->rowStrideBytes(), layout->planeStrideBytes(), engine.limits()) != GeometryError::None)
        return sequenceFailure(ErrorCode::LimitExceeded, SequenceFailure::DescriptorMismatch);
    if (source)
    {
        if (frame.frame.buffer().ownership() == SampleBufferOwnership::Borrowed)
            return sequenceFailure(ErrorCode::InvalidState, SequenceFailure::UnsafeLifetime);
        if (frame.frame.buffer().ownership() == SampleBufferOwnership::Owned &&
            frame.frame.buffer().contextId() != engine.id())
            return sequenceFailure(ErrorCode::IncompatibleArguments, SequenceFailure::ForeignAllocation);
        if (frame.metadata.outputIntent())
            return sequenceFailure(ErrorCode::InvalidData, SequenceFailure::InvalidMetadata);
    }
    return Status::success();
}

Status checkSeek(const SequenceDescriptor& descriptor, const SequenceSeekRequest& request) noexcept
{
    switch (request.kind)
    {
    case SequenceSeekKind::Index:
        if (!descriptor.seekByIndex)
            return sequenceFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedSeek);
        if (request.ticks != 0 || (descriptor.frameCount && request.index > *descriptor.frameCount) ||
            (!descriptor.frameCount && request.index == (std::numeric_limits<std::uint64_t>::max)()))
            return sequenceFailure(ErrorCode::OutOfRange, SequenceFailure::InvalidPosition);
        break;
    case SequenceSeekKind::TimeAtOrAfter:
    case SequenceSeekKind::TimeAtOrBefore:
        if (!descriptor.seekByTime)
            return sequenceFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedSeek);
        if (request.index != 0u || request.ticks < 0 ||
            (descriptor.durationTicks && request.ticks >= *descriptor.durationTicks))
            return sequenceFailure(ErrorCode::OutOfRange, SequenceFailure::InvalidPosition);
        break;
    default:
        return sequenceFailure(ErrorCode::InvalidArgument, SequenceFailure::InvalidPosition);
    }
    return Status::success();
}

Status checkSeekResult(const SequenceDescriptor& descriptor, const SequenceSeekRequest& request,
                       const SequencePosition& position) noexcept
{
    if (position.durationTicks || (descriptor.frameCount && position.index > *descriptor.frameCount) ||
        (!descriptor.frameCount && position.index == (std::numeric_limits<std::uint64_t>::max)()) ||
        (position.presentationTicks &&
         (!descriptor.timeBase || *position.presentationTicks < 0 ||
          (descriptor.durationTicks &&
           (*position.presentationTicks > *descriptor.durationTicks ||
            (*position.presentationTicks == *descriptor.durationTicks && descriptor.frameCount != position.index))))))
        return sequenceFailure(ErrorCode::InvalidData, SequenceFailure::InvalidPosition);
    if (request.kind == SequenceSeekKind::Index)
    {
        if (position.index != request.index)
            return sequenceFailure(ErrorCode::InvalidData, SequenceFailure::InvalidPosition);
    }
    else if (!position.presentationTicks || (descriptor.frameCount && position.index == *descriptor.frameCount) ||
             (request.kind == SequenceSeekKind::TimeAtOrAfter && *position.presentationTicks < request.ticks) ||
             (request.kind == SequenceSeekKind::TimeAtOrBefore && *position.presentationTicks > request.ticks))
        return sequenceFailure(ErrorCode::InvalidData, SequenceFailure::InvalidPosition);
    return Status::success();
}
} // namespace an::sequence_detail
