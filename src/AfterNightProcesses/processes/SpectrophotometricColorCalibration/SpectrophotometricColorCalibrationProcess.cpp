// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "SpectrophotometricColorCalibrationProcess.h"

#include "app/AppContextHelpers.h"
#include "app/CatalogDownloadService.h"
#include "app/ObservationEpochUtils.h"
#include "app/PlateSolveUtils.h"
#include "app/SpccCatalogSettings.h"
#include "app/SpccResponseSettings.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessButtonIcons.h"
#include "controls/ProgressDialogControl.h"
#include "processes/PhotometricCalibrationSupport.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/SpectrophotometricColorCalibration/SpccCatalogSource.h"
#include "processes/SpectrophotometricColorCalibration/SpccHandoffSubsetSpectrophotometricCatalog.h"
#include "processes/SpectrophotometricColorCalibration/SpccResponseRepository.h"
#include "processes/SpectrophotometricColorCalibration/SpccRunInputs.h"
#include "session/ExecutionScope.h"

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricCalibrationTransition.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QAbstractButton>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>

#include <fmt/format.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace an::ui::processes
{

/// What one Apply's worker found, left for the window to show when the run lands.
struct SpccRunRecord
{
    std::mutex mutex;

    /// The summary of the last frame calibrated, or of the refusal.
    SpccRunSummary summary;

    /// Whether the run was stopped.
    bool cancelled = false;

    /// Whether a frame was calibrated.
    bool calibrated = false;

    /// What the hand-off found.
    QStringList handoffDiagnostics;

    /// How often the run read each input its frames share, when its last frame ended.
    SpccRunInputReads inputReads;
};

an::SpectrophotometricColorCalibrationRequest
spectrophotometricCalibrationRequest(const SpectrophotometricColorCalibrationParams& params, const QString& filterName,
                                     const QString& solutionIdentity, const std::optional<double>& epochYear)
{
    an::SpectrophotometricColorCalibrationRequest request;
    request.maximumStars =
        static_cast<std::uint32_t>(std::clamp(params.maxDetectedSources, 1, static_cast<int>(an::kMaxStarsPerFrame)));
    request.sourceSelection.minimumSignalToNoise = params.sourceSnr;
    request.sourceSelection.maximumSources = static_cast<std::uint32_t>((std::max)(params.maxDetectedSources, 1));
    request.preflight.matchRadiusArcseconds = params.matchRadiusArcsec;
    request.preflight.magnitudeLimit = params.magnitudeLimit;
    request.preflight.autoMagnitude.enabled = params.autoMagnitudeLimit;
    request.preflight.residualThresholdPixels = params.residualThresholdPx;
    request.preflight.residualThresholdArcseconds = params.residualThresholdArcsec;
    if (epochYear.has_value())
    {
        request.preflight.observationEpochYear = *epochYear;
    }
    request.preflight.observationEpoch = spccObservationEpochText(epochYear).toStdString();
    request.preflight.properMotionModelVersion = kSpccProperMotionModelVersion;
    request.preflight.wcsSolutionId = solutionIdentity.toStdString();
    request.samples.aperture.fullWidthRadiusScale = params.apertureFwhmScale;
    request.samples.aperture.annulusClipSigmas = params.annulusSigmaClip;
    request.fit.minimumSamples = static_cast<std::uint32_t>((std::max)(params.minimumSampleCount, 1));
    request.fit.lowSupportSamples =
        (std::max)(request.fit.minimumSamples, an::kDefaultSpectrophotometricLowSupportSamples);
    request.fit.outlierPolicy = params.fitOutlierPolicy;
    request.fit.clipSigmas = params.outlierSigma;
    request.fit.maximumIterations = static_cast<std::uint32_t>((std::max)(params.fitIterations, 1));
    request.background.neutralize = params.applyBackgroundNeutralization;
    request.filterName = filterName.toStdString();
    request.allowNarrowbandInput = params.narrowbandEnabled && kSpccProcessNarrowbandControlsEnabled;
    // Only an integer image's result is held to its range: a floating image keeps what the gains make, and an integer
    // one saturates as the working copy goes back to its representation.
    request.clipping = an::ColorCalibrationClipping::PreserveSignedFloat;
    return request;
}

an::StarDetectionRequest spectrophotometricStarDetection(const SpectrophotometricColorCalibrationParams& params)
{
    an::StarDetectionRequest request;
    request.maximumStars =
        static_cast<std::uint32_t>(std::clamp(params.maxDetectedSources, 1, static_cast<int>(an::kMaxStarsPerFrame)));
    return request;
}

} // namespace an::ui::processes

namespace
{

using an::ui::processes::SpccRunRecord;

[[nodiscard]] an::Status refusal(const an::ErrorCode code, const QString& message)
{
    return an::Status::failure(code, an::StatusDetail{.message = message.toStdString()});
}

[[nodiscard]] QString formatDownloadBytes(const qint64 bytes)
{
    constexpr double kib = 1024.0;
    constexpr double mib = kib * 1024.0;
    constexpr double gib = mib * 1024.0;
    const double value = static_cast<double>((std::max)(qint64{0}, bytes));
    if (value >= gib)
    {
        return QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "%1 GB")
            .arg(value / gib, 0, 'f', 1);
    }
    if (value >= mib)
    {
        return QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "%1 MB")
            .arg(value / mib, 0, 'f', 1);
    }
    if (value >= kib)
    {
        return QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "%1 KB")
            .arg(value / kib, 0, 'f', 1);
    }
    return QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "%1 B").arg(bytes);
}

[[nodiscard]] std::optional<an::ui::SpccResponseRepositoryDescriptor> preferredManagedSpccResponseRepository()
{
    const QVector<an::ui::SpccResponseRepositoryDescriptor> repositories = an::ui::availableSpccResponseRepositories();
    const auto usable = [](const an::ui::SpccResponseRepositoryDescriptor& repository) {
        return repository.manifestUrl.isValid() &&
               an::ui::isApprovedSpccResponseDescriptorRightsStatus(repository.rightsReviewStatus) &&
               an::ui::isSupportedSpccResponseDeploymentScope(repository.deploymentScope);
    };
    auto it = std::find_if(repositories.begin(), repositories.end(),
                           [&usable](const auto& repository) { return repository.recommended && usable(repository); });
    if (it == repositories.end())
    {
        it = std::find_if(repositories.begin(), repositories.end(), usable);
    }
    return it == repositories.end() ? std::nullopt : std::optional<an::ui::SpccResponseRepositoryDescriptor>(*it);
}

[[nodiscard]] QString managedCatalogRootForInstallDirectory(const QString& installDirectory)
{
    const QString normalized = installDirectory.trimmed();
    if (normalized.isEmpty())
    {
        return an::ui::defaultSpccCatalogDirectory();
    }
    return QFileInfo(normalized).dir().absolutePath();
}

/// What the window reads of its target on the GUI thread, under a lease that never waits.
struct TargetSnapshot
{
    std::optional<an::ImageDescriptor> descriptor;
    std::optional<an::ImageMetadata> metadata;
    std::optional<an::consumer::AssetRevision> revision;
};

[[nodiscard]] std::optional<TargetSnapshot> targetSnapshotOf(const ImageViewControl* view)
{
    TargetSnapshot snapshot;
    if (view == nullptr || !view->hasImage())
    {
        return snapshot;
    }
    an::Result<an::consumer::AssetReadLease> lease = view->document().tryRead();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    snapshot.descriptor = view->document().descriptor();
    snapshot.metadata =
        std::visit([](const auto& typed) -> an::ImageMetadata { return typed.metadata(); }, lease->image());
    snapshot.revision = lease->revision();
    return snapshot;
}

/// Calibrates a working image against one catalog: the plan's frame, the solution, the detection.
[[nodiscard]] an::Result<an::SpectrophotometricColorCalibrationResult>
calibrateAgainst(an::ui::ExecutionScope& scope, an::consumer::AnyImage& working, const an::ValidityMask* mask,
                 const an::SpectrophotometricColorCalibrationPlan& plan, const an::WcsSolution& solution,
                 const an::SpectrumCatalog& catalog, const an::StarDetectionRequest& detection)
{
    std::optional<an::SpectrophotometricColorCalibrationResult> calibrated;
    const an::Status ran = an::ui::processes::calibrateWorkingImage(
        working, mask, [&](const an::StarFrameView& frame, void* destination, std::uint64_t bytes) -> an::Status {
            an::Result<an::SpectrophotometricColorCalibrationResult> result = an::calibrateSpectrophotometricColor(
                scope.execution(), plan, frame, destination, bytes, solution, catalog, detection);
            if (!result.hasValue())
            {
                return result.status();
            }
            calibrated = std::move(*result);
            return an::Status::success();
        });
    if (ran.failed())
    {
        return ran;
    }
    return std::move(*calibrated);
}

} // namespace

