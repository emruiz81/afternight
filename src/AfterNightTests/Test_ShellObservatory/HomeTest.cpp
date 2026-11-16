// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Home in the Observatory window as a person uses it: the welcome with no recent file and the rows with some; Home
// reached with images open; the quick actions launching what the dashboard's launch and customized as the dashboard's
// are; the Continue row; the recent images and sequences searched, sorted and shown as a grid or a list, a sequence's
// tile with its frame count read from its file; the tile's menu; and a file dropped on Home opening.

#include "ObservatoryWindowCase.h"

#include "pages/HomePage.h"

#include "Test_ShellClassic/AppTestSupport.h"
#include "components/AnIconRail.h"
#include "controls/DashboardPreferences.h"
#include "controls/HomeContinueRow.h"
#include "controls/HomeQuickActions.h"
#include "controls/HomeRecentGrid.h"
#include "controls/HomeWelcome.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/NewSequenceControl.h"
#include "controls/SequenceContainers.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "controls/settings/SettingsKeys.h"
#include "core/ApplicationCore.h"
#include "services/OpenProcessRegistry.h"
#include "services/RecentFilesModel.h"
#include "views/ViewService.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QStyleOption>
#include <QFontMetricsF>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QUrl>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace an::tests::observatory
{
namespace
{

using an::ui::HomeRecentGrid;
using an::ui::HomeRecentTile;
using an::ui::observatory::HomePage;

constexpr int kOpenMs = 20000;

class Home : public ObservatoryWindowCase
{
protected:
    [[nodiscard]] static HomePage& home()
    {
        return *window().homePage();
    }

    /// A folder for the case's files, made once the suite's temporary directory is in force.
    [[nodiscard]] static QString folder()
    {
        static QTemporaryDir files(QDir::temp().filePath(QStringLiteral("observatory-home-XXXXXX")));
        return files.path();
    }

    /// A grey FITS image written under a name.
    [[nodiscard]] static QString writeImage(const QString& name, const float value = 0.4f)
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
        EXPECT_TRUE(intent.hasValue() && an::ui::writeViewImage(session(), *asset, path, *intent, {}).ok());
        return QDir::cleanPath(path);
    }

    /// A SER sequence of grey frames written under a name.
    [[nodiscard]] static QString writeSequence(const QString& name, const std::uint32_t frames)
    {
        const QString path = QDir(folder()).filePath(name);
        an::Result<an::ui::SequenceWriter> writer = an::ui::SequenceWriter::create(
            path, an::ui::SequenceWriteSettings{.container = an::SequenceFormat::Ser, .frameCount = frames});
        EXPECT_TRUE(writer.hasValue()) << writer.status().message();
        an::Result<an::consumer::OperationScope> scope =
            session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
        EXPECT_TRUE(scope.hasValue()) << scope.status().message();
        if (!writer.hasValue() || !scope.hasValue())
        {
            return {};
        }
        for (std::uint32_t frame = 0u; frame < frames; ++frame)
        {
            std::optional<an::consumer::ImageAsset> asset =
                an::tests::app::filledAsset(session(), 0.2f + 0.1f * static_cast<float>(frame), 40u, 30u);
            EXPECT_TRUE(asset.has_value());
            if (!asset.has_value())
            {
                return {};
            }
            an::Result<an::consumer::AssetReadLease> lease = asset->read();
            EXPECT_TRUE(lease.hasValue());
            if (!lease.hasValue())
            {
                return {};
            }
            EXPECT_TRUE(writer->append(scope->engine(), scope->execution(), lease->image()).ok());
        }
        EXPECT_TRUE(writer->finish(scope->execution()).hasValue());
        return QDir::cleanPath(path);
    }

    /// Whether a recent file has its thumbnail on disk.
    [[nodiscard]] static bool hasThumbnail(const QString& path)
    {
        const QString thumbnail = an::ui::appContext()->getThumbnailPathProvider()->selectThumbnailPath(path);
        return !thumbnail.isEmpty() && QFileInfo::exists(thumbnail);
    }

    [[nodiscard]] static bool isRecent(const QString& path)
    {
        for (const std::string& entry : core().recentFiles().entries())
        {
            if (QDir::cleanPath(QString::fromStdString(entry)) == path)
            {
                return true;
            }
        }
        return false;
    }

    /// Opens files as the person's files are opened - through the window - and waits for each to be shown and to be
    /// a recent file with its thumbnail.
    static void openFiles(const QStringList& paths)
    {
        an::ui::appContext()->getRecentFilesService()->openDroppedFiles(paths);
        for (const QString& path : paths)
        {
            EXPECT_TRUE(an::tests::app::processEventsUntil(
                [&path]() {
                    for (ImageViewControl* view : views())
                    {
                        if (view->hasImage() && QDir::cleanPath(view->document().names().sourcePath) == path)
                        {
                            return isRecent(path) && hasThumbnail(path);
                        }
                    }
                    return false;
                },
                kOpenMs))
                << path.toStdString() << " did not open as a recent file with its thumbnail";
        }
    }

    /// Clears the recent files from Home's More menu, answering the question it asks.
    static void clearRecentFiles()
    {
        const std::shared_ptr<an::tests::app::WindowPoll> poll = an::tests::app::answerWhenShown([](QWidget* shown) {
            auto* const box = qobject_cast<QMessageBox*>(shown);
            if (box == nullptr)
            {
                return false;
            }
            box->done(QMessageBox::Yes);
            return true;
        });
        QAction* clear = nullptr;
        for (QAction* action : home().quickActions()->moreMenu()->actions())
        {
            if (action->text() == DashboardPreferences::actionTitleForId(
                                      QString::fromUtf8(DashboardPreferences::ACTION_ID_CLEAR_RECENTS)))
            {
                clear = action;
            }
        }
        ASSERT_NE(clear, nullptr);
        if (clear->isEnabled())
        {
            clear->trigger();
        }
        poll->ended = true;
        QCoreApplication::processEvents();
        ASSERT_TRUE(core().recentFiles().entries().empty());
    }

    /// Shows Home and lets it read the recent files.
    static void showHomeAndSettle()
    {
        window().showHome();
        QCoreApplication::processEvents();
        QTest::qWait(30);
    }

    /// The menu a right-click on a widget pops up, by its name.
    [[nodiscard]] static QMenu* rightClick(QWidget& widget, const QString& menuName)
    {
        const QPoint at(6, 6);
        QContextMenuEvent event(QContextMenuEvent::Mouse, at, widget.mapToGlobal(at));
        QCoreApplication::sendEvent(&widget, &event);
        QMenu* found = nullptr;
        (void)an::tests::app::processEventsUntil(
            [&found, &menuName]() {
                for (QWidget* top : QApplication::topLevelWidgets())
                {
                    auto* menu = qobject_cast<QMenu*>(top);
                    if (menu != nullptr && menu->isVisible() && menu->objectName() == menuName)
                    {
                        found = menu;
                    }
                }
                return found != nullptr;
            },
            5000);
        return found;
    }

    /// Triggers an entry of a popped-up menu by its texts and closes the menu.
    static bool choose(QMenu* menu, const QStringList& path)
    {
        QMenu* current = menu;
        QAction* found = nullptr;
        for (const QString& text : path)
        {
            found = nullptr;
            for (QAction* action : current != nullptr ? current->actions() : QList<QAction*>())
            {
                if (action->text() == text)
                {
                    found = action;
                }
            }
            if (found == nullptr)
            {
                break;
            }
            current = found->menu();
        }
        if (found != nullptr && found->isEnabled())
        {
            found->trigger();
        }
        if (menu != nullptr)
        {
            menu->close();
        }
        QCoreApplication::processEvents();
        return found != nullptr;
    }

    [[nodiscard]] static HomeRecentTile* tileFor(const QString& path)
    {
        for (HomeRecentTile* tile : home().recentGrid()->tiles())
        {
            if (tile->filePath() == path)
            {
                return tile;
            }
        }
        return nullptr;
    }
};

} // namespace

