// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The engine-free helpers the workflow's stages share.

#include "stdafx.hpp"
#include "StageUtilities.h"

#include "processes/ImageIntegration/core/CometStackingMode.h"
#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/core/NormalizationOptionParsing.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"

#include <AfterNightPlatform/Logging.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLocale>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

namespace
{
bool isDisabledToken(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    return normalized.isEmpty() || normalized == QStringLiteral("none") || normalized == QStringLiteral("off") ||
           normalized == QStringLiteral("disabled");
}

QString trimOrDefault(const QString& value, const QString& fallback)
{
    const QString trimmed = value.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

QString formatNumberCompact(double value, int precision = 2)
{
    QString text = QLocale::c().toString(value, 'f', precision);
    while (text.contains('.') && (text.endsWith('0') || text.endsWith('.')))
    {
        text.chop(1);
    }
    return text;
}

QString globalNormalizationMethodToString(GlobalNormalizationMethod method)
{
    switch (method)
    {
    case GlobalNormalizationMethod::Additive:
        return QStringLiteral("Additive");
    case GlobalNormalizationMethod::Multiplicative:
        return QStringLiteral("Multiplicative");
    case GlobalNormalizationMethod::AdditiveScaling:
        return QStringLiteral("Additive With Scaling");
    case GlobalNormalizationMethod::MultiplicativeScaling:
        return QStringLiteral("Multiplicative With Scaling");
    }

    return QStringLiteral("Additive With Scaling");
}

QString scaleEstimatorToString(ScaleEstimatorMethod method)
{
    switch (method)
    {
    case ScaleEstimatorMethod::MAD:
        return QStringLiteral("MAD");
    case ScaleEstimatorMethod::IKSS:
        return QStringLiteral("IKSS");
    }

    return QStringLiteral("IKSS");
}

QString resolvedRejectionMethodToString(const StackingSettings& settings)
{
    const QString effective = trimOrDefault(settings.rejectionMethod, QStringLiteral("None"));
    const QString requested = settings.requestedRejectionMethod.trimmed();
    if (!requested.isEmpty() && requested.compare(effective, Qt::CaseInsensitive) != 0)
    {
        if (requested.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0)
        {
            return QStringLiteral("Auto (%1)").arg(effective);
        }
        return QStringLiteral("%1 -> %2").arg(requested, effective);
    }
    return effective;
}
} // namespace

namespace stage_utils
{
QString normalizationArtifactPath(const WorkflowManifest& manifest, const WorkflowManifest::IntegrationGroup& group)
{
    const QString configured = group.normalizationCoefficientsArtifactPath.value_or(QString()).trimmed();
    if (!configured.isEmpty())
    {
        return configured;
    }
    if (manifest.outputDirectory.trimmed().isEmpty())
    {
        return QString();
    }
    const QString group_dir_name = ImageIntegration::sanitizeFilename(group.groupKey.toString());
    return QDir(manifest.outputDirectory)
        .filePath(QStringLiteral(".afternight/image_integration/artifacts/normalization/%1/coefficients.json")
                      .arg(group_dir_name));
}

bool isWorkflowOwnedOutputPath(const WorkflowManifest& manifest, const FrameId& frame)
{
    const QString path = QDir::cleanPath(QDir::fromNativeSeparators(frame.canonicalPath()));
    const QString output_root =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(manifest.outputDirectory).absoluteFilePath()));
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity path_case = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity path_case = Qt::CaseSensitive;
#endif
    const auto is_under_output_subdirectory = [&](const QString& directory_name) {
        const QString directory =
            QDir::cleanPath(QDir::fromNativeSeparators(QDir(output_root).filePath(directory_name)));
        return path.compare(directory, path_case) == 0 || path.startsWith(directory + QLatin1Char('/'), path_case);
    };
    if (is_under_output_subdirectory(QStringLiteral("registered_lights")) ||
        is_under_output_subdirectory(QStringLiteral("calibrated_lights")) ||
        is_under_output_subdirectory(QStringLiteral("ca_corrected_lights")))
    {
        return true;
    }

