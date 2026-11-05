// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Spatial filters against the imaging library: a blur with `cv::GaussianBlur`, erosion, dilation and
// their compositions with `cv::morphologyEx`, and equalization with `cv::CLAHE`; this engine does all
// three itself.
//
// A Gaussian agrees to single-precision rounding once the two kernels have the same radius, which
// the library derives from sigma and this engine from a stated truncation - the sweep states the
// truncation that reproduces the library's radius, so what is compared is the arithmetic and not the
// footprint. A morphological operation is a minimum or a maximum over a footprint and agrees bit for
// bit when the footprint is the same bytes, so the sweep hands this engine the library's own
// structuring element and compares the named shapes separately.
//
// Contrast-limited equalization is two steps, and the sweep holds each to what separates the sides
// in it. The per-tile mapping - histogram, clip, redistribution, cumulative sum - is compared as a
// table, one tile at a time, and agrees to the library's rounding of a code. The interpolation
// between tiles is compared against a reference built in the test from the library's table rule,
// held to the library on one tile, interpolated at pixel centres, which this engine does and the library does not: the
// library places a tile's centre half a pixel late, a difference the sweep reports against the
// library's whole answer rather than holds, since it is the library's convention and not a rounding.

#include "ParityHarness.h"

#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Algorithms/Spatial/GeneralFilter.h>

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

constexpr const char* kDomain = "spatial";

/// One border rule on both sides.
struct BorderPair
{
    an::KernelBorderMode engine;
    int library;
    const char* name;
};

constexpr std::array<BorderPair, 4u> kBorders{{
    {an::KernelBorderMode::ReflectExcludingEdge, cv::BORDER_REFLECT_101, "reflect-101"},
    {an::KernelBorderMode::Reflect, cv::BORDER_REFLECT, "reflect"},
    {an::KernelBorderMode::Clamp, cv::BORDER_REPLICATE, "replicate"},
    {an::KernelBorderMode::Constant, cv::BORDER_CONSTANT, "constant"},
}};

/// The library's kernel radius for a sigma on a floating-point image: `round(8 sigma + 1) | 1` taps.
[[nodiscard]] std::uint32_t libraryGaussianRadius(double sigma)
{
    const int taps = cvRound(sigma * 4.0 * 2.0 + 1.0) | 1;
    return static_cast<std::uint32_t>((taps - 1) / 2);
}

/// A Gaussian plan whose radius is the library's, found by stating the truncation that yields it.
[[nodiscard]] an::Result<an::GaussianPlan> planMatchingGaussian(double sigma, an::KernelBorderMode border)
{
    const std::uint32_t wanted = libraryGaussianRadius(sigma);
    an::Result<an::GaussianPlan> plan = an::planGaussianBlur({.sigmaX = sigma, .sigmaY = sigma});
    for (double truncation = static_cast<double>(wanted) / sigma - 0.5 / sigma; truncation <= (wanted + 0.51) / sigma;
         truncation += 0.02 / sigma)
    {
        if (truncation <= 0.0)
        {
            continue;
        }
        plan = an::planGaussianBlur({.sigmaX = sigma,
                                     .sigmaY = sigma,
                                     .truncation = truncation,
                                     .borders = {.border = border, .constantBorder = 0.0f}});
        if (plan.hasValue() && plan->horizontalRadius() == wanted && plan->verticalRadius() == wanted)
        {
            return plan;
        }
    }
    return plan;
}

