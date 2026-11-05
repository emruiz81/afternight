// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The shared buffer manager over assets: an allocation's planar strides and mapping, a buffer opened and its bytes
/// shared, a descriptor outside the managed root refused, stale sessions removed, concurrent allocations; an export
/// in the image's own representation, the four representations the protocol carries and the four it refuses by name,
/// a mosaic's Bayer pattern with its phase and an X-Trans refused, the colour space read from the semantics; an import
/// that copies into engine memory and lets the mapping go, a borrowed frame that cannot be adopted and keeps its
/// mapping alive, a read-only borrow, the strides the protocol holds a descriptor to, a Bayer and an sRGB import
/// described; the sidecar's schema and its round trip, a region export's bounds and moved solution; and the cost of
/// the copies.

#include "Test_ControlsExtensionSeam/ExtensionSeamTestSupport.h"

#include "app/SharedBufferManager.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleBuffer.h>

#include <QJsonArray>
#include <QStringList>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace
{

using namespace an::tests::seam;
using an::ui::extensions::ImageExportRequest;
using an::ui::extensions::SharedBufferAccessMode;
using an::ui::extensions::SharedBufferColorSpace;
using an::ui::extensions::SharedBufferDescriptor;
using an::ui::extensions::SharedBufferManager;
using an::ui::extensions::SharedBufferPixelFormat;
using an::ui::extensions::SharedBufferPixelType;
using an::ui::extensions::SharedBufferRole;
using an::ui::extensions::SharedImageBuffer;
using an::ui::extensions::SharedImageBufferSpec;

class SharedBufferSeamTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_provider = an::tests::controls::openProvider(2u);
        ASSERT_TRUE(m_provider.has_value());
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_provider->engineSession();
    }

    [[nodiscard]] SharedBufferManager manager() const
    {
        return SharedBufferManager(m_root.path());
    }

    [[nodiscard]] static SharedImageBufferSpec spec(const std::string& sessionId, const std::string& bufferId,
                                                    int width = 16, int height = 8, int channels = 3)
    {
        SharedImageBufferSpec made;
        made.sessionId = sessionId;
        made.bufferId = bufferId;
        made.symbolicName = "selection";
        made.width = width;
        made.height = height;
        made.channels = channels;
        made.pixelType = SharedBufferPixelType::Float32;
        made.pixelFormat = channels == 1 ? SharedBufferPixelFormat::Mono : SharedBufferPixelFormat::RGB;
        made.colorSpace = SharedBufferColorSpace::Linear;
        made.accessMode = SharedBufferAccessMode::ReadWrite;
        made.bufferRole = SharedBufferRole::Preview;
        return made;
    }

    [[nodiscard]] static ImageExportRequest exportRequest(const std::string& name = "source")
    {
        ImageExportRequest request;
        request.sessionId = "session-image";
        request.role = SharedBufferRole::Source;
        request.bufferId = name + "-buffer";
        request.symbolicName = name;
        return request;
    }

    /// Exports an asset under its read lease; fails the case when the export is refused.
    [[nodiscard]] std::optional<SharedImageBuffer> exported(const an::consumer::ImageAsset& asset,
                                                            ImageExportRequest request = exportRequest()) const
    {
        an::Result<an::consumer::AssetReadLease> lease = asset.read();
        EXPECT_TRUE(lease.hasValue()) << lease.status().message();
        if (!lease.hasValue())
        {
            return std::nullopt;
        }
        an::Result<SharedImageBuffer> buffer = manager().exportImage(lease->image(), request);
        EXPECT_TRUE(buffer.hasValue()) << buffer.status().message();
        if (!buffer.hasValue())
        {
            return std::nullopt;
        }
        return std::move(*buffer);
    }

    /// The refusal an export of an asset answers; fails the case when the export succeeds.
    [[nodiscard]] an::Status exportRefusal(const an::consumer::ImageAsset& asset) const
    {
        an::Result<an::consumer::AssetReadLease> lease = asset.read();
        EXPECT_TRUE(lease.hasValue()) << lease.status().message();
        if (!lease.hasValue())
        {
            return lease.status();
        }
        an::Result<SharedImageBuffer> buffer = manager().exportImage(lease->image(), exportRequest());
        EXPECT_FALSE(buffer.hasValue());
        return buffer.status();
    }

    std::optional<an::tests::controls::TestSessionProvider> m_provider;
    SeamRoot m_root;
};

[[nodiscard]] std::optional<an::CfaDescriptor> bayer(an::CfaPattern pattern, std::uint32_t originX = 0u,
                                                     std::uint32_t originY = 0u)
{
    return an::CfaDescriptor::create(pattern, originX, originY);
}

/// The X-Trans filter table, row-major from its origin.
[[nodiscard]] an::CfaColorEntries xtransEntries()
{
    using an::CfaColor;
    const CfaColor g = CfaColor::Green;
    const CfaColor r = CfaColor::Red;
    const CfaColor b = CfaColor::Blue;
    const std::array<CfaColor, an::kCfaTableEntries> pattern = {g, b, g, g, r, g, r, g, r, b, g, b, g, b, g, g, r, g,
                                                                g, r, g, g, b, g, b, g, b, r, g, r, g, r, g, g, b, g};
    an::CfaColorEntries entries{};
    for (std::size_t index = 0u; index < an::kCfaTableEntries; ++index)
    {
        entries[index] = pattern[index];
    }
    return entries;
}

} // namespace

// ---- Allocation, mapping and the root.

TEST_F(SharedBufferSeamTest, TheDefaultRootIsTheSharedBuffersDirectory)
{
    const std::filesystem::path root = SharedBufferManager::defaultSharedBufferRootPath();
    EXPECT_FALSE(root.empty());
    EXPECT_EQ(root.filename().string(), "SharedBuffers");
}

TEST_F(SharedBufferSeamTest, AnAllocationIsPlanarWithTightStridesAndWritable)
{
    an::Result<SharedImageBuffer> buffer = manager().allocateBuffer(spec("session-a", "buffer-a"));
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();
    ASSERT_TRUE(buffer->valid());
    EXPECT_TRUE(std::filesystem::exists(buffer->filePath()));

    const SharedBufferDescriptor& descriptor = buffer->descriptor();
    EXPECT_EQ(descriptor.buffer_id, "buffer-a");
    EXPECT_EQ(descriptor.width, 16);
    EXPECT_EQ(descriptor.height, 8);
    EXPECT_EQ(descriptor.channels, 3);
    EXPECT_EQ(descriptor.row_stride, 16u * 4u);
    EXPECT_EQ(descriptor.plane_stride, descriptor.row_stride * 8u);
    EXPECT_EQ(descriptor.byte_size, descriptor.plane_stride * 3u);
    EXPECT_EQ(descriptor.buffer_role, SharedBufferRole::Preview);
    EXPECT_EQ(descriptor.access_mode, SharedBufferAccessMode::ReadWrite);
    EXPECT_EQ(buffer->mappedSize(), descriptor.byte_size);

    auto* bytes = reinterpret_cast<std::uint8_t*>(buffer->bytes());
    ASSERT_NE(bytes, nullptr);
    bytes[0] = 17;
    bytes[descriptor.byte_size - 1] = 91;
    EXPECT_EQ(bytes[0], 17);
    EXPECT_EQ(bytes[descriptor.byte_size - 1], 91);
}

TEST_F(SharedBufferSeamTest, AnOpenedBufferSharesTheAllocationsBytes)
{
    an::Result<SharedImageBuffer> writer = manager().allocateBuffer(spec("session-a", "buffer-a"));
    ASSERT_TRUE(writer.hasValue()) << writer.status().message();
    auto* written = reinterpret_cast<std::uint8_t*>(writer->bytes());
    for (std::uint64_t i = 0; i < writer->mappedSize(); ++i)
    {
        written[i] = static_cast<std::uint8_t>(i % 251u);
    }

    an::Result<SharedImageBuffer> reader = manager().openBuffer(writer->descriptor());
    ASSERT_TRUE(reader.hasValue()) << reader.status().message();
    ASSERT_TRUE(reader->valid());
    EXPECT_EQ(reader->mappedSize(), writer->mappedSize());
    const auto* read = reinterpret_cast<const std::uint8_t*>(reader->bytes());
    EXPECT_EQ(read[0], 0);
    EXPECT_EQ(read[1], 1);
    EXPECT_EQ(read[250], 250);
    EXPECT_EQ(read[251], 0);
}

