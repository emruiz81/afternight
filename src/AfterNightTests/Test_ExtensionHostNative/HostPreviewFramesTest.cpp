// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The frame seam of a native window's preview: what one frame lends the extension is the frame's own size - the whole
/// image, a reduction of it, or a part cut from the image with where it lies stated - the manual mask goes as the
/// selection at that size, what the extension wrote comes back as an asset of the session, and every file a run lends
/// is gone once it is done; a window's frames reuse their buffers - the same files for the next frame of a shape, never
/// one lent twice at once, an output cleared and a sidecar reset at each frame, a source's sidecar written only when
/// its cards change, the pool's bounds held and everything removed when it goes; what each step of a frame's way cost,
/// and the trace a line a frame.

#include "HostNativeTestSupport.h"
#include "HostPreviewFrames.h"
#include "app/SharedBufferManager.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/Provenance.h>

#include <gtest/gtest.h>

#include <QByteArray>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QSize>
#include <QTemporaryDir>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <system_error>
#include <utility>
#include <vector>

namespace
{

using an::tests::host_native::assetSampleAt;
using an::tests::host_native::greyAsset;
using an::tests::host_native::suiteSession;
using an::ui::extensions::ExtensionPreviewFrame;
using an::ui::extensions::lendPreviewFrame;
using an::ui::extensions::LentBuffersRemoval;
using an::ui::extensions::PreviewFrameBufferPool;
using an::ui::extensions::PreviewFrameBufferPoolBounds;
using an::ui::extensions::PreviewFrameBuffers;
using an::ui::extensions::PreviewFrameSpans;
using an::ui::extensions::PreviewFrameTrace;
using an::ui::extensions::previewFrameTraceHeader;
using an::ui::extensions::previewFrameTraceLine;
using an::ui::extensions::SharedBufferAccessMode;
using an::ui::extensions::SharedBufferManager;
using an::ui::extensions::SharedBufferPixelType;
using an::ui::extensions::SharedBufferRole;
using an::ui::extensions::SharedImageBuffer;
using an::ui::extensions::takePreviewFrameResult;

/// The regular files under a directory, at any depth; none when it does not exist.
std::vector<std::filesystem::path> filesUnder(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (!std::filesystem::exists(root, ec))
    {
        return files;
    }
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec))
    {
        if (it->is_regular_file(ec))
        {
            files.push_back(it->path());
        }
    }
    return files;
}

/// A Float32 colour gradient of the run's session: red is the column over the last column, green the row over the last
/// row, blue a half.
std::shared_ptr<an::consumer::ImageAsset> gradientAsset(std::uint32_t width, std::uint32_t height)
{
    an::Result<an::consumer::ImageAsset> asset =
        an::tests::fixtures::createRgbGradientAsset(suiteSession(), width, height);
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::make_shared<an::consumer::ImageAsset>(std::move(*asset)) : nullptr;
}

/// A grey Float32 image of the run's session whose cards state a tangent-plane solution at one arcsecond per pixel,
/// its reference pixel at (32, 24); null when it cannot be made.
std::shared_ptr<an::consumer::ImageAsset> solvedAsset(std::uint32_t width, std::uint32_t height)
{
    std::vector<an::CodecCard> cards;
    for (const auto& [keyword, value] : std::vector<std::pair<std::string, std::string>>{{"CTYPE1", "'RA---TAN'"},
                                                                                         {"CTYPE2", "'DEC--TAN'"},
                                                                                         {"CUNIT1", "'deg'"},
                                                                                         {"CUNIT2", "'deg'"},
                                                                                         {"CRPIX1", "32.0"},
                                                                                         {"CRPIX2", "24.0"},
                                                                                         {"CRVAL1", "210.123456"},
                                                                                         {"CRVAL2", "-12.25"},
                                                                                         {"CD1_1", "-0.0002777777778"},
                                                                                         {"CD1_2", "0.0"},
                                                                                         {"CD2_1", "0.0"},
                                                                                         {"CD2_2", "0.0002777777778"},
                                                                                         {"RADESYS", "'ICRS'"}})
    {
        an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, keyword, value, "");
        EXPECT_TRUE(card.hasValue()) << card.status().message();
        if (!card.hasValue())
        {
            return nullptr;
        }
        cards.push_back(std::move(*card));
    }
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(an::ImageSemanticsRequest{});
    an::Result<an::Provenance> provenance = an::Provenance::create(an::ProvenanceSourceKind::Generated, "solved");
    an::Result<an::consumer::SessionHold> hold = suiteSession().hold();
    if (!codec.hasValue() || !semantics.hasValue() || !provenance.hasValue() || !hold.hasValue())
    {
        ADD_FAILURE() << "the solved image's metadata cannot be made";
        return nullptr;
    }
    an::Result<an::ImageMetadata> metadata =
        an::ImageMetadata::create(an::AcquisitionMetadata{}, std::move(*semantics), an::AstrometricSolution{},
                                  std::move(*provenance), std::move(*codec), std::nullopt);
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, hold->engine().limits());
    const std::optional<an::ImageDescriptor> descriptor =
        extent.has_value() ? an::ImageDescriptor::create(
                                 an::ImageDescriptorRequest{.extent = *extent,
                                                            .sampleType = an::SampleType::Float32,
                                                            .channelSchema = an::ChannelSchema::Mono,
                                                            .colorSpace = an::ColorSpace::Unspecified,
                                                            .numericDomain = an::NumericDomain::NormalizedLinear,
                                                            .cfa = an::CfaDescriptor{},
                                                            .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                            .pixelAspectRatio = 1.0})
                           : std::nullopt;
    if (!metadata.hasValue() || !descriptor.has_value())
    {
        ADD_FAILURE() << "the solved image cannot be described";
        return nullptr;
    }
    an::Result<an::Image<float>> image =
        an::Image<float>::create(hold->engine(), *descriptor, an::StorageArrangement::Planar,
                                 an::StorageInitialization::Zeroed, std::move(*metadata));
    if (!image.hasValue())
    {
        ADD_FAILURE() << image.status().message();
        return nullptr;
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(suiteSession(), an::consumer::AnyImage(std::move(*image)));
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::make_shared<an::consumer::ImageAsset>(std::move(*asset)) : nullptr;
}

