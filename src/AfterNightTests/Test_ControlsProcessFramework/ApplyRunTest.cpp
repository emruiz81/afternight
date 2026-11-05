// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Applying a process over the framework: the view's edit, recorded in the history before it is shown; an Apply
/// clicked while a preview runs; Cancel, a failing body and a body that writes nothing; the modulation and the mask;
/// a result made a new image, and a snapshot of the preview; the view's processing lock; and a window closed while an
/// Apply runs.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"
#include "Test_ControlsViewer/EditHoldingTheWriter.h"

#include <gtest/gtest.h>

namespace
{

using namespace an::tests::processes;

/// A grey 8 by 6 Float32 target whose samples are `value`.
[[nodiscard]] ViewHandle flatTarget(const an::consumer::EngineSession& session, float value,
                                    const std::string& name = "target")
{
    std::optional<an::ui::ImageDocument> document =
        greyDocument(session, 8u, 6u, [value](std::uint32_t, std::uint32_t) { return value; });
    EXPECT_TRUE(document.has_value());
    return makeTargetView(name, std::move(*document));
}

} // namespace

TEST(ApplyRunTest, ApplyEditsTheTargetAndRecordsTheStepBeforeShowingIt)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    const std::uint64_t identity = target->document().identity();
    const std::uint64_t asset = target->document().revision().asset.assetIdentity;

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));

    EXPECT_NEAR(documentSample(target->document(), 3u, 2u), 0.5, 1e-6);
    // The document is the same; its image is the edit's result, a new asset.
    EXPECT_EQ(target->document().identity(), identity);
    EXPECT_NE(target->document().revision().asset.assetIdentity, asset);
    an::ui::IViewHistory* history = target->viewHistory();
    ASSERT_NE(history, nullptr);
    EXPECT_EQ(history->previousStepName(), "Test Scale");
    EXPECT_EQ(process->runTelemetry().runsCommitted, 1u);
    EXPECT_EQ(process->completed, 1);
    EXPECT_FALSE(target->isLockedForProcessing());

    // The step kept the state the Apply replaced.
    ASSERT_TRUE(history->undo());
    ASSERT_TRUE(pumpUntil([history]() { return !history->hasUndo(); }));
    ASSERT_TRUE(settleView(*target));
    EXPECT_NEAR(documentSample(target->document(), 3u, 2u), 0.25, 1e-6);
}

TEST(ApplyRunTest, AnApplyTheHistoryRefusesIsNeverShown)
{
    // The history cannot keep the result's copy, so it refuses the step before the view shows it.
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u, an::tests::viewer::historyCannotKeepEdits());
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));
    ASSERT_TRUE(pumpUntil([&context]() { return context->messages.contains(QStringLiteral("error:Image History")); }))
        << "the history says why the step was not kept";
    ASSERT_FALSE(context->errorTexts.isEmpty());
    EXPECT_TRUE(context->errorTexts.back().contains(QStringLiteral("its snapshot cannot be kept")))
        << context->errorTexts.back().toStdString();
    EXPECT_NEAR(documentSample(target->document(), 3u, 2u), 0.25, 1e-6);
    EXPECT_FALSE(target->viewHistory()->hasUndo());
    EXPECT_EQ(process->runTelemetry().runsCommitted, 0u);
    EXPECT_EQ(process->runTelemetry().runsDiscarded, 1u);
    EXPECT_EQ(process->completed, 0);
    EXPECT_FALSE(target->isLockedForProcessing());
}

TEST(ApplyRunTest, AnApplyClickedWhileAPreviewRunsStopsThePreviewAndApplies)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->gate.close();
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));

    // The click is not lost to the run in progress.
    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process]() { return process->runsOf(an::ui::ProcessRunKind::Apply) == 1; }));
    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));

    EXPECT_NEAR(documentSample(target->document(), 0u, 0u), 0.5, 1e-6);
    const ProcessRunTelemetry telemetry = process->runTelemetry();
    EXPECT_EQ(telemetry.runsCommitted, 1u);
    EXPECT_GE(telemetry.previewsDiscarded, 1u);
}

TEST(ApplyRunTest, CancelStopsTheApplyAndPublishesNothing)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{.progressDialog = true});
    process->setParams(TestParams{.body = TestBody::WaitForStop});

    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process]() { return process->runsOf(an::ui::ProcessRunKind::Apply) == 1; }));
    ASSERT_NE(process->lastProgress, nullptr);
    process->lastProgress->requestCancel();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));

    EXPECT_NEAR(documentSample(target->document(), 0u, 0u), 0.25, 1e-6);
    EXPECT_FALSE(target->viewHistory()->hasUndo()) << "nothing was recorded";
    const ProcessRunTelemetry telemetry = process->runTelemetry();
    EXPECT_EQ(telemetry.runsCancelled, 1u);
    EXPECT_EQ(telemetry.runsCommitted, 0u);
    EXPECT_EQ(context->errorCount(), 0);
    EXPECT_FALSE(target->isLockedForProcessing());
}

