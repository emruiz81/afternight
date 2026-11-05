// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The fake device the device-contract tests run against. See MockDeviceBackend.h for what it is and
// why it is built from the published surface alone.

#include "MockDeviceBackend.h"

#include <AfterNightEngine/Algorithms/Kernel/MathKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseEvaluator.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramKernel.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleConversion.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <new>
#include <stdexcept>
#include <utility>

namespace an::test
{

namespace
{

using Clock = std::chrono::steady_clock;

/// One allocation the mock holds.
struct MockBlock
{
    std::byte* data = nullptr;
    std::uint64_t bytes = 0u;
    std::uint64_t alignment = 1u;
    ResourceResidency residency = ResourceResidency::Host;
    bool mapped = false;
};

/// How many of the mock's kernels are alive, shared by every kernel and the mock that prepared them.
using KernelCount = std::shared_ptr<std::atomic<std::uint64_t>>;

/// Counts a mock kernel as alive from its construction to its destruction, on whichever thread drops it.
class KernelCensus
{
public:
    explicit KernelCensus(KernelCount alive) noexcept : m_alive(std::move(alive))
    {
        ++*m_alive;
    }

    ~KernelCensus()
    {
        --*m_alive;
    }

    KernelCensus(const KernelCensus&) = delete;
    KernelCensus& operator=(const KernelCensus&) = delete;

private:
    KernelCount m_alive;
};

/// The affine kernel as the mock prepares it: two layouts, two coefficients, a scratch size and the device
/// memory it reports holding.
class MockAffineKernel final : public DeviceKernel
{
public:
    MockAffineKernel(const KernelPlanKey& plan, const ImageLayout& input, const ImageLayout& output,
                     const AffineKernelParameters& parameters, std::uint64_t scratchBytes, std::uint64_t residentBytes,
                     KernelCount census)
        : m_plan(plan), m_bindings{DeviceBindingRequirement{.access = DeviceAccess::Read,
                                                            .minimumBytes = input.totalBytes(),
                                                            .alignment = alignof(float),
                                                            .layout = input},
                                   DeviceBindingRequirement{.access = DeviceAccess::Write,
                                                            .minimumBytes = output.totalBytes(),
                                                            .alignment = alignof(float),
                                                            .layout = output}},
          m_parameters(parameters), m_scratchBytes(scratchBytes), m_residentBytes(residentBytes),
          m_census(std::move(census))
    {
    }

    [[nodiscard]] KernelPlanKey plan() const noexcept override
    {
        return m_plan;
    }

    [[nodiscard]] std::span<const DeviceBindingRequirement> bindings() const noexcept override
    {
        return m_bindings;
    }

    [[nodiscard]] std::uint64_t scratchBytes() const noexcept override
    {
        return m_scratchBytes;
    }

    [[nodiscard]] std::uint64_t residentBytes() const noexcept override
    {
        return m_residentBytes;
    }

    [[nodiscard]] const ImageLayout& inputLayout() const noexcept
    {
        return *m_bindings[0].layout;
    }

    [[nodiscard]] const ImageLayout& outputLayout() const noexcept
    {
        return *m_bindings[1].layout;
    }

    [[nodiscard]] const AffineKernelParameters& parameters() const noexcept
    {
        return m_parameters;
    }

private:
    KernelPlanKey m_plan;
    std::array<DeviceBindingRequirement, 2u> m_bindings;
    AffineKernelParameters m_parameters;
    std::uint64_t m_scratchBytes;
    std::uint64_t m_residentBytes;
    KernelCensus m_census;
};

/// The bytes of a program's tables, which is what a prepared program kernel keeps between runs.
[[nodiscard]] std::uint64_t programTableBytes(const PointwiseProgram& program) noexcept
{
    std::uint64_t bytes = 0u;
    for (std::uint32_t index = 0u; index < program.tableCount(); ++index)
    {
        bytes += std::uint64_t{program.table(index).size()} * sizeof(double);
    }
    return bytes;
}

/// A pointwise program kernel as the mock prepares it: its own copy of the program, and one requirement per
/// source followed by the destination's. Its copy of the program's tables is the device memory it holds.
class MockProgramKernel final : public DeviceKernel
{
public:
    MockProgramKernel(const KernelPlanKey& plan, PointwiseProgram program,
                      std::vector<DeviceBindingRequirement> bindings, KernelCount census)
        : m_plan(plan), m_program(std::move(program)), m_bindings(std::move(bindings)),
          m_residentBytes(programTableBytes(m_program)), m_census(std::move(census))
    {
    }

    [[nodiscard]] KernelPlanKey plan() const noexcept override
    {
        return m_plan;
    }

    [[nodiscard]] std::span<const DeviceBindingRequirement> bindings() const noexcept override
    {
        return m_bindings;
    }

    [[nodiscard]] std::uint64_t scratchBytes() const noexcept override
    {
        return 0u;
    }

    [[nodiscard]] std::uint64_t residentBytes() const noexcept override
    {
        return m_residentBytes;
    }

