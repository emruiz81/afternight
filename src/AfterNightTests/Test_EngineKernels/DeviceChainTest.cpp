// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Two device operations planned and run as one, against the mock backend.
//
// The property under test is the one whose absence would have to be repaired by a redesign rather
// than an addition: what the first operation writes is what the second reads, on the device, without
// a journey to the host and back. Everything else here exists to keep that honest - the plan says
// what it will transfer before anything is submitted, the device's own counters say what it did, the
// composition is compared with the two scalar references applied in order, and the chain is made to
// fail and to be stopped so that neither leaves work nobody waits for.
//
// The opt-in OpenCL suite runs the same shape of chain on a real device.

#include "MockDeviceBackend.h"

#include <AfterNightEngine/Algorithms/Kernel/Device.h>
#include <AfterNightEngine/Algorithms/Kernel/DeviceChain.h>
#include <AfterNightEngine/Algorithms/Kernel/PointwiseKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Runtime/DeviceError.h>
#include <AfterNightEngine/Runtime/DeviceEvent.h>
#include <AfterNightEngine/Runtime/DeviceResource.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace
{

/// The affine step's coefficients, chosen so that neither is a power of two.
constexpr an::AffineKernelParameters kAffine{.scale = 1.1, .bias = -0.0125};

/// A value no step of the chain writes, so an untouched output is visible.
constexpr float kUntouched = -12345.0f;

[[nodiscard]] an::Stencil3x3KernelParameters stencilParameters()
{
    an::Stencil3x3KernelParameters parameters;
    parameters.coefficients = {0.1, -0.2, 0.3, -0.4, 1.5, -0.6, 0.7, -0.8, 0.9};
    parameters.border = an::KernelBorderMode::Constant;
    parameters.constantBorder = -0.375f;
    return parameters;
}

/// The scalar answer for the whole chain: the affine operation, then the stencil over its result.
[[nodiscard]] std::vector<float> referenceChain(const an::ExecutionContext& execution, std::uint32_t width,
                                                std::uint32_t height, std::span<const float> samples)
{
    std::vector<float> input = an::test::referenceAffine(width, height, samples, kAffine);
    std::vector<float> output(input.size(), kUntouched);
    const std::uint64_t stride = std::uint64_t{width} * sizeof(float);
    const std::optional<an::PlaneView<const float>> source =
        an::PlaneView<const float>::create(input, width, height, 1u, stride);
    const std::optional<an::PlaneView<float>> destination =
        an::PlaneView<float>::create(output, width, height, 1u, stride);
    EXPECT_TRUE(source.has_value() && destination.has_value());
    if (source.has_value() && destination.has_value())
    {
        const an::Result<an::KernelRunResult> ran = an::referenceStencil3x3Kernel(
            execution, *source, *destination, stencilParameters(), an::KernelValidityView{});
        EXPECT_TRUE(ran.hasValue()) << ran.status().message();
    }
    return output;
}

/// The three buffers and two steps every case here plans: an input the host supplies, an intermediate
/// neither side names again, and an output the host reads.
class ChainBench
{
public:
    static constexpr std::uint32_t kWidth = 29u;
    static constexpr std::uint32_t kHeight = 17u;

    explicit ChainBench(const an::test::MockDeviceOptions& options = {}, an::ExecutionOptions executionOptions = {})
        : engine(an::test::makeDeviceTestEngine()),
          execution(an::test::makeDeviceTestExecution(engine, executionOptions)),
          backend(std::make_shared<an::test::MockDeviceBackend>(options)), device(an::test::makeMockDevice(backend)),
          inputLayout(an::test::floatPlaneLayout(kWidth, kHeight, 4u)),
          middleLayout(an::test::floatPlaneLayout(kWidth, kHeight, 64u)),
          outputLayout(an::test::floatPlaneLayout(kWidth, kHeight, 256u)),
          affine(an::test::prepareAffine(device, execution, inputLayout, middleLayout, kAffine)),
          stencil(an::test::prepareStencil3x3(device, execution, middleLayout, outputLayout, stencilParameters())),
          samples(an::test::patternSamples(kWidth, kHeight, 53u))
    {
    }

    /// The buffers of the ordinary chain, with `role` for the middle one so a case can break it.
    [[nodiscard]] std::array<an::DeviceChainBuffer, 3u>
    buffers(an::DeviceChainBufferRole middle = an::DeviceChainBufferRole::Intermediate) const
    {
        return {an::DeviceChainBuffer{.role = an::DeviceChainBufferRole::Input, .layout = inputLayout},
                an::DeviceChainBuffer{.role = middle, .layout = middleLayout},
                an::DeviceChainBuffer{.role = an::DeviceChainBufferRole::Output, .layout = outputLayout}};
    }

    an::EngineContext engine;
    an::ExecutionContext execution;
    std::shared_ptr<an::test::MockDeviceBackend> backend;
    an::Device device;
    an::ImageLayout inputLayout;
    an::ImageLayout middleLayout;
    an::ImageLayout outputLayout;
    an::PreparedDeviceKernel affine;
    an::PreparedDeviceKernel stencil;
    std::vector<float> samples;
};

/// The two steps of the ordinary chain: buffer 0 into buffer 1, then buffer 1 into buffer 2.
constexpr std::array<an::DeviceChainBinding, 2u> kFirstBindings{
    an::DeviceChainBinding{.buffer = 0u, .access = an::DeviceAccess::Read},
    an::DeviceChainBinding{.buffer = 1u, .access = an::DeviceAccess::Write}};
constexpr std::array<an::DeviceChainBinding, 2u> kSecondBindings{
    an::DeviceChainBinding{.buffer = 1u, .access = an::DeviceAccess::Read},
    an::DeviceChainBinding{.buffer = 2u, .access = an::DeviceAccess::Write}};

[[nodiscard]] std::array<an::DeviceChainStep, 2u> steps(const ChainBench& bench)
{
    return {an::DeviceChainStep{.kernel = &bench.affine, .bindings = kFirstBindings},
            an::DeviceChainStep{.kernel = &bench.stencil, .bindings = kSecondBindings}};
}

[[nodiscard]] bool refusedFor(const an::Status& status, an::ErrorCode code, an::DeviceFailure reason)
{
    EXPECT_EQ(status.code(), code) << status.message();
    const std::optional<an::DeviceFailure> carried = an::deviceFailureReason(status);
    EXPECT_TRUE(carried.has_value()) << status.message();
    EXPECT_EQ(carried.value_or(an::DeviceFailure::BackendFailure), reason) << status.message();
    return status.code() == code && carried.has_value() && *carried == reason;
}

TEST(DeviceChainPlanTest, TheIntermediateStaysOnTheDeviceAndTheSavingIsCountedBeforeAnythingRuns)
{
    const ChainBench bench;
    const std::array buffers = bench.buffers();
    const std::array chainSteps = steps(bench);
    const an::Result<an::DeviceChain> chain =
        an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = chainSteps});
    ASSERT_TRUE(chain.hasValue()) << chain.status().message();
    ASSERT_NE(chain->plan(), nullptr);
    const an::DeviceChainPlan& plan = *chain->plan();
    ASSERT_EQ(plan.buffers.size(), 3u);

    EXPECT_EQ(plan.buffers[0].role, an::DeviceChainBufferRole::Input);
    EXPECT_EQ(plan.buffers[0].residency, an::ResourceResidency::Device);
    EXPECT_TRUE(plan.buffers[0].uploaded);
    EXPECT_FALSE(plan.buffers[0].downloaded);
    EXPECT_FALSE(plan.buffers[0].resident);
    EXPECT_EQ(plan.buffers[0].firstStep, 0u);
    EXPECT_EQ(plan.buffers[0].lastStep, 0u);

    EXPECT_EQ(plan.buffers[1].role, an::DeviceChainBufferRole::Intermediate);
    EXPECT_EQ(plan.buffers[1].residency, an::ResourceResidency::Device);
    EXPECT_FALSE(plan.buffers[1].uploaded);
    EXPECT_FALSE(plan.buffers[1].downloaded);
    EXPECT_TRUE(plan.buffers[1].resident);
    EXPECT_EQ(plan.buffers[1].firstStep, 0u);
    EXPECT_EQ(plan.buffers[1].lastStep, 1u);
    EXPECT_EQ(plan.buffers[1].bytes, bench.middleLayout.totalBytes());

    EXPECT_EQ(plan.buffers[2].role, an::DeviceChainBufferRole::Output);
    EXPECT_TRUE(plan.buffers[2].downloaded);
    EXPECT_FALSE(plan.buffers[2].resident);
    EXPECT_EQ(plan.buffers[2].firstStep, 1u);

    const an::DeviceChainAccounting& accounting = plan.accounting;
    EXPECT_EQ(accounting.submissions, 4u);
    EXPECT_EQ(accounting.uploads, 1u);
    EXPECT_EQ(accounting.uploadBytes, bench.inputLayout.totalBytes());
    EXPECT_EQ(accounting.downloads, 1u);
    EXPECT_EQ(accounting.downloadBytes, bench.outputLayout.totalBytes());
    EXPECT_EQ(accounting.residentHandovers, 1u);
    EXPECT_EQ(accounting.residentBytes, bench.middleLayout.totalBytes());
    EXPECT_EQ(accounting.avoidedTransfers, 2u);
    EXPECT_EQ(accounting.avoidedBytes, 2u * bench.middleLayout.totalBytes());
    EXPECT_EQ(accounting.deviceBytes,
              bench.inputLayout.totalBytes() + bench.middleLayout.totalBytes() + bench.outputLayout.totalBytes());
    EXPECT_EQ(accounting.hostBytes, bench.inputLayout.totalBytes() + bench.outputLayout.totalBytes());

    // The host has a door to what it supplies and to what it asked for, and to nothing else.
    EXPECT_TRUE(chain->hostBuffer(0u).valid());
    EXPECT_FALSE(chain->hostBuffer(1u).valid());
    EXPECT_TRUE(chain->hostBuffer(2u).valid());
    EXPECT_TRUE(chain->buffer(1u).valid());
    EXPECT_FALSE(chain->buffer(3u).valid());
    EXPECT_EQ(chain->deviceId(), bench.device.id());
    ASSERT_NE(chain->hostBuffer(0u).descriptor(), nullptr);
    EXPECT_EQ(chain->hostBuffer(0u).descriptor()->residency, an::ResourceResidency::Pinned);
}

