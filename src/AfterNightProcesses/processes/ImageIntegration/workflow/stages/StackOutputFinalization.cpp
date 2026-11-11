// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The lights stacking stage's finalization: previous outputs staged for the archive, maps as images, previews.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/stages/StackOutputFinalization.h"

#include "processes/ImageIntegration/workflow/PreviewThumbnailGenerator.h"
#include "processes/ImageIntegration/workflow/WorkflowFileOperations.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightPlatform/Logging.h>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <utility>

namespace ImageIntegration::detail
{
namespace
{

constexpr QSize kStackUiPreviewMaximumSize{256, 256};
constexpr std::uint32_t kStackReportPreviewMaximumSide = 2048u;
constexpr int kStackReportPreviewJpegQuality = 90;

[[nodiscard]] an::Status refusal(const an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// A map's description: the stack's extent and channels as Float32 samples with no meaning beyond their numbers.
[[nodiscard]] std::optional<an::ImageDescriptor> mapDescriptor(const an::ImageDescriptor& stacked)
{
    an::ImageDescriptorRequest request = stacked.toRequest();
    request.sampleType = an::SampleType::Float32;
    request.numericDomain = an::NumericDomain::NormalizedLinear;
    return an::ImageDescriptor::create(request);
}

/// A map of counts as Float32, each divided by `divisor` and held at `ceiling` when one is given.
[[nodiscard]] an::Result<an::Image<float>> countMap(const an::EngineContext& engine, an::ImageStorageBytes&& counts,
                                                    const an::ImageDescriptor& described,
                                                    const an::ImageMetadata& metadata, const double divisor,
                                                    const std::optional<double> ceiling)
{
    an::Result<an::ImageStorage<std::uint32_t>> storage = an::ImageStorage<std::uint32_t>::fromBytes(std::move(counts));
    if (!storage.hasValue())
    {
        return storage.status();
    }
    const std::optional<an::ImageView<const std::uint32_t>> source = std::as_const(*storage).view();
    if (!source.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "a map holds nothing");
    }
    an::Result<an::Image<float>> map = an::Image<float>::create(engine, described, an::StorageArrangement::Planar,
                                                                an::StorageInitialization::Uninitialized, metadata);
    if (!map.hasValue())
    {
        return map.status();
    }
    std::optional<an::ImageWriteAccess<float>> access = map->write();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "a map holds nothing");
    }
    for (std::uint32_t plane = 0u; plane < source->planeCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < source->height(); ++y)
        {
            const std::optional<std::span<const std::uint32_t>> in = source->row(plane, y);
            const std::optional<std::span<float>> out = access->view().row(plane, y);
            if (!in.has_value() || !out.has_value())
            {
                return refusal(an::ErrorCode::Internal, "a map's row is missing");
            }
            for (std::size_t x = 0u; x < out->size(); ++x)
            {
                double value = static_cast<double>((*in)[x]) / divisor;
                if (ceiling.has_value())
                {
                    value = (std::min)(value, *ceiling);
                }
                (*out)[x] = static_cast<float>(value);
            }
        }
    }
    return std::move(*map);
}

/// A map of Float32 values as an image.
[[nodiscard]] an::Result<an::Image<float>>
valueMap(an::ImageStorageBytes&& values, const an::ImageDescriptor& described, const an::ImageMetadata& metadata)
{
    an::Result<an::ImageStorage<float>> storage = an::ImageStorage<float>::fromBytes(std::move(values));
    if (!storage.hasValue())
    {
        return storage.status();
    }
    return an::Image<float>::fromStorage(std::move(*storage), described, metadata);
}

/// A master's two pictures: the whole frame, and the detail when one could be made.
struct PresentedMaster
{
    QImage whole;
    QImage detail;
};

