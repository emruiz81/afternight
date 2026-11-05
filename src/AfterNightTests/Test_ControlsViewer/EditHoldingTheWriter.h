// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// An edit of a view that holds the document's writer and asks to be admitted only when the case lets it, for the cases
/// that hold work which reads the same image to the order every reader of a view's image keeps: its read lease first,
/// then its admission. An edit takes the writer and then asks to be admitted, so at a session one operation wide work
/// that was admitted first and then waited for a read lease would wait for the edit and the edit for it.
#pragma once

#include "controls/ImageViewControl.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <QCoreApplication>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace an::tests::viewer
{

/// How long a case gives work it started to be admitted, had that work asked for its admission before its lease. Work
/// that takes its lease first is never admitted while the writer is held, so the wait runs to the end and only bounds
/// the case.
inline constexpr std::chrono::milliseconds kAdmissionGrace{500};

/// Pumps the event loop until the session has admitted more than `before` operations or `kAdmissionGrace` has passed.
///
/// @return whether the session admitted another operation.
[[nodiscard]] inline bool admittedBeyond(const an::consumer::EngineSession& session, std::uint64_t before)
{
    const auto until = std::chrono::steady_clock::now() + kAdmissionGrace;
    while (std::chrono::steady_clock::now() < until)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (session.admission().admitted > before)
        {
            return true;
        }
    }
    return false;
}

/// An edit of a view, started on construction: its worker takes the document's writer, as every edit does, says so,
/// and waits for `askToBeAdmitted` before it asks the session for an operation, as an edit asks once it holds the
/// writer. The admission waits at most `patience`, so work that holds the session's only slot while it waits for the
/// writer ends the case rather than hanging it. The edit changes nothing: it fails once it knows whether it was
/// admitted, and the view lets the writer go.
class EditHoldingTheWriter
{
public:
    explicit EditHoldingTheWriter(ImageViewControl& view, std::chrono::milliseconds patience = std::chrono::seconds(5))
        : m_state(std::make_shared<State>())
    {
        view.editImage(
            "Hold the writer", "",
            [state = m_state, patience](const an::consumer::EngineSession& session,
                                        const an::consumer::AssetWriteLease&,
                                        std::stop_token stop) -> an::Result<an::consumer::ImageAsset> {
                {
                    std::unique_lock lock(state->mutex);
                    state->holdsWriter = true;
                    state->changed.notify_all();
                    // Bounded, so a case that never lets the edit go on does not leave its worker behind for ever.
                    state->changed.wait_for(lock, std::chrono::seconds(60),
                                            [&state, &stop]() { return state->mayAsk || stop.stop_requested(); });
                }
                std::stop_source giveUp;
                std::mutex waiting;
                std::condition_variable answered;
                bool hasAnswer = false;
                std::thread watchdog([&]() {
                    std::unique_lock lock(waiting);
                    if (!answered.wait_for(lock, patience, [&hasAnswer]() { return hasAnswer; }))
                    {
                        giveUp.request_stop();
                    }
                });
                an::Result<an::consumer::OperationScope> scope = session.admit(an::consumer::OperationRequest{
                    .purpose = an::consumer::OperationPurpose::Apply, .stopToken = giveUp.get_token()});
                {
                    const std::lock_guard lock(waiting);
                    hasAnswer = true;
                }
                answered.notify_all();
                watchdog.join();
                {
                    const std::lock_guard lock(state->mutex);
                    state->admitted = scope.hasValue();
                }
                return an::Status::failure(an::ErrorCode::Cancelled, "the case's edit changes nothing");
            },
            [state = m_state](bool) {
                const std::lock_guard lock(state->mutex);
                state->finished = true;
            });
    }

    EditHoldingTheWriter(const EditHoldingTheWriter&) = delete;
    EditHoldingTheWriter& operator=(const EditHoldingTheWriter&) = delete;

    /// Lets a worker still waiting to ask go on, so it never outlives the case by more than its patience.
    ~EditHoldingTheWriter()
    {
        askToBeAdmitted();
    }

    /// Whether the edit's worker holds the document's writer and waits to be let go on.
    [[nodiscard]] bool holdsTheWriter() const
    {
        const std::lock_guard lock(m_state->mutex);
        return m_state->holdsWriter;
    }

    /// Lets the edit ask to be admitted.
    void askToBeAdmitted()
    {
        {
            const std::lock_guard lock(m_state->mutex);
            m_state->mayAsk = true;
        }
        m_state->changed.notify_all();
    }

    /// Whether the view has finished with the edit, which it does on the GUI thread.
    [[nodiscard]] bool finished() const
    {
        const std::lock_guard lock(m_state->mutex);
        return m_state->finished;
    }

    /// Whether the edit was admitted within its patience.
    [[nodiscard]] bool wasAdmitted() const
    {
        const std::lock_guard lock(m_state->mutex);
        return m_state->admitted;
    }

private:
    struct State
    {
        mutable std::mutex mutex;
        std::condition_variable changed;
        bool holdsWriter = false;
        bool mayAsk = false;
        bool admitted = false;
        bool finished = false;
    };

    std::shared_ptr<State> m_state;
};

} // namespace an::tests::viewer
