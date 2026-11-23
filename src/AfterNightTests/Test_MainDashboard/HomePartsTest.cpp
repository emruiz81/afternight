// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Home's parts: the quick actions in the order and with the More menu their settings give, moved, hidden, shown and
/// reset step by step, each asking for what it names and recorded by its words, their stored order and hidden actions
/// read and kept in their canonical form, and their tooltips; the featured image, its title and facts, its pin, its
/// fade and the image passed over when its picture cannot be read; the last sequence; the recent files searched and
/// sorted, the search, the sort and the arrangement remembered in their settings and in the settings' format, shown as
/// a grid or a list, a sequence's tile with its film glyph and frame count, and the tile's menu; and the welcome with
/// no recent file, its cards and its drop zone. The parts read the application double's settings and thumbnails
/// through the context the suite installs.

#include "Test_MainDashboard/DashboardApplication.h"

#include "controls/DashboardPreferences.h"
#include "controls/DashboardUtils.h"
#include "controls/EngineVersionReport.h"
#include "controls/HomeContinueRow.h"
#include "controls/HomeParts.h"
#include "controls/HomeQuickActions.h"
#include "controls/HomeRecentGrid.h"
#include "controls/HomeWelcome.h"
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
#include <QGraphicsOpacityEffect>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <iterator>
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

/// Records a thumbnail for each recent file, each in a shade of its own.
void recordThumbnails(const QTemporaryDir& folder, const QStringList& files)
{
    int shade = 60;
    for (const QString& file : files)
    {
        recordThumbnail(folder, file, QColor(shade, 120, 200 - shade / 2));
        shade += 25;
    }
}

/// Three recent images with their thumbnails, most recent first: M42_Light_001.fit, Andromeda_001.xisf and
/// M42_Blue_002.fit.
QStringList threeRecentImages(const QTemporaryDir& folder)
{
    const QStringList files = {recentFile(folder, QStringLiteral("M42_Light_001.fit")),
                               recentFile(folder, QStringLiteral("Andromeda_001.xisf")),
                               recentFile(folder, QStringLiteral("M42_Blue_002.fit"))};
    recordThumbnails(folder, files);
    return files;
}

std::deque<QString> asDeque(const QStringList& files)
{
    return std::deque<QString>(files.cbegin(), files.cend());
}

/// The quick actions' titles, as the row and the More menu show them.
struct Titles
{
    QString open = QStringLiteral("Open file");
    QString sequence = QStringLiteral("New sequence");
    QString integration = QStringLiteral("Image integration");
    QString extensions = QStringLiteral("Extension Manager");
    QString clear = QStringLiteral("Clear recents");
    QString info = QStringLiteral("System info");
};

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

/// The steps a person takes in the customization, as the More menu names them.
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

/// What the row, the More menu and the settings hold before the first step of the customization and after each.
QList<QuickActionsState> customizationStates()
{
    const Titles t;
    const QString canonical =
        QStringLiteral("open_file\nnew_sequence\nimage_integration\nextension_manager\nclear_recents\nsystem_info");
    const QString openLater =
        QStringLiteral("new_sequence\nopen_file\nimage_integration\nextension_manager\nclear_recents\nsystem_info");
    const QString integrationEarlier =
        QStringLiteral("new_sequence\nimage_integration\nopen_file\nextension_manager\nclear_recents\nsystem_info");
    const QString extensionsEarlier =
        QStringLiteral("new_sequence\nimage_integration\nextension_manager\nopen_file\nclear_recents\nsystem_info");
    return {
        {{t.open, t.sequence, t.integration}, {t.extensions, t.clear, t.info}, canonical, QString()},
        {{t.sequence, t.open, t.integration}, {t.extensions, t.clear, t.info}, openLater, QString()},
        {{t.sequence, t.open, t.integration}, {t.extensions, t.clear}, openLater, QStringLiteral("system_info")},
        {{t.sequence, t.integration, t.open},
         {t.extensions, t.clear},
         integrationEarlier,
         QStringLiteral("system_info")},
        {{t.integration, t.open},
         {t.extensions, t.clear},
         integrationEarlier,
         QStringLiteral("new_sequence\nsystem_info")},
        {{t.integration, t.open}, {t.extensions, t.clear, t.info}, integrationEarlier, QStringLiteral("new_sequence")},
        {{t.integration, t.open}, {t.extensions, t.clear, t.info}, extensionsEarlier, QStringLiteral("new_sequence")},
        {{t.open, t.sequence, t.integration}, {t.extensions, t.clear, t.info}, canonical, QString()},
    };
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

} // namespace

// ---- The quick actions

