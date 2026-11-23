// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Integration in the Observatory window, as a person meets it: a task page that holds it whole under a header
/// that says its job - the session's name, its facts, the disk its runs write to - with Undo, Redo, the menu button and
/// Execute Integration; its six menus under the menu button, entry for entry as its bar holds them; its own keys inside
/// the page and the window's outside it - Ctrl+O adds files with the focus in the page and opens an image with the
/// focus on the rail; Execute from Ctrl+Enter in the page and from Apply with the focus outside it; an import shown in
/// the page, paused, resumed and cancelled there, while the rest of the window stays usable; the frame tree's menu, a
/// settings dialog and the three tools opened from the page; the page floated to a window of its own; its stages as a
/// list that a click, the Navigate menu and its keys move, each row saying where its stage stands; the list's foot
/// with the output folder and the project file's acts - export, import, the recent projects, the presets, Reset
/// Settings; and the Summary stage, which lists the runs the job's output folder keeps and keeps the session's notes
/// with the job.

#include "ObservatoryWindowCase.h"
#include "Test_ShellObservatory/KeptWindowLook.h"

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
#include "components/AnStageList.h"
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
#include "processes/ImageIntegration/widgets/StageListNavigator.h"
#include "processes/ImageIntegration/widgets/StageNavigator.h"
#include "services/ProcessLaunchService.h"
#include "tasks/TaskModel.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

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
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
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
using ImageIntegration::StageListNavigator;
using ImageIntegration::StagePage;
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

    /// The page's stages: the list the process shows them as.
    [[nodiscard]] static StageListNavigator* stagesOf(const ImageIntegrationProcess& process)
    {
        return dynamic_cast<StageListNavigator*>(process.stageNavigator());
    }

    /// The row of a stage as the list shows it now; a row with no id for none.
    [[nodiscard]] static an::ui::StageRow rowOf(const ImageIntegrationProcess& process, const char* id)
    {
        if (const StageListNavigator* const stages = stagesOf(process))
        {
            for (const an::ui::StageRow& row : stages->list()->rows())
            {
                if (row.id == QLatin1String(id))
                {
                    return row;
                }
            }
        }
        return {};
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
    const StageListNavigator* const stages = stagesOf(*process);
    ASSERT_NE(stages, nullptr) << "its stages are a list";
    EXPECT_TRUE(layout->fullArea()->isAncestorOf(stages->widget()));
    EXPECT_TRUE(stages->widget()->isVisibleTo(page));
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
    ASSERT_NE(process->stageNavigator(), nullptr);
    const QWidget* const stages = process->stageNavigator()->widget();
    ASSERT_NE(stages, nullptr);

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
    EXPECT_FALSE(stages->isEnabled()) << "the page's own controls wait for the import";
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
    EXPECT_TRUE(stages->isEnabled());
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
    EXPECT_TRUE(stages->isEnabled());
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
    auto* const lights = process->findChild<QWidget*>(QStringLiteral("lightTab"));
    ASSERT_TRUE(process->stageNavigator() != nullptr && lights != nullptr);
    process->stageNavigator()->setCurrentPage(StagePage::Lights);
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
    expectTheLookOfAKeptWindow(*blink);
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
    auto* const registration = process->findChild<QWidget*>(QStringLiteral("registrationTab"));
    ASSERT_TRUE(process->stageNavigator() != nullptr && registration != nullptr);
    process->stageNavigator()->setCurrentPage(StagePage::Registration);
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
    expectTheLookOfAKeptWindow(*settings);

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
    expectTheLookOfAKeptWindow(*points);
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
    expectTheLookOfAKeptWindow(*tracking);
    tracking->reject();
    settle();
    settings->reject();
    settle();
}

