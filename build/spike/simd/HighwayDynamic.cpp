// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Highway with runtime dispatch - the mode that is actually Highway's reason to exist.
//
// The same kernel bodies as Highway.cpp, compiled once per target, with the best one selected at
// load time. A single shipped binary therefore uses AVX-512 where the CPU has it and AVX2 where it
// does not, with no separate build. Measured against the static row, the difference is what that
// costs: one indirect call per kernel invocation, plus the object size of carrying every target.
//
// Dispatch is per call, so on a kernel that processes a whole image it disappears into the noise,
// and on a kernel called per row or per tile it would not. The row-at-a-time `strided` kernel is
// the one to watch for that.

#include "Kernels.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "HighwayDynamic.cpp"
#include "hwy/foreach_target.h" // IWYU pragma: keep

#include "hwy/highway.h"

#include <cmath>
#include <limits>

HWY_BEFORE_NAMESPACE();

#include "hwy/contrib/math/math-inl.h"

namespace spike
{
namespace HWY_NAMESPACE
{

#include "HighwayImpl.inl"

} // namespace HWY_NAMESPACE
} // namespace spike

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace spike
{

HWY_EXPORT(transfer);
HWY_EXPORT(clampScale);
HWY_EXPORT(reduce);
HWY_EXPORT(strided);
HWY_EXPORT(gamma);
HWY_EXPORT(u8ToF32);
HWY_EXPORT(i16ToF32);
HWY_EXPORT(f32ToU16);
HWY_EXPORT(f32ToU8);
HWY_EXPORT(brightnessContrast);
HWY_EXPORT(mtf);
HWY_EXPORT(calibrate);
HWY_EXPORT(calibrateFloat);
HWY_EXPORT(u16BrightnessContrastFused);
HWY_EXPORT(u16BrightnessContrastNative);
HWY_EXPORT(u16Reduce);

namespace
{

void dispatchTransfer(const std::uint16_t* src, float* dst, std::size_t count)
{
    HWY_DYNAMIC_DISPATCH(transfer)(src, dst, count);
}

void dispatchClampScale(const float* src, float* dst, std::size_t count, float scale, float bias, float lo, float hi)
{
    HWY_DYNAMIC_DISPATCH(clampScale)(src, dst, count, scale, bias, lo, hi);
}

Reduction dispatchReduce(const float* src, std::size_t count)
{
    return HWY_DYNAMIC_DISPATCH(reduce)(src, count);
}

void dispatchStrided(const float* src, std::size_t srcStride, float* dst, std::size_t dstStride, std::size_t width,
                     std::size_t height, float scale, float bias, float lo, float hi)
{
    HWY_DYNAMIC_DISPATCH(strided)
    (src, srcStride, dst, dstStride, width, height, scale, bias, lo, hi);
}

void dispatchGamma(const float* src, float* dst, std::size_t count, float exponent)
{
    HWY_DYNAMIC_DISPATCH(gamma)(src, dst, count, exponent);
}

void dispatchU8ToF32(const std::uint8_t* src, float* dst, std::size_t count)
{
    HWY_DYNAMIC_DISPATCH(u8ToF32)(src, dst, count);
}

void dispatchI16ToF32(const std::int16_t* src, float* dst, std::size_t count, float offset, float scale)
{
    HWY_DYNAMIC_DISPATCH(i16ToF32)(src, dst, count, offset, scale);
}

void dispatchF32ToU16(const float* src, std::uint16_t* dst, std::size_t count)
{
    HWY_DYNAMIC_DISPATCH(f32ToU16)(src, dst, count);
}

void dispatchF32ToU8(const float* src, std::uint8_t* dst, std::size_t count)
{
    HWY_DYNAMIC_DISPATCH(f32ToU8)(src, dst, count);
}

void dispatchBrightnessContrast(const float* src, float* dst, std::size_t count, float brightness, float contrast)
{
    HWY_DYNAMIC_DISPATCH(brightnessContrast)(src, dst, count, brightness, contrast);
}

void dispatchMtf(const float* src, float* dst, std::size_t count, float midtones, float lo, float hi)
{
    HWY_DYNAMIC_DISPATCH(mtf)(src, dst, count, midtones, lo, hi);
}

void dispatchCalibrate(const float* light, const float* dark, const float* flat, float* dst, std::size_t count,
                       double factor)
{
    HWY_DYNAMIC_DISPATCH(calibrate)(light, dark, flat, dst, count, factor);
}

void dispatchCalibrateFloat(const float* light, const float* dark, const float* flat, float* dst, std::size_t count,
                            float factor)
{
    HWY_DYNAMIC_DISPATCH(calibrateFloat)(light, dark, flat, dst, count, factor);
}

void dispatchU16BrightnessContrastFused(const std::uint16_t* src, std::uint16_t* dst, std::size_t count,
                                        float brightness, float contrast)
{
    HWY_DYNAMIC_DISPATCH(u16BrightnessContrastFused)(src, dst, count, brightness, contrast);
}

void dispatchU16BrightnessContrastNative(const std::uint16_t* src, std::uint16_t* dst, std::size_t count,
                                         float brightness, float contrast)
{
    HWY_DYNAMIC_DISPATCH(u16BrightnessContrastNative)(src, dst, count, brightness, contrast);
}

Reduction dispatchU16Reduce(const std::uint16_t* src, std::size_t count)
{
    return HWY_DYNAMIC_DISPATCH(u16Reduce)(src, count);
}

const KernelSet kSet{"highway-dynamic",
                     &dispatchTransfer,
                     &dispatchClampScale,
                     &dispatchReduce,
                     &dispatchStrided,
                     &dispatchGamma,
                     &dispatchU8ToF32,
                     &dispatchI16ToF32,
                     &dispatchF32ToU16,
                     &dispatchF32ToU8,
                     &dispatchBrightnessContrast,
                     &dispatchMtf,
                     &dispatchCalibrate,
                     &dispatchCalibrateFloat,
                     &dispatchU16BrightnessContrastFused,
                     &dispatchU16BrightnessContrastNative,
                     nullptr, // u16MtfLut - a table lookup, not a vector kernel
                     &dispatchU16Reduce};

} // namespace

const KernelSet& highwayDynamicKernels()
{
    return kSet;
}

const char* highwayBestDynamicTargetName()
{
    // Targets are ordered best-first as bits, so the lowest set bit of the supported mask is the one
    // dynamic dispatch will pick on this machine.
    const std::int64_t supported = hwy::SupportedTargets();
    return hwy::TargetName(supported & -supported);
}

} // namespace spike

#endif // HWY_ONCE
