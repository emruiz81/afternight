// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// xsimd, compile-time target selection.
//
// The direct counterpart to Highway's static mode: one target fixed by the build flags, header
// only, no runtime dispatch and no library to ship.
//
// Its load_as/store_as pair converts between the stored and the computed type during the memory
// operation itself, which is exactly the shape the conversion kernels need and is the most
// ergonomic expression of them among the candidates. It also gives the double-precision
// calibration kernel a clean spelling: load floats directly as a batch of doubles, compute, store
// back as floats, with no explicit promote/demote step.

#include "Kernels.h"

#include <xsimd/xsimd.hpp>

#include <cmath>
#include <limits>

namespace spike
{
namespace
{

using namespace spike::constants;

using Batch = xsimd::batch<float>;
using DoubleBatch = xsimd::batch<double>;
constexpr xsimd::unaligned_mode kUnaligned{};

void transfer(const std::uint16_t* src, float* dst, std::size_t count)
{
    const std::size_t lanes = Batch::size;
    const Batch scale = Batch::broadcast(kU16Scale);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        xsimd::store_as(dst + i, xsimd::load_as<float>(src + i, kUnaligned) * scale, kUnaligned);
    }
    for (; i < count; ++i)
    {
        dst[i] = static_cast<float>(src[i]) * kU16Scale;
    }
}

void clampScale(const float* src, float* dst, std::size_t count, float scaleValue, float biasValue, float lo, float hi)
{
    const std::size_t lanes = Batch::size;
    const Batch scale = Batch::broadcast(scaleValue);
    const Batch bias = Batch::broadcast(biasValue);
    const Batch low = Batch::broadcast(lo);
    const Batch high = Batch::broadcast(hi);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const Batch value = xsimd::fma(Batch::load_unaligned(src + i), scale, bias);
        xsimd::min(high, xsimd::max(low, value)).store_unaligned(dst + i);
    }
    for (; i < count; ++i)
    {
        const float value = src[i] * scaleValue + biasValue;
        dst[i] = value < lo ? lo : (value > hi ? hi : value);
    }
}

Reduction reduce(const float* src, std::size_t count)
{
    Reduction result;
    if (count == 0)
    {
        return result;
    }

    const std::size_t lanes = Batch::size;
    Batch sum = Batch::broadcast(0.0f);
    Batch minimum = Batch::broadcast(src[0]);
    Batch maximum = Batch::broadcast(src[0]);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const Batch value = Batch::load_unaligned(src + i);
        sum += value;
        minimum = xsimd::min(minimum, value);
        maximum = xsimd::max(maximum, value);
    }

    double total = static_cast<double>(xsimd::reduce_add(sum));
    float lowest = xsimd::reduce_min(minimum);
    float highest = xsimd::reduce_max(maximum);

    for (; i < count; ++i)
    {
        total += static_cast<double>(src[i]);
        lowest = src[i] < lowest ? src[i] : lowest;
        highest = src[i] > highest ? src[i] : highest;
    }

    result.sum = total;
    result.minimum = lowest;
    result.maximum = highest;
    return result;
}

void strided(const float* src, std::size_t srcStride, float* dst, std::size_t dstStride, std::size_t width,
             std::size_t height, float scale, float bias, float lo, float hi)
{
    for (std::size_t y = 0; y < height; ++y)
    {
        clampScale(src + y * srcStride, dst + y * dstStride, width, scale, bias, lo, hi);
    }
}

void gamma(const float* src, float* dst, std::size_t count, float exponent)
{
    const std::size_t lanes = Batch::size;
    const Batch power = Batch::broadcast(exponent);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        xsimd::pow(Batch::load_unaligned(src + i), power).store_unaligned(dst + i);
    }
    for (; i < count; ++i)
    {
        dst[i] = std::pow(src[i], exponent);
    }
}

void u8ToF32(const std::uint8_t* src, float* dst, std::size_t count)
{
    const std::size_t lanes = Batch::size;
    const Batch scale = Batch::broadcast(kU8Scale);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        xsimd::store_as(dst + i, xsimd::load_as<float>(src + i, kUnaligned) * scale, kUnaligned);
    }
    for (; i < count; ++i)
    {
        dst[i] = static_cast<float>(src[i]) * kU8Scale;
    }
}

void i16ToF32(const std::int16_t* src, float* dst, std::size_t count, float offset, float scaleValue)
{
    const std::size_t lanes = Batch::size;
    const Batch bias = Batch::broadcast(offset);
    const Batch scale = Batch::broadcast(scaleValue);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const Batch value = (xsimd::load_as<float>(src + i, kUnaligned) + bias) * scale;
        xsimd::store_as(dst + i, value, kUnaligned);
    }
    for (; i < count; ++i)
    {
        dst[i] = (static_cast<float>(src[i]) + offset) * scaleValue;
    }
}

void f32ToU16(const float* src, std::uint16_t* dst, std::size_t count)
{
    const std::size_t lanes = Batch::size;
    const Batch scale = Batch::broadcast(kU16Max);
    const Batch low = Batch::broadcast(0.0f);
    const Batch high = Batch::broadcast(kU16Max);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const Batch scaled = xsimd::min(high, xsimd::max(low, Batch::load_unaligned(src + i) * scale));
        xsimd::store_as(dst + i, xsimd::nearbyint(scaled), kUnaligned);
    }
    for (; i < count; ++i)
    {
        const float scaled = src[i] * kU16Max;
        const float clamped = scaled < 0.0f ? 0.0f : (scaled > kU16Max ? kU16Max : scaled);
        dst[i] = static_cast<std::uint16_t>(std::nearbyintf(clamped));
    }
}

