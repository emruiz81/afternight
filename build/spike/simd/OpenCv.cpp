// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// OpenCV's universal intrinsics - the "use what is already linked" candidate.
//
// It is here for a reason that is not performance. OpenCV is already a dependency of the product,
// so its universal intrinsics are the only abstraction available at zero additional deployment
// cost. Against that, using them binds the engine to OpenCV's headers and release cadence, which the
// engine is designed to avoid. The spike measures it so that trade is made against numbers rather
// than instinct.
//
// Two kernels are deliberately left unimplemented rather than faked:
//
//   `gamma` uses cv::pow through Mat headers over the caller's buffers - an OpenCV library call,
//   not an inlined kernel, because the intrinsics layer exposes no vector transcendental. The
//   report labels that row accordingly.
//
//   `calibrate` (the double-precision one) is not provided. Expressing it would mean hand-rolling
//   the float-to-double widening that Highway and xsimd both spell in one operation, and the
//   awkwardness is the finding: an abstraction that makes a mixed-precision kernel painful is
//   telling us something a timing cannot.

#include "Kernels.h"

#include <opencv2/core.hpp>
#include <opencv2/core/hal/intrin.hpp>

#include <cmath>
#include <limits>

namespace spike
{
namespace
{

using namespace spike::constants;

void transfer(const std::uint16_t* src, float* dst, std::size_t count)
{
    const int lanes = cv::VTraits<cv::v_float32>::vlanes();
    const cv::v_float32 scale = cv::vx_setall_f32(kU16Scale);
    const std::size_t step = static_cast<std::size_t>(lanes) * 2;

    std::size_t i = 0;
    for (; i + step <= count; i += step)
    {
        const cv::v_uint16 packed = cv::vx_load(src + i);
        cv::v_uint32 lowHalf;
        cv::v_uint32 highHalf;
        cv::v_expand(packed, lowHalf, highHalf);

        cv::v_store(dst + i, cv::v_mul(cv::v_cvt_f32(cv::v_reinterpret_as_s32(lowHalf)), scale));
        cv::v_store(dst + i + lanes, cv::v_mul(cv::v_cvt_f32(cv::v_reinterpret_as_s32(highHalf)), scale));
    }
    for (; i < count; ++i)
    {
        dst[i] = static_cast<float>(src[i]) * kU16Scale;
    }
}

void clampScale(const float* src, float* dst, std::size_t count, float scaleValue, float biasValue, float lo, float hi)
{
    const std::size_t lanes = static_cast<std::size_t>(cv::VTraits<cv::v_float32>::vlanes());
    const cv::v_float32 scale = cv::vx_setall_f32(scaleValue);
    const cv::v_float32 bias = cv::vx_setall_f32(biasValue);
    const cv::v_float32 low = cv::vx_setall_f32(lo);
    const cv::v_float32 high = cv::vx_setall_f32(hi);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const cv::v_float32 value = cv::v_fma(cv::vx_load(src + i), scale, bias);
        cv::v_store(dst + i, cv::v_min(high, cv::v_max(low, value)));
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

    const std::size_t lanes = static_cast<std::size_t>(cv::VTraits<cv::v_float32>::vlanes());
    cv::v_float32 sum = cv::vx_setzero_f32();
    cv::v_float32 minimum = cv::vx_setall_f32(src[0]);
    cv::v_float32 maximum = cv::vx_setall_f32(src[0]);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const cv::v_float32 value = cv::vx_load(src + i);
        sum = cv::v_add(sum, value);
        minimum = cv::v_min(minimum, value);
        maximum = cv::v_max(maximum, value);
    }

