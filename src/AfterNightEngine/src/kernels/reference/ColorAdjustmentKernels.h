// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the run shapes of the compiled colour adjustments, shared by their executor and the CPU
// dispatch tables.
//
// What `AlgorithmKernels.h` is for the rest of the compiled runs, this is for the colour adjustments: the parameters a
// row reads, the function-pointer type a dispatch table holds, and the accessor that reads the row out of the
// execution's immutable CPU table. A table holds function pointers, so no shape here names a public algorithm type;
// the one public enumeration a run needs is carried in a private spelling the algorithm maps onto it. The accessor
// answers null for the scalar table, which is the one `ComputeBackend::Reference` selects, and for a vector table
// that compiled no row; a null answer means the adjustment runs its own scalar authority.

#pragma once

#include <array>
#include <cstdint>

namespace an
{

class ExecutionContext;

/// How the red, green and blue a compiled LCh adjustment reads and writes relate to light: the private spelling of
/// the two transfer functions the row compiles, which the algorithm maps onto the public one.
enum class LchAdjustTransfer : std::uint8_t
{
    /// The samples are proportional to light; nothing is decoded or encoded.
    Linear,

    /// The IEC 61966-2-1 sRGB curve, decoded before the matrix and encoded after the inverse.
    Srgb
};

/// What a compiled LCh adjustment row applies: the colour conversion's constants, read once from its two plans, and
/// the adjustment itself.
///
/// A row decodes each pixel's transfer, takes it through the RGB-to-XYZ matrix to CIE L*, a* and b* under the white
/// and on to chroma and a hue in degrees, adds the lightness offset and holds the lightness to `[0, 100]`, scales the
/// chroma and holds it to `[0, 200]`, turns the hue on round, and comes back: a* and b*, XYZ, the XYZ-to-RGB matrix,
/// the transfer, each channel held to the unit interval - the scalar authority's steps in its order, in Float32 lanes
/// with the vector library's transcendentals. A pixel whose three samples are not all finite is left as the
/// destination holds it and not counted; the algorithm reads the count against the rule it runs under.
struct LchAdjustCoefficients
{
    /// The transfer the red, green and blue are encoded with.
    LchAdjustTransfer transfer = LchAdjustTransfer::Linear;

    /// Whether a pixel that is not finite is one the rule skips, rather than one that refuses the run.
    bool skipNonFinite = false;

    /// The linear RGB to XYZ matrix, row by row.
    std::array<double, 9u> toXyz{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};

    /// The white's tristimulus values, which the Lab stages divide by and multiply back.
    std::array<double, 3u> white{1.0, 1.0, 1.0};

    /// The XYZ to linear RGB matrix, row by row.
    std::array<double, 9u> fromXyz{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};

    /// What is added to the lightness, in CIE units.
    double lightnessOffset = 0.0;

    /// What the chroma is multiplied by.
    double chromaScale = 1.0;

    /// What is added to the hue, in degrees.
    double hueOffsetDegrees = 0.0;
};

/// A compiled LCh adjustment over `count` pixels of three planar Float32 rows, each source row read from its pointer
/// and each destination row written at the same index; a destination may be its source. Answers how many pixels it
/// adjusted: every one whose three samples are finite.
using LchAdjustFloat32RunFn = std::uint64_t (*)(const float* red, const float* green, const float* blue, float* redOut,
                                                float* greenOut, float* blueOut, std::uint64_t count,
                                                const LchAdjustCoefficients& coefficients) noexcept;

/// The compiled LCh adjustment row selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled row, or null when the selected table has none - which is the scalar table
///         always. A null answer means the adjustment runs its own scalar authority.
[[nodiscard]] LchAdjustFloat32RunFn lchAdjustFloat32RunFor(const ExecutionContext& execution) noexcept;

} // namespace an
