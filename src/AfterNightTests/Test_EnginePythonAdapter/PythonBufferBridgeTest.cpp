// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEnginePythonAdapter/PythonBufferBridge.h.
//
// Every case here runs in both configurations of the adapter, because none of them needs an
// interpreter: the bridge is a set of decisions about two memory models, and both models are
// described rather than constructed. That is the property being pinned as much as the answers are -
// a bridge that needed Python to decide whether a buffer is shareable could not answer before
// starting one.
//
// Three groups of cases, and they fail differently.
//
// The format table is a closed two-way mapping. Python's alphabet is larger than this engine's eight
// representations and its spellings carry byte order, so what matters is which codes are refused:
// `<f` and `>f` name the same width as `f` and a buffer that states its order is a buffer that may
// state the other one.
//
// The description is where zero-copy is decided, and Python is the one library in this engine's
// adapters where planar storage shares without copying. The cases pin the axis convention in both
// arrangements, because a shape that reads plausibly and means something else is exactly the failure
// a bridge produces.
//
// The reverse direction is where the ambiguity lives. A three-axis buffer does not say whether its
// first axis is planes or rows, so the caller states it; a case for each reading pins that, and one
// more pins that the same numbers really do describe two different images.

#include <AfterNightEnginePythonAdapter/PythonBufferBridge.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Interop/ExternalBuffer.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace
{

constexpr an::SampleType kAllSampleTypes[] = {an::SampleType::UInt8,   an::SampleType::Int8,   an::SampleType::UInt16,
                                              an::SampleType::Int16,   an::SampleType::UInt32, an::SampleType::Int32,
                                              an::SampleType::Float32, an::SampleType::Float64};

constexpr an::PythonBufferFormat kAllFormats[] = {an::PythonBufferFormat::UInt8,   an::PythonBufferFormat::Int8,
                                                  an::PythonBufferFormat::UInt16,  an::PythonBufferFormat::Int16,
                                                  an::PythonBufferFormat::UInt32,  an::PythonBufferFormat::Int32,
                                                  an::PythonBufferFormat::Float32, an::PythonBufferFormat::Float64};

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
                                       std::uint64_t rowStrideBytes)
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
        an::ImageLayout::createWithStrides(request, rowStrideBytes, rowStrideBytes * height, limits);
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// A description a caller could have received from Python, valid until a case breaks one field.
[[nodiscard]] an::PythonBufferDescription foreignBuffer(void* data, std::int64_t height, std::int64_t width)
{
    return an::PythonBufferDescription{.data = data,
                                       .lengthBytes = height * width * 4,
                                       .itemSizeBytes = 4,
                                       .axes = 2,
                                       .shape = {height, width, 0},
                                       .strides = {width * 4, 4, 0},
                                       .format = an::PythonBufferFormat::Float32,
                                       .readOnly = false};
}

} // namespace

TEST(PythonBufferBridgeTest, EveryResidentRepresentationHasAFormatAndRoundTrips)
{
    for (const an::SampleType type : kAllSampleTypes)
    {
        const std::optional<an::PythonBufferFormat> format = an::pythonBufferFormatOf(type);
        ASSERT_TRUE(format.has_value()) << an::sampleTypeName(type);
        const std::optional<an::SampleType> back = an::pythonSampleTypeOf(*format);
        ASSERT_TRUE(back.has_value()) << an::sampleTypeName(type);
        EXPECT_EQ(*back, type) << an::sampleTypeName(type);
        EXPECT_EQ(an::pythonBufferFormatOfCode(an::pythonBufferFormatCode(*format)), format)
            << an::sampleTypeName(type);
    }
}

TEST(PythonBufferBridgeTest, TheEightNativeCodesAreTheOnlyOnesAccepted)
{
    for (const an::PythonBufferFormat format : kAllFormats)
    {
        const std::string_view code = an::pythonBufferFormatCode(format);
        EXPECT_EQ(code.size(), 1u) << code;
    }

    // A byte-order or alignment character makes the code a statement about order rather than only
    // about width, and this engine shares native-order buffers only.
    for (const std::string_view spelled : {"<f", ">f", "=i", "!d", "@B", "<H"})
    {
        EXPECT_FALSE(an::pythonBufferFormatOfCode(spelled).has_value()) << spelled;
    }

    // Real Python buffers this engine has no representation for: a boolean, a half, a 64-bit
    // integer, a size-typed integer, a complex pair, and a fixed-length byte string.
    for (const std::string_view unrepresentable : {"?", "e", "q", "Q", "n", "N", "Zf", "4s", "", "ff"})
    {
        EXPECT_FALSE(an::pythonBufferFormatOfCode(unrepresentable).has_value()) << unrepresentable;
    }
}

