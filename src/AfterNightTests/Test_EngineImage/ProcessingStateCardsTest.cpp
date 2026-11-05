// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/IO/ProcessingStateCards.h.
//
// The processing-state and colour-calibration card family, read into typed records and written back:
// the spellings, the transitions a loader, a process and a person make, the calibration document that
// carries a long identity past a header writer that clips, and a round trip through the FITS and XISF
// writers. Expected card text is written out here from the rule it follows, and the document's hex
// transport and checksum are built here independently of the writer under test.

#include <AfterNightEngine/IO/ProcessingStateCards.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/IO/XisfCodec.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
using an::RecordedChannelBasis;
using an::RecordedColorCalibration;
using an::RecordedLinearity;
using an::RecordedWhiteBalance;
using an::StateAssignmentSource;

constexpr std::string_view kSchema = "afternight.color_calibration_provenance.v1";

[[nodiscard]] an::CodecCard fits(std::string keyword, std::string value, std::string comment = {})
{
    return *an::CodecCard::create(an::CodecNamespace::Fits, std::move(keyword), std::move(value), std::move(comment));
}

[[nodiscard]] an::CodecMetadata metadataOf(std::vector<an::CodecCard> cards)
{
    an::Result<an::CodecMetadata> made = an::CodecMetadata::create({}, std::move(cards));
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::CodecMetadata{};
}

[[nodiscard]] an::CodecMetadata unwrap(an::Result<an::CodecMetadata> result)
{
    EXPECT_TRUE(result.hasValue()) << result.status().message();
    return result.hasValue() ? std::move(*result) : an::CodecMetadata{};
}

/// The value of the first FITS card with this keyword, or an absent marker.
[[nodiscard]] std::string valueOf(const an::CodecMetadata& metadata, std::string_view keyword)
{
    const an::CodecCard* card = metadata.find(an::CodecNamespace::Fits, keyword);
    return card != nullptr ? card->value() : std::string("<absent>");
}

/// How many FITS cards carry this keyword.
[[nodiscard]] std::size_t countOf(const an::CodecMetadata& metadata, std::string_view keyword)
{
    return static_cast<std::size_t>(
        std::count_if(metadata.cards().begin(), metadata.cards().end(), [&](const an::CodecCard& card) {
            return card.origin() == an::CodecNamespace::Fits && card.keyword() == keyword;
        }));
}

/// The same cards with one FITS keyword's cards given a new value, or removed when the value is absent.
[[nodiscard]] an::CodecMetadata edited(const an::CodecMetadata& metadata, std::string_view keyword,
                                       std::optional<std::string> value)
{
    std::vector<an::CodecCard> cards;
    for (const an::CodecCard& card : metadata.cards())
    {
        if (card.origin() != an::CodecNamespace::Fits || card.keyword() != keyword)
        {
            cards.push_back(card);
        }
        else if (value.has_value())
        {
            cards.push_back(fits(card.keyword(), *value, card.comment()));
        }
    }
    return metadataOf(std::move(cards));
}

[[nodiscard]] std::string hexOf(std::string_view text)
{
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string hex;
    for (const char character : text)
    {
        const auto byte = static_cast<unsigned char>(character);
        hex.push_back(kDigits[byte >> 4u]);
        hex.push_back(kDigits[byte & 0x0fu]);
    }
    return hex;
}

/// The 64-bit FNV-1a checksum of a text, as sixteen lower-case hexadecimal digits.
[[nodiscard]] std::string checksumOf(std::string_view text)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const char character : text)
    {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ull;
    }
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string digits(16u, '0');
    for (std::size_t i = 16u; i > 0u; --i)
    {
        digits[i - 1u] = kDigits[hash & 0x0fu];
        hash >>= 4u;
    }
    return digits;
}

[[nodiscard]] std::string chunkKeyword(std::size_t index)
{
    std::string digits = std::to_string(index);
    digits.insert(0u, 4u - digits.size(), '0');
    return "ANCJ" + digits;
}

/// The cards a calibration document travels in, built from the transport rule rather than by the writer.
[[nodiscard]] std::vector<an::CodecCard> transportCards(std::string_view document)
{
    const std::string hex = hexOf(document);
    const std::size_t chunks = (hex.size() + 31u) / 32u;
    std::vector<an::CodecCard> cards = {fits("ANCJVER", "1"), fits("ANCJCNT", std::to_string(chunks)),
                                        fits("ANCJLEN", std::to_string(document.size())),
                                        fits("ANCJCHK", "'" + checksumOf(document) + "'")};
    for (std::size_t i = 0u; i < chunks; ++i)
    {
        cards.push_back(fits(chunkKeyword(i), "'" + hex.substr(i * 32u, 32u) + "'"));
    }
    return cards;
}

[[nodiscard]] an::ImageDescriptor describe(std::uint32_t channels, an::ChannelSchema schema,
                                           const an::CfaDescriptor& cfa = {})
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(4u, 4u, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> made =
        an::ImageDescriptor::create({.extent = *extent,
                                     .sampleType = an::SampleType::Float32,
                                     .channelSchema = schema,
                                     .colorSpace = an::ColorSpace::Unspecified,
                                     .numericDomain = an::NumericDomain::RawSensorCode,
                                     .cfa = cfa,
                                     .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                     .pixelAspectRatio = 1.0});
    EXPECT_TRUE(made.has_value());
    return *made;
}

[[nodiscard]] an::ProcessingStateRecord fullRecord()
{
    an::ProcessingStateRecord record;
    record.linearity = RecordedLinearity::Nonlinear;
    record.channelBasis = RecordedChannelBasis::DisplayRgb;
    record.colorCalibration = RecordedColorCalibration::Manual;
    record.assignmentSource = StateAssignmentSource::Process;
    record.rawWhiteBalance = an::RecordedRawWhiteBalance{.mode = RecordedWhiteBalance::CameraAsShot, .applied = true};
    record.rawWhiteBalanceMultipliers = std::array<double, 4u>{2.0, 1.0, 1.5, 1.0};
    record.cameraMatrixProvenance = "libraw:rgb_cam";
    record.operation = "O'Brien's stretch";
    record.attestation = {.sourcePath = "imports/m31.tif",
                          .sourceHash = "sha256:0123",
                          .attestedBy = "observer",
                          .timestampUtc = "2024-05-01T20:31:02Z",
                          .toolVersion = "importer 1.2",
                          .warnings = "none"};
    return record;
}

