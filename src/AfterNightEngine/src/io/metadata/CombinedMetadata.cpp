// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the metadata of an image combined from several sources.
//
// The rules are documented on `combinedImageMetadata`. The declaration and the outcome are checked, and the lineage
// entry built, by `operationTransition`, so a combination is refused for exactly what any operation's result is.
// A field - a card and the cards that continue its value - survives when every other source holds a field of the same
// namespace, keyword and value anywhere in its set, the value of a continued field joined, so two long values that
// share their first card are never taken for one; each other source's fields are sorted once so that question is a
// binary search rather than a scan. The exposure spellings are the ones `projectAcquisition` reads an exposure from,
// and the suite holds the two lists together. Three families of card that differ between sources still describe the
// result and follow a rule of their own: where the mount pointed, the first source's, since the result is on its
// grid; when the observation began, the earliest source's; and how much light went into it, the integration and the
// frame counts summed over the distinct acquisitions.

#include <AfterNightEngine/IO/CombinedMetadata.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>

#include "image/CardContinuation.h"
#include "image/SolutionCards.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace an
{
namespace
{
using std::string_view_literals::operator""sv;

constexpr const char* kOperation = "an::combinedImageMetadata";

/// The namespaces whose cards carry Exif.
constexpr std::array kExifNamespaces = {CodecNamespace::Jpeg, CodecNamespace::Tiff, CodecNamespace::Png,
                                        CodecNamespace::CameraRaw};

/// One field as the comparison sees it: its namespace, keyword and value, and not its comment.
using CardKey = std::tuple<CodecNamespace, std::string_view, std::string_view>;

/// A card set's fields, each a card and the cards that continue its value, with the value each one states.
///
/// The value of a field held in one card is read in place. That of a field continued across several is joined into
/// `joined`, which is reserved before the first is built, so the views into it stay valid.
struct CardFields
{
    /// One field: where it starts in the card set, how many cards it holds, and its value.
    struct Field
    {
        std::size_t first = 0u;
        std::size_t count = 0u;
        std::string_view value{};
    };

    std::vector<Field> fields{};
    std::vector<std::string> joined{};
};

[[nodiscard]] CardFields fieldsOf(std::span<const CodecCard> cards)
{
    CardFields result;
    std::size_t continued = 0u;
    for (std::size_t index = 1u; index < cards.size(); ++index)
        if (continuesCard(cards[index - 1u], cards[index]) &&
            (index == 1u || !continuesCard(cards[index - 2u], cards[index - 1u])))
            ++continued;
    result.joined.reserve(continued);
    for (std::size_t index = 0u; index < cards.size();)
    {
        std::size_t count = 1u;
        while (index + count < cards.size() && continuesCard(cards[index + count - 1u], cards[index + count]))
            ++count;
        std::string_view value = cards[index].value();
        if (count > 1u)
        {
            std::string& text = result.joined.emplace_back();
            for (std::size_t part = index; part < index + count; ++part)
                text += cards[part].value();
            value = text;
        }
        result.fields.push_back({.first = index, .count = count, .value = value});
        index += count;
    }
    return result;
}

[[nodiscard]] CardKey keyOf(std::span<const CodecCard> cards, const CardFields::Field& field) noexcept
{
    const CodecCard& card = cards[field.first];
    return {card.origin(), card.keyword(), field.value};
}

[[nodiscard]] Status refuse(ErrorCode code, const char* message, const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = kOperation,
                                        .domain = ErrorDomain::Io,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// Whether a card states an exposure, in a spelling the codec projection reads one from.
[[nodiscard]] bool statesExposure(const CodecCard& card) noexcept
{
    if (card.origin() == CodecNamespace::Fits)
        return card.keyword() == "EXPOSURE"sv || card.keyword() == "EXPTIME"sv;
    return card.keyword() == "Exif.Photo.ExposureTime"sv &&
           std::find(kExifNamespaces.begin(), kExifNamespaces.end(), card.origin()) != kExifNamespaces.end();
}

/// The keywords that state when an observation began, which a combination takes together from its earliest source.
constexpr std::array kObservationStartKeywords = {"DATE-OBS"sv, "DATE-LOC"sv, "MJD-OBS"sv,
                                                  "DATE-BEG"sv, "MJD-BEG"sv,  "TIME-OBS"sv};

/// The keywords that state a source's whole integration in seconds, in the order one is read.
constexpr std::array kIntegrationKeywords = {"TOTEXP"sv, "LIVETIME"sv};

/// The keywords that state how many frames a source combines, in the order one is read.
constexpr std::array kFrameCountKeywords = {"STACKCNT"sv, "NCOMBINE"sv};

[[nodiscard]] bool isFitsKeywordOf(const CodecCard& card, std::span<const std::string_view> keywords) noexcept
{
    return card.origin() == CodecNamespace::Fits &&
           std::find(keywords.begin(), keywords.end(), card.keyword()) != keywords.end();
}

/// The keywords that state where the mount or the target pointed: both keywords of every pair `observedPointingCards`
/// names, which the suite holds this list to. They are spelled here because the codec boundary reads no algorithm.
constexpr std::array kPointingKeywords = {"RA"sv,    "DEC"sv,    "OBJCTRA"sv, "OBJCTDEC"sv,
                                          "OBJRA"sv, "OBJDEC"sv, "TELRA"sv,   "TELDEC"sv};

/// Whether a card states where the mount or the target pointed.
[[nodiscard]] bool statesPointing(const CodecCard& card) noexcept
{
    return isFitsKeywordOf(card, kPointingKeywords);
}

/// The number a card's value states: blanks and one pair of quotes around it removed, then one decimal number.
[[nodiscard]] std::optional<double> cardNumber(std::string_view value) noexcept
{
    const auto trim = [](std::string_view text) {
        const std::size_t first = text.find_first_not_of(" \t");
        return first == std::string_view::npos ? std::string_view{}
                                               : text.substr(first, text.find_last_not_of(" \t") - first + 1u);
    };
    value = trim(value);
    if (value.size() >= 2u && value.front() == '\'' && value.back() == '\'')
        value = trim(value.substr(1u, value.size() - 2u));
    double number = 0.0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (error != std::errc{} || end != value.data() + value.size() || !std::isfinite(number))
        return std::nullopt;
    return number;
}

/// The first FITS card of a set that states a number under one of `keywords`, read in that order.
[[nodiscard]] std::optional<double> firstNumber(std::span<const CodecCard> cards,
                                                std::span<const std::string_view> keywords) noexcept
{
    for (const std::string_view keyword : keywords)
        for (const CodecCard& card : cards)
            if (card.origin() == CodecNamespace::Fits && card.keyword() == keyword)
                if (const std::optional<double> number = cardNumber(card.value()); number.has_value())
                    return number;
    return std::nullopt;
}

/// How much light one source holds: its whole integration, the frames it combines and when it began.
struct SourceIntegration
{
    /// The whole integration in seconds: the card that states it, else the frame count times the exposure; absent
    /// when the source states neither.
    std::optional<double> seconds{};

    /// The frames it combines: the card that states them, else one.
    double frames = 1.0;

    /// When it began, which tells two copies of one acquisition apart from two acquisitions.
    std::optional<UtcInstant> start{};
};

[[nodiscard]] SourceIntegration integrationOf(const ImageMetadata& source)
{
    const std::span<const CodecCard> cards = source.codec().cards();
    SourceIntegration integration{.start = source.acquisition().exposureStart()};
    if (const std::optional<double> frames = firstNumber(cards, kFrameCountKeywords);
        frames.has_value() && *frames >= 1.0)
        integration.frames = *frames;
    integration.seconds = firstNumber(cards, kIntegrationKeywords);
    if (!integration.seconds.has_value())
        if (const std::optional<double> exposure = source.acquisition().exposureSeconds(); exposure.has_value())
            integration.seconds = *exposure * integration.frames;
    return integration;
}

/// The light a combination holds: each distinct acquisition's integration and frames once. Sources that began at the
/// same instant are one acquisition - an image and copies made of it, its starless image, its stars or a channel taken
/// out of it - whose light is the largest integration and the largest frame count any of them states, since a copy may
/// have lost the cards that state them; a source that states no start is always its own.
struct CombinedIntegration
{
    std::optional<double> seconds{};
    double frames = 0.0;
};

[[nodiscard]] CombinedIntegration combinedIntegration(std::span<const ImageMetadata* const> sources)
{
    std::vector<SourceIntegration> acquisitions;
    for (const ImageMetadata* const source : sources)
    {
        const SourceIntegration integration = integrationOf(*source);
        const auto same =
            integration.start.has_value()
                ? std::find_if(acquisitions.begin(), acquisitions.end(),
                               [&](const SourceIntegration& earlier) { return earlier.start == integration.start; })
                : acquisitions.end();
        if (same == acquisitions.end())
        {
            acquisitions.push_back(integration);
            continue;
        }
        if (integration.seconds.has_value())
            same->seconds = (std::max)(same->seconds.value_or(0.0), *integration.seconds);
        same->frames = (std::max)(same->frames, integration.frames);
    }
    CombinedIntegration combined;
    for (const SourceIntegration& acquisition : acquisitions)
    {
        if (acquisition.seconds.has_value())
            combined.seconds = combined.seconds.value_or(0.0) + *acquisition.seconds;
        combined.frames += acquisition.frames;
    }
    return combined;
}

/// The source whose observation began first, of those that state a start; the first of them on a tie.
[[nodiscard]] std::optional<std::size_t> earliestSource(std::span<const ImageMetadata* const> sources) noexcept
{
    std::optional<std::size_t> earliest;
    for (std::size_t index = 0u; index < sources.size(); ++index)
    {
        const std::optional<UtcInstant> start = sources[index]->acquisition().exposureStart();
        if (start.has_value() &&
            (!earliest.has_value() ||
             start->nanosecondsSinceEpoch < sources[*earliest]->acquisition().exposureStart()->nanosecondsSinceEpoch))
            earliest = index;
    }
    return earliest;
}

/// A number as a card states it: the shortest decimal that reads back as the same double.
[[nodiscard]] std::string numberText(double value)
{
    std::array<char, 32> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return error == std::errc{} ? std::string(buffer.data(), end) : std::string("0");
}

/// The value every source states, read by `read`; empty when any source states none or two differ.
template <class Read> [[nodiscard]] auto common(std::span<const ImageMetadata* const> sources, Read read)
{
    auto first = read(*sources[0]);
    for (std::size_t index = 1u; index < sources.size() && first.has_value(); ++index)
    {
        const auto other = read(*sources[index]);
        if (!other.has_value() || !(*other == *first))
            first.reset();
    }
    return first;
}

/// The text every source states, read by `read`; empty when any source states none or two differ.
template <class Read> [[nodiscard]] std::string commonText(std::span<const ImageMetadata* const> sources, Read read)
{
    const std::optional<std::string> text = common(sources, [&](const ImageMetadata& metadata) {
        const std::string& value = read(metadata);
        return value.empty() ? std::optional<std::string>{} : std::optional<std::string>{value};
    });
    return text.value_or(std::string{});
}

[[nodiscard]] Result<AcquisitionMetadata> combinedAcquisition(std::span<const ImageMetadata* const> sources)
{
    AcquisitionMetadataRequest request;
    if (sources.size() == 1u)
        request.exposureSeconds = sources[0]->acquisition().exposureSeconds();
    else
        request.exposureSeconds = combinedIntegration(sources).seconds;
    request.instrumentName = commonText(sources, [](const ImageMetadata& metadata) -> const std::string& {
        return metadata.acquisition().instrumentName();
    });
    request.telescopeName = commonText(sources, [](const ImageMetadata& metadata) -> const std::string& {
        return metadata.acquisition().telescopeName();
    });
    request.observerName = commonText(sources, [](const ImageMetadata& metadata) -> const std::string& {
        return metadata.acquisition().observerName();
    });
    request.objectName = commonText(sources, [](const ImageMetadata& metadata) -> const std::string& {
        return metadata.acquisition().objectName();
    });
    request.filterName = commonText(sources, [](const ImageMetadata& metadata) -> const std::string& {
        return metadata.acquisition().filterName();
    });
    request.sensorTemperatureCelsius = common(
        sources, [](const ImageMetadata& metadata) { return metadata.acquisition().sensorTemperatureCelsius(); });
    request.gainElectronsPerAdu =
        common(sources, [](const ImageMetadata& metadata) { return metadata.acquisition().gainElectronsPerAdu(); });
    request.binning = common(sources, [](const ImageMetadata& metadata) { return metadata.acquisition().binning(); });
    if (const std::optional<std::size_t> earliest = earliestSource(sources); earliest.has_value())
        request.exposureStart = sources[*earliest]->acquisition().exposureStart();
    request.rawCamera =
        common(sources, [](const ImageMetadata& metadata) { return metadata.acquisition().rawCamera(); });
    return AcquisitionMetadata::create(request);
}

[[nodiscard]] Result<ImageSemantics> agreedSemantics(std::span<const ImageMetadata* const> sources,
                                                     const ValidityTransition& validity)
{
    ImageSemanticsRequest request;
    const std::optional<std::pair<TransferFunction, std::optional<double>>> encoding =
        common(sources, [](const ImageMetadata& metadata) {
            return std::optional<std::pair<TransferFunction, std::optional<double>>>{
                std::pair{metadata.semantics().transferFunction(), metadata.semantics().gammaExponent()}};
        });
    if (encoding.has_value())
    {
        request.transferFunction = encoding->first;
        request.gammaExponent = encoding->second;
    }
    ProcessingState& state = request.processingState;
    state = sources[0]->semantics().processingState();
    for (const ImageMetadata* const source : sources)
    {
        const ProcessingState& other = source->semantics().processingState();
        state.biasSubtracted = state.biasSubtracted && other.biasSubtracted;
        state.darkSubtracted = state.darkSubtracted && other.darkSubtracted;
        state.flatFielded = state.flatFielded && other.flatFielded;
        state.cosmeticCorrected = state.cosmeticCorrected && other.cosmeticCorrected;
        state.demosaiced = state.demosaiced && other.demosaiced;
        state.backgroundExtracted = state.backgroundExtracted && other.backgroundExtracted;
        state.registered = state.registered && other.registered;
        state.normalized = state.normalized && other.normalized;
        state.colorCalibrated = state.colorCalibrated && other.colorCalibrated;
    }
    std::vector<ValidityDescriptor> rules;
    rules.reserve(sources.size());
    for (const ImageMetadata* const source : sources)
        rules.push_back(source->semantics().validity());
    Result<ValidityDescriptor> combined = applyValidityPolicy(validity, rules);
    if (!combined.hasValue())
        return combined.status();
    request.validity = *combined;
    return ImageSemantics::create(request);
}

[[nodiscard]] Result<CodecMetadata> combinedCards(std::span<const ImageMetadata* const> sources, bool keepSolution)
{
    // Each other source's fields, sorted, so a field's presence is a binary search. The keys view the sources' cards
    // and the joined values `fields` holds, both of which outlive them.
    std::vector<CardFields> fields;
    fields.reserve(sources.size() - 1u);
    std::vector<std::vector<CardKey>> others;
    others.reserve(sources.size() - 1u);
    for (std::size_t index = 1u; index < sources.size(); ++index)
    {
        const std::span<const CodecCard> cards = sources[index]->codec().cards();
        const CardFields& set = fields.emplace_back(fieldsOf(cards));
        std::vector<CardKey> keys;
        keys.reserve(set.fields.size());
        for (const CardFields::Field& field : set.fields)
            keys.push_back(keyOf(cards, field));
        std::sort(keys.begin(), keys.end());
        others.push_back(std::move(keys));
    }
    const bool combining = sources.size() > 1u;
    // The observation began when the earliest source's did; its start cards are taken together, from it.
    const std::optional<std::size_t> earliest = combining ? earliestSource(sources) : std::nullopt;
    const std::span<const CodecCard> cards = sources[0]->codec().cards();
    const CardFields first = fieldsOf(cards);
    std::vector<CodecCard> kept;
    bool livetimeStated = false;
    bool stackCountStated = false;
    bool combineCountStated = false;
    for (const ImageMetadata* const source : sources)
    {
        for (const CodecCard& card : source->codec().cards())
        {
            if (card.origin() != CodecNamespace::Fits)
                continue;
            livetimeStated = livetimeStated || card.keyword() == "LIVETIME"sv;
            stackCountStated = stackCountStated || card.keyword() == "STACKCNT"sv;
            combineCountStated = combineCountStated || card.keyword() == "NCOMBINE"sv;
        }
    }
    for (const CardFields::Field& field : first.fields)
    {
        const CodecCard& card = cards[field.first];
        const auto keep = [&] {
            const auto begin = cards.begin() + static_cast<std::ptrdiff_t>(field.first);
            kept.insert(kept.end(), begin, begin + static_cast<std::ptrdiff_t>(field.count));
        };
        // The solution, in the FITS vocabulary or XISF's, is the first source's whole or none of it.
        if ((card.origin() == CodecNamespace::Fits && isWcsKeyword(card.keyword())) || isXisfSolutionCard(card))
        {
            if (keepSolution)
                keep();
            continue;
        }
        if ((combining && statesExposure(card)) || isArtifactIdentityKeyword(card.keyword()))
            continue;
        if (combining)
        {
            // Where the mount pointed is the first source's: the result is on its grid.
            if (statesPointing(card))
            {
                keep();
                continue;
            }
            // The light it holds is summed below, and its start is the earliest source's.
            if (isFitsKeywordOf(card, kIntegrationKeywords) || isFitsKeywordOf(card, kFrameCountKeywords) ||
                (earliest.has_value() && isFitsKeywordOf(card, kObservationStartKeywords)))
                continue;
        }
        const CardKey key = keyOf(cards, field);
        const bool everywhere = std::all_of(others.begin(), others.end(), [&](const std::vector<CardKey>& keys) {
            return std::binary_search(keys.begin(), keys.end(), key);
        });
        if (everywhere)
            keep();
    }
    if (combining)
    {
        if (earliest.has_value())
        {
            const std::span<const CodecCard> started = sources[*earliest]->codec().cards();
            const CardFields startedFields = fieldsOf(started);
            for (const CardFields::Field& field : startedFields.fields)
            {
                if (isFitsKeywordOf(started[field.first], kObservationStartKeywords))
                {
                    const auto begin = started.begin() + static_cast<std::ptrdiff_t>(field.first);
                    kept.insert(kept.end(), begin, begin + static_cast<std::ptrdiff_t>(field.count));
                }
            }
        }
        const CombinedIntegration integration = combinedIntegration(sources);
        const auto append = [&](std::string_view keyword, std::string value, std::string_view comment) -> Status {
            Result<CodecCard> card =
                CodecCard::create(CodecNamespace::Fits, std::string(keyword), std::move(value), std::string(comment));
            if (!card.hasValue())
                return card.status();
            kept.push_back(std::move(*card));
            return Status::success();
        };
        Status stated = Status::success();
        if (integration.seconds.has_value())
        {
            const std::string seconds = numberText(*integration.seconds);
            stated = append("EXPTIME"sv, seconds, "total integration in seconds"sv);
            if (stated.ok())
                stated = append("TOTEXP"sv, seconds, "total integration in seconds"sv);
            if (stated.ok() && livetimeStated)
                stated = append("LIVETIME"sv, seconds, "total integration in seconds"sv);
        }
        if (stated.ok() && stackCountStated)
            stated = append("STACKCNT"sv, numberText(integration.frames), "frames combined"sv);
        if (stated.ok() && combineCountStated)
            stated = append("NCOMBINE"sv, numberText(integration.frames), "frames combined"sv);
        if (stated.failed())
            return stated;
    }
    const SourceEncoding encoding = common(sources, [](const ImageMetadata& metadata) {
                                        return std::optional<SourceEncoding>{metadata.codec().sourceEncoding()};
                                    }).value_or(SourceEncoding{});
    Result<CodecMetadata> codec = CodecMetadata::create(encoding, std::move(kept));
    if (!codec.hasValue())
        return codec.status();
    return withoutProcessingStateCards(*codec);
}
} // namespace

Result<ImageMetadata> combinedImageMetadata(std::span<const ImageMetadata* const> sources,
                                            const OperationEffect& effect, const OperationOutcome& outcome)
{
    if (sources.empty())
        return refuse(ErrorCode::InvalidArgument, "a combination has at least one source", "sources");
    if (std::find(sources.begin(), sources.end(), nullptr) != sources.end())
        return refuse(ErrorCode::InvalidArgument, "every source of a combination is an image's metadata", "sources");
    Result<MetadataTransition> change = operationTransition(effect, outcome);
    if (!change.hasValue())
        return change.status();

    Result<AcquisitionMetadata> acquisition = combinedAcquisition(sources);
    if (!acquisition.hasValue())
        return acquisition.status();

    std::optional<ImageSemantics> semantics = change->semantics;
    if (!semantics.has_value())
    {
        Result<ImageSemantics> agreed = agreedSemantics(sources, effect.validity);
        if (!agreed.hasValue())
            return agreed.status();
        semantics = std::move(*agreed);
    }

    const bool keepSolution = effect.solution == SolutionEffect::Preserves;
    const AstrometricSolution astrometry = keepSolution ? sources[0]->astrometry() : AstrometricSolution{};

    Result<Provenance> provenance = sources[0]->provenance().appended(*change->record);
    if (!provenance.hasValue())
        return provenance.status();

    CodecMetadata codec;
    if (effect.codecMetadata == CodecMetadataEffect::Preserves)
    {
        Result<CodecMetadata> cards = combinedCards(sources, keepSolution);
        if (!cards.hasValue())
            return cards.status();
        codec = std::move(*cards);
    }

    std::optional<OutputIntent> intent;
    if (effect.outputIntent == OutputIntentEffect::Preserves)
        intent = common(sources, [](const ImageMetadata& metadata) { return metadata.outputIntent(); });

    return ImageMetadata::create(std::move(*acquisition), std::move(*semantics), astrometry, std::move(*provenance),
                                 std::move(codec), std::move(intent));
}

} // namespace an
