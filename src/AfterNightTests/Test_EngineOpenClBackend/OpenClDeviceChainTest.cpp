// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Residency on real hardware: a fused pointwise program and the direct stencil, planned as one chain,
// with what the first writes staying in the GPU's own memory for the second to read.
//
// The two operations are the ones the earlier proofs established, so nothing here is asking whether a
// kernel is right - that is settled next door. What is being asked is whether the plan can state the
// hand-over and the device can carry it: one upload, two kernels, one download, and an intermediate the
// host is given no way to reach at all.

#include "OpenClTestSupport.h"

#include <AfterNightEngine/Algorithms/Kernel/Device.h>
#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/DeviceChain.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramKernel.h>
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
#include <iostream>
#include <optional>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{

constexpr std::uint32_t kWidth = 61u;
constexpr std::uint32_t kHeight = 37u;

/// A value no step of the chain writes, so an untouched sample is visible.
constexpr float kUntouched = -12345.0f;

/// The first step: a brightness change and a clamp, with no division and no power, so the fused GPU
/// kernel reproduces the scalar evaluator exactly.
[[nodiscard]] an::PointwiseProgram brightnessProgram()
{
    an::PointwiseProgramBuilder builder;
    builder.channels(1u)
        .sources(1u)
        .append(an::pointwiseScale(1.1))
        .append(an::pointwiseBias(-0.0125))
        .append(an::pointwiseClamp(0.0, 1.0));
    an::Result<an::PointwiseProgram> program = builder.build();
    EXPECT_TRUE(program.hasValue()) << program.status().message();
    return std::move(*program);
}

[[nodiscard]] an::Stencil3x3KernelParameters stencilParameters()
{
    an::Stencil3x3KernelParameters parameters;
    parameters.coefficients = {0.1, -0.2, 0.3, -0.4, 1.5, -0.6, 0.7, -0.8, 0.9};
    parameters.border = an::KernelBorderMode::Clamp;
    return parameters;
}

/// The scalar answer for the whole chain: the pointwise executor on its reference backend, then the
/// stencil oracle over what it produced.
[[nodiscard]] std::vector<float> referenceChain(const an::EngineContext& engine, const an::PointwiseProgram& program,
                                                std::span<const float> samples)
{
    an::ExecutionOptions scalar;
    scalar.backend = an::ComputeBackend::Reference;
    scalar.backendFallback = an::BackendFallback::Forbidden;
    const an::ExecutionContext execution = an::test::makeExecution(engine, scalar);
    const std::uint64_t stride = std::uint64_t{kWidth} * sizeof(float);
    const an::ImageLayout tight = an::test::floatPlaneLayout(kWidth, kHeight);

    std::vector<float> source(samples.begin(), samples.end());
    std::vector<float> middle(source.size(), kUntouched);
    const an::PointwiseImageView view{.samples = source.data(), .layout = &tight};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        execution, program, std::span<const an::PointwiseImageView, 1u>(&view, 1u),
        an::PointwiseMutableImageView{.samples = middle.data(), .layout = &tight}, an::KernelValidityView{});
    EXPECT_TRUE(ran.hasValue()) << ran.status().message();

    std::vector<float> output(source.size(), kUntouched);
    const std::optional<an::PlaneView<const float>> input =
        an::PlaneView<const float>::create(middle, kWidth, kHeight, 1u, stride);
    const std::optional<an::PlaneView<float>> destination =
        an::PlaneView<float>::create(output, kWidth, kHeight, 1u, stride);
    EXPECT_TRUE(input.has_value() && destination.has_value());
    if (input.has_value() && destination.has_value())
    {
        const an::Result<an::KernelRunResult> stencil = an::referenceStencil3x3Kernel(
            execution, *input, *destination, stencilParameters(), an::KernelValidityView{});
        EXPECT_TRUE(stencil.hasValue()) << stencil.status().message();
    }
    return output;
}

[[nodiscard]] an::PreparedDeviceKernel prepareProgram(const an::Device& device, const an::ExecutionContext& execution,
                                                      const an::PointwiseProgram& program,
                                                      const an::ImageLayout& source, const an::ImageLayout& destination)
{
    const std::array layouts{source, destination};
    const an::PointwiseProgramKernelArguments arguments{.program = &program};
    const an::Result<an::PreparedDeviceKernel> prepared =
        device.prepareKernel(execution, an::DeviceKernelRequest{.plan = an::pointwiseProgramKernelPlan(program),
                                                                .bindingLayouts = layouts,
                                                                .arguments = an::deviceKernelArguments(arguments)});
    EXPECT_TRUE(prepared.hasValue()) << prepared.status().message();
    return prepared.hasValue() ? *prepared : an::PreparedDeviceKernel{};
}

