// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "controls/IAppContext.h"
#include <QPixmap>
#include <QRect>
#include <QByteArray>
#include <QCryptographicHash>
#include <QCursor>
#include <QPainter>
#include <QDateTime>
#include <QFileInfo>
#include <QString>
#include <QTimeZone>

#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <AfterNightPlatform/FileSystem.h>

#include <filesystem>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

// Cross-platform GetTickCount64() equivalent
inline std::uint64_t GetTickCount64Compat()
{
#if defined(_WIN32) || defined(_WIN64)
    return ::GetTickCount64();
#else
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}

inline void clearTempBackedPath(std::string& fileName)
{
    if (fileName.empty())
    {
        return;
    }

    auto* context = an::ui::appContext();
    auto* tempFileService = context ? context->getTempFileService() : nullptr;
    if (tempFileService && tempFileService->isTempFile(fileName))
    {
        fileName.clear();
    }
}

inline QRect scaleRect(const QRect& rect, qreal scaleFactor)
{
    int newWidth = static_cast<int>(rect.width() * scaleFactor);
    int newHeight = static_cast<int>(rect.height() * scaleFactor);

    int newX = static_cast<int>(rect.x() * scaleFactor);
    int newY = static_cast<int>(rect.y() * scaleFactor);

    return QRect(newX, newY, newWidth, newHeight);
}

inline QRect checkRectSize(const QRect& rect, const QRect& imageRect)
{
    QRect ret(rect);

    if (ret.x() + ret.width() > imageRect.width())
    {
        ret.setWidth(imageRect.width());
    }

    if (ret.y() + ret.height() > imageRect.height())
    {
        ret.setHeight(imageRect.height());
    }

    return ret;
}

inline bool isPointInsideCircle(const int x, const int y, const int circle_x, const int circle_y,
                                const double circle_radius)
{
    // Calculate the distance between the point and the center of the circle
    double distance = sqrt((x - circle_x) * (x - circle_x) + (y - circle_y) * (y - circle_y));

    // Check if the distance is less than or equal to the radius of the circle
    return distance <= circle_radius;
}

// Function to convert a QString to base64
inline QString QStringToBase64(const QString& str)
{
    QByteArray byteArray = str.toUtf8();
    return byteArray.toBase64();
}

// Function to decode base64 to QString
inline QString base64ToQString(const QString& base64Str)
{
    QByteArray byteArray = QByteArray::fromBase64(base64Str.toUtf8());
    return QString::fromUtf8(byteArray);
}

inline QCursor createSolidCrossCursor(const int size = 32, const int width = 2,
                                      const QColor color = QColor(220, 220, 220))
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent); // Fill the pixmap with a transparent color

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false); // Disable anti-aliasing for solid colors

    // Set pen for the cross
    QPen pen(color); // Use a solid color for the cross
    pen.setWidth(width); // Set the width of the cross lines
    painter.setPen(pen);

    // Draw the cross lines
    int mid = size / 2;
    painter.drawLine(mid, 0, mid, size); // Vertical line
    painter.drawLine(0, mid, size, mid); // Horizontal line

    return QCursor(pixmap);
}

// ---------------------------------------------------------------------------
// Summary reports: what a view's summary and its dock tooltip show of an image or a sequence, written from the
// engine's typed records. The acquisition facts are read once through the engine's projection of the file's cards,
// never searched for by keyword here; the labels are the product's own text, which the engine does not own.
// ---------------------------------------------------------------------------

/// What a report says about the file an image or a sequence was read from. Every member the caller could not learn is
/// absent, and an empty path is an image that has never been a file.
struct ReportFileFacts
{
    /// The file as it was opened, in UTF-8; for an image list, the list's own file.
    std::string path;

    /// Its size in bytes; for an image list, the size of every file it names.
    std::optional<std::uint64_t> sizeBytes;

    /// When it was created and last modified, in milliseconds since the epoch.
    std::optional<std::int64_t> createdMs;
    std::optional<std::int64_t> modifiedMs;
};

