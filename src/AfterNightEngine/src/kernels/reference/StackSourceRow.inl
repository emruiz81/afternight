// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the one scalar row body of the stack source path, included where it is compiled.
//
// This file is not a header. It defines the admission test and the per-sample pipeline every
// band of a stack's input runs on its way into the tile - the widening from the stored
// representation, the fused bias, dark and flat calibration, the local-normalization surface and
// the global affine - and it is included twice: once by the algorithm, inside an unnamed
// namespace, as the scalar authority, and once by the compiled source unit inside the vector
// library's per-target namespace, so the same text is compiled once per instruction set. Writing
// the body once is what makes "the compiled row stores the scalar authority's bits" structural.
//
// The includer supplies `StackSourceRunParameters` and its companions from
// `kernels/reference/AlgorithmKernels.h`, the local-normalization row body from
// `kernels/reference/LocalNormalizationRow.inl`, `Image/SampleConversion.h`, and `<cmath>`,
// `<cstdint>` and `<limits>`.
//
// **Every stage narrows where the operation it composes narrows.** The calibration arithmetic is
// the calibration executor's, step by step, with its Float32 rounding points; the local transform
// and the global affine are the two normalization rows' spellings, each computed in Float64 from
// the tile value and narrowed once. A band read through this row therefore holds the bits a frame
// holds after `calibrateImage`, `applyLocalNormalization` and `applyGlobalNormalization` have been
// run on it in turn, which is what lets the fused path be checked against those three operations
// rather than against a second reading of the same arithmetic.

/// Whether one stored sample is admitted by the frame's rule.
///
/// @tparam SourceT the stored representation.
/// @param raw the sample as stored.
/// @param x the sample's column within the row, for the mask bit.
/// @param parameters the rule and its inputs.
/// @return true when the sample takes part.
template <typename SourceT>
[[nodiscard]] inline bool stackSourceAdmitted(SourceT raw, std::uint64_t x,
                                              const StackSourceRunParameters& parameters) noexcept
{
    switch (parameters.rule)
    {
    case ValidityRule::AllValid:
        return true;
    case ValidityRule::FiniteOnly:
        if constexpr (std::is_floating_point_v<SourceT>)
        {
            return raw >= -(std::numeric_limits<SourceT>::max)() && raw <= (std::numeric_limits<SourceT>::max)();
        }
        else
        {
            return true;
        }
    case ValidityRule::ZeroIsNoData:
        return raw != static_cast<SourceT>(0);
    case ValidityRule::ExplicitMask: {
        const std::uint64_t bit = parameters.maskBitOffset + x;
        return ((parameters.maskBits[bit / 8u] >> (bit % 8u)) & 1u) != 0u;
    }
    case ValidityRule::DomainRange: {
        const double widened = static_cast<double>(raw);
        return widened >= parameters.lowestValid && widened <= parameters.highestValid;
    }
    }
    return false;
}

/// Calibrates one widened sample exactly as the calibration executor's fused row does.
///
/// The pedestal is added, the bias and the scaled dark subtracted, and the flat - divided by its
/// central mean first when it is normalized - divided in, each step in Float64 and narrowed to
/// the tile representation, which is the rounding the shipping path was measured with. A zero
/// divisor yields a quiet NaN, which the band's finite-only rule then excludes.
///
/// @tparam DestT the tile representation.
/// @param value the widened sample.
/// @param x the sample's column within the row.
/// @param calibration the masters' rows and the calibration plan's scalars.
/// @return the calibrated sample.
template <typename DestT>
[[nodiscard]] inline DestT stackSourceCalibrate(DestT value, std::uint64_t x,
                                                const StackSourceCalibrationRow& calibration) noexcept
{
    if (calibration.pedestal != 0.0)
    {
        value = static_cast<DestT>(static_cast<double>(value) + calibration.pedestal);
    }
    if (calibration.bias != nullptr)
    {
        const DestT bias = static_cast<const DestT*>(calibration.bias)[x * calibration.biasStride];
        value = static_cast<DestT>(static_cast<double>(value) - static_cast<double>(bias));
    }
    if (calibration.dark != nullptr)
    {
        DestT scaledDark = static_cast<const DestT*>(calibration.dark)[x * calibration.darkStride];
        if (calibration.darkScale != 1.0)
        {
            scaledDark = static_cast<DestT>(static_cast<double>(scaledDark) * calibration.darkScale);
        }
        value = static_cast<DestT>(static_cast<double>(value) - static_cast<double>(scaledDark));
    }
    if (calibration.flat != nullptr)
    {
        DestT divisor = static_cast<const DestT*>(calibration.flat)[x * calibration.flatStride];
        if (calibration.flatFactors != nullptr)
        {
            divisor =
                static_cast<DestT>(static_cast<double>(divisor) / calibration.flatFactors[x % calibration.flatSites]);
        }
        value = divisor == static_cast<DestT>(0)
                    ? std::numeric_limits<DestT>::quiet_NaN()
                    : static_cast<DestT>(static_cast<double>(value) / static_cast<double>(divisor));
    }
    return value;
}

