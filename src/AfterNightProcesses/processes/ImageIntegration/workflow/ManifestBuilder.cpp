// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ManifestBuilder.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/CalibrationMatcher.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/ImageIntegration/model/IntegrationGroupEngine.h"
#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/core/CalibrationMasterDefaults.h"
#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/core/NormalizationOptionParsing.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/core/CometStackingMode.h"
#include "processes/ProcessCommonUtils.h"

#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <limits>
#include <set>
#include <unordered_map>

namespace
{
QString formatDouble(double value, int precision);

/// The master a calibration group's panel names for it, as a master the person assigns from that panel's candidates
/// is named: the group's own supplied master, else the one the run writes for it in the output directory.
[[nodiscard]] QString panelMasterPath(const IntegrationDataModel& model, const QString& outputDirectory,
                                      CalibrationOverrideType type, FrameType frameType, const GroupedFrameSet& group)
{
    QString supplied;
    for (const auto& frame : group.frames)
    {
        if (model.isMasterFrame(frame.id) && (supplied.isEmpty() || frame.id.canonicalPath() < supplied))
        {
            supplied = frame.id.canonicalPath();
        }
    }
    if (!supplied.isEmpty())
    {
        return supplied;
    }
    const QString filename =
        calibrationMasterOutputFilename(type, group.key, model.groupingOptions(frameType), frameType);
    return outputDirectory.trimmed().isEmpty() ? filename : QDir(outputDirectory).absoluteFilePath(filename);
}

/// One kind of calibration group as a matcher's candidates, each named by the master its panel names, so a master
/// assigned by hand resolves to the group that makes it: deciding which groups a run stacks reads only the group a
/// match names.
template <typename CalibrationGroup, typename Key>
[[nodiscard]] std::vector<CalibrationGroup>
matchCandidates(const IntegrationDataModel& model, const QString& outputDirectory, CalibrationOverrideType type,
                FrameType frameType, const GroupedFrameView& groups)
{
    std::vector<CalibrationGroup> candidates;
    candidates.reserve(groups.size());
    for (const auto& group : groups)
    {
        if (group.frames.empty())
        {
            continue;
        }
        CalibrationGroup candidate;
        candidate.key = Key::fromFrame(group.frames.front());
        candidate.groupKey = group.key;
        candidate.masterPath = panelMasterPath(model, outputDirectory, type, frameType, group);
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

/// Adds the group a match names to the groups a run stacks; a match of none, or one the person unassigned, adds
/// nothing.
void addMatchedGroup(std::set<GroupKey>& relevant, const CalibrationMatch& match)
{
    if (match.type != CalibrationMatch::MatchType::None &&
        match.type != CalibrationMatch::MatchType::ManuallyUnassigned && !match.matchedGroupKey.isEmpty())
    {
        relevant.insert(GroupKey{match.matchedGroupKey});
    }
}

[[nodiscard]] QString captureTimestampUtcString(const QDateTime& captured_at)
{
    return captured_at.isValid() ? captured_at.toUTC().toString(Qt::ISODateWithMs) : QString();
}

QString normalizationModeToString(NormalizationMode mode)
{
    switch (mode)
    {
    case NormalizationMode::None:
        return QStringLiteral("None");
    case NormalizationMode::Global:
        return QStringLiteral("Global");
    case NormalizationMode::Local:
        return QStringLiteral("Local");
    }

    return QStringLiteral("Unknown");
}

QString normalizationReferenceModeToString(NormalizationReferenceMode mode)
{
    switch (mode)
    {
    case NormalizationReferenceMode::Automatic:
        return QStringLiteral("Automatic");
    case NormalizationReferenceMode::Manual:
        return QStringLiteral("Manual");
    }

    return QStringLiteral("Unknown");
}

QString describeMetrics(const std::optional<FrameQualityMetrics>& metrics)
{
    if (!metrics.has_value())
    {
        return QStringLiteral("(no metrics)");
    }

    const auto& m = metrics.value();
    return QStringLiteral("stars=%1 fwhm=%2 snr=%3 ecc=%4 zp=%5 astroRms=%6 cat=%7 succeeded=%8")
        .arg(m.stars)
        .arg(formatDouble(m.fwhm, 3))
        .arg(formatDouble(m.snr, 3))
        .arg(formatDouble(m.eccentricity, 3))
        .arg(formatDouble(m.photometric_zero_point, 3))
        .arg(formatDouble(m.astrometric_rms_arcsec, 3))
        .arg(m.catalog_matched_stars)
        .arg(m.succeeded ? QStringLiteral("true") : QStringLiteral("false"));
}

QString describeFrameList(const std::vector<FrameId>& frames)
{
    if (frames.empty())
    {
        return QStringLiteral("(none)");
    }

    QStringList entries;
    entries.reserve(static_cast<int>(frames.size()));
    for (const auto& frame : frames)
    {
        const QString path = frame.canonicalPath();
        const QString name = QFileInfo(path).fileName();
        entries.append(name.isEmpty() ? path : QStringLiteral("%1 [%2]").arg(name, path));
    }
    return entries.join(QStringLiteral("; "));
}

QString normalizeToken(const QString& value)
{
    return value.trimmed().isEmpty() ? QStringLiteral("-") : value;
}

QString resolveDefaultDemosaicLabel()
{
    const QString fallback = QString(SettingsDefaults::RAW_DEMOSAIC_ALGORITHM);
    return process_utils::demosaicLabelFromSettingsKey(
        process_utils::selectGlobalSetting(SettingsKeys::RAW_DEMOSAIC_ALGORITHM, fallback));
}

QString trimTrailingZeros(QString value)
{
    while (value.contains(QLatin1Char('.')) && value.endsWith(QLatin1Char('0')))
    {
        value.chop(1);
    }

    if (value.endsWith(QLatin1Char('.')))
    {
        value.chop(1);
    }

    return value;
}

QString formatDouble(double value, int precision = 3)
{
    return trimTrailingZeros(QString::number(value, 'f', precision));
}

QString normalizationArtifactPathFor(const QString& output_directory, const IntegrationGroupKey& group_key)
{
    if (output_directory.trimmed().isEmpty())
    {
        return QString();
    }

    const QString group_dir_name = ImageIntegration::sanitizeFilename(group_key.toString());
    QDir root(output_directory);
    const QString relative =
        QStringLiteral(".afternight/image_integration/artifacts/normalization/%1/coefficients.json")
            .arg(group_dir_name);
    return root.filePath(relative);
}

QString drizzlePixmapDirFor(const QString& output_directory, const IntegrationGroupKey& group_key)
{
    if (output_directory.trimmed().isEmpty())
    {
        return QString();
    }

    const QString group_dir_name = ImageIntegration::sanitizeFilename(group_key.toString());
    QDir root(output_directory);
    const QString relative =
        QStringLiteral(".afternight/image_integration/artifacts/drizzle/%1/pixmaps").arg(group_dir_name);
    return root.filePath(relative);
}

const WorkflowManifest::RegistrationGroup* findRegistrationGroup(const WorkflowManifest& manifest, const GroupKey& key)
{
    const auto it = std::find_if(
        manifest.registrationGroups.begin(), manifest.registrationGroups.end(),
        [&](const WorkflowManifest::RegistrationGroup& group) { return group.groupKey.toString() == key.toString(); });
    return it == manifest.registrationGroups.end() ? nullptr : &*it;
}

QString stackingModeFromRegistration(const WorkflowManifest::RegistrationGroup* registration_group,
                                     const QString& requested_mode)
{
    Q_UNUSED(requested_mode);

    if (registration_group == nullptr || !ImageIntegration::RegistrationSettingsUtils::usesCometTracking(
                                             registration_group->settings.registrationMethod))
    {
        return QStringLiteral("Stars");
    }

    if (!registration_group->settings.cometTracking.has_value())
    {
        return QStringLiteral("Comet");
    }

    const QString output_mode =
        ImageIntegration::normalizeCometStackingMode(registration_group->settings.cometTracking->outputMode);
    return output_mode.trimmed().isEmpty() ? QStringLiteral("Comet") : output_mode;
}

/// The group's normalization references, the primary first; `logged` says each choice in the log, which the manifest
/// does and the Stacking tab's preview of the stack's reference, asked again as the model changes, does not.
std::vector<FrameId> selectNormalizationReferenceFrames(const IntegrationDataModel& model, const GroupedFrameSet& group,
                                                        const StackingSettings& settings, const bool logged = true)
{
    std::vector<FrameId> frameIds;
    frameIds.reserve(group.frames.size());

    std::unordered_map<FrameId, QString> filtersById;
    filtersById.reserve(group.frames.size());

    for (const auto& frame : group.frames)
    {
        if (frame.excluded)
        {
            continue;
        }
        frameIds.push_back(frame.id);
        filtersById.emplace(frame.id, frame.metadata.filter());
    }

    if (frameIds.empty())
    {
        if (logged)
        {
            LOG_INFO("Normalization reference selection: group='{}' has no eligible frames (all excluded)",
                     group.key.toString().toStdString());
        }
        return {};
    }

    int metrics_available = 0;
    for (const auto& id : frameIds)
    {
        if (model.frameQualityMetrics(id).has_value())
        {
            ++metrics_available;
        }
    }

    if (settings.normalizationReferenceMode == NormalizationReferenceMode::Manual &&
        !settings.manualNormalizationReference.trimmed().isEmpty())
    {
        const FrameId manual(settings.manualNormalizationReference);
        for (const auto& id : frameIds)
        {
            if (id.identityKey() == manual.identityKey())
            {
                const std::vector<FrameId> selected{id};
                if (logged)
                {
                    LOG_INFO(
                        "Normalization reference selection: group='{}' mode={} refMode={} source=Manual -> refs={} "
                        "primary='{}' ({})",
                        group.key.toString().toStdString(),
                        normalizationModeToString(settings.normalizationMode).toStdString(),
                        normalizationReferenceModeToString(settings.normalizationReferenceMode).toStdString(),
                        describeFrameList(selected).toStdString(), id.canonicalPath().toStdString(),
                        describeMetrics(model.frameQualityMetrics(id)).toStdString());
                }
                return selected;
            }
        }

        // A reference chosen by hand that the group does not hold - left out of its stack, or renamed - stays the
        // group's reference, so the stages refuse the group naming it rather than normalize it to a light nobody chose.
        if (logged)
        {
            LOG_WARNING(
                "Normalization reference selection: group='{}' mode={} refMode=Manual manual='{}' not found in group "
                "(eligible frames={}, metrics={}); kept, so the group is refused",
                group.key.toString().toStdString(), normalizationModeToString(settings.normalizationMode).toStdString(),
                settings.manualNormalizationReference.toStdString(), static_cast<int>(frameIds.size()),
                metrics_available);
        }
        return {manual};
    }

    const auto getMetrics = [&](const FrameId& id) { return model.frameQualityMetrics(id); };

    const auto getFilter = [&](const FrameId& id) -> QString {
        const auto it = filtersById.find(id);
        if (it != filtersById.end())
        {
            return it->second;
        }
        return QString();
    };

    std::vector<FrameId> ordered = rankFramesByQualityScore(frameIds, getMetrics, getFilter);
    if (ordered.empty())
    {
        std::sort(frameIds.begin(), frameIds.end());

        if (logged)
        {
            LOG_WARNING(
                "Normalization reference selection: group='{}' mode={} refMode={} -> no quality metrics available "
                "(eligible frames={}, metrics=0); falling back to lexicographic first='{}'",
                group.key.toString().toStdString(), normalizationModeToString(settings.normalizationMode).toStdString(),
                normalizationReferenceModeToString(settings.normalizationReferenceMode).toStdString(),
                static_cast<int>(frameIds.size()), frameIds.front().canonicalPath().toStdString());
        }
        return {frameIds.front()};
    }

    if (settings.normalizationMode == NormalizationMode::Local && settings.localNormalization.enabled)
    {
        auto rankByCriteria = [&]() -> std::vector<FrameId> {
            const auto criteria =
                ImageIntegration::parseLocalNormEvaluationCriteria(settings.localNormalization.evaluationCriteria);

            if (criteria == ImageIntegration::LocalNormEvaluationCriteria::QualityScore ||
                criteria == ImageIntegration::LocalNormEvaluationCriteria::PsfSignalWeight)
            {
                return ordered;
            }

            struct ScoredFrame
            {
                FrameId id;
                double score = -std::numeric_limits<double>::infinity();
            };

            std::vector<ScoredFrame> scored;
            scored.reserve(frameIds.size());

            for (const auto& id : frameIds)
            {
                const auto metrics = getMetrics(id);
                if (!metrics.has_value())
                {
                    continue;
                }

                const auto& m = metrics.value();
                if (!m.succeeded)
                {
                    continue;
                }

                double score = -std::numeric_limits<double>::infinity();
                switch (criteria)
                {
                case ImageIntegration::LocalNormEvaluationCriteria::PsfSnr:
                    score = m.snr;
                    break;
                case ImageIntegration::LocalNormEvaluationCriteria::StarCount:
                    score = static_cast<double>(m.stars);
                    break;
                case ImageIntegration::LocalNormEvaluationCriteria::Median:
                case ImageIntegration::LocalNormEvaluationCriteria::MStar:
                    // No direct median/background metric is currently tracked in FrameQualityMetrics.
                    // Fall back to the default quality ranking.
                    score = frameQualityScore(m);
                    break;
                case ImageIntegration::LocalNormEvaluationCriteria::PsfSignalWeight:
                case ImageIntegration::LocalNormEvaluationCriteria::QualityScore:
                    score = frameQualityScore(m);
                    break;
                }

                scored.push_back({id, score});
            }

            if (scored.empty())
            {
                return ordered;
            }

            std::sort(scored.begin(), scored.end(), [](const ScoredFrame& a, const ScoredFrame& b) {
                if (a.score == b.score)
                {
                    return a.id < b.id;
                }
                return a.score > b.score;
            });

            std::vector<FrameId> ranked;
            ranked.reserve(scored.size());
            for (const auto& item : scored)
            {
                ranked.push_back(item.id);
            }
            return ranked;
        };

        std::vector<FrameId> criteria_ordered = rankByCriteria();
        const QString strategy = settings.localNormalization.referenceStrategy.trimmed();
        if (ImageIntegration::isIntegrateBestFramesLocalNormReferenceStrategy(strategy))
        {
            const size_t max_refs =
                (std::max<size_t>)(1, static_cast<size_t>(settings.localNormalization.maxIntegratedFrames));
            if (criteria_ordered.size() > max_refs)
            {
                criteria_ordered.resize(max_refs);
            }

            if (logged)
            {
                LOG_INFO("Normalization reference selection: group='{}' mode=Local strategy='Integrate Best Frames' "
                         "source=Automatic best frames eligible frames={} metrics={} -> chosen refs={} primary='{}' "
                         "all='{}' ({})",
                         group.key.toString().toStdString(), static_cast<int>(frameIds.size()), metrics_available,
                         static_cast<int>(criteria_ordered.size()),
                         criteria_ordered.front().canonicalPath().toStdString(),
                         describeFrameList(criteria_ordered).toStdString(),
                         describeMetrics(model.frameQualityMetrics(criteria_ordered.front())).toStdString());
            }
            return criteria_ordered;
        }

        if (logged)
        {
            LOG_INFO(
                "Normalization reference selection: group='{}' mode=Local strategy='{}' source=Automatic best frame "
                "eligible frames={} metrics={} -> chosen refs={} primary='{}' ({})",
                group.key.toString().toStdString(), strategy.toStdString(), static_cast<int>(frameIds.size()),
                metrics_available, describeFrameList({criteria_ordered.front()}).toStdString(),
                criteria_ordered.front().canonicalPath().toStdString(),
                describeMetrics(model.frameQualityMetrics(criteria_ordered.front())).toStdString());
        }
        return {criteria_ordered.front()};
    }

    if (logged)
    {
        LOG_INFO(
            "Normalization reference selection: group='{}' mode={} source=Automatic best quality eligible frames={} "
            "metrics={} -> chosen refs={} primary='{}' ({})",
            group.key.toString().toStdString(), normalizationModeToString(settings.normalizationMode).toStdString(),
            static_cast<int>(frameIds.size()), metrics_available, describeFrameList({ordered.front()}).toStdString(),
            ordered.front().canonicalPath().toStdString(),
            describeMetrics(model.frameQualityMetrics(ordered.front())).toStdString());
    }
    return {ordered.front()};
}

QString integrationFilename(const GroupKey& group_key, const GroupingOptions& options, FrameType frameType)
{
    const QString label = group_key.toDisplayString(options, frameType);
    return QStringLiteral("stacked_%1.fit").arg(ImageIntegration::sanitizeFilename(label));
}

QString outputPathFor(const QString& output_directory, const QString& filename)
{
    if (output_directory.trimmed().isEmpty())
    {
        return filename;
    }

    return QDir(output_directory).filePath(filename);
}

bool outputExists(const QString& output_directory, const QString& filename)
{
    if (output_directory.trimmed().isEmpty() || filename.trimmed().isEmpty())
    {
        return false;
    }

    return QFileInfo::exists(outputPathFor(output_directory, filename));
}

std::vector<FrameId> frameIdsFromGroup(const GroupedFrameSet& group)
{
    std::vector<FrameId> ids;
    ids.reserve(group.frames.size());
    for (const auto& frame : group.frames)
    {
        ids.push_back(frame.id);
    }
    return ids;
}

GroupedFrameSet stackIncludedGroup(const IntegrationDataModel& model, const GroupedFrameSet& group)
{
    GroupedFrameSet filtered;
    filtered.key = group.key;
    filtered.frames.reserve(group.frames.size());
    for (const auto& frame : group.frames)
    {
        if (model.isFrameStackExcluded(group.key, frame.id))
        {
            continue;
        }
        filtered.frames.push_back(frame);
    }
    return filtered;
}

} // namespace

ManifestBuilder::ManifestBuilder(const IntegrationDataModel& model) : m_model(model) {}

WorkflowManifest ManifestBuilder::build() const
{
    return buildInternal(std::nullopt);
}

WorkflowManifest ManifestBuilder::buildForIntegrationGroup(const IntegrationGroupKey& integrationGroupKey) const
{
    return buildInternal(integrationGroupKey);
}

ManifestBuilder& ManifestBuilder::setReprocessExisting(bool reprocess)
{
    m_reprocessExisting = reprocess;
    return *this;
}

ManifestBuilder& ManifestBuilder::setIncludeRejectionMaps(bool include)
{
    m_includeRejectionMaps = include;
    return *this;
}

ManifestBuilder& ManifestBuilder::setSkipEmptyGroups(bool skip)
{
    m_skipEmptyGroups = skip;
    return *this;
}

ManifestBuilder& ManifestBuilder::setIncludeBiasFrames(bool include)
{
    m_includeBiasFrames = include;
    return *this;
}

ManifestBuilder& ManifestBuilder::setIncludeDarkFrames(bool include)
{
    m_includeDarkFrames = include;
    return *this;
}

ManifestBuilder& ManifestBuilder::setIncludeFlatFrames(bool include)
{
    m_includeFlatFrames = include;
    return *this;
}

ManifestBuilder& ManifestBuilder::setIncludeLightFrames(bool include)
{
    m_includeLightFrames = include;
    return *this;
}

WorkflowManifest ManifestBuilder::buildInternal(std::optional<IntegrationGroupKey> integration_group) const
{
    WorkflowManifest manifest;
    manifest.outputDirectory = m_model.outputDirectory();
    manifest.reprocessExisting = m_reprocessExisting;
    manifest.calibrateLightsInMemory = m_model.lightCalibrationCacheMode() == LightCalibrationCacheMode::InMemory;
    manifest.plateSolve.defaultSolver = process_utils::selectGlobalSetting(SettingsKeys::PLATESOLVE_DEFAULT_SOLVER,
                                                                           SettingsDefaults::PLATESOLVE_DEFAULT_SOLVER);
    manifest.plateSolve.astapPath =
        process_utils::selectGlobalSetting(SettingsKeys::TOOLS_ASTAP_PATH, SettingsDefaults::TOOLS_ASTAP_PATH);
    manifest.plateSolve.astrometryLocalPath = process_utils::selectGlobalSetting(
        SettingsKeys::TOOLS_ASTROMETRY_LOCAL_PATH, SettingsDefaults::TOOLS_ASTROMETRY_LOCAL_PATH);
    manifest.astrometryApiKeyHash = ImageIntegration::astrometryApiKeyHash(process_utils::selectGlobalSetting(
        SettingsKeys::TOOLS_ASTROMETRY_API_KEY, SettingsDefaults::TOOLS_ASTROMETRY_API_KEY));
    manifest.plateSolve.indexPath = process_utils::selectGlobalSetting(SettingsKeys::PLATESOLVE_INDEX_PATH,
                                                                       SettingsDefaults::PLATESOLVE_INDEX_PATH);
    manifest.plateSolve.gaiaCatalogDirectory = process_utils::selectGlobalSetting(
        SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY, SettingsDefaults::PLATESOLVE_GAIA_CATALOG_DIRECTORY);
    manifest.plateSolve.gaiaCatalogPack = process_utils::selectGlobalSetting(
        SettingsKeys::PLATESOLVE_GAIA_CATALOG_PACK, SettingsDefaults::PLATESOLVE_GAIA_CATALOG_PACK);
    const double focal_length_mm =
        process_utils::selectGlobalSetting(SettingsKeys::PLATESOLVE_FOCAL_LENGTH_MM,
                                           QString::number(SettingsDefaults::PLATESOLVE_FOCAL_LENGTH_MM))
            .toDouble();
    if (focal_length_mm > 0.0)
    {
        manifest.plateSolve.focalLengthMm = focal_length_mm;
    }
    const double pixel_size_um =
        process_utils::selectGlobalSetting(SettingsKeys::PLATESOLVE_PIXEL_SIZE_UM,
                                           QString::number(SettingsDefaults::PLATESOLVE_PIXEL_SIZE_UM))
            .toDouble();
    if (pixel_size_um > 0.0)
    {
        manifest.plateSolve.pixelSizeUm = pixel_size_um;
    }
    manifest.plateSolve.searchRadius =
        process_utils::selectGlobalSetting(SettingsKeys::PLATESOLVE_SEARCH_RADIUS,
                                           QString::number(SettingsDefaults::PLATESOLVE_SEARCH_RADIUS))
            .toDouble();
    manifest.plateSolve.downsample =
        process_utils::selectGlobalSetting(SettingsKeys::PLATESOLVE_DOWNSAMPLE,
                                           QString::number(SettingsDefaults::PLATESOLVE_DOWNSAMPLE))
            .toInt();
    manifest.plateSolve.timeoutSeconds =
        process_utils::selectGlobalSetting(SettingsKeys::PLATESOLVE_TIMEOUT,
                                           QString::number(SettingsDefaults::PLATESOLVE_TIMEOUT))
            .toInt();
    manifest.plateSolve.useHints = process_utils::selectGlobalSetting(SettingsKeys::PLATESOLVE_USE_HINTS,
                                                                      SettingsDefaults::PLATESOLVE_USE_HINTS
                                                                          ? QStringLiteral("1")
                                                                          : QStringLiteral("0")) == QStringLiteral("1");
    if (integration_group.has_value())
    {
        manifest.executionScope = QStringLiteral("integration_group");
        manifest.executionScopeKey = integration_group->toString();
    }
    else
    {
        manifest.executionScope = QStringLiteral("full");
        manifest.executionScopeKey.clear();
    }

    const GroupedFrameView empty_groups;

    const auto& light_groups = m_model.lightGroups();
    const auto& bias_groups = m_includeBiasFrames ? m_model.biasGroups() : empty_groups;
    const auto& dark_groups = m_includeDarkFrames ? m_model.darkGroups() : empty_groups;
    const auto& flat_groups = m_includeFlatFrames ? m_model.flatGroups() : empty_groups;

    const auto& all_light_frames = m_model.lightFrames().frames();
    std::map<FrameId, FrameInfo> light_frame_map;
    for (const auto& frame : all_light_frames)
    {
        light_frame_map.emplace(frame.id, frame);
    }

    std::vector<FrameInfo> frames_to_process;
    if (integration_group.has_value())
    {
        IntegrationGroupEngine engine;
        auto groups = engine.computeGroups(all_light_frames, m_model.lightIntegrationGroupingOptions());
        const auto it = groups.find(*integration_group);
        if (it != groups.end())
        {
            frames_to_process.reserve(it->second.size());
            for (const auto& id : it->second)
            {
                const auto frame_it = light_frame_map.find(id);
                if (frame_it != light_frame_map.end())
                {
                    frames_to_process.push_back(frame_it->second);
                }
            }
        }
    }
    else if (m_includeLightFrames)
    {
        frames_to_process = all_light_frames;
    }

    std::vector<GroupedFrameSet> integration_sets;
    if (!frames_to_process.empty())
    {
        IntegrationGroupEngine engine;
        auto groups = engine.computeGroups(frames_to_process, m_model.lightIntegrationGroupingOptions());

        std::map<FrameId, FrameInfo> process_frame_map;
        for (const auto& frame : frames_to_process)
        {
            process_frame_map.emplace(frame.id, frame);
        }

        integration_sets.reserve(groups.size());
        for (const auto& entry : groups)
        {
            GroupedFrameSet set;
            set.key = entry.first.key;
            set.frames.reserve(entry.second.size());
            for (const auto& id : entry.second)
            {
                const auto frame_it = process_frame_map.find(id);
                if (frame_it != process_frame_map.end())
                {
                    set.frames.push_back(frame_it->second);
                }
            }
            integration_sets.push_back(std::move(set));
        }
    }

    std::sort(integration_sets.begin(), integration_sets.end(),
              [](const GroupedFrameSet& lhs, const GroupedFrameSet& rhs) { return lhs.key < rhs.key; });

    const auto& bias_options = m_model.groupingOptions(FrameType::Bias);
    const auto& dark_options = m_model.groupingOptions(FrameType::Dark);
    const auto& flat_options = m_model.groupingOptions(FrameType::Flat);
    const auto& light_integration_options = m_model.lightIntegrationGroupingOptions();

    std::set<GroupKey> relevant_bias;
    std::set<GroupKey> relevant_dark;
    std::set<GroupKey> relevant_flat;

    if (!frames_to_process.empty())
    {
        std::vector<std::pair<GroupKey, BiasGroupKey>> bias_keys;
        std::vector<std::pair<GroupKey, DarkGroupKey>> dark_keys;
        std::vector<std::pair<GroupKey, FlatGroupKey>> flat_keys;

        for (const auto& group : bias_groups)
        {
            if (!group.frames.empty())
            {
                bias_keys.emplace_back(group.key, BiasGroupKey::fromFrame(group.frames.front()));
            }
        }
        for (const auto& group : dark_groups)
        {
            if (!group.frames.empty())
            {
                dark_keys.emplace_back(group.key, DarkGroupKey::fromFrame(group.frames.front()));
            }
        }
        for (const auto& group : flat_groups)
        {
            if (!group.frames.empty())
            {
                flat_keys.emplace_back(group.key, FlatGroupKey::fromFrame(group.frames.front()));
            }
        }

        // Iterate per frame to ensure we capture all calibration requirements
        // even if integration groups merge different calibration needs.
        for (const auto& frame : frames_to_process)
        {
            if (frame.excluded)
            {
                continue;
            }

            const LightGroupKey light_key = LightGroupKey::fromFrame(frame);
            const CalibrationGroupKey match_key = CalibrationGroupKey::fromLightKey(light_key);

            for (const auto& entry : bias_keys)
            {
                if (match_key.matchesBias(entry.second, bias_options))
                {
                    relevant_bias.insert(entry.first);
                }
            }
            for (const auto& entry : dark_keys)
            {
                if (match_key.matchesDark(entry.second, dark_options))
                {
                    relevant_dark.insert(entry.first);
                }
            }
            for (const auto& entry : flat_keys)
            {
                if (match_key.matchesFlat(entry.second, flat_options))
                {
                    relevant_flat.insert(entry.first);
                }
            }
        }

        // A group is stacked, too, when a light or a flat the run calibrates is matched to it, as the calibration
        // below matches them: a light's dark found by the exposure fallback or assigned by hand, and a flat's dark
        // taken at the flats' exposure, which no light need share. Each names a master the run must make.
        const CalibrationMatchingPolicy& policy = m_model.calibrationMatchingPolicy();
        CalibrationMatcher relevance(bias_options, dark_options, flat_options, policy);
        relevance.setBiasGroups(matchCandidates<BiasCalibrationGroup, BiasGroupKey>(
            m_model, manifest.outputDirectory, CalibrationOverrideType::Bias, FrameType::Bias, bias_groups));
        relevance.setDarkGroups(matchCandidates<DarkCalibrationGroup, DarkGroupKey>(
            m_model, manifest.outputDirectory, CalibrationOverrideType::Dark, FrameType::Dark, dark_groups));
        relevance.setFlatGroups(matchCandidates<FlatCalibrationGroup, FlatGroupKey>(
            m_model, manifest.outputDirectory, CalibrationOverrideType::Flat, FrameType::Flat, flat_groups));
        relevance.setOverrides(m_model.calibrationOverrides());

        const auto& light_calibration_settings = m_model.lightCalibrationSettings();
        GroupingEngine light_engine;
        for (const auto& group :
             light_engine.groupFrames(frames_to_process, FrameType::Light, m_model.groupingOptions(FrameType::Light)))
        {
            if (group.frames.empty())
            {
                continue;
            }
            if (const auto it = light_calibration_settings.find(group.key);
                it != light_calibration_settings.end() && it->second.preCalibrated)
            {
                continue;
            }
            const LightGroupKey light_key = LightGroupKey::fromFrame(group.frames.front());
            const CalibrationMatch dark_match = relevance.findDark(light_key, averageTemperatureC(group.frames));
            addMatchedGroup(relevant_dark, dark_match);
            addMatchedGroup(relevant_flat, relevance.findFlat(light_key));
            if (lightTakesBias(dark_match, policy.subtractBiasFromDark))
            {
                addMatchedGroup(relevant_bias, relevance.findBias(light_key));
            }
        }

        for (const auto& group : flat_groups)
        {
            if (group.frames.empty() || relevant_flat.find(group.key) == relevant_flat.end())
            {
                continue;
            }
            const bool supplied_master = std::any_of(group.frames.begin(), group.frames.end(), [&](const auto& frame) {
                return !frame.excluded && m_model.isMasterFrame(frame.id);
            });
            if (supplied_master)
            {
                continue;
            }
            const LightGroupKey flat_key_as_light = LightGroupKey::fromFrame(group.frames.front());
            const CalibrationMatch dark_match =
                relevance.findDark(flat_key_as_light, averageTemperatureC(group.frames));
            addMatchedGroup(relevant_dark, dark_match);
            if (flatTakesBias(dark_match))
            {
                addMatchedGroup(relevant_bias, relevance.findBias(flat_key_as_light));
            }
        }
    }

    const auto& bias_settings = m_model.stackingSettings(FrameType::Bias);
    const auto& dark_settings = m_model.stackingSettings(FrameType::Dark);
    const auto& flat_settings = m_model.stackingSettings(FrameType::Flat);
    const auto& light_settings = m_model.stackingSettings(FrameType::Light);
    const auto& light_calib_settings = m_model.lightCalibrationSettings();

    auto build_calibration_stage =
        [&](const QString& label, FrameType frameType, const GroupedFrameView& groups,
            const std::set<GroupKey>& relevant_keys,
            const StackingSettingsMap& settings_map) -> std::optional<WorkflowManifest::CalibrationStage> {
        if (groups.empty() && m_skipEmptyGroups)
        {
            return std::nullopt;
        }

        WorkflowManifest::CalibrationStage stage;
        stage.frameType = label;

        const bool filter_by_relevance = !frames_to_process.empty();

        for (const auto& group : groups)
        {
            if (group.frames.empty() && m_skipEmptyGroups)
            {
                continue;
            }
            if (filter_by_relevance && relevant_keys.find(group.key) == relevant_keys.end())
            {
                continue;
            }

            WorkflowManifest::CalibrationStage::Group manifest_group;
            manifest_group.groupKey = group.key;

            CalibrationOverrideType overrideType = CalibrationOverrideType::Bias;
            switch (frameType)
            {
            case FrameType::Bias:
                overrideType = CalibrationOverrideType::Bias;
                break;
            case FrameType::Dark:
                overrideType = CalibrationOverrideType::Dark;
                break;
            case FrameType::Flat:
                overrideType = CalibrationOverrideType::Flat;
                break;
            default:
                break;
            }

            manifest_group.outputFilename =
                calibrationMasterOutputFilename(overrideType, group.key, m_model.groupingOptions(frameType), frameType);
            manifest_group.inputFiles = frameIdsFromGroup(group);

            StackingSettings stacking = defaultCalibrationStackingSettings(frameType);
            if (auto settings_it = settings_map.find(group.key); settings_it != settings_map.end())
            {
                stacking = settings_it->second;
            }
            manifest_group.stacking = normalizeCalibrationStackingSettings(frameType, stacking);
            // A bias or dark master is combined without normalization. The manifest keeps the normalization such a
            // group still states beside the settings the master is made with, which drop it, so the master's stack
            // says it leaves it out and a choice that changes nothing is not part of what the master is.
            if (manifest_group.stacking.normalizationMode != stacking.normalizationMode)
            {
                manifest_group.statedNormalization = stacking.normalizationMode;
            }

            // If the user provided a master calibration frame in this group,
            // treat it as the existing master and skip stacking for this group.
            for (const auto& frame : group.frames)
            {
                if (frame.excluded)
                {
                    continue;
                }

                if (m_model.isMasterFrame(frame.id))
                {
                    manifest_group.skipIfExists = true;
                    manifest_group.existingMaster = frame.id;
                    break;
                }
            }

            if (!manifest_group.skipIfExists && !m_reprocessExisting &&
                outputExists(manifest.outputDirectory, manifest_group.outputFilename))
            {
                manifest_group.skipIfExists = true;
            }

            stage.groups.push_back(std::move(manifest_group));
        }

        if (stage.groups.empty() && m_skipEmptyGroups)
        {
            return std::nullopt;
        }

        return stage;
    };

    manifest.biasStage =
        build_calibration_stage(QStringLiteral("BIAS"), FrameType::Bias, bias_groups, relevant_bias, bias_settings);
    manifest.darkStage =
        build_calibration_stage(QStringLiteral("DARK"), FrameType::Dark, dark_groups, relevant_dark, dark_settings);
    manifest.flatStage =
        build_calibration_stage(QStringLiteral("FLAT"), FrameType::Flat, flat_groups, relevant_flat, flat_settings);

    // A master dark is stacked from its frames as they are, holding their bias, in CCD mode as in standard mode, as
    // a supplied master dark does: CCD mode subtracts the bias from the master once, where a light is calibrated.

    // Flat stacking calibration: Flats should be calibrated with bias and/or dark before stacking.
    // Only omit calibration masters for user-provided masters, which are used as-is.
    // Generated existing outputs still keep dependency metadata for incremental comparison.
    if (manifest.flatStage.has_value())
    {
        CalibrationMatcher flat_matcher(m_model.groupingOptions(FrameType::Bias),
                                        m_model.groupingOptions(FrameType::Dark),
                                        m_model.groupingOptions(FrameType::Flat), m_model.calibrationMatchingPolicy());

        // Populate bias calibration groups
        std::vector<BiasCalibrationGroup> bias_calibration_groups;
        bias_calibration_groups.reserve(bias_groups.size());

        for (const auto& group : bias_groups)
        {
            if (group.frames.empty())
            {
                continue;
            }

            BiasCalibrationGroup entry;
            entry.key = BiasGroupKey::fromFrame(group.frames.front());
            entry.groupKey = group.key;

            QString best_existing;
            for (const auto& frame : group.frames)
            {
                if (frame.excluded)
                {
                    continue;
                }
                if (m_model.isMasterFrame(frame.id))
                {
                    best_existing = frame.id.canonicalPath();
                    break;
                }
            }

            const QString output_filename = calibrationMasterOutputFilename(
                CalibrationOverrideType::Bias, group.key, m_model.groupingOptions(FrameType::Bias), FrameType::Bias);
            const QString output_path = QDir(manifest.outputDirectory).filePath(output_filename);

            if (!best_existing.isEmpty())
            {
                entry.masterPath = best_existing;
            }
            else if (!m_reprocessExisting && outputExists(manifest.outputDirectory, output_filename))
            {
                entry.masterPath = output_path;
            }
            else
            {
                entry.masterPath = manifest.outputDirectory.trimmed().isEmpty() ? output_filename : output_path;
            }
            bias_calibration_groups.push_back(std::move(entry));
        }

        // Populate dark calibration groups
        std::vector<DarkCalibrationGroup> dark_calibration_groups;
        dark_calibration_groups.reserve(dark_groups.size());

        for (const auto& group : dark_groups)
        {
            if (group.frames.empty())
            {
                continue;
            }

            DarkCalibrationGroup entry;
            entry.key = DarkGroupKey::fromFrame(group.frames.front());
            entry.groupKey = group.key;

            QString best_existing;
            for (const auto& frame : group.frames)
            {
                if (frame.excluded)
                {
                    continue;
                }
                if (m_model.isMasterFrame(frame.id))
                {
                    best_existing = frame.id.canonicalPath();
                    break;
                }
            }

            const QString output_filename = calibrationMasterOutputFilename(
                CalibrationOverrideType::Dark, group.key, m_model.groupingOptions(FrameType::Dark), FrameType::Dark);
            const QString output_path = QDir(manifest.outputDirectory).filePath(output_filename);

            if (!best_existing.isEmpty())
            {
                entry.masterPath = best_existing;
            }
            else if (!m_reprocessExisting && outputExists(manifest.outputDirectory, output_filename))
            {
                entry.masterPath = output_path;
            }
            else
            {
                entry.masterPath = manifest.outputDirectory.trimmed().isEmpty() ? output_filename : output_path;
            }
            dark_calibration_groups.push_back(std::move(entry));
        }

        flat_matcher.setBiasGroups(std::move(bias_calibration_groups));
        flat_matcher.setDarkGroups(std::move(dark_calibration_groups));
        flat_matcher.setOverrides(m_model.calibrationOverrides());

        for (auto& group : manifest.flatStage->groups)
        {
            if (group.existingMaster.has_value())
            {
                continue;
            }

            auto it = std::find_if(flat_groups.begin(), flat_groups.end(),
                                   [&](const GroupedFrameSet& set) { return set.key == group.groupKey; });
            if (it == flat_groups.end() || it->frames.empty())
            {
                continue;
            }

            // A flat is matched as a light of its exposure and temperature is, and takes its dark, which holds the
            // bias, so it takes the bias only when no dark is applied to it: none matched, or the person unassigned
            // it. In CCD mode too: taking the bias from the flat and from the dark leaves the same flat at its dark
            // factor of one.
            const LightGroupKey flat_key_as_light = LightGroupKey::fromFrame(it->frames.front());

            const CalibrationMatch dark_match =
                flat_matcher.findDark(flat_key_as_light, averageTemperatureC(it->frames));
            if (!dark_match.masterPath.isEmpty() && appliesMaster(dark_match))
            {
                group.darkMaster = FrameId(dark_match.masterPath);
            }

            if (flatTakesBias(dark_match))
            {
                const CalibrationMatch bias_match = flat_matcher.findBias(flat_key_as_light);
                if (!bias_match.masterPath.isEmpty() &&
                    bias_match.type != CalibrationMatch::MatchType::ManuallyUnassigned)
                {
                    group.biasMaster = FrameId(bias_match.masterPath);
                }
            }
        }
    }

    // Registration grouping is independent of light grouping options.
    std::map<GroupKey, WorkflowManifest::RegistrationGroup> registration_map;

    for (const auto& group : integration_sets)
    {
        for (const auto& frame : group.frames)
        {
            if (frame.excluded)
            {
                continue;
            }

            const RegistrationGroupKey reg_key_struct = RegistrationGroupKey::fromFrame(frame);
            const GroupKey reg_key = RegistrationGroupEngine::toMachineGroupKey(reg_key_struct);

            auto& reg_group = registration_map[reg_key];
            reg_group.groupKey = reg_key;
            reg_group.resolution = reg_key_struct.resolution;
            reg_group.object = reg_key_struct.objectName;
            reg_group.lightFiles.push_back(frame.id);
            reg_group.lightExposureSeconds.push_back((std::max)(0.0, frame.metadata.exposureSeconds()));
            reg_group.lightCaptureTimestampsUtc.push_back(captureTimestampUtcString(frame.metadata.capturedAtUtc()));
        }
    }

    for (auto& entry : registration_map)
    {
        auto& reg_group = entry.second;
        auto settings_it = m_model.registrationSettings().find(reg_group.groupKey);
        if (settings_it != m_model.registrationSettings().end())
        {
            reg_group.settings = settings_it->second;
        }

        const QString debayer_method = reg_group.settings.debayerMethod.trimmed();
        if (debayer_method.isEmpty() ||
            debayer_method.compare(process_utils::DEMOSAIC_DEFAULT_KEY, Qt::CaseInsensitive) == 0)
        {
            reg_group.settings.debayerMethod = resolveDefaultDemosaicLabel();
        }

        const QString ref_path = reg_group.settings.referenceImage.trimmed();
        if (!ref_path.isEmpty())
        {
            const FrameId reference_frame(ref_path);
            const bool reference_is_included = std::find(reg_group.lightFiles.cbegin(), reg_group.lightFiles.cend(),
                                                         reference_frame) != reg_group.lightFiles.cend();
            if (reference_is_included)
            {
                reg_group.referenceFrame = reference_frame;
            }
            else
            {
                reg_group.settings.referenceImage.clear();
            }
        }
    }

    manifest.registrationGroups.clear();
    manifest.registrationGroups.reserve(registration_map.size());
    for (auto& entry : registration_map)
    {
        auto& reg_group = entry.second;
        QHash<QString, double> exposure_by_frame_path;
        QHash<QString, QString> capture_timestamp_by_frame_path;
        const size_t metadata_count =
            (std::min)(reg_group.lightFiles.size(),
                       (std::min)(reg_group.lightExposureSeconds.size(), reg_group.lightCaptureTimestampsUtc.size()));
        exposure_by_frame_path.reserve(static_cast<int>(metadata_count));
        capture_timestamp_by_frame_path.reserve(static_cast<int>(metadata_count));
        for (size_t i = 0; i < metadata_count; ++i)
        {
            const QString frame_path = reg_group.lightFiles[i].canonicalPath();
            exposure_by_frame_path.insert(frame_path, reg_group.lightExposureSeconds[i]);
            capture_timestamp_by_frame_path.insert(frame_path, reg_group.lightCaptureTimestampsUtc[i]);
        }

        std::sort(reg_group.lightFiles.begin(), reg_group.lightFiles.end());
        reg_group.lightFiles.erase(std::unique(reg_group.lightFiles.begin(), reg_group.lightFiles.end()),
                                   reg_group.lightFiles.end());
        std::vector<double> aligned_exposures;
        std::vector<QString> aligned_capture_timestamps;
        aligned_exposures.reserve(reg_group.lightFiles.size());
        aligned_capture_timestamps.reserve(reg_group.lightFiles.size());
        for (const FrameId& frame_id : reg_group.lightFiles)
        {
            const QString frame_path = frame_id.canonicalPath();
            const auto exposure_it = exposure_by_frame_path.constFind(frame_path);
            const auto timestamp_it = capture_timestamp_by_frame_path.constFind(frame_path);
            aligned_exposures.push_back(exposure_it != exposure_by_frame_path.constEnd() ? exposure_it.value() : 0.0);
            aligned_capture_timestamps.push_back(
                timestamp_it != capture_timestamp_by_frame_path.constEnd() ? timestamp_it.value() : QString());
        }
        reg_group.lightExposureSeconds = std::move(aligned_exposures);
        reg_group.lightCaptureTimestampsUtc = std::move(aligned_capture_timestamps);
        manifest.registrationGroups.push_back(std::move(reg_group));
    }

    QSet<QString> usedIntegrationOutputFilenames;
    for (const auto& group : integration_sets)
    {
        const GroupedFrameSet stack_group = stackIncludedGroup(m_model, group);
        if (stack_group.frames.empty())
        {
            continue;
        }

        const LightGroupKey light_key = LightGroupKey::fromFrame(stack_group.frames.front());
        const QString resolution = light_integration_options.byResolution ? light_key.resolution : QStringLiteral("-");
        const QString object = light_integration_options.byObject ? light_key.objectName : QStringLiteral("-");
        const QString filter = light_integration_options.byFilter ? light_key.filter : QStringLiteral("-");
        const QString exposure =
            light_integration_options.byExposure ? formatDouble(light_key.exposureSeconds) : QStringLiteral("-");

        WorkflowManifest::IntegrationGroup manifest_group;
        manifest_group.groupKey = IntegrationGroupKey{group.key};
        const QString baseOutputFilename = integrationFilename(group.key, light_integration_options, FrameType::Light);
        QString resolvedOutputFilename = baseOutputFilename;
        if (usedIntegrationOutputFilenames.contains(resolvedOutputFilename))
        {
            const QString hashSuffix =
                QStringLiteral("_%1").arg(ImageIntegration::stableShortHash(group.key.toString(), 8));
            resolvedOutputFilename = ImageIntegration::appendSuffixBeforeExtension(baseOutputFilename, hashSuffix);
            int dedupeIndex = 2;
            while (usedIntegrationOutputFilenames.contains(resolvedOutputFilename))
            {
                resolvedOutputFilename = ImageIntegration::appendSuffixBeforeExtension(
                    baseOutputFilename, QStringLiteral("%1_%2").arg(hashSuffix).arg(dedupeIndex));
                ++dedupeIndex;
            }

            LOG_WARNING("ManifestBuilder: output filename collision detected for integration group '{}'; "
                        "using '{}' instead of '{}'",
                        group.key.toString().toStdString(), resolvedOutputFilename.toStdString(),
                        baseOutputFilename.toStdString());
        }
        usedIntegrationOutputFilenames.insert(resolvedOutputFilename);
        manifest_group.outputFilename = resolvedOutputFilename;
        manifest_group.resolution = resolution;
        manifest_group.object = object;
        manifest_group.filter = filter;
        manifest_group.exposure = exposure;
        manifest_group.lightFiles = frameIdsFromGroup(stack_group);
        std::sort(manifest_group.lightFiles.begin(), manifest_group.lightFiles.end());
        manifest_group.lightFiles.erase(std::unique(manifest_group.lightFiles.begin(), manifest_group.lightFiles.end()),
                                        manifest_group.lightFiles.end());
        {
            QHash<QString, double> exposureByFramePath;
            exposureByFramePath.reserve(static_cast<int>(stack_group.frames.size()));
            for (const auto& frame : stack_group.frames)
            {
                const double exposureSeconds = (std::max)(0.0, frame.metadata.exposureSeconds());
                exposureByFramePath.insert(frame.id.canonicalPath(), exposureSeconds);
            }

            manifest_group.lightExposureSeconds.reserve(manifest_group.lightFiles.size());
            for (const auto& frameId : manifest_group.lightFiles)
            {
                const auto it = exposureByFramePath.constFind(frameId.canonicalPath());
                manifest_group.lightExposureSeconds.push_back(it != exposureByFramePath.constEnd() ? it.value() : 0.0);
            }
        }

        auto settings_it = light_settings.find(group.key);
        if (settings_it != light_settings.end())
        {
            manifest_group.stacking = settings_it->second;
        }

        // Use actual frame metadata for registration group mapping.
        manifest_group.registrationGroupKey =
            RegistrationGroupEngine::toMachineGroupKey(RegistrationGroupKey::fromFrame(stack_group.frames.front()));
        manifest_group.stacking.cometStackingMode =
            stackingModeFromRegistration(findRegistrationGroup(manifest, manifest_group.registrationGroupKey),
                                         manifest_group.stacking.cometStackingMode);

        if (manifest_group.stacking.drizzle.enabled)
        {
            WorkflowManifest::IntegrationGroup::DrizzleSettings drizzle;
            drizzle.enabled = true;
            drizzle.scale = manifest_group.stacking.drizzle.scale;
            drizzle.pixelFraction = manifest_group.stacking.drizzle.pixelFraction;
            drizzle.kernel = manifest_group.stacking.drizzle.kernel;
            drizzle.cfaAware = manifest_group.stacking.drizzle.cfaAware;
            drizzle.cfaPattern = manifest_group.stacking.drizzle.cfaPattern;
            manifest_group.drizzle = drizzle;
        }

        const bool normalization_enabled = manifest_group.stacking.normalizationMode != NormalizationMode::None;

        // Normalization reference selection (manual or best-quality within this stacking group).
        if (normalization_enabled)
        {
            manifest_group.normalizationReferenceFrames =
                selectNormalizationReferenceFrames(m_model, stack_group, manifest_group.stacking);
        }

        if (normalization_enabled)
        {
            const QString artifact_path =
                normalizationArtifactPathFor(manifest.outputDirectory, manifest_group.groupKey);
            if (!artifact_path.isEmpty())
            {
                manifest_group.normalizationCoefficientsArtifactPath = artifact_path;
            }
        }

        if (manifest_group.drizzle.has_value() && !manifest_group.drizzlePixmapArtifactDir.has_value())
        {
            const QString pixmap_dir = drizzlePixmapDirFor(manifest.outputDirectory, manifest_group.groupKey);
            if (!pixmap_dir.isEmpty())
            {
                manifest_group.drizzlePixmapArtifactDir = pixmap_dir;
            }
        }

        if (!m_reprocessExisting && outputExists(manifest.outputDirectory, manifest_group.outputFilename))
        {
            manifest_group.skipIfExists = true;
        }

        manifest.integrationGroups.push_back(std::move(manifest_group));
    }

    {
        CalibrationMatcher matcher(bias_options, dark_options, flat_options, m_model.calibrationMatchingPolicy());
        std::vector<BiasCalibrationGroup> bias_calibration_groups;
        std::vector<DarkCalibrationGroup> dark_calibration_groups;
        std::vector<FlatCalibrationGroup> flat_calibration_groups;

        if (manifest.biasStage.has_value())
        {
            for (const auto& group : manifest.biasStage->groups)
            {
                auto it = std::find_if(bias_groups.begin(), bias_groups.end(),
                                       [&](const GroupedFrameSet& set) { return set.key == group.groupKey; });
                if (it == bias_groups.end() || it->frames.empty())
                {
                    continue;
                }

                BiasCalibrationGroup entry;
                entry.key = BiasGroupKey::fromFrame(it->frames.front());
                entry.groupKey = group.groupKey;
                entry.masterPath = group.existingMaster.has_value()
                                       ? group.existingMaster->canonicalPath()
                                       : QDir(manifest.outputDirectory).filePath(group.outputFilename);
                bias_calibration_groups.push_back(std::move(entry));
            }
        }

        if (manifest.darkStage.has_value())
        {
            for (const auto& group : manifest.darkStage->groups)
            {
                auto it = std::find_if(dark_groups.begin(), dark_groups.end(),
                                       [&](const GroupedFrameSet& set) { return set.key == group.groupKey; });
                if (it == dark_groups.end() || it->frames.empty())
                {
                    continue;
                }

                DarkCalibrationGroup entry;
                entry.key = DarkGroupKey::fromFrame(it->frames.front());
                entry.groupKey = group.groupKey;
                entry.masterPath = group.existingMaster.has_value()
                                       ? group.existingMaster->canonicalPath()
                                       : QDir(manifest.outputDirectory).filePath(group.outputFilename);
                dark_calibration_groups.push_back(std::move(entry));
            }
        }

        if (manifest.flatStage.has_value())
        {
            for (const auto& group : manifest.flatStage->groups)
            {
                auto it = std::find_if(flat_groups.begin(), flat_groups.end(),
                                       [&](const GroupedFrameSet& set) { return set.key == group.groupKey; });
                if (it == flat_groups.end() || it->frames.empty())
                {
                    continue;
                }

                FlatCalibrationGroup entry;
                entry.key = FlatGroupKey::fromFrame(it->frames.front());
                entry.groupKey = group.groupKey;
                entry.masterPath = group.existingMaster.has_value()
                                       ? group.existingMaster->canonicalPath()
                                       : QDir(manifest.outputDirectory).filePath(group.outputFilename);
                flat_calibration_groups.push_back(std::move(entry));
            }
        }

        matcher.setBiasGroups(std::move(bias_calibration_groups));
        matcher.setDarkGroups(std::move(dark_calibration_groups));
        matcher.setFlatGroups(std::move(flat_calibration_groups));
        matcher.setOverrides(m_model.calibrationOverrides());

        GroupingEngine calib_engine;
        auto calib_groups =
            calib_engine.groupFrames(frames_to_process, FrameType::Light, m_model.groupingOptions(FrameType::Light));

        for (const auto& group : calib_groups)
        {
            if (group.frames.empty())
            {
                continue;
            }

            LightCalibrationSettings group_settings;
            if (auto it = light_calib_settings.find(group.key); it != light_calib_settings.end())
            {
                group_settings = it->second;
            }

            // A pre-calibrated group's lights take no calibration entry, as a group no master matched takes none with
            // a master; the group is named, so a report can tell the two apart.
            if (group_settings.preCalibrated)
            {
                WorkflowManifest::PreCalibratedLightGroup pre_calibrated;
                pre_calibrated.groupKey = group.key;
                pre_calibrated.lightFiles.reserve(group.frames.size());
                for (const auto& frame : group.frames)
                {
                    pre_calibrated.lightFiles.push_back(frame.id);
                }
                manifest.preCalibratedLightGroups.push_back(std::move(pre_calibrated));
                continue;
            }

            const LightGroupKey light_key = LightGroupKey::fromFrame(group.frames.front());
            const std::optional<double> light_temp_c = averageTemperatureC(group.frames);
            const CalibrationMatch dark_match = matcher.findDark(light_key, light_temp_c);
            const CalibrationMatch flat_match = matcher.findFlat(light_key);
            CalibrationMatch bias_match;
            if (lightTakesBias(dark_match, m_model.calibrationMatchingPolicy().subtractBiasFromDark))
            {
                bias_match = matcher.findBias(light_key);
            }

            for (const auto& frame : group.frames)
            {
                WorkflowManifest::LightsCalibrationParams params;
                params.lightPath = frame.id;
                if (!bias_match.masterPath.isEmpty())
                {
                    params.biasPath = FrameId(bias_match.masterPath);
                }
                if (!dark_match.masterPath.isEmpty())
                {
                    params.darkPath = FrameId(dark_match.masterPath);
                }
                if (!flat_match.masterPath.isEmpty())
                {
                    params.flatPath = FrameId(flat_match.masterPath);
                }

                params.settings.cosmeticCorrection = group_settings.cosmeticCorrection;
                params.settings.sigmaHot = group_settings.sigmaHot;
                params.settings.sigmaCold = group_settings.sigmaCold;
                params.settings.darkOptimization = group_settings.darkOptimization;
                params.settings.darkMultiplicationFactor = group_settings.darkMultiplicationFactor;
                params.settings.flatAutoLevel = group_settings.flatAutoLevel;
                params.settings.pedestal = group_settings.pedestal;
                params.settings.subtractBiasFromDark = m_model.calibrationMatchingPolicy().subtractBiasFromDark;

                manifest.lightsCalibration.push_back(std::move(params));
            }
        }
    }

    QStringList sequence;
    if (manifest.biasStage.has_value())
    {
        sequence << QStringLiteral("Bias");
    }
    if (manifest.darkStage.has_value())
    {
        sequence << QStringLiteral("Dark");
    }
    if (manifest.flatStage.has_value())
    {
        sequence << QStringLiteral("Flat");
    }
    if (!manifest.lightsCalibration.empty() && !manifest.calibrateLightsInMemory)
    {
        sequence << QStringLiteral("LightsCalibration");
    }
    if (!manifest.registrationGroups.empty())
    {
        sequence << QStringLiteral("LightsRegistration");
    }
    {
        const bool has_non_drizzle = std::any_of(manifest.integrationGroups.begin(), manifest.integrationGroups.end(),
                                                 [](const WorkflowManifest::IntegrationGroup& group) {
                                                     return !group.drizzle.has_value() || !group.drizzle->enabled;
                                                 });
        if (!manifest.integrationGroups.empty() && has_non_drizzle)
        {
            sequence << QStringLiteral("LightsAlignment");
        }
    }
    {
        const bool has_normalization = std::any_of(manifest.integrationGroups.begin(), manifest.integrationGroups.end(),
                                                   [](const WorkflowManifest::IntegrationGroup& group) {
                                                       return !group.normalizationReferenceFrames.empty();
                                                   });
        if (has_normalization)
        {
            sequence << QStringLiteral("LightsNormalization");
        }
    }
    {
        const bool has_drizzle = std::any_of(manifest.integrationGroups.begin(), manifest.integrationGroups.end(),
                                             [](const WorkflowManifest::IntegrationGroup& group) {
                                                 return group.drizzle.has_value() && group.drizzle->enabled;
                                             });
        if (has_drizzle)
        {
            sequence << QStringLiteral("LightsDrizzle");
        }
    }
    if (!manifest.integrationGroups.empty())
    {
        sequence << QStringLiteral("LightsStacking");
    }

    manifest.stageSequence = sequence;
    (void)m_includeRejectionMaps;

    return manifest;
}

std::optional<FrameId> stackingReferenceFrame(const IntegrationDataModel& model, const IntegrationGroupKey& groupKey)
{
    // The group's lights as the manifest builder groups them, less those left out of this stack.
    const auto& lights = model.lightFrames().frames();
    IntegrationGroupEngine engine;
    const auto groups = engine.computeGroups(lights, model.lightIntegrationGroupingOptions());
    const auto found = groups.find(groupKey);
    if (found == groups.end())
    {
        return std::nullopt;
    }
    std::map<FrameId, FrameInfo> byId;
    for (const auto& frame : lights)
    {
        byId.emplace(frame.id, frame);
    }
    GroupedFrameSet group;
    group.key = groupKey.key;
    for (const auto& id : found->second)
    {
        if (const auto frame = byId.find(id); frame != byId.end())
        {
            group.frames.push_back(frame->second);
        }
    }
    const GroupedFrameSet stacked = stackIncludedGroup(model, group);
    if (stacked.frames.empty())
    {
        return std::nullopt;
    }

    const auto& settingsByGroup = model.stackingSettings(FrameType::Light);
    const auto stored = settingsByGroup.find(groupKey.key);
    const StackingSettings settings = stored != settingsByGroup.end() ? stored->second : StackingSettings{};
    if (settings.normalizationMode != NormalizationMode::None)
    {
        const std::vector<FrameId> references = selectNormalizationReferenceFrames(model, stacked, settings, false);
        if (!references.empty())
        {
            return references.front();
        }
    }
    // The stage's own choice when the manifest names none: the first of the group's lights by identity.
    const auto first =
        std::min_element(stacked.frames.begin(), stacked.frames.end(),
                         [](const FrameInfo& left, const FrameInfo& right) { return left.id < right.id; });
    return first->id;
}
