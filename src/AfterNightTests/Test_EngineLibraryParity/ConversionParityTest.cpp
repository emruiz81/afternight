// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Sample conversion against the imaging library's `cv::Mat::convertTo`, given the scale this engine's
// stated conversion policy implies rather than one that depends on the source type.
//
// Under corresponding full scales both sides compute `round(source * scale)` and saturate. The
// library forms the product in single precision when either side is single precision and rounds to
// nearest-even from there; this engine forms it in double. A product that lands within a float's
// rounding of a half-code boundary can therefore round differently, by one code, and the sweep
// allows exactly that while counting how often it happens. Between unsigned integers the scale is a
// whole number and the two agree bit for bit.

#include "ParityHarness.h"

#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{

using namespace parity_test;

constexpr const char* kDomain = "conversion";

struct TypePair
{
    an::SampleType engine;
    int library;
    double fullScale;
    const char* name;
};

constexpr std::array<TypePair, 4u> kTypes{{
    {an::SampleType::UInt8, CV_8UC1, 255.0, "u8"},
    {an::SampleType::UInt16, CV_16UC1, 65535.0, "u16"},
    {an::SampleType::Float32, CV_32FC1, 1.0, "f32"},
    {an::SampleType::Float64, CV_64FC1, 1.0, "f64"},
}};

/// A source plane of the named representation: whole-range codes for the integers, and for the
/// floats a range that overruns both ends and lands on half-code boundaries often.
[[nodiscard]] std::vector<std::uint8_t> sourceBytes(const TypePair& type, PlaneSize size, std::uint64_t seed)
{
    Sequence sequence{seed};
    const std::size_t count = static_cast<std::size_t>(size.width) * size.height;
    std::vector<std::uint8_t> bytes(count * an::sampleTypeSizeBytes(type.engine), 0u);
    for (std::size_t index = 0u; index < count; ++index)
    {
        const double u = sequence.nextDouble();
        switch (type.engine)
        {
        case an::SampleType::UInt8:
            bytes[index] = static_cast<std::uint8_t>(u * 256.0);
            break;
        case an::SampleType::UInt16: {
            const auto code = static_cast<std::uint16_t>(u * 65536.0);
            std::memcpy(bytes.data() + index * 2u, &code, 2u);
            break;
        }
        case an::SampleType::Float32: {
            // Every fourth sample sits on a half-code boundary of the 16-bit scale.
            const float value = index % 4u == 0u ? static_cast<float>((std::floor(u * 65535.0) + 0.5) / 65535.0)
                                                 : static_cast<float>(u * 1.2 - 0.1);
            std::memcpy(bytes.data() + index * 4u, &value, 4u);
            break;
        }
        default: {
            const double value = index % 4u == 0u ? (std::floor(u * 255.0) + 0.5) / 255.0 : u * 1.2 - 0.1;
            std::memcpy(bytes.data() + index * 8u, &value, 8u);
            break;
        }
        }
    }
    return bytes;
}

[[nodiscard]] float sampleAsFloat(const std::uint8_t* bytes, an::SampleType type, std::size_t index)
{
    switch (type)
    {
    case an::SampleType::UInt8:
        return static_cast<float>(bytes[index]);
    case an::SampleType::UInt16: {
        std::uint16_t code = 0u;
        std::memcpy(&code, bytes + index * 2u, 2u);
        return static_cast<float>(code);
    }
    case an::SampleType::Float32: {
        float value = 0.0f;
        std::memcpy(&value, bytes + index * 4u, 4u);
        return value;
    }
    default: {
        double value = 0.0;
        std::memcpy(&value, bytes + index * 8u, 8u);
        return static_cast<float>(value);
    }
    }
}

[[nodiscard]] float libraryAsFloat(const cv::Mat& matrix, int y, int x)
{
    switch (matrix.depth())
    {
    case CV_8U:
        return static_cast<float>(matrix.at<std::uint8_t>(y, x));
    case CV_16U:
        return static_cast<float>(matrix.at<std::uint16_t>(y, x));
    case CV_32F:
        return matrix.at<float>(y, x);
    default:
        return static_cast<float>(matrix.at<double>(y, x));
    }
}

