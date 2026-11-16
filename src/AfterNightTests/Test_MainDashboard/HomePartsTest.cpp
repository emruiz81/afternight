// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Home's parts, held to the dashboard's: the quick actions in the order and with the overflow the dashboard's
/// settings give, moved, hidden and reset as the dashboard's are; the featured image and its pin, which are the
/// dashboard's settings; the last sequence; the recent files searched and sorted as the dashboard's grid does, shown as
/// a grid or a list, a sequence's tile with its film glyph and frame count, and the tile's menu; and the welcome with
/// no recent file, its cards and its drop zone. The parts read the application double's settings, recent files and
/// thumbnails through the context the suite installs, as the dashboard does.

#include "Test_MainDashboard/DashboardApplication.h"

#include "controls/DashboardOnboardingPanel.h"
#include "controls/DashboardPreferences.h"
#include "controls/EngineVersionReport.h"
#include "controls/HomeContinueRow.h"
#include "controls/HomeParts.h"
#include "controls/HomeQuickActions.h"
#include "controls/HomeRecentGrid.h"
#include "controls/HomeWelcome.h"
#include "controls/MainDashboard.h"
#include "controls/RecentFilesGrid.h"
#include "controls/settings/SettingsKeys.h"
#include "widgets/FeaturedFileCard.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <string>

namespace
{

constexpr int kWaitMs = 3000;

using an::ui::HomeContinueRow;
using an::ui::HomeQuickActions;
using an::ui::HomeRecentGrid;
using an::ui::HomeRecentTile;
using an::ui::HomeWelcome;

GlobalSettingsPersistence& settings()
{
    return *DashboardApplication::instance()->getGlobalSettingsPersistence();
}

QString stored(const char* key)
{
    return settings().select(QString::fromUtf8(key));
}

void store(const char* key, const QString& value)
{
    (void)settings().upsert(QString::fromUtf8(key), value);
}

/// Settings in the format this version reads, so that none is set aside.
void storeTheCurrentFormat()
{
    store(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION, QStringLiteral("4"));
}

/// A file of the recent list: a few bytes under its name.
QString recentFile(const QTemporaryDir& folder, const QString& name)
{
    const QString path = QDir(folder.path()).filePath(name);
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("recent");
    return QDir::cleanPath(path);
}

/// A recent file's thumbnail in a colour, recorded for the file.
void recordThumbnail(const QTemporaryDir& folder, const QString& file, const QColor& colour)
{
    QImage image(48, 32, QImage::Format_ARGB32);
    image.fill(colour);
    const QString path =
        QDir(folder.path()).filePath(QFileInfo(file).completeBaseName() + QStringLiteral("_thumb.png"));
    ASSERT_TRUE(image.save(path, "PNG"));
    DashboardApplication::instance()->setThumbnailPath(file, path);
}

/// Records recent files, most recent first, each with a thumbnail.
void recordRecents(const QTemporaryDir& folder, const QStringList& files)
{
    std::deque<std::string> recents;
    int shade = 60;
    for (const QString& file : files)
    {
        recents.push_back(file.toStdString());
        recordThumbnail(folder, file, QColor(shade, 120, 200 - shade / 2));
        shade += 25;
    }
    DashboardApplication::instance()->setRecentFiles(recents);
}

std::deque<QString> asDeque(const QStringList& files)
{
    return std::deque<QString>(files.cbegin(), files.cend());
}

// ---- The dashboard, read as its own cases read it.

QStringList dashboardRowTitles(MainDashboard& dashboard)
{
    QList<QPushButton*> buttons = dashboard.findChildren<QPushButton*>(QStringLiteral("dashboardActionButton"));
    buttons.erase(std::remove_if(buttons.begin(), buttons.end(),
                                 [](QPushButton* button) {
                                     return !button->isVisible() ||
                                            button->property("dashboardActionPlacement").toString() !=
                                                QStringLiteral("primary");
                                 }),
                  buttons.end());
    std::sort(buttons.begin(), buttons.end(),
              [](QPushButton* lhs, QPushButton* rhs) { return lhs->geometry().x() < rhs->geometry().x(); });
    QStringList titles;
    for (QPushButton* button : buttons)
    {
        titles << button->property("dashboardActionTitle").toString();
    }
    return titles;
}

QMenu* dashboardMoreMenu(MainDashboard& dashboard)
{
    auto* button = dashboard.findChild<QPushButton*>(QStringLiteral("dashboardToolbarOverflowButton"));
    return button != nullptr ? button->menu() : nullptr;
}

/// The actions of a menu that act - not its submenus and separators - by their text.
QStringList actingEntries(const QMenu* menu)
{
    QStringList titles;
    if (menu == nullptr)
    {
        return titles;
    }
    for (const QAction* action : menu->actions())
    {
        if (!action->isSeparator() && action->menu() == nullptr)
        {
            titles << action->text();
        }
    }
    return titles;
}

/// The action a path of texts names in a menu, through its submenus.
QAction* entryAt(QMenu* menu, const QStringList& path)
{
    QMenu* current = menu;
    for (qsizetype step = 0; current != nullptr && step < path.size(); ++step)
    {
        QAction* found = nullptr;
        for (QAction* action : current->actions())
        {
            if (action->text() == path[step])
            {
                found = action;
                break;
            }
        }
        if (found == nullptr)
        {
            return nullptr;
        }
        if (step + 1 == path.size())
        {
            return found;
        }
        current = found->menu();
    }
    return nullptr;
}

QStringList titlesOf(const QStringList& actionIds)
{
    QStringList titles;
    for (const QString& id : actionIds)
    {
        titles << DashboardPreferences::actionTitleForId(id);
    }
    return titles;
}

/// The menu a right-click on a widget pops up, found among the popups by its name.
QMenu* rightClick(QWidget& widget, const QString& menuName, const QPoint& at = QPoint(4, 4))
{
    QContextMenuEvent event(QContextMenuEvent::Mouse, at, widget.mapToGlobal(at));
    QCoreApplication::sendEvent(&widget, &event);
    QMenu* found = nullptr;
    (void)QTest::qWaitFor(
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
        kWaitMs);
    return found;
}

/// Triggers an entry of a popped-up menu and closes it.
bool choose(QMenu* menu, const QStringList& path)
{
    QAction* action = entryAt(menu, path);
    if (action == nullptr || !action->isEnabled())
    {
        if (menu != nullptr)
        {
            menu->close();
        }
        return false;
    }
    action->trigger();
    menu->close();
    QCoreApplication::processEvents();
    return true;
}

/// What a row of quick actions shows and the settings keep.
struct QuickActionsState
{
    QStringList row;
    QStringList more;
    QString order;
    QString hidden;

