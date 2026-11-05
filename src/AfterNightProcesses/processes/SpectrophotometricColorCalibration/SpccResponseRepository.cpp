// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The SPCC response repository: its documents read, its profiles composed and resolved, its aliases matched.

#include "stdafx.hpp"

#include "processes/SpectrophotometricColorCalibration/SpccResponseRepository.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <utility>

namespace an::ui::processes
{

namespace
{

constexpr const char kOscSensorPrefix[] = "afternight.spcc.profile.osc_sensor.";
constexpr const char kMonoSensorPrefix[] = "afternight.spcc.profile.mono_sensor.";
constexpr const char kMonoFilterPrefix[] = "afternight.spcc.profile.mono_filter.";

[[nodiscard]] an::Status refusal(const an::ErrorCode code, const QString& message)
{
    return an::Status::failure(code, an::StatusDetail{.message = message.toStdString()});
}

[[nodiscard]] QString normalizedKey(const QString& text)
{
    return text.trimmed().toLower();
}

/// A JSON document read from a file of the repository.
[[nodiscard]] an::Result<QJsonObject> readDocument(const QString& path, const QString& label)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return refusal(an::ErrorCode::ExternalFailure, QStringLiteral("Failed to open SPCC %1: %2").arg(label, path));
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return refusal(an::ErrorCode::InvalidData,
                       QStringLiteral("Failed to parse SPCC %1: %2").arg(label, error.errorString()));
    }
    return document.object();
}

