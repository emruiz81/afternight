// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Lifecycle, dependency and cancellation, which only a device with a thread of its own exercises.
//
// A mock can be told to hold work at a known point; a GPU cannot, so these cases arrange the point with
// the contract itself. Work on the OpenCL device is made to wait for work on a second device - a gate the
// case opens - which is also the proof that a dependency may cross from one backend to another. And a stop
// is requested while a long sequence of transfers is running, which the backend has to observe at its
// next step.

#include "OpenClTestSupport.h"

#include <AfterNightEngineOpenClBackend/OpenClBackend.h>

#include <AfterNightEngine/Algorithms/Kernel/Device.h>
#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
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
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using namespace std::chrono_literals;

constexpr an::AffineKernelParameters kAffine{.scale = 0.5, .bias = -0.25};

/// A second device that holds every piece of work it takes until the case opens it.
///
/// It offers host memory and runs host copies, which is all the case needs of it: work on the OpenCL
/// device that depends on the gate's work then waits at a point the case chooses, and is released on the
/// thread that opens the gate.
class GateBackend final : public an::DeviceBackend
{
public:
    GateBackend() = default;

    ~GateBackend() override
    {
        for (const auto& entry : m_blocks)
        {
            ::operator delete(static_cast<void*>(entry.second), std::align_val_t{64u});
        }
    }

    GateBackend(const GateBackend&) = delete;
    GateBackend& operator=(const GateBackend&) = delete;

    [[nodiscard]] an::DeviceCapabilities capabilities() const override
    {
        an::DeviceCapabilities capabilities;
        capabilities.name = "gate";
        capabilities.hostResidency = true;
        capabilities.deviceResidency = true;
        capabilities.asynchronous = true;
        capabilities.memoryLimitBytes = 1u << 20u;
        capabilities.maximumResourceBytes = 1u << 16u;
        capabilities.maximumAlignmentBytes = 64u;
        capabilities.maximumBindings = 1u;
        capabilities.maximumTransfers = 1u;
        capabilities.maximumDependencies = 0u;
        return capabilities;
    }

    [[nodiscard]] an::Result<an::DeviceAllocation> allocate(const an::DeviceResourceDescriptor& descriptor) override
    {
        if (descriptor.residency != an::ResourceResidency::Host)
        {
            return an::Status::failure(an::ErrorCode::AllocationFailed, "the gate holds host memory only");
        }
        auto* const bytes = static_cast<std::byte*>(
            ::operator new(static_cast<std::size_t>(descriptor.bytes), std::align_val_t{64u}, std::nothrow));
        if (bytes == nullptr)
        {
            return an::Status::failure(an::ErrorCode::AllocationFailed);
        }
        std::memset(bytes, 0, static_cast<std::size_t>(descriptor.bytes));
        const std::lock_guard<std::mutex> held(m_mutex);
        const std::uint64_t handle = m_nextHandle++;
        m_blocks.emplace(handle, bytes);
        return an::DeviceAllocation{.handle = handle};
    }

    void release(an::DeviceAllocation allocation, const an::DeviceResourceDescriptor& descriptor) noexcept override
    {
        (void)descriptor;
        const std::lock_guard<std::mutex> held(m_mutex);
        const auto found = m_blocks.find(allocation.handle);
        if (found != m_blocks.end())
        {
            ::operator delete(static_cast<void*>(found->second), std::align_val_t{64u});
            m_blocks.erase(found);
        }
    }

    [[nodiscard]] an::Result<std::byte*> map(an::DeviceAllocation allocation,
                                             const an::DeviceResourceDescriptor& descriptor,
                                             an::DeviceAccess access) override
    {
        (void)descriptor;
        (void)access;
        return bytes(allocation);
    }

    void unmap(an::DeviceAllocation allocation, const an::DeviceResourceDescriptor& descriptor) noexcept override
    {
        (void)allocation;
        (void)descriptor;
    }

    [[nodiscard]] an::Result<std::shared_ptr<const an::DeviceKernel>>
    prepareKernel(const an::DeviceKernelRequest& request) override
    {
        (void)request;
        return an::Status::failure(an::ErrorCode::UnsupportedBackend, "the gate runs no kernel");
    }

