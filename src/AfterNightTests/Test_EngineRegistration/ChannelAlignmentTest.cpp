// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Per-channel alignment, spelled as a composition.
//
// Correcting lateral chromatic aberration over an RGB image takes two steps: detect stars on each
// channel, keep the stars away from the centre, match red and blue against green and fit a
// similarity to each; then warp the two channels. Here that answer is the star detector on each
// plane, a caller-side edge filter, the star-field alignment of each colour plane against the green
// one, and the registration warp of the two planes onto the green frame - each already tested on its
// own. These cases prove that the composition brings a planted misalignment back under a tenth of a
// pixel, so no facade has to be written for it.

#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Algorithms/Stars/StarAlignment.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include "RegistrationTestSupport.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

namespace
{

using registration_test::makeEngine;
using registration_test::makeExecution;

constexpr std::uint32_t kWidth = 512u;
constexpr std::uint32_t kHeight = 512u;
constexpr double kBackground = 0.05;
constexpr double kNoise = 0.0012;

/// The share of the frame's half-diagonal inside which stars are left out of the fit, because
/// lateral colour grows with the distance from the optical axis and the centre carries no signal
/// about it. A sensor-scale frame would take 0.55; this small frame keeps 0.4 so the ring outside it
/// still holds enough stars for triangle matching.
constexpr double kEdgeFraction = 0.4;

/// One planted star: a round Gaussian at a sub-pixel position.
struct PlantedStar
{
    double x = 0.0;
    double y = 0.0;
    double amplitude = 0.0;
    double sigma = 0.0;
};

/// One integer hash, so the field is the same field on every platform.
[[nodiscard]] std::uint64_t mix(std::uint64_t value) noexcept
{
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return value;
}

[[nodiscard]] double unit(const std::uint64_t value) noexcept
{
    return static_cast<double>(mix(value) >> 11) / static_cast<double>(1ull << 53);
}

/// One star per cell, jittered inside the middle half of it, with a wide clear margin so every star
/// keeps its whole profile inside the frame after the planted misalignment and the warp.
[[nodiscard]] std::vector<PlantedStar> plantStars(const std::uint64_t seed)
{
    constexpr std::uint32_t cell = 40u;
    constexpr double margin = 40.0;
    std::vector<PlantedStar> stars;
    for (std::uint32_t cellY = 0u; cellY < kHeight / cell; ++cellY)
    {
        for (std::uint32_t cellX = 0u; cellX < kWidth / cell; ++cellX)
        {
            const std::uint64_t key =
                seed ^ (static_cast<std::uint64_t>(cellX) << 3u) ^ (static_cast<std::uint64_t>(cellY) << 24u);
            PlantedStar star;
            star.x = (static_cast<double>(cellX) + 0.25 + 0.5 * unit(key + 1u)) * cell;
            star.y = (static_cast<double>(cellY) + 0.25 + 0.5 * unit(key + 2u)) * cell;
            if (star.x < margin || star.y < margin || star.x > kWidth - margin || star.y > kHeight - margin)
            {
                continue;
            }
            star.amplitude = 0.12 + 0.48 * unit(key + 3u);
            star.sigma = 1.5 + 0.7 * unit(key + 4u);
            stars.push_back(star);
        }
    }
    return stars;
}

/// A similarity the planted red plane is drawn with: a rotation about the frame centre, a scale
/// and a shift, all in the direction green-to-red.
struct PlantedSimilarity
{
    double rotationDegrees = 0.0;
    double scale = 1.0;
    double shiftX = 0.0;
    double shiftY = 0.0;

