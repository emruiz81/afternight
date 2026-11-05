// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The thirty-nine channel and conversion actions over a sequence: every frame goes through the same row of the table
/// a still image does, into new sequences published whole beside the view, under their actions' names, while the
/// view is held for the run and a progress window offers to stop it - and a stop, a busy view, a container that cannot
/// hold the result and a failure each end the run with nothing left behind, the failure said.

#include "Test_ProcessesChannels/ChannelsTestSupport.h"

#include "controls/ImageViewCompanions.h"
#include "controls/ProcessCompanions.h"
#include "controls/ProgressDialogControl.h"
#include "session/ExecutionScope.h"

#include <QApplication>
#include <QDir>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using process_utils::ChannelAction;
using process_utils::ChannelActionKind;

/// The frames a sequence of an action is written from: two colour scenes and the first again, or, for a conversion
/// whose target is float, the sixteen-bit scene three times.
[[nodiscard]] std::vector<std::string_view> framesFor(const ChannelAction& action)
{
    if (action.kind == ChannelActionKind::Convert && action.target != an::SampleType::UInt16)
    {
        return {"scene-rgb-uint16", "scene-rgb-uint16", "scene-rgb-uint16"};
    }
    return {"scene-rgb-float32", "scene-rgb-float32-second", "scene-rgb-float32"};
}

/// Every frame of the sequence a view shows, read in order as assets of the session.
[[nodiscard]] std::vector<an::consumer::ImageAsset> frameAssets(const an::consumer::EngineSession& session,
                                                                const ImageViewControl& view)
{
    std::vector<an::consumer::ImageAsset> frames;
    const std::optional<an::ui::DocumentSequence> sequence = view.document().sequence();
    EXPECT_TRUE(sequence.has_value());
    if (!sequence.has_value())
    {
        return frames;
    }
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    EXPECT_TRUE(scope.hasValue());
    an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> reader =
        an::ui::openProcessFrames(session, *sequence, false);
    EXPECT_TRUE(reader.hasValue()) << reader.status().message();
    if (!scope.hasValue() || !reader.hasValue())
    {
        return frames;
    }
    for (;;)
    {
        an::Result<std::optional<an::ui::ProcessSequenceFrame>> frame =
            (*reader)->next(scope->engine(), scope->execution());
        if (!frame.hasValue() || !frame->has_value())
        {
            break;
        }
        frames.push_back(std::move((**frame).asset));
    }
    scope->finish(an::Status::success());
    return frames;
}

/// The progress windows open now.
[[nodiscard]] std::vector<ProgressDialogControl*> openProgressWindows()
{
    std::vector<ProgressDialogControl*> windows;
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        if (auto* progress = dynamic_cast<ProgressDialogControl*>(widget); progress != nullptr && progress->isVisible())
        {
            windows.push_back(progress);
        }
    }
    return windows;
}

/// The sequences left in the context's temporary directory: manifests, SER files and frames directories.
[[nodiscard]] QStringList temporarySequences(const ProcessTestContext& context)
{
    return QDir(context.getTempDirPath()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
}

class ChannelSequenceTest : public ::testing::TestWithParam<std::size_t>
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(4u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
        ASSERT_TRUE(m_directory.isValid());
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] const ChannelAction& action() const
    {
        return process_utils::channelActions()[GetParam()];
    }

    [[nodiscard]] ProcessTestContext& context()
    {
        return *m_context;
    }

    [[nodiscard]] const QTemporaryDir& directory() const
    {
        return m_directory;
    }

private:
    shared::RegistrySnapshot m_snapshot;
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    QTemporaryDir m_directory;
};

std::string actionName(const ::testing::TestParamInfo<std::size_t>& info)
{
    std::string name = process_utils::channelActions()[info.param].action;
    return name.substr(std::string("action").size());
}

} // namespace

