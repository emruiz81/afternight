// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QMetaObject>
#include <QObject>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace an::ui
{

/// Hands the newest value requested since the last delivery to a callback on the thread that owns `context`, once
/// per event-loop turn: a burst of requests - a slider being dragged - costs one delivery, a request from any thread
/// is delivered where the widget it updates lives, and nothing is delivered once `context` is destroyed.
template <typename Value> class CoalescedGuiUpdate
{
public:
    CoalescedGuiUpdate(QObject* context, std::function<void(const Value&)> deliver)
        : m_Context(context), m_State(std::make_shared<State>())
    {
        m_State->deliver = std::move(deliver);
    }

    CoalescedGuiUpdate(const CoalescedGuiUpdate&) = delete;
    CoalescedGuiUpdate& operator=(const CoalescedGuiUpdate&) = delete;

    /// Replaces any value not yet delivered with `value`, and schedules a delivery if none is scheduled.
    void request(Value value)
    {
        {
            std::lock_guard lock(m_State->mutex);
            m_State->pending = std::move(value);
            if (m_State->scheduled)
            {
                return;
            }

            m_State->scheduled = true;
        }

        // The delivery holds the shared state rather than this object, and Qt drops it with `context`.
        QMetaObject::invokeMethod(
            m_Context,
            [state = m_State]() {
                std::optional<Value> value;
                {
                    std::lock_guard lock(state->mutex);
                    value.swap(state->pending);
                    state->scheduled = false;
                }

                if (value)
                {
                    state->deliver(*value);
                }
            },
            Qt::QueuedConnection);
    }

private:
    struct State
    {
        std::mutex mutex;
        std::optional<Value> pending;
        bool scheduled = false;
        std::function<void(const Value&)> deliver;
    };

    QObject* m_Context;
    std::shared_ptr<State> m_State;
};

} // namespace an::ui
