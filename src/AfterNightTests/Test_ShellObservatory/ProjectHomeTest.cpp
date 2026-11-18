// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Project Home in the Observatory window, as a person meets it with a project open: Home is the project's page; its
// header says what the project is - its name, its object and where that came from, its folder with Show in File
// Browser, when it was made and its size; its actions do what their names say; its workspaces are tiles, each a
// miniature of its arrangement made of its images' pictures; its images are a table with each one's history and where
// it is open; its integrations are its jobs; its storage is measured off the GUI thread and equals the folder's, and
// its undo history is compacted from there; a history above the setting's threshold asks once, as the project opens,
// to be compacted; and its notes are kept in notes.md.

#include "ProjectCase.h"

#include "ObservatoryWindow.h"
#include "ShellActions.h"
#include "pages/HomePage.h"
#include "pages/ProjectHomePage.h"
#include "projects/ProjectFlows.h"
#include "workspaces/WorkspaceHost.h"
#include "workspaces/WorkspaceMiniature.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "components/AnBreadcrumb.h"
#include "components/AnIconRail.h"
#include "components/AnSnackbar.h"
#include "components/AnTag.h"
#include "components/RailItem.h"
#include "chrome/TopBar.h"
#include "controls/ImageHistoryIndex.h"
#include "controls/ImageViewControl.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "controls/SettingsDialog.h"
#include "controls/settings/SettingsPage.h"
#include "controls/settings/SettingsKeys.h"
#include "dialogs/CompactHistoryDialog.h"
#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/persistence/IntegrationDatabase.h"
#include "processes/ImageIntegration/ui_models/IntegrationJobsModel.h"
#include "projects/ProjectService.h"
#include "services/OpenProcessRegistry.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSize>
#include <QStackedWidget>
#include <QThread>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using an::ui::observatory::ProjectHomePage;
namespace shellIds = an::ui::observatory::ids;

constexpr int kWaitMs = 20000;

/// The Observatory window with a project open on Project Home: each case makes its own project and leaves none open.
class ProjectHome : public ProjectCase
{
protected:
    void SetUp() override
    {
        ProjectCase::SetUp();
        forgetProjects();
    }

    void TearDown() override
    {
        flows().setFolderChooser({});
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (auto* const dialog = qobject_cast<an::ui::CompactHistoryDialog*>(widget))
            {
                dialog->reject();
            }
        }
        // An edited image would ask whether to save it as the project closes it: the views go first, unasked.
        for (ImageViewControl* view : views())
        {
            core().viewService().removeViewControl(view, true);
        }
        settleProjects();
        (void)projects().close();
        forgetProjects();
        an::ui::IGlobalSettingsProvider* const settings = core().globalSettings();
        (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_COMPACT_ASK_THRESHOLD_GB), QString());
        ProjectCase::TearDown();
    }

    [[nodiscard]] static ProjectHomePage& page()
    {
        return *window().projectHomePage();
    }

    /// Makes a project under the case's root and opens it; its folder.
    static QString makeProject(const QString& name, const std::optional<an::ui::shell::ProjectObject>& object = {})
    {
        an::ui::shell::ProjectCreateRequest request;
        request.name = name;
        request.location = root();
        request.object = object;
        const an::ui::shell::ProjectOutcome outcome = projects().create(request);
        EXPECT_TRUE(outcome.ok()) << outcome.why.toStdString();
        settleProjects();
        return projects().currentFolder();
    }

    /// Edits a view's image into a grey one of another value, recorded in its history; waits until it is shown.
    static void edit(ImageViewControl& view, const float value)
    {
        std::optional<an::ui::ImageDocument> next =
            an::tests::app::filledDocument(session(), QStringLiteral("edited"), value);
        ASSERT_TRUE(next.has_value());
        an::Result<an::consumer::ImageAsset> copy = next->asset()->snapshot();
        ASSERT_TRUE(copy.hasValue()) << copy.status().message();
        auto held = std::make_shared<an::consumer::ImageAsset>(std::move(*copy));
        std::optional<bool> shown;
        view.editImage(
            "Edited", "Edited by the case",
            [held](const an::consumer::EngineSession&, const an::consumer::AssetWriteLease&, std::stop_token) {
                return an::Result<an::consumer::ImageAsset>(std::move(*held));
            },
            [&shown](const bool done) { shown = done; });
        ASSERT_TRUE(an::tests::app::processEventsUntil(
            [&shown, &view]() { return shown.has_value() && view.isWorkIdle(); }, kWaitMs));
        ASSERT_TRUE(*shown);
    }

    /// The step files of an image's history in the project.
    [[nodiscard]] static QStringList stepFilesOf(const QString& file)
    {
        const an::ui::shell::ProjectImage* const image = projects().imageFor(file);
        if (image == nullptr)
        {
            return {};
        }
        return QDir(projects().historyDirectoryFor(image->id))
            .entryList({QStringLiteral("*.xisf"), QStringLiteral("*.ansnap")}, QDir::Files);
    }

    /// Shows Project Home as the rail's Home does, and waits until its storage is measured.
    static bool showProjectHome()
    {
        window().showHome();
        QCoreApplication::processEvents();
        return window().isProjectHomeShown() &&
               an::tests::app::processEventsUntil([]() { return page().storage().has_value(); }, kWaitMs);
    }

    /// The bytes of the files below a folder, the folder's own count.
    [[nodiscard]] static std::uint64_t bytesBelow(const QString& folder)
    {
        std::uint64_t bytes = 0;
        QDirIterator files(folder, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (files.hasNext())
        {
            bytes += static_cast<std::uint64_t>(QFileInfo(files.next()).size());
        }
        return bytes;
    }
};