// With no recent file Home is the welcome; once a file was opened, Home shows the Continue row and the recent images
// instead, and Clear recents in its More menu - which asks first - brings the welcome back.
TEST_F(Home, WithNoRecentFileShowsTheWelcomeAndWithSomeItsRows)
{
    clearRecentFiles();
    showHomeAndSettle();
    EXPECT_TRUE(home().showsWelcome());
    EXPECT_TRUE(home().welcome()->isVisible());
    EXPECT_FALSE(home().continueRow()->isVisible());
    EXPECT_FALSE(home().recentGrid()->isVisible());
    EXPECT_TRUE(home().quickActions()->isVisible()) << "the quick actions are Home's in both states";

    const QString image = writeImage(QStringLiteral("home-first-run.fits"));
    openFiles({image});
    showHomeAndSettle();
    EXPECT_FALSE(home().showsWelcome());
    EXPECT_FALSE(home().welcome()->isVisible());
    EXPECT_TRUE(home().continueRow()->isVisible());
    EXPECT_TRUE(home().recentGrid()->isVisible());

    clearRecentFiles();
    EXPECT_TRUE(an::tests::app::processEventsUntil([]() { return home().showsWelcome(); }, 5000));
}

// Home is a place of its own while images are open: the rail and the Home action show it, the images stay open, and the
// workspace is one click away.
TEST_F(Home, IsReachableWithImagesOpen)
{
    ImageViewControl* const view = openView(QStringLiteral("home-reachable"));
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(window().isWorkspaceShown());

    const QRect place =
        window().rail()->itemRect(QString::fromLatin1(an::ui::observatory::ObservatoryWindow::kRailHome));
    ASSERT_TRUE(place.isValid());
    QTest::mouseClick(window().rail(), Qt::LeftButton, Qt::NoModifier, place.center());
    QCoreApplication::processEvents();
    EXPECT_TRUE(window().isHomeShown());
    EXPECT_TRUE(home().isVisible());
    EXPECT_EQ(views().size(), 1u) << "showing Home closed an image";

    window().showViews();
    EXPECT_TRUE(window().isWorkspaceShown());
    window().showHome();
    EXPECT_TRUE(window().isHomeShown());
}

