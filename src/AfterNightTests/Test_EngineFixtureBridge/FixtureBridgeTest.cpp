// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the engine benchmark runner's fixture bridge.
//
// A benchmark fixture is a descriptor and a block of bytes, described in a vocabulary the engine
// does not own. Turning that into something this engine can read is four translations - the
// representation, the arrangement, the byte order, the mosaic - plus a geometry and a statement of
// what the samples mean, and every one of them can be wrong in a way that produces a number rather
// than an error.
//
// So these cases are mostly about exactness. A stride the descriptor states is the stride the
// layout carries; a stride it leaves at zero is derived from the same numbers the benchmark layer
// derives it from; a 64-bit integer frame is refused rather than narrowed; a payload in the other
// byte order is refused rather than read; and a view over a fixture points into the fixture's own
// bytes rather than into a copy of them. Each of those, done the other way, would give a comparison
// two measurements of two different things and no way to notice.

#include "FixtureBridge.h"

#include "PipelineStream.h"
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stop_token>

#include "Descriptors.h"
#include "Fixture.h"

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/GeometryError.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace
{

/// The bench layer's engine side, where the fixture bridge lives.
namespace bridge = an::bench::engine;

using an::bench::ByteOrder;
using an::bench::Fixture;
using an::bench::Layout;
using an::bench::SampleDescriptor;
using an::bench::SampleType;
using bridge::BridgedFixture;
using bridge::FixtureBridgeFailure;
using bridge::FixtureSemantics;

constexpr SampleType kAllBenchSampleTypes[] = {
    SampleType::UInt8, SampleType::Int8,  SampleType::UInt16, SampleType::Int16,   SampleType::UInt32,
    SampleType::Int32, SampleType::Int64, SampleType::UInt64, SampleType::Float32, SampleType::Float64};

constexpr an::SampleType kAllEngineSampleTypes[] = {
    an::SampleType::UInt8,  an::SampleType::Int8,  an::SampleType::UInt16,  an::SampleType::Int16,
    an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64};

constexpr FixtureBridgeFailure kAllFailures[] = {FixtureBridgeFailure::None,
                                                 FixtureBridgeFailure::DescriptorNotUsable,
                                                 FixtureBridgeFailure::SampleTypeNotResident,
                                                 FixtureBridgeFailure::ArrangementNotSupported,
                                                 FixtureBridgeFailure::ByteOrderNotNative,
                                                 FixtureBridgeFailure::CfaPatternNotSupported,
                                                 FixtureBridgeFailure::ExtentRefused,
                                                 FixtureBridgeFailure::StrideRefused,
                                                 FixtureBridgeFailure::MeaningRefused,
                                                 FixtureBridgeFailure::PayloadTooSmall};

/// A descriptor a scenario would plausibly declare, so a case can break exactly one field.
[[nodiscard]] SampleDescriptor monoDescriptor(SampleType type, std::uint32_t width, std::uint32_t height)
{
    SampleDescriptor descriptor;
    descriptor.width = width;
    descriptor.height = height;
    descriptor.channels = 1u;
    descriptor.sampleType = type;
    descriptor.layout = Layout::Planar;
    descriptor.byteOrder = ByteOrder::Little;
    return descriptor;
}

/// A fixture holding exactly the bytes its descriptor implies, filled with a recognisable ramp.
[[nodiscard]] Fixture rampFixture(const SampleDescriptor& descriptor)
{
    Fixture fixture;
    fixture.descriptor = descriptor;
    fixture.bytes.resize(static_cast<std::size_t>(descriptor.totalBytes()));
    for (std::size_t index = 0; index < fixture.bytes.size(); ++index)
    {
        fixture.bytes[index] = static_cast<std::uint8_t>(index & 0xffu);
    }
    fixture.sourceId = "synthetic.ramp";
    fixture.generatorVersion = 1u;
    return fixture;
}

TEST(FixtureBridgeTest, EveryEngineRepresentationRoundTripsThroughTheBenchmarkVocabulary)
{
    for (const an::SampleType type : kAllEngineSampleTypes)
    {
        const SampleType benchType = bridge::toBenchSampleType(type);
        const std::optional<an::SampleType> back = bridge::toEngineSampleType(benchType);
        ASSERT_TRUE(back.has_value()) << an::sampleTypeName(type);
        EXPECT_EQ(*back, type) << an::sampleTypeName(type);
    }
}

TEST(FixtureBridgeTest, TheTwoRepresentationsThisEngineDoesNotStoreAreRefusedRatherThanNarrowed)
{
    for (const SampleType type : kAllBenchSampleTypes)
    {
        const std::optional<an::SampleType> engineType = bridge::toEngineSampleType(type);
        if (type == SampleType::Int64 || type == SampleType::UInt64)
        {
            EXPECT_FALSE(engineType.has_value()) << an::bench::toString(type);
        }
        else
        {
            EXPECT_TRUE(engineType.has_value()) << an::bench::toString(type);
        }
    }
}

TEST(FixtureBridgeTest, ASixtyFourBitFixtureIsRefusedByName)
{
    const SampleDescriptor descriptor = monoDescriptor(SampleType::Int64, 8u, 4u);
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);
    EXPECT_EQ(bridge::checkFixture(descriptor, semantics, an::defaultEngineLimits(), descriptor.totalBytes()),
              FixtureBridgeFailure::SampleTypeNotResident);
    EXPECT_FALSE(
        bridge::bridgeFixture(descriptor, semantics, an::defaultEngineLimits(), descriptor.totalBytes()).has_value());
}

