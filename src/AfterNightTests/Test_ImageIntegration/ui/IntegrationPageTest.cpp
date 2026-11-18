// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Integration taken into a page of a window that holds shortcuts of its own, as a shell's presenter takes it:
/// its contents whole in the page's area with its menu bar hidden and its six menus offered, entry for entry as the
/// window's bar holds them; every key sequence the record of the process windows holds for it held by the page - the
/// page's own run with the focus inside it, the window's with the focus outside, a table's only with the focus in the
/// table, and none found ambiguous; Execute as the page's primary action, from either Enter key; closing the page as
/// closing the window - nothing asked, the session and the settings stored; and the job its header says.

#include "Test_ImageIntegration/ui/IntegrationPageCase.h"

#include "components/AnTaskHeader.h"
#include "controls/ProcessPageLayout.h"
#include "processes/ImageIntegration/widgets/StageNavigator.h"

#include <QAbstractButton>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QScrollArea>
#include <QShortcut>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTemporaryDir>

#include <memory>
#include <vector>

namespace
{

using an::tests::integration::entryText;
using an::tests::integration::IntegrationPageCase;
using an::tests::integration::kImageIntegration;
using an::tests::integration::lightFrame;
using an::tests::integration::portable;
using an::tests::integration::press;
using an::tests::integration::pump;
using an::tests::integration::pumpUntil;

/// The session a window opens on.
const QString kSession = an::tests::integration::kPageSession;

/// Every entry under some menus, each as `path|enabled`, in order.
void listEntries(const QList<QAction*>& actions, const QString& path, QStringList& entries)
{
    for (const QAction* action : actions)
    {
        if (action->isSeparator())
        {
            entries.append(path + QStringLiteral("/-"));
            continue;
        }
        const QString here = path + QLatin1Char('/') + entryText(*action);
        entries.append(here + (action->isEnabled() ? QStringLiteral("|enabled") : QStringLiteral("|disabled")));
        if (const QMenu* menu = action->menu())
        {
            listEntries(menu->actions(), here, entries);
        }
    }
}

/// The record of the process windows' shapes, read from the tree.
[[nodiscard]] QString readShapesRecord()
{
    const QString relative = QStringLiteral("src/AfterNightTests/Test_ProcessFramework/data/ProcessShapes.txt");
    QStringList seeds = {QDir::currentPath(), QFileInfo(QString::fromUtf8(__FILE__)).absolutePath(),
                         QCoreApplication::applicationDirPath()};
    for (const QString& seed : seeds)
    {
        QDir dir(seed);
        do
        {
            QFile file(dir.filePath(relative));
            if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                return QString::fromUtf8(file.readAll());
            }
        } while (dir.cdUp());
    }
    return {};
}

/// A key sequence the record holds for a window, and where it runs.
struct RecordedKeys
{
    QKeySequence keys;
    bool ofTheWindow = false;
};

/// The key sequences the record holds for Image Integration: its last field, one entry a sequence and its context, the
/// bindings of a standard key written as "the keys of" its name.
[[nodiscard]] std::vector<RecordedKeys> recordedKeys(const QString& record, int& entries)
{
    std::vector<RecordedKeys> found;
    entries = 0;
    for (const QString& line : record.split(QLatin1Char('\n')))
    {
        if (!line.startsWith(QString::fromLatin1(kImageIntegration) + QStringLiteral(" |")))
        {
            continue;
        }
        const QStringList fields = line.trimmed().split(QStringLiteral(" | "));
        for (const QString& entry : fields.back().split(QStringLiteral("; ")))
        {
            const qsizetype open = entry.indexOf(QStringLiteral(" ("));
            if (open < 0)
            {
                continue;
            }
            ++entries;
            const QString keys = entry.left(open);
            const bool ofTheWindow = entry.mid(open + 2).startsWith(QStringLiteral("window"));
            if (keys == QStringLiteral("the keys of Find"))
            {
                for (const QKeySequence& binding : QKeySequence::keyBindings(QKeySequence::Find))
                {
                    found.push_back(RecordedKeys{binding, ofTheWindow});
                }
                continue;
            }
            found.push_back(RecordedKeys{QKeySequence::fromString(keys, QKeySequence::PortableText), ofTheWindow});
        }
    }
    return found;
}

