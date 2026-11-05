// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The player of a sequence view: a frame read on a worker and shown through the view, only the latest asked for;
/// stepping and playback, which ends at the last frame; the source kept between frames under the engine context's lease
/// and let go when the view closes, a read under way included; another sequence shown in the view; the sequence written
/// through its publication; a frame that cannot be read reported; playback started at the rate a SER's timestamps give;
/// and a mosaic sequence - an X-Trans one among them - demosaiced by the one setting.

#include "Test_ControlsSequences/SequencesTestSupport.h"

#include "app/FileCompanions.h"
#include "controls/SequencePlayerControl.h"
#include "controls/settings/SettingsKeys.h"

#include <QFile>

#include <gtest/gtest.h>

namespace
{

using namespace an::tests::sequences;

/// A sequence view over a SER fixture, made by the context's view factory, with the real player under it.
struct PlayerCase
{
    std::unique_ptr<SequencesTestContext> context;
    std::unique_ptr<InstalledContext> installed;
    FixtureSequence fixture;
    QString path;
    ImageViewControl* view = nullptr;
    SequencePlayerControl* player = nullptr;
};

/// Opens a SER fixture as a sequence view, as the file workflows do; fails the case when it cannot.
[[nodiscard]] PlayerCase openPlayer(const FixtureSequence& fixture, bool autoDebayer = false, std::uint32_t width = 4u)
{
    PlayerCase opened;
    opened.context = openSequencesContext(width);
    EXPECT_NE(opened.context, nullptr);
    if (!opened.context)
    {
        return opened;
    }
    opened.installed = std::make_unique<InstalledContext>(*opened.context);
    opened.fixture = fixture;
    opened.path = opened.context->pathOf(QStringLiteral("capture.ser"));
    EXPECT_TRUE(writeFixture(opened.context->engineSession(), opened.path, an::SequenceFormat::Ser, fixture));
    an::Result<an::ui::ImageDocument> document =
        an::ui::openSequenceDocument(opened.context->engineSession(), opened.path, an::SequenceFormat::Ser, {});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return opened;
    }
    opened.view = opened.context->createNewImageViewControlFromSequence(
        nullptr, std::move(*document), nullptr, autoDebayer, "Sequence loaded from file", ViewControlMode::standard,
        nullptr, false, false);
    EXPECT_NE(opened.view, nullptr);
    if (opened.view != nullptr)
    {
        opened.player = dynamic_cast<SequencePlayerControl*>(opened.view->sequencePlayer());
    }
    EXPECT_NE(opened.player, nullptr);
    return opened;
}

/// Turns the event loop until the player's frames have landed and the view's work with them.
[[nodiscard]] bool settle(const PlayerCase& opened)
{
    const bool landed = pumpUntil([&]() { return opened.player->isIdle() && opened.view->isWorkIdle(); });
    pumpQuietly();
    return landed && opened.player->isIdle() && opened.view->isWorkIdle();
}

/// The samples of what the view shows.
[[nodiscard]] std::vector<double> shownSamples(const ImageViewControl& view)
{
    return samplesOf(*view.document().asset());
}

} // namespace

// A frame asked for is read on a worker and shown through the view: the document holds it and states it as the frame
// shown.
TEST(SequencePlayerTest, AFrameAskedForIsShownThroughTheView)
{
    PlayerCase opened = openPlayer(FixtureSequence{.frames = 4u});
    ASSERT_NE(opened.player, nullptr);
    EXPECT_EQ(opened.player->frameCount(), 4u);
    EXPECT_EQ(opened.player->currentFrame(), 0u);
    EXPECT_EQ(opened.player->widget(), opened.player);

    bool shown = false;
    opened.player->setCurrentFrame(2u, [&shown]() { shown = true; });
    EXPECT_EQ(opened.player->currentFrame(), 2u);
    ASSERT_TRUE(pumpUntil([&shown]() { return shown; }));
    ASSERT_TRUE(settle(opened));
    EXPECT_EQ(opened.player->framesShown(), 1u);
    EXPECT_EQ(opened.view->document().sequence()->currentFrame, 2u);
    EXPECT_EQ(largestDifference(shownSamples(*opened.view), fixtureSamples(2u, opened.fixture, false)), 0.0);
}

