// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/RawCodec.h>

#include <AfterNightEngine/Algorithms/Demosaic/CfaWhiteBalance.h>
#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <limits>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace
{
/// Independent, uncompressed little-endian DNG writer. No codec under test constructs this input.
class RawCodecTest : public testing::Test
{
protected:
    struct Tag
    {
        std::uint16_t key;
        std::uint16_t type;
        std::uint32_t count;
        std::vector<std::uint8_t> bytes;
    };
    static std::vector<std::uint8_t> words(std::initializer_list<std::uint32_t> values, unsigned size = 4u)
    {
        std::vector<std::uint8_t> result;
        for (auto value : values)
            for (unsigned i = 0u; i < size; ++i)
                result.push_back(static_cast<std::uint8_t>(value >> (8u * i)));
        return result;
    }
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
                    ("an-raw-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        auto created = makeEngine();
        ASSERT_TRUE(created.hasValue()) << created.status().message();
        engine = std::move(*created);
    }
    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
    static an::Result<an::EngineContext> makeEngine(std::uint64_t budget = 0u, an::RawCodecOptions rawOptions = {})
    {
        auto codec = an::createRawCodec(rawOptions);
        if (!codec.hasValue())
            return codec.status();
        std::array codecs{*codec};
        auto registry = an::CodecRegistry::create(codecs);
        if (!registry.hasValue())
            return registry.status();
        an::EngineContextOptions options;
        options.codecRegistry = *registry;
        options.maxConcurrency = 2u;
        if (budget)
            options.limits.memoryBudgetBytes = budget;
        return an::EngineContext::create(options);
    }
    /// The fields a camera writes into its Exif directory: an exposure of 1/125 s at f/2.8 and ISO 800, taken at
    /// 2026:10:25 21:30:00.
    static std::vector<Tag> cameraExifTags()
    {
        return {{33434, 5, 1, words({1, 125})},
                {33437, 5, 1, words({28, 10})},
                {34855, 3, 1, words({800}, 2)},
                {36867, 2, 20, {'2', '0', '2', '6', ':', '1', '0', ':', '2', '5',
                                ' ', '2', '1', ':', '3', '0', ':', '0', '0', 0}}};
    }
    /// Writes one directory's entries at the end of `bytes`, the values that do not fit an entry after them.
    static void appendDirectory(std::vector<std::uint8_t>& bytes, const std::vector<Tag>& tags)
    {
        const auto start = static_cast<unsigned>(bytes.size());
        const auto valuesStart = start + 2u + static_cast<unsigned>(tags.size()) * 12u + 4u;
        std::vector<std::uint8_t> values;
        const auto append = [&](const auto& part) { bytes.insert(bytes.end(), part.begin(), part.end()); };
        append(words({static_cast<std::uint32_t>(tags.size())}, 2));
        for (const auto& tag : tags)
        {
            append(words({tag.key, tag.type}, 2));
            append(words({tag.count}));
            if (tag.bytes.size() <= 4u)
            {
                append(tag.bytes);
                bytes.resize(bytes.size() + 4u - tag.bytes.size(), 0);
            }
            else
            {
                append(words({valuesStart + static_cast<unsigned>(values.size())}));
                values.insert(values.end(), tag.bytes.begin(), tag.bytes.end());
                if (values.size() % 2u)
                    values.push_back(0);
            }
        }
        append(words({0}));
        append(values);
    }
    /// `exifTags` are written as the Exif directory the first directory points to, and `xmp` as the first directory's
    /// XMP packet.
    std::string fixture(bool fullColor = false, std::array<std::uint8_t, 4> pattern = {0, 1, 1, 2},
                        bool truncate = false, bool zeroBlack = false, bool tiledBlack = false, unsigned width = 64u,
                        unsigned height = 48u, const std::vector<Tag>& exifTags = {}, const std::string& xmp = {})
    {
        const unsigned channels = fullColor ? 3u : 1u;
        std::vector<Tag> tags{
            {254, 4, 1, words({0})},
            {256, 4, 1, words({width})},
            {257, 4, 1, words({height})},
            {258, 3, channels, fullColor ? words({16, 16, 16}, 2) : words({16}, 2)},
            {259, 3, 1, words({1}, 2)},
            {262, 3, 1, words({fullColor ? 34892u : 32803u}, 2)},
            {271, 2, 5, {'T', 'e', 's', 't', 0}},
            {272, 2, 7, {'C', 'a', 'm', 'e', 'r', 'a', 0}},
            {273, 4, 1, words({0})},
            {274, 3, 1, words({6}, 2)},
            {277, 3, 1, words({channels}, 2)},
            {278, 4, 1, words({height})},
            {279, 4, 1, words({width * height * channels * 2u})},
            {284, 3, 1, words({1}, 2)},
            {50706, 1, 4, {1, 4, 0, 0}},
            {50707, 1, 4, {1, 1, 0, 0}},
            {50708, 2, 12, {'T', 'e', 's', 't', ' ', 'C', 'a', 'm', 'e', 'r', 'a', 0}},
            {50714, 5, tiledBlack ? 4u : 1u,
             tiledBlack ? words({100, 1, 200, 1, 300, 1, 400, 1}) : words({zeroBlack ? 0u : 100u, 1})},
            {50717, 4, 1, words({4095})},
            {50721, 10, 9, words({1, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1})},
            {50728, 5, 3, words({1, 2, 1, 1, 1, 3})},
            {50778, 3, 1, words({21}, 2)}};
        if (!fullColor)
        {
            tags.push_back({33421, 3, 2, words({2, 2}, 2)});
            tags.push_back({33422, 1, 4, {pattern.begin(), pattern.end()}});
            tags.push_back({50710, 1, 3, {0, 1, 2}});
            tags.push_back({50711, 3, 1, words({1}, 2)});
        }
        if (tiledBlack)
            tags.push_back({50713, 3, 2, words({2, 2}, 2)});
        if (!exifTags.empty())
            tags.push_back({34665, 4, 1, words({0})});
        if (!xmp.empty())
            tags.push_back({700, 1, static_cast<std::uint32_t>(xmp.size()), {xmp.begin(), xmp.end()}});
        std::sort(tags.begin(), tags.end(), [](const Tag& a, const Tag& b) { return a.key < b.key; });
        std::vector<std::uint8_t> bytes{'I', 'I', 42, 0, 8, 0, 0, 0};
        const auto append = [&](const auto& part) { bytes.insert(bytes.end(), part.begin(), part.end()); };
        append(words({static_cast<std::uint32_t>(tags.size())}, 2));
        const auto extraStart = 8u + 2u + static_cast<unsigned>(tags.size()) * 12u + 4u;
        std::vector<std::uint8_t> extra;
        std::size_t stripOffset = 0u;
        std::size_t exifPointer = 0u;
        for (const auto& tag : tags)
        {
            append(words({tag.key, tag.type}, 2));
            append(words({tag.count}));
            if (tag.key == 273)
                stripOffset = bytes.size();
            if (tag.key == 34665)
                exifPointer = bytes.size();
            if (tag.bytes.size() <= 4u)
            {
                append(tag.bytes);
                bytes.resize(bytes.size() + 4u - tag.bytes.size(), 0);
            }
            else
            {
                append(words({extraStart + static_cast<unsigned>(extra.size())}));
                extra.insert(extra.end(), tag.bytes.begin(), tag.bytes.end());
                if (extra.size() % 2u)
                    extra.push_back(0);
            }
        }
        append(words({0}));
        append(extra);
        if (!exifTags.empty())
        {
            const auto directory = words({static_cast<unsigned>(bytes.size())});
            std::copy(directory.begin(), directory.end(), bytes.begin() + exifPointer);
            appendDirectory(bytes, exifTags);
        }
        const auto offset = words({static_cast<unsigned>(bytes.size())});
        std::copy(offset.begin(), offset.end(), bytes.begin() + stripOffset);
        if (!truncate)
            for (unsigned i = 0u; i < width * height; ++i)
                for (unsigned c = 0u; c < channels; ++c)
                    append(words({zeroBlack && i == width * height - 1u ? 65535u : 100u + (i + c * 521u) % 3900u}, 2));
        const auto native = directory / std::filesystem::path(u8"cámara[1].dng");
        std::ofstream output(native, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        const auto utf8 = native.u8string();
        return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
    }
    std::filesystem::path directory;
    an::EngineContext engine;
};

TEST_F(RawCodecTest, IndependentBayerCodesBlackLevelsAndOrientation)
{
    const auto path = fixture();
    auto execution = an::ExecutionContext::create(engine, {});
    auto probe = an::probeImage(engine, *execution, {.path = path});
    ASSERT_TRUE(probe.hasValue()) << an::errorCodeName(probe.status().code());
    EXPECT_EQ(probe->disposition, an::CodecProbeDisposition::DecodeRequired);
    EXPECT_EQ(probe->descriptor.extent().width(), 64u);
    EXPECT_EQ(probe->descriptor.extent().height(), 48u);
    auto result = an::decodeImage(engine, *execution, {.path = path});
    ASSERT_TRUE(result.hasValue()) << an::errorCodeName(result.status().code());
    EXPECT_EQ(result->frame.descriptor()->numericDomain(), an::NumericDomain::RawSensorCode);
    EXPECT_EQ(result->frame.descriptor()->cfa().pattern(), an::CfaPattern::Rggb);
    EXPECT_TRUE(result->frame.checkAdoptable().ok());
    ASSERT_TRUE(result->metadata.acquisition().rawCamera());
    EXPECT_EQ(result->metadata.acquisition().rawCamera()->orientation, 6);
    EXPECT_EQ(result->metadata.acquisition().rawCamera()->whiteLevel, 4095u);
    EXPECT_FALSE(result->metadata.outputIntent());
    EXPECT_EQ(result->metadata.semantics().transferFunction(), an::TransferFunction::Linear);
    for (unsigned i = 0u; i < 64u * 48u; ++i)
    {
        std::uint16_t value;
        std::memcpy(&value, result->frame.bytes().data() + 2u * i, 2u);
        ASSERT_EQ(value, i % 3900u) << i;
    }
}

// A camera file's Exif reaches the decoded image as cards of the camera RAW namespace, as a raster container's does,
// which is what the Exif window, a save to a raster container and a product's observation cards read. The decoder
// stated none, so a camera file had no Exif anywhere after it was opened.
TEST_F(RawCodecTest, ADecodeCarriesTheExifTheFileStatesAsCameraRawCards)
{
    const auto path = fixture(false, {0, 1, 1, 2}, false, false, false, 64u, 48u, cameraExifTags());
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    auto decoded = an::decodeImage(engine, *execution, {.path = path});
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
    const an::CodecMetadata& codec = decoded->metadata.codec();
    const auto value = [&codec](const char* keyword) {
        const an::CodecCard* card = codec.find(an::CodecNamespace::CameraRaw, keyword);
        return card != nullptr ? card->value() : std::string("<no card>");
    };
    EXPECT_EQ(value("Exif.Image.Make"), "Test");
    EXPECT_EQ(value("Exif.Image.Model"), "Camera");
    EXPECT_EQ(value("Exif.Photo.ExposureTime"), "1/125");
    EXPECT_EQ(value("Exif.Photo.FNumber"), "28/10");
    EXPECT_EQ(value("Exif.Photo.ISOSpeedRatings"), "800");
    EXPECT_EQ(value("Exif.Photo.DateTimeOriginal"), "2026:10:25 21:30:00");
    // The field's type travels in the card's comment, so a save writes it back in that type.
    const an::CodecCard* exposure = codec.find(an::CodecNamespace::CameraRaw, "Exif.Photo.ExposureTime");
    ASSERT_NE(exposure, nullptr);
    EXPECT_EQ(exposure->comment(), "Rational");
    // Where the file kept its pixels and its directories says nothing about the picture.
    EXPECT_EQ(codec.find(an::CodecNamespace::CameraRaw, "Exif.Image.StripOffsets"), nullptr);
    EXPECT_EQ(codec.find(an::CodecNamespace::CameraRaw, "Exif.Image.ExifTag"), nullptr);
    for (const an::CodecCard& card : codec.cards())
        EXPECT_EQ(card.origin(), an::CodecNamespace::CameraRaw) << card.keyword();
    // The typed camera record is the decoder's as before.
    ASSERT_TRUE(decoded->metadata.acquisition().rawCamera());
    EXPECT_EQ(decoded->metadata.acquisition().rawCamera()->whiteLevel, 4095u);
}

// Only a decode that asks for metadata reads the file's Exif: a probe states LibRaw's header facts, so a folder of
// camera files is probed without every file being read again for its Exif, and a decode that asks for the
// description only is given none.
TEST_F(RawCodecTest, OnlyADecodeThatAsksForMetadataReadsTheFilesExif)
{
    const auto path = fixture(false, {0, 1, 1, 2}, false, false, false, 64u, 48u, cameraExifTags());
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    auto probed = an::probeImage(engine, *execution, {.path = path});
    ASSERT_TRUE(probed.hasValue()) << probed.status().message();
    EXPECT_TRUE(probed->metadata.codec().cards().empty());
    auto plain = an::decodeImage(engine, *execution, {.path = path, .readMetadata = false});
    ASSERT_TRUE(plain.hasValue()) << plain.status().message();
    EXPECT_TRUE(plain->metadata.codec().cards().empty());
}

// The first directory's XMP packet is removed before the metadata library reads a camera file, as it is for a TIFF:
// the engine reads Exif and never hands a file's XML to the library's XML parser, which would also have stated the
// packet as a card.
TEST_F(RawCodecTest, ACameraFilesXmpPacketIsNeverDecoded)
{
    const std::string xmp = "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
                            "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'/></x:xmpmeta>";
    const auto path = fixture(false, {0, 1, 1, 2}, false, false, false, 64u, 48u, cameraExifTags(), xmp);
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    auto decoded = an::decodeImage(engine, *execution, {.path = path});
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
    const an::CodecMetadata& codec = decoded->metadata.codec();
    const an::CodecCard* model = codec.find(an::CodecNamespace::CameraRaw, "Exif.Image.Model");
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->value(), "Camera");
    EXPECT_NE(codec.find(an::CodecNamespace::CameraRaw, "Exif.Photo.ExposureTime"), nullptr);
    EXPECT_EQ(codec.find(an::CodecNamespace::CameraRaw, "Exif.Image.XMLPacket"), nullptr);
}

// A camera file's Exif is read by another library than the one that decodes its pixels, so Exif the image cannot hold
// - here one field longer than every card one image preserves - leaves the file readable: the decode says so as a
// warning and states no card, rather than refusing an image the RAW library read.
TEST_F(RawCodecTest, ExifTheImageCannotHoldIsAWarningRatherThanARefusal)
{
    struct Warnings final : an::DiagnosticSink
    {
        void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
        {
            count += event.severity == an::DiagnosticSeverity::Warning && event.domain == an::ErrorDomain::Io ? 1 : 0;
        }
        int count = 0;
    } warnings;
    // Nine million bytes of 255 are rendered as 36 MB of text, more than the 8192 cards of 4096 bytes an image keeps.
    std::vector<Tag> fields = cameraExifTags();
    fields.push_back({0xbf00, 7, 9000000u, std::vector<std::uint8_t>(9000000u, 255u)});
    const auto path = fixture(false, {0, 1, 1, 2}, false, false, false, 64u, 48u, fields);
    an::ExecutionOptions options;
    options.diagnosticSink = &warnings;
    auto execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());
    auto decoded = an::decodeImage(engine, *execution, {.path = path});
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
    EXPECT_EQ(decoded->frame.descriptor()->extent().width(), 64u);
    EXPECT_TRUE(decoded->metadata.codec().cards().empty());
    EXPECT_EQ(warnings.count, 1);
}

