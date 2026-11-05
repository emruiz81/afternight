// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The SPCC window's readiness: whether a run can start - the catalog, the response repository, the profile the image
/// resolves to and the image's state - and the panel that shows it and offers what is missing; and the diagnostics and
/// status a run leaves.
///
/// A readiness check reads files and composes response curves, so the window decides it on a worker from inputs it
/// snapshots on the GUI thread (`SpccReadinessInputs`) and shows the report when it lands. The rules are stated over
/// the engine's records: the catalog's readiness, `spectrophotometricInputProblem` over the image's declared state and
/// `checkResponseProfileCompatibility` over the profile's capabilities.
#pragma once

#include "AfterNightProcessesExport.h"

#include "app/SpccResponseSettings.h"

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricColorCalibration.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <QMap>
#include <QString>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;

namespace an
{
class ExecutionContext;
} // namespace an

namespace an::ui
{
class AnInfoBanner;
class IGlobalSettingsProvider;
} // namespace an::ui

namespace an::ui::processes
{

enum class SpccProcessReadinessSeverity
{
    Ready,
    Warning,
    Blocked,
};

struct AFTERNIGHTPROCESSES_API SpccResponseProfileComponentSet
{
    QStringList red;
    QStringList green;
    QStringList blue;
};

struct AFTERNIGHTPROCESSES_API SpccProcessReadinessReport
{
    /// Whether the decision was stopped before it finished - a newer one overtook it - so nothing past the catalog is
    /// decided in it.
    bool stopped = false;
    bool catalogReady = false;
    bool responseAssetsReady = false;
    bool catalogRightsApproved = false;
    bool catalogRegionChecked = false;
    bool catalogRegionAvailable = true;
    bool catalogChunkDownloadRequired = false;
    bool catalogChunkAutoDownloadAvailable = false;
    int catalogRequiredChunkCount = 0;
    int catalogMissingChunkCount = 0;
    qint64 catalogMissingChunkBytes = 0;
    QString catalogDirectory;
    QString catalogManifestId;
    QString catalogAccessProfile;
    QString catalogMessage;
    QString catalogRegionMessage;
    QString responseRepositoryDirectory;
    QString responseRepositoryId;
    QString responseRepositoryVersion;
    QString responseAssetsMessage;
    bool responseAssetsDownloadRequired = false;
    bool responseAssetsAutoDownloadAvailable = false;
    qint64 responseAssetsDownloadBytes = 0;
    QString responseAssetsDownloadName;
    bool responseProfileResolved = false;
    bool responseProfileAmbiguous = false;
    bool responseProfileManualSelection = false;
    bool responseProfileSavedMapping = false;
    QString responseProfileCameraModel;
    QString responseProfileFilterName;
    QString explicitResponseProfileId;
    QString responseProfileId;
    QString responseProfileVersion;
    QString explicitWhiteReferenceId;
    QString whiteReferenceId;
    QString whiteReferenceVersion;
    QString responseAliasRegistryVersion;
    QStringList availableResponseProfileIds;
    QStringList candidateResponseProfileIds;
    QMap<QString, SpccResponseProfileComponentSet> availableResponseProfileComponents;
    QStringList availableWhiteReferenceIds;
    QMap<QString, QString> availableWhiteReferenceDisplayNames;
    QString responseProfileMessage;
    bool imageStateChecked = false;
    bool imageStateCompatible = false;
    bool imageStateAlreadyColorCalibrated = false;
    QString imageStateMessage;
    QString summary;
    QString settingsPageTitle;
    QString actionText;

    [[nodiscard]] bool canExecute() const noexcept
    {
        const bool catalogReadyForExecution = catalogReady && catalogRightsApproved;
        const bool catalogPreparationAvailable = catalogChunkDownloadRequired && catalogChunkAutoDownloadAvailable;
        const bool catalogRegionReady = !catalogRegionChecked || catalogRegionAvailable ||
                                        (catalogChunkDownloadRequired && catalogChunkAutoDownloadAvailable);
        const bool responseReadyForExecution = responseAssetsReady && responseProfileResolved;
        return (catalogReadyForExecution || catalogPreparationAvailable) && catalogRegionReady &&
               responseReadyForExecution && (!imageStateChecked || imageStateCompatible);
    }

