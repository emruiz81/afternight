// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The controller that feeds the readout, over views of a real session: it follows the active view; a burst of changes
/// makes one update, and an update measures only what changed; a measurement is admitted as a preview that never
/// waits, and one refused is tried again at the next change; only a measurement whose ticket still commits is shown,
/// so a view closed while it is measured shows nothing; the GUI thread makes no call of the engine. In a task it
/// measures settled frames only, with the original's figures beside them for the change. The figures equal the
/// statistics window's for the same image and range; the overlay chips act on the view's own actions; the column's
/// folds are remembered. And what the readout writes of a solution and of an image's description.

#include "ShellCoreTestSupport.h"
#include "ShellCoreViewSupport.h"

#include "core/ApplicationContext.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationEvents.h"
#include "readout/ReadoutController.h"
#include "readout/ReadoutSummaries.h"
#include "views/ViewService.h"

#include "components/AnAstrometryCard.h"
#include "components/AnChip.h"
#include "components/AnCompactReadout.h"
#include "components/AnHistogramCard.h"
#include "components/AnIconButton.h"
#include "components/AnImageFactsCard.h"
#include "components/AnReadoutColumn.h"
#include "components/AnReadoutGrid.h"
#include "components/AnSegmentedControl.h"
#include "components/AnStatisticsCard.h"
#include "components/ReadoutFigures.h"
#include "controls/IAppContext.h"
#include "controls/ImageAstrometryInfoFormatter.h"
#include "controls/ImageStatisticsControl.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/settings/SettingsKeys.h"
#include "session/ImageDocument.h"
#include "theme/ComponentParts.h"
#include "widgets/HistogramWidget.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QPointer>
#include <QElapsedTimer>
#include <QMenu>
#include <QSet>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::shellcore::pumpUntil;
using an::tests::shellcore::RecordingShell;
using an::tests::shellcore::ScopedStorageRoot;
using an::tests::shellcore::SessionProvider;
using an::ui::ReadoutStatistic;
using an::ui::StatisticsScale;
using an::ui::shell::ApplicationCore;
using an::ui::shell::IReadoutFrameSource;

/// An image of the session: `width` x `height` samples of `channels` channels, each `value(x, y, channel)`.
template <typename SampleT>
[[nodiscard]] std::optional<an::consumer::ImageAsset>
assetOf(const an::consumer::EngineSession& session, const std::uint32_t width, const std::uint32_t height,
        const std::uint32_t channels, const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)>& value)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, hold->engine().limits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(
        an::ImageDescriptorRequest{.extent = *extent,
                                   .sampleType = an::kSampleTypeOf<SampleT>,
                                   .channelSchema = channels >= 3u ? an::ChannelSchema::Rgb : an::ChannelSchema::Mono,
                                   .colorSpace = an::ColorSpace::Unspecified,
                                   .numericDomain = an::NumericDomain::NormalizedLinear,
                                   .cfa = an::CfaDescriptor{},
                                   .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                   .pixelAspectRatio = 1.0});
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<SampleT>> image = an::Image<SampleT>::createFilled(
        hold->engine(), *descriptor, an::StorageArrangement::Planar, static_cast<SampleT>(0));
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
        if (!access.has_value())
        {
            return std::nullopt;
        }
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                const auto row = access->view().row(channel, y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    (*row)[x] = value(x, y, channel);
                }
            }
        }
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*image)));
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*asset);
}

/// A Float32 image of one value in every sample.
[[nodiscard]] std::shared_ptr<const an::consumer::ImageAsset>
filledAsset(const an::consumer::EngineSession& session, const float value, const std::uint32_t channels = 1u)
{
    std::optional<an::consumer::ImageAsset> asset = assetOf<float>(
        session, 8u, 4u, channels, [value](std::uint32_t, std::uint32_t, std::uint32_t) { return value; });
    return asset.has_value() ? std::make_shared<const an::consumer::ImageAsset>(std::move(*asset)) : nullptr;
}

/// A sixteen-bit grey image of 10 x 4: the first column at zero, the last at full scale, the rest at a thousand times
/// the column - the statistics window's own case.
[[nodiscard]] std::uint16_t columnCode(const std::uint32_t x, std::uint32_t, std::uint32_t)
{
    return static_cast<std::uint16_t>(x == 0u ? 0u : (x == 9u ? 65535u : 1000u * x));
}

/// A task's preview a case hands frames to.
class Frames final : public IReadoutFrameSource
{
public:
    [[nodiscard]] std::optional<Frame> latestFrame() const override
    {
        return m_frame;
    }

    [[nodiscard]] std::shared_ptr<const an::consumer::ImageAsset> original() const override
    {
        return m_original;
    }

    [[nodiscard]] ImageViewControl* target() const override
    {
        return m_target;
    }

