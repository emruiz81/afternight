// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The Session Console as the Observatory window's drawer, as a person uses it: opened from the rail, the status line's
// chip and the menu's action, under the centre and the column on every place, resized by its top edge within its
// limits, its toolbar the console's own of AfterNight 1.1, the warnings it holds counted while it is closed, floated to
// a window of its own and attached again with its content, its scroll position and its filters, and kept from one start
// to the next.

#include "ObservatoryWindowCase.h"

#include "chrome/ConsoleDrawer.h"
#include "chrome/StatusLine.h"
#include "workspaces/WorkspaceHost.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "app/SessionConsoleBridge.h"
#include "components/AnChip.h"
#include "components/AnIconRail.h"
#include "components/AnTag.h"
#include "components/RailItem.h"
#include "controls/ImageViewControl.h"
#include "controls/LogViewerControl.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "core/ApplicationCore.h"
#include "fixtures/ConsoleToolbarRecord.h"
#include "theme/ComponentParts.h"

#include <AfterNightPlatform/LoggingConfig.h>

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/LayoutSaver.h>
#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/core/DockWidget.h>

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStackedWidget>
#include <QToolBar>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using Window = an::ui::observatory::ObservatoryWindow;
using Drawer = an::ui::observatory::ConsoleDrawer;

/// Lets the window settle: posted work delivered, the deletions done, the layouts applied.
void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// The item the rail shows under an id; a default item when it shows none.
[[nodiscard]] an::ui::RailItem railItem(const an::ui::AnIconRail& rail, const QString& id)
{
    const std::vector<an::ui::RailItem>& items = rail.items();
    const auto found =
        std::find_if(items.begin(), items.end(), [&id](const an::ui::RailItem& item) { return item.id == id; });
    return found != items.end() ? *found : an::ui::RailItem{};
}

/// Clicks a rail item where a person clicks it.
void clickRail(an::ui::AnIconRail& rail, const char* id)
{
    const QRect place = rail.itemRect(QString::fromLatin1(id));
    ASSERT_TRUE(place.isValid()) << "the rail shows no item " << id;
    QTest::mouseClick(&rail, Qt::LeftButton, Qt::NoModifier, place.center());
    settle();
}

/// A widget's rectangle on the screen.
[[nodiscard]] QRect globalRect(const QWidget& widget)
{
    return QRect(widget.mapToGlobal(QPoint(0, 0)), widget.size());
}

