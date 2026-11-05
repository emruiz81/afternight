// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/ImageMetadata.h.
//
// The aggregate is the easy half; the transition is where the rules live, and these cases are
// organised around what the transition refuses.
//
// **There is no way to change the acquisition record.** That is checked by the shape of the type
// rather than by a case - `MetadataTransition` has no field for it - so what the cases check is the
// consequence: an acquisition record survives every transition unchanged.
//
// **A change carries a record.** An operation that changes what an image means and does not say
// what changed it leaves a lineage with a gap in it, which cannot answer the question a lineage
// exists for.
//
// **Only two of the six move the semantic generation.** A corrected object name must not discard a
// plate solution, and a plate solution must discard a star list; `changesMeaning` is the one place
// that distinction is made, so it is checked member by member.

#include <AfterNightEngine/Image/ImageMetadata.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace
{

/// A lineage entry that is valid.
///
/// @param operation what was done.
/// @return the entry.
[[nodiscard]] an::ProvenanceEntry makeEntry(const char* operation)
{
    an::Result<an::ProvenanceEntry> entry = an::ProvenanceEntry::create(operation, "");
    EXPECT_TRUE(entry.hasValue());
    return entry.hasValue() ? std::move(*entry) : an::ProvenanceEntry::create("fallback", "").value();
}

/// Acquisition facts a transition must never be able to change.
///
/// @return the record.
[[nodiscard]] an::AcquisitionMetadata acquiredFacts()
{
    an::AcquisitionMetadataRequest request;
    request.instrumentName = "ASI2600MM";
    request.filterName = "Ha";
    request.exposureSeconds = 300.0;

    an::Result<an::AcquisitionMetadata> record = an::AcquisitionMetadata::create(request);
    EXPECT_TRUE(record.hasValue());
    return record.hasValue() ? std::move(*record) : an::AcquisitionMetadata{};
}

/// A processing meaning that says the frame is linear and calibrated.
///
/// @return the meaning.
[[nodiscard]] an::ImageSemantics calibratedLinear()
{
    an::ImageSemanticsRequest request;
    request.transferFunction = an::TransferFunction::Linear;
    request.processingState.biasSubtracted = true;
    request.processingState.darkSubtracted = true;
    request.processingState.flatFielded = true;

    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(request);
    EXPECT_TRUE(semantics.hasValue());
    return semantics.hasValue() ? *semantics : an::ImageSemantics{};
}

/// A solved pointing.
///
/// @return the solution.
[[nodiscard]] an::AstrometricSolution solvedPointing()
{
    an::Result<an::AstrometricSolution> solution = an::AstrometricSolution::create(an::AstrometricSolutionRequest{
        .referencePoint = an::SkyCoordinate{.rightAscensionDegrees = 10.0, .declinationDegrees = 41.0},
        .referencePixelX = 100.0,
        .referencePixelY = 100.0,
        .pixelScaleArcsecPerPixel = 2.0,
        .orientationDegrees = 0.0,
        .mirrored = false,
        .hasDistortion = false});
    EXPECT_TRUE(solution.hasValue());
    return solution.hasValue() ? *solution : an::AstrometricSolution{};
}

/// An output decision.
///
/// @return the intent.
[[nodiscard]] an::OutputIntent sixteenBitIntent()
{
    an::Result<an::OutputIntent> intent =
        an::OutputIntent::create(an::OutputIntentRequest{.sampleType = an::SampleType::UInt16});
    EXPECT_TRUE(intent.hasValue());
    return intent.hasValue()
               ? std::move(*intent)
               : an::OutputIntent::create(an::OutputIntentRequest{.sampleType = an::SampleType::Float32}).value();
}

/// Metadata with every part stated except an output decision.
///
/// @return the aggregate.
[[nodiscard]] an::ImageMetadata decodedFrame()
{
    an::Result<an::Provenance> provenance = an::Provenance::create(an::ProvenanceSourceKind::File, "frame.fit");
    EXPECT_TRUE(provenance.hasValue());
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, {});
    EXPECT_TRUE(codec.hasValue());

    return an::ImageMetadata::create(acquiredFacts(), an::ImageSemantics{}, an::AstrometricSolution{},
                                     provenance.hasValue() ? std::move(*provenance) : an::Provenance{},
                                     codec.hasValue() ? std::move(*codec) : an::CodecMetadata{}, std::nullopt);
}

TEST(ImageMetadataTest, TheDefaultStatesNothingAndDecidesNothingAboutWriting)
{
    const an::ImageMetadata metadata;
    EXPECT_EQ(metadata.acquisition(), an::AcquisitionMetadata{});
    EXPECT_EQ(metadata.semantics(), an::ImageSemantics{});
    EXPECT_FALSE(metadata.astrometry().present());
    EXPECT_TRUE(metadata.provenance().entries().empty());
    EXPECT_TRUE(metadata.codec().cards().empty());
    EXPECT_FALSE(metadata.outputIntent().has_value());
}

