// AfterNightEngine - the one scalar row body of the sample-history combinations, included where
// it is compiled.
//
// This file is not a header. It defines how a column run finds each frame's row, which samples the
// frames' rules admit, and how one column becomes its median or its auto-adaptive weighted mean, and
// it is included twice: once by the algorithm, inside an unnamed namespace, as the scalar authority
// every backend is held to, and once by the compiled column unit inside the vector library's
// per-target namespace, where it finishes the pixels a vector of lanes does not cover. Writing the
// body once is what makes the tail of a compiled row the scalar row by construction rather than by a
// test's discovery.
//
// The includer supplies `StackColumnRunParameters` and its companions from
// `kernels/reference/AlgorithmKernels.h`, and `<algorithm>`, `<cmath>`, `<cstdint>` and `<cstring>`.
//
// **Every value is normalized on read and every operation is spelled so a vector lane can spell it
// the same way.** A sample is widened to Float64 and has zero added, which turns a negative zero
// into a positive one and changes nothing else, so that equal samples are equal bits and any exact
// selection of the median agrees with any other. The auto-adaptive iteration is the adopted
// algorithm's arithmetic in its own order: the scaled sum, the plain mean, the root of the mean
// squared deviation, the weight `1 / (1 + (r / sigma)^2)`, the weight total, and the re-weighted
// mean accumulated as `(v * w) / total` one sample at a time. A lane-wise version that adds a zero
// for every excluded sample stores the same bits, because no partial sum here is ever a negative
// zero.
//
// The auto-adaptive weighted mean is adapted from DeepSkyStacker's `AutoAdaptiveWeightedAverage`
// (https://github.com/deepskystacker/DSS, `DeepSkyStackerKernel/DSSTools.h`, by David C. Partridge with
// Michael A. Covington and Simon C. Smith), whose licence states `Copyright (c) 2006-2019, LucCoiffier`
// and `Copyright (c) 2018-2023, David C. Partridge, Tony Cook, Mat Draper, Simon C. Smith, Vitali
// Pelenjow, Michal Schulz, Martin Toeltsch` and the three-clause BSD terms (BSD-3-Clause). This file is
// part of that derived work: it is distributed under the GNU General Public License, version 3 or any
// later version, with that notice kept, and the notice in full is in
// `src/AfterNightEngine/THIRD_PARTY_NOTICES.md`. The routine's are the scaled sum, the plain mean, the
// root mean squared deviation, the weight, the iteration, the small-sigma escape and the three-sample
// minimum; the engine's are the frames' validity rules, the sample scale applied to every column, and a
// column whose largest sample is not positive taking its robust mean rather than zero. The provenance
// note is `docs/dev/DEEPSKYSTACKER_PROVENANCE.md`.

/// The first byte of one frame's row of one channel.
///
/// @param frame the frame.
/// @param channel the channel, which selects a plane and a component of the frame's layout.
/// @param y the row within the band.
/// @return the row's first sample.

// SPDX-License-Identifier: GPL-3.0-or-later AND BSD-3-Clause
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

[[nodiscard]] inline const std::byte* stackColumnFrameRow(const StackColumnFrame& frame, std::uint32_t channel,
                                                          std::uint32_t y) noexcept
{
    const std::uint32_t plane = channel / frame.samplesPerPixel;
    const std::uint32_t component = channel % frame.samplesPerPixel;
    return frame.samples + static_cast<std::uint64_t>(plane) * frame.planeStrideBytes +
           static_cast<std::uint64_t>(y) * frame.rowStrideBytes +
           static_cast<std::uint64_t>(component) * frame.elementBytes;
}

