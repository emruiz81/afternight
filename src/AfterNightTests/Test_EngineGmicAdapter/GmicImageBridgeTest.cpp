// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngineGmicAdapter/GmicImageBridge.h.
//
// Every case here runs in both configurations of the adapter, because none of them needs the
// library: the bridge is a set of decisions about two memory models, and both models are described
// rather than constructed. That is the property being pinned as much as the answers are - a bridge
// that needed G'MIC to decide whether an image is shareable could not answer before loading it.
//
// Three groups of cases, and they fail differently.
//
// The forward direction is where the bridge's whole subject lives. The library's image has no stride
// field at all, so a padded row is not a slower path, it is a different picture: the samples after
// the end of one row are the beginning of the next one as far as the library is concerned. A float
// buffer handed over without asking is read wrong, so the cases that matter most here are the ones
// that refuse - a padded row, a plane that is not where the next one starts, an interleaved
// arrangement, anything that is not float.
//
// The size arithmetic is its own group because it is the one place four unsigned dimensions are
// multiplied together. A count that wraps is a buffer that is described as smaller than it is, which
// a 32-bit product does.
//
// The reverse direction is what a result goes through, and what it checks is what the library is
// free to change while a command runs: the representation, the interleaving, the third dimension and
// the size. Nothing there is a stride, because the library's own buffer never has one.

#include <AfterNightEngineGmicAdapter/GmicImageBridge.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Interop/ExternalBuffer.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{

/// A contract every rule accepts, so a case can break exactly one thing.
[[nodiscard]] constexpr an::ExternalBufferContract
shareableContract(an::BufferMutability mutability = an::BufferMutability::ReadOnly,
                  an::BufferOwnership ownership = an::BufferOwnership::EngineOwned) noexcept
{
    return an::ExternalBufferContract{.engineDomain = an::NumericDomain::NormalizedLinear,
                                      .libraryDomain = an::NumericDomain::NormalizedLinear,
                                      .ownership = ownership,
                                      .scope = an::BorrowScope::SingleCall,
                                      .mutability = mutability};
}

/// One layout, built the way a caller holding an image would build it.
[[nodiscard]] an::ImageLayout layoutOf(std::uint32_t width, std::uint32_t height, std::uint32_t samplesPerPixel,
                                       std::uint32_t planeCount, std::uint32_t elementSizeBytes,
                                       std::uint64_t rowStrideBytes, std::uint64_t planeStrideBytes)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, samplesPerPixel * planeCount, limits);
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = elementSizeBytes,
                                         .samplesPerPixel = samplesPerPixel,
                                         .planeCount = planeCount,
                                         .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStrideBytes, planeStrideBytes, limits);
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// A tightly packed planar float layout, which is the one arrangement the library addresses directly.
[[nodiscard]] an::ImageLayout packedFloatLayout(std::uint32_t width, std::uint32_t height, std::uint32_t planes)
{
    const std::uint64_t row = static_cast<std::uint64_t>(width) * 4u;
    return layoutOf(width, height, 1u, planes, 4u, row, row * height);
}

/// A description the library could have written back, valid until a case breaks one field.
[[nodiscard]] constexpr an::GmicImageDescription libraryResult(void* data, std::uint32_t width, std::uint32_t height,
                                                               std::uint32_t spectrum) noexcept
{
    return an::GmicImageDescription{.data = data,
                                    .width = width,
                                    .height = height,
                                    .depth = 1u,
                                    .spectrum = spectrum,
                                    .interleaved = false,
                                    .format = an::GmicPixelFormat::Float32,
                                    .readOnly = false};
}

} // namespace

TEST(GmicImageBridgeTest, TheRepresentationThisAdapterUsesIsFloatAndNothingElse)
{
    EXPECT_EQ(an::kGmicSampleType, an::SampleType::Float32);
    EXPECT_TRUE(an::isGmicPixelFormat(an::GmicPixelFormat::Float32));
    EXPECT_TRUE(an::isGmicPixelFormat(an::GmicPixelFormat::UInt8));
    EXPECT_FALSE(an::isGmicPixelFormat(static_cast<an::GmicPixelFormat>(7)));
    // The values are the library's own, so a caller assigns them straight across.
    EXPECT_EQ(static_cast<std::int32_t>(an::GmicPixelFormat::Float32), 0);
    EXPECT_EQ(static_cast<std::int32_t>(an::GmicPixelFormat::UInt8), 1);
    EXPECT_STREQ(an::gmicPixelFormatName(an::GmicPixelFormat::Float32), "Float32");
    EXPECT_STREQ(an::gmicPixelFormatName(an::GmicPixelFormat::UInt8), "UInt8");
    EXPECT_STREQ(an::gmicPixelFormatName(static_cast<an::GmicPixelFormat>(7)), "UnknownGmicPixelFormat");
}

