// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The summary reports a view and a sequence show, over the engine's typed records: an image's description, its
/// acquisition facts read once through the engine's projection of the file's cards, and the file facts its caller
/// supplies; a sequence's description from its source. Every field nothing states reads "-", a path the application
/// keeps as a temporary file is not shown, and the labels are the product's own.

#include "ControlsServicesTestSupport.h"

#include "common.h"
#include "controls/IAppContext.h"

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::services::InstalledContext;
using an::tests::services::openContext;
using an::tests::services::TestAppContext;

constexpr std::uint64_t kMegabyte = 1048576u;

/// The description of an image of a given size, representation, schema and filter array.
an::ImageDescriptor describe(std::uint32_t width, std::uint32_t height, std::uint32_t channels, an::SampleType type,
                             an::ChannelSchema schema, an::CfaDescriptor cfa = an::CfaDescriptor{})
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(an::ImageDescriptorRequest{
        .extent = *extent,
        .sampleType = type,
        .channelSchema = schema,
        .colorSpace = schema == an::ChannelSchema::Rgb ? an::ColorSpace::LinearRgb : an::ColorSpace::Unspecified,
        .numericDomain = an::NumericDomain::NormalizedLinear,
        .cfa = cfa,
        .coordinateOrigin = an::CoordinateOrigin::TopLeft,
        .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

/// Metadata whose acquisition facts are what the engine projects out of the given cards, as a decoder builds it.
an::ImageMetadata metadataFrom(std::vector<std::pair<an::CodecNamespace, std::pair<std::string, std::string>>> cards)
{
    std::vector<an::CodecCard> built;
    for (auto& [origin, card] : cards)
    {
        an::Result<an::CodecCard> created = an::CodecCard::create(origin, card.first, card.second, "");
        EXPECT_TRUE(created.hasValue()) << card.first;
        if (created.hasValue())
        {
            built.push_back(*created);
        }
    }
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(built));
    EXPECT_TRUE(codec.hasValue()) << codec.status().message();
    an::Result<an::AcquisitionMetadata> acquisition =
        an::AcquisitionMetadata::create(an::projectAcquisition(*codec).acquisition);
    EXPECT_TRUE(acquisition.hasValue()) << acquisition.status().message();
    return an::ImageMetadata::create(*acquisition, an::ImageSemantics{}, an::AstrometricSolution{}, an::Provenance{},
                                     *codec, std::nullopt);
}

/// Whether a report states a label with a value, as the product's template writes them.
::testing::AssertionResult states(const std::string& report, const std::string& label, const std::string& value)
{
    const std::string expected = "<strong>" + label + ":</strong> " + value;
    if (report.find(expected) != std::string::npos)
    {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << "the report does not state \"" << expected << "\":\n" << report;
}

/// Milliseconds since the epoch of a UTC date and time.
std::int64_t utcMilliseconds(int year, int month, int day, int hour, int minute, int second)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute, second), QTimeZone::UTC).toMSecsSinceEpoch();
}

/// The local-time text the report writes for a file time.
std::string localFileTime(std::int64_t milliseconds)
{
    return QDateTime::fromMSecsSinceEpoch(milliseconds)
        .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))
        .toStdString();
}

