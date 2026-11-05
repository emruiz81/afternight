// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "AfterNightAppListener.h"
#include "controls/IAppContext.h"

AfterNightAppListener::AfterNightAppListener(an::ui::IAppEventBus* eventBus) : m_eventBus(eventBus)
{
    if (m_eventBus)
        m_eventBus->subscribeListener(this);
}

AfterNightAppListener::~AfterNightAppListener()
{
    if (m_eventBus)
    {
        m_eventBus->unsubscribeListener(this);
    }
}
