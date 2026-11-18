// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Integration in the Observatory window, as a person meets it: a task page that holds it whole under a header
/// that says its job - the session's name, its facts, the disk its runs write to - with Undo, Redo, the menu button and
/// Execute Integration; its six menus under the menu button, entry for entry as its bar holds them; its own keys inside
/// the page and the window's outside it - Ctrl+O adds files with the focus in the page and opens an image with the
/// focus on the rail; Execute from Ctrl+Enter in the page and from Apply with the focus outside it; an import shown in
/// the page, paused, resumed and cancelled there, while the rest of the window stays usable; the frame tree's menu, a
/// settings dialog and the three tools opened from the page; and the page floated to a window of its own.

#include "ObservatoryWindowCase.h"

#include "ShellActions.h"
#include "pages/IntegrationHubPage.h"
#include "pages/TaskPage.h"
#include "tasks/TaskPresenter.h"
#include "tasks/TaskWindow.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "app/FileCompanions.h"
#include "components/AnIconButton.h"
#include "components/AnIconRail.h"
#include "components/AnTaskHeader.h"
#include "controls/IAppContext.h"
#include "controls/PageShortcutScope.h"
#include "controls/ProcessPageLayout.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "core/ApplicationCore.h"
#include "processes/ImageIntegration/ImageIntegrationProcess.h"
#include "processes/ImageIntegration/model/FrameCollection.h"
#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/persistence/IntegrationDatabase.h"
#include "processes/ImageIntegration/ui_models/IntegrationJobsModel.h"
#include "services/ProcessLaunchService.h"
#include "tasks/TaskModel.h"
#include "theme/AnDesignTokens.h"

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeView>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using an::ui::observatory::IntegrationHubPage;
using an::ui::shell::Task;
namespace shellIds = an::ui::observatory::ids;
namespace actionIds = an::ui::shell::ids;

constexpr int kWaitMs = 20000;

/// Lets the window settle: posted work delivered, the deletions done, the layouts applied.
void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// An entry's text as its menu draws it: without the mnemonic's mark, and without the keys a page writes after a tab.
[[nodiscard]] QString entryText(const QAction& action)
{
    QString text = action.text();
    const qsizetype tab = text.indexOf(QLatin1Char('\t'));
    if (tab >= 0)
    {
        text.truncate(tab);
    }
    return text.remove(QLatin1Char('&'));
}

/// Every action under some entries, their menus' among them, in order.
void collect(const QList<QAction*>& entries, QList<QAction*>& into)
{
    for (QAction* entry : entries)
    {
        into.append(entry);
        if (const QMenu* menu = entry->menu())
        {
            collect(menu->actions(), into);
        }
    }
}

/// Whether a widget is of a class, by the class's name: a dialog of the processes library is known here by its name.
[[nodiscard]] bool isOfClass(const QWidget& widget, const char* name)
{
    return QString::fromLatin1(typeid(widget).name()).contains(QLatin1String(name));
}

/// The dialog of a class that stands open; null for none.
[[nodiscard]] QDialog* openDialogOf(const char* name)
{
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        auto* const dialog = qobject_cast<QDialog*>(widget);
        if (dialog != nullptr && dialog->isVisible() && isOfClass(*dialog, name))
        {
            return dialog;
        }
    }
    return nullptr;
}