TEST(FixtureBridgeTest, TheArrangementTableRefusesTheOneItCannotPlace)
{
    EXPECT_EQ(bridge::toEngineArrangement(Layout::Planar), an::StorageArrangement::Planar);
    EXPECT_EQ(bridge::toEngineArrangement(Layout::Interleaved), an::StorageArrangement::Interleaved);
    EXPECT_EQ(bridge::toEngineArrangement(Layout::PackedCfa), an::StorageArrangement::Planar);
    EXPECT_FALSE(bridge::toEngineArrangement(Layout::SemiPlanar).has_value());

    // The shared benchmark layer refuses a semi-planar descriptor as one it cannot size, and it is
    // checked first, so that is the reason a fixture carries rather than this bridge's. The two
    // layers agreeing about the same descriptor is the property worth pinning: a bridge that
    // accepted what the shared layer refuses would be measuring a shape no scenario can describe.
    SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    descriptor.channels = 3u;
    descriptor.layout = Layout::SemiPlanar;
    EXPECT_FALSE(descriptor.isValid());
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);
    EXPECT_EQ(bridge::checkFixture(descriptor, semantics, an::defaultEngineLimits(), descriptor.totalBytes()),
              FixtureBridgeFailure::DescriptorNotUsable);
}

TEST(FixtureBridgeTest, ThePayloadsByteOrderIsResolvedAgainstThisHostRatherThanPassedThrough)
{
    EXPECT_EQ(bridge::toEngineByteOrder(ByteOrder::Native), an::nativeByteOrder());
    EXPECT_EQ(bridge::toEngineByteOrder(ByteOrder::Little), an::ByteOrder::Little);
    EXPECT_EQ(bridge::toEngineByteOrder(ByteOrder::Big), an::ByteOrder::Big);
}

TEST(FixtureBridgeTest, APayloadInTheOtherOrderIsRefusedRatherThanRead)
{
    SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    descriptor.byteOrder = an::nativeByteOrder() == an::ByteOrder::Little ? ByteOrder::Big : ByteOrder::Little;
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);

    EXPECT_EQ(bridge::checkFixture(descriptor, semantics, an::defaultEngineLimits(), descriptor.totalBytes()),
              FixtureBridgeFailure::ByteOrderNotNative);

    // One byte has one order, so an 8-bit fixture in the other order is the same fixture and is
    // read where it is. Refusing it would be two descriptions of identical bytes that disagree.
    SampleDescriptor bytes = descriptor;
    bytes.sampleType = SampleType::UInt8;
    EXPECT_EQ(bridge::checkFixture(bytes, bridge::defaultFixtureSemantics(bytes), an::defaultEngineLimits(),
                                   bytes.totalBytes()),
              FixtureBridgeFailure::None);
}

TEST(FixtureBridgeTest, TheMosaicTableIsClosedAndCaseSensitive)
{
    EXPECT_EQ(bridge::toEngineCfaPattern(""), an::CfaPattern::None);
    EXPECT_EQ(bridge::toEngineCfaPattern("RGGB"), an::CfaPattern::Rggb);
    EXPECT_EQ(bridge::toEngineCfaPattern("BGGR"), an::CfaPattern::Bggr);
    EXPECT_EQ(bridge::toEngineCfaPattern("GRBG"), an::CfaPattern::Grbg);
    EXPECT_EQ(bridge::toEngineCfaPattern("GBRG"), an::CfaPattern::Gbrg);
    EXPECT_FALSE(bridge::toEngineCfaPattern("rggb").has_value());
    EXPECT_FALSE(bridge::toEngineCfaPattern("XTRANS").has_value());
}

