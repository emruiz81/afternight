// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "FitsAverageOracle.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace an::bench
{
namespace
{

/// Elements in one check chunk. Fixed, so the combination order never depends on the host; large,
/// because every chunk opens every frame once.
constexpr std::size_t kChunkElements = std::size_t{1} << 20;

/// The FITS block and card sizes.
constexpr std::size_t kBlockBytes = 2880u;
constexpr std::size_t kCardBytes = 80u;

/// Where a value too large for the result's counters is written instead of infinity.
constexpr double kNotFinite = 1e300;

/// One frame's primary image, as its header describes it.
struct FitsImage
{
    std::uint64_t dataOffset = 0u;
    int bitpix = 0; ///< As the header states it: negative for floating samples.
    std::int64_t zero = 0;
    std::uint64_t width = 0u;
    std::uint64_t height = 0u;
};

/// A UTF-8 path as a filesystem path, without the narrow code page's interpretation.
std::filesystem::path pathOf(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

/// The value field of one header card, or nothing when the card assigns no value.
std::optional<std::string> cardValue(const char* card)
{
    if (card[8] != '=' || card[9] != ' ')
    {
        return std::nullopt;
    }
    std::string value(card + 10, card + kCardBytes);
    const std::size_t comment = value.find('/');
    if (comment != std::string::npos)
    {
        value.resize(comment);
    }
    return value;
}

/// A numeric header value, with FITS's `D` exponent read as `E`.
std::optional<double> numberOf(std::string text)
{
    std::replace(text.begin(), text.end(), 'D', 'E');
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str())
    {
        return std::nullopt;
    }
    return value;
}

/// Reads the primary header of one frame, refusing any image this reader does not read exactly.
std::optional<FitsImage> readHeader(const std::string& path, std::string& problem)
{
    std::ifstream in(pathOf(path), std::ios::binary);
    if (!in)
    {
        problem = "could not open '" + path + "'";
        return std::nullopt;
    }
    std::array<char, kBlockBytes> block{};
    std::uint64_t offset = 0u;
    std::optional<double> bitpix;
    std::optional<double> naxis;
    std::optional<double> naxis1;
    std::optional<double> naxis2;
    std::optional<double> naxis3;
    double zero = 0.0;
    double scale = 1.0;
    bool ended = false;
    while (!ended && in.read(block.data(), static_cast<std::streamsize>(block.size())))
    {
        offset += kBlockBytes;
        for (std::size_t card = 0u; card < kBlockBytes / kCardBytes && !ended; ++card)
        {
            const char* text = block.data() + card * kCardBytes;
            const std::string keyword(text, text + 8);
            if (keyword == "END     ")
            {
                ended = true;
                break;
            }
            const std::optional<std::string> value = cardValue(text);
            if (!value)
            {
                continue;
            }
            const std::optional<double> number = numberOf(*value);
            if (keyword == "BITPIX  ")
                bitpix = number;
            else if (keyword == "NAXIS   ")
                naxis = number;
            else if (keyword == "NAXIS1  ")
                naxis1 = number;
            else if (keyword == "NAXIS2  ")
                naxis2 = number;
            else if (keyword == "NAXIS3  ")
                naxis3 = number;
            else if (keyword == "BZERO   " && number)
                zero = *number;
            else if (keyword == "BSCALE  " && number)
                scale = *number;
        }
    }
    if (!ended)
    {
        problem = "'" + path + "' has no complete FITS header";
        return std::nullopt;
    }
    const bool plane = naxis && naxis1 && naxis2 &&
                       (*naxis == 2.0 || (*naxis == 3.0 && naxis3.has_value() && *naxis3 == 1.0)) && *naxis1 >= 1.0 &&
                       *naxis2 >= 1.0;
    if (!plane)
    {
        problem = "'" + path + "' is not one plane in its primary image";
        return std::nullopt;
    }
    if (bitpix && (*bitpix == -32.0 || *bitpix == -64.0))
    {
        if (scale != 1.0 || zero != 0.0)
        {
            problem = "'" + path + "' is a floating image with a scale or a zero point";
            return std::nullopt;
        }
        FitsImage image;
        image.dataOffset = offset;
        image.bitpix = static_cast<int>(*bitpix);
        image.width = static_cast<std::uint64_t>(*naxis1);
        image.height = static_cast<std::uint64_t>(*naxis2);
        return image;
    }
    if (!bitpix || (*bitpix != 8.0 && *bitpix != 16.0 && *bitpix != 32.0) || scale != 1.0)
    {
        problem = "'" + path + "' is not an 8, 16 or 32-bit integer image at unit scale, nor a floating one";
        return std::nullopt;
    }
    const int bits = static_cast<int>(*bitpix);
    const double unsignedZero = bits == 8 ? 0.0 : std::ldexp(1.0, bits - 1);
    if (zero != 0.0 && zero != unsignedZero)
    {
        problem = "'" + path + "' states a zero point other than the signed or unsigned convention";
        return std::nullopt;
    }
    FitsImage image;
    image.dataOffset = offset;
    image.bitpix = bits;
    image.zero = static_cast<std::int64_t>(zero);
    image.width = static_cast<std::uint64_t>(*naxis1);
    image.height = static_cast<std::uint64_t>(*naxis2);
    return image;
}

/// One stored big-endian sample as its code.
std::int64_t codeAt(const unsigned char* bytes, int bitpix, std::int64_t zero) noexcept
{
    switch (bitpix)
    {
    case 8:
        return static_cast<std::int64_t>(bytes[0]) + zero;
    case 16:
        return static_cast<std::int64_t>(static_cast<std::int16_t>((bytes[0] << 8) | bytes[1])) + zero;
    default:
        return static_cast<std::int64_t>(static_cast<std::int32_t>(
                   (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16) |
                   (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]))) +
               zero;
    }
}

/// One stored big-endian floating sample, of 4 or 8 bytes.
double floatingAt(const unsigned char* bytes, int bitpix) noexcept
{
    std::uint64_t word = 0u;
    const int count = bitpix == -32 ? 4 : 8;
    for (int index = 0; index < count; ++index)
    {
        word = (word << 8u) | bytes[index];
    }
    return bitpix == -32 ? static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(word)))
                         : std::bit_cast<double>(word);
}

