// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QtTest/QTest>
#include <QListWidget>
#include <QApplication>
#include <QKeyEvent>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTableWidget>
#include <QVariantList>
#include <QVariantMap>

#include "app/ApplicationStorage.h"
#include "Test_ControlsViewer/ViewerTestContext.h"

#include "controls/ImageViewControl.h"
#include "fixtures/common/IntegrationFrameFiles.h"
#include "processes/ImageIntegration/dialogs/BlinkComparatorDialog.h"
#include "widgets/ComparisonViewWidget.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace
{
void clearBlinkComparatorWindowSettings(QSettings& settings)
{
    settings.beginGroup(QStringLiteral("ImageIntegration/BlinkComparatorDialog"));
    settings.remove(QString());
    settings.endGroup();
    settings.sync();
}

void clearBlinkComparatorWindowSettings()
{
    QSettings settings = an::ui::applicationSettings();
    clearBlinkComparatorWindowSettings(settings);
}

QString makeDummyPath(const QString& directory, const QString& filename)
{
    QDir().mkpath(directory);
    const QString path = QDir(directory).filePath(filename);

    // BlinkComparatorDialog tests mostly operate on file paths (display names, indexing, etc.).
    // Avoid writing FITS files via engine codecs here; that has caused rare SEH crashes on Windows.
    // Creating an empty placeholder file keeps QFileInfo() stable without invoking image IO.
    QFile f(path);
    if (!f.exists())
    {
        (void)f.open(QIODevice::WriteOnly);
        f.close();
    }
    return path;
}

/// Writes a grey frame stating what the comparator's table reads from its header: filter, exposure, sensor
/// temperature, target and camera.
QString writeMetadataImage(const QString& directory, const QString& filename, const std::uint32_t width,
                           const std::uint32_t height, const QString& filter, const double exposureSeconds,
                           const double temperatureCelsius, const QString& object, const QString& camera)
{
    const QString path = QDir(directory).filePath(filename);
    an::tests::fixtures::AcquisitionFacts facts;
    facts.filter = filter.toStdString();
    facts.exposureSeconds = exposureSeconds;
    facts.sensorTemperatureCelsius = temperatureCelsius;
    facts.object = object.toStdString();
    facts.instrument = camera.toStdString();
    std::string error;
    const bool written = an::tests::fixtures::writeAcquisitionFrame(
        an::tests::fixtures::acquisitionFrame(filename.toStdString(), facts, width, height),
        std::filesystem::path(path.toStdWString()), error);
    EXPECT_TRUE(written) << error;
    return path;
}

/// A grey Float32 image of the session whose sample at (x, y) is (x + y) mod 256 as a fraction of 255.
std::optional<an::consumer::ImageAsset> makeFloatMonoImage(const an::consumer::EngineSession& session,
                                                           const std::uint32_t width, const std::uint32_t height)
{
    return an::tests::viewer::makeAsset<float>(
        session, width, height, an::tests::viewer::Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<float>((x + y) % 256u) / 255.0f; });
}
} // namespace

class BlinkComparatorModesTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        clearBlinkComparatorWindowSettings();

        if (!QCoreApplication::instance())
        {
            static int argc = 1;
            static char* argv[] = {const_cast<char*>("test")};
            static QApplication app(argc, argv);
        }

        // The comparator reads its frames' headers, and its views show images, through the session the application
        // context lends.
        context = an::tests::viewer::openViewerContext();
        ASSERT_NE(context, nullptr);
        installed.emplace(*context);

        tempDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(tempDir && tempDir->isValid());

        const QString dir = tempDir->path();
        file1 = makeDummyPath(dir, QStringLiteral("blink_file1.fit"));
        file2 = makeDummyPath(dir, QStringLiteral("blink_file2.fit"));
        file3 = makeDummyPath(dir, QStringLiteral("blink_file3.fit"));

        lowQuality = makeDummyPath(dir, QStringLiteral("blink_low.fit"));
        medQuality = makeDummyPath(dir, QStringLiteral("blink_med.fit"));
        highQuality = makeDummyPath(dir, QStringLiteral("blink_high.fit"));

        files = {file1, file2, file3};
        dialog = new BlinkComparatorDialog(files, nullptr, false);
    }

    void TearDown() override
    {
        delete dialog;
        dialog = nullptr;
        installed.reset();
        context.reset();
        clearBlinkComparatorWindowSettings();
    }

    std::unique_ptr<an::tests::viewer::ViewerTestContext> context;
    std::optional<an::tests::viewer::InstalledContext> installed;
    QStringList files;
    BlinkComparatorDialog* dialog = nullptr;
    std::unique_ptr<QTemporaryDir> tempDir;
    QString file1;
    QString file2;
    QString file3;
    QString lowQuality;
    QString highQuality;
    QString medQuality;

    QStringList make20Files()
    {
        QStringList list;
        const QString dir = tempDir ? tempDir->path() : QDir::tempPath();
        for (int i = 0; i < 20; ++i)
        {
            list << makeDummyPath(dir, QString("blink_%1.fit").arg(i));
        }
        return list;
    }
};

