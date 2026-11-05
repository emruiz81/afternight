// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "kernels/reference/DeviceState.h"

#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/DeviceError.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <new>
#include <span>
#include <utility>

// The checks and the lifecycle behind `Device`. See DeviceState.h for the rules the records follow.
//
// Every check a submission faces runs before anything is accepted, in the order `Device::submit`
// documents, so a submission with several problems is always refused for the same one. Only the checks
// that depend on other work - mappings and pending use - need the device's lock, and they run last,
// after everything that can be decided from the submission alone.

namespace an
{

namespace
{

/// How one submission uses one resource, across its transfers and bindings together.
struct ResourceUse
{
    std::shared_ptr<DeviceResourceState> state;
    bool reads = false;
    bool writes = false;
};

void noteUse(std::vector<ResourceUse>& uses, const std::shared_ptr<DeviceResourceState>& state, bool reads, bool writes)
{
    for (ResourceUse& use : uses)
    {
        if (use.state == state)
        {
            use.reads = use.reads || reads;
            use.writes = use.writes || writes;
            return;
        }
    }
    uses.push_back(ResourceUse{.state = state, .reads = reads, .writes = writes});
}

/// Whether `count` bytes starting at `offset` lie inside `size` bytes, without overflowing.
[[nodiscard]] constexpr bool rangeFits(std::uint64_t offset, std::uint64_t count, std::uint64_t size) noexcept
{
    return offset <= size && count <= size - offset;
}

[[nodiscard]] bool isUnfinished(const std::shared_ptr<DeviceEventState>& event) noexcept
{
    return event != nullptr && event->phase() != DeviceWorkPhase::Finished;
}

/// Whether depending on `dependencies` waits for `target`, directly or through their own dependencies.
[[nodiscard]] bool declared(const std::vector<std::shared_ptr<DeviceEventState>>& dependencies,
                            const DeviceEventState* target)
{
    return std::any_of(
        dependencies.begin(), dependencies.end(),
        [target](const std::shared_ptr<DeviceEventState>& dependency) { return dependency->reaches(target); });
}

[[nodiscard]] Status submissionTooLarge(const char* parameter, std::uint64_t requested, std::uint64_t limit,
                                        const char* operation)
{
    return deviceFailure(ErrorCode::LimitExceeded, DeviceFailure::SubmissionTooLarge,
                         StatusDetail{.message = "the submission declares more than the device accepts",
                                      .operation = operation,
                                      .context = {StatusContextField::ofText(ContextKey::Parameter, parameter),
                                                  StatusContextField::ofCount(ContextKey::Requested, requested),
                                                  StatusContextField::ofCount(ContextKey::Limit, limit)}});
}

[[nodiscard]] Status bindingMismatch(const char* message, const char* operation)
{
    return deviceFailure(ErrorCode::InvalidArgument, DeviceFailure::BindingMismatch,
                         StatusDetail{.message = message,
                                      .operation = operation,
                                      .context = {StatusContextField::ofText(ContextKey::Parameter, "bindings")}});
}

/// The record behind a resource a submission names, or why the submission may not name it.
[[nodiscard]] Result<std::shared_ptr<DeviceResourceState>>
resolveResource(const DeviceResource* resource, std::uint64_t deviceId, const char* operation)
{
    std::shared_ptr<DeviceResourceState> state =
        resource != nullptr ? DeviceResourceState::share(*resource) : std::shared_ptr<DeviceResourceState>{};
    if (state == nullptr)
    {
        return deviceFailure(ErrorCode::InvalidState, DeviceFailure::ResourceReleased,
                             StatusDetail{.message = "the submission names a resource handle that holds nothing",
                                          .operation = operation});
    }
    if (state->deviceId() != deviceId)
    {
        return deviceFailure(
            ErrorCode::IncompatibleArguments, DeviceFailure::ForeignDevice,
            StatusDetail{.message = "the submission names a resource of a different device", .operation = operation});
    }
    return state;
}

/// Checks one declared transfer and resolves it to the allocations a backend copies between.
[[nodiscard]] Status resolveTransfer(const DeviceTransfer& transfer, std::uint64_t deviceId,
                                     std::vector<DeviceTransferStep>& steps, std::vector<ResourceUse>& uses,
                                     const char* operation)
{
    Result<std::shared_ptr<DeviceResourceState>> source = resolveResource(transfer.source, deviceId, operation);
    if (!source.hasValue())
    {
        return source.status();
    }
    Result<std::shared_ptr<DeviceResourceState>> destination =
        resolveResource(transfer.destination, deviceId, operation);
    if (!destination.hasValue())
    {
        return destination.status();
    }

    const DeviceResourceDescriptor& from = (*source)->descriptor();
    const DeviceResourceDescriptor& to = (*destination)->descriptor();
    if (transfer.bytes == 0u || !rangeFits(transfer.sourceOffset, transfer.bytes, from.bytes) ||
        !rangeFits(transfer.destinationOffset, transfer.bytes, to.bytes))
    {
        return deviceFailure(
            ErrorCode::OutOfRange, DeviceFailure::TransferOutOfRange,
            StatusDetail{.message = "a transfer must copy at least one byte and name only bytes inside both resources",
                         .operation = operation,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, transfer.bytes)}});
    }
    // Both ranges were just proved to fit, so neither end below can overflow.
    if (*source == *destination && transfer.sourceOffset < transfer.destinationOffset + transfer.bytes &&
        transfer.destinationOffset < transfer.sourceOffset + transfer.bytes)
    {
        return deviceFailure(ErrorCode::Aliasing, DeviceFailure::TransferOverlap,
                             StatusDetail{.message = "a transfer reads and writes overlapping bytes of one resource",
                                          .operation = operation});
    }

    steps.push_back(DeviceTransferStep{.source = DeviceAllocation{.handle = (*source)->allocation()},
                                       .sourceResidency = from.residency,
                                       .sourceOffset = transfer.sourceOffset,
                                       .destination = DeviceAllocation{.handle = (*destination)->allocation()},
                                       .destinationResidency = to.residency,
                                       .destinationOffset = transfer.destinationOffset,
                                       .bytes = transfer.bytes});
    noteUse(uses, *source, true, false);
    noteUse(uses, *destination, false, true);
    return Status::success();
}