    void setFrameListener(std::function<void()> listener) override
    {
        m_listener = std::move(listener);
    }

    void setOriginal(std::shared_ptr<const an::consumer::ImageAsset> original, ImageViewControl* target)
    {
        m_original = std::move(original);
        m_target = target;
    }

    /// Shows a frame and tells the listener.
    void show(std::shared_ptr<const an::consumer::ImageAsset> image, const bool interactive)
    {
        m_frame = Frame{.image = std::move(image), .interactive = interactive, .number = ++m_number};
        if (m_listener)
        {
            m_listener();
        }
    }

private:
    std::optional<Frame> m_frame;
    std::shared_ptr<const an::consumer::ImageAsset> m_original;
    ImageViewControl* m_target = nullptr;
    std::function<void()> m_listener;
    std::uint64_t m_number = 0u;
};

class ReadoutFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_widgetsBefore = an::tests::shellcore::widgetsNow();
        ASSERT_TRUE(m_root.isValid());
        m_session = an::tests::shellcore::openSession();
        ASSERT_NE(m_session, nullptr);
        m_core.openStores();
        m_core.setShell(&m_shell);
        m_core.setEngineSessionProvider(m_session.get());
        m_target = std::make_unique<an::ui::shell::ServiceEventTarget>(
            m_core, an::ui::shell::ServiceEventTarget::Dependencies{});
        m_core.setEventTarget(m_target.get());
        m_context = std::make_unique<an::ui::shell::ApplicationContext>(m_core, m_shell);
        an::ui::clearAppContext();
        an::ui::setAppContext(m_context.get());
    }

    void TearDown() override
    {
        m_controller.reset();
        m_compact.reset();
        m_column.reset();
        m_core.viewService().manager().clear();
        for (const QPointer<QWidget>& dock : m_docks)
        {
            delete dock.data();
        }
        for (const QPointer<ImageViewControl>& view : m_views)
        {
            delete view.data();
        }
        an::tests::shellcore::settle();
        m_core.discardPendingEvents();
        m_core.setEventTarget(nullptr);
        an::ui::clearAppContext();
        m_core.setEngineSessionProvider(nullptr);
        m_core.setShell(nullptr);
        m_core.closeStores();
        const QStringList left = an::tests::shellcore::widgetsLeftSince(m_widgetsBefore);
        EXPECT_TRUE(left.isEmpty()) << "the case left widgets behind: "
                                    << left.join(QStringLiteral(", ")).toStdString();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_session->engineSession();
    }

    /// A view of a document named `name`, made by the views' factory and made active; the case deletes it.
    ImageViewControl* viewOf(an::consumer::ImageAsset&& asset, const QString& name)
    {
        an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
            std::move(asset), an::ui::DocumentNames{.displayName = name, .sourcePath = {}});
        EXPECT_TRUE(document.hasValue());
        if (!document.hasValue())
        {
            return nullptr;
        }
        an::ui::IViewFactory& factory = m_core.viewService();
        ImageViewControl* view = factory.createNewImageViewControlFromImage(
            nullptr, std::move(*document), nullptr, "made", ViewControlMode::standard, nullptr, false, false);
        EXPECT_NE(view, nullptr);
        if (view != nullptr)
        {
            m_views.emplace_back(view);
            m_docks.emplace_back(view->getDockWidget());
            m_core.viewService().setActiveImageViewControl(view);
        }
        return view;
    }

    ImageViewControl* filledView(const QString& name, const float value, const std::uint32_t channels = 1u)
    {
        std::optional<an::consumer::ImageAsset> asset =
            assetOf<float>(session(), 8u, 4u, channels, [value](std::uint32_t, std::uint32_t, std::uint32_t channel) {
                return value + 0.1f * static_cast<float>(channel);
            });
        EXPECT_TRUE(asset.has_value());
        return asset.has_value() ? viewOf(std::move(*asset), name) : nullptr;
    }

    ImageViewControl* columnView(const QString& name)
    {
        std::optional<an::consumer::ImageAsset> asset = assetOf<std::uint16_t>(session(), 10u, 4u, 1u, columnCode);
        EXPECT_TRUE(asset.has_value());
        return asset.has_value() ? viewOf(std::move(*asset), name) : nullptr;
    }

    /// The controller over a column of the case's.
    an::ui::shell::ReadoutController& controller()
    {
        if (!m_column)
        {
            m_column = std::make_unique<an::ui::AnReadoutColumn>();
        }
        if (!m_controller)
        {
            m_controller = std::make_unique<an::ui::shell::ReadoutController>(m_core);
            m_controller->setColumn(m_column.get());
        }
        return *m_controller;
    }

    /// Waits until the controller has published `count` measurements and is idle.
    [[nodiscard]] bool publishedAtLeast(const std::uint64_t count)
    {
        return pumpUntil(
            [this, count]() { return m_controller->isIdle() && m_controller->telemetry().published >= count; });
    }

    [[nodiscard]] bool idle()
    {
        return pumpUntil([this]() { return m_controller->isIdle(); });
    }

    QTemporaryDir m_root;
    ScopedStorageRoot m_scopedRoot{m_root.path()};
    std::unique_ptr<SessionProvider> m_session;
    ApplicationCore m_core;
    RecordingShell m_shell;
    std::unique_ptr<an::ui::shell::ServiceEventTarget> m_target;
    std::unique_ptr<an::ui::shell::ApplicationContext> m_context;
    std::unique_ptr<an::ui::AnReadoutColumn> m_column;
    std::unique_ptr<an::ui::AnCompactReadout> m_compact;
    std::unique_ptr<an::ui::shell::ReadoutController> m_controller;
    std::vector<QPointer<ImageViewControl>> m_views;
    std::vector<QPointer<QWidget>> m_docks;
    QSet<QWidget*> m_widgetsBefore;
};