    [[nodiscard]] const PointwiseProgram& program() const noexcept
    {
        return m_program;
    }

private:
    KernelPlanKey m_plan;
    PointwiseProgram m_program;
    std::vector<DeviceBindingRequirement> m_bindings;
    std::uint64_t m_residentBytes;
    KernelCensus m_census;
};

/// A three-by-three stencil as the mock prepares it, including its one-pixel halo scratch.
class MockStencil3x3Kernel final : public DeviceKernel
{
public:
    MockStencil3x3Kernel(const KernelPlanKey& plan, const ImageLayout& input, const ImageLayout& output,
                         const Stencil3x3KernelParameters& parameters, KernelCount census)
        : m_plan(plan), m_bindings{DeviceBindingRequirement{.access = DeviceAccess::Read,
                                                            .minimumBytes = input.totalBytes(),
                                                            .alignment = alignof(float),
                                                            .layout = input},
                                   DeviceBindingRequirement{.access = DeviceAccess::Write,
                                                            .minimumBytes = output.totalBytes(),
                                                            .alignment = alignof(float),
                                                            .layout = output}},
          m_parameters(parameters),
          m_scratchBytes(std::uint64_t{output.extent().width()} * output.extent().height() * 9u * sizeof(float)),
          m_census(std::move(census))
    {
    }

    [[nodiscard]] KernelPlanKey plan() const noexcept override
    {
        return m_plan;
    }

    [[nodiscard]] std::span<const DeviceBindingRequirement> bindings() const noexcept override
    {
        return m_bindings;
    }

    [[nodiscard]] std::uint64_t scratchBytes() const noexcept override
    {
        return m_scratchBytes;
    }

    /// The weights travel in the kernel's arguments and the halo in its scratch, so nothing stays behind.
    [[nodiscard]] std::uint64_t residentBytes() const noexcept override
    {
        return 0u;
    }

    [[nodiscard]] const ImageLayout& inputLayout() const noexcept
    {
        return *m_bindings[0].layout;
    }

    [[nodiscard]] const ImageLayout& outputLayout() const noexcept
    {
        return *m_bindings[1].layout;
    }

    [[nodiscard]] const Stencil3x3KernelParameters& parameters() const noexcept
    {
        return m_parameters;
    }

private:
    KernelPlanKey m_plan;
    std::array<DeviceBindingRequirement, 2u> m_bindings;
    Stencil3x3KernelParameters m_parameters;
    std::uint64_t m_scratchBytes;
    KernelCensus m_census;
};

[[nodiscard]] std::uint64_t nanosecondsSince(Clock::time_point start) noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}

} // namespace

struct MockDeviceCore
{
    explicit MockDeviceCore(const MockDeviceOptions& configured) : options(configured), paused(configured.startPaused)
    {
    }

    ~MockDeviceCore()
    {
        for (auto& entry : blocks)
        {
            ::operator delete(static_cast<void*>(entry.second.data),
                              std::align_val_t{static_cast<std::size_t>(entry.second.alignment)});
        }
    }

    MockDeviceCore(const MockDeviceCore&) = delete;
    MockDeviceCore& operator=(const MockDeviceCore&) = delete;

    MockDeviceOptions options;

    mutable std::mutex mutex;
    std::condition_variable changed;
    std::deque<DeviceWork> queue;
    bool paused = false;
    bool stopping = false;
    bool busy = false;
    std::uint64_t started = 0u;

    mutable std::mutex allocationMutex;
    std::map<std::uint64_t, MockBlock> blocks;
    std::uint64_t nextHandle = 1u;
    std::uint64_t allocationCalls = 0u;

    /// Every kernel the mock prepared that has not been destroyed.
    KernelCount liveKernels = std::make_shared<std::atomic<std::uint64_t>>(0u);
};