    [[nodiscard]] an::Status enqueue(an::DeviceWork&& work) override
    {
        std::unique_lock<std::mutex> held(m_mutex);
        if (!m_open)
        {
            m_held.push_back(std::move(work));
            return an::Status::success();
        }
        const an::ErrorCode outcome = m_outcome;
        held.unlock();
        run(std::move(work), outcome);
        return an::Status::success();
    }

    /// Runs every held piece of work, and everything taken from now on, on the calling thread - finishing
    /// each with `outcome`, or performing its copies when `outcome` is `ErrorCode::Ok`.
    void open(an::ErrorCode outcome = an::ErrorCode::Ok)
    {
        std::vector<an::DeviceWork> held;
        {
            const std::lock_guard<std::mutex> locked(m_mutex);
            m_open = true;
            m_outcome = outcome;
            held.swap(m_held);
        }
        for (an::DeviceWork& work : held)
        {
            run(std::move(work), outcome);
        }
    }

private:
    [[nodiscard]] std::byte* bytes(an::DeviceAllocation allocation)
    {
        const std::lock_guard<std::mutex> held(m_mutex);
        const auto found = m_blocks.find(allocation.handle);
        return found == m_blocks.end() ? nullptr : found->second;
    }

    void run(an::DeviceWork work, an::ErrorCode outcome)
    {
        work.markRunning();
        if (outcome != an::ErrorCode::Ok)
        {
            work.complete(an::Status::failure(outcome, "the gate was told to fail this work"));
            return;
        }
        for (const an::DeviceTransferStep& step : work.transfersBefore())
        {
            std::memmove(bytes(step.destination) + step.destinationOffset, bytes(step.source) + step.sourceOffset,
                         static_cast<std::size_t>(step.bytes));
            work.recordTransfer(1u);
        }
        work.complete(an::Status::success());
    }

    std::mutex m_mutex;
    std::map<std::uint64_t, std::byte*> m_blocks;
    std::uint64_t m_nextHandle = 1u;
    std::vector<an::DeviceWork> m_held;
    bool m_open = false;
    an::ErrorCode m_outcome = an::ErrorCode::Ok;
};

/// One piece of gate work and the two resources it copies between.
struct GateWork
{
    an::DeviceResource from;
    an::DeviceResource to;
    an::DeviceEvent event;
};

[[nodiscard]] GateWork submitGateWork(const an::Device& gate, const an::ExecutionContext& execution)
{
    const an::DeviceResourceDescriptor descriptor{
        .bytes = 8u, .alignment = 8u, .residency = an::ResourceResidency::Host};
    GateWork work;
    work.from = an::test::makeResource(gate, descriptor);
    work.to = an::test::makeResource(gate, descriptor);
    const std::array transfer{an::DeviceTransfer{.source = &work.from, .destination = &work.to, .bytes = 8u}};
    const an::Result<an::DeviceEvent> event = gate.submit(execution, an::DeviceSubmission{.transfersBefore = transfer});
    EXPECT_TRUE(event.hasValue()) << event.status().message();
    if (event.hasValue())
    {
        work.event = *event;
    }
    return work;
}

