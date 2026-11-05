// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// scalar. The accuracy oracle: identical arithmetic to the auto-vectorization candidate, with vectorization forbidden.

#if defined(__clang__)
#define SPIKE_NO_VECTOR _Pragma("clang loop vectorize(disable) interleave(disable)")
#elif defined(_MSC_VER)
#define SPIKE_NO_VECTOR __pragma(loop(no_vector))
#else
#define SPIKE_NO_VECTOR
#endif
#define SPIKE_IMPL_NAMESPACE scalar_impl

#include "KernelBodies.inl"

namespace spike
{
namespace
{
const KernelSet kSet{"scalar",
                     &scalar_impl::transfer,
                     &scalar_impl::clampScale,
                     &scalar_impl::reduce,
                     &scalar_impl::strided,
                     &scalar_impl::gamma,
                     &scalar_impl::u8ToF32,
                     &scalar_impl::i16ToF32,
                     &scalar_impl::f32ToU16,
                     &scalar_impl::f32ToU8,
                     &scalar_impl::brightnessContrast,
                     &scalar_impl::mtf,
                     &scalar_impl::calibrate,
                     &scalar_impl::calibrateFloat,
                     &scalar_impl::u16BrightnessContrastFused,
                     &scalar_impl::u16BrightnessContrastNative,
                     &scalar_impl::u16MtfLut,
                     &scalar_impl::u16Reduce};
} // namespace

const KernelSet& scalarKernels()
{
    return kSet;
}

} // namespace spike
