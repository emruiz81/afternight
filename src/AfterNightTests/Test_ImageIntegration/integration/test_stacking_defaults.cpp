// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file test_stacking_defaults.cpp
 * @brief Comprehensive tests for stacking default parameters across all frame types.
 *
 * These tests pin the expected default values for:
 *   - StackingSettingsDialog::resetToDefaults()  (stacking settings dialog)
 *   - defaultCalibrationStackingSettings()        (V2 calibration panels)
 *   - StackingSettings struct defaults             (V2 model)
 *   - LocalNormalizationSettings struct defaults   (V2 model)
 *   - DrizzleSettings struct defaults              (V2 model)
 *
 * StackingSettings expansion checklist:
 *   - IntegrationDataModel JSON
 *   - WorkflowManifest JSON
 *   - ManifestHashing
 *   - ManifestDiff
 *   - defaults/persistence regression tests
 *
 * Changing any default without updating these tests will cause a failure,
 * ensuring intentional review of every parameter change.
 */

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <utility>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSize>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/controls/LocalNormalizationSettingsDialog.h"
#include "processes/ImageIntegration/controls/StackingSettingsDialog.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/tabs/CalibrationPanelUtils.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/core/RegistrationMetricsWriter.h"

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>

namespace
{
void ensureQApp()
{
    if (!QApplication::instance())
    {
        static int argc = 0;
        static char* argv[] = {nullptr};
        static QApplication app(argc, argv);
    }
}

FrameInfo makeLightFrame(const QString& path, const QString& objectName = QStringLiteral("M42"))
{
    FrameMetadata metadata(FrameType::Light, QSize(1000, 800), 60.0, std::nullopt, 100, 10, QStringLiteral("L"),
                           objectName, QDateTime::currentDateTimeUtc(), QStringLiteral("Normal"));
    return FrameInfo(FrameId(path), path, metadata);
}

/// A registration's transform record: a translation onto the reference, written as the registration writes one.
QString transformJson(double tx, double ty)
{
    const an::Result<an::RegistrationTransform> transform = an::RegistrationTransform::create(
        an::RegistrationTransformRequest{.linearKind = an::RegistrationTransformKind::Similarity,
                                         .sourceToReference = {1.0, 0.0, tx, 0.0, 1.0, ty, 0.0, 0.0, 1.0}});
    EXPECT_TRUE(transform.hasValue()) << transform.status().message();
    return transform.hasValue() ? stage_utils::serializeTransformJson(*transform) : QString();
}

/// A second-order polynomial transform record whose linear part is the translation and whose polynomial is the
/// identity over a 1000 x 800 frame.
QString polynomialTransformJson(double tx, double ty)
{
    an::RegistrationTransformRequest request{.linearKind = an::RegistrationTransformKind::Affine,
                                             .sourceToReference = {1.0, 0.0, tx, 0.0, 1.0, ty, 0.0, 0.0, 1.0}};
    request.polynomial.order = 2u;
    request.polynomial.x = {0.0, 1.0, 0.0, 0.0, 0.0, 0.0};
    request.polynomial.y = {0.0, 0.0, 1.0, 0.0, 0.0, 0.0};
    request.polynomial.normalizationOrigin = {0.0, 0.0};
    request.polynomial.normalizationScale = {1000.0, 800.0};
    const an::Result<an::RegistrationTransform> transform = an::RegistrationTransform::create(request);
    EXPECT_TRUE(transform.hasValue()) << transform.status().message();
    return transform.hasValue() ? stage_utils::serializeTransformJson(*transform) : QString();
}

void writeMetric(an::ui::RegistrationMetricsWriter& writer, const QString& framePath, const QString& groupKey,
                 int settingsVersion, const QString& transform, qint64 updatedAt, double fwhm = 2.0,
                 double zeroPoint = 25.0, bool succeeded = true)
{
    an::ui::RegistrationMetricRecord record;
    record.frameId = FrameId(framePath);
    record.groupKey = groupKey;
    record.settingsVersion = settingsVersion;
    record.metrics.stars = 50;
    record.metrics.fwhm = fwhm;
    record.metrics.snr = 20.0;
    record.metrics.eccentricity = 0.2;
    record.metrics.photometric_zero_point = zeroPoint;
    record.metrics.zero_point_scatter = 0.03;
    record.metrics.astrometric_rms_pixels = 0.4;
    record.metrics.astrometric_rms_arcsec = 0.8;
    record.metrics.limiting_magnitude = 18.5;
    record.metrics.catalog_matched_stars = 40;
    record.metrics.succeeded = succeeded;
    record.transformJson = transform;
    record.updatedAt = updatedAt;

    QString error;
    ASSERT_TRUE(writer.upsertMetric(record, &error)) << error.toStdString();
}
} // namespace

// =============================================================================
// 1. StackingSettingsDialog – resetToDefaults() per frame type
// =============================================================================

class DialogResetDefaultsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ensureQApp();
    }
};

// ---- BIAS ----

TEST_F(DialogResetDefaultsTest, Bias_StackMethod)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::BIAS, QStringLiteral("Bias"));
    d.resetToDefaults();
    EXPECT_EQ(d.getStackMethod(), QStringLiteral("Average"));
}

TEST_F(DialogResetDefaultsTest, Bias_Normalization)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::BIAS, QStringLiteral("Bias"));
    d.resetToDefaults();
    EXPECT_EQ(d.getNormalizationMode(), QStringLiteral("None"));
}

TEST_F(DialogResetDefaultsTest, Bias_RejectionMethod)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::BIAS, QStringLiteral("Bias"));
    d.resetToDefaults();
    EXPECT_EQ(d.getRejectionMethod(), QStringLiteral("Sigma Clipping"));
}

TEST_F(DialogResetDefaultsTest, Bias_SigmaValues)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::BIAS, QStringLiteral("Bias"));
    d.resetToDefaults();
    EXPECT_DOUBLE_EQ(d.getSigmaLow(), 3.0);
    EXPECT_DOUBLE_EQ(d.getSigmaHigh(), 3.0);
}

TEST_F(DialogResetDefaultsTest, Bias_Weighting)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::BIAS, QStringLiteral("Bias"));
    d.resetToDefaults();
    EXPECT_EQ(d.getWeighting(), QStringLiteral("None"));
}

TEST_F(DialogResetDefaultsTest, Bias_RejectionMaps)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::BIAS, QStringLiteral("Bias"));
    d.resetToDefaults();
    EXPECT_FALSE(d.getCreateRejectionMaps());
    EXPECT_FALSE(d.getMergeRejectionMaps());
}

// ---- DARK ----

TEST_F(DialogResetDefaultsTest, Dark_StackMethod)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::DARK, QStringLiteral("Dark"));
    d.resetToDefaults();
    EXPECT_EQ(d.getStackMethod(), QStringLiteral("Average"));
}

TEST_F(DialogResetDefaultsTest, Dark_Normalization)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::DARK, QStringLiteral("Dark"));
    d.resetToDefaults();
    EXPECT_EQ(d.getNormalizationMode(), QStringLiteral("None"));
}

TEST_F(DialogResetDefaultsTest, Dark_RejectionMethod)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::DARK, QStringLiteral("Dark"));
    d.resetToDefaults();
    EXPECT_EQ(d.getRejectionMethod(), QStringLiteral("Sigma Clipping"));
}

TEST_F(DialogResetDefaultsTest, Dark_SigmaValues)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::DARK, QStringLiteral("Dark"));
    d.resetToDefaults();
    EXPECT_DOUBLE_EQ(d.getSigmaLow(), 3.0);
    EXPECT_DOUBLE_EQ(d.getSigmaHigh(), 3.0);
}

TEST_F(DialogResetDefaultsTest, Dark_Weighting)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::DARK, QStringLiteral("Dark"));
    d.resetToDefaults();
    EXPECT_EQ(d.getWeighting(), QStringLiteral("None"));
}

TEST_F(DialogResetDefaultsTest, Dark_RejectionMaps)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::DARK, QStringLiteral("Dark"));
    d.resetToDefaults();
    EXPECT_FALSE(d.getCreateRejectionMaps());
    EXPECT_FALSE(d.getMergeRejectionMaps());
}

// ---- FLAT ----

TEST_F(DialogResetDefaultsTest, Flat_StackMethod)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::FLAT, QStringLiteral("Flat"));
    d.resetToDefaults();
    EXPECT_EQ(d.getStackMethod(), QStringLiteral("Average"));
}

TEST_F(DialogResetDefaultsTest, Flat_Normalization)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::FLAT, QStringLiteral("Flat"));
    d.resetToDefaults();
    EXPECT_EQ(d.getNormalizationMode(), QStringLiteral("Multiplicative"));
}

TEST_F(DialogResetDefaultsTest, Flat_RejectionMethod)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::FLAT, QStringLiteral("Flat"));
    d.resetToDefaults();
    EXPECT_EQ(d.getRejectionMethod(), QStringLiteral("Sigma Clipping"));
}

TEST_F(DialogResetDefaultsTest, Flat_SigmaValues)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::FLAT, QStringLiteral("Flat"));
    d.resetToDefaults();
    EXPECT_DOUBLE_EQ(d.getSigmaLow(), 3.0);
    EXPECT_DOUBLE_EQ(d.getSigmaHigh(), 3.0);
}

TEST_F(DialogResetDefaultsTest, Flat_Weighting)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::FLAT, QStringLiteral("Flat"));
    d.resetToDefaults();
    EXPECT_EQ(d.getWeighting(), QStringLiteral("None"));
}

TEST_F(DialogResetDefaultsTest, Flat_RejectionMaps)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::FLAT, QStringLiteral("Flat"));
    d.resetToDefaults();
    EXPECT_FALSE(d.getCreateRejectionMaps());
    EXPECT_FALSE(d.getMergeRejectionMaps());
}

// A bias or dark master is combined without normalization, so its dialog shows None and offers no other, whatever a
// caller sets; a flat's normalization is its own to choose.
TEST_F(DialogResetDefaultsTest, BiasAndDarkNormalizationIsFixedAtNoneAndAFlatsIsOffered)
{
    for (const auto type : {StackingSettingsDialog::FrameType::BIAS, StackingSettingsDialog::FrameType::DARK})
    {
        StackingSettingsDialog d(type, QStringLiteral("Calibration"));
        d.setNormalizationMode(QStringLiteral("Multiplicative"));
        EXPECT_EQ(d.getNormalizationMode(), QStringLiteral("None"));
        EXPECT_EQ(d.getCurrentSettings().normalizationMode, NormalizationMode::None);
        auto* normalization = d.findChild<QComboBox*>(QStringLiteral("normalizationMode"));
        ASSERT_NE(normalization, nullptr);
        EXPECT_FALSE(normalization->isEnabled());
        EXPECT_TRUE(normalization->toolTip().contains(QStringLiteral("without normalization")))
            << normalization->toolTip().toStdString();
    }

    StackingSettingsDialog flat(StackingSettingsDialog::FrameType::FLAT, QStringLiteral("Flat"));
    flat.setNormalizationMode(QStringLiteral("Multiplicative With Scaling"));
    EXPECT_EQ(flat.getNormalizationMode(), QStringLiteral("Multiplicative With Scaling"));
    auto* normalization = flat.findChild<QComboBox*>(QStringLiteral("normalizationMode"));
    ASSERT_NE(normalization, nullptr);
    EXPECT_TRUE(normalization->isEnabled());
}

// ---- LIGHT ----

TEST_F(DialogResetDefaultsTest, Light_StackMethod)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    EXPECT_EQ(d.getStackMethod(), QStringLiteral("Average"));
}

TEST_F(DialogResetDefaultsTest, Light_Normalization)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    EXPECT_EQ(d.getNormalizationMode(), QStringLiteral("Additive With Scaling"));
}

TEST_F(DialogResetDefaultsTest, Light_RejectionMethod)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    EXPECT_EQ(d.getRejectionMethod(), QStringLiteral("Winsorized Sigma Clipping"));
}

TEST_F(DialogResetDefaultsTest, Light_SigmaValues)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    EXPECT_DOUBLE_EQ(d.getSigmaLow(), 3.0);
    EXPECT_DOUBLE_EQ(d.getSigmaHigh(), 3.0);
}

