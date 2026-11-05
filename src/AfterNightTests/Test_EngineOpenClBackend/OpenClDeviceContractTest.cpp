// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The device contract, held by a real GPU.
//
// The rules Test_EngineKernels' DeviceContractTest proves against a mock, proved here against an OpenCL
// device through the same checked facade: memory the host cannot map, transfers as the only way across,
// work that finishes after the call that submitted it, a resource kept on the device between two
// kernels, a fallback to the host that says so, and work arriving from several threads at once. A rule
// that holds for one backend and not for the other is a contract one of them only appears to satisfy.
//
// Every case skips by name on a host with no usable OpenCL GPU.

#include "OpenClTestSupport.h"

#include <AfterNightEngineOpenClBackend/OpenClBackend.h>

#include <AfterNightEngine/Algorithms/Kernel/Device.h>
#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/PointwiseKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/DeviceError.h>
#include <AfterNightEngine/Runtime/DeviceEvent.h>
#include <AfterNightEngine/Runtime/DeviceResource.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{

constexpr an::AffineKernelParameters kAffine{.scale = -1.75, .bias = 0.125};

/// Coefficients no binary fraction represents, so the reference's single rounding and a kernel that rounded
/// at every step give different bits.
constexpr an::AffineKernelParameters kRoundingAffine{.scale = 0.1, .bias = 1.0 / 3.0};

/// Keeps every diagnostic an operation reported, for the fallback case.
class RecordingDiagnostics final : public an::DiagnosticSink
{
public:
    void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
    {
        const std::lock_guard<std::mutex> held(m_mutex);
        try
        {
            m_events.push_back(Entry{event.severity, event.operation, event.message});
        }
        catch (...)
        {
        }
    }

    struct Entry
    {
        an::DiagnosticSeverity severity = an::DiagnosticSeverity::Info;
        std::string operation;
        std::string message;
    };

    [[nodiscard]] std::size_t count(an::DiagnosticSeverity severity) const
    {
        const std::lock_guard<std::mutex> held(m_mutex);
        std::size_t matching = 0u;
        for (const Entry& entry : m_events)
        {
            matching += entry.severity == severity ? 1u : 0u;
        }
        return matching;
    }

    [[nodiscard]] std::vector<Entry> events() const
    {
        const std::lock_guard<std::mutex> held(m_mutex);
        return m_events;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<Entry> m_events;
};

TEST(OpenClDeviceContractTest, ADeviceAcceptsTheBackendAndItsCapabilitiesDescribeADiscreteCard)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::Device device = an::test::makeDevice(subject.backend);
    ASSERT_TRUE(device.valid());

    const an::DeviceCapabilities* const capabilities = device.capabilities();
    ASSERT_NE(capabilities, nullptr);
    EXPECT_NE(capabilities->name.find(subject.description.deviceName), std::string::npos) << capabilities->name;
    EXPECT_TRUE(capabilities->hostResidency);
    EXPECT_TRUE(capabilities->pinnedResidency);
    EXPECT_FALSE(capabilities->sharedResidency);
    EXPECT_TRUE(capabilities->deviceResidency);
    EXPECT_TRUE(capabilities->asynchronous);
    EXPECT_EQ(capabilities->memoryLimitBytes, subject.description.globalMemoryBytes);
    EXPECT_EQ(capabilities->maximumResourceBytes, subject.description.maximumAllocationBytes);
    EXPECT_EQ(capabilities->maximumBindings, an::kMaxPointwiseSources + 1u);
    ASSERT_FALSE(capabilities->kernels.empty());
    EXPECT_EQ(capabilities->kernels[0].determinism, an::KernelDeterminism::Reproducible);
    EXPECT_TRUE(an::kernelContractSupports(capabilities->kernels[0].contract, an::test::openClAffinePlan()));

