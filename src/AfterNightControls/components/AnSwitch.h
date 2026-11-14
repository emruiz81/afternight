// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A switch: a setting that is on or off and acts at once.

#include "AfterNightControlsExport.h"

#include <QAbstractButton>
#include <QSize>
#include <QString>

#include <verdigris/wobjectdefs.h>

class QEnterEvent;
class QEvent;
class QFocusEvent;
class QKeyEvent;
class QPaintEvent;

namespace an::ui
{

/// A track 32 x 18 with a knob, and an optional label at its right in the body text. Off, the track is the fourth
/// surface inside a two-pixel outline and a 10 px knob of the secondary text sits at its left; on, the track is the
/// accent and a 14 px knob of the text on the accent sits at its right carrying a check, so the state is never told by
/// colour alone. The switch is an icon button's row high. Hovered and pressed, the state colour is laid over the track;
/// disabled, it is drawn at 38 % opacity; with the keyboard's focus the track draws the focus ring.
///
/// It is checkable from the start and toggled by the pointer anywhere on it, its label included, by Space and by
/// Enter; it tells it as any button does (`toggled`). Assistive technology reads it as a check box named by its label.
class AFTERNIGHTCONTROLS_API AnSwitch : public QAbstractButton
{
    W_OBJECT(AnSwitch)

public:
    explicit AnSwitch(const QString& text = QString(), QWidget* parent = nullptr);

    /// The track, and the label's gap and width when it has one; an icon button's row high.
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    [[nodiscard]] bool hitButton(const QPoint& position) const override;

private:
    bool m_keyboardFocus = false;
    bool m_hovered = false;
};

} // namespace an::ui
