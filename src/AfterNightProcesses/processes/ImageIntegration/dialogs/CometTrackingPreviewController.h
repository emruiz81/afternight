// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The comet tracking dialog's frame preview: where a click on it lands in the frame's own pixels, and the stretched
/// picture of a frame its fallback pane paints when no image view is available.
///
/// A picture is read through the process's session - a mosaic demosaiced for display by the product's one demosaic
/// setting, which the GUI thread reads and hands over - and presented as the viewer presents an image, each channel its
/// own automatic screen transfer, into display bytes. Reading and presenting run on a worker; the dialog shows the
/// answer on the GUI thread.

#include "app/AppContextHelpers.h"
#include "app/DemosaicTextMaps.h"
#include "controls/ImageViewFiles.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "widgets/PresentationBridge.h"
#include "widgets/ViewPresentation.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QFileInfo>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QSize>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stop_token>

namespace an::ui
{
struct CometTrackingPreviewGeometry
{
    QSize sourceSize;
    double scale = 1.0;
    QPointF imageTopLeft;
};

/// What a frame's preview produced.
struct CometTrackingFramePreviewResult
{
    /// Whether `image` holds the picture.
    bool succeeded = false;

    /// The stretched picture, at most the size asked for.
    QImage image;

    /// The frame's size in its own pixels, which a click on the picture is mapped into.
    QSize sourceSize;