/// The controller's cases, the cards' that need a real view or the statistics window, and the time of an update.
class ReadoutController : public ReadoutFixture
{
};

class StatisticsCard : public ReadoutFixture
{
};

class AstrometryCard : public ReadoutFixture
{
};

class Timing : public ReadoutFixture
{
};

class ReadoutSummaries : public ReadoutFixture
{
};

} // namespace

// The readout follows the active view: its name, its facts and its figures; another view made active is followed, and
// with every view gone the readout shows nothing.
TEST_F(ReadoutController, FollowsTheActiveView)
{
    ImageViewControl* first = filledView(QStringLiteral("first"), 0.25f);
    ASSERT_NE(first, nullptr);
    an::ui::shell::ReadoutController& readout = controller();
    EXPECT_EQ(readout.followedView(), first);
    ASSERT_TRUE(publishedAtLeast(1u));
    EXPECT_EQ(m_column->viewName(), QStringLiteral("first"));
    EXPECT_EQ(m_column->imageCard()->value(QStringLiteral("Dimensions")), QStringLiteral("8 × 4"));
    ASSERT_TRUE(readout.shownFigures().has_value());
    EXPECT_DOUBLE_EQ(m_column->statisticsCard()->figure(ReadoutStatistic::Mean), 0.25);
    EXPECT_EQ(m_column->histogramCard()->summary(), QStringLiteral("Grayscale"));
    EXPECT_FALSE(readout.isStale());

    ImageViewControl* second = filledView(QStringLiteral("second"), 0.5f, 3u);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(readout.followedView(), second);
    EXPECT_EQ(m_column->viewName(), QStringLiteral("second"));
    EXPECT_TRUE(readout.isStale()) << "the figures before stay, dimmed, until the new ones land";
    ASSERT_TRUE(publishedAtLeast(2u));
    EXPECT_FALSE(readout.isStale());
    EXPECT_TRUE(m_column->statisticsCard()->showsTable());
    EXPECT_DOUBLE_EQ(m_column->statisticsCard()->figure(ReadoutStatistic::Mean, 1u), static_cast<double>(0.5f + 0.1f));
    EXPECT_EQ(m_column->histogramCard()->summary(), QStringLiteral("RGB"));

    m_core.viewService().setActiveImageViewControl(first);
    EXPECT_EQ(readout.followedView(), first);
    ASSERT_TRUE(publishedAtLeast(3u));
    EXPECT_DOUBLE_EQ(m_column->statisticsCard()->figure(ReadoutStatistic::Mean), 0.25);

    m_core.viewService().removeViewControl(second, false);
    EXPECT_EQ(readout.followedView(), first);
    m_core.viewService().removeViewControl(first, false);
    EXPECT_EQ(readout.followedView(), nullptr);
    EXPECT_TRUE(m_column->viewName().isEmpty());
    EXPECT_FALSE(readout.shownFigures().has_value());
}

