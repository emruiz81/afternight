// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the vector kernels, compiled once per instruction set, and the tables that
// reach them.
//
// This unit and its per-family siblings in `backends/cpu` - calibration, the demosaic methods,
// normalization, point-spread fitting, the stacking kernels and the warp - are the only translation
// units in the engine that contain instructions above the packaged floor, and the isolation is the
// library's rather than the build system's. The vector abstraction compiles each of them once per
// target with that target's own function attributes, so an AVX-512 body exists as an attributed
// function inside an otherwise ordinary object rather than as a whole object built with a raised
// `/arch` or `-m` switch. No unit is compiled with a flag above the packaged floor - the vector
// units take only the flags that complete the floor on a compiler whose floor switch implies less -
// which is what makes "no instruction set leaked into a translation unit that did not ask for one"
// a property of the arrangement rather than a thing to check.
//
// **The compiled set is declared here and asserted, not discovered later.** The engine's
// instruction-set ladder names exactly two rungs a vector kernel is written for - the packaged AVX2
// floor and the Skylake-server AVX-512 subset - so every other target the library would otherwise
// compile is disabled. That is worth more than the object size it saves: the baseline the library
// derives from the compiler flags is silent when it is wrong, and a build that lost a flag would
// quietly compile 128-bit code and call it the floor. Here it fails to compile instead.
//
// **What this build contains is not the same on both compilers, and that is a finding rather than a
// configuration.** The abstraction's own headers mark AVX-512 and everything above it broken under
// MSVC, so a Microsoft build of this engine compiles the AVX2 rung and no more, while a Clang build
// compiles both. The tables below are conditional on the compiled target set for that reason, and
// `Runtime/BackendDispatch.h` therefore answers "this build has no AVX-512 kernels" on one compiler
// and "it has" on the other - truthfully, on each.
//
// **A layout kernel sees a width, never a sample's own type.** Copying, filling, splitting, joining
// and byte-reversing do not read what a sample means, so they are written once per element width
// and reached through a table that maps the eight representations onto the four widths. Using an
// unsigned integer lane of that width is also what keeps the family's numeric contract: a run of
// `Float32` samples holding a signalling pattern is moved bit for bit, where a body that put those
// bits through a floating lane would be free to hand back a different pattern. The differential
// tests against `kernels/reference/LayoutKernels.h` are what prove it, on every width and every
// tail.

#include "backends/cpu/SimdKernelTables.h"

#include "backends/cpu/ConversionKernelsCpu.h"
#include "backends/cpu/DemosaicKernelsCpu.h"
#include "kernels/reference/AlgorithmKernels.h"
#include "kernels/reference/ConversionKernels.h"
#include "kernels/reference/CurveTableKernels.h"
#include "kernels/reference/LayoutKernels.h"
#include "kernels/reference/OrderCountKernels.h"

#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

// Exactly the two rungs the instruction-set ladder names, and nothing else the library would
// otherwise compile. Left overridable so the budget tooling can compile this unit with one target
// at a time and report what the second one costs; every build of the engine takes the default.
#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

// This engine uses Highway as a header-only vector abstraction and owns runtime dispatch itself.
// Keep even Debug builds independent of libhwy: Highway's debug assertions otherwise refer to its
// out-of-line Abort implementation, although Release optimises those references away.
#define HWY_HEADER_ONLY
// The shared vcpkg headers otherwise mark even inline debug assertions as DLL imports.
// This translation unit uses only header definitions, including those assertions.
#define HWY_DLLEXPORT

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/SimdKernelTables.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

// The transcendental polynomials, which the default port of the vector library does not install:
// this engine asks for the contributed math component by name, and a tree without it fails to
// compile this line rather than quietly building a slower engine.
#include <hwy/contrib/math/math-inl.h>