TEST_F(SharedBufferSeamTest, ADescriptorOutsideTheManagedRootIsRefused)
{
    an::Result<SharedImageBuffer> buffer = manager().allocateBuffer(spec("session-a", "buffer-a", 4, 4, 1));
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();

    SharedBufferDescriptor elsewhere = buffer->descriptor();
    elsewhere.mapped_file_path = (std::filesystem::temp_directory_path() / "rogue-shared-buffer.bin").string();
    an::Result<SharedImageBuffer> opened = manager().openBuffer(elsewhere);
    ASSERT_FALSE(opened.hasValue());
    EXPECT_EQ(opened.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_NE(std::string(opened.status().message()).find("outside the managed root"), std::string::npos);

    SharedBufferDescriptor traversal = buffer->descriptor();
    traversal.mapped_file_path = (m_root.path() / ".." / "escape.bin").generic_string();
    opened = manager().openBuffer(traversal);
    ASSERT_FALSE(opened.hasValue());
    EXPECT_NE(std::string(opened.status().message()).find("outside"), std::string::npos);

    an::Result<an::ui::extensions::ImportedImage> imported = manager().importImage(session(), elsewhere);
    ASSERT_FALSE(imported.hasValue());
    EXPECT_EQ(imported.status().code(), an::ErrorCode::InvalidArgument);
}

TEST_F(SharedBufferSeamTest, StaleSessionsAreRemovedAndLiveOnesKept)
{
    {
        an::Result<SharedImageBuffer> a = manager().allocateBuffer(spec("session-a", "buffer-a"));
        an::Result<SharedImageBuffer> b = manager().allocateBuffer(spec("session-b", "buffer-b"));
        ASSERT_TRUE(a.hasValue() && b.hasValue());
    }

    an::Result<an::ui::extensions::SharedBufferCleanupResult> cleanup = manager().cleanupStaleSessions({"session-a"});
    ASSERT_TRUE(cleanup.hasValue()) << cleanup.status().message();
    EXPECT_EQ(cleanup->removedSessionCount, 1);
    EXPECT_EQ(cleanup->removedBufferCount, 1);
    EXPECT_TRUE(std::filesystem::exists(m_root.path() / "session-a"));
    EXPECT_FALSE(std::filesystem::exists(m_root.path() / "session-b"));

    const an::Status removed = manager().removeSessionBuffers("session-a");
    EXPECT_TRUE(removed.ok()) << removed.message();
    EXPECT_FALSE(std::filesystem::exists(m_root.path() / "session-a"));
    EXPECT_TRUE(manager().removeSessionBuffers("session-a").ok()) << "removing nothing is not a failure";
    EXPECT_EQ(manager().removeSessionBuffers("").code(), an::ErrorCode::InvalidArgument);
}

TEST_F(SharedBufferSeamTest, ConcurrentAllocationsMakeDistinctSessionBuffers)
{
    const SharedBufferManager shared = manager();
    std::array<an::Status, 4> results = {an::Status::success(), an::Status::success(), an::Status::success(),
                                         an::Status::success()};
    std::vector<std::thread> workers;
    for (std::size_t i = 0; i < results.size(); ++i)
    {
        workers.emplace_back([&, i]() {
            an::Result<SharedImageBuffer> buffer = shared.allocateBuffer(
                spec("session-" + std::to_string(i), "buffer-" + std::to_string(i), 8 + static_cast<int>(i), 8, 1));
            if (!buffer.hasValue())
            {
                results[i] = buffer.status();
                return;
            }
            reinterpret_cast<std::uint8_t*>(buffer->bytes())[0] = static_cast<std::uint8_t>(10 + i);
        });
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }
    for (std::size_t i = 0; i < results.size(); ++i)
    {
        EXPECT_TRUE(results[i].ok()) << results[i].message();
        EXPECT_TRUE(std::filesystem::exists(m_root.path() / ("session-" + std::to_string(i))));
    }
}

// ---- Export: the format an image states.

TEST_F(SharedBufferSeamTest, AnRgbFloatExportIsPlanarInTheImagesRepresentation)
{
    SeamImageSpec image;
    image.channels = Channels::Rgb;
    std::optional<an::consumer::ImageAsset> asset = makeSeamAsset<float>(
        session(), image, [](std::uint32_t, std::uint32_t, std::uint32_t channel) { return 0.1f + 0.4f * channel; });
    ASSERT_TRUE(asset.has_value());

    std::optional<SharedImageBuffer> buffer = exported(*asset);
    ASSERT_TRUE(buffer.has_value());
    EXPECT_EQ(buffer->descriptor().symbolic_name, "source");
    EXPECT_EQ(buffer->descriptor().pixel_type, SharedBufferPixelType::Float32);
    EXPECT_EQ(buffer->descriptor().pixel_format, SharedBufferPixelFormat::RGB);
    EXPECT_EQ(buffer->descriptor().color_space, SharedBufferColorSpace::Linear);
    EXPECT_EQ(buffer->descriptor().width, 4);
    EXPECT_EQ(buffer->descriptor().height, 3);
    EXPECT_EQ(buffer->descriptor().channels, 3);
    EXPECT_FALSE(buffer->descriptor().offset_x.has_value());

    const auto* pixels = reinterpret_cast<const float*>(buffer->bytes());
    EXPECT_FLOAT_EQ(pixels[0], 0.1f);
    EXPECT_FLOAT_EQ(pixels[12], 0.5f);
    EXPECT_FLOAT_EQ(pixels[24], 0.9f);
    // An image with no cards and no file still has a sidecar, stating what the buffer is: what an extension sets on a
    // result lands in the sidecar of the buffer it answers.
    ASSERT_FALSE(buffer->descriptor().metadata_sidecar.empty());
    const QJsonObject root = readSidecar(buffer->descriptor().metadata_sidecar);
    EXPECT_EQ(root.value(QStringLiteral("NAXIS")).toString(), QStringLiteral("3"));
    EXPECT_EQ(root.value(QStringLiteral("NAXIS1")).toString(), QStringLiteral("4"));
    EXPECT_EQ(root.value(QStringLiteral("BITPIX")).toString(), QStringLiteral("-32"));
    EXPECT_FALSE(root.contains(QStringLiteral("FILENAME")));
    EXPECT_FALSE(root.contains(QStringLiteral("astrometry")));
    EXPECT_FALSE(root.contains(QStringLiteral("exif_header")));
}

TEST_F(SharedBufferSeamTest, EveryRepresentationTheProtocolCarriesExportsAsItself)
{
    SeamImageSpec image;
    image.channels = Channels::Grey;
    const auto value = [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
        return static_cast<double>(seamSample(x, y, channel));
    };

    std::optional<an::consumer::ImageAsset> eight = makeSeamAsset<std::uint8_t>(
        session(), image, [&](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return seamSample(x, y, c); });
    std::optional<an::consumer::ImageAsset> sixteen =
        makeSeamAsset<std::uint16_t>(session(), image, [&](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<std::uint16_t>(seamSample(x, y, c));
        });
    std::optional<an::consumer::ImageAsset> single =
        makeSeamAsset<float>(session(), image, [&](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<float>(value(x, y, c));
        });
    std::optional<an::consumer::ImageAsset> wide = makeSeamAsset<double>(session(), image, value);
    ASSERT_TRUE(eight && sixteen && single && wide);

    std::optional<SharedImageBuffer> eightBuffer = exported(*eight, exportRequest("eight"));
    std::optional<SharedImageBuffer> sixteenBuffer = exported(*sixteen, exportRequest("sixteen"));
    std::optional<SharedImageBuffer> singleBuffer = exported(*single, exportRequest("single"));
    std::optional<SharedImageBuffer> wideBuffer = exported(*wide, exportRequest("wide"));
    ASSERT_TRUE(eightBuffer && sixteenBuffer && singleBuffer && wideBuffer);
    EXPECT_EQ(eightBuffer->descriptor().pixel_type, SharedBufferPixelType::UInt8);
    EXPECT_EQ(sixteenBuffer->descriptor().pixel_type, SharedBufferPixelType::UInt16);
    EXPECT_EQ(singleBuffer->descriptor().pixel_type, SharedBufferPixelType::Float32);
    EXPECT_EQ(wideBuffer->descriptor().pixel_type, SharedBufferPixelType::Float64);
    EXPECT_EQ(eightBuffer->descriptor().row_stride, 4u);
    EXPECT_EQ(sixteenBuffer->descriptor().row_stride, 8u);
    EXPECT_EQ(singleBuffer->descriptor().row_stride, 16u);
    EXPECT_EQ(wideBuffer->descriptor().row_stride, 32u);
    EXPECT_EQ(bufferSample<std::uint8_t>(*eightBuffer, 3u, 2u, 0u), seamSample(3u, 2u, 0u));
    EXPECT_EQ(bufferSample<std::uint16_t>(*sixteenBuffer, 3u, 2u, 0u), seamSample(3u, 2u, 0u));
    EXPECT_FLOAT_EQ(bufferSample<float>(*singleBuffer, 3u, 2u, 0u), static_cast<float>(seamSample(3u, 2u, 0u)));
    EXPECT_DOUBLE_EQ(bufferSample<double>(*wideBuffer, 3u, 2u, 0u), value(3u, 2u, 0u));
}

TEST_F(SharedBufferSeamTest, ARepresentationTheProtocolDoesNotCarryIsRefusedByName)
{
    SeamImageSpec image;
    image.channels = Channels::Grey;
    std::optional<an::consumer::ImageAsset> signedImage = makeSeamAsset<std::int16_t>(
        session(), image, [](std::uint32_t, std::uint32_t, std::uint32_t) { return std::int16_t{-5}; });
    std::optional<an::consumer::ImageAsset> wideInteger = makeSeamAsset<std::uint32_t>(
        session(), image, [](std::uint32_t, std::uint32_t, std::uint32_t) { return std::uint32_t{70000u}; });
    ASSERT_TRUE(signedImage && wideInteger);

    const an::Status signedRefusal = exportRefusal(*signedImage);
    EXPECT_EQ(signedRefusal.code(), an::ErrorCode::Unsupported);
    EXPECT_NE(std::string(signedRefusal.message()).find("Int16"), std::string::npos) << signedRefusal.message();
    const an::Status wideRefusal = exportRefusal(*wideInteger);
    EXPECT_EQ(wideRefusal.code(), an::ErrorCode::Unsupported);
    EXPECT_NE(std::string(wideRefusal.message()).find("UInt32"), std::string::npos) << wideRefusal.message();

    EXPECT_FALSE(an::ui::extensions::sharedBufferPixelTypeOf(an::SampleType::Int8).hasValue());
    EXPECT_FALSE(an::ui::extensions::sharedBufferPixelTypeOf(an::SampleType::Int32).hasValue());
    EXPECT_EQ(an::ui::extensions::sampleTypeOf(SharedBufferPixelType::UInt16), an::SampleType::UInt16);
}

TEST_F(SharedBufferSeamTest, AnImageOfAnotherChannelSchemaIsRefusedByName)
{
    SeamImageSpec image;
    image.channels = Channels::Rgba;
    std::optional<an::consumer::ImageAsset> asset =
        makeSeamAsset<float>(session(), image, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 1.0f; });
    ASSERT_TRUE(asset.has_value());

    const an::Status refusal = exportRefusal(*asset);
    EXPECT_EQ(refusal.code(), an::ErrorCode::Unsupported);
    EXPECT_NE(std::string(refusal.message()).find("Rgba"), std::string::npos) << refusal.message();
}

TEST_F(SharedBufferSeamTest, AMosaicExportsItsBayerPatternWithItsPhase)
{
    SeamImageSpec image;
    image.channels = Channels::Grey;
    image.domain = an::NumericDomain::RawSensorCode;
    const auto value = [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
        return static_cast<std::uint16_t>(x + 10u * y);
    };

    for (const auto& [pattern, expected] : std::vector<std::pair<an::CfaPattern, SharedBufferPixelFormat>>{
             {an::CfaPattern::Rggb, SharedBufferPixelFormat::BayerRGGB},
             {an::CfaPattern::Bggr, SharedBufferPixelFormat::BayerBGGR},
             {an::CfaPattern::Grbg, SharedBufferPixelFormat::BayerGRBG},
             {an::CfaPattern::Gbrg, SharedBufferPixelFormat::BayerGBRG}})
    {
        image.cfa = *bayer(pattern);
        std::optional<an::consumer::ImageAsset> asset = makeSeamAsset<std::uint16_t>(session(), image, value);
        ASSERT_TRUE(asset.has_value());
        std::optional<SharedImageBuffer> buffer = exported(*asset, exportRequest(an::cfaPatternName(pattern)));
        ASSERT_TRUE(buffer.has_value());
        EXPECT_EQ(buffer->descriptor().pixel_format, expected) << an::cfaPatternName(pattern);
        EXPECT_EQ(buffer->descriptor().color_space, SharedBufferColorSpace::Grayscale);
        EXPECT_EQ(buffer->descriptor().channels, 1);
    }

    // A pattern whose origin is one pixel to the right is the pattern the protocol names from that pixel.
    image.cfa = *bayer(an::CfaPattern::Rggb, 1u, 0u);
    std::optional<an::consumer::ImageAsset> shifted = makeSeamAsset<std::uint16_t>(session(), image, value);
    ASSERT_TRUE(shifted.has_value());
    std::optional<SharedImageBuffer> shiftedBuffer = exported(*shifted, exportRequest("shifted"));
    ASSERT_TRUE(shiftedBuffer.has_value());
    EXPECT_EQ(shiftedBuffer->descriptor().pixel_format, SharedBufferPixelFormat::BayerGRBG);

    image.cfa = *bayer(an::CfaPattern::Rggb, 1u, 1u);
    std::optional<an::consumer::ImageAsset> diagonal = makeSeamAsset<std::uint16_t>(session(), image, value);
    ASSERT_TRUE(diagonal.has_value());
    std::optional<SharedImageBuffer> diagonalBuffer = exported(*diagonal, exportRequest("diagonal"));
    ASSERT_TRUE(diagonalBuffer.has_value());
    EXPECT_EQ(diagonalBuffer->descriptor().pixel_format, SharedBufferPixelFormat::BayerBGGR);
}

TEST_F(SharedBufferSeamTest, AnXTransMosaicIsRefusedByName)
{
    const std::optional<an::CfaDescriptor> xtrans = an::CfaDescriptor::create(xtransEntries(), 0u, 0u);
    ASSERT_TRUE(xtrans.has_value()) << "the fixture's filter table describes a mosaic";
    SeamImageSpec image;
    image.channels = Channels::Grey;
    image.width = 12u;
    image.height = 12u;
    image.domain = an::NumericDomain::RawSensorCode;
    image.cfa = *xtrans;
    std::optional<an::consumer::ImageAsset> asset = makeSeamAsset<std::uint16_t>(
        session(), image, [](std::uint32_t, std::uint32_t, std::uint32_t) { return std::uint16_t{7u}; });
    ASSERT_TRUE(asset.has_value());

    const an::Status refusal = exportRefusal(*asset);
    EXPECT_EQ(refusal.code(), an::ErrorCode::Unsupported);
    EXPECT_NE(std::string(refusal.message()).find("X-Trans"), std::string::npos) << refusal.message();
}

