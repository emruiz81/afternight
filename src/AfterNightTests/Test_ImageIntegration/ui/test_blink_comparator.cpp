// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include "processes/ImageIntegration/dialogs/BlinkComparatorDialog.h"

class BlinkComparatorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        files = {"C:/file1.fit", "C:/file2.fit", "C:/file3.fit"};
        dialog = new BlinkComparatorDialog(files);
    }

    void TearDown() override
    {
        delete dialog;
    }

    QStringList files;
    BlinkComparatorDialog* dialog = nullptr;
};

TEST_F(BlinkComparatorTest, InitialState)
{
    // Verify widgets exist with object names
    auto* playButton = dialog->findChild<QPushButton*>(QStringLiteral("playButton"));
    auto* prevButton = dialog->findChild<QPushButton*>(QStringLiteral("prevButton"));
    auto* nextButton = dialog->findChild<QPushButton*>(QStringLiteral("nextButton"));
    auto* speedSlider = dialog->findChild<QSlider*>(QStringLiteral("speedSlider"));
    auto* statusLabel = dialog->findChild<QLabel*>(QStringLiteral("statusLabel"));

    ASSERT_NE(playButton, nullptr);
    ASSERT_NE(prevButton, nullptr);
    ASSERT_NE(nextButton, nullptr);
    ASSERT_NE(speedSlider, nullptr);
    ASSERT_NE(statusLabel, nullptr);

    EXPECT_EQ(playButton->text(), "Play");
    EXPECT_EQ(dialog->currentIndex(), 0);
    EXPECT_FALSE(dialog->isPlaying());
}

TEST_F(BlinkComparatorTest, NextButtonAdvancesIndex)
{
    auto* nextButton = dialog->findChild<QPushButton*>(QStringLiteral("nextButton"));
    ASSERT_NE(nextButton, nullptr);

    EXPECT_EQ(dialog->currentIndex(), 0);

    QTest::mouseClick(nextButton, Qt::LeftButton);
    EXPECT_EQ(dialog->currentIndex(), 1);

    QTest::mouseClick(nextButton, Qt::LeftButton);
    EXPECT_EQ(dialog->currentIndex(), 2);

    // Wraps around
    QTest::mouseClick(nextButton, Qt::LeftButton);
    EXPECT_EQ(dialog->currentIndex(), 0);
}

TEST_F(BlinkComparatorTest, PrevButtonDecreasesIndex)
{
    auto* prevButton = dialog->findChild<QPushButton*>(QStringLiteral("prevButton"));
    ASSERT_NE(prevButton, nullptr);

    EXPECT_EQ(dialog->currentIndex(), 0);

    // Wraps to end
    QTest::mouseClick(prevButton, Qt::LeftButton);
    EXPECT_EQ(dialog->currentIndex(), 2);

    QTest::mouseClick(prevButton, Qt::LeftButton);
    EXPECT_EQ(dialog->currentIndex(), 1);
}

TEST_F(BlinkComparatorTest, PlayButtonTogglesPlayState)
{
    auto* playButton = dialog->findChild<QPushButton*>(QStringLiteral("playButton"));
    ASSERT_NE(playButton, nullptr);

    EXPECT_FALSE(dialog->isPlaying());
    EXPECT_EQ(playButton->text(), "Play");

    QTest::mouseClick(playButton, Qt::LeftButton);
    EXPECT_TRUE(dialog->isPlaying());
    EXPECT_EQ(playButton->text(), "Pause");

    QTest::mouseClick(playButton, Qt::LeftButton);
    EXPECT_FALSE(dialog->isPlaying());
    EXPECT_EQ(playButton->text(), "Play");
}

TEST_F(BlinkComparatorTest, SpeedSliderAffectsInterval)
{
    auto* speedSlider = dialog->findChild<QSlider*>(QStringLiteral("speedSlider"));
    ASSERT_NE(speedSlider, nullptr);

    int initialInterval = dialog->intervalMs();

    // Move slider to fast (right/high value)
    speedSlider->setValue(100);
    int fastInterval = dialog->intervalMs();
    EXPECT_LT(fastInterval, initialInterval); // Faster = smaller interval

    // Move slider to slow (left/low value)
    speedSlider->setValue(0);
    int slowInterval = dialog->intervalMs();
    EXPECT_GT(slowInterval, fastInterval); // Slower = larger interval
}

