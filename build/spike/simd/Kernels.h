// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Kernel contracts every candidate implements identically.
//
// Two groups. The first five are shape probes - each isolates one property of a SIMD abstraction
// (widening, fused arithmetic, horizontal reduction, tail handling, transcendentals) so a
// difference between candidates can be attributed to something specific.
//
// The rest are modelled on production image-processing kernels, because a choice made only on shape
// probes would be a choice about microbenchmarks. They are deliberately faithful, including the parts
// that look inefficient:
//
//   - `calibrate` promotes to double and divides per pixel behind a zero guard that produces NaN,
//     as a calibration that divides in double does. Half the lane count is part of what is being
//     measured, not an accident to optimize away here.
//     `calibrateFloat` is the same arithmetic in float, so the promotion has a price rather than a
//     reputation.
//   - `mtf` is the midtone transfer function, written in full: two boundary branches and two
//     divides. Branches become selects under vectorization, and whether a library
//     makes that natural is part of the question.
//   - The narrowing conversions clamp before rounding instead of relying on a saturating pack, so
//     every candidate computes the same thing and the comparison is of code generation rather than
//     of which library happens to expose saturation.
//
// Every candidate compiles to its own translation unit so object size is comparable, and every
// candidate is checked against the scalar implementation, which is the accuracy oracle.

#pragma once

#include <cstddef>
#include <cstdint>

namespace spike
{

/// Shared constants, so every candidate agrees on what the kernels compute.
namespace constants
{
inline constexpr float kU8Scale = 1.0f / 255.0f;
inline constexpr float kU16Scale = 1.0f / 65535.0f;
inline constexpr float kU8Max = 255.0f;
inline constexpr float kU16Max = 65535.0f;
inline constexpr float kPivot = 0.5f;
} // namespace constants

/// Horizontal reduction results, kept together so one pass can produce all three.
struct Reduction
{
    double sum = 0.0;
    float minimum = 0.0f;
    float maximum = 0.0f;
};

/// One candidate's implementation of the kernel set.
///
/// A null member means the candidate does not offer that kernel. That is reported as "not provided"
/// rather than quietly skipped, because an abstraction that cannot express a kernel ergonomically is
/// telling us something the timings cannot.
struct KernelSet
{
    const char* name = nullptr;

    // --- shape probes ---
    void (*transfer)(const std::uint16_t* src, float* dst, std::size_t count) = nullptr;
    void (*clampScale)(const float* src, float* dst, std::size_t count, float scale, float bias, float lo,
                       float hi) = nullptr;
    Reduction (*reduce)(const float* src, std::size_t count) = nullptr;
    void (*strided)(const float* src, std::size_t srcStride, float* dst, std::size_t dstStride, std::size_t width,
                    std::size_t height, float scale, float bias, float lo, float hi) = nullptr;
    void (*gamma)(const float* src, float* dst, std::size_t count, float exponent) = nullptr;

    // --- conversion matrix ---
    /// u8 to unit float. Four-to-one widening, against transfer's two-to-one.
    void (*u8ToF32)(const std::uint8_t* src, float* dst, std::size_t count) = nullptr;
    /// Signed 16-bit to unit float with a BZERO-style offset, as FITS integer frames arrive.
    void (*i16ToF32)(const std::int16_t* src, float* dst, std::size_t count, float offset, float scale) = nullptr;
    /// Unit float back to u16: clamp, round to nearest, narrow. The direction that needs saturation.
    void (*f32ToU16)(const float* src, std::uint16_t* dst, std::size_t count) = nullptr;
    /// Unit float back to u8: the same, narrowing four to one.
    void (*f32ToU8)(const float* src, std::uint8_t* dst, std::size_t count) = nullptr;

    // --- processes ---
    /// Brightness and contrast about a mid-grey pivot, clamped to unit range.
    void (*brightnessContrast)(const float* src, float* dst, std::size_t count, float brightness,
                               float contrast) = nullptr;
    /// The engine's midtone transfer function: two boundary branches and two divides.
    void (*mtf)(const float* src, float* dst, std::size_t count, float midtones, float lo, float hi) = nullptr;
    /// (light - dark) / flat * factor in double, flat == 0 producing NaN. As the engine does it.
    void (*calibrate)(const float* light, const float* dark, const float* flat, float* dst, std::size_t count,
                      double factor) = nullptr;
    /// The same calibration in float throughout, to price the engine's double promotion.
    void (*calibrateFloat)(const float* light, const float* dark, const float* flat, float* dst, std::size_t count,
                           float factor) = nullptr;

    // --- native 16-bit strategies ---
    //
    // These answer a different question from everything above: not "which abstraction", but
    // "should a 16-bit frame ever become a float buffer at all". Three strategies produce the same
    // u16 output from the same u16 input and are timed end to end, so the conversion passes the
    // float strategy needs are charged to it rather than hidden:
    //
    //   via f32   convert the frame to a float buffer, operate, convert back. Three passes over
    //             memory and a working set twice the size of the frame. This is what the engine
    //             does today, and the harness composes it from the kernels above rather than
    //             giving it a bespoke implementation.
    //   fused     widen to float in registers, operate, narrow, store. One pass, u16 memory
    //             traffic, identical arithmetic to the float path and therefore identical output.
    //   native    operate in 16-bit lanes with fixed-point arithmetic. One pass, u16 traffic, and
    //             twice the lanes per vector - but the arithmetic is lossy, and how lossy is one
    //             of the things being measured.

    /// Brightness/contrast, u16 in and u16 out, widening to float in registers only.
    void (*u16BrightnessContrastFused)(const std::uint16_t* src, std::uint16_t* dst, std::size_t count,
                                       float brightness, float contrast) = nullptr;
    /// The same, in 16-bit lanes throughout using saturating Q15 fixed point.
    void (*u16BrightnessContrastNative)(const std::uint16_t* src, std::uint16_t* dst, std::size_t count,
                                        float brightness, float contrast) = nullptr;
    /// MTF through a 65536-entry lookup table - an option only 16-bit input makes available.
    void (*u16MtfLut)(const std::uint16_t* src, std::uint16_t* dst, std::size_t count,
                      const std::uint16_t* lut) = nullptr;
    /// Sum, min and max straight off the u16 frame, with a widened accumulator.
    Reduction (*u16Reduce)(const std::uint16_t* src, std::size_t count) = nullptr;
};

const KernelSet& scalarKernels();
const KernelSet& autovecKernels();
const KernelSet& highwayKernels();
const KernelSet& highwayDynamicKernels();
const KernelSet& xsimdKernels();
const KernelSet& sleefKernels();
const KernelSet& opencvKernels();

/// Which instruction set each Highway candidate actually ran on. The static candidate is fixed at
/// compile time by the build flags; the dynamic one chooses at load time from every target compiled
/// in, so without this the dynamic row cannot be read - matching the static row means one thing if
/// both ran AVX2 and quite another if the dynamic one reached AVX-512 and gained nothing.
const char* highwayStaticTargetName();
const char* highwayBestDynamicTargetName();

} // namespace spike
