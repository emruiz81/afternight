// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Memory that belongs to a device, and the only ways the host reaches it.
///
/// An accelerator's memory is not host memory with a different allocator. Some of it the host can
/// address and some it cannot; some of it a kernel on the device can read and some it cannot; and a
/// copy between the two sides is a transfer with a cost a planner has to see. An engine in which
/// every buffer is a host pointer has no word for any of that, and a device added to it discovers
/// the difference one silent copy at a time.
///
/// So residency is stated rather than inferred, and it decides what is allowed:
///
/// - **host** memory is ordinary pageable memory. The host maps it, a kernel cannot bind it, and it
///   reaches the device through a transfer;
/// - **pinned** memory is host memory the device transfers to and from without staging it first. The
///   host maps it, and a kernel still cannot bind it;
/// - **shared** memory is addressable from both sides. The host maps it and a kernel binds it; and
/// - **device** memory is the device's alone. A kernel binds it and the host cannot map it at all, so a
///   transfer is the only way its contents reach the host.
///
/// A resource is named by an opaque allocation its backend chose and never by an address. The engine
/// holds a handle and the backend knows what the handle means, so nothing in this header could be a
/// pointer into a card's memory. Mapping is how the host borrows the bytes of a host-addressable
/// resource, and it is refused while accepted work still uses them: a mapping that could observe a
/// kernel half-way through writing is the torn read this contract exists to rule out.
///
/// A handle owns one reference to its memory. Releasing it while accepted work still uses the resource
/// is legitimate - the work keeps the memory until it finishes, and the memory is returned then.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace an
{

/// Where a resource's bytes live, and therefore which side may address them.
enum class ResourceResidency : std::uint8_t
{
    /// Ordinary pageable host memory. Host-addressable; not reachable by a device kernel.
    Host,

    /// Host memory the device can transfer to and from directly. Host-addressable; not reachable by a
    /// device kernel.
    Pinned,

    /// Memory addressable by both the host and the device. Host-addressable and reachable by a device
    /// kernel.
    Shared,

    /// Memory only the device addresses. Reachable by a device kernel; the host reaches its contents
    /// only through a transfer.
    ///
    /// Deliberately the last enumerator; `kResourceResidencyCount` is derived from it.
    Device
};

/// How many residencies `ResourceResidency` holds.
inline constexpr std::size_t kResourceResidencyCount = static_cast<std::size_t>(ResourceResidency::Device) + 1u;

/// Whether the host may map a resource of this residency.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param residency the residency to test.
/// @return true for host, pinned and shared memory; false for device memory and for a value outside
///         the enumeration.
[[nodiscard]] constexpr bool isHostAddressable(ResourceResidency residency) noexcept
{
    return residency == ResourceResidency::Host || residency == ResourceResidency::Pinned ||
           residency == ResourceResidency::Shared;
}

/// Whether a device kernel may bind a resource of this residency.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param residency the residency to test.
/// @return true for shared and device memory; false for host and pinned memory and for a value outside
///         the enumeration.
[[nodiscard]] constexpr bool isDeviceReachable(ResourceResidency residency) noexcept
{
    return residency == ResourceResidency::Shared || residency == ResourceResidency::Device;
}

/// The residency's own name, for a diagnostic or a report.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param residency the residency to name. A value outside the enumeration is named
///        `"UnknownResourceResidency"`.
/// @return a never-null pointer to a string literal compiled into the library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* resourceResidencyName(ResourceResidency residency) noexcept;

/// What a piece of work or a mapping does with a resource's bytes.
enum class DeviceAccess : std::uint8_t
{
    /// The bytes are read and not written.
    Read,

    /// The bytes are written; what they held before is not read.
    Write,

    /// The bytes are read and written.
    ReadWrite
};

/// Whether an access reads a resource's bytes.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param access the access to test.
/// @return true for `Read` and `ReadWrite`.
[[nodiscard]] constexpr bool accessReads(DeviceAccess access) noexcept
{
    return access == DeviceAccess::Read || access == DeviceAccess::ReadWrite;
}

/// Whether an access writes a resource's bytes.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param access the access to test.
/// @return true for `Write` and `ReadWrite`.
[[nodiscard]] constexpr bool accessWrites(DeviceAccess access) noexcept
{
    return access == DeviceAccess::Write || access == DeviceAccess::ReadWrite;
}

/// The access's own name, for a diagnostic or a report.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param access the access to name. A value outside the enumeration is named `"UnknownDeviceAccess"`.
/// @return a never-null pointer to a string literal compiled into the library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* deviceAccessName(DeviceAccess access) noexcept;

/// What a caller asks a device for when it creates a resource.
///
/// A plain aggregate with a default for every member except the size, which is refused at zero rather
/// than guessed.
struct DeviceResourceDescriptor
{
    /// How many bytes the resource holds. Must be nonzero and no more than the device accepts for one
    /// resource.
    std::uint64_t bytes = 0u;

    /// The boundary the resource's first byte sits on, in bytes. Must be a power of two and no more
    /// than the device accepts.
    std::uint64_t alignment = 1u;

    /// Where the bytes live, which decides whether the host may map them and a kernel may bind them.
    ResourceResidency residency = ResourceResidency::Device;

    /// How an image's samples are arranged in the bytes, when they hold one.
    ///
    /// Absent for a resource that is only bytes. Present, it must fit: `bytes` covers the layout's
    /// total, and `alignment` is at least its element size. A kernel whose binding states a layout is
    /// then refused a resource whose layout differs, which is what keeps a transfer between two
    /// residencies from quietly changing a row stride.
    std::optional<ImageLayout> layout{};
};

/// The engine's own record of one resource, defined inside the engine.
///
/// A handle and a mapping hold a pointer to one, and a caller can do nothing with the name: the
/// allocation, the usage the engine tracks and the device it belongs to live there.
class DeviceResourceState;

/// The host's borrow of a host-addressable resource's bytes.
///
/// Created by `DeviceResource::map`. While a mapping exists, the resource can be neither mapped again
/// nor used by work, so the bytes it exposes are not being changed by anything but the caller. Ending
/// the mapping - by `unmap`, by destroying it, or by assigning over it - is what lets work use the
/// resource again.
///
/// Move-only, because a copy would be a second borrow that the one release could not account for.
/// Nothing here is thread-safe with respect to one mapping; the bytes it exposes are the caller's to
/// synchronise.
class DeviceMapping
{
public:
    /// A mapping that borrows nothing. `valid()` is false, and both byte spans are empty.
    DeviceMapping() noexcept = default;

    /// Ends the mapping, if it still borrows the bytes.
    AFTERNIGHTENGINE_API ~DeviceMapping();

    /// Takes over another mapping's borrow, leaving it borrowing nothing.
    ///
    /// @param other the mapping to take over. Left as a default-constructed one.
    AFTERNIGHTENGINE_API DeviceMapping(DeviceMapping&& other) noexcept;

    /// Ends this mapping and takes over another's.
    ///
    /// @param other the mapping to take over. Left as a default-constructed one.
    /// @return this mapping.
    AFTERNIGHTENGINE_API DeviceMapping& operator=(DeviceMapping&& other) noexcept;

    /// Copying is deleted: a second borrow of the same bytes would end the first one's guarantee.
    DeviceMapping(const DeviceMapping&) = delete;

    /// Copying is deleted. See the copy constructor.
    DeviceMapping& operator=(const DeviceMapping&) = delete;

    /// Whether this mapping still borrows a resource's bytes.
    ///
    /// @return false for a default-constructed mapping, one that was moved from, and one that was
    ///         unmapped.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_resource != nullptr;
    }

    /// What this mapping may do with the bytes.
    ///
    /// @return the access it was created with, or `DeviceAccess::Read` for a mapping that borrows
    ///         nothing.
    [[nodiscard]] DeviceAccess access() const noexcept
    {
        return m_access;
    }

    /// The resource's bytes, for reading.
    ///
    /// @return the whole resource, valid until this mapping ends; empty for a mapping that borrows
    ///         nothing. Reading is permitted whatever the mapping's access, but after a write-only
    ///         mapping the contents are whatever the caller wrote.
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept
    {
        return std::span<const std::byte>(m_bytes, static_cast<std::size_t>(m_size));
    }

    /// The resource's bytes, for writing.
    ///
    /// @return the whole resource, valid until this mapping ends; empty for a mapping created with
    ///         `DeviceAccess::Read` and for one that borrows nothing.
    [[nodiscard]] std::span<std::byte> writableBytes() const noexcept
    {
        return accessWrites(m_access) ? std::span<std::byte>(m_bytes, static_cast<std::size_t>(m_size))
                                      : std::span<std::byte>{};
    }

    /// Ends the mapping now rather than at the end of the scope.
    ///
    /// Idempotent: unmapping a mapping that borrows nothing does nothing. Allocation-free.
    AFTERNIGHTENGINE_API void unmap() noexcept;

private:
    /// The resource record is what makes a mapping, so it is also what may build one.
    friend class DeviceResourceState;

    /// Takes a borrow the resource record has already granted.
    ///
    /// @param resource the record of the mapped resource, kept alive by the mapping.
    /// @param bytes the first byte the backend mapped. Never null on this path.
    /// @param size how many bytes are mapped, which is the resource's size.
    /// @param access what the mapping may do with them.
    DeviceMapping(std::shared_ptr<DeviceResourceState> resource, std::byte* bytes, std::uint64_t size,
                  DeviceAccess access) noexcept;

    /// The mapped resource's record, or null for a mapping that borrows nothing.
    std::shared_ptr<DeviceResourceState> m_resource;

    /// The first mapped byte, or null.
    std::byte* m_bytes = nullptr;

    /// How many bytes are mapped.
    std::uint64_t m_size = 0u;

    /// What the mapping may do with the bytes.
    DeviceAccess m_access = DeviceAccess::Read;
};

