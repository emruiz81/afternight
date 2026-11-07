// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnElidedLabel.h"

#include <QEvent>
#include <QFontMetrics>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStringList>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>

namespace
{

/// What of a text a label's lines show, and whether anything was left out.
struct FittedText
{
    QString text;
    bool elided = false;
};

/// The text without the spaces that end it, which a line wrapped at a word boundary keeps.
QString withoutTrailingSpaces(QString text)
{
    while (!text.isEmpty() && text.back().isSpace())
    {
        text.chop(1);
    }
    return text;
}

/// The text as `lines` lines of `font`, `width` pixels wide, show it: each paragraph wrapped at word boundaries - a
/// word too long for a line anywhere - and, on the last line, the rest of the text with its paragraphs run together,
/// cut with an ellipsis when it does not fit.
FittedText fitToLines(const QString& text, const QFont& font, const QPaintDevice* device, const int width,
                      const int lines)
{
    if (text.isEmpty() || width <= 0)
    {
        return FittedText{.text = text};
    }
    const QStringList paragraphs = text.split(QLatin1Char('\n'));
    QStringList shown;
    for (qsizetype index = 0; index < paragraphs.size(); ++index)
    {
        const QString& paragraph = paragraphs.at(index);
        QTextLayout layout(paragraph, font, device);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine())
        {
            line.setLineWidth(width);
            if (static_cast<int>(shown.size()) + 1 < lines)
            {
                shown.push_back(withoutTrailingSpaces(paragraph.mid(line.textStart(), line.textLength())));
                continue;
            }
            QStringList rest{paragraph.mid(line.textStart())};
            for (qsizetype next = index + 1; next < paragraphs.size(); ++next)
            {
                rest.push_back(paragraphs.at(next));
            }
            const QString remaining = withoutTrailingSpaces(rest.join(QLatin1Char(' ')));
            const QString fitted = QFontMetrics(font, device).elidedText(remaining, Qt::ElideRight, width);
            layout.endLayout();
            shown.push_back(fitted);
            return FittedText{.text = shown.join(QLatin1Char('\n')), .elided = fitted != remaining};
        }
        layout.endLayout();
    }
    return FittedText{.text = shown.join(QLatin1Char('\n'))};
}

} // namespace

namespace an::ui
{

AnElidedLabel::AnElidedLabel(const int lines, QWidget* parent) : QLabel(parent), m_lines((std::max)(lines, 1))
{
    setTextFormat(Qt::PlainText);
    setWordWrap(false);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void AnElidedLabel::setFullText(const QString& text)
{
    m_fullText = text;
    fitText();
}

const QString& AnElidedLabel::fullText() const noexcept
{
    return m_fullText;
}

int AnElidedLabel::lineCount() const noexcept
{
    return m_lines;
}

bool AnElidedLabel::isElided() const noexcept
{
    return m_elided;
}

QSize AnElidedLabel::sizeHint() const
{
    const QMargins margins = contentsMargins();
    const QFontMetrics metrics = fontMetrics();
    return QSize(metrics.horizontalAdvance(QChar(0x2026)) + margins.left() + margins.right() + 2 * margin(),
                 m_lines * metrics.lineSpacing() + margins.top() + margins.bottom() + 2 * margin());
}

QSize AnElidedLabel::minimumSizeHint() const
{
    return sizeHint();
}

void AnElidedLabel::resizeEvent(QResizeEvent* event)
{
    QLabel::resizeEvent(event);
    if (event->size().width() != event->oldSize().width())
    {
        fitText();
    }
}

void AnElidedLabel::changeEvent(QEvent* event)
{
    QLabel::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::ContentsRectChange)
    {
        updateGeometry();
        fitText();
    }
}

void AnElidedLabel::fitText()
{
    const int width = contentsRect().width() - 2 * margin() - (std::max)(indent(), 0);
    const FittedText fitted = fitToLines(m_fullText, font(), this, width, m_lines);
    m_elided = fitted.elided;
    setToolTip(m_elided ? m_fullText : QString());
    QLabel::setText(fitted.text);
}

} // namespace an::ui
