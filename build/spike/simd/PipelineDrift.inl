// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// How far a chain of pointwise operations drifts when the frame stays 16-bit between stages.
//
// A single fused 16-bit operation is bit-identical to the float path - the harness proves that. The
// question this answers is what happens when several of them run back to back, because the engine
// does not apply one adjustment to a frame, it applies a sequence.
//
// Three strategies, run in lockstep over the same input so they can be compared stage by stage:
//
//   float chain   convert once, keep an f32 working buffer across every stage, quantize only to
//                 look at it. One quantization for the whole sequence. This is the reference.
//   fused chain   the frame is u16 between stages; each stage widens, computes in float, narrows.
//                 The arithmetic is exact, but every stage requantizes.
//   native chain  the frame is u16 between stages and the arithmetic is fixed point too, with the
//                 tone curve as a lookup table, which is what one would actually write.
//
// The distinction that matters is that the fused chain accumulates error at all. A single fused
// operation is exact; a sequence of them is not, because the 16-bit intermediate is a quantization
// point. That cost belongs to the storage decision, not to the fixed-point one.

namespace
{

struct DriftStage
{
    bool isToneCurve;
    float first; // brightness, or midtones
    float second; // contrast, unused for the tone curve
};

// Alternating adjustments with parameters that vary per stage, so the sequence is not idempotent
// and errors have somewhere to go.
constexpr DriftStage kDriftStages[] = {
    {false, 0.02f, 1.10f},  {true, 0.45f, 0.0f}, {false, 0.03f, 0.95f}, {true, 0.55f, 0.0f},
    {false, -0.02f, 1.20f}, {true, 0.40f, 0.0f}, {false, 0.01f, 1.05f}, {true, 0.60f, 0.0f},
};

constexpr float kDriftLo = 0.01f;
constexpr float kDriftHi = 0.99f;

struct DriftMeasurement
{
    long long worst = 0;
    double rootMeanSquare = 0.0;
};

DriftMeasurement measureDrift(const std::uint16_t* reference, const std::uint16_t* candidate, std::size_t count)
{
    DriftMeasurement measurement;
    double sumSquares = 0.0;
    for (std::size_t i = 0; i < count; ++i)
    {
        const long long delta = static_cast<long long>(candidate[i]) - static_cast<long long>(reference[i]);
        const long long magnitude = delta < 0 ? -delta : delta;
        measurement.worst = magnitude > measurement.worst ? magnitude : measurement.worst;
        sumSquares += static_cast<double>(delta) * static_cast<double>(delta);
    }
    measurement.rootMeanSquare = std::sqrt(sumSquares / static_cast<double>(count));
    return measurement;
}

/// Runs the three chains in lockstep and writes one row per stage to `path`.
int runPipelineDrift(const spike::KernelSet& kernels, const char* path)
{
    if (kernels.u16BrightnessContrastFused == nullptr || kernels.u16BrightnessContrastNative == nullptr ||
        kernels.u16MtfLut == nullptr)
    {
        std::fprintf(stderr, "pipeline drift skipped: the 16-bit kernels are not available\n");
        return 0;
    }

    constexpr std::size_t kCount = 4u << 20;

    auto* source = allocate<std::uint16_t>(kCount);
    auto* floatState = allocate<float>(kCount);
    auto* floatScratch = allocate<float>(kCount);
    auto* referenceSnapshot = allocate<std::uint16_t>(kCount);
    auto* fusedState = allocate<std::uint16_t>(kCount);
    auto* fusedScratch = allocate<std::uint16_t>(kCount);
    auto* nativeState = allocate<std::uint16_t>(kCount);
    auto* nativeScratch = allocate<std::uint16_t>(kCount);
    auto* table = allocate<std::uint16_t>(65536);
    auto* tableDomain = allocate<float>(65536);
    auto* tableCurve = allocate<float>(65536);

    if (source == nullptr || floatState == nullptr || floatScratch == nullptr || referenceSnapshot == nullptr ||
        fusedState == nullptr || fusedScratch == nullptr || nativeState == nullptr || nativeScratch == nullptr ||
        table == nullptr || tableDomain == nullptr || tableCurve == nullptr)
    {
        std::fprintf(stderr, "pipeline drift: allocation failed\n");
        return 2;
    }

    fillU16(source, kCount, 0x51ED270B7C1A9E35ULL);

    // All three chains start from the same frame.
    kernels.transfer(source, floatState, kCount);
    std::memcpy(fusedState, source, kCount * sizeof(std::uint16_t));
    std::memcpy(nativeState, source, kCount * sizeof(std::uint16_t));

    for (std::size_t v = 0; v < 65536; ++v)
    {
        tableDomain[v] = static_cast<float>(v) * (1.0f / 65535.0f);
    }

    std::FILE* out = std::fopen(path, "wb");
    if (out == nullptr)
    {
        std::fprintf(stderr, "pipeline drift: cannot open %s\n", path);
        return 2;
    }
    std::fprintf(out, "stage,operation,fused_max,fused_rms,native_max,native_rms\r\n");
    std::fprintf(stderr, "\npipeline drift over %zu samples, code values against the float chain\n", kCount);
    std::fprintf(stderr, "%-6s %-18s %10s %10s %10s %10s\n", "stage", "operation", "fused max", "fused rms", "nat max",
                 "nat rms");

    int stageIndex = 0;
    for (const DriftStage& stage : kDriftStages)
    {
        ++stageIndex;

        if (stage.isToneCurve)
        {
            // Float chain: the working buffer stays float across the stage.
            kernels.mtf(floatState, floatState, kCount, stage.first, kDriftLo, kDriftHi);

            // Fused chain: widen, compute, narrow - exact arithmetic, but a 16-bit intermediate.
            kernels.transfer(fusedState, floatScratch, kCount);
            kernels.mtf(floatScratch, floatScratch, kCount, stage.first, kDriftLo, kDriftHi);
            kernels.f32ToU16(floatScratch, fusedScratch, kCount);
            std::memcpy(fusedState, fusedScratch, kCount * sizeof(std::uint16_t));

            // Native chain: the table, built from the same scalar kernel so it is exact for its own
            // definition and differs only by the quantization of its entries.
            kernels.mtf(tableDomain, tableCurve, 65536, stage.first, kDriftLo, kDriftHi);
            kernels.f32ToU16(tableCurve, table, 65536);
            kernels.u16MtfLut(nativeState, nativeScratch, kCount, table);
            std::memcpy(nativeState, nativeScratch, kCount * sizeof(std::uint16_t));
        }
        else
        {
            kernels.brightnessContrast(floatState, floatState, kCount, stage.first, stage.second);

            kernels.u16BrightnessContrastFused(fusedState, fusedScratch, kCount, stage.first, stage.second);
            std::memcpy(fusedState, fusedScratch, kCount * sizeof(std::uint16_t));

            kernels.u16BrightnessContrastNative(nativeState, nativeScratch, kCount, stage.first, stage.second);
            std::memcpy(nativeState, nativeScratch, kCount * sizeof(std::uint16_t));
        }

        // Quantize the float chain only to look at it; it keeps its float state for the next stage.
        kernels.f32ToU16(floatState, referenceSnapshot, kCount);

        const DriftMeasurement fused = measureDrift(referenceSnapshot, fusedState, kCount);
        const DriftMeasurement native = measureDrift(referenceSnapshot, nativeState, kCount);

        const char* name = stage.isToneCurve ? "tone curve" : "brightness/contrast";
        std::fprintf(out, "%d,%s,%lld,%.4f,%lld,%.4f\r\n", stageIndex, name, fused.worst, fused.rootMeanSquare,
                     native.worst, native.rootMeanSquare);
        std::fprintf(stderr, "%-6d %-18s %10lld %10.4f %10lld %10.4f\n", stageIndex, name, fused.worst,
                     fused.rootMeanSquare, native.worst, native.rootMeanSquare);
    }

    std::fclose(out);

    release(source);
    release(floatState);
    release(floatScratch);
    release(referenceSnapshot);
    release(fusedState);
    release(fusedScratch);
    release(nativeState);
    release(nativeScratch);
    release(table);
    release(tableDomain);
    release(tableCurve);
    return 0;
}

} // namespace
