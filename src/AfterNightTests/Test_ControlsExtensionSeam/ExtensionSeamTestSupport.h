// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the extension seam cases share: assets described the way the seam has to read them - a representation, a
/// channel schema, a colour space, a transfer function, a mosaic and its phase, cards and a solution - a typed view of
/// a shared buffer's bytes, the JSON a sidecar holds, and whether a mapping still holds a file.
#pragma once

#include "Test_ControlsViewer/ViewerTestContext.h"

#include "app/SharedBufferManager.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace an::tests::seam
{

using an::tests::viewer::channelCountOf;
using an::tests::viewer::Channels;

/// What a seam case states about the image it exports, beyond its samples.
struct SeamImageSpec
{
    std::uint32_t width = 4u;
    std::uint32_t height = 3u;
    Channels channels = Channels::Rgb;
    an::ColorSpace colorSpace = an::ColorSpace::Unspecified;
    an::NumericDomain domain = an::NumericDomain::NormalizedLinear;
    an::TransferFunction transfer = an::TransferFunction::Unspecified;
    an::CfaDescriptor cfa{};
    /// FITS cards, keyword and value, in order.
    std::vector<std::pair<std::string, std::string>> fitsCards;
    /// EXIF cards, keyword and value, recorded as a TIFF reader records them.
    std::vector<std::pair<std::string, std::string>> exifCards;
    /// Cards stated whole - a comment, an EXIF type, a field continued over several cards - after the others.
    std::vector<an::CodecCard> statedCards;
};

/// A card of a namespace; fails the case when the engine refuses it.
[[nodiscard]] inline std::optional<an::CodecCard> card(an::CodecNamespace origin, const std::string& keyword,
                                                       const std::string& value)
{
    an::Result<an::CodecCard> made = an::CodecCard::create(origin, keyword, value, "");
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    if (!made.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*made);
}

/// The metadata a spec states: its cards, the acquisition record projected from them, its semantics, and a lineage.
[[nodiscard]] inline std::optional<an::ImageMetadata> metadataOf(const SeamImageSpec& spec)
{
    std::vector<an::CodecCard> cards;
    for (const auto& [keyword, value] : spec.fitsCards)
    {
        std::optional<an::CodecCard> made = card(an::CodecNamespace::Fits, keyword, value);
        if (!made.has_value())
        {
            return std::nullopt;
        }
        cards.push_back(std::move(*made));
    }
    for (const auto& [keyword, value] : spec.exifCards)
    {
        std::optional<an::CodecCard> made = card(an::CodecNamespace::Tiff, keyword, value);
        if (!made.has_value())
        {
            return std::nullopt;
        }
        cards.push_back(std::move(*made));
    }
    cards.insert(cards.end(), spec.statedCards.begin(), spec.statedCards.end());
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    EXPECT_TRUE(codec.hasValue()) << codec.status().message();
    an::Result<an::ImageSemantics> semantics =
        an::ImageSemantics::create(an::ImageSemanticsRequest{.transferFunction = spec.transfer});
    EXPECT_TRUE(semantics.hasValue()) << semantics.status().message();
    an::Result<an::Provenance> provenance = an::Provenance::create(an::ProvenanceSourceKind::Generated, "seam fixture");
    EXPECT_TRUE(provenance.hasValue()) << provenance.status().message();
    if (!codec.hasValue() || !semantics.hasValue() || !provenance.hasValue())
    {
        return std::nullopt;
    }
    return an::ImageMetadata::create(an::AcquisitionMetadata{}, std::move(*semantics), an::AstrometricSolution{},
                                     std::move(*provenance), std::move(*codec), std::nullopt);
}

/// An asset of a session as a spec describes it, each sample `value(x, y, channel)`; fails the case when it cannot be
/// made.
template <typename SampleT>
[[nodiscard]] std::optional<an::consumer::ImageAsset>
makeSeamAsset(const an::consumer::EngineSession& session, const SeamImageSpec& spec,
              const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)>& value)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue()) << hold.status().message();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(spec.width, spec.height, channelCountOf(spec.channels), hold->engine().limits());
    EXPECT_TRUE(extent.has_value());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    const an::ChannelSchema schema =
        spec.channels == Channels::Grey
            ? an::ChannelSchema::Mono
            : (spec.channels == Channels::Rgb ? an::ChannelSchema::Rgb : an::ChannelSchema::Rgba);
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::kSampleTypeOf<SampleT>,
                                                               .channelSchema = schema,
                                                               .colorSpace = spec.colorSpace,
                                                               .numericDomain = spec.domain,
                                                               .cfa = spec.cfa,
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value()) << "the spec describes no image";
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    std::optional<an::ImageMetadata> metadata = metadataOf(spec);
    if (!metadata.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<SampleT>> image =
        an::Image<SampleT>::create(hold->engine(), *descriptor, an::StorageArrangement::Planar,
                                   an::StorageInitialization::Zeroed, std::move(*metadata));
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
        for (std::uint32_t channel = 0u; channel < channelCountOf(spec.channels); ++channel)
        {
            for (std::uint32_t y = 0u; y < spec.height; ++y)
            {
                const auto row = access->view().row(channel, y);
                EXPECT_TRUE(row.has_value());
                if (!row.has_value())
                {
                    return std::nullopt;
                }
                for (std::uint32_t x = 0u; x < spec.width; ++x)
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

/// A sample with a distinct value per position and channel, below 250 so it fits every representation the fixtures use.
[[nodiscard]] inline std::uint8_t seamSample(std::uint32_t x, std::uint32_t y, std::uint32_t channel) noexcept
{
    return static_cast<std::uint8_t>((x * 7u + y * 31u + channel * 101u) % 250u);
}

/// The sample a buffer holds at a position of a plane, read through its planar strides.
template <typename SampleT>
[[nodiscard]] SampleT bufferSample(const an::ui::extensions::SharedImageBuffer& buffer, std::uint32_t x,
                                   std::uint32_t y, std::uint32_t plane)
{
    const an::ui::extensions::SharedBufferDescriptor& descriptor = buffer.descriptor();
    const std::byte* bytes = buffer.bytes() + plane * descriptor.plane_stride + y * descriptor.row_stride +
                             static_cast<std::uint64_t>(x) * sizeof(SampleT);
    SampleT sample{};
    std::memcpy(&sample, bytes, sizeof(SampleT));
    return sample;
}

/// Writes a sample at a position of a plane of a buffer, through its planar strides.
template <typename SampleT>
void writeBufferSample(an::ui::extensions::SharedImageBuffer& buffer, std::uint32_t x, std::uint32_t y,
                       std::uint32_t plane, SampleT sample)
{
    const an::ui::extensions::SharedBufferDescriptor& descriptor = buffer.descriptor();
    std::byte* bytes = buffer.bytes() + plane * descriptor.plane_stride + y * descriptor.row_stride +
                       static_cast<std::uint64_t>(x) * sizeof(SampleT);
    std::memcpy(bytes, &sample, sizeof(SampleT));
}

/// Fills every sample of a buffer from a function of its position and plane.
template <typename SampleT>
void fillBuffer(an::ui::extensions::SharedImageBuffer& buffer,
                const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)>& value)
{
    const an::ui::extensions::SharedBufferDescriptor& descriptor = buffer.descriptor();
    for (int plane = 0; plane < descriptor.channels; ++plane)
    {
        for (int y = 0; y < descriptor.height; ++y)
        {
            for (int x = 0; x < descriptor.width; ++x)
            {
                writeBufferSample<SampleT>(buffer, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                           static_cast<std::uint32_t>(plane),
                                           value(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                                 static_cast<std::uint32_t>(plane)));
            }
        }
    }
}

/// The JSON object a sidecar holds; empty when the file is missing or not an object.
[[nodiscard]] inline QJsonObject readSidecar(const std::string& path)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly))
    {
        return QJsonObject();
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

/// The description of the image an asset holds, read without a lease; fails the case when it holds none.
[[nodiscard]] inline std::optional<an::ImageDescriptor> describedBy(const an::consumer::ImageAsset& asset)
{
    const std::optional<an::ImageDescriptor> descriptor = asset.descriptor();
    EXPECT_TRUE(descriptor.has_value());
    return descriptor;
}

/// The metadata of the image an asset holds, copied under a read lease.
[[nodiscard]] inline std::optional<an::ImageMetadata> metadataOf(const an::consumer::ImageAsset& asset)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    return lease->visit([](const auto& typed) { return typed.metadata(); });
}

