// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The second star profiler's window over the one detector, with each star placed on the sky and matched against the
/// installed Gaia catalog.

#include "stdafx.hpp"

#include "StarProfilerProcessV2.h"

#include "app/GaiaCatalogSettings.h"
#include "app/ObservationEpochUtils.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/ProcessButtonIcons.h"
#include "controls/StarsFilterControlV2.h"
#include "processes/ProcessCommonUtils.h"

#include <QTableWidgetItem>

#include <utility>

namespace
{

using an::ui::processes::SecondProfilerColumn;

/// The detector's largest search radius, which the dialog's radius is held to.
constexpr int kLargestSearchRadius = 32;

/// The smallest roundness the detector admits, which the dialog's threshold starts at.
constexpr double kSmallestRoundness = 0.01;

/// The smallest Moffat exponent the dialog offers: the detector needs a positive one.
constexpr double kSmallestBeta = 0.05;

/// How many decimals a column shows.
[[nodiscard]] int decimalsOf(const SecondProfilerColumn column) noexcept
{
    switch (column)
    {
    case SecondProfilerColumn::X:
    case SecondProfilerColumn::Y:
    case SecondProfilerColumn::Magnitude:
    case SecondProfilerColumn::FullWidthX:
    case SecondProfilerColumn::FullWidthY:
    case SecondProfilerColumn::Angle:
        return 2;
    case SecondProfilerColumn::Ratio:
        return 3;
    case SecondProfilerColumn::ResidualRootMeanSquare:
    case SecondProfilerColumn::FullWidth:
    case SecondProfilerColumn::Circularity:
        return 7;
    default:
        return 6;
    }
}

} // namespace

StarProfilerProcessV2::StarProfilerProcessV2(ImageViewControl* control)
    : StarProfilerWindow(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildStarProfilerProcessV2Ui(this);
    setProcessDescription(
        QStringLiteral("Profile stars with robust PSF controls, quality filtering, and reusable star catalogs."));

    m_ui.detectedStarsGroupBox->setFlat(true);
    m_ui.starDetectionGroupBox->setFlat(true);
    m_ui.amplitudeRangeGroupBox->setFlat(true);
    m_ui.roundnessRangeGroupBox->setFlat(true);

    // The dialog offers what the detector accepts.
    m_ui.radiusValue->setMaximum(kLargestSearchRadius);
    m_ui.radiusSlider->setMaximum(kLargestSearchRadius);
    m_ui.roundnessThresholdValue->setMinimum(kSmallestRoundness);
    m_ui.minimumBetaValue->setMinimum(kSmallestBeta);

    QObject::connect(m_ui.profileTypeSelector, &QComboBox::currentTextChanged, this,
                     &StarProfilerProcessV2::on_profileTypeSelector_currentTextChanged);

    INIT_SPINBOX_CTRL(radiusControl, m_ui.radiusValue, m_ui.radiusSlider, []() {});
    INIT_D_SPINBOX_CTRL(thresholdControl, m_ui.thresholdValue, m_ui.thresholdSlider, []() {});
    INIT_D_SPINBOX_CTRL(thresholdMinimumControl, m_ui.thresholdMinimumValue, m_ui.thresholdMinimumSlider, []() {});
    INIT_D_SPINBOX_CTRL(thresholdMaximumControl, m_ui.thresholdMaximumValue, m_ui.thresholdMaximumSlider, []() {});
    INIT_D_SPINBOX_CTRL(roundnessThresholdControl, m_ui.roundnessThresholdValue, m_ui.roundnessThresholdSlider,
                        []() {});
    INIT_D_SPINBOX_CTRL(roundnessThresholdMaximumControl, m_ui.roundnessThresholdMaximumValue,
                        m_ui.roundnessThresholdMaximumSlider, []() {});

    m_ui.profileTypeSelector->addItem(QStringLiteral("Gaussian"));
    m_ui.profileTypeSelector->addItem(QStringLiteral("Moffat"));

    attachWidgets(an::ui::processes::StarProfilerWidgets{.table = m_ui.tableWidget,
                                                         .detect = m_ui.detectStarsButton,
                                                         .reset = m_ui.resetButton,
                                                         .load = m_ui.loadProfileButton,
                                                         .save = m_ui.saveProfileButton,
                                                         .clear = m_ui.clearProfileButton,
                                                         .filter = m_ui.starsFilterButton,
                                                         .mask = m_ui.createMaskButton,
                                                         .deleteStar = m_ui.deleteStarButton,
                                                         .detectedStars = m_ui.detectedStarsLabel,
                                                         .centerSelected = m_ui.centerSelectedStar,
                                                         .zoom = m_ui.zoomDetectedStars,
                                                         .buttons = m_ui.buttonBox});
    resetSettings();
    updateWindowTitle();
    refreshTheme();
}