// For each of three settings - none, an order with a hidden action, a hidden primary action - the row holds the shown
// primary actions in the stored order and the More menu the other shown actions, in that order.
TEST(HomeQuickActions, TheRowAndTheMoreMenuFollowTheStoredOrderAndHiddenActions)
{
    const Titles t;
    struct Stored
    {
        QString order;
        QString hidden;
        QStringList row;
        QStringList more;
    };
    const QList<Stored> cases = {
        {QString(), QString(), {t.open, t.sequence, t.integration}, {t.extensions, t.clear, t.info}},
        {QStringLiteral("image_integration\nnew_sequence\nopen_file\nsystem_info\nclear_recents"),
         QStringLiteral("system_info"),
         {t.integration, t.sequence, t.open},
         {t.clear, t.extensions}},
        {QStringLiteral("system_info\nextension_manager"),
         QStringLiteral("new_sequence\nclear_recents"),
         {t.open, t.integration},
         {t.info, t.extensions}},
    };
    for (const Stored& stated : cases)
    {
        SCOPED_TRACE(("order [" + stated.order + "] hidden [" + stated.hidden + "]").toStdString());
        DashboardApplication::instance()->resetForTest();
        storeTheCurrentFormat();
        store(SettingsKeys::INTERFACE_DASHBOARD_ACTION_ORDER, stated.order);
        store(SettingsKeys::INTERFACE_DASHBOARD_HIDDEN_ACTIONS, stated.hidden);

        HomeQuickActions home;
        showAndSettle(home, QSize(1100, 60));

        const QuickActionsState shown = stateOf(home);
        EXPECT_EQ(shown.row, stated.row) << described(shown);
        EXPECT_EQ(shown.more, stated.more) << described(shown);
        EXPECT_EQ(titlesOf(home.rowActionIds()), shown.row);
    }
}

// Moving, hiding, showing and resetting the quick actions, step by step, leave the row, the More menu and the settings
// as each step says: through the More menu and through the row's right-click alike.
TEST(HomeQuickActions, MovingHidingAndResettingChangeTheRowTheMenuAndTheSettingsStepByStep)
{
    const QList<QuickActionsState> expected = customizationStates();
    ASSERT_EQ(expected.size(), customizationSteps().size() + 1);

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
        ASSERT_EQ(states.size(), expected.size());
        for (qsizetype step = 0; step < states.size(); ++step)
        {
            EXPECT_EQ(states[step], expected[step]) << "after step " << step << ": shown " << described(states[step])
                                                    << " / expected " << described(expected[step]);
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
        ASSERT_EQ(states.size(), expected.size());
        for (qsizetype step = 0; step < states.size(); ++step)
        {
            EXPECT_EQ(states[step], expected[step]) << "after step " << step << ": shown " << described(states[step])
                                                    << " / expected " << described(expected[step]);
        }
    }
}

// Each quick action, from the row or from the More menu, asks for what it names and nothing else, and records itself
// as the last action by its words.
TEST(HomeQuickActions, EachActionAsksForWhatItNamesAndIsRecordedByItsWords)
{
    struct Asked
    {
        const char* id;
        int signal;
        const char* recorded;
    };
    // The signals in the order of the spies below.
    const Asked asked[] = {
        {DashboardPreferences::ACTION_ID_OPEN_FILE, 0, "Open file"},
        {DashboardPreferences::ACTION_ID_NEW_SEQUENCE, 1, "New sequence"},
        {DashboardPreferences::ACTION_ID_IMAGE_INTEGRATION, 2, "Image integration"},
        {DashboardPreferences::ACTION_ID_EXTENSION_MANAGER, 3, "Open Extension Manager"},
        {DashboardPreferences::ACTION_ID_CLEAR_RECENTS, 4, "Clear recent files"},
        {DashboardPreferences::ACTION_ID_SYSTEM_INFO, 5, "Open system info"},
    };
    ASSERT_EQ(static_cast<qsizetype>(std::size(asked)), DashboardPreferences::dashboardActionIds().size());

    DashboardApplication::instance()->resetForTest();
    HomeQuickActions home;
    showAndSettle(home, QSize(1100, 60));

    for (const Asked& action : asked)
    {
        const QString id = QString::fromUtf8(action.id);
        SCOPED_TRACE(id.toStdString());
        ASSERT_TRUE(DashboardPreferences::dashboardActionIds().contains(id));
        QSignalSpy homeSpies[] = {QSignalSpy(&home, &HomeQuickActions::openFileRequested),
                                  QSignalSpy(&home, &HomeQuickActions::newSequenceRequested),
                                  QSignalSpy(&home, &HomeQuickActions::imageIntegrationRequested),
                                  QSignalSpy(&home, &HomeQuickActions::openExtensionManagerRequested),
                                  QSignalSpy(&home, &HomeQuickActions::clearRecentFilesRequested),
                                  QSignalSpy(&home, &HomeQuickActions::systemInformationRequested)};
        store(SettingsKeys::INTERFACE_DASHBOARD_LAST_ACTION, QString());

        // Its row's button, or its More menu's entry.
        if (DashboardPreferences::isPrimaryAction(id))
        {
            QAbstractButton* button = home.actionButton(id);
            ASSERT_NE(button, nullptr);
            QTest::mouseClick(button, Qt::LeftButton);
        }
        else
        {
            QAction* entry = entryAt(home.moreMenu(), {DashboardPreferences::actionTitleForId(id)});
            ASSERT_NE(entry, nullptr);
            entry->trigger();
        }
        QCoreApplication::processEvents();

        for (int signal = 0; signal < 6; ++signal)
        {
            EXPECT_EQ(homeSpies[signal].count(), signal == action.signal ? 1 : 0) << "signal " << signal;
        }
        EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_LAST_ACTION), QString::fromUtf8(action.recorded));
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

