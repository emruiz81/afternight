// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A process applied over a sequence recorded in a SER file: every frame read, processed and written, and the sequence
/// the run wrote shown whole; a frame that cannot be read, a frame the container refuses, an output that cannot be
/// published and a Cancel, each of which publishes nothing and leaves nothing behind; and a run that makes a new
/// sequence view.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"
#include "Test_ControlsViewer/EditHoldingTheWriter.h"

#include "controls/SequenceContainers.h"

#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <QDir>
#include <QFileInfo>

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <type_traits>
#include <variant>

namespace
{

using namespace an::tests::processes;

/// A SER keeps a sample to a 65,535th.
constexpr double kSerStep = 1.0 / 65535.0;

/// The first sample of an image, an integer sample read as a fraction of its representation's largest value; NaN when
/// it cannot be read.
[[nodiscard]] double firstFraction(const an::consumer::AnyImage& image)
{
    return std::visit(
        []<typename ImageT>(const ImageT& typed) -> double {
            const auto view = typed.view();
            const auto* sample = view.has_value() ? view->sampleAt(0u, 0u, 0u, 0u) : nullptr;
            if (sample == nullptr)
            {
                return std::numeric_limits<double>::quiet_NaN();
            }
            using SampleT = std::remove_cvref_t<decltype(*sample)>;
            if constexpr (std::is_floating_point_v<SampleT>)
            {
                return static_cast<double>(*sample);
            }
            else
            {
                return static_cast<double>(*sample) / static_cast<double>((std::numeric_limits<SampleT>::max)());
            }
        },
        image);
}

/// The first sample of what a document shows, as `firstFraction` reads it.
[[nodiscard]] double shownFraction(const an::ui::ImageDocument& document)
{
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    return lease.hasValue() ? firstFraction(lease->image()) : std::numeric_limits<double>::quiet_NaN();
}

/// The first sample of every frame of a SER file, read through the sequence containers; fails the case when the file
/// cannot be read.
[[nodiscard]] std::vector<double> firstFractionsOf(const an::consumer::EngineSession& session, const QString& path)
{
    std::vector<double> fractions;
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    EXPECT_TRUE(scope.has_value());
    if (!scope.has_value())
    {
        return fractions;
    }
    an::Result<an::ui::OpenedSequence> opened =
        an::ui::openSequenceSource(scope->engine(), scope->execution(), path, an::SequenceFormat::Ser);
    EXPECT_TRUE(opened.hasValue()) << opened.status().message();
    if (!opened.hasValue())
    {
        return fractions;
    }
    for (;;)
    {
        an::Result<std::optional<std::pair<std::uint64_t, an::consumer::AnyImage>>> read =
            an::ui::readSequenceFrame(scope->engine(), scope->execution(), opened->source, std::nullopt);
        EXPECT_TRUE(read.hasValue()) << read.status().message();
        if (!read.hasValue() || !read->has_value())
        {
            return fractions;
        }
        fractions.push_back(firstFraction((*read)->second));
    }
}

/// What a run left in the context's temporary directory, by name, leaving out the directory the view's history writes
/// into there.
[[nodiscard]] QStringList leftBehind(const ProcessTestContext& context)
{
    QStringList names =
        QDir(context.getTempDirPath()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    names.erase(std::remove_if(names.begin(), names.end(),
                               [](const QString& name) { return name.startsWith(QStringLiteral("AN_HISTORY_")); }),
                names.end());
    return names;
}

/// A recording of three grey 8 by 6 frames whose samples are 0.1, 0.2 and 0.3, written into a directory of the case's
/// own, and a sequence view of it showing the second frame, its history begun as the application begins it.
class SequenceTarget
{
public:
    SequenceTarget(const an::consumer::EngineSession& session,
                   std::optional<std::vector<std::uint64_t>> selection = std::nullopt)
    {
        EXPECT_TRUE(m_recordings.isValid());
        m_path = m_recordings.path() + QStringLiteral("/frames.ser");
        an::tests::viewer::writeSerFile(
            session, m_path, 8u, 6u, 3u,
            [](std::uint32_t frame, std::uint32_t, std::uint32_t) { return 0.1f * static_cast<float>(frame + 1u); });
        std::optional<an::ui::ImageDocument> shown =
            greyDocument(session, 8u, 6u, [](std::uint32_t, std::uint32_t) { return 0.2f; });
        EXPECT_TRUE(shown.has_value());
        an::ui::DocumentSequence sequence;
        sequence.sourcePath = m_path;
        sequence.descriptor.format = an::SequenceFormat::Ser;
        sequence.descriptor.frameCount = 3u;
        sequence.currentFrame = 1u;
        sequence.selectedFrames = std::move(selection);
        shown->setSequence(sequence);
        m_view.reset(new ImageViewControl());
        m_view->initialize("sequence", ViewControlMode::standard);
        m_view->resize(640, 480);
        m_view->setSequenceDocument(std::move(*shown), false);
        m_view->initializeNewImageHistory("Sequence opened");
    }

    [[nodiscard]] ImageViewControl& view() const
    {
        return *m_view;
    }

    [[nodiscard]] const QString& path() const
    {
        return m_path;
    }

private:
    QTemporaryDir m_recordings;
    QString m_path;
    ViewHandle m_view;
};

} // namespace

TEST(SequenceRunTest, EveryFrameIsProcessedAndTheSequenceWrittenIsShownWhole)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    SequenceTarget recording(context->engineSession());
    ImageViewControl& target = recording.view();
    ASSERT_TRUE(target.isSequence());
    ASSERT_NE(target.sequencePlayer(), nullptr);
    ProcessHandle process = openProcess(target, TestProcessOptions{});

