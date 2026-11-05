// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "EngineFixtureAdapter.h"

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/TiffCodec.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/DescriptorError.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleBuffer.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace an::tests::fixtures::engine
{

namespace
{

[[nodiscard]] an::Status refused(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

// A path as the codec facade takes it: UTF-8.
[[nodiscard]] std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

// A card's value as the FITS writer keeps it: a string quoted, with its quotes doubled, so it stays a string;
// anything else as written, which the writer keeps lexically.
[[nodiscard]] std::string cardValue(const FixtureCard& card)
{
    if (card.kind != FixtureCardKind::Text)
    {
        return card.value;
    }
    std::string quoted = "'";
    for (const char character : card.value)
    {
        quoted += character;
        if (character == '\'')
        {
            quoted += '\'';
        }
    }
    return quoted + "'";
}

[[nodiscard]] an::RawSampleFormat rawFormatOf(FixtureSampleType sampleType) noexcept
{
    const auto size = static_cast<std::uint32_t>(sampleSizeOf(sampleType));
    switch (sampleType)
    {
    case FixtureSampleType::UInt8:
    case FixtureSampleType::UInt16:
    case FixtureSampleType::UInt32:
        return an::RawSampleFormat{size, an::RawSampleClass::UnsignedInteger};
    case FixtureSampleType::Int8:
    case FixtureSampleType::Int16:
    case FixtureSampleType::Int32:
        return an::RawSampleFormat{size, an::RawSampleClass::SignedInteger};
    case FixtureSampleType::Float32:
    case FixtureSampleType::Float64:
        return an::RawSampleFormat{size, an::RawSampleClass::FloatingPoint};
    }
    return an::RawSampleFormat{size, an::RawSampleClass::UnsignedInteger};
}

[[nodiscard]] an::Result<an::CfaDescriptor> cfaOf(FixtureCfa cfa)
{
    if (cfa == FixtureCfa::None)
    {
        return an::CfaDescriptor{};
    }
    const std::optional<an::CfaDescriptor> descriptor = an::CfaDescriptor::create(engineCfaPattern(cfa), 0u, 0u);
    if (!descriptor.has_value())
    {
        return refused(an::ErrorCode::InvalidArgument, "the filter array has no descriptor at phase zero");
    }
    return *descriptor;
}

template <typename SampleT>
[[nodiscard]] an::Result<EngineImage> makeTyped(const an::EngineContext& engine, const an::ImageDescriptor& descriptor,
                                                const FixtureSamples& samples)
{
    an::Result<an::Image<SampleT>> created = an::Image<SampleT>::create(
        engine, descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    if (!created.hasValue())
    {
        return created.status();
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = created->write();
        if (!access.has_value())
        {
            return refused(an::ErrorCode::InvalidState, "the new image gave no write access");
        }
        const std::size_t rowBytes = static_cast<std::size_t>(samples.width()) * sizeof(SampleT);
        for (std::uint32_t channel = 0u; channel < samples.channelCount(); ++channel)
        {
            const std::span<const std::byte> plane = samples.plane(channel);
            for (std::uint32_t y = 0u; y < samples.height(); ++y)
            {
                const std::optional<std::span<SampleT>> row = access->view().row(channel, y);
                if (!row.has_value() || row->size_bytes() != rowBytes)
                {
                    return refused(an::ErrorCode::InvalidState, "the new image's rows are not the fixture's");
                }
                std::memcpy(row->data(), plane.data() + y * rowBytes, rowBytes);
            }
        }
    }
    return EngineImage{std::move(*created)};
}

} // namespace

std::string loadedEngineVersion()
{
    return an::engineVersionString();
}

ExpectationProvenance engineProvenance(const std::string& commit)
{
    return provenanceOfThisBuild("AfterNightEngine", loadedEngineVersion(), std::to_string(an::engineAbiGeneration()),
                                 commit);
}

bool producedElsewhere(const ExpectationProvenance& recorded)
{
    const ExpectationProvenance own = engineProvenance();
    return recorded.producer != own.producer || recorded.producerVersion != own.producerVersion;
}

an::Result<an::consumer::EngineSession> createFixtureSession(std::uint32_t maxConcurrency)
{
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = maxConcurrency;
    settings.codecRegistry = []() -> an::Result<std::shared_ptr<const an::CodecRegistry>> {
        // FITS for the fixture files, and TIFF for what an external program is handed and answers.
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
        const std::array<std::shared_ptr<const an::ImageCodec>, 2u> codecs{*fits, *tiff};
        return an::CodecRegistry::create(codecs);
    };
    return an::consumer::EngineSession::open(settings);
}

an::Result<an::consumer::OperationScope> admitFixtureOperation(const an::consumer::EngineSession& session)
{
    return session.tryAdmit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
}

an::SampleType engineSampleType(FixtureSampleType sampleType) noexcept
{
    switch (sampleType)
    {
    case FixtureSampleType::UInt8:
        return an::SampleType::UInt8;
    case FixtureSampleType::Int8:
        return an::SampleType::Int8;
    case FixtureSampleType::UInt16:
        return an::SampleType::UInt16;
    case FixtureSampleType::Int16:
        return an::SampleType::Int16;
    case FixtureSampleType::UInt32:
        return an::SampleType::UInt32;
    case FixtureSampleType::Int32:
        return an::SampleType::Int32;
    case FixtureSampleType::Float32:
        return an::SampleType::Float32;
    case FixtureSampleType::Float64:
        return an::SampleType::Float64;
    }
    return an::SampleType::UInt8;
}

std::optional<FixtureSampleType> fixtureSampleTypeOf(an::SampleType sampleType) noexcept
{
    for (const FixtureSampleType candidate :
         {FixtureSampleType::UInt8, FixtureSampleType::Int8, FixtureSampleType::UInt16, FixtureSampleType::Int16,
          FixtureSampleType::UInt32, FixtureSampleType::Int32, FixtureSampleType::Float32, FixtureSampleType::Float64})
    {
        if (engineSampleType(candidate) == sampleType)
        {
            return candidate;
        }
    }
    return std::nullopt;
}

an::NumericDomain engineNumericDomain(FixtureDomain domain) noexcept
{
    switch (domain)
    {
    case FixtureDomain::NormalizedLinear:
        return an::NumericDomain::NormalizedLinear;
    case FixtureDomain::SensorCode:
        return an::NumericDomain::RawSensorCode;
    case FixtureDomain::PhysicalScaled:
        return an::NumericDomain::PhysicalScaled;
    }
    return an::NumericDomain::NormalizedLinear;
}

std::optional<FixtureDomain> fixtureDomainOf(an::NumericDomain domain) noexcept
{
    switch (domain)
    {
    case an::NumericDomain::NormalizedLinear:
        return FixtureDomain::NormalizedLinear;
    case an::NumericDomain::RawSensorCode:
        return FixtureDomain::SensorCode;
    case an::NumericDomain::PhysicalScaled:
        return FixtureDomain::PhysicalScaled;
    case an::NumericDomain::EncodedNonlinear:
        break;
    }
    return std::nullopt;
}

an::CfaPattern engineCfaPattern(FixtureCfa cfa) noexcept
{
    switch (cfa)
    {
    case FixtureCfa::None:
        return an::CfaPattern::None;
    case FixtureCfa::Rggb:
        return an::CfaPattern::Rggb;
    case FixtureCfa::Bggr:
        return an::CfaPattern::Bggr;
    case FixtureCfa::Grbg:
        return an::CfaPattern::Grbg;
    case FixtureCfa::Gbrg:
        return an::CfaPattern::Gbrg;
    }
    return an::CfaPattern::None;
}

std::optional<FixtureCfa> fixtureCfaOf(an::CfaPattern pattern) noexcept
{
    for (const FixtureCfa cfa :
         {FixtureCfa::None, FixtureCfa::Rggb, FixtureCfa::Bggr, FixtureCfa::Grbg, FixtureCfa::Gbrg})
    {
        if (engineCfaPattern(cfa) == pattern)
        {
            return cfa;
        }
    }
    return std::nullopt;
}

an::ChannelSchema engineChannelSchema(FixtureChannels channels) noexcept
{
    return channels == FixtureChannels::Rgb ? an::ChannelSchema::Rgb : an::ChannelSchema::Mono;
}

an::Result<an::ImageDescriptor> describeEngineImage(const an::EngineContext& engine, const FixtureImage& image)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(image.width, image.height, channelCountOf(image.channels), engine.limits());
    if (!extent.has_value())
    {
        return refused(an::ErrorCode::InvalidArgument, image.name + ": the extent is outside the engine's limits");
    }
    an::Result<an::CfaDescriptor> cfa = cfaOf(image.cfa);
    if (!cfa.hasValue())
    {
        return cfa.status();
    }
    const an::ImageDescriptorRequest request{.extent = *extent,
                                             .sampleType = engineSampleType(image.sampleType),
                                             .channelSchema = engineChannelSchema(image.channels),
                                             .colorSpace = an::ColorSpace::Unspecified,
                                             .numericDomain = engineNumericDomain(image.domain),
                                             .cfa = *cfa,
                                             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                             .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    if (!descriptor.has_value())
    {
        return refused(an::ErrorCode::InvalidArgument,
                       image.name + ": the engine refuses the description: " +
                           an::descriptorErrorName(an::ImageDescriptor::check(request)));
    }
    return *descriptor;
}

an::Result<EngineImage> makeEngineImage(const an::EngineContext& engine, const FixtureImage& image,
                                        const FixtureSamples& samples)
{
    if (samples.width() != image.width || samples.height() != image.height ||
        samples.channelCount() != channelCountOf(image.channels) || samples.sampleType() != image.sampleType ||
        samples.empty())
    {
        return refused(an::ErrorCode::IncompatibleArguments,
                       image.name + ": the samples are not the description's shape and representation");
    }
    const an::Result<an::ImageDescriptor> descriptor = describeEngineImage(engine, image);
    if (!descriptor.hasValue())
    {
        return descriptor.status();
    }
    switch (image.sampleType)
    {
    case FixtureSampleType::UInt8:
        return makeTyped<std::uint8_t>(engine, *descriptor, samples);
    case FixtureSampleType::Int8:
        return makeTyped<std::int8_t>(engine, *descriptor, samples);
    case FixtureSampleType::UInt16:
        return makeTyped<std::uint16_t>(engine, *descriptor, samples);
    case FixtureSampleType::Int16:
        return makeTyped<std::int16_t>(engine, *descriptor, samples);
    case FixtureSampleType::UInt32:
        return makeTyped<std::uint32_t>(engine, *descriptor, samples);
    case FixtureSampleType::Int32:
        return makeTyped<std::int32_t>(engine, *descriptor, samples);
    case FixtureSampleType::Float32:
        return makeTyped<float>(engine, *descriptor, samples);
    case FixtureSampleType::Float64:
        return makeTyped<double>(engine, *descriptor, samples);
    }
    return refused(an::ErrorCode::InvalidArgument, image.name + ": the representation is not one of the eight");
}

an::Result<an::consumer::ImageAsset> makeImageAsset(const an::consumer::EngineSession& session,
                                                    const FixtureImage& image, const FixtureSamples& samples)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }
    an::Result<EngineImage> made = makeEngineImage(hold->engine(), image, samples);
    if (!made.hasValue())
    {
        return made.status();
    }
    return an::consumer::ImageAsset::adopt(session, std::move(*made));
}

std::optional<FixtureSamples> readAssetSamples(const an::consumer::ImageAsset& asset, std::string& error)
{
    const an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!lease.hasValue())
    {
        error = lease.status().message();
        return std::nullopt;
    }
    return readImageSamples(lease->image(), error);
}

