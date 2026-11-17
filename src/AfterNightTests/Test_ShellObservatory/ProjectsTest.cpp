// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Projects in the Observatory window as a person uses them: the top bar's switcher reads "No project" and its menu
// offers New and Open; New Project... makes a project the switcher then names, with the open images in it; a project
// closed is found again among the ones used last; Open Project... opens a folder and says what cannot be opened - a
// lock held on another machine with Open anyway; Switch Project... opens the switcher's menu; the palette offers the
// commands and the File menu lists them; Home's New project and the welcome's line ask for the same dialog; and the
// project used last is opened at the start only when the settings ask.

#include "ObservatoryWindowCase.h"

#include "ShellActions.h"
#include "chrome/MenuBarOnAlt.h"
#include "chrome/TopBar.h"
#include "pages/HomePage.h"
#include "projects/ProjectFlows.h"

#include "Test_ShellClassic/AppTestSupport.h"
#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "commands/CommandIndex.h"
#include "components/AnSegmentedControl.h"
#include "components/AnSnackbar.h"
#include "controls/HomeQuickActions.h"
#include "controls/HomeWelcome.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/SettingsDialog.h"
#include "controls/settings/PathPickerWidget.h"
#include "controls/settings/ProjectsSettingsPage.h"
#include "controls/settings/SettingsKeys.h"
#include "controls/settings/SettingsPage.h"
#include "core/ApplicationCore.h"
#include "dialogs/NewProjectDialog.h"
#include "projects/ProjectService.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using an::ui::NewProjectDialog;
using an::ui::observatory::ProjectFlows;
using an::ui::shell::ProjectService;
namespace shellIds = an::ui::observatory::ids;

constexpr int kOpenMs = 20000;

void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

class ProjectSwitcher : public ObservatoryWindowCase
{
protected:
    void SetUp() override
    {
        ObservatoryWindowCase::SetUp();
        forgetProjects();
    }

    void TearDown() override
    {
        // No case leaves a project open, a chooser of its own or a project among the ones used last.
        flows().setFolderChooser({});
        (void)projects().close();
        forgetProjects();
        ObservatoryWindowCase::TearDown();
    }

    [[nodiscard]] static ProjectService& projects()
    {
        return core().projects();
    }

    [[nodiscard]] static ProjectFlows& flows()
    {
        return window().projectFlows();
    }

    [[nodiscard]] static an::ui::observatory::TopBar& bar()
    {
        return *window().topBar();
    }

    [[nodiscard]] static QAction* action(const std::string_view id)
    {
        return window().shellActions().action(id);
    }

