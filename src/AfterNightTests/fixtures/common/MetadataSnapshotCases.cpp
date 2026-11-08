// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "MetadataSnapshotCases.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <system_error>
#include <utility>

namespace an::tests::fixtures
{
namespace
{

// ---- Inputs.

FixtureCard text(std::string keyword, std::string value)
{
    return FixtureCard{.keyword = std::move(keyword), .value = std::move(value), .kind = FixtureCardKind::Text};
}

FixtureCard real(std::string keyword, std::string value)
{
    return FixtureCard{.keyword = std::move(keyword), .value = std::move(value), .kind = FixtureCardKind::Real};
}

FixtureCard integer(std::string keyword, std::string value)
{
    return FixtureCard{.keyword = std::move(keyword), .value = std::move(value), .kind = FixtureCardKind::Integer};
}

/// A one-pixel frame that carries a header's cards.
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
    image.levels = {FixtureChannelLevels{.base = 0.25}};
    image.cards = std::move(cards);
    return image;
}

/// A tangent-plane solution's cards with its reference at a right ascension.
std::vector<FixtureCard> solutionCards(std::string rightAscension)
{
    return {text("RADESYS", "ICRS"),    text("CTYPE1", "RA---TAN"),
            text("CTYPE2", "DEC--TAN"), real("CRVAL1", std::move(rightAscension)),
            real("CRVAL2", "-5.391"),   real("CRPIX1", "1.0"),
            real("CRPIX2", "1.0"),      real("CD1_1", "-6.1E-04"),
            real("CD1_2", "0.0"),       real("CD2_1", "0.0"),
            real("CD2_2", "6.1E-04")};
}

/// A luminance light of the Orion nebula with everything a capture program writes about it.
std::vector<FixtureCard> luminanceCards(std::string dateObs, std::string airmass, std::string rightAscension)
{
    std::vector<FixtureCard> cards{text("OBJECT", "M 42"),
                                   text("INSTRUME", "ZWO ASI2600MM Pro"),
                                   text("TELESCOP", "RedCat 51"),
                                   text("FILTER", "L"),
                                   real("CCD-TEMP", "-10.0"),
                                   text("IMAGETYP", "Light Frame"),
                                   integer("GAIN", "100"),
                                   integer("OFFSET", "50"),
                                   real("SITELAT", "40.1"),
                                   real("FOCALLEN", "250.0"),
                                   text("DATE-OBS", std::move(dateObs)),
                                   real("AIRMASS", std::move(airmass)),
                                   real("EXPTIME", "300.0"),
                                   text("ANLIN", "LINEAR")};
    for (FixtureCard& card : solutionCards(std::move(rightAscension)))
    {
        cards.push_back(std::move(card));
    }
    return cards;
}

/// An image of one sample type whose samples cover its range, for a history step.
FixtureImage historyInput(std::string name, FixtureSampleType sampleType, FixtureChannels channels, std::uint32_t width,
                          std::uint32_t height)
{
    FixtureImage image;
    image.name = std::move(name);
    image.width = width;
    image.height = height;
    image.channels = channels;
    image.sampleType = sampleType;
    image.domain = isFloating(sampleType) ? FixtureDomain::NormalizedLinear : FixtureDomain::SensorCode;
    image.pattern = FixturePattern::GradientNoise;
    image.seed = 0x5e7a1u + static_cast<std::uint64_t>(sampleType);
    const double span = isFloating(sampleType) ? 1.0 : highestOf(sampleType) - lowestOf(sampleType);
    const double base = isFloating(sampleType) ? 0.0 : lowestOf(sampleType);
    for (std::uint32_t channel = 0u; channel < channelCountOf(channels); ++channel)
    {
        image.levels.push_back(FixtureChannelLevels{.base = base + span * (0.1 + 0.2 * channel),
                                                    .perColumn = span * 0.05,
                                                    .perRow = span * 0.03,
                                                    .amplitude = span * 0.01});
    }
    image.overrides.push_back(FixtureSampleOverride{.x = 0u, .y = 0u, .channel = 0u, .value = lowestOf(sampleType)});
    image.overrides.push_back(
        FixtureSampleOverride{.x = width - 1u, .y = height - 1u, .channel = 0u, .value = highestOf(sampleType)});
    if (isFloating(sampleType))
    {
        const double infinity = std::numeric_limits<double>::infinity();
        image.overrides.push_back(FixtureSampleOverride{.x = 1u, .y = 0u, .channel = 0u, .value = -0.0});
        image.overrides.push_back(FixtureSampleOverride{.x = 2u, .y = 0u, .channel = 0u, .value = infinity});
        image.overrides.push_back(FixtureSampleOverride{.x = 3u, .y = 0u, .channel = 0u, .value = -infinity});
        image.overrides.push_back(
            FixtureSampleOverride{.x = 4u,
                                  .y = 0u,
                                  .channel = 0u,
                                  .value = sampleType == FixtureSampleType::Float32
                                               ? static_cast<double>(std::numeric_limits<float>::denorm_min())
                                               : std::numeric_limits<double>::denorm_min()});
    }
    return image;
}

std::vector<FixtureImage> makeInputs()
{
    std::vector<FixtureImage> inputs;

    // Frame kinds, as capture programs spell them.
    inputs.push_back(cardsInput("kind-light-frame", {text("IMAGETYP", "Light Frame")}));
    inputs.push_back(cardsInput("kind-master-dark", {text("IMAGETYP", "Master Dark")}));
    inputs.push_back(cardsInput("kind-flat-field", {text("IMAGETYP", "Flat Field")}));
    inputs.push_back(cardsInput("kind-bias-frame", {text("IMAGETYP", "Bias Frame")}));
    inputs.push_back(cardsInput("kind-zero", {text("IMAGETYP", "Zero")}));
    inputs.push_back(cardsInput("kind-science", {text("IMAGETYP", "SCIENCE")}));
    inputs.push_back(cardsInput("kind-dark-flat", {text("IMAGETYP", "DarkFlat")}));
    inputs.push_back(cardsInput("kind-offset", {text("IMAGETYP", "Offset")}));
    inputs.push_back(cardsInput("kind-master-offset", {text("IMAGETYP", "MasterOffset")}));
    inputs.push_back(cardsInput("kind-indi-frame", {text("FRAME", "Light")}));
    inputs.push_back(cardsInput("kind-indi-and-imagetyp", {text("IMAGETYP", "Dark"), text("FRAME", "Light")}));
    inputs.push_back(cardsInput("kind-unrecognised", {text("IMAGETYP", "Calibration")}));
    inputs.push_back(cardsInput("kind-unrecognised-then-frame", {text("IMAGETYP", "Test"), text("FRAME", "Flat")}));
    inputs.push_back(cardsInput("kind-none", {text("OBJECT", "M 42")}));

    // Combination sources.
    inputs.push_back(cardsInput("combine-luminance-1", luminanceCards("2026-01-10T20:00:00", "1.21", "83.822")));
    inputs.push_back(cardsInput("combine-luminance-2", luminanceCards("2026-01-10T20:05:10", "1.19", "83.823")));
    inputs.push_back(cardsInput("combine-colour", [] {
        std::vector<FixtureCard> cards{text("OBJECT", "M 42"),        text("INSTRUME", "Canon EOS Ra"),
                                       text("TELESCOP", "RedCat 51"), text("FILTER", "RGB"),
                                       real("CCD-TEMP", "5.0"),       text("IMAGETYP", "Light Frame"),
                                       integer("GAIN", "800"),        real("SITELAT", "40.1"),
                                       real("FOCALLEN", "250.0"),     text("DATE-OBS", "2026-01-11T21:00:00"),
                                       real("AIRMASS", "1.30"),       real("EXPTIME", "120.0"),
                                       text("ANLIN", "LINEAR")};
        for (FixtureCard& card : solutionCards("83.825"))
        {
            cards.push_back(std::move(card));
        }
        return cards;
    }()));
    inputs.push_back(cardsInput("combine-ha-1", {text("OBJECT", "NGC 7000"), text("FILTER", "Ha"),
                                                 text("IMAGETYP", "Light"), real("EXPTIME", "600.0")}));
    inputs.push_back(cardsInput("combine-ha-2", {text("OBJECT", "NGC 7000"), text("FILTER", "Ha"),
                                                 text("IMAGETYP", "Light"), real("EXPTIME", "600.0")}));
    inputs.push_back(cardsInput("combine-unexposed-1", {text("OBJECT", "NGC 7000")}));
    inputs.push_back(cardsInput("combine-unexposed-2", {text("OBJECT", "NGC 7000")}));
    inputs.push_back(cardsInput("combine-exposure-card", {text("OBJECT", "M 31"), real("EXPOSURE", "60.0")}));

    // History steps: each resident type, two colour images and a mosaic.
    inputs.push_back(historyInput("history-u8", FixtureSampleType::UInt8, FixtureChannels::Mono, 7u, 5u));
    inputs.push_back(historyInput("history-i8", FixtureSampleType::Int8, FixtureChannels::Mono, 7u, 5u));
    inputs.push_back(historyInput("history-u16", FixtureSampleType::UInt16, FixtureChannels::Mono, 7u, 5u));
    inputs.push_back(historyInput("history-i16", FixtureSampleType::Int16, FixtureChannels::Mono, 7u, 5u));
    inputs.push_back(historyInput("history-u32", FixtureSampleType::UInt32, FixtureChannels::Mono, 7u, 5u));
    inputs.push_back(historyInput("history-i32", FixtureSampleType::Int32, FixtureChannels::Mono, 7u, 5u));
    inputs.push_back(historyInput("history-f32", FixtureSampleType::Float32, FixtureChannels::Mono, 7u, 5u));
    inputs.push_back(historyInput("history-f64", FixtureSampleType::Float64, FixtureChannels::Mono, 7u, 5u));
    inputs.push_back(historyInput("history-rgb-f32", FixtureSampleType::Float32, FixtureChannels::Rgb, 6u, 4u));
    inputs.push_back(historyInput("history-rgb-u16", FixtureSampleType::UInt16, FixtureChannels::Rgb, 6u, 4u));
    FixtureImage mosaic = historyInput("history-mosaic-u16", FixtureSampleType::UInt16, FixtureChannels::Mono, 6u, 4u);
    mosaic.cfa = FixtureCfa::Rggb;
    inputs.push_back(std::move(mosaic));
    return inputs;
}

// ---- Cases.

constexpr const char* kKindSource = "src/AfterNightProcesses/processes/ImageIntegration/core/FrameDetection.cpp:59";
constexpr const char* kKindText =
    "the workflow reads a frame's kind from IMAGETYP, bias and zero before dark, flat, then light and science";
constexpr const char* kCombineSource = "src/AfterNightEngine/include/AfterNightEngine/IO/CombinedMetadata.h:91";
constexpr const char* kCombineText =
    "a combination keeps its first source's header, sums the exposures, keeps a value every source shares, removes "
    "the filter and clears the processing state";
constexpr const char* kHistorySource = "src/AfterNightControls/controls/ImageHistoryBackupQueue.cpp:102";
constexpr const char* kHistoryText =
    "a history step keeps the image's samples in a file and restores them before an undo";

MetadataSnapshotCase exact(std::string operation, std::string input, MetadataSnapshotCaseKind kind, const char* text,
                           const char* source, std::vector<std::string> others = {})
{
    MetadataSnapshotCase item;
    item.operation = std::move(operation);
    item.input = std::move(input);
    item.others = std::move(others);
    item.kind = kind;
    item.text = text;
    item.source = source;
    return item;
}

MetadataSnapshotCase accepted(MetadataSnapshotCase item, std::string reason, double bound,
                              std::vector<std::uint32_t> differing, std::string note)
{
    item.classification = ExpectationClass::Accepted;
    item.acceptedReason = std::move(reason);
    item.acceptedBound = bound;
    item.differing = std::move(differing);
    item.note = std::move(note);
    return item;
}

/// The record's sample holding whether the header states the keyword at this index of `kCombinationKeywords`.
constexpr std::uint32_t stated(std::uint32_t keyword) noexcept
{
    return 1u + 2u * keyword;
}

/// The record's sample holding the number that keyword's value states.
constexpr std::uint32_t number(std::uint32_t keyword) noexcept
{
    return 2u + 2u * keyword;
}

constexpr std::uint32_t kFilter = 3u;
constexpr std::uint32_t kAirmass = 11u;
constexpr std::uint32_t kExposureTime = 12u;

std::vector<MetadataSnapshotCase> makeCases()
{
    std::vector<MetadataSnapshotCase> cases;
    constexpr MetadataSnapshotCaseKind kKind = MetadataSnapshotCaseKind::FrameKind;
    for (const char* input :
         {"kind-light-frame", "kind-master-dark", "kind-flat-field", "kind-bias-frame", "kind-zero", "kind-science",
          "kind-dark-flat", "kind-offset", "kind-indi-and-imagetyp", "kind-unrecognised", "kind-none"})
    {
        cases.push_back(exact("frame.kind", input, kKind, kKindText, kKindSource));
    }
    cases.push_back(accepted(exact("frame.kind", "kind-master-offset", kKind, kKindText, kKindSource),
                             "OFFSET names a bias wherever it appears in the value; the record maps only a value "
                             "that is OFFSET and nothing else",
                             1.0, {0u}, "a bias, 0, against no kind, -1"));
    cases.push_back(accepted(exact("frame.kind", "kind-indi-frame", kKind, kKindText, kKindSource),
                             "the FRAME card INDI drivers write is read after IMAGETYP; the record reads IMAGETYP only",
                             4.0, {0u}, "a light, 3, against no kind, -1"));
    cases.push_back(accepted(exact("frame.kind", "kind-unrecognised-then-frame", kKind, kKindText, kKindSource),
                             "an IMAGETYP that names no kind is passed over for the FRAME card; the record reads "
                             "IMAGETYP only",
                             3.0, {0u}, "a flat, 2, against no kind, -1"));

    constexpr MetadataSnapshotCaseKind kCombine = MetadataSnapshotCaseKind::Combination;
    const std::string perFileReason =
        "a card is kept only when every source states it the same way, the filter stays when every source shares "
        "it, and the combination states the integration it holds - its sources' summed - as EXPTIME; the record "
        "keeps its first source's airmass and EXPTIME, which then contradict the EXPOSURE it states, and always "
        "removes the filter";
    // The observation time is the earliest source's, which in every case here is the first source's, as the record's.
    cases.push_back(accepted(exact("combine.metadata", "combine-luminance-1", kCombine, kCombineText, kCombineSource,
                                   {"combine-luminance-2"}),
                             perFileReason, 300.0,
                             {stated(kFilter), stated(kAirmass), number(kAirmass), number(kExposureTime)},
                             "presence flags differ by one; the first source's airmass against no number; the two "
                             "sources' 600 seconds against the first's 300"));
    cases.push_back(accepted(
        exact("combine.metadata", "combine-luminance-1", kCombine, kCombineText, kCombineSource, {"combine-colour"}),
        perFileReason, 120.0, {stated(kAirmass), number(kAirmass), number(kExposureTime)},
        "a presence flag differs by one; the first source's airmass against no number; the two sources' 420 seconds "
        "against the first's 300"));
    cases.back().operation = "combine.lrgb";
    cases.push_back(
        accepted(exact("combine.metadata", "combine-ha-1", kCombine, kCombineText, kCombineSource, {"combine-ha-2"}),
                 perFileReason, 600.0, {stated(kFilter), number(kExposureTime)},
                 "a presence flag differs by one; the two sources' 1200 seconds against the first's 600"));
    // The integration of the one source that states an exposure is the first source's EXPTIME, as the record keeps.
    cases.push_back(
        exact("combine.metadata", "combine-ha-2", kCombine, kCombineText, kCombineSource, {"combine-unexposed-1"}));
    cases.push_back(exact("combine.metadata", "combine-unexposed-1", kCombine, kCombineText, kCombineSource,
                          {"combine-unexposed-2"}));
    cases.push_back(accepted(exact("combine.metadata", "combine-exposure-card", kCombine, kCombineText, kCombineSource,
                                   {"combine-exposure-card"}),
                             "the combination states the integration it holds - both copies' summed, since neither "
                             "states when it began - as EXPTIME; the record states no EXPTIME",
                             1.0, {stated(kExposureTime), number(kExposureTime)},
                             "a presence flag differs by one; the integration against no number"));

    for (const char* input : {"history-u8", "history-i8", "history-u16", "history-i16", "history-u32", "history-i32",
                              "history-f32", "history-f64", "history-rgb-f32", "history-rgb-u16", "history-mosaic-u16"})
    {
        cases.push_back(exact("history.round-trip", input, MetadataSnapshotCaseKind::HistoryRoundTrip, kHistoryText,
                              kHistorySource));
    }
    return cases;
}

} // namespace