    bool operator==(const QuickActionsState&) const = default;
};

std::string described(const QuickActionsState& state)
{
    return ("row [" + state.row.join(QStringLiteral(", ")) + "] more [" + state.more.join(QStringLiteral(", ")) +
            "] order [" + state.order.split(QLatin1Char('\n')).join(QStringLiteral(",")) + "] hidden [" +
            state.hidden.split(QLatin1Char('\n')).join(QStringLiteral(",")) + "]")
        .toStdString();
}

QuickActionsState stateOf(MainDashboard& dashboard)
{
    return {dashboardRowTitles(dashboard), actingEntries(dashboardMoreMenu(dashboard)),
            stored(SettingsKeys::INTERFACE_DASHBOARD_ACTION_ORDER),
            stored(SettingsKeys::INTERFACE_DASHBOARD_HIDDEN_ACTIONS)};
}

QuickActionsState stateOf(HomeQuickActions& home)
{
    // The row as it is laid out, left to right, and every button of it shown.
    QStringList row;
    int lastX = -1;
    for (const QString& id : home.rowActionIds())
    {
        QAbstractButton* button = home.actionButton(id);
        EXPECT_NE(button, nullptr) << id.toStdString();
        if (button == nullptr)
        {
            continue;
        }
        EXPECT_TRUE(button->isVisible()) << id.toStdString();
        EXPECT_GT(button->geometry().x(), lastX) << id.toStdString() << " is not laid out in the row's order";
        lastX = button->geometry().x();
        row << DashboardPreferences::actionTitleForId(id);
    }
    return {row, actingEntries(home.moreMenu()), stored(SettingsKeys::INTERFACE_DASHBOARD_ACTION_ORDER),
            stored(SettingsKeys::INTERFACE_DASHBOARD_HIDDEN_ACTIONS)};
}

void showAndSettle(QWidget& widget, const QSize& size)
{
    widget.resize(size);
    widget.show();
    QCoreApplication::processEvents();
    QTest::qWait(20);
}

/// The steps a person takes in the customization, as the dashboard's More menu names them.
const QList<QStringList>& customizationSteps()
{
    static const QList<QStringList> steps = {
        {QStringLiteral("Open file"), QStringLiteral("Move later")},
        {QStringLiteral("System info"), QStringLiteral("Hide action")},
        {QStringLiteral("Image integration"), QStringLiteral("Move earlier")},
        {QStringLiteral("New sequence"), QStringLiteral("Hide action")},
        {QStringLiteral("Show hidden action"), QStringLiteral("System info")},
        {QStringLiteral("Extension Manager"), QStringLiteral("Move earlier")},
        {QStringLiteral("Reset quick actions")},
    };
    return steps;
}

// ---- Home's recent files

/// What a probe of a recent file gives, for the cases: a sequence's frame size and count, an image's size.
an::ui::HomeRecentFacts probedFacts(const QString& path)
{
    an::ui::HomeRecentFacts facts;
    if (path.endsWith(QStringLiteral(".ser")))
    {
        facts.size = QSize(448, 448);
        facts.frameCount = 5144u;
    }
    else
    {
        facts.size = QSize(4432, 3840);
    }
    return facts;
}

bool waitForTiles(HomeRecentGrid& grid, int count)
{
    return QTest::qWaitFor([&grid, count]() { return grid.tiles().size() == count; }, kWaitMs);
}

QStringList tilePaths(const HomeRecentGrid& grid)
{
    QStringList paths;
    for (const HomeRecentTile* tile : grid.tiles())
    {
        paths << tile->filePath();
    }
    return paths;
}

HomeRecentTile* tileFor(const HomeRecentGrid& grid, const QString& path)
{
    for (HomeRecentTile* tile : grid.tiles())
    {
        if (tile->filePath() == path)
        {
            return tile;
        }
    }
    return nullptr;
}

/// The dashboard's grid over a list, once it has ordered it.
QStringList dashboardGridOrder(an::ui::RecentFilesGrid& grid, const QString& search, int sortMode)
{
    QSignalSpy refreshed(&grid, &an::ui::RecentFilesGrid::stateChanged);
    grid.setSearchFilter(search);
    grid.setSortMode(sortMode);
    grid.reloadThumbnails();
    EXPECT_TRUE(refreshed.wait(kWaitMs));
    return grid.visibleFileOrder();
}

} // namespace

// ---- The quick actions

// For the same settings - none, an order with a hidden action, a hidden primary action - Home's row holds the actions
// the dashboard's toolbar shows, in its order, and its More menu the ones the dashboard's overflow holds.
TEST(HomeQuickActions, TheRowAndTheMoreMenuEqualTheDashboardsForTheSameSettings)
{
    struct Stored
    {
        QString order;
        QString hidden;
    };
    const QList<Stored> cases = {
        {QString(), QString()},
        {QStringLiteral("image_integration\nnew_sequence\nopen_file\nsystem_info\nclear_recents"),
         QStringLiteral("system_info")},
        {QStringLiteral("system_info\nextension_manager"), QStringLiteral("new_sequence\nclear_recents")},
    };
    for (const Stored& stated : cases)
    {
        SCOPED_TRACE(("order [" + stated.order + "] hidden [" + stated.hidden + "]").toStdString());
        DashboardApplication::instance()->resetForTest();
        storeTheCurrentFormat();
        store(SettingsKeys::INTERFACE_DASHBOARD_ACTION_ORDER, stated.order);
        store(SettingsKeys::INTERFACE_DASHBOARD_HIDDEN_ACTIONS, stated.hidden);

        MainDashboard dashboard;
        dashboard.initialize(DashboardApplication::instance());
        showAndSettle(dashboard, QSize(1280, 900));
        HomeQuickActions home;
        showAndSettle(home, QSize(1100, 60));

        const QuickActionsState dashboardShows = stateOf(dashboard);
        const QuickActionsState homeShows = stateOf(home);
        EXPECT_EQ(homeShows.row, dashboardShows.row) << described(homeShows) << " / " << described(dashboardShows);
        EXPECT_EQ(homeShows.more, dashboardShows.more) << described(homeShows) << " / " << described(dashboardShows);
        EXPECT_EQ(titlesOf(home.rowActionIds()), homeShows.row);
    }
}