using Storage = ProjectHome;

/// Counts the times the snackbar is shown asking to compact a history.
class QuestionCounter final : public QObject
{
public:
    explicit QuestionCounter(an::ui::AnSnackbar& snackbar) : m_snackbar(snackbar) {}

    int asked = 0;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Show && watched == &m_snackbar &&
            m_snackbar.actionText() == QStringLiteral("Compact history..."))
        {
            ++asked;
        }
        return false;
    }

private:
    an::ui::AnSnackbar& m_snackbar;
};
using WorkspaceMiniature = ProjectHome;

} // namespace

// With a project open Home is the project's page; the rail's Home shows it, the breadcrumb says Home; closed, the
// project's page gives way to Home.
TEST_F(ProjectHome, IsHomeWhileAProjectIsOpen)
{
    window().showHome();
    EXPECT_TRUE(window().isHomeShown());
    EXPECT_FALSE(window().isProjectHomeShown());
    (void)makeProject(QStringLiteral("Home of a project"));
    ASSERT_TRUE(projects().isOpen());
    window().showHome();
    QCoreApplication::processEvents();
    EXPECT_TRUE(window().isProjectHomeShown());
    EXPECT_TRUE(window().isHomeShown());
    EXPECT_EQ(window().centre()->currentWidget(), window().projectHomePage());
    EXPECT_EQ(window().topBar()->breadcrumb()->parts(), QStringList{QStringLiteral("Home")});
    const std::vector<an::ui::RailItem>& items = window().rail()->items();
    const auto home = std::find_if(items.begin(), items.end(), [](const an::ui::RailItem& item) {
        return item.id == QLatin1String(an::ui::observatory::ObservatoryWindow::kRailHome);
    });
    ASSERT_NE(home, items.end());
    EXPECT_TRUE(home->selected);

    ASSERT_TRUE(projects().close());
    settleProjects();
    EXPECT_FALSE(window().isProjectHomeShown());
    EXPECT_TRUE(window().isHomeShown());
    EXPECT_EQ(window().centre()->currentWidget(), window().homePage());
}