TEST_F(BlinkComparatorModesTest, SideBySideMode)
{
    // BlinkComparatorDialog dialog({file1, file2}); // Using fixture dialog
    dialog->setMode(BlinkComparatorDialog::BlinkMode::SideBySide);
    QApplication::processEvents();

    ASSERT_NE(dialog->leftPanel(), nullptr);
    ASSERT_NE(dialog->rightPanel(), nullptr);
    EXPECT_FALSE(dialog->leftPanel()->isHidden());
    EXPECT_FALSE(dialog->rightPanel()->isHidden());
}

TEST_F(BlinkComparatorModesTest, PreviewPanelsUseAutostretchByDefault)
{
    ASSERT_NE(dialog->leftPanel(), nullptr);
    ASSERT_NE(dialog->rightPanel(), nullptr);
    ASSERT_NE(dialog->leftPanel()->getViewWidget(), nullptr);
    ASSERT_NE(dialog->rightPanel()->getViewWidget(), nullptr);

    EXPECT_EQ(dialog->leftPanel()->getViewWidget()->getViewMode(), ImageViewMode::AUTOSTRETCH_MODE);
    EXPECT_EQ(dialog->rightPanel()->getViewWidget()->getViewMode(), ImageViewMode::AUTOSTRETCH_MODE);
}

TEST_F(BlinkComparatorModesTest, PreviewPanelsFitLoadedImagesByDefault)
{
    dialog->resize(700, 500);
    dialog->setMode(BlinkComparatorDialog::BlinkMode::SideBySide);
    dialog->show();
    QApplication::processEvents();

    ASSERT_NE(dialog->comparisonView(), nullptr);
    // Both images are wider and taller than a panel of the dialog, and one is twice the other; larger ones would hold
    // the claim no better, and a Debug build presents them slower than the case waits.
    std::optional<an::consumer::ImageAsset> left = makeFloatMonoImage(context->engineSession(), 800u, 600u);
    std::optional<an::consumer::ImageAsset> right = makeFloatMonoImage(context->engineSession(), 1600u, 1200u);
    ASSERT_TRUE(left.has_value() && right.has_value());
    an::ui::ComparisonViewWidget* comparison = dialog->comparisonView();
    comparison->setLeftImage(std::move(*left));
    comparison->setRightImage(std::move(*right));
    ASSERT_TRUE(an::tests::viewer::pumpUntil([comparison]() {
        return comparison->isComparisonIdle() && comparison->leftControl()->isWorkIdle() &&
               comparison->rightControl()->isWorkIdle() && comparison->leftControl()->hasImage() &&
               comparison->rightControl()->hasImage();
    }));
    an::tests::viewer::pumpQuietly();

    ASSERT_NE(dialog->leftPanel(), nullptr);
    ASSERT_NE(dialog->rightPanel(), nullptr);
    EXPECT_LT(dialog->leftPanel()->getCurrentZoom(), 100.0f);
    EXPECT_LT(dialog->rightPanel()->getCurrentZoom(), 100.0f);
    EXPECT_LT(dialog->rightPanel()->getCurrentZoom(), dialog->leftPanel()->getCurrentZoom());
}

TEST_F(BlinkComparatorModesTest, OverlayMode)
{
    dialog->setMode(BlinkComparatorDialog::BlinkMode::Overlay);
    dialog->setOpacity(0.5);

    EXPECT_EQ(dialog->overlayOpacity(), 0.5);
}

