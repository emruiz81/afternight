// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Interoperability tests for the XISF adapter: units another program wrote.
//
// Every unit read here was written by PixInsight from an image whose samples an expression gives
// (src/AfterNightTests/fixtures/common/xisf/PROVENANCE.md), so each decode is held to that expression
// rather than to a decoder - this engine's included. The units cover every sample format, every codec
// the format defines with and without byte shuffling, three checksum algorithms, a filter array, a unit
// of three images and the metadata PixInsight writes beside an image.
//
// The other direction is here too: one unit of each form this adapter writes, held to the form stated and kept, when
// asked, for PixInsight to read (`EveryFormTheWriterProducesIsWrittenAsStatedForAnotherReader`).

#include <AfterNightEngine/IO/XisfCodec.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Core/Version.h>

#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <openxisf/reader.h>
#include <openxisf/version.h>
#include <openxisf/writer.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace
{
/// The variable that names the directory of the units, when they are not found in the source tree.
constexpr const char* kUnitsVariable = "AFTERNIGHT_XISF_FIXTURES";

/// The variable that names a copy of OpenXISF's large unit, which is too large to keep in this tree.
constexpr const char* kLargeUnitVariable = "AFTERNIGHT_XISF_LARGE_UNIT";

/// Where the units are kept, relative to the repository root.
constexpr const char* kUnitsInTree = "src/AfterNightTests/fixtures/common/xisf";

std::string environmentText(const char* name)
{
#ifdef _WIN32
    char* stated = nullptr;
    std::size_t length = 0u;
    std::string value;
    if (_dupenv_s(&stated, &length, name) == 0 && stated != nullptr)
        value = stated;
    std::free(stated);
    return value;
#else
    const char* const stated = std::getenv(name);
    return stated != nullptr ? stated : "";
#endif
}

std::filesystem::path executableDirectory()
{
#ifdef _WIN32
    std::wstring buffer(32768u, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
#else
    std::error_code ignored;
    return std::filesystem::read_symlink("/proc/self/exe", ignored).parent_path();
#endif
}

/// The directory of the units: the variable when it is set, otherwise the source tree's found above the
/// executable or the working directory.
std::filesystem::path unitDirectory()
{
    if (const auto stated = environmentText(kUnitsVariable); !stated.empty())
        return stated;
    std::error_code ignored;
    for (const auto& start : {executableDirectory(), std::filesystem::current_path(ignored)})
        for (auto directory = start; !directory.empty(); directory = directory.parent_path())
        {
            const auto candidate = directory / kUnitsInTree;
            if (std::filesystem::is_directory(candidate, ignored))
                return candidate;
            if (directory == directory.parent_path())
                break;
        }
    return {};
}

std::string utf8(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

/// The expression an image's samples were made from.
enum class Pattern
{
    GroupA, ///< The 37 x 23 pattern image most of the units are made from.
    Cfa, ///< The 64 x 48 frame of the filter-array unit: (x + 64y)/3071.
    Third, ///< The third image of the multi-image unit, 11 x 7: (x + 11y)/76.
    Astrometry, ///< The 400 x 300 image of the astrometric units: (x + y)/1398.
};

/// The value in [0, 1] an expression gives channel `c` at (x, y).
double expected(Pattern pattern, std::uint32_t c, std::uint32_t x, std::uint32_t y)
{
    switch (pattern)
    {
    case Pattern::GroupA: {
        const double index = x + 37.0 * y;
        return c == 0u ? index / 850.0 : c == 1u ? 1.0 - index / 850.0 : c == 2u ? x / 36.0 : y / 22.0;
    }
    case Pattern::Cfa:
        return (x + 64.0 * y) / 3071.0;
    case Pattern::Third:
        return (x + 11.0 * y) / 76.0;
    case Pattern::Astrometry:
        return (x + y) / 1398.0;
    }
    return 0.0;
}

/// One image of one unit and what the procedure that wrote it says it holds.
struct Unit
{
    const char* file;
    std::uint32_t image;
    std::uint64_t images;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t channels;
    an::RawSampleFormat format;
    an::ChannelSchema schema;
    Pattern pattern;
};

constexpr an::RawSampleFormat kU8{1u, an::RawSampleClass::UnsignedInteger};
constexpr an::RawSampleFormat kU16{2u, an::RawSampleClass::UnsignedInteger};
constexpr an::RawSampleFormat kU32{4u, an::RawSampleClass::UnsignedInteger};
constexpr an::RawSampleFormat kF32{4u, an::RawSampleClass::FloatingPoint};
constexpr an::RawSampleFormat kF64{8u, an::RawSampleClass::FloatingPoint};

/// Every image of every unit.
const std::vector<Unit>& allUnits()
{
    using an::ChannelSchema;
    static const std::vector<Unit> all{
        {"a1-gray-u8.xisf", 0u, 1u, 37u, 23u, 1u, kU8, ChannelSchema::Mono, Pattern::GroupA},
        {"a2-gray-u16.xisf", 0u, 1u, 37u, 23u, 1u, kU16, ChannelSchema::Mono, Pattern::GroupA},
        {"a3-gray-u32.xisf", 0u, 1u, 37u, 23u, 1u, kU32, ChannelSchema::Mono, Pattern::GroupA},
        {"a4-rgb-f32.xisf", 0u, 1u, 37u, 23u, 3u, kF32, ChannelSchema::Rgb, Pattern::GroupA},
        {"a5-gray-f64.xisf", 0u, 1u, 37u, 23u, 1u, kF64, ChannelSchema::Mono, Pattern::GroupA},
        {"a6-rgba-u16.xisf", 0u, 1u, 37u, 23u, 4u, kU16, ChannelSchema::Rgba, Pattern::GroupA},
        {"b1-rgb-f32-zlib.xisf", 0u, 1u, 37u, 23u, 3u, kF32, ChannelSchema::Rgb, Pattern::GroupA},
        {"b2-rgb-f32-zlib-sh.xisf", 0u, 1u, 37u, 23u, 3u, kF32, ChannelSchema::Rgb, Pattern::GroupA},
        {"b3-rgb-f32-lz4.xisf", 0u, 1u, 37u, 23u, 3u, kF32, ChannelSchema::Rgb, Pattern::GroupA},
        {"b4-rgb-f32-lz4-sh.xisf", 0u, 1u, 37u, 23u, 3u, kF32, ChannelSchema::Rgb, Pattern::GroupA},
        {"b5-rgb-f32-lz4hc.xisf", 0u, 1u, 37u, 23u, 3u, kF32, ChannelSchema::Rgb, Pattern::GroupA},
        {"b6-rgb-f32-lz4hc-sh.xisf", 0u, 1u, 37u, 23u, 3u, kF32, ChannelSchema::Rgb, Pattern::GroupA},
        {"b7-rgb-f32-zstd.xisf", 0u, 1u, 37u, 23u, 3u, kF32, ChannelSchema::Rgb, Pattern::GroupA},
        {"b8-rgb-f32-zstd-sh.xisf", 0u, 1u, 37u, 23u, 3u, kF32, ChannelSchema::Rgb, Pattern::GroupA},
        {"b9-gray-u16-zstd-sh.xisf", 0u, 1u, 37u, 23u, 1u, kU16, ChannelSchema::Mono, Pattern::GroupA},
        {"b10-gray-f64-zlib-sh.xisf", 0u, 1u, 37u, 23u, 1u, kF64, ChannelSchema::Mono, Pattern::GroupA},
        {"c1-gray-u16-sha1.xisf", 0u, 1u, 37u, 23u, 1u, kU16, ChannelSchema::Mono, Pattern::GroupA},
        {"c2-gray-u16-sha256.xisf", 0u, 1u, 37u, 23u, 1u, kU16, ChannelSchema::Mono, Pattern::GroupA},
        {"c3-gray-u16-sha512.xisf", 0u, 1u, 37u, 23u, 1u, kU16, ChannelSchema::Mono, Pattern::GroupA},
        {"c6-gray-u16-zstd-sh-sha256.xisf", 0u, 1u, 37u, 23u, 1u, kU16, ChannelSchema::Mono, Pattern::GroupA},
        {"d1-rich-rgb.xisf", 0u, 1u, 37u, 23u, 3u, kU16, ChannelSchema::Rgb, Pattern::GroupA},
        {"d2-cfa-raw.xisf", 0u, 1u, 64u, 48u, 1u, kU16, ChannelSchema::Mono, Pattern::Cfa},
        {"d3-typed-properties.xisf", 0u, 1u, 37u, 23u, 1u, kU16, ChannelSchema::Mono, Pattern::GroupA},
        {"d4-multi-image.xisf", 0u, 3u, 37u, 23u, 1u, kU16, ChannelSchema::Mono, Pattern::GroupA},
        {"d4-multi-image.xisf", 1u, 3u, 37u, 23u, 3u, kF32, ChannelSchema::Rgb, Pattern::GroupA},
        {"d4-multi-image.xisf", 2u, 3u, 11u, 7u, 1u, kU8, ChannelSchema::Mono, Pattern::Third},
        {"d5-astrometry.xisf", 0u, 1u, 400u, 300u, 1u, kU16, ChannelSchema::Mono, Pattern::Astrometry},
        {"d5-astrometry-local.xisf", 0u, 1u, 400u, 300u, 1u, kU16, ChannelSchema::Mono, Pattern::Astrometry},
        {"d5-astrometry-multiquadric.xisf", 0u, 1u, 400u, 300u, 1u, kU16, ChannelSchema::Mono, Pattern::Astrometry},
    };
    return all;
}

/// The three plate-solved units.
constexpr std::array<const char*, 3u> kSolvedUnits = {"d5-astrometry.xisf", "d5-astrometry-local.xisf",
                                                      "d5-astrometry-multiquadric.xisf"};

/// One preserved field: a card of its keyword and the cards that continue its value, joined.
struct Field
{
    std::string value;
    std::string comment;
    std::size_t cards = 0u;
};

/// The first field of a keyword, its value joined across the cards that continue it.
std::optional<Field> fieldOf(const an::CodecMetadata& codec, an::CodecNamespace origin, std::string_view keyword)
{
    const auto cards = codec.cards();
    for (std::size_t index = 0u; index < cards.size(); ++index)
    {
        if (cards[index].origin() != origin || cards[index].keyword() != keyword)
            continue;
        Field field{cards[index].value(), cards[index].comment(), 1u};
        while (index + field.cards < cards.size() && cards[index + field.cards].origin() == origin &&
               cards[index + field.cards].keyword() == keyword && cards[index + field.cards].comment() == "continued")
            field.value += cards[index + field.cards++].value();
        return field;
    }
    return std::nullopt;
}

/// The numbers of a vector or matrix field's text, its separators either commas or semicolons.
std::vector<double> numbersOf(std::string_view text)
{
    std::vector<double> numbers;
    for (std::size_t at = 0u; at < text.size();)
    {
        const auto end = (std::min)(text.find_first_of(",;", at), text.size());
        double value = 0.0;
        std::from_chars(text.data() + at, text.data() + end, value);
        numbers.push_back(value);
        at = end + 1u;
    }
    return numbers;
}

/// The rows of one of PixInsight's tables, each split at its commas, its comment lines left out.
std::vector<std::vector<std::string>> tableOf(const std::filesystem::path& path)
{
    std::ifstream input(path);
    std::vector<std::vector<std::string>> rows;
    for (std::string line; std::getline(input, line);)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        std::vector<std::string> fields;
        for (std::size_t start = 0u;;)
        {
            const auto comma = line.find(',', start);
            fields.push_back(line.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
            if (comma == std::string::npos)
                break;
            start = comma + 1u;
        }
        rows.push_back(std::move(fields));
    }
    return rows;
}

/// A number of a table, in the full precision PixInsight wrote it with.
double number(const std::string& text)
{
    double value = 0.0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

/// The angle between two directions on the sky, in degrees.
double separationDegrees(const an::SkyCoordinate& left, const an::SkyCoordinate& right)
{
    constexpr double kRadians = std::numbers::pi / 180.0;
    const double dec = std::sin((right.declinationDegrees - left.declinationDegrees) * kRadians / 2.0);
    const double ra = std::sin((right.rightAscensionDegrees - left.rightAscensionDegrees) * kRadians / 2.0);
    const double h = dec * dec + std::cos(left.declinationDegrees * kRadians) *
                                     std::cos(right.declinationDegrees * kRadians) * ra * ra;
    return 2.0 * std::asin((std::min)(1.0, std::sqrt(h))) / kRadians;
}

/// A sample of a decoded frame as a number, read through the frame's own layout.
double sampleAt(const an::RawFrame& frame, std::uint32_t c, std::uint32_t x, std::uint32_t y)
{
    const auto layout = *frame.layout();
    const auto format = frame.descriptor()->sampleFormat();
    const bool planar = layout.planeCount() > 1u || layout.samplesPerPixel() == 1u;
    const auto plane = planar ? c : 0u;
    const auto within = planar ? x : x * layout.samplesPerPixel() + c;
    const auto* const at = frame.bytes().data() + plane * layout.planeStrideBytes() + y * layout.rowStrideBytes() +
                           within * format.sizeBytes;
    const auto read = [at]<typename T>(T) {
        T value;
        std::memcpy(&value, at, sizeof(T));
        return static_cast<double>(value);
    };
    if (format.sampleClass == an::RawSampleClass::FloatingPoint)
        return format.sizeBytes == 4u ? read(float{}) : read(double{});
    switch (format.sizeBytes)
    {
    case 1u:
        return read(std::uint8_t{});
    case 2u:
        return read(std::uint16_t{});
    default:
        return read(std::uint32_t{});
    }
}

/// How many samples of a frame differ from the expression beyond one code of their format, or one unit in the last
/// place of a floating point one. Integer samples are the writer's rounding of the values to the format's range;
/// a 32-bit integer sample is rounded from the 32-bit floating point value the writer computed. A frame decoded from a
/// region holds the expression over that region.
int differences(const an::RawFrame& frame, const Unit& unit, const an::CodecRegion* region = nullptr)
{
    const auto format = unit.format;
    const bool real = format.sampleClass == an::RawSampleClass::FloatingPoint;
    const double one = real ? 1.0 : std::ldexp(1.0, static_cast<int>(8u * format.sizeBytes)) - 1.0;
    const double tolerance = real ? (format.sizeBytes == 4u ? 1e-6 : 1e-12) : 1.0;
    const an::CodecRegion area = region != nullptr ? *region : an::CodecRegion{0u, 0u, unit.width, unit.height};
    int found = 0;
    for (std::uint32_t c = 0u; c < unit.channels; ++c)
        for (std::uint32_t y = 0u; y < area.height; ++y)
            for (std::uint32_t x = 0u; x < area.width; ++x)
            {
                const double exact = expected(unit.pattern, c, area.x + x, area.y + y);
                const double computed = !real && format.sizeBytes == 4u ? static_cast<float>(exact) : exact;
                const double value = sampleAt(frame, c, x, y);
                if (std::abs(value - computed * one) > tolerance && ++found <= 3)
                    ADD_FAILURE() << unit.file << "[" << unit.image << "] channel " << c << " at (" << x << ", " << y
                                  << ") is " << value << ", the expression gives " << computed * one;
            }
    return found;
}

class XisfInteropTest : public testing::Test
{
protected:
    void SetUp() override
    {
        units = unitDirectory();
        ASSERT_FALSE(units.empty()) << "no " << kUnitsInTree << " above the executable or the working directory; set "
                                    << kUnitsVariable;
        scratch = std::filesystem::temp_directory_path() /
                  ("an-xisf-interop-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(scratch);
        auto adapter = an::createXisfCodec();
        ASSERT_TRUE(adapter.hasValue());
        std::array adapters{*adapter};
        auto registry = an::CodecRegistry::create(adapters);
        ASSERT_TRUE(registry.hasValue());
        an::EngineContextOptions options;
        options.codecRegistry = *registry;
        auto created = an::EngineContext::create(options);
        ASSERT_TRUE(created.hasValue());
        engine = std::move(*created);
    }
    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(scratch, ignored);
    }
    /// Writes a frame with an intent into the scratch directory, and names the file.
    std::string write(const char* name, const an::RawFrame& frame, const an::ImageMetadata& metadata,
                      const an::OutputIntent& intent)
    {
        const auto target = utf8(scratch / name);
        auto execution = an::ExecutionContext::create(engine, {});
        if (!execution.hasValue())
        {
            ADD_FAILURE() << execution.status().message();
            return target;
        }
        auto saved =
            an::writeImage(engine, *execution, {.destinationPath = target, .intent = &intent}, frame, metadata);
        if (!saved.hasValue())
            ADD_FAILURE() << saved.status().message() << " / " << saved.status().rootCause().message();
        return target;
    }
    /// The XML header of a unit.
    static std::string headerOf(const std::string& path)
    {
        std::ifstream input(
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size())),
            std::ios::binary);
        std::array<char, 16u> signature{};
        input.read(signature.data(), signature.size());
        std::uint32_t length = 0u;
        std::memcpy(&length, signature.data() + 8u, sizeof(length));
        std::string header(length, '\0');
        input.read(header.data(), static_cast<std::streamsize>(length));
        return header;
    }
    std::string unit(const char* file) const
    {
        return utf8(units / file);
    }
    an::Result<an::CodecDecodeResult> decode(const std::string& path, std::uint32_t image = 0u, bool metadata = true)
    {
        auto execution = an::ExecutionContext::create(engine, {});
        if (!execution.hasValue())
            return execution.status();
        return an::decodeImage(engine, *execution, {.path = path, .imageIndex = image, .readMetadata = metadata});
    }
    an::Result<an::CodecImageInfo> probe(const std::string& path, bool metadata = true)
    {
        auto execution = an::ExecutionContext::create(engine, {});
        if (!execution.hasValue())
            return execution.status();
        return an::probeImage(engine, *execution, {.path = path, .readMetadata = metadata});
    }
    an::Result<std::uint64_t> workspace(const char* file)
    {
        auto execution = an::ExecutionContext::create(engine, {});
        if (!execution.hasValue())
            return execution.status();
        return an::xisfDecodeLibraryBytes(engine, *execution, unit(file));
    }
    /// A copy of a unit in the scratch directory, changed by `change`.
    template <typename Change> std::string copyOf(const char* file, const char* name, Change change)
    {
        std::ifstream input(units / file, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        change(bytes);
        const auto target = scratch / name;
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return utf8(target);
    }
    static const an::CodecCard* card(const an::CodecDecodeResult& decoded, an::CodecNamespace origin,
                                     std::string_view keyword)
    {
        return decoded.metadata.codec().find(origin, keyword);
    }
    std::filesystem::path units;
    std::filesystem::path scratch;
    an::EngineContext engine;
};

TEST_F(XisfInteropTest, EveryUnitDecodesToTheExpressionThatMadeIt)
{
    for (const Unit& expectedUnit : allUnits())
    {
        SCOPED_TRACE(std::string(expectedUnit.file) + " image " + std::to_string(expectedUnit.image));
        auto decoded = decode(unit(expectedUnit.file), expectedUnit.image, false);
        ASSERT_TRUE(decoded.hasValue()) << decoded.status().message() << " / "
                                        << decoded.status().rootCause().message();
        // The frame hands its descriptor over by value, so it is kept: a reference into the returned optional would
        // outlive it.
        const an::RawFrameDescriptor descriptor = *decoded->frame.descriptor();
        EXPECT_EQ(descriptor.extent().width(), expectedUnit.width);
        EXPECT_EQ(descriptor.extent().height(), expectedUnit.height);
        EXPECT_EQ(descriptor.extent().channelCount(), expectedUnit.channels);
        EXPECT_EQ(descriptor.sampleFormat(), expectedUnit.format);
        EXPECT_EQ(descriptor.channelSchema(), expectedUnit.schema);
        EXPECT_EQ(descriptor.byteOrder(), an::ByteOrder::Native);
        // PixInsight writes the planar storage model, and the decode keeps the file's arrangement.
        EXPECT_EQ(an::storageArrangementOf(*decoded->frame.layout()), an::StorageArrangement::Planar);
        EXPECT_EQ(differences(decoded->frame, expectedUnit), 0);
        auto described = probe(unit(expectedUnit.file), false);
        ASSERT_TRUE(described.hasValue()) << described.status().rootCause().message();
        EXPECT_EQ(described->imageCount, expectedUnit.images);
    }
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(XisfInteropTest, ARegionOfEveryUnitDecodesToTheExpressionOverTheRegion)
{
    // Every form PixInsight stored a unit in - each codec with and without shuffling, each checksum, the header's
    // own blocks - reads a region natively, and so does every image of the multi-image unit.
    for (const Unit& expectedUnit : allUnits())
    {
        SCOPED_TRACE(std::string(expectedUnit.file) + " image " + std::to_string(expectedUnit.image));
        const an::CodecRegion region{expectedUnit.width / 4u, expectedUnit.height / 3u, expectedUnit.width / 2u,
                                     expectedUnit.height / 3u};
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        auto decoded = an::decodeImageRegion(
            engine, *execution,
            {.path = unit(expectedUnit.file), .imageIndex = expectedUnit.image, .readMetadata = false}, region);
        ASSERT_TRUE(decoded.hasValue()) << decoded.status().rootCause().message();
        EXPECT_EQ(decoded->frame.descriptor()->extent().width(), region.width);
        EXPECT_EQ(decoded->frame.descriptor()->extent().height(), region.height);
        EXPECT_EQ(differences(decoded->frame, expectedUnit, &region), 0);
    }
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(XisfInteropTest, ARegionOfASolvedUnitCarriesItsOwnSolutionAndNotPixInsights)
{
    // A region of a plate-solved unit is solved by the whole image's solution moved to the region's origin, with the
    // distortion the whole solution states, and PixInsight's solution - which describes the whole image - stays
    // behind, so a save of the region states the region's solution alone.
    for (const char* const file : kSolvedUnits)
    {
        SCOPED_TRACE(file);
        auto whole = decode(unit(file));
        ASSERT_TRUE(whole.hasValue()) << whole.status().rootCause().message();
        const an::CodecRegion box{50u, 40u, 200u, 150u};
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        auto region = an::decodeImageRegion(engine, *execution, {unit(file)}, box);
        ASSERT_TRUE(region.hasValue()) << region.status().rootCause().message();
        const auto solution = an::parseWcsCards(whole->metadata.codec().cards(), 400u, 300u);
        ASSERT_TRUE(solution.hasValue());
        const auto cropped = an::cropWcsSolution(*solution, box.x, box.y, box.width, box.height);
        ASSERT_TRUE(cropped.hasValue());
        auto expected = cropped->toAstrometricSolution()->toRequest();
        ASSERT_TRUE(expected.has_value());
        expected->hasDistortion = whole->metadata.astrometry().hasDistortion().value_or(false);
        EXPECT_EQ(region->metadata.astrometry(), *an::AstrometricSolution::create(*expected));
        EXPECT_TRUE(region->metadata.astrometry().hasDistortion().value_or(false));
        for (const auto& card : region->metadata.codec().cards())
            EXPECT_FALSE(card.origin() == an::CodecNamespace::Xisf &&
                         (card.keyword().starts_with("AstrometricSolution:") ||
                          card.keyword().starts_with("PCL:AstrometricSolution:")))
                << card.keyword();
        // The rest of what PixInsight wrote stays the region's.
        EXPECT_NE(region->metadata.codec().find(an::CodecNamespace::Xisf, "XISF:CreatorApplication"), nullptr);
    }
}

TEST_F(XisfInteropTest, EveryUnitDescribesItselfWithWhatPixInsightWrote)
{
    for (const Unit& expectedUnit : allUnits())
    {
        SCOPED_TRACE(expectedUnit.file);
        // Every unit is read with its metadata whole, a property longer than one card - the typed unit's String of
        // 10,000 characters, the matrices of an astrometric solution - continued in the cards after it.
        auto decoded = decode(unit(expectedUnit.file), expectedUnit.image, true);
        ASSERT_TRUE(decoded.hasValue()) << decoded.status().message() << " / "
                                        << decoded.status().rootCause().message();
        const auto* const space = card(*decoded, an::CodecNamespace::Xisf, "ColorSpace");
        ASSERT_NE(space, nullptr);
        EXPECT_EQ(space->value(), expectedUnit.channels >= 3u ? "RGB" : "Gray");
        // The unit's own metadata says who wrote it.
        const auto* const creator = card(*decoded, an::CodecNamespace::Xisf, "XISF:CreatorApplication");
        ASSERT_NE(creator, nullptr);
        EXPECT_TRUE(creator->value().starts_with("PixInsight 1.9.")) << creator->value();
        // PixInsight states a range for a floating point image alone.
        const auto* const bounds = card(*decoded, an::CodecNamespace::Xisf, "Bounds");
        if (expectedUnit.format.sampleClass == an::RawSampleClass::FloatingPoint)
        {
            ASSERT_NE(bounds, nullptr);
            EXPECT_EQ(bounds->value(), "0:1");
        }
        else
            EXPECT_EQ(bounds, nullptr);
        EXPECT_EQ(decoded->frame.descriptor()->numericDomain(), an::NumericDomain::NormalizedLinear);
    }
}

TEST_F(XisfInteropTest, TheDeclarationsOfARichUnitTravelAsCards)
{
    auto decoded = decode(unit("d1-rich-rgb.xisf"));
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().rootCause().message();
    const auto fits = [&](std::string_view keyword) { return card(*decoded, an::CodecNamespace::Fits, keyword); };
    const auto xisf = [&](std::string_view keyword) { return card(*decoded, an::CodecNamespace::Xisf, keyword); };
    ASSERT_NE(fits("OBJECT"), nullptr);
    EXPECT_EQ(fits("OBJECT")->value(), "'M31'");
    ASSERT_NE(fits("HISTORY"), nullptr);
    EXPECT_TRUE(fits("HISTORY")->value().empty());
    EXPECT_FALSE(fits("HISTORY")->comment().empty());
    ASSERT_NE(fits("LONGSTR"), nullptr);
    EXPECT_EQ(fits("LONGSTR")->value().size(), 92u);
    EXPECT_EQ(decoded->metadata.acquisition().exposureSeconds(), 300.0);
    // The structural facts PixInsight writes as elements of the image.
    ASSERT_NE(xisf("Resolution"), nullptr);
    EXPECT_EQ(xisf("Resolution")->value(), "120;100;cm");
    ASSERT_NE(xisf("RGBWorkingSpace"), nullptr);
    EXPECT_TRUE(xisf("RGBWorkingSpace")->value().starts_with("2.2;0.6")) << xisf("RGBWorkingSpace")->value();
    ASSERT_NE(xisf("DisplayFunction"), nullptr);
    EXPECT_TRUE(xisf("DisplayFunction")->value().starts_with("0.25,")) << xisf("DisplayFunction")->value();
    // Properties as their exact values: a number in its shortest text, a TimePoint in ISO 8601, a vector's
    // elements separated by commas and a matrix's rows by semicolons.
    ASSERT_NE(xisf("Observation:Object:Name"), nullptr);
    EXPECT_EQ(xisf("Observation:Object:Name")->value(), "M31");
    ASSERT_NE(xisf("Instrument:ExposureTime"), nullptr);
    EXPECT_EQ(xisf("Instrument:ExposureTime")->value(), "300");
    ASSERT_NE(xisf("Instrument:Sensor:Temperature"), nullptr);
    EXPECT_EQ(xisf("Instrument:Sensor:Temperature")->value(), "-10");
    ASSERT_NE(xisf("Observation:Time:Start"), nullptr);
    EXPECT_TRUE(xisf("Observation:Time:Start")->value().starts_with("2026-09-20T23:15:00"));
    ASSERT_NE(xisf("Test:SmallVector"), nullptr);
    EXPECT_EQ(xisf("Test:SmallVector")->value(), "1,2,3,4");
    ASSERT_NE(xisf("Test:SmallMatrix"), nullptr);
    EXPECT_EQ(xisf("Test:SmallMatrix")->value(), "1,2;3,4");
    ASSERT_NE(xisf("Test:AttachedVector"), nullptr);
    EXPECT_TRUE(xisf("Test:AttachedVector")->value().starts_with("0,0.25,0.5,0.75,1,"))
        << xisf("Test:AttachedVector")->value();
}

TEST_F(XisfInteropTest, AFilterArrayAndItsNameTravelWithTheFrame)
{
    auto decoded = decode(unit("d2-cfa-raw.xisf"));
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().rootCause().message();
    const auto& cfa = decoded->frame.descriptor()->cfa();
    ASSERT_TRUE(cfa.isMosaiced());
    EXPECT_EQ(cfa.colorAt(0u, 0u), an::CfaColor::Red);
    EXPECT_EQ(cfa.colorAt(1u, 1u), an::CfaColor::Blue);
    const auto* const filters = card(*decoded, an::CodecNamespace::Xisf, "ColorFilterArray");
    ASSERT_NE(filters, nullptr);
    EXPECT_EQ(filters->value(), "2x2:RGGB;RGGB Bayer filter");
    const auto* const type = card(*decoded, an::CodecNamespace::Xisf, "ImageType");
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->value(), "Light");
    EXPECT_EQ(decoded->metadata.acquisition().exposureSeconds(), 120.0);
    ASSERT_NE(card(*decoded, an::CodecNamespace::Fits, "INSTRUME"), nullptr);
    EXPECT_EQ(card(*decoded, an::CodecNamespace::Fits, "INSTRUME")->value(), "'Synthetic CFA camera'");
}

TEST_F(XisfInteropTest, TheImagesOfOneUnitAreSelectedByIndexWithTheirOwnIdentifiers)
{
    const auto path = unit("d4-multi-image.xisf");
    const std::array<const char*, 3u> ids{"first", "second", "third"};
    const std::array<const char*, 3u> properties{"Test:First", "Test:Second", "Test:Third"};
    const std::array<const char*, 3u> values{"1", "second image", "3.5"};
    for (std::uint32_t image = 0u; image < 3u; ++image)
    {
        auto decoded = decode(path, image);
        ASSERT_TRUE(decoded.hasValue()) << image << ": " << decoded.status().rootCause().message();
        const auto* const id = card(*decoded, an::CodecNamespace::Xisf, "Id");
        ASSERT_NE(id, nullptr);
        EXPECT_EQ(id->value(), ids[image]);
        const auto* const property = card(*decoded, an::CodecNamespace::Xisf, properties[image]);
        ASSERT_NE(property, nullptr);
        EXPECT_EQ(property->value(), values[image]);
    }
    EXPECT_EQ(decode(path, 3u).status().code(), an::ErrorCode::OutOfRange);
}

TEST_F(XisfInteropTest, AChecksumIsVerifiedAndACorruptBlockIsRefused)
{
    // Each of the three checksummed units decoded to its pattern above. One Base64 digit changed inside the
    // embedded image keeps the header well formed and fails the block's SHA-256 digest.
    const auto corrupt = copyOf("c2-gray-u16-sha256.xisf", "corrupt.xisf", [](std::vector<char>& bytes) {
        const std::string_view text(bytes.data(), bytes.size());
        const auto data = text.find("<Data ");
        ASSERT_NE(data, std::string_view::npos);
        auto& digit = bytes[text.find('>', data) + 100u];
        digit = digit == 'A' ? 'B' : 'A';
    });
    auto refused = decode(corrupt);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().rootCause().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(an::codecFailureReason(refused.status().cause()), an::CodecFailure::PayloadUnreadable);
    // The image is still described: its header was read, and only its pixels are refused.
    EXPECT_TRUE(probe(corrupt).hasValue());
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(XisfInteropTest, ATruncatedCopyIsRefusedRatherThanReadPastItsEnd)
{
    // The image's block is followed by its ICC profile and thumbnail, so the copy ends halfway through the image's
    // own block, where its header says it is.
    const auto truncated = copyOf("a4-rgb-f32.xisf", "truncated.xisf", [](std::vector<char>& bytes) {
        const std::string_view text(bytes.data(), bytes.size());
        const auto attribute = text.find("location=\"attachment:", text.find("<Image "));
        ASSERT_NE(attribute, std::string_view::npos);
        const auto position = text.substr(attribute + 21u);
        const auto separator = position.find(':');
        const auto at = std::stoull(std::string(position.substr(0u, separator)));
        const auto size =
            std::stoull(std::string(position.substr(separator + 1u, position.find('"') - separator - 1u)));
        ASSERT_EQ(size, 37u * 23u * 3u * 4u);
        bytes.resize(static_cast<std::size_t>(at + size / 2u));
    });
    auto refused = decode(truncated, 0u, false);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().rootCause().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(an::codecFailureReason(refused.status().cause()), an::CodecFailure::PayloadUnreadable);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(XisfInteropTest, TheLibraryWorkspaceIsWhatTheBlockIsStoredAs)
{
    constexpr std::uint64_t kRgbBytes = 37u * 23u * 3u * 4u;
    // Attached and uncompressed: read straight into the frame.
    auto attached = workspace("a4-rgb-f32.xisf");
    ASSERT_TRUE(attached.hasValue()) << attached.status().message();
    EXPECT_EQ(*attached, 0u);
    // Embedded in the header, and so held by the reader: its stored bytes.
    auto embedded = workspace("a1-gray-u8.xisf");
    ASSERT_TRUE(embedded.hasValue());
    EXPECT_EQ(*embedded, 37u * 23u);
    // Compressed: the stored bytes, fewer than the image's; shuffled as well: the buffer it is unshuffled from too.
    auto compressed = workspace("b1-rgb-f32-zlib.xisf");
    ASSERT_TRUE(compressed.hasValue());
    EXPECT_GT(*compressed, 0u);
    EXPECT_LT(*compressed, kRgbBytes);
    auto shuffled = workspace("b2-rgb-f32-zlib-sh.xisf");
    ASSERT_TRUE(shuffled.hasValue());
    EXPECT_GT(*shuffled, kRgbBytes);
    EXPECT_LT(*shuffled, 2u * kRgbBytes);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(XisfInteropTest, AUnitPixInsightWroteIsWrittenBackAndReadTheSame)
{
    auto source = decode(unit("d2-cfa-raw.xisf"));
    ASSERT_TRUE(source.hasValue()) << source.status().rootCause().message();
    auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    ASSERT_TRUE(intent.hasValue());
    const auto copy = utf8(scratch / "copy.xisf");
    {
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        auto saved = an::writeImage(engine, *execution, {.destinationPath = copy, .intent = &*intent}, source->frame,
                                    source->metadata);
        ASSERT_TRUE(saved.hasValue()) << saved.status().message() << " / " << saved.status().rootCause().message();
    }
    auto again = decode(copy);
    ASSERT_TRUE(again.hasValue()) << again.status().rootCause().message();
    ASSERT_EQ(again->frame.bytes().size(), source->frame.bytes().size());
    EXPECT_EQ(std::memcmp(again->frame.bytes().data(), source->frame.bytes().data(), source->frame.bytes().size()), 0);
    EXPECT_TRUE(again->frame.descriptor()->cfa().isMosaiced());
    for (const char* keyword : {"ColorFilterArray", "ImageType", "Resolution"})
    {
        const auto* const before = card(*source, an::CodecNamespace::Xisf, keyword);
        const auto* const after = card(*again, an::CodecNamespace::Xisf, keyword);
        ASSERT_NE(before, nullptr) << keyword;
        ASSERT_NE(after, nullptr) << keyword;
        EXPECT_EQ(after->value(), before->value()) << keyword;
    }
    for (const char* keyword : {"INSTRUME", "EXPTIME", "XPIXSZ", "CCD-TEMP"})
    {
        const auto* const before = card(*source, an::CodecNamespace::Fits, keyword);
        const auto* const after = card(*again, an::CodecNamespace::Fits, keyword);
        ASSERT_NE(before, nullptr) << keyword;
        ASSERT_NE(after, nullptr) << keyword;
        EXPECT_EQ(after->value(), before->value()) << keyword;
    }
    // The unit's metadata is the writer's own, never the source's.
    const auto* const creator = card(*again, an::CodecNamespace::Xisf, "XISF:CreatorApplication");
    ASSERT_NE(creator, nullptr);
    EXPECT_TRUE(creator->value().starts_with("AfterNightEngine ")) << creator->value();
}

TEST_F(XisfInteropTest, EveryPropertyOfTheTypedUnitIsReadWholeWithItsType)
{
    auto decoded = decode(unit("d3-typed-properties.xisf"));
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().rootCause().message();
    const auto& codec = decoded->metadata.codec();
    const auto field = [&](std::string_view keyword) { return fieldOf(codec, an::CodecNamespace::Xisf, keyword); };
    // The String of 10,000 characters continues in the two cards after its first.
    std::string digits;
    for (int i = 0; i < 1000; ++i)
        digits += "0123456789";
    const auto longString = field("Test:LongString");
    ASSERT_TRUE(longString);
    EXPECT_EQ(longString->value, digits);
    EXPECT_EQ(longString->comment, "String");
    EXPECT_EQ(longString->cards, 3u);
    // The vector of 100,000 Float32 elements, i/2, each in its shortest exact text.
    std::string halves;
    for (int i = 0; i < 100000; ++i)
    {
        std::array<char, 32u> buffer{};
        const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), static_cast<float>(i * 0.5));
        if (i != 0)
            halves += ',';
        halves.append(buffer.data(), written.ptr);
    }
    const auto large = field("Test:LargeVector");
    ASSERT_TRUE(large);
    EXPECT_EQ(large->value, halves);
    EXPECT_EQ(large->comment, "F32Vector");
    EXPECT_GT(large->cards, 1u);
    // Every other kind of value PixInsight wrote, exact and with its type: the extremes of each integer width, the
    // largest finite floating point numbers, complex numbers, matrices with their dimensions, the empty ones, a
    // String of characters beyond ASCII.
    const std::vector<std::array<std::string_view, 3u>> expectations = {
        {"Test:Boolean", "true", "Boolean"},
        {"Test:Int8", "-128", "Int8"},
        {"Test:UInt8", "255", "UInt8"},
        {"Test:Int16", "-32768", "Int16"},
        {"Test:UInt16", "65535", "UInt16"},
        {"Test:Int32", "-2147483648", "Int32"},
        {"Test:UInt32", "4294967295", "UInt32"},
        {"Test:Int64", "-9007199254740991", "Int64"},
        {"Test:UInt64", "9007199254740991", "UInt64"},
        {"Test:Float32", "0.1", "Float32"},
        {"Test:Float64", "1e-300", "Float64"},
        {"Test:String",
         "  \xC3\x91"
         "and\xC3\xBA \xE2\x80\x94 \xE6\x98\x9F\xE9\x9B\xB2 \xE2\x9C\x93  ",
         "String"},
        {"Test:TimePoint", "2026-03-14T01:59:26.535Z", "TimePoint"},
        {"Test:I8Vector", "-128,-1,1,127", "I8Vector"},
        {"Test:UI8Vector", "0,0,1,255", "UI8Vector"},
        {"Test:I64Vector", "-9007199254740991,-1,1,9007199254740991", "I64Vector"},
        {"Test:UI64Vector", "0,0,1,9007199254740991", "UI64Vector"},
        {"Test:F32Vector", "-3.4028235e+38,-1,1,3.4028235e+38", "F32Vector"},
        {"Test:F64Vector", "-1.7976931348623157e+308,-1,1,1.7976931348623157e+308", "F64Vector"},
        {"Test:C32Vector", "(1,2),(-3,4)", "C32Vector"},
        {"Test:C64Vector", "(1,2),(-3,4)", "C64Vector"},
        {"Test:EmptyVector", "", "F64Vector"},
        {"Test:F64Matrix", "0,1,2,3;10,11,12,13;20,21,22,23", "F64Matrix 3x4"},
        {"Test:UI16Matrix", "1,2;3,4", "UI16Matrix 2x2"},
        {"Test:EmptyMatrix", "", "F64Matrix 0x0"},
    };
    for (const auto& [keyword, value, comment] : expectations)
    {
        const auto read = field(keyword);
        ASSERT_TRUE(read) << keyword;
        EXPECT_EQ(read->value, value) << keyword;
        EXPECT_EQ(read->comment, comment) << keyword;
        EXPECT_EQ(read->cards, 1u) << keyword;
    }
}

TEST_F(XisfInteropTest, TheTypedUnitIsWrittenBackWithEveryPropertyAsItsType)
{
    auto source = decode(unit("d3-typed-properties.xisf"));
    ASSERT_TRUE(source.hasValue()) << source.status().rootCause().message();
    auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    ASSERT_TRUE(intent.hasValue());
    auto again = decode(write("typed.xisf", source->frame, source->metadata, *intent));
    ASSERT_TRUE(again.hasValue()) << again.status().rootCause().message();
    // Every property but the unit's own metadata, which each writer states for itself, comes back with the same
    // text and the same type: the comment states it from the file the copy is.
    int compared = 0;
    const auto cards = source->metadata.codec().cards();
    for (std::size_t index = 0u; index < cards.size(); ++index)
    {
        const an::CodecCard& first = cards[index];
        if (first.origin() != an::CodecNamespace::Xisf || first.comment() == "continued" ||
            first.keyword().starts_with("XISF:"))
            continue;
        const auto before = fieldOf(source->metadata.codec(), an::CodecNamespace::Xisf, first.keyword());
        const auto after = fieldOf(again->metadata.codec(), an::CodecNamespace::Xisf, first.keyword());
        ASSERT_TRUE(after) << first.keyword();
        EXPECT_EQ(after->value, before->value) << first.keyword();
        EXPECT_EQ(after->comment, before->comment) << first.keyword();
        ++compared;
    }
    EXPECT_GE(compared, 31);
}

TEST_F(XisfInteropTest, PixInsightsSolutionIsTheImagesSolution)
{
    const auto reference = tableOf(units / "d5-reference.csv");
    ASSERT_FALSE(reference.empty());
    for (const char* file : kSolvedUnits)
    {
        SCOPED_TRACE(file);
        auto decoded = decode(unit(file));
        ASSERT_TRUE(decoded.hasValue()) << decoded.status().message() << " / "
                                        << decoded.status().rootCause().message();
        const auto& metadata = decoded->metadata;
        // Solved, the distortion PixInsight's splines model noted as there, which the linear cards do not carry.
        const auto& astrometry = metadata.astrometry();
        ASSERT_TRUE(astrometry.present());
        EXPECT_EQ(astrometry.hasDistortion(), true);
        EXPECT_NEAR(astrometry.pixelScaleArcsecPerPixel().value_or(0.0), 36.0, 0.1);
        // The world-coordinate cards are the solution's first layer exactly, each saying where it came from.
        const auto vector = [&](std::string_view keyword) {
            const auto read = fieldOf(metadata.codec(), an::CodecNamespace::Xisf, keyword);
            return read ? numbersOf(read->value) : std::vector<double>{};
        };
        const auto celestial = vector("AstrometricSolution:ReferenceCelestialCoordinates");
        const auto image = vector("AstrometricSolution:ReferenceImageCoordinates");
        const auto matrix = vector("AstrometricSolution:LinearTransformationMatrix");
        ASSERT_EQ(celestial.size(), 2u);
        ASSERT_EQ(image.size(), 2u);
        ASSERT_EQ(matrix.size(), 4u);
        auto solution = an::parseWcsCards(metadata.codec().cards(), 400u, 300u);
        ASSERT_TRUE(solution.hasValue()) << solution.status().message();
        EXPECT_EQ(solution->referencePoint().rightAscensionDegrees, celestial[0]);
        EXPECT_EQ(solution->referencePoint().declinationDegrees, celestial[1]);
        EXPECT_NEAR(solution->referencePixel().x, image[0], 1e-12);
        EXPECT_NEAR(solution->referencePixel().y, image[1], 1e-12);
        EXPECT_EQ(solution->linear().cd11, matrix[0]);
        EXPECT_EQ(solution->linear().cd12, matrix[1]);
        EXPECT_EQ(solution->linear().cd21, matrix[2]);
        EXPECT_EQ(solution->linear().cd22, matrix[3]);
        for (const an::CodecCard& entry : metadata.codec().cards())
            if (entry.origin() == an::CodecNamespace::Fits && an::isWcsKeyword(entry.keyword()))
                EXPECT_EQ(entry.comment(), "XISF AstrometricSolution") << entry.keyword();
        // Within the image the first layer is within PixInsight's distortion of what its whole solution gives -
        // the synthetic sky has a radial distortion and a wave of a few tenths of a pixel; the three units measure
        // 0.27 to 0.34 pixels.
        int compared = 0;
        double largest = 0.0;
        for (const auto& row : reference)
        {
            if (row.size() < 6u || row[0] != "image" || row[1] != file)
                continue;
            const double x = number(row[2]);
            const double y = number(row[3]);
            if (x < 0.0 || x > 400.0 || y < 0.0 || y > 300.0)
                continue;
            auto pixel =
                solution->worldToPixel({.rightAscensionDegrees = number(row[4]), .declinationDegrees = number(row[5])});
            ASSERT_TRUE(pixel.hasValue());
            largest = (std::max)(largest, std::hypot(pixel->x - x, pixel->y - y));
            ++compared;
        }
        EXPECT_GE(compared, 20);
        EXPECT_LT(largest, 0.5);
    }
}

TEST_F(XisfInteropTest, PixInsightsLinearSolutionsAreEvaluatedAsPixInsightEvaluatesThem)
{
    // Each of PixInsight's gnomonic references, the north celestial pole among them, written as a unit whose
    // properties state it as PixInsight states a solution, and read back: the engine's pixel to sky and sky to pixel
    // are PixInsight's to a few microarcseconds and a millionth of a pixel.
    const auto table = tableOf(units / "d5-projections.csv");
    auto carrier = decode(unit("d5-astrometry.xisf"), 0u, false);
    ASSERT_TRUE(carrier.hasValue());
    int solutions = 0;
    for (const auto& row : table)
    {
        if (row.size() < 11u || row[0] != "solution" || row[2] != "Gnomonic")
            continue;
        const std::string& name = row[1];
        SCOPED_TRACE(name);
        auto intent = an::OutputIntent::create(
            {.sampleType = an::SampleType::UInt16,
             .cards = {
                 *an::CodecCard::create(an::CodecNamespace::Xisf, "AstrometricSolution:Version", "1.0", "String"),
                 *an::CodecCard::create(an::CodecNamespace::Xisf, "AstrometricSolution:ProjectionSystem", "Gnomonic",
                                        "String"),
                 *an::CodecCard::create(an::CodecNamespace::Xisf, "AstrometricSolution:ReferenceCelestialCoordinates",
                                        row[3] + "," + row[4], "F64Vector"),
                 *an::CodecCard::create(an::CodecNamespace::Xisf, "AstrometricSolution:LinearTransformationMatrix",
                                        row[5] + "," + row[6] + ";" + row[7] + "," + row[8], "F64Matrix 2x2"),
                 *an::CodecCard::create(an::CodecNamespace::Xisf, "AstrometricSolution:ReferenceImageCoordinates",
                                        row[9] + "," + row[10], "F64Vector"),
                 *an::CodecCard::create(an::CodecNamespace::Xisf, "AstrometricSolution:CelestialPoleNativeCoordinates",
                                        "180,90", "F64Vector")}});
        ASSERT_TRUE(intent.hasValue()) << intent.status().message();
        auto decoded = decode(write((name + ".xisf").c_str(), carrier->frame, carrier->metadata, *intent));
        ASSERT_TRUE(decoded.hasValue()) << decoded.status().rootCause().message();
        ASSERT_TRUE(decoded->metadata.astrometry().present());
        EXPECT_EQ(decoded->metadata.astrometry().hasDistortion(), false);
        auto solution = an::parseWcsCards(decoded->metadata.codec().cards(), 400u, 300u);
        ASSERT_TRUE(solution.hasValue()) << solution.status().message();
        int points = 0;
        for (const auto& sample : table)
        {
            if (sample.size() < 6u || sample[1] != name || sample[4].empty())
                continue;
            if (sample[0] == "image")
            {
                auto sky = solution->pixelToWorld({.x = number(sample[2]), .y = number(sample[3])});
                ASSERT_TRUE(sky.hasValue()) << sample[2] << ", " << sample[3];
                EXPECT_LT(separationDegrees(*sky, {.rightAscensionDegrees = number(sample[4]),
                                                   .declinationDegrees = number(sample[5])}),
                          1e-9)
                    << sample[2] << ", " << sample[3];
                ++points;
            }
            else if (sample[0] == "celestial")
            {
                auto pixel = solution->worldToPixel(
                    {.rightAscensionDegrees = number(sample[2]), .declinationDegrees = number(sample[3])});
                ASSERT_TRUE(pixel.hasValue()) << sample[2] << ", " << sample[3];
                EXPECT_NEAR(pixel->x, number(sample[4]), 1e-6) << sample[2] << ", " << sample[3];
                EXPECT_NEAR(pixel->y, number(sample[5]), 1e-6) << sample[2] << ", " << sample[3];
                ++points;
            }
        }
        EXPECT_GT(points, 0);
        ++solutions;
    }
    EXPECT_EQ(solutions, 8);
}

TEST_F(XisfInteropTest, ASolutionOfTheNorthPoleThatStatesNoPoleIsReadAsTheSpecificationReadsIt)
{
    // Without its pole stated, the specification puts the celestial pole of a solution whose reference point is the
    // north celestial pole at native longitude 0, where PixInsight's own evaluation puts it at 180: the same solution
    // turned half way round the pole, so every point is PixInsight's own, 180 degrees further in right ascension.
    const auto table = tableOf(units / "d5-projections.csv");
    const auto solutionRow = std::find_if(table.begin(), table.end(), [](const auto& row) {
        return row.size() >= 11u && row[0] == "solution" && row[2] == "Gnomonic" && number(row[4]) == 90.0;
    });
    ASSERT_NE(solutionRow, table.end());
    const auto& row = *solutionRow;
    auto carrier = decode(unit("d5-astrometry.xisf"), 0u, false);
    ASSERT_TRUE(carrier.hasValue());
    auto intent = an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt16,
         .cards = {*an::CodecCard::create(an::CodecNamespace::Xisf, "AstrometricSolution:Version", "1.0", "String"),
                   *an::CodecCard::create(an::CodecNamespace::Xisf, "AstrometricSolution:ProjectionSystem", "Gnomonic",
                                          "String"),
                   *an::CodecCard::create(an::CodecNamespace::Xisf, "AstrometricSolution:ReferenceCelestialCoordinates",
                                          row[3] + "," + row[4], "F64Vector"),
                   *an::CodecCard::create(an::CodecNamespace::Xisf, "AstrometricSolution:LinearTransformationMatrix",
                                          row[5] + "," + row[6] + ";" + row[7] + "," + row[8], "F64Matrix 2x2"),
                   *an::CodecCard::create(an::CodecNamespace::Xisf, "AstrometricSolution:ReferenceImageCoordinates",
                                          row[9] + "," + row[10], "F64Vector")}});
    ASSERT_TRUE(intent.hasValue());
    auto decoded = decode(write("pole.xisf", carrier->frame, carrier->metadata, *intent));
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().rootCause().message();
    auto solution = an::parseWcsCards(decoded->metadata.codec().cards(), 400u, 300u);
    ASSERT_TRUE(solution.hasValue()) << solution.status().message();
    int points = 0;
    for (const auto& sample : table)
    {
        if (sample.size() < 6u || sample[0] != "image" || sample[1] != row[1] || sample[4].empty())
            continue;
        auto sky = solution->pixelToWorld({.x = number(sample[2]), .y = number(sample[3])});
        ASSERT_TRUE(sky.hasValue());
        const double turned = std::fmod(number(sample[4]) + 180.0, 360.0);
        EXPECT_LT(separationDegrees(*sky, {.rightAscensionDegrees = turned, .declinationDegrees = number(sample[5])}),
                  1e-9)
            << sample[2] << ", " << sample[3];
        ++points;
    }
    EXPECT_GT(points, 0);
}

TEST_F(XisfInteropTest, ASolutionTheEngineCannotHoldStaysInItsProperties)
{
    // PixInsight's stereographic references: the engine projects gnomonically alone, so the image is not solved, and
    // the properties that state the solution are kept for a writer that understands them.
    const auto table = tableOf(units / "d5-projections.csv");
    const auto solutionRow = std::find_if(table.begin(), table.end(), [](const auto& row) {
        return row.size() >= 11u && row[0] == "solution" && row[2] == "Stereographic";
    });
    ASSERT_NE(solutionRow, table.end());
    const auto& row = *solutionRow;
    auto carrier = decode(unit("d5-astrometry.xisf"), 0u, false);
    ASSERT_TRUE(carrier.hasValue());
    auto intent = an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt16,
         .cards = {*an::CodecCard::create(an::CodecNamespace::Xisf, "AstrometricSolution:ProjectionSystem",
                                          "Stereographic", "String"),
                   *an::CodecCard::create(an::CodecNamespace::Xisf, "AstrometricSolution:ReferenceCelestialCoordinates",
                                          row[3] + "," + row[4], "F64Vector"),
                   *an::CodecCard::create(an::CodecNamespace::Xisf, "AstrometricSolution:LinearTransformationMatrix",
                                          row[5] + "," + row[6] + ";" + row[7] + "," + row[8], "F64Matrix 2x2"),
                   *an::CodecCard::create(an::CodecNamespace::Xisf, "AstrometricSolution:ReferenceImageCoordinates",
                                          row[9] + "," + row[10], "F64Vector")}});
    ASSERT_TRUE(intent.hasValue());
    auto decoded = decode(write("stereographic.xisf", carrier->frame, carrier->metadata, *intent));
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().rootCause().message();
    EXPECT_FALSE(decoded->metadata.astrometry().present());
    EXPECT_EQ(an::checkWcsCards(decoded->metadata.codec().cards()), an::WcsCardError::NoSolution);
    const auto projection =
        fieldOf(decoded->metadata.codec(), an::CodecNamespace::Xisf, "AstrometricSolution:ProjectionSystem");
    ASSERT_TRUE(projection);
    EXPECT_EQ(projection->value, "Stereographic");
}

TEST_F(XisfInteropTest, ASolvedUnitIsWrittenBackAsPixInsightWroteIt)
{
    auto source = decode(unit("d5-astrometry-local.xisf"));
    ASSERT_TRUE(source.hasValue()) << source.status().rootCause().message();
    auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    ASSERT_TRUE(intent.hasValue());
    const auto copy = write("solved.xisf", source->frame, source->metadata, *intent);
    // The solution is stated once, as PixInsight stated it: in its properties, and in no keyword beside them.
    const auto header = headerOf(copy);
    EXPECT_EQ(header.find("CRVAL1"), std::string::npos);
    EXPECT_NE(header.find("AstrometricSolution:DistortionModel:ImageToProjection:Local:X:Nodes"), std::string::npos);
    auto again = decode(copy);
    ASSERT_TRUE(again.hasValue()) << again.status().rootCause().message();
    ASSERT_TRUE(again->metadata.astrometry().present());
    EXPECT_EQ(again->metadata.astrometry().hasDistortion(), true);
    EXPECT_EQ(*again->metadata.astrometry().referencePoint(), *source->metadata.astrometry().referencePoint());
    // Every property of the solution, PixInsight's own grids and generation parameters with XISF's, is the same
    // value of the same type.
    int compared = 0;
    const auto cards = source->metadata.codec().cards();
    for (std::size_t index = 0u; index < cards.size(); ++index)
    {
        const an::CodecCard& first = cards[index];
        if (first.origin() != an::CodecNamespace::Xisf || first.comment() == "continued" ||
            (!first.keyword().starts_with("AstrometricSolution:") &&
             !first.keyword().starts_with("PCL:AstrometricSolution:")))
            continue;
        const auto before = fieldOf(source->metadata.codec(), an::CodecNamespace::Xisf, first.keyword());
        const auto after = fieldOf(again->metadata.codec(), an::CodecNamespace::Xisf, first.keyword());
        ASSERT_TRUE(after) << first.keyword();
        EXPECT_EQ(after->value, before->value) << first.keyword();
        EXPECT_EQ(after->comment, before->comment) << first.keyword();
        ++compared;
    }
    EXPECT_EQ(compared, 69);
    // Once the solution is withdrawn - the frame cropped, solved again - neither its keywords nor PixInsight's
    // properties of it are left.
    auto withdrawn = an::withoutWcsCards(source->metadata.codec());
    ASSERT_TRUE(withdrawn.hasValue());
    EXPECT_EQ(an::checkWcsCards(withdrawn->cards()), an::WcsCardError::NoSolution);
    for (const an::CodecCard& entry : withdrawn->cards())
    {
        EXPECT_FALSE(entry.keyword().starts_with("AstrometricSolution:")) << entry.keyword();
        EXPECT_FALSE(entry.keyword().starts_with("PCL:AstrometricSolution:")) << entry.keyword();
    }
    EXPECT_NE(withdrawn->find(an::CodecNamespace::Xisf, "Observation:Center:RA"), nullptr);
    EXPECT_NE(withdrawn->find(an::CodecNamespace::Xisf, "PixInsight:ProcessingHistory"), nullptr);
}

TEST_F(XisfInteropTest, TheLargeUnitDecodesWhenACopyIsNamed)
{
    const auto named = environmentText(kLargeUnitVariable);
    if (named.empty())
        GTEST_SKIP() << "OpenXISF's large unit, 4.4 GB, is not kept in this tree; set " << kLargeUnitVariable
                     << " to a copy of tests/data/large/l1-rgb-f32-zlib.xisf to read it";
    auto decoded = decode(named, 0u, false);
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().message() << " / " << decoded.status().rootCause().message();
    const auto& extent = decoded->frame.descriptor()->extent();
    ASSERT_EQ(extent.width(), 22000u);
    ASSERT_EQ(extent.height(), 17000u);
    ASSERT_EQ(extent.channelCount(), 3u);
    // The expressions x/21999, y/16999 and (x + y)/38998, sampled across both subblocks: the first is stored as it
    // is, with equal sizes, and the second compressed.
    for (const auto [x, y] : std::array<std::array<std::uint32_t, 2u>, 5u>{
             {{0u, 0u}, {21999u, 0u}, {12345u, 6789u}, {0u, 16999u}, {21999u, 16999u}}})
    {
        EXPECT_NEAR(sampleAt(decoded->frame, 0u, x, y), x / 21999.0, 1e-6);
        EXPECT_NEAR(sampleAt(decoded->frame, 1u, x, y), y / 16999.0, 1e-6);
        EXPECT_NEAR(sampleAt(decoded->frame, 2u, x, y), (x + y) / 38998.0, 1e-6);
    }
}

// Units this adapter writes, for another reader to open.
//
// One unit of each form the writer produces - every sample format in one channel and three, planar and interleaved;
// every codec with and without byte shuffling in more than one subblock, and in the default subblocks of a frame
// larger than one; every checksum; reserved images whose rows were written in place; the cards of every property type,
// a filter array and astrometric solutions - and PixInsight's own units written again. The test holds each to the form
// stated and to the samples written, through this adapter and through OpenXISF's block facts. When
// AFTERNIGHT_XISF_WRITTEN_UNITS names a directory the units are kept there with `units.json`, the catalogue of what
// each holds, which `pixinsight/read_written_units.js` beside this file reads them against in PixInsight.

/// The variable that names a directory to keep the written units and their catalogue in.
constexpr const char* kWrittenUnitsVariable = "AFTERNIGHT_XISF_WRITTEN_UNITS";

/// The code of the written pattern at channel `c` of (x, y), before it is put in a format: a ramp along both axes,
/// different in each channel, which every codec compresses. The catalogue's reader computes it the same way.
std::uint64_t writtenCode(std::uint32_t c, std::uint32_t x, std::uint32_t y)
{
    return std::uint64_t{x} * 3u + std::uint64_t{y} * 5u + std::uint64_t{c} * 4099u + std::uint64_t{x} * y % 7u;
}

/// The sample of the written pattern in format `T` at channel `c` of (x, y) of a `width` by `height` image: the code
/// put in the format's range as a number the format holds exactly - an integer, or a binary fraction - the format's
/// least value at the first pixel and its greatest at the last.
template <typename T>
T writtenSample(std::uint32_t c, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height)
{
    const bool first = x == 0u && y == 0u;
    const bool last = x + 1u == width && y + 1u == height;
    const std::uint64_t code = writtenCode(c, x, y);
    const std::uint64_t narrow = code * 11u % 65536u;
    const std::uint64_t wide = (code * 65537u + 12345u) % (std::uint64_t{1u} << 32u);
    if constexpr (std::is_same_v<T, float>)
        return first ? 0.0f : last ? 1.0f : static_cast<float>(static_cast<double>(narrow) / 65536.0);
    else if constexpr (std::is_same_v<T, double>)
        return first ? 0.0 : last ? 1.0 : static_cast<double>(wide) / 4294967296.0;
    else if constexpr (sizeof(T) == 1u)
        return first ? T{0} : last ? T{255} : static_cast<T>(code % 256u);
    else if constexpr (sizeof(T) == 2u)
        return first ? T{0} : last ? T{65535} : static_cast<T>(narrow);
    else
        return first ? T{0} : last ? T{4294967295u} : static_cast<T>(wide);
}

/// The engine's sample type of a format.
template <typename T> constexpr an::SampleType sampleTypeOf()
{
    if constexpr (std::is_same_v<T, float>)
        return an::SampleType::Float32;
    else if constexpr (std::is_same_v<T, double>)
        return an::SampleType::Float64;
    else if constexpr (sizeof(T) == 1u)
        return an::SampleType::UInt8;
    else if constexpr (sizeof(T) == 2u)
        return an::SampleType::UInt16;
    else
        return an::SampleType::UInt32;
}

/// A frame of the written pattern in format `T`, stored as stated, behind the filter array stated.
template <typename T>
an::RawFrame writtenFrame(const an::EngineContext& engine, std::uint32_t width, std::uint32_t height,
                          std::uint32_t channels, an::StorageArrangement arrangement, an::CfaDescriptor cfa = {})
{
    const auto extent = *an::ImageExtent::create(width, height, channels, engine.limits());
    const an::RawSampleFormat format{sizeof(T), std::is_floating_point_v<T> ? an::RawSampleClass::FloatingPoint
                                                                            : an::RawSampleClass::UnsignedInteger};
    const auto descriptor = *an::RawFrameDescriptor::create(
        {extent, format, an::ByteOrder::Native, channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb,
         an::ColorSpace::Unspecified, an::NumericDomain::NormalizedLinear, cfa, an::CoordinateOrigin::TopLeft, 1.0});
    const bool planar = arrangement == an::StorageArrangement::Planar;
    const auto layout = *an::ImageLayout::create(
        {extent, sizeof(T), planar ? 1u : channels, planar ? channels : 1u, 1u}, engine.limits());
    auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
    auto raw = an::RawFrame::create(descriptor, layout, std::move(*buffer));
    auto bytes = *raw->mutableBytes();
    for (std::uint32_t c = 0u; c < channels; ++c)
        for (std::uint32_t y = 0u; y < height; ++y)
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const T value = writtenSample<T>(c, x, y, width, height);
                const std::size_t at =
                    planar ? c * layout.planeStrideBytes() + y * layout.rowStrideBytes() + std::size_t{x} * sizeof(T)
                           : y * layout.rowStrideBytes() + (std::size_t{x} * channels + c) * sizeof(T);
                std::memcpy(bytes.data() + at, &value, sizeof(T));
            }
    return std::move(*raw);
}