// The header: the name, the object with where it came from, the folder with Show in File Browser, when the project was
// made, and its size once measured.
TEST_F(ProjectHome, TheHeaderSaysWhatTheProjectIs)
{
    const QString folder = makeProject(
        QStringLiteral("Carina Nebula"),
        an::ui::shell::ProjectObject{QStringLiteral("NGC 3372"), an::ui::shell::ProjectObjectSource::ObjectCard});
    ASSERT_TRUE(showProjectHome());
    EXPECT_EQ(page().headingText(), QStringLiteral("Carina Nebula"));
    ASSERT_NE(page().objectTag(), nullptr);
    EXPECT_TRUE(page().objectTag()->isVisible());
    EXPECT_EQ(page().objectTag()->text(), QStringLiteral("NGC 3372"));
    EXPECT_TRUE(page().objectCaption().contains(QStringLiteral("OBJECT card"))) << page().objectCaption().toStdString();
    EXPECT_EQ(page().folderText(), QDir::toNativeSeparators(folder));
    EXPECT_EQ(page().createdText(), QStringLiteral("Created ") + projects().current()->created.toLocalTime().toString(
                                                                     QStringLiteral("yyyy-MM-dd")));
    EXPECT_FALSE(page().sizeText().isEmpty());

    QString revealed;
    page().setFileReveal([&revealed](const QString& path) {
        revealed = path;
        return true;
    });
    ASSERT_NE(page().showInFileBrowserButton(), nullptr);
    QTest::mouseClick(page().showInFileBrowserButton(), Qt::LeftButton);
    EXPECT_EQ(QDir::cleanPath(revealed), QDir::cleanPath(folder));
}

// The actions: Continue shows the workspace used last; Add images... asks for files; New integration... opens Image
// Integration; Project settings... opens the Settings at Projects; Close project closes it.
TEST_F(ProjectHome, TheActionsDoWhatTheirNamesSay)
{
    (void)makeProject(QStringLiteral("Actions"));
    const QString image = writeImage(QStringLiteral("actions/one.fits"));
    openFile(image);
    ASSERT_TRUE(showProjectHome());

    ASSERT_NE(page().continueButton(), nullptr);
    EXPECT_EQ(page().continueButton()->text(), window().continueLabel());
    EXPECT_EQ(page().continueButton()->text(), QStringLiteral("Continue · Workspace 1"));
    QTest::mouseClick(page().continueButton(), Qt::LeftButton);
    settleProjects();
    EXPECT_TRUE(window().isWorkspaceShown());

    ASSERT_TRUE(showProjectHome());
    bool asked = false;
    const std::shared_ptr<an::tests::app::WindowPoll> files = an::tests::app::answerWhenShown([&asked](QWidget* shown) {
        auto* const dialog = qobject_cast<QFileDialog*>(shown);
        if (dialog == nullptr)
        {
            return false;
        }
        asked = true;
        dialog->reject();
        return true;
    });
    QTest::mouseClick(page().addImagesButton(), Qt::LeftButton);
    EXPECT_TRUE(an::tests::app::processEventsUntil([&asked]() { return asked; }, kWaitMs))
        << "Add images... asked for nothing";
    files->ended = true;

    QString settingsPage;
    const std::shared_ptr<an::tests::app::WindowPoll> settings =
        an::tests::app::answerWhenShown([&settingsPage](QWidget* shown) {
            auto* const dialog = qobject_cast<SettingsDialog*>(shown);
            if (dialog == nullptr)
            {
                return false;
            }
            if (SettingsPage* const current = dialog->currentPage())
            {
                settingsPage = current->pageTitle();
            }
            dialog->reject();
            return true;
        });
    QTest::mouseClick(page().projectSettingsButton(), Qt::LeftButton);
    EXPECT_TRUE(an::tests::app::processEventsUntil([&settingsPage]() { return !settingsPage.isEmpty(); }, kWaitMs));
    EXPECT_EQ(settingsPage, QStringLiteral("Projects"));
    settings->ended = true;

    QTest::mouseClick(page().newIntegrationButton(), Qt::LeftButton);
    EXPECT_TRUE(an::tests::app::processEventsUntil(
        []() {
            for (TopLevelProcessBaseControl* process : core().openProcesses().snapshot())
            {
                if (process != nullptr && process->descriptorId() == "multi_image_stacking.actionImage_Integration")
                {
                    return true;
                }
            }
            return false;
        },
        kWaitMs))
        << "New integration... opened no Image Integration";

    window().showHome();
    QTest::mouseClick(page().closeProjectButton(), Qt::LeftButton);
    settleProjects();
    EXPECT_FALSE(projects().isOpen());
}

