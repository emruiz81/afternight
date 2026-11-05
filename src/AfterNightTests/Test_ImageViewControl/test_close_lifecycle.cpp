// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Closing a view and the windows over its history, through the library: every attached process let go of before any
/// is closed, both by a close and by the question a close asks; the history window's steps jumped to by a double
/// click, stepped through by its arrows, removed and cloned from its context menu, and a sequence's step previewed at
/// the frame the view shows; steps dropped while their snapshots are on their way leaving no file; and an undo whose
/// snapshot is gone reported, with the view and its history left as they were.

#include "Test_ControlsViewer/ViewerTestContext.h"
#include "Test_ImageViewControl/history_test_support.h"

#include "controls/IAppContext.h"
#include "controls/ImageHistoryControl.h"
#include "controls/ImageHistoryTracker.h"
#include "controls/ImageViewCompanions.h"
#include "widgets/PresentationBridge.h"

#include <AfterNightConsumerRuntime/ConsumerMemory.h>

#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{

using namespace an::tests::viewer;
using an::tests::history::FakeHistoryView;
using an::tests::history::GateOpener;
using an::tests::history::historyFilesIn;
using an::tests::history::makeImage;
using an::tests::history::waitUntil;
using an::tests::history::WriterGate;
using an::ui::history::SnapshotState;