// The quick actions launch what the dashboard's launch: Open file the file dialog, New sequence its window, Image
// integration its process.
TEST_F(Home, TheQuickActionsLaunchWhatTheDashboardsLaunch)
{
    showHomeAndSettle();
    an::ui::HomeQuickActions& actions = *home().quickActions();

    bool asked = false;
    const std::shared_ptr<an::tests::app::WindowPoll> poll = an::tests::app::answerWhenShown([&asked](QWidget* shown) {
        auto* const dialog = qobject_cast<QFileDialog*>(shown);
        if (dialog == nullptr)
        {
            return false;
        }
        asked = true;
        dialog->reject();
        return true;
    });
    QAbstractButton* open = actions.actionButton(QString::fromUtf8(DashboardPreferences::ACTION_ID_OPEN_FILE));
    ASSERT_NE(open, nullptr);
    QTest::mouseClick(open, Qt::LeftButton);
    EXPECT_TRUE(an::tests::app::processEventsUntil([&asked]() { return asked; }, 5000)) << "Open file showed no dialog";
    poll->ended = true;

    QAbstractButton* sequence = actions.actionButton(QString::fromUtf8(DashboardPreferences::ACTION_ID_NEW_SEQUENCE));
    ASSERT_NE(sequence, nullptr);
    QTest::mouseClick(sequence, Qt::LeftButton);
    NewSequenceControl* opened = nullptr;
    EXPECT_TRUE(an::tests::app::processEventsUntil([&opened]() {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (auto* const control = dynamic_cast<NewSequenceControl*>(widget);
                control != nullptr && control->isVisible())
            {
                opened = control;
            }
        }
        return opened != nullptr;
    })) << "New sequence opened no window";
    if (opened != nullptr)
    {
        opened->close();
    }

    const std::size_t before = core().openProcesses().snapshot().size();
    QAbstractButton* integration =
        actions.actionButton(QString::fromUtf8(DashboardPreferences::ACTION_ID_IMAGE_INTEGRATION));
    ASSERT_NE(integration, nullptr);
    QTest::mouseClick(integration, Qt::LeftButton);
    EXPECT_TRUE(an::tests::app::processEventsUntil(
        [before]() { return core().openProcesses().snapshot().size() > before; }, kOpenMs))
        << "Image integration opened no process";
}

