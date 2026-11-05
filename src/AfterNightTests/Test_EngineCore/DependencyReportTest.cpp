// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Core/DependencyReport.h.
//
// What this proves is a property of the binary rather than of an algorithm: that the engine can
// say which third-party libraries are inside it and at which versions, that the answer is the same
// every time it is asked and from every thread that asks, and that a version it read out of a
// loaded library is a version rather than the paragraph that library prints.
//
// The last of those is the reason this file exists at all. libtiff's run-time version is a
// multi-line copyright banner and LibRaw's a hyphenated build string; reading either with a
// process-wide `std::regex` is fragile, and the property such a reading reaches for is asserted
// here instead.

#include <AfterNightEngine/Core/DependencyReport.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <set>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

/// The report, in a span sized from the engine's own count.
std::vector<an::DependencyVersion> report()
{
    std::vector<an::DependencyVersion> entries(an::engineDependencyCount());
    const an::Status filled = an::engineDependencies(entries);
    EXPECT_TRUE(filled.ok()) << filled.message();
    return entries;
}

/// True when `text` is terminated inside its own capacity, which is what makes reading it safe.
bool isTerminated(const an::DependencyVersionText& text)
{
    return std::memchr(text.text, '\0', an::DependencyVersionText::kCapacity) != nullptr;
}

/// What one dependency is required to report.
struct Expectation
{
    /// The register's name for it.
    const char* name;

    /// Whether its headers declare a version this engine can read.
    bool compiled;

    /// Whether the loaded library reports a version this engine can read.
    bool loaded;
};

/// Every library this build reports, and which of the two versions each one has.
///
/// Both directions of every row are asserted, and that is what earns the table. A contributor that
/// stopped reporting - a renamed macro, a header that stopped being included, a library swapped for
/// one that answers differently - would otherwise look exactly like a library that legitimately
/// publishes no version, because the report says both with an empty text. A row that changes here
/// is a decision somebody made rather than a number that drifted, and `dependencies.txt` records
/// the same two facts for a reader.
constexpr std::array<Expectation, 13u> kExpected{{
    {"cfitsio", true, true},
    {"exiv2", true, true},
    {"ffmpeg", true, true},
    // FFTW publishes no version macro at all: its headers declare the transforms and nothing about
    // the release they came from, so the loaded library is the only thing that can be asked.
    {"fftw3", false, true},
    {"gsl", true, true},
    {"highway", true, false},
    {"libjpeg-turbo", true, false},
    {"libpng", true, true},
    {"libraw", true, true},
    {"libtiff", true, true},
    {"nlohmann-json", true, false},
    {"onetbb", true, true},
    // The XISF library publishes both: its version header and the loaded copy's own numbers.
    {"openxisf", true, true},
}};

TEST(DependencyReportTest, ReportsExactlyTheLibrariesThisBuildLinks)
{
    EXPECT_EQ(an::engineDependencyCount(), kExpected.size());
}

TEST(DependencyReportTest, TheCountIsTheSameEveryTimeItIsAsked)
{
    const std::size_t first = an::engineDependencyCount();
    EXPECT_EQ(an::engineDependencyCount(), first);
    EXPECT_EQ(an::engineDependencyCount(), first);
}

TEST(DependencyReportTest, NamesEveryLibraryOnceAndNoOther)
{
    const std::vector<an::DependencyVersion> entries = report();
    std::set<std::string_view> seen;
    for (const an::DependencyVersion& entry : entries)
    {
        ASSERT_NE(entry.name, nullptr);
        EXPECT_TRUE(seen.insert(entry.name).second) << entry.name << " is reported twice";
    }
    std::set<std::string_view> expected;
    for (const Expectation& row : kExpected)
    {
        expected.insert(row.name);
    }
    EXPECT_EQ(seen, expected);
}

TEST(DependencyReportTest, EveryEntryCarriesTheVersionsItsLibraryPublishes)
{
    const std::vector<an::DependencyVersion> entries = report();
    for (const Expectation& row : kExpected)
    {
        const auto found = std::find_if(entries.begin(), entries.end(), [&row](const an::DependencyVersion& entry) {
            return entry.name != nullptr && std::string_view(entry.name) == row.name;
        });
        ASSERT_NE(found, entries.end()) << "the report does not name " << row.name;
        ASSERT_TRUE(isTerminated(found->compiled)) << row.name;
        ASSERT_TRUE(isTerminated(found->loaded)) << row.name;
        EXPECT_EQ(std::strlen(found->compiled.text) > 0u, row.compiled)
            << row.name << " compiled version is \"" << found->compiled.text << '"';
        EXPECT_EQ(std::strlen(found->loaded.text) > 0u, row.loaded)
            << row.name << " loaded version is \"" << found->loaded.text << '"';
    }
}

TEST(DependencyReportTest, TheReportIsOrderedByName)
{
    const std::vector<an::DependencyVersion> entries = report();
    ASSERT_FALSE(entries.empty());
    for (std::size_t index = 1u; index < entries.size(); ++index)
    {
        EXPECT_LT(std::string_view(entries[index - 1u].name), std::string_view(entries[index].name));
    }
}

TEST(DependencyReportTest, EveryVersionTextIsAVersionRatherThanAParagraph)
{
    // This is the assertion the two removed regular expressions were standing in for. A version
    // read from a loaded library is what the engine hands a reader, so a banner, a build suffix or
    // an embedded newline reaching this far is a defect rather than a cosmetic difference.
    const std::vector<an::DependencyVersion> entries = report();
    for (const an::DependencyVersion& entry : entries)
    {
        for (const an::DependencyVersionText& text : {entry.compiled, entry.loaded})
        {
            const std::string_view value(text.text, std::strlen(text.text));
            for (const char character : value)
            {
                const bool allowed = (character >= '0' && character <= '9') || (character >= 'a' && character <= 'z') ||
                                     (character >= 'A' && character <= 'Z') || character == '.' || character == '-' ||
                                     character == '+';
                EXPECT_TRUE(allowed) << entry.name << " reports " << value;
            }
        }
    }
}

