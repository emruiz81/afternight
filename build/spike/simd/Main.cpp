// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The measurement harness.
//
// Four deliberate choices, each because the alternative produces a confident wrong answer:
//
//   Candidates are interleaved, not run in sequence. Running every repetition of one candidate
//   before the next lets cache state and clock behaviour attach themselves to whichever ran first,
//   and on this machine that alone can invent a difference larger than the one being measured.
//   Each repetition runs every candidate once, in order, and the median across repetitions is
//   reported.
//
//   Two sizes are measured, not one. A kernel over a whole frame is bound by memory bandwidth, and
//   at that size every candidate converges on the same number no matter how good its code is. The
//   cache-resident size is where an abstraction can actually show a difference. Reporting only one
//   of the two supports whichever conclusion was wanted.
//
//   Accuracy is measured against the scalar implementation and reported as a number rather than a
//   pass mark. Some differences are expected and legitimate - fused multiply-add rounds once where
//   the scalar path rounds twice, and vector reductions accumulate in float where the oracle
//   accumulates in double - so the harness records the size of each difference and fails only on
//   one large enough to be a defect rather than a contract choice.
//
//   Integer outputs are compared exactly, and NaN counts as equal to NaN. A conversion that lands
//   one code value away is a defect, not a rounding preference; and the calibration kernels are
//   contractually required to emit NaN where the flat is zero, so a NaN matching the oracle's NaN
//   has to count as agreement rather than as an incomparable value.