class DeviceChainRunTest : public ::testing::TestWithParam<bool>
{
};

TEST_P(DeviceChainRunTest, TheChainTransfersOnceEachWayAndProducesTheCompositionOfTheTwoReferences)
{
    an::test::MockDeviceOptions options;
    options.asynchronous = GetParam();
    const ChainBench bench(options);
    const std::array buffers = bench.buffers();
    const std::array chainSteps = steps(bench);
    const an::Result<an::DeviceChain> chain =
        an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = chainSteps});
    ASSERT_TRUE(chain.hasValue()) << chain.status().message();

    an::test::writeFloatPlane(chain->hostBuffer(0u), bench.inputLayout, bench.samples);
    const an::Result<an::DeviceChainRun> run = chain->submit(bench.device, bench.execution);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    EXPECT_TRUE(run->submission.ok()) << run->submission.message();
    ASSERT_EQ(run->events.size(), 4u);

    const an::DeviceChainReport report = an::awaitDeviceChain(*run, bench.execution);
    ASSERT_TRUE(report.outcome.ok()) << report.outcome.message();
    EXPECT_EQ(report.failedWork, an::kDeviceChainNoWork);
    EXPECT_EQ(report.succeededWork, 4u);
    EXPECT_EQ(report.transfersPerformed, 2u);
    EXPECT_EQ(report.kernelsRun, 2u);

    EXPECT_TRUE(
        an::test::sameBits(an::test::readFloatPlane(chain->hostBuffer(2u), bench.outputLayout),
                           referenceChain(bench.execution, ChainBench::kWidth, ChainBench::kHeight, bench.samples)));

    const an::DeviceCounters counters = bench.device.counters();
    EXPECT_EQ(counters.uploads.transfers, 1u);
    EXPECT_EQ(counters.uploads.bytes, bench.inputLayout.totalBytes());
    EXPECT_EQ(counters.downloads.transfers, 1u);
    EXPECT_EQ(counters.downloads.bytes, bench.outputLayout.totalBytes());
    // The intermediate never crossed: a chain that round-tripped it would show two of each.
    EXPECT_EQ(counters.deviceCopies.transfers, 0u);
    EXPECT_EQ(counters.hostCopies.transfers, 0u);
    EXPECT_EQ(counters.kernelRuns, 2u);
    EXPECT_EQ(counters.succeededWork, 4u);
    EXPECT_EQ(counters.pendingWork, 0u);
    EXPECT_EQ(counters.backendDefects, 0u);
}

