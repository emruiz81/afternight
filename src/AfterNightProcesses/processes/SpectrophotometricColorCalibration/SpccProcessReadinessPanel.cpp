// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "SpccProcessReadinessPanel.h"

#include "app/SpccCatalogSettings.h"
#include "app/SpccResponseSettings.h"
#include "components/AnInfoBanner.h"
#include "processes/PhotometricCalibrationSupport.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/SpectrophotometricColorCalibration/SpccResponseRepository.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>
#include <AfterNightEngine/Algorithms/Photometry/PhotometricColorCalibration.h>
#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectralResponse.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace an::ui::processes
{
namespace
{

[[nodiscard]] QString catalogMessageForStatus(const an::ui::SpccCatalogDirectoryInspection& inspection)
{
    switch (inspection.status)
    {
    case an::ui::SpccCatalogDirectoryStatus::Ready:
        if (!an::ui::isApprovedSpccCatalogDescriptorRightsStatus(inspection.rightsReviewStatus))
        {
            return QCoreApplication::translate(
                       "SpccProcessReadiness",
                       "The selected SPCC catalog is installed, but its rights/provenance review status is '%1'. Use "
                       "a catalog approved for managed download or bundling before calibration.")
                .arg(inspection.rightsReviewStatus.isEmpty()
                         ? QCoreApplication::translate("SpccProcessReadiness", "unknown")
                         : inspection.rightsReviewStatus);
        }
        return QCoreApplication::translate("SpccProcessReadiness", "SPCC catalog ready: %1 (%2).")
            .arg(inspection.manifestId,
                 inspection.accessProfile.isEmpty()
                     ? QCoreApplication::translate("SpccProcessReadiness", "unknown access profile")
                     : inspection.accessProfile);

    case an::ui::SpccCatalogDirectoryStatus::PartialCache:
        return QCoreApplication::translate(
                   "SpccProcessReadiness",
                   "SPCC catalog metadata ready: %1 (%2). Cached chunks %3/%4; missing chunks will be downloaded "
                   "when needed.")
            .arg(inspection.manifestId,
                 inspection.accessProfile.isEmpty()
                     ? QCoreApplication::translate("SpccProcessReadiness", "unknown access profile")
                     : inspection.accessProfile)
            .arg(inspection.chunkFilesPresent)
            .arg(inspection.chunkCount);

    case an::ui::SpccCatalogDirectoryStatus::IncompleteInstall:
        if (!inspection.diagnostic.isEmpty())
        {
            return QCoreApplication::translate(
                       "SpccProcessReadiness",
                       "The selected SPCC catalog is incomplete. %1 Reinstall or repair it from Catalogs & Data.")
                .arg(inspection.diagnostic);
        }
        return QCoreApplication::translate(
            "SpccProcessReadiness",
            "The selected SPCC catalog is incomplete. Reinstall or repair it from Catalogs & Data.");

    case an::ui::SpccCatalogDirectoryStatus::Invalid:
        return QCoreApplication::translate("SpccProcessReadiness",
                                           "The selected directory does not contain a valid SPCC catalog manifest.");

    case an::ui::SpccCatalogDirectoryStatus::Missing:
    default:
        return QCoreApplication::translate("SpccProcessReadiness",
                                           "No installed Gaia XP sampled SPCC catalog is selected.");
    }
}

[[nodiscard]] QString formatSpccDataBytes(const qint64 bytes)
{
    if (bytes <= 0)
    {
        return QCoreApplication::translate("SpccProcessReadiness", "unknown size");
    }

    double size = static_cast<double>(bytes);
    for (const QString& suffix :
         {QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"), QStringLiteral("GB"), QStringLiteral("TB")})
    {
        if (size < 1024.0 || suffix == QStringLiteral("TB"))
        {
            return suffix == QStringLiteral("B") ? QStringLiteral("%1 B").arg(static_cast<qint64>(size))
                                                 : QStringLiteral("%1 %2").arg(size, 0, 'f', 1).arg(suffix);
        }
        size /= 1024.0;
    }
    return QStringLiteral("%1 TB").arg(size, 0, 'f', 1);
}

[[nodiscard]] QString catalogPackDisplayName(const an::ui::CatalogPackDescriptor& pack)
{
    return pack.displayName.trimmed().isEmpty() ? pack.id.trimmed() : pack.displayName.trimmed();
}

[[nodiscard]] QString responseRepositoryDisplayName(const an::ui::SpccResponseRepositoryDescriptor& repository)
{
    return repository.displayName.trimmed().isEmpty() ? repository.id.trimmed() : repository.displayName.trimmed();
}

[[nodiscard]] std::optional<an::ui::CatalogPackDescriptor> preferredManagedSpccCatalogDescriptor()
{
    return an::ui::preferredDownloadableManagedSpccLocalFullCatalogDescriptor();
}

[[nodiscard]] std::optional<an::ui::SpccResponseRepositoryDescriptor> preferredManagedSpccResponseRepository()
{
    const QVector<an::ui::SpccResponseRepositoryDescriptor> repositories = an::ui::availableSpccResponseRepositories();
    auto it = std::find_if(
        repositories.begin(), repositories.end(), [](const an::ui::SpccResponseRepositoryDescriptor& repository) {
            return repository.recommended && repository.manifestUrl.isValid() &&
                   an::ui::isApprovedSpccResponseDescriptorRightsStatus(repository.rightsReviewStatus) &&
                   an::ui::isSupportedSpccResponseDeploymentScope(repository.deploymentScope);
        });
    if (it == repositories.end())
    {
        it = std::find_if(
            repositories.begin(), repositories.end(), [](const an::ui::SpccResponseRepositoryDescriptor& repository) {
                return repository.manifestUrl.isValid() &&
                       an::ui::isApprovedSpccResponseDescriptorRightsStatus(repository.rightsReviewStatus) &&
                       an::ui::isSupportedSpccResponseDeploymentScope(repository.deploymentScope);
            });
    }
    return it == repositories.end() ? std::nullopt : std::optional<an::ui::SpccResponseRepositoryDescriptor>(*it);
}

[[nodiscard]] QString defaultManagedSpccCatalogInstallDirectory(const an::ui::CatalogPackDescriptor& pack)
{
    return QDir(an::ui::defaultSpccCatalogDirectory()).filePath(pack.id.trimmed());
}

[[nodiscard]] bool hasApprovedManagedSpccCatalogDescriptor(const an::ui::SpccCatalogDirectoryInspection& inspection)
{
    if (inspection.manifestId.trimmed().isEmpty())
    {
        return false;
    }
    const std::optional<an::ui::CatalogPackDescriptor> descriptor =
        an::ui::downloadableManagedSpccCatalogDescriptorForManifest(inspection.manifestId);
    return descriptor.has_value() &&
           descriptor->catalogFamilyId.compare(inspection.catalogFamilyId, Qt::CaseInsensitive) == 0 &&
           descriptor->accessProfile.compare(inspection.accessProfile, Qt::CaseInsensitive) == 0;
}

/// The cone an image's catalog field is: its solution's centre and a radius that covers the frame and the match
/// radius; nothing for an image whose cards state no usable solution.
struct FieldCone
{
    an::SkyCoordinate center{};
    double radiusDegrees = 0.0;
};

[[nodiscard]] std::optional<FieldCone> fieldConeOf(const SpccReadinessImage& image, const double matchRadiusArcsec)
{
    const std::optional<an::WcsSolution> solution =
        an::ui::processes::calibrationSolutionOf(image.metadata, image.descriptor);
    if (!solution.has_value())
    {
        return std::nullopt;
    }
    const an::Result<an::SkyCoordinate> center = solution->fieldCenter();
    const an::Result<double> radius = solution->queryRadiusDegrees(matchRadiusArcsec);
    if (!center.hasValue() || !radius.hasValue() || !std::isfinite(*radius) || *radius <= 0.0)
    {
        return std::nullopt;
    }
    return FieldCone{.center = *center, .radiusDegrees = *radius};
}

void populateCatalogRegionReadiness(SpccProcessReadinessReport& report,
                                    const an::ui::SpccCatalogDirectoryInspection& inspection,
                                    const an::ExecutionContext* execution, const SpccReadinessInputs& inputs)
{
    report.catalogRegionChecked = false;
    report.catalogRegionAvailable = true;
    report.catalogChunkDownloadRequired = false;
    report.catalogChunkAutoDownloadAvailable = false;
    report.catalogRequiredChunkCount = 0;
    report.catalogMissingChunkCount = 0;
    report.catalogMissingChunkBytes = 0;
    report.catalogRegionMessage.clear();
    if (!inputs.image.has_value() || execution == nullptr)
    {
        return;
    }
    const std::optional<FieldCone> cone = fieldConeOf(*inputs.image, inputs.matchRadiusArcsec);
    if (!cone.has_value())
    {
        return;
    }

    if (!report.catalogReady)
    {
        if (inspection.status != an::ui::SpccCatalogDirectoryStatus::Missing)
        {
            return;
        }

        const std::optional<an::ui::CatalogPackDescriptor> descriptor = preferredManagedSpccCatalogDescriptor();
        if (!descriptor.has_value())
        {
            return;
        }

        report.catalogDirectory = defaultManagedSpccCatalogInstallDirectory(*descriptor);
        report.catalogManifestId = descriptor->id;
        report.catalogAccessProfile = descriptor->accessProfile;
        report.catalogRightsApproved = true;
        report.catalogRegionChecked = true;
        report.catalogRegionAvailable = false;
        report.catalogChunkDownloadRequired = true;
        report.catalogChunkAutoDownloadAvailable = true;
        report.catalogMessage =
            QCoreApplication::translate("SpccProcessReadiness", "SPCC catalog will be prepared on demand from %1.")
                .arg(catalogPackDisplayName(*descriptor));
        report.catalogRegionMessage = QCoreApplication::translate(
            "SpccProcessReadiness",
            "SPCC will download catalog metadata and only the catalog chunks needed for this image when Run starts.");
        return;
    }

    const bool localFull = inspection.accessProfile.compare(QString::fromLatin1(an::ui::SPCC_LOCAL_FULL_ACCESS_PROFILE),
                                                            Qt::CaseInsensitive) == 0;
    const bool fullSky =
        localFull && inspection.expectedTileCount > 0 && inspection.declaredTileCount >= inspection.expectedTileCount;
    if (fullSky && inspection.status == an::ui::SpccCatalogDirectoryStatus::Ready)
    {
        report.catalogRegionChecked = true;
        report.catalogRegionAvailable = true;
        return;
    }

    if (inspection.chunkTileRanges.empty())
    {
        return;
    }

    const an::SpectrumCatalogManifest manifest = an::ui::spccCatalogManifestFromInspection(inspection);
    const an::Result<an::SpectrumChunkSelection> selection =
        an::selectSpectrumChunksForRegion(*execution, manifest, cone->center, cone->radiusDegrees);
    report.catalogRegionChecked = true;
    report.catalogRegionAvailable = selection.hasValue() && selection->declaredCoverageComplete();
    if (report.catalogRegionAvailable)
    {
        const an::ui::SpccCatalogRequiredChunkAvailability availability =
            an::ui::spccCatalogRequiredChunkAvailabilityForRegion(*execution, inspection,
                                                                  cone->center.rightAscensionDegrees,
                                                                  cone->center.declinationDegrees, cone->radiusDegrees);
        if (availability.valid)
        {
            report.catalogRequiredChunkCount = availability.requiredChunkIndices.size();
            report.catalogMissingChunkCount = availability.missingChunkIndices.size();
            report.catalogMissingChunkBytes = availability.missingBytes;
            if (!availability.allRequiredChunksCached())
            {
                report.catalogRegionAvailable = false;
                report.catalogChunkDownloadRequired = true;
                report.catalogChunkAutoDownloadAvailable = hasApprovedManagedSpccCatalogDescriptor(inspection);
                report.catalogRegionMessage =
                    report.catalogChunkAutoDownloadAvailable
                        ? QCoreApplication::translate(
                              "SpccProcessReadiness",
                              "SPCC will download %1 missing catalog chunk(s) for this image when Run starts.")
                              .arg(report.catalogMissingChunkCount)
                        : QCoreApplication::translate(
                              "SpccProcessReadiness",
                              "SPCC needs %1 missing catalog chunk(s) for this image, but no approved managed "
                              "download descriptor is configured for this catalog.")
                              .arg(report.catalogMissingChunkCount);
            }
        }
    }
    if (!report.catalogRegionAvailable)
    {
        if (report.catalogChunkDownloadRequired)
        {
            return;
        }
        const an::SpectrumCoverageSummary coverage = an::summarizeSpectrumCatalogCoverage(manifest);
        report.catalogRegionMessage =
            QCoreApplication::translate(
                "SpccProcessReadiness",
                "The installed SPCC regional cache does not cover this WCS footprint "
                "(RA=%1 deg, Dec=%2 deg, radius=%3 deg; declared chunks %4/%5 and tiles %6/%7).")
                .arg(cone->center.rightAscensionDegrees, 0, 'f', 4)
                .arg(cone->center.declinationDegrees, 0, 'f', 4)
                .arg(cone->radiusDegrees, 0, 'f', 3)
                .arg(static_cast<qulonglong>(coverage.declaredChunks))
                .arg(static_cast<qulonglong>(coverage.expectedParentChunks))
                .arg(static_cast<qulonglong>(coverage.declaredTiles))
                .arg(static_cast<qulonglong>(coverage.expectedTiles));
    }
}

[[nodiscard]] QString responseMessageForStatus(const an::ui::SpccResponseRepositoryInspection& inspection)
{
    switch (inspection.status)
    {
    case an::ui::SpccResponseRepositoryStatus::Ready:
        return QCoreApplication::translate("SpccProcessReadiness", "SPCC response repository ready: %1 (%2 profiles).")
            .arg(inspection.repositoryId)
            .arg(inspection.throughputProfileCount);

    case an::ui::SpccResponseRepositoryStatus::IncompleteInstall:
        return inspection.diagnostic.isEmpty()
                   ? QCoreApplication::translate("SpccProcessReadiness",
                                                 "The selected SPCC response repository is incomplete.")
                   : inspection.diagnostic;

    case an::ui::SpccResponseRepositoryStatus::Invalid:
        return inspection.diagnostic.isEmpty()
                   ? QCoreApplication::translate("SpccProcessReadiness",
                                                 "The selected directory is not a valid SPCC response repository.")
                   : inspection.diagnostic;

    case an::ui::SpccResponseRepositoryStatus::Missing:
    default:
        return inspection.diagnostic.isEmpty()
                   ? QCoreApplication::translate("SpccProcessReadiness", "No SPCC response repository is selected.")
                   : inspection.diagnostic;
    }
}

void populateResponseAssetsDownloadReadiness(SpccProcessReadinessReport& report,
                                             const an::ui::SpccResponseRepositoryInspection& inspection)
{
    report.responseAssetsDownloadRequired = false;
    report.responseAssetsAutoDownloadAvailable = false;
    report.responseAssetsDownloadBytes = 0;
    report.responseAssetsDownloadName.clear();

    if (report.responseAssetsReady)
    {
        return;
    }

    if (inspection.status != an::ui::SpccResponseRepositoryStatus::Missing &&
        inspection.status != an::ui::SpccResponseRepositoryStatus::IncompleteInstall)
    {
        return;
    }

    const std::optional<an::ui::SpccResponseRepositoryDescriptor> repository = preferredManagedSpccResponseRepository();
    if (!repository.has_value())
    {
        return;
    }

    report.responseAssetsDownloadRequired = true;
    report.responseAssetsAutoDownloadAvailable = true;
    report.responseAssetsDownloadBytes = repository->downloadSizeBytes;
    report.responseAssetsDownloadName = responseRepositoryDisplayName(*repository);
    report.responseRepositoryId = repository->repositoryId;
    report.responseRepositoryVersion = repository->version;
    report.responseAssetsMessage =
        QCoreApplication::translate(
            "SpccProcessReadiness",
            "Download SPCC response profiles to choose the camera/sensor/filter response before running: %1 (%2).")
            .arg(report.responseAssetsDownloadName, formatSpccDataBytes(repository->downloadSizeBytes));
}

[[nodiscard]] AnInfoBanner::Tone toneForReport(const SpccProcessReadinessReport& report)
{
    switch (report.severity())
    {
    case SpccProcessReadinessSeverity::Ready:
        return AnInfoBanner::Tone::Success;
    case SpccProcessReadinessSeverity::Warning:
        return AnInfoBanner::Tone::Warning;
    case SpccProcessReadinessSeverity::Blocked:
    default:
        return AnInfoBanner::Tone::Error;
    }
}

[[nodiscard]] QString imageStateSummaryForReport(const SpccProcessReadinessReport& report)
{
    if (report.imageStateAlreadyColorCalibrated)
    {
        return QCoreApplication::translate(
            "SpccProcessReadiness",
            "SPCC input image is already color calibrated. Choose a linear RGB image to run SPCC again.");
    }

    return QCoreApplication::translate("SpccProcessReadiness", "%1 %2")
        .arg(report.responseProfileMessage, report.imageStateMessage);
}

[[nodiscard]] QString qStringFromStdString(const std::string& text)
{
    return QString::fromStdString(text);
}

struct ResponseProfileChoice
{
    QString profileId;
    QString profilePrefix;
    QString family;
    QString sensorId;
    QString oscFilterId;
    QString oscLpfId;
    QString monoRedFilterId;
    QString monoGreenFilterId;
    QString monoBlueFilterId;
};

[[nodiscard]] QString withoutSuffix(QString text, const QString& suffix)
{
    if (text.endsWith(suffix, Qt::CaseInsensitive))
    {
        text.chop(suffix.size());
    }
    return text;
}

[[nodiscard]] QString displayNameToken(const QString& token)
{
    const QString lower = token.trimmed().toLower();
    if (lower.isEmpty())
    {
        return {};
    }

    static const QStringList uppercaseTokens = {
        QStringLiteral("rgb"),  QStringLiteral("uv"),  QStringLiteral("ir"),  QStringLiteral("lpf"),
        QStringLiteral("cmos"), QStringLiteral("ccd"), QStringLiteral("ha"),  QStringLiteral("oiii"),
        QStringLiteral("sii"),  QStringLiteral("d65"), QStringLiteral("slr"), QStringLiteral("dslr"),
        QStringLiteral("eos"),  QStringLiteral("ii"),  QStringLiteral("iii"), QStringLiteral("iv"),
        QStringLiteral("v"),
    };
    const bool containsDigit =
        std::any_of(lower.cbegin(), lower.cend(), [](const QChar character) { return character.isDigit(); });
    if (uppercaseTokens.contains(lower) || containsDigit)
    {
        return lower.toUpper();
    }

    return lower.left(1).toUpper() + lower.mid(1);
}

[[nodiscard]] QString matchingPrefix(const QString& text, const QStringList& prefixes)
{
    for (const QString& prefix : prefixes)
    {
        if (text.startsWith(prefix, Qt::CaseInsensitive))
        {
            return prefix;
        }
    }
    return {};
}

[[nodiscard]] QString responseComponentDisplayName(QString componentId)
{
    componentId = componentId.trimmed();
    if (componentId.isEmpty())
    {
        return {};
    }

    const QString original = componentId;
    const QStringList prefixes = {
        QStringLiteral("afternight.spcc.profile."),         QStringLiteral("afternight.spcc.osc_sensor."),
        QStringLiteral("afternight.spcc.osc_filter."),      QStringLiteral("afternight.spcc.osc_lpf."),
        QStringLiteral("afternight.spcc.mono_sensor."),     QStringLiteral("afternight.spcc.mono_filter."),
        QStringLiteral("afternight.spcc.white_reference."), QStringLiteral("profile.synthetic."),
    };
    const QString prefix = matchingPrefix(componentId, prefixes);
    if (!prefix.isEmpty())
    {
        componentId = componentId.mid(prefix.size());
    }

    componentId = withoutSuffix(componentId, QStringLiteral(".default"));
    componentId.replace(QLatin1Char('.'), QLatin1Char(' '));
    componentId.replace(QLatin1Char('_'), QLatin1Char(' '));

    QStringList words;
    for (const QString& word : componentId.split(QLatin1Char(' '), Qt::SkipEmptyParts))
    {
        words.append(displayNameToken(word));
    }
    const QString displayName = words.join(QLatin1Char(' ')).trimmed();
    return displayName.isEmpty() ? original : displayName;
}

void appendUnique(QStringList& values, const QString& value)
{
    const QString trimmed = value.trimmed();
    if (!values.contains(trimmed))
    {
        values.append(trimmed);
    }
}

[[nodiscard]] QString withoutTrailingProfileSuffix(const QString& text)
{
    const int suffixIndex = text.lastIndexOf(QLatin1Char('.'));
    if (suffixIndex <= 0)
    {
        return text.trimmed();
    }
    return text.left(suffixIndex).trimmed();
}

[[nodiscard]] QString trailingProfileSuffix(const QString& text)
{
    const int suffixIndex = text.lastIndexOf(QLatin1Char('.'));
    if (suffixIndex < 0 || suffixIndex + 1 >= text.size())
    {
        return {};
    }
    return text.mid(suffixIndex + 1).trimmed();
}

[[nodiscard]] QString firstNonEmpty(const QStringList& values)
{
    for (const QString& value : values)
    {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty())
        {
            return trimmed;
        }
    }
    return {};
}

[[nodiscard]] QString monoFilterIdFromComponent(QString componentId, const QString& channel)
{
    componentId = componentId.trimmed();
    const QString prefix = matchingPrefix(componentId, {QStringLiteral("afternight.spcc.mono_filter.")});
    if (prefix.isEmpty())
    {
        return {};
    }

    QString filterId = componentId.mid(prefix.size());
    const QString channelMarker = QStringLiteral(".%1").arg(channel);
    const int channelMarkerIndex = filterId.indexOf(channelMarker, 0, Qt::CaseInsensitive);
    if (channelMarkerIndex >= 0 && (channelMarkerIndex + channelMarker.size() == filterId.size() ||
                                    filterId.at(channelMarkerIndex + channelMarker.size()) == QLatin1Char('.')))
    {
        filterId = filterId.left(channelMarkerIndex);
    }
    return filterId.trimmed();
}

void applyComponentSet(ResponseProfileChoice& choice, const SpccResponseProfileComponentSet& components)
{
    if (choice.family != QStringLiteral("mono"))
    {
        return;
    }

    const QString redFilter = firstNonEmpty([&components]() {
        QStringList filters;
        for (const QString& componentId : components.red)
        {
            appendUnique(filters, monoFilterIdFromComponent(componentId, QStringLiteral("red")));
        }
        return filters;
    }());
    const QString greenFilter = firstNonEmpty([&components]() {
        QStringList filters;
        for (const QString& componentId : components.green)
        {
            appendUnique(filters, monoFilterIdFromComponent(componentId, QStringLiteral("green")));
        }
        return filters;
    }());
    const QString blueFilter = firstNonEmpty([&components]() {
        QStringList filters;
        for (const QString& componentId : components.blue)
        {
            appendUnique(filters, monoFilterIdFromComponent(componentId, QStringLiteral("blue")));
        }
        return filters;
    }());

    if (!redFilter.isEmpty())
    {
        choice.monoRedFilterId = redFilter;
    }
    if (!greenFilter.isEmpty())
    {
        choice.monoGreenFilterId = greenFilter;
    }
    if (!blueFilter.isEmpty())
    {
        choice.monoBlueFilterId = blueFilter;
    }
}

[[nodiscard]] QString whiteReferenceFallbackDisplayName(const QString& whiteReferenceId)
{
    const QString id = whiteReferenceId.trimmed();
    const QString lower = id.toLower();
    if (lower.endsWith(QStringLiteral(".avgspiral")) || lower.endsWith(QStringLiteral(".average_spiral_galaxy")))
    {
        return QStringLiteral("Average Spiral Galaxy");
    }
    if (lower.endsWith(QStringLiteral(".g2v")))
    {
        return QStringLiteral("G2V Star");
    }
    return responseComponentDisplayName(id);
}

[[nodiscard]] QString whiteReferenceDisplayName(const SpccProcessReadinessReport& report,
                                                const QString& whiteReferenceId)
{
    const QString id = whiteReferenceId.trimmed();
    const QString displayName = report.availableWhiteReferenceDisplayNames.value(id).trimmed();
    return displayName.isEmpty() ? whiteReferenceFallbackDisplayName(id) : displayName;
}

[[nodiscard]] std::optional<ResponseProfileChoice> parseResponseProfileChoice(const QString& profileId)
{
    const QString id = profileId.trimmed();
    if (id.isEmpty())
    {
        return std::nullopt;
    }

    const QString oscPrefix = matchingPrefix(id, {QStringLiteral("afternight.spcc.profile.osc_sensor.")});
    const QString monoPrefix = matchingPrefix(id, {QStringLiteral("afternight.spcc.profile.mono_sensor.")});
    const QString oscFilterMarker = QStringLiteral(".osc_filter.");
    const QString oscLpfMarker = QStringLiteral(".osc_lpf.");
    const QString monoFilterMarker = QStringLiteral(".mono_filter.");

    if (!oscPrefix.isEmpty())
    {
        const QString rest = id.mid(oscPrefix.size());
        ResponseProfileChoice choice;
        choice.profileId = id;
        choice.profilePrefix = oscPrefix;
        choice.family = QStringLiteral("osc");

        const int filterIndex = rest.indexOf(oscFilterMarker, 0, Qt::CaseInsensitive);
        const int lpfIndex = rest.indexOf(oscLpfMarker, 0, Qt::CaseInsensitive);
        const int firstMarkerIndex =
            filterIndex >= 0 && lpfIndex >= 0 ? (std::min)(filterIndex, lpfIndex) : (std::max)(filterIndex, lpfIndex);
        choice.sensorId = firstMarkerIndex >= 0 ? rest.left(firstMarkerIndex) : withoutTrailingProfileSuffix(rest);

        if (filterIndex >= 0)
        {
            const int startIndex = filterIndex + oscFilterMarker.size();
            const int endIndex = lpfIndex > filterIndex ? lpfIndex : rest.lastIndexOf(QLatin1Char('.'));
            choice.oscFilterId = rest.mid(startIndex, (std::max)(0, endIndex - startIndex)).trimmed();
        }
        if (lpfIndex >= 0)
        {
            const int startIndex = lpfIndex + oscLpfMarker.size();
            const int endIndex = rest.lastIndexOf(QLatin1Char('.'));
            choice.oscLpfId = rest.mid(startIndex, (std::max)(0, endIndex - startIndex)).trimmed();
        }

        return choice.sensorId.trimmed().isEmpty() ? std::nullopt : std::optional<ResponseProfileChoice>(choice);
    }

    if (!monoPrefix.isEmpty())
    {
        const QString rest = id.mid(monoPrefix.size());
        ResponseProfileChoice choice;
        choice.profileId = id;
        choice.profilePrefix = monoPrefix;
        choice.family = QStringLiteral("mono");

        const int filterIndex = rest.indexOf(monoFilterMarker, 0, Qt::CaseInsensitive);
        if (filterIndex >= 0)
        {
            choice.sensorId = rest.left(filterIndex);
            const QString filterId = withoutTrailingProfileSuffix(rest.mid(filterIndex + monoFilterMarker.size()));
            choice.monoRedFilterId = filterId;
            choice.monoGreenFilterId = filterId;
            choice.monoBlueFilterId = filterId;
        }
        else
        {
            choice.sensorId = withoutTrailingProfileSuffix(rest);
        }

        return choice.sensorId.trimmed().isEmpty() ? std::nullopt : std::optional<ResponseProfileChoice>(choice);
    }

    return std::nullopt;
}

[[nodiscard]] QString responseChoiceSensorKey(const ResponseProfileChoice& choice)
{
    return QStringLiteral("%1|%2").arg(choice.family, choice.sensorId);
}

[[nodiscard]] QString responseChoiceSensorLabel(const ResponseProfileChoice& choice)
{
    const QString sensor = responseComponentDisplayName(choice.sensorId);
    if (choice.family == QStringLiteral("mono"))
    {
        return QCoreApplication::translate("SpccProcessReadiness", "Mono sensor: %1").arg(sensor);
    }
    return QCoreApplication::translate("SpccProcessReadiness", "OSC sensor: %1").arg(sensor);
}

[[nodiscard]] QString responseChoiceFilterLabel(const ResponseProfileChoice& choice)
{
    if (choice.family == QStringLiteral("mono"))
    {
        if (choice.monoRedFilterId == choice.monoGreenFilterId && choice.monoGreenFilterId == choice.monoBlueFilterId)
        {
            return QCoreApplication::translate("SpccProcessReadiness", "RGB filters: %1")
                .arg(responseComponentDisplayName(choice.monoRedFilterId));
        }
        return QCoreApplication::translate("SpccProcessReadiness", "R: %1, G: %2, B: %3")
            .arg(responseComponentDisplayName(choice.monoRedFilterId),
                 responseComponentDisplayName(choice.monoGreenFilterId),
                 responseComponentDisplayName(choice.monoBlueFilterId));
    }

    QStringList labels;
    if (!choice.oscFilterId.isEmpty())
    {
        labels.append(QCoreApplication::translate("SpccProcessReadiness", "OSC filter: %1")
                          .arg(responseComponentDisplayName(choice.oscFilterId)));
    }
    if (!choice.oscLpfId.isEmpty())
    {
        labels.append(QCoreApplication::translate("SpccProcessReadiness", "DSLR LPF: %1")
                          .arg(responseComponentDisplayName(choice.oscLpfId)));
    }
    return labels.isEmpty() ? QCoreApplication::translate("SpccProcessReadiness", "No additional filter")
                            : labels.join(QStringLiteral(" / "));
}

[[nodiscard]] QString responseProfileDisplayText(const QString& profileId)
{
    if (const auto choice = parseResponseProfileChoice(profileId); choice.has_value())
    {
        return QStringLiteral("%1 / %2").arg(responseChoiceSensorLabel(*choice), responseChoiceFilterLabel(*choice));
    }
    return responseComponentDisplayName(profileId);
}

[[nodiscard]] QString selectedResponseProfileId(const SpccProcessReadinessReport& report)
{
    return report.explicitResponseProfileId.trimmed().isEmpty() ? report.responseProfileId.trimmed()
                                                                : report.explicitResponseProfileId.trimmed();
}

[[nodiscard]] QString selectedWhiteReferenceId(const SpccProcessReadinessReport& report)
{
    return report.explicitWhiteReferenceId.trimmed().isEmpty() ? report.whiteReferenceId.trimmed()
                                                               : report.explicitWhiteReferenceId.trimmed();
}

[[nodiscard]] QStringList orderedResponseProfileIds(const SpccProcessReadinessReport& report)
{
    QStringList ids;
    const auto appendUnique = [&ids](const QString& profileId) {
        const QString trimmed = profileId.trimmed();
        if (!trimmed.isEmpty() && !ids.contains(trimmed))
        {
            ids.append(trimmed);
        }
    };

    for (const QString& profileId : report.candidateResponseProfileIds)
    {
        appendUnique(profileId);
    }
    for (const QString& profileId : report.availableResponseProfileIds)
    {
        appendUnique(profileId);
    }
    appendUnique(selectedResponseProfileId(report));
    return ids;
}

[[nodiscard]] QStringList orderedWhiteReferenceIds(const SpccProcessReadinessReport& report)
{
    QStringList ids;
    const auto appendUnique = [&ids](const QString& whiteReferenceId) {
        const QString trimmed = whiteReferenceId.trimmed();
        if (!trimmed.isEmpty() && !ids.contains(trimmed))
        {
            ids.append(trimmed);
        }
    };

    for (const QString& whiteReferenceId : report.availableWhiteReferenceIds)
    {
        appendUnique(whiteReferenceId);
    }
    appendUnique(selectedWhiteReferenceId(report));
    return ids;
}

[[nodiscard]] std::vector<ResponseProfileChoice>
structuredResponseProfileChoices(const SpccProcessReadinessReport& report, const QStringList& profileIds)
{
    std::vector<ResponseProfileChoice> choices;
    choices.reserve(static_cast<std::size_t>(profileIds.size()));
    for (const QString& profileId : profileIds)
    {
        if (const auto choice = parseResponseProfileChoice(profileId); choice.has_value())
        {
            ResponseProfileChoice enrichedChoice = *choice;
            applyComponentSet(enrichedChoice, report.availableResponseProfileComponents.value(profileId));
            choices.push_back(std::move(enrichedChoice));
        }
    }
    return choices;
}

[[nodiscard]] std::vector<ResponseProfileChoice> choicesForSensor(const std::vector<ResponseProfileChoice>& choices,
                                                                  const QString& sensorKey)
{
    std::vector<ResponseProfileChoice> result;
    for (const ResponseProfileChoice& choice : choices)
    {
        if (responseChoiceSensorKey(choice) == sensorKey)
        {
            result.push_back(choice);
        }
    }
    return result;
}

[[nodiscard]] QString sensorFamilyFromKey(const QString& sensorKey)
{
    const int separator = sensorKey.indexOf(QLatin1Char('|'));
    return separator < 0 ? QString() : sensorKey.left(separator);
}

[[nodiscard]] QStringList oscFilterOptions(const std::vector<ResponseProfileChoice>& choices)
{
    QStringList values;
    for (const ResponseProfileChoice& choice : choices)
    {
        if (choice.oscFilterId.isEmpty())
        {
            appendUnique(values, choice.oscFilterId);
        }
    }
    for (const ResponseProfileChoice& choice : choices)
    {
        if (!choice.oscFilterId.isEmpty())
        {
            appendUnique(values, choice.oscFilterId);
        }
    }
    return values;
}

[[nodiscard]] QStringList oscLpfOptions(const std::vector<ResponseProfileChoice>& choices)
{
    QStringList values;
    for (const ResponseProfileChoice& choice : choices)
    {
        if (choice.oscLpfId.isEmpty())
        {
            appendUnique(values, choice.oscLpfId);
        }
    }
    for (const ResponseProfileChoice& choice : choices)
    {
        if (!choice.oscLpfId.isEmpty())
        {
            appendUnique(values, choice.oscLpfId);
        }
    }
    return values;
}

[[nodiscard]] QStringList monoFilterOptions(const std::vector<ResponseProfileChoice>& choices, const QString& channel)
{
    QStringList values;
    for (const ResponseProfileChoice& choice : choices)
    {
        if (channel == QStringLiteral("red"))
        {
            appendUnique(values, choice.monoRedFilterId);
        }
        else if (channel == QStringLiteral("green"))
        {
            appendUnique(values, choice.monoGreenFilterId);
        }
        else
        {
            appendUnique(values, choice.monoBlueFilterId);
        }
    }
    return values;
}

[[nodiscard]] QString firstOptionOrEmpty(const QStringList& options)
{
    return options.isEmpty() ? QString() : options.front();
}

[[nodiscard]] QString selectedValueInOptionsOrFirst(const QString& selectedValue, const QStringList& options)
{
    return options.contains(selectedValue) ? selectedValue : firstOptionOrEmpty(options);
}

[[nodiscard]] QString matchingOscProfileId(const std::vector<ResponseProfileChoice>& choices, const QString& filterId,
                                           const QString& lpfId)
{
    for (const ResponseProfileChoice& choice : choices)
    {
        if (choice.oscFilterId == filterId && choice.oscLpfId == lpfId)
        {
            return choice.profileId;
        }
    }

    if (filterId.isEmpty() || lpfId.isEmpty() || choices.empty())
    {
        return {};
    }

    bool hasBaseProfile = false;
    bool hasFilterProfile = false;
    bool hasLpfProfile = false;
    QString suffix;
    QString profilePrefix;
    for (const ResponseProfileChoice& choice : choices)
    {
        if (choice.oscFilterId.isEmpty() && choice.oscLpfId.isEmpty())
        {
            hasBaseProfile = true;
        }
        if (choice.oscFilterId == filterId && choice.oscLpfId.isEmpty())
        {
            hasFilterProfile = true;
            if (suffix.isEmpty())
            {
                suffix = trailingProfileSuffix(choice.profileId);
                profilePrefix = choice.profilePrefix;
            }
        }
        if (choice.oscFilterId.isEmpty() && choice.oscLpfId == lpfId)
        {
            hasLpfProfile = true;
            if (suffix.isEmpty())
            {
                suffix = trailingProfileSuffix(choice.profileId);
                profilePrefix = choice.profilePrefix;
            }
        }
    }

    if (!hasBaseProfile || !hasFilterProfile || !hasLpfProfile || suffix.isEmpty())
    {
        return {};
    }

    if (profilePrefix.isEmpty())
    {
        profilePrefix = choices.front().profilePrefix;
    }
    if (profilePrefix.isEmpty())
    {
        profilePrefix = QStringLiteral("afternight.spcc.profile.osc_sensor.");
    }

    return QStringLiteral("%1%2.osc_filter.%3.osc_lpf.%4.%5")
        .arg(profilePrefix, choices.front().sensorId, filterId, lpfId, suffix);
}

[[nodiscard]] QString matchingMonoProfileId(const std::vector<ResponseProfileChoice>& choices, const QString& redId,
                                            const QString& greenId, const QString& blueId)
{
    for (const ResponseProfileChoice& choice : choices)
    {
        if (choice.monoRedFilterId == redId && choice.monoGreenFilterId == greenId && choice.monoBlueFilterId == blueId)
        {
            return choice.profileId;
        }
    }
    return {};
}

void configureLongResponseComboBox(QComboBox* comboBox)
{
    if (comboBox == nullptr)
    {
        return;
    }

    comboBox->setMinimumWidth(0);
    comboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    comboBox->setProperty("anComboPopupAnchored", "true");
    comboBox->setMaxVisibleItems(12);
    comboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    comboBox->setMinimumContentsLength(8);
    if (auto* view = comboBox->view())
    {
        view->setProperty("anComboPopupAnchored", "true");
        view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
}

[[nodiscard]] QLabel* makeResponseFieldLabel(const QString& text, const QString& objectName, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(objectName);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return label;
}

void setResponseFieldVisible(QLabel* label, QWidget* field, const bool visible)
{
    if (label)
    {
        label->setVisible(visible);
    }
    if (field)
    {
        field->setVisible(visible);
        field->setEnabled(visible);
    }
}

[[nodiscard]] QString finiteNumberOrDash(const std::optional<double>& value, const int precision = 3)
{
    return value.has_value() && std::isfinite(*value) ? QString::number(*value, 'f', precision) : QStringLiteral("-");
}

[[nodiscard]] QString fitVerdictText(const QString& verdict)
{
    if (verdict == QStringLiteral("InsufficientSamples"))
    {
        return QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "insufficient samples");
    }
    if (verdict == QStringLiteral("InsufficientCatalogSpan"))
    {
        return QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "insufficient color span");
    }
    if (verdict == QStringLiteral("InsufficientInliers"))
    {
        return QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "insufficient inliers");
    }
    if (verdict == QStringLiteral("ExcessiveGainUncertainty"))
    {
        return QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "excessive gain uncertainty");
    }
    if (verdict == QStringLiteral("NonFiniteFit"))
    {
        return QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "non-finite fit");
    }
    return verdict.isEmpty() ? QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "unknown")
                             : verdict;
}

