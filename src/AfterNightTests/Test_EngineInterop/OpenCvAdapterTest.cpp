// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Interop/OpenCvAdapter.h.
//
// Three things are being checked, and they fail in different ways.
//
// The depth table is a closed mapping with two gaps, and the gaps are the point: OpenCV has no
// unsigned 32-bit depth, so `SampleType::UInt32` has no matrix representation and says so. Mapping
// it to `CV_64F` and the signed 8-bit type to `CV_32F` would hand back a matrix holding different
// numbers in a different representation, with nothing at the call site recording it.
//
// The packed type code is OpenCV's ABI, written here from its own headers. The values in these
// cases are the ones `CV_8UC3` and its neighbours expand to under the packing this repository
// builds against, spelled as literals rather than as the same arithmetic the header does - a test
// that recomputed the formula would agree with a wrong formula.
//
// The geometry is where zero-copy is decided. A matrix interleaves its channels within a row and
// this engine stores planes, so a planar multi-channel image is not shareable and an interleaved
// one is; that answer comes from what the two memory models are, and these cases pin it in both
// directions rather than leaving it to be rediscovered by whoever writes the first warp.

#include <AfterNightEngine/Interop/OpenCvAdapter.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
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

constexpr an::OpenCvDepth kAllDepths[] = {
    an::OpenCvDepth::Unsigned8, an::OpenCvDepth::Signed8, an::OpenCvDepth::Unsigned16, an::OpenCvDepth::Signed16,
    an::OpenCvDepth::Signed32,  an::OpenCvDepth::Float32, an::OpenCvDepth::Float64};

/// A contract every shared-buffer rule accepts, so a case can break exactly one thing.
[[nodiscard]] constexpr an::ExternalBufferContract shareableContract() noexcept
{
    return an::ExternalBufferContract{.engineDomain = an::NumericDomain::NormalizedLinear,
                                      .libraryDomain = an::NumericDomain::NormalizedLinear,
                                      .ownership = an::BufferOwnership::EngineOwned,
                                      .scope = an::BorrowScope::SingleCall,
                                      .mutability = an::BufferMutability::ReadOnly};
}

/// A layout for one plane, built the way a caller with an image would build it.
[[nodiscard]] an::ImageLayout planeLayout(std::uint32_t width, std::uint32_t height, std::uint32_t samplesPerPixel,
                                          std::uint32_t elementSizeBytes, std::uint64_t rowStrideBytes)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, samplesPerPixel, limits);
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = elementSizeBytes,
                                         .samplesPerPixel = samplesPerPixel,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStrideBytes, rowStrideBytes * height, limits);
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

TEST(OpenCvAdapterTest, EveryDepthThisEngineMapsRoundTripsThroughTheSampleType)
{
    for (const an::OpenCvDepth depth : kAllDepths)
    {
        const std::optional<an::SampleType> type = an::openCvSampleTypeOf(depth);
        ASSERT_TRUE(type.has_value()) << static_cast<int>(depth);
        const std::optional<an::OpenCvDepth> back = an::openCvDepthOf(*type);
        ASSERT_TRUE(back.has_value()) << an::sampleTypeName(*type);
        EXPECT_EQ(*back, depth) << an::sampleTypeName(*type);
    }
}

TEST(OpenCvAdapterTest, TheOneRepresentationOpenCvHasNoDepthForIsRefusedRatherThanWidened)
{
    for (const an::SampleType type : kAllSampleTypes)
    {
        const std::optional<an::OpenCvDepth> depth = an::openCvDepthOf(type);
        if (type == an::SampleType::UInt32)
        {
            EXPECT_FALSE(depth.has_value()) << an::sampleTypeName(type);
        }
        else
        {
            ASSERT_TRUE(depth.has_value()) << an::sampleTypeName(type);
            EXPECT_EQ(an::sampleTypeSizeBytes(*an::openCvSampleTypeOf(*depth)), an::sampleTypeSizeBytes(type))
                << an::sampleTypeName(type);
        }
    }

    // The answers a widening mapping would give, stated so the difference is on the record: signed
    // 8-bit as CV_32F and unsigned 32-bit as CV_64F, both of which change the representation and the
    // numbers.
    EXPECT_EQ(an::openCvDepthOf(an::SampleType::Int8), an::OpenCvDepth::Signed8);
    EXPECT_NE(an::openCvDepthOf(an::SampleType::Int8), an::OpenCvDepth::Float32);
}

