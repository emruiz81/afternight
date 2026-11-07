// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/Image.h.
//
// Four properties are the reason this type looks the way it does, and each answers a defect that
// costs a wrong result rather than an abstract nicety.
//
// **It has no algorithm methods and no implicit conversions.** An image class that decodes files,
// converts representations, resizes and computes statistics ends up dispatching on a pixel type at
// every call site. The static assertions below hold the line that keeps that impossible: no copy, no
// conversion between two sample types, no conversion to a view or to storage.
//
// **Writing is asked for, and asking is recorded.** The pixel generation moves when write access is
// taken - whether or not a sample changes - because an unnecessary recomputation costs time and a
// derived result that survived a write it should not have costs a wrong answer with no symptom.
//
// **What an image means and what its samples are are separate counts.** A corrected object name
// must not discard a plate solution, and a change of colour basis must. That is the whole reason
// there are two generations rather than one.
//
// **A decoded frame becomes an image with no copy.** The adoption case checks the address, not just
// the values: the point of the boundary carrying its ownership is that the buffer moves and nothing
// is touched.

#include <AfterNightEngine/Image/Image.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>
#include <AfterNightEngine/Image/SampleBuffer.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

// The rules that keep this from becoming a class that does everything. Each of them would compile away
// silently if it were only a review comment.
static_assert(!std::is_copy_constructible_v<an::Image<std::uint16_t>>);
static_assert(!std::is_copy_assignable_v<an::Image<std::uint16_t>>);
static_assert(std::is_move_constructible_v<an::Image<std::uint16_t>>);
static_assert(!std::is_convertible_v<an::Image<std::uint16_t>, an::Image<float>>);
static_assert(!std::is_convertible_v<an::Image<std::uint16_t>, an::ImageView<std::uint16_t>>);
static_assert(!std::is_convertible_v<an::Image<std::uint16_t>, an::ImageStorage<std::uint16_t>>);
static_assert(!std::is_copy_constructible_v<an::ImageWriteAccess<std::uint16_t>>);
static_assert(std::is_same_v<an::WorkingImage<an::ComputePrecision::Float32>, an::Image<float>>);
static_assert(std::is_same_v<an::WorkingImage<an::ComputePrecision::Float64>, an::Image<double>>);
static_assert(an::Image<std::uint16_t>::kSampleType == an::SampleType::UInt16);
static_assert(std::is_same_v<an::Image<double>::Sample, double>);

/// An engine context with a stated memory budget.
///
/// @param budgetBytes what the context may have outstanding at once.
/// @return the context, which is valid or the case has already failed.
[[nodiscard]] an::EngineContext makeEngineContext(std::uint64_t budgetBytes = an::kDefaultMemoryBudgetBytes)
{
    an::EngineContextOptions options;
    options.maxConcurrency = 2u;
    options.limits = an::defaultEngineLimits();
    options.limits.memoryBudgetBytes = budgetBytes;

    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue());
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

/// A description of an uncalibrated monochrome sensor frame.
///
/// @param sampleType what one sample is.
/// @param width the frame's width.
/// @param height the frame's height.
/// @return the description.
[[nodiscard]] an::ImageDescriptor monoDescriptor(an::SampleType sampleType, std::uint32_t width = 8u,
                                                 std::uint32_t height = 4u)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = extent.value(),
                                                               .sampleType = sampleType,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::RawSensorCode,
                                                               .cfa = an::CfaDescriptor{},
                                                               .coordinateOrigin = an::CoordinateOrigin::BottomLeft,
                                                               .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value());
    return descriptor.value();
}

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

/// A processing meaning that says the frame is linear.
///
/// @return the meaning.
[[nodiscard]] an::ImageSemantics linearSemantics()
{
    an::ImageSemanticsRequest request;
    request.transferFunction = an::TransferFunction::Linear;
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(request);
    EXPECT_TRUE(semantics.hasValue());
    return semantics.hasValue() ? *semantics : an::ImageSemantics{};
}