/// Writes one Float32 sample of a mapped buffer.
void writeSample(SharedImageBuffer& buffer, std::uint32_t x, std::uint32_t y, std::uint32_t plane, float sample)
{
    const auto& described = buffer.descriptor();
    std::byte* at = buffer.bytes() + plane * described.plane_stride + y * described.row_stride +
                    static_cast<std::uint64_t>(x) * sizeof(float);
    std::memcpy(at, &sample, sizeof(float));
}

/// Writes every Float32 sample of the first `rows` rows of a mapped buffer.
void fillRows(SharedImageBuffer& buffer, int rows, float sample)
{
    const auto& described = buffer.descriptor();
    for (int plane = 0; plane < described.channels; ++plane)
    {
        for (int y = 0; y < rows; ++y)
        {
            for (int x = 0; x < described.width; ++x)
            {
                writeSample(buffer, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                            static_cast<std::uint32_t>(plane), sample);
            }
        }
    }
}

/// The bytes a file holds; empty when it cannot be read.
[[nodiscard]] QByteArray fileBytes(const std::string& path)
{
    QFile file(QString::fromStdString(path));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/// What the bridge does to an output's sidecar once the extension's run returns: the cards it had, and whatever the
/// extension set on its result, written back in the bridge's own formatting.
void setOnTheResult(const SharedImageBuffer& output, const QString& keyword, const QString& value)
{
    const std::string path = output.descriptor().metadata_sidecar;
    QJsonObject cards = QJsonDocument::fromJson(fileBytes(path)).object();
    cards.insert(keyword, value);
    QFile file(QString::fromStdString(path));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(cards).toJson(QJsonDocument::Compact));
}

/// A FITS card of an asset's metadata, or nothing.
[[nodiscard]] std::optional<std::string> cardOf(const an::consumer::ImageAsset& asset, const std::string& keyword)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    return lease->visit([&keyword](const auto& typed) -> std::optional<std::string> {
        const an::CodecCard* card = typed.metadata().codec().find(an::CodecNamespace::Fits, keyword);
        return card != nullptr ? std::optional<std::string>(card->value()) : std::nullopt;
    });
}

/// A frame over an image as the framework hands it over: its source read under a lease the frame holds.
struct LeasedFrame
{
    explicit LeasedFrame(const an::consumer::ImageAsset& source) : lease(source.read())
    {
        EXPECT_TRUE(lease.hasValue()) << lease.status().message();
        if (lease.hasValue())
        {
            frame.source = &lease->image();
        }
    }

    an::Result<an::consumer::AssetReadLease> lease;
    ExtensionPreviewFrame frame;
};

/// Bounds under which a pool keeps nothing once a frame is done.
[[nodiscard]] PreviewFrameBufferPoolBounds keepingNothing()
{
    return PreviewFrameBufferPoolBounds{.largestBufferBytes = 0u, .idleBytes = 0u};
}

/// Bounds under which a pool keeps whatever its frames give back.
[[nodiscard]] PreviewFrameBufferPoolBounds keepingEverything()
{
    return PreviewFrameBufferPoolBounds{.largestBufferBytes = 1ull << 40u, .idleBytes = 1ull << 40u};
}

/// The bytes of one Float32 buffer of a size.
[[nodiscard]] constexpr std::uint64_t floatBytes(std::uint64_t width, std::uint64_t height, std::uint64_t channels = 1u)
{
    return width * height * channels * sizeof(float);
}

class HostPreviewFramesTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
    }

    [[nodiscard]] SharedBufferManager manager() const
    {
        return SharedBufferManager(rootPath());
    }

    [[nodiscard]] std::filesystem::path rootPath() const
    {
        return std::filesystem::path(m_root.path().toStdWString());
    }

    /// The files the buffers of a pool over the case's root are in, and their sidecars.
    [[nodiscard]] std::set<std::filesystem::path> files() const
    {
        const std::vector<std::filesystem::path> found = filesUnder(rootPath());
        return std::set<std::filesystem::path>(found.begin(), found.end());
    }

private:
    QTemporaryDir m_root;
};

