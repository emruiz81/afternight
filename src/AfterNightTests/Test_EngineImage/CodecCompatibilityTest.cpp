// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Compositions that give a caller what one facade that loads and saves images would.
//
// Such a facade clips a signed region request to the image, reads a bottom-up file's region from the rows a
// top-down request names and presents it top-down, reduces a probe to four readability statuses, and converts
// samples to what a PNG or JPEG container holds without saying how. Each of those is here as a caller's
// composition of this engine's own operations - the clip written out, the row order read from the description,
// the conversion stated as a policy - checked against the facade's rule, which is written out here independently
// rather than taken from the code under test.

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/DumpCodec.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/IO/PngCodec.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

/// A rectangle anywhere, as a facade's region request states one.
struct SignedRegion
{
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::uint64_t width = 0u;
    std::uint64_t height = 0u;
};

/// Why a request names no rectangle at all.
enum class ClipRefusal : std::uint8_t
{
    None,
    ZeroExtent,
    CoordinateOverflow
};

/// Which axis a refusal is about.
enum class ClipAxis : std::uint8_t
{
    None,
    X,
    Y
};

/// A request clipped to an image, as a facade's region clip reports it.
struct ClippedRegion
{
    std::optional<an::CodecRegion> region{};
    bool wasClipped = false;
    ClipRefusal refusal = ClipRefusal::None;
    ClipAxis axis = ClipAxis::None;
};

/// Where one axis of a request ends, refusing one that is empty or ends past what a signed coordinate holds.
[[nodiscard]] ClipRefusal axisEnd(std::int64_t origin, std::uint64_t extent, std::int64_t& end)
{
    if (extent == 0u)
    {
        return ClipRefusal::ZeroExtent;
    }
    if (origin >= 0)
    {
        const auto available = static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)() - origin);
        if (extent > available)
        {
            return ClipRefusal::CoordinateOverflow;
        }
        end = origin + static_cast<std::int64_t>(extent);
        return ClipRefusal::None;
    }
    const std::uint64_t magnitude = static_cast<std::uint64_t>(-(origin + 1)) + 1u;
    if (extent >= magnitude)
    {
        const std::uint64_t positive = extent - magnitude;
        if (positive > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
        {
            return ClipRefusal::CoordinateOverflow;
        }
        end = static_cast<std::int64_t>(positive);
        return ClipRefusal::None;
    }
    end = -static_cast<std::int64_t>(magnitude - extent);
    return ClipRefusal::None;
}

/// The part of a request that lies on an image. The x axis is checked before the y axis, and a request counts as
/// clipped whenever what it names differs from what it asked for - including when it names nothing.
[[nodiscard]] ClippedRegion clip(const SignedRegion& request, std::uint32_t width, std::uint32_t height)
{
    ClippedRegion clipped;
    std::int64_t endX = 0;
    std::int64_t endY = 0;
    clipped.refusal = axisEnd(request.x, request.width, endX);
    if (clipped.refusal != ClipRefusal::None)
    {
        clipped.axis = ClipAxis::X;
        return clipped;
    }
    clipped.refusal = axisEnd(request.y, request.height, endY);
    if (clipped.refusal != ClipRefusal::None)
    {
        clipped.axis = ClipAxis::Y;
        return clipped;
    }
    const std::int64_t startX = (std::max)(request.x, std::int64_t{0});
    const std::int64_t startY = (std::max)(request.y, std::int64_t{0});
    const std::int64_t stopX = (std::min)(endX, std::int64_t{width});
    const std::int64_t stopY = (std::min)(endY, std::int64_t{height});
    an::CodecRegion named{};
    if (stopX > startX && stopY > startY)
    {
        named = {static_cast<std::uint32_t>(startX), static_cast<std::uint32_t>(startY),
                 static_cast<std::uint32_t>(stopX - startX), static_cast<std::uint32_t>(stopY - startY)};
        clipped.region = named;
    }
    clipped.wasClipped = request.x < 0 || request.y < 0 || static_cast<std::uint64_t>(request.x) != named.x ||
                         static_cast<std::uint64_t>(request.y) != named.y || request.width != named.width ||
                         request.height != named.height;
    return clipped;
}

