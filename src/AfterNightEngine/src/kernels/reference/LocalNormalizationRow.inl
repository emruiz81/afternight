// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the one row body of local normalization, included where it is compiled.
//
// This file is not a header. It defines the cell polynomial, the row mixing, the per-position
// transform and the contiguous row loop that every local-normalization apply runs. The algorithms
// that apply, plan or fuse a local normalization - the apply, the plan and the stack source path -
// include it inside an unnamed namespace as the scalar authority, and the local-normalization and
// stack-source CPU units include it inside the vector library's per-target namespace, so the same
// text is compiled once per instruction set. Writing the body once is what makes "the compiled row
// stores the scalar authority's bits" structural rather than a test's discovery: there is no
// second expression whose association order could drift.
//
// The includer supplies `LocalNormalizationRunParameters` and its companions from
// `kernels/reference/AlgorithmKernels.h`, and `<cmath>`, `<cstdint>` and `<limits>`.
//
// **Interpolation is evaluated a cell at a time.** Between two node columns the bicubic surface
// along a row is a cubic in the column fraction alone, so its four coefficients are formed once per
// cell from the row's four mixed node values and the inner loop is one Horner evaluation per grid
// per sample - no gather of sixteen nodes, no per-sample index arithmetic - which is what lets the
// target compiler vectorize it. The clamp that keeps a Catmull-Rom overshoot inside the nodes'
// range is a per-cell bound taken from the per-column bounds the row prepared. The bilinear form is
// the same loop with a linear polynomial and no clamp.

/// The Catmull-Rom cubic through four equally spaced values, as coefficients of the fraction.
///
/// @param p0 the value one node before the interval.
/// @param p1 the value at the interval's start.
/// @param p2 the value at the interval's end.
/// @param p3 the value one node after the interval.
/// @param a0 receives the cubic coefficient.
/// @param a1 receives the quadratic coefficient.
/// @param a2 receives the linear coefficient.
/// @param a3 receives the constant, which is `p1`.
inline void localNormalizationCubicCoefficients(double p0, double p1, double p2, double p3, double& a0, double& a1,
                                                double& a2, double& a3) noexcept
{
    a0 = -0.5 * p0 + 1.5 * p1 - 1.5 * p2 + 0.5 * p3;
    a1 = p0 - 2.5 * p1 + 2.0 * p2 - 0.5 * p3;
    a2 = -0.5 * p0 + 0.5 * p2;
    a3 = p1;
}

/// The Catmull-Rom cubic through four equally spaced values at a fraction of the middle interval.
///
/// @param p0 the value one node before the interval.
/// @param p1 the value at the interval's start.
/// @param p2 the value at the interval's end.
/// @param p3 the value one node after the interval.
/// @param t the fraction, within `[0, 1]`.
/// @return the interpolated value, unclamped.
[[nodiscard]] inline double localNormalizationCatmullRom(double p0, double p1, double p2, double p3, double t) noexcept
{
    double a0 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
    double a3 = 0.0;
    localNormalizationCubicCoefficients(p0, p1, p2, p3, a0, a1, a2, a3);
    return ((a0 * t + a1) * t + a2) * t + a3;
}

/// The cell a coordinate falls in along one axis, and its fraction within that cell.
///
/// Nodes are spread evenly from the first coordinate to the last, so the final coordinate falls
/// in a cell of its own numbered `gridSize` at fraction zero.
///
/// @param coordinate the pixel coordinate, already clamped to the axis.
/// @param extent how many pixels the axis has.
/// @param gridSize how many cells the axis has.
/// @param fraction receives the position within the cell.
/// @return the cell, at most `gridSize`.
[[nodiscard]] inline std::uint32_t localNormalizationCellOfCoordinate(double coordinate, std::uint32_t extent,
                                                                      std::uint32_t gridSize, double& fraction) noexcept
{
    const double scaled =
        extent <= 1u ? 0.0 : (coordinate / static_cast<double>(extent - 1u)) * static_cast<double>(gridSize);
    auto cell = static_cast<std::uint32_t>(std::floor(scaled));
    cell = cell > gridSize ? gridSize : cell;
    fraction = scaled - static_cast<double>(cell);
    return cell;
}