/// Counts the messages Qt logs of an ambiguous shortcut while it lives.
class AmbiguityCounter
{
public:
    AmbiguityCounter()
    {
        s_counted = 0;
        m_previous = qInstallMessageHandler(&AmbiguityCounter::handle);
    }
    ~AmbiguityCounter()
    {
        qInstallMessageHandler(m_previous);
    }
    AmbiguityCounter(const AmbiguityCounter&) = delete;
    AmbiguityCounter& operator=(const AmbiguityCounter&) = delete;

    [[nodiscard]] static int count()
    {
        return s_counted;
    }

private:
    static void handle(QtMsgType, const QMessageLogContext&, const QString& message)
    {
        if (message.contains(QStringLiteral("Ambiguous shortcut"), Qt::CaseInsensitive))
        {
            ++s_counted;
        }
    }
    static inline int s_counted = 0;
    QtMessageHandler m_previous = nullptr;
};

/// The cases of Image Integration as a page.
class IntegrationPage : public IntegrationPageCase
{
};

} // namespace

// Taken into a page, Image Integration is whole in the page's area - its stages and their pages - with its menu bar
// hidden and its six menus offered in the bar's order; and every entry of them is there as the window's bar holds it,
// enabled as it is there.
TEST_F(IntegrationPage, ItsSixMenusAreTheWindowsEntryForEntry)
{
    // The window's bar first, in a window of its own.
    QStringList ofTheWindow;
    {
        ImageIntegrationProcess& window = make();
        window.show();
        pump();
        ASSERT_FALSE(window.hasPageLayout()) << "with no presenter it is the window it is";
        const auto* const bar = window.findChild<QMenuBar*>(QStringLiteral("mainMenuBar"));
        ASSERT_NE(bar, nullptr);
        EXPECT_TRUE(bar->isVisibleTo(&window));
        listEntries(bar->actions(), QString(), ofTheWindow);
        delete m_process.data();
        pump();
    }
    ASSERT_GT(ofTheWindow.size(), 40) << "the six menus and their entries";

    ImageIntegrationProcess& process = openAsPage();
    ASSERT_TRUE(process.isPresentedAsPage());
    EXPECT_EQ(process.processSurface(), an::ui::ProcessSurface::FullPage);
    an::ui::ProcessPageLayout* const page = process.pageLayout();
    ASSERT_NE(page, nullptr);
    ASSERT_NE(page->fullArea(), nullptr);
    EXPECT_EQ(page->column(), nullptr) << "a full page has no column";
    ASSERT_NE(process.stageNavigator(), nullptr);
    const QWidget* const stages = process.stageNavigator()->widget();
    ASSERT_NE(stages, nullptr);
    EXPECT_TRUE(page->fullArea()->isAncestorOf(stages));
    EXPECT_TRUE(stages->isVisibleTo(&process));
    const auto* const bar = process.findChild<QMenuBar*>(QStringLiteral("mainMenuBar"));
    ASSERT_NE(bar, nullptr);
    EXPECT_FALSE(bar->isVisibleTo(&process)) << "its menu bar is offered, not shown";

    const QList<QMenu*> menus = page->menus();
    QStringList titles;
    QList<QAction*> entries;
    for (QMenu* menu : menus)
    {
        titles.append(menu->title());
        entries.append(menu->menuAction());
    }
    EXPECT_EQ(titles, (QStringList{QStringLiteral("Frames"), QStringLiteral("Project"), QStringLiteral("Edit"),
                                   QStringLiteral("Run"), QStringLiteral("Navigate"), QStringLiteral("Help")}));
    QStringList ofThePage;
    listEntries(entries, QString(), ofThePage);
    EXPECT_EQ(ofThePage, ofTheWindow);
}

