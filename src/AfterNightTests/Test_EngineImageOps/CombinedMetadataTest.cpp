// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the metadata of an image combined from several sources.
//
// Each of the six metadata objects follows one rule, and each rule is pinned on sources built to tell it apart
// from its neighbours: an exposure that is summed rather than kept, a fact kept only when every source states it,
// a meaning taken from the operation or agreed by the sources, the first source's solution and lineage, cards kept
// only when every source states them identically - except the first source's world coordinates, which follow the
// solution, and the cards that describe one file, which never survive - and an output intent kept only when
// common. The exposure spellings the combination drops are held to the ones the codec projection reads, so the
// two cannot drift apart.

#include <AfterNightEngine/IO/CombinedMetadata.h>

#include <AfterNightEngine/Algorithms/Astrometry/AstrometryHints.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/Provenance.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

/// A card, built or the test fails.
[[nodiscard]] an::CodecCard card(an::CodecNamespace origin, std::string keyword, std::string value,
                                 std::string comment = {})
{
    an::Result<an::CodecCard> made =
        an::CodecCard::create(origin, std::move(keyword), std::move(value), std::move(comment));
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : *an::CodecCard::create(an::CodecNamespace::Fits, "X", "", "");
}

/// A FITS card, built or the test fails.
[[nodiscard]] an::CodecCard fits(std::string keyword, std::string value, std::string comment = {})
{
    return card(an::CodecNamespace::Fits, std::move(keyword), std::move(value), std::move(comment));
}

/// The parts of one source's metadata a case states; everything else stays as it defaults.
struct SourceParts
{
    an::AcquisitionMetadataRequest acquisition{};
    an::ImageSemanticsRequest semantics{};
    std::optional<an::AstrometricSolutionRequest> solution{};
    std::vector<std::string> lineage{};
    an::SourceEncoding encoding{};
    std::vector<an::CodecCard> cards{};
    std::optional<an::OutputIntentRequest> intent{};
};

/// One source's metadata, built or the test fails.
[[nodiscard]] an::ImageMetadata source(const SourceParts& parts)
{
    an::Result<an::AcquisitionMetadata> acquisition = an::AcquisitionMetadata::create(parts.acquisition);
    EXPECT_TRUE(acquisition.hasValue()) << acquisition.status().message();
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(parts.semantics);
    EXPECT_TRUE(semantics.hasValue()) << semantics.status().message();
    an::AstrometricSolution solution;
    if (parts.solution.has_value())
    {
        an::Result<an::AstrometricSolution> made = an::AstrometricSolution::create(*parts.solution);
        EXPECT_TRUE(made.hasValue()) << made.status().message();
        solution = made.hasValue() ? *made : an::AstrometricSolution{};
    }
    an::Result<an::Provenance> provenance = an::Provenance::create(an::ProvenanceSourceKind::File, "source.fits");
    EXPECT_TRUE(provenance.hasValue());
    an::Provenance lineage = provenance.hasValue() ? std::move(*provenance) : an::Provenance{};
    for (const std::string& step : parts.lineage)
    {
        an::Result<an::Provenance> appended = lineage.appended(*an::ProvenanceEntry::create(step, ""));
        EXPECT_TRUE(appended.hasValue());
        lineage = appended.hasValue() ? std::move(*appended) : lineage;
    }
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(parts.encoding, parts.cards);
    EXPECT_TRUE(codec.hasValue()) << codec.status().message();
    std::optional<an::OutputIntent> intent;
    if (parts.intent.has_value())
    {
        an::Result<an::OutputIntent> made = an::OutputIntent::create(*parts.intent);
        EXPECT_TRUE(made.hasValue()) << made.status().message();
        if (made.hasValue())
            intent = std::move(*made);
    }
    return an::ImageMetadata::create(acquisition.hasValue() ? *acquisition : an::AcquisitionMetadata{},
                                     semantics.hasValue() ? *semantics : an::ImageSemantics{}, solution,
                                     std::move(lineage), codec.hasValue() ? std::move(*codec) : an::CodecMetadata{},
                                     std::move(intent));
}

/// A combination's declaration: restating the meaning, keeping the grid and the cards, combining validity.
constexpr an::OperationEffect kRestating{.description = an::DescriptionEffect::Restates,
                                         .semantics = an::SemanticEffect::Restates,
                                         .validity = an::ValidityTransition{.policy = an::ValidityPolicy::Combine}};

/// A combination's declaration that preserves the meaning and combines validity.
constexpr an::OperationEffect kPreserving{.validity = an::ValidityTransition{.policy = an::ValidityPolicy::Combine}};

/// What a combination reports when the effect preserves the meaning.
[[nodiscard]] an::OperationOutcome preserved()
{
    return an::OperationOutcome{.operation = "combineImages", .detail = "two sources"};
}

/// Combines two sources, or fails the test.
[[nodiscard]] an::ImageMetadata combine(const an::ImageMetadata& first, const an::ImageMetadata& second,
                                        const an::OperationEffect& effect = kPreserving,
                                        const an::OperationOutcome& outcome = preserved())
{
    const std::array<const an::ImageMetadata*, 2u> sources{&first, &second};
    an::Result<an::ImageMetadata> combined = an::combinedImageMetadata(sources, effect, outcome);
    EXPECT_TRUE(combined.hasValue()) << combined.status().message();
    return combined.hasValue() ? std::move(*combined) : an::ImageMetadata{};
}