// A burst of changes within the debounce makes one update and one measurement; the channel the view shows is shown at
// once with nothing measured.
TEST_F(ReadoutController, MeasuresOncePerBurstOfChanges)
{
    std::optional<an::consumer::ImageAsset> asset =
        assetOf<float>(session(), 8u, 4u, 3u, [](const std::uint32_t x, std::uint32_t, const std::uint32_t channel) {
            return channel == 0u ? 0.25f : static_cast<float>(x) / 8.0f + 0.05f * static_cast<float>(channel);
        });
    ASSERT_TRUE(asset.has_value());
    ImageViewControl* view = viewOf(std::move(*asset), QStringLiteral("burst"));
    ASSERT_NE(view, nullptr);
    an::ui::shell::ReadoutController& readout = controller();
    ASSERT_TRUE(publishedAtLeast(1u));
    const an::ui::shell::ReadoutTelemetry before = readout.telemetry();

    for (int change = 0; change < 5; ++change)
    {
        view->notifyImageUpdated();
    }
    ASSERT_TRUE(publishedAtLeast(before.published + 1u));
    an::ui::shell::ReadoutTelemetry after = readout.telemetry();
    EXPECT_EQ(after.updates, before.updates + 1u);
    EXPECT_EQ(after.started, before.started + 1u);

    ASSERT_NE(readout.shownCounts(), nullptr);
    ASSERT_EQ(readout.shownCounts()->channels.size(), 3u);
    const double green = static_cast<double>(readout.shownCounts()->channels[1].maxValue());
    ASSERT_LT(green, static_cast<double>(readout.shownCounts()->channels[0].maxValue()));
    // The channel chosen as a person chooses it: in the view's channel box.
    QComboBox* channels = nullptr;
    for (QComboBox* box : view->findChildren<QComboBox*>())
    {
        if (box->accessibleName() == QStringLiteral("View Channel"))
        {
            channels = box;
        }
    }
    ASSERT_NE(channels, nullptr);
    const int greenIndex = channels->findData(static_cast<int>(ImageCompositionMode::g_mode));
    ASSERT_GE(greenIndex, 0);
    channels->setCurrentIndex(greenIndex);
    ASSERT_EQ(view->displayedComposition(), ImageCompositionMode::g_mode);
    ASSERT_TRUE(idle());
    const an::ui::shell::ReadoutTelemetry channel = readout.telemetry();
    EXPECT_EQ(channel.started, after.started) << "the channel shown measures nothing";
    EXPECT_EQ(channel.presented, after.presented + 1u);
    EXPECT_DOUBLE_EQ(m_column->histogramCard()->histogramWidget()->getMaxDisplayedHistogramValue(), green)
        << "the green channel's curve alone is drawn";

    m_column->histogramCard()->resolutionPicker()->setCurrentText(QStringLiteral("10-bit (1024)"));
    ASSERT_TRUE(publishedAtLeast(after.published + 1u));
    EXPECT_EQ(readout.telemetry().started, after.started + 1u) << "another resolution is counted again";
    EXPECT_EQ(readout.shownCounts()->channels[0].bins.size(), 1024u);
}

// A measurement is admitted as a preview that never waits: while every thread of the session is held, it is refused,
// the figures stay as they were, dimmed, and the next change measures again.
TEST_F(ReadoutController, AdmitsAsAPreviewAndRetriesARefusalAtTheNextChange)
{
    ImageViewControl* view = filledView(QStringLiteral("refused"), 0.25f);
    ASSERT_NE(view, nullptr);
    an::ui::shell::ReadoutController& readout = controller();
    ASSERT_TRUE(publishedAtLeast(1u));
    const an::ui::shell::ReadoutTelemetry before = readout.telemetry();

    // Nothing else of the view's runs, then every thread of the session is held by an operation.
    an::tests::shellcore::settle();
    std::optional<an::consumer::OperationScope> held;
    {
        an::Result<an::consumer::OperationScope> admitted = session().tryAdmit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply, .maxConcurrency = 0u});
        ASSERT_TRUE(admitted.hasValue()) << admitted.status().message();
        held.emplace(std::move(*admitted));
    }
    view->notifyImageUpdated();
    ASSERT_TRUE(
        pumpUntil([&readout, &before]() { return readout.isIdle() && readout.telemetry().refused > before.refused; }));
    EXPECT_EQ(readout.telemetry().published, before.published);
    EXPECT_TRUE(readout.isStale());
    EXPECT_TRUE(m_column->statisticsCard()->isStale());
    EXPECT_DOUBLE_EQ(m_column->statisticsCard()->figure(ReadoutStatistic::Mean), 0.25) << "nothing blanks";

    held.reset();
    view->notifyImageUpdated();
    ASSERT_TRUE(publishedAtLeast(before.published + 1u));
    EXPECT_FALSE(readout.isStale());
}

// A view closed while its image is measured shows nothing of the measurement and crashes nothing: the ticket no
// longer commits.
TEST_F(ReadoutController, PublishesOnlyACommittingTicket)
{
    ImageViewControl* view = filledView(QStringLiteral("closing"), 0.25f);
    ASSERT_NE(view, nullptr);
    an::ui::shell::ReadoutController& readout = controller();
    ASSERT_TRUE(publishedAtLeast(1u));
    const an::ui::shell::ReadoutTelemetry before = readout.telemetry();

    // The image's writer held: the measurement waits for its lease on its worker.
    std::optional<an::ui::ImageDocumentWriter> writer;
    ASSERT_TRUE(pumpUntil([&writer, view]() {
        an::Result<an::ui::ImageDocumentWriter> taken = view->document().tryWrite();
        if (taken.hasValue())
        {
            writer.emplace(std::move(*taken));
        }
        return writer.has_value();
    }));
    view->notifyImageUpdated();
    ASSERT_TRUE(pumpUntil([&readout, &before]() { return readout.telemetry().started > before.started; }));
    m_core.viewService().removeViewControl(view, false);
    writer.reset();
    ASSERT_TRUE(idle());
    const an::ui::shell::ReadoutTelemetry after = readout.telemetry();
    EXPECT_EQ(after.published, before.published);
    EXPECT_GE(after.discarded, before.discarded + 1u);
    EXPECT_EQ(readout.followedView(), nullptr);
    EXPECT_FALSE(readout.shownFigures().has_value());
}

