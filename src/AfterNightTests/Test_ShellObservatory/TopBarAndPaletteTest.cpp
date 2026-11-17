// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The Observatory top bar and the command palette: the bar's parts in each density, the breadcrumb of the place, the
// palette's field in its three widths, the palette opened by Ctrl+K and running what the menus run through their
// guards, Night over the whole window, and the shortcuts this shell adds, which no 1.1 shortcut meets.

#include "ObservatoryWindowCase.h"

#include "ShellActions.h"
#include "chrome/MenuBarOnAlt.h"
#include "chrome/PaletteField.h"
#include "chrome/TopBar.h"
#include "pages/WorkspacePage.h"

#include "Test_ShellClassic/InventoryRecords.h"
#include "fixtures/BaselineRecord.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "actions/MenuTree.h"
#include "app/ProcessRegistry.h"
#include "commands/CommandIndex.h"
#include "components/AnBreadcrumb.h"
#include "components/AnCommandPalette.h"
#include "controls/ImageViewControl.h"
#include "controls/KDDockWidgetsHelper.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "core/ApplicationCore.h"
#include "services/FavoriteProcessesModel.h"
#include "theme/AnDesignTokens.h"

#include <kddockwidgets/DockWidget.h>

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QStackedWidget>
#include <QThreadPool>
#include <QToolButton>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <memory>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using TopBar = ObservatoryWindowCase;
using Palette = ObservatoryWindowCase;
using Night = ObservatoryWindowCase;
using NewShortcuts = ObservatoryWindowCase;
using Timing = ObservatoryWindowCase;

void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// Resizes the window and lays it out at the new size.
void resizeTo(an::ui::observatory::ObservatoryWindow& window, const QSize& size)
{
    window.resize(size);
    settle();
}

/// Every row id a query of the core's index answers.
[[nodiscard]] QSet<QString> rowIdsFor(an::ui::shell::CommandIndex& index, const QString& text)
{
    QSet<QString> ids;
    for (const an::ui::CommandRowGroup& group : index.query(text))
    {
        for (const an::ui::CommandRow& row : group.rows)
        {
            ids.insert(row.id);
        }
    }
    return ids;
}

/// Every action id the menu tree's menus hold, the nested ones included.
void collectTreeActions(const an::ui::shell::MenuTree& tree, const std::string_view menuId,
                        std::vector<std::string>& actions)
{
    const an::ui::shell::MenuNode* const node = tree.menu(menuId);
    if (node == nullptr)
    {
        return;
    }
    for (const an::ui::shell::MenuItem& item : node->items)
    {
        if (item.kind == an::ui::shell::MenuItemKind::Action)
        {
            actions.emplace_back(item.id);
        }
        else if (item.kind == an::ui::shell::MenuItemKind::Menu)
        {
            collectTreeActions(tree, item.id, actions);
        }
    }
}

// The bar is the density's top bar high, its palette field the bar's height less 14 and centred in it.
TEST_F(TopBar, GeometryByDensity)
{
    const an::ui::ThemeDensity before = an::ui::themeDensity();
    for (const an::ui::ThemeDensity density : {an::ui::ThemeDensity::Compact, an::ui::ThemeDensity::Comfortable})
    {
        an::ui::setThemeDensity(density);
        window().refreshTheme();
        settle();
        an::ui::observatory::TopBar& bar = *window().topBar();
        const int barTop = an::ui::tokens().metrics.barTop;
        EXPECT_EQ(bar.height(), barTop);
        const QRect field = bar.paletteField()->geometry();
        EXPECT_EQ(field.height(), barTop - 14);
        EXPECT_LE(std::abs(field.center().x() - bar.rect().center().x()), 1) << "the field is not centred";
    }
    an::ui::setThemeDensity(before);
    window().refreshTheme();
    settle();
}

