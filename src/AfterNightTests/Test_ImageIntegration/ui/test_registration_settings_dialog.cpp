// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QtTest/QTest>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPoint>
#include <QPointF>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "Test_ControlsViewer/ViewerTestContext.h"
#include "Test_ImageIntegration/ui/ProcessWindowContext.h"

#include "components/AnCollapsibleSection.h"
#include "controls/ImageViewControl.h"
#include "controls/settings/SettingsKeys.h"
#include "theme/AnDesignTokens.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Algorithms/Registration/CometDetection.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include "processes/ImageIntegration/dialogs/CometTrackingDialog.h"
#include "processes/ImageIntegration/dialogs/CometTrackingDialogAutoTrackAdapter.h"
#include "processes/ImageIntegration/dialogs/CometTrackingPreviewController.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/controls/RegistrationSettingsDialog.h"
#include "processes/core/RegistrationRun.h"
#include "processes/core/WorkflowToolCompanions.h"

namespace
{
void ensureQApplication()
{
    if (QApplication::instance())
    {
        return;
    }

    static int argc = 1;
    static char* argv[] = {const_cast<char*>("test")};
    static QApplication app(argc, argv);
    (void)app;
}

/// An application context lending one engine session, installed for one case. A case declares it before the dialogs
/// it makes, so it outlives them and every image their views hold.
class ScopedSessionContext
{
public:
    ScopedSessionContext() : m_context(an::tests::viewer::openViewerContext())
    {
        if (m_context != nullptr)
        {
            m_installed.emplace(*m_context);
        }
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return m_context != nullptr;
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

private:
    std::unique_ptr<an::tests::viewer::ViewerTestContext> m_context;
    std::optional<an::tests::viewer::InstalledContext> m_installed;
};

/// An admitted batch operation of a session, for a case that runs the comet adapters directly.
std::optional<an::consumer::OperationScope> admitBatch(const an::consumer::EngineSession& session)
{
    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
    EXPECT_TRUE(scope.hasValue()) << scope.status().message();
    if (!scope.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*scope);
}

FrameInfo makeCometDialogAutoTrackFrame(const QString& path, qint64 seconds)
{
    const FrameMetadata metadata(FrameType::Light, QSize(64, 64), 30.0, 0.0, 100, 10, QStringLiteral("L"),
                                 QStringLiteral("CometUI"), QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC),
                                 QStringLiteral("Default"));
    return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2026-03-13"));
}

/// A grey 64 x 64 frame of fractions of full scale, an image of `engine`: a flat sky with a compact comet nucleus
/// centred on the pixel `comet_pixel`, so at the engine pixel coordinate half a pixel beyond its index.
an::Result<an::Image<float>> makeCometDialogAutoTrackImage(const an::EngineContext& engine, const QPoint& comet_pixel)
{
    constexpr std::uint32_t width = 64u;
    constexpr std::uint32_t height = 64u;
    constexpr float sky = 0.01f;
    constexpr double amplitude = 0.8;
    const std::optional<an::ImageDescriptor> descriptor = an::tests::viewer::describeImage(
        engine, an::kSampleTypeOf<float>, width, height, an::tests::viewer::Channels::Grey);
    if (!descriptor.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the comet frame cannot be described");
    }
    an::Result<an::Image<float>> image = an::Image<float>::create(engine, *descriptor, an::StorageArrangement::Planar,
                                                                  an::StorageInitialization::Zeroed);
    if (!image.hasValue())
    {
        return image.status();
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = image->write();
        if (!access.has_value())
        {
            return an::Status::failure(an::ErrorCode::InvalidState, "the comet frame cannot be written");
        }
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const auto row = access->view().row(0u, y);
            if (!row.has_value())
            {
                return an::Status::failure(an::ErrorCode::InvalidState, "the comet frame has no row");
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const int dx = static_cast<int>(x) - comet_pixel.x();
                const int dy = static_cast<int>(y) - comet_pixel.y();
                double value = sky;
                if (std::abs(dx) <= 4 && std::abs(dy) <= 4)
                {
                    value += amplitude * std::exp(-static_cast<double>(dx * dx + dy * dy) / 3.0);
                }
                (*row)[x] = static_cast<float>(value);
            }
        }
    }
    return image;
}

/// A reader that answers the frame each path names, and refuses a frame it holds none for.
an::ui::CometTrackingFrameReader cometFrameReader(const an::EngineContext& engine,
                                                  std::vector<std::pair<QString, QPoint>> comets)
{
    return [&engine, comets = std::move(comets)](
               const an::ui::CometTrackingFrameContext& context) -> an::Result<an::Image<float>> {
        for (const auto& [path, comet] : comets)
        {
            if (context.originalFrameId == FrameId(path))
            {
                return makeCometDialogAutoTrackImage(engine, comet);
            }
        }
        return an::Status::failure(an::ErrorCode::InvalidArgument, "no frame is held for this path");
    };
}

/// Writes a grey Float32 frame whose sample at (x, y) is ((x + y) mod 120) / 120, as a FITS file the session reads.
QString writeCometPreviewFrame(const an::consumer::EngineSession& session, const QString& directory,
                               const QString& filename, const QSize& size = QSize(80, 40))
{
    const QString path = QDir(directory).filePath(filename);
    QDir().mkpath(QFileInfo(path).absolutePath());

    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        session, static_cast<std::uint32_t>(size.width()), static_cast<std::uint32_t>(size.height()),
        an::tests::viewer::Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<float>((x + y) % 120u) / 120.0f; });
    EXPECT_TRUE(asset.has_value());
    if (asset.has_value())
    {
        an::tests::viewer::writeFitsFile(session, *asset, path);
    }
    return path;
}

/// Writes a 16 x 16 RGGB mosaic as a FITS file the session reads, its filter array stated in the header.
QString writeCfaPreviewFrame(const an::consumer::EngineSession& session, const QString& directory)
{
    const QString path = QDir(directory).filePath(QStringLiteral("mosaic_preview.fit"));
    const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    EXPECT_TRUE(rggb.has_value());
    if (!rggb.has_value())
    {
        return path;
    }
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        session, 16u, 16u, an::tests::viewer::Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return 0.01f + static_cast<float>((x * 3u + y * 5u) % 13u) * 0.01f;
        },
        an::NumericDomain::NormalizedLinear, *rggb);
    EXPECT_TRUE(asset.has_value());
    if (asset.has_value())
    {
        an::tests::viewer::writeFitsFile(session, *asset, path);
    }
    return path;
}

QWidget* findCometPreviewHost(QWidget& widget)
{
    return widget.findChild<QWidget*>(QStringLiteral("cometPreviewPlaceholderLabel"));
}

ImageViewControl* findCometImageViewControl(QWidget& widget)
{
    return dynamic_cast<ImageViewControl*>(widget.findChild<QWidget*>(QStringLiteral("cometImageViewControl")));
}

QLabel* findCometFallbackPreview(QWidget& widget)
{
    return widget.findChild<QLabel*>(QStringLiteral("cometPreviewFallbackLabel"));
}

QWidget* findCometPendingPointOverlay(QWidget& widget)
{
    return widget.findChild<QWidget*>(QStringLiteral("cometPendingPointOverlay"));
}

QCheckBox* findCometCombineWithStarsCheckBox(QWidget& widget)
{
    const auto checkboxes = widget.findChildren<QCheckBox*>();
    const auto it = std::find_if(checkboxes.cbegin(), checkboxes.cend(), [](const QCheckBox* checkbox) {
        return checkbox != nullptr &&
               checkbox->text().contains(QStringLiteral("Combine with star-based alignment"), Qt::CaseInsensitive);
    });
    return it == checkboxes.cend() ? nullptr : *it;
}

/// The Track Comet dialog's tracking and fit option controls, found by their names.
struct CometOptionControls
{
    QSpinBox* searchRadius = nullptr;
    QDoubleSpinBox* minimumSnr = nullptr;
    QSpinBox* bootstrapFrames = nullptr;
    QComboBox* trackModel = nullptr;
    QCheckBox* robustRejection = nullptr;
    QDoubleSpinBox* outlierSigma = nullptr;

    [[nodiscard]] bool found() const
    {
        return searchRadius != nullptr && minimumSnr != nullptr && bootstrapFrames != nullptr &&
               trackModel != nullptr && robustRejection != nullptr && outlierSigma != nullptr;
    }
};

CometOptionControls findCometOptionControls(QWidget& dialog)
{
    return CometOptionControls{
        .searchRadius = dialog.findChild<QSpinBox*>(QStringLiteral("cometSearchRadiusSpinBox")),
        .minimumSnr = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("cometMinimumSnrSpinBox")),
        .bootstrapFrames = dialog.findChild<QSpinBox*>(QStringLiteral("cometBootstrapFramesSpinBox")),
        .trackModel = dialog.findChild<QComboBox*>(QStringLiteral("cometTrackModelComboBox")),
        .robustRejection = dialog.findChild<QCheckBox*>(QStringLiteral("cometRobustRejectionCheckBox")),
        .outlierSigma = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("cometOutlierSigmaSpinBox"))};
}
} // namespace

TEST(RegistrationSettingsDialog, AutoReferenceCheckboxExistsAndDefaultsToEnabled)
{
    ensureQApplication();

    const QStringList frames = {QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")};
    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), frames);
    dialog.show();
    QApplication::processEvents();

    auto* checkbox = dialog.findChild<QCheckBox*>(QStringLiteral("chkAutoReferenceDuringRegistration"));
    ASSERT_NE(checkbox, nullptr);
    EXPECT_TRUE(checkbox->isChecked());
    EXPECT_TRUE(dialog.getAutoReferenceDuringRegistration());

    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("referenceImageComboBox"));
    auto* auto_btn = dialog.findChild<QPushButton*>(QStringLiteral("autoAssignButton"));
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(auto_btn, nullptr);

    EXPECT_FALSE(combo->isEnabled());
    EXPECT_FALSE(auto_btn->isEnabled());
}

TEST(RegistrationSettingsDialog, DisablingAutoReferenceReEnablesManualReferenceControls)
{
    ensureQApplication();

    const QStringList frames = {QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")};
    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), frames);
    dialog.show();
    QApplication::processEvents();

    auto* checkbox = dialog.findChild<QCheckBox*>(QStringLiteral("chkAutoReferenceDuringRegistration"));
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("referenceImageComboBox"));
    auto* auto_btn = dialog.findChild<QPushButton*>(QStringLiteral("autoAssignButton"));
    ASSERT_NE(checkbox, nullptr);
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(auto_btn, nullptr);

    checkbox->setChecked(false);
    QApplication::processEvents();

    EXPECT_FALSE(dialog.getAutoReferenceDuringRegistration());
    EXPECT_TRUE(combo->isEnabled());
    EXPECT_TRUE(auto_btn->isEnabled());

    dialog.setReferenceImage(frames.at(1));
    EXPECT_EQ(dialog.getReferenceImage(), frames.at(1));
}

TEST(RegistrationSettingsDialog, RegistrationSettingsSnapshotReflectsLiveWidgetEdits)
{
    ensureQApplication();

    const QStringList frames = {QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")};
    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), frames);
    dialog.show();
    QApplication::processEvents();

    dialog.setAutoReferenceDuringRegistration(false);
    dialog.m_ui.referenceImageComboBox->setCurrentText(frames.at(1));
    dialog.m_ui.maxStarPairsSpinBox->setValue(321);
    dialog.m_ui.thresholdSpinBox->setValue(2.5);
    dialog.m_ui.chkEnableAmplitudeRange->setChecked(true);
    dialog.m_ui.thresholdMinimumSpinBox->setValue(0.25);
    dialog.m_ui.thresholdMaximumSpinBox->setValue(0.75);
    dialog.m_ui.roundnessThresholdSpinBox->setValue(0.35);
    dialog.m_ui.chkEnableRoundnessRange->setChecked(true);
    dialog.m_ui.roundnessThresholdMaximumSpinBox->setValue(0.82);
    dialog.m_ui.convergenceSpinBox->setValue(2);
    dialog.m_ui.relaxPSFChecksCheckBox->setChecked(true);
    dialog.m_ui.profileTypeComboBox->setCurrentText(QStringLiteral("Moffat"));
    dialog.m_ui.minimumBetaSpinBox->setValue(2.2);
    QApplication::processEvents();

    const RegistrationSettings first = dialog.getRegistrationSettings();
    EXPECT_EQ(first.referenceImage, frames.at(1));
    EXPECT_EQ(first.maxStarPairs, 321);
    EXPECT_DOUBLE_EQ(first.threshold, 2.5);
    EXPECT_TRUE(first.amplitudeRangeEnabled);
    EXPECT_DOUBLE_EQ(first.thresholdMinimum, 0.25);
    EXPECT_DOUBLE_EQ(first.thresholdMaximum, 0.75);
    EXPECT_DOUBLE_EQ(first.roundnessThreshold, 0.35);
    EXPECT_TRUE(first.roundnessRangeEnabled);
    EXPECT_DOUBLE_EQ(first.roundnessThresholdMaximum, 0.82);
    EXPECT_EQ(first.convergence, 2);
    EXPECT_TRUE(first.relaxPsfChecks);
    EXPECT_EQ(first.profileType, QStringLiteral("Moffat"));
    EXPECT_DOUBLE_EQ(first.minimumBeta, 2.2);
    EXPECT_FALSE(first.autoReferenceDuringRegistration);

    dialog.m_ui.thresholdSpinBox->setValue(3.75);
    dialog.m_ui.maxStarPairsSpinBox->setValue(128);
    QApplication::processEvents();

    const RegistrationSettings second = dialog.getRegistrationSettings();
    EXPECT_DOUBLE_EQ(second.threshold, 3.75);
    EXPECT_EQ(second.maxStarPairs, 128);
}

TEST(RegistrationSettingsDialog, MaxStarPairsAboveTheFormerDialogCapRoundTripsIntact)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    RegistrationSettings settings;
    settings.maxStarPairs = 15000;
    dialog.setRegistrationSettings(settings);

    auto* spin = dialog.findChild<QSpinBox*>(QStringLiteral("maxStarPairsSpinBox"));
    ASSERT_NE(spin, nullptr);
    EXPECT_EQ(spin->maximum(), 20000);
    EXPECT_EQ(spin->value(), 15000);
    EXPECT_EQ(dialog.getMaxStarPairs(), 15000);
    EXPECT_EQ(dialog.getRegistrationSettings().maxStarPairs, 15000);
}

