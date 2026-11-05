// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The stacking scenarios: what the streaming stack executor does when an allocation is refused at
// each of the points it has, and when a stop arrives between each pair of the stages it reports.
//
// This is the subject with several passes over a working set, a plan that states its own memory, and
// progress that names its stages - so it is the one where a refused allocation has somewhere to go
// wrong other than the call that made it, and where cancellation has interior boundaries rather than
// one. It is also the only scenario in this campaign whose points come from progress reports rather
// than from allocations, because what a stack cancellation has to be tried between is its passes.

#include "FaultHarness.h"

#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>
#include <AfterNightEngine/Algorithms/Stacking/StackModelError.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace
{
using an::faults::FaultKind;
using an::faults::PointSource;
using an::faults::RunContext;
using an::faults::Sweep;

/// How wide the frames a stack scenario stacks are.
constexpr std::uint32_t kStackWidth = 12u;

/// How tall they are.
constexpr std::uint32_t kStackHeight = 8u;

/// How many of them there are. Five rather than three so that a rejection has something to reject.
constexpr std::uint32_t kStackFrames = 5u;

/// One frame held in this scenario's own memory rather than the engine's.
///
/// Host memory on purpose: the sweep refuses allocations the *engine* makes, so a fixture that took
/// its frames from the engine would spend its first injection points on the fixture instead of on
/// the operation under test.
struct HostFrame
{
    /// The samples, as many floats as the layout has bytes divided by four.
    std::vector<float> samples;

    /// Where they are.
    std::optional<an::ImageLayout> layout;

    /// Which frame of the stack this is.
    std::uint32_t index = 0u;

    /// This frame as the source takes it.
    ///
    /// @return the band.
    [[nodiscard]] an::StackSourceBand band() const noexcept
    {
        return an::StackSourceBand{.sampleType = an::SampleType::Float32,
                                   .samples = reinterpret_cast<const std::byte*>(samples.data()),
                                   .layout = layout.has_value() ? &*layout : nullptr,
                                   .frameIndex = index,
                                   .firstRow = 0u,
                                   .validity =
                                       an::StackBandValidity{.descriptor = an::ValidityDescriptor{}, .mask = nullptr}};
    }
};

/// The description every frame of these scenarios carries.
///
/// @return a single-channel normalized-linear Float32 description.
[[nodiscard]] an::ImageDescriptor stackDescriptor()
{
    const an::ImageDescriptorRequest request{
        .extent = *an::ImageExtent::create(kStackWidth, kStackHeight, 1u, an::defaultEngineLimits()),
        .sampleType = an::SampleType::Float32,
        .channelSchema = an::ChannelSchema::Mono,
        .colorSpace = an::ColorSpace::Unspecified,
        .numericDomain = an::NumericDomain::NormalizedLinear,
        .cfa = {},
        .coordinateOrigin = an::CoordinateOrigin::TopLeft,
        .pixelAspectRatio = 1.0};
    return *an::ImageDescriptor::create(request);
}

/// The frames, filled so that no two samples of one pixel agree and one of them is an outlier.
///
/// @return the frames, owned by the caller.
[[nodiscard]] std::vector<HostFrame> stackFrames()
{
    const an::ImageLayoutRequest request{.extent = stackDescriptor().extent(),
                                         .elementSizeBytes = sizeof(float),
                                         .samplesPerPixel = 1u,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
    const an::ImageLayout layout = *an::ImageLayout::create(request, an::defaultEngineLimits());

    std::vector<HostFrame> frames(kStackFrames);
    for (std::uint32_t f = 0u; f < kStackFrames; ++f)
    {
        frames[f].layout = layout;
        frames[f].index = f;
        frames[f].samples.assign(static_cast<std::size_t>(layout.totalBytes() / sizeof(float)), 0.0f);
        for (std::size_t i = 0u; i < frames[f].samples.size(); ++i)
        {
            const double base = 0.2 + 0.6 * static_cast<double>((i * 37u + f * 11u) % 97u) / 97.0;
            // One frame of every twenty pixels is far from the others, so a rejection pass has
            // something to decide rather than agreeing with the mean everywhere.
            const double outlier = (i % 20u == f) ? 0.95 : 0.0;
            frames[f].samples[i] = static_cast<float>(outlier > 0.0 ? outlier : base);
        }
    }
    return frames;
}

/// The request these scenarios stack: an average with a sigma clipping, which gives the plan a
/// measurement pass, a rejection pass and a combination pass rather than one of each.
///
/// @return the request.
[[nodiscard]] an::StackRequest stackRequest()
{
    an::StackRequest request{
        .input = an::StackInputDescription{.frameCount = kStackFrames, .descriptor = stackDescriptor()}};
    request.combination = an::StackCombination::Average;
    request.rejection.method = an::StackRejection::SigmaClipping;
    return request;
}

/// Runs one stack, and destroys everything it built.
///
/// @param run what this run was given.
/// @param frames the frames to stack, owned by the caller.
/// @return the executor's own answer.
an::Status runStack(RunContext& run, const std::vector<HostFrame>& frames)
{
    an::ExecutionOptions policy;
    policy.stopToken = run.stop.get_token();
    policy.progressSink = &run.progress;
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(run.engine, policy);
    if (!execution.hasValue())
    {
        return an::faults::settle(execution);
    }

    const std::optional<an::StackModel> model = an::StackModel::create(stackRequest());
    if (!model.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument,
                                   an::stackModelErrorName(an::StackModel::check(stackRequest())));
    }

    std::vector<an::StackSourceBand> bands;
    bands.reserve(frames.size());
    for (const HostFrame& frame : frames)
    {
        bands.push_back(frame.band());
    }
    an::Result<an::ResidentStackFrameSource> source = an::ResidentStackFrameSource::create(bands);
    if (!source.hasValue())
    {
        return an::faults::settle(source);
    }

    an::Result<an::StackResult> stacked = an::executeStack(run.engine, *execution, *model, *source, {}, {}, {});
    return an::faults::settle(stacked);
}

TEST(FaultCampaign, AnAllocationIsRefusedAtEveryPointOfAStack)
{
    const std::vector<HostFrame> frames = stackFrames();

    Sweep sweep("alloc.stack", FaultKind::Allocation);
    sweep.engineOptions({.maxConcurrency = 2u});
    sweep.accept(an::ErrorCode::AllocationFailed);
    sweep.accept(an::ErrorCode::MemoryBudgetExceeded);
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([&frames](RunContext& run) { return runStack(run, frames); });
}

TEST(FaultCampaign, AStopIsRequestedAtEveryStageAStackReports)
{
    const std::vector<HostFrame> frames = stackFrames();

    Sweep sweep("cancel.stack", FaultKind::Cancellation);
    sweep.engineOptions({.maxConcurrency = 2u});
    sweep.pointsFrom(PointSource::ProgressReports);
    sweep.accept(an::ErrorCode::Cancelled);
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([&frames](RunContext& run) { return runStack(run, frames); });
}

} // namespace
