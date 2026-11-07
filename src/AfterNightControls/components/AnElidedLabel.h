// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A label that keeps the height of a set number of lines whatever text it shows.
#pragma once

#include "AfterNightControlsExport.h"

#include <QLabel>
#include <QSize>
#include <QString>

class QEvent;
class QResizeEvent;

namespace an::ui
{

/// A label of a set number of lines, so what is laid out around it stays where it is whatever text it shows.
///
/// The text is plain and shown from the top, each of its paragraphs wrapped at word boundaries over the label's width
/// and a word too long for a line broken anywhere. When more of it follows than the lines hold, the last line shows the
/// rest of the text run together and cut with an ellipsis, and the whole text is the label's tooltip; a text that fits
/// has none. The label is as tall as its lines in its font and asks for no more width than an ellipsis, so a long text
/// never widens what holds it. It fits the text again when its width or its font changes.
///
/// `QLabel::text()` reads what is shown and `fullText()` what was set. The label sets its own `QLabel` text and
/// tooltip: a caller sets the text through `setFullText`.
class AFTERNIGHTCONTROLS_API AnElidedLabel : public QLabel
{
public:
    /// A label of `lines` lines, at least one, showing nothing.
    explicit AnElidedLabel(int lines = 1, QWidget* parent = nullptr);

    /// Sets the text the label shows within its lines.
    void setFullText(const QString& text);

    /// The text as it was set.
    [[nodiscard]] const QString& fullText() const noexcept;

    /// How many lines the label holds.
    [[nodiscard]] int lineCount() const noexcept;

    /// Whether some of the text is left out at the label's width.
    [[nodiscard]] bool isElided() const noexcept;

    /// As tall as the label's lines and as wide as an ellipsis, whatever the text.
    [[nodiscard]] QSize sizeHint() const override;

    /// The same as `sizeHint`.
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    // Fits the text to the label's width and lines, and shows what fits.
    void fitText();

    int m_lines = 1;
    QString m_fullText;
    bool m_elided = false;
};

} // namespace an::ui