/// What the file system states about a path now; only the path for an empty path or one that names no file.
[[nodiscard]] inline ReportFileFacts reportFileFacts(const std::string& path)
{
    ReportFileFacts facts{.path = path};
    if (path.empty())
    {
        return facts;
    }
    const QFileInfo info(QString::fromUtf8(path));
    if (!info.isFile())
    {
        return facts;
    }
    facts.sizeBytes = static_cast<std::uint64_t>(info.size());
    if (const QDateTime created = info.birthTime(); created.isValid())
    {
        facts.createdMs = created.toMSecsSinceEpoch();
    }
    if (const QDateTime modified = info.lastModified(); modified.isValid())
    {
        facts.modifiedMs = modified.toMSecsSinceEpoch();
    }
    return facts;
}

/// The words a report uses for a representation.
[[nodiscard]] inline std::string sampleTypeReportLabel(const an::SampleType type)
{
    switch (type)
    {
    case an::SampleType::Int8:
        return "8 bits (signed integer)";
    case an::SampleType::UInt8:
        return "8 bits (unsigned integer)";
    case an::SampleType::Int16:
        return "16 bits (signed integer)";
    case an::SampleType::UInt16:
        return "16 bits (unsigned integer)";
    case an::SampleType::Int32:
        return "32 bits (signed integer)";
    case an::SampleType::UInt32:
        return "32 bits (unsigned integer)";
    case an::SampleType::Float32:
        return "32 bits (float)";
    case an::SampleType::Float64:
        return "64 bits (float)";
    }
    return "unknown";
}

/// The words a report uses for what an image's channels are; the schemas the product named before keep their names.
[[nodiscard]] inline std::string colorReportLabel(const an::ChannelSchema schema)
{
    switch (schema)
    {
    case an::ChannelSchema::Mono:
        return "Grayscale";
    case an::ChannelSchema::Rgb:
        return "RGB";
    case an::ChannelSchema::Rgba:
        return "RGBA";
    case an::ChannelSchema::Xyz:
        return "CIE XYZ";
    case an::ChannelSchema::Lab:
        return "CIE L*a*b";
    case an::ChannelSchema::Lch:
        return "CIE LCh";
    case an::ChannelSchema::Hsl:
        return "HSL";
    case an::ChannelSchema::Hsv:
        return "HSV";
    case an::ChannelSchema::Hsi:
        return "HSI";
    case an::ChannelSchema::YCbCr:
        return "YCbCr";
    case an::ChannelSchema::Named:
        return "named channels";
    }
    return "unknown";
}

/// The filter array a report names: a Bayer arrangement as the four filters at the image's own top-left corner, its
/// phase applied; "X-Trans" for that pattern; and "-" for an image that is not a mosaic or whose filters are a table of
/// their own.
[[nodiscard]] inline std::string cfaReportLabel(const an::CfaDescriptor& cfa)
{
    switch (cfa.pattern())
    {
    case an::CfaPattern::Rggb:
    case an::CfaPattern::Bggr:
    case an::CfaPattern::Grbg:
    case an::CfaPattern::Gbrg:
        break;
    case an::CfaPattern::XTrans:
        return "X-Trans";
    default:
        return "-";
    }
    std::string label;
    for (std::uint32_t y = 0u; y < 2u; ++y)
    {
        for (std::uint32_t x = 0u; x < 2u; ++x)
        {
            const an::CfaColor color = cfa.colorAt(x, y);
            label += color == an::CfaColor::Red ? 'R' : color == an::CfaColor::Blue ? 'B' : 'G';
        }
    }
    return label;
}

namespace report_detail
{

constexpr double kBytesPerMegabyte = 1048576.0;

[[nodiscard]] inline std::string orDash(std::string text)
{
    return text.empty() ? std::string("-") : std::move(text);
}

// A measured value in the fewest digits that show it, to six significant digits.
[[nodiscard]] inline std::string number(const double value)
{
    return QString::number(value, 'g', 6).toStdString();
}

// An exposure shorter than a second that is a whole fraction of one is written as a camera writes it.
[[nodiscard]] inline std::string exposure(const double seconds)
{
    if (seconds > 0.0 && seconds < 1.0)
    {
        const double reciprocal = 1.0 / seconds;
        const double whole = std::round(reciprocal);
        if (whole >= 2.0 && std::fabs(reciprocal - whole) <= 1e-6 * reciprocal)
        {
            return "1/" + QString::number(whole, 'f', 0).toStdString();
        }
    }
    return number(seconds);
}

[[nodiscard]] inline std::string megabytes(const double bytes)
{
    return QString::number(bytes / kBytesPerMegabyte, 'f', 2).toStdString() + " MB";
}

// A sequence runs to gigabytes, and is written in them from 1024 MB on.
[[nodiscard]] inline std::string megabytesOrGigabytes(const double bytes)
{
    const double inMegabytes = bytes / kBytesPerMegabyte;
    if (inMegabytes < 1024.0)
    {
        return QString::number(inMegabytes, 'f', 2).toStdString() + " MB";
    }
    return QString::number(inMegabytes / 1024.0, 'f', 2).toStdString() + " GB";
}

// A file time is the file system's, shown in local time to the millisecond.
[[nodiscard]] inline std::string fileTime(const std::optional<std::int64_t> milliseconds)
{
    if (!milliseconds.has_value())
    {
        return "-";
    }
    return QDateTime::fromMSecsSinceEpoch(*milliseconds)
        .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))
        .toStdString();
}

