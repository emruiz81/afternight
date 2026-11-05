// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for Algorithms/Kernel/Device.h that must hold for any backend.
//
// Each case runs twice: against a mock that finishes work later on a thread of its own, and against
// the same mock finishing every piece of work before `enqueue` returns. A contract that only worked
// when completion was immediate would pass the second run and fail the first - and discovering that
// only when a real device arrived is the redesign this seam exists to avoid.
//
// What is proved here is the part a GPU would need and a synchronous interface cannot express:
// memory the host cannot read, transfers that are the only way across, work that finishes after the
// call that submitted it, a resource kept on the device between two kernels, and a fallback to the
// host that says so.

#include "MockDeviceBackend.h"

#include <AfterNightEngine/Algorithms/Kernel/Device.h>
#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/PointwiseKernel.h>
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

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{

constexpr an::AffineKernelParameters kAffine{.scale = -1.75, .bias = 0.125};

/// Keeps every diagnostic an operation reported, for the fallback cases.
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

[[nodiscard]] bool refusedFor(const an::Status& status, an::ErrorCode code, an::DeviceFailure reason)
{
    const std::optional<an::DeviceFailure> carried = an::deviceFailureReason(status);
    return status.code() == code && carried.has_value() && *carried == reason;
}

class DeviceContractTest : public ::testing::TestWithParam<bool>
{
protected:
    [[nodiscard]] an::test::MockDeviceOptions mockOptions() const
    {
        an::test::MockDeviceOptions options;
        options.asynchronous = GetParam();
        return options;
    }
};

TEST_P(DeviceContractTest, AnImageRoundTripsThroughDeviceMemoryExactlyAsTheScalarReferenceComputesIt)
{
    const an::EngineContext engine = an::test::makeDeviceTestEngine();
    const an::ExecutionContext execution = an::test::makeDeviceTestExecution(engine);
    const auto backend = std::make_shared<an::test::MockDeviceBackend>(mockOptions());
    const an::Device device = an::test::makeMockDevice(backend);

    // Rows padded to a 64-byte stride, so both transfers and the kernel have to honour a stride the
    // samples do not fill.
    const an::ImageLayout layout = an::test::floatPlaneLayout(37u, 23u, 64u);
    ASSERT_FALSE(layout.isContiguous());
    const std::vector<float> samples = an::test::patternSamples(37u, 23u, 11u);

    const an::DeviceResource hostInput =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Host));
    const an::DeviceResource deviceInput =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Device));
    const an::DeviceResource deviceOutput =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Device));
    const an::DeviceResource hostOutput =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Pinned));
    an::test::writeFloatPlane(hostInput, layout, samples);

    const an::PreparedDeviceKernel kernel = an::test::prepareAffine(device, execution, layout, layout, kAffine);
    ASSERT_TRUE(kernel.valid());
    EXPECT_EQ(kernel.deviceId(), device.id());

    const std::uint64_t bytes = layout.totalBytes();
    const std::array before{an::DeviceTransfer{.source = &hostInput, .destination = &deviceInput, .bytes = bytes}};
    const std::array bindings{an::DeviceBinding{.resource = &deviceInput, .access = an::DeviceAccess::Read},
                              an::DeviceBinding{.resource = &deviceOutput, .access = an::DeviceAccess::Write}};
    const std::array after{an::DeviceTransfer{.source = &deviceOutput, .destination = &hostOutput, .bytes = bytes}};
    const an::Result<an::DeviceEvent> event = device.submit(
        execution, an::DeviceSubmission{
                       .transfersBefore = before, .kernel = &kernel, .bindings = bindings, .transfersAfter = after});
    ASSERT_TRUE(event.hasValue()) << event.status().message();
    EXPECT_EQ(event->deviceId(), device.id());

    const an::Status outcome = event->wait();
    ASSERT_TRUE(outcome.ok()) << outcome.message();
    EXPECT_EQ(event->phase(), an::DeviceWorkPhase::Finished);
    ASSERT_TRUE(event->outcome().has_value());
    EXPECT_TRUE(event->outcome()->ok());

    const std::vector<float> expected = an::test::referenceAffine(37u, 23u, samples, kAffine);
    EXPECT_TRUE(an::test::sameBits(an::test::readFloatPlane(hostOutput, layout), expected));

    const an::DeviceWorkTiming timing = event->timing();
    EXPECT_EQ(timing.transfersRecorded, 2u);
    EXPECT_TRUE(timing.kernelRecorded);

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