#if HWY_ARCH_X86
#include <xmmintrin.h>
#endif

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace simd_detail
{
namespace HWY_NAMESPACE
{

namespace hn = hwy::HWY_NAMESPACE;

/// Which rung of the engine's ladder the code in this compilation of the file belongs to.
///
/// Derived from the target the vector abstraction is compiling for rather than written beside the
/// table that points at it, so a table wired to the wrong implementation disagrees with itself and
/// a selection test sees that before a wrong sample does.
///
/// @return the instruction set this body was compiled for.
[[nodiscard]] InstructionSet isaOfCompiledCode() noexcept
{
#if HWY_TARGET == HWY_AVX3
    return InstructionSet::Avx512Skylake;
#else
    return InstructionSet::Avx2;
#endif
}

/// Copies one run of samples of one element width.
///
/// @tparam LaneT the unsigned integer of the element's width.
/// @param source where the samples to copy start.
/// @param destination where the copies go.
/// @param count how many samples.
template <typename LaneT> void copyRunOf(const void* source, void* destination, std::uint64_t count) noexcept
{
    const hn::ScalableTag<LaneT> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const LaneT* const in = static_cast<const LaneT*>(source);
    LaneT* const out = static_cast<LaneT*>(destination);

    std::uint64_t index = 0u;
    for (; index + lanes <= count; index += lanes)
    {
        hn::StoreU(hn::LoadU(tag, in + index), tag, out + index);
    }
    for (; index < count; ++index)
    {
        out[index] = in[index];
    }
}

/// Writes one sample value over a whole run of one element width.
///
/// @tparam LaneT the unsigned integer of the element's width.
/// @param value the sample to repeat.
/// @param destination where the run starts.
/// @param count how many samples.
template <typename LaneT> void fillRunOf(const void* value, void* destination, std::uint64_t count) noexcept
{
    LaneT pattern{};
    std::memcpy(&pattern, value, sizeof(LaneT));

    const hn::ScalableTag<LaneT> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const auto broadcast = hn::Set(tag, pattern);
    LaneT* const out = static_cast<LaneT*>(destination);

    std::uint64_t index = 0u;
    for (; index + lanes <= count; index += lanes)
    {
        hn::StoreU(broadcast, tag, out + index);
    }
    for (; index < count; ++index)
    {
        out[index] = pattern;
    }
}

/// Splits one interleaved run into one contiguous run per channel.
///
/// The two, three and four cases are separate because the abstraction provides a single load for
/// each of them: one pass over the interleaved run produces every channel, where a strided copy
/// would read it once per channel.
///
/// @tparam LaneT the unsigned integer of the element's width.
/// @param source where the interleaved samples start.
/// @param destinations one contiguous run per channel, in channel order.
/// @param channelCount how many samples one pixel holds.
/// @param pixelCount how many pixels.
template <typename LaneT>
void deinterleaveRunOf(const void* source, void* const* destinations, std::uint32_t channelCount,
                       std::uint64_t pixelCount) noexcept
{
    const hn::ScalableTag<LaneT> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const LaneT* const in = static_cast<const LaneT*>(source);

    std::uint64_t pixel = 0u;
    if (channelCount == 2u)
    {
        LaneT* const out0 = static_cast<LaneT*>(destinations[0]);
        LaneT* const out1 = static_cast<LaneT*>(destinations[1]);
        for (; pixel + lanes <= pixelCount; pixel += lanes)
        {
            hn::Vec<decltype(tag)> v0;
            hn::Vec<decltype(tag)> v1;
            hn::LoadInterleaved2(tag, in + pixel * 2u, v0, v1);
            hn::StoreU(v0, tag, out0 + pixel);
            hn::StoreU(v1, tag, out1 + pixel);
        }
    }
    else if (channelCount == 3u)
    {
        LaneT* const out0 = static_cast<LaneT*>(destinations[0]);
        LaneT* const out1 = static_cast<LaneT*>(destinations[1]);
        LaneT* const out2 = static_cast<LaneT*>(destinations[2]);
        for (; pixel + lanes <= pixelCount; pixel += lanes)
        {
            hn::Vec<decltype(tag)> v0;
            hn::Vec<decltype(tag)> v1;
            hn::Vec<decltype(tag)> v2;
            hn::LoadInterleaved3(tag, in + pixel * 3u, v0, v1, v2);
            hn::StoreU(v0, tag, out0 + pixel);
            hn::StoreU(v1, tag, out1 + pixel);
            hn::StoreU(v2, tag, out2 + pixel);
        }
    }
    else if (channelCount == 4u)
    {
        LaneT* const out0 = static_cast<LaneT*>(destinations[0]);
        LaneT* const out1 = static_cast<LaneT*>(destinations[1]);
        LaneT* const out2 = static_cast<LaneT*>(destinations[2]);
        LaneT* const out3 = static_cast<LaneT*>(destinations[3]);
        for (; pixel + lanes <= pixelCount; pixel += lanes)
        {
            hn::Vec<decltype(tag)> v0;
            hn::Vec<decltype(tag)> v1;
            hn::Vec<decltype(tag)> v2;
            hn::Vec<decltype(tag)> v3;
            hn::LoadInterleaved4(tag, in + pixel * 4u, v0, v1, v2, v3);
            hn::StoreU(v0, tag, out0 + pixel);
            hn::StoreU(v1, tag, out1 + pixel);
            hn::StoreU(v2, tag, out2 + pixel);
            hn::StoreU(v3, tag, out3 + pixel);
        }
    }

    for (; pixel < pixelCount; ++pixel)
    {
        for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
        {
            static_cast<LaneT*>(destinations[channel])[pixel] = in[pixel * channelCount + channel];
        }
    }
}

/// Joins one contiguous run per channel into one interleaved run.
///
/// @tparam LaneT the unsigned integer of the element's width.
/// @param sources one contiguous run per channel, in channel order.
/// @param destination where the interleaved samples go.
/// @param channelCount how many samples one pixel holds.
/// @param pixelCount how many pixels.
template <typename LaneT>
void interleaveRunOf(const void* const* sources, void* destination, std::uint32_t channelCount,
                     std::uint64_t pixelCount) noexcept
{
    const hn::ScalableTag<LaneT> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    LaneT* const out = static_cast<LaneT*>(destination);

    std::uint64_t pixel = 0u;
    if (channelCount == 2u)
    {
        const LaneT* const in0 = static_cast<const LaneT*>(sources[0]);
        const LaneT* const in1 = static_cast<const LaneT*>(sources[1]);
        for (; pixel + lanes <= pixelCount; pixel += lanes)
        {
            hn::StoreInterleaved2(hn::LoadU(tag, in0 + pixel), hn::LoadU(tag, in1 + pixel), tag, out + pixel * 2u);
        }
    }
    else if (channelCount == 3u)
    {
        const LaneT* const in0 = static_cast<const LaneT*>(sources[0]);
        const LaneT* const in1 = static_cast<const LaneT*>(sources[1]);
        const LaneT* const in2 = static_cast<const LaneT*>(sources[2]);
        for (; pixel + lanes <= pixelCount; pixel += lanes)
        {
            hn::StoreInterleaved3(hn::LoadU(tag, in0 + pixel), hn::LoadU(tag, in1 + pixel), hn::LoadU(tag, in2 + pixel),
                                  tag, out + pixel * 3u);
        }
    }
    else if (channelCount == 4u)
    {
        const LaneT* const in0 = static_cast<const LaneT*>(sources[0]);
        const LaneT* const in1 = static_cast<const LaneT*>(sources[1]);
        const LaneT* const in2 = static_cast<const LaneT*>(sources[2]);
        const LaneT* const in3 = static_cast<const LaneT*>(sources[3]);
        for (; pixel + lanes <= pixelCount; pixel += lanes)
        {
            hn::StoreInterleaved4(hn::LoadU(tag, in0 + pixel), hn::LoadU(tag, in1 + pixel), hn::LoadU(tag, in2 + pixel),
                                  hn::LoadU(tag, in3 + pixel), tag, out + pixel * 4u);
        }
    }

    for (; pixel < pixelCount; ++pixel)
    {
        for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
        {
            out[pixel * channelCount + channel] = static_cast<const LaneT*>(sources[channel])[pixel];
        }
    }
}

/// Reverses the bytes of one run of samples, in place.
///
/// @tparam LaneT the unsigned integer of the element's width, which is at least two bytes.
/// @param samples where the run starts.
/// @param count how many samples.
template <typename LaneT> void byteSwapRunOf(void* samples, std::uint64_t count) noexcept
{
    const hn::ScalableTag<LaneT> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    LaneT* const values = static_cast<LaneT*>(samples);

    std::uint64_t index = 0u;
    for (; index + lanes <= count; index += lanes)
    {
        hn::StoreU(hn::ReverseLaneBytes(hn::LoadU(tag, values + index)), tag, values + index);
    }
    for (; index < count; ++index)
    {
        LaneT value = values[index];
        LaneT reversed{};
        for (std::size_t byte = 0u; byte < sizeof(LaneT); ++byte)
        {
            reversed = static_cast<LaneT>((reversed << 8u) | (value & LaneT{0xFF}));
            value = static_cast<LaneT>(value >> 8u);
        }
        values[index] = reversed;
    }
}

// Declared ahead of the pointwise bodies that raise to a power, and defined and documented with the
// transcendental bodies further down.
template <class TagT>
[[nodiscard]] hn::Vec<TagT> powerOf(TagT tag, hn::Vec<TagT> value, hn::Vec<TagT> exponent) noexcept;

template <class TagT> [[nodiscard]] hn::Vec<TagT> raisedToNothingOf(TagT tag, hn::Vec<TagT> value) noexcept;

/// A table's entries at lane indices, each as the lanes' type: from a table of the lanes' own type directly, and from
/// a Float64 table into Float32 lanes as the entry narrowed to nearest, as `static_cast<float>` narrows it, gathered
/// as Float64 halves and narrowed together.
template <class D, typename TableT>
[[nodiscard]] HWY_INLINE hn::Vec<D> gatheredEntries(D tag, const TableT* table,
                                                    hn::Vec<hn::RebindToSigned<D>> index) noexcept
{
    if constexpr (std::is_same_v<hn::TFromD<D>, TableT>)
    {
        return hn::GatherIndex(tag, table, index);
    }
    else
    {
        const hn::Repartition<double, D> wide;
        const hn::RebindToSigned<decltype(wide)> wideIndex;
        const hn::Half<D> half;
        const hn::Half<hn::RebindToSigned<D>> halfIndex;
        const auto lower = hn::GatherIndex(wide, table, hn::PromoteTo(wideIndex, hn::LowerHalf(halfIndex, index)));
        const auto upper = hn::GatherIndex(wide, table, hn::PromoteTo(wideIndex, hn::UpperHalf(halfIndex, index)));
        return hn::Combine(tag, hn::DemoteTo(half, upper), hn::DemoteTo(half, lower));
    }
}

/// The entries at lane indices and at the indices after them, of a table of the lanes' own type: each pair one
/// 64-bit gather from the entry's address, separated into the two vectors, which reads half the elements two
/// gathers of single entries would.
template <class D>
HWY_INLINE void gatheredPairs(D tag, const hn::TFromD<D>* table, hn::Vec<hn::RebindToSigned<D>> index,
                              hn::Vec<D>& first, hn::Vec<D>& second) noexcept
{
    const hn::Repartition<std::int64_t, D> pairs;
    const hn::Half<hn::RebindToSigned<D>> halfIndex;
    const std::int64_t* const base = reinterpret_cast<const std::int64_t*>(table);
    const auto lowerOffsets = hn::ShiftLeft<2>(hn::PromoteTo(pairs, hn::LowerHalf(halfIndex, index)));
    const auto upperOffsets = hn::ShiftLeft<2>(hn::PromoteTo(pairs, hn::UpperHalf(halfIndex, index)));
    const auto lower = hn::BitCast(tag, hn::GatherOffset(pairs, base, lowerOffsets));
    const auto upper = hn::BitCast(tag, hn::GatherOffset(pairs, base, upperOffsets));
    first = hn::ConcatEven(tag, upper, lower);
    second = hn::ConcatOdd(tag, upper, lower);
}

/// One sampled-curve step over a vector, as the evaluator computes it at the lanes' width.
///
/// A lane's value becomes a fractional table position - its distance from the low end over the domain's width,
/// times the last index, in that order; a domain one wide divides by one, which changes no value, so the division
/// is left out. A not-a-number becomes the quiet not-a-number and never an index; under `Clamp` a position below the
/// table reads the first entry and one above it the last; under `LinearExtrapolation` it extends the end segment.
/// Linear sampling reads the two entries about the position and interpolates them, returning the first unchanged
/// when the two are equal, and nearest sampling reads the entry the position rounds half up to. The table is the
/// program's, or the program's narrowed to the lanes' type, which is what the evaluator reads from it.
/// `pointwiseRunFor` and `sampledCurveFloat32RunFor` hand neither a step that rejects a value outside its domain nor
/// one that reads the entry below the position.
template <class D, typename TableT> class SampledCurveLanes
{
public:
    SampledCurveLanes(D tag, const PointwiseOp& op, const TableT* entries, std::size_t count) noexcept
        : m_entries(entries), m_nearest(op.sampling == PointwiseCurveSampling::Nearest),
          m_clamp(op.domainPolicy == PointwiseCurveDomainPolicy::Clamp),
          m_divide(static_cast<LaneT>(op.high) - static_cast<LaneT>(op.low) != static_cast<LaneT>(1)),
          m_low(hn::Set(tag, static_cast<LaneT>(op.low))),
          m_span(hn::Set(tag, static_cast<LaneT>(op.high) - static_cast<LaneT>(op.low))),
          m_last(hn::Set(tag, static_cast<LaneT>(count - 1u))),
          m_lastLower(hn::Set(tag, static_cast<LaneT>(count - 2u)))
    {
    }

    [[nodiscard]] HWY_INLINE hn::Vec<D> apply(D tag, hn::Vec<D> value) const noexcept
    {
        const hn::RebindToSigned<D> indexTag;
        const auto zero = hn::Zero(tag);
        const auto offset = hn::Sub(value, m_low);
        auto position = hn::Mul(m_divide ? hn::Div(offset, m_span) : offset, m_last);
        const auto notANumber = hn::IsNaN(position);
        if (m_clamp)
        {
            position = hn::IfThenElse(hn::Lt(position, zero), zero,
                                      hn::IfThenElse(hn::Gt(position, m_last), m_last, position));
        }
        // A masked select does not make the gather on its other arm harmless, so a not-a-number lane reads the
        // first entry and is replaced afterwards.
        position = hn::IfThenElse(notANumber, zero, position);
        hn::Vec<D> curved;
        if (m_nearest)
        {
            auto rounded = hn::Floor(hn::Add(position, hn::Set(tag, static_cast<LaneT>(0.5))));
            rounded = hn::IfThenElse(hn::Lt(rounded, zero), zero, rounded);
            rounded = hn::IfThenElse(hn::Lt(m_last, rounded), m_last, rounded);
            curved = gatheredEntries(tag, m_entries, hn::ConvertTo(indexTag, rounded));
        }
        else
        {
            const auto lower =
                hn::IfThenElse(hn::Le(position, zero), zero,
                               hn::IfThenElse(hn::Ge(position, m_last), m_lastLower, hn::Floor(position)));
            const auto fraction = hn::Sub(position, lower);
            const auto index = hn::ConvertTo(indexTag, lower);
            hn::Vec<D> first;
            hn::Vec<D> second;
            if constexpr (std::is_same_v<TableT, float> && std::is_same_v<LaneT, float>)
            {
                gatheredPairs(tag, m_entries, index, first, second);
            }
            else
            {
                first = gatheredEntries(tag, m_entries, index);
                second = gatheredEntries(tag, m_entries + 1, index);
            }
            curved =
                hn::IfThenElse(hn::Eq(first, second), first, hn::Add(first, hn::Mul(hn::Sub(second, first), fraction)));
        }
        return hn::IfThenElse(notANumber, hn::Set(tag, std::numeric_limits<LaneT>::quiet_NaN()), curved);
    }

private:
    using LaneT = hn::TFromD<D>;

    const TableT* m_entries;
    bool m_nearest;
    bool m_clamp;
    bool m_divide;
    hn::Vec<D> m_low;
    hn::Vec<D> m_span;
    hn::Vec<D> m_last;
    hn::Vec<D> m_lastLower;
};

/// A Float32 program that is one sampled curve over a contiguous run, read from the table narrowed to Float32; see
/// `SampledCurveFloat32RunFn`. The run's last partial vector is read and written lane by lane.
std::uint64_t sampledCurveFloat32Run(const float* source, float* destination, std::uint64_t count,
                                     const PointwiseOp& step, const float* table, std::uint32_t entries) noexcept
{
    const hn::ScalableTag<float> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const SampledCurveLanes<decltype(tag), float> curve(tag, step, table, entries);
    std::uint64_t nonFinite = 0u;
    std::uint64_t index = 0u;
    for (; index + lanes <= count; index += lanes)
    {
        const auto value = curve.apply(tag, hn::LoadU(tag, source + index));
        nonFinite += static_cast<std::uint64_t>(hn::CountTrue(tag, hn::Not(hn::IsFinite(value))));
        hn::StoreU(value, tag, destination + index);
    }
    if (index < count)
    {
        const std::size_t rest = static_cast<std::size_t>(count - index);
        const auto value = curve.apply(tag, hn::LoadN(tag, source + index, rest));
        nonFinite += static_cast<std::uint64_t>(
            hn::CountTrue(tag, hn::And(hn::FirstN(tag, rest), hn::Not(hn::IsFinite(value)))));
        hn::StoreN(value, tag, destination + index, rest);
    }
    return nonFinite;
}

/// Executes a program that is one sampled curve, the curves process's shape, without interpreting it per vector.
template <typename LaneT>
[[nodiscard]] PointwiseRunOutcome pointwiseSampledCurveRunOf(const PointwiseProgram& program,
                                                             const void* const* sources, void* const* destinations,
                                                             std::uint64_t pixelCount) noexcept
{
    const hn::ScalableTag<LaneT> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const std::uint64_t vectorPixels = pixelCount - pixelCount % lanes;
    const PointwiseOp& op = program.steps().front();
    const std::span<const double> table = program.table(op.table);
    const SampledCurveLanes<decltype(tag), double> curve(tag, op, table.data(), table.size());
    std::uint64_t nonFinite = 0u;
    for (std::uint32_t channel = 0u; channel < program.channelCount(); ++channel)
    {
        const LaneT* const source = static_cast<const LaneT*>(sources[channel]);
        LaneT* const destination = static_cast<LaneT*>(destinations[channel]);
        for (std::uint64_t pixel = 0u; pixel < vectorPixels; pixel += lanes)
        {
            const auto value = curve.apply(tag, hn::LoadU(tag, source + pixel));
            nonFinite += static_cast<std::uint64_t>(hn::CountTrue(tag, hn::Not(hn::IsFinite(value))));
            hn::StoreU(value, tag, destination + pixel);
        }
    }
    return PointwiseRunOutcome{.pixelsProcessed = vectorPixels, .nonFiniteOutputs = nonFinite};
}

/// Executes the canonical scale-and-clamp shape without interpreting it once per vector.
template <typename LaneT>
[[nodiscard]] PointwiseRunOutcome pointwiseScaleBiasClampRunOf(const PointwiseProgram& program,
                                                               const void* const* sources, void* const* destinations,
                                                               std::uint64_t pixelCount) noexcept
{
    const hn::ScalableTag<LaneT> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const std::uint64_t vectorPixels = pixelCount - pixelCount % lanes;
    const std::span<const PointwiseOp> steps = program.steps();
    const auto factor = hn::Set(tag, static_cast<LaneT>(steps[0].factor));
    const auto bias = hn::Set(tag, static_cast<LaneT>(steps[1].offset));
    const auto low = hn::Set(tag, static_cast<LaneT>(steps[2].low));
    const auto high = hn::Set(tag, static_cast<LaneT>(steps[2].high));
    auto nonFiniteLanes = hn::Zero(tag);
    for (std::uint32_t channel = 0u; channel < program.channelCount(); ++channel)
    {
        const LaneT* const source = static_cast<const LaneT*>(sources[channel]);
        LaneT* const destination = static_cast<LaneT*>(destinations[channel]);
        for (std::uint64_t pixel = 0u; pixel < vectorPixels; pixel += lanes)
        {
            auto value = hn::Add(hn::Mul(hn::LoadU(tag, source + pixel), factor), bias);
            value = hn::IfThenElse(hn::Lt(value, low), low, hn::IfThenElse(hn::Gt(value, high), high, value));
            nonFiniteLanes = hn::Add(
                nonFiniteLanes, hn::IfThenElse(hn::Not(hn::IsFinite(value)), hn::Set(tag, LaneT{1}), hn::Zero(tag)));
            hn::StoreU(value, tag, destination + pixel);
        }
    }
    return PointwiseRunOutcome{.pixelsProcessed = vectorPixels,
                               .nonFiniteOutputs = static_cast<std::uint64_t>(hn::ReduceSum(tag, nonFiniteLanes))};
}

/// Executes the canonical scaled-add-and-clamp shape without interpreting it once per vector.
template <typename LaneT>
[[nodiscard]] PointwiseRunOutcome pointwiseAddScaleBiasClampRunOf(const PointwiseProgram& program,
                                                                  const void* const* sources, void* const* destinations,
                                                                  std::uint64_t pixelCount) noexcept
{
    const hn::ScalableTag<LaneT> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const std::uint64_t vectorPixels = pixelCount - pixelCount % lanes;
    const std::span<const PointwiseOp> steps = program.steps();
    const std::uint32_t channels = program.channelCount();
    const std::uint32_t operand = steps[0].operand;
    const auto addFactor = hn::Set(tag, static_cast<LaneT>(steps[0].factor));
    const auto scaleFactor = hn::Set(tag, static_cast<LaneT>(steps[1].factor));
    const auto bias = hn::Set(tag, static_cast<LaneT>(steps[2].offset));
    const auto low = hn::Set(tag, static_cast<LaneT>(steps[3].low));
    const auto high = hn::Set(tag, static_cast<LaneT>(steps[3].high));
    auto nonFiniteLanes = hn::Zero(tag);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const LaneT* const primary = static_cast<const LaneT*>(sources[channel]);
        const LaneT* const secondary = static_cast<const LaneT*>(sources[operand * channels + channel]);
        LaneT* const destination = static_cast<LaneT*>(destinations[channel]);
        for (std::uint64_t pixel = 0u; pixel < vectorPixels; pixel += lanes)
        {
            auto value =
                hn::Add(hn::LoadU(tag, primary + pixel), hn::Mul(hn::LoadU(tag, secondary + pixel), addFactor));
            value = hn::Add(hn::Mul(value, scaleFactor), bias);
            value = hn::IfThenElse(hn::Lt(value, low), low, hn::IfThenElse(hn::Gt(value, high), high, value));
            nonFiniteLanes = hn::Add(
                nonFiniteLanes, hn::IfThenElse(hn::Not(hn::IsFinite(value)), hn::Set(tag, LaneT{1}), hn::Zero(tag)));
            hn::StoreU(value, tag, destination + pixel);
        }
    }
    return PointwiseRunOutcome{.pixelsProcessed = vectorPixels,
                               .nonFiniteOutputs = static_cast<std::uint64_t>(hn::ReduceSum(tag, nonFiniteLanes))};
}

/// Executes one vectorizable pointwise program with the lanes of this compiled target.
///
/// The three canonical shapes above are recognised first; each writes the vector-width prefix of the run and reports
/// its length, and the caller evaluates the remaining pixels itself, with the same arithmetic. Otherwise the steps are
/// interpreted per vector, a scale followed by a bias becoming one fused multiply-add only when the program allows
/// contraction, and the run's last partial vector goes through the same body, loaded and stored only as far as the run
/// goes, so the whole run is written: a Float32 gamma's vector power is not the evaluator's library power in its last
/// bits, and a sample's power would otherwise depend on where in its row it sat - inside the row's vectors or in its
/// tail, which a part of the image, a split across workers or the target's lane count moves. A gamma is evaluated for
/// Float32 lanes only, and
/// `pointwiseRunFor` never selects this body for a program holding a Float64 gamma, a table, a
/// channel matrix or a sampled curve it has no lane body for, which is why those steps do nothing here.
template <typename LaneT>
[[nodiscard]] PointwiseRunOutcome pointwiseRunOf(const PointwiseProgram& program, const void* const* sources,
                                                 void* const* destinations, std::uint64_t pixelCount) noexcept
{
    const hn::ScalableTag<LaneT> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const std::uint32_t channels = program.channelCount();
    const std::span<const PointwiseOp> steps = program.steps();
    if (program.sourceCount() == 1u && steps.size() == 3u && steps[0].kind == PointwiseOpKind::Scale &&
        steps[1].kind == PointwiseOpKind::Bias && steps[2].kind == PointwiseOpKind::Clamp)
    {
        return pointwiseScaleBiasClampRunOf<LaneT>(program, sources, destinations, pixelCount);
    }
    if (program.sourceCount() == 2u && steps.size() == 4u && steps[0].kind == PointwiseOpKind::Add &&
        steps[1].kind == PointwiseOpKind::Scale && steps[2].kind == PointwiseOpKind::Bias &&
        steps[3].kind == PointwiseOpKind::Clamp)
    {
        return pointwiseAddScaleBiasClampRunOf<LaneT>(program, sources, destinations, pixelCount);
    }
    if (program.sourceCount() == 1u && steps.size() == 1u && steps[0].kind == PointwiseOpKind::SampledCurve)
    {
        return pointwiseSampledCurveRunOf<LaneT>(program, sources, destinations, pixelCount);
    }
    std::uint64_t nonFinite = 0u;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const LaneT* const primary = static_cast<const LaneT*>(sources[channel]);
        LaneT* const destination = static_cast<LaneT*>(destinations[channel]);
        for (std::uint64_t pixel = 0u; pixel < pixelCount; pixel += lanes)
        {
            const std::size_t live = static_cast<std::size_t>((std::min)(lanes, pixelCount - pixel));
            const bool whole = live == static_cast<std::size_t>(lanes);
            auto value = whole ? hn::LoadU(tag, primary + pixel) : hn::LoadN(tag, primary + pixel, live);
            for (std::size_t step = 0u; step < steps.size(); ++step)
            {
                const PointwiseOp& op = steps[step];
                switch (op.kind)
                {
                case PointwiseOpKind::Scale: {
                    const auto factor = hn::Set(tag, static_cast<LaneT>(op.factor));
                    if (program.contractionAllowed() && step + 1u < steps.size() &&
                        steps[step + 1u].kind == PointwiseOpKind::Bias)
                    {
                        value = hn::MulAdd(value, factor, hn::Set(tag, static_cast<LaneT>(steps[step + 1u].offset)));
                        ++step;
                    }
                    else
                    {
                        value = hn::Mul(value, factor);
                    }
                    break;
                }
                case PointwiseOpKind::Bias:
                    value = hn::Add(value, hn::Set(tag, static_cast<LaneT>(op.offset)));
                    break;
                case PointwiseOpKind::Clamp: {
                    const auto low = hn::Set(tag, static_cast<LaneT>(op.low));
                    const auto high = hn::Set(tag, static_cast<LaneT>(op.high));
                    value = hn::IfThenElse(hn::Lt(value, low), low, hn::IfThenElse(hn::Gt(value, high), high, value));
                    break;
                }
                case PointwiseOpKind::Mask: {
                    const LaneT* const operand =
                        static_cast<const LaneT*>(sources[op.operand * channels + channel]) + pixel;
                    value = hn::Mul(value, whole ? hn::LoadU(tag, operand) : hn::LoadN(tag, operand, live));
                    break;
                }
                case PointwiseOpKind::Blend: {
                    const auto weight = hn::Set(tag, static_cast<LaneT>(op.factor));
                    const auto complement = hn::Sub(hn::Set(tag, static_cast<LaneT>(1)), weight);
                    const LaneT* const operand =
                        static_cast<const LaneT*>(sources[op.operand * channels + channel]) + pixel;
                    const auto toward = whole ? hn::LoadU(tag, operand) : hn::LoadN(tag, operand, live);
                    value = hn::Add(hn::Mul(value, complement), hn::Mul(toward, weight));
                    break;
                }
                case PointwiseOpKind::Add:
                case PointwiseOpKind::Subtract:
                case PointwiseOpKind::Multiply:
                case PointwiseOpKind::Divide:
                case PointwiseOpKind::Minimum:
                case PointwiseOpKind::Maximum:
                case PointwiseOpKind::Compare: {
                    const LaneT* const operandSamples =
                        static_cast<const LaneT*>(sources[op.operand * channels + channel]) + pixel;
                    const auto operand = whole ? hn::LoadU(tag, operandSamples) : hn::LoadN(tag, operandSamples, live);
                    const auto factor = hn::Set(tag, static_cast<LaneT>(op.factor));
                    switch (op.kind)
                    {
                    case PointwiseOpKind::Add:
                        value = hn::Add(value, hn::Mul(operand, factor));
                        break;
                    case PointwiseOpKind::Subtract:
                        value = hn::Sub(value, hn::Mul(operand, factor));
                        break;
                    case PointwiseOpKind::Multiply:
                        value = hn::Mul(hn::Mul(value, operand), factor);
                        break;
                    case PointwiseOpKind::Divide:
                        value = hn::Mul(hn::Div(value, operand), factor);
                        break;
                    case PointwiseOpKind::Minimum:
                        value = hn::IfThenElse(hn::IsNaN(value), operand,
                                               hn::IfThenElse(hn::IsNaN(operand), value, hn::Min(value, operand)));
                        break;
                    case PointwiseOpKind::Maximum:
                        value = hn::IfThenElse(hn::IsNaN(value), operand,
                                               hn::IfThenElse(hn::IsNaN(operand), value, hn::Max(value, operand)));
                        break;
                    case PointwiseOpKind::Compare: {
                        const auto zero = hn::Zero(tag);
                        const auto one = hn::Set(tag, static_cast<LaneT>(1));
                        switch (op.comparison)
                        {
                        case PointwiseComparison::Less:
                            value = hn::IfThenElse(hn::Lt(value, operand), one, zero);
                            break;
                        case PointwiseComparison::LessOrEqual:
                            value = hn::IfThenElse(hn::Or(hn::Lt(value, operand), hn::Eq(value, operand)), one, zero);
                            break;
                        case PointwiseComparison::Equal:
                            value = hn::IfThenElse(hn::Eq(value, operand), one, zero);
                            break;
                        case PointwiseComparison::NotEqual:
                            value = hn::IfThenElse(hn::Not(hn::Eq(value, operand)), one, zero);
                            break;
                        case PointwiseComparison::GreaterOrEqual:
                            value = hn::IfThenElse(hn::Or(hn::Gt(value, operand), hn::Eq(value, operand)), one, zero);
                            break;
                        case PointwiseComparison::Greater:
                            value = hn::IfThenElse(hn::Gt(value, operand), one, zero);
                            break;
                        }
                        break;
                    }
                    default:
                        break;
                    }
                    break;
                }
                case PointwiseOpKind::Gamma:
                    if constexpr (std::is_same_v<LaneT, float>)
                    {
                        const float exponent = static_cast<float>(op.factor);
                        value = exponent == 0.0f ? raisedToNothingOf(tag, value)
                                                 : powerOf(tag, value, hn::Set(tag, static_cast<LaneT>(exponent)));
                    }
                    break;
                case PointwiseOpKind::SampledCurve: {
                    const std::span<const double> table = program.table(op.table);
                    value =
                        SampledCurveLanes<decltype(tag), double>(tag, op, table.data(), table.size()).apply(tag, value);
                    break;
                }
                case PointwiseOpKind::Lut:
                case PointwiseOpKind::ChannelMatrix:
                case PointwiseOpKind::Unknown:
                    break;
                }
            }
            if (whole)
            {
                nonFinite += static_cast<std::uint64_t>(hn::CountTrue(tag, hn::Not(hn::IsFinite(value))));
                hn::StoreU(value, tag, destination + pixel);
            }
            else
            {
                nonFinite += static_cast<std::uint64_t>(
                    hn::CountTrue(tag, hn::And(hn::FirstN(tag, live), hn::Not(hn::IsFinite(value)))));
                hn::StoreN(value, tag, destination + pixel, live);
            }
        }
    }
    return PointwiseRunOutcome{.pixelsProcessed = pixelCount, .nonFiniteOutputs = nonFinite};
}

/// `count` integer samples from `samples`, as many as the lanes hold or fewer, widened to the lanes' type exactly; a
/// lane past `count` holds zero.
///
/// A code of eight or sixteen bits becomes a 32-bit integer and then the lanes' value; a 32-bit one becomes a Float64
/// directly, an unsigned one through its two's-complement image, moved back by 2^31 once it is a Float64, where both
/// steps are exact. The evaluator reads each sample as `double` and narrows it to the program's width, which these
/// codes survive unchanged, so the lanes hold what it holds.
template <typename SampleT, class D>
[[nodiscard]] HWY_INLINE hn::Vec<D> widenedIntegers(D tag, const SampleT* samples, std::size_t count) noexcept
{
    using LaneT = hn::TFromD<D>;
    const hn::Rebind<SampleT, D> sampleTag;
    const auto loaded = count == hn::Lanes(tag) ? hn::LoadU(sampleTag, samples) : hn::LoadN(sampleTag, samples, count);
    if constexpr (std::is_same_v<SampleT, std::uint32_t>)
    {
        const hn::Rebind<std::int32_t, D> signedTag;
        const auto image = hn::BitCast(signedTag, hn::Xor(loaded, hn::Set(sampleTag, 0x80000000u)));
        return hn::Add(hn::PromoteTo(tag, image), hn::Set(tag, 2147483648.0));
    }
    else if constexpr (std::is_same_v<SampleT, std::int32_t>)
    {
        return hn::PromoteTo(tag, loaded);
    }
    else
    {
        const hn::Rebind<std::int32_t, D> wideTag;
        const auto promoted = hn::PromoteTo(wideTag, loaded);
        if constexpr (std::is_same_v<LaneT, float>)
        {
            return hn::ConvertTo(tag, promoted);
        }
        else
        {
            return hn::PromoteTo(tag, promoted);
        }
    }
}