TEST(DependencyReportTest, EveryLinkageIsOneThisBuildCanName)
{
    const std::vector<an::DependencyVersion> entries = report();
    for (const an::DependencyVersion& entry : entries)
    {
        const char* name = an::dependencyLinkageName(entry.linkage);
        ASSERT_NE(name, nullptr);
        EXPECT_GT(std::strlen(name), 0u) << entry.name;
    }
}

TEST(DependencyReportTest, AHeaderOnlyLibraryReportsNoLoadedVersion)
{
    // The distinction is the point of reporting two versions rather than one: a header-only library
    // has no loaded copy that could differ from the headers, and a shared library has one that can.
    const std::vector<an::DependencyVersion> entries = report();
    for (const an::DependencyVersion& entry : entries)
    {
        if (entry.linkage != an::DependencyLinkage::HeaderOnly)
        {
            continue;
        }
        EXPECT_EQ(std::strlen(entry.loaded.text), 0u) << entry.name << " is header-only and reports a loaded version";
    }
}

TEST(DependencyReportTest, EveryLoadedLibraryIsTheReleaseItsHeadersDescribe)
{
    // A library that publishes both versions can be asked whether the copy the loader found is the
    // one this engine was compiled against. They differ when an older or a foreign copy sits where the
    // loader looks first - a deployment step that did not run, a directory earlier on the search path -
    // and a release taken for its security fixes is exactly what such a copy quietly takes back. The
    // provenance register pins the version and the host's package database is held to it; this is the
    // third link, the binary that actually runs.
    const std::vector<an::DependencyVersion> entries = report();
    std::size_t compared = 0u;
    for (const an::DependencyVersion& entry : entries)
    {
        if (entry.compiled.text[0] == '\0' || entry.loaded.text[0] == '\0')
        {
            continue;
        }
        ++compared;
        EXPECT_STREQ(entry.loaded.text, entry.compiled.text)
            << entry.name << " was compiled against " << entry.compiled.text << " and the loaded copy is "
            << entry.loaded.text;
    }
    const auto both = static_cast<std::size_t>(std::count_if(
        kExpected.begin(), kExpected.end(), [](const Expectation& row) { return row.compiled && row.loaded; }));
    EXPECT_EQ(compared, both) << "a library that publishes both versions reported only one of them";
}

TEST(DependencyReportTest, RefusesASpanThatIsNotExactlyTheReport)
{
    std::vector<an::DependencyVersion> tooSmall(an::engineDependencyCount() - 1u);
    const an::Status refusedShort = an::engineDependencies(tooSmall);
    EXPECT_TRUE(refusedShort.failed());
    EXPECT_EQ(refusedShort.code(), an::ErrorCode::IncompatibleArguments);

    std::vector<an::DependencyVersion> tooLarge(an::engineDependencyCount() + 1u);
    const an::Status refusedLong = an::engineDependencies(tooLarge);
    EXPECT_TRUE(refusedLong.failed());
    EXPECT_EQ(refusedLong.code(), an::ErrorCode::IncompatibleArguments);
}

TEST(DependencyReportTest, WritesNothingIntoARefusedSpan)
{
    std::vector<an::DependencyVersion> tooSmall(an::engineDependencyCount() - 1u);
    const an::Status refused = an::engineDependencies(tooSmall);
    ASSERT_TRUE(refused.failed());
    for (const an::DependencyVersion& entry : tooSmall)
    {
        EXPECT_EQ(entry.name, nullptr);
        EXPECT_EQ(std::strlen(entry.compiled.text), 0u);
    }
}

TEST(DependencyReportTest, RefusesAnEmptySpan)
{
    const an::Status refused = an::engineDependencies(std::span<an::DependencyVersion>{});
    EXPECT_TRUE(refused.failed());
}

TEST(DependencyReportTest, EveryThreadThatAsksGetsTheSameReport)
{
    // The report is assembled from constants and from accessors of already-loaded libraries, so
    // there is nothing for two callers to race over - which is a claim worth an assertion, because
    // a version parsing built on a process-wide regular expression object would.
    const std::vector<an::DependencyVersion> expected = report();

    constexpr std::size_t kThreads = 8u;
    std::array<std::vector<an::DependencyVersion>, kThreads> answers;
    std::array<an::Status, kThreads> statuses;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (std::size_t index = 0u; index < kThreads; ++index)
    {
        workers.emplace_back([&answers, &statuses, index]() {
            answers[index].resize(an::engineDependencyCount());
            statuses[index] = an::engineDependencies(answers[index]);
        });
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }

    for (std::size_t index = 0u; index < kThreads; ++index)
    {
        ASSERT_TRUE(statuses[index].ok()) << statuses[index].message();
        ASSERT_EQ(answers[index].size(), expected.size());
        for (std::size_t entry = 0u; entry < expected.size(); ++entry)
        {
            EXPECT_STREQ(answers[index][entry].name, expected[entry].name);
            EXPECT_STREQ(answers[index][entry].compiled.text, expected[entry].compiled.text);
            EXPECT_STREQ(answers[index][entry].loaded.text, expected[entry].loaded.text);
            EXPECT_EQ(answers[index][entry].linkage, expected[entry].linkage);
        }
    }
}

} // namespace