void appendDiagnostics(QStringList& lines, const QString& label, const QStringList& diagnostics)
{
    QStringList kept;
    for (const QString& diagnostic : diagnostics)
    {
        if (!diagnostic.trimmed().isEmpty())
        {
            kept.append(QStringLiteral("  - %1").arg(diagnostic.trimmed()));
        }
    }
    if (!kept.isEmpty())
    {
        lines.append(label);
        lines.append(kept);
    }
}

[[nodiscard]] QString sourceIdListText(const std::vector<std::uint64_t>& sourceIds)
{
    QStringList ids;
    for (const std::uint64_t sourceId : sourceIds)
    {
        ids.append(QString::number(static_cast<qulonglong>(sourceId)));
    }
    return ids.join(QStringLiteral(", "));
}

[[nodiscard]] int qualityScoreForSummary(const SpccRunSummary& summary)
{
    if (!summary.calibrated)
    {
        return summary.acceptedSamples >= summary.usableFitSamples ? 40 : 30;
    }

    int score = 50;
    if (summary.acceptedSamples >= 80U)
    {
        score += 25;
    }
    else if (summary.acceptedSamples >= 35U)
    {
        score += 15;
    }
    else if (summary.acceptedSamples >= 20U)
    {
        score += 5;
    }

    if (summary.rmsResidualPixels.has_value() && std::isfinite(*summary.rmsResidualPixels) &&
        *summary.rmsResidualPixels <= 1.0)
    {
        score += 15;
    }
    else if (summary.rmsResidualPixels.has_value() && std::isfinite(*summary.rmsResidualPixels) &&
             *summary.rmsResidualPixels <= summary.residualThresholdPixels)
    {
        score += 8;
    }

    if (summary.rejectedOutlierSources.empty())
    {
        score += 10;
    }
    else if (summary.rejectedOutlierSources.size() <= 5U)
    {
        score += 5;
    }
    return std::clamp(score, 0, 100);
}

