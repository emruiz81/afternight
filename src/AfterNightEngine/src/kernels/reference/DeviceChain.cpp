// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Kernel/DeviceChain.h>

#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/DeviceError.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// A chain is planned entirely from what `Device` already publishes: its capabilities decide where the
// buffers live, its prepared kernels say what each binding needs, and its submissions carry the
// transfers and the dependencies. Nothing here reaches into the device's record, which is deliberate -
// the residency decision is a property of the contract rather than of one implementation of it.

namespace an
{

namespace
{

/// The operation name every refusal from planning carries.
constexpr const char* kCreateOperation = "DeviceChain::create";

/// The operation name every refusal from submission carries.
constexpr const char* kSubmitOperation = "DeviceChain::submit";

/// A handle for the accessors to return when there is nothing to return.
///
/// @return a resource that holds nothing, with the lifetime of the program.
[[nodiscard]] const DeviceResource& noResource() noexcept
{
    static const DeviceResource kNone;
    return kNone;
}

[[nodiscard]] Status refuse(ErrorCode code, DeviceFailure reason, const char* message, const char* operation)
{
    return deviceFailure(code, reason, StatusDetail{.message = message, .operation = operation});
}

/// What the walk over the steps learns about one buffer, before any of it becomes a plan.
struct BufferUse
{
    /// Whether any step binds it.
    bool bound = false;

    /// Whether its bytes exist by the time the step being walked runs: the host supplies it, or an
    /// earlier step wrote it.
    bool produced = false;

    /// Whether an earlier step wrote it, which is what makes a later read a hand-over.
    bool writtenEarlier = false;

    /// Whether any step writes it.
    bool written = false;

    /// Whether a step read it after an earlier step wrote it.
    bool handedOver = false;

    /// The first and last steps that bind it.
    std::uint32_t firstStep = 0u;
    std::uint32_t lastStep = 0u;
};

/// One step of a planned chain: the kernel and where its bindings sit in the flattened list.
struct ChainStepRecord
{
    /// The prepared kernel, whose copy keeps the device's kernel outstanding.
    PreparedDeviceKernel kernel{};

    /// The first of this step's bindings in `DeviceChainState::bindings`.
    std::uint32_t firstBinding = 0u;

    /// How many bindings it has.
    std::uint32_t bindingCount = 0u;
};

} // namespace

/// A planned chain: what was decided, the buffers it holds, and the steps it will submit.
class DeviceChainState
{
public:
    /// What the planner decided, returned to callers as it stands.
    DeviceChainPlan plan{};

    /// Every step's bindings, one step's after another's.
    std::vector<DeviceChainBinding> bindings{};

    /// The steps, in the order they run.
    std::vector<ChainStepRecord> steps{};

    /// The resource a kernel binds for each buffer, in the request's order.
    std::vector<DeviceResource> buffers{};

    /// The host-addressable staging for each buffer, or a handle that holds nothing where there is
    /// none: for every intermediate, and for every buffer when the host shares the device's memory.
    std::vector<DeviceResource> staging{};

    /// The buffers whose bytes are uploaded before the first step, in the request's order.
    std::vector<std::uint32_t> uploads{};

    /// The buffers whose bytes are downloaded after the last step, in the request's order.
    std::vector<std::uint32_t> downloads{};

