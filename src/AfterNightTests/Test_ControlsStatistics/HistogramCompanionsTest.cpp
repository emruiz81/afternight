// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The histograms other parts of Controls show without owning, defined beside the histogram widgets: the lens's, which
/// draws the counts the view hands it, and a process dialog's, which counts the image it is given on a worker in the
/// process's session and shows only the latest count asked for, never one that lands after its widget is gone. A count
/// is not held to the threads kept for previews unless it is a real-time preview's own.

#include "Test_ControlsStatistics/StatisticsTestSupport.h"
#include "Test_ControlsViewer/EditHoldingTheWriter.h"

#include "controls/ImageViewCompanions.h"
#include "controls/ProcessCompanions.h"
#include "widgets/HistogramCompanions.h"
#include "widgets/HistogramWidget.h"
#include "widgets/ViewPresentation.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <QScrollArea>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{

using an::tests::statistics::Channels;
using an::tests::statistics::InstalledContext;
using an::tests::statistics::openStatisticsContext;
using an::tests::statistics::pumpQuietly;
using an::tests::statistics::pumpUntil;
using an::tests::statistics::StatisticsTestContext;

/// A grey Float32 image every sample of which is `level`.
[[nodiscard]] std::shared_ptr<const an::consumer::ImageAsset> flatImage(const an::consumer::EngineSession& session,
                                                                        const float level)
{
    std::optional<an::consumer::ImageAsset> asset = an::tests::statistics::makeAsset<float>(
        session, 16u, 8u, Channels::Grey, [level](std::uint32_t, std::uint32_t, std::uint32_t) { return level; });
    return asset.has_value() ? std::make_shared<const an::consumer::ImageAsset>(std::move(*asset)) : nullptr;
}

/// The bin a flat image's samples all landed in, or nothing.
[[nodiscard]] std::optional<std::size_t> filledBin(const HistogramWidget& widget)
{
    const std::shared_ptr<const an::ui::HistogramDisplayData> counts = widget.getHistogram();
    if (!counts || !counts->isValid())
    {
        return std::nullopt;
    }
    const std::vector<std::uint64_t>& bins = counts->channels.front().bins;
    for (std::size_t bin = 0u; bin < bins.size(); ++bin)
    {
        if (bins[bin] != 0u)
        {
            return bin;
        }
    }
    return std::nullopt;
}

/// The cases' context, installed, over a session one operation wide so a case can hold its only slot.
class HistogramCompanionsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openStatisticsContext(1u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    std::unique_ptr<StatisticsTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

/// The cases' context over a session eight wide, whose previews are kept two threads, with both of them taken.
class HistogramCompanionsPreviewsTest : public HistogramCompanionsTest
{
protected:
    void SetUp() override
    {
        m_context = openStatisticsContext(8u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
        an::Result<an::consumer::OperationScope> previews = session().admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview,
                                           .maxConcurrency = an::consumer::previewReservationWidth(8u)});
        ASSERT_TRUE(previews.hasValue()) << previews.status().message();
        m_previews.emplace(std::move(*previews));
        ASSERT_EQ(session().admission().previewThreads, an::consumer::previewReservationWidth(8u));
    }

    void TearDown() override
    {
        m_previews.reset();
        QThreadPool::globalInstance()->waitForDone();
        HistogramCompanionsTest::TearDown();
    }

    std::optional<an::consumer::OperationScope> m_previews;
};

} // namespace