TEST(OpenClBackendLifecycleTest, StoppingRunningWorkIsObservedBeforeItsNextStep)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::Device device = an::test::makeDevice(subject.backend);

    // Sixteen uploads of 64 MiB, the most transfers one submission may declare: long enough that a stop
    // requested once the work is seen running arrives before the last step.
    constexpr std::uint64_t kBytes = 64u * 1024u * 1024u;
    constexpr std::size_t kSteps = 16u;
    const an::DeviceResource host = an::test::makeResource(
        device,
        an::DeviceResourceDescriptor{.bytes = kBytes, .alignment = 64u, .residency = an::ResourceResidency::Host});
    const an::DeviceResource onDevice = an::test::makeResource(
        device,
        an::DeviceResourceDescriptor{.bytes = kBytes, .alignment = 64u, .residency = an::ResourceResidency::Device});
    std::vector<an::DeviceTransfer> transfers(
        kSteps, an::DeviceTransfer{.source = &host, .destination = &onDevice, .bytes = kBytes});

    bool observed = false;
    for (int attempt = 0; attempt < 3 && !observed; ++attempt)
    {
        std::stop_source stop;
        an::ExecutionOptions options;
        options.stopToken = stop.get_token();
        const an::ExecutionContext execution = an::test::makeExecution(engine, options);
        const an::Result<an::DeviceEvent> event =
            device.submit(execution, an::DeviceSubmission{.transfersBefore = transfers});
        ASSERT_TRUE(event.hasValue()) << event.status().message();

        const auto deadline = std::chrono::steady_clock::now() + 30s;
        while (event->phase() != an::DeviceWorkPhase::Running && event->phase() != an::DeviceWorkPhase::Finished &&
               std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        stop.request_stop();

        const an::Status outcome = event->wait();
        const an::DeviceWorkTiming timing = event->timing();
        if (outcome.code() == an::ErrorCode::Cancelled)
        {
            EXPECT_LT(timing.transfersRecorded, kSteps);
            observed = true;
        }
        else
        {
            // Every step finished before the stop reached a checkpoint, which is a success that stands.
            EXPECT_TRUE(outcome.ok()) << outcome.message();
            EXPECT_EQ(timing.transfersRecorded, kSteps);
        }
    }
    EXPECT_TRUE(observed) << "sixteen 64 MiB uploads finished three times over before a stop requested while they "
                             "ran reached a checkpoint";

    const an::DeviceCounters counters = device.counters();
    EXPECT_GE(counters.cancelledWork, 1u);
    EXPECT_EQ(counters.pendingWork, 0u);
    EXPECT_EQ(counters.backendDefects, 0u);
}

TEST(OpenClBackendLifecycleTest, WorkWaitingForAnotherDevicesWorkRunsOnlyWhenThatWorkSucceeds)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const auto gateBackend = std::make_shared<GateBackend>();
    const an::Device gate = an::test::makeDevice(gateBackend);
    const an::Device device = an::test::makeDevice(subject.backend);

    const an::ImageLayout layout = an::test::floatPlaneLayout(256u, 128u);
    const std::vector<float> samples = an::test::patternSamples(256u, 128u, 23u);
    const auto describe = [&layout](an::ResourceResidency residency) {
        return an::test::planeDescriptor(layout, residency);
    };
    const an::DeviceResource hostIn = an::test::makeResource(device, describe(an::ResourceResidency::Host));
    const an::DeviceResource deviceIn = an::test::makeResource(device, describe(an::ResourceResidency::Device));
    const an::DeviceResource deviceOut = an::test::makeResource(device, describe(an::ResourceResidency::Device));
    const an::DeviceResource hostOut = an::test::makeResource(device, describe(an::ResourceResidency::Pinned));
    an::test::writeFloatPlane(hostIn, layout, samples);
    const an::PreparedDeviceKernel kernel = an::test::prepareAffine(device, execution, layout, layout, kAffine);

    const GateWork gated = submitGateWork(gate, execution);
    const std::uint64_t bytes = layout.totalBytes();
    const std::array before{an::DeviceTransfer{.source = &hostIn, .destination = &deviceIn, .bytes = bytes}};
    const std::array bindings{an::DeviceBinding{.resource = &deviceIn, .access = an::DeviceAccess::Read},
                              an::DeviceBinding{.resource = &deviceOut, .access = an::DeviceAccess::Write}};
    const std::array after{an::DeviceTransfer{.source = &deviceOut, .destination = &hostOut, .bytes = bytes}};
    const std::array dependencies{gated.event};
    const an::Result<an::DeviceEvent> event =
        device.submit(execution, an::DeviceSubmission{.transfersBefore = before,
                                                      .kernel = &kernel,
                                                      .bindings = bindings,
                                                      .transfersAfter = after,
                                                      .dependencies = dependencies});
    ASSERT_TRUE(event.hasValue()) << event.status().message();

    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(event->phase(), an::DeviceWorkPhase::Waiting);
    EXPECT_EQ(device.counters().uploads.transfers, 0u);

    gateBackend->open();
    const an::Status outcome = event->wait();
    ASSERT_TRUE(outcome.ok()) << outcome.message();
    ASSERT_TRUE(gated.event.outcome().has_value());
    EXPECT_TRUE(gated.event.outcome()->ok());
    EXPECT_EQ(an::test::differingSamples(an::test::readFloatPlane(hostOut, layout),
                                         an::test::referenceAffine(256u, 128u, samples, kAffine)),
              0u);

    const an::DeviceCounters counters = device.counters();
    EXPECT_EQ(counters.uploads.transfers, 1u);
    EXPECT_EQ(counters.downloads.transfers, 1u);
    EXPECT_EQ(counters.kernelRuns, 1u);
    EXPECT_EQ(counters.backendDefects, 0u);
}