    // The pointwise program kernel computes the same arithmetic whatever its values mean, and the capability
    // vocabulary states one domain pair and one accuracy per contract: so it is advertised exactly over every
    // pair of the four domains, and at the power tier over the eight whose input a power step is built on.
    std::size_t programKernels = 0u;
    std::size_t atThePowerTier = 0u;
    for (const an::DeviceKernelSupport& support : capabilities->kernels)
    {
        if (!(support.contract.id == an::kPointwiseProgramKernelId))
        {
            continue;
        }
        ++programKernels;
        atThePowerTier += support.contract.capabilities.accuracy.metric == an::KernelAccuracyMetric::Ulp ? 1u : 0u;
        EXPECT_EQ(support.determinism, an::KernelDeterminism::Reproducible);
        EXPECT_EQ(support.contract.capabilities.inputSample, an::SampleType::Float32);
        EXPECT_EQ(support.contract.capabilities.computeSample, an::SampleType::Float32);
        EXPECT_EQ(support.contract.capabilities.validity, an::KernelValidityCapability::AllValidOnly);
    }
    EXPECT_EQ(programKernels, 24u);
    EXPECT_EQ(atThePowerTier, 8u);
    const auto stencil = std::ranges::find_if(capabilities->kernels, [](const an::DeviceKernelSupport& support) {
        return support.contract.id == an::kStencil3x3KernelId;
    });
    ASSERT_NE(stencil, capabilities->kernels.end());
    EXPECT_TRUE(an::kernelContractSupports(stencil->contract, an::stencil3x3KernelPlan({})));
    EXPECT_EQ(stencil->contract.capabilities.computeSample, an::SampleType::Float64);
    EXPECT_EQ(stencil->contract.capabilities.borders, an::KernelBorderCapability::ClampAndConstant);
    EXPECT_EQ(stencil->contract.capabilities.haloRadius, an::kStencil3x3HaloRadius);
    EXPECT_EQ(stencil->contract.capabilities.scratch, an::KernelScratchPolicy::DeviceAllocated);
    EXPECT_EQ(stencil->determinism, an::KernelDeterminism::Reproducible);
    EXPECT_EQ(capabilities->kernels.size(), programKernels + 2u);
}

TEST(OpenClDeviceContractTest, AnImageRoundTripsThroughDeviceMemoryExactlyAsTheScalarReferenceComputesIt)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const an::Device device = an::test::makeDevice(subject.backend);

    // An extent no work-group size divides, with rows padded to a 64-byte stride, so both transfers and
    // the kernel have to honour a stride the samples do not fill.
    constexpr std::uint32_t kWidth = 1021u;
    constexpr std::uint32_t kHeight = 769u;
    const an::ImageLayout layout = an::test::floatPlaneLayout(kWidth, kHeight, 64u);
    ASSERT_FALSE(layout.isContiguous());
    const std::vector<float> samples = an::test::roundingSensitiveSamples(kWidth, kHeight, 11u);

    const an::DeviceResource hostInput =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Host));
    const an::DeviceResource deviceInput =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Device));
    const an::DeviceResource deviceOutput =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Device));
    const an::DeviceResource hostOutput =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Pinned));
    an::test::writeFloatPlane(hostInput, layout, samples);

    const an::PreparedDeviceKernel kernel = an::test::prepareAffine(device, execution, layout, layout, kRoundingAffine);
    ASSERT_TRUE(kernel.valid());
    EXPECT_EQ(kernel.kernel()->residentBytes(), 0u);

    const std::uint64_t bytes = layout.totalBytes();
    const std::array before{an::DeviceTransfer{.source = &hostInput, .destination = &deviceInput, .bytes = bytes}};
    const std::array bindings{an::DeviceBinding{.resource = &deviceInput, .access = an::DeviceAccess::Read},
                              an::DeviceBinding{.resource = &deviceOutput, .access = an::DeviceAccess::Write}};
    const std::array after{an::DeviceTransfer{.source = &deviceOutput, .destination = &hostOutput, .bytes = bytes}};
    const an::Result<an::DeviceEvent> event = device.submit(
        execution, an::DeviceSubmission{
                       .transfersBefore = before, .kernel = &kernel, .bindings = bindings, .transfersAfter = after});
    ASSERT_TRUE(event.hasValue()) << event.status().message();

    const an::Status outcome = event->wait();
    ASSERT_TRUE(outcome.ok()) << outcome.message();
    EXPECT_EQ(event->phase(), an::DeviceWorkPhase::Finished);

    const std::vector<float> expected = an::test::referenceAffine(kWidth, kHeight, samples, kRoundingAffine);
    EXPECT_EQ(an::test::differingSamples(an::test::readFloatPlane(hostOutput, layout), expected), 0u);

    // The comparison can fail: rounded in single precision at every step, the same operation over these
    // samples differs from the reference in a large share of them, so a kernel that computed in Float32
    // could not have matched it.
    const std::size_t singlePrecisionDifferences =
        an::test::differingSamples(an::test::singlePrecisionAffine(samples, kRoundingAffine), expected);
    EXPECT_GT(singlePrecisionDifferences, samples.size() / 10u);
    std::cout << "single-precision arithmetic would differ from the reference in " << singlePrecisionDifferences
              << " of " << samples.size() << " samples\n";

    const an::DeviceWorkTiming timing = event->timing();
    EXPECT_EQ(timing.transfersRecorded, 2u);
    EXPECT_TRUE(timing.kernelRecorded);
    std::cout << kWidth << 'x' << kHeight << " padded Float32 plane on " << subject.description.deviceName
              << ": transfers " << timing.transferNanoseconds << " ns, kernel " << timing.kernelNanoseconds
              << " ns, running " << timing.runningNanoseconds << " ns\n";

    const an::DeviceCounters counters = device.counters();
    EXPECT_EQ(counters.uploads.transfers, 1u);
    EXPECT_EQ(counters.uploads.bytes, bytes);
    EXPECT_EQ(counters.downloads.transfers, 1u);
    EXPECT_EQ(counters.downloads.bytes, bytes);
    EXPECT_EQ(counters.deviceCopies.transfers, 0u);
    EXPECT_EQ(counters.hostCopies.transfers, 0u);
    EXPECT_EQ(counters.kernelRuns, 1u);
    EXPECT_EQ(counters.acceptedWork, 1u);
    EXPECT_EQ(counters.succeededWork, 1u);
    EXPECT_EQ(counters.pendingWork, 0u);
    EXPECT_EQ(counters.backendDefects, 0u);
}

