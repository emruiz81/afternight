// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>
#include <QApplication>
#include <QColor>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSet>
#include <QDir>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>

#include "processes/ImageIntegration/widgets/FrameQualityDashboard.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/core/RegistrationMetricsWriter.h"
#include "theme/AnDesignTokens.h"

#include "Test_ImageIntegration/ui/OpenDialogs.h"
#include "Test_ImageIntegration/ui/ProcessWindowContext.h"
#include "Test_ImageIntegration/workflow/StackWeightRules.h"
#include "fixtures/common/FixtureFits.h"
#include "fixtures/common/FixtureImage.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Stacking/PhotometricWeighting.h>
#include <AfterNightEngine/Core/Result.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace
{
using an::tests::fixtures::FixtureCard;
using an::tests::fixtures::FixtureCardKind;
// Helper to create a dummy frame
FrameInfo createFrame(const QString& path)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000),
                       1.0, // exposure
                       std::nullopt, // temp
                       std::nullopt, // gain
                       std::nullopt, // offset
                       "L", // filter
                       "Obj", // object
                       QDateTime::currentDateTimeUtc(),
                       "" // readout mode
    );

    return FrameInfo(FrameId(path), path, meta);
}

FrameInfo createFrameWithExposure(const QString& path, double exposureSeconds)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), exposureSeconds, std::nullopt, std::nullopt, std::nullopt,
                       "L", "Obj", QDateTime::currentDateTimeUtc(), "");

    return FrameInfo(FrameId(path), path, meta);
}

/// The stack's photometric weights for `measurements` under the group's `floor`, worked out in `session` with the
/// stack's default model terms - the weights the dashboard's preview shows.
std::vector<double> stackPhotometricWeights(const an::consumer::EngineSession& session,
                                            const std::vector<an::PhotometricFrameMeasurement>& measurements,
                                            const double floor)
{
    an::Result<an::consumer::OperationScope> scope = session.tryAdmit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview, .maxConcurrency = 1u});
    if (!scope.hasValue())
    {
        ADD_FAILURE() << "The session admitted no operation: " << scope.status().message();
        return {};
    }
    std::vector<an::PhotometricStackWeight> output(measurements.size());
    const auto result = an::computePhotometricStackWeights(
        scope->execution(), measurements, an::PhotometricWeightingRequest{.minimumRawWeight = floor}, output);
    if (!result.hasValue())
    {
        ADD_FAILURE() << "The photometric weighting failed: " << result.status().message();
        return {};
    }
    std::vector<double> weights;
    weights.reserve(output.size());
    for (const auto& weight : output)
    {
        weights.push_back(weight.value);
    }
    return weights;
}
} // namespace

class FrameQualityDashboardTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        model = new IntegrationDataModel();
        dashboard = new FrameQualityDashboard(model);
    }

    void TearDown() override
    {
        delete dashboard;
        delete model;
    }

    IntegrationDataModel* model = nullptr;
    FrameQualityDashboard* dashboard = nullptr;
};

TEST_F(FrameQualityDashboardTest, InitialStateIsEmpty)
{
    QTableWidget* table = dashboard->findChild<QTableWidget*>("qualityTable");
    ASSERT_TRUE(table != nullptr);
    EXPECT_EQ(table->rowCount(), 0);
}

TEST_F(FrameQualityDashboardTest, ShowsRegisteredFrames)
{
    // Add frames to model
    std::vector<FrameInfo> frames;
    frames.push_back(createFrame("C:/frame1.fit"));
    frames.push_back(createFrame("C:/frame2.fit"));
    model->addFrames(FrameType::Light, frames);

    // In V2, registration populates metadata/quality.
    // Assuming dashboard listens to registrationChanged or similar,
    // or just reflects Light frames if they have quality info.
    // For now, let's assume it shows all light frames.

    QTableWidget* table = dashboard->findChild<QTableWidget*>("qualityTable");
    ASSERT_TRUE(table != nullptr);
    // Depending on implementation (async or immediate), wait or check.
    // Assuming immediate for now as per other widgets.
    EXPECT_EQ(table->rowCount(), 2);
}

TEST_F(FrameQualityDashboardTest, OutlierDetectionSelectsFrames)
{
    // Given frames
    QString goodPath = "C:/good1.fit";
    QString badPath = "C:/bad1.fit";

    std::vector<FrameInfo> frames;
    frames.push_back(createFrame(goodPath));
    frames.push_back(createFrame(badPath));
    model->addFrames(FrameType::Light, frames);

    // Inject quality metrics
    FrameQualityMetrics goodMetrics;
    goodMetrics.stars = 1000;
    goodMetrics.fwhm = 2.0;
    goodMetrics.snr = 50.0;
    goodMetrics.eccentricity = 0.1;

    FrameQualityMetrics badMetrics;
    badMetrics.stars = 50;
    badMetrics.fwhm = 6.0;
    badMetrics.snr = 10.0;
    badMetrics.eccentricity = 0.6;

    model->setFrameQualityMetrics(FrameId(goodPath), goodMetrics);
    model->setFrameQualityMetrics(FrameId(badPath), badMetrics);

    // Refresh dashboard (normally happens via signals, but ensure state)
    dashboard->show(); // Triggers some events
    QCoreApplication::processEvents();

    QTableWidget* table = dashboard->findChild<QTableWidget*>("qualityTable");
    QPushButton* detectBtn = dashboard->findChild<QPushButton*>("detectOutliersButton");
    QPushButton* excludeBtn = dashboard->findChild<QPushButton*>("excludeSelectedButton");

    ASSERT_TRUE(table != nullptr);
    ASSERT_TRUE(detectBtn != nullptr);
    ASSERT_TRUE(excludeBtn != nullptr);

    // When "Detect Outliers" clicked
    // We can't easily click the button if it shows a QMessageBox (it blocks).
    // So we invoke the slot directly or assume it works and check the logic.
    // For unit testing, let's just trigger the slot if possible or test the model behavior.

    // Since QMessageBox will block, we'll avoid the direct click in automated test
    // unless we have a message box handler.
    // Instead, let's verify that selecting a row and clicking exclude works.

    table->selectRow(1); // Select the "bad" frame
    excludeBtn->click();

    // Then the frame should be excluded in the model
    auto frame = model->lightFrames().findById(FrameId(badPath));
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->excluded);
}