TEST_F(DialogResetDefaultsTest, Light_Weighting)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    EXPECT_EQ(d.getWeighting(), QStringLiteral("None"));
}

TEST_F(DialogResetDefaultsTest, Light_RejectionMaps)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    EXPECT_FALSE(d.getCreateRejectionMaps());
    EXPECT_FALSE(d.getMergeRejectionMaps());
}

TEST_F(DialogResetDefaultsTest, Light_WeightingOptions)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    EXPECT_EQ(d.getStarCountScaling(), QStringLiteral("Quadratic"));
    EXPECT_DOUBLE_EQ(d.getExponentialAlpha(), 2.0);
    EXPECT_DOUBLE_EQ(d.getMinWeightFloor(), 0.01);
}

TEST_F(DialogResetDefaultsTest, Light_PhotometricWeightingOptionCanBeDisabledWhenDataIsUnavailable)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    d.setWeighting(QStringLiteral("Photometric"));
    d.setPhotometricWeightingAvailable(false);

    auto* weightingSelector = d.findChild<QComboBox*>(QStringLiteral("weightingSelector"));
    ASSERT_NE(weightingSelector, nullptr);

    const int photometricIndex =
        weightingSelector->findText(QStringLiteral("Photometric (requires photometric zero-point data)"));
    ASSERT_GE(photometricIndex, 0);

    EXPECT_EQ(d.getWeighting(), QStringLiteral("None"));
    EXPECT_FALSE(weightingSelector->model()
                     ->flags(weightingSelector->model()->index(photometricIndex, 0))
                     .testFlag(Qt::ItemIsEnabled));
}

TEST_F(DialogResetDefaultsTest, Light_SpatialWeightingDefaults)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    EXPECT_FALSE(d.getLocalWeightingEnabled());
    EXPECT_EQ(d.getLocalWeightingWindowSize(), 5);
    EXPECT_EQ(d.getLocalWeightingGridSpacing(), 64);
    EXPECT_DOUBLE_EQ(d.getLocalWeightingSnrWeightExponent(), 2.0);
}

TEST_F(DialogResetDefaultsTest, Light_WeightingOptionsUseSecondaryDialogAndRequireEnabledWeighting)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    d.show();
    QApplication::processEvents();

    auto* weightingSelector = d.findChild<QComboBox*>(QStringLiteral("weightingSelector"));
    auto* weightingOptionsButton = d.findChild<QPushButton*>(QStringLiteral("weightingOptionsButton"));

    ASSERT_NE(weightingSelector, nullptr);
    ASSERT_NE(weightingOptionsButton, nullptr);

    EXPECT_FALSE(weightingOptionsButton->isEnabled());

    weightingSelector->setCurrentText(QStringLiteral("Number of stars"));
    QApplication::processEvents();

    EXPECT_TRUE(weightingOptionsButton->isEnabled());

    QTest::mouseClick(weightingOptionsButton, Qt::LeftButton);
    QApplication::processEvents();

    auto* weightingOptionsDialog = d.findChild<QDialog*>(QStringLiteral("stackingWeightingOptionsDialog"));
    ASSERT_NE(weightingOptionsDialog, nullptr);
    EXPECT_TRUE(weightingOptionsDialog->isVisible());
    EXPECT_EQ(weightingOptionsDialog->windowTitle(), QStringLiteral("Weighting Options"));
}

TEST_F(DialogResetDefaultsTest, Light_CancelingWeightingOptionsDialogRestoresPreviousValues)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    d.show();
    QApplication::processEvents();

    auto* weightingSelector = d.findChild<QComboBox*>(QStringLiteral("weightingSelector"));
    auto* weightingOptionsButton = d.findChild<QPushButton*>(QStringLiteral("weightingOptionsButton"));
    ASSERT_NE(weightingSelector, nullptr);
    ASSERT_NE(weightingOptionsButton, nullptr);

    weightingSelector->setCurrentText(QStringLiteral("Number of stars"));
    QApplication::processEvents();
    ASSERT_TRUE(weightingOptionsButton->isEnabled());

    const double originalMinWeightFloor = d.getMinWeightFloor();

    QTest::mouseClick(weightingOptionsButton, Qt::LeftButton);
    QApplication::processEvents();

    auto* weightingOptionsDialog = d.findChild<QDialog*>(QStringLiteral("stackingWeightingOptionsDialog"));
    auto* minWeightFloor = d.findChild<QDoubleSpinBox*>(QStringLiteral("minWeightFloorValue"));
    auto* buttonBox =
        weightingOptionsDialog
            ? weightingOptionsDialog->findChild<QDialogButtonBox*>(QStringLiteral("weightingOptionsButtonBox"))
            : nullptr;
    ASSERT_NE(weightingOptionsDialog, nullptr);
    ASSERT_NE(minWeightFloor, nullptr);
    ASSERT_NE(buttonBox, nullptr);
    ASSERT_NE(buttonBox->button(QDialogButtonBox::Cancel), nullptr);

    minWeightFloor->setValue(originalMinWeightFloor + 0.20);
    QApplication::processEvents();

    QTest::mouseClick(buttonBox->button(QDialogButtonBox::Cancel), Qt::LeftButton);
    QApplication::processEvents();

    EXPECT_DOUBLE_EQ(d.getMinWeightFloor(), originalMinWeightFloor);
}

TEST_F(DialogResetDefaultsTest, Light_DisabledWeightingDoesNotExposeWeightingOptionsInSettings)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();

    d.setWeighting(QStringLiteral("Weighted FWHM"));
    d.setLocalWeightingEnabled(true);

    ASSERT_TRUE(d.getLocalWeightingEnabled());

    d.setWeighting(QStringLiteral("None"));

    const auto settings = d.getCurrentSettings();
    EXPECT_FALSE(d.getLocalWeightingEnabled());
    EXPECT_FALSE(settings.localWeighting.enabled);
}

// Every channel of a colour stack is weighed by its light's one star width: weighed by red, green and blue widths of
// their own, two real sessions' weights moved by a few percent and their stacks' star widths by a third of a percent at
// most, so the weighting options offer no per-channel width weighting under any weighting. They offered a control that
// was always disabled and changed no weight.
TEST_F(DialogResetDefaultsTest, Light_TheWeightingOptionsOfferNoPerChannelWidthWeighting)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    StackingSettings stored;
    stored.weighting = QStringLiteral("Weighted FWHM");
    applyStackingSettingsToDialog(stored, d);

    for (const QString& weighting :
         {QStringLiteral("Weighted FWHM"), QStringLiteral("Number of stars"), QStringLiteral("Noise")})
    {
        d.setWeighting(weighting);
        EXPECT_EQ(d.findChild<QCheckBox*>(QStringLiteral("perChannelFwhmWeighting")), nullptr)
            << weighting.toStdString();
        for (const QCheckBox* option : d.findChildren<QCheckBox*>())
        {
            EXPECT_FALSE(option->text().contains(QStringLiteral("Per-Channel"), Qt::CaseInsensitive))
                << option->objectName().toStdString();
        }
    }
    // The options it sat among are still offered.
    EXPECT_NE(d.findChild<QDoubleSpinBox*>(QStringLiteral("minWeightFloorValue")), nullptr);
    EXPECT_NE(d.findChild<QCheckBox*>(QStringLiteral("localWeightingEnabled")), nullptr);
}

// The stack raises a star-count, width or photometric weight to the floor and no other - a noise or image-count
// weight is a ratio that needs none - so the floor is offered beside those three weightings alone and its tooltip names
// them. It was offered for every weighting, and changed nothing under Noise or Number of images.
TEST_F(DialogResetDefaultsTest, Light_TheWeightFloorIsOfferedOnlyWhereTheStackRaisesAWeightToIt)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    d.setPhotometricWeightingAvailable(true);
    auto* floor = d.findChild<QDoubleSpinBox*>(QStringLiteral("minWeightFloorValue"));
    auto* floorLabel = d.findChild<QLabel*>(QStringLiteral("minWeightFloorLabel"));
    ASSERT_NE(floor, nullptr);
    ASSERT_NE(floorLabel, nullptr);

    const std::array<std::pair<QString, bool>, 5> weightings{
        std::pair{QStringLiteral("Number of stars"), true}, std::pair{QStringLiteral("Weighted FWHM"), true},
        std::pair{QStringLiteral("Photometric"), true}, std::pair{QStringLiteral("Noise"), false},
        std::pair{QStringLiteral("Number of images"), false}};
    for (const auto& [weighting, offered] : weightings)
    {
        d.setWeighting(weighting);
        ASSERT_EQ(d.getWeighting(), weighting);
        EXPECT_EQ(floor->isEnabled(), offered) << weighting.toStdString();
        EXPECT_EQ(floorLabel->isEnabled(), offered) << weighting.toStdString();
    }
    for (const QString& named : {QStringLiteral("Number of stars"), QStringLiteral("Weighted FWHM"),
                                 QStringLiteral("Photometric"), QStringLiteral("Noise")})
    {
        EXPECT_TRUE(floor->toolTip().contains(named)) << named.toStdString() << ": " << floor->toolTip().toStdString();
    }

    // The stored floor is kept under a weighting that does not read it.
    d.setWeighting(QStringLiteral("Number of stars"));
    d.setMinWeightFloor(0.2);
    d.setWeighting(QStringLiteral("Noise"));
    EXPECT_DOUBLE_EQ(d.getCurrentSettings().minWeightFloor, 0.2);
}

// Percentile rejects a sample further below the median than its low fraction of the median, or further above than its
// high fraction: it starts at the engine's 0.2 and 0.1 and its controls say what they are. It started at 1 and 1 -
// which rejects nothing below the median of non-negative data - and its tooltip spoke of a percentile range.
TEST_F(DialogResetDefaultsTest, Light_PercentileStartsAtTheEnginesFractionsOfTheMedian)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    d.setRejectionMethod(QStringLiteral("Percentile"));
    EXPECT_DOUBLE_EQ(d.getSigmaLow(), 0.2);
    EXPECT_DOUBLE_EQ(d.getSigmaHigh(), 0.1);

    auto* lowLabel = d.findChild<QLabel*>(QStringLiteral("sigmaLowLabel"));
    auto* highLabel = d.findChild<QLabel*>(QStringLiteral("sigmaHighLabel"));
    auto* selector = d.findChild<QComboBox*>(QStringLiteral("rejectionMethodSelector"));
    ASSERT_NE(lowLabel, nullptr);
    ASSERT_NE(highLabel, nullptr);
    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(lowLabel->text(), QStringLiteral("Low Fraction of Median:"));
    EXPECT_EQ(highLabel->text(), QStringLiteral("High Fraction of Median:"));
    EXPECT_TRUE(selector->toolTip().contains(QStringLiteral("fraction of the median")))
        << selector->toolTip().toStdString();
    EXPECT_FALSE(selector->toolTip().contains(QStringLiteral("percentile range")));

    // A stored pair is kept.
    StackingSettings stored;
    stored.rejectionMethod = QStringLiteral("Percentile");
    stored.sigmaLow = 0.35;
    stored.sigmaHigh = 0.15;
    applyStackingSettingsToDialog(stored, d);
    EXPECT_DOUBLE_EQ(d.getSigmaLow(), 0.35);
    EXPECT_DOUBLE_EQ(d.getSigmaHigh(), 0.15);
}

TEST_F(DialogResetDefaultsTest, Light_CometStackingDisablesWeightingControlsAndSettings)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();

    d.setWeighting(QStringLiteral("Weighted FWHM"));
    d.setLocalWeightingEnabled(true);
    d.setCometStackingMode(QStringLiteral("Comet"));
    QApplication::processEvents();

    auto* weightingSelector = d.findChild<QComboBox*>(QStringLiteral("weightingSelector"));
    auto* weightingOptionsButton = d.findChild<QPushButton*>(QStringLiteral("weightingOptionsButton"));
    ASSERT_NE(weightingSelector, nullptr);
    ASSERT_NE(weightingOptionsButton, nullptr);

    EXPECT_FALSE(weightingSelector->isEnabled());
    EXPECT_EQ(weightingSelector->currentText(), QStringLiteral("None"));
    EXPECT_TRUE(weightingSelector->toolTip().contains(QStringLiteral("every included frame contributes equally")));
    EXPECT_FALSE(weightingOptionsButton->isEnabled());

    const auto settings = d.getCurrentSettings();
    EXPECT_EQ(settings.weighting, QStringLiteral("None"));
    EXPECT_FALSE(settings.localWeighting.enabled);
    EXPECT_EQ(settings.cometStackingMode, QStringLiteral("Comet"));
}

