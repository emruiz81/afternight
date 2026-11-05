// AfterNightEngine - the compiled ratio-corrected estimate, one per instruction set.
//
// The estimate is the port in `kernels/reference/DemosaicKernelsRcd.cpp`, derived from RawTherapee's RCD
// as librtprocess carries it (`demosaic/rcd.cc`), whose copyright line is
// `Copyright (c) 2017-2018 Luis Sanz Rodriguez (luis.sanz.rodriguez(at)gmail(dot)com) and Ingo Weyrich
// (heckflosse67@gmx.de)` - one line upstream, copyright and addresses together - and which is
// distributed under the GNU General Public License, version 3 or any later version; this file is part of
// that derived work and is distributed under the same licence. What is here is AfterNight's: the
// lane-wise spelling of the reference's passes in its order of evaluation. The provenance note is
// `docs/dev/DEMOSAIC_PROVENANCE.md`.
//
// RCD keeps data-dependent half-resolution passes scalar, but its two full-frame bandwidth passes
// are one numeric dialect and compile once per packaged instruction set here. The target body and
// the reference share every directional decision; only normalization, plane replication and final
// scaling are lane-wise, so conformance remains equality rather than a tolerance.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "backends/cpu/DemosaicRcdCpu.h"

#include "kernels/reference/DemosaicKernelVectorOps.h"

#include <algorithm>
#include <cstdint>

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/DemosaicRcdCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace demosaic_rcd_simd
{
namespace HWY_NAMESPACE
{

namespace hn = hwy::HWY_NAMESPACE;

/// Which rung of the ladder this compilation of the file belongs to.
///
/// @return the instruction set this body was compiled for.
[[nodiscard]] InstructionSet isaOfCompiledRcd() noexcept
{
#if HWY_TARGET == HWY_AVX3
    return InstructionSet::Avx512Skylake;
#else
    return InstructionSet::Avx2;
#endif
}

/// The reference's `std::max`: the second value where the first is less than it, the first otherwise.
///
/// The instruction alone answers the second value where the two compare equal or either is not a
/// number, so a negative zero it is handed against zero would survive where the reference writes zero.
///
/// @param first the value kept on a tie.
/// @param second the other value.
/// @return the larger, as the reference chooses it.
template <class V> HWY_INLINE V referenceMax(const V first, const V second)
{
    return hn::IfThenElse(hn::Lt(first, second), second, first);
}

/// The reference's `std::min`: the second value where it is less than the first, the first otherwise.
///
/// @param first the value kept on a tie.
/// @param second the other value.
/// @return the smaller, as the reference chooses it.
template <class V> HWY_INLINE V referenceMin(const V first, const V second)
{
    return hn::IfThenElse(hn::Lt(second, first), second, first);
}

/// The `DemosaicRcdVectorOps::initialize` pass: the padded tile divided by the white level and
/// clamped to the unit interval, into the mosaic plane and all three colour planes.
void initializeRcd(const DemosaicTile& tile, std::uint32_t padBeginX, std::uint32_t padBeginY, std::uint64_t rows,
                   std::uint64_t columns, std::uint64_t scratchStride, float whiteLevel, float* cfa, float* red,
                   float* green, float* blue) noexcept
{
    const hn::ScalableTag<float> lanes;
    const std::uint64_t width = hn::Lanes(lanes);
    const auto scale = hn::Set(lanes, whiteLevel);
    const auto zero = hn::Zero(lanes);
    const auto one = hn::Set(lanes, 1.0f);
    for (std::uint64_t row = 0u; row < rows; ++row)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(row);
        const float* const source = tile.source + static_cast<std::uint64_t>(y) * tile.sourceStride + padBeginX;
        const std::uint64_t rowBase = row * scratchStride;
        std::uint64_t column = 0u;
        for (; column + width <= columns; column += width)
        {
            const auto measured =
                referenceMax(zero, referenceMin(hn::Div(hn::LoadU(lanes, source + column), scale), one));
            hn::StoreU(measured, lanes, cfa + rowBase + column);
            hn::StoreU(measured, lanes, red + rowBase + column);
            hn::StoreU(measured, lanes, green + rowBase + column);
            hn::StoreU(measured, lanes, blue + rowBase + column);
        }
        for (; column < columns; ++column)
        {
            const float measured = (std::max)(0.0f, (std::min)(source[column] / whiteLevel, 1.0f));
            cfa[rowBase + column] = measured;
            red[rowBase + column] = measured;
            green[rowBase + column] = measured;
            blue[rowBase + column] = measured;
        }
    }
}

/// The `DemosaicRcdVectorOps::write` pass: the region inside the nine-pixel reach scaled back by the
/// white level, raised to zero where negative, into the tile's destination planes.
void writeRcd(const DemosaicTile& tile, std::uint32_t padBeginX, std::uint32_t padBeginY, std::uint64_t rows,
              std::uint64_t columns, std::uint64_t scratchStride, float whiteLevel, const float* red,
              const float* green, const float* blue) noexcept
{
    constexpr std::uint64_t kPad = 9u;
    const hn::ScalableTag<float> lanes;
    const std::uint64_t width = hn::Lanes(lanes);
    const auto scale = hn::Set(lanes, whiteLevel);
    const auto zero = hn::Zero(lanes);
    for (std::uint64_t row = kPad; row < rows - kPad; ++row)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(row);
        const std::uint64_t destinationRow = static_cast<std::uint64_t>(y) * tile.destinationStride;
        std::uint64_t column = kPad;
        for (; column + width <= columns - kPad; column += width)
        {
            const std::uint64_t index = row * scratchStride + column;
            const std::uint64_t destination = destinationRow + padBeginX + column;
            hn::StoreU(referenceMax(zero, hn::Mul(hn::LoadU(lanes, red + index), scale)), lanes,
                       tile.red + destination);
            hn::StoreU(referenceMax(zero, hn::Mul(hn::LoadU(lanes, green + index), scale)), lanes,
                       tile.green + destination);
            hn::StoreU(referenceMax(zero, hn::Mul(hn::LoadU(lanes, blue + index), scale)), lanes,
                       tile.blue + destination);
        }
        for (; column < columns - kPad; ++column)
        {
            const std::uint64_t index = row * scratchStride + column;
            const std::uint64_t destination = destinationRow + padBeginX + column;
            tile.red[destination] = (std::max)(0.0f, red[index] * whiteLevel);
            tile.green[destination] = (std::max)(0.0f, green[index] * whiteLevel);
            tile.blue[destination] = (std::max)(0.0f, blue[index] * whiteLevel);
        }
    }
}