TEST(PythonBufferBridgeTest, AMonochromeImageIsDescribedAsTwoAxesAndKeepsItsPadding)
{
    // Padded rows, because that is what an engine image usually has and what a length computed from
    // the addressed extent would get wrong: the protocol's length is the logical sample bytes.
    const std::uint32_t width = 5u;
    const std::uint32_t height = 3u;
    const std::uint64_t rowStride = 32u;
    std::vector<float> samples(rowStride / sizeof(float) * height, 0.0f);
    const an::ImageLayout layout = layoutOf(width, height, 1u, 1u, sizeof(float), rowStride);
    const std::optional<an::ImageView<const float>> image =
        an::ImageView<const float>::create(std::span<const float>(samples), layout);
    ASSERT_TRUE(image.has_value());

    const std::optional<an::PythonBufferDescription> description =
        an::describeAsPythonBuffer(*image, shareableContract());
    ASSERT_TRUE(description.has_value());

    EXPECT_EQ(description->data, samples.data());
    EXPECT_EQ(description->axes, 2);
    EXPECT_EQ(description->itemSizeBytes, 4);
    EXPECT_EQ(description->format, an::PythonBufferFormat::Float32);
    EXPECT_TRUE(description->readOnly);
    EXPECT_EQ(description->shape[0], height);
    EXPECT_EQ(description->shape[1], width);
    EXPECT_EQ(description->shape[2], 0);
    EXPECT_EQ(description->strides[0], static_cast<std::int64_t>(rowStride));
    EXPECT_EQ(description->strides[1], 4);
    EXPECT_EQ(description->strides[2], 0);
    EXPECT_EQ(description->lengthBytes, static_cast<std::int64_t>(width) * height * 4);
}

TEST(PythonBufferBridgeTest, PlanarStorageSharesWithoutCopyingAsPlaneRowColumn)
{
    const std::uint32_t width = 4u;
    const std::uint32_t height = 2u;
    const std::uint32_t planes = 3u;
    const std::uint64_t rowStride = 24u;
    std::vector<float> samples(rowStride / sizeof(float) * height * planes, 0.0f);
    const an::ImageLayout layout = layoutOf(width, height, 1u, planes, sizeof(float), rowStride);
    const std::optional<an::ImageView<float>> image = an::ImageView<float>::create(std::span<float>(samples), layout);
    ASSERT_TRUE(image.has_value());

    const std::optional<an::PythonBufferDescription> description =
        an::describeAsPythonBuffer(*image, shareableContract(an::BufferMutability::ReadWrite));
    ASSERT_TRUE(description.has_value());

    EXPECT_EQ(description->axes, 3);
    EXPECT_FALSE(description->readOnly);
    EXPECT_EQ(description->shape[0], planes);
    EXPECT_EQ(description->shape[1], height);
    EXPECT_EQ(description->shape[2], width);
    EXPECT_EQ(description->strides[0], static_cast<std::int64_t>(layout.planeStrideBytes()));
    EXPECT_EQ(description->strides[1], static_cast<std::int64_t>(rowStride));
    EXPECT_EQ(description->strides[2], 4);
    EXPECT_EQ(description->lengthBytes, static_cast<std::int64_t>(width) * height * planes * 4);
}

TEST(PythonBufferBridgeTest, InterleavedStorageIsDescribedAsRowColumnSample)
{
    const std::uint32_t width = 4u;
    const std::uint32_t height = 2u;
    const std::uint32_t samplesPerPixel = 3u;
    const std::uint64_t rowStride = 64u;
    std::vector<std::uint16_t> samples(rowStride / sizeof(std::uint16_t) * height, 0u);
    const an::ImageLayout layout = layoutOf(width, height, samplesPerPixel, 1u, sizeof(std::uint16_t), rowStride);
    const std::optional<an::ImageView<std::uint16_t>> image =
        an::ImageView<std::uint16_t>::create(std::span<std::uint16_t>(samples), layout);
    ASSERT_TRUE(image.has_value());

    const std::optional<an::PythonBufferDescription> description =
        an::describeAsPythonBuffer(*image, shareableContract(an::BufferMutability::ReadWrite));
    ASSERT_TRUE(description.has_value());

    EXPECT_EQ(description->axes, 3);
    EXPECT_EQ(description->format, an::PythonBufferFormat::UInt16);
    EXPECT_EQ(description->shape[0], height);
    EXPECT_EQ(description->shape[1], width);
    EXPECT_EQ(description->shape[2], samplesPerPixel);
    EXPECT_EQ(description->strides[0], static_cast<std::int64_t>(rowStride));
    EXPECT_EQ(description->strides[1], static_cast<std::int64_t>(samplesPerPixel) * 2);
    EXPECT_EQ(description->strides[2], 2);
}