// The images: each with its type, its history as its steps and their size, and where it is open - "Open in" its
// workspace, or "Closed".
TEST_F(ProjectHome, TheImagesSayTheirHistoryAndWhereTheyAreOpen)
{
    (void)makeProject(QStringLiteral("Images"));
    const QString shown = writeImage(QStringLiteral("images/shown.fits"));
    const QString closed = writeImage(QStringLiteral("images/closed.fits"), 0.6f);
    openFile(shown);
    openFile(closed);
    ImageViewControl* const edited = viewOf(shown);
    ASSERT_NE(edited, nullptr);
    edit(*edited, 0.7f);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&shown]() { return stepFilesOf(shown).size() == 2; }, kWaitMs));
    ImageViewControl* const closing = viewOf(closed);
    ASSERT_NE(closing, nullptr);
    ASSERT_TRUE(closing->close());
    settleProjects();
    ASSERT_TRUE(showProjectHome());

    const std::vector<ProjectHomePage::ImageRow> rows = page().imageRows();
    ASSERT_EQ(rows.size(), 2u);
    const auto rowOf = [&rows](const QString& name) {
        const auto found = std::find_if(rows.begin(), rows.end(),
                                        [&name](const ProjectHomePage::ImageRow& row) { return row.name == name; });
        return found != rows.end() ? *found : ProjectHomePage::ImageRow{};
    };
    EXPECT_EQ(rowOf(QStringLiteral("shown.fits")).state, QStringLiteral("Open in Workspace 1"));
    EXPECT_EQ(rowOf(QStringLiteral("closed.fits")).state, QStringLiteral("Closed"));
    EXPECT_EQ(rowOf(QStringLiteral("shown.fits")).type, QStringLiteral("FITS"));
    EXPECT_TRUE(rowOf(QStringLiteral("shown.fits")).history.startsWith(QStringLiteral("2 steps · ")))
        << rowOf(QStringLiteral("shown.fits")).history.toStdString();
    EXPECT_TRUE(rowOf(QStringLiteral("closed.fits")).history.startsWith(QStringLiteral("1 step · ")))
        << rowOf(QStringLiteral("closed.fits")).history.toStdString();
}

// The images' rows keep their own height however much room the page has: the card reaches down as far as the column
// beside it, and what it has to spare is left under its last row.
TEST_F(ProjectHome, TheImageRowsKeepTheirHeightOnATallPage)
{
    (void)makeProject(QStringLiteral("Rows"));
    openFile(writeImage(QStringLiteral("rows/one.fits"), 0.3f));
    openFile(writeImage(QStringLiteral("rows/two.fits"), 0.6f));
    ASSERT_TRUE(showProjectHome());
    // The rows are made again once their files' facts are read: they are measured after that.
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        []() {
            const std::vector<ProjectHomePage::ImageRow> rows = page().imageRows();
            return rows.size() == 2u && std::all_of(rows.begin(), rows.end(), [](const ProjectHomePage::ImageRow& row) {
                       return !row.modified.isEmpty();
                   });
        },
        kWaitMs));
    QCoreApplication::processEvents();
    QWidget* const content = page().widget();
    ASSERT_NE(content, nullptr);
    content->layout()->activate();
    std::vector<QWidget*> rows;
    for (QWidget* const row : content->findChildren<QWidget*>(QStringLiteral("projectHomeImageRow")))
    {
        if (row->isVisible())
        {
            rows.push_back(row);
        }
    }
    ASSERT_EQ(rows.size(), 2u);
    QWidget* const imagesCard = rows.front()->parentWidget();
    ASSERT_NE(imagesCard, nullptr);
    ASSERT_GT(imagesCard->height(), imagesCard->sizeHint().height() + rows.front()->sizeHint().height())
        << "the page has no room to spare at this size: the case shows nothing";

    const auto bottomOf = [content](const QWidget* widget) {
        return widget->mapTo(content, QPoint(0, widget->height())).y();
    };
    for (const QWidget* const row : rows)
    {
        EXPECT_EQ(row->height(), row->sizeHint().height()) << "a row was stretched to " << row->height() << " px";
    }
    ASSERT_NE(page().notes(), nullptr);
    EXPECT_EQ(bottomOf(imagesCard), bottomOf(page().notes()->parentWidget()))
        << "the card does not reach as far as the column beside it";
}