[[nodiscard]] an::ColorCalibrationRecord longSpectrophotometricRecord()
{
    an::ColorCalibrationRecord record;
    record.calibration = RecordedColorCalibration::Spectrophotometric;
    record.algorithmVersion = "spcc-engine-v1-long-structured-payload";
    record.responseProfileId = "response.afterNight.test.camera-with-a-very-long-immutable-throughput-profile-id."
                               "sony-imx571.qe-window-filter-lpf.integration-contract-v2026-05-11";
    record.responseProfileVersion = "response-version-2026-05-11.validation-build-with-long-identifier";
    record.whiteReferenceId = "white-reference.d65-afterNight-validation-library.long-identifier";
    record.whiteReferenceVersion = "white-reference-version-2026-05-11";
    record.catalogFamilyId = "spcc_gaia_dr3_xp_sampled";
    record.catalogManifestId = "spcc-gaia-dr3-xp-sampled-private-regional-validation-manifest-2026-05-11-m31-core-"
                               "rights-private-local-checksum-verified";
    record.catalogAccessProfileId = "installed-regional-cache.validation-m31-core.private-local";
    record.gains = {1.125, 1.0, 0.875};
    record.offsets = {0.0125, -0.003, 0.021};
    record.inputLinearity = RecordedLinearity::Linear;
    record.inputChannelBasis = RecordedChannelBasis::CameraRgb;
    return record;
}

/// Cards stating a linear product in a basis, as a process would have left it.
[[nodiscard]] an::CodecMetadata linearProduct(RecordedChannelBasis basis)
{
    return unwrap(an::withProcessingStateCards({}, an::linearState({}, basis, "unit-test")));
}

TEST(ProcessingStateCardsTest, EveryValueIsWrittenInItsDocumentedSpellingAndReadWithoutRegardToCase)
{
    EXPECT_STREQ(an::stateCardText(RecordedLinearity::Unknown), "unknown");
    EXPECT_STREQ(an::stateCardText(RecordedLinearity::Linear), "linear");
    EXPECT_STREQ(an::stateCardText(RecordedLinearity::Nonlinear), "nonlinear");
    EXPECT_STREQ(an::stateCardText(static_cast<RecordedLinearity>(3u)), "unknown");
    EXPECT_STREQ(an::stateCardText(RecordedChannelBasis::Unknown), "unknown");
    EXPECT_STREQ(an::stateCardText(RecordedChannelBasis::RawCfa), "raw-cfa");
    EXPECT_STREQ(an::stateCardText(RecordedChannelBasis::LinearRgb), "linear-rgb");
    EXPECT_STREQ(an::stateCardText(RecordedChannelBasis::CameraRgb), "camera-rgb");
    EXPECT_STREQ(an::stateCardText(RecordedChannelBasis::DisplayRgb), "display-rgb");
    EXPECT_STREQ(an::stateCardText(RecordedChannelBasis::Monochrome), "monochrome");
    EXPECT_STREQ(an::stateCardText(static_cast<RecordedChannelBasis>(6u)), "unknown");
    EXPECT_STREQ(an::stateCardText(RecordedColorCalibration::None), "none");
    EXPECT_STREQ(an::stateCardText(RecordedColorCalibration::Photometric), "pcc");
    EXPECT_STREQ(an::stateCardText(RecordedColorCalibration::Spectrophotometric), "spcc");
    EXPECT_STREQ(an::stateCardText(RecordedColorCalibration::Manual), "manual");
    EXPECT_STREQ(an::stateCardText(RecordedColorCalibration::Unknown), "unknown");
    EXPECT_STREQ(an::stateCardText(static_cast<RecordedColorCalibration>(5u)), "unknown");
    EXPECT_STREQ(an::stateCardText(StateAssignmentSource::None), "none");
    EXPECT_STREQ(an::stateCardText(StateAssignmentSource::Loader), "loader");
    EXPECT_STREQ(an::stateCardText(StateAssignmentSource::Process), "process");
    EXPECT_STREQ(an::stateCardText(StateAssignmentSource::UserAttestation), "user-attestation");
    EXPECT_STREQ(an::stateCardText(static_cast<StateAssignmentSource>(4u)), "none");
    EXPECT_STREQ(an::stateCardText(RecordedWhiteBalance::None), "none");
    EXPECT_STREQ(an::stateCardText(RecordedWhiteBalance::Daylight), "daylight");
    EXPECT_STREQ(an::stateCardText(RecordedWhiteBalance::CameraAsShot), "camera-as-shot");
    EXPECT_STREQ(an::stateCardText(RecordedWhiteBalance::Automatic), "auto");
    EXPECT_STREQ(an::stateCardText(RecordedWhiteBalance::Custom), "custom");
    EXPECT_STREQ(an::stateCardText(static_cast<RecordedWhiteBalance>(5u)), "daylight");
    EXPECT_STREQ(an::calibrationPayloadProblemName(an::CalibrationPayloadProblem::MissingChunk), "MissingChunk");
    EXPECT_STREQ(an::calibrationPayloadProblemName(static_cast<an::CalibrationPayloadProblem>(12u)),
                 "UnknownCalibrationPayloadProblem");
    EXPECT_STREQ(an::spectrophotometricInputProblemName(an::SpectrophotometricInputProblem::AlreadyColorCalibrated),
                 "AlreadyColorCalibrated");
    EXPECT_STREQ(an::spectrophotometricInputProblemName(static_cast<an::SpectrophotometricInputProblem>(6u)),
                 "UnknownSpectrophotometricInputProblem");

    const an::ProcessingStateRecord spelled = an::readProcessingStateCards(metadataOf(
        {fits("ANLIN", "'LINEAR'"), fits("ANBASIS", "' Camera-RGB '"), fits("ANCCAL", "SPCC"),
         fits("ANASRC", "'User-Attestation'"), fits("ANWBMOD", "'Camera-As-Shot'"), fits("ANWBAPP", "'yes'")}));
    EXPECT_EQ(spelled.linearity, RecordedLinearity::Linear);
    EXPECT_EQ(spelled.channelBasis, RecordedChannelBasis::CameraRgb);
    EXPECT_EQ(spelled.colorCalibration, RecordedColorCalibration::Spectrophotometric);
    EXPECT_EQ(spelled.assignmentSource, StateAssignmentSource::UserAttestation);
    ASSERT_TRUE(spelled.rawWhiteBalance.has_value());
    EXPECT_EQ(spelled.rawWhiteBalance->mode, RecordedWhiteBalance::CameraAsShot);
    EXPECT_TRUE(spelled.rawWhiteBalance->applied);

    const an::ProcessingStateRecord unrecognised = an::readProcessingStateCards(
        metadataOf({fits("ANLIN", "'sort of'"), fits("ANBASIS", "'rgb'"), fits("ANCCAL", "'maybe'"),
                    fits("ANASRC", "'robot'"), fits("ANWBMOD", "'tungsten'"), fits("ANWBAPP", "'no'")}));
    EXPECT_EQ(unrecognised.linearity, RecordedLinearity::Unknown);
    EXPECT_EQ(unrecognised.channelBasis, RecordedChannelBasis::Unknown);
    EXPECT_EQ(unrecognised.colorCalibration, RecordedColorCalibration::Unknown);
    EXPECT_EQ(unrecognised.assignmentSource, StateAssignmentSource::None);
    ASSERT_TRUE(unrecognised.rawWhiteBalance.has_value());
    EXPECT_EQ(unrecognised.rawWhiteBalance->mode, RecordedWhiteBalance::Daylight);
    EXPECT_FALSE(unrecognised.rawWhiteBalance->applied);
    EXPECT_EQ(an::readProcessingStateCards(metadataOf({fits("ANCCAL", "''")})).colorCalibration,
              RecordedColorCalibration::None);
}

