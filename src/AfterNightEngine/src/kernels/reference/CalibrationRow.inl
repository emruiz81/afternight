// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the scalar authority for one contiguous fused calibration row.
//
// Included by `backends/cpu/CalibrationCpu.cpp`, inside each compiled target's namespace, so a
// vector body and the samples its lanes do not cover run the same arithmetic rather than two texts
// of it. It defines a function template and nothing else, and is included where a definition is
// wanted rather than compiled on its own. The includer supplies `CalibrationRunParameters` and
// `CalibrationRunOutcome` from `kernels/reference/AlgorithmKernels.h`, and `<cmath>`, `<cstdint>`
// and `<limits>`.

/// Calibrates one contiguous run of samples, a sample at a time.
///
/// Every step rounds back to the sample's own representation before the next one reads it, which is
/// what the arithmetic below is: a chain of stated operations, not one long expression evaluated at
/// whatever width a compiler chose.
///
/// @tparam SampleT the resident floating representation.
/// @param input the light's samples.
/// @param bias the master bias, or null.
/// @param dark the master dark, or null.
/// @param flat the master flat, or null.
/// @param destination where calibrated samples are written.
/// @param firstPixel the first pixel of the run this call covers.
/// @param pixelCount one past the last pixel of the run this call covers.
/// @param parameters the pedestal, the dark scale, the flat normalization and the channel counts.
/// @param outcome accumulates what the run wrote.
template <typename SampleT>
void calibrationScalarRun(const SampleT* input, const SampleT* bias, const SampleT* dark, const SampleT* flat,
                          SampleT* destination, std::uint64_t firstPixel, std::uint64_t pixelCount,
                          const CalibrationRunParameters& parameters, CalibrationRunOutcome& outcome) noexcept
{
    for (std::uint64_t pixel = firstPixel; pixel < pixelCount; ++pixel)
    {
        for (std::uint32_t channel = 0u; channel < parameters.channelCount; ++channel)
        {
            const std::uint64_t sample = pixel * parameters.channelCount + channel;
            SampleT value = input[sample];
            if (parameters.pedestal != 0.0)
            {
                value = static_cast<SampleT>(static_cast<double>(value) + parameters.pedestal);
            }
            if (bias != nullptr)
            {
                value = static_cast<SampleT>(static_cast<double>(value) - static_cast<double>(bias[sample]));
            }
            if (dark != nullptr)
            {
                SampleT scaledDark = dark[sample];
                if (parameters.darkScale != 1.0)
                {
                    scaledDark = static_cast<SampleT>(static_cast<double>(scaledDark) * parameters.darkScale);
                }
                value = static_cast<SampleT>(static_cast<double>(value) - static_cast<double>(scaledDark));
            }
            if (flat != nullptr)
            {
                const std::uint32_t flatChannel = parameters.flatChannelCount == 1u ? 0u : channel;
                const std::uint64_t flatSample = pixel * parameters.flatChannelCount + flatChannel;
                SampleT divisor = flat[flatSample];
                if (parameters.flatNormalization != nullptr)
                {
                    divisor =
                        static_cast<SampleT>(static_cast<double>(divisor) / parameters.flatNormalization[flatChannel]);
                }
                value = divisor == static_cast<SampleT>(0)
                            ? std::numeric_limits<SampleT>::quiet_NaN()
                            : static_cast<SampleT>(static_cast<double>(value) / static_cast<double>(divisor));
            }
            destination[sample] = value;
            ++outcome.samplesWritten;
            outcome.nonFiniteOutputs += std::isfinite(static_cast<double>(value)) ? 0u : 1u;
        }
    }
}
