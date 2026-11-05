// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "PhotometricColorCalibrationProcess.h"

#include "app/GaiaCatalogSettings.h"
#include "app/GaiaCatalogSource.h"
#include "app/ObservationEpochUtils.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessButtonIcons.h"
#include "controls/ProcessExecution.h"
#include "processes/PhotometricCalibrationSupport.h"
#include "processes/ProcessCommonUtils.h"
#include "session/ExecutionScope.h"
#include "session/ImageReads.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QCloseEvent>
#include <QElapsedTimer>
#include <QPainter>
#include <QPen>
#include <QPointer>

#include <fmt/format.h>

#include <algorithm>
#include <utility>

namespace
{

using an::ui::processes::CalibrationTargetProblem;

/// What the window reads of its target on the GUI thread, under a lease that never waits.
struct TargetFacts
{
    std::optional<an::ImageDescriptor> descriptor;
    CalibrationTargetProblem problem = CalibrationTargetProblem::NoImage;
    std::optional<an::WcsSolution> solution;
    std::optional<double> epochYear;
    std::string filterName;
};

/// The facts of a view's image; nothing while a writer holds the document, when the window keeps what it showed.
[[nodiscard]] std::optional<TargetFacts> targetFactsOf(const ImageViewControl* view)
{
    TargetFacts facts;
    if (view == nullptr || !view->hasImage())
    {
        return facts;
    }
    an::Result<an::consumer::AssetReadLease> lease = view->document().tryRead();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    facts.descriptor = view->document().descriptor();
    const an::ImageMetadata& metadata =
        std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, lease->image());
    facts.problem = an::ui::processes::photometricTargetProblem(facts.descriptor, metadata);
    if (facts.descriptor.has_value())
    {
        facts.solution = an::ui::processes::calibrationSolutionOf(metadata, *facts.descriptor);
    }
    facts.epochYear = an::ui::resolveObservationEpochYear(metadata.acquisition());
    facts.filterName = metadata.acquisition().filterName();
    return facts;
}

/// The engine's word for why a run failed, in a sentence for a person.
[[nodiscard]] QString failureText(const an::Status& status)
{
    const QString message = QString::fromUtf8(status.message()).trimmed();
    return message.isEmpty() ? QStringLiteral("Photometric color calibration failed.") : message;
}

} // namespace

namespace an::ui::processes
{

an::StarDetectionRequest photometricStarDetection()
{
    an::StarDetectionRequest request;
    request.searchRadius = 5u;
    request.noiseSigma = 1.0;
    request.minimumRoundness = 0.4;
    request.model = an::PsfModel::Gaussian;
    request.relaxedChecks = true;
    return request;
}

an::PhotometricColorCalibrationRequest photometricCalibrationRequest(const PhotometricCalibrationSettings& settings,
                                                                     std::string_view filterName)
{
    an::PhotometricColorCalibrationRequest request;
    request.maximumStarsPerChannel = 2000u;
    request.match.matchRadiusArcseconds = settings.matchRadiusArcseconds;
    request.match.magnitudeLimit = settings.magnitudeLimit;
    request.channelMatchRadiusPixels = 3.0;
    request.fit.minimumSamples = 10u;
    request.background.neutralize = settings.neutralizeBackground;
    if (settings.neutralizeBackground)
    {
        request.background.region = calibrationRegion(settings.backgroundRegion);
    }
    request.filterName = filterName;
    return request;
}

} // namespace an::ui::processes

/// What a run snapshots on the GUI thread and finds on its worker, read by its completion on the GUI thread.
struct PhotometricColorCalibrationProcess::RunOutcome
{
    an::ui::processes::PhotometricCalibrationSettings settings;
    std::string filterName;
    std::optional<double> epochYear;
    QString catalogDirectory;
    std::shared_ptr<const an::consumer::ImageAsset> mask;
    bool invertMask = false;
    std::shared_ptr<std::stop_source> stop;

    an::Status status = an::Status::success();
    an::ColorChannelGains gains{1.0, 1.0, 1.0};
    std::uint32_t matchedStars = 0u;
    bool usedCatalogColorModel = false;
    std::vector<an::PhotometricColorSample> samples;
};

// ---------------------------------------------------------------------------
// PccMatchedStarOverlay
// ---------------------------------------------------------------------------

void PccMatchedStarOverlay::setMatchedSamples(std::vector<an::PhotometricColorSample> samples)
{
    {
        const std::lock_guard lock(m_mutex);
        m_samples = std::move(samples);
    }
    if (m_ViewWidget)
    {
        m_ViewWidget->update();
    }
}

void PccMatchedStarOverlay::clearMatchedSamples()
{
    setMatchedSamples({});
}

std::vector<an::PhotometricColorSample> PccMatchedStarOverlay::matchedSamples() const
{
    const std::lock_guard lock(m_mutex);
    return m_samples;
}