/// Text as a JSON string.
std::string jsonText(std::string_view text)
{
    constexpr std::string_view kHex = "0123456789abcdef";
    std::string quoted = "\"";
    for (const char c : text)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\')
            quoted += {'\\', c};
        else if (byte < 0x20u)
            quoted += std::string("\\u00") + kHex[byte >> 4u] + kHex[byte & 15u];
        else
            quoted += c;
    }
    return quoted + '"';
}

/// A number as JSON: its shortest exact decimal text.
std::string jsonNumber(double value)
{
    std::array<char, 32> buffer{};
    const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return {buffer.data(), written.ptr};
}

/// A card a written unit states, and the value a reader should find for it, as JSON.
struct StatedCard
{
    an::CodecNamespace origin;
    std::string keyword;
    std::string value;
    std::string comment;
    /// For a property: its XISF type and its value as JSON - a number, a string, `true` or `false`, an array of
    /// numbers, or `{"rows": r, "columns": c, "elements": [...]}`; a TimePoint is its ISO 8601 text.
    std::string type{};
    std::string json{};
};

/// The cards every authored unit carries: FITS keywords of each kind and properties naming the unit.
std::vector<StatedCard> commonCards(const std::string& file, int index)
{
    using an::CodecNamespace;
    return {
        {CodecNamespace::Fits, "OBJECT", "'NGC 3372'", "Name of the object"},
        {CodecNamespace::Fits, "EXPTIME", "120.5", "Exposure time in seconds"},
        {CodecNamespace::Fits, "HISTORY", "Written by the XISF interoperability test", ""},
        {CodecNamespace::Xisf, "Observation:Object:Name", "NGC 3372", "String", "String", jsonText("NGC 3372")},
        {CodecNamespace::Xisf, "Test:Unit", file, "String; the unit's own name", "String", jsonText(file)},
        {CodecNamespace::Xisf, "Test:Index", std::to_string(index), "Int32", "Int32", std::to_string(index)},
    };
}

