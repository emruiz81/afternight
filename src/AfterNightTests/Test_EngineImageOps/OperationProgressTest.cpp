// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Which operations report progress, and what a report from one of them promises.
//
// The rule these cases hold the engine to: an operation whose work is not one pass over its input -
// an iterative restoration, a patch search over tiles, a decomposition into layers, a model fitted by
// rejection - reports progress through the execution's sink; a single pass over a frame does not,
// because a caller running it already counts the frames. A preview of a denoise or a deconvolution is
// the slow half of a dialog, and every one of these reports progress.
//
// Each case asks the same four things of the reports one call makes: that there were some; that the
// total never changed under the caller; that no report said more was done than there was; and that the
// last report said the work was finished, so a display ends full rather than wherever the final
// checkpoint happened to fall.

#include "SpatialFilterFixture.h"

#include <AfterNightEngine/Algorithms/Background/AutoBackground.h>
#include <AfterNightEngine/Algorithms/Deconvolution/Deconvolution.h>
#include <AfterNightEngine/Algorithms/Detail/DarkStructureEnhance.h>
#include <AfterNightEngine/Algorithms/Multiscale/MultiscaleTransform.h>
#include <AfterNightEngine/Algorithms/NoiseReduction/PatchDenoise.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::planarLayout;
using spatial_test::randomPlane;

/// Every report one operation made, in delivery order.
class ProgressLog final : public an::ProgressSink
{
public:
    void onProgress(const an::ProgressReport& report) noexcept override
    {
        const std::scoped_lock lock(m_mutex);
        m_reports.push_back(report);
        m_stages.emplace_back(report.stage);
    }

    /// Checks the four promises and names the operation in every failure.
    ///
    /// @param operation the entry point the reports came from.
    void expectContract(const char* operation) const
    {
        const std::scoped_lock lock(m_mutex);
        ASSERT_FALSE(m_reports.empty()) << operation << " reported no progress";
        const std::uint64_t total = m_reports.front().total;
        EXPECT_GT(total, 0u) << operation;
        for (std::size_t index = 0u; index < m_reports.size(); ++index)
        {
            EXPECT_EQ(m_reports[index].total, total) << operation << " changed its total at report " << index;
            EXPECT_LE(m_reports[index].completed, m_reports[index].total) << operation;
            EXPECT_FALSE(m_stages[index].empty()) << operation << " reported without saying what it was doing";
        }
        EXPECT_EQ(m_reports.back().completed, total) << operation << " did not end at its total";
    }

    /// How many reports arrived.
    [[nodiscard]] std::size_t count() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_reports.size();
    }

    /// How many different completed counts the display was shown.
    ///
    /// The difference between a bar that moves and one that jumps from empty to full: an operation that
    /// reports every unit of an eight-unit job at a total of one shows two positions however many
    /// reports it makes.
    [[nodiscard]] std::size_t positions() const
    {
        const std::scoped_lock lock(m_mutex);
        std::vector<std::uint64_t> seen;
        for (const an::ProgressReport& report : m_reports)
        {
            if (std::find(seen.begin(), seen.end(), report.completed) == seen.end())
            {
                seen.push_back(report.completed);
            }
        }
        return seen.size();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<an::ProgressReport> m_reports;
    std::vector<std::string> m_stages;
};

/// An execution that reports to a log.
///
/// @param engine the context.
/// @param log where reports go.
/// @return the execution.
[[nodiscard]] an::ExecutionContext reportingExecution(const an::EngineContext& engine, ProgressLog& log)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 3u;
    options.progressSink = &log;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// A frame with structure and noise in it, so every operation has something to do.
[[nodiscard]] std::vector<float> scene(std::uint32_t width, std::uint32_t height, std::uint64_t seed)
{
    std::vector<float> samples = randomPlane(width, height, seed);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double dx = static_cast<double>(x) - 0.4 * width;
            const double dy = static_cast<double>(y) - 0.6 * height;
            float& sample = samples[static_cast<std::size_t>(y) * width + x];
            sample = static_cast<float>(0.25 + 0.4 * std::exp(-(dx * dx + dy * dy) / 40.0) + 0.05 * (sample - 0.5f) +
                                        0.0005 * x);
        }
    }
    return samples;
}

} // namespace