// A camera file's Exif is read through a mapping of the whole file on every platform, which takes nothing from the
// context's budget: a decode under a budget that holds the image but not a second copy of a long file states the Exif
// the file carries, as a decode under a loose budget does, and leaves nothing out to warn of.
TEST_F(RawCodecTest, ADecodeUnderABudgetShorterThanTheFileStillCarriesItsExif)
{
    struct Warnings final : an::DiagnosticSink
    {
        void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
        {
            count += event.severity == an::DiagnosticSeverity::Warning && event.domain == an::ErrorDomain::Io ? 1 : 0;
        }
        int count = 0;
    } warnings;
    const auto path = fixture(false, {0, 1, 1, 2}, false, false, false, 64u, 48u, cameraExifTags());
    // Sixteen mebibytes after the pixels that no directory names, as a camera file's maker data and previews are to
    // this decode: the file is many times the budget, the image a small part of it.
    const std::filesystem::path native = directory / std::filesystem::path(u8"cámara[1].dng");
    {
        std::ofstream padding(native, std::ios::binary | std::ios::app);
        const std::vector<char> zeros(std::size_t{1} << 20u, 0);
        for (int mebibyte = 0; mebibyte < 16; ++mebibyte)
            padding.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    }
    constexpr std::uint64_t kBudget = std::uint64_t{1} << 20u;
    ASSERT_GT(std::filesystem::file_size(native), 16u * kBudget);
    auto tight = makeEngine(kBudget);
    ASSERT_TRUE(tight.hasValue()) << tight.status().message();
    an::ExecutionOptions options;
    options.diagnosticSink = &warnings;
    auto execution = an::ExecutionContext::create(*tight, options);
    ASSERT_TRUE(execution.hasValue());
    auto decoded = an::decodeImage(*tight, *execution, {.path = path});
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
    const an::CodecCard* exposure =
        decoded->metadata.codec().find(an::CodecNamespace::CameraRaw, "Exif.Photo.ExposureTime");
    ASSERT_NE(exposure, nullptr) << "the decode under the budget states no Exif";
    EXPECT_EQ(exposure->value(), "1/125");
    EXPECT_EQ(warnings.count, 0) << "the Exif was left out";
}