// A group's stacking settings open from the Stacking stage's button, and from them the weighting options, the local
// normalization settings and the recommended settings: each a window of the page's with the look of one that stays a
// window.
TEST_F(IntegrationPage, TheStackingSettingsAndWhatTheyOpenOpenFromThePage)
{
    const QStringList files = {writeFrame(QStringLiteral("stack_light_1.fits"), 0.2f),
                               writeFrame(QStringLiteral("stack_light_2.fits"), 0.3f),
                               writeFrame(QStringLiteral("stack_light_3.fits"), 0.4f)};
    ASSERT_FALSE(files.contains(QString()));
    ImageIntegrationProcess* const process = launch();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->isPresentedAsPage());
    addLights(*process, files);
    auto* const stacking = process->findChild<QWidget*>(QStringLiteral("stackingTab"));
    ASSERT_TRUE(process->stageNavigator() != nullptr && stacking != nullptr);
    process->stageNavigator()->setCurrentPage(StagePage::Stacking);
    settle();
    auto* const groups = stacking->findChild<QTableWidget*>(QStringLiteral("tblIntegrationGroups"));
    ASSERT_NE(groups, nullptr);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return groups->rowCount() > 0; }, kWaitMs))
        << "the lights' stacking group";
    groups->selectRow(0);
    settle();
    auto* const open = stacking->findChild<QPushButton*>(QStringLiteral("btnStackingSettings"));
    ASSERT_NE(open, nullptr);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return open->isEnabled(); }, kWaitMs));
    open->click();
    QDialog* settings = nullptr;
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() { return (settings = openDialogOf("StackingSettingsDialog")) != nullptr; }, kWaitMs))
        << "the button opens the group's stacking settings; open: " << openDialogs();
    EXPECT_TRUE(process->isAncestorOf(settings->parentWidget())) << "the dialog is the page's";
    settle();
    expectTheLookOfAKeptWindow(*settings);

    // The weighting options: offered once the frames are weighted.
    auto* const weighting = settings->findChild<QComboBox*>(QStringLiteral("weightingSelector"));
    auto* const weightingOptions = settings->findChild<QPushButton*>(QStringLiteral("weightingOptionsButton"));
    ASSERT_TRUE(weighting != nullptr && weightingOptions != nullptr);
    weighting->setCurrentText(QStringLiteral("Number of stars"));
    settle();
    ASSERT_TRUE(weightingOptions->isEnabled()) << "weighted by the number of stars, the options are not offered";
    weightingOptions->click();
    settle();
    auto* const options = settings->findChild<QDialog*>(QStringLiteral("stackingWeightingOptionsDialog"));
    ASSERT_TRUE(options != nullptr && options->isVisible()) << "the weighting options; open: " << openDialogs();
    expectTheLookOfAKeptWindow(*options);
    options->reject();
    settle();

    // The local normalization settings: offered once local normalization is on.
    auto* const normalize = settings->findChild<QAbstractButton*>(QStringLiteral("localNormalizationEnabled"));
    auto* const configure = settings->findChild<QPushButton*>(QStringLiteral("localNormalizationConfigureButton"));
    ASSERT_TRUE(normalize != nullptr && configure != nullptr);
    if (!normalize->isChecked())
    {
        normalize->click();
        settle();
    }
    ASSERT_TRUE(configure->isEnabled()) << "with local normalization on, its settings are not offered";
    configure->click();
    QDialog* local = nullptr;
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() { return (local = openDialogOf("LocalNormalizationSettingsDialog")) != nullptr; }, kWaitMs))
        << "the local normalization settings; open: " << openDialogs();
    settle();
    expectTheLookOfAKeptWindow(*local);
    local->reject();
    settle();

    // The recommended settings: offered for a group that holds frames.
    auto* const recommended = settings->findChild<QPushButton*>(QStringLiteral("recommendedButton"));
    ASSERT_NE(recommended, nullptr);
    ASSERT_TRUE(recommended->isEnabled()) << "the group holds frames, and no recommendation is offered";
    recommended->click();
    QDialog* recommendation = nullptr;
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() { return (recommendation = openDialogOf("StackingRecommendationDialog")) != nullptr; }, kWaitMs))
        << "the recommended settings; open: " << openDialogs();
    settle();
    expectTheLookOfAKeptWindow(*recommendation);
    recommendation->reject();
    settle();
    settings->reject();
    settle();
}