/// The keywords of a card set, in order, with their namespaces' names.
[[nodiscard]] std::vector<std::string> keywordsOf(const an::CodecMetadata& codec)
{
    std::vector<std::string> keywords;
    for (const an::CodecCard& held : codec.cards())
    {
        keywords.push_back(std::string(an::codecNamespaceName(held.origin())) + ":" + held.keyword());
    }
    return keywords;
}

/// A tangent-plane solution's summary.
[[nodiscard]] an::AstrometricSolutionRequest solutionAt(double rightAscension)
{
    return an::AstrometricSolutionRequest{
        .referencePoint = {.rightAscensionDegrees = rightAscension, .declinationDegrees = -5.4},
        .referencePixelX = 1023.5,
        .referencePixelY = 767.5,
        .pixelScaleArcsecPerPixel = 2.2,
        .orientationDegrees = 0.1,
        .mirrored = false,
        .hasDistortion = true};
}

/// The world coordinate cards of a distorted tangent-plane solution at a right ascension.
[[nodiscard]] std::vector<an::CodecCard> wcsCardsAt(const std::string& rightAscension)
{
    return {fits("RADESYS", "'ICRS'"),        fits("CTYPE1", "'RA---TAN-SIP'"),
            fits("CTYPE2", "'DEC--TAN-SIP'"), fits("CRVAL1", rightAscension),
            fits("CRVAL2", "-5.4"),           fits("CRPIX1", "1024.0"),
            fits("CRPIX2", "768.0"),          fits("CD1_1", "-6.1E-04"),
            fits("CD1_2", "1.2E-06"),         fits("CD2_1", "1.1E-06"),
            fits("CD2_2", "6.1E-04"),         fits("A_ORDER", "2"),
            fits("A_2_0", "8.9E-07"),         fits("B_ORDER", "2"),
            fits("B_0_2", "-1.3E-06")};
}

} // namespace

TEST(CombinedMetadataTest, SumsTheExposuresTheSourcesState)
{
    const an::ImageMetadata three = source({.acquisition = {.exposureSeconds = 300.0}});
    const an::ImageMetadata one = source({.acquisition = {.exposureSeconds = 120.5}});
    const an::ImageMetadata none = source({});
    EXPECT_EQ(combine(three, one).acquisition().exposureSeconds(), std::optional<double>(420.5));
    EXPECT_EQ(combine(three, none).acquisition().exposureSeconds(), std::optional<double>(300.0));
    EXPECT_EQ(combine(none, three).acquisition().exposureSeconds(), std::optional<double>(300.0));
    EXPECT_EQ(combine(none, none).acquisition().exposureSeconds(), std::nullopt);

    const std::array<const an::ImageMetadata*, 3u> sources{&three, &three, &one};
    const an::Result<an::ImageMetadata> triple = an::combinedImageMetadata(sources, kPreserving, preserved());
    ASSERT_TRUE(triple.hasValue()) << triple.status().message();
    EXPECT_EQ(triple->acquisition().exposureSeconds(), std::optional<double>(720.5));
}