TEST(SpatialParityTest, GaussianBlursAgreeToSinglePrecisionRoundingAtTheLibrarysRadius)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::array<double, 5u> kSigmas{0.6, 1.0, 1.7, 2.5, 4.3};
    for (const double sigma : kSigmas)
    {
        for (const BorderPair& border : kBorders)
        {
            const an::Result<an::GaussianPlan> plan = planMatchingGaussian(sigma, border.engine);
            ASSERT_TRUE(plan.hasValue()) << plan.status().message();
            ASSERT_EQ(plan->horizontalRadius(), libraryGaussianRadius(sigma)) << "sigma " << sigma;
            const int taps = static_cast<int>(2u * plan->horizontalRadius() + 1u);
            for (const PlaneSize size : kSweepSizes)
            {
                const std::vector<float> source = uniformPlane(size.width, size.height, 5000u + size.width);
                const an::ImageLayout layout = planarLayout(an::SampleType::Float32, size.width, size.height);
                std::vector<float> ours(source.size(), -7.0f);
                const an::Result<an::SpatialFilterResult> ran = an::blurImageGaussian(
                    execution, an::SampleType::Float32, source.data(), layout, ours.data(), layout, *plan);
                ASSERT_TRUE(ran.hasValue()) << ran.status().message();
                const cv::Mat input = matrixOf(source, size.width, size.height);
                cv::Mat output;
                cv::GaussianBlur(input, output, cv::Size(taps, taps), sigma, sigma, border.library);
                expectParity(kDomain,
                             "gaussian sigma " + std::to_string(sigma) + " radius " + std::to_string(taps / 2) + " " +
                                 border.name + " " + sizeLabel(size),
                             ours, planeOf(output), 1.0e-5);
            }
        }
    }
}

/// One morphological operation on both sides.
struct OperationPair
{
    an::MorphologyOperation engine;
    int library;
    const char* name;
    bool repeatable;
};

constexpr std::array<OperationPair, 7u> kOperations{{
    {an::MorphologyOperation::Erode, cv::MORPH_ERODE, "erode", true},
    {an::MorphologyOperation::Dilate, cv::MORPH_DILATE, "dilate", true},
    {an::MorphologyOperation::Open, cv::MORPH_OPEN, "open", false},
    {an::MorphologyOperation::Close, cv::MORPH_CLOSE, "close", false},
    {an::MorphologyOperation::Gradient, cv::MORPH_GRADIENT, "gradient", false},
    {an::MorphologyOperation::TopHat, cv::MORPH_TOPHAT, "tophat", false},
    {an::MorphologyOperation::BlackHat, cv::MORPH_BLACKHAT, "blackhat", false},
}};

struct ShapePair
{
    an::MorphologyShape engine;
    int library;
    const char* name;
};

constexpr std::array<ShapePair, 3u> kShapes{{
    {an::MorphologyShape::Rectangle, cv::MORPH_RECT, "rect"},
    {an::MorphologyShape::Ellipse, cv::MORPH_ELLIPSE, "ellipse"},
    {an::MorphologyShape::Cross, cv::MORPH_CROSS, "cross"},
}};

struct Radii
{
    std::uint32_t x;
    std::uint32_t y;
};

constexpr std::array<Radii, 4u> kRadii{{{1u, 1u}, {2u, 2u}, {3u, 1u}, {1u, 3u}}};

/// The library's structuring element as the row-major bytes this engine's custom footprint reads.
[[nodiscard]] std::vector<std::uint8_t> footprintOf(const cv::Mat& element)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(element.rows) * element.cols, 0u);
    for (int y = 0; y < element.rows; ++y)
    {
        for (int x = 0; x < element.cols; ++x)
        {
            bytes[static_cast<std::size_t>(y) * element.cols + x] = element.at<std::uint8_t>(y, x) != 0u ? 1u : 0u;
        }
    }
    return bytes;
}

[[nodiscard]] std::vector<float> engineMorphology(const an::ExecutionContext& execution, std::span<const float> source,
                                                  PlaneSize size, const an::MorphologyRequest& request)
{
    const an::Result<an::MorphologyPlan> plan = an::planMorphology(request);
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    std::vector<float> ours(source.size(), -7.0f);
    if (!plan.hasValue())
    {
        return ours;
    }
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, size.width, size.height);
    const an::Result<an::SpatialFilterResult> ran = an::filterImageMorphology(
        execution, an::SampleType::Float32, source.data(), layout, ours.data(), layout, *plan);
    EXPECT_TRUE(ran.hasValue()) << ran.status().message();
    return ours;
}