TEST(ImageMetadataTest, TheSixPartsAreCarriedAndReadBackUnchanged)
{
    an::Result<an::Provenance> provenance = an::Provenance::create(an::ProvenanceSourceKind::File, "frame.fit");
    ASSERT_TRUE(provenance.hasValue());

    const an::ImageMetadata metadata =
        an::ImageMetadata::create(acquiredFacts(), calibratedLinear(), solvedPointing(), std::move(*provenance),
                                  an::CodecMetadata{}, sixteenBitIntent());

    EXPECT_EQ(metadata.acquisition().filterName(), "Ha");
    EXPECT_TRUE(metadata.semantics().isLinear());
    EXPECT_TRUE(metadata.astrometry().present());
    EXPECT_EQ(metadata.provenance().sourceName(), "frame.fit");
    EXPECT_TRUE(metadata.codec().cards().empty());
    ASSERT_TRUE(metadata.outputIntent().has_value());
    EXPECT_EQ(metadata.outputIntent()->sampleType(), an::SampleType::UInt16);
}

TEST(ImageMetadataTest, ATransitionThatStatesNothingChangesNothing)
{
    const an::ImageMetadata before = decodedFrame();
    an::Result<an::ImageMetadata> after = an::applyMetadataTransition(before, an::MetadataTransition{});
    ASSERT_TRUE(after.hasValue()) << after.status().message();
    EXPECT_EQ(*after, before);
}

TEST(ImageMetadataTest, ATransitionCarryingOnlyARecordExtendsTheLineageAndNothingElse)
{
    const an::ImageMetadata before = decodedFrame();

    an::MetadataTransition transition;
    transition.record = makeEntry("statistics");
    EXPECT_FALSE(an::changesAnything(transition));
    EXPECT_FALSE(an::changesMeaning(transition));

    an::Result<an::ImageMetadata> after = an::applyMetadataTransition(before, transition);
    ASSERT_TRUE(after.hasValue()) << after.status().message();

    ASSERT_EQ(after->provenance().entries().size(), before.provenance().entries().size() + 1u);
    EXPECT_EQ(after->provenance().entries().back().operation(), "statistics");
    EXPECT_EQ(after->semantics(), before.semantics());
    EXPECT_EQ(after->astrometry(), before.astrometry());
}

TEST(ImageMetadataTest, AChangeWithoutARecordLeavesAGapAndIsRefused)
{
    const an::ImageMetadata before = decodedFrame();

    an::MetadataTransition transition;
    transition.semantics = calibratedLinear();
    EXPECT_TRUE(an::changesAnything(transition));

    an::Result<an::ImageMetadata> refused = an::applyMetadataTransition(before, transition);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
    const an::StatusContextField* const field = refused.status().findContext(an::ContextKey::Parameter);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->text, "record");
}

TEST(ImageMetadataTest, TheAcquisitionRecordSurvivesEveryTransition)
{
    const an::ImageMetadata before = decodedFrame();

    an::MetadataTransition transition;
    transition.semantics = calibratedLinear();
    transition.astrometry = solvedPointing();
    transition.outputIntent = sixteenBitIntent();
    transition.clearCodecMetadata = true;
    transition.record = makeEntry("calibrate");

    an::Result<an::ImageMetadata> after = an::applyMetadataTransition(before, transition);
    ASSERT_TRUE(after.hasValue()) << after.status().message();

    EXPECT_EQ(after->acquisition(), before.acquisition());
    EXPECT_EQ(after->acquisition().filterName(), "Ha");
}

TEST(ImageMetadataTest, OnlyTheTwoSemanticObjectsChangeWhatAnImageMeans)
{
    an::MetadataTransition nothing;
    EXPECT_FALSE(an::changesMeaning(nothing));

    an::MetadataTransition meaning;
    meaning.semantics = calibratedLinear();
    EXPECT_TRUE(an::changesMeaning(meaning));

    an::MetadataTransition pointing;
    pointing.astrometry = solvedPointing();
    EXPECT_TRUE(an::changesMeaning(pointing));

    an::MetadataTransition writing;
    writing.outputIntent = sixteenBitIntent();
    EXPECT_FALSE(an::changesMeaning(writing));
    EXPECT_TRUE(an::changesAnything(writing));

    an::MetadataTransition cards;
    cards.clearCodecMetadata = true;
    EXPECT_FALSE(an::changesMeaning(cards));
    EXPECT_TRUE(an::changesAnything(cards));

    an::MetadataTransition withdrawn;
    withdrawn.clearOutputIntent = true;
    EXPECT_FALSE(an::changesMeaning(withdrawn));
    EXPECT_TRUE(an::changesAnything(withdrawn));
}

TEST(ImageMetadataTest, StatingAndWithdrawingAnOutputDecisionAtOnceIsRefused)
{
    const an::ImageMetadata before = decodedFrame();

    an::MetadataTransition transition;
    transition.outputIntent = sixteenBitIntent();
    transition.clearOutputIntent = true;
    transition.record = makeEntry("save");

    an::Result<an::ImageMetadata> refused = an::applyMetadataTransition(before, transition);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
}

