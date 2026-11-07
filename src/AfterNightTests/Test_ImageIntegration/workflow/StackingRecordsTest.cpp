// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the lights stacking stage decides before it reads a pixel: the method a group's stored texts name, read
/// through the closed maps and refused by name when they name none, and every stored setting the stack cannot honour
/// said; the per-pixel weighting it runs and how many frames a light combines; how an exposure merge groups its frames
/// and blends them; which route a group's lights take; the master's cards; and a mosaic's pointing, read from the
/// lights' own pointing cards through the engine's angle reader; and what the frame weighting reads of a light.

#include "processes/ImageIntegration/core/StackWeightingInputs.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/stages/StackMasterWcs.h"
#include "processes/ImageIntegration/workflow/stages/TransformOnReadRouting.h"

#include <AfterNightEngine/Image/CodecMetadata.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{

[[nodiscard]] an::CodecCard fitsCard(const std::string& keyword, const std::string& value)
{
    an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, keyword, value, {});
    EXPECT_TRUE(card.hasValue());
    return *card;
}

[[nodiscard]] std::optional<std::string> valueOf(const std::vector<an::CodecCard>& cards, std::string_view keyword)
{
    for (const an::CodecCard& card : cards)
    {
        if (card.keyword() == keyword)
        {
            return std::string(card.value());
        }
    }
    return std::nullopt;
}

TEST(StackingChoiceTest, TheStoredTextsAreReadThroughTheClosedMaps)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Median");
    settings.rejectionMethod = QStringLiteral("None");
    settings.weighting = QStringLiteral("Number of Stars");
    const auto choice = ImageIntegration::integrationStackChoice(settings);
    ASSERT_TRUE(choice.hasValue());
    EXPECT_EQ(choice->combination, an::StackCombination::Median);
    EXPECT_EQ(choice->rejection, an::StackRejection::None);
    EXPECT_EQ(choice->weighting, an::StackWeighting::StarCount);
    EXPECT_FALSE(ImageIntegration::combinationTakesWeights(choice->combination));
}

TEST(StackingChoiceTest, AnUnknownTextIsRefusedByName)
{
    for (const auto& edit : std::vector<std::function<void(StackingSettings&)>>{
             [](StackingSettings& settings) { settings.stackMethod = QStringLiteral("Fancy Mean"); },
             [](StackingSettings& settings) { settings.rejectionMethod = QStringLiteral("Fancy Clip"); },
             [](StackingSettings& settings) { settings.weighting = QStringLiteral("Fancy Weight"); },
             [](StackingSettings& settings) { settings.starCountScaling = QStringLiteral("Fancy Scale"); }})
    {
        StackingSettings settings;
        edit(settings);
        const auto choice = ImageIntegration::integrationStackChoice(settings);
        ASSERT_FALSE(choice.hasValue());
        EXPECT_NE(std::string(choice.status().message()).find("Fancy"), std::string::npos) << choice.status().message();
        EXPECT_TRUE(ImageIntegration::stackingSettingsNotes(settings).isEmpty());
    }
}

TEST(StackingChoiceTest, EverySettingTheStackCannotHonourIsSaid)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Median");
    settings.weighting = QStringLiteral("Noise");
    const QStringList notes = ImageIntegration::stackingSettingsNotes(settings);
    EXPECT_TRUE(notes.join(QLatin1Char('\n')).contains(QStringLiteral("takes no frame weights")))
        << notes.join(QLatin1Char('\n')).toStdString();
    EXPECT_TRUE(notes.join(QLatin1Char('\n')).contains(QStringLiteral("rejects nothing")))
        << notes.join(QLatin1Char('\n')).toStdString();

    StackingSettings unrejected;
    unrejected.rejectionMethod = QStringLiteral("None");
    unrejected.createRejectionMaps = true;
    EXPECT_TRUE(ImageIntegration::stackingSettingsNotes(unrejected)
                    .join(QLatin1Char('\n'))
                    .contains(QStringLiteral("no rejection map")));

    StackingSettings plain;
    EXPECT_TRUE(ImageIntegration::stackingSettingsNotes(plain).isEmpty());
}