/// The reason the engine's compatibility verdict states, as the window says it.
[[nodiscard]] QString compatibilityText(const an::ResponseCompatibilityVerdict verdict)
{
    switch (verdict)
    {
    case an::ResponseCompatibilityVerdict::MissingIdentity:
        return QCoreApplication::translate("SpccProcessReadiness",
                                           "SPCC response profile or white reference ID is missing.");
    case an::ResponseCompatibilityVerdict::UnreviewedAsset:
        return QCoreApplication::translate("SpccProcessReadiness",
                                           "SPCC response profile asset is private or lacks review approval.");
    case an::ResponseCompatibilityVerdict::IncompatibleColorSpace:
        return QCoreApplication::translate("SpccProcessReadiness",
                                           "SPCC response profile is incompatible with the image channel basis.");
    case an::ResponseCompatibilityVerdict::RawWhiteBalanceNotModelled:
        return QCoreApplication::translate("SpccProcessReadiness",
                                           "SPCC response profile does not model the applied RAW white balance.");
    case an::ResponseCompatibilityVerdict::CameraMatrixNotModelled:
        return QCoreApplication::translate("SpccProcessReadiness",
                                           "SPCC response profile does not model the applied camera matrix.");
    case an::ResponseCompatibilityVerdict::Compatible:
    default:
        return {};
    }
}