TEST_P(DeviceContractTest, TwoChainedKernelsKeepTheirDataOnTheDeviceAndCrossToItOnlyTwice)
{
    const an::EngineContext engine = an::test::makeDeviceTestEngine();
    const an::ExecutionContext execution = an::test::makeDeviceTestExecution(engine);
    const auto backend = std::make_shared<an::test::MockDeviceBackend>(mockOptions());
    const an::Device device = an::test::makeMockDevice(backend);

    const an::ImageLayout layout = an::test::floatPlaneLayout(64u, 48u);
    const std::vector<float> samples = an::test::patternSamples(64u, 48u, 5u);
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

    ASSERT_TRUE(secondWork->wait().ok());
    EXPECT_TRUE(firstWork->wait().ok());

    const std::vector<float> once = an::test::referenceAffine(64u, 48u, samples, kAffine);
    const std::vector<float> twice = an::test::referenceAffine(64u, 48u, once, kAffine);
    EXPECT_TRUE(an::test::sameBits(an::test::readFloatPlane(result, layout), twice));

    const an::DeviceCounters counters = device.counters();
    EXPECT_EQ(counters.uploads.transfers, 1u);
    EXPECT_EQ(counters.downloads.transfers, 1u);
    EXPECT_EQ(counters.deviceCopies.transfers, 0u);
    EXPECT_EQ(counters.kernelRuns, 2u);
    EXPECT_EQ(counters.succeededWork, 2u);
}

TEST_P(DeviceContractTest, DeviceMemoryReachesTheHostOnlyThroughATransfer)
{
    const an::EngineContext engine = an::test::makeDeviceTestEngine();
    const an::ExecutionContext execution = an::test::makeDeviceTestExecution(engine);
    const auto backend = std::make_shared<an::test::MockDeviceBackend>(mockOptions());
    const an::Device device = an::test::makeMockDevice(backend);

    const an::ImageLayout layout = an::test::floatPlaneLayout(16u, 8u);
    const auto describe = [&layout](an::ResourceResidency residency) {
        return an::test::planeDescriptor(layout, residency);
    };
    const an::DeviceResource onDevice = an::test::makeResource(device, describe(an::ResourceResidency::Device));
    const an::DeviceResource host = an::test::makeResource(device, describe(an::ResourceResidency::Host));
    const an::DeviceResource pinned = an::test::makeResource(device, describe(an::ResourceResidency::Pinned));
    const an::DeviceResource sharedInput = an::test::makeResource(device, describe(an::ResourceResidency::Shared));
    const an::DeviceResource sharedOutput = an::test::makeResource(device, describe(an::ResourceResidency::Shared));

    const an::Result<an::DeviceMapping> refusedMapping = onDevice.map(an::DeviceAccess::Read);
    ASSERT_FALSE(refusedMapping.hasValue());
    EXPECT_TRUE(refusedFor(refusedMapping.status(), an::ErrorCode::IncompatibleArguments,
                           an::DeviceFailure::NotHostAddressable))
        << refusedMapping.status().message();
    for (const an::DeviceResource* mappable : {&host, &pinned, &sharedInput})
    {
        an::Result<an::DeviceMapping> mapping = mappable->map(an::DeviceAccess::ReadWrite);
        ASSERT_TRUE(mapping.hasValue()) << mapping.status().message();
        EXPECT_EQ(mapping->bytes().size(), layout.totalBytes());
        EXPECT_EQ(mapping->writableBytes().size(), layout.totalBytes());
    }

    const an::PreparedDeviceKernel kernel = an::test::prepareAffine(device, execution, layout, layout, kAffine);
    const std::array hostBound{an::DeviceBinding{.resource = &host, .access = an::DeviceAccess::Read},
                               an::DeviceBinding{.resource = &sharedOutput, .access = an::DeviceAccess::Write}};
    const an::Result<an::DeviceEvent> refusedHost =
        device.submit(execution, an::DeviceSubmission{.kernel = &kernel, .bindings = hostBound});
    ASSERT_FALSE(refusedHost.hasValue());
    EXPECT_TRUE(
        refusedFor(refusedHost.status(), an::ErrorCode::IncompatibleArguments, an::DeviceFailure::NotDeviceReachable))
        << refusedHost.status().message();
    const std::array pinnedBound{an::DeviceBinding{.resource = &sharedInput, .access = an::DeviceAccess::Read},
                                 an::DeviceBinding{.resource = &pinned, .access = an::DeviceAccess::Write}};
    const an::Result<an::DeviceEvent> refusedPinned =
        device.submit(execution, an::DeviceSubmission{.kernel = &kernel, .bindings = pinnedBound});
    ASSERT_FALSE(refusedPinned.hasValue());
    EXPECT_TRUE(refusedFor(refusedPinned.status(), an::ErrorCode::IncompatibleArguments,
                           an::DeviceFailure::NotDeviceReachable));

    // Shared memory is the one residency both sides reach, so a kernel over it needs no transfer.
    const std::vector<float> samples = an::test::patternSamples(16u, 8u, 3u);
    an::test::writeFloatPlane(sharedInput, layout, samples);
    const std::array sharedBound{an::DeviceBinding{.resource = &sharedInput, .access = an::DeviceAccess::Read},
                                 an::DeviceBinding{.resource = &sharedOutput, .access = an::DeviceAccess::Write}};
    const an::Result<an::DeviceEvent> work =
        device.submit(execution, an::DeviceSubmission{.kernel = &kernel, .bindings = sharedBound});
    ASSERT_TRUE(work.hasValue()) << work.status().message();
    ASSERT_TRUE(work->wait().ok());
    EXPECT_TRUE(an::test::sameBits(an::test::readFloatPlane(sharedOutput, layout),
                                   an::test::referenceAffine(16u, 8u, samples, kAffine)));

    const an::DeviceCounters counters = device.counters();
    EXPECT_EQ(counters.uploads.transfers, 0u);
    EXPECT_EQ(counters.downloads.transfers, 0u);
    EXPECT_EQ(counters.kernelRuns, 1u);
    EXPECT_EQ(counters.refusedSubmissions, 2u);
    EXPECT_EQ(counters.acceptedWork, 1u);
}