TEST_F(HostPreviewFramesTest, AFrameIsLentAtItsOwnSizeAndItsFilesGoWithIt)
{
    // The framework's reduction of a 160x120 image by four: the extension reads and writes 40x30, not the image.
    const an::consumer::ImageAsset reduced = greyAsset(40u, 30u, 0.25);
    LeasedFrame leased(reduced);
    leased.frame.imageSize = QSize(160, 120);
    leased.frame.factor = 4u;
    leased.frame.viewName = "M42";
    leased.frame.fileName = "m42.fits";

    {
        PreviewFrameBufferPool pool(manager(), "frames", keepingNothing());
        an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
        ASSERT_TRUE(lent.hasValue()) << lent.status().message();
        const auto& source = lent->source.descriptor();
        EXPECT_EQ(source.width, 40);
        EXPECT_EQ(source.height, 30);
        EXPECT_EQ(source.channels, 1);
        EXPECT_EQ(source.symbolic_name, "M42");
        EXPECT_EQ(source.buffer_role, SharedBufferRole::Source);
        EXPECT_FALSE(source.offset_x.has_value()) << "a whole frame states no place in the image";

        const auto& output = lent->output.descriptor();
        EXPECT_EQ(output.width, 40);
        EXPECT_EQ(output.height, 30);
        EXPECT_EQ(output.channels, 1);
        EXPECT_EQ(output.symbolic_name, "destination");
        EXPECT_EQ(output.buffer_role, SharedBufferRole::Destination);
        EXPECT_EQ(output.access_mode, SharedBufferAccessMode::ReadWrite);
        EXPECT_FALSE(output.offset_x.has_value());
        // The output's sidecar is its own, so what the extension sets on its result never reaches the source's, and it
        // starts as the source's cards.
        EXPECT_NE(output.metadata_sidecar, source.metadata_sidecar);
        EXPECT_FALSE(fileBytes(output.metadata_sidecar).isEmpty());
        EXPECT_EQ(fileBytes(output.metadata_sidecar), fileBytes(source.metadata_sidecar));
        EXPECT_TRUE(lent->auxiliary.empty()) << "no mask was drawn";

        EXPECT_FALSE(filesUnder(rootPath()).empty());
        lent->release();
        EXPECT_TRUE(filesUnder(rootPath()).empty()) << "a pool that keeps nothing removes a frame's files with it";
    }
    EXPECT_TRUE(filesUnder(rootPath()).empty());
}

TEST_F(HostPreviewFramesTest, APartIsCutFromTheImageWithWhereItLies)
{
    const std::shared_ptr<const an::consumer::ImageAsset> image = gradientAsset(64u, 48u);
    ASSERT_TRUE(image);
    // What the framework cut stands here as a grey image of the part's size: the part is lent from the image itself.
    const an::consumer::ImageAsset cut = greyAsset(20u, 12u, 0.75);
    LeasedFrame leased(cut);
    leased.frame.image = image;
    leased.frame.part = QRect(10, 8, 20, 12);
    leased.frame.imageSize = QSize(64, 48);

    PreviewFrameBufferPool pool(manager(), "frames");
    an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
    ASSERT_TRUE(lent.hasValue()) << lent.status().message();
    const auto& source = lent->source.descriptor();
    EXPECT_EQ(source.width, 20);
    EXPECT_EQ(source.height, 12);
    EXPECT_EQ(source.channels, 3);
    EXPECT_EQ(source.offset_x, std::optional<int>(10));
    EXPECT_EQ(source.offset_y, std::optional<int>(8));
    EXPECT_EQ(source.source_width, std::optional<int>(64));
    EXPECT_EQ(source.source_height, std::optional<int>(48));
    const auto& output = lent->output.descriptor();
    EXPECT_EQ(output.width, 20);
    EXPECT_EQ(output.height, 12);
    EXPECT_EQ(output.offset_x, std::optional<int>(10));
    EXPECT_EQ(output.offset_y, std::optional<int>(8));
    EXPECT_EQ(output.source_width, std::optional<int>(64));
    EXPECT_EQ(output.source_height, std::optional<int>(48));

    // The part's first pixel is the image's pixel at the part's corner.
    an::Result<an::ui::extensions::ImportedImage> read = manager().importImage(suiteSession(), source);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_NEAR(assetSampleAt(read->asset, 0u, 0u, 0u), 10.0 / 63.0, 1e-6);
    EXPECT_NEAR(assetSampleAt(read->asset, 0u, 0u, 1u), 8.0 / 47.0, 1e-6);
    EXPECT_NEAR(assetSampleAt(read->asset, 19u, 11u, 0u), 29.0 / 63.0, 1e-6);
    EXPECT_NEAR(assetSampleAt(read->asset, 19u, 11u, 1u), 19.0 / 47.0, 1e-6);
}