TEST(ImageReportTest, AFitsImageStatesItsFileDescriptionAndAcquisition)
{
    const an::ImageDescriptor descriptor = describe(1024u, 1024u, 3u, an::SampleType::Float32, an::ChannelSchema::Rgb);
    const an::ImageMetadata metadata = metadataFrom({{an::CodecNamespace::Fits, {"INSTRUME", "'ZWO ASI2600MM'"}},
                                                     {an::CodecNamespace::Fits, {"EXPTIME", "300."}},
                                                     {an::CodecNamespace::Fits, {"CCD-TEMP", "-10.5"}},
                                                     {an::CodecNamespace::Fits, {"GAIN", "100"}},
                                                     {an::CodecNamespace::Fits, {"OFFSET", "50"}},
                                                     {an::CodecNamespace::Fits, {"SWCREATE", "'N.I.N.A.'"}},
                                                     {an::CodecNamespace::Fits, {"DATE", "'2026-03-01T22:15:07'"}}});
    const std::int64_t created = utcMilliseconds(2026, 3, 2, 8, 0, 0);
    const std::int64_t modified = utcMilliseconds(2026, 3, 3, 9, 30, 0);
    const ReportFileFacts file{
        .path = "C:/data/orion.fits", .sizeBytes = 12u * kMegabyte, .createdMs = created, .modifiedMs = modified};

    const std::string report = getImageFullReport(descriptor, metadata, file, "Orion");

    EXPECT_NE(report.find("<strong>Image Name:</strong></span> Orion</h3>"), std::string::npos) << report;
    EXPECT_TRUE(states(report, "File Name", "C:/data/orion.fits"));
    EXPECT_TRUE(states(report, "File Size", "12.00 MB"));
    EXPECT_TRUE(states(report, "File Creation Time", localFileTime(created)));
    EXPECT_TRUE(states(report, "File Modification Time", localFileTime(modified)));
    EXPECT_TRUE(states(report, "File Codec", "FITS"));
    EXPECT_TRUE(states(report, "Image Resolution (Width x Height)", "1024x1024"));
    EXPECT_TRUE(states(report, "Image Channels", "3"));
    EXPECT_NE(report.find("<strong>Image Format:</strong>&nbsp;32 bits (float)"), std::string::npos) << report;
    EXPECT_TRUE(states(report, "Image Pixel Format", "RGB"));
    EXPECT_TRUE(states(report, "CFA pattern", "-"));
    EXPECT_TRUE(states(report, "Image Size (in memory)", "12.00 MB"));
    EXPECT_TRUE(states(report, "Camera Model", "ZWO ASI2600MM"));
    EXPECT_TRUE(states(report, "Gain", "100")) << "a FITS file with a gain and no ISO names it a gain";
    EXPECT_TRUE(states(report, "Offset", "50"));
    EXPECT_TRUE(states(report, "Image Creation Date", "2026-03-01 22:15:07"));
    EXPECT_TRUE(states(report, "Exposure Time", "300 sec"));
    EXPECT_TRUE(states(report, "Sensor Temperature", "-10.5 C"));
    EXPECT_TRUE(states(report, "Software Used", "N.I.N.A."));
}

TEST(ImageReportTest, ACameraFileStatesItsIsoAShortExposureAndItsFilterArrayAtTheImageOrigin)
{
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(an::CfaPattern::Rggb, 1u, 0u);
    ASSERT_TRUE(cfa.has_value());
    const an::ImageDescriptor descriptor =
        describe(6000u, 4000u, 1u, an::SampleType::UInt16, an::ChannelSchema::Mono, *cfa);
    const an::ImageMetadata metadata =
        metadataFrom({{an::CodecNamespace::CameraRaw, {"Exif.Image.Model", "Canon EOS R6"}},
                      {an::CodecNamespace::CameraRaw, {"Exif.Photo.ISOSpeedRatings", "800"}},
                      {an::CodecNamespace::CameraRaw, {"Exif.Photo.ExposureTime", "1/250"}}});

    const std::string report = getImageFullReport(descriptor, metadata, ReportFileFacts{.path = "IMG_0001.CR3"});

    EXPECT_NE(report.find("<strong>Image Name:</strong></span> IMG_0001.CR3</h3>"), std::string::npos)
        << "a report with no title is named after its file:\n"
        << report;
    EXPECT_TRUE(states(report, "File Codec", "RAW"));
    EXPECT_TRUE(states(report, "Camera Model", "Canon EOS R6"));
    EXPECT_TRUE(states(report, "ISO Speed", "800"));
    EXPECT_TRUE(states(report, "Exposure Time", "1/250 sec"));
    EXPECT_TRUE(states(report, "CFA pattern", "GRBG")) << "the pattern is read at the image's own origin";
    EXPECT_NE(report.find("<strong>Image Format:</strong>&nbsp;16 bits (unsigned integer)"), std::string::npos);
    EXPECT_TRUE(states(report, "Image Pixel Format", "Grayscale"));
    EXPECT_TRUE(states(report, "Image Size (in memory)", "45.78 MB"));
    EXPECT_TRUE(states(report, "File Size", "-")) << "a size nobody stated is not guessed";
}

TEST(ImageReportTest, AnImageNothingIsKnownAboutStatesADashForEveryFact)
{
    const an::ImageDescriptor descriptor = describe(8u, 4u, 1u, an::SampleType::Float64, an::ChannelSchema::Mono);
    const std::string report = getImageFullReport(descriptor, an::ImageMetadata{}, ReportFileFacts{});

    for (const char* label : {"File Name", "File Size", "File Creation Time", "File Modification Time", "File Codec",
                              "CFA pattern", "Camera Model", "ISO Speed", "Offset", "Image Creation Date",
                              "Exposure Time", "Sensor Temperature", "Software Used"})
    {
        EXPECT_TRUE(states(report, label, "-")) << label;
    }
    EXPECT_NE(report.find("<strong>Image Format:</strong>&nbsp;64 bits (float)"), std::string::npos) << report;
}

