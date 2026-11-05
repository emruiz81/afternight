// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// compiler-autovec. Identical arithmetic to the scalar reference, with vectorization permitted.

#define SPIKE_NO_VECTOR
#define SPIKE_IMPL_NAMESPACE autovec_impl

#include "KernelBodies.inl"

namespace spike
{
namespace
{
const KernelSet kSet{"compiler-autovec",
                     &autovec_impl::transfer,
                     &autovec_impl::clampScale,
                     &autovec_impl::reduce,
                     &autovec_impl::strided,
                     &autovec_impl::gamma,
                     &autovec_impl::u8ToF32,
                     &autovec_impl::i16ToF32,
                     &autovec_impl::f32ToU16,
                     &autovec_impl::f32ToU8,
                     &autovec_impl::brightnessContrast,
                     &autovec_impl::mtf,
                     &autovec_impl::calibrate,
                     &autovec_impl::calibrateFloat,
                     &autovec_impl::u16BrightnessContrastFused,
                     &autovec_impl::u16BrightnessContrastNative,
                     &autovec_impl::u16MtfLut,
                     &autovec_impl::u16Reduce};
} // namespace

const KernelSet& autovecKernels()
{
    return kSet;
}

} // namespace spike