/// Mixes one grid plane's contributing node rows at a row fraction into per-column values and bounds.
///
/// A bicubic row blends four node rows with the Catmull-Rom cubic and records, per column, the
/// smallest and largest of the four; a bilinear row blends two and records no bounds.
///
/// @param plane the grid's node values for one channel, node-row-major with `nodesPerAxis` per row.
/// @param nodesPerAxis how many nodes one axis holds.
/// @param gridSize how many cells one axis holds.
/// @param rowCell the row's cell.
/// @param rowFraction the row's fraction within it.
/// @param bicubic whether four rows or two contribute.
/// @param mixed receives one mixed value per node column.
/// @param lower receives the smallest contributing node per column, bicubic only.
/// @param upper receives the largest contributing node per column, bicubic only.
inline void localNormalizationMixRows(const double* plane, std::uint32_t nodesPerAxis, std::uint32_t gridSize,
                                      std::uint32_t rowCell, double rowFraction, bool bicubic, double* mixed,
                                      double* lower, double* upper) noexcept
{
    const std::uint32_t j1 = rowCell;
    const std::uint32_t j2 = rowCell + 1u < gridSize ? rowCell + 1u : gridSize;
    const std::uint32_t j0 = rowCell > 0u ? rowCell - 1u : 0u;
    const std::uint32_t j3 = rowCell + 2u < gridSize ? rowCell + 2u : gridSize;
    const double* const row0 = plane + static_cast<std::uint64_t>(j0) * nodesPerAxis;
    const double* const row1 = plane + static_cast<std::uint64_t>(j1) * nodesPerAxis;
    const double* const row2 = plane + static_cast<std::uint64_t>(j2) * nodesPerAxis;
    const double* const row3 = plane + static_cast<std::uint64_t>(j3) * nodesPerAxis;
    for (std::uint32_t nx = 0u; nx < nodesPerAxis; ++nx)
    {
        const double p1 = row1[nx];
        const double p2 = row2[nx];
        if (!bicubic)
        {
            mixed[nx] = p1 + rowFraction * (p2 - p1);
            continue;
        }
        const double p0 = row0[nx];
        const double p3 = row3[nx];
        mixed[nx] = localNormalizationCatmullRom(p0, p1, p2, p3, rowFraction);
        double low = p0;
        double high = p0;
        low = p1 < low ? p1 : low;
        high = p1 > high ? p1 : high;
        low = p2 < low ? p2 : low;
        high = p2 > high ? p2 : high;
        low = p3 < low ? p3 : low;
        high = p3 > high ? p3 : high;
        lower[nx] = low;
        upper[nx] = high;
    }
}

/// One grid's interpolation along one cell of one row, ready to evaluate per sample.
struct LocalNormalizationCell
{
    /// The cubic coefficient, or zero for a bilinear cell.
    double a0 = 0.0;

    /// The quadratic coefficient, or zero for a bilinear cell.
    double a1 = 0.0;

    /// The linear coefficient.
    double a2 = 0.0;

    /// The constant.
    double a3 = 0.0;

    /// The smallest node value that took part, which bounds a bicubic evaluation below.
    double lower = 0.0;

    /// The largest node value that took part, which bounds a bicubic evaluation above.
    double upper = 0.0;
};

/// Forms one grid's cell from the row's mixed node values.
///
/// @param grid the row's mixed values and, for a bicubic row, their per-column bounds.
/// @param cell which cell, below the grid size or equal to it for the final column.
/// @param gridSize how many cells the grid has along the axis.
/// @param bicubic whether the row is bicubic.
/// @return the cell polynomial.
[[nodiscard]] inline LocalNormalizationCell localNormalizationCellOf(const LocalNormalizationRunGrid& grid,
                                                                     std::uint32_t cell, std::uint32_t gridSize,
                                                                     bool bicubic) noexcept
{
    LocalNormalizationCell out{};
    const std::uint32_t i1 = cell;
    const std::uint32_t i2 = cell + 1u < gridSize ? cell + 1u : gridSize;
    if (!bicubic)
    {
        out.a2 = grid.mixed[i2] - grid.mixed[i1];
        out.a3 = grid.mixed[i1];
        return out;
    }
    const std::uint32_t i0 = cell > 0u ? cell - 1u : 0u;
    const std::uint32_t i3 = cell + 2u < gridSize ? cell + 2u : gridSize;
    localNormalizationCubicCoefficients(grid.mixed[i0], grid.mixed[i1], grid.mixed[i2], grid.mixed[i3], out.a0, out.a1,
                                        out.a2, out.a3);
    double lower = grid.lower[i0];
    double upper = grid.upper[i0];
    lower = grid.lower[i1] < lower ? grid.lower[i1] : lower;
    upper = grid.upper[i1] > upper ? grid.upper[i1] : upper;
    lower = grid.lower[i2] < lower ? grid.lower[i2] : lower;
    upper = grid.upper[i2] > upper ? grid.upper[i2] : upper;
    lower = grid.lower[i3] < lower ? grid.lower[i3] : lower;
    upper = grid.upper[i3] > upper ? grid.upper[i3] : upper;
    out.lower = lower;
    out.upper = upper;
    return out;
}

