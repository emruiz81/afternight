// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - one counted allocation that a sequence session holds for its whole life.
//
// The sequence adapters keep a timestamp trailer, a container header, a packet index or a manifest
// buffer for as long as a session is open. Taking that memory from the engine context's memory
// resource rather than the heap counts it against the context's budget, and the lease the block holds
// keeps the context from shutting down while a session still owns it.

#pragma once

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <cstddef>
#include <cstdint>

namespace an::sequence_detail
{

/// One allocation from an engine context's memory resource, returned to it on destruction. The lease
/// keeps the resource alive however the handle that allocated it has moved.
class AccountedBlock
{
public:
    /// An empty block, holding no memory and no lease.
    AccountedBlock() noexcept = default;

    /// Returns the block to its resource and releases the lease.
    ~AccountedBlock();

    /// Takes the block, its lease and its resource from `other`, which is left empty.
    AccountedBlock(AccountedBlock&& other) noexcept;

    /// Returns this block's memory first, then takes the block from `other`, which is left empty.
    ///
    /// @return this block.
    AccountedBlock& operator=(AccountedBlock&& other) noexcept;

    AccountedBlock(const AccountedBlock&) = delete;
    AccountedBlock& operator=(const AccountedBlock&) = delete;

    /// Allocates a block, or reports the resource's refusal. A zero-byte request holds nothing.
    ///
    /// The contents are unspecified. Refused as `ErrorCode::NotInitialized` for an engine context that
    /// holds nothing, as `ErrorCode::MemoryBudgetExceeded` when the context's budget refuses the bytes,
    /// as `ErrorCode::AllocationFailed` when the platform does, and as `ErrorCode::InvalidArgument` for
    /// an alignment the resource does not accept.
    [[nodiscard]] static Result<AccountedBlock> allocate(const EngineContext& engine, std::uint64_t bytes,
                                                         std::uint64_t alignment);

    /// The block as bytes.
    [[nodiscard]] std::byte* bytes() const noexcept
    {
        return static_cast<std::byte*>(m_data);
    }

    /// The block as eight-byte values, for a block allocated with their alignment.
    [[nodiscard]] std::uint64_t* values() const noexcept
    {
        return static_cast<std::uint64_t*>(m_data);
    }

    /// The block's first byte, for a caller that constructs its own trivially copyable records in it.
    [[nodiscard]] void* data() const noexcept
    {
        return m_data;
    }

    /// The block's size in bytes.
    [[nodiscard]] std::uint64_t size() const noexcept
    {
        return m_bytes;
    }

    /// Returns the block now.
    void release() noexcept;

private:
    EngineContextLease m_lease;
    MemoryResource* m_resource = nullptr;
    void* m_data = nullptr;
    std::uint64_t m_bytes = 0u;
    std::uint64_t m_alignment = 1u;
};

} // namespace an::sequence_detail
