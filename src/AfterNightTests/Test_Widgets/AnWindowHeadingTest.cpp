// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A window's heading: an optional eyebrow in capitals, the title, an optional description - each a label the style
// draws in every look - which leaves out the lines it has no text for and grows with a description that wraps.

#include "RenderTestSupport.h"

#include "app/WindowPlacement.h"
#include "components/AnWindowHeading.h"
#include "theme/ComponentParts.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace
{

using an::tests::widgets::forEachLook;
using an::tests::widgets::logicalPixels;
using an::tests::widgets::showOffScreen;
using an::ui::AnWindowHeading;
using an::ui::DesignTokens;

/// Lets a layout settle: a line's change asks its heading to lay out again, and the heading what holds it.
void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::processEvents();
    }
}

/// A heading in a holder as wide as a window's content, shown off the screen.
struct Held
{
    explicit Held(const int width = 480)
    {
        auto* const layout = new QVBoxLayout(&holder);
        layout->setContentsMargins(0, 0, 0, 0);
        heading = new AnWindowHeading(&holder);
        layout->addWidget(heading);
        layout->addStretch(1);
        holder.resize(width, 400);
        showOffScreen(holder);
    }

    QWidget holder;
    AnWindowHeading* heading = nullptr;
};

} // namespace

// The three lines are drawn as the look says: the eyebrow in semibold capitals at the caption's size in the secondary
// text, the title in semibold at the heading's size in the primary text, the description at the small text's size in
// the secondary text - in dark, light and a tinted preset, at both densities.
TEST(AnWindowHeading, ItsLinesAreDrawnAsTheLookSays)
{
    Held held;
    held.heading->setEyebrow(QStringLiteral("Analysis · M31_Lum"));
    held.heading->setTitle(QStringLiteral("Image Statistics"));
    held.heading->setDescription(QStringLiteral("The statistics of each channel."));
    forEachLook([&held](const DesignTokens& t) {
        QLabel* const eyebrow = held.heading->eyebrowLabel();
        QLabel* const title = held.heading->titleLabel();
        QLabel* const description = held.heading->descriptionLabel();

        EXPECT_EQ(logicalPixels(eyebrow->font()), t.metrics.fontCaption);
        EXPECT_EQ(eyebrow->font().capitalization(), QFont::AllUppercase);
        EXPECT_EQ(eyebrow->font().weight(), QFont::DemiBold);
        EXPECT_EQ(eyebrow->palette().color(QPalette::WindowText), t.colors.textSecondary);

        EXPECT_EQ(logicalPixels(title->font()), an::ui::parts::kWindowHeadingTitleFont);
        EXPECT_EQ(title->font().weight(), QFont::DemiBold);
        EXPECT_EQ(title->palette().color(QPalette::WindowText), t.colors.textPrimary);

        EXPECT_EQ(logicalPixels(description->font()), t.metrics.fontSmall);
        EXPECT_EQ(description->palette().color(QPalette::WindowText), t.colors.textSecondary);
    });
}

// A heading with a title alone shows neither of the other lines and is as high as its title; the eyebrow and the
// description are shown when they are given a text, the eyebrow over the title and the description under it, and are
// gone again when their text is.
TEST(AnWindowHeading, ALineWithNoTextIsLeftOut)
{
    forEachLook([](const DesignTokens&) {
        Held held;
        held.heading->setTitle(QStringLiteral("Plate Solve"));
        settle();
        EXPECT_FALSE(held.heading->eyebrowLabel()->isVisibleTo(&held.holder));
        EXPECT_FALSE(held.heading->descriptionLabel()->isVisibleTo(&held.holder));
        const int alone = held.heading->height();
        EXPECT_EQ(alone, held.heading->titleLabel()->height());

        held.heading->setEyebrow(QStringLiteral("Astrometry"));
        held.heading->setDescription(QStringLiteral("Hints improve matching."));
        settle();
        ASSERT_TRUE(held.heading->eyebrowLabel()->isVisibleTo(&held.holder));
        ASSERT_TRUE(held.heading->descriptionLabel()->isVisibleTo(&held.holder));
        EXPECT_LT(held.heading->eyebrowLabel()->geometry().bottom(), held.heading->titleLabel()->geometry().top());
        EXPECT_LT(held.heading->titleLabel()->geometry().bottom(), held.heading->descriptionLabel()->geometry().top());
        EXPECT_EQ(held.heading->titleLabel()->geometry().top() - held.heading->eyebrowLabel()->geometry().bottom() - 1,
                  an::ui::parts::kWindowHeadingLineGap);
        EXPECT_GT(held.heading->height(), alone);

        held.heading->setEyebrow(QString());
        held.heading->setDescription(QString());
        settle();
        EXPECT_EQ(held.heading->height(), alone);
    });
}

