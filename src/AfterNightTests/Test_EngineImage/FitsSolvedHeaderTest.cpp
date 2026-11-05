// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A solved light frame's header, read by this engine's own FITS reader into its world-coordinate model.
//
// The header is written here byte for byte in the spellings a capture and plate-solving program
// writes - exponents with three digits, strings padded inside their quotes, acquisition cards
// interleaved with the solution - rather than through this engine's writer. A reader tested only
// against its own writer agrees with itself; this file is the other half, where the cards the
// world-coordinate bridge is handed are the ones a real header held and the reader preserved.
//
// The three cases are the three states a light frame's header is found in: solved against an
// explicitly stated reference frame, solved against an epoch alone, and not solved at all. None of
// them may cost the caller the pixels.

#include <AfterNightEngine/IO/FitsCodec.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

/// The frame the solved header below describes.
constexpr std::uint32_t kWidth = 64u;
constexpr std::uint32_t kHeight = 48u;

/// One header card and one header or data block, as the FITS standard fixes them.
constexpr std::size_t kCardBytes = 80u;
constexpr std::size_t kBlockBytes = 2880u;

/// A value card in the fixed format: the keyword in columns 1-8, `= ` in 9-10, a string from column
/// 11 or any other value right-justified to column 30, and an optional comment after ` / `.
[[nodiscard]] std::string valueCard(std::string_view keyword, std::string_view value, std::string_view comment = {})
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
    text.resize(kCardBytes, ' ');
    return text;
}

/// A commentary card, which carries text after its keyword and no value indicator.
[[nodiscard]] std::string commentaryCard(std::string_view keyword, std::string_view text)
{
    std::string card(keyword);
    card.resize(8u, ' ');
    card += text;
    card.resize(kCardBytes, ' ');
    return card;
}

/// The keyword a card names, without the padding its column carries.
[[nodiscard]] std::string keywordOf(const std::string& card)
{
    std::string keyword = card.substr(0u, 8u);
    keyword.erase(keyword.find_last_not_of(' ') + 1u);
    return keyword;
}

/// The header cards a plate solve leaves in a light frame, in the order such a header holds them.
///
/// The acquisition cards are here on purpose: a real header is mostly not world-coordinate cards,
/// and the bridge has to find its own vocabulary inside one that is not sorted for it.
[[nodiscard]] std::vector<std::string> solvedHeader()
{
    return {valueCard("SIMPLE", "T", "conforms to FITS standard"),
            valueCard("BITPIX", "16", "bits per data pixel"),
            valueCard("NAXIS", "2", "number of data axes"),
            valueCard("NAXIS1", "64", "length of data axis 1"),
            valueCard("NAXIS2", "48", "length of data axis 2"),
            valueCard("BZERO", "32768", "offset data range to that of unsigned short"),
            valueCard("BSCALE", "1", "default scaling factor"),
            valueCard("EXPTIME", "120.0", "exposure time in seconds"),
            valueCard("OBJECT", "'NGC 7000'"),
            valueCard("CTYPE1", "'RA---TAN-SIP'", "TAN with distortion"),
            valueCard("CTYPE2", "'DEC--TAN-SIP'", "TAN with distortion"),
            valueCard("CUNIT1", "'deg     '"),
            valueCard("CUNIT2", "'deg     '"),
            valueCard("EQUINOX", "2000.0"),
            valueCard("CRPIX1", "3.2500000000E+001", "reference pixel"),
            valueCard("CRPIX2", "2.4500000000E+001", "reference pixel"),
            valueCard("CRVAL1", "314.7500000000", "reference right ascension"),
            valueCard("CRVAL2", "44.5200000000", "reference declination"),
            valueCard("CD1_1", "-1.0555555556E-03"),
            valueCard("CD1_2", "4.3633231299E-05"),
            valueCard("CD2_1", "4.3633231299E-05"),
            valueCard("CD2_2", "1.0555555556E-03"),
            valueCard("A_ORDER", "2"),
            valueCard("A_0_2", "1.4820000000E-05"),
            valueCard("A_1_1", "-9.3100000000E-06"),
            valueCard("A_2_0", "6.7050000000E-06"),
            valueCard("B_ORDER", "2"),
            valueCard("B_0_2", "-8.1170000000E-06"),
            valueCard("B_1_1", "1.2409000000E-05"),
            valueCard("B_2_0", "-3.9260000000E-06"),
            commentaryCard("HISTORY", "plate solved")};
}