TEST_F(BlinkComparatorTest, SliderRightMeansFaster)
{
    auto* speedSlider = dialog->findChild<QSlider*>(QStringLiteral("speedSlider"));
    ASSERT_NE(speedSlider, nullptr);

    // Slider at 0 (leftmost) should be slowest
    speedSlider->setValue(0);
    int leftInterval = dialog->intervalMs();

    // Slider at 100 (rightmost) should be fastest
    speedSlider->setValue(100);
    int rightInterval = dialog->intervalMs();

    // Right should be faster (smaller interval)
    EXPECT_LT(rightInterval, leftInterval);

    // Verify bounds
    EXPECT_GE(rightInterval, 50); // min interval
    EXPECT_LE(leftInterval, 2000); // max interval
}

TEST_F(BlinkComparatorTest, EmptyFilesListHandledGracefully)
{
    BlinkComparatorDialog emptyDialog({});

    auto* playButton = emptyDialog.findChild<QPushButton*>(QStringLiteral("playButton"));
    ASSERT_NE(playButton, nullptr);

    // Should not crash when clicking buttons with no files
    QTest::mouseClick(playButton, Qt::LeftButton);
    EXPECT_EQ(emptyDialog.currentIndex(), 0);
}

TEST_F(BlinkComparatorTest, StatusLabelShowsCurrentFile)
{
    auto* statusLabel = dialog->findChild<QLabel*>(QStringLiteral("statusLabel"));
    ASSERT_NE(statusLabel, nullptr);

    QString text = statusLabel->text();
    EXPECT_TRUE(text.contains("1 / 3")); // First of 3 files
    EXPECT_TRUE(text.contains("file1.fit"));
}

// --- Zoom/Pan Tests ---

TEST_F(BlinkComparatorTest, ZoomSliderIsNotShown)
{
    auto* zoomSlider = dialog->findChild<QSlider*>(QStringLiteral("zoomSlider"));
    EXPECT_EQ(zoomSlider, nullptr);
}

TEST_F(BlinkComparatorTest, FitButtonIsNotShown)
{
    bool foundFit = false;
    for (auto* btn : dialog->findChildren<QPushButton*>())
    {
        if (btn->text() == "Fit")
        {
            foundFit = true;
            break;
        }
    }
    EXPECT_FALSE(foundFit);
}

TEST_F(BlinkComparatorTest, OneToOneButtonIsNotShown)
{
    bool foundOneToOne = false;
    for (auto* btn : dialog->findChildren<QPushButton*>())
    {
        if (btn->text() == "1:1")
        {
            foundOneToOne = true;
            break;
        }
    }
    EXPECT_FALSE(foundOneToOne);
}

TEST(BlinkComparatorSettingsTest, TheDialogsSettingsLiveUnderTheSuitesStorageRoot)
{
    // The dialog keeps its size in the application's settings file under the storage root, never in the platform's
    // native store - the registry on Windows, which no storage root redirects.
    const QString root = QDir::cleanPath(QString::fromLocal8Bit(qgetenv("AFTERNIGHT_TEST_STORAGE_ROOT")));
    ASSERT_FALSE(root.isEmpty());
    const QString settingsFile = QDir(root).filePath(QStringLiteral("settings/AfterNight.ini"));

    QTemporaryDir frames;
    ASSERT_TRUE(frames.isValid());
    QStringList files;
    for (const QString& name : {QStringLiteral("a.fit"), QStringLiteral("b.fit")})
    {
        QFile frame(frames.filePath(name));
        ASSERT_TRUE(frame.open(QIODevice::WriteOnly));
        files << frame.fileName();
    }

    {
        BlinkComparatorDialog dialog(files, nullptr, false);
        dialog.resize(912, 744);
        dialog.close();
        QApplication::processEvents();
    }

    const QSettings settings(settingsFile, QSettings::IniFormat);
    EXPECT_EQ(settings.value(QStringLiteral("ImageIntegration/BlinkComparatorDialog/size")).toSize(), QSize(912, 744))
        << settingsFile.toStdString();
}