// Over every kind of update - another view, an image updated, another resolution - the controller's measurements
// call the engine, and none of those calls is made on the GUI thread.
TEST_F(ReadoutController, NeverCallsTheEngineOnTheGuiThread)
{
    ImageViewControl* first = filledView(QStringLiteral("one"), 0.25f);
    an::ui::shell::ReadoutController& readout = controller();
    ASSERT_TRUE(publishedAtLeast(1u));
    ImageViewControl* second = filledView(QStringLiteral("two"), 0.5f, 3u);
    ASSERT_TRUE(publishedAtLeast(2u));
    second->notifyImageUpdated();
    ASSERT_TRUE(publishedAtLeast(3u));
    m_column->histogramCard()->resolutionPicker()->setCurrentText(QStringLiteral("6-bit (64)"));
    ASSERT_TRUE(publishedAtLeast(4u));
    m_core.viewService().setActiveImageViewControl(first);
    ASSERT_TRUE(publishedAtLeast(5u));
    const an::ui::shell::ReadoutTelemetry telemetry = readout.telemetry();
    EXPECT_GE(telemetry.engineCalls, 5u * 4u);
    EXPECT_EQ(telemetry.engineCallsOnGuiThread, 0u);
}

// In a task the settled frames are measured, with the original's figures beside them for the change against them; an
// interactive frame leaves the figures as they were, dimmed, and measures nothing; the original is measured once.
TEST_F(ReadoutController, InATaskItMeasuresSettledFramesAgainstTheOriginal)
{
    ImageViewControl* target = filledView(QStringLiteral("target"), 0.25f);
    ASSERT_NE(target, nullptr);
    an::ui::shell::ReadoutController& readout = controller();
    ASSERT_TRUE(publishedAtLeast(1u));
    m_compact = std::make_unique<an::ui::AnCompactReadout>();
    readout.setCompactReadout(m_compact.get());

    Frames frames;
    frames.setOriginal(filledAsset(session(), 0.25f), target);
    readout.followTask(&frames);
    EXPECT_TRUE(readout.followsTask());
    frames.show(filledAsset(session(), 0.5f), false);
    ASSERT_TRUE(publishedAtLeast(2u));
    ASSERT_TRUE(readout.originalFigures().has_value());
    EXPECT_DOUBLE_EQ(readout.originalFigures()->channels[0].mean, 0.25);
    const std::vector<an::ui::ReadoutCell>& cells = m_compact->cells()->cells();
    ASSERT_EQ(cells.size(), 5u);
    EXPECT_EQ(cells[2].value, QStringLiteral("0.5000"));
    EXPECT_EQ(cells[2].note, QStringLiteral("Δ +0.2500"));
    EXPECT_EQ(m_compact->modeText(), QStringLiteral("Grayscale · 8-bit"));
    EXPECT_FALSE(m_compact->isStale());

    const an::ui::shell::ReadoutTelemetry settled = readout.telemetry();
    frames.show(filledAsset(session(), 0.9f), true);
    EXPECT_TRUE(m_compact->isStale());
    EXPECT_TRUE(readout.isStale());
    an::tests::shellcore::settle();
    ASSERT_TRUE(idle());
    EXPECT_EQ(readout.telemetry().started, settled.started) << "an interactive frame is not measured";
    EXPECT_EQ(readout.telemetry().interactiveFrames, settled.interactiveFrames + 1u);
    EXPECT_EQ(m_compact->cells()->cells()[2].value, QStringLiteral("0.5000"));

    const std::uint64_t callsBefore = readout.telemetry().engineCalls;
    frames.show(filledAsset(session(), 0.125f), false);
    ASSERT_TRUE(publishedAtLeast(settled.published + 1u));
    EXPECT_FALSE(m_compact->isStale());
    EXPECT_EQ(m_compact->cells()->cells()[2].note, QStringLiteral("Δ −0.1250"));
    // The lease, the admission, the counts and the statistics of the frame: the original's figures are kept.
    EXPECT_EQ(readout.telemetry().engineCalls, callsBefore + 4u);

    readout.followTask(nullptr);
    EXPECT_FALSE(readout.followsTask());
    EXPECT_EQ(readout.followedView(), target);
}