/// Lanes narrowed to an integer representation as the evaluator's conversion narrows a sample - not a number to zero,
/// the infinities and every value beyond the representation to its ends, everything else to the nearest code, ties to
/// even - and stored to the first `count` samples at `samples`, with what the narrowing met added to the counts.
///
/// The ends of every representation narrowed here are whole numbers the lanes hold exactly, so the clamp is the
/// evaluator's clamp of the value as a `double`, and a rounding of a value inside them is exact at either width.
template <typename SampleT, class D>
HWY_INLINE void storeNarrowedIntegers(D tag, hn::Vec<D> value, SampleT* samples, std::size_t count,
                                      std::uint64_t& nonFinite, std::uint64_t& saturated) noexcept
{
    using LaneT = hn::TFromD<D>;
    const auto low = hn::Set(tag, static_cast<LaneT>((std::numeric_limits<SampleT>::lowest)()));
    const auto high = hn::Set(tag, static_cast<LaneT>((std::numeric_limits<SampleT>::max)()));
    const auto live = hn::FirstN(tag, count);
    const auto finite = hn::IsFinite(value);
    const auto below = hn::Lt(value, low);
    const auto above = hn::Gt(value, high);
    nonFinite += static_cast<std::uint64_t>(hn::CountTrue(tag, hn::AndNot(finite, live)));
    saturated += static_cast<std::uint64_t>(hn::CountTrue(tag, hn::And(live, hn::And(finite, hn::Or(below, above)))));
    // An infinity takes the end it is the limit of; a value that is not a number fails both comparisons, survives the
    // rounding as itself and is replaced by zero after it.
    const auto held = hn::IfThenElse(below, low, hn::IfThenElse(above, high, value));
    const auto rounded = hn::IfThenZeroElse(hn::IsNaN(held), hn::Round(held));
    const hn::Rebind<SampleT, D> sampleTag;
    const hn::Rebind<std::int32_t, D> codeTag;
    hn::Vec<decltype(sampleTag)> narrowed;
    if constexpr (std::is_same_v<SampleT, std::uint32_t>)
    {
        // The codes above the signed range are carried as their two's-complement image, which every step keeps exact.
        const auto image = hn::DemoteTo(codeTag, hn::Sub(rounded, hn::Set(tag, 2147483648.0)));
        narrowed = hn::Xor(hn::BitCast(sampleTag, image), hn::Set(sampleTag, 0x80000000u));
    }
    else if constexpr (std::is_same_v<SampleT, std::int32_t>)
    {
        narrowed = hn::DemoteTo(codeTag, rounded);
    }
    else if constexpr (std::is_same_v<LaneT, float>)
    {
        narrowed = hn::DemoteTo(sampleTag, hn::ConvertTo(codeTag, rounded));
    }
    else
    {
        narrowed = hn::DemoteTo(sampleTag, hn::DemoteTo(codeTag, rounded));
    }
    if (count == hn::Lanes(tag))
    {
        hn::StoreU(narrowed, sampleTag, samples);
    }
    else
    {
        hn::StoreN(narrowed, sampleTag, samples, count);
    }
}

/// Executes a pointwise program over integer samples with lanes of the program's arithmetic width.
///
/// Every source is widened as it is read, every step is the evaluator's expression at the same width in the same
/// order, and the result is narrowed back as the evaluator narrows it, so each sample is the scalar program's bit for
/// bit. `pointwiseRunFor` hands this body only a program whose steps are among the ones below and that does not allow
/// contraction, which is why the others do nothing here. The run's last partial vector is read and written lane by
/// lane, so every pixel is written and a destination that is a source - a blend in place - is read before it is
/// written, a vector at a time.
template <typename SampleT, typename LaneT>
[[nodiscard]] PointwiseRunOutcome pointwiseIntegerRunOf(const PointwiseProgram& program, const void* const* sources,
                                                        void* const* destinations, std::uint64_t pixelCount) noexcept
{
    const hn::ScalableTag<LaneT> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const std::uint32_t channels = program.channelCount();
    const std::span<const PointwiseOp> steps = program.steps();
    std::uint64_t nonFinite = 0u;
    std::uint64_t saturated = 0u;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        SampleT* const destination = static_cast<SampleT*>(destinations[channel]);
        for (std::uint64_t pixel = 0u; pixel < pixelCount; pixel += lanes)
        {
            const std::size_t count = static_cast<std::size_t>((std::min)(pixelCount - pixel, lanes));
            const auto operandOf = [&](std::uint32_t source) {
                return widenedIntegers(tag, static_cast<const SampleT*>(sources[source * channels + channel]) + pixel,
                                       count);
            };
            auto value = operandOf(0u);
            for (const PointwiseOp& op : steps)
            {
                switch (op.kind)
                {
                case PointwiseOpKind::Scale:
                    value = hn::Mul(value, hn::Set(tag, static_cast<LaneT>(op.factor)));
                    break;
                case PointwiseOpKind::Bias:
                    value = hn::Add(value, hn::Set(tag, static_cast<LaneT>(op.offset)));
                    break;
                case PointwiseOpKind::Clamp: {
                    const auto low = hn::Set(tag, static_cast<LaneT>(op.low));
                    const auto high = hn::Set(tag, static_cast<LaneT>(op.high));
                    value = hn::IfThenElse(hn::Lt(value, low), low, hn::IfThenElse(hn::Gt(value, high), high, value));
                    break;
                }
                case PointwiseOpKind::Mask:
                    value = hn::Mul(value, operandOf(op.operand));
                    break;
                case PointwiseOpKind::Blend: {
                    const auto weight = hn::Set(tag, static_cast<LaneT>(op.factor));
                    const auto complement = hn::Sub(hn::Set(tag, static_cast<LaneT>(1)), weight);
                    value = hn::Add(hn::Mul(value, complement), hn::Mul(operandOf(op.operand), weight));
                    break;
                }
                case PointwiseOpKind::Add:
                    value = hn::Add(value, hn::Mul(operandOf(op.operand), hn::Set(tag, static_cast<LaneT>(op.factor))));
                    break;
                case PointwiseOpKind::Subtract:
                    value = hn::Sub(value, hn::Mul(operandOf(op.operand), hn::Set(tag, static_cast<LaneT>(op.factor))));
                    break;
                case PointwiseOpKind::Multiply:
                    value = hn::Mul(hn::Mul(value, operandOf(op.operand)), hn::Set(tag, static_cast<LaneT>(op.factor)));
                    break;
                case PointwiseOpKind::Divide:
                    value = hn::Mul(hn::Div(value, operandOf(op.operand)), hn::Set(tag, static_cast<LaneT>(op.factor)));
                    break;
                case PointwiseOpKind::Compare: {
                    const auto operand = operandOf(op.operand);
                    const auto zero = hn::Zero(tag);
                    const auto one = hn::Set(tag, static_cast<LaneT>(1));
                    switch (op.comparison)
                    {
                    case PointwiseComparison::Less:
                        value = hn::IfThenElse(hn::Lt(value, operand), one, zero);
                        break;
                    case PointwiseComparison::LessOrEqual:
                        value = hn::IfThenElse(hn::Or(hn::Lt(value, operand), hn::Eq(value, operand)), one, zero);
                        break;
                    case PointwiseComparison::Equal:
                        value = hn::IfThenElse(hn::Eq(value, operand), one, zero);
                        break;
                    case PointwiseComparison::NotEqual:
                        value = hn::IfThenElse(hn::Not(hn::Eq(value, operand)), one, zero);
                        break;
                    case PointwiseComparison::GreaterOrEqual:
                        value = hn::IfThenElse(hn::Or(hn::Gt(value, operand), hn::Eq(value, operand)), one, zero);
                        break;
                    case PointwiseComparison::Greater:
                        value = hn::IfThenElse(hn::Gt(value, operand), one, zero);
                        break;
                    }
                    break;
                }
                default:
                    break;
                }
            }
            storeNarrowedIntegers(tag, value, destination + pixel, count, nonFinite, saturated);
        }
    }
    return PointwiseRunOutcome{
        .pixelsProcessed = pixelCount, .nonFiniteOutputs = nonFinite, .saturatedOutputs = saturated};
}

/// The integer run this target compiled for one representation at one arithmetic width; see
/// `PointwiseIntegerRunLookupFn`.
[[nodiscard]] PointwiseRunFn pointwiseIntegerRunFor(SampleType sampleType, ComputePrecision precision) noexcept
{
    const bool wide = precision == ComputePrecision::Float64;
    switch (sampleType)
    {
    case SampleType::UInt8:
        return wide ? &pointwiseIntegerRunOf<std::uint8_t, double> : &pointwiseIntegerRunOf<std::uint8_t, float>;
    case SampleType::Int8:
        return wide ? &pointwiseIntegerRunOf<std::int8_t, double> : &pointwiseIntegerRunOf<std::int8_t, float>;
    case SampleType::UInt16:
        return wide ? &pointwiseIntegerRunOf<std::uint16_t, double> : &pointwiseIntegerRunOf<std::uint16_t, float>;
    case SampleType::Int16:
        return wide ? &pointwiseIntegerRunOf<std::int16_t, double> : &pointwiseIntegerRunOf<std::int16_t, float>;
    case SampleType::UInt32:
        return wide ? &pointwiseIntegerRunOf<std::uint32_t, double> : nullptr;
    case SampleType::Int32:
        return wide ? &pointwiseIntegerRunOf<std::int32_t, double> : nullptr;
    case SampleType::Float32:
    case SampleType::Float64:
        break;
    }
    return nullptr;
}

/// Applies a normalized lower-neighbour Float32 table to one sample, as a lane of the vector body does.
[[nodiscard]] inline float normalizedLowerCurveSample(float value, const float* table,
                                                      std::uint32_t tableEntries) noexcept
{
    if (value >= 0.0f && value <= 1.0f) [[likely]]
    {
        return table[static_cast<std::size_t>(value * static_cast<float>(tableEntries - 1u))];
    }
    if (std::isnan(value))
    {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return value < 0.0f ? table[0u] : table[tableEntries - 1u];
}

/// One full vector of `normalizedLowerCurveFloat32RunCase`: each sample's table entry, a quiet NaN for a
/// NaN, and the count of the samples that are not finite added to `nonFinite`.
template <class D, class DI>
[[nodiscard]] HWY_INLINE hn::Vec<D> normalizedLowerCurveVector(D tag, DI indexTag, const float* source,
                                                               const float* table, hn::Vec<D> last,
                                                               std::uint64_t& nonFinite) noexcept
{
    const auto zero = hn::Zero(tag);
    const auto one = hn::Set(tag, 1.0f);
    const auto value = hn::LoadU(tag, source);
    const auto isNaN = hn::IsNaN(value);
    const auto boundedValue = hn::IfThenElse(hn::Lt(value, zero), zero, hn::IfThenElse(hn::Gt(value, one), one, value));
    // A masked select does not make the gather on its other arm harmless. Replace NaNs before
    // converting to an index so no target can form an out-of-range gather address.
    const auto bounded = hn::IfThenElse(isNaN, zero, boundedValue);
    const auto indices = hn::ConvertTo(indexTag, hn::Mul(bounded, last));
    nonFinite += static_cast<std::uint64_t>(hn::CountTrue(tag, hn::Not(hn::IsFinite(value))));
    return hn::IfThenElse(isNaN, hn::Set(tag, std::numeric_limits<float>::quiet_NaN()),
                          hn::GatherIndex(tag, table, indices));
}

/// The samples a streaming run gathers into its staging block before it streams them out: a block
/// the first-level cache holds whole on every target.
constexpr std::uint64_t kStreamedLookupBlock = 1024u;

/// Applies a normalized lower-neighbour Float32 table through this target's gather instruction.
///
/// A streaming run writes the samples before its first aligned vector one at a time with ordinary
/// stores. It gathers its full vectors a block of at most `kStreamedLookupBlock` samples at a time
/// into a staging block with ordinary stores, streams each block out before gathering the next, and
/// fences the streaming stores before it returns. Gathers issued between the streaming stores ran at
/// under half the speed ordinary stores give on one processor measured; staged, they run faster than
/// either.
///
/// @tparam kStream whether full vectors are written with streaming stores.
template <bool kStream>
[[nodiscard]] std::uint64_t normalizedLowerCurveFloat32RunCase(const float* source, float* destination,
                                                               std::uint64_t sampleCount, const float* table,
                                                               std::uint32_t tableEntries) noexcept
{
    const hn::ScalableTag<float> tag;
    const hn::RebindToSigned<decltype(tag)> indexTag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const auto last = hn::Set(tag, static_cast<float>(tableEntries - 1u));
    std::uint64_t nonFinite = 0u;
    std::uint64_t sample = 0u;
    if constexpr (kStream)
    {
        const std::uint64_t offset = (reinterpret_cast<std::uintptr_t>(destination) / sizeof(float)) % lanes;
        const std::uint64_t head = (std::min)(sampleCount, offset == 0u ? std::uint64_t{0u} : lanes - offset);
        for (; sample < head; ++sample)
        {
            nonFinite += std::isfinite(source[sample]) ? 0u : 1u;
            destination[sample] = normalizedLowerCurveSample(source[sample], table, tableEntries);
        }
        alignas(64) std::array<float, kStreamedLookupBlock> staged;
        const std::uint64_t blockSamples = (kStreamedLookupBlock / lanes) * lanes;
        const std::uint64_t vectorEnd = sample + ((sampleCount - sample) / lanes) * lanes;
        while (sample < vectorEnd)
        {
            const std::uint64_t blockEnd = (std::min)(vectorEnd, sample + blockSamples);
            for (std::uint64_t at = sample; at < blockEnd; at += lanes)
            {
                hn::Store(normalizedLowerCurveVector(tag, indexTag, source + at, table, last, nonFinite), tag,
                          staged.data() + (at - sample));
            }
            for (std::uint64_t at = sample; at < blockEnd; at += lanes)
            {
                hn::Stream(hn::Load(tag, staged.data() + (at - sample)), tag, destination + at);
            }
            sample = blockEnd;
        }
    }
    else
    {
        for (; sample + lanes <= sampleCount; sample += lanes)
        {
            hn::StoreU(normalizedLowerCurveVector(tag, indexTag, source + sample, table, last, nonFinite), tag,
                       destination + sample);
        }
    }
    for (; sample < sampleCount; ++sample)
    {
        nonFinite += std::isfinite(source[sample]) ? 0u : 1u;
        destination[sample] = normalizedLowerCurveSample(source[sample], table, tableEntries);
    }
#if HWY_ARCH_X86
    if constexpr (kStream)
    {
        _mm_sfence();
    }
#endif
    return nonFinite;
}

/// Applies a normalized lower-neighbour Float32 table through this target's gather instruction, with
/// streaming stores where the caller asks for them; see `normalizedLowerCurveFloat32RunCase`.
[[nodiscard]] std::uint64_t normalizedLowerCurveFloat32RunOf(const float* source, float* destination,
                                                             std::uint64_t sampleCount, const float* table,
                                                             std::uint32_t tableEntries, bool stream) noexcept
{
    return stream ? normalizedLowerCurveFloat32RunCase<true>(source, destination, sampleCount, table, tableEntries)
                  : normalizedLowerCurveFloat32RunCase<false>(source, destination, sampleCount, table, tableEntries);
}

/// One specialisation of the range-selection run, with its three switches fixed at compile time so
/// the lane loop carries no branch; the tail repeats the lane arithmetic one sample at a time.
template <bool Screening, bool Invert, bool Fuzzy>
[[nodiscard]] std::uint64_t rangeSelectionFloat32RunCase(const float* source, float* destination,
                                                         std::uint64_t sampleCount,
                                                         const RangeSelectionRunParameters& parameters) noexcept
{
    const hn::ScalableTag<double> tag;
    const hn::Rebind<float, decltype(tag)> sampleTag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const auto zero = hn::Zero(tag);
    const auto one = hn::Set(tag, 1.0);
    const auto lowerLimit = hn::Set(tag, parameters.lowerLimit);
    const auto upperLimit = hn::Set(tag, parameters.upperLimit);
    const auto inverseRamp = hn::Set(tag, parameters.inverseRamp);
    const auto lowerPlateau = hn::Set(tag, parameters.lowerPlateau);
    const auto upperPlateau = hn::Set(tag, parameters.upperPlateau);
    std::uint64_t nonFinite = 0u;
    std::uint64_t sample = 0u;
    for (; sample + lanes <= sampleCount; sample += lanes)
    {
        const auto value = hn::PromoteTo(tag, hn::LoadU(sampleTag, source + sample));
        const auto inside = hn::And(hn::Ge(value, lowerLimit), hn::Le(value, upperLimit));
        auto weight = zero;
        if constexpr (Fuzzy)
        {
            const auto lowerWeight = hn::Add(hn::Mul(hn::Sub(value, lowerPlateau), inverseRamp), one);
            const auto upperWeight = hn::Add(hn::Mul(hn::Sub(upperPlateau, value), inverseRamp), one);
            weight = hn::IfThenElse(inside,
                                    hn::IfThenElse(hn::Lt(value, lowerPlateau), lowerWeight,
                                                   hn::IfThenElse(hn::Gt(value, upperPlateau), upperWeight, one)),
                                    zero);
        }
        else
        {
            weight = hn::IfThenElse(inside, one, zero);
        }
        if constexpr (Screening)
        {
            weight = hn::Mul(weight, value);
        }
        if constexpr (Invert)
        {
            weight = hn::Sub(one, weight);
        }
        nonFinite += static_cast<std::uint64_t>(hn::CountTrue(tag, hn::Not(hn::IsFinite(value))));
        hn::StoreU(hn::DemoteTo(sampleTag, weight), sampleTag, destination + sample);
    }
    for (; sample < sampleCount; ++sample)
    {
        const double value = static_cast<double>(source[sample]);
        nonFinite += std::isfinite(value) ? 0u : 1u;
        double weight = value >= parameters.lowerLimit && value <= parameters.upperLimit ? 1.0 : 0.0;
        if constexpr (Fuzzy)
        {
            if (value >= parameters.lowerLimit && value <= parameters.upperLimit)
            {
                if (value < parameters.lowerPlateau)
                {
                    weight = (value - parameters.lowerPlateau) * parameters.inverseRamp + 1.0;
                }
                else if (value > parameters.upperPlateau)
                {
                    weight = (parameters.upperPlateau - value) * parameters.inverseRamp + 1.0;
                }
            }
        }
        if constexpr (Screening)
        {
            weight *= value;
        }
        if constexpr (Invert)
        {
            weight = 1.0 - weight;
        }
        destination[sample] = static_cast<float>(weight);
    }
    return nonFinite;
}

/// Applies one normalized Float32 range-selection plan through widened Float64 lanes.
[[nodiscard]] std::uint64_t rangeSelectionFloat32RunOf(const float* source, float* destination,
                                                       std::uint64_t sampleCount,
                                                       const RangeSelectionRunParameters& parameters) noexcept
{
    if (parameters.fuzzy)
    {
        if (parameters.screening)
        {
            return parameters.invert
                       ? rangeSelectionFloat32RunCase<true, true, true>(source, destination, sampleCount, parameters)
                       : rangeSelectionFloat32RunCase<true, false, true>(source, destination, sampleCount, parameters);
        }
        return parameters.invert
                   ? rangeSelectionFloat32RunCase<false, true, true>(source, destination, sampleCount, parameters)
                   : rangeSelectionFloat32RunCase<false, false, true>(source, destination, sampleCount, parameters);
    }
    if (parameters.screening)
    {
        return parameters.invert
                   ? rangeSelectionFloat32RunCase<true, true, false>(source, destination, sampleCount, parameters)
                   : rangeSelectionFloat32RunCase<true, false, false>(source, destination, sampleCount, parameters);
    }
    return parameters.invert
               ? rangeSelectionFloat32RunCase<false, true, false>(source, destination, sampleCount, parameters)
               : rangeSelectionFloat32RunCase<false, false, false>(source, destination, sampleCount, parameters);
}

/// Float32 samples promoted to Float64 vector accumulators for the first reduction pass.
void reductionFirstFloat32(const void* source, std::uint64_t sampleCount, ReductionRunAccumulator& accumulator) noexcept
{
    const float* const input = static_cast<const float*>(source);
    const hn::ScalableTag<double> accumTag;
    const hn::Rebind<float, decltype(accumTag)> sourceTag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(accumTag));
    auto sum = hn::Zero(accumTag);
    auto l1 = hn::Zero(accumTag);
    auto squared = hn::Zero(accumTag);
    auto minimum = hn::Set(accumTag, (std::numeric_limits<double>::max)());
    auto maximum = hn::Set(accumTag, (std::numeric_limits<double>::lowest)());
    auto infinityNorm = hn::Zero(accumTag);
    bool finite = true;
    std::uint64_t index = 0u;
    for (; index + lanes <= sampleCount; index += lanes)
    {
        const auto value = hn::PromoteTo(accumTag, hn::LoadU(sourceTag, input + index));
        finite = finite && hn::AllTrue(accumTag, hn::IsFinite(value));
        const auto magnitude = hn::Abs(value);
        sum = hn::Add(sum, value);
        l1 = hn::Add(l1, magnitude);
        squared = hn::Add(squared, hn::Mul(value, value));
        minimum = hn::Min(minimum, value);
        maximum = hn::Max(maximum, value);
        infinityNorm = hn::Max(infinityNorm, magnitude);
    }
    const bool hadVector = index != 0u;
    if (hadVector)
    {
        accumulator.sum += hn::ReduceSum(accumTag, sum);
        accumulator.l1 += hn::ReduceSum(accumTag, l1);
        accumulator.squaredL2 += hn::ReduceSum(accumTag, squared);
        accumulator.minimum = (std::min)(accumulator.minimum, hn::ReduceMin(accumTag, minimum));
        accumulator.maximum = (std::max)(accumulator.maximum, hn::ReduceMax(accumTag, maximum));
        accumulator.infinityNorm = (std::max)(accumulator.infinityNorm, hn::ReduceMax(accumTag, infinityNorm));
    }
    for (; index < sampleCount; ++index)
    {
        const double value = static_cast<double>(input[index]);
        const double magnitude = std::abs(value);
        accumulator.sum += value;
        accumulator.l1 += magnitude;
        accumulator.squaredL2 += value * value;
        accumulator.minimum = (std::min)(accumulator.minimum, value);
        accumulator.maximum = (std::max)(accumulator.maximum, value);
        accumulator.infinityNorm = (std::max)(accumulator.infinityNorm, magnitude);
        finite = finite && std::isfinite(value);
    }
    accumulator.finite = accumulator.finite && finite;
    accumulator.count += sampleCount;
}