TEST(CombinedMetadataTest, KeepsAnAcquisitionFactOnlyWhenEverySourceStatesIt)
{
    an::RawCameraMetadata camera;
    camera.blackLevel = 512u;
    camera.whiteLevel = 16383u;
    camera.isoSpeed = 800.0;
    const an::AcquisitionMetadataRequest shared{.instrumentName = "ZWO ASI2600MM Pro",
                                                .telescopeName = "RedCat 51",
                                                .observerName = "Observer",
                                                .objectName = "M 42",
                                                .filterName = "Ha",
                                                .sensorTemperatureCelsius = -10.0,
                                                .gainElectronsPerAdu = 0.25,
                                                .binning = an::PixelBinning{.horizontal = 2u, .vertical = 2u},
                                                .exposureStart = an::UtcInstant{1700000000000000000},
                                                .rawCamera = camera};
    const an::ImageMetadata first = source({.acquisition = shared});
    const an::ImageMetadata same = source({.acquisition = shared});
    const an::AcquisitionMetadata kept = combine(first, same).acquisition();
    EXPECT_EQ(kept.instrumentName(), "ZWO ASI2600MM Pro");
    EXPECT_EQ(kept.telescopeName(), "RedCat 51");
    EXPECT_EQ(kept.observerName(), "Observer");
    EXPECT_EQ(kept.objectName(), "M 42");
    EXPECT_EQ(kept.filterName(), "Ha");
    EXPECT_EQ(kept.sensorTemperatureCelsius(), std::optional<double>(-10.0));
    EXPECT_EQ(kept.gainElectronsPerAdu(), std::optional<double>(0.25));
    EXPECT_EQ(kept.binning(), (std::optional<an::PixelBinning>{an::PixelBinning{2u, 2u}}));
    EXPECT_EQ(kept.exposureStart(), (std::optional<an::UtcInstant>{an::UtcInstant{1700000000000000000}}));
    EXPECT_EQ(kept.rawCamera(), std::optional<an::RawCameraMetadata>(camera));

    an::RawCameraMetadata otherCamera = camera;
    otherCamera.isoSpeed = 1600.0;
    const an::ImageMetadata differing = source({.acquisition = {.instrumentName = "ZWO ASI2600MM Pro",
                                                                .telescopeName = "Askar 107",
                                                                .observerName = "Observer",
                                                                .objectName = "M 43",
                                                                .filterName = "OIII",
                                                                .sensorTemperatureCelsius = -9.5,
                                                                .gainElectronsPerAdu = 0.25,
                                                                .binning = an::PixelBinning{1u, 1u},
                                                                .exposureStart = an::UtcInstant{1700000300000000000},
                                                                .rawCamera = otherCamera}});
    const an::AcquisitionMetadata dropped = combine(first, differing).acquisition();
    EXPECT_EQ(dropped.instrumentName(), "ZWO ASI2600MM Pro");
    EXPECT_EQ(dropped.observerName(), "Observer");
    EXPECT_EQ(dropped.gainElectronsPerAdu(), std::optional<double>(0.25));
    EXPECT_EQ(dropped.telescopeName(), "");
    EXPECT_EQ(dropped.objectName(), "");
    EXPECT_EQ(dropped.filterName(), "");
    EXPECT_EQ(dropped.sensorTemperatureCelsius(), std::nullopt);
    EXPECT_EQ(dropped.binning(), std::nullopt);
    EXPECT_EQ(dropped.rawCamera(), std::nullopt);
    // The observation began when the earliest source's did, whichever source that is.
    EXPECT_EQ(dropped.exposureStart(), (std::optional<an::UtcInstant>{an::UtcInstant{1700000000000000000}}));
    EXPECT_EQ(combine(differing, first).acquisition().exposureStart(),
              (std::optional<an::UtcInstant>{an::UtcInstant{1700000000000000000}}));

    // A fact one source does not state is not common; the start one source states is still the earliest.
    const an::AcquisitionMetadata partial = combine(first, source({})).acquisition();
    EXPECT_EQ(partial, *an::AcquisitionMetadata::create({.exposureStart = an::UtcInstant{1700000000000000000}}));
}

TEST(CombinedMetadataTest, TakesTheOutcomesMeaningWhenTheEffectRestatesIt)
{
    const an::ImageMetadata linear =
        source({.semantics = {.transferFunction = an::TransferFunction::Linear,
                              .processingState = {.biasSubtracted = true, .darkSubtracted = true}}});
    const an::ImageSemantics claimed = *an::ImageSemantics::create(
        {.transferFunction = an::TransferFunction::Srgb, .processingState = {.colorCalibrated = true}});
    const an::OperationOutcome outcome{.operation = "composeBlend", .detail = "Screen", .semantics = claimed};
    EXPECT_EQ(combine(linear, linear, kRestating, outcome).semantics(), claimed);

    const std::array<const an::ImageMetadata*, 2u> sources{&linear, &linear};
    const an::Result<an::ImageMetadata> unstated = an::combinedImageMetadata(sources, kRestating, preserved());
    ASSERT_FALSE(unstated.hasValue());
    EXPECT_EQ(unstated.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_NE(std::string_view(unstated.status().message()).find("MissingSemantics"), std::string_view::npos)
        << unstated.status().message();
}

TEST(CombinedMetadataTest, KeepsOnlyTheMeaningEverySourceAgreesOnWhenTheEffectPreservesIt)
{
    const an::ImageMetadata calibrated = source(
        {.semantics = {.transferFunction = an::TransferFunction::Linear,
                       .processingState = {.biasSubtracted = true, .darkSubtracted = true, .flatFielded = true}}});
    const an::ImageMetadata partly =
        source({.semantics = {.transferFunction = an::TransferFunction::Linear,
                              .processingState = {.biasSubtracted = true, .flatFielded = true, .registered = true}}});
    const an::ImageSemantics agreed = combine(calibrated, partly).semantics();
    EXPECT_EQ(agreed.transferFunction(), an::TransferFunction::Linear);
    EXPECT_EQ(agreed.processingState(), (an::ProcessingState{.biasSubtracted = true, .flatFielded = true}));
    EXPECT_EQ(agreed.gammaExponent(), std::nullopt);

    const an::ImageMetadata gamma =
        source({.semantics = {.transferFunction = an::TransferFunction::PureGamma, .gammaExponent = 2.2}});
    const an::ImageSemantics differing = combine(calibrated, gamma).semantics();
    EXPECT_EQ(differing.transferFunction(), an::TransferFunction::Unspecified);
    EXPECT_EQ(differing.gammaExponent(), std::nullopt);
    EXPECT_EQ(differing.processingState(), an::ProcessingState{});
    EXPECT_EQ(combine(gamma, gamma).semantics().gammaExponent(), std::optional<double>(2.2));

    // The validity follows the effect's policy over the sources' rules.
    const an::ImageMetadata finite =
        source({.semantics = {.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly})}});
    EXPECT_EQ(combine(finite, finite).semantics().validity().rule(), an::ValidityRule::FiniteOnly);
    EXPECT_EQ(combine(finite, calibrated).semantics().validity().rule(), an::ValidityRule::ExplicitMask);
    const an::OperationEffect preservingValidity{};
    const std::array<const an::ImageMetadata*, 2u> disagreeing{&finite, &calibrated};
    const an::Result<an::ImageMetadata> refused =
        an::combinedImageMetadata(disagreeing, preservingValidity, preserved());
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
}