/// The value every sample of the frame holds, recognisable in every one.
[[nodiscard]] std::uint16_t rampValue(std::uint32_t x, std::uint32_t y) noexcept
{
    return static_cast<std::uint16_t>(((y * kWidth + x) * 11u + 3u) & 0xffffu);
}

class FitsSolvedHeaderTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_directory = std::filesystem::temp_directory_path() /
                      ("an-fits-solved-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

    /// Writes a single-image FITS file holding `cards` and the ramp, as unsigned sixteen-bit samples
    /// stored as signed big-endian codes offset by the header's BZERO.
    [[nodiscard]] std::string writeFrame(const std::vector<std::string>& cards) const
    {
        std::string header;
        for (const std::string& card : cards)
            header += card;
        header += commentaryCard("END", "");
        header.resize((header.size() + kBlockBytes - 1u) / kBlockBytes * kBlockBytes, ' ');

        std::string data(static_cast<std::size_t>(kWidth) * kHeight * 2u, '\0');
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const auto stored = static_cast<std::uint16_t>(rampValue(x, y) ^ 0x8000u);
                const std::size_t offset = (static_cast<std::size_t>(y) * kWidth + x) * 2u;
                data[offset] = static_cast<char>(stored >> 8u);
                data[offset + 1u] = static_cast<char>(stored & 0xffu);
            }
        }
        data.resize((data.size() + kBlockBytes - 1u) / kBlockBytes * kBlockBytes, '\0');

        const std::filesystem::path path = m_directory / "solved-light-0007.fit";
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(header.data(), static_cast<std::streamsize>(header.size()));
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.close();
        const std::u8string utf8 = path.u8string();
        return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
    }

    /// Reads the whole frame and its metadata through the context's FITS reader.
    [[nodiscard]] an::Result<an::CodecDecodeResult> read(const std::string& path)
    {
        an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(m_engine, an::ExecutionOptions{});
        if (!execution.hasValue())
            return execution.status();
        return an::decodeImage(m_engine, *execution, an::CodecReadRequest{.path = path});
    }

    /// Whether every sample the reader returned is the one the file stored.
    [[nodiscard]] static bool holdsTheRamp(const an::RawFrame& frame)
    {
        const std::optional<an::ImageLayout> layout = frame.layout();
        if (!layout.has_value() || layout->elementSizeBytes() != sizeof(std::uint16_t))
            return false;
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                std::uint16_t value = 0u;
                std::memcpy(&value, frame.bytes().data() + y * layout->rowStrideBytes() + x * sizeof(value),
                            sizeof(value));
                if (value != rampValue(x, y))
                    return false;
            }
        }
        return true;
    }

    std::filesystem::path m_directory;
    an::EngineContext m_engine;
};