// Moving, hiding, showing and resetting the quick actions, step by step, leave Home's row, its More menu and the
// settings as the same steps leave the dashboard's: through the More menu and through the row's right-click.
TEST(HomeQuickActions, MovingHidingAndResettingEqualTheDashboards)
{
    const auto customizeDashboard = []() {
        DashboardApplication::instance()->resetForTest();
        MainDashboard dashboard;
        dashboard.initialize(DashboardApplication::instance());
        showAndSettle(dashboard, QSize(1280, 900));
        QList<QuickActionsState> states{stateOf(dashboard)};
        for (const QStringList& step : customizationSteps())
        {
            QAction* action =
                entryAt(dashboardMoreMenu(dashboard), QStringList{QStringLiteral("Customize quick actions")} + step);
            EXPECT_NE(action, nullptr) << step.join(QStringLiteral(" > ")).toStdString();
            if (action != nullptr)
            {
                action->trigger();
                QCoreApplication::processEvents();
            }
            states << stateOf(dashboard);
        }
        return states;
    };
    const QList<QuickActionsState> dashboardStates = customizeDashboard();

    // Through Home's More menu.
    {
        DashboardApplication::instance()->resetForTest();
        HomeQuickActions home;
        showAndSettle(home, QSize(1100, 60));
        QList<QuickActionsState> states{stateOf(home)};
        for (const QStringList& step : customizationSteps())
        {
            QAction* action = entryAt(home.moreMenu(), QStringList{QStringLiteral("Customize quick actions")} + step);
            EXPECT_NE(action, nullptr) << step.join(QStringLiteral(" > ")).toStdString();
            if (action != nullptr)
            {
                action->trigger();
                QCoreApplication::processEvents();
            }
            states << stateOf(home);
        }
        ASSERT_EQ(states.size(), dashboardStates.size());
        for (qsizetype step = 0; step < states.size(); ++step)
        {
            EXPECT_EQ(states[step], dashboardStates[step])
                << "after step " << step << ": Home " << described(states[step]) << " / the dashboard "
                << described(dashboardStates[step]);
        }
    }

    // Through the row's right-click, whose menu is the customization itself.
    {
        DashboardApplication::instance()->resetForTest();
        HomeQuickActions home;
        showAndSettle(home, QSize(1100, 60));
        QList<QuickActionsState> states{stateOf(home)};
        for (const QStringList& step : customizationSteps())
        {
            QMenu* menu = rightClick(home, QStringLiteral("homeQuickActionsMenu"));
            ASSERT_NE(menu, nullptr) << "the row's right-click shows no menu";
            EXPECT_TRUE(choose(menu, step)) << step.join(QStringLiteral(" > ")).toStdString();
            states << stateOf(home);
        }
        for (qsizetype step = 0; step < states.size(); ++step)
        {
            EXPECT_EQ(states[step], dashboardStates[step])
                << "after step " << step << ": Home " << described(states[step]) << " / the dashboard "
                << described(dashboardStates[step]);
        }
    }
}

// Each quick action, from the row or from the More menu, asks for what the same action of the dashboard asks for, and
// leaves the dashboard's record of the last action as the dashboard leaves it.
TEST(HomeQuickActions, EachActionAsksForWhatTheDashboardsAsksFor)
{
    // A recent file, so that Clear recents is on in the dashboard as it is in Home's row.
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    recordRecents(folder, {recentFile(folder, QStringLiteral("recent.fit"))});
    MainDashboard dashboard;
    dashboard.initialize(DashboardApplication::instance());
    showAndSettle(dashboard, QSize(1280, 900));
    const QString clearTitle =
        DashboardPreferences::actionTitleForId(QString::fromUtf8(DashboardPreferences::ACTION_ID_CLEAR_RECENTS));
    ASSERT_TRUE(QTest::qWaitFor(
        [&dashboard, &clearTitle]() {
            const QAction* clear = entryAt(dashboardMoreMenu(dashboard), {clearTitle});
            return clear != nullptr && clear->isEnabled();
        },
        kWaitMs))
        << "the dashboard's Clear recents did not turn on with a recent file";
    HomeQuickActions home;
    showAndSettle(home, QSize(1100, 60));

    for (const QString& id : DashboardPreferences::dashboardActionIds())
    {
        SCOPED_TRACE(id.toStdString());
        QSignalSpy dashboardSpies[] = {QSignalSpy(&dashboard, &MainDashboard::openFileRequested),
                                       QSignalSpy(&dashboard, &MainDashboard::newSequenceRequested),
                                       QSignalSpy(&dashboard, &MainDashboard::imageIntegrationRequested),
                                       QSignalSpy(&dashboard, &MainDashboard::openExtensionManagerRequested),
                                       QSignalSpy(&dashboard, &MainDashboard::clearRecentFilesRequested),
                                       QSignalSpy(&dashboard, &MainDashboard::systemInformationRequested)};
        QSignalSpy homeSpies[] = {QSignalSpy(&home, &HomeQuickActions::openFileRequested),
                                  QSignalSpy(&home, &HomeQuickActions::newSequenceRequested),
                                  QSignalSpy(&home, &HomeQuickActions::imageIntegrationRequested),
                                  QSignalSpy(&home, &HomeQuickActions::openExtensionManagerRequested),
                                  QSignalSpy(&home, &HomeQuickActions::clearRecentFilesRequested),
                                  QSignalSpy(&home, &HomeQuickActions::systemInformationRequested)};

        // The dashboard's own way: its button, or its overflow's entry.
        const QString title = DashboardPreferences::actionTitleForId(id);
        if (DashboardPreferences::isPrimaryAction(id))
        {
            for (QPushButton* button : dashboard.findChildren<QPushButton*>(QStringLiteral("dashboardActionButton")))
            {
                if (button->property("dashboardActionId").toString() == id)
                {
                    button->click();
                }
            }
        }
        else
        {
            QAction* entry = entryAt(dashboardMoreMenu(dashboard), {title});
            ASSERT_NE(entry, nullptr);
            entry->trigger();
        }
        const QString dashboardRecorded = stored(SettingsKeys::INTERFACE_DASHBOARD_LAST_ACTION);
        store(SettingsKeys::INTERFACE_DASHBOARD_LAST_ACTION, QString());

        // Home's: its row's button, or its More menu's entry.
        if (DashboardPreferences::isPrimaryAction(id))
        {
            QAbstractButton* button = home.actionButton(id);
            ASSERT_NE(button, nullptr);
            QTest::mouseClick(button, Qt::LeftButton);
        }
        else
        {
            QAction* entry = entryAt(home.moreMenu(), {title});
            ASSERT_NE(entry, nullptr);
            entry->trigger();
        }
        QCoreApplication::processEvents();

        for (int signal = 0; signal < 6; ++signal)
        {
            EXPECT_EQ(homeSpies[signal].count(), dashboardSpies[signal].count()) << "signal " << signal;
        }
        EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_LAST_ACTION), dashboardRecorded);
    }
}

