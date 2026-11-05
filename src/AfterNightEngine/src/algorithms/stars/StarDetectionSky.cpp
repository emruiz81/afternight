// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the three sky figures a detection needs, measured in parallel passes.
//
// The median is found by a radix descent rather than a sort or a selection: every admitted sample is
// turned into an order-preserving 32-bit key, one pass histograms the keys' high sixteen bits per
// worker, and a second histograms the low sixteen bits inside the bucket the middle rank fell in -
// a third when the two middle ranks of an even count straddle a bucket boundary. No sample is moved
// or compared, the working memory is one fixed histogram per worker, reused for both levels, rather
// than a copy of the plane, and the answer is exact. The noise pass evaluates the engine's
// row-difference estimator on each row independently and takes the median of the row figures, so
// the result is the one a serial walk of the same rows gives, whatever the thread count.

#include "algorithms/stars/StarDetectionSky.h"

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/stars/StarsInternal.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace an::stars_detail
{

namespace
{

constexpr const char* kSkyOperation = "an::detectStars";

/// How many bins one level of the rank descent holds.
///
/// Sixteen bits: two levels cover a Float32 key exactly, and a level's histogram is a quarter of a
/// megabyte per worker, which stays inside a private cache on both nominated hosts.
constexpr std::uint32_t kHistogramBins = 65536u;

/// How many rows one chunk of a pass covers.
constexpr std::uint32_t kSkyChunkRows = 64u;

/// How many clipping passes the row-difference noise estimator runs.
///
/// Three, and the width is five deviations. Both are the engine's own noise estimator's, restated
/// here rather than shared because the estimator they belong to walks one serial row order and this
/// walks the rows in parallel; the numbers must be the same and the loops cannot be.
constexpr std::uint32_t kNoiseClippingIterations = 3u;

/// The width, in deviations, of one clipping pass.
constexpr double kNoiseClippingSigma = 5.0;

/// The order-preserving unsigned key of one Float32 sample.
///
/// Flipping the sign bit of a non-negative value and every bit of a negative one turns the
/// representation's own ordering into unsigned integer ordering, so a radix descent over the key
/// ranks the samples without ever comparing two of them.
[[nodiscard]] std::uint32_t orderKeyOf(float value) noexcept
{
    const auto bits = std::bit_cast<std::uint32_t>(value);
    return (bits & 0x80000000u) != 0u ? ~bits : (bits | 0x80000000u);
}

/// The Float32 sample one order-preserving key describes.
[[nodiscard]] float valueOfOrderKey(std::uint32_t key) noexcept
{
    const std::uint32_t bits = (key & 0x80000000u) != 0u ? (key & 0x7FFFFFFFu) : ~key;
    return std::bit_cast<float>(bits);
}

/// Decides, for one sample, whether the frame's validity rule admits it.
class SkyRule
{
public:
    /// Binds a rule and its optional mask.
    explicit SkyRule(const KernelValidityView& validity) noexcept
        : m_descriptor(validity.descriptor), m_mask(validity.mask), m_maskPlane(validity.maskPlane),
          m_allValid(validity.descriptor.isAllValid())
    {
    }

    /// Whether every sample participates, which is the path worth specialising.
    [[nodiscard]] bool admitsEverything() const noexcept
    {
        return m_allValid;
    }

    /// Whether one sample participates.
    [[nodiscard]] bool admits(float value, std::uint32_t x, std::uint32_t y) const noexcept
    {
        if (m_allValid)
        {
            return true;
        }
        if (m_mask != nullptr)
        {
            return m_mask->test(m_maskPlane, x, y).value_or(false);
        }
        return sampleIsValidByValue(m_descriptor, value);
    }

private:
    /// The rule.
    ValidityDescriptor m_descriptor;

    /// The mask, or null.
    const ValidityMaskView<const std::uint8_t>* m_mask;

    /// Which mask plane decides.
    std::uint32_t m_maskPlane;

    /// Whether the rule admits everything.
    bool m_allValid;
};

/// The mean and population standard deviation of a run.
struct MeanAndDeviation
{
    /// The arithmetic mean.
    double mean = 0.0;

    /// The population standard deviation about that mean.
    double deviation = 0.0;
};

/// The mean and population deviation of one run of differences.
[[nodiscard]] MeanAndDeviation populationDeviation(const double* values, std::size_t count) noexcept
{
    if (count == 0u)
    {
        return MeanAndDeviation{};
    }
    double sum = 0.0;
    for (std::size_t index = 0u; index < count; ++index)
    {
        sum += values[index];
    }
    const double mean = sum / static_cast<double>(count);
    double squares = 0.0;
    for (std::size_t index = 0u; index < count; ++index)
    {
        const double centred = values[index] - mean;
        squares += centred * centred;
    }
    return MeanAndDeviation{.mean = mean, .deviation = std::sqrt(squares / static_cast<double>(count))};
}

/// Rounds a byte count up to the engine's buffer alignment.
[[nodiscard]] std::uint64_t alignedBytes(std::uint64_t bytes) noexcept
{
    return (bytes + kDefaultBufferAlignmentBytes - 1u) & ~(kDefaultBufferAlignmentBytes - 1u);
}

} // namespace

Result<DetectionSky> measureDetectionSky(const ExecutionContext& execution, const float* plane, std::uint32_t width,
                                         std::uint32_t height, const StatisticsRegion& region,
                                         const KernelValidityView& validity)
{
    if (plane == nullptr || region.width == 0u || region.height == 0u || region.originX + region.width > width ||
        region.originY + region.height > height)
    {
        return failure(kSkyOperation, ErrorCode::InvalidArgument, "the measured rectangle does not lie in the plane");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }

    const SkyRule rule(validity);
    const std::uint64_t chunkCount = (static_cast<std::uint64_t>(region.height) + kSkyChunkRows - 1u) / kSkyChunkRows;
    const std::uint32_t workers = (std::max)(execution.admittedConcurrency(), 1u);
    const std::uint64_t histogramBytes =
        alignedBytes(static_cast<std::uint64_t>(kHistogramBins) * sizeof(std::uint32_t));
    const std::uint64_t rowBytes = alignedBytes(static_cast<std::uint64_t>(region.height) * sizeof(double));
    const std::uint64_t differenceBytes = alignedBytes(static_cast<std::uint64_t>(region.width) * sizeof(double));
    const std::uint64_t totalBytes = histogramBytes * workers + rowBytes + differenceBytes * workers;

    MemoryResource* const memory = execution.memoryResource();
    Result<void*> taken = memory->allocate(totalBytes, kDefaultBufferAlignmentBytes);
    if (!taken.hasValue())
    {
        return taken.status();
    }
    auto* const block = static_cast<std::byte*>(*taken);
    auto* const histograms = reinterpret_cast<std::uint32_t*>(block);
    auto* const rowDeviations = reinterpret_cast<double*>(block + histogramBytes * workers);
    auto* const differences = reinterpret_cast<double*>(block + histogramBytes * workers + rowBytes);
    const auto release = [&]() { memory->deallocate(*taken, totalBytes, kDefaultBufferAlignmentBytes); };

    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(region.width) * kSkyChunkRows * sizeof(float),
                        .operationsPerItem = static_cast<std::uint64_t>(region.width) * kSkyChunkRows * 4u};

    // Pass one: the high half of every admitted sample's key, the largest sample, and the count.
    std::fill(histograms, histograms + static_cast<std::size_t>(kHistogramBins) * workers, 0u);
    std::atomic<std::uint64_t> admitted{0u};
    std::atomic<std::uint32_t> largestKey{0u};
    std::atomic<bool> sawNonFinite{false};
    const Status counted = parallelForWorkers(execution, chunkCount, cost, [&](WorkerTask task) {
        std::uint32_t* const histogram = histograms + static_cast<std::size_t>(task.workerSlot) * kHistogramBins;
        std::uint64_t localAdmitted = 0u;
        std::uint32_t localLargest = 0u;
        bool localNonFinite = false;
        for (std::uint64_t chunk = task.range.begin; chunk < task.range.end; ++chunk)
        {
            const Status stillRunning = execution.checkCancellation();
            if (stillRunning.failed())
            {
                return stillRunning;
            }
            const std::uint32_t begin = region.originY + static_cast<std::uint32_t>(chunk) * kSkyChunkRows;
            const std::uint32_t end = (std::min)(region.originY + region.height, begin + kSkyChunkRows);
            for (std::uint32_t y = begin; y < end; ++y)
            {
                const float* const row = plane + static_cast<std::size_t>(y) * width;
                for (std::uint32_t x = region.originX; x < region.originX + region.width; ++x)
                {
                    const float value = row[x];
                    if (!rule.admits(value, x, y))
                    {
                        continue;
                    }
                    if (!std::isfinite(value))
                    {
                        localNonFinite = true;
                        continue;
                    }
                    const std::uint32_t key = orderKeyOf(value);
                    ++histogram[key >> 16u];
                    localLargest = (std::max)(localLargest, key);
                    ++localAdmitted;
                }
            }
        }
        admitted.fetch_add(localAdmitted, std::memory_order_relaxed);
        if (localNonFinite)
        {
            sawNonFinite.store(true, std::memory_order_relaxed);
        }
        std::uint32_t previous = largestKey.load(std::memory_order_relaxed);
        while (previous < localLargest &&
               !largestKey.compare_exchange_weak(previous, localLargest, std::memory_order_relaxed))
        {
        }
        return Status::success();
    });
    if (counted.failed())
    {
        release();
        return counted;
    }
    if (sawNonFinite.load(std::memory_order_relaxed))
    {
        release();
        return failure(kSkyOperation, ErrorCode::InvalidData,
                       "the detection plane holds a sample the validity rule admits and no threshold can use");
    }
    const std::uint64_t sampleCount = admitted.load(std::memory_order_relaxed);
    if (sampleCount == 0u)
    {
        release();
        return failure(kSkyOperation, ErrorCode::EmptySelection,
                       "the validity rule admits no sample of the searched rectangle");
    }

    // The two middle ranks, which are the same rank for an odd count.
    const std::uint64_t lowerRank = (sampleCount - 1u) / 2u;
    const std::uint64_t upperRank = sampleCount / 2u;
    const auto bucketOf = [&](std::uint64_t rank, std::uint64_t& below) {
        std::uint64_t seen = 0u;
        for (std::uint32_t bin = 0u; bin < kHistogramBins; ++bin)
        {
            std::uint64_t count = 0u;
            for (std::uint32_t worker = 0u; worker < workers; ++worker)
            {
                count += histograms[static_cast<std::size_t>(worker) * kHistogramBins + bin];
            }
            if (seen + count > rank)
            {
                below = seen;
                return bin;
            }
            seen += count;
        }
        below = seen;
        return kHistogramBins - 1u;
    };
    std::uint64_t belowLower = 0u;
    std::uint64_t belowUpper = 0u;
    const std::uint32_t lowerBucket = bucketOf(lowerRank, belowLower);
    const std::uint32_t upperBucket = bucketOf(upperRank, belowUpper);

    // Pass two: the low half of the key, within one high bucket at a time. Both middle ranks are in
    // the same bucket unless they straddle a boundary, which costs one more pass.
    const auto keyAtRank = [&](std::uint32_t bucket, std::uint64_t rank, std::uint64_t below,
                               std::uint32_t& key) -> Status {
        std::fill(histograms, histograms + static_cast<std::size_t>(kHistogramBins) * workers, 0u);
        const Status ranked = parallelForWorkers(execution, chunkCount, cost, [&](WorkerTask task) {
            std::uint32_t* const histogram = histograms + static_cast<std::size_t>(task.workerSlot) * kHistogramBins;
            for (std::uint64_t chunk = task.range.begin; chunk < task.range.end; ++chunk)
            {
                const Status stillRunning = execution.checkCancellation();
                if (stillRunning.failed())
                {
                    return stillRunning;
                }
                const std::uint32_t begin = region.originY + static_cast<std::uint32_t>(chunk) * kSkyChunkRows;
                const std::uint32_t end = (std::min)(region.originY + region.height, begin + kSkyChunkRows);
                for (std::uint32_t y = begin; y < end; ++y)
                {
                    const float* const row = plane + static_cast<std::size_t>(y) * width;
                    for (std::uint32_t x = region.originX; x < region.originX + region.width; ++x)
                    {
                        const float value = row[x];
                        if (!rule.admits(value, x, y) || !std::isfinite(value))
                        {
                            continue;
                        }
                        const std::uint32_t sampleKey = orderKeyOf(value);
                        if ((sampleKey >> 16u) == bucket)
                        {
                            ++histogram[sampleKey & 0xFFFFu];
                        }
                    }
                }
            }
            return Status::success();
        });
        if (ranked.failed())
        {
            return ranked;
        }
        std::uint64_t seen = below;
        for (std::uint32_t bin = 0u; bin < kHistogramBins; ++bin)
        {
            std::uint64_t count = 0u;
            for (std::uint32_t worker = 0u; worker < workers; ++worker)
            {
                count += histograms[static_cast<std::size_t>(worker) * kHistogramBins + bin];
            }
            if (seen + count > rank)
            {
                key = (bucket << 16u) | bin;
                return Status::success();
            }
            seen += count;
        }
        key = (bucket << 16u) | (kHistogramBins - 1u);
        return Status::success();
    };

    std::uint32_t lowerKey = 0u;
    const Status lowerFound = keyAtRank(lowerBucket, lowerRank, belowLower, lowerKey);
    if (lowerFound.failed())
    {
        release();
        return lowerFound;
    }
    std::uint32_t upperKey = lowerKey;
    if (upperRank != lowerRank)
    {
        if (upperBucket == lowerBucket)
        {
            // The second rank is in the same bucket and the histogram is still the one just built,
            // so it is read rather than rebuilt.
            std::uint64_t seen = belowLower;
            upperKey = (lowerBucket << 16u) | (kHistogramBins - 1u);
            for (std::uint32_t bin = 0u; bin < kHistogramBins; ++bin)
            {
                std::uint64_t count = 0u;
                for (std::uint32_t worker = 0u; worker < workers; ++worker)
                {
                    count += histograms[static_cast<std::size_t>(worker) * kHistogramBins + bin];
                }
                if (seen + count > upperRank)
                {
                    upperKey = (lowerBucket << 16u) | bin;
                    break;
                }
                seen += count;
            }
        }
        else
        {
            const Status upperFound = keyAtRank(upperBucket, upperRank, belowUpper, upperKey);
            if (upperFound.failed())
            {
                release();
                return upperFound;
            }
        }
    }

    // Pass three: the row-difference noise estimator, one row per worker slot's scratch.
    std::fill(rowDeviations, rowDeviations + region.height, -1.0);
    const Status measured = parallelForWorkers(execution, chunkCount, cost, [&](WorkerTask task) {
        double* const scratch = differences + static_cast<std::size_t>(task.workerSlot) * region.width;
        for (std::uint64_t chunk = task.range.begin; chunk < task.range.end; ++chunk)
        {
            const Status stillRunning = execution.checkCancellation();
            if (stillRunning.failed())
            {
                return stillRunning;
            }
            const std::uint32_t begin = region.originY + static_cast<std::uint32_t>(chunk) * kSkyChunkRows;
            const std::uint32_t end = (std::min)(region.originY + region.height, begin + kSkyChunkRows);
            for (std::uint32_t y = begin; y < end; ++y)
            {
                const float* const row = plane + static_cast<std::size_t>(y) * width;
                std::size_t differenceCount = 0u;
                bool hasPrevious = false;
                double previous = 0.0;
                for (std::uint32_t x = region.originX; x < region.originX + region.width; ++x)
                {
                    const float sample = row[x];
                    if (!rule.admits(sample, x, y))
                    {
                        continue;
                    }
                    const double value = static_cast<double>(sample);
                    if (hasPrevious)
                    {
                        scratch[differenceCount++] = previous - value;
                    }
                    previous = value;
                    hasPrevious = true;
                }
                if (differenceCount < 2u)
                {
                    continue;
                }
                MeanAndDeviation rowStatistics = populationDeviation(scratch, differenceCount);
                if (rowStatistics.deviation > 0.0)
                {
                    for (std::uint32_t iteration = 0u; iteration < kNoiseClippingIterations; ++iteration)
                    {
                        std::size_t retained = 0u;
                        for (std::size_t index = 0u; index < differenceCount; ++index)
                        {
                            if (std::abs(scratch[index] - rowStatistics.mean) <
                                kNoiseClippingSigma * rowStatistics.deviation)
                            {
                                scratch[retained++] = scratch[index];
                            }
                        }
                        if (retained == differenceCount || retained == 0u)
                        {
                            break;
                        }
                        differenceCount = retained;
                        rowStatistics = populationDeviation(scratch, differenceCount);
                        if (rowStatistics.deviation == 0.0)
                        {
                            break;
                        }
                    }
                }
                rowDeviations[y - region.originY] = rowStatistics.deviation;
            }
        }
        return Status::success();
    });
    if (measured.failed())
    {
        release();
        return measured;
    }

    // Compacted in row order, so the median is the one a serial walk of the same rows would take.
    std::uint32_t usableRows = 0u;
    for (std::uint32_t row = 0u; row < region.height; ++row)
    {
        if (rowDeviations[row] >= 0.0)
        {
            rowDeviations[usableRows++] = rowDeviations[row];
        }
    }
    double backgroundNoise = 0.0;
    if (usableRows != 0u)
    {
        std::sort(rowDeviations, rowDeviations + usableRows);
        const std::uint32_t lower = (usableRows - 1u) / 2u;
        const std::uint32_t upper = usableRows / 2u;
        backgroundNoise = (rowDeviations[lower] + rowDeviations[upper]) * 0.5 * std::sqrt(0.5);
    }

    const double lowerValue = static_cast<double>(valueOfOrderKey(lowerKey));
    const double upperValue = static_cast<double>(valueOfOrderKey(upperKey));
    DetectionSky sky{};
    sky.median = lowerRank == upperRank ? lowerValue : lowerValue + 0.5 * (upperValue - lowerValue);
    sky.maximum = static_cast<double>(valueOfOrderKey(largestKey.load(std::memory_order_relaxed)));
    sky.backgroundNoise = backgroundNoise;
    sky.admittedSamples = sampleCount;
    release();
    return sky;
}

} // namespace an::stars_detail
