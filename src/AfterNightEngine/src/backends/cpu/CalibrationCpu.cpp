// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the fused image-calibration row, compiled once per instruction set.
//
// The fused calibration row is isolated from the shared SIMD table so its per-instruction-set
// object cost remains attributable to calibration. The ordinary arithmetic and narrowing points
// are the scalar authority's: each vector body below states the same operations on the same
// operands, and the samples a vector does not cover run the scalar authority's own text.

#include "backends/cpu/CalibrationCpu.h"

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/CalibrationCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace calibration_simd
{
namespace HWY_NAMESPACE
{

namespace hn = hwy::HWY_NAMESPACE;

// The scalar authority, compiled here for the samples a vector of lanes does not cover.
#include "kernels/reference/CalibrationRow.inl"

/// Calibrates a contiguous run of single-channel Float64 samples a vector of lanes at a time.
///
/// The arithmetic is the scalar row's arithmetic, lane for lane: every step is computed at Float64,
/// which is the samples' own representation, so a lane and a scalar sample take the same sequence of
/// stated operations and land on the same bits. What the lanes change is how many divisions are in
/// flight - and a calibration that divides by a flat does one or two of them per sample, which is what
/// the scalar row spends most of its time on.
///
/// Restricted to one channel because that is where the flat's normalization is a single constant
/// rather than a value that changes with the lane's position inside a pixel. A mosaic is calibrated
/// before it is resolved into colour, so that is what a calibration run over sensor data is.
///
/// @param input the light's samples.
/// @param bias the master bias, or null.
/// @param dark the master dark, or null.
/// @param flat the master flat, or null.
/// @param destination where calibrated samples are written.
/// @param pixelCount how many samples the run holds.
/// @param parameters the pedestal, the dark scale and the flat normalization.
/// @param outcome accumulates what the run wrote.
/// @return how many samples the vector body covered; the rest are the caller's to finish.
[[nodiscard]] std::uint64_t calibrationFloat64VectorRun(const double* input, const double* bias, const double* dark,
                                                        const double* flat, double* destination,
                                                        std::uint64_t pixelCount,
                                                        const CalibrationRunParameters& parameters,
                                                        CalibrationRunOutcome& outcome) noexcept
{
    const hn::ScalableTag<double> wide;
    using WideVec = hn::Vec<decltype(wide)>;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(wide));

    const WideVec pedestal = hn::Set(wide, parameters.pedestal);
    const WideVec darkScale = hn::Set(wide, parameters.darkScale);
    const WideVec normalization =
        hn::Set(wide, parameters.flatNormalization != nullptr ? parameters.flatNormalization[0] : 1.0);
    const WideVec zero = hn::Zero(wide);
    const WideVec notANumber = hn::Set(wide, std::numeric_limits<double>::quiet_NaN());
    const bool addsPedestal = parameters.pedestal != 0.0;
    const bool scalesDark = parameters.darkScale != 1.0;
    const bool normalizesFlat = parameters.flatNormalization != nullptr;

    std::uint64_t sample = 0u;
    for (; sample + lanes <= pixelCount; sample += lanes)
    {
        WideVec value = hn::LoadU(wide, input + sample);
        if (addsPedestal)
        {
            value = hn::Add(value, pedestal);
        }
        if (bias != nullptr)
        {
            value = hn::Sub(value, hn::LoadU(wide, bias + sample));
        }
        if (dark != nullptr)
        {
            WideVec scaled = hn::LoadU(wide, dark + sample);
            if (scalesDark)
            {
                scaled = hn::Mul(scaled, darkScale);
            }
            value = hn::Sub(value, scaled);
        }
        if (flat != nullptr)
        {
            WideVec divisor = hn::LoadU(wide, flat + sample);
            if (normalizesFlat)
            {
                divisor = hn::Div(divisor, normalization);
            }
            // Selected rather than branched around: a lane whose flat reads zero has no quotient,
            // and the quotient the division produces for it is discarded rather than believed.
            value = hn::IfThenElse(hn::Eq(divisor, zero), notANumber, hn::Div(value, divisor));
        }
        hn::StoreU(value, wide, destination + sample);
        outcome.samplesWritten += lanes;
        outcome.nonFiniteOutputs += lanes - static_cast<std::uint64_t>(hn::CountTrue(wide, hn::IsFinite(value)));
    }
    return sample;
}

/// Calibrates a contiguous run of single-channel Float32 samples a full vector of Float32 lanes at a
/// time.
///
/// The same stated operations as the scalar row, on the same operands, in the same order. The scalar
/// row computes each step at Float64 and rounds to Float32 before the next one reads it. For a step
/// whose two operands are both Float32 samples - subtracting the bias, subtracting the scaled dark,
/// dividing by the flat - that is the step computed in Float32 itself: Float64 carries more than twice
/// Float32's precision plus two bits, which is what makes rounding twice give the bits rounding once
/// gives for an addition, a subtraction, a multiplication and a division, signed zeros, subnormals and
/// overflow included. Those steps therefore run on Float32 lanes, twice as many per vector as Float64
/// lanes and with a division that costs a fraction of Float64's. A step that meets one of the row's
/// Float64 parameters - the pedestal, the dark scale, the flat normalization - has no such
/// equivalence, so it widens each half of the lanes, applies the parameter at Float64 and rounds back.
///
/// @param input the light's samples.
/// @param bias the master bias, or null.
/// @param dark the master dark, or null.
/// @param flat the master flat, or null.
/// @param destination where calibrated samples are written.
/// @param pixelCount how many samples the run holds.
/// @param parameters the pedestal, the dark scale and the flat normalization.
/// @param outcome accumulates what the run wrote.
/// @return how many samples the vector body covered; the rest are the caller's to finish.
[[nodiscard]] std::uint64_t calibrationFloat32VectorRun(const float* input, const float* bias, const float* dark,
                                                        const float* flat, float* destination, std::uint64_t pixelCount,
                                                        const CalibrationRunParameters& parameters,
                                                        CalibrationRunOutcome& outcome) noexcept
{
    const hn::ScalableTag<float> narrow;
    const hn::Repartition<double, decltype(narrow)> wide;
    const hn::Half<decltype(narrow)> half;
    using NarrowVec = hn::Vec<decltype(narrow)>;
    using WideVec = hn::Vec<decltype(wide)>;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(narrow));

    // One step against a Float64 parameter, stated exactly as the scalar row states it: each half of
    // the lanes widened, combined with the parameter at Float64 and rounded back to Float32.
    const auto throughFloat64 = [&](NarrowVec value, WideVec parameter, auto combine) -> NarrowVec {
        const WideVec lower = combine(hn::PromoteLowerTo(wide, value), parameter);
        const WideVec upper = combine(hn::PromoteUpperTo(wide, value), parameter);
        return hn::Combine(narrow, hn::DemoteTo(half, upper), hn::DemoteTo(half, lower));
    };
    const auto add = [](WideVec left, WideVec right) { return hn::Add(left, right); };
    const auto multiply = [](WideVec left, WideVec right) { return hn::Mul(left, right); };
    const auto divide = [](WideVec left, WideVec right) { return hn::Div(left, right); };

    const WideVec pedestal = hn::Set(wide, parameters.pedestal);
    const WideVec darkScale = hn::Set(wide, parameters.darkScale);
    const WideVec normalization =
        hn::Set(wide, parameters.flatNormalization != nullptr ? parameters.flatNormalization[0] : 1.0);
    const NarrowVec zero = hn::Zero(narrow);
    const NarrowVec notANumber = hn::Set(narrow, std::numeric_limits<float>::quiet_NaN());
    const bool addsPedestal = parameters.pedestal != 0.0;
    const bool scalesDark = parameters.darkScale != 1.0;
    const bool normalizesFlat = parameters.flatNormalization != nullptr;

    std::uint64_t sample = 0u;
    for (; sample + lanes <= pixelCount; sample += lanes)
    {
        NarrowVec value = hn::LoadU(narrow, input + sample);
        if (addsPedestal)
        {
            value = throughFloat64(value, pedestal, add);
        }
        if (bias != nullptr)
        {
            value = hn::Sub(value, hn::LoadU(narrow, bias + sample));
        }
        if (dark != nullptr)
        {
            NarrowVec scaled = hn::LoadU(narrow, dark + sample);
            if (scalesDark)
            {
                scaled = throughFloat64(scaled, darkScale, multiply);
            }
            value = hn::Sub(value, scaled);
        }
        if (flat != nullptr)
        {
            NarrowVec divisor = hn::LoadU(narrow, flat + sample);
            if (normalizesFlat)
            {
                divisor = throughFloat64(divisor, normalization, divide);
            }
            // Selected rather than branched around: a lane whose flat reads zero has no quotient,
            // and the quotient the division produces for it is discarded rather than believed.
            value = hn::IfThenElse(hn::Eq(divisor, zero), notANumber, hn::Div(value, divisor));
        }
        hn::StoreU(value, narrow, destination + sample);
        outcome.samplesWritten += lanes;
        outcome.nonFiniteOutputs += lanes - static_cast<std::uint64_t>(hn::CountTrue(narrow, hn::IsFinite(value)));
    }
    return sample;
}