/// Applies one frame's global match to one sample exactly as the normalization row does.
///
/// @tparam DestT the tile representation.
/// @param value the sample.
/// @param parameters the scale, the offset, the method and the zero rule.
/// @param collisions incremented when the zero rule moved the result.
/// @return the sample to store.
template <typename DestT>
[[nodiscard]] inline DestT stackSourceNormalizeGlobally(DestT value, const StackSourceRunParameters& parameters,
                                                        std::uint64_t& collisions) noexcept
{
    const double scaled = static_cast<double>(value) * parameters.scale;
    DestT written = static_cast<DestT>(parameters.multiplicative ? scaled : scaled + parameters.offset);
    if (parameters.zeroRule && written == static_cast<DestT>(0))
    {
        written = std::copysign((std::numeric_limits<DestT>::min)(), static_cast<DestT>(scaled));
        ++collisions;
    }
    return written;
}

/// Reads one row of one channel through the source path into the tile.
///
/// The row is walked a local-normalization cell at a time when a surface is applied, so the three
/// interpolations are polynomials of the column fraction the parameters prepared, and as one cell
/// otherwise. A sample the frame's rule excludes is written as a quiet NaN under calibration or a
/// domain-range rule, whose output states the finite-only rule, and as its own widened value otherwise, so a no-data
/// marker or a masked value travels into the tile unchanged. An admitted sample is widened, calibrated, transformed by
/// the surface and matched, each stage only when the parameters state it; a calibration that produced a non-finite
/// value ends the pipeline for that sample, because the band's finite-only rule excludes it from everything downstream.
///
/// @tparam SourceT the stored representation.
/// @tparam DestT the tile representation.
/// @param source the row's first stored sample of the channel.
/// @param destination where the row's samples go, which may be the source exactly when the two
///        representations agree.
/// @param pixelCount how many pixels the row holds.
/// @param parameters the rule, the conversion, the stages and their inputs.
/// @return what the row did.
template <typename SourceT, typename DestT>
[[nodiscard]] StackSourceRunOutcome stackSourceRunOf(const SourceT* source, DestT* destination,
                                                     std::uint64_t pixelCount,
                                                     const StackSourceRunParameters& parameters) noexcept
{
    StackSourceRunOutcome outcome{};
    const bool local = parameters.local != nullptr;
    const std::uint32_t cells = local ? parameters.local->gridSize + 1u : 1u;
    const bool bicubic = local && parameters.local->bicubic;
    const bool scaled = local && parameters.local->scale.mixed != nullptr;
    bool finite = true;
    for (std::uint32_t cell = 0u; cell < cells; ++cell)
    {
        std::uint64_t first = 0u;
        std::uint64_t last = pixelCount;
        LocalNormalizationCell reference{};
        LocalNormalizationCell target{};
        LocalNormalizationCell scaleCell{};
        if (local)
        {
            first = parameters.local->cellStarts[cell];
            last = parameters.local->cellStarts[cell + 1u];
            if (first >= last)
            {
                continue;
            }
            reference =
                localNormalizationCellOf(parameters.local->reference, cell, parameters.local->gridSize, bicubic);
            target = localNormalizationCellOf(parameters.local->target, cell, parameters.local->gridSize, bicubic);
            scaleCell =
                scaled ? localNormalizationCellOf(parameters.local->scale, cell, parameters.local->gridSize, bicubic)
                       : LocalNormalizationCell{.a3 = parameters.local->uniformScale};
        }
        for (std::uint64_t x = first; x < last; ++x)
        {
            const SourceT raw = source[x * parameters.sourceStride];
            DestT* const out = destination + x * parameters.destinationStride;
            if (!stackSourceAdmitted<SourceT>(raw, x, parameters))
            {
                if (parameters.calibrate || parameters.rule == ValidityRule::DomainRange)
                {
                    *out = std::numeric_limits<DestT>::quiet_NaN();
                    outcome.invalidCalibrationInputs += parameters.calibrate ? 1u : 0u;
                }
                else
                {
                    *out = convertSample<SourceT, DestT>(raw, parameters.policy).value;
                }
                ++outcome.preserved;
                ++outcome.written;
                continue;
            }
            if constexpr (std::is_floating_point_v<SourceT>)
            {
                finite = finite && raw >= -(std::numeric_limits<SourceT>::max)() &&
                         raw <= (std::numeric_limits<SourceT>::max)();
            }
            DestT value = convertSample<SourceT, DestT>(raw, parameters.policy).value;
            if (parameters.calibrate)
            {
                value = stackSourceCalibrate<DestT>(value, x, parameters.calibration);
                if (!(value >= -(std::numeric_limits<DestT>::max)() && value <= (std::numeric_limits<DestT>::max)()))
                {
                    *out = value;
                    ++outcome.nonFiniteOutputs;
                    ++outcome.transformed;
                    ++outcome.written;
                    continue;
                }
            }
            if (local)
            {
                const double t = parameters.local->fractions[x];
                const double referenceLevel = localNormalizationEvaluateCell(reference, t, bicubic);
                const double targetLevel = localNormalizationEvaluateCell(target, t, bicubic);
                const double scale = scaled ? localNormalizationEvaluateCell(scaleCell, t, bicubic) : scaleCell.a3;
                value = localNormalizationTransform<DestT>(value, targetLevel, referenceLevel, scale,
                                                           parameters.zeroRule, outcome.noDataCollisions);
            }
            if (parameters.normalizeGlobally)
            {
                value = stackSourceNormalizeGlobally<DestT>(value, parameters, outcome.noDataCollisions);
            }
            *out = value;
            ++outcome.transformed;
            ++outcome.written;
        }
    }
    outcome.finite = finite;
    return outcome;
}
