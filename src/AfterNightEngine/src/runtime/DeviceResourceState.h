// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the record behind a device resource, and what it needs from its device.
//
// A resource belongs to the device that created it, and the rules about using it - whether it may be
// mapped now, whether work that binds it declared the work already writing it - are the device's. The
// record therefore carries the resource's identity, description and allocation, plus the usage the
// device tracks under its own lock, and reaches the device through a narrow owner interface: the
// runtime layer states the contract a resource needs, and the device that implements it lives above.
//
// The record is shared between the handle, any mapping and any accepted work that uses the resource.
// Its destructor returns the memory, so a handle released while work still uses the resource leaves the
// memory with that work until it finishes.

#pragma once

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Runtime/DeviceResource.h>

#include "runtime/DeviceEventState.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace an
{

class DeviceResourceState;

/// What a resource needs from the device that created it.
class DeviceResourceOwner
{
public:
    /// Virtual, so an owner held by base pointer is destroyed as what it is.
    virtual ~DeviceResourceOwner() = default;

    /// Maps a resource for the host, under the device's rules.
    ///
    /// @param resource the resource's record.
    /// @param access what the host will do with the bytes.
    /// @return the mapping, or the device's refusal.
    [[nodiscard]] virtual Result<DeviceMapping> mapResource(const std::shared_ptr<DeviceResourceState>& resource,
                                                            DeviceAccess access) = 0;

    /// Ends a mapping the device granted.
    ///
    /// @param resource the mapped resource's record.
    virtual void unmapResource(DeviceResourceState& resource) noexcept = 0;

    /// Returns a resource's memory. Called once, by the record's destructor.
    ///
    /// @param resource the record being destroyed.
    virtual void releaseResource(DeviceResourceState& resource) noexcept = 0;

protected:
    /// Constructible only by a derived class.
    DeviceResourceOwner() = default;

    /// Protected so an owner is never copied through a base pointer.
    DeviceResourceOwner(const DeviceResourceOwner&) = default;

    /// Protected for the same reason as the copy constructor.
    DeviceResourceOwner& operator=(const DeviceResourceOwner&) = default;
};

/// One resource's identity, description, allocation and tracked usage.
class DeviceResourceState
{
public:
    /// Records a resource whose allocation the owner has already made.
    ///
    /// @param owner the device, kept alive until the memory is returned.
    /// @param id the resource's identity on its device.
    /// @param deviceId the device's identity.
    /// @param descriptor the validated description.
    /// @param allocation the backend's handle for the memory.
    DeviceResourceState(std::shared_ptr<DeviceResourceOwner> owner, std::uint64_t id, std::uint64_t deviceId,
                        const DeviceResourceDescriptor& descriptor, std::uint64_t allocation) noexcept
        : m_owner(std::move(owner)), m_id(id), m_deviceId(deviceId), m_descriptor(descriptor), m_allocation(allocation)
    {
    }

    /// Returns the memory to the device.
    ~DeviceResourceState()
    {
        m_owner->releaseResource(*this);
    }

    DeviceResourceState(const DeviceResourceState&) = delete;
    DeviceResourceState& operator=(const DeviceResourceState&) = delete;
    DeviceResourceState(DeviceResourceState&&) = delete;
    DeviceResourceState& operator=(DeviceResourceState&&) = delete;

    /// @return the resource's identity on its device.
    [[nodiscard]] std::uint64_t id() const noexcept
    {
        return m_id;
    }

    /// @return the device's identity.
    [[nodiscard]] std::uint64_t deviceId() const noexcept
    {
        return m_deviceId;
    }

    /// @return the validated description.
    [[nodiscard]] const DeviceResourceDescriptor& descriptor() const noexcept
    {
        return m_descriptor;
    }

    /// @return the backend's handle for the memory.
    [[nodiscard]] std::uint64_t allocation() const noexcept
    {
        return m_allocation;
    }

    /// @return the device the resource belongs to.
    [[nodiscard]] DeviceResourceOwner& owner() const noexcept
    {
        return *m_owner;
    }

    /// The last accepted work that writes this resource, finished or not. Guarded by the owner's lock.
    std::shared_ptr<DeviceEventState> lastWriter;

    /// Accepted work that reads this resource, accepted since the last writer. Guarded by the owner's lock.
    std::vector<std::shared_ptr<DeviceEventState>> readers;

    /// Whether the host holds a mapping. Guarded by the owner's lock.
    bool mapped = false;

    /// Builds the public handle over a record.
    ///
    /// @param state the record.
    /// @return a handle owning one reference to it.
    [[nodiscard]] static DeviceResource makeHandle(std::shared_ptr<DeviceResourceState> state) noexcept
    {
        return DeviceResource(std::move(state));
    }

    /// The record behind a public handle.
    ///
    /// @param resource the handle.
    /// @return its record, which is null for a handle that holds nothing.
    [[nodiscard]] static const std::shared_ptr<DeviceResourceState>& share(const DeviceResource& resource) noexcept
    {
        return resource.m_state;
    }

    /// Builds the public mapping over a record the owner has mapped.
    ///
    /// @param state the record.
    /// @param bytes the first mapped byte.
    /// @param access what the mapping may do.
    /// @return the mapping.
    [[nodiscard]] static DeviceMapping makeMapping(std::shared_ptr<DeviceResourceState> state, std::byte* bytes,
                                                   DeviceAccess access) noexcept
    {
        const std::uint64_t size = state->descriptor().bytes;
        return DeviceMapping(std::move(state), bytes, size, access);
    }

private:
    /// The device that created the resource, kept alive until the destructor has returned the memory.
    std::shared_ptr<DeviceResourceOwner> m_owner;

    /// The resource's identity on its device; immutable.
    std::uint64_t m_id;

    /// The device's identity; immutable.
    std::uint64_t m_deviceId;

    /// The description the device validated; immutable.
    DeviceResourceDescriptor m_descriptor;

    /// The backend's opaque handle for the memory; immutable.
    std::uint64_t m_allocation;
};

} // namespace an