namespace
{

[[nodiscard]] std::byte* blockBytes(MockDeviceCore& core, DeviceAllocation allocation)
{
    const std::lock_guard<std::mutex> held(core.allocationMutex);
    const auto found = core.blocks.find(allocation.handle);
    return found == core.blocks.end() ? nullptr : found->second.data;
}

[[nodiscard]] Status copyStep(MockDeviceCore& core, const DeviceTransferStep& step)
{
    std::byte* const source = blockBytes(core, step.source);
    std::byte* const destination = blockBytes(core, step.destination);
    if (source == nullptr || destination == nullptr)
    {
        return Status::failure(ErrorCode::Internal, "the mock device was handed an allocation it does not hold");
    }
    std::memmove(destination + step.destinationOffset, source + step.sourceOffset,
                 static_cast<std::size_t>(step.bytes));
    return Status::success();
}

void runAffine(MockDeviceCore& core, const MockAffineKernel& kernel, std::span<const DeviceBindingStep> bindings)
{
    std::byte* const input = blockBytes(core, bindings[0].allocation);
    std::byte* const output = blockBytes(core, bindings[1].allocation);
    const ImageLayout& inputLayout = kernel.inputLayout();
    const ImageLayout& outputLayout = kernel.outputLayout();
    const AffineKernelParameters& parameters = kernel.parameters();
    for (std::uint32_t y = 0u; y < inputLayout.extent().height(); ++y)
    {
        const std::uint64_t inputRow = *inputLayout.rowByteOffset(0u, y);
        const std::uint64_t outputRow = *outputLayout.rowByteOffset(0u, y);
        for (std::uint32_t x = 0u; x < inputLayout.extent().width(); ++x)
        {
            float value = 0.0f;
            std::memcpy(&value, input + inputRow + std::uint64_t{x} * sizeof(float), sizeof(float));
            const float result = static_cast<float>(static_cast<double>(value) * parameters.scale + parameters.bias);
            std::memcpy(output + outputRow + std::uint64_t{x} * sizeof(float), &result, sizeof(float));
        }
    }
}

[[nodiscard]] Status runStencil3x3(MockDeviceCore& core, const MockStencil3x3Kernel& kernel,
                                   std::span<const DeviceBindingStep> bindings,
                                   std::optional<DeviceAllocation> scratchAllocation)
{
    std::byte* const input = blockBytes(core, bindings[0].allocation);
    std::byte* const output = blockBytes(core, bindings[1].allocation);
    std::byte* const scratch = scratchAllocation.has_value() ? blockBytes(core, *scratchAllocation) : nullptr;
    if (input == nullptr || output == nullptr || scratch == nullptr)
    {
        return Status::failure(ErrorCode::Internal, "the mock stencil did not receive its buffers and scratch");
    }

    const ImageLayout& inputLayout = kernel.inputLayout();
    const ImageLayout& outputLayout = kernel.outputLayout();
    const Stencil3x3KernelParameters& parameters = kernel.parameters();
    const std::uint32_t width = inputLayout.extent().width();
    const std::uint32_t height = inputLayout.extent().height();
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const std::uint64_t scratchBase = (std::uint64_t{y} * width + x) * 9u * sizeof(float);
            std::size_t coefficient = 0u;
            for (std::int32_t offsetY = -1; offsetY <= 1; ++offsetY)
            {
                for (std::int32_t offsetX = -1; offsetX <= 1; ++offsetX, ++coefficient)
                {
                    const std::int64_t candidateX = static_cast<std::int64_t>(x) + offsetX;
                    const std::int64_t candidateY = static_cast<std::int64_t>(y) + offsetY;
                    float neighbor = parameters.constantBorder;
                    if (parameters.border == KernelBorderMode::Clamp ||
                        (candidateX >= 0 && candidateY >= 0 && candidateX < width && candidateY < height))
                    {
                        const std::uint32_t sourceX = static_cast<std::uint32_t>(
                            (std::clamp)(candidateX, std::int64_t{0}, static_cast<std::int64_t>(width) - 1));
                        const std::uint32_t sourceY = static_cast<std::uint32_t>(
                            (std::clamp)(candidateY, std::int64_t{0}, static_cast<std::int64_t>(height) - 1));
                        std::memcpy(&neighbor,
                                    input + *inputLayout.rowByteOffset(0u, sourceY) +
                                        std::uint64_t{sourceX} * sizeof(float),
                                    sizeof(float));
                    }
                    std::memcpy(scratch + scratchBase + coefficient * sizeof(float), &neighbor, sizeof(float));
                }
            }

            double sum = 0.0;
            for (coefficient = 0u; coefficient < parameters.coefficients.size(); ++coefficient)
            {
                float neighbor = 0.0f;
                std::memcpy(&neighbor, scratch + scratchBase + coefficient * sizeof(float), sizeof(float));
                sum += static_cast<double>(neighbor) * parameters.coefficients[coefficient];
            }
            const float narrowed = static_cast<float>(sum);
            std::memcpy(output + *outputLayout.rowByteOffset(0u, y) + std::uint64_t{x} * sizeof(float), &narrowed,
                        sizeof(float));
        }
    }
    return Status::success();
}

/// Evaluates every pixel of a program with the scalar evaluator over the mock's own memory, and narrows each
/// result as the host executor does, so the mock's answer is the host reference's.
[[nodiscard]] Status runProgram(MockDeviceCore& core, const MockProgramKernel& kernel,
                                std::span<const DeviceBindingStep> bindings)
{
    const PointwiseProgram& program = kernel.program();
    const std::span<const DeviceBindingRequirement> requirements = kernel.bindings();
    const std::uint32_t channels = program.channelCount();
    const std::uint32_t sources = program.sourceCount();
    std::vector<std::byte*> blocks;
    for (const DeviceBindingStep& binding : bindings)
    {
        blocks.push_back(blockBytes(core, binding.allocation));
        if (blocks.back() == nullptr)
        {
            return Status::failure(ErrorCode::Internal, "the mock device was bound to an allocation it does not hold");
        }
    }
    const ImageLayout& destination = *requirements[sources].layout;
    const ConversionPolicy narrowing{.domain = DomainMapping::CodeValue};
    std::vector<double> pixel(std::size_t{sources} * channels);
    std::vector<double> result(channels);
    for (std::uint32_t y = 0u; y < destination.extent().height(); ++y)
    {
        for (std::uint32_t x = 0u; x < destination.extent().width(); ++x)
        {
            for (std::uint32_t source = 0u; source < sources; ++source)
            {
                const ImageLayout& layout = *requirements[source].layout;
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    float value = 0.0f;
                    std::memcpy(&value,
                                blocks[source] + *layout.rowByteOffset(channel, y) + std::uint64_t{x} * sizeof(float),
                                sizeof(float));
                    pixel[std::size_t{source} * channels + channel] = static_cast<double>(value);
                }
            }
            Status evaluated = evaluatePointwisePixel(program, pixel, result);
            if (evaluated.failed())
            {
                return evaluated;
            }
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                const float narrowed = convertSample<double, float>(result[channel], narrowing).value;
                std::memcpy(blocks[sources] + *destination.rowByteOffset(channel, y) + std::uint64_t{x} * sizeof(float),
                            &narrowed, sizeof(float));
            }
        }
    }
    return Status::success();
}

