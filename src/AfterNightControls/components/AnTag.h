// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A tag: a short text on a small square-cornered fill of its tone.

#include "AfterNightControlsExport.h"

#include <QIcon>
#include <QLabel>
#include <QSize>
#include <QString>

#include <verdigris/wobjectdefs.h>

class QPaintEvent;

namespace an::ui
{

/// A short text - a state, a kind, a count - on a fill of its tone: a tag high, its corners 4 px, its text the caption
/// in semibold, with an optional icon before it in the text's colour. The neutral tone is the fourth surface under the
/// secondary text; the accent tone the accent's container; the status tones their soft share under their colour, and a
/// warning is outlined in its colour too, so that it is never told by its hue alone.
///
/// The text is `QLabel::text()`; assistive technology reads the tag as static text by it. Colours are read where the
/// tag paints and its size in sizeHint(), so a theme or a density reaches it at once.
class AFTERNIGHTCONTROLS_API AnTag : public QLabel
{
    W_OBJECT(AnTag)

public:
    /// What a tag tells by its colours.
    enum class Tone
    {
        Neutral,
        Accent,
        Success,
        Warning,
        Error,
        Info
    };

    explicit AnTag(const QString& text = QString(), Tone tone = Tone::Neutral, QWidget* parent = nullptr);

    void setTone(Tone tone);
    [[nodiscard]] Tone tone() const noexcept;

    /// An icon before the text, drawn in the text's colour; a null icon shows none.
    void setIcon(const QIcon& icon);
    [[nodiscard]] QIcon icon() const;

    /// A tag high, and as wide as its padding, its icon and its text.
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Tone m_tone = Tone::Neutral;
    QIcon m_icon;
};

} // namespace an::ui