/// The facade's four readability statuses, onto which a probe's outcome maps one to one.
enum class Readability : std::uint8_t
{
    PayloadValidatedWithoutDecode,
    MetadataReadDecodeRequired,
    MetadataUnreadable,
    PayloadUnreadable,
    Refused
};

[[nodiscard]] std::optional<an::CodecFailure> reasonOf(const an::Status& status)
{
    // The facade reports a codec's own refusal as the cause of the failure it returns.
    const std::optional<an::CodecFailure> cause = an::codecFailureReason(status.cause());
    return cause.has_value() ? cause : an::codecFailureReason(status);
}

[[nodiscard]] Readability readabilityOf(const an::Result<an::CodecImageInfo>& probe)
{
    if (probe.hasValue())
    {
        return probe->disposition == an::CodecProbeDisposition::PayloadValidatedWithoutDecode
                   ? Readability::PayloadValidatedWithoutDecode
                   : Readability::MetadataReadDecodeRequired;
    }
    const std::optional<an::CodecFailure> reason = reasonOf(probe.status());
    if (reason == an::CodecFailure::MetadataUnreadable)
    {
        return Readability::MetadataUnreadable;
    }
    if (reason == an::CodecFailure::PayloadUnreadable)
    {
        return Readability::PayloadUnreadable;
    }
    return Readability::Refused;
}