// A stored order and hidden set name each action once; one that names an action twice, one there is not, or stray
// separators, is read in its canonical form and stored so, and the row and the More menu follow it.
TEST(HomeQuickActions, TheStoredOrderAndHiddenActionsAreReadAndKeptInTheirCanonicalForm)
{
    DashboardApplication::instance()->resetForTest();
    store(SettingsKeys::INTERFACE_DASHBOARD_ACTION_ORDER,
          QStringLiteral(" system_info ; open_file,invalid,new_sequence\nsystem_info "));
    store(SettingsKeys::INTERFACE_DASHBOARD_HIDDEN_ACTIONS,
          QStringLiteral("clear_recents,invalid;system_info,clear_recents"));
    storeTheCurrentFormat();

    HomeQuickActions home;
    showAndSettle(home, QSize(1100, 60));

    EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION), QStringLiteral("4"));
    const QString canonicalOrder =
        QStringLiteral("system_info\nopen_file\nnew_sequence\nimage_integration\nextension_manager\nclear_recents");
    EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_ACTION_ORDER), canonicalOrder);
    EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_HIDDEN_ACTIONS), QStringLiteral("clear_recents\nsystem_info"));
    const Titles t;
    const QuickActionsState shown = stateOf(home);
    EXPECT_EQ(shown.row, (QStringList{t.open, t.sequence, t.integration})) << described(shown);
    EXPECT_EQ(shown.more, QStringList{t.extensions}) << described(shown);
}

// The More menu's Extension Manager says what the manager does - install extensions and repair their environments -
// and asks for the manager.
TEST(HomeQuickActions, TheExtensionManagersEntrySaysWhatItDoesAndAsksForIt)
{
    DashboardApplication::instance()->resetForTest();
    HomeQuickActions home;
    showAndSettle(home, QSize(1100, 60));

    QAction* entry = entryAt(home.moreMenu(), {QStringLiteral("Extension Manager")});
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->toolTip().contains(QStringLiteral("install"), Qt::CaseInsensitive))
        << entry->toolTip().toStdString();
    EXPECT_TRUE(entry->toolTip().contains(QStringLiteral("repair environments"), Qt::CaseInsensitive))
        << entry->toolTip().toStdString();

    QSignalSpy asked(&home, &HomeQuickActions::openExtensionManagerRequested);
    entry->trigger();
    EXPECT_EQ(asked.count(), 1);
}

// Open file's button names its shortcut, Ctrl+O, in its tooltip.
TEST(HomeQuickActions, OpenFilesTooltipNamesItsShortcut)
{
    DashboardApplication::instance()->resetForTest();
    HomeQuickActions home;
    showAndSettle(home, QSize(1100, 60));

    QAbstractButton* open = home.actionButton(QString::fromUtf8(DashboardPreferences::ACTION_ID_OPEN_FILE));
    ASSERT_NE(open, nullptr);
    EXPECT_TRUE(open->toolTip().contains(QStringLiteral("Ctrl+O"))) << open->toolTip().toStdString();
}

