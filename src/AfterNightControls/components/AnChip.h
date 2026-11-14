// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A chip: a small pill that is a button, or in its toggle form a choice that is on or off.

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

/// A pill a chip high: an outline, an optional icon, the text in the caption in semibold, and an optional count after
/// it in the monospace and the secondary text. Its toggle form (`setCheckable(true)`), checked, is the accent's
/// container with no outline, its text, icon and count in the container's text. Hovered and pressed, the state colour
/// is laid over it; disabled, it is drawn at 38 % opacity; with the keyboard's focus it draws the focus ring.
///
/// It is clicked by the pointer, by Space and by Enter, and tells it as any button does (`clicked`, `toggled`).
/// Assistive technology reads it as a button - a check box in its toggle form - named by its text.
class AFTERNIGHTCONTROLS_API AnChip : public QAbstractButton
{
    W_OBJECT(AnChip)

public:
    explicit AnChip(const QString& text = QString(), QWidget* parent = nullptr);

    /// A count shown after the text - "718", "3 new" - or none when empty.
    void setCountText(const QString& count);
    [[nodiscard]] QString countText() const;

    /// A chip high, and as wide as its padding, its icon, its text and its count.
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QString m_count;
    bool m_keyboardFocus = false;
    bool m_hovered = false;
};

} // namespace an::ui