/// Float32 centered terms promoted to Float64 before they are squared and accumulated.
void reductionCenteredFloat32(const void* source, std::uint64_t sampleCount, double mean,
                              ReductionRunAccumulator& accumulator) noexcept
{
    const float* const input = static_cast<const float*>(source);
    const hn::ScalableTag<double> accumTag;
    const hn::Rebind<float, decltype(accumTag)> sourceTag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(accumTag));
    const auto center = hn::Set(accumTag, mean);
    auto moment = hn::Zero(accumTag);
    std::uint64_t index = 0u;
    for (; index + lanes <= sampleCount; index += lanes)
    {
        const auto value = hn::PromoteTo(accumTag, hn::LoadU(sourceTag, input + index));
        const auto deviation = hn::Sub(value, center);
        moment = hn::Add(moment, hn::Mul(deviation, deviation));
    }
    if (index != 0u)
    {
        accumulator.centeredMoment += hn::ReduceSum(accumTag, moment);
    }
    for (; index < sampleCount; ++index)
    {
        const double deviation = static_cast<double>(input[index]) - mean;
        accumulator.centeredMoment += deviation * deviation;
    }
}

/// The largest argument whose natural exponential is a finite Float32.
///
/// Above it the true result overflows, and the polynomial is not asked: an argument of positive
/// infinity would otherwise reach a float-to-integer conversion that has no answer for it.
constexpr float kExpMaxArgument = 88.72283935546875f;

/// The natural exponential of one vector, with its exceptional answers blended over the polynomial.
///
/// The polynomial is evaluated for every lane and the exceptional answers are written over the top,
/// which is how a vector kernel handles the cases a scalar one branches on. The list of cases is the
/// scalar reference list: kernels/reference/MathKernel.cpp states it once and this reproduces it,
/// because a case handled in one and not the other is a difference at exactly the inputs nobody
/// generates by accident.
///
/// The library body answers zero below its lower bound, and the comparison that gates it is false
/// for a not-a-number, so a not-a-number would otherwise arrive here as zero. It is restored rather
/// than assumed to propagate.
///
/// @tparam TagT the lane description this target compiles for.
/// @param tag the lane description.
/// @param value the samples.
/// @return the exponentials.
template <class TagT> [[nodiscard]] hn::Vec<TagT> exponentialOf(TagT tag, hn::Vec<TagT> value) noexcept
{
    hn::Vec<TagT> result = hn::Exp(tag, value);
    result = hn::IfThenElse(hn::Gt(value, hn::Set(tag, kExpMaxArgument)), hn::Inf(tag), result);
    return hn::IfThenElse(hn::IsNaN(value), hn::NaN(tag), result);
}

/// The exponential of an argument that is never positive.
///
/// The polynomial, its two-part reduction and its coefficients are those of the shared
/// `exponentialOf`, specialised to the domain: the integer part of the reduction is rounded to
/// nearest directly rather than through the sign-dependent bias the general form needs; with the
/// argument held at or above -86 the reconstruction is one power of two, applied once, that never
/// leaves the normal range; and the overflow case and the underflow select have nothing to decide.
/// Below the clamp the answer is about 4e-38 where the general form returns a denormal or zero,
/// which is the same weight. A not-a-number argument is kept, as `exponentialOf` keeps it.
///
/// @tparam TagT the lane description this target compiles for, of Float32 lanes.
/// @param tag the lane description.
/// @param value the arguments, each at most zero or not a number.
/// @return the exponentials.
template <class TagT> [[nodiscard]] hn::Vec<TagT> nonPositiveExponentialOf(TagT tag, hn::Vec<TagT> value) noexcept
{
    const hn::Rebind<std::int32_t, TagT> intTag;
    const auto clamped = hn::Max(hn::Set(tag, -86.0f), value);
    const auto exponent = hn::NearestInt(hn::Mul(clamped, hn::Set(tag, 1.442695040888963407359924681f)));
    const auto whole = hn::ConvertTo(tag, exponent);
    auto reduced = hn::MulAdd(whole, hn::Set(tag, -0.693145751953125f), clamped);
    reduced = hn::MulAdd(whole, hn::Set(tag, -1.428606765330187045e-6f), reduced);
    const auto k0 = hn::Set(tag, +0.5f);
    const auto k1 = hn::Set(tag, +0.166666671633720397949219f);
    const auto k2 = hn::Set(tag, +0.0416664853692054748535156f);
    const auto k3 = hn::Set(tag, +0.00833336077630519866943359f);
    const auto k4 = hn::Set(tag, +0.00139304355252534151077271f);
    const auto k5 = hn::Set(tag, +0.000198527617612853646278381f);
    const auto square = hn::Mul(reduced, reduced);
    const auto fourth = hn::Mul(square, square);
    const auto estrin = hn::MulAdd(fourth, hn::MulAdd(k5, reduced, k4),
                                   hn::MulAdd(square, hn::MulAdd(k3, reduced, k2), hn::MulAdd(k1, reduced, k0)));
    const auto polynomial = hn::Add(hn::MulAdd(estrin, square, reduced), hn::Set(tag, 1.0f));
    const auto power = hn::BitCast(tag, hn::ShiftLeft<23>(hn::Add(exponent, hn::Set(intTag, std::int32_t{0x7F}))));
    return hn::IfThenElse(hn::IsNaN(value), hn::NaN(tag), hn::Mul(polynomial, power));
}

/// The natural logarithm of one vector, with its exceptional answers blended over the polynomial.
///
/// Zero of either sign is negative infinity, a negative sample is outside the domain and is a quiet
/// not-a-number, and positive infinity is itself. The domain refusal is applied last so that it wins
/// over the zero comparison, which a negative zero also satisfies.
///
/// @tparam TagT the lane description this target compiles for.
/// @param tag the lane description.
/// @param value the samples.
/// @return the logarithms.
template <class TagT> [[nodiscard]] hn::Vec<TagT> logarithmOf(TagT tag, hn::Vec<TagT> value) noexcept
{
    const hn::Vec<TagT> zero = hn::Zero(tag);
    const hn::Vec<TagT> infinity = hn::Inf(tag);
    hn::Vec<TagT> result = hn::Log(tag, value);
    result = hn::IfThenElse(hn::Eq(value, zero), hn::Neg(infinity), result);
    result = hn::IfThenElse(hn::Eq(value, infinity), infinity, result);
    return hn::IfThenElse(hn::Or(hn::Lt(value, zero), hn::IsNaN(value)), hn::NaN(tag), result);
}

/// One non-negative base raised to a stated exponent, composed from the two bodies above it.
///
/// exp(g * log(x)) is the whole of it, and every exceptional answer falls out of the composition
/// rather than being written again: a base of zero has a logarithm of negative infinity, which the
/// exponential turns into zero or infinity by the sign of the exponent, and a negative base is
/// already a not-a-number before the exponential sees it. The one case the composition cannot
/// produce is an exponent of zero, where the product would be a not-a-number instead of the one
/// that every base raised to nothing is, and that case is answered without the composition.
///
/// Zero raised to an odd integral exponent keeps the base's sign, including for a negative
/// exponent whose result is infinity. Restore that sign after the logarithmic composition.
///
/// @tparam TagT the lane description this target compiles for.
/// @param tag the lane description.
/// @param value the bases.
/// @param exponent the exponent, broadcast.
/// @return the powers.
template <class TagT>
[[nodiscard]] hn::Vec<TagT> powerOf(TagT tag, hn::Vec<TagT> value, hn::Vec<TagT> exponent) noexcept
{
    const auto result = exponentialOf(tag, hn::Mul(exponent, logarithmOf(tag, value)));
    const auto evenPart = hn::Mul(hn::Trunc(hn::Mul(exponent, hn::Set(tag, 0.5f))), hn::Set(tag, 2.0f));
    const auto odd = hn::Eq(hn::Abs(hn::Sub(exponent, evenPart)), hn::Set(tag, 1.0f));
    return hn::IfThenElse(hn::And(hn::Eq(value, hn::Zero(tag)), odd), hn::CopySign(result, value), result);
}

/// One base raised to nothing, which is one inside the domain and a not-a-number outside it.
///
/// @tparam TagT the lane description this target compiles for.
/// @param tag the lane description.
/// @param value the bases.
/// @return one for every base inside the domain and a not-a-number outside it.
template <class TagT> [[nodiscard]] hn::Vec<TagT> raisedToNothingOf(TagT tag, hn::Vec<TagT> value) noexcept
{
    const auto outside = hn::Or(hn::Lt(value, hn::Zero(tag)), hn::IsNaN(value));
    return hn::IfThenElse(outside, hn::NaN(tag), hn::Set(tag, 1.0f));
}

/// The natural exponential of a Float32 run.
///
/// @param source where the samples start.
/// @param destination where the results go, which may be the source.
/// @param count how many samples.
/// @param exponent unused by this function.
void expRunFloat32(const float* source, float* destination, std::uint64_t count, float exponent) noexcept
{
    static_cast<void>(exponent);
    const hn::ScalableTag<float> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    std::uint64_t index = 0u;
    for (; index + lanes <= count; index += lanes)
    {
        hn::StoreU(exponentialOf(tag, hn::LoadU(tag, source + index)), tag, destination + index);
    }
    const std::uint64_t remaining = count - index;
    if (remaining != 0u)
    {
        // The tail goes through the same body as the interior rather than through a scalar copy of
        // it, so what one sample evaluates to does not depend on where in the run it sat.
        const std::size_t partial = static_cast<std::size_t>(remaining);
        hn::StoreN(exponentialOf(tag, hn::LoadN(tag, source + index, partial)), tag, destination + index, partial);
    }
}

/// The natural logarithm of a Float32 run.
///
/// @param source where the samples start.
/// @param destination where the results go, which may be the source.
/// @param count how many samples.
/// @param exponent unused by this function.
void logRunFloat32(const float* source, float* destination, std::uint64_t count, float exponent) noexcept
{
    static_cast<void>(exponent);
    const hn::ScalableTag<float> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    std::uint64_t index = 0u;
    for (; index + lanes <= count; index += lanes)
    {
        hn::StoreU(logarithmOf(tag, hn::LoadU(tag, source + index)), tag, destination + index);
    }
    const std::uint64_t remaining = count - index;
    if (remaining != 0u)
    {
        const std::size_t partial = static_cast<std::size_t>(remaining);
        hn::StoreN(logarithmOf(tag, hn::LoadN(tag, source + index, partial)), tag, destination + index, partial);
    }
}

/// A Float32 run of bases raised to a stated exponent.
///
/// @param source where the bases start.
/// @param destination where the results go, which may be the source.
/// @param count how many samples.
/// @param exponent the exponent every sample is raised to.
void powRunFloat32(const float* source, float* destination, std::uint64_t count, float exponent) noexcept
{
    const hn::ScalableTag<float> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const hn::Vec<decltype(tag)> broadcast = hn::Set(tag, exponent);
    const bool nothing = exponent == 0.0f;
    std::uint64_t index = 0u;
    for (; index + lanes <= count; index += lanes)
    {
        const hn::Vec<decltype(tag)> value = hn::LoadU(tag, source + index);
        hn::StoreU(nothing ? raisedToNothingOf(tag, value) : powerOf(tag, value, broadcast), tag, destination + index);
    }
    const std::uint64_t remaining = count - index;
    if (remaining != 0u)
    {
        const std::size_t partial = static_cast<std::size_t>(remaining);
        const hn::Vec<decltype(tag)> value = hn::LoadN(tag, source + index, partial);
        hn::StoreN(nothing ? raisedToNothingOf(tag, value) : powerOf(tag, value, broadcast), tag, destination + index,
                   partial);
    }
}

/// One row of a separable horizontal pass, over Float32 samples whose borders are already resolved.
void stencilRowFloat32(const float* padded, std::uint64_t count, const double* taps, std::uint32_t tapCount,
                       double* destination) noexcept
{
    const hn::ScalableTag<double> accumTag;
    const hn::Rebind<float, decltype(accumTag)> sourceTag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(accumTag));
    std::uint64_t x = 0u;
    for (; x + lanes <= count; x += lanes)
    {
        auto sum = hn::Zero(accumTag);
        for (std::uint32_t tap = 0u; tap < tapCount; ++tap)
        {
            const auto value = hn::PromoteTo(accumTag, hn::LoadU(sourceTag, padded + x + tap));
            sum = hn::Add(sum, hn::Mul(hn::Set(accumTag, taps[tap]), value));
        }
        hn::StoreU(sum, accumTag, destination + x);
    }
    for (; x < count; ++x)
    {
        double sum = 0.0;
        for (std::uint32_t tap = 0u; tap < tapCount; ++tap)
        {
            sum += taps[tap] * static_cast<double>(padded[x + tap]);
        }
        destination[x] = sum;
    }
}

/// One row of a separable vertical pass down a Float64 band, narrowing once on the way out.
void stencilColumnFloat32(const double* band, std::uint64_t rowStride, std::uint64_t count, const double* taps,
                          std::uint32_t tapCount, float* destination) noexcept
{
    const hn::ScalableTag<double> accumTag;
    const hn::Rebind<float, decltype(accumTag)> outputTag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(accumTag));
    std::uint64_t x = 0u;
    for (; x + lanes <= count; x += lanes)
    {
        auto sum = hn::Zero(accumTag);
        for (std::uint32_t tap = 0u; tap < tapCount; ++tap)
        {
            const auto value = hn::LoadU(accumTag, band + static_cast<std::uint64_t>(tap) * rowStride + x);
            sum = hn::Add(sum, hn::Mul(hn::Set(accumTag, taps[tap]), value));
        }
        hn::StoreU(hn::DemoteTo(outputTag, sum), outputTag, destination + x);
    }
    for (; x < count; ++x)
    {
        double sum = 0.0;
        for (std::uint32_t tap = 0u; tap < tapCount; ++tap)
        {
            sum += taps[tap] * band[static_cast<std::uint64_t>(tap) * rowStride + x];
        }
        destination[x] = static_cast<float>(sum);
    }
}

/// One row of the common symmetric Float32 horizontal Gaussian implementation.
///
/// Float32 throughout: the centre tap first, then for each offset from the nearest outward the two
/// samples at that distance added before their shared tap weights them. The border mirrors about
/// the edge sample without repeating it, as `KernelBorderMode::ReflectExcludingEdge` does, and the
/// caller holds `radius` below half the width, which keeps every mirrored index inside the row. The
/// scalar table has no counterpart; the Gaussian takes its general path wherever this is absent.
///
/// @param source the row to filter.
/// @param destination where the filtered row goes; it must not overlap `source`.
/// @param width samples per row.
/// @param taps the `2 * radius + 1` symmetric taps, centre at index `radius`.
/// @param radius how many taps lie on each side of the centre.
void symmetricGaussianHorizontalFloat32(const float* source, float* destination, std::uint32_t width, const float* taps,
                                        std::uint32_t radius) noexcept
{
    const hn::ScalableTag<float> tag;
    const std::uint32_t lanes = static_cast<std::uint32_t>(hn::Lanes(tag));
    const auto center = hn::Set(tag, taps[radius]);
    std::uint32_t x = 0u;
    for (; x + lanes <= width; x += lanes)
    {
        hn::StoreU(hn::Mul(center, hn::LoadU(tag, source + x)), tag, destination + x);
    }
    for (; x < width; ++x)
    {
        destination[x] = taps[radius] * source[x];
    }

    for (std::uint32_t offset = 1u; offset <= radius; ++offset)
    {
        const float coefficient = taps[radius - offset];
        for (x = 0u; x < offset; ++x)
        {
            destination[x] += coefficient * (source[offset - x] + source[x + offset]);
        }
        const std::uint32_t interiorEnd = width - offset;
        const auto factor = hn::Set(tag, coefficient);
        for (x = offset; x + lanes <= interiorEnd; x += lanes)
        {
            const auto pair = hn::Add(hn::LoadU(tag, source + x - offset), hn::LoadU(tag, source + x + offset));
            hn::StoreU(hn::Add(hn::LoadU(tag, destination + x), hn::Mul(factor, pair)), tag, destination + x);
        }
        for (; x < interiorEnd; ++x)
        {
            destination[x] += coefficient * (source[x - offset] + source[x + offset]);
        }
        for (; x < width; ++x)
        {
            const std::uint32_t reflected = 2u * width - x - offset - 2u;
            destination[x] += coefficient * (source[x - offset] + source[reflected]);
        }
    }
}

