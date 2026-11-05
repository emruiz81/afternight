// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// Lightweight header for ProcessControlEvent and FunctionEvent.
/// Include this instead of ProcessCommonUtils.h when you only need event types.

#include "app/AfterNightEvent.h"

#include <atomic>
#include <concepts>
#include <functional>
#include <utility>

template <typename Callback> class ProcessControlEvent : public AfterNightEvent
{
public:
    explicit ProcessControlEvent(Callback callback, std::atomic_bool* finished = nullptr)
        : AfterNightEvent(finished), m_Callback(std::move(callback))
    {
    }

protected:
    void runImpl() override
    {
        // Guard optional callback-like types (e.g. std::function) against empty state.
        if constexpr (requires(const Callback& cb) {
                          { static_cast<bool>(cb) } -> std::convertible_to<bool>;
                      })
        {
            if (!static_cast<bool>(m_Callback))
            {
                return;
            }
        }

        m_Callback();
    }

private:
    Callback m_Callback;
};

// Global alias kept for backward compatibility with existing call sites.
using FunctionEvent = ProcessControlEvent<std::function<void()>>;

namespace an::ui
{
// Namespaced alias — prefer this in new code.
using FunctionEvent = ::FunctionEvent;
} // namespace an::ui
