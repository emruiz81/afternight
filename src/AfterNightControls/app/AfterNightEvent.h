// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QEvent>
#include <atomic>

class AFTERNIGHTCONTROLS_API AfterNightEvent : public QEvent
{
public:
    AfterNightEvent(std::atomic_bool* finished = nullptr);

    static const QEvent::Type getEventType()
    {
        static const QEvent::Type eventType = static_cast<QEvent::Type>(QEvent::User + 1);
        return eventType;
    }

    // Only callable in the main thread, normally you don't call this method, you call schedule() instead
    void run()
    {
        try
        {
            if (m_Finihed)
            {
                *m_Finihed = false;
            }

            runImpl();

            if (m_Finihed)
            {
                *m_Finihed = true;
            }
        }
        catch (...)
        {
            if (m_Finihed)
            {
                *m_Finihed = true;
            }

            throw;
        }
    }

    // Safe to call from any thread
    void schedule(int priority = 0);

    virtual ~AfterNightEvent();

    std::atomic_bool* m_Finihed;

protected:
    virtual void runImpl() = 0;

private:
};