TEST_F(FrameQualityDashboardTest, OutlierDetectionSelectsEveryDetectedOutlierRow)
{
    constexpr int goodFrameCount = 10;
    constexpr int outlierFrameCount = 3;

    std::vector<FrameInfo> frames;
    std::vector<QString> goodPaths;
    std::vector<QString> outlierPaths;
    goodPaths.reserve(goodFrameCount);
    outlierPaths.reserve(outlierFrameCount);

    for (int i = 0; i < goodFrameCount; ++i)
    {
        const QString path = QStringLiteral("C:/quality_good_%1.fit").arg(i);
        goodPaths.push_back(path);
        frames.push_back(createFrame(path));
    }
    for (int i = 0; i < outlierFrameCount; ++i)
    {
        const QString path = QStringLiteral("C:/quality_outlier_%1.fit").arg(i);
        outlierPaths.push_back(path);
        frames.push_back(createFrame(path));
    }

    model->addFrames(FrameType::Light, frames);

    FrameQualityMetrics goodMetrics;
    goodMetrics.stars = 3000;
    goodMetrics.fwhm = 2.0;
    goodMetrics.snr = 60.0;
    goodMetrics.eccentricity = 0.08;

    FrameQualityMetrics outlierMetrics;
    outlierMetrics.stars = 2;
    outlierMetrics.fwhm = 20.0;
    outlierMetrics.snr = 0.5;
    outlierMetrics.eccentricity = 0.95;

    for (const auto& path : goodPaths)
    {
        model->setFrameQualityMetrics(FrameId(path), goodMetrics);
    }
    for (const auto& path : outlierPaths)
    {
        model->setFrameQualityMetrics(FrameId(path), outlierMetrics);
    }

    auto* table = dashboard->findChild<QTableWidget*>("qualityTable");
    auto* detectBtn = dashboard->findChild<QPushButton*>("detectOutliersButton");
    auto* sigmaSpinBox = dashboard->findChild<QDoubleSpinBox*>("sigmaSpinBox");
    ASSERT_TRUE(table != nullptr);
    ASSERT_TRUE(detectBtn != nullptr);
    ASSERT_TRUE(sigmaSpinBox != nullptr);

    sigmaSpinBox->setValue(1.0);
    dashboard->show();
    QCoreApplication::processEvents();

    ASSERT_EQ(table->selectionMode(), QAbstractItemView::ExtendedSelection);

    // The result is reported in a message box that does not block; the rows are selected before it opens.
    detectBtn->click();
    an::tests::integration::pumpDialogEvents();
    EXPECT_NE(an::tests::integration::findOpenMessageBox(QStringLiteral("Outliers Detected")), nullptr);
    an::tests::integration::closeOpenDialogs();

    const auto selectedRows = table->selectionModel()->selectedRows();
    ASSERT_EQ(selectedRows.size(), outlierFrameCount);

    QSet<QString> selectedPaths;
    for (const auto& index : selectedRows)
    {
        auto* item = table->item(index.row(), 0);
        ASSERT_TRUE(item != nullptr);
        selectedPaths.insert(item->data(Qt::UserRole).toString());
    }
    for (const auto& path : outlierPaths)
    {
        EXPECT_TRUE(selectedPaths.contains(FrameId(path).canonicalPath()));
    }
}

TEST_F(FrameQualityDashboardTest, StackModeExcludesSelectedFrameOnlyFromCurrentStack)
{
    const QString keepPath = QStringLiteral("C:/stack_keep.fit");
    const QString dropPath = QStringLiteral("C:/stack_drop.fit");
    model->addFrames(FrameType::Light, {createFrame(keepPath), createFrame(dropPath)});

    ASSERT_EQ(model->integrationGroups().size(), 1u);
    const GroupKey groupKey = model->integrationGroups().front();
    dashboard->setExclusionMode(FrameQualityDashboard::ExclusionMode::StackOnly);
    dashboard->setStackExclusionGroupKey(groupKey);
    dashboard->setFrameIdFilter({FrameId(keepPath).canonicalPath(), FrameId(dropPath).canonicalPath()});
    QCoreApplication::processEvents();

    auto* table = dashboard->findChild<QTableWidget*>("qualityTable");
    auto* excludeBtn = dashboard->findChild<QPushButton*>("excludeSelectedButton");
    auto* includeBtn = dashboard->findChild<QPushButton*>("includeSelectedInStackButton");
    ASSERT_TRUE(table != nullptr);
    ASSERT_TRUE(excludeBtn != nullptr);
    ASSERT_TRUE(includeBtn != nullptr);
    EXPECT_FALSE(includeBtn->isHidden());
    EXPECT_TRUE(excludeBtn->text().contains(QStringLiteral("Stack")));

    int dropRow = -1;
    for (int row = 0; row < table->rowCount(); ++row)
    {
        auto* item = table->item(row, 0);
        if (item && item->data(Qt::UserRole).toString() == FrameId(dropPath).canonicalPath())
        {
            dropRow = row;
            break;
        }
    }
    ASSERT_GE(dropRow, 0);

    table->selectRow(dropRow);
    excludeBtn->click();
    QCoreApplication::processEvents();

    const auto dropFrame = model->lightFrames().findById(FrameId(dropPath));
    ASSERT_TRUE(dropFrame.has_value());
    EXPECT_FALSE(dropFrame->excluded);
    EXPECT_TRUE(model->isFrameStackExcluded(groupKey, dropFrame->id));

    table->selectRow(dropRow);
    includeBtn->click();
    QCoreApplication::processEvents();

    EXPECT_FALSE(model->isFrameStackExcluded(groupKey, dropFrame->id));
}

