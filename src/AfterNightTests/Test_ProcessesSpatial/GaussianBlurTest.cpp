// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Gaussian Blur dialog: the exact Gaussian in every frame as in an Apply, held to a Gaussian computed here in
/// double precision - at a sigma one kernel reaches and at one wider than any kernel, which the dialog offers and the
/// engine composes - and, for an interactive frame over the target reduced, to the Gaussian of the sigma reduced with
/// it.

#include "Test_ProcessesSpatial/SpatialProcessesTestSupport.h"

#include "processes/GaussianBlurProcess.h"
#include "session/ImageDocument.h"
#include "session/PreviewProxies.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>

#include <QString>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::spatial;

/// The index a sample outside a line of a length reads: the line mirrored about its edge sample, without repeating it,
/// as far as the reach goes.
[[nodiscard]] std::int64_t mirrored(std::int64_t index, std::int64_t length)
{
    if (length == 1)
    {
        return 0;
    }
    const std::int64_t period = 2 * (length - 1);
    std::int64_t folded = index % period;
    if (folded < 0)
    {
        folded += period;
    }
    return folded < length ? folded : period - folded;
}

/// The exact Gaussian of a plane in double precision: a tap is the density at the sample's centre, the kernel is cut
/// at four sigmas and renormalized, and a neighbour outside the plane is its mirror image about the edge sample.
[[nodiscard]] std::vector<double> referenceGaussian(const std::vector<double>& plane, std::int64_t width,
                                                    std::int64_t height, double sigma)
{
    const auto radius = static_cast<std::int64_t>(std::ceil(4.0 * sigma));
    std::vector<double> taps(static_cast<std::size_t>(2 * radius + 1));
    double total = 0.0;
    for (std::int64_t k = -radius; k <= radius; ++k)
    {
        taps[static_cast<std::size_t>(k + radius)] = std::exp(-0.5 * static_cast<double>(k * k) / (sigma * sigma));
        total += taps[static_cast<std::size_t>(k + radius)];
    }
    for (double& tap : taps)
    {
        tap /= total;
    }
    std::vector<double> across(plane.size());
    for (std::int64_t y = 0; y < height; ++y)
    {
        for (std::int64_t x = 0; x < width; ++x)
        {
            double sum = 0.0;
            for (std::int64_t k = -radius; k <= radius; ++k)
            {
                sum += taps[static_cast<std::size_t>(k + radius)] *
                       plane[static_cast<std::size_t>(y * width + mirrored(x + k, width))];
            }
            across[static_cast<std::size_t>(y * width + x)] = sum;
        }
    }
    std::vector<double> blurred(plane.size());
    for (std::int64_t y = 0; y < height; ++y)
    {
        for (std::int64_t x = 0; x < width; ++x)
        {
            double sum = 0.0;
            for (std::int64_t k = -radius; k <= radius; ++k)
            {
                sum += taps[static_cast<std::size_t>(k + radius)] *
                       across[static_cast<std::size_t>(mirrored(y + k, height) * width + x)];
            }
            blurred[static_cast<std::size_t>(y * width + x)] = sum;
        }
    }
    return blurred;
}

/// What an Apply of the dialog at a sigma shows over the wide stars, and the reference Gaussian of the same samples;
/// fails the case when either cannot be had.
void applyAgainstReference(double sigma, double tolerance)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document =
        fixtureDocument(context->engineSession(), "stars-mono-float32-wide");
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "stars", std::move(*document));
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target.view().document());
    ASSERT_TRUE(source.has_value());
    const std::int64_t width = source->width();
    const std::int64_t height = source->height();
    std::vector<double> plane(static_cast<std::size_t>(width * height));
    for (std::int64_t y = 0; y < height; ++y)
    {
        for (std::int64_t x = 0; x < width; ++x)
        {
            plane[static_cast<std::size_t>(y * width + x)] =
                source->value(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 0u);
        }
    }
    const std::vector<double> expected = referenceGaussian(plane, width, height, sigma);

    SpatialHandle<GaussianBlurProcess> process = openSpatial<GaussianBlurProcess>(target.view());
    setChecked(*process, "preview", false);
    setReal(*process, "sigmaValue", sigma);
    process->applyProcess();
    ASSERT_TRUE(settleRun(*process, target.view()));
    ASSERT_EQ(process->runTelemetry().runsCommitted, 1u)
        << context->errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<fixtures::FixtureSamples> shown = documentSamples(target.view().document());
    ASSERT_TRUE(shown.has_value());
    double largest = 0.0;
    for (std::int64_t y = 0; y < height; ++y)
    {
        for (std::int64_t x = 0; x < width; ++x)
        {
            const double difference = shown->value(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 0u) -
                                      expected[static_cast<std::size_t>(y * width + x)];
            largest = (std::max)(largest, std::abs(difference));
        }
    }
    EXPECT_LE(largest, tolerance) << "sigma " << sigma;
}

} // namespace

