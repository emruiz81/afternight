// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The featured file's presenter: the picture it decodes for a recent file covers the size it is asked for.

#include "controls/FeaturedFilePresenter.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QSize>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <gtest/gtest.h>

namespace
{

constexpr int kWaitMs = 2000;

/// A recent file: a few bytes under its name.
void createRecentSourceFile(const QString& filePath)
{
    QFile sourceFile(filePath);
    ASSERT_TRUE(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write("recent-source");
    sourceFile.close();
}

} // namespace

// A thumbnail larger than the size asked for is decoded to cover it: at least as wide and as tall.
TEST(FeaturedFilePresenterTest, DecodeProducesCoverSizedHeroPreview)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString sourcePath = QDir(tempDir.path()).filePath(QStringLiteral("hero_source.fit"));
    createRecentSourceFile(sourcePath);

    const QString thumbnailPath = QDir(tempDir.path()).filePath(QStringLiteral("hero_thumbnail.jpg"));
    QImage thumbnail(1800, 1200, QImage::Format_RGB32);
    thumbnail.fill(QColor(80, 110, 180));
    ASSERT_TRUE(thumbnail.save(thumbnailPath, "JPG"));

    an::ui::FeaturedFilePresenter presenter;
    presenter.setRecentFilePaths({sourcePath});
    presenter.setThumbnailPathResolver([thumbnailPath](const QString&) { return thumbnailPath; });

    const QSize targetSize(1200, 360);
    presenter.setPreferredDecodeSize(targetSize);

    QPixmap decodedPreview;
    QObject::connect(&presenter, &an::ui::FeaturedFilePresenter::featuredFileReady,
                     [&decodedPreview](const QString&, const QString&, const QString&, const QPixmap& preview,
                                       const bool) { decodedPreview = preview; });

    presenter.refresh();

    ASSERT_TRUE(QTest::qWaitFor([&decodedPreview]() { return !decodedPreview.isNull(); }, kWaitMs))
        << "the presenter decoded no picture";
    EXPECT_GE(decodedPreview.width(), targetSize.width());
    EXPECT_GE(decodedPreview.height(), targetSize.height());
}