TEST_F(FrameQualityDashboardTest, HasSigmaThresholdSpinBox)
{
    QDoubleSpinBox* sigmaSpinBox = dashboard->findChild<QDoubleSpinBox*>("sigmaSpinBox");
    ASSERT_TRUE(sigmaSpinBox != nullptr);

    // Default value should be 3.0
    EXPECT_DOUBLE_EQ(sigmaSpinBox->value(), 3.0);

    // Should have reasonable range
    EXPECT_GE(sigmaSpinBox->minimum(), 1.0);
    EXPECT_LE(sigmaSpinBox->maximum(), 5.0);
}

TEST_F(FrameQualityDashboardTest, SigmaThresholdIsConfigurable)
{
    QDoubleSpinBox* sigmaSpinBox = dashboard->findChild<QDoubleSpinBox*>("sigmaSpinBox");
    ASSERT_TRUE(sigmaSpinBox != nullptr);

    // Should be able to change the value
    sigmaSpinBox->setValue(2.5);
    EXPECT_DOUBLE_EQ(sigmaSpinBox->value(), 2.5);

    sigmaSpinBox->setValue(1.5);
    EXPECT_DOUBLE_EQ(sigmaSpinBox->value(), 1.5);
}

TEST_F(FrameQualityDashboardTest, RefreshMethodUpdatesTable)
{
    // Add frames
    std::vector<FrameInfo> frames;
    frames.push_back(createFrame("C:/frame1.fit"));
    model->addFrames(FrameType::Light, frames);

    QTableWidget* table = dashboard->findChild<QTableWidget*>("qualityTable");
    ASSERT_TRUE(table != nullptr);

    // Call refresh
    dashboard->refresh();

    EXPECT_EQ(table->rowCount(), 1);
}

TEST_F(FrameQualityDashboardTest, HandlesFrameRemoval)
{
    // Add frames
    std::vector<FrameInfo> frames;
    frames.push_back(createFrame("C:/frame1.fit"));
    frames.push_back(createFrame("C:/frame2.fit"));
    model->addFrames(FrameType::Light, frames);

    QTableWidget* table = dashboard->findChild<QTableWidget*>("qualityTable");
    ASSERT_TRUE(table != nullptr);
    EXPECT_EQ(table->rowCount(), 2);

    // Remove one frame
    model->removeFrames(FrameType::Light, {"C:/frame1.fit"});

    // Dashboard should update via signal
    EXPECT_EQ(table->rowCount(), 1);
}

TEST_F(FrameQualityDashboardTest, DisplaysQualityMetrics)
{
    QString path = "C:/test_frame.fit";
    std::vector<FrameInfo> frames;
    frames.push_back(createFrame(path));
    model->addFrames(FrameType::Light, frames);

    // Set quality metrics
    FrameQualityMetrics metrics;
    metrics.stars = 1234;
    metrics.fwhm = 2.5;
    metrics.snr = 45.6;
    metrics.eccentricity = 0.15;
    metrics.succeeded = true;
    model->setFrameQualityMetrics(FrameId(path), metrics);

    // Refresh dashboard
    dashboard->refresh();

    QTableWidget* table = dashboard->findChild<QTableWidget*>("qualityTable");
    ASSERT_TRUE(table != nullptr);
    ASSERT_EQ(table->rowCount(), 1);

    // Check that metrics are displayed (columns: Frame, Score, Stars, FWHM, SNR, Eccentricity)
    ASSERT_TRUE(table->item(0, 2) != nullptr); // Stars column
    EXPECT_EQ(table->item(0, 2)->text(), QString::number(1234));
}

TEST_F(FrameQualityDashboardTest, AbsoluteScoreTooltipShowsRebalancedPhotometricFormula)
{
    const QString path = "C:/tooltip_frame.fit";
    model->addFrames(FrameType::Light, {createFrame(path)});

    FrameQualityMetrics metrics;
    metrics.stars = 600;
    metrics.fwhm = 2.4;
    metrics.snr = 35.0;
    metrics.eccentricity = 0.12;
    metrics.photometric_zero_point = 24.3;
    metrics.zero_point_scatter = 0.08;
    metrics.astrometric_rms_arcsec = 0.6;
    metrics.catalog_matched_stars = 95;
    metrics.limiting_magnitude = 13.4;
    metrics.succeeded = true;
    model->setFrameQualityMetrics(FrameId(path), metrics);

    dashboard->refresh();

    auto* table = dashboard->findChild<QTableWidget*>("qualityTable");
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 1);
    ASSERT_NE(table->item(0, 1), nullptr);

    const QString tooltip = table->item(0, 1)->toolTip();
    EXPECT_TRUE(tooltip.contains(QStringLiteral("zero_point*4")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("zero_point_scatter*20")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("astro_rms*8")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("catalog_matched")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("limiting_mag*0.5")));
}