/// The part of the master its report shows at full scale - the region `reportDetailRegion` names - presented through
/// the analysis the whole frame was presented through, so the two pictures share one stretch.
[[nodiscard]] QImage presentedDetail(const an::consumer::OperationScope& scope, const an::ImageDescriptor& descriptor,
                                     const an::ImageView<const float>& view,
                                     const an::DisplayPresentationRequest& request,
                                     const an::DisplayPresentationAnalysis& analysis)
{
    const std::uint32_t width = descriptor.extent().width();
    const std::uint32_t height = descriptor.extent().height();
    constexpr auto kLargest = static_cast<std::uint32_t>((std::numeric_limits<int>::max)());
    if (width > kLargest || height > kLargest)
    {
        return {};
    }
    const QRect region = ImageIntegration::reportDetailRegion(QSize(static_cast<int>(width), static_cast<int>(height)));
    if (region.isEmpty())
    {
        return {};
    }
    // The region shares the master's memory: the view is kept for as long as its layout is read.
    const std::optional<an::ImageView<const float>> part =
        view.subview(static_cast<std::uint32_t>(region.x()), static_cast<std::uint32_t>(region.y()),
                     static_cast<std::uint32_t>(region.width()), static_cast<std::uint32_t>(region.height()));
    if (!part.has_value())
    {
        return {};
    }
    an::ImageDescriptorRequest described = descriptor.toRequest();
    described.extent = part->extent();
    const std::optional<an::ImageDescriptor> partDescriptor = an::ImageDescriptor::create(described);
    if (!partDescriptor.has_value())
    {
        return {};
    }
    const an::Result<an::RealtimePreviewDisplayPresentationPlan> plan =
        an::planRealtimePreviewDisplayPresentation(request, 1u);
    if (!plan.hasValue())
    {
        return {};
    }
    const an::Result<an::ImageLayoutRequest> layoutRequest =
        an::realtimePreviewDisplayPresentationLayout(*plan, *partDescriptor);
    if (!layoutRequest.hasValue())
    {
        return {};
    }
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*layoutRequest, scope.engine().limits());
    if (!layout.has_value())
    {
        return {};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(layout->totalBytes()));
    const an::DisplayPresentationSource source{.descriptor = &*partDescriptor,
                                               .samples = part->data(),
                                               .layout = &part->layout(),
                                               .validity = an::KernelValidityView{}};
    const an::Result<an::DisplayPresentationResult> presented =
        an::presentImageRealtimePreview(scope.execution(), *plan, source, bytes.data(), *layout, &analysis);
    if (!presented.hasValue())
    {
        LOG_WARNING("LightsStackingStage: the master's detail was not presented: {}", presented.status().message());
        return {};
    }
    const QImage shared(reinterpret_cast<const uchar*>(bytes.data()), static_cast<int>(presented->width),
                        static_cast<int>(presented->height), static_cast<qsizetype>(layout->rowStrideBytes()),
                        QImage::Format_RGBA8888);
    return shared.copy();
}

/// The master presented for display: the whole frame reduced so its longer side is at most the report's bound, and
/// its detail.
[[nodiscard]] std::optional<PresentedMaster> presentedPreview(const an::consumer::OperationScope& scope,
                                                              const an::Image<float>& master)
{
    const std::optional<an::ImageDescriptor> descriptor = master.descriptor();
    const auto view = master.view();
    if (!descriptor.has_value() || !view.has_value())
    {
        return std::nullopt;
    }
    const std::uint32_t longest = (std::max)(descriptor->extent().width(), descriptor->extent().height());
    const std::uint32_t subsample =
        (std::max)(1u, (longest + kStackReportPreviewMaximumSide - 1u) / kStackReportPreviewMaximumSide);
    // Each colour channel takes its own stretch, as the run history's and the report's other previews do.
    an::DisplayPresentationRequest request;
    request.stretch = an::PresentationStretch::Automatic;
    request.linked = false;
    request.output = an::PresentationOutput::Rgba8;
    const an::Result<an::RealtimePreviewDisplayPresentationPlan> plan =
        an::planRealtimePreviewDisplayPresentation(request, subsample);
    if (!plan.hasValue())
    {
        return std::nullopt;
    }
    const an::Result<an::ImageLayoutRequest> layoutRequest =
        an::realtimePreviewDisplayPresentationLayout(*plan, *descriptor);
    if (!layoutRequest.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*layoutRequest, scope.engine().limits());
    if (!layout.has_value())
    {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(layout->totalBytes()));
    const an::DisplayPresentationSource source{.descriptor = &*descriptor,
                                               .samples = view->data(),
                                               .layout = &view->layout(),
                                               .validity = an::KernelValidityView{}};
    const an::Result<an::DisplayPresentationResult> presented =
        an::presentImageRealtimePreview(scope.execution(), *plan, source, bytes.data(), *layout);
    if (!presented.hasValue())
    {
        return std::nullopt;
    }
    const QImage shared(reinterpret_cast<const uchar*>(bytes.data()), static_cast<int>(presented->width),
                        static_cast<int>(presented->height), static_cast<qsizetype>(layout->rowStrideBytes()),
                        QImage::Format_RGBA8888);
    PresentedMaster pictures;
    pictures.whole = shared.copy();
    pictures.detail = presentedDetail(scope, *descriptor, *view, request, presented->analysis);
    return pictures;
}

