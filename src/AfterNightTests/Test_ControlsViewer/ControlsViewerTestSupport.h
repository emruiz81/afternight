// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the viewer's cases share beyond the session bridge's: images of any of the eight representations and of grey,
/// colour or colour-with-alpha channels, built through the engine from a function of position and channel and held
/// as assets or documents of the provider's session.
#pragma once

#include "Test_ControlsSession/ControlsSessionTestSupport.h"

#include "controls/ImageViewFiles.h"
#include "controls/SequenceContainers.h"

#include <AfterNightConsumerRuntime/ConsumerMemory.h>
#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/TiffCodec.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <QString>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace an::tests::viewer
{

using an::tests::controls::kPatience;
using an::tests::controls::kQuietPeriod;
using an::tests::controls::openProvider;
using an::tests::controls::pumpQuietly;
using an::tests::controls::pumpUntil;
using an::tests::controls::TestSessionProvider;

/// A provider over a session of a given width whose context reads and writes FITS files; fails the case when the
/// session cannot be opened.
///
/// @param width the session's concurrency.
/// @param memory the session's consumer memory allowances; the session defaults unless a case states its own.
[[nodiscard]] inline std::optional<TestSessionProvider> openFileProvider(std::uint32_t width,
                                                                         an::consumer::MemoryAllowances memory = {})
{
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = width;
    settings.memory = memory;
    // FITS for the fixture files, and TIFF for what an external program is handed and answers.
    settings.codecRegistry = []() -> an::Result<std::shared_ptr<const an::CodecRegistry>> {
        an::Result<std::shared_ptr<const an::ImageCodec>> fits = an::createFitsCodec();
        if (!fits.hasValue())
        {
            return fits.status();
        }
        an::Result<std::shared_ptr<const an::ImageCodec>> tiff = an::createTiffCodec();
        if (!tiff.hasValue())
        {
            return tiff.status();
        }
        return an::CodecRegistry::create(std::vector<std::shared_ptr<const an::ImageCodec>>{*fits, *tiff});
    };
    an::Result<an::consumer::EngineSession> session = an::consumer::EngineSession::open(settings);
    EXPECT_TRUE(session.hasValue()) << session.status().message();
    if (!session.hasValue())
    {
        return std::nullopt;
    }
    return std::optional<TestSessionProvider>(std::in_place, std::move(*session));
}

/// What an image's channels are: grey, colour, or colour with alpha.
enum class Channels : std::uint8_t
{
    Grey,
    Rgb,
    Rgba
};

/// How many channels a choice holds.
[[nodiscard]] inline std::uint32_t channelCountOf(Channels channels) noexcept
{
    return channels == Channels::Grey ? 1u : (channels == Channels::Rgb ? 3u : 4u);
}

/// The description of an image of a representation, shape and domain.
[[nodiscard]] inline std::optional<an::ImageDescriptor>
describeImage(const an::EngineContext& engine, an::SampleType type, std::uint32_t width, std::uint32_t height,
              Channels channels, an::NumericDomain domain = an::NumericDomain::NormalizedLinear,
              an::CfaDescriptor cfa = an::CfaDescriptor{})
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channelCountOf(channels), engine.limits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    const an::ChannelSchema schema =
        channels == Channels::Grey ? an::ChannelSchema::Mono
                                   : (channels == Channels::Rgb ? an::ChannelSchema::Rgb : an::ChannelSchema::Rgba);
    return an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                                  .sampleType = type,
                                                                  .channelSchema = schema,
                                                                  .colorSpace = an::ColorSpace::Unspecified,
                                                                  .numericDomain = domain,
                                                                  .cfa = cfa,
                                                                  .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                                  .pixelAspectRatio = 1.0});
}