/// Evaluates one cell at a fraction.
///
/// A bilinear cell is `p1 + t * (p2 - p1)`, which is the same spelling the row mixing uses; the
/// two agree to the last bit only when written the same way everywhere, which is why the point
/// evaluator and this row share this function rather than a formula.
///
/// @param cell the cell polynomial.
/// @param t the fraction, within `[0, 1]`.
/// @param bicubic whether the cell is bicubic and clamped.
/// @return the interpolated value.
[[nodiscard]] inline double localNormalizationEvaluateCell(const LocalNormalizationCell& cell, double t,
                                                           bool bicubic) noexcept
{
    if (!bicubic)
    {
        return cell.a3 + t * cell.a2;
    }
    double value = ((cell.a0 * t + cell.a1) * t + cell.a2) * t + cell.a3;
    value = value < cell.lower ? cell.lower : value;
    value = value > cell.upper ? cell.upper : value;
    return value;
}

/// Transforms one admitted sample.
///
/// `scale * (sample - target) + reference` in Float64, narrowed once. Under the zero rule a result
/// that narrows to zero is moved to the smallest normal magnitude with its own sign and counted.
///
/// @tparam SampleT the resident floating representation.
/// @param value the admitted sample.
/// @param target the frame's own interpolated background here.
/// @param reference the reference's interpolated background here.
/// @param scale the factor here.
/// @param zeroRule whether an exact zero would become no-data downstream.
/// @param collisions incremented when the zero rule moved the result.
/// @return the sample to store.
template <typename SampleT>
[[nodiscard]] inline SampleT localNormalizationTransform(SampleT value, double target, double reference, double scale,
                                                         bool zeroRule, std::uint64_t& collisions) noexcept
{
    const double result = scale * (static_cast<double>(value) - target) + reference;
    SampleT written = static_cast<SampleT>(result);
    if (zeroRule && written == static_cast<SampleT>(0))
    {
        written = std::copysign((std::numeric_limits<SampleT>::min)(), written);
        ++collisions;
    }
    return written;
}

