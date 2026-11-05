// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for how Algorithms/Kernel/Device.h contains a backend that misbehaves, and for the
// lifetime of a device, its resources and its events.
//
// A backend is code the engine does not own. It can describe itself incoherently, drop work, complete
// it twice, claim success for steps it never recorded, refuse work or throw. In every one of those the
// device must end each wait with an answer rather than hang, and count what it contained. And because
// a device's memory outlives the calls that used it, the lifetime rules are tested at the edges: a
// handle destroyed early, a backend released on its own thread, and handles that hold nothing.

#include "MockDeviceBackend.h"
#include "../HeapFailure.h"

#include <AfterNightEngine/Algorithms/Kernel/Device.h>
#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/PointwiseKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DeviceError.h>
#include <AfterNightEngine/Runtime/DeviceEvent.h>
#include <AfterNightEngine/Runtime/DeviceResource.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>

namespace
{

constexpr an::AffineKernelParameters kAffine{.scale = 2.0, .bias = 1.0};

[[nodiscard]] bool refusedFor(const an::Status& status, an::ErrorCode code, an::DeviceFailure reason)
{
    const std::optional<an::DeviceFailure> carried = an::deviceFailureReason(status);
    return status.code() == code && carried.has_value() && *carried == reason;
}

[[nodiscard]] an::Result<an::DeviceEvent> submitCopy(const an::Device& device, const an::ExecutionContext& execution,
                                                     const an::DeviceResource& source,
                                                     const an::DeviceResource& destination,
                                                     const an::ImageLayout& layout,
                                                     std::span<const an::DeviceEvent> dependencies = {})
{
    const std::array steps{
        an::DeviceTransfer{.source = &source, .destination = &destination, .bytes = layout.totalBytes()}};
    return device.submit(execution, an::DeviceSubmission{.transfersBefore = steps, .dependencies = dependencies});
}

/// Polls a condition that another thread makes true, for at most ten seconds.
template <typename PredicateT> [[nodiscard]] bool eventually(PredicateT predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

/// A device over a mock with the given options, and what the backend-defect cases submit.
struct DefectBench
{
    explicit DefectBench(const an::test::MockDeviceOptions& options)
        : engine(an::test::makeDeviceTestEngine()), execution(an::test::makeDeviceTestExecution(engine)),
          backend(std::make_shared<an::test::MockDeviceBackend>(options)), device(an::test::makeMockDevice(backend)),
          layout(an::test::floatPlaneLayout(8u, 4u)),
          host(an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Host))),
          onDevice(an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Device))),
          copy(an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Host)))
    {
    }

    an::EngineContext engine;
    an::ExecutionContext execution;
    std::shared_ptr<an::test::MockDeviceBackend> backend;
    an::Device device;
    an::ImageLayout layout;
    an::DeviceResource host;
    an::DeviceResource onDevice;
    an::DeviceResource copy;
};

TEST(DeviceLifecycleTest, DeviceFailureCallbacksFinishWhenTheHeapIsExhausted)
{
    if (!an::test::HeapFailure::kAvailable)
        GTEST_SKIP() << "requires the shared MSVC Debug CRT allocation hook";
    for (int mode = 0; mode != 4; ++mode)
    {
        std::optional<an::test::HeapFailure> failure;
        an::test::MockDeviceOptions options;
        options.asynchronous = false;
        options.abandonWork = mode == 0;
        options.skipTransferRecords = mode == 1;
        options.refuseEnqueue = mode == 2;
        options.throwOnEnqueue = mode == 3;
        options.beforeEnqueue = [](void* data) noexcept {
            static_cast<std::optional<an::test::HeapFailure>*>(data)->emplace(0u);
        };
        options.enqueueHookData = &failure;
        const DefectBench bench(options);
        auto work = submitCopy(bench.device, bench.execution, bench.host, bench.onDevice, bench.layout);
        const bool exhausted = failure && failure->refused() != 0u;
        failure.reset();
        ASSERT_TRUE(exhausted) << mode;
        ASSERT_TRUE(work.hasValue()) << mode;
        const auto outcome = work->wait();
        EXPECT_EQ(outcome.code(), mode == 2   ? an::ErrorCode::Busy
                                  : mode == 3 ? an::ErrorCode::ExternalFailure
                                              : an::ErrorCode::Internal)
            << mode;
        EXPECT_EQ(bench.device.counters().pendingWork, 0u);
        EXPECT_EQ(bench.device.counters().failedWork, 1u);
    }
}