/// A path the history states in UTF-8.
[[nodiscard]] std::filesystem::path utf8Path(const std::string& path)
{
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

/// A path as Qt states it.
[[nodiscard]] std::filesystem::path localPath(const QString& path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

/// Removes every snapshot a history wrote below a directory, and says how many it removed.
std::size_t removeSnapshotsIn(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> found;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator entry(directory, error), end; !error && entry != end;
         entry.increment(error))
    {
        if (entry->path().extension() == ".ansnap")
        {
            found.push_back(entry->path());
        }
    }
    std::size_t removed = 0u;
    for (const std::filesystem::path& file : found)
    {
        removed += std::filesystem::remove(file, error) ? 1u : 0u;
    }
    return removed;
}

/// The Float32 sample a view shows at a position, or NaN when it shows none.
[[nodiscard]] float sampleAt(const ImageViewControl& view, std::uint32_t x, std::uint32_t y)
{
    const std::shared_ptr<const an::consumer::ImageAsset> asset = view.document().asset();
    const float none = std::numeric_limits<float>::quiet_NaN();
    return view.hasImage() && asset ? sampleOf<float>(*asset, x, y).value_or(none) : none;
}

/// What closing a view's attached processes came to: each process closed, with how many processes the view still held
/// when it was closed, and the processes not closed, which the log lets go when it goes.
struct ClosingLog
{
    /// One process closed.
    struct Closed
    {
        std::string name;
        std::size_t stillAttached = 0u;
    };

    ClosingLog() = default;
    ClosingLog(const ClosingLog&) = delete;
    ClosingLog& operator=(const ClosingLog&) = delete;

    ~ClosingLog()
    {
        for (an::ui::IViewAttachment* process : stillOpen)
        {
            delete process;
        }
    }

    /// The processes closed, in order.
    std::vector<Closed> closed;

    /// The processes not closed yet.
    std::set<an::ui::IViewAttachment*> stillOpen;
};

/// A process attached to a view that goes when it is closed, as a process window deleted on close does: it notes how
/// many processes the view still held, detaches itself and is deleted.
class ClosingProcess final : public an::ui::IViewAttachment
{
public:
    ClosingProcess(ImageViewControl& view, std::string name, ClosingLog& log)
        : m_View(view), m_Name(std::move(name)), m_Log(log)
    {
        m_Log.stillOpen.insert(this);
        m_View.attachProcess(*this);
    }

    [[nodiscard]] std::string attachmentName() const override
    {
        return m_Name;
    }

    [[nodiscard]] bool requiresExclusiveAccess() const override
    {
        return false;
    }

    [[nodiscard]] bool showsRealtimePreview() const override
    {
        return false;
    }

    [[nodiscard]] bool requiresExclusivePreviewAccess() const override
    {
        return false;
    }

    void disableRealtimePreview() override {}

    void detachAndClose() override
    {
        m_Log.closed.push_back(ClosingLog::Closed{m_Name, m_View.numOfAttachedProcesses()});
        m_View.detachProcess(*this);
        m_Log.stillOpen.erase(this);
        delete this;
    }

private:
    ImageViewControl& m_View;
    std::string m_Name;
    ClosingLog& m_Log;
};

/// Attaches a process that goes when it is closed to a view; the log owns it until then.
void attachClosingProcess(ImageViewControl& view, const std::string& name, ClosingLog& log)
{
    (void)new ClosingProcess(view, name, log);
}

class ImageViewControlCloseLifecycle : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_Context = openViewerContext();
        ASSERT_NE(m_Context, nullptr);
        m_Installed.emplace(*m_Context);
    }

    void TearDown() override
    {
        m_Installed.reset();
        m_Context.reset();
    }

    /// A standard view showing an image, which closing leaves for the case to delete.
    [[nodiscard]] ViewHandle closableView(const std::string& name)
    {
        ViewHandle view = makeView(name);
        view->setAttribute(Qt::WA_DeleteOnClose, false);
        std::optional<an::ui::ImageDocument> document =
            makeDocument<float>(m_Context->engineSession(), 4u, 4u, Channels::Grey,
                                [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
        EXPECT_TRUE(document.has_value());
        if (document.has_value())
        {
            view->setDocument(std::move(*document));
        }
        EXPECT_TRUE(settle(*view));
        return view;
    }

    /// Whether the context still lists a view among the views open.
    [[nodiscard]] bool listed(const ImageViewControl* view) const
    {
        const std::vector<ImageViewControl*> views = m_Context->getListOfViewControls();
        return std::find(views.begin(), views.end(), view) != views.end();
    }

    std::unique_ptr<ViewerTestContext> m_Context;
    std::optional<InstalledContext> m_Installed;
};

// Closing a view lets go of every attached process before closing any, so none is reached through it again.
TEST_F(ImageViewControlCloseLifecycle, ClosingAViewLetsGoOfEveryProcessBeforeClosingAny)
{
    ClosingLog log;
    ViewHandle view = closableView("closing");
    attachClosingProcess(*view, "Curves", log);
    attachClosingProcess(*view, "Levels", log);
    ASSERT_EQ(view->numOfAttachedProcesses(), 2u);

    EXPECT_TRUE(view->close());

    ASSERT_EQ(log.closed.size(), 2u) << "every attached process is closed with the view";
    for (const ClosingLog::Closed& closed : log.closed)
    {
        EXPECT_EQ(closed.stillAttached, 0u) << closed.name << " was closed while the view still held a process";
    }
    EXPECT_TRUE(log.stillOpen.empty());
    EXPECT_EQ(view->numOfAttachedProcesses(), 0u);
    EXPECT_FALSE(listed(view.get()));
}

// The question a close asks closes the attached processes, before the view, only when it is answered yes.
TEST_F(ImageViewControlCloseLifecycle, TheCloseQuestionClosesTheAttachedProcessesOnlyWhenAnsweredYes)
{
    ClosingLog log;
    ViewHandle view = closableView("asking");
    view->resetPendingChanges();
    attachClosingProcess(*view, "Curves", log);
    attachClosingProcess(*view, "Levels", log);

    m_Context->answerYes = false;
    view->closeRequested();
    EXPECT_TRUE(m_Context->messages.contains(QStringLiteral("question:Close image with process attached")));
    EXPECT_TRUE(log.closed.empty()) << "a no closes nothing";
    EXPECT_EQ(view->numOfAttachedProcesses(), 2u);
    EXPECT_FALSE(view->isCloseRequested());
    EXPECT_TRUE(listed(view.get())) << "the view stays open";

    m_Context->answerYes = true;
    view->closeRequested();
    ASSERT_EQ(log.closed.size(), 2u) << "a yes closes every attached process";
    for (const ClosingLog::Closed& closed : log.closed)
    {
        EXPECT_EQ(closed.stillAttached, 0u) << closed.name << " was closed while the view still held a process";
    }
    EXPECT_EQ(view->numOfAttachedProcesses(), 0u);
    EXPECT_TRUE(view->isCloseRequested());
    EXPECT_FALSE(listed(view.get())) << "and then the view closes";
}

/// The viewer's context with what the history window reaches beyond it: a temporary directory of the case's own that
/// the views' histories write their steps in, and a view factory that keeps the views a clone makes.
class HistoryWindowContext final : public ViewerTestContext,
                                   public an::ui::ITempFileService,
                                   public an::ui::IViewFactory
{
public:
    explicit HistoryWindowContext(an::consumer::EngineSession session) : ViewerTestContext(std::move(session)) {}

    ~HistoryWindowContext() override
    {
        // The views it made go before the session the viewer's context lets go.
        m_Made.clear();
    }

    [[nodiscard]] an::ui::ITempFileService* getTempFileService() override
    {
        return this;
    }

    [[nodiscard]] an::ui::IViewFactory* getViewFactory() override
    {
        return this;
    }

    // ---- ITempFileService: a directory of the case's own.

    [[nodiscard]] bool isTempFile(const std::string& fileName) override
    {
        return QString::fromStdString(fileName).startsWith(m_Temporary.path());
    }

    [[nodiscard]] std::string createTempFilePathForNewSequence(an::SequenceFormat) const override
    {
        return m_Temporary.filePath(QStringLiteral("sequence.ser")).toStdString();
    }

    [[nodiscard]] QString getTempDirPath() const override
    {
        return m_Temporary.path();
    }

    // ---- IViewFactory: the views a clone makes, kept until the case closes them.

    [[nodiscard]] ImageViewControl* createNewImageViewControlFromImage(ImageViewControl*,
                                                                       an::ui::ImageDocument&& document, QWidget*,
                                                                       const std::string& historyDesc,
                                                                       ViewControlMode mode, ImageViewControl*, bool,
                                                                       bool) override
    {
        ViewHandle view = makeView(document.names().displayName.toStdString(), mode);
        view->setDocument(std::move(document));
        view->initializeNewImageHistory(historyDesc);
        madeHistories.push_back(historyDesc);
        m_Made.push_back(std::move(view));
        return m_Made.back().get();
    }

    [[nodiscard]] ImageViewControl* createNewImageViewControlFromSequence(ImageViewControl*, an::ui::ImageDocument&&,
                                                                          QWidget*, bool, const std::string&,
                                                                          ViewControlMode, ImageViewControl*, bool,
                                                                          bool) override
    {
        // These cases clone no sequence step: no view is made, and the document stays with the caller.
        return nullptr;
    }

    /// The views the factory made, in order.
    [[nodiscard]] std::vector<ImageViewControl*> madeViews() const
    {
        std::vector<ImageViewControl*> views;
        for (const ViewHandle& view : m_Made)
        {
            views.push_back(view.get());
        }
        return views;
    }

    /// Deletes the views the factory made.
    void closeMadeViews()
    {
        m_Made.clear();
    }

    /// The history descriptions the factory's views were made with.
    std::vector<std::string> madeHistories;

private:
    QTemporaryDir m_Temporary;
    std::vector<ViewHandle> m_Made;
};

/// A history window context over a session that reads and writes FITS files; fails the case when the session cannot
/// be opened.
[[nodiscard]] std::unique_ptr<HistoryWindowContext> openHistoryWindowContext()
{
    std::optional<TestSessionProvider> provider = openFileProvider(2u);
    if (!provider.has_value())
    {
        return nullptr;
    }
    return std::make_unique<HistoryWindowContext>(std::move(provider->owned()));
}

/// The history window a view opened last, or null.
[[nodiscard]] ImageHistoryControl* historyWindowOf(const ImageViewControl& view)
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

/// Chooses an entry of the next menu that pops up, as a person does with the keyboard, and keeps what the menu
/// offered. A menu that offers the entry disabled, or not at all, is closed with nothing chosen, so the code that
/// opened it never waits for a choice; and the chooser stops looking after the patience.
class MenuChoice
{
public:
    explicit MenuChoice(QString entry)
        : m_Entry(std::move(entry)), m_Until(std::chrono::steady_clock::now() + kPatience)
    {
        QObject::connect(&m_Timer, &QTimer::timeout, [this]() { look(); });
        m_Timer.start(5);
    }

    MenuChoice(const MenuChoice&) = delete;
    MenuChoice& operator=(const MenuChoice&) = delete;

    /// The entries the menu offered that could be chosen.
    QStringList enabled;

    /// The entries the menu offered that could not.
    QStringList disabled;

    /// Whether the entry was chosen.
    bool chosen = false;

private:
    void look()
    {
        for (QWidget* window : QApplication::topLevelWidgets())
        {
            auto* menu = qobject_cast<QMenu*>(window);
            if (menu == nullptr || !menu->isVisible())
            {
                continue;
            }
            m_Timer.stop();
            QAction* wanted = nullptr;
            for (QAction* action : menu->actions())
            {
                if (action->isSeparator())
                {
                    continue;
                }
                (action->isEnabled() ? enabled : disabled).append(action->text());
                if (action->isEnabled() && action->text() == m_Entry)
                {
                    wanted = action;
                }
            }
            if (wanted == nullptr)
            {
                menu->close();
                return;
            }
            menu->setActiveAction(wanted);
            QTest::keyClick(menu, Qt::Key_Return);
            chosen = true;
            return;
        }
        if (std::chrono::steady_clock::now() > m_Until)
        {
            m_Timer.stop();
        }
    }

    QString m_Entry;
    std::chrono::steady_clock::time_point m_Until;
    QTimer m_Timer;
};

/// Right-clicks a row of the history window's table, as a person asks for its context menu there.
void rightClickRow(QTableWidget& table, int row)
{
    const QTableWidgetItem* item = table.item(row, 0);
    ASSERT_NE(item, nullptr);
    const QRect area = table.visualItemRect(item);
    ASSERT_FALSE(area.isEmpty());
    const QPoint at = area.center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, at, table.viewport()->mapToGlobal(at));
    QCoreApplication::sendEvent(table.viewport(), &event);
}

