// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the one scalar row body of stack accumulation, included where it is compiled.
//
// This file is not a header. It defines the admission test and the per-sample update every
// whole-output stack accumulation runs, and it is included twice: once by the algorithm, inside an
// unnamed namespace, as the scalar authority every backend is held to, and once by the compiled
// accumulator unit inside the vector library's per-target namespace, where it finishes the samples
// a vector of lanes does not cover. Writing the body once is what makes the tail of a compiled row
// the scalar row by construction rather than by a test's discovery.
//
// The includer supplies `StackAccumulateRunParameters` and its companions from
// `kernels/reference/AlgorithmKernels.h`, and `<cmath>` and `<cstdint>`.
//
// **Every comparison is spelled so a vector lane can spell it the same way.** The minimum is
// `v < old ? v : old` and the maximum `old < v ? v : old` rather than the standard library's
// selections, because the two agree with a lane-wise select and the library's answer for a signed
// zero pair is a different bit. The Welford update is `delta = v - mean; mean += delta / n;
// m2 += delta * (v - mean)` in exactly that order, and the vector body writes the same three
// operations on the same operands.

/// Whether one widened sample is admitted by the run's rule.
///
/// @param value the sample, widened to Float64.
/// @param index the sample's index within the run, for the mask bit.
/// @param parameters the rule and its inputs.
/// @return true when the sample takes part.
[[nodiscard]] inline bool stackSampleAdmitted(double value, std::uint64_t index,
                                              const StackAccumulateRunParameters& parameters) noexcept
{
    switch (parameters.rule)
    {
    case ValidityRule::AllValid:
        return true;
    case ValidityRule::FiniteOnly:
        return std::isfinite(value);
    case ValidityRule::ZeroIsNoData:
        return value != 0.0;
    case ValidityRule::ExplicitMask: {
        const std::uint64_t bit = parameters.maskBitOffset + index;
        return ((parameters.maskBits[bit / 8u] >> (bit % 8u)) & 1u) != 0u;
    }
    case ValidityRule::DomainRange:
        return value >= parameters.lowestValid && value <= parameters.highestValid;
    }
    return false;
}

/// Accumulates one run of samples, one at a time, into the run's terms.
///
/// @tparam SampleT the resident floating representation of the samples.
/// @param samples the first sample of the run.
/// @param sampleStride the distance, in samples, from one sample of the run to the next: one for a
///        planar row and the channel count for an interleaved one.
/// @param count how many samples the run holds.
/// @param parameters the rule, the weights and the update.
/// @param targets the terms, already offset to the run's first element.
/// @return what the run did.
template <typename SampleT>
[[nodiscard]] inline StackAccumulateRunOutcome
stackAccumulateRowOf(const SampleT* samples, std::uint32_t sampleStride, std::uint64_t count,
                     const StackAccumulateRunParameters& parameters, const StackAccumulatorRunTargets& targets) noexcept
{
    StackAccumulateRunOutcome outcome{};
    bool finite = true;
    for (std::uint64_t index = 0u; index < count; ++index)
    {
        const double value = static_cast<double>(samples[index * sampleStride]);
        if (!stackSampleAdmitted(value, index, parameters))
        {
            ++outcome.excludedByValidity;
            continue;
        }
        double weight = 1.0;
        if (parameters.weighted)
        {
            weight = parameters.frameWeight;
            if (parameters.localWeights != nullptr)
            {
                weight = weight * static_cast<double>(parameters.localWeights[index]);
            }
            if (!(weight > 0.0) || !std::isfinite(weight))
            {
                ++outcome.excludedByWeight;
                continue;
            }
        }

        bool sampleFinite = std::isfinite(value);
        const std::uint32_t before = targets.counts[index];
        const std::uint32_t after = before + 1u;
        targets.counts[index] = after;

        const double old = targets.sums[index];
        double term = old;
        switch (parameters.update)
        {
        case StackAccumulateRunUpdate::WeightedSum:
            term = old + value * weight;
            break;
        case StackAccumulateRunUpdate::Minimum:
            term = before == 0u ? value : (value < old ? value : old);
            break;
        case StackAccumulateRunUpdate::Maximum:
            term = before == 0u ? value : (old < value ? value : old);
            break;
        }
        targets.sums[index] = term;
        sampleFinite = sampleFinite && std::isfinite(term);

        if (targets.weightSums != nullptr)
        {
            const double sum = targets.weightSums[index] + weight;
            targets.weightSums[index] = sum;
            sampleFinite = sampleFinite && std::isfinite(sum);
        }
        if (targets.weightSquareSums != nullptr)
        {
            const double sum = targets.weightSquareSums[index] + weight * weight;
            targets.weightSquareSums[index] = sum;
            sampleFinite = sampleFinite && std::isfinite(sum);
        }
        if (targets.varianceMoments != nullptr)
        {
            double mean = targets.varianceMoments[2u * index];
            double moment = targets.varianceMoments[2u * index + 1u];
            const double delta = value - mean;
            mean = mean + delta / static_cast<double>(after);
            moment = moment + delta * (value - mean);
            targets.varianceMoments[2u * index] = mean;
            targets.varianceMoments[2u * index + 1u] = moment;
            sampleFinite = sampleFinite && std::isfinite(moment);
        }
        finite = finite && sampleFinite;
        ++outcome.accumulated;
    }
    outcome.finite = finite;
    return outcome;
}

/// Accumulates one row of every band into the row's terms, band after band.
///
/// The scalar authority of the multi-band row: each band's run in band order, which is the order
/// every element takes its samples in whichever way a compiled row walks them.
///
/// @tparam SampleT the bands' representation.
/// @param rows one planar row per band, in band order.
/// @param bands how many bands.
/// @param count how many samples each row holds.
/// @param parameters the rule, the weights and the update, the same for every band.
/// @param targets the terms, already offset to the row's first element.
/// @param admittedPerBand receives, added to what it holds, each band's samples taken in.
/// @return what the run did, over every band.
template <typename SampleT>
[[nodiscard]] inline StackAccumulateRunOutcome
stackAccumulateBandsRowOf(const SampleT* const* rows, std::uint32_t bands, std::uint64_t count,
                          const StackAccumulateRunParameters& parameters, const StackAccumulatorRunTargets& targets,
                          std::uint64_t* admittedPerBand) noexcept
{
    StackAccumulateRunOutcome outcome{};
    bool finite = true;
    for (std::uint32_t band = 0u; band < bands; ++band)
    {
        const StackAccumulateRunOutcome run = stackAccumulateRowOf<SampleT>(rows[band], 1u, count, parameters, targets);
        outcome.accumulated += run.accumulated;
        outcome.excludedByValidity += run.excludedByValidity;
        outcome.excludedByWeight += run.excludedByWeight;
        admittedPerBand[band] += run.accumulated + run.excludedByWeight;
        finite = finite && run.finite;
    }
    outcome.finite = finite;
    return outcome;
}