// Playback starts at the rate a SER's timestamps give - one frame per mean interval between them - and at one frame a
// second for a SER whose trailer names no instant. A SER's header has no field for a rate and its source states none,
// so every SER started at one frame a second.
TEST(SequencePlayerTest, PlaybackStartsAtTheRateTheSerStates)
{
    {
        PlayerCase timed = openPlayer(FixtureSequence{.frames = 4u, .trailerInterval = 1000000});
        ASSERT_NE(timed.player, nullptr);
        const std::optional<an::SequenceRate> rate = timed.view->document().sequence()->descriptor.nominalFrameRate;
        ASSERT_TRUE(rate.has_value()) << "the sequence states the rate its timestamps give";
        EXPECT_EQ(rate->numerator, 10u);
        EXPECT_EQ(rate->denominator, 1u);
        EXPECT_DOUBLE_EQ(timed.player->framesPerSecond(), 10.0);
    }

    PlayerCase untimed = openPlayer(FixtureSequence{.frames = 4u});
    ASSERT_NE(untimed.player, nullptr);
    EXPECT_FALSE(untimed.view->document().sequence()->descriptor.nominalFrameRate.has_value());
    EXPECT_DOUBLE_EQ(untimed.player->framesPerSecond(), 1.0);
}

// Next and Previous step one frame, and stop at the ends.
TEST(SequencePlayerTest, NextAndPreviousStepOneFrameAndStopAtTheEnds)
{
    PlayerCase opened = openPlayer(FixtureSequence{.frames = 3u});
    ASSERT_NE(opened.player, nullptr);
    opened.player->showPreviousFrame();
    EXPECT_EQ(opened.player->currentFrame(), 0u);
    opened.player->showNextFrame();
    opened.player->showNextFrame();
    EXPECT_EQ(opened.player->currentFrame(), 2u);
    opened.player->showNextFrame();
    EXPECT_EQ(opened.player->currentFrame(), 2u);
    ASSERT_TRUE(settle(opened));
    EXPECT_EQ(largestDifference(shownSamples(*opened.view), fixtureSamples(2u, opened.fixture, false)), 0.0);
    opened.player->showPreviousFrame();
    ASSERT_TRUE(settle(opened));
    EXPECT_EQ(opened.view->document().sequence()->currentFrame, 1u);
    EXPECT_EQ(largestDifference(shownSamples(*opened.view), fixtureSamples(1u, opened.fixture, false)), 0.0);
}

// Only the frame last asked for is shown: a frame superseded on its way is dropped, and the view ends on the newer.
TEST(SequencePlayerTest, OnlyTheFrameLastAskedForIsShown)
{
    PlayerCase opened = openPlayer(FixtureSequence{.frames = 5u}, false, 1u);
    ASSERT_NE(opened.player, nullptr);
    // The session's only thread is held, so both requests wait at admission.
    std::optional<an::consumer::OperationScope> held = an::tests::viewer::admitted(opened.context->engineSession());
    ASSERT_TRUE(held.has_value());
    opened.player->setCurrentFrame(1u, {});
    opened.player->setCurrentFrame(4u, {});
    pumpQuietly();
    held.reset();
    ASSERT_TRUE(settle(opened));
    EXPECT_EQ(opened.player->framesShown(), 1u);
    EXPECT_EQ(opened.view->document().sequence()->currentFrame, 4u);
    EXPECT_EQ(largestDifference(shownSamples(*opened.view), fixtureSamples(4u, opened.fixture, false)), 0.0);

    // A read that has finished when a newer frame is asked for lands with its frame, and is dropped.
    opened.player->setCurrentFrame(2u, {});
    QThreadPool::globalInstance()->waitForDone();
    opened.player->setCurrentFrame(3u, {});
    ASSERT_TRUE(settle(opened));
    EXPECT_EQ(opened.player->framesShown(), 2u);
    EXPECT_EQ(opened.view->document().sequence()->currentFrame, 3u);
    EXPECT_EQ(largestDifference(shownSamples(*opened.view), fixtureSamples(3u, opened.fixture, false)), 0.0);
}