class ImageHistoryControlLifecycle : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_Context = openHistoryWindowContext();
        ASSERT_NE(m_Context, nullptr);
        m_Installed.emplace(*m_Context);
    }

    void TearDown() override
    {
        if (m_Context)
        {
            m_Context->closeMadeViews();
        }
        m_Installed.reset();
        m_Context.reset();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_Context->engineSession();
    }

    /// A standard view of a grey Float32 image whose sample at (x, y) is (x + 10 y) / 100, 3 by 2, whose history holds
    /// the image as read, a horizontal mirror and, when asked, a vertical mirror after it.
    [[nodiscard]] ViewHandle editedView(const std::string& name, bool verticalMirrorToo)
    {
        ViewHandle view = makeView(name);
        std::optional<an::ui::ImageDocument> document =
            makeDocument<float>(session(), 3u, 2u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                return static_cast<float>(x + 10u * y) / 100.0f;
            });
        EXPECT_TRUE(document.has_value());
        if (!document.has_value())
        {
            return view;
        }
        view->setDocument(std::move(*document));
        view->initializeNewImageHistory("opened");
        EXPECT_TRUE(settle(*view));
        view->horizontalMirror();
        EXPECT_TRUE(settle(*view));
        if (verticalMirrorToo)
        {
            view->verticalMirror();
            EXPECT_TRUE(settle(*view));
        }
        return view;
    }

    std::unique_ptr<HistoryWindowContext> m_Context;
    std::optional<InstalledContext> m_Installed;
};