TEST_F(RawCodecTest, FullScaleAndSamplesAboveTheStatedWhiteLevelAreRetained)
{
    const auto path = fixture(false, {0, 1, 1, 2}, false, true);
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    auto result = an::decodeImage(engine, *execution, {.path = path});
    ASSERT_TRUE(result.hasValue());
    std::uint16_t last;
    std::memcpy(&last, result->frame.bytes().data() + result->frame.bytes().size() - 2u, 2u);
    EXPECT_EQ(last, 65535u);
    ASSERT_TRUE(result->metadata.acquisition().rawCamera());
    EXPECT_EQ(result->metadata.acquisition().rawCamera()->whiteLevel, 4095u);
}

TEST_F(RawCodecTest, AMosaicLoadIsWhiteBalanceScaleAndTurnOverTheDecodedMosaic)
{
    // A mosaic is not balanced, stretched or turned inside the loader. The decoder hands over the
    // black-subtracted mosaic and the camera record, and the three steps are operations a caller states:
    // the camera's own gains resolved over full scale, applied under each sample's filter, and the recorded
    // flip applied with the filter array restated.
    const auto path = fixture();
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    auto decoded = an::decodeImage(engine, *execution, {.path = path});
    ASSERT_TRUE(decoded.hasValue()) << an::errorCodeName(decoded.status().code());
    const std::optional<an::RawCameraMetadata>& camera = decoded->metadata.acquisition().rawCamera();
    ASSERT_TRUE(camera.has_value());
    const std::optional<an::RawFrameDescriptor> raw = decoded->frame.descriptor();
    ASSERT_TRUE(raw.has_value());
    const std::optional<an::ImageDescriptor> mosaic = raw->toImageDescriptor();
    ASSERT_TRUE(mosaic.has_value());
    const an::ImageLayout sourceLayout = *decoded->frame.layout();
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();

    const an::Result<an::CfaWhiteBalance> balance =
        an::resolveCfaWhiteBalance(*camera, {.mode = an::CfaWhiteBalanceMode::Camera});
    ASSERT_TRUE(balance.hasValue()) << balance.status().message();
    EXPECT_EQ(balance->source, an::CfaWhiteBalanceSource::CameraAsShot);
    std::uint32_t commonBlack =
        camera->blackLevel + *std::min_element(std::begin(camera->channelBlack), std::end(camera->channelBlack));
    if (!camera->blackPattern.empty())
    {
        commonBlack += *std::min_element(camera->blackPattern.begin(), camera->blackPattern.end());
    }
    EXPECT_DOUBLE_EQ(balance->fullScale, 65535.0 / static_cast<double>(camera->whiteLevel - commonBlack));
    const an::CfaChannelValues multipliers = balance->multipliers();

    const an::ImageLayout planeLayout = *an::ImageLayout::create({.extent = sourceLayout.extent(),
                                                                  .elementSizeBytes = 2u,
                                                                  .samplesPerPixel = 1u,
                                                                  .planeCount = 1u,
                                                                  .rowAlignmentBytes = 1u},
                                                                 engine.limits());
    std::vector<std::uint16_t> balanced(static_cast<std::size_t>(width) * height);
    const an::Result<an::CfaChannelResult> gained =
        an::applyCfaChannelGains(*execution, an::SampleType::UInt16, decoded->frame.bytes().data(), sourceLayout,
                                 raw->cfa(), balanced.data(), planeLayout, multipliers);
    ASSERT_TRUE(gained.hasValue()) << gained.status().message();

    const std::optional<an::ImageOrientation> orientation = an::imageOrientationFromRawFlip(camera->orientation);
    ASSERT_EQ(orientation, an::ImageOrientation::Rotate90Clockwise);
    const an::Result<an::GeometryPlan> plan = an::planOrientation(*mosaic, *orientation);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    const an::ImageDescriptor& turned = plan->destinationDescriptor();
    ASSERT_EQ(turned.extent().width(), height);
    ASSERT_EQ(turned.extent().height(), width);
    const an::ImageLayout turnedLayout = *an::ImageLayout::create({.extent = turned.extent(),
                                                                   .elementSizeBytes = 2u,
                                                                   .samplesPerPixel = 1u,
                                                                   .planeCount = 1u,
                                                                   .rowAlignmentBytes = 1u},
                                                                  engine.limits());
    std::vector<std::uint16_t> presented(static_cast<std::size_t>(width) * height);
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, turned.extent());
    ASSERT_TRUE(mask.hasValue());
    const std::optional<an::ValidityMaskView<std::uint8_t>> maskView = mask->view();
    ASSERT_TRUE(maskView.has_value());
    const an::Result<an::GeometryResult> ran =
        an::transformImageGeometry(*execution, an::SampleType::UInt16, balanced.data(), planeLayout, presented.data(),
                                   turnedLayout, *plan, {}, *maskView);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();

    // A clockwise quarter turn shows at (x, y) the stored sample at (y, height - 1 - x), still under the filter it
    // was measured through, carrying its own filter's gain.
    for (std::uint32_t y = 0u; y < width; ++y)
    {
        for (std::uint32_t x = 0u; x < height; ++x)
        {
            const std::uint32_t sourceX = y;
            const std::uint32_t sourceY = height - 1u - x;
            const an::CfaColor color = raw->cfa().colorAt(sourceX, sourceY);
            ASSERT_EQ(turned.cfa().colorAt(x, y), color) << x << "," << y;
            const double gain = color == an::CfaColor::Red     ? multipliers.red
                                : color == an::CfaColor::Green ? multipliers.green
                                                               : multipliers.blue;
            const double stored = static_cast<double>((sourceY * width + sourceX) % 3900u);
            const double expected = (std::min)(std::nearbyint(stored * gain), 65535.0);
            ASSERT_EQ(presented[static_cast<std::size_t>(y) * height + x], static_cast<std::uint16_t>(expected))
                << x << "," << y;
        }
    }
}