// The facade's save conversions, written out: a floating sample clamped to [0, 1]
// with NaN read as 0, scaled in float and truncated; an unsigned integer normalised in float and truncated; and
// a signed one offset by half its unsigned range first.
[[nodiscard]] float facadeClamp01(float value)
{
    return value != value ? 0.0f : value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

[[nodiscard]] std::uint16_t facadeFloatToUint16(float value)
{
    return static_cast<std::uint16_t>(65535.0f * facadeClamp01(value));
}

[[nodiscard]] std::uint8_t facadeFloatToUint8(float value)
{
    return static_cast<std::uint8_t>(255.0f * facadeClamp01(value));
}

[[nodiscard]] std::uint16_t facadeDoubleToUint16(double value)
{
    const double clamped = value != value ? 0.0 : value < 0.0 ? 0.0 : value > 1.0 ? 1.0 : value;
    return static_cast<std::uint16_t>(65535.0 * clamped);
}

[[nodiscard]] std::uint8_t facadeUint16ToUint8(std::uint16_t value)
{
    return static_cast<std::uint8_t>(static_cast<float>(value) / 65535.0f * 255.0f);
}

[[nodiscard]] std::uint16_t facadeUint32ToUint16(std::uint32_t value)
{
    return static_cast<std::uint16_t>(static_cast<float>(value) / 4294967295.0f * 65535.0f);
}

[[nodiscard]] std::uint16_t facadeInt32ToUint16(std::int32_t value)
{
    return facadeUint32ToUint16(static_cast<std::uint32_t>(value) + 2147483648u);
}

[[nodiscard]] std::uint16_t facadeInt16ToUint16(std::int16_t value)
{
    return static_cast<std::uint16_t>(value + 32768);
}

[[nodiscard]] std::uint8_t facadeInt8ToUint8(std::int8_t value)
{
    return static_cast<std::uint8_t>(value + 128);
}

/// How far two runs of codes disagree.
struct Disagreement
{
    std::size_t samples = 0u;
    std::int64_t largest = 0;
};

template <typename T>
[[nodiscard]] Disagreement disagreementOf(const std::vector<T>& reference, const std::vector<T>& stated)
{
    Disagreement disagreement;
    for (std::size_t i = 0u; i < reference.size() && i < stated.size(); ++i)
    {
        const std::int64_t difference =
            std::llabs(static_cast<std::int64_t>(reference[i]) - static_cast<std::int64_t>(stated[i]));
        if (difference != 0)
        {
            ++disagreement.samples;
            disagreement.largest = (std::max)(disagreement.largest, difference);
        }
    }
    return disagreement;
}

class CodecCompatibilityTest : public testing::Test
{
protected:
    void SetUp() override
    {
        directory =
            std::filesystem::temp_directory_path() /
            ("an-codec-compatibility-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        auto fits = an::createFitsCodec();
        auto png = an::createPngCodec();
        auto dump = an::createDumpCodec();
        ASSERT_TRUE(fits.hasValue());
        ASSERT_TRUE(png.hasValue());
        ASSERT_TRUE(dump.hasValue());
        std::array codecs{*fits, *png, *dump};
        auto registry = an::CodecRegistry::create(codecs);
        ASSERT_TRUE(registry.hasValue());
        an::EngineContextOptions options;
        options.codecRegistry = *registry;
        auto created = an::EngineContext::create(options);
        ASSERT_TRUE(created.hasValue());
        engine = std::move(*created);
    }

    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }

    [[nodiscard]] std::string path(const char* name) const
    {
        const auto utf8 =
            (directory / std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(name)))).u8string();
        return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
    }

    template <typename T>
    [[nodiscard]] an::RawFrame frame(const std::vector<T>& samples, std::uint32_t width, std::uint32_t height,
                                     an::CoordinateOrigin origin = an::CoordinateOrigin::TopLeft,
                                     std::optional<an::NumericDomain> domain = std::nullopt)
    {
        const auto extent = *an::ImageExtent::create(width, height, 1u, engine.limits());
        const an::RawSampleFormat format{sizeof(T), std::is_floating_point_v<T> ? an::RawSampleClass::FloatingPoint
                                                    : std::is_signed_v<T>       ? an::RawSampleClass::SignedInteger
                                                                                : an::RawSampleClass::UnsignedInteger};
        const an::NumericDomain stated = domain.value_or(
            std::is_floating_point_v<T> ? an::NumericDomain::PhysicalScaled : an::NumericDomain::RawSensorCode);
        const auto descriptor =
            *an::RawFrameDescriptor::create({extent, format, an::ByteOrder::Native, an::ChannelSchema::Mono,
                                             an::ColorSpace::Unspecified, stated, an::CfaDescriptor{}, origin, 1.0});
        const auto layout = *an::ImageLayout::create({extent, sizeof(T), 1u, 1u, 1u}, engine.limits());
        auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
        auto raw = an::RawFrame::create(descriptor, layout, std::move(*buffer));
        std::memcpy((*raw->mutableBytes()).data(), samples.data(), samples.size() * sizeof(T));
        return std::move(*raw);
    }

    [[nodiscard]] an::Result<an::OutputReport> write(const char* name, const an::RawFrame& source,
                                                     an::CodecFormat format = an::CodecFormat::Unknown)
    {
        auto execution = an::ExecutionContext::create(engine, {});
        if (!execution.hasValue())
        {
            return execution.status();
        }
        const std::string destination = path(name);
        return an::writeImage(engine, *execution, {.destinationPath = destination, .format = format}, source, {});
    }

    template <typename T> [[nodiscard]] static std::vector<T> samplesOf(const an::RawFrame& frame)
    {
        const an::ImageLayout layout = *frame.layout();
        std::vector<T> samples;
        for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
        {
            for (std::uint32_t x = 0u; x < layout.extent().width(); ++x)
            {
                T value;
                std::memcpy(&value, frame.bytes().data() + y * layout.rowStrideBytes() + x * sizeof(T), sizeof(T));
                samples.push_back(value);
            }
        }
        return samples;
    }

    /// What loading a signed region found, as a facade's region load reports it.
    struct RegionLoad
    {
        ClippedRegion clipped{};
        an::CoordinateOrigin origin = an::CoordinateOrigin::TopLeft;
        std::optional<an::CodecDecodeResult> decoded{};
        std::uint64_t logicalBytes = 0u;
    };

    /// Clips a request to the image a probe describes, and decodes only the rows and columns it names.
    [[nodiscard]] RegionLoad loadRegion(const char* name, an::CodecFormat format, const SignedRegion& request)
    {
        RegionLoad load;
        auto execution = an::ExecutionContext::create(engine, {});
        EXPECT_TRUE(execution.hasValue());
        const std::string source = path(name);
        const an::Result<an::CodecImageInfo> probe =
            an::probeImage(engine, *execution, {.path = source, .format = format, .validatePayload = false});
        EXPECT_TRUE(probe.hasValue()) << probe.status().message();
        if (!probe.hasValue())
        {
            return load;
        }
        const an::ImageExtent extent = probe->descriptor.extent();
        load.origin = probe->descriptor.coordinateOrigin();
        load.clipped = clip(request, extent.width(), extent.height());
        if (!load.clipped.region.has_value())
        {
            return load;
        }
        // A request names rows from the top, and a bottom-up file stores its first row at the bottom.
        an::CodecRegion native = *load.clipped.region;
        if (load.origin == an::CoordinateOrigin::BottomLeft)
        {
            native.y = extent.height() - (native.y + native.height);
        }
        an::Result<an::CodecDecodeResult> decoded =
            an::decodeImageRegion(engine, *execution, {.path = source, .format = format}, native);
        EXPECT_TRUE(decoded.hasValue()) << decoded.status().message();
        if (!decoded.hasValue())
        {
            return load;
        }
        load.logicalBytes = std::uint64_t{native.width} * native.height * extent.channelCount() *
                            probe->descriptor.sampleFormat().sizeBytes;
        load.decoded = std::move(*decoded);
        return load;
    }

    /// A one-plane frame stored bottom row first, presented top row first.
    template <typename T> [[nodiscard]] std::vector<T> presentedTopDown(const an::RawFrame& stored)
    {
        auto execution = an::ExecutionContext::create(engine, {});
        EXPECT_TRUE(execution.hasValue());
        const std::optional<an::ImageDescriptor> descriptor = stored.descriptor()->toImageDescriptor();
        EXPECT_TRUE(descriptor.has_value());
        const an::ImageLayout layout = *stored.layout();
        const an::Result<an::GeometryPlan> plan =
            an::planOrientation(*descriptor, an::ImageOrientation::MirrorVertical);
        EXPECT_TRUE(plan.hasValue()) << plan.status().message();
        const an::ImageLayout flat =
            *an::ImageLayout::create({layout.extent(), sizeof(T), 1u, 1u, 1u}, engine.limits());
        std::vector<T> presented(static_cast<std::size_t>(layout.extent().width()) * layout.extent().height());
        an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, layout.extent());
        EXPECT_TRUE(mask.hasValue());
        const std::optional<an::ValidityMaskView<std::uint8_t>> view = mask->view();
        EXPECT_TRUE(view.has_value());
        const an::Result<an::GeometryResult> ran = an::transformImageGeometry(
            *execution, an::kSampleTypeOf<T>, stored.bytes().data(), layout, presented.data(), flat, *plan, {}, *view);
        EXPECT_TRUE(ran.hasValue()) << ran.status().message();
        return presented;
    }

    /// Converts a run of samples under a policy, as a caller does before writing a container that holds fewer
    /// representations than the image.
    template <typename SourceT, typename DestinationT>
    [[nodiscard]] std::vector<DestinationT> converted(std::vector<SourceT> samples, const an::ConversionPolicy& policy)
    {
        constexpr std::uint32_t kRow = 4096u;
        const std::size_t count = samples.size();
        const auto rows = static_cast<std::uint32_t>((count + kRow - 1u) / kRow);
        samples.resize(static_cast<std::size_t>(kRow) * rows);
        auto execution = an::ExecutionContext::create(engine, {});
        EXPECT_TRUE(execution.hasValue());
        const auto extent = *an::ImageExtent::create(kRow, rows, 1u, engine.limits());
        const auto sourceLayout = *an::ImageLayout::create({extent, sizeof(SourceT), 1u, 1u, 1u}, engine.limits());
        const auto destinationLayout =
            *an::ImageLayout::create({extent, sizeof(DestinationT), 1u, 1u, 1u}, engine.limits());
        std::vector<DestinationT> destination(samples.size());
        an::ConversionCounters counters;
        const an::Status status = an::convertImageSamples(*execution, an::kSampleTypeOf<SourceT>, samples.data(),
                                                          sourceLayout, an::kSampleTypeOf<DestinationT>,
                                                          destination.data(), destinationLayout, policy, counters);
        EXPECT_FALSE(status.failed()) << status.message();
        destination.resize(count);
        return destination;
    }

    std::filesystem::path directory;
    an::EngineContext engine;
};