void PccMatchedStarOverlay::onDraw(QPainter& painter)
{
    const std::vector<an::PhotometricColorSample> snapshot = matchedSamples();
    if (snapshot.empty() || !m_ViewWidget)
    {
        return;
    }

    const double scale = m_ViewWidget->getCurrentScaleFactor();
    constexpr double kCircleRadiusPx = 8.0;
    for (const an::PhotometricColorSample& sample : snapshot)
    {
        // Green where the catalog gave the star's colour, yellow where the neutral default stood in.
        const QColor color = sample.colorIndexBpMinusRp.has_value() ? QColor(0, 220, 80) : QColor(220, 200, 40);
        QPen pen(color);
        pen.setWidth(2);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        // An engine pixel coordinate puts a column's centre at its index plus one half, where the view draws it.
        painter.drawEllipse(QPointF(sample.position.x * scale, sample.position.y * scale), kCircleRadiusPx,
                            kCircleRadiusPx);
    }
}

// ---------------------------------------------------------------------------
// PhotometricColorCalibrationProcess
// ---------------------------------------------------------------------------

PhotometricColorCalibrationProcess::PhotometricColorCalibrationProcess(ImageViewControl* control)
    : ImageProcessControl(control, getProcessNameStatic())
{
    setFixedSizeWindow(true);
    setWindowSizeFitMultipliers(1.3, 1.0);

    m_ui = an::ui::processes::buildPhotometricColorCalibrationProcessUi(this);
    setProcessDescription(
        QStringLiteral("Calibrate RGB channel balance from Gaia DR3 star colors on a plate-solved image."));

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &PhotometricColorCalibrationProcess::on_dialog_box_button_clicked);
    QObject::connect(m_ui.backgroundRoiGroupBox, &QGroupBox::clicked, this,
                     &PhotometricColorCalibrationProcess::onBackgroundRoiGroupBoxClicked);
    QObject::connect(m_ui.backgroundRoiSelectionButton, &QPushButton::clicked, this,
                     &PhotometricColorCalibrationProcess::onBackgroundRoiSelectionButtonClicked);
    QObject::connect(m_ui.backgroundRoiLeftValue, &QSpinBox::valueChanged, this,
                     &PhotometricColorCalibrationProcess::onBackgroundRoiLeftValueChanged);
    QObject::connect(m_ui.backgroundRoiTopValue, &QSpinBox::valueChanged, this,
                     &PhotometricColorCalibrationProcess::onBackgroundRoiTopValueChanged);
    QObject::connect(m_ui.backgroundRoiWidthValue, &QSpinBox::valueChanged, this,
                     &PhotometricColorCalibrationProcess::onBackgroundRoiWidthValueChanged);
    QObject::connect(m_ui.backgroundRoiHeightValue, &QSpinBox::valueChanged, this,
                     &PhotometricColorCalibrationProcess::onBackgroundRoiHeightValueChanged);
    QObject::connect(m_ui.backgroundNeutralizationCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState) {
        if (!m_ui.backgroundNeutralizationCheck->isChecked())
        {
            destroyCropController();
            m_ui.backgroundRoiSelectionButton->setChecked(false);
        }
        updateAvailabilityState();
    });
    QObject::connect(m_ui.showMatchedStars, &QCheckBox::toggled, this,
                     &PhotometricColorCalibrationProcess::onShowMatchedStarsToggled);

    updateWindowTitle();

    m_SupportTargetViewControlSelection = true;
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    attachOverlayToMainView();
    updateBackgroundRoiData();
    updateAvailabilityState();
    refreshTheme();
}

std::string PhotometricColorCalibrationProcess::getProcessNameStatic()
{
    return "Photometric Color Calibration";
}

bool PhotometricColorCalibrationProcess::requiresExclusiveAccessStatic()
{
    return true;
}

void PhotometricColorCalibrationProcess::onImageLoaded(ImageViewControl* ctrl)
{
    if (ctrl == m_image_view_control && !isViewControlSupported(ctrl))
    {
        close();
        return;
    }

    ImageProcessControl::onImageLoaded(ctrl);
    updateBackgroundRoiData();
    updateAvailabilityState();
}

void PhotometricColorCalibrationProcess::onImageUpdated(ImageViewControl* ctrl)
{
    if (ctrl == m_image_view_control && !isViewControlSupported(ctrl))
    {
        close();
        return;
    }

    ImageProcessControl::onImageUpdated(ctrl);
    attachOverlayToMainView();
    updateBackgroundRoiData();
    updateSelectionArea();
    updateAvailabilityState();
}

void PhotometricColorCalibrationProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    if (!isViewControlSupported(newCtrl))
    {
        close();
        return;
    }

    detachOverlay();
    destroyCropController();
    resetBackgroundRoiValues();
    clearCalibrationResult(true);
    ImageProcessControl::onViewControlChanged(oldCtrl, newCtrl);
    attachOverlayToMainView();
    updateBackgroundRoiData();
    updateAvailabilityState();
}

void PhotometricColorCalibrationProcess::onImageViewControlDestroyed(ImageViewControl* ctrl)
{
    if (ctrl == m_image_view_control)
    {
        // The edit on its way stops with its document; the window lets the view go before it does.
        if (m_runStop)
        {
            m_runStop->request_stop();
        }
        clearCalibrationResult(true);
        detachOverlay();
        destroyCropController();
    }
    ImageProcessControl::onImageViewControlDestroyed(ctrl);
}

