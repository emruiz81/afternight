// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the step codec cases share: the codecs under test by name, an image built from a description, samples given
/// as bit patterns, metadata and a mask, and everything a step is read out of an asset so two of them compare whole.
#pragma once

#include "Test_ImageViewControl/history_test_support.h"

#include "controls/ImageHistoryStepCodec.h"

#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/XisfCodec.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace an::tests::history
{

/// One codec under test, named for the case it parameterises.
struct CodecCase
{
    const char* name;
    an::ui::history::StepCodecPtr (*make)();
};

inline void PrintTo(const CodecCase& codec, std::ostream* stream)
{
    *stream << codec.name;
}

/// Every codec a history can be given: the snapshot, and XISF with each compression.
[[nodiscard]] inline std::vector<CodecCase> everyStepCodec()
{
    using an::ui::history::StepCompression;
    return {
        {"Snapshot", [] { return an::ui::history::snapshotStepCodec(); }},
        {"XisfUncompressed", [] { return an::ui::history::xisfStepCodec(StepCompression::None); }},
        {"XisfLz4", [] { return an::ui::history::xisfStepCodec(StepCompression::Lz4); }},
        {"XisfZstd", [] { return an::ui::history::xisfStepCodec(StepCompression::Zstd); }},
    };
}

/// The codec that writes a file another program opens as an image, with each compression.
[[nodiscard]] inline std::vector<CodecCase> everyXisfStepCodec()
{
    using an::ui::history::StepCompression;
    return {
        {"XisfUncompressed", [] { return an::ui::history::xisfStepCodec(StepCompression::None); }},
        {"XisfLz4", [] { return an::ui::history::xisfStepCodec(StepCompression::Lz4); }},
        {"XisfZstd", [] { return an::ui::history::xisfStepCodec(StepCompression::Zstd); }},
    };
}

/// A session of two threads whose context reads and writes XISF and FITS files, as the application's does - the first
/// for the steps, the second for the cases that hold a step to another format's file; fails the case when it cannot
/// be opened.
[[nodiscard]] inline std::optional<TestSessionProvider> openStepCodecSession()
{
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = 2u;
    settings.codecRegistry = []() -> an::Result<std::shared_ptr<const an::CodecRegistry>> {
        an::Result<std::shared_ptr<const an::ImageCodec>> fits = an::createFitsCodec();
        if (!fits.hasValue())
        {
            return fits.status();
        }
        an::Result<std::shared_ptr<const an::ImageCodec>> xisf = an::createXisfCodec();
        if (!xisf.hasValue())
        {
            return xisf.status();
        }
        return an::CodecRegistry::create(std::vector<std::shared_ptr<const an::ImageCodec>>{*fits, *xisf});
    };
    an::Result<an::consumer::EngineSession> session = an::consumer::EngineSession::open(settings);
    EXPECT_TRUE(session.hasValue()) << session.status().message();
    if (!session.hasValue())
    {
        return std::nullopt;
    }
    return std::optional<TestSessionProvider>(std::in_place, std::move(*session));
}

/// The bits of the sample at (x, y, channel); the low bytes of the answer are the sample.
using SampleBits = std::function<std::uint64_t(std::uint32_t x, std::uint32_t y, std::uint32_t channel)>;

/// A mask's bit at (x, y, plane).
using MaskBits = std::function<bool(std::uint32_t x, std::uint32_t y, std::uint32_t plane)>;

/// A well-mixed 64-bit value of three coordinates and a salt, so every byte of every sample is exercised.
[[nodiscard]] inline std::uint64_t mixedBits(std::uint32_t x, std::uint32_t y, std::uint32_t channel,
                                             std::uint64_t salt = 0u) noexcept
{
    std::uint64_t value = (static_cast<std::uint64_t>(x) << 40u) ^ (static_cast<std::uint64_t>(y) << 20u) ^
                          static_cast<std::uint64_t>(channel) ^ (salt * 0x9e3779b97f4a7c15ull);
    value ^= value >> 33u;
    value *= 0xff51afd7ed558ccdull;
    value ^= value >> 33u;
    value *= 0xc4ceb9fe1a85ec53ull;
    value ^= value >> 33u;
    return value;
}

/// What to build a step's image from.
struct StepImageRequest
{
    an::SampleType type = an::SampleType::Float32;
    std::uint32_t width = 13u;
    std::uint32_t height = 7u;
    an::tests::viewer::Channels channels = an::tests::viewer::Channels::Grey;
    an::NumericDomain domain = an::NumericDomain::NormalizedLinear;
    an::CfaDescriptor cfa{};
    an::CoordinateOrigin origin = an::CoordinateOrigin::TopLeft;
    an::ImageMetadata metadata{};
    SampleBits samples = [](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return mixedBits(x, y, c); };
    /// How many planes the mask has: zero for none, one shared by the channels, or the channel count.
    std::uint32_t maskPlanes = 0u;
    MaskBits mask = [](std::uint32_t x, std::uint32_t y, std::uint32_t plane) {
        return (mixedBits(x, y, plane, 77u) & 3u) != 0u;
    };
};

/// An asset of the session built from the request; null, with the case failed, when it cannot be built.
[[nodiscard]] inline std::shared_ptr<an::consumer::ImageAsset> makeStepImage(const an::consumer::EngineSession& session,
                                                                             const StepImageRequest& request)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        ADD_FAILURE() << hold.status().message();
        return nullptr;
    }
    const std::uint32_t channelCount = an::tests::viewer::channelCountOf(request.channels);
    const std::optional<an::ImageExtent> imageExtent =
        an::ImageExtent::create(request.width, request.height, channelCount, hold->engine().limits());
    const std::optional<an::ImageDescriptor> descriptor =
        !imageExtent.has_value() ? std::nullopt
                                 : an::ImageDescriptor::create(an::ImageDescriptorRequest{
                                       .extent = *imageExtent,
                                       .sampleType = request.type,
                                       .channelSchema = request.channels == an::tests::viewer::Channels::Grey
                                                            ? an::ChannelSchema::Mono
                                                            : (request.channels == an::tests::viewer::Channels::Rgb
                                                                   ? an::ChannelSchema::Rgb
                                                                   : an::ChannelSchema::Rgba),
                                       .colorSpace = an::ColorSpace::Unspecified,
                                       .numericDomain = request.domain,
                                       .cfa = request.cfa,
                                       .coordinateOrigin = request.origin,
                                       .pixelAspectRatio = 1.0});
    if (!descriptor.has_value())
    {
        ADD_FAILURE() << "the image could not be described";
        return nullptr;
    }
    std::optional<an::consumer::AnyImage> built;
    const bool dispatched = an::dispatchSampleType(request.type, [&]<typename SampleT>() {
        an::Result<an::Image<SampleT>> image =
            an::Image<SampleT>::create(hold->engine(), *descriptor, an::StorageArrangement::Planar,
                                       an::StorageInitialization::Zeroed, request.metadata);
        if (!image.hasValue())
        {
            ADD_FAILURE() << image.status().message();
            return;
        }
        {
            std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
            for (std::uint32_t c = 0u; c < channelCount; ++c)
            {
                for (std::uint32_t y = 0u; y < request.height; ++y)
                {
                    const auto row = access->view().row(c, y);
                    for (std::uint32_t x = 0u; x < request.width; ++x)
                    {
                        // The bits are copied, never converted: a not-a-number keeps its payload on its way in.
                        const std::uint64_t bits = request.samples(x, y, c);
                        std::memcpy(&(*row)[x], &bits, sizeof(SampleT));
                    }
                }
            }
        }
        built.emplace(std::move(*image));
    });
    if (!dispatched || !built.has_value())
    {
        ADD_FAILURE() << "the image could not be made";
        return nullptr;
    }
    std::optional<an::ValidityMask> mask;
    if (request.maskPlanes != 0u)
    {
        const std::optional<an::ImageExtent> extent =
            an::ImageExtent::create(request.width, request.height, request.maskPlanes, hold->engine().limits());
        an::Result<an::ValidityMask> made = an::ValidityMask::createAllValid(hold->engine(), *extent);
        if (!made.hasValue())
        {
            ADD_FAILURE() << made.status().message();
            return nullptr;
        }
        for (std::uint32_t plane = 0u; plane < request.maskPlanes; ++plane)
        {
            for (std::uint32_t y = 0u; y < request.height; ++y)
            {
                for (std::uint32_t x = 0u; x < request.width; ++x)
                {
                    (void)made->view()->setValid(plane, x, y, request.mask(x, y, plane));
                }
            }
        }
        mask.emplace(std::move(*made));
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, std::move(*built), std::move(mask));
    if (!asset.hasValue())
    {
        ADD_FAILURE() << asset.status().message();
        return nullptr;
    }
    return std::make_shared<an::consumer::ImageAsset>(std::move(*asset));
}