TEST_F(DialogResetDefaultsTest, Light_CometOutputModeIsNotConfiguredInStackingDialog)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();

    EXPECT_EQ(d.findChild<QComboBox*>(QStringLiteral("cometStackingModeSelector")), nullptr);
}

TEST_F(DialogResetDefaultsTest, Light_DrizzleDefaults)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    EXPECT_FALSE(d.getDrizzleEnabled());
    EXPECT_DOUBLE_EQ(d.getDrizzleScale(), 2.0);
    EXPECT_EQ(d.getDrizzleKernel(), QStringLiteral("Square"));
    EXPECT_DOUBLE_EQ(d.getDrizzlePixfrac(), 0.9);
}

// A session, preset or project read gave the model the kernel's key ("lanczos3"), which the dialog matched against its
// item texts case-sensitively: it showed Square, and the next OK saved Square. Every spelling of a kernel shows that
// kernel, and the dialog gives back its own spelling and the CFA pattern it has no control for.
TEST_F(DialogResetDefaultsTest, Light_EveryKernelAStoredGroupNamesIsShownAndKeptWhateverItsSpelling)
{
    const std::vector<std::pair<QString, QStringList>> kernels = {
        {QStringLiteral("Square"), {QStringLiteral("square"), QStringLiteral("Square"), QStringLiteral("SQUARE")}},
        {QStringLiteral("Point"), {QStringLiteral("point"), QStringLiteral("Point")}},
        {QStringLiteral("Turbo"), {QStringLiteral("turbo"), QStringLiteral("Turbo")}},
        {QStringLiteral("Gaussian"), {QStringLiteral("gaussian"), QStringLiteral("Gaussian")}},
        {QStringLiteral("Lanczos2"),
         {QStringLiteral("lanczos2"), QStringLiteral("Lanczos2"), QStringLiteral("lanczos-2")}},
        {QStringLiteral("Lanczos3"),
         {QStringLiteral("lanczos3"), QStringLiteral("Lanczos3"), QStringLiteral("lanczos-3")}}};
    for (const auto& [shown, spellings] : kernels)
    {
        for (const QString& spelling : spellings)
        {
            StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
            StackingSettings input;
            input.drizzle.enabled = true;
            input.drizzle.kernel = spelling;
            input.drizzle.cfaPattern = QStringLiteral("GBRG");
            applyStackingSettingsToDialog(input, d);

            auto* selector = d.findChild<QComboBox*>(QStringLiteral("drizzleKernelSelector"));
            ASSERT_NE(selector, nullptr);
            EXPECT_TRUE(selector->currentText().startsWith(shown)) << spelling.toStdString();
            EXPECT_EQ(d.getCurrentSettings().drizzle.kernel, shown) << spelling.toStdString();
            const StackingSettings accepted = stackingSettingsFromDialog(d, input);
            EXPECT_EQ(accepted.drizzle.kernel, shown) << spelling.toStdString();
            EXPECT_EQ(accepted.drizzle.cfaPattern, QStringLiteral("GBRG"));
        }
    }
}

// Lite resolves an automatic rejection and records that it was asked for; a rejection then chosen in the dialog is the
// person's, and the group's settings no longer say "Auto -> ...".
TEST_F(DialogResetDefaultsTest, Light_ARejectionChosenInTheDialogAfterALiteHandoffIsReportedAsChosen)
{
    StackingSettings handedOver;
    handedOver.stackMethod = QStringLiteral("Average");
    handedOver.requestedRejectionMethod = QStringLiteral("Auto");
    handedOver.rejectionMethod = QStringLiteral("Sigma Clipping");
    ASSERT_TRUE(stackingSettingsReport(handedOver).contains(QStringLiteral("Auto -> Sigma Clipping")));

    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    applyStackingSettingsToDialog(handedOver, d);
    d.setRejectionMethod(QStringLiteral("Winsorized Sigma Clipping"));
    const StackingSettings chosen = stackingSettingsFromDialog(d, handedOver);
    EXPECT_EQ(chosen.rejectionMethod, QStringLiteral("Winsorized Sigma Clipping"));
    EXPECT_TRUE(chosen.requestedRejectionMethod.isEmpty());
    const QString report = stackingSettingsReport(chosen);
    EXPECT_TRUE(report.contains(QStringLiteral("Rejection: Winsorized Sigma Clipping"))) << report.toStdString();
    EXPECT_FALSE(report.contains(QStringLiteral("Auto ->"))) << report.toStdString();
}

TEST_F(DialogResetDefaultsTest, Light_FinalMasterFallbackDisabledByDefault)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    EXPECT_FALSE(d.getSolveFinalMasterIfNeeded());
    EXPECT_FALSE(d.getCurrentSettings().solveFinalMasterIfNeeded);
}

TEST_F(DialogResetDefaultsTest, Light_FinalMasterFallbackPersistsThroughApplyAndRetrieve)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));

    // Apply settings with solveFinalMasterIfNeeded = false
    StackingSettings input;
    input.solveFinalMasterIfNeeded = false;
    applyStackingSettingsToDialog(input, d);

    auto result = stackingSettingsFromDialog(d, StackingSettings{});
    EXPECT_FALSE(result.solveFinalMasterIfNeeded);

    // Apply settings with solveFinalMasterIfNeeded = true
    input.solveFinalMasterIfNeeded = true;
    applyStackingSettingsToDialog(input, d);

    result = stackingSettingsFromDialog(d, StackingSettings{});
    EXPECT_TRUE(result.solveFinalMasterIfNeeded);
}

TEST_F(DialogResetDefaultsTest, Light_RegisteredOutputRetentionIsUserConfigurable)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();

    auto* discardOutputs = d.findChild<QCheckBox*>(QStringLiteral("discardRegisteredOutputsAfterStacking"));
    ASSERT_NE(discardOutputs, nullptr);
    EXPECT_TRUE(discardOutputs->isVisibleTo(&d));
    EXPECT_FALSE(discardOutputs->isChecked());
    EXPECT_EQ(d.getRegisteredOutputPolicy(), RegisteredOutputPolicy::Retained);

    StackingSettings input;
    input.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;
    applyStackingSettingsToDialog(input, d);

    EXPECT_TRUE(discardOutputs->isChecked());
    EXPECT_EQ(d.getRegisteredOutputPolicy(), RegisteredOutputPolicy::Discardable);
    const auto result = stackingSettingsFromDialog(d, StackingSettings{});
    EXPECT_EQ(result.registeredOutputPolicy, RegisteredOutputPolicy::Discardable);
}

TEST_F(DialogResetDefaultsTest, Light_RegistrationDerivedCometModeDisablesAppliedWeighting)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));

    StackingSettings input;
    input.stackMethod = QStringLiteral("Average");
    input.weighting = QStringLiteral("Weighted FWHM");
    input.localWeighting.enabled = true;
    input.cometStackingMode = QStringLiteral("Stars");

    applyStackingSettingsToDialog(input, d, QStringLiteral("Comet"));

    auto* weightingSelector = d.findChild<QComboBox*>(QStringLiteral("weightingSelector"));
    ASSERT_NE(weightingSelector, nullptr);
    EXPECT_FALSE(weightingSelector->isEnabled());
    EXPECT_EQ(weightingSelector->currentText(), QStringLiteral("None"));

    const auto result = stackingSettingsFromDialog(d, input);
    EXPECT_EQ(result.weighting, QStringLiteral("None"));
    EXPECT_FALSE(result.localWeighting.enabled);
    EXPECT_EQ(result.cometStackingMode, QStringLiteral("Comet"));
}

TEST_F(DialogResetDefaultsTest, Light_LocalNormalizationDefaults)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    EXPECT_FALSE(d.getLocalNormalizationEnabled());
    EXPECT_EQ(d.getLocalNormalizationReferenceStrategy(), QStringLiteral("Integrate Best Frames"));
    EXPECT_EQ(d.getLocalNormalizationMaxIntegratedFrames(), 16);
    EXPECT_EQ(d.getLocalNormalizationEvaluationCriteria(), QStringLiteral("PSF Signal Weight"));
    EXPECT_EQ(d.getLocalNormalizationGridSize(), 4);
    EXPECT_EQ(d.getLocalNormalizationGridInterpolation(), QStringLiteral("Bicubic"));
    EXPECT_EQ(d.getLocalNormalizationBackgroundEstimator(), QStringLiteral("Sigma Clipped Median"));
    EXPECT_DOUBLE_EQ(d.getLocalNormalizationBackgroundPercentile(), 0.25);
    EXPECT_DOUBLE_EQ(d.getLocalNormalizationSigmaClipK(), 3.0);
    EXPECT_EQ(d.getLocalNormalizationSigmaClipIterations(), 3);
    EXPECT_EQ(d.getLocalNormalizationBackgroundSmoothingRadius(), 1);
    EXPECT_EQ(d.getLocalNormalizationScaleEvaluation(), QStringLiteral("PSF Flux"));
    EXPECT_EQ(d.getLocalNormalizationScaleMode(), QStringLiteral("Linked RGB"));
    EXPECT_EQ(d.getLocalNormalizationPsfType(), QStringLiteral("Auto"));
    EXPECT_DOUBLE_EQ(d.getLocalNormalizationMoffatBeta(), 4.0);
    EXPECT_EQ(d.getLocalNormalizationMaxStars(), 24576);
    EXPECT_DOUBLE_EQ(d.getLocalNormalizationMinDetectionSnr(), 40.0);
    EXPECT_TRUE(d.getLocalNormalizationAllowClusteredSources());
    EXPECT_DOUBLE_EQ(d.getLocalNormalizationLowClip(), 4.5e-05);
    EXPECT_DOUBLE_EQ(d.getLocalNormalizationHighClip(), 0.85);
    EXPECT_EQ(d.getLocalNormalizationHotPixelMedianRadius(), 0);
    EXPECT_EQ(d.getLocalNormalizationNoiseReductionRadius(), 0);
    EXPECT_FALSE(d.getLocalNormalizationLocalScaleEnabled());
    EXPECT_EQ(d.getLocalNormalizationLocalScaleSmoothingRadius(), 1);
}

TEST_F(DialogResetDefaultsTest, LocalNormalizationDialogRecognizesFinePresetFromRestoredSettings)
{
    LocalNormalizationSettings settings;
    settings.enabled = true;
    settings.gridSize = 8;
    settings.referenceStrategy = QStringLiteral("Integrate Best Frames");

    LocalNormalizationSettingsDialog dialog;
    dialog.setSettings(settings);

    auto* presetSelector = dialog.findChild<QComboBox*>(QStringLiteral("presetSelector"));
    ASSERT_NE(presetSelector, nullptr);
    EXPECT_TRUE(presetSelector->currentText().contains(QStringLiteral("Fine")));
}

TEST_F(DialogResetDefaultsTest, Light_LocalNormalizationPresetLabelInfersFineFromRestoredSettings)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.setLocalNormalizationEnabled(true);
    d.setLocalNormalizationGridSize(8);
    d.setLocalNormalizationReferenceStrategy(QStringLiteral("Integrate Best Frames"));
    d.setLocalNormalizationScaleEvaluation(QStringLiteral("PSF Flux"));

    auto* presetLabel = d.findChild<QLabel*>(QStringLiteral("localNormalizationPresetLabel"));
    ASSERT_NE(presetLabel, nullptr);
    EXPECT_TRUE(presetLabel->text().contains(QStringLiteral("Fine")));
}

TEST_F(DialogResetDefaultsTest, Light_NormalizationReferenceMode)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    EXPECT_EQ(d.getNormalizationReferenceMode(), QStringLiteral("Automatic (best quality)"));
}