using ImageHistoryTrackerLifecycle = ImageHistoryControlLifecycle;

// Double-clicking a step in the history window shows that step in the view and makes it the current one.
TEST_F(ImageHistoryControlLifecycle, DoubleClickingAStepInTheHistoryWindowShowsThatStep)
{
    ViewHandle view = editedView("history", true);
    an::ui::IViewHistory* history = view->viewHistory();
    ASSERT_NE(history, nullptr);
    ASSERT_EQ(history->previousStepName(), "Image Vertical Mirror");
    EXPECT_FLOAT_EQ(sampleAt(*view, 0u, 0u), 0.12f);

    view->history();
    QPointer<ImageHistoryControl> window = historyWindowOf(*view);
    ASSERT_FALSE(window.isNull());
    QTableWidget* table = window->findChild<QTableWidget*>(QStringLiteral("tableWidget"));
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 3) << "the image as read and the two mirrors";
    pumpQuietly();
    const QTableWidgetItem* initial = table->item(0, 0);
    ASSERT_NE(initial, nullptr);
    const QRect area = table->visualItemRect(initial);
    ASSERT_FALSE(area.isEmpty());

    // A double click reaches the table as a press and a release, then the second press.
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, area.center());
    QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, area.center());
    ASSERT_TRUE(pumpUntil([history]() {
        return history->previousStepName().empty() && history->nextStepName() == "Image Horizontal Mirror";
    })) << "the step double-clicked becomes the current one";
    ASSERT_TRUE(settle(*view));
    EXPECT_FLOAT_EQ(sampleAt(*view, 0u, 0u), 0.0f) << "the view shows the image as it was read";
}