// A date a file states is an instant, shown as the UTC time it names, to the second.
[[nodiscard]] inline std::string statedTime(const std::optional<an::UtcInstant> instant)
{
    if (!instant.has_value())
    {
        return "-";
    }
    const std::int64_t milliseconds = instant->nanosecondsSinceEpoch / 1000000;
    return QDateTime::fromMSecsSinceEpoch(milliseconds, QTimeZone(QTimeZone::UTC))
        .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))
        .toStdString();
}

// The path a report shows: never a file the application keeps as a temporary file, which means nothing to the user.
[[nodiscard]] inline std::string shownPath(std::string path)
{
    clearTempBackedPath(path);
    return path;
}

} // namespace report_detail

/// The summary of an image: its file, its description and how it was acquired.
///
/// @param descriptor what the image is.
/// @param metadata its metadata. The acquisition facts come from its typed record, and from the engine's projection
///        of its cards for what the record does not model: the ISO, gain and offset settings, the software and the
///        date the file states.
/// @param file what is known of the file it was read from.
/// @param title the name the summary shows; the file's name when empty.
[[nodiscard]] inline std::string getImageFullReport(const an::ImageDescriptor& descriptor,
                                                    const an::ImageMetadata& metadata, const ReportFileFacts& file,
                                                    const std::string& title = std::string())
{
    using namespace report_detail;

    const std::string path = shownPath(file.path);
    const std::string finalTitle = title.empty() ? an::platform::fileNamePart(path) : title;

    std::string imageFormat = "-";
    std::string fileSize = "-";
    std::string fileCreated = "-";
    std::string fileModified = "-";
    if (!path.empty())
    {
        const an::CodecFormat container = an::codecFormatFromPath(path);
        if (container != an::CodecFormat::Unknown)
        {
            imageFormat = an::codecFormatName(container);
        }
        if (file.sizeBytes.has_value())
        {
            fileSize = megabytes(static_cast<double>(*file.sizeBytes));
        }
        fileCreated = fileTime(file.createdMs);
        fileModified = fileTime(file.modifiedMs);
    }

    const an::ImageExtent& extent = descriptor.extent();
    const double imageBytes = static_cast<double>(extent.sampleCount()) *
                              static_cast<double>(an::sampleTypeSizeBytes(descriptor.sampleType()));

    const an::AcquisitionMetadata& acquisition = metadata.acquisition();
    const an::AcquisitionProjection projected = an::projectAcquisition(metadata.codec());

    const std::string cameraModel = orDash(
        !acquisition.instrumentName().empty() ? acquisition.instrumentName() : projected.acquisition.instrumentName);

    std::optional<std::int64_t> iso = projected.isoSpeed;
    if (!iso.has_value() && acquisition.rawCamera().has_value() && acquisition.rawCamera()->isoSpeed > 0.0)
    {
        iso = std::llround(acquisition.rawCamera()->isoSpeed);
    }
    std::string isoSpeedOrGainTitle = "ISO Speed";
    std::string isoSpeedOrGain = "-";
    if (iso.has_value())
    {
        isoSpeedOrGain = std::to_string(*iso);
    }
    else if (projected.gainSetting.has_value())
    {
        isoSpeedOrGainTitle = "Gain";
        isoSpeedOrGain = std::to_string(*projected.gainSetting);
    }
    const std::string offset = projected.cameraOffset.has_value() ? std::to_string(*projected.cameraOffset) : "-";

    std::optional<an::UtcInstant> created = projected.creationTime;
    if (!created.has_value())
    {
        created =
            acquisition.exposureStart().has_value() ? acquisition.exposureStart() : projected.acquisition.exposureStart;
    }

    const std::optional<double> exposureSeconds = acquisition.exposureSeconds().has_value()
                                                      ? acquisition.exposureSeconds()
                                                      : projected.acquisition.exposureSeconds;
    const std::string exposureTime = exposureSeconds.has_value() ? exposure(*exposureSeconds) + " sec" : "-";

    const std::optional<double> temperature = acquisition.sensorTemperatureCelsius().has_value()
                                                  ? acquisition.sensorTemperatureCelsius()
                                                  : projected.acquisition.sensorTemperatureCelsius;
    const std::string sensorTemperature = temperature.has_value() ? number(*temperature) + " C" : "-";

    return "<h3><span style=\"text-decoration: underline;\"><strong>Image Name:</strong></span> " + finalTitle +
           "</h3>"
           "<h3><span style=\"text-decoration: underline;\">File Info</span></h3>"
           "<p><strong>File Name:</strong> " +
           orDash(path) + "<br /><strong>File Size:</strong> " + fileSize +
           "<br /><strong>File Creation Time:</strong> " + fileCreated +
           "<br /><strong>File Modification Time:</strong> " + fileModified + "<br /><strong>File Codec:</strong> " +
           imageFormat +
           "</p>"
           "<h3><span style=\"text-decoration: underline;\">Image General Info</span></h3>"
           "<p><strong>Image Resolution (Width x Height):</strong> " +
           std::to_string(extent.width()) + "x" + std::to_string(extent.height()) +
           "<br /><strong>Image Channels:</strong> " + std::to_string(extent.channelCount()) +
           "<br /><strong>Image Format:</strong>&nbsp;" + sampleTypeReportLabel(descriptor.sampleType()) +
           "<br /><strong>Image Pixel Format:</strong> " + colorReportLabel(descriptor.channelSchema()) +
           "<br /><strong>CFA pattern:</strong> " + cfaReportLabel(descriptor.cfa()) +
           "<br /><strong>Image Size (in memory):</strong> " + megabytes(imageBytes) +
           "</p>"
           "<h3><span style=\"text-decoration: underline;\">Image Detailed Info</span></h3>"
           "<p><strong>Camera Model:</strong> " +
           cameraModel + "<br /><strong>" + isoSpeedOrGainTitle + ":</strong> " + isoSpeedOrGain +
           "<br /><strong>Offset:</strong> " + offset + "<br /><strong>Image Creation Date:</strong> " +
           statedTime(created) + "<br /><strong>Exposure Time:</strong> " + exposureTime +
           "<br /><strong>Sensor Temperature:</strong> " + sensorTemperature +
           "<br /><strong>Software Used:</strong> " + orDash(projected.software) + "</p>";
}