TEST(CombinedMetadataTest, KeepsTheFirstSourcesSolutionOnlyWhenTheEffectPreservesIt)
{
    const an::ImageMetadata first = source({.solution = solutionAt(83.8)});
    const an::ImageMetadata second = source({.solution = solutionAt(84.0)});
    EXPECT_EQ(combine(first, second).astrometry(), *an::AstrometricSolution::create(solutionAt(83.8)));
    EXPECT_EQ(combine(second, first).astrometry(), *an::AstrometricSolution::create(solutionAt(84.0)));

    const an::OperationEffect invalidating{.solution = an::SolutionEffect::Invalidates,
                                           .validity = {.policy = an::ValidityPolicy::Combine}};
    EXPECT_FALSE(combine(first, second, invalidating).astrometry().present());
}

TEST(CombinedMetadataTest, AppendsTheOutcomeToTheFirstSourcesLineage)
{
    const an::ImageMetadata first = source({.lineage = {"decode", "crop"}});
    const an::ImageMetadata second = source({.lineage = {"decode", "stretch"}});
    const an::Provenance lineage = combine(first, second).provenance();
    EXPECT_EQ(lineage.sourceKind(), an::ProvenanceSourceKind::File);
    EXPECT_EQ(lineage.sourceName(), "source.fits");
    ASSERT_EQ(lineage.entries().size(), 3u);
    EXPECT_EQ(lineage.entries()[0].operation(), "decode");
    EXPECT_EQ(lineage.entries()[1].operation(), "crop");
    EXPECT_EQ(lineage.entries()[2].operation(), "combineImages");
    EXPECT_EQ(lineage.entries()[2].detail(), "two sources");
}

TEST(CombinedMetadataTest, KeepsTheCardsEverySourceStatesIdenticallyInTheFirstSourcesOrder)
{
    const an::ImageMetadata first =
        source({.cards = {fits("TELESCOP", "'RedCat 51'", "first's comment"), fits("OBJECT", "'M 42'"),
                          fits("AIRMASS", "1.2"), fits("HISTORY", "", "calibrated"), fits("SITELAT", "40.1"),
                          card(an::CodecNamespace::Xisf, "Observation:Center", "83.8")}});
    const an::ImageMetadata second =
        source({.cards = {fits("SITELAT", "40.1"), fits("OBJECT", "'M 43'"),
                          fits("TELESCOP", "'RedCat 51'", "second's comment"), fits("HISTORY", "", "calibrated"),
                          card(an::CodecNamespace::Tiff, "Observation:Center", "83.8")}});
    const an::CodecMetadata cards = combine(first, second).codec();
    EXPECT_EQ(keywordsOf(cards), (std::vector<std::string>{"Fits:TELESCOP", "Fits:HISTORY", "Fits:SITELAT"}));
    ASSERT_NE(cards.find(an::CodecNamespace::Fits, "TELESCOP"), nullptr);
    EXPECT_EQ(cards.find(an::CodecNamespace::Fits, "TELESCOP")->comment(), "first's comment");
}

TEST(CombinedMetadataTest, TheFirstSourcesWorldCoordinateCardsFollowItsSolution)
{
    std::vector<an::CodecCard> firstCards = wcsCardsAt("83.8");
    firstCards.push_back(fits("OBJECT", "'M 42'"));
    std::vector<an::CodecCard> secondCards = wcsCardsAt("84.0");
    secondCards.push_back(fits("OBJECT", "'M 42'"));
    const an::ImageMetadata first = source({.solution = solutionAt(83.8), .cards = firstCards});
    const an::ImageMetadata second = source({.solution = solutionAt(84.0), .cards = secondCards});

    const an::CodecMetadata kept = combine(first, second).codec();
    std::vector<an::CodecCard> expected = wcsCardsAt("83.8");
    expected.push_back(fits("OBJECT", "'M 42'"));
    EXPECT_EQ(std::vector<an::CodecCard>(kept.cards().begin(), kept.cards().end()), expected);
    EXPECT_EQ(an::checkWcsCards(kept.cards()), an::WcsCardError::None);

    const an::OperationEffect invalidating{.solution = an::SolutionEffect::Invalidates,
                                           .validity = {.policy = an::ValidityPolicy::Combine}};
    const an::CodecMetadata withoutSolution = combine(first, first, invalidating).codec();
    EXPECT_EQ(keywordsOf(withoutSolution), (std::vector<std::string>{"Fits:OBJECT"}));
}