TEST(OpenCvAdapterTest, ThePackedTypeCodeIsTheOneOpenCvsOwnMacroProduces)
{
    // The literals OpenCV's headers expand these to under the packing this repository builds
    // against. Written out rather than recomputed, so a wrong formula cannot agree with itself.
    struct Case
    {
        an::OpenCvDepth depth;
        std::int32_t channels;
        std::int32_t expected;
    };
    constexpr Case kCases[] = {
        {an::OpenCvDepth::Unsigned8, 1, 0}, {an::OpenCvDepth::Unsigned8, 3, 16}, {an::OpenCvDepth::Unsigned8, 4, 24},
        {an::OpenCvDepth::Signed8, 1, 1},   {an::OpenCvDepth::Unsigned16, 1, 2}, {an::OpenCvDepth::Unsigned16, 3, 18},
        {an::OpenCvDepth::Signed16, 1, 3},  {an::OpenCvDepth::Signed32, 1, 4},   {an::OpenCvDepth::Float32, 1, 5},
        {an::OpenCvDepth::Float32, 3, 21},  {an::OpenCvDepth::Float64, 1, 6},    {an::OpenCvDepth::Float64, 2, 14},
    };

    for (const Case& testCase : kCases)
    {
        const std::optional<std::int32_t> code =
            an::openCvTypeCode(testCase.depth, testCase.channels, an::kOpenCvTypeLayout4);
        ASSERT_TRUE(code.has_value()) << testCase.expected;
        EXPECT_EQ(*code, testCase.expected) << testCase.expected;

        EXPECT_EQ(an::openCvDepthOfTypeCode(*code, an::kOpenCvTypeLayout4), testCase.depth) << testCase.expected;
        EXPECT_EQ(an::openCvChannelsOfTypeCode(*code, an::kOpenCvTypeLayout4), testCase.channels) << testCase.expected;
    }
}

TEST(OpenCvAdapterTest, AChannelCountOutsideWhatAMatrixHoldsHasNoCode)
{
    EXPECT_FALSE(an::openCvTypeCode(an::OpenCvDepth::Float32, 0, an::kOpenCvTypeLayout4).has_value());
    EXPECT_FALSE(an::openCvTypeCode(an::OpenCvDepth::Float32, -1, an::kOpenCvTypeLayout4).has_value());
    EXPECT_TRUE(an::openCvTypeCode(an::OpenCvDepth::Float32, 512, an::kOpenCvTypeLayout4).has_value());
    EXPECT_FALSE(an::openCvTypeCode(an::OpenCvDepth::Float32, 513, an::kOpenCvTypeLayout4).has_value());
}

TEST(OpenCvAdapterTest, TheLibrarysOwnConstantsAreCheckedRatherThanAssumed)
{
    // What a consumer with the OpenCV headers passes: CV_8U through CV_64F as its own headers
    // define them, under this repository's OpenCV 4 packing.
    EXPECT_EQ(an::checkOpenCvTypeLayout(an::kOpenCvTypeLayout4, 0, 1, 2, 3, 4, 5, 6), an::BridgeRefusal::None);

    // A build whose depth constants moved. This is the failure the check exists for, and it is a
    // refusal at the bridge rather than a matrix that claims a depth it does not hold.
    EXPECT_EQ(an::checkOpenCvTypeLayout(an::kOpenCvTypeLayout4, 0, 1, 2, 3, 4, 5, 7),
              an::BridgeRefusal::SampleTypeNotRepresentable);

    // And a build whose packing moved, which is what the next major version of OpenCV does.
    constexpr an::OpenCvTypeLayout kFiveBitShift{.channelShift = 5, .depthMask = 31, .maxChannels = 512};
    EXPECT_EQ(an::checkOpenCvTypeLayout(kFiveBitShift, 0, 1, 2, 3, 4, 5, 6), an::BridgeRefusal::None);
    EXPECT_EQ(an::openCvTypeCode(an::OpenCvDepth::Unsigned8, 3, kFiveBitShift), 64);

    constexpr an::OpenCvTypeLayout kImpossible{.channelShift = 0, .depthMask = 7, .maxChannels = 512};
    EXPECT_EQ(an::checkOpenCvTypeLayout(kImpossible, 0, 1, 2, 3, 4, 5, 6),
              an::BridgeRefusal::ArrangementNotRepresentable);
    EXPECT_FALSE(an::isOpenCvTypeLayout(kImpossible));
}