TEST(SpatialParityTest, EveryMorphologicalOperationOverTheLibrarysOwnElementAgreesBitForBit)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::array<BorderPair, 2u> kMorphologyBorders{{
        {an::KernelBorderMode::ReflectExcludingEdge, cv::BORDER_REFLECT_101, "reflect-101"},
        {an::KernelBorderMode::Clamp, cv::BORDER_REPLICATE, "replicate"},
    }};
    for (const OperationPair& operation : kOperations)
    {
        for (const ShapePair& shape : kShapes)
        {
            for (const Radii radii : kRadii)
            {
                const cv::Mat element = cv::getStructuringElement(
                    shape.library, cv::Size(static_cast<int>(2u * radii.x + 1u), static_cast<int>(2u * radii.y + 1u)));
                const std::vector<std::uint8_t> footprint = footprintOf(element);
                for (const BorderPair& border : kMorphologyBorders)
                {
                    for (std::uint32_t iterations = 1u; iterations <= (operation.repeatable ? 2u : 1u); ++iterations)
                    {
                        for (const PlaneSize size : kSweepSizes)
                        {
                            const std::vector<float> source = uniformPlane(size.width, size.height, 6000u + size.width);
                            an::MorphologyRequest request{};
                            request.operation = operation.engine;
                            request.shape = an::MorphologyShape::Custom;
                            request.radiusX = radii.x;
                            request.radiusY = radii.y;
                            request.iterations = iterations;
                            request.border = border.engine;
                            request.customFootprint = footprint;
                            const std::vector<float> ours = engineMorphology(execution, source, size, request);
                            const cv::Mat input = matrixOf(source, size.width, size.height);
                            cv::Mat output;
                            cv::morphologyEx(input, output, operation.library, element, cv::Point(-1, -1),
                                             static_cast<int>(iterations), border.library);
                            expectParity(kDomain,
                                         std::string(operation.name) + " " + shape.name +
                                             " r=" + std::to_string(radii.x) + "," + std::to_string(radii.y) + " it=" +
                                             std::to_string(iterations) + " " + border.name + " " + sizeLabel(size),
                                         ours, planeOf(output), 0.0);
                        }
                    }
                }
            }
        }
    }
}

TEST(SpatialParityTest, TheNamedShapesAreComparedAgainstTheLibrarysRasterisation)
{
    // The rectangle is the same footprint by definition and must agree. The ellipse and the cross
    // are rasterised by each side's own rule; where they differ is reported, not failed, because
    // this engine draws its own element rather than the library's.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr PlaneSize kSize{64u, 48u};
    const std::vector<float> source = uniformPlane(kSize.width, kSize.height, 6500u);
    for (const ShapePair& shape : kShapes)
    {
        for (const Radii radii : kRadii)
        {
            const cv::Mat element = cv::getStructuringElement(
                shape.library, cv::Size(static_cast<int>(2u * radii.x + 1u), static_cast<int>(2u * radii.y + 1u)));
            an::MorphologyRequest request{};
            request.operation = an::MorphologyOperation::Dilate;
            request.shape = shape.engine;
            request.radiusX = radii.x;
            request.radiusY = radii.y;
            request.border = an::KernelBorderMode::ReflectExcludingEdge;
            const std::vector<float> ours = engineMorphology(execution, source, kSize, request);
            const cv::Mat input = matrixOf(source, kSize.width, kSize.height);
            cv::Mat output;
            cv::dilate(input, output, element, cv::Point(-1, -1), 1, cv::BORDER_REFLECT_101);
            const std::vector<float> theirs = planeOf(output);
            const Comparison found = compare(ours, theirs, 0.0, 0.0);
            const std::string label = std::string("named ") + shape.name + " r=" + std::to_string(radii.x) + "," +
                                      std::to_string(radii.y) + " dilate " + sizeLabel(kSize);
            report(kDomain, label, found, 0.0, 0.0);
            if (shape.engine == an::MorphologyShape::Rectangle)
            {
                EXPECT_EQ(found.beyond, 0u) << label;
            }
            else
            {
                std::cout << "parity | " << kDomain << " | " << label
                          << " | named-shape samples differing: " << found.beyond << " of " << found.count << "\n";
            }
        }
    }
}