void f32ToU8(const float* src, std::uint8_t* dst, std::size_t count)
{
    const std::size_t lanes = Batch::size;
    const Batch scale = Batch::broadcast(kU8Max);
    const Batch low = Batch::broadcast(0.0f);
    const Batch high = Batch::broadcast(kU8Max);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const Batch scaled = xsimd::min(high, xsimd::max(low, Batch::load_unaligned(src + i) * scale));
        xsimd::store_as(dst + i, xsimd::nearbyint(scaled), kUnaligned);
    }
    for (; i < count; ++i)
    {
        const float scaled = src[i] * kU8Max;
        const float clamped = scaled < 0.0f ? 0.0f : (scaled > kU8Max ? kU8Max : scaled);
        dst[i] = static_cast<std::uint8_t>(std::nearbyintf(clamped));
    }
}

void brightnessContrast(const float* src, float* dst, std::size_t count, float brightness, float contrast)
{
    const std::size_t lanes = Batch::size;
    const Batch pivot = Batch::broadcast(kPivot);
    const Batch gain = Batch::broadcast(contrast);
    const Batch offset = Batch::broadcast(kPivot + brightness);
    const Batch low = Batch::broadcast(0.0f);
    const Batch high = Batch::broadcast(1.0f);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const Batch value = xsimd::fma(Batch::load_unaligned(src + i) - pivot, gain, offset);
        xsimd::min(high, xsimd::max(low, value)).store_unaligned(dst + i);
    }
    for (; i < count; ++i)
    {
        const float value = (src[i] - kPivot) * contrast + kPivot + brightness;
        dst[i] = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }
}

void mtf(const float* src, float* dst, std::size_t count, float midtones, float lo, float hi)
{
    const std::size_t lanes = Batch::size;
    const Batch vLo = Batch::broadcast(lo);
    const Batch vHi = Batch::broadcast(hi);
    const Batch vSpan = Batch::broadcast(hi - lo);
    const Batch vNum = Batch::broadcast(midtones - 1.0f);
    const Batch vDen = Batch::broadcast(2.0f * midtones - 1.0f);
    const Batch vM = Batch::broadcast(midtones);
    const Batch zero = Batch::broadcast(0.0f);
    const Batch one = Batch::broadcast(1.0f);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const Batch x = Batch::load_unaligned(src + i);
        const Batch xp = (x - vLo) / vSpan;
        const Batch curve = (vNum * xp) / ((vDen * xp) - vM);

        Batch result = xsimd::select(x <= vLo, zero, curve);
        result = xsimd::select(x >= vHi, one, result);
        result.store_unaligned(dst + i);
    }
    for (; i < count; ++i)
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
    // load_as<double> reads floats straight into double lanes, so the promotion the engine performs
    // costs a narrower batch and nothing else in source terms.
    const std::size_t lanes = DoubleBatch::size;
    const DoubleBatch vFactor = DoubleBatch::broadcast(factor);
    const DoubleBatch zero = DoubleBatch::broadcast(0.0);
    const DoubleBatch nan = DoubleBatch::broadcast(static_cast<double>(std::numeric_limits<float>::quiet_NaN()));

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const DoubleBatch vLight = xsimd::load_as<double>(light + i, kUnaligned);
        const DoubleBatch vDark = xsimd::load_as<double>(dark + i, kUnaligned);
        const DoubleBatch vFlat = xsimd::load_as<double>(flat + i, kUnaligned);

        const DoubleBatch value = vFactor * ((vLight - vDark) / vFlat);
        xsimd::store_as(dst + i, xsimd::select(vFlat == zero, nan, value), kUnaligned);
    }
    for (; i < count; ++i)
    {
        if (flat[i] == 0.0f)
        {
            dst[i] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        dst[i] = static_cast<float>(
            factor * ((static_cast<double>(light[i]) - static_cast<double>(dark[i])) / static_cast<double>(flat[i])));
    }
}

void calibrateFloat(const float* light, const float* dark, const float* flat, float* dst, std::size_t count,
                    float factor)
{
    const std::size_t lanes = Batch::size;
    const Batch vFactor = Batch::broadcast(factor);
    const Batch zero = Batch::broadcast(0.0f);
    const Batch nan = Batch::broadcast(std::numeric_limits<float>::quiet_NaN());

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const Batch vLight = Batch::load_unaligned(light + i);
        const Batch vDark = Batch::load_unaligned(dark + i);
        const Batch vFlat = Batch::load_unaligned(flat + i);

        const Batch value = vFactor * ((vLight - vDark) / vFlat);
        xsimd::select(vFlat == zero, nan, value).store_unaligned(dst + i);
    }
    for (; i < count; ++i)
    {
        if (flat[i] == 0.0f)
        {
            dst[i] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        dst[i] = factor * ((light[i] - dark[i]) / flat[i]);
    }
}

const KernelSet kSet{
    "xsimd",   &transfer, &clampScale,         &reduce, &strided,   &gamma,         &u8ToF32, &i16ToF32,
    &f32ToU16, &f32ToU8,  &brightnessContrast, &mtf,    &calibrate, &calibrateFloat};

} // namespace

const KernelSet& xsimdKernels()
{
    return kSet;
}

} // namespace spike