/// A picture as the JPEG bytes a report keeps; empty when it cannot be encoded.
[[nodiscard]] QByteArray reportJpegOf(const QImage& picture)
{
    QByteArray jpeg;
    QBuffer jpeg_buffer(&jpeg);
    const QImage opaque = picture.convertToFormat(QImage::Format_RGB888);
    if (!jpeg_buffer.open(QIODevice::WriteOnly) || !opaque.save(&jpeg_buffer, "JPEG", kStackReportPreviewJpegQuality))
    {
        jpeg.clear();
    }
    return jpeg;
}

} // namespace

bool stageLocalFileForOverwrite(const QString& source_path, LocalOutputTransaction& transaction)
{
    if (source_path.trimmed().isEmpty())
    {
        return true;
    }

    const QString src = QFileInfo(source_path).absoluteFilePath();
    if (transaction.trackedPaths.contains(src))
    {
        return true;
    }
    if (!QFileInfo::exists(src))
    {
        transaction.trackedPaths.push_back(src);
        return true;
    }

    const QString temp = src + QStringLiteral(".tmp_archive_") + transaction.runId;
    (void)QFile::remove(temp);
    if (QFile::rename(src, temp))
    {
        transaction.trackedPaths.push_back(src);
        transaction.staged.push_back({src, temp, transaction.counters});
        return true;
    }
    if (copyWorkflowFile(src, temp, transaction.counters))
    {
        if (!QFile::remove(src))
        {
            (void)QFile::remove(temp);
            LOG_WARNING("LightsStackingStage: copied archive but could not remove source '{}'", src.toStdString());
            return false;
        }
        transaction.trackedPaths.push_back(src);
        transaction.staged.push_back({src, temp, transaction.counters});
        return true;
    }

    LOG_WARNING("LightsStackingStage: failed to stage archive '{}' -> '{}'", src.toStdString(), temp.toStdString());
    return false;
}

void restoreLocalStagedFiles(LocalOutputTransaction& transaction)
{
    if (transaction.restored)
    {
        return;
    }
    transaction.restored = true;

    for (const QString& path : transaction.trackedPaths)
    {
        (void)QFile::remove(path);
    }

    for (const auto& entry : transaction.staged)
    {
        if (!QFileInfo::exists(entry.stagedPath))
        {
            continue;
        }

        (void)QFile::remove(entry.originalPath);
        if (!QFile::rename(entry.stagedPath, entry.originalPath))
        {
            if (copyWorkflowFile(entry.stagedPath, entry.originalPath, entry.counters))
            {
                QFile::remove(entry.stagedPath);
            }
            else
            {
                LOG_WARNING("LightsStackingStage: failed to restore staged output '{}' -> '{}'",
                            entry.stagedPath.toStdString(), entry.originalPath.toStdString());
            }
        }
    }
}