TEST(OpenClBackendLifecycleTest, AFailedDependencyOnAnotherDeviceIsTheCauseAndTheOpenClDeviceIsNeverAsked)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const auto gateBackend = std::make_shared<GateBackend>();
    const an::Device gate = an::test::makeDevice(gateBackend);
    const an::Device device = an::test::makeDevice(subject.backend);

    const an::ImageLayout layout = an::test::floatPlaneLayout(32u, 16u);
    const an::DeviceResource hostIn =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Host));
    const an::DeviceResource deviceIn =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Device));

    const GateWork gated = submitGateWork(gate, execution);
    const std::array upload{
        an::DeviceTransfer{.source = &hostIn, .destination = &deviceIn, .bytes = layout.totalBytes()}};
    const std::array dependencies{gated.event};
    const an::Result<an::DeviceEvent> event =
        device.submit(execution, an::DeviceSubmission{.transfersBefore = upload, .dependencies = dependencies});
    ASSERT_TRUE(event.hasValue()) << event.status().message();

    gateBackend->open(an::ErrorCode::ExternalFailure);
    const an::Status outcome = event->wait();
    EXPECT_EQ(outcome.code(), an::ErrorCode::ExternalFailure) << outcome.message();

    const an::DeviceCounters counters = device.counters();
    EXPECT_EQ(counters.failedWork, 1u);
    EXPECT_EQ(counters.uploads.transfers, 0u);
    EXPECT_EQ(counters.pendingWork, 0u);
    EXPECT_EQ(counters.backendDefects, 0u);
}

TEST(OpenClBackendLifecycleTest, TheLastReferenceToTheBackendMayBeReleasedOnItsOwnWorkerThread)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const auto gateBackend = std::make_shared<GateBackend>();
    const an::Device gate = an::test::makeDevice(gateBackend);
    const std::weak_ptr<an::DeviceBackend> watched = subject.backend;

    const GateWork gated = submitGateWork(gate, execution);
    an::DeviceEvent event;
    {
        // Large enough that the worker is still busy after the gate has handed it the work.
        constexpr std::uint32_t kWidth = 2048u;
        constexpr std::uint32_t kHeight = 1024u;
        an::Device device = an::test::makeDevice(std::move(subject.backend));
        const an::ImageLayout layout = an::test::floatPlaneLayout(kWidth, kHeight);
        const auto describe = [&layout](an::ResourceResidency residency) {
            return an::test::planeDescriptor(layout, residency);
        };
        const an::DeviceResource hostIn = an::test::makeResource(device, describe(an::ResourceResidency::Host));
        const an::DeviceResource deviceIn = an::test::makeResource(device, describe(an::ResourceResidency::Device));
        const an::DeviceResource deviceOut = an::test::makeResource(device, describe(an::ResourceResidency::Device));
        const an::DeviceResource hostOut = an::test::makeResource(device, describe(an::ResourceResidency::Host));
        an::test::writeFloatPlane(hostIn, layout, an::test::patternSamples(kWidth, kHeight, 31u));
        const an::PreparedDeviceKernel kernel = an::test::prepareAffine(device, execution, layout, layout, kAffine);

        const std::uint64_t bytes = layout.totalBytes();
        const std::array before{an::DeviceTransfer{.source = &hostIn, .destination = &deviceIn, .bytes = bytes}};
        const std::array bindings{an::DeviceBinding{.resource = &deviceIn, .access = an::DeviceAccess::Read},
                                  an::DeviceBinding{.resource = &deviceOut, .access = an::DeviceAccess::Write}};
        const std::array after{an::DeviceTransfer{.source = &deviceOut, .destination = &hostOut, .bytes = bytes}};
        const std::array dependencies{gated.event};
        const an::Result<an::DeviceEvent> submitted =
            device.submit(execution, an::DeviceSubmission{.transfersBefore = before,
                                                          .kernel = &kernel,
                                                          .bindings = bindings,
                                                          .transfersAfter = after,
                                                          .dependencies = dependencies});
        ASSERT_TRUE(submitted.hasValue()) << submitted.status().message();
        event = *submitted;
        // The device handle, its resources and its kernel all go out of scope here, before the gate
        // opens. What is left of the OpenCL device is what the waiting work holds.
    }
    ASSERT_FALSE(watched.expired());

    // The gate finishes its work on this thread and hands the dependent work to the OpenCL backend, whose
    // worker completes it and so releases the device's last reference on that worker. A backend that
    // joined its own thread there would end the process.
    gateBackend->open();
    const an::Status outcome = event.wait();
    ASSERT_TRUE(outcome.ok()) << outcome.message();

    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!watched.expired() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(1ms);
    }
    EXPECT_TRUE(watched.expired());

    // The reference count reaches zero before the backend's destructor runs, so give that destructor - on
    // the worker - time to finish inside this case. A destructor that joined its own thread ends the
    // process here rather than during whichever case happens to run next.
    std::this_thread::sleep_for(250ms);
}