TEST(CombinedMetadataTest, AValueContinuedAcrossCardsIsComparedAndKeptWhole)
{
    // Two processing histories that share their first card's text and differ after it: kept as a truncated first
    // card, either would read as a whole history that neither image has.
    const std::string shared(an::kMaxCodecCardTextBytes, 'h');
    const auto history = [&](std::string tail) {
        return std::vector<an::CodecCard>{
            card(an::CodecNamespace::Xisf, "PixInsight:ProcessingHistory", shared, "String"),
            card(an::CodecNamespace::Xisf, "PixInsight:ProcessingHistory", std::move(tail), "continued"),
            card(an::CodecNamespace::Xisf, "Observation:Object:Name", "M42", "String")};
    };
    const an::ImageMetadata first = source({.cards = history("<first/>")});
    const an::ImageMetadata second = source({.cards = history("<second/>")});
    EXPECT_EQ(keywordsOf(combine(first, second).codec()), (std::vector<std::string>{"Xisf:Observation:Object:Name"}));
    // The same history in both is kept, every card of it.
    const an::CodecMetadata same = combine(first, source({.cards = history("<first/>")})).codec();
    EXPECT_EQ(std::vector<an::CodecCard>(same.cards().begin(), same.cards().end()), history("<first/>"));
}

TEST(CombinedMetadataTest, TheFirstSourcesXisfSolutionFollowsItsSolutionWithTheKeywords)
{
    // A PixInsight-solved first source: its solution's properties are kept whole with its keywords when the
    // combination keeps the solution, though no other source states them, and dropped with them when it does not.
    std::vector<an::CodecCard> firstCards = wcsCardsAt("83.8");
    firstCards.push_back(card(an::CodecNamespace::Xisf, "AstrometricSolution:ControlPoints:Image",
                              std::string(an::kMaxCodecCardTextBytes, '1'), "F64Matrix 1000x2"));
    firstCards.push_back(card(an::CodecNamespace::Xisf, "AstrometricSolution:ControlPoints:Image", "2", "continued"));
    firstCards.push_back(card(an::CodecNamespace::Xisf, "PCL:AstrometricSolution:Grid:Fingerprint", "f00d", "String"));
    firstCards.push_back(fits("OBJECT", "'M 42'"));
    std::vector<an::CodecCard> secondCards = wcsCardsAt("84.0");
    secondCards.push_back(fits("OBJECT", "'M 42'"));
    const an::ImageMetadata first = source({.solution = solutionAt(83.8), .cards = firstCards});
    const an::ImageMetadata second = source({.solution = solutionAt(84.0), .cards = secondCards});

    const an::CodecMetadata kept = combine(first, second).codec();
    EXPECT_EQ(std::vector<an::CodecCard>(kept.cards().begin(), kept.cards().end()), firstCards);

    const an::OperationEffect invalidating{.solution = an::SolutionEffect::Invalidates,
                                           .validity = {.policy = an::ValidityPolicy::Combine}};
    EXPECT_EQ(keywordsOf(combine(first, first, invalidating).codec()), (std::vector<std::string>{"Fits:OBJECT"}));
}

TEST(CombinedMetadataTest, DropsTheCardsThatDescribeOneFile)
{
    an::CodecMetadata described =
        *an::CodecMetadata::create({}, {fits("EXPOSURE", "300.0"), fits("EXPTIME", "300.0"), fits("GAIN", "100"),
                                        card(an::CodecNamespace::Jpeg, "Exif.Photo.ExposureTime", "1/250"),
                                        card(an::CodecNamespace::CameraRaw, "Exif.Photo.ExposureTime", "30")});
    described = *an::withProcessingStateCards(
        described, an::ProcessingStateRecord{.linearity = an::RecordedLinearity::Linear,
                                             .channelBasis = an::RecordedChannelBasis::Monochrome});
    std::vector<an::CodecCard> held(described.cards().begin(), described.cards().end());
    // The cards are held in a local: the result that carries them would end with a range statement's first line.
    an::Result<std::vector<an::CodecCard>> stamps =
        an::artifactIdentityCards(*an::ArtifactIdentity::create("light-0001", "run-7"), an::CodecNamespace::Fits);
    ASSERT_TRUE(stamps.hasValue()) << stamps.status().message();
    for (an::CodecCard& stamp : *stamps)
        held.push_back(std::move(stamp));
    const an::ImageMetadata stated = source({.acquisition = {.exposureSeconds = 300.0}, .cards = std::move(held)});

    // The sources' exposure cards go; the combination states its own integration after the cards it keeps.
    const an::CodecMetadata combined = combine(stated, stated).codec();
    EXPECT_EQ(keywordsOf(combined), (std::vector<std::string>{"Fits:GAIN", "Fits:EXPTIME", "Fits:TOTEXP"}));
    EXPECT_EQ(combine(stated, stated).acquisition().exposureSeconds(), std::optional<double>(600.0));

    // A single source's exposure is the combination's, and its cards say so; the per-file cards still go.
    const std::array<const an::ImageMetadata*, 1u> alone{&stated};
    const an::Result<an::ImageMetadata> single = an::combinedImageMetadata(alone, kPreserving, preserved());
    ASSERT_TRUE(single.hasValue()) << single.status().message();
    EXPECT_EQ(keywordsOf(single->codec()),
              (std::vector<std::string>{"Fits:EXPOSURE", "Fits:EXPTIME", "Fits:GAIN", "Jpeg:Exif.Photo.ExposureTime",
                                        "CameraRaw:Exif.Photo.ExposureTime"}));
}