an::Result<LocalOutputTransaction> stageOutputsForOverwrite(const QString& output_path,
                                                            WorkflowExecutionCounterAccumulator* counters)
{
    LocalOutputTransaction transaction;
    transaction.runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    transaction.counters = counters;
    const QFileInfo out_info(output_path);
    const QDir out_dir(out_info.absolutePath());

    QStringList paths{out_info.absoluteFilePath(), out_info.absoluteFilePath() + QStringLiteral(".preview.png"),
                      out_dir.filePath(QStringLiteral("preview.png"))};
    QVector<ImageIntegration::StackingAuxiliaryOutputSpec> auxiliary;
    auxiliary += ImageIntegration::rejectionMapsForOutput(out_info, true);
    auxiliary += ImageIntegration::rejectionMapsForOutput(out_info, false);
    auxiliary += ImageIntegration::qualityMapsForOutput(out_info, true);
    for (const auto& spec : auxiliary)
    {
        const QString path = out_dir.filePath(spec.filename);
        if (!paths.contains(path))
        {
            paths.push_back(path);
        }
    }
    for (const QString& path : paths)
    {
        if (!stageLocalFileForOverwrite(path, transaction))
        {
            restoreLocalStagedFiles(transaction);
            return refusal(an::ErrorCode::ExternalFailure,
                           "Failed to stage existing output '" + path.toUtf8().toStdString() + "'");
        }
    }
    return transaction;
}