// An Apply at a sigma one kernel reaches is the exact Gaussian, to the rounding of a Float32 sample.
TEST(GaussianBlurTest, AnApplyIsTheExactGaussian)
{
    applyAgainstReference(1.5, 2e-6);
    applyAgainstReference(6.0, 2e-6);
}

// A sigma wider than the widest kernel - the dialog offers up to a hundred, a kernel reaches thirty-two - is composed
// of as many exact passes as it needs, and is the Gaussian of that sigma to within what each pass's cut-off leaves out.
TEST(GaussianBlurTest, ABlurWiderThanAnyKernelIsStillTheGaussian)
{
    applyAgainstReference(40.0, 2e-4);
}

// An Apply on a sixteen-bit image is the Gaussian of the codes' fractions, rounded to the nearest code: the plane is
// worked on a unit-interval Float32 copy, where the compiled kernels run, and narrowed back once - at a sigma one
// kernel reaches and at one composed of several passes.
TEST(GaussianBlurTest, AnApplyOnASixteenBitImageIsTheGaussianToACode)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> floating =
        fixtureDocument(context->engineSession(), "stars-mono-float32-wide");
    ASSERT_TRUE(floating.has_value());
    const std::optional<fixtures::FixtureSamples> stars = documentSamples(*floating);
    ASSERT_TRUE(stars.has_value());
    const std::uint32_t width = stars->width();
    const std::uint32_t height = stars->height();
    constexpr double kCodes = 65535.0;
    std::vector<double> plane(static_cast<std::size_t>(width) * height);
    std::optional<an::ui::ImageDocument> document = makeDocument<std::uint16_t>(
        context->engineSession(), width, height, Channels::Grey, [&](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            const double fraction = std::clamp(stars->value(x, y, 0u), 0.0, 1.0);
            const auto code = static_cast<std::uint16_t>(std::llround(fraction * kCodes));
            plane[static_cast<std::size_t>(y) * width + x] = static_cast<double>(code) / kCodes;
            return code;
        });
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "stars", std::move(*document));
    for (const auto& [sigma, tolerance] : {std::pair{2.5, 2e-6}, std::pair{40.0, 2e-4}})
    {
        SCOPED_TRACE(::testing::Message() << "sigma " << sigma);
        const std::vector<double> expected = referenceGaussian(plane, width, height, sigma);
        const ProbedWindow window = openProbed<GaussianBlurProcess>(target.view());
        setReal(*window.window, "sigmaValue", sigma);
        const an::Result<an::consumer::ImageAsset> applied =
            runProbeBody(*window.probe, context->engineSession(), target.view().document(),
                         an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Apply});
        ASSERT_TRUE(applied.hasValue()) << applied.status().message();
        std::string error;
        const std::optional<fixtures::FixtureSamples> shown = fixtures::engine::readAssetSamples(*applied, error);
        ASSERT_TRUE(shown.has_value()) << error;
        ASSERT_EQ(shown->sampleType(), fixtures::FixtureSampleType::UInt16);
        double largest = 0.0;
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double difference =
                    shown->value(x, y, 0u) / kCodes - expected[static_cast<std::size_t>(y) * width + x];
                largest = (std::max)(largest, std::abs(difference));
            }
        }
        EXPECT_LE(largest, 0.5 / kCodes + tolerance) << "within half a code of the Gaussian";
    }
}

