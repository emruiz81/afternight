// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the drizzled stack source: one nested drizzle per stack read, copied into
// the stack's slot.
//
// Each read wraps the underlying reader so that it always serves the one frame being rendered, runs
// `executeDrizzle` over that frame under the stack's own execution, and copies the result plane by
// plane, writing quiet NaN wherever the result's validity bit is clear; the stack then reads the
// band under FiniteOnly validity, so a destination no source pixel reached is excluded rather than
// taken as a zero. The nested result is destroyed before the read returns, so at most one rendered
// frame is resident beside the stack's own buffers.

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleStackSource.h>

#include <AfterNightEngine/Algorithms/Stacking/StackFrameSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>

#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/drizzle/DrizzleExecutionInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace an
{
namespace
{
using drizzle_detail::executionFailure;

class SelectedFrame final : public DrizzleFrameSource
{
public:
    SelectedFrame(DrizzleFrameSource& source, std::uint32_t frame) : m_source(source), m_frame(frame) {}
    DrizzleSourceAccess access() const noexcept override
    {
        return m_source.access();
    }
    Result<DrizzleFrameData> read(const ExecutionContext& execution, std::uint32_t frameIndex, std::uint32_t firstRow,
                                  std::uint32_t rows, const DrizzleReadBuffers& buffers) override
    {
        static_cast<void>(frameIndex);
        return m_source.read(execution, m_frame, firstRow, rows, buffers);
    }

private:
    DrizzleFrameSource& m_source;
    std::uint32_t m_frame;
};

template <typename T>
Status copyFrame(const ExecutionContext& execution, const DrizzleResult& frame, std::span<std::byte> buffer)
{
    const auto* input = static_cast<const T*>(frame.image.data());
    auto* output = reinterpret_cast<T*>(buffer.data());
    const auto mask = *frame.mask.view();
    const auto& extent = frame.descriptor.extent();
    std::size_t index = 0u;
    for (std::uint32_t channel = 0u; channel < extent.channelCount(); ++channel)
        for (std::uint32_t row = 0u; row < extent.height(); ++row)
        {
            if (const auto stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            const auto bits = *mask.row(channel, row);
            for (std::uint32_t x = 0u; x < extent.width(); ++x, ++index)
            {
                if (x % 4096u == 0u)
                    if (const auto stopped = execution.checkCancellation(); stopped.failed())
                        return stopped;
                output[index] =
                    (bits[x / 8u] & (1u << (x % 8u))) != 0u ? input[index] : std::numeric_limits<T>::quiet_NaN();
            }
        }
    return execution.checkCancellation();
}
} // namespace

Result<DrizzledStackFrameSource> DrizzledStackFrameSource::create(const EngineContext& engine,
                                                                  const DrizzleModel& model, DrizzleFrameSource& source,
                                                                  std::span<const DrizzleFrameParameters> frames,
                                                                  const DrizzleExecutionRequest& request)
{
    if (!engine.valid())
        return executionFailure(ErrorCode::NotInitialized, "drizzled stack source requires an initialized context");
    if (frames.size() != model.request().frameCount)
        return executionFailure(ErrorCode::IncompatibleArguments, "drizzled stack source has mismatched frame records");
    if (request.artifact.runId != 0u || request.artifact.artifactId != 0u)
        return executionFailure(ErrorCode::InvalidArgument, "intermediate drizzled frames must be anonymous artifacts");
    if (model.request().output.combination != DrizzleCombination::WeightedMean)
        return executionFailure(ErrorCode::IncompatibleArguments,
                                "joint drizzle Sum must execute over the complete sequence");
    for (const auto& frame : frames)
        if (frame.geometry.sourceExtent() != model.request().input.extent() ||
            frame.geometry.outputExtent() != model.outputDescriptor().extent() ||
            frame.geometry.scale() != model.request().parameters.scale)
            return executionFailure(ErrorCode::IncompatibleArguments, "drizzled stack geometry differs from its model");
    auto singleRequest = model.request();
    singleRequest.frameCount = 1u;
    singleRequest.output.coverageMap = false;
    singleRequest.output.weightMap = false;
    singleRequest.parameters.collectMaximumWeight = false;
    auto single = DrizzleModel::create(singleRequest, engine.limits());
    if (!single.hasValue())
        return single.status();
    const auto& descriptor = single->outputDescriptor();
    const auto layout = ImageLayout::create(
        {descriptor.extent(), sampleTypeSizeBytes(descriptor.sampleType()), 1u, descriptor.extent().channelCount(), 1u},
        engine.limits());
    if (!layout)
        return executionFailure(ErrorCode::LimitExceeded, "drizzled stack output layout exceeds limits");
    auto nested = request;
    nested.planning.concurrentFrameReads = 1u;
    return DrizzledStackFrameSource{engine, *single, source, frames, nested, *layout};
}

StackSourceAccess DrizzledStackFrameSource::access() const noexcept
{
    return StackSourceAccess::WholeFrame;
}

Result<StackSourceBand> DrizzledStackFrameSource::read(const ExecutionContext& execution, std::uint32_t frameIndex,
                                                       std::uint32_t firstRow, std::uint32_t rows,
                                                       std::span<std::byte> buffer)
{
    if (!execution.valid())
        return executionFailure(ErrorCode::NotInitialized, "a drizzled stack read needs an initialized execution");
    if (m_engine->id() != execution.engineContextId())
        return executionFailure(ErrorCode::IncompatibleArguments,
                                "a drizzled stack read runs as an execution of another engine context");
    if (frameIndex >= m_frames.size() || firstRow != 0u || rows != m_layout.extent().height())
        return executionFailure(ErrorCode::OutOfRange, "drizzled stack reader requires one complete frame");
    if (buffer.data() == nullptr || buffer.size() < m_layout.totalBytes() ||
        reinterpret_cast<std::uintptr_t>(buffer.data()) % m_layout.elementSizeBytes() != 0u)
        return executionFailure(ErrorCode::InvalidArgument, "drizzled stack slot is short or misaligned");
    SelectedFrame selected(*m_source, frameIndex);
    auto frame = executeDrizzle(*m_engine, execution, m_model, selected, m_frames.subspan(frameIndex, 1u), m_request);
    if (!frame.hasValue())
        return frame.status();
    const auto copied = m_model.outputDescriptor().sampleType() == SampleType::Float32
                            ? copyFrame<float>(execution, *frame, buffer)
                            : copyFrame<double>(execution, *frame, buffer);
    if (copied.failed())
        return copied;
    ++m_report.renderedFrames;
    m_report.readRequests += frame->report.readRequests;
    m_report.inputBytesRead += frame->report.inputBytesRead;
    m_report.maximumDrizzlePlanBytes =
        (std::max)(m_report.maximumDrizzlePlanBytes, frame->report.plan.memory.peakBytes);
    return StackSourceBand{.sampleType = m_model.outputDescriptor().sampleType(),
                           .samples = buffer.data(),
                           .layout = &m_layout,
                           .frameIndex = frameIndex,
                           .firstRow = 0u,
                           .validity = {.descriptor = *ValidityDescriptor::create({.rule = ValidityRule::FiniteOnly})}};
}
} // namespace an