    const auto calibration_stage_contains = [&](const std::optional<WorkflowManifest::CalibrationStage>& stage) {
        return stage.has_value() && std::any_of(stage->groups.cbegin(), stage->groups.cend(), [&](const auto& group) {
                   const QString output = QDir::cleanPath(QDir(output_root).filePath(group.outputFilename));
                   return path.compare(output, path_case) == 0;
               });
    };
    return calibration_stage_contains(manifest.biasStage) || calibration_stage_contains(manifest.darkStage) ||
           calibration_stage_contains(manifest.flatStage);
}

void accumulateStackInputPreflightProfile(CalibrationStackProfile& total, const StackInputPreflightProfile& group)
{
    total.preflight.probe_nanoseconds += group.probe_nanoseconds;
    total.preflight.fallback_nanoseconds += group.fallback_nanoseconds;
    total.preflight.total_nanoseconds += group.total_nanoseconds;
    total.preflight.decoded_sample_bytes += group.decoded_sample_bytes;
}

QString formatCalibrationStackProfile(const CalibrationStackProfile& profile)
{
    if (profile.frames_stacked == 0 || profile.loop_nanoseconds == 0)
    {
        return {};
    }

    const auto share = [&](const std::uint64_t nanoseconds) {
        return 100.0 * static_cast<double>(nanoseconds) / static_cast<double>(profile.loop_nanoseconds);
    };

    QStringList parts;
    const auto append = [&](const char* name, const std::uint64_t nanoseconds) {
        if (nanoseconds > 0)
        {
            parts << QStringLiteral("%1 %2%").arg(QLatin1String(name)).arg(share(nanoseconds), 0, 'f', 1);
        }
    };

    // The preflight's own total rather than the sum of its parts: it also validates file revisions and
    // estimates memory, and folding that into whichever sub-phase happens to be adjacent would
    // misattribute it. The difference between the two shows up in `other` where it belongs.
    append("probe", profile.preflight.probe_nanoseconds);
    append("fallback", profile.preflight.fallback_nanoseconds);
    append("stack", profile.stack_nanoseconds);
    append("write", profile.write_nanoseconds);

    const std::uint64_t accounted = profile.preflight.probe_nanoseconds + profile.preflight.fallback_nanoseconds +
                                    profile.stack_nanoseconds + profile.write_nanoseconds;
    parts << QStringLiteral("other %1%")
                 .arg(profile.loop_nanoseconds > accounted ? share(profile.loop_nanoseconds - accounted) : 0.0, 0, 'f',
                      1);

    const double per_frame_ms =
        static_cast<double>(profile.loop_nanoseconds) / 1.0e6 / static_cast<double>(profile.frames_stacked);
    return QStringLiteral("%1 ms/frame | %2").arg(per_frame_ms, 0, 'f', 0).arg(parts.join(QStringLiteral(", ")));
}

QString formatCalibrationStackThroughput(const CalibrationStackProfile& profile)
{
    if (profile.frames_stacked == 0 || profile.loop_nanoseconds == 0)
    {
        return {};
    }

    constexpr double kMegabyte = 1024.0 * 1024.0;
    const double seconds = static_cast<double>(profile.loop_nanoseconds) / 1.0e9;
    const double decoded_mb = static_cast<double>(profile.preflight.decoded_sample_bytes) / kMegabyte;

    QString throughput = QStringLiteral("%1 frames").arg(profile.frames_stacked);
    if (decoded_mb > 0.0)
    {
        // Non-zero only when a codec could not validate a payload without decoding it, which no FITS
        // input in this workflow triggers. Left in because a project of TIFFs would, and would then
        // want to know that this stage decoded them twice.
        throughput += QStringLiteral(", %1 MB decoded in preflight").arg(decoded_mb, 0, 'f', 0);
    }
    if (seconds > 0.0)
    {
        throughput += QStringLiteral(", %1 s of stage wall clock").arg(seconds, 0, 'f', 1);
    }
    return throughput + QStringLiteral(", %1 hardware threads").arg(std::thread::hardware_concurrency());
}

