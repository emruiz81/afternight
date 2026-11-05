// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Tests for a metadata defect a single bag of header cards invites.
//
// Picture an engine that keeps one bag of header cards. It reads a source file's cards into it, and
// then, every time an image's geometry or representation is re-described, it *writes back into the
// same bag* a set of cards derived from what the image currently is: a bit depth, a zero offset, a
// scale, and the axis lengths. Three things go wrong at once and only the third is obvious.
//
// The source's own statement is destroyed. A file that said its samples were stored with one offset
// no longer says so, because the value was overwritten by a value derived from an in-memory
// representation the file never had.
//
// A decision about output is taken by a call nobody would read as taking one. Re-describing an image
// is a geometry operation; it emits the cards that a later write will use, so the write inherits a
// decision made by whichever operation last touched the description.
//
// And one of the derived values is simply wrong: the 8-bit cases write a bit depth of 1, which is
// not a value the format defines.
//
// This engine splits the three into objects that cannot be confused: what the file said, what the
// image is, and how somebody has decided it will be written. These cases pin that separation from
// the direction the defect comes from - they set up the source facts that such a bag overwrites,
// then do the things that overwrite them, and assert that nothing moved.

#include <AfterNightEngine/Image/ImageMetadata.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

/// The cards a signed 16-bit FITS frame carries, as the file wrote them.
///
/// Chosen because it is the case the other engine gets most visibly wrong: it stores such a frame as
/// unsigned 16-bit in memory and then rewrites the offset card from the in-memory representation,
/// so the number the file stated is replaced by a number about the engine.
[[nodiscard]] std::vector<an::CodecCard> sourceCards()
{
    struct Written
    {
        const char* keyword;
        const char* value;
        const char* comment;
    };
    constexpr Written kWritten[] = {{"SIMPLE", "T", "conforms to FITS standard"},
                                    {"BITPIX", "16", "bits per data pixel"},
                                    {"BZERO", "32768", "offset applied by the writer"},
                                    {"BSCALE", "1", "multiplier applied by the writer"},
                                    {"NAXIS1", "4096", "length of data axis 1"}};

    std::vector<an::CodecCard> cards;
    for (const Written& written : kWritten)
    {
        an::Result<an::CodecCard> card =
            an::CodecCard::create(an::CodecNamespace::Fits, written.keyword, written.value, written.comment);
        EXPECT_TRUE(card.hasValue());
        if (card.hasValue())
        {
            cards.push_back(std::move(*card));
        }
    }
    return cards;
}

/// Metadata as a decode leaves it: the source's cards, its encoding, a lineage, and no decision
/// about how the image will be written.
[[nodiscard]] an::ImageMetadata decodedMetadata()
{
    an::SourceEncoding encoding;
    encoding.sampleFormat = an::RawSampleFormat{.sizeBytes = 2u, .sampleClass = an::RawSampleClass::SignedInteger};
    encoding.byteOrder = an::ByteOrder::Big;
    encoding.scale = 1.0;
    encoding.zeroOffset = 32768.0;

    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(encoding, sourceCards());
    EXPECT_TRUE(codec.hasValue());
    an::Result<an::Provenance> lineage = an::Provenance::create(an::ProvenanceSourceKind::File, "/corpus/light.fit");
    EXPECT_TRUE(lineage.hasValue());

    return an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{}, an::AstrometricSolution{},
                                     lineage.hasValue() ? std::move(*lineage) : an::Provenance{},
                                     codec.hasValue() ? std::move(*codec) : an::CodecMetadata{}, std::nullopt);
}

/// A descriptor for a small monochrome frame of `type`.
///
/// An optional rather than a value because a descriptor has no default state: it is either a
/// description of an image or it does not exist, which is the rule the whole type is built on.
[[nodiscard]] std::optional<an::ImageDescriptor> describe(an::SampleType type, std::uint32_t width = 8u,
                                                          std::uint32_t height = 4u)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageDescriptorRequest request{.extent = extent.value(),
                                             .sampleType = type,
                                             .channelSchema = an::ChannelSchema::Mono,
                                             .colorSpace = an::ColorSpace::Unspecified,
                                             .numericDomain = an::NumericDomain::NormalizedLinear,
                                             .cfa = an::CfaDescriptor{},
                                             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                             .pixelAspectRatio = 1.0};
    return an::ImageDescriptor::create(request);
}

/// An engine context with the default limits.
[[nodiscard]] an::EngineContext makeEngineContext()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 2u;
    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue());
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

} // namespace

