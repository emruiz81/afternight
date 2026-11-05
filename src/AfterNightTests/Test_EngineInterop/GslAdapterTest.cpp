// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Interop/GslAdapter.h.
//
// A `gsl_matrix` is row-major floating point with a trailing dimension counted in elements, and
// three of its five fields are the ones a bridge can get wrong without anything saying so: `size1`
// and `size2` are rows and columns in that order, and `tda` is a stride in elements rather than in
// bytes. Swapping the first two produces a matrix that is valid, describes the wrong pixels, and
// only looks wrong once somebody plots the result; using bytes for the third produces one that
// reads eight times past the end of every row.
//
// The fields that are deliberately absent are the fourth thing. `gsl_matrix::block` and
// `gsl_matrix::owner` are what `gsl_matrix_free` releases through, so a matrix built over engine
// memory with either set is a free of memory the engine still holds. The description this engine
// produces has neither, which is what `gsl_matrix_view_array_with_tda` also produces - and the
// ownership half of the contract is what refuses the case where a caller wanted GSL to keep the
// samples.

#include <AfterNightEngine/Interop/GslAdapter.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Interop/ExternalBuffer.h>

#include <gtest/gtest.h>

#include <cstddef>
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

TEST(GslAdapterTest, OnlyTheFloatingRepresentationsHaveAMatrixFamily)
{
    for (const an::SampleType type : kAllSampleTypes)
    {
        const std::optional<an::GslPrecision> precision = an::gslPrecisionOf(type);
        if (type == an::SampleType::Float32 || type == an::SampleType::Float64)
        {
            ASSERT_TRUE(precision.has_value()) << an::sampleTypeName(type);
            EXPECT_EQ(an::gslSampleTypeOf(*precision), type) << an::sampleTypeName(type);
        }
        else
        {
            EXPECT_FALSE(precision.has_value()) << an::sampleTypeName(type);
        }
    }
}

TEST(GslAdapterTest, RowsAndColumnsAreTheWayRoundTheStructureSaysTheyAre)
{
    // Eight wide and three high, described. `size1` is rows and `size2` is columns, and getting the
    // pair the wrong way round is the mistake this case exists to catch on every build.
    std::vector<double> samples(8u * 3u, 0.0);
    const std::optional<an::PlaneView<double>> plane =
        an::PlaneView<double>::create(std::span<double>{samples}, 8u, 3u, 1u, 64u);
    ASSERT_TRUE(plane.has_value());

    const std::optional<an::GslMatrixDescription> description = an::describeAsGslMatrix(*plane, shareableContract());
    ASSERT_TRUE(description.has_value());
    EXPECT_EQ(description->data, samples.data());
    EXPECT_EQ(description->precision, an::GslPrecision::Double);
    EXPECT_EQ(description->size1, std::size_t{3});
    EXPECT_EQ(description->size2, std::size_t{8});
    EXPECT_EQ(description->tda, std::size_t{8});
}

TEST(GslAdapterTest, TheTrailingDimensionIsCountedInElementsRatherThanBytes)
{
    // Twelve doubles of stride around eight of data: `tda` is twelve, not ninety-six.
    std::vector<double> samples(12u * 3u, 0.0);
    const std::optional<an::PlaneView<double>> plane =
        an::PlaneView<double>::create(std::span<double>{samples}, 8u, 3u, 1u, 96u);
    ASSERT_TRUE(plane.has_value());

    const std::optional<an::GslMatrixDescription> description = an::describeAsGslMatrix(*plane, shareableContract());
    ASSERT_TRUE(description.has_value());
    EXPECT_EQ(description->tda, std::size_t{12});
    EXPECT_EQ(description->size2, std::size_t{8});
    EXPECT_GT(description->tda, description->size2);
}

TEST(GslAdapterTest, ASingleFamilyMatrixIsDescribedTheSameWay)
{
    std::vector<float> samples(4u * 2u, 0.0f);
    const std::optional<an::PlaneView<float>> plane =
        an::PlaneView<float>::create(std::span<float>{samples}, 4u, 2u, 1u, 16u);
    ASSERT_TRUE(plane.has_value());

    const std::optional<an::GslMatrixDescription> description = an::describeAsGslMatrix(*plane, shareableContract());
    ASSERT_TRUE(description.has_value());
    EXPECT_EQ(description->precision, an::GslPrecision::Single);
    EXPECT_EQ(description->tda, std::size_t{4});
}