    /// The device the chain was planned for.
    std::uint64_t deviceId = 0u;
};

const char* deviceChainBufferRoleName(DeviceChainBufferRole role) noexcept
{
    switch (role)
    {
    case DeviceChainBufferRole::Input:
        return "Input";
    case DeviceChainBufferRole::Intermediate:
        return "Intermediate";
    case DeviceChainBufferRole::Output:
        return "Output";
    }
    return "UnknownDeviceChainBufferRole";
}

namespace
{

/// Checks one step's bindings against its kernel and against what the chain knows so far, and records
/// what the step does to each buffer.
[[nodiscard]] Status walkStep(std::uint32_t stepIndex, const DeviceChainStep& step, std::uint64_t deviceId,
                              std::span<const DeviceChainBuffer> buffers, std::vector<BufferUse>& uses)
{
    if (step.kernel == nullptr || !step.kernel->valid() || step.kernel->kernel() == nullptr)
    {
        return refuse(ErrorCode::InvalidArgument, DeviceFailure::InvalidKernel, "a chain step names no kernel",
                      kCreateOperation);
    }
    if (step.kernel->deviceId() != deviceId)
    {
        return refuse(ErrorCode::IncompatibleArguments, DeviceFailure::ForeignDevice,
                      "a chain step names a kernel another device prepared", kCreateOperation);
    }
    const std::span<const DeviceBindingRequirement> requirements = step.kernel->kernel()->bindings();
    if (step.bindings.size() != requirements.size())
    {
        return refuse(ErrorCode::InvalidArgument, DeviceFailure::BindingMismatch,
                      "a chain step must bind exactly as many buffers as its kernel declares", kCreateOperation);
    }

    for (std::size_t index = 0u; index < step.bindings.size(); ++index)
    {
        if (step.bindings[index].buffer >= buffers.size())
        {
            return refuse(ErrorCode::OutOfRange, DeviceFailure::BindingMismatch,
                          "a chain step binds a buffer the chain does not have", kCreateOperation);
        }
        for (std::size_t earlier = 0u; earlier < index; ++earlier)
        {
            if (step.bindings[earlier].buffer == step.bindings[index].buffer)
            {
                return refuse(ErrorCode::Aliasing, DeviceFailure::BindingAliased,
                              "a chain step binds one buffer to two of its kernel's bindings", kCreateOperation);
            }
        }
    }

    for (std::size_t index = 0u; index < step.bindings.size(); ++index)
    {
        const DeviceChainBinding& binding = step.bindings[index];
        const DeviceBindingRequirement& requirement = requirements[index];
        const DeviceChainBuffer& buffer = buffers[binding.buffer];
        if (binding.access != requirement.access)
        {
            return refuse(ErrorCode::InvalidArgument, DeviceFailure::BindingMismatch,
                          "a chain binding's access is not the one its kernel declares", kCreateOperation);
        }
        if (requirement.layout.has_value() && *requirement.layout != buffer.layout)
        {
            return refuse(ErrorCode::InvalidArgument, DeviceFailure::BindingMismatch,
                          "a chain binding's buffer is not laid out as its kernel declares", kCreateOperation);
        }
        if (buffer.layout.totalBytes() < requirement.minimumBytes)
        {
            return refuse(ErrorCode::InvalidArgument, DeviceFailure::BindingMismatch,
                          "a chain binding's buffer is smaller than its kernel declares", kCreateOperation);
        }
        if (buffer.alignment < requirement.alignment)
        {
            return refuse(ErrorCode::InvalidArgument, DeviceFailure::BindingMismatch,
                          "a chain binding's buffer is aligned less widely than its kernel declares", kCreateOperation);
        }

        BufferUse& use = uses[binding.buffer];
        if (!use.bound)
        {
            use.bound = true;
            use.firstStep = stepIndex;
        }
        use.lastStep = stepIndex;
        if (accessReads(binding.access))
        {
            if (!use.produced)
            {
                return refuse(ErrorCode::InvalidState, DeviceFailure::UnproducedBuffer,
                              "a chain step reads a buffer no earlier step wrote and the host does not supply",
                              kCreateOperation);
            }
            use.handedOver = use.handedOver || use.writtenEarlier;
        }
    }

    // A buffer written by this step is produced for the steps after it, never for this one: a kernel
    // that reads and writes one buffer still needs what was there before it ran.
    for (const DeviceChainBinding& binding : step.bindings)
    {
        if (accessWrites(binding.access))
        {
            BufferUse& use = uses[binding.buffer];
            use.produced = true;
            use.writtenEarlier = true;
            use.written = true;
        }
    }
    return Status::success();
}

} // namespace

Result<DeviceChain> DeviceChain::create(const Device& device, const DeviceChainRequest& request)
{
    if (!device.valid() || device.capabilities() == nullptr)
    {
        return refuse(ErrorCode::NotInitialized, DeviceFailure::DeviceShutDown, "the device handle holds nothing",
                      kCreateOperation);
    }
    if (request.steps.empty() || request.buffers.empty())
    {
        return refuse(ErrorCode::InvalidArgument, DeviceFailure::EmptySubmission,
                      "a chain must declare at least one buffer and at least one step", kCreateOperation);
    }
    const DeviceCapabilities& capabilities = *device.capabilities();

    try
    {
        std::vector<BufferUse> uses(request.buffers.size());
        for (std::size_t index = 0u; index < request.buffers.size(); ++index)
        {
            uses[index].produced = request.buffers[index].role == DeviceChainBufferRole::Input;
        }
        for (std::size_t index = 0u; index < request.steps.size(); ++index)
        {
            const Status walked =
                walkStep(static_cast<std::uint32_t>(index), request.steps[index], device.id(), request.buffers, uses);
            if (walked.failed())
            {
                return walked;
            }
        }
        for (std::size_t index = 0u; index < request.buffers.size(); ++index)
        {
            if (!uses[index].bound)
            {
                return refuse(ErrorCode::InvalidState, DeviceFailure::UnusedBuffer,
                              "a chain declares a buffer no step binds", kCreateOperation);
            }
            if (request.buffers[index].role == DeviceChainBufferRole::Output && !uses[index].written)
            {
                return refuse(ErrorCode::InvalidState, DeviceFailure::UnproducedBuffer,
                              "a chain names an output no step writes", kCreateOperation);
            }
        }

        // Where the buffers live. A device with memory of its own keeps them there and stages what the
        // host supplies or reads; a device whose memory the host already shares keeps them where both
        // sides can reach them, and there is nothing to transfer.
        const ResourceResidency residency =
            capabilities.deviceResidency ? ResourceResidency::Device : ResourceResidency::Shared;
        const bool stages = residency == ResourceResidency::Device;
        ResourceResidency stagingResidency = ResourceResidency::Pinned;
        if (stages)
        {
            if (capabilities.pinnedResidency)
            {
                stagingResidency = ResourceResidency::Pinned;
            }
            else if (capabilities.hostResidency)
            {
                stagingResidency = ResourceResidency::Host;
            }
            else
            {
                for (const DeviceChainBuffer& buffer : request.buffers)
                {
                    if (buffer.role != DeviceChainBufferRole::Intermediate)
                    {
                        return refuse(ErrorCode::Unsupported, DeviceFailure::ResidencyNotOffered,
                                      "the device stages neither host nor pinned memory for a chain's inputs and "
                                      "outputs",
                                      kCreateOperation);
                    }
                }
            }
        }

        auto state = std::make_shared<DeviceChainState>();
        state->deviceId = device.id();
        state->plan.buffers.resize(request.buffers.size());
        state->buffers.resize(request.buffers.size());
        state->staging.resize(request.buffers.size());
        DeviceChainAccounting& accounting = state->plan.accounting;

        for (std::size_t index = 0u; index < request.buffers.size(); ++index)
        {
            const DeviceChainBuffer& buffer = request.buffers[index];
            const BufferUse& use = uses[index];
            const bool staged = stages && buffer.role != DeviceChainBufferRole::Intermediate;
            DeviceChainBufferPlan& planned = state->plan.buffers[index];
            planned.role = buffer.role;
            planned.residency = residency;
            planned.uploaded = staged && buffer.role == DeviceChainBufferRole::Input;
            planned.downloaded = staged && buffer.role == DeviceChainBufferRole::Output;
            planned.resident = use.handedOver;
            planned.firstStep = use.firstStep;
            planned.lastStep = use.lastStep;
            planned.bytes = buffer.layout.totalBytes();

            Result<DeviceResource> made = device.createResource(DeviceResourceDescriptor{.bytes = planned.bytes,
                                                                                         .alignment = buffer.alignment,
                                                                                         .residency = residency,
                                                                                         .layout = buffer.layout});
            if (!made.hasValue())
            {
                return made.status();
            }
            state->buffers[index] = std::move(*made);
            if (staged)
            {
                Result<DeviceResource> host =
                    device.createResource(DeviceResourceDescriptor{.bytes = planned.bytes,
                                                                   .alignment = buffer.alignment,
                                                                   .residency = stagingResidency,
                                                                   .layout = buffer.layout});
                if (!host.hasValue())
                {
                    return host.status();
                }
                state->staging[index] = std::move(*host);
                accounting.hostBytes += planned.bytes;
            }

            accounting.deviceBytes += planned.bytes;
            if (planned.uploaded)
            {
                state->uploads.push_back(static_cast<std::uint32_t>(index));
                ++accounting.uploads;
                accounting.uploadBytes += planned.bytes;
            }
            if (planned.downloaded)
            {
                state->downloads.push_back(static_cast<std::uint32_t>(index));
                ++accounting.downloads;
                accounting.downloadBytes += planned.bytes;
            }
            if (planned.resident)
            {
                ++accounting.residentHandovers;
                accounting.residentBytes += planned.bytes;
                if (stages)
                {
                    accounting.avoidedTransfers += 2u;
                    accounting.avoidedBytes += 2u * planned.bytes;
                }
            }
        }

        state->steps.reserve(request.steps.size());
        for (const DeviceChainStep& step : request.steps)
        {
            ChainStepRecord record;
            record.kernel = *step.kernel;
            record.firstBinding = static_cast<std::uint32_t>(state->bindings.size());
            record.bindingCount = static_cast<std::uint32_t>(step.bindings.size());
            state->bindings.insert(state->bindings.end(), step.bindings.begin(), step.bindings.end());
            state->steps.push_back(std::move(record));
        }
        accounting.submissions = static_cast<std::uint32_t>(request.steps.size()) + (state->uploads.empty() ? 0u : 1u) +
                                 (state->downloads.empty() ? 0u : 1u);

        return DeviceChain(std::move(state));
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

std::uint64_t DeviceChain::deviceId() const noexcept
{
    return m_state != nullptr ? m_state->deviceId : 0u;
}

const DeviceChainPlan* DeviceChain::plan() const noexcept
{
    return m_state != nullptr ? &m_state->plan : nullptr;
}

const DeviceResource& DeviceChain::buffer(std::uint32_t index) const noexcept
{
    if (m_state == nullptr || index >= m_state->buffers.size())
    {
        return noResource();
    }
    return m_state->buffers[index];
}

const DeviceResource& DeviceChain::hostBuffer(std::uint32_t index) const noexcept
{
    if (m_state == nullptr || index >= m_state->buffers.size())
    {
        return noResource();
    }
    if (m_state->plan.buffers[index].role == DeviceChainBufferRole::Intermediate)
    {
        return noResource();
    }
    return m_state->staging[index].valid() ? m_state->staging[index] : m_state->buffers[index];
}

Result<DeviceChainRun> DeviceChain::submit(const Device& device, const ExecutionContext& execution) const
{
    if (m_state == nullptr)
    {
        return refuse(ErrorCode::NotInitialized, DeviceFailure::DeviceShutDown, "the chain holds nothing",
                      kSubmitOperation);
    }
    if (!device.valid())
    {
        return refuse(ErrorCode::NotInitialized, DeviceFailure::DeviceShutDown, "the device handle holds nothing",
                      kSubmitOperation);
    }
    if (device.id() != m_state->deviceId)
    {
        return refuse(ErrorCode::IncompatibleArguments, DeviceFailure::ForeignDevice,
                      "a chain runs on the device it was planned for", kSubmitOperation);
    }

    try
    {
        const DeviceChainState& state = *m_state;
        DeviceChainRun run;
        run.events.reserve(state.plan.accounting.submissions);
        std::array<DeviceEvent, 1u> dependency{};

        // Every piece of work waits for the one before it, so the device's rule that work declares what
        // it depends on is satisfied by the chain rather than by whoever built it.
        const auto submitOne = [&](const DeviceSubmission& submission) {
            DeviceSubmission declared = submission;
            if (!run.events.empty())
            {
                dependency[0] = run.events.back();
                declared.dependencies = dependency;
            }
            Result<DeviceEvent> accepted = device.submit(execution, declared);
            if (accepted.hasValue())
            {
                run.events.push_back(*accepted);
            }
            return accepted.status();
        };

        const auto transfersFor = [&](std::span<const std::uint32_t> indices, bool toDevice) {
            std::vector<DeviceTransfer> transfers;
            transfers.reserve(indices.size());
            for (const std::uint32_t index : indices)
            {
                const DeviceResource& host = state.staging[index];
                const DeviceResource& onDevice = state.buffers[index];
                transfers.push_back(DeviceTransfer{.source = toDevice ? &host : &onDevice,
                                                   .destination = toDevice ? &onDevice : &host,
                                                   .bytes = state.plan.buffers[index].bytes});
            }
            return transfers;
        };

        if (!state.uploads.empty())
        {
            const std::vector<DeviceTransfer> transfers = transfersFor(state.uploads, true);
            const Status accepted = submitOne(DeviceSubmission{.transfersBefore = transfers});
            if (accepted.failed())
            {
                return accepted;
            }
        }

        std::vector<DeviceBinding> bound;
        for (const ChainStepRecord& step : state.steps)
        {
            bound.clear();
            bound.reserve(step.bindingCount);
            for (std::uint32_t index = 0u; index < step.bindingCount; ++index)
            {
                const DeviceChainBinding& binding = state.bindings[step.firstBinding + index];
                bound.push_back(DeviceBinding{.resource = &state.buffers[binding.buffer], .access = binding.access});
            }
            const Status accepted = submitOne(DeviceSubmission{.kernel = &step.kernel, .bindings = bound});
            if (accepted.failed())
            {
                if (run.events.empty())
                {
                    return accepted;
                }
                run.submission = accepted;
                return run;
            }
        }

        if (!state.downloads.empty())
        {
            const std::vector<DeviceTransfer> transfers = transfersFor(state.downloads, false);
            const Status accepted = submitOne(DeviceSubmission{.transfersBefore = transfers});
            if (accepted.failed())
            {
                run.submission = accepted;
            }
        }
        return run;
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

DeviceChainReport awaitDeviceChain(const DeviceChainRun& run, const ExecutionContext& execution) noexcept
{
    DeviceChainReport report;
    for (std::size_t index = 0u; index < run.events.size(); ++index)
    {
        const DeviceEvent& event = run.events[index];
        std::optional<Status> outcome = event.outcome();
        if (!outcome.has_value())
        {
            const Status waited = event.wait(execution);
            outcome = event.outcome();
            if (!outcome.has_value())
            {
                // The wait was stopped rather than the work, which leaves the work alone.
                if (report.failedWork == kDeviceChainNoWork)
                {
                    report.outcome = waited;
                    report.failedWork = static_cast<std::uint32_t>(index);
                }
                return report;
            }
        }
        const DeviceWorkTiming timing = event.timing();
        report.transfersPerformed += timing.transfersRecorded;
        report.kernelsRun += timing.kernelRecorded ? 1u : 0u;
        report.transferNanoseconds += timing.transferNanoseconds;
        report.kernelNanoseconds += timing.kernelNanoseconds;
        if (outcome->ok())
        {
            ++report.succeededWork;
        }
        else if (report.failedWork == kDeviceChainNoWork)
        {
            report.outcome = *outcome;
            report.failedWork = static_cast<std::uint32_t>(index);
        }
    }
    if (report.failedWork == kDeviceChainNoWork && run.submission.failed())
    {
        report.outcome = run.submission;
        report.failedWork = static_cast<std::uint32_t>(run.events.size());
    }
    return report;
}

} // namespace an
