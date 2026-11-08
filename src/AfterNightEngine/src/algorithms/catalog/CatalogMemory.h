// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - persistent catalog allocation: context accounting, and a lease held until
// the shared control block is destroyed.
//
// A built catalog outlives the execution that built it, so its memory cannot be operation scratch.
// `CatalogMemory` is a standard memory resource over the engine context's budgeted allocator that
// holds an engine-context lease; `CatalogAllocator` hands it to `std::allocate_shared`, whose
// control block keeps the allocator - and so the resource and its lease - alive until the last
// handle to the table is gone. Every byte a catalog holds is therefore counted by the context that
// built it, and that context cannot shut down under it. A budget refusal is thrown as
// `CatalogAllocationFailure` so the build's catch site can return the typed status the allocator
// gave, where a plain `std::bad_alloc` would lose it.
#pragma once

#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include "runtime/EngineContextState.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <utility>

namespace an::catalog_detail
{
/// What a catalog allocation throws when the engine context's memory refuses it.
///
/// Thrown through the standard containers' allocation paths and caught at the build's own
/// boundary, which returns `status` as the build's failure; it never crosses the public surface.
struct CatalogAllocationFailure
{
    /// The refusal the engine context's memory resource returned, unchanged.
    Status status;
};

/// The engine's budgeted allocator seen as a standard resource.
///
/// Every byte a catalog holds is requested here, so the context's live, peak and denied counters
/// answer for a catalog exactly as they answer for an image, and a refusal keeps the typed status
/// rather than becoming `bad_alloc`. A zero-byte request is made as one byte.
class BudgetedCatalogUpstream final : public std::pmr::memory_resource
{
public:
    /// Adapts one engine context's memory resource.
    ///
    /// @param memory the resource to allocate from, which must outlive this adapter.
    explicit BudgetedCatalogUpstream(MemoryResource& memory) noexcept : m_memory(memory) {}

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        auto block = m_memory.allocate((std::max<std::size_t>)(bytes, 1u), alignment);
        if (!block.hasValue())
        {
            throw CatalogAllocationFailure{block.status()};
        }
        return *block;
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        m_memory.deallocate(pointer, (std::max<std::size_t>)(bytes, 1u), alignment);
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    MemoryResource& m_memory;
};

/// The bounds of a catalog's pool: what is pooled and how many blocks a chunk holds.
///
/// The blocks an index is made of - a node, a key, a short position list - are pooled; a table -
/// the entries, the declination index, a bucket array - is larger than any pooled block and goes
/// to the context one to one. The bounds are stated rather than left to the library: MSVC's
/// defaults pool every request whatever its size, rounding a table up to a power of two inside a
/// chunk of several such blocks, so a star catalog of 83 479 entries held 204 MiB where its
/// tables and indices need about a quarter of that.
///
/// @return the options the pool is built with.
[[nodiscard]] inline std::pmr::pool_options catalogPoolOptions() noexcept
{
    return std::pmr::pool_options{.max_blocks_per_chunk = 1024u, .largest_required_pool_block = 4096u};
}

/// The memory resource one catalog's persistent state is allocated from.
///
/// The shared-control allocator retains this resource through destruction of the control block,
/// not merely through destruction of the table. Containers borrow it for that complete lifetime.
///
/// A catalog's index is built out of tens of thousands of objects that are individually smaller
/// than the accounting around them: one node per distinct name, one position list per node, one
/// key per name the table did not already hold. Asking the engine's budgeted allocator for each of
/// them separately measures the budget check rather than the index, so the small requests are
/// pooled and the pool takes its chunks from that allocator. What is budgeted, counted and leased
/// is unchanged - every byte still comes from the context - and a request too large for a pool
/// still reaches it one to one, as `catalogPoolOptions` bounds the pool.
///
/// The pool is the unsynchronized one because one of these belongs to exactly one catalog, and a
/// catalog allocates on one thread while it is built and frees on one thread when the last handle
/// to it goes. It is never the engine context's shared resource, which is what concurrent work
/// allocates from, and `do_is_equal` says so: no other resource is interchangeable with this one.
class CatalogMemory final : public std::pmr::memory_resource
{
public:
    /// Leases the execution's engine context and allocates from its memory resource.
    ///
    /// @param execution the building operation, which must hold an engine context; the lease
    ///        taken here keeps that context alive for the life of this resource.
    explicit CatalogMemory(const ExecutionContext& execution)
        : m_lease(EngineContextState::retain(execution)), m_upstream(*execution.memoryResource())
#if !defined(_MSC_VER) || _ITERATOR_DEBUG_LEVEL == 0
          ,
          m_pool(catalogPoolOptions(), &m_upstream)
#endif
    {
    }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL != 0
        return m_upstream.allocate((std::max<std::size_t>)(bytes, 1u), alignment);
#else
        return m_pool.allocate((std::max<std::size_t>)(bytes, 1u), alignment);
#endif
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL != 0
        m_upstream.deallocate(pointer, (std::max<std::size_t>)(bytes, 1u), alignment);
#else
        m_pool.deallocate(pointer, (std::max<std::size_t>)(bytes, 1u), alignment);
#endif
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    EngineContextLease m_lease;