// The page holds every key sequence the record of the process windows holds for Image Integration, and one more: the
// main Enter key beside the keypad's for Execute.
TEST_F(IntegrationPage, ItHoldsEverySequenceTheRecordHolds)
{
    const QString record = readShapesRecord();
    ASSERT_FALSE(record.isEmpty()) << "the record of the process windows was not found";
    int entries = 0;
    const std::vector<RecordedKeys> recorded = recordedKeys(record, entries);
    EXPECT_EQ(entries, 22) << "the sequences the record holds for Image Integration";

    ImageIntegrationProcess& process = openAsPage();
    ASSERT_TRUE(process.isPresentedAsPage());
    const an::ui::PageShortcutScope* const held = scope();
    ASSERT_NE(held, nullptr);
    ASSERT_TRUE(held->isActive());
    QStringList holds;
    for (const QKeySequence& keys : held->heldSequences())
    {
        holds.append(portable(keys));
    }
    QStringList wanted;
    for (const RecordedKeys& keys : recorded)
    {
        EXPECT_TRUE(holds.contains(portable(keys.keys))) << portable(keys.keys).toStdString() << " is not the page's";
        if (!wanted.contains(portable(keys.keys)))
        {
            wanted.append(portable(keys.keys));
        }
    }
    wanted.append(portable(QKeySequence(Qt::CTRL | Qt::Key_Return)));
    holds.sort();
    wanted.sort();
    EXPECT_EQ(holds, wanted);
    // No object of the page carries a live sequence while the page holds them.
    for (const QAction* candidate : process.findChildren<QAction*>())
    {
        EXPECT_TRUE(candidate->shortcut().isEmpty()) << candidate->text().toStdString();
    }
}