TEST(ProcessingStateCardsTest, AProductWithNoCardsRecordsNothingAndMayNotBeCalibratedFromSpectra)
{
    const an::ProcessingStateRecord record = an::readProcessingStateCards(metadataOf({fits("BITPIX", "-32")}));
    EXPECT_EQ(record, an::ProcessingStateRecord{});
    EXPECT_EQ(an::spectrophotometricInputProblem(record), an::SpectrophotometricInputProblem::UnknownLinearity);
    const an::ColorCalibrationReading reading = an::readColorCalibrationCards({});
    EXPECT_EQ(reading.record, an::ColorCalibrationRecord{});
    EXPECT_FALSE(reading.status.present);
    EXPECT_FALSE(reading.status.fellBackToCards);
    EXPECT_EQ(reading.status.problem, an::CalibrationPayloadProblem::Absent);
}

TEST(ProcessingStateCardsTest, ARecordWrittenAsCardsReadsBackAsTheSameRecordAndEditsInPlace)
{
    const an::CodecMetadata destination =
        metadataOf({fits("BITPIX", "-32"), fits("ANLIN", "'linear'", "kept comment"), fits("ANLIN", "'duplicate'"),
                    *an::CodecCard::create(an::CodecNamespace::Tiff, "Exif.Image.Artist", "someone", {})});
    const an::ProcessingStateRecord record = fullRecord();
    const an::CodecMetadata written = unwrap(an::withProcessingStateCards(destination, record));

    EXPECT_EQ(valueOf(written, "ANSTVER"), "1");
    EXPECT_EQ(valueOf(written, "ANLIN"), "'nonlinear'");
    EXPECT_EQ(countOf(written, "ANLIN"), 1u);
    EXPECT_EQ(valueOf(written, "ANBASIS"), "'display-rgb'");
    EXPECT_EQ(valueOf(written, "ANASRC"), "'process'");
    EXPECT_EQ(valueOf(written, "ANCCAL"), "'manual'");
    EXPECT_EQ(valueOf(written, "ANWBMOD"), "'camera-as-shot'");
    EXPECT_EQ(valueOf(written, "ANWBAPP"), "T");
    EXPECT_EQ(valueOf(written, "ANWBMUL"), "'2.000000,1.000000,1.500000,1.000000'");
    EXPECT_EQ(valueOf(written, "ANCMATP"), "'libraw:rgb_cam'");
    EXPECT_EQ(valueOf(written, "ANOP"), "'O''Brien''s stretch'");
    EXPECT_EQ(valueOf(written, "ANASFILE"), "'imports/m31.tif'");
    EXPECT_EQ(valueOf(written, "ANASHSH"), "'sha256:0123'");
    EXPECT_EQ(valueOf(written, "ANASBY"), "'observer'");
    EXPECT_EQ(valueOf(written, "ANASUTC"), "'2024-05-01T20:31:02Z'");
    EXPECT_EQ(valueOf(written, "ANASTOOL"), "'importer 1.2'");
    EXPECT_EQ(valueOf(written, "ANASWARN"), "'none'");
    EXPECT_EQ(an::readProcessingStateCards(written), record);

    // The first card is rewritten where it stood, keeping its comment; the rest are appended in order.
    ASSERT_EQ(written.cards().size(), 18u);
    EXPECT_EQ(written.cards()[1].keyword(), "ANLIN");
    EXPECT_EQ(written.cards()[1].comment(), "kept comment");
    EXPECT_EQ(written.cards()[2].keyword(), "Exif.Image.Artist");
    EXPECT_EQ(written.cards()[3].keyword(), "ANSTVER");
    EXPECT_EQ(written.cards()[4].keyword(), "ANBASIS");
    EXPECT_EQ(written.cards().back().keyword(), "ANASWARN");

    an::ProcessingStateRecord cleared = record;
    cleared.rawWhiteBalance.reset();
    cleared.rawWhiteBalanceMultipliers.reset();
    cleared.cameraMatrixProvenance.clear();
    cleared.attestation = {};
    const an::CodecMetadata rewritten = unwrap(an::withProcessingStateCards(written, cleared));
    for (const std::string_view keyword : {"ANWBMOD", "ANWBAPP", "ANWBMUL", "ANCMATP", "ANASFILE", "ANASHSH", "ANASBY",
                                           "ANASUTC", "ANASTOOL", "ANASWARN"})
    {
        EXPECT_EQ(valueOf(rewritten, keyword), "<absent>") << keyword;
    }
    EXPECT_EQ(an::readProcessingStateCards(rewritten), cleared);
}

TEST(ProcessingStateCardsTest, VersionOneCardsReadAsTheRecordTheyState)
{
    const an::ProcessingStateRecord record = an::readProcessingStateCards(
        metadataOf({fits("ANSTVER", "1"), fits("ANLIN", "linear"), fits("ANBASIS", "raw-cfa"), fits("ANASRC", "loader"),
                    fits("ANCCAL", "none"), fits("ANWBMOD", "daylight"), fits("ANWBAPP", "F"),
                    fits("ANWBMUL", "2.000000,1.000000,1.500000,1.000000"), fits("ANCMATP", "libraw:rgb_cam"),
                    fits("ANOP", "RAW")}));
    an::ProcessingStateRecord expected;
    expected.linearity = RecordedLinearity::Linear;
    expected.channelBasis = RecordedChannelBasis::RawCfa;
    expected.assignmentSource = StateAssignmentSource::Loader;
    expected.rawWhiteBalance = an::RecordedRawWhiteBalance{.mode = RecordedWhiteBalance::Daylight, .applied = false};
    expected.rawWhiteBalanceMultipliers = std::array<double, 4u>{2.0, 1.0, 1.5, 1.0};
    expected.cameraMatrixProvenance = "libraw:rgb_cam";
    expected.operation = "RAW";
    EXPECT_EQ(record, expected);

    // A field that does not begin with a number gives up no position, and a gain without a field stays at 1.
    const an::ProcessingStateRecord partial = an::readProcessingStateCards(metadataOf({fits("ANWBMUL", "'x,3,,4'")}));
    ASSERT_TRUE(partial.rawWhiteBalanceMultipliers.has_value());
    EXPECT_EQ(*partial.rawWhiteBalanceMultipliers, (std::array<double, 4u>{3.0, 4.0, 1.0, 1.0}));
    EXPECT_FALSE(partial.rawWhiteBalance.has_value());
}

