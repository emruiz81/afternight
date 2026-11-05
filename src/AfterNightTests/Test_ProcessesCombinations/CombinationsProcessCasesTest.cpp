// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What only one of the combination processes does: the image comparison's modes, split overlay, histograms and lens;
/// the palette composer's grid built off the GUI thread; the profiled recombination's profile, shapes and mask; the
/// NBRGB preview's display modes; the colour mapper's layers and colourized outputs; the channel composition's
/// representations, missing components, sequences and linear match; the blend's filters, transfers, 32-bit addition,
/// stop and its stretch of the target with no blend image; the LRGB combination's refusal of a grey target and a
/// luminance of another representation; and a recombination's samples above one. Work a window does on a worker is told
/// from work on the GUI thread by holding the writer of an image that work reads: the window goes on answering only
/// when the work waits elsewhere.

#include "Test_ProcessesCombinations/CombinationsTestSupport.h"
#include "Test_ControlsViewer/EditHoldingTheWriter.h"

#include "controls/ImageViewControl.h"
#include "controls/ImageViewScrollArea.h"
#include "controls/SequenceContainers.h"
#include "processes/ChannelCompositionProcess.h"
#include "processes/ImageBlendProcess.h"
#include "widgets/waitingspinnerwidget.h"
#include "processes/ImageCompareProcess.h"
#include "processes/NBColorMapperProcess.h"
#include "processes/NarrowbandPaletteComposerProcess.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProfiledStarmaskRecombinationProcess.h"
#include "widgets/HistogramWidget.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/SplitComparisonDisplayModule.h"

#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/IO/StarProfileFile.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Interop/StarProfileDocument.h>

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::combinations;
using an::tests::viewer::Channels;

/// One process's window, for what only that process does.
class CombinationsProcessCase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(sessionWidth());
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    /// The session's width.
    [[nodiscard]] virtual std::uint32_t sessionWidth() const
    {
        return 4u;
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] ProcessTestContext& context() const
    {
        return *m_context;
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    /// A view of a new asset of a representation, added to the context's views.
    template <typename SampleT>
    [[nodiscard]] ViewHandle addedView(const std::string& name, std::uint32_t width, std::uint32_t height,
                                       Channels channels,
                                       const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)>& value)
    {
        std::optional<an::consumer::ImageAsset> asset =
            an::tests::viewer::makeAsset<SampleT>(session(), width, height, channels, value);
        EXPECT_TRUE(asset.has_value());
        if (!asset.has_value())
        {
            return {};
        }
        ViewHandle view = assetView(std::move(*asset), name);
        if (view != nullptr)
        {
            context().addViewControl(view.get());
        }
        return view;
    }

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

/// A process's window over a session one operation wide, where work that holds the only thread while it waits for an
/// image's writer stops everything.
class CombinationsProcessCaseAtWidthOne : public CombinationsProcessCase
{
protected:
    [[nodiscard]] std::uint32_t sessionWidth() const override
    {
        return 1u;
    }
};

/// The comparison window over the corpus scene and its second image, shown with its preview on and its views settled.
struct OpenedComparison
{
    Targets targets;
    WindowHandle window;
    ImageCompareProcess* compare = nullptr;
    ImageViewControl* preview = nullptr;
    ImageViewWidget* widget = nullptr;
};

[[nodiscard]] OpenedComparison openComparison(ProcessTestContext& context)
{
    const CombinationsProcess& under = processNamed("ImageCompare");
    OpenedComparison opened;
    opened.targets = openScene(context, under);
    EXPECT_NE(opened.targets.target, nullptr);
    if (opened.targets.target == nullptr)
    {
        return opened;
    }
    context.addViewControl(opened.targets.target.get());
    opened.window = under.open(opened.targets.target.get());
    opened.compare = dynamic_cast<ImageCompareProcess*>(opened.window.get());
    EXPECT_NE(opened.compare, nullptr);
    if (opened.compare == nullptr)
    {
        return opened;
    }
    opened.window->show();
    under.configure(*opened.window, opened.targets, "-");
    EXPECT_TRUE(settleWindow(under, *opened.window));
    opened.preview = opened.compare->previewView();
    EXPECT_NE(opened.preview, nullptr) << "the comparison's preview is always on";
    opened.widget = opened.preview != nullptr ? opened.preview->getViewWidget() : nullptr;
    EXPECT_NE(opened.widget, nullptr);
    if (opened.preview != nullptr)
    {
        EXPECT_TRUE(settleView(*opened.preview));
    }
    return opened;
}

/// Waits for the comparison's split overlay and histograms to land.
[[nodiscard]] bool settleComparison(const ImageCompareProcess& compare)
{
    return pumpUntil([&compare]() { return !compare.isOverlayPending() && compare.histogramsIdle(); });
}

/// Holds every thread of the global pool until let go, so work queued meanwhile starts only then.
class PoolHeld
{
public:
    PoolHeld()
    {
        QThreadPool* pool = QThreadPool::globalInstance();
        const int threads = (std::max)(1, pool->maxThreadCount());
        for (int index = 0; index < threads; ++index)
        {
            pool->start([state = m_state]() {
                std::unique_lock lock(state->mutex);
                state->changed.wait(lock, [&state]() { return state->released; });
            });
        }
    }

    ~PoolHeld()
    {
        letGo();
        QThreadPool::globalInstance()->waitForDone();
    }

    PoolHeld(const PoolHeld&) = delete;
    PoolHeld& operator=(const PoolHeld&) = delete;

    void letGo()
    {
        {
            const std::lock_guard lock(m_state->mutex);
            m_state->released = true;
        }
        m_state->changed.notify_all();
    }

private:
    struct State
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool released = false;
    };
    std::shared_ptr<State> m_state = std::make_shared<State>();
};

/// Sends a mouse event to a widget.
void mouse(QWidget& widget, QEvent::Type type, const QPoint& position, Qt::MouseButton button)
{
    QMouseEvent event(type, QPointF(position), widget.mapToGlobal(position), button,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : button, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &event);
}

/// Writes a first-generation profile of stars at positions, each of a radius; fails the case when it cannot.
[[nodiscard]] QString writeProfile(const an::consumer::EngineSession& session, const QTemporaryDir& directory,
                                   const std::vector<std::pair<double, double>>& positions, double radius)
{
    an::StarProfileFile profile;
    profile.generation = an::StarProfileGeneration::Version1;
    an::StarProfileV1 payload;
    for (const auto& [x, y] : positions)
    {
        an::StarProfileV1Star star;
        star.x = x;
        star.y = y;
        star.radius = radius;
        star.meanRadius = radius;
        star.intensity = 0.5;
        star.quality = 1.0;
        payload.stars.push_back(star);
    }
    profile.payload = payload;
    const QString path = directory.filePath(QStringLiteral("stars-%1.anspv1").arg(positions.size()));
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    EXPECT_TRUE(scope.has_value());
    if (!scope.has_value())
    {
        return {};
    }
    const std::string destination = path.toStdString();
    an::Result<an::StarProfileWriteReport> written = an::writeStarProfileFile(
        scope->engine(), scope->execution(), an::StarProfileWriteRequest{.destinationPath = destination}, profile);
    EXPECT_TRUE(written.hasValue()) << written.status().message();
    return written.hasValue() ? path : QString();
}

/// A colour Float32 asset of one value whose record carries an OBJECT card; fails the case when it cannot be made.
[[nodiscard]] std::optional<an::consumer::ImageAsset>
assetWithObjectCard(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height, float value)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    EXPECT_TRUE(scope.has_value());
    if (!scope.has_value())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> described =
        an::tests::viewer::describeImage(scope->engine(), an::SampleType::Float32, width, height, Channels::Rgb);
    an::Result<an::CodecCard> object = an::CodecCard::create(an::CodecNamespace::Fits, "OBJECT", "'M 42'", "target");
    EXPECT_TRUE(described.has_value() && object.hasValue());
    if (!described.has_value() || !object.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::CodecMetadata> cards =
        an::CodecMetadata::create(an::SourceEncoding{}, std::vector<an::CodecCard>{*object});
    EXPECT_TRUE(cards.hasValue()) << cards.status().message();
    if (!cards.hasValue())
    {
        return std::nullopt;
    }
    const an::ImageMetadata metadata =
        an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{}, an::AstrometricSolution{},
                                  an::Provenance{}, *cards, std::nullopt);
    an::Result<an::Image<float>> image = an::Image<float>::create(
        scope->engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, metadata);
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = image->write();
        EXPECT_TRUE(access.has_value());
        if (!access.has_value())
        {
            return std::nullopt;
        }
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                auto row = access->view().row(c, y);
                EXPECT_TRUE(row.has_value());
                if (!row.has_value())
                {
                    return std::nullopt;
                }
                std::fill(row->begin(), row->end(), value);
            }
        }
    }
    scope.reset();
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*image)));
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*asset)) : std::nullopt;
}

/// Whether a document's record carries the OBJECT card `assetWithObjectCard` gives.
[[nodiscard]] bool carriesObjectCard(const an::ui::ImageDocument& document)
{
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    if (!lease.hasValue())
    {
        return false;
    }
    const an::ImageMetadata& metadata =
        std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, lease->image());
    for (const an::CodecCard& card : metadata.codec().cards())
    {
        if (card.keyword() == "OBJECT" && card.value() == "'M 42'")
        {
            return true;
        }
    }
    return false;
}

/// Applies a profiled recombination over a starless image and stars, with a profile loaded from a path, or none.
[[nodiscard]] std::optional<fixtures::FixtureSamples>
recombineWithProfile(ProcessTestContext& context, const QString& profilePath, std::size_t expectedStars)
{
    const CombinationsProcess& under = processNamed("ProfiledStarmaskRecombination");
    Targets targets = openScene(context, under);
    EXPECT_NE(targets.target, nullptr);
    if (targets.target == nullptr)
    {
        return std::nullopt;
    }
    WindowHandle window = under.open(nullptr);
    auto* profiled = dynamic_cast<ProfiledStarmaskRecombinationProcess*>(window.get());
    EXPECT_NE(profiled, nullptr);
    under.configure(*window, targets, "-");
    setDouble(*window, "softenEdgesValue", 0.0);
    if (!profilePath.isEmpty())
    {
        profiled->loadProfile(profilePath);
        EXPECT_TRUE(pumpUntil([profiled]() { return !profiled->isProfileLoading(); }));
        EXPECT_EQ(profiled->profileStarCount(), expectedStars);
    }
    std::optional<fixtures::FixtureSamples> result = applyAndRead(context, under, *window, targets);
    EXPECT_EQ(context.errorCount(), 0) << context.errorTexts.join(QStringLiteral("; ")).toStdString();
    window.reset();
    dropTargets(context, targets);
    return result;
}

/// A document's writer, taken on the GUI thread and held until the case lets it go - or, so that a case whose GUI
/// thread waits behind it still ends, until the patience runs out, when a thread of its own lets it go and says so.
/// While it is held nothing reads the image, so work that reads it waits where it runs: on a worker the GUI thread goes
/// on turning, and on the GUI thread nothing does.
class HeldWriter
{
public:
    explicit HeldWriter(an::ui::ImageDocumentWriter&& writer)
        : m_writer(std::move(writer)), m_watch([this]() { watch(); })
    {
    }

    ~HeldWriter()
    {
        letGo();
    }

    HeldWriter(const HeldWriter&) = delete;
    HeldWriter& operator=(const HeldWriter&) = delete;

