// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The metadata of an image a process combines from several, and the linearity it reads of its sources, as the shared
/// process helpers state them: the combination's metadata built whole through the engine - one exposure, the sum, and
/// only the facts and cards every source shares - and a source's linearity read from its meaning, or from the `ANLIN`
/// card where its meaning states none.

#include "processes/ProcessMetadataUtils.h"

#include <AfterNightEngine/Algorithms/Color/LrgbCombination.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/Provenance.h>

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

/// A FITS card, built or the case fails.
[[nodiscard]] an::CodecCard fitsCard(std::string keyword, std::string value)
{
    an::Result<an::CodecCard> made =
        an::CodecCard::create(an::CodecNamespace::Fits, std::move(keyword), std::move(value), std::string());
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : *an::CodecCard::create(an::CodecNamespace::Fits, "X", "", "");
}

/// What a source states: the facts its acquisition record and its header cards carry.
struct SourceFacts
{
    double exposureSeconds = 0.0;
    std::string object;
    std::string camera;
    int iso = 0;
    int offset = 0;
    std::string telescope;
    std::string siteLatitude;
    std::string siteLongitude;
    std::string filter;
};

/// A source's metadata: the acquisition record its codec would project, and the cards its header holds.
[[nodiscard]] an::ImageMetadata sourceOf(const SourceFacts& facts, an::ImageSemanticsRequest semantics = {},
                                         std::vector<an::CodecCard> extraCards = {})
{
    an::Result<an::AcquisitionMetadata> acquisition =
        an::AcquisitionMetadata::create(an::AcquisitionMetadataRequest{.instrumentName = facts.camera,
                                                                       .telescopeName = facts.telescope,
                                                                       .objectName = facts.object,
                                                                       .filterName = facts.filter,
                                                                       .exposureSeconds = facts.exposureSeconds});
    EXPECT_TRUE(acquisition.hasValue()) << acquisition.status().message();
    an::Result<an::ImageSemantics> meaning = an::ImageSemantics::create(semantics);
    EXPECT_TRUE(meaning.hasValue()) << meaning.status().message();

    std::vector<an::CodecCard> cards{fitsCard("EXPTIME", std::to_string(facts.exposureSeconds)),
                                     fitsCard("OBJECT", facts.object),
                                     fitsCard("INSTRUME", facts.camera),
                                     fitsCard("ISOSPEED", std::to_string(facts.iso)),
                                     fitsCard("OFFSET", std::to_string(facts.offset)),
                                     fitsCard("SOFTWARE", "NINA"),
                                     fitsCard("DATE", "2026-03-04T21:30:00"),
                                     fitsCard("TELESCOP", facts.telescope),
                                     fitsCard("SITELAT", facts.siteLatitude),
                                     fitsCard("SITELONG", facts.siteLongitude),
                                     fitsCard("FILTER", facts.filter)};
    for (an::CodecCard& card : extraCards)
    {
        cards.push_back(std::move(card));
    }
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, cards);
    EXPECT_TRUE(codec.hasValue()) << codec.status().message();

    an::Result<an::Provenance> provenance = an::Provenance::create(an::ProvenanceSourceKind::File, "source.fits");
    EXPECT_TRUE(provenance.hasValue());
    return an::ImageMetadata::create(*acquisition, *meaning, an::AstrometricSolution{}, std::move(*provenance),
                                     std::move(*codec), std::nullopt);
}

/// The value of a FITS card of a card set, or nothing when the set holds no such card.
[[nodiscard]] std::optional<std::string> cardValue(const an::CodecMetadata& codec, const std::string& keyword)
{
    for (const an::CodecCard& card : codec.cards())
    {
        if (card.origin() == an::CodecNamespace::Fits && card.keyword() == keyword)
        {
            return card.value();
        }
    }
    return std::nullopt;
}

/// The cards of a linear product, as the product records one: `ANLIN` linear.
[[nodiscard]] std::vector<an::CodecCard> linearCards()
{
    an::Result<an::CodecMetadata> empty = an::CodecMetadata::create(an::SourceEncoding{}, {});
    EXPECT_TRUE(empty.hasValue());
    an::ProcessingStateRecord record;
    record.linearity = an::RecordedLinearity::Linear;
    an::Result<an::CodecMetadata> recorded = an::withProcessingStateCards(*empty, record);
    EXPECT_TRUE(recorded.hasValue()) << recorded.status().message();
    return recorded.hasValue() ? std::vector<an::CodecCard>(recorded->cards().begin(), recorded->cards().end())
                               : std::vector<an::CodecCard>{};
}

