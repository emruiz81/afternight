// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the guided dual-domain shrinkage, written from the published DA3D method.
//
// The method is the published one (Pierazzo and Facciolo, "DA3D: Data Adaptive Dual Domain Denoising",
// IPOL), written from the publication rather than adapted from an implementation of it: no line of an
// implementation is here. It finds the pixel with the least aggregation weight, fits and subtracts a
// plane so the patch around it is flat, builds a bilateral weight from the guide, attenuates the noisy
// patch's spectrum by how much power the guide has at each frequency against the noise, and
// aggregates - written against this engine's own structures.
// The provenance note is `docs/dev/PATCH_DENOISE_PROVENANCE.md`.
//
// Three of those structures are worth naming:
//
// **The coverage pyramid halves exactly.** Every level is a power of two and halves until it reaches
// one, so the descent is total and the top level is a single cell holding the global minimum. A level
// whose extent were rounded up could keep the extent of the one above it, and a descent through it
// would index one row past its end on a map far wider than it is tall.
//
// **The frame is planar and stays planar.** A channel-major buffer read as interleaved would make
// every "pixel" of a three-channel frame three samples from three different rows of one plane. Here
// the frame is the frame.
//
// **The transform is the engine's FFTW adapter.** One forward plan and one inverse plan serve every
// tile of the operation through the new-array interface, planning is serialised behind the
// adapter's own lock, and the estimate-mode planner does not touch the array it was measured over.
//
// The method's own constants - the transform-weight floor, the shrinkage exponent, the singularity
// threshold and the two relaxations of the regression weight - are kept as published. The
// exponentials - both bilateral weights and the frequency attenuation - go through the CPU
// dispatch table's Float32 exponential, held to five units in the last place, or through the scalar
// reference when the table has no compiled run. Tiles are walked by index over a grid derived from the
// extent, and each tile's patch sequence is a function of its samples alone, so the answer does not
// depend on the thread count; it can differ in the last bits between a compiled and a reference run.

