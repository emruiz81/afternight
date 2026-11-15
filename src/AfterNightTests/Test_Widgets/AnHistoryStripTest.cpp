// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The history strip: a history's steps as chips with the one shown marked, the inherited ones quieter, the pending chip
// of a task in its two forms, the oldest steps giving way to "..." when they do not fit, a click on a step asking for
// it, and Undo, Redo and Image History as the history has them.

#include "RenderTestSupport.h"

#include "components/AnHistoryStrip.h"
#include "controls/ViewChrome.h"
#include "theme/ComponentParts.h"

#include <QAbstractButton>
#include <QAction>
#include <QSignalSpy>

#include <string>
#include <vector>

namespace
{

using an::tests::widgets::forEachLook;
using an::tests::widgets::recordedText;
using an::tests::widgets::showOffScreen;
using an::ui::AnHistoryStrip;
using an::ui::DesignTokens;
using an::ui::HistoryPendingStep;
using an::ui::ViewHistorySteps;

/// A history of named steps, the one shown at `current`.
[[nodiscard]] ViewHistorySteps historyOf(const std::vector<std::string>& names, const std::size_t current)
{
    ViewHistorySteps steps;
    for (const std::string& name : names)
    {
        steps.steps.push_back({.name = name, .description = name + " described"});
    }
    steps.current = current;
    return steps;
}

[[nodiscard]] ViewHistorySteps fiveSteps()
{
    return historyOf({"Image read from a file", "Background Extraction", "Color Calibration", "Curves Transformation",
                      "Noise Reduction"},
                     4u);
}

} // namespace

// Every step of a history of five is a chip, in order, with the caps label before them.
TEST(HistoryStrip, ChipsForAHistoryOfFive)
{
    AnHistoryStrip strip;
    strip.setSteps(fiveSteps());
    strip.resize(strip.sizeHint());
    showOffScreen(strip);
    EXPECT_EQ(strip.shownChips(),
              (QStringList{QStringLiteral("Image read from a file"), QStringLiteral("Background Extraction"),
                           QStringLiteral("Color Calibration"), QStringLiteral("Curves Transformation"),
                           QStringLiteral("Noise Reduction")}));
    EXPECT_EQ(strip.overflowChip(), nullptr);
    EXPECT_EQ(strip.pendingChip(), nullptr);
    forEachLook([](const DesignTokens& t) {
        AnHistoryStrip strip;
        strip.setSteps(fiveSteps());
        strip.resize(strip.sizeHint());
        EXPECT_EQ(strip.sizeHint().height(), t.metrics.strip);
        showOffScreen(strip);
        const an::ui::testing::RecordedText label = recordedText(strip, QStringLiteral("History"));
        EXPECT_EQ(label.colour, t.colors.textTertiary) << an::tests::widgets::describedTexts(strip).toStdString();
        QAbstractButton* chip = strip.chipForStep(1u);
        ASSERT_NE(chip, nullptr);
        EXPECT_EQ(chip->height(), t.metrics.chip);
        EXPECT_EQ(chip->toolTip(), QStringLiteral("Background Extraction described"));
        const an::ui::testing::RecordedText name = recordedText(*chip, QStringLiteral("Background"));
        EXPECT_EQ(name.colour, t.colors.textPrimary);
        EXPECT_EQ(name.font.weight(), QFont::DemiBold);
    });
}