TEST(ImageTest, AnImageIsItsSamplesItsDescriptionAndItsMetadata)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);
    const std::uint64_t before = resource->counters().liveBytes;

    const an::ImageDescriptor descriptor = monoDescriptor(an::SampleType::UInt16);
    an::Result<an::Image<std::uint16_t>> image = an::Image<std::uint16_t>::create(
        engine, descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(image.hasValue()) << image.status().message();

    EXPECT_TRUE(image->valid());
    EXPECT_EQ(image->descriptor(), descriptor);
    ASSERT_TRUE(image->layout().has_value());
    EXPECT_EQ(image->layout()->elementSizeBytes(), sizeof(std::uint16_t));
    EXPECT_EQ(image->layout()->extent(), descriptor.extent());
    EXPECT_EQ(image->pixelGeneration(), 0u);
    EXPECT_EQ(image->semanticGeneration(), 0u);
    EXPECT_EQ(image->metadata(), an::ImageMetadata{});
    EXPECT_GT(resource->counters().liveBytes, before);

    image->release();
    EXPECT_FALSE(image->valid());
    EXPECT_FALSE(image->descriptor().has_value());
    EXPECT_EQ(resource->counters().liveBytes, before);
}

TEST(ImageTest, ADescriptionOfAnotherRepresentationIsRefusedWithBothNames)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::Image<float>> refused =
        an::Image<float>::create(engine, monoDescriptor(an::SampleType::UInt16), an::StorageArrangement::Planar,
                                 an::StorageInitialization::Zeroed);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_NE(std::string(refused.status().message()).find("UInt16"), std::string::npos);
    EXPECT_NE(std::string(refused.status().message()).find("Float32"), std::string::npos);
}

TEST(ImageTest, AnArrangementNoDescriptionCanBeLaidOutInIsRefused)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::Image<std::uint16_t>> refused =
        an::Image<std::uint16_t>::create(engine, monoDescriptor(an::SampleType::UInt16),
                                         an::StorageArrangement::SemiPlanar, an::StorageInitialization::Zeroed);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
}

TEST(ImageTest, AFilledImageHoldsThatValueInEverySample)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::Image<std::uint16_t>> image = an::Image<std::uint16_t>::createFilled(
        engine, monoDescriptor(an::SampleType::UInt16), an::StorageArrangement::Planar, std::uint16_t{4242});
    ASSERT_TRUE(image.hasValue()) << image.status().message();

    const std::optional<an::ImageView<const std::uint16_t>> view = image->view();
    ASSERT_TRUE(view.has_value());
    for (std::uint32_t y = 0u; y < view->height(); ++y)
    {
        const std::optional<std::span<const std::uint16_t>> row = view->row(0u, y);
        ASSERT_TRUE(row.has_value());
        for (const std::uint16_t sample : *row)
        {
            EXPECT_EQ(sample, 4242u);
        }
    }
    EXPECT_EQ(image->pixelGeneration(), 0u);
}

TEST(ImageTest, ReadingIsFreeAndAskingToWriteIsRecorded)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::Image<std::uint16_t>> image =
        an::Image<std::uint16_t>::create(engine, monoDescriptor(an::SampleType::UInt16), an::StorageArrangement::Planar,
                                         an::StorageInitialization::Zeroed);
    ASSERT_TRUE(image.hasValue()) << image.status().message();

    EXPECT_TRUE(image->view().has_value());
    EXPECT_TRUE(image->view().has_value());
    EXPECT_EQ(image->pixelGeneration(), 0u);

    {
        std::optional<an::ImageWriteAccess<std::uint16_t>> access = image->write();
        ASSERT_TRUE(access.has_value());
        EXPECT_EQ(access->pixelGeneration(), 1u);
        EXPECT_EQ(image->pixelGeneration(), 1u);

        const std::optional<std::span<std::uint16_t>> row = access->view().row(0u, 0u);
        ASSERT_TRUE(row.has_value());
        (*row)[0] = 7u;
    }

    const std::optional<an::ImageView<const std::uint16_t>> view = image->view();
    ASSERT_TRUE(view.has_value());
    const std::optional<std::span<const std::uint16_t>> row = view->row(0u, 0u);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)[0], 7u);
}

TEST(ImageTest, TheGenerationMovesWhenWriteAccessIsTakenRatherThanWhenASampleChanges)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::Image<std::uint16_t>> image =
        an::Image<std::uint16_t>::create(engine, monoDescriptor(an::SampleType::UInt16), an::StorageArrangement::Planar,
                                         an::StorageInitialization::Zeroed);
    ASSERT_TRUE(image.hasValue()) << image.status().message();

    // Coarse on purpose: an unnecessary recomputation costs time, and a derived result that
    // survives a write it should not have costs a wrong answer with no symptom.
    std::optional<an::ImageWriteAccess<std::uint16_t>> first = image->write();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(image->pixelGeneration(), 1u);
    first.reset();
    EXPECT_EQ(image->pixelGeneration(), 1u);

    std::optional<an::ImageWriteAccess<std::uint16_t>> second = image->write();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(image->pixelGeneration(), 2u);
    EXPECT_EQ(second->pixelGeneration(), 2u);
    EXPECT_EQ(image->semanticGeneration(), 0u);
}

