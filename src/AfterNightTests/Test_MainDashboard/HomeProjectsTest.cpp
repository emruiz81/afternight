// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Home's project parts: the Projects row - the four projects used last as cards, each with its name, its object, its
/// folder cut at its start, its counts, its size and when it was opened, and "from an integration job" where it was
/// made by one, then the Open project... tile - and the Continue row's last-project card beside the featured image,
/// with its counts and Open project. What they say is given them: the shell reads it from each project's folder.

#include "Test_MainDashboard/DashboardApplication.h"

#include "components/AnTag.h"
#include "controls/HomeContinueRow.h"
#include "controls/HomeParts.h"
#include "controls/HomeProjectsRow.h"
#include "widgets/FeaturedFileCard.h"
#include "theme/ComponentParts.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QLayout>
#include <QLocale>
#include <QTime>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace
{

using an::ui::HomeContinueRow;
using an::ui::HomeProjectsRow;
using an::ui::HomeProjectSummary;

/// A project as the shell reads it from its folder.
HomeProjectSummary project(const QString& name, const int images, const bool fromJob = false)
{
    HomeProjectSummary summary;
    summary.folder = QStringLiteral("D:/Astrofotografia/Projects/") + name;
    summary.name = name;
    summary.object = name == QStringLiteral("Carina Nebula") ? QStringLiteral("NGC 3372") : QString();
    summary.objectSource = summary.object.isEmpty() ? QString() : QStringLiteral("the OBJECT card");
    summary.images = images;
    summary.integrations = fromJob ? 2 : 0;
    summary.steps = 41;
    summary.workspaces = 2;
    summary.bytes = qint64{71} * 1000 * 1000 * 1000 + qint64{400} * 1000 * 1000;
    summary.lastOpened = QDateTime(QDate(2026, 11, 6), QTime(4, 17));
    summary.fromIntegrationJob = fromJob;
    summary.present = true;
    return summary;
}

/// A size in gigabytes as the person's locale writes it: "71.4 GB", "71,4 GB".
QString gigabytes(const double value)
{
    return QLocale().toString(value, 'f', 1) + QStringLiteral(" GB");
}

void showAndSettle(QWidget& widget, const QSize& size)
{
    widget.resize(size);
    widget.show();
    QCoreApplication::processEvents();
    QTest::qWait(20);
}

} // namespace

// The four projects used last as cards, in their order, then the Open project... tile; each card says what its project
// holds, and the one an integration job made says so.
TEST(HomeProjectsRow, ShowsTheFourMostRecentThenTheOpenProjectTile)
{
    DashboardApplication::instance()->resetForTest();
    HomeProjectsRow row;
    row.setProjects({project(QStringLiteral("Carina Nebula"), 6, true), project(QStringLiteral("M 83 LRGB"), 4),
                     project(QStringLiteral("Caldwell 77"), 5, true), project(QStringLiteral("Saturn 2026-10"), 2),
                     project(QStringLiteral("Orion 2025"), 9)});
    showAndSettle(row, QSize(1400, 120));

    const std::vector<an::ui::HomeProjectCard*> cards = row.cards();
    ASSERT_EQ(cards.size(), 4u) << "the row shows four projects";
    EXPECT_EQ(cards[0]->summary().name, QStringLiteral("Carina Nebula"));
    EXPECT_EQ(cards[3]->summary().name, QStringLiteral("Saturn 2026-10"));
    ASSERT_NE(row.openProjectTile(), nullptr);
    EXPECT_TRUE(row.openProjectTile()->isVisible());
    EXPECT_GT(row.openProjectTile()->geometry().x(), cards[3]->geometry().x()) << "the tile is not after the cards";
    EXPECT_EQ(row.openProjectTile()->text(), QStringLiteral("Open project..."));

    EXPECT_EQ(cards[0]->countsText(), QStringLiteral("6 images \u00b7 2 integrations \u00b7 41 steps"));
    EXPECT_EQ(cards[1]->countsText(), QStringLiteral("4 images \u00b7 41 steps"));
    EXPECT_EQ(cards[0]->footText(), gigabytes(71.4) + QStringLiteral(" \u00b7 opened 2026-11-06"));
    EXPECT_EQ(cards[0]->folderText(), QStringLiteral("D:/Astrofotografia/Projects/Carina Nebula"));
    ASSERT_NE(cards[0]->objectTag(), nullptr);
    EXPECT_TRUE(cards[0]->objectTag()->isVisible());
    EXPECT_EQ(cards[0]->objectTag()->text(), QStringLiteral("NGC 3372"));
    EXPECT_FALSE(cards[1]->objectTag()->isVisible()) << "a project with no object shows an object";
    ASSERT_NE(cards[0]->originTag(), nullptr);
    EXPECT_TRUE(cards[0]->originTag()->isVisible());
    EXPECT_EQ(cards[0]->originTag()->text(), QStringLiteral("from an integration job"));
    EXPECT_FALSE(cards[1]->originTag()->isVisible());
}

