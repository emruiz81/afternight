// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the run shapes every demosaic implementation is reached through.
//
// One tile, three destination planes, one worker-private scratch span, and no return value. That
// shape is what lets the scalar reference and a compiled vector kernel be the same function to the
// executor above them, which is the whole mechanism behind the conformance sweep: the differential
// test calls both through this pointer with the same tile and compares what they wrote.
//
// **A kernel reads the source plane where it lies.** There is no copy of the tile into scratch
// first, because the executor never hands a kernel a tile whose halo would fall outside the
// picture: the outer band of the frame belongs to the border kernel, and the tiles cover only what
// is left. So a kernel has no bounds test and no border rule of its own, and a tile at the edge of
// the interior is the same code path as one in the middle.
//
// **The coordinates are the picture's.** A tile carries absolute positions rather than a rebased
// pointer, because which filter covers a pixel is a function of where the pixel is, and a rebased
// tile would have to carry the phase separately and could carry the wrong one.
//
// **Scratch is sized by the method and the tile, never allocated.** `demosaicTileScratchFloats`
// says how many floats one tile of one method needs; the executor reserves that many per worker
// once, from the engine context, and hands each call its own span. A kernel that writes past it is
// a defect, not a budget refusal, which is why the span carries its length.

#pragma once

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Image/CfaPattern.h>

#include <cstdint>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// One region of one picture handed to a demosaic kernel.
///
/// The planes are the picture's, not the tile's: `source[y * sourceStride + x]` is the mosaic
/// sample at picture coordinates, and the three destination planes are indexed the same way. A
/// kernel writes exactly the half-open region `[xBegin, xEnd) x [yBegin, yEnd)` and may read
/// `reach` pixels beyond it in every direction, which the executor guarantees is inside the
/// picture.
struct DemosaicTile
{
    /// Which filter covers each pixel, with the picture's phase. Never null.
    const CfaDescriptor* cfa = nullptr;

    /// First sample of the mosaic plane. Never null.
    const float* source = nullptr;

    /// Floats between one row of the mosaic and the next.
    std::uint64_t sourceStride = 0u;

    /// First sample of the red destination plane. Never null.
    float* red = nullptr;

    /// First sample of the green destination plane. Never null.
    float* green = nullptr;

    /// First sample of the blue destination plane. Never null.
    float* blue = nullptr;

    /// Floats between one row of a destination plane and the next.
    std::uint64_t destinationStride = 0u;

    /// How many pixels the picture holds across.
    std::uint32_t width = 0u;

    /// How many rows the picture holds.
    std::uint32_t height = 0u;

    /// First picture column this call writes.
    std::uint32_t xBegin = 0u;

    /// One past the last picture column this call writes.
    std::uint32_t xEnd = 0u;

    /// First picture row this call writes.
    std::uint32_t yBegin = 0u;

    /// One past the last picture row this call writes.
    std::uint32_t yEnd = 0u;

    /// How far beyond the region the kernel may read, in pixels.
    std::uint32_t reach = 0u;

    /// The sample value a fully exposed photosite reads as.
    float whiteLevel = 1.0f;

    /// The sample value at which a sample stops being a measurement.
    float clipPoint = 1.0f;

    /// Whether a tabulated method judges its directions in a perceptual space.
    bool useCieLab = false;
};