    /// The projects used last are forgotten: the settings hold none and the service reads them again.
    static void forgetProjects()
    {
        an::ui::IGlobalSettingsProvider* const settings = core().globalSettings();
        (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_RECENT), QString());
        (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_LAST), QString());
        (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP), QStringLiteral("0"));
        projects().setSettings(settings);
        bar().refreshProjectMenu();
    }

    /// A root for the case's projects and files, made once the suite's temporary directory is in force.
    [[nodiscard]] static QString root()
    {
        static QTemporaryDir files(QDir::temp().filePath(QStringLiteral("observatory-projects-XXXXXX")));
        return QDir::cleanPath(files.path());
    }

    /// The entry of the switcher's menu under an object name; null for none.
    [[nodiscard]] static QAction* menuEntry(const char* name)
    {
        for (QAction* entry : bar().projectMenu()->actions())
        {
            if (entry->objectName() == QLatin1String(name))
            {
                return entry;
            }
        }
        return nullptr;
    }

    /// The names of the menu's entries of an object name, in its order.
    [[nodiscard]] static QStringList menuTexts(const char* name)
    {
        QStringList texts;
        for (const QAction* entry : bar().projectMenu()->actions())
        {
            if (entry->objectName() == QLatin1String(name))
            {
                texts.append(entry->text());
            }
        }
        return texts;
    }

    /// Asks for a new project through an action or a button, and answers the dialog it opens: the location is the
    /// case's root, the name the one given; `before` changes the dialog before it is accepted.
    static void makeProject(const QString& name, const std::function<void(NewProjectDialog&)>& before = {})
    {
        action(shellIds::kNewProject)->trigger();
        NewProjectDialog* const dialog = flows().newProjectDialog();
        ASSERT_NE(dialog, nullptr) << "New Project... opened no dialog";
        dialog->locationPicker()->setPath(QDir::toNativeSeparators(root()));
        dialog->nameField()->setText(name);
        if (before)
        {
            before(*dialog);
        }
        ASSERT_TRUE(dialog->createButton()->isEnabled()) << dialog->problem().toStdString();
        dialog->createButton()->click();
        settle();
    }

    /// A grey FITS image written under a name in the case's root.
    [[nodiscard]] static QString writeImage(const QString& name)
    {
        const QString path = QDir(root()).filePath(name);
        std::optional<an::consumer::ImageAsset> asset = an::tests::app::filledAsset(session(), 0.4f);
        EXPECT_TRUE(asset.has_value());
        if (!asset.has_value())
        {
            return {};
        }
        const an::Result<an::OutputIntent> intent = an::ui::appContext()->getOutputIntentProvider()->buildOutputIntent(
            *asset->descriptor(), an::CodecFormat::Fits);
        EXPECT_TRUE(intent.hasValue()) << intent.status().message();
        EXPECT_TRUE(intent.hasValue() && an::ui::writeViewImage(session(), *asset, path, *intent, {}).ok());
        return QDir::cleanPath(path);
    }

    /// Opens a file as a person's file is opened, and waits for its view.
    static void openFile(const QString& path)
    {
        an::ui::appContext()->getRecentFilesService()->openDroppedFiles({path});
        EXPECT_TRUE(an::tests::app::processEventsUntil(
            [&path]() {
                for (ImageViewControl* view : views())
                {
                    if (view->hasImage() && QDir::cleanPath(view->document().names().sourcePath) == path)
                    {
                        return true;
                    }
                }
                return false;
            },
            kOpenMs))
            << path.toStdString() << " did not open";
    }

    /// Writes a lock as another machine's session leaves one.
    static void writeForeignLock(const QString& folder)
    {
        QFile lock(QDir(folder).filePath(QStringLiteral(".lock")));
        ASSERT_TRUE(lock.open(QIODevice::WriteOnly | QIODevice::Truncate));
        lock.write("4242\nAfterNight\nOBSERVATORY-PC\nanother-machine-id\nanother-boot-id\n");
    }
};

// With no project the switcher reads "No project", says so in its tooltip, and its menu offers New project... and
// Open project..., Close project withdrawn.
TEST_F(ProjectSwitcher, ItReadsNoProjectAndItsMenuOffersNewAndOpen)
{
    ASSERT_FALSE(projects().isOpen());
    QToolButton* const switcher = bar().projectSwitcher();
    EXPECT_EQ(switcher->text(), QStringLiteral("No project"));
    EXPECT_TRUE(switcher->toolTip().contains(QStringLiteral("No project is open")))
        << switcher->toolTip().toStdString();
    EXPECT_LE(switcher->maximumWidth(), 220);

    QStringList texts;
    for (const QAction* entry : bar().projectMenu()->actions())
    {
        if (!entry->isSeparator())
        {
            texts.append(entry->text());
        }
    }
    EXPECT_EQ(texts, (QStringList{QStringLiteral("New project..."), QStringLiteral("Open project..."),
                                  QStringLiteral("Close project")}));
    ASSERT_NE(menuEntry("projectMenuNew"), nullptr);
    EXPECT_TRUE(menuEntry("projectMenuNew")->isEnabled());
    EXPECT_TRUE(menuEntry("projectMenuOpen")->isEnabled());
    EXPECT_FALSE(menuEntry("projectMenuClose")->isEnabled()) << "Close project is offered with no project";
    EXPECT_FALSE(action(shellIds::kCloseProject)->isEnabled());
}