TEST(ImageTest, AMetadataChangeThatDoesNotChangeMeaningLeavesTheSemanticGenerationAlone)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::Image<std::uint16_t>> image =
        an::Image<std::uint16_t>::create(engine, monoDescriptor(an::SampleType::UInt16), an::StorageArrangement::Planar,
                                         an::StorageInitialization::Zeroed);
    ASSERT_TRUE(image.hasValue()) << image.status().message();

    an::MetadataTransition recordOnly;
    recordOnly.record = makeEntry("statistics");
    ASSERT_TRUE(image->transition(recordOnly).ok());
    EXPECT_EQ(image->semanticGeneration(), 0u);
    EXPECT_EQ(image->pixelGeneration(), 0u);
    EXPECT_EQ(image->metadata().provenance().entries().size(), 1u);

    // Cards an operation states are restated where the samples are: no generation moves, and no sample is touched.
    an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, "ANLIN", "'nonlinear'", "");
    ASSERT_TRUE(card.hasValue());
    std::vector<an::CodecCard> stated;
    stated.push_back(*card);
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(stated));
    ASSERT_TRUE(codec.hasValue());
    const void* const samples = image->view()->data();
    an::MetadataTransition cards;
    cards.codec = *codec;
    cards.record = makeEntry("record the state");
    ASSERT_TRUE(image->transition(cards).ok());
    EXPECT_EQ(image->semanticGeneration(), 0u);
    EXPECT_EQ(image->pixelGeneration(), 0u);
    EXPECT_EQ(image->metadata().codec(), *codec);
    EXPECT_EQ(image->metadata().provenance().entries().size(), 2u);
    EXPECT_EQ(image->view()->data(), samples) << "the cards were restated over other samples";

    an::MetadataTransition meaning;
    meaning.semantics = linearSemantics();
    meaning.record = makeEntry("calibrate");
    ASSERT_TRUE(image->transition(meaning).ok());
    EXPECT_EQ(image->semanticGeneration(), 1u);
    EXPECT_EQ(image->pixelGeneration(), 0u);
    EXPECT_TRUE(image->metadata().semantics().isLinear());
}

TEST(ImageTest, RedescribingAnImageMovesItsSemanticGenerationAndNeedsARecord)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageDescriptor sensorFrame = monoDescriptor(an::SampleType::UInt16);
    an::Result<an::Image<std::uint16_t>> image = an::Image<std::uint16_t>::create(
        engine, sensorFrame, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(image.hasValue()) << image.status().message();

    an::ImageDescriptorRequest calibrated = sensorFrame.toRequest();
    calibrated.numericDomain = an::NumericDomain::NormalizedLinear;
    const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(calibrated);
    ASSERT_TRUE(described.has_value());

    const an::Status refused = image->transition(*described, an::MetadataTransition{});
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(image->descriptor(), sensorFrame);
    EXPECT_EQ(image->semanticGeneration(), 0u);

    an::MetadataTransition transition;
    transition.record = makeEntry("calibrate");
    ASSERT_TRUE(image->transition(*described, transition).ok());
    EXPECT_EQ(image->descriptor(), described);
    EXPECT_EQ(image->semanticGeneration(), 1u);
    EXPECT_EQ(image->pixelGeneration(), 0u);
}