TEST(FixtureBridgeTest, AMosaicedFixtureBecomesAMosaicedDescription)
{
    SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    descriptor.layout = Layout::PackedCfa;
    descriptor.cfaPattern = "RGGB";
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);

    const std::optional<BridgedFixture> bridged =
        bridge::bridgeFixture(descriptor, semantics, an::defaultEngineLimits(), descriptor.totalBytes());
    ASSERT_TRUE(bridged.has_value());
    EXPECT_TRUE(bridged->descriptor.cfa().isMosaiced());
    EXPECT_EQ(bridged->descriptor.cfa().pattern(), an::CfaPattern::Rggb);
    EXPECT_EQ(bridged->descriptor.channelSchema(), an::ChannelSchema::Mono);
    EXPECT_EQ(bridged->arrangement, an::StorageArrangement::Planar);
}

TEST(FixtureBridgeTest, AMosaicOverThreeChannelsIsRefusedByTheDescriptorItself)
{
    SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    descriptor.channels = 3u;
    descriptor.cfaPattern = "RGGB";
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);

    EXPECT_EQ(bridge::checkFixture(descriptor, semantics, an::defaultEngineLimits(), descriptor.totalBytes()),
              FixtureBridgeFailure::MeaningRefused);
}

TEST(FixtureBridgeTest, TheDefaultMeaningIsStatedRatherThanInferredFromTheRepresentation)
{
    const SampleDescriptor mono = monoDescriptor(SampleType::UInt16, 8u, 4u);
    EXPECT_EQ(bridge::defaultFixtureSemantics(mono).channelSchema, an::ChannelSchema::Mono);

    SampleDescriptor colour = mono;
    colour.channels = 3u;
    EXPECT_EQ(bridge::defaultFixtureSemantics(colour).channelSchema, an::ChannelSchema::Rgb);

    SampleDescriptor filters = mono;
    filters.channels = 5u;
    EXPECT_EQ(bridge::defaultFixtureSemantics(filters).channelSchema, an::ChannelSchema::Named);

    // The domain is a fraction of full scale whatever the representation is. That an unsigned
    // 16-bit fixture and a float one get the same answer is the point: this engine does not read a
    // meaning out of a C++ type, and a scenario measuring sensor codes states so.
    SampleDescriptor asFloat = mono;
    asFloat.sampleType = SampleType::Float32;
    EXPECT_EQ(bridge::defaultFixtureSemantics(mono).numericDomain,
              bridge::defaultFixtureSemantics(asFloat).numericDomain);
    EXPECT_EQ(bridge::defaultFixtureSemantics(mono).numericDomain, an::NumericDomain::NormalizedLinear);
}

TEST(FixtureBridgeTest, AScenarioMayStateAMeaningOfItsOwn)
{
    const SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    const FixtureSemantics sensor{.channelSchema = an::ChannelSchema::Mono,
                                  .colorSpace = an::ColorSpace::Unspecified,
                                  .numericDomain = an::NumericDomain::RawSensorCode,
                                  .coordinateOrigin = an::CoordinateOrigin::BottomLeft,
                                  .pixelAspectRatio = 1.0};

    const std::optional<BridgedFixture> bridged =
        bridge::bridgeFixture(descriptor, sensor, an::defaultEngineLimits(), descriptor.totalBytes());
    ASSERT_TRUE(bridged.has_value());
    EXPECT_EQ(bridged->descriptor.numericDomain(), an::NumericDomain::RawSensorCode);
    EXPECT_EQ(bridged->descriptor.coordinateOrigin(), an::CoordinateOrigin::BottomLeft);
}

TEST(FixtureBridgeTest, AMeaningTheRepresentationCannotCarryIsRefusedByName)
{
    // A physically scaled value in an integer sample: the descriptor carries no scale factor for it
    // to be read through, so the pair cannot describe one image.
    const SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    const FixtureSemantics physical{.channelSchema = an::ChannelSchema::Mono,
                                    .colorSpace = an::ColorSpace::Unspecified,
                                    .numericDomain = an::NumericDomain::PhysicalScaled,
                                    .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                    .pixelAspectRatio = 1.0};

    EXPECT_EQ(bridge::checkFixture(descriptor, physical, an::defaultEngineLimits(), descriptor.totalBytes()),
              FixtureBridgeFailure::MeaningRefused);
}

TEST(FixtureBridgeTest, ATightlyPackedFixtureGetsTheStridesTheBenchmarkLayerDerives)
{
    const SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);

    const std::optional<BridgedFixture> bridged =
        bridge::bridgeFixture(descriptor, semantics, an::defaultEngineLimits(), descriptor.totalBytes());
    ASSERT_TRUE(bridged.has_value());
    EXPECT_EQ(bridged->layout.rowStrideBytes(), descriptor.rowBytes());
    EXPECT_EQ(bridged->layout.totalBytes(), descriptor.totalBytes());
    EXPECT_TRUE(bridged->layout.isContiguous());
    EXPECT_TRUE(an::layoutDescribes(bridged->descriptor, bridged->layout));
}