/// A quantized plane as the library's integer matrix.
[[nodiscard]] cv::Mat codeMatrixOf(std::span<const std::uint32_t> codes, PlaneSize size, std::uint32_t levels)
{
    cv::Mat matrix(static_cast<int>(size.height), static_cast<int>(size.width), levels == 256u ? CV_8UC1 : CV_16UC1);
    for (std::uint32_t y = 0u; y < size.height; ++y)
    {
        for (std::uint32_t x = 0u; x < size.width; ++x)
        {
            const std::uint32_t code = codes[static_cast<std::size_t>(y) * size.width + x];
            if (levels == 256u)
            {
                matrix.at<std::uint8_t>(static_cast<int>(y), static_cast<int>(x)) = static_cast<std::uint8_t>(code);
            }
            else
            {
                matrix.at<std::uint16_t>(static_cast<int>(y), static_cast<int>(x)) = static_cast<std::uint16_t>(code);
            }
        }
    }
    return matrix;
}

[[nodiscard]] double codeAt(const cv::Mat& matrix, int y, int x)
{
    return matrix.depth() == CV_8U ? static_cast<double>(matrix.at<std::uint8_t>(y, x))
                                   : static_cast<double>(matrix.at<std::uint16_t>(y, x));
}

/// The library's per-tile lookup table, by its own rule: histogram, the clip limit as the stated
/// multiple of the average bin truncated to a whole count and never below one, the clipped counts
/// returned evenly with the remainder stepped across the range, the cumulative sum scaled to the
/// code range in single precision and rounded to a code. The one-tile sweep holds this rule to the
/// library's own answer, so the grid sweep can interpolate it as the library's tables.
[[nodiscard]] std::vector<double> libraryTable(std::span<const std::uint32_t> codes, std::uint32_t levels,
                                               double clipLimit)
{
    std::vector<std::uint32_t> histogram(levels, 0u);
    for (const std::uint32_t code : codes)
    {
        ++histogram[code];
    }
    const auto area = static_cast<double>(codes.size());
    const std::uint32_t clip =
        (std::max)(1u, static_cast<std::uint32_t>(clipLimit * area / static_cast<double>(levels)));
    std::uint32_t clipped = 0u;
    for (std::uint32_t& count : histogram)
    {
        if (count > clip)
        {
            clipped += count - clip;
            count = clip;
        }
    }
    const std::uint32_t batch = clipped / levels;
    std::uint32_t residual = clipped - batch * levels;
    for (std::uint32_t& count : histogram)
    {
        count += batch;
    }
    if (residual != 0u)
    {
        const std::uint32_t step = (std::max)(levels / residual, 1u);
        for (std::uint32_t bin = 0u; bin < levels && residual > 0u; bin += step, --residual)
        {
            ++histogram[bin];
        }
    }
    const float scale = static_cast<float>(levels - 1u) / static_cast<float>(area);
    std::vector<double> table(levels, 0.0);
    std::uint32_t cumulative = 0u;
    for (std::uint32_t bin = 0u; bin < levels; ++bin)
    {
        cumulative += histogram[bin];
        const float scaled = static_cast<float>(cumulative) * scale;
        table[bin] = (std::min)(static_cast<double>(levels - 1u), static_cast<double>(std::nearbyint(scaled)));
    }
    return table;
}

/// The library's tables for every tile of a grid, by the rule above.
[[nodiscard]] std::vector<std::vector<double>> libraryTileTables(std::span<const std::uint32_t> codes, PlaneSize size,
                                                                 std::uint32_t tileColumns, std::uint32_t tileRows,
                                                                 double clipLimit, std::uint32_t levels)
{
    std::vector<std::vector<double>> tables;
    tables.reserve(static_cast<std::size_t>(tileColumns) * tileRows);
    const std::uint32_t tileWidth = size.width / tileColumns;
    const std::uint32_t tileHeight = size.height / tileRows;
    for (std::uint32_t ty = 0u; ty < tileRows; ++ty)
    {
        for (std::uint32_t tx = 0u; tx < tileColumns; ++tx)
        {
            std::vector<std::uint32_t> tileCodes;
            tileCodes.reserve(static_cast<std::size_t>(tileWidth) * tileHeight);
            for (std::uint32_t y = ty * tileHeight; y < (ty + 1u) * tileHeight; ++y)
            {
                for (std::uint32_t x = tx * tileWidth; x < (tx + 1u) * tileWidth; ++x)
                {
                    tileCodes.push_back(codes[static_cast<std::size_t>(y) * size.width + x]);
                }
            }
            tables.push_back(libraryTable(tileCodes, levels, clipLimit));
        }
    }
    return tables;
}