// The workspaces are tiles: each its name, its images and its arrangement, a miniature of it, the one used last marked;
// Open shows a workspace.
TEST_F(ProjectHome, TheWorkspacesAreTilesOfTheirArrangement)
{
    (void)makeProject(QStringLiteral("Tiles"));
    openFile(writeImage(QStringLiteral("tiles/a.fits"), 0.2f));
    openFile(writeImage(QStringLiteral("tiles/b.fits"), 0.8f));
    core().actions().action(an::ui::shell::ids::kSplitAllImages)->trigger();
    settleProjects();
    ASSERT_TRUE(showProjectHome());

    const std::vector<ProjectHomePage::WorkspaceTile*> tiles = page().workspaceTiles();
    ASSERT_EQ(tiles.size(), 1u);
    EXPECT_EQ(tiles[0]->name(), QStringLiteral("Workspace 1"));
    EXPECT_TRUE(tiles[0]->isLast());
    EXPECT_EQ(tiles[0]->factsText(), QStringLiteral("2 images · 2 × 1 split"));
    ASSERT_NE(tiles[0]->miniature(), nullptr);
    EXPECT_EQ(tiles[0]->miniature()->miniatureLayout().groups.size(), 2u);
    QTest::mouseClick(tiles[0]->openButton(), Qt::LeftButton);
    settleProjects();
    EXPECT_TRUE(window().isWorkspaceShown());
}

// The workspaces' tiles are two to a row: one workspace's tile is as wide as each of two, and leaves the other half of
// the card empty, rather than stretching across it.
TEST_F(ProjectHome, AWorkspaceTileIsOneOfTwoToARow)
{
    (void)makeProject(QStringLiteral("Halves"));
    openFile(writeImage(QStringLiteral("halves/a.fits"), 0.2f));
    settleProjects();
    ASSERT_TRUE(showProjectHome());
    std::vector<ProjectHomePage::WorkspaceTile*> tiles = page().workspaceTiles();
    ASSERT_EQ(tiles.size(), 1u);
    const QWidget* const card = tiles[0]->parentWidget();
    ASSERT_NE(card, nullptr);
    const int alone = tiles[0]->width();
    EXPECT_LT(alone, card->width() * 2 / 3)
        << "one tile took " << alone << " px of a card " << card->width() << " px wide";

    QAction* const newWorkspace = window().shellActions().action(shellIds::kNewWorkspace);
    ASSERT_NE(newWorkspace, nullptr);
    newWorkspace->trigger();
    settleProjects();
    ASSERT_TRUE(showProjectHome());
    tiles = page().workspaceTiles();
    ASSERT_EQ(tiles.size(), 2u);
    EXPECT_EQ(tiles[0]->width(), alone);
    EXPECT_EQ(tiles[1]->width(), alone);
    EXPECT_EQ(tiles[0]->y(), tiles[1]->y()) << "two tiles are not on one row";
}

// The integrations are the project's jobs: a session of its own database, with its status, and Open opens it.
TEST_F(ProjectHome, TheIntegrationsAreTheProjectsJobs)
{
    const QString folder = makeProject(QStringLiteral("Jobs"));
    const QString database = QDir(folder).filePath(QStringLiteral("integration/") +
                                                   QString::fromLatin1(ImageIntegration::kSessionDatabaseFileName));
    {
        IntegrationDataModel model;
        model.setOutputDirectory(QDir(folder).filePath(QStringLiteral("integration/carina")));
        IntegrationDatabase store(database);
        ASSERT_TRUE(store.saveSession(QStringLiteral("Carina LRGB"), model.toJson()))
            << store.lastError().toStdString();
    }
    window().integrationJobs().refresh();
    ASSERT_TRUE(an::tests::app::processEventsUntil([]() { return !window().integrationJobs().isReading(); }, kWaitMs));
    ASSERT_TRUE(showProjectHome());
    ASSERT_TRUE(an::tests::app::processEventsUntil([]() { return !page().integrationRows().isEmpty(); }, kWaitMs));
    EXPECT_EQ(page().integrationRows(), QStringList{QStringLiteral("Carina LRGB · Not run")});
}