// A description longer than the heading is wide wraps, and the heading is as high as its lines then: nothing of it is
// cut.
TEST(AnWindowHeading, ADescriptionThatWrapsMakesTheHeadingHigher)
{
    forEachLook([](const DesignTokens&) {
        Held held(260);
        held.heading->setTitle(QStringLiteral("Annotate Image"));
        held.heading->setDescription(QStringLiteral("In one line."));
        settle();
        const int oneLine = held.heading->height();

        held.heading->setDescription(QStringLiteral(
            "Draws the objects of the catalogues, the grid and the scale over the solved image, and exports the "
            "picture at the size chosen."));
        settle();
        QLabel* const description = held.heading->descriptionLabel();
        EXPECT_GT(held.heading->height(), oneLine);
        EXPECT_GE(description->height(), description->heightForWidth(description->width()));
        EXPECT_LE(description->geometry().bottom(), held.heading->rect().bottom());
    });
}

// What the heading says is what its lines say, and assistive technology reads the title as the heading's name.
TEST(AnWindowHeading, SaysWhatItWasGiven)
{
    Held held;
    held.heading->setEyebrow(QStringLiteral("Image Integration"));
    held.heading->setTitle(QStringLiteral("Stacking Settings"));
    held.heading->setDescription(QStringLiteral("How the frames are combined."));
    EXPECT_EQ(held.heading->eyebrow(), QStringLiteral("Image Integration"));
    EXPECT_EQ(held.heading->title(), QStringLiteral("Stacking Settings"));
    EXPECT_EQ(held.heading->description(), QStringLiteral("How the frames are combined."));
    EXPECT_EQ(held.heading->titleLabel()->text(), QStringLiteral("Stacking Settings"));
    EXPECT_EQ(held.heading->objectName(), QStringLiteral("anWindowHeading"));
    EXPECT_EQ(an::tests::widgets::accessibleName(*held.heading), QStringLiteral("Stacking Settings"));
}