// A per-pixel weighting stored with values the engine refuses - a grid finer than its finest, an even or too small
// window, an exponent that is not positive - runs at the nearest values it takes, and the notes say which. The grid was
// raised to one, which the engine refuses, and nothing was said.
TEST(StackingChoiceTest, APerPixelWeightingRunsAtTheNearestValuesTheEngineTakesAndSaysSo)
{
    StackingSettings settings;
    settings.weighting = QStringLiteral("Noise");
    settings.localWeighting.enabled = true;
    settings.localWeighting.windowSize = 4;
    settings.localWeighting.gridSpacing = 2;
    settings.localWeighting.snrWeightExponent = 0.0;
    const an::LocalWeightingRequest request = ImageIntegration::localWeightingRequestFor(settings.localWeighting);
    EXPECT_TRUE(request.enabled);
    EXPECT_EQ(request.windowSize, 5u);
    EXPECT_EQ(request.gridSpacing, an::kMinimumLocalWeightGridSpacing);
    EXPECT_DOUBLE_EQ(request.snrExponent, ImageIntegration::kMinimumLocalWeightingSnrExponent);
    const QString notes = ImageIntegration::stackingSettingsNotes(settings).join(QLatin1Char('\n'));
    EXPECT_TRUE(notes.contains(QStringLiteral("window 4 as 5"))) << notes.toStdString();
    EXPECT_TRUE(notes.contains(QStringLiteral("grid spacing 2 as 4"))) << notes.toStdString();
    EXPECT_TRUE(notes.contains(QStringLiteral("exponent 0.00 as 0.10"))) << notes.toStdString();

    settings.localWeighting.snrWeightExponent = std::numeric_limits<double>::quiet_NaN();
    EXPECT_DOUBLE_EQ(ImageIntegration::localWeightingRequestFor(settings.localWeighting).snrExponent,
                     an::kDefaultLocalWeightSnrExponent);

    // Values the dialog offers run as stored, and a per-pixel weighting the stack does not apply is not adjusted.
    StackingSettings offered;
    offered.weighting = QStringLiteral("Noise");
    offered.localWeighting.enabled = true;
    const an::LocalWeightingRequest asStored = ImageIntegration::localWeightingRequestFor(offered.localWeighting);
    EXPECT_EQ(asStored.windowSize, 5u);
    EXPECT_EQ(asStored.gridSpacing, 64u);
    EXPECT_DOUBLE_EQ(asStored.snrExponent, 2.0);
    EXPECT_TRUE(ImageIntegration::stackingSettingsNotes(offered).isEmpty());
    settings.weighting = QStringLiteral("None");
    EXPECT_FALSE(ImageIntegration::stackingSettingsNotes(settings)
                     .join(QLatin1Char('\n'))
                     .contains(QStringLiteral("grid spacing")));
}

// How many frames a light combines is read from STACKCNT, which a master the stack writes carries, then from the
// NCOMBINE another program writes; a light that states neither, or no count of at least one, is one frame.
TEST(StackingChoiceTest, ACombinedFrameCountIsReadFromStackcntThenNcombine)
{
    EXPECT_EQ(ImageIntegration::combinedFrameCount({}), 1u);
    EXPECT_EQ(ImageIntegration::combinedFrameCount(std::vector<an::CodecCard>{fitsCard("NCOMBINE", "12")}), 12u);
    EXPECT_EQ(ImageIntegration::combinedFrameCount(std::vector<an::CodecCard>{fitsCard("STACKCNT", "5")}), 5u);
    EXPECT_EQ(ImageIntegration::combinedFrameCount(
                  std::vector<an::CodecCard>{fitsCard("NCOMBINE", "7"), fitsCard("STACKCNT", "5")}),
              5u);
    for (const char* unusable : {"0", "-3", "'many'", "nan"})
    {
        EXPECT_EQ(ImageIntegration::combinedFrameCount(std::vector<an::CodecCard>{fitsCard("STACKCNT", unusable)}), 1u)
            << unusable;
    }
    EXPECT_EQ(ImageIntegration::combinedFrameCount(
                  std::vector<an::CodecCard>{fitsCard("STACKCNT", "0"), fitsCard("NCOMBINE", "9")}),
              9u)
        << "an unusable STACKCNT gives way to NCOMBINE";
}

