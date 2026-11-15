// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// An icon button: a square button that shows an icon and names itself in its tooltip.

#include "AfterNightControlsExport.h"

#include <QAbstractButton>
#include <QSize>
#include <QString>

#include <verdigris/wobjectdefs.h>

class QEnterEvent;
class QEvent;
class QFocusEvent;
class QPaintEvent;

namespace an::ui
{

/// A square a strip's icon button high (`metrics.controlXSmall`) holding a 16 px icon drawn in the secondary text. Its
/// toggle form (`setCheckable(true)`), checked, is the accent's container with the icon in the container's text.
/// Hovered and pressed, the state colour is laid over it; disabled, it is drawn at 38 % opacity; with the keyboard's
/// focus it draws the focus ring.
///
/// Its text is what it does - "Zoom in", "Open the Histogram window (Ctrl+H)" - and is never drawn: it is the tooltip
/// and the name assistive technology reads, which reads it as a button, a check box in its toggle form.
class AFTERNIGHTCONTROLS_API AnIconButton : public QAbstractButton
{
    W_OBJECT(AnIconButton)

public:
    /// A button showing the product's icon of a key (`:/AfterNight/icons/SVG/<key>.svg`) and doing what `text` says.
    explicit AnIconButton(const QString& iconKey = QString(), const QString& text = QString(),
                          QWidget* parent = nullptr);

    /// The key of the icon it shows.
    [[nodiscard]] QString iconKey() const;

    /// Shows the product's icon of another key.
    void setIconKey(const QString& key);

    /// Says what it does: its text, its tooltip and its accessible name.
    void setDescription(const QString& text);

    /// A strip's icon button high and as wide.
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QString m_iconKey;
    bool m_keyboardFocus = false;
    bool m_hovered = false;
};

} // namespace an::ui
