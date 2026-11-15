// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The rail: a column the rail's width high, the first surface with a hairline at its right, showing the items it is
// given - places at the top, tasks under them past a short hairline, the foot at the bottom - and saying what the user
// does with them. A place or a foot item is a 36 px row with a 32 px square and a 20 px icon; the selected one is the
// accent's container with a 3 x 20 accent bar at the rail's left edge. A task is a 42 px row with its target's
// thumbnail, a ring with the run's progress, the process's glyph and the dot of a result not yet seen. Too many tasks
// scroll and end with an item counting the hidden ones, which lists them all. The rail is one stop for the keyboard and
// a list to assistive technology; a task's thumbnail is dragged with a ghost under the cursor. It decides nothing about
// what an item stands for or what a drag means.

#include "RenderTestSupport.h"

#include "components/AnIconRail.h"
#include "components/AnTaskThumbnail.h"
#include "components/RailItem.h"
#include "theme/AnimationUtils.h"
#include "theme/ComponentParts.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

#include <chrono>
#include <cmath>
#include <numbers>
#include <vector>

namespace
{

using an::tests::widgets::expectColour;
using an::tests::widgets::forEachLook;
using an::tests::widgets::forEachRatio;
using an::tests::widgets::pumpFor;
using an::tests::widgets::pumpUntil;
using an::tests::widgets::recordedText;
using an::tests::widgets::RenderedPicture;
using an::tests::widgets::showOffScreen;
using an::ui::AnIconRail;
using an::ui::DesignTokens;
using an::ui::RailItem;
using an::ui::RailRunPhase;
using an::ui::RailSection;
using namespace an::ui::parts;
using namespace std::chrono_literals;

RailItem place(const QString& id, const QString& icon, const QString& text, const bool selected = false)
{
    RailItem item;
    item.id = id;
    item.section = RailSection::Place;
    item.iconKey = icon;
    item.text = text;
    item.selected = selected;
    return item;
}

RailItem foot(const QString& id, const QString& icon, const QString& text)
{
    RailItem item = place(id, icon, text);
    item.section = RailSection::Foot;
    return item;
}

/// A picture of one colour, as a target's thumbnail.
QImage picture(const QColor& colour)
{
    QImage image(60, 60, QImage::Format_ARGB32_Premultiplied);
    image.fill(colour);
    return image;
}

RailItem task(const QString& id, const QString& text, const RailRunPhase phase = RailRunPhase::None,
              const std::optional<int> progress = std::nullopt)
{
    RailItem item;
    item.id = id;
    item.section = RailSection::Task;
    item.text = text;
    item.glyphKey = QStringLiteral("tune");
    item.thumbnail = picture(QColor(200, 60, 40));
    item.runPhase = phase;
    item.progress = progress;
    return item;
}

/// Home and a workspace, two tasks, the console and the settings.
std::vector<RailItem> ordinaryRail()
{
    return {place(QStringLiteral("home"), QStringLiteral("home"), QStringLiteral("Home"), true),
            place(QStringLiteral("ws1"), QStringLiteral("images"), QStringLiteral("Workspace 1")),
            place(QStringLiteral("integration"), QStringLiteral("layers"), QStringLiteral("Integration")),
            task(QStringLiteral("t1"), QStringLiteral("Curves Transformation - M20_FINAL_PNG_05")),
            task(QStringLiteral("t2"), QStringLiteral("Non Local Bayes - stacked_3008x3004_Luminance_Caldwell_77"),
                 RailRunPhase::Running, 42),
            foot(QStringLiteral("console"), QStringLiteral("terminal"), QStringLiteral("Console")),
            foot(QStringLiteral("settings"), QStringLiteral("settings"), QStringLiteral("Settings"))};
}

/// A rail shown off screen at its own size, holding the items.
struct ShownRail
{
    explicit ShownRail(const std::vector<RailItem>& items, const int height = 0)
    {
        showOffScreen(rail);
        rail.setItems(items);
        rail.resize(rail.sizeHint().width(), height > 0 ? height : rail.sizeHint().height());
    }