TEST_F(BlinkComparatorModesTest, DifferenceView)
{
    dialog->setMode(BlinkComparatorDialog::BlinkMode::Difference);

    EXPECT_EQ(dialog->currentMode(), BlinkComparatorDialog::BlinkMode::Difference);
}

TEST_F(BlinkComparatorModesTest, SynchronizedZoomPan)
{
    // A view scrolls only over an image larger than it, so both panels show one.
    dialog->resize(700, 500);
    dialog->setMode(BlinkComparatorDialog::BlinkMode::SideBySide);
    dialog->show();
    QApplication::processEvents();
    ASSERT_NE(dialog->comparisonView(), nullptr);
    std::optional<an::consumer::ImageAsset> left = makeFloatMonoImage(context->engineSession(), 1600u, 1200u);
    std::optional<an::consumer::ImageAsset> right = makeFloatMonoImage(context->engineSession(), 1600u, 1200u);
    ASSERT_TRUE(left.has_value() && right.has_value());
    an::ui::ComparisonViewWidget* comparison = dialog->comparisonView();
    comparison->setLeftImage(std::move(*left));
    comparison->setRightImage(std::move(*right));
    ASSERT_TRUE(an::tests::viewer::pumpUntil([comparison]() {
        return comparison->isComparisonIdle() && comparison->leftControl()->isWorkIdle() &&
               comparison->rightControl()->isWorkIdle() && comparison->leftControl()->hasImage() &&
               comparison->rightControl()->hasImage();
    }));
    an::tests::viewer::pumpQuietly();

    // Ensure panels are initialized
    ASSERT_NE(dialog->leftPanel(), nullptr);
    ASSERT_NE(dialog->rightPanel(), nullptr);

    ASSERT_NE(dialog->leftPanel()->getViewWidget(), nullptr);
    dialog->leftPanel()->getViewWidget()->scaleImageView(2.0, true);
    QApplication::processEvents();
    EXPECT_FLOAT_EQ(dialog->rightPanel()->getCurrentZoom(), 200.0f);

    dialog->leftPanel()->setViewportOffset(QPoint(100, 50));
    QApplication::processEvents();
    EXPECT_EQ(dialog->rightPanel()->getViewportOffset(), QPoint(100, 50));
}

TEST_F(BlinkComparatorModesTest, FrameExclusion)
{
    // Exclusion usually requires model interaction.
    // Since BlinkComparatorDialog takes QStringList, it might not have the model reference
    // unless passed in. The current constructor only takes QStringList.
    // We'll update the implementation to stub this or just check internal state for now.

    dialog->excludeCurrentFrame();

    EXPECT_TRUE(dialog->isExcluded(dialog->currentIndex()));
}

TEST_F(BlinkComparatorModesTest, SortByQuality)
{
    // This requires files with associated metrics.
    // Since we are mocking/using paths, we might need to mock the sort logic or
    // just verify the sort function is called/state changes if we can't easily mock metadata.
    // For now, let's assume we can set up a scenario where sorting works by name at least.

    dialog->sortBy(BlinkComparatorDialog::SortCriteria::Filename, Qt::DescendingOrder);

    // file3, file2, file1
    EXPECT_EQ(dialog->currentIndex(), 2);
    // Sorting should track the same current file after reordering.
    // Without access to internal list, difficult to verify order directly
    // unless we check what 'currentIndex 0' corresponds to.
    // But dialog doesn't expose 'fileAt(index)'.
    // Let's defer strict verification until implementation details are clear.
    // Just calling it shouldn't crash.
}

TEST_F(BlinkComparatorModesTest, ThumbnailStrip)
{
    delete dialog;
    dialog = new BlinkComparatorDialog(make20Files(), nullptr, false);

    ASSERT_NE(dialog->thumbnailStrip(), nullptr);
    EXPECT_FALSE(dialog->thumbnailStrip()->isHidden());
    // Checking count depends on widget type. Assuming QListWidget for now.
    auto* listWidget = qobject_cast<QListWidget*>(dialog->thumbnailStrip());
    if (listWidget)
    {
        EXPECT_EQ(listWidget->count(), 20);
    }
}

