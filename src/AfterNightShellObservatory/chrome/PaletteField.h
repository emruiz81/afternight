// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The command palette's field in the top bar: where a click, or `Ctrl+K`, opens the palette.

#include "AfterNightShellObservatoryExport.h"

#include <QAbstractButton>
#include <QIcon>
#include <QString>

#include <functional>

class QEvent;
class QPaintEvent;

namespace an::ui::observatory
{

/// The palette's field: on the third surface with the small parts' corners, a search icon, "Search or run a command"
/// in the tertiary text and the key hint `Ctrl K` at its right; the bar's height less 14. It is 340 px wide in a window
/// of 1200 px or more, 240 px under it, and under 1100 px an icon button. A click opens the palette.
class AFTERNIGHTSHELLOBSERVATORY_API PaletteField final : public QAbstractButton
{
public:
    /// The forms the window's width gives it.
    enum class Form
    {
        Wide,
        Narrow,
        Icon
    };

    explicit PaletteField(QWidget* parent = nullptr);

    /// The form a window of `width` gives the field.
    [[nodiscard]] static Form formFor(int windowWidth);

    /// What gives the field its search icon, in the theme's colours; with none, the icon's file.
    void setIconProvider(std::function<QIcon(const QString& name)> provider);

    /// Takes the form a window of `width` gives it.
    void setWindowWidth(int width);
    [[nodiscard]] Form form() const noexcept;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    Form m_form = Form::Wide;
    std::function<QIcon(const QString& name)> m_icons;
};

} // namespace an::ui::observatory