    /// Why there is no picture.
    QString message;
};

[[nodiscard]] inline bool isValidCometPreviewGeometry(const CometTrackingPreviewGeometry& geometry)
{
    return geometry.sourceSize.width() > 0 && geometry.sourceSize.height() > 0 && std::isfinite(geometry.scale) &&
           geometry.scale > 0.0 && std::isfinite(geometry.imageTopLeft.x()) && std::isfinite(geometry.imageTopLeft.y());
}

[[nodiscard]] inline bool containsCometSourcePixel(const QPointF& sourcePoint, const QSize& sourceSize)
{
    return std::isfinite(sourcePoint.x()) && std::isfinite(sourcePoint.y()) && sourcePoint.x() >= 0.0 &&
           sourcePoint.y() >= 0.0 && sourcePoint.x() < static_cast<double>(sourceSize.width()) &&
           sourcePoint.y() < static_cast<double>(sourceSize.height());
}

[[nodiscard]] inline std::optional<QPointF>
mapCometPreviewWidgetPointToSourcePixel(const QPointF& widgetPoint, const CometTrackingPreviewGeometry& geometry)
{
    if (!isValidCometPreviewGeometry(geometry) || !std::isfinite(widgetPoint.x()) || !std::isfinite(widgetPoint.y()))
        return std::nullopt;

    const QPointF source_point((widgetPoint.x() - geometry.imageTopLeft.x()) / geometry.scale,
                               (widgetPoint.y() - geometry.imageTopLeft.y()) / geometry.scale);
    if (!containsCometSourcePixel(source_point, geometry.sourceSize))
        return std::nullopt;

    return source_point;
}

[[nodiscard]] inline std::optional<QPointF>
mapCometSourcePixelToPreviewWidgetPoint(const QPointF& sourcePoint, const CometTrackingPreviewGeometry& geometry)
{
    if (!isValidCometPreviewGeometry(geometry) || !containsCometSourcePixel(sourcePoint, geometry.sourceSize))
        return std::nullopt;

    return QPointF(geometry.imageTopLeft.x() + sourcePoint.x() * geometry.scale,
                   geometry.imageTopLeft.y() + sourcePoint.y() * geometry.scale);
}

/// The method a mosaic frame is demosaiced with for the preview: the product's one demosaic setting. Reads a setting,
/// so it is called on the GUI thread and its answer handed to the worker.
///
/// @return the method; RCD where no setting is available.
[[nodiscard]] inline an::DemosaicMethod cometTrackingPreviewDemosaicMethod()
{
    const QString key =
        process_utils::selectGlobalSetting(QString::fromLatin1(SettingsKeys::RAW_DEMOSAIC_ALGORITHM),
                                           QString::fromLatin1(SettingsDefaults::RAW_DEMOSAIC_ALGORITHM));
    return demosaicMethodFromSettingsKey(key).method;
}

/// Presents an image read for the preview: each channel its own automatic screen transfer, as the viewer presents it,
/// into display bytes, scaled down to `maxSize` when it is larger.
///
/// @param session the process's session.
/// @param asset the image.
/// @param maxSize the largest picture, or an empty size for the image's own size.
/// @param stop the preview's stop.
/// @return the picture and the image's size, or why there is none.
[[nodiscard]] inline CometTrackingFramePreviewResult
buildCometTrackingFramePreviewFromAsset(const an::consumer::EngineSession& session,
                                        const an::consumer::ImageAsset& asset, const QSize& maxSize = {},
                                        std::stop_token stop = {})
{
    CometTrackingFramePreviewResult result;
    const std::optional<an::ImageDescriptor> descriptor = asset.descriptor();
    if (!descriptor.has_value() || descriptor->extent().width() == 0u || descriptor->extent().height() == 0u)
    {
        result.message = QObject::tr("Failed to load image pixels.");
        return result;
    }
    const std::uint32_t channels = descriptor->extent().channelCount();
    if (channels != 1u && channels != 3u && channels != 4u)
    {
        result.message = QObject::tr("Unsupported preview channel count.");
        return result;
    }

    an::Result<an::consumer::AssetReadLease> lease = asset.read(stop);
    if (!lease.hasValue())
    {
        result.message = QObject::tr("Preview load failed: %1").arg(QString::fromUtf8(lease.status().message()));
        return result;
    }
    an::Result<an::consumer::OperationScope> scope = session.admit(an::consumer::OperationRequest{
        .purpose = an::consumer::OperationPurpose::Preview, .maxConcurrency = 1u, .stopToken = stop});
    if (!scope.hasValue())
    {
        result.message = QObject::tr("Preview load failed: %1").arg(QString::fromUtf8(scope.status().message()));
        return result;
    }

    const ViewPresentationSettings settings{};
    const an::Result<ViewPresentationOutcome> presented =
        presentForView(scope->engine(), scope->execution(), *lease, settings, nullptr, &session.memory());
    if (!presented.hasValue() || !presented->image)
    {
        result.message = presented.hasValue() ? QObject::tr("Failed to prepare preview image.")
                                              : QObject::tr("Failed to convert preview image: %1")
                                                    .arg(QString::fromUtf8(presented.status().message()));
        return result;
    }

    result.sourceSize =
        QSize(static_cast<int>(descriptor->extent().width()), static_cast<int>(descriptor->extent().height()));
    // A deep copy, so the picture outlives the display bytes and their reservation.
    result.image = presentedQImage(presented->image).copy();
    if (maxSize.isValid() && !maxSize.isEmpty() &&
        (result.image.width() > maxSize.width() || result.image.height() > maxSize.height()))
    {
        result.image = result.image.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    result.succeeded = !result.image.isNull();
    if (!result.succeeded)
    {
        result.message = QObject::tr("Failed to render preview image.");
    }
    return result;
}

/// Reads a frame for the preview and presents it: a mosaic demosaiced by `demosaic`. Runs on a worker.
///
/// @param session the process's session.
/// @param framePath the frame.
/// @param demosaic the method a mosaic is demosaiced with, read on the GUI thread.
/// @param maxSize the largest picture, or an empty size for the frame's own size.
/// @param stop the preview's stop.
/// @return the picture and the frame's size, or why there is none.
[[nodiscard]] inline CometTrackingFramePreviewResult
loadCometTrackingFramePreviewFromFile(const an::consumer::EngineSession& session, const QString& framePath,
                                      const an::DemosaicMethod demosaic, const QSize& maxSize = {},
                                      std::stop_token stop = {})
{
    CometTrackingFramePreviewResult result;
    const QFileInfo frame_info(framePath);
    if (framePath.trimmed().isEmpty() || !frame_info.exists() || !frame_info.isFile())
    {
        result.message = QObject::tr("Preview file is unavailable.");
        return result;
    }

    an::Result<ViewReadResult> read =
        readViewImage(session, ViewReadRequest{.path = framePath, .demosaic = demosaic}, stop, nullptr);
    if (!read.hasValue())
    {
        result.message = QObject::tr("Preview load failed: %1").arg(QString::fromUtf8(read.status().message()));
        return result;
    }
    return buildCometTrackingFramePreviewFromAsset(session, read->asset, maxSize, stop);
}
} // namespace an::ui