// The history window's arrows step back and forward, each enabled only while there is a step that way.
TEST_F(ImageHistoryControlLifecycle, TheHistoryWindowsArrowsStepBackAndForward)
{
    ViewHandle view = editedView("arrows", false);
    an::ui::IViewHistory* history = view->viewHistory();
    ASSERT_NE(history, nullptr);
    view->history();
    QPointer<ImageHistoryControl> window = historyWindowOf(*view);
    ASSERT_FALSE(window.isNull());
    auto* back = window->findChild<QPushButton*>(QStringLiteral("moveUpButton"));
    auto* forward = window->findChild<QPushButton*>(QStringLiteral("moveDownButton"));
    ASSERT_NE(back, nullptr);
    ASSERT_NE(forward, nullptr);
    EXPECT_TRUE(back->isEnabled());
    EXPECT_FALSE(forward->isEnabled()) << "the mirror is the last step";

    back->click();
    ASSERT_TRUE(pumpUntil([history]() { return history->previousStepName().empty() && history->hasRedo(); }));
    ASSERT_TRUE(settle(*view));
    EXPECT_FLOAT_EQ(sampleAt(*view, 0u, 0u), 0.0f) << "back shows the image as it was read";
    ASSERT_TRUE(pumpUntil([back, forward]() { return !back->isEnabled() && forward->isEnabled(); }))
        << "at the first step only forward is left";

    forward->click();
    ASSERT_TRUE(pumpUntil([history]() { return history->previousStepName() == "Image Horizontal Mirror"; }));
    ASSERT_TRUE(settle(*view));
    EXPECT_FLOAT_EQ(sampleAt(*view, 0u, 0u), 0.02f) << "forward shows the mirror again";
    ASSERT_TRUE(pumpUntil([back, forward]() { return back->isEnabled() && !forward->isEnabled(); }));
}

