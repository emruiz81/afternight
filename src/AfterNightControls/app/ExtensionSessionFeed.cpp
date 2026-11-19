// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ExtensionSessionFeed.h"

#include "controls/ImageViewControl.h"

#include <algorithm>
#include <utility>

namespace an::ui::extensions
{

bool sameStanding(const ExtensionSessionStanding& a, const ExtensionSessionStanding& b)
{
    return a.sessionId == b.sessionId && a.descriptorId == b.descriptorId && a.displayName == b.displayName &&
           a.target.data() == b.target.data() && a.host == b.host && a.run == b.run &&
           a.raisesItsWindow == b.raisesItsWindow && a.hostedPanel == b.hostedPanel;
}

ExtensionSessionReport::ExtensionSessionReport() = default;
ExtensionSessionReport::~ExtensionSessionReport() = default;

std::size_t ExtensionSessionReport::observe(IExtensionSessionFeed::Observer observer)
{
    const std::size_t token = m_nextToken++;
    m_observers.emplace(token, std::move(observer));
    return token;
}

void ExtensionSessionReport::stopObserving(const std::size_t token)
{
    m_observers.erase(token);
}

void ExtensionSessionReport::stood(const ExtensionSessionStanding& standing)
{
    const auto known = std::find_if(m_told.begin(), m_told.end(), [&standing](const ExtensionSessionStanding& told) {
        return told.sessionId == standing.sessionId;
    });
    if (known != m_told.end())
    {
        if (sameStanding(*known, standing))
        {
            return;
        }
        *known = standing;
    }
    else
    {
        m_told.push_back(standing);
    }
    // An observer may come or go while it is told: the ones told are the ones there now.
    const std::map<std::size_t, IExtensionSessionFeed::Observer> told = m_observers;
    for (const auto& [token, observer] : told)
    {
        if (observer.stood && m_observers.contains(token))
        {
            observer.stood(standing);
        }
    }
}

void ExtensionSessionReport::ended(const ExtensionSessionEnd& end)
{
    const auto known = std::find_if(m_told.begin(), m_told.end(), [&end](const ExtensionSessionStanding& told) {
        return told.sessionId == end.sessionId;
    });
    if (known == m_told.end())
    {
        return;
    }
    m_told.erase(known);
    const std::map<std::size_t, IExtensionSessionFeed::Observer> told = m_observers;
    for (const auto& [token, observer] : told)
    {
        if (observer.ended && m_observers.contains(token))
        {
            observer.ended(end);
        }
    }
}

std::vector<ExtensionSessionStanding> ExtensionSessionReport::told() const
{
    return m_told;
}

} // namespace an::ui::extensions
