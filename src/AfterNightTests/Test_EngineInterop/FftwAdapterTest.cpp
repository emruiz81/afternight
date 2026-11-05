// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Interop/FftwAdapter.h.
//
// FFTW's memory model is four facts and each of them fails silently when it is wrong: strides are
// counted in elements rather than bytes, dimensions and strides are `int`, a SIMD plan remembers
// the alignment of the arrays it was planned over, and an in-place real-to-complex transform needs
// its real rows padded to `2 * (width / 2 + 1)`.
//
// The padding case is the one worth writing tests for even before there is a transform. A real
// array packed tightly is exactly right for an out-of-place plan and wrong for an in-place one, the
// two differ by at most two columns, and the symptom is a spectrum whose last bins are garbage -
// which reads as a numerical problem rather than as a layout problem. Here it is a stated
// placement, and a tight row under an in-place plan is refused by name.

#include <AfterNightEngine/Interop/FftwAdapter.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Interop/ExternalBuffer.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{

constexpr an::SampleType kAllSampleTypes[] = {an::SampleType::UInt8,   an::SampleType::Int8,   an::SampleType::UInt16,
                                              an::SampleType::Int16,   an::SampleType::UInt32, an::SampleType::Int32,
                                              an::SampleType::Float32, an::SampleType::Float64};

/// A contract every shared-buffer rule accepts, so a case can break exactly one thing.
[[nodiscard]] constexpr an::ExternalBufferContract shareableContract() noexcept
{
    return an::ExternalBufferContract{.engineDomain = an::NumericDomain::NormalizedLinear,
                                      .libraryDomain = an::NumericDomain::NormalizedLinear,
                                      .ownership = an::BufferOwnership::EngineOwned,
                                      .scope = an::BorrowScope::Operation,
                                      .mutability = an::BufferMutability::ReadWrite};
}

/// A layout for one plane of reals, built the way a caller with a scratch buffer would build it.
[[nodiscard]] an::ImageLayout realPlaneLayout(std::uint32_t width, std::uint32_t height, std::uint32_t elementSizeBytes,
                                              std::uint64_t rowStrideBytes)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, limits);
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = elementSizeBytes,
                                         .samplesPerPixel = 1u,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStrideBytes, rowStrideBytes * height, limits);
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

TEST(FftwAdapterTest, OnlyTheFloatingRepresentationsHaveAPrecision)
{
    for (const an::SampleType type : kAllSampleTypes)
    {
        const std::optional<an::FftwPrecision> precision = an::fftwPrecisionOf(type);
        if (type == an::SampleType::Float32 || type == an::SampleType::Float64)
        {
            ASSERT_TRUE(precision.has_value()) << an::sampleTypeName(type);
            EXPECT_EQ(an::fftwSampleTypeOf(*precision), type) << an::sampleTypeName(type);
        }
        else
        {
            EXPECT_FALSE(precision.has_value()) << an::sampleTypeName(type);
        }
    }
}

TEST(FftwAdapterTest, TheHalfSpectrumIsHalfTheWidthPlusTheZeroFrequency)
{
    EXPECT_EQ(an::fftwHermitianColumns(0u), 0u);
    EXPECT_EQ(an::fftwHermitianColumns(1u), 1u);
    EXPECT_EQ(an::fftwHermitianColumns(2u), 2u);
    EXPECT_EQ(an::fftwHermitianColumns(3u), 2u);
    EXPECT_EQ(an::fftwHermitianColumns(4u), 3u);
    EXPECT_EQ(an::fftwHermitianColumns(8u), 5u);
    EXPECT_EQ(an::fftwHermitianColumns(9u), 5u);
}

TEST(FftwAdapterTest, AnInPlaceRealRowIsPaddedAndAnOutOfPlaceOneIsNot)
{
    // The two columns that are the whole of the difference, at an even width and an odd one.
    EXPECT_EQ(an::fftwRealRowReals(8u, an::FftwPlacement::OutOfPlace), 8u);
    EXPECT_EQ(an::fftwRealRowReals(8u, an::FftwPlacement::InPlace), 10u);
    EXPECT_EQ(an::fftwRealRowReals(9u, an::FftwPlacement::OutOfPlace), 9u);
    EXPECT_EQ(an::fftwRealRowReals(9u, an::FftwPlacement::InPlace), 10u);
}

