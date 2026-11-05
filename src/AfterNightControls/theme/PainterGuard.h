// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QPainter>

namespace an::ui
{

class PainterGuard
{
public:
    explicit PainterGuard(QPainter* painter) : m_Painter(painter)
    {
        if (m_Painter)
        {
            m_Painter->save();
        }
    }

    ~PainterGuard()
    {
        if (m_Painter)
        {
            m_Painter->restore();
        }
    }

    PainterGuard(const PainterGuard&) = delete;
    PainterGuard& operator=(const PainterGuard&) = delete;

private:
    QPainter* m_Painter = nullptr;
};

} // namespace an::ui