// The lens's histogram is a small histogram widget without a grid scale or a gradient, drawing the lens's counts as
// they are handed to it.
TEST_F(HistogramCompanionsTest, TheLensHistogramDrawsTheCountsItIsHanded)
{
    QWidget lens;
    std::unique_ptr<an::ui::ILensHistogram> histogram = an::ui::createLensHistogram(&lens);
    ASSERT_NE(histogram, nullptr);
    auto* widget = dynamic_cast<HistogramWidget*>(histogram->widget());
    ASSERT_NE(widget, nullptr);
    EXPECT_EQ(widget->parentWidget(), &lens);
    EXPECT_FALSE(widget->getGradientAreaEnabled());
    EXPECT_FALSE(widget->getClippingInfoEnabled());
    EXPECT_TRUE(widget->getGridEnabled());

    an::ui::LensHistogram counts;
    counts.channels = 3u;
    counts.counts[0][3] = 7u;
    counts.counts[1][10] = 2u;
    counts.counts[2][63] = 5u;
    histogram->setCounts(counts);
    const std::shared_ptr<const an::ui::HistogramDisplayData> shown = widget->getHistogram();
    ASSERT_EQ(shown->channels.size(), 3u);
    EXPECT_EQ(shown->channels[0].bins.size(), an::ui::kLensHistogramBins);
    EXPECT_EQ(shown->channels[0].bins[3], 7u);
    EXPECT_EQ(shown->channels[1].bins[10], 2u);
    EXPECT_EQ(shown->channels[2].bins[63], 5u);
    EXPECT_EQ(shown->channels[2].samples, 5u);

    counts.channels = 1u;
    histogram->setCounts(counts);
    EXPECT_EQ(widget->getHistogram()->channels.size(), 1u);
}

// A process dialog's histogram sits in its scroll area, counts the image it is given on a worker, and counts it again
// at a resolution it is told; each resolution is the number of bins its label says.
TEST_F(HistogramCompanionsTest, ADialogsHistogramCountsItsImageOnAWorkerAtItsResolution)
{
    QScrollArea area;
    std::unique_ptr<an::ui::IPreviewHistogram> histogram = an::ui::createPreviewHistogram(&area);
    ASSERT_NE(histogram, nullptr);
    auto* view = dynamic_cast<an::ui::PreviewHistogramView*>(histogram.get());
    ASSERT_NE(view, nullptr);
    auto* widget = dynamic_cast<HistogramWidget*>(histogram->widget());
    ASSERT_NE(widget, nullptr);
    EXPECT_EQ(area.widget(), widget);
    EXPECT_TRUE(widget->getClippingInfoEnabled());

    histogram->setImage(flatImage(session(), 0.5f));
    ASSERT_TRUE(pumpUntil([view]() { return view->scheduler().isIdle(); }));
    ASSERT_TRUE(widget->getHistogram()->isValid());
    EXPECT_EQ(widget->getHistogram()->channels.front().bins.size(), 256u);
    EXPECT_EQ(filledBin(*widget), std::optional<std::size_t>(127u));

    histogram->setResolution(an::ui::PreviewHistogramResolution::Levels1000);
    ASSERT_TRUE(pumpUntil([view]() { return view->scheduler().isIdle(); }));
    EXPECT_EQ(widget->getHistogram()->channels.front().bins.size(), 1000u);
    EXPECT_EQ(view->scheduler().telemetry().shown, 2u);

    // The same resolution again counts nothing.
    histogram->setResolution(an::ui::PreviewHistogramResolution::Levels1000);
    pumpQuietly();
    EXPECT_EQ(view->scheduler().telemetry().started, 2u);

    histogram->setImage(nullptr);
    EXPECT_FALSE(widget->getHistogram()->isValid());

    for (const auto [resolution, bins] : {std::pair{an::ui::PreviewHistogramResolution::Bits6, 64u},
                                          std::pair{an::ui::PreviewHistogramResolution::Bits7, 128u},
                                          std::pair{an::ui::PreviewHistogramResolution::Bits8, 256u},
                                          std::pair{an::ui::PreviewHistogramResolution::Bits9, 512u},
                                          std::pair{an::ui::PreviewHistogramResolution::Bits10, 1024u},
                                          std::pair{an::ui::PreviewHistogramResolution::Bits12, 4096u},
                                          std::pair{an::ui::PreviewHistogramResolution::Bits14, 16384u},
                                          std::pair{an::ui::PreviewHistogramResolution::Bits16, 65536u},
                                          std::pair{an::ui::PreviewHistogramResolution::Levels100, 100u},
                                          std::pair{an::ui::PreviewHistogramResolution::Levels1000, 1000u},
                                          std::pair{an::ui::PreviewHistogramResolution::Levels10000, 10000u}})
    {
        EXPECT_EQ(an::ui::histogramBinCount(an::ui::histogramResolutionOf(resolution)), bins);
    }
}

