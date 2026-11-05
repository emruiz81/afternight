// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Phase correlation against the imaging library's `cv::phaseCorrelate`, called after subtracting each
// frame's mean and under a Hann window.
//
// A phase correlation measures a cyclic shift exactly, so the sweep's first fixture is one: white
// periodic noise band-limited just short of the Nyquist frequency, synthesised in the frequency
// domain and shifted there by its exact phase ramp, so the translation is known to the last digit
// and every bin of the whitened spectrum carries energy. Neither side windows that fixture,
// because a window makes a cyclic shift no longer one; both must recover it to within their
// five-by-five centroid refinement and agree with each other closely, since they compute the same
// refinement of the same surface. The second fixture is a star field whose stars move within a
// fixed frame under a Hann window - what a registration actually sees -
// and there the two sides are held to the whole pixel and reported to the sub-pixel, because the
// frame's edges are not a shift. The sign the library reports is established from the truth rather
// than assumed, and both responses are reported: each defines it as the five-by-five sum over the
// surface's pixel count.

#include "ParityHarness.h"

#include <AfterNightEngine/Algorithms/Registration/PhaseCorrelation.h>

#include <opencv2/imgproc.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace
{

using namespace parity_test;

constexpr const char* kDomain = "registration";

/// Band-limited white periodic noise in [0, 1], shifted cyclically by an exact phase ramp.
///
/// Every bin below nine tenths of the Nyquist frequency on each axis carries unit magnitude and a
/// seeded phase, with the conjugate symmetry a real field needs; the zero-frequency bin and the
/// Nyquist bins are left empty. The shift multiplies each bin by its phase ramp before the inverse
/// transform, which is the translation itself rather than an interpolation of it.
[[nodiscard]] std::vector<float> cyclicPlane(PlaneSize size, std::uint64_t seed, double shiftX, double shiftY)
{
    const int width = static_cast<int>(size.width);
    const int height = static_cast<int>(size.height);
    Sequence sequence{seed};
    cv::Mat spectrum = cv::Mat::zeros(height, width, CV_32FC2);
    const int limitX = static_cast<int>(std::floor(0.9 * (width / 2.0)));
    const int limitY = static_cast<int>(std::floor(0.9 * (height / 2.0)));
    for (int ky = -limitY; ky <= limitY; ++ky)
    {
        for (int kx = -limitX; kx <= limitX; ++kx)
        {
            // Each conjugate pair is visited once: the bin whose (ky, kx) is lexicographically
            // positive draws the phase and its mirror takes the conjugate.
            if (ky < 0 || (ky == 0 && kx < 0) || (ky == 0 && kx == 0))
            {
                continue;
            }
            const double phase = 6.283185307179586 * sequence.nextDouble();
            const double ramp = -6.283185307179586 * (kx * shiftX / width + ky * shiftY / height);
            const float real = static_cast<float>(std::cos(phase + ramp));
            const float imaginary = static_cast<float>(std::sin(phase + ramp));
            const int row = (ky + height) % height;
            const int column = (kx + width) % width;
            const int mirrorRow = (height - ky) % height;
            const int mirrorColumn = (width - kx) % width;
            spectrum.at<cv::Vec2f>(row, column) = cv::Vec2f(real, imaginary);
            spectrum.at<cv::Vec2f>(mirrorRow, mirrorColumn) = cv::Vec2f(real, -imaginary);
        }
    }
    cv::Mat field;
    cv::dft(spectrum, field, cv::DFT_INVERSE | cv::DFT_REAL_OUTPUT | cv::DFT_SCALE);
    double lowest = 0.0;
    double highest = 0.0;
    cv::minMaxLoc(field, &lowest, &highest);
    std::vector<float> samples(static_cast<std::size_t>(size.width) * size.height, 0.0f);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            samples[static_cast<std::size_t>(y) * size.width + x] =
                static_cast<float>((field.at<float>(y, x) - lowest) / (highest - lowest));
        }
    }
    return samples;
}

/// The mean is subtracted before the frame is handed to the library, which itself subtracts
/// nothing, so the sweep compares the same operation on both sides.
[[nodiscard]] cv::Mat centred(std::span<const float> samples, PlaneSize size)
{
    cv::Mat matrix = matrixOf(samples, size.width, size.height);
    const cv::Scalar mean = cv::mean(matrix);
    matrix -= mean[0];
    return matrix;
}

struct Estimates
{
    double engineX;
    double engineY;
    double engineResponse;
    double libraryX;
    double libraryY;
    double libraryResponse;
    double sign;
};

/// Runs both sides over one pair, with or without the Hann window on both, and establishes the
/// library's sign from the truth.
[[nodiscard]] Estimates estimate(const an::ExecutionContext& execution, std::span<const float> reference,
                                 std::span<const float> target, PlaneSize size, double truthX, double truthY,
                                 bool windowed)
{
    Estimates found{};
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, size.width, size.height);
    const an::PhaseCorrelationRequest request{
        .subtractMean = true, .window = windowed ? an::PhaseCorrelationWindow::Hann : an::PhaseCorrelationWindow::None};
    const an::Result<an::PhaseCorrelationResult> ours = an::computeImagePhaseCorrelation(
        execution, an::SampleType::Float32, reference.data(), layout, target.data(), layout, request);
    EXPECT_TRUE(ours.hasValue()) << ours.status().message();
    if (ours.hasValue())
    {
        found.engineX = ours->translationX;
        found.engineY = ours->translationY;
        found.engineResponse = ours->response;
    }
    cv::Mat window;
    if (windowed)
    {
        cv::createHanningWindow(window, cv::Size(static_cast<int>(size.width), static_cast<int>(size.height)), CV_32F);
    }
    double response = 0.0;
    const cv::Point2d shift = cv::phaseCorrelate(centred(target, size), centred(reference, size), window, &response);
    const double plusDistance = std::hypot(shift.x - truthX, shift.y - truthY);
    const double minusDistance = std::hypot(-shift.x - truthX, -shift.y - truthY);
    found.sign = plusDistance <= minusDistance ? 1.0 : -1.0;
    found.libraryX = found.sign * shift.x;
    found.libraryY = found.sign * shift.y;
    found.libraryResponse = response;
    return found;
}