TEST_F(DialogResetDefaultsTest, Light_ManualReferenceSelectionShowsBestReferenceButton)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.setNormalizationReferenceCandidates(
        {QStringLiteral("C:/lights/light_a.fit"), QStringLiteral("C:/lights/light_b.fit")});
    d.setNormalizationMode(QStringLiteral("Additive With Scaling"));
    d.setNormalizationReferenceMode(QStringLiteral("Manual"));

    auto* referenceCombo = d.findChild<QComboBox*>(QStringLiteral("normalizationReferenceFrame"));
    auto* bestButton = d.findChild<QPushButton*>(QStringLiteral("normalizationReferenceBestButton"));
    ASSERT_NE(referenceCombo, nullptr);
    ASSERT_NE(bestButton, nullptr);

    EXPECT_TRUE(referenceCombo->isEnabled());
    EXPECT_FALSE(bestButton->isHidden());
    EXPECT_TRUE(bestButton->isEnabled());
    EXPECT_EQ(bestButton->text(), QStringLiteral("Best"));
}

TEST_F(DialogResetDefaultsTest, Light_LocalNormalizationExplicitReferenceEnablesManualReferenceSelection)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.setNormalizationReferenceCandidates(
        {QStringLiteral("C:/lights/light_a.fit"), QStringLiteral("C:/lights/light_b.fit")});
    d.setLocalNormalizationEnabled(true);
    d.setLocalNormalizationReferenceStrategy(QStringLiteral("Explicit Reference"));

    auto* modeCombo = d.findChild<QComboBox*>(QStringLiteral("normalizationReferenceMode"));
    auto* referenceCombo = d.findChild<QComboBox*>(QStringLiteral("normalizationReferenceFrame"));
    auto* bestButton = d.findChild<QPushButton*>(QStringLiteral("normalizationReferenceBestButton"));
    ASSERT_NE(modeCombo, nullptr);
    ASSERT_NE(referenceCombo, nullptr);
    ASSERT_NE(bestButton, nullptr);

    EXPECT_TRUE(modeCombo->isEnabled());
    EXPECT_EQ(d.getNormalizationReferenceMode(), QStringLiteral("Manual"));
    EXPECT_TRUE(referenceCombo->isEnabled());
    EXPECT_TRUE(bestButton->isEnabled());
}

// With drizzle on, the drizzled stack fits a Local group's surfaces itself, against the group's reference, and reads
// no reference strategy, evaluation, PSF flux scale or analysis filter: the local normalization dialog leaves those
// out and says why, and offers them again once drizzle is off. It offered every one, and none had an effect.
TEST_F(DialogResetDefaultsTest, Light_ADrizzledLocalNormalizationOffersOnlyWhatTheDrizzledStackFits)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.setLocalNormalizationEnabled(true);
    d.setDrizzleEnabled(true);
    auto* configure = d.findChild<QPushButton*>(QStringLiteral("localNormalizationConfigureButton"));
    auto* preset = d.findChild<QLabel*>(QStringLiteral("localNormalizationPresetLabel"));
    ASSERT_NE(configure, nullptr);
    ASSERT_NE(preset, nullptr);
    EXPECT_TRUE(preset->text().contains(QStringLiteral("drizzled stack"))) << preset->text().toStdString();

    const auto opened = [&d, configure]() -> LocalNormalizationSettingsDialog* {
        configure->click();
        const QList<LocalNormalizationSettingsDialog*> dialogs = d.findChildren<LocalNormalizationSettingsDialog*>();
        return dialogs.size() == 1 ? dialogs.front() : nullptr;
    };
    const std::array<const char*, 9> analysis = {"referenceGroupBox",
                                                 "evaluationCriteria",
                                                 "scaleEvaluation",
                                                 "psfType",
                                                 "maxStars",
                                                 "minDetectionSnr",
                                                 "allowClusteredSources",
                                                 "hotPixelMedianRadius",
                                                 "noiseReductionRadius"};

    LocalNormalizationSettingsDialog* drizzled = opened();
    ASSERT_NE(drizzled, nullptr);
    for (const char* name : analysis)
    {
        auto* widget = drizzled->findChild<QWidget*>(QString::fromLatin1(name));
        ASSERT_NE(widget, nullptr) << name;
        EXPECT_TRUE(widget->isHidden()) << name;
    }
    auto* note = drizzled->findChild<QLabel*>(QStringLiteral("referenceAnalysisNote"));
    ASSERT_NE(note, nullptr);
    EXPECT_FALSE(note->isHidden());
    EXPECT_TRUE(note->text().contains(QStringLiteral("drizzled stack"))) << note->text().toStdString();
    drizzled->reject();
    delete drizzled;

    d.setDrizzleEnabled(false);
    EXPECT_FALSE(preset->text().contains(QStringLiteral("drizzled stack"))) << preset->text().toStdString();
    LocalNormalizationSettingsDialog* standard = opened();
    ASSERT_NE(standard, nullptr);
    for (const char* name : analysis)
    {
        auto* widget = standard->findChild<QWidget*>(QString::fromLatin1(name));
        ASSERT_NE(widget, nullptr) << name;
        EXPECT_FALSE(widget->isHidden()) << name;
    }
    note = standard->findChild<QLabel*>(QStringLiteral("referenceAnalysisNote"));
    ASSERT_NE(note, nullptr);
    EXPECT_TRUE(note->isHidden());
    standard->reject();
    delete standard;
}

// Local normalization matches each light globally in Additive With Scaling, whatever method the group stores, so the
// disabled Normalization choice shows that method while it is on, and the stored one - kept for when it is turned off
// - comes back with the choice. The choice kept showing the stored method, which did not run.
TEST_F(DialogResetDefaultsTest, Light_LocalNormalizationShowsTheGlobalMethodItRunsAndKeepsTheStoredOne)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    StackingSettings input;
    input.normalizationMode = NormalizationMode::Local;
    input.globalNormalizationMethod = GlobalNormalizationMethod::Multiplicative;
    input.localNormalization.enabled = true;
    applyStackingSettingsToDialog(input, d);

    const QString setByLocal = QStringLiteral("Additive With Scaling (set by local normalization)");
    auto* normalization = d.findChild<QComboBox*>(QStringLiteral("normalizationMode"));
    ASSERT_NE(normalization, nullptr);
    EXPECT_FALSE(normalization->isEnabled());
    EXPECT_EQ(normalization->currentText(), setByLocal);
    for (const StackingSettings& read : {d.getCurrentSettings(), stackingSettingsFromDialog(d, StackingSettings{})})
    {
        EXPECT_EQ(read.normalizationMode, NormalizationMode::Local);
        EXPECT_EQ(read.globalNormalizationMethod, GlobalNormalizationMethod::Multiplicative)
            << "the stored method is kept for when local normalization is turned off";
    }

    d.setLocalNormalizationEnabled(false);
    EXPECT_TRUE(normalization->isEnabled());
    EXPECT_EQ(normalization->currentText(), QStringLiteral("Multiplicative"));
    EXPECT_EQ(normalization->findText(setByLocal), -1) << "offered only while local normalization sets the method";
    const StackingSettings global = stackingSettingsFromDialog(d, StackingSettings{});
    EXPECT_EQ(global.normalizationMode, NormalizationMode::Global);
    EXPECT_EQ(global.globalNormalizationMethod, GlobalNormalizationMethod::Multiplicative);

    // A method set while local normalization is on is the one it gives back.
    d.setLocalNormalizationEnabled(true);
    d.setNormalizationMode(QStringLiteral("Additive"));
    EXPECT_EQ(normalization->currentText(), setByLocal);
    d.setLocalNormalizationEnabled(false);
    EXPECT_EQ(normalization->currentText(), QStringLiteral("Additive"));
}

// The scale estimator is the dispersion the two scaling methods divide by - and local normalization's global match,
// which is one of them: it is offered where one runs, stored as the group's, and read back. No control offered it,
// so a group edited to MAD ran MAD and showed nothing.
TEST_F(DialogResetDefaultsTest, TheScaleEstimatorIsOfferedForTheScalingMethodsAndRoundTrips)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    auto* estimator = d.findChild<QComboBox*>(QStringLiteral("scaleEstimatorSelector"));
    ASSERT_NE(estimator, nullptr);
    EXPECT_EQ(estimator->currentText(), QStringLiteral("IKSS"));
    EXPECT_TRUE(estimator->isEnabled()) << "the lights' default, Additive With Scaling, scales";

    StackingSettings input;
    input.globalNormalizationMethod = GlobalNormalizationMethod::MultiplicativeScaling;
    input.scaleEstimator = ScaleEstimatorMethod::MAD;
    applyStackingSettingsToDialog(input, d);
    EXPECT_EQ(estimator->currentText(), QStringLiteral("MAD"));
    EXPECT_TRUE(estimator->isEnabled());
    EXPECT_EQ(d.getCurrentSettings().scaleEstimator, ScaleEstimatorMethod::MAD);
    EXPECT_EQ(stackingSettingsFromDialog(d, StackingSettings{}).scaleEstimator, ScaleEstimatorMethod::MAD);

    d.setNormalizationMode(QStringLiteral("Additive"));
    EXPECT_FALSE(estimator->isEnabled()) << "an additive match measures no scale";
    d.setLocalNormalizationEnabled(true);
    EXPECT_TRUE(estimator->isEnabled()) << "local normalization's global match is Additive With Scaling";
    d.setLocalNormalizationEnabled(false);
    d.setNormalizationMode(QStringLiteral("None"));
    EXPECT_FALSE(estimator->isEnabled());
    d.resetToDefaults();
    EXPECT_EQ(estimator->currentText(), QStringLiteral("IKSS"));

    StackingSettingsDialog flat(StackingSettingsDialog::FrameType::FLAT, QStringLiteral("Flat"));
    auto* flatEstimator = flat.findChild<QComboBox*>(QStringLiteral("scaleEstimatorSelector"));
    ASSERT_NE(flatEstimator, nullptr);
    EXPECT_FALSE(flatEstimator->isEnabled()) << "a flat's default, Multiplicative, measures no scale";
    flat.setNormalizationMode(QStringLiteral("Multiplicative With Scaling"));
    EXPECT_TRUE(flatEstimator->isEnabled());

    StackingSettingsDialog bias(StackingSettingsDialog::FrameType::BIAS, QStringLiteral("Bias"));
    auto* biasEstimator = bias.findChild<QComboBox*>(QStringLiteral("scaleEstimatorSelector"));
    ASSERT_NE(biasEstimator, nullptr);
    EXPECT_FALSE(biasEstimator->isEnabled()) << "a bias master is not normalized";
}

// Overlap normalization matches the lights over the areas they share - a mosaic's panels - under global
// normalization: it is offered there with its two floors, stored as the group's and read back. The stage honoured it
// and no control offered it.
TEST_F(DialogResetDefaultsTest, Light_OverlapNormalizationIsOfferedUnderGlobalNormalizationAndRoundTrips)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    auto* overlap = d.findChild<QCheckBox*>(QStringLiteral("overlapNormalizationEnabled"));
    auto* pixels = d.findChild<QSpinBox*>(QStringLiteral("overlapMinimumPixelsValue"));
    auto* fraction = d.findChild<QDoubleSpinBox*>(QStringLiteral("overlapMinimumFractionValue"));
    ASSERT_NE(overlap, nullptr);
    ASSERT_NE(pixels, nullptr);
    ASSERT_NE(fraction, nullptr);
    EXPECT_FALSE(overlap->isHidden());
    EXPECT_FALSE(overlap->isChecked());
    EXPECT_TRUE(overlap->isEnabled());
    EXPECT_FALSE(pixels->isEnabled()) << "the floors are read only when overlap normalization is on";
    EXPECT_EQ(pixels->value(), 1000);
    EXPECT_DOUBLE_EQ(fraction->value(), 0.01);

    StackingSettings input;
    input.overlapNormalization.enabled = true;
    input.overlapNormalization.minOverlapPixels = 2500;
    input.overlapNormalization.minOverlapFraction = 0.05;
    applyStackingSettingsToDialog(input, d);
    EXPECT_TRUE(overlap->isChecked());
    EXPECT_TRUE(pixels->isEnabled());
    EXPECT_TRUE(fraction->isEnabled());
    for (const StackingSettings& read : {d.getCurrentSettings(), stackingSettingsFromDialog(d, StackingSettings{})})
    {
        EXPECT_TRUE(read.overlapNormalization.enabled);
        EXPECT_EQ(read.overlapNormalization.minOverlapPixels, 2500);
        EXPECT_DOUBLE_EQ(read.overlapNormalization.minOverlapFraction, 0.05);
    }

    d.setLocalNormalizationEnabled(true);
    EXPECT_FALSE(overlap->isEnabled()) << "overlap normalization is a global normalization";
    EXPECT_FALSE(pixels->isEnabled());
    d.setLocalNormalizationEnabled(false);
    d.setNormalizationMode(QStringLiteral("None"));
    EXPECT_FALSE(overlap->isEnabled());
    d.resetToDefaults();
    EXPECT_FALSE(overlap->isChecked());
    EXPECT_EQ(pixels->value(), 1000);

    StackingSettingsDialog flat(StackingSettingsDialog::FrameType::FLAT, QStringLiteral("Flat"));
    auto* flatOverlap = flat.findChild<QCheckBox*>(QStringLiteral("overlapNormalizationEnabled"));
    ASSERT_NE(flatOverlap, nullptr);
    EXPECT_TRUE(flatOverlap->isHidden()) << "a master flat's frames are not normalized over their overlaps";
}

