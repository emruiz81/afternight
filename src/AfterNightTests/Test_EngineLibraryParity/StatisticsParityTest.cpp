// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Statistics and background fitting against the scientific library: means and medians from GSL's
// statistics routines and background surfaces fitted with `gsl_multifit_linear`; this engine computes
// both itself.
//
// A mean, a population variance, a median and a median absolute deviation are defined numbers with
// no approximation on either side, so they agree to double-precision rounding, with the variance
// allowed the difference between a two-pass formula and a compensated one. A polynomial fit is the
// least-squares solution of one system, so two solvers agree on the surface it describes to the
// conditioning of that system; the sweep compares surfaces rather than coefficients, because the two
// sides are free to choose their own basis.

#include "ParityHarness.h"

#include <AfterNightEngine/Algorithms/Background/BackgroundModel.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Image/PlaneView.h>

#include <gsl/gsl_multifit.h>
#include <gsl/gsl_sort.h>
#include <gsl/gsl_statistics.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace
{

using namespace parity_test;

constexpr const char* kDomain = "statistics";

void expectClose(const std::string& label, double engine, double library, double tolAbs, double tolRel)
{
    const double allowance = tolAbs + tolRel * std::abs(library);
    std::cout << "parity | " << kDomain << " | " << label << " | engine " << engine << " | library " << library
              << " | diff " << std::abs(engine - library) << " | tol " << allowance
              << (std::abs(engine - library) > allowance ? " | FAIL" : "") << "\n";
    EXPECT_NEAR(engine, library, allowance) << label;
}

TEST(StatisticsParityTest, MomentsAndMediansAgreeWithTheLibraryToDoublePrecisionRounding)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::array<PlaneSize, 6u> kSizes{{{1u, 1u}, {2u, 1u}, {7u, 5u}, {64u, 48u}, {127u, 61u}, {256u, 192u}}};
    for (const PlaneSize size : kSizes)
    {
        // Three uniforms summed give a bell-shaped plane; the odd sizes give an odd count and the
        // even ones the average of two middle values.
        Sequence sequence{11000u + size.width};
        std::vector<float> samples(static_cast<std::size_t>(size.width) * size.height, 0.0f);
        for (float& sample : samples)
        {
            sample = (sequence.next() + sequence.next() + sequence.next()) / 3.0f;
        }
        const an::ImageLayout layout = planarLayout(an::SampleType::Float32, size.width, size.height);
        const an::Result<an::BasicStatistics> basic =
            an::computeImageBasicStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u);
        ASSERT_TRUE(basic.hasValue()) << basic.status().message();
        const an::Result<an::RobustStatistics> robust = an::computeImageRobustStatistics(
            execution, an::SampleType::Float32, samples.data(), layout, 0u, {}, {.medianAbsoluteDeviation = true});
        ASSERT_TRUE(robust.hasValue()) << robust.status().message();

        std::vector<double> data(samples.begin(), samples.end());
        const double mean = gsl_stats_mean(data.data(), 1, data.size());
        const double variance = gsl_stats_variance_with_fixed_mean(data.data(), 1, data.size(), mean);
        std::vector<double> sorted = data;
        gsl_sort(sorted.data(), 1, sorted.size());
        const double median = gsl_stats_median_from_sorted_data(sorted.data(), 1, sorted.size());
        std::vector<double> work(data.size(), 0.0);
        const double mad = gsl_stats_mad0(data.data(), 1, data.size(), work.data());

        const std::string label = sizeLabel(size);
        expectClose(label + " mean", basic->mean, mean, 0.0, 1.0e-12);
        expectClose(label + " population variance", basic->populationVariance, variance, 1.0e-18, 1.0e-9);
        expectClose(label + " minimum", basic->minimum, *std::min_element(data.begin(), data.end()), 0.0, 0.0);
        expectClose(label + " maximum", basic->maximum, *std::max_element(data.begin(), data.end()), 0.0, 0.0);
        expectClose(label + " median", robust->median, median, 0.0, 1.0e-12);
        expectClose(label + " mad", robust->medianAbsoluteDeviation, mad, 1.0e-12, 1.0e-9);
    }
}