/// Everything a step is, read out of an asset.
struct StepState
{
    bool read = false;
    an::SampleType type = an::SampleType::UInt8;
    std::optional<an::ImageDescriptor> descriptor;
    /// How the samples are arranged: the planes they are in and the samples a pixel holds in one. A step keeps no row
    /// or plane padding, so the strides are not part of it.
    std::uint32_t planes = 0u;
    std::uint32_t samplesPerPixel = 0u;
    an::ImageMetadata metadata;
    /// Every sample's bytes, plane by plane and row by row, with no padding.
    std::vector<std::byte> samples;
    bool hasMask = false;
    std::uint32_t maskPlanes = 0u;
    std::uint32_t maskWidth = 0u;
    std::uint32_t maskHeight = 0u;
    /// Every mask row's bytes, plane by plane, its pad bits included.
    std::vector<std::uint8_t> mask;
};

/// The state of an asset; `read` is false, with the case failed, when it cannot be leased.
[[nodiscard]] inline StepState stepStateOf(const an::consumer::ImageAsset& asset)
{
    StepState state;
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!lease.hasValue())
    {
        ADD_FAILURE() << lease.status().message();
        return state;
    }
    lease->visit([&state]<typename SampleT>(const an::Image<SampleT>& typed) {
        state.type = an::kSampleTypeOf<SampleT>;
        state.descriptor = typed.descriptor();
        state.metadata = typed.metadata();
        const auto view = typed.view();
        if (!view.has_value())
        {
            ADD_FAILURE() << "the image holds no samples";
            return;
        }
        state.planes = view->layout().planeCount();
        state.samplesPerPixel = view->layout().samplesPerPixel();
        for (std::uint32_t c = 0u; c < view->planeCount(); ++c)
        {
            for (std::uint32_t y = 0u; y < view->height(); ++y)
            {
                const auto row = view->row(c, y);
                const auto* const bytes = reinterpret_cast<const std::byte*>(row->data());
                state.samples.insert(state.samples.end(), bytes, bytes + row->size_bytes());
            }
        }
        state.read = true;
    });
    if (const an::ValidityMask* const mask = lease->validity(); mask != nullptr)
    {
        const auto view = mask->view();
        state.hasMask = true;
        state.maskPlanes = view->planeCount();
        state.maskWidth = view->width();
        state.maskHeight = view->height();
        for (std::uint32_t plane = 0u; plane < view->planeCount(); ++plane)
        {
            for (std::uint32_t y = 0u; y < view->height(); ++y)
            {
                const std::uint8_t* const row =
                    view->data() + plane * view->planeStrideBytes() + y * view->rowStrideBytes();
                state.mask.insert(state.mask.end(), row, row + view->minimumRowBytes());
            }
        }
    }
    return state;
}