SpectrophotometricColorCalibrationProcess::SpectrophotometricColorCalibrationProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<SpectrophotometricColorCalibrationParams>(control, getProcessNameStatic())
{
    setFixedSizeWindow(true);
    setWindowSizeFitMultipliers(1.35, 1.0);

    m_ui = an::ui::processes::buildSpectrophotometricColorCalibrationProcessUi(this);
    setProcessDescription(
        QStringLiteral("Calibrate linear RGB images from Gaia XP sampled spectra and response profiles."));
    m_spccCatalogDownloadService = new an::ui::CatalogDownloadService(this);
    m_SupportMasking = true;

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &SpectrophotometricColorCalibrationProcess::onDialogButtonClicked);
    QObject::connect(m_spccCatalogDownloadService, &an::ui::CatalogDownloadService::downloadProgress, this,
                     &SpectrophotometricColorCalibrationProcess::applyDownloadProgress);
    if (m_ui.narrowbandModeCheck)
    {
        QObject::connect(m_ui.narrowbandModeCheck, &QCheckBox::toggled, this,
                         [this](bool) { updateAvailabilityState(); });
    }
    if (m_ui.autoMagnitudeLimitCheck)
    {
        QObject::connect(m_ui.autoMagnitudeLimitCheck, &QCheckBox::toggled, this,
                         [this](bool) { updateAvailabilityState(); });
    }
    if (m_ui.matchRadiusSpin)
    {
        QObject::connect(m_ui.matchRadiusSpin, &QDoubleSpinBox::valueChanged, this,
                         [this](double) { updateAvailabilityState(); });
    }
    if (m_ui.readinessPanel)
    {
        m_ui.readinessPanel->setOpenSettingsPageCallback(
            [this](const QString& pageName) { openSettingsPageAndRefresh(pageName); });
        m_ui.readinessPanel->setManualResponseProfileSelectedCallback([this](const QString& profileId) {
            m_manualResponseProfileId = profileId;
            saveManualResponseProfileMapping(profileId, m_manualWhiteReferenceId);
            updateAvailabilityState();
        });
        m_ui.readinessPanel->setManualWhiteReferenceSelectedCallback([this](const QString& whiteReferenceId) {
            m_manualWhiteReferenceId = whiteReferenceId;
            const QString profileId = m_manualResponseProfileId.trimmed().isEmpty()
                                          ? m_ui.readinessPanel->report().responseProfileId
                                          : m_manualResponseProfileId;
            saveManualResponseProfileMapping(profileId, whiteReferenceId);
            updateAvailabilityState();
        });
        m_ui.readinessPanel->setDownloadFullCatalogCallback([this]() { requestCatalogDownload(); });
        m_ui.readinessPanel->setDownloadNeededRegionCallback([this]() { requestNeededRegionCatalogDownload(); });
        m_ui.readinessPanel->setDownloadResponseProfilesCallback([this]() { requestResponseProfileDownload(); });
        m_ui.readinessPanel->setAssignImageStateCallback([this]() { assignCurrentImageStateFromUserAction(); });
    }

    m_SupportTargetViewControlSelection = true;
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    updateWindowTitle();
    updateAvailabilityState();
    refreshTheme();
}

SpectrophotometricColorCalibrationProcess::~SpectrophotometricColorCalibrationProcess()
{
    m_readinessStop.request_stop();
}

std::string SpectrophotometricColorCalibrationProcess::getProcessNameStatic()
{
    return "Spectrophotometric Color Calibration";
}

bool SpectrophotometricColorCalibrationProcess::requiresExclusiveAccessStatic()
{
    return true;
}

bool SpectrophotometricColorCalibrationProcess::isRTPreviewProcess() const
{
    return false;
}

bool SpectrophotometricColorCalibrationProcess::isPreviewEnabled() const
{
    return false;
}

void SpectrophotometricColorCalibrationProcess::enablePreview(bool enabled)
{
    (void)enabled;
}

std::uint64_t SpectrophotometricColorCalibrationProcess::readinessReportsShown() const noexcept
{
    return m_readinessShown;
}

bool SpectrophotometricColorCalibrationProcess::readinessPending() const noexcept
{
    return m_readinessLanded < m_readinessRequests;
}

an::ui::processes::SpccRunInputReads SpectrophotometricColorCalibrationProcess::lastRunInputReads() const
{
    return m_lastRunInputReads;
}

void SpectrophotometricColorCalibrationProcess::clearPostExecutionState()
{
    m_lastCalibratedRevision.reset();
    m_lastSuccessfulCalibrationStatusText.clear();
}

bool SpectrophotometricColorCalibrationProcess::postExecutionSuccessAppliesToCurrentImage() const
{
    if (!m_lastCalibratedRevision.has_value() || m_image_view_control == nullptr || !m_image_view_control->hasImage())
    {
        return false;
    }
    const std::shared_ptr<const an::consumer::ImageAsset> asset = m_image_view_control->document().asset();
    return asset != nullptr && asset->revision() == *m_lastCalibratedRevision;
}

void SpectrophotometricColorCalibrationProcess::showIndeterminateProgress()
{
    if (!m_ui.progressBar)
    {
        return;
    }
    m_ui.progressBar->setVisible(true);
    m_ui.progressBar->setRange(0, 0);
    m_ui.progressBar->setValue(0);
}

void SpectrophotometricColorCalibrationProcess::resetOperationProgress()
{
    if (!m_ui.progressBar)
    {
        return;
    }
    m_ui.progressBar->setRange(0, 100);
    m_ui.progressBar->setValue(0);
    m_ui.progressBar->setVisible(false);
}

void SpectrophotometricColorCalibrationProcess::applyDownloadProgress(const an::ui::CatalogDownloadProgress& progress)
{
    if (m_ui.progressBar)
    {
        m_ui.progressBar->setVisible(true);
        if (progress.bytesTotal > 0)
        {
            m_ui.progressBar->setRange(0, 100);
            const int percent = static_cast<int>((100.0 * static_cast<double>(progress.bytesReceived)) /
                                                 static_cast<double>(progress.bytesTotal));
            m_ui.progressBar->setValue(std::clamp(percent, 0, 100));
        }
        else
        {
            m_ui.progressBar->setRange(0, 0);
        }
    }

    if (!m_ui.statusLabel)
    {
        return;
    }

    if (progress.phase == QStringLiteral("download") || progress.phase == QStringLiteral("verify"))
    {
        const QString bytesText = progress.bytesTotal > 0
                                      ? tr("%1 / %2").arg(formatDownloadBytes(progress.bytesReceived),
                                                          formatDownloadBytes(progress.bytesTotal))
                                      : QString();
        const QString fileText =
            progress.currentFile.trimmed().isEmpty() ? QString() : tr(" (%1)").arg(progress.currentFile);

        if (m_spccResponseDownloadInProgress)
        {
            m_ui.statusLabel->setText(tr("Downloading SPCC response profiles in parallel... %1/%2%3%4")
                                          .arg(progress.filesCompleted)
                                          .arg(progress.filesTotal)
                                          .arg(fileText)
                                          .arg(bytesText.isEmpty() ? QString() : tr(" - %1").arg(bytesText)));
            return;
        }

        const QString verb = progress.phase == QStringLiteral("verify") ? tr("Verifying") : tr("Downloading");
        m_ui.statusLabel->setText(tr("%1 SPCC catalog chunks in parallel... %2/%3%4%5")
                                      .arg(verb)
                                      .arg(progress.filesCompleted)
                                      .arg(progress.filesTotal)
                                      .arg(fileText)
                                      .arg(bytesText.isEmpty() ? QString() : tr(" - %1").arg(bytesText)));
        return;
    }

    m_ui.statusLabel->setText(m_spccResponseDownloadInProgress ? tr("Fetching SPCC response profile metadata...")
                                                               : tr("Fetching SPCC catalog metadata..."));
}

void SpectrophotometricColorCalibrationProcess::settingsUpdatedImpl()
{
    updateAvailabilityState();
}