void PhotometricColorCalibrationProcess::selectionAreaUpdated()
{
    if (!m_image_view_control)
    {
        return;
    }

    m_IgnoreLastCropMoveEvent = true;
    const auto selection = m_image_view_control->getViewWidget()->getCurrentSelectionArea();
    m_ui.backgroundRoiLeftValue->setValue(selection.x());
    m_ui.backgroundRoiTopValue->setValue(selection.y());
    m_ui.backgroundRoiWidthValue->setValue(selection.width());
    m_ui.backgroundRoiHeightValue->setValue(selection.height());
    m_IgnoreLastCropMoveEvent = false;
}

void PhotometricColorCalibrationProcess::selectionAreaChanged() {}

void PhotometricColorCalibrationProcess::newSelectionFinished() {}

PhotometricColorCalibrationProcess::~PhotometricColorCalibrationProcess()
{
    destroyCropController();
    detachOverlay();
}

void PhotometricColorCalibrationProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }

    if (m_ui.warningBanner)
    {
        m_ui.warningBanner->refreshTheme();
    }
}

bool PhotometricColorCalibrationProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    if (ctrl == nullptr || !ctrl->hasImage())
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> descriptor = ctrl->imageDescriptor();
    return descriptor.has_value() && an::ui::processes::isCalibrationRgb(*descriptor);
}

void PhotometricColorCalibrationProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    if (!m_ui.buttonBox)
    {
        return;
    }

    const QDialogButtonBox::StandardButton sb = m_ui.buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Apply)
    {
        applyCalibration();
    }
    else if (sb == QDialogButtonBox::Reset)
    {
        m_ui.magnitudeLimitSpin->setValue(14.0);
        m_ui.matchRadiusSpin->setValue(5.0);
        m_ui.backgroundNeutralizationCheck->setChecked(true);
        destroyCropController();
        m_ui.backgroundRoiSelectionButton->setChecked(false);
        m_ui.backgroundRoiGroupBox->setChecked(false);
        resetBackgroundRoiValues();
        updateBackgroundRoiData();
        m_ui.showMatchedStars->setChecked(true);
        clearCalibrationResult(true);
        updateAvailabilityState();
    }
    else if (sb == QDialogButtonBox::Close || sb == QDialogButtonBox::Cancel)
    {
        this->close();
    }
}

an::ui::processes::PhotometricCalibrationSettings PhotometricColorCalibrationProcess::settings() const
{
    an::ui::processes::PhotometricCalibrationSettings settings;
    settings.magnitudeLimit = m_ui.magnitudeLimitSpin->value();
    settings.matchRadiusArcseconds = m_ui.matchRadiusSpin->value();
    settings.neutralizeBackground = m_ui.backgroundNeutralizationCheck->isChecked();
    if (settings.neutralizeBackground && m_ui.backgroundRoiGroupBox->isChecked())
    {
        settings.backgroundRegion =
            QRect(m_ui.backgroundRoiLeftValue->value(), m_ui.backgroundRoiTopValue->value(),
                  m_ui.backgroundRoiWidthValue->value(), m_ui.backgroundRoiHeightValue->value());
    }
    return settings;
}

const PccMatchedStarOverlay* PhotometricColorCalibrationProcess::matchedStarOverlay() const
{
    return m_starOverlay.get();
}