TEST(ApplyRunTest, AFailingBodyPublishesNothingAndSaysWhy)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->setParams(TestParams{.body = TestBody::Fail});

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));

    EXPECT_NEAR(documentSample(target->document(), 0u, 0u), 0.25, 1e-6);
    EXPECT_EQ(process->runTelemetry().runsFailed, 1u);
    ASSERT_EQ(context->errorCount(), 1);
    EXPECT_TRUE(context->errorTexts.back().contains(QStringLiteral("the test body failed on purpose")));
    EXPECT_FALSE(target->isLockedForProcessing());
}

TEST(ApplyRunTest, ABodyThatWritesNothingPublishesTheSourceUnchanged)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = greyDocument(
        context->engineSession(), 8u, 6u, [](std::uint32_t x, std::uint32_t y) { return 0.01f * (x + 10.0f * y); });
    ViewHandle target = makeTargetView("target", std::move(*document));
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->setParams(TestParams{.body = TestBody::WriteNothing});

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));

    // The destination starts as a copy of the source: nothing of an allocation nobody wrote is published.
    for (std::uint32_t y = 0u; y < 6u; ++y)
    {
        for (std::uint32_t x = 0u; x < 8u; ++x)
        {
            EXPECT_NEAR(documentSample(target->document(), x, y), 0.01 * (x + 10.0 * y), 1e-6) << x << "," << y;
        }
    }
    EXPECT_EQ(process->runTelemetry().runsCommitted, 1u);
}

TEST(ApplyRunTest, TheModulationMovesTheTargetTowardTheResultByItsWeight)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->setParams(TestParams{.factor = 2.0, .modulation = 0.5});

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));
    EXPECT_NEAR(documentSample(target->document(), 0u, 0u), 0.375, 1e-6);
}

TEST(ApplyRunTest, TheTargetsMaskWeighsTheResultAndAnInvertedOneTheSource)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    std::optional<an::ui::ImageDocument> maskDocument = greyDocument(
        context->engineSession(), 8u, 6u, [](std::uint32_t x, std::uint32_t) { return x < 4u ? 0.0f : 1.0f; });
    ASSERT_TRUE(maskDocument.has_value());
    ViewHandle mask = makeTargetView("mask", std::move(*maskDocument));
    target->setCurrentMask(mask.get());
    target->setEnableMask(true);
    ASSERT_TRUE(target->isMasked());
    ProcessHandle process = openProcess(*target, TestProcessOptions{});

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));
    EXPECT_NEAR(documentSample(target->document(), 1u, 1u), 0.25, 1e-6);
    EXPECT_NEAR(documentSample(target->document(), 6u, 1u), 0.5, 1e-6);

    target->setInvertMask(true);
    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));
    EXPECT_NEAR(documentSample(target->document(), 1u, 1u), 0.5, 1e-6);
    EXPECT_NEAR(documentSample(target->document(), 6u, 1u), 0.5, 1e-6);
    target->removeCurrentMask();
}

TEST(ApplyRunTest, ARunOnOneChannelChangesThatChannelAlone)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = makeDocument<float>(
        context->engineSession(), 8u, 6u, Channels::Rgb,
        [](std::uint32_t, std::uint32_t, std::uint32_t c) { return 0.1f * static_cast<float>(c + 1u); });
    ViewHandle target = makeTargetView("colour", std::move(*document));
    ProcessHandle process = openProcess(*target, TestProcessOptions{.perChannel = true});
    process->channelSelector->setCurrentIndex(process->channelSelector->findText(QStringLiteral("Red")));
    EXPECT_EQ(process->selectedChannelScope(), an::ChannelScope::Red);

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));
    EXPECT_NEAR(documentSample(target->document(), 2u, 2u, 0u), 0.2, 1e-6);
    EXPECT_NEAR(documentSample(target->document(), 2u, 2u, 1u), 0.2, 1e-6);
    EXPECT_NEAR(documentSample(target->document(), 2u, 2u, 2u), 0.3, 1e-6);
    ASSERT_EQ(process->runs().size(), 1u);
    EXPECT_EQ(process->runs().front().channel, an::ChannelScope::Red);
}

