// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <atomic>
#include <cstdint>

namespace an::ui
{

/// The requests for a read one worker serves - a setting read off the GUI thread, say: the first request starts the
/// worker, and the worker reads once more for the requests made while it read. No request is lost to a read already on
/// its way, because the worker counts the requests it answers before it reads and takes only those off afterwards: a
/// request made during the read leaves a count the worker finds when it ends, and reads again for; one made after the
/// worker took its count off finds the count at zero and starts a worker of its own. There is one worker at a time.
///
/// Thread-safe; the three calls may come from any thread.
class ReadRequests
{
public:
    /// Records a request. True when no read is on its way, so the caller starts one; false when the read on its way
    /// reads once more for it.
    [[nodiscard]] bool request() noexcept
    {
        return m_Pending.fetch_add(1u, std::memory_order_acq_rel) == 0u;
    }

    /// What the worker calls before it reads: the requests this read answers.
    [[nodiscard]] std::uint32_t begin() const noexcept
    {
        return m_Pending.load(std::memory_order_acquire);
    }

    /// What the worker calls after it read, with what `begin` gave it: true when requests were made meanwhile, so it
    /// reads once more; false when none were, so it ends.
    [[nodiscard]] bool end(const std::uint32_t answered) noexcept
    {
        return m_Pending.fetch_sub(answered, std::memory_order_acq_rel) != answered;
    }

private:
    std::atomic<std::uint32_t> m_Pending{0u};
};

} // namespace an::ui
