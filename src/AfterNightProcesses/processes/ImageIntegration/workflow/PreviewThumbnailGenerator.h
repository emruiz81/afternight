// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// The integration's bounded previews: the run history's thumbnail and the report's master and map previews, each the
/// image shrunk to a bound by the largest sample under each preview pixel - so a star smaller than the reduction stays
/// visible - and then presented as the viewer presents an image, each channel its own automatic screen transfer.

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <QImage>
#include <QSize>
#include <QString>

#include <cstddef>
#include <optional>

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace ImageIntegration
{
struct PreviewGenerationDiagnostics
{
    QSize sourceSize;
    QSize previewSize;
    std::size_t sourcePixelCount = 0;
    std::size_t workingImagePixelCount = 0;
    std::size_t sourceSizedIntermediatePixelCount = 0;
    std::size_t stretchStatisticsPixelCount = 0;
    std::size_t interleavedDisplayBufferBytes = 0;
    bool usedUnlinkedRgbStretch = false;
};

/// Renders a bounded, viewer-like preview of an image without changing it: the image shrunk into a Float32 preview of
/// fractions of full scale no larger than `maxSize`, then presented through the viewer's presentation. An image of one
/// or three channels is previewed; any other, an empty bound or an image too large to describe in a `QSize` is not.
///
/// @param session the session the preview is worked out in.
/// @param image the image, read for the call.
/// @param maxSize the bound.
/// @param diagnostics what the preview allocated and measured, when asked.
/// @return the preview, or nothing.
AFTERNIGHTPROCESSES_API std::optional<QImage>
generateBoundedPreview(const an::consumer::EngineSession& session, const an::consumer::AnyImage& image,
                       const QSize& maxSize, PreviewGenerationDiagnostics* diagnostics = nullptr);

/// Writes a run's history thumbnail, `preview.png`, of an output file into the run's directory.
///
/// @return the thumbnail's name, or nothing when the file cannot be read or previewed.
AFTERNIGHTPROCESSES_API std::optional<QString> generatePreviewThumbnail(const an::consumer::EngineSession& session,
                                                                        const QString& outputPath,
                                                                        const QString& runDir,
                                                                        const QSize& maxSize = QSize(256, 256));

/// Writes an autostretched report-grade preview of an output file as a JPEG, no larger than `maxSize` when given and
/// than 2048 pixels on a side otherwise.
std::optional<QString> generateReportPreviewJpeg(const an::consumer::EngineSession& session, const QString& outputPath,
                                                 const QString& outputDir, const QString& fileName,
                                                 std::optional<QSize> maxSize = std::nullopt, int quality = 90);

/// Writes an autostretched report-grade preview of an output file as a PNG, no larger than `maxSize` when given and
/// than 2048 pixels on a side otherwise.
AFTERNIGHTPROCESSES_API std::optional<QString> generateReportPreviewPng(const an::consumer::EngineSession& session,
                                                                        const QString& outputPath,
                                                                        const QString& outputDir,
                                                                        const QString& fileName,
                                                                        std::optional<QSize> maxSize = std::nullopt);
} // namespace ImageIntegration
