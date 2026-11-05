// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QFileDialog>
#include <QObject>
#include <QString>

#include <deque>
#include <string>

#include "app/FileDialogUtils.h"
#include "app/FileManager.h"
#include "app/FileWorkflowRunner.h"

TEST(AfterNightAppFileSuite, UpdateRecentFileListAddsNewFileToFront)
{
    std::deque<std::string> recent = {"b.fit", "c.fit"};
    const bool changed = an::ui::updateRecentFileList(recent, "a.fit", 5);

    EXPECT_TRUE(changed);
    ASSERT_EQ(recent.size(), 3u);
    EXPECT_EQ(recent.front(), "a.fit");
}

TEST(AfterNightAppFileSuite, UpdateRecentFileListDeduplicatesExistingEntry)
{
    std::deque<std::string> recent = {"a.fit", "b.fit", "c.fit"};
    const bool changed = an::ui::updateRecentFileList(recent, "c.fit", 5);

    EXPECT_TRUE(changed);
    ASSERT_EQ(recent.size(), 3u);
    EXPECT_EQ(recent.front(), "c.fit");
    EXPECT_EQ(recent.back(), "b.fit");
}

TEST(AfterNightAppFileSuite, UpdateRecentFileListSkipsEmptyInput)
{
    std::deque<std::string> recent = {"a.fit"};
    const bool changed = an::ui::updateRecentFileList(recent, "", 5);

    EXPECT_FALSE(changed);
    ASSERT_EQ(recent.size(), 1u);
    EXPECT_EQ(recent.front(), "a.fit");
}

TEST(AfterNightAppFileSuite, UpdateRecentFileListSkipsWhenAlreadyMostRecent)
{
    std::deque<std::string> recent = {"a.fit", "b.fit"};
    const bool changed = an::ui::updateRecentFileList(recent, "a.fit", 5);

    EXPECT_FALSE(changed);
    ASSERT_EQ(recent.size(), 2u);
    EXPECT_EQ(recent.front(), "a.fit");
}

TEST(AfterNightAppFileSuite, UpdateRecentFileListTrimsToConfiguredLimit)
{
    std::deque<std::string> recent = {"c.fit", "d.fit"};
    const bool changed = an::ui::updateRecentFileList(recent, "a.fit", 2);

    EXPECT_TRUE(changed);
    ASSERT_EQ(recent.size(), 2u);
    EXPECT_EQ(recent.front(), "a.fit");
    EXPECT_EQ(recent.back(), "c.fit");
}

TEST(AfterNightAppFileSuite, IsLikelySupportedImagePathRecognizesCoreFormats)
{
    EXPECT_TRUE(an::ui::isLikelySupportedImagePath(QStringLiteral("frame.fits")));
    EXPECT_TRUE(an::ui::isLikelySupportedImagePath(QStringLiteral("frame.FTS")));
    EXPECT_TRUE(an::ui::isLikelySupportedImagePath(QStringLiteral("frame.xisf")));
    EXPECT_TRUE(an::ui::isLikelySupportedImagePath(QStringLiteral("frame.jpeg")));
    EXPECT_TRUE(an::ui::isLikelySupportedImagePath(QStringLiteral("frame.jpe")));
    EXPECT_TRUE(an::ui::isLikelySupportedImagePath(QStringLiteral("frame.x3f")));
    EXPECT_TRUE(an::ui::isLikelySupportedImagePath(QStringLiteral("frame.anseq")));
    EXPECT_TRUE(an::ui::isLikelySupportedImagePath(QStringLiteral("frame.mpeg")));
}

TEST(AfterNightAppFileSuite, IsLikelySupportedImagePathRejectsUnknownFormats)
{
    EXPECT_FALSE(an::ui::isLikelySupportedImagePath(QStringLiteral("notes.txt")));
    EXPECT_FALSE(an::ui::isLikelySupportedImagePath(QStringLiteral("archive.zip")));
}

TEST(AfterNightAppFileSuite, RecentFileThumbnailSizeUsesHeroReadyResolution)
{
    EXPECT_EQ(an::ui::recentFileThumbnailSize(QSize(6000, 4000)), QSize(2400, 1600));
    EXPECT_EQ(an::ui::recentFileThumbnailSize(QSize(4000, 6000)), QSize(1600, 2400));
    EXPECT_EQ(an::ui::recentFileThumbnailSize(QSize(1200, 800)), QSize(1200, 800));
}

TEST(AfterNightAppFileSuite, BuildOpenProgressDescriptionFormatsSingleFile)
{
    const QString text = an::ui::buildOpenProgressDescription("frame.fit", 1, 1);
    EXPECT_EQ(text, QStringLiteral("Opening file 'frame.fit'..."));
}

TEST(AfterNightAppFileSuite, BuildOpenProgressDescriptionFormatsBatch)
{
    const QString text = an::ui::buildOpenProgressDescription("frame.fit", 3, 8);
    EXPECT_EQ(text, QStringLiteral("Opening file 3/8: 'frame.fit'..."));
}

TEST(AfterNightAppFileSuite, ResolveSequenceSaveSuffixPreservesVideoContainer)
{
    EXPECT_EQ(an::ui::resolveSequenceSaveSuffix(3, QStringLiteral("capture.webm"), QStringLiteral("tiff")),
              QStringLiteral("webm"));
    EXPECT_EQ(an::ui::resolveSequenceSaveSuffix(3, QStringLiteral("capture.mkv"), QStringLiteral("tiff")),
              QStringLiteral("mkv"));
}

TEST(AfterNightAppFileSuite, ResolveSequenceSaveSuffixUsesNativeSequenceFamilies)
{
    EXPECT_EQ(an::ui::resolveSequenceSaveSuffix(1, QString(), QStringLiteral("tiff")), QStringLiteral("ser"));
    EXPECT_EQ(an::ui::resolveSequenceSaveSuffix(2, QString(), QStringLiteral("tiff")), QStringLiteral("anseq"));
    EXPECT_EQ(an::ui::resolveSequenceSaveSuffix(0, QString(), QStringLiteral("fits")), QStringLiteral("fits"));
}

// A non-native file dialog's themed icon provider goes with the dialog: QFileDialog::setIconProvider takes no
// ownership, so a provider the dialog does not hold is made for every dialog and deleted by nothing.
TEST(AfterNightAppFileSuite, ANonNativeFileDialogsIconProviderIsHeldByTheDialog)
{
    QFileDialog dialog;
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    an::ui::dialogs::detail::applyNonNativeDialogChrome(dialog);
    ASSERT_NE(dialog.iconProvider(), nullptr);
    const QObject* owner =
        dialog.findChild<QObject*>(QStringLiteral("anFileIconProviderOwner"), Qt::FindDirectChildrenOnly);
    ASSERT_NE(owner, nullptr) << "nothing the dialog owns holds its icon provider";
    const auto* holder = dynamic_cast<const an::ui::dialogs::detail::FileIconProviderOwner*>(owner);
    ASSERT_NE(holder, nullptr);
    EXPECT_EQ(static_cast<const QAbstractFileIconProvider*>(holder->provider()), dialog.iconProvider());
}