// A group's calibration settings open from the Calibration stage's button, a window of the page's with the look of
// one that stays a window.
TEST_F(IntegrationPage, TheCalibrationSettingsOpenFromThePage)
{
    const QStringList files = {writeFrame(QStringLiteral("calibrate_light_1.fits"), 0.2f),
                               writeFrame(QStringLiteral("calibrate_light_2.fits"), 0.3f)};
    ASSERT_FALSE(files.contains(QString()));
    ImageIntegrationProcess* const process = launch();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->isPresentedAsPage());
    addLights(*process, files);
    auto* const calibration = process->findChild<QWidget*>(QStringLiteral("calibrationTab"));
    ASSERT_TRUE(process->stageNavigator() != nullptr && calibration != nullptr);
    process->stageNavigator()->setCurrentPage(StagePage::Calibration);
    settle();
    auto* const groups = calibration->findChild<QTableWidget*>(QStringLiteral("tblLightCalibration"));
    ASSERT_NE(groups, nullptr);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return groups->rowCount() > 0; }, kWaitMs))
        << "the lights' calibration group";
    auto* const open = calibration->findChild<QPushButton*>(QStringLiteral("btnLightCalibrationSettings"));
    ASSERT_NE(open, nullptr);
    // The table is filled again as the frames' facts are read, and a row selected before that is no longer: the
    // group is selected once the table has stopped changing, as a person selects it.
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() {
            if (groups->rowCount() > 0 && groups->selectionModel()->selectedRows().isEmpty())
            {
                groups->selectRow(0);
            }
            return open->isEnabled();
        },
        kWaitMs))
        << "a group is selected, and its calibration settings are not offered: " << groups->rowCount() << " rows, "
        << groups->selectionModel()->selectedRows().size() << " selected, the first one's group \""
        << (groups->item(0, 0) != nullptr ? groups->item(0, 0)->data(Qt::UserRole).toString().toStdString()
                                          : std::string("none"))
        << "\"";
    open->click();
    QDialog* settings = nullptr;
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() { return (settings = openDialogOf("LightCalibrationSettingsDialog")) != nullptr; }, kWaitMs))
        << "the button opens the group's calibration settings; open: " << openDialogs();
    EXPECT_TRUE(process->isAncestorOf(settings->parentWidget())) << "the dialog is the page's";
    settle();
    expectTheLookOfAKeptWindow(*settings);
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