/// One row of the common symmetric Float32 vertical Gaussian implementation, over a run of the
/// plane's columns.
///
/// The vertical counterpart of `symmetricGaussianHorizontalFloat32`, with the same order of
/// arithmetic and the same mirrored border applied to rows, and the caller holding `radius` below
/// half the height. Each sample's sum is its own, so a run of any width gives every sample it
/// covers the value a run over the whole row gives it.
///
/// @param source the run's first sample in the plane's first row.
/// @param destination where the run of output row `row` goes; it must not overlap the plane.
/// @param count samples in the run.
/// @param rowStride samples from one row of the plane to the next.
/// @param height rows in the plane.
/// @param row which output row to produce.
/// @param taps the `2 * radius + 1` symmetric taps, centre at index `radius`.
/// @param radius how many taps lie on each side of the centre.
void symmetricGaussianVerticalFloat32(const float* source, float* destination, std::uint32_t count,
                                      std::uint64_t rowStride, std::uint32_t height, std::uint32_t row,
                                      const float* taps, std::uint32_t radius) noexcept
{
    const hn::ScalableTag<float> tag;
    const std::uint32_t lanes = static_cast<std::uint32_t>(hn::Lanes(tag));
    const float* const centerRow = source + static_cast<std::size_t>(row) * rowStride;
    const auto center = hn::Set(tag, taps[radius]);
    std::uint32_t x = 0u;
    for (; x + lanes <= count; x += lanes)
    {
        hn::StoreU(hn::Mul(center, hn::LoadU(tag, centerRow + x)), tag, destination + x);
    }
    for (; x < count; ++x)
    {
        destination[x] = taps[radius] * centerRow[x];
    }

    for (std::uint32_t offset = 1u; offset <= radius; ++offset)
    {
        const std::uint32_t above = row >= offset ? row - offset : offset - row;
        const std::uint32_t below = row + offset < height ? row + offset : 2u * height - row - offset - 2u;
        const float* const first = source + static_cast<std::size_t>(above) * rowStride;
        const float* const second = source + static_cast<std::size_t>(below) * rowStride;
        const float coefficient = taps[radius - offset];
        const auto factor = hn::Set(tag, coefficient);
        for (x = 0u; x + lanes <= count; x += lanes)
        {
            const auto pair = hn::Add(hn::LoadU(tag, first + x), hn::LoadU(tag, second + x));
            hn::StoreU(hn::Add(hn::LoadU(tag, destination + x), hn::Mul(factor, pair)), tag, destination + x);
        }
        for (; x < count; ++x)
        {
            destination[x] += coefficient * (first[x] + second[x]);
        }
    }
}

/// One interior run of the four-direction edge tightening; see `EdgeTightenRowRunFn`.
///
/// Lane for lane and in its order, the arithmetic is the scalar body's: every term of every
/// response is multiplied and added - a zero coefficient's too, so a non-finite neighbour answers as
/// it does there - and the clamp compares as `std::clamp` does, so a NaN passes through it. The last
/// partial vector is loaded and stored as a vector, so no sample of the run goes through scalar code
/// a compiler could contract differently.
void edgeTightenRowFloat32(const float* above, const float* current, const float* below, std::uint64_t count,
                           const std::array<double, 9>* kernels, double amount, double offset, double lowest,
                           double highest, float* destination) noexcept
{
    const hn::ScalableTag<double> wide;
    const hn::Rebind<float, decltype(wide)> narrow;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(wide));
    const std::array<const float*, 3> rows{above, current, below};
    const auto twentieth = hn::Set(wide, 0.05);
    const auto scale = hn::Set(wide, amount);
    const auto shift = hn::Set(wide, offset);
    const auto low = hn::Set(wide, lowest);
    const auto high = hn::Set(wide, highest);
    for (std::uint64_t x = 0u; x < count; x += lanes)
    {
        const std::size_t loaded = static_cast<std::size_t>((std::min)(lanes, count - x));
        std::array<hn::Vec<decltype(wide)>, 9> taps;
        for (std::size_t row = 0u; row < rows.size(); ++row)
        {
            for (std::size_t column = 0u; column < 3u; ++column)
            {
                const float* const at = rows[row] + (x + column) - 1;
                taps[row * 3u + column] =
                    hn::PromoteTo(wide, loaded == lanes ? hn::LoadU(narrow, at) : hn::LoadN(narrow, at, loaded));
            }
        }
        auto magnitude = hn::Zero(narrow);
        for (std::size_t direction = 0u; direction < 4u; ++direction)
        {
            auto sum = hn::Zero(wide);
            for (std::size_t tap = 0u; tap < taps.size(); ++tap)
            {
                sum = hn::Add(sum, hn::Mul(hn::Set(wide, kernels[direction][tap]), taps[tap]));
            }
            const auto response = hn::Abs(hn::DemoteTo(narrow, sum));
            magnitude = direction == 0u ? response : hn::Add(magnitude, response);
        }
        const auto edge = hn::Mul(twentieth, hn::PromoteTo(wide, magnitude));
        const auto value = hn::Add(hn::Sub(taps[4], hn::Mul(scale, edge)), shift);
        const auto held = hn::IfThenElse(hn::Lt(value, low), low, hn::IfThenElse(hn::Lt(high, value), high, value));
        const auto narrowed = hn::DemoteTo(narrow, held);
        if (loaded == lanes)
        {
            hn::StoreU(narrowed, narrow, destination + x);
        }
        else
        {
            hn::StoreN(narrowed, narrow, destination + x, loaded);
        }
    }
}

/// Stores a vector of the preview gamma curve and the entries it decides: the floor of the curve times the largest
/// code where the slack either side cannot move it, a NaN elsewhere.
template <class D, class V>
HWY_INLINE void decidePreviewEntries(D tag, V value, V maximum, V below, V above, V undecided, double* curve,
                                     double* entries)
{
    const V scaled = hn::Mul(value, maximum);
    const V low = hn::Floor(hn::Mul(scaled, below));
    const V high = hn::Floor(hn::Mul(scaled, above));
    hn::StoreU(value, tag, curve);
    hn::StoreU(hn::IfThenElse(hn::Eq(low, high), hn::Div(low, maximum), undecided), tag, entries);
}

/// The real-time preview's gamma table through the vector power; see `PreviewGammaTableRunFn`.
///
/// The curve is evaluated in Float32 lanes - the quotient, the logarithm and the exponential - which is what makes
/// the table cheaper than the scalar power, and is decided in Float64 against the slack
/// `previewGammaCurveSlack` states for the exponent, so a floor taken on either side of it agrees exactly where the
/// exact power's would; an entry whose two floors differ is left for the caller.
double previewGammaTableFloat32(double exponent, double codeMaximum, std::uint32_t count, double* curve,
                                double* entries, double* errors) noexcept
{
    const double slack = previewGammaCurveSlack(exponent);
    const hn::ScalableTag<float> narrow;
    const hn::Repartition<double, decltype(narrow)> wide;
    const std::uint32_t lanes = static_cast<std::uint32_t>(hn::Lanes(narrow));
    const std::uint32_t half = static_cast<std::uint32_t>(hn::Lanes(wide));
    const auto power = hn::Set(narrow, static_cast<float>(exponent));
    const auto codes = hn::Set(narrow, static_cast<float>(codeMaximum));
    const auto maximum = hn::Set(wide, codeMaximum);
    const auto below = hn::Set(wide, 1.0 - slack);
    const auto above = hn::Set(wide, 1.0 + slack);
    const auto undecided = hn::Set(wide, std::numeric_limits<double>::quiet_NaN());
    std::uint32_t index = 1u;
    for (; index + lanes <= count; index += lanes)
    {
        const auto input = hn::Div(hn::Iota(narrow, static_cast<float>(index)), codes);
        const auto value = hn::Exp(narrow, hn::Mul(power, hn::Log(narrow, input)));
        decidePreviewEntries(wide, hn::PromoteLowerTo(wide, value), maximum, below, above, undecided, curve + index,
                             entries + index);
        decidePreviewEntries(wide, hn::PromoteUpperTo(wide, value), maximum, below, above, undecided,
                             curve + index + half, entries + index + half);
    }
    for (; index < count; ++index)
    {
        const double value = std::exp(exponent * std::log(static_cast<double>(index) / codeMaximum));
        const double scaled = value * codeMaximum;
        const double low = std::floor(scaled * (1.0 - slack));
        curve[index] = value;
        entries[index] =
            low == std::floor(scaled * (1.0 + slack)) ? low / codeMaximum : std::numeric_limits<double>::quiet_NaN();
    }
    // Each interval's distance from the entry below it; one below an undecided entry is a NaN and left out of the
    // largest.
    auto largestLanes = hn::Zero(wide);
    index = 1u;
    for (; index + half <= count; index += half)
    {
        const auto distance = hn::Abs(hn::Sub(hn::LoadU(wide, entries + index - 1u), hn::LoadU(wide, curve + index)));
        hn::StoreU(distance, wide, errors + index);
        largestLanes = hn::IfThenElse(hn::Gt(distance, largestLanes), distance, largestLanes);
    }
    double largest = hn::ReduceMax(wide, largestLanes);
    for (; index < count; ++index)
    {
        const double distance = std::abs(entries[index - 1u] - curve[index]);
        errors[index] = distance;
        largest = distance > largest ? distance : largest;
    }
    return largest;
}

/// One region of a prepared curve, broadcast across a run of lanes that all lie in it.
///
/// `at` evaluates the curve as the tone curve's evaluator does there. Below the first point it is the first point's
/// output, or under extrapolation that output plus the input's distance from the point times the first segment's
/// secant; above the last point the same from the last point and the last segment. On a segment the position is
/// the input's distance from the left point over the segment's width; a straight segment interpolates its two
/// outputs, and a cubic one weighs its two outputs and its two derivatives, each scaled by the width, by the Hermite
/// basis functions of the position. Every operation is in the evaluator's order.
template <class D> class CurveRegionLanes
{
public:
    CurveRegionLanes(D tag, const CurveTableShape& shape, std::uint32_t region) noexcept
    {
        const std::uint32_t points = shape.pointCount;
        if (region == 0u || region == points)
        {
            const std::uint32_t edge = region == 0u ? 0u : points - 1u;
            const std::uint32_t adjacent = region == 0u ? 1u : points - 2u;
            m_mode = shape.extrapolate ? Mode::Extrapolated : Mode::Constant;
            m_leftInput = hn::Set(tag, shape.inputs[edge]);
            m_leftOutput = hn::Set(tag, shape.outputs[edge]);
            m_width = hn::Set(tag, (shape.outputs[edge] - shape.outputs[adjacent]) /
                                       (shape.inputs[edge] - shape.inputs[adjacent]));
            m_rightOutput = m_leftOutput;
            m_leftTangent = hn::Zero(tag);
            m_rightTangent = hn::Zero(tag);
            return;
        }
        const std::uint32_t segment = region - 1u;
        m_mode = shape.segmentTangents == nullptr ? Mode::Straight : Mode::Cubic;
        m_leftInput = hn::Set(tag, shape.inputs[segment]);
        m_leftOutput = hn::Set(tag, shape.outputs[segment]);
        m_rightOutput = hn::Set(tag, shape.outputs[segment + 1u]);
        m_width = hn::Set(tag, shape.inputs[segment + 1u] - shape.inputs[segment]);
        m_leftTangent = hn::Set(tag, m_mode == Mode::Cubic ? shape.segmentTangents[2u * segment] : 0.0);
        m_rightTangent = hn::Set(tag, m_mode == Mode::Cubic ? shape.segmentTangents[2u * segment + 1u] : 0.0);
    }

    [[nodiscard]] HWY_INLINE hn::Vec<D> at(D tag, hn::Vec<D> input) const noexcept
    {
        switch (m_mode)
        {
        case Mode::Constant:
            return m_leftOutput;
        case Mode::Extrapolated:
            // The secant is held where a segment's width is.
            return hn::Add(m_leftOutput, hn::Mul(hn::Sub(input, m_leftInput), m_width));
        case Mode::Straight:
            return hn::Add(m_leftOutput, hn::Mul(hn::Sub(m_rightOutput, m_leftOutput),
                                                 hn::Div(hn::Sub(input, m_leftInput), m_width)));
        case Mode::Cubic:
            break;
        }
        const auto position = hn::Div(hn::Sub(input, m_leftInput), m_width);
        const auto one = hn::Set(tag, 1.0);
        const auto two = hn::Set(tag, 2.0);
        const auto three = hn::Set(tag, 3.0);
        const auto squared = hn::Mul(position, position);
        const auto cubed = hn::Mul(squared, position);
        const auto h00 = hn::Add(hn::Sub(hn::Mul(two, cubed), hn::Mul(three, squared)), one);
        const auto h10 = hn::Add(hn::Sub(cubed, hn::Mul(two, squared)), position);
        const auto h01 = hn::Add(hn::Mul(hn::Set(tag, -2.0), cubed), hn::Mul(three, squared));
        const auto h11 = hn::Sub(cubed, squared);
        return hn::Add(hn::Add(hn::Add(hn::Mul(h00, m_leftOutput), hn::Mul(hn::Mul(h10, m_width), m_leftTangent)),
                               hn::Mul(h01, m_rightOutput)),
                       hn::Mul(hn::Mul(h11, m_width), m_rightTangent));
    }

private:
    enum class Mode : std::uint8_t
    {
        Constant,
        Extrapolated,
        Straight,
        Cubic
    };

    Mode m_mode = Mode::Constant;
    hn::Vec<D> m_leftInput;
    hn::Vec<D> m_leftOutput;
    hn::Vec<D> m_rightOutput;
    hn::Vec<D> m_width;
    hn::Vec<D> m_leftTangent;
    hn::Vec<D> m_rightTangent;
};

/// The first index from `index` to `end` whose input lies in a later region than `region`, or `end`.
///
/// The inputs rise with the index and their regions with the inputs, so the run of indices in one region is found
/// by halving.
template <typename InputOf>
[[nodiscard]] std::uint32_t curveRegionRunEnd(const CurveTableShape& shape, std::uint32_t region, std::uint32_t index,
                                              std::uint32_t end, InputOf inputOf) noexcept
{
    if (region == shape.pointCount)
    {
        return end;
    }
    std::uint32_t low = index + 1u;
    std::uint32_t high = end;
    while (low < high)
    {
        const std::uint32_t middle = low + (high - low) / 2u;
        if (curveTableRegion(shape, inputOf(middle)) > region)
        {
            high = middle;
        }
        else
        {
            low = middle + 1u;
        }
    }
    return low;
}

/// A curve table's entries `begin` to `end`; see `CurveTableEntriesRunFn`.
///
/// The indices in one region of the curve are one run, so each run broadcasts its region and evaluates whole
/// vectors of consecutive entries; an entry's input is formed in the lanes as `curveTableEntryInput` forms it.
double curveTableEntriesRun(const CurveTableShape& shape, std::uint32_t begin, std::uint32_t end,
                            double* entries) noexcept
{
    const hn::ScalableTag<double> tag;
    const std::uint32_t lanes = static_cast<std::uint32_t>(hn::Lanes(tag));
    const double low = shape.inputs[0];
    const double high = shape.inputs[shape.pointCount - 1u];
    const auto lowLanes = hn::Set(tag, low);
    const auto highLanes = hn::Set(tag, high);
    const auto span = hn::Set(tag, high - low);
    const auto lastIndex = hn::Set(tag, static_cast<double>(shape.entryCount - 1u));
    const auto zero = hn::Zero(tag);
    const auto one = hn::Set(tag, 1.0);
    const auto inputOf = [&shape](std::uint32_t index) { return curveTableEntryInput(shape, index); };
    auto heldBy = hn::Zero(tag);
    std::uint32_t index = begin;
    while (index < end)
    {
        const std::uint32_t region = curveTableRegion(shape, inputOf(index));
        const std::uint32_t runEnd = curveRegionRunEnd(shape, region, index, end, inputOf);
        const CurveRegionLanes<decltype(tag)> curve(tag, shape, region);
        for (; index < runEnd; index += lanes)
        {
            const std::uint32_t active = (std::min)(lanes, runEnd - index);
            const auto at = hn::Iota(tag, static_cast<double>(index));
            const auto input = shape.codeInputs
                                   ? hn::Div(at, lastIndex)
                                   : hn::IfThenElse(hn::Eq(at, lastIndex), highLanes,
                                                    hn::Add(lowLanes, hn::Div(hn::Mul(span, at), lastIndex)));
            const auto value = curve.at(tag, input);
            auto held = hn::IfThenElse(hn::Lt(value, zero), zero, value);
            held = hn::IfThenElse(hn::Lt(one, held), one, held);
            const auto distance = hn::Abs(hn::Sub(held, value));
            heldBy = hn::IfThenElse(hn::And(hn::FirstN(tag, active), hn::Lt(heldBy, distance)), distance, heldBy);
            hn::StoreN(held, tag, entries + index, active);
        }
        index = runEnd;
    }
    return hn::ReduceMax(tag, heldBy);
}

/// A sampled curve table's largest distance from its curve over intervals `begin` to `end`; see
/// `CurveTableProbesRunFn`.
///
/// Each probe of an interval is one pass, its intervals in runs in one region like the entries; the two entries a
/// probe reads are the interval's own wherever every lane of a vector finds them there, and gathered where not.
double curveTableProbesRun(const CurveTableShape& shape, const double* entries, std::uint32_t begin,
                           std::uint32_t end) noexcept
{
    const hn::ScalableTag<double> tag;
    const hn::RebindToSigned<decltype(tag)> indexTag;
    const std::uint32_t lanes = static_cast<std::uint32_t>(hn::Lanes(tag));
    const std::uint32_t count = shape.entryCount;
    const double low = shape.inputs[0];
    const double high = shape.inputs[shape.pointCount - 1u];
    const auto lowLanes = hn::Set(tag, low);
    const auto span = hn::Set(tag, high - low);
    const auto lastIndex = hn::Set(tag, static_cast<double>(count - 1u));
    const auto lastLower = hn::Set(tag, static_cast<double>(count - 2u));
    const auto zero = hn::Zero(tag);
    auto largest = hn::Zero(tag);
    for (std::uint32_t probe = 1u; probe < kCurveTableProbesPerInterval; ++probe)
    {
        const auto share = hn::Set(tag, static_cast<double>(probe) / kCurveTableProbesPerInterval);
        const auto inputOf = [&](std::uint32_t index) { return curveTableProbeInput(low, high, index, probe, count); };
        std::uint32_t index = begin;
        while (index < end)
        {
            const std::uint32_t region = curveTableRegion(shape, inputOf(index));
            const std::uint32_t runEnd = curveRegionRunEnd(shape, region, index, end, inputOf);
            const CurveRegionLanes<decltype(tag)> curve(tag, shape, region);
            for (; index < runEnd; index += lanes)
            {
                const std::uint32_t active = (std::min)(lanes, runEnd - index);
                const auto at = hn::Iota(tag, static_cast<double>(index));
                const auto input = hn::Add(lowLanes, hn::Mul(span, hn::Div(hn::Add(at, share), lastIndex)));
                const auto expected = curve.at(tag, input);
                auto position = hn::Mul(hn::Div(hn::Sub(input, lowLanes), span), lastIndex);
                position = hn::IfThenElse(hn::Lt(position, zero), zero, position);
                position = hn::IfThenElse(hn::Lt(lastIndex, position), lastIndex, position);
                const auto floored = hn::Floor(position);
                const auto lower = hn::IfThenElse(hn::Lt(lastLower, floored), lastLower, floored);
                const auto fraction = hn::Sub(position, lower);
                hn::Vec<decltype(tag)> first;
                hn::Vec<decltype(tag)> second;
                if (active == lanes && hn::AllTrue(tag, hn::Eq(lower, at)))
                {
                    first = hn::LoadU(tag, entries + index);
                    second = hn::LoadU(tag, entries + index + 1u);
                }
                else
                {
                    const auto entry = hn::ConvertTo(indexTag, lower);
                    first = hn::GatherIndex(tag, entries, entry);
                    second = hn::GatherIndex(tag, entries + 1, entry);
                }
                const auto sampled = hn::Add(first, hn::Mul(hn::Sub(second, first), fraction));
                const auto distance = hn::Abs(hn::Sub(sampled, expected));
                largest =
                    hn::IfThenElse(hn::And(hn::FirstN(tag, active), hn::Lt(largest, distance)), distance, largest);
            }
            index = runEnd;
        }
    }
    return hn::ReduceMax(tag, largest);
}

