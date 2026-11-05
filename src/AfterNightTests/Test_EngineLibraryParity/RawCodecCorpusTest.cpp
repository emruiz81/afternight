// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/RawCodec.h>

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4251 4275)
#endif
#include <libraw/libraw.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
/// Licensed host-local originals are compared against LibRaw.
class RawCodecCorpusTest : public testing::TestWithParam<const char*>
{
};

// Actual camera codes reach AMaZE with a stated white level, regardless of
// whether the caller retains UInt16 or explicitly chooses a Float32 working plane.
void checkAmazeHandoff(const an::ExecutionContext& execution, const an::CodecDecodeResult& decoded)
{
    constexpr unsigned side = 192u;
    constexpr unsigned count = side * side;
    const auto descriptor = *decoded.frame.descriptor();
    const auto width = descriptor.extent().width();
    const auto height = descriptor.extent().height();
    ASSERT_GE(width, side);
    ASSERT_GE(height, side);
    const auto x0 = ((width - side) / 2u) & ~1u;
    const auto y0 = ((height - side) / 2u) & ~1u;
    std::vector<std::uint16_t> codes(count), integerRgb(count * 3u);
    std::vector<float> working(count), floatRgb(count * 3u);
    for (unsigned y = 0u; y < side; ++y)
        for (unsigned x = 0u; x < side; ++x)
        {
            const auto i = y * side + x;
            std::memcpy(&codes[i], decoded.frame.bytes().data() + ((y + y0) * width + x + x0) * 2u, 2u);
            working[i] = static_cast<float>(codes[i]);
        }
    const auto layout = [](unsigned elementBytes, unsigned channels) {
        const auto extent = an::ImageExtent::create(side, side, channels, an::defaultEngineLimits());
        EXPECT_TRUE(extent.has_value());
        const auto result = an::ImageLayout::create({.extent = *extent,
                                                     .elementSizeBytes = elementBytes,
                                                     .samplesPerPixel = 1u,
                                                     .planeCount = channels,
                                                     .rowAlignmentBytes = 1u},
                                                    an::defaultEngineLimits());
        EXPECT_TRUE(result.has_value());
        return *result;
    };
    const auto& camera = *decoded.metadata.acquisition().rawCamera();
    ASSERT_GT(camera.whiteLevel, camera.blackLevel);
    const auto plan =
        an::planDemosaic({.method = an::DemosaicMethod::Amaze,
                          .domain = {.whiteLevel = static_cast<double>(camera.whiteLevel - camera.blackLevel)}},
                         descriptor.cfa());
    ASSERT_TRUE(plan.hasValue());
    const auto integer = an::demosaicImage(execution, an::SampleType::UInt16, codes.data(), layout(2, 1),
                                           integerRgb.data(), layout(2, 3), *plan);
    ASSERT_TRUE(integer.hasValue()) << integer.status().message();
    const auto floating = an::demosaicImage(execution, an::SampleType::Float32, working.data(), layout(4, 1),
                                            floatRgb.data(), layout(4, 3), *plan);
    ASSERT_TRUE(floating.hasValue()) << floating.status().message();
    EXPECT_TRUE(floating->readInPlace);
    EXPECT_FALSE(integer->readInPlace);
    for (unsigned i = 0u; i < count * 3u; ++i)
    {
        ASSERT_TRUE(std::isfinite(floatRgb[i]));
        const float clamped = (std::min)(65535.0f, (std::max)(0.0f, floatRgb[i]));
        ASSERT_LE(std::abs(static_cast<float>(integerRgb[i]) - clamped), 0.5f) << i;
    }
}