// A frame never waits for a writer, and a part lent without where it lies would claim to be a whole image of its size
// at the origin: a part of an image being written is stopped as a superseded frame is, lending nothing, and the frame
// after the writer has gone is lent with its place.
TEST_F(HostPreviewFramesTest, APartOfAnImageBeingWrittenIsStoppedAndLendsNothing)
{
    const std::shared_ptr<an::consumer::ImageAsset> image = gradientAsset(64u, 48u);
    ASSERT_TRUE(image);
    const an::consumer::ImageAsset cut = greyAsset(20u, 12u, 0.75);
    LeasedFrame leased(cut);
    leased.frame.image = image;
    leased.frame.part = QRect(10, 8, 20, 12);
    leased.frame.imageSize = QSize(64, 48);

    PreviewFrameBufferPool pool(manager(), "frames");
    an::Result<an::consumer::AssetWriteLease> writing = image->tryWrite();
    ASSERT_TRUE(writing.hasValue()) << writing.status().message();
    an::Result<PreviewFrameBuffers> refused = lendPreviewFrame(pool, leased.frame);
    writing->release();
    ASSERT_FALSE(refused.hasValue()) << "a part was lent without where it lies";
    EXPECT_EQ(refused.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(pool.idleBuffers(), 0u);
    EXPECT_TRUE(filesUnder(rootPath()).empty()) << "a stopped frame leaves no buffer behind";

    an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
    ASSERT_TRUE(lent.hasValue()) << lent.status().message();
    EXPECT_EQ(lent->source.descriptor().width, 20);
    EXPECT_EQ(lent->source.descriptor().offset_x, std::optional<int>(10));
    EXPECT_EQ(lent->source.descriptor().offset_y, std::optional<int>(8));
    EXPECT_EQ(lent->source.descriptor().source_width, std::optional<int>(64));
}

TEST_F(HostPreviewFramesTest, TheManualMaskIsLentAsTheSelectionAtTheFramesSize)
{
    const an::consumer::ImageAsset reduced = greyAsset(40u, 30u, 0.25);
    const an::consumer::ImageAsset mask = greyAsset(40u, 30u, 1.0);
    LeasedFrame leased(reduced);
    an::Result<an::consumer::AssetReadLease> maskLease = mask.read();
    ASSERT_TRUE(maskLease.hasValue());
    leased.frame.mask = &maskLease->image();
    leased.frame.factor = 4u;

    PreviewFrameBufferPool pool(manager(), "frames");
    an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
    ASSERT_TRUE(lent.hasValue()) << lent.status().message();
    ASSERT_EQ(lent->auxiliary.size(), 1u);
    const auto& selection = lent->auxiliary.front().descriptor();
    EXPECT_EQ(selection.symbolic_name, "selection");
    EXPECT_EQ(selection.buffer_role, SharedBufferRole::Mask);
    EXPECT_EQ(selection.access_mode, SharedBufferAccessMode::ReadOnly);
    EXPECT_EQ(selection.width, 40);
    EXPECT_EQ(selection.height, 30);
}

TEST_F(HostPreviewFramesTest, WhatTheExtensionWroteComesBackAsAnAssetOfTheSession)
{
    const an::consumer::ImageAsset reduced = greyAsset(8u, 6u, 0.25);
    LeasedFrame leased(reduced);

    PreviewFrameBufferPool pool(manager(), "frames", keepingNothing());
    an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
    ASSERT_TRUE(lent.hasValue()) << lent.status().message();
    for (std::uint32_t y = 0u; y < 6u; ++y)
    {
        for (std::uint32_t x = 0u; x < 8u; ++x)
        {
            writeSample(lent->output, x, y, 0u, 0.5f + 0.01f * static_cast<float>(x));
        }
    }
    an::Result<an::consumer::ImageAsset> made = takePreviewFrameResult(pool.manager(), suiteSession(), *lent);
    ASSERT_TRUE(made.hasValue()) << made.status().message();

    // The asset's samples are its own: the frame's files go, and the asset keeps what the extension wrote.
    lent->release();
    EXPECT_TRUE(filesUnder(rootPath()).empty());
    EXPECT_NEAR(assetSampleAt(*made, 0u, 0u), 0.5, 1e-6);
    EXPECT_NEAR(assetSampleAt(*made, 7u, 5u), 0.57, 1e-6);
}

TEST_F(HostPreviewFramesTest, AMovedFrameKeepsItsBuffersUntilItsNewOwnerIsDone)
{
    const an::consumer::ImageAsset reduced = greyAsset(8u, 6u, 0.25);
    LeasedFrame leased(reduced);

    PreviewFrameBufferPool pool(manager(), "frames", keepingNothing());
    std::optional<PreviewFrameBuffers> kept;
    {
        an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
        ASSERT_TRUE(lent.hasValue()) << lent.status().message();
        kept.emplace(std::move(*lent));
    }
    EXPECT_FALSE(filesUnder(rootPath()).empty()) << "the moved-from frame gave back what its new owner lends";
    EXPECT_TRUE(kept->output.valid());
    kept.reset();
    EXPECT_TRUE(filesUnder(rootPath()).empty());
}

TEST_F(HostPreviewFramesTest, ARunsLentBuffersAreRemovedHoweverItEnds)
{
    const an::consumer::ImageAsset image = greyAsset(8u, 6u, 0.25);
    an::Result<an::consumer::AssetReadLease> lease = image.read();
    ASSERT_TRUE(lease.hasValue()) << lease.status().message();
    an::ui::extensions::ImageExportRequest request;
    request.sessionId = "run";
    request.role = SharedBufferRole::Source;
    request.accessMode = SharedBufferAccessMode::ReadWrite;

    SharedImageBuffer source;
    std::vector<SharedImageBuffer> auxiliary;
    {
        const LentBuffersRemoval removal({&source}, &auxiliary);
        request.bufferId = "run.src";
        an::Result<SharedImageBuffer> exported = manager().exportImage(lease->image(), request);
        ASSERT_TRUE(exported.hasValue()) << exported.status().message();
        source = std::move(*exported);
        // A buffer lent after the removal was armed goes with the rest.
        request.bufferId = "run.mask";
        an::Result<SharedImageBuffer> mask = manager().exportImage(lease->image(), request);
        ASSERT_TRUE(mask.hasValue()) << mask.status().message();
        auxiliary.push_back(std::move(*mask));
        EXPECT_FALSE(filesUnder(rootPath()).empty());
    }
    EXPECT_TRUE(filesUnder(rootPath()).empty()) << "a run's buffers and their sidecars are removed when it ends";
    EXPECT_FALSE(source.valid());
}

TEST_F(HostPreviewFramesTest, AFrameWithNothingToLendIsRefused)
{
    PreviewFrameBufferPool pool(manager(), "frames");
    ExtensionPreviewFrame frame;
    an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, frame);
    ASSERT_FALSE(lent.hasValue());
    EXPECT_EQ(lent.status().code(), an::ErrorCode::NotInitialized);
    EXPECT_TRUE(filesUnder(rootPath()).empty());
}

// ---- Buffers kept between frames.