// New Project... opens the dialog from the settings; accepted, the project is made and open, the switcher names it and
// its menu lists it first, checked, with Close project offered. Closed, the switcher reads "No project" again and the
// project is among the ones used last, which opens it again.
TEST_F(ProjectSwitcher, NewProjectMakesAProjectTheSwitcherNamesAndFindsAgain)
{
    action(shellIds::kNewProject)->trigger();
    NewProjectDialog* const opened = flows().newProjectDialog();
    ASSERT_NE(opened, nullptr);
    EXPECT_EQ(opened->parentWidget(), &window());
    EXPECT_EQ(opened->windowModality(), Qt::WindowModal);
    EXPECT_EQ(opened->compressionPicker()->currentIndex(), 0) << "the setting's compression, LZ4, is chosen";
    EXPECT_TRUE(opened->addOpenImagesCheck()->isHidden()) << "no image is open";
    // Asked again while it is open, the same dialog comes forward.
    action(shellIds::kNewProject)->trigger();
    EXPECT_EQ(flows().newProjectDialog(), opened);
    opened->cancelButton()->click();
    settle();
    EXPECT_EQ(flows().newProjectDialog(), nullptr);
    EXPECT_FALSE(projects().isOpen());

    makeProject(QStringLiteral("Carina"));
    ASSERT_TRUE(projects().isOpen());
    const QString folder = root() + QStringLiteral("/Carina");
    EXPECT_EQ(projects().currentFolder(), folder);
    EXPECT_TRUE(QFileInfo(folder + QStringLiteral("/project.anproj")).isFile());
    EXPECT_TRUE(QFileInfo(folder + QStringLiteral("/history")).isDir());
    EXPECT_EQ(bar().projectSwitcher()->text(), QStringLiteral("Carina"));
    EXPECT_EQ(bar().projectName(), QStringLiteral("Carina"));
    EXPECT_TRUE(bar().projectSwitcher()->toolTip().contains(QStringLiteral("Carina")));

    QAction* const current = menuEntry("projectMenuCurrent");
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(bar().projectMenu()->actions().constFirst(), current) << "the open project is the menu's first entry";
    EXPECT_EQ(current->text(), QStringLiteral("Carina"));
    EXPECT_TRUE(current->isChecked());
    EXPECT_TRUE(menuTexts("projectMenuRecent").isEmpty()) << "the open project is not listed again as a recent one";
    EXPECT_TRUE(menuEntry("projectMenuClose")->isEnabled());

    // Close project, from the switcher's menu.
    menuEntry("projectMenuClose")->trigger();
    settle();
    EXPECT_FALSE(projects().isOpen());
    EXPECT_EQ(bar().projectSwitcher()->text(), QStringLiteral("No project"));
    EXPECT_EQ(menuEntry("projectMenuCurrent"), nullptr);
    EXPECT_EQ(menuTexts("projectMenuRecent"), QStringList{QStringLiteral("Carina")});
    EXPECT_FALSE(QFileInfo::exists(folder + QStringLiteral("/.lock")));

    // The recent project opens again.
    menuEntry("projectMenuRecent")->trigger();
    settle();
    ASSERT_TRUE(projects().isOpen());
    EXPECT_EQ(projects().currentFolder(), folder);
    EXPECT_EQ(bar().projectSwitcher()->text(), QStringLiteral("Carina"));
}

// A name wider than the switcher is cut in its middle, the switcher no wider than 220 px, and said whole in the
// tooltip.
TEST_F(ProjectSwitcher, ALongNameIsCutInItsMiddleAndSaidWholeInTheTooltip)
{
    const QString name = QStringLiteral("NGC 7000 North America and Pelican mosaic of six panels 2026");
    makeProject(name);
    ASSERT_TRUE(projects().isOpen());
    settle();
    QToolButton* const switcher = bar().projectSwitcher();
    EXPECT_LE(switcher->width(), 220);
    EXPECT_LE(switcher->sizeHint().width(), 220) << "the switcher asks for more than its most";
    EXPECT_NE(switcher->text(), name);
    EXPECT_TRUE(switcher->text().startsWith(name.left(3))) << switcher->text().toStdString();
    EXPECT_TRUE(switcher->text().endsWith(name.right(3))) << switcher->text().toStdString();
    EXPECT_TRUE(switcher->toolTip().contains(name));
    EXPECT_EQ(bar().projectName(), name);
}