void reportStageProgress(ProgressReporter& reporter, WorkflowStage stage, int group_index, int total_groups,
                         float stage_progress, const QString& operation, const QString& current_file)
{
    WorkflowProgress progress;
    progress.currentStage = stage;
    progress.currentGroup = group_index;
    progress.totalGroups = total_groups;
    progress.stageProgress = std::clamp(stage_progress, 0.0f, 1.0f);
    if (total_groups > 0)
    {
        progress.overallProgress = std::clamp((static_cast<float>(group_index - 1) + progress.stageProgress) /
                                                  static_cast<float>(total_groups),
                                              0.0f, 1.0f);
    }
    else
    {
        progress.overallProgress = progress.stageProgress;
    }
    progress.currentOperation = operation;
    progress.currentFile = current_file;
    reporter.report(progress);
}

void reportStageProgress(ProgressReporter& reporter, WorkflowStage stage, int group_index, int total_groups,
                         float stage_progress, const QString& operation, const QString& current_file,
                         int current_sub_step, int total_sub_steps, const QString& sub_step_name)
{
    WorkflowProgress progress;
    progress.currentStage = stage;
    progress.currentGroup = group_index;
    progress.totalGroups = total_groups;
    progress.currentSubStep = current_sub_step;
    progress.totalSubSteps = total_sub_steps;
    progress.subStepName = sub_step_name;

    const float clamped = std::clamp(stage_progress, 0.0f, 1.0f);

    // Sub-step-aware progress: spread each sub-step's 0…1 range across
    // the fraction allocated to this group.
    if (total_sub_steps > 0 && current_sub_step > 0)
    {
        progress.stageProgress = std::clamp(
            (static_cast<float>(current_sub_step - 1) + clamped) / static_cast<float>(total_sub_steps), 0.0f, 1.0f);
    }
    else
    {
        progress.stageProgress = clamped;
    }

    if (total_groups > 0)
    {
        progress.overallProgress = std::clamp((static_cast<float>(group_index - 1) + progress.stageProgress) /
                                                  static_cast<float>(total_groups),
                                              0.0f, 1.0f);
    }
    else
    {
        progress.overallProgress = progress.stageProgress;
    }

    progress.currentOperation = operation;
    progress.currentFile = current_file;
    reporter.report(progress);
}

void logBatchProgress(const QString& operation, int current, int total, int batchSize)
{
    if (current == 1 || current == total || current % batchSize == 0)
    {
        const double percent = (total > 0) ? (static_cast<double>(current) * 100.0 / total) : 0.0;
        LOG_TRACE("{} {}/{} ({:.0f}%)", operation.toStdString(), current, total, percent);
    }
}

void sayStageWarnings(ProgressReporter& reporter, const QStringList& said, QStringList& warnings)
{
    for (const QString& warning : said)
    {
        LOG_WARNING("{}", warning.toStdString());
        reporter.logger().warning(warning);
        if (!warnings.contains(warning))
        {
            warnings.append(warning);
        }
    }
}

