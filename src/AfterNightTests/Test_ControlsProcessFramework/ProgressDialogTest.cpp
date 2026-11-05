// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The progress dialog over an execution scope's observers: a run's progress shown as a fraction of its total, its
/// stage as the card's title, one step of several as the whole's progress, Cancel as the stop of the token it lends,
/// and nothing delivered to a dialog that is gone.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "common.h"
#include "components/AnProgressCard.h"
#include "controls/ImageSelectorTooltipBuilder.h"
#include "controls/ProgressDialogControl.h"

#include <QLabel>
#include <QProgressBar>

#include <gtest/gtest.h>

namespace
{

using namespace an::tests::processes;

/// The value of the progress card a dialog names.
[[nodiscard]] int valueOf(ProgressDialogControl& dialog, const char* cardName)
{
    const auto* card = dialog.findChild<an::ui::AnProgressCard*>(QString::fromLatin1(cardName));
    return card != nullptr ? card->progressValue() : -1;
}

} // namespace

TEST(ProgressDialogTest, ARunsProgressIsShownAsAFractionOfItsTotal)
{
    ProgressDialogControl dialog(nullptr, QStringLiteral("Progress"));
    dialog.setProgressBarRange(0, 100);
    const an::ui::ExecutionObserver observer = dialog.observer();
    ASSERT_TRUE(observer.progress);
    EXPECT_EQ(observer.receiver.data(), &dialog);

    observer.progress(an::ui::ExecutionProgress{.completed = 25u, .total = 100u, .stage = "Scaling"});
    EXPECT_EQ(valueOf(dialog, "progressCard"), 25);

    // A report whose total is not known yet moves nothing.
    observer.progress(an::ui::ExecutionProgress{.completed = 7u, .total = 0u});
    EXPECT_EQ(valueOf(dialog, "progressCard"), 25);

    dialog.setValueCoeff(50.0f);
    dialog.setValueOffset(50.0f);
    observer.progress(an::ui::ExecutionProgress{.completed = 1u, .total = 2u});
    EXPECT_EQ(valueOf(dialog, "progressCard"), 75);
}

// A run whose share done is not known is shown as a busy bar with no percentage, not as 0%; the first report that
// states a share ends it, and the dialog shows that share and every one after it.
TEST(ProgressDialogTest, ABusyBarLastsUntilARunStatesItsShareDone)
{
    ProgressDialogControl dialog(nullptr, QStringLiteral("Progress"));
    dialog.setProgressBarRange(0, 100);
    dialog.setBusy(true);
    EXPECT_TRUE(dialog.isBusy());
    const auto* card = dialog.findChild<an::ui::AnProgressCard*>(QStringLiteral("progressCard"));
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->progressBar()->maximum(), 0) << "Qt's busy indicator";
    EXPECT_TRUE(card->percentageLabel()->text().isEmpty()) << "no share is shown as none done";

    const an::ui::ExecutionObserver observer = dialog.observer();
    // A report that states no share leaves it busy, and its stage is still shown.
    observer.progress(an::ui::ExecutionProgress{.completed = 3u, .total = 0u, .stage = "Smoothing"});
    EXPECT_TRUE(dialog.isBusy());
    EXPECT_EQ(card->title(), QStringLiteral("Smoothing"));

    observer.progress(an::ui::ExecutionProgress{.completed = 30u, .total = 100u});
    EXPECT_FALSE(dialog.isBusy());
    EXPECT_EQ(card->progressBar()->maximum(), 100) << "the range the dialog was given";
    EXPECT_EQ(valueOf(dialog, "progressCard"), 30);
    EXPECT_EQ(card->percentageLabel()->text(), QStringLiteral("30%"));
}

TEST(ProgressDialogTest, OneStepOfSeveralIsShownAsTheWholesProgress)
{
    ProgressDialogControl dialog(nullptr, QStringLiteral("Progress"), true);
    EXPECT_TRUE(dialog.isTwoStepProgress());
    const an::ui::ExecutionObserver third = dialog.overallObserver(2, 4);
    third.progress(an::ui::ExecutionProgress{.completed = 1u, .total = 2u, .stage = "Step three"});
    // Two steps done and half of the third: 2.5 of 4.
    EXPECT_EQ(valueOf(dialog, "sequenceProgressCard"), 62);

    const an::ui::ExecutionObserver sequence = dialog.sequenceObserver();
    sequence.progress(an::ui::ExecutionProgress{.completed = 3u, .total = 4u});
    EXPECT_EQ(valueOf(dialog, "sequenceProgressCard"), 75);
}

TEST(ProgressDialogTest, CancelStopsTheTokenTheDialogLends)
{
    auto* dialog = new ProgressDialogControl(nullptr, QStringLiteral("Progress"));
    const std::stop_token token = dialog->stopToken();
    EXPECT_FALSE(token.stop_requested());
    EXPECT_FALSE(dialog->cancelRequested());
    QPushButton* cancel = dialog->findChild<QPushButton*>(QStringLiteral("cancelButton"));
    ASSERT_NE(cancel, nullptr);
    cancel->click();
    EXPECT_TRUE(dialog->cancelRequested());
    EXPECT_TRUE(token.stop_requested());
    EXPECT_FALSE(cancel->isEnabled());
    delete dialog;
}

TEST(ProgressDialogTest, ADialogThatGoesStopsItsRunAndReceivesNothingMore)
{
    auto* dialog = new ProgressDialogControl(nullptr, QStringLiteral("Progress"));
    const std::stop_token token = dialog->stopToken();
    const an::ui::ExecutionObserver observer = dialog->observer();
    dialog->setValueAsync(40);
    dialog->closeAsync();
    delete dialog;
    // A run that outlives its dialog is stopped, and what it still delivers goes nowhere.
    EXPECT_TRUE(token.stop_requested());
    EXPECT_TRUE(observer.receiver.isNull());
    observer.progress(an::ui::ExecutionProgress{.completed = 1u, .total = 2u});
    pumpQuietly();
}

TEST(ImageSelectorTooltipTest, SaysWhatTheViewShowsFromItsDocument)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = makeDocument<std::uint16_t>(
        context->engineSession(), 64u, 32u, Channels::Rgb,
        [](std::uint32_t, std::uint32_t, std::uint32_t) { return static_cast<std::uint16_t>(1u); },
        an::ui::DocumentNames{.displayName = QStringLiteral("m42"), .sourcePath = QStringLiteral("C:/data/m42.fits")});
    ASSERT_TRUE(document.has_value());
    ViewHandle view = makeTargetView("m42.fits", std::move(*document));

    const QString tooltip = an::ui::buildImageSelectorTooltip(view.get());
    EXPECT_TRUE(tooltip.contains(QStringLiteral("<b>m42</b>")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("m42.fits")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("64 × 32 px")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("3 (")));
    EXPECT_TRUE(tooltip.contains(QString::fromStdString(sampleTypeReportLabel(an::SampleType::UInt16))));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("12.0 KB")));

    std::optional<an::ui::ImageDocument> inMemory =
        greyDocument(context->engineSession(), 4u, 4u, [](std::uint32_t, std::uint32_t) { return 0.0f; });
    ViewHandle other = makeTargetView("made", std::move(*inMemory));
    EXPECT_TRUE(an::ui::buildImageSelectorTooltip(other.get()).contains(QStringLiteral("In-memory file")));
    EXPECT_TRUE(an::ui::buildImageSelectorTooltip(nullptr).isEmpty());
}