TEST(OperationProgressTest, APatchDenoiseReportsEveryPassOfEveryIteration)
{
    const an::EngineContext engine = makeEngine();
    ProgressLog log;
    const an::ExecutionContext execution = reportingExecution(engine, log);
    constexpr std::uint32_t kSide = 72u;
    const std::vector<float> source = scene(kSide, kSide, 3u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kSide, kSide);
    an::PatchDenoiseRequest request;
    request.sigma = 0.03;
    request.iterations = 2u;
    request.finishing = an::PatchDenoiseFinishing::GuidedDualDomain;
    const an::Result<an::PatchDenoisePlan> plan = an::planPatchDenoise(request, 1u);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    std::vector<float> answer(source.size(), 0.0f);
    const an::Result<an::PatchDenoiseResult> run = an::denoiseImagePatches(
        execution, an::SampleType::Float32, source.data(), layout, answer.data(), layout, *plan);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    log.expectContract("an::denoiseImagePatches");
    // Two passes per iteration and a finishing stage: the bar stops at each of them.
    EXPECT_GE(log.positions(), plan->imagePasses());
}

TEST(OperationProgressTest, AGuidedShrinkageReportsItsWalkOverTheFrame)
{
    const an::EngineContext engine = makeEngine();
    ProgressLog log;
    const an::ExecutionContext execution = reportingExecution(engine, log);
    constexpr std::uint32_t kSide = 64u;
    const std::vector<float> source = scene(kSide, kSide, 5u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kSide, kSide);
    an::GuidedShrinkageRequest request;
    request.sigma = 0.02;
    request.radius = 8u;
    const an::Result<an::GuidedShrinkagePlan> plan = an::planGuidedShrinkage(request);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    std::vector<float> answer(source.size(), 0.0f);
    const an::Result<an::GuidedShrinkageResult> run = an::shrinkImageWithGuide(
        execution, an::SampleType::Float32, source.data(), layout, source.data(), layout, answer.data(), layout, *plan);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    log.expectContract("an::shrinkImageWithGuide");
}

TEST(OperationProgressTest, ARichardsonLucyDeconvolutionReportsEveryIteration)
{
    const an::EngineContext engine = makeEngine();
    ProgressLog log;
    const an::ExecutionContext execution = reportingExecution(engine, log);
    constexpr std::uint32_t kSide = 48u;
    constexpr std::uint32_t kIterations = 6u;
    const std::vector<float> source = scene(kSide, kSide, 7u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kSide, kSide);
    an::PsfRequest psfRequest;
    psfRequest.fwhmX = 2.5;
    psfRequest.fwhmY = 2.5;
    const an::Result<std::uint32_t> side = an::psfExtent(psfRequest);
    ASSERT_TRUE(side.hasValue()) << side.status().message();
    std::vector<float> psf(static_cast<std::size_t>(*side) * *side, 0.0f);
    const std::optional<an::PlaneView<float>> kernel =
        an::PlaneView<float>::create(psf, *side, *side, 1u, static_cast<std::uint64_t>(*side) * sizeof(float));
    ASSERT_TRUE(kernel.has_value());
    ASSERT_TRUE(an::buildPsf(psfRequest, *kernel).ok());
    an::RichardsonLucyRequest request;
    request.iterations = kIterations;
    const std::optional<an::PlaneView<const float>> constKernel = an::PlaneView<const float>::create(
        std::span<const float>(psf), *side, *side, 1u, static_cast<std::uint64_t>(*side) * sizeof(float));
    ASSERT_TRUE(constKernel.has_value());
    const an::Result<an::RichardsonLucyPlan> plan = an::planRichardsonLucy(request, *constKernel);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    std::vector<float> answer(source.size(), 0.0f);
    const an::Result<an::DeconvolutionResult> run = an::deconvolveImageRichardsonLucy(
        execution, an::SampleType::Float32, source.data(), layout, answer.data(), layout, *constKernel, *plan);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    log.expectContract("an::deconvolveImageRichardsonLucy");
    EXPECT_GE(log.positions(), kIterations);
}