StackPreviewAssets generateResidentStackPreviewAssets(const an::consumer::OperationScope& scope,
                                                      const an::Image<float>& master, const QString& thumbnail_path)
{
    const std::optional<PresentedMaster> pictures = presentedPreview(scope, master);
    if (!pictures.has_value() || pictures->whole.isNull())
    {
        return {};
    }
    const QImage* preview = &pictures->whole;

    StackPreviewAssets assets;
    assets.reportPreviewJpeg = reportJpegOf(*preview);
    if (!assets.reportPreviewJpeg.isEmpty())
    {
        assets.reportPreviewSize = preview->size();
        // A detail is kept beside the whole frame it shares a stretch with, never alone.
        if (!pictures->detail.isNull())
        {
            assets.reportDetailJpeg = reportJpegOf(pictures->detail);
            if (!assets.reportDetailJpeg.isEmpty())
            {
                assets.reportDetailSize = pictures->detail.size();
            }
        }
    }

    const QImage thumbnail = preview->scaled(kStackUiPreviewMaximumSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QSaveFile thumbnail_file(thumbnail_path);
    if (thumbnail_file.open(QIODevice::WriteOnly) && thumbnail.save(&thumbnail_file, "PNG"))
    {
        (void)thumbnail_file.commit();
    }

    return assets;
}

void clearPreparedStackPreviewAssets(IntegrationGroupResult& group_result)
{
    if (!group_result.previewPath.isEmpty())
    {
        (void)QFile::remove(group_result.previewPath);
        group_result.previewPath.clear();
    }
    group_result.reportPreviewJpeg.clear();
    group_result.reportPreviewSize = {};
    group_result.reportDetailJpeg.clear();
    group_result.reportDetailSize = {};
}

void prepareResidentStackPreviewAssets(const StackPreviewGenerator& preview_generator,
                                       const an::consumer::OperationScope& scope, const an::Image<float>& master,
                                       const QString& output_path, IntegrationGroupResult& group_result)
{
    const QString thumbnail_path = output_path + QStringLiteral(".preview.png");
    group_result.reportPreviewPreparedForCurrentRun = true;
    try
    {
        const StackPreviewAssets assets = preview_generator(scope, master, thumbnail_path);
        if (QFileInfo::exists(thumbnail_path))
        {
            group_result.previewPath = thumbnail_path;
        }
        if (!assets.reportPreviewJpeg.isEmpty() && assets.reportPreviewSize.isValid() &&
            assets.reportPreviewSize.width() <= static_cast<int>(kStackReportPreviewMaximumSide) &&
            assets.reportPreviewSize.height() <= static_cast<int>(kStackReportPreviewMaximumSide))
        {
            group_result.reportPreviewJpeg = assets.reportPreviewJpeg;
            group_result.reportPreviewSize = assets.reportPreviewSize;
            if (!assets.reportDetailJpeg.isEmpty() && assets.reportDetailSize.isValid() &&
                assets.reportDetailSize.width() <= ImageIntegration::kReportDetailRegionSize.width() &&
                assets.reportDetailSize.height() <= ImageIntegration::kReportDetailRegionSize.height())
            {
                group_result.reportDetailJpeg = assets.reportDetailJpeg;
                group_result.reportDetailSize = assets.reportDetailSize;
            }
        }
    }
    catch (const std::exception& ex)
    {
        (void)QFile::remove(thumbnail_path);
        clearPreparedStackPreviewAssets(group_result);
        LOG_WARNING("LightsStackingStage: optional resident preview generation failed: {}", ex.what());
    }
}

an::Result<std::vector<StackMapImage>> stackMapImages(const an::EngineContext& engine, an::StackResult& result,
                                                      const std::uint32_t frameCount, const an::ImageMetadata& metadata)
{
    std::vector<StackMapImage> maps;
    const std::optional<an::ImageDescriptor> described = mapDescriptor(result.descriptor);
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::Internal, "a map beside the stack has no description");
    }
    const double frames = static_cast<double>((std::max)(frameCount, 1u));
    const std::array<std::pair<an::StackOutputMap, StackingAuxiliaryOutputKind>, 7u> kinds{{
        {an::StackOutputMap::RejectionMerged, StackingAuxiliaryOutputKind::RejectionMerged},
        {an::StackOutputMap::RejectionLow, StackingAuxiliaryOutputKind::RejectionLow},
        {an::StackOutputMap::RejectionHigh, StackingAuxiliaryOutputKind::RejectionHigh},
        {an::StackOutputMap::Coverage, StackingAuxiliaryOutputKind::Coverage},
        {an::StackOutputMap::Variance, StackingAuxiliaryOutputKind::Variance},
        {an::StackOutputMap::SignalToNoiseGain, StackingAuxiliaryOutputKind::SnrGain},
        {an::StackOutputMap::WeightSum, StackingAuxiliaryOutputKind::WeightSum},
    }};
    for (const auto& [which, kind] : kinds)
    {
        an::ImageStorageBytes& bytes = result.maps[static_cast<std::size_t>(which)];
        if (!bytes.valid())
        {
            continue;
        }
        an::Result<an::Image<float>> image = an::Status::failure(an::ErrorCode::Internal, "the map was not made");
        if (which == an::StackOutputMap::Coverage)
        {
            image = countMap(engine, std::move(bytes), *described, metadata, 1.0, std::nullopt);
        }
        else if (an::stackOutputMapSampleType(which) == an::SampleType::UInt32)
        {
            image = countMap(engine, std::move(bytes), *described, metadata, frames, 1.0);
        }
        else
        {
            image = valueMap(std::move(bytes), *described, metadata);
        }
        if (!image.hasValue())
        {
            return image.status();
        }
        maps.push_back(StackMapImage{.kind = kind, .image = std::move(*image)});
    }
    return maps;
}

QString stackFinalizationPhaseName(const StackFinalizationPhase phase)
{
    switch (phase)
    {
    case StackFinalizationPhase::FinalMasterSave:
        return QStringLiteral("Final Master Save");
    case StackFinalizationPhase::RequestedMaps:
        return QStringLiteral("Requested Maps");
    case StackFinalizationPhase::FinalMasterWcs:
        return QStringLiteral("Final Master WCS");
    case StackFinalizationPhase::PreviewGeneration:
        return QStringLiteral("Preview Generation");
    case StackFinalizationPhase::DigestArtifactRecording:
        return QStringLiteral("Digest & Artifact Recording");
    case StackFinalizationPhase::CompletionPublication:
        return QStringLiteral("Completion Publication");
    }

    return {};
}

} // namespace ImageIntegration::detail
