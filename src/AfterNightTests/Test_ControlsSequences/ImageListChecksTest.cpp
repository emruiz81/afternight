// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The checks a list of image files passes before its frames are made into a sequence: the same size, the same
/// representation and channels, the same ISO speed or gain and the same exposure, each read from what probing a file
/// stated - and a file that states none of a fact is not compared on it.

#include "Test_ControlsSequences/SequencesTestSupport.h"

#include "controls/ImageListTableHelpers.h"

#include <AfterNightEngine/Core/EngineLimits.h>

#include <QTableWidget>

#include <gtest/gtest.h>

#include <unordered_set>

namespace
{

using namespace an::tests::sequences;
using ImageListProcessHelpers::ImageListEntry;

/// A grey sixteen-bit description of a size.
[[nodiscard]] an::ImageDescriptor describedAs(std::uint32_t width, std::uint32_t height,
                                              an::SampleType type = an::SampleType::UInt16,
                                              Channels channels = Channels::Grey)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channelCountOf(channels), an::defaultEngineLimits());
    const an::ChannelSchema schema = channels == Channels::Grey ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb;
    return *an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                                   .sampleType = type,
                                                                   .channelSchema = schema,
                                                                   .colorSpace = an::ColorSpace::Unspecified,
                                                                   .numericDomain = an::NumericDomain::NormalizedLinear,
                                                                   .cfa = an::CfaDescriptor{},
                                                                   .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                                   .pixelAspectRatio = 1.0});
}

/// A file stating the FITS cards given.
[[nodiscard]] ImageListEntry frameStating(std::initializer_list<std::pair<const char*, const char*>> cards,
                                          const char* name = "frame.fits")
{
    std::vector<an::CodecCard> stated;
    for (const auto& [key, value] : cards)
    {
        an::Result<an::CodecCard> made = an::CodecCard::create(an::CodecNamespace::Fits, key, value, {});
        EXPECT_TRUE(made.hasValue()) << made.status().message();
        stated.push_back(*made);
    }
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(stated));
    EXPECT_TRUE(codec.hasValue());
    return ImageListEntry{.path = QString::fromLatin1(name),
                          .descriptor = describedAs(8u, 6u),
                          .metadata = an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{},
                                                                an::AstrometricSolution{}, an::Provenance{}, *codec,
                                                                std::nullopt)};
}

} // namespace

// Frames taken at another ISO or gain are refused, and a frame whose metadata states neither is not compared.
TEST(ImageListChecks, FramesOfAnotherIsoOrGainAreRefused)
{
    using ImageListProcessHelpers::validateImageListISO;
    EXPECT_TRUE(validateImageListISO(
        {frameStating({{"GAIN", "100"}}), frameStating({{"GAIN", "100"}}), frameStating({{"OBJECT", "'M31'"}})}));
    EXPECT_FALSE(validateImageListISO({frameStating({{"GAIN", "100"}}), frameStating({{"GAIN", "120"}})}));
    EXPECT_FALSE(validateImageListISO({frameStating({{"ISOSPEED", "800"}}), frameStating({{"GAIN", "800"}})}));
    EXPECT_TRUE(validateImageListISO({}));
}

// Frames of another exposure are refused, and a frame whose metadata states none is not compared.
TEST(ImageListChecks, FramesOfAnotherExposureAreRefused)
{
    using ImageListProcessHelpers::validateImageListExposure;
    EXPECT_TRUE(validateImageListExposure({frameStating({{"EXPTIME", "300"}}), frameStating({{"EXPOSURE", "300."}}),
                                           frameStating({{"OBJECT", "'M31'"}})}));
    EXPECT_FALSE(validateImageListExposure({frameStating({{"EXPTIME", "300"}}), frameStating({{"EXPTIME", "120"}})}));
    EXPECT_TRUE(validateImageListExposure({}));
}