// The page's stages are a list at its left: a click on a row shows its page, the Navigate menu under the header's
// button and its keys in the page move the selection, and each row says where its stage stands - a source with no
// frames not started, the lights ready once there are some.
TEST_F(IntegrationPage, TheStagesAreAListThatNavigates)
{
    ImageIntegrationProcess* const process = launch();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->isPresentedAsPage());
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    StageListNavigator* const stages = stagesOf(*process);
    ASSERT_NE(stages, nullptr);
    an::ui::AnStageList* const list = stages->list();
    ASSERT_TRUE(list->isVisibleTo(page));
    EXPECT_EQ(stages->column()->width(), an::ui::parts::kStageColumnWidth);
    EXPECT_EQ(stages->column()->mapTo(process, QPoint(0, 0)).x(), 0) << "the list is at the page's left";
    ASSERT_EQ(list->rowCount(), ImageIntegration::kStagePageCount) << "a row for each page";
    EXPECT_EQ(stages->currentRow(), QStringLiteral("bias")) << "it opens on the page the window opens on";

    // A click on a row shows its page.
    const auto click = [list](const int row) {
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                          list->visualRect(list->model()->index(row, 0)).center());
        settle();
    };
    click(5);
    EXPECT_EQ(stages->currentRow(), QStringLiteral("registration"));
    EXPECT_EQ(stages->currentPage(), StagePage::Registration);
    EXPECT_TRUE(process->findChild<QWidget*>(QStringLiteral("registrationTab"))->isVisibleTo(page));
    EXPECT_FALSE(process->findChild<QWidget*>(QStringLiteral("biasTab"))->isVisibleTo(page));
    click(6);
    EXPECT_EQ(stages->currentRow(), QStringLiteral("stacking")) << "the row after the registration's";
    EXPECT_EQ(stages->currentPage(), StagePage::Stacking);
    EXPECT_TRUE(process->findChild<QWidget*>(QStringLiteral("stackingTab"))->isVisibleTo(page));
    EXPECT_FALSE(process->findChild<QWidget*>(QStringLiteral("registrationTab"))->isVisibleTo(page));

    // The Navigate menu, under the header's button, moves the selection.
    std::unique_ptr<QMenu> menu(page->makeProcessMenu(nullptr));
    QAction* toSummary = nullptr;
    QAction* toLights = nullptr;
    for (const QAction* top : menu->actions())
    {
        if (entryText(*top) != QStringLiteral("Navigate") || top->menu() == nullptr)
        {
            continue;
        }
        for (QAction* entry : top->menu()->actions())
        {
            if (entryText(*entry) == QStringLiteral("Summary"))
            {
                toSummary = entry;
            }
            if (entryText(*entry) == QStringLiteral("Lights"))
            {
                toLights = entry;
            }
        }
    }
    ASSERT_TRUE(toSummary != nullptr && toLights != nullptr);
    toSummary->trigger();
    settle();
    EXPECT_EQ(stages->currentRow(), QStringLiteral("summary"));
    EXPECT_TRUE(process->findChild<QWidget*>(QStringLiteral("summaryTab"))->isVisibleTo(page));

    // Its keys are the page's: Ctrl+4 shows the lights.
    QPushButton* const inside = insideOf(*process);
    ASSERT_NE(inside, nullptr);
    ASSERT_TRUE(inside->isVisibleTo(page));
    press(*inside, Qt::Key_4, Qt::ControlModifier);
    EXPECT_EQ(stages->currentRow(), QStringLiteral("lights"));
    EXPECT_TRUE(process->findChild<QWidget*>(QStringLiteral("lightTab"))->isVisibleTo(page));

    // The rows follow the job.
    // A job with no lights cannot run: its validation says so of the lights' page, and the row with it.
    EXPECT_EQ(rowOf(*process, "lights").line, QStringLiteral("1 issue · no frames"));
    EXPECT_EQ(rowOf(*process, "lights").status, an::ui::StageStatus::Failed);
    EXPECT_FALSE(rowOf(*process, "lights").toolTip.isEmpty()) << "the issue is spelled out in the row's tooltip";
    EXPECT_EQ(rowOf(*process, "darks").line, QStringLiteral("no frames"));
    EXPECT_EQ(rowOf(*process, "darks").status, an::ui::StageStatus::Pending);
    QTemporaryDir output(QDir(folder()).filePath(QStringLiteral("stages-XXXXXX")));
    ASSERT_TRUE(output.isValid());
    process->model()->setOutputDirectory(output.path());
    addLights(*process,
              {QDir(folder()).filePath(QStringLiteral("s1.fit")), QDir(folder()).filePath(QStringLiteral("s2.fit")),
               QDir(folder()).filePath(QStringLiteral("s3.fit"))});
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() { return rowOf(*process, "stacking").status == an::ui::StageStatus::Ready; }, kWaitMs))
        << rowOf(*process, "stacking").line.toStdString();
    EXPECT_EQ(rowOf(*process, "lights").status, an::ui::StageStatus::Ready);
    EXPECT_EQ(rowOf(*process, "lights").line, QStringLiteral("3 frames \u00B7 1 group"));
    EXPECT_EQ(rowOf(*process, "registration").status, an::ui::StageStatus::Ready);
    EXPECT_EQ(rowOf(*process, "darks").status, an::ui::StageStatus::Pending);

    // Floated, the same list in the task's window.
    const Task* const task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    const an::ui::shell::TaskId id = task->id;
    ASSERT_TRUE(window().floatTask(id));
    settle();
    EXPECT_EQ(stagesOf(*process), stages) << "floated, the page keeps its list";
    EXPECT_EQ(stages->currentRow(), QStringLiteral("lights"));
    ASSERT_TRUE(window().attachTask(id));
    settle();
}

