// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A scroll area that scrolls up and down alone.

#include <QScrollArea>
#include <QScrollBar>
#include <QSize>
#include <QWidget>

#include <algorithm>

namespace an::ui
{

/// A scroll area that scrolls up and down alone, and so is never narrower than what it holds needs beside its bar. A
/// plain scroll area asks for no more than its bars' width: in a narrow place it would cut its content at the right,
/// or give the content less room than it needs, so that a row lays its controls over each other and cuts their texts.
/// Made, laid out and used on the GUI thread.
class AnVerticalScrollArea final : public QScrollArea
{
public:
    explicit AnVerticalScrollArea(QWidget* parent = nullptr) : QScrollArea(parent)
    {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        QSize least = QScrollArea::minimumSizeHint();
        if (const QWidget* const content = widget())
        {
            least.setWidth((std::max)(least.width(), content->minimumSizeHint().width() +
                                                         verticalScrollBar()->sizeHint().width() + 2 * frameWidth()));
        }
        return least;
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return QScrollArea::sizeHint().expandedTo(minimumSizeHint());
    }
};

} // namespace an::ui