TEST_F(HostPreviewFramesTest, TwoFramesOfOneShapeAreLentTheSameFilesAndKeepOneFileOfEachRoleBetweenThem)
{
    const an::consumer::ImageAsset first = greyAsset(40u, 30u, 0.25);
    const an::consumer::ImageAsset second = greyAsset(40u, 30u, 0.5);
    PreviewFrameBufferPool pool(manager(), "frames", keepingEverything());

    std::filesystem::path sourceFile;
    std::filesystem::path outputFile;
    {
        LeasedFrame leased(first);
        an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
        ASSERT_TRUE(lent.hasValue()) << lent.status().message();
        sourceFile = lent->source.filePath();
        outputFile = lent->output.filePath();
    }
    // Between the frames: the source and the output, each with its sidecar, and nothing else.
    const std::set<std::filesystem::path> between = files();
    EXPECT_EQ(between.size(), 4u);
    EXPECT_TRUE(between.contains(sourceFile));
    EXPECT_TRUE(between.contains(outputFile));
    EXPECT_EQ(pool.idleBuffers(), 2u);

    LeasedFrame leased(second);
    an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
    ASSERT_TRUE(lent.hasValue()) << lent.status().message();
    EXPECT_EQ(lent->source.filePath(), sourceFile);
    EXPECT_EQ(lent->output.filePath(), outputFile);
    EXPECT_EQ(files(), between) << "no file was made for the second frame";
    an::Result<an::ui::extensions::ImportedImage> read =
        manager().importImage(suiteSession(), lent->source.descriptor());
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_NEAR(assetSampleAt(read->asset, 5u, 5u), 0.5, 1e-6) << "the second frame's samples, in the first's file";
}

TEST_F(HostPreviewFramesTest, AFrameOfAnotherShapeIsLentFilesOfItsOwn)
{
    const an::consumer::ImageAsset small = greyAsset(40u, 30u, 0.25);
    const an::consumer::ImageAsset large = greyAsset(80u, 60u, 0.25);
    PreviewFrameBufferPool pool(manager(), "frames", keepingEverything());

    std::filesystem::path smallSource;
    {
        LeasedFrame leased(small);
        an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
        ASSERT_TRUE(lent.hasValue()) << lent.status().message();
        smallSource = lent->source.filePath();
    }
    LeasedFrame leased(large);
    an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
    ASSERT_TRUE(lent.hasValue()) << lent.status().message();
    EXPECT_NE(lent->source.filePath(), smallSource);
    EXPECT_EQ(lent->source.descriptor().width, 80);
    EXPECT_EQ(files().size(), 8u) << "the small frame's buffers wait for a frame of their shape";
}

TEST_F(HostPreviewFramesTest, ABufferLentToAFrameIsNotLentToAFrameThatStartsBeforeItEnds)
{
    const an::consumer::ImageAsset image = greyAsset(40u, 30u, 0.25);
    PreviewFrameBufferPool pool(manager(), "frames", keepingEverything());
    {
        // Two frames before: their buffers wait in the pool.
        LeasedFrame a(image);
        LeasedFrame b(image);
        an::Result<PreviewFrameBuffers> first = lendPreviewFrame(pool, a.frame);
        an::Result<PreviewFrameBuffers> second = lendPreviewFrame(pool, b.frame);
        ASSERT_TRUE(first.hasValue() && second.hasValue());
    }
    ASSERT_EQ(pool.idleBuffers(), 4u);

    LeasedFrame a(image);
    LeasedFrame b(image);
    LeasedFrame c(image);
    an::Result<PreviewFrameBuffers> first = lendPreviewFrame(pool, a.frame);
    an::Result<PreviewFrameBuffers> second = lendPreviewFrame(pool, b.frame);
    an::Result<PreviewFrameBuffers> third = lendPreviewFrame(pool, c.frame);
    ASSERT_TRUE(first.hasValue() && second.hasValue() && third.hasValue());
    const std::set<std::filesystem::path> lentFiles{first->source.filePath(),  first->output.filePath(),
                                                    second->source.filePath(), second->output.filePath(),
                                                    third->source.filePath(),  third->output.filePath()};
    EXPECT_EQ(lentFiles.size(), 6u) << "no buffer is lent to two frames at once";
    EXPECT_EQ(pool.idleBuffers(), 0u);
}

TEST_F(HostPreviewFramesTest, ABufferOverTheSizeBoundIsRemovedWhenItsFrameEnds)
{
    const an::consumer::ImageAsset small = greyAsset(8u, 6u, 0.25);
    const an::consumer::ImageAsset large = greyAsset(80u, 60u, 0.25);
    PreviewFrameBufferPool pool(
        manager(), "frames",
        PreviewFrameBufferPoolBounds{.largestBufferBytes = floatBytes(40u, 30u), .idleBytes = 1ull << 30u});
    {
        LeasedFrame leased(large);
        an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
        ASSERT_TRUE(lent.hasValue()) << lent.status().message();
        EXPECT_EQ(files().size(), 4u);
    }
    EXPECT_TRUE(files().empty()) << "a buffer larger than the largest kept goes with its frame";
    {
        LeasedFrame leased(small);
        an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
        ASSERT_TRUE(lent.hasValue()) << lent.status().message();
    }
    EXPECT_EQ(files().size(), 4u) << "a buffer within the bound is kept";
    EXPECT_EQ(pool.idleBytes(), 2u * floatBytes(8u, 6u));
}

TEST_F(HostPreviewFramesTest, TheIdleBuffersNeverPassTheirBoundTheLeastRecentlyLentGoingFirst)
{
    // Room for three of the frames' buffers, each 40x30 Float32.
    const std::uint64_t bound = 3u * floatBytes(40u, 30u);
    PreviewFrameBufferPool pool(manager(), "frames",
                                PreviewFrameBufferPoolBounds{.largestBufferBytes = 1ull << 30u, .idleBytes = bound});
    const an::consumer::ImageAsset image = greyAsset(40u, 30u, 0.25);
    LeasedFrame a(image);
    LeasedFrame b(image);
    an::Result<PreviewFrameBuffers> first = lendPreviewFrame(pool, a.frame);
    an::Result<PreviewFrameBuffers> second = lendPreviewFrame(pool, b.frame);
    ASSERT_TRUE(first.hasValue() && second.hasValue());
    const std::filesystem::path firstSource = first->source.filePath();
    const std::filesystem::path secondSource = second->source.filePath();
    const std::filesystem::path secondOutput = second->output.filePath();

    first->release();
    EXPECT_LE(pool.idleBytes(), bound);
    EXPECT_EQ(pool.idleBuffers(), 2u);
    second->release();
    EXPECT_LE(pool.idleBytes(), bound) << "four buffers came back to room for three";
    EXPECT_EQ(pool.idleBuffers(), 3u);
    const std::set<std::filesystem::path> kept = files();
    EXPECT_TRUE(kept.contains(secondSource)) << "the buffers lent last are kept";
    EXPECT_TRUE(kept.contains(secondOutput));
    EXPECT_FALSE(kept.contains(firstSource)) << "the buffer lent first goes first";
}