/// Writes one row of local normalization under a value rule.
///
/// The row is walked cell by cell; within a cell the three interpolations are polynomials of the
/// column fraction the parameters prepared, and each admitted sample is transformed once.
///
/// @tparam SampleT the resident floating representation.
/// @param source where the row's samples start.
/// @param destination where they go, which may be the source exactly.
/// @param parameters the row's fractions, cell starts, mixed node values and rule.
/// @return how many samples were transformed, preserved and moved off the zero marker.
template <typename SampleT>
[[nodiscard]] LocalNormalizationRunOutcome
localNormalizationRunOf(const SampleT* source, SampleT* destination,
                        const LocalNormalizationRunParameters& parameters) noexcept
{
    LocalNormalizationRunOutcome outcome{};
    const bool zeroRule = parameters.rule == ValidityRule::ZeroIsNoData;
    const bool finiteRule = parameters.rule == ValidityRule::FiniteOnly;
    const bool bicubic = parameters.bicubic;
    const bool scaled = parameters.scale.mixed != nullptr;
    const std::uint64_t stride = parameters.sampleStride;
    std::uint64_t transformed = 0u;
    std::uint64_t preserved = 0u;
    std::uint64_t collisions = 0u;
    for (std::uint32_t cell = 0u; cell <= parameters.gridSize; ++cell)
    {
        const std::uint32_t first = parameters.cellStarts[cell];
        const std::uint32_t last = parameters.cellStarts[cell + 1u];
        if (first >= last)
        {
            continue;
        }
        const LocalNormalizationCell reference =
            localNormalizationCellOf(parameters.reference, cell, parameters.gridSize, bicubic);
        const LocalNormalizationCell target =
            localNormalizationCellOf(parameters.target, cell, parameters.gridSize, bicubic);
        const LocalNormalizationCell scaleCell =
            scaled ? localNormalizationCellOf(parameters.scale, cell, parameters.gridSize, bicubic)
                   : LocalNormalizationCell{.a3 = parameters.uniformScale};
        for (std::uint32_t x = first; x < last; ++x)
        {
            const SampleT value = source[static_cast<std::uint64_t>(x) * stride];
            const bool admitted = finiteRule ? (value >= -(std::numeric_limits<SampleT>::max)() &&
                                                value <= (std::numeric_limits<SampleT>::max)())
                                             : (!zeroRule || value != static_cast<SampleT>(0));
            if (!admitted)
            {
                destination[static_cast<std::uint64_t>(x) * stride] = value;
                ++preserved;
                continue;
            }
            const double t = parameters.fractions[x];
            const double referenceLevel = localNormalizationEvaluateCell(reference, t, bicubic);
            const double targetLevel = localNormalizationEvaluateCell(target, t, bicubic);
            const double scale = scaled ? localNormalizationEvaluateCell(scaleCell, t, bicubic) : scaleCell.a3;
            destination[static_cast<std::uint64_t>(x) * stride] =
                localNormalizationTransform<SampleT>(value, targetLevel, referenceLevel, scale, zeroRule, collisions);
            ++transformed;
        }
    }
    outcome.samplesTransformed = transformed;
    outcome.samplesPreserved = preserved;
    outcome.noDataCollisions = collisions;
    return outcome;
}

/// The per-row working room of one worker: three grids' mixed columns and bounds.
///
/// Nine arrays of one value per node column, laid over one block the worker owns, so a row is
/// prepared without an allocation and two workers never share a column.
struct LocalNormalizationRowScratch
{
    /// The reference grid's mixed node row.
    double* referenceMixed = nullptr;

    /// The smallest reference node that took part, per column.
    double* referenceLower = nullptr;

    /// The largest reference node that took part, per column.
    double* referenceUpper = nullptr;

    /// The target grid's mixed node row.
    double* targetMixed = nullptr;

    /// The smallest target node that took part, per column.
    double* targetLower = nullptr;

    /// The largest target node that took part, per column.
    double* targetUpper = nullptr;

    /// The scale grid's mixed node row.
    double* scaleMixed = nullptr;

    /// The smallest scale node that took part, per column.
    double* scaleLower = nullptr;

    /// The largest scale node that took part, per column.
    double* scaleUpper = nullptr;
};

/// How many doubles one worker's row scratch holds for a grid with `nodes` columns.
///
/// @param nodes how many nodes one axis holds.
/// @return the count, nine per node column.
[[nodiscard]] inline std::uint64_t localNormalizationRowScratchDoubles(std::uint32_t nodes) noexcept
{
    return 9ull * nodes;
}

/// Lays one worker's row scratch over its block.
///
/// @param block at least `localNormalizationRowScratchDoubles(nodes)` doubles the worker owns.
/// @param nodes how many nodes one axis holds.
/// @return the nine arrays, each `nodes` long.
[[nodiscard]] inline LocalNormalizationRowScratch localNormalizationRowScratchOver(double* block,
                                                                                   std::uint32_t nodes) noexcept
{
    LocalNormalizationRowScratch scratch{};
    scratch.referenceMixed = block;
    scratch.referenceLower = block + nodes;
    scratch.referenceUpper = block + 2u * nodes;
    scratch.targetMixed = block + 3u * nodes;
    scratch.targetLower = block + 4u * nodes;
    scratch.targetUpper = block + 5u * nodes;
    scratch.scaleMixed = block + 6u * nodes;
    scratch.scaleLower = block + 7u * nodes;
    scratch.scaleUpper = block + 8u * nodes;
    return scratch;
}