// The history window's context menu removes a step the view does not show at once, and clones another as a view.
TEST_F(ImageHistoryControlLifecycle, TheHistoryWindowsContextMenuRemovesOneStepAndClonesAnother)
{
    ViewHandle view = editedView("history", true);
    an::ui::IViewHistory* history = view->viewHistory();
    ASSERT_NE(history, nullptr);
    view->history();
    QPointer<ImageHistoryControl> window = historyWindowOf(*view);
    ASSERT_FALSE(window.isNull());
    QTableWidget* table = window->findChild<QTableWidget*>(QStringLiteral("tableWidget"));
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 3);
    pumpQuietly();

    {
        MenuChoice removal(QStringLiteral("Remove Step"));
        rightClickRow(*table, 1);
        ASSERT_TRUE(removal.chosen) << "the menu offered " << removal.enabled.join(QStringLiteral(", ")).toStdString();
        for (const QString& offered :
             {QStringLiteral("Jump To Step"), QStringLiteral("Clone From Step"), QStringLiteral("Save History Log...")})
        {
            EXPECT_TRUE(removal.enabled.contains(offered)) << offered.toStdString();
        }
    }
    ASSERT_TRUE(pumpUntil([table]() { return table->rowCount() == 2; })) << "the horizontal mirror is removed at once";
    EXPECT_EQ(history->previousStepName(), "Image Vertical Mirror");
    EXPECT_FLOAT_EQ(sampleAt(*view, 0u, 0u), 0.12f) << "removing a step the view does not show changes nothing shown";

    {
        MenuChoice clone(QStringLiteral("Clone From Step"));
        rightClickRow(*table, 0);
        ASSERT_TRUE(clone.chosen) << "the menu offered " << clone.enabled.join(QStringLiteral(", ")).toStdString();
    }
    ASSERT_TRUE(pumpUntil([this]() { return m_Context->madeViews().size() == 1u; })) << "the clone is a new view";
    ImageViewControl* made = m_Context->madeViews().front();
    ASSERT_TRUE(settle(*made));
    EXPECT_FLOAT_EQ(sampleAt(*made, 0u, 0u), 0.0f) << "the clone shows the state the step recorded";
    ASSERT_EQ(m_Context->madeHistories.size(), 1u);
    EXPECT_EQ(m_Context->madeHistories.front(), "Cloned from 'history' step 'Initial state'");
    EXPECT_FLOAT_EQ(sampleAt(*view, 0u, 0u), 0.12f) << "and the view it was cloned from is left as it was";
}

// The history window previews a sequence step at the frame the view shows, counted from zero.
TEST_F(ImageHistoryControlLifecycle, TheHistoryWindowPreviewsTheSequenceFrameTheViewShows)
{
    // Frame f of the recording holds one value everywhere, a different one for each frame.
    static constexpr std::array<float, 3> kFrameValues{0.1f, 0.5f, 0.9f};
    const QString capture = m_Context->getTempDirPath() + QStringLiteral("/capture.ser");
    writeSerFile(session(), capture, 3u, 2u, 3u,
                 [](std::uint32_t frame, std::uint32_t, std::uint32_t) { return kFrameValues.at(frame); });
    std::optional<an::ui::ImageDocument> frame =
        makeDocument<float>(session(), 3u, 2u, Channels::Grey,
                            [](std::uint32_t, std::uint32_t, std::uint32_t) { return kFrameValues.at(1u); });
    ASSERT_TRUE(frame.has_value());
    an::ui::DocumentSequence sequence{.sourcePath = capture, .currentFrame = 1u};
    sequence.descriptor.format = an::SequenceFormat::Ser;
    sequence.descriptor.frameCount = 3u;
    frame->setSequence(std::move(sequence));
    ViewHandle view = makeView("sequence");
    view->setSequenceDocument(std::move(*frame), false);
    view->initializeNewImageHistory("opened");
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(view->getSequenceCurrentFrameIndex(), 1u) << "the frame's index counts from zero";
    EXPECT_EQ(view->getSequenceCurrentFrame(), 2u) << "the frame's number a person reads counts from one";

    view->history();
    QPointer<ImageHistoryControl> window = historyWindowOf(*view);
    ASSERT_FALSE(window.isNull());
    ASSERT_TRUE(pumpUntil([&window]() { return window && window->previewsShown() > 0u; }))
        << "the step the view shows is previewed";
    const std::shared_ptr<const an::ui::PresentedImage> preview = window->shownPreview();
    ASSERT_NE(preview, nullptr);
    const std::uint8_t* shown = preview->pixel(1u, 1u);
    ASSERT_NE(shown, nullptr);
    EXPECT_NEAR(static_cast<double>(shown[0]), 255.0 * kFrameValues.at(1u), 2.0)
        << "the preview reads the frame the view shows, not the one after it";
}