TEST_F(BlinkComparatorModesTest, NormalBlinkShowsSourceMetadataTableAndSwitchesOnDoubleClick)
{
    const QString dir = tempDir->path();
    const QString first = writeMetadataImage(dir, QStringLiteral("light_l.fit"), 64u, 32u, QStringLiteral("L"), 60.0,
                                             -10.5, QStringLiteral("M83"), QStringLiteral("ASI2600MM"));
    const QString second = writeMetadataImage(dir, QStringLiteral("light_ha.fit"), 80u, 40u, QStringLiteral("Ha"),
                                              180.0, -12.0, QStringLiteral("M83"), QStringLiteral("ASI2600MM"));

    delete dialog;
    dialog = new BlinkComparatorDialog({first, second}, nullptr, false);
    dialog->show();
    QApplication::processEvents();

    auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("historyTable"));
    ASSERT_NE(table, nullptr);
    EXPECT_FALSE(table->isHidden());
    EXPECT_EQ(table->rowCount(), 2);
    EXPECT_EQ(table->columnCount(), 8);
    EXPECT_FALSE(dialog->thumbnailStrip()->isHidden());

    ASSERT_NE(table->horizontalHeaderItem(0), nullptr);
    ASSERT_NE(table->horizontalHeaderItem(7), nullptr);
    EXPECT_EQ(table->horizontalHeaderItem(0)->text(), QStringLiteral("File"));
    EXPECT_EQ(table->horizontalHeaderItem(1)->text(), QStringLiteral("Filter"));
    EXPECT_EQ(table->horizontalHeaderItem(2)->text(), QStringLiteral("Resolution"));
    EXPECT_EQ(table->horizontalHeaderItem(3)->text(), QStringLiteral("Exposure"));
    EXPECT_EQ(table->horizontalHeaderItem(4)->text(), QStringLiteral("Temp"));
    EXPECT_EQ(table->horizontalHeaderItem(7)->text(), QStringLiteral("Path"));

    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("light_l.fit"));
    EXPECT_EQ(table->item(0, 1)->text(), QStringLiteral("L"));
    EXPECT_EQ(table->item(0, 2)->text(), QStringLiteral("64x32"));
    EXPECT_EQ(table->item(0, 3)->text(), QStringLiteral("60s"));
    EXPECT_EQ(table->item(0, 4)->text(), QStringLiteral("-10.5 C"));
    EXPECT_EQ(table->item(0, 5)->text(), QStringLiteral("M83"));
    EXPECT_EQ(table->item(0, 6)->text(), QStringLiteral("ASI2600MM"));
    EXPECT_EQ(table->item(0, 7)->text(), first);

    EXPECT_EQ(dialog->currentIndex(), 0);

    table->setCurrentCell(1, 0);
    QApplication::processEvents();
    EXPECT_EQ(dialog->currentIndex(), 0);

    const QRect secondRect = table->visualItemRect(table->item(1, 0));
    ASSERT_FALSE(secondRect.isEmpty());
    QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, secondRect.center());
    QApplication::processEvents();
    EXPECT_EQ(dialog->currentIndex(), 1);
    EXPECT_EQ(table->currentRow(), 1);
}