/// Contrast-limited equalization on a quantized plane, compared in code values.
void expectClaheParity(const an::ExecutionContext& execution, PlaneSize size, std::uint32_t tileColumns,
                       std::uint32_t tileRows, double clipLimit, std::uint32_t levels)
{
    std::vector<std::uint32_t> codes;
    const std::vector<float> source =
        quantizedPlane(size.width, size.height, 7000u + size.width + levels, levels, codes);
    const an::Result<an::AdaptiveHistogramPlan> plan =
        an::planAdaptiveHistogramEqualization({.tileColumns = tileColumns,
                                               .tileRows = tileRows,
                                               .binCount = levels,
                                               .clipLimit = clipLimit,
                                               .lowest = 0.0,
                                               .highest = 1.0});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, size.width, size.height);
    std::vector<float> ours(source.size(), -7.0f);
    const an::Result<an::AdaptiveHistogramResult> ran = an::equalizeImageAdaptiveHistogram(
        execution, an::SampleType::Float32, source.data(), layout, ours.data(), layout, *plan);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    std::vector<float> oursCodes(source.size(), 0.0f);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        oursCodes[index] = static_cast<float>(std::round(static_cast<double>(ours[index]) * (levels - 1u)));
    }

    const cv::Mat input = codeMatrixOf(codes, size, levels);
    const std::string label = "clahe " + std::to_string(levels) + " levels tiles " + std::to_string(tileColumns) + "x" +
                              std::to_string(tileRows) + " clip " + std::to_string(clipLimit) + " " + sizeLabel(size);

    // Step one: the tables. With one tile the library's answer is its table, so the whole image
    // compares directly; with a grid each tile's table is read from the tile equalized alone.
    if (tileColumns == 1u && tileRows == 1u)
    {
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(clipLimit, cv::Size(1, 1));
        cv::Mat output;
        clahe->apply(input, output);
        std::vector<float> theirs(source.size(), 0.0f);
        for (std::uint32_t y = 0u; y < size.height; ++y)
        {
            for (std::uint32_t x = 0u; x < size.width; ++x)
            {
                theirs[static_cast<std::size_t>(y) * size.width + x] =
                    static_cast<float>(codeAt(output, static_cast<int>(y), static_cast<int>(x)));
            }
        }
        expectParity(kDomain, label + " table", oursCodes, theirs, 1.0);
        // The rule the grid sweep interpolates is held to the library here, code for code.
        const std::vector<double> rule = libraryTable(codes, levels, clipLimit);
        std::vector<float> ruled(source.size(), 0.0f);
        for (std::size_t index = 0u; index < source.size(); ++index)
        {
            ruled[index] = static_cast<float>(rule[codes[index]]);
        }
        expectParity(kDomain, label + " rule against the library", ruled, theirs, 0.0);
        return;
    }

    // Step two: the interpolation, against the library's tables interpolated at pixel centres.
    const std::vector<std::vector<double>> tables =
        libraryTileTables(codes, size, tileColumns, tileRows, clipLimit, levels);
    std::vector<float> reference(source.size(), 0.0f);
    for (std::uint32_t y = 0u; y < size.height; ++y)
    {
        const double gy = (static_cast<double>(y) + 0.5) * tileRows / size.height - 0.5;
        const auto rawY = static_cast<std::int64_t>(std::floor(gy));
        const std::uint32_t y0 =
            static_cast<std::uint32_t>((std::clamp)(rawY, std::int64_t{0}, std::int64_t{tileRows} - 1));
        const std::uint32_t y1 =
            static_cast<std::uint32_t>((std::clamp)(rawY + 1, std::int64_t{0}, std::int64_t{tileRows} - 1));
        const double fy = gy - static_cast<double>(rawY);
        for (std::uint32_t x = 0u; x < size.width; ++x)
        {
            const double gx = (static_cast<double>(x) + 0.5) * tileColumns / size.width - 0.5;
            const auto rawX = static_cast<std::int64_t>(std::floor(gx));
            const std::uint32_t x0 =
                static_cast<std::uint32_t>((std::clamp)(rawX, std::int64_t{0}, std::int64_t{tileColumns} - 1));
            const std::uint32_t x1 =
                static_cast<std::uint32_t>((std::clamp)(rawX + 1, std::int64_t{0}, std::int64_t{tileColumns} - 1));
            const double fx = gx - static_cast<double>(rawX);
            const std::uint32_t code = codes[static_cast<std::size_t>(y) * size.width + x];
            const auto at = [&](std::uint32_t tx, std::uint32_t ty) {
                return tables[static_cast<std::size_t>(ty) * tileColumns + tx][code];
            };
            const double top = at(x0, y0) + fx * (at(x1, y0) - at(x0, y0));
            const double bottom = at(x0, y1) + fx * (at(x1, y1) - at(x0, y1));
            reference[static_cast<std::size_t>(y) * size.width + x] = static_cast<float>(top + fy * (bottom - top));
        }
    }
    // The library's tables are rounded to a code and the interpolated answer is rounded again,
    // so a code and a half is the rounding of both steps.
    expectParity(kDomain, label + " interpolation of the library tables", oursCodes, reference, 1.5);

    // The library's whole answer, with its half-pixel tile centres, reported for the record.
    cv::Ptr<cv::CLAHE> clahe =
        cv::createCLAHE(clipLimit, cv::Size(static_cast<int>(tileColumns), static_cast<int>(tileRows)));
    cv::Mat output;
    clahe->apply(input, output);
    std::vector<float> theirs(source.size(), 0.0f);
    for (std::uint32_t y = 0u; y < size.height; ++y)
    {
        for (std::uint32_t x = 0u; x < size.width; ++x)
        {
            theirs[static_cast<std::size_t>(y) * size.width + x] =
                static_cast<float>(codeAt(output, static_cast<int>(y), static_cast<int>(x)));
        }
    }
    const Comparison whole = compare(oursCodes, theirs, 1.0e9, 0.0);
    std::cout << "parity | " << kDomain << " | " << label
              << " against the library's whole answer | maxAbs=" << whole.maxAbs << " meanAbs=" << whole.meanAbs
              << " | reported, not held (half-pixel tile centres)\n";
}