// Every sequence of the page's own actions and of its window-wide shortcut runs the page's with the focus inside the
// page and the window's with the focus outside it; a sequence made for a table runs only with the focus in the table;
// and Qt finds none ambiguous.
TEST_F(IntegrationPage, EverySequenceRunsThePagesOwnInsideIt)
{
    // The process's objects and their keys are read before the page takes the keys off them, and each is made to
    // count its runs instead of acting.
    m_scope.context().presenter = &m_window;
    ImageIntegrationProcess& process = make();
    struct Counted
    {
        QString what;
        QList<QKeySequence> keys;
        QPointer<QAction> action;
        QPointer<QShortcut> shortcut;
        int ran = 0;
    };
    std::vector<std::unique_ptr<Counted>> counted;
    for (QAction* candidate : process.findChildren<QAction*>())
    {
        if (candidate->shortcuts().isEmpty())
        {
            continue;
        }
        auto entry = std::make_unique<Counted>();
        entry->what = entryText(*candidate);
        entry->keys = candidate->shortcuts();
        entry->action = candidate;
        QObject::disconnect(candidate, &QAction::triggered, nullptr, nullptr);
        QObject::connect(candidate, &QAction::triggered, &process, [counter = entry.get()]() { ++counter->ran; });
        candidate->setEnabled(true);
        counted.push_back(std::move(entry));
    }
    for (QShortcut* candidate : process.findChildren<QShortcut*>())
    {
        if (candidate->keys().isEmpty())
        {
            continue;
        }
        auto entry = std::make_unique<Counted>();
        entry->what = QStringLiteral("shortcut of %1").arg(candidate->parent()->objectName());
        entry->keys = candidate->keys();
        entry->shortcut = candidate;
        QObject::disconnect(candidate, &QShortcut::activated, nullptr, nullptr);
        QObject::connect(candidate, &QShortcut::activated, &process, [counter = entry.get()]() { ++counter->ran; });
        counted.push_back(std::move(entry));
    }
    ASSERT_GE(counted.size(), 20u);
    for (const std::unique_ptr<Counted>& entry : counted)
    {
        for (const QKeySequence& keys : entry->keys)
        {
            m_window.hold(keys);
        }
    }

    m_window.show();
    process.show();
    pump();
    ASSERT_TRUE(process.isPresentedAsPage());
    ASSERT_TRUE(m_window.isActive()) << "a shortcut is looked for in the active window";
    auto* const inside = process.findChild<QPushButton*>(QStringLiteral("outputDirectoryBrowseButton"));
    ASSERT_NE(inside, nullptr);
    ASSERT_TRUE(inside->isVisibleTo(&process));
    const AmbiguityCounter ambiguity;

    // Shows a widget of the page: what holds it is told to show it, the tabs of a window or the stage list's pages.
    const auto reveal = [&process](QWidget* widget) {
        for (QWidget* at = widget; at != nullptr && at != &process; at = at->parentWidget())
        {
            auto* const stack = qobject_cast<QStackedWidget*>(at->parentWidget());
            if (stack == nullptr)
            {
                continue;
            }
            if (auto* const tabs = qobject_cast<QTabWidget*>(stack->parentWidget()))
            {
                tabs->setCurrentWidget(at);
            }
            else
            {
                stack->setCurrentWidget(at);
            }
        }
        pump();
    };
    // A widget that takes the focus in a widget or below it.
    const auto focusIn = [](QWidget* owner) -> QWidget* {
        if (owner->focusPolicy() != Qt::NoFocus)
        {
            return owner;
        }
        for (QWidget* child : owner->findChildren<QWidget*>())
        {
            if (child->focusPolicy() != Qt::NoFocus && child->isVisibleTo(owner) && child->isEnabled())
            {
                return child;
            }
        }
        return nullptr;
    };

    // The actions and the window-wide shortcut first: the page's inside, the window's outside.
    for (const std::unique_ptr<Counted>& entry : counted)
    {
        const bool wide = !entry->action.isNull() || entry->shortcut->context() == Qt::WindowShortcut ||
                          entry->shortcut->context() == Qt::ApplicationShortcut;
        if (!wide)
        {
            continue;
        }
        for (const QKeySequence& keys : entry->keys)
        {
            SCOPED_TRACE((entry->what + QStringLiteral(": ") + portable(keys)).toStdString());
            const int before = m_window.ran[portable(keys)];
            entry->ran = 0;
            press(*inside, keys);
            EXPECT_EQ(entry->ran, 1) << "the page's runs with the focus inside it";
            EXPECT_EQ(m_window.ran[portable(keys)], before) << "and the window's does not";
            press(*m_window.outside, keys);
            EXPECT_EQ(entry->ran, 1) << "the page's does not run with the focus outside it";
            EXPECT_EQ(m_window.ran[portable(keys)], before + 1) << "the window's does";
        }
    }

    // A table's own: with the focus in it. An action that holds the same key would run before it, so the actions stand
    // disabled meanwhile, as Remove Selected does with nothing selected.
    for (const std::unique_ptr<Counted>& entry : counted)
    {
        if (!entry->action.isNull())
        {
            entry->action->setEnabled(false);
        }
    }
    int tables = 0;
    for (const std::unique_ptr<Counted>& entry : counted)
    {
        if (!entry->action.isNull() || entry->shortcut->context() == Qt::WindowShortcut ||
            entry->shortcut->context() == Qt::ApplicationShortcut)
        {
            continue;
        }
        auto* const owner = qobject_cast<QWidget*>(entry->shortcut->parent());
        ASSERT_NE(owner, nullptr);
        reveal(owner);
        QWidget* const focus = focusIn(owner);
        ASSERT_NE(focus, nullptr) << entry->what.toStdString();
        ASSERT_TRUE(focus->isVisibleTo(&process)) << entry->what.toStdString();
        ++tables;
        for (const QKeySequence& keys : entry->keys)
        {
            SCOPED_TRACE((entry->what + QStringLiteral(": ") + portable(keys)).toStdString());
            const int before = m_window.ran[portable(keys)];
            entry->ran = 0;
            press(*focus, keys);
            EXPECT_EQ(entry->ran, 1) << "with the focus in it";
            EXPECT_EQ(m_window.ran[portable(keys)], before);
            press(*inside, keys);
            EXPECT_EQ(entry->ran, 1) << "not with the focus elsewhere in the page";
        }
    }
    EXPECT_GE(tables, 5) << "the tables' Return, Enter and Delete, and a frame page's Find";
    EXPECT_EQ(AmbiguityCounter::count(), 0);
}