// The breadcrumb says where the person is: Home; the workspace and its active view; the workspace alone while it shows
// no view of its own, every image floating in a window of its own.
TEST_F(TopBar, TheBreadcrumbNamesThePlaceAndItsItem)
{
    an::ui::AnBreadcrumb& crumbs = *window().topBar()->breadcrumb();
    EXPECT_EQ(crumbs.parts(), QStringList{QStringLiteral("Home")});

    ImageViewControl* const view = openView(QStringLiteral("breadcrumb-view"));
    ASSERT_NE(view, nullptr);
    settle();
    EXPECT_EQ(crumbs.parts(), (QStringList{QStringLiteral("Workspace 1"), QString::fromUtf8(view->getUniqueName())}));

    view->getDockWidget()->setFloating(true);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() {
        settle();
        return window().workspacePage()->isFloatingImagesShown();
    }));
    ASSERT_TRUE(window().isWorkspaceShown());
    EXPECT_EQ(crumbs.parts(), QStringList{QStringLiteral("Workspace 1")});
    view->getDockWidget()->setFloating(false);
    settle();
}

// A name too long for the bar is elided in the middle, never shortened to a part of itself, with the whole path as the
// tooltip.
TEST_F(TopBar, ALongNameIsElidedInTheMiddleWithTheWholeNameAsTooltip)
{
    const QString name = QStringLiteral("stacked_3008x3004_Red_Caldwell_77_integration_of_four_nights_RGB_final");
    ImageViewControl* const view = openView(name);
    ASSERT_NE(view, nullptr);
    const QSize before = window().size();
    resizeTo(window(), QSize(1024, before.height()));
    an::ui::AnBreadcrumb& crumbs = *window().topBar()->breadcrumb();
    ASSERT_TRUE(crumbs.isElided()) << "the breadcrumb has room for " << crumbs.width() << " px";
    const QString shown = crumbs.shownParts().constLast();
    EXPECT_TRUE(shown.startsWith(name.left(4)));
    EXPECT_TRUE(shown.endsWith(name.right(4)));
    EXPECT_TRUE(crumbs.toolTip().contains(name));
    resizeTo(window(), before);
}

// The palette's field is 340 px wide from a 1200 px window, 240 px under it, and an icon button under 1100 px.
TEST_F(TopBar, TheFieldsThreeWidths)
{
    const QSize before = window().size();
    an::ui::observatory::PaletteField& field = *window().topBar()->paletteField();
    resizeTo(window(), QSize(1300, before.height()));
    EXPECT_EQ(field.form(), an::ui::observatory::PaletteField::Form::Wide);
    EXPECT_EQ(field.width(), 340);
    resizeTo(window(), QSize(1150, before.height()));
    EXPECT_EQ(field.form(), an::ui::observatory::PaletteField::Form::Narrow);
    EXPECT_EQ(field.width(), 240);
    resizeTo(window(), QSize(1050, before.height()));
    EXPECT_EQ(field.form(), an::ui::observatory::PaletteField::Form::Icon);
    EXPECT_EQ(field.width(), 32);
    resizeTo(window(), before);
}

// Night, Help, Information and More at the bar's right: Night the shell's toggle, Help the user guide with its key,
// Information About, About Qt and System Information, More the menu tree.
TEST_F(TopBar, TheButtonsAtItsRight)
{
    an::ui::observatory::TopBar& bar = *window().topBar();
    auto* const night = bar.findChild<QToolButton*>(QStringLiteral("topBarNightButton"));
    auto* const help = bar.findChild<QToolButton*>(QStringLiteral("topBarHelpButton"));
    auto* const information = bar.findChild<QToolButton*>(QStringLiteral("topBarInformationButton"));
    auto* const more = bar.findChild<QToolButton*>(QStringLiteral("topBarMoreButton"));
    ASSERT_NE(night, nullptr);
    ASSERT_NE(help, nullptr);
    ASSERT_NE(information, nullptr);
    ASSERT_NE(more, nullptr);
    EXPECT_EQ(night->defaultAction(), window().shellActions().action(an::ui::observatory::ids::kToggleNight));
    EXPECT_TRUE(
        night->toolTip().contains(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N).toString(QKeySequence::NativeText)));
    EXPECT_TRUE(help->toolTip().contains(QStringLiteral("F1"))) << help->toolTip().toStdString();
    ASSERT_NE(information->menu(), nullptr);
    const QList<QAction*> about = information->menu()->actions();
    ASSERT_EQ(about.size(), 3);
    EXPECT_EQ(about[0], core().actions().action(an::ui::shell::ids::kAboutAfterNight));
    EXPECT_EQ(about[1], core().actions().action(an::ui::shell::ids::kAboutQt));
    EXPECT_EQ(about[2], core().actions().action(an::ui::shell::ids::kSystemInformation));
    ASSERT_NE(more->menu(), nullptr);
    EXPECT_EQ(more->menu(), window().moreMenu());
    EXPECT_EQ(window().moreMenu()->actions().size(),
              static_cast<qsizetype>(an::ui::shell::MenuTree::menuBar().topLevel().size()));
}