TEST(OpenCvAdapterTest, AMonoPlaneIsSharedExactlyAsItSits)
{
    std::vector<float> samples(4u * 3u, 0.0f);
    const std::optional<an::PlaneView<float>> plane =
        an::PlaneView<float>::create(std::span<float>{samples}, 4u, 3u, 1u, 16u);
    ASSERT_TRUE(plane.has_value());

    an::ExternalBufferContract contract = shareableContract();
    contract.mutability = an::BufferMutability::ReadWrite;

    EXPECT_EQ(an::checkOpenCvMat(*plane, contract, 1u, an::kOpenCvTypeLayout4), an::BridgeRefusal::None);
    const std::optional<an::OpenCvMatDescription> description =
        an::describeAsOpenCvMat(*plane, contract, 1u, an::kOpenCvTypeLayout4);
    ASSERT_TRUE(description.has_value());

    EXPECT_EQ(description->data, samples.data());
    EXPECT_EQ(description->rows, 3);
    EXPECT_EQ(description->cols, 4);
    EXPECT_EQ(description->type, 5);
    EXPECT_EQ(description->stepBytes, std::size_t{16});
}

TEST(OpenCvAdapterTest, APaddedPlaneKeepsItsPaddingRatherThanBeingRepacked)
{
    // A row of four floats in a stride of six. A matrix carries the step, so this is shareable and
    // the description reports the stride the plane actually has - the alternative would be a
    // repack that nothing asked for and no profile shows.
    std::vector<float> samples(6u * 3u, 0.0f);
    const std::optional<an::PlaneView<float>> plane =
        an::PlaneView<float>::create(std::span<float>{samples}, 4u, 3u, 1u, 24u);
    ASSERT_TRUE(plane.has_value());

    const std::optional<an::OpenCvMatDescription> description =
        an::describeAsOpenCvMat(*plane, shareableContract(), 1u, an::kOpenCvTypeLayout4);
    ASSERT_TRUE(description.has_value());
    EXPECT_EQ(description->stepBytes, std::size_t{24});
    EXPECT_EQ(description->cols, 4);
}

TEST(OpenCvAdapterTest, AnInterleavedPlaneIsSharedAndAPlanarImagesPlaneIsNot)
{
    // Three channels interleaved into one plane: exactly what a matrix's memory is.
    std::vector<std::uint8_t> interleaved(4u * 3u * 2u, 0u);
    const std::optional<an::PlaneView<std::uint8_t>> interleavedPlane =
        an::PlaneView<std::uint8_t>::create(std::span<std::uint8_t>{interleaved}, 4u, 2u, 3u, 12u);
    ASSERT_TRUE(interleavedPlane.has_value());

    const std::optional<an::OpenCvMatDescription> asMatrix =
        an::describeAsOpenCvMat(*interleavedPlane, shareableContract(), 3u, an::kOpenCvTypeLayout4);
    ASSERT_TRUE(asMatrix.has_value());
    EXPECT_EQ(asMatrix->type, 16);
    EXPECT_EQ(asMatrix->cols, 4);

    // One plane of a planar three-channel image, offered as three channels. The plane holds one
    // sample per pixel, so a matrix built over it would read three consecutive pixels of one
    // channel as one pixel of three.
    std::vector<std::uint8_t> planar(4u * 2u, 0u);
    const std::optional<an::PlaneView<std::uint8_t>> planarPlane =
        an::PlaneView<std::uint8_t>::create(std::span<std::uint8_t>{planar}, 4u, 2u, 1u, 4u);
    ASSERT_TRUE(planarPlane.has_value());
    EXPECT_EQ(an::checkOpenCvMat(*planarPlane, shareableContract(), 3u, an::kOpenCvTypeLayout4),
              an::BridgeRefusal::ArrangementNotRepresentable);
    EXPECT_FALSE(an::describeAsOpenCvMat(*planarPlane, shareableContract(), 3u, an::kOpenCvTypeLayout4).has_value());
}