// The row's right-click customizes the quick actions as the dashboard's menu does: an action moved later is later in
// the row and in the dashboard's setting, and Reset puts every action back.
TEST_F(Home, TheQuickActionsAreCustomizedAsOnTheDashboard)
{
    showHomeAndSettle();
    an::ui::HomeQuickActions& actions = *home().quickActions();
    const QString open = QString::fromUtf8(DashboardPreferences::ACTION_ID_OPEN_FILE);
    const QString sequence = QString::fromUtf8(DashboardPreferences::ACTION_ID_NEW_SEQUENCE);
    ASSERT_TRUE(
        choose(rightClick(actions, QStringLiteral("homeQuickActionsMenu")), {QStringLiteral("Reset quick actions")}));
    ASSERT_EQ(actions.rowActionIds().value(0), open);

    ASSERT_TRUE(choose(rightClick(actions, QStringLiteral("homeQuickActionsMenu")),
                       {QStringLiteral("Open file"), QStringLiteral("Move later")}));
    EXPECT_EQ(actions.rowActionIds().value(0), sequence);
    EXPECT_EQ(actions.rowActionIds().value(1), open);
    EXPECT_GT(actions.actionButton(open)->geometry().x(), actions.actionButton(sequence)->geometry().x());
    EXPECT_TRUE(an::ui::appContext()
                    ->getGlobalSettings()
                    ->select(QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_ACTION_ORDER))
                    .startsWith(QStringLiteral("new_sequence\nopen_file")));

    ASSERT_TRUE(
        choose(rightClick(actions, QStringLiteral("homeQuickActionsMenu")), {QStringLiteral("Reset quick actions")}));
    EXPECT_EQ(actions.rowActionIds().value(0), open);
}

// The Continue row features the image last opened, and its Continue shows that image again.
TEST_F(Home, TheContinueRowShowsTheLastImageAndContinues)
{
    const QString image = writeImage(QStringLiteral("home-continue.fits"), 0.6f);
    openFiles({image});
    for (ImageViewControl* view : views())
    {
        core().viewService().removeViewControl(view, true);
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    showHomeAndSettle();
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&image]() { return home().continueRow()->featuredFilePath() == image; }, 5000))
        << home().continueRow()->featuredFilePath().toStdString();

    auto* const continueButton =
        home().continueRow()->findChild<QAbstractButton*>(QStringLiteral("dashboardFeaturedContinueButton"));
    ASSERT_NE(continueButton, nullptr);
    QTest::mouseClick(continueButton, Qt::LeftButton);
    EXPECT_TRUE(an::tests::app::processEventsUntil(
        [&image]() {
            for (ImageViewControl* view : views())
            {
                if (view->hasImage() && QDir::cleanPath(view->document().names().sourcePath) == image)
                {
                    return window().isWorkspaceShown();
                }
            }
            return false;
        },
        kOpenMs))
        << "Continue did not show the image";
}