/// A FITS card's value in a metadata's codec cards, or empty.
[[nodiscard]] inline std::string fitsCardValue(const an::ImageMetadata& metadata, const std::string& keyword)
{
    const an::CodecCard* found = metadata.codec().find(an::CodecNamespace::Fits, keyword);
    return found != nullptr ? found->value() : std::string();
}

/// A directory of the case's own for a manager's root, removed with the case.
class SeamRoot
{
public:
    SeamRoot()
    {
        EXPECT_TRUE(m_directory.isValid());
    }

    [[nodiscard]] std::filesystem::path path() const
    {
        return std::filesystem::path(m_directory.path().toStdString()) / "SharedBuffers";
    }

private:
    QTemporaryDir m_directory;
};

/// Whether a file can be removed now, which tells a released mapping from a held one. Windows refuses to remove a
/// mapped file; elsewhere a mapped file can be removed, so this process's own list of mappings is asked first.
[[nodiscard]] inline bool removable(const std::filesystem::path& path)
{
#ifndef _WIN32
    std::error_code resolveError;
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(path, resolveError);
    std::ifstream mappings("/proc/self/maps");
    for (std::string line; std::getline(mappings, line);)
    {
        const std::size_t name = line.find('/');
        if (name != std::string::npos && std::filesystem::path(line.substr(name)) == resolved)
            return false;
    }
#endif
    std::error_code ec;
    return std::filesystem::remove(path, ec) && !ec;
}

/// The world-coordinate cards of a solved frame: a tangent-plane solution at one arcsecond per pixel, with its solver.
[[nodiscard]] inline std::vector<std::pair<std::string, std::string>> solvedCards(std::uint32_t width,
                                                                                  std::uint32_t height)
{
    return {{"NAXIS", "2"},
            {"NAXIS1", std::to_string(width)},
            {"NAXIS2", std::to_string(height)},
            {"CTYPE1", "'RA---TAN'"},
            {"CTYPE2", "'DEC--TAN'"},
            {"CUNIT1", "'deg'"},
            {"CUNIT2", "'deg'"},
            {"CRPIX1", "2.0"},
            {"CRPIX2", "2.0"},
            {"CRVAL1", "210.123456"},
            {"CRVAL2", "-12.25"},
            {"CD1_1", "-0.0002777777778"},
            {"CD1_2", "0.0"},
            {"CD2_1", "0.0"},
            {"CD2_2", "0.0002777777778"},
            {"RADESYS", "'ICRS'"},
            {"PLTSOLVR", "'ASTAP'"}};
}

} // namespace an::tests::seam
