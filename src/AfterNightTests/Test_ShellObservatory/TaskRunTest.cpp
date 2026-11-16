// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A task's run in the Observatory window, as a person meets it: applied to a sequence, its page shows Overall and
/// Processing in the banner's place and Cancel, and no progress window opens; the list of every task the rail's
/// overflow item opens, one line each with its picture and where its run stands, from which a task is shown; and the
/// end of a run said to assistive technology.

#include "ObservatoryWindowCase.h"

#include "pages/TaskPage.h"
#include "tasks/TaskPresenter.h"
#include "tasks/TaskWindow.h"

#include "app/FileCompanions.h"
#include "components/AnIconRail.h"
#include "components/AnTaskHeader.h"
#include "components/RailItem.h"
#include "controls/ImageRTPreviewControl.h"
#include "controls/ProcessPageLayout.h"
#include "controls/ProgressDialogControl.h"
#include "controls/SequenceContainers.h"
#include "services/ProcessLaunchService.h"
#include "tasks/TaskModel.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <QAbstractButton>
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QPointer>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using an::ui::ProcessRunState;
using an::ui::shell::Task;
using an::ui::shell::TaskId;
using Phase = ProcessRunState::Phase;
using Window = an::ui::observatory::ObservatoryWindow;

/// A process launched on an image, a page over a view of it.
constexpr std::string_view kTargetProcess = "denoising.actionDeepSNR";
constexpr std::string_view kBrightnessContrast = "tone_brightness.actionAdjustBrightness_Contrast";

void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// Holds every thread of the pool until it is let go, so a run started meanwhile waits, its state on screen.
class PoolHold
{
public:
    PoolHold() : m_state(std::make_shared<State>()), m_threads(QThreadPool::globalInstance()->maxThreadCount())
    {
        for (int thread = 0; thread < m_threads; ++thread)
        {
            QThreadPool::globalInstance()->start([state = m_state]() {
                state->started.release();
                state->letGo.acquire();
            });
        }
        m_held = an::tests::app::processEventsUntil([this]() { return m_state->started.available() >= m_threads; });
    }

    ~PoolHold()
    {
        m_state->letGo.release(m_threads);
    }

    PoolHold(const PoolHold&) = delete;
    PoolHold& operator=(const PoolHold&) = delete;
    PoolHold(PoolHold&&) = delete;
    PoolHold& operator=(PoolHold&&) = delete;

    [[nodiscard]] bool held() const noexcept
    {
        return m_held;
    }

private:
    struct State
    {
        QSemaphore started;
        QSemaphore letGo;
    };
    std::shared_ptr<State> m_state;
    int m_threads = 0;
    bool m_held = false;
};

/// Counts the progress windows shown while it lives.
class ProgressWindowsShown final : public QObject
{
public:
    ProgressWindowsShown()
    {
        qApp->installEventFilter(this);
    }
    ~ProgressWindowsShown() override
    {
        qApp->removeEventFilter(this);
    }

    ProgressWindowsShown(const ProgressWindowsShown&) = delete;
    ProgressWindowsShown& operator=(const ProgressWindowsShown&) = delete;

    [[nodiscard]] int count() const noexcept
    {
        return m_count;
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Show && dynamic_cast<ProgressDialogControl*>(watched) != nullptr)
        {
            ++m_count;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    int m_count = 0;
};

/// What assistive technology is told while it lives: the announcements, each with how urgently.
class Announcements
{
public:
    Announcements()
    {
        s_told.clear();
        s_previous = QAccessible::installUpdateHandler([](QAccessibleEvent* event) {
            if (event->type() == QAccessible::Announcement)
            {
                const auto* announced = static_cast<const QAccessibleAnnouncementEvent*>(event);
                s_told.push_back(Told{.message = announced->message(), .politeness = announced->politeness()});
            }
        });
    }
    ~Announcements()
    {
        QAccessible::installUpdateHandler(s_previous);
    }

    Announcements(const Announcements&) = delete;
    Announcements& operator=(const Announcements&) = delete;

    struct Told
    {
        QString message;
        QAccessible::AnnouncementPoliteness politeness = QAccessible::AnnouncementPoliteness::Polite;
    };