// Under the Observatory profile a window whose layout is a column gains a heading at its head, which says the title,
// the description and the eyebrow it is given, and whose eyebrow is set again as the image it names is renamed. Under
// the Classic profile the window is left as it is.
TEST(WindowHeading, AWindowIsHeadedUnderTheObservatoryProfileAlone)
{
    EXPECT_EQ(an::ui::windowEyebrow(QStringLiteral("Analysis"), QStringLiteral("M31_Lum")),
              QStringLiteral("Analysis · M31_Lum"));
    EXPECT_EQ(an::ui::windowEyebrow(QStringLiteral("Astrometry")), QStringLiteral("Astrometry"));
    {
        const an::ui::testing::ObservatoryStyleScope observatory(an::ui::testing::observatoryLooks()[0]);
        ASSERT_TRUE(an::ui::windowsTakeHeadings());
        QDialog window;
        auto* const column = new QVBoxLayout(&window);
        auto* const content = new QLabel(QStringLiteral("What the window holds"), &window);
        column->addWidget(content);
        AnWindowHeading* const heading =
            an::ui::headWindow(window, QStringLiteral("Find Object"), QStringLiteral("Search the catalogs."),
                               an::ui::windowEyebrow(QStringLiteral("Astrometry")));
        ASSERT_NE(heading, nullptr);
        EXPECT_EQ(column->indexOf(heading), 0) << "the heading is not at the window's head";
        EXPECT_EQ(column->indexOf(content), 1);
        EXPECT_EQ(heading->title(), QStringLiteral("Find Object"));
        EXPECT_EQ(heading->description(), QStringLiteral("Search the catalogs."));
        EXPECT_EQ(heading->eyebrow(), QStringLiteral("Astrometry"));
        an::ui::setWindowEyebrow(window, QStringLiteral("Astrometry · M83"));
        EXPECT_EQ(heading->eyebrow(), QStringLiteral("Astrometry · M83"));
    }
    const an::ui::testing::ObservatoryStyleScope classic(an::ui::testing::observatoryLooks()[0],
                                                         an::ui::StyleProfile::Classic);
    EXPECT_FALSE(an::ui::windowsTakeHeadings());
    QDialog window;
    auto* const column = new QVBoxLayout(&window);
    column->addWidget(new QLabel(QStringLiteral("What the window holds"), &window));
    EXPECT_EQ(an::ui::headWindow(window, QStringLiteral("Find Object"), QString()), nullptr);
    EXPECT_EQ(column->count(), 1);
    an::ui::setWindowEyebrow(window, QStringLiteral("Astrometry"));
}

// A window's own header - a title over a description, in labels the style knows by their names - gives way to a
// heading that says what they said, in the header's place; the header is shown no more. Under the Classic profile the
// header stays.
TEST(WindowHeading, AHeaderGivesWayToAHeadingInItsPlace)
{
    const auto headed = [](QDialog& window) {
        auto* const column = new QVBoxLayout(&window);
        column->addWidget(new QLabel(QStringLiteral("Above"), &window));
        auto* const header = new QWidget(&window);
        auto* const lines = new QVBoxLayout(header);
        auto* const title = new QLabel(QStringLiteral("Image Statistics"), header);
        title->setObjectName(QStringLiteral("anProcessShellHeaderTitle"));
        lines->addWidget(title);
        auto* const description = new QLabel(QStringLiteral("The statistics of each channel."), header);
        description->setObjectName(QStringLiteral("anProcessShellHeaderDescription"));
        lines->addWidget(description);
        column->addWidget(header);
        column->addWidget(new QLabel(QStringLiteral("Below"), &window));
        return header;
    };
    {
        const an::ui::testing::ObservatoryStyleScope observatory(an::ui::testing::observatoryLooks()[0]);
        QDialog window;
        QWidget* const header = headed(window);
        AnWindowHeading* const heading = an::ui::headWindowInPlaceOf(header, QStringLiteral("Analysis · M31"));
        ASSERT_NE(heading, nullptr);
        auto* const column = qobject_cast<QVBoxLayout*>(window.layout());
        EXPECT_EQ(column->indexOf(heading), 1) << "the heading is not where the header was";
        EXPECT_EQ(column->indexOf(header), -1);
        EXPECT_TRUE(header->isHidden());
        EXPECT_EQ(heading->title(), QStringLiteral("Image Statistics"));
        EXPECT_EQ(heading->description(), QStringLiteral("The statistics of each channel."));
        EXPECT_EQ(heading->eyebrow(), QStringLiteral("Analysis · M31"));
    }
    const an::ui::testing::ObservatoryStyleScope classic(an::ui::testing::observatoryLooks()[0],
                                                         an::ui::StyleProfile::Classic);
    QDialog window;
    QWidget* const header = headed(window);
    EXPECT_EQ(an::ui::headWindowInPlaceOf(header), nullptr);
    EXPECT_FALSE(header->isHidden());
    EXPECT_EQ(window.layout()->indexOf(header), 1);
}