// With images open the dialog offers to add them, and they join the project; an image opened while the project is open
// joins it too; an image that no file holds does not; and closing the project leaves the images open.
TEST_F(ProjectSwitcher, TheOpenImagesJoinTheNewProjectAndAnImageOpenedLaterJoinsToo)
{
    const QString first = writeImage(QStringLiteral("joins-first.fits"));
    const QString second = writeImage(QStringLiteral("joins-second.fits"));
    ASSERT_FALSE(first.isEmpty());
    ASSERT_FALSE(second.isEmpty());
    openFile(first);
    ASSERT_NE(openView(QStringLiteral("never-saved")), nullptr);

    makeProject(QStringLiteral("Joins"), [](NewProjectDialog& dialog) {
        EXPECT_FALSE(dialog.addOpenImagesCheck()->isHidden());
        EXPECT_EQ(dialog.addOpenImagesCheck()->text(), QStringLiteral("Add the open image to the project"))
            << "the image no file holds is not one the project can take";
        EXPECT_TRUE(dialog.addOpenImagesCheck()->isChecked());
        EXPECT_TRUE(dialog.moveHistoryCheck()->isHidden());
    });
    ASSERT_TRUE(projects().isOpen());
    ASSERT_EQ(projects().current()->images.size(), 1u);
    EXPECT_EQ(projects().current()->images.front().path, first);
    EXPECT_NE(projects().imageFor(first), nullptr);

    openFile(second);
    ASSERT_TRUE(an::tests::app::processEventsUntil([]() { return projects().current()->images.size() == 2u; }, 5000))
        << "an image opened while the project is open did not join it";
    EXPECT_NE(projects().imageFor(second), nullptr);
    EXPECT_EQ(projects().current()->images.back().kind, an::ui::shell::ProjectImageKind::Image);

    const std::size_t open = views().size();
    action(shellIds::kCloseProject)->trigger();
    settle();
    EXPECT_FALSE(projects().isOpen());
    EXPECT_EQ(views().size(), open) << "closing a project closed its images";

    // With the choice turned off, the open images stay out of the project.
    makeProject(QStringLiteral("Stays empty"),
                [](NewProjectDialog& dialog) { dialog.addOpenImagesCheck()->setChecked(false); });
    ASSERT_TRUE(projects().isOpen());
    EXPECT_TRUE(projects().current()->images.empty());
}

// Open Project... asks for a folder and opens its project; a folder that holds none is said, and the project open
// stays open.
TEST_F(ProjectSwitcher, OpenProjectOpensTheFolderChosenAndSaysWhatIsNoProject)
{
    makeProject(QStringLiteral("To open"));
    const QString folder = projects().currentFolder();
    ASSERT_TRUE(projects().close());

    QString asked;
    flows().setFolderChooser([&asked, folder](QWidget* parent, const QString& caption, const QString& start) {
        EXPECT_EQ(parent, &window());
        EXPECT_EQ(caption, QStringLiteral("Open Project"));
        asked = start;
        return folder;
    });
    action(shellIds::kOpenProject)->trigger();
    settle();
    EXPECT_FALSE(asked.isEmpty()) << "Open Project... asked for no folder";
    ASSERT_TRUE(projects().isOpen());
    EXPECT_EQ(projects().currentFolder(), folder);
    EXPECT_EQ(flows().refusalMessage(), nullptr);

    // A folder that is no project: said over the window, and the open project stays.
    ASSERT_TRUE(QDir(root()).mkpath(QStringLiteral("plain-folder")));
    flows().setFolderChooser(
        [](QWidget*, const QString&, const QString&) { return root() + QStringLiteral("/plain-folder"); });
    menuEntry("projectMenuOpen")->trigger();
    settle();
    QMessageBox* const said = flows().refusalMessage();
    ASSERT_NE(said, nullptr) << "a folder that holds no project was not said";
    EXPECT_TRUE(said->text().contains(QStringLiteral("holds no project"))) << said->text().toStdString();
    EXPECT_EQ(said->button(QMessageBox::Ok) != nullptr, true);
    EXPECT_EQ(said->windowModality(), Qt::WindowModal);
    said->button(QMessageBox::Ok)->click();
    settle();
    EXPECT_EQ(flows().refusalMessage(), nullptr);
    EXPECT_EQ(projects().currentFolder(), folder);

    // A cancelled folder dialog opens nothing and says nothing.
    flows().setFolderChooser([](QWidget*, const QString&, const QString&) { return QString(); });
    action(shellIds::kOpenProject)->trigger();
    settle();
    EXPECT_EQ(flows().refusalMessage(), nullptr);
    EXPECT_EQ(projects().currentFolder(), folder);
}