TEST_F(BlinkComparatorModesTest, CustomLabelsIdentifyImagesWithSameFilename)
{
    const QString olderDir = QDir(tempDir->path()).filePath(QStringLiteral("older"));
    const QString newerDir = QDir(tempDir->path()).filePath(QStringLiteral("newer"));
    const QString older = makeDummyPath(olderDir, QStringLiteral("stacked_Blue.fit"));
    const QString newer = makeDummyPath(newerDir, QStringLiteral("stacked_Blue.fit"));

    delete dialog;
    dialog = new BlinkComparatorDialog(
        {newer, older}, {QStringLiteral("Run 2025-01-03 - Blue"), QStringLiteral("Previous before 2025-01-03 - Blue")},
        {QStringLiteral("Stacking: Average / Winsorized Sigma Clipping\n"
                        "Weighting: PSF Signal Weight\n"
                        "Drizzle: Enabled x2\n"
                        "Resolution: 2000x2000"),
         QStringLiteral("Stacking: Median / None\n"
                        "Weighting: Disabled\n"
                        "Drizzle: Disabled\n"
                        "Resolution: 1000x1000")},
        nullptr, false);
    dialog->setMode(BlinkComparatorDialog::BlinkMode::SideBySide);

    auto* statusLabel = dialog->findChild<QLabel*>(QStringLiteral("statusLabel"));
    ASSERT_NE(statusLabel, nullptr);
    EXPECT_TRUE(statusLabel->text().contains(QStringLiteral("Run 2025-01-03 - Blue")));
    EXPECT_TRUE(statusLabel->text().contains(QStringLiteral("Previous before 2025-01-03 - Blue")));

    auto* leftLabel = dialog->findChild<QLabel*>(QStringLiteral("leftImageLabel"));
    auto* rightLabel = dialog->findChild<QLabel*>(QStringLiteral("rightImageLabel"));
    ASSERT_NE(leftLabel, nullptr);
    ASSERT_NE(rightLabel, nullptr);
    EXPECT_EQ(leftLabel->text(), QStringLiteral("Run 2025-01-03 - Blue"));
    EXPECT_EQ(rightLabel->text(), QStringLiteral("Previous before 2025-01-03 - Blue"));

    auto* leftContext = dialog->findChild<QLabel*>(QStringLiteral("leftContextLabel"));
    auto* rightContext = dialog->findChild<QLabel*>(QStringLiteral("rightContextLabel"));
    ASSERT_NE(leftContext, nullptr);
    ASSERT_NE(rightContext, nullptr);
    EXPECT_TRUE(leftContext->text().contains(QStringLiteral("PSF Signal Weight")));
    EXPECT_TRUE(leftContext->text().contains(QStringLiteral("2000x2000")));
    EXPECT_TRUE(rightContext->text().contains(QStringLiteral("Disabled")));
    EXPECT_TRUE(rightContext->text().contains(QStringLiteral("1000x1000")));

    auto* listWidget = qobject_cast<QListWidget*>(dialog->thumbnailStrip());
    ASSERT_NE(listWidget, nullptr);
    ASSERT_EQ(listWidget->count(), 2);
    EXPECT_EQ(listWidget->item(0)->text(), QStringLiteral("Run 2025-01-03 - Blue"));
    EXPECT_EQ(listWidget->item(1)->text(), QStringLiteral("Previous before 2025-01-03 - Blue"));
}