SpectrophotometricColorCalibrationParams SpectrophotometricColorCalibrationProcess::getParams() const
{
    SpectrophotometricColorCalibrationParams params;
    params.autoMagnitudeLimit =
        m_ui.autoMagnitudeLimitCheck ? m_ui.autoMagnitudeLimitCheck->isChecked() : params.autoMagnitudeLimit;
    params.magnitudeLimit = m_ui.magnitudeLimitSpin ? m_ui.magnitudeLimitSpin->value() : params.magnitudeLimit;
    params.matchRadiusArcsec = m_ui.matchRadiusSpin ? m_ui.matchRadiusSpin->value() : params.matchRadiusArcsec;
    params.maxDetectedSources = m_ui.maxSourcesSpin ? m_ui.maxSourcesSpin->value() : params.maxDetectedSources;
    params.sourceSnr = m_ui.sourceSnrSpin ? m_ui.sourceSnrSpin->value() : params.sourceSnr;
    params.residualThresholdPx = m_ui.residualPxSpin ? m_ui.residualPxSpin->value() : params.residualThresholdPx;
    params.residualThresholdArcsec =
        m_ui.residualArcsecSpin ? m_ui.residualArcsecSpin->value() : params.residualThresholdArcsec;
    params.minimumSampleCount = m_ui.minimumSamplesSpin ? m_ui.minimumSamplesSpin->value() : params.minimumSampleCount;
    params.apertureFwhmScale = m_ui.apertureScaleSpin ? m_ui.apertureScaleSpin->value() : params.apertureFwhmScale;
    params.annulusSigmaClip = m_ui.annulusSigmaClipSpin ? m_ui.annulusSigmaClipSpin->value() : params.annulusSigmaClip;
    if (m_ui.fitOutlierPolicyCombo)
    {
        const auto policy =
            static_cast<an::SpectrophotometricOutlierPolicy>(m_ui.fitOutlierPolicyCombo->currentData().toInt());
        if (an::isSpectrophotometricOutlierPolicy(policy))
        {
            params.fitOutlierPolicy = policy;
        }
    }
    params.outlierSigma = m_ui.outlierSigmaSpin ? m_ui.outlierSigmaSpin->value() : params.outlierSigma;
    params.fitIterations = m_ui.fitIterationsSpin ? m_ui.fitIterationsSpin->value() : params.fitIterations;
    params.applyBackgroundNeutralization = m_ui.backgroundNeutralizationCheck
                                               ? m_ui.backgroundNeutralizationCheck->isChecked()
                                               : params.applyBackgroundNeutralization;
    params.atmosphereEnabled =
        m_ui.atmosphereCorrectionCheck ? m_ui.atmosphereCorrectionCheck->isChecked() : params.atmosphereEnabled;
    params.observerHeightM = m_ui.observerHeightSpin ? m_ui.observerHeightSpin->value() : params.observerHeightM;
    params.pressureHpa = m_ui.pressureSpin ? m_ui.pressureSpin->value() : params.pressureHpa;
    params.pressureIsSeaLevel =
        m_ui.pressureSeaLevelCheck ? m_ui.pressureSeaLevelCheck->isChecked() : params.pressureIsSeaLevel;
    params.airmass = m_ui.airmassSpin ? m_ui.airmassSpin->value() : params.airmass;
    params.narrowbandEnabled =
        m_ui.narrowbandModeCheck ? m_ui.narrowbandModeCheck->isChecked() : params.narrowbandEnabled;
    params.narrowbandRedCenterNm =
        m_ui.narrowbandRedCenterSpin ? m_ui.narrowbandRedCenterSpin->value() : params.narrowbandRedCenterNm;
    params.narrowbandRedBandwidthNm =
        m_ui.narrowbandRedBandwidthSpin ? m_ui.narrowbandRedBandwidthSpin->value() : params.narrowbandRedBandwidthNm;
    params.narrowbandGreenCenterNm =
        m_ui.narrowbandGreenCenterSpin ? m_ui.narrowbandGreenCenterSpin->value() : params.narrowbandGreenCenterNm;
    params.narrowbandGreenBandwidthNm = m_ui.narrowbandGreenBandwidthSpin ? m_ui.narrowbandGreenBandwidthSpin->value()
                                                                          : params.narrowbandGreenBandwidthNm;
    params.narrowbandBlueCenterNm =
        m_ui.narrowbandBlueCenterSpin ? m_ui.narrowbandBlueCenterSpin->value() : params.narrowbandBlueCenterNm;
    params.narrowbandBlueBandwidthNm =
        m_ui.narrowbandBlueBandwidthSpin ? m_ui.narrowbandBlueBandwidthSpin->value() : params.narrowbandBlueBandwidthNm;
    if constexpr (!an::ui::processes::kSpccProcessAtmosphericCorrectionControlsEnabled)
    {
        params.atmosphereEnabled = false;
    }
    if constexpr (!an::ui::processes::kSpccProcessNarrowbandControlsEnabled)
    {
        params.narrowbandEnabled = false;
    }

    if (m_ui.readinessPanel)
    {
        const an::ui::processes::SpccProcessReadinessReport& report = m_ui.readinessPanel->report();
        params.catalogDirectory =
            m_downloadedCatalogDirectory.isEmpty() ? report.catalogDirectory : m_downloadedCatalogDirectory;
        params.responseRepositoryDirectory = report.responseRepositoryDirectory;
        params.responseProfileId = report.responseProfileId;
        params.whiteReferenceId = report.whiteReferenceId;
    }
    params.targetPath = targetFilePath();
    params.record = m_runRecord;
    params.inputs = std::make_shared<an::ui::processes::SpccRunInputs>();
    return params;
}

an::ui::ProcessStep SpectrophotometricColorCalibrationProcess::describeTypedProcessStep(
    const SpectrophotometricColorCalibrationParams& params) const
{
    return an::ui::ProcessStep{
        .name = getProcessName(),
        .description = fmt::format("Response profile: {}, White reference: {}, Background neutralization: {}",
                                   params.responseProfileId.toStdString(), params.whiteReferenceId.toStdString(),
                                   params.applyBackgroundNeutralization ? "on" : "off")};
}