// The registration records an eccentricity of one where it measured no roundness, and the engine refuses a roundness
// of zero in any light's record, whatever the weighting reads; so a light whose stars have no measured roundness states
// none, rather than one that refuses the group's weights. It stated zero, and every weighted stack of its group failed.
TEST(StackWeightingInputsTest, ALightWhoseStarsHaveNoMeasuredRoundnessStatesNone)
{
    FrameQualityMetrics metrics;
    metrics.stars = 120;
    metrics.fwhm = 2.5;
    metrics.snr = 30.0;
    metrics.eccentricity = 0.25;
    const an::StackFrameMeasurement round = ImageIntegration::stackFrameMeasurementFor(metrics, {});
    EXPECT_EQ(round.starCount, std::optional<std::uint32_t>(120u));
    ASSERT_TRUE(round.medianRoundness.has_value());
    EXPECT_DOUBLE_EQ(*round.medianRoundness, 0.75);
    ASSERT_TRUE(round.fullWidth.has_value());
    EXPECT_DOUBLE_EQ(*round.fullWidth, 2.5);
    ASSERT_TRUE(round.signalToNoise.has_value());
    EXPECT_DOUBLE_EQ(*round.signalToNoise, 30.0);

    for (const double unmeasured : {1.0, std::numeric_limits<double>::quiet_NaN()})
    {
        metrics.eccentricity = unmeasured;
        const an::StackFrameMeasurement measurement = ImageIntegration::stackFrameMeasurementFor(metrics, {});
        EXPECT_FALSE(measurement.medianRoundness.has_value()) << unmeasured;
        EXPECT_EQ(measurement.starCount, std::optional<std::uint32_t>(120u)) << unmeasured;
    }

    // A light the registration found no stars in states no count, no roundness and no width.
    const an::StackFrameMeasurement empty = ImageIntegration::stackFrameMeasurementFor(FrameQualityMetrics{}, {});
    EXPECT_FALSE(empty.starCount.has_value());
    EXPECT_FALSE(empty.medianRoundness.has_value());
    EXPECT_FALSE(empty.fullWidth.has_value());
    EXPECT_FALSE(ImageIntegration::carriesWeightingFigure(an::StackWeighting::StarCount, empty));
    EXPECT_TRUE(ImageIntegration::carriesWeightingFigure(an::StackWeighting::StarCount, round));
    EXPECT_TRUE(ImageIntegration::carriesWeightingFigure(an::StackWeighting::Fwhm, round));
    EXPECT_FALSE(ImageIntegration::carriesWeightingFigure(an::StackWeighting::Noise, round));
}

// A light's background noise is stated with the scale that restates it: the IKSS scale, else the MAD; a value the
// engine refuses is not stated.
TEST(StackWeightingInputsTest, ALightsNoiseIsStatedWithTheScaleThatRestatesIt)
{
    FrameNoiseStatistics noise;
    noise.background_noise = 0.02;
    noise.global_mad = 0.4;
    noise.global_scale = 0.5;
    const an::StackFrameMeasurement scaled = ImageIntegration::stackFrameMeasurementFor({}, noise);
    ASSERT_TRUE(scaled.backgroundNoise.has_value());
    EXPECT_DOUBLE_EQ(*scaled.backgroundNoise, 0.02);
    ASSERT_TRUE(scaled.dispersion.has_value());
    EXPECT_DOUBLE_EQ(*scaled.dispersion, 0.5);
    EXPECT_TRUE(ImageIntegration::carriesWeightingFigure(an::StackWeighting::Noise, scaled));

    noise.global_scale = 0.0;
    const an::StackFrameMeasurement mad = ImageIntegration::stackFrameMeasurementFor({}, noise);
    ASSERT_TRUE(mad.dispersion.has_value());
    EXPECT_DOUBLE_EQ(*mad.dispersion, 0.4);

    noise.background_noise = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(ImageIntegration::stackFrameMeasurementFor({}, noise).backgroundNoise.has_value());
}

