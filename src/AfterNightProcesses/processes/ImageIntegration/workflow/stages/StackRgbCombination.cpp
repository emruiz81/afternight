// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A run's red, green and blue masters combined into one colour master.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/stages/StackRgbCombination.h"

#include "processes/ImageIntegration/core/CalibrationMasterSet.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/WorkflowArtifactStamp.h"
#include "processes/ImageIntegration/workflow/WorkflowStageServices.h"
#include "processes/ImageIntegration/workflow/stages/StackArtifactFingerprints.h"
#include "processes/ImageIntegration/workflow/stages/StackMasterWcs.h"
#include "processes/ImageIntegration/workflow/stages/StackOutputFinalization.h"
#include "processes/core/LinearChannelMatch.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ChannelTransfer.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

#include <array>
#include <cmath>
#include <optional>

namespace ImageIntegration::detail
{
namespace
{

struct RgbChannelMaster
{
    ImageIntegration::RgbIntegrationGroupCandidate candidate;
    IntegrationGroupResult result;
    QString path;
    std::optional<an::Image<float>> image;
};

[[nodiscard]] std::vector<ImageIntegration::RgbIntegrationGroupCandidate>
rgbCombinationCandidatesFromManifest(const WorkflowManifest& manifest)
{
    std::vector<ImageIntegration::RgbIntegrationGroupCandidate> candidates;
    candidates.reserve(manifest.integrationGroups.size());
    for (const auto& group : manifest.integrationGroups)
    {
        candidates.push_back(ImageIntegration::RgbIntegrationGroupCandidate{
            group.groupKey.toString(), group.registrationGroupKey.toString(), group.filter, group.outputFilename,
            static_cast<int>(group.lightFiles.size())});
    }
    return candidates;
}

[[nodiscard]] QHash<QString, IntegrationGroupResult>
successfulIntegrationResultsByGroupKey(const std::vector<IntegrationGroupResult>& results)
{
    QHash<QString, IntegrationGroupResult> by_group_key;
    by_group_key.reserve(static_cast<int>(results.size()));
    for (const auto& result : results)
    {
        if (result.success && !result.groupKey.trimmed().isEmpty())
        {
            by_group_key.insert(result.groupKey, result);
        }
    }
    return by_group_key;
}

[[nodiscard]] QString resolvedStackedOutputPath(const IntegrationGroupResult& result, const QString& output_directory)
{
    const QString result_path = result.outputPath.trimmed();
    if (!result_path.isEmpty())
    {
        return QFileInfo(result_path).isRelative() ? QDir(output_directory).filePath(result_path)
                                                   : QFileInfo(result_path).absoluteFilePath();
    }

    const QString filename = result.outputFilename.trimmed();
    return filename.isEmpty() ? QString() : QDir(output_directory).filePath(filename);
}

[[nodiscard]] std::optional<IntegrationGroupResult>
resolveRgbInputResult(const ImageIntegration::RgbIntegrationGroupCandidate& candidate,
                      const QHash<QString, IntegrationGroupResult>& completed_results,
                      const QSet<QString>& processed_group_keys, const QString& output_directory)
{
    const auto completed_it = completed_results.constFind(candidate.groupKey);
    if (completed_it != completed_results.cend())
    {
        return completed_it.value();
    }
    if (processed_group_keys.contains(candidate.groupKey) || candidate.outputFilename.trimmed().isEmpty())
    {
        return std::nullopt;
    }
    const QString output_path = QDir(output_directory).filePath(candidate.outputFilename);
    if (!QFileInfo::exists(output_path))
    {
        return std::nullopt;
    }
    IntegrationGroupResult result;
    result.groupKey = candidate.groupKey;
    result.outputPath = QFileInfo(output_path).absoluteFilePath();
    result.outputFilename = candidate.outputFilename;
    result.lightFrameCount = candidate.lightFrameCount;
    result.registrationGroupKey = candidate.registrationGroupKey;
    result.success = true;
    return result;
}

[[nodiscard]] an::Result<RgbChannelMaster>
loadRgbChannelMaster(const an::consumer::OperationScope& scope, const an::CodecRegistry* registry,
                     const ImageIntegration::RgbIntegrationGroupCandidate& candidate,
                     const QHash<QString, IntegrationGroupResult>& completed_results,
                     const QSet<QString>& processed_group_keys, const QString& output_directory)
{
    const auto fail = [](const QString& message) {
        return an::Status::failure(an::ErrorCode::InvalidArgument, message.toUtf8().toStdString());
    };
    const auto resolved = resolveRgbInputResult(candidate, completed_results, processed_group_keys, output_directory);
    if (!resolved.has_value())
    {
        return fail(QStringLiteral("No successful mono master was available for filter '%1'.").arg(candidate.filter));
    }
    const QString path = resolvedStackedOutputPath(*resolved, output_directory);
    if (path.trimmed().isEmpty() || !QFileInfo::exists(path))
    {
        return fail(QStringLiteral("Mono master '%1' is missing.").arg(candidate.outputFilename));
    }
    an::Result<an::consumer::AnyImage> read = core::readWorkflowImage(scope, registry, path);
    if (!read.hasValue())
    {
        return fail(QStringLiteral("Mono master '%1' could not be loaded: %2")
                        .arg(QFileInfo(path).fileName(), QString::fromUtf8(read.status().message())));
    }
    an::Result<an::Image<float>> image = core::workingFractions(scope, *read);
    if (!image.hasValue())
    {
        return fail(QStringLiteral("Mono master '%1' could not be loaded: %2")
                        .arg(QFileInfo(path).fileName(), QString::fromUtf8(image.status().message())));
    }
    const std::optional<an::ImageDescriptor> described = image->descriptor();
    if (!described.has_value() || described->extent().channelCount() != 1u)
    {
        return fail(QStringLiteral("Mono master '%1' has %2 channels; expected one.")
                        .arg(QFileInfo(path).fileName())
                        .arg(described.has_value() ? described->extent().channelCount() : 0u));
    }
    return RgbChannelMaster{.candidate = candidate, .result = *resolved, .path = path, .image = std::move(*image)};
}

[[nodiscard]] double rgbReferenceQualityScore(const IntegrationGroupResult& result)
{
    const StackQualityRecord& quality = result.qualityReport;
    if (quality.valid)
    {
        for (const double score : {quality.actualSnrGain, quality.effectiveFrameCount, quality.theoreticalSnrGain,
                                   quality.integrationEfficiency})
        {
            if (std::isfinite(score) && score > 0.0)
            {
                return score;
            }
        }
    }
    return static_cast<double>((std::max)(result.lightFrameCount, 0));
}

void addRgbCombinationError(IntegrationErrorReport& error_report, QStringList& stage_warnings,
                            ProgressReporter& reporter, const QString& registration_group_key, const QString& detail,
                            const QString& output_path = {})
{
    const QString warning =
        QStringLiteral("RGB combination skipped for registration group '%1': %2").arg(registration_group_key, detail);
    appendUniqueWarnings(stage_warnings, QStringList{warning});
    reporter.logger().warning(warning);
    error_report.addError(IntegrationError(IntegrationErrorCode::StackingFailed,
                                           QStringLiteral("RGB master combination failed"), output_path, detail));
}

/// The colour master's cards: the reference master's, its filter replaced by RGB and the three filters named.
[[nodiscard]] an::Result<an::CodecMetadata> rgbCards(const an::CodecMetadata& reference,
                                                     const std::array<RgbChannelMaster, 3>& channels,
                                                     const std::size_t referenceIndex)
{
    an::Result<an::CodecMetadata> cards = withFitsCard(reference, "FILTER", fitsString(QStringLiteral("RGB")));
    const std::array<std::pair<const char*, QString>, 5u> stated{{
        {"RGBCOMB", QStringLiteral("T")},
        {"RGBREF", QString::fromStdString(fitsString(channels[referenceIndex].candidate.filter))},
        {"RGBR", QString::fromStdString(fitsString(channels[0].candidate.filter))},
        {"RGBG", QString::fromStdString(fitsString(channels[1].candidate.filter))},
        {"RGBB", QString::fromStdString(fitsString(channels[2].candidate.filter))},
    }};
    for (const auto& [keyword, value] : stated)
    {
        if (!cards.hasValue())
        {
            return cards;
        }
        cards = withFitsCard(*cards, keyword, value.toStdString());
    }
    return cards;
}

/// The colour master: the reference channel as it is and the other two matched to it, joined into one image.
[[nodiscard]] an::Result<an::Image<float>> combineChannels(const an::consumer::OperationScope& scope,
                                                           const std::array<RgbChannelMaster, 3>& channels,
                                                           const std::size_t referenceIndex)
{
    const an::ExecutionContext& execution = scope.execution();
    const an::Image<float>& reference = *channels[referenceIndex].image;
    const std::optional<an::ImageDescriptor> referenceDescriptor = reference.descriptor();
    const auto referenceView = reference.view();
    if (!referenceDescriptor.has_value() || !referenceView.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the reference master holds nothing");
    }
    const an::ui::processes::LinearMatchPlane referencePlane{
        .samples = referenceView->data(), .layout = &referenceView->layout(), .descriptor = &*referenceDescriptor};

    std::array<std::optional<an::Image<float>>, 3> matched;
    for (std::size_t index = 0; index < channels.size(); ++index)
    {
        if (index == referenceIndex)
        {
            continue;
        }
        const an::Image<float>& component = *channels[index].image;
        const std::optional<an::ImageDescriptor> descriptor = component.descriptor();
        const auto view = component.view();
        if (!descriptor.has_value() || !view.has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "a mono master holds nothing");
        }
        const an::ui::processes::LinearMatchPlane plane{
            .samples = view->data(), .layout = &view->layout(), .descriptor = &*descriptor};
        const an::Result<an::FrameNormalization> match =
            an::ui::processes::measureLinearChannelMatch(execution, referencePlane, plane);
        if (!match.hasValue())
        {
            return match.status();
        }
        an::Result<an::Image<float>> output =
            an::Image<float>::create(scope.engine(), *descriptor, an::StorageArrangement::Planar,
                                     an::StorageInitialization::Uninitialized, component.metadata());
        if (!output.hasValue())
        {
            return output.status();
        }
        {
            std::optional<an::ImageWriteAccess<float>> access = output->write();
            if (!access.has_value())
            {
                return an::Status::failure(an::ErrorCode::NotInitialized, "a matched master holds nothing");
            }
            if (an::Status applied = an::ui::processes::applyLinearChannelMatch(
                    execution, *match, plane, access->view().data(), access->view().layout());
                applied.failed())
            {
                return applied;
            }
        }
        matched[index] = std::move(*output);
    }

    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(
        referenceDescriptor->extent().width(), referenceDescriptor->extent().height(), 3u, scope.engine().limits());
    if (!extent.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the masters' size cannot hold a colour image");
    }
    const std::optional<an::ImageDescriptor> colour = an::ImageDescriptor::create(
        an::ImageDescriptorRequest{.extent = *extent,
                                   .sampleType = an::SampleType::Float32,
                                   .channelSchema = an::ChannelSchema::Rgb,
                                   .colorSpace = an::ColorSpace::Unspecified,
                                   .numericDomain = an::NumericDomain::NormalizedLinear,
                                   .cfa = an::CfaDescriptor{},
                                   .coordinateOrigin = referenceDescriptor->coordinateOrigin(),
                                   .pixelAspectRatio = referenceDescriptor->pixelAspectRatio()});
    if (!colour.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the colour master cannot be described");
    }
    const an::Result<an::CodecMetadata> cards = rgbCards(reference.metadata().codec(), channels, referenceIndex);
    if (!cards.hasValue())
    {
        return cards.status();
    }
    const an::ImageMetadata& referenceMetadata = reference.metadata();
    const an::ImageMetadata metadata =
        an::ImageMetadata::create(referenceMetadata.acquisition(), referenceMetadata.semantics(),
                                  referenceMetadata.astrometry(), referenceMetadata.provenance(), *cards, std::nullopt);
    an::Result<an::Image<float>> rgb = an::Image<float>::create(scope.engine(), *colour, an::StorageArrangement::Planar,
                                                                an::StorageInitialization::Uninitialized, metadata);
    if (!rgb.hasValue())
    {
        return rgb.status();
    }
    std::array<an::ChannelSource, 3> sources{};
    std::array<std::optional<an::ImageView<const float>>, 3> views;
    for (std::size_t index = 0; index < channels.size(); ++index)
    {
        const an::Image<float>& plane = index == referenceIndex ? reference : *matched[index];
        views[index] = plane.view();
        if (!views[index].has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "a channel of the colour master holds nothing");
        }
        sources[index] = an::ChannelSource{.samples = views[index]->data(), .layout = &views[index]->layout()};
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = rgb->write();
        if (!access.has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the colour master holds nothing");
        }
        if (an::Status composed = an::composeImageChannels(execution, an::SampleType::Float32, sources,
                                                           access->view().data(), access->view().layout());
            composed.failed())
        {
            return composed;
        }
    }
    return std::move(*rgb);
}

} // namespace