void PhotometricColorCalibrationProcess::applyCalibration()
{
    if (m_ExecutionInProgress || !m_image_view_control || !m_image_view_control->hasImage())
    {
        return;
    }

    const QString title =
        QCoreApplication::translate("PhotometricColorCalibrationProcess", "Photometric Color Calibration");
    const std::optional<TargetFacts> facts = targetFactsOf(m_image_view_control);
    if (!facts.has_value())
    {
        m_image_view_control->showViewControlBusyWarning(QString::fromUtf8(getProcessName()));
        return;
    }
    auto outcome = std::make_shared<RunOutcome>();
    outcome->settings = settings();
    QString refusal;
    switch (facts->problem)
    {
    case CalibrationTargetProblem::None:
        break;
    case CalibrationTargetProblem::NoImage:
        refusal = tr("No target image is available.");
        break;
    case CalibrationTargetProblem::NotRgb:
        refusal = tr("Photometric color calibration only supports RGB images.");
        break;
    case CalibrationTargetProblem::Narrowband:
        refusal = tr("Photometric color calibration is not applicable to narrowband data.");
        break;
    case CalibrationTargetProblem::NotPlateSolved:
        refusal = tr("Photometric color calibration requires a plate-solved image.");
        break;
    case CalibrationTargetProblem::DeclaredNonlinear:
        refusal = tr("Photometric color calibration requires linear data; this image is marked nonlinear.");
        break;
    }
    if (refusal.isEmpty() && outcome->settings.neutralizeBackground && m_ui.backgroundRoiGroupBox->isChecked())
    {
        const QRect& region = outcome->settings.backgroundRegion;
        const an::ImageExtent& extent = facts->descriptor->extent();
        const bool inside = region.width() > 0 && region.height() > 0 && region.x() >= 0 && region.y() >= 0 &&
                            region.x() + region.width() <= static_cast<int>(extent.width()) &&
                            region.y() + region.height() <= static_cast<int>(extent.height());
        if (!inside)
        {
            refusal = tr("The PCC background ROI must be fully inside the target image and have a non-zero size.");
        }
    }
    outcome->catalogDirectory = an::ui::resolveGaiaCatalogDirectory(process_utils::currentGlobalSettings());
    if (refusal.isEmpty() &&
        an::ui::inspectGaiaCatalogDirectory(outcome->catalogDirectory) != an::ui::GaiaCatalogDirectoryStatus::Ready)
    {
        refusal = tr("The Gaia DR3 catalog is not configured or not found.");
    }
    if (!refusal.isEmpty())
    {
        notifyErrorMessage(title, refusal);
        updateAvailabilityState();
        return;
    }

    outcome->filterName = facts->filterName;
    outcome->epochYear = facts->epochYear;
    if (m_image_view_control->isMasked() && m_image_view_control->isMaskEnabled())
    {
        if (ImageViewControl* maskView = m_image_view_control->getCurrentMask(); maskView && maskView->hasImage())
        {
            outcome->mask = maskView->document().asset();
            outcome->invertMask = m_image_view_control->isMaskInverted();
        }
    }
    m_runStop = std::make_shared<std::stop_source>();
    outcome->stop = m_runStop;
    calibrationStarting(*m_runStop);

    clearCalibrationResult(true);
    m_ui.statusLabel->setText(tr("Calibrating photometric gains from Gaia-matched stars..."));
    m_ExecutionInProgress = true;
    updateAvailabilityState();
    LOG_INFO("Executing process '{}' on image '{}'... ", getProcessName(), m_image_view_control->getUniqueName());

    const an::ui::processes::PhotometricCalibrationSettings& asked = outcome->settings;
    const std::string description =
        fmt::format("Magnitude limit: {:.1f}, Match radius: {:.1f} arcsec, Background neutralization: {}",
                    asked.magnitudeLimit, asked.matchRadiusArcseconds, asked.neutralizeBackground ? "on" : "off");

    QPointer<PhotometricColorCalibrationProcess> guard(this);
    an::ui::ExecutionObserver observer;
    observer.receiver = this;
    observer.progress = [guard](const an::ui::ExecutionProgress& progress) {
        if (guard && !progress.stage.empty())
        {
            QString text = QString::fromStdString(progress.stage);
            if (progress.total > 0u)
            {
                text += QStringLiteral(" (%1/%2)").arg(progress.completed).arg(progress.total);
            }
            guard->m_ui.statusLabel->setText(text);
        }
    };
    const std::shared_ptr<void> inFlight = beginRunInFlight();
    // The mask is another view's image, leased by the edit with the target's writer, in one order, before the run asks
    // to be admitted.
    auto reads = std::make_shared<an::ui::ImageReads>();
    reads->name(outcome->mask);
    QElapsedTimer timer;
    timer.start();
    m_image_view_control->editImage(
        getProcessName(), description,
        [outcome, observer, inFlight, reads, processStop = windowStopToken()](
            const an::consumer::EngineSession& session, const an::consumer::AssetWriteLease& image,
            std::stop_token stop) -> an::Result<an::consumer::ImageAsset> {
            // The edit's own stop - the document changed or closed - the run's and the window's going all stop the run.
            std::stop_source runStop;
            const std::stop_callback onEdit(stop, [&runStop]() { runStop.request_stop(); });
            const std::stop_callback onWindow(outcome->stop->get_token(), [&runStop]() { runStop.request_stop(); });
            const std::stop_callback onProcess(processStop, [&runStop]() { runStop.request_stop(); });
            an::Result<an::ui::ExecutionScope> scope =
                an::ui::ExecutionScope::admit(session,
                                              an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply,
                                                                       .stopToken = runStop.get_token(),
                                                                       .reads = reads.get()},
                                              observer);
            if (!scope.hasValue())
            {
                outcome->status = scope.status();
                return scope.status();
            }
            const auto finish = [&scope, &outcome](an::Status status) {
                outcome->status = status;
                scope->finish(status);
                return status;
            };
            const an::ui::ProcessSource source = an::ui::ProcessSource::of(image);
            const std::optional<an::ImageDescriptor> descriptor = source.descriptor();
            if (!descriptor.has_value())
            {
                return finish(an::Status::failure(an::ErrorCode::NotInitialized,
                                                  an::StatusDetail{.message = "the target holds nothing"}));
            }
            const std::optional<an::WcsSolution> solution =
                an::ui::processes::calibrationSolutionOf(source.metadata(), *descriptor);
            if (!solution.has_value())
            {
                return finish(an::Status::failure(
                    an::ErrorCode::InvalidState,
                    an::StatusDetail{.message = "Photometric color calibration requires a plate-solved image."}));
            }
            const an::Result<an::SkyCoordinate> center = solution->fieldCenter();
            const an::Result<double> radius = solution->queryRadiusDegrees(outcome->settings.matchRadiusArcseconds);
            if (!center.hasValue() || !radius.hasValue())
            {
                return finish(center.hasValue() ? radius.status() : center.status());
            }
            an::Result<an::ui::GaiaCatalogSource> catalog =
                an::ui::GaiaCatalogSource::open(scope->execution(), outcome->catalogDirectory);
            if (!catalog.hasValue())
            {
                return finish(catalog.status());
            }
            an::Result<an::ui::GaiaPhotometricRegion> region = catalog->regionWithColors(
                scope->execution(), *center, *radius, outcome->epochYear, outcome->settings.magnitudeLimit);
            if (!region.hasValue())
            {
                return finish(region.status());
            }

            an::Result<an::consumer::AnyImage> working =
                an::ui::processes::makeCalibrationWorkingImage(scope->engine(), scope->execution(), image.image());
            if (!working.hasValue())
            {
                return finish(working.status());
            }
            const an::PhotometricReferenceCatalog reference{.catalog = &region->catalog,
                                                            .colorIndexBpMinusRp = region->colorIndexBpMinusRp};
            const an::StarDetectionRequest detection = an::ui::processes::photometricStarDetection();
            const an::PhotometricColorCalibrationRequest request =
                an::ui::processes::photometricCalibrationRequest(outcome->settings, outcome->filterName);
            std::optional<an::PhotometricColorCalibrationResult> calibrated;
            const an::Status ran = an::ui::processes::calibrateWorkingImage(
                *working, image.validity(),
                [&](const an::StarFrameView& frame, void* destination, std::uint64_t bytes) -> an::Status {
                    an::Result<an::PhotometricColorCalibrationResult> result = an::calibratePhotometricColor(
                        scope->execution(), frame, destination, bytes, *solution, reference, detection, request);
                    if (!result.hasValue())
                    {
                        return result.status();
                    }
                    calibrated = std::move(*result);
                    return an::Status::success();
                });
            if (ran.failed())
            {
                return finish(ran);
            }

            an::Result<an::MetadataTransition> change =
                an::photometricCalibrationTransition(source.metadata().semantics(), *calibrated);
            if (!change.hasValue())
            {
                return finish(change.status());
            }
            an::ColorCalibrationRecord record;
            record.calibration = an::RecordedColorCalibration::Photometric;
            record.gains = calibrated->fit.gains;
            record.offsets = calibrated->offsets;
            record.catalogFamilyId = catalog->metadata().catalogName;
            record.algorithmVersion = an::kPhotometricColorCalibrationAlgorithmVersion;
            an::Result<an::ImageMetadata> metadata =
                an::ui::processes::calibratedMetadata(source.metadata(), *change, record);
            if (!metadata.hasValue())
            {
                return finish(metadata.status());
            }
            an::Result<an::consumer::ImageAsset> result = an::ui::processes::calibratedAsset(
                session, scope->engine(), scope->execution(), source, *working, std::move(*metadata));
            if (!result.hasValue())
            {
                return finish(result.status());
            }

            if (outcome->mask != nullptr)
            {
                // The mask weighs the result pixel by pixel; a mask that is the image itself is read through the edit's
                // own lease.
                std::optional<an::consumer::AssetReadLease> maskLease;
                const an::consumer::AnyImage* maskImage = &image.image();
                if (outcome->mask->identity() != image.assetIdentity())
                {
                    an::Result<an::consumer::AssetReadLease> read =
                        an::ui::readBeside(*scope, *outcome->mask, runStop.get_token());
                    if (!read.hasValue())
                    {
                        return finish(read.status());
                    }
                    maskLease = std::move(*read);
                    maskImage = &maskLease->image();
                }
                an::Result<an::ui::ProcessDestination> destination =
                    an::ui::ProcessDestination::copyOf(session, scope->engine(), scope->execution(), source);
                if (!destination.hasValue())
                {
                    return finish(destination.status());
                }
                if (an::Status replaced = destination->replace(std::move(*result)); replaced.failed())
                {
                    return finish(replaced);
                }
                const an::Status blended = an::ui::blendProcessResult(
                    scope->engine(), scope->execution(), source, *destination,
                    an::ui::ProcessBlendRequest{.mask = maskImage, .invertMask = outcome->invertMask});
                if (blended.failed())
                {
                    return finish(blended);
                }
                result = destination->release();
                if (!result.hasValue())
                {
                    return finish(result.status());
                }
            }

            outcome->gains = calibrated->fit.gains;
            outcome->matchedStars = static_cast<std::uint32_t>(calibrated->fit.usedSampleIndices.size());
            outcome->usedCatalogColorModel = calibrated->fit.usedCatalogColorModel;
            for (const std::uint32_t used : calibrated->fit.usedSampleIndices)
            {
                if (used < calibrated->samples.size())
                {
                    outcome->samples.push_back(calibrated->samples[used]);
                }
            }
            (void)finish(an::Status::success());
            return result;
        },
        [guard, outcome, inFlight, timer](bool shown) {
            if (!guard)
            {
                return;
            }
            if (shown)
            {
                LOG_INFO("Process '{}' executed successfully on image '{}'. Execution time was {:.2f} seconds",
                         guard->getProcessName(),
                         guard->m_image_view_control ? guard->m_image_view_control->getUniqueName() : std::string(),
                         static_cast<float>(timer.elapsed()) / 1000.0f);
            }
            guard->finalizeCalibration(*outcome, shown);
        },
        reads);
}