INSTANTIATE_TEST_SUITE_P(MockCompletion, DeviceChainRunTest, ::testing::Values(true, false),
                         [](const ::testing::TestParamInfo<bool>& info) {
                             return std::string(info.param ? "Asynchronous" : "Synchronous");
                         });

TEST(DeviceChainRunTest, RunningTheSameChainAgainUploadsTheNewInputAndKeepsTheIntermediateWhereItWas)
{
    const ChainBench bench;
    const std::array buffers = bench.buffers();
    const std::array chainSteps = steps(bench);
    const an::Result<an::DeviceChain> chain =
        an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = chainSteps});
    ASSERT_TRUE(chain.hasValue()) << chain.status().message();

    for (std::uint32_t seed : {53u, 91u})
    {
        const std::vector<float> samples = an::test::patternSamples(ChainBench::kWidth, ChainBench::kHeight, seed);
        an::test::writeFloatPlane(chain->hostBuffer(0u), bench.inputLayout, samples);
        const an::Result<an::DeviceChainRun> run = chain->submit(bench.device, bench.execution);
        ASSERT_TRUE(run.hasValue()) << run.status().message();
        const an::DeviceChainReport report = an::awaitDeviceChain(*run, bench.execution);
        ASSERT_TRUE(report.outcome.ok()) << report.outcome.message();
        EXPECT_TRUE(
            an::test::sameBits(an::test::readFloatPlane(chain->hostBuffer(2u), bench.outputLayout),
                               referenceChain(bench.execution, ChainBench::kWidth, ChainBench::kHeight, samples)));
    }

    const an::DeviceCounters counters = bench.device.counters();
    EXPECT_EQ(counters.uploads.transfers, 2u);
    EXPECT_EQ(counters.downloads.transfers, 2u);
    EXPECT_EQ(counters.kernelRuns, 4u);
    EXPECT_EQ(counters.succeededWork, 8u);
}