QString combinedRgbArtifactHash(const QStringList& channelPaths, const int referenceChannel)
{
    QJsonObject object;
    schema::insertArtifactFamilyIdentity(object, schema::ArtifactFamily::Stack);
    QJsonArray channels;
    std::vector<FrameId> frames;
    for (const QString& path : channelPaths)
    {
        channels.append(QFileInfo(path).absoluteFilePath());
        frames.emplace_back(path);
    }
    object.insert(QStringLiteral("rgbChannels"), channels);
    object.insert(QStringLiteral("rgbChannelStamps"), computeFrameFileStampFingerprint(frames));
    object.insert(QStringLiteral("rgbReferenceChannel"), referenceChannel);
    return fingerprintJson(object);
}

bool combineEligibleRgbMasters(const WorkflowManifest& manifest, const WorkflowStageServices& services,
                               CancellationToken& token, ProgressReporter& reporter,
                               std::vector<IntegrationGroupResult>& completed_group_results,
                               const QSet<QString>& processed_group_keys, IntegrationErrorReport& error_report,
                               QStringList& stage_warnings, bool& any_group_succeeded,
                               const StackGroupMonotonicClock& monotonic_clock,
                               WorkflowExecutionCounterAccumulator* counters, ArtifactTracker& tracker,
                               const StackPreviewGenerator& preview_generator)
{
    if (!manifest.combineRgbImages)
    {
        return false;
    }

    const auto rgb_sets =
        ImageIntegration::findEligibleRgbIntegrationGroups(rgbCombinationCandidatesFromManifest(manifest));
    if (rgb_sets.empty())
    {
        reporter.logger().operationDetail(QStringLiteral("RGB combination"),
                                          QStringLiteral("No eligible red/green/blue integration groups were found."));
        return false;
    }

    reporter.logger().operationDetail(QStringLiteral("RGB combination"),
                                      QStringLiteral("Combining %1 eligible RGB registration group%2.")
                                          .arg(static_cast<int>(rgb_sets.size()))
                                          .arg(rgb_sets.size() == 1u ? QStringLiteral("") : QStringLiteral("s")));

    const QHash<QString, IntegrationGroupResult> completed_results =
        successfulIntegrationResultsByGroupKey(completed_group_results);
    completed_group_results.reserve(completed_group_results.size() + rgb_sets.size());
    const auto cancelRequested = [&token]() { return token.isCancellationRequested(); };

    for (const auto& set : rgb_sets)
    {
        if (token.isCancellationRequested())
        {
            return true;
        }

        an::Result<an::consumer::OperationScope> scope = services.admitStageOperation();
        if (!scope.hasValue())
        {
            if (scope.status().code() == an::ErrorCode::Cancelled)
            {
                return true;
            }
            addRgbCombinationError(error_report, stage_warnings, reporter, set.registrationGroupKey,
                                   QString::fromUtf8(scope.status().message()));
            continue;
        }
        const an::CodecRegistry* registry = services.codecRegistry().get();

        std::array<std::optional<RgbChannelMaster>, 3> loaded;
        bool loadFailed = false;
        const std::array<const ImageIntegration::RgbIntegrationGroupCandidate*, 3> candidates{&set.red, &set.green,
                                                                                              &set.blue};
        for (std::size_t index = 0; index < candidates.size() && !loadFailed; ++index)
        {
            an::Result<RgbChannelMaster> master =
                loadRgbChannelMaster(*scope, registry, *candidates[index], completed_results, processed_group_keys,
                                     manifest.outputDirectory);
            if (!master.hasValue())
            {
                addRgbCombinationError(error_report, stage_warnings, reporter, set.registrationGroupKey,
                                       QString::fromUtf8(master.status().message()));
                loadFailed = true;
                continue;
            }
            loaded[index] = std::move(*master);
        }
        if (loadFailed)
        {
            continue;
        }
        std::array<RgbChannelMaster, 3> channels{std::move(*loaded[0]), std::move(*loaded[1]), std::move(*loaded[2])};
        const auto extentOf = [](const RgbChannelMaster& channel) { return channel.image->descriptor()->extent(); };
        if (extentOf(channels[1]).width() != extentOf(channels[0]).width() ||
            extentOf(channels[1]).height() != extentOf(channels[0]).height() ||
            extentOf(channels[2]).width() != extentOf(channels[0]).width() ||
            extentOf(channels[2]).height() != extentOf(channels[0]).height())
        {
            addRgbCombinationError(error_report, stage_warnings, reporter, set.registrationGroupKey,
                                   QStringLiteral("RGB mono masters have incompatible dimensions."));
            continue;
        }

        std::size_t referenceIndex = 0u;
        double bestScore = rgbReferenceQualityScore(channels[0].result);
        for (std::size_t index = 1u; index < channels.size(); ++index)
        {
            const double score = rgbReferenceQualityScore(channels[index].result);
            if (score > bestScore)
            {
                bestScore = score;
                referenceIndex = index;
            }
        }
        reporter.logger().operationDetail(
            QStringLiteral("RGB linear fit"),
            QStringLiteral("Using '%1' as RGB reference for registration group '%2'.")
                .arg(channels[referenceIndex].candidate.filter, set.registrationGroupKey));

        const QString output_filename = ImageIntegration::combinedRgbOutputFilename(set.registrationGroupKey);
        QString output_dir_path = manifest.outputDirectory;
        for (const auto& channel : channels)
        {
            const QString directory = QFileInfo(channel.path).absolutePath();
            if (!directory.trimmed().isEmpty())
            {
                output_dir_path = directory;
                break;
            }
        }
        const QString output_path = QDir(output_dir_path).filePath(output_filename);
        const QString hash = combinedRgbArtifactHash(QStringList{channels[0].path, channels[1].path, channels[2].path},
                                                     static_cast<int>(referenceIndex));

        StackGroupTimingRecorder rgb_timing(monotonic_clock);
        IntegrationGroupResult rgb_result;
        rgb_result.groupKey = ImageIntegration::combinedRgbGroupKey(set.registrationGroupKey);
        rgb_result.outputPath = QFileInfo(output_path).absoluteFilePath();
        rgb_result.outputFilename = output_filename;
        rgb_result.registrationGroupKey = set.registrationGroupKey;
        rgb_result.lightFrameCount = (std::max)(0, channels[0].result.lightFrameCount) +
                                     (std::max)(0, channels[1].result.lightFrameCount) +
                                     (std::max)(0, channels[2].result.lightFrameCount);
        const auto publish_incomplete_result = [&]() {
            clearPreparedStackPreviewAssets(rgb_result);
            rgb_result.success = false;
            rgb_result.outputPath.clear();
            rgb_result.outputBytes = 0;
            rgb_result.committedArtifact.reset();
            rgb_result.stagedFiles.clear();
            rgb_result.durationMs = rgb_timing.finalizationTiming().captured ? rgb_timing.capturedGroupDurationMs()
                                                                             : rgb_timing.elapsedNowMs();
            rgb_result.finalizationTiming = rgb_timing.finalizationTiming();
            completed_group_results.push_back(std::move(rgb_result));
        };

        an::Result<an::Image<float>> rgb = combineChannels(*scope, channels, referenceIndex);
        if (!rgb.hasValue())
        {
            if (rgb.status().code() == an::ErrorCode::Cancelled)
            {
                return true;
            }
            addRgbCombinationError(error_report, stage_warnings, reporter, set.registrationGroupKey,
                                   QString::fromUtf8(rgb.status().message()), output_path);
            publish_incomplete_result();
            continue;
        }

        rgb_timing.beginFinalization();
        an::Result<LocalOutputTransaction> transaction = stageOutputsForOverwrite(output_path, counters);
        if (!transaction.hasValue())
        {
            rgb_timing.capturePhase(StackFinalizationPhase::FinalMasterSave, QStringLiteral("failed"));
            addRgbCombinationError(error_report, stage_warnings, reporter, set.registrationGroupKey,
                                   QString::fromUtf8(transaction.status().message()), output_path);
            publish_incomplete_result();
            continue;
        }
        for (const auto& staged_file : transaction->staged)
        {
            rgb_result.stagedFiles.push_back({staged_file.originalPath, staged_file.stagedPath});
        }
        const an::Result<an::ArtifactIdentity> identity = workflowArtifactIdentity(hash, manifest.executionId);
        const an::Status published =
            identity.hasValue() ? publishWorkflowImage(*scope, *rgb, output_path, *identity) : identity.status();
        if (published.failed())
        {
            rgb_timing.capturePhase(StackFinalizationPhase::FinalMasterSave, QStringLiteral("failed"));
            restoreLocalStagedFiles(*transaction);
            if (published.code() == an::ErrorCode::Cancelled)
            {
                publish_incomplete_result();
                return true;
            }
            addRgbCombinationError(error_report, stage_warnings, reporter, set.registrationGroupKey,
                                   QString::fromUtf8(published.message()), output_path);
            publish_incomplete_result();
            continue;
        }
        rgb_timing.capturePhase(StackFinalizationPhase::FinalMasterSave, QStringLiteral("completed"));

        if (token.isCancellationRequested())
        {
            rgb_timing.capturePhase(StackFinalizationPhase::PreviewGeneration, QStringLiteral("cancelled"));
            restoreLocalStagedFiles(*transaction);
            publish_incomplete_result();
            return true;
        }
        prepareResidentStackPreviewAssets(preview_generator, *scope, *rgb, output_path, rgb_result);
        rgb_timing.capturePhase(StackFinalizationPhase::PreviewGeneration, QStringLiteral("completed"));

        // Reading the stamp the record checks is an operation of its own, admitted once this one lets go.
        scope->end();
        const auto committed = FinalCommittedArtifact::fromFile(output_path, counters, cancelRequested);
        if (!committed.has_value() || !tracker.recordCurrentRunCommittedArtifact(*committed, hash, cancelRequested))
        {
            rgb_timing.capturePhase(StackFinalizationPhase::DigestArtifactRecording, QStringLiteral("failed"));
            restoreLocalStagedFiles(*transaction);
            if (token.isCancellationRequested())
            {
                publish_incomplete_result();
                return true;
            }
            addRgbCombinationError(error_report, stage_warnings, reporter, set.registrationGroupKey,
                                   QStringLiteral("Failed to record the combined RGB master."), output_path);
            publish_incomplete_result();
            continue;
        }
        rgb_timing.capturePhase(StackFinalizationPhase::DigestArtifactRecording, QStringLiteral("completed"));

        rgb_result.outputBytes = committed->sizeBytes();
        rgb_result.committedArtifact = *committed;
        rgb_result.success = true;
        any_group_succeeded = true;
        reporter.logger().artifactCreated(QStringLiteral("Combined RGB master"), output_path);
        rgb_timing.capturePhase(StackFinalizationPhase::CompletionPublication, QStringLiteral("completed"));
        rgb_result.durationMs = rgb_timing.capturedGroupDurationMs();
        rgb_result.finalizationTiming = rgb_timing.finalizationTiming();
        completed_group_results.push_back(std::move(rgb_result));
    }

    return false;
}

} // namespace ImageIntegration::detail