/// Runs one step list, recording each transfer, and stops at the first checkpoint that says so.
[[nodiscard]] Status runTransfers(MockDeviceCore& core, DeviceWork& work, std::span<const DeviceTransferStep> steps)
{
    for (const DeviceTransferStep& step : steps)
    {
        Status status = work.checkCancellation();
        if (status.failed())
        {
            return status;
        }
        const Clock::time_point start = Clock::now();
        status = copyStep(core, step);
        if (status.failed())
        {
            return status;
        }
        if (!core.options.skipTransferRecords)
        {
            work.recordTransfer(nanosecondsSince(start));
        }
    }
    return Status::success();
}

void execute(MockDeviceCore& core, DeviceWork work)
{
    std::uint64_t order = 0u;
    {
        const std::lock_guard<std::mutex> held(core.mutex);
        order = core.started++;
    }
    work.markRunning();

    if (order == core.options.failWorkNumber)
    {
        work.complete(Status::failure(core.options.failWorkCode, "the mock device was told to fail this work"));
        return;
    }
    if (core.options.abandonWork)
    {
        return;
    }

    Status status = runTransfers(core, work, work.transfersBefore());
    if (status.failed())
    {
        work.complete(std::move(status));
        return;
    }

    if (const DeviceKernel* const kernel = work.kernel(); kernel != nullptr)
    {
        status = work.checkCancellation();
        if (status.failed())
        {
            work.complete(std::move(status));
            return;
        }
        if (const std::optional<DeviceAllocation> scratch = work.scratch(); scratch.has_value())
        {
            // The scratch is real memory: write all of it, so an allocation smaller than declared
            // would be found by a sanitizer rather than trusted.
            if (std::byte* const bytes = blockBytes(core, *scratch); bytes != nullptr)
            {
                std::memset(bytes, 0xA5, static_cast<std::size_t>(kernel->scratchBytes()));
            }
        }
        const Clock::time_point start = Clock::now();
        if (const auto* const program = dynamic_cast<const MockProgramKernel*>(kernel); program != nullptr)
        {
            status = runProgram(core, *program, work.bindings());
            if (status.failed())
            {
                work.complete(std::move(status));
                return;
            }
        }
        else if (const auto* const stencil = dynamic_cast<const MockStencil3x3Kernel*>(kernel); stencil != nullptr)
        {
            status = runStencil3x3(core, *stencil, work.bindings(), work.scratch());
            if (status.failed())
            {
                work.complete(std::move(status));
                return;
            }
        }
        else
        {
            runAffine(core, static_cast<const MockAffineKernel&>(*kernel), work.bindings());
        }
        work.recordKernel(nanosecondsSince(start));
    }

    status = runTransfers(core, work, work.transfersAfter());
    if (status.failed())
    {
        work.complete(std::move(status));
        return;
    }

    work.complete(Status::success());
    if (core.options.completeTwice)
    {
        work.complete(Status::failure(ErrorCode::Internal, "the mock device completed this work a second time"));
    }
}

void runWorker(const std::shared_ptr<MockDeviceCore>& core)
{
    std::unique_lock<std::mutex> lock(core->mutex);
    for (;;)
    {
        core->changed.wait(lock, [&core] { return core->stopping || (!core->paused && !core->queue.empty()); });
        if (core->stopping)
        {
            return;
        }
        DeviceWork work = std::move(core->queue.front());
        core->queue.pop_front();
        core->busy = true;
        lock.unlock();
        execute(*core, std::move(work));
        lock.lock();
        core->busy = false;
        core->changed.notify_all();
    }
}

} // namespace

KernelContract mockAffineContract()
{
    for (const KernelRegistration& registration : kernelRegistry())
    {
        if (registration.contract.id.operation == KernelOperation::PointwiseAffine &&
            registration.backend == ComputeBackend::Reference)
        {
            KernelContract contract = registration.contract;
            contract.capabilities.validity = KernelValidityCapability::AllValidOnly;
            contract.capabilities.parallelism = KernelParallelPolicy::Serial;
            return contract;
        }
    }
    ADD_FAILURE() << "the kernel registry holds no reference affine operation";
    return KernelContract{};
}