TEST(GmicImageBridgeTest, APackedPlanarFloatImageIsDescribedWithNothingBetweenIt)
{
    std::vector<float> samples(static_cast<std::size_t>(8 * 5 * 3), 0.0f);
    const an::ImageLayout layout = packedFloatLayout(8u, 5u, 3u);
    const std::optional<an::ImageView<const float>> image =
        an::ImageView<const float>::create(std::span<const float>(samples), layout);
    ASSERT_TRUE(image.has_value());

    ASSERT_EQ(an::checkGmicImage(*image, shareableContract()), an::BridgeRefusal::None);
    const std::optional<an::GmicImageDescription> description = an::describeAsGmicImage(*image, shareableContract());
    ASSERT_TRUE(description.has_value());
    EXPECT_EQ(description->data, samples.data());
    EXPECT_EQ(description->width, 8u);
    EXPECT_EQ(description->height, 5u);
    EXPECT_EQ(description->depth, 1u);
    EXPECT_EQ(description->spectrum, 3u);
    EXPECT_FALSE(description->interleaved);
    EXPECT_EQ(description->format, an::GmicPixelFormat::Float32);
    EXPECT_TRUE(description->readOnly);
    EXPECT_EQ(an::gmicSampleCount(*description), std::optional<std::uint64_t>{8u * 5u * 3u});
    EXPECT_EQ(an::gmicByteCount(*description), std::optional<std::uint64_t>{8u * 5u * 3u * 4u});
}

TEST(GmicImageBridgeTest, AMutableViewUnderAWritableContractIsNotReadOnly)
{
    std::vector<float> samples(static_cast<std::size_t>(4 * 4), 0.0f);
    const an::ImageLayout layout = packedFloatLayout(4u, 4u, 1u);
    const std::optional<an::ImageView<float>> image = an::ImageView<float>::create(std::span<float>(samples), layout);
    ASSERT_TRUE(image.has_value());

    const std::optional<an::GmicImageDescription> shared =
        an::describeAsGmicImage(*image, shareableContract(an::BufferMutability::ReadWrite));
    ASSERT_TRUE(shared.has_value());
    EXPECT_FALSE(shared->readOnly);

    // The same samples under a read-only contract are described read-only, because the contract
    // decides what the borrower may do and the sample type decides what it can do.
    const std::optional<an::GmicImageDescription> lent =
        an::describeAsGmicImage(*image, shareableContract(an::BufferMutability::ReadOnly));
    ASSERT_TRUE(lent.has_value());
    EXPECT_TRUE(lent->readOnly);
}

TEST(GmicImageBridgeTest, APaddedRowIsRefusedRatherThanQuietlyMisread)
{
    // The bridge's subject in one case. The library's image has no row stride, so a padded buffer
    // handed to it is read as a picture whose rows walk sideways.
    const an::ImageLayout padded = layoutOf(8u, 5u, 1u, 1u, 4u, 8u * 4u + 16u, (8u * 4u + 16u) * 5u);
    EXPECT_EQ(an::checkGmicImageGeometry(an::SampleType::Float32, padded),
              an::BridgeRefusal::RowStrideNotRepresentable);
}

TEST(GmicImageBridgeTest, PlanesThatAreNotConsecutiveAreRefused)
{
    const an::ImageLayout gapped = layoutOf(8u, 5u, 1u, 3u, 4u, 8u * 4u, 8u * 4u * 5u + 64u);
    EXPECT_EQ(an::checkGmicImageGeometry(an::SampleType::Float32, gapped),
              an::BridgeRefusal::ArrangementNotRepresentable);
}

TEST(GmicImageBridgeTest, InterleavedStorageIsRefusedBecauseTheLibraryWouldTransposeACopy)
{
    const an::ImageLayout interleaved = layoutOf(8u, 5u, 3u, 1u, 4u, 8u * 3u * 4u, 8u * 3u * 4u * 5u);
    EXPECT_EQ(an::checkGmicImageGeometry(an::SampleType::Float32, interleaved),
              an::BridgeRefusal::ArrangementNotRepresentable);
}

TEST(GmicImageBridgeTest, EveryRepresentationButFloatIsRefusedByName)
{
    constexpr an::SampleType kOthers[] = {an::SampleType::UInt8,  an::SampleType::Int8,   an::SampleType::UInt16,
                                          an::SampleType::Int16,  an::SampleType::UInt32, an::SampleType::Int32,
                                          an::SampleType::Float64};
    for (const an::SampleType type : kOthers)
    {
        const an::ImageLayout layout =
            layoutOf(4u, 4u, 1u, 1u, an::sampleTypeSizeBytes(type), 4u * an::sampleTypeSizeBytes(type),
                     4u * an::sampleTypeSizeBytes(type) * 4u);
        EXPECT_EQ(an::checkGmicImageGeometry(type, layout), an::BridgeRefusal::SampleTypeNotRepresentable)
            << an::sampleTypeName(type);
    }
}