TEST_F(SharedBufferSeamTest, TheColourSpaceComesFromTheSemanticsAndNotTheChannelCount)
{
    const auto value = [](std::uint32_t, std::uint32_t, std::uint32_t channel) { return 0.25f * (channel + 1u); };

    SeamImageSpec linear;
    linear.transfer = an::TransferFunction::Linear;
    std::optional<an::consumer::ImageAsset> linearAsset = makeSeamAsset<float>(session(), linear, value);

    SeamImageSpec stretched;
    stretched.transfer = an::TransferFunction::Srgb;
    std::optional<an::consumer::ImageAsset> stretchedAsset = makeSeamAsset<float>(session(), stretched, value);

    SeamImageSpec display;
    display.colorSpace = an::ColorSpace::DisplayRgb;
    display.domain = an::NumericDomain::EncodedNonlinear;
    std::optional<an::consumer::ImageAsset> displayAsset = makeSeamAsset<float>(session(), display, value);

    SeamImageSpec unstated;
    std::optional<an::consumer::ImageAsset> unstatedAsset = makeSeamAsset<float>(session(), unstated, value);

    SeamImageSpec greyStretched;
    greyStretched.channels = Channels::Grey;
    greyStretched.transfer = an::TransferFunction::Srgb;
    std::optional<an::consumer::ImageAsset> greyAsset = makeSeamAsset<float>(session(), greyStretched, value);
    ASSERT_TRUE(linearAsset && stretchedAsset && displayAsset && unstatedAsset && greyAsset);

    std::optional<SharedImageBuffer> linearBuffer = exported(*linearAsset, exportRequest("linear"));
    std::optional<SharedImageBuffer> stretchedBuffer = exported(*stretchedAsset, exportRequest("stretched"));
    std::optional<SharedImageBuffer> displayBuffer = exported(*displayAsset, exportRequest("display"));
    std::optional<SharedImageBuffer> unstatedBuffer = exported(*unstatedAsset, exportRequest("unstated"));
    std::optional<SharedImageBuffer> greyBuffer = exported(*greyAsset, exportRequest("grey"));
    ASSERT_TRUE(linearBuffer && stretchedBuffer && displayBuffer && unstatedBuffer && greyBuffer);
    EXPECT_EQ(linearBuffer->descriptor().color_space, SharedBufferColorSpace::Linear);
    EXPECT_EQ(stretchedBuffer->descriptor().color_space, SharedBufferColorSpace::SRGB);
    EXPECT_EQ(displayBuffer->descriptor().color_space, SharedBufferColorSpace::SRGB);
    EXPECT_EQ(unstatedBuffer->descriptor().color_space, SharedBufferColorSpace::Linear);
    EXPECT_EQ(greyBuffer->descriptor().color_space, SharedBufferColorSpace::Grayscale);
    EXPECT_EQ(greyBuffer->descriptor().pixel_format, SharedBufferPixelFormat::Mono);
}

// A stretch the product runs states no transfer function and records the samples nonlinear in the processing-state
// cards, so an image whose transfer is unstated is lent by what its cards record.
TEST_F(SharedBufferSeamTest, AnImageTheCardsRecordNonlinearIsLentEncodedWhenItsTransferIsUnstated)
{
    const auto value = [](std::uint32_t, std::uint32_t, std::uint32_t channel) { return 0.25f * (channel + 1u); };

    SeamImageSpec stretched;
    stretched.fitsCards = {{"ANLIN", "'nonlinear'"}};
    std::optional<an::consumer::ImageAsset> stretchedAsset = makeSeamAsset<float>(session(), stretched, value);

    SeamImageSpec recordedLinear;
    recordedLinear.fitsCards = {{"ANLIN", "'linear'"}};
    std::optional<an::consumer::ImageAsset> linearAsset = makeSeamAsset<float>(session(), recordedLinear, value);

    SeamImageSpec statedLinear;
    statedLinear.transfer = an::TransferFunction::Linear;
    statedLinear.fitsCards = {{"ANLIN", "'nonlinear'"}};
    std::optional<an::consumer::ImageAsset> statedAsset = makeSeamAsset<float>(session(), statedLinear, value);
    ASSERT_TRUE(stretchedAsset && linearAsset && statedAsset);

    std::optional<SharedImageBuffer> stretchedBuffer = exported(*stretchedAsset, exportRequest("stretched"));
    std::optional<SharedImageBuffer> linearBuffer = exported(*linearAsset, exportRequest("recorded-linear"));
    std::optional<SharedImageBuffer> statedBuffer = exported(*statedAsset, exportRequest("stated-linear"));
    ASSERT_TRUE(stretchedBuffer && linearBuffer && statedBuffer);
    EXPECT_EQ(stretchedBuffer->descriptor().color_space, SharedBufferColorSpace::SRGB);
    EXPECT_EQ(linearBuffer->descriptor().color_space, SharedBufferColorSpace::Linear);
    EXPECT_EQ(statedBuffer->descriptor().color_space, SharedBufferColorSpace::Linear)
        << "a stated transfer function decides before the cards";
}

// ---- Import: an explicit copy, a borrow that stays one.

TEST_F(SharedBufferSeamTest, AnImportCopiesIntoEngineMemoryAndLetsTheMappingGo)
{
    SharedImageBufferSpec made = spec("session-a", "buffer-a", 4, 3, 1);
    made.pixelType = SharedBufferPixelType::UInt8;
    made.pixelFormat = SharedBufferPixelFormat::Mono;
    made.colorSpace = SharedBufferColorSpace::Grayscale;
    an::Result<SharedImageBuffer> buffer = manager().allocateBuffer(made);
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();
    fillBuffer<std::uint8_t>(*buffer, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
        return static_cast<std::uint8_t>(20u + x + 4u * y);
    });
    const SharedBufferDescriptor descriptor = buffer->descriptor();
    const std::filesystem::path file = buffer->filePath();
    buffer->release();

    an::Result<an::ui::extensions::ImportedImage> imported = manager().importImage(session(), descriptor);
    ASSERT_TRUE(imported.hasValue()) << imported.status().message();
    EXPECT_EQ(imported->name, "selection");
    EXPECT_TRUE(imported->fileName.empty());
    const std::optional<an::ImageDescriptor> described = describedBy(imported->asset);
    ASSERT_TRUE(described.has_value());
    EXPECT_EQ(described->sampleType(), an::SampleType::UInt8);
    EXPECT_EQ(described->channelSchema(), an::ChannelSchema::Mono);
    EXPECT_EQ(described->colorSpace(), an::ColorSpace::Unspecified);
    EXPECT_EQ(described->numericDomain(), an::NumericDomain::NormalizedLinear);
    EXPECT_FALSE(described->isMosaiced());
    EXPECT_EQ(an::tests::viewer::sampleOf<std::uint8_t>(imported->asset, 0u, 0u), std::uint8_t{20u});
    EXPECT_EQ(an::tests::viewer::sampleOf<std::uint8_t>(imported->asset, 3u, 2u), std::uint8_t{31u});

    // The samples are the asset's own: the buffer changing afterwards changes nothing in it.
    an::Result<SharedImageBuffer> reopened = manager().openBuffer(descriptor);
    ASSERT_TRUE(reopened.hasValue()) << reopened.status().message();
    writeBufferSample<std::uint8_t>(*reopened, 0u, 0u, 0u, 0u);
    EXPECT_EQ(an::tests::viewer::sampleOf<std::uint8_t>(imported->asset, 0u, 0u), std::uint8_t{20u});
    reopened->release();

    // The import let its mapping go: the file can be removed while the asset lives.
    EXPECT_TRUE(removable(file));
    EXPECT_EQ(an::tests::viewer::sampleOf<std::uint8_t>(imported->asset, 1u, 1u), std::uint8_t{25u});

    const std::optional<an::ImageMetadata> metadata = metadataOf(imported->asset);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->provenance().sourceKind(), an::ProvenanceSourceKind::Derived);
    ASSERT_FALSE(metadata->provenance().entries().empty());
    EXPECT_EQ(metadata->provenance().entries().back().operation(), "shared buffer import");
}

TEST_F(SharedBufferSeamTest, ABorrowedFrameCannotBeAdoptedAndKeepsItsMappingAlive)
{
    SharedImageBufferSpec made = spec("session-a", "buffer-a", 4, 3, 3);
    an::Result<SharedImageBuffer> buffer = manager().allocateBuffer(made);
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();
    fillBuffer<float>(*buffer, [](std::uint32_t x, std::uint32_t y, std::uint32_t plane) {
        return static_cast<float>(seamSample(x, y, plane));
    });
    const std::filesystem::path file = buffer->filePath();

    an::Result<an::RawFrame> frame = SharedBufferManager::borrowFrame(*buffer);
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();
    ASSERT_TRUE(frame->valid());
    EXPECT_EQ(frame->buffer().ownership(), an::SampleBufferOwnership::Mapped);
    EXPECT_TRUE(frame->buffer().writable());
    EXPECT_FALSE(frame->buffer().adoptable());
    const an::Status adoptable = frame->checkAdoptable();
    EXPECT_TRUE(adoptable.failed()) << "a mapping is a borrow, never engine storage";
    const std::optional<an::RawFrameDescriptor> described = frame->descriptor();
    ASSERT_TRUE(described.has_value());
    EXPECT_EQ(described->extent().width(), 4u);
    EXPECT_EQ(described->extent().channelCount(), 3u);
    EXPECT_EQ(described->channelSchema(), an::ChannelSchema::Rgb);
    EXPECT_EQ(described->colorSpace(), an::ColorSpace::LinearRgb);
    const std::optional<an::ImageLayout> layout = frame->layout();
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->rowStrideBytes(), buffer->descriptor().row_stride);
    EXPECT_EQ(layout->planeStrideBytes(), buffer->descriptor().plane_stride);

    // The frame holds the mapping's lifetime: the buffer object can go first, and the file stays mapped.
    buffer->release();
    EXPECT_FALSE(buffer->valid());
    const std::optional<std::span<const std::byte>> row = frame->row(2u, 1u);
    ASSERT_TRUE(row.has_value());
    float sample = 0.0f;
    std::memcpy(&sample, row->data() + 3u * sizeof(float), sizeof(float));
    EXPECT_FLOAT_EQ(sample, static_cast<float>(seamSample(3u, 1u, 2u)));
    EXPECT_FALSE(removable(file)) << "the frame still holds the mapping";

    frame->release();
    EXPECT_TRUE(removable(file)) << "the last holder let the mapping go";
}

TEST_F(SharedBufferSeamTest, AReadOnlyBufferIsBorrowedReadOnly)
{
    SharedImageBufferSpec made = spec("session-a", "buffer-a", 4, 3, 1);
    made.accessMode = SharedBufferAccessMode::ReadOnly;
    an::Result<SharedImageBuffer> allocated = manager().allocateBuffer(made);
    ASSERT_TRUE(allocated.hasValue()) << allocated.status().message();
    const SharedBufferDescriptor descriptor = allocated->descriptor();
    allocated->release();

    an::Result<SharedImageBuffer> opened = manager().openBuffer(descriptor);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    an::Result<an::RawFrame> frame = SharedBufferManager::borrowFrame(*opened);
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();
    EXPECT_FALSE(frame->buffer().writable());
    EXPECT_FALSE(frame->mutableBytes().has_value());
    EXPECT_FALSE(frame->mutableRow(0u, 0u).has_value());
    EXPECT_TRUE(frame->row(0u, 0u).has_value());
}

