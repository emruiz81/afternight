// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A stage list: rows of a status mark, a title and one line of state, with an optional progress under the line; the
// marks joined by a line. The mark is a ring for a stage to come, the accent while it runs, success with a check when
// done, the warning with an exclamation mark, the error with a cross: never colour alone. The selected row is the
// accent's container. The keyboard moves the selection with Up and Down; the pointer selects a row by a click; a new
// selection is told by the row's id. Assistive technology reads a list of rows, each named by its title, its state and
// its line.

#include "RenderTestSupport.h"

#include "components/AnStageList.h"
#include "theme/ComponentParts.h"

#include <QAbstractItemModel>
#include <QFontMetricsF>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <optional>
#include <vector>

namespace
{

using an::tests::widgets::expectColour;
using an::tests::widgets::forEachLook;
using an::tests::widgets::forEachRatio;
using an::tests::widgets::logicalPixels;
using an::tests::widgets::recordedText;
using an::tests::widgets::RenderedPicture;
using an::ui::AnStageList;
using an::ui::DesignTokens;
using an::ui::StageRow;
using an::ui::StageStatus;
using namespace an::ui::parts;

std::vector<StageRow> runSheet()
{
    return {
        StageRow{QStringLiteral("frames"), QStringLiteral("Frames"), QStringLiteral("718 frames - 11 groups"),
                 StageStatus::Done, std::nullopt},
        StageRow{QStringLiteral("calibration"), QStringLiteral("Calibration"), QStringLiteral("15 masters"),
                 StageStatus::Warning, std::nullopt},
        StageRow{QStringLiteral("registration"), QStringLiteral("Registration"), QStringLiteral("running - 412 / 718"),
                 StageStatus::Running, 57},
        StageRow{QStringLiteral("stacking"), QStringLiteral("Stacking"), QStringLiteral("4 groups"),
                 StageStatus::Pending, std::nullopt},
        StageRow{QStringLiteral("drizzle"), QStringLiteral("Drizzle"), QStringLiteral("no output"), StageStatus::Failed,
                 std::nullopt},
    };
}

int lineHeight(const QFont& font)
{
    return static_cast<int>(std::ceil(QFontMetricsF(font).height()));
}

/// The centre of a row's status mark, in the list's viewport.
QPointF markCentre(const AnStageList& list, const int row)
{
    const QRect rect = list.visualRect(list.model()->index(row, 0));
    return QPointF(rect.left() + kStageRowPaddingHorizontal + kStageMarkBox / 2.0,
                   rect.top() + kStageRowPaddingVertical + kStageMarkBox / 2.0);
}

} // namespace

// A row is its padding, its title and its line in their fonts, and the gap under it; a progress adds its gap and its
// bar. The rows are as the list was given them, in order, and a row can be changed by its id.
TEST(AnStageList, RowsHoldTheirPartsAndFollowTheDensity)
{
    forEachLook([](const DesignTokens& t) {
        AnStageList list;
        an::tests::widgets::showOffScreen(list);
        list.setRows(runSheet());
        ASSERT_EQ(list.rowCount(), 5);
        EXPECT_EQ(list.rows().at(2).title, QStringLiteral("Registration"));
        list.resize(240, list.sizeHint().height());
        const an::ui::testing::RecordedText title = recordedText(*list.viewport(), QStringLiteral("Calibration"));
        const an::ui::testing::RecordedText line = recordedText(*list.viewport(), QStringLiteral("15 masters"));
        ASSERT_EQ(title.text, QStringLiteral("Calibration"));
        ASSERT_EQ(line.text, QStringLiteral("15 masters"));
        EXPECT_EQ(logicalPixels(title.font), t.metrics.fontBody);
        EXPECT_EQ(title.font.weight(), QFont::DemiBold);
        EXPECT_EQ(logicalPixels(line.font), t.metrics.fontCaption);
        EXPECT_EQ(line.font.families(), t.typography.monospace.families());
        expectColour(title.colour, t.colors.textPrimary, "a title", 0);
        expectColour(line.colour, t.colors.textSecondary, "a line", 0);

        const int plain =
            2 * kStageRowPaddingVertical + lineHeight(title.font) + lineHeight(line.font) + kStageRowSpacing;
        EXPECT_EQ(list.visualRect(list.model()->index(1, 0)).height(), plain);
        EXPECT_EQ(list.visualRect(list.model()->index(2, 0)).height(), plain + kStageProgressGap + kStageProgress);
        EXPECT_GE(list.sizeHint().height(), 4 * plain + plain + kStageProgressGap + kStageProgress);

        StageRow registration = list.rows().at(2);
        registration.status = StageStatus::Done;
        registration.progress.reset();
        registration.line = QStringLiteral("713 registered");
        list.updateRow(registration);
        EXPECT_EQ(list.rows().at(2).status, StageStatus::Done);
        EXPECT_EQ(list.visualRect(list.model()->index(2, 0)).height(), plain);
        EXPECT_EQ(recordedText(*list.viewport(), QStringLiteral("713 registered")).text,
                  QStringLiteral("713 registered"));
    });
}

