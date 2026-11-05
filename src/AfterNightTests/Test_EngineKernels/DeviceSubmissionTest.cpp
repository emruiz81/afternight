// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the rules Algorithms/Kernel/Device.h enforces before and while work runs.
//
// These cases need work held at a known point - queued, waiting for a dependency, mapped - so they run
// against a mock that can be paused and told to fail. Every rule here is one no backend has to
// remember, because the device applies it before the backend is asked: what work may bind, which
// transfers are in range, which dependencies must be declared, when a mapping is refused, and what a
// stop does to work that has not started.

#include "MockDeviceBackend.h"

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
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace
{

constexpr an::AffineKernelParameters kAffine{.scale = 0.5, .bias = -2.0};

[[nodiscard]] bool refusedFor(const an::Status& status, an::ErrorCode code, an::DeviceFailure reason)
{
    const std::optional<an::DeviceFailure> carried = an::deviceFailureReason(status);
    return status.code() == code && carried.has_value() && *carried == reason;
}

[[nodiscard]] an::test::MockDeviceOptions pausedOptions()
{
    an::test::MockDeviceOptions options;
    options.startPaused = true;
    return options;
}

/// A device over a mock, an execution to submit under, and a small plane layout.
struct DeviceBench
{
    explicit DeviceBench(const an::test::MockDeviceOptions& options = {})
        : engine(an::test::makeDeviceTestEngine()), execution(an::test::makeDeviceTestExecution(engine)),
          backend(std::make_shared<an::test::MockDeviceBackend>(options)), device(an::test::makeMockDevice(backend)),
          layout(an::test::floatPlaneLayout(8u, 4u))
    {
    }

    [[nodiscard]] an::DeviceResource resource(an::ResourceResidency residency) const
    {
        return an::test::makeResource(device, an::test::planeDescriptor(layout, residency));
    }

    [[nodiscard]] an::Result<an::DeviceEvent> transfer(const an::DeviceResource& source,
                                                       const an::DeviceResource& destination,
                                                       std::span<const an::DeviceEvent> dependencies = {}) const
    {
        const std::array steps{
            an::DeviceTransfer{.source = &source, .destination = &destination, .bytes = layout.totalBytes()}};
        return device.submit(execution, an::DeviceSubmission{.transfersBefore = steps, .dependencies = dependencies});
    }

    an::EngineContext engine;
    an::ExecutionContext execution;
    std::shared_ptr<an::test::MockDeviceBackend> backend;
    an::Device device;
    an::ImageLayout layout;
};

TEST(DeviceSubmissionTest, EveryMalformedSubmissionIsRefusedBeforeAnythingIsAccepted)
{
    an::test::MockDeviceOptions options;
    options.maximumTransfers = 2u;
    options.maximumDependencies = 1u;
    const DeviceBench bench(options);
    const std::uint64_t bytes = bench.layout.totalBytes();

    const an::DeviceResource host = bench.resource(an::ResourceResidency::Host);
    const an::DeviceResource input = bench.resource(an::ResourceResidency::Device);
    const an::DeviceResource output = bench.resource(an::ResourceResidency::Device);
    const an::DeviceResource tiny = an::test::makeResource(
        bench.device,
        an::DeviceResourceDescriptor{.bytes = 4u, .alignment = 64u, .residency = an::ResourceResidency::Device});
    const an::ImageLayout otherLayout = an::test::floatPlaneLayout(4u, 8u);
    ASSERT_EQ(otherLayout.totalBytes(), bytes);
    const an::DeviceResource reshaped =
        an::test::makeResource(bench.device, an::test::planeDescriptor(otherLayout, an::ResourceResidency::Device));
    const an::DeviceResource empty;
    const an::PreparedDeviceKernel kernel =
        an::test::prepareAffine(bench.device, bench.execution, bench.layout, bench.layout, kAffine);

    const auto otherBackend = std::make_shared<an::test::MockDeviceBackend>();
    const an::Device other = an::test::makeMockDevice(otherBackend);
    const an::DeviceResource foreign =
        an::test::makeResource(other, an::test::planeDescriptor(bench.layout, an::ResourceResidency::Device));
    const an::PreparedDeviceKernel foreignKernel =
        an::test::prepareAffine(other, bench.execution, bench.layout, bench.layout, kAffine);

    std::uint64_t refusals = 0u;
    const auto expectRefused = [&](const an::DeviceSubmission& submission, an::ErrorCode code, an::DeviceFailure reason,
                                   const char* what) {
        ++refusals;
        const an::Result<an::DeviceEvent> refused = bench.device.submit(bench.execution, submission);
        ASSERT_FALSE(refused.hasValue()) << what;
        EXPECT_TRUE(refusedFor(refused.status(), code, reason))
            << what << ": " << an::errorCodeName(refused.code()) << " - " << refused.status().message();
    };
    const auto bind = [](const an::DeviceResource& in, an::DeviceAccess inAccess, const an::DeviceResource& out,
                         an::DeviceAccess outAccess) {
        return std::array{an::DeviceBinding{.resource = &in, .access = inAccess},
                          an::DeviceBinding{.resource = &out, .access = outAccess}};
    };
    const auto read = an::DeviceAccess::Read;
    const auto write = an::DeviceAccess::Write;

    expectRefused(an::DeviceSubmission{}, an::ErrorCode::InvalidArgument, an::DeviceFailure::EmptySubmission,
                  "a submission with nothing to do");

    const auto correct = bind(input, read, output, write);
    expectRefused({.bindings = correct}, an::ErrorCode::InvalidArgument, an::DeviceFailure::BindingMismatch,
                  "bindings with no kernel to bind them to");
    const std::array oneBinding{an::DeviceBinding{.resource = &input, .access = read}};
    expectRefused({.kernel = &kernel, .bindings = oneBinding}, an::ErrorCode::InvalidArgument,
                  an::DeviceFailure::BindingMismatch, "fewer bindings than the kernel declares");
    const auto wrongAccess = bind(input, write, output, write);
    expectRefused({.kernel = &kernel, .bindings = wrongAccess}, an::ErrorCode::InvalidArgument,
                  an::DeviceFailure::BindingMismatch, "an access the kernel does not use");
    const auto tooSmall = bind(tiny, read, output, write);
    expectRefused({.kernel = &kernel, .bindings = tooSmall}, an::ErrorCode::InvalidArgument,
                  an::DeviceFailure::BindingMismatch, "a resource smaller than the plane the kernel reads");
    const auto wrongLayout = bind(reshaped, read, output, write);
    expectRefused({.kernel = &kernel, .bindings = wrongLayout}, an::ErrorCode::InvalidArgument,
                  an::DeviceFailure::BindingMismatch, "a resource laid out differently from the kernel's plane");
    const auto aliased = bind(input, read, input, write);
    expectRefused({.kernel = &kernel, .bindings = aliased}, an::ErrorCode::Aliasing, an::DeviceFailure::BindingAliased,
                  "an input that is also the output, under a plan that forbids overlap");
    const auto hostBound = bind(host, read, output, write);
    expectRefused({.kernel = &kernel, .bindings = hostBound}, an::ErrorCode::IncompatibleArguments,
                  an::DeviceFailure::NotDeviceReachable, "host memory bound to a kernel");
    const auto foreignBound = bind(foreign, read, output, write);
    expectRefused({.kernel = &kernel, .bindings = foreignBound}, an::ErrorCode::IncompatibleArguments,
                  an::DeviceFailure::ForeignDevice, "another device's resource bound to a kernel");
    expectRefused({.kernel = &foreignKernel, .bindings = correct}, an::ErrorCode::IncompatibleArguments,
                  an::DeviceFailure::ForeignDevice, "another device's kernel");
    const auto emptyBound = bind(empty, read, output, write);
    expectRefused({.kernel = &kernel, .bindings = emptyBound}, an::ErrorCode::InvalidState,
                  an::DeviceFailure::ResourceReleased, "a resource handle that holds nothing");

    const std::array nullSource{an::DeviceTransfer{.source = nullptr, .destination = &input, .bytes = bytes}};
    expectRefused({.transfersBefore = nullSource}, an::ErrorCode::InvalidState, an::DeviceFailure::ResourceReleased,
                  "a transfer from nothing");
    const std::array foreignSource{an::DeviceTransfer{.source = &foreign, .destination = &input, .bytes = bytes}};
    expectRefused({.transfersBefore = foreignSource}, an::ErrorCode::IncompatibleArguments,
                  an::DeviceFailure::ForeignDevice, "a transfer from another device's resource");
    const std::array noBytes{an::DeviceTransfer{.source = &host, .destination = &input, .bytes = 0u}};
    expectRefused({.transfersBefore = noBytes}, an::ErrorCode::OutOfRange, an::DeviceFailure::TransferOutOfRange,
                  "a transfer of no bytes");
    const std::array pastEnd{
        an::DeviceTransfer{.source = &host, .destination = &input, .destinationOffset = 4u, .bytes = bytes}};
    expectRefused({.transfersBefore = pastEnd}, an::ErrorCode::OutOfRange, an::DeviceFailure::TransferOutOfRange,
                  "a transfer that runs past the end of its destination");
    const std::array wrapping{an::DeviceTransfer{.source = &host,
                                                 .sourceOffset = (std::numeric_limits<std::uint64_t>::max)() - 1u,
                                                 .destination = &input,
                                                 .bytes = 4u}};
    expectRefused({.transfersAfter = wrapping}, an::ErrorCode::OutOfRange, an::DeviceFailure::TransferOutOfRange,
                  "a source offset whose end wraps");
    const std::array overlapping{
        an::DeviceTransfer{.source = &input, .destination = &input, .destinationOffset = 4u, .bytes = 8u}};
    expectRefused({.transfersBefore = overlapping}, an::ErrorCode::Aliasing, an::DeviceFailure::TransferOverlap,
                  "a transfer onto its own bytes");
    const an::DeviceTransfer small{.source = &host, .destination = &input, .bytes = 4u};
    const std::array three{small, small, small};
    expectRefused({.transfersBefore = three}, an::ErrorCode::LimitExceeded, an::DeviceFailure::SubmissionTooLarge,
                  "more transfers than the device accepts");
    const std::array once{an::DeviceTransfer{.source = &host, .destination = &input, .bytes = bytes}};
    const std::array twoDependencies{an::DeviceEvent{}, an::DeviceEvent{}};
    expectRefused({.transfersBefore = once, .dependencies = twoDependencies}, an::ErrorCode::LimitExceeded,
                  an::DeviceFailure::SubmissionTooLarge, "more dependencies than the device accepts");
    const std::array emptyDependency{an::DeviceEvent{}};
    expectRefused({.transfersBefore = once, .dependencies = emptyDependency}, an::ErrorCode::InvalidArgument,
                  an::DeviceFailure::InvalidDependency, "a dependency that observes nothing");

    std::stop_source stop;
    an::ExecutionOptions stopping;
    stopping.stopToken = stop.get_token();
    const an::ExecutionContext stopped = an::test::makeDeviceTestExecution(bench.engine, stopping);
    stop.request_stop();
    const an::Result<an::DeviceEvent> cancelled = bench.device.submit(stopped, {.transfersBefore = once});
    ++refusals;
    ASSERT_FALSE(cancelled.hasValue());
    EXPECT_EQ(cancelled.code(), an::ErrorCode::Cancelled);

    const an::DeviceCounters counters = bench.device.counters();
    EXPECT_EQ(counters.acceptedWork, 0u);
    EXPECT_EQ(counters.refusedSubmissions, refusals);
    EXPECT_EQ(counters.uploads.transfers, 0u);

    // Nothing refused left a trace on the resources it named, so work that reads them needs no dependency.
    const an::Result<an::DeviceEvent> reads =
        bench.device.submit(bench.execution, {.kernel = &kernel, .bindings = correct});
    ASSERT_TRUE(reads.hasValue()) << reads.status().message();
    EXPECT_TRUE(reads->wait().ok());

    // Exact in-place work is legal under a plan that says so, as is a copy between two disjoint ranges
    // of one resource.
    an::KernelPlanKey inPlacePlan = an::test::mockAffinePlan();
    inPlacePlan.alias = an::KernelAliasPolicy::ExactInPlace;
    const an::PreparedDeviceKernel inPlace =
        an::test::prepareAffine(bench.device, bench.execution, bench.layout, bench.layout, kAffine, inPlacePlan);
    const an::Result<an::DeviceEvent> inPlaceWork =
        bench.device.submit(bench.execution, {.kernel = &inPlace, .bindings = aliased});
    ASSERT_TRUE(inPlaceWork.hasValue()) << inPlaceWork.status().message();
    EXPECT_TRUE(inPlaceWork->wait().ok());
    const std::array disjoint{
        an::DeviceTransfer{.source = &input, .destination = &input, .destinationOffset = 16u, .bytes = 16u}};
    const an::Result<an::DeviceEvent> selfCopy = bench.device.submit(bench.execution, {.transfersBefore = disjoint});
    ASSERT_TRUE(selfCopy.hasValue()) << selfCopy.status().message();
    EXPECT_TRUE(selfCopy->wait().ok());
    EXPECT_EQ(bench.device.counters().deviceCopies.transfers, 1u);
}

TEST(DeviceSubmissionTest, WorkWaitsUntilEveryDependencyItDeclaredHasSucceeded)
{
    const DeviceBench slow(pausedOptions());
    const DeviceBench fast;
    const an::DeviceResource slowHost = slow.resource(an::ResourceResidency::Host);
    const an::DeviceResource slowDevice = slow.resource(an::ResourceResidency::Device);
    const an::DeviceResource fastSource = fast.resource(an::ResourceResidency::Host);
    const an::DeviceResource fastCopy = fast.resource(an::ResourceResidency::Host);

    const an::Result<an::DeviceEvent> first = slow.transfer(slowHost, slowDevice);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    // A dependency may belong to another device: the second device waits for the first one's work.
    const std::array dependencies{*first};
    const an::Result<an::DeviceEvent> second = fast.transfer(fastSource, fastCopy, dependencies);
    ASSERT_TRUE(second.hasValue()) << second.status().message();

    EXPECT_EQ(first->phase(), an::DeviceWorkPhase::Queued);
    EXPECT_EQ(second->phase(), an::DeviceWorkPhase::Waiting);
    EXPECT_FALSE(second->outcome().has_value());
    EXPECT_TRUE(fast.backend->waitUntilIdle(std::chrono::milliseconds(200)));
    EXPECT_EQ(fast.backend->startedWork(), 0u);

    slow.backend->resume();
    const an::Status outcome = second->wait();
    EXPECT_TRUE(outcome.ok()) << outcome.message();
    ASSERT_TRUE(first->outcome().has_value());
    EXPECT_TRUE(first->outcome()->ok());
    EXPECT_EQ(fast.backend->startedWork(), 1u);
    EXPECT_GT(second->timing().waitingNanoseconds, 0u);
}

TEST(DeviceSubmissionTest, ReadingWhatPendingWorkWritesRequiresDependingOnThatWork)
{
    const DeviceBench bench(pausedOptions());
    const an::DeviceResource host = bench.resource(an::ResourceResidency::Host);
    const an::DeviceResource onDevice = bench.resource(an::ResourceResidency::Device);
    const an::DeviceResource firstCopy = bench.resource(an::ResourceResidency::Host);
    const an::DeviceResource secondCopy = bench.resource(an::ResourceResidency::Host);
    const std::vector<float> samples = an::test::patternSamples(8u, 4u, 21u);
    an::test::writeFloatPlane(host, bench.layout, samples);

    const an::Result<an::DeviceEvent> upload = bench.transfer(host, onDevice);
    ASSERT_TRUE(upload.hasValue()) << upload.status().message();

    const an::Result<an::DeviceEvent> undeclared = bench.transfer(onDevice, firstCopy);
    ASSERT_FALSE(undeclared.hasValue());
    EXPECT_TRUE(refusedFor(undeclared.status(), an::ErrorCode::InvalidState, an::DeviceFailure::UndeclaredDependency))
        << undeclared.status().message();

    const std::array afterUpload{*upload};
    const an::Result<an::DeviceEvent> download = bench.transfer(onDevice, firstCopy, afterUpload);
    ASSERT_TRUE(download.hasValue()) << download.status().message();

    // Depending on work that itself depends on the writer declares the writer too.
    const std::array afterDownload{*download};
    const an::Result<an::DeviceEvent> secondDownload = bench.transfer(onDevice, secondCopy, afterDownload);
    ASSERT_TRUE(secondDownload.hasValue()) << secondDownload.status().message();

    bench.backend->resume();
    ASSERT_TRUE(secondDownload->wait().ok());
    ASSERT_TRUE(download->wait().ok());
    EXPECT_TRUE(an::test::sameBits(an::test::readFloatPlane(firstCopy, bench.layout), samples));
    EXPECT_TRUE(an::test::sameBits(an::test::readFloatPlane(secondCopy, bench.layout), samples));
}

TEST(DeviceSubmissionTest, WritingWhatPendingWorkReadsRequiresDependingOnThatWork)
{
    const DeviceBench bench(pausedOptions());
    const an::DeviceResource host = bench.resource(an::ResourceResidency::Host);
    const an::DeviceResource onDevice = bench.resource(an::ResourceResidency::Device);
    const an::DeviceResource copy = bench.resource(an::ResourceResidency::Host);

    const an::Result<an::DeviceEvent> download = bench.transfer(onDevice, copy);
    ASSERT_TRUE(download.hasValue()) << download.status().message();

    const an::Result<an::DeviceEvent> undeclared = bench.transfer(host, onDevice);
    ASSERT_FALSE(undeclared.hasValue());
    EXPECT_TRUE(refusedFor(undeclared.status(), an::ErrorCode::InvalidState, an::DeviceFailure::UndeclaredDependency))
        << undeclared.status().message();

    const std::array afterDownload{*download};
    const an::Result<an::DeviceEvent> upload = bench.transfer(host, onDevice, afterDownload);
    ASSERT_TRUE(upload.hasValue()) << upload.status().message();

    // Once that writer is pending, a reader has to wait for it in turn.
    const an::Result<an::DeviceEvent> lateRead = bench.transfer(onDevice, copy);
    ASSERT_FALSE(lateRead.hasValue());
    EXPECT_TRUE(refusedFor(lateRead.status(), an::ErrorCode::InvalidState, an::DeviceFailure::UndeclaredDependency));

    bench.backend->resume();
    EXPECT_TRUE(upload->wait().ok());
    EXPECT_TRUE(download->wait().ok());
}

TEST(DeviceSubmissionTest, AMappingIsRefusedWhileWorkUsesTheResourceAndHoldsWorkOffWhileItLasts)
{
    const DeviceBench bench(pausedOptions());
    const an::DeviceResource host = bench.resource(an::ResourceResidency::Host);
    const an::DeviceResource shared = bench.resource(an::ResourceResidency::Shared);
    const std::vector<float> samples = an::test::patternSamples(8u, 4u, 9u);
    an::test::writeFloatPlane(host, bench.layout, samples);

    const an::Result<an::DeviceEvent> upload = bench.transfer(host, shared);
    ASSERT_TRUE(upload.hasValue()) << upload.status().message();

    const an::Result<an::DeviceMapping> whileWritten = shared.map(an::DeviceAccess::Read);
    ASSERT_FALSE(whileWritten.hasValue());
    EXPECT_TRUE(refusedFor(whileWritten.status(), an::ErrorCode::Busy, an::DeviceFailure::PendingUse))
        << whileWritten.status().message();
    {
        // Work that only reads a resource does not stop the host reading it too.
        const an::Result<an::DeviceMapping> reader = host.map(an::DeviceAccess::Read);
        EXPECT_TRUE(reader.hasValue()) << reader.status().message();
    }
    const an::Result<an::DeviceMapping> writer = host.map(an::DeviceAccess::Write);
    ASSERT_FALSE(writer.hasValue());
    EXPECT_TRUE(refusedFor(writer.status(), an::ErrorCode::Busy, an::DeviceFailure::PendingUse));

    bench.backend->resume();
    ASSERT_TRUE(upload->wait().ok());

    an::Result<an::DeviceMapping> mapping = shared.map(an::DeviceAccess::Read);
    ASSERT_TRUE(mapping.hasValue()) << mapping.status().message();
    EXPECT_EQ(bench.device.counters().liveMappings, 1u);

    const an::Result<an::DeviceEvent> whileMapped = bench.transfer(shared, host);
    ASSERT_FALSE(whileMapped.hasValue());
    EXPECT_TRUE(refusedFor(whileMapped.status(), an::ErrorCode::Busy, an::DeviceFailure::ResourceMapped))
        << whileMapped.status().message();
    const an::Result<an::DeviceMapping> second = shared.map(an::DeviceAccess::Read);
    ASSERT_FALSE(second.hasValue());
    EXPECT_TRUE(refusedFor(second.status(), an::ErrorCode::Busy, an::DeviceFailure::ResourceMapped));

    mapping->unmap();
    EXPECT_FALSE(mapping->valid());
    EXPECT_EQ(bench.device.counters().liveMappings, 0u);
    const an::Result<an::DeviceEvent> afterUnmap = bench.transfer(shared, host);
    ASSERT_TRUE(afterUnmap.hasValue()) << afterUnmap.status().message();
    EXPECT_TRUE(afterUnmap->wait().ok());
    EXPECT_TRUE(an::test::sameBits(an::test::readFloatPlane(shared, bench.layout), samples));
}

TEST(DeviceSubmissionTest, AFailedDependencyIsTheCauseOfTheFailureOfTheWorkThatNeededIt)
{
    an::test::MockDeviceOptions options;
    options.failWorkNumber = 0u;
    const DeviceBench bench(options);
    const an::DeviceResource host = bench.resource(an::ResourceResidency::Host);
    const an::DeviceResource onDevice = bench.resource(an::ResourceResidency::Device);
    const an::DeviceResource copy = bench.resource(an::ResourceResidency::Host);

    const an::Result<an::DeviceEvent> upload = bench.transfer(host, onDevice);
    ASSERT_TRUE(upload.hasValue()) << upload.status().message();
    const std::array afterUpload{*upload};
    const an::Result<an::DeviceEvent> download = bench.transfer(onDevice, copy, afterUpload);
    ASSERT_TRUE(download.hasValue()) << download.status().message();

    const an::Status failure = download->wait();
    EXPECT_TRUE(refusedFor(failure, an::ErrorCode::ExternalFailure, an::DeviceFailure::DependencyFailed))
        << failure.message();
    ASSERT_TRUE(failure.hasCause());
    EXPECT_EQ(failure.cause().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(upload->wait().code(), an::ErrorCode::ExternalFailure);
    EXPECT_TRUE(bench.backend->waitUntilIdle(std::chrono::seconds(5)));
    EXPECT_EQ(bench.backend->startedWork(), 1u);

    const an::DeviceCounters counters = bench.device.counters();
    EXPECT_EQ(counters.failedWork, 2u);
    EXPECT_EQ(counters.succeededWork, 0u);
    EXPECT_EQ(counters.uploads.transfers, 0u);
    EXPECT_EQ(counters.downloads.transfers, 0u);
    EXPECT_EQ(counters.pendingWork, 0u);
}

TEST(DeviceSubmissionTest, StoppingWorkThatWaitsForADependencyFinishesItAtOnce)
{
    const DeviceBench bench(pausedOptions());
    const an::DeviceResource host = bench.resource(an::ResourceResidency::Host);
    const an::DeviceResource onDevice = bench.resource(an::ResourceResidency::Device);
    const an::DeviceResource copy = bench.resource(an::ResourceResidency::Host);
    const an::Result<an::DeviceEvent> upload = bench.transfer(host, onDevice);
    ASSERT_TRUE(upload.hasValue()) << upload.status().message();

    std::stop_source stop;
    an::ExecutionOptions stoppable;
    stoppable.stopToken = stop.get_token();
    const an::ExecutionContext execution = an::test::makeDeviceTestExecution(bench.engine, stoppable);
    const std::array afterUpload{*upload};
    const std::array steps{
        an::DeviceTransfer{.source = &onDevice, .destination = &copy, .bytes = bench.layout.totalBytes()}};
    const an::Result<an::DeviceEvent> download =
        bench.device.submit(execution, {.transfersBefore = steps, .dependencies = afterUpload});
    ASSERT_TRUE(download.hasValue()) << download.status().message();
    EXPECT_EQ(download->phase(), an::DeviceWorkPhase::Waiting);

    stop.request_stop();
    EXPECT_EQ(download->phase(), an::DeviceWorkPhase::Finished);
    ASSERT_TRUE(download->outcome().has_value());
    EXPECT_EQ(download->outcome()->code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(upload->phase(), an::DeviceWorkPhase::Queued);
    const an::DeviceCounters whileHeld = bench.device.counters();
    EXPECT_EQ(whileHeld.pendingWork, 1u);
    EXPECT_EQ(whileHeld.cancelledWork, 1u);

    bench.backend->resume();
    EXPECT_TRUE(upload->wait().ok());
    EXPECT_EQ(bench.backend->startedWork(), 1u);
}

TEST(DeviceSubmissionTest, StoppingWorkTheBackendHasNotStartedIsObservedWhenItStarts)
{
    const DeviceBench bench(pausedOptions());
    const an::DeviceResource host = bench.resource(an::ResourceResidency::Host);
    const an::DeviceResource onDevice = bench.resource(an::ResourceResidency::Device);

    std::stop_source stop;
    an::ExecutionOptions stoppable;
    stoppable.stopToken = stop.get_token();
    const an::ExecutionContext execution = an::test::makeDeviceTestExecution(bench.engine, stoppable);
    const std::array steps{
        an::DeviceTransfer{.source = &host, .destination = &onDevice, .bytes = bench.layout.totalBytes()}};
    const an::Result<an::DeviceEvent> work = bench.device.submit(execution, {.transfersBefore = steps});
    ASSERT_TRUE(work.hasValue()) << work.status().message();
    EXPECT_EQ(work->phase(), an::DeviceWorkPhase::Queued);

    stop.request_stop();
    // The backend holds the work, so the stop is the backend's to observe at its first checkpoint.
    EXPECT_EQ(work->phase(), an::DeviceWorkPhase::Queued);

    bench.backend->resume();
    EXPECT_EQ(work->wait().code(), an::ErrorCode::Cancelled);
    const an::DeviceCounters counters = bench.device.counters();
    EXPECT_EQ(counters.uploads.transfers, 0u);
    EXPECT_EQ(counters.cancelledWork, 1u);
}

TEST(DeviceSubmissionTest, AWaitStoppedByTheWaitingOperationLeavesTheWorkAlone)
{
    const DeviceBench bench(pausedOptions());
    const an::DeviceResource host = bench.resource(an::ResourceResidency::Host);
    const an::DeviceResource onDevice = bench.resource(an::ResourceResidency::Device);
    const an::Result<an::DeviceEvent> work = bench.transfer(host, onDevice);
    ASSERT_TRUE(work.hasValue()) << work.status().message();

    std::stop_source stop;
    stop.request_stop();
    an::ExecutionOptions stopped;
    stopped.stopToken = stop.get_token();
    const an::ExecutionContext stoppedWaiter = an::test::makeDeviceTestExecution(bench.engine, stopped);
    EXPECT_EQ(work->wait(stoppedWaiter).code(), an::ErrorCode::Cancelled);

    an::ExecutionOptions late;
    late.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    const an::ExecutionContext lateWaiter = an::test::makeDeviceTestExecution(bench.engine, late);
    EXPECT_EQ(work->wait(lateWaiter).code(), an::ErrorCode::DeadlineExceeded);

    EXPECT_FALSE(work->outcome().has_value());
    EXPECT_EQ(work->phase(), an::DeviceWorkPhase::Queued);

    bench.backend->resume();
    EXPECT_TRUE(work->wait(bench.execution).ok());
}

TEST(DeviceSubmissionTest, AResourceReleasedWhileWorkUsesItIsReturnedWhenThatWorkFinishes)
{
    const DeviceBench bench(pausedOptions());
    an::DeviceResource host = bench.resource(an::ResourceResidency::Host);
    an::DeviceResource onDevice = bench.resource(an::ResourceResidency::Device);
    const an::Result<an::DeviceEvent> work = bench.transfer(host, onDevice);
    ASSERT_TRUE(work.hasValue()) << work.status().message();

    host.release();
    onDevice.release();
    EXPECT_FALSE(host.valid());
    EXPECT_EQ(bench.device.counters().liveResources, 2u);
    EXPECT_EQ(bench.backend->liveAllocations(), 2u);

    bench.backend->resume();
    ASSERT_TRUE(work->wait().ok());
    const an::DeviceCounters counters = bench.device.counters();
    EXPECT_EQ(counters.liveResources, 0u);
    EXPECT_EQ(counters.deviceMemory.liveBytes, 0u);
    EXPECT_EQ(counters.hostMemory.liveBytes, 0u);
    EXPECT_EQ(counters.uploads.transfers, 1u);
    EXPECT_EQ(bench.backend->liveAllocations(), 0u);
}

TEST(DeviceSubmissionTest, KernelScratchIsHeldOnlyWhileItsWorkIsOutstanding)
{
    an::test::MockDeviceOptions options = pausedOptions();
    options.kernelScratchBytes = 4096u;
    const DeviceBench bench(options);
    const an::DeviceResource input = bench.resource(an::ResourceResidency::Device);
    const an::DeviceResource output = bench.resource(an::ResourceResidency::Device);
    const an::PreparedDeviceKernel kernel =
        an::test::prepareAffine(bench.device, bench.execution, bench.layout, bench.layout, kAffine);
    ASSERT_NE(kernel.kernel(), nullptr);
    EXPECT_EQ(kernel.kernel()->scratchBytes(), 4096u);

    const std::uint64_t resident = bench.device.counters().deviceMemory.liveBytes;
    EXPECT_EQ(resident, 2u * bench.layout.totalBytes());
    const std::array bindings{an::DeviceBinding{.resource = &input, .access = an::DeviceAccess::Read},
                              an::DeviceBinding{.resource = &output, .access = an::DeviceAccess::Write}};
    const an::Result<an::DeviceEvent> work =
        bench.device.submit(bench.execution, {.kernel = &kernel, .bindings = bindings});
    ASSERT_TRUE(work.hasValue()) << work.status().message();
    EXPECT_EQ(bench.device.counters().deviceMemory.liveBytes, resident + 4096u);

    bench.backend->resume();
    ASSERT_TRUE(work->wait().ok());
    const an::DeviceCounters counters = bench.device.counters();
    EXPECT_EQ(counters.deviceMemory.liveBytes, resident);
    EXPECT_GE(counters.deviceMemory.peakBytes, resident + 4096u);
}

TEST(DeviceSubmissionTest, APreparedKernelsOwnDeviceMemoryIsCountedUntilTheLastHandleNamingItIsGone)
{
    an::test::MockDeviceOptions options;
    options.kernelResidentBytes = 8192u;
    const DeviceBench bench(options);
    const an::DeviceResource input = bench.resource(an::ResourceResidency::Device);
    const an::DeviceCounters before = bench.device.counters();

    std::optional<an::PreparedDeviceKernel> kernel =
        an::test::prepareAffine(bench.device, bench.execution, bench.layout, bench.layout, kAffine);
    ASSERT_NE(kernel->kernel(), nullptr);
    EXPECT_EQ(kernel->kernel()->residentBytes(), 8192u);
    const an::DeviceCounters prepared = bench.device.counters();
    EXPECT_EQ(prepared.deviceMemory.liveBytes, before.deviceMemory.liveBytes + 8192u);
    EXPECT_EQ(prepared.deviceMemory.liveAllocations, before.deviceMemory.liveAllocations + 1u);
    EXPECT_EQ(prepared.deviceMemory.totalBytes, before.deviceMemory.totalBytes + 8192u);
    EXPECT_EQ(prepared.deviceMemory.totalAllocations, before.deviceMemory.totalAllocations + 1u);
    EXPECT_GE(prepared.deviceMemory.peakBytes, before.deviceMemory.liveBytes + 8192u);
    EXPECT_EQ(prepared.hostMemory.liveBytes, before.hostMemory.liveBytes);

    // A copy names the same kernel: its memory is counted once, and stays counted while either handle exists.
    std::optional<an::PreparedDeviceKernel> copy = *kernel;
    EXPECT_EQ(bench.device.counters().deviceMemory.liveBytes, before.deviceMemory.liveBytes + 8192u);
    kernel.reset();
    EXPECT_EQ(bench.device.counters().deviceMemory.liveBytes, before.deviceMemory.liveBytes + 8192u);
    EXPECT_EQ(bench.backend->liveKernels(), 1u);

    copy.reset();
    const an::DeviceCounters released = bench.device.counters();
    EXPECT_EQ(released.deviceMemory.liveBytes, before.deviceMemory.liveBytes);
    EXPECT_EQ(released.deviceMemory.liveAllocations, before.deviceMemory.liveAllocations);
    EXPECT_EQ(released.deviceMemory.deniedAllocations, 0u);
    EXPECT_EQ(released.liveKernels, 0u);
    EXPECT_EQ(bench.backend->liveKernels(), 0u);
}

TEST(DeviceSubmissionTest, AKernelWhoseDeviceMemoryTheLimitDoesNotAdmitIsReleasedAndItsPreparationRefused)
{
    constexpr std::uint64_t kLimit = 1024u * 1024u;
    constexpr std::uint64_t kHeld = 600u * 1024u;
    constexpr std::uint64_t kKernel = 700u * 1024u;
    an::test::MockDeviceOptions options;
    options.memoryLimitBytes = kLimit;
    options.kernelResidentBytes = kKernel;
    const DeviceBench bench(options);
    an::DeviceResource held = an::test::makeResource(bench.device, {.bytes = kHeld});
    const an::DeviceCounters before = bench.device.counters();

    const std::array layouts{bench.layout, bench.layout};
    const auto prepare = [&bench, &layouts] {
        return bench.device.prepareKernel(bench.execution,
                                          an::DeviceKernelRequest{.plan = an::test::mockAffinePlan(),
                                                                  .bindingLayouts = layouts,
                                                                  .arguments = an::deviceKernelArguments(kAffine)});
    };
    const an::Result<an::PreparedDeviceKernel> refused = prepare();
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::MemoryBudgetExceeded) << refused.status().message();
    const an::StatusContextField* requested = refused.status().findContext(an::ContextKey::Requested);
    const an::StatusContextField* available = refused.status().findContext(an::ContextKey::Available);
    const an::StatusContextField* limit = refused.status().findContext(an::ContextKey::Limit);
    ASSERT_NE(requested, nullptr);
    ASSERT_NE(available, nullptr);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(requested->count, kKernel);
    EXPECT_EQ(available->count, kLimit - kHeld);
    EXPECT_EQ(limit->count, kLimit);

    // Refused means released: the device holds exactly what it held, and the backend's kernel is gone.
    const an::DeviceCounters after = bench.device.counters();
    EXPECT_EQ(after.liveKernels, 0u);
    EXPECT_EQ(after.deviceMemory.liveBytes, before.deviceMemory.liveBytes);
    EXPECT_EQ(after.deviceMemory.liveAllocations, before.deviceMemory.liveAllocations);
    EXPECT_EQ(after.deviceMemory.totalBytes, before.deviceMemory.totalBytes);
    EXPECT_EQ(after.deviceMemory.deniedAllocations, before.deviceMemory.deniedAllocations + 1u);
    EXPECT_EQ(bench.backend->liveKernels(), 0u);

    // With room, the same kernel is admitted - and the room it takes is then what a resource is refused against.
    held.release();
    const an::Result<an::PreparedDeviceKernel> admitted = prepare();
    ASSERT_TRUE(admitted.hasValue()) << admitted.status().message();
    const an::Result<an::DeviceResource> crowdedOut = bench.device.createResource({.bytes = kHeld});
    ASSERT_FALSE(crowdedOut.hasValue());
    EXPECT_EQ(crowdedOut.code(), an::ErrorCode::MemoryBudgetExceeded) << crowdedOut.status().message();
    const an::StatusContextField* remaining = crowdedOut.status().findContext(an::ContextKey::Available);
    ASSERT_NE(remaining, nullptr);
    EXPECT_EQ(remaining->count, kLimit - kKernel);
}

TEST(DeviceSubmissionTest, AResourceIsCheckedAgainstItsDescriptionAndTheDeviceLimitsBeforeTheBackendIsAsked)
{
    an::test::MockDeviceOptions options;
    options.memoryLimitBytes = 1024u * 1024u;
    options.maximumResourceBytes = 2u * 1024u * 1024u;
    options.offerPinned = false;
    const DeviceBench bench(options);
    const std::uint64_t callsBefore = bench.backend->allocationCalls();

    const auto expectRefused = [&bench](const an::DeviceResourceDescriptor& descriptor, an::ErrorCode code,
                                        an::DeviceFailure reason, const char* what) {
        const an::Result<an::DeviceResource> refused = bench.device.createResource(descriptor);
        ASSERT_FALSE(refused.hasValue()) << what;
        EXPECT_TRUE(refusedFor(refused.status(), code, reason))
            << what << ": " << an::errorCodeName(refused.code()) << " - " << refused.status().message();
    };
    expectRefused({.bytes = 0u}, an::ErrorCode::InvalidArgument, an::DeviceFailure::InvalidDescriptor, "no bytes");
    expectRefused({.bytes = 64u, .alignment = 3u}, an::ErrorCode::InvalidArgument, an::DeviceFailure::InvalidDescriptor,
                  "an alignment that is not a power of two");
    an::DeviceResourceDescriptor shortOfLayout = an::test::planeDescriptor(bench.layout, an::ResourceResidency::Device);
    shortOfLayout.bytes -= 1u;
    expectRefused(shortOfLayout, an::ErrorCode::InvalidArgument, an::DeviceFailure::InvalidDescriptor,
                  "fewer bytes than the layout names");
    an::DeviceResourceDescriptor looselyAligned =
        an::test::planeDescriptor(bench.layout, an::ResourceResidency::Device);
    looselyAligned.alignment = 2u;
    expectRefused(looselyAligned, an::ErrorCode::InvalidArgument, an::DeviceFailure::InvalidDescriptor,
                  "an alignment narrower than one sample");
    expectRefused({.bytes = 64u, .residency = static_cast<an::ResourceResidency>(9)}, an::ErrorCode::InvalidArgument,
                  an::DeviceFailure::InvalidDescriptor, "a residency outside the enumeration");
    expectRefused({.bytes = 64u, .residency = an::ResourceResidency::Pinned}, an::ErrorCode::Unsupported,
                  an::DeviceFailure::ResidencyNotOffered, "a residency the backend does not create");
    expectRefused({.bytes = 64u, .alignment = 8192u}, an::ErrorCode::LimitExceeded, an::DeviceFailure::ResourceTooLarge,
                  "an alignment wider than the device accepts");

    const std::uint64_t oversized = 3u * 1024u * 1024u;
    const an::Result<an::DeviceResource> tooLarge = bench.device.createResource({.bytes = oversized});
    ASSERT_FALSE(tooLarge.hasValue());
    EXPECT_TRUE(refusedFor(tooLarge.status(), an::ErrorCode::LimitExceeded, an::DeviceFailure::ResourceTooLarge));
    const an::StatusContextField* tooLargeRequested = tooLarge.status().findContext(an::ContextKey::Requested);
    const an::StatusContextField* tooLargeLimit = tooLarge.status().findContext(an::ContextKey::Limit);
    ASSERT_NE(tooLargeRequested, nullptr);
    ASSERT_NE(tooLargeLimit, nullptr);
    EXPECT_EQ(tooLargeRequested->count, oversized);
    EXPECT_EQ(tooLargeLimit->count, 2u * 1024u * 1024u);
    EXPECT_EQ(bench.backend->allocationCalls(), callsBefore);

    const std::uint64_t part = 600u * 1024u;
    const an::DeviceResource first = an::test::makeResource(bench.device, {.bytes = part});
    const an::Result<an::DeviceResource> second = bench.device.createResource({.bytes = part});
    ASSERT_FALSE(second.hasValue());
    EXPECT_EQ(second.code(), an::ErrorCode::MemoryBudgetExceeded) << second.status().message();
    const an::StatusContextField* requested = second.status().findContext(an::ContextKey::Requested);
    const an::StatusContextField* available = second.status().findContext(an::ContextKey::Available);
    const an::StatusContextField* limit = second.status().findContext(an::ContextKey::Limit);
    ASSERT_NE(requested, nullptr);
    ASSERT_NE(available, nullptr);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(requested->count, part);
    EXPECT_EQ(available->count, 1024u * 1024u - part);
    EXPECT_EQ(limit->count, 1024u * 1024u);
    EXPECT_EQ(bench.device.counters().deviceMemory.deniedAllocations, 1u);

    // Host memory is not what the device's memory limit bounds.
    const an::DeviceResource host =
        an::test::makeResource(bench.device, {.bytes = 1536u * 1024u, .residency = an::ResourceResidency::Host});
    EXPECT_TRUE(host.valid());
    EXPECT_EQ(bench.backend->allocationCalls(), callsBefore + 2u);
    EXPECT_EQ(bench.device.counters().hostMemory.liveBytes, 1536u * 1024u);
    EXPECT_EQ(bench.device.counters().deviceMemory.liveBytes, part);
}

TEST(DeviceSubmissionTest, ABackendThatRefusesAnAllocationIsTheCauseAndTheReservationIsReturned)
{
    an::test::MockDeviceOptions options;
    options.failAllocationsAfter = 0u;
    const DeviceBench bench(options);

    const an::Result<an::DeviceResource> refused =
        bench.device.createResource(an::test::planeDescriptor(bench.layout, an::ResourceResidency::Device));
    ASSERT_FALSE(refused.hasValue());
    EXPECT_TRUE(refusedFor(refused.status(), an::ErrorCode::AllocationFailed, an::DeviceFailure::BackendFailure))
        << refused.status().message();
    ASSERT_TRUE(refused.status().hasCause());
    EXPECT_EQ(refused.status().cause().code(), an::ErrorCode::AllocationFailed);

    const an::DeviceCounters counters = bench.device.counters();
    EXPECT_EQ(counters.deviceMemory.liveBytes, 0u);
    EXPECT_EQ(counters.deviceMemory.deniedAllocations, 1u);
    EXPECT_EQ(counters.liveResources, 0u);
}

TEST(DeviceSubmissionTest, TimingSeparatesTheTransfersFromTheKernelAndBothFromTheWait)
{
    const DeviceBench bench;
    const an::ImageLayout layout = an::test::floatPlaneLayout(512u, 512u);
    const auto describe = [&layout](an::ResourceResidency residency) {
        return an::test::planeDescriptor(layout, residency);
    };
    const an::DeviceResource hostIn = an::test::makeResource(bench.device, describe(an::ResourceResidency::Host));
    const an::DeviceResource deviceIn = an::test::makeResource(bench.device, describe(an::ResourceResidency::Device));
    const an::DeviceResource deviceOut = an::test::makeResource(bench.device, describe(an::ResourceResidency::Device));
    const an::DeviceResource hostOut = an::test::makeResource(bench.device, describe(an::ResourceResidency::Host));
    an::test::writeFloatPlane(hostIn, layout, an::test::patternSamples(512u, 512u, 1u));
    const an::PreparedDeviceKernel kernel =
        an::test::prepareAffine(bench.device, bench.execution, layout, layout, kAffine);

    const std::uint64_t bytes = layout.totalBytes();
    const std::array before{an::DeviceTransfer{.source = &hostIn, .destination = &deviceIn, .bytes = bytes}};
    const std::array bindings{an::DeviceBinding{.resource = &deviceIn, .access = an::DeviceAccess::Read},
                              an::DeviceBinding{.resource = &deviceOut, .access = an::DeviceAccess::Write}};
    const std::array after{an::DeviceTransfer{.source = &deviceOut, .destination = &hostOut, .bytes = bytes}};
    const an::Result<an::DeviceEvent> work = bench.device.submit(
        bench.execution, {.transfersBefore = before, .kernel = &kernel, .bindings = bindings, .transfersAfter = after});
    ASSERT_TRUE(work.hasValue()) << work.status().message();
    ASSERT_TRUE(work->wait().ok());

    const an::DeviceWorkTiming timing = work->timing();
    EXPECT_EQ(timing.transfersRecorded, 2u);
    EXPECT_TRUE(timing.kernelRecorded);
    EXPECT_GT(timing.transferNanoseconds, 0u);
    EXPECT_GT(timing.kernelNanoseconds, 0u);
    EXPECT_LE(timing.transferNanoseconds + timing.kernelNanoseconds, timing.runningNanoseconds);
}

} // namespace
