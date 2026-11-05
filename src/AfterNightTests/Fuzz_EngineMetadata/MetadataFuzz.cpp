// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What must hold of every set of header cards a file could carry.
//
// Metadata is the part of a decode that comes straight from a stranger's file and is then read as
// numbers, dates and enumerated states. Nothing in a card's text is checked by the container that
// held it, so every reader here is a parser whose input is arbitrary: a date that is not a date, a
// number with two decimal points, a keyword repeated nine times, a value longer than a card, a state
// card naming a state this build does not have.
//
// The claims:
//
// * **A reader answers.** Every projection and every card reading returns a record and says how many
//   cards it consulted and how many it could not read; none of them may crash, a projection given
//   nothing consulted nothing, and a projection that reported a fact counted the card it read it off.
//   The two counts are disjoint rather than nested, which this target's first claim got wrong.
// * **A projection is a function of its cards.** The same cards project to the same answer twice, so
//   nothing carried between two reads of two different files.
// * **A scalar parse is a value or a reason.** `parseDateCard` and `parseNumericCard` never report
//   success without a value.
// * **The processing state survives a round trip.** Cards read into a record and written back out
//   read as the same record. That is the property that decides whether an image's linearity and
//   colour basis survive being saved and loaded, and it is the one a partial writer breaks.
// * **Removal is idempotent and complete.** Cards with the state removed read as the empty state, and
//   removing twice is removing once.
//
// The observation-card writer is here too, because its input is two card sets rather than one and its
// rule - write where the destination says nothing, replace where it is told to - is exactly the kind
// that goes wrong when the same keyword appears more than once.

#include "../FuzzEngine/FuzzHarness.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/ObservationCards.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace an::fuzz
{

TargetInfo targetInfo()
{
    return {.name = "Fuzz_EngineMetadata", .maxInputBytes = 8192u};
}

std::vector<std::vector<std::uint8_t>> seedCorpus()
{
    // A seed is a card set in this target's own wire form: a namespace byte, then the keyword, a NUL,
    // the value, a NUL, the comment and a NUL. Writing them out as text keeps the seeds readable in a
    // hex dump and lets a mutation that changes one byte change one field.
    static constexpr const char* kCards[][3] = {
        {"DATE-OBS", "2026-09-15T21:04:33", "start of exposure"},
        {"DATE-OBS", "2026:09:15 21:04:33", ""},
        {"DATE-OBS", "not a date", ""},
        {"EXPTIME", "120.5", "seconds"},
        {"EXPTIME", "1/250", ""},
        {"EXPTIME", "1.2.3", ""},
        {"XPIXSZ", "3.76", "micrometres"},
        {"GAIN", "120", ""},
        {"OFFSET", "-10", ""},
        {"CCD-TEMP", "-10.5", ""},
        {"IMAGETYP", "Light Frame", ""},
        {"IMAGETYP", "FLAT", ""},
        {"INSTRUME", "ASI2600MM", ""},
        {"TELESCOP", "TSA-120", ""},
        {"FILTER", "Ha", ""},
        {"BAYERPAT", "RGGB", ""},
        // The processing-state family, spelled exactly as the writer spells it. A near miss here
        // leaves the round-trip claim reading an empty record and agreeing with itself.
        {"ANSTVER", "1", ""},
        {"ANLIN", "'Linear'", "recorded linearity"},
        {"ANBASIS", "'CameraRgb'", ""},
        {"ANASRC", "'Measured'", ""},
        {"ANCCAL", "'WhiteBalance'", ""},
        {"ANWBMOD", "'AsShot'", ""},
        {"ANWBAPP", "T", ""},
        {"ANWBMUL", "'1.000000 1.000000 1.000000 1.000000'", ""},
        {"ANCMATP", "'dcraw'", ""},
        {"ANOP", "'calibrate'", ""},
        {"ANASFILE", "'frame.fits'", ""},
        {"ANASHSH", "'0123456789abcdef'", ""},
        {"SITELAT", "40 24 30 N", ""},
        {"SITELONG", "3 41 00 W", ""},
        {"SWCREATE", "AfterNight", ""},
        {"COMMENT", "", ""},
    };

    std::vector<std::vector<std::uint8_t>> corpus;
    std::vector<std::uint8_t> everything;
    for (const auto& card : kCards)
    {
        std::vector<std::uint8_t> one;
        const auto append = [&one](const char* text) {
            for (const char* cursor = text; *cursor != '\0'; ++cursor)
            {
                one.push_back(static_cast<std::uint8_t>(*cursor));
            }
            one.push_back(0u);
        };
        one.push_back(1u); // The FITS namespace, which is what every reader here understands.
        append(card[0]);
        append(card[1]);
        append(card[2]);
        everything.insert(everything.end(), one.begin(), one.end());
        corpus.push_back(std::move(one));
    }
    corpus.push_back(std::move(everything));
    return corpus;
}

} // namespace an::fuzz

