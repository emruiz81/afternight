// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Runtime/DeviceError.h>

#include <utility>

// The device contract's refusal vocabulary: the names, the sentences, and the one factory that attaches
// a reason to a failure.
//
// None of the switches below carries a `default` label, so a reason added to the enumeration without a
// name or a sentence fails the build rather than reaching a report as a blank. The return after each
// switch is for a value cast in from outside the enumeration, which is a different thing and says so.

namespace an
{

const char* deviceFailureName(DeviceFailure reason) noexcept
{
    switch (reason)
    {
    case DeviceFailure::InvalidCapabilities:
        return "InvalidCapabilities";
    case DeviceFailure::DeviceShutDown:
        return "DeviceShutDown";
    case DeviceFailure::DeviceInUse:
        return "DeviceInUse";
    case DeviceFailure::InvalidDescriptor:
        return "InvalidDescriptor";
    case DeviceFailure::ResidencyNotOffered:
        return "ResidencyNotOffered";
    case DeviceFailure::ResourceTooLarge:
        return "ResourceTooLarge";
    case DeviceFailure::ResourceReleased:
        return "ResourceReleased";
    case DeviceFailure::ForeignDevice:
        return "ForeignDevice";
    case DeviceFailure::NotHostAddressable:
        return "NotHostAddressable";
    case DeviceFailure::NotDeviceReachable:
        return "NotDeviceReachable";
    case DeviceFailure::ResourceMapped:
        return "ResourceMapped";
    case DeviceFailure::PendingUse:
        return "PendingUse";
    case DeviceFailure::UndeclaredDependency:
        return "UndeclaredDependency";
    case DeviceFailure::KernelNotSupported:
        return "KernelNotSupported";
    case DeviceFailure::InvalidKernel:
        return "InvalidKernel";
    case DeviceFailure::BindingMismatch:
        return "BindingMismatch";
    case DeviceFailure::BindingAliased:
        return "BindingAliased";
    case DeviceFailure::TransferOutOfRange:
        return "TransferOutOfRange";
    case DeviceFailure::TransferOverlap:
        return "TransferOverlap";
    case DeviceFailure::EmptySubmission:
        return "EmptySubmission";
    case DeviceFailure::SubmissionTooLarge:
        return "SubmissionTooLarge";
    case DeviceFailure::InvalidDependency:
        return "InvalidDependency";
    case DeviceFailure::DependencyFailed:
        return "DependencyFailed";
    case DeviceFailure::WorkAbandoned:
        return "WorkAbandoned";
    case DeviceFailure::IncompleteReport:
        return "IncompleteReport";
    case DeviceFailure::UnproducedBuffer:
        return "UnproducedBuffer";
    case DeviceFailure::UnusedBuffer:
        return "UnusedBuffer";
    case DeviceFailure::BackendFailure:
        return "BackendFailure";
    }
    return "UnknownDeviceFailure";
}

const char* deviceFailureDescription(DeviceFailure reason) noexcept
{
    switch (reason)
    {
    case DeviceFailure::InvalidCapabilities:
        return "the device backend described what it offers incoherently";
    case DeviceFailure::DeviceShutDown:
        return "the device handle holds nothing";
    case DeviceFailure::DeviceInUse:
        return "the device still has resources, mappings, prepared kernels or unfinished work outstanding";
    case DeviceFailure::InvalidDescriptor:
        return "the resource description is incoherent";
    case DeviceFailure::ResidencyNotOffered:
        return "the device does not create resources of this residency";
    case DeviceFailure::ResourceTooLarge:
        return "the resource is larger, or more strictly aligned, than the device accepts for one resource";
    case DeviceFailure::ResourceReleased:
        return "the resource handle holds nothing";
    case DeviceFailure::ForeignDevice:
        return "the resource, kernel or mapping belongs to a different device";
    case DeviceFailure::NotHostAddressable:
        return "the host cannot address device memory; its contents reach the host through a transfer";
    case DeviceFailure::NotDeviceReachable:
        return "a kernel cannot bind host or pinned memory; its data reaches the device through a transfer";
    case DeviceFailure::ResourceMapped:
        return "the resource is mapped by the host";
    case DeviceFailure::PendingUse:
        return "accepted work that has not finished still uses the resource";
    case DeviceFailure::UndeclaredDependency:
        return "the work uses a resource that unfinished work writes, or writes one that unfinished work reads, "
               "without depending on that work";
    case DeviceFailure::KernelNotSupported:
        return "the device does not support this kernel plan";
    case DeviceFailure::InvalidKernel:
        return "the prepared kernel is incoherent";
    case DeviceFailure::BindingMismatch:
        return "the bindings disagree with the kernel's requirements";
    case DeviceFailure::BindingAliased:
        return "two bindings name the same resource where the kernel does not permit it";
    case DeviceFailure::TransferOutOfRange:
        return "a transfer names bytes outside a resource";
    case DeviceFailure::TransferOverlap:
        return "a transfer reads and writes overlapping bytes of one resource";
    case DeviceFailure::EmptySubmission:
        return "the submission declares no transfer and no kernel";
    case DeviceFailure::SubmissionTooLarge:
        return "the submission declares more bindings, transfers or dependencies than the device accepts";
    case DeviceFailure::InvalidDependency:
        return "a dependency is an event that observes nothing";
    case DeviceFailure::DependencyFailed:
        return "a dependency finished without succeeding, so the work that needed it was not run";
    case DeviceFailure::WorkAbandoned:
        return "the device backend released the work without completing it";
    case DeviceFailure::IncompleteReport:
        return "the device backend reported success without recording every step the work declared";
    case DeviceFailure::UnproducedBuffer:
        return "a chained plan reads a buffer nothing produces, or names an output no step writes";
    case DeviceFailure::UnusedBuffer:
        return "a chained plan declares a buffer no step binds";
    case DeviceFailure::BackendFailure:
        return "the device backend failed";
    }
    return "the device failure reason is not one this build knows";
}

Status deviceFailure(ErrorCode code, DeviceFailure reason, StatusDetail detail, Status cause) noexcept
{
    try
    {
        if (detail.message.empty())
        {
            detail.message = deviceFailureDescription(reason);
        }
        detail.domain = ErrorDomain::Runtime;
        detail.context.push_back(
            StatusContextField::ofCount(ContextKey::DeviceReason, static_cast<std::uint64_t>(reason)));
        return Status::failure(code, std::move(detail), std::move(cause));
    }
    catch (...)
    {
        // Out of memory while describing a refusal: the code survives, the reason and the words do not.
        return Status::failure(code);
    }
}

std::optional<DeviceFailure> deviceFailureReason(const Status& status) noexcept
{
    if (status.ok() || status.domain() != ErrorDomain::Runtime)
    {
        return std::nullopt;
    }
    const StatusContextField* const field = status.findContext(ContextKey::DeviceReason);
    if (field == nullptr || field->kind != ContextValueKind::Count || field->count >= kDeviceFailureCount)
    {
        return std::nullopt;
    }
    return static_cast<DeviceFailure>(field->count);
}

} // namespace an