TEST(OpenClBackendLifecycleTest, ADeviceRefusesToShutDownWhileAnythingOfItsIsOutstandingAndThenReleasesTheBackend)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const std::weak_ptr<an::DeviceBackend> watched = subject.backend;
    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    an::Device device = an::test::makeDevice(std::move(subject.backend));

    const an::ImageLayout layout = an::test::floatPlaneLayout(64u, 64u);
    an::DeviceResource pinned =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Pinned));
    an::DeviceResource onDevice =
        an::test::makeResource(device, an::test::planeDescriptor(layout, an::ResourceResidency::Device));
    const std::array upload{
        an::DeviceTransfer{.source = &pinned, .destination = &onDevice, .bytes = layout.totalBytes()}};
    const an::Result<an::DeviceEvent> event = device.submit(execution, an::DeviceSubmission{.transfersBefore = upload});
    ASSERT_TRUE(event.hasValue()) << event.status().message();
    ASSERT_TRUE(event->wait().ok());

    an::PreparedDeviceKernel kernel = an::test::prepareAffine(device, execution, layout, layout, kAffine);
    an::Result<an::DeviceMapping> mapping = pinned.map(an::DeviceAccess::Read);
    ASSERT_TRUE(mapping.hasValue()) << mapping.status().message();
    pinned.release();
    onDevice.release();

    const auto expectBusy = [&device] {
        const an::Status refusal = device.shutdown();
        EXPECT_TRUE(an::test::refusedFor(refusal, an::ErrorCode::Busy, an::DeviceFailure::DeviceInUse))
            << refusal.message();
        EXPECT_TRUE(device.valid());
    };

    // The mapping keeps the pinned buffer, and the prepared kernel keeps the program.
    EXPECT_EQ(device.counters().liveMappings, 1u);
    EXPECT_EQ(device.counters().liveResources, 1u);
    EXPECT_EQ(device.counters().liveKernels, 1u);
    expectBusy();

    mapping->unmap();
    EXPECT_EQ(device.counters().liveMappings, 0u);
    EXPECT_EQ(device.counters().liveResources, 0u);
    expectBusy();
    kernel = an::PreparedDeviceKernel{};
    EXPECT_EQ(device.counters().liveKernels, 0u);

    const an::Status closed = device.shutdown();
    EXPECT_TRUE(closed.ok()) << closed.message();
    EXPECT_FALSE(device.valid());
    EXPECT_TRUE(watched.expired());
    ASSERT_TRUE(event->outcome().has_value());
    EXPECT_TRUE(event->outcome()->ok());
}

} // namespace