// A project locked by a session on another machine is said naming the machine and the application that hold it;
// Cancel leaves it closed, and Open anyway takes the lock and opens it.
TEST_F(ProjectSwitcher, ALockHeldElsewhereIsSaidNamingItsHolderAndOpenAnywayTakesIt)
{
    makeProject(QStringLiteral("Shared"));
    const QString folder = projects().currentFolder();
    ASSERT_TRUE(projects().close());
    writeForeignLock(folder);

    flows().openProjectFolder(folder);
    settle();
    QMessageBox* said = flows().refusalMessage();
    ASSERT_NE(said, nullptr);
    EXPECT_TRUE(said->text().contains(QStringLiteral("OBSERVATORY-PC"))) << said->text().toStdString();
    EXPECT_TRUE(said->text().contains(QStringLiteral("AfterNight"))) << said->text().toStdString();
    auto* anyway = said->findChild<QPushButton*>(QStringLiteral("projectOpenAnyway"));
    ASSERT_NE(anyway, nullptr);
    EXPECT_EQ(anyway->text(), QStringLiteral("Open anyway"));
    ASSERT_NE(said->button(QMessageBox::Cancel), nullptr);
    EXPECT_EQ(said->defaultButton(), said->button(QMessageBox::Cancel)) << "the lock is not taken by Enter";
    said->button(QMessageBox::Cancel)->click();
    settle();
    EXPECT_FALSE(projects().isOpen());
    EXPECT_EQ(bar().projectSwitcher()->text(), QStringLiteral("No project"));

    flows().openProjectFolder(folder);
    settle();
    said = flows().refusalMessage();
    ASSERT_NE(said, nullptr);
    anyway = said->findChild<QPushButton*>(QStringLiteral("projectOpenAnyway"));
    ASSERT_NE(anyway, nullptr);
    anyway->click();
    settle();
    ASSERT_TRUE(projects().isOpen());
    EXPECT_EQ(projects().currentFolder(), folder);
    EXPECT_EQ(bar().projectSwitcher()->text(), QStringLiteral("Shared"));
}

// Switch Project... opens the switcher's menu under the switcher.
TEST_F(ProjectSwitcher, SwitchProjectOpensTheSwitchersMenu)
{
    QMenu* const menu = bar().projectMenu();
    ASSERT_FALSE(menu->isVisible());
    action(shellIds::kSwitchProject)->trigger();
    EXPECT_TRUE(an::tests::app::processEventsUntil([menu]() { return menu->isVisible(); }, 3000))
        << "Switch Project... opened no menu";
    menu->hide();
    settle();
}