TEST(ProcessingStateCardsTest, AMultiplierIsCarriedToSixDecimalsAndNoFurther)
{
    // Found by the campaign target, which claimed the state survived a write and a read exactly and
    // was told otherwise by a multiplier of 1.0000001. The header states four numbers with six
    // decimals, so this is the format doing what it says rather than a defect - and it is pinned
    // here because "the record round-trips" is what a reader of that header would otherwise assume.
    an::ProcessingStateRecord record;
    record.linearity = an::RecordedLinearity::Linear;
    record.rawWhiteBalanceMultipliers = std::array<double, 4u>{1.0000001, 2.4736542, 0.0000004, 1.0};

    const an::Result<an::CodecMetadata> written = an::withProcessingStateCards(an::CodecMetadata{}, record);
    ASSERT_TRUE(written.hasValue());
    EXPECT_EQ(valueOf(*written, "ANWBMUL"), "'1.000000,2.473654,0.000000,1.000000'");

    const an::ProcessingStateRecord reread = an::readProcessingStateCards(*written);
    ASSERT_TRUE(reread.rawWhiteBalanceMultipliers.has_value());
    EXPECT_EQ(*reread.rawWhiteBalanceMultipliers, (std::array<double, 4u>{1.0, 2.473654, 0.0, 1.0}));

    // Everything else is exact, and a second pass changes nothing: the rounding happens once.
    an::ProcessingStateRecord expected = record;
    expected.rawWhiteBalanceMultipliers = reread.rawWhiteBalanceMultipliers;
    EXPECT_EQ(reread, expected);

    const an::Result<an::CodecMetadata> again = an::withProcessingStateCards(an::CodecMetadata{}, reread);
    ASSERT_TRUE(again.hasValue());
    EXPECT_EQ(an::readProcessingStateCards(*again), reread);
}

TEST(ProcessingStateCardsTest, ClearingRemovesTheWholeFamilyAndNothingElse)
{
    constexpr std::array<std::string_view, 32u> kFamily = {
        "ANSTVER", "ANLIN",    "ANBASIS", "ANASRC",  "ANWBMOD", "ANWBAPP",  "ANWBMUL",  "ANCMATP",
        "ANOP",    "ANASFILE", "ANASHSH", "ANASBY",  "ANASUTC", "ANASTOOL", "ANASWARN", "ANCCAL",
        "ANCCGN",  "ANCCOF",   "ANRSPID", "ANRSPVR", "ANWREF",  "ANWREFV",  "ANCATF",   "ANCATM",
        "ANCATP",  "ANALGOV",  "ANINLIN", "ANINBAS", "ANCJVER", "ANCJCNT",  "ANCJLEN",  "ANCJCHK"};
    std::vector<an::CodecCard> cards = {fits("BITPIX", "-32"), fits("OBJECT", "'M 31'")};
    for (const std::string_view keyword : kFamily)
    {
        cards.push_back(fits(std::string(keyword), "'x'"));
    }
    cards.push_back(fits("ANLIN", "'again'"));
    cards.push_back(fits("ANCJ0000", "'00'"));
    cards.push_back(fits("ANCJ9999", "'00'"));
    cards.push_back(fits("ANCJ12", "'kept'"));
    cards.push_back(fits("ANCJABCD", "'kept'"));
    cards.push_back(fits("HISTORY", "stacked"));
    cards.push_back(*an::CodecCard::create(an::CodecNamespace::Xisf, "ANLIN", "other namespace", {}));

    const an::CodecMetadata cleared = unwrap(an::withoutProcessingStateCards(metadataOf(std::move(cards))));
    std::vector<std::string> remaining;
    for (const an::CodecCard& card : cleared.cards())
    {
        remaining.push_back(card.keyword());
    }
    EXPECT_EQ(remaining, (std::vector<std::string>{"BITPIX", "OBJECT", "ANCJ12", "ANCJABCD", "HISTORY", "ANLIN"}));
}

TEST(ProcessingStateCardsTest, ChannelBasesFollowTheDescriptionAndWhatWasRecorded)
{
    const an::CfaDescriptor rggb = *an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    const an::ImageDescriptor mosaic = describe(1u, an::ChannelSchema::Mono, rggb);
    const an::ImageDescriptor mono = describe(1u, an::ChannelSchema::Mono);
    const an::ImageDescriptor rgb = describe(3u, an::ChannelSchema::Rgb);
    const an::ImageDescriptor rgba = describe(4u, an::ChannelSchema::Rgba);

    EXPECT_EQ(an::linearChannelBasisOf(mosaic, RecordedChannelBasis::CameraRgb), RecordedChannelBasis::RawCfa);
    EXPECT_EQ(an::linearChannelBasisOf(mono, RecordedChannelBasis::LinearRgb), RecordedChannelBasis::Monochrome);
    EXPECT_EQ(an::linearChannelBasisOf(rgb, RecordedChannelBasis::Unknown), RecordedChannelBasis::LinearRgb);
    EXPECT_EQ(an::linearChannelBasisOf(rgb, RecordedChannelBasis::CameraRgb), RecordedChannelBasis::CameraRgb);
    EXPECT_EQ(an::linearChannelBasisOf(rgb, RecordedChannelBasis::LinearRgb), RecordedChannelBasis::LinearRgb);
    EXPECT_EQ(an::linearChannelBasisOf(rgb, RecordedChannelBasis::DisplayRgb), RecordedChannelBasis::LinearRgb);
    EXPECT_EQ(an::linearChannelBasisOf(rgba, RecordedChannelBasis::CameraRgb), RecordedChannelBasis::Unknown);

    // What was recorded decides first after a stretch; the description decides only when it names nothing.
    EXPECT_EQ(an::nonlinearChannelBasisOf(mono, RecordedChannelBasis::CameraRgb), RecordedChannelBasis::DisplayRgb);
    EXPECT_EQ(an::nonlinearChannelBasisOf(rgb, RecordedChannelBasis::Monochrome), RecordedChannelBasis::Monochrome);
    EXPECT_EQ(an::nonlinearChannelBasisOf(rgb, RecordedChannelBasis::Unknown), RecordedChannelBasis::DisplayRgb);
    EXPECT_EQ(an::nonlinearChannelBasisOf(mosaic, RecordedChannelBasis::RawCfa), RecordedChannelBasis::Monochrome);
    EXPECT_EQ(an::nonlinearChannelBasisOf(rgba, RecordedChannelBasis::RawCfa), RecordedChannelBasis::RawCfa);
}

