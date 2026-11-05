// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "processes/ImageIntegration/dialogs/BlinkComparatorDialog.h"
#include "processes/ImageIntegration/model/FrameId.h"

namespace
{

QString makeTempFile(QTemporaryDir& dir, const QString& fileName)
{
    const QString path = dir.filePath(fileName);
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    file.close();
    return path;
}

} // namespace

TEST(BlinkComparatorResult, ExclusionsRemainReadableAfterEscapeClose)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString fileA = makeTempFile(dir, QStringLiteral("light_a.fit"));
    const QString fileB = makeTempFile(dir, QStringLiteral("light_b.fit"));

    BlinkComparatorDialog dialog({fileA, fileB}, nullptr, false);

    QTest::keyClick(&dialog, Qt::Key_X);
    QTest::keyClick(&dialog, Qt::Key_Escape);

    const auto result = dialog.comparisonResult();
    ASSERT_EQ(result.excludedPaths.size(), 1);
    EXPECT_EQ(result.excludedPaths.first(), FrameId(fileA).canonicalPath());
}

TEST(BlinkComparatorResult, ResultTracksCurrentCanonicalPathAfterSort)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString alpha = makeTempFile(dir, QStringLiteral("alpha.fit"));
    const QString beta = makeTempFile(dir, QStringLiteral("beta.fit"));

    BlinkComparatorDialog dialog({beta, alpha}, nullptr, false);

    dialog.sortBy(BlinkComparatorDialog::SortCriteria::Filename, Qt::AscendingOrder);
    EXPECT_EQ(dialog.currentIndex(), 1);
    dialog.excludeCurrentFrame();
    dialog.close();

    const auto result = dialog.comparisonResult();
    ASSERT_EQ(result.excludedPaths.size(), 1);
    EXPECT_EQ(result.excludedPaths.first(), FrameId(beta).canonicalPath());
}

TEST(BlinkComparatorResult, ExclusionsAndRatingsRemainAttachedToCanonicalPathAfterSort)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString fileA = makeTempFile(dir, QStringLiteral("alpha.fit"));
    const QString fileB = makeTempFile(dir, QStringLiteral("beta.fit"));

    BlinkComparatorDialog dialog({fileA, fileB}, nullptr, false);

    QTest::keyClick(&dialog, Qt::Key_X);
    QTest::keyClick(&dialog, Qt::Key_7);

    dialog.sortBy(BlinkComparatorDialog::SortCriteria::Filename, Qt::DescendingOrder);

    EXPECT_EQ(dialog.currentIndex(), 1);
    EXPECT_FALSE(dialog.isExcluded(0));
    EXPECT_EQ(dialog.ratingFor(0), 0);
    EXPECT_TRUE(dialog.isExcluded(dialog.currentIndex()));
    EXPECT_EQ(dialog.ratingFor(dialog.currentIndex()), 7);

    const auto result = dialog.comparisonResult();
    ASSERT_EQ(result.excludedPaths.size(), 1);
    EXPECT_EQ(result.excludedPaths.first(), FrameId(fileA).canonicalPath());
}

TEST(BlinkComparatorResult, UnsupportedSortCriteriaAreHiddenAndDoNotReorder)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString alpha = makeTempFile(dir, QStringLiteral("alpha.fit"));
    const QString beta = makeTempFile(dir, QStringLiteral("beta.fit"));

    BlinkComparatorDialog dialog({beta, alpha}, nullptr, false);

    EXPECT_TRUE(dialog.supportsSortCriteria(BlinkComparatorDialog::SortCriteria::Filename));
    EXPECT_FALSE(dialog.supportsSortCriteria(BlinkComparatorDialog::SortCriteria::Quality));
    EXPECT_FALSE(dialog.supportsSortCriteria(BlinkComparatorDialog::SortCriteria::FWHM));
    EXPECT_FALSE(dialog.supportsSortCriteria(BlinkComparatorDialog::SortCriteria::Stars));

    dialog.sortBy(BlinkComparatorDialog::SortCriteria::Quality, Qt::AscendingOrder);
    dialog.excludeCurrentFrame();

    const auto result = dialog.comparisonResult();
    ASSERT_EQ(result.excludedPaths.size(), 1);
    EXPECT_EQ(result.excludedPaths.first(), FrameId(beta).canonicalPath());
}