    [[nodiscard]] static const std::vector<Told>& told()
    {
        return s_told;
    }

private:
    static inline std::vector<Told> s_told;
    static inline QAccessible::UpdateHandler s_previous = nullptr;
};

/// The cases of a task's run: each opens its images and its processes as a person does.
class TaskRunCase : public ObservatoryWindowCase
{
protected:
    void TearDown() override
    {
        ObservatoryWindowCase::TearDown();
        window().resize(1536, 864);
        settle();
    }

    [[nodiscard]] static an::ui::observatory::TaskPresenter& tasks()
    {
        an::ui::observatory::TaskPresenter* presenter = window().taskPresenter();
        EXPECT_NE(presenter, nullptr);
        return *presenter;
    }

    static TopLevelProcessBaseControl* launch(std::string_view descriptorId)
    {
        const std::size_t before = tasks().model().tasks().size();
        EXPECT_TRUE(core().processLaunch().launch(std::string(descriptorId)));
        settle();
        const std::vector<Task>& now = tasks().model().tasks();
        EXPECT_EQ(now.size(), before + 1u);
        return now.size() > before ? now.back().process.data() : nullptr;
    }

    [[nodiscard]] static an::ui::RailItem railItem(const TaskId id)
    {
        for (const an::ui::RailItem& item : window().rail()->items())
        {
            if (item.id == Window::railTaskId(id))
            {
                return item;
            }
        }
        return an::ui::RailItem{};
    }

    /// Opens a sequence of grey frames recorded in a SER file as a person's file does, the active view.
    static ImageViewControl* openSequence(const QString& name, const std::uint32_t frames)
    {
        // Made once the suite's temporary directory is in force.
        static QTemporaryDir recordings(QDir::temp().filePath(QStringLiteral("observatory-runs-XXXXXX")));
        const QString path = recordings.filePath(name + QStringLiteral(".ser"));
        {
            an::Result<an::ui::SequenceWriter> writer = an::ui::SequenceWriter::create(
                path, an::ui::SequenceWriteSettings{.container = an::SequenceFormat::Ser, .frameCount = frames});
            EXPECT_TRUE(writer.hasValue()) << writer.status().message();
            if (!writer.hasValue())
            {
                return nullptr;
            }
            an::Result<an::consumer::OperationScope> scope =
                session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
            EXPECT_TRUE(scope.hasValue()) << scope.status().message();
            if (!scope.hasValue())
            {
                return nullptr;
            }
            for (std::uint32_t frame = 0u; frame < frames; ++frame)
            {
                std::optional<an::consumer::ImageAsset> asset =
                    an::tests::app::filledAsset(session(), 0.1f + 0.1f * static_cast<float>(frame), 32u, 24u);
                EXPECT_TRUE(asset.has_value());
                if (!asset.has_value())
                {
                    return nullptr;
                }
                an::Result<an::consumer::AssetReadLease> lease = asset->read();
                EXPECT_TRUE(lease.hasValue());
                if (!lease.hasValue())
                {
                    return nullptr;
                }
                const an::Status appended = writer->append(scope->engine(), scope->execution(), lease->image());
                EXPECT_TRUE(appended.ok()) << appended.message();
            }
            an::Result<an::SequenceDescriptor> published = writer->finish(scope->execution());
            EXPECT_TRUE(published.hasValue()) << published.status().message();
        }
        an::Result<an::ui::ImageDocument> document =
            an::ui::openSequenceDocument(session(), path, an::SequenceFormat::Ser, {});
        EXPECT_TRUE(document.hasValue()) << document.status().message();
        if (!document.hasValue())
        {
            return nullptr;
        }
        ImageViewControl* const view = core().viewService().userViews().createNewImageViewControlFromSequence(
            nullptr, std::move(*document), nullptr, false, name.toStdString());
        EXPECT_NE(view, nullptr);
        if (view != nullptr)
        {
            core().viewService().setActiveImageViewControl(view);
            settle();
        }
        return view;
    }

