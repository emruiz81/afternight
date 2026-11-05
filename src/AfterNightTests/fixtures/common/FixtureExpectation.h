// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// Reference expectations: a result recorded for a neutral input, with what produced it and how a result is compared
// with it.
//
// A test that compares the engine's answer with the engine's own answer proves nothing, so the expected result of an
// operation is a text record, checked in: the result for a neutral input, what produced it and how a candidate is to
// be compared with it. A fixture executable reads the record and holds its own answer to it; a capture run writes
// fresh records to be reviewed and checked in.
//
// A record states its input by name and by two digests - the description's canonical text and its samples - so a
// comparison against an input that has since changed is refused rather than passed. It states one of three
// classifications: `exact` (integers equal, floats bit-identical, any NaN matching any NaN), `tolerance` (an
// absolute plus a relative bound per sample) or `accepted` (the difference is expected, and the record says why;
// the values are still compared and reported, and never fail). The result samples are written out in full, one
// row per line, so a record reads back exactly and a change to one shows in a diff.

#include "FixtureImage.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::fixtures
{

/// How a captured result is compared.
enum class ExpectationClass : std::uint8_t
{
    Exact,
    Tolerance,
    Accepted
};

[[nodiscard]] std::string_view expectationClassName(ExpectationClass classification) noexcept;
[[nodiscard]] std::optional<ExpectationClass> expectationClassFromName(std::string_view name) noexcept;

/// Where a captured result came from.
struct ExpectationProvenance
{
    /// The engine that produced it, by its library name, and that library's version and interface generation.
    std::string producer;
    std::string producerVersion;
    std::string producerAbi;
    /// `Release` or `Debug`, the compiler and its version, and the operating system and architecture.
    std::string configuration;
    std::string compiler;
    std::string platform;
    /// The capture date, `YYYY-MM-DD`.
    std::string captured;
    /// The commit the producer was built from, or `-` when the capture was not told.
    std::string commit = "-";
};

/// Where a result produced in this executable came from: the producer as the caller names it, and this build's
/// configuration, compiler and platform, dated today. The adapter states its engine through it.
[[nodiscard]] ExpectationProvenance provenanceOfThisBuild(std::string producer, std::string producerVersion,
                                                          std::string producerAbi, std::string commit = "-");

/// One captured result.
struct Expectation
{
    /// Unique within its file: `<operation>/<input name>`.
    std::string id;
    /// What was done to the input, in words.
    std::string operation;
    std::string inputName;
    /// `fixtureDigest` of the input's description and `FixtureSamples::digest` of its samples.
    std::string inputDescription;
    std::string inputSamples;
    ExpectationClass classification = ExpectationClass::Exact;
    /// Read by `ExpectationClass::Tolerance` only.
    double absoluteTolerance = 0.0;
    double relativeTolerance = 0.0;
    /// Why the difference is expected; required by `ExpectationClass::Accepted` and refused by the others.
    std::string acceptedReason;
    ExpectationProvenance provenance;
    FixtureSamples result;
};

/// The environment variable a capture run sets to the directory fresh records are written to, and the one naming
/// the commit the producer was built from.
inline constexpr const char* kCaptureDirectoryVariable = "AFTERNIGHT_FIXTURE_CAPTURE";
inline constexpr const char* kCaptureCommitVariable = "AFTERNIGHT_FIXTURE_CAPTURE_COMMIT";

/// A record of a result captured now. Its id is `<operationKey>/<input name>`, and the input is stated by its name
/// and both digests.
[[nodiscard]] Expectation makeExpectation(const std::string& operationKey, std::string operationText,
                                          const FixtureImage& input, const FixtureSamples& inputSamples,
                                          FixtureSamples result, ExpectationClass classification,
                                          ExpectationProvenance provenance);

/// A record's text, lines ending in LF; `writeExpectationFile` writes them with CRLF.
[[nodiscard]] std::string formatExpectation(const Expectation& expectation);

/// Every record of a text, in order. Empty, with the reason in `error`, when a record is incomplete, names an
/// unknown field or class, carries a tolerance or reason its class does not read, states a result whose rows are
/// missing or malformed, or whose samples do not match the digest the record states.
[[nodiscard]] std::optional<std::vector<Expectation>> parseExpectations(std::string_view text, std::string& error);

/// What comparing a candidate with a record found.
struct ExpectationComparison
{
    /// Whether the candidate satisfies the record's classification.
    bool matched = false;
    /// Why not, or what an accepted difference amounted to; empty for an exact or tolerated match.
    std::string reason;
    /// Samples that differ at all, and the largest absolute difference among finite pairs.
    std::uint64_t differingSamples = 0u;
    double largestDifference = 0.0;
};

/// Compares a candidate with a record's result by the record's classification. A different shape or
/// representation never matches, whatever the classification.
[[nodiscard]] ExpectationComparison compareWithExpectation(const Expectation& expectation,
                                                           const FixtureSamples& candidate);

/// Whether a record was captured from this input: its name and both digests.
[[nodiscard]] bool expectationDescribes(const Expectation& expectation, const FixtureImage& input,
                                        const FixtureSamples& inputSamples);

/// Reads every record of a file.
[[nodiscard]] std::optional<std::vector<Expectation>> readExpectationFile(const std::filesystem::path& path,
                                                                          std::string& error);

/// Writes records to a file with CRLF line endings, replacing it.
[[nodiscard]] bool writeExpectationFile(const std::filesystem::path& path, const std::vector<Expectation>& expectations,
                                        std::string& error);

/// The checked-in expectation records: `AFTERNIGHT_FIXTURE_EXPECTATIONS` when it is set, otherwise
/// `src/AfterNightTests/fixtures/common/expectations` found by walking up from the executable's directory and then
/// from the working directory. Empty, with where it looked in `error`, when neither finds it.
[[nodiscard]] std::optional<std::filesystem::path> expectationDirectory(std::string& error);

} // namespace an::tests::fixtures