TEST(DeviceLifecycleTest, ABackendThatDescribesItselfIncoherentlyIsRefusedWhenTheDeviceIsCreated)
{
    const an::Result<an::Device> nullBackend = an::Device::create(nullptr);
    ASSERT_FALSE(nullBackend.hasValue());
    EXPECT_EQ(nullBackend.code(), an::ErrorCode::InvalidArgument);

    const an::DeviceCapabilities coherent = an::test::MockDeviceBackend{}.capabilities();
    const auto expectRefused = [](const an::DeviceCapabilities& capabilities, const char* what) {
        an::test::MockDeviceOptions options;
        options.capabilities = capabilities;
        const an::Result<an::Device> refused =
            an::Device::create(std::make_shared<an::test::MockDeviceBackend>(options));
        ASSERT_FALSE(refused.hasValue()) << what;
        EXPECT_TRUE(
            refusedFor(refused.status(), an::ErrorCode::InvalidArgument, an::DeviceFailure::InvalidCapabilities))
            << what << ": " << refused.status().message();
    };

    an::DeviceCapabilities unnamed = coherent;
    unnamed.name.clear();
    expectRefused(unnamed, "an empty name");
    an::DeviceCapabilities noMemory = coherent;
    noMemory.hostResidency = false;
    noMemory.pinnedResidency = false;
    noMemory.sharedResidency = false;
    noMemory.deviceResidency = false;
    expectRefused(noMemory, "no residency at all");
    an::DeviceCapabilities hostOnly = coherent;
    hostOnly.sharedResidency = false;
    hostOnly.deviceResidency = false;
    expectRefused(hostOnly, "no residency a kernel can bind");
    an::DeviceCapabilities noLimit = coherent;
    noLimit.memoryLimitBytes = 0u;
    expectRefused(noLimit, "a memory limit of zero");
    an::DeviceCapabilities noResource = coherent;
    noResource.maximumResourceBytes = 0u;
    expectRefused(noResource, "a resource size limit of zero");
    an::DeviceCapabilities oddAlignment = coherent;
    oddAlignment.maximumAlignmentBytes = 3u;
    expectRefused(oddAlignment, "an alignment ceiling that is not a power of two");
    an::DeviceCapabilities noBindings = coherent;
    noBindings.maximumBindings = 0u;
    expectRefused(noBindings, "no bindings");
    an::DeviceCapabilities noTransfers = coherent;
    noTransfers.maximumTransfers = 0u;
    expectRefused(noTransfers, "no transfers");
    an::DeviceCapabilities unknownKernel = coherent;
    ASSERT_FALSE(unknownKernel.kernels.empty());
    unknownKernel.kernels.front().contract.id = an::KernelId{};
    expectRefused(unknownKernel, "a kernel whose operation is not a known one");
    an::DeviceCapabilities oddDeterminism = coherent;
    oddDeterminism.kernels.front().determinism = static_cast<an::KernelDeterminism>(7);
    expectRefused(oddDeterminism, "a determinism outside the enumeration");

    an::test::MockDeviceOptions options;
    options.capabilities = coherent;
    const an::Result<an::Device> accepted = an::Device::create(std::make_shared<an::test::MockDeviceBackend>(options));
    ASSERT_TRUE(accepted.hasValue()) << accepted.status().message();
    ASSERT_NE(accepted->capabilities(), nullptr);
    EXPECT_EQ(accepted->capabilities()->name, "mock");
    EXPECT_NE(accepted->id(), 0u);
    EXPECT_TRUE(an::offersResidency(*accepted->capabilities(), an::ResourceResidency::Pinned));
    EXPECT_FALSE(an::offersResidency(*accepted->capabilities(), static_cast<an::ResourceResidency>(9)));
}

