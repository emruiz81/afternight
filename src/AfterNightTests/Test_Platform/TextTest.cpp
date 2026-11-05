// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The text helpers: prefix and suffix tests, the upper-case copy and the UTF-8 conversions.

#include <AfterNightPlatform/Text.h>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace
{

namespace platform = an::platform;
using platform::CaseSensitivity;

TEST(TextTest, StartsWithFindsAPrefixAndCanIgnoreCase)
{
    EXPECT_TRUE(platform::startsWith("\\\\server\\share", "\\\\"));
    EXPECT_TRUE(platform::startsWith("abc", "abc"));
    EXPECT_FALSE(platform::startsWith("abc", "abcd"));
    EXPECT_FALSE(platform::startsWith("ABC", "ab"));
    EXPECT_TRUE(platform::startsWith("ABC", "ab", CaseSensitivity::Insensitive));
    EXPECT_TRUE(platform::startsWith("/tmp/AfterNight/swap.dump", "/TMP/afternight", CaseSensitivity::Insensitive));
}

TEST(TextTest, EndsWithFindsASuffixAndCanIgnoreCase)
{
    EXPECT_TRUE(platform::endsWith("capture.ser", ".ser"));
    EXPECT_FALSE(platform::endsWith("capture.SER", ".ser"));
    EXPECT_TRUE(platform::endsWith("capture.SER", ".ser", CaseSensitivity::Insensitive));
    EXPECT_FALSE(platform::endsWith("ser", ".ser", CaseSensitivity::Insensitive));
}

TEST(TextTest, AnEmptyPatternMatchesNothing)
{
    EXPECT_FALSE(platform::startsWith("abc", ""));
    EXPECT_FALSE(platform::endsWith("abc", ""));
    EXPECT_FALSE(platform::startsWith("", ""));
    EXPECT_FALSE(platform::endsWith("", "", CaseSensitivity::Insensitive));
}

TEST(TextTest, IgnoringCaseFoldsAsciiLettersOnly)
{
    const std::string lowerAccent = "\xC3\xA9t\xC3\xA9"; // "été"

    EXPECT_TRUE(platform::startsWith(lowerAccent, "\xC3\xA9T", CaseSensitivity::Insensitive));
    EXPECT_FALSE(platform::startsWith(lowerAccent, "\xC3\x89T", CaseSensitivity::Insensitive)); // "ÉT"
}

TEST(TextTest, ToUpperMapsAsciiLettersAndKeepsEveryOtherByte)
{
    EXPECT_EQ(platform::toUpper("light_Frame-01.fits"), "LIGHT_FRAME-01.FITS");
    EXPECT_EQ(platform::toUpper("\xC3\xA9"), "\xC3\xA9");
    EXPECT_EQ(platform::toUpper(""), "");
}

TEST(TextTest, Utf8AndWideTextRoundTrip)
{
    // ASCII, Latin-1, a CJK ideograph and a character outside the basic plane, which UTF-16 splits in two.
    const std::string utf8 = "M42 \xC3\x85ngstr\xC3\xB6m \xE6\x98\x9F \xF0\x9F\x8C\x8C";

    const std::wstring wide = platform::utf8ToWide(utf8);

    ASSERT_FALSE(wide.empty());
    EXPECT_EQ(wide.front(), L'M');
    EXPECT_EQ(platform::wideToUtf8(wide), utf8);
    EXPECT_EQ(platform::wideToUtf8(L"Å"), "\xC3\x85");
    EXPECT_EQ(platform::utf8ToWide("\xC3\x85"), L"Å");
    EXPECT_TRUE(platform::utf8ToWide("").empty());
    EXPECT_TRUE(platform::wideToUtf8(L"").empty());
}

#ifndef _WIN32
TEST(TextTest, InvalidUtf8IsRefusedWhereIconvConverts)
{
    EXPECT_THROW((void)platform::utf8ToWide("\xC3\x28"), std::runtime_error);
}
#endif

} // namespace