TEST_F(CodecCompatibilityTest, ASignedRequestIsClippedToTheImageAndOnlyTheClippedSamplesAreRead)
{
    const std::uint32_t width = 7u;
    const std::uint32_t height = 5u;
    std::vector<std::uint16_t> picture(static_cast<std::size_t>(width) * height);
    for (std::size_t i = 0u; i < picture.size(); ++i)
    {
        picture[i] = static_cast<std::uint16_t>(1000u + i);
    }
    ASSERT_TRUE(write("frame.fits", frame(picture, width, height)).hasValue());

    struct Case
    {
        SignedRegion request;
        std::optional<an::CodecRegion> expected;
        bool clipped;
    };
    const std::array<Case, 6u> cases = {{
        {{1, 1, 3u, 2u}, an::CodecRegion{1u, 1u, 3u, 2u}, false},
        {{0, 0, 7u, 5u}, an::CodecRegion{0u, 0u, 7u, 5u}, false},
        {{-2, 1, 5u, 10u}, an::CodecRegion{0u, 1u, 3u, 4u}, true},
        {{5, -3, 4u, 4u}, an::CodecRegion{5u, 0u, 2u, 1u}, true},
        {{7, 0, 2u, 2u}, std::nullopt, true},
        {{-4, -4, 4u, 4u}, std::nullopt, true},
    }};
    for (const Case& entry : cases)
    {
        SCOPED_TRACE(testing::Message() << entry.request.x << "," << entry.request.y << " " << entry.request.width
                                        << "x" << entry.request.height);
        const RegionLoad load = loadRegion("frame.fits", an::CodecFormat::Unknown, entry.request);
        EXPECT_EQ(load.clipped.refusal, ClipRefusal::None);
        EXPECT_EQ(load.clipped.wasClipped, entry.clipped);
        ASSERT_EQ(load.clipped.region.has_value(), entry.expected.has_value());
        if (!entry.expected.has_value())
        {
            // Nothing overlaps, so nothing is decoded: the result is empty rather than a refusal.
            EXPECT_FALSE(load.decoded.has_value());
            continue;
        }
        const an::CodecRegion& region = *load.clipped.region;
        EXPECT_EQ(region.x, entry.expected->x);
        EXPECT_EQ(region.y, entry.expected->y);
        EXPECT_EQ(region.width, entry.expected->width);
        EXPECT_EQ(region.height, entry.expected->height);
        ASSERT_TRUE(load.decoded.has_value());
        EXPECT_EQ(load.logicalBytes, std::uint64_t{region.width} * region.height * sizeof(std::uint16_t));
        std::vector<std::uint16_t> expected;
        for (std::uint32_t y = region.y; y < region.y + region.height; ++y)
        {
            for (std::uint32_t x = region.x; x < region.x + region.width; ++x)
            {
                expected.push_back(picture[static_cast<std::size_t>(y) * width + x]);
            }
        }
        EXPECT_EQ(samplesOf<std::uint16_t>(load.decoded->frame), expected);
    }
}

