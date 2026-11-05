// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The history a real image view reaches through its companion seam: an edit recorded before the view shows it, its
/// copy waited for on the edit's worker while the GUI thread turns, an edit refused before it is shown, undo and redo
/// through the view, a sequence frame edit written before it is shown, the window over the history, a lineage carried
/// to a view made from another, and the history's directory reclaimed when the view closes.

#include "Test_ControlsHistory/HistoryTestContext.h"

#include "controls/ImageHistoryControl.h"
#include "controls/ImageViewCompanions.h"
#include "widgets/HistogramDisplayData.h"
#include "widgets/HistogramWidget.h"
#include "widgets/PresentationBridge.h"

#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <QCoreApplication>
#include <QDialog>
#include <QPointer>
#include <QTableWidget>
#include <QTimer>

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace
{

using namespace an::tests::history;
using an::tests::viewer::Channels;
using an::tests::viewer::InstalledContext;
using an::tests::viewer::makeDocument;
using an::tests::viewer::makeView;
using an::tests::viewer::sampleOf;
using an::tests::viewer::settle;
using an::tests::viewer::ViewHandle;

/// A grey Float32 image whose sample at (x, y) is (x + 10 y) / 100.
std::optional<an::ui::ImageDocument> numberedDocument(const an::consumer::EngineSession& session,
                                                      std::uint32_t width = 3u, std::uint32_t height = 2u)
{
    return makeDocument<float>(
        session, width, height, Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<float>(x + 10u * y) / 100.0f; });
}

/// The first sample of an asset at a position, an integer sample read as a fraction of its representation's largest
/// value, as a frame read back from a recording is; NaN when it cannot be read.
double fractionAt(const an::consumer::ImageAsset& asset, std::uint32_t x, std::uint32_t y)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!lease.hasValue())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::visit(
        [x, y]<typename ImageT>(const ImageT& typed) -> double {
            const auto view = typed.view();
            const auto* sample = view.has_value() ? view->sampleAt(0u, x, y, 0u) : nullptr;
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
        lease->image());
}

/// The history directories a temporary directory holds.
std::vector<std::filesystem::path> historyDirectoriesIn(const QString& directory)
{
    std::vector<std::filesystem::path> found;
#ifdef _WIN32
    const std::filesystem::path root(directory.toStdWString());
#else
    const std::filesystem::path root(directory.toStdString());
#endif
    std::error_code error;
    for (std::filesystem::directory_iterator entry(root, error), end; !error && entry != end; entry.increment(error))
    {
        if (entry->path().filename().string().rfind("AN_HISTORY_", 0) == 0)
        {
            found.push_back(entry->path());
        }
    }
    return found;
}

/// The history window a view opened last, or null.
ImageHistoryControl* historyWindowOf(ImageViewControl& view)
{
    ImageHistoryControl* found = nullptr;
    for (QDialog* dialog : view.findChildren<QDialog*>())
    {
        if (auto* window = dynamic_cast<ImageHistoryControl*>(dialog))
        {
            found = window;
        }
    }
    return found;
}

class ViewHistoryTest : public ::testing::Test
{
protected:
    void open(const an::consumer::MemoryAllowances& memory = {})
    {
        m_Context = openHistoryContext(memory);
        ASSERT_NE(m_Context, nullptr);
        m_Installed.emplace(*m_Context);
    }

    void TearDown() override
    {
        m_Installed.reset();
        m_Context.reset();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_Context->engineSession();
    }

    /// A standard view showing a numbered image, its initial state recorded and written.
    [[nodiscard]] ViewHandle shownView(const std::string& name)
    {
        ViewHandle view = makeView(name);
        std::optional<an::ui::ImageDocument> document = numberedDocument(session());
        EXPECT_TRUE(document.has_value());
        view->setDocument(std::move(*document));
        view->initializeNewImageHistory("opened");
        EXPECT_TRUE(settle(*view));
        EXPECT_TRUE(written(1u));
        return view;
    }

