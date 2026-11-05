// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The record that holds the raw-dump exporter to its recorded exports: how two exports are compared, how the
// register of expected card differences is read and matched, and the record's own round trip.
//
// Each rule is held from both sides: a difference the register names passes and marks its entry used, and the
// same difference with the entry gone, or with an entry for another kind of change, fails.

#include "RawDumpExportRecord.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using namespace an::bench;

namespace
{

SampleDescriptor grey(std::uint32_t width, std::uint32_t height)
{
    SampleDescriptor descriptor;
    descriptor.width = width;
    descriptor.height = height;
    descriptor.channels = 1u;
    descriptor.sampleType = SampleType::UInt16;
    descriptor.layout = Layout::Planar;
    descriptor.byteOrder = ByteOrder::Little;
    return descriptor;
}

RawDumpExportOutcome outcome(std::vector<MetadataCard> cards = {})
{
    RawDumpExportOutcome made;
    made.name = "fits-case";
    made.exported = true;
    made.sourceFormat = "FITS";
    made.descriptor = grey(7u, 5u);
    made.payloadBytes = 70u;
    made.payloadDigest = std::string(64u, 'a');
    made.cards = std::move(cards);
    return made;
}

RawDumpCardDifference entry(const char* caseName, const char* key, RawDumpCardChange change)
{
    RawDumpCardDifference made;
    made.caseName = caseName;
    made.key = key;
    made.change = change;
    made.reason = "stated for the test";
    return made;
}

std::vector<std::string> compare(const RawDumpExportOutcome& recorded, const RawDumpExportOutcome& current,
                                 const std::vector<RawDumpCardDifference>& registered, std::vector<bool>& used)
{
    return compareRawDumpExports(recorded, current, registered, used);
}

std::filesystem::path scratchFile(const char* name)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (std::string("an-raw-dump-record-") + name + "-" + std::to_string(stamp) + ".json");
}

bool readRegister(const std::string& text, std::vector<RawDumpCardDifference>& out, std::string& error)
{
    const std::filesystem::path path = scratchFile("register");
    std::ofstream(path, std::ios::binary) << text;
    const bool read = readRawDumpCardDifferences(path, out, error);
    std::filesystem::remove(path);
    return read;
}

} // namespace

TEST(RawDumpExportRecordTest, ACardValueIsReadAsFitsReadsIt)
{
    EXPECT_TRUE(sameCardValue("'M42     '", "M42"));
    EXPECT_TRUE(sameCardValue("'M42'", "'M42   '"));
    EXPECT_TRUE(sameCardValue("'it''s'", "it's"));
    EXPECT_TRUE(sameCardValue("300.0", "300."));
    EXPECT_TRUE(sameCardValue("300.0", "3E2"));
    EXPECT_TRUE(sameCardValue("3D2", "300"));
    EXPECT_TRUE(sameCardValue("+1", "1.0"));
    EXPECT_TRUE(sameCardValue(" T ", "T"));

    EXPECT_FALSE(sameCardValue("300.0", "300.5"));
    EXPECT_FALSE(sameCardValue("T", "F"));
    EXPECT_FALSE(sameCardValue("'T'", "T")) << "a quoted string is not a logical";
    EXPECT_FALSE(sameCardValue("'1'", "1")) << "a quoted string is not a number";
    EXPECT_FALSE(sameCardValue("'  M42'", "M42")) << "leading blanks in a string are significant";
}

TEST(RawDumpExportRecordTest, TwoEqualExportsAgree)
{
    std::vector<bool> used;
    EXPECT_TRUE(
        compare(outcome({{"OBJECT", "'M42'", "target"}}), outcome({{"OBJECT", "M42", " target "}}), {}, used).empty());
}

TEST(RawDumpExportRecordTest, TheDescriptorPayloadAndFormatMustBeEqual)
{
    std::vector<bool> used;
    RawDumpExportOutcome current = outcome();
    current.descriptor = grey(5u, 7u);
    EXPECT_EQ(compare(outcome(), current, {}, used).size(), 1u);

    current = outcome();
    current.payloadDigest = std::string(64u, 'b');
    EXPECT_EQ(compare(outcome(), current, {}, used).size(), 1u);

    current = outcome();
    current.payloadBytes = 72u;
    EXPECT_EQ(compare(outcome(), current, {}, used).size(), 1u);

    current = outcome();
    current.sourceFormat = "XISF";
    EXPECT_EQ(compare(outcome(), current, {}, used).size(), 1u);
}

