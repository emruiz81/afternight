// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A deliberately fake device for the device-contract tests: asynchronous, inspectable, and able to
// misbehave on request.
//
// It is built from the published surface alone, which is exactly the position an opt-in GPU module is
// in, so a passing suite proves the contract can be implemented without reaching into the engine. Its
// device memory is host memory the mock refuses to map, and the contract gives neither a caller nor
// the engine an address to read instead - so in these tests a transfer is the only way data crosses
// the residency boundary, exactly as it would be on a card.
//
// Its kernels are the affine pointwise operation, computed over its own memory with the formula the
// scalar reference states, and a pointwise program, evaluated pixel by pixel with the scalar evaluator and
// narrowed as the host executor narrows. Both advertise a narrower capability than the host - all-valid
// input only - so a plan that carries a validity rule is one the mock genuinely cannot run, and a fallback
// in these tests is a real one. A program kernel reports its copy of the program's tables as the device
// memory it holds, which is what a device keeps between runs of a fused program.
//
// Its state lives in a shared core rather than in the backend object. A device may release its last
// reference to a backend on the backend's own thread - completing the last piece of work of a device
// whose handle is already gone does exactly that - and the worker then has to keep running safely
// past the destruction of the object that started it.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/Device.h>
#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/PointwiseKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DeviceResource.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace an::test
{

/// The mock's queue, allocations and counters, shared with its worker thread.
struct MockDeviceCore;

/// How a mock device behaves, including the ways it can be told to misbehave.
struct MockDeviceOptions
{
    /// Whether work finishes later on the mock's own thread, or before `enqueue` returns.
    bool asynchronous = true;

    /// Whether the mock holds every piece of work it takes until `resume` is called.
    bool startPaused = false;

    /// Which residencies the mock creates.
    bool offerHost = true;
    bool offerPinned = true;
    bool offerShared = true;
    bool offerDevice = true;

    /// The limits the mock reports.
    std::uint64_t memoryLimitBytes = 64u * 1024u * 1024u;
    std::uint64_t maximumResourceBytes = 16u * 1024u * 1024u;
    std::uint32_t maximumTransfers = 8u;
    std::uint32_t maximumDependencies = 8u;
    std::uint32_t maximumBindings = 4u;

    /// Scratch bytes the affine kernel declares, so scratch accounting has something to count.
    std::uint64_t kernelScratchBytes = 0u;

    /// Device memory the affine kernel reports holding for its life, so the accounting of prepared kernels
    /// has something to count.
    std::uint64_t kernelResidentBytes = 0u;

    /// The zero-based order in which the mock starts work, of the one piece it fails.
    std::uint64_t failWorkNumber = (std::numeric_limits<std::uint64_t>::max)();

    /// The code that work fails with.
    ErrorCode failWorkCode = ErrorCode::ExternalFailure;

    /// A synchronous test hook run after submission bookkeeping and before the backend takes work.
    void (*beforeEnqueue)(void*) noexcept = nullptr;
    void* enqueueHookData = nullptr;

    /// Faults at the seam.
    bool refuseEnqueue = false;
    bool throwOnEnqueue = false;
    bool throwOnPrepare = false;
    bool completeTwice = false;
    bool abandonWork = false;
    bool skipTransferRecords = false;

    /// How many allocations succeed before the mock refuses the rest.
    std::uint64_t failAllocationsAfter = (std::numeric_limits<std::uint64_t>::max)();

    /// Capabilities to report verbatim instead of the mock's own, for the validation cases.
    std::optional<DeviceCapabilities> capabilities{};
};

/// The contract the mock's affine kernel satisfies: the reference operation's, narrowed to all-valid
/// input.
[[nodiscard]] KernelContract mockAffineContract();

/// The canonical plan the mock's affine kernel supports.
[[nodiscard]] KernelPlanKey mockAffinePlan();

/// The contracts the mock's pointwise program kernel satisfies: Float32 programs computing in Float32 over
/// every domain pair, exactly and - over the two domains a power step is built on - at the power tier, for
/// all-valid input.
[[nodiscard]] std::vector<KernelContract> mockPointwiseProgramContracts();

/// The direct three-by-three stencil contract the mock satisfies.
[[nodiscard]] KernelContract mockStencil3x3Contract();

/// The plan the mock's direct stencil supports for `parameters`.
[[nodiscard]] KernelPlanKey mockStencil3x3Plan(const Stencil3x3KernelParameters& parameters);

/// A fake device with one kernel, faults on request, and inspection for the tests.
class MockDeviceBackend final : public DeviceBackend
{
public:
    explicit MockDeviceBackend(const MockDeviceOptions& options = {});
    ~MockDeviceBackend() override;

    MockDeviceBackend(const MockDeviceBackend&) = delete;
    MockDeviceBackend& operator=(const MockDeviceBackend&) = delete;

    [[nodiscard]] DeviceCapabilities capabilities() const override;
    [[nodiscard]] Result<DeviceAllocation> allocate(const DeviceResourceDescriptor& descriptor) override;
    void release(DeviceAllocation allocation, const DeviceResourceDescriptor& descriptor) noexcept override;
    [[nodiscard]] Result<std::byte*> map(DeviceAllocation allocation, const DeviceResourceDescriptor& descriptor,
                                         DeviceAccess access) override;
    void unmap(DeviceAllocation allocation, const DeviceResourceDescriptor& descriptor) noexcept override;
    [[nodiscard]] Result<std::shared_ptr<const DeviceKernel>>
    prepareKernel(const DeviceKernelRequest& request) override;
    [[nodiscard]] Status enqueue(DeviceWork&& work) override;

    /// Holds every piece of work taken from now on.
    void pause();

    /// Releases held work. A synchronous mock runs it on the calling thread before returning.
    void resume();

    /// Waits until nothing is queued and nothing is running.
    [[nodiscard]] bool waitUntilIdle(std::chrono::milliseconds timeout);

    /// How many pieces of work the mock has started.
    [[nodiscard]] std::uint64_t startedWork() const;

    /// How many pieces of work are held or waiting for the worker.
    [[nodiscard]] std::uint64_t queuedWork() const;

    /// How many allocations the mock currently holds.
    [[nodiscard]] std::uint64_t liveAllocations() const;

    /// How many times `allocate` was called.
    [[nodiscard]] std::uint64_t allocationCalls() const;

    /// How many kernels the mock prepared are still alive, whoever holds them.
    [[nodiscard]] std::uint64_t liveKernels() const;

private:
    std::shared_ptr<MockDeviceCore> m_core;
    std::thread m_worker;
};

/// An engine context for the device tests, with room for several concurrent executions.
[[nodiscard]] EngineContext makeDeviceTestEngine();

/// An execution for the device tests. A caller that leaves the concurrency at its default gets one
/// thread, so several executions fit in one engine context at once.
[[nodiscard]] ExecutionContext makeDeviceTestExecution(const EngineContext& engine, ExecutionOptions options = {});

/// A device over a mock, expected to be accepted.
[[nodiscard]] Device makeMockDevice(const std::shared_ptr<MockDeviceBackend>& backend);

/// A single-plane Float32 layout, with rows padded to `rowAlignmentBytes`.
[[nodiscard]] ImageLayout floatPlaneLayout(std::uint32_t width, std::uint32_t height,
                                           std::uint64_t rowAlignmentBytes = 1u);

/// A description of a resource that holds exactly one plane of `layout`.
[[nodiscard]] DeviceResourceDescriptor planeDescriptor(const ImageLayout& layout, ResourceResidency residency);

/// A resource, expected to be created.
[[nodiscard]] DeviceResource makeResource(const Device& device, const DeviceResourceDescriptor& descriptor);

/// Deterministic row-major samples, finite and far from Float32's range limits.
[[nodiscard]] std::vector<float> patternSamples(std::uint32_t width, std::uint32_t height, std::uint32_t seed);

/// Writes row-major samples into a host-addressable resource at `layout`'s row offsets.
void writeFloatPlane(const DeviceResource& resource, const ImageLayout& layout, std::span<const float> samples);

/// Reads a host-addressable resource's samples at `layout`'s row offsets, row-major.
[[nodiscard]] std::vector<float> readFloatPlane(const DeviceResource& resource, const ImageLayout& layout);

/// The scalar reference's answer for the affine operation over row-major samples.
[[nodiscard]] std::vector<float> referenceAffine(std::uint32_t width, std::uint32_t height,
                                                 std::span<const float> samples,
                                                 const AffineKernelParameters& parameters);

/// Prepares the mock's affine kernel over two layouts under a plan, expected to succeed.
[[nodiscard]] PreparedDeviceKernel prepareAffine(const Device& device, const ExecutionContext& execution,
                                                 const ImageLayout& input, const ImageLayout& output,
                                                 const AffineKernelParameters& parameters,
                                                 const KernelPlanKey& plan = mockAffinePlan());

/// Prepares the mock's direct three-by-three stencil over two layouts, expected to succeed.
[[nodiscard]] PreparedDeviceKernel prepareStencil3x3(const Device& device, const ExecutionContext& execution,
                                                     const ImageLayout& input, const ImageLayout& output,
                                                     const Stencil3x3KernelParameters& parameters);

/// Whether two runs of samples are identical bit for bit.
[[nodiscard]] bool sameBits(std::span<const float> left, std::span<const float> right);

} // namespace an::test