[[nodiscard]] an::PreparedDeviceKernel prepareStencil(const an::Device& device, const an::ExecutionContext& execution,
                                                      const an::ImageLayout& source, const an::ImageLayout& destination)
{
    const an::Stencil3x3KernelParameters parameters = stencilParameters();
    const std::array layouts{source, destination};
    const an::Result<an::PreparedDeviceKernel> prepared =
        device.prepareKernel(execution, an::DeviceKernelRequest{.plan = an::stencil3x3KernelPlan(parameters),
                                                                .bindingLayouts = layouts,
                                                                .arguments = an::deviceKernelArguments(parameters)});
    EXPECT_TRUE(prepared.hasValue()) << prepared.status().message();
    return prepared.hasValue() ? *prepared : an::PreparedDeviceKernel{};
}

constexpr std::array<an::DeviceChainBinding, 2u> kFirstBindings{
    an::DeviceChainBinding{.buffer = 0u, .access = an::DeviceAccess::Read},
    an::DeviceChainBinding{.buffer = 1u, .access = an::DeviceAccess::Write}};
constexpr std::array<an::DeviceChainBinding, 2u> kSecondBindings{
    an::DeviceChainBinding{.buffer = 1u, .access = an::DeviceAccess::Read},
    an::DeviceChainBinding{.buffer = 2u, .access = an::DeviceAccess::Write}};

TEST(OpenClDeviceChainTest, TheIntermediateNeverLeavesTheDeviceAndTheChainMatchesTheScalarComposition)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const an::Device device = an::test::makeDevice(subject.backend);

    const an::ImageLayout inputLayout = an::test::floatPlaneLayout(kWidth, kHeight, 4u);
    const an::ImageLayout middleLayout = an::test::floatPlaneLayout(kWidth, kHeight, 64u);
    const an::ImageLayout outputLayout = an::test::floatPlaneLayout(kWidth, kHeight, 256u);
    const an::PointwiseProgram program = brightnessProgram();
    const an::PreparedDeviceKernel first = prepareProgram(device, execution, program, inputLayout, middleLayout);
    const an::PreparedDeviceKernel second = prepareStencil(device, execution, middleLayout, outputLayout);
    ASSERT_NE(first.kernel(), nullptr);
    ASSERT_NE(second.kernel(), nullptr);

    const std::array buffers{
        an::DeviceChainBuffer{.role = an::DeviceChainBufferRole::Input, .layout = inputLayout},
        an::DeviceChainBuffer{.role = an::DeviceChainBufferRole::Intermediate, .layout = middleLayout},
        an::DeviceChainBuffer{.role = an::DeviceChainBufferRole::Output, .layout = outputLayout}};
    const std::array steps{an::DeviceChainStep{.kernel = &first, .bindings = kFirstBindings},
                           an::DeviceChainStep{.kernel = &second, .bindings = kSecondBindings}};
    const an::Result<an::DeviceChain> chain = an::DeviceChain::create(device, {.buffers = buffers, .steps = steps});
    ASSERT_TRUE(chain.hasValue()) << chain.status().message();
    ASSERT_NE(chain->plan(), nullptr);
    const an::DeviceChainAccounting& accounting = chain->plan()->accounting;
    EXPECT_EQ(accounting.submissions, 4u);
    EXPECT_EQ(accounting.uploads, 1u);
    EXPECT_EQ(accounting.downloads, 1u);
    EXPECT_EQ(accounting.residentHandovers, 1u);
    EXPECT_EQ(accounting.avoidedTransfers, 2u);
    EXPECT_EQ(accounting.avoidedBytes, 2u * middleLayout.totalBytes());
    EXPECT_TRUE(chain->plan()->buffers[1].resident);

    // The device's own memory is the only place the intermediate exists: the host is given no handle
    // for it, and the one the kernels bind cannot be mapped.
    EXPECT_FALSE(chain->hostBuffer(1u).valid());
    ASSERT_NE(chain->buffer(1u).descriptor(), nullptr);
    EXPECT_EQ(chain->buffer(1u).descriptor()->residency, an::ResourceResidency::Device);
    EXPECT_TRUE(an::test::refusedFor(chain->buffer(1u).map(an::DeviceAccess::Read).status(),
                                     an::ErrorCode::IncompatibleArguments, an::DeviceFailure::NotHostAddressable));

    const std::vector<float> samples = an::test::roundingSensitiveSamples(kWidth, kHeight, 131u);
    an::test::writeFloatPlane(chain->hostBuffer(0u), inputLayout, samples);
    const an::Result<an::DeviceChainRun> run = chain->submit(device, execution);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    EXPECT_TRUE(run->submission.ok()) << run->submission.message();
    ASSERT_EQ(run->events.size(), 4u);

    const an::DeviceChainReport report = an::awaitDeviceChain(*run, execution);
    ASSERT_TRUE(report.outcome.ok()) << report.outcome.message();
    EXPECT_EQ(report.failedWork, an::kDeviceChainNoWork);
    EXPECT_EQ(report.succeededWork, 4u);
    EXPECT_EQ(report.transfersPerformed, 2u);
    EXPECT_EQ(report.kernelsRun, 2u);

    EXPECT_EQ(an::test::differingSamples(an::test::readFloatPlane(chain->hostBuffer(2u), outputLayout),
                                         referenceChain(engine, program, samples)),
              0u);

    const an::DeviceCounters counters = device.counters();
    EXPECT_EQ(counters.uploads.transfers, 1u);
    EXPECT_EQ(counters.downloads.transfers, 1u);
    EXPECT_EQ(counters.deviceCopies.transfers, 0u);
    EXPECT_EQ(counters.hostCopies.transfers, 0u);
    EXPECT_EQ(counters.kernelRuns, 2u);
    EXPECT_EQ(counters.succeededWork, 4u);
    EXPECT_EQ(counters.pendingWork, 0u);
    EXPECT_EQ(counters.backendDefects, 0u);

    // Transfer time is reported apart from kernel time; neither is held to a bound here.
    std::cout << "chain on " << subject.description.deviceName << ": transfers " << report.transferNanoseconds / 1000u
              << " us over " << report.transfersPerformed << ", kernels " << report.kernelNanoseconds / 1000u
              << " us over " << report.kernelsRun << ", intermediate " << middleLayout.totalBytes()
              << " bytes kept resident\n";
}