// The step shown is the accent's container with a check; the others are outlined. An inherited step is drawn in the
// tertiary text and asks for nothing.
TEST(HistoryStrip, TheCurrentStep)
{
    forEachLook([](const DesignTokens& t) {
        AnHistoryStrip strip;
        ViewHistorySteps steps = fiveSteps();
        steps.steps.insert(steps.steps.begin(), {.name = "Inherited Crop", .inherited = true});
        steps.current = 3u;
        strip.setSteps(steps);
        strip.resize(strip.sizeHint());
        showOffScreen(strip);
        QAbstractButton* current = strip.chipForStep(3u);
        ASSERT_NE(current, nullptr);
        const an::ui::testing::RenderedPicture picture =
            an::ui::testing::RenderedPicture::ofWidget(*current, 1.0, t.layers.surface1);
        an::tests::widgets::expectColour(picture.at(current->width() / 2.0, 2.0),
                                         an::ui::compositeOver(t.colors.accentContainer, t.layers.surface1),
                                         "the current step's fill");
        EXPECT_EQ(recordedText(*current, QStringLiteral("Color")).colour, t.colors.textOnAccentContainer);
        QAbstractButton* other = strip.chipForStep(4u);
        ASSERT_NE(other, nullptr);
        const an::ui::testing::RenderedPicture plain =
            an::ui::testing::RenderedPicture::ofWidget(*other, 1.0, t.layers.surface1);
        an::tests::widgets::expectColour(plain.at(other->width() / 2.0, other->height() / 2.0), t.layers.surface1,
                                         "an outlined step's inside");
        QAbstractButton* inherited = strip.chipForStep(0u);
        ASSERT_NE(inherited, nullptr);
        EXPECT_EQ(recordedText(*inherited, QStringLiteral("Inherited")).colour, t.colors.textTertiary);

        QSignalSpy asked(&strip, &AnHistoryStrip::stepClicked);
        inherited->click();
        current->click();
        EXPECT_EQ(asked.count(), 0) << "neither an inherited step nor the one shown is asked for";
    });
}

// A task open on the image adds a dashed chip after the current step: the process, "pending", and the tool tip that
// says why; while a run writes the image, "writing".
TEST(HistoryStrip, ThePendingChipInBothForms)
{
    forEachLook([](const DesignTokens& t) {
        AnHistoryStrip strip;
        strip.setSteps(historyOf({"Image read from a file", "Curves Transformation"}, 1u));
        strip.setPendingStep(HistoryPendingStep{.processName = QStringLiteral("Non Local Bayes")});
        strip.resize(strip.sizeHint());
        showOffScreen(strip);
        EXPECT_EQ(strip.shownChips(),
                  (QStringList{QStringLiteral("Image read from a file"), QStringLiteral("Curves Transformation"),
                               QStringLiteral("Non Local Bayes")}));
        QAbstractButton* pending = strip.pendingChip();
        ASSERT_NE(pending, nullptr);
        EXPECT_GT(pending->geometry().left(), strip.chipForStep(1u)->geometry().right()) << "after the current step";
        EXPECT_EQ(pending->toolTip(), QStringLiteral("The open task: not in the history until Apply"));
        EXPECT_EQ(recordedText(*pending, QStringLiteral("Non Local")).colour, t.colors.textPrimary);
        EXPECT_EQ(recordedText(*pending, QStringLiteral("pending")).colour, t.colors.textSecondary)
            << an::tests::widgets::describedTexts(*pending).toStdString() << " in a chip " << pending->width()
            << " wide";

        strip.setPendingStep(HistoryPendingStep{.processName = QStringLiteral("Non Local Bayes"), .writing = true});
        EXPECT_EQ(pending->toolTip(), QStringLiteral("The run is writing this step"));
        EXPECT_FALSE(recordedText(*pending, QStringLiteral("writing")).text.isEmpty());

        strip.setPendingStep(std::nullopt);
        EXPECT_EQ(strip.pendingChip(), nullptr);
        EXPECT_EQ(strip.shownChips().size(), 2);
    });
}

