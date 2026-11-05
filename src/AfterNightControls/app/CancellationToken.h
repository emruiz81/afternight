// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file CancellationToken.h
 * @brief Thread-safe cancellation token for cooperative background abort.
 */
#pragma once

#include <atomic>

namespace an::ui
{

class CancellationToken
{
public:
    void reset() noexcept
    {
        m_cancelRequested.store(false, std::memory_order_release);
    }

    void requestCancel() noexcept
    {
        m_cancelRequested.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool isCancellationRequested() const noexcept
    {
        return m_cancelRequested.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool consumeCancellationRequest() noexcept
    {
        return m_cancelRequested.exchange(false, std::memory_order_acq_rel);
    }

private:
    std::atomic<bool> m_cancelRequested{false};
};

} // namespace an::ui