    /// Whether the writer is still held.
    [[nodiscard]] bool held() const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return !m_released;
    }

    /// Whether the writer was let go because the patience ran out, rather than by the case.
    [[nodiscard]] bool releasedAtDeadline() const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_atDeadline;
    }

    /// Lets the writer go, and returns once it has.
    void letGo()
    {
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_letGo = true;
        }
        m_changed.notify_all();
        if (m_watch.joinable())
        {
            m_watch.join();
        }
    }

private:
    void watch()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_changed.wait_for(lock, kPatience, [this]() { return m_letGo; }))
        {
            m_atDeadline = true;
        }
        m_writer.release();
        m_released = true;
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    bool m_letGo = false;
    bool m_released = false;
    bool m_atDeadline = false;
    an::ui::ImageDocumentWriter m_writer;
    std::thread m_watch;
};

/// The writer of a view's document, taken once nothing reads its image; fails the case when it cannot be taken.
[[nodiscard]] std::optional<an::ui::ImageDocumentWriter> writerOf(ImageViewControl& view)
{
    EXPECT_TRUE(settleView(view));
    std::optional<an::ui::ImageDocumentWriter> writer;
    const bool taken = pumpUntil([&view, &writer]() {
        an::Result<an::ui::ImageDocumentWriter> tried = view.document().tryWrite();
        if (!tried.hasValue())
        {
            return false;
        }
        writer.emplace(std::move(*tried));
        return true;
    });
    EXPECT_TRUE(taken) << "the image's writer is free once its view is idle";
    return writer;
}

/// An image blend window whose run asks for its own stop before the blend's body runs, and keeps what the body
/// answered: whether the composition saw the stop, which the framework's answer for a stopped run does not say.
class StopAnsweringBlend final : public ImageBlendProcess
{
public:
    using ImageBlendProcess::ImageBlendProcess;

    /// Whether the body has run.
    [[nodiscard]] bool bodyRan() const noexcept
    {
        return m_ran.load();
    }

    /// What the body answered.
    [[nodiscard]] an::ErrorCode bodyAnswer() const noexcept
    {
        return m_answer.load();
    }

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const ImageBlendParams& params,
                                       an::ui::ExecutionScope& scope) override
    {
        scope.requestStop();
        const an::Status answered = ImageBlendProcess::executeTypedProcessImpl(source, destination, run, params, scope);
        m_answer.store(answered.code());
        m_ran.store(true);
        return answered;
    }

private:
    std::atomic<an::ErrorCode> m_answer{an::ErrorCode::Internal};
    std::atomic<bool> m_ran{false};
};

} // namespace

// ---- The image comparison.

// The comparison compares and changes nothing: Apply copies the target, the window needs no exclusive preview, and
// its mode is one of three.
TEST_F(CombinationsProcessCase, ImageCompareLeavesTheTargetAndSharesThePreview)
{
    const CombinationsProcess& under = processNamed("ImageCompare");
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    const std::optional<fixtures::FixtureSamples> before = documentSamples(targets.target->document());
    WindowHandle window = under.open(targets.target.get());
    auto* compare = dynamic_cast<ImageCompareProcess*>(window.get());
    ASSERT_NE(compare, nullptr);
    EXPECT_FALSE(ImageCompareProcess::requiresExclusiveAccessStatic());
    EXPECT_EQ(compare->compareMode(), an::ui::processes::ImageCompareMode::Toggle);
    under.configure(*window, targets, "-");
    const std::optional<fixtures::FixtureSamples> after = applyAndRead(context(), under, *window, targets);
    ASSERT_TRUE(before.has_value() && after.has_value());
    EXPECT_TRUE(fixtures::sameSamples(*before, *after));
    window.reset();
    dropTargets(context(), targets);
}

// The second selector offers the views of the target's size and no other.
TEST_F(CombinationsProcessCase, ImageCompareOffersOnlyViewsOfTheTargetsSize)
{
    const CombinationsProcess& under = processNamed("ImageCompare");
    ViewHandle smaller = addedView<float>("smaller", 7u, 5u, Channels::Rgb,
                                          [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.3f; });
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    WindowHandle window = under.open(targets.target.get());
    auto* selector = widgetOf<QComboBox>(*window, "secondImageSelector");
    ASSERT_NE(selector, nullptr);
    EXPECT_GE(selector->findText(QStringLiteral("second")), 0);
    EXPECT_LT(selector->findText(QStringLiteral("smaller")), 0);
    EXPECT_LT(selector->findText(QStringLiteral("scene")), 0) << "the target is not its own second image";
    window.reset();
    dropTargets(context(), targets);
    dropView(context(), smaller);
}

// In the toggle mode the second image is the preview's alternate, which the preview's switch shows; a split keeps it
// set with its rendering off, and the switch in a split swaps the sides instead.
TEST_F(CombinationsProcessCase, ImageCompareTogglesTheSecondImageAsTheAlternate)
{
    OpenedComparison opened = openComparison(context());
    ASSERT_NE(opened.widget, nullptr);
    ImageViewControl* second = opened.targets.side("second");
    ASSERT_NE(second, nullptr);
    ASSERT_TRUE(opened.preview->hasAlternateImage());
    EXPECT_EQ(opened.preview->alternateAsset(), second->document().asset());

    opened.compare->setCompareMode(an::ui::processes::ImageCompareMode::HorizontalSplit);
    ASSERT_TRUE(settleComparison(*opened.compare));
    EXPECT_TRUE(opened.preview->hasAlternateImage()) << "the alternate stays set for the toolbar's switch";
    EXPECT_FALSE(opened.widget->isAlternateImageRenderEnabled());
    ASSERT_TRUE(opened.compare->splitModule()->isDisplayModuleEnabled());
    EXPECT_FALSE(opened.compare->splitModule()->isSwapped());
    opened.widget->enableAlternateImageRendering(true);
    EXPECT_TRUE(opened.compare->splitModule()->isSwapped()) << "the switch swaps the sides";
    EXPECT_FALSE(opened.widget->isAlternateImageRenderEnabled()) << "the split module keeps the display";

    opened.compare->setCompareMode(an::ui::processes::ImageCompareMode::Toggle);
    EXPECT_FALSE(opened.compare->splitModule()->isDisplayModuleEnabled());
    EXPECT_FALSE(opened.compare->splitModule()->isSwapped());
    EXPECT_EQ(opened.preview->alternateAsset(), second->document().asset());
    opened.window.reset();
    dropTargets(context(), opened.targets);
}

// A split shows the second image presented on a worker as the preview presents the first - the presentation lands
// only while it is the latest asked for - and turning the split off clears it; the second selector set to no image
// clears it too.
TEST_F(CombinationsProcessCase, ImageCompareSplitPresentsTheSecondImageOffTheGuiThread)
{
    OpenedComparison opened = openComparison(context());
    ASSERT_NE(opened.widget, nullptr);
    an::ui::SplitComparisonDisplayModule* split = opened.compare->splitModule();
    ASSERT_NE(split, nullptr);
    EXPECT_FALSE(split->hasOverlayImage());
    EXPECT_EQ(opened.compare->overlaysShown(), 0u);

    opened.compare->setCompareMode(an::ui::processes::ImageCompareMode::VerticalSplit);
    EXPECT_EQ(split->orientation(), an::ui::SplitOrientation::Vertical);
    ASSERT_TRUE(settleComparison(*opened.compare));
    EXPECT_TRUE(split->hasOverlayImage());
    EXPECT_EQ(opened.compare->overlaysShown(), 1u);

    // Two requests in a row: the first is superseded, and at most one more presentation lands.
    const ImageViewMode mode = opened.widget->getViewMode();
    opened.widget->setViewMode(mode == ImageViewMode::AUTOSTRETCH_MODE ? ImageViewMode::LINEAR_MODE
                                                                       : ImageViewMode::AUTOSTRETCH_MODE);
    opened.widget->setViewMode(mode);
    ASSERT_TRUE(settleComparison(*opened.compare));
    EXPECT_TRUE(split->hasOverlayImage());
    EXPECT_LE(opened.compare->overlaysShown(), 3u);

    selectView(*opened.window, "secondImageSelector", "");
    ASSERT_TRUE(settleComparison(*opened.compare));
    EXPECT_FALSE(split->hasOverlayImage()) << "no second image, no overlay";
    selectView(*opened.window, "secondImageSelector", "second");
    ASSERT_TRUE(settleComparison(*opened.compare));
    EXPECT_TRUE(split->hasOverlayImage());

    opened.compare->setCompareMode(an::ui::processes::ImageCompareMode::Toggle);
    EXPECT_FALSE(split->hasOverlayImage());
    EXPECT_FALSE(split->isDisplayModuleEnabled());
    opened.window.reset();
    dropTargets(context(), opened.targets);
}

// The split's presentation reads the second image under a lease taken before it asks to be admitted. An edit of the
// second image's view holds its writer and then asks to be admitted, so at a width of one a presentation admitted
// first would hold the only thread while it waited for the writer, and the edit would wait for the thread.
TEST_F(CombinationsProcessCaseAtWidthOne, ImageCompareSplitWaitingForAnEditsWriterHoldsNoThreadTheEditNeeds)
{
    OpenedComparison opened = openComparison(context());
    ASSERT_NE(opened.widget, nullptr);
    ASSERT_FALSE(opened.targets.sides.empty());
    ImageViewControl* second = opened.targets.sides.front().view.get();
    ASSERT_NE(second, nullptr);
    an::ui::SplitComparisonDisplayModule* split = opened.compare->splitModule();
    ASSERT_NE(split, nullptr);

    an::tests::viewer::EditHoldingTheWriter edit(*second);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    opened.compare->setCompareMode(an::ui::processes::ImageCompareMode::VerticalSplit);
    // The mode also asks for work over the target, which is admitted and ends; the split's own presentation waits.
    pumpQuietly();
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the edit waited for the thread the split held while the split waited for the edit's writer";

    ASSERT_TRUE(settleComparison(*opened.compare));
    EXPECT_TRUE(split->hasOverlayImage());
}

// In a split a right click on the preview swaps the sides, a drag of the splitter moves it, and a double click on it
// puts it back in the middle; the splitter's cursor is shown while the pointer is near it and restored when it leaves.
TEST_F(CombinationsProcessCase, ImageCompareSplitFollowsTheMouse)
{
    OpenedComparison opened = openComparison(context());
    ASSERT_NE(opened.widget, nullptr);
    opened.compare->setCompareMode(an::ui::processes::ImageCompareMode::HorizontalSplit);
    ASSERT_TRUE(settleComparison(*opened.compare));
    an::ui::SplitComparisonDisplayModule* split = opened.compare->splitModule();
    ASSERT_TRUE(split->hasOverlayImage());
    const int width = opened.widget->width();
    const int height = opened.widget->height();
    ASSERT_GT(width, 20);
    ASSERT_GT(height, 20);

    // A right click swaps.
    mouse(*opened.widget, QEvent::MouseButtonPress, QPoint(width / 4, height / 2), Qt::RightButton);
    mouse(*opened.widget, QEvent::MouseButtonRelease, QPoint(width / 4, height / 2), Qt::RightButton);
    EXPECT_TRUE(split->isSwapped());
    mouse(*opened.widget, QEvent::MouseButtonPress, QPoint(width / 4, height / 2), Qt::RightButton);
    mouse(*opened.widget, QEvent::MouseButtonRelease, QPoint(width / 4, height / 2), Qt::RightButton);
    EXPECT_FALSE(split->isSwapped());

    // A drag moves the splitter from the middle to a quarter.
    const QPoint middle(width / 2, height / 2);
    ASSERT_TRUE(split->isNearSplitter(middle));
    mouse(*opened.widget, QEvent::MouseMove, middle, Qt::NoButton);
    EXPECT_EQ(opened.preview->getScrollArea()->viewport()->cursor().shape(), Qt::SplitHCursor);
    mouse(*opened.widget, QEvent::MouseButtonPress, middle, Qt::LeftButton);
    EXPECT_TRUE(split->isDragging());
    mouse(*opened.widget, QEvent::MouseMove, QPoint(width / 4, height / 2), Qt::LeftButton);
    mouse(*opened.widget, QEvent::MouseButtonRelease, QPoint(width / 4, height / 2), Qt::LeftButton);
    EXPECT_FALSE(split->isDragging());
    EXPECT_NEAR(split->splitPosition(), 0.25, 0.05);

    // A double click on it puts it back in the middle.
    const QPoint quarter(static_cast<int>(split->splitPosition() * width), height / 2);
    ASSERT_TRUE(split->isNearSplitter(quarter));
    mouse(*opened.widget, QEvent::MouseButtonDblClick, quarter, Qt::LeftButton);
    EXPECT_DOUBLE_EQ(split->splitPosition(), 0.5);

    // Leaving restores the cursor.
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(opened.widget, &leave);
    EXPECT_NE(opened.preview->getScrollArea()->viewport()->cursor().shape(), Qt::SplitHCursor);
    opened.window.reset();
    dropTargets(context(), opened.targets);
}

