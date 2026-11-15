// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A key hint: the keys of a shortcut, as a small outlined box beside what they do.

#include "AfterNightControlsExport.h"

#include <QColor>
#include <QKeySequence>
#include <QLabel>
#include <QSize>
#include <QString>

#include <verdigris/wobjectdefs.h>

class QPainter;
class QPaintEvent;
class QRectF;

namespace an::ui
{

/// The keys of a shortcut - "Ctrl K", "Esc", "Ctrl Enter" - in the monospace at the caption's size, in a box two pixels
/// less than a tag high with 4 px corners. Raised, the box is the second surface inside a hairline and the keys are in
/// the secondary text; on a coloured part - a button, a transient message - it has no fill and draws its outline and
/// its keys in the text colour of its palette (`QPalette::WindowText`), which the part that holds it sets.
///
/// `QLabel::text()` is what is shown; assistive technology reads the shortcut as the platform writes it.
class AFTERNIGHTCONTROLS_API AnKeyHint : public QLabel
{
    W_OBJECT(AnKeyHint)

public:
    /// What the hint lies on.
    enum class Surface
    {
        Raised,
        OnColour
    };

    explicit AnKeyHint(const QKeySequence& keys = QKeySequence(), QWidget* parent = nullptr);

    void setKeySequence(const QKeySequence& keys);
    [[nodiscard]] QKeySequence keySequence() const;

    void setSurface(Surface surface);
    [[nodiscard]] Surface surface() const noexcept;

    /// The keys as the hint writes them: each combination's modifiers and key in the platform's words, separated by
    /// spaces, and combinations separated by commas.
    [[nodiscard]] static QString keysText(const QKeySequence& keys);

    /// The size of a hint showing a text, and the hint painted in a box: how a part that paints its own rows - the
    /// command palette's - draws a shortcut, as the widget draws itself. On a coloured part the hint draws its outline
    /// and its keys in `onColourText`.
    [[nodiscard]] static QSize hintSize(const QString& text);
    static void paintHint(QPainter& painter, const QRectF& box, const QString& text, Surface surface,
                          const QColor& onColourText);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QKeySequence m_keys;
    Surface m_surface = Surface::Raised;
};

} // namespace an::ui