/// The dialogs that stand open, each by its class and its title: what a case that waited for another one says.
[[nodiscard]] std::string openDialogs()
{
    QStringList found;
    for (const QWidget* widget : QApplication::topLevelWidgets())
    {
        const auto* const dialog = qobject_cast<const QDialog*>(widget);
        if (dialog == nullptr || !dialog->isVisible())
        {
            continue;
        }
        QString said = QString::fromLatin1(typeid(*dialog).name()) + QStringLiteral(" \"") + dialog->windowTitle() +
                       QLatin1Char('"');
        for (const QLabel* label : dialog->findChildren<QLabel*>())
        {
            if (!label->text().isEmpty())
            {
                said += QStringLiteral(" [") + label->text().left(120) + QLatin1Char(']');
                break;
            }
        }
        found.append(said);
    }
    return found.isEmpty() ? std::string("no dialog is open") : found.join(QStringLiteral("; ")).toStdString();
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

/// The cases of Image Integration's page: each launches it as its menu entry does, on the session it keeps outside any
/// project, emptied before and after.
class IntegrationPage : public ObservatoryWindowCase
{
protected:
    void SetUp() override
    {
        ObservatoryWindowCase::SetUp();
        forgetTheSession();
    }

    void TearDown() override
    {
        // Image Integration saves the session it shows as it closes: its page goes before the session is removed.
        ObservatoryWindowCase::TearDown();
        settle();
        forgetTheSession();
        window().integrationJobs().refresh();
        (void)an::tests::app::processEventsUntil([]() { return !window().integrationJobs().isReading(); }, kWaitMs);
    }

    static void forgetTheSession()
    {
        IntegrationDatabase database(ImageIntegration::applicationSessionDatabasePath());
        if (database.initialize())
        {
            (void)database.deleteSession(QStringLiteral("last_session"));
        }
    }

    [[nodiscard]] static an::ui::observatory::TaskPresenter& tasks()
    {
        return *window().taskPresenter();
    }

    /// Launches Image Integration as its menu entry does; the process, null when none opened.
    static ImageIntegrationProcess* launch()
    {
        EXPECT_TRUE(core().processLaunch().launch(IntegrationHubPage::kImageIntegration));
        settle();
        for (TopLevelProcessBaseControl* process : core().openProcesses().snapshot())
        {
            if (auto* const integration = dynamic_cast<ImageIntegrationProcess*>(process))
            {
                return integration;
            }
        }
        return nullptr;
    }

    [[nodiscard]] static an::ui::observatory::TaskPage* pageOf(const TopLevelProcessBaseControl* process)
    {
        const Task* const task = tasks().model().taskFor(process);
        return task != nullptr ? window().taskPage(task->id) : nullptr;
    }

    /// An action of the process by the text of its entry; null for none.
    [[nodiscard]] static QAction* actionOf(const QWidget& process, const QString& text)
    {
        for (QAction* candidate : process.findChildren<QAction*>())
        {
            if (entryText(*candidate) == text)
            {
                return candidate;
            }
        }
        return nullptr;
    }

    /// A folder for the files a case writes, made once the suite's temporary directory is in force.
    [[nodiscard]] static QString folder()
    {
        static QTemporaryDir made(QDir::temp().filePath(QStringLiteral("observatory-integration-XXXXXX")));
        return made.path();
    }

    /// Writes a small FITS image; its path, empty when it was not written.
    static QString writeFrame(const QString& name, const float value)
    {
        const QString path = QDir(folder()).filePath(name);
        std::optional<an::consumer::ImageAsset> asset = an::tests::app::filledAsset(session(), value);
        EXPECT_TRUE(asset.has_value());
        if (!asset.has_value())
        {
            return {};
        }
        const an::Result<an::OutputIntent> intent = an::ui::appContext()->getOutputIntentProvider()->buildOutputIntent(
            *asset->descriptor(), an::CodecFormat::Fits);
        EXPECT_TRUE(intent.hasValue()) << intent.status().message();
        if (!intent.hasValue() || !an::ui::writeViewImage(session(), *asset, path, *intent, {}).ok())
        {
            ADD_FAILURE() << "the frame was not written: " << path.toStdString();
            return {};
        }
        return path;
    }

    /// Puts light frames of files a case wrote into the job, as an import leaves them.
    static void addLights(ImageIntegrationProcess& process, const QStringList& files)
    {
        std::vector<FrameInfo> frames;
        for (const QString& file : files)
        {
            frames.emplace_back(FrameId(file), file,
                                FrameMetadata(FrameType::Light, QSize(64, 48), 120.0, -10.0, 800, 1,
                                              QStringLiteral("L"), QStringLiteral("M 42"),
                                              QDateTime::currentDateTimeUtc(), QString()));
        }
        process.model()->addFrames(FrameType::Light, frames);
        settle();
    }

    /// The frames of every kind the job holds.
    [[nodiscard]] static int framesOf(const ImageIntegrationProcess& process)
    {
        const IntegrationDataModel& model = *process.model();
        return static_cast<int>(model.biasFrames().size() + model.darkFrames().size() + model.flatFrames().size() +
                                model.lightFrames().size());
    }

    /// Presses a key with the focus on a widget of the window: through the window's shortcuts where the platform makes
    /// the window active, and otherwise as the key reaches the widget - its override, which a page answers - or, for a
    /// widget outside every page, by the catalogue's action the key is the shortcut of.
    static void press(QWidget& focus, const Qt::Key key, const Qt::KeyboardModifiers modifiers,
                      QAction* const catalogueAction = nullptr)
    {
        const bool active = an::tests::app::activateWindow(*focus.window());
        focus.setFocus(Qt::OtherFocusReason);
        settle();
        if (active)
        {
            EXPECT_EQ(QApplication::focusWidget(), &focus);
            QTest::keyClick(&focus, key, modifiers);
        }
        else if (catalogueAction != nullptr)
        {
            catalogueAction->trigger();
        }
        else
        {
            QKeyEvent override(QEvent::ShortcutOverride, key, modifiers);
            QCoreApplication::sendEvent(&focus, &override);
        }
        settle();
    }

    /// A widget of the page that takes the focus and edits with no key.
    [[nodiscard]] static QPushButton* insideOf(const ImageIntegrationProcess& process)
    {
        return process.findChild<QPushButton*>(QStringLiteral("outputDirectoryBrowseButton"));
    }

    /// The panel of an import on its way in the page; null for none shown.
    [[nodiscard]] static QWidget* importPanelOf(const ImageIntegrationProcess& process)
    {
        auto* const panel = process.findChild<QWidget*>(QStringLiteral("importProgressPanel"));
        return panel != nullptr && panel->isVisibleTo(&process) ? panel : nullptr;
    }
};

} // namespace