TEST(ApplyRunTest, ApplyingToANewImageMakesAViewAndLeavesTheTargetAlone)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{.createNewImage = true});

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_EQ(context->madeViews.size(), 1u);
    ImageViewControl* made = context->madeViews.front();
    ASSERT_TRUE(settleView(*made));
    EXPECT_NEAR(documentSample(made->document(), 0u, 0u), 0.5, 1e-6);
    EXPECT_EQ(made->document().names().displayName, QStringLiteral("Test Scale result"));
    EXPECT_EQ(context->madeHistories.front(), "factor 2.000000");
    EXPECT_EQ(context->madeNeighbours.front(), nullptr);
    EXPECT_NEAR(documentSample(target->document(), 0u, 0u), 0.25, 1e-6);
    EXPECT_EQ(process->runTelemetry().runsCommitted, 1u);
    EXPECT_FALSE(target->isLockedForProcessing());
}

TEST(ApplyRunTest, ANewImageOfADocumentTheTargetLetGoIsNotMade)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{.createNewImage = true});
    process->gate.close();

    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));
    std::optional<an::ui::ImageDocument> other =
        greyDocument(context->engineSession(), 8u, 6u, [](std::uint32_t, std::uint32_t) { return 0.1f; });
    target->setDocument(std::move(*other));
    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));

    EXPECT_TRUE(context->madeViews.empty());
    EXPECT_EQ(process->runTelemetry().runsCommitted, 0u);
    EXPECT_EQ(process->runTelemetry().runsDiscarded, 1u);
}

TEST(ApplyRunTest, ASnapshotOfThePreviewIsANewViewBesideTheTarget)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = greyDocument(
        context->engineSession(), 8u, 6u, [](std::uint32_t x, std::uint32_t y) { return 0.01f * (x + 10.0f * y); });
    ViewHandle target = makeTargetView("target", std::move(*document));
    ProcessHandle process =
        openProcess(*target, TestProcessOptions{.preview = true, .previewArea = true, .previewAreaSize = QSize(4, 2)});
    ASSERT_TRUE(settleProcess(*process));

    process->onRTTakeSnapshot(process->previewView());
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_EQ(context->madeViews.size(), 1u);
    ImageViewControl* made = context->madeViews.front();
    EXPECT_EQ(context->madeNeighbours.front(), target.get());
    // The snapshot is what the preview showed: the area's result over the whole image.
    EXPECT_NEAR(documentSample(made->document(), 2u, 2u), 2.0 * 0.22, 1e-6);
    EXPECT_NEAR(documentSample(made->document(), 0u, 0u), 0.0, 1e-6);
    EXPECT_NEAR(documentSample(made->document(), 7u, 5u), 0.57, 1e-6);
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::Snapshot), 1);
}

// A run reads its images under leases taken before it asks to be admitted. An edit of a view holds the view's writer
// and then asks to be admitted, so at a width of one a run admitted first would hold the only thread while it waited
// for that writer, and the edit would wait for the thread: neither would end. A run to a new image waits for the
// target's writer.
TEST(ApplyRunTest, ARunToANewImageWaitingForAnEditsWriterHoldsNoThreadTheEditNeeds)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(1u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{.createNewImage = true});
    ASSERT_TRUE(settleViewAndSnapshots(*target, context->engineSession()));

    an::tests::viewer::EditHoldingTheWriter edit(*target);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    const std::uint64_t admittedBefore = context->engineSession().admission().admitted;
    process->applyProcess();
    EXPECT_FALSE(an::tests::viewer::admittedBeyond(context->engineSession(), admittedBefore))
        << "the run was admitted while it could not read the target";
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the edit waited for the thread the run held while the run waited for the edit's writer";

    ASSERT_TRUE(settleProcess(*process));
    ASSERT_EQ(context->madeViews.size(), 1u);
    ImageViewControl* made = context->madeViews.front();
    ASSERT_TRUE(settleView(*made));
    EXPECT_NEAR(documentSample(made->document(), 0u, 0u), 0.5, 1e-6);
    ASSERT_TRUE(settleView(*target));
}

// A snapshot of the preview waits for the target's writer.
TEST(ApplyRunTest, ASnapshotWaitingForAnEditsWriterHoldsNoThreadTheEditNeeds)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(1u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{.preview = true});
    ASSERT_TRUE(settleProcess(*process));

    an::tests::viewer::EditHoldingTheWriter edit(*target);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    const std::uint64_t admittedBefore = context->engineSession().admission().admitted;
    process->onRTTakeSnapshot(process->previewView());
    EXPECT_FALSE(an::tests::viewer::admittedBeyond(context->engineSession(), admittedBefore))
        << "the snapshot was admitted while it could not read the target";
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the edit waited for the thread the snapshot held while the snapshot waited for the edit's writer";

    ASSERT_TRUE(settleProcess(*process));
    ASSERT_EQ(context->madeViews.size(), 1u);
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::Snapshot), 1);
    ASSERT_TRUE(settleView(*target));
}