/// A lane of widened samples held to a hold as the scalar reads hold a sample: the lower end first and then the
/// upper, each a comparison and a choice rather than a lane minimum, so that a signed zero and a value at an end come
/// out as the standard minimum and maximum give them.
///
/// @tparam VectorT the lane vector type.
/// @param value the widened samples.
/// @param holdLowest the hold's first value in every lane.
/// @param holdHighest its last value in every lane.
/// @return the held samples.
template <typename VectorT>
HWY_INLINE VectorT heldLanes(VectorT value, VectorT holdLowest, VectorT holdHighest) noexcept
{
    const VectorT raised = hn::IfThenElse(hn::Lt(value, holdLowest), holdLowest, value);
    return hn::IfThenElse(hn::Lt(holdHighest, raised), holdHighest, raised);
}

/// One sample held as the scalar reads hold it.
HWY_INLINE double heldSample(float stored, const OrderRead& read) noexcept
{
    return (std::min)((std::max)(static_cast<double>(stored), read.holdLowest), read.holdHighest);
}

/// What the whole blocks of one bracket count counted: where the samples after the last whole block begin, and the
/// counts below and above the bracket.
struct OrderCountBlocks
{
    /// The index of the first sample after the last whole block.
    std::uint64_t index = 0u;

    /// The count below the bracket.
    std::uint64_t below = 0u;

    /// The count above it.
    std::uint64_t above = 0u;
};

/// The whole blocks of one bracket count, with or without a hold.
///
/// A block's samples are widened, held to the bracket's hold when one is read, ordered, held to the bracket and
/// mapped to their counters in lanes - the truncation taken after the hold to the last bin, which leaves every bin
/// the scalar loop finds - and its counters are then incremented one after another in the block's order, as the
/// scalar loop increments them. A not-a-number lane is held to the hold's end and left uncounted by the window, as
/// the scalar loop leaves it. The counts are kept in this function's own locals and returned, so that the compiler
/// keeps them in registers beside the counter stores.
///
/// @tparam held whether the bracket's hold reads the samples.
/// @param samples the first sample.
/// @param length how many consecutive samples the run holds.
/// @param deviation whether to count absolute deviations from `center`.
/// @param center the value deviations are taken around.
/// @param bracket the bracket, its window and its hold.
/// @param bins the counters.
/// @return where the whole blocks ended and what they counted.
template <bool held>
HWY_INLINE OrderCountBlocks orderCountFloat32Blocks(const float* samples, std::uint64_t length, bool deviation,
                                                    double center, const OrderBracket& bracket,
                                                    std::uint64_t* bins) noexcept
{
    const hn::ScalableTag<double> wide;
    const hn::Rebind<float, decltype(wide)> narrow;
    const hn::Rebind<std::int32_t, decltype(wide)> counters;
    const std::size_t lanes = hn::Lanes(wide);
    const auto lowest = hn::Set(wide, bracket.lowest);
    const auto highest = hn::Set(wide, bracket.highest);
    const auto scale = hn::Set(wide, bracket.scale);
    const auto centre = hn::Set(wide, center);
    const auto lastBin = hn::Set(wide, static_cast<double>(bracket.bins - 1u));
    const auto windowLowest = hn::Set(wide, bracket.windowLowest);
    const auto windowHighest = hn::Set(wide, bracket.windowHighest);
    const auto holdLowest = hn::Set(wide, bracket.holdLowest);
    const auto holdHighest = hn::Set(wide, bracket.holdHighest);
    const auto discardedFirst = hn::Iota(wide, static_cast<double>(bracket.bins));
    std::array<std::int32_t, kOrderCountBlock> target{};
    OrderCountBlocks counted;
    for (; counted.index + kOrderCountBlock <= length; counted.index += kOrderCountBlock)
    {
        for (std::size_t lane = 0u; lane < kOrderCountBlock; lane += lanes)
        {
            const auto value = hn::PromoteTo(wide, hn::LoadU(narrow, samples + counted.index + lane));
            const auto inside = hn::And(hn::Ge(value, windowLowest), hn::Le(value, windowHighest));
            const auto read = held ? heldLanes(value, holdLowest, holdHighest) : value;
            const auto ordered = deviation ? hn::Abs(hn::Sub(read, centre)) : read;
            const auto low = hn::And(hn::Lt(ordered, lowest), inside);
            const auto high = hn::And(hn::Gt(ordered, highest), inside);
            counted.below += static_cast<std::uint64_t>(hn::CountTrue(wide, low));
            counted.above += static_cast<std::uint64_t>(hn::CountTrue(wide, high));
            const auto bracketed = hn::Min(hn::Max(ordered, lowest), highest);
            const auto bin = hn::Min(hn::Mul(hn::Sub(bracketed, lowest), scale), lastBin);
            const auto discarded = hn::Add(discardedFirst, hn::Set(wide, static_cast<double>(lane)));
            const auto uncounted = hn::Or(hn::Not(inside), hn::Or(low, high));
            hn::StoreU(hn::DemoteInRangeTo(counters, hn::IfThenElse(uncounted, discarded, bin)), counters,
                       target.data() + lane);
        }
        for (const std::int32_t counter : target)
        {
            ++bins[counter];
        }
    }
    return counted;
}

/// An exact selection's bracket count over consecutive Float32 samples; see `OrderCountFloat32RunFn`.
///
/// The whole blocks are counted with the hold's arithmetic only when a hold reads the selection, and the samples
/// after the last whole block one at a time.
void orderCountFloat32Run(const float* samples, std::uint64_t length, bool deviation, double center,
                          const OrderBracket& bracket, std::uint64_t* bins, std::uint64_t& below,
                          std::uint64_t& above) noexcept
{
    const bool holds = std::isfinite(bracket.holdLowest) || std::isfinite(bracket.holdHighest);
    const OrderCountBlocks counted =
        holds ? orderCountFloat32Blocks<true>(samples, length, deviation, center, bracket, bins)
              : orderCountFloat32Blocks<false>(samples, length, deviation, center, bracket, bins);
    std::uint64_t lowCount = counted.below;
    std::uint64_t highCount = counted.above;
    std::uint64_t index = counted.index;
    const auto last = static_cast<std::int32_t>(bracket.bins - 1u);
    for (; index < length; ++index)
    {
        const double value = static_cast<double>(samples[index]);
        if (!(value >= bracket.windowLowest && value <= bracket.windowHighest))
        {
            continue;
        }
        const double read = (std::min)((std::max)(value, bracket.holdLowest), bracket.holdHighest);
        const double ordered = deviation ? std::abs(read - center) : read;
        lowCount += static_cast<std::uint64_t>(ordered < bracket.lowest);
        highCount += static_cast<std::uint64_t>(ordered > bracket.highest);
        if ((ordered >= bracket.lowest) & (ordered <= bracket.highest))
        {
            const auto bin = static_cast<std::int32_t>((ordered - bracket.lowest) * bracket.scale);
            ++bins[bin < last ? bin : last];
        }
    }
    below += lowCount;
    above += highCount;
}

/// A count of consecutive Float32 samples inside a window with the extremes of their held values; see
/// `OrderExtremesFloat32RunFn`.
///
/// Each lane keeps an extreme under the scalar count's rule - replaced only by a smaller or a larger value - and the
/// lanes are then reduced in their order under the same rule. That keeps the first of equal values within a lane and
/// then across lanes, which is not the first in the samples' order when two zeros of either sign share an extreme, so
/// the run's first zero is found in the lanes of the first block holding one and a zero extreme is that zero.
void orderExtremesFloat32Run(const float* samples, std::uint64_t length, const OrderRead& read, std::uint64_t& count,
                             double& minimum, double& maximum) noexcept
{
    const hn::ScalableTag<double> wide;
    const hn::Rebind<float, decltype(wide)> narrow;
    const std::size_t lanes = hn::Lanes(wide);
    const auto windowLowest = hn::Set(wide, read.windowLowest);
    const auto windowHighest = hn::Set(wide, read.windowHighest);
    const auto holdLowest = hn::Set(wide, read.holdLowest);
    const auto holdHighest = hn::Set(wide, read.holdHighest);
    const auto positiveInfinity = hn::Set(wide, std::numeric_limits<double>::infinity());
    const auto negativeInfinity = hn::Set(wide, -std::numeric_limits<double>::infinity());
    const auto zero = hn::Zero(wide);
    auto smallest = positiveInfinity;
    auto largest = negativeInfinity;
    std::uint64_t counted = 0u;
    bool zeroSeen = false;
    double firstZero = 0.0;
    std::uint64_t index = 0u;
    for (; index + lanes <= length; index += lanes)
    {
        const auto value = hn::PromoteTo(wide, hn::LoadU(narrow, samples + index));
        const auto inside = hn::And(hn::Ge(value, windowLowest), hn::Le(value, windowHighest));
        const auto held = heldLanes(value, holdLowest, holdHighest);
        counted += static_cast<std::uint64_t>(hn::CountTrue(wide, inside));
        const auto low = hn::IfThenElse(inside, held, positiveInfinity);
        const auto high = hn::IfThenElse(inside, held, negativeInfinity);
        smallest = hn::IfThenElse(hn::Lt(low, smallest), low, smallest);
        largest = hn::IfThenElse(hn::Lt(largest, high), high, largest);
        if (!zeroSeen)
        {
            const intptr_t firstLane = hn::FindFirstTrue(wide, hn::And(inside, hn::Eq(held, zero)));
            if (firstLane >= 0)
            {
                zeroSeen = true;
                firstZero = heldSample(samples[index + static_cast<std::size_t>(firstLane)], read);
            }
        }
    }
    double runMinimum = std::numeric_limits<double>::infinity();
    double runMaximum = -std::numeric_limits<double>::infinity();
    HWY_ALIGN double laneValues[hn::MaxLanes(wide)];
    hn::Store(smallest, wide, laneValues);
    for (std::size_t lane = 0u; lane < lanes; ++lane)
    {
        runMinimum = laneValues[lane] < runMinimum ? laneValues[lane] : runMinimum;
    }
    hn::Store(largest, wide, laneValues);
    for (std::size_t lane = 0u; lane < lanes; ++lane)
    {
        runMaximum = runMaximum < laneValues[lane] ? laneValues[lane] : runMaximum;
    }
    for (; index < length; ++index)
    {
        const double value = static_cast<double>(samples[index]);
        if (!(value >= read.windowLowest && value <= read.windowHighest))
        {
            continue;
        }
        ++counted;
        const double held = heldSample(samples[index], read);
        runMinimum = held < runMinimum ? held : runMinimum;
        runMaximum = runMaximum < held ? held : runMaximum;
        if (!zeroSeen && held == 0.0)
        {
            zeroSeen = true;
            firstZero = held;
        }
    }
    if (zeroSeen)
    {
        runMinimum = runMinimum == 0.0 ? firstZero : runMinimum;
        runMaximum = runMaximum == 0.0 ? firstZero : runMaximum;
    }
    count += counted;
    minimum = runMinimum < minimum ? runMinimum : minimum;
    maximum = maximum < runMaximum ? runMaximum : maximum;
}

/// A gather of one bin's members from consecutive Float32 samples, with the smallest ordered value above the bin;
/// see `OrderGatherFloat32RunFn`.
///
/// A block's membership and its candidates above the bin are decided in lanes; a block holding a member is then
/// walked in the samples' order and its members appended, each ordered in scalar arithmetic as the block loop orders
/// it, and written only inside the bin's place. The running minima above the bin are kept per position in a block,
/// loaded and stored around each block, so a run over one row of a chunk continues the positions the row before it
/// left.
void orderGatherFloat32Run(const float* samples, std::uint64_t length, bool deviation, double center,
                           const OrderRead& read, double first, double last, double* gathered, std::uint64_t end,
                           std::uint64_t& write, double* nextAbove) noexcept
{
    const hn::ScalableTag<double> wide;
    const hn::Rebind<float, decltype(wide)> narrow;
    const std::size_t lanes = hn::Lanes(wide);
    const auto windowLowest = hn::Set(wide, read.windowLowest);
    const auto windowHighest = hn::Set(wide, read.windowHighest);
    const auto holdLowest = hn::Set(wide, read.holdLowest);
    const auto holdHighest = hn::Set(wide, read.holdHighest);
    const auto centre = hn::Set(wide, center);
    const auto firstValue = hn::Set(wide, first);
    const auto lastValue = hn::Set(wide, last);
    const auto none = hn::Set(wide, std::numeric_limits<double>::infinity());
    const auto orderedSample = [&](float stored) noexcept {
        const double held = heldSample(stored, read);
        return deviation ? std::abs(held - center) : held;
    };
    std::uint64_t index = 0u;
    for (; index + kOrderCountBlock <= length; index += kOrderCountBlock)
    {
        bool any = false;
        for (std::size_t lane = 0u; lane < kOrderCountBlock; lane += lanes)
        {
            const auto value = hn::PromoteTo(wide, hn::LoadU(narrow, samples + index + lane));
            const auto inside = hn::And(hn::Ge(value, windowLowest), hn::Le(value, windowHighest));
            const auto held = heldLanes(value, holdLowest, holdHighest);
            const auto ordered = deviation ? hn::Abs(hn::Sub(held, centre)) : held;
            const auto member = hn::And(inside, hn::And(hn::Ge(ordered, firstValue), hn::Le(ordered, lastValue)));
            any |= !hn::AllFalse(wide, member);
            if (nextAbove != nullptr)
            {
                const auto candidate = hn::IfThenElse(hn::And(inside, hn::Gt(ordered, lastValue)), ordered, none);
                const auto running = hn::LoadU(wide, nextAbove + lane);
                hn::StoreU(hn::IfThenElse(hn::Lt(candidate, running), candidate, running), wide, nextAbove + lane);
            }
        }
        if (any)
        {
            for (std::size_t lane = 0u; lane < kOrderCountBlock; ++lane)
            {
                const float stored = samples[index + lane];
                const double value = static_cast<double>(stored);
                if (!(value >= read.windowLowest && value <= read.windowHighest))
                {
                    continue;
                }
                const double ordered = orderedSample(stored);
                if ((ordered >= first) & (ordered <= last))
                {
                    if (write < end)
                    {
                        gathered[write] = ordered;
                    }
                    ++write;
                }
            }
        }
    }
    for (; index < length; ++index)
    {
        const float stored = samples[index];
        const double value = static_cast<double>(stored);
        if (!(value >= read.windowLowest && value <= read.windowHighest))
        {
            continue;
        }
        const double ordered = orderedSample(stored);
        if ((ordered >= first) & (ordered <= last))
        {
            if (write < end)
            {
                gathered[write] = ordered;
            }
            ++write;
        }
        else if (nextAbove != nullptr && ordered > last && ordered < nextAbove[0])
        {
            nextAbove[0] = ordered;
        }
    }
}

/// The extremes one count of a set of brackets kept over a run: each lane's, and the first zero counted, whose sign a
/// zero extreme takes.
struct OrderRunExtremes
{
    /// The run's smallest held value.
    double minimum = std::numeric_limits<double>::infinity();

    /// The run's largest held value.
    double maximum = -std::numeric_limits<double>::infinity();

    /// Whether a zero of either sign was counted.
    bool zeroSeen = false;

    /// The first zero counted, with its sign.
    double firstZero = 0.0;
};