    // The player holds still while the run reads the recording's frames.
    process->gate.close();
    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));
    EXPECT_FALSE(target.sequencePlayer()->widget()->isEnabled());
    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(target));

    // The view shows the sequence written, at the frame it showed before, processed.
    const std::optional<an::ui::DocumentSequence> shown = target.document().sequence();
    ASSERT_TRUE(shown.has_value());
    EXPECT_NE(shown->sourcePath, recording.path());
    EXPECT_TRUE(shown->sourcePath.startsWith(context->getTempDirPath()));
    EXPECT_EQ(shown->currentFrame, 1u);
    EXPECT_NEAR(shownFraction(target.document()), 0.4, kSerStep);
    const std::vector<double> written = firstFractionsOf(context->engineSession(), shown->sourcePath);
    ASSERT_EQ(written.size(), 3u);
    EXPECT_NEAR(written[0], 0.2, 2.0 * kSerStep);
    EXPECT_NEAR(written[1], 0.4, 2.0 * kSerStep);
    EXPECT_NEAR(written[2], 0.6, 2.0 * kSerStep);
    EXPECT_EQ(leftBehind(*context), QStringList{QFileInfo(shown->sourcePath).fileName()});

    ASSERT_NE(target.viewHistory(), nullptr);
    EXPECT_EQ(target.viewHistory()->previousStepName(), "Test Scale");
    const std::vector<TestRun> runs = process->runs();
    ASSERT_EQ(runs.size(), 3u);
    EXPECT_EQ(runs[2].kind, an::ui::ProcessRunKind::SequenceFrame);
    EXPECT_EQ(runs[2].frame, 2u);
    EXPECT_EQ(process->runTelemetry().runsCommitted, 1u);
    EXPECT_FALSE(target.isLockedForProcessing());
    EXPECT_TRUE(target.sequencePlayer()->widget()->isEnabled()) << "the player answers again";
}

// A run over a sequence reads its mask, another view's image, under a lease taken before it asks to be admitted: an
// edit of the mask's view holds that writer and then asks to be admitted, so at a width of one a run admitted first
// would hold the only thread while it waited for the writer, and the edit would wait for the thread.
TEST(SequenceRunTest, ASequenceRunWaitingForItsMasksWriterHoldsNoThreadTheMasksEditNeeds)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(1u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    SequenceTarget recording(context->engineSession());
    ImageViewControl& target = recording.view();
    std::optional<an::ui::ImageDocument> maskDocument =
        greyDocument(context->engineSession(), 8u, 6u, [](std::uint32_t, std::uint32_t) { return 1.0f; });
    ASSERT_TRUE(maskDocument.has_value());
    ViewHandle mask = makeTargetView("mask", std::move(*maskDocument));
    target.setCurrentMask(mask.get());
    target.setEnableMask(true);
    ASSERT_TRUE(target.isMasked());
    ASSERT_TRUE(settleView(target));
    ProcessHandle process = openProcess(target, TestProcessOptions{});
    ASSERT_TRUE(settleViewAndSnapshots(*mask, context->engineSession()));

    an::tests::viewer::EditHoldingTheWriter edit(*mask);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    const std::uint64_t admittedBefore = context->engineSession().admission().admitted;
    process->applyProcess();
    EXPECT_FALSE(an::tests::viewer::admittedBeyond(context->engineSession(), admittedBefore))
        << "the run was admitted while it could not read its mask";
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the mask's edit waited for the thread the run held while the run waited for the mask's writer";

    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(target));
    EXPECT_EQ(process->runTelemetry().runsCommitted, 1u);
    EXPECT_NEAR(shownFraction(target.document()), 0.4, kSerStep);
    ASSERT_TRUE(settleView(*mask));
    target.removeCurrentMask();
}