/// The reason the engine's input check states, as the window says it.
[[nodiscard]] QString inputProblemText(const an::SpectrophotometricInputProblem problem)
{
    switch (problem)
    {
    case an::SpectrophotometricInputProblem::UnknownLinearity:
        return QCoreApplication::translate("SpccProcessReadiness", "SPCC input has unknown linearity state.");
    case an::SpectrophotometricInputProblem::Nonlinear:
        return QCoreApplication::translate("SpccProcessReadiness", "SPCC input is marked nonlinear.");
    case an::SpectrophotometricInputProblem::IncompatibleChannelBasis:
        return QCoreApplication::translate("SpccProcessReadiness",
                                           "SPCC input has incompatible or unknown channel basis.");
    case an::SpectrophotometricInputProblem::IncompleteAttestation:
        return QCoreApplication::translate("SpccProcessReadiness",
                                           "SPCC input has incomplete user-attested state provenance.");
    case an::SpectrophotometricInputProblem::AlreadyColorCalibrated:
        return QCoreApplication::translate("SpccProcessReadiness", "SPCC input is already color calibrated.");
    case an::SpectrophotometricInputProblem::None:
    default:
        return {};
    }
}

bool populateResolvedResponseProfile(SpccProcessReadinessReport& report, const SpccRepositoryProfile& profile,
                                     const SpccRepositoryWhiteReference& whiteReference, const bool manualSelection,
                                     const bool savedMapping, const SpccReadinessInputs& inputs)
{
    report.responseProfileAmbiguous = false;
    report.responseProfileManualSelection = manualSelection;
    report.responseProfileSavedMapping = savedMapping;
    report.responseProfileId = QString::fromStdString(profile.profile.identifier);
    report.responseProfileVersion = QString::fromStdString(profile.profile.version);
    report.whiteReferenceId = QString::fromStdString(whiteReference.spectrum.identifier);
    report.whiteReferenceVersion = QString::fromStdString(whiteReference.spectrum.version);

    if (!profile.capabilities.reviewed)
    {
        report.responseProfileMessage = QCoreApplication::translate(
            "SpccProcessReadiness", "The selected SPCC response profile uses private or unreviewed response assets.");
        return false;
    }

    if (inputs.image.has_value())
    {
        const an::ProcessingStateRecord state =
            an::ui::processes::declaredCalibrationState(inputs.image->descriptor, inputs.image->metadata);
        if (an::spectrophotometricInputProblem(state) == an::SpectrophotometricInputProblem::None)
        {
            const an::ResponseCompatibilityVerdict verdict = an::checkResponseProfileCompatibility(
                profile.profile, profile.capabilities, an::ui::processes::spccFrameColorState(state));
            if (verdict != an::ResponseCompatibilityVerdict::Compatible)
            {
                report.responseProfileMessage =
                    QCoreApplication::translate(
                        "SpccProcessReadiness",
                        "The selected SPCC response profile is incompatible with this image state: %1")
                        .arg(compatibilityText(verdict));
                return false;
            }
        }
    }

    report.responseProfileResolved = true;
    if (manualSelection)
    {
        report.responseProfileMessage =
            QCoreApplication::translate("SpccProcessReadiness", "SPCC response profile manually selected: %1 with %2.")
                .arg(report.responseProfileId, report.whiteReferenceId);
    }
    else if (savedMapping)
    {
        report.responseProfileMessage =
            QCoreApplication::translate("SpccProcessReadiness",
                                        "SPCC response profile selected from saved mapping: %1 with %2.")
                .arg(report.responseProfileId, report.whiteReferenceId);
    }
    else
    {
        report.responseProfileMessage =
            QCoreApplication::translate("SpccProcessReadiness", "SPCC response profile selected: %1 with %2.")
                .arg(report.responseProfileId, report.whiteReferenceId);
    }
    return true;
}

/// The saved mapping that names a camera and filter in a repository's alias registry, or nothing.
[[nodiscard]] std::optional<SpccResponseProfileMapping>
savedMappingFor(const SpccReadinessInputs& inputs, const QString& cameraModel, const QString& filterName,
                const QString& repositoryId, const QString& aliasRegistryVersion)
{
    for (const SpccResponseProfileMapping& mapping : inputs.savedMappings)
    {
        if (an::ui::spcc_response_detail::mappingMatches(mapping, cameraModel, filterName, repositoryId,
                                                         aliasRegistryVersion))
        {
            return mapping;
        }
    }
    return std::nullopt;
}