// The palette offers the project's commands, says which project they are about, and Close Project only while one is
// open; the File menu lists the five before Exit; and the classic shell's catalogue holds none of them.
TEST_F(ProjectSwitcher, ThePaletteOffersTheCommandsAndTheFileMenuListsThem)
{
    an::ui::shell::CommandIndex& index = core().commands();
    index.refresh();
    const auto command = [&index](const char* actionId) {
        return index.entry(QStringLiteral("action:") + QLatin1String(actionId));
    };
    const an::ui::shell::CommandEntry* const create = command("actionNew_Project");
    const an::ui::shell::CommandEntry* const open = command("actionOpen_Project");
    const an::ui::shell::CommandEntry* const change = command("actionSwitch_Project");
    const an::ui::shell::CommandEntry* close = command("actionClose_Project");
    ASSERT_NE(create, nullptr);
    ASSERT_NE(open, nullptr);
    ASSERT_NE(change, nullptr);
    ASSERT_NE(close, nullptr);
    EXPECT_EQ(create->title, QStringLiteral("New Project..."));
    EXPECT_EQ(open->title, QStringLiteral("Open Project..."));
    EXPECT_EQ(change->title, QStringLiteral("Switch Project..."));
    EXPECT_EQ(close->title, QStringLiteral("Close Project"));
    EXPECT_EQ(change->subtitle, QString::fromUtf8("Project \xC2\xB7 No project"));
    EXPECT_TRUE(create->available);
    EXPECT_FALSE(close->available) << "Close Project is offered with no project";
    ASSERT_NE(command("actionProject_Settings"), nullptr);
    EXPECT_EQ(command("actionProject_Settings")->title, QStringLiteral("Project Settings..."));

    // Typed, they are found; run, New Project... opens its dialog.
    QSet<QString> found;
    for (const an::ui::CommandRowGroup& group : index.query(QStringLiteral("project")))
    {
        for (const an::ui::CommandRow& row : group.rows)
        {
            found.insert(row.id);
        }
    }
    for (const char* id : {"action:actionNew_Project", "action:actionOpen_Project", "action:actionSwitch_Project",
                           "action:actionClose_Project"})
    {
        EXPECT_TRUE(found.contains(QLatin1String(id))) << id << " is not found by \"project\"";
    }
    ASSERT_TRUE(index.run(QStringLiteral("action:actionNew_Project")));
    ASSERT_NE(flows().newProjectDialog(), nullptr);
    flows().newProjectDialog()->cancelButton()->click();
    settle();

    makeProject(QStringLiteral("In the palette"));
    index.refresh();
    close = command("actionClose_Project");
    ASSERT_NE(close, nullptr);
    EXPECT_TRUE(close->available);
    EXPECT_EQ(close->subtitle, QString::fromUtf8("Project \xC2\xB7 In the palette"));
    ASSERT_TRUE(index.run(QStringLiteral("action:actionClose_Project")));
    EXPECT_FALSE(projects().isOpen());

    // The File menu: the five, in the table's order, between two separators before Exit.
    QMenu* const file = window().menuBarOnAlt().menu("menuFile");
    ASSERT_NE(file, nullptr);
    const QList<QAction*> entries = file->actions();
    const qsizetype at = entries.indexOf(action(shellIds::kNewProject));
    ASSERT_GT(at, 0);
    ASSERT_LT(at + 6, entries.size());
    EXPECT_TRUE(entries.at(at - 1)->isSeparator());
    EXPECT_EQ(entries.at(at + 1), action(shellIds::kOpenProject));
    EXPECT_EQ(entries.at(at + 2), action(shellIds::kSwitchProject));
    EXPECT_EQ(entries.at(at + 3), action(shellIds::kCloseProject));
    EXPECT_EQ(entries.at(at + 4), action(shellIds::kProjectSettings));
    EXPECT_TRUE(entries.at(at + 5)->isSeparator());
    EXPECT_EQ(entries.at(at + 6), core().actions().action(an::ui::shell::ids::kExit));

    // They are this shell's: the catalogue every shell shares holds none of them.
    for (const char* id : {"actionNew_Project", "actionOpen_Project", "actionSwitch_Project", "actionClose_Project",
                           "actionProject_Settings"})
    {
        EXPECT_EQ(core().actions().action(id), nullptr) << id << " is in the catalogue's base table";
        ASSERT_NE(action(id), nullptr) << id;
        EXPECT_EQ(action(id)->parent(), &window());
    }
}