#include "algorithms/noisereduction/PatchDenoiseInternal.h"

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Interop/FftwAdapter.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "interop/fftw/FftwRealTransform.h"
#include "kernels/reference/AlgorithmKernels.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace an::patch_detail
{
namespace
{

/// The most levels a coverage pyramid can have.
///
/// A level halves the extent, and the extent starts at a power of two at or above the largest tile
/// dimension the engine's geometry limits admit, so thirty-two levels is past any frame this engine
/// will see and the array below is a fixed size rather than an allocation.
constexpr std::uint32_t kMaximumCoverageLevels = 32u;

/// What the shrinkage says it is doing, once per tile it finishes.
constexpr const char* kShrinkageStage = "guided shrinkage: tiles";

/// The determinant magnitude below which the regression system is treated as singular, so the fitted
/// plane is flat.
constexpr float kRegressionEpsilon = 1.0e-4f;

/// How much total bilateral weight a patch must carry before its spectrum is worth shrinking.
///
/// Below this the weight is concentrated on so few samples that the transform describes the window
/// rather than the image, and the published method falls back to accepting the guide over the
/// patch. It is the method's own number.
constexpr float kMinimumTransformWeight = 10.0f;

/// What multiplies the noise power in the frequency attenuation.
///
/// The published shrinkage is `exp(-0.8 * sigma_f^2 / |G(f)|^2)`, where `sigma_f^2` is the noise
/// power the weighted patch carries - the noise variance times the sum of the squared weights - so a
/// frequency the guide has much power at passes and one it has none at is removed. The zero
/// frequency always passes.
constexpr float kShrinkageExponent = 0.8f;

/// The range weight of the regression fit is looser than the shrinkage's by this factor.
constexpr float kRegressionRangeRelaxation = 10.0f;

/// The spatial weight of the regression fit is looser than the shrinkage's by this factor.
constexpr float kRegressionSpatialRelaxation = 2.0f;

/// Rounds a byte count up to the alignment every part of a scratch block starts at.
///
/// @param bytes what a part needs.
/// @return what it occupies.
[[nodiscard]] constexpr std::uint64_t alignedBytes(std::uint64_t bytes) noexcept
{
    return (bytes + kDefaultBufferAlignmentBytes - 1u) / kDefaultBufferAlignmentBytes * kDefaultBufferAlignmentBytes;
}

/// The smallest power of two at or above a value.
///
/// @param value the value, at least one.
/// @return the power of two.
[[nodiscard]] constexpr std::uint32_t roundUpToPowerOfTwo(std::uint32_t value) noexcept
{
    std::uint32_t rounded = 1u;
    while (rounded < value)
    {
        rounded <<= 1u;
    }
    return rounded;
}

/// A minimum pyramid over the aggregation weight of every pixel a patch can be centred on.
///
/// Level zero holds the weight itself; every level above holds the smallest weight of the two-by-two
/// block below it, so the least-covered pixel is found by descending from the single cell at the top
/// rather than by scanning. At every level a tie goes to the child visited first - the upper row,
/// then the left column - so ties resolve in the pyramid's quadrant order rather than row-major
/// order, and the sequence of patches is a function of the image alone.
class CoveragePyramid
{
public:
    /// Builds a pyramid over one block of memory.
    ///
    /// @param block the bytes, at least `bytesFor(rows, columns)`.
    /// @param rows how many pixels a patch can be centred on, down.
    /// @param columns how many across.
    CoveragePyramid(std::uint8_t* block, std::uint32_t rows, std::uint32_t columns) noexcept
        : m_rows(rows), m_columns(columns)
    {
        std::uint32_t levelRows = roundUpToPowerOfTwo(rows);
        std::uint32_t levelColumns = roundUpToPowerOfTwo(columns);
        std::uint8_t* cursor = block;
        m_levels = 0u;
        while (true)
        {
            m_levelRows[m_levels] = levelRows;
            m_levelColumns[m_levels] = levelColumns;
            m_level[m_levels] = reinterpret_cast<float*>(cursor);
            cursor += alignedBytes(static_cast<std::uint64_t>(levelRows) * levelColumns * sizeof(float));
            ++m_levels;
            if (levelRows == 1u && levelColumns == 1u)
            {
                break;
            }
            levelRows = levelRows > 1u ? levelRows / 2u : 1u;
            levelColumns = levelColumns > 1u ? levelColumns / 2u : 1u;
        }

        // Everything outside the addressable rectangle is infinitely covered, so the descent never
        // chooses it and the rounding up to a power of two costs correctness nothing.
        for (std::uint32_t row = 0u; row < m_levelRows[0]; ++row)
        {
            float* const target = m_level[0] + static_cast<std::uint64_t>(row) * m_levelColumns[0];
            for (std::uint32_t column = 0u; column < m_levelColumns[0]; ++column)
            {
                target[column] = (row < rows && column < columns) ? 0.0f : std::numeric_limits<float>::infinity();
            }
        }
        for (std::uint32_t level = 1u; level < m_levels; ++level)
        {
            rebuildLevel(level, 0u, m_levelRows[level] - 1u, 0u, m_levelColumns[level] - 1u);
        }
    }

    /// How many bytes a pyramid of this shape needs.
    ///
    /// @param rows how many pixels a patch can be centred on, down.
    /// @param columns how many across.
    /// @return the byte count.
    [[nodiscard]] static std::uint64_t bytesFor(std::uint32_t rows, std::uint32_t columns) noexcept
    {
        std::uint32_t levelRows = roundUpToPowerOfTwo(rows);
        std::uint32_t levelColumns = roundUpToPowerOfTwo(columns);
        std::uint64_t total = 0u;
        while (true)
        {
            total += alignedBytes(static_cast<std::uint64_t>(levelRows) * levelColumns * sizeof(float));
            if (levelRows == 1u && levelColumns == 1u)
            {
                return total;
            }
            levelRows = levelRows > 1u ? levelRows / 2u : 1u;
            levelColumns = levelColumns > 1u ? levelColumns / 2u : 1u;
        }
    }

    /// The smallest weight any addressable pixel has accumulated.
    ///
    /// @return the minimum.
    [[nodiscard]] float minimum() const noexcept
    {
        return m_level[m_levels - 1u][0];
    }

    /// Where the least-covered pixel is.
    ///
    /// @param row the row, written on return.
    /// @param column the column, written on return.
    void findMinimum(std::uint32_t& row, std::uint32_t& column) const noexcept
    {
        std::uint32_t currentRow = 0u;
        std::uint32_t currentColumn = 0u;
        for (std::uint32_t above = m_levels - 1u; above > 0u; --above)
        {
            const std::uint32_t level = above - 1u;
            const std::uint32_t rowSpan = m_levelRows[level] > m_levelRows[above] ? 2u : 1u;
            const std::uint32_t columnSpan = m_levelColumns[level] > m_levelColumns[above] ? 2u : 1u;
            const std::uint32_t baseRow = rowSpan == 2u ? currentRow * 2u : currentRow;
            const std::uint32_t baseColumn = columnSpan == 2u ? currentColumn * 2u : currentColumn;
            float best = std::numeric_limits<float>::infinity();
            std::uint32_t bestRow = baseRow;
            std::uint32_t bestColumn = baseColumn;
            bool chosen = false;
            for (std::uint32_t dr = 0u; dr < rowSpan; ++dr)
            {
                for (std::uint32_t dc = 0u; dc < columnSpan; ++dc)
                {
                    const float value = at(level, baseRow + dr, baseColumn + dc);
                    if (!chosen || value < best)
                    {
                        best = value;
                        bestRow = baseRow + dr;
                        bestColumn = baseColumn + dc;
                        chosen = true;
                    }
                }
            }
            currentRow = bestRow;
            currentColumn = bestColumn;
        }
        row = currentRow;
        column = currentColumn;
    }

    /// Adds one patch's weights over the pixels it covered.
    ///
    /// @param weights the patch's per-sample weight, `side` by `side`.
    /// @param side the patch's side.
    /// @param originRow the first pixel row the patch covers, which may be negative.
    /// @param originColumn the first pixel column, which may be negative.
    void increase(std::span<const float> weights, std::uint32_t side, std::int64_t originRow,
                  std::int64_t originColumn) noexcept
    {
        const std::int64_t firstRow = (std::max)(std::int64_t{0}, originRow);
        const std::int64_t lastRow =
            (std::min)(static_cast<std::int64_t>(m_rows), originRow + static_cast<std::int64_t>(side)) - 1;
        const std::int64_t firstColumn = (std::max)(std::int64_t{0}, originColumn);
        const std::int64_t lastColumn =
            (std::min)(static_cast<std::int64_t>(m_columns), originColumn + static_cast<std::int64_t>(side)) - 1;
        if (lastRow < firstRow || lastColumn < firstColumn)
        {
            return;
        }
        const auto count = static_cast<std::uint64_t>(lastColumn - firstColumn + 1);
        for (std::int64_t row = firstRow; row <= lastRow; ++row)
        {
            float* const target = m_level[0] + static_cast<std::uint64_t>(row) * m_levelColumns[0] + firstColumn;
            const float* const source =
                weights.data() + static_cast<std::uint64_t>(row - originRow) * side + (firstColumn - originColumn);
            for (std::uint64_t column = 0u; column < count; ++column)
            {
                target[column] += source[column];
            }
        }

        std::uint32_t low = static_cast<std::uint32_t>(firstRow);
        std::uint32_t high = static_cast<std::uint32_t>(lastRow);
        std::uint32_t left = static_cast<std::uint32_t>(firstColumn);
        std::uint32_t right = static_cast<std::uint32_t>(lastColumn);
        for (std::uint32_t level = 1u; level < m_levels; ++level)
        {
            low = m_levelRows[level] < m_levelRows[level - 1u] ? low / 2u : low;
            high = m_levelRows[level] < m_levelRows[level - 1u] ? high / 2u : high;
            left = m_levelColumns[level] < m_levelColumns[level - 1u] ? left / 2u : left;
            right = m_levelColumns[level] < m_levelColumns[level - 1u] ? right / 2u : right;
            rebuildLevel(level, low, high, left, right);
        }
    }

private:
    /// One cell of one level.
    ///
    /// @param level which level.
    /// @param row the row.
    /// @param column the column.
    /// @return the value.
    [[nodiscard]] float at(std::uint32_t level, std::uint32_t row, std::uint32_t column) const noexcept
    {
        return m_level[level][static_cast<std::uint64_t>(row) * m_levelColumns[level] + column];
    }

    /// Recomputes a rectangle of one level from the level below it.
    ///
    /// @param level which level, at least one.
    /// @param firstRow the first row to recompute.
    /// @param lastRow the last row.
    /// @param firstColumn the first column.
    /// @param lastColumn the last column.
    void rebuildLevel(std::uint32_t level, std::uint32_t firstRow, std::uint32_t lastRow, std::uint32_t firstColumn,
                      std::uint32_t lastColumn) noexcept
    {
        const std::uint32_t below = level - 1u;
        const std::uint32_t rowSpan = m_levelRows[below] > m_levelRows[level] ? 2u : 1u;
        const std::uint32_t columnSpan = m_levelColumns[below] > m_levelColumns[level] ? 2u : 1u;
        for (std::uint32_t row = firstRow; row <= lastRow && row < m_levelRows[level]; ++row)
        {
            float* const target = m_level[level] + static_cast<std::uint64_t>(row) * m_levelColumns[level];
            for (std::uint32_t column = firstColumn; column <= lastColumn && column < m_levelColumns[level]; ++column)
            {
                float smallest = std::numeric_limits<float>::infinity();
                for (std::uint32_t dr = 0u; dr < rowSpan; ++dr)
                {
                    for (std::uint32_t dc = 0u; dc < columnSpan; ++dc)
                    {
                        smallest = (std::min)(smallest, at(below, row * rowSpan + dr, column * columnSpan + dc));
                    }
                }
                target[column] = smallest;
            }
        }
    }

    /// Where each level's cells start.
    std::array<float*, kMaximumCoverageLevels> m_level{};

    /// How many rows each level holds.
    std::array<std::uint32_t, kMaximumCoverageLevels> m_levelRows{};

    /// How many columns each level holds.
    std::array<std::uint32_t, kMaximumCoverageLevels> m_levelColumns{};

    /// How many levels there are.
    std::uint32_t m_levels = 0u;

    /// How many rows a patch can be centred on.
    std::uint32_t m_rows = 0u;

    /// How many columns a patch can be centred on.
    std::uint32_t m_columns = 0u;
};

/// The scratch one worker needs to shrink one tile.
struct TileScratch
{
    /// The padded tile read out of the noisy frame, colour-decorrelated.
    WorkingFrame noisy{};

    /// The padded tile read out of the guide, colour-decorrelated.
    WorkingFrame guide{};

    /// Where the aggregated answer accumulates.
    WorkingFrame answer{};

    /// How much weight each sample of the answer has accumulated.
    std::span<float> weight{};

    /// The noisy patch.
    std::span<float> patch{};

    /// The guide patch.
    std::span<float> guidePatch{};

    /// The regression fit's bilateral weight, reused afterwards for each frequency's attenuation,
    /// which needs fewer entries than a patch has samples.
    std::span<float> regressionWeight{};

    /// The shrinkage's bilateral weight, squared in place before it feeds the coverage pyramid,
    /// because a sample's aggregation weight is the square of its bilateral weight.
    std::span<float> shrinkageWeight{};

    /// The noisy patch's transform buffers, one padded plane per channel.
    std::span<float> noisySpectrum{};

    /// The guide patch's transform buffers.
    std::span<float> guideSpectrum{};

    /// The coverage pyramid's bytes.
    std::span<std::uint8_t> coverage{};
};

/// How many floats one channel's in-place transform plane holds, including its padding.
///
/// @param side the transform side.
/// @return the plane's float count, rounded up to the alignment every plane starts at.
[[nodiscard]] std::uint64_t spectrumPlaneFloats(std::uint32_t side) noexcept
{
    const std::uint64_t rowReals = fftwRealRowReals(side, FftwPlacement::InPlace);
    return alignedBytes(static_cast<std::uint64_t>(side) * rowReals * sizeof(float)) / sizeof(float);
}

/// Reads one padded tile out of a frame, reflecting where it reaches outside.
///
/// The pad is asymmetric because a patch is addressed by its top-left corner: a pixel needs
/// `radius` samples before it and `side - radius - 1` after it for the patch centred on it to exist.
///
/// @param frame the frame to read.
/// @param originX the first interior column, in frame coordinates.
/// @param originY the first interior row.
/// @param padBefore how many samples of border precede the interior.
/// @param destination a frame of the padded size and the source's channel count.
void readPaddedTile(const WorkingFrame& frame, std::int64_t originX, std::int64_t originY, std::uint32_t padBefore,
                    const WorkingFrame& destination) noexcept
{
    for (std::uint32_t channel = 0u; channel < frame.channels; ++channel)
    {
        const float* const source = frame.plane(channel);
        float* const target = destination.plane(channel);
        for (std::uint32_t row = 0u; row < destination.height; ++row)
        {
            const std::uint32_t sourceRow =
                symmetricCoordinate(originY + static_cast<std::int64_t>(row) - padBefore, frame.height);
            const float* const sourceLine = source + static_cast<std::uint64_t>(sourceRow) * frame.width;
            float* const targetLine = target + static_cast<std::uint64_t>(row) * destination.width;
            for (std::uint32_t column = 0u; column < destination.width; ++column)
            {
                const std::uint32_t sourceColumn =
                    symmetricCoordinate(originX + static_cast<std::int64_t>(column) - padBefore, frame.width);
                targetLine[column] = sourceLine[sourceColumn];
            }
        }
    }
}

/// Decorrelates or recorrelates a padded tile's colour channels in place.
///
/// The published method's own transform for this stage, which differs from the patch estimator's:
/// it is the orthonormal basis whose first row is the mean and whose second and third rows are the
/// two chromatic differences, so the inverse is its transpose. A frame that is not three channels
/// is left as it is.
///
/// @param frame the tile.
/// @param forward true to decorrelate, false to put the colours back.
void transformTileColour(const WorkingFrame& frame, bool forward) noexcept
{
    if (frame.channels != 3u)
    {
        return;
    }
    const float rootTwo = std::sqrt(2.0f);
    const float rootThree = std::sqrt(3.0f);
    const float rootSix = std::sqrt(6.0f);
    float* const first = frame.plane(0u);
    float* const second = frame.plane(1u);
    float* const third = frame.plane(2u);
    const std::uint64_t samples = frame.planeSamples();
    for (std::uint64_t i = 0u; i < samples; ++i)
    {
        const float a = first[i];
        const float b = second[i];
        const float c = third[i];
        if (forward)
        {
            first[i] = (a + b + c) / rootThree;
            second[i] = (a - c) / rootTwo;
            third[i] = (a - 2.0f * b + c) / rootSix;
        }
        else
        {
            first[i] = (rootTwo * a + rootThree * b + c) / rootSix;
            second[i] = (a - rootTwo * c) / rootThree;
            third[i] = (rootTwo * a - rootThree * b + c) / rootSix;
        }
    }
}

/// Copies one square patch out of a padded tile.
///
/// @param tile the padded tile.
/// @param row the patch's first row.
/// @param column the patch's first column.
/// @param side the patch's side.
/// @param patch where the samples go, channel-major.
void extractPatch(const WorkingFrame& tile, std::uint32_t row, std::uint32_t column, std::uint32_t side,
                  std::span<float> patch) noexcept
{
    const std::uint64_t patchSamples = static_cast<std::uint64_t>(side) * side;
    for (std::uint32_t channel = 0u; channel < tile.channels; ++channel)
    {
        const float* const source = tile.plane(channel) + static_cast<std::uint64_t>(row) * tile.width + column;
        float* const target = patch.data() + static_cast<std::uint64_t>(channel) * patchSamples;
        for (std::uint32_t y = 0u; y < side; ++y)
        {
            const float* const line = source + static_cast<std::uint64_t>(y) * tile.width;
            std::copy_n(line, side, target + static_cast<std::uint64_t>(y) * side);
        }
    }
}

/// Each position's offset from the patch centre along one axis.
///
/// @param radius where the centre sits.
/// @param offsets where the offsets go, one per position along the axis.
void axisOffsets(std::uint32_t radius, std::span<float> offsets) noexcept
{
    for (std::size_t position = 0u; position < offsets.size(); ++position)
    {
        offsets[position] = static_cast<float>(static_cast<std::int32_t>(position) - static_cast<std::int32_t>(radius));
    }
}

/// Each position's spatial term in a bilateral weight: its squared distance from the patch centre
/// over twice the spatial variance.
///
/// @param offsets each position's offset from the centre along one axis, from `axisOffsets`.
/// @param spatialVariance what divides the squared distance, doubled.
/// @param terms where the terms go, one per sample of the patch.
void spatialTerms(std::span<const float> offsets, float spatialVariance, std::span<float> terms) noexcept
{
    const std::size_t side = offsets.size();
    for (std::size_t row = 0u; row < side; ++row)
    {
        const float rowOffset = offsets[row];
        for (std::size_t column = 0u; column < side; ++column)
        {
            const float columnOffset = offsets[column];
            terms[row * side + column] =
                (rowOffset * rowOffset + columnOffset * columnOffset) / (2.0f * spatialVariance);
        }
    }
}

/// The bilateral weight of one patch around its centre.
///
/// A sample's exponent is its squared differences from the centre, summed over the channels in
/// channel order from zero and divided by the range variance, plus its position's spatial term; its
/// weight is the exponential of the exponent's negation. The squared differences are taken one
/// channel at a time across the whole patch, which keeps every sample's order and lets the samples
/// advance side by side.
///
/// @param guide the guide patch, channel-major.
/// @param side the patch's side.
/// @param channels how many channels the patch carries.
/// @param radius where the centre sits, in both axes.
/// @param rangeVariance what divides the summed squared difference.
/// @param spatialTerm each sample's spatial term, from `spatialTerms`.
/// @param exponential the compiled exponential for this execution, or null for the scalar
///        reference.
/// @param weight where the weights go.
void bilateralWeight(std::span<const float> guide, std::uint32_t side, std::uint32_t channels, std::uint32_t radius,
                     float rangeVariance, std::span<const float> spatialTerm, MathRunFn exponential,
                     std::span<float> weight) noexcept
{
    const std::uint64_t patchSamples = static_cast<std::uint64_t>(side) * side;
    const std::uint64_t centre = static_cast<std::uint64_t>(radius) * side + radius;
    float* const exponent = weight.data();
    std::fill_n(exponent, patchSamples, 0.0f);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const float* const plane = guide.data() + static_cast<std::uint64_t>(channel) * patchSamples;
        const float central = plane[centre];
        for (std::uint64_t position = 0u; position < patchSamples; ++position)
        {
            const float delta = plane[position] - central;
            exponent[position] += delta * delta;
        }
    }
    const float* const spatial = spatialTerm.data();
    for (std::uint64_t position = 0u; position < patchSamples; ++position)
    {
        exponent[position] = -(exponent[position] / rangeVariance + spatial[position]);
    }
    if (exponential != nullptr)
    {
        exponential(weight.data(), weight.data(), patchSamples, 0.0f);
    }
    else
    {
        referenceMathRun(MathFunction::Exp, weight.data(), weight.data(), patchSamples, 0.0);
    }
}

/// The weighted least-squares plane through a patch, one slope pair per channel.
///
/// The plane is anchored at the guide's centre sample and only its two slopes are fitted, from the
/// two-by-two normal equations the weights define. When their determinant is below
/// `kRegressionEpsilon` every slope is zero and the plane is flat. Every sum runs over the patch in
/// raster order from zero; the first channel's two are taken in the same walk as the normal
/// equations', so the five advance side by side, and any further channel's in a walk of its own.
///
/// @param patch the noisy patch.
/// @param guide the guide patch, whose centre is the plane's constant term.
/// @param weight the fit's weights.
/// @param offsets each position's offset from the centre along one axis, from `axisOffsets`.
/// @param channels how many channels the patch carries.
/// @param radius where the centre sits.
/// @param slopes where the row and column slopes go, two per channel.
void regressionPlane(std::span<const float> patch, std::span<const float> guide, std::span<const float> weight,
                     std::span<const float> offsets, std::uint32_t channels, std::uint32_t radius,
                     std::span<float> slopes) noexcept
{
    const auto side = static_cast<std::uint32_t>(offsets.size());
    const std::uint64_t patchSamples = static_cast<std::uint64_t>(side) * side;
    const std::uint64_t centre = static_cast<std::uint64_t>(radius) * side + radius;
    const float firstCentral = guide[static_cast<std::size_t>(centre)];
    float a = 0.0f;
    float b = 0.0f;
    float c = 0.0f;
    float d = 0.0f;
    float e = 0.0f;
    for (std::uint32_t row = 0u; row < side; ++row)
    {
        const float rowOffset = offsets[row];
        const float* const weightLine = weight.data() + static_cast<std::uint64_t>(row) * side;
        const float* const patchLine = patch.data() + static_cast<std::uint64_t>(row) * side;
        for (std::uint32_t column = 0u; column < side; ++column)
        {
            const float columnOffset = offsets[column];
            const float w = weightLine[column];
            a += rowOffset * rowOffset * w;
            b += rowOffset * columnOffset * w;
            c += columnOffset * columnOffset * w;
            const float residual = patchLine[column] - firstCentral;
            d += rowOffset * residual * w;
            e += columnOffset * residual * w;
        }
    }
    const float determinant = a * c - b * b;
    if (std::abs(determinant) < kRegressionEpsilon)
    {
        std::fill(slopes.begin(), slopes.begin() + 2u * channels, 0.0f);
        return;
    }
    slopes[0] = (c * d - b * e) / determinant;
    slopes[1] = (a * e - b * d) / determinant;
    for (std::uint32_t channel = 1u; channel < channels; ++channel)
    {
        const std::uint64_t plane = static_cast<std::uint64_t>(channel) * patchSamples;
        const float central = guide[static_cast<std::size_t>(plane + centre)];
        float rowMoment = 0.0f;
        float columnMoment = 0.0f;
        for (std::uint32_t row = 0u; row < side; ++row)
        {
            const float rowOffset = offsets[row];
            for (std::uint32_t column = 0u; column < side; ++column)
            {
                const float columnOffset = offsets[column];
                const std::uint64_t position = static_cast<std::uint64_t>(row) * side + column;
                const float w = weight[static_cast<std::size_t>(position)];
                const float residual = patch[static_cast<std::size_t>(plane + position)] - central;
                rowMoment += rowOffset * residual * w;
                columnMoment += columnOffset * residual * w;
            }
        }
        slopes[static_cast<std::size_t>(2u * channel)] = (c * rowMoment - b * columnMoment) / determinant;
        slopes[static_cast<std::size_t>(2u * channel + 1u)] = (a * columnMoment - b * rowMoment) / determinant;
    }
}

/// Subtracts a plane from a patch.
///
/// @param patch the patch, changed in place.
/// @param side the patch's side.
/// @param channels how many channels the patch carries.
/// @param radius where the plane's origin sits.
/// @param slopes the row and column slopes, two per channel.
void subtractPlane(std::span<float> patch, std::uint32_t side, std::uint32_t channels, std::uint32_t radius,
                   std::span<const float> slopes) noexcept
{
    const std::uint64_t patchSamples = static_cast<std::uint64_t>(side) * side;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const float rowSlope = slopes[static_cast<std::size_t>(2u * channel)];
        const float columnSlope = slopes[static_cast<std::size_t>(2u * channel + 1u)];
        float* const plane = patch.data() + static_cast<std::uint64_t>(channel) * patchSamples;
        for (std::uint32_t row = 0u; row < side; ++row)
        {
            const auto rowOffset =
                static_cast<float>(static_cast<std::int32_t>(row) - static_cast<std::int32_t>(radius));
            float* const line = plane + static_cast<std::uint64_t>(row) * side;
            for (std::uint32_t column = 0u; column < side; ++column)
            {
                const auto columnOffset =
                    static_cast<float>(static_cast<std::int32_t>(column) - static_cast<std::int32_t>(radius));
                line[column] -= rowSlope * rowOffset + columnSlope * columnOffset;
            }
        }
    }
}