namespace report_detail
{

// The summary both sequence kinds share; the size label and the kind are what tell them apart. `frame` is what every
// frame is described as: the sequence's fixed frame, or an image list's first.
[[nodiscard]] inline std::string sequenceReport(const an::SequenceDescriptor& sequence,
                                                const std::optional<an::RawFrameDescriptor>& frame,
                                                const ReportFileFacts& file, const std::string& title,
                                                const std::optional<an::UtcInstant> recorded, const char* sizeLabel,
                                                const char* kind)
{
    const std::string path = shownPath(file.path);
    const std::string finalTitle = title.empty() ? an::platform::fileNamePart(path) : title;

    std::string fileSize = "-";
    std::string fileCreated = "-";
    std::string fileModified = "-";
    if (!path.empty())
    {
        if (file.sizeBytes.has_value())
        {
            fileSize = megabytesOrGigabytes(static_cast<double>(*file.sizeBytes));
        }
        fileCreated = fileTime(file.createdMs);
        fileModified = fileTime(file.modifiedMs);
    }

    std::string width = "-";
    std::string height = "-";
    std::string channels = "-";
    std::string cfaPattern = "-";
    std::string pixelType = "-";
    std::string frameSize = "-";
    if (frame.has_value())
    {
        const an::ImageExtent& extent = frame->extent();
        width = std::to_string(extent.width());
        height = std::to_string(extent.height());
        channels = std::to_string(extent.channelCount());
        cfaPattern = cfaReportLabel(frame->cfa());
        if (const std::optional<an::SampleType> resident = frame->residentSampleType())
        {
            pixelType = sampleTypeReportLabel(*resident);
        }
        frameSize = megabytesOrGigabytes(static_cast<double>(extent.sampleCount()) *
                                         static_cast<double>(frame->sampleFormat().sizeBytes));
    }
    const std::string frames = sequence.frameCount.has_value() ? std::to_string(*sequence.frameCount) : "-";
    std::string framesPerSecond = "-";
    if (sequence.nominalFrameRate.has_value() && sequence.nominalFrameRate->denominator != 0u)
    {
        framesPerSecond = QString::number(static_cast<double>(sequence.nominalFrameRate->numerator) /
                                              static_cast<double>(sequence.nominalFrameRate->denominator),
                                          'f', 2)
                              .toStdString();
    }

    return "<h3><strong><u>Sequence Name:</u></strong> " + finalTitle +
           "</h3>"
           "<h3><u>File Info</u></h3>"
           "<p><strong>File Name:</strong> " +
           orDash(path) + "<br><strong>" + sizeLabel + ":</strong> " + fileSize +
           "<br><strong>File Creation Time:</strong> " + fileCreated + "<br><strong>File Modification Time:</strong> " +
           fileModified + "<br><strong>Sequence Type:</strong> " + kind +
           "</p>"
           "<h3><u>Sequence General Info</u></h3>"
           "<p><strong>Resolution (Width x Height):</strong> " +
           width + "x" + height + "<br><strong>Channels:</strong> " + channels + "<br><strong>CFA pattern:</strong> " +
           cfaPattern + "<br><strong>Number of Frames:</strong> " + frames +
           "<br><strong>Frames per Second:</strong> " + framesPerSecond + "<br><strong>Pixel Format:</strong> " +
           pixelType + "<br><strong>Frame size (in memory):</strong> " + frameSize +
           "<br><strong>Sequence creation date:</strong> " + statedTime(recorded) +
           "</p>"
           "<p>&nbsp;</p>";
}

} // namespace report_detail