StarProfilerProcessV2::~StarProfilerProcessV2() = default;

std::string StarProfilerProcessV2::getProcessNameStatic()
{
    return "StarProfiler";
}

bool StarProfilerProcessV2::requiresExclusiveAccessStatic()
{
    return true;
}

an::ui::processes::SecondProfilerSettings StarProfilerProcessV2::settings() const
{
    an::ui::processes::SecondProfilerSettings settings;
    settings.radius = radiusControl->getValue();
    settings.sigma = thresholdControl->getValue();
    settings.amplitudeRange = m_ui.amplitudeRangeGroupBox->isChecked();
    settings.minimumAmplitude = thresholdMinimumControl->getValue();
    settings.maximumAmplitude = thresholdMaximumControl->getValue();
    settings.roundness = roundnessThresholdControl->getValue();
    settings.roundnessRange = m_ui.roundnessRangeGroupBox->isChecked();
    settings.maximumRoundness = roundnessThresholdMaximumControl->getValue();
    settings.convergence = m_ui.convergenceValue->value();
    settings.relaxChecks = m_ui.relaxPSFChecks->isChecked();
    settings.moffat = m_ui.profileTypeSelector->currentText() == QStringLiteral("Moffat");
    settings.minimumBeta = m_ui.minimumBetaValue->value();
    settings.useLightness = m_ui.useLightness->isChecked();
    return settings;
}

const an::ui::processes::StarProfilerProcessV2UiElements& StarProfilerProcessV2::ui() const
{
    return m_ui;
}

void StarProfilerProcessV2::resetSettings()
{
    radiusControl->setValue(5);
    thresholdControl->setValue(1.0);
    thresholdMinimumControl->setValue(0.0);
    thresholdMaximumControl->setValue(1.0);
    m_ui.amplitudeRangeGroupBox->setChecked(false);
    roundnessThresholdControl->setValue(0.5);
    roundnessThresholdMaximumControl->setValue(1.0);
    m_ui.roundnessRangeGroupBox->setChecked(false);
    m_ui.convergenceValue->setValue(1);
    m_ui.relaxPSFChecks->setChecked(false);
    m_ui.minimumBetaValue->setValue(1.5);
    m_ui.profileTypeSelector->setCurrentText(QStringLiteral("Gaussian"));
    m_ui.minimumBetaValue->setEnabled(false);
}

void StarProfilerProcessV2::refreshTheme()
{
    StarProfilerWindow::refreshTheme();
    if (auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
        m_ui.deleteStarButton->setIcon(iconProvider->getIcon(QStringLiteral("close")));
    }
}

void StarProfilerProcessV2::on_profileTypeSelector_currentTextChanged(const QString& text)
{
    m_ui.minimumBetaValue->setEnabled(text == QStringLiteral("Moffat"));
}

an::StarProfileGeneration StarProfilerProcessV2::profileGeneration() const
{
    return an::StarProfileGeneration::Version2;
}

QString StarProfilerProcessV2::profileFileFilter() const
{
    return QStringLiteral("Files (*.anspv2)");
}

QString StarProfilerProcessV2::profileFolderContext() const
{
    return QStringLiteral("__STAR_PROFILER_V2_FOLDER__");
}

std::string StarProfilerProcessV2::logName() const
{
    return "StarProfilerV2";
}