// The Statistics card's figures equal the statistics window's for the same image, at each of the card's three ranges.
TEST_F(StatisticsCard, FiguresEqualTheStatisticsWindowsForTheSameImageAndRange)
{
    ImageViewControl* view = columnView(QStringLiteral("columns"));
    ASSERT_NE(view, nullptr);
    an::ui::shell::ReadoutController& readout = controller();
    ASSERT_TRUE(publishedAtLeast(1u));
    auto* window = new ImageStatisticsControl(view);
    ASSERT_TRUE(pumpUntil([window]() { return window->isIdle(); }));
    ASSERT_TRUE(window->report().has_value());
    ASSERT_TRUE(readout.shownFigures().has_value());
    const an::ui::ImageStatisticsChannel& measured = window->report()->channels[0];
    EXPECT_EQ(readout.shownFigures()->channels[0].count, measured.count);

    an::ui::AnStatisticsCard& card = *m_column->statisticsCard();
    const std::array<StatisticsScale, 3> scales{StatisticsScale::Normalized, StatisticsScale::Bits8,
                                                StatisticsScale::Bits16};
    for (std::size_t index = 0u; index < scales.size(); ++index)
    {
        card.rangePicker()->segmentButton(static_cast<int>(index))->click();
        for (const ReadoutStatistic statistic : an::ui::kReadoutStatistics)
        {
            EXPECT_DOUBLE_EQ(card.figure(statistic), an::ui::readoutFigure(measured, statistic, scales[index]))
                << an::ui::readoutStatisticName(statistic).toStdString() << " at range " << index;
        }
    }
    EXPECT_NEAR(card.figure(ReadoutStatistic::Mean), 4500.0, 1e-9) << "the window's own mean, at 16 bits";
    window->close();
}

// The astrometry card's chips act on the view's own overlay actions: a chip turns its overlay on, and the view's
// overlays with it; the view's action toggled as its shortcut toggles it moves the chip.
TEST_F(AstrometryCard, EachChipTogglesItsOverlayOnTheViewAndFollowsItsShortcut)
{
    ImageViewControl* view = filledView(QStringLiteral("overlays"), 0.25f);
    ASSERT_NE(view, nullptr);
    (void)controller();
    QAction* grid = view->findChild<QAction*>(QStringLiteral("astrometryGridAction"));
    QAction* overlays = view->findChild<QAction*>(QStringLiteral("astrometryOverlayAction"));
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(overlays, nullptr);
    an::ui::AnAstrometryCard& card = *m_column->astrometryCard();
    EXPECT_EQ(card.settingsMenu(), view->findChild<QMenu*>(QStringLiteral("astrometrySettingsMenu")));
    for (std::size_t index = 0u; index < an::ui::kAstrometryOverlays; ++index)
    {
        EXPECT_TRUE(card.overlayChip(static_cast<an::ui::AstrometryOverlay>(index))->isEnabled()) << index;
    }
    an::ui::AnChip* chip = card.overlayChip(an::ui::AstrometryOverlay::Grid);
    const bool gridWas = grid->isChecked();
    overlays->setChecked(false);
    if (gridWas)
    {
        grid->setChecked(false);
    }
    EXPECT_FALSE(chip->isChecked());
    chip->click();
    EXPECT_TRUE(grid->isChecked());
    EXPECT_TRUE(overlays->isChecked()) << "an overlay turned on shows only while the view draws its overlays";
    grid->toggle();
    EXPECT_FALSE(chip->isChecked());
}

// The folded cards and the collapsed column are kept in the global settings store and handed to the next column.
TEST_F(ReadoutController, RemembersTheFoldsAndTheCollapse)
{
    (void)controller();
    EXPECT_FALSE(m_core.globalSettings()
                     ->selectOptional(QString::fromLatin1(SettingsKeys::INTERFACE_READOUT_CARDS))
                     .has_value());
    m_column->statisticsCard()->header()->click();
    m_column->collapseButton()->click();
    EXPECT_EQ(m_core.globalSettings()->select(QString::fromLatin1(SettingsKeys::INTERFACE_READOUT_CARDS)),
              QStringLiteral("statistics"));
    EXPECT_EQ(m_core.globalSettings()->select(QString::fromLatin1(SettingsKeys::INTERFACE_READOUT_COLLAPSED)),
              QStringLiteral("true"));

    m_controller.reset();
    m_column.reset();
    an::ui::shell::ReadoutController& next = controller();
    (void)next;
    EXPECT_TRUE(m_column->statisticsCard()->isFolded());
    EXPECT_FALSE(m_column->imageCard()->isFolded());
    EXPECT_TRUE(m_column->isCollapsed());
    m_column->setWindowSize(QSize(1536, 700));
    EXPECT_FALSE(m_column->imageCard()->isFolded()) << "a remembered line is the person's, whatever the window";

    // Every card open again is remembered too: an empty line, not the absence of one.
    m_column->statisticsCard()->header()->click();
    ASSERT_TRUE(m_core.globalSettings()
                    ->selectOptional(QString::fromLatin1(SettingsKeys::INTERFACE_READOUT_CARDS))
                    .has_value());
    m_controller.reset();
    m_column.reset();
    (void)controller();
    m_column->setWindowSize(QSize(1536, 700));
    EXPECT_FALSE(m_column->statisticsCard()->isFolded());
    EXPECT_FALSE(m_column->imageCard()->isFolded());
}