// The group's weighting is the request its stack makes: the floor held to the unit interval and stated as the
// photometric model's raw floor too, the scaling and its steepness - the engine's default for one that is not positive
// - and per-channel widths only beside the width weighting.
TEST(StackWeightingInputsTest, TheGroupsWeightingIsTheRequestItsStackMakes)
{
    StackingSettings settings;
    settings.minWeightFloor = 1.5;
    settings.exponentialAlpha = -1.0;
    settings.perChannelFwhmWeighting = true;
    const an::StackWeightingRequest width =
        ImageIntegration::stackWeightingRequestFor(settings, an::StackWeighting::Fwhm, an::StarCountScaling::Linear);
    EXPECT_EQ(width.method, an::StackWeighting::Fwhm);
    EXPECT_DOUBLE_EQ(width.minimumWeight, 1.0);
    EXPECT_DOUBLE_EQ(width.photometric.minimumRawWeight, 1.0);
    EXPECT_DOUBLE_EQ(width.starCount.exponentialAlpha, an::kDefaultStarCountExponentialAlpha);
    EXPECT_TRUE(width.perChannelFwhm);

    settings.minWeightFloor = 0.05;
    settings.exponentialAlpha = 3.5;
    const an::StackWeightingRequest stars = ImageIntegration::stackWeightingRequestFor(
        settings, an::StackWeighting::StarCount, an::StarCountScaling::Exponential);
    EXPECT_DOUBLE_EQ(stars.minimumWeight, 0.05);
    EXPECT_EQ(stars.starCount.scaling, an::StarCountScaling::Exponential);
    EXPECT_DOUBLE_EQ(stars.starCount.exponentialAlpha, 3.5);
    EXPECT_FALSE(stars.perChannelFwhm);
}

// A group's reference is found among the frames a stack weighs by identity, or is the first of them by identity - the
// light the stacking stage weighs against when the manifest names none it stacks.
TEST(StackWeightingInputsTest, TheReferenceIsFoundByIdentityElseItIsTheFirstFrameByIdentity)
{
    const std::vector<FrameId> frames{FrameId(QStringLiteral("C:/lights/c.fit")),
                                      FrameId(QStringLiteral("C:/lights/a.fit")),
                                      FrameId(QStringLiteral("C:/lights/b.fit"))};
    EXPECT_EQ(ImageIntegration::stackWeightingReferenceIndex(frames, FrameId(QStringLiteral("C:/lights/b.fit"))), 2u);
    EXPECT_EQ(ImageIntegration::stackWeightingReferenceIndex(frames, std::nullopt), 1u);
    EXPECT_EQ(ImageIntegration::stackWeightingReferenceIndex(frames, FrameId(QStringLiteral("C:/lights/gone.fit"))),
              1u);
    EXPECT_EQ(ImageIntegration::stackWeightingReferenceIndex({}, std::nullopt), 0u);
}

TEST(ExposureMergeTest, FramesAreGroupedByExposureLongestFirst)
{
    const auto buckets = ImageIntegration::exposureMergeBuckets({60.0, 15.0, 60.5, 5.0, 15.2});
    ASSERT_TRUE(buckets.hasValue());
    ASSERT_EQ(buckets->size(), 3u);
    EXPECT_EQ((*buckets)[0].frames, (std::vector<std::uint32_t>{0u, 2u}));
    EXPECT_EQ((*buckets)[1].frames, (std::vector<std::uint32_t>{1u, 4u}));
    EXPECT_EQ((*buckets)[2].frames, (std::vector<std::uint32_t>{3u}));
    EXPECT_NEAR((*buckets)[0].exposureSeconds, 60.25, 1e-12);
}

TEST(ExposureMergeTest, OneExposureOrAFrameWithoutOneIsRefused)
{
    EXPECT_FALSE(ImageIntegration::exposureMergeBuckets({60.0, 60.0, 61.0}).hasValue());
    EXPECT_FALSE(ImageIntegration::exposureMergeBuckets({60.0, 0.0}).hasValue());
}

TEST(ExposureMergeTest, AShorterExposureTakesOverOnlyPastTheHighlight)
{
    EXPECT_DOUBLE_EQ(ImageIntegration::exposureMergeBlendWeight(0.5), 0.0);
    EXPECT_DOUBLE_EQ(ImageIntegration::exposureMergeBlendWeight(ImageIntegration::kExposureMergeHighlightLow), 0.0);
    EXPECT_DOUBLE_EQ(ImageIntegration::exposureMergeBlendWeight(ImageIntegration::kExposureMergeHighlightHigh), 1.0);
    EXPECT_DOUBLE_EQ(ImageIntegration::exposureMergeBlendWeight(1.5), 1.0);
    const double middle = ImageIntegration::exposureMergeBlendWeight(
        0.5 * (ImageIntegration::kExposureMergeHighlightLow + ImageIntegration::kExposureMergeHighlightHigh));
    EXPECT_NEAR(middle, 0.5, 1e-12);
}

class TransformOnReadDecisionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_registration.groupKey = GroupKey{QStringLiteral("reg|a")};
        m_group.registrationGroupKey = m_registration.groupKey;
        m_group.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;
        m_manifest.registrationGroups = {m_registration};
    }

    [[nodiscard]] QString reason() const
    {
        return ImageIntegration::decideLightsTransformOnRead(m_manifest, m_group).reason;
    }

    WorkflowManifest m_manifest;
    WorkflowManifest::RegistrationGroup m_registration;
    WorkflowManifest::IntegrationGroup m_group;
};

TEST_F(TransformOnReadDecisionTest, ADiscardableRegisteredGroupIsWarpedAsItIsRead)
{
    const auto routing = ImageIntegration::decideLightsTransformOnRead(m_manifest, m_group);
    EXPECT_TRUE(routing.usesTransformOnRead());

    // An incremental run reads the lights as a full run does, so a run that only stacks again writes no registered
    // image and measures nothing again.
    m_manifest.executionMode = ExecutionMode::Incremental;
    EXPECT_TRUE(ImageIntegration::decideLightsTransformOnRead(m_manifest, m_group).usesTransformOnRead());
}

TEST_F(TransformOnReadDecisionTest, EachWorkflowReasonKeepsTheRegisteredImages)
{
    m_manifest.registrationGroups.clear();
    EXPECT_EQ(reason(), QStringLiteral("registration_data_missing"));
    m_manifest.registrationGroups = {m_registration};

    WorkflowManifest::IntegrationGroup::DrizzleSettings drizzle;
    drizzle.enabled = true;
    m_group.drizzle = drizzle;
    EXPECT_EQ(reason(), QStringLiteral("drizzle"));
    m_group.drizzle.reset();

    m_group.stacking.cometStackingMode = QStringLiteral("Comet");
    EXPECT_EQ(reason(), QStringLiteral("comet_product"));
    m_group.stacking.cometStackingMode = QStringLiteral("Stars");

    m_manifest.reprocessExisting = true;
    EXPECT_EQ(reason(), QStringLiteral("resume_checkpoint_recovery"));
    m_manifest.reprocessExisting = false;

    m_group.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Retained;
    EXPECT_EQ(reason(), QStringLiteral("registered_output_retention"));
}

TEST(MasterCardsTest, TheMasterStatesItsTotalExposureAndItsDrizzle)
{
    const an::Result<an::CodecMetadata> reference =
        an::CodecMetadata::create(an::SourceEncoding{}, {fitsCard("EXPTIME", "60"), fitsCard("OBJECT", "'M42'")});
    ASSERT_TRUE(reference.hasValue());
    ImageIntegration::detail::MasterCardsRequest request;
    request.referenceCards = &*reference;
    request.integrationSeconds = 180.0;
    WorkflowManifest::IntegrationGroup::DrizzleSettings drizzle;
    drizzle.enabled = true;
    drizzle.scale = 2.0f;
    drizzle.pixelFraction = 0.8f;
    request.drizzle = drizzle;
    request.drizzledLights = 3u;
    const an::Result<an::CodecMetadata> cards = ImageIntegration::detail::masterCodecMetadata(request);
    ASSERT_TRUE(cards.hasValue());
    const std::vector<an::CodecCard> list(cards->cards().begin(), cards->cards().end());
    ASSERT_TRUE(valueOf(list, "EXPTIME").has_value());
    EXPECT_DOUBLE_EQ(std::stod(*valueOf(list, "EXPTIME")), 180.0);
    EXPECT_DOUBLE_EQ(std::stod(*valueOf(list, "TOTEXP")), 180.0);
    EXPECT_EQ(valueOf(list, "DRIZZLE"), std::optional<std::string>("T"));
    EXPECT_EQ(valueOf(list, "NDRIZIM"), std::optional<std::string>("3"));
    EXPECT_DOUBLE_EQ(std::stod(*valueOf(list, "DRIZMULT")), 2.0);
    EXPECT_TRUE(valueOf(list, "OBJECT").has_value());
}