TEST(DeviceChainPlanTest, ADeviceWhoseMemoryTheHostSharesTransfersNothingAtAll)
{
    an::test::MockDeviceOptions options;
    options.offerDevice = false;
    const ChainBench bench(options);
    const std::array buffers = bench.buffers();
    const std::array chainSteps = steps(bench);
    const an::Result<an::DeviceChain> chain =
        an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = chainSteps});
    ASSERT_TRUE(chain.hasValue()) << chain.status().message();
    ASSERT_NE(chain->plan(), nullptr);
    const an::DeviceChainPlan& plan = *chain->plan();

    for (const an::DeviceChainBufferPlan& buffer : plan.buffers)
    {
        EXPECT_EQ(buffer.residency, an::ResourceResidency::Shared);
        EXPECT_FALSE(buffer.uploaded);
        EXPECT_FALSE(buffer.downloaded);
    }
    EXPECT_TRUE(plan.buffers[1].resident);
    EXPECT_EQ(plan.accounting.submissions, 2u);
    EXPECT_EQ(plan.accounting.uploads, 0u);
    EXPECT_EQ(plan.accounting.downloads, 0u);
    EXPECT_EQ(plan.accounting.residentHandovers, 1u);
    EXPECT_EQ(plan.accounting.avoidedTransfers, 0u);
    EXPECT_EQ(plan.accounting.avoidedBytes, 0u);
    EXPECT_EQ(plan.accounting.hostBytes, 0u);
    // The host reaches a shared input directly; there is nothing to stage it through.
    EXPECT_EQ(chain->hostBuffer(0u).id(), chain->buffer(0u).id());
    EXPECT_FALSE(chain->hostBuffer(1u).valid());

    an::test::writeFloatPlane(chain->hostBuffer(0u), bench.inputLayout, bench.samples);
    const an::Result<an::DeviceChainRun> run = chain->submit(bench.device, bench.execution);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    ASSERT_EQ(run->events.size(), 2u);
    const an::DeviceChainReport report = an::awaitDeviceChain(*run, bench.execution);
    ASSERT_TRUE(report.outcome.ok()) << report.outcome.message();
    EXPECT_EQ(report.transfersPerformed, 0u);
    EXPECT_EQ(report.kernelsRun, 2u);
    EXPECT_TRUE(
        an::test::sameBits(an::test::readFloatPlane(chain->hostBuffer(2u), bench.outputLayout),
                           referenceChain(bench.execution, ChainBench::kWidth, ChainBench::kHeight, bench.samples)));

    const an::DeviceCounters counters = bench.device.counters();
    EXPECT_EQ(counters.uploads.transfers, 0u);
    EXPECT_EQ(counters.downloads.transfers, 0u);
    EXPECT_EQ(counters.kernelRuns, 2u);
    EXPECT_EQ(counters.backendDefects, 0u);
}

