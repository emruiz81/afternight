// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - shared moving-comet detection fixture and truth scoring.
//
// The runner consumes these exact planes. Static stars exercise temporal-background rejection, a
// different single-pixel impulse in each frame exercises component-area rejection, and the only
// persistent residual source follows a straight track. Coordinates in the fixture use the convention
// in which the first sample centre is 0.5; the score accepts an explicit reporting offset so an answer
// in integer-centred coordinates is compared with the same sky positions rather than mistaken for a
// half-pixel error.

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

/// Width of every moving-comet fixture frame, in pixels.
inline constexpr std::uint32_t kCometDetectionFixtureWidth = 192u;

/// Height of every moving-comet fixture frame, in pixels.
inline constexpr std::uint32_t kCometDetectionFixtureHeight = 128u;

/// Number of equally spaced exposures in the fixture.
inline constexpr std::uint32_t kCometDetectionFixtureFrameCount = 7u;

/// Stable name of the engine-neutral pixel and truth mapping.
inline constexpr const char* kCometDetectionFixtureMapping = "moving-comet-linear-v1";

/// One truth or reported point in a moving-object sequence.
struct CometDetectionFixturePoint
{
    /// Stable frame identity.
    std::uint64_t frameId = 0u;

    /// Horizontal centre in pixels.
    double x = 0.0;

    /// Vertical centre in pixels.
    double y = 0.0;
};

/// Complete moving-comet input and its planted track.
struct CometDetectionFixture
{
    /// Tight normalized Float32 planes in exposure order.
    std::vector<std::vector<float>> frames{};

    /// Planted centres in the engine's pixel convention.
    std::vector<CometDetectionFixturePoint> truth{};
};

/// Builds the deterministic static-star, impulse and moving-source sequence.
///
/// @param seed scenario seed used only to phase the bounded background texture.
/// @return seven tight normalized Float32 planes and the planted linear track.
[[nodiscard]] inline CometDetectionFixture makeCometDetectionFixture(std::uint64_t seed)
{
    constexpr double background = 0.08;
    constexpr double movingAmplitude = 0.72;
    constexpr double movingSigma = 1.25;
    constexpr std::size_t pixels = static_cast<std::size_t>(kCometDetectionFixtureWidth) * kCometDetectionFixtureHeight;

    CometDetectionFixture fixture;
    fixture.frames.assign(kCometDetectionFixtureFrameCount, std::vector<float>(pixels, 0.0F));
    fixture.truth.reserve(kCometDetectionFixtureFrameCount);

    const auto addGaussian = [](std::vector<float>& plane, const double centerX, const double centerY,
                                const double amplitude, const double sigma) {
        const double denominator = 2.0 * sigma * sigma;
        for (std::uint32_t y = 0u; y < kCometDetectionFixtureHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kCometDetectionFixtureWidth; ++x)
            {
                const double dx = static_cast<double>(x) + 0.5 - centerX;
                const double dy = static_cast<double>(y) + 0.5 - centerY;
                plane[static_cast<std::size_t>(y) * kCometDetectionFixtureWidth + x] +=
                    static_cast<float>(amplitude * std::exp(-(dx * dx + dy * dy) / denominator));
            }
        }
    };

    for (std::uint32_t frame = 0u; frame < kCometDetectionFixtureFrameCount; ++frame)
    {
        std::vector<float>& plane = fixture.frames[frame];
        for (std::size_t index = 0u; index < plane.size(); ++index)
        {
            const std::uint64_t code =
                (static_cast<std::uint64_t>(index) * 37u + static_cast<std::uint64_t>(frame) * 19u + seed * 11u) % 29u;
            const double texture = (static_cast<double>(code) - 14.0) * 0.00008;
            plane[index] = static_cast<float>(background + texture);
        }

        addGaussian(plane, 24.5, 22.5, 0.58, 1.35);
        addGaussian(plane, 151.5, 34.5, 0.51, 1.55);
        addGaussian(plane, 112.5, 105.5, 0.63, 1.20);

        const double cometX = 40.5 + 8.0 * static_cast<double>(frame);
        const double cometY = 96.5 - 5.0 * static_cast<double>(frame);
        addGaussian(plane, cometX, cometY, movingAmplitude, movingSigma);
        fixture.truth.push_back({.frameId = 1000u + frame, .x = cometX, .y = cometY});

        const std::uint32_t hotX = 177u - frame * 3u;
        const std::uint32_t hotY = 8u + frame * 7u;
        plane[static_cast<std::size_t>(hotY) * kCometDetectionFixtureWidth + hotX] = 0.98F;
    }
    return fixture;
}

/// Scores a reported track against the planted source.
///
/// @param fixture the input and truth used for the run.
/// @param reported at most one reported centre per frame.
/// @param coordinateOffset value added to both reported coordinates before comparison; 0.5 for an
///        answer in integer-centred coordinates, zero for the engine's own.
/// @return root-mean-square positional error in pixels, or the largest finite double when any truth
///         frame is absent or an answer is non-finite.
[[nodiscard]] inline double cometDetectionRootMeanSquare(const CometDetectionFixture& fixture,
                                                         std::span<const CometDetectionFixturePoint> reported,
                                                         double coordinateOffset) noexcept
{
    if (fixture.truth.empty() || !std::isfinite(coordinateOffset))
    {
        return (std::numeric_limits<double>::max)();
    }

    double squares = 0.0;
    for (const CometDetectionFixturePoint& expected : fixture.truth)
    {
        const auto found = std::find_if(reported.begin(), reported.end(), [&](const CometDetectionFixturePoint& point) {
            return point.frameId == expected.frameId;
        });
        if (found == reported.end() || !std::isfinite(found->x) || !std::isfinite(found->y))
        {
            return (std::numeric_limits<double>::max)();
        }
        const double dx = found->x + coordinateOffset - expected.x;
        const double dy = found->y + coordinateOffset - expected.y;
        squares += dx * dx + dy * dy;
    }
    return std::sqrt(squares / static_cast<double>(fixture.truth.size()));
}

} // namespace an::bench