// A control shows a value to its decimals. A value the settings hold more finely - a file edited by hand - is given
// back as stored while its control still shows it, one beyond the control's range is held to it, and one the person
// sets is the control's. The dialog rounded every such value each time it was accepted, which changed the group's
// settings, and the fingerprints that hold them, without anyone changing them.
TEST_F(DialogResetDefaultsTest, Light_AStoredValueFinerThanItsControlIsGivenBackWhileTheControlShowsIt)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    StackingSettings input;
    input.sigmaLow = 2.1234;
    input.sigmaHigh = 50.0;
    input.exponentialAlpha = 1.2567;
    input.minWeightFloor = 0.01234;
    input.localWeighting.snrWeightExponent = 1.5555;
    input.drizzle.pixelFraction = 0.8765;
    input.overlapNormalization.minOverlapFraction = 0.001234;
    applyStackingSettingsToDialog(input, d);

    for (const StackingSettings& read : {stackingSettingsFromDialog(d, StackingSettings{}), d.getCurrentSettings()})
    {
        EXPECT_DOUBLE_EQ(read.sigmaLow, 2.1234);
        EXPECT_DOUBLE_EQ(read.sigmaHigh, 10.0) << "a value beyond the control's range is held to it";
        EXPECT_DOUBLE_EQ(read.exponentialAlpha, 1.2567);
        EXPECT_DOUBLE_EQ(read.minWeightFloor, 0.01234);
        EXPECT_DOUBLE_EQ(read.localWeighting.snrWeightExponent, 1.5555);
        EXPECT_DOUBLE_EQ(read.drizzle.pixelFraction, 0.8765);
        EXPECT_DOUBLE_EQ(read.overlapNormalization.minOverlapFraction, 0.001234);
    }

    auto* fraction = d.findChild<QDoubleSpinBox*>(QStringLiteral("overlapMinimumFractionValue"));
    auto* sigmaLow = d.findChild<QDoubleSpinBox*>(QStringLiteral("sigmaLowValue"));
    ASSERT_NE(fraction, nullptr);
    ASSERT_NE(sigmaLow, nullptr);
    fraction->setValue(0.05);
    sigmaLow->setValue(2.5);
    const StackingSettings changed = stackingSettingsFromDialog(d, StackingSettings{});
    EXPECT_DOUBLE_EQ(changed.overlapNormalization.minOverlapFraction, 0.05)
        << "a value the person sets is the control's";
    EXPECT_DOUBLE_EQ(changed.sigmaLow, 2.5);
    EXPECT_DOUBLE_EQ(changed.exponentialAlpha, 1.2567) << "an untouched value is still the stored one";
}

// The settings a group's row shows on hover say what runs: the global match local normalization runs rather than the
// method stored for Global, the star floor as the flux scale's, overlap normalization with its floors, and for a
// drizzled Local group what its stack does not read. It showed the stored method and no overlap normalization.
TEST(StackingSettingsReport, TheGroupsSettingsSayWhatTheNormalizationRuns)
{
    StackingSettings local;
    local.normalizationMode = NormalizationMode::Local;
    local.globalNormalizationMethod = GlobalNormalizationMethod::Multiplicative;
    local.localNormalization.enabled = true;
    const QString shown = stackingSettingsReport(local);
    EXPECT_TRUE(shown.contains(QStringLiteral("Global Method: Additive With Scaling (set by local normalization)")))
        << shown.toStdString();
    EXPECT_FALSE(shown.contains(QStringLiteral("Global Method: Multiplicative"))) << shown.toStdString();
    EXPECT_TRUE(shown.contains(QStringLiteral("Min SNR (flux scale): 40.00"))) << shown.toStdString();
    EXPECT_FALSE(shown.contains(QStringLiteral("Fitted by the drizzled stack"))) << shown.toStdString();
    local.drizzle.enabled = true;
    EXPECT_TRUE(stackingSettingsReport(local).contains(QStringLiteral("Fitted by the drizzled stack")));

    StackingSettings global;
    global.normalizationMode = NormalizationMode::Global;
    global.overlapNormalization.enabled = true;
    global.overlapNormalization.minOverlapPixels = 2500;
    global.overlapNormalization.minOverlapFraction = 0.05;
    const QString overlap = stackingSettingsReport(global);
    EXPECT_TRUE(overlap.contains(QStringLiteral("Overlap Normalization: Yes (at least 2500 px and 0.05 of the "
                                                "smaller light)")))
        << overlap.toStdString();
}

// A group weighed by its stars' width says its floor and nothing of per-channel widths: every channel is weighed by its
// light's one width. The report listed a per-channel choice no stack could honour.
TEST(StackingSettingsReport, AWidthWeightingSaysNothingOfPerChannelWidths)
{
    StackingSettings width;
    width.weighting = QStringLiteral("Weighted FWHM");
    width.minWeightFloor = 0.05;
    const QString shown = stackingSettingsReport(width);
    EXPECT_TRUE(shown.contains(QStringLiteral("Weighting: Weighted FWHM"))) << shown.toStdString();
    EXPECT_TRUE(shown.contains(QStringLiteral("Min Weight Floor: 0.050"))) << shown.toStdString();
    EXPECT_FALSE(shown.contains(QStringLiteral("Per-channel"), Qt::CaseInsensitive)) << shown.toStdString();
}

TEST_F(DialogResetDefaultsTest, Light_HdrDisabledByDefault)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    d.resetToDefaults();
    EXPECT_FALSE(d.getHdrExposureMergeEnabled());
}

TEST_F(DialogResetDefaultsTest, Light_HdrSelectionClearsUnsupportedSettingsInSnapshot)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));

    d.setNormalizationMode(QStringLiteral("Additive With Scaling"));
    d.setCreateQualityMaps(true);
    d.setCreateRejectionMaps(true);
    d.setMergeRejectionMaps(true);
    d.setSpatialRejectionClustering(true);
    d.setRestackExcludingFlaggedFrames(true);
    d.setDrizzleEnabled(true);
    d.setHdrExposureMergeEnabled(true);

    const auto settings = d.getCurrentSettings();
    EXPECT_TRUE(settings.hdr.enabled);
    EXPECT_EQ(settings.normalizationMode, NormalizationMode::None);
    EXPECT_FALSE(settings.createQualityMaps);
    EXPECT_FALSE(settings.createRejectionMaps);
    EXPECT_FALSE(settings.mergeRejectionMaps);
    EXPECT_FALSE(settings.spatialRejectionClustering);
    EXPECT_FALSE(settings.restackExcludingFlaggedFrames);
    EXPECT_FALSE(settings.drizzle.enabled);
}

TEST_F(DialogResetDefaultsTest, Light_AutoAdaptiveSelectionClearsUnsupportedSettingsInSnapshot)
{
    StackingSettingsDialog d(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));

    d.setWeighting(QStringLiteral("Noise"));
    d.setRejectionMethod(QStringLiteral("Sigma Clipping"));
    d.setCreateRejectionMaps(true);
    d.setMergeRejectionMaps(true);
    d.setSpatialRejectionClustering(true);
    d.setRestackExcludingFlaggedFrames(true);
    d.setStackMethod(QStringLiteral("Auto-Adaptive Weighted Average"));

    const auto settings = d.getCurrentSettings();
    EXPECT_EQ(settings.stackMethod, QStringLiteral("Auto-Adaptive Weighted Average"));
    EXPECT_EQ(settings.weighting, QStringLiteral("None"));
    EXPECT_EQ(settings.rejectionMethod, QStringLiteral("None"));
    EXPECT_FALSE(settings.createRejectionMaps);
    EXPECT_FALSE(settings.mergeRejectionMaps);
    EXPECT_FALSE(settings.spatialRejectionClustering);
    EXPECT_FALSE(settings.restackExcludingFlaggedFrames);
}

TEST(StackingParamsUtilsTest, AutoAdaptiveMappingSanitizesWeightingAndRejection)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Auto-Adaptive Weighted Average");
    settings.rejectionMethod = QStringLiteral("Sigma Clipping");
    settings.weighting = QStringLiteral("Noise");
    settings.createRejectionMaps = true;
    settings.mergeRejectionMaps = true;

    // The method is a combination of its own, which takes no frame weights and rejects nothing, so the stack lets the
    // weighting and the rejection maps go and says so.
    const auto choice = ImageIntegration::integrationStackChoice(settings);
    ASSERT_TRUE(choice.hasValue()) << choice.status().message();
    EXPECT_EQ(choice->combination, an::StackCombination::AutoAdaptiveWeightedAverage);
    EXPECT_EQ(choice->rejection, an::StackRejection::None);
    EXPECT_FALSE(ImageIntegration::combinationTakesWeights(choice->combination));

    const auto sanitized = ImageIntegration::sanitizeStackingSettings(settings);
    EXPECT_EQ(sanitized.weighting, QStringLiteral("None"));
    EXPECT_FALSE(sanitized.createRejectionMaps);
    EXPECT_FALSE(sanitized.mergeRejectionMaps);
    EXPECT_FALSE(ImageIntegration::stackingSettingsNotes(settings).isEmpty());
}

TEST(StackingParamsUtilsTest, HdrMappingSanitizesNormalizationDrizzleAndRejectionMaps)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Average");
    settings.normalizationMode = NormalizationMode::Global;
    settings.globalNormalizationMethod = GlobalNormalizationMethod::AdditiveScaling;
    settings.createQualityMaps = true;
    settings.createRejectionMaps = true;
    settings.mergeRejectionMaps = true;
    settings.spatialRejectionClustering = true;
    settings.restackExcludingFlaggedFrames = true;
    settings.drizzle.enabled = true;
    settings.hdr.enabled = true;

    const auto sanitized = ImageIntegration::sanitizeStackingSettings(settings);

    // The exposure merge stacks each exposure on its own: no normalization, no map of any kind.
    EXPECT_TRUE(sanitized.hdr.enabled);
    EXPECT_FALSE(ImageIntegration::stackingQualityMapProductPolicy(sanitized).requestsAnyProduct());
    EXPECT_TRUE(ImageIntegration::stackingSettingsNotes(settings)
                    .join(QLatin1Char('\n'))
                    .contains(QStringLiteral("exposure merge")));

    EXPECT_EQ(sanitized.normalizationMode, NormalizationMode::None);
    EXPECT_FALSE(sanitized.createQualityMaps);
    EXPECT_FALSE(sanitized.createRejectionMaps);
    EXPECT_FALSE(sanitized.mergeRejectionMaps);
    EXPECT_FALSE(sanitized.spatialRejectionClustering);
    EXPECT_FALSE(sanitized.restackExcludingFlaggedFrames);
    EXPECT_FALSE(sanitized.drizzle.enabled);
}