TEST(SourceFactsTest, ADecodedImageHasSourceFactsAndNoDecisionAboutHowItIsWritten)
{
    const an::ImageMetadata metadata = decodedMetadata();

    // What the file said about its own encoding, kept as typed source facts.
    ASSERT_TRUE(metadata.codec().sourceEncoding().sampleFormat.has_value());
    EXPECT_EQ(metadata.codec().sourceEncoding().sampleFormat->sizeBytes, 2u);
    EXPECT_EQ(metadata.codec().sourceEncoding().sampleFormat->sampleClass, an::RawSampleClass::SignedInteger);
    EXPECT_EQ(metadata.codec().sourceEncoding().zeroOffset, 32768.0);

    // And nothing at all about how it leaves. Were these one bag, a decode would already have
    // decided the write.
    EXPECT_FALSE(metadata.outputIntent().has_value());
}

TEST(SourceFactsTest, RedescribingAnImageLeavesEveryCardTheSourceWroteExactlyAsItWroteIt)
{
    an::EngineContext engine = makeEngineContext();
    const std::optional<an::ImageDescriptor> described = describe(an::SampleType::UInt16);
    ASSERT_TRUE(described.has_value());
    an::Result<an::Image<std::uint16_t>> image = an::Image<std::uint16_t>::create(
        engine, *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, decodedMetadata());
    ASSERT_TRUE(image.hasValue()) << image.status().message();
    const an::CodecMetadata before = image->metadata().codec();

    // The operation the other engine emits header cards from: a change of description. Here it is a
    // change of description and nothing else, and it needs a record saying what changed it.
    an::Result<an::ProvenanceEntry> record =
        an::ProvenanceEntry::create("re-described the frame", "same samples, different meaning");
    ASSERT_TRUE(record.hasValue());
    an::ImageDescriptorRequest request = described->toRequest();
    request.coordinateOrigin = an::CoordinateOrigin::BottomLeft;
    const std::optional<an::ImageDescriptor> flipped = an::ImageDescriptor::create(request);
    ASSERT_TRUE(flipped.has_value());

    ASSERT_TRUE(image->transition(*flipped, an::MetadataTransition{.record = *record}).ok());

    EXPECT_EQ(image->metadata().codec(), before);
    const an::CodecCard* const depth = image->metadata().codec().find(an::CodecNamespace::Fits, "BITPIX");
    const an::CodecCard* const offset = image->metadata().codec().find(an::CodecNamespace::Fits, "BZERO");
    const an::CodecCard* const axis = image->metadata().codec().find(an::CodecNamespace::Fits, "NAXIS1");
    ASSERT_NE(depth, nullptr);
    ASSERT_NE(offset, nullptr);
    ASSERT_NE(axis, nullptr);
    // The frame is eight pixels wide and the card still says four thousand and ninety six, because a
    // preserved card is a record of what the file said and not a description of what the image is.
    EXPECT_EQ(depth->value(), "16");
    EXPECT_EQ(offset->value(), "32768");
    EXPECT_EQ(axis->value(), "4096");
    EXPECT_FALSE(image->metadata().outputIntent().has_value());
}

TEST(SourceFactsTest, ThereIsNoWayForAnOperationToRewriteOneCard)
{
    // A transition is the only door, and it has three doors of its own: state a new meaning, state a
    // new solution, state or withdraw an output decision. None of them names a card. Dropping the
    // preserved set whole is the only thing an operation may do to it, and it has to say so.
    const an::MetadataTransition nothing;
    EXPECT_FALSE(an::changesAnything(nothing));

    an::Result<an::ProvenanceEntry> record = an::ProvenanceEntry::create("stacked fifty frames", "");
    ASSERT_TRUE(record.hasValue());
    const an::ImageMetadata decoded = decodedMetadata();

    an::Result<an::ImageMetadata> kept =
        an::applyMetadataTransition(decoded, an::MetadataTransition{.record = *record});
    ASSERT_TRUE(kept.hasValue());
    EXPECT_EQ(kept->codec(), decoded.codec());

    an::Result<an::ImageMetadata> dropped =
        an::applyMetadataTransition(decoded, an::MetadataTransition{.clearCodecMetadata = true, .record = *record});
    ASSERT_TRUE(dropped.hasValue());
    EXPECT_TRUE(dropped->codec().cards().empty());
    // Dropping the cards drops the source encoding with them, because both are statements about a
    // source file that the result is no longer a frame of.
    EXPECT_FALSE(dropped->codec().sourceEncoding().sampleFormat.has_value());
    // And the original is untouched, so an operation that decided to drop them cannot have dropped
    // them from an image somebody else is holding.
    EXPECT_EQ(decoded.codec().cards().size(), 5u);
}