QString describeNormalizationSettings(const StackingSettings& settings, const bool drizzled)
{
    switch (settings.normalizationMode)
    {
    case NormalizationMode::None:
        return QStringLiteral("None");
    case NormalizationMode::Global: {
        QString base =
            QStringLiteral("Global (%1)").arg(globalNormalizationMethodToString(settings.globalNormalizationMethod));
        if (ImageIntegration::normalizationUsesScaleEstimator(settings.globalNormalizationMethod))
        {
            base = QStringLiteral("Global (%1, %2)")
                       .arg(globalNormalizationMethodToString(settings.globalNormalizationMethod),
                            scaleEstimatorToString(settings.scaleEstimator));
        }
        if (settings.overlapNormalization.enabled)
        {
            return base + QStringLiteral(" [overlap]");
        }
        return base;
    }
    case NormalizationMode::Local: {
        if (!settings.localNormalization.enabled)
        {
            return QStringLiteral("Local (disabled)");
        }

        // Local normalization matches each light globally in Additive With Scaling, whatever method the group stores.
        const QString match = QStringLiteral("Additive With Scaling, set by local normalization, %1")
                                  .arg(scaleEstimatorToString(settings.scaleEstimator));
        if (drizzled)
        {
            // A drizzled group's stack fits the surfaces itself, against the group's reference.
            return QStringLiteral("Local + Global (%1; fitted by the drizzled stack)").arg(match);
        }
        const QString strategy =
            trimOrDefault(settings.localNormalization.referenceStrategy, QStringLiteral("Automatic"));
        return QStringLiteral("Local + Global (%1; ref: %2)").arg(match, strategy);
    }
    }

    return QStringLiteral("None");
}

QString describeReferenceSelection(const StackingSettings& settings, const std::vector<FrameId>& referenceFrames)
{
    const auto referenceSource = [&]() {
        if (settings.normalizationReferenceMode == NormalizationReferenceMode::Manual &&
            !settings.manualNormalizationReference.trimmed().isEmpty())
        {
            // Manual only when the light used is the one chosen by hand.
            const FrameId manual(settings.manualNormalizationReference);
            if (!referenceFrames.empty() && referenceFrames.front().identityKey() == manual.identityKey())
            {
                return QStringLiteral("Manual");
            }
            return QStringLiteral("The manual reference %1 is not used")
                .arg(QFileInfo(manual.canonicalPath()).fileName());
        }

        if (settings.normalizationMode == NormalizationMode::Local && settings.localNormalization.enabled)
        {
            const QString strategy =
                trimOrDefault(settings.localNormalization.referenceStrategy, QStringLiteral("Automatic"));
            if (ImageIntegration::isIntegrateBestFramesLocalNormReferenceStrategy(strategy))
            {
                return QStringLiteral("Automatic best frames");
            }
            if (ImageIntegration::isExplicitLocalNormReferenceStrategy(strategy))
            {
                return QStringLiteral("Automatic explicit-reference fallback");
            }
            return QStringLiteral("Automatic best frame");
        }

        return QStringLiteral("Automatic best quality");
    };

    QStringList refs;
    refs.reserve(static_cast<int>(referenceFrames.size()));
    for (const auto& frame : referenceFrames)
    {
        const QString path = frame.canonicalPath();
        const QString name = QFileInfo(path).fileName();
        refs.append(name.isEmpty() ? path : QStringLiteral("%1 [%2]").arg(name, path));
    }

    const QString primary = refs.isEmpty() ? QStringLiteral("(none)") : refs.front();
    const QString allRefs = refs.isEmpty() ? QStringLiteral("(none)") : refs.join(QStringLiteral("; "));

    return QStringLiteral("%1 | primary: %2 | reference count: %3 | references: %4")
        .arg(referenceSource(), primary, QString::number(static_cast<int>(referenceFrames.size())), allRefs);
}