TEST(ImageTest, AChangeOfValidityMovesTheSemanticGenerationBecauseItIsAChangeOfMeaning)
{
    // A result computed while every sample meant something is wrong once some of them do not, so a
    // validity change has to invalidate what was derived from the image. It does so because the
    // rule travels in the processing meaning rather than beside it: one rule about what moves the
    // semantic generation, and no second list to keep in step with it.
    an::EngineContext engine = makeEngineContext();
    const an::ImageDescriptor descriptor = monoDescriptor(an::SampleType::Float32);
    an::Result<an::Image<float>> image =
        an::Image<float>::create(engine, descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(image.hasValue()) << image.status().message();
    EXPECT_TRUE(image->metadata().semantics().validity().isAllValid());

    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    an::ImageSemanticsRequest meaning = image->metadata().semantics().toRequest();
    meaning.validity = *finite;
    an::Result<an::ImageSemantics> invalidated = an::ImageSemantics::create(meaning);
    ASSERT_TRUE(invalidated.hasValue());

    an::MetadataTransition transition;
    transition.semantics = *invalidated;
    transition.record = makeEntry("divide by flat");
    EXPECT_TRUE(an::changesMeaning(transition));
    ASSERT_TRUE(image->transition(transition).ok());

    EXPECT_EQ(image->semanticGeneration(), 1u);
    EXPECT_EQ(image->pixelGeneration(), 0u);
    EXPECT_EQ(image->metadata().semantics().validity(), *finite);
}

TEST(ImageTest, RedescribingWithTheSameDescriptionIsNotAChange)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageDescriptor descriptor = monoDescriptor(an::SampleType::UInt16);
    an::Result<an::Image<std::uint16_t>> image = an::Image<std::uint16_t>::create(
        engine, descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(image.hasValue()) << image.status().message();

    ASSERT_TRUE(image->transition(descriptor, an::MetadataTransition{}).ok());
    EXPECT_EQ(image->semanticGeneration(), 0u);
}

TEST(ImageTest, ADescriptionThatDoesNotDescribeTheseSamplesIsRefused)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::Image<std::uint16_t>> image =
        an::Image<std::uint16_t>::create(engine, monoDescriptor(an::SampleType::UInt16), an::StorageArrangement::Planar,
                                         an::StorageInitialization::Zeroed);
    ASSERT_TRUE(image.hasValue()) << image.status().message();

    an::MetadataTransition transition;
    transition.record = makeEntry("reinterpret");

    const an::Status differentType = image->transition(monoDescriptor(an::SampleType::Int16), transition);
    EXPECT_EQ(differentType.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(image->descriptor(), monoDescriptor(an::SampleType::UInt16));

    const an::Status differentSize = image->transition(monoDescriptor(an::SampleType::Float32), transition);
    EXPECT_EQ(differentSize.code(), an::ErrorCode::IncompatibleArguments);

    const an::Status differentExtent = image->transition(monoDescriptor(an::SampleType::UInt16, 16u, 4u), transition);
    EXPECT_EQ(differentExtent.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(image->semanticGeneration(), 0u);
}

TEST(ImageTest, ACloneCopiesTheSamplesTheDescriptionAndTheMetadataAndStartsItsOwnHistory)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::Image<std::uint16_t>> image = an::Image<std::uint16_t>::createFilled(
        engine, monoDescriptor(an::SampleType::UInt16), an::StorageArrangement::Planar, std::uint16_t{9});
    ASSERT_TRUE(image.hasValue()) << image.status().message();

    an::MetadataTransition transition;
    transition.semantics = linearSemantics();
    transition.record = makeEntry("calibrate");
    ASSERT_TRUE(image->transition(transition).ok());
    ASSERT_TRUE(image->write().has_value());
    EXPECT_EQ(image->pixelGeneration(), 1u);
    EXPECT_EQ(image->semanticGeneration(), 1u);

    an::Result<an::Image<std::uint16_t>> copy = image->clone();
    ASSERT_TRUE(copy.hasValue()) << copy.status().message();

    EXPECT_EQ(copy->descriptor(), image->descriptor());
    EXPECT_EQ(copy->metadata(), image->metadata());
    EXPECT_EQ(copy->pixelGeneration(), 0u);
    EXPECT_EQ(copy->semanticGeneration(), 0u);
    EXPECT_NE(copy->storage().data(), image->storage().data());

    const std::optional<an::ImageView<const std::uint16_t>> view = copy->view();
    ASSERT_TRUE(view.has_value());
    const std::optional<std::span<const std::uint16_t>> row = view->row(0u, 0u);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)[0], 9u);
}

TEST(ImageTest, ADestinationCarriesWhatIsKnownAboutTheImageTheOperationReads)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::Image<std::uint16_t>> source =
        an::Image<std::uint16_t>::create(engine, monoDescriptor(an::SampleType::UInt16), an::StorageArrangement::Planar,
                                         an::StorageInitialization::Zeroed);
    ASSERT_TRUE(source.hasValue()) << source.status().message();

    an::MetadataTransition transition;
    transition.semantics = linearSemantics();
    transition.record = makeEntry("calibrate");
    ASSERT_TRUE(source->transition(transition).ok());

    // A destination of another representation, described for itself, carrying the input's history.
    const an::Result<an::Image<float>> destination = an::Image<float>::createDestination(
        engine, monoDescriptor(an::SampleType::Float32), an::StorageArrangement::Planar,
        an::StorageInitialization::Uninitialized, *source);
    ASSERT_TRUE(destination.hasValue()) << destination.status().message();

    EXPECT_EQ(destination->descriptor()->sampleType(), an::SampleType::Float32);
    EXPECT_EQ(destination->metadata(), source->metadata());
    EXPECT_EQ(destination->metadata().provenance().entries().size(), source->metadata().provenance().entries().size());
    EXPECT_EQ(destination->pixelGeneration(), 0u);
    EXPECT_EQ(destination->semanticGeneration(), 0u);

    // And what `create` leaves behind, which is the failure the named allocation exists to prevent.
    const an::Result<an::Image<float>> forgotten =
        an::Image<float>::create(engine, monoDescriptor(an::SampleType::Float32), an::StorageArrangement::Planar,
                                 an::StorageInitialization::Uninitialized);
    ASSERT_TRUE(forgotten.hasValue()) << forgotten.status().message();
    EXPECT_TRUE(forgotten->metadata().provenance().entries().empty());
    EXPECT_NE(forgotten->metadata(), source->metadata());
}