std::optional<FixtureSamples> readImageSamples(const EngineImage& image, std::string& error)
{
    return std::visit(
        [&error](const auto& typed) -> std::optional<FixtureSamples> {
            using SampleT = typename std::decay_t<decltype(typed)>::Sample;
            const std::optional<an::ImageDescriptor> descriptor = typed.descriptor();
            const auto view = typed.view();
            if (!descriptor.has_value() || !view.has_value())
            {
                error = "the image holds nothing";
                return std::nullopt;
            }
            const an::ChannelSchema schema = descriptor->channelSchema();
            const std::uint32_t channels = descriptor->extent().channelCount();
            if (!(schema == an::ChannelSchema::Mono && channels == 1u) &&
                !(schema == an::ChannelSchema::Rgb && channels == 3u))
            {
                error = std::string("the image's ") + an::channelSchemaName(schema) +
                        " channels are not ones the "
                        "fixtures describe";
                return std::nullopt;
            }
            const std::optional<FixtureSampleType> sampleType = fixtureSampleTypeOf(descriptor->sampleType());
            if (!sampleType.has_value())
            {
                error = "the image's representation is not one of the eight";
                return std::nullopt;
            }
            FixtureSamples samples(descriptor->extent().width(), descriptor->extent().height(), channels, *sampleType);
            const std::uint32_t planes = view->planeCount();
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                for (std::uint32_t y = 0u; y < samples.height(); ++y)
                {
                    // Planar: one plane per channel. Interleaved: the channels of a pixel side by side in one plane.
                    const std::optional<std::span<const SampleT>> row = view->row(planes == 1u ? 0u : channel, y);
                    if (!row.has_value())
                    {
                        error = "the image has no row " + std::to_string(y);
                        return std::nullopt;
                    }
                    const std::uint32_t stride = planes == 1u ? channels : 1u;
                    const std::uint32_t offset = planes == 1u ? channel : 0u;
                    for (std::uint32_t x = 0u; x < samples.width(); ++x)
                    {
                        samples.setValue(x, y, channel, static_cast<double>((*row)[x * stride + offset]));
                    }
                }
            }
            return samples;
        },
        image);
}