TEST(ProcessingStateCardsTest, ProcessesLoadersAndPeopleEachStateTheRecordTheirWay)
{
    an::ProcessingStateRecord before = fullRecord();
    before.colorCalibration = RecordedColorCalibration::Photometric;

    const an::ProcessingStateRecord linear = an::linearState(before, RecordedChannelBasis::CameraRgb, "demosaic");
    EXPECT_EQ(linear.linearity, RecordedLinearity::Linear);
    EXPECT_EQ(linear.channelBasis, RecordedChannelBasis::CameraRgb);
    EXPECT_EQ(linear.assignmentSource, StateAssignmentSource::Process);
    EXPECT_EQ(linear.operation, "demosaic");
    EXPECT_EQ(linear.colorCalibration, before.colorCalibration);
    EXPECT_EQ(linear.rawWhiteBalance, before.rawWhiteBalance);
    EXPECT_EQ(linear.attestation, before.attestation);

    const an::ProcessingStateRecord stretched =
        an::nonlinearState(linear, describe(3u, an::ChannelSchema::Rgb), "histogram-mtf");
    EXPECT_EQ(stretched.linearity, RecordedLinearity::Nonlinear);
    EXPECT_EQ(stretched.channelBasis, RecordedChannelBasis::DisplayRgb);
    EXPECT_EQ(stretched.operation, "histogram-mtf");
    EXPECT_EQ(an::spectrophotometricInputProblem(stretched), an::SpectrophotometricInputProblem::Nonlinear);

    const an::StateAttestation evidence{
        .sourcePath = "imports/m31.tif", .sourceHash = "sha256:0123", .attestedBy = "me"};
    const an::ProcessingStateRecord attested =
        an::attestedState(before, RecordedLinearity::Linear, RecordedChannelBasis::LinearRgb, evidence);
    EXPECT_EQ(attested.assignmentSource, StateAssignmentSource::UserAttestation);
    EXPECT_EQ(attested.operation, "");
    EXPECT_EQ(attested.attestation, evidence);
    EXPECT_EQ(attested.colorCalibration, RecordedColorCalibration::Photometric);
    EXPECT_EQ(an::spectrophotometricInputProblem(an::attestedState(
                  {}, RecordedLinearity::Linear, RecordedChannelBasis::LinearRgb, {.attestedBy = "me"})),
              an::SpectrophotometricInputProblem::IncompleteAttestation);
    EXPECT_EQ(an::spectrophotometricInputProblem(
                  an::attestedState({}, RecordedLinearity::Linear, RecordedChannelBasis::CameraRgb, evidence)),
              an::SpectrophotometricInputProblem::None);

    an::RawCameraMetadata camera;
    camera.cameraMultipliers[0] = 2.4;
    camera.cameraMultipliers[1] = 1.0;
    camera.cameraMultipliers[2] = 1.2;
    camera.cameraMultipliers[3] = 1.0;
    camera.daylightMultipliers[0] = 2.0;
    camera.daylightMultipliers[1] = 1.0;
    camera.daylightMultipliers[2] = 1.5;
    camera.daylightMultipliers[3] = 1.0;
    camera.cameraToRgb[0][0] = 0.75;
    const an::ProcessingStateRecord decoded =
        an::loaderState({}, describe(3u, an::ChannelSchema::Rgb), "RAW", &camera, RecordedWhiteBalance::CameraAsShot);
    EXPECT_EQ(decoded.linearity, RecordedLinearity::Linear);
    EXPECT_EQ(decoded.channelBasis, RecordedChannelBasis::LinearRgb);
    EXPECT_EQ(decoded.assignmentSource, StateAssignmentSource::Loader);
    EXPECT_EQ(decoded.operation, "RAW");
    ASSERT_TRUE(decoded.rawWhiteBalance.has_value());
    EXPECT_EQ(decoded.rawWhiteBalance->mode, RecordedWhiteBalance::CameraAsShot);
    EXPECT_TRUE(decoded.rawWhiteBalance->applied);
    EXPECT_EQ(decoded.rawWhiteBalanceMultipliers, (std::array<double, 4u>{2.0, 1.0, 1.5, 1.0}));
    EXPECT_EQ(decoded.cameraMatrixProvenance, "libraw:rgb_cam");

    // Without daylight gains the as-shot ones are recorded; without a matrix, what was recorded stays.
    an::RawCameraMetadata asShotOnly = camera;
    asShotOnly.daylightMultipliers[2] = 0.0;
    asShotOnly.cameraToRgb[0][0] = 0.0;
    an::ProcessingStateRecord withMatrix;
    withMatrix.cameraMatrixProvenance = "stated earlier";
    const an::ProcessingStateRecord mosaic = an::loaderState(
        withMatrix, describe(1u, an::ChannelSchema::Mono, *an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u)),
        "RAW", &asShotOnly, RecordedWhiteBalance::None);
    EXPECT_EQ(mosaic.channelBasis, RecordedChannelBasis::RawCfa);
    EXPECT_EQ(mosaic.rawWhiteBalance,
              (an::RecordedRawWhiteBalance{.mode = RecordedWhiteBalance::None, .applied = false}));
    EXPECT_EQ(mosaic.rawWhiteBalanceMultipliers, (std::array<double, 4u>{2.4, 1.0, 1.2, 1.0}));
    EXPECT_EQ(mosaic.cameraMatrixProvenance, "stated earlier");

    // A format without a camera record leaves the white balance it did not decode as it was.
    const an::ProcessingStateRecord tiff = an::loaderState(before, describe(1u, an::ChannelSchema::Mono), "TIFF");
    EXPECT_EQ(tiff.channelBasis, RecordedChannelBasis::Monochrome);
    EXPECT_EQ(tiff.operation, "TIFF");
    EXPECT_EQ(tiff.rawWhiteBalance, before.rawWhiteBalance);
    EXPECT_EQ(tiff.rawWhiteBalanceMultipliers, before.rawWhiteBalanceMultipliers);
}

TEST(ProcessingStateCardsTest, SpectrophotometricInputProblemsAreReportedInOrder)
{
    const auto problemOf = [](RecordedLinearity linearity, RecordedChannelBasis basis, StateAssignmentSource source,
                              RecordedColorCalibration calibration) {
        an::ProcessingStateRecord record;
        record.linearity = linearity;
        record.channelBasis = basis;
        record.assignmentSource = source;
        record.colorCalibration = calibration;
        return an::spectrophotometricInputProblem(record);
    };
    using Problem = an::SpectrophotometricInputProblem;
    EXPECT_EQ(problemOf(RecordedLinearity::Unknown, RecordedChannelBasis::LinearRgb, StateAssignmentSource::Process,
                        RecordedColorCalibration::None),
              Problem::UnknownLinearity);
    EXPECT_EQ(problemOf(RecordedLinearity::Nonlinear, RecordedChannelBasis::DisplayRgb,
                        StateAssignmentSource::UserAttestation, RecordedColorCalibration::Manual),
              Problem::Nonlinear);
    EXPECT_EQ(problemOf(RecordedLinearity::Linear, RecordedChannelBasis::DisplayRgb, StateAssignmentSource::Process,
                        RecordedColorCalibration::Manual),
              Problem::IncompatibleChannelBasis);
    EXPECT_EQ(problemOf(RecordedLinearity::Linear, RecordedChannelBasis::Monochrome, StateAssignmentSource::Process,
                        RecordedColorCalibration::None),
              Problem::IncompatibleChannelBasis);
    EXPECT_EQ(problemOf(RecordedLinearity::Linear, RecordedChannelBasis::LinearRgb,
                        StateAssignmentSource::UserAttestation, RecordedColorCalibration::Manual),
              Problem::IncompleteAttestation);
    EXPECT_EQ(problemOf(RecordedLinearity::Linear, RecordedChannelBasis::CameraRgb, StateAssignmentSource::Loader,
                        RecordedColorCalibration::Unknown),
              Problem::AlreadyColorCalibrated);
    EXPECT_EQ(problemOf(RecordedLinearity::Linear, RecordedChannelBasis::LinearRgb, StateAssignmentSource::Process,
                        RecordedColorCalibration::None),
              Problem::None);
}