KernelPlanKey mockAffinePlan()
{
    return kernelPlanFor(mockAffineContract());
}

std::vector<KernelContract> mockPointwiseProgramContracts()
{
    constexpr std::array kDomains{NumericDomain::NormalizedLinear, NumericDomain::EncodedNonlinear,
                                  NumericDomain::RawSensorCode, NumericDomain::PhysicalScaled};
    std::vector<KernelContract> contracts;
    for (const NumericDomain input : kDomains)
    {
        for (const NumericDomain output : kDomains)
        {
            contracts.push_back(pointwiseProgramKernelContract(
                PointwiseProgramKernelShape{.inputDomain = input, .outputDomain = output}));
            // The builder accepts a power step over these two input domains only.
            if (input == NumericDomain::NormalizedLinear || input == NumericDomain::EncodedNonlinear)
            {
                contracts.push_back(pointwiseProgramKernelContract(
                    PointwiseProgramKernelShape{.inputDomain = input,
                                                .outputDomain = output,
                                                .accuracy = mathFunctionAccuracy(MathFunction::Pow)}));
            }
        }
    }
    return contracts;
}

KernelContract mockStencil3x3Contract()
{
    return stencil3x3KernelContract();
}

KernelPlanKey mockStencil3x3Plan(const Stencil3x3KernelParameters& parameters)
{
    return stencil3x3KernelPlan(parameters);
}

MockDeviceBackend::MockDeviceBackend(const MockDeviceOptions& options)
    : m_core(std::make_shared<MockDeviceCore>(options))
{
    if (options.asynchronous)
    {
        m_worker = std::thread([core = m_core] { runWorker(core); });
    }
}

MockDeviceBackend::~MockDeviceBackend()
{
    {
        const std::lock_guard<std::mutex> held(m_core->mutex);
        m_core->stopping = true;
    }
    m_core->changed.notify_all();
    if (m_worker.joinable())
    {
        // Destroyed on its own worker thread when that thread released the device's last reference:
        // joining would wait for itself, so it detaches and the worker, which holds the core, finishes
        // the loop it is in without touching this object again.
        if (m_worker.get_id() == std::this_thread::get_id())
        {
            m_worker.detach();
        }
        else
        {
            m_worker.join();
        }
    }
}

DeviceCapabilities MockDeviceBackend::capabilities() const
{
    const MockDeviceOptions& options = m_core->options;
    if (options.capabilities.has_value())
    {
        return *options.capabilities;
    }
    DeviceCapabilities capabilities;
    capabilities.name = "mock";
    capabilities.hostResidency = options.offerHost;
    capabilities.pinnedResidency = options.offerPinned;
    capabilities.sharedResidency = options.offerShared;
    capabilities.deviceResidency = options.offerDevice;
    // A mock that holds work until it is resumed does not finish it before `enqueue` returns, whatever
    // thread it later runs the work on, and says so.
    capabilities.asynchronous = options.asynchronous || options.startPaused;
    capabilities.memoryLimitBytes = options.memoryLimitBytes;
    capabilities.maximumResourceBytes = options.maximumResourceBytes;
    capabilities.maximumAlignmentBytes = 4096u;
    capabilities.maximumBindings = options.maximumBindings;
    capabilities.maximumTransfers = options.maximumTransfers;
    capabilities.maximumDependencies = options.maximumDependencies;
    capabilities.kernels.push_back(
        DeviceKernelSupport{.contract = mockAffineContract(), .determinism = KernelDeterminism::Reproducible});
    capabilities.kernels.push_back(
        DeviceKernelSupport{.contract = mockStencil3x3Contract(), .determinism = KernelDeterminism::Reproducible});
    for (const KernelContract& contract : mockPointwiseProgramContracts())
    {
        capabilities.kernels.push_back(
            DeviceKernelSupport{.contract = contract, .determinism = KernelDeterminism::Reproducible});
    }
    return capabilities;
}

Result<DeviceAllocation> MockDeviceBackend::allocate(const DeviceResourceDescriptor& descriptor)
{
    const std::lock_guard<std::mutex> held(m_core->allocationMutex);
    ++m_core->allocationCalls;
    if (m_core->allocationCalls > m_core->options.failAllocationsAfter)
    {
        return Status::failure(ErrorCode::AllocationFailed, "the mock device was told to refuse this allocation");
    }
    auto* const data = static_cast<std::byte*>(
        ::operator new(static_cast<std::size_t>(descriptor.bytes),
                       std::align_val_t{static_cast<std::size_t>(descriptor.alignment)}, std::nothrow));
    if (data == nullptr)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
    std::memset(data, 0, static_cast<std::size_t>(descriptor.bytes));
    const std::uint64_t handle = m_core->nextHandle++;
    m_core->blocks.emplace(handle, MockBlock{.data = data,
                                             .bytes = descriptor.bytes,
                                             .alignment = descriptor.alignment,
                                             .residency = descriptor.residency,
                                             .mapped = false});
    return DeviceAllocation{.handle = handle};
}

