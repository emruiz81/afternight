// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// The integration's bounded previews: the run history's thumbnail and the report's master and map previews, each the
/// image shrunk to a bound by the largest sample under each preview pixel - so a star smaller than the reduction stays
/// visible - and then presented as the viewer presents an image, each channel its own automatic screen transfer.
///
/// A master has a second picture, its detail: the part at its centre at full scale, one picture pixel for each of the
/// master's, presented through the statistics its whole-frame preview was presented through, so the two pictures of
/// one master share one stretch. A preview is a picture of a file, not a workflow image: it carries no identity and
/// is never published as one.

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>

#include <cstddef>
#include <memory>
#include <optional>

namespace an
{
struct DisplayPresentationAnalysis;
} // namespace an

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace ImageIntegration
{
/// The most of a master, in its own pixels, that its report's detail shows.
inline constexpr QSize kReportDetailRegionSize{960, 540};

/// The part of an image its report's detail shows: a rectangle of at most `kReportDetailRegionSize` at the image's
/// centre. An image narrower or shorter than that is shown whole that way, so a small master is its own detail.
///
/// @param imageSize the image's size in pixels.
/// @return the rectangle, in the image's pixels; empty for an image with no pixels.
[[nodiscard]] AFTERNIGHTPROCESSES_API QRect reportDetailRegion(const QSize& imageSize);

/// The previews one read of a master's file wrote, each by its name; nothing for one that was not written.
struct ReportMasterPreviews
{
    std::optional<QString> whole;
    std::optional<QString> detail;

    /// The display stretch the whole frame was presented through, when it was written: what a picture set beside
    /// it is presented through, without the master's file being read again to measure it.
    std::shared_ptr<const an::DisplayPresentationAnalysis> stretch;
};

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
///
/// @param diagnostics what the preview measured, when asked: among it the image's size in pixels and the picture's,
///        by which a caller that draws on the picture places a coordinate of the image where the picture shows it.
///        Left empty when the file was not previewed.
AFTERNIGHTPROCESSES_API std::optional<QString>
generateReportPreviewJpeg(const an::consumer::EngineSession& session, const QString& outputPath,
                          const QString& outputDir, const QString& fileName,
                          std::optional<QSize> maxSize = std::nullopt, int quality = 90,
                          PreviewGenerationDiagnostics* diagnostics = nullptr);

/// Writes a master's two report previews from one read of its file: the whole frame as `generateReportPreviewJpeg`
/// writes it, and the detail - the region `reportDetailRegion` names, at full scale, presented through the statistics
/// the whole frame was. The detail is written only when the whole frame was.
///
/// @param session the session the file is read and the previews are worked out in.
/// @param outputPath the master's file.
/// @param outputDir the folder the previews are written into, made when it is not there.
/// @param wholeName the whole-frame preview's file name.
/// @param detailName the detail's file name.
/// @param maxSize the whole frame's bound; 2048 pixels on a side when none is given.
/// @param quality the JPEG quality of both.
/// @return the names of what was written.
AFTERNIGHTPROCESSES_API ReportMasterPreviews generateReportMasterPreviews(
    const an::consumer::EngineSession& session, const QString& outputPath, const QString& outputDir,
    const QString& wholeName, const QString& detailName, std::optional<QSize> maxSize = std::nullopt, int quality = 90);

/// How a master's own report pictures were made. A picture set beside them is made the same way, or the two differ
/// by how they were reduced and stretched as well as by their samples.
enum class ReportPicturePresentation
{
    /// As `generateReportMasterPreviews` makes them of the master's file: shrunk by the largest sample of each
    /// footprint, and stretched by what that shrunk frame measures.
    Bounded,
    /// As the stacking stage makes them of the master it holds: every n-th sample, stretched by what those measure.
    AsStacked
};

/// Writes a master's two report previews as `generateReportMasterPreviews` does, presented through the display
/// stretch of another image rather than its own: the stretch that image's own whole-frame preview is presented
/// through. Two masters pictured under one stretch differ in the pictures as they differ in the samples, where
/// each under its own stretch would look alike however much brighter one is. Each file is read once.
///
/// The other image's own pictures were made one of two ways, and the stretch is the one of the way they were
/// made: a picture that is to stand beside them is reduced as they were and presented through the analysis that
/// reduction measures. A master the stretch cannot present - one of another number of channels - gets no picture.
///
/// @param session the session the files are read and the previews are worked out in.
/// @param stretchPath the image whose stretch is used.
/// @param outputPath the master to picture.
/// @param outputDir the folder the previews are written into, made when it is not there.
/// @param wholeName the whole-frame preview's file name.
/// @param detailName the detail's file name.
/// @param maxSize the whole frame's bound; 2048 pixels on a side when none is given. A picture made as the stacking
///        stage makes one takes that stage's bound, whatever this is.
/// @param quality the JPEG quality of both.
/// @param presentation how the other image's own pictures were made.
/// @param knownStretch the stretch the other image's own pictures were presented through, when whoever made them
///        kept it; the other image's file is then not read, and need not be there. Null to measure it of the file.
/// @return the names of what was written; nothing when either file does not read or the stretch cannot present the
///         master.
AFTERNIGHTPROCESSES_API ReportMasterPreviews generateReportMasterPreviewsUnderStretchOf(
    const an::consumer::EngineSession& session, const QString& stretchPath, const QString& outputPath,
    const QString& outputDir, const QString& wholeName, const QString& detailName,
    std::optional<QSize> maxSize = std::nullopt, int quality = 90,
    ReportPicturePresentation presentation = ReportPicturePresentation::Bounded,
    const an::DisplayPresentationAnalysis* knownStretch = nullptr);

/// Writes an autostretched report-grade preview of an output file as a PNG, no larger than `maxSize` when given and
/// than 2048 pixels on a side otherwise.
AFTERNIGHTPROCESSES_API std::optional<QString> generateReportPreviewPng(const an::consumer::EngineSession& session,
                                                                        const QString& outputPath,
                                                                        const QString& outputDir,
                                                                        const QString& fileName,
                                                                        std::optional<QSize> maxSize = std::nullopt);
} // namespace ImageIntegration