TEST(PythonBufferBridgeTest, ASemiPlanarArrangementNeedsAFourthAxisAndIsRefused)
{
    const an::ImageLayout layout = layoutOf(4u, 2u, 2u, 2u, sizeof(float), 32u);
    ASSERT_EQ(an::storageArrangementOf(layout), an::StorageArrangement::SemiPlanar);

    EXPECT_EQ(an::checkPythonBufferGeometry(an::SampleType::Float32, layout),
              an::BridgeRefusal::ArrangementNotRepresentable);
}

TEST(PythonBufferBridgeTest, TheContractIsJudgedBeforeTheLayout)
{
    std::vector<float> samples(16u, 0.0f);
    const an::ImageLayout layout = layoutOf(4u, 4u, 1u, 1u, sizeof(float), 16u);
    const std::optional<an::ImageView<const float>> readOnly =
        an::ImageView<const float>::create(std::span<const float>(samples), layout);
    ASSERT_TRUE(readOnly.has_value());

    an::ExternalBufferContract rescaling = shareableContract();
    rescaling.libraryDomain = an::NumericDomain::RawSensorCode;
    EXPECT_EQ(an::checkPythonBuffer(*readOnly, rescaling), an::BridgeRefusal::NumericDomainDiffers);
    EXPECT_FALSE(an::describeAsPythonBuffer(*readOnly, rescaling).has_value());

    an::ExternalBufferContract retained = shareableContract();
    retained.scope = an::BorrowScope::Retained;
    EXPECT_EQ(an::checkPythonBuffer(*readOnly, retained), an::BridgeRefusal::BorrowOutlivesSamples);

    EXPECT_EQ(an::checkPythonBuffer(*readOnly, shareableContract(an::BufferMutability::ReadWrite)),
              an::BridgeRefusal::WriteThroughReadOnlyView);
}

TEST(PythonBufferBridgeTest, AReadOnlyContractOverWritableSamplesStillLendsThemReadOnly)
{
    std::vector<float> samples(16u, 0.0f);
    const an::ImageLayout layout = layoutOf(4u, 4u, 1u, 1u, sizeof(float), 16u);
    const std::optional<an::ImageView<float>> writable =
        an::ImageView<float>::create(std::span<float>(samples), layout);
    ASSERT_TRUE(writable.has_value());

    const std::optional<an::PythonBufferDescription> lent =
        an::describeAsPythonBuffer(*writable, shareableContract(an::BufferMutability::ReadOnly));
    ASSERT_TRUE(lent.has_value());
    EXPECT_TRUE(lent->readOnly);
}

TEST(PythonBufferBridgeTest, AForeignBufferThisEngineCanDescribeIsAccepted)
{
    std::vector<float> samples(12u, 0.0f);
    const an::PythonBufferDescription description = foreignBuffer(samples.data(), 3, 4);
    const an::ExternalBufferContract contract =
        shareableContract(an::BufferMutability::ReadWrite, an::BufferOwnership::LibraryOwned);

    EXPECT_EQ(an::adoptPythonBufferCheck(description, contract, an::StorageArrangement::Planar),
              an::BridgeRefusal::None);

    const std::optional<an::ImageLayout> layout =
        an::pythonBufferLayout(description, an::StorageArrangement::Planar, an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->extent().width(), 4u);
    EXPECT_EQ(layout->extent().height(), 3u);
    EXPECT_EQ(layout->planeCount(), 1u);
    EXPECT_EQ(layout->samplesPerPixel(), 1u);
    EXPECT_EQ(layout->rowStrideBytes(), 16u);
    EXPECT_EQ(an::pythonBufferSampleType(description), an::SampleType::Float32);
}