TEST(SpatialParityTest, ContrastLimitedEqualizationTablesAgreeWithTheLibraryToACode)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::array<PlaneSize, 3u> kSizes{{{64u, 64u}, {96u, 80u}, {33u, 21u}}};
    constexpr std::array<double, 4u> kClipLimits{1.0, 2.0, 4.0, 40.0};
    for (const PlaneSize size : kSizes)
    {
        for (const double clip : kClipLimits)
        {
            expectClaheParity(execution, size, 1u, 1u, clip, 256u);
            expectClaheParity(execution, size, 1u, 1u, clip, 65536u);
        }
    }
}

TEST(SpatialParityTest, ContrastLimitedEqualizationInterpolatesTheLibrarysTablesToACodeAndAHalf)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    struct Case
    {
        PlaneSize size;
        std::uint32_t tileColumns;
        std::uint32_t tileRows;
    };
    // Sizes the tile grid divides, so neither side has to pad a partial tile.
    constexpr std::array<Case, 4u> kCases{
        {{{64u, 64u}, 8u, 8u}, {{128u, 96u}, 8u, 6u}, {{96u, 80u}, 4u, 4u}, {{60u, 45u}, 2u, 3u}}};
    constexpr std::array<double, 3u> kClipLimits{1.0, 4.0, 40.0};
    for (const Case& item : kCases)
    {
        for (const double clip : kClipLimits)
        {
            expectClaheParity(execution, item.size, item.tileColumns, item.tileRows, clip, 256u);
            expectClaheParity(execution, item.size, item.tileColumns, item.tileRows, clip, 65536u);
        }
    }
}

} // namespace