// Playback shows frame after frame and ends at the last; Stop shows the first frame again; Play from the last frame
// plays from the first.
TEST(SequencePlayerTest, PlaybackShowsEachFrameAndEndsAtTheLast)
{
    PlayerCase opened = openPlayer(FixtureSequence{.frames = 4u});
    ASSERT_NE(opened.player, nullptr);
    opened.player->setFramesPerSecond(60.0);
    opened.player->resume();
    EXPECT_TRUE(opened.player->isPlaying());
    ASSERT_TRUE(pumpUntil([&]() { return !opened.player->isPlaying(); }));
    ASSERT_TRUE(settle(opened));
    EXPECT_EQ(opened.player->currentFrame(), 3u);
    EXPECT_EQ(opened.player->framesShown(), 3u);
    EXPECT_EQ(largestDifference(shownSamples(*opened.view), fixtureSamples(3u, opened.fixture, false)), 0.0);

    // Playing again from the last frame starts from the first; Stop while playing goes back to it.
    opened.player->setFramesPerSecond(1.0);
    opened.player->resume();
    EXPECT_EQ(opened.player->currentFrame(), 0u);
    opened.player->stop();
    EXPECT_FALSE(opened.player->isPlaying());
    ASSERT_TRUE(settle(opened));
    EXPECT_EQ(opened.player->currentFrame(), 0u);
    EXPECT_EQ(largestDifference(shownSamples(*opened.view), fixtureSamples(0u, opened.fixture, false)), 0.0);

    // Stop while playing from another frame shows the first.
    opened.player->setCurrentFrame(2u, {});
    ASSERT_TRUE(settle(opened));
    opened.player->resume();
    ASSERT_TRUE(opened.player->isPlaying());
    EXPECT_EQ(opened.player->currentFrame(), 2u);
    opened.player->stop();
    ASSERT_TRUE(settle(opened));
    EXPECT_EQ(opened.player->currentFrame(), 0u);
    EXPECT_EQ(opened.view->document().sequence()->currentFrame, 0u);
    EXPECT_EQ(largestDifference(shownSamples(*opened.view), fixtureSamples(0u, opened.fixture, false)), 0.0);
}

// Playback never overtakes a frame on its way: while one waits, playback drops ticks rather than frames, and shows
// every frame once they can be read.
TEST(SequencePlayerTest, PlaybackWaitsForAFrameOnItsWay)
{
    PlayerCase opened = openPlayer(FixtureSequence{.frames = 4u}, false, 1u);
    ASSERT_NE(opened.player, nullptr);
    opened.player->setFramesPerSecond(60.0);
    // The session's only thread is held, so the first frame playback asks for waits at admission.
    std::optional<an::consumer::OperationScope> held = an::tests::viewer::admitted(opened.context->engineSession());
    ASSERT_TRUE(held.has_value());
    opened.player->resume();
    for (int quiet = 0; quiet < 3; ++quiet)
    {
        pumpQuietly();
    }
    EXPECT_TRUE(opened.player->isPlaying());
    EXPECT_EQ(opened.player->currentFrame(), 1u);
    EXPECT_FALSE(opened.player->isIdle());
    held.reset();
    ASSERT_TRUE(pumpUntil([&]() { return !opened.player->isPlaying(); }));
    ASSERT_TRUE(settle(opened));
    EXPECT_EQ(opened.player->currentFrame(), 3u);
    EXPECT_EQ(opened.player->framesShown(), 3u);
    EXPECT_EQ(largestDifference(shownSamples(*opened.view), fixtureSamples(3u, opened.fixture, false)), 0.0);
}

