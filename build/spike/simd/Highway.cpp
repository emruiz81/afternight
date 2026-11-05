// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Highway, compile-time target selection.
//
// The apples-to-apples candidate against xsimd: one target chosen by the build flags, no runtime
// dispatch. HighwayDynamic.cpp compiles the same kernel bodies for every target and selects at load
// time, so the difference between the two rows is what dispatch costs and what a wider instruction
// set buys.
//
// Note on the reduction: every vector candidate accumulates the sum in float lanes and widens only
// at the end, while the scalar oracle accumulates in double throughout. That is a real numeric
// difference, not a defect in any library, and the harness reports it rather than hiding it behind
// a loose tolerance. Which accumulation the engine promises is a contract decision.

#include "Kernels.h"

#include "hwy/highway.h"

#include <cmath>
#include <limits>

HWY_BEFORE_NAMESPACE();

#include "hwy/contrib/math/math-inl.h"

namespace spike
{
// A namespace of its own: HighwayDynamic.cpp compiles the same bodies into spike::HWY_NAMESPACE for
// every target, and Highway's per-target namespace name is the same one for the shared target. Two
// definitions of spike::N_AVX2::transfer in one binary is a link error, so the static candidate
// keeps its copies out of the way.
namespace staticTarget
{
namespace HWY_NAMESPACE
{

#include "HighwayImpl.inl"

} // namespace HWY_NAMESPACE
} // namespace staticTarget
} // namespace spike

HWY_AFTER_NAMESPACE();

namespace spike
{
namespace
{
const KernelSet kSet{"highway-static",
                     &staticTarget::HWY_NAMESPACE::transfer,
                     &staticTarget::HWY_NAMESPACE::clampScale,
                     &staticTarget::HWY_NAMESPACE::reduce,
                     &staticTarget::HWY_NAMESPACE::strided,
                     &staticTarget::HWY_NAMESPACE::gamma,
                     &staticTarget::HWY_NAMESPACE::u8ToF32,
                     &staticTarget::HWY_NAMESPACE::i16ToF32,
                     &staticTarget::HWY_NAMESPACE::f32ToU16,
                     &staticTarget::HWY_NAMESPACE::f32ToU8,
                     &staticTarget::HWY_NAMESPACE::brightnessContrast,
                     &staticTarget::HWY_NAMESPACE::mtf,
                     &staticTarget::HWY_NAMESPACE::calibrate,
                     &staticTarget::HWY_NAMESPACE::calibrateFloat,
                     &staticTarget::HWY_NAMESPACE::u16BrightnessContrastFused,
                     &staticTarget::HWY_NAMESPACE::u16BrightnessContrastNative,
                     nullptr, // u16MtfLut - a table lookup, not a vector kernel
                     &staticTarget::HWY_NAMESPACE::u16Reduce};
} // namespace

const KernelSet& highwayKernels()
{
    return kSet;
}

const char* highwayStaticTargetName()
{
    return hwy::TargetName(HWY_STATIC_TARGET);
}

} // namespace spike