/// The whole blocks of one count of a set of brackets, with or without a hold.
///
/// A block's samples are widened, held when a hold reads them, counted with their extremes, tallied and mapped to their
/// counters in lanes. The extremes are kept per lane, replaced only by a smaller or a larger value, and reduced in lane
/// order, with the first zero counted found in the lanes of the first block holding one - as `orderExtremesFloat32Run`
/// keeps them. Each lane takes the first value, the last value, the scale and the first bin of the bracket it lies in;
/// its distance above that first value is held to the bracket and scaled, held to the bracket's last bin and
/// truncated, and only then offset to the bracket's bins - the order the scalar loop maps a sample in, so that no
/// rounding of the offset sum can move a bin. A lane in no bracket takes its discarded counter. The counters are then
/// incremented one after another in the block's order, as the scalar loop increments them, and the counts are kept in
/// locals so that the compiler keeps them in registers beside the counter stores.
///
/// @tparam held whether the brackets' hold reads the samples.
/// @param samples the first sample.
/// @param length how many consecutive samples the run holds.
/// @param brackets the brackets, the window and the hold.
/// @param bins the counters.
/// @param tallies where the blocks' count and tallies are added.
/// @param extremes where the blocks' extremes go.
/// @return the index of the first sample after the last whole block.
template <bool held>
HWY_INLINE std::uint64_t orderCountBracketSetFloat32Blocks(const float* samples, std::uint64_t length,
                                                           const OrderBracketSet& brackets, std::uint32_t* bins,
                                                           OrderBracketTallies& tallies,
                                                           OrderRunExtremes& extremes) noexcept
{
    const hn::ScalableTag<double> wide;
    const hn::Rebind<float, decltype(wide)> narrow;
    const hn::Rebind<std::int32_t, decltype(wide)> counters;
    const std::size_t lanes = hn::Lanes(wide);
    const auto firstLowest = hn::Set(wide, brackets.lowest[0]);
    const auto secondLowest = hn::Set(wide, brackets.lowest[1]);
    const auto thirdLowest = hn::Set(wide, brackets.lowest[2]);
    const auto firstHighest = hn::Set(wide, brackets.highest[0]);
    const auto secondHighest = hn::Set(wide, brackets.highest[1]);
    const auto thirdHighest = hn::Set(wide, brackets.highest[2]);
    const auto firstScale = hn::Set(wide, brackets.scale[0]);
    const auto secondScale = hn::Set(wide, brackets.scale[1]);
    const auto thirdScale = hn::Set(wide, brackets.scale[2]);
    const auto zero = hn::Zero(wide);
    const auto secondOffset = hn::Set(wide, static_cast<double>(brackets.bins));
    const auto thirdOffset = hn::Set(wide, 2.0 * static_cast<double>(brackets.bins));
    const auto lastBin = hn::Set(wide, static_cast<double>(brackets.bins - 1u));
    const auto windowLowest = hn::Set(wide, brackets.windowLowest);
    const auto windowHighest = hn::Set(wide, brackets.windowHighest);
    const auto holdLowest = hn::Set(wide, brackets.holdLowest);
    const auto holdHighest = hn::Set(wide, brackets.holdHighest);
    const auto positiveInfinity = hn::Set(wide, std::numeric_limits<double>::infinity());
    const auto negativeInfinity = hn::Set(wide, -std::numeric_limits<double>::infinity());
    const auto discardedFirst =
        hn::Iota(wide, static_cast<double>(kOrderBracketSetSize) * static_cast<double>(brackets.bins));
    auto smallest = positiveInfinity;
    auto largest = negativeInfinity;
    std::array<std::int32_t, kOrderCountBlock> target{};
    std::uint64_t counted = 0u;
    std::uint64_t belowFirst = 0u;
    std::uint64_t belowSecond = 0u;
    std::uint64_t aboveSecond = 0u;
    std::uint64_t aboveThird = 0u;
    std::uint64_t index = 0u;
    for (; index + kOrderCountBlock <= length; index += kOrderCountBlock)
    {
        for (std::size_t lane = 0u; lane < kOrderCountBlock; lane += lanes)
        {
            const auto value = hn::PromoteTo(wide, hn::LoadU(narrow, samples + index + lane));
            const auto inside = hn::And(hn::Ge(value, windowLowest), hn::Le(value, windowHighest));
            const auto read = held ? heldLanes(value, holdLowest, holdHighest) : value;
            counted += static_cast<std::uint64_t>(hn::CountTrue(wide, inside));
            const auto low = hn::IfThenElse(inside, read, positiveInfinity);
            const auto high = hn::IfThenElse(inside, read, negativeInfinity);
            smallest = hn::IfThenElse(hn::Lt(low, smallest), low, smallest);
            largest = hn::IfThenElse(hn::Lt(largest, high), high, largest);
            if (!extremes.zeroSeen)
            {
                const intptr_t firstLane = hn::FindFirstTrue(wide, hn::And(inside, hn::Eq(read, zero)));
                if (firstLane >= 0)
                {
                    extremes.zeroSeen = true;
                    extremes.firstZero =
                        (std::min)((std::max)(static_cast<double>(
                                                  samples[index + lane + static_cast<std::size_t>(firstLane)]),
                                              brackets.holdLowest),
                                   brackets.holdHighest);
                }
            }
            belowFirst += static_cast<std::uint64_t>(hn::CountTrue(wide, hn::And(hn::Lt(read, firstLowest), inside)));
            belowSecond += static_cast<std::uint64_t>(hn::CountTrue(wide, hn::And(hn::Lt(read, secondLowest), inside)));
            aboveSecond +=
                static_cast<std::uint64_t>(hn::CountTrue(wide, hn::And(hn::Gt(read, secondHighest), inside)));
            aboveThird += static_cast<std::uint64_t>(hn::CountTrue(wide, hn::And(hn::Gt(read, thirdHighest), inside)));
            const auto inFirst = hn::And(inside, hn::And(hn::Ge(read, firstLowest), hn::Le(read, firstHighest)));
            const auto inSecond = hn::And(inside, hn::And(hn::Ge(read, secondLowest), hn::Le(read, secondHighest)));
            const auto inThird = hn::And(inside, hn::And(hn::Ge(read, thirdLowest), hn::Le(read, thirdHighest)));
            const auto lowest =
                hn::IfThenElse(inFirst, firstLowest, hn::IfThenElse(inSecond, secondLowest, thirdLowest));
            const auto highest =
                hn::IfThenElse(inFirst, firstHighest, hn::IfThenElse(inSecond, secondHighest, thirdHighest));
            const auto scale = hn::IfThenElse(inFirst, firstScale, hn::IfThenElse(inSecond, secondScale, thirdScale));
            const auto offset = hn::IfThenElse(inFirst, zero, hn::IfThenElse(inSecond, secondOffset, thirdOffset));
            const auto bracketed = hn::Min(hn::Max(read, lowest), highest);
            const auto bin = hn::Add(hn::Floor(hn::Min(hn::Mul(hn::Sub(bracketed, lowest), scale), lastBin)), offset);
            const auto discarded = hn::Add(discardedFirst, hn::Set(wide, static_cast<double>(lane)));
            const auto inBracket = hn::Or(inFirst, hn::Or(inSecond, inThird));
            hn::StoreU(hn::DemoteInRangeTo(counters, hn::IfThenElse(inBracket, bin, discarded)), counters,
                       target.data() + lane);
        }
        for (const std::int32_t counter : target)
        {
            ++bins[counter];
        }
    }
    HWY_ALIGN double laneValues[hn::MaxLanes(wide)];
    hn::Store(smallest, wide, laneValues);
    for (std::size_t lane = 0u; lane < lanes; ++lane)
    {
        extremes.minimum = laneValues[lane] < extremes.minimum ? laneValues[lane] : extremes.minimum;
    }
    hn::Store(largest, wide, laneValues);
    for (std::size_t lane = 0u; lane < lanes; ++lane)
    {
        extremes.maximum = extremes.maximum < laneValues[lane] ? laneValues[lane] : extremes.maximum;
    }
    tallies.counted += counted;
    tallies.belowFirst += belowFirst;
    tallies.belowSecond += belowSecond;
    tallies.aboveSecond += aboveSecond;
    tallies.aboveThird += aboveThird;
    return index;
}

/// A count of a set of brackets over consecutive Float32 samples; see `OrderCountBracketSetFloat32RunFn`.
///
/// The whole blocks are counted with the hold's arithmetic only when a hold reads the selection, and the samples after
/// the last whole block one at a time. The run's extremes take the first zero's sign where they are zero, and are then
/// merged into the running ones as `orderExtremesFloat32Run` merges a run's.
void orderCountBracketSetFloat32Run(const float* samples, std::uint64_t length, const OrderBracketSet& brackets,
                                    std::uint32_t* bins, OrderBracketTallies& tallies) noexcept
{
    const bool holds = std::isfinite(brackets.holdLowest) || std::isfinite(brackets.holdHighest);
    OrderRunExtremes extremes;
    std::uint64_t index =
        holds ? orderCountBracketSetFloat32Blocks<true>(samples, length, brackets, bins, tallies, extremes)
              : orderCountBracketSetFloat32Blocks<false>(samples, length, brackets, bins, tallies, extremes);
    const auto last = static_cast<std::int32_t>(brackets.bins - 1u);
    for (; index < length; ++index)
    {
        const double value = static_cast<double>(samples[index]);
        if (!(value >= brackets.windowLowest && value <= brackets.windowHighest))
        {
            continue;
        }
        const double read = (std::min)((std::max)(value, brackets.holdLowest), brackets.holdHighest);
        ++tallies.counted;
        extremes.minimum = read < extremes.minimum ? read : extremes.minimum;
        extremes.maximum = extremes.maximum < read ? read : extremes.maximum;
        if (!extremes.zeroSeen && read == 0.0)
        {
            extremes.zeroSeen = true;
            extremes.firstZero = read;
        }
        tallies.belowFirst += static_cast<std::uint64_t>(read < brackets.lowest[0]);
        tallies.belowSecond += static_cast<std::uint64_t>(read < brackets.lowest[1]);
        tallies.aboveSecond += static_cast<std::uint64_t>(read > brackets.highest[1]);
        tallies.aboveThird += static_cast<std::uint64_t>(read > brackets.highest[2]);
        for (std::size_t bracket = 0u; bracket < kOrderBracketSetSize; ++bracket)
        {
            if ((read >= brackets.lowest[bracket]) & (read <= brackets.highest[bracket]))
            {
                const auto bin = static_cast<std::int32_t>((read - brackets.lowest[bracket]) * brackets.scale[bracket]);
                ++bins[static_cast<std::size_t>(bracket) * brackets.bins +
                       static_cast<std::size_t>(bin < last ? bin : last)];
                break;
            }
        }
    }
    if (extremes.zeroSeen)
    {
        extremes.minimum = extremes.minimum == 0.0 ? extremes.firstZero : extremes.minimum;
        extremes.maximum = extremes.maximum == 0.0 ? extremes.firstZero : extremes.maximum;
    }
    tallies.minimum = extremes.minimum < tallies.minimum ? extremes.minimum : tallies.minimum;
    tallies.maximum = tallies.maximum < extremes.maximum ? extremes.maximum : tallies.maximum;
}

/// A gather of a set of intervals' members from consecutive Float32 samples; see `OrderGatherIntervalSetFloat32RunFn`.
///
/// A block's membership is decided in lanes; a block holding a member is then walked in the samples' order and each
/// member appended at its interval's position, held in scalar arithmetic as the block loop holds it, and written only
/// inside its interval's place.
void orderGatherIntervalSetFloat32Run(const float* samples, std::uint64_t length, const OrderRead& read,
                                      const OrderIntervalSet& intervals, double* gathered,
                                      const std::array<std::uint64_t, kOrderBracketSetSize>& end,
                                      std::array<std::uint64_t, kOrderBracketSetSize>& write) noexcept
{
    const hn::ScalableTag<double> wide;
    const hn::Rebind<float, decltype(wide)> narrow;
    const std::size_t lanes = hn::Lanes(wide);
    const auto windowLowest = hn::Set(wide, read.windowLowest);
    const auto windowHighest = hn::Set(wide, read.windowHighest);
    const auto holdLowest = hn::Set(wide, read.holdLowest);
    const auto holdHighest = hn::Set(wide, read.holdHighest);
    const auto firstFirst = hn::Set(wide, intervals.first[0]);
    const auto secondFirst = hn::Set(wide, intervals.first[1]);
    const auto thirdFirst = hn::Set(wide, intervals.first[2]);
    const auto firstLast = hn::Set(wide, intervals.last[0]);
    const auto secondLast = hn::Set(wide, intervals.last[1]);
    const auto thirdLast = hn::Set(wide, intervals.last[2]);
    const auto take = [&](float stored) noexcept {
        const double value = static_cast<double>(stored);
        if (!(value >= read.windowLowest && value <= read.windowHighest))
        {
            return;
        }
        const double held = heldSample(stored, read);
        for (std::size_t interval = 0u; interval < kOrderBracketSetSize; ++interval)
        {
            if ((held >= intervals.first[interval]) & (held <= intervals.last[interval]))
            {
                if (write[interval] < end[interval])
                {
                    gathered[write[interval]] = held;
                }
                ++write[interval];
                return;
            }
        }
    };
    std::uint64_t index = 0u;
    for (; index + kOrderCountBlock <= length; index += kOrderCountBlock)
    {
        bool any = false;
        for (std::size_t lane = 0u; lane < kOrderCountBlock; lane += lanes)
        {
            const auto value = hn::PromoteTo(wide, hn::LoadU(narrow, samples + index + lane));
            const auto inside = hn::And(hn::Ge(value, windowLowest), hn::Le(value, windowHighest));
            const auto held = heldLanes(value, holdLowest, holdHighest);
            const auto inFirst = hn::And(hn::Ge(held, firstFirst), hn::Le(held, firstLast));
            const auto inSecond = hn::And(hn::Ge(held, secondFirst), hn::Le(held, secondLast));
            const auto inThird = hn::And(hn::Ge(held, thirdFirst), hn::Le(held, thirdLast));
            any |= !hn::AllFalse(wide, hn::And(inside, hn::Or(inFirst, hn::Or(inSecond, inThird))));
        }
        if (any)
        {
            for (std::size_t lane = 0u; lane < kOrderCountBlock; ++lane)
            {
                take(samples[index + lane]);
            }
        }
    }
    for (; index < length; ++index)
    {
        take(samples[index]);
    }
}

/// One run of two packed validity masks intersected.
///
/// A mask is bits rather than samples, so there is one body for every image this engine can carry
/// and its lane type is a byte. What makes this worth compiling per instruction set is only how wide
/// one load is: three megabytes of bits per twenty-four megapixel frame is bandwidth, and a byte at a
/// time is a thirty-second of what the packaged floor can move in one instruction.
void maskIntersectRun(const std::uint8_t* left, const std::uint8_t* right, std::uint8_t* destination,
                      std::uint64_t byteCount) noexcept
{
    const hn::ScalableTag<std::uint8_t> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    std::uint64_t index = 0u;
    for (; index + lanes <= byteCount; index += lanes)
    {
        hn::StoreU(hn::And(hn::LoadU(tag, left + index), hn::LoadU(tag, right + index)), tag, destination + index);
    }
    for (; index < byteCount; ++index)
    {
        destination[index] = static_cast<std::uint8_t>(left[index] & right[index]);
    }
}

/// One run of two packed validity masks united.
void maskUnionRun(const std::uint8_t* left, const std::uint8_t* right, std::uint8_t* destination,
                  std::uint64_t byteCount) noexcept
{
    const hn::ScalableTag<std::uint8_t> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    std::uint64_t index = 0u;
    for (; index + lanes <= byteCount; index += lanes)
    {
        hn::StoreU(hn::Or(hn::LoadU(tag, left + index), hn::LoadU(tag, right + index)), tag, destination + index);
    }
    for (; index < byteCount; ++index)
    {
        destination[index] = static_cast<std::uint8_t>(left[index] | right[index]);
    }
}

/// One run of a packed validity mask inverted.
void maskInvertRun(const std::uint8_t* source, std::uint8_t* destination, std::uint64_t byteCount) noexcept
{
    const hn::ScalableTag<std::uint8_t> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    std::uint64_t index = 0u;
    for (; index + lanes <= byteCount; index += lanes)
    {
        hn::StoreU(hn::Not(hn::LoadU(tag, source + index)), tag, destination + index);
    }
    for (; index < byteCount; ++index)
    {
        destination[index] = static_cast<std::uint8_t>(~source[index]);
    }
}

/// One run of a packed validity mask's set bits counted.
///
/// The per-byte counts are summed eight bytes at a time into sixty-four-bit lanes rather than being
/// accumulated in the byte lanes they were produced in, because a byte lane holds at most 255 and a
/// count of up to eight per load would overflow it after thirty-two loads.
std::uint64_t maskPopcountRun(const std::uint8_t* bits, std::uint64_t byteCount) noexcept
{
    const hn::ScalableTag<std::uint8_t> tag;
    const hn::Repartition<std::uint64_t, decltype(tag)> wideTag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    auto accumulator = hn::Zero(wideTag);
    std::uint64_t index = 0u;
    for (; index + lanes <= byteCount; index += lanes)
    {
        accumulator = hn::Add(accumulator, hn::SumsOf8(hn::PopulationCount(hn::LoadU(tag, bits + index))));
    }
    std::uint64_t total = hn::ReduceSum(wideTag, accumulator);
    for (; index < byteCount; ++index)
    {
        total += static_cast<std::uint64_t>(std::popcount(bits[index]));
    }
    return total;
}

/// One row of the plain chromatic green suppression, a vector of pixels at a time.
///
/// Every selection is spelled as the scalar row spells it - `ceiling < green ? ceiling : green`
/// and the two clamps likewise - through a compare and a blend rather than the lane minimum, whose
/// answer for a not-a-number is the other operand, so the bits stored here are the scalar row's.
///
/// @param red the first channel's row.
/// @param green the second channel's row.
/// @param blue the third channel's row.
/// @param outRed where the first channel goes.
/// @param outGreen where the held second channel goes.
/// @param outBlue where the third channel goes.
/// @param count samples per row.
/// @param whiteLevel the value that means fully bright.
/// @param averageNeutral whether the ceiling is half the sum of red and blue rather than their maximum.
/// @return how many stored samples differ from their inputs.
std::uint64_t chromaticCeilingFloat32(const float* red, const float* green, const float* blue, float* outRed,
                                      float* outGreen, float* outBlue, std::uint64_t count, float whiteLevel,
                                      bool averageNeutral) noexcept
{
    const hn::ScalableTag<float> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const auto zero = hn::Zero(tag);
    const auto white = hn::Set(tag, whiteLevel);
    const auto half = hn::Set(tag, 0.5f);
    const auto lower = [&](auto value, auto ceiling) { return hn::IfThenElse(hn::Lt(ceiling, value), ceiling, value); };
    const auto upper = [&](auto value, auto floor) { return hn::IfThenElse(hn::Lt(value, floor), floor, value); };
    std::uint64_t changed = 0u;
    for (std::uint64_t x = 0u; x < count; x += lanes)
    {
        const std::size_t partial = static_cast<std::size_t>((std::min)(lanes, count - x));
        const auto r = hn::LoadN(tag, red + x, partial);
        const auto g = hn::LoadN(tag, green + x, partial);
        const auto b = hn::LoadN(tag, blue + x, partial);
        const auto ceiling = averageNeutral ? hn::Mul(half, hn::Add(r, b)) : upper(r, b);
        const auto held = lower(g, ceiling);
        const auto storedRed = lower(upper(r, zero), white);
        const auto storedGreen = lower(upper(held, zero), white);
        const auto storedBlue = lower(upper(b, zero), white);
        // A changed sample is one that is not equal to its input, which for a not-a-number is
        // true, as the scalar row counts it; the lane not-equal is ordered and would say false.
        const auto inside = hn::FirstN(tag, partial);
        changed += static_cast<std::uint64_t>(hn::CountTrue(tag, hn::AndNot(hn::Eq(storedRed, r), inside)));
        changed += static_cast<std::uint64_t>(hn::CountTrue(tag, hn::AndNot(hn::Eq(storedGreen, g), inside)));
        changed += static_cast<std::uint64_t>(hn::CountTrue(tag, hn::AndNot(hn::Eq(storedBlue, b), inside)));
        hn::StoreN(storedRed, tag, outRed + x, partial);
        hn::StoreN(storedGreen, tag, outGreen + x, partial);
        hn::StoreN(storedBlue, tag, outBlue + x, partial);
    }
    return changed;
}

