// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for reading a FITS header's cards without decoding an image.
//
// The reader stands beside the FITS codec, not in front of it: a solver's `.wcs` output is a header with
// no image, which the codec does not open, and its cards must come out exactly as the codec would have
// handed them over had the same header carried an image. So the first case writes a header of every card
// shape the codec meets - strings padded inside their quotes, doubled quotes, a slash inside a string, a
// long string continued over two records, an ESO hierarchical keyword, commentary with leading blanks, an
// undefined value, a complex value, a value indicator outside column nine - in front of an image, and
// holds the reader's cards to the codec's, card for card. The rest pin each rule the header states.

#include <AfterNightEngine/IO/FitsHeaderCards.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

constexpr std::size_t kCardBytes = 80u;
constexpr std::size_t kBlockBytes = 2880u;

/// A record exactly as written, padded with blanks to eighty bytes.
[[nodiscard]] std::string record(std::string_view text)
{
    std::string padded(text);
    padded.resize(kCardBytes, ' ');
    return padded;
}

/// A value record in the fixed format: keyword in columns 1-8, `= `, the value, and ` / ` comment.
[[nodiscard]] std::string valueRecord(std::string_view keyword, std::string_view value, std::string_view comment = {})
{
    std::string text(keyword);
    text.resize(8u, ' ');
    text += "= ";
    if (!value.empty() && value.front() == '\'')
        text += value;
    else
        text += std::string(value.size() < 20u ? 20u - value.size() : 0u, ' ') + std::string(value);
    if (!comment.empty())
        text += " / " + std::string(comment);
    return record(text);
}

/// The records of a header, each eighty bytes, in order.
[[nodiscard]] std::vector<std::string> wcsHeaderRecords()
{
    return {valueRecord("SIMPLE", "T", "file does conform to FITS standard"),
            valueRecord("BITPIX", "8", "number of bits per data pixel"),
            valueRecord("NAXIS", "0", "number of data axes"),
            valueRecord("EXTEND", "T", "FITS dataset may contain extensions"),
            record("COMMENT   Original key: \"END\""),
            valueRecord("WCSAXES", "2", "no comment"),
            valueRecord("CTYPE1", "'RA---TAN-SIP'", "TAN (gnomic) projection + SIP distortions"),
            valueRecord("CTYPE2", "'DEC--TAN-SIP'", "TAN (gnomic) projection + SIP distortions"),
            valueRecord("EQUINOX", "2000.0", "Equatorial coordinates definition (yr)"),
            valueRecord("LONPOLE", "180.0", "no comment"),
            valueRecord("LATPOLE", "0.0", "no comment"),
            valueRecord("CRVAL1", "83.8220830000", "RA  of reference point"),
            valueRecord("CRVAL2", "-5.39111100000", "DEC of reference point"),
            valueRecord("CRPIX1", "1024.5", "X reference pixel"),
            valueRecord("CRPIX2", "768.5", "Y reference pixel"),
            valueRecord("CUNIT1", "'deg     '", "X pixel scale units"),
            valueRecord("CUNIT2", "'deg     '", "Y pixel scale units"),
            valueRecord("CD1_1", "-6.11111111E-04", "Transformation matrix"),
            valueRecord("CD1_2", "1.20000000E-06", "no comment"),
            valueRecord("CD2_1", "1.10000000E-06", "no comment"),
            valueRecord("CD2_2", "6.11111111E-04", "no comment"),
            valueRecord("IMAGEW", "2048", "Image width,  in pixels."),
            valueRecord("IMAGEH", "1536", "Image height, in pixels."),
            valueRecord("A_ORDER", "2", "Polynomial order, axis 1"),
            valueRecord("A_0_2", "2.5077E-07", "no comment"),
            valueRecord("A_1_1", "-1.1857E-06", "no comment"),
            valueRecord("A_2_0", "8.9146E-07", "no comment"),
            valueRecord("B_ORDER", "2", "Polynomial order, axis 2"),
            valueRecord("B_0_2", "-1.3437E-06", "no comment"),
            valueRecord("B_1_1", "6.3402E-07", "no comment"),
            valueRecord("B_2_0", "3.2088E-07", "no comment"),
            record("HISTORY Created by the Astrometry.net suite."),
            record("END")};
}