TEST(StatisticsParityTest, TheSixteenBitMedianAgreesWithTheLibrarysIntegerMedian)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::array<PlaneSize, 4u> kSizes{{{7u, 5u}, {16u, 9u}, {127u, 61u}, {256u, 192u}}};
    for (const PlaneSize size : kSizes)
    {
        Sequence sequence{12000u + size.width};
        std::vector<std::uint16_t> samples(static_cast<std::size_t>(size.width) * size.height, 0u);
        for (std::uint16_t& sample : samples)
        {
            sample = static_cast<std::uint16_t>(sequence.nextDouble() * 65536.0);
        }
        const an::ImageLayout layout = planarLayout(an::SampleType::UInt16, size.width, size.height);
        const an::Result<an::RobustStatistics> robust =
            an::computeImageRobustStatistics(execution, an::SampleType::UInt16, samples.data(), layout, 0u, {}, {});
        ASSERT_TRUE(robust.hasValue()) << robust.status().message();
        std::vector<unsigned short> sorted(samples.begin(), samples.end());
        gsl_sort_ushort(sorted.data(), 1, sorted.size());
        const double median = gsl_stats_ushort_median_from_sorted_data(sorted.data(), 1, sorted.size());
        expectClose(sizeLabel(size) + " uint16 median", robust->median, median, 0.0, 0.0);
    }
}

/// The number of monomials `x^i y^j` with `i + j <= degree`.
[[nodiscard]] std::size_t monomialCount(std::uint32_t degree)
{
    return static_cast<std::size_t>(degree + 1u) * (degree + 2u) / 2u;
}

/// Fills one design-matrix row with the monomials of a site in normalised coordinates.
void monomialsAt(double u, double v, std::uint32_t degree, gsl_matrix* design, std::size_t row)
{
    std::size_t column = 0u;
    for (std::uint32_t total = 0u; total <= degree; ++total)
    {
        for (std::uint32_t powerX = 0u; powerX <= total; ++powerX)
        {
            const std::uint32_t powerY = total - powerX;
            gsl_matrix_set(design, row, column, std::pow(u, powerX) * std::pow(v, powerY));
            ++column;
        }
    }
}