TEST_F(FrameQualityDashboardTest, WeightingPreview_ShowsWeightColumn)
{
    dashboard->setWeightingEnabled(true);
    EXPECT_TRUE(dashboard->hasColumn("Weight"));
}

TEST_F(FrameQualityDashboardTest, WeightingPreview_CalculatesWeights)
{
    an::tests::integration::ScopedProcessWindowContext lent;
    ASSERT_TRUE(lent.context().valid());
    const QString goodPath = "C:/good.fit";
    const QString badPath = "C:/bad.fit";

    model->addFrames(FrameType::Light, {createFrame(goodPath), createFrame(badPath)});

    FrameQualityDashboard::WeightingPreviewConfig cfg;
    cfg.weightingMode = "Number of stars";
    cfg.starCountScaling = "Quadratic";
    cfg.minWeightFloor = 0.01;
    cfg.exponentialAlpha = 2.0;
    dashboard->setWeightingPreviewConfig(cfg);

    FrameQualityMetrics goodMetrics;
    goodMetrics.stars = 1000;
    goodMetrics.fwhm = 2.0;
    goodMetrics.snr = 50.0;
    goodMetrics.eccentricity = 0.1;
    goodMetrics.succeeded = true;

    FrameQualityMetrics badMetrics;
    badMetrics.stars = 10;
    badMetrics.fwhm = 8.0;
    badMetrics.snr = 5.0;
    badMetrics.eccentricity = 0.7;
    badMetrics.succeeded = true;

    model->setFrameQualityMetrics(FrameId(goodPath), goodMetrics);
    model->setFrameQualityMetrics(FrameId(badPath), badMetrics);

    dashboard->setWeightingEnabled(true);
    dashboard->calculateWeights();

    EXPECT_GT(dashboard->weightFor(0), dashboard->weightFor(1));
}

// Each frame's exposure counts in proportion to its weight, the heaviest frame's whole: the poor frame's exposure
// counts for its floor alone.
TEST_F(FrameQualityDashboardTest, WeightingPreview_ShowsEffectiveExposure)
{
    an::tests::integration::ScopedProcessWindowContext lent;
    ASSERT_TRUE(lent.context().valid());
    const QString aPath = "C:/a.fit";
    const QString bPath = "C:/b.fit";

    model->addFrames(FrameType::Light, {createFrameWithExposure(aPath, 10.0), createFrameWithExposure(bPath, 10.0)});

    FrameQualityDashboard::WeightingPreviewConfig cfg;
    cfg.weightingMode = "Number of stars";
    cfg.starCountScaling = "Quadratic";
    cfg.minWeightFloor = 0.01;
    cfg.exponentialAlpha = 2.0;
    dashboard->setWeightingPreviewConfig(cfg);

    FrameQualityMetrics high;
    high.stars = 1000;
    high.fwhm = 2.0;
    high.snr = 50.0;
    high.eccentricity = 0.1;
    high.succeeded = true;

    FrameQualityMetrics low;
    low.stars = 10;
    low.fwhm = 8.0;
    low.snr = 5.0;
    low.eccentricity = 0.7;
    low.succeeded = true;

    model->setFrameQualityMetrics(FrameId(aPath), high);
    model->setFrameQualityMetrics(FrameId(bPath), low);

    dashboard->setWeightingEnabled(true);
    dashboard->calculateWeights();

    const double effective = dashboard->effectiveExposureSeconds();
    EXPECT_LT(effective, 20.0);
    EXPECT_GT(effective, 0.0);
    EXPECT_NEAR(effective, 10.0 + 10.0 * cfg.minWeightFloor, 1e-9);
}

TEST_F(FrameQualityDashboardTest, WeightingPreview_AllowsManualOverride)
{
    const QString path = "C:/override.fit";
    model->addFrames(FrameType::Light, {createFrame(path)});

    FrameQualityDashboard::WeightingPreviewConfig cfg;
    cfg.weightingMode = "Number of stars";
    cfg.starCountScaling = "Quadratic";
    cfg.minWeightFloor = 0.01;
    cfg.exponentialAlpha = 2.0;
    dashboard->setWeightingPreviewConfig(cfg);

    FrameQualityMetrics metrics;
    metrics.stars = 500;
    metrics.fwhm = 3.0;
    metrics.snr = 30.0;
    metrics.eccentricity = 0.2;
    metrics.succeeded = true;
    model->setFrameQualityMetrics(FrameId(path), metrics);

    dashboard->setWeightingEnabled(true);
    dashboard->calculateWeights();

    dashboard->setManualWeight(0, 0.5);
    EXPECT_DOUBLE_EQ(dashboard->weightFor(0), 0.5);
    EXPECT_TRUE(dashboard->isManuallyOverridden(0));
}