TEST(FixtureBridgeTest, AStatedStrideIsKeptExactlyRatherThanTightened)
{
    // A scenario that declares a noncanonical layout is measuring that layout. Tightening it here
    // would measure a different one and report it under the scenario's name.
    SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    descriptor.rowStrideBytes = 24u;
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);

    const std::optional<BridgedFixture> bridged =
        bridge::bridgeFixture(descriptor, semantics, an::defaultEngineLimits(), descriptor.totalBytes());
    ASSERT_TRUE(bridged.has_value());
    EXPECT_EQ(bridged->layout.rowStrideBytes(), 24u);
    EXPECT_EQ(bridged->layout.minimumRowBytes(), 16u);
    EXPECT_FALSE(bridged->layout.isContiguous());
}

TEST(FixtureBridgeTest, AStatedPlaneStrideIsKeptExactlyToo)
{
    SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    descriptor.channels = 3u;
    descriptor.planeStrideBytes = 128u;
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);

    const std::optional<BridgedFixture> bridged =
        bridge::bridgeFixture(descriptor, semantics, an::defaultEngineLimits(), descriptor.totalBytes());
    ASSERT_TRUE(bridged.has_value());
    EXPECT_EQ(bridged->layout.planeStrideBytes(), 128u);
    EXPECT_EQ(bridged->layout.planeCount(), 3u);
    EXPECT_EQ(bridged->layout.samplesPerPixel(), 1u);
}

TEST(FixtureBridgeTest, AnInterleavedFixtureBecomesOneInterleavedPlane)
{
    SampleDescriptor descriptor = monoDescriptor(SampleType::UInt8, 8u, 4u);
    descriptor.channels = 3u;
    descriptor.layout = Layout::Interleaved;
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);

    const std::optional<BridgedFixture> bridged =
        bridge::bridgeFixture(descriptor, semantics, an::defaultEngineLimits(), descriptor.totalBytes());
    ASSERT_TRUE(bridged.has_value());
    EXPECT_EQ(bridged->arrangement, an::StorageArrangement::Interleaved);
    EXPECT_EQ(an::storageArrangementOf(bridged->layout), an::StorageArrangement::Interleaved);
    EXPECT_EQ(bridged->layout.samplesPerPixel(), 3u);
    EXPECT_EQ(bridged->layout.planeCount(), 1u);
    EXPECT_EQ(bridged->layout.rowStrideBytes(), 24u);
}

TEST(FixtureBridgeTest, AStrideShorterThanARowIsRefusedByBothLayers)
{
    // The shared layer refuses this one first, so the reason a fixture carries is that its own
    // descriptor is unusable rather than that this engine's geometry refused it. Both refuse, which
    // is what matters: the failure a caller must never see is the stride being accepted.
    SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    descriptor.rowStrideBytes = 8u;
    EXPECT_FALSE(descriptor.isValid());
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);

    EXPECT_EQ(bridge::checkFixture(descriptor, semantics, an::defaultEngineLimits(), 1024u),
              FixtureBridgeFailure::DescriptorNotUsable);

    const an::ImageLayoutRequest request{.extent =
                                             an::ImageExtent::create(8u, 4u, 1u, an::defaultEngineLimits()).value(),
                                         .elementSizeBytes = 2u,
                                         .samplesPerPixel = 1u,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
    EXPECT_EQ(an::ImageLayout::checkWithStrides(request, 8u, 32u, an::defaultEngineLimits()),
              an::GeometryError::StrideTooSmall);
}

TEST(FixtureBridgeTest, AStrideThatIsNotAWholeNumberOfSamplesIsRefusedByName)
{
    SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    descriptor.rowStrideBytes = 17u;
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);

    EXPECT_EQ(bridge::checkFixture(descriptor, semantics, an::defaultEngineLimits(), 1024u),
              FixtureBridgeFailure::StrideRefused);
}

TEST(FixtureBridgeTest, AnExtentTheConfiguredLimitsRefuseIsNamedAsOne)
{
    const SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);
    const an::EngineLimits narrow{.maxImageWidth = 4u,
                                  .maxImageHeight = 4u,
                                  .maxChannelCount = 1u,
                                  .maxImageBytes = 1024u,
                                  .memoryBudgetBytes = an::kDefaultMemoryBudgetBytes};

    EXPECT_EQ(bridge::checkFixture(descriptor, semantics, narrow, descriptor.totalBytes()),
              FixtureBridgeFailure::ExtentRefused);
}