TEST(RegistrationSettingsDialog, TooltipExplainsWhyManualControlsAreDisabled)
{
    ensureQApplication();

    const QStringList frames = {QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")};
    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), frames);
    dialog.show();
    QApplication::processEvents();

    auto* checkbox = dialog.findChild<QCheckBox*>(QStringLiteral("chkAutoReferenceDuringRegistration"));
    ASSERT_NE(checkbox, nullptr);
    EXPECT_TRUE(checkbox->toolTip().contains(QStringLiteral("Star detection")));
    EXPECT_TRUE(checkbox->toolTip().contains(QStringLiteral("Registration (final pass)")));

    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("referenceImageComboBox"));
    auto* auto_btn = dialog.findChild<QPushButton*>(QStringLiteral("autoAssignButton"));
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(auto_btn, nullptr);

    EXPECT_TRUE(combo->toolTip().contains(QStringLiteral("Auto-select reference during Registration")));
    EXPECT_TRUE(auto_btn->toolTip().contains(QStringLiteral("Auto-select reference during Registration")));
}

TEST(RegistrationSettingsDialog, UsesWindowModalLifecycleDefaults)
{
    ensureQApplication();

    const QStringList frames = {QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")};
    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), frames);

    EXPECT_TRUE(dialog.isModal());
    EXPECT_EQ(dialog.windowModality(), Qt::WindowModal);
    EXPECT_FALSE(dialog.windowFlags().testFlag(Qt::WindowContextHelpButtonHint));
}

TEST(RegistrationSettingsDialog, TransformSelectorIncludesPolynomialAndAutoModes)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    dialog.show();
    QApplication::processEvents();

    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("transformationComboBox"));
    ASSERT_NE(combo, nullptr);

    EXPECT_NE(combo->findText(QStringLiteral("Similarity")), -1);
    EXPECT_NE(combo->findText(QStringLiteral("Affine")), -1);
    EXPECT_NE(combo->findText(QStringLiteral("Homography")), -1);
    EXPECT_NE(combo->findText(QStringLiteral("Polynomial 2")), -1);
    EXPECT_NE(combo->findText(QStringLiteral("Polynomial 3")), -1);
    EXPECT_NE(combo->findText(QStringLiteral("Polynomial 4")), -1);
    EXPECT_NE(combo->findText(QStringLiteral("Auto")), -1);
    EXPECT_EQ(combo->currentText(), QStringLiteral("Auto"));
    EXPECT_EQ(dialog.getTransformation(), QStringLiteral("Homography"));
    EXPECT_EQ(dialog.getTransformMode(), QStringLiteral("Auto"));
}

TEST(RegistrationSettingsDialog, RegistrationMethodSelectorIncludesPhaseCorrelation)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    dialog.show();
    QApplication::processEvents();

    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("registrationMethodComboBox"));
    ASSERT_NE(combo, nullptr);

    EXPECT_NE(combo->findText(QStringLiteral("Star Matching")), -1);
    EXPECT_NE(combo->findText(QStringLiteral("Phase Correlation")), -1);
    EXPECT_NE(combo->findText(QStringLiteral("Manual Points")), -1);
    EXPECT_NE(combo->findText(QStringLiteral("Comet Tracking")), -1);
}

// A group's stored Auto-solve was dropped when the dialog opened where no plate solver is configured, so accepting the
// dialog moved the group's registration hash and registered it again. The control shows it off and disabled there -
// the run solves nothing - and gives the stored choice back; a Gaia directory holding no catalog configures no
// solver, as the run and the solve service find.
TEST(RegistrationSettingsDialog, AutoSolveUnsolvedFramesRoundTripsThroughDialogApi)
{
    ensureQApplication();
    QTemporaryDir not_a_catalog;
    ASSERT_TRUE(not_a_catalog.isValid());
    an::tests::integration::ScopedProcessWindowContext installed;
    ASSERT_TRUE(installed.context().upsert(QString::fromUtf8(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY),
                                           not_a_catalog.path()));

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    RegistrationSettings settings;
    settings.autoSolveUnsolvedFrames = true;
    dialog.setRegistrationSettings(settings);

    auto* checkbox = dialog.findChild<QCheckBox*>(QStringLiteral("chkAutoSolveUnsolvedFrames"));
    ASSERT_NE(checkbox, nullptr);
    EXPECT_FALSE(checkbox->isEnabled());
    EXPECT_FALSE(checkbox->isChecked());
    EXPECT_TRUE(dialog.getAutoSolveUnsolvedFrames());
    EXPECT_TRUE(dialog.getRegistrationSettings().autoSolveUnsolvedFrames) << "the stored choice was dropped";
    EXPECT_TRUE(checkbox->toolTip().contains(QStringLiteral("plate solver")));
    EXPECT_TRUE(checkbox->toolTip().contains(QStringLiteral("Settings")));
}

// The integration's registration sends a mosaic's panels star matching cannot register to the configured plate solver
// whatever Auto-solve says, which only the mosaic tooltip's "uses astrometry when available" hinted at. Choosing Mosaic
// shows Auto-solve on and fixed, with a note saying why, and another canvas gives back the choice the group stores,
// which Mosaic never changes.
TEST(RegistrationSettingsDialog, MosaicAlignmentShowsAutoSolveOnAndKeepsTheChoiceTheGroupStores)
{
    ensureQApplication();
    // The internal solver over a ready catalog that holds no star configures a solver and reaches no network.
    QTemporaryDir catalog;
    ASSERT_TRUE(catalog.isValid());
    an::tests::integration::ScopedProcessWindowContext installed;
    ASSERT_TRUE(an::tests::integration::configureEmptyGaiaSolver(installed.context(), catalog.path()));

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    RegistrationSettings settings;
    settings.autoSolveUnsolvedFrames = false;
    settings.alignmentMode = QStringLiteral("mosaic");
    dialog.setRegistrationSettings(settings);

    auto* checkbox = dialog.findChild<QCheckBox*>(QStringLiteral("chkAutoSolveUnsolvedFrames"));
    auto* note = dialog.findChild<QLabel*>(QStringLiteral("autoSolveMosaicNote"));
    ASSERT_NE(checkbox, nullptr);
    ASSERT_NE(note, nullptr);
    const auto shows_what_runs = [&dialog, checkbox]() {
        const RegistrationSettings stated = dialog.getRegistrationSettings();
        return checkbox->isChecked() ==
               ImageIntegration::RegistrationSettingsUtils::allowsPlateSolveDuringRegistration(
                   stated.autoSolveUnsolvedFrames, stated.alignmentMode == QStringLiteral("mosaic"), true);
    };
    EXPECT_TRUE(checkbox->isChecked());
    EXPECT_FALSE(checkbox->isEnabled());
    EXPECT_FALSE(note->isHidden());
    EXPECT_TRUE(note->text().contains(QStringLiteral("plate solver"))) << note->text().toStdString();
    EXPECT_TRUE(checkbox->toolTip().contains(QStringLiteral("Mosaic"))) << checkbox->toolTip().toStdString();
    EXPECT_TRUE(dialog.m_ui.alignModeMosaic->toolTip().contains(QStringLiteral("plate solver")))
        << dialog.m_ui.alignModeMosaic->toolTip().toStdString();
    EXPECT_FALSE(dialog.getRegistrationSettings().autoSolveUnsolvedFrames) << "Mosaic changed the stored choice";
    EXPECT_TRUE(shows_what_runs());

    dialog.m_ui.alignModeStandard->setChecked(true);
    EXPECT_FALSE(checkbox->isChecked());
    EXPECT_TRUE(checkbox->isEnabled());
    EXPECT_TRUE(note->isHidden());
    EXPECT_TRUE(shows_what_runs());

    checkbox->setChecked(true);
    dialog.m_ui.alignModeMosaic->setChecked(true);
    EXPECT_TRUE(checkbox->isChecked());
    EXPECT_FALSE(checkbox->isEnabled());
    EXPECT_TRUE(shows_what_runs());
    dialog.m_ui.alignModeIntersection->setChecked(true);
    EXPECT_TRUE(checkbox->isChecked());
    EXPECT_TRUE(checkbox->isEnabled());
    EXPECT_TRUE(note->isHidden());
    EXPECT_TRUE(dialog.getRegistrationSettings().autoSolveUnsolvedFrames);
    EXPECT_TRUE(shows_what_runs());
}

// The standalone registration solves a frame only when Auto-solve is on, whatever the canvas, so over its settings the
// dialog leaves Auto-solve as it is under Mosaic and says nothing of the integration's rule.
TEST(RegistrationSettingsDialog, TheStandaloneRegistrationsMosaicLeavesAutoSolveAsTheWindowRunsIt)
{
    ensureQApplication();
    QTemporaryDir catalog;
    ASSERT_TRUE(catalog.isValid());
    an::tests::integration::ScopedProcessWindowContext installed;
    ASSERT_TRUE(an::tests::integration::configureEmptyGaiaSolver(installed.context(), catalog.path()));

    QWidget window;
    an::ui::processes::StandaloneRegistrationSettingsRequest request;
    request.settings.autoSolveUnsolvedFrames = false;
    request.settings.alignmentMode = QStringLiteral("mosaic");
    request.frames = {QStringLiteral("C:/light_a.fit")};
    an::ui::processes::openStandaloneRegistrationSettings(&window, request, {});
    auto* dialog = window.findChild<RegistrationSettingsDialog*>();
    ASSERT_NE(dialog, nullptr);
    auto* checkbox = dialog->findChild<QCheckBox*>(QStringLiteral("chkAutoSolveUnsolvedFrames"));
    auto* note = dialog->findChild<QLabel*>(QStringLiteral("autoSolveMosaicNote"));
    ASSERT_NE(checkbox, nullptr);
    ASSERT_NE(note, nullptr);
    EXPECT_TRUE(dialog->m_ui.alignModeMosaic->isChecked());
    EXPECT_FALSE(checkbox->isChecked());
    EXPECT_TRUE(checkbox->isEnabled());
    EXPECT_TRUE(note->isHidden());
    EXPECT_FALSE(dialog->m_ui.alignModeMosaic->toolTip().contains(QStringLiteral("whatever")))
        << dialog->m_ui.alignModeMosaic->toolTip().toStdString();
}

// The group's Gaia enrichment, which the run hashes and reads, had no control in the integration's dialog. It shows
// and keeps the group's choice, and stays offered where no catalog is installed, so a group can turn off what the run
// cannot do there.
TEST(RegistrationSettingsDialog, GaiaEnrichmentHasAControlThatShowsAndKeepsTheGroupsChoice)
{
    ensureQApplication();
    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    auto* checkbox = dialog.findChild<QCheckBox*>(QStringLiteral("chkEnrichWithGaiaCatalog"));
    ASSERT_NE(checkbox, nullptr);
    EXPECT_TRUE(checkbox->isChecked()) << "on by default, as the group's record is";
    EXPECT_TRUE(checkbox->isEnabled());
    EXPECT_TRUE(checkbox->toolTip().contains(QStringLiteral("Gaia catalog"))) << checkbox->toolTip().toStdString();

    RegistrationSettings settings;
    settings.enrichWithGaiaCatalog = false;
    dialog.setRegistrationSettings(settings);
    EXPECT_FALSE(checkbox->isChecked());
    EXPECT_FALSE(dialog.getRegistrationSettings().enrichWithGaiaCatalog);

    checkbox->setChecked(true);
    EXPECT_TRUE(dialog.getRegistrationSettings().enrichWithGaiaCatalog);

    checkbox->setChecked(false);
    dialog.resetToDefaults();
    EXPECT_TRUE(checkbox->isChecked());
    EXPECT_TRUE(dialog.getRegistrationSettings().enrichWithGaiaCatalog);
}

// A detector value stored beyond a control's range - a radius above the largest, a roundness or a Moffat exponent below
// the smallest, which the run clamps as the control does - is kept while the control shows it clamped: accepting the
// dialog untouched rewrote it as the clamped value, and the group's stars were detected and registered again for
// nothing. A value the person changes is the control's.
TEST(RegistrationSettingsDialog, ADetectorValueStoredBeyondItsControlsRangeIsKeptWhileShownClamped)
{
    ensureQApplication();
    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    RegistrationSettings stored;
    stored.radius = 50;
    stored.roundnessThreshold = 0.001;
    stored.roundnessThresholdMaximum = 0.002;
    stored.minimumBeta = 0.01;
    dialog.setRegistrationSettings(stored);
    ASSERT_EQ(dialog.m_ui.radiusSpinBox->value(), dialog.m_ui.radiusSpinBox->maximum())
        << "the control shows it clamped";

    const RegistrationSettings untouched = dialog.getRegistrationSettings();
    EXPECT_EQ(untouched.radius, 50);
    EXPECT_DOUBLE_EQ(untouched.roundnessThreshold, 0.001);
    EXPECT_DOUBLE_EQ(untouched.roundnessThresholdMaximum, 0.002);
    EXPECT_DOUBLE_EQ(untouched.minimumBeta, 0.01);

    dialog.m_ui.radiusSpinBox->setValue(20);
    dialog.m_ui.minimumBetaSpinBox->setValue(1.5);
    const RegistrationSettings changed = dialog.getRegistrationSettings();
    EXPECT_EQ(changed.radius, 20);
    EXPECT_DOUBLE_EQ(changed.minimumBeta, 1.5);
    EXPECT_DOUBLE_EQ(changed.roundnessThreshold, 0.001) << "an untouched value stays as stored";
}

TEST(RegistrationSettingsDialog, PolynomialTransformContractRoundTripsThroughDialogApi)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    RegistrationSettings settings;
    settings.transformation = QStringLiteral("Homography");
    settings.transformMode = QStringLiteral("polynomial");
    settings.transformOrder = 3;
    settings.twoPassRefinementEnabled = true;
    dialog.setRegistrationSettings(settings);

    EXPECT_EQ(dialog.getTransformation(), QStringLiteral("Homography"));
    EXPECT_EQ(dialog.getTransformMode(), QStringLiteral("polynomial"));
    EXPECT_EQ(dialog.getTransformOrder(), 3);
    EXPECT_TRUE(dialog.getTwoPassRefinementEnabled());

    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("transformationComboBox"));
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->currentText(), QStringLiteral("Polynomial 3"));
}

// The star matcher fits the linear model a linear mode names, whatever order and two-pass flag are stored beside it, so
// the dialog shows that model rather than a polynomial the run does not fit.
TEST(RegistrationSettingsDialog, AnOrderBesideALinearModeIsShownAsTheLinearModelTheRunFits)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    RegistrationSettings settings;
    settings.transformation = QStringLiteral("Affine");
    settings.transformMode = QStringLiteral("linear");
    settings.transformOrder = 3;
    settings.twoPassRefinementEnabled = true;
    dialog.setRegistrationSettings(settings);

    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("transformationComboBox"));
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->currentText(), QStringLiteral("Affine"));
    EXPECT_EQ(dialog.getTransformMode(), QStringLiteral("linear"));
    EXPECT_EQ(dialog.getTransformOrder(), 0);
    EXPECT_FALSE(dialog.getTwoPassRefinementEnabled());
}