TEST(ImageReportTest, AFileTheApplicationKeepsAsATemporaryFileIsNotShown)
{
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    context->tempDirectory = QStringLiteral("C:/scratch");
    const InstalledContext installed(*context);
    const an::ImageDescriptor descriptor = describe(8u, 4u, 1u, an::SampleType::Float32, an::ChannelSchema::Mono);

    const std::string report = getImageFullReport(
        descriptor, an::ImageMetadata{},
        ReportFileFacts{.path = "C:/scratch/history_3.fits", .sizeBytes = kMegabyte, .createdMs = 0, .modifiedMs = 0},
        "Edited");

    EXPECT_TRUE(states(report, "File Name", "-")) << report;
    EXPECT_TRUE(states(report, "File Size", "-"));
    EXPECT_TRUE(states(report, "File Codec", "-"));
}

TEST(ImageReportTest, EveryRepresentationAndSchemaHasTheLabelTheProductShows)
{
    EXPECT_EQ(sampleTypeReportLabel(an::SampleType::Int8), "8 bits (signed integer)");
    EXPECT_EQ(sampleTypeReportLabel(an::SampleType::UInt8), "8 bits (unsigned integer)");
    EXPECT_EQ(sampleTypeReportLabel(an::SampleType::Int16), "16 bits (signed integer)");
    EXPECT_EQ(sampleTypeReportLabel(an::SampleType::UInt16), "16 bits (unsigned integer)");
    EXPECT_EQ(sampleTypeReportLabel(an::SampleType::Int32), "32 bits (signed integer)");
    EXPECT_EQ(sampleTypeReportLabel(an::SampleType::UInt32), "32 bits (unsigned integer)");
    EXPECT_EQ(sampleTypeReportLabel(an::SampleType::Float32), "32 bits (float)");
    EXPECT_EQ(sampleTypeReportLabel(an::SampleType::Float64), "64 bits (float)");

    EXPECT_EQ(colorReportLabel(an::ChannelSchema::Mono), "Grayscale");
    EXPECT_EQ(colorReportLabel(an::ChannelSchema::Rgb), "RGB");
    EXPECT_EQ(colorReportLabel(an::ChannelSchema::Xyz), "CIE XYZ");
    EXPECT_EQ(colorReportLabel(an::ChannelSchema::Lab), "CIE L*a*b");
    EXPECT_EQ(colorReportLabel(an::ChannelSchema::Hsl), "HSL");
    EXPECT_EQ(colorReportLabel(an::ChannelSchema::Hsv), "HSV");

    EXPECT_EQ(cfaReportLabel(an::CfaDescriptor{}), "-");
    for (const auto& [pattern, label] :
         {std::pair{an::CfaPattern::Rggb, "RGGB"}, std::pair{an::CfaPattern::Bggr, "BGGR"},
          std::pair{an::CfaPattern::Grbg, "GRBG"}, std::pair{an::CfaPattern::Gbrg, "GBRG"}})
    {
        const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(pattern, 0u, 0u);
        ASSERT_TRUE(cfa.has_value());
        EXPECT_EQ(cfaReportLabel(*cfa), label);
    }
}

TEST(ImageReportTest, TheFileFactsAreWhatTheFileSystemStatesAndNothingForAPathThatNamesNoFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("light.fits"));
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(QByteArray(3 * 1048576, '\0')), 3 * 1048576);
    }

    const ReportFileFacts facts = reportFileFacts(path.toStdString());
    EXPECT_EQ(facts.path, path.toStdString());
    ASSERT_TRUE(facts.sizeBytes.has_value());
    EXPECT_EQ(*facts.sizeBytes, 3u * kMegabyte);
    ASSERT_TRUE(facts.modifiedMs.has_value());
    EXPECT_EQ(*facts.modifiedMs, QFileInfo(path).lastModified().toMSecsSinceEpoch());

    for (const std::string& nothing : {std::string(), directory.filePath(QStringLiteral("missing.fits")).toStdString(),
                                       directory.path().toStdString()})
    {
        const ReportFileFacts none = reportFileFacts(nothing);
        EXPECT_EQ(none.path, nothing);
        EXPECT_FALSE(none.sizeBytes.has_value()) << nothing;
        EXPECT_FALSE(none.createdMs.has_value()) << nothing;
        EXPECT_FALSE(none.modifiedMs.has_value()) << nothing;
    }
}