TEST(OpenClDeviceContractTest, TwoChainedKernelsKeepTheirDataOnTheDeviceAndCrossToItOnlyTwice)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const an::Device device = an::test::makeDevice(subject.backend);

    constexpr std::uint32_t kWidth = 1920u;
    constexpr std::uint32_t kHeight = 1080u;
    const an::ImageLayout layout = an::test::floatPlaneLayout(kWidth, kHeight);
    const std::vector<float> samples = an::test::patternSamples(kWidth, kHeight, 5u);
    const auto describe = [&layout](an::ResourceResidency residency) {
        return an::test::planeDescriptor(layout, residency);
    };
    const an::DeviceResource host = an::test::makeResource(device, describe(an::ResourceResidency::Host));
    const an::DeviceResource first = an::test::makeResource(device, describe(an::ResourceResidency::Device));
    const an::DeviceResource middle = an::test::makeResource(device, describe(an::ResourceResidency::Device));
    const an::DeviceResource last = an::test::makeResource(device, describe(an::ResourceResidency::Device));
    const an::DeviceResource result = an::test::makeResource(device, describe(an::ResourceResidency::Host));
    an::test::writeFloatPlane(host, layout, samples);
    const an::PreparedDeviceKernel kernel = an::test::prepareAffine(device, execution, layout, layout, kAffine);

    const std::uint64_t bytes = layout.totalBytes();
    const std::array upload{an::DeviceTransfer{.source = &host, .destination = &first, .bytes = bytes}};
    const std::array firstBindings{an::DeviceBinding{.resource = &first, .access = an::DeviceAccess::Read},
                                   an::DeviceBinding{.resource = &middle, .access = an::DeviceAccess::Write}};
    const an::Result<an::DeviceEvent> firstWork = device.submit(
        execution, an::DeviceSubmission{.transfersBefore = upload, .kernel = &kernel, .bindings = firstBindings});
    ASSERT_TRUE(firstWork.hasValue()) << firstWork.status().message();

    const std::array secondBindings{an::DeviceBinding{.resource = &middle, .access = an::DeviceAccess::Read},
                                    an::DeviceBinding{.resource = &last, .access = an::DeviceAccess::Write}};
    const std::array download{an::DeviceTransfer{.source = &last, .destination = &result, .bytes = bytes}};
    const std::array dependencies{*firstWork};
    const an::Result<an::DeviceEvent> secondWork = device.submit(
        execution,
        an::DeviceSubmission{
            .kernel = &kernel, .bindings = secondBindings, .transfersAfter = download, .dependencies = dependencies});
    ASSERT_TRUE(secondWork.hasValue()) << secondWork.status().message();

    const an::Status secondOutcome = secondWork->wait();
    ASSERT_TRUE(secondOutcome.ok()) << secondOutcome.message();
    EXPECT_TRUE(firstWork->wait().ok());

    const std::vector<float> once = an::test::referenceAffine(kWidth, kHeight, samples, kAffine);
    const std::vector<float> twice = an::test::referenceAffine(kWidth, kHeight, once, kAffine);
    EXPECT_EQ(an::test::differingSamples(an::test::readFloatPlane(result, layout), twice), 0u);

    const an::DeviceCounters counters = device.counters();
    EXPECT_EQ(counters.uploads.transfers, 1u);
    EXPECT_EQ(counters.downloads.transfers, 1u);
    EXPECT_EQ(counters.deviceCopies.transfers, 0u);
    EXPECT_EQ(counters.kernelRuns, 2u);
    EXPECT_EQ(counters.succeededWork, 2u);
    EXPECT_EQ(counters.backendDefects, 0u);
}