/// The cards of the unit that states every kind of card: more keywords, a property of each type PixInsight reads, and
/// the structural facts an image states outside its properties.
std::vector<StatedCard> everyKindOfCard()
{
    using an::CodecNamespace;
    const std::string unicode = "\xc3\x91"
                                "and\xc3\xba \xe2\x80\x94 \xe6\x98\x9f\xe9\x9b\xb2";
    const std::string lines = "first line\nsecond line\tafter a tab";
    const std::string longest(68u, '7');
    return {
        {CodecNamespace::Fits, "DATE-OBS", "'2026-03-14T01:02:03.500'", "Start of the exposure"},
        {CodecNamespace::Fits, "GAIN", "120", "Sensor gain"},
        {CodecNamespace::Fits, "CCD-TEMP", "-10.25", "Sensor temperature in degrees C"},
        {CodecNamespace::Fits, "FLIPPED", "T", "A logical value"},
        {CodecNamespace::Fits, "CARD68", "'" + longest + "'", "The longest string of a FITS card"},
        {CodecNamespace::Fits, "COMMENT", "Written with every kind of card", ""},
        {CodecNamespace::Fits, "HISTORY", "A second history record", ""},
        {CodecNamespace::Xisf, "Test:Boolean", "true", "Boolean", "Boolean", "true"},
        {CodecNamespace::Xisf, "Test:Int8", "-128", "Int8", "Int8", "-128"},
        {CodecNamespace::Xisf, "Test:UInt8", "255", "UInt8", "UInt8", "255"},
        {CodecNamespace::Xisf, "Test:Int16", "-32768", "Int16", "Int16", "-32768"},
        {CodecNamespace::Xisf, "Test:UInt16", "65535", "UInt16", "UInt16", "65535"},
        {CodecNamespace::Xisf, "Test:Int32", "-2147483648", "Int32", "Int32", "-2147483648"},
        {CodecNamespace::Xisf, "Test:UInt32", "4294967295", "UInt32", "UInt32", "4294967295"},
        {CodecNamespace::Xisf, "Test:Int64", "-9007199254740991", "Int64", "Int64", "-9007199254740991"},
        {CodecNamespace::Xisf, "Test:UInt64", "9007199254740991", "UInt64", "UInt64", "9007199254740991"},
        {CodecNamespace::Xisf, "Test:Float32", "-10.25", "Float32", "Float32", "-10.25"},
        {CodecNamespace::Xisf, "Test:Float64", "1e-300", "Float64; the smallest of the unit", "Float64", "1e-300"},
        {CodecNamespace::Xisf, "Test:TimePoint", "2026-03-14T01:02:03.5Z", "TimePoint", "TimePoint",
         jsonText("2026-03-14T01:02:03.5Z")},
        {CodecNamespace::Xisf, "Test:F64Vector", "1.5,-2.25,1e-300", "F64Vector", "F64Vector", "[1.5,-2.25,1e-300]"},
        {CodecNamespace::Xisf, "Test:F32Vector", "0.5,0.25,-8", "F32Vector", "F32Vector", "[0.5,0.25,-8]"},
        {CodecNamespace::Xisf, "Test:I32Vector", "-1,0,2147483647", "I32Vector", "I32Vector", "[-1,0,2147483647]"},
        {CodecNamespace::Xisf, "Test:UI16Vector", "0,1,65535", "UI16Vector", "UI16Vector", "[0,1,65535]"},
        {CodecNamespace::Xisf, "Test:UI8Vector", "0,128,255", "UI8Vector", "UI8Vector", "[0,128,255]"},
        {CodecNamespace::Xisf, "Test:F64Matrix", "1,2,3;4,5,6.5", "F64Matrix 2x3", "F64Matrix",
         R"({"rows":2,"columns":3,"elements":[1,2,3,4,5,6.5]})"},
        {CodecNamespace::Xisf, "Test:UI16Matrix", "1,2;3,65535", "UI16Matrix 2x2", "UI16Matrix",
         R"({"rows":2,"columns":2,"elements":[1,2,3,65535]})"},
        {CodecNamespace::Xisf, "Test:Unicode", unicode, "String", "String", jsonText(unicode)},
        {CodecNamespace::Xisf, "Test:Lines", lines, "String; held in a data block", "String", jsonText(lines)},
        {CodecNamespace::Xisf, "Observation:Center:RA", "161.265", "Float64", "Float64", "161.265"},
        {CodecNamespace::Xisf, "Observation:Center:Dec", "-59.866", "Float64", "Float64", "-59.866"},
        {CodecNamespace::Xisf, "Instrument:ExposureTime", "120.5", "Float32", "Float32", "120.5"},
        {CodecNamespace::Xisf, "ImageType", "MasterFlat", ""},
        {CodecNamespace::Xisf, "Id", "afternight_every_card", ""},
        {CodecNamespace::Xisf, "Resolution", "120;100;cm", ""},
        {CodecNamespace::Xisf, "DisplayFunction",
         "0.25,0.3,0.35,0.5;0.01,0.02,0.03,0;0.9,0.95,1,1;0,0,0,0;1,1,1,1;stretch", ""},
    };
}