#include "Kernels.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace
{

using spike::KernelSet;
using spike::Reduction;

constexpr std::size_t kAlignment = 64;
constexpr int kWarmup = 2;
constexpr int kRepetitions = 7;

// ---------------------------------------------------------------------------------------------
// Aligned buffers
// ---------------------------------------------------------------------------------------------

template <typename T> T* allocate(std::size_t count)
{
#if defined(_WIN32)
    return static_cast<T*>(_aligned_malloc(count * sizeof(T), kAlignment));
#else
    return static_cast<T*>(std::aligned_alloc(kAlignment, count * sizeof(T)));
#endif
}

void release(void* pointer)
{
#if defined(_WIN32)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

// ---------------------------------------------------------------------------------------------
// Deterministic input, including the values that break naive kernels
// ---------------------------------------------------------------------------------------------

std::uint32_t nextRandom(std::uint64_t& state)
{
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<std::uint32_t>(state >> 32);
}

void fillU16(std::uint16_t* data, std::size_t count, std::uint64_t seed)
{
    std::uint64_t state = seed;
    for (std::size_t i = 0; i < count; ++i)
    {
        data[i] = static_cast<std::uint16_t>(nextRandom(state) & 0xFFFFu);
    }
    // The endpoints must appear: a conversion wrong only at full scale is wrong where it matters
    // most, and random draws will not reliably produce 0 or 65535.
    if (count > 2)
    {
        data[0] = 0;
        data[1] = 65535;
        data[count - 1] = 65535;
    }
}

void fillU8(std::uint8_t* data, std::size_t count, std::uint64_t seed)
{
    std::uint64_t state = seed;
    for (std::size_t i = 0; i < count; ++i)
    {
        data[i] = static_cast<std::uint8_t>(nextRandom(state) & 0xFFu);
    }
    if (count > 2)
    {
        data[0] = 0;
        data[1] = 255;
        data[count - 1] = 255;
    }
}

void fillI16(std::int16_t* data, std::size_t count, std::uint64_t seed)
{
    std::uint64_t state = seed;
    for (std::size_t i = 0; i < count; ++i)
    {
        data[i] = static_cast<std::int16_t>(nextRandom(state) & 0xFFFFu);
    }
    if (count > 2)
    {
        data[0] = -32768;
        data[1] = 32767;
        data[count - 1] = 0;
    }
}

void fillF32Unit(float* data, std::size_t count, std::uint64_t seed)
{
    std::uint64_t state = seed;
    for (std::size_t i = 0; i < count; ++i)
    {
        data[i] = static_cast<float>(nextRandom(state)) * (1.0f / 4294967296.0f);
    }
    if (count > 3)
    {
        data[0] = 0.0f;
        data[1] = 1.0f;
        data[2] = 1.0f / 65535.0f;
        data[count - 1] = 0.0f;
    }
}

/// A master flat: values around 1.0, with a scattering of exact zeros so the calibration kernels'
/// zero guard is actually exercised rather than merely present. A flat with no dead pixels would
/// let a candidate that ignores the guard pass.
void fillFlat(float* data, std::size_t count, std::uint64_t seed)
{
    std::uint64_t state = seed;
    for (std::size_t i = 0; i < count; ++i)
    {
        data[i] = 0.5f + static_cast<float>(nextRandom(state)) * (1.0f / 4294967296.0f);
    }
    for (std::size_t i = 0; i < count; i += 4096)
    {
        data[i] = 0.0f;
    }
    if (count > 1)
    {
        data[0] = 0.0f;
    }
}

// ---------------------------------------------------------------------------------------------
// Accuracy
// ---------------------------------------------------------------------------------------------

struct Accuracy
{
    double maxAbsolute = 0.0;
    double maxRelative = 0.0;
    long long maxUlp = 0;
    bool comparable = true;
    long long outsideTolerance = 0;
};

// Unit-range image data. The absolute term is what makes the comparison meaningful next to zero: a
// clamped kernel produces exact zeros, and both relative error and ULP distance explode when one
// side is zero even though the two values differ by a single rounding. Judging on relative error
// alone reports a correct kernel as broken.
constexpr double kAbsoluteTolerance = 1.0e-6;
constexpr double kRelativeTolerance = 1.0e-5;

long long ulpDistance(float a, float b)
{
    if (a == b)
    {
        return 0;
    }
    if (std::isnan(a) || std::isnan(b) || std::isinf(a) || std::isinf(b))
    {
        return -1;
    }

    std::int32_t ia = 0;
    std::int32_t ib = 0;
    std::memcpy(&ia, &a, sizeof(ia));
    std::memcpy(&ib, &b, sizeof(ib));
    if (ia < 0)
    {
        ia = static_cast<std::int32_t>(0x80000000u - static_cast<std::uint32_t>(ia));
    }
    if (ib < 0)
    {
        ib = static_cast<std::int32_t>(0x80000000u - static_cast<std::uint32_t>(ib));
    }
    return std::llabs(static_cast<long long>(ia) - static_cast<long long>(ib));
}

void accumulate(Accuracy& accuracy, const float* reference, const float* candidate, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        const float r = reference[i];
        const float c = candidate[i];
        if (r == c)
        {
            continue;
        }
        // The calibration kernels must emit NaN where the flat is zero. Two NaNs agree.
        if (std::isnan(r) && std::isnan(c))
        {
            continue;
        }

        const double absolute = std::fabs(static_cast<double>(c) - static_cast<double>(r));
        accuracy.maxAbsolute = (std::max)(accuracy.maxAbsolute, absolute);

        const double denominator = std::fabs(static_cast<double>(r));
        if (denominator > 0.0)
        {
            accuracy.maxRelative = (std::max)(accuracy.maxRelative, absolute / denominator);
        }

        const long long ulp = ulpDistance(r, c);
        if (ulp < 0)
        {
            accuracy.comparable = false;
        }
        else
        {
            accuracy.maxUlp = (std::max)(accuracy.maxUlp, ulp);
        }

        if (absolute > kAbsoluteTolerance + kRelativeTolerance * denominator)
        {
            ++accuracy.outsideTolerance;
        }
    }
}

/// Integer outputs are exact or they are wrong. One code value of drift in a 16-bit conversion is a
/// visible banding difference, not a rounding preference.
template <typename T> Accuracy compareExact(const T* reference, const T* candidate, std::size_t count)
{
    Accuracy accuracy;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (reference[i] != candidate[i])
        {
            ++accuracy.outsideTolerance;
            const double difference = std::fabs(static_cast<double>(candidate[i]) - static_cast<double>(reference[i]));
            accuracy.maxAbsolute = (std::max)(accuracy.maxAbsolute, difference);
        }
    }
    return accuracy;
}

// ---------------------------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------------------------

double medianOf(std::vector<double>& samples)
{
    if (samples.empty())
    {
        return 0.0;
    }
    std::sort(samples.begin(), samples.end());
    const std::size_t middle = samples.size() / 2;
    return samples.size() % 2 == 1 ? samples[middle] : 0.5 * (samples[middle - 1] + samples[middle]);
}