// The lens over the second image's side magnifies the second image's presentation, and over the first's the view's
// own image; near the splitter it is cancelled.
TEST_F(CombinationsProcessCase, ImageCompareLensShowsTheImageUnderThePointer)
{
    OpenedComparison opened = openComparison(context());
    ASSERT_NE(opened.widget, nullptr);
    opened.compare->setCompareMode(an::ui::processes::ImageCompareMode::HorizontalSplit);
    ASSERT_TRUE(settleComparison(*opened.compare));
    const int width = opened.widget->width();
    const int height = opened.widget->height();
    ASSERT_GT(width, 20);
    const QPoint onSecond(width * 3 / 4, height / 2);
    const QPoint onFirst(width / 8, height / 2);
    ASSERT_TRUE(opened.compare->splitModule()->isPointOnOverlaySide(onSecond));
    ASSERT_FALSE(opened.compare->splitModule()->isPointOnOverlaySide(onFirst));

    mouse(*opened.widget, QEvent::MouseButtonPress, onSecond, Qt::LeftButton);
    EXPECT_TRUE(opened.compare->lensShowsTheSecondImage());
    mouse(*opened.widget, QEvent::MouseMove, onFirst, Qt::LeftButton);
    EXPECT_FALSE(opened.compare->lensShowsTheSecondImage());
    mouse(*opened.widget, QEvent::MouseMove, onSecond, Qt::LeftButton);
    EXPECT_TRUE(opened.compare->lensShowsTheSecondImage());
    mouse(*opened.widget, QEvent::MouseButtonRelease, onSecond, Qt::LeftButton);
    EXPECT_FALSE(opened.compare->lensShowsTheSecondImage()) << "released, the lens shows the view's image again";

    // Swapped, the first image's side is the second's.
    opened.compare->splitModule()->setSwapped(true);
    mouse(*opened.widget, QEvent::MouseButtonPress, onFirst, Qt::LeftButton);
    EXPECT_TRUE(opened.compare->lensShowsTheSecondImage());
    mouse(*opened.widget, QEvent::MouseButtonRelease, onFirst, Qt::LeftButton);
    opened.window.reset();
    dropTargets(context(), opened.targets);
}

// Each image has a histogram of its own under the preview's histogram buttons: the first counts the preview's image,
// the second - shown in a split, with both labels naming their images - the second image.
TEST_F(CombinationsProcessCase, ImageCompareCountsAHistogramPerImage)
{
    OpenedComparison opened = openComparison(context());
    ASSERT_NE(opened.widget, nullptr);
    ASSERT_NE(opened.compare->primaryHistogram(), nullptr);
    ASSERT_NE(opened.compare->secondaryHistogram(), nullptr);
    ASSERT_TRUE(settleComparison(*opened.compare));
    EXPECT_FALSE(opened.compare->secondaryHistogram()->isVisibleTo(opened.window.get()))
        << "in the toggle mode one histogram is shown";
    auto* primaryLabel = opened.window->findChild<QLabel*>();
    ASSERT_NE(primaryLabel, nullptr);

    opened.compare->setCompareMode(an::ui::processes::ImageCompareMode::HorizontalSplit);
    ASSERT_TRUE(settleComparison(*opened.compare));
    EXPECT_TRUE(opened.compare->secondaryHistogram()->isVisibleTo(opened.window.get()));
    const std::shared_ptr<const an::ui::HistogramDisplayData> first =
        opened.compare->primaryHistogram()->getHistogram();
    const std::shared_ptr<const an::ui::HistogramDisplayData> second =
        opened.compare->secondaryHistogram()->getHistogram();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->channels.size(), 3u);
    EXPECT_EQ(second->channels.size(), 3u);
    EXPECT_NE(first->channels.front().bins, second->channels.front().bins) << "two images, two counts";
    bool labelled = false;
    for (const QLabel* label : opened.window->findChildren<QLabel*>())
    {
        labelled = labelled || label->text() == QStringLiteral("Image 2: second");
    }
    EXPECT_TRUE(labelled);
    // The resolution buttons drive both.
    setIndex(*opened.window, "plotResolution", 0);
    EXPECT_EQ(opened.compare->primaryHistogram()->getHistogramResolution(),
              opened.compare->secondaryHistogram()->getHistogramResolution());
    ASSERT_TRUE(settleComparison(*opened.compare));
    opened.window.reset();
    dropTargets(context(), opened.targets);
}

// A second view that closes leaves the comparison showing the target alone, and a target of another size offers
// nothing: the second selector is rebuilt when the views change.
TEST_F(CombinationsProcessCase, ImageCompareFollowsTheViewsThatCloseAndChange)
{
    OpenedComparison opened = openComparison(context());
    ASSERT_NE(opened.widget, nullptr);
    opened.compare->setCompareMode(an::ui::processes::ImageCompareMode::HorizontalSplit);
    ASSERT_TRUE(settleComparison(*opened.compare));
    ASSERT_TRUE(opened.compare->splitModule()->hasOverlayImage());
    auto* selector = widgetOf<QComboBox>(*opened.window, "secondImageSelector");
    ASSERT_NE(selector, nullptr);
    for (SideView& side : opened.targets.sides)
    {
        // As the application does: the registry forgets the view, the listeners hear it is going, and it is deleted.
        context().removeViewControl(side.view.get(), false);
        opened.compare->onViewControlDestroyed(side.view.get());
        QStringList registered;
        for (ImageViewControl* view : context().getListOfViewControls())
        {
            registered << QString::fromStdString(view->getUniqueName());
        }
        QStringList items;
        for (int index = 0; index < selector->count(); ++index)
        {
            items << selector->itemText(index);
        }
        EXPECT_LT(selector->findText(QString::fromStdString(side.name)), 0)
            << "told of the view's closing; the registry holds " << registered.join(QStringLiteral(", ")).toStdString()
            << " and the selector " << items.join(QStringLiteral(", ")).toStdString();
        side.view.reset();
        QCoreApplication::processEvents();
        EXPECT_LT(selector->findText(QString::fromStdString(side.name)), 0) << "once the view is deleted";
    }
    opened.targets.sides.clear();
    ASSERT_TRUE(settleComparison(*opened.compare));
    EXPECT_EQ(selector->currentText(), NO_IMAGE_STR);
    QStringList offered;
    for (int index = 0; index < selector->count(); ++index)
    {
        offered << selector->itemText(index);
    }
    EXPECT_LT(selector->findText(QStringLiteral("second")), 0) << offered.join(QStringLiteral(", ")).toStdString();
    EXPECT_FALSE(opened.compare->splitModule()->hasOverlayImage());
    opened.window.reset();
    dropTargets(context(), opened.targets);
}

// The split's overlay reads the second image on a worker: while the second image's writer is held the window goes on
// answering with the overlay pending and nothing shown, and the overlay lands once the writer lets it go.
TEST_F(CombinationsProcessCase, ImageCompareSplitReadsTheSecondImageOffTheGuiThread)
{
    OpenedComparison opened = openComparison(context());
    ASSERT_NE(opened.widget, nullptr);
    ImageViewControl* second = opened.targets.side("second");
    ASSERT_NE(second, nullptr);
    an::ui::SplitComparisonDisplayModule* split = opened.compare->splitModule();
    ASSERT_NE(split, nullptr);
    std::optional<an::ui::ImageDocumentWriter> writer = writerOf(*second);
    ASSERT_TRUE(writer.has_value());
    HeldWriter gate(std::move(*writer));

    opened.compare->setCompareMode(an::ui::processes::ImageCompareMode::HorizontalSplit);
    EXPECT_TRUE(gate.held()) << "the window answered while the overlay waited for the second image";
    EXPECT_TRUE(opened.compare->isOverlayPending());
    pumpQuietly();
    EXPECT_TRUE(opened.compare->isOverlayPending()) << "nothing lands while the second image is held";
    EXPECT_FALSE(split->hasOverlayImage());
    EXPECT_EQ(opened.compare->overlaysShown(), 0u);

    gate.letGo();
    EXPECT_FALSE(gate.releasedAtDeadline());
    ASSERT_TRUE(settleComparison(*opened.compare));
    EXPECT_TRUE(split->hasOverlayImage());
    EXPECT_EQ(opened.compare->overlaysShown(), 1u);
    opened.window.reset();
    dropTargets(context(), opened.targets);
}

// A split on its way when the comparison turns to toggling no longer applies: once it lands nothing is pending and
// nothing is shown.
TEST_F(CombinationsProcessCase, ImageCompareSplitOnItsWayEndsWhenTheModeTurnsToToggle)
{
    OpenedComparison opened = openComparison(context());
    ASSERT_NE(opened.widget, nullptr);
    ImageViewControl* second = opened.targets.side("second");
    ASSERT_NE(second, nullptr);
    an::ui::SplitComparisonDisplayModule* split = opened.compare->splitModule();
    ASSERT_NE(split, nullptr);
    std::optional<an::ui::ImageDocumentWriter> writer = writerOf(*second);
    ASSERT_TRUE(writer.has_value());
    HeldWriter gate(std::move(*writer));

    opened.compare->setCompareMode(an::ui::processes::ImageCompareMode::HorizontalSplit);
    ASSERT_TRUE(opened.compare->isOverlayPending());
    opened.compare->setCompareMode(an::ui::processes::ImageCompareMode::Toggle);

    gate.letGo();
    EXPECT_FALSE(gate.releasedAtDeadline());
    EXPECT_TRUE(settleComparison(*opened.compare)) << "the split on its way leaves nothing pending";
    EXPECT_FALSE(opened.compare->isOverlayPending());
    EXPECT_FALSE(split->hasOverlayImage());
    EXPECT_EQ(opened.compare->overlaysShown(), 0u);
    opened.window.reset();
    dropTargets(context(), opened.targets);
}