an::Result<DecodedFile> decodeEngineFile(const an::consumer::EngineSession& session, const std::filesystem::path& path)
{
    const an::Result<an::consumer::OperationScope> scope = admitFixtureOperation(session);
    if (!scope.hasValue())
    {
        return scope.status();
    }
    const std::string spelled = utf8(path);
    an::Result<an::CodecDecodeResult> decoded =
        an::decodeImage(scope->engine(), scope->execution(), an::CodecReadRequest{.path = spelled});
    if (!decoded.hasValue())
    {
        return decoded.status();
    }
    const an::RawFrame& frame = decoded->frame;
    const std::optional<an::RawFrameDescriptor> descriptor = frame.descriptor();
    const std::optional<an::ImageLayout> layout = frame.layout();
    if (!descriptor.has_value() || !layout.has_value())
    {
        return refused(an::ErrorCode::InvalidData, spelled + ": the decoded frame holds nothing");
    }
    const std::optional<an::SampleType> resident = descriptor->residentSampleType();
    const std::optional<FixtureSampleType> sampleType =
        resident.has_value() ? fixtureSampleTypeOf(*resident) : std::nullopt;
    if (!sampleType.has_value())
    {
        return refused(an::ErrorCode::Unsupported, spelled + ": the frame's representation is not one of the eight");
    }
    if (descriptor->byteOrder() != an::ByteOrder::Native)
    {
        return refused(an::ErrorCode::Unsupported, spelled + ": the frame is not in the host's byte order");
    }
    const an::ChannelSchema schema = descriptor->channelSchema();
    const std::uint32_t channels = descriptor->extent().channelCount();
    if (!(schema == an::ChannelSchema::Mono && channels == 1u) && !(schema == an::ChannelSchema::Rgb && channels == 3u))
    {
        return refused(an::ErrorCode::Unsupported, spelled + ": the frame's " + an::channelSchemaName(schema) +
                                                       " channels are not ones the fixtures describe");
    }
    const an::CfaDescriptor& cfa = descriptor->cfa();
    const std::optional<FixtureCfa> fixtureCfa = fixtureCfaOf(cfa.pattern());
    if (!fixtureCfa.has_value() || (cfa.isMosaiced() && (cfa.originX() != 0u || cfa.originY() != 0u)))
    {
        return refused(an::ErrorCode::Unsupported, spelled + ": the frame's filter array is not one the fixtures "
                                                             "describe at phase zero");
    }
    const std::optional<FixtureDomain> domain = fixtureDomainOf(descriptor->numericDomain());
    if (!domain.has_value())
    {
        return refused(an::ErrorCode::Unsupported, spelled + ": the frame's numeric meaning is not one the fixtures "
                                                             "describe");
    }

    const std::uint32_t width = descriptor->extent().width();
    const std::uint32_t height = descriptor->extent().height();
    const std::size_t size = sampleSizeOf(*sampleType);
    const std::uint32_t planes = layout->planeCount();
    const bool bottomUp = descriptor->coordinateOrigin() == an::CoordinateOrigin::BottomLeft;
    DecodedFile result{FixtureSamples(width, height, channels, *sampleType), *fixtureCfa, *domain};
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const std::span<std::byte> plane = result.samples.mutablePlane(channel);
        for (std::uint32_t row = 0u; row < height; ++row)
        {
            const std::optional<std::span<const std::byte>> bytes = frame.row(planes == 1u ? 0u : channel, row);
            if (!bytes.has_value())
            {
                return refused(an::ErrorCode::InvalidData, spelled + ": the frame has no row " + std::to_string(row));
            }
            const std::uint32_t y = bottomUp ? height - 1u - row : row;
            std::byte* target = plane.data() + static_cast<std::size_t>(y) * width * size;
            if (planes == 1u && channels > 1u)
            {
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    std::memcpy(target + x * size,
                                bytes->data() + (static_cast<std::size_t>(x) * channels + channel) * size, size);
                }
            }
            else
            {
                std::memcpy(target, bytes->data(), static_cast<std::size_t>(width) * size);
            }
        }
    }
    return result;
}