double coefficientOfVariation(const std::vector<double>& samples)
{
    if (samples.size() < 2)
    {
        return 0.0;
    }
    double mean = 0.0;
    for (double s : samples)
    {
        mean += s;
    }
    mean /= static_cast<double>(samples.size());
    if (mean <= 0.0)
    {
        return 0.0;
    }
    double sumSquares = 0.0;
    for (double s : samples)
    {
        sumSquares += (s - mean) * (s - mean);
    }
    return std::sqrt(sumSquares / static_cast<double>(samples.size() - 1)) / mean;
}

// ---------------------------------------------------------------------------------------------
// Jobs
// ---------------------------------------------------------------------------------------------

struct Buffers
{
    const std::uint16_t* srcU16 = nullptr;
    const std::uint8_t* srcU8 = nullptr;
    const std::int16_t* srcI16 = nullptr;
    const float* srcF32 = nullptr;
    const float* dark = nullptr;
    const float* flat = nullptr;

    const std::uint16_t* mtfLut = nullptr;

    float* dstF32 = nullptr;
    std::uint16_t* dstU16 = nullptr;
    std::uint8_t* dstU8 = nullptr;

    std::size_t count = 0;
    std::size_t width = 0;
    std::size_t stride = 0;
    std::size_t height = 0;
};

enum class Output
{
    F32,
    U16,
    U8
};

struct Job
{
    const char* name;
    Output output;
    bool stridedRegion;
    bool (*provided)(const KernelSet&);
    void (*run)(const KernelSet&, const Buffers&);
};

const Job kJobs[] = {
    {"transfer", Output::F32, false, [](const KernelSet& k) { return k.transfer != nullptr; },
     [](const KernelSet& k, const Buffers& b) { k.transfer(b.srcU16, b.dstF32, b.count); }},

    {"clampScale", Output::F32, false, [](const KernelSet& k) { return k.clampScale != nullptr; },
     [](const KernelSet& k, const Buffers& b) { k.clampScale(b.srcF32, b.dstF32, b.count, 1.5f, -0.25f, 0.0f, 1.0f); }},

    {"strided", Output::F32, true, [](const KernelSet& k) { return k.strided != nullptr; },
     [](const KernelSet& k, const Buffers& b) {
         k.strided(b.srcF32, b.stride, b.dstF32, b.stride, b.width, b.height, 1.5f, -0.25f, 0.0f, 1.0f);
     }},

    {"gamma", Output::F32, false, [](const KernelSet& k) { return k.gamma != nullptr; },
     [](const KernelSet& k, const Buffers& b) { k.gamma(b.srcF32, b.dstF32, b.count, 1.0f / 2.2f); }},

    {"convert.u8_to_f32", Output::F32, false, [](const KernelSet& k) { return k.u8ToF32 != nullptr; },
     [](const KernelSet& k, const Buffers& b) { k.u8ToF32(b.srcU8, b.dstF32, b.count); }},

    {"convert.i16_to_f32", Output::F32, false, [](const KernelSet& k) { return k.i16ToF32 != nullptr; },
     [](const KernelSet& k, const Buffers& b) { k.i16ToF32(b.srcI16, b.dstF32, b.count, 32768.0f, 1.0f / 65535.0f); }},

    {"convert.f32_to_u16", Output::U16, false, [](const KernelSet& k) { return k.f32ToU16 != nullptr; },
     [](const KernelSet& k, const Buffers& b) { k.f32ToU16(b.srcF32, b.dstU16, b.count); }},

    {"convert.f32_to_u8", Output::U8, false, [](const KernelSet& k) { return k.f32ToU8 != nullptr; },
     [](const KernelSet& k, const Buffers& b) { k.f32ToU8(b.srcF32, b.dstU8, b.count); }},

    {"brightnessContrast", Output::F32, false, [](const KernelSet& k) { return k.brightnessContrast != nullptr; },
     [](const KernelSet& k, const Buffers& b) { k.brightnessContrast(b.srcF32, b.dstF32, b.count, 0.05f, 1.25f); }},

    {"mtf", Output::F32, false, [](const KernelSet& k) { return k.mtf != nullptr; },
     [](const KernelSet& k, const Buffers& b) { k.mtf(b.srcF32, b.dstF32, b.count, 0.25f, 0.02f, 0.98f); }},

    {"calibrate.double", Output::F32, false, [](const KernelSet& k) { return k.calibrate != nullptr; },
     [](const KernelSet& k, const Buffers& b) { k.calibrate(b.srcF32, b.dark, b.flat, b.dstF32, b.count, 1.0); }},

    {"calibrate.float", Output::F32, false, [](const KernelSet& k) { return k.calibrateFloat != nullptr; },
     [](const KernelSet& k, const Buffers& b) { k.calibrateFloat(b.srcF32, b.dark, b.flat, b.dstF32, b.count, 1.0f); }},

    // The three 16-bit strategies, all u16 in and u16 out so they are directly comparable. The
    // float one is composed from the conversion kernels rather than given its own implementation,
    // which is the point: it has to pay for the two passes it needs, because that is what the
    // engine actually spends today.
    {"u16.brightnessContrast.viaF32", Output::U16, false,
     [](const KernelSet& k) {
         return k.transfer != nullptr && k.brightnessContrast != nullptr && k.f32ToU16 != nullptr;
     },
     [](const KernelSet& k, const Buffers& b) {
         k.transfer(b.srcU16, b.dstF32, b.count);
         k.brightnessContrast(b.dstF32, b.dstF32, b.count, 0.05f, 1.25f);
         k.f32ToU16(b.dstF32, b.dstU16, b.count);
     }},

    {"u16.brightnessContrast.fused", Output::U16, false,
     [](const KernelSet& k) { return k.u16BrightnessContrastFused != nullptr; },
     [](const KernelSet& k, const Buffers& b) {
         k.u16BrightnessContrastFused(b.srcU16, b.dstU16, b.count, 0.05f, 1.25f);
     }},

    {"u16.brightnessContrast.native", Output::U16, false,
     [](const KernelSet& k) { return k.u16BrightnessContrastNative != nullptr; },
     [](const KernelSet& k, const Buffers& b) {
         k.u16BrightnessContrastNative(b.srcU16, b.dstU16, b.count, 0.05f, 1.25f);
     }},

    {"u16.mtf.viaF32", Output::U16, false,
     [](const KernelSet& k) { return k.transfer != nullptr && k.mtf != nullptr && k.f32ToU16 != nullptr; },
     [](const KernelSet& k, const Buffers& b) {
         k.transfer(b.srcU16, b.dstF32, b.count);
         k.mtf(b.dstF32, b.dstF32, b.count, 0.25f, 0.02f, 0.98f);
         k.f32ToU16(b.dstF32, b.dstU16, b.count);
     }},

    {"u16.mtf.lut", Output::U16, false, [](const KernelSet& k) { return k.u16MtfLut != nullptr; },
     [](const KernelSet& k, const Buffers& b) { k.u16MtfLut(b.srcU16, b.dstU16, b.count, b.mtfLut); }},
};