    [[nodiscard]] PlantedStar apply(const PlantedStar& star) const
    {
        const double angle = rotationDegrees * std::numbers::pi / 180.0;
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        const double centreX = 0.5 * kWidth;
        const double centreY = 0.5 * kHeight;
        const double offsetX = star.x - centreX;
        const double offsetY = star.y - centreY;
        PlantedStar moved = star;
        moved.x = centreX + scale * (cosine * offsetX - sine * offsetY) + shiftX;
        moved.y = centreY + scale * (sine * offsetX + cosine * offsetY) + shiftY;
        return moved;
    }
};

/// Renders stars onto a flat sky with a little deterministic noise, sampling every profile at the
/// pixel's centre, which is the convention the detector measures in.
[[nodiscard]] std::vector<float> render(std::span<const PlantedStar> stars, const std::uint64_t noiseSeed)
{
    std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight, static_cast<float>(kBackground));
    for (const PlantedStar& star : stars)
    {
        const double reach = 5.0 * star.sigma;
        const auto left = static_cast<std::int64_t>(std::floor(star.x - reach));
        const auto right = static_cast<std::int64_t>(std::ceil(star.x + reach));
        const auto top = static_cast<std::int64_t>(std::floor(star.y - reach));
        const auto bottom = static_cast<std::int64_t>(std::ceil(star.y + reach));
        const double inverse = 1.0 / (2.0 * star.sigma * star.sigma);
        for (std::int64_t row = (std::max)(std::int64_t{0}, top);
             row <= (std::min)(static_cast<std::int64_t>(kHeight) - 1, bottom); ++row)
        {
            for (std::int64_t column = (std::max)(std::int64_t{0}, left);
                 column <= (std::min)(static_cast<std::int64_t>(kWidth) - 1, right); ++column)
            {
                const double offsetX = static_cast<double>(column) + 0.5 - star.x;
                const double offsetY = static_cast<double>(row) + 0.5 - star.y;
                const double value = star.amplitude * std::exp(-(offsetX * offsetX + offsetY * offsetY) * inverse);
                samples[static_cast<std::size_t>(row) * kWidth + static_cast<std::size_t>(column)] +=
                    static_cast<float>(value);
            }
        }
    }
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        double sum = 0.0;
        for (std::uint64_t draw = 0u; draw < 12u; ++draw)
        {
            sum += unit((noiseSeed * 1000003u + index) * 12u + draw);
        }
        samples[index] += static_cast<float>(kNoise * (sum - 6.0));
    }
    return samples;
}

/// The detector over one plane, in the order it returns stars: brightest first.
[[nodiscard]] std::vector<an::DetectedStar> detect(const an::ExecutionContext& execution,
                                                   const std::vector<float>& samples,
                                                   const an::ImageDescriptor& descriptor, const an::ImageLayout& layout)
{
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(samples.data(), layout.totalBytes(), layout, descriptor);
    EXPECT_TRUE(frame.hasValue()) << frame.status().message();
    const an::Result<an::StarDetectionPlan> plan =
        an::planStarDetection(an::StarDetectionRequest{}, descriptor, layout);
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    an::StarDetectionResult detected;
    if (frame.hasValue() && plan.hasValue())
    {
        const an::Status ran = an::detectStars(execution, *frame, *plan, detected);
        EXPECT_TRUE(ran.ok()) << ran.message();
    }
    return std::move(detected.stars);
}

/// The edge filter: only stars at least the stated share of the half-diagonal from the frame centre
/// take part in the fit.
[[nodiscard]] std::vector<an::StarAlignmentPoint> edgePoints(std::span<const an::DetectedStar> stars)
{
    const double centreX = 0.5 * kWidth;
    const double centreY = 0.5 * kHeight;
    const double minimumRadius = kEdgeFraction * std::hypot(centreX, centreY);
    std::vector<an::StarAlignmentPoint> points;
    for (const an::DetectedStar& star : stars)
    {
        if (std::hypot(star.positionX - centreX, star.positionY - centreY) < minimumRadius)
        {
            continue;
        }
        points.push_back({.x = star.positionX, .y = star.positionY});
    }
    return points;
}

/// How far each green star sits from the nearest star of another plane, over the stars that have
/// one within `radius`; the pair count says how many did.
struct Coincidence
{
    std::size_t paired = 0u;
    double rootMeanSquare = 0.0;
    double worst = 0.0;
};

[[nodiscard]] Coincidence coincidence(std::span<const an::DetectedStar> green, std::span<const an::DetectedStar> other,
                                      const double radius)
{
    Coincidence answer;
    double squares = 0.0;
    for (const an::DetectedStar& star : green)
    {
        double best = radius;
        for (const an::DetectedStar& candidate : other)
        {
            best = (std::min)(best,
                              std::hypot(candidate.positionX - star.positionX, candidate.positionY - star.positionY));
        }
        if (best < radius)
        {
            ++answer.paired;
            squares += best * best;
            answer.worst = (std::max)(answer.worst, best);
        }
    }
    answer.rootMeanSquare = answer.paired == 0u ? 0.0 : std::sqrt(squares / static_cast<double>(answer.paired));
    return answer;
}