void MockDeviceBackend::release(DeviceAllocation allocation, const DeviceResourceDescriptor& descriptor) noexcept
{
    (void)descriptor;
    const std::lock_guard<std::mutex> held(m_core->allocationMutex);
    const auto found = m_core->blocks.find(allocation.handle);
    if (found == m_core->blocks.end())
    {
        ADD_FAILURE() << "the device released an allocation the mock does not hold";
        return;
    }
    EXPECT_FALSE(found->second.mapped) << "the device released an allocation that is still mapped";
    ::operator delete(static_cast<void*>(found->second.data),
                      std::align_val_t{static_cast<std::size_t>(found->second.alignment)});
    m_core->blocks.erase(found);
}

Result<std::byte*> MockDeviceBackend::map(DeviceAllocation allocation, const DeviceResourceDescriptor& descriptor,
                                          DeviceAccess access)
{
    (void)descriptor;
    (void)access;
    const std::lock_guard<std::mutex> held(m_core->allocationMutex);
    const auto found = m_core->blocks.find(allocation.handle);
    if (found == m_core->blocks.end())
    {
        return Status::failure(ErrorCode::Internal, "the device mapped an allocation the mock does not hold");
    }
    if (!isHostAddressable(found->second.residency))
    {
        // The device never asks for this; refusing anyway keeps the mock's device memory unreadable
        // from the host by any route.
        return Status::failure(ErrorCode::IncompatibleArguments, "the mock device never maps its device memory");
    }
    if (found->second.mapped)
    {
        return Status::failure(ErrorCode::InvalidState, "the device mapped an allocation twice");
    }
    found->second.mapped = true;
    return found->second.data;
}

void MockDeviceBackend::unmap(DeviceAllocation allocation, const DeviceResourceDescriptor& descriptor) noexcept
{
    (void)descriptor;
    const std::lock_guard<std::mutex> held(m_core->allocationMutex);
    const auto found = m_core->blocks.find(allocation.handle);
    if (found == m_core->blocks.end() || !found->second.mapped)
    {
        ADD_FAILURE() << "the device unmapped an allocation that was not mapped";
        return;
    }
    found->second.mapped = false;
}

Result<std::shared_ptr<const DeviceKernel>> MockDeviceBackend::prepareKernel(const DeviceKernelRequest& request)
{
    if (m_core->options.throwOnPrepare)
    {
        throw std::runtime_error("the mock device was told to throw while preparing a kernel");
    }
    if (request.plan.id == kPointwiseProgramKernelId)
    {
        const Status checked = checkPointwiseProgramKernelRequest(request);
        if (checked.failed())
        {
            return checked;
        }
        const PointwiseProgram& program = *pointwiseProgramOf(request.arguments);
        if (program.sourceSampleType() != SampleType::Float32 || program.precision() != ComputePrecision::Float32)
        {
            return Status::failure(ErrorCode::UnsupportedBackend, "the mock device evaluates Float32 programs only");
        }
        std::vector<DeviceBindingRequirement> bindings;
        for (std::size_t index = 0u; index < request.bindingLayouts.size(); ++index)
        {
            const ImageLayout& layout = request.bindingLayouts[index];
            bindings.push_back(DeviceBindingRequirement{
                .access = index + 1u < request.bindingLayouts.size() ? DeviceAccess::Read : DeviceAccess::Write,
                .minimumBytes = layout.totalBytes(),
                .alignment = alignof(float),
                .layout = layout});
        }
        return std::shared_ptr<const DeviceKernel>(
            std::make_shared<const MockProgramKernel>(request.plan, program, std::move(bindings), m_core->liveKernels));
    }
    if (request.plan.id == kStencil3x3KernelId)
    {
        const Status checked = checkStencil3x3KernelRequest(request);
        if (checked.failed())
        {
            return checked;
        }
        Stencil3x3KernelParameters parameters;
        std::memcpy(&parameters, request.arguments.data(), sizeof(parameters));
        return std::shared_ptr<const DeviceKernel>(std::make_shared<const MockStencil3x3Kernel>(
            request.plan, request.bindingLayouts[0], request.bindingLayouts[1], parameters, m_core->liveKernels));
    }
    if (!kernelContractSupports(mockAffineContract(), request.plan))
    {
        return Status::failure(ErrorCode::UnsupportedBackend, "the mock device only runs the affine kernel");
    }
    if (request.bindingLayouts.size() != 2u)
    {
        return Status::failure(ErrorCode::InvalidArgument, "the affine kernel binds one input and one output");
    }
    const ImageLayout& input = request.bindingLayouts[0];
    const ImageLayout& output = request.bindingLayouts[1];
    const auto isFloatPlane = [](const ImageLayout& layout) {
        return layout.elementSizeBytes() == sizeof(float) && layout.samplesPerPixel() == 1u &&
               layout.planeCount() == 1u;
    };
    if (!isFloatPlane(input) || !isFloatPlane(output) || input.extent().width() != output.extent().width() ||
        input.extent().height() != output.extent().height())
    {
        return Status::failure(ErrorCode::InvalidArgument, "the affine kernel maps one Float32 plane onto another");
    }
    if (request.arguments.size() != sizeof(AffineKernelParameters))
    {
        return Status::failure(ErrorCode::InvalidArgument, "the affine kernel's arguments are its two coefficients");
    }
    AffineKernelParameters parameters;
    std::memcpy(&parameters, request.arguments.data(), sizeof(parameters));
    if (!std::isfinite(parameters.scale) || !std::isfinite(parameters.bias))
    {
        return Status::failure(ErrorCode::InvalidArgument, "the affine coefficients must be finite");
    }
    return std::shared_ptr<const DeviceKernel>(std::make_shared<const MockAffineKernel>(
        request.plan, input, output, parameters, m_core->options.kernelScratchBytes,
        m_core->options.kernelResidentBytes, m_core->liveKernels));
}