an::Status SpectrophotometricColorCalibrationProcess::executeTypedProcessImpl(
    const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination, const an::ui::ProcessRun& run,
    const SpectrophotometricColorCalibrationParams& params, an::ui::ExecutionScope& scope)
{
    (void)run;
    const std::shared_ptr<SpccRunRecord> record = params.record ? params.record : std::make_shared<SpccRunRecord>();
    an::ui::processes::SpccRunSummary summary;
    const auto refuse = [&record, &summary](an::Status status) {
        summary.message = QString::fromUtf8(status.message());
        const std::lock_guard lock(record->mutex);
        record->summary = summary;
        record->cancelled = status.code() == an::ErrorCode::Cancelled;
        return status;
    };

    const std::optional<an::ImageDescriptor> descriptor = source.descriptor();
    if (!descriptor.has_value() || !an::ui::processes::isCalibrationRgb(*descriptor))
    {
        return refuse(refusal(an::ErrorCode::Unsupported, tr("SPCC only supports RGB images.")));
    }
    const an::ImageMetadata& metadata = source.metadata();
    const std::optional<an::WcsSolution> solution = an::ui::processes::calibrationSolutionOf(metadata, *descriptor);
    if (!solution.has_value())
    {
        return refuse(refusal(an::ErrorCode::InvalidState, tr("SPCC requires a plate-solved image.")));
    }
    // The readiness judged the image the view shows; every frame of a sequence is judged here.
    if (const an::SpectrophotometricInputProblem problem =
            an::spectrophotometricInputProblem(an::ui::processes::declaredCalibrationState(*descriptor, metadata));
        problem != an::SpectrophotometricInputProblem::None)
    {
        return refuse(refusal(an::ErrorCode::InvalidState,
                              tr("SPCC input image state is not compatible: %1.")
                                  .arg(QString::fromLatin1(an::spectrophotometricInputProblemName(problem)))));
    }
    const an::Result<an::SkyCoordinate> center = solution->fieldCenter();
    const an::Result<double> radius = solution->queryRadiusDegrees(params.matchRadiusArcsec);
    if (!center.hasValue() || !radius.hasValue())
    {
        return refuse(center.hasValue() ? radius.status() : center.status());
    }
    const std::optional<double> epochYear = an::ui::resolveObservationEpochYear(metadata.acquisition());
    const QString solutionIdentity = an::ui::processes::spccSolutionIdentity(*solution);
    const QString filterName = an::ui::processes::calibrationFilterName(metadata);
    const an::ExecutionContext& execution = scope.execution();
    // What the run reads once for all its frames; parameters made without it read everything for this frame alone.
    const std::shared_ptr<an::ui::processes::SpccRunInputs> inputs =
        params.inputs ? params.inputs : std::make_shared<an::ui::processes::SpccRunInputs>();

    // The catalog: its checksums verified, the field's chunks read, and its readiness held to the engine's rule.
    const an::ui::SpccCatalogDirectoryInspection inspection = inputs->verifiedCatalog(params.catalogDirectory);
    const bool checksumsValid = (inspection.status == an::ui::SpccCatalogDirectoryStatus::Ready ||
                                 inspection.status == an::ui::SpccCatalogDirectoryStatus::PartialCache) &&
                                inspection.checksumsValid;
    an::Result<std::shared_ptr<const an::SpectrumCatalogManifest>> manifest =
        inputs->manifest(execution, params.catalogDirectory);
    if (!manifest.hasValue())
    {
        return refuse(manifest.status());
    }
    const an::ui::processes::SpccIntegrationHandoff handoff = inputs->handoff(params.targetPath);
    std::vector<std::uint64_t> handoffSources;
    for (const an::MatchedSourceReference& reference : handoff.references)
    {
        if (reference.sourceIdentifier != 0u)
        {
            handoffSources.push_back(reference.sourceIdentifier);
        }
    }
    an::Result<std::shared_ptr<const an::ui::processes::SpccCatalogRegion>> region =
        inputs->region(execution, params.catalogDirectory, **manifest, *center, *radius, handoffSources);
    if (!region.hasValue())
    {
        summary.catalogCoverageFailure = region.status().code() == an::ErrorCode::EmptySelection;
        return refuse(region.status());
    }
    const an::SpectrumCatalogReadiness readiness =
        an::makeSpectrumCatalogReadiness(**manifest, checksumsValid, (*region)->selection.declaredCoverageComplete());
    if (const an::SpectrumCatalogReadinessError problem = an::checkSpectrumCatalogReadiness(readiness);
        problem != an::SpectrumCatalogReadinessError::None)
    {
        summary.catalogCoverageFailure = problem == an::SpectrumCatalogReadinessError::RegionNotCovered;
        return refuse(refusal(an::ErrorCode::InvalidState,
                              tr("SPCC catalog readiness rejected the installed catalog: %1.")
                                  .arg(QString::fromLatin1(an::spectrumCatalogReadinessErrorName(problem)))));
    }
    an::Result<std::shared_ptr<const an::SpectrumCatalog>> catalog = inputs->catalog(execution, *region);
    if (!catalog.hasValue())
    {
        return refuse(catalog.status());
    }

    // The recipe: the response repository's profile and white reference, planned on the catalog's grid.
    an::Result<std::shared_ptr<const an::ui::processes::SpccResponseRepository>> repository =
        inputs->repository(execution, params.responseRepositoryDirectory);
    if (!repository.hasValue())
    {
        return refuse(repository.status());
    }
    an::Result<an::ui::processes::SpccRepositoryProfile> profile =
        (*repository)->resolveProfile(params.responseProfileId);
    if (!profile.hasValue())
    {
        return refuse(profile.status());
    }
    const QString whiteReferenceId =
        params.whiteReferenceId.trimmed().isEmpty() ? profile->whiteReferenceId : params.whiteReferenceId.trimmed();
    const an::ui::processes::SpccRepositoryWhiteReference* white = (*repository)->findWhiteReference(whiteReferenceId);
    if (white == nullptr)
    {
        return refuse(
            refusal(an::ErrorCode::InvalidArgument, tr("The selected SPCC white reference is not available.")));
    }
    an::Result<an::SpectrophotometricColorCalibrationPlan> plan = an::planSpectrophotometricColorCalibration(
        execution, profile->profile, white->spectrum, an::ObservationModel{}, **manifest,
        an::ui::processes::spectrophotometricCalibrationRequest(params, filterName, solutionIdentity, epochYear));
    if (!plan.hasValue())
    {
        return refuse(plan.status());
    }

    an::Result<an::consumer::AnyImage> working =
        an::ui::processes::makeCalibrationWorkingImage(scope.engine(), execution, source.image());
    if (!working.hasValue())
    {
        return refuse(working.status());
    }
    const an::StarDetectionRequest detection = an::ui::processes::spectrophotometricStarDetection(params);

    // The ImageIntegration hand-off's sources, when enough of them still hold, and the whole field otherwise.
    QStringList diagnostics = handoff.diagnostics;
    std::optional<an::Result<an::SpectrophotometricColorCalibrationResult>> calibrated;
    if (!handoff.references.empty())
    {
        an::Result<an::MatchedSourceResolution> resolution =
            an::resolveMatchedSources(execution, handoff.references, **catalog,
                                      an::ui::processes::spccHandoffRequirements(solutionIdentity, epochYear));
        if (!resolution.hasValue())
        {
            return refuse(resolution.status());
        }
        diagnostics << an::ui::processes::spccHandoffResolutionText(handoff, *resolution, **manifest);
        const std::vector<std::uint64_t> accepted = an::ui::processes::acceptedHandoffSources(*resolution, **catalog);
        if (!accepted.empty() && accepted.size() >= plan->request.fit.minimumSamples)
        {
            diagnostics << tr("ImageIntegration handoff: using %1 validated Gaia source_id references as the SPCC "
                              "catalog source set.")
                               .arg(static_cast<qulonglong>(accepted.size()));
            an::Result<an::SpectrumCatalog> subset =
                an::ui::processes::buildSpccSubsetCatalog(execution, **region, accepted);
            if (!subset.hasValue())
            {
                return refuse(subset.status());
            }
            calibrated = calibrateAgainst(scope, *working, source.validity(), *plan, *solution, *subset, detection);
            if (!calibrated->hasValue() && !calibrated->status().isCancellation() && handoff.fallbackRegionQueryAllowed)
            {
                diagnostics << tr("ImageIntegration handoff: constrained source matching failed (%1); running the "
                                  "explicit normal WCS region-query fallback.")
                                   .arg(QString::fromUtf8(calibrated->status().message()));
                calibrated.reset();
                // The constrained run may have written part of the working copy; the whole field starts afresh.
                an::Result<an::consumer::AnyImage> fresh =
                    an::ui::processes::makeCalibrationWorkingImage(scope.engine(), execution, source.image());
                if (!fresh.hasValue())
                {
                    return refuse(fresh.status());
                }
                *working = std::move(*fresh);
            }
        }
        else
        {
            diagnostics << tr("ImageIntegration handoff: only %1 validated source_id references were available but "
                              "%2 samples are required; running the normal WCS region-query fallback.")
                               .arg(static_cast<qulonglong>(accepted.size()))
                               .arg(static_cast<qulonglong>(plan->request.fit.minimumSamples));
        }
    }
    if (!calibrated.has_value())
    {
        calibrated = calibrateAgainst(scope, *working, source.validity(), *plan, *solution, **catalog, detection);
    }
    summary.workflowDiagnostics = diagnostics;
    if (!calibrated->hasValue())
    {
        return refuse(calibrated->status());
    }
    const an::SpectrophotometricColorCalibrationResult& result = **calibrated;
    an::ui::processes::SpccRunSummary resultSummary = an::ui::processes::spccRunSummary(result);
    resultSummary.workflowDiagnostics = diagnostics + resultSummary.workflowDiagnostics;
    summary = resultSummary;

    // What the result means: the engine's transition and the colour-calibration cards.
    an::Result<an::MetadataTransition> change = an::spectrophotometricCalibrationTransition(
        metadata.semantics(), plan->identity, result.fit.gains, result.applied.offsets);
    if (!change.hasValue())
    {
        return refuse(change.status());
    }
    an::ColorCalibrationRecord calibration;
    calibration.calibration = an::RecordedColorCalibration::Spectrophotometric;
    calibration.gains = result.fit.gains;
    calibration.offsets = result.applied.offsets;
    calibration.responseProfileId = plan->identity.responseProfileId;
    calibration.responseProfileVersion = plan->identity.responseProfileVersion;
    calibration.whiteReferenceId = plan->identity.whiteReferenceId;
    calibration.whiteReferenceVersion = plan->identity.whiteReferenceVersion;
    calibration.catalogFamilyId = plan->identity.catalogFamilyId;
    calibration.catalogManifestId = plan->identity.catalogManifestId;
    calibration.catalogAccessProfileId = inspection.accessProfile.toStdString();
    calibration.algorithmVersion = an::kSpectrophotometricColorCalibrationAlgorithmVersion;
    an::Result<an::ImageMetadata> calibratedMetadata =
        an::ui::processes::calibratedMetadata(metadata, *change, calibration);
    if (!calibratedMetadata.hasValue())
    {
        return refuse(calibratedMetadata.status());
    }
    an::Result<an::consumer::ImageAsset> asset = an::ui::processes::calibratedAsset(
        scope.session(), scope.engine(), execution, source, *working, std::move(*calibratedMetadata));
    if (!asset.hasValue())
    {
        return refuse(asset.status());
    }
    if (an::Status replaced = destination.replace(std::move(*asset)); replaced.failed())
    {
        return refuse(replaced);
    }
    {
        const std::lock_guard lock(record->mutex);
        record->summary = summary;
        record->calibrated = true;
        record->inputReads = inputs->reads();
    }
    LOG_INFO("Process '{}' calibrated a frame. Samples: {}; gains R={:.3f} G={:.3f} B={:.3f}", getProcessName(),
             summary.acceptedSamples, summary.gains[0], summary.gains[1], summary.gains[2]);
    return an::Status::success();
}