/// What one chunk found.
struct ChunkTotals
{
    double maximumRelative = 0.0;
    double maximumAbsolute = 0.0;
    double squares = 0.0;
    std::uint64_t withoutSamples = 0u;
    std::uint64_t clamped = 0u;
};

} // namespace

FitsAverageCheck checkFitsAverage(std::span<const std::string> paths, std::span<const float> result, double resultScale,
                                  std::optional<FitsAverageResultRange> range)
{
    FitsAverageCheck check;
    check.frames = paths.size();
    check.elements = result.size();
    if (paths.empty())
    {
        check.problem = "no frames to average";
        return check;
    }
    std::vector<FitsImage> images;
    images.reserve(paths.size());
    for (const std::string& path : paths)
    {
        std::optional<FitsImage> image = readHeader(path, check.problem);
        if (!image)
        {
            return check;
        }
        if (image->width * image->height != result.size())
        {
            check.problem = "'" + path + "' does not hold one sample per element of the result";
            return check;
        }
        images.push_back(*image);
    }

    const std::size_t chunks = (result.size() + kChunkElements - 1u) / kChunkElements;
    std::vector<ChunkTotals> partial(chunks);
    std::atomic<std::size_t> next{0u};
    std::atomic<bool> failed{false};
    std::mutex problemLock;
    const auto work = [&]() {
        std::vector<std::int64_t> sums;
        std::vector<double> floatingSums;
        std::vector<double> magnitudes;
        std::vector<std::uint32_t> counts;
        std::vector<unsigned char> raw;
        for (std::size_t index = next.fetch_add(1u); index < chunks && !failed.load(); index = next.fetch_add(1u))
        {
            const std::size_t begin = index * kChunkElements;
            const std::size_t end = (std::min)(result.size(), begin + kChunkElements);
            const std::size_t length = end - begin;
            sums.assign(length, 0);
            floatingSums.assign(length, 0.0);
            magnitudes.assign(length, 0.0);
            counts.assign(length, 0u);
            for (std::size_t frame = 0u; frame < images.size(); ++frame)
            {
                const FitsImage& image = images[frame];
                const std::size_t bytes = static_cast<std::size_t>(std::abs(image.bitpix) / 8);
                raw.resize(length * bytes);
                std::ifstream in(pathOf(paths[frame]), std::ios::binary);
                in.seekg(static_cast<std::streamoff>(image.dataOffset + begin * bytes));
                if (!in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size())))
                {
                    const std::lock_guard<std::mutex> hold(problemLock);
                    check.problem = "'" + paths[frame] + "' ends before its image does";
                    failed.store(true);
                    return;
                }
                if (image.bitpix < 0)
                {
                    for (std::size_t i = 0u; i < length; ++i)
                    {
                        const double sample = floatingAt(raw.data() + i * bytes, image.bitpix);
                        if (!std::isfinite(sample))
                        {
                            const std::lock_guard<std::mutex> hold(problemLock);
                            check.problem = "'" + paths[frame] + "' holds a sample that is not finite at element " +
                                            std::to_string(begin + i);
                            failed.store(true);
                            return;
                        }
                        if (sample != 0.0)
                        {
                            floatingSums[i] += sample;
                            magnitudes[i] += std::fabs(sample);
                            ++counts[i];
                        }
                    }
                    continue;
                }
                for (std::size_t i = 0u; i < length; ++i)
                {
                    const std::int64_t code = codeAt(raw.data() + i * bytes, image.bitpix, image.zero);
                    if (code != 0)
                    {
                        sums[i] += code;
                        magnitudes[i] += std::fabs(static_cast<double>(code));
                        ++counts[i];
                    }
                }
            }
            ChunkTotals& totals = partial[index];
            for (std::size_t i = 0u; i < length; ++i)
            {
                const double value = static_cast<double>(result[begin + i]) * resultScale;
                double mean = counts[i] == 0u
                                  ? 0.0
                                  : (static_cast<double>(sums[i]) + floatingSums[i]) / static_cast<double>(counts[i]);
                totals.withoutSamples += counts[i] == 0u ? 1u : 0u;
                if (range && (mean < range->lowest || mean > range->highest))
                {
                    mean = mean < range->lowest ? range->lowest : range->highest;
                    ++totals.clamped;
                }
                if (!std::isfinite(value))
                {
                    totals.maximumRelative = std::numeric_limits<double>::infinity();
                    totals.maximumAbsolute = std::numeric_limits<double>::infinity();
                    totals.squares = std::numeric_limits<double>::infinity();
                    continue;
                }
                const double absolute = std::fabs(value - mean);
                const double scale = counts[i] == 0u ? 0.0 : magnitudes[i] / static_cast<double>(counts[i]);
                const double relative = scale != 0.0      ? absolute / scale
                                        : absolute == 0.0 ? 0.0
                                                          : std::numeric_limits<double>::infinity();
                totals.maximumAbsolute = (std::max)(totals.maximumAbsolute, absolute);
                totals.maximumRelative = (std::max)(totals.maximumRelative, relative);
                totals.squares += absolute * absolute;
            }
        }
    };
    const std::size_t workers =
        (std::min)(chunks, static_cast<std::size_t>((std::max)(1u, std::thread::hardware_concurrency())));
    std::vector<std::thread> threads;
    for (std::size_t worker = 1u; worker < workers; ++worker)
    {
        threads.emplace_back(work);
    }
    work();
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    if (failed.load())
    {
        return check;
    }
    double squares = 0.0;
    for (const ChunkTotals& totals : partial)
    {
        check.maximumRelative = (std::max)(check.maximumRelative, totals.maximumRelative);
        check.maximumAbsolute = (std::max)(check.maximumAbsolute, totals.maximumAbsolute);
        check.elementsWithoutSamples += totals.withoutSamples;
        check.elementsClamped += totals.clamped;
        squares += totals.squares;
    }
    check.rmsAbsolute = result.empty() ? 0.0 : std::sqrt(squares / static_cast<double>(result.size()));
    check.valid = !result.empty();
    if (!check.valid)
    {
        check.problem = "the result holds no element";
    }
    return check;
}