// An Apply in place holds the target's writer, as every edit does, and waits for its mask's writer before it asks to
// be admitted: an edit of the mask's view holds that writer and asks for the thread.
TEST(ApplyRunTest, AnApplyWaitingForItsMasksWriterHoldsNoThreadTheMasksEditNeeds)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(1u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    std::optional<an::ui::ImageDocument> maskDocument = greyDocument(
        context->engineSession(), 8u, 6u, [](std::uint32_t x, std::uint32_t) { return x < 4u ? 0.0f : 1.0f; });
    ASSERT_TRUE(maskDocument.has_value());
    ViewHandle mask = makeTargetView("mask", std::move(*maskDocument));
    target->setCurrentMask(mask.get());
    target->setEnableMask(true);
    ASSERT_TRUE(target->isMasked());
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    ASSERT_TRUE(settleView(*target));
    ASSERT_TRUE(settleViewAndSnapshots(*mask, context->engineSession()));

    an::tests::viewer::EditHoldingTheWriter edit(*mask);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    const std::uint64_t admittedBefore = context->engineSession().admission().admitted;
    process->applyProcess();
    EXPECT_FALSE(an::tests::viewer::admittedBeyond(context->engineSession(), admittedBefore))
        << "the Apply was admitted while it could not read its mask";
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the mask's edit waited for the thread the Apply held while the Apply waited for the mask's writer";

    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));
    EXPECT_NEAR(documentSample(target->document(), 1u, 1u), 0.25, 1e-6);
    EXPECT_NEAR(documentSample(target->document(), 6u, 1u), 0.5, 1e-6);
    ASSERT_TRUE(settleView(*mask));
    target->removeCurrentMask();
}

TEST(ApplyRunTest, AnApplyOnAViewAnotherRunHoldsIsRefusedWithAWarning)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    ASSERT_TRUE(target->tryRequestLockForProcessing());

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::Apply), 0);
    EXPECT_NEAR(documentSample(target->document(), 0u, 0u), 0.25, 1e-6);
    target->releaseLockForProcessing();
}

// An Apply whose work throws outside its body's own guard ends as a failure the window says: the window takes its next
// Apply rather than refuse it for the window's life, and the target is left as it was.
TEST(ApplyRunTest, AnApplyWhoseWorkThrowsEndsAsAFailureAndTheNextOneRuns)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->throwWhenReady = true;

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));
    EXPECT_TRUE(process->isRunIdle());
    EXPECT_NEAR(documentSample(target->document(), 0u, 0u), 0.25, 1e-6) << "a run that failed changed the target";
    EXPECT_EQ(process->runTelemetry().runsFailed, 1u);
    ASSERT_EQ(context->errorCount(), 1) << "the failure was not said";
    EXPECT_TRUE(context->errorTexts.back().contains(QStringLiteral("bad"))) << context->errorTexts.back().toStdString();
    EXPECT_FALSE(target->isLockedForProcessing());

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::Apply), 2) << "the window refused its next Apply";
    EXPECT_NEAR(documentSample(target->document(), 0u, 0u), 0.5, 1e-6);
}

TEST(ApplyRunTest, AWindowClosedWhileAnApplyRunsClosesWhenTheApplyLands)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->show();
    process->gate.close();

    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));
    EXPECT_FALSE(process->close());
    EXPECT_TRUE(process->isHidden());
    EXPECT_FALSE(process->isRunIdle());
    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));
    EXPECT_TRUE(process->isRunIdle());
    EXPECT_NEAR(documentSample(target->document(), 0u, 0u), 0.5, 1e-6);
}

TEST(ApplyRunTest, AWindowClosedWhileAnApplyRunsSaysNothingWhenItsTargetClosesBeforeTheApplyLands)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{.createNewImage = true});
    process->show();
    process->gate.close();

    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));
    EXPECT_FALSE(process->close());
    ASSERT_TRUE(process->isHidden());

    // The person closed the window already: its target closing is no news to them, and a box over a window nobody sees
    // would run while the Apply lands and completes the close beneath it.
    target.reset();
    EXPECT_TRUE(std::none_of(context->messages.begin(), context->messages.end(), [](const QString& message) {
        return message.startsWith(QStringLiteral("warning:"));
    })) << context->messages.join(QStringLiteral(", ")).toStdString();

    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_TRUE(process->isRunIdle());
    EXPECT_TRUE(process->isHidden());
    EXPECT_TRUE(context->madeViews.empty());
}

TEST(ApplyRunTest, ClosingTheTargetClosesTheProcessAndRefusesWhatIsOnItsWay)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{.createNewImage = true});
    process->show();
    process->gate.close();

    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));
    target.reset();
    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_TRUE(context->madeViews.empty());
    EXPECT_TRUE(process->isHidden());
}