/// One run of interior bilateral outputs, every tap of a vector of adjacent pixels at a time.
///
/// The difference, its square and the spatial product are Float32, as in the scalar loop, and the
/// two sums are Float64 in tap order per lane, kept in registers for the whole window, each
/// weighted sample fused into its sum; the exponential is this target's polynomial over an
/// argument that is never positive, evaluated over a full Float32 vector whose two halves are then
/// widened. The tail goes through the same lane arithmetic on a half-width vector, so what one
/// output evaluates to does not depend on where in the run it sat.
///
/// @param centre the pixels.
/// @param count how many.
/// @param tapOffsets each neighbour's displacement from its pixel, in samples.
/// @param tapWeights each tap's spatial weight.
/// @param tapCount how many taps.
/// @param rangeScale the negated reciprocal of twice the range variance.
/// @param output where the filtered pixels go.
void bilateralRowFloat32(const float* centre, std::uint64_t count, const std::int64_t* tapOffsets,
                         const float* tapWeights, std::uint32_t tapCount, float rangeScale, float* output) noexcept
{
    const hn::ScalableTag<float> tag;
    const hn::ScalableTag<double> accumTag;
    const hn::Rebind<float, decltype(accumTag)> halfTag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const std::uint64_t halfLanes = static_cast<std::uint64_t>(hn::Lanes(accumTag));
    const auto scale = hn::Set(tag, rangeScale);
    std::uint64_t x = 0u;
    for (; x + lanes <= count; x += lanes)
    {
        const auto pixel = hn::LoadU(tag, centre + x);
        auto weightLower = hn::Zero(accumTag);
        auto weightUpper = hn::Zero(accumTag);
        auto sampleLower = hn::Zero(accumTag);
        auto sampleUpper = hn::Zero(accumTag);
        for (std::uint32_t tap = 0u; tap < tapCount; ++tap)
        {
            const auto neighbour = hn::LoadU(tag, centre + x + tapOffsets[tap]);
            const auto difference = hn::Sub(neighbour, pixel);
            const auto weight = hn::Mul(hn::Set(tag, tapWeights[tap]),
                                        nonPositiveExponentialOf(tag, hn::Mul(hn::Mul(difference, difference), scale)));
            const auto lower = hn::PromoteLowerTo(accumTag, weight);
            const auto upper = hn::PromoteUpperTo(accumTag, weight);
            weightLower = hn::Add(weightLower, lower);
            weightUpper = hn::Add(weightUpper, upper);
            sampleLower = hn::MulAdd(lower, hn::PromoteLowerTo(accumTag, neighbour), sampleLower);
            sampleUpper = hn::MulAdd(upper, hn::PromoteUpperTo(accumTag, neighbour), sampleUpper);
        }
        const auto lower = hn::DemoteTo(halfTag, hn::Div(sampleLower, weightLower));
        const auto upper = hn::DemoteTo(halfTag, hn::Div(sampleUpper, weightUpper));
        hn::StoreU(hn::Combine(tag, upper, lower), tag, output + x);
    }
    const auto halfScale = hn::Set(halfTag, rangeScale);
    for (; x < count; x += halfLanes)
    {
        const std::size_t partial = static_cast<std::size_t>((std::min)(halfLanes, count - x));
        const auto pixel = hn::LoadN(halfTag, centre + x, partial);
        auto weightSum = hn::Zero(accumTag);
        auto sampleSum = hn::Zero(accumTag);
        for (std::uint32_t tap = 0u; tap < tapCount; ++tap)
        {
            const auto neighbour = hn::LoadN(halfTag, centre + x + tapOffsets[tap], partial);
            const auto difference = hn::Sub(neighbour, pixel);
            const auto weight =
                hn::Mul(hn::Set(halfTag, tapWeights[tap]),
                        nonPositiveExponentialOf(halfTag, hn::Mul(hn::Mul(difference, difference), halfScale)));
            const auto widened = hn::PromoteTo(accumTag, weight);
            weightSum = hn::Add(weightSum, widened);
            sampleSum = hn::MulAdd(widened, hn::PromoteTo(accumTag, neighbour), sampleSum);
        }
        hn::StoreN(hn::DemoteTo(halfTag, hn::Div(sampleSum, weightSum)), halfTag, output + x, partial);
    }
}

} // namespace HWY_NAMESPACE
} // namespace simd_detail
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace simd_detail
{
namespace
{

// The compiled target set is the engine's ladder and nothing else. A build whose flags moved would
// otherwise silently compile a narrower baseline and keep calling it the floor, which is the one
// mistake in this file that produces correct output and meaningless measurements.
static_assert((HWY_TARGETS & ~(HWY_AVX2 | HWY_AVX3)) == 0,
              "this engine compiles vector kernels for the two rungs its instruction-set ladder names and no other "
              "target; a wider set means the disabled-target list stopped being applied");
static_assert((HWY_TARGETS & HWY_AVX2) != 0,
              "the packaged floor has no vector kernels in this build; the compiler flags for this translation unit "
              "produced a narrower baseline than the floor this product is built at");

/// One kernel per representation, built from the four bodies the four element widths need.
///
/// The mapping happens once, here, rather than at every call site: two representations of one width
/// share one body, so the object this file produces carries four copies of each kernel per
/// instruction set rather than eight.
///
/// @tparam FnT the kernel shape being tabulated.
/// @param one what a one-byte representation uses.
/// @param two what a two-byte representation uses.
/// @param four what a four-byte representation uses.
/// @param eight what an eight-byte representation uses.
/// @return the table, indexed by representation.
template <typename FnT>
[[nodiscard]] constexpr std::array<FnT, kSampleTypeCount> perWidth(FnT one, FnT two, FnT four, FnT eight) noexcept
{
    std::array<FnT, kSampleTypeCount> table{};
    for (std::size_t index = 0u; index < kSampleTypeCount; ++index)
    {
        switch (sampleTypeSizeBytes(static_cast<SampleType>(index)))
        {
        case 1u:
            table[index] = one;
            break;
        case 2u:
            table[index] = two;
            break;
        case 4u:
            table[index] = four;
            break;
        default:
            table[index] = eight;
            break;
        }
    }
    return table;
}

/// The demosaic estimate the packaged floor has for one method.
///
/// One small function per rung rather than one per rung and per method: the per-method units are
/// where the kernels live, and a table entry only has to name the rung it belongs to.
///
/// @param method which estimate.
/// @return the compiled run, or null when this build has none for that pairing.
[[nodiscard]] DemosaicRunFn avx2DemosaicRun(DemosaicMethod method) noexcept
{
    return simdDemosaicRun(InstructionSet::Avx2, method);
}

#if (HWY_TARGETS & HWY_AVX3) != 0
/// The demosaic estimate the Skylake-server subset has for one method.
///
/// @param method which estimate.
/// @return the compiled run, or null when this build has none for that pairing.
[[nodiscard]] DemosaicRunFn avx3DemosaicRun(DemosaicMethod method) noexcept
{
    return simdDemosaicRun(InstructionSet::Avx512Skylake, method);
}
#endif

/// Looks one kernel out of one instruction set's table.
///
/// A template over the table rather than one function written per table and per family: the tables
/// are compile-time constants, so naming one as a template argument produces exactly the small
/// function a dispatch table entry needs without ten transcriptions of the same three lines.
///
/// @tparam FnT the kernel shape.
/// @tparam TableV the instruction set's table for that shape.
/// @param sampleType what the samples are.
/// @return the kernel, or null when the value is outside the enumeration or that width has none.
template <typename FnT, const std::array<FnT, kSampleTypeCount>& TableV>
[[nodiscard]] FnT lookupOf(SampleType sampleType) noexcept
{
    if (!isSampleType(sampleType))
    {
        return nullptr;
    }
    return TableV[static_cast<std::size_t>(sampleType)];
}

constexpr auto kAvx2Copy =
    perWidth<CopyRunFn>(HWY_CHOOSE_AVX2(copyRunOf<std::uint8_t>), HWY_CHOOSE_AVX2(copyRunOf<std::uint16_t>),
                        HWY_CHOOSE_AVX2(copyRunOf<std::uint32_t>), HWY_CHOOSE_AVX2(copyRunOf<std::uint64_t>));
constexpr auto kAvx2Fill =
    perWidth<FillRunFn>(HWY_CHOOSE_AVX2(fillRunOf<std::uint8_t>), HWY_CHOOSE_AVX2(fillRunOf<std::uint16_t>),
                        HWY_CHOOSE_AVX2(fillRunOf<std::uint32_t>), HWY_CHOOSE_AVX2(fillRunOf<std::uint64_t>));
constexpr auto kAvx2Deinterleave = perWidth<DeinterleaveRunFn>(
    HWY_CHOOSE_AVX2(deinterleaveRunOf<std::uint8_t>), HWY_CHOOSE_AVX2(deinterleaveRunOf<std::uint16_t>),
    HWY_CHOOSE_AVX2(deinterleaveRunOf<std::uint32_t>), HWY_CHOOSE_AVX2(deinterleaveRunOf<std::uint64_t>));
constexpr auto kAvx2Interleave = perWidth<InterleaveRunFn>(
    HWY_CHOOSE_AVX2(interleaveRunOf<std::uint8_t>), HWY_CHOOSE_AVX2(interleaveRunOf<std::uint16_t>),
    HWY_CHOOSE_AVX2(interleaveRunOf<std::uint32_t>), HWY_CHOOSE_AVX2(interleaveRunOf<std::uint64_t>));

// One byte has one order, so the narrowest representations have no kernel here at all rather than
// one that does nothing.
constexpr auto kAvx2ByteSwap = perWidth<ByteSwapRunFn>(nullptr, HWY_CHOOSE_AVX2(byteSwapRunOf<std::uint16_t>),
                                                       HWY_CHOOSE_AVX2(byteSwapRunOf<std::uint32_t>),
                                                       HWY_CHOOSE_AVX2(byteSwapRunOf<std::uint64_t>));

/// The kernels compiled for the packaged floor.
constexpr CpuKernelTable kAvx2Table{
    .instructionSet = InstructionSet::Avx2,
    .reportInstructionSet = HWY_CHOOSE_AVX2(isaOfCompiledCode),
    .convertRun = &cpuConvertRun,
    .strideCopyRun = &cpuStrideCopyRun,
    .byteSwapRun = &lookupOf<ByteSwapRunFn, kAvx2ByteSwap>,
    .copyRun = &lookupOf<CopyRunFn, kAvx2Copy>,
    .fillRun = &lookupOf<FillRunFn, kAvx2Fill>,
    .deinterleaveRun = &lookupOf<DeinterleaveRunFn, kAvx2Deinterleave>,
    .interleaveRun = &lookupOf<InterleaveRunFn, kAvx2Interleave>,
    .pointwiseFloat32Run = HWY_CHOOSE_AVX2(pointwiseRunOf<float>),
    .pointwiseFloat64Run = HWY_CHOOSE_AVX2(pointwiseRunOf<double>),
    .pointwiseIntegerRun = HWY_CHOOSE_AVX2(pointwiseIntegerRunFor),
    .normalizedLowerCurveFloat32Run = HWY_CHOOSE_AVX2(normalizedLowerCurveFloat32RunOf),
    .rangeSelectionFloat32Run = HWY_CHOOSE_AVX2(rangeSelectionFloat32RunOf),
    .reductionFirstRun = HWY_CHOOSE_AVX2(reductionFirstFloat32),
    .reductionCenteredRun = HWY_CHOOSE_AVX2(reductionCenteredFloat32),
    .stencilRowRun = HWY_CHOOSE_AVX2(stencilRowFloat32),
    .stencilColumnRun = HWY_CHOOSE_AVX2(stencilColumnFloat32),
    .symmetricGaussianHorizontalRun = HWY_CHOOSE_AVX2(symmetricGaussianHorizontalFloat32),
    .symmetricGaussianVerticalRun = HWY_CHOOSE_AVX2(symmetricGaussianVerticalFloat32),
    .edgeTightenRowRun = HWY_CHOOSE_AVX2(edgeTightenRowFloat32),
    .previewGammaTableRun = HWY_CHOOSE_AVX2(previewGammaTableFloat32),
    .sampledCurveFloat32Run = HWY_CHOOSE_AVX2(sampledCurveFloat32Run),
    .curveTableEntriesRun = HWY_CHOOSE_AVX2(curveTableEntriesRun),
    .curveTableProbesRun = HWY_CHOOSE_AVX2(curveTableProbesRun),
    .orderCountFloat32Run = HWY_CHOOSE_AVX2(orderCountFloat32Run),
    .orderExtremesFloat32Run = HWY_CHOOSE_AVX2(orderExtremesFloat32Run),
    .orderGatherFloat32Run = HWY_CHOOSE_AVX2(orderGatherFloat32Run),
    .orderCountBracketSetFloat32Run = HWY_CHOOSE_AVX2(orderCountBracketSetFloat32Run),
    .orderGatherIntervalSetFloat32Run = HWY_CHOOSE_AVX2(orderGatherIntervalSetFloat32Run),
    .expFloat32Run = HWY_CHOOSE_AVX2(expRunFloat32),
    .logFloat32Run = HWY_CHOOSE_AVX2(logRunFloat32),
    .powFloat32Run = HWY_CHOOSE_AVX2(powRunFloat32),
    .bilateralRowRun = HWY_CHOOSE_AVX2(bilateralRowFloat32),
    .chromaticCeilingRun = HWY_CHOOSE_AVX2(chromaticCeilingFloat32),
    .maskIntersectRun = HWY_CHOOSE_AVX2(maskIntersectRun),
    .maskUnionRun = HWY_CHOOSE_AVX2(maskUnionRun),
    .maskInvertRun = HWY_CHOOSE_AVX2(maskInvertRun),
    .maskPopcountRun = HWY_CHOOSE_AVX2(maskPopcountRun),
    .demosaicRun = &avx2DemosaicRun};

#if (HWY_TARGETS & HWY_AVX3) != 0

constexpr auto kAvx3Copy =
    perWidth<CopyRunFn>(HWY_CHOOSE_AVX3(copyRunOf<std::uint8_t>), HWY_CHOOSE_AVX3(copyRunOf<std::uint16_t>),
                        HWY_CHOOSE_AVX3(copyRunOf<std::uint32_t>), HWY_CHOOSE_AVX3(copyRunOf<std::uint64_t>));
constexpr auto kAvx3Fill =
    perWidth<FillRunFn>(HWY_CHOOSE_AVX3(fillRunOf<std::uint8_t>), HWY_CHOOSE_AVX3(fillRunOf<std::uint16_t>),
                        HWY_CHOOSE_AVX3(fillRunOf<std::uint32_t>), HWY_CHOOSE_AVX3(fillRunOf<std::uint64_t>));
constexpr auto kAvx3Deinterleave = perWidth<DeinterleaveRunFn>(
    HWY_CHOOSE_AVX3(deinterleaveRunOf<std::uint8_t>), HWY_CHOOSE_AVX3(deinterleaveRunOf<std::uint16_t>),
    HWY_CHOOSE_AVX3(deinterleaveRunOf<std::uint32_t>), HWY_CHOOSE_AVX3(deinterleaveRunOf<std::uint64_t>));
constexpr auto kAvx3Interleave = perWidth<InterleaveRunFn>(
    HWY_CHOOSE_AVX3(interleaveRunOf<std::uint8_t>), HWY_CHOOSE_AVX3(interleaveRunOf<std::uint16_t>),
    HWY_CHOOSE_AVX3(interleaveRunOf<std::uint32_t>), HWY_CHOOSE_AVX3(interleaveRunOf<std::uint64_t>));
constexpr auto kAvx3ByteSwap = perWidth<ByteSwapRunFn>(nullptr, HWY_CHOOSE_AVX3(byteSwapRunOf<std::uint16_t>),
                                                       HWY_CHOOSE_AVX3(byteSwapRunOf<std::uint32_t>),
                                                       HWY_CHOOSE_AVX3(byteSwapRunOf<std::uint64_t>));

/// The kernels compiled for the Skylake-server AVX-512 subset, on the builds that have them.
constexpr CpuKernelTable kAvx3Table{
    .instructionSet = InstructionSet::Avx512Skylake,
    .reportInstructionSet = HWY_CHOOSE_AVX3(isaOfCompiledCode),
    .convertRun = &cpuConvertRun,
    .strideCopyRun = &cpuStrideCopyRun,
    .byteSwapRun = &lookupOf<ByteSwapRunFn, kAvx3ByteSwap>,
    .copyRun = &lookupOf<CopyRunFn, kAvx3Copy>,
    .fillRun = &lookupOf<FillRunFn, kAvx3Fill>,
    .deinterleaveRun = &lookupOf<DeinterleaveRunFn, kAvx3Deinterleave>,
    .interleaveRun = &lookupOf<InterleaveRunFn, kAvx3Interleave>,
    .pointwiseFloat32Run = HWY_CHOOSE_AVX3(pointwiseRunOf<float>),
    .pointwiseFloat64Run = HWY_CHOOSE_AVX3(pointwiseRunOf<double>),
    .pointwiseIntegerRun = HWY_CHOOSE_AVX3(pointwiseIntegerRunFor),
    .normalizedLowerCurveFloat32Run = HWY_CHOOSE_AVX3(normalizedLowerCurveFloat32RunOf),
    .rangeSelectionFloat32Run = HWY_CHOOSE_AVX3(rangeSelectionFloat32RunOf),
    .reductionFirstRun = HWY_CHOOSE_AVX3(reductionFirstFloat32),
    .reductionCenteredRun = HWY_CHOOSE_AVX3(reductionCenteredFloat32),
    .stencilRowRun = HWY_CHOOSE_AVX3(stencilRowFloat32),
    .stencilColumnRun = HWY_CHOOSE_AVX3(stencilColumnFloat32),
    .symmetricGaussianHorizontalRun = HWY_CHOOSE_AVX3(symmetricGaussianHorizontalFloat32),
    .symmetricGaussianVerticalRun = HWY_CHOOSE_AVX3(symmetricGaussianVerticalFloat32),
    .edgeTightenRowRun = HWY_CHOOSE_AVX3(edgeTightenRowFloat32),
    .previewGammaTableRun = HWY_CHOOSE_AVX3(previewGammaTableFloat32),
    .sampledCurveFloat32Run = HWY_CHOOSE_AVX3(sampledCurveFloat32Run),
    .curveTableEntriesRun = HWY_CHOOSE_AVX3(curveTableEntriesRun),
    .curveTableProbesRun = HWY_CHOOSE_AVX3(curveTableProbesRun),
    .orderCountFloat32Run = HWY_CHOOSE_AVX3(orderCountFloat32Run),
    .orderExtremesFloat32Run = HWY_CHOOSE_AVX3(orderExtremesFloat32Run),
    .orderGatherFloat32Run = HWY_CHOOSE_AVX3(orderGatherFloat32Run),
    .orderCountBracketSetFloat32Run = HWY_CHOOSE_AVX3(orderCountBracketSetFloat32Run),
    .orderGatherIntervalSetFloat32Run = HWY_CHOOSE_AVX3(orderGatherIntervalSetFloat32Run),
    .expFloat32Run = HWY_CHOOSE_AVX3(expRunFloat32),
    .logFloat32Run = HWY_CHOOSE_AVX3(logRunFloat32),
    .powFloat32Run = HWY_CHOOSE_AVX3(powRunFloat32),
    .bilateralRowRun = HWY_CHOOSE_AVX3(bilateralRowFloat32),
    .chromaticCeilingRun = HWY_CHOOSE_AVX3(chromaticCeilingFloat32),
    .maskIntersectRun = HWY_CHOOSE_AVX3(maskIntersectRun),
    .maskUnionRun = HWY_CHOOSE_AVX3(maskUnionRun),
    .maskInvertRun = HWY_CHOOSE_AVX3(maskInvertRun),
    .maskPopcountRun = HWY_CHOOSE_AVX3(maskPopcountRun),
    .demosaicRun = &avx3DemosaicRun};

#endif // (HWY_TARGETS & HWY_AVX3) != 0

} // namespace
} // namespace simd_detail

const CpuKernelTable* simdCpuKernelTable(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return &simd_detail::kAvx2Table;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return &simd_detail::kAvx3Table;
#else
        // The vector abstraction marks this target and everything above it broken on this compiler,
        // so this build genuinely contains no such kernel and says so rather than handing back the
        // floor's table under a name it does not have.
        return nullptr;
#endif
    case InstructionSet::Automatic:
    case InstructionSet::Scalar:
    case InstructionSet::Sse2:
    case InstructionSet::Sse42:
    case InstructionSet::Avx:
    case InstructionSet::Neon:
        // Listed rather than covered by a default label, so a rung added to the ladder fails this
        // switch on a compiler that reports an unhandled case instead of silently answering null.
        return nullptr;
    }
    return nullptr;
}

} // namespace an

#endif // HWY_ONCE
