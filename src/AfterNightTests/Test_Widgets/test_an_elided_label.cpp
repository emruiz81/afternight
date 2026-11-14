// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The elided label: a text that fits is shown whole, one longer than the label's lines is cut on its last line with an
/// ellipsis and given whole in the tooltip, the label is as tall as its lines whatever its text so what lies below it
/// never moves, and it fits its text again when it is resized or its font changes.

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFont>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QRect>
#include <QSize>
#include <QSizePolicy>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

#include "components/AnElidedLabel.h"

namespace
{

/// A text of many words, longer than a few lines of any narrow label.
QString longText()
{
    QStringList words;
    for (int index = 0; index < 120; ++index)
    {
        words << QStringLiteral("word%1").arg(index);
    }
    return words.join(QLatin1Char(' '));
}

/// The lines a label shows.
QStringList shownLines(const an::ui::AnElidedLabel& label)
{
    return label.text().split(QLatin1Char('\n'));
}

/// The widest line a label shows, in its font.
int widestLine(const an::ui::AnElidedLabel& label)
{
    int widest = 0;
    for (const QString& line : shownLines(label))
    {
        widest = (std::max)(widest, label.fontMetrics().horizontalAdvance(line));
    }
    return widest;
}

/// A shown window holding a label of `lines` lines over a widget that takes the rest of its height, so a resize of the
/// window reaches the label at once.
struct LabelOverContent
{
    explicit LabelOverContent(int lines)
    {
        auto* layout = new QVBoxLayout(&window);
        layout->setContentsMargins(0, 0, 0, 0);
        label = new an::ui::AnElidedLabel(lines, &window);
        content = new QWidget(&window);
        content->setMinimumHeight(100);
        layout->addWidget(label);
        layout->addWidget(content, 1);
        window.resize(240, 400);
        window.show();
        settle();
    }

    /// Lays the window out and delivers what that sends.
    void settle()
    {
        window.layout()->activate();
        QCoreApplication::processEvents();
    }

    QWidget window;
    an::ui::AnElidedLabel* label = nullptr;
    QWidget* content = nullptr;
};

} // namespace

TEST(WidgetsComponentsAnElidedLabel, ATextThatFitsIsShownWholeWithNoTooltip)
{
    an::ui::AnElidedLabel label(3);
    label.resize(400, label.sizeHint().height());
    label.setFullText(QStringLiteral("First line\nSecond line"));

    EXPECT_EQ(label.text(), QStringLiteral("First line\nSecond line"));
    EXPECT_EQ(label.fullText(), QStringLiteral("First line\nSecond line"));
    EXPECT_FALSE(label.isElided());
    EXPECT_TRUE(label.toolTip().isEmpty());
}

TEST(WidgetsComponentsAnElidedLabel, ATextLongerThanItsLinesIsCutOnTheLastLineAndGivenWholeInTheTooltip)
{
    an::ui::AnElidedLabel label(3);
    label.resize(200, label.sizeHint().height());
    const QString text = longText();
    label.setFullText(text);

    const QStringList lines = shownLines(label);
    ASSERT_EQ(lines.size(), 3);
    EXPECT_TRUE(text.startsWith(lines.front())) << "the first line is the start of the text, wrapped at a word";
    EXPECT_FALSE(lines.front().endsWith(QChar(0x2026)));
    EXPECT_TRUE(lines.back().endsWith(QChar(0x2026))) << lines.back().toStdString();
    EXPECT_LE(widestLine(label), label.contentsRect().width()) << "every line fits the label's width";
    EXPECT_TRUE(label.isElided());
    EXPECT_EQ(label.toolTip(), text);
    EXPECT_EQ(label.fullText(), text);
}

TEST(WidgetsComponentsAnElidedLabel, ParagraphsPastTheLastLineAreRunTogetherOnIt)
{
    an::ui::AnElidedLabel label(2);
    label.resize(600, label.sizeHint().height());
    label.setFullText(QStringLiteral("One\nTwo\nThree"));

    EXPECT_EQ(label.text(), QStringLiteral("One\nTwo Three"));
    EXPECT_FALSE(label.isElided()) << "every word is shown";
    EXPECT_TRUE(label.toolTip().isEmpty());
}

TEST(WidgetsComponentsAnElidedLabel, TheLabelIsAsTallAsItsLinesWhateverItsText)
{
    an::ui::AnElidedLabel label(4);
    EXPECT_EQ(label.lineCount(), 4);
    EXPECT_GE(label.sizeHint().height(), 4 * label.fontMetrics().lineSpacing());
    EXPECT_EQ(label.sizePolicy().verticalPolicy(), QSizePolicy::Fixed);

    label.setFullText(QStringLiteral("short"));
    const QSize shortHint = label.sizeHint();
    const QSize shortMinimum = label.minimumSizeHint();
    label.setFullText(longText());
    EXPECT_EQ(label.sizeHint(), shortHint) << "a longer text asks for neither more height nor more width";
    EXPECT_EQ(label.minimumSizeHint(), shortMinimum);
    label.setFullText(QString());
    EXPECT_EQ(label.sizeHint(), shortHint);
    EXPECT_EQ(an::ui::AnElidedLabel(0).lineCount(), 1) << "a label holds at least one line";
}