TEST(GslAdapterTest, AnIntegerPlaneHasNoMatrixFamilyAndSaysSo)
{
    std::vector<std::uint16_t> samples(8u * 3u, 0u);
    const std::optional<an::PlaneView<std::uint16_t>> plane =
        an::PlaneView<std::uint16_t>::create(std::span<std::uint16_t>{samples}, 8u, 3u, 1u, 16u);
    ASSERT_TRUE(plane.has_value());

    EXPECT_EQ(an::checkGslMatrix(*plane, shareableContract()), an::BridgeRefusal::SampleTypeNotRepresentable);
    EXPECT_FALSE(an::describeAsGslMatrix(*plane, shareableContract()).has_value());
}

TEST(GslAdapterTest, APlaneWhosePixelsHoldMoreThanOneSampleIsNoMatrix)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(4u, 2u, 3u, limits);
    ASSERT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{
        .extent = *extent, .elementSizeBytes = 8u, .samplesPerPixel = 3u, .planeCount = 1u, .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, limits);
    ASSERT_TRUE(layout.has_value());

    EXPECT_EQ(an::checkGslGeometry(an::SampleType::Float64, *layout), an::BridgeRefusal::ArrangementNotRepresentable);
}

TEST(GslAdapterTest, EngineMemoryIsNotLentToAMatrixThatWouldOutliveIt)
{
    std::vector<double> samples(8u * 3u, 0.0);
    const std::optional<an::PlaneView<double>> plane =
        an::PlaneView<double>::create(std::span<double>{samples}, 8u, 3u, 1u, 64u);
    ASSERT_TRUE(plane.has_value());

    an::ExternalBufferContract contract = shareableContract();
    contract.scope = an::BorrowScope::Retained;
    EXPECT_EQ(an::checkGslMatrix(*plane, contract), an::BridgeRefusal::BorrowOutlivesSamples);
    EXPECT_FALSE(an::describeAsGslMatrix(*plane, contract).has_value());
}

TEST(GslAdapterTest, AReadOnlyPlaneIsNeverHandedOverForWriting)
{
    std::vector<double> samples(8u * 3u, 0.0);
    const std::optional<an::PlaneView<double>> writable =
        an::PlaneView<double>::create(std::span<double>{samples}, 8u, 3u, 1u, 64u);
    ASSERT_TRUE(writable.has_value());
    const an::PlaneView<const double> readable = writable->constView();

    EXPECT_EQ(an::checkGslMatrix(readable, shareableContract()), an::BridgeRefusal::WriteThroughReadOnlyView);

    an::ExternalBufferContract readOnly = shareableContract();
    readOnly.mutability = an::BufferMutability::ReadOnly;
    EXPECT_EQ(an::checkGslMatrix(readable, readOnly), an::BridgeRefusal::None);
}

TEST(GslAdapterTest, ARowIsDescribedAsAContiguousVector)
{
    std::vector<double> samples(12u * 3u, 0.0);
    const std::optional<an::PlaneView<double>> plane =
        an::PlaneView<double>::create(std::span<double>{samples}, 8u, 3u, 1u, 96u);
    ASSERT_TRUE(plane.has_value());

    const std::optional<an::GslVectorDescription> row = an::describeRowAsGslVector(*plane, shareableContract(), 2u);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ(row->data, samples.data() + 24);
    EXPECT_EQ(row->size, std::size_t{8});
    EXPECT_EQ(row->stride, std::size_t{1});
    EXPECT_EQ(row->precision, an::GslPrecision::Double);

    // A row outside the plane is not a vector at all.
    EXPECT_FALSE(an::describeRowAsGslVector(*plane, shareableContract(), 3u).has_value());
}