TEST(ProcessingStateCardsTest, AColorCalibrationIsRecordedInTheStateAndTravelsAsItsCalibrationDocument)
{
    an::ColorCalibrationRecord record;
    record.calibration = RecordedColorCalibration::Spectrophotometric;
    record.algorithmVersion = "spcc-foundation-v1";
    record.responseProfileId = "resp.test.camera";
    record.whiteReferenceId = "white.d65";
    record.catalogFamilyId = "spcc_gaia_dr3_xp_sampled";
    record.catalogManifestId = "manifest.test";
    record.catalogAccessProfileId = "fixture";
    record.gains = {1.1, 1.0, 0.9};
    record.offsets = {0.01, 0.02, 0.03};

    const an::CodecMetadata calibrated =
        unwrap(an::withColorCalibrationCards(linearProduct(RecordedChannelBasis::LinearRgb), record));
    const an::ProcessingStateRecord state = an::readProcessingStateCards(calibrated);
    EXPECT_EQ(state.colorCalibration, RecordedColorCalibration::Spectrophotometric);
    EXPECT_EQ(state.linearity, RecordedLinearity::Linear);
    EXPECT_EQ(an::spectrophotometricInputProblem(state), an::SpectrophotometricInputProblem::AlreadyColorCalibrated);

    EXPECT_EQ(valueOf(calibrated, "ANCCAL"), "'spcc'");
    EXPECT_EQ(valueOf(calibrated, "ANCCGN"), "'1.100000,1.000000,0.900000'");
    EXPECT_EQ(valueOf(calibrated, "ANCCOF"), "'0.010000,0.020000,0.030000'");
    EXPECT_EQ(valueOf(calibrated, "ANRSPID"), "'resp.test.camera'");
    EXPECT_EQ(valueOf(calibrated, "ANRSPVR"), "<absent>");
    EXPECT_EQ(valueOf(calibrated, "ANINLIN"), "'linear'");
    EXPECT_EQ(valueOf(calibrated, "ANINBAS"), "'linear-rgb'");

    // The input's state is taken from the product where the record left it unknown.
    an::ColorCalibrationRecord expected = record;
    expected.inputLinearity = RecordedLinearity::Linear;
    expected.inputChannelBasis = RecordedChannelBasis::LinearRgb;
    const an::ColorCalibrationReading reading = an::readColorCalibrationCards(calibrated);
    EXPECT_EQ(reading.record, expected);
    EXPECT_TRUE(reading.status.present);
    EXPECT_TRUE(reading.status.valid);
    EXPECT_TRUE(reading.status.usedPayload);
    EXPECT_FALSE(reading.status.fellBackToCards);
    EXPECT_EQ(reading.status.problem, an::CalibrationPayloadProblem::None);

    const std::string document =
        R"({"algorithm_version":"spcc-foundation-v1","calibration_state":"spcc","catalog_access_profile_id":"fixture",)"
        R"("catalog_family_id":"spcc_gaia_dr3_xp_sampled","catalog_manifest_id":"manifest.test","gains":[1.1,1.0,0.9],)"
        R"("input_channel_basis":"linear-rgb","input_linearity":"linear","offsets":[0.01,0.02,0.03],)"
        R"("response_profile_id":"resp.test.camera","response_profile_version":"",)"
        R"("schema":"afternight.color_calibration_provenance.v1","white_reference_id":"white.d65",)"
        R"("white_reference_version":""})";
    EXPECT_EQ(an::readColorCalibrationPayload(calibrated).document, document);
    const std::string hex = hexOf(document);
    EXPECT_EQ(valueOf(calibrated, "ANCJVER"), "1");
    EXPECT_EQ(valueOf(calibrated, "ANCJCNT"), std::to_string((hex.size() + 31u) / 32u));
    EXPECT_EQ(valueOf(calibrated, "ANCJLEN"), std::to_string(document.size()));
    EXPECT_EQ(valueOf(calibrated, "ANCJCHK"), "'" + checksumOf(document) + "'");
    EXPECT_EQ(valueOf(calibrated, "ANCJ0000"), "'" + hex.substr(0u, 32u) + "'");
    const std::size_t lastChunk = (hex.size() + 31u) / 32u - 1u;
    EXPECT_EQ(valueOf(calibrated, chunkKeyword(lastChunk)), "'" + hex.substr(lastChunk * 32u) + "'");
}

TEST(ProcessingStateCardsTest, AnOverwriteClearsTheIdentitiesItDoesNotState)
{
    an::ColorCalibrationRecord first;
    first.calibration = RecordedColorCalibration::Spectrophotometric;
    first.responseProfileId = "resp.original";
    first.whiteReferenceId = "white.original";
    first.catalogFamilyId = "catalog.original";
    first.catalogManifestId = "manifest.original";
    first.catalogAccessProfileId = "profile.original";
    first.algorithmVersion = "algorithm.original";
    const an::CodecMetadata once =
        unwrap(an::withColorCalibrationCards(linearProduct(RecordedChannelBasis::LinearRgb), first));

    an::ColorCalibrationRecord second;
    second.calibration = RecordedColorCalibration::Manual;
    const an::CodecMetadata twice = unwrap(an::withColorCalibrationCards(once, second));

    const an::ColorCalibrationReading reading = an::readColorCalibrationCards(twice);
    EXPECT_EQ(reading.record.calibration, RecordedColorCalibration::Manual);
    EXPECT_TRUE(reading.record.responseProfileId.empty());
    EXPECT_TRUE(reading.record.whiteReferenceId.empty());
    EXPECT_TRUE(reading.record.catalogFamilyId.empty());
    EXPECT_TRUE(reading.record.catalogManifestId.empty());
    EXPECT_TRUE(reading.record.catalogAccessProfileId.empty());
    EXPECT_TRUE(reading.record.algorithmVersion.empty());
    for (const std::string_view keyword : {"ANRSPID", "ANWREF", "ANCATF", "ANCATM", "ANCATP", "ANALGOV"})
    {
        EXPECT_EQ(valueOf(twice, keyword), "<absent>") << keyword;
    }
    // One document is carried, whatever the length of the one before it.
    EXPECT_EQ(countOf(twice, "ANCJVER"), 1u);
    EXPECT_EQ(valueOf(twice, chunkKeyword(std::stoul(valueOf(twice, "ANCJCNT")))), "<absent>");
}

