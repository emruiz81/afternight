// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the default zero-filling allocation every memory resource inherits.
//
// A resource that cannot obtain zeroed pages from its platform - a caller's own, most often -
// serves a zeroed request by allocating and clearing, so the interface can offer the cheaper path
// without requiring every implementation to provide it.

#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <cstring>

namespace an
{
Result<void*> MemoryResource::allocateZeroed(std::uint64_t bytes, std::uint64_t alignment)
{
    auto block = allocate(bytes, alignment);
    if (block)
        std::memset(*block, 0, static_cast<std::size_t>(bytes));
    return block;
}
} // namespace an