TEST_F(RawCodecTest, ForcedAccelerationRefusesUnqualifiedInputWithoutPublishingAFrame)
{
    const auto path = fixture();
    auto required = makeEngine(0u, {.decoderPreference = an::RawDecoderPreference::RequireRawSpeed});
    ASSERT_TRUE(required.hasValue());
    auto execution = an::ExecutionContext::create(*required, {});
    ASSERT_TRUE(execution.hasValue());
    const auto result = an::decodeImage(*required, *execution, {.path = path});
    EXPECT_FALSE(result.hasValue());
    EXPECT_EQ(result.status().code(), an::ErrorCode::UnsupportedBackend);
    EXPECT_EQ(required->memoryResource()->counters().liveBytes, 0u);
}

TEST_F(RawCodecTest, NativeAndAutomaticRetainEveryDefinedSampleForUnqualifiedInputs)
{
    const auto path = fixture(false, {1, 2, 0, 1}, false, false, true, 65u, 49u);
    auto native = makeEngine(0u, {.decoderPreference = an::RawDecoderPreference::NativeLibRaw});
    ASSERT_TRUE(native.hasValue());
    auto automaticExecution = an::ExecutionContext::create(engine, {});
    auto nativeExecution = an::ExecutionContext::create(*native, {});
    ASSERT_TRUE(automaticExecution.hasValue());
    ASSERT_TRUE(nativeExecution.hasValue());
    auto automaticResult = an::decodeImage(engine, *automaticExecution, {.path = path});
    auto nativeResult = an::decodeImage(*native, *nativeExecution, {.path = path});
    ASSERT_TRUE(automaticResult.hasValue());
    ASSERT_TRUE(nativeResult.hasValue());
    ASSERT_EQ(automaticResult->frame.bytes().size(), nativeResult->frame.bytes().size());
    EXPECT_EQ(std::memcmp(automaticResult->frame.bytes().data(), nativeResult->frame.bytes().data(),
                          nativeResult->frame.bytes().size()),
              0);
    EXPECT_EQ(automaticResult->metadata.acquisition(), nativeResult->metadata.acquisition());
}