/// What a patch's shrinkage weight sums to, alone and squared, and each channel's weighted noisy and
/// guide samples.
struct WeightedSums
{
    /// The weights' sum.
    float total = 0.0f;

    /// The sum of the weights' squares.
    float squared = 0.0f;

    /// Each channel's noisy samples, weighted and summed.
    std::array<float, kMaximumPatchDenoiseChannels> noisy{};

    /// Each channel's guide samples, weighted and summed.
    std::array<float, kMaximumPatchDenoiseChannels> guide{};
};

/// Takes a patch's weighted sums.
///
/// Each runs over the patch in raster order from zero. The first channel's two are taken in the same
/// walk as the weights' own two, so the four advance side by side; any further channel's take a walk
/// of their own.
///
/// @param weight the shrinkage weight.
/// @param patch the noisy patch, channel-major.
/// @param guide the guide patch, channel-major.
/// @param channels how many channels the patches carry.
/// @return the sums.
[[nodiscard]] WeightedSums weightedSums(std::span<const float> weight, std::span<const float> patch,
                                        std::span<const float> guide, std::uint32_t channels) noexcept
{
    const std::size_t patchSamples = weight.size();
    float total = 0.0f;
    float squared = 0.0f;
    float noisy = 0.0f;
    float guided = 0.0f;
    for (std::size_t i = 0u; i < patchSamples; ++i)
    {
        const float w = weight[i];
        total += w;
        squared += w * w;
        noisy += w * patch[i];
        guided += w * guide[i];
    }
    WeightedSums sums;
    sums.total = total;
    sums.squared = squared;
    sums.noisy[0] = noisy;
    sums.guide[0] = guided;
    for (std::uint32_t channel = 1u; channel < channels; ++channel)
    {
        const std::size_t plane = channel * patchSamples;
        float channelNoisy = 0.0f;
        float channelGuide = 0.0f;
        for (std::size_t i = 0u; i < patchSamples; ++i)
        {
            channelNoisy += weight[i] * patch[plane + i];
            channelGuide += weight[i] * guide[plane + i];
        }
        sums.noisy[channel] = channelNoisy;
        sums.guide[channel] = channelGuide;
    }
    return sums;
}