// ---- What the readout writes

// A solution is read out as the astrometry window writes its centre, with the scale to three decimals, the rotation
// to two, the distortion's orders and the solver; no solution reads out nothing.
TEST_F(ReadoutSummaries, ASolutionIsReadOutAsTheCardShowsIt)
{
    const double scale = 0.772 / 3600.0;
    an::SipDistortionRequest sip;
    sip.referencePixel = an::PixelCoordinate{.x = 500.5, .y = 400.5};
    sip.forwardX.order = 4u;
    sip.forwardY.order = 4u;
    sip.forwardX.coefficients[2][0] = 1e-7;
    an::Result<an::SipDistortion> distortion = an::SipDistortion::create(sip);
    ASSERT_TRUE(distortion.hasValue()) << distortion.status().message();
    an::WcsSolutionRequest request;
    request.referencePixel = an::PixelCoordinate{.x = 500.5, .y = 400.5};
    request.referencePoint = an::SkyCoordinate{.rightAscensionDegrees = 201.365, .declinationDegrees = -43.019};
    request.linear = an::WcsLinearTransform{.cd11 = -scale, .cd12 = 0.0, .cd21 = 0.0, .cd22 = scale};
    request.imageWidth = 1000u;
    request.imageHeight = 800u;
    request.distortion = *distortion;
    an::Result<an::WcsSolution> solution = an::WcsSolution::create(request);
    ASSERT_TRUE(solution.hasValue()) << solution.status().message();

    const an::ui::AstrometryReadout readout =
        an::ui::shell::astrometryReadout(*solution, QStringLiteral("Internal Gaia"));
    EXPECT_TRUE(readout.solved);
    const an::Result<an::SkyCoordinate> centre = solution->fieldCenter();
    ASSERT_TRUE(centre.hasValue());
    EXPECT_EQ(readout.rightAscension, an::ui::formatAstrometryRa(centre->rightAscensionDegrees));
    EXPECT_EQ(readout.declination, an::ui::formatAstrometryDec(centre->declinationDegrees));
    EXPECT_EQ(readout.scale, QStringLiteral("0.772"));
    EXPECT_EQ(readout.rotation, QString::number(solution->orientationDegrees(), 'f', 2));
    EXPECT_EQ(readout.distortion, QStringLiteral("SIP 4/4"));
    EXPECT_EQ(readout.solver, QStringLiteral("Internal Gaia"));
    EXPECT_EQ(an::ui::shell::compactAstrometryLine(readout), QStringLiteral("0.772″/px · SIP 4/4"));

    const an::ui::AstrometryReadout none = an::ui::shell::astrometryReadout(std::nullopt, QStringLiteral("Gaia"));
    EXPECT_FALSE(none.solved);
    EXPECT_TRUE(none.solver.isEmpty());
    EXPECT_TRUE(an::ui::shell::compactAstrometryLine(none).isEmpty());
}

// An image's facts: its size, the status line's name of its samples, the memory they take, its channels, and the
// folded card's one line.
TEST_F(ReadoutSummaries, AnImagesFactsAreItsDescription)
{
    an::Result<an::consumer::SessionHold> hold = session().hold();
    ASSERT_TRUE(hold.hasValue());
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(4432u, 3840u, 3u, hold->engine().limits());
    ASSERT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::UInt16,
                                                               .channelSchema = an::ChannelSchema::Rgb,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = an::CfaDescriptor{},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    ASSERT_TRUE(descriptor.has_value());
    const an::ui::ImageFactsReadout facts = an::ui::shell::imageFactsReadout(*descriptor);
    EXPECT_EQ(facts.dimensions, QStringLiteral("4432 × 3840"));
    EXPECT_EQ(facts.format, QStringLiteral("16 bit unsigned integer"));
    EXPECT_EQ(facts.sizeInMemory, QStringLiteral("97.38 MB"));
    EXPECT_EQ(facts.channels, QStringLiteral("RGB"));
    EXPECT_EQ(facts.summary, QStringLiteral("4432x3840 · 16 bit uint · 97.38 MB"));
}

// ---- The time of an update