// The exposure merge stacks each exposure on its own, so it weighs no frame: the weighting a group stored before the
// merge was turned on is let go of, and the merge's note names it, as the dialog that greys the selector and the guide
// say. The weighting stayed, greyed in the dialog and unsaid, and every bucket was weighted by it.
TEST(StackingParamsUtilsTest, HdrMergeLetsGoOfTheFrameWeightingAndItsNoteNamesIt)
{
    StackingSettings settings;
    settings.weighting = QStringLiteral("Noise");
    settings.hdr.enabled = true;

    const auto sanitized = ImageIntegration::sanitizeStackingSettings(settings);
    EXPECT_EQ(sanitized.weighting, QStringLiteral("None"));
    const auto choice = ImageIntegration::integrationStackChoice(sanitized);
    ASSERT_TRUE(choice.hasValue()) << choice.status().message();
    EXPECT_EQ(choice->weighting, an::StackWeighting::Uniform);
    const QString notes = ImageIntegration::stackingSettingsNotes(settings).join(QLatin1Char('\n'));
    EXPECT_TRUE(notes.contains(QStringLiteral("exposure merge"))) << notes.toStdString();
    EXPECT_TRUE(notes.contains(QStringLiteral("the Noise weighting"))) << notes.toStdString();
}

TEST(StackingParamsUtilsTest, QualityMapProductPolicyUsesExplicitGateAcrossPairwiseWorkflowMatrix)
{
    struct TestCase
    {
        const char* label;
        bool drizzle = false;
        bool cfaAware = false;
        QString outputConfiguration;
        QString weighting;
        bool localWeighting = false;
        bool createQualityMaps = false;
        bool createRejectionMaps = false;
        bool expectWeightSum = false;
    };

    const std::array<TestCase, 8> cases = {
        TestCase{"standard mono unweighted quality off rejection off", false, false, QStringLiteral("Mono"),
                 QStringLiteral("None"), false, false, false, false},
        TestCase{"drizzle CFA global weighted quality off rejection on", true, true, QStringLiteral("CFA"),
                 QStringLiteral("Noise"), false, false, true, false},
        TestCase{"standard RGB width weighted quality off rejection off", false, false, QStringLiteral("RGB"),
                 QStringLiteral("Weighted FWHM"), false, false, false, false},
        TestCase{"drizzle mono local weighted quality off rejection off", true, false, QStringLiteral("Mono"),
                 QStringLiteral("Noise"), true, false, false, false},
        TestCase{"standard mono global weighted quality on rejection off", false, false, QStringLiteral("Mono"),
                 QStringLiteral("Noise"), false, true, false, true},
        TestCase{"standard RGB width weighted quality on rejection off", false, false, QStringLiteral("RGB"),
                 QStringLiteral("Weighted FWHM"), false, true, false, true},
        TestCase{"drizzle mono local weighted quality on rejection on", true, false, QStringLiteral("Mono"),
                 QStringLiteral("Noise"), true, true, true, true},
        TestCase{"drizzle RGB unweighted quality on rejection off", true, false, QStringLiteral("RGB"),
                 QStringLiteral("None"), false, true, false, false},
    };

    for (const auto& testCase : cases)
    {
        SCOPED_TRACE(testCase.label);
        SCOPED_TRACE(testCase.outputConfiguration.toStdString());

        StackingSettings settings;
        settings.stackMethod = QStringLiteral("Average");
        settings.rejectionMethod = QStringLiteral("Sigma Clipping");
        settings.weighting = testCase.weighting;
        settings.localWeighting.enabled = testCase.localWeighting;
        settings.createQualityMaps = testCase.createQualityMaps;
        settings.createRejectionMaps = testCase.createRejectionMaps;
        settings.drizzle.enabled = testCase.drizzle;
        settings.drizzle.cfaAware = testCase.cfaAware;

        const StackingSettings sanitized = ImageIntegration::sanitizeStackingSettings(settings);
        const ImageIntegration::StackingQualityMapProductPolicy policy =
            ImageIntegration::stackingQualityMapProductPolicy(sanitized);

        EXPECT_EQ(policy.coverage, testCase.createQualityMaps);
        EXPECT_EQ(policy.variance, testCase.createQualityMaps);
        EXPECT_EQ(policy.snrGain, testCase.createQualityMaps);
        EXPECT_EQ(policy.weightSum, testCase.expectWeightSum);
        EXPECT_EQ(sanitized.createRejectionMaps, testCase.createRejectionMaps);
    }
}

TEST(StackingParamsUtilsTest, SpatialRestackRequiresClusteringAndDisablesForDrizzle)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Average");
    settings.rejectionMethod = QStringLiteral("Sigma Clipping");
    settings.restackExcludingFlaggedFrames = true;

    const auto sanitized = ImageIntegration::sanitizeStackingSettings(settings);
    EXPECT_TRUE(sanitized.spatialRejectionClustering);
    EXPECT_TRUE(sanitized.restackExcludingFlaggedFrames);

    settings.drizzle.enabled = true;
    const auto drizzleSanitized = ImageIntegration::sanitizeStackingSettings(settings);
    EXPECT_TRUE(drizzleSanitized.spatialRejectionClustering);
    EXPECT_FALSE(drizzleSanitized.restackExcludingFlaggedFrames);
}

TEST(StackingParamsUtilsTest, SpatialWeightingMappingSanitizesUnsupportedAndDrizzleSettings)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Auto-Adaptive Weighted Average");
    settings.weighting = QStringLiteral("Noise");
    settings.localWeighting.enabled = true;
    settings.localWeighting.windowSize = 11;
    settings.localWeighting.gridSpacing = 32;
    settings.localWeighting.snrWeightExponent = 2.5;
    settings.drizzle.enabled = true;

    const auto sanitized = ImageIntegration::sanitizeStackingSettings(settings);
    const auto choice = ImageIntegration::integrationStackChoice(sanitized);

    // Auto-Adaptive doesn't support weighting, so local weighting must be stripped
    EXPECT_FALSE(sanitized.localWeighting.enabled);
    // Drizzle is now compatible with local weighting — it should stay enabled
    EXPECT_TRUE(sanitized.drizzle.enabled);
    ASSERT_TRUE(choice.hasValue()) << choice.status().message();
    EXPECT_FALSE(ImageIntegration::combinationTakesWeights(choice->combination));
    EXPECT_EQ(choice->weighting, an::StackWeighting::Uniform);
}

TEST(StackingParamsUtilsTest, SpatialWeightingAndDrizzleBothSurviveSanitization)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Average");
    settings.weighting = QStringLiteral("Noise");
    settings.localWeighting.enabled = true;
    settings.localWeighting.windowSize = 9;
    settings.localWeighting.gridSpacing = 16;
    settings.localWeighting.snrWeightExponent = 2.0;
    settings.drizzle.enabled = true;
    settings.drizzle.scale = 2.0;
    settings.drizzle.pixelFraction = 0.8;

    const auto sanitized = ImageIntegration::sanitizeStackingSettings(settings);
    const auto choice = ImageIntegration::integrationStackChoice(sanitized);

    // Average supports weighting — both should survive sanitization
    EXPECT_TRUE(sanitized.localWeighting.enabled);
    EXPECT_TRUE(sanitized.drizzle.enabled);
    ASSERT_TRUE(choice.hasValue()) << choice.status().message();
    EXPECT_TRUE(ImageIntegration::combinationTakesWeights(choice->combination));
    EXPECT_EQ(choice->weighting, an::StackWeighting::Noise);
}

TEST(StackingParamsUtilsTest, CometStackingDisablesExecutionWeighting)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Average");
    settings.weighting = QStringLiteral("Weighted FWHM");
    settings.localWeighting.enabled = true;
    settings.localWeighting.windowSize = 9;
    settings.localWeighting.gridSpacing = 16;
    settings.localWeighting.snrWeightExponent = 2.0;
    settings.cometStackingMode = QStringLiteral("Comet");

    const auto sanitized = ImageIntegration::sanitizeStackingSettings(settings);

    // A comet stack weighs its frames equally: the sanitized settings carry no weighting, and the stack says it lets
    // the stored one go.
    EXPECT_EQ(sanitized.weighting, QStringLiteral("None"));
    EXPECT_FALSE(sanitized.localWeighting.enabled);
    EXPECT_TRUE(ImageIntegration::stackingSettingsNotes(settings)
                    .join(QLatin1Char('\n'))
                    .contains(QStringLiteral("comet stack weighs its frames equally")));
}

TEST(StackingParamsUtilsTest, StarsPlusCometExperimentalExactTokenIsExplicitOptIn)
{
    const QString token = ImageIntegration::starsPlusCometExperimentalStackingModeToken();

    EXPECT_TRUE(ImageIntegration::isStarsPlusCometExperimentalStackingMode(token));
    EXPECT_TRUE(ImageIntegration::usesStarsPlusCometExperimentalStackingMode(token));
    EXPECT_TRUE(ImageIntegration::usesCometStackingMode(token));
    EXPECT_FALSE(ImageIntegration::usesCometOnlyStackingMode(token));
    EXPECT_EQ(ImageIntegration::normalizeCometStackingMode(token), token);

    EXPECT_EQ(ImageIntegration::normalizeCometStackingMode(QStringLiteral("Stars")), QStringLiteral("Stars"));
    EXPECT_EQ(ImageIntegration::normalizeCometStackingMode(QStringLiteral("Comet")), QStringLiteral("Comet"));

    for (const QString& unofferedMode :
         {QStringLiteral("Stars+Comet"), QStringLiteral("Stars + Comet"), QStringLiteral("stars_plus_comet")})
    {
        EXPECT_FALSE(ImageIntegration::isStarsPlusCometExperimentalStackingMode(unofferedMode));
        EXPECT_FALSE(ImageIntegration::usesCometStackingMode(unofferedMode));
        EXPECT_EQ(ImageIntegration::normalizeCometStackingMode(unofferedMode), unofferedMode.trimmed());
    }
}

TEST(StackingParamsUtilsTest, StarsPlusCometExperimentalKeepsPureCometDistinct)
{
    const QString token = ImageIntegration::starsPlusCometExperimentalStackingModeToken();

    EXPECT_TRUE(ImageIntegration::usesStarsPlusCometExperimentalStackingMode(token));
    EXPECT_TRUE(ImageIntegration::usesCometStackingMode(token));
    EXPECT_FALSE(ImageIntegration::usesCometOnlyStackingMode(token));
    EXPECT_EQ(ImageIntegration::normalizeCometStackingMode(token), token);
    EXPECT_TRUE(ImageIntegration::usesCometOnlyStackingMode(QStringLiteral("Comet")));
    EXPECT_FALSE(ImageIntegration::usesStarsPlusCometExperimentalStackingMode(QStringLiteral("Comet")));
    EXPECT_TRUE(ImageIntegration::usesCometStackingMode(token));
    EXPECT_FALSE(ImageIntegration::usesCometOnlyStackingMode(token));
}

TEST(StackingParamsUtilsTest, DisabledWeightingClearsDependentWeightingOptions)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Average");
    settings.weighting = QStringLiteral("None");
    settings.localWeighting.enabled = true;

    const auto sanitized = ImageIntegration::sanitizeStackingSettings(settings);
    const auto choice = ImageIntegration::integrationStackChoice(sanitized);

    EXPECT_FALSE(sanitized.localWeighting.enabled);
    ASSERT_TRUE(choice.hasValue()) << choice.status().message();
    EXPECT_EQ(choice->weighting, an::StackWeighting::Uniform);
}

TEST(StackingParamsUtilsTest, PhotometricWeightingMapsToEngineEnum)
{
    for (const QString& spelling : {QStringLiteral("Photometric"), QStringLiteral("photometric")})
    {
        StackingSettings settings;
        settings.stackMethod = QStringLiteral("Average");
        settings.weighting = spelling;
        const auto choice = ImageIntegration::integrationStackChoice(settings);
        ASSERT_TRUE(choice.hasValue()) << choice.status().message();
        EXPECT_EQ(choice->weighting, an::StackWeighting::Photometric) << spelling.toStdString();
    }
}

// =============================================================================
// 2. V2 model – defaultCalibrationStackingSettings() per frame type
// =============================================================================

class V2CalibrationDefaultsTest : public ::testing::Test
{
};

// ---- BIAS (V2) ----