/// The records of a header in front of an image, exercising every card shape the codec meets.
[[nodiscard]] std::vector<std::string> everyShapeRecords()
{
    return {valueRecord("SIMPLE", "T", "conforms to FITS standard"),
            valueRecord("BITPIX", "16"),
            valueRecord("NAXIS", "2"),
            valueRecord("NAXIS1", "8"),
            valueRecord("NAXIS2", "4"),
            valueRecord("OBJECT", "'M 31    '", "padded inside its quotes"),
            valueRecord("OBSERVER", "'O''Brien'", "a doubled quote"),
            valueRecord("NOTE", "'a/b/c'", "a slash inside a string"),
            valueRecord("EMPTY", "''", "an empty string"),
            valueRecord("LEADING", "'   indented'"),
            valueRecord("LONGSTR", "'This is a long string value that continues&'", "first part"),
            record("CONTINUE  'on the next record&'"),
            record("CONTINUE  'and ends here'"),
            record("HIERARCH ESO DET DIT = 1.5 / exposure"),
            record("COMMENT   two leading blanks survive"),
            record("HISTORY plate solved"),
            record("         a record whose keyword is blank"),
            record("UNDEF   =                      / an undefined value"),
            record("SLASH   = / only a comment"),
            valueRecord("LOGICAL", "F"),
            valueRecord("CMPLX", "(1.5, -2.0)", "a complex value"),
            record("ODD     xyz= 12 / a value indicator outside column nine"),
            record("EXPTIME =                300.0 /seconds"),
            record("GAIN    =                  1.5 junk"),
            valueRecord("DATE-OBS", "'2024-01-15T22:13:05.500'"),
            record("NOVALUE no value indicator at all"),
            record("COMMENT"),
            record("END")};
}

/// The bytes of a header: its records end to end, padded with blanks to whole blocks.
[[nodiscard]] std::string blocksOf(const std::vector<std::string>& records)
{
    std::string header;
    for (const std::string& card : records)
        header += card;
    header.resize((header.size() + kBlockBytes - 1u) / kBlockBytes * kBlockBytes, ' ');
    return header;
}

/// The same records as text lines, each trimmed of its trailing blanks and ended by `ending`.
[[nodiscard]] std::string linesOf(const std::vector<std::string>& records, std::string_view ending)
{
    std::string text;
    for (const std::string& card : records)
    {
        std::string line = card;
        line.erase(line.find_last_not_of(' ') + 1u);
        text += line;
        text += ending;
    }
    return text;
}

/// The keyword, value and comment of a card, for a readable failure.
[[nodiscard]] std::string describe(const an::CodecCard& card)
{
    return "[" + card.keyword() + "] [" + card.value() + "] [" + card.comment() + "]";
}

/// The count a failure carries under a key, or the largest count when it carries none.
[[nodiscard]] std::uint64_t countOf(const an::Status& status, an::ContextKey key)
{
    const an::StatusContextField* const field = status.findContext(key);
    return field != nullptr && field->kind == an::ContextValueKind::Count ? field->count
                                                                          : std::numeric_limits<std::uint64_t>::max();
}

class FitsHeaderCardsTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_directory = std::filesystem::temp_directory_path() /
                      ("an-fits-header-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(m_directory);
        const an::Result<std::shared_ptr<const an::ImageCodec>> codec = an::createFitsCodec();
        ASSERT_TRUE(codec.hasValue()) << codec.status().message();
        const std::array codecs{*codec};
        an::Result<std::shared_ptr<const an::CodecRegistry>> registry = an::CodecRegistry::create(codecs);
        ASSERT_TRUE(registry.hasValue()) << registry.status().message();
        an::EngineContextOptions options;
        options.codecRegistry = std::move(*registry);
        an::Result<an::EngineContext> engine = an::EngineContext::create(options);
        ASSERT_TRUE(engine.hasValue()) << engine.status().message();
        m_engine = std::move(*engine);
    }

    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }

    /// Writes bytes to a file of the test's directory and returns its UTF-8 path.
    [[nodiscard]] std::string writeFile(std::string_view name, std::string_view bytes) const
    {
        const std::filesystem::path path = m_directory / name;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        const std::u8string utf8 = path.u8string();
        return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
    }

    /// An execution of the test's engine context.
    [[nodiscard]] an::ExecutionContext execution(std::stop_token stop = {}) const
    {
        an::ExecutionOptions options;
        options.stopToken = std::move(stop);
        an::Result<an::ExecutionContext> made = an::ExecutionContext::create(m_engine, options);
        EXPECT_TRUE(made.hasValue()) << made.status().message();
        return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
    }

    std::filesystem::path m_directory;
    an::EngineContext m_engine;
};

} // namespace