// The source is kept between frames under the engine context's lease, and let go - its lease with it - when the view
// shuts the player down.
TEST(SequencePlayerTest, TheSourceIsKeptBetweenFramesAndLetGoAtShutdown)
{
    PlayerCase opened = openPlayer(FixtureSequence{.frames = 3u});
    ASSERT_NE(opened.player, nullptr);
    EXPECT_FALSE(opened.player->holdsSource());
    opened.player->setCurrentFrame(1u, {});
    ASSERT_TRUE(settle(opened));
    EXPECT_TRUE(opened.player->holdsSource());
    opened.player->setCurrentFrame(2u, {});
    ASSERT_TRUE(settle(opened));
    EXPECT_TRUE(opened.player->holdsSource());
    const std::uint64_t withSource = opened.context->engineSession().outstanding().engineLeases;

    opened.player->shutdown();
    EXPECT_FALSE(opened.player->holdsSource());
    EXPECT_EQ(opened.context->engineSession().outstanding().engineLeases, withSource - 1u);
    // A shut-down player asks for nothing more.
    opened.player->setCurrentFrame(0u, {});
    ASSERT_TRUE(settle(opened));
    EXPECT_EQ(opened.view->document().sequence()->currentFrame, 2u);
}

// A read under way when the player shuts down closes the source as it lands, and shows nothing.
TEST(SequencePlayerTest, AReadUnderWayAtShutdownLetsTheSourceGoWhenItLands)
{
    PlayerCase opened = openPlayer(FixtureSequence{.frames = 3u}, false, 1u);
    ASSERT_NE(opened.player, nullptr);
    opened.player->setCurrentFrame(1u, {});
    ASSERT_TRUE(settle(opened));
    const std::uint64_t withSource = opened.context->engineSession().outstanding().engineLeases;
    std::optional<an::consumer::OperationScope> held = an::tests::viewer::admitted(opened.context->engineSession());
    ASSERT_TRUE(held.has_value());
    opened.player->setCurrentFrame(2u, {});
    // The read holds the source while it waits for admission, so the shutdown cannot close it then.
    ASSERT_TRUE(pumpUntil([&]() { return opened.context->engineSession().outstanding().queued == 1u; }));
    opened.player->shutdown();
    // The shutdown stops the wait: the read ends while the session's only thread is still held.
    EXPECT_TRUE(QThreadPool::globalInstance()->waitForDone(5000));
    EXPECT_EQ(opened.context->engineSession().outstanding().queued, 0u);
    held.reset();
    QThreadPool::globalInstance()->waitForDone();
    pumpQuietly();
    EXPECT_FALSE(opened.player->holdsSource());
    EXPECT_EQ(opened.context->engineSession().outstanding().engineLeases, withSource - 1u);
    EXPECT_EQ(opened.view->document().sequence()->currentFrame, 1u);
}

// Another sequence shown in the view replaces the player's source: the player follows its frames and lets the first
// sequence's source go.
TEST(SequencePlayerTest, AnotherSequenceShownInTheViewReplacesTheSource)
{
    PlayerCase opened = openPlayer(FixtureSequence{.frames = 3u});
    ASSERT_NE(opened.player, nullptr);
    opened.player->setCurrentFrame(1u, {});
    ASSERT_TRUE(settle(opened));
    const std::uint64_t withSource = opened.context->engineSession().outstanding().engineLeases;

    const FixtureSequence other{.frames = 5u, .width = 4u, .height = 4u};
    const QString otherPath = opened.context->pathOf(QStringLiteral("other.ser"));
    ASSERT_TRUE(writeFixture(opened.context->engineSession(), otherPath, an::SequenceFormat::Ser, other));
    an::Result<an::ui::ImageDocument> document =
        an::ui::openSequenceDocument(opened.context->engineSession(), otherPath, an::SequenceFormat::Ser, {});
    ASSERT_TRUE(document.hasValue());
    opened.view->setSequenceDocument(std::move(*document), false);
    pumpQuietly();
    EXPECT_EQ(opened.player->frameCount(), 5u);
    EXPECT_EQ(opened.player->currentFrame(), 0u);
    EXPECT_FALSE(opened.player->holdsSource());
    EXPECT_LE(opened.context->engineSession().outstanding().engineLeases, withSource);

    opened.player->setCurrentFrame(4u, {});
    ASSERT_TRUE(settle(opened));
    EXPECT_EQ(largestDifference(shownSamples(*opened.view), fixtureSamples(4u, other, false)), 0.0);
}