void populateResponseProfileReadiness(SpccProcessReadinessReport& report, const an::ExecutionContext* execution,
                                      const SpccReadinessInputs& inputs)
{
    report.responseProfileCameraModel = report.responseProfileCameraModel.trimmed();
    report.responseProfileFilterName = report.responseProfileFilterName.trimmed();
    report.explicitResponseProfileId = report.explicitResponseProfileId.trimmed();
    report.explicitWhiteReferenceId = report.explicitWhiteReferenceId.trimmed();
    report.responseProfileResolved = false;
    report.responseProfileAmbiguous = false;
    report.responseProfileManualSelection = false;
    report.responseProfileSavedMapping = false;
    report.responseProfileId.clear();
    report.responseProfileVersion.clear();
    report.whiteReferenceId.clear();
    report.whiteReferenceVersion.clear();
    report.responseAliasRegistryVersion.clear();
    report.availableResponseProfileIds.clear();
    report.candidateResponseProfileIds.clear();
    report.availableResponseProfileComponents.clear();
    report.availableWhiteReferenceIds.clear();
    report.availableWhiteReferenceDisplayNames.clear();

    if (!report.responseAssetsReady || execution == nullptr)
    {
        return;
    }

    an::Result<SpccResponseRepository> repository =
        SpccResponseRepository::load(*execution, report.responseRepositoryDirectory);
    if (!repository.hasValue())
    {
        report.responseAssetsReady = false;
        report.responseAssetsMessage =
            QCoreApplication::translate("SpccProcessReadiness", "Unable to load SPCC response repository: %1")
                .arg(QString::fromUtf8(repository.status().message()));
        return;
    }

    report.responseAliasRegistryVersion = repository->aliasRegistryVersion();
    for (const SpccRepositoryProfile& profile : repository->profiles())
    {
        const QString profileId = QString::fromStdString(profile.profile.identifier);
        report.availableResponseProfileIds.append(profileId);
        report.availableResponseProfileComponents.insert(
            profileId, SpccResponseProfileComponentSet{.red = profile.channelComponentIds[0],
                                                       .green = profile.channelComponentIds[1],
                                                       .blue = profile.channelComponentIds[2]});
    }
    for (const SpccRepositoryWhiteReference& white : repository->whiteReferences())
    {
        const QString whiteReferenceId = QString::fromStdString(white.spectrum.identifier);
        report.availableWhiteReferenceIds.append(whiteReferenceId);
        if (!white.displayName.isEmpty())
        {
            report.availableWhiteReferenceDisplayNames.insert(whiteReferenceId, white.displayName);
        }
    }

    const auto resolveWhiteReference = [&report,
                                        &repository](const SpccRepositoryProfile& profile,
                                                     const QString& requested) -> const SpccRepositoryWhiteReference* {
        const QString id = requested.trimmed().isEmpty() ? profile.whiteReferenceId : requested.trimmed();
        const SpccRepositoryWhiteReference* white = repository->findWhiteReference(id);
        if (white == nullptr)
        {
            report.whiteReferenceId = id;
            report.responseProfileMessage = QCoreApplication::translate(
                "SpccProcessReadiness", "The selected SPCC white reference is not available in this repository.");
        }
        return white;
    };

    if (!report.explicitResponseProfileId.isEmpty())
    {
        report.responseProfileManualSelection = true;
        an::Result<SpccRepositoryProfile> profile = repository->resolveProfile(report.explicitResponseProfileId);
        if (!profile.hasValue())
        {
            report.responseProfileId = report.explicitResponseProfileId;
            const QString reason = QString::fromUtf8(profile.status().message()).trimmed();
            report.responseProfileMessage =
                reason.isEmpty() ? QCoreApplication::translate(
                                       "SpccProcessReadiness",
                                       "The selected SPCC response profile is not available in this repository.")
                                 : reason;
            return;
        }
        if (const SpccRepositoryWhiteReference* white =
                resolveWhiteReference(*profile, report.explicitWhiteReferenceId))
        {
            populateResolvedResponseProfile(report, *profile, *white, true, false, inputs);
        }
        return;
    }

    if (report.responseProfileCameraModel.isEmpty() && report.responseProfileFilterName.isEmpty())
    {
        report.responseProfileMessage = QCoreApplication::translate(
            "SpccProcessReadiness", "SPCC response profile is unresolved. Select an image or choose a response "
                                    "profile before calibration.");
        return;
    }

    const SpccAliasResolution resolution =
        repository->resolveAlias(report.responseProfileCameraModel, report.responseProfileFilterName);
    report.candidateResponseProfileIds = resolution.candidateProfileIds;

    const std::optional<SpccResponseProfileMapping> savedMapping =
        savedMappingFor(inputs, report.responseProfileCameraModel, report.responseProfileFilterName,
                        report.responseRepositoryId, report.responseAliasRegistryVersion);
    if (savedMapping.has_value())
    {
        an::Result<SpccRepositoryProfile> profile = repository->resolveProfile(savedMapping->profileId);
        if (!profile.hasValue())
        {
            report.responseProfileId = savedMapping->profileId;
            report.responseProfileMessage = QCoreApplication::translate(
                "SpccProcessReadiness", "The saved SPCC response profile mapping points to a profile that is not "
                                        "available in this repository.");
            return;
        }
        const QString mappedWhiteReferenceId = report.explicitWhiteReferenceId.isEmpty()
                                                   ? savedMapping->whiteReferenceId
                                                   : report.explicitWhiteReferenceId;
        if (const SpccRepositoryWhiteReference* white = resolveWhiteReference(*profile, mappedWhiteReferenceId))
        {
            populateResolvedResponseProfile(report, *profile, *white, false, true, inputs);
        }
        return;
    }

    if (resolution.ambiguous)
    {
        report.responseProfileAmbiguous = true;
        report.responseProfileMessage = QCoreApplication::translate(
            "SpccProcessReadiness", "SPCC response profile metadata is ambiguous. Choose one response profile "
                                    "before calibration.");
        return;
    }

    if (!resolution.profileId.has_value())
    {
        report.responseProfileMessage = QCoreApplication::translate(
            "SpccProcessReadiness", "No exact SPCC response profile alias matches this camera/filter metadata.");
        return;
    }

    const SpccRepositoryProfile* profile = repository->findProfile(*resolution.profileId);
    if (profile == nullptr)
    {
        report.responseProfileMessage = QCoreApplication::translate(
            "SpccProcessReadiness", "The matched SPCC response profile is not available in this repository.");
        return;
    }
    if (const SpccRepositoryWhiteReference* white = resolveWhiteReference(*profile, report.explicitWhiteReferenceId))
    {
        populateResolvedResponseProfile(report, *profile, *white, false, false, inputs);
    }
}

void populateImageStateReadiness(SpccProcessReadinessReport& report, const SpccReadinessImage& image,
                                 const bool allowNarrowbandInput)
{
    report.imageStateChecked = true;
    report.imageStateCompatible = false;
    report.imageStateAlreadyColorCalibrated = false;
    report.imageStateMessage.clear();

    if (an::isNarrowbandFilterName(image.metadata.acquisition().filterName()) && !allowNarrowbandInput)
    {
        report.imageStateMessage = QCoreApplication::translate(
            "SpccProcessReadiness",
            "SPCC input image state is not compatible: narrowband filter metadata is outside the current SPCC support "
            "envelope unless narrowband passbands are enabled.");
        return;
    }

    const an::ProcessingStateRecord state =
        an::ui::processes::declaredCalibrationState(image.descriptor, image.metadata);
    const an::SpectrophotometricInputProblem problem = an::spectrophotometricInputProblem(state);
    if (problem == an::SpectrophotometricInputProblem::None)
    {
        report.imageStateCompatible = true;
        report.imageStateMessage =
            QCoreApplication::translate("SpccProcessReadiness", "SPCC input image state is compatible.");
        return;
    }

    report.imageStateAlreadyColorCalibrated = state.colorCalibration != an::RecordedColorCalibration::None;
    report.imageStateMessage =
        QCoreApplication::translate("SpccProcessReadiness", "SPCC input image state is not compatible: %1")
            .arg(inputProblemText(problem));
}

} // namespace

QString spccCatalogsDataSettingsPageTitle()
{
    return QCoreApplication::translate("SpccProcessReadiness", "Catalogs & Data");
}

SpccRunSummary spccRunSummary(const an::SpectrophotometricColorCalibrationResult& result)
{
    SpccRunSummary summary;
    summary.calibrated = result.fit.accepted();
    summary.wcsMatches = result.preflight.report.matchedStarCount;
    summary.rmsResidualPixels = result.preflight.report.rmsResidualPixels;
    summary.rmsResidualArcseconds = result.preflight.report.rmsResidualArcseconds;
    summary.residualThresholdPixels = result.preflight.report.residualThresholdPixels;
    summary.acceptedSamples = result.samples.acceptedCount;
    summary.rejectedSamples = result.samples.rejectedCount;
    summary.usableFitSamples = result.fit.usableCount;
    summary.fitSupport = result.fit.supportCount;
    summary.lowSupport = result.fit.lowSupport;
    if (!result.fit.accepted())
    {
        summary.fitVerdict = QString::fromLatin1(an::spectrophotometricFitVerdictName(result.fit.verdict));
    }
    summary.gains = result.fit.gains;
    summary.offsets = result.applied.offsets;
    for (const std::uint32_t rejected : result.fit.rejectedSampleIndices)
    {
        if (rejected < result.samples.samples.size())
        {
            summary.rejectedOutlierSources.push_back(result.samples.samples[rejected].sourceIdentifier);
        }
    }
    if (result.qualityVerdict != an::WcsQualityVerdict::Acceptable)
    {
        summary.workflowDiagnostics.append(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                        "The WCS quality check reported %1; calibration continued.")
                .arg(QString::fromLatin1(an::wcsQualityVerdictName(result.qualityVerdict))));
    }
    return summary;
}

QString formatSpccWorkflowDiagnostics(const SpccRunSummary& summary)
{
    QStringList lines;
    // A run refused before the engine measured anything has its reason and its guidance, and no figures to show.
    const bool measured = summary.calibrated || summary.wcsMatches > 0u || summary.acceptedSamples > 0u ||
                          summary.usableFitSamples > 0u || !summary.fitVerdict.isEmpty();
    if (measured)
    {
        lines.append(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Quality score: %1/100")
                         .arg(qualityScoreForSummary(summary)));
    }
    if (!summary.message.isEmpty())
    {
        lines.append(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Result: %1")
                         .arg(summary.message));
    }
    if (!measured)
    {
        if (summary.catalogCoverageFailure)
        {
            lines.append(QCoreApplication::translate(
                "SpectrophotometricColorCalibrationProcess",
                "Catalog coverage guidance: install a complete SPCC local-full catalog or an installed regional cache "
                "that covers this WCS footprint."));
        }
        appendDiagnostics(
            lines, QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Workflow diagnostics:"),
            summary.workflowDiagnostics);
        return lines.join(QStringLiteral("\n"));
    }
    lines.append(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                             "WCS matches: %1, RMS residual: %2 px / %3 arcsec")
                     .arg(static_cast<qulonglong>(summary.wcsMatches))
                     .arg(finiteNumberOrDash(summary.rmsResidualPixels))
                     .arg(finiteNumberOrDash(summary.rmsResidualArcseconds)));
    lines.append(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                             "Accepted samples: %1, rejected samples: %2, usable fit samples: %3")
                     .arg(static_cast<qulonglong>(summary.acceptedSamples))
                     .arg(static_cast<qulonglong>(summary.rejectedSamples))
                     .arg(static_cast<qulonglong>(summary.usableFitSamples)));
    lines.append(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Gains R=%1 G=%2 B=%3")
                     .arg(summary.gains[0], 0, 'f', 3)
                     .arg(summary.gains[1], 0, 'f', 3)
                     .arg(summary.gains[2], 0, 'f', 3));
    lines.append(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Offsets R=%1 G=%2 B=%3")
                     .arg(summary.offsets[0], 0, 'f', 5)
                     .arg(summary.offsets[1], 0, 'f', 5)
                     .arg(summary.offsets[2], 0, 'f', 5));

    if (!summary.fitVerdict.isEmpty())
    {
        lines.append(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Fit warning: %1.")
                         .arg(fitVerdictText(summary.fitVerdict)));
    }
    const std::uint32_t support = summary.calibrated ? summary.fitSupport : summary.usableFitSamples;
    if (summary.lowSupport || support < an::kDefaultSpectrophotometricLowSupportSamples)
    {
        if (summary.catalogCoverageFailure)
        {
            lines.append(QCoreApplication::translate(
                "SpectrophotometricColorCalibrationProcess",
                "Catalog coverage guidance: install a complete SPCC local-full catalog or an installed regional cache "
                "that covers this WCS footprint."));
        }
        else
        {
            lines.append(QCoreApplication::translate(
                "SpectrophotometricColorCalibrationProcess",
                "Sparse-field guidance: use a wider plate-solved field, confirm the local SPCC region covers this "
                "target, or increase the magnitude limit."));
        }
    }
    if (!summary.rejectedOutlierSources.empty())
    {
        lines.append(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Rejected outlier source IDs: %1")
                .arg(sourceIdListText(summary.rejectedOutlierSources)));
    }
    appendDiagnostics(lines,
                      QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Workflow diagnostics:"),
                      summary.workflowDiagnostics);
    return lines.join(QStringLiteral("\n"));
}

QString formatSpccProcessFinalizationStatus(const SpccRunSummary& summary, const bool success, const bool cancelled,
                                            const bool targetResultApplied, const QString& errorText)
{
    if (success && targetResultApplied)
    {
        return QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                           "SPCC completed. Samples: %1. Gains R=%2 G=%3 B=%4")
            .arg(static_cast<qulonglong>(summary.acceptedSamples))
            .arg(summary.gains[0], 0, 'f', 3)
            .arg(summary.gains[1], 0, 'f', 3)
            .arg(summary.gains[2], 0, 'f', 3);
    }

    if (success)
    {
        return QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                           "SPCC completed, but the target image was closed or changed before the "
                                           "result could be applied.");
    }

    if (cancelled)
    {
        return QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "SPCC cancelled.");
    }

    const QString failureText = !errorText.trimmed().isEmpty() ? errorText.trimmed() : summary.message.trimmed();
    if (!failureText.isEmpty())
    {
        return QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "SPCC failed. %1")
            .arg(failureText);
    }

    return QString();
}

SpccReadinessInputs spccReadinessInputs(IGlobalSettingsProvider* settings)
{
    SpccReadinessInputs inputs;
    inputs.catalogDirectory = an::ui::resolveSpccCatalogDirectory(settings, false);
    inputs.responseRepositoryDirectory = an::ui::resolveSpccResponseRepositoryDirectory(settings);
    inputs.savedMappings = an::ui::loadSpccResponseProfileMappings(settings);
    return inputs;
}