TEST(DeviceChainFailureTest, AStepThatFailsNamesItsOwnWorkAndLeavesTheOutputUntouched)
{
    an::test::MockDeviceOptions options;
    // Work zero is the uploads; work one is the first step.
    options.failWorkNumber = 1u;
    const ChainBench bench(options);
    const std::array buffers = bench.buffers();
    const std::array chainSteps = steps(bench);
    const an::Result<an::DeviceChain> chain =
        an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = chainSteps});
    ASSERT_TRUE(chain.hasValue()) << chain.status().message();

    an::test::writeFloatPlane(chain->hostBuffer(0u), bench.inputLayout, bench.samples);
    const std::vector<float> before(static_cast<std::size_t>(ChainBench::kWidth) * ChainBench::kHeight, kUntouched);
    an::test::writeFloatPlane(chain->hostBuffer(2u), bench.outputLayout, before);

    const an::Result<an::DeviceChainRun> run = chain->submit(bench.device, bench.execution);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    ASSERT_EQ(run->events.size(), 4u);

    const an::DeviceChainReport report = an::awaitDeviceChain(*run, bench.execution);
    EXPECT_EQ(report.outcome.code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(report.failedWork, 1u);
    EXPECT_EQ(report.succeededWork, 1u);
    EXPECT_EQ(report.kernelsRun, 0u);

    // Everything after the failing step reports it as its cause rather than rediscovering it.
    ASSERT_TRUE(run->events[3].outcome().has_value());
    EXPECT_TRUE(an::deviceFailureReason(*run->events[3].outcome()) == an::DeviceFailure::DependencyFailed);
    EXPECT_TRUE(an::test::sameBits(an::test::readFloatPlane(chain->hostBuffer(2u), bench.outputLayout), before));

    const an::DeviceCounters counters = bench.device.counters();
    EXPECT_EQ(counters.pendingWork, 0u);
    EXPECT_EQ(counters.succeededWork, 1u);
    EXPECT_EQ(counters.failedWork, 3u);
    EXPECT_EQ(counters.backendDefects, 0u);
}