TEST(RegistrationSettingsDialog, PhaseCorrelationMethodRoundTripsThroughDialogApi)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("phase_correlation");
    dialog.setRegistrationSettings(settings);

    EXPECT_EQ(dialog.getRegistrationMethod(), QStringLiteral("phase_correlation"));

    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("registrationMethodComboBox"));
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->currentText(), QStringLiteral("Phase Correlation"));
}

TEST(RegistrationSettingsDialog, PhaseCorrelationDisablesAutoReferenceButKeepsManualReferenceControls)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"),
                                      {QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")});
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("phase_correlation");
    settings.autoReferenceDuringRegistration = true;
    settings.referenceImage = QStringLiteral("C:/light_a.fit");
    dialog.setRegistrationSettings(settings);
    dialog.show();
    QApplication::processEvents();

    auto* checkbox = dialog.findChild<QCheckBox*>(QStringLiteral("chkAutoReferenceDuringRegistration"));
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("referenceImageComboBox"));
    auto* autoAssignButton = dialog.findChild<QPushButton*>(QStringLiteral("autoAssignButton"));
    ASSERT_NE(checkbox, nullptr);
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(autoAssignButton, nullptr);

    EXPECT_FALSE(checkbox->isEnabled());
    EXPECT_FALSE(checkbox->isChecked());
    EXPECT_FALSE(dialog.getAutoReferenceDuringRegistration());
    EXPECT_TRUE(combo->isEnabled());
    EXPECT_TRUE(autoAssignButton->isEnabled());
    EXPECT_TRUE(checkbox->toolTip().contains(QStringLiteral("Phase Correlation uses a fixed reference frame")));
    EXPECT_TRUE(combo->toolTip().contains(QStringLiteral("phase correlation registration")));
}

TEST(RegistrationSettingsDialog, ManualPointsMethodRoundTripsThroughDialogApi)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"),
                                      {QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")});
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("manual_points");
    settings.referenceImage = QStringLiteral("C:/light_a.fit");
    settings.manualControlPointsByFrame.insert(
        QStringLiteral("C:/light_b.fit"),
        RegistrationSettings::ManualControlPointList{{QPointF(14.0, 14.0), QPointF(12.0, 12.0)},
                                                     {QPointF(22.0, 16.0), QPointF(20.0, 14.0)}});
    dialog.setRegistrationSettings(settings);

    EXPECT_EQ(dialog.getRegistrationMethod(), QStringLiteral("manual_points"));
    EXPECT_EQ(dialog.getReferenceImage(), QStringLiteral("C:/light_a.fit"));
    ASSERT_EQ(dialog.getManualControlPointsByFrame().size(), 1);
    ASSERT_TRUE(dialog.getManualControlPointsByFrame().contains(QStringLiteral("C:/light_b.fit")));
    EXPECT_EQ(dialog.getManualControlPointsByFrame().value(QStringLiteral("C:/light_b.fit")).size(), 2);

    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("registrationMethodComboBox"));
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->currentText(), QStringLiteral("Manual Points"));
}