// A split on its way when the second image is let go no longer applies: once it lands nothing is pending and nothing
// is shown.
TEST_F(CombinationsProcessCase, ImageCompareSplitOnItsWayEndsWhenTheSecondImageIsLetGo)
{
    OpenedComparison opened = openComparison(context());
    ASSERT_NE(opened.widget, nullptr);
    ImageViewControl* second = opened.targets.side("second");
    ASSERT_NE(second, nullptr);
    an::ui::SplitComparisonDisplayModule* split = opened.compare->splitModule();
    ASSERT_NE(split, nullptr);
    std::optional<an::ui::ImageDocumentWriter> writer = writerOf(*second);
    ASSERT_TRUE(writer.has_value());
    HeldWriter gate(std::move(*writer));

    opened.compare->setCompareMode(an::ui::processes::ImageCompareMode::HorizontalSplit);
    ASSERT_TRUE(opened.compare->isOverlayPending());
    selectView(*opened.window, "secondImageSelector", "");

    gate.letGo();
    EXPECT_FALSE(gate.releasedAtDeadline());
    EXPECT_TRUE(settleComparison(*opened.compare)) << "the split on its way leaves nothing pending";
    EXPECT_FALSE(opened.compare->isOverlayPending());
    EXPECT_FALSE(split->hasOverlayImage());
    EXPECT_EQ(opened.compare->overlaysShown(), 0u);
    opened.window.reset();
    dropTargets(context(), opened.targets);
}

// A split on its way when the window closes reads nothing the window freed: the worker holds what it shares with the
// window until it is done. The window's stop reaches the worker's reads, so it is soon done; nothing waits for it.
TEST_F(CombinationsProcessCase, ImageCompareClosedWhileItsSplitIsOnItsWayLeavesItsWorkerNothingFreed)
{
    OpenedComparison opened = openComparison(context());
    ASSERT_NE(opened.widget, nullptr);
    std::weak_ptr<const void> work;
    {
        // The pool is full, so the split's worker starts only once the window has gone - as a close does.
        PoolHeld pool;
        opened.compare->setCompareMode(an::ui::processes::ImageCompareMode::HorizontalSplit);
        ASSERT_TRUE(opened.compare->isOverlayPending());
        work = opened.compare->overlayWorkHandle();
        ASSERT_FALSE(work.expired());
        delete opened.window.release();
        opened.compare = nullptr;
        EXPECT_FALSE(work.expired()) << "the window freed what its worker on its way reads";
        pool.letGo();
    }
    QThreadPool::globalInstance()->waitForDone();
    pumpQuietly();
    EXPECT_TRUE(work.expired()) << "the worker kept what it shared with the window after it was done";
    dropTargets(context(), opened.targets);
}

// ---- The narrowband palette composer.

// The grid of every palette is built on the thread pool once the inputs are chosen, the latest request winning, and
// shown when it lands; Apply composes the selected palette into a new Float32 image named for it.
TEST_F(CombinationsProcessCase, NarrowbandPaletteGridIsBuiltOffTheGuiThreadAndApplyComposesThePalette)
{
    const CombinationsProcess& under = processNamed("NarrowbandPaletteComposer");
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    WindowHandle window = under.open(nullptr);
    auto* composer = dynamic_cast<NarrowbandPaletteComposerProcess*>(window.get());
    ASSERT_NE(composer, nullptr);
    EXPECT_FALSE(composer->hasGrid());
    auto* buttons = widgetOf<QDialogButtonBox>(*window, "buttonBox");
    ASSERT_NE(buttons, nullptr);
    EXPECT_FALSE(buttons->button(QDialogButtonBox::Apply)->isEnabled()) << "no inputs, no Apply";

    under.configure(*window, targets, "-");
    EXPECT_TRUE(buttons->button(QDialogButtonBox::Apply)->isEnabled());
    ASSERT_TRUE(pumpUntil([composer]() { return !composer->isGridBuilding() && composer->hasGrid(); }));
    const std::uint64_t shown = composer->gridsShown();
    EXPECT_GE(shown, 1u);
    // Three requests in a row: at most two more grids land, the last for the last request.
    setInt(*window, "haIntensitySpin", 80);
    setInt(*window, "haIntensitySpin", 120);
    setInt(*window, "haIntensitySpin", 100);
    ASSERT_TRUE(pumpUntil([composer]() { return !composer->isGridBuilding(); }));
    pumpQuietly();
    EXPECT_TRUE(composer->hasGrid());
    EXPECT_GE(composer->gridsShown(), shown + 1u);
    EXPECT_LE(composer->gridsShown(), shown + 3u);

    const std::size_t before = context().madeViews.size();
    const std::optional<fixtures::FixtureSamples> sho = applyAndRead(context(), under, *window, targets);
    ASSERT_TRUE(sho.has_value());
    EXPECT_EQ(sho->sampleType(), fixtures::FixtureSampleType::Float32);
    EXPECT_EQ(sho->channelCount(), 3u);
    const std::vector<ImageViewControl*> made = viewsMadeSince(context(), before);
    ASSERT_EQ(made.size(), 1u);
    EXPECT_NE(made.front()->getUniqueName().find("SHO"), std::string::npos) << made.front()->getUniqueName();

    under.configure(*window, targets, "hoo");
    const std::optional<fixtures::FixtureSamples> hoo = applyAndRead(context(), under, *window, targets);
    ASSERT_TRUE(hoo.has_value());
    EXPECT_FALSE(fixtures::sameSamples(*sho, *hoo)) << "another palette, another picture";
    window.reset();
    dropTargets(context(), targets);
}

// A composer with only an H-alpha image offers no Apply: the palettes need an OIII and an H-alpha or SII, or a
// one-shot-colour source.
TEST_F(CombinationsProcessCase, NarrowbandPaletteNeedsAnOiiiAndALine)
{
    const CombinationsProcess& under = processNamed("NarrowbandPaletteComposer");
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    WindowHandle window = under.open(nullptr);
    auto* buttons = widgetOf<QDialogButtonBox>(*window, "buttonBox");
    ASSERT_NE(buttons, nullptr);
    selectView(*window, "haSourceSelector", "ha");
    EXPECT_FALSE(buttons->button(QDialogButtonBox::Apply)->isEnabled());
    selectView(*window, "oiiiSourceSelector", "oiii");
    EXPECT_TRUE(buttons->button(QDialogButtonBox::Apply)->isEnabled());
    selectView(*window, "haSourceSelector", "");
    EXPECT_FALSE(buttons->button(QDialogButtonBox::Apply)->isEnabled());
    selectView(*window, "osc1SourceSelector", "ha");
    EXPECT_TRUE(buttons->button(QDialogButtonBox::Apply)->isEnabled()) << "a one-shot-colour source stands for both";
    window.reset();
    dropTargets(context(), targets);
}

// A grid builds on a worker that can outlive its window: closing the composer just after its grid started leaves the
// worker nothing of the window to read, however far the worker had got.
TEST_F(CombinationsProcessCase, NarrowbandPaletteClosedWhileItsGridBuildsLeavesItsWorkerNothingFreed)
{
    const CombinationsProcess& under = processNamed("NarrowbandPaletteComposer");
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        WindowHandle window = under.open(nullptr);
        auto* composer = dynamic_cast<NarrowbandPaletteComposerProcess*>(window.get());
        ASSERT_NE(composer, nullptr);
        under.configure(*window, targets, "-");
        EXPECT_TRUE(composer->isGridBuilding()) << attempt;
        window.reset();
    }
    QThreadPool::globalInstance()->waitForDone();
    pumpQuietly();
    dropTargets(context(), targets);
}

// The grid reads its sources on a worker: while the H-alpha source's writer is held, choosing the inputs returns with
// the grid building and nothing shown, and the grid lands once the writer lets it go.
TEST_F(CombinationsProcessCase, NarrowbandPaletteGridReadsItsSourcesOffTheGuiThread)
{
    const CombinationsProcess& under = processNamed("NarrowbandPaletteComposer");
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    ImageViewControl* ha = targets.side("ha");
    ASSERT_NE(ha, nullptr);
    WindowHandle window = under.open(nullptr);
    auto* composer = dynamic_cast<NarrowbandPaletteComposerProcess*>(window.get());
    ASSERT_NE(composer, nullptr);
    std::optional<an::ui::ImageDocumentWriter> writer = writerOf(*ha);
    ASSERT_TRUE(writer.has_value());
    HeldWriter gate(std::move(*writer));

    under.configure(*window, targets, "-");
    EXPECT_TRUE(gate.held()) << "choosing the inputs returned while the grid waited for its H-alpha source";
    EXPECT_TRUE(composer->isGridBuilding());
    pumpQuietly();
    EXPECT_TRUE(composer->isGridBuilding()) << "nothing lands while a source is held";
    EXPECT_FALSE(composer->hasGrid());

    gate.letGo();
    EXPECT_FALSE(gate.releasedAtDeadline());
    ASSERT_TRUE(pumpUntil([composer]() { return !composer->isGridBuilding() && composer->hasGrid(); }));
    window.reset();
    dropTargets(context(), targets);
}

// The grid reads its sources under leases it takes before it asks to be admitted: an edit of a source's view holds that
// writer and then asks to be admitted, so at a width of one a grid admitted first would hold the only thread while it
// waited for the writer, and the edit would wait for the thread.
TEST_F(CombinationsProcessCaseAtWidthOne, NarrowbandPaletteGridWaitingForAnEditOfASourceHoldsNoThreadTheEditNeeds)
{
    const CombinationsProcess& under = processNamed("NarrowbandPaletteComposer");
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    ImageViewControl* ha = targets.side("ha");
    ASSERT_NE(ha, nullptr);
    WindowHandle window = under.open(nullptr);
    auto* composer = dynamic_cast<NarrowbandPaletteComposerProcess*>(window.get());
    ASSERT_NE(composer, nullptr);

    an::tests::viewer::EditHoldingTheWriter edit(*ha);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    under.configure(*window, targets, "-");
    pumpQuietly();
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the edit waited for the thread the grid held while the grid waited for the edit's writer";

    ASSERT_TRUE(pumpUntil([composer]() { return !composer->isGridBuilding() && composer->hasGrid(); }));
    window.reset();
    dropTargets(context(), targets);
}

// Requests made while a grid builds are not lost: the grid being built lands, and then one more, for the values the
// window holds last.
TEST_F(CombinationsProcessCase, NarrowbandPaletteGridBuildsTheLatestRequestMadeWhileOneBuilds)
{
    const CombinationsProcess& under = processNamed("NarrowbandPaletteComposer");
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    ImageViewControl* ha = targets.side("ha");
    ASSERT_NE(ha, nullptr);
    WindowHandle window = under.open(nullptr);
    auto* composer = dynamic_cast<NarrowbandPaletteComposerProcess*>(window.get());
    ASSERT_NE(composer, nullptr);
    under.configure(*window, targets, "-");
    ASSERT_TRUE(pumpUntil([composer]() { return !composer->isGridBuilding() && composer->hasGrid(); }));
    pumpQuietly();
    const std::uint64_t shown = composer->gridsShown();

    // The next grid waits for its H-alpha source while two more requests are made.
    std::optional<an::ui::ImageDocumentWriter> writer = writerOf(*ha);
    ASSERT_TRUE(writer.has_value());
    HeldWriter gate(std::move(*writer));
    setInt(*window, "haIntensitySpin", 80);
    ASSERT_TRUE(composer->isGridBuilding());
    setInt(*window, "haIntensitySpin", 120);
    setInt(*window, "haIntensitySpin", 90);
    pumpQuietly();
    EXPECT_EQ(composer->gridsShown(), shown) << "nothing lands while a source is held";

    gate.letGo();
    EXPECT_FALSE(gate.releasedAtDeadline());
    ASSERT_TRUE(pumpUntil([composer]() { return !composer->isGridBuilding(); }));
    pumpQuietly();
    EXPECT_TRUE(composer->hasGrid());
    EXPECT_EQ(composer->gridsShown(), shown + 2u) << "the grid being built lands, then one for the latest values";
    window.reset();
    dropTargets(context(), targets);
}