// The recent images and sequences: a sequence's tile carries its film glyph and the frames its file holds, as the
// probe reads them; the search narrows the tiles, the sort orders them, and the list shows the same tiles as the grid.
TEST_F(Home, TheRecentImagesAndSequencesAreSearchedSortedAndShownAsAGridOrAList)
{
    const QString image = writeImage(QStringLiteral("home-recent-image.fits"));
    const QString sequence = writeSequence(QStringLiteral("home-recent-planet.ser"), 7u);
    openFiles({image, sequence});
    showHomeAndSettle();
    HomeRecentGrid& grid = *home().recentGrid();
    grid.searchField()->setText(QStringLiteral("home-recent"));
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&grid, &image, &sequence]() {
            const QStringList order = grid.visibleFileOrder();
            return order.size() == 2 && order.contains(image) && order.contains(sequence);
        },
        5000))
        << grid.visibleFileOrder().join(QStringLiteral(", ")).toStdString();

    HomeRecentTile* const sequenceTile = tileFor(sequence);
    ASSERT_NE(sequenceTile, nullptr);
    EXPECT_TRUE(sequenceTile->isSequence());
    EXPECT_EQ(sequenceTile->typeText(), QStringLiteral("SER"));
    EXPECT_TRUE(an::tests::app::processEventsUntil(
        [sequenceTile]() { return sequenceTile->factsText().contains(QStringLiteral("7 frames")); }, 10000))
        << sequenceTile->factsText().toStdString();
    EXPECT_TRUE(sequenceTile->factsText().startsWith(QStringLiteral("40x30")))
        << sequenceTile->factsText().toStdString();
    EXPECT_TRUE(an::tests::app::processEventsUntil([sequenceTile]() { return sequenceTile->hasPicture(); }, 10000));

    grid.searchField()->setText(QStringLiteral("planet"));
    EXPECT_TRUE(an::tests::app::processEventsUntil(
        [&grid, &sequence]() { return grid.visibleFileOrder() == QStringList{sequence}; }, 5000));
    grid.searchField()->setText(QStringLiteral("home-recent"));
    grid.sortBox()->setCurrentIndex(grid.sortBox()->findData(DashboardPreferences::RECENT_SORT_NAME));
    EXPECT_TRUE(an::tests::app::processEventsUntil(
        [&grid, &image, &sequence]() { return grid.visibleFileOrder() == (QStringList{image, sequence}); }, 5000));

    QTest::mouseClick(grid.listButton(), Qt::LeftButton);
    EXPECT_EQ(grid.arrangement(), HomeRecentGrid::Arrangement::List);
    QStringList listed;
    for (const HomeRecentTile* tile : grid.tiles())
    {
        listed << tile->filePath();
    }
    EXPECT_EQ(listed, (QStringList{image, sequence}));
    QTest::mouseClick(grid.gridButton(), Qt::LeftButton);
    grid.sortBox()->setCurrentIndex(grid.sortBox()->findData(DashboardPreferences::RECENT_SORT_RECENT));
    grid.searchField()->clear();
}

// The sort box beside the search shows each of its choices whole: its text field holds Recent, Name and Type.
TEST_F(Home, TheSortBoxShowsEachChoiceWhole)
{
    const QString image = writeImage(QStringLiteral("home-sort-box.fits"));
    openFiles({image});
    showHomeAndSettle();
    QComboBox& sort = *home().recentGrid()->sortBox();
    ASSERT_TRUE(sort.isVisible());
    const int chosen = sort.currentIndex();
    for (int index = 0; index < sort.count(); ++index)
    {
        sort.setCurrentIndex(index);
        QStyleOptionComboBox option;
        option.initFrom(&sort);
        option.currentText = sort.currentText();
        option.frame = sort.hasFrame();
        const QRect field =
            sort.style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, &sort);
        const int text = static_cast<int>(std::ceil(QFontMetricsF(sort.font()).horizontalAdvance(sort.currentText())));
        EXPECT_GE(field.width(), text) << sort.currentText().toStdString() << " is cut in a sort box " << sort.width()
                                       << " px wide";
    }
    sort.setCurrentIndex(chosen);

    // Opened, its list gives each row room for the check's column, the gap and the whole of its text.
    sort.showPopup();
    QAbstractItemView* const list = sort.view();
    ASSERT_TRUE(an::tests::app::processEventsUntil([list]() { return list->isVisible(); }, 5000));
    const QFontMetricsF metrics(sort.font());
    const int decoration = list->iconSize().isValid()
                               ? list->iconSize().width()
                               : sort.style()->pixelMetric(QStyle::PM_ListViewIconSize, nullptr, list);
    const int column = (std::max)(decoration + 4, 16);
    for (int row = 0; row < sort.count(); ++row)
    {
        // What the list shows of the row: its viewport cuts a row laid out wider than it.
        const QRect place = list->visualRect(sort.model()->index(row, 0));
        const int shown = (std::min)(place.right(), list->viewport()->rect().right()) - place.left() + 1;
        const int needed =
            8 + column + 10 + static_cast<int>(std::ceil(metrics.horizontalAdvance(sort.itemText(row)))) + 8;
        EXPECT_GE(shown, needed) << sort.itemText(row).toStdString() << " is cut in the sort box's list";
    }
    sort.hidePopup();
}