// A quick action turned off - Clear recents with no recent file - is off in the row or the menu that holds it.
TEST(HomeQuickActions, AnActionTurnedOffIsOffWhereverItIs)
{
    DashboardApplication::instance()->resetForTest();
    HomeQuickActions home;
    showAndSettle(home, QSize(1100, 60));
    const QString clear = QString::fromUtf8(DashboardPreferences::ACTION_ID_CLEAR_RECENTS);
    home.setActionEnabled(clear, false);
    QAction* entry = entryAt(home.moreMenu(), {DashboardPreferences::actionTitleForId(clear)});
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(entry->isEnabled());
    home.setActionEnabled(clear, true);
    entry = entryAt(home.moreMenu(), {DashboardPreferences::actionTitleForId(clear)});
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->isEnabled());

    const QString open = QString::fromUtf8(DashboardPreferences::ACTION_ID_OPEN_FILE);
    home.setActionEnabled(open, false);
    ASSERT_NE(home.actionButton(open), nullptr);
    EXPECT_FALSE(home.actionButton(open)->isEnabled());
}

// ---- The Continue row

// With images only, the featured image is the one the dashboard features - the most recent with a thumbnail - and its
// Continue asks for that file to be opened.
TEST(HomeContinueRow, TheFeaturedImageIsTheDashboardsAndContinueOpensIt)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QStringList files = {recentFile(folder, QStringLiteral("M42_Light_001.fit")),
                               recentFile(folder, QStringLiteral("Andromeda_001.xisf")),
                               recentFile(folder, QStringLiteral("M42_Blue_002.fit"))};
    recordRecents(folder, files);

    MainDashboard dashboard;
    dashboard.initialize(DashboardApplication::instance());
    showAndSettle(dashboard, QSize(1280, 900));
    auto* dashboardFeatured = dashboard.findChild<QWidget*>(QStringLiteral("dashboardFeaturedFileCard"));
    ASSERT_NE(dashboardFeatured, nullptr);
    ASSERT_TRUE(QTest::qWaitFor(
        [dashboardFeatured]() {
            return !dashboardFeatured->property("dashboardFeaturedFilePath").toString().isEmpty();
        },
        kWaitMs));

    HomeContinueRow row;
    row.setRecentFiles(asDeque(files));
    showAndSettle(row, QSize(1200, 240));
    ASSERT_TRUE(QTest::qWaitFor([&row]() { return !row.featuredFilePath().isEmpty(); }, kWaitMs));
    EXPECT_EQ(row.featuredFilePath(), dashboardFeatured->property("dashboardFeaturedFilePath").toString());
    EXPECT_EQ(row.featuredFilePath(), files.front());

    QSignalSpy opened(&row, &HomeContinueRow::openRecentFileRequested);
    auto* continueButton = row.findChild<QPushButton*>(QStringLiteral("dashboardFeaturedContinueButton"));
    ASSERT_NE(continueButton, nullptr);
    QTest::mouseClick(continueButton, Qt::LeftButton);
    ASSERT_EQ(opened.count(), 1);
    EXPECT_EQ(opened.front().front().toString(), files.front());
}

// The featured image's pin is the dashboard's: pinned on Home, the dashboard features it too and it stays featured when
// another file becomes the most recent; unpinned, the most recent image is featured again.
TEST(HomeContinueRow, APinnedImageStaysFeaturedAndItsPinIsTheDashboards)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    QStringList files = {recentFile(folder, QStringLiteral("NGC7000.fit")),
                         recentFile(folder, QStringLiteral("M31.xisf")), recentFile(folder, QStringLiteral("M45.png"))};
    recordRecents(folder, files);

    HomeContinueRow row;
    row.setRecentFiles(asDeque(files));
    showAndSettle(row, QSize(1200, 240));
    ASSERT_TRUE(QTest::qWaitFor([&row, &files]() { return row.featuredFilePath() == files.front(); }, kWaitMs));
    auto* pin = row.findChild<QPushButton*>(QStringLiteral("dashboardFeaturedPinButton"));
    ASSERT_NE(pin, nullptr);
    QTest::mouseClick(pin, Qt::LeftButton);
    QCoreApplication::processEvents();
    EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_PINNED_HERO_FILE), files.front());
    EXPECT_TRUE(row.isFeaturedPinned());

    // Another file is the most recent: the pinned one stays.
    files.move(2, 0);
    DashboardApplication::instance()->setRecentFiles(
        {files[0].toStdString(), files[1].toStdString(), files[2].toStdString()});
    row.setRecentFiles(asDeque(files));
    QTest::qWait(50);
    EXPECT_EQ(row.featuredFilePath(), files[1]);

    MainDashboard dashboard;
    dashboard.initialize(DashboardApplication::instance());
    showAndSettle(dashboard, QSize(1280, 900));
    auto* dashboardFeatured = dashboard.findChild<QWidget*>(QStringLiteral("dashboardFeaturedFileCard"));
    ASSERT_NE(dashboardFeatured, nullptr);
    EXPECT_TRUE(QTest::qWaitFor(
        [dashboardFeatured, &files]() {
            return dashboardFeatured->property("dashboardFeaturedFilePath").toString() == files[1];
        },
        kWaitMs))
        << "the dashboard does not feature the image Home pinned";

    // Unpinned on Home: the most recent image is featured again.
    QTest::mouseClick(pin, Qt::LeftButton);
    QCoreApplication::processEvents();
    EXPECT_TRUE(stored(SettingsKeys::INTERFACE_DASHBOARD_PINNED_HERO_FILE).isEmpty());
    EXPECT_TRUE(QTest::qWaitFor([&row, &files]() { return row.featuredFilePath() == files[0]; }, kWaitMs));
    EXPECT_FALSE(row.isFeaturedPinned());
}

