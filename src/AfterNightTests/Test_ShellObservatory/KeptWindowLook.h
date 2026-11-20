// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a window opened from the Observatory shell is held to when it is one that stays a window: the heading, the
/// footer, the contrast and the surface of the look, in the look the shell is in.

#pragma once

#include "fixtures/WindowLook.h"
#include "theme/AnDesignTokens.h"

#include <QPalette>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <gtest/gtest.h>

namespace an::tests::observatory
{

/// What is wrong with the look of a window that stays a window, as the shell shows it: no heading that says its
/// title, a footer whose rightmost button is not the primary one, text that does not meet the contrast rule, a
/// surface that is not a dialog's. Empty when it has the look.
[[nodiscard]] inline QStringList lookProblemsOfAKeptWindow(const QWidget& opened)
{
    QStringList problems = an::tests::look::headingProblems(opened);
    problems.append(an::tests::look::footerProblems(opened));
    problems.append(an::tests::look::contrastProblems(opened));
    if (opened.palette().color(QPalette::Window) != an::ui::tokens().layers.surface1)
    {
        problems.append(QStringLiteral("the window does not lie on a dialog's surface"));
    }
    return problems;
}

/// Expects a window opened from the shell to have the look of a window that stays a window: a heading that says its
/// title, a footer whose rightmost button is the primary one, text that meets the contrast rule, a dialog's surface,
/// and, where the screen holds it, a place within the screen's available area.
inline void expectTheLookOfAKeptWindow(const QWidget& opened)
{
    const QStringList problems = lookProblemsOfAKeptWindow(opened);
    EXPECT_TRUE(problems.isEmpty()) << problems.join(QStringLiteral("\n  ")).toStdString();
    if (an::tests::look::screenHolds(opened))
    {
        EXPECT_EQ(an::tests::look::standsOutOfItsScreen(opened), QString());
    }
}

} // namespace an::tests::observatory