TEST(OpenCvAdapterTest, TheRepresentationOpenCvLacksIsRefusedAtTheGeometryToo)
{
    std::vector<std::uint32_t> samples(4u * 2u, 0u);
    const std::optional<an::PlaneView<std::uint32_t>> plane =
        an::PlaneView<std::uint32_t>::create(std::span<std::uint32_t>{samples}, 4u, 2u, 1u, 16u);
    ASSERT_TRUE(plane.has_value());

    EXPECT_EQ(an::checkOpenCvMat(*plane, shareableContract(), 1u, an::kOpenCvTypeLayout4),
              an::BridgeRefusal::SampleTypeNotRepresentable);
    EXPECT_FALSE(an::describeAsOpenCvMat(*plane, shareableContract(), 1u, an::kOpenCvTypeLayout4).has_value());
}

TEST(OpenCvAdapterTest, TheClaimIsRefusedBeforeTheLayoutIs)
{
    // A UInt32 plane offered under a contract whose domains disagree. Both halves refuse; the claim
    // is the one reported, because "this library means something else by these numbers" is a
    // different conversation from "this library has no type for them".
    std::vector<std::uint32_t> samples(4u * 2u, 0u);
    const std::optional<an::PlaneView<std::uint32_t>> plane =
        an::PlaneView<std::uint32_t>::create(std::span<std::uint32_t>{samples}, 4u, 2u, 1u, 16u);
    ASSERT_TRUE(plane.has_value());

    an::ExternalBufferContract contract = shareableContract();
    contract.libraryDomain = an::NumericDomain::RawSensorCode;
    EXPECT_EQ(an::checkOpenCvMat(*plane, contract, 1u, an::kOpenCvTypeLayout4),
              an::BridgeRefusal::NumericDomainDiffers);
}

TEST(OpenCvAdapterTest, AReadOnlyPlaneIsNeverHandedOverForWriting)
{
    std::vector<float> samples(4u * 2u, 0.0f);
    const std::optional<an::PlaneView<float>> writable =
        an::PlaneView<float>::create(std::span<float>{samples}, 4u, 2u, 1u, 16u);
    ASSERT_TRUE(writable.has_value());
    const an::PlaneView<const float> readable = writable->constView();

    an::ExternalBufferContract contract = shareableContract();
    contract.mutability = an::BufferMutability::ReadWrite;

    EXPECT_EQ(an::checkOpenCvMat(*writable, contract, 1u, an::kOpenCvTypeLayout4), an::BridgeRefusal::None);
    EXPECT_EQ(an::checkOpenCvMat(readable, contract, 1u, an::kOpenCvTypeLayout4),
              an::BridgeRefusal::WriteThroughReadOnlyView);

    contract.mutability = an::BufferMutability::ReadOnly;
    EXPECT_EQ(an::checkOpenCvMat(readable, contract, 1u, an::kOpenCvTypeLayout4), an::BridgeRefusal::None);

    // And a read-only plane still produces a description, because `cv::Mat` has no const form: what
    // stops the write is the contract that was checked, which is the whole reason it is stated.
    const std::optional<an::OpenCvMatDescription> description =
        an::describeAsOpenCvMat(readable, contract, 1u, an::kOpenCvTypeLayout4);
    ASSERT_TRUE(description.has_value());
    EXPECT_EQ(description->data, samples.data());
}

TEST(OpenCvAdapterTest, EngineMemoryIsNotLentToAMatrixThatOutlivesIt)
{
    std::vector<float> samples(4u * 2u, 0.0f);
    const std::optional<an::PlaneView<float>> plane =
        an::PlaneView<float>::create(std::span<float>{samples}, 4u, 2u, 1u, 16u);
    ASSERT_TRUE(plane.has_value());

    an::ExternalBufferContract contract = shareableContract();
    contract.scope = an::BorrowScope::Retained;
    EXPECT_EQ(an::checkOpenCvMat(*plane, contract, 1u, an::kOpenCvTypeLayout4),
              an::BridgeRefusal::BorrowOutlivesSamples);
}