// A count asked for while another waits stops it, and only the latest is shown: the first image's count is set aside
// when it lands, and the widget shows the second's.
TEST_F(HistogramCompanionsTest, OnlyTheLatestCountAskedForIsShown)
{
    QScrollArea area;
    std::unique_ptr<an::ui::IPreviewHistogram> histogram = an::ui::createPreviewHistogram(&area);
    auto* view = dynamic_cast<an::ui::PreviewHistogramView*>(histogram.get());
    ASSERT_NE(view, nullptr);
    auto* widget = dynamic_cast<HistogramWidget*>(histogram->widget());
    const std::shared_ptr<const an::consumer::ImageAsset> dark = flatImage(session(), 0.25f);
    const std::shared_ptr<const an::consumer::ImageAsset> bright = flatImage(session(), 0.75f);
    ASSERT_TRUE(dark && bright);

    // The session's only slot is held, so both counts wait to be admitted.
    std::optional<an::consumer::OperationScope> held = an::tests::viewer::admitted(session());
    ASSERT_TRUE(held.has_value());
    histogram->setImage(dark);
    histogram->setImage(bright);
    ASSERT_TRUE(pumpUntil([view]() { return view->scheduler().telemetry().superseded == 1u; }));
    EXPECT_FALSE(widget->getHistogram()->isValid()) << "the set-aside count was shown";
    held.reset();

    ASSERT_TRUE(pumpUntil([view]() { return view->scheduler().isIdle(); }));
    const an::ui::HistogramCountTelemetry telemetry = view->scheduler().telemetry();
    EXPECT_EQ(telemetry.started, 2u);
    EXPECT_EQ(telemetry.shown, 1u);
    EXPECT_EQ(telemetry.superseded, 1u);
    EXPECT_EQ(telemetry.failed, 0u);
    EXPECT_EQ(filledBin(*widget), std::optional<std::size_t>(191u));
}

// Counts a preview's frame made are shown as they are handed in: a count of an image on its way is stopped and never
// shown, nothing is counted for them, and a change of resolution counts nothing either - the frame that made them makes
// the next at the new resolution.
TEST_F(HistogramCompanionsTest, CountsHandedInAreShownAsTheyCameAndACountOnItsWayIsNeverShown)
{
    QScrollArea area;
    std::unique_ptr<an::ui::IPreviewHistogram> histogram = an::ui::createPreviewHistogram(&area);
    auto* view = dynamic_cast<an::ui::PreviewHistogramView*>(histogram.get());
    ASSERT_NE(view, nullptr);
    auto* widget = dynamic_cast<HistogramWidget*>(histogram->widget());
    ASSERT_NE(widget, nullptr);
    EXPECT_EQ(histogram->resolution(), an::ui::PreviewHistogramResolution::Bits8);

    // The session's only slot is held, so the count of the image waits to be admitted while the counts are handed in.
    std::optional<an::consumer::OperationScope> held = an::tests::viewer::admitted(session());
    ASSERT_TRUE(held.has_value());
    histogram->setImage(flatImage(session(), 0.25f));
    auto handed = std::make_shared<an::ui::HistogramDisplayData>();
    an::ui::HistogramDisplayChannel channel;
    channel.bins.assign(256u, 0u);
    channel.bins[42u] = 7u;
    channel.samples = 7u;
    handed->channels.push_back(channel);
    histogram->showCounts(handed);
    EXPECT_EQ(widget->getHistogram(), handed);
    held.reset();
    ASSERT_TRUE(pumpUntil([view]() { return view->scheduler().telemetry().superseded == 1u; }));
    pumpQuietly();
    EXPECT_EQ(widget->getHistogram(), handed) << "the count of the image was shown over the counts handed in";
    EXPECT_TRUE(view->scheduler().isIdle());

    histogram->setResolution(an::ui::PreviewHistogramResolution::Levels1000);
    pumpQuietly();
    EXPECT_EQ(histogram->resolution(), an::ui::PreviewHistogramResolution::Levels1000);
    const an::ui::HistogramCountTelemetry telemetry = view->scheduler().telemetry();
    EXPECT_EQ(telemetry.started, 1u) << "a change of resolution counted the counts handed in again";
    EXPECT_EQ(telemetry.handed, 1u);
    EXPECT_EQ(telemetry.shown, 0u);
}