/// Whether a relative path stays inside the repository: not empty, not absolute, no parent step.
[[nodiscard]] bool isSafeRelativePath(const QString& path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path))
    {
        return false;
    }
    for (const QString& part : QDir::fromNativeSeparators(path).split(QLatin1Char('/')))
    {
        if (part == QStringLiteral(".."))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool hasRuntimeScope(const QJsonArray& scopes)
{
    for (const QJsonValue& scope : scopes)
    {
        const QString value = scope.toString();
        if (value == QStringLiteral("deployed-runtime") || value == QStringLiteral("managed-cache") ||
            value == QStringLiteral("user-private"))
        {
            return true;
        }
    }
    return false;
}

/// A sample list read from a document, checked as the engine checks a curve.
[[nodiscard]] an::Result<std::vector<an::SpectralSample>> readSamples(const QJsonValue& value, const QString& label)
{
    if (!value.isArray())
    {
        return refusal(an::ErrorCode::InvalidData, QStringLiteral("%1 samples must be an array").arg(label));
    }
    std::vector<an::SpectralSample> samples;
    for (const QJsonValue& entry : value.toArray())
    {
        if (!entry.isObject())
        {
            return refusal(an::ErrorCode::InvalidData, QStringLiteral("%1 sample entry must be an object").arg(label));
        }
        const QJsonObject sample = entry.toObject();
        samples.push_back(an::SpectralSample{
            .wavelengthNm =
                sample.value(QStringLiteral("wavelength_nm")).toDouble(std::numeric_limits<double>::quiet_NaN()),
            .value = sample.value(QStringLiteral("value")).toDouble(std::numeric_limits<double>::quiet_NaN())});
    }
    an::SpectralCurve curve;
    curve.samples = samples;
    if (const an::SpectralCurveError error = an::checkSpectralCurve(curve); error != an::SpectralCurveError::None)
    {
        return refusal(an::ErrorCode::InvalidData,
                       QStringLiteral("%1 is not a usable curve: %2")
                           .arg(label, QString::fromLatin1(an::spectralCurveErrorName(error))));
    }
    return samples;
}

/// One asset of the repository - a component or a white reference - read and held to its manifest entry.
struct AssetRead
{
    QString id;
    QString version;
    QJsonObject entry;
    QJsonObject payload;
    std::vector<an::SpectralSample> samples;
};

[[nodiscard]] an::Result<AssetRead> readAsset(const QDir& root, const QJsonValue& value, const QString& idKey,
                                              const QString& label)
{
    if (!value.isObject())
    {
        return refusal(an::ErrorCode::InvalidData,
                       QStringLiteral("SPCC response repository %1 entry is not an object").arg(label));
    }
    AssetRead asset;
    asset.entry = value.toObject();
    asset.id = asset.entry.value(idKey).toString().trimmed();
    const QString relativeFile = asset.entry.value(QStringLiteral("file")).toString().trimmed();
    if (asset.id.isEmpty() || relativeFile.isEmpty())
    {
        return refusal(an::ErrorCode::InvalidData, QStringLiteral("SPCC %1 is missing %2 or file").arg(label, idKey));
    }
    if (!isSafeRelativePath(relativeFile))
    {
        return refusal(an::ErrorCode::InvalidData,
                       QStringLiteral("SPCC %1 path is not a safe relative path: %2").arg(label, relativeFile));
    }
    an::Result<QJsonObject> payload = readDocument(root.filePath(relativeFile), label);
    if (!payload.hasValue())
    {
        return payload.status();
    }
    asset.payload = std::move(*payload);
    if (asset.payload.value(QStringLiteral("schema_version")).toString() !=
        QString::fromLatin1(kSpccResponseAssetSchema))
    {
        return refusal(an::ErrorCode::InvalidData,
                       QStringLiteral("SPCC %1 has an unsupported schema: %2").arg(label, asset.id));
    }
    if (asset.payload.value(idKey).toString().trimmed() != asset.id)
    {
        return refusal(an::ErrorCode::InvalidData, QStringLiteral("SPCC %1 ID mismatch: %2").arg(label, asset.id));
    }
    if (asset.payload.value(QStringLiteral("wavelength_unit")).toString() != QStringLiteral("nm") ||
        asset.payload.value(QStringLiteral("value_semantics")).toString().trimmed().isEmpty())
    {
        return refusal(an::ErrorCode::InvalidData,
                       QStringLiteral("SPCC %1 has invalid units or value semantics: %2").arg(label, asset.id));
    }
    const QString expectedData = asset.entry.value(QStringLiteral("data_sha256")).toString().trimmed();
    const QString payloadData = asset.payload.value(QStringLiteral("data_sha256")).toString().trimmed();
    if (expectedData.isEmpty() || payloadData.isEmpty() || expectedData != payloadData)
    {
        return refusal(an::ErrorCode::InvalidData,
                       QStringLiteral("SPCC %1 data checksum metadata mismatch: %2").arg(label, asset.id));
    }
    an::Result<std::vector<an::SpectralSample>> samples =
        readSamples(asset.payload.value(QStringLiteral("samples")), QStringLiteral("SPCC %1").arg(label));
    if (!samples.hasValue())
    {
        return samples.status();
    }
    asset.samples = std::move(*samples);
    asset.version = asset.entry.value(QStringLiteral("version"))
                        .toString(asset.payload.value(QStringLiteral("version")).toString())
                        .trimmed();
    return asset;
}

/// The first of a document's texts that is not empty.
[[nodiscard]] QString firstNonEmpty(const QJsonObject& entry, const QJsonObject& payload)
{
    for (const char* key : {"display_name", "name", "source_upstream_name", "source_upstream_model"})
    {
        for (const QJsonObject* document : {&entry, &payload})
        {
            const QString text = document->value(QString::fromLatin1(key)).toString().trimmed();
            if (!text.isEmpty())
            {
                return text;
            }
        }
    }
    return {};
}

[[nodiscard]] QStringList stringList(const QJsonValue& value)
{
    QStringList list;
    for (const QJsonValue& entry : value.toArray())
    {
        if (entry.isString())
        {
            list.append(entry.toString().trimmed());
        }
    }
    return list;
}

/// The parts of a one-shot-colour profile identifier.
struct OscProfileParts
{
    QString sensor;
    QString filter;
    QString window;
    QString suffix;
};

[[nodiscard]] std::optional<OscProfileParts> oscProfileParts(const QString& profileId)
{
    const QString prefix = QString::fromLatin1(kOscSensorPrefix);
    if (!profileId.startsWith(prefix))
    {
        return std::nullopt;
    }
    const QString rest = profileId.mid(prefix.size());
    const qsizetype suffixIndex = rest.lastIndexOf(QLatin1Char('.'));
    if (suffixIndex < 0 || suffixIndex + 1 >= rest.size())
    {
        return std::nullopt;
    }
    const QString filterMarker = QStringLiteral(".osc_filter.");
    const QString windowMarker = QStringLiteral(".osc_lpf.");
    const qsizetype filterIndex = rest.indexOf(filterMarker);
    const qsizetype windowIndex = rest.indexOf(windowMarker);
    qsizetype firstMarker = -1;
    if (filterIndex >= 0 && windowIndex >= 0)
    {
        firstMarker = (std::min)(filterIndex, windowIndex);
    }
    else
    {
        firstMarker = (std::max)(filterIndex, windowIndex);
    }

    OscProfileParts parts;
    parts.sensor = rest.left(firstMarker < 0 ? suffixIndex : firstMarker).trimmed();
    parts.suffix = rest.mid(suffixIndex + 1).trimmed();
    if (parts.sensor.isEmpty() || parts.suffix.isEmpty())
    {
        return std::nullopt;
    }
    if (filterIndex >= 0)
    {
        const qsizetype start = filterIndex + filterMarker.size();
        const qsizetype end = windowIndex > filterIndex ? windowIndex : suffixIndex;
        parts.filter = rest.mid(start, end - start).trimmed();
    }
    if (windowIndex >= 0)
    {
        const qsizetype start = windowIndex + windowMarker.size();
        parts.window = rest.mid(start, suffixIndex - start).trimmed();
    }
    return parts;
}

} // namespace