// Execute is the page's primary action: nothing while the job is not ready or a run is on its way, and from either
// Enter key with Ctrl inside the page.
TEST_F(IntegrationPage, ExecuteIsItsPrimaryActionFromEitherEnterKey)
{
    m_scope.context().presenter = &m_window;
    ImageIntegrationProcess& process = make();
    QAction* const execute = action(QStringLiteral("Execute Integration"));
    ASSERT_NE(execute, nullptr);
    EXPECT_EQ(execute->shortcuts(), QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+Enter"))})
        << "the keypad's alone in the window";
    int executed = 0;
    QObject::disconnect(execute, &QAction::triggered, nullptr, nullptr);
    QObject::connect(execute, &QAction::triggered, &process, [&executed]() { ++executed; });
    m_window.show();
    process.show();
    pump();
    ASSERT_TRUE(process.isPresentedAsPage());
    ASSERT_TRUE(m_window.isActive());

    ASSERT_FALSE(execute->isEnabled()) << "a job with no frames is not ready";
    EXPECT_FALSE(process.triggerPrimaryAction());
    EXPECT_EQ(executed, 0);

    execute->setEnabled(true);
    EXPECT_TRUE(process.triggerPrimaryAction());
    EXPECT_EQ(executed, 1);

    auto* const inside = process.findChild<QPushButton*>(QStringLiteral("outputDirectoryBrowseButton"));
    ASSERT_NE(inside, nullptr);
    press(*inside, QKeySequence(Qt::CTRL | Qt::Key_Return));
    EXPECT_EQ(executed, 2) << "the main Enter key";
    press(*inside, QKeySequence(Qt::CTRL | Qt::Key_Enter));
    EXPECT_EQ(executed, 3) << "the keypad's";

    process.setRunState(an::ui::runningState());
    EXPECT_FALSE(process.triggerPrimaryAction()) << "not during a run";
    EXPECT_EQ(executed, 3);
    process.setRunState(an::ui::ProcessRunState{.phase = an::ui::ProcessRunState::Phase::Cancelled});
}

// Closing the page does what closing the window does: nothing is asked, the session is saved at once and the settings
// are stored.
TEST_F(IntegrationPage, ClosingAsksAsTheWindowDoes)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    struct Closed
    {
        QStringList questions;
        bool aDialogOpened = false;
        bool settingsStored = false;
        int lightsSaved = -1;
        QString outputSaved;
    };
    const auto closeIt = [this, &folder](const bool asPage) {
        Closed closed;
        forgetTheSession();
        m_scope.context().messages.clear();
        m_scope.context().presenter = asPage ? &m_window : nullptr;
        ImageIntegrationProcess& process = make();
        if (asPage)
        {
            m_window.show();
        }
        process.show();
        pump();
        EXPECT_EQ(process.isPresentedAsPage(), asPage);
        process.model()->setOutputDirectory(folder.path());
        process.model()->addFrames(FrameType::Light, {lightFrame(folder.path(), 0), lightFrame(folder.path(), 1),
                                                      lightFrame(folder.path(), 2)});
        pump();

        EXPECT_TRUE(process.close());
        closed.aDialogOpened = aDialogIsOpen();
        pump();
        EXPECT_TRUE(m_process.isNull()) << "the process goes once it is closed";
        for (const QString& message : m_scope.context().messages)
        {
            if (message.startsWith(QStringLiteral("question:")))
            {
                closed.questions.append(message);
            }
        }
        int state = 0;
        int width = 0;
        int height = 0;
        QString json;
        closed.settingsStored =
            m_scope.context().select(QStringLiteral("Image Integration"), state, width, height, json) &&
            !json.isEmpty();
        IntegrationDatabase database(ImageIntegration::applicationSessionDatabasePath());
        if (database.initialize())
        {
            const SessionLoadResult loaded = database.loadSessionResult(kSession);
            if (loaded.model.has_value())
            {
                IntegrationDataModel saved;
                saved.fromJson(*loaded.model);
                closed.lightsSaved = static_cast<int>(saved.lightFrames().size());
                closed.outputSaved = saved.outputDirectory();
            }
        }
        return closed;
    };

    const Closed window = closeIt(false);
    const Closed page = closeIt(true);
    EXPECT_TRUE(window.questions.isEmpty()) << window.questions.join(QStringLiteral(", ")).toStdString();
    EXPECT_FALSE(window.aDialogOpened);
    EXPECT_EQ(page.questions, window.questions) << "the page asks what the window asks: nothing";
    EXPECT_EQ(page.aDialogOpened, window.aDialogOpened);
    EXPECT_TRUE(window.settingsStored);
    EXPECT_TRUE(page.settingsStored);
    EXPECT_EQ(window.lightsSaved, 3);
    EXPECT_EQ(page.lightsSaved, 3) << "nothing is lost";
    EXPECT_EQ(QDir::cleanPath(page.outputSaved), QDir::cleanPath(folder.path()));
}