void SpectrophotometricColorCalibrationProcess::onImageLoaded(ImageViewControl* ctrl)
{
    if (ctrl == m_image_view_control && !isViewControlSupported(ctrl))
    {
        close();
        return;
    }
    if (ctrl == m_image_view_control && !postExecutionSuccessAppliesToCurrentImage())
    {
        clearPostExecutionState();
    }
    ImageRTPreviewControl::onImageLoaded(ctrl);
    updateAvailabilityState();
}

void SpectrophotometricColorCalibrationProcess::onImageUpdated(ImageViewControl* ctrl)
{
    if (ctrl == m_image_view_control && !isViewControlSupported(ctrl))
    {
        close();
        return;
    }
    if (ctrl == m_image_view_control && !postExecutionSuccessAppliesToCurrentImage())
    {
        clearPostExecutionState();
    }
    ImageRTPreviewControl::onImageUpdated(ctrl);
    updateAvailabilityState();
}

void SpectrophotometricColorCalibrationProcess::onViewControlChanged(ImageViewControl* oldCtrl,
                                                                     ImageViewControl* newCtrl)
{
    if (!isViewControlSupported(newCtrl))
    {
        close();
        return;
    }
    clearPostExecutionState();
    ImageRTPreviewControl::onViewControlChanged(oldCtrl, newCtrl);
    updateAvailabilityState();
}

void SpectrophotometricColorCalibrationProcess::onImageViewControlDestroyed(ImageViewControl* ctrl)
{
    if (ctrl == m_image_view_control)
    {
        clearPostExecutionState();
        cancelActiveWork();
    }
    ImageRTPreviewControl::onImageViewControlDestroyed(ctrl);
}

void SpectrophotometricColorCalibrationProcess::refreshTheme()
{
    ImageRTPreviewControl::refreshTheme();
    if (auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
    if (m_ui.readinessPanel)
    {
        m_ui.readinessPanel->refreshTheme();
    }
}

bool SpectrophotometricColorCalibrationProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    if (ctrl == nullptr || !ctrl->hasImage())
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> descriptor = ctrl->imageDescriptor();
    return descriptor.has_value() && an::ui::processes::isCalibrationRgb(*descriptor);
}

void SpectrophotometricColorCalibrationProcess::cancelActiveWork()
{
    cancelCurrentProgressDialog();
    if (m_spccCatalogDownloadService && m_spccCatalogDownloadService->isBusy())
    {
        m_spccCatalogDownloadService->cancel();
    }
}

void SpectrophotometricColorCalibrationProcess::closeEvent(QCloseEvent* event)
{
    // Downloads and a run on their way are stopped; the framework hides the window and closes it when the run lands.
    cancelActiveWork();
    ImageRTPreviewControl::closeEvent(event);
}

void SpectrophotometricColorCalibrationProcess::onDialogButtonClicked(QAbstractButton* button)
{
    if (!m_ui.buttonBox)
    {
        return;
    }

    const QDialogButtonBox::StandardButton standardButton = m_ui.buttonBox->standardButton(button);
    if (standardButton == QDialogButtonBox::Apply)
    {
        executeCalibration();
    }
    else if (standardButton == QDialogButtonBox::Reset)
    {
        restoreDefaults();
    }
    else if (standardButton == QDialogButtonBox::Close || standardButton == QDialogButtonBox::Cancel)
    {
        close();
    }
}

void SpectrophotometricColorCalibrationProcess::executeCalibration()
{
    if (m_ExecutionInProgress || m_calibrationPreparing || readinessPending() || !m_ui.readinessPanel)
    {
        return;
    }

    const QString title = QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                                      "Spectrophotometric Color Calibration");
    const an::ui::processes::SpccProcessReadinessReport& report = m_ui.readinessPanel->report();
    QString errorMessage;
    if (!m_image_view_control || !m_image_view_control->hasImage())
    {
        errorMessage = tr("No target image is available.");
    }
    else if (!isViewControlSupported(m_image_view_control))
    {
        errorMessage = tr("SPCC only supports RGB images.");
    }
    else if (!m_image_view_control->imageSolution().has_value())
    {
        errorMessage = tr("SPCC requires a plate-solved image.");
    }
    else if (!report.canExecute())
    {
        errorMessage = report.summary.isEmpty() ? tr("SPCC prerequisites are not ready.") : report.summary;
    }
    if (!errorMessage.isEmpty())
    {
        notifyErrorMessage(title, errorMessage);
        updateAvailabilityState();
        return;
    }

    clearPostExecutionState();
    if (m_ui.diagnosticsText)
    {
        m_ui.diagnosticsText->clear();
    }
    if (report.catalogChunkDownloadRequired)
    {
        startRequiredCatalogChunkDownload();
        return;
    }
    startCalibration();
}

void SpectrophotometricColorCalibrationProcess::startCalibration()
{
    m_runRecord = std::make_shared<an::ui::processes::SpccRunRecord>();
    if (m_ui.statusLabel)
    {
        m_ui.statusLabel->setText(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Preparing SPCC workflow..."));
    }
    applyProcess();
}

ProgressDialogControl* SpectrophotometricColorCalibrationProcess::getProgressDialogControl(const bool forSequence)
{
    (void)forSequence;
    auto* progress = new ProgressDialogControl(
        this, QString::fromStdString(
                  fmt::format("{} <{}>", getProcessName(),
                              m_image_view_control ? m_image_view_control->getUniqueName() : std::string())));
    progress->setProgressBarRange(0, 100);
    progress->setValue(0);
    progress->setText(tr("Running spectrophotometric color calibration..."));
    return progress;
}

void SpectrophotometricColorCalibrationProcess::executeProcessStarted(const RTPreviewMode previewMode,
                                                                      const bool sequenceMode)
{
    (void)previewMode;
    (void)sequenceMode;
    m_calibrationPreparing = false;
    if (m_ui.statusLabel)
    {
        m_ui.statusLabel->setText(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                                              "Running spectrophotometric color calibration..."));
    }
    updateControlsEnabled();
}

void SpectrophotometricColorCalibrationProcess::executeProcessCompletedImpl(const RTPreviewMode previewMode,
                                                                            const bool sequenceMode)
{
    (void)previewMode;
    (void)sequenceMode;
    if (m_image_view_control && m_image_view_control->hasImage())
    {
        if (const std::shared_ptr<const an::consumer::ImageAsset> asset = m_image_view_control->document().asset())
        {
            m_lastCalibratedRevision = asset->revision();
        }
    }
}

void SpectrophotometricColorCalibrationProcess::executeProcessEndedImpl(const RTPreviewMode previewMode,
                                                                        const bool sequenceMode)
{
    (void)previewMode;
    (void)sequenceMode;
    an::ui::processes::SpccRunSummary summary;
    bool cancelled = false;
    bool calibrated = false;
    if (m_runRecord)
    {
        const std::lock_guard lock(m_runRecord->mutex);
        summary = m_runRecord->summary;
        cancelled = m_runRecord->cancelled;
        calibrated = m_runRecord->calibrated;
        m_lastRunInputReads = m_runRecord->inputReads;
    }
    const bool applied = m_lastCalibratedRevision.has_value();
    if (m_ui.diagnosticsText)
    {
        m_ui.diagnosticsText->setPlainText(an::ui::processes::formatSpccWorkflowDiagnostics(summary));
    }
    const QString finalStatusText =
        an::ui::processes::formatSpccProcessFinalizationStatus(summary, calibrated, cancelled, applied);
    if (applied)
    {
        m_lastSuccessfulCalibrationStatusText = finalStatusText;
    }
    m_runRecord.reset();
    m_downloadedCatalogDirectory.clear();
    resetOperationProgress();
    if (m_ui.statusLabel && !finalStatusText.isEmpty())
    {
        m_ui.statusLabel->setText(finalStatusText);
    }
    if (applied && !m_ProcessCloseRequested)
    {
        notifyInfoMessage(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                                      "Spectrophotometric Color Calibration"),
                          finalStatusText);
    }
    else if (calibrated && !applied && !m_ProcessCloseRequested)
    {
        notifyWarnMessage(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                                      "Spectrophotometric Color Calibration"),
                          finalStatusText);
    }
    updateAvailabilityState();
}