Status MockDeviceBackend::enqueue(DeviceWork&& work)
{
    if (m_core->options.beforeEnqueue != nullptr)
        m_core->options.beforeEnqueue(m_core->options.enqueueHookData);
    if (m_core->options.throwOnEnqueue)
    {
        throw std::runtime_error("the mock device was told to throw while taking work");
    }
    if (m_core->options.refuseEnqueue)
    {
        return Status::failure(ErrorCode::Busy);
    }

    const std::shared_ptr<MockDeviceCore> core = m_core;
    std::unique_lock<std::mutex> lock(core->mutex);
    if (core->paused || core->options.asynchronous)
    {
        core->queue.push_back(std::move(work));
        lock.unlock();
        core->changed.notify_all();
        return Status::success();
    }
    lock.unlock();
    execute(*core, std::move(work));
    return Status::success();
}

void MockDeviceBackend::pause()
{
    const std::lock_guard<std::mutex> held(m_core->mutex);
    m_core->paused = true;
}

void MockDeviceBackend::resume()
{
    // Everything below touches the core and never this object: releasing held work can complete the
    // device's last reference and destroy this backend before the call returns.
    const std::shared_ptr<MockDeviceCore> core = m_core;
    {
        const std::lock_guard<std::mutex> held(core->mutex);
        core->paused = false;
    }
    core->changed.notify_all();
    if (core->options.asynchronous)
    {
        return;
    }
    for (;;)
    {
        DeviceWork work;
        {
            const std::lock_guard<std::mutex> held(core->mutex);
            if (core->paused || core->queue.empty())
            {
                return;
            }
            work = std::move(core->queue.front());
            core->queue.pop_front();
        }
        execute(*core, std::move(work));
    }
}

bool MockDeviceBackend::waitUntilIdle(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_core->mutex);
    return m_core->changed.wait_for(lock, timeout, [this] { return m_core->queue.empty() && !m_core->busy; });
}

std::uint64_t MockDeviceBackend::startedWork() const
{
    const std::lock_guard<std::mutex> held(m_core->mutex);
    return m_core->started;
}

std::uint64_t MockDeviceBackend::queuedWork() const
{
    const std::lock_guard<std::mutex> held(m_core->mutex);
    return static_cast<std::uint64_t>(m_core->queue.size());
}

std::uint64_t MockDeviceBackend::liveAllocations() const
{
    const std::lock_guard<std::mutex> held(m_core->allocationMutex);
    return static_cast<std::uint64_t>(m_core->blocks.size());
}

std::uint64_t MockDeviceBackend::allocationCalls() const
{
    const std::lock_guard<std::mutex> held(m_core->allocationMutex);
    return m_core->allocationCalls;
}

std::uint64_t MockDeviceBackend::liveKernels() const
{
    return m_core->liveKernels->load();
}

EngineContext makeDeviceTestEngine()
{
    EngineContextOptions options;
    options.maxConcurrency = 16u;
    Result<EngineContext> made = EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : EngineContext{};
}

ExecutionContext makeDeviceTestExecution(const EngineContext& engine, ExecutionOptions options)
{
    if (options.maxConcurrency == kUseContextConcurrency)
    {
        options.maxConcurrency = 1u;
    }
    Result<ExecutionContext> made = ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : ExecutionContext{};
}

Device makeMockDevice(const std::shared_ptr<MockDeviceBackend>& backend)
{
    Result<Device> made = Device::create(backend);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : Device{};
}

ImageLayout floatPlaneLayout(std::uint32_t width, std::uint32_t height, std::uint64_t rowAlignmentBytes)
{
    const EngineLimits limits = defaultEngineLimits();
    const std::optional<ImageExtent> extent = ImageExtent::create(width, height, 1u, limits);
    const ImageLayoutRequest request{.extent = extent.value(),
                                     .elementSizeBytes = sizeof(float),
                                     .samplesPerPixel = 1u,
                                     .planeCount = 1u,
                                     .rowAlignmentBytes = rowAlignmentBytes};
    return ImageLayout::create(request, limits).value();
}

DeviceResourceDescriptor planeDescriptor(const ImageLayout& layout, ResourceResidency residency)
{
    return DeviceResourceDescriptor{
        .bytes = layout.totalBytes(), .alignment = 64u, .residency = residency, .layout = layout};
}