    AnIconRail rail;
};

/// The point of a task's ring at an angle, clockwise from the top, in degrees.
QPointF ringPoint(const QRect& row, const qreal degrees)
{
    const QPointF centre = QRectF(row).center();
    const qreal radius = (kTaskBox - kTaskRing) / 2.0;
    const qreal angle = degrees * std::numbers::pi / 180.0;
    return QPointF(centre.x() + radius * std::sin(angle), centre.y() - radius * std::cos(angle));
}

/// The rectangle a task's thumbnail box takes in its row.
QRectF taskBox(const QRect& row)
{
    const QPointF centre = QRectF(row).center();
    return QRectF(centre.x() - kTaskBox / 2.0, centre.y() - kTaskBox / 2.0, kTaskBox, kTaskBox);
}

QPoint global(const QWidget& widget, const QPoint& local)
{
    return widget.mapToGlobal(local);
}

void sendMouse(QWidget& widget, const QEvent::Type type, const QPoint& local, const Qt::MouseButton button,
               const Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(local), QPointF(widget.mapToGlobal(local)), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

} // namespace

// The rail is the rail's width in both densities; a place and a foot item are 36 px rows, a task 42 px, 2 px apart; the
// places start 6 px from the top and the foot ends 6 px from the bottom; the short hairline lies between the places and
// the tasks only when there are tasks.
TEST(Rail, Geometry)
{
    forEachLook([](const DesignTokens& t) {
        ShownRail shown(ordinaryRail(), 600);
        AnIconRail& rail = shown.rail;
        EXPECT_EQ(rail.sizeHint().width(), t.metrics.rail);
        EXPECT_EQ(rail.width(), t.metrics.rail);
        const QRect home = rail.itemRect(QStringLiteral("home"));
        EXPECT_EQ(home, QRect(0, kRailPadding, t.metrics.rail, kRailPlaceRow));
        EXPECT_EQ(rail.itemRect(QStringLiteral("ws1")).top(), home.bottom() + 1 + kRailGap);
        const QRect integration = rail.itemRect(QStringLiteral("integration"));
        const QRect hairline = rail.hairlineRect();
        ASSERT_FALSE(hairline.isNull());
        EXPECT_EQ(hairline.size(), QSize(kRailHairlineWidth, 1));
        EXPECT_EQ(hairline.center().x(), t.metrics.rail / 2 - (t.metrics.rail % 2 == 0 ? 1 : 0));
        EXPECT_EQ(hairline.top(), integration.bottom() + 1 + kRailGap + kRailHairlineAbove);
        const QRect first = rail.itemRect(QStringLiteral("t1"));
        EXPECT_EQ(first.height(), kRailTaskRow);
        EXPECT_EQ(first.top(), hairline.bottom() + 1 + kRailHairlineBelow + kRailGap);
        EXPECT_EQ(rail.itemRect(QStringLiteral("t2")).top(), first.bottom() + 1 + kRailGap);
        const QRect settings = rail.itemRect(QStringLiteral("settings"));
        EXPECT_EQ(settings.bottom(), rail.height() - 1 - kRailPadding);
        EXPECT_EQ(settings.height(), kRailPlaceRow);
        EXPECT_EQ(rail.itemRect(QStringLiteral("console")).bottom(), settings.top() - 1 - kRailGap);
        EXPECT_EQ(rail.sizeHint().height(), 2 * kRailPadding + 3 * kRailPlaceRow + kRailHairlineAbove + 1 +
                                                kRailHairlineBelow + 2 * kRailTaskRow + 2 * kRailPlaceRow +
                                                7 * kRailGap);

        std::vector<RailItem> noTasks = ordinaryRail();
        std::erase_if(noTasks, [](const RailItem& item) { return item.section == RailSection::Task; });
        rail.setItems(noTasks);
        EXPECT_TRUE(rail.hairlineRect().isNull()) << "no hairline without tasks";
        EXPECT_TRUE(rail.itemRect(QStringLiteral("t1")).isNull());

        // The first surface, a hairline at the right.
        const QColor bg = t.layers.surface0;
        forEachRatio(rail, bg, [&](const RenderedPicture& p) {
            expectColour(p.at(t.metrics.rail / 2.0, rail.height() / 2.0), t.layers.surface1, "the rail");
            expectColour(p.at(t.metrics.rail - 0.5, rail.height() / 2.0),
                         an::ui::compositeOver(t.colors.borderSubtle, t.layers.surface1), "the hairline at the right",
                         24);
        });
    });
}

// The selected item's square is the accent's container and a 3 x 20 bar of the accent stands at the rail's left edge,
// its right corners rounded; another item's square is the rail's surface.
TEST(Rail, SelectionIndicator)
{
    forEachLook([](const DesignTokens& t) {
        ShownRail shown(ordinaryRail());
        AnIconRail& rail = shown.rail;
        const QRect home = rail.itemRect(QStringLiteral("home"));
        const QRect workspace = rail.itemRect(QStringLiteral("ws1"));
        const qreal squareLeft = (t.metrics.rail - kRailSquare) / 2.0;
        forEachRatio(rail, t.layers.surface0, [&](const RenderedPicture& p) {
            expectColour(p.at(1.5, home.center().y()), t.colors.accentPrimary, "the bar");
            expectColour(p.at(1.5, home.center().y() - kRailIndicatorHeight / 2.0 - 1.5), t.layers.surface1,
                         "above the bar");
            expectColour(p.at(squareLeft + 2.5, home.center().y()),
                         an::ui::compositeOver(t.colors.accentContainer, t.layers.surface1), "the tonal square");
            expectColour(p.at(1.5, workspace.center().y()), t.layers.surface1, "no bar on another place");
            expectColour(p.at(squareLeft + 2.5, workspace.center().y()), t.layers.surface1, "no square on another");
        });

        // A selected task has the same bar.
        std::vector<RailItem> items = ordinaryRail();
        items[0].selected = false;
        items[3].selected = true;
        rail.setItems(items);
        const QRect selectedTask = rail.itemRect(QStringLiteral("t1"));
        expectColour(RenderedPicture::ofWidget(rail, 1.0, t.layers.surface0).at(1.5, selectedTask.center().y()),
                     t.colors.accentPrimary, "a task's bar");
    });
}

// A workspace carries its digit at its square's lower right: a 15 px badge of the fourth surface in the monospace,
// ringed by the rail's surface; a workspace in a window of its own carries the window glyph instead. The console
// carries the count of unseen warnings at its upper right as a warning pill.
TEST(Rail, WorkspaceBadges)
{
    forEachLook([](const DesignTokens& t) {
        std::vector<RailItem> items = ordinaryRail();
        items[1].badgeText = QStringLiteral("1");
        RailItem second =
            place(QStringLiteral("ws2"), QStringLiteral("images"), QStringLiteral("Workspace 2 - Masters"));
        second.badgeText = QStringLiteral("2");
        second.floating = true;
        items.insert(items.begin() + 2, second);
        items[items.size() - 2].badgeText = QStringLiteral("3");
        ShownRail shown(items);
        AnIconRail& rail = shown.rail;

        const QRect workspace = rail.itemRect(QStringLiteral("ws1"));
        const qreal squareRight = (t.metrics.rail + kRailSquare) / 2.0;
        const qreal squareBottom = QRectF(workspace).center().y() + kRailSquare / 2.0;
        const qreal badgeBottom = squareBottom + kRailBadgeOffset;
        const qreal badgeRight = squareRight + kRailBadgeOffset;
        forEachRatio(rail, t.layers.surface0, [&](const RenderedPicture& p) {
            expectColour(p.at(badgeRight - 2.5, badgeBottom - kRailBadge / 2.0), t.layers.surface4, "the digit badge");
        });
        const an::ui::testing::RecordedText digit = recordedText(rail, QStringLiteral("1"));
        ASSERT_EQ(digit.text, QStringLiteral("1"));
        expectColour(digit.colour, t.colors.textPrimary, "the digit", 0);
        EXPECT_EQ(an::tests::widgets::logicalPixels(digit.font), kRailBadgeFont);
        EXPECT_EQ(digit.font.families(), t.typography.monospace.families());
        EXPECT_TRUE(recordedText(rail, QStringLiteral("2")).text.isEmpty()) << "a floating workspace shows its glyph";

        const QRect console = rail.itemRect(QStringLiteral("console"));
        const qreal pillTop = QRectF(console).center().y() - kRailSquare / 2.0 - kRailPillOffset;
        const qreal pillRight = squareRight + kRailPillOffset;
        forEachRatio(rail, t.layers.surface0, [&](const RenderedPicture& p) {
            expectColour(p.at(pillRight - 3.0, pillTop + kRailBadge / 2.0), t.colors.statusWarning, "the pill");
        });
        const an::ui::testing::RecordedText count = recordedText(rail, QStringLiteral("3"));
        ASSERT_EQ(count.text, QStringLiteral("3"));
        expectColour(count.colour, t.layers.surface0, "the count", 0);
        EXPECT_EQ(count.font.weight(), QFont::Bold);
    });
}

// A running task's ring is the accent's track with the accent's arc from the top, clockwise, as far as it has come:
// none at 0 %, five twelfths at 42 %, the whole ring at 100 %. The ring is drawn only while the task runs.
TEST(Rail, TaskThumbnailArc)
{
    forEachLook([](const DesignTokens& t) {
        std::vector<RailItem> items = ordinaryRail();
        ShownRail shown(items);
        AnIconRail& rail = shown.rail;
        const QColor bg = t.layers.surface0;
        const QColor track = an::ui::compositeOver(t.colors.accentTrack, t.layers.surface1);
        const auto ringAt = [&](const QString& id, const qreal degrees, const qreal ratio = 1.0) {
            const QPointF point = ringPoint(rail.itemRect(id), degrees);
            return RenderedPicture::ofWidget(rail, ratio, bg).at(point.x(), point.y());
        };
        for (const qreal ratio : an::ui::testing::kRenderRatios)
        {
            SCOPED_TRACE(ratio);
            // Clear of the glyph's disc, which lies over the ring at the box's lower right.
            expectColour(ringAt(QStringLiteral("t2"), 80.0, ratio), t.colors.accentPrimary, "inside 42 %", 40);
            expectColour(ringAt(QStringLiteral("t2"), 200.0, ratio), track, "past 42 %", 40);
            expectColour(ringAt(QStringLiteral("t1"), 90.0, ratio), t.layers.surface1, "no ring when not running", 24);
        }
        items[4].progress = 0;
        rail.updateItem(items[4]);
        pumpFor(150);
        expectColour(ringAt(QStringLiteral("t2"), 20.0), track, "nothing done at 0 %", 40);
        items[4].progress = 100;
        rail.updateItem(items[4]);
        pumpFor(150);
        expectColour(ringAt(QStringLiteral("t2"), 300.0), t.colors.accentPrimary, "the whole ring at 100 %", 40);
    });
}

// Without progress the arc is a quarter that turns once in 1.2 s, read from the rail's clock; with the animations off
// it stays at the top.
TEST(Rail, TaskThumbnailTurningArc)
{
    std::chrono::milliseconds now{0};
    std::vector<RailItem> items = ordinaryRail();
    items[4].progress.reset();
    ShownRail shown(items);
    AnIconRail& rail = shown.rail;
    rail.setClock([&now] { return now; });
    an::ui::testing::ObservatoryStyleScope scope(an::ui::testing::observatoryLooks()[0]);
    const DesignTokens& t = an::ui::tokens();
    const QRect row = rail.itemRect(QStringLiteral("t2"));
    const QColor track = an::ui::compositeOver(t.colors.accentTrack, t.layers.surface1);
    const auto ringAt = [&](const qreal degrees) {
        const QPointF point = ringPoint(row, degrees);
        return RenderedPicture::ofWidget(rail, 1.0, t.layers.surface0).at(point.x(), point.y());
    };
    // Probed clear of the glyph's disc, which lies over the ring at the box's lower right.
    expectColour(ringAt(45.0), t.colors.accentPrimary, "the quarter from the top", 40);
    expectColour(ringAt(225.0), track, "past the quarter", 40);
    now = 600ms;
    expectColour(ringAt(225.0), t.colors.accentPrimary, "half a turn later", 40);
    expectColour(ringAt(45.0), track, "behind it", 40);
    EXPECT_TRUE(rail.isTurning());

    const bool animations = an::ui::AnimationUtils::animationsEnabled();
    an::ui::AnimationUtils::setAnimationsEnabled(false);
    rail.update();
    expectColour(ringAt(45.0), t.colors.accentPrimary, "still at the top without animations", 40);
    expectColour(ringAt(225.0), track, "not turned", 40);
    an::ui::AnimationUtils::setAnimationsEnabled(animations);
}

// A run that ended unseen draws its dot at the box's upper right - success with a check, a failure as a rounded square
// with a cross - over the rail's surface; a seen result and a run draw none.
TEST(Rail, TaskThumbnailResultDots)
{
    forEachLook([](const DesignTokens& t) {
        std::vector<RailItem> items = ordinaryRail();
        items[3].runPhase = RailRunPhase::Succeeded;
        items[3].unseenResult = true;
        items[4].runPhase = RailRunPhase::Failed;
        items[4].progress.reset();
        items[4].unseenResult = true;
        ShownRail shown(items);
        AnIconRail& rail = shown.rail;
        const auto dotAt = [&](const QString& id, const RenderedPicture& p) {
            const QRectF box = taskBox(rail.itemRect(id));
            const QPointF centre(box.right() + 1.0 - kTaskResultDot / 2.0, box.top() - 1.0 + kTaskResultDot / 2.0);
            return p.at(centre.x() - 4.5, centre.y());
        };
        forEachRatio(rail, t.layers.surface0, [&](const RenderedPicture& p) {
            expectColour(dotAt(QStringLiteral("t1"), p), t.colors.statusSuccess, "the success dot", 24);
            expectColour(dotAt(QStringLiteral("t2"), p), t.colors.statusError, "the failure dot", 24);
        });
        items[3].unseenResult = false;
        rail.updateItem(items[3]);
        const RenderedPicture seen = RenderedPicture::ofWidget(rail, 1.0, t.layers.surface0);
        EXPECT_FALSE(an::ui::testing::sameColour(dotAt(QStringLiteral("t1"), seen), t.colors.statusSuccess, 24))
            << "a seen result draws no dot";
    });
}

// A floating task's thumbnail is drawn at 45 % with the window glyph in place of the process's; a task with no target
// image shows the process's glyph on a tile of the third surface.
TEST(Rail, TaskThumbnailFloatingAndWithoutPicture)
{
    forEachLook([](const DesignTokens& t) {
        std::vector<RailItem> items = ordinaryRail();
        items[3].floating = true;
        items[4].thumbnail = QImage();
        items[4].runPhase = RailRunPhase::None;
        items[4].progress.reset();
        ShownRail shown(items);
        AnIconRail& rail = shown.rail;
        const QColor thumbnail(200, 60, 40);
        forEachRatio(rail, t.layers.surface0, [&](const RenderedPicture& p) {
            const QRectF floating = taskBox(rail.itemRect(QStringLiteral("t1")));
            expectColour(p.at(floating.left() + kTaskBox / 2.0 - 6.0, floating.top() + 8.0),
                         an::ui::testing::layered(thumbnail, kTaskFloatingOpacity, t.layers.surface1),
                         "the dimmed thumbnail", 6);
            const QRectF bare = taskBox(rail.itemRect(QStringLiteral("t2")));
            expectColour(p.at(bare.left() + 8.0, bare.top() + 8.0), t.layers.surface3, "the tile");
        });

        // The full picture when the task is attached again.
        items[3].floating = false;
        rail.updateItem(items[3]);
        const QRectF attached = taskBox(rail.itemRect(QStringLiteral("t1")));
        expectColour(RenderedPicture::ofWidget(rail, 1.0, t.layers.surface0)
                         .at(attached.left() + kTaskBox / 2.0 - 6.0, attached.top() + 8.0),
                     thumbnail, "the thumbnail whole");
    });
}

// A hundred progress updates in a second paint the rail at most ten times, and lay nothing out again.
TEST(Rail, ProgressRepaintsAreCoalesced)
{
    std::vector<RailItem> items = ordinaryRail();
    AnIconRail rail;
    rail.setItems(items);
    rail.resize(rail.sizeHint());
    rail.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&rail));
    pumpFor(200);
    const std::uint64_t paints = rail.paintCount();
    const std::uint64_t layouts = rail.layoutCount();
    ASSERT_GT(paints, 0u) << "the rail paints when shown";

    QElapsedTimer clock;
    clock.start();
    for (int update = 0; update < 100; ++update)
    {
        items[4].progress = update;
        rail.updateItem(items[4]);
        pumpFor(9);
    }
    const qint64 elapsed = clock.elapsed();
    const std::uint64_t painted = rail.paintCount() - paints;
    EXPECT_LE(painted, static_cast<std::uint64_t>((std::max)(qint64{10}, (elapsed + 99) / 100)))
        << painted << " paints in " << elapsed << " ms";
    EXPECT_LE(painted, 10u + static_cast<std::uint64_t>((std::max)(qint64{0}, elapsed - 1000) / 100));
    EXPECT_EQ(rail.layoutCount(), layouts) << "a progress update lays nothing out";
    pumpFor(200);
    EXPECT_GT(rail.paintCount(), paints) << "the last progress is painted";
    EXPECT_EQ(rail.items()[4].progress, 99);
}