TEST(OpenCvAdapterTest, AChannelCountBeyondAMatrixIsNamedRatherThanTruncated)
{
    const an::ImageLayout layout = planeLayout(4u, 2u, 600u, 1u, 4u * 600u);
    EXPECT_EQ(an::checkOpenCvGeometry(an::SampleType::UInt8, layout, 600u, an::kOpenCvTypeLayout4),
              an::BridgeRefusal::ChannelCountNotRepresentable);
}

TEST(OpenCvAdapterTest, AnExtentBeyondWhatAMatrixIndexesIsNamed)
{
    // A width no `int` holds. It needs limits that admit it, which is what makes this reachable at
    // all: the engine's own default maxima stop long before OpenCV's index does.
    const an::EngineLimits wide{.maxImageWidth = 4000000000u,
                                .maxImageHeight = 1000000u,
                                .maxChannelCount = 4096u,
                                .maxImageBytes = 64ull * 1024ull * 1024ull * 1024ull,
                                .memoryBudgetBytes = an::kDefaultMemoryBudgetBytes};
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(3000000000u, 1u, 1u, wide);
    ASSERT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{
        .extent = *extent, .elementSizeBytes = 1u, .samplesPerPixel = 1u, .planeCount = 1u, .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, wide);
    ASSERT_TRUE(layout.has_value());

    EXPECT_EQ(an::checkOpenCvGeometry(an::SampleType::UInt8, *layout, 1u, an::kOpenCvTypeLayout4),
              an::BridgeRefusal::ExtentExceedsLibraryIndex);
}

TEST(OpenCvAdapterTest, AMatrixOfALibrarysOwnIsViewedRatherThanAdopted)
{
    std::vector<std::uint16_t> matrixSamples(8u * 4u, 0u);
    const an::OpenCvMatDescription description{
        .data = matrixSamples.data(), .rows = 4, .cols = 8, .type = 2, .stepBytes = 16u};

    an::ExternalBufferContract contract = shareableContract();
    contract.ownership = an::BufferOwnership::LibraryOwned;
    contract.scope = an::BorrowScope::Retained;

    EXPECT_EQ(an::adoptOpenCvMatCheck(description, contract, an::kOpenCvTypeLayout4), an::BridgeRefusal::None);
    EXPECT_EQ(an::openCvMatSampleType(description, an::kOpenCvTypeLayout4), an::SampleType::UInt16);

    const std::optional<an::ImageLayout> layout =
        an::openCvMatLayout(description, an::kOpenCvTypeLayout4, an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->extent().width(), 8u);
    EXPECT_EQ(layout->extent().height(), 4u);
    EXPECT_EQ(layout->extent().channelCount(), 1u);
    EXPECT_EQ(layout->rowStrideBytes(), 16u);
    EXPECT_EQ(layout->samplesPerPixel(), 1u);
    EXPECT_EQ(layout->planeCount(), 1u);

    // And the view a caller builds from it reaches every sample, which is the whole point of
    // describing the matrix rather than copying it.
    const std::optional<an::PlaneView<std::uint16_t>> view = an::PlaneView<std::uint16_t>::create(
        std::span<std::uint16_t>{matrixSamples}, 8u, 4u, 1u, layout->rowStrideBytes());
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->sampleAt(7u, 3u, 0u), matrixSamples.data() + 31);
}

TEST(OpenCvAdapterTest, AThreeChannelMatrixIsDescribedAsOneInterleavedPlane)
{
    // The copy path made explicit: a matrix this engine will not share is still describable, so the
    // caller views OpenCV's memory and moves the samples through the conversion entry points rather
    // than through a hidden repack.
    std::vector<std::uint8_t> matrixSamples(4u * 3u * 2u, 0u);
    const an::OpenCvMatDescription description{
        .data = matrixSamples.data(), .rows = 2, .cols = 4, .type = 16, .stepBytes = 12u};

    const std::optional<an::ImageLayout> layout =
        an::openCvMatLayout(description, an::kOpenCvTypeLayout4, an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->extent().channelCount(), 3u);
    EXPECT_EQ(layout->samplesPerPixel(), 3u);
    EXPECT_EQ(layout->planeCount(), 1u);
    EXPECT_EQ(an::storageArrangementOf(*layout), an::StorageArrangement::Interleaved);
}