TEST(RawDumpExportRecordTest, AFileOneExporterRefusedTheOtherMustRefuse)
{
    std::vector<bool> used;
    RawDumpExportOutcome refused;
    refused.name = "fits-case";
    EXPECT_EQ(compare(outcome(), refused, {}, used).size(), 1u);
    EXPECT_EQ(compare(refused, outcome(), {}, used).size(), 1u);
    EXPECT_TRUE(compare(refused, refused, {}, used).empty());
}

TEST(RawDumpExportRecordTest, ACardDifferenceFailsUnlessTheRegisterNamesIt)
{
    const RawDumpExportOutcome recorded = outcome({{"BZERO", "0", ""}, {"NAXIS3", "1", ""}});
    const RawDumpExportOutcome current = outcome({{"BZERO", "32768", ""}});

    std::vector<bool> used;
    EXPECT_EQ(compare(recorded, current, {}, used).size(), 2u);

    RawDumpCardDifference offset = entry("*", "BZERO", RawDumpCardChange::Value);
    offset.recorded = "0";
    offset.current = "32768";
    const RawDumpCardDifference axis = entry("fits-case", "NAXIS3", RawDumpCardChange::OnlyRecorded);
    EXPECT_TRUE(compare(recorded, current, {offset, axis}, used).empty());
    EXPECT_EQ(used, (std::vector<bool>{true, true}));

    // An entry for another case, another kind of change or other values explains nothing.
    used.clear();
    EXPECT_EQ(compare(recorded, current, {offset, entry("other-case", "NAXIS3", RawDumpCardChange::OnlyRecorded)}, used)
                  .size(),
              1u);
    EXPECT_EQ(compare(recorded, current, {offset, entry("*", "NAXIS3", RawDumpCardChange::OnlyCurrent)}, used).size(),
              1u);
    RawDumpCardDifference otherValues = offset;
    otherValues.current = "2147483648";
    used.clear();
    EXPECT_EQ(compare(recorded, current, {otherValues, axis}, used).size(), 1u);
    EXPECT_EQ(used, (std::vector<bool>{false, true}));
}

TEST(RawDumpExportRecordTest, AKeyPrefixNamesEveryKeyItBegins)
{
    const RawDumpExportOutcome recorded = outcome();
    const RawDumpExportOutcome current = outcome(
        {{"Exif.Image.Software", "AfterNight", ""}, {"Exif.Image.DateTime", "2026:10:15", ""}, {"Other", "1", ""}});
    std::vector<bool> used;
    const std::vector<std::string> problems =
        compare(recorded, current, {entry("*", "Exif.*", RawDumpCardChange::OnlyCurrent)}, used);
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems.front().find("Other"), std::string::npos);
    EXPECT_TRUE(used.front());
}

TEST(RawDumpExportRecordTest, ACommentDifferenceIsItsOwnKind)
{
    const RawDumpExportOutcome recorded = outcome({{"COMMENT", "", ""}});
    const RawDumpExportOutcome current = outcome({{"COMMENT", "", "  FITS format"}});
    std::vector<bool> used;
    EXPECT_EQ(compare(recorded, current, {entry("*", "COMMENT", RawDumpCardChange::Value)}, used).size(), 1u);
    EXPECT_TRUE(compare(recorded, current, {entry("*", "COMMENT", RawDumpCardChange::Comment)}, used).empty());
}

TEST(RawDumpExportRecordTest, RepeatedKeysAreMatchedInOrder)
{
    const RawDumpExportOutcome recorded = outcome({{"HISTORY", "", "first"}, {"HISTORY", "", "second"}});
    std::vector<bool> used;
    EXPECT_TRUE(compare(recorded, outcome({{"HISTORY", "", "first"}, {"HISTORY", "", "second"}}), {}, used).empty());
    EXPECT_EQ(compare(recorded, outcome({{"HISTORY", "", "second"}, {"HISTORY", "", "first"}}), {}, used).size(), 2u);
    EXPECT_EQ(compare(recorded, outcome({{"HISTORY", "", "first"}}), {}, used).size(), 1u);
}