TEST(DeviceChainFailureTest, StoppingTheOperationEndsEveryPieceOfTheChainAndProducesNoOutput)
{
    an::test::MockDeviceOptions options;
    options.startPaused = true;
    std::stop_source stop;
    an::ExecutionOptions stoppable;
    stoppable.stopToken = stop.get_token();
    const ChainBench bench(options, stoppable);
    const std::array buffers = bench.buffers();
    const std::array chainSteps = steps(bench);
    const an::Result<an::DeviceChain> chain =
        an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = chainSteps});
    ASSERT_TRUE(chain.hasValue()) << chain.status().message();

    an::test::writeFloatPlane(chain->hostBuffer(0u), bench.inputLayout, bench.samples);
    const std::vector<float> before(static_cast<std::size_t>(ChainBench::kWidth) * ChainBench::kHeight, kUntouched);
    an::test::writeFloatPlane(chain->hostBuffer(2u), bench.outputLayout, before);

    const an::Result<an::DeviceChainRun> run = chain->submit(bench.device, bench.execution);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    ASSERT_EQ(run->events.size(), 4u);

    stop.request_stop();
    bench.backend->resume();

    // Waiting is not cancelling, so the report is taken under an operation of its own.
    const an::ExecutionContext waiter = an::test::makeDeviceTestExecution(bench.engine);
    const an::DeviceChainReport report = an::awaitDeviceChain(*run, waiter);
    EXPECT_EQ(report.outcome.code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(report.failedWork, 0u);
    EXPECT_EQ(report.succeededWork, 0u);
    EXPECT_EQ(report.kernelsRun, 0u);
    EXPECT_TRUE(an::test::sameBits(an::test::readFloatPlane(chain->hostBuffer(2u), bench.outputLayout), before));

    const an::DeviceCounters counters = bench.device.counters();
    EXPECT_EQ(counters.pendingWork, 0u);
    EXPECT_EQ(counters.cancelledWork, 4u);
    EXPECT_EQ(counters.kernelRuns, 0u);
    EXPECT_EQ(counters.backendDefects, 0u);
}

TEST(DeviceChainFailureTest, AnOperationAlreadyStoppedSubmitsNothingAtAll)
{
    const ChainBench bench;
    const std::array buffers = bench.buffers();
    const std::array chainSteps = steps(bench);
    const an::Result<an::DeviceChain> chain =
        an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = chainSteps});
    ASSERT_TRUE(chain.hasValue()) << chain.status().message();

    std::stop_source stop;
    stop.request_stop();
    an::ExecutionOptions stopped;
    stopped.stopToken = stop.get_token();
    const an::ExecutionContext halted = an::test::makeDeviceTestExecution(bench.engine, stopped);
    const an::Result<an::DeviceChainRun> run = chain->submit(bench.device, halted);
    ASSERT_FALSE(run.hasValue());
    EXPECT_EQ(run.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(bench.device.counters().acceptedWork, 0u);
}

TEST(DeviceChainRefusalTest, AChainWithNoStepOrNoBufferHasNothingToDo)
{
    const ChainBench bench;
    const std::array buffers = bench.buffers();
    const std::array chainSteps = steps(bench);
    EXPECT_TRUE(refusedFor(an::DeviceChain::create(bench.device, {.buffers = buffers}).status(),
                           an::ErrorCode::InvalidArgument, an::DeviceFailure::EmptySubmission));
    EXPECT_TRUE(refusedFor(an::DeviceChain::create(bench.device, {.steps = chainSteps}).status(),
                           an::ErrorCode::InvalidArgument, an::DeviceFailure::EmptySubmission));
}

TEST(DeviceChainRefusalTest, ABindingThatNamesNoBufferOfTheChainIsOutOfRange)
{
    const ChainBench bench;
    const std::array buffers = bench.buffers();
    constexpr std::array beyond{an::DeviceChainBinding{.buffer = 0u, .access = an::DeviceAccess::Read},
                                an::DeviceChainBinding{.buffer = 7u, .access = an::DeviceAccess::Write}};
    const std::array chainSteps{an::DeviceChainStep{.kernel = &bench.affine, .bindings = beyond}};
    EXPECT_TRUE(refusedFor(an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = chainSteps}).status(),
                           an::ErrorCode::OutOfRange, an::DeviceFailure::BindingMismatch));
}

TEST(DeviceChainRefusalTest, AStepThatBindsOneBufferTwiceIsAliased)
{
    const ChainBench bench;
    const std::array buffers{
        an::DeviceChainBuffer{.role = an::DeviceChainBufferRole::Input, .layout = bench.inputLayout}};
    constexpr std::array twice{an::DeviceChainBinding{.buffer = 0u, .access = an::DeviceAccess::Read},
                               an::DeviceChainBinding{.buffer = 0u, .access = an::DeviceAccess::Write}};
    const std::array chainSteps{an::DeviceChainStep{.kernel = &bench.affine, .bindings = twice}};
    EXPECT_TRUE(refusedFor(an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = chainSteps}).status(),
                           an::ErrorCode::Aliasing, an::DeviceFailure::BindingAliased));
}