// An action a shell adds to the row stands after the quick actions the settings arrange and before More, asks by its
// id, and is none of them: the customization does not list it, the settings do not hold it, and moving, hiding and
// resetting the arranged actions leave it where it is.
TEST(HomeQuickActions, AnActionAddedToTheRowIsAfterTheArrangedActionsAndIsNotCustomized)
{
    DashboardApplication::instance()->resetForTest();
    HomeQuickActions home;
    const QStringList arrangedRow = home.rowActionIds();
    const QStringList arrangedMore = home.moreActionIds();
    home.addRowAction(QStringLiteral("new_project"), QStringLiteral("New project"), QStringLiteral("folder_closed"),
                      QStringLiteral("Make a project"));
    showAndSettle(home, QSize(1100, 60));

    EXPECT_EQ(home.addedRowActionIds(), QStringList{QStringLiteral("new_project")});
    EXPECT_EQ(home.rowActionIds(), arrangedRow) << "the arranged row changed";
    EXPECT_EQ(home.moreActionIds(), arrangedMore);
    QAbstractButton* const added = home.actionButton(QStringLiteral("new_project"));
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(added->text(), QStringLiteral("New project"));
    EXPECT_EQ(added->toolTip(), QStringLiteral("Make a project"));
    EXPECT_EQ(added->property("anButtonVariant").toString(), QStringLiteral("tonal"));
    for (const QString& id : arrangedRow)
    {
        ASSERT_NE(home.actionButton(id), nullptr) << id.toStdString();
        EXPECT_LT(home.actionButton(id)->x(), added->x()) << id.toStdString() << " is after the added action";
    }
    EXPECT_LT(added->x(), home.moreButton()->x()) << "the added action is after More";

    QSignalSpy asked(&home, &HomeQuickActions::rowActionRequested);
    QTest::mouseClick(added, Qt::LeftButton);
    ASSERT_EQ(asked.count(), 1);
    EXPECT_EQ(asked.at(0).at(0).toString(), QStringLiteral("new_project"));

    // The customization lists the arranged actions alone, and what it changes leaves the added action in place.
    QMenu customization;
    home.populateCustomizationMenu(customization);
    for (const QAction* entry : customization.actions())
    {
        EXPECT_NE(entry->text(), QStringLiteral("New project"));
    }
    for (const QStringList& step : customizationSteps())
    {
        QAction* const entry = entryAt(home.moreMenu(), QStringList{QStringLiteral("Customize quick actions")} + step);
        ASSERT_NE(entry, nullptr) << step.join(QStringLiteral(" > ")).toStdString();
        entry->trigger();
        QCoreApplication::processEvents();
        QAbstractButton* const still = home.actionButton(QStringLiteral("new_project"));
        ASSERT_NE(still, nullptr) << "the added action went after " << step.join(QStringLiteral(" > ")).toStdString();
        EXPECT_LT(still->x(), home.moreButton()->x());
    }
    EXPECT_FALSE(stored(SettingsKeys::INTERFACE_DASHBOARD_ACTION_ORDER).contains(QStringLiteral("new_project")));
    EXPECT_FALSE(stored(SettingsKeys::INTERFACE_DASHBOARD_HIDDEN_ACTIONS).contains(QStringLiteral("new_project")));

    // Turned off, it is off; added again under its id, it is replaced, not doubled.
    home.setActionEnabled(QStringLiteral("new_project"), false);
    EXPECT_FALSE(home.actionButton(QStringLiteral("new_project"))->isEnabled());
    home.addRowAction(QStringLiteral("new_project"), QStringLiteral("New project..."), QString(), QString());
    EXPECT_EQ(home.addedRowActionIds().size(), 1);
    EXPECT_EQ(home.actionButton(QStringLiteral("new_project"))->text(), QStringLiteral("New project..."));
}

// ---- The Continue row

// With images only, the featured image is the most recent with a thumbnail: its card names it and says its format and
// size, and its Continue asks for that file to be opened.
TEST(HomeContinueRow, TheFeaturedImageIsTheMostRecentAndContinueOpensIt)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QStringList files = threeRecentImages(folder);

    HomeContinueRow row;
    row.setRecentFiles(asDeque(files));
    showAndSettle(row, QSize(1200, 240));
    ASSERT_TRUE(QTest::qWaitFor([&row]() { return !row.featuredFilePath().isEmpty(); }, kWaitMs));
    EXPECT_EQ(row.featuredFilePath(), files.front());

    const auto* title = row.findChild<QLabel*>(QStringLiteral("dashboardFeaturedFileTitle"));
    const auto* facts = row.findChild<QLabel*>(QStringLiteral("dashboardFeaturedFileMetadata"));
    ASSERT_NE(title, nullptr);
    ASSERT_NE(facts, nullptr);
    EXPECT_EQ(title->text(), QStringLiteral("M42_Light_001.fit"));
    // A file too small to hold a header is described by its format and the size of the picture read for it.
    EXPECT_EQ(facts->text().section(QLatin1Char('\n'), 0, 0), QStringLiteral("FITS \u00b7 48 x 32"))
        << facts->text().toStdString();

    QSignalSpy opened(&row, &HomeContinueRow::openRecentFileRequested);
    auto* continueButton = row.findChild<QPushButton*>(QStringLiteral("dashboardFeaturedContinueButton"));
    ASSERT_NE(continueButton, nullptr);
    QTest::mouseClick(continueButton, Qt::LeftButton);
    ASSERT_EQ(opened.count(), 1);
    EXPECT_EQ(opened.front().front().toString(), files.front());
}