/// One resource a device created, held by the caller that asked for it.
///
/// Created by `Device::createResource`. The handle owns one reference to the memory: releasing it
/// returns the memory to the device unless accepted work still uses the resource, in which case the
/// work keeps the memory until it finishes and the memory is returned then.
///
/// Move-only, because a copy would be a second owner the caller never asked for. The accessors are
/// immutable and safe to read from several threads; `map` is safe to call from several threads, and
/// at most one of those calls is granted a mapping at a time. Moving, releasing or destroying one
/// handle while another thread uses it is not safe, as for any other object.
class DeviceResource
{
public:
    /// A handle that holds nothing. `valid()` is false.
    DeviceResource() noexcept = default;

    /// Releases the handle's reference to the memory.
    ~DeviceResource() = default;

    /// Takes over another handle's reference, leaving it holding nothing.
    ///
    /// @param other the handle to take over. Left as a default-constructed one.
    DeviceResource(DeviceResource&& other) noexcept = default;

    /// Releases this handle's reference and takes over another's.
    ///
    /// @param other the handle to take over. Left as a default-constructed one.
    /// @return this handle.
    DeviceResource& operator=(DeviceResource&& other) noexcept = default;

    /// Copying is deleted: a copy would be a second owner of the same memory.
    DeviceResource(const DeviceResource&) = delete;