namespace
{

SpccProcessReadinessReport evaluateReadiness(const an::ExecutionContext* execution, const SpccReadinessInputs& inputs)
{
    SpccProcessReadinessReport report;
    report.catalogDirectory = inputs.catalogDirectory.trimmed();
    report.responseRepositoryDirectory = inputs.responseRepositoryDirectory.trimmed();
    report.responseProfileCameraModel = inputs.cameraModel.trimmed();
    report.responseProfileFilterName = inputs.allowNarrowbandInput ? QString() : inputs.filterName.trimmed();
    report.explicitResponseProfileId = inputs.explicitResponseProfileId.trimmed();
    report.explicitWhiteReferenceId = inputs.explicitWhiteReferenceId.trimmed();
    report.settingsPageTitle = spccCatalogsDataSettingsPageTitle();
    report.actionText = QCoreApplication::translate("SpccProcessReadiness", "Open Catalogs & Data...");

    // Process-window readiness must stay cheap enough to follow every change of the window. Full checksum verification
    // still happens in managed downloads and again on the calibration worker before the catalog is read.
    const an::ui::SpccCatalogDirectoryInspection inspection =
        an::ui::inspectSpccCatalogDirectoryDetailed(report.catalogDirectory, false);
    report.catalogReady = inspection.status == an::ui::SpccCatalogDirectoryStatus::Ready ||
                          inspection.status == an::ui::SpccCatalogDirectoryStatus::PartialCache;
    report.catalogRightsApproved =
        report.catalogReady && an::ui::isApprovedSpccCatalogDescriptorRightsStatus(inspection.rightsReviewStatus);
    report.catalogManifestId = inspection.manifestId;
    report.catalogAccessProfile = inspection.accessProfile;
    report.catalogMessage = catalogMessageForStatus(inspection);
    populateCatalogRegionReadiness(report, inspection, execution, inputs);

    // A decision a newer one overtook stops between its steps: the window shows only the latest, so the rest of
    // this one - the response repository's assets hashed and the repository loaded - would be read for nothing.
    const auto overtaken = [execution, &report]() {
        if (execution == nullptr || !execution->checkCancellation().failed())
        {
            return false;
        }
        report.stopped = true;
        return true;
    };
    if (overtaken())
    {
        return report;
    }
    const an::ui::SpccResponseRepositoryInspection responseInspection =
        an::ui::inspectSpccResponseRepositoryDetailed(report.responseRepositoryDirectory);
    report.responseAssetsReady = responseInspection.status == an::ui::SpccResponseRepositoryStatus::Ready;
    report.responseRepositoryId = responseInspection.repositoryId;
    report.responseRepositoryVersion = responseInspection.version;
    report.responseAssetsMessage = responseMessageForStatus(responseInspection);
    populateResponseAssetsDownloadReadiness(report, responseInspection);
    if (overtaken())
    {
        return report;
    }
    populateResponseProfileReadiness(report, execution, inputs);
    if (overtaken())
    {
        return report;
    }
    if (inputs.image.has_value())
    {
        populateImageStateReadiness(report, *inputs.image, inputs.allowNarrowbandInput);
    }

    const bool catalogCanPrepareOnRun =
        report.catalogChunkDownloadRequired && report.catalogChunkAutoDownloadAvailable &&
        ((!report.catalogReady) || (report.catalogRegionChecked && !report.catalogRegionAvailable));
    if (report.canExecute())
    {
        if (report.catalogChunkDownloadRequired)
        {
            report.summary = QCoreApplication::translate("SpccProcessReadiness", "SPCC can start; %1.")
                                 .arg(QCoreApplication::translate("SpccProcessReadiness",
                                                                  "needed catalog chunks will download first"));
        }
        else
        {
            report.summary = QCoreApplication::translate("SpccProcessReadiness", "SPCC prerequisites are ready. %1")
                                 .arg(report.responseProfileMessage);
        }
    }
    else if (catalogCanPrepareOnRun && report.responseAssetsReady && !report.responseProfileResolved)
    {
        report.summary = report.responseProfileMessage;
    }
    else if (catalogCanPrepareOnRun && report.imageStateChecked && !report.imageStateCompatible)
    {
        report.summary = imageStateSummaryForReport(report);
    }
    else if (!report.catalogReady)
    {
        const QString catalogSummary =
            report.catalogChunkDownloadRequired && report.catalogChunkAutoDownloadAvailable
                ? report.catalogRegionMessage
                : QCoreApplication::translate("SpccProcessReadiness",
                                              "%1 Install or select a Gaia XP sampled SPCC catalog before calibration.")
                      .arg(report.catalogMessage);
        report.summary = !report.responseAssetsReady ? QCoreApplication::translate("SpccProcessReadiness", "%1 %2")
                                                           .arg(catalogSummary, report.responseAssetsMessage)
                                                     : catalogSummary;
    }
    else if (!report.catalogRightsApproved)
    {
        report.summary = report.catalogMessage;
    }
    else if (report.catalogRegionChecked && !report.catalogRegionAvailable)
    {
        report.summary = !report.responseAssetsReady
                             ? QCoreApplication::translate("SpccProcessReadiness", "%1 %2")
                                   .arg(report.catalogRegionMessage, report.responseAssetsMessage)
                             : report.catalogRegionMessage;
    }
    else if (!report.responseAssetsReady)
    {
        report.summary = QCoreApplication::translate("SpccProcessReadiness", "%1 %2")
                             .arg(report.catalogMessage, report.responseAssetsMessage);
    }
    else if (!report.responseProfileResolved)
    {
        report.summary = QCoreApplication::translate("SpccProcessReadiness", "%1 %2 %3")
                             .arg(report.catalogMessage, report.responseAssetsMessage, report.responseProfileMessage);
    }
    else if (report.imageStateChecked && !report.imageStateCompatible)
    {
        report.summary = imageStateSummaryForReport(report);
    }

    return report;
}

} // namespace

SpccProcessReadinessReport evaluateSpccProcessReadiness(const an::ExecutionContext& execution,
                                                        const SpccReadinessInputs& inputs)
{
    return evaluateReadiness(&execution, inputs);
}

SpccProcessReadinessPanel::SpccProcessReadinessPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("spccProcessReadinessPanel"));
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    const auto& tokenSet = an::ui::tokens();
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(tokenSet.spacing.sm);

    m_banner = new AnInfoBanner(QString(), AnInfoBanner::Tone::Warning, this);
    m_banner->setObjectName(QStringLiteral("spccProcessReadinessBanner"));
    m_banner->setMinimumWidth(0);
    layout->addWidget(m_banner);

    auto* responseForm = new QFormLayout();
    responseForm->setContentsMargins(0, 0, 0, 0);
    responseForm->setHorizontalSpacing(tokenSet.spacing.sm);
    responseForm->setVerticalSpacing(tokenSet.spacing.sm);
    responseForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    responseForm->setRowWrapPolicy(QFormLayout::DontWrapRows);
    layout->addLayout(responseForm);

    m_responseSensorLabel = makeResponseFieldLabel(QCoreApplication::translate("SpccProcessReadiness", "Sensor:"),
                                                   QStringLiteral("spccProcessResponseSensorLabel"), this);
    m_responseSensorCombo = new QComboBox(this);
    m_responseSensorCombo->setObjectName(QStringLiteral("spccProcessResponseSensorCombo"));
    configureLongResponseComboBox(m_responseSensorCombo);
    responseForm->addRow(m_responseSensorLabel, m_responseSensorCombo);

    m_responseFilterLabel = makeResponseFieldLabel(QCoreApplication::translate("SpccProcessReadiness", "OSC filter:"),
                                                   QStringLiteral("spccProcessResponseFilterLabel"), this);
    m_responseFilterCombo = new QComboBox(this);
    m_responseFilterCombo->setObjectName(QStringLiteral("spccProcessResponseFilterCombo"));
    configureLongResponseComboBox(m_responseFilterCombo);
    responseForm->addRow(m_responseFilterLabel, m_responseFilterCombo);

    m_responseLpfLabel = makeResponseFieldLabel(QCoreApplication::translate("SpccProcessReadiness", "DSLR LPF:"),
                                                QStringLiteral("spccProcessResponseLpfLabel"), this);
    m_responseLpfCombo = new QComboBox(this);
    m_responseLpfCombo->setObjectName(QStringLiteral("spccProcessResponseLpfCombo"));
    configureLongResponseComboBox(m_responseLpfCombo);
    responseForm->addRow(m_responseLpfLabel, m_responseLpfCombo);

    m_responseMonoRedFilterLabel =
        makeResponseFieldLabel(QCoreApplication::translate("SpccProcessReadiness", "Red filter:"),
                               QStringLiteral("spccProcessResponseMonoRedFilterLabel"), this);
    m_responseMonoRedFilterCombo = new QComboBox(this);
    m_responseMonoRedFilterCombo->setObjectName(QStringLiteral("spccProcessResponseMonoRedFilterCombo"));
    configureLongResponseComboBox(m_responseMonoRedFilterCombo);
    responseForm->addRow(m_responseMonoRedFilterLabel, m_responseMonoRedFilterCombo);

    m_responseMonoGreenFilterLabel =
        makeResponseFieldLabel(QCoreApplication::translate("SpccProcessReadiness", "Green filter:"),
                               QStringLiteral("spccProcessResponseMonoGreenFilterLabel"), this);
    m_responseMonoGreenFilterCombo = new QComboBox(this);
    m_responseMonoGreenFilterCombo->setObjectName(QStringLiteral("spccProcessResponseMonoGreenFilterCombo"));
    configureLongResponseComboBox(m_responseMonoGreenFilterCombo);
    responseForm->addRow(m_responseMonoGreenFilterLabel, m_responseMonoGreenFilterCombo);

    m_responseMonoBlueFilterLabel =
        makeResponseFieldLabel(QCoreApplication::translate("SpccProcessReadiness", "Blue filter:"),
                               QStringLiteral("spccProcessResponseMonoBlueFilterLabel"), this);
    m_responseMonoBlueFilterCombo = new QComboBox(this);
    m_responseMonoBlueFilterCombo->setObjectName(QStringLiteral("spccProcessResponseMonoBlueFilterCombo"));
    configureLongResponseComboBox(m_responseMonoBlueFilterCombo);
    responseForm->addRow(m_responseMonoBlueFilterLabel, m_responseMonoBlueFilterCombo);

    m_responseProfileLabel =
        makeResponseFieldLabel(QCoreApplication::translate("SpccProcessReadiness", "Response profile:"),
                               QStringLiteral("spccProcessResponseProfileLabel"), this);
    m_responseProfileCombo = new QComboBox(this);
    m_responseProfileCombo->setObjectName(QStringLiteral("spccProcessResponseProfileCombo"));
    configureLongResponseComboBox(m_responseProfileCombo);
    responseForm->addRow(m_responseProfileLabel, m_responseProfileCombo);

    m_whiteReferenceLabel =
        makeResponseFieldLabel(QCoreApplication::translate("SpccProcessReadiness", "White reference:"),
                               QStringLiteral("spccProcessWhiteReferenceLabel"), this);
    m_whiteReferenceCombo = new QComboBox(this);
    m_whiteReferenceCombo->setObjectName(QStringLiteral("spccProcessWhiteReferenceCombo"));
    configureLongResponseComboBox(m_whiteReferenceCombo);
    responseForm->addRow(m_whiteReferenceLabel, m_whiteReferenceCombo);

    m_responseProfileDetailsLabel = new QLabel(this);
    m_responseProfileDetailsLabel->setObjectName(QStringLiteral("spccProcessResponseProfileDetailsLabel"));
    m_responseProfileDetailsLabel->setWordWrap(true);
    m_responseProfileDetailsLabel->setMinimumWidth(0);
    m_responseProfileDetailsLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_responseProfileDetailsLabel);

    m_manageCatalogsButton = new QPushButton(this);
    m_manageCatalogsButton->setObjectName(QStringLiteral("spccProcessManageCatalogsDataButton"));
    m_manageCatalogsButton->setMinimumWidth(0);
    m_manageCatalogsButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_manageCatalogsButton);

    auto* actionLayout = new QVBoxLayout();
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(tokenSet.spacing.sm);
    m_downloadFullCatalogButton =
        new QPushButton(QCoreApplication::translate("SpccProcessReadiness", "Download Offline Catalog"), this);
    m_downloadFullCatalogButton->setObjectName(QStringLiteral("spccProcessDownloadFullCatalogButton"));
    m_downloadFullCatalogButton->setToolTip(QCoreApplication::translate(
        "SpccProcessReadiness", "Open Catalogs & Data to download the full offline SPCC catalog."));
    m_downloadNeededRegionButton =
        new QPushButton(QCoreApplication::translate("SpccProcessReadiness", "Download Needed Region"), this);
    m_downloadNeededRegionButton->setObjectName(QStringLiteral("spccProcessDownloadNeededRegionButton"));
    m_downloadNeededRegionButton->setToolTip(QCoreApplication::translate(
        "SpccProcessReadiness",
        "Open Catalogs & Data with the current image footprint prepared for regional download."));
    m_downloadResponseProfilesButton =
        new QPushButton(QCoreApplication::translate("SpccProcessReadiness", "Download Response Profiles"), this);
    m_downloadResponseProfilesButton->setObjectName(QStringLiteral("spccProcessDownloadResponseProfilesButton"));
    m_downloadResponseProfilesButton->setToolTip(QCoreApplication::translate(
        "SpccProcessReadiness",
        "Download the SPCC response profiles needed to choose sensor and filter response data."));
    m_assignImageStateButton =
        new QPushButton(QCoreApplication::translate("SpccProcessReadiness", "Assign Image State"), this);
    m_assignImageStateButton->setObjectName(QStringLiteral("spccProcessAssignImageStateButton"));
    for (QPushButton* button : {m_downloadFullCatalogButton, m_downloadNeededRegionButton,
                                m_downloadResponseProfilesButton, m_assignImageStateButton})
    {
        button->setMinimumWidth(0);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    actionLayout->addWidget(m_downloadFullCatalogButton);
    actionLayout->addWidget(m_downloadNeededRegionButton);
    actionLayout->addWidget(m_downloadResponseProfilesButton);
    actionLayout->addWidget(m_assignImageStateButton);
    layout->addLayout(actionLayout);

    QObject::connect(m_responseProfileCombo, &QComboBox::currentIndexChanged, this, [this](const int index) {
        if (m_updatingResponseProfileCombo || index < 0)
        {
            return;
        }

        const QString profileId = m_responseProfileCombo->itemData(index).toString();
        if (profileId.isEmpty())
        {
            return;
        }

        if (m_manualResponseProfileSelected)
        {
            m_manualResponseProfileSelected(profileId);
        }
    });
    QObject::connect(m_responseSensorCombo, &QComboBox::currentIndexChanged, this, [this](const int index) {
        if (m_updatingResponseProfileCombo || index < 0)
        {
            return;
        }

        m_selectedResponseSensorKey = m_responseSensorCombo->itemData(index).toString();
        m_selectedResponseOscFilterId.clear();
        m_selectedResponseOscLpfId.clear();
        m_selectedResponseMonoRedFilterId.clear();
        m_selectedResponseMonoGreenFilterId.clear();
        m_selectedResponseMonoBlueFilterId.clear();
        updateResponseProfileSelector();
        selectCurrentStructuredResponseProfile();
    });
    QObject::connect(m_responseFilterCombo, &QComboBox::currentIndexChanged, this, [this](const int index) {
        if (m_updatingResponseProfileCombo || index < 0)
        {
            return;
        }

        m_selectedResponseOscFilterId = m_responseFilterCombo->itemData(index).toString();
        updateResponseProfileSelector();
        selectCurrentStructuredResponseProfile();
    });
    QObject::connect(m_responseLpfCombo, &QComboBox::currentIndexChanged, this, [this](const int index) {
        if (m_updatingResponseProfileCombo || index < 0)
        {
            return;
        }

        m_selectedResponseOscLpfId = m_responseLpfCombo->itemData(index).toString();
        updateResponseProfileSelector();
        selectCurrentStructuredResponseProfile();
    });
    const auto connectMonoFilterCombo = [this](QComboBox* comboBox, QString* selectedFilterId) {
        QObject::connect(comboBox, &QComboBox::currentIndexChanged, this,
                         [this, comboBox, selectedFilterId](const int index) {
                             if (m_updatingResponseProfileCombo || index < 0)
                             {
                                 return;
                             }

                             *selectedFilterId = comboBox->itemData(index).toString();
                             updateResponseProfileSelector();
                             selectCurrentStructuredResponseProfile();
                         });
    };
    connectMonoFilterCombo(m_responseMonoRedFilterCombo, &m_selectedResponseMonoRedFilterId);
    connectMonoFilterCombo(m_responseMonoGreenFilterCombo, &m_selectedResponseMonoGreenFilterId);
    connectMonoFilterCombo(m_responseMonoBlueFilterCombo, &m_selectedResponseMonoBlueFilterId);
    QObject::connect(m_whiteReferenceCombo, &QComboBox::currentIndexChanged, this, [this](const int index) {
        if (m_updatingResponseProfileCombo || index < 0)
        {
            return;
        }

        const QString whiteReferenceId = m_whiteReferenceCombo->itemData(index).toString();
        if (whiteReferenceId.isEmpty())
        {
            return;
        }

        if (m_manualWhiteReferenceSelected)
        {
            m_manualWhiteReferenceSelected(whiteReferenceId);
        }
    });
    QObject::connect(m_manageCatalogsButton, &QPushButton::clicked, this, [this]() { openSettingsTarget(); });
    QObject::connect(m_downloadFullCatalogButton, &QPushButton::clicked, this,
                     [this]() { triggerDownloadFullCatalog(); });
    QObject::connect(m_downloadNeededRegionButton, &QPushButton::clicked, this,
                     [this]() { triggerDownloadNeededRegion(); });
    QObject::connect(m_downloadResponseProfilesButton, &QPushButton::clicked, this,
                     [this]() { triggerDownloadResponseProfiles(); });
    QObject::connect(m_assignImageStateButton, &QPushButton::clicked, this, [this]() { triggerAssignImageState(); });

    setReport(evaluateReadiness(nullptr, SpccReadinessInputs{}));
}