// A count that lands after its dialog's histogram is gone is dropped: nothing is drawn into a widget that no longer
// exists, and nothing waits for it.
TEST_F(HistogramCompanionsTest, ACountThatLandsAfterItsHistogramIsGoneIsDropped)
{
    std::optional<an::consumer::OperationScope> held = an::tests::viewer::admitted(session());
    ASSERT_TRUE(held.has_value());
    {
        auto area = std::make_unique<QScrollArea>();
        std::unique_ptr<an::ui::IPreviewHistogram> histogram = an::ui::createPreviewHistogram(area.get());
        histogram->setImage(flatImage(session(), 0.5f));
        histogram.reset();
        area.reset();
    }
    held.reset();
    QThreadPool::globalInstance()->waitForDone();
    pumpQuietly();
    SUCCEED();
}

// A count takes its read lease before it asks to be admitted. An edit holds the image's writer and then asks to be
// admitted, so at a width of one a count admitted first would hold the only thread while it waited for the writer, and
// the edit would wait for that thread: neither would end.
TEST_F(HistogramCompanionsTest, ACountWaitingForAnEditsWriterHoldsNoThreadTheEditNeeds)
{
    std::optional<an::consumer::ImageAsset> image = an::tests::statistics::makeAsset<float>(
        session(), 16u, 8u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    ASSERT_TRUE(image.has_value());
    an::Result<an::consumer::AssetWriteLease> writer = image->write();
    ASSERT_TRUE(writer.hasValue()) << writer.status().message();

    const std::uint64_t admittedBefore = session().admission().admitted;
    std::future<an::Result<an::ui::HistogramDisplayData>> counted = std::async(std::launch::async, [this, &image]() {
        return an::ui::countHistogram(session(), *image, an::ui::HistogramResolution::Bits6);
    });
    EXPECT_FALSE(an::tests::viewer::admittedBeyond(session(), admittedBefore))
        << "the count was admitted while it could not read the image";

    std::stop_source giveUp;
    std::future<an::Result<an::consumer::OperationScope>> edit = std::async(std::launch::async, [this, &giveUp]() {
        return session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply,
                                                              .stopToken = giveUp.get_token()});
    });
    const bool answered = edit.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    giveUp.request_stop();
    an::Result<an::consumer::OperationScope> admitted = edit.get();
    EXPECT_TRUE(answered && admitted.hasValue())
        << "the edit waited for the thread the count held while the count waited for the edit's writer";
    if (admitted.hasValue())
    {
        admitted->end();
    }
    *writer = an::consumer::AssetWriteLease{};

    an::Result<an::ui::HistogramDisplayData> counts = counted.get();
    ASSERT_TRUE(counts.hasValue()) << counts.status().message();
    EXPECT_TRUE(counts->isValid());
}

// A histogram counted because someone asked for it - the histogram window's, the history window's, a dialog's over its
// target - is not held to the threads a session keeps for previews: with every one of them taken, it is still counted.
TEST_F(HistogramCompanionsPreviewsTest, ACountIsNotQueuedBehindThePreviews)
{
    const std::shared_ptr<const an::consumer::ImageAsset> image = flatImage(session(), 0.5f);
    ASSERT_NE(image, nullptr);

    QScrollArea windowArea;
    auto* widget = new HistogramWidget(nullptr);
    windowArea.setWidget(widget);
    an::ui::HistogramCountScheduler scheduler(widget);
    scheduler.count(image);
    EXPECT_TRUE(pumpUntil([&scheduler]() { return scheduler.isIdle(); })) << "the count waited for a preview thread";
    EXPECT_EQ(scheduler.telemetry().shown, 1u);

    QScrollArea historyArea;
    std::unique_ptr<an::ui::IPreviewHistogram> histogram = an::ui::createPreviewHistogram(&historyArea);
    auto* view = dynamic_cast<an::ui::PreviewHistogramView*>(histogram.get());
    ASSERT_NE(view, nullptr);
    histogram->setImage(image);
    EXPECT_TRUE(pumpUntil([view]() { return view->scheduler().isIdle(); })) << "the count waited for a preview thread";
    EXPECT_EQ(view->scheduler().telemetry().shown, 1u);
}