/// The summary of a SER sequence: its file, its frames and when it was recorded.
///
/// @param sequence what the sequence is, as its source described it.
/// @param file what is known of its file.
/// @param title the name the summary shows; the file's name when empty.
/// @param recorded when the sequence was recorded, as the file states it.
[[nodiscard]] inline std::string getSERSequenceFullReport(const an::SequenceDescriptor& sequence,
                                                          const ReportFileFacts& file,
                                                          const std::string& title = std::string(),
                                                          const std::optional<an::UtcInstant> recorded = std::nullopt)
{
    return report_detail::sequenceReport(sequence, sequence.fixedFrame, file, title, recorded, "File Size",
                                         "SER sequence");
}

/// The summary of an image-list sequence: its list file, the size of the files it names, and its frames.
///
/// @param sequence what the sequence is, as its source described it.
/// @param files what is known of the list file, with the size of every file it names.
/// @param title the name the summary shows; the list file's name when empty.
/// @param recorded when the list was made, as its file states it, when the caller knows it.
/// @param firstFrame the first frame's description, for a list that states no frame every frame is held to.
[[nodiscard]] inline std::string
getImageListSequenceFullReport(const an::SequenceDescriptor& sequence, const ReportFileFacts& files,
                               const std::string& title = std::string(),
                               const std::optional<an::UtcInstant> recorded = std::nullopt,
                               const std::optional<an::RawFrameDescriptor>& firstFrame = std::nullopt)
{
    return report_detail::sequenceReport(sequence, sequence.fixedFrame.has_value() ? sequence.fixedFrame : firstFrame,
                                         files, title, recorded, "Total files size", "Image list sequence");
}

/// The summary of a sequence a view shows, by its container: a SER's, or an image list's - the list file's own times,
/// the size of every file it names in place of its own, and its first frame's description where it states no frame
/// every frame is held to. What a view's summary and its dock tooltip both show, from what the document recorded when
/// the sequence was opened.
///
/// @param sequence what the sequence is, as its source described it.
/// @param file what is known of the sequence's own file now.
/// @param title the name the summary shows; the file's name when empty.
/// @param recorded when the sequence was recorded or made, as its file states it.
/// @param firstFrame the first frame's description, read when the sequence was opened.
/// @param namedFilesBytes for an image list, the size of every file it names, measured when it was opened.
/// @return the report; empty for any other container, whose summary is its frame's.
[[nodiscard]] inline std::string getSequenceFullReport(const an::SequenceDescriptor& sequence, ReportFileFacts file,
                                                       const std::string& title,
                                                       const std::optional<an::UtcInstant> recorded,
                                                       const std::optional<an::RawFrameDescriptor>& firstFrame,
                                                       const std::optional<std::uint64_t> namedFilesBytes)
{
    if (sequence.format == an::SequenceFormat::Ser)
    {
        return getSERSequenceFullReport(sequence, file, title, recorded);
    }
    if (sequence.format == an::SequenceFormat::ImageList)
    {
        // The list file's own times, and the size of the files it names, never its own.
        file.sizeBytes = namedFilesBytes;
        return getImageListSequenceFullReport(sequence, file, title, recorded, firstFrame);
    }
    return {};
}