/// Writes a weighted patch into the transform's padded planes.
///
/// The sample a frequency transform sees is the weighted patch pulled towards its own weighted mean,
/// so the window the weight imposes does not itself appear in the spectrum as an edge.
///
/// @param patch the patch to read, which is not changed.
/// @param weight the bilateral weight.
/// @param side the patch's side.
/// @param channels how many channels the patch carries.
/// @param means each channel's weighted mean: its weighted sum over the weights' sum.
/// @param spectrum the padded transform planes.
void modifyPatch(std::span<const float> patch, std::span<const float> weight, std::uint32_t side,
                 std::uint32_t channels, std::span<const float> means, std::span<float> spectrum) noexcept
{
    const std::uint64_t patchSamples = static_cast<std::uint64_t>(side) * side;
    const std::uint64_t rowReals = fftwRealRowReals(side, FftwPlacement::InPlace);
    const std::uint64_t planeFloats = spectrumPlaneFloats(side);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const float* const source = patch.data() + static_cast<std::uint64_t>(channel) * patchSamples;
        const float mean = means[static_cast<std::size_t>(channel)];
        float* const target = spectrum.data() + static_cast<std::uint64_t>(channel) * planeFloats;
        for (std::uint32_t row = 0u; row < side; ++row)
        {
            float* const line = target + static_cast<std::uint64_t>(row) * rowReals;
            const float* const sourceLine = source + static_cast<std::uint64_t>(row) * side;
            const float* const weightLine = weight.data() + static_cast<std::uint64_t>(row) * side;
            for (std::uint32_t column = 0u; column < side; ++column)
            {
                line[column] = weightLine[column] * sourceLine[column] + (1.0f - weightLine[column]) * mean;
            }
        }
    }
}