TEST_F(FitsHeaderCardsTest, ReadsEveryCardShapeExactlyAsTheFitsCodecDoes)
{
    const std::string header = blocksOf(everyShapeRecords());
    std::string data(static_cast<std::size_t>(8u * 4u * 2u), '\0');
    data.resize(kBlockBytes, '\0');
    const std::string path = writeFile("every-shape.fits", header + data);

    std::vector<an::CodecCard> fromCodec;
    {
        const an::ExecutionContext decodeExecution = execution();
        const an::Result<an::CodecDecodeResult> decoded =
            an::decodeImage(m_engine, decodeExecution, an::CodecReadRequest{.path = path});
        ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
        const std::span<const an::CodecCard> cards = decoded->metadata.codec().cards();
        fromCodec.assign(cards.begin(), cards.end());
    }

    const an::Result<std::vector<an::CodecCard>> fromBytes = an::parseFitsHeaderCards(header + data);
    ASSERT_TRUE(fromBytes.hasValue()) << fromBytes.status().message();
    ASSERT_EQ(fromBytes->size(), fromCodec.size());
    for (std::size_t index = 0u; index < fromCodec.size(); ++index)
    {
        EXPECT_EQ((*fromBytes)[index], fromCodec[index])
            << "card " << index << ": read " << describe((*fromBytes)[index]) << ", codec "
            << describe(fromCodec[index]);
    }

    // The file read stops at its bound, before the image's data, and reads the same cards.
    const an::Result<std::vector<an::CodecCard>> fromFile =
        an::readFitsHeaderCards(execution(), path, static_cast<std::uint64_t>(header.size()));
    ASSERT_TRUE(fromFile.hasValue()) << fromFile.status().message();
    EXPECT_EQ(*fromFile, *fromBytes);
}

TEST_F(FitsHeaderCardsTest, StatesEachCardAsTheHeaderRulesSay)
{
    const an::Result<std::vector<an::CodecCard>> parsed = an::parseFitsHeaderCards(blocksOf(everyShapeRecords()));
    ASSERT_TRUE(parsed.hasValue()) << parsed.status().message();
    const std::vector<std::pair<std::string, std::pair<std::string, std::string>>> expected{
        {"SIMPLE", {"T", "conforms to FITS standard"}},
        {"BITPIX", {"16", ""}},
        {"NAXIS", {"2", ""}},
        {"NAXIS1", {"8", ""}},
        {"NAXIS2", {"4", ""}},
        {"OBJECT", {"'M 31'", "padded inside its quotes"}},
        {"OBSERVER", {"'O''Brien'", "a doubled quote"}},
        {"NOTE", {"'a/b/c'", "a slash inside a string"}},
        {"EMPTY", {"''", "an empty string"}},
        {"LEADING", {"'   indented'", ""}},
        {"LONGSTR", {"'This is a long string value that continueson the next recordand ends here'", "first part"}},
        {"ESO DET DIT", {"1.5", "exposure"}},
        {"COMMENT", {"", "  two leading blanks survive"}},
        {"HISTORY", {"", "plate solved"}},
        {"UNDEF", {"", "an undefined value"}},
        {"SLASH", {"", "only a comment"}},
        {"LOGICAL", {"F", ""}},
        {"CMPLX", {"(1.5, -2.0)", "a complex value"}},
        {"ODD", {"12", "a value indicator outside column nine"}},
        {"EXPTIME", {"300.0", "seconds"}},
        {"GAIN", {"1.5", "junk"}},
        {"DATE-OBS", {"'2024-01-15T22:13:05.500'", ""}},
        {"NOVALUE", {"", "no value indicator at all"}},
        {"COMMENT", {"", ""}}};
    ASSERT_EQ(parsed->size(), expected.size());
    for (std::size_t index = 0u; index < expected.size(); ++index)
    {
        const an::CodecCard& card = (*parsed)[index];
        EXPECT_EQ(card.origin(), an::CodecNamespace::Fits) << index;
        EXPECT_EQ(card.keyword(), expected[index].first) << index;
        EXPECT_EQ(card.value(), expected[index].second.first) << index << " " << describe(card);
        EXPECT_EQ(card.comment(), expected[index].second.second) << index << " " << describe(card);
    }
}