// The notes are kept in notes.md, and read back when the project is opened again.
TEST_F(ProjectHome, TheNotesAreSavedToNotesMdAndReadBack)
{
    const QString folder = makeProject(QStringLiteral("Notes"));
    ASSERT_TRUE(showProjectHome());
    ASSERT_NE(page().notes(), nullptr);
    page().notes()->setFocus();
    QTest::keyClicks(page().notes(), QStringLiteral("Red needs a second night."));
    page().saveNotes();
    QFile notes(QDir(folder).filePath(QStringLiteral("notes.md")));
    ASSERT_TRUE(notes.open(QIODevice::ReadOnly));
    EXPECT_EQ(QString::fromUtf8(notes.readAll()), QStringLiteral("Red needs a second night."));
    notes.close();

    ASSERT_TRUE(projects().close());
    settleProjects();
    flows().openProjectFolder(folder);
    settleProjects();
    ASSERT_TRUE(projects().isOpen());
    ASSERT_TRUE(showProjectHome());
    EXPECT_EQ(page().notes()->toPlainText(), QStringLiteral("Red needs a second night."));
}

// The storage card's figures equal the folder's: its undo history, its integration data, the rest and the whole.
TEST_F(Storage, ReportEqualsTheFolder)
{
    const QString folder = makeProject(QStringLiteral("Storage"));
    const QString image = writeImage(QStringLiteral("storage/one.fits"));
    openFile(image);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&image]() { return stepFilesOf(image).size() == 1; }, kWaitMs));
    // The history settled: its index lists its step, and is not written again while the folder is measured.
    const QString directory = projects().historyDirectoryFor(projects().imageFor(image)->id);
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&directory]() {
            const std::optional<an::ui::history::HistoryIndex> index = an::ui::history::readHistoryIndex(directory);
            return index.has_value() && index->steps.size() == 1u;
        },
        kWaitMs));
    QTest::qWait(200);
    // Integration data: a master, and a calibrated frame that can be made again.
    const QDir integration(QDir(folder).filePath(QStringLiteral("integration")));
    ASSERT_TRUE(QDir().mkpath(integration.filePath(QStringLiteral("carina/calibrated_lights"))));
    for (const auto& [name, size] :
         std::vector<std::pair<QString, qint64>>{{QStringLiteral("carina/stacked_L.fit"), 3000},
                                                 {QStringLiteral("carina/calibrated_lights/light_cal.fit"), 7000}})
    {
        QFile file(integration.filePath(name));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(QByteArray(static_cast<int>(size), 'x')), size);
    }
    ASSERT_TRUE(showProjectHome());
    const an::ui::shell::ProjectStorageReport report = *page().storage();
    EXPECT_EQ(report.historyBytes + report.unlistedHistoryBytes,
              bytesBelow(QDir(folder).filePath(QStringLiteral("history"))));
    EXPECT_EQ(report.intermediateBytes, 7000u);
    EXPECT_EQ(report.integrationBytes, 3000u);
    EXPECT_EQ(report.historyBytes + report.unlistedHistoryBytes + report.integrationBytes + report.intermediateBytes +
                  report.otherBytes,
              bytesBelow(folder));
    // The legend says the same figures.
    const std::vector<std::pair<QString, std::uint64_t>> legend = page().storageLegend();
    ASSERT_GE(legend.size(), 4u);
    EXPECT_EQ(legend[0].second, report.historyBytes + report.unlistedHistoryBytes);
    EXPECT_EQ(legend[1].second, report.integrationBytes);
    EXPECT_EQ(legend[2].second, report.intermediateBytes);
    EXPECT_EQ(legend[3].second, report.otherBytes);
}

// The storage is measured off the GUI thread: asked, the card has no figures yet, and they land from a worker.
TEST_F(Storage, IsComputedOffTheGuiThread)
{
    (void)makeProject(QStringLiteral("Measured"));
    window().showHome();
    page().refresh();
    EXPECT_FALSE(page().storage().has_value()) << "the storage was measured before the call returned";
    ASSERT_TRUE(an::tests::app::processEventsUntil([]() { return page().storage().has_value(); }, kWaitMs));
    EXPECT_FALSE(page().storage()->measuredOnGuiThread);
}