TEST_F(FitsSolvedHeaderTest, AnExplicitIcrsSolvedHeaderReachesTheModelThroughTheCardsTheReaderPreserved)
{
    std::vector<std::string> cards = solvedHeader();
    // The epoch-only original implies FK5. Stating the frame is what makes the solution one this
    // engine accepts, and adding the declaration here is deliberate rather than an inference.
    cards.push_back(valueCard("RADESYS", "'ICRS    '", "explicit reference frame"));

    const an::Result<an::CodecDecodeResult> decoded = read(writeFrame(cards));
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();

    // The bridge reads the cards the reader preserved. Nothing between the file and here has
    // interpreted one of them on the bridge's behalf.
    const std::span<const an::CodecCard> preserved = decoded->metadata.codec().cards();
    ASSERT_EQ(an::checkWcsCards(preserved), an::WcsCardError::None);
    const an::Result<an::WcsSolution> solution = an::parseWcsCards(preserved, kWidth, kHeight);
    ASSERT_TRUE(solution.hasValue()) << solution.status().message();

    // The header's one-based reference pixel is the frame centre, which in engine coordinates is
    // (32, 24) for a 64 by 48 frame.
    EXPECT_DOUBLE_EQ(solution->referencePixel().x, 32.0);
    EXPECT_DOUBLE_EQ(solution->referencePixel().y, 24.0);
    EXPECT_DOUBLE_EQ(solution->referencePoint().rightAscensionDegrees, 314.75);
    EXPECT_DOUBLE_EQ(solution->referencePoint().declinationDegrees, 44.52);
    EXPECT_TRUE(solution->distortion().hasForwardModel());
    EXPECT_FALSE(solution->distortion().hasInverseModel());
    EXPECT_NEAR(solution->meanPixelScaleArcsecPerPixel(), 3.8, 1.0e-2);
    EXPECT_FALSE(solution->mirrored());

    // The reader's own typed solution is the one the bridge builds from the same cards.
    const an::Result<an::AstrometricSolution> summary = solution->toAstrometricSolution();
    ASSERT_TRUE(summary.hasValue()) << summary.status().message();
    EXPECT_EQ(decoded->metadata.astrometry(), *summary);

    // The frame centre projects to the reference direction, and every corner comes back to itself.
    const an::Result<an::SkyCoordinate> centre = solution->fieldCenter();
    ASSERT_TRUE(centre.hasValue()) << centre.status().message();
    EXPECT_DOUBLE_EQ(centre->rightAscensionDegrees, 314.75);
    EXPECT_DOUBLE_EQ(centre->declinationDegrees, 44.52);
    for (const an::PixelCoordinate corner :
         {an::PixelCoordinate{.x = 0.5, .y = 0.5}, an::PixelCoordinate{.x = 63.5, .y = 0.5},
          an::PixelCoordinate{.x = 0.5, .y = 47.5}, an::PixelCoordinate{.x = 63.5, .y = 47.5}})
    {
        const an::Result<an::SkyCoordinate> sky = solution->pixelToWorld(corner);
        ASSERT_TRUE(sky.hasValue()) << sky.status().message();
        const an::Result<an::PixelCoordinate> back = solution->worldToPixel(*sky);
        ASSERT_TRUE(back.hasValue()) << back.status().message();
        EXPECT_NEAR(back->x, corner.x, 1.0e-3);
        EXPECT_NEAR(back->y, corner.y, 1.0e-3);
    }

    // The frame that came in beside the header is the one the solution describes, sample for sample.
    const std::optional<an::RawFrameDescriptor> frame = decoded->frame.descriptor();
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->extent().width(), kWidth);
    EXPECT_EQ(frame->extent().height(), kHeight);
    EXPECT_TRUE(holdsTheRamp(decoded->frame));
}

TEST_F(FitsSolvedHeaderTest, AnEpochOnlyHeaderIsPreservedButNeverImplicitlyConvertedToIcrs)
{
    const an::Result<an::CodecDecodeResult> decoded = read(writeFrame(solvedHeader()));
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();

    const an::CodecMetadata& codec = decoded->metadata.codec();
    EXPECT_NE(codec.find(an::CodecNamespace::Fits, "EQUINOX"), nullptr);
    EXPECT_EQ(codec.find(an::CodecNamespace::Fits, "RADESYS"), nullptr);
    EXPECT_EQ(an::checkWcsCards(codec.cards()), an::WcsCardError::UnsupportedFrame);
    EXPECT_EQ(an::parseWcsCards(codec.cards(), kWidth, kHeight).code(), an::ErrorCode::Unsupported);

    // A solution this engine will not accept costs the caller neither the pixels nor the cards, and
    // the reader states no typed solution in its place.
    EXPECT_EQ(decoded->metadata.astrometry(), an::AstrometricSolution{});
    EXPECT_TRUE(holdsTheRamp(decoded->frame));
}

TEST_F(FitsSolvedHeaderTest, AnUnsolvedFramesHeaderReportsNoSolutionRatherThanABrokenOne)
{
    std::vector<std::string> cards = solvedHeader();
    // What a light frame's header looks like before anything has solved it.
    cards.erase(std::remove_if(cards.begin(), cards.end(),
                               [](const std::string& card) { return an::isWcsKeyword(keywordOf(card)); }),
                cards.end());

    const an::Result<an::CodecDecodeResult> decoded = read(writeFrame(cards));
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();

    const an::CodecMetadata& codec = decoded->metadata.codec();
    EXPECT_EQ(an::checkWcsCards(codec.cards()), an::WcsCardError::NoSolution);
    EXPECT_EQ(an::parseWcsCards(codec.cards(), kWidth, kHeight).code(), an::ErrorCode::EmptySelection);
    EXPECT_EQ(decoded->metadata.astrometry(), an::AstrometricSolution{});

    // The acquisition cards the solve had nothing to do with are all still there.
    for (const char* keyword : {"EXPTIME", "OBJECT", "HISTORY"})
    {
        EXPECT_NE(codec.find(an::CodecNamespace::Fits, keyword), nullptr) << keyword;
    }
    EXPECT_TRUE(holdsTheRamp(decoded->frame));
}

} // namespace