namespace
{

/// What a numeric card may never parse to, which the reader states it refuses.
constexpr double kInfinity = std::numeric_limits<double>::infinity();

/// Reads one NUL-terminated field out of the input.
///
/// @param input where the bytes come from.
/// @param maxBytes the most this field may be, so one malformed length cannot make one card the whole
///        input.
/// @return the field, without its terminator.
[[nodiscard]] std::string field(an::fuzz::Input& input, std::size_t maxBytes)
{
    std::string text;
    while (input.remaining() != 0u && text.size() < maxBytes)
    {
        const std::uint8_t byte = input.take();
        if (byte == 0u)
        {
            break;
        }
        text.push_back(static_cast<char>(byte));
    }
    return text;
}

/// The card set this input describes.
///
/// A card that the type refuses - an empty keyword, a field longer than a card holds - is dropped
/// rather than being a finding: what is under test is the readers, and `CodecCard::create` refusing a
/// keyword is a rule it states rather than a defect.
///
/// @param input where the cards come from.
/// @param maxCards how many at most.
/// @return the cards, and the source encoding read alongside them.
[[nodiscard]] an::CodecMetadata metadataFrom(an::fuzz::Input& input, std::size_t maxCards)
{
    std::vector<an::CodecCard> cards;
    while (input.remaining() != 0u && cards.size() < maxCards)
    {
        const an::CodecNamespace origin = input.takeEnum(an::CodecNamespace::Ffmpeg);
        std::string keyword = field(input, 96u);
        std::string value = field(input, 256u);
        std::string comment = field(input, 96u);
        an::Result<an::CodecCard> card =
            an::CodecCard::create(origin, std::move(keyword), std::move(value), std::move(comment));
        if (card.hasValue())
        {
            cards.push_back(std::move(*card));
        }
    }

    an::SourceEncoding encoding;
    an::Result<an::CodecMetadata> metadata = an::CodecMetadata::create(encoding, std::move(cards));
    if (!metadata.hasValue())
    {
        return an::CodecMetadata{};
    }
    return std::move(*metadata);
}

/// The NUL-separated fields the input holds, which is a card record in this target's own wire form.
///
/// @param input where the bytes come from.
/// @param maxFields how many at most.
/// @return the fields, which may be empty and need not be valid UTF-8.
[[nodiscard]] std::vector<std::string> fieldsOf(an::fuzz::Input& input, std::size_t maxFields)
{
    std::vector<std::string> fields;
    while (input.remaining() != 0u && fields.size() < maxFields)
    {
        fields.push_back(field(input, 256u));
    }
    return fields;
}

/// Everything the two scalar card parsers claim about one value.
///
/// @param text the card's value, as a file spelled it.
void checkScalarParsers(const std::string& text)
{
    const an::Result<an::UtcInstant> instant = an::parseDateCard(text);
    AN_FUZZ_CHECK(instant.hasValue() == instant.status().ok(),
                  "a date parse yields an instant exactly when it reports success");
    const an::Result<double> number = an::parseNumericCard(text);
    AN_FUZZ_CHECK(number.hasValue() == number.status().ok(),
                  "a numeric parse yields a value exactly when it reports success");
    an::fuzz::countReached(instant.hasValue() ? "dates-parsed" : "dates-refused");
    an::fuzz::countReached(number.hasValue() ? "numbers-parsed" : "numbers-refused");
    if (number.hasValue())
    {
        AN_FUZZ_CHECK(number.value() == number.value() && number.value() != kInfinity && number.value() != -kInfinity,
                      "a number that parsed is a finite one, which is what the reader promises");
    }
    if (!instant.hasValue())
    {
        return;
    }

    // The FITS family writes the fraction, so its form is lossless. The Exif form is to the second
    // by definition, so the claim there is that it parses and names the same second - the truncation
    // is documented behaviour rather than something to report.
    for (const an::CodecNamespace target :
         {an::CodecNamespace::Fits, an::CodecNamespace::Xisf, an::CodecNamespace::Ser, an::CodecNamespace::Unspecified})
    {
        const std::string written = an::formatDateCard(*instant, target);
        const an::Result<an::UtcInstant> reread = an::parseDateCard(written);
        AN_FUZZ_CHECK(reread.hasValue(), "a formatted date must parse");
        AN_FUZZ_CHECK(*reread == *instant, "a date's text form must be lossless");
    }
    for (const an::CodecNamespace target :
         {an::CodecNamespace::Jpeg, an::CodecNamespace::Tiff, an::CodecNamespace::Png, an::CodecNamespace::CameraRaw})
    {
        const std::string written = an::formatDateCard(*instant, target);
        const an::Result<an::UtcInstant> reread = an::parseDateCard(written);
        AN_FUZZ_CHECK(reread.hasValue(), "a formatted date must parse");
        const std::int64_t difference = reread->nanosecondsSinceEpoch - instant->nanosecondsSinceEpoch;
        AN_FUZZ_CHECK(difference > -1000000000 && difference < 1000000000,
                      "a date written to the second must name the same second");
    }
}

/// Every claim the acquisition projection makes.
///
/// @param codec the cards.
void checkProjection(const an::CodecMetadata& codec)
{
    const an::AcquisitionProjection projection = an::projectAcquisition(codec);
    an::fuzz::countReached(projection.consultedCards != 0u ? "cards-consulted" : "cards-ignored");
    an::fuzz::countReached(projection.malformedCards != 0u ? "cards-malformed" : "cards-well-formed");

    // The two counts are disjoint rather than nested - one counts cards that supplied a value and the
    // other cards whose spelling was known and whose value was not readable - so neither bounds the
    // other and a card set of one unreadable card reports zero and one. What does hold is that a
    // projection given nothing consulted nothing.
    AN_FUZZ_CHECK(!codec.cards().empty() || (projection.consultedCards == 0u && projection.malformedCards == 0u),
                  "a projection of no cards consulted no cards");
    AN_FUZZ_CHECK(projection.consultedCards != 0u ||
                      (projection.software.empty() && projection.frameTypeText.empty() &&
                       !projection.creationTime.has_value() && !projection.isoSpeed.has_value() &&
                       !projection.gainSetting.has_value() && !projection.cameraOffset.has_value()),
                  "a projection that reported a fact read it off a card it counted");

    // It is a function of the cards and of nothing else, so it gives the same answer twice.
    const an::AcquisitionProjection again = an::projectAcquisition(codec);
    AN_FUZZ_CHECK(again.consultedCards == projection.consultedCards &&
                      again.malformedCards == projection.malformedCards && again.frameType == projection.frameType &&
                      again.software == projection.software && again.creationTime == projection.creationTime,
                  "projecting the same cards twice gives the same answer");

    // What the projection read must be writable again in a vocabulary that has an encoder, and
    // refused by name in one that has none.
    for (const an::CodecNamespace target :
         {an::CodecNamespace::Fits, an::CodecNamespace::Jpeg, an::CodecNamespace::Tiff, an::CodecNamespace::Ser})
    {
        const an::Result<std::vector<an::CodecCard>> written = an::acquisitionCards(projection, target);
        AN_FUZZ_CHECK(written.hasValue() || !written.status().ok(),
                      "writing the projection back either produces cards or reports why it could not");
        if (written.hasValue())
        {
            for (const an::CodecCard& card : *written)
            {
                AN_FUZZ_CHECK(card.origin() == target, "a written card belongs to the vocabulary it was asked for");
                AN_FUZZ_CHECK(!card.keyword().empty(), "a written card has a keyword");
            }
        }
    }
}

/// A number as the state format carries it: fixed notation with six decimals, read back.
///
/// Not a rounding computed here, because the question is what the writer's own text parses to and a
/// multiplication by a million answers a different question at the last bit.
///
/// @param value the number the record holds.
/// @return what a write and a read of it produce.
[[nodiscard]] double toSixDecimals(double value)
{
    char buffer[512] = {};
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    return std::strtod(buffer, nullptr);
}

/// Every claim the processing-state cards make.
///
/// @param codec the cards.
void checkProcessingState(const an::CodecMetadata& codec)
{
    const an::ProcessingStateRecord record = an::readProcessingStateCards(codec);

    // Written onto cards the state has been removed from, rather than onto the cards it was read
    // from. Writing onto the originals lets a field the writer forgot survive in the card it came
    // from, so the round trip agrees with itself and says nothing - which a deliberately dropped
    // field proved, by passing.
    const an::Result<an::CodecMetadata> cleared = an::withoutProcessingStateCards(codec);
    const an::Result<an::CodecMetadata> written = cleared.hasValue() ? an::withProcessingStateCards(*cleared, record)
                                                                     : an::Result<an::CodecMetadata>(cleared.status());
    if (written.hasValue())
    {
        an::ProcessingStateRecord reread = an::readProcessingStateCards(*written);
        // The multipliers are the one field the format rounds: it states four numbers with six
        // decimals, so a multiplier is carried to that precision and no further. Checked at that
        // precision here and then set aside, so the rest of the record is still compared exactly.
        AN_FUZZ_CHECK(reread.rawWhiteBalanceMultipliers.has_value() == record.rawWhiteBalanceMultipliers.has_value(),
                      "multipliers that were written are multipliers that are read");
        if (record.rawWhiteBalanceMultipliers.has_value())
        {
            for (std::size_t index = 0u; index < record.rawWhiteBalanceMultipliers->size(); ++index)
            {
                AN_FUZZ_CHECK((*reread.rawWhiteBalanceMultipliers)[index] ==
                                  toSixDecimals((*record.rawWhiteBalanceMultipliers)[index]),
                              "a multiplier survives to the six decimals the format states");
            }
            reread.rawWhiteBalanceMultipliers = record.rawWhiteBalanceMultipliers;
        }
        AN_FUZZ_CHECK(reread == record, "the processing state must survive being written and read back");
        an::fuzz::countReached("state-round-trips");
    }

    const an::Result<an::CodecMetadata> stripped = an::withoutProcessingStateCards(codec);
    if (stripped.hasValue())
    {
        const an::ProcessingStateRecord empty = an::readProcessingStateCards(*stripped);
        AN_FUZZ_CHECK(empty == an::ProcessingStateRecord{}, "cards with the state removed read as no state at all");
        const an::Result<an::CodecMetadata> again = an::withoutProcessingStateCards(*stripped);
        AN_FUZZ_CHECK(again.hasValue(), "removing a state that is already absent must succeed");
        AN_FUZZ_CHECK(again->cards().size() == stripped->cards().size(), "removing the state twice removes it once");
    }

    // The colour and calibration readings have no round trip of their own here; what is checked is
    // that they answer at all, whatever the cards said.
    const an::ColorCalibrationReading color = an::readColorCalibrationCards(codec);
    AN_FUZZ_CHECK(color.record.gains.size() == 3u, "a colour calibration reading always has three gains");
    const an::CalibrationPayloadReading payload = an::readColorCalibrationPayload(codec);
    (void)payload;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    an::fuzz::Input input(data, size);
    const std::uint32_t mode = input.controlBelow(4u);

    if (mode == 0u)
    {
        // The two scalar parsers, over each NUL-separated field rather than over the whole input.
        // Their subject is one card's value, and a seed in this target's wire form is a record of
        // three of them; handing them the record whole would only ever reach the refusal.
        for (const std::string& text : fieldsOf(input, 24u))
        {
            checkScalarParsers(text);
        }
        return 0;
    }

    const an::CodecMetadata codec = metadataFrom(input, 64u);
    checkProjection(codec);
    checkProcessingState(codec);

    if (mode == 3u)
    {
        // The writer's second card set is whatever is left of the input, so one mutation can make the
        // two sets share a keyword, which is the case its rule is about.
        const an::CodecMetadata source = metadataFrom(input, 32u);
        for (const an::ObservationCardPolicy policy :
             {an::ObservationCardPolicy::KeepExisting, an::ObservationCardPolicy::Replace})
        {
            const an::Result<an::CodecMetadata> merged = an::withObservationCards(codec, source, policy);
            AN_FUZZ_CHECK(merged.hasValue() || !merged.status().ok(),
                          "writing observation cards either produces cards or reports why it could not");
        }
    }
    return 0;
}