TEST(OutputDecisionTest, WritingIsDecidedBySomebodyAndNeverInheritedFromWhatTheSourceSaid)
{
    const an::ImageMetadata decoded = decodedMetadata();
    an::Result<an::ProvenanceEntry> record = an::ProvenanceEntry::create("chose an output encoding", "float, unscaled");
    ASSERT_TRUE(record.hasValue());

    // A decision that contradicts every source fact: the file held signed 16-bit big-endian samples
    // with an offset, and this write is little-endian 32-bit float with none. Both statements are
    // true at once, which is the point - one is about the past and one is about the future.
    an::OutputIntentRequest chosen;
    chosen.sampleType = an::SampleType::Float32;
    chosen.byteOrder = an::ByteOrder::Little;
    chosen.scaling = an::OutputScaling::None;
    an::Result<an::OutputIntent> intent = an::OutputIntent::create(chosen);
    ASSERT_TRUE(intent.hasValue()) << intent.status().message();

    an::Result<an::ImageMetadata> decided =
        an::applyMetadataTransition(decoded, an::MetadataTransition{.outputIntent = *intent, .record = *record});
    ASSERT_TRUE(decided.hasValue());

    ASSERT_TRUE(decided->outputIntent().has_value());
    EXPECT_EQ(decided->outputIntent()->sampleType(), an::SampleType::Float32);
    EXPECT_FALSE(decided->outputIntent()->scale().has_value());
    EXPECT_FALSE(decided->outputIntent()->zeroOffset().has_value());

    ASSERT_TRUE(decided->codec().sourceEncoding().sampleFormat.has_value());
    EXPECT_EQ(decided->codec().sourceEncoding().sampleFormat->sampleClass, an::RawSampleClass::SignedInteger);
    EXPECT_EQ(decided->codec().sourceEncoding().byteOrder, an::ByteOrder::Big);
    EXPECT_EQ(decided->codec().sourceEncoding().zeroOffset, 32768.0);
}

TEST(OutputDecisionTest, ADecisionAboutWritingCannotBeTakenWithoutSayingWhoTookIt)
{
    const an::ImageMetadata decoded = decodedMetadata();
    an::OutputIntentRequest chosen;
    chosen.sampleType = an::SampleType::UInt16;
    an::Result<an::OutputIntent> intent = an::OutputIntent::create(chosen);
    ASSERT_TRUE(intent.hasValue());

    // The defective path takes this decision inside a geometry call, with nothing recording that it
    // did. Here a decision with no record is refused outright.
    an::Result<an::ImageMetadata> anonymous =
        an::applyMetadataTransition(decoded, an::MetadataTransition{.outputIntent = *intent});
    ASSERT_FALSE(anonymous.hasValue());
    EXPECT_EQ(anonymous.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_FALSE(decoded.outputIntent().has_value());
}

TEST(OutputDecisionTest, EveryRepresentationTheEngineStoresIsStatableAsADestinationInItsOwnTerms)
{
    // The other engine encodes the destination as a format-level bit depth computed in a switch, and
    // gets the eight-bit cases wrong - a signed byte becomes a depth of one. Here the destination is
    // the engine's own representation, so there is no arithmetic to get wrong and the mapping onto a
    // format's own vocabulary belongs to the codec that owns that format.
    constexpr an::SampleType kResident[] = {an::SampleType::UInt8,   an::SampleType::Int8,   an::SampleType::UInt16,
                                            an::SampleType::Int16,   an::SampleType::UInt32, an::SampleType::Int32,
                                            an::SampleType::Float32, an::SampleType::Float64};
    for (const an::SampleType type : kResident)
    {
        an::OutputIntentRequest request;
        request.sampleType = type;
        // A one-byte sample has one order and stating another is refused, which is the rule that
        // makes a byte-order card meaningful when it is written at all.
        request.byteOrder = an::isByteOrderMeaningful(type) ? an::ByteOrder::Big : an::ByteOrder::Native;

        an::Result<an::OutputIntent> intent = an::OutputIntent::create(request);
        ASSERT_TRUE(intent.hasValue()) << an::sampleTypeName(type) << ": " << intent.status().message();
        EXPECT_EQ(intent->sampleType(), type) << an::sampleTypeName(type);
        EXPECT_EQ(intent->scaling(), an::OutputScaling::None) << an::sampleTypeName(type);
    }
}

TEST(OutputDecisionTest, AScalingIsStatedInFullOrNotAtAllRatherThanDerivedFromARepresentation)
{
    // The value the other engine writes for a signed 16-bit frame, taken here as a deliberate
    // decision rather than as a consequence of the pixel type - and it has to carry both of its
    // numbers, because a multiplier without an offset is half a mapping.
    an::OutputIntentRequest request;
    request.sampleType = an::SampleType::UInt16;
    request.scaling = an::OutputScaling::Explicit;
    request.scale = 1.0;
    an::Result<an::OutputIntent> halfStated = an::OutputIntent::create(request);
    ASSERT_FALSE(halfStated.hasValue());
    EXPECT_EQ(halfStated.status().code(), an::ErrorCode::IncompatibleArguments);

    request.zeroOffset = 32768.0;
    an::Result<an::OutputIntent> stated = an::OutputIntent::create(request);
    ASSERT_TRUE(stated.hasValue()) << stated.status().message();
    EXPECT_EQ(stated->scale(), 1.0);
    EXPECT_EQ(stated->zeroOffset(), 32768.0);
}