// A master states how many frames it combines in STACKCNT, so a later stack weighted
// by the number of images reads it; one that states no count carries no card. No master stated a count.
TEST(MasterCardsTest, TheMasterStatesHowManyFramesItCombines)
{
    const an::Result<an::CodecMetadata> reference =
        an::CodecMetadata::create(an::SourceEncoding{}, {fitsCard("STACKCNT", "4")});
    ASSERT_TRUE(reference.hasValue());
    ImageIntegration::detail::MasterCardsRequest request;
    request.referenceCards = &*reference;
    request.combinedFrames = 70u;
    const an::Result<an::CodecMetadata> cards = ImageIntegration::detail::masterCodecMetadata(request);
    ASSERT_TRUE(cards.hasValue());
    const std::vector<an::CodecCard> list(cards->cards().begin(), cards->cards().end());
    EXPECT_EQ(valueOf(list, "STACKCNT"), std::optional<std::string>("70"))
        << "the master's own count, not its reference light's";

    request.combinedFrames.reset();
    const an::Result<an::CodecMetadata> uncounted = ImageIntegration::detail::masterCodecMetadata(request);
    ASSERT_TRUE(uncounted.hasValue());
    const std::vector<an::CodecCard> none(uncounted->cards().begin(), uncounted->cards().end());
    EXPECT_FALSE(valueOf(none, "STACKCNT").has_value());
}

TEST(MosaicPointingTest, TheCentreIsTheSphericalMeanOfTheLightsPointings)
{
    const std::vector<an::CodecCard> reference{fitsCard("RA", "10.0"), fitsCard("DEC", "20.0"),
                                               fitsCard("OBJCTROT", "12.5")};
    const std::vector<std::vector<an::CodecCard>> lights{{fitsCard("RA", "10.0"), fitsCard("DEC", "20.0")},
                                                         {fitsCard("RA", "12.0"), fitsCard("DEC", "20.0")},
                                                         {fitsCard("OBJECT", "'nothing here'")}};
    const std::vector<an::CodecCard> cards =
        ImageIntegration::detail::mosaicPointingCards(reference, lights, std::nullopt);

    ASSERT_TRUE(valueOf(cards, "RA").has_value());
    EXPECT_NEAR(std::stod(*valueOf(cards, "RA")), 11.0, 1e-3);
    EXPECT_NEAR(std::stod(*valueOf(cards, "DEC")), 20.0, 0.01);
    ASSERT_TRUE(valueOf(cards, "ANPOSMTH").has_value());
    EXPECT_NE(valueOf(cards, "ANPOSMTH")->find("SPHAVG"), std::string::npos);
    EXPECT_EQ(valueOf(cards, "ANPOSCNT"), std::optional<std::string>("2"));
    EXPECT_EQ(valueOf(cards, "ANREFRA"), std::optional<std::string>("10.0"));
    EXPECT_EQ(valueOf(cards, "ANREFROT"), std::optional<std::string>("12.5"));
    ASSERT_TRUE(valueOf(cards, "OBJCTRA").has_value());
    EXPECT_NE(valueOf(cards, "OBJCTRA")->find("00 44"), std::string::npos) << *valueOf(cards, "OBJCTRA");
}

TEST(MosaicPointingTest, APointingWhoseFractionAnotherFieldFollowsIsNotRead)
{
    // `00 44.5 30` puts a fraction in the minutes and seconds after it: no angle, so that light adds no direction,
    // where a reader that added the fraction would have moved the centre half a minute of time.
    const std::vector<std::vector<an::CodecCard>> lights{
        {fitsCard("RA", "10.0"), fitsCard("DEC", "20.0")},
        {fitsCard("RA", "12.0"), fitsCard("DEC", "20.0")},
        {fitsCard("OBJCTRA", "'00 44.5 30'"), fitsCard("OBJCTDEC", "'+20 00 00'")}};
    const std::vector<an::CodecCard> cards = ImageIntegration::detail::mosaicPointingCards({}, lights, std::nullopt);
    EXPECT_EQ(valueOf(cards, "ANPOSCNT"), std::optional<std::string>("2"));
    ASSERT_TRUE(valueOf(cards, "RA").has_value());
    EXPECT_NEAR(std::stod(*valueOf(cards, "RA")), 11.0, 1e-3);
}

TEST(MosaicPointingTest, LightsThatStateNoPointingGiveNoCentre)
{
    const std::vector<std::vector<an::CodecCard>> lights{{fitsCard("OBJECT", "'a'")}, {fitsCard("OBJECT", "'b'")}};
    const std::vector<an::CodecCard> cards = ImageIntegration::detail::mosaicPointingCards({}, lights, std::nullopt);
    EXPECT_FALSE(valueOf(cards, "RA").has_value());
    EXPECT_FALSE(valueOf(cards, "ANPOSMTH").has_value());
}

} // namespace