// The featured image is the most recent whose picture can be read: one whose thumbnail is gone is passed over for the
// next.
TEST(HomeContinueRow, AnImageWhosePictureCannotBeReadIsPassedOver)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QString first = recentFile(folder, QStringLiteral("NoThumb_001.fit"));
    const QString second = recentFile(folder, QStringLiteral("HasThumb_001.xisf"));
    // The first file's thumbnail is recorded where no file is; the second's is on disk.
    const QString missing = QDir(folder.path()).filePath(QStringLiteral("missing_thumb.png"));
    DashboardApplication::instance()->setThumbnailPath(first, missing);
    recordThumbnail(folder, second, QColor(200, 200, 50));

    HomeContinueRow row;
    row.setRecentFiles(asDeque({first, second}));
    showAndSettle(row, QSize(1200, 240));
    ASSERT_TRUE(QTest::qWaitFor([&row]() { return !row.featuredFilePath().isEmpty(); }, kWaitMs));
    EXPECT_EQ(row.featuredFilePath(), second);
    const auto* title = row.findChild<QLabel*>(QStringLiteral("dashboardFeaturedFileTitle"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->text(), QStringLiteral("HasThumb_001.xisf"));
}

// A pinned image stays featured when another file becomes the most recent, and its pin is kept in the settings: a
// Continue row made afresh over them features it too. Unpinned, the most recent image is featured again.
TEST(HomeContinueRow, APinnedImageStaysFeaturedAndItsPinIsKept)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    QStringList files = {recentFile(folder, QStringLiteral("NGC7000.fit")),
                         recentFile(folder, QStringLiteral("M31.xisf")), recentFile(folder, QStringLiteral("M45.png"))};
    recordThumbnails(folder, files);

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
    row.setRecentFiles(asDeque(files));
    QTest::qWait(50);
    EXPECT_EQ(row.featuredFilePath(), files[1]);
    EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_PINNED_HERO_FILE), files[1]);

    {
        HomeContinueRow next;
        next.setRecentFiles(asDeque(files));
        showAndSettle(next, QSize(1200, 240));
        EXPECT_TRUE(QTest::qWaitFor([&next, &files]() { return next.featuredFilePath() == files[1]; }, kWaitMs))
            << "a row made afresh does not feature the pinned image";
        EXPECT_TRUE(next.isFeaturedPinned());
    }

    // Unpinned: the most recent image is featured again.
    QTest::mouseClick(pin, Qt::LeftButton);
    QCoreApplication::processEvents();
    EXPECT_TRUE(stored(SettingsKeys::INTERFACE_DASHBOARD_PINNED_HERO_FILE).isEmpty());
    EXPECT_TRUE(QTest::qWaitFor([&row, &files]() { return row.featuredFilePath() == files[0]; }, kWaitMs));
    EXPECT_FALSE(row.isFeaturedPinned());
}

// The featured image fades in whole: a row features the most recent image, and once another is pinned in the settings
// the next row features that one, pinned, at full opacity.
TEST(HomeContinueRow, TheFeaturedImageFadesInWhole)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QStringList files = threeRecentImages(folder);
    {
        HomeContinueRow row;
        row.setRecentFiles(asDeque(files));
        showAndSettle(row, QSize(1200, 240));
        ASSERT_TRUE(QTest::qWaitFor([&row, &files]() { return row.featuredFilePath() == files.front(); }, kWaitMs));
    }

    storeTheCurrentFormat();
    store(SettingsKeys::INTERFACE_DASHBOARD_PINNED_HERO_FILE, files[1]);
    HomeContinueRow row;
    row.setRecentFiles(asDeque(files));
    showAndSettle(row, QSize(1200, 240));
    ASSERT_TRUE(QTest::qWaitFor([&row, &files]() { return row.featuredFilePath() == files[1]; }, kWaitMs))
        << row.featuredFilePath().toStdString();
    EXPECT_TRUE(row.isFeaturedPinned());

    auto* const fade = qobject_cast<QGraphicsOpacityEffect*>(row.featuredCard()->graphicsEffect());
    ASSERT_NE(fade, nullptr) << "the featured card has no opacity effect to fade in with";
    EXPECT_TRUE(QTest::qWaitFor([fade]() { return fade->opacity() >= 0.99; }, kWaitMs)) << fade->opacity();
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
    recordThumbnails(folder, files);

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
    recordThumbnails(folder, files);
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