TEST_F(FitsHeaderCardsTest, ReadsTheSameCardsFromLinesAsFromRecords)
{
    const std::vector<std::string> records = wcsHeaderRecords();
    const an::Result<std::vector<an::CodecCard>> fromRecords = an::parseFitsHeaderCards(blocksOf(records));
    ASSERT_TRUE(fromRecords.hasValue()) << fromRecords.status().message();

    for (const std::string_view ending : {std::string_view("\n"), std::string_view("\r\n")})
    {
        const an::Result<std::vector<an::CodecCard>> fromLines = an::parseFitsHeaderCards(linesOf(records, ending));
        ASSERT_TRUE(fromLines.hasValue()) << fromLines.status().message();
        EXPECT_EQ(*fromLines, *fromRecords);
    }

    // Blank lines are skipped, a line may keep blanks past column eighty, and nothing after END is read.
    std::string loose = "\n\n" + linesOf(records, "\r\n");
    loose.insert(loose.find("CTYPE1"), "   \r\n");
    loose.insert(loose.find("CRPIX1"), "CRPIX0  = 1" + std::string(100u, ' ') + "\n");
    loose += "garbage \x01 after the end\nmore\n";
    const an::Result<std::vector<an::CodecCard>> fromLoose = an::parseFitsHeaderCards(loose);
    ASSERT_TRUE(fromLoose.hasValue()) << fromLoose.status().message();
    ASSERT_EQ(fromLoose->size(), fromRecords->size() + 1u);
}

TEST_F(FitsHeaderCardsTest, HandsTheWcsBridgeTheCardsOfASolversHeader)
{
    an::Result<std::vector<an::CodecCard>> cards = an::parseFitsHeaderCards(blocksOf(wcsHeaderRecords()));
    ASSERT_TRUE(cards.hasValue()) << cards.status().message();

    // The solver writes an equinox and no reference frame, which the bridge reads as FK5 and refuses;
    // it solved against an ICRS catalogue, and the caller that ran it states so.
    EXPECT_EQ(an::checkWcsCards(*cards), an::WcsCardError::UnsupportedFrame);
    an::Result<an::CodecCard> frame = an::CodecCard::create(an::CodecNamespace::Fits, "RADESYS", "'ICRS'", "");
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();
    cards->push_back(std::move(*frame));
    ASSERT_EQ(an::checkWcsCards(*cards), an::WcsCardError::None);
    const an::Result<an::WcsSolution> solution = an::parseWcsCards(*cards, 2048u, 1536u);
    ASSERT_TRUE(solution.hasValue()) << solution.status().message();
    EXPECT_DOUBLE_EQ(solution->referencePixel().x, 1024.0);
    EXPECT_DOUBLE_EQ(solution->referencePixel().y, 768.0);
    EXPECT_DOUBLE_EQ(solution->referencePoint().rightAscensionDegrees, 83.822083);
    EXPECT_DOUBLE_EQ(solution->referencePoint().declinationDegrees, -5.391111);
    EXPECT_DOUBLE_EQ(solution->linear().cd11, -6.11111111E-04);
    EXPECT_TRUE(solution->distortion().hasForwardModel());
}