// The list's foot holds the job's output folder and the project file's acts: Export writes the job to a file, which
// is then the first of the recent projects; Reset Settings asks and resets; Import reads a project file back into the
// job; and the presets are the Project menu's.
TEST_F(IntegrationPage, TheListsFootHoldsTheProjectFile)
{
    ImageIntegrationProcess* const process = launch();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->isPresentedAsPage());
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    StageListNavigator* const stages = stagesOf(*process);
    ASSERT_NE(stages, nullptr);
    const auto said = std::make_shared<an::tests::app::Answered>();
    an::tests::app::answerEveryMessageBox(said);

    // The output folder is the foot's field.
    auto* const edit = process->findChild<QLineEdit*>(QStringLiteral("outputDirectoryEdit"));
    ASSERT_NE(edit, nullptr);
    EXPECT_TRUE(stages->foot()->isAncestorOf(edit));
    EXPECT_TRUE(edit->isVisibleTo(page));
    QTemporaryDir output(QDir(folder()).filePath(QStringLiteral("foot-XXXXXX")));
    ASSERT_TRUE(output.isValid());
    process->model()->setOutputDirectory(output.path());
    settle();
    EXPECT_EQ(QDir::cleanPath(edit->text()), QDir::cleanPath(output.path()));

    const QList<QAbstractButton*> buttons = stages->footButtons();
    ASSERT_EQ(buttons.size(), 5);
    for (const QAbstractButton* button : buttons)
    {
        EXPECT_TRUE(button->isVisibleTo(page)) << button->text().toStdString();
        EXPECT_TRUE(button->isEnabled()) << button->text().toStdString();
    }
    addLights(*process,
              {QDir(folder()).filePath(QStringLiteral("p1.fit")), QDir(folder()).filePath(QStringLiteral("p2.fit"))});
    ASSERT_EQ(framesOf(*process), 2);

    // Export...: the job is written to the file chosen, and the file is the first of the recent projects.
    const QString file = QDir(output.path()).filePath(QStringLiteral("carina.json"));
    an::tests::app::Answered chosen;
    an::tests::app::chooseInTheNextFileDialog({file}, &chosen);
    buttons[1]->click();
    ASSERT_TRUE(chosen.seen) << "Export... asks for the file";
    EXPECT_EQ(chosen.title, QStringLiteral("Export Project"));
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return QFileInfo::exists(file); }, kWaitMs))
        << "the project file is written";
    settle();
    auto* const recent = qobject_cast<QPushButton*>(buttons[2]);
    ASSERT_TRUE(recent != nullptr && recent->menu() != nullptr);
    ASSERT_FALSE(recent->menu()->actions().isEmpty());
    EXPECT_TRUE(entryText(*recent->menu()->actions().front()).contains(QStringLiteral("carina")))
        << entryText(*recent->menu()->actions().front()).toStdString();
    ASSERT_FALSE(process->recentProjects().isEmpty());
    EXPECT_EQ(QDir::cleanPath(process->recentProjects().front()), QDir::cleanPath(file));

    // The presets are the Project menu's: Save Preset..., and the presets to load and to delete.
    auto* const presets = qobject_cast<QPushButton*>(buttons[3]);
    ASSERT_TRUE(presets != nullptr && presets->menu() != nullptr);
    QStringList offered;
    for (const QAction* entry : presets->menu()->actions())
    {
        offered.append(entryText(*entry));
    }
    EXPECT_EQ(offered, (QStringList{QStringLiteral("Save Preset..."), QStringLiteral("Load Preset..."),
                                    QStringLiteral("Delete Preset...")}));

    // Import...: the frames go, and the file brings them back.
    process->model()->removeFrames(FrameType::Light, {QDir(folder()).filePath(QStringLiteral("p1.fit")),
                                                      QDir(folder()).filePath(QStringLiteral("p2.fit"))});
    settle();
    ASSERT_EQ(framesOf(*process), 0);
    an::tests::app::Answered imported;
    an::tests::app::chooseInTheNextFileDialog({file}, &imported);
    buttons[0]->click();
    ASSERT_TRUE(imported.seen) << "Import... asks for the file";
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return framesOf(*process) == 2; }, kWaitMs))
        << "the project file's frames are the job's again";

    // Reset Settings... is the Project menu's act: it asks before it resets.
    said->seen = false;
    buttons[4]->click();
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return said->seen; }, kWaitMs))
        << "Reset Settings... asks before it resets";
    settle();
    EXPECT_EQ(framesOf(*process), 2) << "a reset keeps the frames";
}