TEST(RawCodecOptionsTest, RejectsUnknownDecoderPreference)
{
    auto codec = an::createRawCodec({.decoderPreference = static_cast<an::RawDecoderPreference>(255)});
    ASSERT_FALSE(codec.hasValue());
    EXPECT_EQ(codec.status().code(), an::ErrorCode::InvalidArgument);
}

TEST_F(RawCodecTest, ReadPreferenceOverridesTheDefaultWhileEarlierFramesRemainAlive)
{
    const auto path = fixture();
    auto required = makeEngine(0u, {.decoderPreference = an::RawDecoderPreference::RequireRawSpeed});
    ASSERT_TRUE(required.hasValue());
    auto execution = an::ExecutionContext::create(*required, {});
    ASSERT_TRUE(execution.hasValue());
    auto first = an::decodeImage(*required, *execution,
                                 {.path = path, .rawDecoderPreference = an::RawDecoderPreference::NativeLibRaw});
    ASSERT_TRUE(first.hasValue()) << an::errorCodeName(first.status().code());
    EXPECT_GT(required->memoryResource()->counters().liveBytes, 0u);
    auto second = an::decodeImage(*required, *execution,
                                  {.path = path, .rawDecoderPreference = an::RawDecoderPreference::Automatic});
    ASSERT_TRUE(second.hasValue());
    ASSERT_EQ(first->frame.bytes().size(), second->frame.bytes().size());
    EXPECT_EQ(std::memcmp(first->frame.bytes().data(), second->frame.bytes().data(), first->frame.bytes().size()), 0);
    EXPECT_EQ(first->metadata.acquisition(), second->metadata.acquisition());
    const auto inherited = an::decodeImage(*required, *execution, {.path = path});
    EXPECT_FALSE(inherited.hasValue());
    EXPECT_EQ(inherited.status().code(), an::ErrorCode::UnsupportedBackend);
}