TEST(ProcessingStateCardsTest, TheDocumentIsPreferredOverACardTheHeaderWriterClipped)
{
    const an::ColorCalibrationRecord record = longSpectrophotometricRecord();
    const an::CodecMetadata calibrated =
        unwrap(an::withColorCalibrationCards(linearProduct(RecordedChannelBasis::CameraRgb), record));
    const an::CodecMetadata clipped = edited(calibrated, "ANRSPID", "'truncated-short-key-value'");

    const an::ColorCalibrationReading reading = an::readColorCalibrationCards(clipped);
    EXPECT_TRUE(reading.status.present);
    EXPECT_TRUE(reading.status.valid);
    EXPECT_TRUE(reading.status.usedPayload);
    EXPECT_FALSE(reading.status.fellBackToCards);
    EXPECT_EQ(reading.record, record);
}

TEST(ProcessingStateCardsTest, AnUnusableDocumentIsReportedByNameAndTheCardsAreRead)
{
    const an::ColorCalibrationRecord record = longSpectrophotometricRecord();
    const an::CodecMetadata calibrated =
        unwrap(an::withColorCalibrationCards(linearProduct(RecordedChannelBasis::LinearRgb), record));

    const an::ColorCalibrationReading missing =
        an::readColorCalibrationCards(edited(calibrated, "ANCJ0000", std::nullopt));
    EXPECT_TRUE(missing.status.present);
    EXPECT_FALSE(missing.status.valid);
    EXPECT_FALSE(missing.status.usedPayload);
    EXPECT_TRUE(missing.status.fellBackToCards);
    EXPECT_EQ(missing.status.problem, an::CalibrationPayloadProblem::MissingChunk);
    EXPECT_EQ(missing.status.missingChunk, 0u);
    EXPECT_EQ(missing.record, record);

    using Problem = an::CalibrationPayloadProblem;
    const std::string document = std::string(R"({"schema":")") + std::string(kSchema) +
                                 R"(","calibration_state":"pcc","gains":[1.5,1,0.5],"input_linearity":"linear"})";
    const an::CodecMetadata transported = metadataOf(transportCards(document));
    const auto problemOf = [](const an::CodecMetadata& cards) { return an::readColorCalibrationPayload(cards).status; };

    const an::CalibrationPayloadStatus good = problemOf(transported);
    EXPECT_TRUE(good.valid);
    EXPECT_EQ(good.problem, Problem::None);
    EXPECT_EQ(an::readColorCalibrationPayload(transported).document, document);
    // The checksum is compared without regard to case.
    std::string upperChecksum = checksumOf(document);
    for (char& digit : upperChecksum)
    {
        if (digit >= 'a' && digit <= 'f')
        {
            digit = static_cast<char>(digit - 'a' + 'A');
        }
    }
    EXPECT_TRUE(problemOf(edited(transported, "ANCJCHK", "'" + upperChecksum + "'")).valid);

    EXPECT_EQ(problemOf(edited(transported, "ANCJVER", "2")).problem, Problem::UnsupportedVersion);
    EXPECT_EQ(problemOf(edited(transported, "ANCJVER", std::nullopt)).problem, Problem::UnsupportedVersion);
    EXPECT_EQ(problemOf(edited(transported, "ANCJCNT", "0")).problem, Problem::InvalidChunkCount);
    EXPECT_EQ(problemOf(edited(transported, "ANCJCNT", "10001")).problem, Problem::InvalidChunkCount);
    EXPECT_EQ(problemOf(edited(transported, "ANCJCNT", "2.5")).problem, Problem::InvalidChunkCount);
    EXPECT_EQ(problemOf(edited(transported, "ANCJLEN", "'abc'")).problem, Problem::InvalidLength);
    EXPECT_EQ(problemOf(edited(transported, "ANCJCHK", "''")).problem, Problem::MissingChecksum);
    EXPECT_EQ(problemOf(edited(transported, "ANCJCHK", std::nullopt)).problem, Problem::MissingChecksum);
    const an::CalibrationPayloadStatus secondMissing = problemOf(edited(transported, "ANCJ0001", std::nullopt));
    EXPECT_EQ(secondMissing.problem, Problem::MissingChunk);
    EXPECT_EQ(secondMissing.missingChunk, 1u);
    std::string badHex = "'z" + hexOf(document).substr(1u, 31u) + "'";
    EXPECT_EQ(problemOf(edited(transported, "ANCJ0000", badHex)).problem, Problem::InvalidHex);
    EXPECT_EQ(problemOf(edited(transported, "ANCJLEN", std::to_string(document.size() + 1u))).problem,
              Problem::LengthMismatch);
    EXPECT_EQ(problemOf(edited(transported, "ANCJCHK", "'0000000000000000'")).problem, Problem::ChecksumMismatch);
    EXPECT_EQ(problemOf(metadataOf(transportCards(R"({"schema":"another.schema"})"))).problem,
              Problem::UnsupportedSchema);
    EXPECT_EQ(problemOf(metadataOf(transportCards("not a document"))).problem, Problem::InvalidDocument);
    EXPECT_EQ(problemOf(metadataOf(transportCards(std::string(R"({"schema":")") + std::string(kSchema) +
                                                  R"(","algorithm_version":7})")))
                  .problem,
              Problem::InvalidDocument);
}

TEST(ProcessingStateCardsTest, ADocumentGivenAsTextIsVerifiedWrittenAsGivenAndStatedAsCards)
{
    const std::string document =
        std::string(R"({ "schema": ")") + std::string(kSchema) +
        R"(", "calibration_state": "pcc", "gains": [1.2, 1, 0.8], "response_profile_id": "resp",)"
        R"( "input_linearity": "linear" })";
    const an::CodecMetadata stated =
        unwrap(an::withColorCalibrationPayload(linearProduct(RecordedChannelBasis::LinearRgb), document));
    EXPECT_EQ(an::readColorCalibrationPayload(stated).document, document);
    EXPECT_EQ(an::readProcessingStateCards(stated).colorCalibration, RecordedColorCalibration::Photometric);
    EXPECT_EQ(valueOf(stated, "ANCCAL"), "'pcc'");
    EXPECT_EQ(valueOf(stated, "ANCCGN"), "'1.200000,1.000000,0.800000'");
    EXPECT_EQ(valueOf(stated, "ANCCOF"), "'0.000000,0.000000,0.000000'");
    EXPECT_EQ(valueOf(stated, "ANRSPID"), "'resp'");
    EXPECT_EQ(valueOf(stated, "ANINLIN"), "'linear'");
    // The document's own input state is written, not the product's.
    EXPECT_EQ(valueOf(stated, "ANINBAS"), "'unknown'");

    EXPECT_EQ(an::withColorCalibrationPayload({}, "not a document").status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(an::withColorCalibrationPayload({}, R"({"schema":"another.schema"})").status().code(),
              an::ErrorCode::InvalidData);
    EXPECT_EQ(an::withColorCalibrationPayload({}, std::string(R"({"schema":")") + std::string(kSchema) +
                                                      R"(","calibration_state":7})")
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);
}

