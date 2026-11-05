// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - shared global-normalization frame fixtures and their oracle.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace an::bench
{

/// The factor the target frame applies to the reference inside the canvas.
inline constexpr double kNormalizationFixtureScale = 1.25;

/// The offset the target frame adds to the scaled reference inside the canvas.
inline constexpr double kNormalizationFixtureOffset = 0.03125;

/// The share of each dimension, on every side, that both frames leave as an exact-zero border.
///
/// A registered canvas carries such a border wherever a frame's footprint does not reach, and it
/// is what the zero-is-no-data rule exists for: an engine that counted it as sky would derive
/// coefficients biased by the border fraction.
inline constexpr std::uint32_t kNormalizationFixtureBorderDivisor = 8u;

/// The fixed factor the apply rows use.
inline constexpr double kNormalizationApplyScale = 1.03125;

/// The fixed offset the apply rows use.
inline constexpr double kNormalizationApplyOffset = -0.0078125;

/// The two frames a normalization row relates.
struct NormalizationFixtureFrames
{
    /// The fixture with its border zeroed: the frame the target is normalized to.
    std::vector<float> reference;

    /// The reference scaled and offset inside the canvas, zero in the border.
    std::vector<float> target;
};

/// Whether a position lies in the zero border both fixture frames carry.
///
/// @param x the column.
/// @param y the row.
/// @param width the frame width.
/// @param height the frame height.
/// @return true inside the border band.
[[nodiscard]] inline bool normalizationFixtureBorder(std::uint32_t x, std::uint32_t y, std::uint32_t width,
                                                     std::uint32_t height) noexcept
{
    const std::uint32_t borderX = width / kNormalizationFixtureBorderDivisor;
    const std::uint32_t borderY = height / kNormalizationFixtureBorderDivisor;
    return x < borderX || x >= width - borderX || y < borderY || y >= height - borderY;
}

/// Builds the reference and target frames the normalization rows read.
///
/// The target is an exact affine image of the reference inside the canvas, so the coefficients that
/// bring it back are known - the reciprocal factor and the negated scaled offset - and so is the
/// answer: the reference itself. Every constant is a power-of-two fraction and the arithmetic is one
/// Float64 multiply-add narrowed once, so every run builds byte-identical frames.
///
/// @param samples the planar fixture samples, `width * height * channels` of them.
/// @param width the frame width.
/// @param height the frame height.
/// @param channels the plane count.
/// @return the two frames, each as long as `samples`.
[[nodiscard]] inline NormalizationFixtureFrames makeNormalizationFixtureFrames(std::span<const float> samples,
                                                                               std::uint32_t width,
                                                                               std::uint32_t height,
                                                                               std::uint32_t channels)
{
    NormalizationFixtureFrames frames{.reference = std::vector<float>(samples.size(), 0.0f),
                                      .target = std::vector<float>(samples.size(), 0.0f)};
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const std::size_t index =
                    (static_cast<std::size_t>(channel) * height + y) * width + static_cast<std::size_t>(x);
                if (index >= samples.size() || normalizationFixtureBorder(x, y, width, height))
                {
                    continue;
                }
                const float value = samples[index];
                if (!std::isfinite(value))
                {
                    continue;
                }
                frames.reference[index] = value;
                frames.target[index] = static_cast<float>(static_cast<double>(value) * kNormalizationFixtureScale +
                                                          kNormalizationFixtureOffset);
            }
        }
    }
    return frames;
}

/// How far a normalized target is from the reference it should have become.
///
/// The largest normalized error over every position the target carried data at - absolute below one,
/// relative above - and the largest finite double when a border zero was moved, a data sample became
/// zero, or a sample is not finite, so a result stays schema-valid and fails its tolerance.
///
/// @param normalized the target after normalization, as long as the frames.
/// @param frames the fixture frames the row was built from.
/// @return the error, or the largest finite double for a structural failure.
[[nodiscard]] inline double normalizationRecoveryError(std::span<const float> normalized,
                                                       const NormalizationFixtureFrames& frames) noexcept
{
    if (normalized.size() != frames.reference.size() || frames.reference.size() != frames.target.size())
    {
        return (std::numeric_limits<double>::max)();
    }
    double worst = 0.0;
    for (std::size_t index = 0u; index < normalized.size(); ++index)
    {
        const float actual = normalized[index];
        if (frames.target[index] == 0.0f)
        {
            if (actual != 0.0f)
            {
                return (std::numeric_limits<double>::max)();
            }
            continue;
        }
        if (!std::isfinite(actual) || actual == 0.0f)
        {
            return (std::numeric_limits<double>::max)();
        }
        const double expected = frames.reference[index];
        const double magnitude = std::abs(expected);
        const double error = std::abs(static_cast<double>(actual) - expected) / (magnitude < 1.0 ? 1.0 : magnitude);
        worst = error > worst ? error : worst;
    }
    return worst;
}