TEST_F(SharedBufferSeamTest, TheStridesAreHeldToThePlanarContract)
{
    an::Result<SharedImageBuffer> buffer = manager().allocateBuffer(spec("session-a", "buffer-a", 5, 3, 3));
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();
    fillBuffer<float>(*buffer, [](std::uint32_t x, std::uint32_t y, std::uint32_t plane) {
        return static_cast<float>(seamSample(x, y, plane));
    });
    const SharedBufferDescriptor descriptor = buffer->descriptor();
    EXPECT_EQ(descriptor.row_stride, 20u) << "a five-sample row of Float32 is twenty bytes, with no padding";
    EXPECT_EQ(descriptor.plane_stride, 60u);
    buffer->release();

    // Each plane is read at its stride: the third plane's last sample is where the strides say.
    an::Result<an::ui::extensions::ImportedImage> imported = manager().importImage(session(), descriptor);
    ASSERT_TRUE(imported.hasValue()) << imported.status().message();
    EXPECT_FLOAT_EQ(*an::tests::viewer::sampleOf<float>(imported->asset, 4u, 2u, 2u),
                    static_cast<float>(seamSample(4u, 2u, 2u)));
    EXPECT_FLOAT_EQ(*an::tests::viewer::sampleOf<float>(imported->asset, 0u, 0u, 1u),
                    static_cast<float>(seamSample(0u, 0u, 1u)));

    // A padded row or a plane stride the protocol does not state is refused before anything is read.
    SharedBufferDescriptor padded = descriptor;
    padded.row_stride = 32u;
    EXPECT_FALSE(manager().openBuffer(padded).hasValue());
    EXPECT_EQ(manager().importImage(session(), padded).status().code(), an::ErrorCode::InvalidArgument);
    SharedBufferDescriptor loosePlanes = descriptor;
    loosePlanes.plane_stride = 64u;
    EXPECT_FALSE(manager().openBuffer(loosePlanes).hasValue());
    SharedBufferDescriptor tooSmall = descriptor;
    tooSmall.byte_size = 100u;
    EXPECT_FALSE(manager().openBuffer(tooSmall).hasValue());
}

TEST_F(SharedBufferSeamTest, AnImportIsDescribedByItsFormatAndColourSpace)
{
    SharedImageBufferSpec mosaic = spec("session-a", "mosaic", 4, 4, 1);
    mosaic.pixelType = SharedBufferPixelType::UInt16;
    mosaic.pixelFormat = SharedBufferPixelFormat::BayerGBRG;
    mosaic.colorSpace = SharedBufferColorSpace::Grayscale;
    an::Result<SharedImageBuffer> mosaicBuffer = manager().allocateBuffer(mosaic);
    ASSERT_TRUE(mosaicBuffer.hasValue()) << mosaicBuffer.status().message();
    an::Result<an::ui::extensions::ImportedImage> importedMosaic =
        manager().importImage(session(), mosaicBuffer->descriptor());
    ASSERT_TRUE(importedMosaic.hasValue()) << importedMosaic.status().message();
    const std::optional<an::ImageDescriptor> mosaicDescribed = describedBy(importedMosaic->asset);
    ASSERT_TRUE(mosaicDescribed.has_value());
    EXPECT_TRUE(mosaicDescribed->isMosaiced());
    EXPECT_EQ(mosaicDescribed->cfa().pattern(), an::CfaPattern::Gbrg);
    EXPECT_EQ(mosaicDescribed->cfa().originX(), 0u);
    EXPECT_EQ(mosaicDescribed->sampleType(), an::SampleType::UInt16);

    SharedImageBufferSpec encoded = spec("session-a", "encoded", 4, 4, 3);
    encoded.colorSpace = SharedBufferColorSpace::SRGB;
    an::Result<SharedImageBuffer> encodedBuffer = manager().allocateBuffer(encoded);
    ASSERT_TRUE(encodedBuffer.hasValue()) << encodedBuffer.status().message();
    an::Result<an::ui::extensions::ImportedImage> importedEncoded =
        manager().importImage(session(), encodedBuffer->descriptor());
    ASSERT_TRUE(importedEncoded.hasValue()) << importedEncoded.status().message();
    const std::optional<an::ImageDescriptor> encodedDescribed = describedBy(importedEncoded->asset);
    ASSERT_TRUE(encodedDescribed.has_value());
    EXPECT_EQ(encodedDescribed->colorSpace(), an::ColorSpace::DisplayRgb);
    EXPECT_EQ(encodedDescribed->numericDomain(), an::NumericDomain::EncodedNonlinear);
    const std::optional<an::ImageMetadata> encodedMetadata = metadataOf(importedEncoded->asset);
    ASSERT_TRUE(encodedMetadata.has_value());
    EXPECT_EQ(encodedMetadata->semantics().transferFunction(), an::TransferFunction::Srgb);

    SharedImageBufferSpec linear = spec("session-a", "linear", 4, 4, 3);
    an::Result<SharedImageBuffer> linearBuffer = manager().allocateBuffer(linear);
    ASSERT_TRUE(linearBuffer.hasValue()) << linearBuffer.status().message();
    an::Result<an::ui::extensions::ImportedImage> importedLinear =
        manager().importImage(session(), linearBuffer->descriptor());
    ASSERT_TRUE(importedLinear.hasValue()) << importedLinear.status().message();
    const std::optional<an::ImageDescriptor> linearDescribed = describedBy(importedLinear->asset);
    ASSERT_TRUE(linearDescribed.has_value());
    EXPECT_EQ(linearDescribed->colorSpace(), an::ColorSpace::LinearRgb);
    EXPECT_EQ(linearDescribed->numericDomain(), an::NumericDomain::NormalizedLinear);
    const std::optional<an::ImageMetadata> linearMetadata = metadataOf(importedLinear->asset);
    ASSERT_TRUE(linearMetadata.has_value());
    EXPECT_TRUE(linearMetadata->semantics().isLinear());

    // A three-channel buffer in the grey colour space describes no image.
    SharedBufferDescriptor contradictory = linearBuffer->descriptor();
    contradictory.color_space = SharedBufferColorSpace::Grayscale;
    const an::Result<an::RawFrameDescriptor> refused = an::ui::extensions::frameDescriptorOf(contradictory);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported);
}

// ---- The sidecar.

TEST_F(SharedBufferSeamTest, TheSidecarKeepsItsSchema)
{
    SeamImageSpec image;
    image.fitsCards = solvedCards(image.width, image.height);
    image.fitsCards.emplace_back("OBJECT", "'M42'");
    image.fitsCards.emplace_back("FILTER", "'L'");
    image.fitsCards.emplace_back("EXPTIME", "120.0");
    image.exifCards.emplace_back("Exif.Image.Make", "Canon");
    image.exifCards.emplace_back("Exif.Photo.ExposureTime", "120/1");
    std::optional<an::consumer::ImageAsset> asset = makeSeamAsset<float>(
        session(), image, [](std::uint32_t, std::uint32_t, std::uint32_t channel) { return 0.1f + 0.4f * channel; });
    ASSERT_TRUE(asset.has_value());

    ImageExportRequest request = exportRequest();
    request.fileName = "solved.fit";
    std::optional<SharedImageBuffer> buffer = exported(*asset, request);
    ASSERT_TRUE(buffer.has_value());
    ASSERT_FALSE(buffer->descriptor().metadata_sidecar.empty());
    EXPECT_TRUE(std::filesystem::exists(buffer->descriptor().metadata_sidecar));

    const QJsonObject root = readSidecar(buffer->descriptor().metadata_sidecar);
    EXPECT_EQ(root.value(QStringLiteral("FILENAME")).toString(), QStringLiteral("solved.fit"));
    EXPECT_EQ(root.value(QStringLiteral("CRVAL1")).toString(), QStringLiteral("210.123456"));
    EXPECT_EQ(root.value(QStringLiteral("OBJECT")).toString(), QStringLiteral("'M42'"));
    EXPECT_FALSE(root.contains(QStringLiteral("Exif.Image.Make"))) << "an EXIF field is not a top-level card";

    const QJsonObject header = root.value(QStringLiteral("fits_header")).toObject();
    EXPECT_EQ(header.value(QStringLiteral("CTYPE1")).toString(), QStringLiteral("'RA---TAN'"));
    EXPECT_EQ(header.value(QStringLiteral("PLTSOLVR")).toString(), QStringLiteral("'ASTAP'"));
    // The cards that describe a file's structure describe the buffer: a colour Float32 primary image of its size, its
    // samples unscaled - whatever the image's own header said of the file it came from.
    EXPECT_EQ(header.value(QStringLiteral("SIMPLE")).toString(), QStringLiteral("T"));
    EXPECT_EQ(header.value(QStringLiteral("BITPIX")).toString(), QStringLiteral("-32"));
    EXPECT_EQ(header.value(QStringLiteral("NAXIS")).toString(), QStringLiteral("3"));
    EXPECT_EQ(header.value(QStringLiteral("NAXIS1")).toString(), QStringLiteral("4"));
    EXPECT_EQ(header.value(QStringLiteral("NAXIS2")).toString(), QStringLiteral("3"));
    EXPECT_EQ(header.value(QStringLiteral("NAXIS3")).toString(), QStringLiteral("3"));
    EXPECT_EQ(header.value(QStringLiteral("BZERO")).toString(), QStringLiteral("0"));
    EXPECT_EQ(header.value(QStringLiteral("BSCALE")).toString(), QStringLiteral("1"));
    const QJsonArray cards = root.value(QStringLiteral("fits_cards")).toArray();
    EXPECT_EQ(cards.size(), static_cast<qsizetype>(image.fitsCards.size() - 3u + 8u))
        << "the image's three structural cards replaced by the buffer's eight";
    EXPECT_EQ(cards.first().toObject().value(QStringLiteral("key")).toString(), QStringLiteral("SIMPLE"));

    const QJsonObject exifHeader = root.value(QStringLiteral("exif_header")).toObject();
    EXPECT_EQ(exifHeader.value(QStringLiteral("Exif.Image.Make")).toString(), QStringLiteral("Canon"));
    EXPECT_EQ(exifHeader.value(QStringLiteral("Exif.Photo.ExposureTime")).toString(), QStringLiteral("120/1"));
    const QJsonArray exifEntries = root.value(QStringLiteral("exif_entries")).toArray();
    EXPECT_EQ(exifEntries.size(), 2);

    const QJsonObject astrometry = root.value(QStringLiteral("astrometry")).toObject();
    EXPECT_TRUE(astrometry.value(QStringLiteral("has_solution")).toBool());
    // The astrometry record states texts unquoted; the header map keeps each card as written.
    EXPECT_EQ(astrometry.value(QStringLiteral("solver")).toString(), QStringLiteral("ASTAP"));
    EXPECT_EQ(astrometry.value(QStringLiteral("width")).toInt(), 4);
    EXPECT_EQ(astrometry.value(QStringLiteral("height")).toInt(), 3);
    EXPECT_NEAR(astrometry.value(QStringLiteral("crval_ra_deg")).toDouble(), 210.123456, 1e-6);
    EXPECT_NEAR(astrometry.value(QStringLiteral("crval_dec_deg")).toDouble(), -12.25, 1e-6);
    EXPECT_NEAR(astrometry.value(QStringLiteral("image_scale_arcsec_per_pixel")).toDouble(), 1.0, 1e-3);
    EXPECT_TRUE(astrometry.contains(QStringLiteral("field_center_ra_deg")));
    EXPECT_TRUE(astrometry.contains(QStringLiteral("field_of_view_width_arcmin")));
    EXPECT_EQ(astrometry.value(QStringLiteral("projection")).toObject().value(QStringLiteral("ctype1")).toString(),
              QStringLiteral("RA---TAN"));
    EXPECT_FALSE(astrometry.contains(QStringLiteral("sip"))) << "a solution without distortion states no SIP";
}