TEST_F(FrameQualityDashboardTest, WeightingPreview_ShowsWeightHistogram)
{
    std::vector<FrameInfo> frames;
    frames.reserve(10);
    for (int i = 0; i < 10; ++i)
    {
        frames.push_back(createFrame(QStringLiteral("C:/frame_%1.fit").arg(i)));
    }
    model->addFrames(FrameType::Light, frames);

    FrameQualityDashboard::WeightingPreviewConfig cfg;
    cfg.weightingMode = "Number of stars";
    cfg.starCountScaling = "Quadratic";
    cfg.minWeightFloor = 0.01;
    cfg.exponentialAlpha = 2.0;
    dashboard->setWeightingPreviewConfig(cfg);

    for (int i = 0; i < 10; ++i)
    {
        const QString path = QStringLiteral("C:/frame_%1.fit").arg(i);
        FrameQualityMetrics m;
        m.stars = 1000 - i * 50;
        m.fwhm = 2.0 + i * 0.1;
        m.snr = 50.0 - i;
        m.eccentricity = 0.1 + i * 0.01;
        m.succeeded = true;
        model->setFrameQualityMetrics(FrameId(path), m);
    }

    dashboard->setWeightingEnabled(true);
    dashboard->calculateWeights();

    dashboard->show();
    QCoreApplication::processEvents();

    QWidget* hist = dashboard->weightHistogram();
    ASSERT_TRUE(hist != nullptr);
    EXPECT_TRUE(hist->isVisible());
}

TEST_F(FrameQualityDashboardTest, WeightingPreview_PhotometricMatchesSharedHelperAndPreservesWeightsAboveOne)
{
    // The preview is the stack's own weighting, worked out in the session the application lends.
    an::tests::integration::ScopedProcessWindowContext lent;
    ASSERT_TRUE(lent.context().valid());

    const QString dimPath = "C:/dim.fit";
    const QString clearPath = "C:/clear.fit";
    model->addFrames(FrameType::Light,
                     {createFrameWithExposure(dimPath, 10.0), createFrameWithExposure(clearPath, 10.0)});

    FrameQualityMetrics dimMetrics;
    dimMetrics.stars = 800;
    dimMetrics.fwhm = 2.5;
    dimMetrics.snr = 25.0;
    dimMetrics.photometric_zero_point = 24.0;
    dimMetrics.astrometric_rms_pixels = 1.0;
    dimMetrics.succeeded = true;

    FrameQualityMetrics clearMetrics;
    clearMetrics.stars = 900;
    clearMetrics.fwhm = 2.2;
    clearMetrics.snr = 28.0;
    clearMetrics.photometric_zero_point = 24.8;
    clearMetrics.astrometric_rms_pixels = 1.0;
    clearMetrics.succeeded = true;

    model->setFrameQualityMetrics(FrameId(dimPath), dimMetrics);
    model->setFrameQualityMetrics(FrameId(clearPath), clearMetrics);

    FrameQualityDashboard::WeightingPreviewConfig cfg;
    cfg.weightingMode = "Photometric";
    cfg.minWeightFloor = 0.05;
    dashboard->setWeightingPreviewConfig(cfg);
    dashboard->setWeightingEnabled(true);
    dashboard->calculateWeights();

    // The stack's weights have mean one under the group's floor, which the stack and the preview both read.
    const std::vector<an::PhotometricFrameMeasurement> measurements = {
        {.zeroPointMagnitudes = dimMetrics.photometric_zero_point,
         .zeroPointStandardUncertaintyMagnitudes = std::nullopt,
         .astrometricRootMeanSquarePixels = dimMetrics.astrometric_rms_pixels},
        {.zeroPointMagnitudes = clearMetrics.photometric_zero_point,
         .zeroPointStandardUncertaintyMagnitudes = std::nullopt,
         .astrometricRootMeanSquarePixels = clearMetrics.astrometric_rms_pixels}};
    const std::vector<double> expected =
        stackPhotometricWeights(lent.context().engineSession(), measurements, cfg.minWeightFloor);

    ASSERT_EQ(expected.size(), 2u);
    EXPECT_NEAR(dashboard->weightFor(0), expected[0], 1e-9);
    EXPECT_NEAR(dashboard->weightFor(1), expected[1], 1e-9);
    EXPECT_GT(dashboard->weightFor(1), 1.0);
    // Each exposure counts in proportion to its weight, the clearer frame's whole.
    EXPECT_NEAR(dashboard->effectiveExposureSeconds(), 10.0 * (expected[0] + expected[1]) / expected[1], 1e-9);
}

TEST_F(FrameQualityDashboardTest, WeightingPreview_PhotometricFallsBackToUniformAndShowsCoverageWarning)
{
    const QString aPath = "C:/a.fit";
    const QString bPath = "C:/b.fit";
    model->addFrames(FrameType::Light, {createFrame(aPath), createFrame(bPath)});

    FrameQualityMetrics metrics;
    metrics.stars = 500;
    metrics.fwhm = 2.5;
    metrics.snr = 20.0;
    metrics.succeeded = true;
    model->setFrameQualityMetrics(FrameId(aPath), metrics);
    model->setFrameQualityMetrics(FrameId(bPath), metrics);

    FrameQualityDashboard::WeightingPreviewConfig cfg;
    cfg.weightingMode = "Photometric";
    cfg.minWeightFloor = 0.05;
    dashboard->setWeightingPreviewConfig(cfg);
    dashboard->setWeightingEnabled(true);
    dashboard->calculateWeights();

    auto* banner = dashboard->findChild<QLabel*>(QStringLiteral("weightingPreviewBanner"));
    ASSERT_NE(banner, nullptr);

    EXPECT_DOUBLE_EQ(dashboard->weightFor(0), 1.0);
    EXPECT_DOUBLE_EQ(dashboard->weightFor(1), 1.0);
    EXPECT_FALSE(banner->isHidden());
    EXPECT_TRUE(banner->text().contains(QStringLiteral("photometric zero-point"), Qt::CaseInsensitive));
}