TEST(OperationProgressTest, AMultiscaleDecompositionReportsEveryLayer)
{
    const an::EngineContext engine = makeEngine();
    ProgressLog log;
    const an::ExecutionContext execution = reportingExecution(engine, log);
    constexpr std::uint32_t kSide = 64u;
    const std::vector<float> source = scene(kSide, kSide, 9u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kSide, kSide);
    an::MultiscaleTransformRequest request{};
    request.algorithm = an::MultiscaleAlgorithm::Starlet;
    request.layerCount = 5u;
    const an::Result<an::MultiscaleTransformPlan> plan = an::planMultiscaleTransform(request);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    const an::Result<an::MultiscaleDecomposition> decomposed =
        an::decomposeImageMultiscale(engine, execution, an::SampleType::Float32, source.data(), layout, *plan);
    ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();
    log.expectContract("an::decomposeImageMultiscale");
    EXPECT_GE(log.positions(), plan->detailLayerCount());
}

TEST(OperationProgressTest, ADarkStructureEnhancementReportsItsDecompositions)
{
    const an::EngineContext engine = makeEngine();
    ProgressLog log;
    const an::ExecutionContext execution = reportingExecution(engine, log);
    constexpr std::uint32_t kSide = 96u;
    const std::vector<float> source = scene(kSide, kSide, 11u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kSide, kSide);
    an::DarkStructureRequest request{};
    request.removedLayers = an::kMinDarkStructureRemovedLayers;
    const an::Result<an::DarkStructurePlan> plan = an::planDarkStructure(request);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    std::vector<float> enhanced(source.size(), 0.0f);
    const an::Result<an::DarkStructureResult> applied = an::enhanceImageDarkStructures(
        engine, execution, an::SampleType::Float32, source.data(), layout, enhanced.data(), layout, *plan);
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();
    log.expectContract("an::enhanceImageDarkStructures");
    // The mask, its denoise, and every enhancement iteration.
    EXPECT_GE(log.positions(), 2u + request.iterations);
}

TEST(OperationProgressTest, AnAutomaticBackgroundReportsEveryFittingRound)
{
    const an::EngineContext engine = makeEngine();
    ProgressLog log;
    const an::ExecutionContext execution = reportingExecution(engine, log);
    constexpr std::uint32_t kWidth = 128u;
    constexpr std::uint32_t kHeight = 96u;
    const std::vector<float> source = scene(kWidth, kHeight, 13u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    an::AutoBackgroundRequest request{};
    request.model = an::AutoBackgroundModel::Polynomial;
    request.polynomialDegree = 2u;
    request.downsample = 4u;
    request.protectStructures = false;
    const an::Result<an::AutoBackgroundPlan> plan = an::planAutoBackground(request, kWidth, kHeight);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    std::vector<float> surface(source.size(), 0.0f);
    const std::optional<an::PlaneView<float>> plane =
        an::PlaneView<float>::create(surface, kWidth, kHeight, 1u, static_cast<std::uint64_t>(kWidth) * sizeof(float));
    ASSERT_TRUE(plane.has_value());
    const std::array<an::PlaneView<float>, 1u> models{*plane};
    const an::Result<an::AutoBackgroundResult> estimated =
        an::estimateImageAutoBackground(execution, an::SampleType::Float32, source.data(), layout, *plan, models);
    ASSERT_TRUE(estimated.hasValue()) << estimated.status().message();
    log.expectContract("an::estimateImageAutoBackground");
    EXPECT_GE(log.positions(), estimated->iterationsRun);
}