void PhotometricColorCalibrationProcess::finalizeCalibration(const RunOutcome& outcome, const bool shown)
{
    m_ExecutionInProgress = false;
    m_runStop.reset();
    const QString title =
        QCoreApplication::translate("PhotometricColorCalibrationProcess", "Photometric Color Calibration");
    const bool closing = m_ProcessCloseRequested.load();

    if (shown)
    {
        m_hasCalibrationResult = true;
        m_ui.warningBanner->setVisible(false);
        m_ui.actionButton->setVisible(false);
        const QString model = outcome.usedCatalogColorModel
                                  ? tr("Calibrated with the Gaia color model.")
                                  : tr("Calibrated with the neutral color default for most stars.");
        m_ui.statusLabel->setText(tr("%1 Matched stars: %2. Gains R=%3 G=1.000 B=%4")
                                      .arg(model)
                                      .arg(static_cast<qulonglong>(outcome.matchedStars))
                                      .arg(outcome.gains[0], 0, 'f', 3)
                                      .arg(outcome.gains[2], 0, 'f', 3));
        if (m_starOverlay)
        {
            m_starOverlay->setMatchedSamples(outcome.samples);
            m_starOverlay->enableDisplayModule(m_ui.showMatchedStars->isChecked());
        }
        if (!closing)
        {
            notifyInfoMessage(title, tr("Calibration completed successfully.\nMatched stars: %1\nGains: R=%2  G=1.000  "
                                        "B=%3")
                                         .arg(outcome.matchedStars)
                                         .arg(outcome.gains[0], 0, 'f', 3)
                                         .arg(outcome.gains[2], 0, 'f', 3));
        }
    }
    else
    {
        clearCalibrationResult(true);
        const bool cancelled = outcome.status.code() == an::ErrorCode::Cancelled || closing;
        if (outcome.status.ok() && !cancelled)
        {
            notifyWarnMessage(title, tr("Calibration finished, but its result was not applied: the target image was "
                                        "closed or changed, or its history could not record the step."));
        }
        else if (!cancelled)
        {
            const QString text = tr("An error occurred when executing the photometric color calibration process:\n%1")
                                     .arg(failureText(outcome.status));
            LOG_WARNING(text.toStdString());
            notifyErrorMessage(tr("Error running photometric color calibration"), text);
        }
    }

    updateAvailabilityState();
}