    /// Turns the event loop until the views' histories hold `count` written snapshots and nothing is pending.
    [[nodiscard]] bool written(std::size_t count) const
    {
        const QString directory = m_Context->getTempDirPath();
        return pumpUntil([this, directory, count]() {
            std::size_t files = 0u;
            for (const auto& own : historyDirectoriesIn(directory))
            {
                files += historyFilesIn(own).size();
            }
            return files >= count &&
                   session().memory().counters(an::consumer::MemoryCategory::PendingSnapshots).entries == 0u;
        });
    }

    std::unique_ptr<HistoryTestContext> m_Context;
    std::optional<InstalledContext> m_Installed;
};

TEST_F(ViewHistoryTest, AnEditIsRecordedBeforeTheViewShowsItAndUndoneThroughTheView)
{
    open();
    ViewHandle view = shownView("mirror");
    an::ui::IViewHistory* history = view->viewHistory();
    ASSERT_NE(history, nullptr);
    const std::uint64_t opened = view->document().asset()->identity();

    view->horizontalMirror();
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(view->workTelemetry().editsCommitted, 1u);
    ASSERT_TRUE(history->hasUndo());
    EXPECT_EQ(history->previousStepName(), "Image Horizontal Mirror");
    const std::uint64_t mirrored = view->document().asset()->identity();
    EXPECT_FLOAT_EQ(*sampleOf<float>(*view->document().asset(), 0u, 0u), 0.02f);

    view->undo();
    ASSERT_TRUE(pumpUntil([&history]() { return !history->hasUndo(); }));
    ASSERT_TRUE(settle(*view));
    EXPECT_FLOAT_EQ(*sampleOf<float>(*view->document().asset(), 0u, 0u), 0.0f);
    EXPECT_NE(view->document().asset()->identity(), opened) << "a restored state is a new asset";
    EXPECT_NE(view->document().asset()->identity(), mirrored);
    EXPECT_TRUE(history->hasRedo());

    view->redo();
    ASSERT_TRUE(pumpUntil([&history]() { return !history->hasRedo(); }));
    ASSERT_TRUE(settle(*view));
    EXPECT_FLOAT_EQ(*sampleOf<float>(*view->document().asset(), 0u, 0u), 0.02f);
}

TEST_F(ViewHistoryTest, AnEditWaitsForRoomOnItsWorkerWhileTheGuiThreadTurns)
{
    open();
    ViewHandle view = shownView("held");
    // Something else holds the session's pending-snapshot allowance: both of its entries.
    an::consumer::ConsumerMemory& memory = session().memory();
    an::Result<an::consumer::MemoryReservation> firstHeld =
        memory.reserve(an::consumer::MemoryCategory::PendingSnapshots, 1u);
    an::Result<an::consumer::MemoryReservation> secondHeld =
        memory.reserve(an::consumer::MemoryCategory::PendingSnapshots, 1u);
    ASSERT_TRUE(firstHeld.hasValue());
    ASSERT_TRUE(secondHeld.hasValue());
    std::optional<an::consumer::MemoryReservation> first(std::move(*firstHeld));
    std::optional<an::consumer::MemoryReservation> second(std::move(*secondHeld));

    view->horizontalMirror();
    std::atomic_int ticks = 0;
    QTimer ticker;
    QObject::connect(&ticker, &QTimer::timeout, [&ticks]() { ++ticks; });
    ticker.start(5);
    pumpQuietly();
    pumpQuietly();
    EXPECT_EQ(view->workTelemetry().editsCommitted, 0u) << "the edit was shown before its copy was admitted";
    EXPECT_FALSE(view->isWorkIdle()) << "the edit waits on its worker";
    EXPECT_GT(ticks.load(), 0) << "the GUI thread's event loop turned meanwhile";
    EXPECT_FALSE(view->viewHistory()->hasUndo());

    first.reset();
    second.reset();
    ASSERT_TRUE(settle(*view));
    ticker.stop();
    EXPECT_EQ(view->workTelemetry().editsCommitted, 1u);
    EXPECT_TRUE(view->viewHistory()->hasUndo());
}

