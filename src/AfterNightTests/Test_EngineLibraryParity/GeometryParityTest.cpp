// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Geometry against the imaging library: `cv::resize`, and `cv::getRotationMatrix2D` plus
// `cv::warpAffine` for a rotation; this engine resamples itself.
//
// Three things separate the two, and each case is held to the one that applies. A resize agrees to
// single-precision rounding: both walk the same taps with the same weights, the library's in Float32
// and this engine's in Float64. The bilinear rows also run the benchmark harness's independent
// long-double oracle, so a library build that exceeds the rounding bound is distinguished from an
// engine defect without widening that bound. A warp agrees only to the library's coordinate step:
// `warpAffine` resolves destination coordinates in fixed point, 1/32 of a pixel for the interpolation
// and 1/1024 for the linear terms, so a translation stated in whole 32nds compares to rounding while
// a rotation compares to that step times the picture's gradient. The bit-exact modes agree bit for
// bit, which is what they are for.
//
// The area choice is two rules on both sides. When both axes shrink, both integrate the source
// footprint and agree to rounding. When either axis grows, this engine falls back to its ordinary
// bilinear sampling, which the library's `INTER_LINEAR` is; the library's own `INTER_AREA` falls
// back instead to a replication-phased rule of its own, and the sweep reports that distance rather
// than holding it, because it is a difference the geometry record has to decide, not a rounding.

#include "ParityHarness.h"

#include "BilinearResampleOracle.h"

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Image/ValidityMask.h>

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

constexpr const char* kDomain = "geometry";
constexpr double kBilinearOracleTolerance = 2.0e-7;

/// One resize mode on both sides and the distance the two may legitimately keep.
struct ResizeMode
{
    an::KernelInterpolation engine;
    int library;
    const char* name;
    double tolAbs;
};

constexpr std::array<ResizeMode, 6u> kPointModes{{
    {an::KernelInterpolation::Nearest, cv::INTER_NEAREST, "nearest", 0.0},
    {an::KernelInterpolation::NearestExact, cv::INTER_NEAREST_EXACT, "nearest-exact", 0.0},
    {an::KernelInterpolation::Linear, cv::INTER_LINEAR, "bilinear", 1.0e-5},
    {an::KernelInterpolation::LinearExact, cv::INTER_LINEAR_EXACT, "bilinear-exact", 1.0e-5},
    {an::KernelInterpolation::Cubic, cv::INTER_CUBIC, "bicubic", 2.0e-5},
    {an::KernelInterpolation::Lanczos4, cv::INTER_LANCZOS4, "lanczos4", 5.0e-5},
}};

/// The scale pairs a resize sweep walks: three pure reductions, two pure enlargements and two
/// mixed, none of them whole numbers except one.
struct ScalePair
{
    double x;
    double y;
};

constexpr std::array<ScalePair, 7u> kScales{
    {{0.37, 0.5}, {0.5, 0.75}, {0.75, 0.37}, {1.5, 2.63}, {2.0, 1.5}, {2.63, 0.5}, {0.5, 2.0}}};

[[nodiscard]] an::ValidityMaskView<std::uint8_t> viewOf(an::ValidityMask& mask)
{
    const std::optional<an::ValidityMaskView<std::uint8_t>> view = mask.view();
    EXPECT_TRUE(view.has_value());
    return *view;
}

/// Runs one geometry plan over a Float32 plane and returns the destination.
[[nodiscard]] std::vector<float> runPlan(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                         const an::GeometryPlan& plan, std::span<const float> source,
                                         PlaneSize sourceSize, PlaneSize destinationSize)
{
    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::Float32, sourceSize.width, sourceSize.height);
    const an::ImageLayout destinationLayout =
        planarLayout(an::SampleType::Float32, destinationSize.width, destinationSize.height);
    std::vector<float> destination(static_cast<std::size_t>(destinationSize.width) * destinationSize.height, -7.0f);
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
    EXPECT_TRUE(mask.hasValue());
    if (!mask.hasValue())
    {
        return destination;
    }
    an::ValidityMaskView<std::uint8_t> maskView = viewOf(*mask);
    const an::Result<an::GeometryResult> ran =
        an::transformImageGeometry(execution, an::SampleType::Float32, source.data(), sourceLayout, destination.data(),
                                   destinationLayout, plan, {}, maskView);
    EXPECT_TRUE(ran.hasValue()) << ran.status().message();
    return destination;
}

