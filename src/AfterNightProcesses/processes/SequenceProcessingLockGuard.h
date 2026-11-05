// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "controls/ImageViewControl.h"

#include <QPointer>

namespace process_utils
{

class SequenceProcessingLockUnavailable final
{
};

class SequenceProcessingLockGuard final
{
public:
    explicit SequenceProcessingLockGuard(ImageViewControl* control) : m_Control(control) {}

    [[nodiscard]] bool tryLock()
    {
        if (!m_Control)
        {
            return false;
        }

        m_Locked = m_Control->tryRequestLockForProcessing();
        return m_Locked;
    }

    ~SequenceProcessingLockGuard()
    {
        if (m_Locked && m_Control)
        {
            m_Control->releaseLockForProcessing();
        }
    }

    SequenceProcessingLockGuard(const SequenceProcessingLockGuard&) = delete;
    SequenceProcessingLockGuard& operator=(const SequenceProcessingLockGuard&) = delete;

private:
    QPointer<ImageViewControl> m_Control;
    bool m_Locked = false;
};

} // namespace process_utils