TEST(GmicImageBridgeTest, AValueOutsideTheSampleEnumerationIsRefusedBeforeAnyOtherRule)
{
    const an::ImageLayout layout = packedFloatLayout(4u, 4u, 1u);
    EXPECT_EQ(an::checkGmicImageGeometry(static_cast<an::SampleType>(99), layout), an::BridgeRefusal::UnknownContract);
}

TEST(GmicImageBridgeTest, TheContractIsJudgedBeforeTheLayout)
{
    std::vector<float> samples(static_cast<std::size_t>(4 * 4), 0.0f);
    // A padded layout *and* a domain disagreement. The claim is reported, because a refusal about
    // what the samples mean is never hidden behind one about how they sit.
    const an::ImageLayout padded = layoutOf(4u, 4u, 1u, 1u, 4u, 4u * 4u + 8u, (4u * 4u + 8u) * 4u);
    std::vector<float> padSamples(static_cast<std::size_t>(padded.totalBytes() / 4u), 0.0f);
    const std::optional<an::ImageView<const float>> image =
        an::ImageView<const float>::create(std::span<const float>(padSamples), padded);
    ASSERT_TRUE(image.has_value());
    an::ExternalBufferContract contract = shareableContract();
    contract.libraryDomain = an::NumericDomain::RawSensorCode;

    EXPECT_EQ(an::checkGmicImage(*image, contract), an::BridgeRefusal::NumericDomainDiffers);
    EXPECT_FALSE(an::describeAsGmicImage(*image, contract).has_value());
}

TEST(GmicImageBridgeTest, ARetainedBorrowOfEngineMemoryIsRefused)
{
    std::vector<float> samples(static_cast<std::size_t>(4 * 4), 0.0f);
    const std::optional<an::ImageView<const float>> image =
        an::ImageView<const float>::create(std::span<const float>(samples), packedFloatLayout(4u, 4u, 1u));
    ASSERT_TRUE(image.has_value());
    an::ExternalBufferContract contract = shareableContract();
    contract.scope = an::BorrowScope::Retained;

    EXPECT_EQ(an::checkGmicImage(*image, contract), an::BridgeRefusal::BorrowOutlivesSamples);
}

TEST(GmicImageBridgeTest, ASizeThatWouldWrapIsCountedAsAbsentRatherThanWrong)
{
    // Four unsigned dimensions multiplied together is the one place this bridge can overflow, and a
    // count that wraps describes a buffer as smaller than it is.
    an::GmicImageDescription huge =
        libraryResult(reinterpret_cast<void*>(std::uintptr_t{16}), 0xffffffffu, 0xffffffffu, 4u);
    EXPECT_FALSE(an::gmicSampleCount(huge).has_value());
    EXPECT_FALSE(an::gmicByteCount(huge).has_value());
    EXPECT_EQ(an::checkGmicImageDescription(huge), an::BridgeRefusal::ExtentExceedsLibraryIndex);

    // One that fits as a sample count and not as a byte count is refused too, which is why the two
    // are counted separately rather than the second derived from the first and hoped about.
    huge.width = 0x80000000u;
    huge.height = 0x80000000u;
    huge.spectrum = 2u;
    EXPECT_EQ(an::gmicSampleCount(huge), std::optional<std::uint64_t>{std::uint64_t{1} << 63u});
    EXPECT_FALSE(an::gmicByteCount(huge).has_value());
    EXPECT_EQ(an::checkGmicImageDescription(huge), an::BridgeRefusal::ExtentExceedsLibraryIndex);
}

TEST(GmicImageBridgeTest, AZeroDimensionIsNotAnImage)
{
    void* base = reinterpret_cast<void*>(std::uintptr_t{16});
    EXPECT_FALSE(an::gmicSampleCount(libraryResult(base, 0u, 4u, 1u)).has_value());
    EXPECT_FALSE(an::gmicSampleCount(libraryResult(base, 4u, 0u, 1u)).has_value());
    EXPECT_FALSE(an::gmicSampleCount(libraryResult(base, 4u, 4u, 0u)).has_value());
    EXPECT_EQ(an::checkGmicImageDescription(libraryResult(base, 0u, 4u, 1u)),
              an::BridgeRefusal::ExtentExceedsLibraryIndex);
}