[[nodiscard]] std::vector<float> engineResize(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                              std::span<const float> source, PlaneSize sourceSize,
                                              PlaneSize destinationSize, an::KernelInterpolation mode)
{
    const an::Result<an::GeometryPlan> plan =
        an::planResize(monoDescriptor(sourceSize.width, sourceSize.height),
                       {.width = destinationSize.width,
                        .height = destinationSize.height,
                        .sampling = {.interpolation = mode, .border = an::KernelBorderMode::Clamp},
                        .ringing = an::ResizeRingingPolicy::Keep});
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    if (!plan.hasValue())
    {
        return std::vector<float>(static_cast<std::size_t>(destinationSize.width) * destinationSize.height, 0.0f);
    }
    return runPlan(engine, execution, *plan, source, sourceSize, destinationSize);
}

[[nodiscard]] std::vector<float> libraryResize(std::span<const float> source, PlaneSize sourceSize,
                                               PlaneSize destinationSize, int flag)
{
    const cv::Mat input = matrixOf(source, sourceSize.width, sourceSize.height);
    cv::Mat output;
    cv::resize(input, output,
               cv::Size(static_cast<int>(destinationSize.width), static_cast<int>(destinationSize.height)), 0.0, 0.0,
               flag);
    return planeOf(output);
}

[[nodiscard]] PlaneSize scaled(PlaneSize size, ScalePair scale)
{
    return {static_cast<std::uint32_t>((std::max)(1.0, std::round(size.width * scale.x))),
            static_cast<std::uint32_t>((std::max)(1.0, std::round(size.height * scale.y)))};
}

/// Holds a bilinear result to the independent oracle and classifies any larger library difference.
///
/// The direct library comparison retains its original tolerance. When that comparison exceeds the
/// tolerance, it is accepted only if the engine remains inside the tighter oracle bound and the
/// library is the side outside the direct bound. This makes an OpenCV build's isolated arithmetic
/// drift reportable without turning it into either a wider allowance or a false engine failure.
void expectBilinearParity(const std::string& label, std::span<const float> source, PlaneSize sourceSize,
                          PlaneSize destinationSize, std::span<const float> engine, std::span<const float> library,
                          double libraryTolerance)
{
    const std::vector<an::bench::BilinearResampleProbe> oracle = an::bench::computeBilinearResizeOracle(
        source, sourceSize.width, sourceSize.height, destinationSize.width, destinationSize.height, 1u);
    ASSERT_FALSE(oracle.empty());

    const double engineError = an::bench::bilinearResizeError(engine, destinationSize.width, oracle);
    const double libraryError = an::bench::bilinearResizeError(library, destinationSize.width, oracle);
    EXPECT_LE(engineError, kBilinearOracleTolerance)
        << kDomain << " / " << label << ": engine differs from the independent long-double oracle";

    const Comparison direct = compare(engine, library, libraryTolerance, 0.0);
    report(kDomain, label, direct, libraryTolerance, 0.0);
    std::cout << "parity | " << kDomain << " | " << label << " independent oracle | engineMax=" << engineError
              << " | libraryMax=" << libraryError << " | engineTol=" << kBilinearOracleTolerance << "\n";
    if (direct.beyond != 0u)
    {
        EXPECT_GT(libraryError, libraryTolerance)
            << kDomain << " / " << label
            << ": direct parity failed but the independent oracle does not identify the library as the outlier";
    }
}

TEST(GeometryParityTest, EveryPointResizeModeAgreesWithTheLibraryToSinglePrecisionRounding)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    for (const ResizeMode& mode : kPointModes)
    {
        for (const PlaneSize size : kSweepSizes)
        {
            const std::vector<float> source = uniformPlane(size.width, size.height, 1000u + size.width);
            for (const ScalePair scale : kScales)
            {
                const PlaneSize destination = scaled(size, scale);
                const std::vector<float> ours = engineResize(engine, execution, source, size, destination, mode.engine);
                const std::vector<float> theirs = libraryResize(source, size, destination, mode.library);
                const std::string label =
                    std::string(mode.name) + " " + sizeLabel(size) + " -> " + sizeLabel(destination);
                if (mode.engine == an::KernelInterpolation::Linear ||
                    mode.engine == an::KernelInterpolation::LinearExact)
                {
                    expectBilinearParity(label, source, size, destination, ours, theirs, mode.tolAbs);
                }
                else
                {
                    expectParity(kDomain, label, ours, theirs, mode.tolAbs);
                }
            }
        }
    }
}