// Frames of another size, or of another representation or channel count, are refused.
TEST(ImageListChecks, FramesOfAnotherSizeOrDepthAreRefused)
{
    using ImageListProcessHelpers::validateImageListColorDepth;
    using ImageListProcessHelpers::validateImageListSizes;
    const an::ImageMetadata nothing{};
    const ImageListEntry first{.path = "a", .descriptor = describedAs(8u, 6u), .metadata = nothing};
    const ImageListEntry same{.path = "b", .descriptor = describedAs(8u, 6u), .metadata = nothing};
    const ImageListEntry taller{.path = "c", .descriptor = describedAs(8u, 7u), .metadata = nothing};
    const ImageListEntry wider{.path = "d", .descriptor = describedAs(9u, 6u), .metadata = nothing};
    const ImageListEntry floating{
        .path = "e", .descriptor = describedAs(8u, 6u, an::SampleType::Float32), .metadata = nothing};
    const ImageListEntry colour{
        .path = "f", .descriptor = describedAs(8u, 6u, an::SampleType::UInt16, Channels::Rgb), .metadata = nothing};
    EXPECT_TRUE(validateImageListSizes({first, same}));
    EXPECT_FALSE(validateImageListSizes({first, taller}));
    EXPECT_FALSE(validateImageListSizes({first, wider}));
    EXPECT_TRUE(validateImageListSizes({}));
    EXPECT_TRUE(validateImageListColorDepth({first, same, taller}));
    EXPECT_FALSE(validateImageListColorDepth({first, floating}));
    EXPECT_FALSE(validateImageListColorDepth({first, colour}));
    EXPECT_TRUE(validateImageListColorDepth({}));
}

// The ISO speed, the gain setting, the offset and the exposure a file states are read through the projection of its
// cards.
TEST(ImageListChecks, TheFactsAFileStatesAreReadFromItsCards)
{
    const ImageListEntry iso = frameStating({{"ISOSPEED", "1600"}, {"EXPTIME", "2.5"}, {"OFFSET", "30"}});
    EXPECT_EQ(ImageListProcessHelpers::isoSpeedOrGainOf(iso.metadata), std::make_pair(true, std::int64_t{1600}));
    EXPECT_EQ(ImageListProcessHelpers::exposureSecondsOf(iso.metadata), 2.5);
    EXPECT_EQ(ImageListProcessHelpers::cameraOffsetOf(iso.metadata), std::int64_t{30});
    const ImageListEntry gain = frameStating({{"GAIN", "139"}});
    EXPECT_EQ(ImageListProcessHelpers::isoSpeedOrGainOf(gain.metadata), std::make_pair(false, std::int64_t{139}));
    const ImageListEntry none = frameStating({{"OBJECT", "'M31'"}});
    EXPECT_FALSE(ImageListProcessHelpers::isoSpeedOrGainOf(none.metadata).has_value());
    EXPECT_FALSE(ImageListProcessHelpers::exposureSecondsOf(none.metadata).has_value());
    EXPECT_FALSE(ImageListProcessHelpers::cameraOffsetOf(none.metadata).has_value());
}

// Removing a file removes its row and its record; clearing empties both.
TEST(ImageListChecks, RemovingAFileRemovesItsRowAndRecord)
{
    QTableWidget table(0, 1);
    std::vector<ImageListEntry> list;
    std::unordered_set<std::string> paths;
    for (const char* name : {"a.fits", "b.fits", "c.fits"})
    {
        table.insertRow(table.rowCount());
        table.setItem(table.rowCount() - 1, 0, new QTableWidgetItem(QString::fromLatin1(name)));
        list.push_back(frameStating({}, name));
        paths.insert(name);
    }
    EXPECT_EQ(ImageListProcessHelpers::findItemInTable(&table, QStringLiteral("b.fits")), 1);
    ImageListProcessHelpers::removeItemsFromImageList({QStringLiteral("b.fits")}, &table, list, paths);
    EXPECT_EQ(table.rowCount(), 2);
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[1].path, QStringLiteral("c.fits"));
    EXPECT_EQ(paths.count("b.fits"), 0u);
    EXPECT_EQ(ImageListProcessHelpers::findItemInTable(&table, QStringLiteral("b.fits")), -1);
    bool ignored = false;
    ImageListProcessHelpers::clearImageListData(&table, list, paths, ignored);
    EXPECT_EQ(table.rowCount(), 0);
    EXPECT_TRUE(list.empty());
    EXPECT_TRUE(paths.empty());
    EXPECT_FALSE(ignored);
}