    [[nodiscard]] SpccProcessReadinessSeverity severity() const noexcept
    {
        if (canExecute())
        {
            return SpccProcessReadinessSeverity::Ready;
        }

        const bool catalogReadyForExecution = catalogReady && catalogRightsApproved;
        const bool catalogPreparationAvailable = catalogChunkDownloadRequired && catalogChunkAutoDownloadAvailable;
        const bool catalogRegionReady = !catalogRegionChecked || catalogRegionAvailable ||
                                        (catalogChunkDownloadRequired && catalogChunkAutoDownloadAvailable);
        const bool catalogReadyOrPreparable =
            (catalogReadyForExecution || catalogPreparationAvailable) && catalogRegionReady;
        const bool responseReadyForExecution = responseAssetsReady && responseProfileResolved;
        const bool imageReadyForExecution = !imageStateChecked || imageStateCompatible;
        const bool imageAlreadyCalibrated =
            imageStateChecked && !imageStateCompatible && imageStateAlreadyColorCalibrated;
        const bool responseProfileChoicePending =
            responseAssetsReady && !responseProfileResolved && responseProfileId.isEmpty() &&
            whiteReferenceId.isEmpty() && explicitResponseProfileId.isEmpty() && explicitWhiteReferenceId.isEmpty() &&
            (!availableResponseProfileIds.isEmpty() || !candidateResponseProfileIds.isEmpty() ||
             responseProfileAmbiguous);
        if (catalogReadyOrPreparable && imageReadyForExecution && responseProfileChoicePending)
        {
            return SpccProcessReadinessSeverity::Warning;
        }
        if (catalogReadyOrPreparable && responseReadyForExecution && imageAlreadyCalibrated)
        {
            return SpccProcessReadinessSeverity::Warning;
        }

        return SpccProcessReadinessSeverity::Blocked;
    }
};

/// How often one run read each input its frames share - the catalog verified against its checksums, its manifest, the
/// response repository, the ImageIntegration hand-off, a field's chunks and the catalog built from them.
struct SpccRunInputReads
{
    std::uint32_t catalogVerifications = 0u;
    std::uint32_t manifests = 0u;
    std::uint32_t repositories = 0u;
    std::uint32_t handoffs = 0u;
    std::uint32_t regions = 0u;
    std::uint32_t catalogs = 0u;
};

/// What one SPCC run came to, for the diagnostics a person reads and the status line the window shows.
struct AFTERNIGHTPROCESSES_API SpccRunSummary
{
    /// Whether the fit was accepted and the gains were written.
    bool calibrated = false;

    /// What the run said: the engine's refusal, or empty for a calibration.
    QString message;

    /// How many detected stars the preflight matched to the catalog, and their residuals.
    std::uint32_t wcsMatches = 0u;
    std::optional<double> rmsResidualPixels;
    std::optional<double> rmsResidualArcseconds;
    double residualThresholdPixels = 3.0;

    /// How many matches became colour samples, how many were refused, and how many the fit could use.
    std::uint32_t acceptedSamples = 0u;
    std::uint32_t rejectedSamples = 0u;
    std::uint32_t usableFitSamples = 0u;

    /// The inliers both colour lines kept; the fit's support.
    std::uint32_t fitSupport = 0u;

    /// Whether the fit was accepted on fewer inliers than is comfortable.
    bool lowSupport = false;

    /// The fit's verdict when it was not accepted, by its name.
    QString fitVerdict;

    /// Whether the run failed because the catalog does not cover the image's field.
    bool catalogCoverageFailure = false;

    /// The gains and offsets written, red, green and blue.
    std::array<double, 3u> gains{1.0, 1.0, 1.0};
    std::array<double, 3u> offsets{0.0, 0.0, 0.0};

    /// The Gaia sources the fit rejected as outliers.
    std::vector<std::uint64_t> rejectedOutlierSources;

    /// What the run's stages said, for a person.
    QStringList workflowDiagnostics;
};

/// The summary of a calibration the engine completed.
///
/// @param result the engine's result.
/// @return the summary.
[[nodiscard]] AFTERNIGHTPROCESSES_API SpccRunSummary
spccRunSummary(const an::SpectrophotometricColorCalibrationResult& result);

/// What a readiness check reads of the image a run would calibrate.
struct SpccReadinessImage
{
    /// What the image is.
    an::ImageDescriptor descriptor;

    /// What is known about it.
    an::ImageMetadata metadata;
};

/// What an SPCC readiness check is decided from, snapshotted on the GUI thread so it can be decided on a worker.
struct AFTERNIGHTPROCESSES_API SpccReadinessInputs
{
    QString catalogDirectory;
    QString responseRepositoryDirectory;
    QString cameraModel;
    QString filterName;
    QString explicitResponseProfileId;
    QString explicitWhiteReferenceId;

    /// The camera and filter mappings a person saved, from the settings.
    QVector<SpccResponseProfileMapping> savedMappings;

    /// Whether a narrowband filter is admitted, for an observation model that has its passbands.
    bool allowNarrowbandInput = false;

    /// The catalog match radius, which widens the field's cone.
    double matchRadiusArcsec = 5.0;