QString describeStackingSettings(const StackingSettings& settings, const bool drizzled)
{
    const QString stack_method = trimOrDefault(settings.stackMethod, QStringLiteral("Average"));
    const QString rejection_method = resolvedRejectionMethodToString(settings);
    const QString weighting = isDisabledToken(settings.weighting)
                                  ? QStringLiteral("None")
                                  : trimOrDefault(settings.weighting, QStringLiteral("None"));
    const QString local_weighting = settings.localWeighting.enabled ? QStringLiteral("On (%1px / %2px)")
                                                                          .arg(settings.localWeighting.windowSize)
                                                                          .arg(settings.localWeighting.gridSpacing)
                                                                    : QStringLiteral("Off");

    QString stack_summary = settings.hdr.enabled ? QStringLiteral("HDR Merge (%1)").arg(stack_method) : stack_method;
    const bool has_rejection_provenance = !settings.requestedRejectionMethod.trimmed().isEmpty();
    if (stack_method.compare(QStringLiteral("Average"), Qt::CaseInsensitive) == 0 && !settings.hdr.enabled &&
        (!isDisabledToken(settings.rejectionMethod) || has_rejection_provenance))
    {
        stack_summary = QStringLiteral("Average + %1").arg(rejection_method);
    }

    QStringList summary_parts = {
        QStringLiteral("Stack: %1").arg(stack_summary), QStringLiteral("Weighting: %1").arg(weighting),
        QStringLiteral("Spatial: %1").arg(local_weighting),
        QStringLiteral("Normalization: %1").arg(describeNormalizationSettings(settings, drizzled))};

    const QString comet_mode = ImageIntegration::normalizeCometStackingMode(settings.cometStackingMode);
    if (ImageIntegration::usesCometStackingMode(comet_mode))
    {
        summary_parts << QStringLiteral("Output: %1").arg(comet_mode);
    }

    return summary_parts.join(QStringLiteral(" | "));
}

QString describeDrizzleSettings(const WorkflowManifest::IntegrationGroup::DrizzleSettings& drizzle)
{
    const QString kernel = trimOrDefault(drizzle.kernel, QStringLiteral("square"));
    QString cfa = QStringLiteral("off");
    if (drizzle.cfaAware)
    {
        cfa = QStringLiteral("on");
        const QString pattern = drizzle.cfaPattern.trimmed();
        if (!pattern.isEmpty())
        {
            cfa += QStringLiteral(" (%1)").arg(pattern);
        }
    }

    return QStringLiteral("Scale %1, PixFrac %2, Kernel %3, CFA %4")
        .arg(formatNumberCompact(static_cast<double>(drizzle.scale)),
             formatNumberCompact(static_cast<double>(drizzle.pixelFraction)), kernel, cfa);
}

QString fingerprintRegistrationOutputWcs(const QString& output_wcs_json)
{
    return QString::fromLatin1(QCryptographicHash::hash(output_wcs_json.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool buildDrizzleLocalNormalizationReferenceIndices(const std::vector<size_t>& input_reference_indices,
                                                    const std::vector<FrameId>& registration_inputs,
                                                    std::vector<size_t>& out_indices, QString& reason)
{
    out_indices.clear();
    if (input_reference_indices.empty())
    {
        return true;
    }

    std::vector<std::string> sorted_paths;
    sorted_paths.reserve(registration_inputs.size());
    for (const auto& input : registration_inputs)
    {
        sorted_paths.push_back(input.canonicalPath().toStdString());
    }
    std::sort(sorted_paths.begin(), sorted_paths.end());
    if (std::adjacent_find(sorted_paths.begin(), sorted_paths.end()) != sorted_paths.end())
    {
        reason = QStringLiteral("Duplicate registration input path for drizzle local normalization.");
        return false;
    }

    out_indices.reserve(input_reference_indices.size());
    for (const size_t input_index : input_reference_indices)
    {
        if (input_index >= registration_inputs.size())
        {
            reason = QStringLiteral("Normalization reference index is outside the drizzle input set.");
            return false;
        }

        const std::string path = registration_inputs[input_index].canonicalPath().toStdString();
        const auto it = std::lower_bound(sorted_paths.begin(), sorted_paths.end(), path);
        if (it == sorted_paths.end() || *it != path)
        {
            reason = QStringLiteral("Normalization reference is absent from the drizzle input set.");
            return false;
        }
        out_indices.push_back(static_cast<size_t>(std::distance(sorted_paths.begin(), it)));
    }

    std::sort(out_indices.begin(), out_indices.end());
    out_indices.erase(std::unique(out_indices.begin(), out_indices.end()), out_indices.end());
    return true;
}
} // namespace stage_utils
