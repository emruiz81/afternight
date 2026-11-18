// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A stand-in for the extension host's launch coordinator as a shell hears it: the sessions a case starts, moves on and
/// ends by hand, told to observers as the coordinator tells them, and the raises and closes a shell asks of it.

#pragma once

#include "app/ExtensionSessionFeed.h"
#include "controls/ProcessPresentation.h"

#include <QPointer>
#include <QString>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class ImageViewControl;

namespace an::tests::shellcore
{

/// The sessions a case plays the host manager for.
class StandInExtensionSessions final : public an::ui::extensions::IExtensionSessionFeed
{
public:
    using Standing = an::ui::extensions::ExtensionSessionStanding;

    /// A session of `descriptorId` on `target`, its host starting.
    [[nodiscard]] static Standing session(const std::string& sessionId, const std::string& descriptorId,
                                          const QString& displayName, ImageViewControl* target = nullptr)
    {
        Standing standing;
        standing.sessionId = sessionId;
        standing.descriptorId = descriptorId;
        standing.displayName = displayName;
        standing.target = target;
        standing.host = an::ui::extensions::ExtensionHostStanding::Starting;
        return standing;
    }

    /// A session starts, or where it stands changed.
    void stand(const Standing& standing)
    {
        m_report.stood(standing);
    }

    /// A session ends: as it should, or failed with why.
    void end(const std::string& sessionId, const bool failed = false, const QString& reason = {})
    {
        m_report.ended(
            an::ui::extensions::ExtensionSessionEnd{.sessionId = sessionId, .failed = failed, .reason = reason});
    }

    [[nodiscard]] std::vector<Standing> sessionStandings() const override
    {
        return m_report.told();
    }

    std::size_t observeSessions(Observer observer) override
    {
        ++m_observers;
        return m_report.observe(std::move(observer));
    }

    void stopObservingSessions(const std::size_t token) override
    {
        --m_observers;
        m_report.stopObserving(token);
    }

    bool raiseSessionWindow(const std::string_view sessionId, std::string* error = nullptr) override
    {
        const std::optional<Standing> standing = told(sessionId);
        if (!standing.has_value() || !standing->raisesItsWindow)
        {
            if (error != nullptr)
            {
                *error = "the host does not raise its window";
            }
            return false;
        }
        m_raised.emplace_back(sessionId);
        return true;
    }

    void closeSession(const std::string_view sessionId) override
    {
        m_closed.emplace_back(sessionId);
        end(std::string(sessionId));
    }

    /// The sessions whose window a shell asked to raise, in order.
    [[nodiscard]] const std::vector<std::string>& raised() const noexcept
    {
        return m_raised;
    }

    /// The sessions a shell closed, in order.
    [[nodiscard]] const std::vector<std::string>& closed() const noexcept
    {
        return m_closed;
    }

    /// How many observers listen now.
    [[nodiscard]] int observers() const noexcept
    {
        return m_observers;
    }

private:
    [[nodiscard]] std::optional<Standing> told(const std::string_view sessionId) const
    {
        const std::vector<Standing> standings = m_report.told();
        const auto found = std::find_if(standings.begin(), standings.end(), [sessionId](const Standing& standing) {
            return standing.sessionId == sessionId;
        });
        return found != standings.end() ? std::optional<Standing>(*found) : std::nullopt;
    }

    an::ui::extensions::ExtensionSessionReport m_report;
    std::vector<std::string> m_raised;
    std::vector<std::string> m_closed;
    int m_observers = 0;
};

} // namespace an::tests::shellcore