TEST(ImageTest, AnImageOverStorageSomethingElseFilledKeepsThoseSamples)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageDescriptor descriptor = monoDescriptor(an::SampleType::UInt16);
    const std::optional<an::ImageLayoutRequest> request =
        an::layoutRequestFor(descriptor, an::StorageArrangement::Planar);
    ASSERT_TRUE(request.has_value());

    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::createFilled(engine, *request, std::uint16_t{11});
    ASSERT_TRUE(storage.hasValue()) << storage.status().message();
    const std::uint16_t* const address = storage->data();

    an::Result<an::Image<std::uint16_t>> image = an::Image<std::uint16_t>::fromStorage(std::move(*storage), descriptor);
    ASSERT_TRUE(image.hasValue()) << image.status().message();
    EXPECT_EQ(image->storage().data(), address);

    const std::optional<an::ImageView<const std::uint16_t>> view = image->view();
    ASSERT_TRUE(view.has_value());
    const std::optional<std::span<const std::uint16_t>> row = view->row(0u, 0u);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)[0], 11u);
}

TEST(ImageTest, StorageThatDoesNotMatchTheDescriptionIsRefusedAndIsLeftWithTheCaller)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageDescriptor descriptor = monoDescriptor(an::SampleType::UInt16, 8u, 4u);
    const std::optional<an::ImageLayoutRequest> request =
        an::layoutRequestFor(monoDescriptor(an::SampleType::UInt16, 16u, 4u), an::StorageArrangement::Planar);
    ASSERT_TRUE(request.has_value());

    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::create(engine, *request, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(storage.hasValue()) << storage.status().message();

    an::Result<an::Image<std::uint16_t>> refused =
        an::Image<std::uint16_t>::fromStorage(std::move(*storage), descriptor);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);

    an::Result<an::Image<std::uint16_t>> empty =
        an::Image<std::uint16_t>::fromStorage(an::ImageStorage<std::uint16_t>{}, descriptor);
    ASSERT_FALSE(empty.hasValue());
    EXPECT_EQ(empty.code(), an::ErrorCode::NotInitialized);
}

TEST(ImageTest, ADecodedFrameBecomesAnImageWithoutMovingAByte)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageDescriptor descriptor = monoDescriptor(an::SampleType::UInt16);
    const std::optional<an::ImageLayoutRequest> request =
        an::layoutRequestFor(descriptor, an::StorageArrangement::Planar);
    ASSERT_TRUE(request.has_value());
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());

    const std::optional<an::RawFrameDescriptor> boundary = an::RawFrameDescriptor::create(
        an::RawFrameDescriptorRequest{.extent = descriptor.extent(),
                                      .sampleFormat = an::rawSampleFormatOf(an::SampleType::UInt16),
                                      .byteOrder = an::nativeByteOrder(),
                                      .channelSchema = an::ChannelSchema::Mono,
                                      .colorSpace = an::ColorSpace::Unspecified,
                                      .numericDomain = an::NumericDomain::RawSensorCode,
                                      .cfa = an::CfaDescriptor{},
                                      .coordinateOrigin = an::CoordinateOrigin::BottomLeft,
                                      .pixelAspectRatio = 1.0});
    ASSERT_TRUE(boundary.has_value());

    an::Result<an::SampleBuffer> buffer =
        an::SampleBuffer::createOwned(engine, *layout, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();
    an::Result<an::RawFrame> frame = an::RawFrame::create(*boundary, *layout, std::move(*buffer));
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();

    const std::optional<std::span<std::byte>> decoded = frame->mutableRow(0u, 0u);
    ASSERT_TRUE(decoded.has_value());
    (*decoded)[0] = std::byte{0x2A};
    const void* const address = frame->bytes().data();

    an::Result<an::Image<std::uint16_t>> image = an::Image<std::uint16_t>::adopt(std::move(*frame));
    ASSERT_TRUE(image.hasValue()) << image.status().message();

    EXPECT_EQ(static_cast<const void*>(image->storage().data()), address);
    EXPECT_EQ(image->descriptor(), descriptor);
    EXPECT_FALSE(frame->valid());
}