/// Reductions return a value rather than filling a buffer, so they are timed separately.
struct ReduceJob
{
    const char* name;
    bool (*provided)(const KernelSet&);
    Reduction (*run)(const KernelSet&, const Buffers&);
};

const ReduceJob kReduceJobs[] = {
    {"reduce", [](const KernelSet& k) { return k.reduce != nullptr; },
     [](const KernelSet& k, const Buffers& b) { return k.reduce(b.srcF32, b.count); }},
    {"u16.reduce.native", [](const KernelSet& k) { return k.u16Reduce != nullptr; },
     [](const KernelSet& k, const Buffers& b) { return k.u16Reduce(b.srcU16, b.count); }},
};

struct Row
{
    std::string kernel;
    std::string size;
    std::string candidate;
    double medianMs = 0.0;
    double cv = 0.0;
    double gigaElementsPerSecond = 0.0;
    Accuracy accuracy;
    bool provided = true;
    bool isReference = false;
};

std::vector<Row> gRows;

struct SizeCase
{
    const char* label;
    std::size_t count;
};

// 1 Mi is L2/L3 resident on any current desktop; 24 Mi is a realistic mono frame at 24 MP and is
// bound by memory bandwidth rather than by the kernel.
constexpr SizeCase kSizes[] = {{"cache-1Mi", 1u << 20}, {"frame-24Mi", 24u * 1024u * 1024u}};

} // namespace

#include "PipelineDrift.inl"

namespace
{
} // namespace