// The histogram of a real-time preview is counted again with every preview the dialog shows, so it is part of the
// preview: while other work waits for threads it is counted on the ones kept for previews, where a count anyone else
// asked for takes its turn behind that work.
TEST_F(HistogramCompanionsPreviewsTest, APreviewsHistogramIsCountedOnThePreviewsThreadsWhileOtherWorkWaits)
{
    const std::shared_ptr<const an::consumer::ImageAsset> image = flatImage(session(), 0.5f);
    ASSERT_NE(image, nullptr);
    // A dialog keeps the reservation, other work holds the rest of the session, and more of it waits.
    m_previews.reset();
    an::Result<an::consumer::PreviewReservation> dialog = session().reservePreview();
    ASSERT_TRUE(dialog.hasValue());
    an::Result<an::consumer::OperationScope> other =
        session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
    ASSERT_TRUE(other.hasValue()) << other.status().message();
    ASSERT_EQ(other->grantedConcurrency(), 8u - an::consumer::previewReservationWidth(8u));
    std::stop_source giveUp;
    std::future<an::Result<an::consumer::OperationScope>> waiting = std::async(std::launch::async, [this, &giveUp]() {
        return session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch,
                                                              .stopToken = giveUp.get_token()});
    });
    ASSERT_TRUE(pumpUntil([this]() { return session().admission().queued == 1u; }));

    QScrollArea previewArea;
    std::unique_ptr<an::ui::IPreviewHistogram> preview =
        an::ui::createPreviewHistogram(&previewArea, an::consumer::OperationPurpose::Preview);
    auto* previewView = dynamic_cast<an::ui::PreviewHistogramView*>(preview.get());
    ASSERT_NE(previewView, nullptr);
    preview->setImage(image);
    EXPECT_TRUE(pumpUntil([previewView]() { return previewView->scheduler().isIdle(); }))
        << "the preview's histogram waited behind other work";
    EXPECT_EQ(previewView->scheduler().telemetry().shown, 1u);

    QScrollArea askedArea;
    std::unique_ptr<an::ui::IPreviewHistogram> asked = an::ui::createPreviewHistogram(&askedArea);
    auto* askedView = dynamic_cast<an::ui::PreviewHistogramView*>(asked.get());
    ASSERT_NE(askedView, nullptr);
    asked->setImage(image);
    ASSERT_TRUE(pumpUntil([this]() { return session().admission().queued == 2u; }));
    EXPECT_EQ(askedView->scheduler().telemetry().shown, 0u) << "a count asked for overtook the work waiting before it";

    giveUp.request_stop();
    EXPECT_EQ(waiting.get().code(), an::ErrorCode::Cancelled);
    other->end();
    ASSERT_TRUE(pumpUntil([askedView]() { return askedView->scheduler().isIdle(); }));
    EXPECT_EQ(askedView->scheduler().telemetry().shown, 1u);
}

// Without a session the histogram shows nothing and says the count failed, rather than waiting for one.
TEST(HistogramCompanionsWithoutSessionTest, WithoutASessionTheHistogramShowsNothing)
{
    QScrollArea area;
    HistogramWidget* widget = new HistogramWidget(nullptr);
    area.setWidget(widget);
    an::ui::HistogramCountScheduler scheduler(widget);

    std::optional<an::consumer::EngineSession> session;
    {
        an::Result<an::consumer::EngineSession> opened = an::tests::fixtures::engine::createFixtureSession(1u);
        ASSERT_TRUE(opened.hasValue());
        session.emplace(std::move(*opened));
    }
    const std::shared_ptr<const an::consumer::ImageAsset> image = flatImage(*session, 0.5f);
    scheduler.count(image);
    EXPECT_TRUE(scheduler.isIdle());
    EXPECT_EQ(scheduler.telemetry().failed, 1u);
    EXPECT_EQ(scheduler.telemetry().started, 0u);
    EXPECT_FALSE(widget->getHistogram()->isValid());
    EXPECT_EQ(scheduler.image(), image);
}