// When the tasks do not fit, the task area ends with an item counting the hidden ones; the wheel scrolls the tasks; the
// item opens a list of every task, and choosing one tells its id.
TEST(Rail, OverflowItemListsHiddenTasks)
{
    std::vector<RailItem> items = ordinaryRail();
    items.erase(items.begin() + 3, items.begin() + 5);
    for (int index = 0; index < 8; ++index)
    {
        items.insert(items.end() - 2,
                     task(QStringLiteral("task%1").arg(index), QStringLiteral("Process %1 - image_%1").arg(index)));
    }
    // Room for three task rows.
    const int height = 2 * kRailPadding + 3 * kRailPlaceRow + kRailHairlineAbove + 1 + kRailHairlineBelow +
                       3 * kRailTaskRow + 2 * kRailPlaceRow + 8 * kRailGap + 6;
    ShownRail shown(items, height);
    AnIconRail& rail = shown.rail;
    EXPECT_EQ(rail.hiddenTaskCount(), 6);
    EXPECT_FALSE(rail.itemRect(QStringLiteral("task0")).isNull());
    EXPECT_FALSE(rail.itemRect(QStringLiteral("task1")).isNull());
    EXPECT_TRUE(rail.itemRect(QStringLiteral("task2")).isNull());
    const QRect overflow = rail.overflowRect();
    ASSERT_FALSE(overflow.isNull());
    EXPECT_EQ(overflow.height(), kRailTaskRow);
    EXPECT_EQ(overflow.top(), rail.itemRect(QStringLiteral("task1")).bottom() + 1 + kRailGap);
    EXPECT_EQ(recordedText(rail, QStringLiteral("+6")).text, QStringLiteral("+6"));

    QWheelEvent wheel(QPointF(overflow.center()), QPointF(rail.mapToGlobal(overflow.center())), QPoint(),
                      QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&rail, &wheel);
    EXPECT_TRUE(rail.itemRect(QStringLiteral("task0")).isNull()) << "the wheel scrolls the tasks";
    EXPECT_FALSE(rail.itemRect(QStringLiteral("task2")).isNull());

    QSignalSpy activated(&rail, &AnIconRail::itemActivated);
    QTest::mouseClick(&rail, Qt::LeftButton, Qt::NoModifier, overflow.center());
    QMenu* const list = rail.taskList();
    ASSERT_NE(list, nullptr);
    const QList<QAction*> entries = list->actions();
    ASSERT_EQ(entries.size(), 8);
    EXPECT_EQ(entries.at(5)->text(), QStringLiteral("Process 5 - image_5"));
    EXPECT_EQ(activated.count(), 0) << "the overflow item activates no task by itself";
    entries.at(5)->trigger();
    ASSERT_EQ(activated.count(), 1);
    EXPECT_EQ(activated.at(0).at(0).toString(), QStringLiteral("task5"));
}