// For the same list and the same pin, every search keeps the files whose path or name holds it, case aside, and every
// sort orders them - most recent first, by name, or by type and then name - with the pinned file first.
TEST(HomeRecentGrid, SearchAndSortOrderTheRecentFilesWithThePinnedFirst)
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
    recordThumbnails(folder, files);
    const QString& light = files[0];
    const QString& andromeda = files[1];
    const QString& saturn = files[2];
    const QString& blue = files[3];
    const QString& bubble = files[4];
    const QString& crescent = files[5];
    store(SettingsKeys::INTERFACE_DASHBOARD_PINNED_RECENTS, bubble);

    HomeRecentGrid home;
    home.setRecentFiles(asDeque(files));
    showAndSettle(home, QSize(1200, 700));

    // Names are compared as the system's locale compares them: one that sets case aside puts andromeda before
    // Crescent, the C locale after every name that begins with a capital.
    const bool caseAside =
        QString::localeAwareCompare(QStringLiteral("andromeda.xisf"), QStringLiteral("Crescent.tif")) < 0;
    // Every file, pinned first, for each sort: most recent first, by name, by type and then name.
    const QList<QStringList> everyFile = {
        {bubble, light, andromeda, saturn, blue, crescent},
        caseAside ? QStringList{bubble, andromeda, crescent, blue, light, saturn}
                  : QStringList{bubble, crescent, blue, light, saturn, andromeda},
        {bubble, blue, light, saturn, crescent, andromeda},
    };
    // The two M42 frames, the two FITS files.
    const QList<QStringList> theM42Frames = {{light, blue}, {blue, light}, {blue, light}};
    struct Searched
    {
        QString search;
        QList<QStringList> bySort;
    };
    const QList<Searched> searches = {
        {QString(), everyFile},
        {QStringLiteral("m42"), theM42Frames},
        {QStringLiteral("FIT"), theM42Frames},
        {QStringLiteral(".x"), {{andromeda}, {andromeda}, {andromeda}}},
        {QStringLiteral("nothing-matches"), {{}, {}, {}}},
    };
    const QList<int> sorts = {DashboardPreferences::RECENT_SORT_RECENT, DashboardPreferences::RECENT_SORT_NAME,
                              DashboardPreferences::RECENT_SORT_TYPE};
    for (const Searched& searched : searches)
    {
        for (qsizetype sort = 0; sort < sorts.size(); ++sort)
        {
            SCOPED_TRACE(("search '" + searched.search + "' sort " + QString::number(sorts[sort])).toStdString());
            // The path is searched with the name: a search the folder's own path holds keeps every file.
            const QStringList expected =
                folder.path().contains(searched.search, Qt::CaseInsensitive) ? everyFile[sort] : searched.bySort[sort];
            home.searchField()->setText(searched.search);
            home.sortBox()->setCurrentIndex(home.sortBox()->findData(sorts[sort]));
            EXPECT_TRUE(QTest::qWaitFor([&home, &expected]() { return home.visibleFileOrder() == expected; }, kWaitMs))
                << "shown: " << home.visibleFileOrder().join(QStringLiteral(", ")).toStdString()
                << "\nexpected: " << expected.join(QStringLiteral(", ")).toStdString();
            EXPECT_TRUE(QTest::qWaitFor([&home, &expected]() { return tilePaths(home) == expected; }, kWaitMs))
                << "the tiles are not the order";
        }
    }
    // The search and the sort are kept in the settings.
    EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SEARCH_TEXT), searches.back().search);
    EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SORT_MODE), QStringLiteral("type"));
}

// The sort is remembered in the settings and their format: chosen in one Home, the next sorts the recent files so.
TEST(HomeRecentGrid, TheSortIsRemembered)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QStringList files = threeRecentImages(folder);
    // By type: the two FITS files by name, then the XISF file.
    const QStringList byType = {files[2], files[0], files[1]};
    {
        HomeRecentGrid home;
        home.setRecentFiles(asDeque(files));
        showAndSettle(home, QSize(1300, 800));
        home.sortBox()->setCurrentIndex(home.sortBox()->findData(DashboardPreferences::RECENT_SORT_TYPE));
        EXPECT_TRUE(QTest::qWaitFor([&home, &byType]() { return home.visibleFileOrder() == byType; }, kWaitMs))
            << home.visibleFileOrder().join(QStringLiteral(", ")).toStdString();
        EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SORT_MODE), QStringLiteral("type"));
        EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION), QStringLiteral("4"));
    }

    HomeRecentGrid home;
    home.setRecentFiles(asDeque(files));
    showAndSettle(home, QSize(1300, 800));
    EXPECT_EQ(home.sortBox()->currentText(), QStringLiteral("Type"));
    EXPECT_TRUE(QTest::qWaitFor([&home, &byType]() { return home.visibleFileOrder() == byType; }, kWaitMs))
        << home.visibleFileOrder().join(QStringLiteral(", ")).toStdString();
}