TEST(FixtureBridgeTest, AFixtureWithFewerBytesThanItsOwnDescriptorIsRefused)
{
    const SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);

    EXPECT_EQ(bridge::checkFixture(descriptor, semantics, an::defaultEngineLimits(), descriptor.totalBytes() - 1u),
              FixtureBridgeFailure::PayloadTooSmall);
    EXPECT_EQ(bridge::checkFixture(descriptor, semantics, an::defaultEngineLimits(), descriptor.totalBytes()),
              FixtureBridgeFailure::None);
}

TEST(FixtureBridgeTest, ADescriptorThatDescribesNoImageIsRefusedBeforeAnythingElse)
{
    SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 0u, 4u);
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);
    EXPECT_EQ(bridge::checkFixture(descriptor, semantics, an::defaultEngineLimits(), 0u),
              FixtureBridgeFailure::DescriptorNotUsable);
}

TEST(FixtureBridgeTest, AViewOverAFixturePointsIntoTheFixturesOwnBytes)
{
    const SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    const Fixture fixture = rampFixture(descriptor);
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);

    const std::optional<BridgedFixture> bridged =
        bridge::bridgeFixture(descriptor, semantics, an::defaultEngineLimits(), fixture.bytes.size());
    ASSERT_TRUE(bridged.has_value());

    const std::optional<an::ImageView<const std::uint16_t>> view =
        bridge::viewFixture<std::uint16_t>(fixture, bridged->layout);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(reinterpret_cast<const void*>(view->data()), reinterpret_cast<const void*>(fixture.bytes.data()));
    EXPECT_EQ(view->width(), 8u);
    EXPECT_EQ(view->height(), 4u);

    // And every sample it reaches is the byte the fixture holds there, which is what proves the
    // bridge described the buffer rather than a buffer of the same size.
    std::uint16_t expected = 0u;
    std::memcpy(&expected, fixture.bytes.data() + 2u * 5u, sizeof(expected));
    const std::uint16_t* const sample = view->sampleAt(0u, 5u, 0u, 0u);
    ASSERT_NE(sample, nullptr);
    EXPECT_EQ(*sample, expected);
}

TEST(FixtureBridgeTest, AViewHonoursAStatedStrideRatherThanAssumingATightOne)
{
    SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    descriptor.rowStrideBytes = 24u;
    const Fixture fixture = rampFixture(descriptor);
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);

    const std::optional<BridgedFixture> bridged =
        bridge::bridgeFixture(descriptor, semantics, an::defaultEngineLimits(), fixture.bytes.size());
    ASSERT_TRUE(bridged.has_value());

    const std::optional<an::ImageView<const std::uint16_t>> view =
        bridge::viewFixture<std::uint16_t>(fixture, bridged->layout);
    ASSERT_TRUE(view.has_value());

    // The first sample of row one sits twenty-four bytes in, not sixteen. A bridge that tightened
    // the stride would put it at sixteen and every measurement over the frame would read the
    // padding as data.
    const std::uint16_t* const rowOne = view->sampleAt(0u, 0u, 1u, 0u);
    ASSERT_NE(rowOne, nullptr);
    EXPECT_EQ(reinterpret_cast<const std::uint8_t*>(rowOne), fixture.bytes.data() + 24u);
}

TEST(FixtureBridgeTest, AViewWhoseSampleTypeDoesNotMatchTheLayoutIsRefused)
{
    const SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    const Fixture fixture = rampFixture(descriptor);
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);

    const std::optional<BridgedFixture> bridged =
        bridge::bridgeFixture(descriptor, semantics, an::defaultEngineLimits(), fixture.bytes.size());
    ASSERT_TRUE(bridged.has_value());

    EXPECT_FALSE(bridge::viewFixture<float>(fixture, bridged->layout).has_value());
    EXPECT_FALSE(bridge::viewFixture<std::uint8_t>(fixture, bridged->layout).has_value());
}

TEST(FixtureBridgeTest, AViewOverAFixtureThatLostItsBytesIsRefusedRatherThanDangling)
{
    const SampleDescriptor descriptor = monoDescriptor(SampleType::UInt16, 8u, 4u);
    Fixture fixture = rampFixture(descriptor);
    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(descriptor);

    const std::optional<BridgedFixture> bridged =
        bridge::bridgeFixture(descriptor, semantics, an::defaultEngineLimits(), fixture.bytes.size());
    ASSERT_TRUE(bridged.has_value());

    fixture.bytes.resize(fixture.bytes.size() / 2u);
    EXPECT_FALSE(bridge::viewFixture<std::uint16_t>(fixture, bridged->layout).has_value());
}

