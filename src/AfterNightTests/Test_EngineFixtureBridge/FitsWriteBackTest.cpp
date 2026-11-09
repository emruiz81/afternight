// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The FITS files checked in beside the raw-dump record, written back through the FITS codec.
//
// The engine wrote those inputs while CFITSIO wrote its FITS files, and OpenFITS writes them now. What a FITS file
// stores of an image - its samples in big-endian order, the unsigned and signed offsets, the planes one after another,
// the fill of the last block - is the container's and not the library's, so an input decoded and written back in the
// representation it holds keeps its data unit byte for byte, whichever library wrote it first. Its header is the
// writer's own, and is not compared.

#include "RawDumpExportRecord.h"

#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{

constexpr std::size_t kRecordBytes = 80u;
constexpr std::size_t kBlockBytes = 2880u;

/// The inputs a decode refuses, as the raw-dump record states them: one is no FITS file and one is cut short.
constexpr std::array<std::string_view, 2u> kRefusedInputs{"not-an-image.fits", "truncated-fits.fits"};

std::vector<char> readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

/// What follows a file's primary header: the data unit, the fill of its last block, and anything after them. The
/// header ends with the block its END record is in, wherever that is.
std::optional<std::string_view> afterPrimaryHeader(const std::vector<char>& bytes)
{
    for (std::size_t offset = 0u; offset + kRecordBytes <= bytes.size(); offset += kRecordBytes)
    {
        const std::string_view record(bytes.data() + offset, kRecordBytes);
        if (record.substr(0u, 3u) == "END" && record.substr(3u, 5u).find_first_not_of(' ') == std::string_view::npos)
        {
            const std::size_t end = (offset / kBlockBytes + 1u) * kBlockBytes;
            if (end > bytes.size())
                return std::nullopt;
            return std::string_view(bytes.data() + end, bytes.size() - end);
        }
    }
    return std::nullopt;
}

class FitsWriteBackTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_scratch =
            std::filesystem::temp_directory_path() /
            ("an-fits-write-back-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(m_scratch);
        const auto adapter = an::createFitsCodec();
        ASSERT_TRUE(adapter.hasValue()) << adapter.status().message();
        const std::array adapters{*adapter};
        auto registry = an::CodecRegistry::create(adapters);
        ASSERT_TRUE(registry.hasValue()) << registry.status().message();
        an::EngineContextOptions options;
        options.codecRegistry = std::move(*registry);
        auto engine = an::EngineContext::create(options);
        ASSERT_TRUE(engine.hasValue()) << engine.status().message();
        m_engine = std::move(*engine);
    }

    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_scratch, ignored);
    }

    std::filesystem::path m_scratch;
    an::EngineContext m_engine;
};

TEST_F(FitsWriteBackTest, EveryCheckedInImageKeepsItsDataUnitWhenWrittenBack)
{
    std::string error;
    const std::optional<std::filesystem::path> expectations = an::bench::locateExpectationDirectory(error);
    ASSERT_TRUE(expectations.has_value()) << error;
    const std::filesystem::path inputs = *expectations / an::bench::kRawDumpExportInputDirectory;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(inputs))
    {
        std::string extension = utf8(entry.path().extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".fits" || extension == ".fit" || extension == ".fts")
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    std::size_t compared = 0u;
    for (const std::filesystem::path& input : files)
    {
        const std::string name = utf8(input.filename());
        SCOPED_TRACE(name);
        auto reading = an::ExecutionContext::create(m_engine, {});
        ASSERT_TRUE(reading.hasValue());
        auto decoded = an::decodeImage(m_engine, *reading, {utf8(input)});
        if (std::find(kRefusedInputs.begin(), kRefusedInputs.end(), name) != kRefusedInputs.end())
        {
            EXPECT_FALSE(decoded.hasValue());
            continue;
        }
        ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
        // The engine grants one execution its whole width at a time.
        *reading = an::ExecutionContext{};
        const auto type = decoded->frame.descriptor()->residentSampleType();
        ASSERT_TRUE(type.has_value());
        const auto intent = an::OutputIntent::create({.sampleType = *type});
        ASSERT_TRUE(intent.hasValue()) << intent.status().message();
        auto writing = an::ExecutionContext::create(m_engine, {});
        ASSERT_TRUE(writing.hasValue());
        const std::filesystem::path destination = m_scratch / input.filename();
        const auto written =
            an::writeImage(m_engine, *writing, {.destinationPath = utf8(destination), .intent = &*intent},
                           decoded->frame, decoded->metadata);
        ASSERT_TRUE(written.hasValue()) << written.status().message();

        const std::vector<char> before = readFile(input);
        const std::vector<char> after = readFile(destination);
        const std::optional<std::string_view> storedBefore = afterPrimaryHeader(before);
        const std::optional<std::string_view> storedAfter = afterPrimaryHeader(after);
        ASSERT_TRUE(storedBefore.has_value());
        ASSERT_TRUE(storedAfter.has_value());
        EXPECT_EQ(storedBefore->size(), storedAfter->size());
        EXPECT_TRUE(*storedBefore == *storedAfter) << "the data unit written back differs from the checked-in one";
        ++compared;
    }
    EXPECT_GE(compared, 10u) << "every checked-in image is written back";
}

} // namespace