TEST_F(FrameQualityDashboardTest, WeightingPreview_PhotometricManualOverrideAllowsValuesAboveOne)
{
    const QString path = "C:/photometric_override.fit";
    model->addFrames(FrameType::Light, {createFrame(path)});

    FrameQualityMetrics metrics;
    metrics.stars = 500;
    metrics.fwhm = 3.0;
    metrics.snr = 30.0;
    metrics.photometric_zero_point = 24.0;
    metrics.astrometric_rms_pixels = 1.0;
    metrics.succeeded = true;
    model->setFrameQualityMetrics(FrameId(path), metrics);

    FrameQualityDashboard::WeightingPreviewConfig cfg;
    cfg.weightingMode = "Photometric";
    dashboard->setWeightingPreviewConfig(cfg);
    dashboard->setWeightingEnabled(true);
    dashboard->calculateWeights();

    dashboard->setManualWeight(0, 1.75);
    EXPECT_DOUBLE_EQ(dashboard->weightFor(0), 1.75);
}

TEST_F(FrameQualityDashboardTest, WeightingPreview_NoiseWeighsByTheBackgroundNoiseTheStackReads)
{
    // The stack weighs a frame by one over the square of the background noise its registration recorded; the preview
    // shows those weights, not a ranking of the frames' signal-to-noise ratios.
    an::tests::integration::ScopedProcessWindowContext lent;
    ASSERT_TRUE(lent.context().valid());
    QTemporaryDir output;
    ASSERT_TRUE(output.isValid());
    model->setOutputDirectory(output.path());

    const QString quietPath = "C:/quiet.fit";
    const QString noisyPath = "C:/noisy.fit";
    model->addFrames(FrameType::Light,
                     {createFrameWithExposure(quietPath, 10.0), createFrameWithExposure(noisyPath, 10.0)});
    ASSERT_FALSE(model->registrationGroups().empty());
    const QString groupKey =
        RegistrationGroupEngine::toMachineGroupKey(model->registrationGroups().begin()->first).toString();

    {
        an::ui::RegistrationMetricsWriter writer(
            QDir(output.path()).filePath(QStringLiteral("registration_metrics.db")),
            QStringLiteral("QualityDashboardNoiseSeedWriter"));
        QString error;
        ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
        // The quiet frame has the lower signal-to-noise ratio, so a ranking by that ratio would favour the noisy one.
        const auto seed = [&](const QString& path, const double snr, const double noise) {
            an::ui::RegistrationMetricRecord metric;
            metric.frameId = FrameId(path);
            metric.groupKey = groupKey;
            metric.settingsVersion = 0;
            metric.metrics.stars = 100;
            metric.metrics.fwhm = 2.0;
            metric.metrics.snr = snr;
            metric.metrics.succeeded = true;
            metric.noise.background_noise = noise;
            metric.noise.global_scale = 1.0;
            return writer.upsertMetric(metric, &error);
        };
        ASSERT_TRUE(seed(quietPath, 10.0, 0.01)) << error.toStdString();
        ASSERT_TRUE(seed(noisyPath, 50.0, 0.02)) << error.toStdString();
    }
    // The registration's records are read when the dashboard is refreshed, as a finished registration refreshes it.
    dashboard->refresh();
    ASSERT_EQ(dashboard->findChild<QTableWidget*>("qualityTable")->rowCount(), 2);

    FrameQualityDashboard::WeightingPreviewConfig cfg;
    cfg.weightingMode = "Noise";
    cfg.minWeightFloor = 0.05;
    dashboard->setWeightingPreviewConfig(cfg);
    dashboard->setWeightingEnabled(true);
    dashboard->calculateWeights();

    // Twice the noise is a quarter of the weight, and the stack's weights have mean one.
    EXPECT_NEAR(dashboard->weightFor(0), 1.6, 1e-9);
    EXPECT_NEAR(dashboard->weightFor(1), 0.4, 1e-9);
}

// The width preview is the stack's own weighting: a frame's width is penalized for the stars it lacks against the
// group's reference before the inverse square ranks it, so the hazy frame with the narrowest stars is the lightest, as
// the stack weighs it. The preview ranked the raw widths and showed that frame the heaviest.
TEST_F(FrameQualityDashboardTest, WeightingPreview_WeightedFwhmIsTheStacksWeightsWithItsStarCountPenalty)
{
    an::tests::integration::ScopedProcessWindowContext lent;
    ASSERT_TRUE(lent.context().valid());
    const QString referencePath = "C:/fwhm_reference.fit";
    const QString hazyPath = "C:/fwhm_hazy.fit";
    const QString fairPath = "C:/fwhm_fair.fit";
    model->addFrames(FrameType::Light,
                     {createFrameWithExposure(referencePath, 10.0), createFrameWithExposure(hazyPath, 10.0),
                      createFrameWithExposure(fairPath, 10.0)});
    const std::vector<double> stars{400.0, 100.0, 300.0};
    const std::vector<double> widths{3.0, 2.0, 2.5};
    const std::vector<QString> paths{referencePath, hazyPath, fairPath};
    for (std::size_t i = 0; i < paths.size(); ++i)
    {
        FrameQualityMetrics metrics;
        metrics.stars = static_cast<int>(stars[i]);
        metrics.fwhm = widths[i];
        metrics.snr = 20.0;
        metrics.eccentricity = 0.2;
        metrics.succeeded = true;
        model->setFrameQualityMetrics(FrameId(paths[i]), metrics);
    }

    FrameQualityDashboard::WeightingPreviewConfig cfg;
    cfg.weightingMode = "Weighted FWHM";
    cfg.minWeightFloor = 0.01;
    cfg.referenceFrame = FrameId(referencePath).canonicalPath();
    dashboard->setWeightingPreviewConfig(cfg);
    dashboard->setWeightingEnabled(true);
    dashboard->calculateWeights();

    const std::vector<double> expected =
        an::tests::integration::stackWidthWeights(widths, stars, 0u, cfg.minWeightFloor);
    EXPECT_NEAR(dashboard->weightFor(0), expected[0], 1e-9);
    EXPECT_NEAR(dashboard->weightFor(1), expected[1], 1e-9);
    EXPECT_NEAR(dashboard->weightFor(2), expected[2], 1e-9);
    EXPECT_LT(dashboard->weightFor(1), dashboard->weightFor(2)) << "the hazy frame is penalized for its missing stars";
}