TEST(GeometryParityTest, AnAreaReductionAgreesWithTheLibraryAndAnAreaEnlargementIsTheBilinearFallback)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    for (const PlaneSize size : kSweepSizes)
    {
        const std::vector<float> source = uniformPlane(size.width, size.height, 1500u + size.width);
        for (const ScalePair scale : kScales)
        {
            const PlaneSize destination = scaled(size, scale);
            const bool reduction = destination.width <= size.width && destination.height <= size.height;
            const std::vector<float> ours =
                engineResize(engine, execution, source, size, destination, an::KernelInterpolation::Area);
            const std::vector<float> theirsArea = libraryResize(source, size, destination, cv::INTER_AREA);
            const std::string label = "area " + sizeLabel(size) + " -> " + sizeLabel(destination);
            if (reduction)
            {
                expectParity(kDomain, label + " reduction", ours, theirsArea, 1.0e-5);
                continue;
            }
            // The fallback is this engine's bilinear, which the library's INTER_LINEAR is; the
            // distance to the library's own area fallback is reported for the record.
            const std::vector<float> theirsLinear = libraryResize(source, size, destination, cv::INTER_LINEAR);
            expectParity(kDomain, label + " fallback against INTER_LINEAR", ours, theirsLinear, 1.0e-5);
            const Comparison fromArea = compare(ours, theirsArea, 1.0e9, 0.0);
            std::cout << "parity | " << kDomain << " | " << label
                      << " fallback against INTER_AREA | maxAbs=" << fromArea.maxAbs << " meanAbs=" << fromArea.meanAbs
                      << " | reported, not held\n";
        }
    }
}

/// The library's fixed-point path for whole-number samples.
template <typename SampleT> void expectBitExactResize(an::SampleType sampleType, int libraryType, std::uint32_t levels)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    struct Pair
    {
        an::KernelInterpolation engine;
        int library;
        const char* name;
    };
    constexpr std::array<Pair, 2u> kPairs{
        {{an::KernelInterpolation::LinearExact, cv::INTER_LINEAR_EXACT, "bilinear-exact"},
         {an::KernelInterpolation::NearestExact, cv::INTER_NEAREST_EXACT, "nearest-exact"}}};
    for (const Pair& pair : kPairs)
    {
        for (const PlaneSize size : kSweepSizes)
        {
            Sequence sequence{2000u + size.width};
            std::vector<SampleT> source(static_cast<std::size_t>(size.width) * size.height, SampleT{});
            for (SampleT& sample : source)
            {
                sample = static_cast<SampleT>(sequence.nextDouble() * levels);
            }
            for (const ScalePair scale : kScales)
            {
                const PlaneSize destination = scaled(size, scale);
                const an::Result<an::GeometryPlan> plan =
                    an::planResize(monoDescriptor(size.width, size.height, sampleType),
                                   {.width = destination.width,
                                    .height = destination.height,
                                    .sampling = {.interpolation = pair.engine, .border = an::KernelBorderMode::Clamp},
                                    .ringing = an::ResizeRingingPolicy::Keep});
                ASSERT_TRUE(plan.hasValue()) << plan.status().message();
                const an::ImageLayout sourceLayout = planarLayout(sampleType, size.width, size.height);
                const an::ImageLayout destinationLayout =
                    planarLayout(sampleType, destination.width, destination.height);
                std::vector<SampleT> ours(static_cast<std::size_t>(destination.width) * destination.height, SampleT{});
                an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
                ASSERT_TRUE(mask.hasValue());
                an::ValidityMaskView<std::uint8_t> maskView = viewOf(*mask);
                const an::Result<an::GeometryResult> ran =
                    an::transformImageGeometry(execution, sampleType, source.data(), sourceLayout, ours.data(),
                                               destinationLayout, *plan, {}, maskView);
                ASSERT_TRUE(ran.hasValue()) << ran.status().message();

                cv::Mat input(static_cast<int>(size.height), static_cast<int>(size.width), libraryType);
                for (std::uint32_t y = 0u; y < size.height; ++y)
                {
                    std::copy_n(source.data() + static_cast<std::size_t>(y) * size.width, size.width,
                                input.ptr<SampleT>(static_cast<int>(y)));
                }
                cv::Mat output;
                cv::resize(input, output,
                           cv::Size(static_cast<int>(destination.width), static_cast<int>(destination.height)), 0.0,
                           0.0, pair.library);
                std::vector<float> oursAsFloat(ours.size());
                std::vector<float> theirsAsFloat(ours.size());
                for (std::uint32_t y = 0u; y < destination.height; ++y)
                {
                    for (std::uint32_t x = 0u; x < destination.width; ++x)
                    {
                        const std::size_t index = static_cast<std::size_t>(y) * destination.width + x;
                        oursAsFloat[index] = static_cast<float>(ours[index]);
                        theirsAsFloat[index] =
                            static_cast<float>(output.at<SampleT>(static_cast<int>(y), static_cast<int>(x)));
                    }
                }
                expectParity(kDomain,
                             std::string(pair.name) + " " + an::sampleTypeName(sampleType) + " " + sizeLabel(size) +
                                 " -> " + sizeLabel(destination),
                             oursAsFloat, theirsAsFloat, 0.0);
            }
        }
    }
}