// The row features an image, never a sequence: the most recent sequence has a card of its own beside it - its name,
// its frames and its size in the facts - whose Resume asks for it to be opened. With no sequence among the recent
// files there is no sequence card.
TEST(HomeContinueRow, TheLastSequenceIsShownBesideTheImageAndResumed)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QStringList files = {recentFile(folder, QStringLiteral("jupiter_2026.ser")),
                               recentFile(folder, QStringLiteral("M42.fit")),
                               recentFile(folder, QStringLiteral("saturn_old.ser"))};
    recordRecents(folder, files);

    HomeContinueRow row;
    row.setFactsProbe(&probedFacts);
    row.setRecentFiles(asDeque(files));
    showAndSettle(row, QSize(1200, 240));
    ASSERT_TRUE(QTest::qWaitFor([&row]() { return !row.featuredFilePath().isEmpty(); }, kWaitMs));
    EXPECT_EQ(row.featuredFilePath(), files[1]) << "a sequence was featured as the image";
    EXPECT_EQ(row.lastSequencePath(), files[0]);
    ASSERT_NE(row.sequenceCard(), nullptr);
    EXPECT_TRUE(row.sequenceCard()->isVisible());
    EXPECT_TRUE(
        QTest::qWaitFor([&row]() { return row.sequenceFactsText().contains(QStringLiteral("5144 frames")); }, kWaitMs))
        << row.sequenceFactsText().toStdString();
    EXPECT_TRUE(row.sequenceFactsText().contains(QStringLiteral("448x448"))) << row.sequenceFactsText().toStdString();
    EXPECT_TRUE(row.sequenceFactsText().contains(QStringLiteral("SER"))) << row.sequenceFactsText().toStdString();

    QSignalSpy opened(&row, &HomeContinueRow::openRecentFileRequested);
    ASSERT_NE(row.resumeButton(), nullptr);
    QTest::mouseClick(row.resumeButton(), Qt::LeftButton);
    ASSERT_EQ(opened.count(), 1);
    EXPECT_EQ(opened.front().front().toString(), files[0]);

    row.setRecentFiles(asDeque({files[1]}));
    QCoreApplication::processEvents();
    EXPECT_TRUE(row.lastSequencePath().isEmpty());
    EXPECT_FALSE(row.sequenceCard()->isVisible());
}

// A recent file is listed before its thumbnail is written: the sequence card reads the thumbnail once it is, when the
// recent files are given again unchanged.
TEST(HomeContinueRow, TheSequenceCardReadsItsThumbnailOnceItIsWritten)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QString sequence = recentFile(folder, QStringLiteral("late_thumbnail.ser"));
    DashboardApplication::instance()->setRecentFiles({sequence.toStdString()});

    HomeContinueRow row;
    row.setFactsProbe(&probedFacts);
    row.setRecentFiles(asDeque({sequence}));
    showAndSettle(row, QSize(1200, 240));
    ASSERT_TRUE(
        QTest::qWaitFor([&row]() { return row.sequenceFactsText().contains(QStringLiteral("frames")); }, kWaitMs));
    EXPECT_FALSE(row.hasSequencePicture());

    recordThumbnail(folder, sequence, QColor(90, 160, 90));
    row.setRecentFiles(asDeque({sequence}));
    EXPECT_TRUE(QTest::qWaitFor([&row]() { return row.hasSequencePicture(); }, kWaitMs))
        << "the card never read the thumbnail written after it was shown";
}

// The featured image fills the Continue row's height, as tall as the sequence card beside it.
TEST(HomeContinueRow, TheFeaturedImageFillsTheRow)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QStringList files = {recentFile(folder, QStringLiteral("tall.fit")),
                               recentFile(folder, QStringLiteral("seq.ser"))};
    recordRecents(folder, files);
    HomeContinueRow row;
    row.setFactsProbe(&probedFacts);
    row.setRecentFiles(asDeque(files));
    showAndSettle(row, QSize(1200, 240));
    ASSERT_TRUE(QTest::qWaitFor([&row]() { return !row.featuredFilePath().isEmpty(); }, kWaitMs));
    QCoreApplication::processEvents();
    EXPECT_EQ(row.featuredCard()->geometry().height(), row.sequenceCard()->geometry().height());
    EXPECT_EQ(row.featuredCard()->geometry().height(), row.height());
}

// ---- Recent images and sequences

// For the same list and the same pins, every search and sort leaves Home's recent files in the order the dashboard's
// grid gives them.
TEST(HomeRecentGrid, SearchAndSortEqualTheDashboardsGridForTheSameList)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    storeTheCurrentFormat();
    const QStringList files = {recentFile(folder, QStringLiteral("M42_Light_001.fit")),
                               recentFile(folder, QStringLiteral("andromeda.xisf")),
                               recentFile(folder, QStringLiteral("Saturn_capture.ser")),
                               recentFile(folder, QStringLiteral("M42_Blue_002.fit")),
                               recentFile(folder, QStringLiteral("bubble.png")),
                               recentFile(folder, QStringLiteral("Crescent.tif"))};
    recordRecents(folder, files);
    const QSet<QString> pins = {files[4]};
    store(SettingsKeys::INTERFACE_DASHBOARD_PINNED_RECENTS, files[4]);

    an::ui::RecentFilesGrid dashboardGrid;
    dashboardGrid.setPinnedFilePaths(pins);
    dashboardGrid.setRecentFiles(asDeque(files));

    HomeRecentGrid home;
    home.setRecentFiles(asDeque(files));
    showAndSettle(home, QSize(1200, 700));

    const QStringList searches = {QString(), QStringLiteral("m42"), QStringLiteral("FIT"), QStringLiteral(".x"),
                                  QStringLiteral("nothing-matches")};
    const QList<int> sorts = {DashboardPreferences::RECENT_SORT_RECENT, DashboardPreferences::RECENT_SORT_NAME,
                              DashboardPreferences::RECENT_SORT_TYPE};
    for (const QString& search : searches)
    {
        for (const int sort : sorts)
        {
            SCOPED_TRACE(("search '" + search + "' sort " + QString::number(sort)).toStdString());
            const QStringList expected = dashboardGridOrder(dashboardGrid, search, sort);
            home.searchField()->setText(search);
            home.sortBox()->setCurrentIndex(home.sortBox()->findData(sort));
            EXPECT_TRUE(QTest::qWaitFor([&home, &expected]() { return home.visibleFileOrder() == expected; }, kWaitMs))
                << "Home: " << home.visibleFileOrder().join(QStringLiteral(", ")).toStdString()
                << "\nthe dashboard: " << expected.join(QStringLiteral(", ")).toStdString();
            EXPECT_TRUE(QTest::qWaitFor([&home, &expected]() { return tilePaths(home) == expected; }, kWaitMs))
                << "the tiles are not the order";
        }
    }
    // The search and the sort are the dashboard's settings.
    EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SEARCH_TEXT), searches.back());
    EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SORT_MODE), QStringLiteral("type"));
}