TEST_F(ViewHistoryTest, AnEditWhoseCopyCannotBeKeptIsNeverShown)
{
    an::consumer::MemoryAllowances memory;
    memory.pendingSnapshots.bytes = 8u;
    memory.pendingSnapshots.admitsOneOversized = false;
    open(memory);
    ViewHandle view = shownView("refused");
    const std::uint64_t before = view->document().asset()->identity();

    view->rotate180Degree();
    ASSERT_TRUE(settle(*view));

    EXPECT_EQ(view->document().asset()->identity(), before);
    EXPECT_FLOAT_EQ(*sampleOf<float>(*view->document().asset(), 0u, 0u), 0.0f);
    EXPECT_EQ(view->workTelemetry().editsDiscarded, 1u);
    EXPECT_EQ(view->workTelemetry().editsCommitted, 0u);
    EXPECT_FALSE(view->viewHistory()->hasUndo());
    ASSERT_TRUE(pumpUntil([this]() { return m_Context->messages.contains(QStringLiteral("error:Image History")); }))
        << "the history says why the edit was not applied";
}

TEST_F(ViewHistoryTest, ASequenceFrameEditIsWrittenBeforeItIsShownAndUndoneToTheSequencesFrame)
{
    open();
    // Frame f of the recording holds 0.1 (f + 1) + 0.2 x at (x, y); SER keeps a sample to a 65,535th.
    const auto sample = [](std::uint32_t frame, std::uint32_t x, std::uint32_t) {
        return 0.1f * static_cast<float>(frame + 1u) + 0.2f * static_cast<float>(x);
    };
    constexpr float kSerStep = 1.0f / 65535.0f;
    const QString capture = m_Context->getTempDirPath() + QStringLiteral("/capture.ser");
    an::tests::viewer::writeSerFile(session(), capture, 3u, 2u, 3u, sample);
    std::optional<an::ui::ImageDocument> frame =
        makeDocument<float>(session(), 3u, 2u, Channels::Grey,
                            [&sample](std::uint32_t x, std::uint32_t y, std::uint32_t) { return sample(1u, x, y); });
    ASSERT_TRUE(frame.has_value());
    an::ui::DocumentSequence sequence{.sourcePath = capture, .currentFrame = 1u};
    sequence.descriptor.format = an::SequenceFormat::Ser;
    sequence.descriptor.frameCount = 3u;
    frame->setSequence(std::move(sequence));
    ViewHandle view = makeView("sequence");
    view->setSequenceDocument(std::move(*frame), false);
    view->initializeNewImageHistory("opened");
    ASSERT_TRUE(settle(*view));

    view->horizontalMirror();
    ASSERT_TRUE(settle(*view));
    ASSERT_EQ(view->workTelemetry().editsCommitted, 1u);
    EXPECT_NEAR(fractionAt(*view->document().asset(), 0u, 0u), sample(1u, 2u, 0u), kSerStep);
    EXPECT_TRUE(written(1u)) << "the changed frame was written before the edit was shown";

    ASSERT_TRUE(view->viewHistory()->undo());
    ASSERT_TRUE(pumpUntil([&view]() { return !view->viewHistory()->hasUndo(); }));
    ASSERT_TRUE(settle(*view));
    EXPECT_NEAR(fractionAt(*view->document().asset(), 0u, 0u), sample(1u, 0u, 0u), kSerStep)
        << "leaving the frame step shows the sequence's own frame, read from the recording";
    EXPECT_EQ(view->getSequenceCurrentFrameIndex(), 1u);

    ASSERT_TRUE(view->viewHistory()->redo());
    ASSERT_TRUE(pumpUntil([&view]() { return !view->viewHistory()->hasRedo(); }));
    ASSERT_TRUE(settle(*view));
    EXPECT_NEAR(fractionAt(*view->document().asset(), 0u, 0u), sample(1u, 2u, 0u), kSerStep);
}