void SpectrophotometricColorCalibrationProcess::startResponseRepositoryDownload(
    const an::ui::SpccResponseRepositoryDescriptor& repository)
{
    if (!m_spccCatalogDownloadService)
    {
        notifyErrorMessage(tr("SPCC Response Profiles"), tr("SPCC response profile downloader is not available."));
        updateAvailabilityState();
        return;
    }

    if (m_spccCatalogDownloadService->isBusy())
    {
        notifyWarnMessage(tr("SPCC Response Profiles"), tr("Another SPCC data download is already in progress."));
        return;
    }

    if (m_ui.statusLabel)
    {
        m_ui.statusLabel->setText(tr("Downloading SPCC response profiles..."));
    }
    showIndeterminateProgress();
    m_spccResponseDownloadInProgress = true;
    updateControlsEnabled();

    const QPointer<SpectrophotometricColorCalibrationProcess> guard(this);
    auto* service = m_spccCatalogDownloadService;
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(
        service, &an::ui::CatalogDownloadService::downloadFinished, this,
        [guard, connection](const an::ui::CatalogDownloadResult& downloadResult) {
            if (!guard)
            {
                return;
            }
            QObject::disconnect(*connection);
            guard->m_spccResponseDownloadInProgress = false;

            if (!downloadResult.success)
            {
                const QString errorText =
                    downloadResult.cancelled
                        ? guard->tr("SPCC response profile download was cancelled.")
                        : (downloadResult.errorMessage.isEmpty() ? guard->tr("SPCC response profile download failed.")
                                                                 : downloadResult.errorMessage);
                if (guard->m_ui.statusLabel)
                {
                    guard->m_ui.statusLabel->setText(errorText);
                }
                if (!downloadResult.cancelled)
                {
                    guard->notifyErrorMessage(guard->tr("SPCC Response Profiles"), errorText);
                }
                guard->resetOperationProgress();
                guard->updateAvailabilityState();
                return;
            }

            auto* settings = process_utils::currentGlobalSettings();
            if (settings == nullptr || !settings->upsert(SettingsKeys::CATALOGS_SPCC_RESPONSE_REPOSITORY_DIRECTORY,
                                                         downloadResult.installDirectory))
            {
                const QString errorText = guard->tr(
                    "SPCC response profiles were downloaded, but AfterNight could not save the managed repository "
                    "path.");
                if (guard->m_ui.statusLabel)
                {
                    guard->m_ui.statusLabel->setText(errorText);
                }
                guard->notifyErrorMessage(guard->tr("SPCC Response Profiles"), errorText);
                guard->resetOperationProgress();
                guard->updateAvailabilityState();
                return;
            }

            guard->m_manualResponseProfileId.clear();
            guard->m_manualWhiteReferenceId.clear();
            guard->resetOperationProgress();
            guard->updateAvailabilityState();
            if (guard->m_ui.statusLabel)
            {
                guard->m_ui.statusLabel->setText(
                    guard->tr("SPCC response profiles downloaded. Choose the sensor and filter response data."));
            }
        });

    service->setSpccResponseRepositoryRootDirectory(an::ui::defaultSpccResponseRepositoryDirectory());
    service->downloadSpccResponseRepository(repository);
}

void SpectrophotometricColorCalibrationProcess::startRequiredCatalogChunkDownload()
{
    const QString title = tr("Spectrophotometric Color Calibration");
    if (!m_spccCatalogDownloadService)
    {
        notifyErrorMessage(title, tr("SPCC catalog downloader is not available."));
        return;
    }
    const an::ui::processes::SpccProcessReadinessReport& report = m_ui.readinessPanel->report();
    const std::optional<an::ui::CatalogPackDescriptor> descriptor =
        an::ui::downloadableManagedSpccCatalogDescriptorForManifest(report.catalogManifestId);
    const std::optional<an::WcsSolution>& solution = m_image_view_control->imageSolution();
    const an::Result<an::SkyCoordinate> center =
        solution.has_value() ? solution->fieldCenter()
                             : an::Result<an::SkyCoordinate>(an::Status::failure(an::ErrorCode::InvalidState));
    const an::Result<double> radius = solution.has_value()
                                          ? solution->queryRadiusDegrees(getParams().matchRadiusArcsec)
                                          : an::Result<double>(an::Status::failure(an::ErrorCode::InvalidState));
    if (!descriptor.has_value())
    {
        notifyErrorMessage(title, tr("SPCC needs catalog chunks for this image, but no approved managed catalog "
                                     "descriptor with a manifest URL is available."));
        return;
    }
    if (!center.hasValue() || !radius.hasValue())
    {
        notifyErrorMessage(title, tr("SPCC could not derive the catalog chunks needed for this image."));
        return;
    }
    if (m_spccCatalogDownloadService->isBusy())
    {
        notifyWarnMessage(title, tr("Another SPCC data download is already in progress."));
        return;
    }

    m_calibrationPreparing = true;
    m_spccResponseDownloadInProgress = false;
    if (m_ui.statusLabel)
    {
        m_ui.statusLabel->setText(tr("Downloading SPCC catalog chunks needed for this image..."));
    }
    showIndeterminateProgress();
    updateControlsEnabled();

    const QPointer<SpectrophotometricColorCalibrationProcess> guard(this);
    auto* service = m_spccCatalogDownloadService;
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(
        service, &an::ui::CatalogDownloadService::downloadFinished, this,
        [guard, connection, title](const an::ui::CatalogDownloadResult& downloadResult) {
            if (!guard)
            {
                return;
            }
            QObject::disconnect(*connection);
            guard->resetOperationProgress();
            if (!downloadResult.success)
            {
                guard->m_calibrationPreparing = false;
                const QString errorText =
                    downloadResult.cancelled
                        ? guard->tr("SPCC catalog chunk download was cancelled.")
                        : (downloadResult.errorMessage.isEmpty() ? guard->tr("SPCC catalog chunk download failed.")
                                                                 : downloadResult.errorMessage);
                if (guard->m_ui.statusLabel)
                {
                    guard->m_ui.statusLabel->setText(errorText);
                }
                if (!downloadResult.cancelled)
                {
                    guard->notifyErrorMessage(title, errorText);
                }
                guard->updateAvailabilityState();
                return;
            }
            if (auto* settings = process_utils::currentGlobalSettings())
            {
                const QString catalogRoot = managedCatalogRootForInstallDirectory(downloadResult.installDirectory);
                if (!catalogRoot.trimmed().isEmpty())
                {
                    settings->upsert(SettingsKeys::CATALOGS_SPCC_CATALOG_DIRECTORY, catalogRoot);
                }
                if (!downloadResult.packId.trimmed().isEmpty())
                {
                    settings->upsert(SettingsKeys::CATALOGS_SPCC_CATALOG_PACK, downloadResult.packId);
                }
            }
            guard->m_downloadedCatalogDirectory = downloadResult.installDirectory;
            if (guard->m_ui.statusLabel)
            {
                guard->m_ui.statusLabel->setText(guard->tr("SPCC catalog chunks ready. Running calibration..."));
            }
            guard->startCalibration();
        });
    service->downloadSpccCatalogChunks(an::ui::SpccCatalogChunkDownloadRequest{
        .pack = *descriptor,
        .installRootDirectory = managedCatalogRootForInstallDirectory(report.catalogDirectory),
        .mode = an::ui::SpccCatalogChunkDownloadMode::RequiredRegion,
        .hasRegion = true,
        .centerRaDeg = center->rightAscensionDegrees,
        .centerDecDeg = center->declinationDegrees,
        .radiusDeg = *radius,
    });
}