/// A new asset of a session of the representation `SampleT`, planar, each sample `value(x, y, channel)`; fails the
/// case when it cannot be built.
template <typename SampleT>
[[nodiscard]] std::optional<an::consumer::ImageAsset>
makeAsset(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height, Channels channels,
          const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)>& value,
          an::NumericDomain domain = an::NumericDomain::NormalizedLinear, an::CfaDescriptor cfa = an::CfaDescriptor{})
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue()) << hold.status().message();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor =
        describeImage(hold->engine(), an::kSampleTypeOf<SampleT>, width, height, channels, domain, cfa);
    EXPECT_TRUE(descriptor.has_value());
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<SampleT>> image = an::Image<SampleT>::create(
        hold->engine(), *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
        EXPECT_TRUE(access.has_value());
        if (!access.has_value())
        {
            return std::nullopt;
        }
        for (std::uint32_t channel = 0u; channel < channelCountOf(channels); ++channel)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                const auto row = access->view().row(channel, y);
                EXPECT_TRUE(row.has_value());
                if (!row.has_value())
                {
                    return std::nullopt;
                }
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    (*row)[x] = value(x, y, channel);
                }
            }
        }
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage{std::move(*image)});
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*asset);
}

/// A document around a new asset of `SampleT`; fails the case when it cannot be built.
template <typename SampleT>
[[nodiscard]] std::optional<an::ui::ImageDocument>
makeDocument(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height, Channels channels,
             const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)>& value,
             an::ui::DocumentNames names = {})
{
    std::optional<an::consumer::ImageAsset> asset = makeAsset<SampleT>(session, width, height, channels, value);
    if (!asset.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(*asset), std::move(names));
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*document);
}

/// An admitted operation of the session for a case that calls an engine operation directly; fails the case when it is
/// refused.
[[nodiscard]] inline std::optional<an::consumer::OperationScope> admitted(const an::consumer::EngineSession& session)
{
    an::Result<an::consumer::OperationScope> scope =
        session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
    EXPECT_TRUE(scope.hasValue()) << scope.status().message();
    if (!scope.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*scope);
}

/// Allowances under which the history cannot keep an edit's copy: a pending snapshot may hold one byte, and an
/// oversized one is not admitted - so with nothing pending the copy is refused at once, as a session whose allowance an
/// image outgrows refuses it.
[[nodiscard]] inline an::consumer::MemoryAllowances historyCannotKeepEdits()
{
    an::consumer::MemoryAllowances memory;
    memory.pendingSnapshots =
        an::consumer::CategoryAllowance{.bytes = 1u, .maxEntries = 2u, .admitsOneOversized = false};
    return memory;
}

/// Writes a SER sequence of grey frames, the sample of frame f at (x, y) being `value(f, x, y)`, published at `path`;
/// fails the case when it cannot. The container stores sixteen bits, so a value is kept to a 65,535th; a frame
/// described with a filter array is kept as the mosaic it is, its pattern stated in the header.
inline void writeSerFile(const an::consumer::EngineSession& session, const QString& path, std::uint32_t width,
                         std::uint32_t height, std::uint32_t frames,
                         const std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)>& value,
                         an::CfaDescriptor cfa = an::CfaDescriptor{})
{
    an::Result<an::ui::SequenceWriter> writer = an::ui::SequenceWriter::create(
        path, an::ui::SequenceWriteSettings{.container = an::SequenceFormat::Ser, .frameCount = frames});
    ASSERT_TRUE(writer.hasValue()) << writer.status().message();
    std::optional<an::consumer::OperationScope> scope = admitted(session);
    ASSERT_TRUE(scope.has_value());
    for (std::uint32_t frame = 0u; frame < frames; ++frame)
    {
        std::optional<an::consumer::ImageAsset> asset = makeAsset<float>(
            session, width, height, Channels::Grey,
            [&value, frame](std::uint32_t x, std::uint32_t y, std::uint32_t) { return value(frame, x, y); },
            an::NumericDomain::NormalizedLinear, cfa);
        ASSERT_TRUE(asset.has_value());
        an::Result<an::consumer::AssetReadLease> lease = asset->read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        const an::Status appended = writer->append(scope->engine(), scope->execution(), lease->image());
        ASSERT_TRUE(appended.ok()) << appended.message();
    }
    an::Result<an::SequenceDescriptor> published = writer->finish(scope->execution());
    ASSERT_TRUE(published.hasValue()) << published.status().message();
}