TEST(GeometryParityTest, TheBitExactModesReproduceTheLibrarysIntegerPathsBitForBit)
{
    expectBitExactResize<std::uint16_t>(an::SampleType::UInt16, CV_16UC1, 65536u);
    expectBitExactResize<std::uint8_t>(an::SampleType::UInt8, CV_8UC1, 256u);
}

/// One warp interpolation on both sides.
struct WarpMode
{
    an::KernelInterpolation engine;
    int library;
    const char* name;
    double tolAbs;
};

constexpr std::array<WarpMode, 4u> kWarpModes{{
    {an::KernelInterpolation::Nearest, cv::INTER_NEAREST, "nearest", 0.0},
    {an::KernelInterpolation::Linear, cv::INTER_LINEAR, "bilinear", 1.0e-5},
    {an::KernelInterpolation::Cubic, cv::INTER_CUBIC, "bicubic", 2.0e-5},
    {an::KernelInterpolation::Lanczos4, cv::INTER_LANCZOS4, "lanczos4", 5.0e-5},
}};

[[nodiscard]] std::vector<float> engineAffine(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                              std::span<const float> source, PlaneSize size,
                                              const std::array<double, 6>& destinationToSource,
                                              an::KernelInterpolation mode)
{
    const an::Result<an::GeometryPlan> plan = an::planAffineWarp(
        monoDescriptor(size.width, size.height),
        {.width = size.width,
         .height = size.height,
         .destinationToSource = destinationToSource,
         .sampling = {.interpolation = mode, .border = an::KernelBorderMode::Constant, .constantBorder = 0.0f}});
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    if (!plan.hasValue())
    {
        return std::vector<float>(static_cast<std::size_t>(size.width) * size.height, 0.0f);
    }
    return runPlan(engine, execution, *plan, source, size, size);
}

[[nodiscard]] std::vector<float> libraryAffine(std::span<const float> source, PlaneSize size,
                                               const std::array<double, 6>& destinationToSource, int flag)
{
    const cv::Mat input = matrixOf(source, size.width, size.height);
    cv::Mat matrix(2, 3, CV_64FC1);
    for (int row = 0; row < 2; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            matrix.at<double>(row, column) = destinationToSource[static_cast<std::size_t>(row) * 3u + column];
        }
    }
    cv::Mat output;
    cv::warpAffine(input, output, matrix, input.size(), flag | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT,
                   cv::Scalar(0.0));
    return planeOf(output);
}