/// Every column's fraction within its cell, and the first column of every cell.
///
/// A cell no column falls in starts where the next one does, so its range is empty and the row
/// loop skips it.
///
/// @param width how many pixels one row holds.
/// @param gridSize how many cells one axis holds.
/// @param fractions receives one fraction per column.
/// @param cellStarts receives `gridSize + 2` entries: the first column of each of the
///        `gridSize + 1` cells, then the width.
inline void localNormalizationPrepareColumns(std::uint32_t width, std::uint32_t gridSize, double* fractions,
                                             std::uint32_t* cellStarts) noexcept
{
    for (std::uint32_t cell = 0u; cell <= gridSize + 1u; ++cell)
    {
        cellStarts[cell] = width;
    }
    std::uint32_t previousCell = gridSize + 1u;
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        double fraction = 0.0;
        const std::uint32_t cell =
            localNormalizationCellOfCoordinate(static_cast<double>(x), width, gridSize, fraction);
        fractions[x] = fraction;
        if (cell != previousCell)
        {
            cellStarts[cell] = x;
            previousCell = cell;
        }
    }
    for (std::uint32_t cell = gridSize + 1u; cell > 0u; --cell)
    {
        if (cellStarts[cell - 1u] > cellStarts[cell])
        {
            cellStarts[cell - 1u] = cellStarts[cell];
        }
    }
}

/// Mixes every grid of one channel for one row into the worker's scratch and names the result.
///
/// @param referencePlane the reference grid's node values for the channel, node-row-major.
/// @param targetPlane the frame's own grid's node values for the channel.
/// @param scalePlane the scale grid's node values for the channel, or null to apply
///        `uniformScale` everywhere.
/// @param nodesPerAxis how many nodes one axis holds.
/// @param gridSize how many cells one axis holds.
/// @param height how many rows the frame holds.
/// @param y the row.
/// @param bicubic whether four node rows contribute or two.
/// @param uniformScale the factor applied when `scalePlane` is null.
/// @param fractions every column's fraction, from `localNormalizationPrepareColumns`.
/// @param cellStarts every cell's first column, from `localNormalizationPrepareColumns`.
/// @param sampleStride one for a planar row and the channel count for an interleaved one.
/// @param rule the value rule the row is transformed under.
/// @param scratch the worker's row scratch, which the result points into.
/// @return the row's parameters, valid while `scratch` is.
[[nodiscard]] inline LocalNormalizationRunParameters
localNormalizationPrepareRow(const double* referencePlane, const double* targetPlane, const double* scalePlane,
                             std::uint32_t nodesPerAxis, std::uint32_t gridSize, std::uint32_t height, std::uint32_t y,
                             bool bicubic, double uniformScale, const double* fractions,
                             const std::uint32_t* cellStarts, std::uint32_t sampleStride, ValidityRule rule,
                             const LocalNormalizationRowScratch& scratch) noexcept
{
    double rowFraction = 0.0;
    const std::uint32_t rowCell =
        localNormalizationCellOfCoordinate(static_cast<double>(y), height, gridSize, rowFraction);
    localNormalizationMixRows(referencePlane, nodesPerAxis, gridSize, rowCell, rowFraction, bicubic,
                              scratch.referenceMixed, scratch.referenceLower, scratch.referenceUpper);
    localNormalizationMixRows(targetPlane, nodesPerAxis, gridSize, rowCell, rowFraction, bicubic, scratch.targetMixed,
                              scratch.targetLower, scratch.targetUpper);
    LocalNormalizationRunParameters parameters{};
    parameters.fractions = fractions;
    parameters.cellStarts = cellStarts;
    parameters.gridSize = gridSize;
    parameters.sampleStride = sampleStride;
    parameters.reference = LocalNormalizationRunGrid{
        .mixed = scratch.referenceMixed, .lower = scratch.referenceLower, .upper = scratch.referenceUpper};
    parameters.target = LocalNormalizationRunGrid{
        .mixed = scratch.targetMixed, .lower = scratch.targetLower, .upper = scratch.targetUpper};
    if (scalePlane != nullptr)
    {
        localNormalizationMixRows(scalePlane, nodesPerAxis, gridSize, rowCell, rowFraction, bicubic, scratch.scaleMixed,
                                  scratch.scaleLower, scratch.scaleUpper);
        parameters.scale = LocalNormalizationRunGrid{
            .mixed = scratch.scaleMixed, .lower = scratch.scaleLower, .upper = scratch.scaleUpper};
    }
    parameters.uniformScale = uniformScale;
    parameters.rule = rule;
    parameters.bicubic = bicubic;
    return parameters;
}