void PhotometricColorCalibrationProcess::onBackgroundRoiGroupBoxClicked(const bool checked)
{
    if (checked)
    {
        updateBackgroundRoiData();
        if (m_ui.backgroundRoiSelectionButton->isChecked())
        {
            destroyCropController();
            m_ui.backgroundRoiSelectionButton->setChecked(false);
        }
    }
    else
    {
        destroyCropController();
        m_ui.backgroundRoiSelectionButton->setChecked(false);
        resetBackgroundRoiValues();
    }

    updateAvailabilityState();
}

void PhotometricColorCalibrationProcess::onBackgroundRoiSelectionButtonClicked(const bool checked)
{
    if (!m_image_view_control)
    {
        m_ui.backgroundRoiSelectionButton->setChecked(false);
        return;
    }

    if (checked)
    {
        destroyCropController();
        resetBackgroundRoiValues();

        m_CropController = std::make_unique<ImageCropController>(m_image_view_control->getScrollArea(), this);
        m_CropController->setListenerEnabled(true);
        m_CropController->enableOneClickMove(true);
        m_CropController->startNewSelection();
        m_image_view_control->getViewWidget()->setImageViewSelectionStyle(
            ImageViewSelectionStyle::AREA_SELECTION_STYLE);
        updateSelectionArea();
    }
    else
    {
        destroyCropController();
        if (m_ui.backgroundRoiWidthValue->value() == 0 || m_ui.backgroundRoiHeightValue->value() == 0)
        {
            updateBackgroundRoiData();
        }
    }
}

void PhotometricColorCalibrationProcess::onBackgroundRoiLeftValueChanged(const int value)
{
    const std::optional<an::ImageDescriptor> descriptor =
        m_image_view_control ? m_image_view_control->imageDescriptor() : std::nullopt;
    if (!descriptor.has_value())
    {
        return;
    }

    m_ui.backgroundRoiWidthValue->setMaximum(static_cast<int>(descriptor->extent().width()) - value);
    updateSelectionArea();
}

void PhotometricColorCalibrationProcess::onBackgroundRoiTopValueChanged(const int value)
{
    const std::optional<an::ImageDescriptor> descriptor =
        m_image_view_control ? m_image_view_control->imageDescriptor() : std::nullopt;
    if (!descriptor.has_value())
    {
        return;
    }

    m_ui.backgroundRoiHeightValue->setMaximum(static_cast<int>(descriptor->extent().height()) - value);
    updateSelectionArea();
}

