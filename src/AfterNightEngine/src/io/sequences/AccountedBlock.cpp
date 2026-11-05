// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the counted allocation a sequence session holds for its whole life.
//
// The lease is taken before the memory and released after it, so a block never holds memory from a
// context it does not also keep open.

#include "io/sequences/AccountedBlock.h"

#include <utility>

namespace an::sequence_detail
{

AccountedBlock::~AccountedBlock()
{
    release();
}

AccountedBlock::AccountedBlock(AccountedBlock&& other) noexcept
    : m_lease(std::move(other.m_lease)), m_resource(std::exchange(other.m_resource, nullptr)),
      m_data(std::exchange(other.m_data, nullptr)), m_bytes(std::exchange(other.m_bytes, 0u)),
      m_alignment(std::exchange(other.m_alignment, 1u))
{
}

AccountedBlock& AccountedBlock::operator=(AccountedBlock&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_lease = std::move(other.m_lease);
        m_resource = std::exchange(other.m_resource, nullptr);
        m_data = std::exchange(other.m_data, nullptr);
        m_bytes = std::exchange(other.m_bytes, 0u);
        m_alignment = std::exchange(other.m_alignment, 1u);
    }
    return *this;
}

Result<AccountedBlock> AccountedBlock::allocate(const EngineContext& engine, std::uint64_t bytes,
                                                std::uint64_t alignment)
{
    AccountedBlock block;
    if (bytes == 0u)
        return block;
    MemoryResource* resource = engine.memoryResource();
    if (resource == nullptr)
        return Status::failure(ErrorCode::NotInitialized);
    auto lease = engine.acquireLease();
    if (!lease.hasValue())
        return lease.status();
    auto allocation = resource->allocate(bytes, alignment);
    if (!allocation.hasValue())
        return allocation.status();
    block.m_lease = std::move(*lease);
    block.m_resource = resource;
    block.m_data = *allocation;
    block.m_bytes = bytes;
    block.m_alignment = alignment;
    return block;
}

void AccountedBlock::release() noexcept
{
    if (m_data != nullptr)
        m_resource->deallocate(m_data, m_bytes, m_alignment);
    m_data = nullptr;
    m_resource = nullptr;
    m_bytes = 0u;
    m_lease.release();
}

} // namespace an::sequence_detail