TEST_F(FitsHeaderCardsTest, StopsAtTheEndRecordWhateverFollowsIt)
{
    std::string bytes = blocksOf(wcsHeaderRecords());
    const an::Result<std::vector<an::CodecCard>> clean = an::parseFitsHeaderCards(bytes);
    ASSERT_TRUE(clean.hasValue()) << clean.status().message();

    // Binary data with line feeds and control bytes, and a trailing partial record, are never read.
    std::string data;
    for (int value = 0; value < 3000; ++value)
        data.push_back(static_cast<char>(value % 256));
    const an::Result<std::vector<an::CodecCard>> withData = an::parseFitsHeaderCards(bytes + data + "PARTIAL");
    ASSERT_TRUE(withData.hasValue()) << withData.status().message();
    EXPECT_EQ(*withData, *clean);

    // Unpadded records stop at END too.
    std::string unpadded;
    for (const std::string& card : wcsHeaderRecords())
        unpadded += card;
    const an::Result<std::vector<an::CodecCard>> fromUnpadded = an::parseFitsHeaderCards(unpadded);
    ASSERT_TRUE(fromUnpadded.hasValue()) << fromUnpadded.status().message();
    EXPECT_EQ(*fromUnpadded, *clean);
}

TEST_F(FitsHeaderCardsTest, RefusesWhatIsNotAWholeHeader)
{
    std::vector<std::string> records = wcsHeaderRecords();
    records.pop_back();

    // No END record: a truncated header, with the cards read before the fault counted.
    const an::Result<std::vector<an::CodecCard>> truncated = an::parseFitsHeaderCards(blocksOf(records));
    ASSERT_FALSE(truncated.hasValue());
    EXPECT_EQ(truncated.code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(countOf(truncated.status(), an::ContextKey::Completed), records.size());
    EXPECT_EQ(an::parseFitsHeaderCards(linesOf(records, "\n")).code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(an::parseFitsHeaderCards("").code(), an::ErrorCode::InvalidData);
    std::string partial;
    for (const std::string& card : records)
        partial += card;
    EXPECT_EQ(an::parseFitsHeaderCards(partial + "END").code(), an::ErrorCode::InvalidData);

    // A control byte inside a record, counted at the record that holds it.
    std::vector<std::string> control = wcsHeaderRecords();
    control[5][30] = '\t';
    const an::Result<std::vector<an::CodecCard>> tabbed = an::parseFitsHeaderCards(blocksOf(control));
    ASSERT_FALSE(tabbed.hasValue());
    EXPECT_EQ(tabbed.code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(countOf(tabbed.status(), an::ContextKey::Completed), 5u);
    control[5][30] = '\x7f';
    EXPECT_EQ(an::parseFitsHeaderCards(blocksOf(control)).code(), an::ErrorCode::InvalidData);

    // A line longer than a record, and a complex value that never closes.
    std::string longLine = linesOf(wcsHeaderRecords(), "\n");
    longLine.insert(0u, "COMMENT " + std::string(80u, 'x') + "\n");
    EXPECT_EQ(an::parseFitsHeaderCards(longLine).code(), an::ErrorCode::InvalidData);
    std::vector<std::string> complex = wcsHeaderRecords();
    complex.insert(complex.begin() + 1, valueRecord("BROKEN", "(1.5, 2.0"));
    EXPECT_EQ(an::parseFitsHeaderCards(blocksOf(complex)).code(), an::ErrorCode::InvalidData);

    // More cards than one image preserves.
    std::vector<std::string> crowded(an::kMaxCodecCards + 1u, valueRecord("FILLER", "1"));
    crowded.push_back(record("END"));
    const an::Result<std::vector<an::CodecCard>> refused = an::parseFitsHeaderCards(blocksOf(crowded));
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::LimitExceeded);
    std::vector<std::string> full(an::kMaxCodecCards, valueRecord("FILLER", "1"));
    full.push_back(record("END"));
    EXPECT_TRUE(an::parseFitsHeaderCards(blocksOf(full)).hasValue());
}

TEST_F(FitsHeaderCardsTest, ReadsAFileWithinItsBoundAndSaysWhyNot)
{
    const std::string header = blocksOf(wcsHeaderRecords());
    const std::string path = writeFile("solution.wcs", header);
    const an::Result<std::vector<an::CodecCard>> read = an::readFitsHeaderCards(execution(), path);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_EQ(*read, *an::parseFitsHeaderCards(header));

    // A file longer than the bound is read up to it; an END beyond the bound is refused.
    const std::string longer = writeFile("longer.wcs", header + std::string(10000u, ' '));
    EXPECT_TRUE(an::readFitsHeaderCards(execution(), longer, kBlockBytes * 2u).hasValue());
    const an::Result<std::vector<an::CodecCard>> beyond = an::readFitsHeaderCards(execution(), longer, kCardBytes * 4u);
    ASSERT_FALSE(beyond.hasValue());
    EXPECT_EQ(beyond.code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(countOf(beyond.status(), an::ContextKey::Limit), kCardBytes * 4u);

    // A short file without END is a truncated header, not a bound.
    const std::string shortFile = writeFile("short.wcs", header.substr(0u, kCardBytes * 3u));
    EXPECT_EQ(an::readFitsHeaderCards(execution(), shortFile).code(), an::ErrorCode::InvalidData);

    EXPECT_EQ(an::readFitsHeaderCards(execution(), (m_directory / "missing.wcs").string()).code(),
              an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::readFitsHeaderCards(execution(), "").code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::readFitsHeaderCards(execution(), path, 0u).code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::readFitsHeaderCards(an::ExecutionContext{}, path).code(), an::ErrorCode::NotInitialized);

    std::stop_source stop;
    stop.request_stop();
    const an::Result<std::vector<an::CodecCard>> cancelled = an::readFitsHeaderCards(execution(stop.get_token()), path);
    ASSERT_FALSE(cancelled.hasValue());
    EXPECT_TRUE(cancelled.status().isCancellation());
}

TEST_F(FitsHeaderCardsTest, WritesACardBackAsTheRecordCfitsioWritesForIt)
{
    // The first eight are records CFITSIO wrote, verbatim from a saved file; the rest are laid out as its keyword
    // writer lays them: a short string padded inside its quotes and its comment held to column
    // thirty, a value too long to end there, a hierarchical keyword, a keyword with no value, a comment cut where the
    // record ends.
    const std::vector<std::string> written = {
        "SIMPLE  =                    T / file does conform to FITS standard",
        "BITPIX  =                  -32 / number of bits per data pixel",
        "NAXIS   =                    2 / number of data axes",
        "NAXIS1  =                  324 / length of data axis 1",
        "NAXIS2  =                  245 / length of data axis 2",
        "EXTEND  =                    T / FITS dataset may contain extensions",
        "COMMENT   FITS (Flexible Image Transport System) format is defined in 'Astronomy",
        "COMMENT   and Astrophysics', volume 376, page 359; bibcode: 2001A&A...376..359H",
        "OBJECT  = 'M 31    '           / target",
        "CTYPE1  = 'RA---TAN-SIP'       / TAN (gnomic) projection + SIP distortions",
        "DATE-OBS= '2025-03-01T21:10:00.000'",
        "CRVAL1  =   83.822083000000001 / RA  of reference point",
        "HIERARCH ESO DET DIT =     1.5 / exposure",
        "HISTORY Created by the Astrometry.net suite.",
        "UNDEF     an undefined value",
        "NOTE    = 'O''Brien'           / a doubled quote and a comment that fills it all",
    };
    std::vector<std::string> records;
    for (const std::string& line : written)
        records.push_back(record(line));
    records.push_back(record("END"));
    const an::Result<std::vector<an::CodecCard>> cards = an::parseFitsHeaderCards(blocksOf(records));
    ASSERT_TRUE(cards.hasValue()) << cards.status().message();
    ASSERT_EQ(cards->size(), written.size());
    for (std::size_t index = 0u; index < written.size(); ++index)
    {
        const an::Result<std::string> formatted = an::formatFitsCardRecord((*cards)[index]);
        ASSERT_TRUE(formatted.hasValue()) << describe((*cards)[index]) << ": " << formatted.status().message();
        EXPECT_EQ(*formatted, written[index]) << describe((*cards)[index]);
        EXPECT_LE(formatted->size(), kCardBytes);
    }

    // Every card of a solver's header reads back from its record as the same card.
    const an::Result<std::vector<an::CodecCard>> solverCards = an::parseFitsHeaderCards(blocksOf(wcsHeaderRecords()));
    ASSERT_TRUE(solverCards.hasValue()) << solverCards.status().message();
    std::vector<std::string> rewritten;
    for (const an::CodecCard& card : *solverCards)
    {
        const an::Result<std::string> formatted = an::formatFitsCardRecord(card);
        ASSERT_TRUE(formatted.hasValue()) << describe(card) << ": " << formatted.status().message();
        rewritten.push_back(record(*formatted));
    }
    rewritten.push_back(record("END"));
    EXPECT_EQ(*an::parseFitsHeaderCards(blocksOf(rewritten)), *solverCards);

    // A comment longer than the record holds is cut where the record ends.
    const an::Result<an::CodecCard> chatty =
        an::CodecCard::create(an::CodecNamespace::Fits, "EXPTIME", "300.0", "seconds " + std::string(60u, 'x'));
    ASSERT_TRUE(chatty.hasValue());
    EXPECT_EQ(*an::formatFitsCardRecord(*chatty),
              "EXPTIME =                300.0 / seconds " + std::string(kCardBytes - 41u, 'x'));

    // A card in another namespace is written by its keyword, value and comment all the same.
    const an::Result<an::CodecCard> xisf = an::CodecCard::create(an::CodecNamespace::Xisf, "GAIN", "100", "");
    ASSERT_TRUE(xisf.hasValue());
    EXPECT_EQ(*an::formatFitsCardRecord(*xisf), "GAIN    =                  100");
}

TEST_F(FitsHeaderCardsTest, RefusesACardNoRecordHolds)
{
    const an::Result<an::CodecCard> commentary =
        an::CodecCard::create(an::CodecNamespace::Fits, "HISTORY", "", std::string(73u, 'h'));
    ASSERT_TRUE(commentary.hasValue());
    const an::Result<std::string> longCommentary = an::formatFitsCardRecord(*commentary);
    ASSERT_FALSE(longCommentary.hasValue());
    EXPECT_EQ(longCommentary.code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(countOf(longCommentary.status(), an::ContextKey::Requested), 81u);
    EXPECT_EQ(countOf(longCommentary.status(), an::ContextKey::Limit), kCardBytes);
    const an::Result<an::CodecCard> fits72 =
        an::CodecCard::create(an::CodecNamespace::Fits, "COMMENT", std::string(72u, 'c'), "");
    ASSERT_TRUE(fits72.hasValue());
    EXPECT_EQ(an::formatFitsCardRecord(*fits72)->size(), kCardBytes);

    const an::Result<an::CodecCard> longString =
        an::CodecCard::create(an::CodecNamespace::Fits, "LONGSTR", "'" + std::string(69u, 's') + "'", "");
    ASSERT_TRUE(longString.hasValue());
    const an::Result<std::string> continued = an::formatFitsCardRecord(*longString);
    ASSERT_FALSE(continued.hasValue());
    EXPECT_EQ(continued.code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(countOf(continued.status(), an::ContextKey::Requested), 81u);

    const an::Result<an::CodecCard> fitting =
        an::CodecCard::create(an::CodecNamespace::Fits, "LONGSTR", "'" + std::string(68u, 's') + "'", "a comment");
    ASSERT_TRUE(fitting.hasValue());
    EXPECT_EQ(*an::formatFitsCardRecord(*fitting), "LONGSTR = '" + std::string(68u, 's') + "'");

    const an::Result<an::CodecCard> equals = an::CodecCard::create(an::CodecNamespace::Fits, "A=B", "1", "");
    if (equals.hasValue())
    {
        EXPECT_EQ(an::formatFitsCardRecord(*equals).code(), an::ErrorCode::InvalidArgument);
    }
}