TEST_P(DeviceContractTest, KernelSelectionSaysWhereAndWhyAndWarnsWhenItFallsBack)
{
    const an::EngineContext engine = an::test::makeDeviceTestEngine();
    const auto backend = std::make_shared<an::test::MockDeviceBackend>(mockOptions());
    const an::Device device = an::test::makeMockDevice(backend);

    RecordingDiagnostics diagnostics;
    an::ExecutionOptions allowing;
    allowing.diagnosticSink = &diagnostics;
    const an::ExecutionContext execution = an::test::makeDeviceTestExecution(engine, allowing);

    const an::Result<an::DeviceKernelSelection> supported = device.selectKernel(execution, an::test::mockAffinePlan());
    ASSERT_TRUE(supported.hasValue()) << supported.status().message();
    EXPECT_EQ(supported->dispatch, an::DeviceDispatch::Device);
    EXPECT_EQ(supported->reason, an::DeviceDispatchReason::KernelSupported);
    EXPECT_EQ(supported->determinism, an::KernelDeterminism::Reproducible);
    EXPECT_EQ(diagnostics.count(an::DiagnosticSeverity::Warning), 0u);
    EXPECT_GE(diagnostics.count(an::DiagnosticSeverity::Debug), 1u);

    // The mock honours all-valid input only, so a plan carrying a validity rule is one it cannot run.
    an::KernelPlanKey unsupportedPlan = an::test::mockAffinePlan();
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
            EXPECT_FALSE(entry.operation.empty());
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
    EXPECT_TRUE(refusedFor(refusedPreparation.status(), an::ErrorCode::UnsupportedBackend,
                           an::DeviceFailure::KernelNotSupported));

    an::ExecutionOptions forbidding;
    forbidding.backendFallback = an::BackendFallback::Forbidden;
    const an::ExecutionContext strict = an::test::makeDeviceTestExecution(engine, forbidding);
    const an::Result<an::DeviceKernelSelection> refused = device.selectKernel(strict, unsupportedPlan);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_TRUE(refusedFor(refused.status(), an::ErrorCode::UnsupportedBackend, an::DeviceFailure::KernelNotSupported))
        << refused.status().message();
    const an::Result<an::DeviceKernelSelection> strictSupported =
        device.selectKernel(strict, an::test::mockAffinePlan());
    ASSERT_TRUE(strictSupported.hasValue());
    EXPECT_EQ(strictSupported->dispatch, an::DeviceDispatch::Device);

    for (const an::ComputeBackend hostBackend : {an::ComputeBackend::Reference, an::ComputeBackend::Cpu})
    {
        an::ExecutionOptions constrained;
        constrained.backend = hostBackend;
        const an::ExecutionContext onHost = an::test::makeDeviceTestExecution(engine, constrained);
        const an::Result<an::DeviceKernelSelection> host = device.selectKernel(onHost, an::test::mockAffinePlan());
        ASSERT_TRUE(host.hasValue()) << host.status().message();
        EXPECT_EQ(host->dispatch, an::DeviceDispatch::Host);
        EXPECT_EQ(host->reason, an::DeviceDispatchReason::ExecutionConstrainedToHost);
    }
}