// A distortion's record states its reference pixel as the CRPIX cards beside it do, from the first pixel's centre at
// one, and describes itself by its orders and that reference pixel.
TEST_F(SharedBufferSeamTest, TheSidecarsSipRecordStatesTheReferencePixelOfItsCards)
{
    SeamImageSpec image;
    image.fitsCards = solvedCards(image.width, image.height);
    for (auto& [keyword, value] : image.fitsCards)
    {
        if (keyword == "CTYPE1")
            value = "'RA---TAN-SIP'";
        else if (keyword == "CTYPE2")
            value = "'DEC--TAN-SIP'";
    }
    image.fitsCards.emplace_back("A_ORDER", "2");
    image.fitsCards.emplace_back("A_2_0", "1.0E-5");
    image.fitsCards.emplace_back("B_ORDER", "2");
    image.fitsCards.emplace_back("B_0_2", "2.0E-5");
    std::optional<an::consumer::ImageAsset> asset = makeSeamAsset<float>(
        session(), image, [](std::uint32_t, std::uint32_t, std::uint32_t channel) { return 0.1f + 0.4f * channel; });
    ASSERT_TRUE(asset.has_value());

    std::optional<SharedImageBuffer> buffer = exported(*asset, exportRequest());
    ASSERT_TRUE(buffer.has_value());
    const QJsonObject sip = readSidecar(buffer->descriptor().metadata_sidecar)
                                .value(QStringLiteral("astrometry"))
                                .toObject()
                                .value(QStringLiteral("sip"))
                                .toObject();
    ASSERT_FALSE(sip.isEmpty()) << "a solution with a distortion states its SIP record";
    EXPECT_TRUE(sip.value(QStringLiteral("has_forward_model")).toBool());
    EXPECT_FALSE(sip.value(QStringLiteral("has_inverse_model")).toBool());
    EXPECT_EQ(sip.value(QStringLiteral("a_order")).toInt(), 2);
    EXPECT_DOUBLE_EQ(sip.value(QStringLiteral("crpix1")).toDouble(), 2.0);
    EXPECT_DOUBLE_EQ(sip.value(QStringLiteral("crpix2")).toDouble(), 2.0);
    EXPECT_EQ(sip.value(QStringLiteral("description")).toString(),
              QStringLiteral("SIP forward A_ORDER=2, B_ORDER=2, CRPIX=(2.0, 2.0)"));
}

// The sidecar lists each card with the record it was read from and each EXIF field with what Exiv2 says of its tag, a
// field continued over several cards as one entry, the entries in key order.
TEST_F(SharedBufferSeamTest, TheSidecarStatesEachCardsRecordAndEachExifFieldsTag)
{
    const std::string longDescription = std::string(an::kMaxCodecCardTextBytes, 'a') + "tail";
    SeamImageSpec image;
    image.fitsCards.emplace_back("OBJECT", "'M42'");
    image.fitsCards.emplace_back("EXPTIME", "120.0");
    image.exifCards.emplace_back("Exif.Image.Make", "Canon");
    image.statedCards = {
        *an::CodecCard::create(an::CodecNamespace::Fits, "GAIN", "100", "sensor gain"),
        *an::CodecCard::create(an::CodecNamespace::Tiff, "Exif.Photo.ExposureTime", "1/100", "Rational"),
        *an::CodecCard::create(an::CodecNamespace::Tiff, "Exif.Image.Model", "Canon EOS R5", "Ascii"),
        *an::CodecCard::create(an::CodecNamespace::Tiff, "Exif.Image.ImageDescription",
                               longDescription.substr(0u, an::kMaxCodecCardTextBytes), "Ascii"),
        *an::CodecCard::create(an::CodecNamespace::Tiff, "Exif.Image.ImageDescription", "tail", "continued"),
    };
    std::optional<an::consumer::ImageAsset> asset =
        makeSeamAsset<float>(session(), image, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    ASSERT_TRUE(asset.has_value());
    std::optional<SharedImageBuffer> buffer = exported(*asset, exportRequest());
    ASSERT_TRUE(buffer.has_value());
    const QJsonObject root = readSidecar(buffer->descriptor().metadata_sidecar);

    std::map<QString, QJsonObject> cards;
    for (const QJsonValue& entry : root.value(QStringLiteral("fits_cards")).toArray())
    {
        cards[entry.toObject().value(QStringLiteral("key")).toString()] = entry.toObject();
    }
    EXPECT_EQ(cards[QStringLiteral("OBJECT")].value(QStringLiteral("raw")).toString(),
              QStringLiteral("OBJECT  = 'M42     '"));
    EXPECT_EQ(cards[QStringLiteral("EXPTIME")].value(QStringLiteral("raw")).toString(),
              QStringLiteral("EXPTIME =                120.0"));
    EXPECT_EQ(cards[QStringLiteral("GAIN")].value(QStringLiteral("raw")).toString(),
              QStringLiteral("GAIN    =                  100 / sensor gain"));
    EXPECT_EQ(cards[QStringLiteral("GAIN")].value(QStringLiteral("comment")).toString(), QStringLiteral("sensor gain"));

    const QJsonObject exifHeader = root.value(QStringLiteral("exif_header")).toObject();
    EXPECT_EQ(exifHeader.value(QStringLiteral("Exif.Image.ImageDescription")).toString().toStdString(), longDescription)
        << "a continued field is stated whole";
    const QJsonArray entries = root.value(QStringLiteral("exif_entries")).toArray();
    ASSERT_EQ(entries.size(), 4);
    QStringList keys;
    for (const QJsonValue& entry : entries)
    {
        keys.push_back(entry.toObject().value(QStringLiteral("key")).toString());
    }
    EXPECT_EQ(keys, QStringList({QStringLiteral("Exif.Image.ImageDescription"), QStringLiteral("Exif.Image.Make"),
                                 QStringLiteral("Exif.Image.Model"), QStringLiteral("Exif.Photo.ExposureTime")}));
    const QJsonObject model = entries.at(2).toObject();
    EXPECT_EQ(model.value(QStringLiteral("value")).toString(), QStringLiteral("Canon EOS R5"));
    EXPECT_EQ(model.value(QStringLiteral("tag")).toInt(), 0x0110);
    EXPECT_EQ(model.value(QStringLiteral("count")).toDouble(), 13.0);
    EXPECT_EQ(model.value(QStringLiteral("tag_name")).toString(), QStringLiteral("Model"));
    EXPECT_EQ(model.value(QStringLiteral("tag_label")).toString(), QStringLiteral("Model"));
    EXPECT_FALSE(model.value(QStringLiteral("tag_description")).toString().isEmpty());
    EXPECT_EQ(model.value(QStringLiteral("type_name")).toString(), QStringLiteral("Ascii"));
    const QJsonObject make = entries.at(1).toObject();
    EXPECT_EQ(make.value(QStringLiteral("type_name")).toString(), QStringLiteral("Ascii"))
        << "a field read with no type states the type its key takes";
    EXPECT_EQ(make.value(QStringLiteral("tag")).toInt(), 0x010F);
    const QJsonObject exposure = entries.at(3).toObject();
    EXPECT_EQ(exposure.value(QStringLiteral("tag")).toInt(), 0x829A);
    EXPECT_EQ(exposure.value(QStringLiteral("tag_label")).toString(), QStringLiteral("Exposure Time"));
    EXPECT_EQ(exposure.value(QStringLiteral("count")).toDouble(), 1.0);
}

// A field longer than one card comes back from the sidecar as the cards a raster's reader states it in, and each field
// keeps the type its entry states.
TEST_F(SharedBufferSeamTest, ALongExifFieldComesBackWholeWithItsType)
{
    const std::string longDescription = std::string(an::kMaxCodecCardTextBytes, 'b') + "end";
    SeamImageSpec image;
    image.statedCards = {
        *an::CodecCard::create(an::CodecNamespace::Tiff, "Exif.Image.Model", "Canon EOS R5", "Ascii"),
        *an::CodecCard::create(an::CodecNamespace::Tiff, "Exif.Image.ImageDescription",
                               longDescription.substr(0u, an::kMaxCodecCardTextBytes), "Ascii"),
        *an::CodecCard::create(an::CodecNamespace::Tiff, "Exif.Image.ImageDescription", "end", "continued"),
    };
    std::optional<an::consumer::ImageAsset> asset =
        makeSeamAsset<float>(session(), image, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.25f; });
    ASSERT_TRUE(asset.has_value());
    std::optional<SharedImageBuffer> buffer = exported(*asset, exportRequest());
    ASSERT_TRUE(buffer.has_value());
    const SharedBufferDescriptor descriptor = buffer->descriptor();
    buffer->release();

    an::Result<an::ui::extensions::ImportedImage> imported = manager().importImage(session(), descriptor);
    ASSERT_TRUE(imported.hasValue()) << imported.status().message();
    const std::optional<an::ImageMetadata> metadata = metadataOf(imported->asset);
    ASSERT_TRUE(metadata.has_value());
    std::vector<an::CodecCard> description;
    for (const an::CodecCard& card : metadata->codec().cards())
    {
        if (card.keyword() == "Exif.Image.ImageDescription")
        {
            description.push_back(card);
        }
    }
    ASSERT_EQ(description.size(), 2u);
    EXPECT_EQ(description[0].value() + description[1].value(), longDescription);
    EXPECT_EQ(description[0].comment(), "Ascii");
    EXPECT_EQ(description[1].comment(), "continued");
    const an::CodecCard* model = metadata->codec().find(an::CodecNamespace::Tiff, "Exif.Image.Model");
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->comment(), "Ascii") << "a field keeps the type its entry states";
}

// An XISF property travels through the sidecar as the file states it: whole when it is longer than one card, back in
// its own namespace, and with the type its card states for as long as the extension leaves its value as it was - so an
// XISF write states it as the property it was rather than as a FITS keyword its name cannot be.
TEST_F(SharedBufferSeamTest, AnXisfPropertyTravelsWholeInItsOwnNamespaceWithItsType)
{
    const std::string history = std::string(an::kMaxCodecCardTextBytes, 'h') + "<end/>";
    SeamImageSpec image;
    image.statedCards = {
        *an::CodecCard::create(an::CodecNamespace::Xisf, "Observation:Object:Name", "M42", "String"),
        *an::CodecCard::create(an::CodecNamespace::Xisf, "AstrometricSolution:LinearTransformationMatrix",
                               "-0.01,0;0,-0.01", "F64Matrix 2x2"),
        *an::CodecCard::create(an::CodecNamespace::Xisf, "PixInsight:ProcessingHistory",
                               history.substr(0u, an::kMaxCodecCardTextBytes), "String"),
        *an::CodecCard::create(an::CodecNamespace::Xisf, "PixInsight:ProcessingHistory", "<end/>", "continued"),
    };
    std::optional<an::consumer::ImageAsset> asset =
        makeSeamAsset<float>(session(), image, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.25f; });
    ASSERT_TRUE(asset.has_value());
    std::optional<SharedImageBuffer> buffer = exported(*asset, exportRequest());
    ASSERT_TRUE(buffer.has_value());
    const SharedBufferDescriptor descriptor = buffer->descriptor();
    const QJsonObject root = readSidecar(descriptor.metadata_sidecar);
    // The extension reads the history whole, once.
    EXPECT_EQ(root.value(QStringLiteral("fits_header"))
                  .toObject()
                  .value(QStringLiteral("PixInsight:ProcessingHistory"))
                  .toString()
                  .toStdString(),
              history);
    int entries = 0;
    for (const QJsonValue& entry : root.value(QStringLiteral("fits_cards")).toArray())
    {
        if (entry.toObject().value(QStringLiteral("key")).toString() == QStringLiteral("PixInsight:ProcessingHistory"))
        {
            ++entries;
            EXPECT_EQ(entry.toObject().value(QStringLiteral("value")).toString().toStdString(), history);
            EXPECT_EQ(entry.toObject().value(QStringLiteral("comment")).toString(), QStringLiteral("String"));
        }
    }
    EXPECT_EQ(entries, 1);
    buffer->release();

    an::Result<an::ui::extensions::ImportedImage> imported = manager().importImage(session(), descriptor);
    ASSERT_TRUE(imported.hasValue()) << imported.status().message();
    const std::optional<an::ImageMetadata> metadata = metadataOf(imported->asset);
    ASSERT_TRUE(metadata.has_value());
    const an::CodecCard* name = metadata->codec().find(an::CodecNamespace::Xisf, "Observation:Object:Name");
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->value(), "M42");
    EXPECT_EQ(name->comment(), "String");
    EXPECT_EQ(metadata->codec().find(an::CodecNamespace::Fits, "Observation:Object:Name"), nullptr);
    const an::CodecCard* matrix =
        metadata->codec().find(an::CodecNamespace::Xisf, "AstrometricSolution:LinearTransformationMatrix");
    ASSERT_NE(matrix, nullptr);
    EXPECT_EQ(matrix->comment(), "F64Matrix 2x2");
    std::vector<an::CodecCard> pieces;
    for (const an::CodecCard& card : metadata->codec().cards())
    {
        if (card.keyword() == "PixInsight:ProcessingHistory")
        {
            pieces.push_back(card);
        }
    }
    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_EQ(pieces[0].origin(), an::CodecNamespace::Xisf);
    EXPECT_EQ(pieces[0].value() + pieces[1].value(), history);
    EXPECT_EQ(pieces[0].comment(), "String");
    EXPECT_EQ(pieces[1].comment(), "continued");
}