TEST_P(RawCodecCorpusTest, EverySampleMatchesTheIndependentDecoder)
{
#if defined(_MSC_VER)
    char* source = nullptr;
    std::size_t sourceBytes = 0u;
    ASSERT_EQ(_dupenv_s(&source, &sourceBytes, GetParam()), 0);
    const std::unique_ptr<char, decltype(&std::free)> sourceOwner(source, &std::free);
#else
    const char* source = std::getenv(GetParam());
#endif
    if (source == nullptr)
        GTEST_SKIP() << "Set " << GetParam() << " to the licensed original camera file";
    const auto native = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(source)));
    ASSERT_TRUE(std::filesystem::is_regular_file(native));
    LibRaw oracle;
    oracle.imgdata.params.use_fuji_rotate = 0;
    oracle.imgdata.params.user_flip = 0;
    oracle.imgdata.params.gamm[0] = oracle.imgdata.params.gamm[1] = 1;
    oracle.imgdata.params.no_auto_bright = 1;
    oracle.imgdata.params.output_bps = 16;
    oracle.imgdata.params.output_color = 1;
    for (float& gain : oracle.imgdata.params.user_mul)
        gain = 1.0f;
    ASSERT_EQ(oracle.open_file(native.c_str()), LIBRAW_SUCCESS);
    ASSERT_EQ(oracle.unpack(), LIBRAW_SUCCESS);
    const bool fullColor =
        oracle.imgdata.idata.is_foveon || oracle.imgdata.idata.filters == 0u || oracle.imgdata.idata.filters == 1u;
    const auto originalBlack = oracle.imgdata.color.black;
    const auto originalWhite = oracle.imgdata.color.maximum;
    std::array<unsigned, 4> originalChannels{};
    std::copy_n(oracle.imgdata.color.cblack, 4, originalChannels.begin());
    auto codec = an::createRawCodec();
    ASSERT_TRUE(codec.hasValue());
    std::array codecs{*codec};
    auto registry = an::CodecRegistry::create(codecs);
    ASSERT_TRUE(registry.hasValue());
    an::EngineContextOptions options;
    options.codecRegistry = *registry;
    auto engine = an::EngineContext::create(options);
    ASSERT_TRUE(engine.hasValue());
    auto execution = an::ExecutionContext::create(*engine, {});
    auto result = an::decodeImage(*engine, *execution, {.path = source, .format = an::CodecFormat::Raw});
    ASSERT_TRUE(result.hasValue()) << an::errorCodeName(result.status().code());
    ASSERT_TRUE(result->metadata.acquisition().rawCamera());
    const auto& camera = *result->metadata.acquisition().rawCamera();
    EXPECT_EQ(camera.blackLevel, originalBlack);
    EXPECT_EQ(camera.whiteLevel, originalWhite);
    for (unsigned c = 0; c < 4; ++c)
        EXPECT_EQ(camera.channelBlack[c], originalChannels[c]);
    if (std::string_view(GetParam()) == "AFTERNIGHT_TEST_RAW_CANON_CHANNEL_BLACK")
        EXPECT_NE(camera.channelBlack[0], camera.channelBlack[1]);
    const auto descriptor = *result->frame.descriptor();
    const auto width = descriptor.extent().width();
    const auto height = descriptor.extent().height();
    const auto pixels = static_cast<std::size_t>(width) * height;
    const auto* actual = reinterpret_cast<const std::uint16_t*>(result->frame.bytes().data());
    std::size_t different = 0u;
    if (fullColor)
    {
        ASSERT_EQ(oracle.dcraw_process(), LIBRAW_SUCCESS);
        int error = 0;
        std::unique_ptr<libraw_processed_image_t, decltype(&LibRaw::dcraw_clear_mem)> expected(
            oracle.dcraw_make_mem_image(&error), &LibRaw::dcraw_clear_mem);
        ASSERT_EQ(error, LIBRAW_SUCCESS);
        ASSERT_NE(expected, nullptr);
        ASSERT_EQ(expected->colors, 3u);
        ASSERT_EQ(expected->bits, 16u);
        ASSERT_EQ(expected->width, width);
        ASSERT_EQ(expected->height, height);
        for (std::size_t i = 0; i < pixels; ++i)
            for (unsigned c = 0; c < 3; ++c)
            {
                std::uint16_t value;
                std::memcpy(&value, expected->data + (3u * i + c) * 2u, 2u);
                different += actual[c * pixels + i] != value;
            }
        EXPECT_EQ(descriptor.colorSpace(), an::ColorSpace::LinearRgb);
        EXPECT_EQ(result->frame.layout()->planeCount(), 3u);
    }
    else
    {
        ASSERT_EQ(oracle.raw2image(), LIBRAW_SUCCESS);
        ASSERT_EQ(oracle.subtract_black(), LIBRAW_SUCCESS);
        ASSERT_EQ(oracle.imgdata.sizes.iwidth, width);
        ASSERT_EQ(oracle.imgdata.sizes.iheight, height);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x)
            {
                const auto i = static_cast<std::size_t>(y) * width + x;
                different +=
                    actual[i] != oracle.imgdata.image[i][oracle.COLOR(static_cast<int>(y), static_cast<int>(x))];
            }
        EXPECT_EQ(descriptor.numericDomain(), an::NumericDomain::RawSensorCode);
        if (oracle.imgdata.idata.filters == 9u)
        {
            EXPECT_EQ(descriptor.cfa().pattern(), an::CfaPattern::XTrans);
            for (unsigned y = 0; y < 6; ++y)
                for (unsigned x = 0; x < 6; ++x)
                {
                    const int value = oracle.imgdata.idata.xtrans[y][x];
                    const auto expected = value == 0   ? an::CfaColor::Red
                                          : value == 1 ? an::CfaColor::Green
                                                       : an::CfaColor::Blue;
                    EXPECT_EQ(descriptor.cfa().colorAt(x, y), expected);
                }
        }
    }
    EXPECT_EQ(different, 0u) << pixels << " pixels compared against LibRaw " << LibRaw::version();
    EXPECT_TRUE(result->frame.checkAdoptable().ok());
    if (descriptor.cfa().pattern() != an::CfaPattern::None && descriptor.cfa().pattern() != an::CfaPattern::XTrans)
        checkAmazeHandoff(*execution, *result);
}

INSTANTIATE_TEST_SUITE_P(LicensedCameras, RawCodecCorpusTest,
                         testing::Values("AFTERNIGHT_TEST_RAW_CANON", "AFTERNIGHT_TEST_RAW_NIKON",
                                         "AFTERNIGHT_TEST_RAW_XPRO1", "AFTERNIGHT_TEST_RAW_XT50",
                                         "AFTERNIGHT_TEST_RAW_FOVEON", "AFTERNIGHT_TEST_RAW_CANON_CHANNEL_BLACK"));
} // namespace