/// Squares every weight of a patch in place, which turns a bilateral weight into the aggregation
/// weight its sample carries.
///
/// @param weights the weights.
void squareWeights(std::span<float> weights) noexcept
{
    float* const values = weights.data();
    const std::size_t count = weights.size();
    for (std::size_t i = 0u; i < count; ++i)
    {
        values[i] = values[i] * values[i];
    }
}

/// Multiplies the samples of one inverse transform's plane by a factor, leaving its padding alone.
///
/// @param plane the plane's first sample.
/// @param side the transform side.
/// @param rowReals how many floats one padded row holds.
/// @param factor what to multiply by.
void scaleTransformPlane(float* plane, std::uint64_t side, std::uint64_t rowReals, float factor) noexcept
{
    for (std::uint64_t row = 0u; row < side; ++row)
    {
        float* const line = plane + row * rowReals;
        for (std::uint64_t column = 0u; column < side; ++column)
        {
            line[column] *= factor;
        }
    }
}

/// Adds one patch whose guide was accepted into a tile: each channel's guide sample with the plane
/// put back, times its aggregation weight, into the answer, and the weight into the tile's weight.
///
/// A sample of the answer takes one term from a patch and a sample of the weight one, so walking the
/// channels and then the weight one at a time keeps every sample's order.
///
/// @param answer the tile's answer.
/// @param covered the tile's aggregation weight, one per sample of an answer plane.
/// @param patchRow the patch's first row in the tile.
/// @param patchColumn the patch's first column.
/// @param guidePatch the guide patch with the plane taken out, channel-major.
/// @param weight each patch sample's aggregation weight.
/// @param offsets each position's offset from the centre along one axis, one per patch column.
/// @param slopes the plane's row and column slopes, two per channel.
void addAcceptedPatch(const WorkingFrame& answer, std::span<float> covered, std::uint32_t patchRow,
                      std::uint32_t patchColumn, std::span<const float> guidePatch, std::span<const float> weight,
                      std::span<const float> offsets, std::span<const float> slopes) noexcept
{
    const std::size_t side = offsets.size();
    const std::size_t patchSamples = side * side;
    const std::uint64_t width = answer.width;
    const std::uint32_t channels = answer.channels;
    const float* const columnOffsets = offsets.data();
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const float rowSlope = slopes[2u * channel];
        const float columnSlope = slopes[2u * channel + 1u];
        const float* const guidePlane = guidePatch.data() + channel * patchSamples;
        float* const answerPlane = answer.plane(channel);
        for (std::size_t row = 0u; row < side; ++row)
        {
            const float rowOffset = offsets[row];
            const float* const weightLine = weight.data() + row * side;
            const float* const guideLine = guidePlane + row * side;
            float* const answerLine = answerPlane + (patchRow + row) * width + patchColumn;
            for (std::size_t column = 0u; column < side; ++column)
            {
                const float restored = guideLine[column] + rowSlope * rowOffset + columnSlope * columnOffsets[column];
                answerLine[column] += restored * weightLine[column];
            }
        }
    }
    for (std::size_t row = 0u; row < side; ++row)
    {
        const float* const weightLine = weight.data() + row * side;
        float* const coveredLine = covered.data() + (patchRow + row) * width + patchColumn;
        for (std::size_t column = 0u; column < side; ++column)
        {
            coveredLine[column] += weightLine[column];
        }
    }
}