// A value the extension changed says nothing of the type its card stated for the old one, so it comes back without it.
TEST_F(SharedBufferSeamTest, AnXisfPropertyAnExtensionChangedComesBackWithoutTheOldType)
{
    SharedImageBufferSpec made = spec("session-a", "changed", 4, 3, 1);
    made.pixelFormat = SharedBufferPixelFormat::Mono;
    made.colorSpace = SharedBufferColorSpace::Grayscale;
    an::Result<SharedImageBuffer> buffer = manager().allocateBuffer(made);
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();
    std::filesystem::path sidecar = buffer->filePath();
    sidecar += ".metadata.json";
    {
        QFile file(QString::fromStdString(sidecar.string()));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(R"({"Instrument:ExposureTime": "300", "Observation:Object:Name": "M43",
                      "fits_cards": [{"key": "Instrument:ExposureTime", "value": "300", "comment": "Float32"},
                                     {"key": "Observation:Object:Name", "value": "M42", "comment": "String"}]})");
    }
    SharedBufferDescriptor descriptor = buffer->descriptor();
    descriptor.metadata_sidecar = sidecar.lexically_normal().generic_string();
    buffer->release();

    an::Result<an::ui::extensions::ImportedImage> imported = manager().importImage(session(), descriptor);
    ASSERT_TRUE(imported.hasValue()) << imported.status().message();
    const std::optional<an::ImageMetadata> metadata = metadataOf(imported->asset);
    ASSERT_TRUE(metadata.has_value());
    const an::CodecCard* exposure = metadata->codec().find(an::CodecNamespace::Xisf, "Instrument:ExposureTime");
    ASSERT_NE(exposure, nullptr);
    EXPECT_EQ(exposure->comment(), "Float32");
    const an::CodecCard* object = metadata->codec().find(an::CodecNamespace::Xisf, "Observation:Object:Name");
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(object->value(), "M43");
    EXPECT_TRUE(object->comment().empty());
}

TEST_F(SharedBufferSeamTest, TheSidecarRoundTripsTheCardsTheFileNameAndTheSolution)
{
    SeamImageSpec image;
    image.fitsCards = solvedCards(image.width, image.height);
    image.fitsCards.emplace_back("OBJECT", "'M42'");
    image.fitsCards.emplace_back("EXPTIME", "120.0");
    image.exifCards.emplace_back("Exif.Image.Make", "Canon");
    std::optional<an::consumer::ImageAsset> asset =
        makeSeamAsset<float>(session(), image, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<float>(seamSample(x, y, c));
        });
    ASSERT_TRUE(asset.has_value());

    ImageExportRequest request = exportRequest();
    request.fileName = "input.fit";
    std::optional<SharedImageBuffer> buffer = exported(*asset, request);
    ASSERT_TRUE(buffer.has_value());
    const SharedBufferDescriptor descriptor = buffer->descriptor();
    buffer->release();

    an::Result<an::ui::extensions::ImportedImage> imported = manager().importImage(session(), descriptor);
    ASSERT_TRUE(imported.hasValue()) << imported.status().message();
    EXPECT_EQ(imported->fileName, "input.fit");
    EXPECT_EQ(imported->name, "source");
    EXPECT_FLOAT_EQ(*an::tests::viewer::sampleOf<float>(imported->asset, 2u, 1u, 1u),
                    static_cast<float>(seamSample(2u, 1u, 1u)));

    const std::optional<an::ImageMetadata> metadata = metadataOf(imported->asset);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(fitsCardValue(*metadata, "OBJECT"), "'M42'");
    EXPECT_EQ(fitsCardValue(*metadata, "CRVAL1"), "210.123456");
    const an::CodecCard* make = metadata->codec().find(an::CodecNamespace::Tiff, "Exif.Image.Make");
    ASSERT_NE(make, nullptr);
    EXPECT_EQ(make->value(), "Canon");
    EXPECT_TRUE(metadata->astrometry().present()) << "the solution is projected from the cards that came back";
    ASSERT_TRUE(metadata->acquisition().exposureSeconds().has_value());
    EXPECT_DOUBLE_EQ(*metadata->acquisition().exposureSeconds(), 120.0);
    EXPECT_EQ(metadata->acquisition().objectName(), "M42");
}

// A sidecar another writer produced - a header object and EXIF entries alone, no top-level cards and no card list - is
// read as the host's own.
TEST_F(SharedBufferSeamTest, ASidecarWrittenByAnotherHandIsReadFromEachOfItsRecords)
{
    SharedImageBufferSpec made = spec("session-a", "foreign", 4, 3, 1);
    made.pixelFormat = SharedBufferPixelFormat::Mono;
    made.colorSpace = SharedBufferColorSpace::Grayscale;
    an::Result<SharedImageBuffer> buffer = manager().allocateBuffer(made);
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();
    std::filesystem::path sidecar = buffer->filePath();
    sidecar += ".metadata.json";
    {
        QFile file(QString::fromStdString(sidecar.string()));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(R"({"FILENAME": "foreign.fit", "fits_header": {"OBJECT": "'M31'", "EXPTIME": "30.0"},
                      "exif_entries": [{"key": "Exif.Image.Make", "value": "Nikon"}]})");
    }
    SharedBufferDescriptor descriptor = buffer->descriptor();
    descriptor.metadata_sidecar = sidecar.lexically_normal().generic_string();
    buffer->release();

    an::Result<an::ui::extensions::ImportedImage> imported = manager().importImage(session(), descriptor);
    ASSERT_TRUE(imported.hasValue()) << imported.status().message();
    EXPECT_EQ(imported->fileName, "foreign.fit");
    const std::optional<an::ImageMetadata> metadata = metadataOf(imported->asset);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(fitsCardValue(*metadata, "OBJECT"), "'M31'");
    EXPECT_EQ(metadata->acquisition().objectName(), "M31");
    ASSERT_TRUE(metadata->acquisition().exposureSeconds().has_value());
    EXPECT_DOUBLE_EQ(*metadata->acquisition().exposureSeconds(), 30.0);
    const an::CodecCard* make = metadata->codec().find(an::CodecNamespace::Tiff, "Exif.Image.Make");
    ASSERT_NE(make, nullptr);
    EXPECT_EQ(make->value(), "Nikon");

    // A sidecar outside the root is refused, and a missing one reads as no cards.
    SharedBufferDescriptor elsewhere = descriptor;
    elsewhere.metadata_sidecar = (std::filesystem::temp_directory_path() / "rogue.metadata.json").generic_string();
    EXPECT_EQ(manager().importImage(session(), elsewhere).status().code(), an::ErrorCode::InvalidArgument);
    SharedBufferDescriptor missing = descriptor;
    missing.metadata_sidecar = (m_root.path() / "session-a" / "missing.metadata.json").generic_string();
    an::Result<an::ui::extensions::ImportedImage> bare = manager().importImage(session(), missing);
    ASSERT_TRUE(bare.hasValue()) << bare.status().message();
    EXPECT_TRUE(bare->fileName.empty());
}

// A region states the whole image's size on the descriptor and, in the sidecar, the region's own: its structure and
// the solution moved with its samples, as a crop moves one.
TEST_F(SharedBufferSeamTest, ARegionExportRecordsTheWholeImageAndCarriesTheSolutionMovedToIt)
{
    SeamImageSpec image;
    image.fitsCards = solvedCards(image.width, image.height);
    image.fitsCards.emplace_back("OBJECT", "'M42'");
    std::optional<an::consumer::ImageAsset> asset =
        makeSeamAsset<float>(session(), image, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<float>(seamSample(x, y, c));
        });
    ASSERT_TRUE(asset.has_value());

    ImageExportRequest request = exportRequest();
    request.region = QRect(1, 1, 2, 2);
    request.fileName = "input.fit";
    std::optional<SharedImageBuffer> buffer = exported(*asset, request);
    ASSERT_TRUE(buffer.has_value());
    const SharedBufferDescriptor& descriptor = buffer->descriptor();
    EXPECT_EQ(descriptor.width, 2);
    EXPECT_EQ(descriptor.height, 2);
    ASSERT_TRUE(descriptor.offset_x.has_value() && descriptor.offset_y.has_value());
    EXPECT_EQ(*descriptor.offset_x, 1);
    EXPECT_EQ(*descriptor.offset_y, 1);
    ASSERT_TRUE(descriptor.source_width.has_value() && descriptor.source_height.has_value());
    EXPECT_EQ(*descriptor.source_width, 4);
    EXPECT_EQ(*descriptor.source_height, 3);
    EXPECT_FLOAT_EQ(bufferSample<float>(*buffer, 0u, 0u, 0u), static_cast<float>(seamSample(1u, 1u, 0u)));
    EXPECT_FLOAT_EQ(bufferSample<float>(*buffer, 1u, 1u, 2u), static_cast<float>(seamSample(2u, 2u, 2u)));

    ASSERT_FALSE(descriptor.metadata_sidecar.empty());
    const QJsonObject root = readSidecar(descriptor.metadata_sidecar);
    EXPECT_EQ(root.value(QStringLiteral("FILENAME")).toString(), QStringLiteral("input.fit"));
    EXPECT_EQ(root.value(QStringLiteral("OBJECT")).toString(), QStringLiteral("'M42'"));
    EXPECT_EQ(root.value(QStringLiteral("NAXIS1")).toString(), QStringLiteral("2"));
    EXPECT_EQ(root.value(QStringLiteral("NAXIS2")).toString(), QStringLiteral("2"));
    EXPECT_DOUBLE_EQ(root.value(QStringLiteral("CRPIX1")).toString().toDouble(), 1.0)
        << "the reference pixel moves with the region's origin";
    EXPECT_DOUBLE_EQ(root.value(QStringLiteral("CRPIX2")).toString().toDouble(), 1.0);
    EXPECT_DOUBLE_EQ(root.value(QStringLiteral("CRVAL1")).toString().toDouble(), 210.123456);
    const QJsonObject astrometry = root.value(QStringLiteral("astrometry")).toObject();
    EXPECT_TRUE(astrometry.value(QStringLiteral("has_solution")).toBool());
    EXPECT_EQ(astrometry.value(QStringLiteral("width")).toInt(), 2);
    EXPECT_EQ(astrometry.value(QStringLiteral("height")).toInt(), 2);
    EXPECT_NEAR(astrometry.value(QStringLiteral("crval_ra_deg")).toDouble(), 210.123456, 1e-9);

    // A region that lies outside the image is refused; one that overlaps it is clipped.
    ImageExportRequest outside = exportRequest("outside");
    outside.region = QRect(10, 10, 2, 2);
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    ASSERT_TRUE(lease.hasValue());
    EXPECT_EQ(manager().exportImage(lease->image(), outside).status().code(), an::ErrorCode::InvalidArgument);
    ImageExportRequest overlapping = exportRequest("overlapping");
    overlapping.region = QRect(3, 2, 5, 5);
    an::Result<SharedImageBuffer> clipped = manager().exportImage(lease->image(), overlapping);
    ASSERT_TRUE(clipped.hasValue()) << clipped.status().message();
    EXPECT_EQ(clipped->descriptor().width, 1);
    EXPECT_EQ(clipped->descriptor().height, 1);
}