// The rail is one stop: the focus lands on the selected item, Up and Down move it through every item, Enter and Space
// activate it and the menu key asks for its menu; a disabled item is passed over.
TEST(Rail, Keyboard)
{
    std::vector<RailItem> items = ordinaryRail();
    items[2].enabled = false;
    ShownRail shown(items);
    AnIconRail& rail = shown.rail;
    EXPECT_EQ(rail.focusPolicy(), Qt::StrongFocus);
    QSignalSpy activated(&rail, &AnIconRail::itemActivated);
    QSignalSpy menu(&rail, &AnIconRail::itemMenuRequested);

    QFocusEvent focusIn(QEvent::FocusIn, Qt::TabFocusReason);
    QApplication::sendEvent(&rail, &focusIn);
    EXPECT_EQ(rail.focusedItemId(), QStringLiteral("home"));
    QTest::keyClick(&rail, Qt::Key_Down);
    EXPECT_EQ(rail.focusedItemId(), QStringLiteral("ws1"));
    QTest::keyClick(&rail, Qt::Key_Down);
    EXPECT_EQ(rail.focusedItemId(), QStringLiteral("t1")) << "the disabled place is passed over";
    QTest::keyClick(&rail, Qt::Key_Return);
    QTest::keyClick(&rail, Qt::Key_Down);
    QTest::keyClick(&rail, Qt::Key_Space);
    ASSERT_EQ(activated.count(), 2);
    EXPECT_EQ(activated.at(0).at(0).toString(), QStringLiteral("t1"));
    EXPECT_EQ(activated.at(1).at(0).toString(), QStringLiteral("t2"));
    QTest::keyClick(&rail, Qt::Key_Menu);
    ASSERT_EQ(menu.count(), 1);
    EXPECT_EQ(menu.at(0).at(0).toString(), QStringLiteral("t2"));
    EXPECT_EQ(menu.at(0).at(1).toPoint(), rail.mapToGlobal(rail.itemRect(QStringLiteral("t2")).center()));
    QTest::keyClick(&rail, Qt::Key_End);
    EXPECT_EQ(rail.focusedItemId(), QStringLiteral("settings"));
    QTest::keyClick(&rail, Qt::Key_Down);
    EXPECT_EQ(rail.focusedItemId(), QStringLiteral("settings")) << "the last item is where Down stops";
    QTest::keyClick(&rail, Qt::Key_Home);
    EXPECT_EQ(rail.focusedItemId(), QStringLiteral("home"));
}

