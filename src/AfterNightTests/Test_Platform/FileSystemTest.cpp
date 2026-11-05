// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The lexical path helpers: the file-name and directory parts of a path in either platform's separators.

#include <AfterNightPlatform/FileSystem.h>

#include <gtest/gtest.h>

namespace
{

namespace platform = an::platform;

TEST(FileSystemTest, TheFileNamePartIsTheTextAfterTheLastSeparator)
{
    EXPECT_EQ(platform::fileNamePart("/data/lights/M42_001.fits"), "M42_001.fits");
    EXPECT_EQ(platform::fileNamePart("C:\\data\\lights\\M42_001.fits"), "M42_001.fits");
    EXPECT_EQ(platform::fileNamePart("C:\\data/mixed\\M42.fits"), "M42.fits");
    EXPECT_EQ(platform::fileNamePart("M42.fits"), "M42.fits");
    EXPECT_EQ(platform::fileNamePart("/data/lights/"), "");
    EXPECT_EQ(platform::fileNamePart(""), "");
}

TEST(FileSystemTest, TheDirectoryPartIsTheTextBeforeTheLastSeparator)
{
    EXPECT_EQ(platform::directoryPart("/data/lights/M42.fits"), "/data/lights");
    EXPECT_EQ(platform::directoryPart("C:\\data\\M42.fits"), "C:\\data");
    EXPECT_EQ(platform::directoryPart("/data/lights/"), "/data/lights");
    EXPECT_EQ(platform::directoryPart("/M42.fits"), "");
}

TEST(FileSystemTest, APathWithNoSeparatorIsItsOwnDirectoryPart)
{
    EXPECT_EQ(platform::directoryPart("M42.fits"), "M42.fits");
    EXPECT_EQ(platform::directoryPart(""), "");
}

} // namespace