TEST_F(SharedBufferSeamTest, ARegionOfAMosaicStatesThePatternSeenFromItsOrigin)
{
    SeamImageSpec image;
    image.channels = Channels::Grey;
    image.domain = an::NumericDomain::RawSensorCode;
    image.cfa = *bayer(an::CfaPattern::Rggb);
    std::optional<an::consumer::ImageAsset> asset =
        makeSeamAsset<std::uint16_t>(session(), image, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return static_cast<std::uint16_t>(x + 10u * y);
        });
    ASSERT_TRUE(asset.has_value());

    ImageExportRequest request = exportRequest("region");
    request.region = QRect(1, 0, 2, 2);
    std::optional<SharedImageBuffer> buffer = exported(*asset, request);
    ASSERT_TRUE(buffer.has_value());
    EXPECT_EQ(buffer->descriptor().pixel_format, SharedBufferPixelFormat::BayerGRBG);
    EXPECT_EQ(bufferSample<std::uint16_t>(*buffer, 0u, 0u, 0u), std::uint16_t{1u});
}

// ---- A buffer reused for the next export.

namespace
{

/// The bytes a file holds; empty when it cannot be read.
[[nodiscard]] QByteArray fileBytes(const std::string& path)
{
    QFile file(QString::fromStdString(path));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/// Whether two buffers hold the same samples.
[[nodiscard]] bool sameBytes(const SharedImageBuffer& left, const SharedImageBuffer& right)
{
    return left.mappedSize() == right.mappedSize() &&
           std::memcmp(left.bytes(), right.bytes(), static_cast<std::size_t>(left.mappedSize())) == 0;
}

/// Expects a reused buffer to state what a fresh export states, field by field, but for what names the buffer itself:
/// its identity, its file and its sidecar's path.
void expectStatedAlike(const SharedBufferDescriptor& reused, const SharedBufferDescriptor& fresh)
{
    EXPECT_EQ(reused.width, fresh.width);
    EXPECT_EQ(reused.height, fresh.height);
    EXPECT_EQ(reused.channels, fresh.channels);
    EXPECT_EQ(reused.pixel_type, fresh.pixel_type);
    EXPECT_EQ(reused.pixel_format, fresh.pixel_format);
    EXPECT_EQ(reused.color_space, fresh.color_space);
    EXPECT_EQ(reused.row_stride, fresh.row_stride);
    EXPECT_EQ(reused.plane_stride, fresh.plane_stride);
    EXPECT_EQ(reused.byte_size, fresh.byte_size);
    EXPECT_EQ(reused.access_mode, fresh.access_mode);
    EXPECT_EQ(reused.buffer_role, fresh.buffer_role);
    EXPECT_EQ(reused.symbolic_name, fresh.symbolic_name);
    EXPECT_EQ(reused.offset_x, fresh.offset_x);
    EXPECT_EQ(reused.offset_y, fresh.offset_y);
    EXPECT_EQ(reused.source_width, fresh.source_width);
    EXPECT_EQ(reused.source_height, fresh.source_height);
    EXPECT_FALSE(reused.metadata_sidecar.empty());
    EXPECT_NE(reused.metadata_sidecar, fresh.metadata_sidecar) << "each buffer keeps its own sidecar";
}

} // namespace

// An export into a buffer allocated before - the whole image, a region of it, a mosaic's region - leaves the bytes, the
// description and the sidecar a fresh export of the same request leaves; only what names the buffer stays its own.
TEST_F(SharedBufferSeamTest, AnExportIntoABufferLeavesWhatAFreshExportOfTheRequestLeaves)
{
    SeamImageSpec solvedSpec;
    solvedSpec.fitsCards = solvedCards(solvedSpec.width, solvedSpec.height);
    solvedSpec.fitsCards.emplace_back("OBJECT", "'M42'");
    solvedSpec.exifCards.emplace_back("Exif.Image.Make", "Canon");
    std::optional<an::consumer::ImageAsset> solved =
        makeSeamAsset<float>(session(), solvedSpec, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<float>(seamSample(x, y, c));
        });
    ASSERT_TRUE(solved.has_value());
    SeamImageSpec mosaicSpec;
    mosaicSpec.channels = Channels::Grey;
    mosaicSpec.domain = an::NumericDomain::RawSensorCode;
    mosaicSpec.cfa = *bayer(an::CfaPattern::Rggb);
    std::optional<an::consumer::ImageAsset> mosaic =
        makeSeamAsset<std::uint16_t>(session(), mosaicSpec, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return static_cast<std::uint16_t>(x + 10u * y);
        });
    ASSERT_TRUE(mosaic.has_value());

    struct Case
    {
        const char* name;
        const an::consumer::ImageAsset* asset;
        std::optional<QRect> region;
    };
    for (const Case& each : {Case{"whole", &*solved, std::nullopt}, Case{"region", &*solved, QRect(1, 1, 2, 2)},
                             Case{"mosaic region", &*mosaic, QRect(1, 0, 2, 2)}})
    {
        SCOPED_TRACE(each.name);
        ImageExportRequest request = exportRequest(each.name);
        request.region = each.region;
        request.fileName = "input.fit";
        std::optional<SharedImageBuffer> fresh = exported(*each.asset, request);
        ASSERT_TRUE(fresh.has_value());

        // A buffer of the export's shape, stated as something else entirely.
        SharedImageBufferSpec other =
            spec("session-reuse", std::string("reused-") + each.name, fresh->descriptor().width,
                 fresh->descriptor().height, fresh->descriptor().channels);
        other.pixelType = fresh->descriptor().pixel_type;
        other.colorSpace = other.channels == 1 ? SharedBufferColorSpace::Grayscale : SharedBufferColorSpace::SRGB;
        other.offsetX = 7;
        other.offsetY = 9;
        other.sourceWidth = 100;
        other.sourceHeight = 100;
        an::Result<SharedImageBuffer> reused = manager().allocateBuffer(other);
        ASSERT_TRUE(reused.hasValue()) << reused.status().message();
        fillBuffer<std::uint8_t>(*reused,
                                 [](std::uint32_t, std::uint32_t, std::uint32_t) { return std::uint8_t{0xA5}; });

        an::Result<an::consumer::AssetReadLease> lease = each.asset->read();
        ASSERT_TRUE(lease.hasValue());
        const an::Status into = manager().exportImageInto(lease->image(), request, *reused);
        ASSERT_TRUE(into.ok()) << into.message();
        EXPECT_EQ(reused->descriptor().buffer_id, std::string("reused-") + each.name) << "the buffer keeps its name";
        expectStatedAlike(reused->descriptor(), fresh->descriptor());
        EXPECT_TRUE(sameBytes(*reused, *fresh));
        EXPECT_EQ(fileBytes(reused->descriptor().metadata_sidecar), fileBytes(fresh->descriptor().metadata_sidecar));
        EXPECT_EQ(reused->sidecarBytes(), fileBytes(reused->descriptor().metadata_sidecar))
            << "the buffer remembers what its sidecar holds";
    }
}

// A buffer that held a region of one image and then holds the whole of another states no place in an image any more,
// and one that held a region states the next region's place.
TEST_F(SharedBufferSeamTest, AnExportIntoABufferStatesWhereTheNewSamplesLie)
{
    SeamImageSpec largeSpec;
    std::optional<an::consumer::ImageAsset> large =
        makeSeamAsset<float>(session(), largeSpec, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<float>(seamSample(x, y, c));
        });
    SeamImageSpec smallSpec;
    smallSpec.width = 2u;
    smallSpec.height = 2u;
    std::optional<an::consumer::ImageAsset> small =
        makeSeamAsset<float>(session(), smallSpec, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    ASSERT_TRUE(large.has_value() && small.has_value());

    ImageExportRequest request = exportRequest("moving");
    request.region = QRect(1, 1, 2, 2);
    std::optional<SharedImageBuffer> buffer = exported(*large, request);
    ASSERT_TRUE(buffer.has_value());
    ASSERT_EQ(buffer->descriptor().offset_x, std::optional<int>(1));

    an::Result<an::consumer::AssetReadLease> largeLease = large->read();
    ASSERT_TRUE(largeLease.hasValue());
    request.region = QRect(2, 0, 2, 2);
    ASSERT_TRUE(manager().exportImageInto(largeLease->image(), request, *buffer).ok());
    EXPECT_EQ(buffer->descriptor().offset_x, std::optional<int>(2));
    EXPECT_EQ(buffer->descriptor().offset_y, std::optional<int>(0));
    EXPECT_EQ(buffer->descriptor().source_width, std::optional<int>(4));
    EXPECT_FLOAT_EQ(bufferSample<float>(*buffer, 0u, 0u, 1u), static_cast<float>(seamSample(2u, 0u, 1u)));

    an::Result<an::consumer::AssetReadLease> smallLease = small->read();
    ASSERT_TRUE(smallLease.hasValue());
    request.region.reset();
    ASSERT_TRUE(manager().exportImageInto(smallLease->image(), request, *buffer).ok());
    EXPECT_FALSE(buffer->descriptor().offset_x.has_value()) << "a whole image states no place in another";
    EXPECT_FALSE(buffer->descriptor().offset_y.has_value());
    EXPECT_FALSE(buffer->descriptor().source_width.has_value());
    EXPECT_FALSE(buffer->descriptor().source_height.has_value());
    EXPECT_FLOAT_EQ(bufferSample<float>(*buffer, 1u, 1u, 2u), 0.5f);
}

TEST_F(SharedBufferSeamTest, AnExportIntoABufferOfAnotherShapeIsRefused)
{
    SeamImageSpec image;
    std::optional<an::consumer::ImageAsset> asset =
        makeSeamAsset<float>(session(), image, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.25f; });
    ASSERT_TRUE(asset.has_value());
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    ASSERT_TRUE(lease.hasValue());

    std::vector<SharedImageBufferSpec> shapes{
        spec("session-shape", "wider", 5, 3, 3), spec("session-shape", "taller", 4, 4, 3),
        spec("session-shape", "grey", 4, 3, 1), spec("session-shape", "sixteen-bit", 4, 3, 3)};
    shapes.back().pixelType = SharedBufferPixelType::UInt16;
    for (const SharedImageBufferSpec& shape : shapes)
    {
        SCOPED_TRACE(shape.bufferId);
        an::Result<SharedImageBuffer> buffer = manager().allocateBuffer(shape);
        ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();
        const SharedBufferDescriptor before = buffer->descriptor();
        const an::Status refused = manager().exportImageInto(lease->image(), exportRequest(), *buffer);
        EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(buffer->descriptor().symbolic_name, before.symbolic_name) << "a refused buffer is left as it was";
        EXPECT_EQ(buffer->descriptor().buffer_role, before.buffer_role);
        EXPECT_TRUE(buffer->descriptor().metadata_sidecar.empty());
    }
    SharedImageBuffer nothing;
    EXPECT_FALSE(manager().exportImageInto(lease->image(), exportRequest(), nothing).ok());
}