/// The factor the local target frame applies to the reference inside the canvas.
///
/// Below one, where the global fixture's is above it, so that the target's bright tail stays under
/// the estimators' default high clip and a clipped tail cannot bias the percentile background of
/// one frame against the other's.
inline constexpr double kLocalNormalizationFixtureScale = 0.8;

/// The amplitude, across the whole canvas, of the plane the synthetic local target frame adds to
/// the scaled reference.
///
/// A power-of-two fraction so every run builds byte-identical frames, and small against the
/// synthetic frame's own spread so that its variation inside one node's region stays well below the
/// residuals the ratio estimator divides by.
inline constexpr double kLocalNormalizationFixtureGradient = 0.03125;

/// The amplitude of that plane over the real capture.
///
/// A real sky's residual noise is a few parts in ten thousand of full scale, so the plane a
/// residual-ratio estimate can be expected to see through has to be proportionate to it: the
/// estimator divides the reference's residual by the target's, and a plane whose variation inside
/// one region exceeds the residual noise makes the sky pixels' ratios swamp the signal's.
inline constexpr double kLocalNormalizationRealFixtureGradient = 0.00048828125;

/// How many cells a side the local-normalization rows fit.
inline constexpr std::uint32_t kLocalNormalizationFixtureGridSize = 8u;

/// The plane the local target frame adds, at one position.
///
/// @param x the column.
/// @param y the row.
/// @param width the frame width.
/// @param height the frame height.
/// @param amplitude the plane's rise from the origin to the far corner.
/// @return the level added there.
[[nodiscard]] inline double localNormalizationFixtureGradient(std::uint32_t x, std::uint32_t y, std::uint32_t width,
                                                              std::uint32_t height, double amplitude) noexcept
{
    const double span = static_cast<double>(width) + static_cast<double>(height) - 2.0;
    return span <= 0.0 ? 0.0 : amplitude * (static_cast<double>(x) + static_cast<double>(y)) / span;
}

/// Builds the reference and target frames the local normalization rows read.
///
/// The target is the reference scaled and offset as the global fixture is, plus a plane that no
/// global match can remove, inside the same exact-zero border. The answer is the reference itself
/// to within what a node grid can follow of a plane, and the run is scored against it.
///
/// @param samples the planar fixture samples, `width * height * channels` of them.
/// @param width the frame width.
/// @param height the frame height.
/// @param channels the plane count.
/// @param gradient the plane's rise from the origin to the far corner.
/// @return the two frames, each as long as `samples`.
[[nodiscard]] inline NormalizationFixtureFrames
makeLocalNormalizationFixtureFrames(std::span<const float> samples, std::uint32_t width, std::uint32_t height,
                                    std::uint32_t channels, double gradient)
{
    NormalizationFixtureFrames frames{.reference = std::vector<float>(samples.size(), 0.0f),
                                      .target = std::vector<float>(samples.size(), 0.0f)};
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const std::size_t index =
                    (static_cast<std::size_t>(channel) * height + y) * width + static_cast<std::size_t>(x);
                if (index >= samples.size() || normalizationFixtureBorder(x, y, width, height))
                {
                    continue;
                }
                const float value = samples[index];
                if (!std::isfinite(value))
                {
                    continue;
                }
                frames.reference[index] = value;
                frames.target[index] = static_cast<float>(
                    static_cast<double>(value) * kLocalNormalizationFixtureScale + kNormalizationFixtureOffset +
                    localNormalizationFixtureGradient(x, y, width, height, gradient));
            }
        }
    }
    return frames;
}

