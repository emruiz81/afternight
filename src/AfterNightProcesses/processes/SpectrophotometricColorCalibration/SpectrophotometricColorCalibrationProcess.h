// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The spectrophotometric colour calibration window: channel gains from the Gaia XP spectra of the stars a
/// plate-solved linear RGB image holds, measured through the camera's response, and a neutral sky.
///
/// **Readiness is decided on a worker and shown when it lands.** Whether a run can start - the catalog, its rights and
/// the chunks the field needs, the response repository and the profile and white reference the image resolves to,
/// the image's state - reads files and composes response curves, so the window snapshots what it is decided from
/// (`SpccReadinessInputs`) and decides it on the thread pool every time any of it changes, showing only the latest.
///
/// **A run is the framework's Apply.** Apply first downloads the catalog chunks the field needs when the readiness
/// says it will, then applies through the typed framework: on the edit's worker the solution is read from the image's
/// cards, the response repository loaded and the recipe planned (`planSpectrophotometricColorCalibration`), the
/// catalog read for the field, the ImageIntegration hand-off's sources resolved, the working copy calibrated by
/// `calibrateSpectrophotometricColor` and brought back to the image's representation, and the result's metadata
/// records the calibration. The framework weighs it by the mask, records the step before the view shows it, stops it
/// from the progress dialog's Cancel, and runs a sequence frame by frame.
///
/// **An attestation names a revision.** A person's attestation of an image's state names the revision of the asset
/// the person looked at, and the window keeps the revision it calibrated.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TypedProcessControl.h"
#include "processes/SpectrophotometricColorCalibration/SpectrophotometricColorCalibrationUiBuilder.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricColorCalibration.h>
#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricFit.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>

#include <QPointer>
#include <QString>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>

class ImageViewControl;
class QAbstractButton;
class QCloseEvent;

namespace an::ui
{
class CatalogDownloadService;
struct CatalogDownloadProgress;
struct SpccResponseRepositoryDescriptor;
} // namespace an::ui

namespace an::ui::processes
{
struct SpccRunRecord;
class SpccRunInputs;
} // namespace an::ui::processes

struct SpectrophotometricColorCalibrationParams
{
    bool autoMagnitudeLimit = true;
    double magnitudeLimit = an::ui::processes::kSpccProcessDefaultMagnitudeLimit;
    double matchRadiusArcsec = 5.0;
    int maxDetectedSources = 4096;
    double sourceSnr = 5.0;
    double residualThresholdPx = 3.0;
    double residualThresholdArcsec = 3.0;
    int minimumSampleCount = static_cast<int>(an::kDefaultSpectrophotometricMinimumSamples);
    double apertureFwhmScale = an::ui::processes::kSpccProcessDefaultApertureFwhmScale;
    double annulusSigmaClip = 3.0;
    an::SpectrophotometricOutlierPolicy fitOutlierPolicy = an::SpectrophotometricOutlierPolicy::IterativeSigmaClipRefit;
    double outlierSigma = an::kDefaultSpectrophotometricClipSigmas;
    int fitIterations = static_cast<int>(an::kDefaultSpectrophotometricMaximumIterations);
    bool applyBackgroundNeutralization = true;
    bool atmosphereEnabled = false;
    double observerHeightM = 10.0;
    double pressureHpa = 1013.25;
    bool pressureIsSeaLevel = true;
    double airmass = 0.0;
    bool narrowbandEnabled = false;
    double narrowbandRedCenterNm = 656.3;
    double narrowbandRedBandwidthNm = 7.0;
    double narrowbandGreenCenterNm = 500.7;
    double narrowbandGreenBandwidthNm = 7.0;
    double narrowbandBlueCenterNm = 486.1;
    double narrowbandBlueBandwidthNm = 7.0;

    /// The installed catalog the run reads, from the readiness the Apply was decided on.
    QString catalogDirectory;

    /// The response repository the run reads.
    QString responseRepositoryDirectory;

    /// The response profile and white reference the readiness resolved.
    QString responseProfileId;
    QString whiteReferenceId;

    /// The target's file, for the ImageIntegration hand-off; empty for an image that came from none.
    QString targetPath;

    /// Where the run's worker leaves what it found, for the window to show when the run lands.
    std::shared_ptr<an::ui::processes::SpccRunRecord> record;

    /// What the run reads once for all its frames - the verified catalog, its manifest, the response repository, the
    /// hand-off and the last field's region - made with these parameters, so every frame of one run shares it.
    std::shared_ptr<an::ui::processes::SpccRunInputs> inputs;
};