// On a page the process says its job for the task's header: the session's name under the process's, its facts as its
// frames say them and following them, the disk its runs write to, Undo and Redo as its acts and Execute as its primary
// act. In a window it says none.
TEST_F(IntegrationPage, ItSaysItsJobForTheHeader)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    ImageIntegrationProcess& process = openAsPage();
    an::ui::ProcessPageLayout* const page = process.pageLayout();
    ASSERT_NE(page, nullptr);
    const an::ui::JobHeading* job = page->jobHeading();
    ASSERT_NE(job, nullptr);
    EXPECT_TRUE(job->eyebrow.startsWith(QStringLiteral("Image Integration"))) << job->eyebrow.toStdString();
    EXPECT_EQ(job->title, process.sessionName());
    EXPECT_TRUE(job->facts.startsWith(QStringLiteral("0 frames"))) << job->facts.toStdString();
    ASSERT_EQ(job->acts.size(), 2u);
    EXPECT_EQ(job->acts[0].action.data(), action(QStringLiteral("Undo")));
    EXPECT_EQ(job->acts[1].action.data(), action(QStringLiteral("Redo")));
    EXPECT_EQ(job->acts[0].iconKey, QStringLiteral("undo"));
    EXPECT_EQ(job->acts[1].iconKey, QStringLiteral("redo"));
    EXPECT_EQ(job->primary.data(), action(QStringLiteral("Execute Integration")));
    EXPECT_FALSE(job->meter.has_value()) << "a job with no output folder has no disk to say";

    int told = 0;
    page->setJobHeadingListener([&told]() { ++told; });
    process.model()->setOutputDirectory(folder.path());
    process.model()->addFrames(
        FrameType::Light, {lightFrame(folder.path(), 0), lightFrame(folder.path(), 1), lightFrame(folder.path(), 2)});
    ASSERT_TRUE(pumpUntil([&]() {
        const an::ui::JobHeading* now = page->jobHeading();
        return now != nullptr && now->facts.startsWith(QStringLiteral("3 frames")) && now->meter.has_value();
    })) << (page->jobHeading() != nullptr ? page->jobHeading()->facts.toStdString() : std::string("no job"));
    job = page->jobHeading();
    EXPECT_GT(told, 0) << "the header is told when the job's facts change";
    EXPECT_TRUE(job->facts.contains(QStringLiteral("1 group"))) << job->facts.toStdString();
    EXPECT_TRUE(job->facts.contains(QStringLiteral("6m 0s"))) << job->facts.toStdString();
    EXPECT_TRUE(job->facts.contains(QStringLiteral("64x48"))) << job->facts.toStdString();
    EXPECT_TRUE(job->facts.contains(QStringLiteral("Gain 800"))) << job->facts.toStdString();
    EXPECT_TRUE(job->facts.contains(QStringLiteral("Offset 1"))) << job->facts.toStdString();
    EXPECT_EQ(job->meter->label, QStringLiteral("Disk space"));
    EXPECT_TRUE(job->meter->text.contains(QStringLiteral("free"))) << job->meter->text.toStdString();
    EXPECT_TRUE(job->meter->text.contains(QStringLiteral("need ~"))) << job->meter->text.toStdString();
    EXPECT_GE(job->meter->share, 0.0);
    EXPECT_LE(job->meter->share, 1.0);
    EXPECT_FALSE(job->meter->exceeded) << "three small frames fit the suite's disk";
}
