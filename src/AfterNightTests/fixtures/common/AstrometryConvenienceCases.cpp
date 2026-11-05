// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "AstrometryConvenienceCases.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <utility>

namespace an::tests::fixtures
{
namespace
{

// ---- Inputs.

/// A one-row UInt8 image whose samples are a text's bytes.
FixtureImage textInput(std::string name, std::string_view text)
{
    FixtureImage image;
    image.name = std::move(name);
    image.width = static_cast<std::uint32_t>(text.size());
    image.height = 1u;
    image.channels = FixtureChannels::Mono;
    image.sampleType = FixtureSampleType::UInt8;
    image.domain = FixtureDomain::SensorCode;
    image.pattern = FixturePattern::Constant;
    image.levels = {FixtureChannelLevels{}};
    for (std::size_t index = 0u; index < text.size(); ++index)
    {
        image.overrides.push_back(
            FixtureSampleOverride{.x = static_cast<std::uint32_t>(index),
                                  .y = 0u,
                                  .channel = 0u,
                                  .value = static_cast<double>(static_cast<unsigned char>(text[index]))});
    }
    return image;
}

/// A one-row Float64 image whose samples are numbers.
FixtureImage numbersInput(std::string name, const std::vector<double>& values)
{
    FixtureImage image;
    image.name = std::move(name);
    image.width = static_cast<std::uint32_t>(values.size());
    image.height = 1u;
    image.channels = FixtureChannels::Mono;
    image.sampleType = FixtureSampleType::Float64;
    image.domain = FixtureDomain::PhysicalScaled;
    image.pattern = FixturePattern::Constant;
    image.levels = {FixtureChannelLevels{}};
    for (std::size_t index = 0u; index < values.size(); ++index)
    {
        image.overrides.push_back(FixtureSampleOverride{
            .x = static_cast<std::uint32_t>(index), .y = 0u, .channel = 0u, .value = values[index]});
    }
    return image;
}

/// A one-pixel image that carries header cards.
FixtureImage cardsInput(std::string name, std::vector<FixtureCard> cards)
{
    FixtureImage image;
    image.name = std::move(name);
    image.width = 1u;
    image.height = 1u;
    image.channels = FixtureChannels::Mono;
    image.sampleType = FixtureSampleType::Float32;
    image.domain = FixtureDomain::NormalizedLinear;
    image.pattern = FixturePattern::Constant;
    image.levels = {FixtureChannelLevels{}};
    image.cards = std::move(cards);
    return image;
}

FixtureCard text(std::string keyword, std::string value)
{
    return FixtureCard{.keyword = std::move(keyword), .value = std::move(value), .kind = FixtureCardKind::Text};
}

FixtureCard real(std::string keyword, std::string value)
{
    return FixtureCard{.keyword = std::move(keyword), .value = std::move(value), .kind = FixtureCardKind::Real};
}

/// A star field of seven by seven round stars on a noisy sky, the stars scaled in each channel by its factor.
FixtureImage starFieldInput(std::string name, const std::vector<double>& factors)
{
    FixtureImage image;
    image.name = std::move(name);
    image.width = 384u;
    image.height = 384u;
    image.channels = factors.size() == 1u ? FixtureChannels::Mono : FixtureChannels::Rgb;
    image.sampleType = FixtureSampleType::Float32;
    image.domain = FixtureDomain::NormalizedLinear;
    image.pattern = FixturePattern::StarField;
    image.seed = 0x5a17u;
    for (const double factor : factors)
    {
        image.levels.push_back(FixtureChannelLevels{.base = 0.1, .alternate = factor, .amplitude = 0.002});
    }
    for (int row = 0; row < 7; ++row)
    {
        for (int column = 0; column < 7; ++column)
        {
            image.stars.push_back(FixtureStar{.x = 48.0 + 48.0 * column + 0.37 * row,
                                              .y = 48.0 + 48.0 * row + 0.29 * column,
                                              .width = 2.0,
                                              .peak = 0.5});
        }
    }
    return image;
}

// ---- Header-only solver outputs.

std::string record(std::string_view content)
{
    std::string padded(content);
    padded.resize(80u, ' ');
    return padded;
}

std::string valueRecord(std::string_view keyword, std::string_view value, std::string_view comment = {})
{
    std::string content(keyword);
    content.resize(8u, ' ');
    content += "= ";
    if (!value.empty() && value.front() == '\'')
    {
        content += value;
    }
    else
    {
        content += std::string(value.size() < 20u ? 20u - value.size() : 0u, ' ') + std::string(value);
    }
    if (!comment.empty())
    {
        content += " / " + std::string(comment);
    }
    return record(content);
}

/// What astrometry.net writes as `solution.wcs`, without its END record.
std::vector<std::string> astrometryNetRecords()
{
    return {valueRecord("SIMPLE", "T", "Standard FITS file"),
            valueRecord("BITPIX", "8", "ASCII or bytes array"),
            valueRecord("NAXIS", "0", "Minimal header"),
            valueRecord("EXTEND", "T", "There may be FITS ext"),
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
            record("COMMENT   index id: 4110")};
}

/// What ASTAP writes as `<name>.wcs`, without its END record.
std::vector<std::string> astapRecords()
{
    return {valueRecord("SIMPLE", "T", "Produced by ASTAP"),
            valueRecord("BITPIX", "8", "Bits per entry"),
            valueRecord("NAXIS", "2", "Number of dimensions"),
            valueRecord("NAXIS1", "2048", "length of x axis"),
            valueRecord("NAXIS2", "1536", "length of y axis"),
            valueRecord("EQUINOX", "2000.0", "Equinox of coordinates"),
            valueRecord("DATE-OBS", "'2024-01-15T22:13:05'", "Date of observation"),
            valueRecord("CTYPE1", "'RA---TAN'", "first parameter RA  ,  projection TANgential"),
            valueRecord("CTYPE2", "'DEC--TAN'", "second parameter DEC,  projection TANgential"),
            valueRecord("CUNIT1", "'deg     '", "Unit of coordinates"),
            valueRecord("CUNIT2", "'deg     '", "Unit of coordinates"),
            valueRecord("CRPIX1", "1.024500000000E+003", "X of reference pixel"),
            valueRecord("CRPIX2", "7.685000000000E+002", "Y of reference pixel"),
            valueRecord("CRVAL1", "8.382208300000E+001", "RA of reference pixel (deg)"),
            valueRecord("CRVAL2", "-5.391111000000E+000", "DEC of reference pixel (deg)"),
            valueRecord("CDELT1", "-6.111111111000E-004", "X pixel size (deg)"),
            valueRecord("CDELT2", "6.111111111000E-004", "Y pixel size (deg)"),
            valueRecord("CROTA1", "1.125000000000E-001", "Image twist of X axis (deg)"),
            valueRecord("CROTA2", "1.031000000000E-001", "Image twist of Y axis (deg)"),
            valueRecord("CD1_1", "-6.111099999000E-004", "CD matrix to convert (x,y) to (Ra, Dec)"),
            valueRecord("CD1_2", "1.200000000000E-006", "CD matrix to convert (x,y) to (Ra, Dec)"),
            valueRecord("CD2_1", "1.100000000000E-006", "CD matrix to convert (x,y) to (Ra, Dec)"),
            valueRecord("CD2_2", "6.111099999000E-004", "CD matrix to convert (x,y) to (Ra, Dec)"),
            valueRecord("PLTSOLVD", "T", "ASTAP internal solver"),
            record("COMMENT 7  Solved in 0.3 sec. Offset was 0.012 deg.")};
}

/// Records end to end, an END record after them, and blank records to a whole block.
std::string blocks(std::vector<std::string> records)
{
    records.push_back(record("END"));
    std::string bytes;
    for (const std::string& card : records)
    {
        bytes += card;
    }
    bytes.resize((bytes.size() + 2879u) / 2880u * 2880u, ' ');
    return bytes;
}

/// Records as text lines, each trimmed and ended by a carriage return and a line feed.
std::string lines(std::vector<std::string> records)
{
    records.push_back(record("END"));
    std::string bytes;
    for (std::string card : records)
    {
        card.erase(card.find_last_not_of(' ') + 1u);
        bytes += card + "\r\n";
    }
    return bytes;
}

std::string concatenated(const std::vector<std::string>& records)
{
    std::string bytes;
    for (const std::string& card : records)
    {
        bytes += card;
    }
    return bytes;
}

std::vector<FixtureImage> makeInputs()
{
    std::vector<FixtureImage> inputs;

    // Right ascensions and declinations as text.
    inputs.push_back(textInput("ra-hours-spaces", "05 34 31.94"));
    inputs.push_back(textInput("ra-hours-colons", "05:34:31.94"));
    inputs.push_back(textInput("ra-hours-letters", "05h34m31.94s"));
    inputs.push_back(textInput("ra-fits-string", "'05 34 31.94 '"));
    inputs.push_back(textInput("ra-decimal-degrees", "83.6331"));
    inputs.push_back(textInput("ra-decimal-minutes", "05 34.5"));
    inputs.push_back(textInput("ra-twenty-four-hours", "24 00 00"));
    inputs.push_back(textInput("ra-full-turn-degrees", "360"));
    inputs.push_back(textInput("ra-fractional-hours", "12.5 30"));
    inputs.push_back(textInput("ra-upper-case-letters", "05H34M31.94S"));
    inputs.push_back(textInput("ra-negative", "-01 00 00"));
    inputs.push_back(textInput("ra-not-an-angle", "abc"));
    inputs.push_back(textInput("dec-signed-spaces", "+22 00 52.2"));
    inputs.push_back(textInput("dec-south-spaces", "-05 23 28"));
    inputs.push_back(textInput("dec-negative-zero-degrees", "-00 30 00"));
    inputs.push_back(textInput("dec-letters", "22d00m52.2s"));
    inputs.push_back(textInput("dec-colons", "22:00:52.2"));
    inputs.push_back(textInput("dec-decimal-degrees", "-5.391111"));
    inputs.push_back(textInput("dec-fits-string", "'+41 16 09'"));
    inputs.push_back(textInput("dec-degree-sign", "22\xC2\xB0"
                                                  "00'52.2\""));
    inputs.push_back(textInput("dec-past-the-pole", "91 00 00"));

    // Angles to write.
    inputs.push_back(numbersInput("ra-orion", {83.633083}));
    inputs.push_back(numbersInput("ra-almost-a-turn", {359.9999999}));
    inputs.push_back(numbersInput("ra-zero", {0.0}));
    inputs.push_back(numbersInput("dec-orion", {-5.391111}));
    inputs.push_back(numbersInput("dec-negative-zero", {-0.0}));
    inputs.push_back(numbersInput("dec-tiny-negative", {-0.00000001}));
    inputs.push_back(numbersInput("dec-almost-the-pole", {89.9999999}));
    inputs.push_back(numbersInput("dec-andromeda", {41.269167}));

    // Pointing cards.
    inputs.push_back(cardsInput("hint-every-pair", {real("CRVAL1", "83.822083"), real("CRVAL2", "-5.391111"),
                                                    real("RA", "83.8221"), real("DEC", "-5.3911"),
                                                    text("OBJCTRA", "05 35 17.3"), text("OBJCTDEC", "-05 23 28")}));
    inputs.push_back(cardsInput("hint-object-pair",
                                {text("OBJECT", "M42"), text("OBJCTRA", "05 35 17.3"), text("OBJCTDEC", "-05 23 28")}));
    inputs.push_back(
        cardsInput("hint-passes-over", {real("CRVAL1", "10.5"), text("RA", "not an angle"), real("DEC", "12.0"),
                                        text("OBJCTRA", "25 00 00"), real("OBJCTDEC", "10.0"),
                                        text("OBJRA", "12 30 00"), text("OBJDEC", "+41 16 09")}));
    inputs.push_back(cardsInput("hint-telescope", {real("TELRA", "1.5"), real("TELDEC", "2.5")}));
    inputs.push_back(cardsInput("hint-none", {real("CRVAL1", "10.5"), real("EXPTIME", "300")}));
    inputs.push_back(cardsInput("hint-full-turn", {real("RA", "360"), real("DEC", "0")}));
    inputs.push_back(cardsInput("hint-unit-letters", {text("OBJCTRA", "05h35m17.3s"), text("OBJCTDEC", "-05d23m28s")}));

    // Optics cards.
    inputs.push_back(cardsInput("optics-focal-and-pixel", {real("FOCALLEN", "530"), real("XPIXSZ", "3.76")}));
    inputs.push_back(cardsInput("optics-drizzle-scale",
                                {real("FOCALLEN", "530"), real("XPIXSZ", "3.76"), real("D001SCAL", "0.7314")}));
    inputs.push_back(cardsInput("optics-drizzle-multiplier",
                                {real("FOCAL", "400"), real("PIXSIZE", "4.63"), real("DRIZMULT", "2")}));
    inputs.push_back(cardsInput("optics-fallback-keywords",
                                {real("FOCALLEN", "0"), real("FOCAL", "530"), real("PIXSIZE1", "3.76")}));
    inputs.push_back(cardsInput("optics-focal-only", {real("FOCALLEN", "530")}));
    inputs.push_back(cardsInput("optics-quoted-pixel", {real("FOCALLEN", "1000"), text("XPIXSZ", "4.63")}));
    inputs.push_back(cardsInput("optics-native-drizzle",
                                {real("FOCALLEN", "530"), real("XPIXSZ", "3.76"), real("D001SCAL", "0.5")}));
    inputs.push_back(cardsInput("optics-pixel-multiplier", {real("XPIXSZ", "3.76"), real("DRIZMULT", "2")}));

    // Mosaic panels as right ascension and declination pairs.
    inputs.push_back(numbersInput("panels-orion", {83.1, -5.9, 84.3, -5.1, 83.7, -4.2, 82.9, -4.8, 84.0, -6.3}));
    inputs.push_back(numbersInput("panels-across-zero-hours", {359.5, 10.0, 0.5, 10.0}));
    inputs.push_back(numbersInput("panels-around-the-pole", {0.0, 88.0, 90.0, 88.0, 180.0, 88.0, 270.0, 88.0}));
    inputs.push_back(numbersInput("panels-opposite", {0.0, 0.0, 180.0, 0.0}));
    inputs.push_back(numbersInput("panels-single", {210.802, 54.349}));

    // Observation dates.
    inputs.push_back(textInput("date-with-fraction", "'2024-01-15T22:13:05.500'"));
    inputs.push_back(textInput("date-gaia-epoch", "'2016-01-01T12:00:00'"));
    inputs.push_back(textInput("date-millennium", "'2000-01-01T00:00:00'"));
    inputs.push_back(textInput("date-not-a-date", "'not a date'"));

    // Header-only solver outputs.
    inputs.push_back(textInput("header-astrometry-net", blocks(astrometryNetRecords())));
    inputs.push_back(textInput("header-astap", blocks(astapRecords())));
    inputs.push_back(textInput("header-astrometry-net-lines", lines(astrometryNetRecords())));
    inputs.push_back(textInput("header-truncated", concatenated(astrometryNetRecords())));
    std::vector<std::string> afterEnd = astrometryNetRecords();
    afterEnd.push_back(record("END"));
    afterEnd.push_back(valueRecord("JUNK1", "1"));
    afterEnd.push_back(valueRecord("IMAGEW", "9999"));
    inputs.push_back(textInput("header-cards-after-end", blocks(afterEnd)));
    std::vector<std::string> duplicated = astrometryNetRecords();
    duplicated.push_back(valueRecord("CRVAL1", "84.0000000000", "a second reference"));
    inputs.push_back(textInput("header-duplicated-keyword", blocks(duplicated)));

    // Star fields for the detection policy.
    inputs.push_back(starFieldInput("stars-every-channel", {1.0, 1.0, 1.0}));
    inputs.push_back(starFieldInput("stars-red-only", {1.0, 0.0, 0.0}));
    inputs.push_back(starFieldInput("stars-none", {0.0, 0.0, 0.0}));
    inputs.push_back(starFieldInput("stars-mono", {1.0}));
    return inputs;
}

// ---- Cases.

constexpr const char* kParseSource = "src/AfterNightControls/app/PlateSolveService.cpp:191";
constexpr const char* kFormatSource =
    "src/AfterNightProcesses/processes/ImageIntegration/workflow/stages/StackMasterWcs.cpp:458";
constexpr const char* kHintSource = "src/AfterNightControls/app/PlateSolveService.cpp:191";
constexpr const char* kObservedSource =
    "src/AfterNightProcesses/processes/ImageIntegration/workflow/stages/StackMasterWcs.cpp:622";
constexpr const char* kScaleSource = "src/AfterNightControls/app/PlateSolveService.cpp:233";
constexpr const char* kFocalSource = "src/AfterNightControls/app/PlateSolveService.cpp:369";
constexpr const char* kMosaicSource =
    "src/AfterNightProcesses/processes/ImageIntegration/workflow/stages/StackMasterWcs.cpp:656";
constexpr const char* kEpochSource = "src/AfterNightControls/app/ObservationEpochUtils.h:28";
constexpr const char* kHeaderSource = "src/AfterNightControls/app/PlateSolveUtils.cpp:192";
constexpr const char* kDetectionSource = "src/AfterNightControls/controls/ImageViewFiles.cpp:1470";

AstrometryCase exact(std::string operation, std::string input, AstrometryCaseKind kind, std::string description,
                     const char* source)
{
    AstrometryCase item;
    item.operation = std::move(operation);
    item.input = std::move(input);
    item.kind = kind;
    item.text = std::move(description);
    item.source = source;
    return item;
}

/// The largest difference a result the host's trigonometric functions round may show between two compilers' libraries,
/// in degrees: the sum of unit vectors goes through `atan2` and `asin`, whose last place the C runtimes do not round
/// alike, and a few such places at 360 degrees stay below a nanoarcsecond.
constexpr double kTrigonometricLastPlaceDegrees = 1.0e-12;

/// A case held to a tolerance: a computation whose answer the host's mathematical library rounds, so two builds of one
/// source can differ in its last place.
AstrometryCase withinTolerance(AstrometryCase item, double absolute, std::string note)
{
    item.classification = ExpectationClass::Tolerance;
    item.absoluteTolerance = absolute;
    item.relativeTolerance = 0.0;
    item.note = std::move(note);
    return item;
}

AstrometryCase accepted(AstrometryCase item, std::string reason, double bound, std::string note)
{
    item.classification = ExpectationClass::Accepted;
    item.acceptedReason = std::move(reason);
    item.acceptedBound = bound;
    item.note = std::move(note);
    return item;
}

std::vector<AstrometryCase> makeCases()
{
    std::vector<AstrometryCase> cases;
    const auto parse = [&](const char* operation, AstrometryCaseKind kind, const char* input) {
        cases.push_back(exact(operation, input, kind,
                              kind == AstrometryCaseKind::ParseRightAscension
                                  ? "the plate solve reads a right ascension from text, decimal degrees or hours"
                                  : "the plate solve reads a declination from text, decimal degrees or signed degrees",
                              kParseSource));
    };
    const auto acceptParse = [&](const char* operation, AstrometryCaseKind kind, const char* input, const char* reason,
                                 double bound, const char* note) {
        parse(operation, kind, input);
        cases.back() = accepted(cases.back(), reason, bound, note);
    };

    constexpr AstrometryCaseKind kRa = AstrometryCaseKind::ParseRightAscension;
    constexpr AstrometryCaseKind kDec = AstrometryCaseKind::ParseDeclination;
    for (const char* input : {"ra-hours-spaces", "ra-hours-colons", "ra-hours-letters", "ra-fits-string",
                              "ra-decimal-degrees", "ra-decimal-minutes", "ra-negative", "ra-not-an-angle"})
    {
        parse("sexagesimal.parse-ra", kRa, input);
    }
    acceptParse("sexagesimal.parse-ra", kRa, "ra-twenty-four-hours",
                "twenty-four hours is the start of the next turn, so it reads as 0 degrees rather than 360", 360.0,
                "the whole turn the record keeps");
    acceptParse("sexagesimal.parse-ra", kRa, "ra-full-turn-degrees",
                "360 decimal degrees reads as 0, the start of the half-open turn every direction is folded into", 360.0,
                "the whole turn the record keeps");
    acceptParse("sexagesimal.parse-ra", kRa, "ra-fractional-hours",
                "a fraction in a field another field follows is refused rather than added to it", 195.0 + 1.0,
                "the record's reading, 195 degrees, against a refusal recorded as zero, and one for the flag");
    acceptParse("sexagesimal.parse-ra", kRa, "ra-upper-case-letters",
                "the unit letters are read in either case; the record refuses the upper-case spelling", 83.633083 + 1.0,
                "the engine's reading against the record's refusal, and one for the flag");
    for (const char* input : {"dec-signed-spaces", "dec-south-spaces", "dec-negative-zero-degrees", "dec-letters",
                              "dec-colons", "dec-decimal-degrees", "dec-fits-string", "dec-past-the-pole"})
    {
        parse("sexagesimal.parse-dec", kDec, input);
    }
    acceptParse("sexagesimal.parse-dec", kDec, "dec-degree-sign",
                "the degree sign and the arcminute and arcsecond marks separate fields; the record refuses them",
                22.0145 + 1.0, "the engine's reading against the record's refusal, and one for the flag");

    for (const char* input : {"ra-orion", "ra-almost-a-turn", "ra-zero"})
    {
        cases.push_back(exact("sexagesimal.format-ra", input, AstrometryCaseKind::FormatRightAscension,
                              "the mosaic writer spells a right ascension as hours, minutes and seconds to three "
                              "decimals",
                              kFormatSource));
    }
    for (const char* input : {"dec-orion", "dec-almost-the-pole", "dec-andromeda"})
    {
        cases.push_back(exact("sexagesimal.format-dec", input, AstrometryCaseKind::FormatDeclination,
                              "the mosaic writer spells a declination as signed degrees, minutes and seconds to three "
                              "decimals",
                              kFormatSource));
    }
    for (const char* input : {"dec-negative-zero", "dec-tiny-negative"})
    {
        cases.push_back(accepted(
            exact("sexagesimal.format-dec", input, AstrometryCaseKind::FormatDeclination,
                  "the mosaic writer spells a declination as signed degrees, minutes and seconds to three decimals",
                  kFormatSource),
            "a declination that rounds to zero is written with a plus sign; the record has a minus for any "
            "negative angle, negative zero included",
            2.0, "the two sign characters' codes, 45 and 43"));
    }

    for (const char* input : {"hint-every-pair", "hint-object-pair", "hint-passes-over", "hint-telescope", "hint-none",
                              "hint-unit-letters"})
    {
        cases.push_back(exact("hint.plate-solve", input, AstrometryCaseKind::PlateSolveHint,
                              "the plate solve takes its position hint from the first pair of cards that reads",
                              kHintSource));
    }
    cases.push_back(accepted(
        exact("hint.plate-solve", "hint-full-turn", AstrometryCaseKind::PlateSolveHint,
              "the plate solve takes its position hint from the first pair of cards that reads", kHintSource),
        "a right ascension of 360 is folded to 0, as every direction is", 360.0, "the whole turn the record keeps"));
    for (const char* input : {"hint-every-pair", "hint-object-pair", "hint-none"})
    {
        cases.push_back(exact("hint.observed", input, AstrometryCaseKind::ObservedPointing,
                              "a mosaic reads each panel's observed pointing from the cards, never a solution's "
                              "reference",
                              kObservedSource));
    }

    const std::string constantReason =
        "the pixel scale is the small-angle relation with 180 times 3600 over pi arcseconds a radian; the record "
        "multiplies by 206.265, that constant to six figures";
    for (const char* input :
         {"optics-focal-and-pixel", "optics-drizzle-multiplier", "optics-fallback-keywords", "optics-quoted-pixel"})
    {
        cases.push_back(accepted(exact("scale.hint", input, AstrometryCaseKind::ScaleHint,
                                       "the plate solve's scale hint from a drizzled scale, or the pixel size behind "
                                       "the focal length",
                                       kScaleSource),
                                 constantReason, 1.0e-5,
                                 "a part in a million of scales below ten arcseconds a pixel, the constants' ratio"));
    }
    for (const char* input : {"optics-drizzle-scale", "optics-focal-only"})
    {
        cases.push_back(exact("scale.hint", input, AstrometryCaseKind::ScaleHint,
                              "the plate solve's scale hint from a drizzled scale, or the pixel size behind the focal "
                              "length",
                              kScaleSource));
    }
    const auto focal = [&](const char* input, double solvedScale, bool exactly) {
        AstrometryCase item =
            exact("scale.derived-focal", input, AstrometryCaseKind::DerivedFocalLength,
                  "a solved frame's focal length from its scale and the drizzled pixel size", kFocalSource);
        item.solvedScale = solvedScale;
        cases.push_back(exactly ? item
                                : accepted(item, constantReason, 1.0e-3,
                                           "a part in a million of focal lengths below a thousand millimetres"));
    };
    focal("optics-focal-and-pixel", 1.4633, false);
    focal("optics-pixel-multiplier", 0.73, false);
    focal("optics-native-drizzle", 0.52, false);
    focal("optics-focal-only", 1.4633, true);

    for (const char* input :
         {"panels-orion", "panels-across-zero-hours", "panels-around-the-pole", "panels-opposite", "panels-single"})
    {
        cases.push_back(withinTolerance(
            exact("mosaic.centre", input, AstrometryCaseKind::MosaicCentre,
                  "a mosaic is centred on the mean of its panels' unit vectors", kMosaicSource),
            kTrigonometricLastPlaceDegrees,
            "the centre goes through the host's atan2 and asin, which the MSVC and glibc runtimes round one place "
            "apart; the record and the engine agree on one build"));
    }

    for (const char* input : {"date-with-fraction", "date-gaia-epoch", "date-millennium"})
    {
        cases.push_back(
            accepted(exact("epoch.year", input, AstrometryCaseKind::EpochYear,
                           "the catalog's proper motions are carried to the Julian year of DATE-OBS", kEpochSource),
                     "the Julian year counts from J2000.0, 2000-01-01T12:00:00; the record counts from "
                     "midnight, half a day earlier",
                     0.0014, "half a day is 0.00137 of a Julian year"));
    }
    cases.push_back(exact("epoch.year", "date-not-a-date", AstrometryCaseKind::EpochYear,
                          "the catalog's proper motions are carried to the Julian year of DATE-OBS", kEpochSource));

    for (const char* input : {"header-astrometry-net", "header-astap", "header-astrometry-net-lines"})
    {
        cases.push_back(exact("header.cards", input, AstrometryCaseKind::HeaderCards,
                              "the cards of a solver's header-only output, read as records or as lines",
                              kHeaderSource));
    }
    cases.push_back(
        accepted(exact("header.cards", "header-truncated", AstrometryCaseKind::HeaderCards,
                       "the cards of a solver's header-only output, read as records or as lines", kHeaderSource),
                 "a header without an END record is refused, since its last cards may be missing; the "
                 "record holds whatever cards were there",
                 2049.0, "every recorded number against a refusal, the largest an image width"));
    cases.push_back(
        accepted(exact("header.cards", "header-cards-after-end", AstrometryCaseKind::HeaderCards,
                       "the cards of a solver's header-only output, read as records or as lines", kHeaderSource),
                 "nothing after the END record is read; the record reads on and lets a later card replace "
                 "an earlier one",
                 7952.0, "the width a card after END restated, 9999 against 2048"));
    cases.push_back(
        accepted(exact("header.cards", "header-duplicated-keyword", AstrometryCaseKind::HeaderCards,
                       "the cards of a solver's header-only output, read as records or as lines", kHeaderSource),
                 "every card is kept in order, and the bridge refuses two values for one keyword; the "
                 "record keeps the last card of a keyword and drops the others",
                 1.0, "one more card, and the first reference against the last, 0.18 degrees apart"));

    for (const char* input : {"stars-every-channel", "stars-red-only", "stars-none", "stars-mono"})
    {
        cases.push_back(exact("detection.policy", input, AstrometryCaseKind::DetectionPolicy,
                              "the overlay detects in green, then red when green gives fewer than 25 stars, and "
                              "keeps the channel that gave more",
                              kDetectionSource));
    }
    return cases;
}

} // namespace

const std::vector<FixtureImage>& astrometryConvenienceInputs()
{
    static const std::vector<FixtureImage> inputs = makeInputs();
    return inputs;
}

const FixtureImage* astrometryConvenienceInput(std::string_view name)
{
    for (const FixtureImage& image : astrometryConvenienceInputs())
    {
        if (image.name == name)
        {
            return &image;
        }
    }
    return nullptr;
}

const std::vector<AstrometryCase>& astrometryConvenienceCases()
{
    static const std::vector<AstrometryCase> cases = makeCases();
    return cases;
}

const AstrometryCase* astrometryConvenienceCase(std::string_view id)
{
    for (const AstrometryCase& item : astrometryConvenienceCases())
    {
        if (item.id() == id)
        {
            return &item;
        }
    }
    return nullptr;
}

std::string astrometryInputText(const FixtureSamples& samples)
{
    std::string text;
    text.reserve(samples.width());
    for (std::uint32_t x = 0u; x < samples.width(); ++x)
    {
        text.push_back(static_cast<char>(static_cast<unsigned char>(samples.value(x, 0u, 0u))));
    }
    return text;
}

std::vector<double> astrometryInputNumbers(const FixtureSamples& samples)
{
    std::vector<double> numbers;
    numbers.reserve(samples.width());
    for (std::uint32_t x = 0u; x < samples.width(); ++x)
    {
        numbers.push_back(samples.value(x, 0u, 0u));
    }
    return numbers;
}

std::string astrometryCardValue(const FixtureCard& card)
{
    if (card.kind != FixtureCardKind::Text)
    {
        return card.value;
    }
    std::string value = "'";
    for (const char character : card.value)
    {
        value.push_back(character);
        if (character == '\'')
        {
            value.push_back('\'');
        }
    }
    value.push_back('\'');
    return value;
}

std::optional<double> astrometryCardNumber(std::string_view value) noexcept
{
    const auto trim = [](std::string_view text) {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        {
            text.remove_prefix(1u);
        }
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        {
            text.remove_suffix(1u);
        }
        return text;
    };
    value = trim(value);
    if (value.size() >= 2u && value.front() == '\'' && value.back() == '\'')
    {
        value = trim(value.substr(1u, value.size() - 2u));
    }
    if (!value.empty() && value.front() == '+')
    {
        value.remove_prefix(1u);
    }
    double number = 0.0;
    const std::from_chars_result parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        !std::isfinite(number))
    {
        return std::nullopt;
    }
    return number;
}

FixtureSamples astrometryRow(const std::vector<double>& values)
{
    FixtureSamples samples(static_cast<std::uint32_t>(values.size()), 1u, 1u, FixtureSampleType::Float64);
    for (std::uint32_t index = 0u; index < values.size(); ++index)
    {
        samples.setValue(index, 0u, 0u, values[index]);
    }
    return samples;
}

FixtureSamples astrometryTextRow(std::string_view text)
{
    FixtureSamples samples(static_cast<std::uint32_t>(text.size()), 1u, 1u, FixtureSampleType::UInt8);
    for (std::uint32_t index = 0u; index < text.size(); ++index)
    {
        samples.setValue(index, 0u, 0u, static_cast<double>(static_cast<unsigned char>(text[index])));
    }
    return samples;
}

} // namespace an::tests::fixtures