an::ui::processes::StarProfileWork StarProfilerProcessV2::detectionWork() const
{
    const an::ui::processes::SecondProfilerSettings asked = settings();
    // The catalog is found now, on the GUI thread; a directory that holds no ready catalog matches nothing.
    QString catalogDirectory = an::ui::resolveGaiaCatalogDirectory(process_utils::currentGlobalSettings());
    if (an::ui::inspectGaiaCatalogDirectory(catalogDirectory) != an::ui::GaiaCatalogDirectoryStatus::Ready)
    {
        catalogDirectory.clear();
    }
    return [asked,
            catalogDirectory](const an::ui::ExecutionScope& scope,
                              const an::ui::ProcessSource& source) -> an::Result<an::ui::processes::StarProfileResult> {
        an::Result<an::ui::processes::ProfileDetection> detection = an::ui::processes::detectProfileStars(
            scope, source, an::ui::processes::secondProfilerDetection(asked), asked.useLightness);
        if (!detection.hasValue())
        {
            return detection.status();
        }
        an::ui::processes::StarProfileResult result;
        result.stars = std::move(detection->stars);
        an::ui::processes::ProfileCatalogFigures figures;
        const std::optional<an::ImageDescriptor> described = source.descriptor();
        const std::optional<an::WcsSolution> solution =
            described.has_value() ? an::ui::viewImageSolution(source.metadata(), described->extent().width(),
                                                              described->extent().height())
                                  : std::nullopt;
        if (solution.has_value() && !result.stars.empty())
        {
            an::ui::processes::ProfileEnrichmentRequest request;
            request.catalogDirectory = catalogDirectory;
            request.observationEpochYear = an::ui::resolveObservationEpochYear(source.metadata().acquisition());
            an::Result<an::ui::processes::ProfileEnrichment> enrichment =
                an::ui::processes::enrichProfileStars(scope.execution(), result.stars, *solution, request);
            if (!enrichment.hasValue())
            {
                return enrichment.status();
            }
            result.enrichment = an::ui::processes::enrichmentTable(result.stars, *enrichment);
            result.catalogFailure = enrichment->catalogFailure;
            figures = enrichment->figures;
        }
        result.header =
            an::ui::processes::secondProfileHeader(asked, detection->summary, figures, detection->channelCount);
        return result;
    };
}

QStringList StarProfilerProcessV2::columnTitles() const
{
    return {QStringLiteral("x"),     QStringLiteral("y"),    QStringLiteral("Mag"),        QStringLiteral("FWHMx"),
            QStringLiteral("FWHMy"), QStringLiteral("Ra"),   QStringLiteral("Dec"),        QStringLiteral("Angle"),
            QStringLiteral("B"),     QStringLiteral("A"),    QStringLiteral("Beta"),       QStringLiteral("r"),
            QStringLiteral("RMSE"),  QStringLiteral("FWHM"), QStringLiteral("Circularity")};
}

void StarProfilerProcessV2::fillRow(TableWidget& table, const int row, const an::DetectedStar& star) const
{
    const an::ui::processes::StarEnrichment* enrichment = an::ui::processes::enrichmentOf(this->enrichment(), star);
    for (int column = 0; column < an::ui::processes::kSecondProfilerColumnCount; ++column)
    {
        const auto which = static_cast<SecondProfilerColumn>(column);
        QString text = an::ui::processes::figureText(an::ui::processes::secondProfilerFigure(star, enrichment, which),
                                                     decimalsOf(which));
        // The widths are in pixels: the dialog states no plate scale.
        if ((which == SecondProfilerColumn::FullWidthX || which == SecondProfilerColumn::FullWidthY) &&
            text != QStringLiteral("-"))
        {
            text += QStringLiteral("px");
        }
        table.setItem(row, column, new QTableWidgetItem(text));
    }
}

an::ui::StarList StarProfilerProcessV2::orderedBy(an::ui::StarList stars, const int column, const bool descending) const
{
    if (column < 0 || column >= an::ui::processes::kSecondProfilerColumnCount)
    {
        return stars;
    }
    return an::ui::processes::orderedForSecondProfiler(std::move(stars), enrichment(),
                                                       static_cast<SecondProfilerColumn>(column), descending);
}

StarsFilterControlBase* StarProfilerProcessV2::makeStarsFilter()
{
    return new StarsFilterControlV2(this);
}