    // Declared before the pool so that it outlives it: the pool returns every chunk it holds to
    // this resource as it is destroyed.
    BudgetedCatalogUpstream m_upstream;
#if !defined(_MSC_VER) || _ITERATOR_DEBUG_LEVEL == 0
    // MSVC's noexcept pool constructor allocates a vector iterator proxy in Debug. A refusal
    // there terminates instead of reaching the catalog's failure boundary. Iterator-debug builds
    // therefore allocate directly upstream; the same budget and lifetime rules still apply.
    std::pmr::unsynchronized_pool_resource m_pool;
#endif
};

/// The standard allocator that shares ownership of one catalog's `CatalogMemory`.
///
/// Passed to `std::allocate_shared`, whose control block keeps a copy until it is destroyed, which
/// is what keeps the resource and its context lease alive exactly as long as the table.
///
/// @tparam ValueT the element type the standard library allocates.
template <typename ValueT> struct CatalogAllocator
{
    /// The element type, as `std::allocator_traits` requires.
    using value_type = ValueT; // NOLINT(readability-identifier-naming): required by std::allocator_traits.
    /// The shared resource every copy allocates from.
    std::shared_ptr<CatalogMemory> memory;
    /// Shares one catalog's resource.
    ///
    /// @param resource the resource to share.
    explicit CatalogAllocator(std::shared_ptr<CatalogMemory> resource) : memory(std::move(resource)) {}
    /// Rebinds another element type's allocator onto the same resource.
    ///
    /// @tparam OtherT the other allocator's element type.
    /// @param other the allocator whose resource is shared.
    template <typename OtherT> CatalogAllocator(const CatalogAllocator<OtherT>& other) noexcept : memory(other.memory)
    {
    }
    /// Allocates storage for `count` elements from the shared resource.
    ///
    /// @param count how many elements.
    /// @return the storage; throws `std::bad_alloc` when the byte count overflows and
    ///         `CatalogAllocationFailure` when the engine context refuses it.
    ValueT* allocate(std::size_t count)
    {
        if (count > (std::numeric_limits<std::size_t>::max)() / sizeof(ValueT))
        {
            throw std::bad_alloc{};
        }
        return static_cast<ValueT*>(memory->allocate(count * sizeof(ValueT), alignof(ValueT)));
    }
    /// Returns storage `allocate` produced to the shared resource.
    ///
    /// @param pointer the storage.
    /// @param count the element count it was allocated for.
    void deallocate(ValueT* pointer, std::size_t count) noexcept
    {
        memory->deallocate(pointer, count * sizeof(ValueT), alignof(ValueT));
    }
    /// Whether two allocators can free each other's storage, which they can exactly when they share
    /// one resource.
    ///
    /// @tparam OtherT the other allocator's element type.
    /// @param other the other allocator.
    /// @return true when both hold the same resource.
    template <typename OtherT> bool operator==(const CatalogAllocator<OtherT>& other) const noexcept
    {
        return memory == other.memory;
    }
};
} // namespace an::catalog_detail