TEST(CombinedMetadataTest, ThePointingKeptIsEveryPairTheObservedPointingNames)
{
    // Two sources that point a little apart under every pair a frame's observed pointing is read from: the first
    // source's values are the combination's, and a value the sources differ in outside those pairs is not.
    std::vector<an::CodecCard> first;
    std::vector<an::CodecCard> second;
    for (const an::PointingCards pair : an::observedPointingCards())
    {
        const an::PointingKeywords keywords = an::pointingCardKeywords(pair);
        first.push_back(fits(std::string(keywords.rightAscension), "83.8"));
        first.push_back(fits(std::string(keywords.declination), "-5.4"));
        second.push_back(fits(std::string(keywords.rightAscension), "83.9"));
        second.push_back(fits(std::string(keywords.declination), "-5.5"));
    }
    first.push_back(fits("AIRMASS", "1.2"));
    second.push_back(fits("AIRMASS", "1.3"));
    const an::CodecMetadata kept = combine(source({.cards = first}), source({.cards = second})).codec();
    first.pop_back();
    EXPECT_EQ(std::vector<an::CodecCard>(kept.cards().begin(), kept.cards().end()), first);
}

TEST(CombinedMetadataTest, TheExposureCardsDroppedAreTheOnesTheProjectionReads)
{
    const std::array<an::CodecCard, 6u> spellings{
        fits("EXPOSURE", "2.5"),
        fits("EXPTIME", "2.5"),
        card(an::CodecNamespace::Jpeg, "Exif.Photo.ExposureTime", "5/2"),
        card(an::CodecNamespace::Tiff, "Exif.Photo.ExposureTime", "5/2"),
        card(an::CodecNamespace::Png, "Exif.Photo.ExposureTime", "5/2"),
        card(an::CodecNamespace::CameraRaw, "Exif.Photo.ExposureTime", "5/2")};
    for (const an::CodecCard& spelling : spellings)
    {
        const an::CodecMetadata alone = *an::CodecMetadata::create({}, {spelling});
        EXPECT_EQ(an::projectAcquisition(alone).acquisition.exposureSeconds, std::optional<double>(2.5))
            << spelling.keyword();
        const an::ImageMetadata stated = source({.acquisition = {.exposureSeconds = 2.5}, .cards = {spelling}});
        // The source's spelling goes, and the combination's own integration is all that states an exposure.
        const an::CodecMetadata combined = combine(stated, stated).codec();
        EXPECT_EQ(keywordsOf(combined), (std::vector<std::string>{"Fits:EXPTIME", "Fits:TOTEXP"}))
            << spelling.keyword();
        ASSERT_NE(combined.find(an::CodecNamespace::Fits, "EXPTIME"), nullptr);
        EXPECT_EQ(combined.find(an::CodecNamespace::Fits, "EXPTIME")->value(), "5") << spelling.keyword();
    }
}

namespace
{

/// A mono master of one filter as the stacking stage writes it, read back: the observing context its first light
/// carried, where the mount pointed and when that light began, its sub-exposure as the acquisition record and its
/// integration and frame count as cards, and the run's world coordinates when it was solved.
[[nodiscard]] an::ImageMetadata masterOf(std::string filter, std::string rightAscension, std::string dateObs,
                                         std::int64_t startNanoseconds, double integration, int frames,
                                         std::string airmass, bool solved)
{
    std::vector<an::CodecCard> cards;
    if (solved)
    {
        cards = wcsCardsAt("161.12779589800232");
    }
    const auto add = [&](std::string keyword, std::string value) { cards.push_back(fits(keyword, std::move(value))); };
    add("EXPTIME", std::to_string(static_cast<int>(integration)));
    add("TOTEXP", std::to_string(static_cast<int>(integration)));
    add("STACKCNT", std::to_string(frames));
    add("TELESCOP", "'Duoptic APO 90'");
    add("FOCALLEN", "432.0");
    add("XPIXSZ", "2.315");
    add("RA", std::move(rightAscension));
    add("DEC", "-59.8894394779668");
    add("OBJCTRA", "'10 44 19'");
    add("OBJCTDEC", "'-59 53 21'");
    add("AIRMASS", std::move(airmass));
    add("DATE-OBS", "'" + dateObs + "'");
    add("DATE-LOC", "'" + dateObs.replace(11u, 2u, "21") + "'");
    add("FILTER", "'" + filter + "'");
    add("OBJECT", "'Carina Nebula'");
    return source(
        {.acquisition = {.objectName = "Carina Nebula",
                         .filterName = std::move(filter),
                         .exposureSeconds = 120.0,
                         .exposureStart = an::UtcInstant{startNanoseconds}},
         .solution = solved ? std::optional<an::AstrometricSolutionRequest>(solutionAt(161.1278)) : std::nullopt,
         .cards = std::move(cards)});
}

/// The value of the first card of a keyword, or nothing.
[[nodiscard]] std::optional<std::string> valueOf(const an::CodecMetadata& codec, std::string_view keyword)
{
    const an::CodecCard* found = codec.find(an::CodecNamespace::Fits, std::string(keyword));
    return found == nullptr ? std::nullopt : std::optional<std::string>(found->value());
}

} // namespace