/// Combines two sources as an LRGB combination does - the engine's published effect, whose meaning the process
/// restates - or fails the case.
[[nodiscard]] an::ImageMetadata combined(const an::ImageMetadata& first, const an::ImageMetadata& second,
                                         an::ImageSemanticsRequest meaning = {})
{
    const std::array<const an::ImageMetadata*, 2u> sources{&first, &second};
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    EXPECT_TRUE(semantics.hasValue());
    an::Result<an::ImageMetadata> merged = process_utils::combinedProcessMetadata(
        sources, an::kLrgbCombinationEffect, "combineLrgb", "LRGB Combination", *semantics);
    EXPECT_TRUE(merged.hasValue()) << merged.status().message();
    return merged.hasValue() ? std::move(*merged) : an::ImageMetadata{};
}

const SourceFacts kRed{30.0, "M42", "ASI2600MM", 100, 50, "Esprit 100", "34.0500", "-118.2500", "R"};
const SourceFacts kGreen{45.0, "M42", "ASI2600MM", 100, 50, "Esprit 100", "34.0500", "-118.2500", "G"};

} // namespace

// A combination states one exposure, the sum of its sources', and keeps every fact and card the sources share - the
// object, the camera, the ISO speed and offset, the telescope and the site; the filters differ, so it states none.
TEST(ImageMetadataMerge, CombinedImagePreservesSharedMetadataAndSumsExposure)
{
    const an::ImageMetadata metadata = combined(sourceOf(kRed), sourceOf(kGreen));

    const an::AcquisitionMetadata& acquisition = metadata.acquisition();
    ASSERT_TRUE(acquisition.exposureSeconds().has_value());
    EXPECT_NEAR(*acquisition.exposureSeconds(), 75.0, 1e-9);
    EXPECT_EQ(acquisition.objectName(), "M42");
    EXPECT_EQ(acquisition.instrumentName(), "ASI2600MM");
    EXPECT_EQ(acquisition.telescopeName(), "Esprit 100");
    EXPECT_TRUE(acquisition.filterName().empty());

    const an::CodecMetadata& codec = metadata.codec();
    EXPECT_EQ(cardValue(codec, "ISOSPEED"), std::optional<std::string>("100"));
    EXPECT_EQ(cardValue(codec, "OFFSET"), std::optional<std::string>("50"));
    EXPECT_EQ(cardValue(codec, "TELESCOP"), std::optional<std::string>("Esprit 100"));
    EXPECT_EQ(cardValue(codec, "SITELAT"), std::optional<std::string>("34.0500"));
    EXPECT_EQ(cardValue(codec, "SITELONG"), std::optional<std::string>("-118.2500"));
    EXPECT_EQ(cardValue(codec, "SOFTWARE"), std::optional<std::string>("NINA"));
    EXPECT_FALSE(cardValue(codec, "FILTER").has_value());
    // The sum is the record's; no source's exposure card survives to contradict it.
    EXPECT_FALSE(cardValue(codec, "EXPTIME").has_value());
}

// A combination makes its own claim about what it means rather than inheriting its first source's: the sources'
// processing-state cards do not survive, and the meaning is the one the process states. The sources are still read
// as declared linear.
TEST(ImageMetadataMerge, CombinedImageClearsInheritedProcessingState)
{
    const an::ImageMetadata red = sourceOf(kRed, {}, linearCards());
    const an::ImageMetadata green = sourceOf(kGreen, {}, linearCards());
    const std::array<const an::ImageMetadata*, 2u> sources{&red, &green};
    EXPECT_TRUE(process_utils::allSourcesDeclaredLinear(sources));
    EXPECT_FALSE(process_utils::anySourceDeclaredNonlinear(sources));

    const an::ImageMetadata unclaimed = combined(red, green);
    EXPECT_FALSE(cardValue(unclaimed.codec(), "ANLIN").has_value());
    EXPECT_EQ(an::readProcessingStateCards(unclaimed.codec()).linearity, an::RecordedLinearity::Unknown);
    EXPECT_EQ(unclaimed.semantics().transferFunction(), an::TransferFunction::Unspecified);
    EXPECT_EQ(process_utils::declaredLinearity(unclaimed), an::RecordedLinearity::Unknown);

    const an::ImageMetadata claimed =
        combined(red, green, an::ImageSemanticsRequest{.transferFunction = an::TransferFunction::Linear});
    EXPECT_EQ(process_utils::declaredLinearity(claimed), an::RecordedLinearity::Linear);
    ASSERT_FALSE(claimed.provenance().entries().empty());
    EXPECT_EQ(claimed.provenance().entries().back().operation(), "combineLrgb");
}