/// Adds one shrunk patch into a tile: each channel's shrunk sample, pulled back from the weighted
/// mean and with the plane put back, times its bilateral weight, into the answer, and the weight's
/// square into the tile's weight. Each sample keeps its order, as in `addAcceptedPatch`.
///
/// @param answer the tile's answer.
/// @param covered the tile's aggregation weight, one per sample of an answer plane.
/// @param patchRow the patch's first row in the tile.
/// @param patchColumn the patch's first column.
/// @param spectrum the inverse transforms' padded planes, one per channel.
/// @param planeFloats how many floats one channel's plane occupies.
/// @param rowReals how many floats one padded row holds.
/// @param weight each patch sample's bilateral weight.
/// @param offsets each position's offset from the centre along one axis, one per patch column.
/// @param slopes the plane's row and column slopes, two per channel.
/// @param means each channel's weighted mean.
void addShrunkPatch(const WorkingFrame& answer, std::span<float> covered, std::uint32_t patchRow,
                    std::uint32_t patchColumn, std::span<const float> spectrum, std::uint64_t planeFloats,
                    std::uint64_t rowReals, std::span<const float> weight, std::span<const float> offsets,
                    std::span<const float> slopes, std::span<const float> means) noexcept
{
    const std::size_t side = offsets.size();
    const std::uint64_t width = answer.width;
    const std::uint32_t channels = answer.channels;
    const float* const columnOffsets = offsets.data();
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const float rowSlope = slopes[2u * channel];
        const float columnSlope = slopes[2u * channel + 1u];
        const float mean = means[channel];
        const float* const plane = spectrum.data() + channel * planeFloats;
        float* const answerPlane = answer.plane(channel);
        for (std::size_t row = 0u; row < side; ++row)
        {
            const float rowOffset = offsets[row];
            const float* const valueLine = plane + row * rowReals;
            const float* const weightLine = weight.data() + row * side;
            float* const answerLine = answerPlane + (patchRow + row) * width + patchColumn;
            for (std::size_t column = 0u; column < side; ++column)
            {
                const float w = weightLine[column];
                const float slope = rowSlope * rowOffset + columnSlope * columnOffsets[column];
                const float restored = valueLine[column] - (1.0f - w) * mean + slope * w;
                answerLine[column] += restored * w;
            }
        }
    }
    for (std::size_t row = 0u; row < side; ++row)
    {
        const float* const weightLine = weight.data() + row * side;
        float* const coveredLine = covered.data() + (patchRow + row) * width + patchColumn;
        for (std::size_t column = 0u; column < side; ++column)
        {
            const float w = weightLine[column];
            coveredLine[column] += w * w;
        }
    }
}

} // namespace