// Image Integration opens as a task page: whole in the page's area, under a header that says its job - the session's
// name, its facts, and the disk once it has an output folder - with Undo, Redo, the menu button and Execute
// Integration, which waits for a job that is ready.
TEST_F(IntegrationPage, OpensAsATaskPageUnderAHeaderThatSaysItsJob)
{
    ImageIntegrationProcess* const process = launch();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->isPresentedAsPage());
    EXPECT_EQ(process->processSurface(), an::ui::ProcessSurface::FullPage);
    ASSERT_TRUE(window().isTaskPageShown());
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    const an::ui::ProcessPageLayout* const layout = process->pageLayout();
    ASSERT_NE(layout, nullptr);
    ASSERT_NE(layout->fullArea(), nullptr);
    const auto* const tabs = process->findChild<QTabWidget*>(QStringLiteral("rootTabWidget"));
    ASSERT_NE(tabs, nullptr);
    EXPECT_TRUE(layout->fullArea()->isAncestorOf(tabs));
    EXPECT_TRUE(tabs->isVisibleTo(page));
    EXPECT_FALSE(process->findChild<QMenuBar*>(QStringLiteral("mainMenuBar"))->isVisibleTo(process));

    an::ui::AnTaskHeader* const header = page->header();
    ASSERT_TRUE(header->job().has_value()) << "the header says the job";
    EXPECT_EQ(header->job()->title, process->sessionName());
    EXPECT_TRUE(header->job()->eyebrow.startsWith(QStringLiteral("Image Integration")));
    EXPECT_GT(header->height(), an::ui::tokens().metrics.strip) << "three lines, not a strip";
    EXPECT_TRUE(header->menuButton()->isVisibleTo(page));
    const QList<QAbstractButton*> acts = header->jobActButtons();
    ASSERT_EQ(acts.size(), 2);
    EXPECT_FALSE(acts[0]->isEnabled()) << "nothing to undo yet";
    QAbstractButton* const execute = header->jobPrimaryButton();
    ASSERT_NE(execute, nullptr);
    EXPECT_EQ(execute->text(), QStringLiteral("Execute Integration"));
    EXPECT_FALSE(execute->isEnabled()) << "a job with no frames is not ready";
    EXPECT_FALSE(header->job()->meter.has_value());

    QTemporaryDir output(QDir(folder()).filePath(QStringLiteral("job-XXXXXX")));
    ASSERT_TRUE(output.isValid());
    process->model()->setOutputDirectory(output.path());
    addLights(*process,
              {QDir(folder()).filePath(QStringLiteral("a.fit")), QDir(folder()).filePath(QStringLiteral("b.fit")),
               QDir(folder()).filePath(QStringLiteral("c.fit"))});
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() {
            return header->job().has_value() && header->job()->facts.startsWith(QStringLiteral("3 frames")) &&
                   header->job()->meter.has_value();
        },
        kWaitMs))
        << header->job()->facts.toStdString();
    EXPECT_TRUE(header->job()->meter->text.contains(QStringLiteral("free")));
    EXPECT_FALSE(header->jobMeterRect().isEmpty()) << "the disk's bar is drawn at the window's size";
}