void SpccProcessReadinessPanel::refreshTheme()
{
    if (m_banner)
    {
        m_banner->refreshTheme();
    }
}

void SpccProcessReadinessPanel::setReport(SpccProcessReadinessReport report)
{
    const QString previousProfileId = selectedResponseProfileId(m_report);
    m_report = std::move(report);
    const QString currentProfileId = selectedResponseProfileId(m_report);
    if (currentProfileId != previousProfileId)
    {
        const QStringList profileIds = orderedResponseProfileIds(m_report);
        const std::vector<ResponseProfileChoice> choices = structuredResponseProfileChoices(m_report, profileIds);
        const auto choice = (std::find_if)(choices.begin(), choices.end(), [&currentProfileId](const auto& item) {
            return item.profileId == currentProfileId;
        });
        if (choice != choices.end())
        {
            m_selectedResponseSensorKey = responseChoiceSensorKey(*choice);
            m_selectedResponseOscFilterId = choice->oscFilterId;
            m_selectedResponseOscLpfId = choice->oscLpfId;
            m_selectedResponseMonoRedFilterId = choice->monoRedFilterId;
            m_selectedResponseMonoGreenFilterId = choice->monoGreenFilterId;
            m_selectedResponseMonoBlueFilterId = choice->monoBlueFilterId;
        }
        else
        {
            m_selectedResponseSensorKey.clear();
            m_selectedResponseOscFilterId.clear();
            m_selectedResponseOscLpfId.clear();
            m_selectedResponseMonoRedFilterId.clear();
            m_selectedResponseMonoGreenFilterId.clear();
            m_selectedResponseMonoBlueFilterId.clear();
        }
    }

    if (m_banner)
    {
        m_banner->setTone(toneForReport(m_report));
        m_banner->setMessage(m_report.summary);
        m_banner->setVisible(!m_report.summary.isEmpty());
    }

    if (m_manageCatalogsButton)
    {
        const bool catalogRegionUserActionRequired =
            m_report.catalogRegionChecked && !m_report.catalogRegionAvailable &&
            !(m_report.catalogChunkDownloadRequired && m_report.catalogChunkAutoDownloadAvailable);
        const bool catalogPreparationAvailable =
            m_report.catalogChunkDownloadRequired && m_report.catalogChunkAutoDownloadAvailable;
        const bool catalogUserActionRequired = (!m_report.catalogReady && !catalogPreparationAvailable) ||
                                               !m_report.catalogRightsApproved || catalogRegionUserActionRequired;
        const bool needsCatalogAction = !m_report.canExecute() && catalogUserActionRequired;
        m_manageCatalogsButton->setText(m_report.actionText);
        m_manageCatalogsButton->setVisible(needsCatalogAction && !m_report.actionText.isEmpty());
    }

    updateActionButtons();
    updateResponseProfileSelector();
}

const SpccProcessReadinessReport& SpccProcessReadinessPanel::report() const noexcept
{
    return m_report;
}

void SpccProcessReadinessPanel::setOpenSettingsPageCallback(std::function<void(const QString& pageName)> callback)
{
    m_openSettingsPage = std::move(callback);
}

void SpccProcessReadinessPanel::setManualResponseProfileSelectedCallback(
    std::function<void(const QString& profileId)> callback)
{
    m_manualResponseProfileSelected = std::move(callback);
}

void SpccProcessReadinessPanel::setManualWhiteReferenceSelectedCallback(
    std::function<void(const QString& whiteReferenceId)> callback)
{
    m_manualWhiteReferenceSelected = std::move(callback);
}

void SpccProcessReadinessPanel::setDownloadFullCatalogCallback(std::function<void()> callback)
{
    m_downloadFullCatalog = std::move(callback);
}

void SpccProcessReadinessPanel::setDownloadNeededRegionCallback(std::function<void()> callback)
{
    m_downloadNeededRegion = std::move(callback);
}

void SpccProcessReadinessPanel::setDownloadResponseProfilesCallback(std::function<void()> callback)
{
    m_downloadResponseProfiles = std::move(callback);
}

void SpccProcessReadinessPanel::setAssignImageStateCallback(std::function<void()> callback)
{
    m_assignImageState = std::move(callback);
}

void SpccProcessReadinessPanel::openSettingsTarget()
{
    if (m_report.settingsPageTitle.isEmpty())
    {
        return;
    }

    if (m_openSettingsPage)
    {
        m_openSettingsPage(m_report.settingsPageTitle);
        return;
    }

    process_utils::openSettingsPage(m_report.settingsPageTitle);
}

void SpccProcessReadinessPanel::triggerDownloadFullCatalog()
{
    if (m_downloadFullCatalog)
    {
        m_downloadFullCatalog();
        return;
    }

    openSettingsTarget();
}

void SpccProcessReadinessPanel::triggerDownloadNeededRegion()
{
    if (m_downloadNeededRegion)
    {
        m_downloadNeededRegion();
        return;
    }

    openSettingsTarget();
}

void SpccProcessReadinessPanel::triggerDownloadResponseProfiles()
{
    if (m_downloadResponseProfiles)
    {
        m_downloadResponseProfiles();
    }
}

void SpccProcessReadinessPanel::triggerAssignImageState()
{
    if (m_assignImageState)
    {
        m_assignImageState();
    }
}

void SpccProcessReadinessPanel::updateActionButtons()
{
    const bool catalogRegionUserActionRequired =
        m_report.catalogRegionChecked && !m_report.catalogRegionAvailable &&
        !(m_report.catalogChunkDownloadRequired && m_report.catalogChunkAutoDownloadAvailable);
    const bool catalogPreparationAvailable =
        m_report.catalogChunkDownloadRequired && m_report.catalogChunkAutoDownloadAvailable;
    const bool catalogUserActionRequired = (!m_report.catalogReady && !catalogPreparationAvailable) ||
                                           !m_report.catalogRightsApproved || catalogRegionUserActionRequired;
    const bool needsCatalogAction = !m_report.canExecute() && catalogUserActionRequired;
    if (m_downloadFullCatalogButton)
    {
        m_downloadFullCatalogButton->setVisible(needsCatalogAction);
        m_downloadFullCatalogButton->setEnabled(needsCatalogAction);
    }
    if (m_downloadNeededRegionButton)
    {
        const bool needsRegionAction = !m_report.canExecute() && catalogRegionUserActionRequired;
        m_downloadNeededRegionButton->setVisible(needsRegionAction);
        m_downloadNeededRegionButton->setEnabled(needsRegionAction);
    }
    if (m_downloadResponseProfilesButton)
    {
        const bool needsResponseDownloadAction = !m_report.responseAssetsReady &&
                                                 m_report.responseAssetsDownloadRequired &&
                                                 m_report.responseAssetsAutoDownloadAvailable;
        m_downloadResponseProfilesButton->setVisible(needsResponseDownloadAction);
        m_downloadResponseProfilesButton->setEnabled(needsResponseDownloadAction);
    }
    if (m_assignImageStateButton)
    {
        const bool needsImageStateAction =
            m_report.imageStateChecked && !m_report.imageStateCompatible && !m_report.imageStateAlreadyColorCalibrated;
        m_assignImageStateButton->setVisible(needsImageStateAction);
        m_assignImageStateButton->setEnabled(needsImageStateAction);
    }
}

