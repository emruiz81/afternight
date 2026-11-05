// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/DumpCodec.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/JpegCodec.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/IO/PngCodec.h>
#include <AfterNightEngine/IO/TiffCodec.h>
#include <AfterNightEngine/IO/XisfCodec.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
bool supported(an::CodecFormat format, an::SampleType sample, an::OutputCompression compression)
{
    using enum an::OutputCompression;
    using enum an::SampleType;
    switch (format)
    {
    case an::CodecFormat::Fits:
        return compression == None || compression == Gzip ||
               ((sample != Float32 && sample != Float64) &&
                (compression == Rice || (compression == Plio && sample != UInt32)));
    case an::CodecFormat::Xisf:
        return (sample == UInt8 || sample == UInt16 || sample == UInt32 || sample == Float32 || sample == Float64) &&
               (compression == None || compression == Deflate || compression == Lz4 || compression == Lz4Hc ||
                compression == Zstd);
    case an::CodecFormat::Png:
        return (sample == UInt8 || sample == UInt16) && (compression == None || compression == Deflate);
    case an::CodecFormat::Jpeg:
        return sample == UInt8 && compression == Jpeg;
    case an::CodecFormat::Tiff:
        return compression == None || compression == Lzw || compression == Deflate ||
               (sample == UInt8 && compression == Jpeg);
    case an::CodecFormat::Dump:
        return compression == None;
    default:
        return false;
    }
}

TEST(CodecCompressionTest, EveryCompressionAndResidentRepresentationHasAnExplicitOutcome)
{
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("an-compression-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{directory};
    const auto destination = directory / "frame";
    const auto utf8 = destination.u8string();
    const std::string path(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    const std::array createdAdapters{an::createFitsCodec(), an::createXisfCodec(), an::createPngCodec(),
                                     an::createJpegCodec(), an::createTiffCodec(), an::createDumpCodec()};
    const std::array formats{an::CodecFormat::Fits, an::CodecFormat::Xisf, an::CodecFormat::Png,
                             an::CodecFormat::Jpeg, an::CodecFormat::Tiff, an::CodecFormat::Dump};
    for (std::size_t index = 0u; index < formats.size(); ++index)
    {
        const auto& adapter = createdAdapters[index];
        ASSERT_TRUE(adapter.hasValue());
        const std::array adapters{*adapter};
        auto registry = an::CodecRegistry::create(adapters);
        ASSERT_TRUE(registry.hasValue());
        an::EngineContextOptions options;
        options.codecRegistry = *registry;
        auto engine = an::EngineContext::create(options);
        ASSERT_TRUE(engine.hasValue());
        for (unsigned sampleCode = 0u; an::isSampleType(static_cast<an::SampleType>(sampleCode)); ++sampleCode)
        {
            const auto sample = static_cast<an::SampleType>(sampleCode);
            const auto rawFormat = an::rawSampleFormatOf(sample);
            const auto extent = *an::ImageExtent::create(16u, 16u, 1u, engine->limits());
            const auto descriptor = *an::RawFrameDescriptor::create({extent,
                                                                     rawFormat,
                                                                     an::ByteOrder::Native,
                                                                     an::ChannelSchema::Mono,
                                                                     an::ColorSpace::Unspecified,
                                                                     an::NumericDomain::NormalizedLinear,
                                                                     {},
                                                                     an::CoordinateOrigin::TopLeft,
                                                                     1.0});
            const auto layout = *an::ImageLayout::create({extent, rawFormat.sizeBytes, 1u, 1u, 1u}, engine->limits());
            auto buffer = an::SampleBuffer::createOwned(*engine, layout, an::StorageInitialization::Zeroed);
            ASSERT_TRUE(buffer.hasValue());
            auto frame = an::RawFrame::create(descriptor, layout, std::move(*buffer));
            ASSERT_TRUE(frame.hasValue());
            for (unsigned code = 0u; an::isOutputCompression(static_cast<an::OutputCompression>(code)); ++code)
            {
                const auto compression = static_cast<an::OutputCompression>(code);
                SCOPED_TRACE(testing::Message() << "codec=" << unsigned(formats[index]) << " sample=" << sampleCode
                                                << " compression=" << code);
                {
                    std::ofstream old(destination, std::ios::binary);
                    old << "existing";
                }
                auto execution = an::ExecutionContext::create(*engine, {});
                ASSERT_TRUE(execution.hasValue());
                auto intent = an::OutputIntent::create({.sampleType = sample, .compression = compression});
                ASSERT_TRUE(intent.hasValue());
                auto result = an::writeImage(*engine, *execution,
                                             {.destinationPath = path,
                                              .format = formats[index],
                                              .intent = &*intent,
                                              .durability = an::OutputDurability::CloseOnly},
                                             *frame, {});
                ASSERT_EQ(result.hasValue(), supported(formats[index], sample, compression))
                    << result.status().message();
                EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory),
                                        std::filesystem::directory_iterator{}),
                          1);
                if (!result.hasValue())
                {
                    EXPECT_EQ(result.code(), an::ErrorCode::Unsupported);
                    std::ifstream previous(destination, std::ios::binary);
                    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(previous), {}), "existing");
                    continue;
                }
                ASSERT_TRUE(result->intent.has_value());
                EXPECT_EQ(result->intent->compression(), compression);
                auto decoded = an::decodeImage(*engine, *execution, {.path = path, .format = formats[index]});
                ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
                EXPECT_EQ(decoded->frame.descriptor()->sampleFormat(), rawFormat);
                EXPECT_EQ(decoded->frame.bytes().size(), frame->bytes().size());
                EXPECT_TRUE(std::all_of(decoded->frame.bytes().begin(), decoded->frame.bytes().end(),
                                        [](std::byte value) { return value == std::byte{0}; }));
            }
        }
    }
}