// The six menus are under the header's menu button, in the bar's order, and every entry of the bar is there: the
// process's own action, enabled as it is in the bar.
TEST_F(IntegrationPage, MenusEqualTheMenuBar)
{
    ImageIntegrationProcess* const process = launch();
    ASSERT_NE(process, nullptr);
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    const auto* const bar = process->findChild<QMenuBar*>(QStringLiteral("mainMenuBar"));
    ASSERT_NE(bar, nullptr);
    QList<QAction*> ofTheBar;
    collect(bar->actions(), ofTheBar);
    ASSERT_GT(ofTheBar.size(), 40);

    std::unique_ptr<QMenu> menu(page->makeProcessMenu(nullptr));
    QStringList titles;
    for (const QAction* entry : menu->actions())
    {
        titles.append(entryText(*entry));
    }
    EXPECT_EQ(titles, (QStringList{QStringLiteral("Frames"), QStringLiteral("Project"), QStringLiteral("Edit"),
                                   QStringLiteral("Run"), QStringLiteral("Navigate"), QStringLiteral("Help")}));
    QList<QAction*> underTheButton;
    collect(menu->actions(), underTheButton);
    ASSERT_EQ(underTheButton.size(), ofTheBar.size());
    for (qsizetype index = 0; index < ofTheBar.size(); ++index)
    {
        EXPECT_EQ(underTheButton[index], ofTheBar[index]) << entryText(*ofTheBar[index]).toStdString();
        EXPECT_EQ(underTheButton[index]->isEnabled(), ofTheBar[index]->isEnabled());
    }
    // An entry still says its keys, and the menu button opens the menus.
    const QAction* const addFiles = actionOf(*process, QStringLiteral("Auto-sort by file type..."));
    ASSERT_NE(addFiles, nullptr);
    EXPECT_TRUE(addFiles->text().contains(QLatin1Char('\t'))) << addFiles->text().toStdString();
    page->header()->menuButton()->click();
    QMenu* shown = nullptr;
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() {
            shown = page->findChild<QMenu*>(QStringLiteral("observatoryTaskProcessMenu"));
            return shown != nullptr && shown->isVisible();
        },
        kWaitMs));
    EXPECT_EQ(shown->actions().size(), 6);
    shown->close();
    settle();
}