/// One unit the writer test writes.
struct WrittenUnit
{
    std::string file;
    an::SampleType type = an::SampleType::Float32;
    std::uint32_t width = 97u;
    std::uint32_t height = 61u;
    std::uint32_t channels = 3u;
    an::StorageArrangement arrangement = an::StorageArrangement::Planar;
    an::OutputCompression compression = an::OutputCompression::None;
    std::optional<bool> shuffle{};
    std::optional<std::uint64_t> subblockBytes{};
    an::OutputChecksum checksum = an::OutputChecksum::None;
    /// Written through a reservation, its rows then written in place last band first.
    bool reserved = false;
    an::CfaDescriptor cfa{};
    /// Whether the unit states every kind of card besides the common ones.
    bool everyCard = false;
    /// The solution the unit carries in its world-coordinate keywords.
    std::optional<an::WcsSolution> solution{};
    /// A unit of PixInsight's written again, its samples and cards taken from it, when this is one.
    const char* source = nullptr;
};

/// The name the catalogue gives a codec, as XISF spells it, or null.
std::string codecJson(const std::optional<openxisf::codec>& codec)
{
    return codec ? jsonText(openxisf::codec_name(*codec)) : "null";
}

/// The name the catalogue gives a checksum algorithm, as XISF spells it, or null.
std::string checksumJson(const std::optional<openxisf::checksum_algorithm>& checksum)
{
    if (!checksum)
        return "null";
    switch (*checksum)
    {
    case openxisf::checksum_algorithm::sha1:
        return jsonText("sha-1");
    case openxisf::checksum_algorithm::sha256:
        return jsonText("sha-256");
    case openxisf::checksum_algorithm::sha512:
        return jsonText("sha-512");
    case openxisf::checksum_algorithm::sha3_256:
        return jsonText("sha3-256");
    case openxisf::checksum_algorithm::sha3_512:
        return jsonText("sha3-512");
    }
    return "null";
}