// A card opens its project; the tile asks for another; with no project used last the row has only the tile.
TEST(HomeProjectsRow, ACardOpensItsProjectAndTheTileAsksForAnother)
{
    DashboardApplication::instance()->resetForTest();
    HomeProjectsRow row;
    row.setProjects({project(QStringLiteral("Carina Nebula"), 6), project(QStringLiteral("M 83 LRGB"), 4)});
    showAndSettle(row, QSize(1400, 120));
    QSignalSpy opened(&row, &HomeProjectsRow::openProjectRequested);
    QSignalSpy other(&row, &HomeProjectsRow::openOtherProjectRequested);
    ASSERT_EQ(row.cards().size(), 2u);
    QTest::mouseClick(row.cards()[1], Qt::LeftButton);
    ASSERT_EQ(opened.count(), 1);
    EXPECT_EQ(opened.front().front().toString(), QStringLiteral("D:/Astrofotografia/Projects/M 83 LRGB"));
    QTest::mouseClick(row.openProjectTile(), Qt::LeftButton);
    EXPECT_EQ(other.count(), 1);

    row.setProjects({});
    QCoreApplication::processEvents();
    EXPECT_TRUE(row.cards().empty());
    EXPECT_TRUE(row.openProjectTile()->isVisible());
}

// The Continue row shows the project used last between the featured image and the last sequence, with its counts and
// Open project, which opens it; with none it is not shown.
TEST(HomeContinueRow, TheLastProjectIsBesideTheFeaturedImageAndOpened)
{
    DashboardApplication::instance()->resetForTest();
    HomeContinueRow row;
    row.setLastProject(project(QStringLiteral("Carina Nebula"), 6, true));
    showAndSettle(row, QSize(1400, 240));
    ASSERT_NE(row.lastProjectCard(), nullptr);
    EXPECT_TRUE(row.lastProjectCard()->isVisible());
    EXPECT_EQ(row.lastProjectName(), QStringLiteral("Carina Nebula"));
    EXPECT_EQ(row.lastProjectCountsText(), QStringLiteral("6 images \u00b7 2 integrations \u00b7 41 history steps"));
    EXPECT_EQ(row.lastProjectCard()->width(), an::ui::parts::kHomeSideCardWidth);
    // Between the featured image and the last sequence.
    ASSERT_NE(row.layout(), nullptr);
    EXPECT_EQ(row.layout()->indexOf(row.lastProjectCard()), row.layout()->indexOf(row.featuredCard()) + 1);
    EXPECT_EQ(row.layout()->indexOf(row.sequenceCard()), row.layout()->indexOf(row.lastProjectCard()) + 1);

    QSignalSpy opened(&row, &HomeContinueRow::openProjectRequested);
    ASSERT_NE(row.openProjectButton(), nullptr);
    EXPECT_EQ(row.openProjectButton()->text(), QStringLiteral("Open project"));
    QTest::mouseClick(row.openProjectButton(), Qt::LeftButton);
    ASSERT_EQ(opened.count(), 1);
    EXPECT_EQ(opened.front().front().toString(), QStringLiteral("D:/Astrofotografia/Projects/Carina Nebula"));

    row.setLastProject(std::nullopt);
    QCoreApplication::processEvents();
    EXPECT_FALSE(row.lastProjectCard()->isVisible());
}

// What a project's counts and size read as.
TEST(HomeProjectsRow, TheCountsAndTheSizeReadAsTheDesignWritesThem)
{
    HomeProjectSummary summary = project(QStringLiteral("Saturn 2026-10"), 2);
    summary.sequences = 3;
    summary.integrations = 1;
    summary.steps = 6;
    EXPECT_EQ(an::ui::homeProjectCountsText(summary, false),
              QStringLiteral("2 images \u00b7 3 sequences \u00b7 1 integration \u00b7 6 steps"));
    EXPECT_EQ(an::ui::homeProjectCountsText(summary, true),
              QStringLiteral("2 images \u00b7 1 integration \u00b7 6 history steps"));
    summary.images = 1;
    summary.sequences = 0;
    summary.steps = 1;
    EXPECT_EQ(an::ui::homeProjectCountsText(summary, false),
              QStringLiteral("1 image \u00b7 1 integration \u00b7 1 step"));
    EXPECT_EQ(an::ui::homeSizeText(qint64{8} * 1000 * 1000 * 1000 + qint64{900} * 1000 * 1000), gigabytes(8.9));
    EXPECT_EQ(an::ui::homeSizeText(qint64{512} * 1000 * 1000), gigabytes(0.5));
    EXPECT_EQ(an::ui::homeSizeText(-1), QString());
}