// A player held still while a process runs over the sequence stops playing and answers nothing, until it is enabled
// again.
TEST(SequencePlayerTest, AHeldPlayerStopsPlayingUntilItIsEnabled)
{
    PlayerCase opened = openPlayer(FixtureSequence{.frames = 3u});
    ASSERT_NE(opened.player, nullptr);
    opened.player->setFramesPerSecond(1.0);
    opened.player->resume();
    ASSERT_TRUE(opened.player->isPlaying());
    opened.player->setEnabled(false);
    EXPECT_FALSE(opened.player->isPlaying());
    EXPECT_FALSE(opened.player->isEnabled());
    opened.player->setEnabled(true);
    EXPECT_TRUE(opened.player->isEnabled());
    ASSERT_TRUE(settle(opened));
}

// The player writes the view's sequence to a file through its publication.
TEST(SequencePlayerTest, TheSequenceIsWrittenThroughItsPublication)
{
    PlayerCase opened = openPlayer(FixtureSequence{.frames = 3u});
    ASSERT_NE(opened.player, nullptr);
    const QString copy = opened.context->pathOf(QStringLiteral("copy.anseq"));
    const an::Status saved = opened.player->saveTo(copy);
    ASSERT_TRUE(saved.ok()) << saved.message();
    EXPECT_TRUE(opened.view->saveSequenceToFile(opened.context->pathOf(QStringLiteral("again.ser")).toStdString()));
    const std::optional<ReadBack> read = readBack(opened.context->engineSession(), copy, an::SequenceFormat::ImageList);
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(read->samples.size(), 3u);
    EXPECT_EQ(largestDifference(read->samples[2], fixtureSamples(2u, opened.fixture, false)), 0.0);
}

// A frame that cannot be read is reported and ends playback; the view keeps what it showed.
TEST(SequencePlayerTest, AFrameThatCannotBeReadIsReported)
{
    PlayerCase opened = openPlayer(FixtureSequence{.frames = 3u});
    ASSERT_NE(opened.player, nullptr);
    ASSERT_TRUE(QFile::remove(opened.path));
    opened.player->setFramesPerSecond(30.0);
    opened.player->resume();
    ASSERT_TRUE(pumpUntil([&]() { return opened.context->errorCount() > 0; }));
    ASSERT_TRUE(settle(opened));
    EXPECT_TRUE(opened.context->messages.contains(QStringLiteral("error:Sequence Player")));
    EXPECT_FALSE(opened.player->isPlaying());
    EXPECT_EQ(opened.player->framesShown(), 0u);
    EXPECT_EQ(largestDifference(shownSamples(*opened.view), fixtureSamples(0u, opened.fixture, false)), 0.0);
}

// A view closed while a frame is on its way keeps nothing: the frame lands on a player that is gone, and the source's
// lease goes with it.
TEST(SequencePlayerTest, AViewClosedWhileAFrameIsOnItsWayKeepsNothing)
{
    PlayerCase opened = openPlayer(FixtureSequence{.frames = 3u}, false, 1u);
    ASSERT_NE(opened.player, nullptr);
    std::optional<an::consumer::OperationScope> held = an::tests::viewer::admitted(opened.context->engineSession());
    ASSERT_TRUE(held.has_value());
    opened.player->setCurrentFrame(2u, {});
    opened.context->closeView(opened.view);
    opened.view = nullptr;
    opened.player = nullptr;
    // The read waiting for the session's thread is stopped by the closing player, not by the thread coming free.
    QThreadPool::globalInstance()->waitForDone();
    held.reset();
    QThreadPool::globalInstance()->waitForDone();
    pumpQuietly();
    const an::consumer::SessionOutstanding outstanding = opened.context->engineSession().outstanding();
    EXPECT_EQ(outstanding.assets, 0u);
    EXPECT_EQ(outstanding.operations, 0u);
}