TEST_F(CodecCompatibilityTest, AnEmptyOrOverflowingRequestIsRefusedOnTheAxisItNames)
{
    constexpr std::int64_t kLargest = (std::numeric_limits<std::int64_t>::max)();
    constexpr std::int64_t kSmallest = (std::numeric_limits<std::int64_t>::min)();
    const ClippedRegion noWidth = clip({0, 0, 0u, 3u}, 7u, 5u);
    EXPECT_EQ(noWidth.refusal, ClipRefusal::ZeroExtent);
    EXPECT_EQ(noWidth.axis, ClipAxis::X);
    const ClippedRegion noHeight = clip({0, 0, 3u, 0u}, 7u, 5u);
    EXPECT_EQ(noHeight.refusal, ClipRefusal::ZeroExtent);
    EXPECT_EQ(noHeight.axis, ClipAxis::Y);
    const ClippedRegion pastRight = clip({kLargest, 0, 2u, 1u}, 7u, 5u);
    EXPECT_EQ(pastRight.refusal, ClipRefusal::CoordinateOverflow);
    EXPECT_EQ(pastRight.axis, ClipAxis::X);
    const ClippedRegion pastBottom = clip({0, kLargest - 1, 1u, 2u}, 7u, 5u);
    EXPECT_EQ(pastBottom.refusal, ClipRefusal::CoordinateOverflow);
    EXPECT_EQ(pastBottom.axis, ClipAxis::Y);

    // The widest request the arithmetic holds is not refused: it is clipped to the image.
    const ClippedRegion widest = clip({kSmallest, 0, (std::numeric_limits<std::uint64_t>::max)(), 1u}, 7u, 5u);
    EXPECT_EQ(widest.refusal, ClipRefusal::None);
    ASSERT_TRUE(widest.region.has_value());
    EXPECT_EQ(widest.region->x, 0u);
    EXPECT_EQ(widest.region->width, 7u);
    EXPECT_EQ(widest.region->height, 1u);
    EXPECT_TRUE(widest.wasClipped);
}