/// The packed mask bits of one frame's row of one channel, or null when the frame carries none.
///
/// @param frame the frame.
/// @param channel the channel, which selects a bit-plane when the mask has one per channel.
/// @param y the row within the band.
/// @return the row's first mask byte, or null.
[[nodiscard]] inline const std::uint8_t* stackColumnFrameMaskRow(const StackColumnFrame& frame, std::uint32_t channel,
                                                                 std::uint32_t y) noexcept
{
    if (frame.maskBits == nullptr)
    {
        return nullptr;
    }
    const std::uint32_t plane = frame.maskPlanes == 1u ? 0u : channel;
    return frame.maskBits + static_cast<std::uint64_t>(plane) * frame.maskPlaneStrideBytes +
           static_cast<std::uint64_t>(y) * frame.maskRowStrideBytes;
}

/// Whether one widened sample is admitted by its frame's rule.
///
/// @param value the sample, widened to Float64.
/// @param x the pixel within the row, for the mask bit.
/// @param frame the frame, for its rule.
/// @param maskRow the frame's mask bits for this row, or null.
/// @return true when the sample takes part.
[[nodiscard]] inline bool stackColumnSampleAdmitted(double value, std::uint64_t x, const StackColumnFrame& frame,
                                                    const std::uint8_t* maskRow) noexcept
{
    switch (frame.rule)
    {
    case ValidityRule::AllValid:
        return true;
    case ValidityRule::FiniteOnly:
        return std::isfinite(value);
    case ValidityRule::ZeroIsNoData:
        return value != 0.0;
    case ValidityRule::ExplicitMask:
        return ((maskRow[x / 8u] >> (x % 8u)) & 1u) != 0u;
    case ValidityRule::DomainRange:
        return value >= frame.lowestValid && value <= frame.highestValid;
    }
    return false;
}

/// One sample of a row, widened to Float64 with its zero made positive.
///
/// @tparam SampleT the resident floating representation of the samples.
/// @param row the row's first sample.
/// @param x the pixel within the row.
/// @param stride the distance, in samples, from one pixel of the row to the next: one for a
///        planar row and the channel count for an interleaved one.
/// @return the widened sample.
template <typename SampleT>
[[nodiscard]] inline double stackColumnSampleAt(const std::byte* row, std::uint64_t x, std::uint32_t stride) noexcept
{
    SampleT sample;
    std::memcpy(&sample, row + x * stride * sizeof(SampleT), sizeof(SampleT));
    return static_cast<double>(sample) + 0.0;
}

/// The exact median of a column, which is permuted by the selection.
///
/// @param column the admitted samples.
/// @param count how many, at least one.
/// @return the middle sample, or the mean of the two middle ones spelled `(lower + upper) * 0.5`.
[[nodiscard]] inline double stackColumnMedianOf(double* column, std::uint32_t count) noexcept
{
    const std::uint32_t upperIndex = count / 2u;
    std::nth_element(column, column + upperIndex, column + count);
    const double upper = column[upperIndex];
    if (count % 2u != 0u)
    {
        return upper;
    }
    const double lower = *std::max_element(column, column + upperIndex);
    return (lower + upper) * 0.5;
}

/// The auto-adaptive weighted mean of a column, in the adopted algorithm's own operation order.
///
/// @param column the admitted samples, unscaled and in frame order.
/// @param weights room for one weight per sample.
/// @param count how many samples, at least one.
/// @param parameters the scale, the iteration count, the small-sigma escape and the sample floor.
/// @return the mean, scaled back to the samples' units.
[[nodiscard]] inline double stackColumnAutoAdaptiveOf(const double* column, double* weights, std::uint32_t count,
                                                      const StackColumnRunParameters& parameters) noexcept
{
    const double scale = parameters.sampleScale;
    const double n = static_cast<double>(count);
    double sum = 0.0;
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        sum = sum + column[index] * scale;
    }
    double average = sum / n;
    if (count < parameters.minimumSamples)
    {
        return average / scale;
    }
    for (std::uint32_t iteration = 0u; iteration < parameters.iterations; ++iteration)
    {
        double squares = 0.0;
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            const double deviation = column[index] * scale - average;
            squares = squares + deviation * deviation;
        }
        const double sigma = std::sqrt(squares / n);
        if (iteration == 0u && sigma <= parameters.smallSigma)
        {
            return average / scale;
        }
        double total = 0.0;
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            const double distance = std::fabs(column[index] * scale - average);
            const double ratio = distance / sigma;
            const double weight = 1.0 / (1.0 + ratio * ratio);
            weights[index] = weight;
            total = total + weight;
        }
        double next = 0.0;
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            next = next + (column[index] * scale * weights[index]) / total;
        }
        average = next;
    }
    return average / scale;
}