TEST(ImageTest, AdoptingIsRefusedWhenTheFrameHoldsAnotherRepresentation)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageDescriptor descriptor = monoDescriptor(an::SampleType::UInt16);
    const std::optional<an::ImageLayoutRequest> request =
        an::layoutRequestFor(descriptor, an::StorageArrangement::Planar);
    ASSERT_TRUE(request.has_value());
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());

    const std::optional<an::RawFrameDescriptor> boundary = an::RawFrameDescriptor::create(
        an::RawFrameDescriptorRequest{.extent = descriptor.extent(),
                                      .sampleFormat = an::rawSampleFormatOf(an::SampleType::UInt16),
                                      .byteOrder = an::nativeByteOrder(),
                                      .channelSchema = an::ChannelSchema::Mono,
                                      .colorSpace = an::ColorSpace::Unspecified,
                                      .numericDomain = an::NumericDomain::RawSensorCode,
                                      .cfa = an::CfaDescriptor{},
                                      .coordinateOrigin = an::CoordinateOrigin::BottomLeft,
                                      .pixelAspectRatio = 1.0});
    ASSERT_TRUE(boundary.has_value());

    an::Result<an::SampleBuffer> buffer =
        an::SampleBuffer::createOwned(engine, *layout, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(buffer.hasValue());
    an::Result<an::RawFrame> frame = an::RawFrame::create(*boundary, *layout, std::move(*buffer));
    ASSERT_TRUE(frame.hasValue());

    an::Result<an::Image<std::int16_t>> refused = an::Image<std::int16_t>::adopt(std::move(*frame));
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(frame->valid());
}

TEST(ImageTest, AnImageIsLentToACodecAsAFrameWithoutMovingAByte)
{
    // The counterpart of adopting a decoded frame: what an operation computed goes to a writer as a
    // frame that describes the image and borrows its samples. Borrowed bytes are never adoptable, so
    // the frame cannot be mistaken for a second owner of the image's memory.
    an::EngineContext engine = makeEngineContext();
    const an::ImageDescriptor descriptor = monoDescriptor(an::SampleType::UInt16);
    an::Result<an::Image<std::uint16_t>> image =
        an::Image<std::uint16_t>::createFilled(engine, descriptor, an::StorageArrangement::Planar, 7u);
    ASSERT_TRUE(image.hasValue()) << image.status().message();

    const an::Result<an::RawFrame> frame = an::borrowImageFrame(*image);
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();
    EXPECT_EQ(static_cast<const void*>(frame->bytes().data()), static_cast<const void*>(image->storage().data()));
    EXPECT_EQ(frame->layout(), image->layout());
    ASSERT_TRUE(frame->descriptor().has_value());
    EXPECT_EQ(frame->descriptor()->toImageDescriptor(), descriptor);
    EXPECT_EQ(frame->buffer().ownership(), an::SampleBufferOwnership::Borrowed);
    EXPECT_FALSE(frame->checkAdoptable().ok());

    // An image that holds nothing lends nothing.
    const an::Image<std::uint16_t> empty;
    EXPECT_EQ(an::borrowImageFrame(empty).status().code(), an::ErrorCode::NotInitialized);
}