TEST(FixtureBridgeTest, TheBackendIdentityReportsTheSelectedImplementationDialect)
{
    const an::Result<an::BackendSelection> reference =
        an::BackendSelection{.backend = an::ComputeBackend::Reference, .instructionSet = an::InstructionSet::Scalar};
    EXPECT_EQ(bridge::toBenchBackend(reference), an::bench::BackendId::Scalar);

    const an::Result<an::BackendSelection> compiled =
        an::BackendSelection{.backend = an::ComputeBackend::Cpu, .instructionSet = an::InstructionSet::Avx2};
    EXPECT_EQ(bridge::toBenchBackend(compiled), an::bench::BackendId::Avx2);

    const an::Result<an::BackendSelection> compiledAvx512 =
        an::BackendSelection{.backend = an::ComputeBackend::Cpu, .instructionSet = an::InstructionSet::Avx512Skylake};
    EXPECT_EQ(bridge::toBenchBackend(compiledAvx512), an::bench::BackendId::Avx512);

    const an::Result<an::BackendSelection> compiledScalar =
        an::BackendSelection{.backend = an::ComputeBackend::Cpu, .instructionSet = an::InstructionSet::Scalar};
    EXPECT_EQ(bridge::toBenchBackend(compiledScalar), an::bench::BackendId::Scalar);

    const an::Result<an::BackendSelection> gpu =
        an::BackendSelection{.backend = an::ComputeBackend::Gpu, .instructionSet = an::InstructionSet::Scalar};
    EXPECT_EQ(bridge::toBenchBackend(gpu), an::bench::BackendId::Gpu);

    const an::Result<an::BackendSelection> refused{an::Status::failure(an::ErrorCode::UnsupportedBackend)};
    EXPECT_EQ(bridge::toBenchBackend(refused), an::bench::BackendId::Unspecified);
}

TEST(FixtureBridgeTest, AnEngineFailureIsReportedByCodeNameAsWellAsByMessage)
{
    const an::Status refused = an::Status::failure(an::ErrorCode::MemoryBudgetExceeded);
    const std::string sentence = bridge::describeEngineFailure("the destination", refused);

    EXPECT_NE(sentence.find("the destination"), std::string::npos) << sentence;
    EXPECT_NE(sentence.find("MemoryBudgetExceeded"), std::string::npos) << sentence;
}

