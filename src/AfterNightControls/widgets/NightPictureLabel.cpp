// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "widgets/NightPictureLabel.h"

#include <QEvent>

namespace an::ui
{

NightPictureLabel::NightPictureLabel(QWidget* parent) : QLabel(parent) {}

void NightPictureLabel::setPicture(const QPixmap& picture)
{
    m_picture.setPicture(picture);
    // The label keeps the picture's room while a Night form is made, so nothing beside it moves.
    setMinimumSize(picture.deviceIndependentSize().toSize());
    showPicture();
}

void NightPictureLabel::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
    {
        showPicture();
    }
    QLabel::changeEvent(event);
}

void NightPictureLabel::showPicture()
{
    const QPixmap shown = m_picture.shown(this, [this]() { showPicture(); });
    if (shown.isNull())
    {
        clear();
        return;
    }
    // Shown only when it changes: a label told its pixmap again repaints and lays its owner out again.
    if (pixmap().cacheKey() != shown.cacheKey())
    {
        setPixmap(shown);
    }
}

} // namespace an::ui
