// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - frames already in memory, offered to the stack executor whole or by region.
//
// The source for frames a caller already holds decoded, published beside the executor; the
// drizzle stack source is the other one this engine ships. It owns nothing but its records: every
// frame is the caller's, borrowed as a band of all its rows, and a region read is a window cut
// from that band without moving a sample. It keeps one window per frame rather than one for the
// last read, because a caller holding its frames resident may be handed several bands at once
// and each must stay valid until that frame is read again.

#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>

#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/stacking/StackHistory.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace an
{
namespace
{

constexpr const char* kCreate = "ResidentStackFrameSource::create";
constexpr const char* kRead = "ResidentStackFrameSource::read";

using stacking::failure;
using stacking::refuse;

/// A refusal about one frame, naming it as the requested count.
///
/// @param code why.
/// @param frame which frame.
/// @param message what is wrong with it.
/// @return the failure.
[[nodiscard]] Status frameRefusal(ErrorCode code, std::uint32_t frame, std::string message)
{
    return failure(code, kCreate, "frame " + std::to_string(frame) + ": " + std::move(message),
                   {StatusContextField::ofText(ContextKey::Parameter, "frames"),
                    StatusContextField::ofCount(ContextKey::Requested, frame)});
}

} // namespace

/// The frame records and the windows.
class ResidentStackFrameSource::State
{
public:
    /// Every frame as a band of all its rows.
    std::vector<StackSourceBand> frames;

    /// One window per frame, holding the last region read of that frame.
    std::vector<StackSourceRows> windows;

    /// How the executor reads.
    StackSourceAccess access = StackSourceAccess::WholeFrame;
};

ResidentStackFrameSource::~ResidentStackFrameSource() = default;

ResidentStackFrameSource::ResidentStackFrameSource(ResidentStackFrameSource&& other) noexcept
    : m_state(std::move(other.m_state))
{
}

ResidentStackFrameSource& ResidentStackFrameSource::operator=(ResidentStackFrameSource&& other) noexcept
{
    if (this != &other)
    {
        m_state = std::move(other.m_state);
    }
    return *this;
}

ResidentStackFrameSource::ResidentStackFrameSource(std::unique_ptr<State> state) noexcept : m_state(std::move(state)) {}

Result<ResidentStackFrameSource> ResidentStackFrameSource::create(std::span<const StackSourceBand> frames,
                                                                  StackSourceAccess access)
{
    if (frames.empty())
    {
        return failure(ErrorCode::EmptySelection, kCreate, "a resident source needs at least one frame",
                       {StatusContextField::ofText(ContextKey::Parameter, "frames")});
    }
    if (!isStackSourceAccess(access))
    {
        return refuse(ErrorCode::InvalidArgument, kCreate, "access", "access is not one this build defines");
    }
    for (std::size_t index = 0u; index < frames.size(); ++index)
    {
        const StackSourceBand& frame = frames[index];
        const auto position = static_cast<std::uint32_t>(index);
        if (frame.samples == nullptr)
        {
            return frameRefusal(ErrorCode::InvalidArgument, position, "samples is null");
        }
        if (frame.layout == nullptr)
        {
            return frameRefusal(ErrorCode::InvalidArgument, position, "layout is null");
        }
        if (frame.frameIndex != position)
        {
            return frameRefusal(ErrorCode::InvalidArgument, position, "frameIndex is not the frame's position");
        }
        if (frame.firstRow != 0u)
        {
            return frameRefusal(ErrorCode::InvalidArgument, position, "firstRow is not zero for a whole frame");
        }
        const bool needsMask = frame.validity.descriptor.rule() == ValidityRule::ExplicitMask;
        if (needsMask != (frame.validity.mask != nullptr))
        {
            return frameRefusal(ErrorCode::InvalidArgument, position,
                                needsMask ? "validity.mask is null under the explicit-mask rule"
                                          : "validity.mask is given under a rule that reads none");
        }
        const StackSourceBand& first = frames[0];
        if (frame.sampleType != first.sampleType || frame.layout->extent() != first.layout->extent())
        {
            return frameRefusal(ErrorCode::IncompatibleArguments, position,
                                "representation or geometry differs from the first frame's");
        }
    }
    std::unique_ptr<State> state = std::make_unique<State>();
    state->frames.assign(frames.begin(), frames.end());
    state->windows.resize(frames.size());
    state->access = access;
    return ResidentStackFrameSource{std::move(state)};
}

std::uint32_t ResidentStackFrameSource::frameCount() const noexcept
{
    return m_state != nullptr ? static_cast<std::uint32_t>(m_state->frames.size()) : 0u;
}

StackSourceAccess ResidentStackFrameSource::access() const noexcept
{
    return m_state != nullptr ? m_state->access : StackSourceAccess::WholeFrame;
}

Result<StackSourceBand> ResidentStackFrameSource::read(const ExecutionContext& execution, std::uint32_t frameIndex,
                                                       std::uint32_t firstRow, std::uint32_t rows,
                                                       std::span<std::byte> buffer)
{
    static_cast<void>(buffer);
    if (m_state == nullptr)
    {
        return failure(ErrorCode::NotInitialized, kRead, "this source holds no frame");
    }
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kRead, "a read needs the operation it is running as");
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (frameIndex >= m_state->frames.size())
    {
        return failure(ErrorCode::OutOfRange, kRead, "frameIndex is outside the source's frames",
                       {StatusContextField::ofText(ContextKey::Parameter, "frameIndex"),
                        StatusContextField::ofCount(ContextKey::Requested, frameIndex),
                        StatusContextField::ofCount(ContextKey::Limit, m_state->frames.size())});
    }
    if (rows == 0u)
    {
        return refuse(ErrorCode::InvalidArgument, kRead, "rows", "rows must be at least one");
    }
    const StackSourceBand& frame = m_state->frames[frameIndex];
    const std::uint32_t height = frame.layout->extent().height();
    if (firstRow == 0u && rows == height)
    {
        return frame;
    }
    if (m_state->access == StackSourceAccess::WholeFrame)
    {
        return refuse(ErrorCode::InvalidArgument, kRead, "rows",
                      "a whole-frame source produces the whole frame, and a window of it was asked for");
    }
    Result<StackSourceRows> window = stackSourceRowsOf(frame, firstRow, rows);
    if (!window.hasValue())
    {
        return window.status();
    }
    m_state->windows[frameIndex] = std::move(*window);
    return m_state->windows[frameIndex].band();
}

} // namespace an