TEST(FixtureBridgeTest, TheLastDescribedFailureIsTakenByCodeAndSaysWhetherAStopCausedIt)
{
    (void)bridge::takeLastDescribedEngineFailure();
    EXPECT_FALSE(bridge::takeLastDescribedEngineFailure().has_value());

    const an::Status stopped = an::Status::failure(an::ErrorCode::Cancelled);
    const an::Status wrapped =
        an::Status::failure(an::ErrorCode::ExternalFailure, {.message = "the stage gave up"}, stopped);
    const an::Status refused = an::Status::failure(an::ErrorCode::MemoryBudgetExceeded);
    (void)bridge::describeEngineFailure("the fit", refused);
    (void)bridge::describeEngineFailure("the fit", wrapped);

    const std::optional<bridge::DescribedEngineFailure> taken = bridge::takeLastDescribedEngineFailure();
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(taken->code, an::ErrorCode::ExternalFailure);
    EXPECT_TRUE(taken->cancelled);
    EXPECT_FALSE(bridge::takeLastDescribedEngineFailure().has_value());

    (void)bridge::describeEngineFailure("the fit", refused);
    const std::optional<bridge::DescribedEngineFailure> plain = bridge::takeLastDescribedEngineFailure();
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ(plain->code, an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_FALSE(plain->cancelled);
}

TEST(FixtureBridgeTest, EngineFailureReportsRetainThePlatformCodeAcrossWrappers)
{
    const auto native =
        an::Status::failure(an::ErrorCode::ExternalFailure,
                            {.context = {an::StatusContextField::ofNumber(an::ContextKey::LibraryCode, 32)}});
    const auto wrapped = an::Status::failure(an::ErrorCode::ExternalFailure, {.message = "publish failed"}, native);
    for (const auto& status : {native, wrapped})
        EXPECT_NE(bridge::describeEngineFailure("publication", status).find("[LibraryCode=32]"), std::string::npos);
    const auto overridden =
        an::Status::failure(an::ErrorCode::ExternalFailure,
                            {.context = {an::StatusContextField::ofNumber(an::ContextKey::LibraryCode, 5)}}, native);
    const auto sentence = bridge::describeEngineFailure("publication", overridden);
    EXPECT_NE(sentence.find("[LibraryCode=5]"), std::string::npos);
    EXPECT_EQ(sentence.find("[LibraryCode=32]"), std::string::npos);
}

TEST(FixtureBridgeTest, ARefusalSentenceNamesTheReasonAndTheShapeItRefused)
{
    const SampleDescriptor descriptor = monoDescriptor(SampleType::Int64, 1024u, 512u);
    const std::string sentence =
        bridge::describeFixtureBridgeFailure(FixtureBridgeFailure::SampleTypeNotResident, descriptor);

    EXPECT_NE(sentence.find("SampleTypeNotResident"), std::string::npos) << sentence;
    EXPECT_NE(sentence.find("1024x512x1"), std::string::npos) << sentence;
    EXPECT_NE(sentence.find("Int64"), std::string::npos) << sentence;
}

TEST(FixtureBridgeTest, NoTwoFailuresShareAName)
{
    for (const FixtureBridgeFailure left : kAllFailures)
    {
        const char* const leftName = bridge::fixtureBridgeFailureName(left);
        ASSERT_NE(leftName, nullptr);
        EXPECT_STRNE(leftName, "UnknownFixtureBridgeFailure") << static_cast<int>(left);
        for (const FixtureBridgeFailure right : kAllFailures)
        {
            if (left == right)
            {
                continue;
            }
            EXPECT_STRNE(leftName, bridge::fixtureBridgeFailureName(right)) << leftName;
        }
    }
    EXPECT_STREQ(bridge::fixtureBridgeFailureName(static_cast<FixtureBridgeFailure>(200)),
                 "UnknownFixtureBridgeFailure");
}

TEST(FixtureBridgeTest, TheScenarioTheBenchmarkRunnerActuallyMeasuresGoesThroughThisBridge)
{
    // The runner's own smoke scenario, built from the same numbers: a 1024-square planar unsigned
    // 16-bit frame, converted to Float32. Both sides are bridged, which is what the runner does, so
    // a change to these rules that broke that scenario fails here rather than in a benchmark run.
    SampleDescriptor source;
    source.width = 1024u;
    source.height = 1024u;
    source.channels = 1u;
    source.sampleType = SampleType::UInt16;
    source.layout = Layout::Planar;
    source.byteOrder = ByteOrder::Little;

    SampleDescriptor destination = source;
    destination.sampleType = SampleType::Float32;

    const FixtureSemantics semantics = bridge::defaultFixtureSemantics(source);
    const std::optional<BridgedFixture> bridgedSource =
        bridge::bridgeFixture(source, semantics, an::defaultEngineLimits(), source.totalBytes());
    const std::optional<BridgedFixture> bridgedDestination =
        bridge::bridgeFixture(destination, semantics, an::defaultEngineLimits(), destination.totalBytes());

    ASSERT_TRUE(bridgedSource.has_value());
    ASSERT_TRUE(bridgedDestination.has_value());
    EXPECT_TRUE(bridgedSource->layout.isContiguous());
    EXPECT_TRUE(bridgedDestination->layout.isContiguous());
    EXPECT_EQ(bridgedSource->layout.totalBytes(), 1024u * 1024u * 2u);
    EXPECT_EQ(bridgedDestination->layout.totalBytes(), 1024u * 1024u * 4u);
    EXPECT_EQ(bridgedSource->descriptor.extent(), bridgedDestination->descriptor.extent());
}

} // namespace