TEST(WidgetsComponentsAnElidedLabel, WhatLiesBelowTheLabelStaysWhereItIsWhenTheTextGrows)
{
    LabelOverContent shown(3);
    shown.label->setFullText(QStringLiteral("short"));
    shown.settle();
    const QRect before = shown.content->geometry();
    const QSize window = shown.window.size();

    shown.label->setFullText(longText());
    shown.settle();

    EXPECT_TRUE(shown.label->isElided());
    EXPECT_EQ(shown.content->geometry(), before);
    EXPECT_EQ(shown.window.size(), window);
}

TEST(WidgetsComponentsAnElidedLabel, ResizingFitsTheTextAgain)
{
    LabelOverContent shown(2);
    const QString text =
        QStringLiteral("A sentence long enough to need more than two lines of a narrow label to show it");
    const int textWidth = shown.label->fontMetrics().horizontalAdvance(text);
    shown.label->setFullText(text);
    shown.window.resize(textWidth / 4, 400);
    shown.settle();
    ASSERT_LE(shown.label->width(), textWidth / 4);
    EXPECT_TRUE(shown.label->isElided());
    EXPECT_EQ(shown.label->toolTip(), text);

    shown.window.resize(textWidth + 40, 400);
    shown.settle();
    ASSERT_GE(shown.label->width(), textWidth + 20);
    EXPECT_FALSE(shown.label->isElided());
    EXPECT_EQ(shown.label->text(), text);
    EXPECT_TRUE(shown.label->toolTip().isEmpty());
}

TEST(WidgetsComponentsAnElidedLabel, ALargerFontFitsTheTextAgainAndTakesTheHeightOfItsLines)
{
    LabelOverContent shown(1);
    const QString text = QStringLiteral("Two words");
    shown.label->setFullText(text);
    shown.window.resize(shown.label->fontMetrics().horizontalAdvance(text) + 8, 400);
    shown.settle();
    ASSERT_FALSE(shown.label->isElided());
    const int height = shown.label->height();

    QFont larger = shown.label->font();
    larger.setPointSizeF(larger.pointSizeF() * 3.0);
    shown.label->setFont(larger);
    shown.settle();

    EXPECT_TRUE(shown.label->isElided()) << "the text no longer fits the label's width";
    EXPECT_GE(shown.label->height(), QFontMetrics(larger).lineSpacing());
    EXPECT_GT(shown.label->height(), height);
}

// Elided in the middle, a name keeps its start and its end - a file's stem and its frame number, a filter and its
// channel - and is given whole in the tooltip; at the width its whole text needs, measured exactly, it is shown whole
// and has none. The ellipsis stands where the cut is.
TEST(AnElidedLabel, MiddleElisionKeepsBothEnds)
{
    an::ui::AnElidedLabel label(1);
    label.setAttribute(Qt::WA_DontShowOnScreen, true);
    label.show();
    EXPECT_EQ(label.elideMode(), Qt::ElideRight);
    label.setElideMode(Qt::ElideMiddle);
    EXPECT_EQ(label.elideMode(), Qt::ElideMiddle);
    const QString name = QStringLiteral("stacked_3008x3004_Red_Caldwell_77");
    label.setFullText(name);
    const int whole = static_cast<int>(std::ceil(QFontMetricsF(label.font()).horizontalAdvance(name)));

    label.resize(whole / 2, label.sizeHint().height());
    ASSERT_TRUE(label.isElided());
    const QString shown = label.text();
    EXPECT_TRUE(shown.contains(QChar(0x2026))) << shown.toStdString();
    EXPECT_TRUE(shown.startsWith(QStringLiteral("stac"))) << shown.toStdString();
    EXPECT_TRUE(shown.endsWith(QStringLiteral("_77"))) << shown.toStdString();
    EXPECT_FALSE(shown.contains(QLatin1Char('\n')));
    EXPECT_EQ(label.toolTip(), name);

    label.resize(whole + label.contentsMargins().left() + label.contentsMargins().right(), label.height());
    EXPECT_FALSE(label.isElided());
    EXPECT_EQ(label.text(), name);
    EXPECT_TRUE(label.toolTip().isEmpty());
}

// A label of several lines elided in the middle wraps its first lines as it does elided at the right and cuts only its
// last line in the middle, so the end of the text is the end of what it shows; going back to the right keeps the start
// of the rest instead.
TEST(AnElidedLabel, MiddleElisionCutsOnlyTheLastLine)
{
    an::ui::AnElidedLabel label(2);
    label.setAttribute(Qt::WA_DontShowOnScreen, true);
    label.show();
    label.setElideMode(Qt::ElideMiddle);
    const QString text = longText() + QStringLiteral(" the_end");
    label.setFullText(text);
    label.resize(label.fontMetrics().horizontalAdvance(QStringLiteral("word10 word11 word12")),
                 label.sizeHint().height());
    ASSERT_TRUE(label.isElided());
    const QStringList lines = shownLines(label);
    ASSERT_EQ(lines.size(), 2);
    EXPECT_TRUE(lines.front().startsWith(QStringLiteral("word0"))) << lines.front().toStdString();
    EXPECT_FALSE(lines.front().contains(QChar(0x2026)));
    EXPECT_TRUE(lines.back().contains(QChar(0x2026)));
    EXPECT_TRUE(lines.back().endsWith(QStringLiteral("the_end"))) << lines.back().toStdString();
    EXPECT_EQ(label.toolTip(), text);

    label.setElideMode(Qt::ElideRight);
    EXPECT_TRUE(shownLines(label).back().endsWith(QChar(0x2026))) << shownLines(label).back().toStdString();
}