const std::vector<FixtureImage>& metadataSnapshotInputs()
{
    static const std::vector<FixtureImage> inputs = makeInputs();
    return inputs;
}

const FixtureImage* metadataSnapshotInput(std::string_view name)
{
    for (const FixtureImage& image : metadataSnapshotInputs())
    {
        if (image.name == name)
        {
            return &image;
        }
    }
    return nullptr;
}

const std::vector<MetadataSnapshotCase>& metadataSnapshotCases()
{
    static const std::vector<MetadataSnapshotCase> cases = makeCases();
    return cases;
}

const MetadataSnapshotCase* metadataSnapshotCase(std::string_view id)
{
    for (const MetadataSnapshotCase& item : metadataSnapshotCases())
    {
        if (item.id() == id)
        {
            return &item;
        }
    }
    return nullptr;
}

std::string metadataCardValue(const FixtureCard& card)
{
    if (card.kind != FixtureCardKind::Text)
    {
        return card.value;
    }
    std::string quoted = "'";
    for (const char character : card.value)
    {
        quoted += character;
        if (character == '\'')
        {
            quoted += '\'';
        }
    }
    return quoted + "'";
}

std::optional<double> metadataCardNumber(std::string_view value) noexcept
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
    double parsed = 0.0;
    const std::from_chars_result result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (value.empty() || result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        !std::isfinite(parsed))
    {
        return std::nullopt;
    }
    return parsed;
}

double frameKindCode(std::string_view kind) noexcept
{
    if (kind == "BIAS")
    {
        return 0.0;
    }
    if (kind == "DARK")
    {
        return 1.0;
    }
    if (kind == "FLAT")
    {
        return 2.0;
    }
    if (kind == "LIGHT")
    {
        return 3.0;
    }
    return -1.0;
}

FixtureSamples metadataRow(const std::vector<double>& values)
{
    FixtureSamples samples(static_cast<std::uint32_t>(values.size()), 1u, 1u, FixtureSampleType::Float64);
    for (std::uint32_t index = 0u; index < values.size(); ++index)
    {
        samples.setValue(index, 0u, 0u, values[index]);
    }
    return samples;
}

} // namespace an::tests::fixtures