// The search is remembered in the settings and their format: typed in one Home, the next shows it and the files it
// keeps.
TEST(HomeRecentGrid, TheSearchIsRemembered)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QStringList files = threeRecentImages(folder);
    // The two M42 frames, most recent first.
    const QStringList found = {files[0], files[2]};
    {
        HomeRecentGrid home;
        home.setRecentFiles(asDeque(files));
        showAndSettle(home, QSize(1300, 800));
        home.searchField()->setText(QStringLiteral("M42"));
        EXPECT_TRUE(QTest::qWaitFor([&home, &found]() { return home.visibleFileOrder() == found; }, kWaitMs))
            << home.visibleFileOrder().join(QStringLiteral(", ")).toStdString();
        EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SEARCH_TEXT), QStringLiteral("M42"));
        EXPECT_EQ(stored(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION), QStringLiteral("4"));
    }

    HomeRecentGrid home;
    home.setRecentFiles(asDeque(files));
    showAndSettle(home, QSize(1300, 800));
    EXPECT_EQ(home.searchField()->text(), QStringLiteral("M42"));
    EXPECT_TRUE(QTest::qWaitFor([&home, &found]() { return tilePaths(home) == found; }, kWaitMs))
        << tilePaths(home).join(QStringLiteral(", ")).toStdString();
}

// The settings are read only at the format their writer writes, 4. Settings of another format - an older one, a newer
// one, or keys that state none - are never read: each is set aside under its own name with the format it states beside
// it, and Home starts from the defaults - every file, most recent first.
TEST(HomeRecentGrid, SettingsOfAnotherFormatAreSetAsideAndNotRead)
{
    const QString sortKey = QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SORT_MODE);
    const QString searchKey = QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SEARCH_TEXT);
    const QString formatKey = QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION);
    for (const QString& format : {QStringLiteral("3"), QStringLiteral("5"), QString()})
    {
        SCOPED_TRACE("format '" + format.toStdString() + "'");
        QTemporaryDir folder;
        ASSERT_TRUE(folder.isValid());
        DashboardApplication::instance()->resetForTest();
        const QStringList files = threeRecentImages(folder);
        store(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SORT_MODE, QStringLiteral("type"));
        store(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SEARCH_TEXT, QStringLiteral("M42"));
        if (!format.isEmpty())
        {
            store(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION, format);
        }
        const QString suffix =
            format.isEmpty() ? QStringLiteral(".unversioned") : QStringLiteral(".format%1").arg(format);

        HomeRecentGrid home;
        home.setRecentFiles(asDeque(files));
        showAndSettle(home, QSize(1300, 800));
        EXPECT_EQ(home.sortBox()->currentData().toInt(), DashboardPreferences::RECENT_SORT_RECENT);
        EXPECT_TRUE(home.searchField()->text().isEmpty()) << home.searchField()->text().toStdString();
        EXPECT_TRUE(QTest::qWaitFor([&home, &files]() { return home.visibleFileOrder() == files; }, kWaitMs))
            << home.visibleFileOrder().join(QStringLiteral(", ")).toStdString();
        EXPECT_EQ(settings().select(formatKey), QStringLiteral("4"));
        EXPECT_EQ(settings().select(sortKey + suffix), QStringLiteral("type"));
        EXPECT_EQ(settings().select(searchKey + suffix), QStringLiteral("M42"));
        if (!format.isEmpty())
        {
            EXPECT_EQ(settings().select(formatKey + suffix), format);
        }
    }
}

// A recent entry with a byte order mark before its path is the file the path names: read as Home reads the recent
// files, it is the pinned file, first and marked pinned.
TEST(HomeRecentGrid, AnEntryWithAByteOrderMarkIsTheFileItNamesAndKeepsItsPin)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QStringList files = threeRecentImages(folder);
    const QString& pinned = files[2];
    storeTheCurrentFormat();
    store(SettingsKeys::INTERFACE_DASHBOARD_PINNED_RECENTS, pinned);

    const QString marked = QString(QChar::ByteOrderMark) + pinned;
    ASSERT_EQ(DashboardUtils::normalizeRecentFilePath(marked), pinned);

    HomeRecentGrid home;
    home.setRecentFiles(asDeque({files[0], files[1], DashboardUtils::normalizeRecentFilePath(marked)}));
    showAndSettle(home, QSize(1300, 800));
    ASSERT_TRUE(waitForTiles(home, 3));
    const QStringList expected = {pinned, files[0], files[1]};
    EXPECT_TRUE(QTest::qWaitFor([&home, &expected]() { return home.visibleFileOrder() == expected; }, kWaitMs))
        << home.visibleFileOrder().join(QStringLiteral(", ")).toStdString();
    ASSERT_NE(tileFor(home, pinned), nullptr);
    EXPECT_TRUE(tileFor(home, pinned)->isPinned());
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
    recordThumbnails(folder, files);

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