TEST_F(V2CalibrationDefaultsTest, Bias_StackMethod)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Bias);
    EXPECT_EQ(s.stackMethod, QStringLiteral("Average"));
}

TEST_F(V2CalibrationDefaultsTest, Bias_RejectionMethod)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Bias);
    EXPECT_EQ(s.rejectionMethod, QStringLiteral("Sigma Clipping"));
}

TEST_F(V2CalibrationDefaultsTest, Bias_Normalization)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Bias);
    EXPECT_EQ(s.normalizationMode, NormalizationMode::None);
}

TEST_F(V2CalibrationDefaultsTest, Bias_SigmaValues)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Bias);
    EXPECT_DOUBLE_EQ(s.sigmaLow, 3.0);
    EXPECT_DOUBLE_EQ(s.sigmaHigh, 3.0);
}

TEST_F(V2CalibrationDefaultsTest, Bias_Weighting)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Bias);
    EXPECT_EQ(s.weighting, QStringLiteral("None"));
}

TEST_F(V2CalibrationDefaultsTest, Bias_NoRejectionMaps)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Bias);
    EXPECT_FALSE(s.createRejectionMaps);
    EXPECT_FALSE(s.mergeRejectionMaps);
}

TEST_F(V2CalibrationDefaultsTest, Bias_LocalNormDisabled)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Bias);
    EXPECT_FALSE(s.localNormalization.enabled);
}

TEST_F(V2CalibrationDefaultsTest, Bias_DrizzleDisabled)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Bias);
    EXPECT_FALSE(s.drizzle.enabled);
}

// ---- DARK (V2) ----

TEST_F(V2CalibrationDefaultsTest, Dark_StackMethod)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Dark);
    EXPECT_EQ(s.stackMethod, QStringLiteral("Average"));
}

TEST_F(V2CalibrationDefaultsTest, Dark_RejectionMethod)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Dark);
    EXPECT_EQ(s.rejectionMethod, QStringLiteral("Sigma Clipping"));
}

TEST_F(V2CalibrationDefaultsTest, Dark_Normalization)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Dark);
    EXPECT_EQ(s.normalizationMode, NormalizationMode::None);
}

TEST_F(V2CalibrationDefaultsTest, Dark_SigmaValues)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Dark);
    EXPECT_DOUBLE_EQ(s.sigmaLow, 3.0);
    EXPECT_DOUBLE_EQ(s.sigmaHigh, 3.0);
}

TEST_F(V2CalibrationDefaultsTest, Dark_Weighting)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Dark);
    EXPECT_EQ(s.weighting, QStringLiteral("None"));
}

TEST_F(V2CalibrationDefaultsTest, Dark_NoRejectionMaps)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Dark);
    EXPECT_FALSE(s.createRejectionMaps);
    EXPECT_FALSE(s.mergeRejectionMaps);
}

TEST_F(V2CalibrationDefaultsTest, Dark_LocalNormDisabled)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Dark);
    EXPECT_FALSE(s.localNormalization.enabled);
}

TEST_F(V2CalibrationDefaultsTest, Dark_DrizzleDisabled)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Dark);
    EXPECT_FALSE(s.drizzle.enabled);
}

// ---- FLAT (V2) ----

TEST_F(V2CalibrationDefaultsTest, Flat_StackMethod)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Flat);
    EXPECT_EQ(s.stackMethod, QStringLiteral("Average"));
}

TEST_F(V2CalibrationDefaultsTest, Flat_RejectionMethod)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Flat);
    EXPECT_EQ(s.rejectionMethod, QStringLiteral("Sigma Clipping"));
}

TEST_F(V2CalibrationDefaultsTest, Flat_NormalizationMode)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Flat);
    EXPECT_EQ(s.normalizationMode, NormalizationMode::Global);
}

TEST_F(V2CalibrationDefaultsTest, Flat_NormalizationMethod)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Flat);
    EXPECT_EQ(s.globalNormalizationMethod, GlobalNormalizationMethod::Multiplicative);
}

TEST_F(V2CalibrationDefaultsTest, Flat_SigmaValues)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Flat);
    EXPECT_DOUBLE_EQ(s.sigmaLow, 3.0);
    EXPECT_DOUBLE_EQ(s.sigmaHigh, 3.0);
}

TEST_F(V2CalibrationDefaultsTest, Flat_Weighting)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Flat);
    EXPECT_EQ(s.weighting, QStringLiteral("None"));
}

TEST_F(V2CalibrationDefaultsTest, Flat_NoRejectionMaps)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Flat);
    EXPECT_FALSE(s.createRejectionMaps);
    EXPECT_FALSE(s.mergeRejectionMaps);
}

TEST_F(V2CalibrationDefaultsTest, Flat_LocalNormDisabled)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Flat);
    EXPECT_FALSE(s.localNormalization.enabled);
}

TEST_F(V2CalibrationDefaultsTest, Flat_DrizzleDisabled)
{
    const auto s = defaultCalibrationStackingSettings(FrameType::Flat);
    EXPECT_FALSE(s.drizzle.enabled);
}

// =============================================================================
// 3. V2 model – StackingSettings struct default-constructed values (Light defaults)
// =============================================================================

class V2StackingSettingsStructDefaultsTest : public ::testing::Test
{
};

TEST_F(V2StackingSettingsStructDefaultsTest, StackMethod)
{
    StackingSettings s;
    EXPECT_EQ(s.stackMethod, QStringLiteral("Average"));
}

TEST_F(V2StackingSettingsStructDefaultsTest, RejectionMethod)
{
    StackingSettings s;
    EXPECT_EQ(s.rejectionMethod, QStringLiteral("Winsorized Sigma Clipping"));
}

TEST_F(V2StackingSettingsStructDefaultsTest, SigmaValues)
{
    StackingSettings s;
    EXPECT_DOUBLE_EQ(s.sigmaLow, 3.0);
    EXPECT_DOUBLE_EQ(s.sigmaHigh, 3.0);
}

TEST_F(V2StackingSettingsStructDefaultsTest, Weighting)
{
    StackingSettings s;
    EXPECT_EQ(s.weighting, QStringLiteral("None"));
}

TEST_F(V2StackingSettingsStructDefaultsTest, RejectionMaps)
{
    StackingSettings s;
    EXPECT_FALSE(s.createRejectionMaps);
    EXPECT_FALSE(s.mergeRejectionMaps);
    EXPECT_FALSE(s.spatialRejectionClustering);
    EXPECT_FALSE(s.restackExcludingFlaggedFrames);
}

TEST_F(V2StackingSettingsStructDefaultsTest, WeightingOptions)
{
    StackingSettings s;
    EXPECT_EQ(s.starCountScaling, QStringLiteral("Quadratic"));
    EXPECT_DOUBLE_EQ(s.exponentialAlpha, 2.0);
    EXPECT_DOUBLE_EQ(s.minWeightFloor, 0.01);
}

TEST_F(V2StackingSettingsStructDefaultsTest, MemorySettings)
{
    StackingSettings s;
    EXPECT_FALSE(s.limitMemory);
    EXPECT_EQ(s.maxMemoryMb, 2048);
}

TEST_F(V2StackingSettingsStructDefaultsTest, NormalizationMode)
{
    StackingSettings s;
    EXPECT_EQ(s.normalizationMode, NormalizationMode::Global);
}

TEST_F(V2StackingSettingsStructDefaultsTest, GlobalNormalizationMethod)
{
    StackingSettings s;
    EXPECT_EQ(s.globalNormalizationMethod, GlobalNormalizationMethod::AdditiveScaling);
}

TEST_F(V2StackingSettingsStructDefaultsTest, ScaleEstimator)
{
    StackingSettings s;
    EXPECT_EQ(s.scaleEstimator, ScaleEstimatorMethod::IKSS);
}

TEST_F(V2StackingSettingsStructDefaultsTest, NormalizationReferenceMode)
{
    StackingSettings s;
    EXPECT_EQ(s.normalizationReferenceMode, NormalizationReferenceMode::Automatic);
}

TEST_F(V2StackingSettingsStructDefaultsTest, ManualNormalizationReference_Empty)
{
    StackingSettings s;
    EXPECT_TRUE(s.manualNormalizationReference.isEmpty());
}

TEST_F(V2StackingSettingsStructDefaultsTest, FinalMasterFallbackDisabled)
{
    StackingSettings s;
    EXPECT_FALSE(s.solveFinalMasterIfNeeded);
}

// =============================================================================
// 4. V2 model – DrizzleSettings struct defaults
// =============================================================================

class V2DrizzleSettingsDefaultsTest : public ::testing::Test
{
};

TEST_F(V2DrizzleSettingsDefaultsTest, Enabled)
{
    DrizzleSettings s;
    EXPECT_FALSE(s.enabled);
}

TEST_F(V2DrizzleSettingsDefaultsTest, Scale)
{
    DrizzleSettings s;
    EXPECT_DOUBLE_EQ(s.scale, 2.0);
}

TEST_F(V2DrizzleSettingsDefaultsTest, PixelFraction)
{
    DrizzleSettings s;
    EXPECT_DOUBLE_EQ(s.pixelFraction, 0.9);
}

TEST_F(V2DrizzleSettingsDefaultsTest, Kernel)
{
    // The dialog's spelling, which a session read gives back too.
    DrizzleSettings s;
    EXPECT_EQ(s.kernel, QStringLiteral("Square"));
}

TEST_F(V2DrizzleSettingsDefaultsTest, CfaAware)
{
    DrizzleSettings s;
    EXPECT_FALSE(s.cfaAware);
}

TEST_F(V2DrizzleSettingsDefaultsTest, CfaPattern)
{
    DrizzleSettings s;
    EXPECT_EQ(s.cfaPattern, QStringLiteral("RGGB"));
}

// =============================================================================
// 5. V2 model – LocalNormalizationSettings struct defaults
// =============================================================================

class V2LocalNormDefaultsTest : public ::testing::Test
{
};

TEST_F(V2LocalNormDefaultsTest, Enabled)
{
    LocalNormalizationSettings s;
    EXPECT_FALSE(s.enabled);
}

TEST_F(V2LocalNormDefaultsTest, GridSize)
{
    LocalNormalizationSettings s;
    EXPECT_EQ(s.gridSize, 4);
}

TEST_F(V2LocalNormDefaultsTest, GridInterpolation)
{
    LocalNormalizationSettings s;
    EXPECT_EQ(s.gridInterpolation, QStringLiteral("Bicubic"));
}

TEST_F(V2LocalNormDefaultsTest, LocalScaleEnabled)
{
    LocalNormalizationSettings s;
    EXPECT_FALSE(s.localScaleEnabled);
}

TEST_F(V2LocalNormDefaultsTest, ReferenceStrategy)
{
    LocalNormalizationSettings s;
    EXPECT_EQ(s.referenceStrategy, QStringLiteral("Integrate Best Frames"));
}

TEST_F(V2LocalNormDefaultsTest, MaxIntegratedFrames)
{
    LocalNormalizationSettings s;
    EXPECT_EQ(s.maxIntegratedFrames, 16);
}

TEST_F(V2LocalNormDefaultsTest, EvaluationCriteria)
{
    LocalNormalizationSettings s;
    EXPECT_EQ(s.evaluationCriteria, QStringLiteral("PSF Signal Weight"));
}

TEST_F(V2LocalNormDefaultsTest, BackgroundEstimator)
{
    LocalNormalizationSettings s;
    EXPECT_EQ(s.backgroundEstimator, QStringLiteral("Sigma Clipped Median"));
}

TEST_F(V2LocalNormDefaultsTest, BackgroundPercentile)
{
    LocalNormalizationSettings s;
    EXPECT_DOUBLE_EQ(s.backgroundPercentile, 0.25);
}

TEST_F(V2LocalNormDefaultsTest, SigmaClipK)
{
    LocalNormalizationSettings s;
    EXPECT_DOUBLE_EQ(s.sigmaClipK, 3.0);
}

TEST_F(V2LocalNormDefaultsTest, SigmaClipIterations)
{
    LocalNormalizationSettings s;
    EXPECT_EQ(s.sigmaClipIterations, 3);
}