TEST(PythonBufferBridgeTest, OneSetOfNumbersDescribesTwoDifferentImagesAndTheCallerSaysWhich)
{
    std::vector<float> samples(24u, 0.0f);
    an::PythonBufferDescription description = foreignBuffer(samples.data(), 2, 3);
    description.axes = 3;
    description.shape = {2, 3, 4};
    description.strides = {48, 16, 4};
    description.lengthBytes = 2 * 3 * 4 * 4;

    const std::optional<an::ImageLayout> planar =
        an::pythonBufferLayout(description, an::StorageArrangement::Planar, an::defaultEngineLimits());
    ASSERT_TRUE(planar.has_value());
    EXPECT_EQ(planar->planeCount(), 2u);
    EXPECT_EQ(planar->extent().height(), 3u);
    EXPECT_EQ(planar->extent().width(), 4u);
    EXPECT_EQ(planar->samplesPerPixel(), 1u);

    const std::optional<an::ImageLayout> interleaved =
        an::pythonBufferLayout(description, an::StorageArrangement::Interleaved, an::defaultEngineLimits());
    ASSERT_TRUE(interleaved.has_value());
    EXPECT_EQ(interleaved->planeCount(), 1u);
    EXPECT_EQ(interleaved->extent().height(), 2u);
    EXPECT_EQ(interleaved->extent().width(), 3u);
    EXPECT_EQ(interleaved->samplesPerPixel(), 4u);
}

TEST(PythonBufferBridgeTest, AnInterleavedColumnStrideMustCoverExactlyOnePixel)
{
    std::vector<float> samples(32u, 0.0f);
    an::PythonBufferDescription sliced = foreignBuffer(samples.data(), 2, 4);
    sliced.axes = 3;
    sliced.shape = {2, 4, 3};
    sliced.strides = {64, 16, 4};
    sliced.lengthBytes = 2 * 4 * 3 * 4;
    const an::ExternalBufferContract contract =
        shareableContract(an::BufferMutability::ReadWrite, an::BufferOwnership::LibraryOwned);

    EXPECT_EQ(an::adoptPythonBufferCheck(sliced, contract, an::StorageArrangement::Interleaved),
              an::BridgeRefusal::ArrangementNotRepresentable);
    EXPECT_FALSE(
        an::pythonBufferLayout(sliced, an::StorageArrangement::Interleaved, an::defaultEngineLimits()).has_value());
}

TEST(PythonBufferBridgeTest, EveryWayAForeignBufferCanBeUnusableIsNamed)
{
    std::vector<float> samples(64u, 0.0f);
    const an::ExternalBufferContract contract =
        shareableContract(an::BufferMutability::ReadWrite, an::BufferOwnership::LibraryOwned);
    const auto refusal = [&contract](const an::PythonBufferDescription& description) {
        return an::adoptPythonBufferCheck(description, contract, an::StorageArrangement::Planar);
    };

    an::PythonBufferDescription noSamples = foreignBuffer(nullptr, 3, 4);
    EXPECT_EQ(refusal(noSamples), an::BridgeRefusal::NoSamples);

    an::PythonBufferDescription unknownFormat = foreignBuffer(samples.data(), 3, 4);
    unknownFormat.format = static_cast<an::PythonBufferFormat>(99);
    EXPECT_EQ(refusal(unknownFormat), an::BridgeRefusal::UnknownContract);

    an::PythonBufferDescription wrongItemSize = foreignBuffer(samples.data(), 3, 4);
    wrongItemSize.itemSizeBytes = 8;
    EXPECT_EQ(refusal(wrongItemSize), an::BridgeRefusal::SampleTypeNotRepresentable);

    an::PythonBufferDescription oneAxis = foreignBuffer(samples.data(), 3, 4);
    oneAxis.axes = 1;
    EXPECT_EQ(refusal(oneAxis), an::BridgeRefusal::ArrangementNotRepresentable);

    an::PythonBufferDescription fourAxes = foreignBuffer(samples.data(), 3, 4);
    fourAxes.axes = 4;
    EXPECT_EQ(refusal(fourAxes), an::BridgeRefusal::ArrangementNotRepresentable);

    // A sliced or transposed array: contiguous in Python's terms, and not a row of samples in this
    // engine's, because the samples of one row are not adjacent.
    an::PythonBufferDescription stridedSamples = foreignBuffer(samples.data(), 3, 4);
    stridedSamples.strides = {32, 8, 0};
    EXPECT_EQ(refusal(stridedSamples), an::BridgeRefusal::ArrangementNotRepresentable);

    // A reversed array. Python indexes backwards happily; an engine layout holds unsigned strides.
    an::PythonBufferDescription reversedRows = foreignBuffer(samples.data(), 3, 4);
    reversedRows.strides = {-16, 4, 0};
    EXPECT_EQ(refusal(reversedRows), an::BridgeRefusal::RowStrideNotRepresentable);

    // Rows that overlap: a stride below the samples one row holds.
    an::PythonBufferDescription overlappingRows = foreignBuffer(samples.data(), 3, 4);
    overlappingRows.strides = {8, 4, 0};
    EXPECT_EQ(refusal(overlappingRows), an::BridgeRefusal::RowStrideNotRepresentable);

    an::PythonBufferDescription emptyShape = foreignBuffer(samples.data(), 0, 4);
    EXPECT_EQ(refusal(emptyShape), an::BridgeRefusal::ExtentExceedsLibraryIndex);

    an::PythonBufferDescription hugeShape = foreignBuffer(samples.data(), 3, 4);
    hugeShape.shape = {3, static_cast<std::int64_t>(0x100000000), 0};
    EXPECT_EQ(refusal(hugeShape), an::BridgeRefusal::ExtentExceedsLibraryIndex);

    an::PythonBufferDescription readOnlyBuffer = foreignBuffer(samples.data(), 3, 4);
    readOnlyBuffer.readOnly = true;
    EXPECT_EQ(refusal(readOnlyBuffer), an::BridgeRefusal::WriteThroughReadOnlyView);

    // The claim is still judged first: a domain difference is refused before anything is measured.
    an::ExternalBufferContract rescaling = contract;
    rescaling.libraryDomain = an::NumericDomain::RawSensorCode;
    EXPECT_EQ(
        an::adoptPythonBufferCheck(foreignBuffer(samples.data(), 3, 4), rescaling, an::StorageArrangement::Planar),
        an::BridgeRefusal::NumericDomainDiffers);

    // A semi-planar reading needs an axis this bridge does not have, whichever buffer it is about.
    EXPECT_EQ(
        an::adoptPythonBufferCheck(foreignBuffer(samples.data(), 3, 4), contract, an::StorageArrangement::SemiPlanar),
        an::BridgeRefusal::ArrangementNotRepresentable);
}