TEST_F(ViewHistoryTest, TheHistoryWindowListsAndPreviewsTheStepsAndGoesWithTheView)
{
    open();
    ViewHandle view = shownView("window");
    view->horizontalMirror();
    ASSERT_TRUE(settle(*view));

    view->history();
    QPointer<ImageHistoryControl> window = historyWindowOf(*view);
    ASSERT_FALSE(window.isNull());
    const auto* table = window->findChild<QTableWidget*>(QStringLiteral("tableWidget"));
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->rowCount(), 2) << "the initial state and the mirror";
    ASSERT_TRUE(pumpUntil([&window]() { return window && window->previewsShown() > 0u; }));
    ASSERT_NE(window->shownPreview(), nullptr);
    EXPECT_EQ(window->shownPreview()->width(), 3u);
    auto* histogram =
        dynamic_cast<HistogramWidget*>(window->findChild<QWidget*>(QStringLiteral("historyStepHistogramWidget")));
    ASSERT_NE(histogram, nullptr);
    ASSERT_TRUE(pumpUntil([&histogram]() {
        const std::shared_ptr<const an::ui::HistogramDisplayData> counts = histogram->getHistogram();
        return counts != nullptr && counts->isValid();
    })) << "the histogram counts the step it previews";

    view.reset();
    pumpQuietly();
    EXPECT_TRUE(window.isNull()) << "the window goes with the view whose history it shows";
}

TEST_F(ViewHistoryTest, AFourChannelStepIsPreviewedWithItsAlphaUnsheared)
{
    open();
    ViewHandle view = makeView("alpha");
    std::optional<an::ui::ImageDocument> document = makeDocument<std::uint8_t>(
        session(), 4u, 2u, Channels::Rgba, [](std::uint32_t x, std::uint32_t, std::uint32_t channel) {
            if (channel == 3u)
            {
                return static_cast<std::uint8_t>(x < 2u ? 255u : 0u);
            }
            return static_cast<std::uint8_t>(channel == 0u ? 200u : (channel == 1u ? 100u : 50u));
        });
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    view->initializeNewImageHistory("opened");
    ASSERT_TRUE(settle(*view));
    ASSERT_TRUE(written(1u));

    view->history();
    QPointer<ImageHistoryControl> window = historyWindowOf(*view);
    ASSERT_FALSE(window.isNull());
    ASSERT_TRUE(pumpUntil([&window]() { return window && window->shownPreview() != nullptr; }));
    const auto preview = window->shownPreview();
    ASSERT_EQ(preview->format(), an::ui::PresentedFormat::Rgba8) << "four channels are wrapped as four bytes a pixel";
    const std::uint8_t* opaque = preview->pixel(1u, 1u);
    const std::uint8_t* clear = preview->pixel(3u, 1u);
    ASSERT_NE(opaque, nullptr);
    ASSERT_NE(clear, nullptr);
    EXPECT_EQ(opaque[0], 200u);
    EXPECT_EQ(opaque[1], 100u);
    EXPECT_EQ(opaque[2], 50u);
    EXPECT_EQ(opaque[3], 255u);
    EXPECT_EQ(clear[3], 0u) << "the alpha of a pixel is its own, not a neighbour's colour";
}

TEST_F(ViewHistoryTest, AViewMadeFromAnotherContinuesItsLineage)
{
    open();
    ViewHandle source = shownView("source");
    source->horizontalMirror();
    ASSERT_TRUE(settle(*source));
    ViewHandle made = shownView("made");

    made->viewHistory()->inheritLineage(*source->viewHistory());
    made->history();
    QPointer<ImageHistoryControl> window = historyWindowOf(*made);
    ASSERT_FALSE(window.isNull());
    const auto* table = window->findChild<QTableWidget*>(QStringLiteral("tableWidget"));
    ASSERT_NE(table, nullptr);
    // One heading for the source's entries, its two steps, the separator, then the made view's own initial state.
    EXPECT_EQ(table->rowCount(), 5);
}

TEST_F(ViewHistoryTest, ClosingTheViewReclaimsItsHistoryDirectory)
{
    open();
    ViewHandle view = shownView("closing");
    view->horizontalMirror();
    ASSERT_TRUE(settle(*view));
    ASSERT_TRUE(written(2u));
    ASSERT_EQ(historyDirectoriesIn(m_Context->getTempDirPath()).size(), 1u);

    view.reset();
    pumpQuietly();

    EXPECT_TRUE(historyDirectoriesIn(m_Context->getTempDirPath()).empty());
}

} // namespace