// With the focus in the page Ctrl+O adds files to the job and opens no image; with the focus on the rail it opens an
// image and adds nothing; and Qt finds the two shortcuts of one key ambiguous in neither place.
TEST_F(IntegrationPage, ItsShortcutsWinInsideThePage)
{
    const QString first = writeFrame(QStringLiteral("keys_light_1.fits"), 0.2f);
    const QString second = writeFrame(QStringLiteral("keys_light_2.fits"), 0.3f);
    ASSERT_FALSE(first.isEmpty() || second.isEmpty());
    ImageIntegrationProcess* const process = launch();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->isPresentedAsPage());
    QPushButton* const inside = insideOf(*process);
    ASSERT_NE(inside, nullptr);
    const auto said = std::make_shared<an::tests::app::Answered>();
    an::tests::app::answerEveryMessageBox(said);
    const AmbiguityCounter ambiguity;
    const std::size_t viewsBefore = views().size();

    an::tests::app::Answered chosen;
    an::tests::app::chooseInTheNextFileDialog({first, second}, &chosen);
    press(*inside, Qt::Key_O, Qt::ControlModifier);
    ASSERT_TRUE(chosen.seen) << "Ctrl+O in the page asks for the files to add";
    EXPECT_EQ(chosen.title, QStringLiteral("Select Files"));
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return framesOf(*process) == 2; }, kWaitMs))
        << "the files are the job's frames";
    settle();
    EXPECT_EQ(views().size(), viewsBefore) << "and no image opened";

    QAction* const open = core().actions().action(actionIds::kOpen);
    ASSERT_NE(open, nullptr);
    an::tests::app::Answered opened;
    an::tests::app::chooseInTheNextFileDialog({first}, &opened);
    press(*window().rail(), Qt::Key_O, Qt::ControlModifier, open);
    ASSERT_TRUE(opened.seen) << "Ctrl+O on the rail asks for the image to open";
    EXPECT_NE(opened.title, QStringLiteral("Select Files"));
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return views().size() == viewsBefore + 1u; }, kWaitMs))
        << "the image opened";
    EXPECT_EQ(framesOf(*process), 2) << "and the job gained nothing";
    EXPECT_EQ(AmbiguityCounter::count(), 0);
}

// Execute Integration runs from Ctrl+Enter with the focus in the page, from the header's button, and through Apply
// with the focus outside the page.
TEST_F(IntegrationPage, CtrlEnterExecutes)
{
    ImageIntegrationProcess* const process = launch();
    ASSERT_NE(process, nullptr);
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    QAction* const execute = actionOf(*process, QStringLiteral("Execute Integration"));
    ASSERT_NE(execute, nullptr);
    // The act is counted in place of run: a run is another case's.
    int executed = 0;
    QObject::disconnect(execute, &QAction::triggered, nullptr, nullptr);
    QObject::connect(execute, &QAction::triggered, process, [&executed]() { ++executed; });
    QPushButton* const inside = insideOf(*process);
    ASSERT_NE(inside, nullptr);

    press(*inside, Qt::Key_Return, Qt::ControlModifier);
    EXPECT_EQ(executed, 0) << "a job that is not ready does not run";
    execute->setEnabled(true);
    press(*inside, Qt::Key_Return, Qt::ControlModifier);
    EXPECT_EQ(executed, 1) << "from the page";

    ASSERT_TRUE(page->header()->jobPrimaryButton()->isEnabled());
    page->header()->jobPrimaryButton()->click();
    EXPECT_EQ(executed, 2) << "from the header";

    QAction* const apply = window().shellActions().action(shellIds::kApplyTask);
    ASSERT_NE(apply, nullptr);
    press(*window().rail(), Qt::Key_Return, Qt::ControlModifier, apply);
    EXPECT_FALSE(process->isAncestorOf(QApplication::focusWidget())) << "the focus is outside the page";
    EXPECT_EQ(executed, 3) << "through Apply with the focus outside it";
}