// A project whose undo history is above the setting's threshold asks once, as it opens, to be compacted; the question's
// action opens the Compact history dialog.
TEST_F(Storage, AHistoryAboveTheThresholdAsksOnceToCompact)
{
    an::ui::IGlobalSettingsProvider* const settings = core().globalSettings();
    (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_COMPACT_ASK_THRESHOLD_GB), QStringLiteral("1"));
    const QString folder = makeProject(QStringLiteral("Heavy"));
    ASSERT_TRUE(projects().close());
    settleProjects();
    // An undo history of more than a gigabyte, as a project's folder holds it.
    const QString history = QDir(folder).filePath(QStringLiteral("history/heavy"));
    ASSERT_TRUE(QDir().mkpath(history));
    {
        QFile filler(QDir(history).filePath(QStringLiteral("filler.bin")));
        ASSERT_TRUE(filler.open(QIODevice::WriteOnly));
        ASSERT_TRUE(filler.resize(qint64{1100} * 1000 * 1000));
    }
    window().snackbar()->hide();
    QuestionCounter counter(*window().snackbar());
    window().snackbar()->installEventFilter(&counter);
    int& asked = counter.asked;
    flows().openProjectFolder(folder);
    settleProjects();
    ASSERT_TRUE(projects().isOpen());
    ASSERT_TRUE(an::tests::app::processEventsUntil([&asked]() { return asked == 1; }, kWaitMs))
        << "a heavy history did not ask to be compacted";
    EXPECT_TRUE(window().snackbar()->text().contains(QStringLiteral("undo history")))
        << window().snackbar()->text().toStdString();

    // Measured again, it does not ask again.
    window().snackbar()->hide();
    ASSERT_TRUE(showProjectHome());
    page().refresh();
    ASSERT_TRUE(an::tests::app::processEventsUntil([]() { return page().storage().has_value(); }, kWaitMs));
    QCoreApplication::processEvents();
    EXPECT_EQ(asked, 1);

    window().snackbar()->removeEventFilter(&counter);
    QFile::remove(QDir(history).filePath(QStringLiteral("filler.bin")));
}

// The Compact history dialog frees what it says it frees: it removes the step files and rewrites the index.
TEST_F(Storage, TheCompactHistoryDialogFreesWhatItSays)
{
    (void)makeProject(QStringLiteral("Compact"));
    const QString image = writeImage(QStringLiteral("compact/one.fits"));
    openFile(image);
    ImageViewControl* const view = viewOf(image);
    ASSERT_NE(view, nullptr);
    edit(*view, 0.5f);
    edit(*view, 0.6f);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&image]() { return stepFilesOf(image).size() == 3; }, kWaitMs));
    const QString directory = projects().historyDirectoryFor(projects().imageFor(image)->id);
    ASSERT_TRUE(showProjectHome());
    ASSERT_NE(page().compactHistoryButton(), nullptr);
    EXPECT_TRUE(page().compactHistoryButton()->isEnabled());
    QTest::mouseClick(page().compactHistoryButton(), Qt::LeftButton);
    ASSERT_TRUE(
        an::tests::app::processEventsUntil([]() { return window().compactHistoryDialog() != nullptr; }, kWaitMs));
    an::ui::CompactHistoryDialog& dialog = *window().compactHistoryDialog();
    dialog.keepCurrentState()->setChecked(true);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&dialog]() { return dialog.plannedBytes() > 0u; }, kWaitMs));
    const std::uint64_t said = dialog.plannedBytes();
    EXPECT_TRUE(dialog.warningText().contains(QStringLiteral("cannot be undone")));
    std::uint64_t freed = 0;
    QObject::connect(&dialog, &an::ui::CompactHistoryDialog::compacted, &dialog,
                     [&freed](const quint64 bytes) { freed = bytes; });
    QTest::mouseClick(dialog.compactButton(), Qt::LeftButton);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&freed]() { return freed > 0u; }, kWaitMs));
    EXPECT_EQ(freed, said) << "it did not free what it said it frees";
    EXPECT_EQ(stepFilesOf(image).size(), 1) << "the step files were not removed";
    const std::optional<an::ui::history::HistoryIndex> index = an::ui::history::readHistoryIndex(directory);
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(index->steps.size(), 1u) << "the index was not rewritten";
}