TEST(FftwAdapterTest, ATightRowIsRefusedForAnInPlaceTransformAndAcceptedForAnOutOfPlaceOne)
{
    const an::ImageLayout tight = realPlaneLayout(8u, 4u, 4u, 32u);
    EXPECT_EQ(an::checkFftwGeometry(an::SampleType::Float32, tight, an::FftwPlacement::OutOfPlace),
              an::BridgeRefusal::None);
    EXPECT_EQ(an::checkFftwGeometry(an::SampleType::Float32, tight, an::FftwPlacement::InPlace),
              an::BridgeRefusal::ArrangementNotRepresentable);

    // The same plane with the two reals an in-place plan needs.
    const an::ImageLayout padded = realPlaneLayout(8u, 4u, 4u, 40u);
    EXPECT_EQ(an::checkFftwGeometry(an::SampleType::Float32, padded, an::FftwPlacement::InPlace),
              an::BridgeRefusal::None);
}

TEST(FftwAdapterTest, AnIntegerPlaneHasNoTransformAndSaysSo)
{
    const an::ImageLayout layout = realPlaneLayout(8u, 4u, 2u, 16u);
    EXPECT_EQ(an::checkFftwGeometry(an::SampleType::UInt16, layout, an::FftwPlacement::OutOfPlace),
              an::BridgeRefusal::SampleTypeNotRepresentable);
}

TEST(FftwAdapterTest, APlaneWhosePixelsHoldMoreThanOneSampleIsNotASignal)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(8u, 4u, 3u, limits);
    ASSERT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{
        .extent = *extent, .elementSizeBytes = 4u, .samplesPerPixel = 3u, .planeCount = 1u, .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, limits);
    ASSERT_TRUE(layout.has_value());

    EXPECT_EQ(an::checkFftwGeometry(an::SampleType::Float32, *layout, an::FftwPlacement::OutOfPlace),
              an::BridgeRefusal::ArrangementNotRepresentable);
}

TEST(FftwAdapterTest, APlaneIsDescribedWithItsStrideCountedInReals)
{
    // Sixteen doubles of stride around eight of data: the description reports two, in elements,
    // because that is the number FFTW's advanced interface takes.
    std::vector<double> samples(16u * 4u, 0.0);
    const std::optional<an::PlaneView<double>> plane =
        an::PlaneView<double>::create(std::span<double>{samples}, 8u, 4u, 1u, 128u);
    ASSERT_TRUE(plane.has_value());

    const std::optional<an::FftwArrayDescription> description =
        an::describeAsFftwReal(*plane, shareableContract(), an::FftwPlacement::OutOfPlace, 1u);
    ASSERT_TRUE(description.has_value());
    EXPECT_EQ(description->data, samples.data());
    EXPECT_EQ(description->precision, an::FftwPrecision::Double);
    EXPECT_EQ(description->kind, an::FftwArrayKind::Real);
    EXPECT_EQ(description->n0, 4);
    EXPECT_EQ(description->n1, 8);
    EXPECT_EQ(description->rowStrideReals, 16);
    EXPECT_TRUE(description->alignmentSatisfied);
}

TEST(FftwAdapterTest, AComplexPlaneIsHalfAsManyElementsAsItIsReals)
{
    std::vector<float> samples(10u * 4u, 0.0f);
    const std::optional<an::PlaneView<float>> plane =
        an::PlaneView<float>::create(std::span<float>{samples}, 10u, 4u, 1u, 40u);
    ASSERT_TRUE(plane.has_value());

    const std::optional<an::FftwArrayDescription> description =
        an::describeAsFftwComplex(*plane, shareableContract(), 1u);
    ASSERT_TRUE(description.has_value());
    EXPECT_EQ(description->kind, an::FftwArrayKind::InterleavedComplex);
    EXPECT_EQ(description->n1, 5);
    EXPECT_EQ(description->rowStrideReals, 10);
    EXPECT_EQ(description->precision, an::FftwPrecision::Single);

    // The half-spectrum of an eight-point real transform, which is what a plane of ten reals holds.
    EXPECT_EQ(static_cast<std::uint32_t>(description->n1), an::fftwHermitianColumns(8u));
}

TEST(FftwAdapterTest, AnOddNumberOfRealsIsNoComplexArrayAtAll)
{
    std::vector<float> samples(9u * 2u, 0.0f);
    const std::optional<an::PlaneView<float>> plane =
        an::PlaneView<float>::create(std::span<float>{samples}, 9u, 2u, 1u, 36u);
    ASSERT_TRUE(plane.has_value());

    EXPECT_FALSE(an::describeAsFftwComplex(*plane, shareableContract(), 1u).has_value());
}