TEST(DeviceChainRefusalTest, ABindingWhoseAccessOrLayoutDisagreesWithItsKernelIsRefused)
{
    const ChainBench bench;
    const std::array buffers = bench.buffers();
    constexpr std::array reversed{an::DeviceChainBinding{.buffer = 0u, .access = an::DeviceAccess::Write},
                                  an::DeviceChainBinding{.buffer = 1u, .access = an::DeviceAccess::Read}};
    const std::array reversedSteps{an::DeviceChainStep{.kernel = &bench.affine, .bindings = reversed}};
    EXPECT_TRUE(refusedFor(an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = reversedSteps}).status(),
                           an::ErrorCode::InvalidArgument, an::DeviceFailure::BindingMismatch));

    constexpr std::array shortened{an::DeviceChainBinding{.buffer = 0u, .access = an::DeviceAccess::Read}};
    const std::array shortSteps{an::DeviceChainStep{.kernel = &bench.affine, .bindings = shortened}};
    EXPECT_TRUE(refusedFor(an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = shortSteps}).status(),
                           an::ErrorCode::InvalidArgument, an::DeviceFailure::BindingMismatch));

    // The kernel states the layout it writes, so a buffer padded differently is not that buffer.
    const std::array mismatched{
        an::DeviceChainBuffer{.role = an::DeviceChainBufferRole::Input, .layout = bench.inputLayout},
        an::DeviceChainBuffer{.role = an::DeviceChainBufferRole::Output, .layout = bench.outputLayout}};
    const std::array firstOnly{an::DeviceChainStep{.kernel = &bench.affine, .bindings = kFirstBindings}};
    EXPECT_TRUE(refusedFor(an::DeviceChain::create(bench.device, {.buffers = mismatched, .steps = firstOnly}).status(),
                           an::ErrorCode::InvalidArgument, an::DeviceFailure::BindingMismatch));
}

TEST(DeviceChainRefusalTest, AKernelOfAnotherDeviceBelongsToAnotherChain)
{
    const ChainBench bench;
    const ChainBench other;
    const std::array buffers = bench.buffers();
    const std::array chainSteps{an::DeviceChainStep{.kernel = &other.affine, .bindings = kFirstBindings},
                                an::DeviceChainStep{.kernel = &bench.stencil, .bindings = kSecondBindings}};
    EXPECT_TRUE(refusedFor(an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = chainSteps}).status(),
                           an::ErrorCode::IncompatibleArguments, an::DeviceFailure::ForeignDevice));
}

TEST(DeviceChainRefusalTest, AStepThatNamesNoKernelIsRefused)
{
    const ChainBench bench;
    const std::array buffers = bench.buffers();
    const an::PreparedDeviceKernel unprepared;
    const std::array chainSteps{an::DeviceChainStep{.kernel = &unprepared, .bindings = kFirstBindings},
                                an::DeviceChainStep{.kernel = &bench.stencil, .bindings = kSecondBindings}};
    EXPECT_TRUE(refusedFor(an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = chainSteps}).status(),
                           an::ErrorCode::InvalidArgument, an::DeviceFailure::InvalidKernel));
}

TEST(DeviceChainRefusalTest, ReadingAnIntermediateNothingHasWrittenIsRefusedBeforeItIsAllocated)
{
    const ChainBench bench;
    const std::array buffers = bench.buffers();
    const std::uint64_t allocationsBefore = bench.backend->allocationCalls();
    // The steps in the other order: the stencil reads the intermediate before the affine writes it.
    const std::array chainSteps{an::DeviceChainStep{.kernel = &bench.stencil, .bindings = kSecondBindings},
                                an::DeviceChainStep{.kernel = &bench.affine, .bindings = kFirstBindings}};
    EXPECT_TRUE(refusedFor(an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = chainSteps}).status(),
                           an::ErrorCode::InvalidState, an::DeviceFailure::UnproducedBuffer));
    EXPECT_EQ(bench.backend->allocationCalls(), allocationsBefore);
}