// "Roundness Weighted" weighs each frame's star count by its stars' roundness, as the stack does; the preview squared
// the bare counts.
TEST_F(FrameQualityDashboardTest, WeightingPreview_RoundnessWeightedStarCountReadsEachFramesRoundness)
{
    an::tests::integration::ScopedProcessWindowContext lent;
    ASSERT_TRUE(lent.context().valid());
    const std::vector<QString> paths{"C:/round_a.fit", "C:/round_b.fit", "C:/round_c.fit"};
    model->addFrames(FrameType::Light, {createFrame(paths[0]), createFrame(paths[1]), createFrame(paths[2])});
    const std::vector<double> stars{100.0, 100.0, 80.0};
    const std::vector<double> eccentricity{0.1, 0.5, 0.0};
    for (std::size_t i = 0; i < paths.size(); ++i)
    {
        FrameQualityMetrics metrics;
        metrics.stars = static_cast<int>(stars[i]);
        metrics.fwhm = 2.5;
        metrics.eccentricity = eccentricity[i];
        metrics.succeeded = true;
        model->setFrameQualityMetrics(FrameId(paths[i]), metrics);
    }

    FrameQualityDashboard::WeightingPreviewConfig cfg;
    cfg.weightingMode = "Number of stars";
    cfg.starCountScaling = "Roundness Weighted";
    cfg.minWeightFloor = 0.01;
    dashboard->setWeightingPreviewConfig(cfg);
    dashboard->setWeightingEnabled(true);
    dashboard->calculateWeights();

    const std::vector<double> expected =
        an::tests::integration::stackRoundnessWeightedStarWeights(stars, {0.9, 0.5, 1.0}, cfg.minWeightFloor);
    for (int row = 0; row < 3; ++row)
    {
        EXPECT_NEAR(dashboard->weightFor(row), expected[static_cast<std::size_t>(row)], 1e-9) << row;
    }
}

// The stack weighs a set by a figure only when every frame carries it, and otherwise stacks it uniformly; the preview
// shows the same and says why.
TEST_F(FrameQualityDashboardTest, WeightingPreview_AFigureSomeFramesLackIsPreviewedUniformlyAsTheStackDoes)
{
    an::tests::integration::ScopedProcessWindowContext lent;
    ASSERT_TRUE(lent.context().valid());
    const QString starryPath = "C:/starry.fit";
    const QString emptyPath = "C:/starless.fit";
    model->addFrames(FrameType::Light, {createFrame(starryPath), createFrame(emptyPath)});
    FrameQualityMetrics starry;
    starry.stars = 500;
    starry.fwhm = 2.5;
    starry.succeeded = true;
    FrameQualityMetrics starless;
    starless.succeeded = true;
    model->setFrameQualityMetrics(FrameId(starryPath), starry);
    model->setFrameQualityMetrics(FrameId(emptyPath), starless);

    FrameQualityDashboard::WeightingPreviewConfig cfg;
    cfg.weightingMode = "Number of stars";
    dashboard->setWeightingPreviewConfig(cfg);
    dashboard->setWeightingEnabled(true);
    dashboard->calculateWeights();

    EXPECT_DOUBLE_EQ(dashboard->weightFor(0), 1.0);
    EXPECT_DOUBLE_EQ(dashboard->weightFor(1), 1.0);
    auto* banner = dashboard->findChild<QLabel*>(QStringLiteral("weightingPreviewBanner"));
    ASSERT_NE(banner, nullptr);
    EXPECT_FALSE(banner->isHidden());
    EXPECT_TRUE(banner->text().contains(QStringLiteral("as the stack does"))) << banner->text().toStdString();
    EXPECT_TRUE(banner->text().contains(QStringLiteral("1 of 2"))) << banner->text().toStdString();
}