void PhotometricColorCalibrationProcess::onBackgroundRoiWidthValueChanged(const int value)
{
    Q_UNUSED(value);
    updateSelectionArea();
}

void PhotometricColorCalibrationProcess::onBackgroundRoiHeightValueChanged(const int value)
{
    Q_UNUSED(value);
    updateSelectionArea();
}

void PhotometricColorCalibrationProcess::clearCalibrationResult(const bool clearStatusLabel)
{
    m_hasCalibrationResult = false;

    if (m_starOverlay)
    {
        m_starOverlay->clearMatchedSamples();
    }

    if (clearStatusLabel)
    {
        m_ui.statusLabel->clear();
    }
}

void PhotometricColorCalibrationProcess::updateAvailabilityState()
{
    const bool busy = m_ExecutionInProgress.load();

    // A target a writer holds keeps what the window showed until the writer lets it go.
    const std::optional<TargetFacts> facts = targetFactsOf(m_image_view_control);
    if (!facts.has_value())
    {
        return;
    }

    bool ready = true;
    an::ui::AnInfoBanner::Tone bannerTone = an::ui::AnInfoBanner::Tone::Warning;
    QString bannerText;
    QString actionText;
    bool showAction = false;

    // Disconnect any previous action button connections
    QObject::disconnect(m_ui.actionButton, &QPushButton::clicked, this, nullptr);

    switch (facts->problem)
    {
    case CalibrationTargetProblem::NoImage:
    case CalibrationTargetProblem::NotRgb:
        ready = false;
        bannerTone = an::ui::AnInfoBanner::Tone::Error;
        bannerText = tr("Photometric color calibration only supports RGB images.");
        break;
    case CalibrationTargetProblem::Narrowband:
        ready = false;
        bannerText = tr("Photometric color calibration is not applicable to narrowband data.");
        break;
    case CalibrationTargetProblem::NotPlateSolved:
        ready = false;
        bannerText = tr("The image is not plate-solved. A valid WCS solution is required for photometric calibration.");
        actionText = tr("Plate Solve...");
        showAction = true;
        QObject::connect(m_ui.actionButton, &QPushButton::clicked, this,
                         &PhotometricColorCalibrationProcess::triggerPlateSolve);
        break;
    case CalibrationTargetProblem::DeclaredNonlinear:
        ready = false;
        bannerText = tr("Photometric color calibration requires linear data; this image is marked nonlinear.");
        break;
    case CalibrationTargetProblem::None: {
        // Check Gaia catalog availability from global settings
        auto* settings = process_utils::currentGlobalSettings();
        const auto catalogStatus = an::ui::inspectGaiaCatalogDirectory(an::ui::resolveGaiaCatalogDirectory(settings));
        if (catalogStatus != an::ui::GaiaCatalogDirectoryStatus::Ready)
        {
            ready = false;
            bannerText = tr("The Gaia DR3 catalog is not configured or not found. "
                            "Configure it in the Plate Solving settings page.");
            actionText = tr("Configure Gaia Catalog...");
            showAction = true;
            QObject::connect(m_ui.actionButton, &QPushButton::clicked, this,
                             &PhotometricColorCalibrationProcess::openPlateSolveSettings);
        }
        break;
    }
    }

    if (!bannerText.isEmpty())
    {
        m_ui.warningBanner->setTone(bannerTone);
        m_ui.warningBanner->setMessage(bannerText);
        m_ui.warningBanner->setVisible(true);
    }
    else
    {
        m_ui.warningBanner->setVisible(false);
    }

    if (showAction)
    {
        m_ui.actionButton->setText(actionText);
        m_ui.actionButton->setEnabled(!busy);
        m_ui.actionButton->setVisible(true);
    }
    else
    {
        m_ui.actionButton->setVisible(false);
    }

    if (!busy)
    {
        if (ready)
        {
            if (!m_hasCalibrationResult)
            {
                m_ui.statusLabel->setText(tr("Ready to calibrate photometric gains from Gaia-matched stars."));
            }
        }
        else
        {
            m_hasCalibrationResult = false;
            m_ui.statusLabel->clear();
        }
    }

    if (auto* apply = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        apply->setEnabled(ready && !busy);
    }
    if (auto* reset = m_ui.buttonBox->button(QDialogButtonBox::Reset))
    {
        reset->setEnabled(!busy);
    }
    if (auto* close = m_ui.buttonBox->button(QDialogButtonBox::Close))
    {
        close->setEnabled(true);
    }

    m_ui.targetImageSelector->setEnabled(!busy);
    m_ui.magnitudeLimitSpin->setEnabled(ready && !busy);
    m_ui.matchRadiusSpin->setEnabled(ready && !busy);
    m_ui.backgroundNeutralizationCheck->setEnabled(ready && !busy);
    m_ui.backgroundRoiGroupBox->setEnabled(ready && !busy && m_ui.backgroundNeutralizationCheck->isChecked());
    const bool roiControlsEnabled =
        ready && !busy && m_ui.backgroundNeutralizationCheck->isChecked() && m_ui.backgroundRoiGroupBox->isChecked();
    m_ui.backgroundRoiSelectionButton->setEnabled(roiControlsEnabled);
    m_ui.backgroundRoiLeftValue->setEnabled(roiControlsEnabled);
    m_ui.backgroundRoiTopValue->setEnabled(roiControlsEnabled);
    m_ui.backgroundRoiWidthValue->setEnabled(roiControlsEnabled);
    m_ui.backgroundRoiHeightValue->setEnabled(roiControlsEnabled);
    m_ui.showMatchedStars->setEnabled(ready && !busy);
}