// An item's tooltip is its name, and a task's adds where its run stands. Hovered for 400 ms, the item shows it at its
// right, centred on its row; leaving the rail takes it away.
TEST(Rail, Tooltips)
{
    std::vector<RailItem> items = ordinaryRail();
    items[1].text = QStringLiteral("Workspace 2 - Masters");
    items[3].runPhase = RailRunPhase::Succeeded;
    items[3].unseenResult = true;
    ShownRail shown(items);
    AnIconRail& rail = shown.rail;
    EXPECT_EQ(rail.itemToolTip(QStringLiteral("home")), QStringLiteral("Home"));
    EXPECT_EQ(rail.itemToolTip(QStringLiteral("ws1")), QStringLiteral("Workspace 2 - Masters"));
    EXPECT_EQ(rail.itemToolTip(QStringLiteral("t2")),
              QStringLiteral("Non Local Bayes - stacked_3008x3004_Luminance_Caldwell_77 - applying 42 %"));
    EXPECT_EQ(rail.itemToolTip(QStringLiteral("t1")),
              QStringLiteral("Curves Transformation - M20_FINAL_PNG_05 - applied"));
    items[4].progress.reset();
    rail.updateItem(items[4]);
    EXPECT_EQ(rail.itemToolTip(QStringLiteral("t2")),
              QStringLiteral("Non Local Bayes - stacked_3008x3004_Luminance_Caldwell_77 - applying"));
    items[3].runPhase = RailRunPhase::Failed;
    items[3].floating = true;
    rail.updateItem(items[3]);
    EXPECT_EQ(rail.itemToolTip(QStringLiteral("t1")),
              QStringLiteral("Curves Transformation - M20_FINAL_PNG_05 - failed - in a window"));

    const QRect workspace = rail.itemRect(QStringLiteral("ws1"));
    sendMouse(rail, QEvent::MouseMove, workspace.center(), Qt::NoButton, Qt::NoButton);
    pumpFor(200);
    EXPECT_TRUE(rail.shownToolTipItem().isEmpty()) << "not before 400 ms";
    ASSERT_TRUE(pumpUntil([&rail] { return !rail.shownToolTipItem().isEmpty(); }, 1000));
    EXPECT_EQ(rail.shownToolTipItem(), QStringLiteral("ws1"));
    QWidget* const tip = rail.toolTipWindow();
    ASSERT_NE(tip, nullptr);
    EXPECT_TRUE(tip->isVisible());
    const QRect surface = tip->geometry().marginsRemoved(tip->contentsMargins());
    EXPECT_EQ(surface.left(), rail.mapToGlobal(QPoint(rail.width(), 0)).x() + kRailTipGap);
    EXPECT_EQ(surface.center().y(), rail.mapToGlobal(workspace.center()).y());
    EXPECT_EQ(surface.height(), kRailTipHeight);

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&rail, &leave);
    EXPECT_TRUE(rail.shownToolTipItem().isEmpty());
    EXPECT_FALSE(tip->isVisible());
}