// When the steps do not fit, the oldest give way to a "..." chip, which opens the Image History window; the current
// step and the pending chip stay.
TEST(HistoryStrip, OverflowGivesWayToTheOldest)
{
    QAction undo(QStringLiteral("Undo"));
    QAction redo(QStringLiteral("Redo"));
    QAction history(QStringLiteral("Image History"));
    AnHistoryStrip strip;
    strip.setActions(&undo, &redo, &history);
    strip.setSteps(fiveSteps());
    strip.setPendingStep(HistoryPendingStep{.processName = QStringLiteral("Star Removal")});
    // Room for about half the steps: the strip as wide as every chip needs, cut to 55 %.
    const QSize full = strip.sizeHint();
    strip.resize(full.width() * 11 / 20, full.height());
    showOffScreen(strip);
    const QStringList shown = strip.shownChips();
    ASSERT_GE(shown.size(), 3);
    EXPECT_EQ(shown.front(), QStringLiteral("..."));
    EXPECT_EQ(shown.back(), QStringLiteral("Star Removal"));
    EXPECT_TRUE(shown.contains(QStringLiteral("Noise Reduction"))) << "the current step stays";
    EXPECT_FALSE(shown.contains(QStringLiteral("Image read from a file"))) << "the oldest gave way";
    EXPECT_EQ(strip.chipForStep(0u), nullptr);
    QAbstractButton* overflow = strip.overflowChip();
    ASSERT_NE(overflow, nullptr);
    EXPECT_LE(strip.pendingChip()->geometry().right(), strip.undoButton()->geometry().left())
        << "the chips end before the buttons";

    QSignalSpy opened(&history, &QAction::triggered);
    overflow->click();
    EXPECT_EQ(opened.count(), 1) << "the \"...\" chip opens the Image History window";

    strip.resize(full);
    EXPECT_EQ(strip.overflowChip(), nullptr)
        << "wide again, every step fits: " << strip.shownChips().join(QStringLiteral(", ")).toStdString() << " in "
        << strip.width();
    EXPECT_EQ(strip.shownChips().size(), 6);
}

// A click on one of the view's own steps other than the one shown asks for it, by its index in the steps.
TEST(HistoryStrip, AClickJumps)
{
    AnHistoryStrip strip;
    ViewHistorySteps steps = fiveSteps();
    steps.steps.insert(steps.steps.begin(), {.name = "Inherited Crop", .inherited = true});
    steps.current = 5u;
    strip.setSteps(steps);
    strip.resize(strip.sizeHint());
    showOffScreen(strip);
    QSignalSpy asked(&strip, &AnHistoryStrip::stepClicked);
    QAbstractButton* chip = strip.chipForStep(2u);
    ASSERT_NE(chip, nullptr);
    EXPECT_EQ(chip->text(), QStringLiteral("Background Extraction"));
    chip->click();
    ASSERT_EQ(asked.count(), 1);
    EXPECT_EQ(asked.takeFirst().at(0).toInt(), 2);
}

// Undo, Redo and Image History are the view's: enabled and shown as the history has them, and a click triggers them.
TEST(HistoryStrip, UndoAndRedoEnabledAsTheHistorySays)
{
    QAction undo(QStringLiteral("Undo <Curves Transformation>"));
    QAction redo(QStringLiteral("Redo"));
    QAction history(QStringLiteral("Image History"));
    history.setToolTip(QStringLiteral("Image History (Ctrl+Shift+Z)"));
    redo.setEnabled(false);
    AnHistoryStrip strip;
    strip.setActions(&undo, &redo, &history);
    strip.setSteps(historyOf({"Image read from a file", "Curves Transformation"}, 1u));
    strip.resize(strip.sizeHint());
    showOffScreen(strip);

    EXPECT_TRUE(strip.undoButton()->isEnabled());
    EXPECT_FALSE(strip.redoButton()->isEnabled());
    EXPECT_EQ(strip.historyButton()->toolTip(), QStringLiteral("Image History (Ctrl+Shift+Z)"));
    EXPECT_EQ(an::tests::widgets::accessibleName(*strip.undoButton()), QStringLiteral("Undo <Curves Transformation>"));

    // The history undid its last step: Undo has nothing left, Redo has the step.
    undo.setEnabled(false);
    redo.setEnabled(true);
    EXPECT_FALSE(strip.undoButton()->isEnabled());
    EXPECT_TRUE(strip.redoButton()->isEnabled());
    QSignalSpy redone(&redo, &QAction::triggered);
    strip.redoButton()->click();
    EXPECT_EQ(redone.count(), 1);

    EXPECT_LT(strip.undoButton()->geometry().left(), strip.redoButton()->geometry().left());
    EXPECT_LT(strip.redoButton()->geometry().left(), strip.historyButton()->geometry().left());
    EXPECT_EQ(strip.historyButton()->geometry().right(), strip.width() - 1 - an::ui::parts::kHistoryStripPaddingEnd);

    strip.setActions(nullptr, nullptr, nullptr);
    EXPECT_FALSE(strip.undoButton()->isVisibleTo(&strip)) << "with no history, no buttons";
}
