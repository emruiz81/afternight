// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Kernel bodies shared by the scalar reference and the auto-vectorization candidate.
//
// They are one source rather than two copies on purpose: the only difference those two candidates
// may have is whether the compiler is allowed to vectorize. If the arithmetic could drift between
// them, the pair would stop measuring "what does the compiler manage on its own" and start
// measuring whichever transcription was luckier.
//
// The includer defines SPIKE_NO_VECTOR - to the pragma that forbids vectorization, or to nothing.

#include "Kernels.h"

#include <algorithm>
#include <cmath>
#include <limits>

#if !defined(SPIKE_NO_VECTOR)
#error "define SPIKE_NO_VECTOR before including this file"
#endif

namespace spike
{
namespace SPIKE_IMPL_NAMESPACE
{

using namespace spike::constants;

void transfer(const std::uint16_t* src, float* dst, std::size_t count)
{
    SPIKE_NO_VECTOR
    for (std::size_t i = 0; i < count; ++i)
    {
        dst[i] = static_cast<float>(src[i]) * kU16Scale;
    }
}

void clampScale(const float* src, float* dst, std::size_t count, float scale, float bias, float lo, float hi)
{
    SPIKE_NO_VECTOR
    for (std::size_t i = 0; i < count; ++i)
    {
        const float value = src[i] * scale + bias;
        dst[i] = (std::min)(hi, (std::max)(lo, value));
    }
}

Reduction reduce(const float* src, std::size_t count)
{
    Reduction result;
    if (count == 0)
    {
        return result;
    }

    // The sum accumulates in double. Every candidate is measured against this, so the tolerance for
    // a float-accumulating vector implementation has to allow for that difference rather than
    // pretend the two are the same computation.
    double sum = 0.0;
    float minimum = src[0];
    float maximum = src[0];

    SPIKE_NO_VECTOR
    for (std::size_t i = 0; i < count; ++i)
    {
        const float value = src[i];
        sum += static_cast<double>(value);
        minimum = (std::min)(minimum, value);
        maximum = (std::max)(maximum, value);
    }

    result.sum = sum;
    result.minimum = minimum;
    result.maximum = maximum;
    return result;
}

void strided(const float* src, std::size_t srcStride, float* dst, std::size_t dstStride, std::size_t width,
             std::size_t height, float scale, float bias, float lo, float hi)
{
    for (std::size_t y = 0; y < height; ++y)
    {
        const float* srcRow = src + y * srcStride;
        float* dstRow = dst + y * dstStride;

        SPIKE_NO_VECTOR
        for (std::size_t x = 0; x < width; ++x)
        {
            const float value = srcRow[x] * scale + bias;
            dstRow[x] = (std::min)(hi, (std::max)(lo, value));
        }
    }
}

void gamma(const float* src, float* dst, std::size_t count, float exponent)
{
    SPIKE_NO_VECTOR
    for (std::size_t i = 0; i < count; ++i)
    {
        dst[i] = std::pow(src[i], exponent);
    }
}

void u8ToF32(const std::uint8_t* src, float* dst, std::size_t count)
{
    SPIKE_NO_VECTOR
    for (std::size_t i = 0; i < count; ++i)
    {
        dst[i] = static_cast<float>(src[i]) * kU8Scale;
    }
}

void i16ToF32(const std::int16_t* src, float* dst, std::size_t count, float offset, float scale)
{
    SPIKE_NO_VECTOR
    for (std::size_t i = 0; i < count; ++i)
    {
        dst[i] = (static_cast<float>(src[i]) + offset) * scale;
    }
}

void f32ToU16(const float* src, std::uint16_t* dst, std::size_t count)
{
    SPIKE_NO_VECTOR
    for (std::size_t i = 0; i < count; ++i)
    {
        const float scaled = (std::min)(kU16Max, (std::max)(0.0f, src[i] * kU16Max));
        dst[i] = static_cast<std::uint16_t>(std::nearbyintf(scaled));
    }
}

void f32ToU8(const float* src, std::uint8_t* dst, std::size_t count)
{
    SPIKE_NO_VECTOR
    for (std::size_t i = 0; i < count; ++i)
    {
        const float scaled = (std::min)(kU8Max, (std::max)(0.0f, src[i] * kU8Max));
        dst[i] = static_cast<std::uint8_t>(std::nearbyintf(scaled));
    }
}

void brightnessContrast(const float* src, float* dst, std::size_t count, float brightness, float contrast)
{
    SPIKE_NO_VECTOR
    for (std::size_t i = 0; i < count; ++i)
    {
        const float value = (src[i] - kPivot) * contrast + kPivot + brightness;
        dst[i] = (std::min)(1.0f, (std::max)(0.0f, value));
    }
}

void mtf(const float* src, float* dst, std::size_t count, float midtones, float lo, float hi)
{
    // image_hist_transform::MTF, verbatim.
    SPIKE_NO_VECTOR
    for (std::size_t i = 0; i < count; ++i)
    {
        const float x = src[i];
        if (x <= lo)
        {
            dst[i] = 0.0f;
            continue;
        }
        if (x >= hi)
        {
            dst[i] = 1.0f;
            continue;
        }
        const float xp = (x - lo) / (hi - lo);
        dst[i] = ((midtones - 1.0f) * xp) / (((2.0f * midtones - 1.0f) * xp) - midtones);
    }
}

void calibrate(const float* light, const float* dark, const float* flat, float* dst, std::size_t count, double factor)
{
    // calibration_divide's contract: a zero divisor yields NaN rather than an infinity.
    SPIKE_NO_VECTOR
    for (std::size_t i = 0; i < count; ++i)
    {
        if (flat[i] == 0.0f)
        {
            dst[i] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        const double value =
            factor * ((static_cast<double>(light[i]) - static_cast<double>(dark[i])) / static_cast<double>(flat[i]));
        dst[i] = static_cast<float>(value);
    }
}

void calibrateFloat(const float* light, const float* dark, const float* flat, float* dst, std::size_t count,
                    float factor)
{
    SPIKE_NO_VECTOR
    for (std::size_t i = 0; i < count; ++i)
    {
        if (flat[i] == 0.0f)
        {
            dst[i] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        dst[i] = factor * ((light[i] - dark[i]) / flat[i]);
    }
}

// --- native 16-bit strategies ---
//
// The fixed-point spelling below is written once here and mirrored exactly by the vector
// candidates, so "native 16-bit" means the same arithmetic everywhere and the strategies can be
// compared on speed and precision rather than on who rounded differently.
//
// Q15 with a halved coefficient, because the honest range does not fit otherwise: a contrast above
// 1.0 can push a centred sample past the signed 16-bit limit, so the multiply carries contrast/2
// and the doubling afterwards saturates. That saturation, and the two bits lost in the Q15 round,
// are the price of staying in 16-bit lanes.

int fixedPointCoefficient(float contrast)
{
    const long value = std::lroundf(contrast * 0.5f * 32768.0f);
    return static_cast<int>(value < -32768 ? -32768 : (value > 32767 ? 32767 : value));
}

int clampToInt16(int value)
{
    return value < -32768 ? -32768 : (value > 32767 ? 32767 : value);
}

int clampToUInt16(int value)
{
    return value < 0 ? 0 : (value > 65535 ? 65535 : value);
}

void u16BrightnessContrastFused(const std::uint16_t* src, std::uint16_t* dst, std::size_t count, float brightness,
                                float contrast)
{
    SPIKE_NO_VECTOR
    for (std::size_t i = 0; i < count; ++i)
    {
        const float x = static_cast<float>(src[i]) * kU16Scale;
        float value = (x - kPivot) * contrast + kPivot + brightness;
        value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        dst[i] = static_cast<std::uint16_t>(std::nearbyintf(value * kU16Max));
    }
}

void u16BrightnessContrastNative(const std::uint16_t* src, std::uint16_t* dst, std::size_t count, float brightness,
                                 float contrast)
{
    const int coefficient = fixedPointCoefficient(contrast);
    // Half scale, because it is added inside the halved-coefficient domain so that the expression
    // saturates exactly once. Saturating the contrast product first and adding brightness after -
    // the obvious order - clips a shadow to zero and then lifts it back up, disagreeing with the
    // float path by the full offset.
    const int halfOffset = static_cast<int>(std::lroundf(brightness * kU16Max * 0.5f));

    SPIKE_NO_VECTOR
    for (std::size_t i = 0; i < count; ++i)
    {
        const int centred = static_cast<int>(src[i]) - 32768;
        // MulFixedPoint15: (a * b + 0x4000) >> 15, saturated.
        const int scaled = clampToInt16(clampToInt16((centred * coefficient + 16384) >> 15) + halfOffset);
        // SaturatedAdd(v, v): the doubling that undoes the halved coefficient.
        const int doubled = clampToInt16(scaled + scaled);
        dst[i] = static_cast<std::uint16_t>(doubled + 32768);
    }
}

void u16MtfLut(const std::uint16_t* src, std::uint16_t* dst, std::size_t count, const std::uint16_t* lut)
{
    // Every distinct input value is one of 65536, so the whole transfer function fits in 128 KB and
    // the transcendental disappears entirely. This option exists only because the input is 16-bit.
    SPIKE_NO_VECTOR
    for (std::size_t i = 0; i < count; ++i)
    {
        dst[i] = lut[src[i]];
    }
}

Reduction u16Reduce(const std::uint16_t* src, std::size_t count)
{
    Reduction result;
    if (count == 0)
    {
        return result;
    }

    // The accumulator is 64-bit and exact: 2^16 values summed over any plausible frame cannot
    // overflow it, so unlike the float reduction this one has no accumulation error at all.
    std::uint64_t total = 0;
    std::uint16_t lowest = src[0];
    std::uint16_t highest = src[0];

    SPIKE_NO_VECTOR
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::uint16_t value = src[i];
        total += value;
        lowest = value < lowest ? value : lowest;
        highest = value > highest ? value : highest;
    }

    result.sum = static_cast<double>(total) * static_cast<double>(kU16Scale);
    result.minimum = static_cast<float>(lowest) * kU16Scale;
    result.maximum = static_cast<float>(highest) * kU16Scale;
    return result;
}

} // namespace SPIKE_IMPL_NAMESPACE
} // namespace spike