TEST(ProcessingStateCardsTest, WhatACardCannotCarryIsRefusedByName)
{
    an::ProcessingStateRecord badLinearity;
    badLinearity.linearity = static_cast<RecordedLinearity>(3u);
    EXPECT_EQ(an::withProcessingStateCards({}, badLinearity).status().code(), an::ErrorCode::InvalidArgument);
    an::ProcessingStateRecord badGain;
    badGain.rawWhiteBalanceMultipliers =
        std::array<double, 4u>{std::numeric_limits<double>::quiet_NaN(), 1.0, 1.0, 1.0};
    EXPECT_EQ(an::withProcessingStateCards({}, badGain).status().code(), an::ErrorCode::InvalidArgument);
    an::ProcessingStateRecord longOperation;
    longOperation.operation = std::string(an::kMaxCodecCardTextBytes - 1u, 'x');
    EXPECT_EQ(an::withProcessingStateCards({}, longOperation).status().code(), an::ErrorCode::LimitExceeded);

    an::ColorCalibrationRecord badCalibration;
    badCalibration.calibration = static_cast<RecordedColorCalibration>(9u);
    EXPECT_EQ(an::withColorCalibrationCards({}, badCalibration).status().code(), an::ErrorCode::InvalidArgument);
    an::ColorCalibrationRecord infiniteOffset;
    infiniteOffset.offsets[1] = std::numeric_limits<double>::infinity();
    EXPECT_EQ(an::withColorCalibrationCards({}, infiniteOffset).status().code(), an::ErrorCode::InvalidArgument);
    an::ColorCalibrationRecord notText;
    notText.algorithmVersion = std::string("\xff\xfe");
    EXPECT_EQ(an::withColorCalibrationCards({}, notText).status().code(), an::ErrorCode::InvalidArgument);

    std::vector<an::CodecCard> full;
    full.reserve(an::kMaxCodecCards);
    for (std::size_t i = 0u; i < an::kMaxCodecCards; ++i)
    {
        full.push_back(fits("HISTORY", "line " + std::to_string(i)));
    }
    EXPECT_EQ(an::withProcessingStateCards(metadataOf(std::move(full)), {}).status().code(),
              an::ErrorCode::LimitExceeded);
}

class ProcessingStateCardsCodecTest : public testing::Test
{
protected:
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
                    ("an-state-cards-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        auto fitsCodec = an::createFitsCodec();
        auto xisfCodec = an::createXisfCodec();
        ASSERT_TRUE(fitsCodec.hasValue());
        ASSERT_TRUE(xisfCodec.hasValue());
        std::array adapters{*fitsCodec, *xisfCodec};
        auto registry = an::CodecRegistry::create(adapters);
        ASSERT_TRUE(registry.hasValue());
        an::EngineContextOptions options;
        options.codecRegistry = *registry;
        auto created = an::EngineContext::create(options);
        ASSERT_TRUE(created.hasValue());
        engine = std::move(*created);
    }

    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }

    [[nodiscard]] std::string path(const char* name) const
    {
        const auto utf8 =
            (directory / std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(name)))).u8string();
        return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
    }

    [[nodiscard]] an::RawFrame frame()
    {
        const auto extent = *an::ImageExtent::create(2u, 2u, 1u, engine.limits());
        const an::RawSampleFormat format{sizeof(float), an::RawSampleClass::FloatingPoint};
        const auto descriptor = *an::RawFrameDescriptor::create(
            {extent, format, an::ByteOrder::Native, an::ChannelSchema::Mono, an::ColorSpace::Unspecified,
             an::NumericDomain::PhysicalScaled, an::CfaDescriptor{}, an::CoordinateOrigin::TopLeft, 1.0});
        const auto layout = *an::ImageLayout::create({extent, sizeof(float), 1u, 1u, 1u}, engine.limits());
        auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
        auto raw = an::RawFrame::create(descriptor, layout, std::move(*buffer));
        const std::array<float, 4u> samples{0.25f, 0.5f, 0.75f, 1.0f};
        std::memcpy((*raw->mutableBytes()).data(), samples.data(), sizeof(samples));
        return std::move(*raw);
    }

    /// Writes the frame carrying these cards, and returns the cards the file reads back with.
    [[nodiscard]] an::CodecMetadata roundTrip(const an::CodecMetadata& cards, const char* name)
    {
        auto execution = an::ExecutionContext::create(engine, {});
        EXPECT_TRUE(execution.hasValue());
        const std::string destination = path(name);
        // A writer states preserved cards only under an intent that keeps them, which is the default.
        const auto intent = an::OutputIntent::create({.sampleType = an::SampleType::Float32});
        EXPECT_TRUE(intent.hasValue()) << intent.status().message();
        const an::ImageMetadata metadata = an::ImageMetadata::create({}, {}, {}, {}, cards, std::nullopt);
        const auto written =
            an::writeImage(engine, *execution, {.destinationPath = destination, .intent = &*intent}, frame(), metadata);
        EXPECT_TRUE(written.hasValue()) << written.status().message();
        auto decoded = an::decodeImage(engine, *execution, {destination});
        EXPECT_TRUE(decoded.hasValue()) << decoded.status().message();
        return decoded.hasValue() ? decoded->metadata.codec() : an::CodecMetadata{};
    }

    std::filesystem::path directory;
    an::EngineContext engine;
};

TEST_F(ProcessingStateCardsCodecTest, TheRecordsSurviveAFitsAndAnXisfWriteWithTheirLongIdentities)
{
    const an::ProcessingStateRecord state = an::linearState(fullRecord(), RecordedChannelBasis::CameraRgb, "unit-test");
    const an::CodecMetadata cards = unwrap(
        an::withColorCalibrationCards(unwrap(an::withProcessingStateCards({}, state)), longSpectrophotometricRecord()));
    an::ProcessingStateRecord expectedState = state;
    expectedState.colorCalibration = RecordedColorCalibration::Spectrophotometric;

    for (const char* name : {"state.fits", "state.xisf"})
    {
        const an::CodecMetadata reread = roundTrip(cards, name);
        EXPECT_EQ(an::readProcessingStateCards(reread), expectedState) << name;
        const an::ColorCalibrationReading reading = an::readColorCalibrationCards(reread);
        EXPECT_TRUE(reading.status.valid) << name << ": " << an::calibrationPayloadProblemName(reading.status.problem);
        EXPECT_TRUE(reading.status.usedPayload) << name;
        EXPECT_EQ(reading.record, longSpectrophotometricRecord()) << name;
    }
}

} // namespace