an::ResponseProfileKind spccResponseProfileKind(const QString& profileId)
{
    if (profileId.startsWith(QString::fromLatin1(kMonoSensorPrefix)))
    {
        return profileId.contains(QStringLiteral(".mono_filter.")) ? an::ResponseProfileKind::MonoSensorRgbFilters
                                                                   : an::ResponseProfileKind::MonoSensor;
    }
    if (profileId.startsWith(QString::fromLatin1(kMonoFilterPrefix)))
    {
        return an::ResponseProfileKind::MonoSensor;
    }
    return an::ResponseProfileKind::ColorSensor;
}

bool isApprovedSpccResponseRights(const QString& status)
{
    return status == QStringLiteral("approved-for-managed-download") ||
           status == QStringLiteral("approved-for-bundling");
}

an::ColorSpace spccColorSpaceOfBasis(const an::RecordedChannelBasis basis) noexcept
{
    switch (basis)
    {
    case an::RecordedChannelBasis::LinearRgb:
        return an::ColorSpace::LinearRgb;
    case an::RecordedChannelBasis::DisplayRgb:
        return an::ColorSpace::DisplayRgb;
    case an::RecordedChannelBasis::CameraRgb:
    default:
        return an::ColorSpace::Unspecified;
    }
}

an::FrameColorState spccFrameColorState(const an::ProcessingStateRecord& state)
{
    return an::FrameColorState{
        .colorSpace = spccColorSpaceOfBasis(state.channelBasis),
        .rawWhiteBalanceApplied = state.rawWhiteBalance.has_value() && state.rawWhiteBalance->applied,
        .cameraMatrixApplied = !state.cameraMatrixProvenance.empty(),
    };
}