// Ctrl+K opens the palette over the centre, the centre and the column under the scrim; Esc closes it and the scrim
// goes; a click on the field opens it too.
TEST_F(Palette, OpensWithCtrlK)
{
    an::ui::AnCommandPalette& palette = *window().palette();
    ASSERT_TRUE(an::tests::app::activateWindow(window()));
    QTest::keyClick(window().centre(), Qt::Key_K, Qt::ControlModifier);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return palette.isVisible(); }, 3000));
    EXPECT_TRUE(window().paletteScrim()->isVisible());
    EXPECT_TRUE(window().paletteScrim()->geometry().contains(window().centre()->geometry().center()));

    QTest::keyClick(palette.queryField(), Qt::Key_Escape);
    EXPECT_TRUE(an::tests::app::processEventsUntil([&]() { return !palette.isVisible(); }, 3000));
    EXPECT_FALSE(window().paletteScrim()->isVisible());

    window().topBar()->paletteField()->click();
    EXPECT_TRUE(an::tests::app::processEventsUntil([&]() { return palette.isVisible(); }, 3000));
    palette.close();
    settle();
}

// With no image, a process run from the palette goes through the launcher's guards as the menu's entry does: the
// guard's message appears, and nothing opens.
TEST_F(Palette, RunsAProcessThroughTheGuards)
{
    ASSERT_TRUE(views().empty());
    an::ui::shell::CommandIndex& index = core().commands();
    index.refresh();
    const QString id = QStringLiteral("process:tone_brightness.actionAdjustBrightness_Contrast");
    const an::ui::shell::CommandEntry* const entry = index.entry(id);
    ASSERT_NE(entry, nullptr) << "the palette does not list Brightness/Contrast";
    const QString title = entry->title;

    window().openPalette();
    an::ui::AnCommandPalette& palette = *window().palette();
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return palette.isVisible(); }, 3000));
    QTest::keyClicks(palette.queryField(), title);
    settle();
    ASSERT_TRUE(palette.shownRowIds().contains(id));
    palette.setSelectedId(id);
    auto said = std::make_shared<an::tests::app::Answered>();
    const std::shared_ptr<an::tests::app::WindowPoll> answering =
        an::tests::app::answerWhenShown([said](QWidget* box) { return an::tests::app::pressYesOrOk(box, said.get()); });
    QTest::keyClick(palette.queryField(), Qt::Key_Return);
    EXPECT_TRUE(an::tests::app::processEventsUntil([&]() { return said->seen; }, 5000)) << "the guard said nothing";
    answering->ended = true;
    EXPECT_FALSE(said->text.isEmpty());
    settle();
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        EXPECT_FALSE(dynamic_cast<TopLevelProcessBaseControl*>(widget) != nullptr && widget->isVisible())
            << "a process window opened with no image";
    }
    if (palette.isVisible())
    {
        palette.close();
    }
    settle();
}

// A command run from the palette runs and is kept among the commands run last, first.
TEST_F(Palette, RunsAnActionAndRecordsIt)
{
    ImageViewControl* const view = openView(QStringLiteral("palette-runs-home"));
    ASSERT_NE(view, nullptr);
    settle();
    ASSERT_TRUE(window().isWorkspaceShown());
    window().openPalette();
    an::ui::AnCommandPalette& palette = *window().palette();
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return palette.isVisible(); }, 3000));
    QTest::keyClicks(palette.queryField(), QStringLiteral("Home"));
    settle();
    const QString id = QStringLiteral("action:actionShow_Home");
    ASSERT_TRUE(palette.shownRowIds().contains(id)) << palette.shownRowIds().join(QStringLiteral(", ")).toStdString();
    palette.setSelectedId(id);
    QTest::keyClick(palette.queryField(), Qt::Key_Return);
    settle();
    EXPECT_TRUE(window().isHomeShown());
    ASSERT_FALSE(core().commands().recentIds().empty());
    EXPECT_EQ(core().commands().recentIds().front(), id);
    // The palette opened again lists it first among the commands run last.
    const std::vector<an::ui::CommandRowGroup> empty = core().commands().query(QString());
    const auto recent = std::find_if(empty.begin(), empty.end(), [](const an::ui::CommandRowGroup& group) {
        return group.id == QLatin1String("recent");
    });
    ASSERT_NE(recent, empty.end());
    ASSERT_FALSE(recent->rows.empty());
    EXPECT_EQ(recent->rows.front().id, id);
}