// The menu of an item is asked for by a right click, or by a press held 500 ms - after which the release activates
// nothing - each with the global point it was asked at.
TEST(Rail, MenuRequest)
{
    ShownRail shown(ordinaryRail());
    AnIconRail& rail = shown.rail;
    QSignalSpy menu(&rail, &AnIconRail::itemMenuRequested);
    QSignalSpy activated(&rail, &AnIconRail::itemActivated);
    const QPoint workspace = rail.itemRect(QStringLiteral("ws1")).center();
    QContextMenuEvent context(QContextMenuEvent::Mouse, workspace, rail.mapToGlobal(workspace));
    QApplication::sendEvent(&rail, &context);
    ASSERT_EQ(menu.count(), 1);
    EXPECT_EQ(menu.at(0).at(0).toString(), QStringLiteral("ws1"));
    EXPECT_EQ(menu.at(0).at(1).toPoint(), rail.mapToGlobal(workspace));

    const QPoint taskPoint = rail.itemRect(QStringLiteral("t1")).center();
    sendMouse(rail, QEvent::MouseButtonPress, taskPoint, Qt::LeftButton, Qt::LeftButton);
    pumpFor(200);
    EXPECT_EQ(menu.count(), 1) << "not before 500 ms";
    ASSERT_TRUE(pumpUntil([&menu] { return menu.count() == 2; }, 1500));
    EXPECT_EQ(menu.at(1).at(0).toString(), QStringLiteral("t1"));
    EXPECT_EQ(menu.at(1).at(1).toPoint(), rail.mapToGlobal(taskPoint));
    sendMouse(rail, QEvent::MouseButtonRelease, taskPoint, Qt::LeftButton, Qt::NoButton);
    EXPECT_EQ(activated.count(), 0) << "a held press does not activate";

    QTest::mouseClick(&rail, Qt::LeftButton, Qt::NoModifier, taskPoint);
    ASSERT_EQ(activated.count(), 1) << "a click does";
    EXPECT_EQ(activated.at(0).at(0).toString(), QStringLiteral("t1"));
}