// Project Settings... opens the Settings at its Projects page, which this shell's Settings show after General.
TEST_F(ProjectSwitcher, ProjectSettingsOpensTheSettingsAtTheProjectsPage)
{
    QString shownPage;
    QStringList pages;
    const std::shared_ptr<an::tests::app::WindowPoll> poll =
        an::tests::app::answerWhenShown([&shownPage, &pages](QWidget* shown) {
            auto* const dialog = qobject_cast<SettingsDialog*>(shown);
            if (dialog == nullptr)
            {
                return false;
            }
            if (SettingsPage* const page = dialog->currentPage())
            {
                shownPage = page->pageTitle();
            }
            // The pages in the order the navigation lists them.
            if (const auto* const navigation = dialog->findChild<QListWidget*>())
            {
                for (int row = 0; row < navigation->count(); ++row)
                {
                    pages.append(navigation->item(row)->text());
                }
            }
            dialog->reject();
            return true;
        });
    action(shellIds::kProjectSettings)->trigger();
    EXPECT_TRUE(an::tests::app::processEventsUntil([&shownPage]() { return !shownPage.isEmpty(); }, 5000))
        << "Project Settings... opened no Settings";
    poll->ended = true;
    EXPECT_EQ(shownPage, QStringLiteral("Projects"));
    ASSERT_GE(pages.size(), 2);
    EXPECT_EQ(pages.at(0), QStringLiteral("General"));
    EXPECT_EQ(pages.at(1), QStringLiteral("Projects"));
    settle();
}

// Home's New project quick action, after the dashboard's and before More, and the welcome's line "Projects are
// optional..." with New project..., both ask for what New Project... does.
TEST_F(ProjectSwitcher, HomesNewProjectAndTheWelcomesLineOpenTheDialog)
{
    window().showHome();
    settle();
    an::ui::observatory::HomePage& home = *window().homePage();
    an::ui::HomeQuickActions& actions = *home.quickActions();
    EXPECT_EQ(actions.addedRowActionIds(), QStringList{QStringLiteral("new_project")});
    QAbstractButton* const quick = actions.actionButton(QStringLiteral("new_project"));
    ASSERT_NE(quick, nullptr);
    EXPECT_EQ(quick->text(), QStringLiteral("New project"));
    // After every action of the dashboard's the row shows, and before More.
    for (const QString& id : actions.rowActionIds())
    {
        QAbstractButton* const other = actions.actionButton(id);
        ASSERT_NE(other, nullptr) << id.toStdString();
        EXPECT_LT(other->x(), quick->x()) << id.toStdString() << " is after New project";
    }
    EXPECT_LT(quick->x(), actions.moreButton()->x());

    QTest::mouseClick(quick, Qt::LeftButton);
    ASSERT_NE(flows().newProjectDialog(), nullptr) << "Home's New project opened no dialog";
    flows().newProjectDialog()->cancelButton()->click();
    settle();
    ASSERT_EQ(flows().newProjectDialog(), nullptr);

    // The welcome's line, shown when there is no recent file.
    an::ui::HomeWelcome& welcome = *home.welcome();
    auto* const line = welcome.findChild<QWidget*>(QStringLiteral("homeWelcomeProjectsLine"));
    auto* const button = welcome.findChild<QPushButton*>(QStringLiteral("homeWelcomeNewProject"));
    ASSERT_NE(line, nullptr);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->text(), QStringLiteral("New project..."));
    button->click();
    ASSERT_NE(flows().newProjectDialog(), nullptr) << "the welcome's New project... opened no dialog";
    flows().newProjectDialog()->cancelButton()->click();
    settle();
}

/// The start, each case over a shell of its own.
class ProjectAtStart : public ::testing::Test
{
protected:
    /// A project made under a root of the case's own by a service of its own, and closed: its folder.
    [[nodiscard]] QString madeProject(const QString& name)
    {
        EXPECT_TRUE(m_root.isValid());
        ProjectService service;
        an::ui::shell::ProjectCreateRequest request;
        request.name = name;
        request.location = QDir::cleanPath(m_root.path());
        const an::ui::shell::ProjectOutcome made = service.create(request);
        EXPECT_TRUE(made.ok()) << made.why.toStdString();
        (void)service.close();
        return made.folder;
    }