// The part of a window's column between two of its widgets scrolls: its items - a widget, a row of widgets, the room
// between them - move as they are, in their order, into an area that takes their place and the column's room; what is
// outside stays where it was. Under the Classic profile, and for a part the column does not hold, nothing moves.
TEST(WindowBody, ThePartBetweenTwoWidgetsScrolls)
{
    struct Built
    {
        QVBoxLayout* column = nullptr;
        QLabel* heading = nullptr;
        QLabel* first = nullptr;
        QPushButton* inARow = nullptr;
        QLabel* last = nullptr;
        QPushButton* footer = nullptr;
    };
    const auto build = [](QDialog& window) {
        Built built;
        built.column = new QVBoxLayout(&window);
        built.heading = new QLabel(QStringLiteral("Heading"), &window);
        built.column->addWidget(built.heading);
        built.first = new QLabel(QStringLiteral("First"), &window);
        built.column->addWidget(built.first);
        auto* const row = new QHBoxLayout();
        built.inARow = new QPushButton(QStringLiteral("In a row"), &window);
        row->addWidget(built.inARow);
        row->addStretch(1);
        built.column->addLayout(row);
        built.column->addStretch(1);
        built.last = new QLabel(QStringLiteral("Last"), &window);
        built.column->addWidget(built.last);
        built.footer = new QPushButton(QStringLiteral("Close"), &window);
        built.column->addWidget(built.footer);
        return built;
    };
    {
        const an::ui::testing::ObservatoryStyleScope observatory(an::ui::testing::observatoryLooks()[0]);
        QDialog window;
        const Built built = build(window);
        QScrollArea* const body = an::ui::scrollWindowBody(*built.column, built.heading, built.footer);
        ASSERT_NE(body, nullptr);
        EXPECT_EQ(body->objectName(), QString::fromLatin1(an::ui::kWindowBodyName));
        EXPECT_EQ(built.column->count(), 3) << "the heading, the body and the footer";
        EXPECT_EQ(built.column->indexOf(built.heading), 0);
        EXPECT_EQ(built.column->indexOf(body), 1);
        EXPECT_EQ(built.column->indexOf(built.footer), 2);
        EXPECT_EQ(built.column->stretch(1), 1) << "the body does not take the column's room";
        ASSERT_NE(body->widget(), nullptr);
        const auto* const moved = qobject_cast<QVBoxLayout*>(body->widget()->layout());
        ASSERT_NE(moved, nullptr);
        ASSERT_EQ(moved->count(), 4) << "a label, a row, the room between and a label";
        EXPECT_EQ(moved->itemAt(0)->widget(), built.first);
        EXPECT_NE(moved->itemAt(1)->layout(), nullptr);
        EXPECT_NE(moved->itemAt(2)->spacerItem(), nullptr);
        EXPECT_EQ(moved->itemAt(3)->widget(), built.last);
        EXPECT_TRUE(body->widget()->isAncestorOf(built.first));
        EXPECT_TRUE(body->widget()->isAncestorOf(built.inARow)) << "a widget of a row moved did not go with its row";
        EXPECT_FALSE(body->widget()->isAncestorOf(built.footer));
        EXPECT_EQ(body->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
        EXPECT_EQ(body->frameShape(), QFrame::NoFrame);

        // A part the column does not hold.
        QDialog other;
        const Built others = build(other);
        EXPECT_EQ(an::ui::scrollWindowBody(*others.column, built.heading, others.footer), nullptr);
        EXPECT_EQ(an::ui::scrollWindowBody(*others.column, 4, 9), nullptr);
        EXPECT_EQ(others.column->count(), 6);
    }
    const an::ui::testing::ObservatoryStyleScope classic(an::ui::testing::observatoryLooks()[0],
                                                         an::ui::StyleProfile::Classic);
    QDialog window;
    const Built built = build(window);
    EXPECT_EQ(an::ui::scrollWindowBody(*built.column, built.heading, built.footer), nullptr);
    EXPECT_EQ(built.column->count(), 6);
}