TEST(GeometryParityTest, TranslationsAtTheLibrarysCoordinateStepAgreeToSinglePrecisionRounding)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // Whole 32nds of a pixel, none of them a half, so the library's fixed-point coordinate is the
    // real one and its nearest choice has no tie to break.
    constexpr std::array<std::array<double, 2u>, 5u> kShifts{{{0.0, 0.0},
                                                              {3.0 / 32.0, 7.0 / 32.0},
                                                              {-11.0 / 32.0, 21.0 / 32.0},
                                                              {2.0 + 29.0 / 32.0, -1.0 - 3.0 / 32.0},
                                                              {-5.0 + 13.0 / 32.0, 4.0 + 27.0 / 32.0}}};
    constexpr std::array<PlaneSize, 3u> kSizes{{{33u, 21u}, {64u, 48u}, {127u, 61u}}};
    for (const WarpMode& mode : kWarpModes)
    {
        for (const PlaneSize size : kSizes)
        {
            const std::vector<float> source = uniformPlane(size.width, size.height, 3000u + size.width);
            for (const std::array<double, 2u>& shift : kShifts)
            {
                const std::array<double, 6> matrix{1.0, 0.0, shift[0], 0.0, 1.0, shift[1]};
                const std::vector<float> ours = engineAffine(engine, execution, source, size, matrix, mode.engine);
                const std::vector<float> theirs = libraryAffine(source, size, matrix, mode.library);
                expectParity(kDomain,
                             std::string("translate ") + mode.name + " " + sizeLabel(size) + " by (" +
                                 std::to_string(shift[0]) + ", " + std::to_string(shift[1]) + ")",
                             ours, theirs, mode.tolAbs);
            }
        }
    }
}

TEST(GeometryParityTest, RotationsAgreeToTheLibrarysFixedPointCoordinateStepTimesTheGradient)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::array<double, 4u> kAngleDegrees{7.0, 33.0, -20.0, 61.5};
    constexpr std::array<PlaneSize, 2u> kSizes{{{96u, 72u}, {127u, 61u}}};
    constexpr std::array<WarpMode, 2u> kModes{{{an::KernelInterpolation::Linear, cv::INTER_LINEAR, "bilinear", 0.0},
                                               {an::KernelInterpolation::Cubic, cv::INTER_CUBIC, "bicubic", 0.0}}};
    for (const WarpMode& mode : kModes)
    {
        for (const PlaneSize size : kSizes)
        {
            const std::vector<float> source = smoothPlane(size.width, size.height, 4000u + size.width);
            // The bound: the library resolves a source coordinate to within half of 1/32 of a
            // pixel, plus 1/1024 from its linear terms, and a coordinate error moves the sampled
            // value by at most that times the steepest gradient of the picture, which the smooth
            // fixture bounds by construction; the cubic window amplifies a difference by up to 1.5.
            double steepest = 0.0;
            for (std::uint32_t y = 0u; y + 1u < size.height; ++y)
            {
                for (std::uint32_t x = 0u; x + 1u < size.width; ++x)
                {
                    const std::size_t index = static_cast<std::size_t>(y) * size.width + x;
                    steepest = (std::max)(steepest, static_cast<double>(std::abs(source[index + 1u] - source[index])));
                    steepest =
                        (std::max)(steepest, static_cast<double>(std::abs(source[index + size.width] - source[index])));
                }
            }
            const double tolerance = 1.5 * steepest * (1.0 / 64.0 + 1.0 / 1024.0) * 2.0 + 1.0e-5;
            for (const double degrees : kAngleDegrees)
            {
                const cv::Point2f centre(static_cast<float>(size.width) / 2.0f, static_cast<float>(size.height) / 2.0f);
                const cv::Mat forward = cv::getRotationMatrix2D(centre, -degrees, 1.0);
                cv::Mat inverse;
                cv::invertAffineTransform(forward, inverse);
                std::array<double, 6> destinationToSource{};
                for (int row = 0; row < 2; ++row)
                {
                    for (int column = 0; column < 3; ++column)
                    {
                        destinationToSource[static_cast<std::size_t>(row) * 3u + column] =
                            inverse.at<double>(row, column);
                    }
                }
                const std::vector<float> ours =
                    engineAffine(engine, execution, source, size, destinationToSource, mode.engine);
                const cv::Mat input = matrixOf(source, size.width, size.height);
                cv::Mat output;
                cv::warpAffine(input, output, forward, input.size(), mode.library, cv::BORDER_CONSTANT,
                               cv::Scalar(0.0));
                const std::vector<float> theirs = planeOf(output);
                expectParity(kDomain,
                             std::string("rotate ") + mode.name + " " + sizeLabel(size) + " by " +
                                 std::to_string(degrees) + " deg (gradient " + std::to_string(steepest) + ")",
                             ours, theirs, tolerance);
            }
        }
    }
}

} // namespace