    /// Opens tasks - DeepSNR over images of their own - until the rail cannot show them all, at the window's least
    /// size; their ids, in the order they were opened.
    static std::vector<TaskId> openTasksUntilTheRailOverflows()
    {
        window().resize(Window::kMinimumWidth, Window::kMinimumHeight);
        settle();
        std::vector<TaskId> opened;
        for (int count = 0; count < 20 && window().rail()->hiddenTaskCount() == 0; ++count)
        {
            if (openView(QStringLiteral("list-%1").arg(count)) == nullptr)
            {
                break;
            }
            TopLevelProcessBaseControl* const process = launch(kTargetProcess);
            if (process == nullptr || tasks().model().taskFor(process) == nullptr)
            {
                break;
            }
            opened.push_back(tasks().model().taskFor(process)->id);
            settle();
        }
        // Each with its image's picture.
        EXPECT_TRUE(an::tests::app::processEventsUntil([&opened]() {
            for (const TaskId id : opened)
            {
                const Task* task = tasks().model().task(id);
                if (task == nullptr || task->thumbnail.isNull())
                {
                    return false;
                }
            }
            return true;
        }));
        return opened;
    }
};

class SequenceApply : public TaskRunCase
{
};

class TaskList : public TaskRunCase
{
};

class RunEnd : public TaskRunCase
{
};

} // namespace

// Applied to a sequence, a task's page shows the run's two bars in the banner's place - Overall and Processing - and
// Cancel, the header and the rail say it runs, and no progress window opens; Cancel stops the run, and the view shows
// the sequence it was opened on.
TEST_F(SequenceApply, PageShowsTwoBarsAndCancels)
{
    ImageViewControl* const view = openSequence(QStringLiteral("frames"), 4u);
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(view->isSequence());
    const QString recording = view->document().sequence()->sourcePath;
    const QPointer<ImageRTPreviewControl> process = dynamic_cast<ImageRTPreviewControl*>(launch(kBrightnessContrast));
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(window().isTaskPageShown());
    const TaskId id = tasks().model().taskFor(process)->id;
    an::ui::ProcessPageLayout* const layout = process->pageLayout();
    ASSERT_NE(layout, nullptr);
    an::ui::observatory::TaskPage* const page = window().taskPage(id);
    ASSERT_NE(page, nullptr);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&process]() { return process->isProcessIdle(); }));

    const ProgressWindowsShown windows;
    {
        PoolHold hold;
        ASSERT_TRUE(hold.held());
        process->applyProcess();
        settle();
        EXPECT_TRUE(process->runState().running());
        EXPECT_TRUE(layout->runBanner()->isVisible());
        EXPECT_EQ(layout->runBars(), 2) << "Overall and Processing";
        EXPECT_EQ(page->header()->state(), an::ui::AnTaskHeader::State::Running);
        EXPECT_EQ(railItem(id).runPhase, an::ui::RailRunPhase::Running);
        ASSERT_TRUE(layout->cancelButton()->isVisible());
        layout->cancelButton()->click();
    }
    ASSERT_TRUE(an::tests::app::processEventsUntil([&process]() { return process->isProcessIdle(); }));
    ASSERT_TRUE(an::tests::app::processEventsUntil([view]() { return view->isWorkIdle(); }));
    settle();

    EXPECT_EQ(windows.count(), 0) << "no progress window in a page";
    EXPECT_EQ(process->runState().phase, Phase::Cancelled);
    EXPECT_EQ(layout->runBars(), 0);
    ASSERT_TRUE(view->document().sequence().has_value());
    EXPECT_EQ(QFileInfo(view->document().sequence()->sourcePath).canonicalFilePath(),
              QFileInfo(recording).canonicalFilePath())
        << "nothing published";
}