/// Fails the case for each part of `restored` that is not `original`'s: the representation, the description, the
/// arrangement, every sample bit for bit, the metadata and the mask.
inline void expectSameStep(const StepState& original, const StepState& restored)
{
    ASSERT_TRUE(original.read);
    ASSERT_TRUE(restored.read);
    EXPECT_EQ(original.type, restored.type) << "the representation changed";
    EXPECT_TRUE(original.descriptor == restored.descriptor) << "the description changed";
    EXPECT_EQ(original.planes, restored.planes) << "the arrangement changed";
    EXPECT_EQ(original.samplesPerPixel, restored.samplesPerPixel) << "the arrangement changed";
    ASSERT_EQ(original.samples.size(), restored.samples.size());
    EXPECT_EQ(std::memcmp(original.samples.data(), restored.samples.data(), original.samples.size()), 0)
        << "a sample changed";
    EXPECT_TRUE(original.metadata.acquisition() == restored.metadata.acquisition()) << "the acquisition record changed";
    EXPECT_TRUE(original.metadata.semantics() == restored.metadata.semantics()) << "the processing meaning changed";
    EXPECT_TRUE(original.metadata.astrometry() == restored.metadata.astrometry()) << "the astrometric solution changed";
    EXPECT_TRUE(original.metadata.provenance() == restored.metadata.provenance()) << "the lineage changed";
    EXPECT_TRUE(original.metadata.codec() == restored.metadata.codec()) << "the preserved cards changed";
    EXPECT_TRUE(original.metadata.outputIntent() == restored.metadata.outputIntent()) << "the output intent changed";
    EXPECT_EQ(original.hasMask, restored.hasMask) << "the mask appeared or went";
    EXPECT_EQ(original.maskPlanes, restored.maskPlanes);
    EXPECT_EQ(original.maskWidth, restored.maskWidth);
    EXPECT_EQ(original.maskHeight, restored.maskHeight);
    EXPECT_TRUE(original.mask == restored.mask) << "a bit of the mask changed";
}

/// The identity a case writes its step for.
[[nodiscard]] inline an::ArtifactIdentity stepIdentityOf(const std::string& artifact,
                                                         const std::string& run = "run-of-the-case")
{
    return *an::ArtifactIdentity::create(artifact, run);
}

/// A path as the UTF-8 text the codecs take.
[[nodiscard]] inline std::string utf8Of(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

/// The names of the entries a directory holds.
[[nodiscard]] inline std::vector<std::string> entriesOf(const std::filesystem::path& directory)
{
    std::vector<std::string> names;
    std::error_code error;
    for (std::filesystem::directory_iterator entry(directory, error), end; !error && entry != end;
         entry.increment(error))
    {
        names.push_back(entry->path().filename().string());
    }
    return names;
}

} // namespace an::tests::history