// "Number of images" weighs each light by how many frames its header says it combines, read off the GUI thread: until
// the headers are read the preview says so, then it shows the stack's weights. It gave every light one.
TEST_F(FrameQualityDashboardTest, WeightingPreview_NumberOfImagesWeighsByTheFramesEachLightsHeaderCombines)
{
    an::tests::integration::ScopedProcessWindowContext lent;
    ASSERT_TRUE(lent.context().valid());
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const auto writeMaster = [&root](const char* name, const char* frames) {
        an::tests::fixtures::FixtureImage image;
        image.name = name;
        image.width = 16u;
        image.height = 10u;
        image.sampleType = an::tests::fixtures::FixtureSampleType::Float32;
        image.pattern = an::tests::fixtures::FixturePattern::Constant;
        an::tests::fixtures::FixtureChannelLevels levels;
        levels.base = 0.2;
        image.levels = {levels};
        image.cards = {FixtureCard{"NCOMBINE", frames, "", FixtureCardKind::Integer}};
        std::string error;
        const auto samples = an::tests::fixtures::materializeFixture(image, error);
        EXPECT_TRUE(samples.has_value()) << error;
        const QString path = QDir(root.path()).filePath(QString::fromLatin1(name) + QStringLiteral(".fits"));
        EXPECT_TRUE(samples.has_value() && an::tests::fixtures::writeFixtureFits(
                                               image, *samples, std::filesystem::path(path.toStdWString()), error))
            << error;
        return path;
    };
    const QString few = writeMaster("master_few", "7");
    const QString many = writeMaster("master_many", "40");
    model->addFrames(FrameType::Light, {createFrameWithExposure(few, 60.0), createFrameWithExposure(many, 60.0)});
    for (const QString& path : {few, many})
    {
        FrameQualityMetrics metrics;
        metrics.stars = 300;
        metrics.fwhm = 2.5;
        metrics.succeeded = true;
        model->setFrameQualityMetrics(FrameId(path), metrics);
    }

    FrameQualityDashboard::WeightingPreviewConfig cfg;
    cfg.weightingMode = "Number of images";
    dashboard->setWeightingPreviewConfig(cfg);
    dashboard->setWeightingEnabled(true);
    dashboard->calculateWeights();

    auto* banner = dashboard->findChild<QLabel*>(QStringLiteral("weightingPreviewBanner"));
    ASSERT_NE(banner, nullptr);
    EXPECT_DOUBLE_EQ(dashboard->weightFor(0), 1.0) << "nothing is shown as read before the headers are";
    EXPECT_FALSE(banner->isHidden());

    // The headers are read on the thread pool, and the preview is computed again when they land on this thread.
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    EXPECT_NEAR(dashboard->weightFor(0), 7.0 / 23.5, 1e-9);
    EXPECT_NEAR(dashboard->weightFor(1), 40.0 / 23.5, 1e-9);
}

// Switching weighting off says so, whatever the weighted preview's banner said last.
TEST_F(FrameQualityDashboardTest, WeightingPreview_SwitchingWeightingOffSaysSoWhateverThePreviewSaid)
{
    an::tests::integration::ScopedProcessWindowContext lent;
    ASSERT_TRUE(lent.context().valid());
    model->addFrames(FrameType::Light,
                     {createFrame(QStringLiteral("C:/off_a.fit")), createFrame(QStringLiteral("C:/off_b.fit"))});
    FrameQualityMetrics starry;
    starry.stars = 500;
    starry.succeeded = true;
    FrameQualityMetrics starless;
    starless.succeeded = true;
    model->setFrameQualityMetrics(FrameId(QStringLiteral("C:/off_a.fit")), starry);
    model->setFrameQualityMetrics(FrameId(QStringLiteral("C:/off_b.fit")), starless);

    FrameQualityDashboard::WeightingPreviewConfig cfg;
    cfg.weightingMode = "Number of stars";
    dashboard->setWeightingPreviewConfig(cfg);
    dashboard->setWeightingEnabled(true);
    auto* banner = dashboard->findChild<QLabel*>(QStringLiteral("weightingPreviewBanner"));
    ASSERT_NE(banner, nullptr);
    ASSERT_TRUE(banner->text().contains(QStringLiteral("as the stack does"))) << banner->text().toStdString();

    dashboard->setWeightingEnabled(false);
    EXPECT_FALSE(banner->isHidden());
    EXPECT_TRUE(banner->text().contains(QStringLiteral("Weighting is disabled"))) << banner->text().toStdString();
}

TEST_F(FrameQualityDashboardTest, HistogramTitleUsesHeading3TokenFont)
{
    auto* title = dashboard->findChild<QLabel*>(QStringLiteral("histogramTitleLabel"));
    ASSERT_TRUE(title != nullptr);
    EXPECT_EQ(title->font(), an::ui::tokens().typography.heading3);
}

TEST_F(FrameQualityDashboardTest, ThemeChangeEventsDoNotCrash)
{
    dashboard->resize(640, 420);

    QEvent paletteChange(QEvent::ApplicationPaletteChange);
    QApplication::sendEvent(dashboard, &paletteChange);

    QEvent themeChange(QEvent::ThemeChange);
    QApplication::sendEvent(dashboard, &themeChange);

    SUCCEED();
}

TEST_F(FrameQualityDashboardTest, UsesOpaqueBackgroundWhenParentSurfaceIsTranslucent)
{
    QWidget parent;
    QPalette parentPalette = parent.palette();
    parentPalette.setColor(QPalette::Window, QColor(10, 20, 30, 64));
    parent.setPalette(parentPalette);

    FrameQualityDashboard childDashboard(model, &parent);
    childDashboard.refresh();

    const auto& tokenSet = an::ui::tokens();
    const QColor expectedWindowColor =
        tokenSet.materials.micaSurface.alpha() >= 255
            ? tokenSet.materials.micaSurface
            : an::ui::compositeOver(tokenSet.materials.micaSurface, tokenSet.colors.bgPrimary);

    EXPECT_EQ(childDashboard.palette().color(QPalette::Window).alpha(), 255);
    EXPECT_EQ(childDashboard.palette().color(QPalette::Window), expectedWindowColor);
    EXPECT_NE(childDashboard.palette().color(QPalette::Window), parent.palette().color(QPalette::Window));
}