TEST_F(HostPreviewFramesTest, AnIdleBufferNoFrameAskedForInSixteenFramesGoes)
{
    PreviewFrameBufferPool pool(manager(), "frames", keepingEverything());
    const an::consumer::ImageAsset odd = greyAsset(24u, 18u, 0.25);
    const an::consumer::ImageAsset usual = greyAsset(40u, 30u, 0.25);
    std::filesystem::path oddSource;
    {
        LeasedFrame leased(odd);
        an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
        ASSERT_TRUE(lent.hasValue());
        oddSource = lent->source.filePath();
    }
    for (int frame = 1; frame <= 16; ++frame)
    {
        LeasedFrame leased(usual);
        an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
        ASSERT_TRUE(lent.hasValue());
        lent->release();
        if (frame < 16)
        {
            EXPECT_TRUE(files().contains(oddSource)) << "frame " << frame;
        }
    }
    EXPECT_FALSE(files().contains(oddSource)) << "sixteen frames asked for another shape";
    EXPECT_EQ(pool.idleBuffers(), 2u);
}

TEST_F(HostPreviewFramesTest, LettingThePoolGoRemovesEveryFileAndABufferStillLentGoesWithItsFrame)
{
    const an::consumer::ImageAsset image = greyAsset(40u, 30u, 0.25);
    const an::consumer::ImageAsset mask = greyAsset(40u, 30u, 1.0);
    an::Result<an::consumer::AssetReadLease> maskLease = mask.read();
    ASSERT_TRUE(maskLease.hasValue());
    std::optional<PreviewFrameBuffers> stillLent;
    {
        PreviewFrameBufferPool pool(manager(), "frames", keepingEverything());
        {
            LeasedFrame leased(image);
            leased.frame.mask = &maskLease->image();
            an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
            ASSERT_TRUE(lent.hasValue()) << lent.status().message();
        }
        LeasedFrame leased(image);
        an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
        ASSERT_TRUE(lent.hasValue()) << lent.status().message();
        stillLent.emplace(std::move(*lent));
        EXPECT_EQ(files().size(), 6u) << "the source, the output and the selection, each with its sidecar";
    }
    // The pool is gone: what it kept is gone with it, and the frame still running keeps only its own buffers.
    EXPECT_EQ(files().size(), 4u);
    EXPECT_TRUE(stillLent->source.valid());
    stillLent.reset();
    EXPECT_TRUE(files().empty());
}

TEST_F(HostPreviewFramesTest, AClosedPoolKeepsNothingAFrameGivesBack)
{
    const an::consumer::ImageAsset image = greyAsset(40u, 30u, 0.25);
    PreviewFrameBufferPool pool(manager(), "frames", keepingEverything());
    LeasedFrame leased(image);
    an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
    ASSERT_TRUE(lent.hasValue()) << lent.status().message();
    {
        LeasedFrame other(image);
        an::Result<PreviewFrameBuffers> idle = lendPreviewFrame(pool, other.frame);
        ASSERT_TRUE(idle.hasValue());
    }
    pool.close();
    EXPECT_EQ(files().size(), 4u) << "the idle buffers went at once";
    lent->release();
    EXPECT_TRUE(files().empty()) << "a buffer given back to a closed pool is removed";
    EXPECT_EQ(pool.idleBuffers(), 0u);

    // A frame that starts once the pool is closed still runs, and leaves nothing behind.
    an::Result<PreviewFrameBuffers> late = lendPreviewFrame(pool, leased.frame);
    ASSERT_TRUE(late.hasValue()) << late.status().message();
    late->release();
    EXPECT_TRUE(files().empty());
}

TEST_F(HostPreviewFramesTest, AnOutputAFrameWroteHalfOfHoldsZerosInTheOtherHalf)
{
    const an::consumer::ImageAsset image = greyAsset(8u, 6u, 0.25);
    PreviewFrameBufferPool pool(manager(), "frames", keepingEverything());
    {
        LeasedFrame leased(image);
        an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
        ASSERT_TRUE(lent.hasValue()) << lent.status().message();
        fillRows(lent->output, 6, 0.5f);
    }
    LeasedFrame leased(image);
    an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
    ASSERT_TRUE(lent.hasValue()) << lent.status().message();
    fillRows(lent->output, 3, 0.25f);
    an::Result<an::consumer::ImageAsset> made = takePreviewFrameResult(pool.manager(), suiteSession(), *lent);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    EXPECT_NEAR(assetSampleAt(*made, 4u, 1u), 0.25, 1e-6);
    EXPECT_EQ(assetSampleAt(*made, 4u, 4u), 0.0) << "the rows this frame left unwritten start as a new file's do";
    EXPECT_EQ(assetSampleAt(*made, 7u, 5u), 0.0);
}