// A mosaic sequence shown demosaiced takes the one demosaic setting when the view shows it, and every frame the player
// shows is demosaiced by it.
TEST(SequencePlayerTest, AMosaicSequenceIsDemosaicedByTheOneSetting)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ASSERT_TRUE(context->upsert(QString::fromLatin1(SettingsKeys::RAW_DEMOSAIC_ALGORITHM), QStringLiteral("bilinear")));
    const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(rggb.has_value());
    const QString path = context->pathOf(QStringLiteral("mosaic.ser"));
    ASSERT_TRUE(writeFixture(context->engineSession(), path, an::SequenceFormat::Ser,
                             FixtureSequence{.frames = 3u, .cfa = *rggb}));
    an::Result<an::ui::ImageDocument> document =
        an::ui::openSequenceDocument(context->engineSession(), path, an::SequenceFormat::Ser, {});
    ASSERT_TRUE(document.hasValue());
    EXPECT_FALSE(document->sequence()->demosaic.has_value());
    ImageViewControl* view = context->createNewImageViewControlFromSequence(
        nullptr, std::move(*document), nullptr, true, "mosaic", ViewControlMode::standard, nullptr, false, false);
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->document().sequence()->demosaic, an::DemosaicMethod::Fast);

    auto* player = dynamic_cast<SequencePlayerControl*>(view->sequencePlayer());
    ASSERT_NE(player, nullptr);
    player->setCurrentFrame(1u, {});
    ASSERT_TRUE(pumpUntil([&]() { return player->framesShown() == 1u && view->isWorkIdle(); }));
    const std::optional<an::ImageDescriptor> shown = view->imageDescriptor();
    ASSERT_TRUE(shown.has_value());
    EXPECT_EQ(shown->channelSchema(), an::ChannelSchema::Rgb);
    EXPECT_FALSE(shown->isMosaiced());
}

// An X-Trans sequence shown demosaiced keeps the one setting as the method chosen, and every frame the player shows is
// demosaiced by that method's counterpart for a six-by-six table - three-pass Markesteijn for RCD. Every method the
// setting offers estimates a two-by-two block, which the engine refuses over an X-Trans frame, so the player showed
// none of the sequence's frames.
TEST(SequencePlayerTest, AnXTransSequenceIsDemosaicedByTheTabulatedCounterpartOfTheOneSetting)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ASSERT_TRUE(context->upsert(QString::fromLatin1(SettingsKeys::RAW_DEMOSAIC_ALGORITHM), QStringLiteral("rcd")));
    const QString list = writeXTransList(*context, 3u);
    ASSERT_FALSE(list.isEmpty());
    an::Result<an::ui::ImageDocument> document =
        an::ui::openSequenceDocument(context->engineSession(), list, an::SequenceFormat::ImageList, {});
    ASSERT_TRUE(document.hasValue()) << document.status().message();
    {
        an::Result<an::consumer::AssetReadLease> lease = document->asset()->read();
        ASSERT_TRUE(lease.hasValue());
        const std::optional<an::ImageDescriptor> stored =
            lease->visit([](const auto& typed) { return typed.descriptor(); });
        ASSERT_TRUE(stored.has_value());
        ASSERT_TRUE(stored->cfa().hasFilterTable()) << "the stills keep their six-by-six table";
    }
    ImageViewControl* view = context->createNewImageViewControlFromSequence(
        nullptr, std::move(*document), nullptr, true, "xtrans", ViewControlMode::standard, nullptr, false, false);
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->document().sequence()->demosaic, an::DemosaicMethod::Rcd) << "the method chosen";

    auto* player = dynamic_cast<SequencePlayerControl*>(view->sequencePlayer());
    ASSERT_NE(player, nullptr);
    player->setCurrentFrame(1u, {});
    ASSERT_TRUE(
        pumpUntil([&]() { return (player->framesShown() == 1u && view->isWorkIdle()) || context->errorCount() > 0; }));
    ASSERT_EQ(player->framesShown(), 1u) << context->messages.join(QStringLiteral(", ")).toStdString();
    const std::optional<an::ImageDescriptor> shown = view->imageDescriptor();
    ASSERT_TRUE(shown.has_value());
    EXPECT_EQ(shown->channelSchema(), an::ChannelSchema::Rgb);
    EXPECT_FALSE(shown->isMosaiced());
    EXPECT_TRUE(an::tests::viewer::lineageMentions(*view->document().asset(), "MarkesteijnThreePass"));
}