TEST(OpenCvAdapterTest, AStepThatIsNotAWholeNumberOfSamplesIsRefusedByName)
{
    // OpenCV allows it - a submatrix of an odd-width parent has one - and a typed pointer cannot
    // reach the rows after the first, so it is named rather than reported as a stride that was
    // merely too small.
    std::vector<std::uint8_t> matrixSamples(64u, 0u);
    const an::OpenCvMatDescription description{
        .data = matrixSamples.data(), .rows = 2, .cols = 4, .type = 2, .stepBytes = 9u};

    EXPECT_EQ(an::adoptOpenCvMatCheck(description, shareableContract(), an::kOpenCvTypeLayout4),
              an::BridgeRefusal::RowStrideNotRepresentable);
    EXPECT_FALSE(an::openCvMatLayout(description, an::kOpenCvTypeLayout4, an::defaultEngineLimits()).has_value());
}

TEST(OpenCvAdapterTest, AStepShorterThanARowIsRefused)
{
    std::vector<std::uint8_t> matrixSamples(64u, 0u);
    const an::OpenCvMatDescription description{
        .data = matrixSamples.data(), .rows = 2, .cols = 8, .type = 2, .stepBytes = 8u};

    EXPECT_EQ(an::adoptOpenCvMatCheck(description, shareableContract(), an::kOpenCvTypeLayout4),
              an::BridgeRefusal::RowStrideNotRepresentable);
}

TEST(OpenCvAdapterTest, AMatrixWithNoSamplesOrNoRowsIsRefusedByName)
{
    const an::OpenCvMatDescription empty{.data = nullptr, .rows = 2, .cols = 2, .type = 5, .stepBytes = 8u};
    EXPECT_EQ(an::adoptOpenCvMatCheck(empty, shareableContract(), an::kOpenCvTypeLayout4),
              an::BridgeRefusal::NoSamples);

    std::vector<float> samples(4u, 0.0f);
    const an::OpenCvMatDescription noRows{.data = samples.data(), .rows = 0, .cols = 2, .type = 5, .stepBytes = 8u};
    EXPECT_EQ(an::adoptOpenCvMatCheck(noRows, shareableContract(), an::kOpenCvTypeLayout4),
              an::BridgeRefusal::ExtentExceedsLibraryIndex);
}

TEST(OpenCvAdapterTest, ADepthThisEngineDoesNotMapIsRefusedRatherThanGuessed)
{
    // CV_16F, which OpenCV defines and this engine has no sample type for.
    std::vector<std::uint16_t> samples(16u, 0u);
    const an::OpenCvMatDescription half{.data = samples.data(), .rows = 2, .cols = 4, .type = 7, .stepBytes = 8u};

    EXPECT_EQ(an::adoptOpenCvMatCheck(half, shareableContract(), an::kOpenCvTypeLayout4),
              an::BridgeRefusal::SampleTypeNotRepresentable);
    EXPECT_FALSE(an::openCvMatSampleType(half, an::kOpenCvTypeLayout4).has_value());
    EXPECT_FALSE(an::openCvMatLayout(half, an::kOpenCvTypeLayout4, an::defaultEngineLimits()).has_value());
}

// The table and the packing are constant expressions, so a build that got either wrong fails here
// rather than at run time.
static_assert(!an::openCvDepthOf(an::SampleType::UInt32).has_value());
static_assert(an::openCvDepthOf(an::SampleType::Int8) == an::OpenCvDepth::Signed8);
static_assert(an::openCvTypeCode(an::OpenCvDepth::Unsigned8, 3, an::kOpenCvTypeLayout4) == 16);
static_assert(an::checkOpenCvTypeLayout(an::kOpenCvTypeLayout4, 0, 1, 2, 3, 4, 5, 6) == an::BridgeRefusal::None);

} // namespace