TEST(OpenClDeviceContractTest, DeviceMemoryReachesTheHostOnlyThroughATransferAndNoSharedMemoryIsOffered)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const an::Device device = an::test::makeDevice(subject.backend);

    const an::ImageLayout layout = an::test::floatPlaneLayout(16u, 8u);
    const auto describe = [&layout](an::ResourceResidency residency) {
        return an::test::planeDescriptor(layout, residency);
    };
    const an::DeviceResource onDevice = an::test::makeResource(device, describe(an::ResourceResidency::Device));
    const an::DeviceResource deviceOutput = an::test::makeResource(device, describe(an::ResourceResidency::Device));
    const an::DeviceResource host = an::test::makeResource(device, describe(an::ResourceResidency::Host));
    const an::DeviceResource pinned = an::test::makeResource(device, describe(an::ResourceResidency::Pinned));

    const an::Result<an::DeviceMapping> refusedMapping = onDevice.map(an::DeviceAccess::Read);
    ASSERT_FALSE(refusedMapping.hasValue());
    EXPECT_TRUE(an::test::refusedFor(refusedMapping.status(), an::ErrorCode::IncompatibleArguments,
                                     an::DeviceFailure::NotHostAddressable))
        << refusedMapping.status().message();
    for (const an::DeviceResource* mappable : {&host, &pinned})
    {
        an::Result<an::DeviceMapping> mapping = mappable->map(an::DeviceAccess::ReadWrite);
        ASSERT_TRUE(mapping.hasValue()) << mapping.status().message();
        EXPECT_EQ(mapping->bytes().size(), layout.totalBytes());
        EXPECT_EQ(mapping->writableBytes().size(), layout.totalBytes());
    }

    // A discrete card has no memory both sides address, and the device says so rather than emulating it.
    const an::Result<an::DeviceResource> shared = device.createResource(describe(an::ResourceResidency::Shared));
    ASSERT_FALSE(shared.hasValue());
    EXPECT_TRUE(
        an::test::refusedFor(shared.status(), an::ErrorCode::Unsupported, an::DeviceFailure::ResidencyNotOffered))
        << shared.status().message();

    const an::PreparedDeviceKernel kernel = an::test::prepareAffine(device, execution, layout, layout, kAffine);
    const std::array hostBound{an::DeviceBinding{.resource = &host, .access = an::DeviceAccess::Read},
                               an::DeviceBinding{.resource = &deviceOutput, .access = an::DeviceAccess::Write}};
    const an::Result<an::DeviceEvent> refusedHost =
        device.submit(execution, an::DeviceSubmission{.kernel = &kernel, .bindings = hostBound});
    ASSERT_FALSE(refusedHost.hasValue());
    EXPECT_TRUE(an::test::refusedFor(refusedHost.status(), an::ErrorCode::IncompatibleArguments,
                                     an::DeviceFailure::NotDeviceReachable))
        << refusedHost.status().message();
    const std::array pinnedBound{an::DeviceBinding{.resource = &onDevice, .access = an::DeviceAccess::Read},
                                 an::DeviceBinding{.resource = &pinned, .access = an::DeviceAccess::Write}};
    const an::Result<an::DeviceEvent> refusedPinned =
        device.submit(execution, an::DeviceSubmission{.kernel = &kernel, .bindings = pinnedBound});
    ASSERT_FALSE(refusedPinned.hasValue());
    EXPECT_TRUE(an::test::refusedFor(refusedPinned.status(), an::ErrorCode::IncompatibleArguments,
                                     an::DeviceFailure::NotDeviceReachable))
        << refusedPinned.status().message();

    const an::DeviceCounters counters = device.counters();
    EXPECT_EQ(counters.refusedSubmissions, 2u);
    EXPECT_EQ(counters.acceptedWork, 0u);
    EXPECT_EQ(counters.uploads.transfers, 0u);
    EXPECT_EQ(counters.downloads.transfers, 0u);
}