void reportEstimates(const std::string& label, const Estimates& found, double truthX, double truthY)
{
    std::cout << "parity | " << kDomain << " | " << label << " | engine (" << found.engineX << ", " << found.engineY
              << ") response " << found.engineResponse << " | library (" << found.libraryX << ", " << found.libraryY
              << ") response " << found.libraryResponse << " | truth (" << truthX << ", " << truthY << ") | sign "
              << found.sign << " | engine-truth " << std::hypot(found.engineX - truthX, found.engineY - truthY)
              << " | library-truth " << std::hypot(found.libraryX - truthX, found.libraryY - truthY)
              << " | engine-library " << std::hypot(found.engineX - found.libraryX, found.engineY - found.libraryY)
              << "\n";
}

constexpr std::array<PlaneSize, 3u> kSizes{{{64u, 48u}, {128u, 128u}, {100u, 70u}}};
constexpr std::array<std::array<double, 2u>, 5u> kShifts{
    {{0.0, 0.0}, {3.0, -2.0}, {0.5, 0.25}, {-7.3, 4.6}, {12.75, -0.125}}};

TEST(RegistrationParityTest, ACyclicShiftIsRecoveredByBothSidesAndTheyAgree)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // A whole-pixel shift is a delta on both sides and comes back to a few hundredths; a sub-pixel
    // one is a band-limited peak whose five-by-five centroid is biased by up to a third of a pixel,
    // on both sides. A shift of exactly half a pixel on an axis is a tie there: the samples either
    // side of the peak are equally high, rounding - which differs between processors - picks the
    // integer peak, and each side leans towards its own pick, so on that axis the two can differ
    // by both biases.
    constexpr double kWholeAgainstTruth = 0.1;
    constexpr double kSubPixelAgainstTruth = 0.5;
    constexpr double kAgainstLibrary = 0.5;
    constexpr double kTiedAgainstLibrary = 2.0 / 3.0;
    for (const PlaneSize size : kSizes)
    {
        for (const std::array<double, 2u>& shift : kShifts)
        {
            const std::vector<float> reference = cyclicPlane(size, 9000u + size.width, 0.0, 0.0);
            const std::vector<float> target = cyclicPlane(size, 9000u + size.width, shift[0], shift[1]);
            // This engine reports the translation that maps the target back onto the reference.
            const double truthX = -shift[0];
            const double truthY = -shift[1];
            const Estimates found = estimate(execution, reference, target, size, truthX, truthY, false);
            const std::string label = "cyclic " + sizeLabel(size) + " shift (" + std::to_string(shift[0]) + ", " +
                                      std::to_string(shift[1]) + ")";
            reportEstimates(label, found, truthX, truthY);
            const bool whole = shift[0] == std::floor(shift[0]) && shift[1] == std::floor(shift[1]);
            const double againstTruth = whole ? kWholeAgainstTruth : kSubPixelAgainstTruth;
            EXPECT_NEAR(found.engineX, truthX, againstTruth) << label;
            EXPECT_NEAR(found.engineY, truthY, againstTruth) << label;
            EXPECT_NEAR(found.libraryX, truthX, againstTruth) << label << " (library)";
            EXPECT_NEAR(found.libraryY, truthY, againstTruth) << label << " (library)";
            const bool tiedX = shift[0] - std::floor(shift[0]) == 0.5;
            const bool tiedY = shift[1] - std::floor(shift[1]) == 0.5;
            EXPECT_NEAR(found.engineX, found.libraryX, tiedX ? kTiedAgainstLibrary : kAgainstLibrary) << label;
            EXPECT_NEAR(found.engineY, found.libraryY, tiedY ? kTiedAgainstLibrary : kAgainstLibrary) << label;
        }
    }
}

TEST(RegistrationParityTest, AStarFieldMovingInsideItsFrameIsHeldToThePixelAndReportedToTheSubPixel)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    for (const PlaneSize size : kSizes)
    {
        for (const std::array<double, 2u>& shift : kShifts)
        {
            const std::vector<float> reference = smoothPlane(size.width, size.height, 9500u + size.width);
            const std::vector<float> target =
                smoothPlane(size.width, size.height, 9500u + size.width, shift[0], shift[1]);
            const double truthX = -shift[0];
            const double truthY = -shift[1];
            const Estimates found = estimate(execution, reference, target, size, truthX, truthY, true);
            const std::string label = "stars " + sizeLabel(size) + " shift (" + std::to_string(shift[0]) + ", " +
                                      std::to_string(shift[1]) + ")";
            reportEstimates(label, found, truthX, truthY);
            EXPECT_NEAR(found.engineX, truthX, 0.5) << label;
            EXPECT_NEAR(found.engineY, truthY, 0.5) << label;
            EXPECT_NEAR(found.libraryX, truthX, 0.5) << label << " (library)";
            EXPECT_NEAR(found.libraryY, truthY, 0.5) << label << " (library)";
        }
    }
}

} // namespace