TEST(DeviceLifecycleTest, WorkTheBackendDropsIsFinishedAsAnEngineDefectRatherThanWaitedForForever)
{
    an::test::MockDeviceOptions options;
    options.abandonWork = true;
    const DefectBench bench(options);

    const an::Result<an::DeviceEvent> work =
        submitCopy(bench.device, bench.execution, bench.host, bench.onDevice, bench.layout);
    ASSERT_TRUE(work.hasValue()) << work.status().message();
    const an::Status outcome = work->wait();
    EXPECT_TRUE(refusedFor(outcome, an::ErrorCode::Internal, an::DeviceFailure::WorkAbandoned)) << outcome.message();

    const std::array afterWork{*work};
    const an::Result<an::DeviceEvent> dependent =
        submitCopy(bench.device, bench.execution, bench.onDevice, bench.copy, bench.layout, afterWork);
    ASSERT_TRUE(dependent.hasValue()) << dependent.status().message();
    EXPECT_TRUE(refusedFor(dependent->wait(), an::ErrorCode::Internal, an::DeviceFailure::DependencyFailed));

    const an::DeviceCounters counters = bench.device.counters();
    EXPECT_EQ(counters.backendDefects, 1u);
    EXPECT_EQ(counters.failedWork, 2u);
    EXPECT_EQ(counters.pendingWork, 0u);
}

TEST(DeviceLifecycleTest, ASecondCompletionIsIgnoredAndCountedAsABackendDefect)
{
    an::test::MockDeviceOptions options;
    options.asynchronous = false;
    options.completeTwice = true;
    const DefectBench bench(options);

    const an::Result<an::DeviceEvent> work =
        submitCopy(bench.device, bench.execution, bench.host, bench.onDevice, bench.layout);
    ASSERT_TRUE(work.hasValue()) << work.status().message();
    EXPECT_TRUE(work->wait().ok());

    const an::DeviceCounters counters = bench.device.counters();
    EXPECT_EQ(counters.backendDefects, 1u);
    EXPECT_EQ(counters.succeededWork, 1u);
    EXPECT_EQ(counters.failedWork, 0u);
    EXPECT_EQ(counters.uploads.transfers, 1u);
}

TEST(DeviceLifecycleTest, ASuccessThatDoesNotRecordEveryDeclaredStepIsTurnedIntoAFailure)
{
    an::test::MockDeviceOptions options;
    options.asynchronous = false;
    options.skipTransferRecords = true;
    const DefectBench bench(options);

    const an::Result<an::DeviceEvent> work =
        submitCopy(bench.device, bench.execution, bench.host, bench.onDevice, bench.layout);
    ASSERT_TRUE(work.hasValue()) << work.status().message();
    const an::Status outcome = work->wait();
    EXPECT_TRUE(refusedFor(outcome, an::ErrorCode::Internal, an::DeviceFailure::IncompleteReport)) << outcome.message();

    const an::DeviceCounters counters = bench.device.counters();
    EXPECT_EQ(counters.uploads.transfers, 0u);
    EXPECT_EQ(counters.backendDefects, 1u);
    EXPECT_EQ(counters.failedWork, 1u);
}

TEST(DeviceLifecycleTest, ABackendRefusingWorkFinishesItWithTheBackendsFailureAsTheCause)
{
    an::test::MockDeviceOptions options;
    options.refuseEnqueue = true;
    const DefectBench bench(options);

    const an::Result<an::DeviceEvent> work =
        submitCopy(bench.device, bench.execution, bench.host, bench.onDevice, bench.layout);
    ASSERT_TRUE(work.hasValue()) << work.status().message();
    const an::Status outcome = work->wait();
    EXPECT_TRUE(refusedFor(outcome, an::ErrorCode::Busy, an::DeviceFailure::BackendFailure)) << outcome.message();
    ASSERT_TRUE(outcome.hasCause());
    EXPECT_EQ(outcome.cause().code(), an::ErrorCode::Busy);

    const std::array afterWork{*work};
    const an::Result<an::DeviceEvent> dependent =
        submitCopy(bench.device, bench.execution, bench.onDevice, bench.copy, bench.layout, afterWork);
    ASSERT_TRUE(dependent.hasValue()) << dependent.status().message();
    EXPECT_TRUE(refusedFor(dependent->wait(), an::ErrorCode::Busy, an::DeviceFailure::DependencyFailed));

    const an::DeviceCounters counters = bench.device.counters();
    EXPECT_EQ(counters.backendDefects, 0u);
    EXPECT_EQ(counters.failedWork, 2u);
}