/// What one demosaic implementation is called as.
///
/// @param tile the region to write and the picture to read it from.
/// @param scratch worker-private floats, at least `scratchFloats` of them, whose contents on entry
///        are whatever the previous tile left.
/// @param scratchFloats how many floats the span holds. A run given fewer than
///        `demosaicTileScratchFloats` reports for its method and region returns without writing
///        anything, because it has no way to report the shortfall.
using DemosaicRunFn = void (*)(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept;

/// How many floats one tile of one method needs privately.
///
/// Derived from the method and the padded tile rather than measured, so the executor reserves the
/// exact amount once per worker before any tile runs.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param method the estimate.
/// @param regionWidth how many columns one tile writes.
/// @param regionHeight how many rows one tile writes.
/// @return the float count, which is zero for a method that needs no scratch and saturates at
///         `UINT64_MAX` for a region no tile of this engine can be.
[[nodiscard]] std::uint64_t demosaicTileScratchFloats(DemosaicMethod method, std::uint32_t regionWidth,
                                                      std::uint32_t regionHeight) noexcept;

/// How wide and tall one tile of this method should be.
///
/// Chosen per method from what its scratch costs per pixel, so a method that holds forty-one
/// working planes per pixel gets a smaller tile than one that holds none. The ratio-corrected
/// estimate is the exception: its 176-pixel square is the adopted dialect's own tile, whose seams
/// are part of its answer, and its body accepts no larger region. Both numbers are the region a
/// tile writes, not counting its halo.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param method the estimate.
/// @param width set to the tile width in pixels.
/// @param height set to the tile height in pixels.
void demosaicTileShape(DemosaicMethod method, std::uint32_t& width, std::uint32_t& height) noexcept;

/// The authoritative scalar implementation of one method.
///
/// The conformance oracle, kept whether or not a vector kernel exists for the method. It is the
/// implementation `ComputeBackend::Reference` runs.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param method the estimate.
/// @return the run, or null for a value outside the enumeration.
[[nodiscard]] DemosaicRunFn referenceDemosaicRun(DemosaicMethod method) noexcept;

/// The implementation one execution's immutable CPU table selects for one method.
///
/// The reference backend reports the scalar run above. A CPU backend reports its compiled table
/// entry when one exists, falls back to the reference run when policy permits, and reports null when
/// the caller required a compiled entry the table does not contain.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @param method the estimate.
/// @return the selected run, or null for a value outside the enumeration or an unavailable required
///         CPU entry.
[[nodiscard]] DemosaicRunFn demosaicRunFor(const ExecutionContext& execution, DemosaicMethod method) noexcept;

/// Fills the outer band of a picture, where no method has a neighbourhood to read.
///
/// One rule for every method and both shapes of mosaic: the pixel keeps its own measurement for the
/// filter that covered it, and the other two colours are the weighted average of the like-filtered
/// samples of its three-by-three neighbourhood, clipped to the picture. A named mosaic weighs its
/// neighbours equally and a tabulated one weighs the four edge-adjacent neighbours twice the
/// corners, which is what each shape's arithmetic wants; a colour with no site in the neighbourhood
/// at all - which happens at the four corners of a tabulated mosaic and in a picture one or two
/// pixels wide - falls back to the pixel's own measurement rather than dividing by zero.
///
/// It writes exactly the region the tile names and reads only inside the picture, whatever that
/// region is; the executor hands it the rows and row ends that make up the band `reach` pixels wide
/// around the picture, and never a pixel of the interior.
///
/// Thread-safe with respect to its inputs, and safe to call concurrently for disjoint regions.
/// Allocation-free.
///
/// @param tile the region to write, whose region bounds name part of the band rather than an
///        interior tile.
void demosaicBorderRun(const DemosaicTile& tile) noexcept;

/// The reference bilinear estimate.
///
/// @param tile the region to write.
/// @param scratch unused.
/// @param scratchFloats unused.
void demosaicBilinearReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept;

/// The reference gradient-weighted estimate.
///
/// @param tile the region to write.
/// @param scratch the tile working planes.
/// @param scratchFloats how many floats the span holds.
void demosaicFastReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept;

/// The reference ratio-corrected estimate.
///
/// @param tile the region to write.
/// @param scratch the tile working planes.
/// @param scratchFloats how many floats the span holds.
void demosaicRcdReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept;

/// The reference aliasing-minimised estimate.
///
/// @param tile the region to write.
/// @param scratch the tile working planes.
/// @param scratchFloats how many floats the span holds.
void demosaicAmazeReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept;

/// The reference three-by-three weighted average over a tabulated mosaic.
///
/// @param tile the region to write.
/// @param scratch unused.
/// @param scratchFloats unused.
void demosaicXTransFastReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept;

/// The reference Markesteijn estimate over a tabulated mosaic, one pass.
///
/// @param tile the region to write.
/// @param scratch the tile working planes.
/// @param scratchFloats how many floats the span holds.
void demosaicMarkesteijnOneReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept;

/// The reference Markesteijn estimate over a tabulated mosaic, three passes.
///
/// @param tile the region to write.
/// @param scratch the tile working planes.
/// @param scratchFloats how many floats the span holds.
void demosaicMarkesteijnThreeReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept;

/// The reference linear minimum mean-square-error estimate.
///
/// @param tile the region to write.
/// @param scratch the tile working planes.
/// @param scratchFloats how many floats the span holds.
void demosaicLmmseReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept;

/// The reference variable-number-of-gradients estimate.
///
/// @param tile the region to write.
/// @param scratch the four-colour interpolation and final-green working planes.
/// @param scratchFloats how many floats the span holds.
void demosaicVng4Reference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept;

/// The reference luminance-only adaptive homogeneity-directed estimate.
///
/// @param tile the region to write.
/// @param scratch the horizontal and vertical candidate, lightness and homogeneity planes.
/// @param scratchFloats how many floats the span holds.
void demosaicLuminanceAhdReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept;

/// The shared body of both Markesteijn passes.
///
/// @param tile the region to write.
/// @param scratch the tile working planes.
/// @param scratchFloats how many floats the span holds.
/// @param passes one or three.
void demosaicMarkesteijnBody(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats,
                             std::uint32_t passes) noexcept;

} // namespace an