TEST(CometTrackingDialog, WindowCanMaximizeResizeAndPrioritizesPreviewRow)
{
    ensureQApplication();

    an::ui::CometTrackingDialog dialog({QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")});

    EXPECT_TRUE(dialog.windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
    EXPECT_TRUE(dialog.isSizeGripEnabled());
    EXPECT_LT(dialog.minimumWidth(), dialog.maximumWidth());
    EXPECT_LT(dialog.minimumHeight(), dialog.maximumHeight());

    auto* mainSplitter = dialog.findChild<QSplitter*>(QStringLiteral("cometTrackingMainSplitter"));
    auto* framePreviewSplitter = dialog.findChild<QSplitter*>(QStringLiteral("cometFramePreviewSplitter"));
    ASSERT_NE(mainSplitter, nullptr);
    ASSERT_NE(framePreviewSplitter, nullptr);
    EXPECT_EQ(mainSplitter->orientation(), Qt::Vertical);
    EXPECT_EQ(framePreviewSplitter->orientation(), Qt::Horizontal);
    EXPECT_EQ(mainSplitter->count(), 3);
    EXPECT_EQ(framePreviewSplitter->count(), 2);

    dialog.resize(1000, 800);
    dialog.show();
    QApplication::processEvents();

    const QList<int> row_sizes = mainSplitter->sizes();
    ASSERT_EQ(row_sizes.size(), 3);
    EXPECT_GT(row_sizes.at(0), row_sizes.at(1));
    EXPECT_GT(row_sizes.at(0), row_sizes.at(2));

    const QSize resized_size = dialog.size() + QSize(120, 90);
    dialog.resize(resized_size);
    QApplication::processEvents();
    EXPECT_GE(dialog.width(), resized_size.width());
    EXPECT_GE(dialog.height(), resized_size.height());
}

TEST(CometTrackingDialog, RequiresTwoAcceptedObservationsBeforeProductionFit)
{
    ensureQApplication();

    an::ui::CometTrackingDialog dialog({QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")});

    RegistrationSettings::CometTrackingSettings settings;
    settings.observations = {
        RegistrationSettings::CometTrackingObservation{QStringLiteral("C:/light_a.fit"), QPointF(10.0, 10.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true}};
    dialog.setCometTrackingSettings(settings);
    EXPECT_FALSE(dialog.canAcceptProductionFit());

    settings.observations.push_back(RegistrationSettings::CometTrackingObservation{
        QStringLiteral("C:/light_b.fit"), QPointF(20.0, 14.0),
        RegistrationSettings::CometObservationOrigin::AcceptedAutomatic, true});
    dialog.setCometTrackingSettings(settings);
    EXPECT_TRUE(dialog.canAcceptProductionFit());
}

TEST(CometTrackingDialog, OkButtonRequiresTwoAcceptedObservations)
{
    ensureQApplication();

    an::ui::CometTrackingDialog dialog({QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")});
    RegistrationSettings::CometTrackingSettings settings;
    settings.observations = {
        RegistrationSettings::CometTrackingObservation{QStringLiteral("C:/light_a.fit"), QPointF(10.0, 10.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true}};
    dialog.setCometTrackingSettings(settings);
    dialog.show();
    QApplication::processEvents();

    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    ASSERT_NE(buttons, nullptr);
    QPushButton* ok_button = buttons->button(QDialogButtonBox::Ok);
    ASSERT_NE(ok_button, nullptr);

    QTest::mouseClick(ok_button, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_NE(dialog.result(), QDialog::Accepted);
    EXPECT_TRUE(dialog.isVisible());

    settings.observations.push_back(RegistrationSettings::CometTrackingObservation{
        QStringLiteral("C:/light_b.fit"), QPointF(20.0, 14.0),
        RegistrationSettings::CometObservationOrigin::AcceptedAutomatic, true});
    dialog.setCometTrackingSettings(settings);
    QTest::mouseClick(ok_button, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_EQ(dialog.result(), QDialog::Accepted);
}

TEST(CometTrackingDialog, AutoTrackRunsAsyncHandlerAndDisplaysCandidatesForAcceptance)
{
    ensureQApplication();

    an::ui::CometTrackingDialog dialog({QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")});
    RegistrationSettings::CometTrackingSettings settings;
    settings.observations = {
        RegistrationSettings::CometTrackingObservation{QStringLiteral("C:/light_a.fit"), QPointF(10.0, 10.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true}};
    dialog.setCometTrackingSettings(settings);

    an::ui::CometTrackingAutoTrackCompletion completion;
    dialog.setAutoTrackHandler([&](const RegistrationSettings::CometTrackingSettings& request_settings,
                                   an::ui::CometTrackingAutoTrackCompletion done) {
        EXPECT_EQ(request_settings.observations.size(), 1);
        EXPECT_EQ(request_settings.observations.front().frameId, QStringLiteral("C:/light_a.fit"));
        completion = std::move(done);
    });

    auto* autoTrackButton = dialog.findChild<QPushButton*>(QStringLiteral("autoTrackCometButton"));
    auto* diagnosticsTable = dialog.findChild<QTableWidget*>(QStringLiteral("cometDiagnosticsTable"));
    auto* acceptButton = dialog.findChild<QPushButton*>(QStringLiteral("acceptCometTrackingButton"));
    ASSERT_NE(autoTrackButton, nullptr);
    ASSERT_NE(diagnosticsTable, nullptr);
    ASSERT_NE(acceptButton, nullptr);
    EXPECT_TRUE(autoTrackButton->isEnabled());

    QTest::mouseClick(autoTrackButton, Qt::LeftButton);
    ASSERT_TRUE(static_cast<bool>(completion));
    EXPECT_FALSE(autoTrackButton->isEnabled());
    EXPECT_EQ(diagnosticsTable->rowCount(), 0);

    an::ui::CometTrackingDialogDiagnostic diagnostic;
    diagnostic.frameId = QStringLiteral("C:/light_b.fit");
    diagnostic.sourcePosition = QPointF(21.5, 19.25);
    diagnostic.confidence = 0.88;
    diagnostic.state = QStringLiteral("Candidate");
    diagnostic.message = QStringLiteral("Automatic bootstrap candidate");
    completion({diagnostic}, QStringLiteral("Auto-tracking completed."));

    EXPECT_EQ(diagnosticsTable->rowCount(), 1);
    diagnosticsTable->selectRow(0);
    EXPECT_TRUE(acceptButton->isEnabled());
    QTest::mouseClick(acceptButton, Qt::LeftButton);

    const RegistrationSettings::CometTrackingSettings updated = dialog.cometTrackingSettings();
    ASSERT_EQ(updated.observations.size(), 2);
    EXPECT_EQ(updated.observations[1].frameId, QStringLiteral("C:/light_b.fit"));
    EXPECT_EQ(updated.observations[1].origin, RegistrationSettings::CometObservationOrigin::AcceptedAutomatic);
}

// The tracking and fit options are the Track Comet dialog's own controls, in a section of their own: each shows what
// the settings store, and the settings the dialog gives back are what the controls show.
TEST(CometTrackingDialog, TheTrackingAndFitOptionsShowTheStoredValuesAndGiveBackWhatIsSet)
{
    ensureQApplication();

    an::ui::CometTrackingDialog dialog({QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")});
    EXPECT_NE(dynamic_cast<an::ui::AnCollapsibleSection*>(
                  dialog.findChild<QWidget*>(QStringLiteral("cometTrackingOptionsSection"))),
              nullptr);
    const CometOptionControls controls = findCometOptionControls(dialog);
    ASSERT_TRUE(controls.found());

    RegistrationSettings::CometTrackingSettings settings;
    settings.trackingOptions.searchRadiusPixels = 48.0;
    settings.trackingOptions.minimumSnr = 4.5;
    settings.trackingOptions.maxBootstrapFrames = 12;
    settings.fitOptions.model = QStringLiteral("quadratic");
    settings.fitOptions.robustRejection = false;
    settings.fitOptions.outlierSigma = 2.5;
    dialog.setCometTrackingSettings(settings);

    EXPECT_EQ(controls.searchRadius->value(), 48);
    EXPECT_DOUBLE_EQ(controls.minimumSnr->value(), 4.5);
    EXPECT_EQ(controls.bootstrapFrames->value(), 12);
    EXPECT_EQ(controls.trackModel->currentData().toString(), QStringLiteral("quadratic"));
    EXPECT_FALSE(controls.robustRejection->isChecked());
    EXPECT_DOUBLE_EQ(controls.outlierSigma->value(), 2.5);
    // The multiple applies only while outlying observations are rejected.
    EXPECT_FALSE(controls.outlierSigma->isEnabled());
    EXPECT_TRUE(dialog.cometTrackingSettings().trackingOptions == settings.trackingOptions);
    EXPECT_TRUE(dialog.cometTrackingSettings().fitOptions == settings.fitOptions);

    controls.searchRadius->setValue(20);
    controls.minimumSnr->setValue(6.0);
    controls.bootstrapFrames->setValue(3);
    controls.trackModel->setCurrentIndex(controls.trackModel->findData(QStringLiteral("automatic")));
    controls.robustRejection->setChecked(true);
    EXPECT_TRUE(controls.outlierSigma->isEnabled());
    controls.outlierSigma->setValue(4.0);

    const RegistrationSettings::CometTrackingSettings set = dialog.cometTrackingSettings();
    EXPECT_DOUBLE_EQ(set.trackingOptions.searchRadiusPixels, 20.0);
    EXPECT_DOUBLE_EQ(set.trackingOptions.minimumSnr, 6.0);
    EXPECT_EQ(set.trackingOptions.maxBootstrapFrames, 3);
    EXPECT_EQ(set.fitOptions.model, QStringLiteral("automatic"));
    EXPECT_TRUE(set.fitOptions.robustRejection);
    EXPECT_DOUBLE_EQ(set.fitOptions.outlierSigma, 4.0);
}

// The controls offer the values the track runs with - the nucleus search radius the engine's centre refinement takes,
// an outlier multiple of at least one, the three track models - and start at the defaults the settings hold.
TEST(CometTrackingDialog, TheOptionControlsOfferWhatTheTrackRunsWithAndStartAtTheDefaults)
{
    ensureQApplication();

    an::ui::CometTrackingDialog dialog({QStringLiteral("C:/light_a.fit")});
    const CometOptionControls controls = findCometOptionControls(dialog);
    ASSERT_TRUE(controls.found());

    EXPECT_EQ(controls.searchRadius->minimum(), 1);
    EXPECT_EQ(controls.searchRadius->maximum(), static_cast<int>(an::kMaximumCometCenterRadius));
    EXPECT_GT(controls.minimumSnr->minimum(), 0.0);
    EXPECT_EQ(controls.bootstrapFrames->minimum(), 0);
    EXPECT_DOUBLE_EQ(controls.outlierSigma->minimum(), 1.0);
    ASSERT_EQ(controls.trackModel->count(), 3);
    EXPECT_GE(controls.trackModel->findData(QStringLiteral("linear")), 0);
    EXPECT_GE(controls.trackModel->findData(QStringLiteral("automatic")), 0);
    EXPECT_GE(controls.trackModel->findData(QStringLiteral("quadratic")), 0);

    const RegistrationSettings::CometTrackingSettings defaults;
    EXPECT_EQ(controls.searchRadius->value(), static_cast<int>(defaults.trackingOptions.searchRadiusPixels));
    EXPECT_DOUBLE_EQ(controls.minimumSnr->value(), defaults.trackingOptions.minimumSnr);
    EXPECT_EQ(controls.bootstrapFrames->value(), defaults.trackingOptions.maxBootstrapFrames);
    EXPECT_EQ(controls.trackModel->currentData().toString(), defaults.fitOptions.model);
    EXPECT_EQ(controls.robustRejection->isChecked(), defaults.fitOptions.robustRejection);
    EXPECT_DOUBLE_EQ(controls.outlierSigma->value(), defaults.fitOptions.outlierSigma);
    EXPECT_TRUE(dialog.cometTrackingSettings().trackingOptions == defaults.trackingOptions);
    EXPECT_TRUE(dialog.cometTrackingSettings().fitOptions == defaults.fitOptions);
}

// Auto Track runs with the options the controls show, before the dialog is accepted.
TEST(CometTrackingDialog, AutoTrackRunsWithTheOptionsTheControlsShow)
{
    ensureQApplication();

    an::ui::CometTrackingDialog dialog({QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")});
    RegistrationSettings::CometTrackingSettings settings;
    settings.observations = {
        RegistrationSettings::CometTrackingObservation{QStringLiteral("C:/light_a.fit"), QPointF(10.0, 10.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true}};
    dialog.setCometTrackingSettings(settings);
    const CometOptionControls controls = findCometOptionControls(dialog);
    ASSERT_TRUE(controls.found());
    controls.searchRadius->setValue(12);
    controls.minimumSnr->setValue(5.0);
    controls.bootstrapFrames->setValue(2);
    controls.trackModel->setCurrentIndex(controls.trackModel->findData(QStringLiteral("quadratic")));
    controls.robustRejection->setChecked(false);

    std::optional<RegistrationSettings::CometTrackingSettings> requested;
    dialog.setAutoTrackHandler([&](const RegistrationSettings::CometTrackingSettings& request_settings,
                                   an::ui::CometTrackingAutoTrackCompletion) { requested = request_settings; });
    auto* autoTrackButton = dialog.findChild<QPushButton*>(QStringLiteral("autoTrackCometButton"));
    ASSERT_NE(autoTrackButton, nullptr);
    QTest::mouseClick(autoTrackButton, Qt::LeftButton);

    ASSERT_TRUE(requested.has_value());
    EXPECT_DOUBLE_EQ(requested->trackingOptions.searchRadiusPixels, 12.0);
    EXPECT_DOUBLE_EQ(requested->trackingOptions.minimumSnr, 5.0);
    EXPECT_EQ(requested->trackingOptions.maxBootstrapFrames, 2);
    EXPECT_EQ(requested->fitOptions.model, QStringLiteral("quadratic"));
    EXPECT_FALSE(requested->fitOptions.robustRejection);
}

// A value the settings store more finely than its control shows - a radius between whole pixels, a threshold with
// more decimals, an older spelling of a track model - is given back as stored while the control shows what it runs
// as, so opening and accepting the dialog changes neither the settings nor the registration's fingerprint.
TEST(CometTrackingDialog, AStoredOptionTheControlShowsAsItRunsIsGivenBackAsStored)
{
    ensureQApplication();

    an::ui::CometTrackingDialog dialog({QStringLiteral("C:/light_a.fit")});
    const CometOptionControls controls = findCometOptionControls(dialog);
    ASSERT_TRUE(controls.found());
    RegistrationSettings::CometTrackingSettings settings;
    settings.trackingOptions.searchRadiusPixels = 12.4;
    settings.trackingOptions.minimumSnr = 3.27;
    settings.fitOptions.model = QStringLiteral("prefer_quadratic");
    settings.fitOptions.outlierSigma = 2.73;
    dialog.setCometTrackingSettings(settings);

    EXPECT_EQ(controls.searchRadius->value(), 12);
    EXPECT_EQ(controls.trackModel->currentData().toString(), QStringLiteral("quadratic"));
    EXPECT_TRUE(dialog.cometTrackingSettings().trackingOptions == settings.trackingOptions);
    EXPECT_TRUE(dialog.cometTrackingSettings().fitOptions == settings.fitOptions);

    controls.searchRadius->setValue(20);
    controls.trackModel->setCurrentIndex(controls.trackModel->findData(QStringLiteral("linear")));
    const RegistrationSettings::CometTrackingSettings set = dialog.cometTrackingSettings();
    EXPECT_DOUBLE_EQ(set.trackingOptions.searchRadiusPixels, 20.0);
    EXPECT_DOUBLE_EQ(set.trackingOptions.minimumSnr, 3.27);
    EXPECT_EQ(set.fitOptions.model, QStringLiteral("linear"));
    EXPECT_DOUBLE_EQ(set.fitOptions.outlierSigma, 2.73);
}

TEST(CometTrackingDialog, FindMovingObjectRunsWithoutSeedAndAcceptsConfirmedTrack)
{
    ensureQApplication();

    an::ui::CometTrackingDialog dialog(
        {QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit"), QStringLiteral("C:/light_c.fit")});

    an::ui::CometTrackingAutoTrackCompletion completion;
    dialog.setBlindDetectHandler([&](const RegistrationSettings::CometTrackingSettings& request_settings,
                                     an::ui::CometTrackingAutoTrackCompletion done) {
        EXPECT_TRUE(request_settings.observations.empty());
        completion = std::move(done);
    });

    auto* findButton = dialog.findChild<QPushButton*>(QStringLiteral("findMovingCometButton"));
    auto* autoTrackButton = dialog.findChild<QPushButton*>(QStringLiteral("autoTrackCometButton"));
    auto* diagnosticsTable = dialog.findChild<QTableWidget*>(QStringLiteral("cometDiagnosticsTable"));
    auto* acceptButton = dialog.findChild<QPushButton*>(QStringLiteral("acceptCometTrackingButton"));
    ASSERT_NE(findButton, nullptr);
    ASSERT_NE(autoTrackButton, nullptr);
    ASSERT_NE(diagnosticsTable, nullptr);
    ASSERT_NE(acceptButton, nullptr);
    EXPECT_TRUE(findButton->isEnabled());
    EXPECT_FALSE(autoTrackButton->isEnabled());

    QTest::mouseClick(findButton, Qt::LeftButton);
    ASSERT_TRUE(static_cast<bool>(completion));
    EXPECT_FALSE(findButton->isEnabled());

    an::ui::CometTrackingDialogDiagnostic first;
    first.candidateId = QStringLiteral("blind-0");
    first.frameId = QStringLiteral("C:/light_a.fit");
    first.sourcePosition = QPointF(11.0, 20.0);
    first.confidence = 0.91;
    first.state = QStringLiteral("Candidate");
    first.message = QStringLiteral("Moving object candidate 1/2");

    an::ui::CometTrackingDialogDiagnostic second = first;
    second.frameId = QStringLiteral("C:/light_c.fit");
    second.sourcePosition = QPointF(25.0, 27.0);
    second.message = QStringLiteral("Moving object candidate 2/2");

    completion({first, second}, QStringLiteral("Moving-object search found 1 candidate track."));

    EXPECT_EQ(diagnosticsTable->rowCount(), 2);
    EXPECT_TRUE(dialog.cometTrackingSettings().observations.empty());
    diagnosticsTable->selectRow(0);
    EXPECT_TRUE(acceptButton->isEnabled());
    QTest::mouseClick(acceptButton, Qt::LeftButton);

    const RegistrationSettings::CometTrackingSettings updated = dialog.cometTrackingSettings();
    ASSERT_EQ(updated.observations.size(), 2);
    EXPECT_EQ(updated.observations[0].frameId, QStringLiteral("C:/light_a.fit"));
    EXPECT_EQ(updated.observations[1].frameId, QStringLiteral("C:/light_c.fit"));
    EXPECT_EQ(updated.observations[0].origin, RegistrationSettings::CometObservationOrigin::AcceptedAutomatic);
    EXPECT_EQ(updated.observations[1].origin, RegistrationSettings::CometObservationOrigin::AcceptedAutomatic);

    QTest::mouseClick(acceptButton, Qt::LeftButton);
    const RegistrationSettings::CometTrackingSettings reaccepted = dialog.cometTrackingSettings();
    ASSERT_EQ(reaccepted.observations.size(), 2);
    EXPECT_EQ(reaccepted.observations[0].frameId, QStringLiteral("C:/light_a.fit"));
    EXPECT_EQ(reaccepted.observations[1].frameId, QStringLiteral("C:/light_c.fit"));
}

// A position is an engine pixel coordinate: a nucleus centred on the pixel (16, 10) is at (16.5, 10.5).
TEST(CometTrackingDialogAutoTrackAdapter, BootstrapReturnsCandidateDiagnosticsWithoutAccepting)
{
    const std::unique_ptr<an::tests::viewer::ViewerTestContext> context = an::tests::viewer::openViewerContext();
    ASSERT_NE(context, nullptr);
    std::optional<an::consumer::OperationScope> scope = admitBatch(context->engineSession());
    ASSERT_TRUE(scope.has_value());

    RegistrationSettings::CometTrackingSettings settings;
    settings.observations = {
        RegistrationSettings::CometTrackingObservation{QStringLiteral("C:/light_a.fit"), QPointF(10.5, 10.5),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true}};

    an::ui::CometTrackingDialogAutoTrackRequest request;
    request.frames = {makeCometDialogAutoTrackFrame(QStringLiteral("C:/light_a.fit"), 1773440000),
                      makeCometDialogAutoTrackFrame(QStringLiteral("C:/light_b.fit"), 1773440060)};
    request.referenceFrame = FrameId(QStringLiteral("C:/light_a.fit"));
    request.settings = settings;
    request.frameReader = cometFrameReader(scope->engine(), {{QStringLiteral("C:/light_b.fit"), QPoint(16, 10)}});

    const an::ui::CometTrackingDialogAutoTrackResult result =
        an::ui::runCometTrackingDialogAutoTrack(scope->execution(), request);

    EXPECT_TRUE(result.succeeded) << result.message.toStdString();
    EXPECT_TRUE(result.bootstrapOnly);
    EXPECT_FALSE(result.productionFitAvailable);
    ASSERT_EQ(result.diagnostics.size(), 1);
    EXPECT_EQ(result.diagnostics.front().frameId, QStringLiteral("C:/light_b.fit"));
    EXPECT_EQ(result.diagnostics.front().state, QStringLiteral("Candidate"));
    EXPECT_NEAR(result.diagnostics.front().sourcePosition.x(), 16.5, 1.0);
    EXPECT_NEAR(result.diagnostics.front().sourcePosition.y(), 10.5, 1.0);
    EXPECT_EQ(settings.observations.size(), 1);
}

// The frames are fractions of full scale, which the dialog's detection options state their residual threshold in.
TEST(CometTrackingDialogBlindDetectAdapter, DetectsSyntheticMovingCandidateWithoutSeedObservations)
{
    const std::unique_ptr<an::tests::viewer::ViewerTestContext> context = an::tests::viewer::openViewerContext();
    ASSERT_NE(context, nullptr);
    std::optional<an::consumer::OperationScope> scope = admitBatch(context->engineSession());
    ASSERT_TRUE(scope.has_value());

    an::ui::CometTrackingDialogBlindDetectRequest request;
    request.frames = {makeCometDialogAutoTrackFrame(QStringLiteral("C:/light_a.fit"), 1773440000),
                      makeCometDialogAutoTrackFrame(QStringLiteral("C:/light_b.fit"), 1773440060),
                      makeCometDialogAutoTrackFrame(QStringLiteral("C:/light_c.fit"), 1773440120),
                      makeCometDialogAutoTrackFrame(QStringLiteral("C:/light_d.fit"), 1773440180)};
    request.referenceFrame = FrameId(QStringLiteral("C:/light_a.fit"));
    request.frameReader = cometFrameReader(scope->engine(), {{QStringLiteral("C:/light_a.fit"), QPoint(12, 24)},
                                                             {QStringLiteral("C:/light_b.fit"), QPoint(17, 26)},
                                                             {QStringLiteral("C:/light_c.fit"), QPoint(22, 28)},
                                                             {QStringLiteral("C:/light_d.fit"), QPoint(27, 30)}});
    EXPECT_DOUBLE_EQ(request.detectionOptions.minimumPositiveResidual, 1.0 / 65535.0);

    const an::ui::CometTrackingDialogAutoTrackResult result =
        an::ui::runCometTrackingDialogBlindDetect(scope->execution(), request);

    EXPECT_TRUE(result.succeeded) << result.message.toStdString();
    EXPECT_FALSE(result.productionFitAvailable);
    ASSERT_GE(result.diagnostics.size(), 4u);
    EXPECT_FALSE(result.diagnostics.front().candidateId.isEmpty());
    EXPECT_EQ(result.diagnostics.front().state, QStringLiteral("Candidate"));
    EXPECT_EQ(result.diagnostics.front().frameId, QStringLiteral("C:/light_a.fit"));
    EXPECT_NEAR(result.diagnostics.front().sourcePosition.x(), 12.5, 1.5);
    EXPECT_NEAR(result.diagnostics.back().sourcePosition.x(), 27.5, 1.5);
}

TEST(CometTrackingDialog, MarkButtonAddsManualObservationInSelectedFrameSourceCoordinates)
{
    ensureQApplication();

    an::ui::CometTrackingDialog dialog({QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")});
    auto* frameTable = dialog.findChild<QTableWidget*>(QStringLiteral("cometFrameTable"));
    auto* markButton = dialog.findChild<QPushButton*>(QStringLiteral("markCometButton"));
    ASSERT_NE(frameTable, nullptr);
    ASSERT_NE(markButton, nullptr);

    frameTable->selectRow(1);
    dialog.setPendingSourceFramePosition(QPointF(42.25, 8.75));
    QTest::mouseClick(markButton, Qt::LeftButton);

    const RegistrationSettings::CometTrackingSettings settings = dialog.cometTrackingSettings();
    ASSERT_EQ(settings.observations.size(), 1);
    EXPECT_EQ(settings.observations.front().frameId, QStringLiteral("C:/light_b.fit"));
    EXPECT_EQ(settings.observations.front().sourcePosition, QPointF(42.25, 8.75));
    EXPECT_EQ(settings.observations.front().origin, RegistrationSettings::CometObservationOrigin::Manual);
    EXPECT_TRUE(settings.observations.front().enabled);
}

TEST(CometTrackingDialog, PreviewClickMarksSelectedFrameSourceCoordinates)
{
    ensureQApplication();

    an::ui::CometTrackingDialog dialog({QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")});
    dialog.setFramePreviewSizeProvider([](const QString& frame_id) -> std::optional<QSize> {
        if (frame_id == QStringLiteral("C:/light_b.fit"))
        {
            return QSize(100, 50);
        }
        return std::nullopt;
    });
    dialog.resize(640, 420);
    dialog.show();
    QApplication::processEvents();

    auto* frameTable = dialog.findChild<QTableWidget*>(QStringLiteral("cometFrameTable"));
    auto* preview = findCometFallbackPreview(dialog);
    auto* markButton = dialog.findChild<QPushButton*>(QStringLiteral("markCometButton"));
    ASSERT_NE(frameTable, nullptr);
    ASSERT_NE(preview, nullptr);
    ASSERT_NE(markButton, nullptr);

    frameTable->selectRow(1);
    QApplication::processEvents();
    ASSERT_GT(preview->width(), 0);
    ASSERT_GT(preview->height(), 0);

    QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, QPoint(preview->width() / 2, preview->height() / 2));
    EXPECT_TRUE(markButton->isEnabled());
    QTest::mouseClick(markButton, Qt::LeftButton);

    const RegistrationSettings::CometTrackingSettings settings = dialog.cometTrackingSettings();
    ASSERT_EQ(settings.observations.size(), 1);
    EXPECT_EQ(settings.observations.front().frameId, QStringLiteral("C:/light_b.fit"));
    EXPECT_NEAR(settings.observations.front().sourcePosition.x(), 50.0, 0.75);
    EXPECT_NEAR(settings.observations.front().sourcePosition.y(), 25.0, 0.75);
}

TEST(CometTrackingDialog, UsesImageViewControlPreviewWithAutostretchAndAutoDebayer)
{
    ensureQApplication();

    an::ui::CometTrackingDialog dialog({QStringLiteral("C:/light_a.fit")});

    auto* imageView = findCometImageViewControl(dialog);
    ASSERT_NE(imageView, nullptr);
    EXPECT_EQ(imageView->getUniqueName(), std::string("CometTrackingPreview"));
    EXPECT_TRUE(imageView->isAutoDebayerCfaImagePreviewsEnabled());
    auto* viewWidget = imageView->getViewWidget();
    ASSERT_NE(viewWidget, nullptr);
    EXPECT_EQ(viewWidget->getViewMode(), ImageViewMode::AUTOSTRETCH_MODE);
}

TEST(CometTrackingDialog, ImageViewLoadingStateKeepsImageViewSurfaceVisible)
{
    ensureQApplication();

    an::ui::CometTrackingPreviewPane preview;
    preview.resize(640, 420);
    preview.show();
    QApplication::processEvents();

    auto* imageView = findCometImageViewControl(preview);
    auto* fallbackPreview = findCometFallbackPreview(preview);
    ASSERT_NE(imageView, nullptr);
    ASSERT_NE(fallbackPreview, nullptr);

    preview.setImageViewLoading(QSize(80, 40));
    QApplication::processEvents();

    EXPECT_TRUE(preview.property("anPreviewLoading").toBool());
    EXPECT_FALSE(preview.property("anPreviewLoaded").toBool());
    EXPECT_TRUE(imageView->isVisible());
    EXPECT_FALSE(fallbackPreview->isVisible());
}

TEST(CometTrackingDialog, SelectingExistingFrameLoadsAutostretchedPreviewForMarking)
{
    ensureQApplication();
    const ScopedSessionContext sessionContext;
    ASSERT_TRUE(sessionContext.valid());

    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    const QString frame_path =
        writeCometPreviewFrame(sessionContext.session(), temp_dir.path(), QStringLiteral("light_preview.fit"));

    an::ui::CometTrackingDialog dialog({frame_path});
    dialog.resize(640, 420);
    dialog.show();
    QApplication::processEvents();

    auto* frameTable = dialog.findChild<QTableWidget*>(QStringLiteral("cometFrameTable"));
    auto* preview = findCometPreviewHost(dialog);
    auto* imageView = findCometImageViewControl(dialog);
    auto* fallbackPreview = findCometFallbackPreview(dialog);
    auto* markButton = dialog.findChild<QPushButton*>(QStringLiteral("markCometButton"));
    ASSERT_NE(frameTable, nullptr);
    ASSERT_NE(preview, nullptr);
    ASSERT_NE(imageView, nullptr);
    ASSERT_NE(fallbackPreview, nullptr);
    ASSERT_NE(markButton, nullptr);

    frameTable->selectRow(0);
    QTRY_VERIFY_WITH_TIMEOUT(preview->property("anPreviewLoaded").toBool(), 5000);
    EXPECT_EQ(preview->property("anPreviewImageWidth").toInt(), 80);
    EXPECT_EQ(preview->property("anPreviewImageHeight").toInt(), 40);
    EXPECT_TRUE(imageView->hasImage());
    EXPECT_TRUE(imageView->isVisible());
    EXPECT_FALSE(fallbackPreview->isVisible());

    auto* viewWidget = imageView->getViewWidget();
    ASSERT_NE(viewWidget, nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(viewWidget->width() > 0 && viewWidget->height() > 0, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(viewWidget->presentedImage() != nullptr, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(viewWidget->getCurrentScaleFactor() > 0.0, 1000);

    const double scale = viewWidget->getCurrentScaleFactor();
    QTest::mouseClick(viewWidget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(static_cast<int>(40.0 * scale), static_cast<int>(20.0 * scale)));
    EXPECT_TRUE(markButton->isEnabled());
    QTest::mouseClick(markButton, Qt::LeftButton);

    const RegistrationSettings::CometTrackingSettings settings = dialog.cometTrackingSettings();
    ASSERT_EQ(settings.observations.size(), 1);
    EXPECT_EQ(settings.observations.front().frameId, frame_path);
    EXPECT_NEAR(settings.observations.front().sourcePosition.x(), 40.0, 0.75);
    EXPECT_NEAR(settings.observations.front().sourcePosition.y(), 20.0, 0.75);
}

TEST(CometTrackingDialog, PreviewClickShowsMovablePendingMarkerBeforeMark)
{
    ensureQApplication();
    const ScopedSessionContext sessionContext;
    ASSERT_TRUE(sessionContext.valid());

    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    const QString frame_path =
        writeCometPreviewFrame(sessionContext.session(), temp_dir.path(), QStringLiteral("light_preview.fit"));

    an::ui::CometTrackingDialog dialog({frame_path});
    dialog.resize(640, 420);
    dialog.show();
    QApplication::processEvents();

    auto* preview = findCometPreviewHost(dialog);
    auto* imageView = findCometImageViewControl(dialog);
    auto* markerOverlay = findCometPendingPointOverlay(dialog);
    auto* markButton = dialog.findChild<QPushButton*>(QStringLiteral("markCometButton"));
    auto* clearPointButton = dialog.findChild<QPushButton*>(QStringLiteral("clearCometPointButton"));
    auto* statusLabel = dialog.findChild<QLabel*>(QStringLiteral("cometTrackingStatusLabel"));
    ASSERT_NE(preview, nullptr);
    ASSERT_NE(imageView, nullptr);
    ASSERT_NE(markerOverlay, nullptr);
    ASSERT_NE(markButton, nullptr);
    ASSERT_NE(clearPointButton, nullptr);
    ASSERT_NE(statusLabel, nullptr);

    QTRY_VERIFY_WITH_TIMEOUT(preview->property("anPreviewLoaded").toBool(), 5000);
    auto* viewWidget = imageView->getViewWidget();
    ASSERT_NE(viewWidget, nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(viewWidget->getCurrentScaleFactor() > 0.0, 1000);

    const double scale = viewWidget->getCurrentScaleFactor();
    QTest::mouseClick(viewWidget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(static_cast<int>(40.0 * scale), static_cast<int>(20.0 * scale)));
    EXPECT_TRUE(markerOverlay->isVisible());
    EXPECT_TRUE(markerOverlay->property("anMarkerVisible").toBool());
    EXPECT_NEAR(markerOverlay->property("anMarkerSourceX").toDouble(), 40.0, 0.75);
    EXPECT_NEAR(markerOverlay->property("anMarkerSourceY").toDouble(), 20.0, 0.75);
    EXPECT_TRUE(markButton->isEnabled());
    EXPECT_TRUE(clearPointButton->isEnabled());
    // A click lands on a whole widget pixel, so the pending point is where that pixel maps to: within a source pixel of
    // the point aimed at, and on it only at a zoom that maps it to a whole widget pixel. The status names the pending
    // point the marker shows.
    const QString pending = QStringLiteral("X %1, Y %2")
                                .arg(markerOverlay->property("anMarkerSourceX").toDouble(), 0, 'f', 1)
                                .arg(markerOverlay->property("anMarkerSourceY").toDouble(), 0, 'f', 1);
    EXPECT_TRUE(statusLabel->text().contains(pending)) << statusLabel->text().toStdString();

    QTest::mouseClick(viewWidget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(static_cast<int>(12.0 * scale), static_cast<int>(8.0 * scale)));
    EXPECT_NEAR(markerOverlay->property("anMarkerSourceX").toDouble(), 12.0, 0.75);
    EXPECT_NEAR(markerOverlay->property("anMarkerSourceY").toDouble(), 8.0, 0.75);

    QTest::mouseClick(clearPointButton, Qt::LeftButton);
    EXPECT_FALSE(markerOverlay->isVisible());
    EXPECT_FALSE(markerOverlay->property("anMarkerVisible").toBool());
    EXPECT_FALSE(markButton->isEnabled());
    EXPECT_FALSE(clearPointButton->isEnabled());
}

TEST(CometTrackingDialog, MouseSelectingAnotherFrameLoadsThatPreview)
{
    ensureQApplication();
    const ScopedSessionContext sessionContext;
    ASSERT_TRUE(sessionContext.valid());

    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    const QString first_frame = writeCometPreviewFrame(sessionContext.session(), temp_dir.path(),
                                                       QStringLiteral("light_preview_a.fit"), QSize(80, 40));
    const QString second_frame = writeCometPreviewFrame(sessionContext.session(), temp_dir.path(),
                                                        QStringLiteral("light_preview_b.fit"), QSize(96, 48));

    an::ui::CometTrackingDialog dialog({first_frame, second_frame});
    dialog.resize(640, 420);
    dialog.show();
    QApplication::processEvents();

    auto* frameTable = dialog.findChild<QTableWidget*>(QStringLiteral("cometFrameTable"));
    auto* preview = findCometPreviewHost(dialog);
    ASSERT_NE(frameTable, nullptr);
    ASSERT_NE(preview, nullptr);

    QTRY_COMPARE_WITH_TIMEOUT(preview->property("anPreviewImageWidth").toInt(), 80, 5000);
    ASSERT_NE(frameTable->item(1, 0), nullptr);

    const QRect target_rect = frameTable->visualItemRect(frameTable->item(1, 0));
    QTest::mouseClick(frameTable->viewport(), Qt::LeftButton, Qt::NoModifier, target_rect.center());

    QTRY_COMPARE_WITH_TIMEOUT(preview->property("anPreviewImageWidth").toInt(), 96, 5000);
    EXPECT_EQ(preview->property("anPreviewImageHeight").toInt(), 48);
}

TEST(CometTrackingDialog, ChangingSelectedFramePreservesPreviewZoomAndScroll)
{
    ensureQApplication();
    const ScopedSessionContext sessionContext;
    ASSERT_TRUE(sessionContext.valid());

    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    const QString first_frame = writeCometPreviewFrame(sessionContext.session(), temp_dir.path(),
                                                       QStringLiteral("light_preview_a.fit"), QSize(1024, 768));
    const QString second_frame = writeCometPreviewFrame(sessionContext.session(), temp_dir.path(),
                                                        QStringLiteral("light_preview_b.fit"), QSize(1000, 750));

    an::ui::CometTrackingDialog dialog({first_frame, second_frame});
    dialog.resize(760, 520);
    dialog.show();
    QApplication::processEvents();

    auto* frameTable = dialog.findChild<QTableWidget*>(QStringLiteral("cometFrameTable"));
    auto* preview = findCometPreviewHost(dialog);
    auto* imageView = findCometImageViewControl(dialog);
    ASSERT_NE(frameTable, nullptr);
    ASSERT_NE(preview, nullptr);
    ASSERT_NE(imageView, nullptr);

    QTRY_COMPARE_WITH_TIMEOUT(preview->property("anPreviewImageWidth").toInt(), 1024, 5000);
    auto* viewWidget = imageView->getViewWidget();
    ASSERT_NE(viewWidget, nullptr);

    constexpr double target_scale = 1.5;
    const QPoint target_offset(120, 90);
    viewWidget->scaleImageView(target_scale, true);
    QApplication::processEvents();
    imageView->setViewportOffset(target_offset);
    QTRY_COMPARE_WITH_TIMEOUT(imageView->getViewportOffset(), target_offset, 1000);
    ASSERT_NE(frameTable->item(1, 0), nullptr);

    frameTable->selectRow(1);

    QTRY_COMPARE_WITH_TIMEOUT(preview->property("anPreviewImageWidth").toInt(), 1000, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(imageView->getCurrentZoom() - target_scale * 100.0) < 0.01, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(imageView->getViewportOffset(), target_offset, 1000);
}

TEST(CometTrackingDialog, ExistingInvalidFrameReportsPreviewLoadFailure)
{
    ensureQApplication();
    const ScopedSessionContext sessionContext;
    ASSERT_TRUE(sessionContext.valid());

    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    const QString frame_path = QDir(temp_dir.path()).filePath(QStringLiteral("invalid_preview.fit"));
    QFile file(frame_path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_GT(file.write("not an image"), 0);
    file.close();

    an::ui::CometTrackingDialog dialog({frame_path});
    dialog.resize(640, 420);
    dialog.show();
    QApplication::processEvents();

    auto* preview = findCometPreviewHost(dialog);
    auto* fallbackPreview = findCometFallbackPreview(dialog);
    ASSERT_NE(preview, nullptr);
    ASSERT_NE(fallbackPreview, nullptr);

    QTRY_VERIFY_WITH_TIMEOUT(!preview->property("anPreviewErrorMessage").toString().trimmed().isEmpty(), 5000);
    EXPECT_FALSE(preview->property("anPreviewLoaded").toBool());
    EXPECT_FALSE(preview->property("anPreviewLoading").toBool());
    EXPECT_TRUE(fallbackPreview->isVisible());
    EXPECT_FALSE(fallbackPreview->text().trimmed().isEmpty());
}

TEST(CometTrackingDialog, LoadedPreviewDoesNotGrowDialogFromPixmapSizeHint)
{
    ensureQApplication();
    const ScopedSessionContext sessionContext;
    ASSERT_TRUE(sessionContext.valid());

    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    const QString frame_path =
        writeCometPreviewFrame(sessionContext.session(), temp_dir.path(), QStringLiteral("light_preview.fit"));

    an::ui::CometTrackingDialog dialog({frame_path});
    dialog.resize(640, 420);
    dialog.show();
    QApplication::processEvents();

    auto* preview = findCometPreviewHost(dialog);
    ASSERT_NE(preview, nullptr);

    const QSize requested_size = dialog.size();
    QTRY_VERIFY_WITH_TIMEOUT(preview->property("anPreviewLoaded").toBool(), 5000);

    for (int i = 0; i < 12; ++i)
    {
        QTest::qWait(10);
        QApplication::processEvents();
    }

    EXPECT_LE(dialog.width(), requested_size.width() + 8);
    EXPECT_LE(dialog.height(), requested_size.height() + 8);
}

TEST(CometTrackingDialog, PreviewSizeHintDoesNotDependOnLoadedImageDimensions)
{
    ensureQApplication();

    an::ui::CometTrackingPreviewPane preview;
    preview.resize(640, 420);
    const QSize placeholder_hint = preview.sizeHint();

    QImage image(1600, 900, QImage::Format_RGB888);
    image.fill(Qt::white);
    preview.setPreviewImage(image, image.size());

    EXPECT_LE(preview.sizeHint().width(), placeholder_hint.width() + 8);
    EXPECT_LE(preview.sizeHint().height(), placeholder_hint.height() + 8);
}

TEST(CometTrackingDialog, AcceptCandidateAddsAcceptedObservationWithoutRemovingDiagnostics)
{
    ensureQApplication();

    an::ui::CometTrackingDialog dialog({QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")});
    an::ui::CometTrackingDialogDiagnostic diagnostic;
    diagnostic.frameId = QStringLiteral("C:/light_b.fit");
    diagnostic.sourcePosition = QPointF(31.5, 27.25);
    diagnostic.confidence = 0.91;
    diagnostic.state = QStringLiteral("Candidate");
    diagnostic.message = QStringLiteral("High-confidence candidate");
    dialog.setDiagnostics({diagnostic});

    auto* diagnosticsTable = dialog.findChild<QTableWidget*>(QStringLiteral("cometDiagnosticsTable"));
    auto* acceptButton = dialog.findChild<QPushButton*>(QStringLiteral("acceptCometTrackingButton"));
    ASSERT_NE(diagnosticsTable, nullptr);
    ASSERT_NE(acceptButton, nullptr);

    diagnosticsTable->selectRow(0);
    QTest::mouseClick(acceptButton, Qt::LeftButton);

    const RegistrationSettings::CometTrackingSettings settings = dialog.cometTrackingSettings();
    ASSERT_EQ(settings.observations.size(), 1);
    EXPECT_EQ(settings.observations.front().frameId, QStringLiteral("C:/light_b.fit"));
    EXPECT_EQ(settings.observations.front().sourcePosition, QPointF(31.5, 27.25));
    EXPECT_EQ(settings.observations.front().origin, RegistrationSettings::CometObservationOrigin::AcceptedAutomatic);
    EXPECT_TRUE(settings.observations.front().note.isEmpty());
    EXPECT_EQ(diagnosticsTable->rowCount(), 1);
}

TEST(CometTrackingDialog, ClearButtonKeepsDiagnosticsVisibleWhileClearingObservations)
{
    ensureQApplication();

    an::ui::CometTrackingDialog dialog({QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")});
    RegistrationSettings::CometTrackingSettings settings;
    settings.observations = {
        RegistrationSettings::CometTrackingObservation{QStringLiteral("C:/light_a.fit"), QPointF(10.0, 10.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true}};
    dialog.setCometTrackingSettings(settings);

    an::ui::CometTrackingDialogDiagnostic diagnostic;
    diagnostic.frameId = QStringLiteral("C:/light_b.fit");
    diagnostic.sourcePosition = QPointF(20.0, 15.0);
    diagnostic.state = QStringLiteral("Candidate");
    dialog.setDiagnostics({diagnostic});

    auto* clearButton = dialog.findChild<QPushButton*>(QStringLiteral("clearCometButton"));
    auto* diagnosticsTable = dialog.findChild<QTableWidget*>(QStringLiteral("cometDiagnosticsTable"));
    ASSERT_NE(clearButton, nullptr);
    ASSERT_NE(diagnosticsTable, nullptr);

    QTest::mouseClick(clearButton, Qt::LeftButton);

    EXPECT_TRUE(dialog.cometTrackingSettings().observations.empty());
    EXPECT_EQ(diagnosticsTable->rowCount(), 1);
}

TEST(CometTrackingPreviewController, BuildsDebayeredAutostretchedPreviewForCfaImage)
{
    const std::unique_ptr<an::tests::viewer::ViewerTestContext> context = an::tests::viewer::openViewerContext();
    ASSERT_NE(context, nullptr);
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    const QString frame_path = writeCfaPreviewFrame(context->engineSession(), temp_dir.path());

    const an::ui::CometTrackingFramePreviewResult result = an::ui::loadCometTrackingFramePreviewFromFile(
        context->engineSession(), frame_path, an::DemosaicMethod::Fast, QSize(64, 64));

    ASSERT_TRUE(result.succeeded) << result.message.toStdString();
    EXPECT_EQ(result.sourceSize, QSize(16, 16));
    ASSERT_FALSE(result.image.isNull());
    EXPECT_EQ(result.image.format(), QImage::Format_RGB888);
}

TEST(CometTrackingPreviewController, BuildsPreviewFromAssetScaledDownToTheSizeAskedFor)
{
    const std::unique_ptr<an::tests::viewer::ViewerTestContext> context = an::tests::viewer::openViewerContext();
    ASSERT_NE(context, nullptr);
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        context->engineSession(), 128u, 64u, an::tests::viewer::Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<float>((x + y) % 64u) / 64.0f; });
    ASSERT_TRUE(asset.has_value());

    const an::ui::CometTrackingFramePreviewResult result =
        an::ui::buildCometTrackingFramePreviewFromAsset(context->engineSession(), *asset, QSize(64, 64));

    ASSERT_TRUE(result.succeeded) << result.message.toStdString();
    EXPECT_EQ(result.sourceSize, QSize(128, 64));
    ASSERT_FALSE(result.image.isNull());
    EXPECT_LE(result.image.width(), 64);
    EXPECT_LE(result.image.height(), 64);
}

TEST(CometTrackingPreviewController, MissingFrameFileReportsWhyThereIsNoPreview)
{
    const std::unique_ptr<an::tests::viewer::ViewerTestContext> context = an::tests::viewer::openViewerContext();
    ASSERT_NE(context, nullptr);

    const an::ui::CometTrackingFramePreviewResult result = an::ui::loadCometTrackingFramePreviewFromFile(
        context->engineSession(), QStringLiteral("C:/does/not/exist/light.fit"), an::DemosaicMethod::Fast);

    EXPECT_FALSE(result.succeeded);
    EXPECT_TRUE(result.image.isNull());
    EXPECT_FALSE(result.message.trimmed().isEmpty());
}

TEST(CometTrackingPreviewController, MapsWidgetClickToSourcePixelWithScaledImageOrigin)
{
    const an::ui::CometTrackingPreviewGeometry geometry{
        .sourceSize = QSize(200, 100), .scale = 2.0, .imageTopLeft = QPointF(12.0, 8.0)};

    const std::optional<QPointF> source_point =
        an::ui::mapCometPreviewWidgetPointToSourcePixel(QPointF(52.5, 28.0), geometry);

    ASSERT_TRUE(source_point.has_value());
    EXPECT_EQ(source_point.value(), QPointF(20.25, 10.0));
}

TEST(CometTrackingPreviewController, RejectsOutsideSourceBoundsAndInvalidScale)
{
    const an::ui::CometTrackingPreviewGeometry geometry{
        .sourceSize = QSize(200, 100), .scale = 2.0, .imageTopLeft = QPointF(12.0, 8.0)};

    EXPECT_FALSE(an::ui::mapCometPreviewWidgetPointToSourcePixel(QPointF(11.0, 28.0), geometry).has_value());
    EXPECT_FALSE(an::ui::mapCometPreviewWidgetPointToSourcePixel(QPointF(412.0, 28.0), geometry).has_value());

    an::ui::CometTrackingPreviewGeometry invalid_scale = geometry;
    invalid_scale.scale = 0.0;
    EXPECT_FALSE(an::ui::mapCometPreviewWidgetPointToSourcePixel(QPointF(52.5, 28.0), invalid_scale).has_value());
}

TEST(CometTrackingPreviewController, MapsSourcePixelToWidgetOverlayPoint)
{
    const an::ui::CometTrackingPreviewGeometry geometry{
        .sourceSize = QSize(200, 100), .scale = 1.5, .imageTopLeft = QPointF(4.0, 6.0)};

    const std::optional<QPointF> widget_point =
        an::ui::mapCometSourcePixelToPreviewWidgetPoint(QPointF(10.0, 12.5), geometry);

    ASSERT_TRUE(widget_point.has_value());
    EXPECT_EQ(widget_point.value(), QPointF(19.0, 24.75));
    EXPECT_FALSE(an::ui::mapCometSourcePixelToPreviewWidgetPoint(QPointF(-1.0, 12.5), geometry).has_value());
}

TEST(RegistrationSettingsDialog, CometTrackingPanelUsesTrackDialogInsteadOfInlineObservationFields)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/sessions/comet/light_a.fit"),
                                                                  QStringLiteral("C:/sessions/comet/light_b.fit")});
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("comet_tracking");
    settings.cometTracking = RegistrationSettings::CometTrackingSettings{};
    settings.cometTracking->observations = {RegistrationSettings::CometTrackingObservation{
                                                QStringLiteral("C:/sessions/comet/light_a.fit"), QPointF(10.0, 10.0),
                                                RegistrationSettings::CometObservationOrigin::Manual, true},
                                            RegistrationSettings::CometTrackingObservation{
                                                QStringLiteral("C:/sessions/comet/light_b.fit"), QPointF(20.0, 12.0),
                                                RegistrationSettings::CometObservationOrigin::AcceptedAutomatic, true}};
    dialog.setRegistrationSettings(settings);
    dialog.show();
    QApplication::processEvents();

    auto* trackButton = dialog.findChild<QPushButton*>(QStringLiteral("trackCometButton"));
    auto* summaryLabel = dialog.findChild<QLabel*>(QStringLiteral("cometTrackingSummaryLabel"));
    ASSERT_NE(trackButton, nullptr);
    ASSERT_NE(summaryLabel, nullptr);
    ASSERT_TRUE(trackButton->isVisible());
    ASSERT_TRUE(summaryLabel->isVisible());

    QWidget* cometPanel = trackButton->parentWidget();
    ASSERT_NE(cometPanel, nullptr);

    const auto labels = cometPanel->findChildren<QLabel*>();
    EXPECT_NE(cometPanel->findChild<QComboBox*>(QStringLiteral("cometOutputModeSelector")), nullptr);
    EXPECT_TRUE(cometPanel->findChildren<QDoubleSpinBox*>().empty());

    EXPECT_TRUE(std::none_of(labels.cbegin(), labels.cend(), [](const QLabel* label) {
        return label != nullptr && (label->text().contains(QStringLiteral("Start Frame"), Qt::CaseInsensitive) ||
                                    label->text().contains(QStringLiteral("End Frame"), Qt::CaseInsensitive) ||
                                    label->text().contains(QStringLiteral("Start Position"), Qt::CaseInsensitive) ||
                                    label->text().contains(QStringLiteral("End Position"), Qt::CaseInsensitive));
    }));
    EXPECT_TRUE(summaryLabel->text().contains(QStringLiteral("2 observation")));
    EXPECT_FALSE(summaryLabel->text().contains(QStringLiteral("C:/sessions/comet")));
    EXPECT_FALSE(summaryLabel->text().contains(QStringLiteral("Guided auto-track")));
    EXPECT_TRUE(summaryLabel->toolTip().contains(QStringLiteral("Guided auto-track")));
}

// The comet panel says which tracking and fit options the group's registration runs with, which the Track Comet
// dialog sets, and the dialog keeps them.
TEST(RegistrationSettingsDialog, TheCometSummaryStatesTheTrackingAndFitOptionsTheRegistrationRunsWith)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/sessions/comet/light_a.fit"),
                                                                  QStringLiteral("C:/sessions/comet/light_b.fit")});
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("comet_tracking");
    settings.cometTracking = RegistrationSettings::CometTrackingSettings{};
    settings.cometTracking->trackingOptions.searchRadiusPixels = 48.0;
    settings.cometTracking->trackingOptions.minimumSnr = 4.5;
    settings.cometTracking->trackingOptions.maxBootstrapFrames = 12;
    settings.cometTracking->fitOptions.model = QStringLiteral("quadratic");
    settings.cometTracking->fitOptions.robustRejection = false;
    dialog.setRegistrationSettings(settings);

    auto* summaryLabel = dialog.findChild<QLabel*>(QStringLiteral("cometTrackingSummaryLabel"));
    ASSERT_NE(summaryLabel, nullptr);
    const QString summary = summaryLabel->text();
    EXPECT_TRUE(summary.contains(QStringLiteral("search radius 48 px"))) << summary.toStdString();
    EXPECT_TRUE(summary.contains(QStringLiteral("minimum SNR 4.5"))) << summary.toStdString();
    EXPECT_TRUE(summary.contains(QStringLiteral("12 bootstrap frames"))) << summary.toStdString();
    EXPECT_TRUE(summary.contains(QStringLiteral("quadratic preferred"))) << summary.toStdString();
    EXPECT_TRUE(summary.contains(QStringLiteral("no outlier rejection"))) << summary.toStdString();

    settings.cometTracking->fitOptions = RegistrationSettings::CometFitOptions{};
    dialog.setRegistrationSettings(settings);
    EXPECT_TRUE(summaryLabel->text().contains(QStringLiteral("linear track, outliers beyond 3 sigma rejected")))
        << summaryLabel->text().toStdString();

    const RegistrationSettings kept = dialog.getRegistrationSettings();
    ASSERT_TRUE(kept.cometTracking.has_value());
    EXPECT_TRUE(kept.cometTracking->trackingOptions == settings.cometTracking->trackingOptions);
    EXPECT_TRUE(kept.cometTracking->fitOptions == settings.cometTracking->fitOptions);
}

TEST(RegistrationSettingsDialog, CometOutputModeHidesExperimentalStarsPlusCometAndFallsBackToCometOnly)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/sessions/comet/light_a.fit"),
                                                                  QStringLiteral("C:/sessions/comet/light_b.fit")});
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("comet_tracking");
    settings.cometTracking = RegistrationSettings::CometTrackingSettings{};
    settings.cometTracking->outputMode = ImageIntegration::starsPlusCometExperimentalStackingModeToken();
    dialog.setRegistrationSettings(settings);
    dialog.show();
    QApplication::processEvents();

    auto* outputSelector = dialog.findChild<QComboBox*>(QStringLiteral("cometOutputModeSelector"));
    ASSERT_NE(outputSelector, nullptr);
    ASSERT_TRUE(outputSelector->isVisible());
    EXPECT_EQ(outputSelector->findData(ImageIntegration::starsPlusCometExperimentalStackingModeToken()), -1);
    EXPECT_EQ(outputSelector->count(), 1);
    EXPECT_EQ(outputSelector->currentData().toString(), QStringLiteral("Comet"));
    EXPECT_FALSE(outputSelector->itemText(0).contains(QStringLiteral("Stars"), Qt::CaseInsensitive));

    const RegistrationSettings updated = dialog.getRegistrationSettings();
    ASSERT_TRUE(updated.cometTracking.has_value());
    EXPECT_EQ(updated.cometTracking->outputMode, QStringLiteral("Comet"));
}

TEST(RegistrationSettingsDialog, CometTrackingModeHidesIgnoredRowsAndAllowsReducedHeight)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/sessions/comet/light_a.fit"),
                                                                  QStringLiteral("C:/sessions/comet/light_b.fit")});
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("comet_tracking");
    settings.cometTracking = RegistrationSettings::CometTrackingSettings{};
    dialog.setRegistrationSettings(settings);
    dialog.show();
    QApplication::processEvents();

    ASSERT_NE(dialog.m_ui.label_minStarPairs, nullptr);
    ASSERT_NE(dialog.m_ui.minStarPairsSpinBox, nullptr);
    ASSERT_NE(dialog.m_ui.label_maxStarPairs, nullptr);
    ASSERT_NE(dialog.m_ui.maxStarPairsSpinBox, nullptr);
    ASSERT_NE(dialog.m_ui.label_transformation, nullptr);
    ASSERT_NE(dialog.m_ui.transformationComboBox, nullptr);
    ASSERT_NE(dialog.m_ui.transformSummaryLabel, nullptr);

    EXPECT_FALSE(dialog.m_ui.label_minStarPairs->isVisible());
    EXPECT_FALSE(dialog.m_ui.minStarPairsSpinBox->isVisible());
    EXPECT_FALSE(dialog.m_ui.label_maxStarPairs->isVisible());
    EXPECT_FALSE(dialog.m_ui.maxStarPairsSpinBox->isVisible());
    EXPECT_FALSE(dialog.m_ui.label_transformation->isVisible());
    EXPECT_FALSE(dialog.m_ui.transformationComboBox->isVisible());
    EXPECT_FALSE(dialog.m_ui.transformSummaryLabel->isVisible());

    const int expanded_height = dialog.height();
    const int target_height = (std::max)(260, expanded_height - 180);
    ASSERT_LT(target_height, expanded_height);

    dialog.resize(dialog.width(), target_height);
    QApplication::processEvents();

    EXPECT_LT(dialog.height(), expanded_height);
    EXPECT_LE(dialog.height(), target_height + 8);
}

TEST(RegistrationSettingsDialog, ScrollContentKeepsFullWidthWhenDialogIsNarrowed)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/sessions/comet/light_a.fit"),
                                                                  QStringLiteral("C:/sessions/comet/light_b.fit")});
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("comet_tracking");
    settings.cometTracking = RegistrationSettings::CometTrackingSettings{};
    dialog.setRegistrationSettings(settings);
    dialog.show();
    QApplication::processEvents();

    auto* scrollArea = dialog.findChild<QScrollArea*>(QStringLiteral("registrationSettingsScrollArea"));
    auto* registrationSection = dialog.findChild<QWidget*>(QStringLiteral("registrationGroupBox"));
    auto* alignmentSection = dialog.findChild<QWidget*>(QStringLiteral("alignmentGroupBox"));
    auto* advancedButton = dialog.findChild<QPushButton*>(QStringLiteral("advancedSettingsButton"));
    ASSERT_NE(scrollArea, nullptr);
    ASSERT_NE(scrollArea->viewport(), nullptr);
    ASSERT_NE(registrationSection, nullptr);
    ASSERT_NE(alignmentSection, nullptr);
    ASSERT_NE(advancedButton, nullptr);

    dialog.resize(420, 260);
    QApplication::processEvents();

    EXPECT_GE(dialog.width(), 620);
    ASSERT_TRUE(scrollArea->verticalScrollBar()->isVisible());
    EXPECT_GE(scrollArea->widget()->width(), scrollArea->viewport()->width());

    int required_content_width = 0;
    for (QWidget* widget : {registrationSection, alignmentSection, static_cast<QWidget*>(advancedButton)})
    {
        required_content_width = (std::max)(required_content_width,
                                            (std::max)(widget->minimumSizeHint().width(), widget->sizeHint().width()));
    }

    const QMargins dialog_margins = dialog.layout() ? dialog.layout()->contentsMargins() : QMargins{};
    const int required_dialog_width = dialog_margins.left() + dialog_margins.right() + scrollArea->frameWidth() * 2 +
                                      scrollArea->verticalScrollBar()->sizeHint().width() + required_content_width;
    EXPECT_GE(dialog.minimumWidth(), required_dialog_width);

    const QRect viewport_rect = scrollArea->viewport()->rect();
    for (QWidget* widget : {registrationSection, alignmentSection, static_cast<QWidget*>(advancedButton)})
    {
        const QRect widget_rect(widget->mapTo(scrollArea->viewport(), QPoint(0, 0)), widget->size());
        EXPECT_GE(widget_rect.left(), viewport_rect.left());
        EXPECT_LE(widget_rect.right(), viewport_rect.right());
        EXPECT_GE(viewport_rect.right() - widget_rect.right(), an::ui::tokens().spacing.xs);
    }
}

TEST(RegistrationSettingsDialog, TrackCometButtonOpensCometTrackingDialog)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(
        QStringLiteral("Group 1"),
        {QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit"), QStringLiteral("C:/light_c.fit")});
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("comet_tracking");
    settings.cometTracking = RegistrationSettings::CometTrackingSettings{};
    dialog.setRegistrationSettings(settings);
    dialog.show();
    QApplication::processEvents();

    auto* button = dialog.findChild<QPushButton*>(QStringLiteral("trackCometButton"));
    ASSERT_NE(button, nullptr);
    ASSERT_TRUE(button->isVisible());

    QTest::mouseClick(button, Qt::LeftButton);
    QApplication::processEvents();

    auto* cometDialog = dialog.findChild<QDialog*>(QStringLiteral("cometTrackingDialog"));
    ASSERT_NE(cometDialog, nullptr);
    EXPECT_TRUE(cometDialog->isVisible());
    EXPECT_EQ(cometDialog->windowModality(), Qt::WindowModal);
}

TEST(RegistrationSettingsDialog, TrackCometButtonLoadsExistingFramePreview)
{
    ensureQApplication();
    const ScopedSessionContext sessionContext;
    ASSERT_TRUE(sessionContext.valid());

    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    const QString first_frame = writeCometPreviewFrame(sessionContext.session(), temp_dir.path(),
                                                       QStringLiteral("light_preview_a.fit"), QSize(80, 40));
    const QString second_frame = writeCometPreviewFrame(sessionContext.session(), temp_dir.path(),
                                                        QStringLiteral("light_preview_b.fit"), QSize(96, 48));

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {first_frame, second_frame});
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("comet_tracking");
    settings.cometTracking = RegistrationSettings::CometTrackingSettings{};
    dialog.setRegistrationSettings(settings);
    dialog.show();
    QApplication::processEvents();

    auto* button = dialog.findChild<QPushButton*>(QStringLiteral("trackCometButton"));
    ASSERT_NE(button, nullptr);
    QTest::mouseClick(button, Qt::LeftButton);
    QApplication::processEvents();

    auto* cometDialog = dialog.findChild<QDialog*>(QStringLiteral("cometTrackingDialog"));
    ASSERT_NE(cometDialog, nullptr);
    auto* preview = findCometPreviewHost(*cometDialog);
    auto* imageView = findCometImageViewControl(*cometDialog);
    auto* fallbackPreview = findCometFallbackPreview(*cometDialog);
    ASSERT_NE(preview, nullptr);
    ASSERT_NE(imageView, nullptr);
    ASSERT_NE(fallbackPreview, nullptr);

    QTRY_VERIFY_WITH_TIMEOUT(preview->property("anPreviewLoaded").toBool(), 5000);
    EXPECT_EQ(preview->property("anPreviewImageWidth").toInt(), 80);
    EXPECT_EQ(preview->property("anPreviewImageHeight").toInt(), 40);
    EXPECT_TRUE(imageView->isVisible());
    EXPECT_FALSE(fallbackPreview->isVisible());
}

TEST(RegistrationSettingsDialog, TrackCometButtonInstallsAutoTrackHandler)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(
        QStringLiteral("Group 1"),
        {QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit"), QStringLiteral("C:/light_c.fit")});
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("comet_tracking");
    settings.cometTracking = RegistrationSettings::CometTrackingSettings{};
    settings.cometTracking->observations = {
        RegistrationSettings::CometTrackingObservation{QStringLiteral("C:/light_a.fit"), QPointF(10.0, 10.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true}};
    dialog.setRegistrationSettings(settings);

    an::ui::CometTrackingAutoTrackCompletion completion;
    dialog.setCometAutoTrackHandler([&](const RegistrationSettings::CometTrackingSettings& request_settings,
                                        an::ui::CometTrackingAutoTrackCompletion done) {
        EXPECT_EQ(request_settings.observations.size(), 1);
        EXPECT_EQ(request_settings.observations.front().frameId, QStringLiteral("C:/light_a.fit"));
        completion = std::move(done);
    });
    dialog.show();
    QApplication::processEvents();

    auto* trackButton = dialog.findChild<QPushButton*>(QStringLiteral("trackCometButton"));
    ASSERT_NE(trackButton, nullptr);
    QTest::mouseClick(trackButton, Qt::LeftButton);
    QApplication::processEvents();

    auto* cometDialog = dialog.findChild<QDialog*>(QStringLiteral("cometTrackingDialog"));
    ASSERT_NE(cometDialog, nullptr);
    auto* autoTrackButton = cometDialog->findChild<QPushButton*>(QStringLiteral("autoTrackCometButton"));
    auto* diagnosticsTable = cometDialog->findChild<QTableWidget*>(QStringLiteral("cometDiagnosticsTable"));
    ASSERT_NE(autoTrackButton, nullptr);
    ASSERT_NE(diagnosticsTable, nullptr);
    EXPECT_TRUE(autoTrackButton->isEnabled());

    QTest::mouseClick(autoTrackButton, Qt::LeftButton);
    ASSERT_TRUE(static_cast<bool>(completion));

    an::ui::CometTrackingDialogDiagnostic diagnostic;
    diagnostic.frameId = QStringLiteral("C:/light_b.fit");
    diagnostic.sourcePosition = QPointF(21.5, 19.25);
    diagnostic.confidence = 0.88;
    diagnostic.state = QStringLiteral("Candidate");
    diagnostic.message = QStringLiteral("Automatic bootstrap candidate");
    completion({diagnostic}, QStringLiteral("Auto-tracking completed."));
    EXPECT_EQ(diagnosticsTable->rowCount(), 1);
}

TEST(RegistrationSettingsDialog, TrackCometButtonInstallsPreviewSizeProvider)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"),
                                      {QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")});
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("comet_tracking");
    settings.cometTracking = RegistrationSettings::CometTrackingSettings{};
    dialog.setRegistrationSettings(settings);
    dialog.setCometFramePreviewSizeProvider([](const QString& frame_id) -> std::optional<QSize> {
        if (frame_id == QStringLiteral("C:/light_b.fit"))
        {
            return QSize(120, 60);
        }
        return std::nullopt;
    });
    dialog.show();
    QApplication::processEvents();

    auto* trackButton = dialog.findChild<QPushButton*>(QStringLiteral("trackCometButton"));
    ASSERT_NE(trackButton, nullptr);
    QTest::mouseClick(trackButton, Qt::LeftButton);
    QApplication::processEvents();

    auto* cometDialogBase = dialog.findChild<QDialog*>(QStringLiteral("cometTrackingDialog"));
    ASSERT_NE(cometDialogBase, nullptr);
    auto* cometDialog = static_cast<an::ui::CometTrackingDialog*>(cometDialogBase);
    auto* frameTable = cometDialog->findChild<QTableWidget*>(QStringLiteral("cometFrameTable"));
    auto* preview = findCometFallbackPreview(*cometDialog);
    auto* markButton = cometDialog->findChild<QPushButton*>(QStringLiteral("markCometButton"));
    ASSERT_NE(frameTable, nullptr);
    ASSERT_NE(preview, nullptr);
    ASSERT_NE(markButton, nullptr);

    frameTable->selectRow(1);
    QApplication::processEvents();
    QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, QPoint(preview->width() / 2, preview->height() / 2));
    EXPECT_TRUE(markButton->isEnabled());
    QTest::mouseClick(markButton, Qt::LeftButton);

    const RegistrationSettings::CometTrackingSettings updated = cometDialog->cometTrackingSettings();
    ASSERT_EQ(updated.observations.size(), 1);
    EXPECT_EQ(updated.observations.front().frameId, QStringLiteral("C:/light_b.fit"));
    EXPECT_NEAR(updated.observations.front().sourcePosition.x(), 60.0, 0.75);
    EXPECT_NEAR(updated.observations.front().sourcePosition.y(), 30.0, 0.75);
}

// The detector refuses a search radius above thirty-two and a roundness threshold or a Moffat exponent that is not
// positive, which the controls offered: the radius up to fifty, the threshold and the exponent from zero. They offer
// what the detector takes, as the Star Profiler's do, and show a stored value it refuses at the value the run takes.
TEST(RegistrationSettingsDialog, TheStarDetectionControlsOfferOnlyWhatTheDetectorTakes)
{
    ensureQApplication();
    using ImageIntegration::RegistrationSettingsUtils::kLargestStarSearchRadius;
    using ImageIntegration::RegistrationSettingsUtils::kSmallestMoffatBeta;
    using ImageIntegration::RegistrationSettingsUtils::kSmallestStarRoundness;

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    EXPECT_EQ(dialog.m_ui.radiusSpinBox->maximum(), kLargestStarSearchRadius);
    EXPECT_EQ(dialog.m_ui.radiusSlider->maximum(), kLargestStarSearchRadius);
    EXPECT_DOUBLE_EQ(dialog.m_ui.roundnessThresholdSpinBox->minimum(), kSmallestStarRoundness);
    EXPECT_EQ(dialog.m_ui.roundnessThresholdSlider->minimum(),
              static_cast<int>(std::lround(kSmallestStarRoundness * 1000.0)));
    EXPECT_DOUBLE_EQ(dialog.m_ui.minimumBetaSpinBox->minimum(), kSmallestMoffatBeta);

    RegistrationSettings stored;
    stored.radius = 50;
    stored.roundnessThreshold = 0.0;
    stored.profileType = QStringLiteral("Moffat");
    stored.minimumBeta = 0.0;
    dialog.setRegistrationSettings(stored);
    // The controls show what the detector takes of the stored values; the stored values are kept while they do, so
    // accepting the dialog changes nothing the detection is known by.
    const an::StarDetectionRequest run = an::ui::processes::registrationStarDetection(stored);
    EXPECT_EQ(static_cast<std::uint32_t>(dialog.m_ui.radiusSpinBox->value()), run.searchRadius);
    EXPECT_DOUBLE_EQ(dialog.m_ui.roundnessThresholdSpinBox->value(), run.minimumRoundness);
    EXPECT_DOUBLE_EQ(dialog.m_ui.minimumBetaSpinBox->value(), run.minimumBeta);
    const RegistrationSettings kept = dialog.getRegistrationSettings();
    EXPECT_EQ(kept.radius, stored.radius);
    EXPECT_DOUBLE_EQ(kept.roundnessThreshold, stored.roundnessThreshold);
    EXPECT_DOUBLE_EQ(kept.minimumBeta, stored.minimumBeta);
    const an::StarDetectionRequest keptRun = an::ui::processes::registrationStarDetection(kept);
    EXPECT_EQ(keptRun.searchRadius, run.searchRadius);
    EXPECT_DOUBLE_EQ(keptRun.minimumRoundness, run.minimumRoundness);
    EXPECT_DOUBLE_EQ(keptRun.minimumBeta, run.minimumBeta);
}

TEST(RegistrationSettingsDialog, AdvancedSettingsOpenInSecondaryDialog)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    dialog.show();
    QApplication::processEvents();

    auto* button = dialog.findChild<QPushButton*>(QStringLiteral("advancedSettingsButton"));
    ASSERT_NE(button, nullptr);
    EXPECT_FALSE(button->isCheckable());

    QTest::mouseClick(button, Qt::LeftButton);
    QApplication::processEvents();

    auto* advancedDialog = dialog.findChild<QDialog*>(QStringLiteral("registrationAdvancedSettingsDialog"));
    ASSERT_NE(advancedDialog, nullptr);
    EXPECT_TRUE(advancedDialog->isVisible());
    EXPECT_EQ(advancedDialog->windowTitle(), QStringLiteral("Advanced Registration Settings"));
}

TEST(RegistrationSettingsDialog, AutoReferenceCheckboxSharesReferenceSelectorRow)
{
    ensureQApplication();

    const QStringList frames = {QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")};
    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), frames);
    dialog.show();
    QApplication::processEvents();

    auto* checkbox = dialog.findChild<QCheckBox*>(QStringLiteral("chkAutoReferenceDuringRegistration"));
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("referenceImageComboBox"));
    auto* autoBtn = dialog.findChild<QPushButton*>(QStringLiteral("autoAssignButton"));
    ASSERT_NE(checkbox, nullptr);
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(autoBtn, nullptr);

    // The button is hidden by default; show it as callers do before checking layout.
    autoBtn->setVisible(true);
    QApplication::processEvents();

    const int checkboxY = checkbox->mapTo(&dialog, QPoint(0, 0)).y();
    const int comboY = combo->mapTo(&dialog, QPoint(0, 0)).y();
    const int autoBtnY = autoBtn->mapTo(&dialog, QPoint(0, 0)).y();

    EXPECT_NEAR(checkboxY, comboY, 8);
    EXPECT_NEAR(comboY, autoBtnY, 8);
    EXPECT_GT(checkbox->mapTo(&dialog, QPoint(0, 0)).x(), combo->mapTo(&dialog, QPoint(0, 0)).x());
}

TEST(RegistrationSettingsDialog, CollapsingMainSectionsResizesDialogHeight)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    dialog.show();
    QApplication::processEvents();

    auto* sectionWidget = dialog.findChild<QWidget*>(QStringLiteral("alignmentGroupBox"));
    auto* section = dynamic_cast<an::ui::AnCollapsibleSection*>(sectionWidget);
    ASSERT_NE(section, nullptr);
    ASSERT_NE(section->toggleButton(), nullptr);

    const int expandedHeight = dialog.height();
    const int expandedWidth = dialog.width();

    QTest::mouseClick(section->toggleButton(), Qt::LeftButton);
    QTest::qWait(section->animationDuration() + 80);
    QApplication::processEvents();

    const int collapsedHeight = dialog.height();
    const int collapsedWidth = dialog.width();
    EXPECT_LT(collapsedHeight, expandedHeight);
    EXPECT_EQ(collapsedWidth, expandedWidth);
}

TEST(RegistrationSettingsDialog, CollapsingAdvancedSectionsResizesAdvancedDialogHeight)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    dialog.show();
    QApplication::processEvents();

    auto* button = dialog.findChild<QPushButton*>(QStringLiteral("advancedSettingsButton"));
    ASSERT_NE(button, nullptr);
    QTest::mouseClick(button, Qt::LeftButton);
    QApplication::processEvents();

    auto* advancedDialog = dialog.findChild<QDialog*>(QStringLiteral("registrationAdvancedSettingsDialog"));
    ASSERT_NE(advancedDialog, nullptr);
    ASSERT_TRUE(advancedDialog->isVisible());

    auto* sectionWidget = advancedDialog->findChild<QWidget*>(QStringLiteral("starDetectionGroupBox"));
    auto* section = dynamic_cast<an::ui::AnCollapsibleSection*>(sectionWidget);
    ASSERT_NE(section, nullptr);
    ASSERT_NE(section->toggleButton(), nullptr);

    const int expandedHeight = advancedDialog->height();
    const int expandedWidth = advancedDialog->width();

    QTest::mouseClick(section->toggleButton(), Qt::LeftButton);
    QTest::qWait(section->animationDuration() + 80);
    QApplication::processEvents();

    const int collapsedHeight = advancedDialog->height();
    const int collapsedWidth = advancedDialog->width();
    EXPECT_LT(collapsedHeight, expandedHeight);
    EXPECT_EQ(collapsedWidth, expandedWidth);
}

TEST(RegistrationSettingsDialog, DetailedTooltipsArePresentForMainAndAdvancedParameters)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    dialog.show();
    QApplication::processEvents();

    auto* minStarPairs = dialog.findChild<QSpinBox*>(QStringLiteral("minStarPairsSpinBox"));
    auto* methodCombo = dialog.findChild<QComboBox*>(QStringLiteral("registrationMethodComboBox"));
    auto* interpolationCombo = dialog.findChild<QComboBox*>(QStringLiteral("interpolationComboBox"));
    ASSERT_NE(minStarPairs, nullptr);
    ASSERT_NE(methodCombo, nullptr);
    ASSERT_NE(interpolationCombo, nullptr);

    EXPECT_TRUE(minStarPairs->toolTip().contains(QStringLiteral("matched stars")));
    EXPECT_TRUE(methodCombo->toolTip().contains(QStringLiteral("registration pipeline")));
    EXPECT_TRUE(interpolationCombo->toolTip().contains(QStringLiteral("resampling")));
    EXPECT_TRUE(interpolationCombo->toolTip().contains(QStringLiteral("Nearest Neighbor")));
    EXPECT_TRUE(interpolationCombo->toolTip().contains(QStringLiteral("Bilinear")));
    EXPECT_TRUE(interpolationCombo->toolTip().contains(QStringLiteral("Lanczos4")));
    EXPECT_FALSE(interpolationCombo->toolTip().contains(QStringLiteral("Sharper kernels")));

    auto* button = dialog.findChild<QPushButton*>(QStringLiteral("advancedSettingsButton"));
    ASSERT_NE(button, nullptr);
    QTest::mouseClick(button, Qt::LeftButton);
    QApplication::processEvents();

    auto* advancedDialog = dialog.findChild<QDialog*>(QStringLiteral("registrationAdvancedSettingsDialog"));
    ASSERT_NE(advancedDialog, nullptr);

    auto* radiusSpinBox = advancedDialog->findChild<QSpinBox*>(QStringLiteral("radiusSpinBox"));
    auto* thresholdSpinBox = advancedDialog->findChild<QDoubleSpinBox*>(QStringLiteral("thresholdSpinBox"));
    auto* cfaCombo = advancedDialog->findChild<QComboBox*>(QStringLiteral("cfaPatternComboBox"));
    ASSERT_NE(radiusSpinBox, nullptr);
    ASSERT_NE(thresholdSpinBox, nullptr);
    ASSERT_NE(cfaCombo, nullptr);

    EXPECT_TRUE(radiusSpinBox->toolTip().contains(QStringLiteral("PSF")));
    EXPECT_TRUE(thresholdSpinBox->toolTip().contains(QStringLiteral("sigma")));
    EXPECT_TRUE(cfaCombo->toolTip().contains(QStringLiteral("Bayer")));
}

TEST(RegistrationSettingsDialog, TooltipsUseReadableMultilineFormatting)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    dialog.show();
    QApplication::processEvents();

    auto* autoReference = dialog.findChild<QCheckBox*>(QStringLiteral("chkAutoReferenceDuringRegistration"));
    auto* registrationMethod = dialog.findChild<QComboBox*>(QStringLiteral("registrationMethodComboBox"));
    auto* transformation = dialog.findChild<QComboBox*>(QStringLiteral("transformationComboBox"));
    auto* autoSolve = dialog.findChild<QCheckBox*>(QStringLiteral("chkAutoSolveUnsolvedFrames"));
    ASSERT_NE(autoReference, nullptr);
    ASSERT_NE(registrationMethod, nullptr);
    ASSERT_NE(transformation, nullptr);
    ASSERT_NE(autoSolve, nullptr);

    EXPECT_TRUE(autoReference->toolTip().contains(QStringLiteral("What it does:")));
    EXPECT_TRUE(autoReference->toolTip().contains(QStringLiteral("How it works:")));
    EXPECT_TRUE(autoReference->toolTip().contains(QStringLiteral("\n\n")));

    EXPECT_TRUE(registrationMethod->toolTip().contains(QStringLiteral("What it does:")));
    EXPECT_TRUE(registrationMethod->toolTip().contains(QStringLiteral("Options:")));
    EXPECT_TRUE(registrationMethod->toolTip().contains(QStringLiteral("- ")));

    EXPECT_TRUE(transformation->toolTip().contains(QStringLiteral("Modes:")));
    EXPECT_TRUE(transformation->toolTip().contains(QStringLiteral("Similarity")));
    EXPECT_TRUE(transformation->toolTip().contains(QStringLiteral("Polynomial 2/3/4")));
    EXPECT_TRUE(transformation->toolTip().contains(QStringLiteral("Auto starts with Homography")));

    EXPECT_TRUE(autoSolve->toolTip().contains(QStringLiteral("Why it's unavailable:")));
    EXPECT_TRUE(autoSolve->toolTip().contains(QStringLiteral("To enable it:")));
    EXPECT_TRUE(autoSolve->toolTip().contains(QStringLiteral("\n\n")));

    auto* button = dialog.findChild<QPushButton*>(QStringLiteral("advancedSettingsButton"));
    ASSERT_NE(button, nullptr);
    QTest::mouseClick(button, Qt::LeftButton);
    QApplication::processEvents();

    auto* advancedDialog = dialog.findChild<QDialog*>(QStringLiteral("registrationAdvancedSettingsDialog"));
    ASSERT_NE(advancedDialog, nullptr);

    auto* radiusSpinBox = advancedDialog->findChild<QSpinBox*>(QStringLiteral("radiusSpinBox"));
    auto* cfaCombo = advancedDialog->findChild<QComboBox*>(QStringLiteral("cfaPatternComboBox"));
    ASSERT_NE(radiusSpinBox, nullptr);
    ASSERT_NE(cfaCombo, nullptr);

    EXPECT_TRUE(radiusSpinBox->toolTip().contains(QStringLiteral("What it does:")));
    EXPECT_TRUE(radiusSpinBox->toolTip().contains(QStringLiteral("Adjust it when:")));
    EXPECT_TRUE(radiusSpinBox->toolTip().contains(QStringLiteral("- ")));

    EXPECT_TRUE(cfaCombo->toolTip().contains(QStringLiteral("What it does:")));
    EXPECT_TRUE(cfaCombo->toolTip().contains(QStringLiteral("Use this when:")));
    EXPECT_TRUE(cfaCombo->toolTip().contains(QStringLiteral("- ")));
}

TEST(RegistrationSettingsDialog, AffectedControlsKeepReadableHeightsAndSummaryPadding)
{
    ensureQApplication();

    RegistrationSettingsDialog dialog(QStringLiteral("Group 1"), {QStringLiteral("C:/light_a.fit")});
    dialog.show();
    QApplication::processEvents();

    auto* caFitTypeCombo = dialog.findChild<QComboBox*>(QStringLiteral("chromaticAberrationTransformTypeComboBox"));
    auto* caSummaryLabel = dialog.findChild<QLabel*>(QStringLiteral("chromaticAberrationSummaryLabel"));
    ASSERT_NE(caFitTypeCombo, nullptr);
    ASSERT_NE(caSummaryLabel, nullptr);

    auto* advancedButton = dialog.findChild<QPushButton*>(QStringLiteral("advancedSettingsButton"));
    ASSERT_NE(advancedButton, nullptr);
    QTest::mouseClick(advancedButton, Qt::LeftButton);
    QApplication::processEvents();

    auto* advancedDialog = dialog.findChild<QDialog*>(QStringLiteral("registrationAdvancedSettingsDialog"));
    ASSERT_NE(advancedDialog, nullptr);

    auto* profileTypeCombo = advancedDialog->findChild<QComboBox*>(QStringLiteral("profileTypeComboBox"));
    auto* cfaPatternCombo = advancedDialog->findChild<QComboBox*>(QStringLiteral("cfaPatternComboBox"));
    auto* caInterpolationCombo =
        dialog.findChild<QComboBox*>(QStringLiteral("chromaticAberrationInterpolationComboBox"));
    ASSERT_NE(profileTypeCombo, nullptr);
    ASSERT_NE(cfaPatternCombo, nullptr);
    ASSERT_NE(caInterpolationCombo, nullptr);

    const int minReadableComboHeight = profileTypeCombo->fontMetrics().height() + 8;
    EXPECT_GE(caFitTypeCombo->height(), minReadableComboHeight);
    EXPECT_GE(caInterpolationCombo->height(), minReadableComboHeight);
    EXPECT_GE(profileTypeCombo->height(), minReadableComboHeight);
    EXPECT_GE(cfaPatternCombo->height(), minReadableComboHeight);

    EXPECT_GE(caSummaryLabel->margin(), 2);
    EXPECT_GE(caSummaryLabel->height(), caSummaryLabel->fontMetrics().height() + 4);
}