    /// The image the run would calibrate, or nothing for readiness without an image.
    std::optional<SpccReadinessImage> image;
};

/// The inputs the settings state: the catalog and response directories and the saved mappings.
///
/// @param settings the application's settings, or null for the defaults.
/// @return the inputs, with no image.
[[nodiscard]] AFTERNIGHTPROCESSES_API SpccReadinessInputs spccReadinessInputs(IGlobalSettingsProvider* settings);

[[nodiscard]] AFTERNIGHTPROCESSES_API QString spccCatalogsDataSettingsPageTitle();

[[nodiscard]] AFTERNIGHTPROCESSES_API QString formatSpccWorkflowDiagnostics(const SpccRunSummary& summary);

[[nodiscard]] AFTERNIGHTPROCESSES_API QString formatSpccProcessFinalizationStatus(const SpccRunSummary& summary,
                                                                                  bool success, bool cancelled,
                                                                                  bool targetResultApplied,
                                                                                  const QString& errorText = QString());

/// Decides whether an SPCC run can start, and what it will prepare first: the catalog's installation, rights and the
/// chunks the image's field needs; the response repository and the profile and white reference the image resolves to;
/// and whether the image's state may be calibrated.
///
/// Reads the catalog's manifest without verifying its checksums, the response repository whole, and the image's
/// metadata; runs on a worker.
///
/// @param execution the operation the engine's chunk selection and curve composition run in.
/// @param inputs what to decide from.
/// @return the report.
[[nodiscard]] AFTERNIGHTPROCESSES_API SpccProcessReadinessReport
evaluateSpccProcessReadiness(const an::ExecutionContext& execution, const SpccReadinessInputs& inputs);

class AFTERNIGHTPROCESSES_API SpccProcessReadinessPanel final : public QWidget
{
public:
    explicit SpccProcessReadinessPanel(QWidget* parent = nullptr);
    ~SpccProcessReadinessPanel() override = default;

    void refreshTheme();
    void setReport(SpccProcessReadinessReport report);
    [[nodiscard]] const SpccProcessReadinessReport& report() const noexcept;

    void setOpenSettingsPageCallback(std::function<void(const QString& pageName)> callback);
    void setManualResponseProfileSelectedCallback(std::function<void(const QString& profileId)> callback);
    void setManualWhiteReferenceSelectedCallback(std::function<void(const QString& whiteReferenceId)> callback);
    void setDownloadFullCatalogCallback(std::function<void()> callback);
    void setDownloadNeededRegionCallback(std::function<void()> callback);
    void setDownloadResponseProfilesCallback(std::function<void()> callback);
    void setAssignImageStateCallback(std::function<void()> callback);

private:
    void openSettingsTarget();
    void triggerDownloadFullCatalog();
    void triggerDownloadNeededRegion();
    void triggerDownloadResponseProfiles();
    void triggerAssignImageState();
    void updateActionButtons();
    void updateResponseProfileSelector();
    void selectCurrentStructuredResponseProfile();

    AnInfoBanner* m_banner = nullptr;
    QLabel* m_responseSensorLabel = nullptr;
    QLabel* m_responseFilterLabel = nullptr;
    QLabel* m_responseLpfLabel = nullptr;
    QLabel* m_responseMonoRedFilterLabel = nullptr;
    QLabel* m_responseMonoGreenFilterLabel = nullptr;
    QLabel* m_responseMonoBlueFilterLabel = nullptr;
    QLabel* m_responseProfileLabel = nullptr;
    QLabel* m_whiteReferenceLabel = nullptr;
    QComboBox* m_responseSensorCombo = nullptr;
    QComboBox* m_responseFilterCombo = nullptr;
    QComboBox* m_responseLpfCombo = nullptr;
    QComboBox* m_responseMonoRedFilterCombo = nullptr;
    QComboBox* m_responseMonoGreenFilterCombo = nullptr;
    QComboBox* m_responseMonoBlueFilterCombo = nullptr;
    QComboBox* m_responseProfileCombo = nullptr;
    QComboBox* m_whiteReferenceCombo = nullptr;
    QLabel* m_responseProfileDetailsLabel = nullptr;
    QPushButton* m_manageCatalogsButton = nullptr;
    QPushButton* m_downloadFullCatalogButton = nullptr;
    QPushButton* m_downloadNeededRegionButton = nullptr;
    QPushButton* m_downloadResponseProfilesButton = nullptr;
    QPushButton* m_assignImageStateButton = nullptr;
    std::function<void(const QString& pageName)> m_openSettingsPage;
    std::function<void(const QString& profileId)> m_manualResponseProfileSelected;
    std::function<void(const QString& whiteReferenceId)> m_manualWhiteReferenceSelected;
    std::function<void()> m_downloadFullCatalog;
    std::function<void()> m_downloadNeededRegion;
    std::function<void()> m_downloadResponseProfiles;
    std::function<void()> m_assignImageState;
    SpccProcessReadinessReport m_report;
    bool m_updatingResponseProfileCombo = false;
    QString m_selectedResponseSensorKey;
    QString m_selectedResponseOscFilterId;
    QString m_selectedResponseOscLpfId;
    QString m_selectedResponseMonoRedFilterId;
    QString m_selectedResponseMonoGreenFilterId;
    QString m_selectedResponseMonoBlueFilterId;
};

} // namespace an::ui::processes
