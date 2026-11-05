// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The image-list helpers the calibration, stacking, registration and list analyzer windows share: a list as the paths
/// a window persists and reads back, a calibration master held to the frames it calibrates by its description - a
/// failure a window shows rather than an exception - and a folder's image files found by the extensions the open
/// dialogs offer.

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "controls/ImageListTableHelpers.h"
#include "processes/ImageListProcessHelpers.h"

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace
{

using ImageListProcessHelpers::ImageListEntry;

/// The description of a grey or colour Float32 frame of a size.
[[nodiscard]] an::ImageDescriptor frameOf(std::uint32_t width, std::uint32_t height, std::uint32_t channels)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(
        an::ImageDescriptorRequest{.extent = *extent,
                                   .sampleType = an::SampleType::Float32,
                                   .channelSchema = channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb,
                                   .colorSpace = an::ColorSpace::Unspecified,
                                   .numericDomain = an::NumericDomain::NormalizedLinear,
                                   .cfa = an::CfaDescriptor{},
                                   .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                   .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

/// A list row of a path and a frame's description.
[[nodiscard]] ImageListEntry entryOf(const QString& path, const an::ImageDescriptor& descriptor)
{
    return ImageListEntry{.path = path, .descriptor = descriptor, .metadata = an::ImageMetadata{}};
}

/// Writes an empty file at a path below a directory, making its folders.
void touch(const QDir& root, const QString& relative)
{
    const QString path = root.filePath(relative);
    ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.close();
}

/// The file names of paths, sorted.
[[nodiscard]] QStringList namesOf(const QStringList& paths)
{
    QStringList names;
    for (const QString& path : paths)
    {
        names.push_back(QFileInfo(path).fileName());
    }
    names.sort();
    return names;
}

} // namespace

// A list is persisted as the paths of its rows in order, a row with no path left out, and read back as the strings of
// the array, trimmed, with anything else left out.
TEST(ImageListHelpersTest, AListIsPersistedAsItsPathsAndReadBackInOrder)
{
    const an::ImageDescriptor frame = frameOf(4u, 3u, 1u);
    const std::vector<ImageListEntry> list{entryOf(QStringLiteral("C:/frames/b.fits"), frame),
                                           entryOf(QString(), frame),
                                           entryOf(QStringLiteral("C:/frames/a \u00e9.fits"), frame)};
    const nlohmann::json persisted = ImageListProcessHelpers::imageFilePathsToJson(list);
    ASSERT_TRUE(persisted.is_array());
    ASSERT_EQ(persisted.size(), 2u);
    EXPECT_EQ(persisted[0].get<std::string>(), "C:/frames/b.fits");

    EXPECT_EQ(ImageListProcessHelpers::imageFilePathsFromJson(persisted),
              (QStringList{QStringLiteral("C:/frames/b.fits"), QStringLiteral("C:/frames/a \u00e9.fits")}));

    nlohmann::json mixed = nlohmann::json::array();
    mixed.push_back("  C:/x.fits ");
    mixed.push_back(7);
    mixed.push_back("   ");
    mixed.push_back("C:/y.xisf");
    EXPECT_EQ(ImageListProcessHelpers::imageFilePathsFromJson(mixed),
              (QStringList{QStringLiteral("C:/x.fits"), QStringLiteral("C:/y.xisf")}));
    EXPECT_TRUE(ImageListProcessHelpers::imageFilePathsFromJson(nlohmann::json::object()).isEmpty());
}

// A master fits the frames when it has the first frame's size and channels; a master of another size or depth, one
// that could not be read and an empty list are failures the window shows, named for what the master is.
TEST(ImageListHelpersTest, ACalibrationMasterIsHeldToTheFramesByItsDescription)
{
    const std::vector<ImageListEntry> frames{entryOf(QStringLiteral("a.fits"), frameOf(8u, 6u, 1u)),
                                             entryOf(QStringLiteral("b.fits"), frameOf(10u, 6u, 3u))};

    EXPECT_TRUE(ImageListProcessHelpers::checkCalibrationMaster(frameOf(8u, 6u, 1u), frames, "dark").ok());

    const an::Status wider = ImageListProcessHelpers::checkCalibrationMaster(frameOf(9u, 6u, 1u), frames, "flat");
    EXPECT_EQ(wider.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_STREQ(wider.message(), "Calibration flat file size is different than the target images");

    const an::Status taller = ImageListProcessHelpers::checkCalibrationMaster(frameOf(8u, 7u, 1u), frames, "flat");
    EXPECT_EQ(taller.code(), an::ErrorCode::IncompatibleArguments);

    const an::Status deeper = ImageListProcessHelpers::checkCalibrationMaster(frameOf(8u, 6u, 3u), frames, "bias");
    EXPECT_EQ(deeper.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_STREQ(deeper.message(), "Calibration bias file depth is different than the target images");

    const an::Status unread = ImageListProcessHelpers::checkCalibrationMaster(std::nullopt, frames, "dark");
    EXPECT_EQ(unread.code(), an::ErrorCode::NotInitialized);
    EXPECT_STREQ(unread.message(), "Could NOT load calibration dark frame");

    const an::Status empty = ImageListProcessHelpers::checkCalibrationMaster(frameOf(8u, 6u, 1u), {}, "dark");
    EXPECT_EQ(empty.code(), an::ErrorCode::InvalidArgument);
}

// Adding a folder finds the files whose extension is one of the twenty-eight the open dialogs offer - the FITS, XISF,
// TIFF, PNG and JPEG spellings and the camera RAW ones - whatever their case, below the folder too when asked.
TEST(ImageListHelpersTest, AFolderIsScannedForTheExtensionsTheOpenDialogsOffer)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QDir root(directory.path());
    const QStringList readable{
        QStringLiteral("a.fits"), QStringLiteral("b.FIT"),  QStringLiteral("c.fts"),  QStringLiteral("d.xisf"),
        QStringLiteral("e.tif"),  QStringLiteral("f.TIFF"), QStringLiteral("g.png"),  QStringLiteral("h.jpg"),
        QStringLiteral("i.jpeg"), QStringLiteral("j.jpe"),  QStringLiteral("k.CR2"),  QStringLiteral("l.cr3"),
        QStringLiteral("m.crw"),  QStringLiteral("n.nef"),  QStringLiteral("o.dng"),  QStringLiteral("p.arw"),
        QStringLiteral("q.sr2"),  QStringLiteral("r.srf"),  QStringLiteral("s.dcr"),  QStringLiteral("t.k25"),
        QStringLiteral("u.kdc"),  QStringLiteral("v.erf"),  QStringLiteral("w.mrw"),  QStringLiteral("x.orf"),
        QStringLiteral("y.pef"),  QStringLiteral("z.raf"),  QStringLiteral("za.raw"), QStringLiteral("zb.x3f")};
    for (const QString& name : readable)
    {
        touch(root, name);
    }
    for (const QString& name : {QStringLiteral("notes.txt"), QStringLiteral("clip.ser"), QStringLiteral("movie.mp4"),
                                QStringLiteral("fits"), QStringLiteral("archive.fits.gz")})
    {
        touch(root, name);
    }
    touch(root, QStringLiteral("nested/deeper.fits"));

    QStringList expected = readable;
    expected.sort();
    EXPECT_EQ(namesOf(ImageListProcessHelpers::scanFolderForSupportedImages(root.path(), false)), expected);

    expected.push_back(QStringLiteral("deeper.fits"));
    expected.sort();
    EXPECT_EQ(namesOf(ImageListProcessHelpers::scanFolderForSupportedImages(root.path(), true)), expected);
}

// The list analyzer's proxy of a source is named for the source and the first sixteen hex digits of its path's
// SHA-256, so two sources of one name in two folders never share a proxy.
TEST(ImageListHelpersTest, AProxyIsNamedForItsSourceAndItsPath)
{
    const QString first = ImageListProcessHelpers::optimizedProxyFileName(QStringLiteral("C:/night1/light_001.fits"));
    const QString second = ImageListProcessHelpers::optimizedProxyFileName(QStringLiteral("C:/night2/light_001.fits"));
    EXPECT_TRUE(first.startsWith(QStringLiteral("light_001_")));
    EXPECT_TRUE(first.endsWith(QStringLiteral(".fits")));
    EXPECT_EQ(first.size(), QStringLiteral("light_001_").size() + 16 + QStringLiteral(".fits").size());
    EXPECT_NE(first, second);
    EXPECT_EQ(first, ImageListProcessHelpers::optimizedProxyFileName(QStringLiteral("C:/night1/light_001.fits")));
}