an::Result<SpccResponseRepository> SpccResponseRepository::load(const an::ExecutionContext& execution,
                                                                const QString& directory)
{
    const QDir root(directory);
    an::Result<QJsonObject> read = readDocument(root.filePath(QStringLiteral("response_meta.json")),
                                                QStringLiteral("response repository metadata"));
    if (!read.hasValue())
    {
        return read.status();
    }
    const QJsonObject& meta = *read;
    if (meta.value(QStringLiteral("schema_version")).toString() != QString::fromLatin1(kSpccResponseRepositorySchema))
    {
        return refusal(an::ErrorCode::InvalidData,
                       QStringLiteral("SPCC response repository has an unsupported schema_version"));
    }
    if (!hasRuntimeScope(meta.value(QStringLiteral("deployment_scopes")).toArray()))
    {
        return refusal(an::ErrorCode::InvalidData,
                       QStringLiteral("SPCC response repository is not marked for runtime deployment"));
    }

    SpccResponseRepository repository;
    repository.m_repositoryId = meta.value(QStringLiteral("repository_id")).toString().trimmed();
    repository.m_version = meta.value(QStringLiteral("version")).toString().trimmed();
    repository.m_aliasRegistryVersion =
        meta.value(QStringLiteral("alias_registry_version")).toString(repository.m_version).trimmed();
    repository.m_rightsReviewStatus =
        meta.value(QStringLiteral("rights")).toObject().value(QStringLiteral("review_status")).toString().trimmed();
    if (repository.m_repositoryId.isEmpty() || repository.m_version.isEmpty())
    {
        return refusal(an::ErrorCode::InvalidData,
                       QStringLiteral("SPCC response repository metadata is missing repository identity"));
    }

    // A component is a curve and the channel it belongs to.
    struct Component
    {
        an::SpectralCurve curve;
        QString channel;
    };
    std::map<QString, Component> components;
    for (const QJsonValue& value : meta.value(QStringLiteral("components")).toArray())
    {
        an::Result<AssetRead> asset =
            readAsset(root, value, QStringLiteral("component_id"), QStringLiteral("response component"));
        if (!asset.hasValue())
        {
            return asset.status();
        }
        Component component;
        component.curve.identifier = asset->id.toStdString();
        component.curve.version = asset->version.toStdString();
        component.curve.samples = std::move(asset->samples);
        component.channel = normalizedKey(asset->payload.value(QStringLiteral("channel")).toString());
        if (!components.emplace(asset->id, std::move(component)).second)
        {
            return refusal(an::ErrorCode::InvalidData,
                           QStringLiteral("SPCC response repository contains duplicate component: %1").arg(asset->id));
        }
    }
    if (components.empty())
    {
        return refusal(an::ErrorCode::InvalidData,
                       QStringLiteral("SPCC response repository has no response components"));
    }

    for (const QJsonValue& value : meta.value(QStringLiteral("white_references")).toArray())
    {
        an::Result<AssetRead> asset =
            readAsset(root, value, QStringLiteral("white_reference_id"), QStringLiteral("white reference"));
        if (!asset.hasValue())
        {
            return asset.status();
        }
        if (repository.findWhiteReference(asset->id) != nullptr)
        {
            return refusal(
                an::ErrorCode::InvalidData,
                QStringLiteral("SPCC response repository contains duplicate white reference: %1").arg(asset->id));
        }
        SpccRepositoryWhiteReference white;
        white.spectrum.identifier = asset->id.toStdString();
        white.spectrum.version = asset->version.toStdString();
        white.spectrum.photonWeighted =
            asset->payload.value(QStringLiteral("photon_count_weighted"))
                .toBool(asset->entry.value(QStringLiteral("photon_count_weighted")).toBool(false));
        white.spectrum.samples = std::move(asset->samples);
        white.displayName = firstNonEmpty(asset->entry, asset->payload);
        repository.m_whiteReferences.push_back(std::move(white));
    }
    if (repository.m_whiteReferences.empty())
    {
        return refusal(an::ErrorCode::InvalidData, QStringLiteral("SPCC response repository has no white references"));
    }

    const QJsonArray profiles = meta.value(QStringLiteral("throughput_profiles")).toArray();
    if (profiles.isEmpty())
    {
        return refusal(an::ErrorCode::InvalidData,
                       QStringLiteral("SPCC response repository has no throughput profiles"));
    }
    static const std::array<QString, 3u> channelNames{QStringLiteral("red"), QStringLiteral("green"),
                                                      QStringLiteral("blue")};
    for (const QJsonValue& value : profiles)
    {
        if (!value.isObject())
        {
            return refusal(an::ErrorCode::InvalidData,
                           QStringLiteral("SPCC response repository profile entry is not an object"));
        }
        const QJsonObject document = value.toObject();
        SpccRepositoryProfile profile;
        const QString profileId = document.value(QStringLiteral("profile_id")).toString().trimmed();
        const QString version = document.value(QStringLiteral("version")).toString().trimmed();
        if (profileId.isEmpty() || version.isEmpty())
        {
            return refusal(an::ErrorCode::InvalidData,
                           QStringLiteral("SPCC response profile is missing profile identity"));
        }
        profile.profile.identifier = profileId.toStdString();
        profile.profile.version = version.toStdString();
        profile.profile.kind = spccResponseProfileKind(profileId);

        const QJsonObject channelIds = document.value(QStringLiteral("channel_component_ids")).toObject();
        const QStringList componentIds = stringList(document.value(QStringLiteral("component_ids")));
        for (std::size_t channel = 0u; channel < channelNames.size(); ++channel)
        {
            QStringList ids = stringList(channelIds.value(channelNames[channel]));
            if (ids.isEmpty())
            {
                for (const QString& id : componentIds)
                {
                    const auto found = components.find(id);
                    if (found != components.end() && found->second.channel == channelNames[channel])
                    {
                        ids.append(id);
                    }
                }
            }
            if (ids.isEmpty())
            {
                return refusal(an::ErrorCode::InvalidData,
                               QStringLiteral("SPCC response profile has no component for channel: %1")
                                   .arg(channelNames[channel]));
            }
            std::vector<an::SpectralCurve> curves;
            for (const QString& id : ids)
            {
                const auto found = components.find(id);
                if (found == components.end())
                {
                    return refusal(an::ErrorCode::InvalidData,
                                   QStringLiteral("SPCC response profile references a missing component: %1").arg(id));
                }
                curves.push_back(found->second.curve);
            }
            an::Result<an::SpectralCurve> composed = an::composeSpectralCurves(
                execution, curves, profileId.toStdString() + "." + channelNames[channel].toStdString(),
                version.toStdString());
            if (!composed.hasValue())
            {
                return composed.status();
            }
            profile.profile.channels[channel] = std::move(*composed);
            profile.channelComponentIds[channel] = ids;
        }

        profile.whiteReferenceId = document.value(QStringLiteral("white_reference_id")).toString().trimmed();
        if (repository.findWhiteReference(profile.whiteReferenceId) == nullptr)
        {
            return refusal(an::ErrorCode::InvalidData,
                           QStringLiteral("SPCC response profile references a missing white reference: %1")
                               .arg(profile.whiteReferenceId));
        }

        for (const QString& basis : stringList(document.value(QStringLiteral("compatible_channel_bases"))))
        {
            const QString key = normalizedKey(basis);
            if (key == QStringLiteral("linear-rgb"))
            {
                profile.capabilities.compatibleColorSpaces.push_back(an::ColorSpace::LinearRgb);
            }
            else if (key == QStringLiteral("camera-rgb"))
            {
                profile.capabilities.compatibleColorSpaces.push_back(an::ColorSpace::Unspecified);
            }
            else if (key == QStringLiteral("display-rgb"))
            {
                profile.capabilities.compatibleColorSpaces.push_back(an::ColorSpace::DisplayRgb);
            }
            else if (key != QStringLiteral("raw-cfa") && key != QStringLiteral("monochrome"))
            {
                return refusal(an::ErrorCode::InvalidData,
                               QStringLiteral("SPCC response profile has an unsupported channel basis: %1").arg(basis));
            }
        }
        if (stringList(document.value(QStringLiteral("compatible_channel_bases"))).isEmpty())
        {
            return refusal(an::ErrorCode::InvalidData,
                           QStringLiteral("SPCC response profile has no compatible channel bases: %1").arg(profileId));
        }
        profile.capabilities.modelsRawWhiteBalance =
            document.value(QStringLiteral("models_raw_white_balance")).toBool(false);
        profile.capabilities.modelsCameraMatrix = document.value(QStringLiteral("models_camera_matrix")).toBool(false);
        profile.capabilities.reviewed = isApprovedSpccResponseRights(document.value(QStringLiteral("rights"))
                                                                         .toObject()
                                                                         .value(QStringLiteral("review_status"))
                                                                         .toString(repository.m_rightsReviewStatus)
                                                                         .trimmed());

        for (const QJsonValue& aliasValue : document.value(QStringLiteral("aliases")).toArray())
        {
            const QJsonObject alias = aliasValue.toObject();
            SpccProfileAlias parsed{.cameraModel = alias.value(QStringLiteral("camera_model")).toString().trimmed(),
                                    .filterName = alias.value(QStringLiteral("filter")).toString().trimmed()};
            if (!parsed.cameraModel.isEmpty() || !parsed.filterName.isEmpty())
            {
                profile.aliases.push_back(std::move(parsed));
            }
        }
        repository.m_profiles.push_back(std::move(profile));
    }
    return repository;
}