    /// Copying is deleted. See the copy constructor.
    DeviceResource& operator=(const DeviceResource&) = delete;

    /// Whether this handle holds a resource.
    ///
    /// @return false for a default-constructed handle, one that was moved from, and one that was
    ///         released.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_state != nullptr;
    }

    /// This resource's identity, for telling one from another in a diagnostic or a report.
    ///
    /// Unique among the resources its device creates and never reused.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the identity, or `0` for a handle that holds nothing. No live resource is zero.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t id() const noexcept;

    /// The identity of the device this resource belongs to.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the device's identity, or `0` for a handle that holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t deviceId() const noexcept;

    /// What this resource was created as.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the validated description, valid while this handle holds the resource; null for a
    ///         handle that holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API const DeviceResourceDescriptor* descriptor() const noexcept;

    /// Borrows the resource's bytes on the host.
    ///
    /// Refused rather than waited for: a caller that has submitted work using the resource waits on
    /// that work's event first. That keeps the order of the host's reads and the device's writes a
    /// fact the caller stated rather than a race the engine resolved.
    ///
    /// Thread-safe. Allocates only a failure it has to describe.
    ///
    /// @param access what the caller will do with the bytes.
    /// @return the mapping, or a failure: `ErrorCode::InvalidState` with `DeviceFailure::ResourceReleased`
    ///         for a handle that holds nothing, `ErrorCode::InvalidArgument` for an access outside the
    ///         enumeration, `ErrorCode::IncompatibleArguments` with `DeviceFailure::NotHostAddressable`
    ///         for device memory, `ErrorCode::Busy` with `DeviceFailure::ResourceMapped` while another
    ///         mapping exists or with `DeviceFailure::PendingUse` while accepted work that has not
    ///         finished writes the resource - or reads it, for an access that writes - and
    ///         `DeviceFailure::BackendFailure` when the backend refuses or throws, carrying the
    ///         backend's own code with its failure as the cause, or `ErrorCode::Internal` when it
    ///         maps the resource at a null address.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<DeviceMapping> map(DeviceAccess access) const;

    /// Releases this handle's reference now rather than at the end of the scope.
    ///
    /// Idempotent. When accepted work still uses the resource, the memory is returned when that work
    /// finishes. Allocation-free.
    void release() noexcept
    {
        m_state.reset();
    }

private:
    /// The resource record is what makes a handle, so it is also what may build one and look inside
    /// one.
    friend class DeviceResourceState;

    /// Takes the record the device has already built.
    ///
    /// @param state the record. Never null on this path.
    explicit DeviceResource(std::shared_ptr<DeviceResourceState> state) noexcept : m_state(std::move(state)) {}

    /// The resource's record, or null. Shared with the work that uses the resource, so the memory
    /// outlives a handle released while that work is outstanding.
    std::shared_ptr<DeviceResourceState> m_state;
};

} // namespace an
