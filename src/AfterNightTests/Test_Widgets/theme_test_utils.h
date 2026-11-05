// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <gtest/gtest.h>

#include <QColor>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QWidget>

#include "theme/AnDesignTokens.h"

namespace an::tests::widgets
{

class ScopedPaletteOverride final
{
public:
    ScopedPaletteOverride() : m_originalPalette(QGuiApplication::palette()) {}

    // Under the system preference the tokens are resolved from the application's palette, so a case that resolved
    // them under the palette it set leaves them resolved again from the palette it restores.
    ~ScopedPaletteOverride()
    {
        QGuiApplication::setPalette(m_originalPalette);
        an::ui::refreshTokens();
        QCoreApplication::processEvents();
    }

    void setWindowColor(const QColor& color) const
    {
        QPalette palette = QGuiApplication::palette();
        palette.setColor(QPalette::Window, color);
        QGuiApplication::setPalette(palette);
        QCoreApplication::processEvents();
    }

private:
    QPalette m_originalPalette;
};

inline void sendPaletteChange(QWidget* widget)
{
    QEvent ev(QEvent::ApplicationPaletteChange);
    QApplication::sendEvent(widget, &ev);
}

inline void sendThemeChange(QWidget* widget)
{
    QEvent ev(QEvent::ThemeChange);
    QApplication::sendEvent(widget, &ev);
}

inline void assertColorFromToken(const QColor& actual, const QColor& expected, const char* context)
{
    ASSERT_EQ(actual, expected) << context << " color mismatch";
}

inline void assertFontFromToken(const QFont& actual, const QFont& expected, const char* context)
{
    ASSERT_EQ(actual, expected) << context << " font mismatch";
}

} // namespace an::tests::widgets
