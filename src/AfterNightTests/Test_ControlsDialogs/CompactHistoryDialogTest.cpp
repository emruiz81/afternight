// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Compact history dialog: each image of the project with its steps and their size; the choice of the last steps
/// kept or only the current state; what it frees, as the plan of the compaction says, asked again when the choice
/// changes; that it removes the step files, rewrites the index and cannot be undone; and Compact, which compacts every
/// image as chosen and says what was freed.

#include "Test_ControlsDialogs/DialogsTestSupport.h"

#include "dialogs/CompactHistoryDialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTest>
#include <QtTest/QSignalSpy>

#include <gtest/gtest.h>

#include <map>
#include <vector>

namespace
{

using an::ui::CompactHistoryDialog;
using an::ui::CompactHistoryImage;
using an::ui::history::HistoryCompaction;
using an::ui::history::HistoryCompactionResult;

constexpr std::uint64_t kMegabyte = 1000u * 1000u;

/// Two images of a project, one with histories set aside.
std::vector<CompactHistoryImage> twoImages()
{
    return {
        CompactHistoryImage{QStringLiteral("a1"), QStringLiteral("Carina_RGB.fit"), 12, 1400 * kMegabyte, 0},
        CompactHistoryImage{QStringLiteral("b2"), QStringLiteral("Carina_L.fit"), 9, 600 * kMegabyte, 200 * kMegabyte}};
}

/// What a compaction of a fixture image would free: a step of each image is 100 MB; the current state alone frees
/// all but one, the last N all but N; the histories set aside are freed when asked.
HistoryCompactionResult planned(const CompactHistoryImage& image, const HistoryCompaction& what)
{
    const std::size_t kept = what.keep == HistoryCompaction::Keep::CurrentState ? 1u : what.steps;
    HistoryCompactionResult result;
    result.ok = true;
    result.stepsRemoved = image.steps > kept ? image.steps - kept : 0u;
    result.bytesFreed = result.stepsRemoved * 100u * kMegabyte + (what.removeDetached ? image.detachedBytes : 0u);
    return result;
}

/// The plan and the compaction a case gives the dialog: each answers at once, as a worker that landed would, and
/// records what it was asked.
struct Operations
{
    std::vector<std::pair<QString, HistoryCompaction>> plans;
    std::vector<std::pair<QString, HistoryCompaction>> compactions;

    CompactHistoryDialog::Operation plan()
    {
        return [this](const QString& imageId, const HistoryCompaction& what, CompactHistoryDialog::Done done) {
            plans.emplace_back(imageId, what);
            for (const CompactHistoryImage& image : twoImages())
            {
                if (image.id == imageId)
                {
                    done(planned(image, what));
                }
            }
        };
    }

    CompactHistoryDialog::Operation compact()
    {
        return [this](const QString& imageId, const HistoryCompaction& what, CompactHistoryDialog::Done done) {
            compactions.emplace_back(imageId, what);
            for (const CompactHistoryImage& image : twoImages())
            {
                if (image.id == imageId)
                {
                    done(planned(image, what));
                }
            }
        };
    }
};

QString gigabytes(const double value)
{
    return QLocale().toString(value, 'f', 1) + QStringLiteral(" GB");
}

} // namespace

// Each image is listed with its steps and their size, the histories set aside beside them.
TEST(CompactHistoryDialog, ListsEachImageWithItsStepsAndTheirSize)
{
    Operations operations;
    CompactHistoryDialog dialog(twoImages(), operations.plan(), operations.compact());
    const QStringList rows = dialog.imageRows();
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0], QStringLiteral("Carina_RGB.fit · 12 steps · ") + gigabytes(1.4));
    EXPECT_EQ(rows[1], QStringLiteral("Carina_L.fit · 9 steps · ") + gigabytes(0.6) + QStringLiteral(" · ") +
                           gigabytes(0.2) + QStringLiteral(" set aside"));
    ASSERT_NE(dialog.removeSetAside(), nullptr);
    EXPECT_TRUE(dialog.removeSetAside()->isVisibleTo(&dialog)) << "histories set aside are not offered";
}

// What it frees is the plan's for the choice, asked again when the choice changes; the dialog says that it removes the
// step files, rewrites the index and cannot be undone.
TEST(CompactHistoryDialog, SaysWhatItFreesForTheChoiceAndThatItCannotBeUndone)
{
    Operations operations;
    CompactHistoryDialog dialog(twoImages(), operations.plan(), operations.compact());
    dialog.show();
    QCoreApplication::processEvents();
    ASSERT_NE(dialog.keepLastSteps(), nullptr);
    ASSERT_NE(dialog.keepCurrentState(), nullptr);
    EXPECT_TRUE(dialog.keepLastSteps()->isChecked()) << "the gentler choice is not the first";

    dialog.stepsToKeep()->setValue(5);
    QCoreApplication::processEvents();
    // 12 - 5 and 9 - 5 steps of 100 MB.
    EXPECT_EQ(dialog.plannedBytes(), 11u * 100u * kMegabyte);
    EXPECT_TRUE(dialog.freesText().contains(gigabytes(1.1))) << dialog.freesText().toStdString();

    dialog.keepCurrentState()->setChecked(true);
    QCoreApplication::processEvents();
    EXPECT_EQ(dialog.plannedBytes(), 19u * 100u * kMegabyte);
    dialog.removeSetAside()->setChecked(true);
    QCoreApplication::processEvents();
    EXPECT_EQ(dialog.plannedBytes(), 19u * 100u * kMegabyte + 200u * kMegabyte);
    ASSERT_FALSE(operations.plans.empty());
    EXPECT_EQ(operations.plans.back().second.keep, HistoryCompaction::Keep::CurrentState);
    EXPECT_TRUE(operations.plans.back().second.removeDetached);

    const QString warning = dialog.warningText();
    EXPECT_TRUE(warning.contains(QStringLiteral("removes the step files"))) << warning.toStdString();
    EXPECT_TRUE(warning.contains(QStringLiteral("rewrites"))) << warning.toStdString();
    EXPECT_TRUE(warning.contains(QStringLiteral("cannot be undone"))) << warning.toStdString();
}

// Compact compacts every image as chosen, and the dialog closes saying what was freed.
TEST(CompactHistoryDialog, CompactCompactsEveryImageAsChosenAndSaysWhatWasFreed)
{
    Operations operations;
    CompactHistoryDialog dialog(twoImages(), operations.plan(), operations.compact());
    dialog.show();
    QCoreApplication::processEvents();
    dialog.stepsToKeep()->setValue(3);
    QCoreApplication::processEvents();
    QSignalSpy compacted(&dialog, &CompactHistoryDialog::compacted);
    ASSERT_NE(dialog.compactButton(), nullptr);
    QTest::mouseClick(dialog.compactButton(), Qt::LeftButton);
    QCoreApplication::processEvents();
    ASSERT_EQ(operations.compactions.size(), 2u);
    for (const auto& [imageId, what] : operations.compactions)
    {
        EXPECT_EQ(what.keep, HistoryCompaction::Keep::LastSteps) << imageId.toStdString();
        EXPECT_EQ(what.steps, 3u);
        EXPECT_FALSE(what.removeDetached);
    }
    ASSERT_EQ(compacted.count(), 1);
    EXPECT_EQ(compacted.front().front().toULongLong(), (9u + 6u) * 100u * kMegabyte);
    EXPECT_EQ(dialog.result(), QDialog::Accepted);
}