/// The coordinate a node of the apply fixture's grid stands for along one axis.
///
/// The fractional position the engine interpolates a node at: spread evenly from the
/// first coordinate to the last. Sampling the planes there, rather than at the nearest pixel, is
/// what makes an interior interpolation reproduce the plane to rounding.
///
/// @param node the node index along the axis.
/// @param gridSize how many cells the axis has.
/// @param extent how many pixels the axis has.
/// @return the coordinate.
[[nodiscard]] inline double localNormalizationFixtureNode(std::uint32_t node, std::uint32_t gridSize,
                                                          std::uint32_t extent) noexcept
{
    if (extent <= 1u)
    {
        return 0.0;
    }
    return static_cast<double>(node) / static_cast<double>(gridSize) * (static_cast<double>(extent) - 1.0);
}

/// The plane the apply fixture's target grid describes.
///
/// @param x the column.
/// @param y the row.
/// @param width the frame width.
/// @param height the frame height.
/// @return the level there.
[[nodiscard]] inline double localNormalizationFixtureTargetPlane(double x, double y, std::uint32_t width,
                                                                 std::uint32_t height) noexcept
{
    return 0.03125 + 0.015625 * x / (std::max)(1.0, static_cast<double>(width) - 1.0) +
           0.0078125 * y / (std::max)(1.0, static_cast<double>(height) - 1.0);
}

/// The plane the apply fixture's reference grid describes.
///
/// @param x the column.
/// @param y the row.
/// @param width the frame width.
/// @param height the frame height.
/// @return the level there.
[[nodiscard]] inline double localNormalizationFixtureReferencePlane(double x, double y, std::uint32_t width,
                                                                    std::uint32_t height) noexcept
{
    return 0.046875 - 0.01171875 * x / (std::max)(1.0, static_cast<double>(width) - 1.0) +
           0.00390625 * y / (std::max)(1.0, static_cast<double>(height) - 1.0);
}

/// One node grid of the apply fixture, node-row-major with `gridSize + 1` nodes a side.
///
/// A plane sampled at the nodes, so that the engine's interpolation reproduces the plane
/// everywhere and the row has a closed-form answer.
///
/// @param target whether to sample the target plane or the reference plane.
/// @param gridSize how many cells a side.
/// @param width the frame width.
/// @param height the frame height.
/// @return the node values.
[[nodiscard]] inline std::vector<double> makeLocalNormalizationFixtureGrid(bool target, std::uint32_t gridSize,
                                                                           std::uint32_t width, std::uint32_t height)
{
    const std::uint32_t nodes = gridSize + 1u;
    std::vector<double> values(static_cast<std::size_t>(nodes) * nodes, 0.0);
    for (std::uint32_t ny = 0u; ny < nodes; ++ny)
    {
        const double y = localNormalizationFixtureNode(ny, gridSize, height);
        for (std::uint32_t nx = 0u; nx < nodes; ++nx)
        {
            const double x = localNormalizationFixtureNode(nx, gridSize, width);
            values[static_cast<std::size_t>(ny) * nodes + nx] =
                target ? localNormalizationFixtureTargetPlane(x, y, width, height)
                       : localNormalizationFixtureReferencePlane(x, y, width, height);
        }
    }
    return values;
}

/// How far a locally applied frame is from the closed form the plane grids define.
///
/// Every non-zero source sample should have become `scale * (sample - target) + reference` with
/// the two planes evaluated at its position; every zero source sample should still be zero. The
/// largest absolute distance is reported, and the largest finite double for a structural failure.
///
/// @param normalized the frame after the apply.
/// @param source the frame before it.
/// @param width the frame width.
/// @param height the frame height.
/// @param scale the factor the row applied.
/// @return the error, or the largest finite double for a structural failure.
[[nodiscard]] inline double localNormalizationApplyError(std::span<const float> normalized,
                                                         std::span<const float> source, std::uint32_t width,
                                                         std::uint32_t height, double scale) noexcept
{
    if (normalized.size() != source.size() || source.size() != static_cast<std::size_t>(width) * height)
    {
        return (std::numeric_limits<double>::max)();
    }
    double worst = 0.0;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const float before = source[index];
            const float after = normalized[index];
            if (before == 0.0f)
            {
                if (after != 0.0f)
                {
                    return (std::numeric_limits<double>::max)();
                }
                continue;
            }
            if (!std::isfinite(after))
            {
                return (std::numeric_limits<double>::max)();
            }
            const double expected =
                scale * (static_cast<double>(before) - localNormalizationFixtureTargetPlane(x, y, width, height)) +
                localNormalizationFixtureReferencePlane(x, y, width, height);
            const double error = std::abs(static_cast<double>(after) - expected);
            worst = error > worst ? error : worst;
        }
    }
    return worst;
}

} // namespace an::bench
