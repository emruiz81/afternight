// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "AfterNightEvent.h"
#include "controls/IAppContext.h"

#include <cassert>

AfterNightEvent::AfterNightEvent(std::atomic_bool* finished) : QEvent(getEventType()), m_Finihed(finished) {}

void AfterNightEvent::schedule(int priority)
{
    auto* context = an::ui::appContext();
    assert(context && "AfterNightEvent::schedule() requires an installed app context");

    auto* event_bus = context ? context->getAppEventBus() : nullptr;
    assert(event_bus && "AfterNightEvent::schedule() requires an app event bus");
    if (!event_bus)
    {
        return;
    }

    event_bus->postAfterNightEvent(this, priority);
}

AfterNightEvent::~AfterNightEvent() {}