// A grid on its way when an input is let go no longer applies: once it lands the window stops saying it is updating,
// its spinner stops, and it says what the inputs lack.
TEST_F(CombinationsProcessCase, NarrowbandPaletteSpinnerStopsWhenTheGridOnItsWayNoLongerApplies)
{
    const CombinationsProcess& under = processNamed("NarrowbandPaletteComposer");
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    ImageViewControl* ha = targets.side("ha");
    ASSERT_NE(ha, nullptr);
    WindowHandle window = under.open(nullptr);
    auto* composer = dynamic_cast<NarrowbandPaletteComposerProcess*>(window.get());
    ASSERT_NE(composer, nullptr);
    auto* spinner =
        static_cast<WaitingSpinnerWidget*>(window->findChild<QWidget*>(QStringLiteral("paletteGridBusySpinner")));
    ASSERT_NE(spinner, nullptr);
    std::optional<an::ui::ImageDocumentWriter> writer = writerOf(*ha);
    ASSERT_TRUE(writer.has_value());
    HeldWriter gate(std::move(*writer));

    under.configure(*window, targets, "-");
    ASSERT_TRUE(composer->isGridBuilding());
    ASSERT_TRUE(spinner->isSpinning());
    selectView(*window, "oiiiSourceSelector", "");

    gate.letGo();
    EXPECT_FALSE(gate.releasedAtDeadline());
    ASSERT_TRUE(pumpUntil([composer]() { return !composer->isGridBuilding(); }));
    pumpQuietly();
    EXPECT_FALSE(spinner->isSpinning()) << "the spinner stops with the grid that no longer applies";
    const QString status = widgetOf<QLabel>(*window, "statusLabel")->text();
    EXPECT_FALSE(status.startsWith(QStringLiteral("Updating"))) << status.toStdString();
    EXPECT_EQ(composer->gridsShown(), 0u);
    window.reset();
    dropTargets(context(), targets);
}

// ---- The profiled starmask recombination.

// A profile is read on the thread pool and its stars counted once it lands; a file that is no profile is refused
// with a message and leaves no stars; without a profile the stars are put back unmasked.
TEST_F(CombinationsProcessCase, ProfiledRecombinationLoadsAProfileOffTheGuiThread)
{
    const CombinationsProcess& under = processNamed("ProfiledStarmaskRecombination");
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = writeProfile(session(), directory, {{10.0, 12.0}, {30.0, 20.0}}, 3.0);
    ASSERT_FALSE(path.isEmpty());
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    WindowHandle window = under.open(nullptr);
    auto* profiled = dynamic_cast<ProfiledStarmaskRecombinationProcess*>(window.get());
    ASSERT_NE(profiled, nullptr);
    under.configure(*window, targets, "-");
    EXPECT_EQ(profiled->profileStarCount(), 0u);

    profiled->loadProfile(path);
    EXPECT_TRUE(profiled->isProfileLoading());
    ASSERT_TRUE(pumpUntil([profiled]() { return !profiled->isProfileLoading(); }));
    EXPECT_EQ(profiled->profileStarCount(), 2u);
    EXPECT_EQ(widgetOf<QLineEdit>(*window, "filteredStarsTxt")->text(), QStringLiteral("2"));

    profiled->loadProfile(directory.filePath(QStringLiteral("missing.anspv1")));
    ASSERT_TRUE(pumpUntil([profiled]() { return !profiled->isProfileLoading(); }));
    EXPECT_EQ(profiled->profileStarCount(), 0u);
    EXPECT_EQ(context().errorCount(), 1) << "the window says the file could not be read";
    window.reset();
    dropTargets(context(), targets);
}

// A profile load on its way when the window closes reads nothing the window freed: the worker holds what it shares
// with the window until it is done.
TEST_F(CombinationsProcessCase, ProfiledRecombinationClosedWhileItsProfileLoadsLeavesItsWorkerNothingFreed)
{
    const CombinationsProcess& under = processNamed("ProfiledStarmaskRecombination");
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = writeProfile(session(), directory, {{10.0, 12.0}}, 3.0);
    ASSERT_FALSE(path.isEmpty());
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    WindowHandle window = under.open(nullptr);
    auto* profiled = dynamic_cast<ProfiledStarmaskRecombinationProcess*>(window.get());
    ASSERT_NE(profiled, nullptr);
    under.configure(*window, targets, "-");
    ASSERT_TRUE(settleWindow(under, *window));
    std::weak_ptr<const void> load;
    {
        // The pool is full, so the load's worker starts only once the window has gone - as a close does.
        PoolHeld pool;
        profiled->loadProfile(path);
        ASSERT_TRUE(profiled->isProfileLoading());
        load = profiled->profileLoadHandle();
        ASSERT_FALSE(load.expired());
        delete window.release();
        profiled = nullptr;
        EXPECT_FALSE(load.expired()) << "the window freed what its worker on its way reads";
        pool.letGo();
    }
    QThreadPool::globalInstance()->waitForDone();
    pumpQuietly();
    EXPECT_TRUE(load.expired()) << "the worker kept what it shared with the window after it was done";
    dropTargets(context(), targets);
}

// The mask is painted from the profile's stars: where a star is the stars are put back, far from every star the
// starless image stays as it is; without a profile the sum is unmasked everywhere; inverted, the mask is the other way
// round.
TEST_F(CombinationsProcessCase, ProfiledRecombinationPutsBackTheStarsOfTheProfile)
{
    const CombinationsProcess& under = processNamed("ProfiledStarmaskRecombination");
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples(under.scene);
    ASSERT_TRUE(scene.has_value());
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const std::uint32_t starX = scene->width() / 4u;
    const std::uint32_t starY = scene->height() / 4u;
    const QString path =
        writeProfile(session(), directory, {{static_cast<double>(starX), static_cast<double>(starY)}}, 6.0);
    ASSERT_FALSE(path.isEmpty());

    const std::optional<fixtures::FixtureSamples> unmasked = recombineWithProfile(context(), QString(), 0u);
    const std::optional<fixtures::FixtureSamples> masked = recombineWithProfile(context(), path, 1u);
    ASSERT_TRUE(unmasked.has_value() && masked.has_value());
    const std::optional<fixtures::FixtureSamples> starless = sceneSamples(std::string(under.scene) + "-starless");
    ASSERT_TRUE(starless.has_value());
    // Far from the star, the masked result is the starless image; at the star, the unmasked sum.
    const std::uint32_t farX = scene->width() * 3u / 4u;
    const std::uint32_t farY = scene->height() * 3u / 4u;
    for (std::uint32_t c = 0u; c < scene->channelCount(); ++c)
    {
        EXPECT_NEAR(masked->value(farX, farY, c), starless->value(farX, farY, c), 1.0e-6);
        EXPECT_NEAR(unmasked->value(farX, farY, c), starless->value(farX, farY, c) + scene->value(farX, farY, c),
                    1.0e-5);
        EXPECT_NEAR(masked->value(starX, starY, c), unmasked->value(starX, starY, c), 1.0e-5);
    }
}

// The shape selector decides the mask's controls: binary areas soften, shaped areas grow by a percentage and pixels.
TEST_F(CombinationsProcessCase, ProfiledRecombinationShapesFollowTheSelector)
{
    const CombinationsProcess& under = processNamed("ProfiledStarmaskRecombination");
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = writeProfile(session(), directory, {{5.0, 5.0}}, 3.0);
    ASSERT_FALSE(path.isEmpty());
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    WindowHandle window = under.open(nullptr);
    auto* profiled = dynamic_cast<ProfiledStarmaskRecombinationProcess*>(window.get());
    ASSERT_NE(profiled, nullptr);
    under.configure(*window, targets, "-");
    auto* soften = widgetOf<QDoubleSpinBox>(*window, "softenEdgesValue");
    ASSERT_NE(soften, nullptr);
    EXPECT_FALSE(soften->isEnabled()) << "the mask's controls wait for a profile";
    profiled->loadProfile(path);
    ASSERT_TRUE(pumpUntil([profiled]() { return !profiled->isProfileLoading(); }));
    ASSERT_EQ(profiled->profileStarCount(), 1u);
    auto* percent = widgetOf<QDoubleSpinBox>(*window, "percentPixelIncreaseValue");
    auto* pixels = widgetOf<QSpinBox>(*window, "pixelIncreaseValue");
    ASSERT_TRUE(soften != nullptr && percent != nullptr && pixels != nullptr);
    EXPECT_TRUE(soften->isEnabled());
    EXPECT_FALSE(percent->isEnabled());
    EXPECT_FALSE(pixels->isEnabled());
    for (const char* shape : {"Bell", "Truncated Bell", "Linear", "Truncated Linear", "Cubic", "Quadratic"})
    {
        setCurrentText(*window, "shapeSelector", QString::fromLatin1(shape));
        EXPECT_FALSE(soften->isEnabled()) << shape;
        EXPECT_TRUE(percent->isEnabled()) << shape;
        EXPECT_TRUE(pixels->isEnabled()) << shape;
        EXPECT_TRUE(an::ui::processes::profiled_recombination_detail::starMaskShapeFromLabel(QString::fromLatin1(shape))
                        .has_value());
    }
    EXPECT_FALSE(
        an::ui::processes::profiled_recombination_detail::starMaskShapeFromLabel(QStringLiteral("Binary Areas"))
            .has_value());
    window.reset();
    dropTargets(context(), targets);
}

// ---- The recombinations' arithmetic.

// A recombination of physical images keeps samples above one - a starless image at 1.2 plus stars at 0.5 is 1.7 -
// where a normalized one is held at one; a sixteen-bit star image
// over a floating starless one is written in the floating representation; and the result starts from the starless
// image's record, cards included.
TEST_F(CombinationsProcessCase, RecombinationKeepsSamplesAboveOneTheHigherRepresentationAndTheStarlessRecord)
{
    const CombinationsProcess& under = processNamed("StarmaskRecombination");
    {
        std::optional<an::consumer::ImageAsset> physicalStarless = an::tests::viewer::makeAsset<float>(
            session(), 12u, 9u, Channels::Rgb, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 1.2f; },
            an::NumericDomain::PhysicalScaled);
        std::optional<an::consumer::ImageAsset> physicalStars = an::tests::viewer::makeAsset<float>(
            session(), 12u, 9u, Channels::Rgb, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; },
            an::NumericDomain::PhysicalScaled);
        ASSERT_TRUE(physicalStarless.has_value() && physicalStars.has_value());
        Targets targets;
        targets.target = assetView(std::move(*physicalStars), "stars");
        context().addViewControl(targets.target.get());
        SideView starless{.name = "starless", .view = assetView(std::move(*physicalStarless), "starless")};
        context().addViewControl(starless.view.get());
        targets.sides.push_back(std::move(starless));
        WindowHandle window = under.open(nullptr);
        under.configure(*window, targets, "-");
        const std::optional<fixtures::FixtureSamples> result = applyAndRead(context(), under, *window, targets);
        ASSERT_TRUE(result.has_value());
        EXPECT_NEAR(result->value(3u, 3u, 1u), 1.7, 1.0e-5) << "a physical sum is not held at one";
        window.reset();
        dropTargets(context(), targets);
    }
    std::optional<an::consumer::ImageAsset> recorded = assetWithObjectCard(session(), 12u, 9u, 0.4f);
    ASSERT_TRUE(recorded.has_value());
    ViewHandle starless = assetView(std::move(*recorded), "starless");
    context().addViewControl(starless.get());
    ViewHandle stars = addedView<std::uint16_t>("stars", 12u, 9u, Channels::Rgb,
                                                [](std::uint32_t, std::uint32_t, std::uint32_t) { return 6554u; });
    ASSERT_TRUE(starless != nullptr && stars != nullptr);
    Targets targets;
    targets.target = std::move(stars);
    targets.sides.push_back(SideView{.name = "starless", .view = std::move(starless)});
    WindowHandle window = under.open(nullptr);
    under.configure(*window, targets, "-");
    const std::size_t before = context().madeViews.size();
    const std::optional<fixtures::FixtureSamples> result = applyAndRead(context(), under, *window, targets);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->sampleType(), fixtures::FixtureSampleType::Float32);
    EXPECT_NEAR(result->value(3u, 3u, 1u), 0.4 + 6554.0 / 65535.0, 1.0e-5);
    const std::vector<ImageViewControl*> made = viewsMadeSince(context(), before);
    ASSERT_EQ(made.size(), 1u);
    EXPECT_TRUE(carriesObjectCard(made.front()->document())) << "the starless image's own cards are kept";
    window.reset();
    dropTargets(context(), targets);
}