// The grid and the list show the same tiles in the same order, each with the same name, type and facts: the list one
// under another, the grid several to a row.
TEST(HomeRecentGrid, TheGridAndTheListShowTheSameItems)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QStringList files = {
        recentFile(folder, QStringLiteral("one.fit")), recentFile(folder, QStringLiteral("two.xisf")),
        recentFile(folder, QStringLiteral("three.ser")), recentFile(folder, QStringLiteral("four.png")),
        recentFile(folder, QStringLiteral("five.tiff"))};
    recordRecents(folder, files);

    HomeRecentGrid home;
    home.setFactsProbe(&probedFacts);
    home.setRecentFiles(asDeque(files));
    showAndSettle(home, QSize(1300, 900));
    ASSERT_TRUE(waitForTiles(home, 5));
    ASSERT_EQ(home.arrangement(), HomeRecentGrid::Arrangement::Grid);
    ASSERT_TRUE(QTest::qWaitFor(
        [&home]() {
            const QList<HomeRecentTile*> tiles = home.tiles();
            return std::all_of(tiles.cbegin(), tiles.cend(), [](const HomeRecentTile* tile) {
                return tile->hasPicture() && !tile->factsText().isEmpty();
            });
        },
        kWaitMs))
        << "the tiles' pictures and facts did not arrive";

    struct Shown
    {
        QString path;
        QString name;
        QString type;
        QString facts;
        bool operator==(const Shown&) const = default;
    };
    const auto shown = [&home]() {
        QList<Shown> items;
        for (const HomeRecentTile* tile : home.tiles())
        {
            items << Shown{tile->filePath(), tile->nameText(), tile->typeText(), tile->factsText()};
        }
        return items;
    };
    const QList<Shown> inGrid = shown();
    const QList<HomeRecentTile*> gridTiles = home.tiles();
    EXPECT_EQ(gridTiles[0]->geometry().y(), gridTiles[1]->geometry().y()) << "the grid puts one tile to a row";

    ASSERT_NE(home.listButton(), nullptr);
    QTest::mouseClick(home.listButton(), Qt::LeftButton);
    QCoreApplication::processEvents();
    EXPECT_EQ(home.arrangement(), HomeRecentGrid::Arrangement::List);
    const QList<Shown> inList = shown();
    EXPECT_EQ(inList.size(), inGrid.size());
    for (qsizetype item = 0; item < (std::min)(inList.size(), inGrid.size()); ++item)
    {
        EXPECT_EQ(inList[item].path, inGrid[item].path);
        EXPECT_EQ(inList[item].name, inGrid[item].name);
        EXPECT_EQ(inList[item].type, inGrid[item].type);
        EXPECT_EQ(inList[item].facts, inGrid[item].facts);
    }
    QTest::qWait(20);
    const QList<HomeRecentTile*> listTiles = home.tiles();
    for (qsizetype item = 1; item < listTiles.size(); ++item)
    {
        EXPECT_EQ(listTiles[item]->geometry().x(), listTiles[0]->geometry().x());
        EXPECT_GT(listTiles[item]->geometry().y(), listTiles[item - 1]->geometry().y());
    }

    QTest::mouseClick(home.gridButton(), Qt::LeftButton);
    QCoreApplication::processEvents();
    EXPECT_EQ(home.arrangement(), HomeRecentGrid::Arrangement::Grid);
    EXPECT_EQ(shown().size(), inGrid.size());
}

// The grid or the list is remembered, in the dashboard's settings and their format: chosen in one Home, it is how the
// next shows the recent files; a stored word is read in its canonical form, one that names neither is the grid, and a
// choice of another format is set aside with the dashboard's other settings.
TEST(HomeRecentGrid, TheArrangementIsRemembered)
{
    DashboardApplication::instance()->resetForTest();
    const QString key = QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_RECENT_ARRANGEMENT);
    {
        HomeRecentGrid home;
        showAndSettle(home, QSize(1300, 700));
        EXPECT_EQ(home.arrangement(), HomeRecentGrid::Arrangement::Grid);
        QTest::mouseClick(home.listButton(), Qt::LeftButton);
        EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_RECENT_ARRANGEMENT), QStringLiteral("list"));
    }
    {
        HomeRecentGrid home;
        showAndSettle(home, QSize(1300, 700));
        EXPECT_EQ(home.arrangement(), HomeRecentGrid::Arrangement::List);
        EXPECT_TRUE(home.listButton()->isChecked());
        EXPECT_FALSE(home.gridButton()->isChecked());
        QTest::mouseClick(home.gridButton(), Qt::LeftButton);
        EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_RECENT_ARRANGEMENT), QStringLiteral("grid"));
    }

    store(SettingsKeys::INTERFACE_DASHBOARD_RECENT_ARRANGEMENT, QStringLiteral(" List "));
    {
        HomeRecentGrid home;
        EXPECT_EQ(home.arrangement(), HomeRecentGrid::Arrangement::List);
        EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_RECENT_ARRANGEMENT), QStringLiteral("list"));
    }
    store(SettingsKeys::INTERFACE_DASHBOARD_RECENT_ARRANGEMENT, QStringLiteral("tiles"));
    {
        HomeRecentGrid home;
        EXPECT_EQ(home.arrangement(), HomeRecentGrid::Arrangement::Grid);
        EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_RECENT_ARRANGEMENT), QStringLiteral("grid"));
    }

    DashboardApplication::instance()->resetForTest();
    store(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION, QStringLiteral("3"));
    store(SettingsKeys::INTERFACE_DASHBOARD_RECENT_ARRANGEMENT, QStringLiteral("list"));
    {
        HomeRecentGrid home;
        EXPECT_EQ(home.arrangement(), HomeRecentGrid::Arrangement::Grid);
        EXPECT_EQ(settings().select(key + QStringLiteral(".format3")), QStringLiteral("list"));
    }
}