TEST(FftwAdapterTest, AnUnalignedBaseIsRefusedWhenThePlanRequiresAlignment)
{
    // One double past the start of an over-aligned buffer, which is eight bytes and therefore not
    // the sixteen a SIMD plan was created for.
    alignas(32) double storage[8u * 4u + 1u] = {};
    const std::span<double> offsetSamples{storage + 1, 8u * 4u};
    const std::optional<an::PlaneView<double>> plane = an::PlaneView<double>::create(offsetSamples, 8u, 4u, 1u, 64u);
    ASSERT_TRUE(plane.has_value());
    ASSERT_FALSE(an::isPointerAligned(plane->data(), an::kFftwDefaultAlignmentBytes));

    EXPECT_EQ(
        an::checkFftwArray(*plane, shareableContract(), an::FftwPlacement::OutOfPlace, an::kFftwDefaultAlignmentBytes),
        an::BridgeRefusal::AlignmentInsufficient);

    // The same plane under a plan created with FFTW_UNALIGNED, which asks for nothing.
    EXPECT_EQ(an::checkFftwArray(*plane, shareableContract(), an::FftwPlacement::OutOfPlace, 1u),
              an::BridgeRefusal::None);
}

TEST(FftwAdapterTest, AnAlignedBaseSatisfiesThePlanItWasCreatedFor)
{
    alignas(32) double storage[8u * 4u] = {};
    const std::optional<an::PlaneView<double>> plane =
        an::PlaneView<double>::create(std::span<double>{storage}, 8u, 4u, 1u, 64u);
    ASSERT_TRUE(plane.has_value());

    EXPECT_EQ(
        an::checkFftwArray(*plane, shareableContract(), an::FftwPlacement::OutOfPlace, an::kFftwDefaultAlignmentBytes),
        an::BridgeRefusal::None);
    EXPECT_TRUE(an::describeAsFftwReal(*plane, shareableContract(), an::FftwPlacement::OutOfPlace,
                                       an::kFftwDefaultAlignmentBytes)
                    .has_value());
}

TEST(FftwAdapterTest, AnAlignmentThatIsNotAPowerOfTwoIsRefusedRatherThanRounded)
{
    alignas(32) double storage[8u * 4u] = {};
    const std::optional<an::PlaneView<double>> plane =
        an::PlaneView<double>::create(std::span<double>{storage}, 8u, 4u, 1u, 64u);
    ASSERT_TRUE(plane.has_value());

    EXPECT_EQ(an::checkFftwArray(*plane, shareableContract(), an::FftwPlacement::OutOfPlace, 24u),
              an::BridgeRefusal::AlignmentInsufficient);
}

TEST(FftwAdapterTest, TheClaimIsRefusedBeforeTheAlignmentIs)
{
    alignas(32) double storage[8u * 4u] = {};
    const std::optional<an::PlaneView<double>> plane =
        an::PlaneView<double>::create(std::span<double>{storage}, 8u, 4u, 1u, 64u);
    ASSERT_TRUE(plane.has_value());

    an::ExternalBufferContract contract = shareableContract();
    contract.libraryDomain = an::NumericDomain::EncodedNonlinear;
    EXPECT_EQ(an::checkFftwArray(*plane, contract, an::FftwPlacement::OutOfPlace, 24u),
              an::BridgeRefusal::NumericDomainDiffers);
}

TEST(FftwAdapterTest, AnArrayThePlanOwnsIsViewedRatherThanAdopted)
{
    std::vector<double> planSamples(20u * 4u, 0.0);
    const an::FftwArrayDescription description{.data = planSamples.data(),
                                               .precision = an::FftwPrecision::Double,
                                               .kind = an::FftwArrayKind::InterleavedComplex,
                                               .n0 = 4,
                                               .n1 = 5,
                                               .rowStrideReals = 20,
                                               .alignmentSatisfied = true};

    an::ExternalBufferContract contract = shareableContract();
    contract.ownership = an::BufferOwnership::LibraryOwned;
    contract.scope = an::BorrowScope::Retained;
    EXPECT_EQ(an::adoptFftwArrayCheck(description, contract), an::BridgeRefusal::None);

    const std::optional<an::ImageLayout> layout = an::fftwArrayLayout(description, an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());

    // Ten reals of data, twenty of stride: a view over a complex array describes its reals, because
    // that is what a typed pointer can reach.
    EXPECT_EQ(layout->extent().width(), 10u);
    EXPECT_EQ(layout->extent().height(), 4u);
    EXPECT_EQ(layout->rowStrideBytes(), 160u);
    EXPECT_EQ(layout->elementSizeBytes(), 8u);
}

