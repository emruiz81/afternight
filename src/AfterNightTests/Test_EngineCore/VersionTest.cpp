// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Core/Version.h.
//
// The header exists to answer one question - do these headers and this binary come from the same
// build? - so the cases below are mostly about the two halves agreeing. They can only fail when
// the suite is linked against a library built from different headers, which is exactly the
// mistake the header is meant to expose.

#include <AfterNightEngine/Core/Version.h>

#include <gtest/gtest.h>

#include <string>

namespace
{

TEST(EngineVersionTest, TheBinaryReportsTheVersionItsHeadersDeclare)
{
    EXPECT_EQ(an::engineVersion(), an::engineHeaderVersion());
}

TEST(EngineVersionTest, TheHeaderVersionIsTheConstantTriple)
{
    const an::EngineVersion header = an::engineHeaderVersion();
    EXPECT_EQ(header.majorVersion, an::kEngineVersionMajor);
    EXPECT_EQ(header.minorVersion, an::kEngineVersionMinor);
    EXPECT_EQ(header.patchVersion, an::kEngineVersionPatch);
}

TEST(EngineVersionTest, TheVersionStringSpellsTheVersionNumbers)
{
    // The string is written out rather than assembled from the numbers, because assembling a
    // string literal from integers needs the preprocessor and this header is deliberately
    // macro-free. That trade is only safe while something checks the two agree.
    const std::string spelled = std::to_string(an::kEngineVersionMajor) + '.' +
                                std::to_string(an::kEngineVersionMinor) + '.' + std::to_string(an::kEngineVersionPatch);
    EXPECT_EQ(spelled, an::kEngineVersionString);
    EXPECT_EQ(spelled, an::engineVersionString());
}

TEST(EngineVersionTest, TheBinaryReportsTheAbiGenerationItsHeadersDeclare)
{
    EXPECT_EQ(an::engineAbiGeneration(), an::kEngineAbiGeneration);
}

TEST(EngineVersionTest, HeadersAndBinaryAgreeInThisBuild)
{
    EXPECT_TRUE(an::engineMatchesHeaders());
    EXPECT_EQ(an::engineCompatibility(), an::EngineCompatibility::Compatible);
}

TEST(EngineVersionTest, AStaleAbiGenerationIsReportedBeforeAStaleVersion)
{
    an::EngineVersion staleVersion = an::engineHeaderVersion();
    ++staleVersion.patchVersion;

    EXPECT_EQ(an::engineCompatibility(an::engineHeaderVersion(), an::kEngineAbiGeneration - 1),
              an::EngineCompatibility::AbiMismatch);
    EXPECT_EQ(an::engineCompatibility(staleVersion, an::kEngineAbiGeneration - 1),
              an::EngineCompatibility::AbiMismatch);
}

TEST(EngineVersionTest, AStaleVersionWithTheSameLayoutIsReportedSeparately)
{
    an::EngineVersion staleVersion = an::engineHeaderVersion();
    ++staleVersion.patchVersion;

    EXPECT_EQ(an::engineCompatibility(staleVersion, an::kEngineAbiGeneration),
              an::EngineCompatibility::VersionMismatch);
}

TEST(EngineVersionTest, TheVersionStringHasStaticStorageDuration)
{
    // Same promise LibraryInfo makes: a string literal compiled into the library, so a caller may
    // hold the pointer indefinitely.
    EXPECT_EQ(an::engineVersionString(), an::engineVersionString());
}

TEST(EngineVersionTest, EqualityComparesEveryComponent)
{
    constexpr an::EngineVersion reference{1, 2, 3};
    EXPECT_EQ(reference, (an::EngineVersion{1, 2, 3}));
    EXPECT_NE(reference, (an::EngineVersion{9, 2, 3}));
    EXPECT_NE(reference, (an::EngineVersion{1, 9, 3}));
    EXPECT_NE(reference, (an::EngineVersion{1, 2, 9}));
}

} // namespace