// A sequence's tile is marked as one - a film glyph in its tag - and says how many frames it holds; an image's says its
// size. The count above the tiles names the images and the sequences.
TEST(HomeRecentGrid, ASequencesTileShowsItsFilmGlyphAndFrameCount)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QStringList files = {recentFile(folder, QStringLiteral("02_52_14_pipp.ser")),
                               recentFile(folder, QStringLiteral("M20_FINAL.png")),
                               recentFile(folder, QStringLiteral("Carina.fit"))};
    recordRecents(folder, files);

    HomeRecentGrid home;
    home.setFactsProbe(&probedFacts);
    home.setRecentFiles(asDeque(files));
    showAndSettle(home, QSize(1300, 800));
    ASSERT_TRUE(waitForTiles(home, 3));

    HomeRecentTile* sequence = tileFor(home, files[0]);
    HomeRecentTile* image = tileFor(home, files[1]);
    ASSERT_NE(sequence, nullptr);
    ASSERT_NE(image, nullptr);
    EXPECT_TRUE(sequence->isSequence());
    EXPECT_FALSE(image->isSequence());
    EXPECT_EQ(sequence->typeText(), QStringLiteral("SER"));
    EXPECT_EQ(image->typeText(), QStringLiteral("PNG"));
    EXPECT_EQ(tileFor(home, files[2])->typeText(), QStringLiteral("FITS"));
    EXPECT_TRUE(QTest::qWaitFor([sequence]() { return sequence->factsText().contains(QStringLiteral("5144 frames")); },
                                kWaitMs))
        << sequence->factsText().toStdString();
    EXPECT_TRUE(sequence->factsText().startsWith(QStringLiteral("448x448"))) << sequence->factsText().toStdString();
    EXPECT_TRUE(
        QTest::qWaitFor([image]() { return image->factsText().startsWith(QStringLiteral("4432x3840")); }, kWaitMs))
        << image->factsText().toStdString();
    EXPECT_FALSE(image->factsText().contains(QStringLiteral("frames")));
    EXPECT_TRUE(sequence->accessibleName().contains(QStringLiteral("sequence"), Qt::CaseInsensitive))
        << sequence->accessibleName().toStdString();

    EXPECT_EQ(home.countText(), QStringLiteral("2 images · 1 sequence"));
}

// The tile's menu is the dashboard thumbnail's: Open, Pin to top, Show in File Browser, Copy path and Remove from
// recent files, and each does what it says. Pinned, the tile is first, the pin is the dashboard's setting and the entry
// unpins it.
TEST(HomeRecentGrid, TheTilesMenuHasItsFiveEntriesAndEachActs)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QStringList files = {recentFile(folder, QStringLiteral("first.fit")),
                               recentFile(folder, QStringLiteral("second.fit")),
                               recentFile(folder, QStringLiteral("third.xisf"))};
    recordRecents(folder, files);

    HomeRecentGrid home;
    QStringList revealed;
    home.setFileReveal([&revealed](const QString& path) {
        revealed << path;
        return true;
    });
    home.setRecentFiles(asDeque(files));
    showAndSettle(home, QSize(1300, 800));
    ASSERT_TRUE(waitForTiles(home, 3));
    const QString target = files[2];
    const auto menuOf = [&home, &target]() -> QMenu* {
        HomeRecentTile* tile = tileFor(home, target);
        return tile != nullptr ? rightClick(*tile, QStringLiteral("homeRecentTileMenu")) : nullptr;
    };

    QMenu* menu = menuOf();
    ASSERT_NE(menu, nullptr) << "a tile's right-click shows no menu";
    EXPECT_EQ(actingEntries(menu),
              (QStringList{QStringLiteral("Open"), QStringLiteral("Pin to top"), QStringLiteral("Show in File Browser"),
                           QStringLiteral("Copy path"), QStringLiteral("Remove from recent files")}));
    QSignalSpy opened(&home, &HomeRecentGrid::openFileRequested);
    ASSERT_TRUE(choose(menu, {QStringLiteral("Open")}));
    ASSERT_EQ(opened.count(), 1);
    EXPECT_EQ(opened.front().front().toString(), target);

    QSignalSpy pinned(&home, &HomeRecentGrid::pinToggled);
    ASSERT_TRUE(choose(menuOf(), {QStringLiteral("Pin to top")}));
    ASSERT_EQ(pinned.count(), 1);
    EXPECT_TRUE(QTest::qWaitFor([&home, &target]() { return home.visibleFileOrder().value(0) == target; }, kWaitMs));
    EXPECT_TRUE(stored(SettingsKeys::INTERFACE_DASHBOARD_PINNED_RECENTS).contains(target));
    ASSERT_TRUE(QTest::qWaitFor([&home, &target]() { return tilePaths(home).value(0) == target; }, kWaitMs));
    EXPECT_TRUE(tileFor(home, target)->isPinned());
    ASSERT_TRUE(choose(menuOf(), {QStringLiteral("Unpin from top")}));
    EXPECT_TRUE(QTest::qWaitFor([&home, &files]() { return home.visibleFileOrder() == files; }, kWaitMs));
    EXPECT_FALSE(stored(SettingsKeys::INTERFACE_DASHBOARD_PINNED_RECENTS).contains(target));

    ASSERT_TRUE(choose(menuOf(), {QStringLiteral("Show in File Browser")}));
    EXPECT_EQ(revealed, QStringList{target});

    QApplication::clipboard()->clear();
    ASSERT_TRUE(choose(menuOf(), {QStringLiteral("Copy path")}));
    EXPECT_EQ(QApplication::clipboard()->text(), target);

    QSignalSpy removed(&home, &HomeRecentGrid::removeFileRequested);
    ASSERT_TRUE(choose(menuOf(), {QStringLiteral("Remove from recent files")}));
    ASSERT_EQ(removed.count(), 1);
    EXPECT_EQ(removed.front().front().toString(), target);
}