/// The checksum algorithm the library writes for an intent's checksum.
std::optional<openxisf::checksum_algorithm> libraryChecksum(an::OutputChecksum checksum)
{
    switch (checksum)
    {
    case an::OutputChecksum::Sha1:
        return openxisf::checksum_algorithm::sha1;
    case an::OutputChecksum::Sha256:
        return openxisf::checksum_algorithm::sha256;
    case an::OutputChecksum::Sha512:
        return openxisf::checksum_algorithm::sha512;
    case an::OutputChecksum::Sha3Digest256:
        return openxisf::checksum_algorithm::sha3_256;
    case an::OutputChecksum::Sha3Digest512:
        return openxisf::checksum_algorithm::sha3_512;
    default:
        return std::nullopt;
    }
}

/// The codec the library writes for an intent's compression.
std::optional<openxisf::codec> libraryCodec(an::OutputCompression compression)
{
    switch (compression)
    {
    case an::OutputCompression::Deflate:
        return openxisf::codec::zlib;
    case an::OutputCompression::Lz4:
        return openxisf::codec::lz4;
    case an::OutputCompression::Lz4Hc:
        return openxisf::codec::lz4hc;
    case an::OutputCompression::Zstd:
        return openxisf::codec::zstd;
    default:
        return std::nullopt;
    }
}