TEST(PythonBufferBridgeTest, ALayoutIsOnlyProducedForABufferThatWasAccepted)
{
    std::vector<float> samples(64u, 0.0f);
    an::PythonBufferDescription stridedSamples = foreignBuffer(samples.data(), 3, 4);
    stridedSamples.strides = {32, 8, 0};

    EXPECT_FALSE(
        an::pythonBufferLayout(stridedSamples, an::StorageArrangement::Planar, an::defaultEngineLimits()).has_value());

    // And the limits in force are the engine's, not this bridge's: a buffer larger than the caller's
    // configured maximum is refused here rather than at the first allocation.
    an::EngineLimits narrow = an::defaultEngineLimits();
    narrow.maxImageWidth = 2u;
    EXPECT_FALSE(an::pythonBufferLayout(foreignBuffer(samples.data(), 3, 4), an::StorageArrangement::Planar, narrow)
                     .has_value());
}

TEST(PythonBufferBridgeTest, ADescriptionThisBridgeProducedIsOneThisBridgeAccepts)
{
    // The two directions have to agree, or a round trip through Python would lose an image.
    for (const an::SampleType type : {an::SampleType::UInt8, an::SampleType::Int16, an::SampleType::Float64})
    {
        const std::uint32_t elementSize = an::sampleTypeSizeBytes(type);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(elementSize) * 4u * 3u, 0u);
        const an::ImageLayout layout = layoutOf(4u, 3u, 1u, 1u, elementSize, elementSize * 4u);
        const std::optional<an::PythonBufferFormat> format = an::pythonBufferFormatOf(type);
        ASSERT_TRUE(format.has_value());

        an::PythonBufferDescription description{.data = bytes.data(),
                                                .lengthBytes = static_cast<std::int64_t>(bytes.size()),
                                                .itemSizeBytes = elementSize,
                                                .axes = 2,
                                                .shape = {3, 4, 0},
                                                .strides = {elementSize * 4, elementSize, 0},
                                                .format = *format,
                                                .readOnly = false};
        EXPECT_EQ(an::adoptPythonBufferCheck(
                      description,
                      shareableContract(an::BufferMutability::ReadWrite, an::BufferOwnership::LibraryOwned),
                      an::StorageArrangement::Planar),
                  an::BridgeRefusal::None)
            << an::sampleTypeName(type);
        const std::optional<an::ImageLayout> reconstructed =
            an::pythonBufferLayout(description, an::StorageArrangement::Planar, an::defaultEngineLimits());
        ASSERT_TRUE(reconstructed.has_value()) << an::sampleTypeName(type);
        EXPECT_EQ(reconstructed->rowStrideBytes(), layout.rowStrideBytes()) << an::sampleTypeName(type);
        EXPECT_EQ(reconstructed->elementSizeBytes(), layout.elementSizeBytes()) << an::sampleTypeName(type);
    }
}