TEST(CombinedMetadataTest, AColourCompositionOfThreeMastersKeepsWhatStillDescribesIt)
{
    // Red, green and blue masters of one target and one telescope, taken over one night: each began at its own time,
    // the mount pointed a little differently at each, and each integrates its own frames. The green began first.
    const an::ImageMetadata red =
        masterOf("Red", "161.073958281148", "2026-03-24T03:07:03.043", 1774321623043000000, 25920.0, 216, "1.21", true);
    const an::ImageMetadata green = masterOf("Green", "161.075815757531", "2026-03-24T00:49:11.056",
                                             1774313351056000000, 18840.0, 157, "1.11", true);
    const an::ImageMetadata blue = masterOf("Blue", "161.175056262942", "2026-03-24T04:36:55.101", 1774327015101000000,
                                            13800.0, 115, "1.19", false);
    const std::array<const an::ImageMetadata*, 3u> sources{&red, &green, &blue};
    const an::Result<an::ImageMetadata> combined = an::combinedImageMetadata(sources, kPreserving, preserved());
    ASSERT_TRUE(combined.hasValue()) << combined.status().message();
    const an::CodecMetadata& cards = combined->codec();

    // The equipment and the target every master states the same way.
    EXPECT_EQ(valueOf(cards, "TELESCOP"), std::optional<std::string>("'Duoptic APO 90'"));
    EXPECT_EQ(valueOf(cards, "FOCALLEN"), std::optional<std::string>("432.0"));
    EXPECT_EQ(valueOf(cards, "XPIXSZ"), std::optional<std::string>("2.315"));
    EXPECT_EQ(valueOf(cards, "OBJECT"), std::optional<std::string>("'Carina Nebula'"));
    // The red master's world coordinates and pointing: the composite is on its grid.
    EXPECT_EQ(an::checkWcsCards(cards.cards()), an::WcsCardError::None);
    EXPECT_EQ(valueOf(cards, "CRVAL1"), std::optional<std::string>("161.12779589800232"));
    EXPECT_EQ(valueOf(cards, "RA"), std::optional<std::string>("161.073958281148"));
    EXPECT_EQ(valueOf(cards, "DEC"), std::optional<std::string>("-59.8894394779668"));
    EXPECT_EQ(valueOf(cards, "OBJCTRA"), std::optional<std::string>("'10 44 19'"));
    // The night began with the green master's first light.
    EXPECT_EQ(valueOf(cards, "DATE-OBS"), std::optional<std::string>("'2026-03-24T00:49:11.056'"));
    EXPECT_EQ(valueOf(cards, "DATE-LOC"), std::optional<std::string>("'2026-03-24T21:49:11.056'"));
    EXPECT_EQ(combined->acquisition().exposureStart(),
              (std::optional<an::UtcInstant>{an::UtcInstant{1774313351056000000}}));
    // All the light: 58560 seconds over 488 frames, not three sub-exposures.
    EXPECT_EQ(valueOf(cards, "EXPTIME"), std::optional<std::string>("58560"));
    EXPECT_EQ(valueOf(cards, "TOTEXP"), std::optional<std::string>("58560"));
    EXPECT_EQ(valueOf(cards, "STACKCNT"), std::optional<std::string>("488"));
    EXPECT_EQ(combined->acquisition().exposureSeconds(), std::optional<double>(58560.0));
    // What described one master's light alone no longer describes the composite.
    EXPECT_EQ(valueOf(cards, "FILTER"), std::nullopt);
    EXPECT_EQ(valueOf(cards, "AIRMASS"), std::nullopt);
    EXPECT_EQ(combined->acquisition().filterName(), "");
    std::size_t dates = 0u;
    for (const an::CodecCard& held : cards.cards())
    {
        dates += held.keyword() == "DATE-OBS" ? 1u : 0u;
    }
    EXPECT_EQ(dates, 1u);
}

TEST(CombinedMetadataTest, ACopyOfOneAcquisitionCountsOnce)
{
    // A master combined with itself holds the light of one acquisition.
    const an::ImageMetadata master =
        masterOf("Red", "161.073958281148", "2026-03-24T03:07:03.043", 1774321623043000000, 25920.0, 216, "1.21", true);
    const an::CodecMetadata cards = combine(master, master).codec();
    EXPECT_EQ(valueOf(cards, "EXPTIME"), std::optional<std::string>("25920"));
    EXPECT_EQ(valueOf(cards, "STACKCNT"), std::optional<std::string>("216"));
    EXPECT_EQ(combine(master, master).acquisition().exposureSeconds(), std::optional<double>(25920.0));
    // Every card the two state the same way stays, the filter and the airmass with them.
    EXPECT_EQ(valueOf(cards, "FILTER"), std::optional<std::string>("'Red'"));
    EXPECT_EQ(valueOf(cards, "DATE-OBS"), std::optional<std::string>("'2026-03-24T03:07:03.043'"));

    // Its starless image keeps the acquisition record - the sub-exposure and when the light began - but none of the
    // master's cards: it is the same acquisition, and the master's integration and frames are the light both hold.
    const an::ImageMetadata starless =
        source({.acquisition = {.exposureSeconds = 120.0, .exposureStart = an::UtcInstant{1774321623043000000}}});
    for (const auto& [first, second] : {std::pair{&master, &starless}, std::pair{&starless, &master}})
    {
        const an::ImageMetadata blended = combine(*first, *second);
        EXPECT_EQ(valueOf(blended.codec(), "EXPTIME"), std::optional<std::string>("25920"));
        EXPECT_EQ(valueOf(blended.codec(), "STACKCNT"), std::optional<std::string>("216"));
        EXPECT_EQ(blended.acquisition().exposureSeconds(), std::optional<double>(25920.0));
    }
}