/// The checks that depend on other work. Called under the device's lock.
[[nodiscard]] Status checkUses(const std::vector<ResourceUse>& uses,
                               const std::vector<std::shared_ptr<DeviceEventState>>& dependencies,
                               const char* operation) noexcept
{
    try
    {
        for (const ResourceUse& use : uses)
        {
            const DeviceResourceState& state = *use.state;
            if (state.mapped)
            {
                return deviceFailure(ErrorCode::Busy, DeviceFailure::ResourceMapped,
                                     StatusDetail{.message = "the submission uses a resource the host has mapped",
                                                  .operation = operation});
            }
            if (isUnfinished(state.lastWriter) && !declared(dependencies, state.lastWriter.get()))
            {
                return deviceFailure(ErrorCode::InvalidState, DeviceFailure::UndeclaredDependency,
                                     StatusDetail{.message = "the submission uses a resource that unfinished work "
                                                             "writes, without depending on that work",
                                                  .operation = operation});
            }
            if (!use.writes)
            {
                continue;
            }
            for (const std::shared_ptr<DeviceEventState>& reader : state.readers)
            {
                if (isUnfinished(reader) && !declared(dependencies, reader.get()))
                {
                    return deviceFailure(ErrorCode::InvalidState, DeviceFailure::UndeclaredDependency,
                                         StatusDetail{.message = "the submission writes a resource that unfinished "
                                                                 "work reads, without depending on that work",
                                                      .operation = operation});
                }
            }
        }
        return Status::success();
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

/// Which tally a performed transfer belongs to: whether an end of it is device memory decides.
[[nodiscard]] DeviceTransferTally& tallyFor(DeviceCounters& counters, const DeviceTransferStep& step) noexcept
{
    const bool fromDevice = step.sourceResidency == ResourceResidency::Device;
    const bool toDevice = step.destinationResidency == ResourceResidency::Device;
    if (toDevice && !fromDevice)
    {
        return counters.uploads;
    }
    if (fromDevice && !toDevice)
    {
        return counters.downloads;
    }
    return fromDevice ? counters.deviceCopies : counters.hostCopies;
}

/// Counts the device memory a prepared kernel holds as one allocation of that size. A kernel that holds none
/// adds nothing, so the allocation count stays the number of things holding memory.
void reserveKernelMemory(MemoryCounters& memory, std::uint64_t residentBytes) noexcept
{
    if (residentBytes == 0u)
    {
        return;
    }
    memory.liveBytes += residentBytes;
    ++memory.liveAllocations;
    memory.peakBytes = (std::max)(memory.peakBytes, memory.liveBytes);
    memory.totalBytes += residentBytes;
    ++memory.totalAllocations;
}

/// Whether a kernel a backend prepared agrees with the request and with the device's limits.
[[nodiscard]] Status checkPreparedKernel(const std::shared_ptr<const DeviceKernel>& kernel,
                                         const DeviceKernelRequest& request, const DeviceCapabilities& capabilities,
                                         const char* operation)
{
    const auto incoherent = [operation](const char* message) {
        return deviceFailure(ErrorCode::Internal, DeviceFailure::InvalidKernel,
                             StatusDetail{.message = message, .operation = operation});
    };
    if (kernel == nullptr)
    {
        return incoherent("the backend returned no kernel");
    }
    if (!(kernel->plan() == request.plan))
    {
        return incoherent("the backend prepared a kernel for a different plan");
    }
    const std::span<const DeviceBindingRequirement> requirements = kernel->bindings();
    if (requirements.empty() || requirements.size() > capabilities.maximumBindings)
    {
        return incoherent("the backend prepared a kernel with no bindings, or more than the device accepts");
    }
    for (const DeviceBindingRequirement& requirement : requirements)
    {
        const bool knownAccess = requirement.access == DeviceAccess::Read ||
                                 requirement.access == DeviceAccess::Write ||
                                 requirement.access == DeviceAccess::ReadWrite;
        if (!knownAccess || requirement.minimumBytes == 0u || !isPowerOfTwo(requirement.alignment) ||
            (requirement.layout.has_value() && requirement.layout->totalBytes() > requirement.minimumBytes))
        {
            return incoherent("the backend prepared a kernel with an incoherent binding requirement");
        }
    }
    return Status::success();
}

} // namespace

void DeviceStopHook::operator()() const noexcept
{
    if (const std::shared_ptr<DeviceWorkRecord> locked = record.lock())
    {
        DeviceState::stop(locked);
    }
}

PreparedKernelRecord::PreparedKernelRecord(std::shared_ptr<DeviceState> owner,
                                           std::shared_ptr<const DeviceKernel> prepared,
                                           std::uint64_t residentBytes) noexcept
    : m_device(std::move(owner)), m_kernel(std::move(prepared)), m_residentBytes(residentBytes)
{
}

PreparedKernelRecord::~PreparedKernelRecord()
{
    // The backend's kernel goes before the device is told, so a shutdown that sees no kernel outstanding
    // never releases a backend a kernel still refers to, and the memory it held is no longer counted only
    // once it has actually been returned.
    m_kernel.reset();
    m_device->kernelReleased(m_residentBytes);
}

DeviceWorkRecord::DeviceWorkRecord(const std::shared_ptr<DeviceState>& owner,
                                   std::shared_ptr<DeviceEventState> observed, std::stop_token token,
                                   std::optional<std::chrono::steady_clock::time_point> expiry,
                                   std::size_t dependencyCount) noexcept
    : event(std::move(observed)), stopToken(std::move(token)), deadline(expiry), deviceObserver(owner), device(owner),
      remainingDependencies(dependencyCount + 1u)
{
}

DeviceWorkRecord::~DeviceWorkRecord() = default;

void DeviceWorkRecord::onEventFinished(const DeviceEventState& dependency) noexcept
{
    const std::optional<Status> dependencyOutcome = dependency.outcome();
    bool ready = false;
    {
        const std::lock_guard<std::mutex> held(mutex);
        if (stage == DeviceWorkStage::HandedOff || stage == DeviceWorkStage::Finished)
        {
            return;
        }
        if (dependencyOutcome.has_value() && dependencyOutcome->failed() && !dependencyFailure.has_value())
        {
            dependencyFailure = *dependencyOutcome;
        }
        if (remainingDependencies > 0u)
        {
            --remainingDependencies;
        }
        ready = stage == DeviceWorkStage::Waiting && (remainingDependencies == 0u || dependencyFailure.has_value());
    }
    if (ready)
    {
        DeviceState::advance(shared_from_this());
    }
}

Status DeviceWorkRecord::checkCancellation() const noexcept
{
    if (stopToken.stop_requested())
    {
        return Status::failure(ErrorCode::Cancelled);
    }
    if (deadline.has_value() && std::chrono::steady_clock::now() >= *deadline)
    {
        return Status::failure(ErrorCode::DeadlineExceeded);
    }
    return Status::success();
}

DeviceState::DeviceState(std::uint64_t identity, std::shared_ptr<DeviceBackend> implementation,
                         DeviceCapabilities offered) noexcept
    : m_backend(std::move(implementation)), m_capabilities(std::move(offered)), m_id(identity)
{
}

DeviceState::~DeviceState() = default;

Result<DeviceResource> DeviceState::createResource(const DeviceResourceDescriptor& descriptor)
{
    constexpr const char* kOperation = "Device::createResource";
    sweepRetired();

    Status invalid = checkDescriptor(descriptor, kOperation);
    if (invalid.failed())
    {
        return invalid;
    }
    Result<DeviceAllocation> allocation = allocate(descriptor, kOperation);
    if (!allocation.hasValue())
    {
        return allocation.status();
    }

    std::uint64_t resourceId = 0u;
    {
        const std::lock_guard<std::mutex> held(m_mutex);
        resourceId = ++m_lastResourceId;
        ++m_counters.liveResources;
    }
    try
    {
        return DeviceResourceState::makeHandle(std::make_shared<DeviceResourceState>(
            shared_from_this(), resourceId, m_id, descriptor, allocation->handle));
    }
    catch (const std::bad_alloc&)
    {
        {
            const std::lock_guard<std::mutex> held(m_mutex);
            --m_counters.liveResources;
        }
        releaseAllocation(*allocation, descriptor);
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

Result<DeviceAllocation> DeviceState::allocate(const DeviceResourceDescriptor& descriptor, const char* operation)
{
    const bool deviceSide = isDeviceReachable(descriptor.residency);
    {
        const std::lock_guard<std::mutex> held(m_mutex);
        MemoryCounters& memory = deviceSide ? m_counters.deviceMemory : m_counters.hostMemory;
        if (deviceSide)
        {
            const std::uint64_t limit = m_capabilities.memoryLimitBytes;
            const std::uint64_t available = limit > memory.liveBytes ? limit - memory.liveBytes : 0u;
            if (descriptor.bytes > available)
            {
                ++memory.deniedAllocations;
                return Status::failure(
                    ErrorCode::MemoryBudgetExceeded,
                    StatusDetail{.message = "the device's memory limit does not admit this allocation",
                                 .operation = operation,
                                 .domain = ErrorDomain::Runtime,
                                 .context = {StatusContextField::ofCount(ContextKey::Requested, descriptor.bytes),
                                             StatusContextField::ofCount(ContextKey::Available, available),
                                             StatusContextField::ofCount(ContextKey::Limit, limit)}});
            }
        }
        // Counted before the backend is asked, so two concurrent requests cannot both fit a limit that
        // admits only one of them.
        memory.liveBytes += descriptor.bytes;
        ++memory.liveAllocations;
        memory.peakBytes = (std::max)(memory.peakBytes, memory.liveBytes);
    }

    std::optional<Result<DeviceAllocation>> allocated;
    const Status thrown = callAcrossBoundary(
        boundary(operation), [this, &allocated, &descriptor] { allocated.emplace(m_backend->allocate(descriptor)); });
    Status failure;
    if (thrown.failed())
    {
        failure = deviceFailure(thrown.code(), DeviceFailure::BackendFailure,
                                StatusDetail{.message = "the backend threw while allocating", .operation = operation},
                                thrown);
    }
    else if (!allocated->hasValue())
    {
        failure = deviceFailure(allocated->code(), DeviceFailure::BackendFailure,
                                StatusDetail{.message = "the backend refused the allocation", .operation = operation},
                                allocated->status());
    }
    else if ((**allocated).handle == 0u)
    {
        failure = deviceFailure(
            ErrorCode::Internal, DeviceFailure::BackendFailure,
            StatusDetail{.message = "the backend returned the null allocation handle", .operation = operation});
    }

    const std::lock_guard<std::mutex> held(m_mutex);
    MemoryCounters& memory = deviceSide ? m_counters.deviceMemory : m_counters.hostMemory;
    if (failure.failed())
    {
        memory.liveBytes -= descriptor.bytes;
        --memory.liveAllocations;
        ++memory.deniedAllocations;
        return failure;
    }
    memory.totalBytes += descriptor.bytes;
    ++memory.totalAllocations;
    return **allocated;
}

void DeviceState::releaseAllocation(DeviceAllocation allocation, const DeviceResourceDescriptor& descriptor) noexcept
{
    m_backend->release(allocation, descriptor);
    const std::lock_guard<std::mutex> held(m_mutex);
    MemoryCounters& memory = isDeviceReachable(descriptor.residency) ? m_counters.deviceMemory : m_counters.hostMemory;
    memory.liveBytes -= descriptor.bytes;
    --memory.liveAllocations;
}

Status DeviceState::checkDescriptor(const DeviceResourceDescriptor& descriptor, const char* operation) const
{
    const auto invalid = [operation](const char* message, const char* parameter) {
        return deviceFailure(ErrorCode::InvalidArgument, DeviceFailure::InvalidDescriptor,
                             StatusDetail{.message = message,
                                          .operation = operation,
                                          .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
    };
    if (descriptor.bytes == 0u)
    {
        return invalid("a resource must hold at least one byte", "bytes");
    }
    if (!isPowerOfTwo(descriptor.alignment))
    {
        return invalid("a resource's alignment must be a power of two", "alignment");
    }
    if (static_cast<std::size_t>(descriptor.residency) >= kResourceResidencyCount)
    {
        return invalid("the residency is not one this engine knows", "residency");
    }
    if (descriptor.layout.has_value())
    {
        if (descriptor.layout->totalBytes() > descriptor.bytes)
        {
            return invalid("the resource holds fewer bytes than its layout names", "layout");
        }
        if (descriptor.alignment < descriptor.layout->elementSizeBytes())
        {
            return invalid("the resource's alignment is narrower than one sample of its layout", "alignment");
        }
    }
    if (!offersResidency(m_capabilities, descriptor.residency))
    {
        return deviceFailure(ErrorCode::Unsupported, DeviceFailure::ResidencyNotOffered,
                             StatusDetail{.message = "the device does not create resources of this residency",
                                          .operation = operation,
                                          .context = {StatusContextField::ofText(
                                              ContextKey::Parameter, resourceResidencyName(descriptor.residency))}});
    }
    if (descriptor.bytes > m_capabilities.maximumResourceBytes)
    {
        return deviceFailure(
            ErrorCode::LimitExceeded, DeviceFailure::ResourceTooLarge,
            StatusDetail{
                .message = "the resource is larger than the device accepts for one resource",
                .operation = operation,
                .context = {StatusContextField::ofText(ContextKey::Parameter, "bytes"),
                            StatusContextField::ofCount(ContextKey::Requested, descriptor.bytes),
                            StatusContextField::ofCount(ContextKey::Limit, m_capabilities.maximumResourceBytes)}});
    }
    if (descriptor.alignment > m_capabilities.maximumAlignmentBytes)
    {
        return deviceFailure(
            ErrorCode::LimitExceeded, DeviceFailure::ResourceTooLarge,
            StatusDetail{
                .message = "the resource asks for a wider alignment than the device accepts",
                .operation = operation,
                .context = {StatusContextField::ofText(ContextKey::Parameter, "alignment"),
                            StatusContextField::ofCount(ContextKey::Requested, descriptor.alignment),
                            StatusContextField::ofCount(ContextKey::Limit, m_capabilities.maximumAlignmentBytes)}});
    }
    return Status::success();
}

Result<DeviceMapping> DeviceState::mapResource(const std::shared_ptr<DeviceResourceState>& resource,
                                               DeviceAccess access)
{
    constexpr const char* kOperation = "DeviceResource::map";
    const DeviceResourceDescriptor& descriptor = resource->descriptor();
    if (!isHostAddressable(descriptor.residency))
    {
        return deviceFailure(ErrorCode::IncompatibleArguments, DeviceFailure::NotHostAddressable,
                             StatusDetail{.message = "the host cannot address device memory; its contents reach the "
                                                     "host through a transfer",
                                          .operation = kOperation});
    }

    {
        const std::lock_guard<std::mutex> held(m_mutex);
        if (resource->mapped)
        {
            return deviceFailure(ErrorCode::Busy, DeviceFailure::ResourceMapped,
                                 StatusDetail{.message = "the resource is already mapped", .operation = kOperation});
        }
        if (isUnfinished(resource->lastWriter))
        {
            return deviceFailure(ErrorCode::Busy, DeviceFailure::PendingUse,
                                 StatusDetail{.message = "unfinished work writes this resource; wait on its event "
                                                         "before mapping it",
                                              .operation = kOperation});
        }
        if (accessWrites(access))
        {
            for (const std::shared_ptr<DeviceEventState>& reader : resource->readers)
            {
                if (isUnfinished(reader))
                {
                    return deviceFailure(ErrorCode::Busy, DeviceFailure::PendingUse,
                                         StatusDetail{.message = "unfinished work reads this resource; wait on its "
                                                                 "event before mapping it for writing",
                                                      .operation = kOperation});
                }
            }
        }
        // Claimed before the backend is asked, so a concurrent submission or mapping sees it mapped.
        resource->mapped = true;
    }

    std::optional<Result<std::byte*>> mapped;
    const Status thrown = callAcrossBoundary(boundary(kOperation), [this, &mapped, &resource, &descriptor, access] {
        mapped.emplace(m_backend->map(DeviceAllocation{.handle = resource->allocation()}, descriptor, access));
    });
    Status failure;
    if (thrown.failed())
    {
        failure =
            deviceFailure(thrown.code(), DeviceFailure::BackendFailure,
                          StatusDetail{.message = "the backend threw while mapping", .operation = kOperation}, thrown);
    }
    else if (!mapped->hasValue())
    {
        failure = deviceFailure(mapped->code(), DeviceFailure::BackendFailure,
                                StatusDetail{.message = "the backend refused the mapping", .operation = kOperation},
                                mapped->status());
    }
    else if (**mapped == nullptr)
    {
        failure = deviceFailure(
            ErrorCode::Internal, DeviceFailure::BackendFailure,
            StatusDetail{.message = "the backend mapped the resource at a null address", .operation = kOperation});
    }

    const std::lock_guard<std::mutex> held(m_mutex);
    if (failure.failed())
    {
        resource->mapped = false;
        return failure;
    }
    ++m_counters.liveMappings;
    return DeviceResourceState::makeMapping(resource, **mapped, access);
}

void DeviceState::unmapResource(DeviceResourceState& resource) noexcept
{
    m_backend->unmap(DeviceAllocation{.handle = resource.allocation()}, resource.descriptor());
    const std::lock_guard<std::mutex> held(m_mutex);
    resource.mapped = false;
    --m_counters.liveMappings;
}

void DeviceState::releaseResource(DeviceResourceState& resource) noexcept
{
    releaseAllocation(DeviceAllocation{.handle = resource.allocation()}, resource.descriptor());
    const std::lock_guard<std::mutex> held(m_mutex);
    --m_counters.liveResources;
}

Result<DeviceKernelSelection> DeviceState::selectKernel(const ExecutionContext& execution,
                                                        const KernelPlanKey& plan) const
{
    constexpr const char* kOperation = "Device::selectKernel";
    if (!execution.valid())
    {
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "a kernel cannot be placed for an execution context that "
                                                       "holds nothing",
                                            .operation = kOperation,
                                            .domain = ErrorDomain::Runtime});
    }

    // Stack buffers, as in the CPU dispatch table's own report: this is on the path into a kernel.
    char message[256] = {};
    const char* const operationName = kernelOperationName(plan.id.operation);
    const ComputeBackend constraint = execution.backend();
    if (constraint == ComputeBackend::Reference || constraint == ComputeBackend::Cpu)
    {
        std::snprintf(message, sizeof(message),
                      "kernel %s runs on the host: the execution is constrained to the %s backend", operationName,
                      computeBackendName(constraint));
        execution.reportDiagnostic(DiagnosticSeverity::Debug, ErrorDomain::Runtime, kOperation, message);
        return DeviceKernelSelection{.dispatch = DeviceDispatch::Host,
                                     .reason = DeviceDispatchReason::ExecutionConstrainedToHost,
                                     .determinism = KernelDeterminism::Reproducible};
    }

    if (const DeviceKernelSupport* const support = findSupport(plan); support != nullptr)
    {
        std::snprintf(message, sizeof(message), "kernel %s runs on device %s", operationName,
                      m_capabilities.name.c_str());
        execution.reportDiagnostic(DiagnosticSeverity::Debug, ErrorDomain::Runtime, kOperation, message);
        return DeviceKernelSelection{.dispatch = DeviceDispatch::Device,
                                     .reason = DeviceDispatchReason::KernelSupported,
                                     .determinism = support->determinism};
    }

    if (constraint == ComputeBackend::Gpu || execution.backendFallback() == BackendFallback::Forbidden)
    {
        return deviceFailure(
            ErrorCode::UnsupportedBackend, DeviceFailure::KernelNotSupported,
            StatusDetail{.message = "the device does not support this kernel plan, and the execution does not permit "
                                    "falling back to the host",
                         .operation = kOperation,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, operationName)}});
    }

    std::snprintf(message, sizeof(message), "kernel %s falls back to the host: device %s does not support its plan",
                  operationName, m_capabilities.name.c_str());
    execution.reportDiagnostic(DiagnosticSeverity::Warning, ErrorDomain::Runtime, kOperation, message);
    return DeviceKernelSelection{.dispatch = DeviceDispatch::Host,
                                 .reason = DeviceDispatchReason::KernelNotSupported,
                                 .determinism = KernelDeterminism::Reproducible};
}

Result<PreparedDeviceKernel> DeviceState::prepareKernel(const ExecutionContext& execution,
                                                        const DeviceKernelRequest& request)
{
    constexpr const char* kOperation = "Device::prepareKernel";
    if (!execution.valid())
    {
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "a kernel cannot be prepared for an execution context that "
                                                       "holds nothing",
                                            .operation = kOperation,
                                            .domain = ErrorDomain::Runtime});
    }
    if (Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (findSupport(request.plan) == nullptr)
    {
        return deviceFailure(ErrorCode::UnsupportedBackend, DeviceFailure::KernelNotSupported,
                             StatusDetail{.message = "the device does not support this kernel plan; preparing a "
                                                     "kernel never falls back",
                                          .operation = kOperation,
                                          .context = {StatusContextField::ofText(
                                              ContextKey::Parameter, kernelOperationName(request.plan.id.operation))}});
    }

    std::optional<Result<std::shared_ptr<const DeviceKernel>>> prepared;
    const Status thrown = callAcrossBoundary(
        boundary(kOperation), [this, &prepared, &request] { prepared.emplace(m_backend->prepareKernel(request)); });
    if (thrown.failed())
    {
        return deviceFailure(
            thrown.code(), DeviceFailure::BackendFailure,
            StatusDetail{.message = "the backend threw while preparing a kernel", .operation = kOperation}, thrown);
    }
    if (!prepared->hasValue())
    {
        return deviceFailure(
            prepared->code(), DeviceFailure::BackendFailure,
            StatusDetail{.message = "the backend refused to prepare the kernel", .operation = kOperation},
            prepared->status());
    }
    std::shared_ptr<const DeviceKernel> kernel = std::move(**prepared);
    Status incoherent = checkPreparedKernel(kernel, request, m_capabilities, kOperation);
    if (incoherent.failed())
    {
        return incoherent;
    }

    // What the kernel keeps on the device can only be known once the backend has prepared it, so the limit is
    // applied here rather than before the backend was asked, and a kernel it does not admit is released -
    // outside the lock, because releasing it calls into the backend - before the refusal returns. Admission
    // and the reservation are one step under the lock, so two preparations cannot both fit a limit that
    // admits only one of them.
    const std::uint64_t residentBytes = kernel->residentBytes();
    Status refused;
    {
        const std::lock_guard<std::mutex> held(m_mutex);
        MemoryCounters& memory = m_counters.deviceMemory;
        const std::uint64_t limit = m_capabilities.memoryLimitBytes;
        const std::uint64_t available = limit > memory.liveBytes ? limit - memory.liveBytes : 0u;
        if (residentBytes > available)
        {
            ++memory.deniedAllocations;
            refused = Status::failure(
                ErrorCode::MemoryBudgetExceeded,
                StatusDetail{.message = "the device's memory limit does not admit the memory this kernel holds",
                             .operation = kOperation,
                             .domain = ErrorDomain::Runtime,
                             .context = {StatusContextField::ofCount(ContextKey::Requested, residentBytes),
                                         StatusContextField::ofCount(ContextKey::Available, available),
                                         StatusContextField::ofCount(ContextKey::Limit, limit)}});
        }
        else
        {
            reserveKernelMemory(memory, residentBytes);
            ++m_counters.liveKernels;
        }
    }
    if (refused.failed())
    {
        kernel.reset();
        return refused;
    }

    try
    {
        return PreparedKernelRecord::makeHandle(
            std::make_shared<PreparedKernelRecord>(shared_from_this(), std::move(kernel), residentBytes));
    }
    catch (const std::bad_alloc&)
    {
        // The record was never built, so the kernel is still here to release and nothing will count it out.
        kernel.reset();
        kernelReleased(residentBytes);
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

Result<DeviceEvent> DeviceState::submit(const ExecutionContext& execution, const DeviceSubmission& submission)
{
    constexpr const char* kOperation = "Device::submit";
    sweepRetired();

    // The execution and its cancellation.
    if (!execution.valid())
    {
        return refuse(Status::failure(ErrorCode::NotInitialized,
                                      StatusDetail{.message = "work cannot be submitted under an execution context "
                                                              "that holds nothing",
                                                   .operation = kOperation,
                                                   .domain = ErrorDomain::Runtime}));
    }
    if (Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return refuse(std::move(stopped));
    }

    // The submission's shape. Bindings with no kernel are named before emptiness, because a caller that
    // bound resources evidently meant to run a kernel and forgot to name it.
    const bool hasKernel = submission.kernel != nullptr;
    if (!hasKernel && !submission.bindings.empty())
    {
        return refuse(
            bindingMismatch("the submission binds resources but names no kernel to bind them to", kOperation));
    }
    if (!hasKernel && submission.transfersBefore.empty() && submission.transfersAfter.empty())
    {
        return refuse(deviceFailure(ErrorCode::InvalidArgument, DeviceFailure::EmptySubmission,
                                    StatusDetail{.operation = kOperation}));
    }
    const std::uint64_t transferCount =
        static_cast<std::uint64_t>(submission.transfersBefore.size()) + submission.transfersAfter.size();
    if (transferCount > m_capabilities.maximumTransfers)
    {
        return refuse(submissionTooLarge("transfers", transferCount, m_capabilities.maximumTransfers, kOperation));
    }
    if (submission.bindings.size() > m_capabilities.maximumBindings)
    {
        return refuse(
            submissionTooLarge("bindings", submission.bindings.size(), m_capabilities.maximumBindings, kOperation));
    }
    if (submission.dependencies.size() > m_capabilities.maximumDependencies)
    {
        return refuse(submissionTooLarge("dependencies", submission.dependencies.size(),
                                         m_capabilities.maximumDependencies, kOperation));
    }

    std::vector<ResourceUse> uses;
    std::vector<DeviceBindingStep> bindingSteps;
    std::shared_ptr<PreparedKernelRecord> kernelRecord;
    std::uint64_t scratchBytes = 0u;

    // The kernel and its bindings.
    if (hasKernel)
    {
        kernelRecord = PreparedKernelRecord::share(*submission.kernel);
        if (kernelRecord == nullptr)
        {
            return refuse(deviceFailure(ErrorCode::InvalidArgument, DeviceFailure::InvalidKernel,
                                        StatusDetail{.message = "the submission names a prepared kernel handle that "
                                                                "holds nothing",
                                                     .operation = kOperation}));
        }
        if (kernelRecord->device().get() != this)
        {
            return refuse(deviceFailure(ErrorCode::IncompatibleArguments, DeviceFailure::ForeignDevice,
                                        StatusDetail{.message = "the submission names a kernel a different device "
                                                                "prepared",
                                                     .operation = kOperation}));
        }
        const DeviceKernel& kernel = kernelRecord->kernel();
        const std::span<const DeviceBindingRequirement> requirements = kernel.bindings();
        if (submission.bindings.size() != requirements.size())
        {
            return refuse(bindingMismatch("the submission binds a different number of resources from the kernel's "
                                          "bindings",
                                          kOperation));
        }

        std::vector<std::shared_ptr<DeviceResourceState>> bound;
        bound.reserve(requirements.size());
        for (std::size_t index = 0u; index < requirements.size(); ++index)
        {
            const DeviceBinding& binding = submission.bindings[index];
            const DeviceBindingRequirement& requirement = requirements[index];
            Result<std::shared_ptr<DeviceResourceState>> resolved = resolveResource(binding.resource, m_id, kOperation);
            if (!resolved.hasValue())
            {
                return refuse(resolved.status());
            }
            const DeviceResourceDescriptor& descriptor = (*resolved)->descriptor();
            if (binding.access != requirement.access)
            {
                return refuse(
                    bindingMismatch("a binding's access differs from what the kernel does with it", kOperation));
            }
            if (!isDeviceReachable(descriptor.residency))
            {
                return refuse(deviceFailure(
                    ErrorCode::IncompatibleArguments, DeviceFailure::NotDeviceReachable,
                    StatusDetail{.message = "a kernel cannot bind host or pinned memory; declare a transfer into "
                                            "shared or device memory",
                                 .operation = kOperation,
                                 .context = {StatusContextField::ofText(
                                     ContextKey::Parameter, resourceResidencyName(descriptor.residency))}}));
            }
            if (descriptor.bytes < requirement.minimumBytes || descriptor.alignment < requirement.alignment)
            {
                return refuse(bindingMismatch(
                    "a bound resource is smaller, or less strictly aligned, than the kernel needs", kOperation));
            }
            if (requirement.layout.has_value() && descriptor.layout.has_value() &&
                !(*requirement.layout == *descriptor.layout))
            {
                return refuse(
                    bindingMismatch("a bound resource is laid out differently from the kernel's binding", kOperation));
            }
            bindingSteps.push_back(
                DeviceBindingStep{.allocation = DeviceAllocation{.handle = (*resolved)->allocation()},
                                  .residency = descriptor.residency,
                                  .access = binding.access,
                                  .bytes = descriptor.bytes});
            bound.push_back(std::move(*resolved));
        }

        const bool exactInPlace = kernel.plan().alias == KernelAliasPolicy::ExactInPlace;
        for (std::size_t first = 0u; first < bound.size(); ++first)
        {
            for (std::size_t second = first + 1u; second < bound.size(); ++second)
            {
                const DeviceAccess firstAccess = submission.bindings[first].access;
                const DeviceAccess secondAccess = submission.bindings[second].access;
                if (bound[first] != bound[second] || (!accessWrites(firstAccess) && !accessWrites(secondAccess)))
                {
                    continue;
                }
                // Exact in-place work is one input read and one output written through the same bytes.
                const bool inputAndOutput = (firstAccess == DeviceAccess::Read) != (secondAccess == DeviceAccess::Read);
                if (exactInPlace && inputAndOutput)
                {
                    continue;
                }
                return refuse(deviceFailure(ErrorCode::Aliasing, DeviceFailure::BindingAliased,
                                            StatusDetail{.message = "two bindings name one resource, and the kernel's "
                                                                    "plan does not permit exact in-place work",
                                                         .operation = kOperation}));
            }
        }
        for (std::size_t index = 0u; index < bound.size(); ++index)
        {
            const DeviceAccess access = submission.bindings[index].access;
            noteUse(uses, bound[index], accessReads(access), accessWrites(access));
        }
        scratchBytes = kernel.scratchBytes();
    }

    // The transfers.
    std::vector<DeviceTransferStep> beforeSteps;
    std::vector<DeviceTransferStep> afterSteps;
    for (const DeviceTransfer& transfer : submission.transfersBefore)
    {
        Status problem = resolveTransfer(transfer, m_id, beforeSteps, uses, kOperation);
        if (problem.failed())
        {
            return refuse(std::move(problem));
        }
    }
    for (const DeviceTransfer& transfer : submission.transfersAfter)
    {
        Status problem = resolveTransfer(transfer, m_id, afterSteps, uses, kOperation);
        if (problem.failed())
        {
            return refuse(std::move(problem));
        }
    }

    // The dependencies.
    std::vector<std::shared_ptr<DeviceEventState>> dependencyStates;
    dependencyStates.reserve(submission.dependencies.size());
    for (const DeviceEvent& dependency : submission.dependencies)
    {
        const std::shared_ptr<DeviceEventState>& state = DeviceEventState::share(dependency);
        if (state == nullptr)
        {
            return refuse(deviceFailure(ErrorCode::InvalidArgument, DeviceFailure::InvalidDependency,
                                        StatusDetail{.operation = kOperation}));
        }
        dependencyStates.push_back(state);
    }

    // The kernel's scratch, counted against the memory limit like any other device memory.
    std::optional<DeviceAllocation> scratch;
    DeviceResourceDescriptor scratchDescriptor{};
    if (scratchBytes > 0u)
    {
        scratchDescriptor = DeviceResourceDescriptor{
            .bytes = scratchBytes,
            .alignment = (std::min)(kDefaultBufferAlignmentBytes, m_capabilities.maximumAlignmentBytes),
            .residency = m_capabilities.deviceResidency ? ResourceResidency::Device : ResourceResidency::Shared};
        Result<DeviceAllocation> allocated = allocate(scratchDescriptor, kOperation);
        if (!allocated.hasValue())
        {
            return refuse(allocated.status());
        }
        scratch = *allocated;
    }
    const auto releaseScratch = [this, &scratch, &scratchDescriptor]() noexcept {
        if (scratch.has_value())
        {
            releaseAllocation(*scratch, scratchDescriptor);
            scratch.reset();
        }
    };

    std::uint64_t eventId = 0u;
    {
        const std::lock_guard<std::mutex> held(m_mutex);
        eventId = ++m_lastEventId;
    }
    std::shared_ptr<DeviceEventState> event;
    std::shared_ptr<DeviceWorkRecord> record;
    try
    {
        event = std::make_shared<DeviceEventState>(eventId, m_id, dependencyStates);
        record = std::make_shared<DeviceWorkRecord>(shared_from_this(), event, execution.stopToken(),
                                                    execution.deadline(), dependencyStates.size());
        record->transfersBefore = std::move(beforeSteps);
        record->transfersAfter = std::move(afterSteps);
        record->bindings = std::move(bindingSteps);
        record->kernel = std::move(kernelRecord);
        record->scratch = scratch;
        record->scratchDescriptor = scratchDescriptor;
        record->resources.reserve(uses.size());
        for (const ResourceUse& use : uses)
        {
            record->resources.push_back(use.state);
        }
    }
    catch (const std::bad_alloc&)
    {
        releaseScratch();
        return refuse(Status::failure(ErrorCode::AllocationFailed));
    }

    // Mappings and pending use, decided and registered under one lock so no other submission slips between.
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        Status conflict = checkUses(uses, dependencyStates, kOperation);
        if (conflict.ok())
        {
            try
            {
                for (const ResourceUse& use : uses)
                {
                    if (!use.writes)
                    {
                        use.state->readers.reserve(use.state->readers.size() + 1u);
                    }
                }
                m_waiting.reserve(m_waiting.size() + 1u);
            }
            catch (const std::bad_alloc&)
            {
                conflict = Status::failure(ErrorCode::AllocationFailed);
            }
        }
        if (conflict.failed())
        {
            ++m_counters.refusedSubmissions;
            lock.unlock();
            // The record never reached a backend and holds no scratch of its own to return.
            record->scratch.reset();
            releaseScratch();
            return conflict;
        }

        // Nothing below allocates: every vector grown here had its room reserved above.
        for (const ResourceUse& use : uses)
        {
            DeviceResourceState& state = *use.state;
            if (use.writes)
            {
                state.lastWriter = event;
                state.readers.clear();
            }
            else
            {
                std::erase_if(state.readers, [](const std::shared_ptr<DeviceEventState>& reader) {
                    return reader->phase() == DeviceWorkPhase::Finished;
                });
                state.readers.push_back(event);
            }
        }
        m_waiting.push_back(record);
        ++m_counters.acceptedWork;
        ++m_counters.pendingWork;
    }

    // Dependencies and the stop token. The work cannot move on until registration ends, however many of
    // its dependencies finish meanwhile, because it still counts itself as one of them.
    for (const std::shared_ptr<DeviceEventState>& dependency : dependencyStates)
    {
        bool registered = false;
        try
        {
            registered = dependency->addContinuation(std::weak_ptr<DeviceEventContinuation>(record));
        }
        catch (const std::bad_alloc&)
        {
            const std::lock_guard<std::mutex> held(record->mutex);
            if (!record->dependencyFailure.has_value())
            {
                record->dependencyFailure = Status::failure(ErrorCode::AllocationFailed);
            }
        }
        if (!registered)
        {
            record->onEventFinished(*dependency);
        }
    }
    if (record->stopToken.stop_possible())
    {
        try
        {
            record->stopHook = std::make_unique<std::stop_callback<DeviceStopHook>>(record->stopToken,
                                                                                    DeviceStopHook{.record = record});
        }
        catch (const std::bad_alloc&)
        {
            // Without the hook a stop is still observed when the work is handed off and at the backend's
            // checkpoints; it is only no longer observed while the work waits.
        }
    }
    {
        const std::lock_guard<std::mutex> held(record->mutex);
        record->stage = DeviceWorkStage::Waiting;
        --record->remainingDependencies;
    }
    advance(record);
    return DeviceEventState::makeHandle(std::move(event));
}

DeviceCounters DeviceState::counters() const noexcept
{
    const std::lock_guard<std::mutex> held(m_mutex);
    return m_counters;
}

Status DeviceState::shutdown()
{
    std::vector<std::shared_ptr<DeviceWorkRecord>> retired;
    {
        const std::lock_guard<std::mutex> held(m_mutex);
        const std::uint64_t outstanding =
            m_counters.pendingWork + m_counters.liveResources + m_counters.liveMappings + m_counters.liveKernels;
        if (outstanding != 0u)
        {
            return deviceFailure(
                ErrorCode::Busy, DeviceFailure::DeviceInUse,
                StatusDetail{.operation = "Device::shutdown",
                             .context = {StatusContextField::ofCount(ContextKey::Total, outstanding)}});
        }
        retired.swap(m_retired);
    }
    return Status::success();
}

void DeviceState::kernelReleased(std::uint64_t residentBytes) noexcept
{
    const std::lock_guard<std::mutex> held(m_mutex);
    --m_counters.liveKernels;
    if (residentBytes > 0u)
    {
        m_counters.deviceMemory.liveBytes -= residentBytes;
        --m_counters.deviceMemory.liveAllocations;
    }
}

void DeviceState::countBackendDefect() noexcept
{
    const std::lock_guard<std::mutex> held(m_mutex);
    ++m_counters.backendDefects;
}

void DeviceState::complete(const std::shared_ptr<DeviceWorkRecord>& record, Status outcome) noexcept
{
    if (outcome.ok())
    {
        const DeviceWorkTiming timing = record->event->timing();
        const std::uint32_t declaredTransfers = record->declaredTransfers();
        const bool kernelDeclared = record->kernel != nullptr;
        if (timing.transfersRecorded != declaredTransfers || timing.kernelRecorded != kernelDeclared)
        {
            countBackendDefect();
            try
            {
                outcome = deviceFailure(
                    ErrorCode::Internal, DeviceFailure::IncompleteReport,
                    StatusDetail{
                        .message = "the backend reported success without recording every transfer and kernel "
                                   "the work declared",
                        .operation = "DeviceWork::complete",
                        .context = {StatusContextField::ofCount(ContextKey::Completed, timing.transfersRecorded),
                                    StatusContextField::ofCount(ContextKey::Total, declaredTransfers)}});
            }
            catch (...)
            {
                outcome = Status::failure(ErrorCode::Internal);
            }
        }
    }
    finish(record, std::move(outcome));
}

void DeviceState::finish(const std::shared_ptr<DeviceWorkRecord>& record, Status outcome) noexcept
{
    // Held until the end: finishing the work may release the last other reference to this device.
    std::shared_ptr<DeviceState> keepAlive;
    bool wasWaiting = false;
    {
        const std::lock_guard<std::mutex> held(record->mutex);
        if (record->stage == DeviceWorkStage::Finished)
        {
            return;
        }
        wasWaiting = record->stage != DeviceWorkStage::HandedOff;
        record->stage = DeviceWorkStage::Finished;
        keepAlive = std::move(record->device);
    }

    if (record->scratch.has_value())
    {
        releaseAllocation(*record->scratch, record->scratchDescriptor);
    }

    const DeviceWorkTiming timing = record->event->timing();
    {
        const std::lock_guard<std::mutex> held(m_mutex);
        --m_counters.pendingWork;
        if (outcome.ok())
        {
            ++m_counters.succeededWork;
        }
        else if (outcome.isCancellation())
        {
            ++m_counters.cancelledWork;
        }
        else
        {
            ++m_counters.failedWork;
        }

        // Transfers are counted as the backend recorded performing them, in the order they were declared.
        std::uint32_t performed = timing.transfersRecorded;
        for (const std::vector<DeviceTransferStep>* steps : {&record->transfersBefore, &record->transfersAfter})
        {
            for (const DeviceTransferStep& step : *steps)
            {
                if (performed == 0u)
                {
                    break;
                }
                --performed;
                DeviceTransferTally& tally = tallyFor(m_counters, step);
                ++tally.transfers;
                tally.bytes += step.bytes;
            }
        }
        if (timing.kernelRecorded)
        {
            ++m_counters.kernelRuns;
        }

        if (wasWaiting)
        {
            const auto found = std::find(m_waiting.begin(), m_waiting.end(), record);
            if (found != m_waiting.end())
            {
                try
                {
                    m_retired.push_back(*found);
                }
                catch (...)
                {
                    // Out of memory: the record is then released by whichever reference is last.
                }
                m_waiting.erase(found);
            }
        }
    }

    // What the work held goes back before anybody waiting on it is woken, so a waiter that returns sees the
    // memory returned and the kernel released.
    record->resources.clear();
    record->kernel.reset();
    record->event->finish(std::move(outcome));
}

void DeviceState::advance(const std::shared_ptr<DeviceWorkRecord>& record) noexcept
{
    enum class Next : std::uint8_t
    {
        Nothing,
        Cancel,
        Stop,
        Fail,
        HandOff
    };

    Next next = Next::Nothing;
    std::optional<Status> reason;
    std::shared_ptr<DeviceState> device;
    {
        const std::lock_guard<std::mutex> held(record->mutex);
        if (record->stage != DeviceWorkStage::Waiting)
        {
            return;
        }
        device = record->device;
        if (record->stopRequested)
        {
            next = Next::Cancel;
        }
        else if (record->dependencyFailure.has_value())
        {
            next = Next::Fail;
            reason = record->dependencyFailure;
        }
        else if (record->remainingDependencies == 0u)
        {
            Status stopped = record->checkCancellation();
            if (stopped.failed())
            {
                next = Next::Stop;
                reason = std::move(stopped);
            }
            else
            {
                record->stage = DeviceWorkStage::HandedOff;
                next = Next::HandOff;
            }
        }
    }
    if (device == nullptr)
    {
        return;
    }

    switch (next)
    {
    case Next::Nothing:
        return;
    case Next::Cancel:
        device->finish(record, Status::failure(ErrorCode::Cancelled));
        return;
    case Next::Stop:
        device->finish(record, std::move(*reason));
        return;
    case Next::Fail:
        // The dependency's own code, so a stopped pipeline reports every stage it stopped as cancelled.
        try
        {
            device->finish(record, deviceFailure(reason->code(), DeviceFailure::DependencyFailed,
                                                 StatusDetail{.operation = "Device::submit"}, *reason));
        }
        catch (...)
        {
            device->finish(record, Status::failure(reason->code()));
        }
        return;
    case Next::HandOff:
        device->handOff(record);
        return;
    }
}

void DeviceState::stop(const std::shared_ptr<DeviceWorkRecord>& record) noexcept
{
    {
        const std::lock_guard<std::mutex> held(record->mutex);
        if (record->stage == DeviceWorkStage::HandedOff || record->stage == DeviceWorkStage::Finished)
        {
            // The backend observes the stop itself, at its checkpoints.
            return;
        }
        record->stopRequested = true;
        if (record->stage == DeviceWorkStage::Registering)
        {
            return;
        }
    }
    advance(record);
}

void DeviceState::handOff(const std::shared_ptr<DeviceWorkRecord>& record) noexcept
{
    constexpr const char* kOperation = "DeviceBackend::enqueue";
    // A backend that completes the work inline can release every other reference to this device before
    // `enqueue` returns.
    const std::shared_ptr<DeviceState> keepAlive = shared_from_this();
    {
        const std::lock_guard<std::mutex> held(m_mutex);
        const auto found = std::find(m_waiting.begin(), m_waiting.end(), record);
        if (found != m_waiting.end())
        {
            m_waiting.erase(found);
        }
    }
    // The stop hook has nothing left to do; the backend observes the stop through the work.
    record->stopHook.reset();
    record->event->markQueued();

    DeviceWork work = DeviceWorkRecord::makeWork(record);
    Status taken;
    const Status thrown = callAcrossBoundary(boundary(kOperation),
                                             [this, &work, &taken] { taken = m_backend->enqueue(std::move(work)); });
    if (thrown.ok() && taken.ok())
    {
        return;
    }
    if (!work.valid())
    {
        // The backend took the work and still reported a failure; completing it remains the backend's.
        countBackendDefect();
        return;
    }
    const Status& cause = thrown.failed() ? thrown : taken;
    try
    {
        finish(record, deviceFailure(cause.code(), DeviceFailure::BackendFailure,
                                     StatusDetail{.message = thrown.failed() ? "the backend threw while taking the work"
                                                                             : "the backend refused the work",
                                                  .operation = kOperation},
                                     cause));
    }
    catch (...)
    {
        finish(record, Status::failure(cause.code()));
    }
}

const DeviceKernelSupport* DeviceState::findSupport(const KernelPlanKey& plan) const noexcept
{
    for (const DeviceKernelSupport& support : m_capabilities.kernels)
    {
        if (kernelContractSupports(support.contract, plan))
        {
            return &support;
        }
    }
    return nullptr;
}

ExternalBoundary DeviceState::boundary(const char* operation) const noexcept
{
    return ExternalBoundary{
        .library = m_capabilities.name.c_str(), .operation = operation, .domain = ErrorDomain::Runtime};
}

Status DeviceState::refuse(Status refusal) noexcept
{
    const std::lock_guard<std::mutex> held(m_mutex);
    ++m_counters.refusedSubmissions;
    return refusal;
}

void DeviceState::sweepRetired() noexcept
{
    std::vector<std::shared_ptr<DeviceWorkRecord>> retired;
    {
        const std::lock_guard<std::mutex> held(m_mutex);
        retired.swap(m_retired);
    }
    // Destroyed here, with no lock held: a retired record's stop registration may still be running on the
    // thread that requested the stop, and its destructor waits for that.
}

} // namespace an