TEST_P(DeviceContractTest, ADeviceRefusesToShutDownWhileAnythingOfItsIsOutstanding)
{
    const an::EngineContext engine = an::test::makeDeviceTestEngine();
    const an::ExecutionContext execution = an::test::makeDeviceTestExecution(engine);
    const auto backend = std::make_shared<an::test::MockDeviceBackend>(mockOptions());
    an::Device device = an::test::makeMockDevice(backend);

    const an::ImageLayout layout = an::test::floatPlaneLayout(8u, 8u);
    an::DeviceResource source =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Host));
    an::DeviceResource copy =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Host));
    const std::array transfer{
        an::DeviceTransfer{.source = &source, .destination = &copy, .bytes = layout.totalBytes()}};
    const an::Result<an::DeviceEvent> event =
        device.submit(execution, an::DeviceSubmission{.transfersBefore = transfer});
    ASSERT_TRUE(event.hasValue()) << event.status().message();
    ASSERT_TRUE(event->wait().ok());
    EXPECT_EQ(device.counters().hostCopies.transfers, 1u);

    an::PreparedDeviceKernel kernel = an::test::prepareAffine(device, execution, layout, layout, kAffine);
    an::Result<an::DeviceMapping> mapping = source.map(an::DeviceAccess::Read);
    ASSERT_TRUE(mapping.hasValue()) << mapping.status().message();
    source.release();
    copy.release();

    const auto expectBusy = [&device] {
        const an::Status refusal = device.shutdown();
        EXPECT_TRUE(refusedFor(refusal, an::ErrorCode::Busy, an::DeviceFailure::DeviceInUse)) << refusal.message();
        EXPECT_TRUE(device.valid());
    };

    // The mapping keeps its resource, and the prepared kernel keeps the backend's program.
    EXPECT_EQ(device.counters().liveMappings, 1u);
    EXPECT_EQ(device.counters().liveResources, 1u);
    EXPECT_EQ(device.counters().liveKernels, 1u);
    expectBusy();

    // Ending the mapping lets its resource go, whose handle was already released.
    mapping->unmap();
    EXPECT_EQ(device.counters().liveMappings, 0u);
    EXPECT_EQ(device.counters().liveResources, 0u);
    expectBusy();
    kernel = an::PreparedDeviceKernel{};
    EXPECT_EQ(device.counters().liveKernels, 0u);

    // A finished event holds nothing of the device, so it neither blocks the shutdown nor dies with it.
    const an::Status closed = device.shutdown();
    EXPECT_TRUE(closed.ok()) << closed.message();
    EXPECT_FALSE(device.valid());
    EXPECT_TRUE(device.shutdown().ok());
    ASSERT_TRUE(event->outcome().has_value());
    EXPECT_TRUE(event->outcome()->ok());
    EXPECT_EQ(event->timing().transfersRecorded, 1u);
}

TEST_P(DeviceContractTest, WorkSubmittedFromSeveralThreadsIsAllAcceptedRunAndCounted)
{
    const an::EngineContext engine = an::test::makeDeviceTestEngine();
    const auto backend = std::make_shared<an::test::MockDeviceBackend>(mockOptions());
    const an::Device device = an::test::makeMockDevice(backend);

    constexpr std::size_t kThreads = 4u;
    constexpr std::size_t kSubmissionsPerThread = 25u;
    const an::ImageLayout layout = an::test::floatPlaneLayout(24u, 12u);
    std::atomic<std::size_t> failures{0u};
    std::vector<std::thread> threads;
    for (std::size_t thread = 0u; thread < kThreads; ++thread)
    {
        threads.emplace_back([&, thread] {
            const an::ExecutionContext execution = an::test::makeDeviceTestExecution(engine);
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
                    !an::test::sameBits(an::test::readFloatPlane(hostOut, layout),
                                        an::test::referenceAffine(24u, 12u, samples, kAffine)))
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
    EXPECT_EQ(backend->liveAllocations(), 0u);
}

INSTANTIATE_TEST_SUITE_P(MockCompletion, DeviceContractTest, ::testing::Values(true, false),
                         [](const ::testing::TestParamInfo<bool>& info) {
                             return std::string(info.param ? "Asynchronous" : "Synchronous");
                         });

} // namespace