// A workspace split in two by two is four groups in its four quarters, each painted with its image's picture.
TEST_F(WorkspaceMiniature, ASplitIsItsGroupsInTheirPlaces)
{
    (void)makeProject(QStringLiteral("Quarters"));
    const std::vector<float> values = {0.1f, 0.4f, 0.7f, 0.95f};
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        openFile(writeImage(QStringLiteral("quarters/q%1.fits").arg(index), values[index]));
    }
    core().actions().action(an::ui::shell::ids::kSplitAllImages)->trigger();
    settleProjects();
    ASSERT_TRUE(showProjectHome());
    const std::vector<ProjectHomePage::WorkspaceTile*> tiles = page().workspaceTiles();
    ASSERT_EQ(tiles.size(), 1u);
    const an::ui::observatory::MiniatureLayout layout = tiles[0]->miniature()->miniatureLayout();
    ASSERT_EQ(layout.groups.size(), 4u);
    EXPECT_EQ(an::ui::observatory::miniatureArrangementText(layout), QStringLiteral("2 × 2 split"));
    // The four quarters are held, each by one group of one image.
    std::vector<QPointF> centres;
    for (const an::ui::observatory::MiniatureGroup& group : layout.groups)
    {
        EXPECT_EQ(group.tabs.size(), 1);
        EXPECT_NEAR(group.place.width(), 0.5, 0.05);
        EXPECT_NEAR(group.place.height(), 0.5, 0.05);
        centres.push_back(group.place.center());
    }
    for (const QPointF quarter : {QPointF(0.25, 0.25), QPointF(0.75, 0.25), QPointF(0.25, 0.75), QPointF(0.75, 0.75)})
    {
        EXPECT_TRUE(std::any_of(centres.begin(), centres.end(),
                                [quarter](const QPointF& centre) {
                                    return std::abs(centre.x() - quarter.x()) < 0.05 &&
                                           std::abs(centre.y() - quarter.y()) < 0.05;
                                }))
            << "no group holds the quarter at " << quarter.x() << ", " << quarter.y();
    }
    // Each quarter is painted with the picture of its image once the pictures are made.
    ASSERT_TRUE(an::tests::app::processEventsUntil([&tiles]() { return tiles[0]->miniature()->picturesShown() == 4; },
                                                   kWaitMs));
}

// A workspace of tabs is one group with its strip: its tabs, and the image shown painted under it.
TEST_F(WorkspaceMiniature, ATabbedGroupShowsItsStripAndTheImageShown)
{
    (void)makeProject(QStringLiteral("Tabs"));
    openFile(writeImage(QStringLiteral("tabs/a.fits"), 0.2f));
    openFile(writeImage(QStringLiteral("tabs/b.fits"), 0.5f));
    openFile(writeImage(QStringLiteral("tabs/c.fits"), 0.8f));
    core().actions().action(an::ui::shell::ids::kGroupAllImages)->trigger();
    settleProjects();
    ASSERT_TRUE(showProjectHome());
    const std::vector<ProjectHomePage::WorkspaceTile*> tiles = page().workspaceTiles();
    ASSERT_EQ(tiles.size(), 1u);
    const an::ui::observatory::MiniatureLayout layout = tiles[0]->miniature()->miniatureLayout();
    ASSERT_EQ(layout.groups.size(), 1u);
    EXPECT_EQ(layout.groups[0].tabs.size(), 3);
    EXPECT_GE(layout.groups[0].current, 0);
    EXPECT_EQ(an::ui::observatory::miniatureArrangementText(layout), QStringLiteral("3 tabs"));
    EXPECT_EQ(tiles[0]->factsText(), QStringLiteral("3 images · 3 tabs"));
    EXPECT_TRUE(tiles[0]->miniature()->showsTabStrip(0));
}

} // namespace an::tests::observatory