const QString& SpccResponseRepository::repositoryId() const noexcept
{
    return m_repositoryId;
}

const QString& SpccResponseRepository::version() const noexcept
{
    return m_version;
}

const QString& SpccResponseRepository::aliasRegistryVersion() const noexcept
{
    return m_aliasRegistryVersion;
}

const std::vector<SpccRepositoryProfile>& SpccResponseRepository::profiles() const noexcept
{
    return m_profiles;
}

const std::vector<SpccRepositoryWhiteReference>& SpccResponseRepository::whiteReferences() const noexcept
{
    return m_whiteReferences;
}

const SpccRepositoryProfile* SpccResponseRepository::findProfile(const QString& profileId) const noexcept
{
    for (const SpccRepositoryProfile& profile : m_profiles)
    {
        if (QString::fromStdString(profile.profile.identifier) == profileId)
        {
            return &profile;
        }
    }
    return nullptr;
}

const SpccRepositoryWhiteReference*
SpccResponseRepository::findWhiteReference(const QString& whiteReferenceId) const noexcept
{
    for (const SpccRepositoryWhiteReference& white : m_whiteReferences)
    {
        if (QString::fromStdString(white.spectrum.identifier) == whiteReferenceId)
        {
            return &white;
        }
    }
    return nullptr;
}