// A tile's menu pins its file to the top of the recent images and removes it from the recent files.
TEST_F(Home, ATilesMenuPinsAndRemoves)
{
    const QString first = writeImage(QStringLiteral("home-menu-first.fits"), 0.3f);
    const QString second = writeImage(QStringLiteral("home-menu-second.fits"), 0.5f);
    openFiles({first, second});
    showHomeAndSettle();
    HomeRecentGrid& grid = *home().recentGrid();
    grid.searchField()->setText(QStringLiteral("home-menu"));
    ASSERT_TRUE(an::tests::app::processEventsUntil([&grid]() { return grid.tiles().size() == 2; }, 5000));
    const QString older = grid.visibleFileOrder().value(1);
    HomeRecentTile* const tile = tileFor(older);
    ASSERT_NE(tile, nullptr);

    ASSERT_TRUE(choose(rightClick(*tile, QStringLiteral("homeRecentTileMenu")), {QStringLiteral("Pin to top")}));
    EXPECT_TRUE(an::tests::app::processEventsUntil(
        [&grid, &older]() { return grid.visibleFileOrder().value(0) == older; }, 5000));
    ASSERT_TRUE(
        choose(rightClick(*tileFor(older), QStringLiteral("homeRecentTileMenu")), {QStringLiteral("Unpin from top")}));

    ASSERT_TRUE(choose(rightClick(*tileFor(older), QStringLiteral("homeRecentTileMenu")),
                       {QStringLiteral("Remove from recent files")}));
    EXPECT_TRUE(an::tests::app::processEventsUntil([&older]() { return !isRecent(older); }, 5000));
    EXPECT_TRUE(an::tests::app::processEventsUntil(
        [&grid, &older]() { return !grid.visibleFileOrder().contains(older); }, 5000));
    grid.searchField()->clear();
}

// Showing Home asks the engine for nothing, even with a recent file whose facts cannot be read - a file the engine does
// not read: Home reads the recent files when they change, not when it is shown.
TEST_F(Home, ShowingHomeAsksTheEngineForNothing)
{
    const QString unreadable = QDir(folder()).filePath(QStringLiteral("home-unreadable.fits"));
    {
        QFile file(unreadable);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(6000, 'n'));
    }
    core().recentFiles().entriesForEdit().push_front(QDir::cleanPath(unreadable).toStdString());
    home().reloadRecentFiles();
    ASSERT_NE(openView(QStringLiteral("home-place")), nullptr);
    const auto drained = []() {
        for (int round = 0; round < 2; ++round)
        {
            QTest::qWait(300);
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::processEvents();
        }
    };
    window().showHome();
    drained();
    window().showViews();
    drained();

    const an::consumer::AdmissionSnapshot before = session().admission();
    for (int round = 0; round < 3; ++round)
    {
        window().showHome();
        QCoreApplication::processEvents();
        window().showViews();
        QCoreApplication::processEvents();
    }
    drained();
    const an::consumer::AdmissionSnapshot after = session().admission();
    EXPECT_EQ(after.admitted + after.refusedBusy, before.admitted + before.refusedBusy)
        << "showing Home asked the engine";

    std::deque<std::string>& entries = core().recentFiles().entriesForEdit();
    entries.erase(std::remove(entries.begin(), entries.end(), QDir::cleanPath(unreadable).toStdString()),
                  entries.end());
    home().reloadRecentFiles();
}

// A file dropped on Home opens, wherever on Home it is dropped.
TEST_F(Home, ADroppedFileOpens)
{
    showHomeAndSettle();
    const QString path = writeImage(QStringLiteral("home-dropped.fits"));
    QMimeData data;
    data.setUrls({QUrl::fromLocalFile(path)});

    // The drop goes where the platform sends it: to the widget under the point that takes drops.
    QWidget* const page = &home();
    QWidget* target = page->childAt(page->rect().center());
    if (target == nullptr)
    {
        target = page;
    }
    while (target != nullptr && !target->acceptDrops())
    {
        target = target->parentWidget();
    }
    ASSERT_NE(target, nullptr) << "nothing under Home takes a drop";
    const QPoint at = target->mapFromGlobal(page->mapToGlobal(page->rect().center()));
    QDragEnterEvent enter(at, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    enter.ignore();
    QCoreApplication::sendEvent(target, &enter);
    ASSERT_TRUE(enter.isAccepted()) << "Home refuses a file dragged over it";
    QDropEvent drop(QPointF(at), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    drop.ignore();
    QCoreApplication::sendEvent(target, &drop);
    EXPECT_TRUE(drop.isAccepted());
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
        kOpenMs));
}

} // namespace an::tests::observatory
