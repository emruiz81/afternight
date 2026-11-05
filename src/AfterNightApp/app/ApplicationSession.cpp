// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The engine session the application owns, and the order it is shut down in.

#include "app/ApplicationSession.h"

#include "app/FilePolicies.h"

#include <AfterNightPlatform/Logging.h>

#include <QCoreApplication>
#include <QEvent>
#include <QThreadPool>

#include <cassert>
#include <utility>

namespace an::ui
{

an::consumer::EngineSessionSettings applicationSessionSettings(const ProcessingLimits& limits)
{
    an::consumer::EngineSessionSettings base;
    base.codecRegistry = []() { return createProductCodecRegistry(); };
    return sessionSettingsForLimits(limits, std::move(base));
}

ApplicationSession::~ApplicationSession()
{
    closeAdmission();
}

an::Status ApplicationSession::open(const ProcessingLimits& limits)
{
    if (m_session.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidState, "the application's engine session is already open");
    }

    an::Result<an::consumer::EngineSession> opened =
        an::consumer::EngineSession::open(applicationSessionSettings(limits));
    if (!opened.hasValue())
    {
        return opened.status();
    }

    m_session.emplace(std::move(*opened));
    LOG_INFO("The engine session is open: {} threads, a memory budget of {} bytes", m_session->width(),
             m_session->limits().memoryBudgetBytes);
    return an::Status::success();
}

bool ApplicationSession::isOpen() const noexcept
{
    return m_session.has_value();
}

const an::consumer::EngineSession& ApplicationSession::engineSession() const noexcept
{
    assert(m_session.has_value());
    return *m_session;
}

void ApplicationSession::closeAdmission() noexcept
{
    if (m_session.has_value())
    {
        m_session->closeAdmission();
    }
}

an::Status ApplicationSession::shutdown()
{
    if (!m_session.has_value())
    {
        return an::Status::success();
    }

    // Nothing new is admitted from here on, so the work still queued on the pool finishes or is refused rather than
    // starting more.
    m_session->closeAdmission();
    QThreadPool::globalInstance()->waitForDone();

    const an::Status shut = m_session->shutdown();
    if (shut.failed())
    {
        LOG_WARNING("The engine session did not shut down cleanly: {}", shut.message());
    }
    return shut;
}

an::Status shutDownApplication(ApplicationSession& session, const std::function<void()>& destroyMainWindow)
{
    session.closeAdmission();
    if (destroyMainWindow)
    {
        destroyMainWindow();
    }
    // A window closed while the application closed was only marked for deletion; with the event loop gone, nothing
    // else deletes it.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // Work still on the pool finishes or is refused, and what it posted back for the GUI thread - a result on its way
    // to a view that is gone, holding the image it was computed from - is delivered and let go before the checked
    // shutdown counts what is outstanding.
    session.closeAdmission();
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::sendPostedEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return session.shutdown();
}

} // namespace an::ui