TEST(OpenClDeviceContractTest, KernelSelectionRunsTheAffinePlanOnTheDeviceAndFallsBackForAPlanItCannotRun)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::Device device = an::test::makeDevice(subject.backend);

    RecordingDiagnostics diagnostics;
    an::ExecutionOptions allowing;
    allowing.diagnosticSink = &diagnostics;
    const an::ExecutionContext execution = an::test::makeExecution(engine, allowing);

    const an::Result<an::DeviceKernelSelection> supported =
        device.selectKernel(execution, an::test::openClAffinePlan());
    ASSERT_TRUE(supported.hasValue()) << supported.status().message();
    EXPECT_EQ(supported->dispatch, an::DeviceDispatch::Device);
    EXPECT_EQ(supported->reason, an::DeviceDispatchReason::KernelSupported);
    EXPECT_EQ(supported->determinism, an::KernelDeterminism::Reproducible);
    EXPECT_EQ(diagnostics.count(an::DiagnosticSeverity::Warning), 0u);

    // The kernel honours all-valid input only, so a plan carrying a validity rule is one it cannot run.
    an::KernelPlanKey unsupportedPlan = an::test::openClAffinePlan();
    unsupportedPlan.validity = an::ValidityRule::FiniteOnly;
    const an::Result<an::DeviceKernelSelection> fellBack = device.selectKernel(execution, unsupportedPlan);
    ASSERT_TRUE(fellBack.hasValue()) << fellBack.status().message();
    EXPECT_EQ(fellBack->dispatch, an::DeviceDispatch::Host);
    EXPECT_EQ(fellBack->reason, an::DeviceDispatchReason::KernelNotSupported);
    ASSERT_EQ(diagnostics.count(an::DiagnosticSeverity::Warning), 1u);
    for (const RecordingDiagnostics::Entry& entry : diagnostics.events())
    {
        if (entry.severity == an::DiagnosticSeverity::Warning)
        {
            EXPECT_FALSE(entry.message.empty());
        }
    }

    // Preparing never falls back, whatever the execution permits.
    const std::array<an::ImageLayout, 2u> layouts{an::test::floatPlaneLayout(4u, 4u),
                                                  an::test::floatPlaneLayout(4u, 4u)};
    const an::Result<an::PreparedDeviceKernel> refusedPreparation =
        device.prepareKernel(execution, an::DeviceKernelRequest{.plan = unsupportedPlan,
                                                                .bindingLayouts = layouts,
                                                                .arguments = an::deviceKernelArguments(kAffine)});
    ASSERT_FALSE(refusedPreparation.hasValue());
    EXPECT_TRUE(an::test::refusedFor(refusedPreparation.status(), an::ErrorCode::UnsupportedBackend,
                                     an::DeviceFailure::KernelNotSupported));

    an::ExecutionOptions forbidding;
    forbidding.backendFallback = an::BackendFallback::Forbidden;
    const an::ExecutionContext strict = an::test::makeExecution(engine, forbidding);
    const an::Result<an::DeviceKernelSelection> refused = device.selectKernel(strict, unsupportedPlan);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_TRUE(an::test::refusedFor(refused.status(), an::ErrorCode::UnsupportedBackend,
                                     an::DeviceFailure::KernelNotSupported))
        << refused.status().message();

    for (const an::ComputeBackend hostBackend : {an::ComputeBackend::Reference, an::ComputeBackend::Cpu})
    {
        an::ExecutionOptions constrained;
        constrained.backend = hostBackend;
        const an::ExecutionContext onHost = an::test::makeExecution(engine, constrained);
        const an::Result<an::DeviceKernelSelection> host = device.selectKernel(onHost, an::test::openClAffinePlan());
        ASSERT_TRUE(host.hasValue()) << host.status().message();
        EXPECT_EQ(host->dispatch, an::DeviceDispatch::Host);
        EXPECT_EQ(host->reason, an::DeviceDispatchReason::ExecutionConstrainedToHost);
    }
}