TEST(CombinedMetadataTest, DropsEveryCardWhenTheEffectDropsThem)
{
    const an::ImageMetadata stated =
        source({.solution = solutionAt(83.8), .encoding = {.scale = 2.0}, .cards = wcsCardsAt("83.8")});
    const an::OperationEffect dropping{.codecMetadata = an::CodecMetadataEffect::Drops,
                                       .validity = {.policy = an::ValidityPolicy::Combine}};
    const an::ImageMetadata combined = combine(stated, stated, dropping);
    EXPECT_EQ(combined.codec(), an::CodecMetadata{});
    EXPECT_TRUE(combined.astrometry().present());
}

TEST(CombinedMetadataTest, KeepsTheSourceEncodingAndTheOutputIntentOnlyWhenCommon)
{
    const an::OutputIntentRequest sixteenBit{.sampleType = an::SampleType::UInt16,
                                             .scaling = an::OutputScaling::NormalizeFullRange,
                                             .cards = {fits("ORIGIN", "'AfterNight'")}};
    const an::ImageMetadata first =
        source({.encoding = {.sampleFormat = an::RawSampleFormat{2u, an::RawSampleClass::UnsignedInteger},
                             .zeroOffset = 32768.0},
                .intent = sixteenBit});
    const an::ImageMetadata same =
        source({.encoding = {.sampleFormat = an::RawSampleFormat{2u, an::RawSampleClass::UnsignedInteger},
                             .zeroOffset = 32768.0},
                .intent = sixteenBit});
    const an::ImageMetadata other =
        source({.encoding = {.sampleFormat = an::RawSampleFormat{4u, an::RawSampleClass::FloatingPoint}},
                .intent = an::OutputIntentRequest{.sampleType = an::SampleType::Float32}});
    const an::ImageMetadata common = combine(first, same);
    EXPECT_EQ(common.codec().sourceEncoding(), first.codec().sourceEncoding());
    EXPECT_EQ(common.outputIntent(), first.outputIntent());

    const an::ImageMetadata differing = combine(first, other);
    EXPECT_EQ(differing.codec().sourceEncoding(), an::SourceEncoding{});
    EXPECT_EQ(differing.outputIntent(), std::nullopt);

    const an::OperationEffect withdrawing{.outputIntent = an::OutputIntentEffect::Withdraws,
                                          .validity = {.policy = an::ValidityPolicy::Combine}};
    EXPECT_EQ(combine(first, same, withdrawing).outputIntent(), std::nullopt);
}

TEST(CombinedMetadataTest, RefusesWhatItCannotCombine)
{
    const an::ImageMetadata stated = source({});
    const auto refusal = [](std::span<const an::ImageMetadata* const> sources, const an::OperationEffect& effect,
                            const an::OperationOutcome& outcome) {
        const an::Result<an::ImageMetadata> combined = an::combinedImageMetadata(sources, effect, outcome);
        return combined.hasValue() ? an::ErrorCode::Ok : combined.status().code();
    };
    EXPECT_EQ(refusal({}, kPreserving, preserved()), an::ErrorCode::InvalidArgument);
    const std::array<const an::ImageMetadata*, 2u> withNull{&stated, nullptr};
    EXPECT_EQ(refusal(withNull, kPreserving, preserved()), an::ErrorCode::InvalidArgument);
    const std::array<const an::ImageMetadata*, 2u> pair{&stated, &stated};
    const an::OperationEffect reading{.samples = an::SampleEffect::Reads, .semantics = an::SemanticEffect::Restates};
    EXPECT_EQ(refusal(pair, reading, preserved()), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(refusal(pair, kPreserving, an::OperationOutcome{.operation = ""}), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(refusal(pair, kPreserving,
                      an::OperationOutcome{.operation = "combineImages", .semantics = an::ImageSemantics{}}),
              an::ErrorCode::InvalidArgument);

    // A sum of stated exposures the acquisition record refuses is refused here too.
    const an::ImageMetadata huge = source({.acquisition = {.exposureSeconds = 1.0e308}});
    const std::array<const an::ImageMetadata*, 2u> overflowing{&huge, &huge};
    EXPECT_EQ(refusal(overflowing, kPreserving, preserved()), an::ErrorCode::InvalidArgument);
}