TEST(ImageMetadataTest, AWithdrawnOutputDecisionLeavesNoneRatherThanADefaultOne)
{
    an::Result<an::Provenance> provenance = an::Provenance::create(an::ProvenanceSourceKind::File, "frame.fit");
    ASSERT_TRUE(provenance.hasValue());
    const an::ImageMetadata before =
        an::ImageMetadata::create(acquiredFacts(), an::ImageSemantics{}, an::AstrometricSolution{},
                                  std::move(*provenance), an::CodecMetadata{}, sixteenBitIntent());
    ASSERT_TRUE(before.outputIntent().has_value());

    an::MetadataTransition transition;
    transition.clearOutputIntent = true;
    transition.record = makeEntry("reprocess");

    an::Result<an::ImageMetadata> after = an::applyMetadataTransition(before, transition);
    ASSERT_TRUE(after.hasValue()) << after.status().message();
    EXPECT_FALSE(after->outputIntent().has_value());
}

TEST(ImageMetadataTest, DroppingPreservedCardsIsSomethingAnOperationHasToState)
{
    an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, "OBSERVER", "someone", "");
    ASSERT_TRUE(card.hasValue());
    std::vector<an::CodecCard> cards;
    cards.push_back(*card);
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    ASSERT_TRUE(codec.hasValue());

    an::Result<an::Provenance> provenance = an::Provenance::create(an::ProvenanceSourceKind::File, "frame.fit");
    ASSERT_TRUE(provenance.hasValue());
    const an::ImageMetadata before =
        an::ImageMetadata::create(acquiredFacts(), an::ImageSemantics{}, an::AstrometricSolution{},
                                  std::move(*provenance), std::move(*codec), std::nullopt);
    ASSERT_EQ(before.codec().cards().size(), 1u);

    an::MetadataTransition kept;
    kept.record = makeEntry("crop");
    an::Result<an::ImageMetadata> unchanged = an::applyMetadataTransition(before, kept);
    ASSERT_TRUE(unchanged.hasValue());
    EXPECT_EQ(unchanged->codec().cards().size(), 1u);

    an::MetadataTransition dropped;
    dropped.clearCodecMetadata = true;
    dropped.record = makeEntry("stack");
    an::Result<an::ImageMetadata> after = an::applyMetadataTransition(before, dropped);
    ASSERT_TRUE(after.hasValue()) << after.status().message();
    EXPECT_TRUE(after->codec().cards().empty());
}

TEST(ImageMetadataTest, InvalidatingASolutionIsStatedByGivingAnAbsentOne)
{
    an::Result<an::Provenance> provenance = an::Provenance::create(an::ProvenanceSourceKind::File, "frame.fit");
    ASSERT_TRUE(provenance.hasValue());
    const an::ImageMetadata before =
        an::ImageMetadata::create(acquiredFacts(), an::ImageSemantics{}, solvedPointing(), std::move(*provenance),
                                  an::CodecMetadata{}, std::nullopt);
    ASSERT_TRUE(before.astrometry().present());

    an::MetadataTransition transition;
    transition.astrometry = an::AstrometricSolution{};
    transition.record = makeEntry("resample");
    EXPECT_TRUE(an::changesMeaning(transition));

    an::Result<an::ImageMetadata> after = an::applyMetadataTransition(before, transition);
    ASSERT_TRUE(after.hasValue()) << after.status().message();
    EXPECT_FALSE(after->astrometry().present());
}

TEST(ImageMetadataTest, ARefusedTransitionLeavesTheOriginalUntouched)
{
    const an::ImageMetadata before = decodedFrame();

    an::MetadataTransition transition;
    transition.semantics = calibratedLinear();

    an::Result<an::ImageMetadata> refused = an::applyMetadataTransition(before, transition);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(before, decodedFrame());
}

TEST(ImageMetadataTest, ALineageThatIsFullRefusesTheTransitionRatherThanDroppingTheRecord)
{
    an::Result<an::Provenance> provenance = an::Provenance::create(an::ProvenanceSourceKind::Generated, "synthetic");
    ASSERT_TRUE(provenance.hasValue());
    const an::ProvenanceEntry entry = makeEntry("step");
    for (std::size_t index = 0u; index < an::kMaxProvenanceEntries; ++index)
    {
        an::Result<an::Provenance> extended = provenance->appended(entry);
        ASSERT_TRUE(extended.hasValue()) << index;
        provenance = std::move(extended);
    }

    const an::ImageMetadata before =
        an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{}, an::AstrometricSolution{},
                                  std::move(*provenance), an::CodecMetadata{}, std::nullopt);

    an::MetadataTransition transition;
    transition.semantics = calibratedLinear();
    transition.record = entry;

    an::Result<an::ImageMetadata> refused = an::applyMetadataTransition(before, transition);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::LimitExceeded);
}

} // namespace