TEST_F(HostPreviewFramesTest, AFramesResultCarriesOnlyWhatItsOwnPreviewSet)
{
    const an::consumer::ImageAsset image = greyAsset(8u, 6u, 0.25);
    PreviewFrameBufferPool pool(manager(), "frames", keepingEverything());
    {
        LeasedFrame leased(image);
        an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
        ASSERT_TRUE(lent.hasValue()) << lent.status().message();
        setOnTheResult(lent->output, QStringLiteral("PREVIEW1"), QStringLiteral("'first'"));
        an::Result<an::consumer::ImageAsset> made = takePreviewFrameResult(pool.manager(), suiteSession(), *lent);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        EXPECT_EQ(cardOf(*made, "PREVIEW1"), std::optional<std::string>("'first'"));
        EXPECT_NE(fileBytes(lent->output.descriptor().metadata_sidecar),
                  fileBytes(lent->source.descriptor().metadata_sidecar));
    }
    LeasedFrame leased(image);
    an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
    ASSERT_TRUE(lent.hasValue()) << lent.status().message();
    EXPECT_EQ(fileBytes(lent->output.descriptor().metadata_sidecar),
              fileBytes(lent->source.descriptor().metadata_sidecar))
        << "each frame's output starts with the source's cards";
    an::Result<an::consumer::ImageAsset> made = takePreviewFrameResult(pool.manager(), suiteSession(), *lent);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    EXPECT_FALSE(cardOf(*made, "PREVIEW1").has_value()) << "the earlier frame's result set it, this one's did not";
}

TEST_F(HostPreviewFramesTest, ASourcesSidecarIsWrittenOnlyWhenItsCardsChange)
{
    const std::shared_ptr<const an::consumer::ImageAsset> image = solvedAsset(64u, 48u);
    ASSERT_TRUE(image);
    const an::consumer::ImageAsset cut = greyAsset(20u, 12u, 0.75);
    PreviewFrameBufferPool pool(manager(), "frames", keepingEverything());
    const auto lendPart = [&](const QRect& part) {
        LeasedFrame leased(cut);
        leased.frame.image = image;
        leased.frame.part = part;
        leased.frame.imageSize = QSize(64, 48);
        an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
        EXPECT_TRUE(lent.hasValue()) << lent.status().message();
        return lent.hasValue() ? QJsonDocument::fromJson(fileBytes(lent->source.descriptor().metadata_sidecar)).object()
                               : QJsonObject();
    };

    const QJsonObject first = lendPart(QRect(10, 8, 20, 12));
    EXPECT_EQ(pool.sidecarWrites(), 1u);
    EXPECT_DOUBLE_EQ(first.value(QStringLiteral("CRPIX1")).toString().toDouble(), 22.0);
    lendPart(QRect(10, 8, 20, 12));
    EXPECT_EQ(pool.sidecarWrites(), 1u) << "the same part's cards are already in the file";

    // A pan: the part moved, and so did the reference pixel its cards state.
    const QJsonObject moved = lendPart(QRect(30, 20, 20, 12));
    EXPECT_EQ(pool.sidecarWrites(), 2u);
    EXPECT_DOUBLE_EQ(moved.value(QStringLiteral("CRPIX1")).toString().toDouble(), 2.0);
    EXPECT_DOUBLE_EQ(moved.value(QStringLiteral("CRPIX2")).toString().toDouble(), 4.0);

    // A reduction carries no revision of its own: its cards are compared, and the same ones are not written again.
    const an::consumer::ImageAsset reduced = greyAsset(16u, 12u, 0.25);
    for (int frame = 0; frame < 3; ++frame)
    {
        LeasedFrame leased(reduced);
        an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
        ASSERT_TRUE(lent.hasValue()) << lent.status().message();
    }
    EXPECT_EQ(pool.sidecarWrites(), 3u);
}

// ---- What a frame's way costs.

TEST_F(HostPreviewFramesTest, AFrameStatesWhatItLentAndHowLongEachStepOfItsWayTook)
{
    const an::consumer::ImageAsset reduced = greyAsset(40u, 30u, 0.25);
    const an::consumer::ImageAsset mask = greyAsset(40u, 30u, 1.0);
    LeasedFrame leased(reduced);
    an::Result<an::consumer::AssetReadLease> maskLease = mask.read();
    ASSERT_TRUE(maskLease.hasValue());
    leased.frame.mask = &maskLease->image();
    leased.frame.interactive = true;

    PreviewFrameBufferPool pool(manager(), "frames", keepingNothing());
    PreviewFrameSpans spans;
    an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame, &spans);
    ASSERT_TRUE(lent.hasValue()) << lent.status().message();
    EXPECT_EQ(spans.width, 40);
    EXPECT_EQ(spans.height, 30);
    EXPECT_EQ(spans.channels, 1);
    EXPECT_EQ(spans.pixelType, SharedBufferPixelType::Float32);
    EXPECT_TRUE(spans.interactive);
    EXPECT_GT(spans.lendSource.count(), 0);
    EXPECT_GT(spans.lendOutput.count(), 0);
    EXPECT_GT(spans.lendMask.count(), 0);
    EXPECT_EQ(spans.take.count(), 0) << "nothing was taken back yet";

    an::Result<an::consumer::ImageAsset> made = takePreviewFrameResult(pool.manager(), suiteSession(), *lent, &spans);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    EXPECT_GT(spans.take.count(), 0);
    lent->release(&spans);
    EXPECT_GT(spans.release.count(), 0);
    EXPECT_TRUE(filesUnder(rootPath()).empty());
    // What only the host sees - the extension's word on the parameters, its binding, its run and its flush - stays
    // unmeasured here.
    EXPECT_EQ(spans.prepare.count(), 0);
    EXPECT_EQ(spans.bind.count(), 0);
    EXPECT_EQ(spans.run.count(), 0);
    EXPECT_EQ(spans.flush.count(), 0);
}