/// The samples of a frame's bytes in storage order, its rows packed: what two frames of one pattern share.
std::vector<std::byte> packedSamples(const an::RawFrame& frame)
{
    const auto layout = *frame.layout();
    const std::size_t rowBytes =
        std::size_t{layout.extent().width()} * layout.samplesPerPixel() * layout.elementSizeBytes();
    std::vector<std::byte> packed;
    for (std::uint32_t plane = 0u; plane < layout.planeCount(); ++plane)
        for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
        {
            const auto* const row =
                frame.bytes().data() + plane * layout.planeStrideBytes() + y * layout.rowStrideBytes();
            packed.insert(packed.end(), row, row + rowBytes);
        }
    return packed;
}

TEST_F(XisfInteropTest, EveryFormTheWriterProducesIsWrittenAsStatedForAnotherReader)
{
    const auto kept = environmentText(kWrittenUnitsVariable);
    const std::filesystem::path directory = kept.empty() ? scratch / "written" : std::filesystem::path(kept);
    std::filesystem::create_directories(directory);

    using an::OutputChecksum;
    using an::OutputCompression;
    using an::SampleType;
    using an::StorageArrangement;
    std::vector<WrittenUnit> plan;
    // Every sample format, in one channel and in three stored both ways.
    for (const auto& [type, name] : std::array<std::pair<SampleType, const char*>, 5u>{{{SampleType::UInt8, "u8"},
                                                                                        {SampleType::UInt16, "u16"},
                                                                                        {SampleType::UInt32, "u32"},
                                                                                        {SampleType::Float32, "f32"},
                                                                                        {SampleType::Float64, "f64"}}})
    {
        plan.push_back({.file = std::string("a-") + name + "-gray.xisf", .type = type, .channels = 1u});
        plan.push_back({.file = std::string("a-") + name + "-rgb-planar.xisf", .type = type});
        plan.push_back({.file = std::string("a-") + name + "-rgb-normal.xisf",
                        .type = type,
                        .arrangement = StorageArrangement::Interleaved});
    }
    // Every codec, with and without byte shuffling, in subblocks of 16 KiB - five of a 97 x 61 RGB Float32 image - and
    // the default form of a compressed frame larger than its 4 MiB subblock.
    for (const auto& [compression, name] :
         std::array<std::pair<OutputCompression, const char*>, 4u>{{{OutputCompression::Deflate, "zlib"},
                                                                    {OutputCompression::Lz4, "lz4"},
                                                                    {OutputCompression::Lz4Hc, "lz4hc"},
                                                                    {OutputCompression::Zstd, "zstd"}}})
        for (const bool shuffle : {false, true})
            plan.push_back({.file = std::string("b-f32-rgb-") + name + (shuffle ? "-sh" : "") + ".xisf",
                            .compression = compression,
                            .shuffle = shuffle,
                            .subblockBytes = std::uint64_t{16384u}});
    plan.push_back({.file = "b-u16-rgb-zstd-default.xisf",
                    .type = SampleType::UInt16,
                    .width = 1100u,
                    .height = 1000u,
                    .compression = OutputCompression::Zstd});
    // Every checksum, and one beside a codec.
    for (const auto& [checksum, name] :
         std::array<std::pair<OutputChecksum, const char*>, 5u>{{{OutputChecksum::Sha1, "sha1"},
                                                                 {OutputChecksum::Sha256, "sha256"},
                                                                 {OutputChecksum::Sha512, "sha512"},
                                                                 {OutputChecksum::Sha3Digest256, "sha3-256"},
                                                                 {OutputChecksum::Sha3Digest512, "sha3-512"}}})
        plan.push_back({.file = std::string("c-u16-gray-") + name + ".xisf",
                        .type = SampleType::UInt16,
                        .channels = 1u,
                        .checksum = checksum});
    plan.push_back({.file = "c-f32-rgb-zstd-sh-sha3-512.xisf",
                    .compression = OutputCompression::Zstd,
                    .subblockBytes = std::uint64_t{16384u},
                    .checksum = OutputChecksum::Sha3Digest512});
    // Reserved images, their rows written in place and their checksum computed at the commit.
    plan.push_back(
        {.file = "d-f32-rgb-planar-reserved-sha256.xisf", .checksum = OutputChecksum::Sha256, .reserved = true});
    plan.push_back({.file = "d-u16-rgb-normal-reserved-sha3-512.xisf",
                    .type = SampleType::UInt16,
                    .arrangement = StorageArrangement::Interleaved,
                    .checksum = OutputChecksum::Sha3Digest512,
                    .reserved = true});
    plan.push_back({.file = "d-u8-gray-reserved.xisf", .type = SampleType::UInt8, .channels = 1u, .reserved = true});
    // Every kind of card, filter arrays and astrometric solutions.
    const auto linear =
        an::WcsSolution::create({.referencePixel = {.x = 48.5, .y = 32.25},
                                 .referencePoint = {.rightAscensionDegrees = 161.25, .declinationDegrees = -59.5},
                                 .linear = {.cd11 = -2.0e-4, .cd12 = 1.0e-5, .cd21 = 1.2e-5, .cd22 = 2.0e-4},
                                 .imageWidth = 96u,
                                 .imageHeight = 64u});
    ASSERT_TRUE(linear.hasValue()) << linear.status().message();
    an::SipPolynomial forwardX{.order = 2u};
    forwardX.coefficients[2][0] = 5.0e-5;
    forwardX.coefficients[1][1] = -3.0e-5;
    an::SipPolynomial forwardY{.order = 2u};
    forwardY.coefficients[0][2] = 4.0e-5;
    forwardY.coefficients[1][1] = 2.0e-5;
    const auto distortion = an::SipDistortion::create(
        {.referencePixel = {.x = 48.5, .y = 30.5}, .forwardX = forwardX, .forwardY = forwardY});
    ASSERT_TRUE(distortion.hasValue()) << distortion.status().message();
    const auto distorted =
        an::WcsSolution::create({.referencePixel = {.x = 48.5, .y = 30.5},
                                 .referencePoint = {.rightAscensionDegrees = 83.82, .declinationDegrees = -5.39},
                                 .linear = {.cd11 = 1.5e-4, .cd12 = -2.0e-5, .cd21 = -2.5e-5, .cd22 = -1.5e-4},
                                 .imageWidth = 97u,
                                 .imageHeight = 61u,
                                 .distortion = *distortion});
    ASSERT_TRUE(distorted.hasValue()) << distorted.status().message();
    // The same distortion with an inverse model beside it: the forward one negated, which inverts it to well within
    // a thousandth of a pixel over this image.
    an::SipPolynomial inverseX = forwardX;
    an::SipPolynomial inverseY = forwardY;
    for (auto* polynomial : {&inverseX, &inverseY})
        for (auto& row : polynomial->coefficients)
            for (double& coefficient : row)
                coefficient = -coefficient;
    const auto invertible = an::SipDistortion::create({.referencePixel = {.x = 48.5, .y = 30.5},
                                                       .forwardX = forwardX,
                                                       .forwardY = forwardY,
                                                       .inverseX = inverseX,
                                                       .inverseY = inverseY});
    ASSERT_TRUE(invertible.hasValue()) << invertible.status().message();
    auto withInverse = distorted->toRequest();
    withInverse.distortion = *invertible;
    const auto inverted = an::WcsSolution::create(withInverse);
    ASSERT_TRUE(inverted.hasValue()) << inverted.status().message();
    plan.push_back(
        {.file = "e-u16-gray-every-card.xisf", .type = SampleType::UInt16, .channels = 1u, .everyCard = true});
    plan.push_back({.file = "e-u16-cfa-rggb-solved.xisf",
                    .type = SampleType::UInt16,
                    .width = 96u,
                    .height = 64u,
                    .channels = 1u,
                    .cfa = *an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u),
                    .solution = *linear});
    plan.push_back({.file = "e-u16-cfa-rggb-phase-1-1.xisf",
                    .type = SampleType::UInt16,
                    .width = 96u,
                    .height = 64u,
                    .channels = 1u,
                    .cfa = *an::CfaDescriptor::create(an::CfaPattern::Rggb, 1u, 1u)});
    plan.push_back({.file = "e-f32-rgb-solved-sip.xisf", .solution = *distorted});
    plan.push_back({.file = "e-f32-rgb-solved-sip-inverse.xisf", .solution = *inverted});
    // PixInsight's units written again with their own cards, as an opened unit is saved.
    plan.push_back({.file = "r-d1-rich-rgb.xisf",
                    .compression = OutputCompression::Zstd,
                    .checksum = OutputChecksum::Sha256,
                    .source = "d1-rich-rgb.xisf"});
    for (const char* source : {"d2-cfa-raw.xisf", "d3-typed-properties.xisf", "d5-astrometry.xisf",
                               "d5-astrometry-local.xisf", "d5-astrometry-multiquadric.xisf"})
        plan.push_back({.file = std::string("r-") + source, .source = source});

    std::string catalogue = "{\n\"engine\": " + jsonText(an::kEngineVersionString) +
                            ",\n\"openxisf\": " + jsonText(OPENXISF_VERSION_STRING) + ",\n\"units\": [";
    for (std::size_t index = 0u; index < plan.size(); ++index)
    {
        const WrittenUnit& planned = plan[index];
        SCOPED_TRACE(planned.file);
        const auto target = utf8(directory / planned.file);
        std::error_code ignored;
        std::filesystem::remove(directory / planned.file, ignored);

        // The frame and the metadata written: the pattern, or the samples and cards of PixInsight's unit.
        std::optional<an::CodecDecodeResult> source;
        if (planned.source != nullptr)
        {
            auto decoded = decode(unit(planned.source));
            ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
            source = std::move(*decoded);
        }
        const auto frameOf = [&]<typename T>() {
            return writtenFrame<T>(engine, planned.width, planned.height, planned.channels, planned.arrangement,
                                   planned.cfa);
        };
        an::RawFrame frame;
        if (source)
            frame = std::move(source->frame);
        else if (planned.type == SampleType::UInt8)
            frame = frameOf.template operator()<std::uint8_t>();
        else if (planned.type == SampleType::UInt16)
            frame = frameOf.template operator()<std::uint16_t>();
        else if (planned.type == SampleType::UInt32)
            frame = frameOf.template operator()<std::uint32_t>();
        else if (planned.type == SampleType::Float32)
            frame = frameOf.template operator()<float>();
        else
            frame = frameOf.template operator()<double>();
        const auto descriptor = *frame.descriptor();
        const auto extent = descriptor.extent();
        const an::ImageMetadata metadata = source ? source->metadata : an::ImageMetadata{};

        std::vector<StatedCard> stated;
        if (!source)
        {
            stated = commonCards(planned.file, static_cast<int>(index));
            if (planned.everyCard)
                for (StatedCard& card : everyKindOfCard())
                    stated.push_back(std::move(card));
            if (planned.solution)
            {
                auto cards = an::buildWcsCards(*planned.solution);
                ASSERT_TRUE(cards.hasValue()) << cards.status().message();
                for (const an::CodecCard& card : *cards)
                    stated.push_back({card.origin(), std::string(card.keyword()), std::string(card.value()),
                                      std::string(card.comment())});
            }
        }
        std::vector<an::CodecCard> cards;
        for (const StatedCard& card : stated)
            cards.push_back(*an::CodecCard::create(card.origin, card.keyword, card.value, card.comment));
        const auto sampleType = source ? SampleType::UInt16 : planned.type;
        auto intent = an::OutputIntent::create({.sampleType = sampleType,
                                                .compression = planned.compression,
                                                .byteShuffle = planned.shuffle,
                                                .compressionBlockBytes = planned.subblockBytes,
                                                .checksum = planned.checksum,
                                                .cards = cards});
        ASSERT_TRUE(intent.hasValue()) << intent.status().message();

        // The write, whose execution ends before the unit is read back.
        if (auto execution = an::ExecutionContext::create(engine, {}); !execution.hasValue())
            FAIL() << execution.status().message();
        else if (planned.reserved)
        {
            auto transaction =
                an::OutputTransaction::begin(engine, *execution, {.destinationPath = target, .intent = &*intent});
            ASSERT_TRUE(transaction.hasValue()) << transaction.status().message();
            const an::Status reserved = transaction->reserve(descriptor, planned.arrangement, metadata);
            ASSERT_TRUE(reserved.ok()) << reserved.message() << " / " << reserved.rootCause().message();
            const auto whole = *frame.layout();
            // Bands of five rows, the last first, each described where it lies in the frame.
            for (std::uint32_t first = (extent.height() - 1u) / 5u * 5u;; first -= 5u)
            {
                const std::uint32_t rows = (std::min)(5u, extent.height() - first);
                const auto bandExtent =
                    *an::ImageExtent::create(extent.width(), rows, extent.channelCount(), engine.limits());
                const auto band = an::ImageLayout::createWithStrides({.extent = bandExtent,
                                                                      .elementSizeBytes = whole.elementSizeBytes(),
                                                                      .samplesPerPixel = whole.samplesPerPixel(),
                                                                      .planeCount = whole.planeCount(),
                                                                      .rowAlignmentBytes = 1u},
                                                                     whole.rowStrideBytes(), whole.planeStrideBytes(),
                                                                     engine.limits());
                ASSERT_TRUE(band.has_value());
                const an::Status written = transaction->writeRows(
                    first, *band, frame.bytes().subspan(static_cast<std::size_t>(first * whole.rowStrideBytes())));
                ASSERT_TRUE(written.ok()) << written.message() << " / " << written.rootCause().message();
                if (first == 0u)
                    break;
            }
            const auto report = transaction->commit();
            ASSERT_TRUE(report.hasValue())
                << report.status().message() << " / " << report.status().rootCause().message();
        }
        else
        {
            auto saved =
                an::writeImage(engine, *execution, {.destinationPath = target, .intent = &*intent}, frame, metadata);
            ASSERT_TRUE(saved.hasValue()) << saved.status().message() << " / " << saved.status().rootCause().message();
        }

        // Read back through this adapter: the samples written, in the arrangement written, and the cards stated.
        auto back = decode(target);
        ASSERT_TRUE(back.hasValue()) << back.status().message() << " / " << back.status().rootCause().message();
        EXPECT_EQ(packedSamples(back->frame), packedSamples(frame));
        EXPECT_EQ(back->frame.descriptor()->sampleFormat(), descriptor.sampleFormat());
        EXPECT_EQ(back->frame.descriptor()->cfa().isMosaiced(), descriptor.cfa().isMosaiced());
        for (const StatedCard& card : stated)
        {
            if (card.origin == an::CodecNamespace::Fits && (card.keyword == "HISTORY" || card.keyword == "COMMENT"))
                continue;
            const auto read = fieldOf(back->metadata.codec(), card.origin, card.keyword);
            ASSERT_TRUE(read) << card.keyword;
            EXPECT_EQ(read->value, card.value) << card.keyword;
        }
        if (planned.solution)
        {
            ASSERT_TRUE(back->metadata.astrometry().present());
        }

        // The block in the form stated, as OpenXISF describes it.
        const openxisf::reader oracle(target);
        const auto block = oracle.block(0u);
        const auto codec = libraryCodec(planned.compression);
        EXPECT_EQ(block.codec, codec);
        EXPECT_EQ(block.checksum, libraryChecksum(planned.checksum));
        if (codec)
        {
            const bool shuffled = planned.shuffle.value_or(an::sampleTypeSizeBytes(sampleType) > 1u);
            EXPECT_EQ(block.byte_shuffle, shuffled);
            const std::uint64_t subblock = planned.subblockBytes.value_or(std::uint64_t{4u} << 20u);
            EXPECT_EQ(block.subblock_size, (std::min)(subblock, block.uncompressed_size));
            EXPECT_EQ(block.subblock_count, (block.uncompressed_size + subblock - 1u) / subblock);
            EXPECT_LT(block.stored_size, block.uncompressed_size);
            if (!source)
                EXPECT_GT(block.subblock_count, 1u);
        }

        // The unit's entry in the catalogue.
        const auto& image = oracle.images().front();
        std::string entry = "\n{\"file\": " + jsonText(planned.file);
        entry += ", \"width\": " + std::to_string(extent.width()) + ", \"height\": " + std::to_string(extent.height()) +
                 ", \"channels\": " + std::to_string(extent.channelCount());
        entry += ", \"format\": " + jsonText(openxisf::sample_format_name(image.sample_format));
        entry += ", \"colorSpace\": " + jsonText(openxisf::color_space_name(image.color_space));
        entry += ", \"storage\": " + jsonText(openxisf::pixel_storage_name(image.pixel_storage));
        entry += ", \"imageType\": " +
                 (image.image_type ? jsonText(openxisf::image_type_name(*image.image_type)) : std::string("null"));
        entry += ", \"reserved\": " + std::string(planned.reserved ? "true" : "false");
        entry +=
            source ? ", \"pixels\": \"source\", \"source\": " + jsonText(planned.source) : ", \"pixels\": \"pattern\"";
        entry += ",\n \"block\": {\"codec\": " + codecJson(block.codec) +
                 ", \"shuffle\": " + (block.byte_shuffle ? "true" : "false") +
                 ", \"subblocks\": " + std::to_string(block.subblock_count) +
                 ", \"subblockSize\": " + std::to_string(block.subblock_size) +
                 ", \"checksum\": " + checksumJson(block.checksum) +
                 ", \"storedSize\": " + std::to_string(block.stored_size) +
                 ", \"uncompressedSize\": " + std::to_string(block.uncompressed_size) + "}";
        if (image.color_filter_array)
            entry += ",\n \"cfa\": {\"pattern\": " + jsonText(image.color_filter_array->pattern) +
                     ", \"width\": " + std::to_string(image.color_filter_array->width) +
                     ", \"height\": " + std::to_string(image.color_filter_array->height) +
                     ", \"name\": " + jsonText(image.color_filter_array->name) + "}";
        else
            entry += ",\n \"cfa\": null";
        std::string keywords;
        std::string properties;
        for (const StatedCard& card : stated)
        {
            if (card.origin == an::CodecNamespace::Fits)
            {
                // A commentary keyword's text is its comment, which is how XISF carries it.
                const bool commentary = card.keyword == "HISTORY" || card.keyword == "COMMENT";
                keywords += std::string(keywords.empty() ? "" : ",") + "\n  {\"name\": " + jsonText(card.keyword) +
                            ", \"value\": " + jsonText(commentary ? "" : card.value) +
                            ", \"comment\": " + jsonText(commentary ? card.value : card.comment) + "}";
            }
            else if (!card.type.empty())
                properties += std::string(properties.empty() ? "" : ",") + "\n  {\"id\": " + jsonText(card.keyword) +
                              ", \"type\": " + jsonText(card.type) + ", \"value\": " + card.json + "}";
        }
        entry += ",\n \"keywords\": [" + keywords + "]";
        entry += ",\n \"properties\": [" + properties + "]";
        entry += ",\n \"structure\": {";
        if (planned.everyCard)
            entry +=
                R"("id": "afternight_every_card", "resolution": [120, 100, "cm"], )"
                R"("displayFunction": [[0.25,0.3,0.35,0.5],[0.01,0.02,0.03,0],[0.9,0.95,1,1],[0,0,0,0],[1,1,1,1]])";
        entry += "}";
        // Where the solution the unit carries puts each of a grid of points, as this engine evaluates it.
        std::string sky;
        if (planned.solution)
            for (const double fx : {0.0, 0.25, 0.5, 0.75, 1.0})
                for (const double fy : {0.0, 0.25, 0.5, 0.75, 1.0})
                {
                    const an::PixelCoordinate pixel{.x = 0.5 + fx * (extent.width() - 1u),
                                                    .y = 0.5 + fy * (extent.height() - 1u)};
                    // And where its linear part alone puts it, which is what a reader that leaves the distortion
                    // polynomials out finds.
                    const auto world = planned.solution->pixelToWorld(pixel);
                    const auto linearWorld = planned.solution->withoutDistortion().pixelToWorld(pixel);
                    ASSERT_TRUE(world.hasValue());
                    ASSERT_TRUE(linearWorld.hasValue());
                    sky += std::string(sky.empty() ? "" : ",") + "[" + jsonNumber(pixel.x) + "," + jsonNumber(pixel.y) +
                           "," + jsonNumber(world->rightAscensionDegrees) + "," +
                           jsonNumber(world->declinationDegrees) + "," +
                           jsonNumber(linearWorld->rightAscensionDegrees) + "," +
                           jsonNumber(linearWorld->declinationDegrees) + "]";
                }
        entry += ",\n \"distortion\": " +
                 std::string(planned.solution && planned.solution->distortion().hasForwardModel() ? "true" : "false");
        entry += ",\n \"sky\": [" + sky + "]";
        entry += ", \"scale\": " +
                 jsonNumber(planned.solution ? planned.solution->meanPixelScaleArcsecPerPixel() / 3600.0 : 0.0) + "}";
        catalogue += (index == 0u ? "" : ",") + entry;
    }
    catalogue += "\n]\n}\n";
    std::ofstream output(directory / "units.json", std::ios::binary | std::ios::trunc);
    output << catalogue;
    EXPECT_TRUE(output.good());
}
} // namespace