// ---------------------------------------------------------------------------
// Exif fields: which of the cards a file stated the Exif window shows a row for. A view offers the window exactly when
// it would show one, so the window's rows and the view's action read this one rule.
// ---------------------------------------------------------------------------

namespace exif_detail
{

// The prefix every Exif field's key carries.
inline constexpr std::string_view kExifPrefix = "Exif.";

// The Exif fields the window hides: the maker note, the interoperability fields, the strip offsets, the subfile type,
// the embedded XMP, IPTC and Photoshop blocks, the colour profile and the thumbnail.
inline constexpr std::array<std::string_view, 10u> kHiddenExifFields{"Exif.Photo.MakerNote",
                                                                     "Exif.Iop.InteroperabilityVersion",
                                                                     "Exif.Iop.InteroperabilityIndex",
                                                                     "Exif.Image.StripOffsets",
                                                                     "Exif.Image.NewSubfileType",
                                                                     "Exif.Image.XMLPacket",
                                                                     "Exif.Image.IPTCNAA",
                                                                     "Exif.Image.ImageResources",
                                                                     "Exif.Image.InterColorProfile",
                                                                     "Exif.Thumbnail.JPEGInterchangeFormat"};

} // namespace exif_detail

/// Whether a card's namespace carries Exif: the three raster containers - JPEG, TIFF and PNG - and a camera RAW file.
/// FITS and XISF carry none.
[[nodiscard]] inline bool codecNamespaceCarriesExif(const an::CodecNamespace origin) noexcept
{
    return origin == an::CodecNamespace::Jpeg || origin == an::CodecNamespace::Tiff ||
           origin == an::CodecNamespace::Png || origin == an::CodecNamespace::CameraRaw;
}

/// Whether a card is an Exif field: a card of a namespace that carries Exif whose key starts `Exif.`. A container's
/// own entries - a PNG's bit depth, a JPEG comment - are not.
[[nodiscard]] inline bool isExifFieldCard(const an::CodecCard& card) noexcept
{
    return codecNamespaceCarriesExif(card.origin()) &&
           std::string_view(card.keyword()).starts_with(exif_detail::kExifPrefix);
}

/// Whether an Exif field is one the Exif window hides.
///
/// @param keyword the field's full key, as `Exif.Photo.MakerNote`.
[[nodiscard]] inline bool isHiddenExifKeyword(const std::string_view keyword) noexcept
{
    return std::find(exif_detail::kHiddenExifFields.begin(), exif_detail::kHiddenExifFields.end(), keyword) !=
           exif_detail::kHiddenExifFields.end();
}

/// The name the Exif window shows a field under: its key's last part - `Exif.Photo.ExposureTime` is `ExposureTime`.
/// Empty for a key that ends in a dot, which the window does not show.
[[nodiscard]] inline std::string_view exifFieldName(const std::string_view keyword) noexcept
{
    const std::size_t dot = keyword.rfind('.');
    return dot == std::string_view::npos ? keyword : keyword.substr(dot + 1u);
}

/// Whether a file stated an Exif field the Exif window shows a row for: an Exif field of a JPEG, TIFF, PNG or camera
/// RAW file, with a name, that the window does not hide. A field continued across cards keeps its key on every card,
/// so this is decided card by card.
///
/// @param codec what the file said.
/// @return true when the Exif window shows a row.
[[nodiscard]] inline bool statesShownExifField(const an::CodecMetadata& codec) noexcept
{
    return std::any_of(codec.cards().begin(), codec.cards().end(), [](const an::CodecCard& card) {
        return isExifFieldCard(card) && !isHiddenExifKeyword(card.keyword()) && !exifFieldName(card.keyword()).empty();
    });
}