// An import is shown in the page, not in a dialog over the window: its file, its counts, Pause, Resume and Cancel.
// Paused, the page's own acts wait for it while the rest of the window stays usable; resumed, its frames join the job;
// and Esc in the page cancels one on its way and leaves the page open.
TEST_F(IntegrationPage, ImportShowsProgressInPlace)
{
    QStringList files;
    for (int index = 0; index < 6; ++index)
    {
        files.append(writeFrame(QStringLiteral("import_light_%1.fits").arg(index), 0.1f + 0.05f * index));
        ASSERT_FALSE(files.back().isEmpty());
    }
    ImageIntegrationProcess* const process = launch();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->isPresentedAsPage());
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    const auto said = std::make_shared<an::tests::app::Answered>();
    an::tests::app::answerEveryMessageBox(said);
    QAction* const addFiles = actionOf(*process, QStringLiteral("Auto-sort by file type..."));
    ASSERT_NE(addFiles, nullptr);
    const auto* const tabs = process->findChild<QTabWidget*>(QStringLiteral("rootTabWidget"));
    ASSERT_NE(tabs, nullptr);

    an::tests::app::Answered chosen;
    an::tests::app::chooseInTheNextFileDialog(files, &chosen);
    addFiles->trigger();
    ASSERT_TRUE(chosen.seen);
    // The panel is there before the first file is read, and nothing stands over the window.
    QWidget* const panel = importPanelOf(*process);
    ASSERT_NE(panel, nullptr) << "the import's progress is in the page";
    EXPECT_TRUE(page->isAncestorOf(panel));
    EXPECT_EQ(QApplication::activeModalWidget(), nullptr) << "no dialog stands over the window";
    auto* const pause = panel->findChild<QPushButton*>(QStringLiteral("importPause"));
    auto* const cancel = panel->findChild<QPushButton*>(QStringLiteral("importCancel"));
    ASSERT_TRUE(pause != nullptr && cancel != nullptr);
    EXPECT_EQ(pause->text(), QStringLiteral("Pause"));
    pause->click();
    EXPECT_EQ(pause->text(), QStringLiteral("Resume"));
    settle();
    QTest::qWait(150);
    EXPECT_EQ(framesOf(*process), 0) << "a paused import reads nothing";
    EXPECT_FALSE(tabs->isEnabled()) << "the page's own controls wait for the import";
    EXPECT_FALSE(addFiles->isEnabled());
    EXPECT_TRUE(pause->isEnabled());
    EXPECT_TRUE(cancel->isEnabled());

    // The rest of the window is usable meanwhile: Home, and back to the task.
    const Task* const task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    window().showHome();
    settle();
    EXPECT_FALSE(window().isTaskPageShown());
    ASSERT_TRUE(tasks().show(task->id));
    settle();
    ASSERT_TRUE(window().isTaskPageShown());
    ASSERT_NE(importPanelOf(*process), nullptr) << "the import is where it was";

    pause->click();
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return framesOf(*process) == files.size(); }, kWaitMs))
        << "resumed, the import ends and its frames are the job's";
    settle();
    EXPECT_EQ(importPanelOf(*process), nullptr) << "the panel goes with the import";
    EXPECT_TRUE(tabs->isEnabled());
    EXPECT_TRUE(addFiles->isEnabled());

    // A second import, cancelled by Esc: nothing joins the job and the page stays.
    const QString more = writeFrame(QStringLiteral("import_light_more.fits"), 0.6f);
    ASSERT_FALSE(more.isEmpty());
    an::tests::app::Answered again;
    an::tests::app::chooseInTheNextFileDialog({more}, &again);
    addFiles->trigger();
    ASSERT_TRUE(again.seen);
    QWidget* const second = importPanelOf(*process);
    ASSERT_NE(second, nullptr);
    second->findChild<QPushButton*>(QStringLiteral("importPause"))->click();
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(process, &escape);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return importPanelOf(*process) == nullptr; }, kWaitMs));
    settle();
    EXPECT_EQ(framesOf(*process), files.size()) << "a cancelled import adds nothing";
    EXPECT_TRUE(process->isPresentedAsPage()) << "Esc cancelled the import, not the task";
    EXPECT_TRUE(tabs->isEnabled());
}