/// Combines the columns of one run of pixels, one at a time, into the targets.
///
/// @tparam SampleT the resident floating representation of the bands' samples.
/// @param firstPixel the first pixel of the row the run covers.
/// @param count how many pixels the run covers.
/// @param parameters the frames, the row and the combination.
/// @param scratch the worker's column and workspace.
/// @param targets the rows of the output, the mask and the maps.
/// @return what the run did.
template <typename SampleT>
[[nodiscard]] inline StackColumnRunOutcome
stackColumnRowOf(std::uint64_t firstPixel, std::uint64_t count, const StackColumnRunParameters& parameters,
                 const StackColumnRunScratch& scratch, const StackColumnRunTargets& targets) noexcept
{
    StackColumnRunOutcome outcome{};
    bool finite = true;
    const std::uint64_t end = firstPixel + count;
    for (std::uint64_t x = firstPixel; x < end; ++x)
    {
        std::uint32_t admitted = 0u;
        double sum = 0.0;
        for (std::uint32_t index = 0u; index < parameters.frameCount; ++index)
        {
            const StackColumnFrame& frame = parameters.frames[index];
            const std::byte* const row = stackColumnFrameRow(frame, parameters.channel, parameters.y);
            const double value = stackColumnSampleAt<SampleT>(row, x, frame.samplesPerPixel);
            if (!stackColumnSampleAdmitted(value, x, frame,
                                           stackColumnFrameMaskRow(frame, parameters.channel, parameters.y)))
            {
                ++outcome.excludedByValidity;
                continue;
            }
            finite = finite && std::isfinite(value);
            scratch.column[admitted] = value;
            ++admitted;
            sum = sum + value;
        }
        outcome.admitted += admitted;

        double result = 0.0;
        double variance = 0.0;
        if (admitted != 0u)
        {
            if (targets.variance != nullptr && admitted > 1u)
            {
                const double mean = sum / static_cast<double>(admitted);
                double squares = 0.0;
                for (std::uint32_t index = 0u; index < admitted; ++index)
                {
                    const double deviation = scratch.column[index] - mean;
                    squares = squares + deviation * deviation;
                }
                variance = squares / static_cast<double>(admitted - 1u);
            }
            switch (parameters.combination)
            {
            case StackColumnRunCombination::Median:
                result = stackColumnMedianOf(scratch.column, admitted);
                break;
            case StackColumnRunCombination::AutoAdaptiveWeightedAverage:
                result = stackColumnAutoAdaptiveOf(scratch.column, scratch.workspace, admitted, parameters);
                break;
            }
            finite = finite && std::isfinite(result);
            ++outcome.withSamples;
            if (targets.maskRow != nullptr)
            {
                targets.maskRow[x / 8u] |= static_cast<std::uint8_t>(1u << (x % 8u));
            }
        }
        if (parameters.outputFloat64)
        {
            std::memcpy(targets.output + x * sizeof(double), &result, sizeof(double));
        }
        else
        {
            const float narrowed = static_cast<float>(result);
            finite = finite && std::isfinite(narrowed);
            std::memcpy(targets.output + x * sizeof(float), &narrowed, sizeof(float));
        }
        if (targets.coverage != nullptr)
        {
            targets.coverage[x] = admitted;
        }
        if (targets.variance != nullptr)
        {
            targets.variance[x] = static_cast<float>(variance);
        }
        if (targets.gain != nullptr)
        {
            targets.gain[x] = static_cast<float>(admitted);
        }
        if (targets.weightSum != nullptr)
        {
            targets.weightSum[x] = static_cast<float>(admitted);
        }
    }
    outcome.finite = finite;
    return outcome;
}