TEST_F(CodecCompatibilityTest, ABottomUpRegionIsReadFromTheRowsATopDownRequestNamesAndPresentedTopDown)
{
    const std::uint32_t width = 6u;
    const std::uint32_t height = 4u;
    // The picture as it is seen, top row first, and as a bottom-up file stores it, bottom row first.
    std::vector<float> seen(static_cast<std::size_t>(width) * height);
    std::vector<float> stored(seen.size());
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            seen[static_cast<std::size_t>(y) * width + x] =
                100.0f + static_cast<float>(y) * 10.0f + static_cast<float>(x);
        }
    }
    for (std::uint32_t row = 0u; row < height; ++row)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            stored[static_cast<std::size_t>(row) * width + x] =
                seen[static_cast<std::size_t>(height - 1u - row) * width + x];
        }
    }
    ASSERT_TRUE(write("bottom-up.fits", frame(stored, width, height, an::CoordinateOrigin::BottomLeft)).hasValue());

    const RegionLoad load = loadRegion("bottom-up.fits", an::CodecFormat::Unknown, {1, 1, 3u, 2u});
    EXPECT_EQ(load.origin, an::CoordinateOrigin::BottomLeft);
    EXPECT_FALSE(load.clipped.wasClipped);
    ASSERT_TRUE(load.decoded.has_value());
    std::vector<float> expected;
    for (std::uint32_t y = 1u; y < 3u; ++y)
    {
        for (std::uint32_t x = 1u; x < 4u; ++x)
        {
            expected.push_back(seen[static_cast<std::size_t>(y) * width + x]);
        }
    }
    EXPECT_EQ(presentedTopDown<float>(load.decoded->frame), expected);
}

TEST_F(CodecCompatibilityTest, ADumpRegionIsClippedAndReadTheSameWay)
{
    const std::uint32_t width = 5u;
    const std::uint32_t height = 4u;
    std::vector<float> picture(static_cast<std::size_t>(width) * height);
    for (std::size_t i = 0u; i < picture.size(); ++i)
    {
        picture[i] = 0.25f * static_cast<float>(i);
    }
    ASSERT_TRUE(write("frame.dump", frame(picture, width, height), an::CodecFormat::Dump).hasValue());

    const RegionLoad load = loadRegion("frame.dump", an::CodecFormat::Dump, {-1, 2, 3u, 5u});
    ASSERT_TRUE(load.clipped.region.has_value());
    EXPECT_TRUE(load.clipped.wasClipped);
    EXPECT_EQ(load.clipped.region->x, 0u);
    EXPECT_EQ(load.clipped.region->y, 2u);
    EXPECT_EQ(load.clipped.region->width, 2u);
    EXPECT_EQ(load.clipped.region->height, 2u);
    ASSERT_TRUE(load.decoded.has_value());
    EXPECT_EQ(load.logicalBytes, 2u * 2u * sizeof(float));
    EXPECT_EQ(samplesOf<float>(load.decoded->frame),
              (std::vector<float>{picture[10], picture[11], picture[15], picture[16]}));
}

TEST_F(CodecCompatibilityTest, EveryProbeOutcomeIsOneOfTheFourReadabilityStatuses)
{
    std::vector<std::uint8_t> ramp(64u * 64u);
    for (std::size_t i = 0u; i < ramp.size(); ++i)
    {
        ramp[i] = static_cast<std::uint8_t>(i % 251u);
    }
    ASSERT_TRUE(
        write("frame.png", frame(ramp, 64u, 64u, an::CoordinateOrigin::TopLeft, an::NumericDomain::EncodedNonlinear))
            .hasValue());
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());

    // A header-only probe establishes only that the header is readable.
    const std::string readable = path("frame.png");
    EXPECT_EQ(readabilityOf(an::probeImage(engine, *execution, {.path = readable, .validatePayload = false})),
              Readability::MetadataReadDecodeRequired);

    {
        std::ofstream broken(
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path("broken.png").data()),
                                                     path("broken.png").size())),
            std::ios::binary);
        broken << "not a container at all";
    }
    EXPECT_EQ(readabilityOf(an::probeImage(engine, *execution, {.path = path("broken.png")})),
              Readability::MetadataUnreadable);

    // A truncated payload is unreadable, whether the probe finds that out itself or the decode it defers to does.
    const std::filesystem::path native(
        std::u8string_view(reinterpret_cast<const char8_t*>(readable.data()), readable.size()));
    std::error_code error;
    std::filesystem::resize_file(native, std::filesystem::file_size(native) / 2u, error);
    ASSERT_FALSE(error);
    const Readability truncated = readabilityOf(an::probeImage(engine, *execution, {.path = readable}));
    if (truncated == Readability::MetadataReadDecodeRequired)
    {
        const auto decoded = an::decodeImage(engine, *execution, {.path = readable});
        ASSERT_FALSE(decoded.hasValue());
        EXPECT_EQ(reasonOf(decoded.status()), an::CodecFailure::PayloadUnreadable);
    }
    else
    {
        EXPECT_EQ(truncated, Readability::PayloadUnreadable);
    }
}