TEST(DeviceChainRefusalTest, AnOutputTheChainWouldHaveToProduceFromNothingIsRefused)
{
    const ChainBench bench;
    // The first buffer is asked for as an output, so the host does not supply it - and no step writes
    // it before the affine step reads it.
    const std::array buffers{
        an::DeviceChainBuffer{.role = an::DeviceChainBufferRole::Output, .layout = bench.inputLayout},
        an::DeviceChainBuffer{.role = an::DeviceChainBufferRole::Output, .layout = bench.middleLayout}};
    const std::array chainSteps{an::DeviceChainStep{.kernel = &bench.affine, .bindings = kFirstBindings}};
    EXPECT_TRUE(refusedFor(an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = chainSteps}).status(),
                           an::ErrorCode::InvalidState, an::DeviceFailure::UnproducedBuffer));
}

TEST(DeviceChainRefusalTest, ABufferNoStepBindsIsMemoryTheChainWouldHoldForNothing)
{
    const ChainBench bench;
    const std::array buffers{
        an::DeviceChainBuffer{.role = an::DeviceChainBufferRole::Input, .layout = bench.inputLayout},
        an::DeviceChainBuffer{.role = an::DeviceChainBufferRole::Output, .layout = bench.middleLayout},
        an::DeviceChainBuffer{.role = an::DeviceChainBufferRole::Intermediate, .layout = bench.outputLayout}};
    const std::array chainSteps{an::DeviceChainStep{.kernel = &bench.affine, .bindings = kFirstBindings}};
    EXPECT_TRUE(refusedFor(an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = chainSteps}).status(),
                           an::ErrorCode::InvalidState, an::DeviceFailure::UnusedBuffer));
}

TEST(DeviceChainRefusalTest, AChainRunOnAnotherDeviceIsRefusedWithNothingSubmitted)
{
    const ChainBench bench;
    const ChainBench other;
    const std::array buffers = bench.buffers();
    const std::array chainSteps = steps(bench);
    const an::Result<an::DeviceChain> chain =
        an::DeviceChain::create(bench.device, {.buffers = buffers, .steps = chainSteps});
    ASSERT_TRUE(chain.hasValue()) << chain.status().message();
    const an::Result<an::DeviceChainRun> run = chain->submit(other.device, other.execution);
    ASSERT_FALSE(run.hasValue());
    EXPECT_TRUE(refusedFor(run.status(), an::ErrorCode::IncompatibleArguments, an::DeviceFailure::ForeignDevice));
    EXPECT_EQ(other.device.counters().acceptedWork, 0u);
}

TEST(DeviceChainRefusalTest, AChainThatHoldsNothingAnswersWithoutReachingADevice)
{
    const an::DeviceChain empty;
    EXPECT_FALSE(empty.valid());
    EXPECT_EQ(empty.deviceId(), 0u);
    EXPECT_EQ(empty.plan(), nullptr);
    EXPECT_FALSE(empty.buffer(0u).valid());
    EXPECT_FALSE(empty.hostBuffer(0u).valid());
    const ChainBench bench;
    const an::Result<an::DeviceChainRun> run = empty.submit(bench.device, bench.execution);
    ASSERT_FALSE(run.hasValue());
    EXPECT_EQ(run.status().code(), an::ErrorCode::NotInitialized);
}

TEST(DeviceChainRefusalTest, EveryBufferRoleHasAName)
{
    EXPECT_STREQ(an::deviceChainBufferRoleName(an::DeviceChainBufferRole::Input), "Input");
    EXPECT_STREQ(an::deviceChainBufferRoleName(an::DeviceChainBufferRole::Intermediate), "Intermediate");
    EXPECT_STREQ(an::deviceChainBufferRoleName(an::DeviceChainBufferRole::Output), "Output");
    EXPECT_STREQ(an::deviceChainBufferRoleName(static_cast<an::DeviceChainBufferRole>(an::kDeviceChainBufferRoleCount)),
                 "UnknownDeviceChainBufferRole");
}

} // namespace