// A floating file's image states a physical quantity, as the reader describes every floating file: a blend and an LRGB
// combination read it as the fractions of full scale the view shows, answering as over the same samples stated
// normalized, and a recombination of one beside an image in other units adds both as fractions, held at one.
TEST_F(CombinationsProcessCase, FloatingFilesAreCombinedAsTheFractionsTheViewShows)
{
    static constexpr std::uint32_t kWidth = 9u;
    static constexpr std::uint32_t kHeight = 7u;
    const auto viewOf = [this](const std::string& name, Channels channels, an::NumericDomain domain,
                               const std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)>& value) {
        std::optional<an::consumer::ImageAsset> asset =
            an::tests::viewer::makeAsset<float>(session(), kWidth, kHeight, channels, value, domain);
        EXPECT_TRUE(asset.has_value());
        ViewHandle view = asset.has_value() ? assetView(std::move(*asset), name) : ViewHandle{};
        if (view != nullptr)
        {
            context().addViewControl(view.get());
        }
        return view;
    };
    const auto colour = [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
        return static_cast<float>(0.1 + 0.05 * x + 0.03 * y + 0.02 * c);
    };
    const auto second = [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
        return static_cast<float>(0.6 - 0.03 * x + 0.02 * y - 0.01 * c);
    };
    const auto grey = [](std::uint32_t, std::uint32_t y, std::uint32_t) { return 0.2f + 0.04f * y; };
    const auto combined = [&](const char* process, const char* side, const char* mode, an::NumericDomain domain,
                              Channels sideChannels,
                              const std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)>& sideValue)
        -> std::optional<fixtures::FixtureSamples> {
        SCOPED_TRACE(process);
        const CombinationsProcess& under = processNamed(process);
        Targets targets;
        targets.target = viewOf("target", Channels::Rgb, domain, colour);
        targets.sides.push_back(SideView{.name = side, .view = viewOf(side, sideChannels, domain, sideValue)});
        EXPECT_TRUE(targets.target != nullptr && targets.sides.front().view != nullptr);
        if (targets.target == nullptr || targets.sides.front().view == nullptr)
        {
            return std::nullopt;
        }
        const int errorsBefore = context().errorCount();
        WindowHandle window = under.open(targets.target.get());
        under.configure(*window, targets, mode);
        std::optional<fixtures::FixtureSamples> result = applyAndRead(context(), under, *window, targets);
        EXPECT_EQ(context().errorCount(), errorsBefore)
            << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        window.reset();
        dropTargets(context(), targets);
        return result;
    };
    struct Combination
    {
        const char* process;
        const char* side;
        const char* mode;
        Channels sideChannels;
        std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)> sideValue;
    };
    const std::vector<Combination> combinations{{"ImageBlend", "second", "replace", Channels::Rgb, second},
                                                {"LRGBCombination", "luminance", "-", Channels::Grey, grey}};
    for (const Combination& combination : combinations)
    {
        const std::optional<fixtures::FixtureSamples> physical =
            combined(combination.process, combination.side, combination.mode, an::NumericDomain::PhysicalScaled,
                     combination.sideChannels, combination.sideValue);
        const std::optional<fixtures::FixtureSamples> normalized =
            combined(combination.process, combination.side, combination.mode, an::NumericDomain::NormalizedLinear,
                     combination.sideChannels, combination.sideValue);
        ASSERT_TRUE(physical.has_value() && normalized.has_value()) << combination.process;
        EXPECT_LE(largestDifference(*physical, *normalized), 1.0e-6) << combination.process;
    }

    // A floating file's starless image at 0.8 and sixteen-bit stars at 0.4: added as fractions, held at one.
    const CombinationsProcess& under = processNamed("StarmaskRecombination");
    Targets targets;
    targets.target = addedView<std::uint16_t>("stars", kWidth, kHeight, Channels::Rgb,
                                              [](std::uint32_t, std::uint32_t, std::uint32_t) { return 26214u; });
    targets.sides.push_back(SideView{.name = "starless",
                                     .view = viewOf("starless", Channels::Rgb, an::NumericDomain::PhysicalScaled,
                                                    [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.8f; })});
    ASSERT_TRUE(targets.target != nullptr && targets.sides.front().view != nullptr);
    const int errorsBefore = context().errorCount();
    WindowHandle window = under.open(nullptr);
    under.configure(*window, targets, "-");
    const std::optional<fixtures::FixtureSamples> result = applyAndRead(context(), under, *window, targets);
    EXPECT_EQ(context().errorCount(), errorsBefore) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->value(3u, 3u, 1u), 1.0, 1.0e-5) << "the sum of fractions is held at one";
    window.reset();
    dropTargets(context(), targets);
}

// ---- The NBRGB combination.

// The preview's display mode shows the combination, the RGB image or one narrowband channel as colour; the Apply
// is offered only with a narrowband channel.
TEST_F(CombinationsProcessCase, NbrgbPreviewShowsTheInputsItIsAskedFor)
{
    const CombinationsProcess& under = processNamed("NBRGBCombination");
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    const std::optional<fixtures::FixtureSamples> source = documentSamples(targets.target->document());
    ASSERT_TRUE(source.has_value());
    WindowHandle window = under.open(targets.target.get());
    auto* preview = dynamic_cast<ImageRTPreviewControl*>(window.get());
    ASSERT_NE(preview, nullptr);
    auto* buttons = widgetOf<QDialogButtonBox>(*window, "buttonBox");
    ASSERT_NE(buttons, nullptr);
    selectView(*window, "redImageSelector", "");
    EXPECT_FALSE(buttons->button(QDialogButtonBox::Apply)->isEnabled());
    under.configure(*window, targets, "-");
    EXPECT_TRUE(buttons->button(QDialogButtonBox::Apply)->isEnabled());

    // The window's preview is always on: it shows once the inputs are chosen.
    ASSERT_TRUE(pumpUntil([preview]() { return preview->runTelemetry().previewsCommitted >= 1u; }));
    ASSERT_TRUE(settleWindow(under, *window));
    ASSERT_NE(preview->previewView(), nullptr);
    const std::optional<fixtures::FixtureSamples> combined = documentSamples(preview->previewView()->document());
    ASSERT_TRUE(combined.has_value());
    EXPECT_FALSE(fixtures::sameSamples(*source, *combined));

    auto showMode = [&](int index) -> std::optional<fixtures::FixtureSamples> {
        const std::uint64_t committed = preview->runTelemetry().previewsCommitted;
        setIndex(*window, "previewDisplayMode", index);
        EXPECT_TRUE(
            pumpUntil([preview, committed]() { return preview->runTelemetry().previewsCommitted > committed; }));
        EXPECT_TRUE(settleWindow(under, *window));
        return documentSamples(preview->previewView()->document());
    };
    const std::optional<fixtures::FixtureSamples> rgb = showMode(1);
    ASSERT_TRUE(rgb.has_value());
    EXPECT_TRUE(fixtures::sameSamples(*source, *rgb)) << "the RGB mode shows the target";
    const std::optional<fixtures::FixtureSamples> red = showMode(2);
    ASSERT_TRUE(red.has_value());
    ASSERT_EQ(red->channelCount(), 3u);
    const std::optional<fixtures::FixtureSamples> narrowband = documentSamples(targets.side("red")->document());
    ASSERT_TRUE(narrowband.has_value());
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        EXPECT_NEAR(red->value(2u, 2u, c), narrowband->value(2u, 2u, 0u), 1.0e-6)
            << "the narrowband channel is shown as grey colour";
    }
    window.reset();
    dropTargets(context(), targets);
}

// ---- The narrowband colour mapper.

// The layers follow the editor and the table, the active image is a layer when the window opens, and Apply opens the
// colourized layers beside the result when asked.
TEST_F(CombinationsProcessCase, MapperLayersFollowTheEditorAndColourizedLayersOpenBesideTheResult)
{
    const CombinationsProcess& under = processNamed("NBColorMapper");
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    WindowHandle window = under.open(targets.target.get());
    auto* mapper = dynamic_cast<NBColorMapperProcess*>(window.get());
    ASSERT_NE(mapper, nullptr);
    ASSERT_EQ(mapper->layers().size(), 1u) << "the target is the first layer";
    EXPECT_EQ(mapper->layers().front().imageName, targets.target->getUniqueName());

    under.configure(*window, targets, "-");
    ASSERT_EQ(mapper->layers().size(), 2u);
    EXPECT_EQ(mapper->layers()[0].imageName, "first");
    EXPECT_DOUBLE_EQ(mapper->layers()[0].layer.hueDegrees, 0.0);
    EXPECT_EQ(mapper->layers()[1].imageName, "second");
    EXPECT_DOUBLE_EQ(mapper->layers()[1].layer.hueDegrees, 180.0);

    setChecked(*window, "returnColorizedLayers", true);
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(under, *window, targets));
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::vector<ImageViewControl*> made = viewsMadeSince(context(), before);
    ASSERT_EQ(made.size(), 3u) << "the result and one colourized image per layer";
    for (ImageViewControl* view : made)
    {
        ASSERT_TRUE(settleView(*view));
        const std::optional<fixtures::FixtureSamples> samples = documentSamples(view->document());
        ASSERT_TRUE(samples.has_value());
        EXPECT_EQ(samples->channelCount(), 3u);
        EXPECT_EQ(samples->sampleType(), fixtures::FixtureSampleType::Float32);
    }
    window.reset();
    dropTargets(context(), targets);
}

// Each colourized image is its layer mapped alone: the two layers' images differ from each other and from the whole
// map.
TEST_F(CombinationsProcessCase, MapperColourizesEachLayerAlone)
{
    const CombinationsProcess& under = processNamed("NBColorMapper");
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    WindowHandle window = under.open(targets.target.get());
    under.configure(*window, targets, "-");
    setChecked(*window, "returnColorizedLayers", true);
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(under, *window, targets));
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::vector<ImageViewControl*> made = viewsMadeSince(context(), before);
    ASSERT_EQ(made.size(), 3u) << "the map and one colourized image per layer";
    std::optional<fixtures::FixtureSamples> whole;
    std::optional<fixtures::FixtureSamples> first;
    std::optional<fixtures::FixtureSamples> second;
    for (ImageViewControl* view : made)
    {
        ASSERT_TRUE(settleView(*view));
        std::optional<fixtures::FixtureSamples> samples = documentSamples(view->document());
        ASSERT_TRUE(samples.has_value());
        const std::string name = view->getUniqueName();
        if (name.find("first colorized") != std::string::npos)
        {
            first = std::move(samples);
        }
        else if (name.find("second colorized") != std::string::npos)
        {
            second = std::move(samples);
        }
        else
        {
            whole = std::move(samples);
        }
    }
    ASSERT_TRUE(whole.has_value() && first.has_value() && second.has_value()) << "one image of each kind";
    EXPECT_FALSE(fixtures::sameSamples(*first, *second)) << "each colourized image shows its own layer";
    EXPECT_FALSE(fixtures::sameSamples(*first, *whole)) << "a layer mapped alone is not the whole map";
    EXPECT_FALSE(fixtures::sameSamples(*second, *whole)) << "a layer mapped alone is not the whole map";
    window.reset();
    dropTargets(context(), targets);
}