// sequence: every frame of an image list goes through the row a still image goes through, into one new image list per
// image the row makes, each opened beside the view under its action's name and history; the view is held for the
// run, shows a progress window while it lasts, and is let go when it lands.
TEST_P(ChannelSequenceTest, EveryFrameGoesThroughTheRowAStillImageGoesThrough)
{
    const QString path = writeSceneSequence(context().engineSession(), directory(), framesFor(action()));
    ASSERT_FALSE(path.isEmpty());
    ImageViewControl* source = openSequenceView(context(), path, an::SequenceFormat::ImageList);
    ASSERT_NE(source, nullptr);
    const std::vector<an::consumer::ImageAsset> frames = frameAssets(context().engineSession(), *source);
    ASSERT_EQ(frames.size(), 3u);
    // A new sequence is shown at the frame its source showed, so the source shows its second rather than its first.
    ASSERT_NE(source->sequencePlayer(), nullptr);
    source->sequencePlayer()->setCurrentFrame(1u, {});
    ASSERT_TRUE(pumpUntil([&]() { return source->isWorkIdle() && source->document().sequence()->currentFrame == 1u; }));

    const std::size_t count = process_utils::channelActionImageCount(action());
    const std::size_t before = context().madeViews.size();
    runRegisteredAction(action().action);
    EXPECT_EQ(openProgressWindows().size(), 1u) << "a sequence run shows its progress";
    EXPECT_FALSE(source->tryRequestLockForProcessing()) << "the view is held for the run";
    const std::vector<ImageViewControl*> views = awaitNewViews(context(), before, count, 0);
    ASSERT_EQ(views.size(), count) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_TRUE(context().messages.isEmpty()) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_TRUE(openProgressWindows().empty());
    EXPECT_TRUE(source->tryRequestLockForProcessing()) << "the view is let go when the run lands";
    source->releaseLockForProcessing();

    std::vector<std::vector<fixtures::FixtureSamples>> expected;
    for (const an::consumer::ImageAsset& frame : frames)
    {
        expected.push_back(expectedOutputs(context().engineSession(), frame, action()));
        ASSERT_EQ(expected.back().size(), count);
    }
    for (std::size_t index = 0u; index < count; ++index)
    {
        SCOPED_TRACE(index);
        ImageViewControl* view = views[index];
        ASSERT_TRUE(view->isSequence());
        const std::string suffix =
            action().kind == ChannelActionKind::Extract ? action().outputs[index].suffix : action().suffix;
        EXPECT_EQ(view->getUniqueName(), "frames" + suffix);
        EXPECT_EQ(madeNeighbourOf(context(), view), source);
        const std::string history = madeHistoryOf(context(), view);
        EXPECT_NE(history.find(" from sequence 'frames'"), std::string::npos) << history;
        EXPECT_NE(view->document().names().sourcePath, path);
        EXPECT_TRUE(QFile::exists(view->document().names().sourcePath));
        EXPECT_EQ(view->document().sequence()->currentFrame, 1u) << "a new sequence shows the frame its source showed";
        const std::optional<fixtures::FixtureSamples> shown = documentSamples(view->document());
        ASSERT_TRUE(shown.has_value());
        EXPECT_EQ(largestDifference(*shown, expected[1][index]), 0.0) << "the view shows that frame's result";
        const std::vector<fixtures::FixtureSamples> written = sequenceFrames(context().engineSession(), *view);
        ASSERT_EQ(written.size(), 3u);
        for (std::size_t frame = 0u; frame < written.size(); ++frame)
        {
            EXPECT_EQ(largestDifference(written[frame], expected[frame][index]), 0.0) << "frame " << frame;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(ThirtyNineActions, ChannelSequenceTest,
                         ::testing::Range<std::size_t>(0u, process_utils::channelActions().size()), actionName);

// error: a sequence conversion to a representation its container cannot hold is refused by name, before anything is
// read, rather than written into a container that would store it converted back; one it can hold is written.
TEST(ChannelSequenceRunTest, AConversionASerSequenceCannotHoldIsRefusedByName)
{
    for (const ChannelAction& action : process_utils::channelActions())
    {
        if (action.kind != ChannelActionKind::Convert)
        {
            continue;
        }
        SCOPED_TRACE(action.action);
        const shared::RegistrySnapshot snapshot;
        std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
        ASSERT_NE(context, nullptr);
        const InstalledContext installed(*context);
        QTemporaryDir directory;
        ASSERT_TRUE(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("frames.ser"));
        an::tests::viewer::writeSerFile(
            context->engineSession(), path, 6u, 4u, 2u,
            [](std::uint32_t f, std::uint32_t x, std::uint32_t y) { return static_cast<float>(f + x + y) / 16.0f; });
        ImageViewControl* source = openSequenceView(*context, path, an::SequenceFormat::Ser);
        ASSERT_NE(source, nullptr);
        const bool holds = action.target == an::SampleType::UInt8 || action.target == an::SampleType::UInt16;
        const std::size_t before = context->madeViews.size();
        runRegisteredAction(action.action);
        if (holds)
        {
            const std::vector<ImageViewControl*> views = awaitNewViews(*context, before, 1u, 0);
            ASSERT_EQ(views.size(), 1u) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
            const std::vector<fixtures::FixtureSamples> written =
                sequenceFrames(context->engineSession(), *views.front());
            ASSERT_EQ(written.size(), 2u);
            EXPECT_EQ(written.front().sampleType(), action.target == an::SampleType::UInt8
                                                        ? fixtures::FixtureSampleType::UInt8
                                                        : fixtures::FixtureSampleType::UInt16);
            continue;
        }
        ASSERT_EQ(context->messages.size(), 1);
        EXPECT_EQ(context->messages.back(), QStringLiteral("error:Convert sequence format"));
        EXPECT_TRUE(context->errorTexts.back().contains(QStringLiteral("a SER sequence cannot hold")))
            << context->errorTexts.back().toStdString();
        EXPECT_TRUE(openProgressWindows().empty());
        EXPECT_TRUE(settleAll(*context));
        EXPECT_EQ(context->madeViews.size(), before);
        EXPECT_TRUE(source->tryRequestLockForProcessing());
        source->releaseLockForProcessing();
    }
}

// cancel: the progress window's Cancel stops the run; nothing is opened, nothing is said, nothing is left in the
// temporary directory, and the view is let go.
TEST(ChannelSequenceRunTest, CancelStopsTheRunAndLeavesNothing)
{
    const shared::RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext(1u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = writeSceneSequence(context->engineSession(), directory,
                                            {"scene-rgb-float32", "scene-rgb-float32-second", "scene-rgb-float32"});
    ImageViewControl* source = openSequenceView(*context, path, an::SequenceFormat::ImageList);
    ASSERT_NE(source, nullptr);
    const QStringList leftBefore = temporarySequences(*context);
    {
        // The session admits one operation at a time; the run waits for this one, and is stopped while it waits.
        std::optional<an::consumer::OperationScope> held = an::tests::viewer::admitted(context->engineSession());
        ASSERT_TRUE(held.has_value());
        runRegisteredAction("actionExtract_RGB_Channels");
        const std::vector<ProgressDialogControl*> windows = openProgressWindows();
        ASSERT_EQ(windows.size(), 1u);
        windows.front()->requestCancel();
    }
    EXPECT_TRUE(settleAll(*context));
    EXPECT_TRUE(context->messages.isEmpty()) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(context->madeViews.size(), 1u) << "only the source's view";
    EXPECT_EQ(temporarySequences(*context), leftBefore);
    EXPECT_TRUE(openProgressWindows().empty());
    EXPECT_TRUE(source->tryRequestLockForProcessing());
    source->releaseLockForProcessing();
}

// A view another run holds says so and runs nothing.
TEST(ChannelSequenceRunTest, ABusyViewSaysSoAndRunsNothing)
{
    const shared::RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = writeSceneSequence(context->engineSession(), directory, {"scene-rgb-float32"});
    ImageViewControl* source = openSequenceView(*context, path, an::SequenceFormat::ImageList);
    ASSERT_NE(source, nullptr);
    ASSERT_TRUE(source->tryRequestLockForProcessing());
    runRegisteredAction("actionExtract_Red_Channel");
    runRegisteredAction("actionConvert_to_8_bit");
    EXPECT_TRUE(settleAll(*context));
    EXPECT_EQ(context->messages,
              (QStringList{QStringLiteral("warning:Extract Channel"), QStringLiteral("warning:Convert Format")}));
    EXPECT_EQ(context->madeViews.size(), 1u);
    EXPECT_TRUE(openProgressWindows().empty());
    source->releaseLockForProcessing();
}

// A run that cannot open what it made says so, and the sequences it wrote are removed; with no temporary files lent it
// says so and runs nothing.
TEST(ChannelSequenceRunTest, AFailureIsSaidAndLeavesNoSequenceBehind)
{
    const shared::RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path =
        writeSceneSequence(context->engineSession(), directory, {"scene-rgb-float32", "scene-rgb-float32-second"});
    ImageViewControl* source = openSequenceView(*context, path, an::SequenceFormat::ImageList);
    ASSERT_NE(source, nullptr);
    const QStringList leftBefore = temporarySequences(*context);

    context->refuseViews = true;
    runRegisteredAction("actionExtract_HSV");
    ASSERT_TRUE(awaitMessage(*context, 0));
    EXPECT_EQ(context->messages.back(), QStringLiteral("error:Extract sequence channel"));
    EXPECT_TRUE(context->errorTexts.back().startsWith(QStringLiteral("Could not extract HSV Hue, HSV Saturation and "
                                                                     "HSV Value from the sequence 'frames'")))
        << context->errorTexts.back().toStdString();
    EXPECT_EQ(temporarySequences(*context), leftBefore) << "the sequences no view shows are removed";
    EXPECT_EQ(context->madeViews.size(), 1u);
    context->refuseViews = false;

    context->lendTemporaryFiles = false;
    runRegisteredAction("actionConvert_to_8_bit");
    ASSERT_TRUE(awaitMessage(*context, 1));
    EXPECT_EQ(context->messages.back(), QStringLiteral("error:Convert sequence format"));
    EXPECT_TRUE(context->errorTexts.back().contains(QStringLiteral("temporary files")))
        << context->errorTexts.back().toStdString();
    EXPECT_TRUE(openProgressWindows().empty());
    EXPECT_TRUE(source->tryRequestLockForProcessing());
    source->releaseLockForProcessing();
}

// A mosaic sequence's frames are demosaiced before a channel is extracted, whatever the view shows; a conversion keeps
// them the mosaic they are.
TEST(ChannelSequenceRunTest, AnExtractionReadsAMosaicDemosaicedAndAConversionKeepsIt)
{
    const shared::RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("mosaic.ser"));
    const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(rggb.has_value());
    an::tests::viewer::writeSerFile(
        context->engineSession(), path, 8u, 6u, 2u,
        [](std::uint32_t f, std::uint32_t x, std::uint32_t y) { return static_cast<float>(f * 8u + x + y) / 32.0f; },
        *rggb);
    ImageViewControl* source = openSequenceView(*context, path, an::SequenceFormat::Ser, "mosaic");
    ASSERT_NE(source, nullptr);

    // A grey mosaic has no green to extract until it is demosaiced.
    std::size_t before = context->madeViews.size();
    runRegisteredAction("actionExtract_Green_Channel");
    std::vector<ImageViewControl*> views = awaitNewViews(*context, before, 1u, 0);
    ASSERT_EQ(views.size(), 1u) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
    std::vector<fixtures::FixtureSamples> written = sequenceFrames(context->engineSession(), *views.front());
    ASSERT_EQ(written.size(), 2u);
    EXPECT_EQ(written.front().channelCount(), 1u);

    before = context->madeViews.size();
    runRegisteredAction("actionConvert_to_8_bit");
    views = awaitNewViews(*context, before, 1u, 0);
    ASSERT_EQ(views.size(), 1u) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
    written = sequenceFrames(context->engineSession(), *views.front());
    ASSERT_EQ(written.size(), 2u);
    EXPECT_EQ(written.front().channelCount(), 1u) << "the mosaic stays a mosaic";
    EXPECT_EQ(written.front().sampleType(), fixtures::FixtureSampleType::UInt8);
}