// The frame tree's menu opens over the page with its entries, and Blink Selected... opens the blink comparator from
// the page.
TEST_F(IntegrationPage, TheFrameTreeMenuOpensOnThePageAndBlinks)
{
    const QStringList files = {writeFrame(QStringLiteral("tree_light_1.fits"), 0.2f),
                               writeFrame(QStringLiteral("tree_light_2.fits"), 0.3f),
                               writeFrame(QStringLiteral("tree_light_3.fits"), 0.4f)};
    ASSERT_FALSE(files.contains(QString()));
    ImageIntegrationProcess* const process = launch();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->isPresentedAsPage());
    addLights(*process, files);
    auto* const sources = process->findChild<QTabWidget*>(QStringLiteral("sourceTabWidget"));
    auto* const lights = process->findChild<QWidget*>(QStringLiteral("lightTab"));
    ASSERT_TRUE(sources != nullptr && lights != nullptr);
    sources->setCurrentWidget(lights);
    settle();
    auto* const tree = lights->findChild<QTreeView*>();
    ASSERT_NE(tree, nullptr);
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() { return tree->model() != nullptr && tree->model()->rowCount() > 0; }, kWaitMs));
    tree->expandAll();
    settle();
    const QModelIndex group = tree->model()->index(0, 0);
    ASSERT_GE(tree->model()->rowCount(group), 2) << "the group's frames";
    const QModelIndex one = tree->model()->index(0, 0, group);
    const QModelIndex two = tree->model()->index(1, 0, group);
    tree->selectionModel()->select(one, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    tree->selectionModel()->select(two, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    settle();

    // The menu is answered as it opens: its entries read, Blink Selected... chosen.
    QStringList entries;
    bool blinked = false;
    an::tests::app::answerWhenShown([&entries, &blinked](QWidget* widget) {
        auto* const menu = qobject_cast<QMenu*>(widget);
        if (menu == nullptr)
        {
            return false;
        }
        QAction* blink = nullptr;
        for (QAction* entry : menu->actions())
        {
            if (!entry->isSeparator())
            {
                entries.append(entry->text());
            }
            if (entry->text() == QStringLiteral("Blink Selected..."))
            {
                blink = entry;
            }
        }
        if (blink != nullptr)
        {
            blink->trigger();
            blinked = true;
        }
        menu->close();
        return true;
    });
    const QPoint at = tree->visualRect(one).center();
    QContextMenuEvent asked(QContextMenuEvent::Mouse, at, tree->viewport()->mapToGlobal(at));
    QCoreApplication::sendEvent(tree->viewport(), &asked);
    settle();
    EXPECT_TRUE(entries.contains(QStringLiteral("Remove Selected")))
        << entries.join(QStringLiteral(", ")).toStdString();
    ASSERT_TRUE(blinked) << "the menu offers Blink Selected... for two frames: "
                         << entries.join(QStringLiteral(", ")).toStdString();

    QDialog* blink = nullptr;
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() { return (blink = openDialogOf("BlinkComparatorDialog")) != nullptr; }, kWaitMs));
    EXPECT_EQ(blink->parentWidget(), process) << "the comparator is the page's";
    blink->reject();
    settle();
}