TEST(FftwAdapterTest, ARowStrideShorterThanTheRowIsRefusedByName)
{
    std::vector<double> planSamples(64u, 0.0);
    an::FftwArrayDescription description{.data = planSamples.data(),
                                         .precision = an::FftwPrecision::Double,
                                         .kind = an::FftwArrayKind::InterleavedComplex,
                                         .n0 = 4,
                                         .n1 = 5,
                                         .rowStrideReals = 9,
                                         .alignmentSatisfied = true};

    EXPECT_EQ(an::adoptFftwArrayCheck(description, shareableContract()), an::BridgeRefusal::RowStrideNotRepresentable);
    EXPECT_FALSE(an::fftwArrayLayout(description, an::defaultEngineLimits()).has_value());

    description.rowStrideReals = 10;
    EXPECT_EQ(an::adoptFftwArrayCheck(description, shareableContract()), an::BridgeRefusal::None);
}

TEST(FftwAdapterTest, AnArrayThatDoesNotMeetItsOwnPlansAlignmentIsRefused)
{
    std::vector<double> planSamples(64u, 0.0);
    const an::FftwArrayDescription description{.data = planSamples.data(),
                                               .precision = an::FftwPrecision::Double,
                                               .kind = an::FftwArrayKind::Real,
                                               .n0 = 4,
                                               .n1 = 8,
                                               .rowStrideReals = 8,
                                               .alignmentSatisfied = false};

    EXPECT_EQ(an::adoptFftwArrayCheck(description, shareableContract()), an::BridgeRefusal::AlignmentInsufficient);
}

TEST(FftwAdapterTest, AnArrayWithNoDataOrNoDimensionIsRefusedByName)
{
    const an::FftwArrayDescription empty{.data = nullptr,
                                         .precision = an::FftwPrecision::Double,
                                         .kind = an::FftwArrayKind::Real,
                                         .n0 = 4,
                                         .n1 = 8,
                                         .rowStrideReals = 8,
                                         .alignmentSatisfied = true};
    EXPECT_EQ(an::adoptFftwArrayCheck(empty, shareableContract()), an::BridgeRefusal::NoSamples);

    std::vector<double> planSamples(64u, 0.0);
    const an::FftwArrayDescription flat{.data = planSamples.data(),
                                        .precision = an::FftwPrecision::Double,
                                        .kind = an::FftwArrayKind::Real,
                                        .n0 = 0,
                                        .n1 = 8,
                                        .rowStrideReals = 8,
                                        .alignmentSatisfied = true};
    EXPECT_EQ(an::adoptFftwArrayCheck(flat, shareableContract()), an::BridgeRefusal::ExtentExceedsLibraryIndex);
}

TEST(FftwAdapterTest, MembershipTestsCoverExactlyTheEnumerators)
{
    EXPECT_TRUE(an::isFftwPrecision(an::FftwPrecision::Single));
    EXPECT_TRUE(an::isFftwPrecision(an::FftwPrecision::Double));
    EXPECT_FALSE(an::isFftwPrecision(static_cast<an::FftwPrecision>(2)));

    EXPECT_TRUE(an::isFftwArrayKind(an::FftwArrayKind::InterleavedComplex));
    EXPECT_FALSE(an::isFftwArrayKind(static_cast<an::FftwArrayKind>(2)));

    EXPECT_TRUE(an::isFftwPlacement(an::FftwPlacement::InPlace));
    EXPECT_FALSE(an::isFftwPlacement(static_cast<an::FftwPlacement>(2)));
}

// The padding rule is a constant expression, so a build that got it wrong fails here rather than in
// the last two bins of somebody's spectrum.
static_assert(an::fftwRealRowReals(8u, an::FftwPlacement::InPlace) == 10u);
static_assert(an::fftwRealRowReals(8u, an::FftwPlacement::OutOfPlace) == 8u);
static_assert(an::fftwHermitianColumns(9u) == 5u);
static_assert(!an::fftwPrecisionOf(an::SampleType::Int32).has_value());

} // namespace
