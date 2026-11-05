// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Every-sample stretch output checks, run in fixed chunks on every core outside timing.
//
// A capture row visits tens of millions of samples, and each one evaluates the independent oracle
// in long double - one to three transcendentals - so a serial walk costs seconds per row per
// process and dominates a qualification's wall time. The walk is split into chunks of a fixed
// sample count, whatever the machine, and each chunk's partial result is kept by chunk index and
// combined in chunk order. Maxima and counts are therefore the serial walk's exactly, and the sum
// of squared errors is one fixed association of it, independent of how many threads ran or which
// chunk each took. An adjacent-ordering comparison at a chunk's first sample reads the previous
// chunk's last sample, so no pair is skipped at a boundary.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <span>
#include <thread>
#include <vector>

namespace an::bench
{

/// Samples in one check chunk. Fixed, so the combination order never depends on the host.
inline constexpr std::size_t kStretchCheckChunkSamples = 65536u;

/// What one full-output walk found, before it is written to counters.
struct StretchCheckTotals
{
    long double maximum = 0.0L;
    long double squares = 0.0L;
    long double roundtrip = 0.0L;
    double rangeExcursion = 0.0;
    double invalid = 0.0;
    double endpoints = 0.0;
    double ordering = 0.0;
    std::size_t count = 0u;
};

/// Runs `chunk(index, begin, end)` over fixed chunks of `count` samples on up to every core.
///
/// Chunks are handed out dynamically, so a slow chunk does not idle the rest; the caller keeps each
/// chunk's result at its index, which is what makes the combination deterministic.
template <typename ChunkFn> void forEachStretchCheckChunk(std::size_t count, ChunkFn&& chunk)
{
    const std::size_t chunks = (count + kStretchCheckChunkSamples - 1u) / kStretchCheckChunkSamples;
    const std::size_t workers =
        (std::min)(chunks, static_cast<std::size_t>((std::max)(1u, std::thread::hardware_concurrency())));
    std::atomic<std::size_t> next{0u};
    const auto work = [&]() {
        for (std::size_t index = next.fetch_add(1u); index < chunks; index = next.fetch_add(1u))
        {
            const std::size_t begin = index * kStretchCheckChunkSamples;
            chunk(index, begin, (std::min)(count, begin + kStretchCheckChunkSamples));
        }
    };
    std::vector<std::thread> threads;
    threads.reserve(workers > 1u ? workers - 1u : 0u);
    for (std::size_t worker = 1u; worker < workers; ++worker)
        threads.emplace_back(work);
    work();
    for (std::thread& thread : threads)
        thread.join();
}

/// Checks every sample of one stretch output against `oracle`.
///
/// Per sample: finiteness, the range excursion and whether it exceeds 1e-6, the absolute error from
/// the oracle and its square, for an inverse the round trip through the oracle's forward curve, the
/// endpoints, and the ordering against the previous sample. A mismatched or empty pair of spans
/// counts one invalid sample, as the serial walk always has.
///
/// @tparam OracleT an oracle with `forward` and `inverse` over long double.
template <typename OracleT>
[[nodiscard]] StretchCheckTotals checkStretchSamples(const OracleT& oracle, bool inverse, std::span<const float> input,
                                                     std::span<const float> output)
{
    StretchCheckTotals totals;
    totals.invalid = input.empty() || input.size() != output.size() ? 1.0 : 0.0;
    totals.count = (std::min)(input.size(), output.size());
    const std::size_t chunks = (totals.count + kStretchCheckChunkSamples - 1u) / kStretchCheckChunkSamples;
    std::vector<StretchCheckTotals> partial(chunks);
    forEachStretchCheckChunk(totals.count, [&](std::size_t index, std::size_t begin, std::size_t end) {
        StretchCheckTotals& part = partial[index];
        for (std::size_t i = begin; i < end; ++i)
        {
            if (!std::isfinite(input[i]) || !std::isfinite(output[i]))
            {
                part.invalid += 1.0;
                continue;
            }
            const double excursion =
                (std::max)({0.0, -static_cast<double>(output[i]), static_cast<double>(output[i]) - 1.0});
            part.rangeExcursion = (std::max)(part.rangeExcursion, excursion);
            if (excursion > 1.0e-6)
                part.invalid += 1.0;
            const auto expected = inverse ? oracle.inverse(input[i]) : oracle.forward(input[i]);
            const auto error = std::abs(static_cast<long double>(output[i]) - expected);
            part.maximum = (std::max)(part.maximum, error);
            part.squares += error * error;
            if (inverse)
                part.roundtrip =
                    (std::max)(part.roundtrip, std::abs(oracle.forward(output[i]) - std::clamp(input[i], 0.0f, 1.0f)));
            if ((input[i] <= 0.0f && std::abs(output[i]) > 1.0e-6f) ||
                (input[i] >= 1.0f && std::abs(output[i] - 1.0f) > 1.0e-6f))
                part.endpoints += 1.0;
            if (i != 0u && ((input[i] > input[i - 1] && output[i] + 2.0e-6f < output[i - 1]) ||
                            (input[i] < input[i - 1] && output[i] > output[i - 1] + 2.0e-6f) ||
                            (input[i] == input[i - 1] && output[i] != output[i - 1])))
                part.ordering += 1.0;
        }
    });
    for (const StretchCheckTotals& part : partial)
    {
        totals.maximum = (std::max)(totals.maximum, part.maximum);
        totals.squares += part.squares;
        totals.roundtrip = (std::max)(totals.roundtrip, part.roundtrip);
        totals.rangeExcursion = (std::max)(totals.rangeExcursion, part.rangeExcursion);
        totals.invalid += part.invalid;
        totals.endpoints += part.endpoints;
        totals.ordering += part.ordering;
    }
    return totals;
}

} // namespace an::bench