TEST(ImageTest, AWorkingImageIsTheSamePictureAtAChosenWidth)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::Image<std::uint16_t>> source =
        an::Image<std::uint16_t>::create(engine, monoDescriptor(an::SampleType::UInt16), an::StorageArrangement::Planar,
                                         an::StorageInitialization::Zeroed);
    ASSERT_TRUE(source.hasValue()) << source.status().message();

    an::MetadataTransition transition;
    transition.semantics = linearSemantics();
    transition.record = makeEntry("calibrate");
    ASSERT_TRUE(source->transition(transition).ok());

    an::Result<an::Image<float>> working = an::createWorkingImage<an::ComputePrecision::Float32>(
        engine, *source, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(working.hasValue()) << working.status().message();

    ASSERT_TRUE(working->descriptor().has_value());
    EXPECT_EQ(working->descriptor()->sampleType(), an::SampleType::Float32);
    EXPECT_EQ(working->descriptor()->extent(), source->descriptor()->extent());
    EXPECT_EQ(working->descriptor()->channelSchema(), source->descriptor()->channelSchema());
    EXPECT_EQ(working->descriptor()->numericDomain(), source->descriptor()->numericDomain());
    EXPECT_EQ(working->metadata(), source->metadata());

    an::Result<an::Image<double>> wider = an::createWorkingImage<an::ComputePrecision::Float64>(
        engine, *source, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(wider.hasValue()) << wider.status().message();
    EXPECT_EQ(wider->descriptor()->sampleType(), an::SampleType::Float64);
    EXPECT_EQ(wider->descriptor()->extent(), source->descriptor()->extent());

    // The wider working image holds wider samples of the same picture. Its total is not simply
    // twice the narrower one: both layouts pad their rows to the same boundary, so the doubling
    // shows in the element size and in the row's own bytes rather than in the allocation.
    ASSERT_TRUE(wider->layout().has_value());
    ASSERT_TRUE(working->layout().has_value());
    EXPECT_EQ(wider->layout()->elementSizeBytes(), 2u * working->layout()->elementSizeBytes());
    EXPECT_EQ(wider->layout()->minimumRowBytes(), 2u * working->layout()->minimumRowBytes());
}

TEST(ImageTest, AWorkingImageOfAnImageThatHoldsNothingIsRefused)
{
    an::EngineContext engine = makeEngineContext();
    const an::Image<std::uint16_t> empty;

    an::Result<an::Image<float>> refused = an::createWorkingImage<an::ComputePrecision::Float32>(
        engine, empty, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::NotInitialized);
}

TEST(ImageTest, ADescriptionThatCannotBeRepresentedAtAWidthIsRefusedRatherThanApproximated)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(4u, 4u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> physical =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = extent.value(),
                                                               .sampleType = an::SampleType::Float32,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::PhysicalScaled,
                                                               .cfa = an::CfaDescriptor{},
                                                               .coordinateOrigin = an::CoordinateOrigin::BottomLeft,
                                                               .pixelAspectRatio = 1.0});
    ASSERT_TRUE(physical.has_value());

    // A physically scaled image has no integer form: the description carries no scale factor for an
    // integer to be read through.
    an::Result<an::ImageDescriptor> refused = an::describeSamplesAs(*physical, an::SampleType::UInt16);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);

    an::Result<an::ImageDescriptor> wider = an::describeSamplesAs(*physical, an::SampleType::Float64);
    ASSERT_TRUE(wider.hasValue()) << wider.status().message();
    EXPECT_EQ(wider->sampleType(), an::SampleType::Float64);
    EXPECT_EQ(wider->numericDomain(), an::NumericDomain::PhysicalScaled);

    an::Result<an::ImageDescriptor> unknown = an::describeSamplesAs(*physical, static_cast<an::SampleType>(200));
    ASSERT_FALSE(unknown.hasValue());
    EXPECT_EQ(unknown.code(), an::ErrorCode::InvalidArgument);
}

TEST(ImageTest, AnImageRefusesToLetItsEngineContextShutDown)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::Image<std::uint16_t>> image =
        an::Image<std::uint16_t>::create(engine, monoDescriptor(an::SampleType::UInt16), an::StorageArrangement::Planar,
                                         an::StorageInitialization::Zeroed);
    ASSERT_TRUE(image.hasValue()) << image.status().message();

    const an::Status refused = engine.shutdown();
    EXPECT_TRUE(refused.failed());

    image->release();
    EXPECT_TRUE(engine.shutdown().ok());
}