TEST(ConversionParityTest, FullScaleConversionsAgreeWithTheLibraryToOneCodeAtAHalfCodeBoundary)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::array<PlaneSize, 3u> kSizes{{{7u, 5u}, {64u, 48u}, {256u, 192u}}};
    for (const TypePair& from : kTypes)
    {
        for (const TypePair& to : kTypes)
        {
            if (from.engine == to.engine)
            {
                continue;
            }
            const bool integerDestination = to.engine == an::SampleType::UInt8 || to.engine == an::SampleType::UInt16;
            const bool integerSource = from.engine == an::SampleType::UInt8 || from.engine == an::SampleType::UInt16;
            for (const PlaneSize size : kSizes)
            {
                const std::vector<std::uint8_t> source = sourceBytes(from, size, 14000u + size.width);
                const an::ImageLayout sourceLayout = planarLayout(from.engine, size.width, size.height);
                const an::ImageLayout destinationLayout = planarLayout(to.engine, size.width, size.height);
                const std::size_t count = static_cast<std::size_t>(size.width) * size.height;
                std::vector<std::uint8_t> ours(count * an::sampleTypeSizeBytes(to.engine), 0u);
                an::ConversionCounters counters{};
                const an::Status ran =
                    an::convertImageSamples(execution, from.engine, source.data(), sourceLayout, to.engine, ours.data(),
                                            destinationLayout, an::ConversionPolicy{}, counters);
                ASSERT_TRUE(ran.ok()) << ran.message();

                cv::Mat input(static_cast<int>(size.height), static_cast<int>(size.width), from.library);
                for (std::uint32_t y = 0u; y < size.height; ++y)
                {
                    std::memcpy(input.ptr(static_cast<int>(y)),
                                source.data() +
                                    static_cast<std::size_t>(y) * size.width * an::sampleTypeSizeBytes(from.engine),
                                static_cast<std::size_t>(size.width) * an::sampleTypeSizeBytes(from.engine));
                }
                cv::Mat output;
                input.convertTo(output, to.library, to.fullScale / from.fullScale, 0.0);

                std::vector<float> oursAsFloat(count, 0.0f);
                std::vector<float> theirsAsFloat(count, 0.0f);
                for (std::uint32_t y = 0u; y < size.height; ++y)
                {
                    for (std::uint32_t x = 0u; x < size.width; ++x)
                    {
                        const std::size_t index = static_cast<std::size_t>(y) * size.width + x;
                        oursAsFloat[index] = sampleAsFloat(ours.data(), to.engine, index);
                        theirsAsFloat[index] = libraryAsFloat(output, static_cast<int>(y), static_cast<int>(x));
                    }
                }
                // Whole-number scales between integers agree bit for bit; a single-precision
                // product may round across a half code; a float destination agrees to its rounding.
                const bool wholeScale = integerSource && integerDestination;
                const double tolerance = wholeScale ? 0.0 : (integerDestination ? 1.0 : 0.0);
                const double relative = integerDestination ? 0.0 : 2.0e-7;
                const Comparison found =
                    expectParity(kDomain, std::string(from.name) + "->" + to.name + " " + sizeLabel(size), oursAsFloat,
                                 theirsAsFloat, tolerance, relative);
                if (integerDestination && !wholeScale)
                {
                    std::size_t differing = 0u;
                    for (std::size_t index = 0u; index < count; ++index)
                    {
                        differing += oursAsFloat[index] != theirsAsFloat[index] ? 1u : 0u;
                    }
                    std::cout << "parity | " << kDomain << " | " << from.name << "->" << to.name << " "
                              << sizeLabel(size) << " | half-code disagreements: " << differing << " of " << found.count
                              << "\n";
                }
            }
        }
    }
}

} // namespace