TEST(OpenClDeviceContractTest, AKernelRequestTheBackendCannotHonourIsRefusedWithTheBackendAsTheCause)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const an::Device device = an::test::makeDevice(subject.backend);
    const an::KernelPlanKey plan = an::test::openClAffinePlan();

    const auto expectRefused = [&](std::span<const an::ImageLayout> layouts,
                                   const an::AffineKernelParameters& arguments) {
        const an::Result<an::PreparedDeviceKernel> prepared = device.prepareKernel(
            execution, an::DeviceKernelRequest{
                           .plan = plan, .bindingLayouts = layouts, .arguments = an::deviceKernelArguments(arguments)});
        ASSERT_FALSE(prepared.hasValue());
        EXPECT_TRUE(
            an::test::refusedFor(prepared.status(), an::ErrorCode::InvalidArgument, an::DeviceFailure::BackendFailure))
            << prepared.status().message();
    };

    // Two planes of different extents.
    const std::array<an::ImageLayout, 2u> mismatched{an::test::floatPlaneLayout(16u, 8u),
                                                     an::test::floatPlaneLayout(8u, 16u)};
    expectRefused(mismatched, kAffine);

    // Coefficients that are not finite.
    const std::array<an::ImageLayout, 2u> matching{an::test::floatPlaneLayout(16u, 8u),
                                                   an::test::floatPlaneLayout(16u, 8u)};
    expectRefused(matching, an::AffineKernelParameters{.scale = std::numeric_limits<double>::infinity(), .bias = 0.0});

    // One binding where the kernel needs an input and an output.
    const std::array<an::ImageLayout, 1u> single{an::test::floatPlaneLayout(16u, 8u)};
    expectRefused(single, kAffine);

    EXPECT_EQ(device.counters().liveKernels, 0u);
}

TEST(OpenClDeviceContractTest, CopiesWithinTheHostAndWithinTheDeviceAreCountedByKind)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const an::Device device = an::test::makeDevice(subject.backend);

    const an::ImageLayout layout = an::test::floatPlaneLayout(64u, 32u);
    const auto describe = [&layout](an::ResourceResidency residency) {
        return an::test::planeDescriptor(layout, residency);
    };
    const an::DeviceResource source = an::test::makeResource(device, describe(an::ResourceResidency::Host));
    const an::DeviceResource pinned = an::test::makeResource(device, describe(an::ResourceResidency::Pinned));
    const an::DeviceResource first = an::test::makeResource(device, describe(an::ResourceResidency::Device));
    const an::DeviceResource second = an::test::makeResource(device, describe(an::ResourceResidency::Device));
    const an::DeviceResource destination = an::test::makeResource(device, describe(an::ResourceResidency::Host));
    const std::vector<float> samples = an::test::patternSamples(64u, 32u, 17u);
    an::test::writeFloatPlane(source, layout, samples);

    // Host to pinned, pinned to device, device to device, device to host: one transfer of every kind.
    const std::uint64_t bytes = layout.totalBytes();
    const std::array transfers{an::DeviceTransfer{.source = &source, .destination = &pinned, .bytes = bytes},
                               an::DeviceTransfer{.source = &pinned, .destination = &first, .bytes = bytes},
                               an::DeviceTransfer{.source = &first, .destination = &second, .bytes = bytes},
                               an::DeviceTransfer{.source = &second, .destination = &destination, .bytes = bytes}};
    const an::Result<an::DeviceEvent> event =
        device.submit(execution, an::DeviceSubmission{.transfersBefore = transfers});
    ASSERT_TRUE(event.hasValue()) << event.status().message();
    const an::Status outcome = event->wait();
    ASSERT_TRUE(outcome.ok()) << outcome.message();

    EXPECT_EQ(an::test::differingSamples(an::test::readFloatPlane(destination, layout), samples), 0u);
    EXPECT_EQ(event->timing().transfersRecorded, 4u);
    EXPECT_FALSE(event->timing().kernelRecorded);

    const an::DeviceCounters counters = device.counters();
    EXPECT_EQ(counters.hostCopies.transfers, 1u);
    EXPECT_EQ(counters.uploads.transfers, 1u);
    EXPECT_EQ(counters.deviceCopies.transfers, 1u);
    EXPECT_EQ(counters.deviceCopies.bytes, bytes);
    EXPECT_EQ(counters.downloads.transfers, 1u);
    EXPECT_EQ(counters.kernelRuns, 0u);
}