TEST(DeviceLifecycleTest, AnExceptionThrownByTheBackendIsTranslatedAtTheBoundary)
{
    {
        an::test::MockDeviceOptions options;
        options.throwOnEnqueue = true;
        const DefectBench bench(options);
        const an::Result<an::DeviceEvent> work =
            submitCopy(bench.device, bench.execution, bench.host, bench.onDevice, bench.layout);
        ASSERT_TRUE(work.hasValue()) << work.status().message();
        const an::Status outcome = work->wait();
        EXPECT_TRUE(refusedFor(outcome, an::ErrorCode::ExternalFailure, an::DeviceFailure::BackendFailure))
            << outcome.message();
        const an::Status cause = outcome.cause();
        const an::StatusContextField* library = cause.findContext(an::ContextKey::Library);
        ASSERT_NE(library, nullptr);
        EXPECT_EQ(library->text, "mock");
    }
    {
        an::test::MockDeviceOptions options;
        options.throwOnPrepare = true;
        const DefectBench bench(options);
        const std::array<an::ImageLayout, 2u> layouts{bench.layout, bench.layout};
        const an::Result<an::PreparedDeviceKernel> refused = bench.device.prepareKernel(
            bench.execution, an::DeviceKernelRequest{.plan = an::test::mockAffinePlan(),
                                                     .bindingLayouts = layouts,
                                                     .arguments = an::deviceKernelArguments(kAffine)});
        ASSERT_FALSE(refused.hasValue());
        EXPECT_TRUE(refusedFor(refused.status(), an::ErrorCode::ExternalFailure, an::DeviceFailure::BackendFailure))
            << refused.status().message();
        const an::Status cause = refused.status().cause();
        const an::StatusContextField* library = cause.findContext(an::ContextKey::Library);
        ASSERT_NE(library, nullptr);
        EXPECT_EQ(library->text, "mock");
        EXPECT_EQ(bench.device.counters().liveKernels, 0u);
    }
}

TEST(DeviceLifecycleTest, ADeviceHandleDestroyedEarlyKeepsItsBackendUntilTheLastResourceAndWorkAreGone)
{
    const an::EngineContext engine = an::test::makeDeviceTestEngine();
    const an::ExecutionContext execution = an::test::makeDeviceTestExecution(engine);
    an::test::MockDeviceOptions options;
    options.startPaused = true;
    auto backend = std::make_shared<an::test::MockDeviceBackend>(options);
    const std::weak_ptr<an::test::MockDeviceBackend> observer = backend;
    an::Device device = an::test::makeMockDevice(backend);

    const an::ImageLayout layout = an::test::floatPlaneLayout(8u, 4u);
    an::DeviceResource host =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Host));
    an::DeviceResource onDevice =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Device));
    const an::Result<an::DeviceEvent> work = submitCopy(device, execution, host, onDevice, layout);
    ASSERT_TRUE(work.hasValue()) << work.status().message();

    backend.reset();
    device = an::Device{};
    EXPECT_FALSE(observer.expired());
    {
        const std::shared_ptr<an::test::MockDeviceBackend> held = observer.lock();
        ASSERT_NE(held, nullptr);
        held->resume();
    }
    EXPECT_TRUE(work->wait().ok());
    {
        const an::Result<an::DeviceMapping> mapping = host.map(an::DeviceAccess::Read);
        EXPECT_TRUE(mapping.hasValue()) << mapping.status().message();
    }

    host.release();
    onDevice.release();
    EXPECT_TRUE(eventually([&observer] { return observer.expired(); }));
    ASSERT_TRUE(work->outcome().has_value());
    EXPECT_TRUE(work->outcome()->ok());
}