// ---- The channel composition.

// A composition narrows to the highest input representation, and a component left out is a zero plane.
TEST_F(CombinationsProcessCase, CompositionNarrowsToTheHighestInputAndFillsAMissingComponentWithZero)
{
    const CombinationsProcess& under = processNamed("ChannelComposition");
    ViewHandle eight =
        addedView<std::uint8_t>("eight", 10u, 6u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            return static_cast<std::uint8_t>(20u * x);
        });
    ViewHandle sixteen =
        addedView<std::uint16_t>("sixteen", 10u, 6u, Channels::Grey, [](std::uint32_t, std::uint32_t y, std::uint32_t) {
            return static_cast<std::uint16_t>(6000u * y);
        });
    ASSERT_TRUE(eight != nullptr && sixteen != nullptr);
    Targets targets;
    targets.sides.push_back(SideView{.name = "red", .view = std::move(eight)});
    targets.sides.push_back(SideView{.name = "green", .view = std::move(sixteen)});
    WindowHandle window = under.open(nullptr);
    selectView(*window, "redChannel", "eight");
    selectView(*window, "greenChannel", "sixteen");
    selectView(*window, "blueChannel", "");
    setChecked(*window, "linearMatchCheckBox", false);
    setCurrentText(*window, "colorSpace", QStringLiteral("RGB"));
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(under, *window, targets));
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::vector<ImageViewControl*> made = viewsMadeSince(context(), before);
    ASSERT_EQ(made.size(), 1u);
    ASSERT_TRUE(settleView(*made.front()));
    const std::optional<fixtures::FixtureSamples> result = documentSamples(made.front()->document());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->sampleType(), fixtures::FixtureSampleType::UInt16);
    ASSERT_EQ(result->channelCount(), 3u);
    EXPECT_NEAR(result->value(3u, 2u, 0u), std::round(60.0 / 255.0 * 65535.0), 1.0);
    EXPECT_DOUBLE_EQ(result->value(3u, 2u, 1u), 12000.0);
    EXPECT_DOUBLE_EQ(result->value(3u, 2u, 2u), 0.0);
    EXPECT_NE(made.front()->getUniqueName().find("RGB"), std::string::npos);
    window.reset();
    dropTargets(context(), targets);
}

// Three sequences compose frame by frame into a new sequence, shown in a view of its own.
TEST_F(CombinationsProcessCase, CompositionOverSequencesWritesASequence)
{
    const CombinationsProcess& under = processNamed("ChannelComposition");
    constexpr std::uint32_t kWidth = 8u;
    constexpr std::uint32_t kHeight = 6u;
    QTemporaryDir recordings;
    ASSERT_TRUE(recordings.isValid());
    std::vector<ViewHandle> views;
    const char* names[3] = {"red", "green", "blue"};
    Targets targets;
    for (std::uint32_t component = 0u; component < 3u; ++component)
    {
        const QString path = recordings.filePath(QStringLiteral("%1.ser").arg(QLatin1String(names[component])));
        an::Result<an::ui::SequenceWriter> writer = an::ui::SequenceWriter::create(
            path, an::ui::SequenceWriteSettings{.container = an::SequenceFormat::Ser, .frameCount = 2u});
        ASSERT_TRUE(writer.hasValue()) << writer.status().message();
        std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
        ASSERT_TRUE(scope.has_value());
        for (std::uint32_t frame = 0u; frame < 2u; ++frame)
        {
            std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
                session(), kWidth, kHeight, Channels::Grey,
                [component, frame](std::uint32_t x, std::uint32_t, std::uint32_t) {
                    return static_cast<float>(0.1 * component + 0.05 * frame + 0.01 * x);
                });
            ASSERT_TRUE(asset.has_value());
            an::Result<an::consumer::AssetReadLease> lease = asset->read();
            ASSERT_TRUE(lease.hasValue());
            const an::Status appended = writer->append(scope->engine(), scope->execution(), lease->image());
            ASSERT_TRUE(appended.ok()) << appended.message();
        }
        an::Result<an::SequenceDescriptor> published = writer->finish(scope->execution());
        ASSERT_TRUE(published.hasValue()) << published.status().message();
        scope.reset();

        std::optional<an::consumer::ImageAsset> first = an::tests::viewer::makeAsset<float>(
            session(), kWidth, kHeight, Channels::Grey, [component](std::uint32_t x, std::uint32_t, std::uint32_t) {
                return static_cast<float>(0.1 * component + 0.01 * x);
            });
        ASSERT_TRUE(first.has_value());
        an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
            std::move(*first), an::ui::DocumentNames{.displayName = QLatin1String(names[component])});
        ASSERT_TRUE(document.hasValue());
        an::ui::DocumentSequence sequence;
        sequence.sourcePath = path;
        sequence.descriptor.format = an::SequenceFormat::Ser;
        sequence.descriptor.frameCount = 2u;
        sequence.currentFrame = 0u;
        document->setSequence(sequence);
        ViewHandle view(new ImageViewControl());
        view->initialize(names[component], ViewControlMode::standard);
        view->resize(640, 480);
        view->setSequenceDocument(std::move(*document), false);
        view->initializeNewImageHistory("Sequence opened");
        ASSERT_TRUE(settleView(*view));
        context().addViewControl(view.get());
        targets.sides.push_back(SideView{.name = names[component], .view = std::move(view)});
    }
    WindowHandle window = under.open(nullptr);
    under.configure(*window, targets, "-");
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(under, *window, targets));
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::vector<ImageViewControl*> made = viewsMadeSince(context(), before);
    ASSERT_EQ(made.size(), 1u);
    ASSERT_TRUE(settleView(*made.front()));
    EXPECT_TRUE(made.front()->isSequence());
    const std::optional<an::ui::DocumentSequence> written = made.front()->document().sequence();
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->descriptor.frameCount, 2u);
    const std::optional<fixtures::FixtureSamples> shown = documentSamples(made.front()->document());
    ASSERT_TRUE(shown.has_value());
    ASSERT_EQ(shown->channelCount(), 3u);
    EXPECT_NEAR(unitValueOf(shown->value(2u, 1u, 2u), shown->sampleType()), 0.22, 1.0e-4)
        << "the blue component's first frame";
    window.reset();
    dropTargets(context(), targets);
}

// Linear Match scales the second and third components onto the first: a component that is an affine function of the
// first with a positive slope is matched onto it exactly, and the first is kept as it is.
TEST_F(CombinationsProcessCase, CompositionLinearMatchScalesEachComponentOntoTheFirst)
{
    const CombinationsProcess& under = processNamed("ChannelComposition");
    constexpr std::uint32_t kWidth = 12u;
    constexpr std::uint32_t kHeight = 9u;
    // Every sample of every component inside the interval the match measures, the first spread across it.
    const auto first = [](std::uint32_t x, std::uint32_t y) {
        return 0.1 + 0.6 * static_cast<double>((x * 7u + y * 5u) % 23u) / 22.0;
    };
    ViewHandle red = addedView<float>(
        "red", kWidth, kHeight, Channels::Grey,
        [&first](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<float>(first(x, y)); });
    ViewHandle green = addedView<float>("green", kWidth, kHeight, Channels::Grey,
                                        [&first](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                            return static_cast<float>(0.5 * first(x, y) + 0.2);
                                        });
    ViewHandle blue = addedView<float>("blue", kWidth, kHeight, Channels::Grey,
                                       [&first](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                           return static_cast<float>(0.25 * first(x, y) + 0.05);
                                       });
    ASSERT_TRUE(red != nullptr && green != nullptr && blue != nullptr);
    Targets targets;
    targets.sides.push_back(SideView{.name = "red", .view = std::move(red)});
    targets.sides.push_back(SideView{.name = "green", .view = std::move(green)});
    targets.sides.push_back(SideView{.name = "blue", .view = std::move(blue)});
    WindowHandle window = under.open(nullptr);
    selectView(*window, "redChannel", "red");
    selectView(*window, "greenChannel", "green");
    selectView(*window, "blueChannel", "blue");
    setChecked(*window, "linearMatchCheckBox", true);
    setCurrentText(*window, "colorSpace", QStringLiteral("RGB"));
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(under, *window, targets));
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::vector<ImageViewControl*> made = viewsMadeSince(context(), before);
    ASSERT_EQ(made.size(), 1u);
    ASSERT_TRUE(settleView(*made.front()));
    const std::optional<fixtures::FixtureSamples> result = documentSamples(made.front()->document());
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->channelCount(), 3u);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        double largest = 0.0;
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                largest = (std::max)(largest, std::abs(result->value(x, y, c) - first(x, y)));
            }
        }
        EXPECT_LE(largest, 1.0e-4) << "the component " << c << " matched onto the first";
    }
    window.reset();
    dropTargets(context(), targets);
}

// ---- The image blend.

// A filter of the second image below half a pixel still filters: a Gaussian blur of radius 0.3 changes the image, and
// a high pass of that radius is not mid-grey everywhere.
TEST_F(CombinationsProcessCase, BlendFiltersBelowHalfAPixelStillFilter)
{
    const CombinationsProcess& under = processNamed("ImageBlend");
    auto blended = [&](const char* filter) {
        Targets targets = openScene(context(), under);
        EXPECT_NE(targets.target, nullptr);
        WindowHandle window = under.open(targets.target.get());
        under.configure(*window, targets, "replace");
        setCurrentText(*window, "filterSelector", QString::fromLatin1(filter));
        setDouble(*window, "filterRadiusValue", 0.3);
        std::optional<fixtures::FixtureSamples> result = applyAndRead(context(), under, *window, targets);
        EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        window.reset();
        dropTargets(context(), targets);
        return result;
    };
    const std::optional<fixtures::FixtureSamples> plain = blended("None");
    const std::optional<fixtures::FixtureSamples> blurred = blended("Gaussian Blur");
    const std::optional<fixtures::FixtureSamples> passed = blended("High Pass");
    ASSERT_TRUE(plain.has_value() && blurred.has_value() && passed.has_value());
    EXPECT_FALSE(fixtures::sameSamples(*plain, *blurred)) << "a blur of radius 0.3 blurs";
    double spread = 0.0;
    for (std::uint32_t y = 0u; y < passed->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < passed->width(); ++x)
        {
            spread = (std::max)(spread, std::abs(passed->value(x, y, 0u) - 0.5));
        }
    }
    EXPECT_GT(spread, 1.0e-3) << "a high pass of radius 0.3 is not mid-grey";
}

// Linear Dodge over a 32-bit image saturates at the largest code rather than wrapping.
TEST_F(CombinationsProcessCase, BlendLinearDodgeSaturatesA32BitImage)
{
    const CombinationsProcess& under = processNamed("ImageBlend");
    constexpr std::uint32_t kHigh = 3000000000u;
    ViewHandle target = addedView<std::uint32_t>("wide", 8u, 6u, Channels::Rgb,
                                                 [](std::uint32_t, std::uint32_t, std::uint32_t) { return kHigh; });
    ViewHandle second = addedView<std::uint32_t>("second", 8u, 6u, Channels::Rgb,
                                                 [](std::uint32_t, std::uint32_t, std::uint32_t) { return kHigh; });
    ASSERT_TRUE(target != nullptr && second != nullptr);
    Targets targets;
    targets.target = std::move(target);
    targets.sides.push_back(SideView{.name = "second", .view = std::move(second)});
    WindowHandle window = under.open(targets.target.get());
    under.configure(*window, targets, "linear-dodge-add");
    const std::optional<fixtures::FixtureSamples> result = applyAndRead(context(), under, *window, targets);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->sampleType(), fixtures::FixtureSampleType::UInt32);
    EXPECT_DOUBLE_EQ(result->value(2u, 2u, 0u), 4294967295.0) << "the sum saturates";
    window.reset();
    dropTargets(context(), targets);
}