/// A sequence whose frames are a fixed raw frame.
an::SequenceDescriptor sequenceOf(an::SequenceFormat format, std::uint32_t width, std::uint32_t height,
                                  an::SampleType type, std::optional<std::uint64_t> frames,
                                  std::optional<an::SequenceRate> rate)
{
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    EXPECT_TRUE(cfa.has_value() && extent.has_value());
    an::SequenceDescriptor sequence;
    sequence.format = format;
    sequence.fixedFrame =
        an::RawFrameDescriptor::create(an::RawFrameDescriptorRequest{.extent = *extent,
                                                                     .sampleFormat = an::rawSampleFormatOf(type),
                                                                     .byteOrder = an::ByteOrder::Native,
                                                                     .channelSchema = an::ChannelSchema::Mono,
                                                                     .colorSpace = an::ColorSpace::Unspecified,
                                                                     .numericDomain = an::NumericDomain::RawSensorCode,
                                                                     .cfa = *cfa,
                                                                     .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                                     .pixelAspectRatio = 1.0});
    EXPECT_TRUE(sequence.fixedFrame.has_value());
    sequence.frameCount = frames;
    sequence.nominalFrameRate = rate;
    return sequence;
}

TEST(ImageReportTest, ASerSequenceStatesItsFramesRateAndRecordingDate)
{
    const an::SequenceDescriptor sequence =
        sequenceOf(an::SequenceFormat::Ser, 640u, 480u, an::SampleType::UInt16, 1000u, an::SequenceRate{30u, 1u});
    const an::UtcInstant recorded{utcMilliseconds(2026, 1, 15, 3, 4, 5) * 1000000};

    const std::string report = getSERSequenceFullReport(
        sequence, ReportFileFacts{.path = "D:/planets/jupiter.ser", .sizeBytes = 2048u * kMegabyte}, "Jupiter",
        recorded);

    EXPECT_NE(report.find("<strong><u>Sequence Name:</u></strong> Jupiter</h3>"), std::string::npos) << report;
    EXPECT_NE(report.find("<strong>File Size:</strong> 2.00 GB"), std::string::npos) << report;
    EXPECT_NE(report.find("<strong>Sequence Type:</strong> SER sequence"), std::string::npos) << report;
    EXPECT_NE(report.find("<strong>Resolution (Width x Height):</strong> 640x480"), std::string::npos) << report;
    EXPECT_NE(report.find("<strong>Channels:</strong> 1"), std::string::npos) << report;
    EXPECT_NE(report.find("<strong>CFA pattern:</strong> RGGB"), std::string::npos) << report;
    EXPECT_NE(report.find("<strong>Number of Frames:</strong> 1000"), std::string::npos) << report;
    EXPECT_NE(report.find("<strong>Frames per Second:</strong> 30.00"), std::string::npos) << report;
    EXPECT_NE(report.find("<strong>Pixel Format:</strong> 16 bits (unsigned integer)"), std::string::npos) << report;
    EXPECT_NE(report.find("<strong>Frame size (in memory):</strong> 0.59 MB"), std::string::npos) << report;
    EXPECT_NE(report.find("<strong>Sequence creation date:</strong> 2026-01-15 03:04:05"), std::string::npos) << report;
}

TEST(ImageReportTest, AnImageListStatesWhatItKnowsAndADashForTheRest)
{
    const an::SequenceDescriptor sequence =
        sequenceOf(an::SequenceFormat::ImageList, 4096u, 4096u, an::SampleType::Float32, std::nullopt, std::nullopt);

    const std::string report =
        getImageListSequenceFullReport(sequence, ReportFileFacts{.path = "E:/lights/lights.anseq"});

    EXPECT_NE(report.find("<strong><u>Sequence Name:</u></strong> lights.anseq</h3>"), std::string::npos) << report;
    EXPECT_NE(report.find("<strong>Total files size:</strong> -"), std::string::npos) << report;
    EXPECT_NE(report.find("<strong>Sequence Type:</strong> Image list sequence"), std::string::npos) << report;
    EXPECT_NE(report.find("<strong>Number of Frames:</strong> -"), std::string::npos) << report;
    EXPECT_NE(report.find("<strong>Frames per Second:</strong> -"), std::string::npos) << report;
    EXPECT_NE(report.find("<strong>Frame size (in memory):</strong> 64.00 MB"), std::string::npos) << report;
    EXPECT_NE(report.find("<strong>Sequence creation date:</strong> -"), std::string::npos) << report;
}

} // namespace