// An interactive frame over the target reduced by the preview view's factor blurs the reduction by the sigma reduced
// with it, so it shows what the whole result's reduction shows; a frame over a part of the target at full scale blurs
// by the dialog's sigma, as an Apply does.
TEST(GaussianBlurTest, AFrameOverAReductionBlursItByTheSigmaReduced)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    std::optional<an::ui::ImageDocument> document = fixtureDocument(session, "stars-mono-float32-wide");
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "stars", std::move(*document));
    constexpr std::uint32_t kFactor = 2u;
    constexpr double kSigma = 3.0;
    std::optional<an::ui::ImageDocument> reduced;
    {
        an::Result<an::consumer::OperationScope> scope =
            session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
        ASSERT_TRUE(scope.hasValue()) << scope.status().message();
        an::Result<an::consumer::AssetReadLease> lease = target.view().document().read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        an::Result<an::consumer::ImageAsset> made =
            an::ui::reduceForPreview(session, scope->engine(), scope->execution(), *lease, kFactor);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        an::Result<an::ui::ImageDocument> held = an::ui::ImageDocument::create(
            std::move(*made), an::ui::DocumentNames{.displayName = QStringLiteral("reduced")});
        ASSERT_TRUE(held.hasValue()) << held.status().message();
        reduced.emplace(std::move(*held));
    }
    const std::optional<fixtures::FixtureSamples> source = documentSamples(*reduced);
    ASSERT_TRUE(source.has_value());
    const std::int64_t width = source->width();
    const std::int64_t height = source->height();
    std::vector<double> plane(static_cast<std::size_t>(width * height));
    for (std::int64_t y = 0; y < height; ++y)
    {
        for (std::int64_t x = 0; x < width; ++x)
        {
            plane[static_cast<std::size_t>(y * width + x)] =
                source->value(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 0u);
        }
    }

    const ProbedWindow window = openProbed<GaussianBlurProcess>(target.view());
    setReal(*window.window, "sigmaValue", kSigma);
    // The largest distance between what the body makes of the reduction under a run and the reference Gaussian of a
    // sigma; one, which fails every bound, when the body cannot be run.
    const auto largestAgainst = [&](const an::ui::ProcessRun& run, double sigma) {
        const an::Result<an::consumer::ImageAsset> framed = runProbeBody(*window.probe, session, *reduced, run);
        if (!framed.hasValue())
        {
            ADD_FAILURE() << framed.status().message();
            return 1.0;
        }
        std::string error;
        const std::optional<fixtures::FixtureSamples> shown = fixtures::engine::readAssetSamples(*framed, error);
        if (!shown.has_value())
        {
            ADD_FAILURE() << error;
            return 1.0;
        }
        const std::vector<double> expected = referenceGaussian(plane, width, height, sigma);
        double largest = 0.0;
        for (std::int64_t y = 0; y < height; ++y)
        {
            for (std::int64_t x = 0; x < width; ++x)
            {
                const double difference =
                    shown->value(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 0u) -
                    expected[static_cast<std::size_t>(y * width + x)];
                largest = (std::max)(largest, std::abs(difference));
            }
        }
        return largest;
    };
    an::ui::ProcessRun overReduction{.kind = an::ui::ProcessRunKind::Preview,
                                     .frameKind = an::ui::PreviewFrameKind::Interactive};
    overReduction.proxyFactor = kFactor;
    EXPECT_LE(largestAgainst(overReduction, kSigma / kFactor), 2e-6) << "over the reduction, the sigma reduced";
    an::ui::ProcessRun overPart{.kind = an::ui::ProcessRunKind::Preview,
                                .frameKind = an::ui::PreviewFrameKind::Interactive};
    overPart.partOfTarget = true;
    EXPECT_LE(largestAgainst(overPart, kSigma), 2e-6) << "over a part at full scale, the dialog's sigma";
}