TEST_F(CodecCompatibilityTest, ASaveConversionIsTheFacadesConversionStatedAsAPolicy)
{
    // Truncation, saturation, NaN to zero and corresponding full scales: the facade's save conversions stated as
    // the five decisions, with signed samples offset by half their unsigned range.
    const an::ConversionPolicy truncating{.rounding = an::RoundingMode::TowardZero};
    const an::ConversionPolicy truncatingOffset{.rounding = an::RoundingMode::TowardZero,
                                                .signedConvention = an::SignedIntegerConvention::OffsetBinary};

    std::vector<float> floats = {std::numeric_limits<float>::quiet_NaN(),
                                 -0.5f,
                                 -0.0f,
                                 0.0f,
                                 1e-7f,
                                 0.5f,
                                 1.0f,
                                 1.5f,
                                 std::numeric_limits<float>::infinity(),
                                 -std::numeric_limits<float>::infinity()};
    const std::size_t specials = floats.size();
    for (std::uint32_t code = 0u; code <= 65535u; ++code)
    {
        const float exact = static_cast<float>(code) / 65535.0f;
        floats.push_back(std::nextafter(exact, 0.0f));
        floats.push_back(exact);
        floats.push_back(std::nextafter(exact, 2.0f));
    }
    for (std::uint32_t step = 0u; step < (1u << 20u); ++step)
    {
        floats.push_back(static_cast<float>(step) / static_cast<float>((1u << 20u) - 1u));
    }

    std::vector<std::uint16_t> facadeSixteen;
    std::vector<std::uint8_t> facadeEight;
    for (const float value : floats)
    {
        facadeSixteen.push_back(facadeFloatToUint16(value));
        facadeEight.push_back(facadeFloatToUint8(value));
    }
    const std::vector<std::uint16_t> statedSixteen = converted<float, std::uint16_t>(floats, truncating);
    const std::vector<std::uint8_t> statedEight = converted<float, std::uint8_t>(floats, truncating);
    // The special values agree exactly; elsewhere only the width of the product can move a code, by one.
    for (std::size_t i = 0u; i < specials; ++i)
    {
        EXPECT_EQ(statedSixteen[i], facadeSixteen[i]) << i;
        EXPECT_EQ(statedEight[i], facadeEight[i]) << i;
    }
    const Disagreement floatSixteen = disagreementOf(facadeSixteen, statedSixteen);
    const Disagreement floatEight = disagreementOf(facadeEight, statedEight);
    EXPECT_LE(floatSixteen.largest, 1);
    EXPECT_LE(floatEight.largest, 1);
    RecordProperty("floatToUint16Differing", static_cast<int>(floatSixteen.samples));
    RecordProperty("floatToUint8Differing", static_cast<int>(floatEight.samples));
    RecordProperty("floatSamples", static_cast<int>(floats.size()));

    std::vector<double> doubles;
    for (std::uint32_t step = 0u; step < (1u << 20u); ++step)
    {
        doubles.push_back(static_cast<double>(step) / static_cast<double>((1u << 20u) - 1u));
    }
    doubles.push_back(std::numeric_limits<double>::quiet_NaN());
    doubles.push_back(-1.0);
    doubles.push_back(2.0);
    std::vector<std::uint16_t> facadeDoubles;
    for (const double value : doubles)
    {
        facadeDoubles.push_back(facadeDoubleToUint16(value));
    }
    EXPECT_EQ(disagreementOf(facadeDoubles, converted<double, std::uint16_t>(doubles, truncating)).samples, 0u);

    std::vector<std::int16_t> shorts;
    std::vector<std::uint16_t> facadeShorts;
    for (std::int32_t value = -32768; value <= 32767; ++value)
    {
        shorts.push_back(static_cast<std::int16_t>(value));
        facadeShorts.push_back(facadeInt16ToUint16(static_cast<std::int16_t>(value)));
    }
    EXPECT_EQ(disagreementOf(facadeShorts, converted<std::int16_t, std::uint16_t>(shorts, truncatingOffset)).samples,
              0u);

    std::vector<std::int8_t> bytes;
    std::vector<std::uint8_t> facadeBytes;
    for (std::int32_t value = -128; value <= 127; ++value)
    {
        bytes.push_back(static_cast<std::int8_t>(value));
        facadeBytes.push_back(facadeInt8ToUint8(static_cast<std::int8_t>(value)));
    }
    EXPECT_EQ(disagreementOf(facadeBytes, converted<std::int8_t, std::uint8_t>(bytes, truncatingOffset)).samples, 0u);

    std::vector<std::uint16_t> sixteens;
    std::vector<std::uint8_t> facadeNarrowed;
    for (std::uint32_t value = 0u; value <= 65535u; ++value)
    {
        sixteens.push_back(static_cast<std::uint16_t>(value));
        facadeNarrowed.push_back(facadeUint16ToUint8(static_cast<std::uint16_t>(value)));
    }
    const Disagreement narrowed =
        disagreementOf(facadeNarrowed, converted<std::uint16_t, std::uint8_t>(sixteens, truncating));
    EXPECT_LE(narrowed.largest, 1);
    RecordProperty("uint16ToUint8Differing", static_cast<int>(narrowed.samples));

    std::vector<std::uint32_t> wide;
    std::vector<std::int32_t> signedWide;
    std::vector<std::uint16_t> facadeWide;
    std::vector<std::uint16_t> facadeSignedWide;
    for (std::uint64_t value = 0u; value <= 0xffffffffu; value += 4099u)
    {
        wide.push_back(static_cast<std::uint32_t>(value));
        facadeWide.push_back(facadeUint32ToUint16(static_cast<std::uint32_t>(value)));
        signedWide.push_back(static_cast<std::int32_t>(static_cast<std::uint32_t>(value)));
        facadeSignedWide.push_back(facadeInt32ToUint16(static_cast<std::int32_t>(static_cast<std::uint32_t>(value))));
    }
    const Disagreement unsignedWide =
        disagreementOf(facadeWide, converted<std::uint32_t, std::uint16_t>(wide, truncating));
    EXPECT_LE(unsignedWide.largest, 1);
    RecordProperty("uint32ToUint16Differing", static_cast<int>(unsignedWide.samples));
    // A signed 32-bit sample reaches sixteen bits the way the facade takes it there: offset into the unsigned type
    // of its own width, then scaled.
    const Disagreement signedToSixteen = disagreementOf(
        facadeSignedWide, converted<std::uint32_t, std::uint16_t>(
                              converted<std::int32_t, std::uint32_t>(signedWide, truncatingOffset), truncating));
    EXPECT_LE(signedToSixteen.largest, 1);
    RecordProperty("int32ToUint16Differing", static_cast<int>(signedToSixteen.samples));
    RecordProperty("wideSamples", static_cast<int>(wide.size()));

    // The converted samples are what the container then stores exactly.
    std::vector<float> picture(64u * 64u);
    for (std::size_t i = 0u; i < picture.size(); ++i)
    {
        picture[i] = static_cast<float>(i) / static_cast<float>(picture.size() - 1u);
    }
    const std::vector<std::uint16_t> codes = converted<float, std::uint16_t>(picture, truncating);
    ASSERT_TRUE(write("converted.png",
                      frame(codes, 64u, 64u, an::CoordinateOrigin::TopLeft, an::NumericDomain::NormalizedLinear))
                    .hasValue());
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto decoded = an::decodeImage(engine, *execution, {.path = path("converted.png")});
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
    EXPECT_EQ(samplesOf<std::uint16_t>(decoded->frame), codes);
}

} // namespace
