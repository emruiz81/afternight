// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the drizzle executor's private pieces: the context-owned buffer that holds
// each of its working allocations, and the finalization that turns a destination band's terms into
// output samples.
//
// Finalization writes into the result the executor is still building. Nothing here publishes an
// output: on a failure the executor returns without handing the result over, and every buffer's
// scope releases what it took.
#pragma once

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleExecutor.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

namespace an::drizzle_detail
{

/// One 64-byte-aligned allocation from a memory resource, returned when the buffer is destroyed.
/// The executor declares each working buffer once and lets its scope release it on every return.
class ExecutionBuffer
{
public:
    /// An empty buffer.
    ExecutionBuffer() = default;
    /// Returns the allocation, if any, to the resource it came from.
    ~ExecutionBuffer()
    {
        if (m_memory != nullptr)
            m_memory->deallocate(m_data, m_bytes, 64u);
    }
    /// A buffer has one owner.
    ExecutionBuffer(const ExecutionBuffer&) = delete;
    /// A buffer has one owner.
    ExecutionBuffer& operator=(const ExecutionBuffer&) = delete;
    /// Takes the buffer's bytes; a zero-byte request takes nothing and leaves the buffer empty. Call
    /// it at most once per buffer.
    /// @param memory the resource the bytes come from and return to.
    /// @param bytes the size.
    /// @return success, or the resource's refusal.
    [[nodiscard]] Status allocate(MemoryResource& memory, std::uint64_t bytes)
    {
        if (bytes == 0u)
            return Status::success();
        auto block = memory.allocate(bytes, 64u);
        if (!block.hasValue())
            return block.status();
        m_memory = &memory;
        m_data = *block;
        m_bytes = bytes;
        return Status::success();
    }
    /// The allocation as bytes.
    /// @return the bytes, empty when nothing was taken.
    [[nodiscard]] std::span<std::byte> bytes() const noexcept
    {
        return {static_cast<std::byte*>(m_data), static_cast<std::size_t>(m_bytes)};
    }
    /// The allocation as an array of `T`; the caller constructs any element that needs it.
    /// @tparam T the element type.
    /// @return as many whole elements as fit, empty when nothing was taken.
    template <typename T> [[nodiscard]] std::span<T> values() const noexcept
    {
        return {static_cast<T*>(m_data), static_cast<std::size_t>(m_bytes / sizeof(T))};
    }

private:
    MemoryResource* m_memory = nullptr; ///< The resource the bytes return to, or null when empty.
    void* m_data = nullptr; ///< The allocation.
    std::uint64_t m_bytes = 0u; ///< Its size.
};

/// A failure in the algorithms domain, attributed to `an::executeDrizzle`.
/// @param code why the operation failed.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status executionFailure(ErrorCode code, const char* message);
/// Turns one destination band's terms into output samples at the band's rows of `result`.
///
/// A sample with positive signed total weight becomes its weighted mean, or for a peak-normalized
/// sum its numerator, divided by the input representation's full scale when that is a normalized
/// integer; a sample without positive support becomes zero with its mask bit clear. The requested
/// weight and coverage maps are written, valid and invalid samples counted, and the band's largest
/// total weight folded into the report's maximum when one was requested. Rows are spread over the
/// operation's workers; each sample is computed alone, so no floating sum is reordered.
/// @param execution the operation.
/// @param model the checked model, for the input representation.
/// @param band the completed terms.
/// @param result the result being built.
/// @param numerators the Float64 numerator image of a peak-normalized sum, or empty for a mean.
/// @return success; `ErrorCode::InvalidData` for a non-finite term, a mean the output type cannot
///         represent, or a requested weight beyond Float32's range; or cancellation.
[[nodiscard]] Status finalizeBand(const ExecutionContext& execution, const DrizzleModel& model,
                                  const DrizzleAccumulatorBand& band, DrizzleResult& result,
                                  std::span<double> numerators);
/// Divides a peak-normalized sum's numerators by their largest value and clamps them to [0,1], or
/// keeps them when none is positive, narrowing once into the output image.
/// @param execution the operation.
/// @param numerators every output sample's numerator, checked finite by `finalizeBand`.
/// @param result the result being built.
/// @return success; `ErrorCode::InvalidData` when a Float32 output cannot represent a kept
///         numerator; or cancellation.
[[nodiscard]] Status normalizeSum(const ExecutionContext& execution, std::span<const double> numerators,
                                  DrizzleResult& result);

} // namespace an::drizzle_detail