// The GUI thread's time in an update of the readout - the update that starts a measurement, the publication of what
// it measured and the paint of the two cards it changed, the histogram and the statistics - over a 4000 x 3000 RGB
// image, the median of seven rounds, in the column of the floor display (1536 x 864: 794 px under the top bar and over
// the status line). Two images: a sky - a background peak with noise and stars, the histogram an astronomical image
// gives - and an image whose histogram is flat, the most a plot ever fills. The paint of the whole column is printed
// beside each. Measured, never asserted, and only when AFTERNIGHT_TEST_TIMINGS=1 asks for it; the budget is 2 ms.
TEST_F(Timing, ReadoutUpdate)
{
    if (qEnvironmentVariable("AFTERNIGHT_TEST_TIMINGS") != QStringLiteral("1"))
    {
        GTEST_SKIP() << "a time is measured only when AFTERNIGHT_TEST_TIMINGS=1 asks for it";
    }
    const auto sky = [](const std::uint32_t x, const std::uint32_t y, const std::uint32_t c) {
        const std::uint32_t hash = (x * 73856093u) ^ (y * 19349663u) ^ (c * 83492791u);
        if (hash % 997u == 0u)
        {
            return 0.6f + static_cast<float>(hash % 350u) / 1000.0f;
        }
        return 0.08f + (static_cast<float>(hash % 4001u) / 4000.0f - 0.5f) * 0.04f;
    };
    const auto flat = [](const std::uint32_t x, const std::uint32_t y, const std::uint32_t c) {
        return static_cast<float>((x * 7u + y * 13u + c * 101u) % 1000u) / 1000.0f;
    };
    const auto median = [](std::vector<double> values) {
        std::sort(values.begin(), values.end());
        return values[values.size() / 2u];
    };
    an::ui::shell::ReadoutController& readout = controller();
    m_column->resize(an::ui::parts::kReadoutColumnWidth, 794);
    m_column->setAttribute(Qt::WA_DontShowOnScreen, true);
    m_column->show();

    for (const auto& [label, value] :
         {std::pair<const char*, std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)>>{"sky", sky},
          std::pair<const char*, std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)>>{"flat", flat}})
    {
        std::optional<an::consumer::ImageAsset> asset = assetOf<float>(session(), 4000u, 3000u, 3u, value);
        ASSERT_TRUE(asset.has_value());
        const std::uint64_t before = readout.telemetry().published;
        ImageViewControl* view = viewOf(std::move(*asset), QString::fromLatin1(label));
        ASSERT_NE(view, nullptr);
        ASSERT_TRUE(publishedAtLeast(before + 1u));

        std::vector<double> updates;
        std::vector<double> publications;
        std::vector<double> paints;
        std::vector<double> histograms;
        std::vector<double> columns;
        for (int round = 0; round < 7; ++round)
        {
            const std::uint64_t published = readout.telemetry().published;
            view->notifyImageUpdated();
            ASSERT_TRUE(publishedAtLeast(published + 1u));
            QElapsedTimer paint;
            paint.start();
            m_column->histogramCard()->repaint();
            const qint64 histogram = paint.nsecsElapsed();
            m_column->statisticsCard()->repaint();
            paints.push_back(static_cast<double>(paint.nsecsElapsed()) / 1.0e6);
            histograms.push_back(static_cast<double>(histogram) / 1.0e6);
            QElapsedTimer column;
            column.start();
            m_column->repaint();
            columns.push_back(static_cast<double>(column.nsecsElapsed()) / 1.0e6);
            updates.push_back(static_cast<double>(readout.telemetry().lastUpdateNanoseconds) / 1.0e6);
            publications.push_back(static_cast<double>(readout.telemetry().lastPublishNanoseconds) / 1.0e6);
        }
        const double update = median(updates);
        const double publication = median(publications);
        const double painted = median(paints);
        const double histogramPaint = median(histograms);
        const double wholeColumn = median(columns);
        std::printf("\nReadout update, %s, 4000 x 3000 RGB Float32, median of 7 rounds: update %.3f ms, publication "
                    "%.3f ms, paint of the two cards %.3f ms (the histogram's %.3f ms), GUI thread %.3f ms (budget 2 "
                    "ms); the whole column's paint %.3f ms\n",
                    label, update, publication, painted, histogramPaint, update + publication + painted, wholeColumn);
        const std::string prefix = std::string(label) + ".";
        ::testing::Test::RecordProperty(prefix + "updateMs", std::to_string(update));
        ::testing::Test::RecordProperty(prefix + "publicationMs", std::to_string(publication));
        ::testing::Test::RecordProperty(prefix + "paintMs", std::to_string(painted));
        ::testing::Test::RecordProperty(prefix + "columnPaintMs", std::to_string(wholeColumn));
    }
    EXPECT_EQ(readout.telemetry().engineCallsOnGuiThread, 0u);
}