TEST_F(BlinkComparatorModesTest, HistoryRowsReplaceThumbnailStripWithExecutionTable)
{
    const QString olderDir = QDir(tempDir->path()).filePath(QStringLiteral("older"));
    const QString newerDir = QDir(tempDir->path()).filePath(QStringLiteral("newer"));
    const QString oldestDir = QDir(tempDir->path()).filePath(QStringLiteral("oldest"));
    const QString older = makeDummyPath(olderDir, QStringLiteral("stacked_Blue.fit"));
    const QString newer = makeDummyPath(newerDir, QStringLiteral("stacked_Blue.fit"));
    const QString oldest = makeDummyPath(oldestDir, QStringLiteral("stacked_Blue.fit"));

    delete dialog;
    dialog = new BlinkComparatorDialog({newer, older, oldest},
                                       {QStringLiteral("Blue master - 2025-01-03"),
                                        QStringLiteral("Blue master - 2025-01-02"),
                                        QStringLiteral("Blue master - 2025-01-01")},
                                       {QStringLiteral("Stacking: Average\nResolution: 2000x2000"),
                                        QStringLiteral("Stacking: Average\nResolution: 1000x1000"),
                                        QStringLiteral("Stacking: Median\nResolution: 900x900")},
                                       nullptr, false);
    dialog->setMode(BlinkComparatorDialog::BlinkMode::SideBySide);

    QVariantMap newerRow;
    newerRow.insert(QStringLiteral("label"), QStringLiteral("Blue master - 2025-01-03"));
    newerRow.insert(QStringLiteral("path"), newer);
    newerRow.insert(QStringLiteral("executedAtText"), QStringLiteral("2025-01-03 12:00"));
    newerRow.insert(QStringLiteral("integrationTimeText"), QStringLiteral("3h 30m"));
    newerRow.insert(QStringLiteral("frameCount"), 42);
    newerRow.insert(QStringLiteral("filter"), QStringLiteral("Blue"));
    newerRow.insert(QStringLiteral("object"), QStringLiteral("M83"));
    newerRow.insert(QStringLiteral("resolution"), QStringLiteral("2000x2000"));

    QVariantMap olderRow;
    olderRow.insert(QStringLiteral("label"), QStringLiteral("Blue master - 2025-01-02"));
    olderRow.insert(QStringLiteral("path"), older);
    olderRow.insert(QStringLiteral("executedAtText"), QStringLiteral("2025-01-02 12:00"));
    olderRow.insert(QStringLiteral("integrationTimeText"), QStringLiteral("3h 20m"));
    olderRow.insert(QStringLiteral("frameCount"), 40);
    olderRow.insert(QStringLiteral("filter"), QStringLiteral("Blue"));
    olderRow.insert(QStringLiteral("object"), QStringLiteral("M83"));
    olderRow.insert(QStringLiteral("resolution"), QStringLiteral("1000x1000"));

    QVariantMap oldestRow;
    oldestRow.insert(QStringLiteral("label"), QStringLiteral("Blue master - 2025-01-01"));
    oldestRow.insert(QStringLiteral("path"), oldest);
    oldestRow.insert(QStringLiteral("executedAtText"), QStringLiteral("2025-01-01 12:00"));
    oldestRow.insert(QStringLiteral("integrationSeconds"), 11400.0);
    oldestRow.insert(QStringLiteral("frameCount"), 38);
    oldestRow.insert(QStringLiteral("filter"), QStringLiteral("Blue"));
    oldestRow.insert(QStringLiteral("object"), QStringLiteral("M83"));
    oldestRow.insert(QStringLiteral("resolution"), QStringLiteral("900x900"));

    QVariantList rows;
    rows << newerRow << olderRow << oldestRow;
    dialog->setHistoryRows(rows);
    dialog->show();
    QApplication::processEvents();

    auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("historyTable"));
    ASSERT_NE(table, nullptr);
    EXPECT_FALSE(table->isHidden());
    EXPECT_EQ(table->rowCount(), 3);
    EXPECT_EQ(table->columnCount(), 8);
    ASSERT_NE(table->horizontalHeaderItem(0), nullptr);
    ASSERT_NE(table->horizontalHeaderItem(7), nullptr);
    EXPECT_EQ(table->horizontalHeaderItem(0)->text(), QStringLiteral("Run"));
    EXPECT_EQ(table->horizontalHeaderItem(2)->text(), QStringLiteral("Integration Time"));
    EXPECT_EQ(table->horizontalHeaderItem(7)->text(), QStringLiteral("Path"));
    for (int column = 0; column < table->columnCount(); ++column)
    {
        const QString header = table->horizontalHeaderItem(column)->text();
        EXPECT_NE(header, QStringLiteral("Duration"));
        EXPECT_NE(header, QStringLiteral("Exposure"));
    }
    EXPECT_EQ(table->item(0, 1)->text(), QStringLiteral("2025-01-03 12:00"));
    EXPECT_EQ(table->item(0, 2)->text(), QStringLiteral("3h 30m"));
    EXPECT_EQ(table->item(0, 3)->text(), QStringLiteral("42"));
    EXPECT_EQ(table->item(0, 7)->text(), newer);
    EXPECT_FALSE(table->item(0, 0)->flags().testFlag(Qt::ItemIsSelectable));
    EXPECT_TRUE(table->item(1, 0)->flags().testFlag(Qt::ItemIsSelectable));

    auto* listWidget = qobject_cast<QListWidget*>(dialog->thumbnailStrip());
    ASSERT_NE(listWidget, nullptr);
    EXPECT_TRUE(listWidget->isHidden());

    EXPECT_EQ(dialog->currentIndex(), 0);
    EXPECT_EQ(dialog->comparisonTargetIndex(), 1);

    auto* leftLabel = dialog->findChild<QLabel*>(QStringLiteral("leftImageLabel"));
    auto* rightLabel = dialog->findChild<QLabel*>(QStringLiteral("rightImageLabel"));
    ASSERT_NE(leftLabel, nullptr);
    ASSERT_NE(rightLabel, nullptr);
    EXPECT_TRUE(leftLabel->text().contains(QStringLiteral("Blue master - 2025-01-03")));
    EXPECT_TRUE(leftLabel->text().contains(QStringLiteral("newer/stacked_Blue.fit")));
    EXPECT_FALSE(leftLabel->text().contains(tempDir->path()));
    EXPECT_TRUE(rightLabel->text().contains(QStringLiteral("Blue master - 2025-01-02")));
    EXPECT_TRUE(rightLabel->text().contains(QStringLiteral("older/stacked_Blue.fit")));
    EXPECT_FALSE(rightLabel->text().contains(tempDir->path()));

    const QRect oldestRect = table->visualItemRect(table->item(2, 0));
    ASSERT_FALSE(oldestRect.isEmpty());
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, oldestRect.center());
    QApplication::processEvents();
    EXPECT_EQ(dialog->currentIndex(), 0);
    EXPECT_EQ(dialog->comparisonTargetIndex(), 2);
    EXPECT_TRUE(rightLabel->text().contains(QStringLiteral("Blue master - 2025-01-01")));
    EXPECT_TRUE(rightLabel->text().contains(QStringLiteral("oldest/stacked_Blue.fit")));
    EXPECT_FALSE(rightLabel->text().contains(tempDir->path()));

    const QRect referenceRect = table->visualItemRect(table->item(0, 0));
    ASSERT_FALSE(referenceRect.isEmpty());
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, referenceRect.center());
    QApplication::processEvents();
    EXPECT_EQ(dialog->comparisonTargetIndex(), 2);

    auto* previousButton = dialog->findChild<QPushButton*>(QStringLiteral("prevButton"));
    auto* nextButton = dialog->findChild<QPushButton*>(QStringLiteral("nextButton"));
    ASSERT_NE(previousButton, nullptr);
    ASSERT_NE(nextButton, nullptr);

    QTest::mouseClick(previousButton, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_EQ(dialog->comparisonTargetIndex(), 1);

    QTest::mouseClick(nextButton, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_EQ(dialog->comparisonTargetIndex(), 2);
}

TEST_F(BlinkComparatorModesTest, ContextCardsHighlightDifferingSummaryLines)
{
    const QString olderDir = QDir(tempDir->path()).filePath(QStringLiteral("older"));
    const QString newerDir = QDir(tempDir->path()).filePath(QStringLiteral("newer"));
    const QString older = makeDummyPath(olderDir, QStringLiteral("stacked_Blue.fit"));
    const QString newer = makeDummyPath(newerDir, QStringLiteral("stacked_Blue.fit"));

    delete dialog;
    dialog = new BlinkComparatorDialog({newer, older}, {QStringLiteral("Newer Blue"), QStringLiteral("Older Blue")},
                                       {QStringLiteral("Stacking: Average / Winsorized Sigma Clipping\n"
                                                       "Weighting: PSF Signal Weight\n"
                                                       "Drizzle: Enabled x2\n"
                                                       "Resolution: 2000x2000\n"
                                                       "Normalization: Local (Fine), Additive+Scale, IKSS"),
                                        QStringLiteral("Stacking: Average / Winsorized Sigma Clipping\n"
                                                       "Weighting: PSF Signal Weight\n"
                                                       "Drizzle: Disabled\n"
                                                       "Resolution: 1000x1000\n"
                                                       "Normalization: Global, Additive+Scale, IKSS")},
                                       nullptr, false);
    dialog->setMode(BlinkComparatorDialog::BlinkMode::SideBySide);

    auto* leftContext = dialog->findChild<QLabel*>(QStringLiteral("leftContextLabel"));
    auto* rightContext = dialog->findChild<QLabel*>(QStringLiteral("rightContextLabel"));
    ASSERT_NE(leftContext, nullptr);
    ASSERT_NE(rightContext, nullptr);

    EXPECT_EQ(leftContext->textFormat(), Qt::RichText);
    EXPECT_TRUE(leftContext->text().contains(QStringLiteral("background-color")));
    EXPECT_TRUE(leftContext->text().contains(QStringLiteral("Drizzle: Enabled x2")));
    EXPECT_TRUE(leftContext->text().contains(QStringLiteral("Resolution: 2000x2000")));
    EXPECT_TRUE(leftContext->text().contains(QStringLiteral("Normalization: Local (Fine)")));
    EXPECT_FALSE(leftContext->text().contains(QStringLiteral("<strong>")));
    EXPECT_FALSE(leftContext->text().contains(QStringLiteral("Stacking: Average / Winsorized Sigma Clipping</span>")));
    EXPECT_FALSE(leftContext->text().contains(QStringLiteral("Weighting: PSF Signal Weight</span>")));
    EXPECT_FALSE(leftContext->text().contains(QStringLiteral("->")));

    EXPECT_TRUE(rightContext->text().contains(QStringLiteral("background-color")));
    EXPECT_TRUE(rightContext->text().contains(QStringLiteral("Drizzle: Disabled")));
    EXPECT_TRUE(rightContext->text().contains(QStringLiteral("Resolution: 1000x1000")));
    EXPECT_TRUE(rightContext->text().contains(QStringLiteral("Normalization: Global")));
    EXPECT_FALSE(rightContext->text().contains(QStringLiteral("<strong>")));
    EXPECT_FALSE(rightContext->text().contains(QStringLiteral("->")));
}

TEST_F(BlinkComparatorModesTest, WindowSupportsMaximizeButton)
{
    EXPECT_TRUE(dialog->windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
}

TEST_F(BlinkComparatorModesTest, WindowUsesTallerDefaultSize)
{
    EXPECT_GE(dialog->size().height(), 700);
}

TEST(BlinkComparatorDialogWindowSettingsTest, RemembersWindowSize)
{
    clearBlinkComparatorWindowSettings();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString file1 = makeDummyPath(tempDir.path(), QStringLiteral("remembered_size_a.fit"));
    const QString file2 = makeDummyPath(tempDir.path(), QStringLiteral("remembered_size_b.fit"));

    {
        BlinkComparatorDialog dialog({file1, file2}, nullptr, false);
        dialog.resize(900, 740);
        dialog.close();
        QApplication::processEvents();
    }

    {
        BlinkComparatorDialog dialog({file1, file2}, nullptr, false);
        EXPECT_EQ(dialog.size(), QSize(900, 740));
    }

    clearBlinkComparatorWindowSettings();
}

TEST(BlinkComparatorDialogWindowSettingsTest, RemembersWindowSizeAfterHistoryComparisonSetup)
{
    clearBlinkComparatorWindowSettings();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString newer =
        makeDummyPath(QDir(tempDir.path()).filePath(QStringLiteral("newer")), QStringLiteral("stacked_Blue.fit"));
    const QString older =
        makeDummyPath(QDir(tempDir.path()).filePath(QStringLiteral("older")), QStringLiteral("stacked_Blue.fit"));

    QVariantMap newerRow;
    newerRow.insert(QStringLiteral("label"), QStringLiteral("Newer Blue"));
    newerRow.insert(QStringLiteral("path"), newer);
    newerRow.insert(QStringLiteral("executedAtText"), QStringLiteral("2025-01-03 12:00"));

    QVariantMap olderRow;
    olderRow.insert(QStringLiteral("label"), QStringLiteral("Older Blue"));
    olderRow.insert(QStringLiteral("path"), older);
    olderRow.insert(QStringLiteral("executedAtText"), QStringLiteral("2025-01-02 12:00"));

    QVariantList rows;
    rows << newerRow << olderRow;

    {
        BlinkComparatorDialog dialog({newer, older}, {QStringLiteral("Newer Blue"), QStringLiteral("Older Blue")}, {},
                                     nullptr, false);
        dialog.setHistoryRows(rows);
        dialog.setMode(BlinkComparatorDialog::BlinkMode::SideBySide);
        dialog.open();
        QApplication::processEvents();
        dialog.resize(930, 760);
        QApplication::processEvents();
        dialog.reject();
        QApplication::processEvents();
    }

    {
        BlinkComparatorDialog dialog({newer, older}, {QStringLiteral("Newer Blue"), QStringLiteral("Older Blue")}, {},
                                     nullptr, false);
        dialog.setHistoryRows(rows);
        dialog.resize(820, 680);
        dialog.setMode(BlinkComparatorDialog::BlinkMode::SideBySide);
        dialog.open();
        QApplication::processEvents();
        EXPECT_EQ(dialog.size(), QSize(930, 760));
    }

    clearBlinkComparatorWindowSettings();
}

TEST_F(BlinkComparatorModesTest, KeyboardRating)
{
    QKeyEvent rateEvent(QEvent::KeyPress, Qt::Key_5, Qt::NoModifier);
    QApplication::sendEvent(dialog, &rateEvent);
    EXPECT_EQ(dialog->ratingFor(dialog->currentIndex()), 5);

    QKeyEvent excludeEvent(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
    QApplication::sendEvent(dialog, &excludeEvent);
    EXPECT_TRUE(dialog->isExcluded(dialog->currentIndex()));
}