TEST(DeviceLifecycleTest, TheLastReferenceToABackendMayBeReleasedOnTheBackendsOwnThread)
{
    const an::EngineContext engine = an::test::makeDeviceTestEngine();
    const an::ExecutionContext execution = an::test::makeDeviceTestExecution(engine);
    an::test::MockDeviceOptions options;
    options.startPaused = true;
    auto backend = std::make_shared<an::test::MockDeviceBackend>(options);
    const std::weak_ptr<an::test::MockDeviceBackend> observer = backend;
    an::Device device = an::test::makeMockDevice(backend);

    const an::ImageLayout layout = an::test::floatPlaneLayout(8u, 4u);
    an::DeviceResource host =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Host));
    an::DeviceResource onDevice =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Device));
    const an::Result<an::DeviceEvent> work = submitCopy(device, execution, host, onDevice, layout);
    ASSERT_TRUE(work.hasValue()) << work.status().message();

    host.release();
    onDevice.release();
    device = an::Device{};
    an::test::MockDeviceBackend* const queuedOn = backend.get();
    backend.reset();

    // Only the queued work keeps the device, and with it the backend, alive now. Releasing it makes the
    // worker thread that completes the work the thread that destroys the backend.
    ASSERT_FALSE(observer.expired());
    queuedOn->resume();
    EXPECT_TRUE(work->wait().ok());
    EXPECT_TRUE(eventually([&observer] { return observer.expired(); }));
}