// The Summary stage of the page lists the runs the job's output folder keeps - a folder of that history that is no
// run's archive, as a project's own are, is not one - and the notes written there are kept with the job, in its
// output folder.
TEST_F(IntegrationPage, TheSummaryListsTheJobsRunsAndKeepsItsNotesWithTheJob)
{
    QTemporaryDir output;
    ASSERT_TRUE(output.isValid());
    // The archive of a run, as a run leaves it, and beside it a folder that is none.
    const QDir history(QDir(output.path()).filePath(QStringLiteral("history")));
    ASSERT_TRUE(QDir().mkpath(history.filePath(QStringLiteral("20260101_120000_000"))));
    ASSERT_TRUE(QDir().mkpath(history.filePath(QStringLiteral("0b6f6c0e-an-images-undo-history"))));
    {
        QJsonObject run;
        run.insert(QStringLiteral("archivedAtUtc"), QStringLiteral("2026-01-01T12:00:00.000Z"));
        run.insert(QStringLiteral("archivedAtLocal"), QStringLiteral("2026-01-01T12:00:00.000"));
        run.insert(QStringLiteral("executionId"), QStringLiteral("page-run-001"));
        run.insert(QStringLiteral("executionMode"), QStringLiteral("Full"));
        run.insert(QStringLiteral("overallResult"), QStringLiteral("success"));
        run.insert(QStringLiteral("totalDurationMs"), 5000);
        run.insert(QStringLiteral("warningCount"), 0);
        // A run is listed for the master it made.
        run.insert(QStringLiteral("integrationGroupResults"),
                   QJsonArray{QJsonObject{
                       {QStringLiteral("groupKey"), QStringLiteral("PageGroup")},
                       {QStringLiteral("outputPath"), QDir(output.path()).filePath(QStringLiteral("stacked.fit"))},
                       {QStringLiteral("outputFilename"), QStringLiteral("stacked.fit")},
                       {QStringLiteral("outputBytes"), 1024},
                       {QStringLiteral("lightFrameCount"), 5},
                       {QStringLiteral("durationMs"), 5000},
                       {QStringLiteral("result"), QStringLiteral("success")}}});
        QFile manifest(history.filePath(QStringLiteral("20260101_120000_000/workflow_manifest.json")));
        ASSERT_TRUE(manifest.open(QIODevice::WriteOnly));
        ASSERT_GT(manifest.write(QJsonDocument(run).toJson()), 0);
    }

    ImageIntegrationProcess* const process = launch();
    ASSERT_NE(process, nullptr);
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    StageListNavigator* const stages = stagesOf(*process);
    ASSERT_NE(stages, nullptr);
    process->model()->setOutputDirectory(output.path());
    stages->setCurrentPage(StagePage::Summary);
    settle();
    QWidget* const summary = process->findChild<QWidget*>(QStringLiteral("summaryTab"));
    ASSERT_NE(summary, nullptr);
    ASSERT_TRUE(summary->isVisibleTo(page));

    // The run history: the one archive, and not the folder beside it.
    const auto* const frame = summary->findChild<QWidget*>(QStringLiteral("runHistoryFrame"));
    ASSERT_NE(frame, nullptr);
    auto* const runs = frame->findChild<QTreeView*>();
    ASSERT_NE(runs, nullptr);
    ASSERT_TRUE(runs->isVisibleTo(page));
    ASSERT_NE(runs->model(), nullptr);
    ASSERT_TRUE(an::tests::app::processEventsUntil([runs]() { return runs->model()->rowCount() == 1; }, kWaitMs))
        << runs->model()->rowCount() << " runs are listed";

    // The notes are kept with the job.
    auto* const notes = summary->findChild<QPlainTextEdit*>(QStringLiteral("txtSessionNotes"));
    ASSERT_NE(notes, nullptr);
    ASSERT_TRUE(notes->isVisibleTo(page));
    notes->setPlainText(QStringLiteral("Clear until two; clouds after."));
    const QString kept =
        QDir(output.path()).filePath(QStringLiteral(".afternight/image_integration/session_notes.txt"));
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&kept]() {
            QFile file(kept);
            return file.open(QIODevice::ReadOnly) &&
                   QString::fromUtf8(file.readAll()).contains(QStringLiteral("Clear until two"));
        },
        kWaitMs))
        << "the notes were not kept in " << kept.toStdString();
}

} // namespace an::tests::observatory