/// Sends a mouse event to a widget at a point of its own, the left button held as a drag holds it.
void sendMouse(QWidget& widget, const QEvent::Type type, const QPoint local)
{
    const bool release = type == QEvent::MouseButtonRelease;
    QMouseEvent event(type, QPointF(local), QPointF(widget.mapToGlobal(local)),
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      release ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

/// Drags the drawer's top edge by `dy` pixels, down for a positive one.
void dragGrip(Drawer& drawer, const int dy)
{
    QWidget& grip = *drawer.grip();
    const QPoint start = grip.rect().center();
    sendMouse(grip, QEvent::MouseButtonPress, start);
    sendMouse(grip, QEvent::MouseMove, start + QPoint(0, dy / 2));
    sendMouse(grip, QEvent::MouseMove, start + QPoint(0, dy));
    sendMouse(grip, QEvent::MouseButtonRelease, start + QPoint(0, dy));
    settle();
}

/// Whether an action is one of the drawer's own parts rather than the console's.
[[nodiscard]] bool drawersOwn(const QAction* action)
{
    return action != nullptr && action->property(Drawer::kOwnPartProperty).toBool();
}

/// The session's console lines, which every case of the suite shares.
[[nodiscard]] an::ui::SessionConsoleBridge& bridge()
{
    an::ui::SessionConsoleBridge* const instance = an::ui::SessionConsoleBridge::instance();
    EXPECT_NE(instance, nullptr) << "the suite's application keeps no session console lines";
    return *instance;
}

/// The window's drawer, each case ending with it closed, attached and as high as at first.
class ConsoleDrawer : public ObservatoryWindowCase
{
protected:
    void TearDown() override
    {
        Drawer& drawer = *window().consoleDrawer();
        drawer.setFloating(false);
        window().setSessionConsoleVisible(false);
        drawer.setDrawerHeight(SettingsDefaults::INTERFACE_CONSOLE_DRAWER_HEIGHT);
        settle();
        ObservatoryWindowCase::TearDown();
    }

    [[nodiscard]] static Drawer& drawer()
    {
        return *window().consoleDrawer();
    }
};

// The rail's Console item, the status line's chip and the menu's action open the drawer, and the rail's item, the
// drawer's close button and the action close it; the action is checked while it is open, and the shell answers what
// it is asked.
TEST_F(ConsoleDrawer, OpensFromTheRailTheChipAndTheAction)
{
    QAction* const entry = core().actions().action(an::ui::shell::ids::kLogViewer);
    ASSERT_NE(entry, nullptr);
    ASSERT_FALSE(window().isSessionConsoleVisible());
    EXPECT_FALSE(drawer().isVisible());

    clickRail(*window().rail(), Window::kRailConsole);
    EXPECT_TRUE(window().isSessionConsoleVisible());
    EXPECT_TRUE(drawer().isVisible());
    EXPECT_TRUE(entry->isChecked());
    clickRail(*window().rail(), Window::kRailConsole);
    EXPECT_FALSE(window().isSessionConsoleVisible());
    EXPECT_FALSE(drawer().isVisible());
    EXPECT_FALSE(entry->isChecked());

    QTest::mouseClick(window().statusLine()->consoleChip(), Qt::LeftButton);
    settle();
    EXPECT_TRUE(window().isSessionConsoleVisible());
    drawer().closeAction()->trigger();
    settle();
    EXPECT_FALSE(window().isSessionConsoleVisible());
    EXPECT_FALSE(entry->isChecked());

    entry->trigger();
    settle();
    EXPECT_TRUE(window().isSessionConsoleVisible());
    EXPECT_TRUE(drawer().isVisible());
    entry->trigger();
    settle();
    EXPECT_FALSE(window().isSessionConsoleVisible());

    shell().setSessionConsoleVisible(true);
    settle();
    EXPECT_TRUE(shell().isSessionConsoleVisible());
    EXPECT_TRUE(drawer().isVisible());
    shell().setSessionConsoleVisible(false);
    settle();
    EXPECT_FALSE(shell().isSessionConsoleVisible());
}

// The drawer lies between the centre and the status line and spans the centre and the right column, 220 px high at
// first; the rail keeps its height beside it.
TEST_F(ConsoleDrawer, SpansTheCentreAndTheColumn)
{
    ImageViewControl* const view = openView(QStringLiteral("drawer-spans"));
    ASSERT_NE(view, nullptr);
    window().setSessionConsoleVisible(true);
    settle();
    ASSERT_TRUE(window().rightColumn()->isVisible());
    const QRect drawerRect = globalRect(drawer());
    const QRect centre = globalRect(*window().centre());
    const QRect column = globalRect(*window().rightColumn());
    const QRect status = globalRect(*window().statusLine());
    const QRect rail = globalRect(*window().rail());
    EXPECT_EQ(drawerRect.left(), centre.left());
    EXPECT_EQ(drawerRect.right(), column.right());
    EXPECT_EQ(drawerRect.top(), (std::max)(centre.bottom(), column.bottom()) + 1);
    EXPECT_EQ(drawerRect.bottom() + 1, status.top());
    EXPECT_EQ(drawerRect.height(), SettingsDefaults::INTERFACE_CONSOLE_DRAWER_HEIGHT);
    EXPECT_EQ(drawerRect.left(), rail.right() + 1);
    EXPECT_GE(rail.bottom(), drawerRect.bottom()) << "the rail is cut short by the drawer";
    // On Home with no image the column is not shown, and the drawer spans the centre.
    core().viewService().removeViewControl(view, true);
    settle();
    ASSERT_TRUE(window().isHomeShown());
    ASSERT_FALSE(window().rightColumn()->isVisible());
    EXPECT_EQ(globalRect(drawer()).left(), globalRect(*window().centre()).left());
    EXPECT_EQ(globalRect(drawer()).right(), globalRect(*window().centre()).right());
}

// Dragged by its top edge the drawer grows and shrinks, never under 120 px nor over 60 % of the window.
TEST_F(ConsoleDrawer, ResizesWithinItsLimits)
{
    window().setSessionConsoleVisible(true);
    settle();
    ASSERT_EQ(drawer().height(), SettingsDefaults::INTERFACE_CONSOLE_DRAWER_HEIGHT);
    EXPECT_EQ(drawer().minimumDrawerHeight(), an::ui::parts::kConsoleDrawerMinimumHeight);
    EXPECT_EQ(drawer().minimumDrawerHeight(), 120);
    EXPECT_EQ(drawer().maximumDrawerHeight(), window().height() * 60 / 100);

    dragGrip(drawer(), -80);
    EXPECT_EQ(drawer().height(), 300);
    EXPECT_EQ(drawer().drawerHeight(), 300);
    dragGrip(drawer(), 400);
    EXPECT_EQ(drawer().height(), 120);
    dragGrip(drawer(), -2000);
    EXPECT_EQ(drawer().height(), drawer().maximumDrawerHeight());

    drawer().setDrawerHeight(10);
    settle();
    EXPECT_EQ(drawer().height(), 120);
    drawer().setDrawerHeight(100000);
    settle();
    EXPECT_EQ(drawer().height(), drawer().maximumDrawerHeight());
    // The edge says it can be dragged.
    EXPECT_EQ(drawer().grip()->cursor().shape(), Qt::SizeVerCursor);
}

// The drawer's header is the console's toolbar of AfterNight 1.1, entry by entry, with the drawer's own parts about
// it: the title first, a stretch and the warning count before Search, Float and the close button last.
TEST_F(ConsoleDrawer, TheToolbarIsTheConsoles)
{
    an::tests::fixtures::BaselineRecord record(QString::fromLatin1(an::tests::fixtures::kConsoleToolbarRecordPath),
                                               an::tests::fixtures::consoleToolbarPreamble());
    window().setSessionConsoleVisible(true);
    settle();
    QToolBar* const header = drawer().header();
    ASSERT_NE(header, nullptr);
    EXPECT_TRUE(drawer().console()->isAncestorOf(header));
    EXPECT_TRUE(record.matches(QString::fromLatin1(an::tests::fixtures::kConsoleToolbarSection),
                               an::tests::fixtures::toolbarEntries(*header, drawersOwn)));

    const QList<QAction*> actions = header->actions();
    ASSERT_GE(actions.size(), 6);
    ASSERT_TRUE(drawersOwn(actions.first()));
    const auto* title = qobject_cast<QLabel*>(header->widgetForAction(actions.first()));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->text(), QStringLiteral("Session Console"));
    qsizetype searchIndex = -1;
    for (qsizetype index = 0; index < actions.size(); ++index)
    {
        if (actions[index]->text() == QStringLiteral("Search..."))
        {
            searchIndex = index;
        }
    }
    ASSERT_GE(searchIndex, 2);
    EXPECT_TRUE(drawersOwn(actions[searchIndex - 2]));
    EXPECT_EQ(header->widgetForAction(actions[searchIndex - 2])->sizePolicy().horizontalPolicy(),
              QSizePolicy::Expanding);
    EXPECT_TRUE(drawersOwn(actions[searchIndex - 1]));
    EXPECT_EQ(header->widgetForAction(actions[searchIndex - 1]), drawer().warningTag());
    EXPECT_EQ(actions[actions.size() - 2], drawer().floatAction());
    EXPECT_EQ(drawer().floatAction()->text(), QStringLiteral("Float"));
    EXPECT_EQ(actions.last(), drawer().closeAction());
    EXPECT_TRUE(drawersOwn(drawer().floatAction()));
    EXPECT_TRUE(drawersOwn(drawer().closeAction()));
}

// A warning or an error that arrives while the drawer is closed is counted on the rail's Console item and on the status
// line's chip until the drawer opens; one that arrives while it is open is seen. The header's tag counts the warnings
// the session holds.
TEST_F(ConsoleDrawer, TheCountRisesWhileClosedAndClearsWhenOpened)
{
    an::ui::SessionConsoleBridge& lines = bridge();
    // What the cases before logged is seen once the drawer has been open.
    window().setSessionConsoleVisible(true);
    settle();
    window().setSessionConsoleVisible(false);
    settle();
    const QString consoleItem = QString::fromLatin1(Window::kRailConsole);
    ASSERT_EQ(drawer().unseenWarnings(), 0);
    EXPECT_TRUE(railItem(*window().rail(), consoleItem).badgeText.isEmpty());
    EXPECT_TRUE(window().statusLine()->consoleChip()->countText().isEmpty());
    const int warningsBefore = drawer().warnings();

    lines.appendEntry(QStringLiteral("[warning] a first warning"), spdlog::level::warn);
    lines.appendEntry(QStringLiteral("[info] some news"), spdlog::level::info);
    lines.appendEntry(QStringLiteral("[error] an error"), spdlog::level::err);
    EXPECT_EQ(drawer().unseenWarnings(), 2);
    EXPECT_EQ(railItem(*window().rail(), consoleItem).badgeText, QStringLiteral("2"));
    EXPECT_EQ(window().statusLine()->consoleChip()->countText(), QStringLiteral("2"));
    EXPECT_EQ(drawer().warnings(), warningsBefore + 2);

    window().setSessionConsoleVisible(true);
    settle();
    EXPECT_EQ(drawer().unseenWarnings(), 0);
    EXPECT_TRUE(railItem(*window().rail(), consoleItem).badgeText.isEmpty());
    EXPECT_TRUE(window().statusLine()->consoleChip()->countText().isEmpty());
    EXPECT_TRUE(drawer().warningTag()->isVisibleTo(&drawer()));
    EXPECT_EQ(drawer().warningTag()->text(), QString::number(drawer().warnings()));

    lines.appendEntry(QStringLiteral("[warning] seen as it comes"), spdlog::level::warn);
    EXPECT_EQ(drawer().unseenWarnings(), 0);
    EXPECT_TRUE(railItem(*window().rail(), consoleItem).badgeText.isEmpty());
    EXPECT_EQ(drawer().warnings(), warningsBefore + 3);
    EXPECT_EQ(drawer().warningTag()->text(), QString::number(warningsBefore + 3));

    // Clear Session clears the lines and what they counted.
    lines.clearHistory();
    EXPECT_EQ(drawer().warnings(), 0);
    // A toolbar shows or hides an entry's widget when it lays itself out.
    settle();
    EXPECT_FALSE(drawer().warningTag()->isVisibleTo(&drawer()));
}

// Float moves the console into a window of its own - a floating window of the dock library with the affinity
// an.console, which no saved layout holds - and Attach brings it back into the drawer; the console is the same one
// throughout, its lines, its scroll position and its filters kept. Closing its window closes the console, which opens
// in its window again.
TEST_F(ConsoleDrawer, FloatsAndAttachesKeepingItsContentScrollAndFilters)
{
    an::ui::SessionConsoleBridge& lines = bridge();
    window().setSessionConsoleVisible(true);
    settle();
    LogViewerControl* const console = drawer().console();
    ASSERT_NE(console, nullptr);
    for (int line = 0; line < 400; ++line)
    {
        lines.appendEntry(QStringLiteral("[info] line %1 of a long session").arg(line), spdlog::level::info);
    }
    auto* const text = console->findChild<QPlainTextEdit*>();
    auto* const level = console->findChild<QComboBox*>();
    ASSERT_NE(text, nullptr);
    ASSERT_NE(level, nullptr);
    level->setCurrentText(QStringLiteral("debug"));
    settle();
    QScrollBar* const scroll = text->verticalScrollBar();
    ASSERT_GT(scroll->maximum(), 100);
    scroll->setValue(scroll->maximum() / 2);
    settle();
    const int position = scroll->value();
    const QString content = text->toPlainText();

    drawer().floatAction()->trigger();
    settle();
    EXPECT_TRUE(drawer().isFloating());
    EXPECT_TRUE(window().isSessionConsoleVisible());
    EXPECT_FALSE(drawer().isVisible()) << "the drawer stays open under the floating console";
    KDDockWidgets::QtWidgets::DockWidget* const dock = drawer().floatingDock();
    ASSERT_NE(dock, nullptr);
    EXPECT_TRUE(dock->isOpen());
    EXPECT_TRUE(dock->isFloating());
    EXPECT_EQ(dock->widget(), console);
    EXPECT_EQ(dock->affinities(), QStringList{QString::fromLatin1(Drawer::kAffinity)});
    EXPECT_TRUE(dock->dockWidget()->layoutSaverOptions().testFlag(KDDockWidgets::LayoutSaverOption::Skip));
    EXPECT_FALSE(window().workspaces().mainWindow()->isAncestorOf(console));
    EXPECT_FALSE(KDDockWidgets::LayoutSaver().serializeLayout().contains(dock->uniqueName().toUtf8()));
    EXPECT_EQ(drawer().floatAction()->text(), QStringLiteral("Attach"));
    EXPECT_EQ(text->toPlainText(), content);
    EXPECT_EQ(level->currentText(), QStringLiteral("debug"));
    EXPECT_EQ(scroll->value(), position);

    drawer().floatAction()->trigger();
    settle();
    EXPECT_FALSE(drawer().isFloating());
    EXPECT_TRUE(drawer().isVisible());
    EXPECT_TRUE(drawer().isAncestorOf(console));
    EXPECT_FALSE(dock->isOpen());
    EXPECT_EQ(drawer().floatAction()->text(), QStringLiteral("Float"));
    EXPECT_EQ(text->toPlainText(), content);
    EXPECT_EQ(level->currentText(), QStringLiteral("debug"));
    EXPECT_EQ(scroll->value(), position);

    // Its window closed, the console is closed and still floats: opened, it opens in its window.
    drawer().setFloating(true);
    settle();
    ASSERT_TRUE(dock->isOpen());
    // The library's views name their own window(): the widget's is the floating window.
    QWidget* const consoleWindow = static_cast<QWidget*>(dock)->window();
    ASSERT_TRUE(consoleWindow != nullptr && consoleWindow != &window());
    consoleWindow->close();
    settle();
    EXPECT_FALSE(window().isSessionConsoleVisible());
    EXPECT_TRUE(drawer().isFloating());
    EXPECT_EQ(drawer().console(), console);
    window().setSessionConsoleVisible(true);
    settle();
    EXPECT_TRUE(dock->isOpen());
    EXPECT_EQ(dock->widget(), console);
    level->setCurrentText(QStringLiteral("info"));
}

namespace
{

/// The console's readout of the file logging level: "File log: info".
[[nodiscard]] const QLabel* fileLogReadout(const LogViewerControl& console)
{
    for (const QLabel* label : console.findChildren<QLabel*>())
    {
        if (label->text().startsWith(QStringLiteral("File log:")))
        {
            return label;
        }
    }
    return nullptr;
}

/// Stores a file logging level as Settings > Logging stores it.
void storeFileLogLevel(const std::string& level)
{
    an::platform::LoggingConfig config;
    (void)an::platform::loadLoggingConfig(config);
    config.logLevel = level;
    ASSERT_TRUE(an::platform::saveLoggingConfig(config));
}

} // namespace

// The console's "File log" readout says the level Settings > Logging stores, read again each time the console is
// opened or shown again - in the drawer and in its own window - so a level changed meanwhile is the one it says.
TEST_F(ConsoleDrawer, TheFileLogReadoutIsReadAgainWhenTheConsoleIsShown)
{
    const std::filesystem::path stored = an::platform::LoggingConfig::defaultConfigPath();
    std::optional<QByteArray> before;
    {
        QFile file(QString::fromStdU16String(stored.u16string()));
        if (file.open(QIODevice::ReadOnly))
        {
            before = file.readAll();
        }
    }
    storeFileLogLevel("info");
    window().setSessionConsoleVisible(true);
    settle();
    LogViewerControl* const console = drawer().console();
    ASSERT_NE(console, nullptr);
    const QLabel* const readout = fileLogReadout(*console);
    ASSERT_NE(readout, nullptr);
    EXPECT_EQ(readout->text(), QStringLiteral("File log: info"));

    window().setSessionConsoleVisible(false);
    settle();
    storeFileLogLevel("debug");
    window().setSessionConsoleVisible(true);
    settle();
    EXPECT_EQ(readout->text(), QStringLiteral("File log: debug")) << "opened again, it says the level stored now";

    storeFileLogLevel("warning");
    drawer().setFloating(true);
    settle();
    EXPECT_EQ(readout->text(), QStringLiteral("File log: warning")) << "shown again in its own window";

    window().setSessionConsoleVisible(false);
    settle();
    storeFileLogLevel("trace");
    window().setSessionConsoleVisible(true);
    settle();
    EXPECT_EQ(readout->text(), QStringLiteral("File log: trace")) << "opened again in its own window";

    if (before.has_value())
    {
        QFile file(QString::fromStdU16String(stored.u16string()));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(*before);
    }
    else
    {
        std::error_code ignored;
        std::filesystem::remove(stored, ignored);
    }
}

// The drawer is the window's, not a place's: it stays open on Home, on the workspace and from one to the other, and the
// workspace's dock area does not hold it.
TEST_F(ConsoleDrawer, IsAvailableOnHomeAndOnAWorkspace)
{
    ASSERT_TRUE(window().isHomeShown());
    window().setSessionConsoleVisible(true);
    settle();
    EXPECT_TRUE(window().isHomeShown()) << "opening the console left Home";
    EXPECT_TRUE(drawer().isVisible());

    ImageViewControl* const view = openView(QStringLiteral("drawer-places"));
    ASSERT_NE(view, nullptr);
    settle();
    EXPECT_TRUE(window().isWorkspaceShown());
    EXPECT_TRUE(drawer().isVisible());
    EXPECT_FALSE(window().workspaces().mainWindow()->isAncestorOf(drawer().console()));

    clickRail(*window().rail(), Window::kRailHome);
    EXPECT_TRUE(window().isHomeShown());
    EXPECT_TRUE(drawer().isVisible());
    clickRail(*window().rail(), Window::kRailWorkspace);
    EXPECT_TRUE(window().isWorkspaceShown());
    EXPECT_TRUE(drawer().isVisible());

    // The last image closed, Home is shown, and the drawer with it.
    core().viewService().removeViewControl(view, true);
    settle();
    EXPECT_TRUE(window().isHomeShown());
    EXPECT_TRUE(drawer().isVisible());
}

/// The drawer kept from one start to the next, each start of its own.
class ConsoleDrawerKept : public ::testing::Test
{
};

// The drawer's open state, its height and whether the console floats are what the next start shows.
TEST_F(ConsoleDrawerKept, FromOneStartToTheNext)
{
    const auto stop = [](std::unique_ptr<RunningObservatory> running) {
        running->shell->window()->close();
        const an::Status shut = stopObservatory(std::move(running));
        EXPECT_TRUE(shut.ok()) << shut.message();
    };
    const auto stored = [](const RunningObservatory& running, const char* key) {
        return running.core->globalSettings()->select(QString::fromLatin1(key), QString());
    };

    {
        std::unique_ptr<RunningObservatory> running = startObservatory();
        settle();
        Drawer& drawer = *running->shell->window()->consoleDrawer();
        ASSERT_FALSE(running->shell->window()->isSessionConsoleVisible());
        running->shell->window()->setSessionConsoleVisible(true);
        drawer.setDrawerHeight(300);
        settle();
        EXPECT_EQ(stored(*running, SettingsKeys::INTERFACE_CONSOLE_DRAWER_OPEN), QStringLiteral("1"));
        EXPECT_EQ(stored(*running, SettingsKeys::INTERFACE_CONSOLE_DRAWER_HEIGHT), QStringLiteral("300"));
        EXPECT_EQ(stored(*running, SettingsKeys::INTERFACE_CONSOLE_FLOATING), QStringLiteral("0"));
        stop(std::move(running));
    }
    {
        std::unique_ptr<RunningObservatory> running = startObservatory();
        settle();
        Drawer& drawer = *running->shell->window()->consoleDrawer();
        EXPECT_TRUE(running->shell->window()->isSessionConsoleVisible());
        EXPECT_TRUE(drawer.isVisible());
        EXPECT_FALSE(drawer.isFloating());
        EXPECT_EQ(drawer.drawerHeight(), 300);
        EXPECT_EQ(drawer.height(), 300);
        drawer.setFloating(true);
        settle();
        EXPECT_EQ(stored(*running, SettingsKeys::INTERFACE_CONSOLE_FLOATING), QStringLiteral("1"));
        stop(std::move(running));
    }
    {
        std::unique_ptr<RunningObservatory> running = startObservatory();
        settle();
        Drawer& drawer = *running->shell->window()->consoleDrawer();
        EXPECT_TRUE(running->shell->window()->isSessionConsoleVisible());
        EXPECT_TRUE(drawer.isFloating());
        ASSERT_NE(drawer.floatingDock(), nullptr);
        EXPECT_TRUE(drawer.floatingDock()->isOpen());
        // Put back as a first start has it, closed in the next start.
        drawer.setFloating(false);
        running->shell->window()->setSessionConsoleVisible(false);
        drawer.setDrawerHeight(SettingsDefaults::INTERFACE_CONSOLE_DRAWER_HEIGHT);
        settle();
        stop(std::move(running));
    }
    {
        std::unique_ptr<RunningObservatory> running = startObservatory();
        settle();
        EXPECT_FALSE(running->shell->window()->isSessionConsoleVisible());
        EXPECT_EQ(running->shell->window()->consoleDrawer()->drawerHeight(),
                  SettingsDefaults::INTERFACE_CONSOLE_DRAWER_HEIGHT);
        stop(std::move(running));
    }
}

} // namespace
} // namespace an::tests::observatory