    /// Starts the shell over settings that name a project as the one used last.
    [[nodiscard]] static std::unique_ptr<RunningObservatory> startWith(const QString& last, const bool openLast)
    {
        return startObservatory([last, openLast](an::ui::shell::ApplicationCore& core) {
            an::ui::IGlobalSettingsProvider* const settings = core.globalSettings();
            (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_LAST), last);
            (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_RECENT), QString());
            (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP),
                                   openLast ? QStringLiteral("1") : QStringLiteral("0"));
            core.projects().setSettings(settings);
        });
    }

    static void stop(std::unique_ptr<RunningObservatory> running)
    {
        // What the case stored does not reach the cases after it.
        an::ui::IGlobalSettingsProvider* const settings = running->core->globalSettings();
        (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_LAST), QString());
        (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_RECENT), QString());
        (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP), QStringLiteral("0"));
        running->shell->window()->close();
        const an::Status shut = stopObservatory(std::move(running));
        EXPECT_TRUE(shut.ok()) << shut.message();
    }

    QTemporaryDir m_root;
};

// With "Open the last project on startup" off - its default - the application starts with no project, as it always
// has, whatever project was used last.
TEST_F(ProjectAtStart, WithTheSettingOffTheApplicationStartsWithNoProject)
{
    const QString folder = madeProject(QStringLiteral("Not restored"));
    std::unique_ptr<RunningObservatory> running = startWith(folder, false);
    ASSERT_NE(running, nullptr);
    EXPECT_FALSE(running->core->projects().isOpen());
    EXPECT_EQ(running->shell->window()->topBar()->projectSwitcher()->text(), QStringLiteral("No project"));
    EXPECT_FALSE(QFileInfo::exists(folder + QStringLiteral("/.lock")));
    EXPECT_TRUE(running->shell->window()->isHomeShown());
    stop(std::move(running));
}

// With the setting on, the project used last is open as the window shows, and the switcher names it; the lock goes
// when the application ends.
TEST_F(ProjectAtStart, WithTheSettingOnTheLastProjectIsOpenAsTheWindowShows)
{
    const QString folder = madeProject(QStringLiteral("Restored"));
    std::unique_ptr<RunningObservatory> running = startWith(folder, true);
    ASSERT_NE(running, nullptr);
    ASSERT_TRUE(running->core->projects().isOpen());
    EXPECT_EQ(running->core->projects().currentFolder(), folder);
    EXPECT_EQ(running->shell->window()->topBar()->projectSwitcher()->text(), QStringLiteral("Restored"));
    EXPECT_TRUE(QFileInfo::exists(folder + QStringLiteral("/.lock")));
    stop(std::move(running));
    EXPECT_FALSE(QFileInfo::exists(folder + QStringLiteral("/.lock"))) << "the application ended and left its lock";
}

// A project used last that is gone leaves the application with no project, and the window says so once it shows,
// offering to open it.
TEST_F(ProjectAtStart, ALastProjectThatIsGoneIsSaidAndTheApplicationStartsWithNone)
{
    const QString folder = madeProject(QStringLiteral("Gone"));
    ASSERT_TRUE(QDir(folder).removeRecursively());
    std::unique_ptr<RunningObservatory> running = startWith(folder, true);
    ASSERT_NE(running, nullptr);
    EXPECT_FALSE(running->core->projects().isOpen());
    EXPECT_EQ(running->shell->window()->topBar()->projectSwitcher()->text(), QStringLiteral("No project"));
    an::ui::AnSnackbar* const snackbar = running->shell->window()->snackbar();
    ASSERT_NE(snackbar, nullptr);
    EXPECT_TRUE(an::tests::app::processEventsUntil([snackbar]() { return snackbar->isVisible(); }, 3000))
        << "the project that could not be opened was not said";
    EXPECT_TRUE(snackbar->text().contains(QStringLiteral("Gone"))) << snackbar->text().toStdString();
    EXPECT_EQ(snackbar->actionText(), QStringLiteral("Open..."));
    stop(std::move(running));
}

} // namespace
} // namespace an::tests::observatory