an::Status writeEngineFits(const an::consumer::EngineSession& session, const FixtureImage& image,
                           const FixtureSamples& samples, const std::filesystem::path& path)
{
    const an::Result<an::consumer::OperationScope> scope = admitFixtureOperation(session);
    if (!scope.hasValue())
    {
        return scope.status();
    }
    const an::EngineContext& engine = scope->engine();
    const std::uint32_t channels = channelCountOf(image.channels);
    if (samples.width() != image.width || samples.height() != image.height || samples.channelCount() != channels ||
        samples.sampleType() != image.sampleType || samples.empty())
    {
        return refused(an::ErrorCode::IncompatibleArguments,
                       image.name + ": the samples are not the description's shape and representation");
    }
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(image.width, image.height, channels, engine.limits());
    const an::Result<an::CfaDescriptor> cfa = cfaOf(image.cfa);
    if (!extent.has_value() || !cfa.hasValue())
    {
        return refused(an::ErrorCode::InvalidArgument, image.name + ": the engine cannot describe the frame");
    }
    const std::optional<an::RawFrameDescriptor> descriptor = an::RawFrameDescriptor::create(
        an::RawFrameDescriptorRequest{.extent = *extent,
                                      .sampleFormat = rawFormatOf(image.sampleType),
                                      .byteOrder = an::ByteOrder::Native,
                                      .channelSchema = engineChannelSchema(image.channels),
                                      .colorSpace = an::ColorSpace::Unspecified,
                                      .numericDomain = engineNumericDomain(image.domain),
                                      .cfa = *cfa,
                                      .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                      .pixelAspectRatio = 1.0});
    const auto size = static_cast<std::uint32_t>(sampleSizeOf(image.sampleType));
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = size,
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = channels,
                                                       .rowAlignmentBytes = an::kDefaultBufferAlignmentBytes},
                                engine.limits());
    if (!descriptor.has_value() || !layout.has_value())
    {
        return refused(an::ErrorCode::InvalidArgument, image.name + ": the engine cannot describe the frame");
    }
    an::Result<an::SampleBuffer> buffer =
        an::SampleBuffer::createOwned(engine, *layout, an::StorageInitialization::Zeroed);
    if (!buffer.hasValue())
    {
        return buffer.status();
    }
    an::Result<an::RawFrame> frame = an::RawFrame::create(*descriptor, *layout, std::move(*buffer));
    if (!frame.hasValue())
    {
        return frame.status();
    }
    const std::size_t rowBytes = static_cast<std::size_t>(image.width) * size;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const std::span<const std::byte> plane = samples.plane(channel);
        for (std::uint32_t y = 0u; y < image.height; ++y)
        {
            const std::optional<std::span<std::byte>> row = frame->mutableRow(channel, y);
            if (!row.has_value())
            {
                return refused(an::ErrorCode::InvalidState, image.name + ": the frame has no writable row");
            }
            std::memcpy(row->data(), plane.data() + y * rowBytes, rowBytes);
        }
    }

    std::vector<an::CodecCard> cards;
    for (const FixtureCard& card : image.cards)
    {
        an::Result<an::CodecCard> made =
            an::CodecCard::create(an::CodecNamespace::Fits, card.keyword, cardValue(card), card.comment);
        if (!made.hasValue())
        {
            return made.status();
        }
        cards.push_back(std::move(*made));
    }
    const an::Result<an::OutputIntent> intent = an::OutputIntent::create(
        an::OutputIntentRequest{.sampleType = engineSampleType(image.sampleType), .cards = std::move(cards)});
    if (!intent.hasValue())
    {
        return intent.status();
    }
    const std::string spelled = utf8(path);
    const an::Result<an::OutputReport> written =
        an::writeImage(engine, scope->execution(),
                       an::OutputRequest{.destinationPath = spelled,
                                         .format = an::CodecFormat::Fits,
                                         .intent = &*intent,
                                         .durability = an::OutputDurability::CloseOnly},
                       *frame, an::ImageMetadata{});
    return written.hasValue() ? an::Status::success() : written.status();
}

} // namespace an::tests::fixtures::engine