namespace an::ui::processes
{

/// The engine request one SPCC run is planned with, from the dialog's parameters: the detection ceiling and the
/// source selection, the preflight's match, magnitude and residuals with the solution and epoch it names, the
/// apertures, the fit's floor, clipping and policy, and the sky.
///
/// @param params the dialog's parameters.
/// @param filterName the image's filter.
/// @param solutionIdentity the solution's identity, from `spccSolutionIdentity`.
/// @param epochYear the observation epoch, or nothing for the catalog's.
/// @return the request.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::SpectrophotometricColorCalibrationRequest
spectrophotometricCalibrationRequest(const SpectrophotometricColorCalibrationParams& params, const QString& filterName,
                                     const QString& solutionIdentity, const std::optional<double>& epochYear);

/// How the spectrophotometric calibration finds stars in the green channel: the star finder at its defaults, with the
/// ceiling the dialog states.
///
/// @param params the dialog's parameters.
/// @return the detection request.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::StarDetectionRequest
spectrophotometricStarDetection(const SpectrophotometricColorCalibrationParams& params);

} // namespace an::ui::processes

class AFTERNIGHTPROCESSES_API SpectrophotometricColorCalibrationProcess
    : public an::ui::TypedProcessControl<SpectrophotometricColorCalibrationParams>
{
public:
    explicit SpectrophotometricColorCalibrationProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    bool isRTPreviewProcess() const override;

    void onImageLoaded(ImageViewControl* ctrl) override;
    void onImageUpdated(ImageViewControl* ctrl) override;
    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;
    void onImageViewControlDestroyed(ImageViewControl* ctrl) override;

    /// Runs the calibration as the Apply button does: downloads the chunks the field needs first, when the readiness
    /// says it will.
    void executeCalibration();

    /// Asks for the readiness to be decided again from what the window holds now.
    void updateAvailabilityState();

    /// How many readiness reports have landed; a case waits for the next one.
    [[nodiscard]] std::uint64_t readinessReportsShown() const noexcept;

    /// Whether a readiness decision is on its way.
    [[nodiscard]] bool readinessPending() const noexcept;

    /// How often the last run read each of the inputs it shares across its frames, as it stood when its last frame
    /// ended.
    [[nodiscard]] an::ui::processes::SpccRunInputReads lastRunInputReads() const;

    ~SpectrophotometricColorCalibrationProcess() override;

protected:
    [[nodiscard]] SpectrophotometricColorCalibrationParams getParams() const override;

    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run,
                                       const SpectrophotometricColorCalibrationParams& params,
                                       an::ui::ExecutionScope& scope) override;

    [[nodiscard]] an::ui::ProcessStep
    describeTypedProcessStep(const SpectrophotometricColorCalibrationParams& params) const override;

private:
    void refreshTheme() override;
    bool isViewControlSupported(const ImageViewControl* ctrl) override;
    void closeEvent(QCloseEvent* event) override;
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    void settingsUpdatedImpl() override;
    ProgressDialogControl* getProgressDialogControl(bool forSequence) override;
    void executeProcessStarted(RTPreviewMode previewMode, bool sequenceMode) override;
    void executeProcessCompletedImpl(RTPreviewMode previewMode, bool sequenceMode) override;
    void executeProcessEndedImpl(RTPreviewMode previewMode, bool sequenceMode) override;

    void onDialogButtonClicked(QAbstractButton* button);
    void applyDownloadProgress(const an::ui::CatalogDownloadProgress& progress);
    void showIndeterminateProgress();
    void resetOperationProgress();
    void startCalibration();
    void startRequiredCatalogChunkDownload();
    void restoreDefaults();
    void showReadinessReport(an::ui::processes::SpccProcessReadinessReport report);
    void updateStatusForReport();
    void updateControlsEnabled();
    void clearPostExecutionState();
    [[nodiscard]] bool postExecutionSuccessAppliesToCurrentImage() const;
    void setParameterControlsEnabled(bool enabled);
    void saveManualResponseProfileMapping(const QString& profileId, const QString& whiteReferenceId = QString());
    void openSettingsPageAndRefresh(const QString& pageName);
    void requestCatalogDownload();
    void requestNeededRegionCatalogDownload();
    void requestResponseProfileDownload();
    void startResponseRepositoryDownload(const an::ui::SpccResponseRepositoryDescriptor& repository);
    void assignCurrentImageStateFromUserAction();
    void cancelActiveWork();
    [[nodiscard]] QString targetFilePath() const;

    an::ui::processes::SpectrophotometricColorCalibrationUiElements m_ui;
    QString m_manualResponseProfileId;
    QString m_manualWhiteReferenceId;
    QString m_downloadedCatalogDirectory;
    an::ui::CatalogDownloadService* m_spccCatalogDownloadService = nullptr;
    std::shared_ptr<an::ui::processes::SpccRunRecord> m_runRecord;
    an::ui::processes::SpccRunInputReads m_lastRunInputReads;
    std::optional<an::consumer::AssetRevision> m_lastCalibratedRevision;
    QString m_lastSuccessfulCalibrationStatusText;
    std::uint64_t m_readinessRequests = 0u;
    std::uint64_t m_readinessShown = 0u;
    std::uint64_t m_readinessLanded = 0u;
    // Stops the decision on its way when a newer one is asked for or the window goes: only the latest is ever shown.
    std::stop_source m_readinessStop;
    bool m_spccResponseDownloadInProgress = false;
    bool m_calibrationPreparing = false;
};