TEST(DeviceLifecycleTest, EmptyHandlesReportNothingAndRefuseEveryUse)
{
    const an::EngineContext engine = an::test::makeDeviceTestEngine();
    const an::ExecutionContext execution = an::test::makeDeviceTestExecution(engine);
    const an::ExecutionContext noExecution;

    const an::DeviceResource resource;
    EXPECT_FALSE(resource.valid());
    EXPECT_EQ(resource.id(), 0u);
    EXPECT_EQ(resource.deviceId(), 0u);
    EXPECT_EQ(resource.descriptor(), nullptr);
    const an::Result<an::DeviceMapping> refusedMapping = resource.map(an::DeviceAccess::Read);
    ASSERT_FALSE(refusedMapping.hasValue());
    EXPECT_TRUE(refusedFor(refusedMapping.status(), an::ErrorCode::InvalidState, an::DeviceFailure::ResourceReleased));

    an::DeviceMapping unmapped;
    EXPECT_FALSE(unmapped.valid());
    EXPECT_TRUE(unmapped.bytes().empty());
    EXPECT_TRUE(unmapped.writableBytes().empty());
    unmapped.unmap();
    EXPECT_FALSE(unmapped.valid());

    const an::DeviceEvent event;
    EXPECT_FALSE(event.valid());
    EXPECT_EQ(event.id(), 0u);
    EXPECT_EQ(event.deviceId(), 0u);
    EXPECT_EQ(event.phase(), an::DeviceWorkPhase::Finished);
    ASSERT_TRUE(event.outcome().has_value());
    EXPECT_EQ(event.outcome()->code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(event.wait().code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(event.wait(execution).code(), an::ErrorCode::NotInitialized);
    const an::DeviceWorkTiming timing = event.timing();
    EXPECT_EQ(timing.waitingNanoseconds + timing.queuedNanoseconds + timing.runningNanoseconds, 0u);

    const an::PreparedDeviceKernel kernel;
    EXPECT_FALSE(kernel.valid());
    EXPECT_EQ(kernel.kernel(), nullptr);
    EXPECT_EQ(kernel.deviceId(), 0u);

    const auto backend = std::make_shared<an::test::MockDeviceBackend>();
    const an::Device live = an::test::makeMockDevice(backend);
    const an::ImageLayout layout = an::test::floatPlaneLayout(8u, 4u);
    const an::DeviceResource host =
        an::test::makeResource(live, an::test::planeDescriptor(layout, an::ResourceResidency::Host));
    const an::DeviceResource onDevice =
        an::test::makeResource(live, an::test::planeDescriptor(layout, an::ResourceResidency::Device));
    const an::Result<an::DeviceEvent> finished = submitCopy(live, execution, host, onDevice, layout);
    ASSERT_TRUE(finished.hasValue()) << finished.status().message();
    ASSERT_TRUE(finished->wait().ok());
    EXPECT_EQ(finished->wait(noExecution).code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(submitCopy(live, noExecution, host, onDevice, layout).code(), an::ErrorCode::NotInitialized);

    an::Device device;
    EXPECT_FALSE(device.valid());
    EXPECT_EQ(device.id(), 0u);
    EXPECT_EQ(device.capabilities(), nullptr);
    const an::Result<an::DeviceResource> refusedResource = device.createResource({.bytes = 64u});
    EXPECT_TRUE(refusedFor(refusedResource.status(), an::ErrorCode::NotInitialized, an::DeviceFailure::DeviceShutDown));
    EXPECT_EQ(device.selectKernel(execution, an::test::mockAffinePlan()).code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(device.prepareKernel(execution, an::DeviceKernelRequest{.plan = an::test::mockAffinePlan()}).code(),
              an::ErrorCode::NotInitialized);
    const an::Result<an::DeviceEvent> refusedSubmission = submitCopy(device, execution, host, onDevice, layout);
    EXPECT_TRUE(
        refusedFor(refusedSubmission.status(), an::ErrorCode::NotInitialized, an::DeviceFailure::DeviceShutDown));
    const an::DeviceCounters counters = device.counters();
    EXPECT_EQ(counters.acceptedWork + counters.refusedSubmissions + counters.liveResources, 0u);
    EXPECT_TRUE(device.shutdown().ok());

    an::DeviceWork work;
    EXPECT_FALSE(work.valid());
    EXPECT_EQ(work.eventId(), 0u);
    EXPECT_TRUE(work.transfersBefore().empty());
    EXPECT_TRUE(work.transfersAfter().empty());
    EXPECT_TRUE(work.bindings().empty());
    EXPECT_EQ(work.kernel(), nullptr);
    EXPECT_FALSE(work.scratch().has_value());
    EXPECT_EQ(work.checkCancellation().code(), an::ErrorCode::NotInitialized);
    work.markRunning();
    work.recordTransfer(1u);
    work.recordKernel(1u);
    work.complete(an::Status::success());
    EXPECT_FALSE(work.valid());
}

TEST(DeviceLifecycleTest, ReasonsResidenciesAccessesPhasesAndDeterminismHaveStableNamesAndReadBack)
{
    std::set<std::string> reasons;
    for (std::size_t index = 0u; index < an::kDeviceFailureCount; ++index)
    {
        const auto reason = static_cast<an::DeviceFailure>(index);
        const std::string name = an::deviceFailureName(reason);
        EXPECT_NE(name, "UnknownDeviceFailure") << index;
        EXPECT_FALSE(std::string(an::deviceFailureDescription(reason)).empty()) << name;
        reasons.insert(name);
    }
    EXPECT_EQ(reasons.size(), an::kDeviceFailureCount);
    EXPECT_STREQ(an::deviceFailureName(static_cast<an::DeviceFailure>(200)), "UnknownDeviceFailure");
    EXPECT_FALSE(std::string(an::deviceFailureDescription(static_cast<an::DeviceFailure>(200))).empty());

    const an::Status plain = an::deviceFailure(an::ErrorCode::Busy, an::DeviceFailure::PendingUse);
    EXPECT_TRUE(refusedFor(plain, an::ErrorCode::Busy, an::DeviceFailure::PendingUse));
    EXPECT_EQ(plain.domain(), an::ErrorDomain::Runtime);
    EXPECT_STREQ(plain.message(), an::deviceFailureDescription(an::DeviceFailure::PendingUse));

    const an::Status detailed =
        an::deviceFailure(an::ErrorCode::Internal, an::DeviceFailure::WorkAbandoned,
                          an::StatusDetail{.message = "a sentence",
                                           .operation = "DeviceLifecycleTest",
                                           .domain = an::ErrorDomain::Io,
                                           .context = {an::StatusContextField::ofCount(an::ContextKey::Requested, 5u)}},
                          an::Status::failure(an::ErrorCode::Cancelled));
    EXPECT_TRUE(refusedFor(detailed, an::ErrorCode::Internal, an::DeviceFailure::WorkAbandoned));
    EXPECT_EQ(detailed.domain(), an::ErrorDomain::Runtime);
    EXPECT_STREQ(detailed.message(), "a sentence");
    EXPECT_STREQ(detailed.operation(), "DeviceLifecycleTest");
    const an::StatusContextField* requested = detailed.findContext(an::ContextKey::Requested);
    ASSERT_NE(requested, nullptr);
    EXPECT_EQ(requested->count, 5u);
    ASSERT_TRUE(detailed.hasCause());
    EXPECT_EQ(detailed.cause().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(an::deviceFailure(an::ErrorCode::Ok, an::DeviceFailure::PendingUse).code(), an::ErrorCode::Internal);

    EXPECT_FALSE(an::deviceFailureReason(an::Status::success()).has_value());
    EXPECT_FALSE(an::deviceFailureReason(an::Status::failure(an::ErrorCode::Busy)).has_value());
    const an::Status elsewhere = an::Status::failure(
        an::ErrorCode::Busy,
        an::StatusDetail{.domain = an::ErrorDomain::Io,
                         .context = {an::StatusContextField::ofCount(an::ContextKey::DeviceReason, 3u)}});
    EXPECT_FALSE(an::deviceFailureReason(elsewhere).has_value());
    const an::Status unknown = an::Status::failure(
        an::ErrorCode::Busy,
        an::StatusDetail{.domain = an::ErrorDomain::Runtime,
                         .context = {an::StatusContextField::ofCount(an::ContextKey::DeviceReason, 200u)}});
    EXPECT_FALSE(an::deviceFailureReason(unknown).has_value());

    EXPECT_STREQ(an::resourceResidencyName(an::ResourceResidency::Host), "Host");
    EXPECT_STREQ(an::resourceResidencyName(an::ResourceResidency::Pinned), "Pinned");
    EXPECT_STREQ(an::resourceResidencyName(an::ResourceResidency::Shared), "Shared");
    EXPECT_STREQ(an::resourceResidencyName(an::ResourceResidency::Device), "Device");
    EXPECT_STREQ(an::resourceResidencyName(static_cast<an::ResourceResidency>(9)), "UnknownResourceResidency");
    EXPECT_STREQ(an::deviceAccessName(an::DeviceAccess::Read), "Read");
    EXPECT_STREQ(an::deviceAccessName(an::DeviceAccess::Write), "Write");
    EXPECT_STREQ(an::deviceAccessName(an::DeviceAccess::ReadWrite), "ReadWrite");
    EXPECT_STREQ(an::deviceAccessName(static_cast<an::DeviceAccess>(9)), "UnknownDeviceAccess");
    EXPECT_STREQ(an::deviceWorkPhaseName(an::DeviceWorkPhase::Waiting), "Waiting");
    EXPECT_STREQ(an::deviceWorkPhaseName(an::DeviceWorkPhase::Queued), "Queued");
    EXPECT_STREQ(an::deviceWorkPhaseName(an::DeviceWorkPhase::Running), "Running");
    EXPECT_STREQ(an::deviceWorkPhaseName(an::DeviceWorkPhase::Finished), "Finished");
    EXPECT_STREQ(an::deviceWorkPhaseName(static_cast<an::DeviceWorkPhase>(9)), "UnknownDeviceWorkPhase");
    EXPECT_STREQ(an::kernelDeterminismName(an::KernelDeterminism::Reproducible), "Reproducible");
    EXPECT_STREQ(an::kernelDeterminismName(an::KernelDeterminism::WithinAccuracy), "WithinAccuracy");
    EXPECT_STREQ(an::kernelDeterminismName(static_cast<an::KernelDeterminism>(9)), "UnknownKernelDeterminism");

    static_assert(an::kResourceResidencyCount == 4u);
    static_assert(an::isHostAddressable(an::ResourceResidency::Host) &&
                  an::isHostAddressable(an::ResourceResidency::Pinned));
    static_assert(an::isHostAddressable(an::ResourceResidency::Shared) &&
                  !an::isHostAddressable(an::ResourceResidency::Device));
    static_assert(an::isDeviceReachable(an::ResourceResidency::Shared) &&
                  an::isDeviceReachable(an::ResourceResidency::Device));
    static_assert(!an::isDeviceReachable(an::ResourceResidency::Host) &&
                  !an::isDeviceReachable(an::ResourceResidency::Pinned));
    static_assert(an::accessReads(an::DeviceAccess::ReadWrite) && !an::accessReads(an::DeviceAccess::Write));
    static_assert(an::accessWrites(an::DeviceAccess::ReadWrite) && !an::accessWrites(an::DeviceAccess::Read));
}

} // namespace