an::Result<SpccRepositoryProfile> SpccResponseRepository::resolveProfile(const QString& profileId) const
{
    if (const SpccRepositoryProfile* held = findProfile(profileId); held != nullptr)
    {
        return *held;
    }
    const QString unavailable = QStringLiteral("The selected SPCC response profile is not available.");
    const std::optional<OscProfileParts> parts = oscProfileParts(profileId);
    if (!parts.has_value() || parts->filter.isEmpty() || parts->window.isEmpty())
    {
        return refusal(an::ErrorCode::InvalidArgument, unavailable);
    }
    const QString prefix = QString::fromLatin1(kOscSensorPrefix);
    const SpccRepositoryProfile* sensor = findProfile(prefix + parts->sensor + QLatin1Char('.') + parts->suffix);
    const SpccRepositoryProfile* filter = findProfile(prefix + parts->sensor + QStringLiteral(".osc_filter.") +
                                                      parts->filter + QLatin1Char('.') + parts->suffix);
    const SpccRepositoryProfile* window = findProfile(prefix + parts->sensor + QStringLiteral(".osc_lpf.") +
                                                      parts->window + QLatin1Char('.') + parts->suffix);
    if (sensor == nullptr || filter == nullptr || window == nullptr)
    {
        return refusal(an::ErrorCode::InvalidArgument, unavailable);
    }
    if (filter->whiteReferenceId != window->whiteReferenceId || filter->whiteReferenceId != sensor->whiteReferenceId)
    {
        return refusal(an::ErrorCode::InvalidData,
                       QStringLiteral("SPCC response synthesis requires matching white references."));
    }

    SpccRepositoryProfile synthesized;
    synthesized.profile.identifier = profileId.toStdString();
    synthesized.profile.version = filter->profile.version;
    synthesized.profile.kind = spccResponseProfileKind(profileId);
    synthesized.whiteReferenceId = filter->whiteReferenceId;
    synthesized.capabilities.compatibleColorSpaces = filter->capabilities.compatibleColorSpaces;
    synthesized.capabilities.modelsRawWhiteBalance = filter->capabilities.modelsRawWhiteBalance ||
                                                     window->capabilities.modelsRawWhiteBalance ||
                                                     sensor->capabilities.modelsRawWhiteBalance;
    synthesized.capabilities.modelsCameraMatrix = filter->capabilities.modelsCameraMatrix ||
                                                  window->capabilities.modelsCameraMatrix ||
                                                  sensor->capabilities.modelsCameraMatrix;
    synthesized.capabilities.reviewed =
        filter->capabilities.reviewed && window->capabilities.reviewed && sensor->capabilities.reviewed;
    static const std::array<const char*, 3u> channelNames{"red", "green", "blue"};
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        for (const QStringList* ids : {&filter->channelComponentIds[channel], &window->channelComponentIds[channel]})
        {
            for (const QString& id : *ids)
            {
                if (!synthesized.channelComponentIds[channel].contains(id))
                {
                    synthesized.channelComponentIds[channel].append(id);
                }
            }
        }
        an::Result<an::SpectralCurve> combined = an::combineFilterResponses(
            sensor->profile.channels[channel], filter->profile.channels[channel], window->profile.channels[channel],
            synthesized.profile.identifier + "." + channelNames[channel], synthesized.profile.version);
        if (!combined.hasValue())
        {
            return combined.status();
        }
        synthesized.profile.channels[channel] = std::move(*combined);
    }
    return synthesized;
}

SpccAliasResolution SpccResponseRepository::resolveAlias(const QString& cameraModel, const QString& filterName) const
{
    SpccAliasResolution resolution;
    const QString camera = normalizedKey(cameraModel);
    const QString filter = normalizedKey(filterName);
    if (camera.isEmpty() && filter.isEmpty())
    {
        return resolution;
    }
    for (const SpccRepositoryProfile& profile : m_profiles)
    {
        for (const SpccProfileAlias& alias : profile.aliases)
        {
            if (normalizedKey(alias.cameraModel) == camera && normalizedKey(alias.filterName) == filter)
            {
                resolution.candidateProfileIds.append(QString::fromStdString(profile.profile.identifier));
                break;
            }
        }
    }
    if (resolution.candidateProfileIds.size() == 1)
    {
        resolution.profileId = resolution.candidateProfileIds.front();
    }
    resolution.ambiguous = resolution.candidateProfileIds.size() > 1;
    return resolution;
}

} // namespace an::ui::processes