namespace
{
class PipelineStreamTest : public testing::Test
{
protected:
    void SetUp() override
    {
        directory =
            std::filesystem::temp_directory_path() /
            ("an-pipeline-stream-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        const auto codec = an::createFitsCodec();
        ASSERT_TRUE(codec);
        const std::array codecs{*codec};
        an::EngineContextOptions options;
        options.maxConcurrency = 4u;
        options.codecRegistry = *an::CodecRegistry::create(codecs);
        engine = std::move(*an::EngineContext::create(options));
        auto execution = an::ExecutionContext::create(engine, {.maxConcurrency = 1u});
        const auto extent = *an::ImageExtent::create(17u, 9u, 1u, engine.limits());
        const auto descriptor = *an::RawFrameDescriptor::create({extent,
                                                                 {2u, an::RawSampleClass::UnsignedInteger},
                                                                 an::ByteOrder::Native,
                                                                 an::ChannelSchema::Mono,
                                                                 an::ColorSpace::Unspecified,
                                                                 an::NumericDomain::RawSensorCode,
                                                                 {},
                                                                 an::CoordinateOrigin::TopLeft,
                                                                 1.0});
        const auto layout = *an::ImageLayout::create({extent, 2u, 1u, 1u, 1u}, engine.limits());
        for (std::size_t index = 0u; index < 4u; ++index)
        {
            inputs.push_back((directory / ("input-" + std::to_string(index) + ".fits")).string());
            outputs.push_back((directory / ("output-" + std::to_string(index) + ".fits")).string());
            auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
            auto frame = an::RawFrame::create(descriptor, layout, std::move(*buffer));
            const auto value = static_cast<std::uint16_t>(index + 17u);
            std::memcpy(frame->mutableBytes()->data(), &value, sizeof(value));
            ASSERT_TRUE(an::writeImage(engine, *execution,
                                       {.destinationPath = inputs.back(),
                                        .format = an::CodecFormat::Fits,
                                        .durability = an::OutputDurability::CloseOnly},
                                       *frame, {}));
        }
    }
    void TearDown() override
    {
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
        EXPECT_TRUE(engine.shutdown().ok());
        const auto root = std::filesystem::weakly_canonical(directory);
        ASSERT_EQ(root.parent_path(), std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()));
        std::error_code error;
        std::filesystem::remove_all(root, error);
        EXPECT_FALSE(error);
    }
    void expectNoPendingFiles()
    {
        for (const auto& entry : std::filesystem::directory_iterator(directory))
            EXPECT_NE(entry.path().extension(), ".antmp");
    }
    an::EngineContext engine;
    std::filesystem::path directory;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
};

TEST_F(PipelineStreamTest, InlineAndQueuedFramesRetainOrderAndSamples)
{
    for (const auto workers : {1u, 4u})
    {
        auto execution = an::ExecutionContext::create(engine, {.maxConcurrency = workers});
        std::uint64_t seen = 0u;
        auto visit = [&](const an::EngineContext&, const an::ExecutionContext&,
                         an::SequenceFrame frame) -> an::Result<an::SequenceFrame> {
            EXPECT_EQ(frame.position.index, seen++);
            return frame;
        };
        an::FrameStreamCounters counters;
        const auto status = bridge::streamPipelineFiles(engine, *execution, inputs, outputs, visit, counters);
        ASSERT_TRUE(status.ok()) << status.message();
        EXPECT_EQ(seen, inputs.size());
        EXPECT_EQ(counters.framesWritten, inputs.size());
        EXPECT_EQ(counters.readAhead.peakFrames, workers == 1u ? 0u : 1u);
        EXPECT_EQ(counters.writeBehind.peakFrames, workers == 1u ? 0u : 1u);
        for (std::size_t index = 0; index < outputs.size(); ++index)
        {
            auto read = an::decodeImage(engine, *execution, {.path = outputs[index], .format = an::CodecFormat::Fits});
            ASSERT_TRUE(read);
            std::uint16_t value = 0u;
            std::memcpy(&value, read->frame.bytes().data(), sizeof(value));
            EXPECT_EQ(value, index + 17u);
        }
        expectNoPendingFiles();
    }
}

TEST_F(PipelineStreamTest, LaterReadFailurePreservesDestinationsAndRemovesEveryPendingFile)
{
    auto execution = an::ExecutionContext::create(engine, {.maxConcurrency = 4u});
    inputs.back() += ".missing";
    std::ofstream(outputs.front()) << "existing destination";
    auto visit = [](const an::EngineContext&, const an::ExecutionContext&,
                    an::SequenceFrame frame) -> an::Result<an::SequenceFrame> { return frame; };
    an::FrameStreamCounters counters;
    const auto status = bridge::streamPipelineFiles(engine, *execution, inputs, outputs, visit, counters);
    EXPECT_TRUE(status.failed());
    EXPECT_GT(counters.framesProcessed, 0u);
    std::ifstream existing(outputs.front());
    std::string text;
    std::getline(existing, text);
    EXPECT_EQ(text, "existing destination");
    for (std::size_t index = 1u; index < outputs.size(); ++index)
        EXPECT_FALSE(std::filesystem::exists(outputs[index]));
    expectNoPendingFiles();
}

TEST_F(PipelineStreamTest, CancellationJoinsTheQueuesBeforeAbortingPrivateWrites)
{
    std::stop_source stop;
    an::ExecutionOptions options;
    options.maxConcurrency = 4u;
    options.stopToken = stop.get_token();
    auto execution = an::ExecutionContext::create(engine, options);
    auto cancel = [&](const an::EngineContext&, const an::ExecutionContext&,
                      an::SequenceFrame frame) -> an::Result<an::SequenceFrame> {
        if (frame.position.index == 2u)
            stop.request_stop();
        return frame;
    };
    an::FrameStreamCounters counters;
    const auto status = bridge::streamPipelineFiles(engine, *execution, inputs, outputs, cancel, counters);
    EXPECT_EQ(status.code(), an::ErrorCode::Cancelled);
    for (const auto& path : outputs)
        EXPECT_FALSE(std::filesystem::exists(path));
    expectNoPendingFiles();
}
} // namespace