// Each status draws its mark - the ring of a stage to come, the accent of a run, success, warning and error with their
// glyphs - and the selected row is the container with its texts in the container's text.
TEST(AnStageList, PaintsTheMarksAndTheSelectedRow)
{
    forEachLook([](const DesignTokens& t) {
        const QColor bg = t.layers.surface1;
        AnStageList list;
        an::tests::widgets::showOffScreen(list);
        list.setRows(runSheet());
        list.resize(240, list.sizeHint().height());
        list.setSelectedId(QStringLiteral("stacking"));
        const QRect selected = list.visualRect(list.model()->index(3, 0));
        forEachRatio(*list.viewport(), bg, [&](const RenderedPicture& picture) {
            // A filled mark is read off its centre's side, clear of its glyph; a ring at its outline.
            const auto markEdge = [&](const int row) {
                const QPointF centre = markCentre(list, row);
                return picture.at(centre.x() - kStageMark / 2.0 + 1.0, centre.y());
            };
            expectColour(markEdge(0), t.colors.statusSuccess, "done", 24);
            expectColour(markEdge(1), t.colors.statusWarning, "a warning", 24);
            expectColour(markEdge(2), t.colors.accentPrimary, "running", 24);
            expectColour(markEdge(3), t.colors.textTertiary, "to come", 32);
            expectColour(markEdge(4), t.colors.statusError, "failed", 24);
            expectColour(picture.at(selected.right() - 12, selected.top() + 2),
                         an::ui::compositeOver(t.colors.accentContainer, bg), "the selected row");
            const QRect other = list.visualRect(list.model()->index(1, 0));
            expectColour(picture.at(other.right() - 12, other.top() + 2), bg, "a row not selected");
        });
        expectColour(recordedText(*list.viewport(), QStringLiteral("Stacking")).colour, t.colors.textOnAccentContainer,
                     "the selected title", 0);
        expectColour(recordedText(*list.viewport(), QStringLiteral("4 groups")).colour, t.colors.textOnAccentContainer,
                     "the selected line", 0);

        // The run's progress: the accent over its track, as far as it has come.
        const QRect running = list.visualRect(list.model()->index(2, 0));
        const qreal barY = running.bottom() + 1 - kStageRowSpacing - kStageRowPaddingVertical - kStageProgress / 2.0;
        const qreal barLeft = running.left() + kStageRowPaddingHorizontal + kStageMarkBox + kStageRowGap;
        const qreal barRight = running.right() + 1 - kStageRowPaddingHorizontal;
        const RenderedPicture picture = RenderedPicture::ofWidget(*list.viewport(), 1.0, bg);
        expectColour(picture.at(barLeft + (barRight - barLeft) * 0.2, barY), t.colors.accentPrimary, "the progress");
        expectColour(picture.at(barLeft + (barRight - barLeft) * 0.9, barY),
                     an::ui::compositeOver(t.colors.accentTrack, bg), "the track");
    });
}

// A click selects a row and tells its id; Up and Down move the selection and tell each new row; setting the selection
// by id tells it too.
TEST(AnStageList, SelectsByThePointerAndTheKeyboard)
{
    AnStageList list;
    an::tests::widgets::showOffScreen(list);
    list.setRows(runSheet());
    list.resize(240, list.sizeHint().height());
    EXPECT_TRUE(list.selectedId().isEmpty());
    QSignalSpy selected(&list, &AnStageList::selectedStageChanged);

    QTest::mouseClick(list.viewport(), Qt::LeftButton, Qt::NoModifier,
                      list.visualRect(list.model()->index(1, 0)).center());
    EXPECT_EQ(list.selectedId(), QStringLiteral("calibration"));
    QTest::keyClick(&list, Qt::Key_Down);
    EXPECT_EQ(list.selectedId(), QStringLiteral("registration"));
    QTest::keyClick(&list, Qt::Key_Up);
    QTest::keyClick(&list, Qt::Key_Up);
    EXPECT_EQ(list.selectedId(), QStringLiteral("frames"));
    list.setSelectedId(QStringLiteral("drizzle"));
    ASSERT_EQ(selected.count(), 5);
    EXPECT_EQ(selected.at(0).at(0).toString(), QStringLiteral("calibration"));
    EXPECT_EQ(selected.at(4).at(0).toString(), QStringLiteral("drizzle"));

    list.setRows(runSheet());
    EXPECT_EQ(list.selectedId(), QStringLiteral("drizzle")) << "rows given again keep the selected stage";
}

// Assistive technology reads a list whose rows are named by the title, the state and the line.
TEST(AnStageList, IsAListOfRowsNamedByTheirState)
{
    AnStageList list;
    an::tests::widgets::showOffScreen(list);
    list.setRows(runSheet());
    list.resize(240, list.sizeHint().height());
    EXPECT_EQ(an::tests::widgets::accessibleRole(list), QAccessible::List);
    QAccessibleInterface* const iface = QAccessible::queryAccessibleInterface(&list);
    ASSERT_NE(iface, nullptr);
    QAccessibleTableInterface* const table = iface->tableInterface();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 5);
    QAccessibleInterface* const row = table->cellAt(2, 0);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->role(), QAccessible::ListItem);
    EXPECT_EQ(row->text(QAccessible::Name), QStringLiteral("Registration, running 57 %, running - 412 / 718"));
    EXPECT_EQ(table->cellAt(0, 0)->text(QAccessible::Name), QStringLiteral("Frames, done, 718 frames - 11 groups"));
    EXPECT_EQ(table->cellAt(1, 0)->text(QAccessible::Name), QStringLiteral("Calibration, warning, 15 masters"));
    EXPECT_EQ(table->cellAt(3, 0)->text(QAccessible::Name), QStringLiteral("Stacking, to come, 4 groups"));
    EXPECT_EQ(table->cellAt(4, 0)->text(QAccessible::Name), QStringLiteral("Drizzle, failed, no output"));
}