// When the tasks do not fit, the rail's overflow item counts the hidden ones and opens a list of every task, one line
// each: its picture, its process and image, and where its run stands - running, or in a window of its own.
TEST_F(TaskList, TheOverflowItemListsEveryTaskOneLineEach)
{
    const std::vector<TaskId> opened = openTasksUntilTheRailOverflows();
    ASSERT_GT(window().rail()->hiddenTaskCount(), 0) << "the tasks do not fit";
    ASSERT_GE(opened.size(), 2u);
    TopLevelProcessBaseControl* const running = tasks().model().task(opened[1])->process.data();
    ASSERT_NE(running, nullptr);
    running->setRunState(an::ui::runningState({}, 0.42));
    window().showViews();
    settle();

    const QRect overflow = window().rail()->overflowRect();
    ASSERT_TRUE(overflow.isValid());
    QTest::mouseClick(window().rail(), Qt::LeftButton, Qt::NoModifier, overflow.center());
    settle();
    QMenu* const list = window().rail()->taskList();
    ASSERT_NE(list, nullptr);
    EXPECT_TRUE(list->isVisible());
    const QList<QAction*> entries = list->actions();
    ASSERT_EQ(entries.size(), static_cast<qsizetype>(tasks().model().tasks().size())) << "every task, one line each";
    for (qsizetype index = 0; index < entries.size(); ++index)
    {
        const Task& task = tasks().model().tasks()[static_cast<std::size_t>(index)];
        QString expected = an::ui::railItemDescription(railItem(task.id));
        expected.replace(QLatin1Char('&'), QStringLiteral("&&"));
        EXPECT_EQ(entries[index]->text(), expected);
        EXPECT_TRUE(entries[index]->text().startsWith(QStringLiteral("DeepSNR - list-")));
        EXPECT_FALSE(entries[index]->icon().isNull()) << "its picture";
    }
    EXPECT_TRUE(entries[1]->text().endsWith(QStringLiteral("applying 42 %"))) << entries[1]->text().toStdString();
    list->close();
    running->setRunState(ProcessRunState{.phase = Phase::Cancelled});
}

// Choosing a task in the list shows it: its page in the centre, the current task.
TEST_F(TaskList, ChoosingAnEntryShowsTheTask)
{
    const std::vector<TaskId> opened = openTasksUntilTheRailOverflows();
    ASSERT_GT(window().rail()->hiddenTaskCount(), 0);
    ASSERT_GE(opened.size(), 2u);
    window().showViews();
    settle();
    const QRect overflow = window().rail()->overflowRect();
    ASSERT_TRUE(overflow.isValid());
    QTest::mouseClick(window().rail(), Qt::LeftButton, Qt::NoModifier, overflow.center());
    settle();
    QMenu* const list = window().rail()->taskList();
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->actions().size(), static_cast<qsizetype>(opened.size()));

    list->actions().constFirst()->trigger();
    settle();
    EXPECT_TRUE(window().isTaskPageShown());
    EXPECT_EQ(window().shownTask(), opened.front());
    EXPECT_EQ(tasks().model().current(), opened.front());
}

// The end of a run is said to assistive technology: applied to its image, politely; failed, with why, at once; and
// stopped.
TEST_F(RunEnd, IsAnnounced)
{
    ImageViewControl* const view = openView(QStringLiteral("announced"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kTargetProcess);
    ASSERT_NE(process, nullptr);
    const Announcements announcements;

    process->setRunState(an::ui::runningState());
    EXPECT_TRUE(Announcements::told().empty()) << "a run that starts is not announced";
    process->setRunState(ProcessRunState{.phase = Phase::Succeeded});
    ASSERT_EQ(Announcements::told().size(), 1u);
    EXPECT_EQ(Announcements::told().back().message, QStringLiteral("DeepSNR applied to announced"));
    EXPECT_EQ(Announcements::told().back().politeness, QAccessible::AnnouncementPoliteness::Polite);

    process->setRunState(an::ui::runningState());
    process->setRunState(ProcessRunState{.phase = Phase::Failed, .text = QStringLiteral("out of memory")});
    ASSERT_EQ(Announcements::told().size(), 2u);
    EXPECT_EQ(Announcements::told().back().message, QStringLiteral("DeepSNR failed: out of memory"));
    EXPECT_EQ(Announcements::told().back().politeness, QAccessible::AnnouncementPoliteness::Assertive);

    process->setRunState(an::ui::runningState());
    process->setRunState(ProcessRunState{.phase = Phase::Cancelled});
    ASSERT_EQ(Announcements::told().size(), 3u);
    EXPECT_EQ(Announcements::told().back().message, QStringLiteral("DeepSNR stopped"));
}

} // namespace an::tests::observatory