/// The whole composition for one colour plane: detect, filter, align against the green points, and
/// warp the plane onto the green frame through the fitted similarity.
struct CorrectedPlane
{
    an::StarAlignmentResult alignment{};
    an::RegistrationTransform transform{};
    std::vector<float> samples{};
    std::vector<an::DetectedStar> stars{};
};

[[nodiscard]] CorrectedPlane correctPlane(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                          const std::vector<float>& plane,
                                          std::span<const an::StarAlignmentPoint> green,
                                          const an::ImageDescriptor& descriptor, const an::ImageLayout& layout)
{
    CorrectedPlane corrected;
    const std::vector<an::DetectedStar> detected = detect(execution, plane, descriptor, layout);
    const std::vector<an::StarAlignmentPoint> points = edgePoints(detected);
    EXPECT_GE(points.size(), 6u);

    an::StarAlignmentRequest request;
    request.model = an::StarTransformModel::Similarity;
    request.minimumMatches = 6u;
    const an::Result<an::StarAlignmentResult> aligned = an::alignStarFields(execution, points, green, request);
    EXPECT_TRUE(aligned.hasValue()) << aligned.status().message();
    if (!aligned.hasValue())
    {
        return corrected;
    }
    corrected.alignment = *aligned;

    const an::Result<an::RegistrationTransform> transform =
        an::RegistrationTransform::create({.linearKind = an::RegistrationTransformKind::Similarity,
                                           .sourceToReference = aligned->transform.sourceToReference});
    EXPECT_TRUE(transform.hasValue()) << transform.status().message();
    if (!transform.hasValue())
    {
        return corrected;
    }
    corrected.transform = *transform;

    const an::Result<an::RegistrationWarpPlan> planned =
        an::planRegistrationWarp(descriptor, *transform, {}, {.width = kWidth, .height = kHeight});
    EXPECT_TRUE(planned.hasValue()) << planned.status().message();
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, layout.extent());
    EXPECT_TRUE(mask.hasValue()) << mask.status().message();
    if (!planned.hasValue() || !mask.hasValue())
    {
        return corrected;
    }
    const std::optional<an::ValidityMaskView<std::uint8_t>> maskView = mask->view();
    EXPECT_TRUE(maskView.has_value());
    corrected.samples.assign(plane.size(), 0.0f);
    const an::Result<an::RegistrationWarpResult> warped =
        an::warpRegisteredImage(execution, an::SampleType::Float32, plane.data(), layout, corrected.samples.data(),
                                layout, *planned, {}, *maskView);
    EXPECT_TRUE(warped.hasValue()) << warped.status().message();
    corrected.stars = detect(execution, corrected.samples, descriptor, layout);
    return corrected;
}

} // namespace