/// One contiguous fused calibration row.
///
/// A row of one channel against a one-channel flat takes the vector body for its whole vectors; any
/// other shape, and every row's remaining samples, run the scalar authority, so a sample lands on
/// the scalar authority's bits whichever body wrote it.
///
/// @tparam SampleT the resident floating representation.
/// @param input the light's samples.
/// @param bias the master bias, or null.
/// @param dark the master dark, or null.
/// @param flat the master flat, or null.
/// @param destination where calibrated samples are written.
/// @param pixelCount how many pixels the run holds.
/// @param parameters the pedestal, the dark scale, the flat normalization and the channel counts.
/// @return what the run wrote.
template <typename SampleT>
[[nodiscard]] CalibrationRunOutcome
calibrationRunOf(const SampleT* input, const SampleT* bias, const SampleT* dark, const SampleT* flat,
                 SampleT* destination, std::uint64_t pixelCount, const CalibrationRunParameters& parameters) noexcept
{
    CalibrationRunOutcome outcome{};
    std::uint64_t pixel = 0u;
    if (parameters.channelCount == 1u && parameters.flatChannelCount == 1u)
    {
        if constexpr (std::is_same_v<SampleT, float>)
        {
            pixel = calibrationFloat32VectorRun(input, bias, dark, flat, destination, pixelCount, parameters, outcome);
        }
        else
        {
            pixel = calibrationFloat64VectorRun(input, bias, dark, flat, destination, pixelCount, parameters, outcome);
        }
    }
    calibrationScalarRun(input, bias, dark, flat, destination, pixel, pixelCount, parameters, outcome);
    return outcome;
}

} // namespace HWY_NAMESPACE
} // namespace calibration_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace calibration_simd
{
namespace
{

constexpr CalibrationCpuRuns kAvx2Runs{.float32 = HWY_CHOOSE_AVX2(calibrationRunOf<float>),
                                       .float64 = HWY_CHOOSE_AVX2(calibrationRunOf<double>)};

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr CalibrationCpuRuns kAvx3Runs{.float32 = HWY_CHOOSE_AVX3(calibrationRunOf<float>),
                                       .float64 = HWY_CHOOSE_AVX3(calibrationRunOf<double>)};
#endif

} // namespace
} // namespace calibration_simd

const CalibrationCpuRuns* simdCalibrationRuns(const InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return &calibration_simd::kAvx2Runs;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return &calibration_simd::kAvx3Runs;
#else
        return nullptr;
#endif
    case InstructionSet::Automatic:
    case InstructionSet::Scalar:
    case InstructionSet::Sse2:
    case InstructionSet::Sse42:
    case InstructionSet::Avx:
    case InstructionSet::Neon:
        return nullptr;
    }
    return nullptr;
}

} // namespace an

#endif // HWY_ONCE