TEST(OpenClDeviceContractTest, WorkSubmittedFromSeveralThreadsIsAllAcceptedRunAndCounted)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::Device device = an::test::makeDevice(subject.backend);

    constexpr std::size_t kThreads = 4u;
    constexpr std::size_t kSubmissionsPerThread = 25u;
    const an::ImageLayout layout = an::test::floatPlaneLayout(24u, 12u);
    std::atomic<std::size_t> failures{0u};
    std::vector<std::thread> threads;
    for (std::size_t thread = 0u; thread < kThreads; ++thread)
    {
        threads.emplace_back([&, thread] {
            const an::ExecutionContext execution = an::test::makeExecution(engine);
            const an::PreparedDeviceKernel kernel = an::test::prepareAffine(device, execution, layout, layout, kAffine);
            for (std::size_t submission = 0u; submission < kSubmissionsPerThread; ++submission)
            {
                const auto describe = [&layout](an::ResourceResidency residency) {
                    return an::test::planeDescriptor(layout, residency);
                };
                const an::DeviceResource hostIn = an::test::makeResource(device, describe(an::ResourceResidency::Host));
                const an::DeviceResource deviceIn =
                    an::test::makeResource(device, describe(an::ResourceResidency::Device));
                const an::DeviceResource deviceOut =
                    an::test::makeResource(device, describe(an::ResourceResidency::Device));
                const an::DeviceResource hostOut =
                    an::test::makeResource(device, describe(an::ResourceResidency::Host));
                const std::vector<float> samples =
                    an::test::patternSamples(24u, 12u, static_cast<std::uint32_t>(thread * 100u + submission));
                an::test::writeFloatPlane(hostIn, layout, samples);

                const std::uint64_t bytes = layout.totalBytes();
                const std::array before{
                    an::DeviceTransfer{.source = &hostIn, .destination = &deviceIn, .bytes = bytes}};
                const std::array bindings{an::DeviceBinding{.resource = &deviceIn, .access = an::DeviceAccess::Read},
                                          an::DeviceBinding{.resource = &deviceOut, .access = an::DeviceAccess::Write}};
                const std::array after{
                    an::DeviceTransfer{.source = &deviceOut, .destination = &hostOut, .bytes = bytes}};
                const an::Result<an::DeviceEvent> event = device.submit(
                    execution,
                    an::DeviceSubmission{
                        .transfersBefore = before, .kernel = &kernel, .bindings = bindings, .transfersAfter = after});
                if (!event.hasValue() || event->wait().failed() ||
                    an::test::differingSamples(an::test::readFloatPlane(hostOut, layout),
                                               an::test::referenceAffine(24u, 12u, samples, kAffine)) != 0u)
                {
                    failures.fetch_add(1u);
                }
            }
        });
    }
    for (std::thread& worker : threads)
    {
        worker.join();
    }

    EXPECT_EQ(failures.load(), 0u);
    const an::DeviceCounters counters = device.counters();
    const std::uint64_t total = kThreads * kSubmissionsPerThread;
    EXPECT_EQ(counters.acceptedWork, total);
    EXPECT_EQ(counters.succeededWork, total);
    EXPECT_EQ(counters.uploads.transfers, total);
    EXPECT_EQ(counters.downloads.transfers, total);
    EXPECT_EQ(counters.kernelRuns, total);
    EXPECT_EQ(counters.pendingWork, 0u);
    EXPECT_EQ(counters.liveResources, 0u);
    EXPECT_EQ(counters.deviceMemory.liveBytes, 0u);
    EXPECT_EQ(counters.backendDefects, 0u);
}

} // namespace