// The grid or the list is remembered with the other settings of the recent files, in their format: chosen in one Home,
// it is how the next shows the recent files; a stored word is read in its canonical form, one that names neither is
// the grid, and a choice of another format is set aside with the other settings.
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
    recordThumbnails(folder, files);

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

// The tile's menu has five entries - Open, Pin to top, Show in File Browser, Copy path and Remove from recent files -
// and each does what it says. Pinned, the tile is first, the pin is kept in the settings and the entry unpins it.
TEST(HomeRecentGrid, TheTilesMenuHasItsFiveEntriesAndEachActs)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QStringList files = {recentFile(folder, QStringLiteral("first.fit")),
                               recentFile(folder, QStringLiteral("second.fit")),
                               recentFile(folder, QStringLiteral("third.xisf"))};
    recordThumbnails(folder, files);

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

// A click on a tile asks for its file to be opened.
TEST(HomeRecentGrid, AClickOnATileOpensItsFile)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    DashboardApplication::instance()->resetForTest();
    const QStringList files = {recentFile(folder, QStringLiteral("a.fit")),
                               recentFile(folder, QStringLiteral("b.fit"))};
    recordThumbnails(folder, files);
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
// file, and six things AfterNight does, in their order; of projects it says one line.
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
    const QStringList six = {QStringLiteral("Multi-Format Support"),  QStringLiteral("Frame Calibration"),
                             QStringLiteral("Star Alignment"),        QStringLiteral("Image Stacking"),
                             QStringLiteral("Background Extraction"), QStringLiteral("Enhancement")};
    EXPECT_EQ(features, six);

    // One text speaks of projects: the line that says they are optional.
    QStringList aboutProjects;
    for (an::ui::HomeText* text : welcome.findChildren<an::ui::HomeText*>())
    {
        if (text->text().contains(QStringLiteral("project"), Qt::CaseInsensitive))
        {
            aboutProjects << text->objectName();
        }
    }
    EXPECT_EQ(aboutProjects, QStringList{QStringLiteral("homeWelcomeProjectsLine")});
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

// Under the cards one quiet line says projects are optional, and its New project... asks for one.
TEST(HomeWelcome, TheLineAboutProjectsSaysTheyAreOptionalAndOffersOne)
{
    HomeWelcome welcome;
    showAndSettle(welcome, QSize(1300, 900));
    const auto* const line = welcome.findChild<an::ui::HomeText*>(QStringLiteral("homeWelcomeProjectsLine"));
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->text(), QStringLiteral("Projects are optional. Without one, AfterNight works as it always has."));
    EXPECT_TRUE(line->isVisible());
    auto* const button = welcome.findChild<QPushButton*>(QStringLiteral("homeWelcomeNewProject"));
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->text(), QStringLiteral("New project..."));
    EXPECT_EQ(button->property("anButtonVariant").toString(), QStringLiteral("quiet"));
    // The line stands under the cards and over the drop zone, on one row with its button.
    const QList<QAbstractButton*> cards = welcome.findChildren<QAbstractButton*>(QStringLiteral("homeWelcomeCard"));
    ASSERT_EQ(cards.size(), 3);
    const auto* const zone = welcome.findChild<QWidget*>(QStringLiteral("homeWelcomeDropZone"));
    ASSERT_NE(zone, nullptr);
    const int lineTop = line->mapTo(&welcome, QPoint(0, 0)).y();
    EXPECT_GE(lineTop, cards[0]->mapTo(&welcome, QPoint(0, cards[0]->height())).y());
    EXPECT_LE(lineTop + line->height(), zone->mapTo(&welcome, QPoint(0, 0)).y());
    EXPECT_GT(button->mapTo(&welcome, QPoint(0, 0)).x(), line->mapTo(&welcome, QPoint(0, 0)).x());

    QSignalSpy asked(&welcome, &HomeWelcome::newProjectRequested);
    QTest::mouseClick(button, Qt::LeftButton);
    EXPECT_EQ(asked.count(), 1);
}

// A file dragged over the welcome lights its drop zone, which goes dark when the drag leaves; dropped, the file is
// handed to the application to open.
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