void recordFitsAverageCheck(const FitsAverageCheck& check, ResultRecord& out)
{
    const auto finite = [](double value) { return std::isfinite(value) ? value : kNotFinite; };
    out.counters["stageOracleVersion"] = kFitsAverageOracleVersion;
    out.counters["stageOracleValid"] = check.valid ? 1.0 : 0.0;
    out.counters["stageOracleFrames"] = static_cast<double>(check.frames);
    out.counters["stageOracleElements"] = static_cast<double>(check.elements);
    out.counters["stageOracleElementsWithoutSamples"] = static_cast<double>(check.elementsWithoutSamples);
    out.counters["stageOracleElementsClamped"] = static_cast<double>(check.elementsClamped);
    out.counters["stageOracleMaxRelative"] = finite(check.maximumRelative);
    out.counters["stageOracleMaxAbsolute"] = finite(check.maximumAbsolute);
    out.counters["stageOracleRmsAbsolute"] = finite(check.rmsAbsolute);
    if (check.valid)
    {
        out.correctnessMetric = finite(check.maximumRelative);
    }
    else
    {
        out.correctnessMetric.reset();
    }
}

bool fitsAverageEvidenceValid(const ScenarioSpec& scenario, std::uint64_t frames, const ResultRecord& record)
{
    if (record.skipped || !record.correctnessMetric.has_value())
    {
        return false;
    }
    const auto counter = [&](const char* name) {
        const auto found = record.counters.find(name);
        return found == record.counters.end() ? std::numeric_limits<double>::quiet_NaN() : found->second;
    };
    const double elements = static_cast<double>(scenario.input.width) * scenario.input.height * scenario.input.channels;
    const double relative = counter("stageOracleMaxRelative");
    return counter("stageOracleVersion") == kFitsAverageOracleVersion && counter("stageOracleValid") == 1.0 &&
           counter("stageOracleFrames") == static_cast<double>(frames) && counter("stageOracleElements") == elements &&
           relative <= scenario.tolerance && *record.correctnessMetric == relative;
}

} // namespace an::bench