void SpectrophotometricColorCalibrationProcess::restoreDefaults()
{
    if (m_ui.autoMagnitudeLimitCheck)
    {
        m_ui.autoMagnitudeLimitCheck->setChecked(true);
    }
    if (m_ui.magnitudeLimitSpin)
    {
        m_ui.magnitudeLimitSpin->setValue(an::ui::processes::kSpccProcessDefaultMagnitudeLimit);
    }
    if (m_ui.matchRadiusSpin)
    {
        m_ui.matchRadiusSpin->setValue(5.0);
    }
    if (m_ui.maxSourcesSpin)
    {
        m_ui.maxSourcesSpin->setValue(4096);
    }
    if (m_ui.sourceSnrSpin)
    {
        m_ui.sourceSnrSpin->setValue(5.0);
    }
    if (m_ui.residualPxSpin)
    {
        m_ui.residualPxSpin->setValue(3.0);
    }
    if (m_ui.residualArcsecSpin)
    {
        m_ui.residualArcsecSpin->setValue(3.0);
    }
    if (m_ui.minimumSamplesSpin)
    {
        m_ui.minimumSamplesSpin->setValue(static_cast<int>(an::kDefaultSpectrophotometricMinimumSamples));
    }
    if (m_ui.apertureScaleSpin)
    {
        m_ui.apertureScaleSpin->setValue(an::ui::processes::kSpccProcessDefaultApertureFwhmScale);
    }
    if (m_ui.annulusSigmaClipSpin)
    {
        m_ui.annulusSigmaClipSpin->setValue(3.0);
    }
    if (m_ui.outlierSigmaSpin)
    {
        m_ui.outlierSigmaSpin->setValue(an::kDefaultSpectrophotometricClipSigmas);
    }
    if (m_ui.fitOutlierPolicyCombo)
    {
        const int index = m_ui.fitOutlierPolicyCombo->findData(
            static_cast<int>(an::SpectrophotometricOutlierPolicy::IterativeSigmaClipRefit));
        m_ui.fitOutlierPolicyCombo->setCurrentIndex((std::max)(index, 0));
    }
    if (m_ui.fitIterationsSpin)
    {
        m_ui.fitIterationsSpin->setValue(static_cast<int>(an::kDefaultSpectrophotometricMaximumIterations));
    }
    if (m_ui.backgroundNeutralizationCheck)
    {
        m_ui.backgroundNeutralizationCheck->setChecked(true);
    }
    if (m_ui.atmosphereCorrectionCheck)
    {
        m_ui.atmosphereCorrectionCheck->setChecked(false);
    }
    if (m_ui.observerHeightSpin)
    {
        m_ui.observerHeightSpin->setValue(10.0);
    }
    if (m_ui.pressureSpin)
    {
        m_ui.pressureSpin->setValue(1013.25);
    }
    if (m_ui.pressureSeaLevelCheck)
    {
        m_ui.pressureSeaLevelCheck->setChecked(true);
    }
    if (m_ui.airmassSpin)
    {
        m_ui.airmassSpin->setValue(0.0);
    }
    if (m_ui.narrowbandModeCheck)
    {
        m_ui.narrowbandModeCheck->setChecked(false);
    }
    if (m_ui.narrowbandRedCenterSpin)
    {
        m_ui.narrowbandRedCenterSpin->setValue(656.3);
    }
    if (m_ui.narrowbandRedBandwidthSpin)
    {
        m_ui.narrowbandRedBandwidthSpin->setValue(7.0);
    }
    if (m_ui.narrowbandGreenCenterSpin)
    {
        m_ui.narrowbandGreenCenterSpin->setValue(500.7);
    }
    if (m_ui.narrowbandGreenBandwidthSpin)
    {
        m_ui.narrowbandGreenBandwidthSpin->setValue(7.0);
    }
    if (m_ui.narrowbandBlueCenterSpin)
    {
        m_ui.narrowbandBlueCenterSpin->setValue(486.1);
    }
    if (m_ui.narrowbandBlueBandwidthSpin)
    {
        m_ui.narrowbandBlueBandwidthSpin->setValue(7.0);
    }

    m_manualResponseProfileId.clear();
    m_manualWhiteReferenceId.clear();
    updateAvailabilityState();
}

void SpectrophotometricColorCalibrationProcess::updateAvailabilityState()
{
    updateControlsEnabled();
    if (!m_ui.readinessPanel)
    {
        return;
    }
    const an::consumer::EngineSession* session = engineSession();
    // A target a writer holds is read again when the writer lets it go and the view says so.
    const std::optional<TargetSnapshot> target = targetSnapshotOf(m_image_view_control);
    if (session == nullptr || !target.has_value())
    {
        return;
    }

    const SpectrophotometricColorCalibrationParams params = getParams();
    an::ui::processes::SpccReadinessInputs inputs =
        an::ui::processes::spccReadinessInputs(process_utils::currentGlobalSettings());
    inputs.explicitResponseProfileId = m_manualResponseProfileId;
    inputs.explicitWhiteReferenceId = m_manualWhiteReferenceId;
    inputs.allowNarrowbandInput = an::ui::processes::kSpccProcessNarrowbandControlsEnabled;
    inputs.matchRadiusArcsec = params.matchRadiusArcsec;
    if (target->descriptor.has_value() && target->metadata.has_value())
    {
        inputs.cameraModel = an::ui::processes::calibrationCameraModel(*target->metadata);
        inputs.filterName = an::ui::processes::calibrationFilterName(*target->metadata);
        inputs.image =
            an::ui::processes::SpccReadinessImage{.descriptor = *target->descriptor, .metadata = *target->metadata};
    }

    const std::uint64_t request = ++m_readinessRequests;
    // The decision this one overtakes stops where it is; its report would be dropped when it landed anyway.
    m_readinessStop.request_stop();
    m_readinessStop = std::stop_source();
    const std::stop_token requestStop = m_readinessStop.get_token();
    const std::stop_token processStop = windowStopToken();
    updateControlsEnabled();
    QPointer<SpectrophotometricColorCalibrationProcess> guard(this);
    runThenPublish(
        [session, inputs, requestStop, processStop]() {
            // Apply waits for this decision, so it is the dialog's own work and waits on this worker for a thread:
            // background work finding none free is refused, and nothing would ask again. A later request's stop ends
            // the wait, as the window's going does.
            std::stop_source runStop;
            const std::stop_callback onRequest(requestStop, [&runStop]() { runStop.request_stop(); });
            const std::stop_callback onProcess(processStop, [&runStop]() { runStop.request_stop(); });
            an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
                *session,
                an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Preview,
                                         .stopToken = runStop.get_token()},
                an::ui::ExecutionObserver{});
            if (!scope.hasValue())
            {
                an::ui::processes::SpccProcessReadinessReport report;
                report.summary = QString::fromUtf8(scope.status().message());
                return report;
            }
            an::ui::processes::SpccProcessReadinessReport report =
                an::ui::processes::evaluateSpccProcessReadiness(scope->execution(), inputs);
            scope->finish(an::Status::success());
            return report;
        },
        [guard, request](an::ui::processes::SpccProcessReadinessReport report) {
            if (!guard)
            {
                return;
            }
            guard->m_readinessLanded = (std::max)(guard->m_readinessLanded, request);
            // Only the latest decision is shown; one overtaken by a later request is dropped.
            if (request == guard->m_readinessRequests)
            {
                guard->showReadinessReport(std::move(report));
            }
            guard->updateControlsEnabled();
        });
}

void SpectrophotometricColorCalibrationProcess::showReadinessReport(
    an::ui::processes::SpccProcessReadinessReport report)
{
    if (postExecutionSuccessAppliesToCurrentImage() && report.imageStateAlreadyColorCalibrated)
    {
        report.summary.clear();
    }
    m_ui.readinessPanel->setReport(std::move(report));
    ++m_readinessShown;
    updateStatusForReport();
}

void SpectrophotometricColorCalibrationProcess::updateControlsEnabled()
{
    const bool busy = m_ExecutionInProgress || m_spccResponseDownloadInProgress || m_calibrationPreparing;
    setParameterControlsEnabled(!busy);
    if (m_ui.readinessPanel)
    {
        m_ui.readinessPanel->setEnabled(!busy);
    }
    if (m_ui.buttonBox)
    {
        if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
        {
            applyButton->setEnabled(!busy && !readinessPending() && m_ui.readinessPanel &&
                                    m_ui.readinessPanel->report().canExecute() &&
                                    isViewControlSupported(m_image_view_control));
        }
        if (auto* resetButton = m_ui.buttonBox->button(QDialogButtonBox::Reset))
        {
            resetButton->setEnabled(!busy);
        }
        if (auto* closeButton = m_ui.buttonBox->button(QDialogButtonBox::Close))
        {
            const bool downloading = m_spccCatalogDownloadService && m_spccCatalogDownloadService->isBusy();
            closeButton->setEnabled(true);
            closeButton->setText(busy || downloading ? tr("Cancel") : tr("Close"));
        }
    }
}

void SpectrophotometricColorCalibrationProcess::updateStatusForReport()
{
    if (!m_ui.statusLabel || !m_ui.readinessPanel || m_ExecutionInProgress || m_spccResponseDownloadInProgress ||
        m_calibrationPreparing)
    {
        return;
    }
    if (postExecutionSuccessAppliesToCurrentImage())
    {
        if (!m_lastSuccessfulCalibrationStatusText.isEmpty())
        {
            m_ui.statusLabel->setText(m_lastSuccessfulCalibrationStatusText);
        }
        return;
    }
    if (!isViewControlSupported(m_image_view_control))
    {
        m_ui.statusLabel->setText(QCoreApplication::translate(
            "SpectrophotometricColorCalibrationProcess", "Select an RGB target image to evaluate SPCC readiness."));
        return;
    }

    const auto& report = m_ui.readinessPanel->report();
    if (report.canExecute())
    {
        if (report.catalogChunkDownloadRequired)
        {
            m_ui.statusLabel->setText(QCoreApplication::translate(
                "SpectrophotometricColorCalibrationProcess",
                "Ready to run SPCC; AfterNight will download needed catalog chunks first."));
            return;
        }
        m_ui.statusLabel->setText(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                        "Ready to run SPCC with response profile %1 and white reference %2.")
                .arg(report.responseProfileId, report.whiteReferenceId));
        return;
    }
    m_ui.statusLabel->setText(report.summary);
}