TEST_F(HostPreviewFramesTest, TheTraceStatesItsColumnsOnceAndThenALineAFrame)
{
    PreviewFrameSpans first;
    first.width = 640;
    first.height = 480;
    first.channels = 3;
    first.pixelType = SharedBufferPixelType::UInt16;
    first.interactive = true;
    first.prepare = std::chrono::microseconds(1500);
    first.lendSource = std::chrono::microseconds(250);
    first.run = std::chrono::nanoseconds(12345678);
    first.release = std::chrono::nanoseconds(120);
    PreviewFrameSpans second;
    second.width = 64;
    second.height = 48;
    second.channels = 1;

    const std::filesystem::path path = rootPath() / "frames.csv";
    {
        PreviewFrameTrace trace(path);
        EXPECT_TRUE(trace.enabled());
        trace.write(first);
        trace.write(second);
    }
    QFile file(QString::fromStdWString(path.wstring()));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QList<QByteArray> lines = file.readAll().split('\n');
    ASSERT_EQ(lines.size(), 4) << "a header, two frames and the last line's end";
    EXPECT_EQ(lines[0], previewFrameTraceHeader());
    EXPECT_EQ(lines[0], QByteArrayLiteral("width,height,channels,pixel_type,interactive,prepare_us,lend_source_us,"
                                          "lend_output_us,lend_mask_us,bind_us,run_us,flush_us,take_us,release_us"));
    EXPECT_EQ(lines[1], QByteArrayLiteral("640,480,3,uint16,1,1500.0,250.0,0.0,0.0,0.0,12345.7,0.0,0.0,0.1"));
    EXPECT_EQ(lines[2], previewFrameTraceLine(second));
    EXPECT_TRUE(lines[3].isEmpty());

    // A trace that names no file writes nothing.
    PreviewFrameTrace silent;
    EXPECT_FALSE(silent.enabled());
    silent.write(first);
    EXPECT_EQ(filesUnder(rootPath()).size(), 1u);
}

// Of the idle buffers of a shape, the one lent longest ago is lent next: every buffer two frames of one shape left is
// lent in turn, so none ages out while the others serve.
TEST_F(HostPreviewFramesTest, TheIdleBufferLentLongestAgoIsLentNextSoEveryKeptBufferOfAShapeServesInTurn)
{
    const an::consumer::ImageAsset image = greyAsset(40u, 30u, 0.25);
    PreviewFrameBufferPool pool(manager(), "frames", keepingEverything());
    LeasedFrame first(image);
    LeasedFrame second(image);
    LeasedFrame third(image);
    LeasedFrame fourth(image);

    // Two frames at once take two buffers; the first is given back and lent again, so the second's is now the one lent
    // longest ago.
    an::Result<PreviewFrameBuffers> a = lendPreviewFrame(pool, first.frame);
    an::Result<PreviewFrameBuffers> b = lendPreviewFrame(pool, second.frame);
    ASSERT_TRUE(a.hasValue() && b.hasValue());
    const std::filesystem::path aFile = a->source.filePath();
    const std::filesystem::path bFile = b->source.filePath();
    ASSERT_NE(aFile, bFile);
    {
        std::optional<PreviewFrameBuffers> held(std::move(*a));
        held.reset();
    }
    an::Result<PreviewFrameBuffers> c = lendPreviewFrame(pool, third.frame);
    ASSERT_TRUE(c.hasValue());
    EXPECT_EQ(c->source.filePath(), aFile) << "the only idle buffer serves";
    {
        std::optional<PreviewFrameBuffers> heldB(std::move(*b));
        heldB.reset();
        std::optional<PreviewFrameBuffers> heldC(std::move(*c));
        heldC.reset();
    }
    EXPECT_EQ(pool.idleBuffers(), 4u);

    an::Result<PreviewFrameBuffers> d = lendPreviewFrame(pool, fourth.frame);
    ASSERT_TRUE(d.hasValue());
    EXPECT_EQ(d->source.filePath(), bFile) << "the buffer lent longest ago is lent, not the first made";
}

// Frames lent from several threads at once - a frame and the sampled frame its histogram runs, or a frame and a run of
// the extension - never share a buffer, and what they give back stays within the pool's bounds.
TEST_F(HostPreviewFramesTest, FramesLentFromSeveralThreadsAtOnceNeverShareABufferAndTheIdleBoundHolds)
{
    const an::consumer::ImageAsset image = greyAsset(40u, 30u, 0.25);
    PreviewFrameBufferPool pool(manager(), "frames", keepingEverything());
    constexpr int kThreads = 4;
    constexpr int kFramesEach = 24;

    std::mutex lentMutex;
    std::set<std::filesystem::path> lentNow;
    std::atomic<int> sharedFiles{0};
    std::atomic<int> refused{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&]() {
            for (int frame = 0; frame < kFramesEach; ++frame)
            {
                LeasedFrame leased(image);
                an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(pool, leased.frame);
                if (!lent.hasValue())
                {
                    ++refused;
                    continue;
                }
                const std::filesystem::path source = lent->source.filePath();
                const std::filesystem::path output = lent->output.filePath();
                {
                    const std::lock_guard lock(lentMutex);
                    if (!lentNow.insert(source).second || !lentNow.insert(output).second)
                    {
                        ++sharedFiles;
                    }
                }
                std::this_thread::yield();
                {
                    const std::lock_guard lock(lentMutex);
                    lentNow.erase(source);
                    lentNow.erase(output);
                }
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    EXPECT_EQ(refused.load(), 0);
    EXPECT_EQ(sharedFiles.load(), 0) << "a buffer was lent to two frames at once";
    EXPECT_LE(pool.idleBuffers(), static_cast<std::size_t>(2 * kThreads))
        << "no more buffers are kept than the frames that ran at once took";
    EXPECT_GE(pool.idleBuffers(), 2u);
}

} // namespace