// An empty query lists the favourite processes first, then the commands run last.
TEST_F(Palette, AnEmptyQueryListsTheFavouritesFirst)
{
    const std::string process = "tone_brightness.actionAdjustBrightness_Contrast";
    core().favoriteProcesses().setIds({process});
    core().commands().refresh();
    const std::vector<an::ui::CommandRowGroup> empty = core().commands().query(QString());
    ASSERT_FALSE(empty.empty());
    EXPECT_EQ(empty.front().id, QStringLiteral("favourites"));
    ASSERT_FALSE(empty.front().rows.empty());
    EXPECT_EQ(empty.front().rows.front().id, QStringLiteral("process:") + QString::fromStdString(process));
    core().favoriteProcesses().setIds({});
}

// Every action of the menu tree is found in the palette by its text, and so is each of the shell's own actions.
TEST_F(Palette, FindsEveryMenuAction)
{
    an::ui::shell::CommandIndex& index = core().commands();
    index.refresh();
    std::vector<std::string> actions;
    const an::ui::shell::MenuTree& tree = an::ui::shell::MenuTree::menuBar();
    for (const std::string_view top : tree.topLevel())
    {
        collectTreeActions(tree, top, actions);
    }
    ASSERT_GT(actions.size(), 100u);
    QStringList missing;
    for (const std::string& id : actions)
    {
        const QAction* const action = core().actions().action(id);
        if (action == nullptr || QString(action->text()).remove(QLatin1Char('&')).isEmpty())
        {
            continue;
        }
        // An action that launches a process is offered as the process.
        QString command = QStringLiteral("action:") + QString::fromStdString(id);
        for (const auto& descriptor : an::ui::process_registry::ProcessRegistry::instance().all())
        {
            if (descriptor.action_object_name == id)
            {
                command = QStringLiteral("process:") + QString::fromStdString(descriptor.id);
                break;
            }
        }
        if (!rowIdsFor(index, QString(action->text()).remove(QLatin1Char('&'))).contains(command))
        {
            missing.append(QString::fromStdString(id));
        }
    }
    for (const QAction* action : window().shellActions().all())
    {
        const QString command = QStringLiteral("action:") + action->objectName();
        if (!rowIdsFor(index, action->text()).contains(command))
        {
            missing.append(action->objectName());
        }
    }
    EXPECT_TRUE(missing.isEmpty()) << "not found by their text: " << missing.join(QStringLiteral(", ")).toStdString();
}

// Night, from its action, repaints the whole window red with an image open: no pixel of a grab holds green or blue;
// switched off, the window has its colours again.
TEST_F(Night, ToggleRepaintsEverythingRed)
{
    ImageViewControl* const view = openView(QStringLiteral("night-everything-red"), 0.5f);
    ASSERT_NE(view, nullptr);
    settle();
    QAction* const night = window().shellActions().action(an::ui::observatory::ids::kToggleNight);
    ASSERT_NE(night, nullptr);
    ASSERT_FALSE(an::ui::nightModeSet());
    night->trigger();
    ASSERT_TRUE(an::ui::nightModeSet());
    // The Night forms of the pictures are made on the pool and shown once they are kept.
    for (int round = 0; round < 4; ++round)
    {
        QThreadPool::globalInstance()->waitForDone();
        settle();
    }
    const QImage grab = window().grab().toImage().convertToFormat(QImage::Format_RGB32);
    int coloured = 0;
    QPoint first(-1, -1);
    for (int y = 0; y < grab.height(); ++y)
    {
        const auto* line = reinterpret_cast<const QRgb*>(grab.constScanLine(y));
        for (int x = 0; x < grab.width(); ++x)
        {
            if (qGreen(line[x]) != 0 || qBlue(line[x]) != 0)
            {
                if (coloured == 0)
                {
                    first = QPoint(x, y);
                }
                ++coloured;
            }
        }
    }
    QWidget* const offender = coloured > 0 ? window().childAt(first) : nullptr;
    EXPECT_EQ(coloured, 0) << coloured << " pixels hold green or blue, the first at " << first.x() << "," << first.y()
                           << " in " << (offender != nullptr ? offender->metaObject()->className() : "nothing") << " "
                           << (offender != nullptr ? offender->objectName().toStdString() : std::string());

    night->trigger();
    settle();
    EXPECT_FALSE(an::ui::nightModeSet());
}