TEST(RawDumpExportRecordTest, TheRegisterRefusesAnEntryThatCannotNameADifference)
{
    std::vector<RawDumpCardDifference> read;
    std::string error;
    const std::string head = "{\"record\": \"raw-dump-export-differences\", \"differences\": [";
    EXPECT_TRUE(readRegister(head + "{\"case\": \"*\", \"key\": \"BZERO\", \"change\": \"value\", \"recorded\": \"0\", "
                                    "\"current\": \"32768\", \"reason\": \"why\"}]}",
                             read, error))
        << error;
    ASSERT_EQ(read.size(), 1u);
    EXPECT_EQ(read.front().change, RawDumpCardChange::Value);
    EXPECT_EQ(read.front().recorded, std::optional<std::string>("0"));

    for (const char* refused : {
             "{\"case\": \"*\", \"key\": \"BZERO\", \"change\": \"value\", \"reason\": \"\"}",
             "{\"case\": \"*\", \"key\": \"BZERO\", \"change\": \"moved\", \"reason\": \"why\"}",
             "{\"case\": \"*\", \"key\": \"BZERO\", \"change\": \"only-recorded\", \"recorded\": \"0\", \"reason\": "
             "\"why\"}",
             "{\"case\": \"*\", \"key\": \"BZERO\", \"change\": \"value\", \"recorded\": \"0\", \"current\": \"0\", "
             "\"reason\": \"why\"}",
             "{\"case\": \"\", \"key\": \"BZERO\", \"change\": \"value\", \"reason\": \"why\"}",
         })
    {
        EXPECT_FALSE(readRegister(head + refused + "]}", read, error)) << refused;
    }
    EXPECT_FALSE(readRegister("{\"record\": \"something-else\", \"differences\": []}", read, error));
}

TEST(RawDumpExportRecordTest, TheRecordRoundTripsWithCrlfEndings)
{
    RawDumpExportRecord record;
    record.producer = "producer 1";
    record.commit = std::string(40u, 'c');
    record.captured = "2026-10-15";
    record.outcomes.push_back(outcome({{"OBJECT", "'M42'", "target"}}));
    RawDumpExportOutcome refused;
    refused.name = "refused-case";
    record.outcomes.push_back(refused);

    const std::filesystem::path path = scratchFile("round-trip");
    std::string error;
    ASSERT_TRUE(writeRawDumpExportRecord(path, record, error)) << error;
    std::ifstream file(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    EXPECT_NE(text.find("\r\n"), std::string::npos);
    EXPECT_EQ(text.find("\n"), text.find("\r\n") + 1u) << "every line ends CRLF";

    RawDumpExportRecord reread;
    ASSERT_TRUE(readRawDumpExportRecord(path, reread, error)) << error;
    std::filesystem::remove(path);
    EXPECT_EQ(reread.producer, record.producer);
    EXPECT_EQ(reread.commit, record.commit);
    ASSERT_EQ(reread.outcomes.size(), 2u);
    std::vector<bool> used;
    EXPECT_TRUE(compare(record.outcomes[0], reread.outcomes[0], {}, used).empty());
    EXPECT_EQ(reread.outcomes[0].cards, record.outcomes[0].cards);
    EXPECT_FALSE(reread.outcomes[1].exported);
    ASSERT_NE(reread.find("refused-case"), nullptr);
    EXPECT_EQ(reread.find("missing-case"), nullptr);
}

TEST(RawDumpExportRecordTest, EveryCaseHasItsOwnNameAndFileAndEveryContainerIsCovered)
{
    std::set<std::string> names;
    std::set<std::string> files;
    std::set<std::string> extensions;
    for (const RawDumpExportCase& exportCase : rawDumpExportCases())
    {
        EXPECT_TRUE(names.insert(exportCase.name).second) << exportCase.name;
        EXPECT_TRUE(files.insert(exportCase.file).second) << exportCase.file;
        extensions.insert(std::filesystem::path(exportCase.file).extension().string());
        EXPECT_TRUE(exportCase.descriptor.isValid()) << exportCase.name;
    }
    for (const char* extension : {".fits", ".fit", ".fts", ".xisf", ".tif", ".tiff", ".png", ".jpg", ".jpeg"})
    {
        EXPECT_TRUE(extensions.count(extension) == 1u) << extension;
    }
}