/// The filter array of an X-Trans sensor at its origin: the six-by-six table a Fujifilm camera's mosaic states.
[[nodiscard]] inline an::CfaDescriptor xtransFilterArray()
{
    using an::CfaColor;
    const CfaColor g = CfaColor::Green;
    const CfaColor r = CfaColor::Red;
    const CfaColor b = CfaColor::Blue;
    const std::array<CfaColor, an::kCfaTableEntries> table = {g, b, g, g, r, g, r, g, r, b, g, b, g, b, g, g, r, g,
                                                              g, r, g, g, b, g, b, g, b, r, g, r, g, r, g, g, b, g};
    an::CfaColorEntries entries{};
    for (std::size_t index = 0u; index < an::kCfaTableEntries; ++index)
    {
        entries[index] = table[index];
    }
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(entries, 0u, 0u);
    EXPECT_TRUE(cfa.has_value());
    return cfa.value_or(an::CfaDescriptor{});
}

/// A sixteen-bit X-Trans mosaic of the session: each sample its filter's level plus a texture on which the fast and the
/// Markesteijn estimates disagree; a multiple of six on each side keeps every block of the table whole. Fails the case
/// when it cannot be built.
[[nodiscard]] inline std::optional<an::consumer::ImageAsset>
xtransMosaicAsset(const an::consumer::EngineSession& session, std::uint32_t width = 72u, std::uint32_t height = 48u)
{
    const an::CfaDescriptor cfa = xtransFilterArray();
    return makeAsset<std::uint16_t>(
        session, width, height, Channels::Grey,
        [&cfa](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            const an::CfaColor colour = cfa.colorAt(x, y);
            const std::uint32_t level =
                colour == an::CfaColor::Red ? 9000u : (colour == an::CfaColor::Green ? 4000u : 1500u);
            const std::uint32_t texture = ((x * 7919u + y * 104729u + x * y * 31u) % 997u) * 20u;
            return static_cast<std::uint16_t>(2000u + level + texture);
        },
        an::NumericDomain::NormalizedLinear, cfa);
}

/// Writes that mosaic to a TIFF file, the one container of the test sessions that states a six-by-six filter array -
/// FITS states a two-by-two pattern, and its writer refuses a tabulated mosaic. Fails the case when it cannot.
///
/// @return whether the file was written.
[[nodiscard]] inline bool writeXTransTiff(const an::consumer::EngineSession& session, const QString& path,
                                          std::uint32_t width = 72u, std::uint32_t height = 48u)
{
    std::optional<an::consumer::ImageAsset> mosaic = xtransMosaicAsset(session, width, height);
    if (!mosaic.has_value())
    {
        return false;
    }
    an::Result<an::OutputIntent> intent =
        an::OutputIntent::create(an::OutputIntentRequest{.sampleType = an::SampleType::UInt16});
    EXPECT_TRUE(intent.hasValue()) << intent.status().message();
    if (!intent.hasValue())
    {
        return false;
    }
    const an::Status written = an::ui::writeViewImage(session, *mosaic, path, *intent, {});
    EXPECT_TRUE(written.ok()) << written.message();
    return written.ok();
}

/// Whether some entry of an asset's lineage names `needle` in its detail.
[[nodiscard]] inline bool lineageMentions(const an::consumer::ImageAsset& asset, std::string_view needle)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!lease.hasValue())
    {
        return false;
    }
    bool found = false;
    lease->visit([&](const auto& typed) {
        for (const auto& entry : typed.metadata().provenance().entries())
        {
            found = found || std::string(entry.detail()).find(needle) != std::string::npos;
        }
    });
    return found;
}

} // namespace an::tests::viewer
