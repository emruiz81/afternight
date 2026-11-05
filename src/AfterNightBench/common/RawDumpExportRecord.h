// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the record that holds the raw-dump exporter to its recorded exports.
//
// What the exporter writes is held, file for file, to a record: a set of small input files in every
// container the engine reads, checked in; the recorded export of each, captured once and checked in
// beside them; and a register of every card the exporter's export carries differently from the record,
// each with its reason.
//
// The comparison is by meaning. A descriptor, a payload's digest and size and the container's name must be
// equal, and a file the record refused the exporter must refuse. A card is matched to the card of the same
// key and occurrence, its value read as FITS reads one - a quoted string without its quotes and trailing
// blanks, a number by its value, a logical by its letter - and its comment without surrounding blanks;
// every card that still differs must be named in the register, and every entry of the register must still
// name a difference, so it cannot outlive its reason.
//
// Nothing here names an engine: `Test_EngineFixtureBridge` reads the record and compares the exporter
// with it.

#pragma once

#include "Descriptors.h"
#include "RawDump.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an::bench
{

/// The record of the exports, under the expectation directory.
inline constexpr std::string_view kRawDumpExportRecordFile = "raw-dump/recorded-exports.json";

/// The register of every card the exporter's export carries differently, under the expectation directory.
inline constexpr std::string_view kRawDumpExportDifferencesFile = "raw-dump/export-differences.json";

/// The directory of the input files, under the expectation directory.
inline constexpr std::string_view kRawDumpExportInputDirectory = "raw-dump/inputs";

/// How one input file is made.
enum class RawDumpExportInput : std::uint8_t
{
    Image, ///< An image the capture writes through a codec.
    NotAnImage, ///< Text under an image's extension: nothing decodes it.
    Truncated, ///< An image's file cut short inside its samples.
    UnknownExtension ///< An image's file under an extension no codec claims.
};

/// One input file of the record.
struct RawDumpExportCase
{
    /// The record's key.
    std::string name;
    /// The file's name under the input directory.
    std::string file;
    /// How the capture makes the file.
    RawDumpExportInput input = RawDumpExportInput::Image;
    /// The image an `Image` case is written from, and the image the other kinds start from.
    SampleDescriptor descriptor;
    /// The synthetic generator that fills it.
    std::string generator;
    /// Whether the capture states the cards a capture program writes.
    bool cards = false;
};

/// Every input, in the record's order.
[[nodiscard]] const std::vector<RawDumpExportCase>& rawDumpExportCases();

/// What one exporter answered for one input.
struct RawDumpExportOutcome
{
    std::string name;
    /// Whether the pair was written. A refusal states nothing else.
    bool exported = false;
    std::string sourceFormat;
    SampleDescriptor descriptor;
    std::uint64_t payloadBytes = 0;
    std::string payloadDigest;
    std::vector<MetadataCard> cards;
};

/// The outcome a written dump states.
[[nodiscard]] RawDumpExportOutcome exportedOutcome(const std::string& name, const RawDump& dump);

/// The record: who captured it, and every outcome.
struct RawDumpExportRecord
{
    std::string producer;
    std::string commit;
    std::string captured;
    std::vector<RawDumpExportOutcome> outcomes;

    /// The outcome named `name`, or nullptr.
    [[nodiscard]] const RawDumpExportOutcome* find(const std::string& name) const;
};

[[nodiscard]] bool readRawDumpExportRecord(const std::filesystem::path& path, RawDumpExportRecord& out,
                                           std::string& error);
[[nodiscard]] bool writeRawDumpExportRecord(const std::filesystem::path& path, const RawDumpExportRecord& record,
                                            std::string& error);

/// How a card of one export differs from the same card of the other.
enum class RawDumpCardChange : std::uint8_t
{
    OnlyRecorded, ///< The record holds the card and this exporter wrote no such card.
    OnlyCurrent, ///< This exporter wrote the card and the record holds no such card.
    Value, ///< Both wrote it, with values that mean different things.
    Comment ///< Both wrote it with one meaning, and different comments.
};

/// A kind of card difference the exporter's export is expected to have, and why.
///
/// `caseName` is one case or `*` for every case that has the difference; `key` is one key or a prefix ending
/// in `*`. A `Value` entry may state the value each exporter wrote; one it leaves out matches any.
struct RawDumpCardDifference
{
    std::string caseName;
    std::string key;
    RawDumpCardChange change = RawDumpCardChange::Value;
    std::optional<std::string> recorded;
    std::optional<std::string> current;
    std::string reason;
};

[[nodiscard]] bool readRawDumpCardDifferences(const std::filesystem::path& path,
                                              std::vector<RawDumpCardDifference>& out, std::string& error);

/// Whether two cards' values mean the same thing, read as FITS reads a value.
[[nodiscard]] bool sameCardValue(const std::string& left, const std::string& right);

/// Every way `current` differs from `recorded` that the register does not name.
///
/// @param recorded the recorded outcome.
/// @param current the exporter's outcome for the same input.
/// @param registered the register.
/// @param used one flag per register entry, set for each entry that named a difference here.
/// @return one sentence per unexplained difference; empty when the two agree.
[[nodiscard]] std::vector<std::string> compareRawDumpExports(const RawDumpExportOutcome& recorded,
                                                             const RawDumpExportOutcome& current,
                                                             const std::vector<RawDumpCardDifference>& registered,
                                                             std::vector<bool>& used);

/// The checked-in expectation directory: `AFTERNIGHT_FIXTURE_EXPECTATIONS`, or the tree's own found by
/// walking up from the executable and from the working directory.
[[nodiscard]] std::optional<std::filesystem::path> locateExpectationDirectory(std::string& error);

} // namespace an::bench