TEST(GmicImageBridgeTest, AResultTheLibraryChangedInAWayThisEngineCannotCarryIsNamed)
{
    void* base = reinterpret_cast<void*>(std::uintptr_t{16});
    an::GmicImageDescription result = libraryResult(base, 4u, 4u, 1u);
    EXPECT_EQ(an::checkGmicImageDescription(result), an::BridgeRefusal::None);

    result.data = nullptr;
    EXPECT_EQ(an::checkGmicImageDescription(result), an::BridgeRefusal::NoSamples);

    result = libraryResult(base, 4u, 4u, 1u);
    result.format = an::GmicPixelFormat::UInt8;
    EXPECT_EQ(an::checkGmicImageDescription(result), an::BridgeRefusal::SampleTypeNotRepresentable);

    result = libraryResult(base, 4u, 4u, 1u);
    result.format = static_cast<an::GmicPixelFormat>(7);
    EXPECT_EQ(an::checkGmicImageDescription(result), an::BridgeRefusal::UnknownContract);

    result = libraryResult(base, 4u, 4u, 1u);
    result.interleaved = true;
    EXPECT_EQ(an::checkGmicImageDescription(result), an::BridgeRefusal::ArrangementNotRepresentable);

    result = libraryResult(base, 4u, 4u, 1u);
    result.depth = 2u;
    EXPECT_EQ(an::checkGmicImageDescription(result), an::BridgeRefusal::ArrangementNotRepresentable);
}

TEST(GmicImageBridgeTest, ALibraryResultBecomesAPackedPlanarEngineLayout)
{
    void* base = reinterpret_cast<void*>(std::uintptr_t{64});
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageLayout> layout = an::gmicResultLayout(libraryResult(base, 9u, 7u, 3u), limits);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->extent().width(), 9u);
    EXPECT_EQ(layout->extent().height(), 7u);
    EXPECT_EQ(layout->extent().channelCount(), 3u);
    EXPECT_EQ(layout->elementSizeBytes(), 4u);
    EXPECT_EQ(layout->samplesPerPixel(), 1u);
    EXPECT_EQ(layout->planeCount(), 3u);
    EXPECT_EQ(layout->rowStrideBytes(), 9u * 4u);
    EXPECT_EQ(layout->planeStrideBytes(), 9u * 4u * 7u);
    EXPECT_EQ(layout->totalBytes(), 9u * 4u * 7u * 3u);
}

TEST(GmicImageBridgeTest, ALayoutIsRefusedForAResultTheCheckAlreadyRefuses)
{
    void* base = reinterpret_cast<void*>(std::uintptr_t{64});
    const an::EngineLimits limits = an::defaultEngineLimits();
    an::GmicImageDescription result = libraryResult(base, 4u, 4u, 1u);
    result.interleaved = true;
    EXPECT_FALSE(an::gmicResultLayout(result, limits).has_value());

    result = libraryResult(nullptr, 4u, 4u, 1u);
    EXPECT_FALSE(an::gmicResultLayout(result, limits).has_value());
}

TEST(GmicImageBridgeTest, AnExtentOutsideTheLimitsInForceIsRefused)
{
    void* base = reinterpret_cast<void*>(std::uintptr_t{64});
    an::EngineLimits limits = an::defaultEngineLimits();
    limits.maxImageWidth = 8u;
    EXPECT_FALSE(an::gmicResultLayout(libraryResult(base, 9u, 4u, 1u), limits).has_value());
    EXPECT_TRUE(an::gmicResultLayout(libraryResult(base, 8u, 4u, 1u), limits).has_value());
}

TEST(GmicImageBridgeTest, TheRoundTripOfAPackedImageIsTheSameGeometryBack)
{
    std::vector<float> samples(static_cast<std::size_t>(6 * 4 * 2), 0.0f);
    const std::optional<an::ImageView<const float>> image =
        an::ImageView<const float>::create(std::span<const float>(samples), packedFloatLayout(6u, 4u, 2u));
    ASSERT_TRUE(image.has_value());
    const std::optional<an::GmicImageDescription> description = an::describeAsGmicImage(*image, shareableContract());
    ASSERT_TRUE(description.has_value());

    const std::optional<an::ImageLayout> back = an::gmicResultLayout(*description, an::defaultEngineLimits());
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->extent().width(), image->layout().extent().width());
    EXPECT_EQ(back->extent().height(), image->layout().extent().height());
    EXPECT_EQ(back->planeCount(), image->layout().planeCount());
    EXPECT_EQ(back->rowStrideBytes(), image->layout().rowStrideBytes());
    EXPECT_EQ(back->planeStrideBytes(), image->layout().planeStrideBytes());
}