// Sources that disagree keep nothing they disagree on: no object, camera, ISO speed, offset, telescope, site or filter
// survives, and the exposure is still the sum.
TEST(ImageMetadataMerge, CombinedImageDropsMismatchedOrComponentSpecificMetadata)
{
    const an::ImageMetadata metadata =
        combined(sourceOf({20.0, "M42", "ASI2600MM", 100, 40, "Esprit 100", "34.0500", "-118.2500", "Ha"}),
                 sourceOf({40.0, "M31", "ASI6200MM", 200, 55, "C9.25", "-33.9500", "151.1800", "OIII"}));

    const an::AcquisitionMetadata& acquisition = metadata.acquisition();
    ASSERT_TRUE(acquisition.exposureSeconds().has_value());
    EXPECT_NEAR(*acquisition.exposureSeconds(), 60.0, 1e-9);
    EXPECT_TRUE(acquisition.objectName().empty());
    EXPECT_TRUE(acquisition.instrumentName().empty());
    EXPECT_TRUE(acquisition.telescopeName().empty());
    EXPECT_TRUE(acquisition.filterName().empty());
    for (const char* keyword :
         {"OBJECT", "INSTRUME", "ISOSPEED", "OFFSET", "TELESCOP", "SITELAT", "SITELONG", "FILTER", "EXPTIME"})
    {
        EXPECT_FALSE(cardValue(metadata.codec(), keyword).has_value()) << keyword;
    }
    EXPECT_EQ(cardValue(metadata.codec(), "SOFTWARE"), std::optional<std::string>("NINA"));
}

// A source's linearity is what its meaning declares, and its `ANLIN` card only where its meaning declares nothing; a
// source that declares neither is neither linear nor nonlinear.
TEST(ImageMetadataMerge, ASourcesLinearityIsItsMeaningsOrElseItsCards)
{
    an::ProcessingStateRecord nonlinear;
    nonlinear.linearity = an::RecordedLinearity::Nonlinear;
    an::Result<an::CodecMetadata> empty = an::CodecMetadata::create(an::SourceEncoding{}, {});
    ASSERT_TRUE(empty.hasValue());
    an::Result<an::CodecMetadata> nonlinearCodec = an::withProcessingStateCards(*empty, nonlinear);
    ASSERT_TRUE(nonlinearCodec.hasValue());
    const std::vector<an::CodecCard> nonlinearCards(nonlinearCodec->cards().begin(), nonlinearCodec->cards().end());

    const an::ImageMetadata linearMeaning =
        sourceOf(kRed, an::ImageSemanticsRequest{.transferFunction = an::TransferFunction::Linear}, nonlinearCards);
    EXPECT_EQ(process_utils::declaredLinearity(linearMeaning), an::RecordedLinearity::Linear)
        << "the meaning decides when it states a transfer function";
    const an::ImageMetadata srgb =
        sourceOf(kRed, an::ImageSemanticsRequest{.transferFunction = an::TransferFunction::Srgb});
    EXPECT_EQ(process_utils::declaredLinearity(srgb), an::RecordedLinearity::Nonlinear);
    const an::ImageMetadata gamma = sourceOf(
        kRed, an::ImageSemanticsRequest{.transferFunction = an::TransferFunction::PureGamma, .gammaExponent = 2.2});
    EXPECT_EQ(process_utils::declaredLinearity(gamma), an::RecordedLinearity::Nonlinear);
    const an::ImageMetadata nonlinearCard = sourceOf(kRed, {}, nonlinearCards);
    EXPECT_EQ(process_utils::declaredLinearity(nonlinearCard), an::RecordedLinearity::Nonlinear);
    const an::ImageMetadata undeclared = sourceOf(kRed);
    EXPECT_EQ(process_utils::declaredLinearity(undeclared), an::RecordedLinearity::Unknown);

    const an::ImageMetadata linearCard = sourceOf(kGreen, {}, linearCards());
    const std::array<const an::ImageMetadata*, 3u> mixed{&linearCard, nullptr, &nonlinearCard};
    EXPECT_FALSE(process_utils::allSourcesDeclaredLinear(mixed));
    EXPECT_TRUE(process_utils::anySourceDeclaredNonlinear(mixed));
    const std::array<const an::ImageMetadata*, 2u> linearAndNull{&linearCard, nullptr};
    EXPECT_TRUE(process_utils::allSourcesDeclaredLinear(linearAndNull)) << "a null source is passed over";
    const std::array<const an::ImageMetadata*, 2u> linearAndUndeclared{&linearCard, &undeclared};
    EXPECT_FALSE(process_utils::allSourcesDeclaredLinear(linearAndUndeclared));
    EXPECT_FALSE(process_utils::anySourceDeclaredNonlinear(linearAndUndeclared));
    const std::array<const an::ImageMetadata*, 1u> none{nullptr};
    EXPECT_FALSE(process_utils::allSourcesDeclaredLinear(none)) << "no source is no claim";
}