TEST(CodecCompressionTest, AFormOfThePayloadIsWrittenOnlyByTheContainerThatHasIt)
{
    // Byte shuffling, a compression block size and a checksum are XISF's: every other writing adapter refuses an
    // intent that states one, before anything is written and beside a compression it writes, rather than writing the
    // file without what was asked.
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("an-payload-form-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{directory};
    struct Container
    {
        an::Result<std::shared_ptr<const an::ImageCodec>> adapter;
        an::CodecFormat format;
        an::SampleType sample;
        an::OutputCompression compression;
    };
    const std::array containers{
        Container{an::createFitsCodec(), an::CodecFormat::Fits, an::SampleType::UInt16, an::OutputCompression::Gzip},
        Container{an::createTiffCodec(), an::CodecFormat::Tiff, an::SampleType::UInt16, an::OutputCompression::Deflate},
        Container{an::createPngCodec(), an::CodecFormat::Png, an::SampleType::UInt16, an::OutputCompression::Deflate},
        Container{an::createJpegCodec(), an::CodecFormat::Jpeg, an::SampleType::UInt8, an::OutputCompression::Jpeg},
        Container{an::createDumpCodec(), an::CodecFormat::Dump, an::SampleType::UInt16, an::OutputCompression::None},
        Container{an::createXisfCodec(), an::CodecFormat::Xisf, an::SampleType::UInt16, an::OutputCompression::Zstd}};
    for (const Container& container : containers)
    {
        ASSERT_TRUE(container.adapter.hasValue());
        const std::array adapters{*container.adapter};
        an::EngineContextOptions options;
        options.codecRegistry = *an::CodecRegistry::create(adapters);
        auto engine = an::EngineContext::create(options);
        ASSERT_TRUE(engine.hasValue());
        const auto rawFormat = an::rawSampleFormatOf(container.sample);
        const auto extent = *an::ImageExtent::create(16u, 16u, 1u, engine->limits());
        const auto descriptor = *an::RawFrameDescriptor::create({extent,
                                                                 rawFormat,
                                                                 an::ByteOrder::Native,
                                                                 an::ChannelSchema::Mono,
                                                                 an::ColorSpace::Unspecified,
                                                                 an::NumericDomain::NormalizedLinear,
                                                                 {},
                                                                 an::CoordinateOrigin::TopLeft,
                                                                 1.0});
        const auto layout = *an::ImageLayout::create({extent, rawFormat.sizeBytes, 1u, 1u, 1u}, engine->limits());
        auto frame = an::RawFrame::create(
            descriptor, layout, *an::SampleBuffer::createOwned(*engine, layout, an::StorageInitialization::Zeroed));
        ASSERT_TRUE(frame.hasValue());
        const auto destination = (directory / "frame").u8string();
        const std::string path(reinterpret_cast<const char*>(destination.data()), destination.size());
        const bool compressed = container.compression != an::OutputCompression::None;
        std::vector<an::OutputIntentRequest> stated;
        if (compressed)
        {
            stated.push_back(
                {.sampleType = container.sample, .compression = container.compression, .byteShuffle = true});
            stated.push_back(
                {.sampleType = container.sample, .compression = container.compression, .compressionBlockBytes = 4096u});
        }
        stated.push_back({.sampleType = container.sample,
                          .compression = container.compression,
                          .checksum = an::OutputChecksum::Sha256});
        for (const an::OutputIntentRequest& request : stated)
        {
            SCOPED_TRACE(testing::Message()
                         << "codec=" << unsigned(container.format) << " shuffle=" << request.byteShuffle.has_value()
                         << " block=" << request.compressionBlockBytes.has_value()
                         << " checksum=" << an::outputChecksumName(request.checksum));
            auto intent = an::OutputIntent::create(request);
            ASSERT_TRUE(intent.hasValue()) << intent.status().message();
            auto execution = an::ExecutionContext::create(*engine, {});
            ASSERT_TRUE(execution.hasValue());
            auto result = an::writeImage(*engine, *execution,
                                         {.destinationPath = path,
                                          .format = container.format,
                                          .intent = &*intent,
                                          .durability = an::OutputDurability::CloseOnly},
                                         *frame, {});
            if (container.format == an::CodecFormat::Xisf)
            {
                EXPECT_TRUE(result.hasValue()) << result.status().message();
                continue;
            }
            ASSERT_FALSE(result.hasValue());
            EXPECT_EQ(result.code(), an::ErrorCode::Unsupported);
            EXPECT_FALSE(std::filesystem::exists(directory / "frame"));
        }
        // Without one, each writes the compression it writes.
        auto plain = an::OutputIntent::create({.sampleType = container.sample, .compression = container.compression});
        ASSERT_TRUE(plain.hasValue());
        auto execution = an::ExecutionContext::create(*engine, {});
        auto written = an::writeImage(*engine, *execution,
                                      {.destinationPath = path,
                                       .format = container.format,
                                       .intent = &*plain,
                                       .durability = an::OutputDurability::CloseOnly},
                                      *frame, {});
        EXPECT_TRUE(written.hasValue()) << written.status().message();
        std::error_code ignored;
        std::filesystem::remove(directory / "frame", ignored);
    }
}
} // namespace