DeviceResource makeResource(const Device& device, const DeviceResourceDescriptor& descriptor)
{
    Result<DeviceResource> made = device.createResource(descriptor);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : DeviceResource{};
}

std::vector<float> patternSamples(std::uint32_t width, std::uint32_t height, std::uint32_t seed)
{
    std::vector<float> samples(static_cast<std::size_t>(width) * height);
    std::uint32_t state = seed * 2654435761u + 1u;
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        state = state * 1664525u + 1013904223u;
        const auto whole = static_cast<float>(static_cast<std::int32_t>(state % 20001u) - 10000);
        samples[index] = std::ldexp(whole, -static_cast<int>(state >> 28u));
    }
    if (!samples.empty())
    {
        samples.front() = -0.0f;
    }
    return samples;
}

void writeFloatPlane(const DeviceResource& resource, const ImageLayout& layout, std::span<const float> samples)
{
    Result<DeviceMapping> mapping = resource.map(DeviceAccess::Write);
    ASSERT_TRUE(mapping.hasValue()) << mapping.status().message();
    const std::span<std::byte> bytes = mapping->writableBytes();
    const std::uint32_t width = layout.extent().width();
    for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
    {
        std::memcpy(bytes.data() + *layout.rowByteOffset(0u, y), samples.data() + static_cast<std::size_t>(y) * width,
                    std::size_t{width} * sizeof(float));
    }
}

std::vector<float> readFloatPlane(const DeviceResource& resource, const ImageLayout& layout)
{
    const std::uint32_t width = layout.extent().width();
    std::vector<float> samples(static_cast<std::size_t>(width) * layout.extent().height());
    Result<DeviceMapping> mapping = resource.map(DeviceAccess::Read);
    EXPECT_TRUE(mapping.hasValue()) << mapping.status().message();
    if (!mapping.hasValue())
    {
        return samples;
    }
    const std::span<const std::byte> bytes = mapping->bytes();
    for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
    {
        std::memcpy(samples.data() + static_cast<std::size_t>(y) * width, bytes.data() + *layout.rowByteOffset(0u, y),
                    std::size_t{width} * sizeof(float));
    }
    return samples;
}

std::vector<float> referenceAffine(std::uint32_t width, std::uint32_t height, std::span<const float> samples,
                                   const AffineKernelParameters& parameters)
{
    const EngineContext engine = makeDeviceTestEngine();
    const ExecutionContext execution = makeDeviceTestExecution(engine);
    std::vector<float> input(samples.begin(), samples.end());
    std::vector<float> output(input.size(), 0.0f);
    const std::uint64_t stride = std::uint64_t{width} * sizeof(float);
    const std::optional<PlaneView<const float>> source =
        PlaneView<const float>::create(std::span<const float>(input), width, height, 1u, stride);
    const std::optional<PlaneView<float>> destination =
        PlaneView<float>::create(std::span<float>(output), width, height, 1u, stride);
    EXPECT_TRUE(source.has_value() && destination.has_value());
    if (!source.has_value() || !destination.has_value())
    {
        return output;
    }
    const Result<KernelRunResult> run =
        referencePointwiseAffineKernel(execution, *source, *destination, parameters, KernelValidityView{});
    EXPECT_TRUE(run.hasValue()) << run.status().message();
    return output;
}

PreparedDeviceKernel prepareAffine(const Device& device, const ExecutionContext& execution, const ImageLayout& input,
                                   const ImageLayout& output, const AffineKernelParameters& parameters,
                                   const KernelPlanKey& plan)
{
    const std::array<ImageLayout, 2u> layouts{input, output};
    Result<PreparedDeviceKernel> prepared = device.prepareKernel(
        execution,
        DeviceKernelRequest{.plan = plan, .bindingLayouts = layouts, .arguments = deviceKernelArguments(parameters)});
    EXPECT_TRUE(prepared.hasValue()) << prepared.status().message();
    return prepared.hasValue() ? *prepared : PreparedDeviceKernel{};
}

PreparedDeviceKernel prepareStencil3x3(const Device& device, const ExecutionContext& execution,
                                       const ImageLayout& input, const ImageLayout& output,
                                       const Stencil3x3KernelParameters& parameters)
{
    const std::array<ImageLayout, 2u> layouts{input, output};
    Result<PreparedDeviceKernel> prepared =
        device.prepareKernel(execution, DeviceKernelRequest{.plan = mockStencil3x3Plan(parameters),
                                                            .bindingLayouts = layouts,
                                                            .arguments = deviceKernelArguments(parameters)});
    EXPECT_TRUE(prepared.hasValue()) << prepared.status().message();
    return prepared.hasValue() ? *prepared : PreparedDeviceKernel{};
}

bool sameBits(std::span<const float> left, std::span<const float> right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0u; index < left.size(); ++index)
    {
        if (std::bit_cast<std::uint32_t>(left[index]) != std::bit_cast<std::uint32_t>(right[index]))
        {
            return false;
        }
    }
    return true;
}

} // namespace an::test