TEST(OpenClDeviceChainTest, AnOperationAlreadyStoppedSubmitsNothingToTheDevice)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const an::Device device = an::test::makeDevice(subject.backend);

    const an::ImageLayout inputLayout = an::test::floatPlaneLayout(kWidth, kHeight, 4u);
    const an::ImageLayout middleLayout = an::test::floatPlaneLayout(kWidth, kHeight, 64u);
    const an::ImageLayout outputLayout = an::test::floatPlaneLayout(kWidth, kHeight, 256u);
    const an::PointwiseProgram program = brightnessProgram();
    const an::PreparedDeviceKernel first = prepareProgram(device, execution, program, inputLayout, middleLayout);
    const an::PreparedDeviceKernel second = prepareStencil(device, execution, middleLayout, outputLayout);
    const std::array buffers{
        an::DeviceChainBuffer{.role = an::DeviceChainBufferRole::Input, .layout = inputLayout},
        an::DeviceChainBuffer{.role = an::DeviceChainBufferRole::Intermediate, .layout = middleLayout},
        an::DeviceChainBuffer{.role = an::DeviceChainBufferRole::Output, .layout = outputLayout}};
    const std::array steps{an::DeviceChainStep{.kernel = &first, .bindings = kFirstBindings},
                           an::DeviceChainStep{.kernel = &second, .bindings = kSecondBindings}};
    const an::Result<an::DeviceChain> chain = an::DeviceChain::create(device, {.buffers = buffers, .steps = steps});
    ASSERT_TRUE(chain.hasValue()) << chain.status().message();

    std::stop_source stop;
    stop.request_stop();
    an::ExecutionOptions stopped;
    stopped.stopToken = stop.get_token();
    const an::ExecutionContext halted = an::test::makeExecution(engine, stopped);
    const an::Result<an::DeviceChainRun> run = chain->submit(device, halted);
    ASSERT_FALSE(run.hasValue());
    EXPECT_EQ(run.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(device.counters().acceptedWork, 0u);
    EXPECT_EQ(device.counters().kernelRuns, 0u);
}

} // namespace