TEST_F(V2LocalNormDefaultsTest, BackgroundSmoothingRadius)
{
    LocalNormalizationSettings s;
    EXPECT_EQ(s.backgroundSmoothingRadius, 1);
}

TEST_F(V2LocalNormDefaultsTest, ScaleEvaluation)
{
    LocalNormalizationSettings s;
    EXPECT_EQ(s.scaleEvaluation, QStringLiteral("PSF Flux"));
}

TEST_F(V2LocalNormDefaultsTest, ScaleMode)
{
    LocalNormalizationSettings s;
    EXPECT_EQ(s.scaleMode, QStringLiteral("Linked RGB"));
}

TEST_F(V2LocalNormDefaultsTest, PsfType)
{
    LocalNormalizationSettings s;
    EXPECT_EQ(s.psfType, QStringLiteral("Auto"));
}

TEST_F(V2LocalNormDefaultsTest, MoffatBeta)
{
    LocalNormalizationSettings s;
    EXPECT_DOUBLE_EQ(s.moffatBeta, 4.0);
}

TEST_F(V2LocalNormDefaultsTest, MaxStars)
{
    LocalNormalizationSettings s;
    EXPECT_EQ(s.maxStars, 24576);
}

TEST_F(V2LocalNormDefaultsTest, MinDetectionSnr)
{
    LocalNormalizationSettings s;
    EXPECT_DOUBLE_EQ(s.minDetectionSnr, 40.0);
}

TEST_F(V2LocalNormDefaultsTest, AllowClusteredSources)
{
    LocalNormalizationSettings s;
    EXPECT_TRUE(s.allowClusteredSources);
}

TEST_F(V2LocalNormDefaultsTest, LowClip)
{
    LocalNormalizationSettings s;
    EXPECT_DOUBLE_EQ(s.lowClip, 4.5e-05);
}

TEST_F(V2LocalNormDefaultsTest, HighClip)
{
    LocalNormalizationSettings s;
    EXPECT_DOUBLE_EQ(s.highClip, 0.85);
}

TEST_F(V2LocalNormDefaultsTest, HotPixelMedianRadius)
{
    LocalNormalizationSettings s;
    EXPECT_EQ(s.hotPixelMedianRadius, 0);
}

TEST_F(V2LocalNormDefaultsTest, NoiseReductionRadius)
{
    LocalNormalizationSettings s;
    EXPECT_EQ(s.noiseReductionRadius, 0);
}

TEST_F(V2LocalNormDefaultsTest, LocalScaleSmoothingRadius)
{
    LocalNormalizationSettings s;
    EXPECT_EQ(s.localScaleSmoothingRadius, 1);
}

// =============================================================================
// 6. V2 model – normalizeCalibrationStackingSettings() enforces constraints
// =============================================================================

class V2NormalizeCalibrationSettingsTest : public ::testing::Test
{
};

TEST_F(V2NormalizeCalibrationSettingsTest, Bias_ForcesNormalizationNone)
{
    StackingSettings input;
    input.normalizationMode = NormalizationMode::Global;
    input.globalNormalizationMethod = GlobalNormalizationMethod::AdditiveScaling;
    input.localNormalization.enabled = true;

    const auto result = normalizeCalibrationStackingSettings(FrameType::Bias, input);
    EXPECT_EQ(result.normalizationMode, NormalizationMode::None);
    EXPECT_FALSE(result.localNormalization.enabled);
}

TEST_F(V2NormalizeCalibrationSettingsTest, Dark_ForcesNormalizationNone)
{
    StackingSettings input;
    input.normalizationMode = NormalizationMode::Local;
    input.localNormalization.enabled = true;

    const auto result = normalizeCalibrationStackingSettings(FrameType::Dark, input);
    EXPECT_EQ(result.normalizationMode, NormalizationMode::None);
    EXPECT_FALSE(result.localNormalization.enabled);
}

TEST_F(V2NormalizeCalibrationSettingsTest, Flat_PreservesNormalization)
{
    StackingSettings input;
    input.normalizationMode = NormalizationMode::Global;
    input.globalNormalizationMethod = GlobalNormalizationMethod::Multiplicative;

    const auto result = normalizeCalibrationStackingSettings(FrameType::Flat, input);
    EXPECT_EQ(result.normalizationMode, NormalizationMode::Global);
    EXPECT_EQ(result.globalNormalizationMethod, GlobalNormalizationMethod::Multiplicative);
}

TEST_F(V2NormalizeCalibrationSettingsTest, Light_PreservesEverything)
{
    StackingSettings input;
    input.normalizationMode = NormalizationMode::Local;
    input.localNormalization.enabled = true;
    input.rejectionMethod = QStringLiteral("GESDT");
    input.sigmaLow = 2.5;
    input.solveFinalMasterIfNeeded = false;

    const auto result = normalizeCalibrationStackingSettings(FrameType::Light, input);
    EXPECT_EQ(result.normalizationMode, NormalizationMode::Local);
    EXPECT_TRUE(result.localNormalization.enabled);
    EXPECT_EQ(result.rejectionMethod, QStringLiteral("GESDT"));
    EXPECT_DOUBLE_EQ(result.sigmaLow, 2.5);
    EXPECT_FALSE(result.solveFinalMasterIfNeeded);
}

// =============================================================================
// 7. Drizzle recommendation analysis – registration metric handoff
// =============================================================================

TEST(DrizzleRecommendationAnalysisTest, UsesLatestRegistrationRecordsByFrameAcrossRegistrationGroups)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString frameA = tempDir.filePath(QStringLiteral("light_a.fit"));
    const QString frameB = tempDir.filePath(QStringLiteral("light_b.fit"));
    const QString unrelatedFrame = tempDir.filePath(QStringLiteral("light_unrelated.fit"));
    const QString dbPath = QDir(tempDir.path()).filePath(QStringLiteral("registration_metrics.db"));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("DrizzleRecommendationAnalysisTest"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    writeMetric(writer, frameA, QStringLiteral("reg|1000x800|M42"), 1, transformJson(0.25, 0.50), 100, 1.8);
    writeMetric(writer, frameB, QStringLiteral("reg|1000x800|Alternate"), 3, transformJson(0.75, 0.25), 110, 2.2);
    writeMetric(writer, unrelatedFrame, QStringLiteral("reg|1000x800|M42"), 3, transformJson(0.10, 0.10), 120, 4.0);

    const std::vector<FrameInfo> frames = {makeLightFrame(frameA), makeLightFrame(frameB)};
    const DrizzleAnalysis analysis = computeDrizzleAnalysis(tempDir.path(), frames);

    EXPECT_TRUE(analysis.available);
    EXPECT_EQ(analysis.totalFrames, 2);
    EXPECT_EQ(analysis.framesWithMetrics, 2);
    EXPECT_EQ(analysis.framesWithTransforms, 2);
    EXPECT_EQ(analysis.framesWithUnsupportedTransforms, 0);
    EXPECT_GT(analysis.subPixelCoverage, 0.0);
    EXPECT_DOUBLE_EQ(analysis.medianFwhmPx, 2.0);
}

TEST(DrizzleRecommendationAnalysisTest, DistinguishesMetricRowsFromMissingTransforms)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString frameA = tempDir.filePath(QStringLiteral("light_a.fit"));
    const QString frameB = tempDir.filePath(QStringLiteral("light_b.fit"));
    const QString dbPath = QDir(tempDir.path()).filePath(QStringLiteral("registration_metrics.db"));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("DrizzleRecommendationAnalysisMissingTransforms"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    writeMetric(writer, frameA, QStringLiteral("reg|1000x800|M42"), 1, QString(), 100);
    writeMetric(writer, frameB, QStringLiteral("reg|1000x800|M42"), 1, QString(), 110);

    const std::vector<FrameInfo> frames = {makeLightFrame(frameA), makeLightFrame(frameB)};
    const DrizzleAnalysis analysis = computeDrizzleAnalysis(tempDir.path(), frames);

    EXPECT_FALSE(analysis.available);
    EXPECT_EQ(analysis.totalFrames, 2);
    EXPECT_EQ(analysis.framesWithMetrics, 2);
    EXPECT_EQ(analysis.framesWithTransforms, 0);
}

TEST(DrizzleRecommendationAnalysisTest, IgnoresExcludedFramesWhenCountingTransforms)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString frameA = tempDir.filePath(QStringLiteral("light_a.fit"));
    const QString frameB = tempDir.filePath(QStringLiteral("light_b.fit"));
    const QString excludedFrame = tempDir.filePath(QStringLiteral("light_excluded.fit"));
    const QString dbPath = QDir(tempDir.path()).filePath(QStringLiteral("registration_metrics.db"));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("DrizzleRecommendationAnalysisExcludedFrames"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    writeMetric(writer, frameA, QStringLiteral("reg|1000x800|M42"), 1, transformJson(0.25, 0.50), 100);
    writeMetric(writer, frameB, QStringLiteral("reg|1000x800|M42"), 1, transformJson(0.75, 0.25), 110);

    FrameInfo excluded = makeLightFrame(excludedFrame);
    excluded.excluded = true;

    const std::vector<FrameInfo> frames = {makeLightFrame(frameA), makeLightFrame(frameB), excluded};
    const DrizzleAnalysis analysis = computeDrizzleAnalysis(tempDir.path(), frames);

    EXPECT_TRUE(analysis.available);
    EXPECT_EQ(analysis.totalFrames, 2);
    EXPECT_EQ(analysis.framesWithMetrics, 2);
    EXPECT_EQ(analysis.framesWithTransforms, 2);
}

TEST(DrizzleRecommendationAnalysisTest, IgnoresFailedRegistrationRowsWhenCountingTransforms)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString frameA = tempDir.filePath(QStringLiteral("light_a.fit"));
    const QString frameB = tempDir.filePath(QStringLiteral("light_b.fit"));
    const QString failedFrame = tempDir.filePath(QStringLiteral("light_failed.fit"));
    const QString dbPath = QDir(tempDir.path()).filePath(QStringLiteral("registration_metrics.db"));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("DrizzleRecommendationAnalysisFailedFrames"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    writeMetric(writer, frameA, QStringLiteral("reg|1000x800|M42"), 1, transformJson(0.25, 0.50), 100);
    writeMetric(writer, frameB, QStringLiteral("reg|1000x800|M42"), 1, transformJson(0.75, 0.25), 110);
    writeMetric(writer, failedFrame, QStringLiteral("reg|1000x800|M42"), 1, QString(), 120, 2.0, 25.0, false);

    const std::vector<FrameInfo> frames = {makeLightFrame(frameA), makeLightFrame(frameB), makeLightFrame(failedFrame)};
    const DrizzleAnalysis analysis = computeDrizzleAnalysis(tempDir.path(), frames);

    EXPECT_TRUE(analysis.available);
    EXPECT_EQ(analysis.totalFrames, 2);
    EXPECT_EQ(analysis.framesWithMetrics, 2);
    EXPECT_EQ(analysis.framesWithTransforms, 2);
}

TEST(DrizzleRecommendationAnalysisTest, TreatsPolynomialRegistrationTransformsAsUsable)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString frameA = tempDir.filePath(QStringLiteral("light_a.fit"));
    const QString frameB = tempDir.filePath(QStringLiteral("light_b.fit"));
    const QString dbPath = QDir(tempDir.path()).filePath(QStringLiteral("registration_metrics.db"));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("DrizzleRecommendationAnalysisPolynomial"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    writeMetric(writer, frameA, QStringLiteral("reg|1000x800|M42"), 1, polynomialTransformJson(0.25, 0.50), 100);
    writeMetric(writer, frameB, QStringLiteral("reg|1000x800|M42"), 1, polynomialTransformJson(0.75, 0.10), 110);

    const std::vector<FrameInfo> frames = {makeLightFrame(frameA), makeLightFrame(frameB)};
    const DrizzleAnalysis analysis = computeDrizzleAnalysis(tempDir.path(), frames);

    EXPECT_TRUE(analysis.available);
    EXPECT_EQ(analysis.totalFrames, 2);
    EXPECT_EQ(analysis.framesWithMetrics, 2);
    EXPECT_EQ(analysis.framesWithTransforms, 2);
    EXPECT_EQ(analysis.framesWithUnsupportedTransforms, 0);
}