// Steps removed while their snapshots are on their way leave no file, whether being written or still waiting.
TEST_F(ImageHistoryTrackerLifecycle, StepsRemovedWhileTheirSnapshotsAreOnTheirWayLeaveNoFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    WriterGate gate;
    FakeHistoryView view;
    view.image = makeImage(session(), 8u, 6u, 1u, 1.0f);
    auto tracker = std::make_unique<ImageHistoryTracker>(view, session(), gate.writer(), directory.path());
    GateOpener opener(gate);
    tracker->registerInitialStep("opened");
    tracker->waitAllAsyncJobsDone();

    // The first change's snapshot is held at the start of its write; the next two wait for the writer behind it.
    gate.close();
    view.image = makeImage(session(), 8u, 6u, 1u, 500.0f);
    ASSERT_TRUE(tracker->registerShownState("Stretch", ""));
    ASSERT_TRUE(gate.waitForStarted(2u));
    view.image = makeImage(session(), 8u, 6u, 1u, 900.0f);
    ASSERT_TRUE(tracker->registerShownState("Mirror", ""));
    view.image = makeImage(session(), 8u, 6u, 1u, 1300.0f);
    ASSERT_TRUE(tracker->registerShownState("Rotate", ""));
    const std::vector<ImageHistoryStepSPtr> steps = tracker->getImageHistorySteps();
    ASSERT_EQ(steps.size(), 3u);
    ASSERT_EQ(steps[0]->snapshotState(), SnapshotState::Pending);
    const std::string beingWritten = steps[0]->m_Snapshot->path();
    const std::string waiting = steps[1]->m_Snapshot->path();
    const std::string kept = steps[2]->m_Snapshot->path();

    ASSERT_TRUE(tracker->removeStep(2u));
    ASSERT_TRUE(tracker->removeStep(1u));
    EXPECT_EQ(tracker->getTotalStepCount(), 2u) << "steps the view does not show go at once";
    EXPECT_EQ(tracker->getPreviousStepName(), "Rotate");

    gate.open();
    tracker->waitAllAsyncJobsDone();
    EXPECT_TRUE(waitUntil([&beingWritten]() { return !std::filesystem::exists(utf8Path(beingWritten)); }))
        << "the snapshot being written when its step went is removed once its write ends";
    EXPECT_FALSE(std::filesystem::exists(utf8Path(waiting)));
    const std::vector<std::string> writes = gate.paths();
    EXPECT_EQ(std::count(writes.begin(), writes.end(), waiting), 0)
        << "a snapshot whose step went before its write began is never written";
    EXPECT_TRUE(std::filesystem::exists(utf8Path(kept)));
    EXPECT_EQ(historyFilesIn(utf8Path(tracker->snapshotDirectory())).size(), 2u)
        << "the history keeps the files of the image as read and of the step it shows, and nothing else";
}

// An undo whose snapshot is gone is reported, and the view keeps its image and the history its current step.
TEST_F(ImageHistoryTrackerLifecycle, AnUndoWhoseSnapshotIsGoneIsReportedAndChangesNothing)
{
    ViewHandle view = editedView("history", false);
    an::ui::IViewHistory* history = view->viewHistory();
    ASSERT_NE(history, nullptr);
    ASSERT_EQ(history->previousStepName(), "Image Horizontal Mirror");
    const std::filesystem::path temporary = localPath(m_Context->getTempDirPath());
    ASSERT_TRUE(pumpUntil([this, &temporary]() {
        return historyFilesIn(temporary).size() == 2u &&
               session().memory().counters(an::consumer::MemoryCategory::PendingSnapshots).entries == 0u;
    })) << "the image as read and the mirror are written";
    ASSERT_EQ(removeSnapshotsIn(temporary), 2u);
    const std::uint64_t shown = view->document().asset()->identity();
    m_Context->messages.clear();

    ASSERT_TRUE(history->undo()) << "the restore begins: whether its step can be read is learnt on its worker";
    ASSERT_TRUE(pumpUntil([this]() { return m_Context->messages.contains(QStringLiteral("error:Image History")); }))
        << "a step that cannot be read is reported";
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(view->document().asset()->identity(), shown) << "the view keeps the image it shows";
    EXPECT_FLOAT_EQ(sampleAt(*view, 0u, 0u), 0.02f);
    EXPECT_EQ(history->previousStepName(), "Image Horizontal Mirror") << "the current step stays where it was";
    EXPECT_FALSE(history->hasRedo());
}

} // namespace
