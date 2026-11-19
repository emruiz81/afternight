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
class QPainter;
class QRectF;

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
        Info,

        /// A hairline of the line colour over no fill, its text the secondary text in the regular weight: a quiet
        /// fact - a stage a way of stacking goes through, where a project came from.
        Outlined
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

    /// The size of a tag of a text and a tone, with an icon or without, in the tokens in force: what a painter that
    /// draws a tag in a part of its own - a palette's row, a menu's entry - reserves for it.
    [[nodiscard]] static QSize tagSize(const QString& text, Tone tone, bool withIcon = false);

    /// Paints a tag of a text and a tone over `bounds`, as a tag widget paints itself. The painter is left as it was.
    static void paintTag(QPainter& painter, const QRectF& bounds, const QString& text, Tone tone,
                         const QIcon& icon = QIcon());

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Tone m_tone = Tone::Neutral;
    QIcon m_icon;
};

} // namespace an::ui
