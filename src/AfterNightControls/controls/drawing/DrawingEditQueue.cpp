// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/drawing/DrawingEditQueue.h"

#include "controls/drawing/DrawingDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightPlatform/Logging.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <utility>

namespace an::ui::drawing
{

DrawingEditQueue::DrawingEditQueue(std::function<void(const DrawingEditReport&)> landed) : m_landed(std::move(landed))
{
    m_retry.setSingleShot(true);
    m_retry.setInterval(static_cast<int>(kDefaultRetryInterval.count()));
    QObject::connect(&m_retry, &QTimer::timeout, [this]() { static_cast<void>(flush()); });
}

DrawingEditQueue::~DrawingEditQueue() = default;

void DrawingEditQueue::setDocument(std::shared_ptr<DrawingDocument> document)
{
    m_document = std::move(document);
    m_edits.clear();
    m_retry.stop();
}

const std::shared_ptr<DrawingDocument>& DrawingEditQueue::document() const noexcept
{
    return m_document;
}

void DrawingEditQueue::submit(DrawingEdit edit)
{
    if (!edit || !m_document)
    {
        return;
    }
    m_edits.push_back(std::move(edit));
    static_cast<void>(flush());
}

bool DrawingEditQueue::flush()
{
    if (m_flushing)
    {
        // An edit's own landing callback submitted another: it lands with this flush.
        return m_edits.empty();
    }
    if (m_edits.empty() || !m_document)
    {
        return true;
    }

    // One operation for every edit waiting: a stroke's points land together, and a busy session refuses them
    // together, in order.
    const std::shared_ptr<DrawingDocument> document = m_document;
    an::Result<an::consumer::OperationScope> scope = document->session().tryAdmit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview});
    if (!scope.hasValue())
    {
        ++m_refusals;
        if (scope.status().code() == an::ErrorCode::Busy)
        {
            scheduleRetry();
            return false;
        }
        // A session that cannot admit anything any more - closed, or unavailable - lands nothing ever again.
        LOG_WARNING("A drawing's {} queued edits were dropped: {}", m_edits.size(), scope.status().message());
        m_lastFailure = scope.status();
        DrawingEditReport report{.applied = 0u, .dropped = m_edits.size(), .pending = 0u};
        m_edits.clear();
        if (m_landed)
        {
            m_landed(report);
        }
        return true;
    }

    m_flushing = true;
    DrawingEditReport report;
    while (!m_edits.empty())
    {
        const an::Status landed = m_edits.front()(*document, scope->execution());
        if (landed.failed() && landed.code() == an::ErrorCode::Busy)
        {
            break;
        }
        m_edits.pop_front();
        if (landed.failed())
        {
            LOG_WARNING("A drawing edit was refused: {}", landed.message());
            m_lastFailure = landed;
            ++report.dropped;
        }
        else
        {
            ++report.applied;
        }
    }
    scope->end();
    m_flushing = false;

    report.pending = m_edits.size();
    if (!m_edits.empty())
    {
        ++m_refusals;
        scheduleRetry();
    }
    if (m_landed && (report.applied > 0u || report.dropped > 0u))
    {
        m_landed(report);
    }
    return m_edits.empty();
}

std::size_t DrawingEditQueue::pending() const noexcept
{
    return m_edits.size();
}

std::uint64_t DrawingEditQueue::refusals() const noexcept
{
    return m_refusals;
}

const an::Status& DrawingEditQueue::lastFailure() const noexcept
{
    return m_lastFailure;
}

void DrawingEditQueue::setRetryInterval(const std::chrono::milliseconds interval)
{
    m_retry.setInterval(static_cast<int>(interval.count()));
}

void DrawingEditQueue::scheduleRetry()
{
    if (!m_retry.isActive())
    {
        m_retry.start();
    }
}

} // namespace an::ui::drawing
