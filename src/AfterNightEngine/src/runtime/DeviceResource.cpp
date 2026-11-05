// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Runtime/DeviceResource.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/DeviceError.h>

#include "runtime/DeviceResourceState.h"

#include <utility>

// The public faces of a device resource and a host mapping. What a resource may do is the device's to
// decide, so these forward to the record's owner and add only the checks a handle can make on its own:
// that it holds something, and that an access is one of the three there are.

namespace an
{

const char* resourceResidencyName(ResourceResidency residency) noexcept
{
    switch (residency)
    {
    case ResourceResidency::Host:
        return "Host";
    case ResourceResidency::Pinned:
        return "Pinned";
    case ResourceResidency::Shared:
        return "Shared";
    case ResourceResidency::Device:
        return "Device";
    }
    return "UnknownResourceResidency";
}

const char* deviceAccessName(DeviceAccess access) noexcept
{
    switch (access)
    {
    case DeviceAccess::Read:
        return "Read";
    case DeviceAccess::Write:
        return "Write";
    case DeviceAccess::ReadWrite:
        return "ReadWrite";
    }
    return "UnknownDeviceAccess";
}

DeviceMapping::DeviceMapping(std::shared_ptr<DeviceResourceState> resource, std::byte* bytes, std::uint64_t size,
                             DeviceAccess access) noexcept
    : m_resource(std::move(resource)), m_bytes(bytes), m_size(size), m_access(access)
{
}

DeviceMapping::~DeviceMapping()
{
    unmap();
}

DeviceMapping::DeviceMapping(DeviceMapping&& other) noexcept
    : m_resource(std::move(other.m_resource)), m_bytes(std::exchange(other.m_bytes, nullptr)),
      m_size(std::exchange(other.m_size, 0u)), m_access(std::exchange(other.m_access, DeviceAccess::Read))
{
}

DeviceMapping& DeviceMapping::operator=(DeviceMapping&& other) noexcept
{
    if (this != &other)
    {
        unmap();
        m_resource = std::move(other.m_resource);
        m_bytes = std::exchange(other.m_bytes, nullptr);
        m_size = std::exchange(other.m_size, 0u);
        m_access = std::exchange(other.m_access, DeviceAccess::Read);
    }
    return *this;
}

void DeviceMapping::unmap() noexcept
{
    if (m_resource == nullptr)
    {
        return;
    }
    // The record is held by a local until the owner has ended the mapping, so a mapping that was the
    // resource's last reference returns the memory only after the backend has unmapped it.
    const std::shared_ptr<DeviceResourceState> resource = std::move(m_resource);
    m_bytes = nullptr;
    m_size = 0u;
    m_access = DeviceAccess::Read;
    resource->owner().unmapResource(*resource);
}

std::uint64_t DeviceResource::id() const noexcept
{
    return m_state != nullptr ? m_state->id() : 0u;
}

std::uint64_t DeviceResource::deviceId() const noexcept
{
    return m_state != nullptr ? m_state->deviceId() : 0u;
}

const DeviceResourceDescriptor* DeviceResource::descriptor() const noexcept
{
    return m_state != nullptr ? &m_state->descriptor() : nullptr;
}

Result<DeviceMapping> DeviceResource::map(DeviceAccess access) const
{
    constexpr const char* kOperation = "DeviceResource::map";
    if (m_state == nullptr)
    {
        return deviceFailure(
            ErrorCode::InvalidState, DeviceFailure::ResourceReleased,
            StatusDetail{.message = "a resource handle that holds nothing cannot be mapped", .operation = kOperation});
    }
    if (access != DeviceAccess::Read && access != DeviceAccess::Write && access != DeviceAccess::ReadWrite)
    {
        return Status::failure(ErrorCode::InvalidArgument,
                               StatusDetail{.message = "the access is not one this engine knows",
                                            .operation = kOperation,
                                            .domain = ErrorDomain::Runtime,
                                            .context = {StatusContextField::ofText(ContextKey::Parameter, "access")}});
    }
    return m_state->owner().mapResource(m_state, access);
}

} // namespace an