void PhotometricColorCalibrationProcess::onShowMatchedStarsToggled(const bool checked)
{
    if (m_starOverlay)
    {
        m_starOverlay->enableDisplayModule(checked);
        if (auto* viewWidget = m_starOverlay->getViewWidget())
        {
            viewWidget->update();
        }
    }
}

void PhotometricColorCalibrationProcess::openPlateSolveSettings()
{
    process_utils::openSettingsPage(tr("Plate Solving"));
    updateAvailabilityState();
}

void PhotometricColorCalibrationProcess::triggerPlateSolve()
{
    if (m_image_view_control)
    {
        m_image_view_control->executePlateSolve();
    }
}

void PhotometricColorCalibrationProcess::attachOverlayToMainView()
{
    if (!m_starOverlay && m_image_view_control)
    {
        m_starOverlay = std::make_unique<PccMatchedStarOverlay>();
        m_starOverlay->attachToViewWidget(m_image_view_control->getViewWidget());
        m_starOverlay->enableDisplayModule(m_ui.showMatchedStars->isChecked());
    }
}

void PhotometricColorCalibrationProcess::detachOverlay()
{
    m_starOverlay.reset();
}

void PhotometricColorCalibrationProcess::updateBackgroundRoiData()
{
    const std::optional<an::ImageDescriptor> descriptor =
        m_image_view_control ? m_image_view_control->imageDescriptor() : std::nullopt;
    if (!descriptor.has_value())
    {
        return;
    }

    const int imageWidth = static_cast<int>(descriptor->extent().width());
    const int imageHeight = static_cast<int>(descriptor->extent().height());
    const bool preserveSelection = m_ui.backgroundRoiGroupBox->isChecked() &&
                                   m_ui.backgroundRoiWidthValue->value() > 0 &&
                                   m_ui.backgroundRoiHeightValue->value() > 0;

    const int left =
        preserveSelection ? std::clamp(m_ui.backgroundRoiLeftValue->value(), 0, (std::max)(0, imageWidth - 1)) : 0;
    const int top =
        preserveSelection ? std::clamp(m_ui.backgroundRoiTopValue->value(), 0, (std::max)(0, imageHeight - 1)) : 0;
    const int width = preserveSelection
                          ? std::clamp(m_ui.backgroundRoiWidthValue->value(), 1, (std::max)(1, imageWidth - left))
                          : imageWidth;
    const int height = preserveSelection
                           ? std::clamp(m_ui.backgroundRoiHeightValue->value(), 1, (std::max)(1, imageHeight - top))
                           : imageHeight;

    m_ui.backgroundRoiLeftValue->setMaximum((std::max)(0, imageWidth - 1));
    m_ui.backgroundRoiLeftValue->setValue(left);
    m_ui.backgroundRoiTopValue->setMaximum((std::max)(0, imageHeight - 1));
    m_ui.backgroundRoiTopValue->setValue(top);
    m_ui.backgroundRoiWidthValue->setMaximum(imageWidth - left);
    m_ui.backgroundRoiWidthValue->setValue(width);
    m_ui.backgroundRoiHeightValue->setMaximum(imageHeight - top);
    m_ui.backgroundRoiHeightValue->setValue(height);
}

void PhotometricColorCalibrationProcess::updateSelectionArea()
{
    if (!m_IgnoreLastCropMoveEvent && m_CropController)
    {
        m_CropController->updateSelectionArea(
            QRect(m_ui.backgroundRoiLeftValue->value(), m_ui.backgroundRoiTopValue->value(),
                  m_ui.backgroundRoiWidthValue->value(), m_ui.backgroundRoiHeightValue->value()));
    }
}

void PhotometricColorCalibrationProcess::destroyCropController()
{
    if (m_CropController)
    {
        m_CropController->destroy();
        m_CropController.reset();
    }
}

void PhotometricColorCalibrationProcess::resetBackgroundRoiValues()
{
    m_ui.backgroundRoiLeftValue->setValue(0);
    m_ui.backgroundRoiTopValue->setValue(0);
    m_ui.backgroundRoiWidthValue->setValue(0);
    m_ui.backgroundRoiHeightValue->setValue(0);
}

void PhotometricColorCalibrationProcess::closeEvent(QCloseEvent* event)
{
    // A run on its way is stopped; the framework hides the window and closes it when the run lands.
    if (m_runStop)
    {
        m_runStop->request_stop();
    }
    destroyCropController();
    detachOverlay();
    ImageProcessControl::closeEvent(event);
}
