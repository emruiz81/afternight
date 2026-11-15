// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A button that is an action: it shows the action's icon and does what the action does.

#include "AfterNightControlsExport.h"

#include <QAbstractButton>
#include <QMetaObject>
#include <QPointer>
#include <QSize>

#include <verdigris/wobjectdefs.h>

class QAction;
class QEnterEvent;
class QEvent;
class QFocusEvent;
class QPaintEvent;

namespace an::ui
{

/// A square button standing for an action that is someone else's - a view's Zoom In, its Undo - so that what shows it
/// shows the action's own state: enabled or not, shown or hidden, checkable and checked or not, its text as the
/// button's text and accessible name and its tool tip as the button's. A click triggers the action, and the button
/// follows every change of it. With no action, or once the action is gone, it is hidden.
///
/// It draws the action's icon in one colour, at its icon side in the middle of its square: in its surface tone the
/// secondary text, the primary text while hovered, and checked the accent's container with its text; in its tone over
/// images the secondary text over images, the text over images while hovered, and checked the accent over images with
/// its text. Hovered and pressed, a state layer is laid over it; disabled it is drawn at 38 % opacity; with the
/// keyboard's focus it draws the focus ring.
class AFTERNIGHTCONTROLS_API AnActionButton : public QAbstractButton
{
    W_OBJECT(AnActionButton)

public:
    /// What the button lies on.
    enum class Tone
    {
        /// A surface of the interface: a strip, a panel.
        Surface,

        /// A scrim laid over an image: the floating toolbar.
        OnImage
    };

    explicit AnActionButton(QAction* action = nullptr, QWidget* parent = nullptr);
    ~AnActionButton() override;

    AnActionButton(const AnActionButton&) = delete;
    AnActionButton& operator=(const AnActionButton&) = delete;

    /// Stands for another action, or for none.
    void setAction(QAction* action);

    /// The action it stands for, or null.
    [[nodiscard]] QAction* action() const;

    void setTone(Tone tone);
    [[nodiscard]] Tone tone() const noexcept;

    /// How wide and high the button is, and its icon; a side of 0 is a small control's (`metrics.controlSmall`).
    void setSides(int side, int iconSide);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    /// Takes the action's state.
    void follow();

    QPointer<QAction> m_action;
    QMetaObject::Connection m_changed;
    QMetaObject::Connection m_toggled;
    QMetaObject::Connection m_gone;
    Tone m_tone = Tone::Surface;
    int m_side = 0;
    int m_iconSide = 16;
    bool m_keyboardFocus = false;
    bool m_hovered = false;
};

} // namespace an::ui