TEST(GslAdapterTest, AMatrixOfGslsOwnIsViewedRatherThanAdopted)
{
    std::vector<double> matrixSamples(12u * 3u, 0.0);
    const an::GslMatrixDescription description{
        .data = matrixSamples.data(), .precision = an::GslPrecision::Double, .size1 = 3u, .size2 = 8u, .tda = 12u};

    an::ExternalBufferContract contract = shareableContract();
    contract.ownership = an::BufferOwnership::LibraryOwned;
    contract.scope = an::BorrowScope::Retained;
    EXPECT_EQ(an::adoptGslMatrixCheck(description, contract), an::BridgeRefusal::None);

    const std::optional<an::ImageLayout> layout = an::gslMatrixLayout(description, an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->extent().width(), 8u);
    EXPECT_EQ(layout->extent().height(), 3u);
    EXPECT_EQ(layout->rowStrideBytes(), 96u);
    EXPECT_EQ(layout->elementSizeBytes(), 8u);
    EXPECT_EQ(layout->samplesPerPixel(), 1u);

    // And the view a caller builds from it reaches the last sample of the last row, which is what
    // makes a trailing dimension worth carrying rather than assuming.
    const std::optional<an::PlaneView<double>> view =
        an::PlaneView<double>::create(std::span<double>{matrixSamples}, 8u, 3u, 1u, layout->rowStrideBytes());
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->sampleAt(7u, 2u, 0u), matrixSamples.data() + 31);
}

TEST(GslAdapterTest, ATrailingDimensionShorterThanARowIsRefusedByName)
{
    std::vector<double> matrixSamples(64u, 0.0);
    const an::GslMatrixDescription description{
        .data = matrixSamples.data(), .precision = an::GslPrecision::Double, .size1 = 3u, .size2 = 8u, .tda = 7u};

    EXPECT_EQ(an::adoptGslMatrixCheck(description, shareableContract()), an::BridgeRefusal::RowStrideNotRepresentable);
    EXPECT_FALSE(an::gslMatrixLayout(description, an::defaultEngineLimits()).has_value());
}

TEST(GslAdapterTest, AMatrixWithNoDataOrNoDimensionIsRefusedByName)
{
    const an::GslMatrixDescription empty{
        .data = nullptr, .precision = an::GslPrecision::Double, .size1 = 3u, .size2 = 8u, .tda = 8u};
    EXPECT_EQ(an::adoptGslMatrixCheck(empty, shareableContract()), an::BridgeRefusal::NoSamples);

    std::vector<double> matrixSamples(64u, 0.0);
    const an::GslMatrixDescription flat{
        .data = matrixSamples.data(), .precision = an::GslPrecision::Double, .size1 = 0u, .size2 = 8u, .tda = 8u};
    EXPECT_EQ(an::adoptGslMatrixCheck(flat, shareableContract()), an::BridgeRefusal::ExtentExceedsLibraryIndex);
    EXPECT_FALSE(an::gslMatrixLayout(flat, an::defaultEngineLimits()).has_value());
}

TEST(GslAdapterTest, TheClaimIsRefusedBeforeTheStructureIs)
{
    const an::GslMatrixDescription broken{
        .data = nullptr, .precision = an::GslPrecision::Double, .size1 = 0u, .size2 = 0u, .tda = 0u};

    an::ExternalBufferContract contract = shareableContract();
    contract.libraryDomain = an::NumericDomain::PhysicalScaled;
    EXPECT_EQ(an::adoptGslMatrixCheck(broken, contract), an::BridgeRefusal::NumericDomainDiffers);
}

TEST(GslAdapterTest, MembershipTestsCoverExactlyTheEnumerators)
{
    EXPECT_TRUE(an::isGslPrecision(an::GslPrecision::Single));
    EXPECT_TRUE(an::isGslPrecision(an::GslPrecision::Double));
    EXPECT_FALSE(an::isGslPrecision(static_cast<an::GslPrecision>(2)));
}

// The family table is a constant expression, so a build that got it wrong fails here rather than in
// a fit that silently read integers as doubles.
static_assert(an::gslPrecisionOf(an::SampleType::Float64) == an::GslPrecision::Double);
static_assert(!an::gslPrecisionOf(an::SampleType::UInt32).has_value());

} // namespace