/// Estimates one tile through the shared RCD authority, with the two passes above substituted.
void rcdRun(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
{
    const DemosaicRcdVectorOps ops{.initialize = &initializeRcd, .write = &writeRcd};
    demosaicRcdBody(tile, scratch, scratchFloats, &ops);
}

} // namespace HWY_NAMESPACE
} // namespace demosaic_rcd_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace demosaic_rcd_simd
{
namespace
{

constexpr DemosaicRunFn kRcdAvx2 = HWY_CHOOSE_AVX2(rcdRun);
constexpr InstructionSet (*kRcdAvx2Isa)() noexcept = HWY_CHOOSE_AVX2(isaOfCompiledRcd);

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr DemosaicRunFn kRcdAvx3 = HWY_CHOOSE_AVX3(rcdRun);
constexpr InstructionSet (*kRcdAvx3Isa)() noexcept = HWY_CHOOSE_AVX3(isaOfCompiledRcd);
#endif

} // namespace
} // namespace demosaic_rcd_simd

DemosaicRunFn simdRcdRun(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return demosaic_rcd_simd::kRcdAvx2;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return demosaic_rcd_simd::kRcdAvx3;
#else
        return nullptr;
#endif
    case InstructionSet::Automatic:
    case InstructionSet::Scalar:
    case InstructionSet::Sse2:
    case InstructionSet::Sse42:
    case InstructionSet::Avx:
    case InstructionSet::Neon:
        return nullptr;
    }
    return nullptr;
}

InstructionSet simdRcdIsa(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return demosaic_rcd_simd::kRcdAvx2Isa();
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return demosaic_rcd_simd::kRcdAvx3Isa();
#else
        return InstructionSet::Automatic;
#endif
    case InstructionSet::Automatic:
    case InstructionSet::Scalar:
    case InstructionSet::Sse2:
    case InstructionSet::Sse42:
    case InstructionSet::Avx:
    case InstructionSet::Neon:
        return InstructionSet::Automatic;
    }
    return InstructionSet::Automatic;
}

} // namespace an

#endif // HWY_ONCE