void SpectrophotometricColorCalibrationProcess::setParameterControlsEnabled(const bool enabled)
{
    for (QWidget* widget : {static_cast<QWidget*>(m_ui.autoMagnitudeLimitCheck),
                            static_cast<QWidget*>(m_ui.magnitudeLimitSpin),
                            static_cast<QWidget*>(m_ui.matchRadiusSpin),
                            static_cast<QWidget*>(m_ui.maxSourcesSpin),
                            static_cast<QWidget*>(m_ui.sourceSnrSpin),
                            static_cast<QWidget*>(m_ui.residualPxSpin),
                            static_cast<QWidget*>(m_ui.residualArcsecSpin),
                            static_cast<QWidget*>(m_ui.minimumSamplesSpin),
                            static_cast<QWidget*>(m_ui.apertureScaleSpin),
                            static_cast<QWidget*>(m_ui.annulusSigmaClipSpin),
                            static_cast<QWidget*>(m_ui.fitOutlierPolicyCombo),
                            static_cast<QWidget*>(m_ui.outlierSigmaSpin),
                            static_cast<QWidget*>(m_ui.fitIterationsSpin),
                            static_cast<QWidget*>(m_ui.backgroundNeutralizationCheck),
                            static_cast<QWidget*>(m_ui.atmosphereCorrectionCheck),
                            static_cast<QWidget*>(m_ui.observerHeightSpin),
                            static_cast<QWidget*>(m_ui.pressureSpin),
                            static_cast<QWidget*>(m_ui.pressureSeaLevelCheck),
                            static_cast<QWidget*>(m_ui.airmassSpin),
                            static_cast<QWidget*>(m_ui.narrowbandModeCheck),
                            static_cast<QWidget*>(m_ui.narrowbandRedCenterSpin),
                            static_cast<QWidget*>(m_ui.narrowbandRedBandwidthSpin),
                            static_cast<QWidget*>(m_ui.narrowbandGreenCenterSpin),
                            static_cast<QWidget*>(m_ui.narrowbandGreenBandwidthSpin),
                            static_cast<QWidget*>(m_ui.narrowbandBlueCenterSpin),
                            static_cast<QWidget*>(m_ui.narrowbandBlueBandwidthSpin),
                            static_cast<QWidget*>(m_ui.targetImageSelector)})
    {
        if (widget)
        {
            widget->setEnabled(enabled);
        }
    }

    if (m_ui.magnitudeLimitSpin)
    {
        const bool autoMagnitude = m_ui.autoMagnitudeLimitCheck
                                       ? m_ui.autoMagnitudeLimitCheck->isChecked()
                                       : SpectrophotometricColorCalibrationParams{}.autoMagnitudeLimit;
        m_ui.magnitudeLimitSpin->setEnabled(enabled && !autoMagnitude);
    }
}

void SpectrophotometricColorCalibrationProcess::saveManualResponseProfileMapping(const QString& profileId,
                                                                                 const QString& whiteReferenceId)
{
    if (profileId.trimmed().isEmpty() || !m_ui.readinessPanel)
    {
        return;
    }

    const auto& report = m_ui.readinessPanel->report();
    if (report.responseProfileCameraModel.trimmed().isEmpty() || report.responseRepositoryId.trimmed().isEmpty() ||
        report.responseAliasRegistryVersion.trimmed().isEmpty())
    {
        return;
    }

    const QString mappedWhiteReferenceId =
        whiteReferenceId.trimmed().isEmpty() ? report.whiteReferenceId : whiteReferenceId.trimmed();
    (void)an::ui::saveSpccResponseProfileMapping(
        process_utils::currentGlobalSettings(), report.responseProfileCameraModel, report.responseProfileFilterName,
        report.responseRepositoryId, report.responseAliasRegistryVersion, profileId, mappedWhiteReferenceId);
}

void SpectrophotometricColorCalibrationProcess::openSettingsPageAndRefresh(const QString& pageName)
{
    process_utils::openSettingsPage(pageName);
    updateAvailabilityState();
}

void SpectrophotometricColorCalibrationProcess::requestCatalogDownload()
{
    openSettingsPageAndRefresh(an::ui::processes::spccCatalogsDataSettingsPageTitle());
}

void SpectrophotometricColorCalibrationProcess::requestNeededRegionCatalogDownload()
{
    const std::optional<an::WcsSolution> solution =
        m_image_view_control ? m_image_view_control->imageSolution() : std::nullopt;
    if (!solution.has_value())
    {
        notifyWarnMessage(tr("Download Needed SPCC Region"),
                          tr("Load a plate-solved image before requesting a regional SPCC catalog cache."));
        return;
    }

    const SpectrophotometricColorCalibrationParams params = getParams();
    QString diagnostic;
    const QString targetName =
        m_image_view_control ? QString::fromStdString(m_image_view_control->getUniqueName()) : QString();
    const std::optional<an::ui::SpccRegionalCatalogRequest> request = an::ui::makeSpccRegionalCatalogRequestFromWcs(
        *solution, params.matchRadiusArcsec, params.magnitudeLimit, targetName, &diagnostic);
    if (!request.has_value())
    {
        notifyWarnMessage(tr("Download Needed SPCC Region"),
                          diagnostic.isEmpty()
                              ? tr("SPCC could not derive a regional catalog request from the current image.")
                              : diagnostic);
        return;
    }

    if (!an::ui::saveSpccRegionalCatalogRequest(process_utils::currentGlobalSettings(), *request))
    {
        notifyWarnMessage(tr("Download Needed SPCC Region"),
                          tr("AfterNight could not save the regional SPCC catalog request in global settings."));
        return;
    }

    openSettingsPageAndRefresh(an::ui::processes::spccCatalogsDataSettingsPageTitle());
}

void SpectrophotometricColorCalibrationProcess::requestResponseProfileDownload()
{
    if (m_ExecutionInProgress || m_spccResponseDownloadInProgress)
    {
        return;
    }

    const std::optional<an::ui::SpccResponseRepositoryDescriptor> repository = preferredManagedSpccResponseRepository();
    if (!repository.has_value())
    {
        notifyWarnMessage(tr("SPCC Response Profiles"),
                          tr("No approved managed SPCC response profile repository is available for download."));
        return;
    }

    startResponseRepositoryDownload(*repository);
}

void SpectrophotometricColorCalibrationProcess::assignCurrentImageStateFromUserAction()
{
    if (!m_image_view_control || !m_image_view_control->hasImage())
    {
        return;
    }
    const std::shared_ptr<const an::consumer::ImageAsset> asset = m_image_view_control->document().asset();
    if (asset == nullptr)
    {
        return;
    }

    // The person attests the image as it is now: the revision they looked at is the attestation's evidence.
    const an::consumer::AssetRevision attested = asset->revision();
    int answer = QMessageBox::No;
    process_utils::messageBoxYesNo(
        this, QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Assign Linear Image State"),
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                    "Mark the current RGB image as linear for SPCC. Use this only when the data has "
                                    "not been stretched or color calibrated."),
        true, &answer);
    if (answer != QMessageBox::Yes || !m_image_view_control)
    {
        return;
    }

    const QString sourcePath =
        targetFilePath().isEmpty()
            ? QStringLiteral("view:%1").arg(QString::fromStdString(m_image_view_control->getUniqueName()))
            : targetFilePath();
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QPointer<SpectrophotometricColorCalibrationProcess> guard(this);
    m_image_view_control->editImage(
        "Assign Linear Image State", "Linear RGB attested for SPCC",
        [attested, sourcePath, timestamp](const an::consumer::EngineSession& session,
                                          const an::consumer::AssetWriteLease& image,
                                          std::stop_token stop) -> an::Result<an::consumer::ImageAsset> {
            if (image.revision() != attested)
            {
                return refusal(an::ErrorCode::InvalidState,
                               QStringLiteral("The image changed after it was attested; attest it again."));
            }
            const an::ImageMetadata& metadata = std::visit(
                [](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, image.image());
            an::Result<an::ImageMetadata> attestedMetadata =
                an::ui::processes::attestedLinearMetadata(metadata, sourcePath, attested, timestamp);
            if (!attestedMetadata.hasValue())
            {
                return attestedMetadata.status();
            }
            return an::ui::copyImageWithMetadata(session, image, std::move(*attestedMetadata), stop);
        },
        [guard](bool shown) {
            if (!guard)
            {
                return;
            }
            if (!shown)
            {
                guard->notifyWarnMessage(
                    QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                                "Assign Linear Image State"),
                    QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                                "The image was not marked linear: it changed after it was attested, "
                                                "or it was closed. Attest it again."));
            }
            guard->updateAvailabilityState();
        });
}

QString SpectrophotometricColorCalibrationProcess::targetFilePath() const
{
    const QString filePath =
        m_image_view_control ? QString::fromStdString(m_image_view_control->getFileName()).trimmed() : QString();
    return filePath.isEmpty() ? QString() : QFileInfo(filePath).absoluteFilePath();
}