TEST_F(RawCodecTest, RequiredReadOverridesAutomaticAndNativeAdapterDefaults)
{
    const auto path = fixture();
    for (const auto preference : {an::RawDecoderPreference::Automatic, an::RawDecoderPreference::NativeLibRaw})
    {
        auto context = makeEngine(0u, {.decoderPreference = preference});
        ASSERT_TRUE(context.hasValue());
        auto execution = an::ExecutionContext::create(*context, {});
        ASSERT_TRUE(execution.hasValue());
        const auto result = an::decodeImage(
            *context, *execution, {.path = path, .rawDecoderPreference = an::RawDecoderPreference::RequireRawSpeed});
        EXPECT_FALSE(result.hasValue());
        EXPECT_EQ(result.status().code(), an::ErrorCode::UnsupportedBackend);
        EXPECT_EQ(context->memoryResource()->counters().liveBytes, 0u);
    }
}

TEST_F(RawCodecTest, InvalidReadPreferenceIsRefusedBeforeOpeningTheFile)
{
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const an::CodecReadRequest request{.path = "does-not-exist.dng",
                                       .rawDecoderPreference = static_cast<an::RawDecoderPreference>(255)};
    EXPECT_EQ(an::probeImage(engine, *execution, request).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::decodeImage(engine, *execution, request).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(RawCodecTest, ConcurrentReadPreferencesAreIndependentInOneContext)
{
    const auto path = fixture();
    const auto read = [&](an::RawDecoderPreference preference) {
        auto execution = an::ExecutionContext::create(engine, {.maxConcurrency = 1u});
        if (!execution.hasValue())
            return execution.status().code();
        auto result = an::decodeImage(engine, *execution, {.path = path, .rawDecoderPreference = preference});
        return result.status().code();
    };
    auto native = std::async(std::launch::async, read, an::RawDecoderPreference::NativeLibRaw);
    auto required = std::async(std::launch::async, read, an::RawDecoderPreference::RequireRawSpeed);
    EXPECT_EQ(native.get(), an::ErrorCode::Ok);
    EXPECT_EQ(required.get(), an::ErrorCode::UnsupportedBackend);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(RawCodecTest, UnequalBlackTilesFollowCoordinatesAcrossEveryBayerPhase)
{
    for (const auto pattern : {std::array<std::uint8_t, 4>{0, 1, 1, 2}, std::array<std::uint8_t, 4>{2, 1, 1, 0},
                               std::array<std::uint8_t, 4>{1, 0, 2, 1}, std::array<std::uint8_t, 4>{1, 2, 0, 1}})
    {
        const auto path = fixture(false, pattern, false, false, true);
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        auto result = an::decodeImage(engine, *execution, {.path = path});
        ASSERT_TRUE(result.hasValue());
        for (unsigned y = 0u; y < 48u; ++y)
            for (unsigned x = 0u; x < 64u; ++x)
            {
                const auto index = y * 64u + x;
                const int expected = static_cast<int>(index) - static_cast<int>((y % 2u) * 2u + x % 2u) * 100;
                std::uint16_t value;
                std::memcpy(&value, result->frame.bytes().data() + index * 2u, 2u);
                ASSERT_EQ(value, (std::max)(0, expected)) << x << "," << y;
            }
    }
}

TEST_F(RawCodecTest, UnequalBlackLevelsPreserveOddRowsAndLastColumn)
{
    constexpr unsigned width = 65u;
    constexpr unsigned height = 49u;
    for (const auto pattern : {std::array<std::uint8_t, 4>{0, 1, 1, 2}, std::array<std::uint8_t, 4>{2, 1, 1, 0},
                               std::array<std::uint8_t, 4>{1, 0, 2, 1}, std::array<std::uint8_t, 4>{1, 2, 0, 1}})
    {
        const auto path = fixture(false, pattern, false, false, true, width, height);
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        auto result = an::decodeImage(engine, *execution, {.path = path});
        ASSERT_TRUE(result.hasValue());
        ASSERT_EQ(result->frame.descriptor()->extent().width(), width);
        ASSERT_EQ(result->frame.descriptor()->extent().height(), height);
        for (unsigned y = 0u; y < height; ++y)
            for (unsigned x = 0u; x < width; ++x)
            {
                const auto index = y * width + x;
                const int expected = static_cast<int>(index) - static_cast<int>((y % 2u) * 2u + x % 2u) * 100;
                std::uint16_t value;
                std::memcpy(&value, result->frame.bytes().data() + index * 2u, 2u);
                ASSERT_EQ(value, (std::max)(0, expected)) << x << "," << y;
            }
    }
}

TEST_F(RawCodecTest, EveryBayerPatternHasItsOwnDescriptor)
{
    const std::array patterns{std::array<std::uint8_t, 4>{0, 1, 1, 2}, std::array<std::uint8_t, 4>{2, 1, 1, 0},
                              std::array<std::uint8_t, 4>{1, 0, 2, 1}, std::array<std::uint8_t, 4>{1, 2, 0, 1}};
    const std::array expected{an::CfaPattern::Rggb, an::CfaPattern::Bggr, an::CfaPattern::Grbg, an::CfaPattern::Gbrg};
    for (std::size_t i = 0u; i < patterns.size(); ++i)
    {
        const auto path = fixture(false, patterns[i]);
        auto execution = an::ExecutionContext::create(engine, {});
        auto result = an::decodeImage(engine, *execution, {.path = path});
        ASSERT_TRUE(result.hasValue());
        EXPECT_EQ(result->frame.descriptor()->cfa().pattern(), expected[i]);
    }
}

TEST_F(RawCodecTest, FullColorLinearDngProducesThreeDistinctPlanes)
{
    const auto path = fixture(true);
    auto execution = an::ExecutionContext::create(engine, {});
    auto result = an::decodeImage(engine, *execution, {.path = path});
    ASSERT_TRUE(result.hasValue()) << an::errorCodeName(result.status().code());
    EXPECT_EQ(result->frame.layout()->planeCount(), 3u);
    EXPECT_EQ(result->frame.descriptor()->numericDomain(), an::NumericDomain::NormalizedLinear);
    EXPECT_EQ(result->frame.descriptor()->colorSpace(), an::ColorSpace::LinearRgb);
    EXPECT_NE(std::memcmp(result->frame.row(0, 20)->data(), result->frame.row(1, 20)->data(), 128u), 0);
    EXPECT_NE(std::memcmp(result->frame.row(1, 20)->data(), result->frame.row(2, 20)->data(), 128u), 0);
}

TEST_F(RawCodecTest, HeaderOnlyProbeDoesNotDecodeTruncatedPayload)
{
    const auto path = fixture(false, {0, 1, 1, 2}, true);
    auto execution = an::ExecutionContext::create(engine, {});
    auto probe = an::probeImage(engine, *execution, {.path = path, .validatePayload = false});
    ASSERT_TRUE(probe.hasValue());
    auto result = an::decodeImage(engine, *execution, {.path = path});
    EXPECT_FALSE(result.hasValue());
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(RawCodecTest, BudgetCancellationSelectionAndUnsupportedOperations)
{
    const auto path = fixture();
    auto tiny = makeEngine(1024u);
    auto execution = an::ExecutionContext::create(*tiny, {});
    auto result = an::decodeImage(*tiny, *execution, {.path = path});
    EXPECT_EQ(result.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    {
        auto normal = an::ExecutionContext::create(engine, {});
        EXPECT_FALSE(an::decodeImage(engine, *normal, {.path = path, .imageIndex = 1u}).hasValue());
        EXPECT_FALSE(an::decodeImageRegion(engine, *normal, {.path = path}, {0, 0, 4, 4}).hasValue());
    }
    std::stop_source stop;
    an::ExecutionOptions options;
    options.stopToken = stop.get_token();
    auto cancelled = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(cancelled.hasValue());
    stop.request_stop();
    EXPECT_EQ(an::decodeImage(engine, *cancelled, {.path = path}).status().code(), an::ErrorCode::Cancelled);
}

TEST_F(RawCodecTest, InvalidConfigurationAndMalformedInputAreTypedFailures)
{
    EXPECT_FALSE(an::createRawCodec({.libraryMemoryLimitMegabytes = 0u}).hasValue());
    EXPECT_FALSE(an::createRawCodec({.fullColorWhiteBalance = an::RawFullColorWhiteBalance::Custom,
                                     .fullColorMultipliers = {1.0, 0.0, 1.0}})
                     .hasValue());
    const auto path = fixture();
    std::ofstream(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.c_str()))),
                  std::ios::binary | std::ios::trunc)
        << "not a camera image";
    auto execution = an::ExecutionContext::create(engine, {});
    EXPECT_FALSE(an::decodeImage(engine, *execution, {.path = path}).hasValue());
    EXPECT_FALSE(an::probeImage(engine, *execution, {.path = "missing.dng"}).hasValue());
}

TEST_F(RawCodecTest, StopAtFinalProgressDoesNotPublishOrRetainAFrame)
{
    struct StopSink final : an::ProgressSink
    {
        std::stop_source stop;
        void onProgress(const an::ProgressReport&) noexcept override
        {
            stop.request_stop();
        }
    } sink;
    const auto path = fixture();
    an::ExecutionOptions options;
    options.stopToken = sink.stop.get_token();
    options.progressSink = &sink;
    auto execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());
    const auto result = an::decodeImage(engine, *execution, {.path = path});
    EXPECT_TRUE(sink.stop.stop_requested());
    EXPECT_EQ(result.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(RawCodecTest, CapabilitiesAdvertiseOnlyImplementedOperations)
{
    auto codec = an::createRawCodec();
    ASSERT_TRUE(codec.hasValue());
    const auto capabilities = (*codec)->capabilities();
    EXPECT_TRUE(an::validCodecCapabilities(capabilities));
    EXPECT_EQ(capabilities.decodeSamples, an::codecSampleBit({2u, an::RawSampleClass::UnsignedInteger}));
    EXPECT_EQ(capabilities.encodeSamples, 0u);
    EXPECT_FALSE(capabilities.regionDecode);
    EXPECT_EQ(capabilities.maxChannels, 3u);
}

TEST_F(RawCodecTest, FullColorWhiteBalanceOptionsNeverAlterAMosaic)
{
    const auto path = fixture();
    std::vector<std::byte> reference;
    for (const auto mode : {an::RawFullColorWhiteBalance::None, an::RawFullColorWhiteBalance::Camera,
                            an::RawFullColorWhiteBalance::Daylight, an::RawFullColorWhiteBalance::Automatic,
                            an::RawFullColorWhiteBalance::Custom})
    {
        auto owner = makeEngine(0u, {.fullColorWhiteBalance = mode, .fullColorMultipliers = {2.0, 1.0, 3.0}});
        ASSERT_TRUE(owner.hasValue());
        auto execution = an::ExecutionContext::create(*owner, {});
        ASSERT_TRUE(execution.hasValue());
        auto result = an::decodeImage(*owner, *execution, {.path = path});
        ASSERT_TRUE(result.hasValue());
        const auto bytes = result->frame.bytes();
        if (reference.empty())
            reference.assign(bytes.begin(), bytes.end());
        EXPECT_EQ(std::memcmp(reference.data(), bytes.data(), bytes.size()), 0);
    }
}

TEST_F(RawCodecTest, EveryFullColorWhiteBalanceModeIsUsableAndCustomGainsAreApplied)
{
    const auto path = fixture(true);
    std::vector<std::byte> unbalanced;
    for (const auto mode : {an::RawFullColorWhiteBalance::None, an::RawFullColorWhiteBalance::Camera,
                            an::RawFullColorWhiteBalance::Daylight, an::RawFullColorWhiteBalance::Automatic,
                            an::RawFullColorWhiteBalance::Custom})
    {
        auto owner = makeEngine(0u, {.fullColorWhiteBalance = mode, .fullColorMultipliers = {2.0, 1.0, 3.0}});
        ASSERT_TRUE(owner.hasValue());
        auto execution = an::ExecutionContext::create(*owner, {});
        ASSERT_TRUE(execution.hasValue());
        auto result = an::decodeImage(*owner, *execution, {.path = path});
        ASSERT_TRUE(result.hasValue());
        const auto bytes = result->frame.bytes();
        if (mode == an::RawFullColorWhiteBalance::None)
            unbalanced.assign(bytes.begin(), bytes.end());
        if (mode == an::RawFullColorWhiteBalance::Custom)
            EXPECT_NE(std::memcmp(unbalanced.data(), bytes.data(), bytes.size()), 0);
    }
}

TEST_F(RawCodecTest, SourceCalibrationSurvivesAcquisitionRoundTripWithoutBecomingAnOutputIntent)
{
    const auto path = fixture();
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    auto result = an::decodeImage(engine, *execution, {.path = path});
    ASSERT_TRUE(result.hasValue());
    const auto& acquisition = result->metadata.acquisition();
    auto copied = an::AcquisitionMetadata::create(acquisition.toRequest());
    ASSERT_TRUE(copied.hasValue());
    EXPECT_EQ(*copied, acquisition);
    EXPECT_FALSE(result->metadata.outputIntent());
}

TEST_F(RawCodecTest, MetadataCanBeOmittedAndTwoContextsDecodeIndependently)
{
    const auto path = fixture();
    auto second = makeEngine();
    auto check = [&](an::EngineContext& owner) {
        auto execution = an::ExecutionContext::create(owner, {});
        auto result = an::decodeImage(owner, *execution, {.path = path, .readMetadata = false});
        EXPECT_TRUE(result.hasValue());
        if (result.hasValue())
            EXPECT_FALSE(result->metadata.acquisition().rawCamera());
    };
    std::thread other([&] { check(*second); });
    check(engine);
    other.join();
}

TEST(RawCameraMetadataTest, RejectsUnboundedTilesAndNonfiniteCalibration)
{
    an::RawCameraMetadata camera;
    EXPECT_TRUE(an::validateRawCameraMetadata(camera).ok());
    camera.blackPatternRows = 1;
    EXPECT_FALSE(an::validateRawCameraMetadata(camera).ok());
    camera.blackPatternRows = 0;
    camera.cameraToRgb[0][0] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(an::AcquisitionMetadata::create({.rawCamera = camera}).hasValue());
}
} // namespace
