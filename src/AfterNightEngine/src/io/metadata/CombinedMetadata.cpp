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
// and the suite holds the two lists together.

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
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
    for (const ImageMetadata* const source : sources)
        if (const std::optional<double> exposure = source->acquisition().exposureSeconds(); exposure.has_value())
            request.exposureSeconds = request.exposureSeconds.value_or(0.0) + *exposure;
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
    request.exposureStart =
        common(sources, [](const ImageMetadata& metadata) { return metadata.acquisition().exposureStart(); });
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
    const std::span<const CodecCard> cards = sources[0]->codec().cards();
    const CardFields first = fieldsOf(cards);
    std::vector<CodecCard> kept;
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
        const CardKey key = keyOf(cards, field);
        const bool everywhere = std::all_of(others.begin(), others.end(), [&](const std::vector<CardKey>& keys) {
            return std::binary_search(keys.begin(), keys.end(), key);
        });
        if (everywhere)
            keep();
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