TEST(StatisticsParityTest, APolynomialBackgroundFitDescribesTheSameSurfaceAsTheLibrarysLeastSquares)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr PlaneSize kSize{256u, 192u};
    // A cubic sky with a little noise, so every degree below three underfits it visibly and
    // every degree at or above it recovers it.
    Sequence sequence{13000u};
    std::vector<float> samples(static_cast<std::size_t>(kSize.width) * kSize.height, 0.0f);
    for (std::uint32_t y = 0u; y < kSize.height; ++y)
    {
        for (std::uint32_t x = 0u; x < kSize.width; ++x)
        {
            const double u = 2.0 * x / (kSize.width - 1u) - 1.0;
            const double v = 2.0 * y / (kSize.height - 1u) - 1.0;
            const double sky =
                0.3 + 0.08 * u - 0.05 * v + 0.04 * u * v - 0.03 * u * u + 0.02 * v * v * v + 0.015 * u * u * v;
            samples[static_cast<std::size_t>(y) * kSize.width + x] =
                static_cast<float>(sky + 0.004 * (sequence.nextDouble() - 0.5));
        }
    }
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kSize.width, kSize.height);
    // Ten boxes of fifteen pixels per line, so the grid fits both axes of the picture with room.
    const an::BackgroundSampleRequest request{.extent = 15u, .samplesPerLine = 10u};
    const an::Result<an::BackgroundSampleGrid> grid = an::planBackgroundSampleGrid(request, kSize.width, kSize.height);
    ASSERT_TRUE(grid.hasValue()) << grid.status().message();
    std::vector<an::BackgroundSample> sites(grid->siteCount(), an::BackgroundSample{});
    const an::Result<an::BackgroundSampleResult> measured =
        an::sampleImageBackground(execution, an::SampleType::Float32, samples.data(), layout, request, sites);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    sites.resize(measured->acceptedCount);
    ASSERT_GE(sites.size(), 30u);

    for (std::uint32_t degree = 1u; degree <= 4u; ++degree)
    {
        const an::BackgroundFitRequest fit{.kind = an::BackgroundModelKind::Polynomial, .degree = degree};
        const an::Result<std::uint32_t> termCount =
            an::backgroundModelTermCount(fit, static_cast<std::uint32_t>(sites.size()));
        ASSERT_TRUE(termCount.hasValue()) << termCount.status().message();
        std::vector<double> coefficients(*termCount, 0.0);
        const an::Result<an::BackgroundFitResult> fitted =
            an::fitBackgroundModel(execution, fit, sites, 0u, kSize.width, kSize.height, coefficients);
        ASSERT_TRUE(fitted.hasValue()) << fitted.status().message();
        EXPECT_EQ(fitted->droppedTerms, 0u);
        std::vector<float> ours(samples.size(), 0.0f);
        const std::optional<an::PlaneView<float>> plane = an::PlaneView<float>::create(
            ours, kSize.width, kSize.height, 1u, static_cast<std::uint64_t>(kSize.width) * sizeof(float));
        ASSERT_TRUE(plane.has_value());
        const an::BackgroundModelView model{
            .fit = fit, .sites = sites, .coefficients = coefficients, .width = kSize.width, .height = kSize.height};
        ASSERT_TRUE(an::evaluateBackgroundModel(execution, model, *plane).hasValue());

        // The library's fit of the same sites in a normalised monomial basis.
        const std::size_t terms = monomialCount(degree);
        gsl_matrix* design = gsl_matrix_alloc(sites.size(), terms);
        gsl_vector* observed = gsl_vector_alloc(sites.size());
        gsl_vector* solution = gsl_vector_alloc(terms);
        gsl_matrix* covariance = gsl_matrix_alloc(terms, terms);
        gsl_multifit_linear_workspace* workspace = gsl_multifit_linear_alloc(sites.size(), terms);
        const double halfWidth = (kSize.width - 1u) / 2.0;
        const double halfHeight = (kSize.height - 1u) / 2.0;
        for (std::size_t site = 0u; site < sites.size(); ++site)
        {
            monomialsAt((sites[site].x - halfWidth) / halfWidth, (sites[site].y - halfHeight) / halfHeight, degree,
                        design, site);
            gsl_vector_set(observed, site, sites[site].median[0]);
        }
        double chiSquared = 0.0;
        ASSERT_EQ(gsl_multifit_linear(design, observed, solution, covariance, &chiSquared, workspace), 0);
        std::vector<float> theirs(samples.size(), 0.0f);
        gsl_matrix* rowDesign = gsl_matrix_alloc(1, terms);
        for (std::uint32_t y = 0u; y < kSize.height; ++y)
        {
            for (std::uint32_t x = 0u; x < kSize.width; ++x)
            {
                monomialsAt((x - halfWidth) / halfWidth, (y - halfHeight) / halfHeight, degree, rowDesign, 0u);
                double value = 0.0;
                for (std::size_t term = 0u; term < terms; ++term)
                {
                    value += gsl_matrix_get(rowDesign, 0u, term) * gsl_vector_get(solution, term);
                }
                theirs[static_cast<std::size_t>(y) * kSize.width + x] = static_cast<float>(value);
            }
        }
        gsl_matrix_free(rowDesign);
        gsl_multifit_linear_free(workspace);
        gsl_matrix_free(covariance);
        gsl_vector_free(solution);
        gsl_vector_free(observed);
        gsl_matrix_free(design);

        expectParity(kDomain, "polynomial background degree " + std::to_string(degree) + " " + sizeLabel(kSize), ours,
                     theirs, 1.0e-5);
    }
}

} // namespace