// Each transfer stretches its own image: over Replace, which writes the second image, the target's transfer changes
// nothing that is written and the second image's changes it.
TEST_F(CombinationsProcessCase, BlendTransfersStretchOnlyTheirOwnImage)
{
    const CombinationsProcess& under = processNamed("ImageBlend");
    auto replaced = [&](const char* stretched) {
        Targets targets = openScene(context(), under);
        EXPECT_NE(targets.target, nullptr);
        WindowHandle window = under.open(targets.target.get());
        under.configure(*window, targets, "replace");
        if (stretched != nullptr)
        {
            setDouble(*window, stretched, 0.25);
        }
        std::optional<fixtures::FixtureSamples> result = applyAndRead(context(), under, *window, targets);
        EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        window.reset();
        dropTargets(context(), targets);
        return result;
    };
    const std::optional<fixtures::FixtureSamples> plain = replaced(nullptr);
    const std::optional<fixtures::FixtureSamples> targetStretched = replaced("srcImageMidtonesValue");
    const std::optional<fixtures::FixtureSamples> secondStretched = replaced("imageBlendMidtonesValue");
    ASSERT_TRUE(plain.has_value() && targetStretched.has_value() && secondStretched.has_value());
    EXPECT_TRUE(fixtures::sameSamples(*plain, *targetStretched))
        << "the target's transfer stretches only the target, which Replace does not write";
    EXPECT_FALSE(fixtures::sameSamples(*plain, *secondStretched))
        << "the second image's transfer stretches what Replace writes";
}

// With no blend image chosen, OK applies the target's own stretch alone: the result is the target through its shadows
// and midtones - each sample clipped at the shadows and raised by the midtones transfer - recorded nonlinear under the
// blend's name as one step of the target's history; at the identity the target is left as it is.
TEST_F(CombinationsProcessCase, BlendWithNoBlendImageAppliesTheTargetsStretchAlone)
{
    const CombinationsProcess& under = processNamed("ImageBlend");
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples(under.scene);
    ASSERT_TRUE(scene.has_value());
    constexpr double kShadows = 0.05;
    constexpr double kMidtones = 0.25;
    {
        Targets targets = openScene(context(), under);
        ASSERT_NE(targets.target, nullptr);
        WindowHandle window = under.open(targets.target.get());
        under.configure(*window, targets, "replace");
        selectView(*window, "secondImageSelector", "");
        setDouble(*window, "srcImageBlackPointValue", kShadows);
        setDouble(*window, "srcImageMidtonesValue", kMidtones);
        const std::optional<fixtures::FixtureSamples> result = applyAndRead(context(), under, *window, targets);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        ASSERT_EQ(result->channelCount(), scene->channelCount());
        double largest = 0.0;
        for (std::uint32_t c = 0u; c < scene->channelCount(); ++c)
        {
            for (std::uint32_t y = 0u; y < scene->height(); ++y)
            {
                for (std::uint32_t x = 0u; x < scene->width(); ++x)
                {
                    const double clipped = std::clamp((scene->value(x, y, c) - kShadows) / (1.0 - kShadows), 0.0, 1.0);
                    const double expected =
                        (kMidtones - 1.0) * clipped / ((2.0 * kMidtones - 1.0) * clipped - kMidtones);
                    largest = (std::max)(largest, std::abs(result->value(x, y, c) - expected));
                }
            }
        }
        EXPECT_LE(largest, 2e-6) << "the target through its own stretch";
        EXPECT_TRUE(targets.target->viewHistory()->hasUndo()) << "the stretch is a step of the target's history";
        {
            an::Result<an::consumer::AssetReadLease> read = targets.target->document().read();
            ASSERT_TRUE(read.hasValue()) << read.status().message();
            const an::ImageMetadata& metadata = std::visit(
                [](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, read->image());
            const an::ProcessingStateRecord state = an::readProcessingStateCards(metadata.codec());
            EXPECT_EQ(state.linearity, an::RecordedLinearity::Nonlinear);
            EXPECT_EQ(state.operation, "image-blend");
        }
        window.reset();
        dropTargets(context(), targets);
    }
    {
        Targets targets = openScene(context(), under);
        ASSERT_NE(targets.target, nullptr);
        const std::optional<fixtures::FixtureSamples> before = documentSamples(targets.target->document());
        ASSERT_TRUE(before.has_value());
        WindowHandle window = under.open(targets.target.get());
        under.configure(*window, targets, "replace");
        selectView(*window, "secondImageSelector", "");
        const std::optional<fixtures::FixtureSamples> after = applyAndRead(context(), under, *window, targets);
        ASSERT_TRUE(after.has_value());
        EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        EXPECT_TRUE(fixtures::sameSamples(*before, *after)) << "the identity stretch leaves the target as it is";
        window.reset();
        dropTargets(context(), targets);
    }
}

// A stopped run's composition sees the stop: the blend's body answers Cancelled from the engine rather than running
// the composition to the end, where the framework answers any stopped run as cancelled whatever its body did.
TEST_F(CombinationsProcessCase, BlendCompositionSeesTheRunsStop)
{
    const CombinationsProcess& under = processNamed("ImageBlend");
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    const an::ui::DocumentRevision before = targets.target->document().revision();
    WindowHandle window(new StopAnsweringBlend(targets.target.get()));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    window->resize(900, 600);
    for (const char* name : {"preview", "previewCheckbox"})
    {
        if (auto* preview = window->findChild<QCheckBox*>(QString::fromLatin1(name)))
        {
            preview->setChecked(false);
        }
    }
    auto* blend = dynamic_cast<StopAnsweringBlend*>(window.get());
    ASSERT_NE(blend, nullptr);
    under.configure(*window, targets, under.changingMode);
    ASSERT_TRUE(applyAndSettle(under, *window, targets));
    ASSERT_TRUE(blend->bodyRan());
    EXPECT_EQ(blend->bodyAnswer(), an::ErrorCode::Cancelled) << "the composition stops at the run's stop";
    EXPECT_EQ(under.committed(*window), 0u);
    EXPECT_EQ(targets.target->document().revision(), before);
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    window.reset();
    dropTargets(context(), targets);
}

// ---- The LRGB combination.

// A grey target is refused by name, and a sixteen-bit luminance over a floating colour image combines as the same
// luminance held in Float32 does, within a code.
TEST_F(CombinationsProcessCase, LrgbRefusesAGreyTargetAndReadsALuminanceOfAnotherRepresentation)
{
    const CombinationsProcess& under = processNamed("LRGBCombination");
    ViewHandle grey = addedView<float>("grey", 9u, 7u, Channels::Grey,
                                       [](std::uint32_t x, std::uint32_t, std::uint32_t) { return 0.1f * x; });
    ASSERT_NE(grey, nullptr);
    EXPECT_FALSE(process_utils::holdsColourImage(grey.get())) << "the combination needs a colour target";

    const std::optional<fixtures::FixtureSamples> scene = sceneSamples(under.scene);
    ASSERT_TRUE(scene.has_value());
    auto combinedWith = [&](auto luminanceView) {
        std::optional<an::consumer::ImageAsset> colour = sceneAsset(session(), *scene);
        EXPECT_TRUE(colour.has_value());
        Targets targets;
        targets.target = assetView(std::move(*colour), "colour");
        targets.sides.push_back(SideView{.name = "luminance", .view = std::move(luminanceView)});
        WindowHandle window = under.open(targets.target.get());
        under.configure(*window, targets, "-");
        std::optional<fixtures::FixtureSamples> result = applyAndRead(context(), under, *window, targets);
        window.reset();
        dropTargets(context(), targets);
        return result;
    };
    const std::optional<fixtures::FixtureSamples> withFloat =
        combinedWith(addedView<float>("luminance", scene->width(), scene->height(), Channels::Grey,
                                      [&scene](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                          return static_cast<float>(scene->value(x, y, 1u));
                                      }));
    const std::optional<fixtures::FixtureSamples> withSixteen = combinedWith(
        addedView<std::uint16_t>("luminance", scene->width(), scene->height(), Channels::Grey,
                                 [&scene](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                     return static_cast<std::uint16_t>(std::llround(scene->value(x, y, 1u) * 65535.0));
                                 }));
    ASSERT_TRUE(withFloat.has_value() && withSixteen.has_value());
    EXPECT_EQ(withSixteen->sampleType(), fixtures::FixtureSampleType::Float32) << "the colour image's representation";
    EXPECT_LE(largestDifference(*withFloat, *withSixteen), 1.0 / 65535.0 + 1.0e-4);
    dropView(context(), grey);
}

// The combination colours a red, green and blue image: a window over a colour image offers no grey image as its
// target, and a window opened over a grey image refuses its Apply by name and changes nothing.
TEST_F(CombinationsProcessCase, LrgbRefusesAGreyTargetByNameThroughItsWindow)
{
    const CombinationsProcess& under = processNamed("LRGBCombination");
    constexpr std::uint32_t kWidth = 9u;
    constexpr std::uint32_t kHeight = 7u;
    ViewHandle colour =
        addedView<float>("colour", kWidth, kHeight, Channels::Rgb, [](std::uint32_t x, std::uint32_t, std::uint32_t c) {
            return static_cast<float>(0.1 + 0.05 * x + 0.02 * c);
        });
    ViewHandle grey = addedView<float>("grey", kWidth, kHeight, Channels::Grey,
                                       [](std::uint32_t x, std::uint32_t, std::uint32_t) { return 0.1f + 0.05f * x; });
    ViewHandle luminance =
        addedView<float>("luminance", kWidth, kHeight, Channels::Grey,
                         [](std::uint32_t, std::uint32_t y, std::uint32_t) { return 0.2f + 0.04f * y; });
    ASSERT_TRUE(colour != nullptr && grey != nullptr && luminance != nullptr);
    {
        WindowHandle offered = under.open(colour.get());
        auto* selector = widgetOf<QComboBox>(*offered, "rgbImageSelector");
        ASSERT_NE(selector, nullptr);
        EXPECT_GE(selector->findText(QStringLiteral("colour")), 0);
        EXPECT_LT(selector->findText(QStringLiteral("grey")), 0) << "a grey image is no target of the combination";
    }
    dropView(context(), colour);

    Targets targets;
    targets.target = std::move(grey);
    targets.sides.push_back(SideView{.name = "luminance", .view = std::move(luminance)});
    WindowHandle window = under.open(targets.target.get());
    under.configure(*window, targets, "-");
    ASSERT_TRUE(settleWindow(under, *window));
    const an::ui::DocumentRevision before = targets.target->document().revision();
    const qsizetype errorsBefore = context().errorTexts.size();
    ASSERT_TRUE(applyAndSettle(under, *window, targets));
    EXPECT_EQ(under.committed(*window), 0u);
    EXPECT_EQ(targets.target->document().revision(), before);
    ASSERT_TRUE(pumpUntil([this, errorsBefore]() { return context().errorTexts.size() > errorsBefore; }))
        << "the window says why";
    bool named = false;
    for (qsizetype index = errorsBefore; index < context().errorTexts.size(); ++index)
    {
        named = named || context().errorTexts[index].contains(QStringLiteral("red, green and blue"));
    }
    EXPECT_TRUE(named) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    window.reset();
    dropTargets(context(), targets);
}