Status runGuidedShrinkage(const ExecutionContext& execution, const WorkingFrame& noisy, const WorkingFrame& guide,
                          const WorkingFrame& output, const GuidedShrinkageGeometry& geometry,
                          GuidedShrinkageCounters& counters, const char* operation)
{
    const std::uint32_t side = geometry.transformSize;
    const std::uint32_t radius = geometry.radius;
    const std::uint32_t channels = noisy.channels;

    std::uint32_t tilesX = 0u;
    std::uint32_t tilesY = 0u;
    // A tile's pad is the transform side less one, split around the patch centre, so it carries half
    // a transform of border on each side, and four of those is the smallest interior a tile takes. The
    // preferred interior is four whole transforms, or 256 samples when that is more.
    tileGridBetween(noisy.width, noisy.height, (std::max)(4u * side, 256u), 2u * side, tilesX, tilesY);
    const std::uint32_t tileCount = tilesX * tilesY;
    counters.tileCount = tileCount;

    const std::uint32_t widestInterior = (noisy.width + tilesX - 1u) / tilesX;
    const std::uint32_t tallestInterior = (noisy.height + tilesY - 1u) / tilesY;
    const std::uint32_t paddedWidth = widestInterior + side - 1u;
    const std::uint32_t paddedHeight = tallestInterior + side - 1u;

    const std::uint64_t paddedSamples = static_cast<std::uint64_t>(paddedWidth) * paddedHeight;
    const std::uint64_t paddedFrame = paddedSamples * channels;
    const std::uint64_t patchSamples = static_cast<std::uint64_t>(side) * side;
    const std::uint64_t planeFloats = spectrumPlaneFloats(side);
    const std::uint64_t coverageBytes = CoveragePyramid::bytesFor(tallestInterior, widestInterior);

    const std::uint64_t perWorker =
        alignedBytes(paddedFrame * sizeof(float)) * 3u + alignedBytes(paddedSamples * sizeof(float)) +
        alignedBytes(patchSamples * channels * sizeof(float)) * 2u + alignedBytes(patchSamples * sizeof(float)) * 2u +
        alignedBytes(planeFloats * channels * sizeof(float)) * 2u + coverageBytes;
    // What every patch reads and no worker writes, after the workers' blocks: each position's offset
    // from the centre along an axis, and the spatial terms of the regression's and the shrinkage's
    // weights.
    const std::uint64_t offsetBytes = alignedBytes(side * sizeof(float));
    const std::uint64_t termBytes = alignedBytes(patchSamples * sizeof(float));

    const TaskCost cost{.bytesPerItem = paddedFrame * sizeof(float), .operationsPerItem = paddedSamples * patchSamples};
    const std::uint32_t workers = (std::max)(plannedWorkerCount(execution, tileCount, cost), 1u);
    Result<plane_detail::ScopedContextMemory> taken = plane_detail::ScopedContextMemory::take(
        execution, perWorker * workers + offsetBytes + termBytes * 2u, kDefaultBufferAlignmentBytes, operation);
    if (!taken.hasValue())
    {
        return taken.status();
    }
    const plane_detail::ScopedContextMemory memory = std::move(taken).value();

    Result<plane_detail::ScopedContextMemory> countersTaken = plane_detail::ScopedContextMemory::take(
        execution, static_cast<std::uint64_t>(workers) * sizeof(GuidedShrinkageCounters), kDefaultBufferAlignmentBytes,
        operation);
    if (!countersTaken.hasValue())
    {
        return countersTaken.status();
    }
    const plane_detail::ScopedContextMemory workerCounters = std::move(countersTaken).value();
    counters.workingBytes = (std::max)(counters.workingBytes, memory.bytes() + workerCounters.bytes());
    auto* const perWorkerCounters = static_cast<GuidedShrinkageCounters*>(workerCounters.data());
    for (std::uint32_t worker = 0u; worker < workers; ++worker)
    {
        perWorkerCounters[worker] = GuidedShrinkageCounters{};
    }

    // The two plans are built once, over the first worker's first spectrum plane, and then executed
    // over every worker's planes through the new-array interface: the planes share their geometry by
    // construction and each starts at the engine allocator's alignment, which is above the alignment
    // class the plan is measured at.
    const auto carve = [&](std::uint32_t workerSlot) noexcept {
        std::uint8_t* cursor =
            static_cast<std::uint8_t*>(memory.data()) + static_cast<std::uint64_t>(workerSlot) * perWorker;
        const auto take = [&cursor](std::uint64_t bytes) noexcept {
            std::uint8_t* const result = cursor;
            cursor += alignedBytes(bytes);
            return result;
        };
        const auto floats = [&take](std::uint64_t count) noexcept {
            return std::span<float>{reinterpret_cast<float*>(take(count * sizeof(float))),
                                    static_cast<std::size_t>(count)};
        };
        TileScratch scratch;
        scratch.noisy = WorkingFrame{
            .base = floats(paddedFrame).data(), .width = paddedWidth, .height = paddedHeight, .channels = channels};
        scratch.guide = WorkingFrame{
            .base = floats(paddedFrame).data(), .width = paddedWidth, .height = paddedHeight, .channels = channels};
        scratch.answer = WorkingFrame{
            .base = floats(paddedFrame).data(), .width = paddedWidth, .height = paddedHeight, .channels = channels};
        scratch.weight = floats(paddedSamples);
        scratch.patch = floats(patchSamples * channels);
        scratch.guidePatch = floats(patchSamples * channels);
        scratch.regressionWeight = floats(patchSamples);
        scratch.shrinkageWeight = floats(patchSamples);
        scratch.noisySpectrum = floats(planeFloats * channels);
        scratch.guideSpectrum = floats(planeFloats * channels);
        scratch.coverage = std::span<std::uint8_t>{take(coverageBytes), static_cast<std::size_t>(coverageBytes)};
        return scratch;
    };

    const TileScratch first = carve(0u);
    const std::uint64_t rowReals = fftwRealRowReals(side, FftwPlacement::InPlace);
    const std::optional<PlaneView<float>> spectrumPlane = PlaneView<float>::create(
        std::span<float>{first.noisySpectrum.data(), static_cast<std::size_t>((side - 1u) * rowReals + side)}, side,
        side, 1u, rowReals * sizeof(float));
    if (!spectrumPlane.has_value())
    {
        return patchFailure(ErrorCode::Internal, operation, "the transform buffer does not describe a plane");
    }
    const ExternalBufferContract contract{.engineDomain = NumericDomain::PhysicalScaled,
                                          .libraryDomain = NumericDomain::PhysicalScaled,
                                          .ownership = BufferOwnership::EngineOwned,
                                          .scope = BorrowScope::Operation,
                                          .mutability = BufferMutability::ReadWrite};
    const std::optional<FftwArrayDescription> described =
        describeAsFftwReal(*spectrumPlane, contract, FftwPlacement::InPlace, kFftwDefaultAlignmentBytes);
    if (!described.has_value())
    {
        return patchFailure(ErrorCode::Internal, operation, "the transform buffer does not satisfy the adapter bridge");
    }
    Result<fftw_detail::FftwRealTransformPlan> forward =
        fftw_detail::FftwRealTransformPlan::create(fftw_detail::FftwTransformDirection::Forward, *described, operation);
    if (!forward.hasValue())
    {
        return forward.status();
    }
    Result<fftw_detail::FftwRealTransformPlan> inverse =
        fftw_detail::FftwRealTransformPlan::create(fftw_detail::FftwTransformDirection::Inverse, *described, operation);
    if (!inverse.hasValue())
    {
        return inverse.status();
    }

    const float noiseVariance = geometry.sigma * geometry.sigma;
    const float shrinkageRange = geometry.rangeFactor * noiseVariance;
    const float shrinkageSpatial = geometry.spatialSigma * geometry.spatialSigma;
    const float regressionRange = shrinkageRange * kRegressionRangeRelaxation;
    const float regressionSpatial = shrinkageSpatial * kRegressionSpatialRelaxation;
    const MathRunFn exponential = mathRunFor(execution, MathFunction::Exp);
    std::uint8_t* const shared = static_cast<std::uint8_t*>(memory.data()) + perWorker * workers;
    const std::span<float> offsets{reinterpret_cast<float*>(shared), side};
    const std::span<float> regressionTerms{reinterpret_cast<float*>(shared + offsetBytes),
                                           static_cast<std::size_t>(patchSamples)};
    const std::span<float> shrinkageTerms{reinterpret_cast<float*>(shared + offsetBytes + termBytes),
                                          static_cast<std::size_t>(patchSamples)};
    axisOffsets(radius, offsets);
    spatialTerms(offsets, regressionSpatial, regressionTerms);
    spatialTerms(offsets, shrinkageSpatial, shrinkageTerms);

    std::atomic<std::uint64_t> tilesFinished{0u};
    const Status walked = parallelForWorkers(execution, tileCount, cost, [&](WorkerTask task) -> Status {
        const TileScratch worker = carve(task.workerSlot);
        std::array<float, 2u * kMaximumPatchDenoiseChannels> slopes{};
        std::array<float, kMaximumPatchDenoiseChannels> means{};
        std::array<float, kMaximumPatchDenoiseChannels> guideMeans{};

        for (std::uint64_t item = task.range.begin; item < task.range.end; ++item)
        {
            const Status cancelled = execution.checkCancellation();
            if (cancelled.failed())
            {
                return cancelled;
            }
            const FrameTile tile = tileAt(noisy.width, noisy.height, tilesX, tilesY, static_cast<std::uint32_t>(item));
            const std::uint32_t thisWidth = tile.interiorWidth + side - 1u;
            const std::uint32_t thisHeight = tile.interiorHeight + side - 1u;
            const auto thisSamples = static_cast<std::size_t>(thisWidth) * thisHeight;

            TileScratch local = worker;
            local.noisy.width = thisWidth;
            local.noisy.height = thisHeight;
            local.guide.width = thisWidth;
            local.guide.height = thisHeight;
            local.answer.width = thisWidth;
            local.answer.height = thisHeight;
            local.weight = worker.weight.first(thisSamples);

            readPaddedTile(noisy, tile.originX, tile.originY, radius, local.noisy);
            readPaddedTile(guide, tile.originX, tile.originY, radius, local.guide);
            transformTileColour(local.noisy, true);
            transformTileColour(local.guide, true);
            std::fill_n(local.answer.base, local.answer.sampleCount(), 0.0f);
            std::fill(local.weight.begin(), local.weight.end(), 0.0f);

            CoveragePyramid coverage{local.coverage.data(), tile.interiorHeight, tile.interiorWidth};
            while (coverage.minimum() < geometry.coverageThreshold)
            {
                const Status stopped = execution.checkCancellation();
                if (stopped.failed())
                {
                    return stopped;
                }
                std::uint32_t patchRow = 0u;
                std::uint32_t patchColumn = 0u;
                coverage.findMinimum(patchRow, patchColumn);
                extractPatch(local.noisy, patchRow, patchColumn, side, local.patch);
                extractPatch(local.guide, patchRow, patchColumn, side, local.guidePatch);
                bilateralWeight(local.guidePatch, side, channels, radius, regressionRange, regressionTerms, exponential,
                                local.regressionWeight);
                regressionPlane(local.patch, local.guidePatch, local.regressionWeight, offsets, channels, radius,
                                std::span<float>{slopes});
                subtractPlane(local.patch, side, channels, radius, std::span<const float>{slopes});
                subtractPlane(local.guidePatch, side, channels, radius, std::span<const float>{slopes});
                bilateralWeight(local.guidePatch, side, channels, radius, shrinkageRange, shrinkageTerms, exponential,
                                local.shrinkageWeight);

                const WeightedSums sums =
                    weightedSums(local.shrinkageWeight.first(static_cast<std::size_t>(patchSamples)), local.patch,
                                 local.guidePatch, channels);
                ++perWorkerCounters[task.workerSlot].patchesPlaced;

                if (sums.total < kMinimumTransformWeight)
                {
                    // Too little of the patch survives the weight for a spectrum to describe it, so
                    // the guide is accepted as it stands and the plane is put back.
                    ++perWorkerCounters[task.workerSlot].guideAcceptedPatches;
                    squareWeights(local.shrinkageWeight);
                    addAcceptedPatch(local.answer, local.weight, patchRow, patchColumn, local.guidePatch,
                                     local.shrinkageWeight, offsets, std::span<const float>{slopes});
                }
                else
                {
                    for (std::uint32_t channel = 0u; channel < channels; ++channel)
                    {
                        means[channel] = sums.noisy[channel] / sums.total;
                        guideMeans[channel] = sums.guide[channel] / sums.total;
                    }
                    modifyPatch(local.patch, local.shrinkageWeight, side, channels, std::span<const float>{means},
                                local.noisySpectrum);
                    modifyPatch(local.guidePatch, local.shrinkageWeight, side, channels,
                                std::span<const float>{guideMeans}, local.guideSpectrum);
                    const float noiseFloor = sums.squared * noiseVariance;

                    for (std::uint32_t channel = 0u; channel < channels; ++channel)
                    {
                        float* const noisyPlane =
                            local.noisySpectrum.data() + static_cast<std::uint64_t>(channel) * planeFloats;
                        float* const guidePlane =
                            local.guideSpectrum.data() + static_cast<std::uint64_t>(channel) * planeFloats;
                        const Status transformed = forward->execute(noisyPlane, operation);
                        if (transformed.failed())
                        {
                            return transformed;
                        }
                        const Status guided = forward->execute(guidePlane, operation);
                        if (guided.failed())
                        {
                            return guided;
                        }
                        const std::uint64_t frequencies = static_cast<std::uint64_t>(side) * fftwHermitianColumns(side);
                        local.regressionWeight[0] = 1.0f;
                        for (std::uint64_t frequency = 1u; frequency < frequencies; ++frequency)
                        {
                            const float real = guidePlane[2u * frequency];
                            const float imaginary = guidePlane[2u * frequency + 1u];
                            const float power = real * real + imaginary * imaginary;
                            const float ratio = power / noiseFloor;
                            local.regressionWeight[static_cast<std::size_t>(frequency)] =
                                ratio > 0.0f ? -kShrinkageExponent / ratio : -std::numeric_limits<float>::infinity();
                        }
                        if (exponential != nullptr)
                        {
                            exponential(local.regressionWeight.data() + 1u, local.regressionWeight.data() + 1u,
                                        frequencies - 1u, 0.0f);
                        }
                        else
                        {
                            referenceMathRun(MathFunction::Exp, local.regressionWeight.data() + 1u,
                                             local.regressionWeight.data() + 1u, frequencies - 1u, 0.0);
                        }
                        for (std::uint64_t frequency = 1u; frequency < frequencies; ++frequency)
                        {
                            const float attenuation = local.regressionWeight[static_cast<std::size_t>(frequency)];
                            noisyPlane[2u * frequency] *= attenuation;
                            noisyPlane[2u * frequency + 1u] *= attenuation;
                        }
                        const Status restored = inverse->execute(noisyPlane, operation);
                        if (restored.failed())
                        {
                            return restored;
                        }
                        scaleTransformPlane(noisyPlane, side, rowReals, 1.0f / static_cast<float>(patchSamples));
                    }

                    addShrunkPatch(local.answer, local.weight, patchRow, patchColumn, local.noisySpectrum, planeFloats,
                                   rowReals, local.shrinkageWeight, offsets, std::span<const float>{slopes},
                                   std::span<const float>{means});
                    squareWeights(local.shrinkageWeight);
                }

                coverage.increase(local.shrinkageWeight.first(static_cast<std::size_t>(patchSamples)), side,
                                  static_cast<std::int64_t>(patchRow) - radius,
                                  static_cast<std::int64_t>(patchColumn) - radius);
            }

            // Every sample of the padded tile carries weight, because the coverage rule places
            // patches until each pixel a patch can be centred on is covered and those patches span
            // the whole padded extent. A sample that somehow carries none keeps the guide.
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                float* const answer = local.answer.plane(channel);
                const float* const guidePlane = local.guide.plane(channel);
                for (std::size_t i = 0u; i < thisSamples; ++i)
                {
                    const float w = local.weight[i];
                    answer[i] = w > 0.0f ? answer[i] / w : guidePlane[i];
                }
            }
            transformTileColour(local.answer, false);

            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                const float* const source = local.answer.plane(channel);
                float* const target = output.plane(channel);
                for (std::uint32_t row = 0u; row < tile.interiorHeight; ++row)
                {
                    const float* const line = source + static_cast<std::uint64_t>(row + radius) * thisWidth + radius;
                    float* const targetLine =
                        target + static_cast<std::uint64_t>(tile.originY + row) * output.width + tile.originX;
                    std::copy_n(line, tile.interiorWidth, targetLine);
                }
            }
            execution.reportProgress(tilesFinished.fetch_add(1u, std::memory_order_relaxed) + 1u, tileCount,
                                     kShrinkageStage);
        }
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    execution.reportProgress(tileCount, tileCount, kShrinkageStage);

    for (std::uint32_t worker = 0u; worker < workers; ++worker)
    {
        counters.patchesPlaced += perWorkerCounters[worker].patchesPlaced;
        counters.guideAcceptedPatches += perWorkerCounters[worker].guideAcceptedPatches;
    }
    return Status::success();
}

} // namespace an::patch_detail