int main(int argc, char** argv)
{
    const KernelSet* candidates[] = {
        &spike::scalarKernels(), &spike::autovecKernels(), &spike::highwayKernels(), &spike::highwayDynamicKernels(),
        &spike::xsimdKernels(),  &spike::sleefKernels(),   &spike::opencvKernels()};
    const int candidateCount = static_cast<int>(sizeof(candidates) / sizeof(candidates[0]));

    for (const SizeCase& size : kSizes)
    {
        const std::size_t count = size.count;

        Buffers buffers;
        buffers.count = count;
        buffers.width = 1021;
        buffers.stride = 1024;
        buffers.height = count / 1024;

        auto* srcU16 = allocate<std::uint16_t>(count);
        auto* srcU8 = allocate<std::uint8_t>(count);
        auto* srcI16 = allocate<std::int16_t>(count);
        auto* srcF32 = allocate<float>(count);
        auto* dark = allocate<float>(count);
        auto* flat = allocate<float>(count);
        auto* dstF32 = allocate<float>(count);
        auto* dstU16 = allocate<std::uint16_t>(count);
        auto* dstU8 = allocate<std::uint8_t>(count);
        auto* refF32 = allocate<float>(count);
        auto* refU16 = allocate<std::uint16_t>(count);
        auto* refU8 = allocate<std::uint8_t>(count);

        if (srcU16 == nullptr || srcU8 == nullptr || srcI16 == nullptr || srcF32 == nullptr || dark == nullptr ||
            flat == nullptr || dstF32 == nullptr || dstU16 == nullptr || dstU8 == nullptr || refF32 == nullptr ||
            refU16 == nullptr || refU8 == nullptr)
        {
            std::fprintf(stderr, "allocation failed for %zu elements\n", count);
            return 2;
        }

        fillU16(srcU16, count, 0x9E3779B97F4A7C15ULL);
        fillU8(srcU8, count, 0xC2B2AE3D27D4EB4FULL);
        fillI16(srcI16, count, 0x165667B19E3779F9ULL);
        fillF32Unit(srcF32, count, 0xD1B54A32D192ED03ULL);
        fillF32Unit(dark, count, 0x2545F4914F6CDD1DULL);
        fillFlat(flat, count, 0x9E3779B185EBCA87ULL);

        // The MTF lookup table, built once from the scalar kernel so the table strategy and the
        // computed strategies agree by construction rather than by coincidence.
        auto* mtfLut = allocate<std::uint16_t>(65536);
        if (mtfLut == nullptr)
        {
            std::fprintf(stderr, "allocation failed for the mtf lookup table\n");
            return 2;
        }
        {
            std::vector<float> domain(65536);
            std::vector<float> curve(65536);
            for (std::size_t v = 0; v < 65536; ++v)
            {
                domain[v] = static_cast<float>(v) * (1.0f / 65535.0f);
            }
            candidates[0]->mtf(domain.data(), curve.data(), 65536, 0.25f, 0.02f, 0.98f);
            candidates[0]->f32ToU16(curve.data(), mtfLut, 65536);
        }
        buffers.mtfLut = mtfLut;

        buffers.srcU16 = srcU16;
        buffers.srcU8 = srcU8;
        buffers.srcI16 = srcI16;
        buffers.srcF32 = srcF32;
        buffers.dark = dark;
        buffers.flat = flat;

        for (const Job& job : kJobs)
        {
            // Reference output, computed once by the scalar candidate.
            Buffers reference = buffers;
            reference.dstF32 = refF32;
            reference.dstU16 = refU16;
            reference.dstU8 = refU8;
            std::memset(refF32, 0, count * sizeof(float));
            std::memset(refU16, 0, count * sizeof(std::uint16_t));
            std::memset(refU8, 0, count * sizeof(std::uint8_t));
            job.run(*candidates[0], reference);

            Buffers working = buffers;
            working.dstF32 = dstF32;
            working.dstU16 = dstU16;
            working.dstU8 = dstU8;

            std::vector<std::vector<double>> samples(static_cast<std::size_t>(candidateCount));
            std::vector<Accuracy> accuracies(static_cast<std::size_t>(candidateCount));
            std::vector<bool> provided(static_cast<std::size_t>(candidateCount), true);

            for (int c = 0; c < candidateCount; ++c)
            {
                provided[static_cast<std::size_t>(c)] = job.provided(*candidates[c]);
                if (!provided[static_cast<std::size_t>(c)])
                {
                    continue;
                }
                for (int w = 0; w < kWarmup; ++w)
                {
                    job.run(*candidates[c], working);
                }
            }

            for (int rep = 0; rep < kRepetitions; ++rep)
            {
                for (int c = 0; c < candidateCount; ++c)
                {
                    if (!provided[static_cast<std::size_t>(c)])
                    {
                        continue;
                    }

                    const auto start = std::chrono::steady_clock::now();
                    job.run(*candidates[c], working);
                    const auto stop = std::chrono::steady_clock::now();
                    samples[static_cast<std::size_t>(c)].push_back(
                        std::chrono::duration<double, std::milli>(stop - start).count());

                    if (rep == 0)
                    {
                        Accuracy accuracy;
                        if (job.output == Output::U16)
                        {
                            accuracy = compareExact(refU16, dstU16, count);
                        }
                        else if (job.output == Output::U8)
                        {
                            accuracy = compareExact(refU8, dstU8, count);
                        }
                        else if (job.stridedRegion)
                        {
                            // Only what the kernel wrote: the padding between each row's width and
                            // its stride is untouched, and comparing it would compare whatever the
                            // previous candidate left there.
                            for (std::size_t y = 0; y < buffers.height; ++y)
                            {
                                accumulate(accuracy, refF32 + y * buffers.stride, dstF32 + y * buffers.stride,
                                           buffers.width);
                            }
                        }
                        else
                        {
                            accumulate(accuracy, refF32, dstF32, count);
                        }
                        accuracies[static_cast<std::size_t>(c)] = accuracy;
                    }
                }
            }

            const std::size_t elements = job.stridedRegion ? buffers.width * buffers.height : count;
            for (int c = 0; c < candidateCount; ++c)
            {
                Row row;
                row.kernel = job.name;
                row.size = size.label;
                row.candidate = candidates[c]->name;
                row.provided = provided[static_cast<std::size_t>(c)];
                row.isReference = (c == 0);
                if (row.provided)
                {
                    row.medianMs = medianOf(samples[static_cast<std::size_t>(c)]);
                    row.cv = coefficientOfVariation(samples[static_cast<std::size_t>(c)]);
                    row.accuracy = accuracies[static_cast<std::size_t>(c)];
                    if (row.medianMs > 0.0)
                    {
                        row.gigaElementsPerSecond = static_cast<double>(elements) / (row.medianMs * 1.0e-3) / 1.0e9;
                    }
                }
                gRows.push_back(std::move(row));
            }
        }

        for (const ReduceJob& job : kReduceJobs)
        {
            std::vector<std::vector<double>> samples(static_cast<std::size_t>(candidateCount));
            std::vector<Reduction> results(static_cast<std::size_t>(candidateCount));
            std::vector<bool> provided(static_cast<std::size_t>(candidateCount), true);

            for (int c = 0; c < candidateCount; ++c)
            {
                provided[static_cast<std::size_t>(c)] = job.provided(*candidates[c]);
                if (!provided[static_cast<std::size_t>(c)])
                {
                    continue;
                }
                for (int w = 0; w < kWarmup; ++w)
                {
                    results[static_cast<std::size_t>(c)] = job.run(*candidates[c], buffers);
                }
            }

            for (int rep = 0; rep < kRepetitions; ++rep)
            {
                for (int c = 0; c < candidateCount; ++c)
                {
                    if (!provided[static_cast<std::size_t>(c)])
                    {
                        continue;
                    }
                    const auto start = std::chrono::steady_clock::now();
                    results[static_cast<std::size_t>(c)] = job.run(*candidates[c], buffers);
                    const auto stop = std::chrono::steady_clock::now();
                    samples[static_cast<std::size_t>(c)].push_back(
                        std::chrono::duration<double, std::milli>(stop - start).count());
                }
            }

            // The oracle is the first candidate that provided this reduction, which is the scalar
            // one wherever it exists.
            int oracleIndex = 0;
            while (oracleIndex < candidateCount && !provided[static_cast<std::size_t>(oracleIndex)])
            {
                ++oracleIndex;
            }
            const Reduction oracle =
                oracleIndex < candidateCount ? results[static_cast<std::size_t>(oracleIndex)] : Reduction{};

            for (int c = 0; c < candidateCount; ++c)
            {
                Row row;
                row.kernel = job.name;
                row.size = size.label;
                row.candidate = candidates[c]->name;
                row.provided = provided[static_cast<std::size_t>(c)];
                row.isReference = (c == oracleIndex);
                if (row.provided)
                {
                    row.medianMs = medianOf(samples[static_cast<std::size_t>(c)]);
                    row.cv = coefficientOfVariation(samples[static_cast<std::size_t>(c)]);
                    const Reduction& r = results[static_cast<std::size_t>(c)];
                    row.accuracy.maxAbsolute = std::fabs(r.sum - oracle.sum);
                    row.accuracy.maxRelative =
                        oracle.sum != 0.0 ? std::fabs(r.sum - oracle.sum) / std::fabs(oracle.sum) : 0.0;
                    if (r.minimum != oracle.minimum || r.maximum != oracle.maximum)
                    {
                        row.accuracy.comparable = false;
                    }
                    if (row.medianMs > 0.0)
                    {
                        row.gigaElementsPerSecond = static_cast<double>(count) / (row.medianMs * 1.0e-3) / 1.0e9;
                    }
                }
                gRows.push_back(std::move(row));
            }
        }

        // What the native 16-bit strategy costs in precision. The fused strategy computes exactly
        // what the float path computes, so the only strategy that can differ is the fixed-point
        // one, and the difference belongs in the report as code values rather than as a verdict.
        if (candidates[0]->u16BrightnessContrastFused != nullptr &&
            candidates[0]->u16BrightnessContrastNative != nullptr)
        {
            candidates[0]->u16BrightnessContrastFused(srcU16, refU16, count, 0.05f, 1.25f);
            candidates[0]->u16BrightnessContrastNative(srcU16, dstU16, count, 0.05f, 1.25f);
            long long worst = 0;
            long long differing = 0;
            for (std::size_t i = 0; i < count; ++i)
            {
                const long long delta = static_cast<long long>(dstU16[i]) - static_cast<long long>(refU16[i]);
                const long long magnitude = delta < 0 ? -delta : delta;
                if (magnitude != 0)
                {
                    ++differing;
                }
                worst = magnitude > worst ? magnitude : worst;
            }
            std::fprintf(stderr, "native-16 vs fused (%s): max %lld code values, %lld of %zu samples differ\n",
                         size.label, worst, differing, count);
        }

        release(mtfLut);
        release(srcU16);
        release(srcU8);
        release(srcI16);
        release(srcF32);
        release(dark);
        release(flat);
        release(dstF32);
        release(dstU16);
        release(dstU8);
        release(refF32);
        release(refU16);
        release(refU8);
    }

    // The drift table is a second output file rather than a column in the CSV, and the caller names
    // it so that a run on one host does not overwrite another host's table.
    const char* driftPath = argc > 1 ? argv[1] : "build/spike/simd/drift.csv";
    if (const int drift = runPipelineDrift(*candidates[0], driftPath); drift != 0)
    {
        return drift;
    }

    std::fprintf(stderr, "highway static target: %s\n", spike::highwayStaticTargetName());
    std::fprintf(stderr, "highway dynamic target: %s\n", spike::highwayBestDynamicTargetName());

    // CSV to stdout; the report is built from this rather than from prose.
    std::printf("kernel,size,candidate,median_ms,cv,gelem_per_s,max_abs_err,max_rel_err,max_ulp,"
                "outside_tolerance,provided\n");
    int defects = 0;
    for (const Row& row : gRows)
    {
        if (!row.provided)
        {
            std::printf("%s,%s,%s,,,,,,,,no\n", row.kernel.c_str(), row.size.c_str(), row.candidate.c_str());
            continue;
        }
        std::printf("%s,%s,%s,%.4f,%.4f,%.4f,%.3e,%.3e,%lld,%lld,yes\n", row.kernel.c_str(), row.size.c_str(),
                    row.candidate.c_str(), row.medianMs, row.cv, row.gigaElementsPerSecond, row.accuracy.maxAbsolute,
                    row.accuracy.maxRelative, row.accuracy.maxUlp, row.accuracy.outsideTolerance);

        // Outside the combined absolute/relative tolerance is a wrong kernel. A large relative error
        // on its own is not, and near zero it is expected.
        if (!row.isReference && (row.accuracy.outsideTolerance != 0 || !row.accuracy.comparable))
        {
            std::fprintf(stderr, "DEFECT %s/%s/%s outside_tolerance=%lld comparable=%d\n", row.kernel.c_str(),
                         row.size.c_str(), row.candidate.c_str(), row.accuracy.outsideTolerance,
                         row.accuracy.comparable ? 1 : 0);
            ++defects;
        }
    }

    if (defects != 0)
    {
        std::fprintf(stderr, "%d candidate/kernel pairs disagree with the scalar reference\n", defects);
        return 1;
    }
    return 0;
}