// The shortcuts this shell adds - Ctrl+K, Ctrl+Shift+N and Ctrl+W - are none of the window's, the Session Console's or
// a view's in AfterNight 1.1.
TEST_F(NewShortcuts, DoNotCollideWithTheRecordedOnes)
{
    const an::tests::fixtures::BaselineRecord record(QString::fromLatin1(an::tests::app::kShortcutInventoryPath),
                                                     an::tests::app::shortcutInventoryPreamble());
    QStringList recorded;
    for (const QString& section : record.sections())
    {
        for (const QString& entry : record.entries(section))
        {
            for (const QString& field : an::tests::fixtures::baselineFields(entry))
            {
                recorded.append(field.trimmed());
            }
        }
    }
    ASSERT_GT(recorded.size(), 20);
    for (const QKeySequence& added :
         {QKeySequence(Qt::CTRL | Qt::Key_K), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N),
          QKeySequence(Qt::CTRL | Qt::Key_W)})
    {
        const QString portable = added.toString(QKeySequence::PortableText);
        EXPECT_FALSE(recorded.contains(portable)) << portable.toStdString() << " is a shortcut of 1.1";
    }
}

// Ctrl+W closes the active image in this shell: the Close action carries it here, and the classic shell's record of its
// shortcuts gives Close none.
TEST_F(NewShortcuts, CtrlWIsTheObservatoryShellsAlone)
{
    QAction* const close = core().actions().action(an::ui::shell::ids::kClose);
    ASSERT_NE(close, nullptr);
    EXPECT_EQ(close->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_W));
    const an::tests::fixtures::BaselineRecord record(QString::fromLatin1(an::tests::app::kShortcutInventoryPath),
                                                     an::tests::app::shortcutInventoryPreamble());
    // The record lists every action of 1.1 that has a shortcut: Close is not among them.
    for (const QString& section : record.sections())
    {
        for (const QString& entry : record.entries(section))
        {
            EXPECT_FALSE(an::tests::fixtures::baselineFields(entry).contains(QStringLiteral("actionClose")))
                << entry.toStdString();
        }
    }
}

// The time from Ctrl+K to the palette painted, the median of twenty opens. Measured, never asserted, and only when
// AFTERNIGHT_TEST_TIMINGS=1 asks for it.
TEST_F(Timing, OpeningThePalette)
{
    if (qEnvironmentVariable("AFTERNIGHT_TEST_TIMINGS") != QStringLiteral("1"))
    {
        GTEST_SKIP() << "a time is measured only when AFTERNIGHT_TEST_TIMINGS=1 asks for it";
    }
    an::ui::AnCommandPalette& palette = *window().palette();
    ASSERT_TRUE(an::tests::app::activateWindow(window()));
    class Painted final : public QObject
    {
    public:
        bool painted = false;

    protected:
        bool eventFilter(QObject* watched, QEvent* event) override
        {
            if (event != nullptr && event->type() == QEvent::Paint)
            {
                painted = true;
            }
            return QObject::eventFilter(watched, event);
        }
    } seen;
    palette.installEventFilter(&seen);
    std::vector<double> times;
    for (int open = 0; open < 20; ++open)
    {
        seen.painted = false;
        QElapsedTimer timer;
        timer.start();
        QTest::keyClick(window().centre(), Qt::Key_K, Qt::ControlModifier);
        while (!seen.painted && timer.elapsed() < 5000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        }
        times.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
        palette.close();
        settle();
    }
    palette.removeEventFilter(&seen);
    std::sort(times.begin(), times.end());
    const double median = times[times.size() / 2];
    std::printf("\nOpening the palette, median of 20 opens: %.2f ms (max %.2f ms)\n", median, times.back());
    ::testing::Test::RecordProperty("medianMs", std::to_string(median));
}

} // namespace
} // namespace an::tests::observatory