TEST(ChannelAlignmentTest, RedAndBlueAreBroughtOntoGreenByDetectionAlignmentAndTheWarp)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor descriptor = registration_test::descriptorOf(kWidth, kHeight, 1u, an::SampleType::Float32,
                                                                           an::NumericDomain::NormalizedLinear);
    const an::ImageLayout layout = registration_test::layoutOf(descriptor);

    // Green is the frame the others are brought to. Red carries a rotation, a magnification and a
    // shift; blue carries a shift only, which is the common shape of lateral colour on a refractor.
    const std::vector<PlantedStar> stars = plantStars(0x5eedu);
    ASSERT_GE(stars.size(), 40u);
    const PlantedSimilarity redMotion{.rotationDegrees = 0.25, .scale = 1.001, .shiftX = 1.6, .shiftY = -0.9};
    const PlantedSimilarity blueMotion{.rotationDegrees = 0.0, .scale = 1.0, .shiftX = -1.2, .shiftY = 0.7};
    std::vector<PlantedStar> redStars;
    std::vector<PlantedStar> blueStars;
    for (const PlantedStar& star : stars)
    {
        redStars.push_back(redMotion.apply(star));
        blueStars.push_back(blueMotion.apply(star));
    }
    const std::vector<float> green = render(stars, 1u);
    const std::vector<float> red = render(redStars, 2u);
    const std::vector<float> blue = render(blueStars, 3u);

    const std::vector<an::DetectedStar> greenStars = detect(execution, green, descriptor, layout);
    ASSERT_GE(greenStars.size(), 36u);
    const std::vector<an::StarAlignmentPoint> greenPoints = edgePoints(greenStars);
    ASSERT_GE(greenPoints.size(), 24u);

    // Before the correction the planted motion is visible: every red star is more than a pixel from
    // the green star it belongs to.
    const std::vector<an::DetectedStar> rawRed = detect(execution, red, descriptor, layout);
    const Coincidence before = coincidence(greenStars, rawRed, 4.0);
    ASSERT_GE(before.paired, greenStars.size() * 9u / 10u);
    EXPECT_GT(before.rootMeanSquare, 1.0);

    const CorrectedPlane correctedRed = correctPlane(engine, execution, red, greenPoints, descriptor, layout);
    ASSERT_FALSE(correctedRed.samples.empty());
    EXPECT_GE(correctedRed.alignment.quality.inlierMatches, 6u);
    EXPECT_LT(correctedRed.alignment.quality.rootMeanSquareResidual, 0.15);
    // The fit runs red-to-green, so it reads as the planted motion undone.
    EXPECT_NEAR(correctedRed.transform.rotationDegrees(), -redMotion.rotationDegrees, 0.02);
    EXPECT_NEAR(correctedRed.transform.scale(), 1.0 / redMotion.scale, 5.0e-4);

    const Coincidence afterRed = coincidence(greenStars, correctedRed.stars, 1.0);
    EXPECT_GE(afterRed.paired, greenStars.size() * 9u / 10u);
    EXPECT_LT(afterRed.rootMeanSquare, 0.1);
    EXPECT_LT(afterRed.worst, 0.35);

    const CorrectedPlane correctedBlue = correctPlane(engine, execution, blue, greenPoints, descriptor, layout);
    ASSERT_FALSE(correctedBlue.samples.empty());
    EXPECT_GE(correctedBlue.alignment.quality.inlierMatches, 6u);
    // A pure shift comes back as one: the origin maps to the shift undone and nothing turns.
    const an::Result<an::PixelCoordinate> origin = an::mapRegistrationPoint(correctedBlue.transform, {});
    ASSERT_TRUE(origin.hasValue()) << origin.status().message();
    EXPECT_NEAR(origin->x, -blueMotion.shiftX, 0.05);
    EXPECT_NEAR(origin->y, -blueMotion.shiftY, 0.05);
    EXPECT_NEAR(correctedBlue.transform.rotationDegrees(), 0.0, 0.02);

    const Coincidence afterBlue = coincidence(greenStars, correctedBlue.stars, 1.0);
    EXPECT_GE(afterBlue.paired, greenStars.size() * 9u / 10u);
    EXPECT_LT(afterBlue.rootMeanSquare, 0.1);
}

TEST(ChannelAlignmentTest, TooFewEdgeStarsIsAReturnedRefusalRatherThanAnIdentityCorrection)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    // An unfittable channel is not answered with an identity transform and a warning: the alignment
    // answers with a failure the caller reads, and the caller decides to leave the plane.
    const std::vector<an::StarAlignmentPoint> few{{.x = 40.0, .y = 40.0}, {.x = 400.0, .y = 60.0}};
    const std::vector<an::StarAlignmentPoint> green{{.x = 41.0, .y = 40.0},   {.x = 401.0, .y = 60.0},
                                                    {.x = 200.0, .y = 300.0}, {.x = 300.0, .y = 420.0},
                                                    {.x = 100.0, .y = 250.0}, {.x = 450.0, .y = 450.0}};
    an::StarAlignmentRequest request;
    request.model = an::StarTransformModel::Similarity;
    request.minimumMatches = 6u;
    const an::Result<an::StarAlignmentResult> aligned = an::alignStarFields(execution, few, green, request);
    EXPECT_FALSE(aligned.hasValue());
}