// A click on a tile asks for its file to be opened, as a click on the dashboard's thumbnail does.
TEST(HomeRecentGrid, AClickOnATileOpensItsFile)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QStringList files = {recentFile(folder, QStringLiteral("a.fit")),
                               recentFile(folder, QStringLiteral("b.fit"))};
    recordRecents(folder, files);
    HomeRecentGrid home;
    home.setRecentFiles(asDeque(files));
    showAndSettle(home, QSize(1300, 800));
    ASSERT_TRUE(waitForTiles(home, 2));
    QSignalSpy opened(&home, &HomeRecentGrid::openFileRequested);
    QTest::mouseClick(tileFor(home, files[1]), Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
    ASSERT_EQ(opened.count(), 1);
    EXPECT_EQ(opened.front().front().toString(), files[1]);
}

// ---- The welcome

// With no recent file Home welcomes: the logo and the version, the three cards to start with, the drop zone with Open
// file, and the first-run panel's six things AfterNight does; nothing offers a project yet.
TEST(HomeWelcome, ShowsTheLogoTheVersionTheCardsTheDropZoneAndTheFeatures)
{
    HomeWelcome welcome;
    showAndSettle(welcome, QSize(1300, 900));

    auto* logo = welcome.findChild<QWidget*>(QStringLiteral("homeWelcomeLogo"));
    ASSERT_NE(logo, nullptr);
    EXPECT_TRUE(logo->isVisible());
    auto* version = welcome.findChild<an::ui::HomeText*>(QStringLiteral("homeWelcomeVersion"));
    ASSERT_NE(version, nullptr);
    const QString product = an::ui::productVersion();
    EXPECT_TRUE(version->text().contains(product.isEmpty() ? QStringLiteral("dev") : product))
        << version->text().toStdString();

    QStringList cards;
    for (QAbstractButton* card : welcome.findChildren<QAbstractButton*>(QStringLiteral("homeWelcomeCard")))
    {
        cards << card->accessibleName();
    }
    EXPECT_EQ(cards, (QStringList{QStringLiteral("Open an image"), QStringLiteral("Stack your frames"),
                                  QStringLiteral("Read the guide")}));

    auto* zone = welcome.findChild<QWidget*>(QStringLiteral("homeWelcomeDropZone"));
    ASSERT_NE(zone, nullptr);
    EXPECT_TRUE(zone->isVisible());
    EXPECT_EQ(zone->accessibleName(), QStringLiteral("Drop images or sequences here"));
    EXPECT_NE(welcome.findChild<QPushButton*>(QStringLiteral("homeWelcomeOpenFile")), nullptr);

    QStringList features;
    for (QWidget* feature : welcome.findChildren<QWidget*>(QStringLiteral("homeWelcomeFeature")))
    {
        features << feature->accessibleName();
    }
    QStringList firstRun;
    for (const an::ui::DashboardFeature& feature : an::ui::dashboardFeatures())
    {
        firstRun << QString::fromUtf8(feature.title);
    }
    EXPECT_EQ(features, firstRun);

    for (an::ui::HomeText* text : welcome.findChildren<an::ui::HomeText*>())
    {
        EXPECT_FALSE(text->text().contains(QStringLiteral("project"), Qt::CaseInsensitive))
            << text->text().toStdString();
    }
}

// Each card asks for what it names, and the drop zone's Open file asks for a file to open.
TEST(HomeWelcome, ItsCardsAndItsDropZoneAskForWhatTheyName)
{
    HomeWelcome welcome;
    showAndSettle(welcome, QSize(1300, 900));
    QSignalSpy open(&welcome, &HomeWelcome::openFileRequested);
    QSignalSpy stack(&welcome, &HomeWelcome::imageIntegrationRequested);
    QSignalSpy guide(&welcome, &HomeWelcome::openHelpRequested);
    const QList<QAbstractButton*> cards = welcome.findChildren<QAbstractButton*>(QStringLiteral("homeWelcomeCard"));
    ASSERT_EQ(cards.size(), 3);
    QTest::mouseClick(cards[0], Qt::LeftButton);
    QTest::mouseClick(cards[1], Qt::LeftButton);
    QTest::mouseClick(cards[2], Qt::LeftButton);
    EXPECT_EQ(open.count(), 1);
    EXPECT_EQ(stack.count(), 1);
    EXPECT_EQ(guide.count(), 1);
    QTest::mouseClick(welcome.findChild<QPushButton*>(QStringLiteral("homeWelcomeOpenFile")), Qt::LeftButton);
    EXPECT_EQ(open.count(), 2);
}

// A file dragged over the welcome lights its drop zone, which goes dark when the drag leaves; dropped, the file is
// handed to the application to open, as a file dropped on the dashboard is.
TEST(HomeWelcome, AFileDroppedOnItIsOpenedAndTheZoneLightsWhileDragged)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QString file = recentFile(folder, QStringLiteral("dropped.fit"));
    HomeWelcome welcome;
    showAndSettle(welcome, QSize(1300, 900));
    auto* zone = welcome.findChild<QWidget*>(QStringLiteral("homeWelcomeDropZone"));
    ASSERT_NE(zone, nullptr);

    QMimeData data;
    data.setUrls({QUrl::fromLocalFile(file)});
    const QPoint inside = zone->mapTo(&welcome, zone->rect().center());
    QDragEnterEvent enter(inside, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    enter.ignore();
    QCoreApplication::sendEvent(&welcome, &enter);
    EXPECT_TRUE(enter.isAccepted());
    EXPECT_TRUE(welcome.isDropHighlighted());
    QDragLeaveEvent leave;
    QCoreApplication::sendEvent(&welcome, &leave);
    EXPECT_FALSE(welcome.isDropHighlighted());

    QCoreApplication::sendEvent(&welcome, &enter);
    QDropEvent drop(QPointF(inside), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    drop.ignore();
    QCoreApplication::sendEvent(&welcome, &drop);
    EXPECT_TRUE(drop.isAccepted());
    EXPECT_FALSE(welcome.isDropHighlighted());
    EXPECT_EQ(DashboardApplication::instance()->droppedFiles(), QStringList{file});

    // A drag that carries no file is not taken.
    QMimeData text;
    text.setText(QStringLiteral("not a file"));
    QDragEnterEvent textEnter(inside, Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier);
    textEnter.ignore();
    QCoreApplication::sendEvent(&welcome, &textEnter);
    EXPECT_FALSE(textEnter.isAccepted());
    EXPECT_FALSE(welcome.isDropHighlighted());
}