// A settings dialog opens from the page - Registration's, from the button of its group - and from it the manual
// registration points and the comet tracking.
TEST_F(IntegrationPage, ASettingsDialogAndTheToolsOpenFromThePage)
{
    const QStringList files = {writeFrame(QStringLiteral("tools_light_1.fits"), 0.2f),
                               writeFrame(QStringLiteral("tools_light_2.fits"), 0.3f),
                               writeFrame(QStringLiteral("tools_light_3.fits"), 0.4f)};
    ASSERT_FALSE(files.contains(QString()));
    ImageIntegrationProcess* const process = launch();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->isPresentedAsPage());
    const auto said = std::make_shared<an::tests::app::Answered>();
    addLights(*process, files);
    auto* const root = process->findChild<QTabWidget*>(QStringLiteral("rootTabWidget"));
    auto* const workflow = process->findChild<QTabWidget*>(QStringLiteral("workflowTabWidget"));
    auto* const registration = process->findChild<QWidget*>(QStringLiteral("registrationTab"));
    ASSERT_TRUE(root != nullptr && workflow != nullptr && registration != nullptr);
    root->setCurrentIndex(1);
    workflow->setCurrentWidget(registration);
    settle();
    auto* const groups = registration->findChild<QTableWidget*>(QStringLiteral("tblRegistrationGroups"));
    ASSERT_NE(groups, nullptr);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return groups->rowCount() > 0; }, kWaitMs))
        << "the lights' registration group";
    groups->selectRow(0);
    settle();

    // The group's settings open from its button, as in the window.
    auto* const open = registration->findChild<QPushButton*>(QStringLiteral("btnGroupSettings"));
    ASSERT_NE(open, nullptr);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return open->isEnabled(); }, kWaitMs))
        << "a group is selected: " << groups->rowCount() << " rows, " << groups->selectionModel()->selectedRows().size()
        << " selected";
    open->click();
    QDialog* settings = nullptr;
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() { return (settings = openDialogOf("RegistrationSettingsDialog")) != nullptr; }, kWaitMs))
        << "the button opens the group's settings";
    EXPECT_TRUE(process->isAncestorOf(settings->parentWidget())) << "the dialog is the page's";

    // Each tool is its registration method's: the method is chosen, and its button opens the tool.
    auto* const method = settings->findChild<QComboBox*>(QStringLiteral("registrationMethodComboBox"));
    auto* const manual = settings->findChild<QAbstractButton*>(QStringLiteral("editManualPointsButton"));
    auto* const comet = settings->findChild<QAbstractButton*>(QStringLiteral("trackCometButton"));
    ASSERT_TRUE(method != nullptr && manual != nullptr && comet != nullptr);
    ASSERT_GE(method->findData(QStringLiteral("manual_points")), 0);
    method->setCurrentIndex(method->findData(QStringLiteral("manual_points")));
    settle();
    ASSERT_TRUE(manual->isEnabled());
    manual->click();
    QDialog* points = nullptr;
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() { return (points = openDialogOf("ManualRegistrationDialog")) != nullptr; }, kWaitMs))
        << "the manual registration points; open: " << openDialogs();
    points->reject();
    settle();

    ASSERT_GE(method->findData(QStringLiteral("comet_tracking")), 0);
    method->setCurrentIndex(method->findData(QStringLiteral("comet_tracking")));
    settle();
    ASSERT_TRUE(comet->isEnabled());
    comet->click();
    QDialog* tracking = nullptr;
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() { return (tracking = openDialogOf("CometTrackingDialog")) != nullptr; }, kWaitMs))
        << "the comet tracking; open: " << openDialogs();
    tracking->reject();
    settle();
    settings->reject();
    settle();
}

// Floated, the task is the same page in a window of its own: its header still says the job, with Attach in Float's
// place, and its keys are still the page's.
TEST_F(IntegrationPage, FloatedItIsTheSamePageWithItsKeys)
{
    ImageIntegrationProcess* const process = launch();
    ASSERT_NE(process, nullptr);
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    const Task* const task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    const an::ui::shell::TaskId id = task->id;
    QAction* const execute = actionOf(*process, QStringLiteral("Execute Integration"));
    ASSERT_NE(execute, nullptr);
    int executed = 0;
    QObject::disconnect(execute, &QAction::triggered, nullptr, nullptr);
    QObject::connect(execute, &QAction::triggered, process, [&executed]() { ++executed; });
    execute->setEnabled(true);

    ASSERT_TRUE(window().floatTask(id));
    settle();
    ASSERT_NE(window().taskWindow(id), nullptr);
    EXPECT_EQ(pageOf(process), page) << "the same page";
    EXPECT_FALSE(window().isTaskPageShown());
    EXPECT_TRUE(page->header()->job().has_value());
    EXPECT_TRUE(page->header()->isFloating());
    EXPECT_EQ(page->header()->floatButton()->text(), QStringLiteral("Attach"));
    EXPECT_TRUE(process->hasPageLayout());

    // Its keys are the page's: the override reaches the page and runs Execute.
    QPushButton* const inside = insideOf(*process);
    ASSERT_NE(inside, nullptr);
    QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_Return, Qt::ControlModifier);
    QCoreApplication::sendEvent(inside, &override);
    EXPECT_TRUE(override.isAccepted());
    EXPECT_EQ(executed, 1);

    ASSERT_TRUE(window().attachTask(id));
    settle();
    EXPECT_TRUE(window().isTaskPageShown());
    EXPECT_FALSE(page->header()->isFloating());
}

} // namespace an::tests::observatory