void SpccProcessReadinessPanel::selectCurrentStructuredResponseProfile()
{
    if (!m_manualResponseProfileSelected || m_selectedResponseSensorKey.isEmpty())
    {
        return;
    }

    const QStringList profileIds = orderedResponseProfileIds(m_report);
    const std::vector<ResponseProfileChoice> sensorChoices =
        choicesForSensor(structuredResponseProfileChoices(m_report, profileIds), m_selectedResponseSensorKey);
    const QString family = sensorFamilyFromKey(m_selectedResponseSensorKey);
    QString profileId;
    if (family == QStringLiteral("osc"))
    {
        profileId = matchingOscProfileId(sensorChoices, m_selectedResponseOscFilterId, m_selectedResponseOscLpfId);
    }
    else if (family == QStringLiteral("mono"))
    {
        profileId = matchingMonoProfileId(sensorChoices, m_selectedResponseMonoRedFilterId,
                                          m_selectedResponseMonoGreenFilterId, m_selectedResponseMonoBlueFilterId);
    }

    if (!profileId.isEmpty())
    {
        m_manualResponseProfileSelected(profileId);
    }
}

void SpccProcessReadinessPanel::updateResponseProfileSelector()
{
    if (m_responseProfileCombo == nullptr)
    {
        return;
    }

    const QStringList profileIds = orderedResponseProfileIds(m_report);
    const QStringList whiteReferenceIds = orderedWhiteReferenceIds(m_report);
    const bool hasProfiles = m_report.responseAssetsReady && !profileIds.isEmpty();
    const bool hasWhiteReferences = m_report.responseAssetsReady && !whiteReferenceIds.isEmpty();
    const std::vector<ResponseProfileChoice> structuredChoices = structuredResponseProfileChoices(m_report, profileIds);
    const bool hasStructuredChoices = hasProfiles && !structuredChoices.empty();
    const QString selectedProfileId = selectedResponseProfileId(m_report);
    const QString selectedWhiteId = selectedWhiteReferenceId(m_report);
    const auto selectedChoice =
        (std::find_if)(structuredChoices.begin(), structuredChoices.end(),
                       [&selectedProfileId](const auto& choice) { return choice.profileId == selectedProfileId; });
    QString selectedSensorKey = m_selectedResponseSensorKey;
    if (selectedSensorKey.isEmpty() && selectedChoice != structuredChoices.end())
    {
        selectedSensorKey = responseChoiceSensorKey(*selectedChoice);
    }

    m_updatingResponseProfileCombo = true;

    const auto hideCombo = [](QLabel* label, QComboBox* comboBox) {
        if (comboBox)
        {
            comboBox->clear();
        }
        setResponseFieldVisible(label, comboBox, false);
    };

    if (m_responseSensorCombo)
    {
        m_responseSensorCombo->clear();
        if (hasStructuredChoices)
        {
            m_responseSensorCombo->addItem(QCoreApplication::translate("SpccProcessReadiness", "Select sensor"),
                                           QString());

            QStringList seenSensorKeys;
            for (const ResponseProfileChoice& choice : structuredChoices)
            {
                const QString sensorKey = responseChoiceSensorKey(choice);
                if (seenSensorKeys.contains(sensorKey))
                {
                    continue;
                }
                seenSensorKeys.append(sensorKey);
                m_responseSensorCombo->addItem(responseChoiceSensorLabel(choice), sensorKey);
            }

            const int selectedSensorIndex = m_responseSensorCombo->findData(selectedSensorKey);
            if (selectedSensorIndex >= 0)
            {
                m_responseSensorCombo->setCurrentIndex(selectedSensorIndex);
            }
        }
        setResponseFieldVisible(m_responseSensorLabel, m_responseSensorCombo, hasStructuredChoices);
    }

    hideCombo(m_responseFilterLabel, m_responseFilterCombo);
    hideCombo(m_responseLpfLabel, m_responseLpfCombo);
    hideCombo(m_responseMonoRedFilterLabel, m_responseMonoRedFilterCombo);
    hideCombo(m_responseMonoGreenFilterLabel, m_responseMonoGreenFilterCombo);
    hideCombo(m_responseMonoBlueFilterLabel, m_responseMonoBlueFilterCombo);

    const std::vector<ResponseProfileChoice> selectedSensorChoices =
        choicesForSensor(structuredChoices, selectedSensorKey);
    const QString sensorFamily = sensorFamilyFromKey(selectedSensorKey);
    if (hasStructuredChoices && !selectedSensorChoices.empty() && sensorFamily == QStringLiteral("osc"))
    {
        QString selectedFilterId = m_selectedResponseOscFilterId;
        QString selectedLpfId = m_selectedResponseOscLpfId;
        const QStringList filterOptions = oscFilterOptions(selectedSensorChoices);
        selectedFilterId = selectedValueInOptionsOrFirst(selectedFilterId, filterOptions);

        const QStringList lpfOptions = oscLpfOptions(selectedSensorChoices);
        selectedLpfId = selectedValueInOptionsOrFirst(selectedLpfId, lpfOptions);

        m_selectedResponseOscFilterId = selectedFilterId;
        m_selectedResponseOscLpfId = selectedLpfId;

        if (m_responseFilterCombo && !filterOptions.isEmpty())
        {
            for (const QString& filterId : filterOptions)
            {
                m_responseFilterCombo->addItem(
                    filterId.isEmpty() ? QCoreApplication::translate("SpccProcessReadiness", "No OSC filter")
                                       : QCoreApplication::translate("SpccProcessReadiness", "OSC filter: %1")
                                             .arg(responseComponentDisplayName(filterId)),
                    filterId);
            }
            const int selectedIndex = m_responseFilterCombo->findData(selectedFilterId);
            if (selectedIndex >= 0)
            {
                m_responseFilterCombo->setCurrentIndex(selectedIndex);
            }
            const bool shouldShow = filterOptions.size() > 1 || !filterOptions.front().isEmpty();
            setResponseFieldVisible(m_responseFilterLabel, m_responseFilterCombo, shouldShow);
        }

        if (m_responseLpfCombo && !lpfOptions.isEmpty())
        {
            for (const QString& lpfId : lpfOptions)
            {
                m_responseLpfCombo->addItem(lpfId.isEmpty()
                                                ? QCoreApplication::translate("SpccProcessReadiness", "No DSLR LPF")
                                                : QCoreApplication::translate("SpccProcessReadiness", "DSLR LPF: %1")
                                                      .arg(responseComponentDisplayName(lpfId)),
                                            lpfId);
            }
            const int selectedIndex = m_responseLpfCombo->findData(selectedLpfId);
            if (selectedIndex >= 0)
            {
                m_responseLpfCombo->setCurrentIndex(selectedIndex);
            }
            const bool shouldShow = lpfOptions.size() > 1 || !lpfOptions.front().isEmpty();
            setResponseFieldVisible(m_responseLpfLabel, m_responseLpfCombo, shouldShow);
        }
    }
    else if (hasStructuredChoices && !selectedSensorChoices.empty() && sensorFamily == QStringLiteral("mono"))
    {
        QString selectedRedId = m_selectedResponseMonoRedFilterId;
        QString selectedGreenId = m_selectedResponseMonoGreenFilterId;
        QString selectedBlueId = m_selectedResponseMonoBlueFilterId;

        const QStringList redOptions = monoFilterOptions(selectedSensorChoices, QStringLiteral("red"));
        selectedRedId = selectedValueInOptionsOrFirst(selectedRedId, redOptions);

        const QStringList greenOptions = monoFilterOptions(selectedSensorChoices, QStringLiteral("green"));
        selectedGreenId = selectedValueInOptionsOrFirst(selectedGreenId, greenOptions);

        const QStringList blueOptions = monoFilterOptions(selectedSensorChoices, QStringLiteral("blue"));
        selectedBlueId = selectedValueInOptionsOrFirst(selectedBlueId, blueOptions);

        m_selectedResponseMonoRedFilterId = selectedRedId;
        m_selectedResponseMonoGreenFilterId = selectedGreenId;
        m_selectedResponseMonoBlueFilterId = selectedBlueId;

        const auto populateMonoCombo = [](QLabel* fieldLabel, QComboBox* comboBox, const QStringList& options,
                                          const QString& selectedId, const QString& channelLabel) {
            if (!comboBox || options.isEmpty())
            {
                return;
            }

            for (const QString& filterId : options)
            {
                comboBox->addItem(QCoreApplication::translate("SpccProcessReadiness", "%1 filter: %2")
                                      .arg(channelLabel, responseComponentDisplayName(filterId)),
                                  filterId);
            }
            const int selectedIndex = comboBox->findData(selectedId);
            if (selectedIndex >= 0)
            {
                comboBox->setCurrentIndex(selectedIndex);
            }
            setResponseFieldVisible(fieldLabel, comboBox, true);
        };

        populateMonoCombo(m_responseMonoRedFilterLabel, m_responseMonoRedFilterCombo, redOptions, selectedRedId,
                          QCoreApplication::translate("SpccProcessReadiness", "Red"));
        populateMonoCombo(m_responseMonoGreenFilterLabel, m_responseMonoGreenFilterCombo, greenOptions, selectedGreenId,
                          QCoreApplication::translate("SpccProcessReadiness", "Green"));
        populateMonoCombo(m_responseMonoBlueFilterLabel, m_responseMonoBlueFilterCombo, blueOptions, selectedBlueId,
                          QCoreApplication::translate("SpccProcessReadiness", "Blue"));
    }

    const bool showProfileSelector = hasProfiles && !hasStructuredChoices;
    m_responseProfileCombo->clear();
    if (showProfileSelector)
    {
        m_responseProfileCombo->addItem(QCoreApplication::translate("SpccProcessReadiness", "Select response profile"),
                                        QString());
        for (const QString& profileId : profileIds)
        {
            m_responseProfileCombo->addItem(responseProfileDisplayText(profileId), profileId);
        }

        const int selectedIndex = m_responseProfileCombo->findData(selectedProfileId);
        if (selectedIndex >= 0)
        {
            m_responseProfileCombo->setCurrentIndex(selectedIndex);
        }
    }
    setResponseFieldVisible(m_responseProfileLabel, m_responseProfileCombo, showProfileSelector);

    if (m_whiteReferenceCombo)
    {
        m_whiteReferenceCombo->clear();
        if (hasWhiteReferences)
        {
            m_whiteReferenceCombo->addItem(
                QCoreApplication::translate("SpccProcessReadiness", "Select white reference"), QString());
            for (const QString& whiteReferenceId : whiteReferenceIds)
            {
                m_whiteReferenceCombo->addItem(whiteReferenceDisplayName(m_report, whiteReferenceId), whiteReferenceId);
            }

            const int selectedIndex = m_whiteReferenceCombo->findData(selectedWhiteId);
            if (selectedIndex >= 0)
            {
                m_whiteReferenceCombo->setCurrentIndex(selectedIndex);
            }
        }
        setResponseFieldVisible(m_whiteReferenceLabel, m_whiteReferenceCombo, hasWhiteReferences);
    }

    if (m_responseProfileDetailsLabel)
    {
        QStringList details;
        if (hasProfiles && !selectedProfileId.isEmpty())
        {
            details.append(QCoreApplication::translate("SpccProcessReadiness", "Response: %1")
                               .arg(responseProfileDisplayText(selectedProfileId)));
            if (!m_report.whiteReferenceId.trimmed().isEmpty())
            {
                details.append(QCoreApplication::translate("SpccProcessReadiness", "White reference: %1")
                                   .arg(whiteReferenceDisplayName(m_report, m_report.whiteReferenceId)));
            }
            if (m_report.responseProfileManualSelection)
            {
                details.append(QCoreApplication::translate("SpccProcessReadiness", "Selection: manual"));
            }
            else if (m_report.responseProfileSavedMapping)
            {
                details.append(QCoreApplication::translate("SpccProcessReadiness", "Selection: saved mapping"));
            }
            else if (m_report.responseProfileResolved)
            {
                details.append(QCoreApplication::translate("SpccProcessReadiness", "Selection: image metadata"));
            }
        }

        m_responseProfileDetailsLabel->setText(details.join(QStringLiteral("  |  ")));
        m_responseProfileDetailsLabel->setVisible(!details.isEmpty());
    }

    m_updatingResponseProfileCombo = false;
}

} // namespace an::ui::processes