    double total = static_cast<double>(cv::v_reduce_sum(sum));
    float lowest = cv::v_reduce_min(minimum);
    float highest = cv::v_reduce_max(maximum);

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
    // Mat headers over the caller's memory: no allocation, no copy, so this times cv::pow itself.
    const cv::Mat source(1, static_cast<int>(count), CV_32F, const_cast<float*>(src));
    cv::Mat destination(1, static_cast<int>(count), CV_32F, dst);
    cv::pow(source, static_cast<double>(exponent), destination);
}

void u8ToF32(const std::uint8_t* src, float* dst, std::size_t count)
{
    const int lanes = cv::VTraits<cv::v_float32>::vlanes();
    const cv::v_float32 scale = cv::vx_setall_f32(kU8Scale);
    const std::size_t step = static_cast<std::size_t>(lanes) * 4;

    std::size_t i = 0;
    for (; i + step <= count; i += step)
    {
        const cv::v_uint8 packed = cv::vx_load(src + i);
        cv::v_uint16 wide0;
        cv::v_uint16 wide1;
        cv::v_expand(packed, wide0, wide1);

        cv::v_uint32 q0;
        cv::v_uint32 q1;
        cv::v_uint32 q2;
        cv::v_uint32 q3;
        cv::v_expand(wide0, q0, q1);
        cv::v_expand(wide1, q2, q3);

        cv::v_store(dst + i, cv::v_mul(cv::v_cvt_f32(cv::v_reinterpret_as_s32(q0)), scale));
        cv::v_store(dst + i + lanes, cv::v_mul(cv::v_cvt_f32(cv::v_reinterpret_as_s32(q1)), scale));
        cv::v_store(dst + i + 2 * lanes, cv::v_mul(cv::v_cvt_f32(cv::v_reinterpret_as_s32(q2)), scale));
        cv::v_store(dst + i + 3 * lanes, cv::v_mul(cv::v_cvt_f32(cv::v_reinterpret_as_s32(q3)), scale));
    }
    for (; i < count; ++i)
    {
        dst[i] = static_cast<float>(src[i]) * kU8Scale;
    }
}

void i16ToF32(const std::int16_t* src, float* dst, std::size_t count, float offset, float scaleValue)
{
    const int lanes = cv::VTraits<cv::v_float32>::vlanes();
    const cv::v_float32 bias = cv::vx_setall_f32(offset);
    const cv::v_float32 scale = cv::vx_setall_f32(scaleValue);
    const std::size_t step = static_cast<std::size_t>(lanes) * 2;

    std::size_t i = 0;
    for (; i + step <= count; i += step)
    {
        const cv::v_int16 packed = cv::vx_load(src + i);
        cv::v_int32 lowHalf;
        cv::v_int32 highHalf;
        cv::v_expand(packed, lowHalf, highHalf);

        cv::v_store(dst + i, cv::v_mul(cv::v_add(cv::v_cvt_f32(lowHalf), bias), scale));
        cv::v_store(dst + i + lanes, cv::v_mul(cv::v_add(cv::v_cvt_f32(highHalf), bias), scale));
    }
    for (; i < count; ++i)
    {
        dst[i] = (static_cast<float>(src[i]) + offset) * scaleValue;
    }
}

void f32ToU16(const float* src, std::uint16_t* dst, std::size_t count)
{
    const int lanes = cv::VTraits<cv::v_float32>::vlanes();
    const cv::v_float32 scale = cv::vx_setall_f32(kU16Max);
    const cv::v_float32 low = cv::vx_setzero_f32();
    const cv::v_float32 high = cv::vx_setall_f32(kU16Max);
    const std::size_t step = static_cast<std::size_t>(lanes) * 2;

    std::size_t i = 0;
    for (; i + step <= count; i += step)
    {
        const cv::v_float32 a = cv::v_min(high, cv::v_max(low, cv::v_mul(cv::vx_load(src + i), scale)));
        const cv::v_float32 b = cv::v_min(high, cv::v_max(low, cv::v_mul(cv::vx_load(src + i + lanes), scale)));
        cv::v_store(dst + i, cv::v_pack_u(cv::v_round(a), cv::v_round(b)));
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
    const int lanes = cv::VTraits<cv::v_float32>::vlanes();
    const cv::v_float32 scale = cv::vx_setall_f32(kU8Max);
    const cv::v_float32 low = cv::vx_setzero_f32();
    const cv::v_float32 high = cv::vx_setall_f32(kU8Max);
    const std::size_t step = static_cast<std::size_t>(lanes) * 4;

    std::size_t i = 0;
    for (; i + step <= count; i += step)
    {
        const cv::v_float32 a = cv::v_min(high, cv::v_max(low, cv::v_mul(cv::vx_load(src + i), scale)));
        const cv::v_float32 b = cv::v_min(high, cv::v_max(low, cv::v_mul(cv::vx_load(src + i + lanes), scale)));
        const cv::v_float32 c = cv::v_min(high, cv::v_max(low, cv::v_mul(cv::vx_load(src + i + 2 * lanes), scale)));
        const cv::v_float32 d = cv::v_min(high, cv::v_max(low, cv::v_mul(cv::vx_load(src + i + 3 * lanes), scale)));

        const cv::v_uint16 lowPack = cv::v_pack_u(cv::v_round(a), cv::v_round(b));
        const cv::v_uint16 highPack = cv::v_pack_u(cv::v_round(c), cv::v_round(d));
        cv::v_store(dst + i, cv::v_pack(lowPack, highPack));
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
    const std::size_t lanes = static_cast<std::size_t>(cv::VTraits<cv::v_float32>::vlanes());
    const cv::v_float32 pivot = cv::vx_setall_f32(kPivot);
    const cv::v_float32 gain = cv::vx_setall_f32(contrast);
    const cv::v_float32 offset = cv::vx_setall_f32(kPivot + brightness);
    const cv::v_float32 low = cv::vx_setzero_f32();
    const cv::v_float32 high = cv::vx_setall_f32(1.0f);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const cv::v_float32 value = cv::v_fma(cv::v_sub(cv::vx_load(src + i), pivot), gain, offset);
        cv::v_store(dst + i, cv::v_min(high, cv::v_max(low, value)));
    }
    for (; i < count; ++i)
    {
        const float value = (src[i] - kPivot) * contrast + kPivot + brightness;
        dst[i] = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }
}

void mtf(const float* src, float* dst, std::size_t count, float midtones, float lo, float hi)
{
    const std::size_t lanes = static_cast<std::size_t>(cv::VTraits<cv::v_float32>::vlanes());
    const cv::v_float32 vLo = cv::vx_setall_f32(lo);
    const cv::v_float32 vHi = cv::vx_setall_f32(hi);
    const cv::v_float32 vSpan = cv::vx_setall_f32(hi - lo);
    const cv::v_float32 vNum = cv::vx_setall_f32(midtones - 1.0f);
    const cv::v_float32 vDen = cv::vx_setall_f32(2.0f * midtones - 1.0f);
    const cv::v_float32 vM = cv::vx_setall_f32(midtones);
    const cv::v_float32 zero = cv::vx_setzero_f32();
    const cv::v_float32 one = cv::vx_setall_f32(1.0f);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const cv::v_float32 x = cv::vx_load(src + i);
        const cv::v_float32 xp = cv::v_div(cv::v_sub(x, vLo), vSpan);
        const cv::v_float32 curve = cv::v_div(cv::v_mul(vNum, xp), cv::v_sub(cv::v_mul(vDen, xp), vM));

        cv::v_float32 result = cv::v_select(cv::v_le(x, vLo), zero, curve);
        result = cv::v_select(cv::v_ge(x, vHi), one, result);
        cv::v_store(dst + i, result);
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

void calibrateFloat(const float* light, const float* dark, const float* flat, float* dst, std::size_t count,
                    float factor)
{
    const std::size_t lanes = static_cast<std::size_t>(cv::VTraits<cv::v_float32>::vlanes());
    const cv::v_float32 vFactor = cv::vx_setall_f32(factor);
    const cv::v_float32 zero = cv::vx_setzero_f32();
    const cv::v_float32 nan = cv::vx_setall_f32(std::numeric_limits<float>::quiet_NaN());

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const cv::v_float32 vLight = cv::vx_load(light + i);
        const cv::v_float32 vDark = cv::vx_load(dark + i);
        const cv::v_float32 vFlat = cv::vx_load(flat + i);

        const cv::v_float32 value = cv::v_mul(vFactor, cv::v_div(cv::v_sub(vLight, vDark), vFlat));
        cv::v_store(dst + i, cv::v_select(cv::v_eq(vFlat, zero), nan, value));
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

const KernelSet kSet{"opencv-intrinsics",
                     &transfer,
                     &clampScale,
                     &reduce,
                     &strided,
                     &gamma,
                     &u8ToF32,
                     &i16ToF32,
                     &f32ToU16,
                     &f32ToU8,
                     &brightnessContrast,
                     &mtf,
                     nullptr, // calibrate (double) - see the file comment
                     &calibrateFloat};

} // namespace

const KernelSet& opencvKernels()
{
    return kSet;
}

} // namespace spike