// The sidecar is rendered at every export and written only when what it renders differs from what the file holds: the
// same image again writes nothing, a region whose solution moved writes the moved cards.
TEST_F(SharedBufferSeamTest, AnExportIntoABufferWritesItsSidecarOnlyWhenItChanges)
{
    SeamImageSpec image;
    image.fitsCards = solvedCards(image.width, image.height);
    std::optional<an::consumer::ImageAsset> asset =
        makeSeamAsset<float>(session(), image, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<float>(seamSample(x, y, c));
        });
    ASSERT_TRUE(asset.has_value());
    ImageExportRequest request = exportRequest("solved");
    request.region = QRect(0, 0, 2, 2);
    std::optional<SharedImageBuffer> buffer = exported(*asset, request);
    ASSERT_TRUE(buffer.has_value());
    EXPECT_EQ(buffer->sidecarWrites(), 1u);
    const QByteArray first = fileBytes(buffer->descriptor().metadata_sidecar);

    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    ASSERT_TRUE(lease.hasValue());
    ASSERT_TRUE(manager().exportImageInto(lease->image(), request, *buffer).ok());
    EXPECT_EQ(buffer->sidecarWrites(), 1u) << "the same cards are not written again";
    EXPECT_EQ(fileBytes(buffer->descriptor().metadata_sidecar), first);

    request.region = QRect(2, 1, 2, 2);
    ASSERT_TRUE(manager().exportImageInto(lease->image(), request, *buffer).ok());
    EXPECT_EQ(buffer->sidecarWrites(), 2u) << "the region's solution moved";
    const QJsonObject moved = readSidecar(buffer->descriptor().metadata_sidecar);
    EXPECT_DOUBLE_EQ(moved.value(QStringLiteral("CRPIX1")).toString().toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(moved.value(QStringLiteral("CRPIX2")).toString().toDouble(), 1.0);
    EXPECT_EQ(buffer->sidecarBytes(), fileBytes(buffer->descriptor().metadata_sidecar));
}

// A buffer restated for another use states what an allocation of the new specification states, but for its identity,
// its file and its strides; one of another shape is refused.
TEST_F(SharedBufferSeamTest, ARestatedBufferStatesWhatAnAllocationOfItsSpecificationStates)
{
    an::Result<SharedImageBuffer> buffer = manager().allocateBuffer(spec("session-restate", "kept", 4, 3, 3));
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();
    SharedImageBufferSpec next = spec("session-restate", "fresh", 4, 3, 3);
    next.symbolicName = "destination";
    next.colorSpace = SharedBufferColorSpace::SRGB;
    next.accessMode = SharedBufferAccessMode::ReadWrite;
    next.bufferRole = SharedBufferRole::Destination;
    next.offsetX = 2;
    next.offsetY = 1;
    next.sourceWidth = 40;
    next.sourceHeight = 30;
    next.metadataSidecar = (m_root.path() / "session-restate" / "kept.bin.metadata.json").generic_string();
    an::Result<SharedImageBuffer> allocated = manager().allocateBuffer(next);
    ASSERT_TRUE(allocated.hasValue()) << allocated.status().message();

    ASSERT_TRUE(manager().restateBuffer(next, *buffer).ok());
    EXPECT_EQ(buffer->descriptor().buffer_id, "kept");
    EXPECT_EQ(buffer->descriptor().metadata_sidecar, next.metadataSidecar);
    const SharedBufferDescriptor& stated = buffer->descriptor();
    const SharedBufferDescriptor& fresh = allocated->descriptor();
    EXPECT_EQ(stated.pixel_format, fresh.pixel_format);
    EXPECT_EQ(stated.color_space, fresh.color_space);
    EXPECT_EQ(stated.access_mode, fresh.access_mode);
    EXPECT_EQ(stated.buffer_role, fresh.buffer_role);
    EXPECT_EQ(stated.symbolic_name, fresh.symbolic_name);
    EXPECT_EQ(stated.offset_x, fresh.offset_x);
    EXPECT_EQ(stated.offset_y, fresh.offset_y);
    EXPECT_EQ(stated.source_width, fresh.source_width);
    EXPECT_EQ(stated.source_height, fresh.source_height);
    EXPECT_EQ(stated.row_stride, fresh.row_stride);

    SharedImageBufferSpec wider = next;
    wider.width = 5;
    EXPECT_EQ(manager().restateBuffer(wider, *buffer).code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(buffer->descriptor().width, 4) << "a refused restatement changes nothing";
    SharedImageBufferSpec unpaired = next;
    unpaired.sourceHeight.reset();
    EXPECT_EQ(manager().restateBuffer(unpaired, *buffer).code(), an::ErrorCode::InvalidArgument)
        << "a place in an image is stated whole or not at all";
}

TEST_F(SharedBufferSeamTest, AClearedBufferHoldsZeros)
{
    an::Result<SharedImageBuffer> buffer = manager().allocateBuffer(spec("session-clear", "cleared", 5, 3, 3));
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();
    fillBuffer<float>(*buffer, [](std::uint32_t x, std::uint32_t y, std::uint32_t plane) {
        return 1.0f + static_cast<float>(seamSample(x, y, plane));
    });
    const SharedBufferDescriptor before = buffer->descriptor();

    ASSERT_TRUE(manager().clearBuffer(*buffer).ok());
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(buffer->bytes());
    EXPECT_TRUE(std::all_of(bytes, bytes + buffer->mappedSize(), [](std::uint8_t byte) { return byte == 0u; }));
    EXPECT_EQ(buffer->descriptor().buffer_id, before.buffer_id);
    EXPECT_EQ(buffer->mappedSize(), before.byte_size);

    SharedImageBuffer nothing;
    EXPECT_FALSE(manager().clearBuffer(nothing).ok());
}

// An import of a buffer still mapped reads what an import of its description reads - the samples, the cards, the
// solution and the names - and leaves the mapping in place for the next use.
TEST_F(SharedBufferSeamTest, AnImportOfAMappedBufferReadsWhatAnImportOfItsDescriptionReads)
{
    SeamImageSpec image;
    image.fitsCards = solvedCards(image.width, image.height);
    image.fitsCards.emplace_back("OBJECT", "'M42'");
    image.fitsCards.emplace_back("EXPTIME", "120.0");
    image.exifCards.emplace_back("Exif.Image.Make", "Canon");
    std::optional<an::consumer::ImageAsset> asset =
        makeSeamAsset<float>(session(), image, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<float>(seamSample(x, y, c));
        });
    ASSERT_TRUE(asset.has_value());
    ImageExportRequest request = exportRequest();
    request.fileName = "input.fit";
    std::optional<SharedImageBuffer> buffer = exported(*asset, request);
    ASSERT_TRUE(buffer.has_value());

    an::Result<an::ui::extensions::ImportedImage> described = manager().importImage(session(), buffer->descriptor());
    ASSERT_TRUE(described.hasValue()) << described.status().message();
    an::Result<an::ui::extensions::ImportedImage> mapped = manager().importMapped(session(), *buffer);
    ASSERT_TRUE(mapped.hasValue()) << mapped.status().message();
    EXPECT_TRUE(buffer->valid()) << "the mapping stays for the buffer's next use";
    EXPECT_EQ(mapped->name, described->name);
    EXPECT_EQ(mapped->fileName, described->fileName);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (const auto& [x, y] : {std::pair{0u, 0u}, std::pair{3u, 2u}, std::pair{1u, 2u}})
        {
            EXPECT_EQ(an::tests::viewer::sampleOf<float>(mapped->asset, x, y, channel),
                      an::tests::viewer::sampleOf<float>(described->asset, x, y, channel));
        }
    }
    const std::optional<an::ImageMetadata> fromMapping = metadataOf(mapped->asset);
    const std::optional<an::ImageMetadata> fromDescription = metadataOf(described->asset);
    ASSERT_TRUE(fromMapping.has_value() && fromDescription.has_value());
    ASSERT_EQ(fromMapping->codec().cards().size(), fromDescription->codec().cards().size());
    for (std::size_t index = 0u; index < fromMapping->codec().cards().size(); ++index)
    {
        EXPECT_EQ(fromMapping->codec().cards()[index].keyword(), fromDescription->codec().cards()[index].keyword());
        EXPECT_EQ(fromMapping->codec().cards()[index].value(), fromDescription->codec().cards()[index].value());
    }
    EXPECT_EQ(fromMapping->astrometry().present(), fromDescription->astrometry().present());
    EXPECT_EQ(fitsCardValue(*fromMapping, "OBJECT"), "'M42'");

    // What the next use writes is what the next import reads.
    writeBufferSample<float>(*buffer, 0u, 0u, 0u, 0.875f);
    an::Result<an::ui::extensions::ImportedImage> again = manager().importMapped(session(), *buffer);
    ASSERT_TRUE(again.hasValue()) << again.status().message();
    EXPECT_EQ(an::tests::viewer::sampleOf<float>(again->asset, 0u, 0u, 0u), std::optional<float>(0.875f));
    EXPECT_EQ(an::tests::viewer::sampleOf<float>(mapped->asset, 0u, 0u, 0u),
              std::optional<float>(static_cast<float>(seamSample(0u, 0u, 0u))))
        << "an import's samples are its own";

    SharedImageBuffer nothing;
    EXPECT_FALSE(manager().importMapped(session(), nothing).hasValue());
}

// ---- The cost of the copies.

TEST_F(SharedBufferSeamTest, TheCopyCostIsMeasured)
{
    SeamImageSpec image;
    image.width = 2048u;
    image.height = 1536u;
    image.channels = Channels::Rgb;
    std::optional<an::consumer::ImageAsset> asset =
        makeSeamAsset<float>(session(), image, [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            return static_cast<float>((x * 3u + y * 5u + channel * 7u) % 1000u) / 1000.0f;
        });
    ASSERT_TRUE(asset.has_value());
    const double megabytes = static_cast<double>(2048u * 1536u * 3u * sizeof(float)) / (1024.0 * 1024.0);

    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    ASSERT_TRUE(lease.hasValue());
    const auto exportStart = std::chrono::steady_clock::now();
    an::Result<SharedImageBuffer> buffer = manager().exportImage(lease->image(), exportRequest("large"));
    const auto exportEnd = std::chrono::steady_clock::now();
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();
    lease->release();
    const SharedBufferDescriptor descriptor = buffer->descriptor();
    buffer->release();

    const auto importStart = std::chrono::steady_clock::now();
    an::Result<an::ui::extensions::ImportedImage> imported = manager().importImage(session(), descriptor);
    const auto importEnd = std::chrono::steady_clock::now();
    ASSERT_TRUE(imported.hasValue()) << imported.status().message();

    const double exportMs = std::chrono::duration<double, std::milli>(exportEnd - exportStart).count();
    const double importMs = std::chrono::duration<double, std::milli>(importEnd - importStart).count();
    RecordProperty("megabytes", std::to_string(megabytes));
    RecordProperty("export_ms", std::to_string(exportMs));
    RecordProperty("import_ms", std::to_string(importMs));
    RecordProperty("export_megabytes_per_second", std::to_string(megabytes / (exportMs / 1000.0)));
    RecordProperty("import_megabytes_per_second", std::to_string(megabytes / (importMs / 1000.0)));
    std::cout << "[  measure ] " << megabytes << " MiB exported in " << exportMs << " ms and imported in " << importMs
              << " ms\n";

    for (const auto& [x, y] : std::array<std::pair<std::uint32_t, std::uint32_t>, 4>{
             {{0u, 0u}, {1024u, 768u}, {2047u, 1535u}, {137u, 911u}}})
    {
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            EXPECT_FLOAT_EQ(*an::tests::viewer::sampleOf<float>(imported->asset, x, y, channel),
                            *an::tests::viewer::sampleOf<float>(*asset, x, y, channel))
                << x << "," << y << " channel " << channel;
        }
    }
    EXPECT_LT(exportMs + importMs, 20000.0) << "a round trip of this size is a transfer, not a computation";
}