// A task is dragged once the pointer moves the platform's distance with the button down: started, moved and released
// with global points, a ghost of its thumbnail under the cursor meanwhile; Esc cancels and the release then tells
// nothing. A place is never dragged, and a drag activates nothing.
TEST(Rail, DragThresholdAndSignals)
{
    ShownRail shown(ordinaryRail());
    AnIconRail& rail = shown.rail;
    QSignalSpy started(&rail, &AnIconRail::itemDragStarted);
    QSignalSpy moved(&rail, &AnIconRail::itemDragMoved);
    QSignalSpy released(&rail, &AnIconRail::itemDragReleased);
    QSignalSpy cancelled(&rail, &AnIconRail::itemDragCancelled);
    QSignalSpy activated(&rail, &AnIconRail::itemActivated);
    const int distance = QApplication::startDragDistance();
    const QPoint press = rail.itemRect(QStringLiteral("t1")).center();

    sendMouse(rail, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    sendMouse(rail, QEvent::MouseMove, press + QPoint(distance - 1, 0), Qt::NoButton, Qt::LeftButton);
    EXPECT_EQ(started.count(), 0) << "under the platform's distance";
    EXPECT_FALSE(rail.isDragging());
    const QPoint far = press + QPoint(distance + 30, 12);
    sendMouse(rail, QEvent::MouseMove, far, Qt::NoButton, Qt::LeftButton);
    ASSERT_EQ(started.count(), 1);
    EXPECT_EQ(started.at(0).at(0).toString(), QStringLiteral("t1"));
    EXPECT_TRUE(rail.isDragging());
    ASSERT_GE(moved.count(), 1);
    EXPECT_EQ(moved.last().at(1).toPoint(), global(rail, far));
    QWidget* const ghost = rail.dragGhost();
    ASSERT_NE(ghost, nullptr);
    EXPECT_TRUE(ghost->isVisible());
    EXPECT_EQ(ghost->geometry().marginsRemoved(ghost->contentsMargins()).center(), global(rail, far));
    const QPoint further = far + QPoint(80, 40);
    sendMouse(rail, QEvent::MouseMove, further, Qt::NoButton, Qt::LeftButton);
    EXPECT_EQ(moved.last().at(1).toPoint(), global(rail, further));
    EXPECT_EQ(ghost->geometry().marginsRemoved(ghost->contentsMargins()).center(), global(rail, further));
    sendMouse(rail, QEvent::MouseButtonRelease, further, Qt::LeftButton, Qt::NoButton);
    ASSERT_EQ(released.count(), 1);
    EXPECT_EQ(released.at(0).at(0).toString(), QStringLiteral("t1"));
    EXPECT_EQ(released.at(0).at(1).toPoint(), global(rail, further));
    EXPECT_FALSE(rail.isDragging());
    EXPECT_FALSE(ghost->isVisible());

    // Esc cancels; the release that follows tells nothing.
    sendMouse(rail, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    sendMouse(rail, QEvent::MouseMove, far, Qt::NoButton, Qt::LeftButton);
    ASSERT_EQ(started.count(), 2);
    QTest::keyClick(&rail, Qt::Key_Escape);
    ASSERT_EQ(cancelled.count(), 1);
    EXPECT_EQ(cancelled.at(0).at(0).toString(), QStringLiteral("t1"));
    EXPECT_FALSE(ghost->isVisible());
    sendMouse(rail, QEvent::MouseButtonRelease, far, Qt::LeftButton, Qt::NoButton);
    EXPECT_EQ(released.count(), 1);

    // A drag the owner ends - the dock library's, which takes the release - clears the rail without a word.
    sendMouse(rail, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    sendMouse(rail, QEvent::MouseMove, far, Qt::NoButton, Qt::LeftButton);
    rail.endDrag();
    EXPECT_FALSE(rail.isDragging());
    EXPECT_EQ(cancelled.count(), 1);
    EXPECT_EQ(released.count(), 1);

    // A place is never dragged.
    const QPoint home = rail.itemRect(QStringLiteral("home")).center();
    sendMouse(rail, QEvent::MouseButtonPress, home, Qt::LeftButton, Qt::LeftButton);
    sendMouse(rail, QEvent::MouseMove, home + QPoint(distance + 30, 0), Qt::NoButton, Qt::LeftButton);
    sendMouse(rail, QEvent::MouseButtonRelease, home + QPoint(distance + 30, 0), Qt::LeftButton, Qt::NoButton);
    EXPECT_EQ(started.count(), 3);
    EXPECT_EQ(activated.count(), 0);
}

// Assistive technology reads a list whose items are named as their tooltips are, the selected one selected and a
// disabled one unavailable.
TEST(Rail, AccessibleNames)
{
    std::vector<RailItem> items = ordinaryRail();
    items[2].enabled = false;
    ShownRail shown(items);
    AnIconRail& rail = shown.rail;
    rail.setAccessibleName(QStringLiteral("Places and tasks"));
    QAccessibleInterface* const iface = QAccessible::queryAccessibleInterface(&rail);
    ASSERT_NE(iface, nullptr);
    EXPECT_EQ(iface->role(), QAccessible::List);
    EXPECT_EQ(iface->text(QAccessible::Name), QStringLiteral("Places and tasks"));
    ASSERT_EQ(iface->childCount(), static_cast<int>(items.size()));
    for (int index = 0; index < iface->childCount(); ++index)
    {
        QAccessibleInterface* const child = iface->child(index);
        ASSERT_NE(child, nullptr);
        SCOPED_TRACE(items[static_cast<std::size_t>(index)].id.toStdString());
        EXPECT_EQ(child->role(), QAccessible::ListItem);
        EXPECT_EQ(child->text(QAccessible::Name), rail.itemToolTip(items[static_cast<std::size_t>(index)].id));
        EXPECT_EQ(child->state().selected, items[static_cast<std::size_t>(index)].selected);
        EXPECT_EQ(child->state().disabled, !items[static_cast<std::size_t>(index)].enabled);
        EXPECT_EQ(child->parent(), iface);
        EXPECT_EQ(iface->indexOfChild(child), index);
        EXPECT_EQ(child->rect().center(),
                  rail.mapToGlobal(rail.itemRect(items[static_cast<std::size_t>(index)].id).center()));
    }
}

// Under Night the rail paints no green or blue: its colours are the tokens', its icons are drawn in them, and a task's
// picture is shown in its Night form once it is made.
TEST(Rail, NightShowsNoHue)
{
    an::ui::testing::ObservatoryStyleScope scope(an::ui::testing::observatoryLooks()[2]);
    scope.setNight(true);
    std::vector<RailItem> items = ordinaryRail();
    items[1].badgeText = QStringLiteral("1");
    items[3].runPhase = RailRunPhase::Succeeded;
    items[3].unseenResult = true;
    items[items.size() - 2].badgeText = QStringLiteral("3");
    ShownRail shown(items);
    AnIconRail& rail = shown.rail;
    (void)RenderedPicture::ofWidget(rail, 1.0, an::ui::tokens().layers.surface0);
    pumpFor(300);
    const RenderedPicture picture = RenderedPicture::ofWidget(rail, 2.0, an::ui::tokens().layers.surface0);
    const QImage& image = picture.image();
    int hued = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor pixel = image.pixelColor(x, y);
            hued += pixel.green() != 0 || pixel.blue() != 0 ? 1 : 0;
        }
    }
    EXPECT_EQ(hued, 0);
    scope.setNight(false);
}