TEST(ImageTest, AnImageThatDoesNotFitTheBudgetIsRefusedWithTheNumbers)
{
    an::EngineContext engine = makeEngineContext(1024u);
    an::Result<an::Image<std::uint16_t>> refused =
        an::Image<std::uint16_t>::create(engine, monoDescriptor(an::SampleType::UInt16, 4096u, 4096u),
                                         an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_NE(refused.status().findContext(an::ContextKey::Requested), nullptr);
    EXPECT_NE(refused.status().findContext(an::ContextKey::Limit), nullptr);
}

TEST(ImageTest, AnImageThatHoldsNothingAnswersEveryQuestionWithoutBeingAsked)
{
    an::Image<std::uint16_t> image;
    EXPECT_FALSE(image.valid());
    EXPECT_FALSE(image.descriptor().has_value());
    EXPECT_FALSE(image.layout().has_value());
    EXPECT_FALSE(image.view().has_value());
    EXPECT_FALSE(image.write().has_value());
    EXPECT_EQ(image.pixelGeneration(), 0u);
    EXPECT_EQ(image.semanticGeneration(), 0u);
    EXPECT_EQ(image.metadata(), an::ImageMetadata{});
    EXPECT_EQ(image.clone().code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(image.transition(an::MetadataTransition{}).code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(image.transition(monoDescriptor(an::SampleType::UInt16), an::MetadataTransition{}).code(),
              an::ErrorCode::NotInitialized);

    image.release();
    EXPECT_FALSE(image.valid());
}

TEST(ImageTest, MovingAnImageMovesItsHistoryAndLeavesTheOtherHoldingNothing)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::Image<std::uint16_t>> image =
        an::Image<std::uint16_t>::create(engine, monoDescriptor(an::SampleType::UInt16), an::StorageArrangement::Planar,
                                         an::StorageInitialization::Zeroed);
    ASSERT_TRUE(image.hasValue()) << image.status().message();
    ASSERT_TRUE(image->write().has_value());
    const std::uint16_t* const address = image->storage().data();

    an::Image<std::uint16_t> moved = std::move(*image);
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.storage().data(), address);
    EXPECT_EQ(moved.pixelGeneration(), 1u);
    EXPECT_FALSE(image->valid());
}

TEST(ImageTest, EveryResidentRepresentationCanBeAnImage)
{
    an::EngineContext engine = makeEngineContext();

    EXPECT_TRUE(an::Image<std::uint8_t>::create(engine, monoDescriptor(an::SampleType::UInt8),
                                                an::StorageArrangement::Planar, an::StorageInitialization::Zeroed)
                    .hasValue());
    EXPECT_TRUE(an::Image<std::int8_t>::create(engine, monoDescriptor(an::SampleType::Int8),
                                               an::StorageArrangement::Planar, an::StorageInitialization::Zeroed)
                    .hasValue());
    EXPECT_TRUE(an::Image<std::uint16_t>::create(engine, monoDescriptor(an::SampleType::UInt16),
                                                 an::StorageArrangement::Planar, an::StorageInitialization::Zeroed)
                    .hasValue());
    EXPECT_TRUE(an::Image<std::int16_t>::create(engine, monoDescriptor(an::SampleType::Int16),
                                                an::StorageArrangement::Planar, an::StorageInitialization::Zeroed)
                    .hasValue());
    EXPECT_TRUE(an::Image<std::uint32_t>::create(engine, monoDescriptor(an::SampleType::UInt32),
                                                 an::StorageArrangement::Planar, an::StorageInitialization::Zeroed)
                    .hasValue());
    EXPECT_TRUE(an::Image<std::int32_t>::create(engine, monoDescriptor(an::SampleType::Int32),
                                                an::StorageArrangement::Planar, an::StorageInitialization::Zeroed)
                    .hasValue());
    EXPECT_TRUE(an::Image<float>::create(engine, monoDescriptor(an::SampleType::Float32),
                                         an::StorageArrangement::Planar, an::StorageInitialization::Zeroed)
                    .hasValue());
    EXPECT_TRUE(an::Image<double>::create(engine, monoDescriptor(an::SampleType::Float64),
                                          an::StorageArrangement::Planar, an::StorageInitialization::Zeroed)
                    .hasValue());
}

} // namespace

TEST(ImageTest, SameWidthSampleTypeMismatchPreservesStorageAndDescription)
{
    const an::EngineContext engine = makeEngineContext();
    const auto descriptor = monoDescriptor(an::SampleType::UInt16);
    const auto wrong = monoDescriptor(an::SampleType::Int16);
    const auto request = an::layoutRequestFor(descriptor, an::StorageArrangement::Planar);
    ASSERT_TRUE(request.has_value());
    auto storage = an::ImageStorage<std::uint16_t>::createFilled(engine, *request, std::uint16_t{65535});
    ASSERT_TRUE(storage.hasValue());
    const auto* address = storage->data();
    const auto refused = an::Image<std::uint16_t>::fromStorage(std::move(*storage), wrong);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(storage->data(), address);
    EXPECT_EQ(storage->data()[0], 65535u);
    auto image = an::Image<std::uint16_t>::fromStorage(std::move(*storage), descriptor);
    ASSERT_TRUE(image.hasValue());
    const auto generation = image->semanticGeneration();
    EXPECT_EQ(image->transition(wrong, {}).code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(image->descriptor(), descriptor);
    EXPECT_EQ(image->semanticGeneration(), generation);
    EXPECT_EQ(image->storage().data(), address);
}