// The selection names a frame the recording does not hold, so the second read fails after the first selected frame
// was processed.
TEST(SequenceRunTest, AFrameThatCannotBeReadPublishesNothingAndLeavesNothingBehind)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    SequenceTarget recording(context->engineSession(), std::vector<std::uint64_t>{1u, 4u});
    ImageViewControl& target = recording.view();
    const std::uint64_t identity = target.document().identity();
    ProcessHandle process = openProcess(target, TestProcessOptions{});

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(target));

    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::SequenceFrame), 1);
    EXPECT_TRUE(leftBehind(*context).isEmpty()) << leftBehind(*context).join(QStringLiteral(", ")).toStdString();
    EXPECT_EQ(target.document().identity(), identity);
    EXPECT_EQ(target.document().sequence()->sourcePath, recording.path());
    EXPECT_NEAR(shownFraction(target.document()), 0.2, kSerStep);
    EXPECT_EQ(process->runTelemetry().runsFailed, 1u);
    EXPECT_EQ(context->errorCount(), 1);
    EXPECT_FALSE(target.isLockedForProcessing());
}

// The third frame comes out of the body half the size of the first two, and the SER container, whose frames share one
// size, refuses it.
TEST(SequenceRunTest, AFrameThatCannotBeWrittenPublishesNothing)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    SequenceTarget recording(context->engineSession());
    ImageViewControl& target = recording.view();
    ProcessHandle process = openProcess(target, TestProcessOptions{});
    process->setParams(TestParams{.body = TestBody::ShrinkFromThirdFrame});

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(target));
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::SequenceFrame), 3);
    EXPECT_TRUE(leftBehind(*context).isEmpty()) << leftBehind(*context).join(QStringLiteral(", ")).toStdString();
    EXPECT_EQ(target.document().sequence()->sourcePath, recording.path());
    EXPECT_EQ(process->runTelemetry().runsFailed, 1u);
    EXPECT_EQ(context->errorCount(), 1);
}

// A directory stands where the run's output is to be published, so every frame is written and the publication fails.
TEST(SequenceRunTest, AnOutputThatCannotBePublishedPublishesNothing)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    SequenceTarget recording(context->engineSession());
    ImageViewControl& target = recording.view();
    ProcessHandle process = openProcess(target, TestProcessOptions{});
    const QString blocked = context->nextTempFilePathForNewSequence(an::SequenceFormat::Ser);
    ASSERT_TRUE(QDir().mkpath(blocked));

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(target));
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::SequenceFrame), 3);
    EXPECT_EQ(leftBehind(*context), QStringList{QFileInfo(blocked).fileName()}) << "only the directory remains";
    EXPECT_TRUE(QFileInfo(blocked).isDir());
    EXPECT_EQ(target.document().sequence()->sourcePath, recording.path());
    EXPECT_EQ(process->runTelemetry().runsFailed, 1u);
}

TEST(SequenceRunTest, CancelStopsTheRunAndPublishesNothing)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    SequenceTarget recording(context->engineSession());
    ImageViewControl& target = recording.view();
    ProcessHandle process = openProcess(target, TestProcessOptions{.progressDialog = true});
    process->setParams(TestParams{.body = TestBody::WaitForStop});

    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process]() { return process->runsOf(an::ui::ProcessRunKind::SequenceFrame) == 1; }));
    ASSERT_NE(process->lastProgress, nullptr);
    process->lastProgress->requestCancel();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(target));

    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::SequenceFrame), 1);
    EXPECT_TRUE(leftBehind(*context).isEmpty()) << leftBehind(*context).join(QStringLiteral(", ")).toStdString();
    EXPECT_EQ(target.document().sequence()->sourcePath, recording.path());
    EXPECT_EQ(process->runTelemetry().runsCancelled, 1u);
    EXPECT_EQ(context->errorCount(), 0);
}

TEST(SequenceRunTest, ARunToANewSequenceMakesASequenceView)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    SequenceTarget recording(context->engineSession());
    ImageViewControl& target = recording.view();
    ProcessHandle process = openProcess(target, TestProcessOptions{.createNewImage = true});

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_EQ(context->madeViews.size(), 1u);
    ImageViewControl* made = context->madeViews.front();
    ASSERT_TRUE(made->isSequence());
    const QString written = made->document().sequence()->sourcePath;
    EXPECT_TRUE(written.startsWith(context->getTempDirPath()));
    EXPECT_EQ(firstFractionsOf(context->engineSession(), written).size(), 3u);
    // The target still shows the sequence it was opened on.
    EXPECT_EQ(target.document().sequence()->sourcePath, recording.path());
}

TEST(SequenceRunTest, ASequenceRunWithNoTemporaryFileServiceFailsBeforeReadingAnyFrame)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    context->lendTemporaryFiles = false;
    SequenceTarget recording(context->engineSession());
    ImageViewControl& target = recording.view();
    ProcessHandle process = openProcess(target, TestProcessOptions{});

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_TRUE(process->runs().empty());
    EXPECT_EQ(process->runTelemetry().runsFailed, 1u);
    EXPECT_FALSE(target.isLockedForProcessing());
}
