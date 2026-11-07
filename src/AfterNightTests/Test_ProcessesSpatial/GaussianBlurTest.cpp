// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Gaussian Blur dialog: the exact Gaussian in an Apply and in a settled frame, held to a Gaussian computed here in
/// double precision - at a sigma the stencil family's widest kernel reaches, at a wider one over a frame too small for
/// one wider kernel, which the dialog composes of exact passes, and at a wider one over a frame large enough, which one
/// exact kernel blurs, or passes of narrower ones where the engine's compiled rows do not run - and, for an interactive
/// frame over the target reduced, to the Gaussian of the sigma reduced with it. An interactive frame's sigma beyond the
/// stencil family's kernel is blurred by the real-time preview's boxes.

#include "Test_ProcessesSpatial/SpatialProcessesTestSupport.h"

#include "processes/GaussianBlurProcess.h"
#include "processes/SpatialProcessSupport.h"
#include "session/ImageDocument.h"
#include "session/PreviewProxies.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>

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

/// A grey scene of stars on a rippled sky, of a size the case chooses, brighter by `band` over the middle third of its
/// columns. A blur of a wide sigma carries the band's edges out to its kernel's reach, where kernels of that sigma cut
/// at different reaches answer differently; the stars and the ripples, which it flattens, leave them to agree.
[[nodiscard]] std::optional<an::ui::ImageDocument>
skyDocument(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height, double band = 0.0)
{
    return makeDocument<float>(
        session, width, height, Channels::Grey, [width, band](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            double value = 0.2 + 0.08 * std::sin(static_cast<double>(x) / 7.0) * std::cos(static_cast<double>(y) / 5.0);
            value += (x >= width / 3u && x < 2u * width / 3u) ? band : 0.0;
            for (std::uint32_t star = 0u; star < 40u; ++star)
            {
                const double dx = static_cast<double>(x) - static_cast<double>((star * 97u) % 400u);
                const double dy = static_cast<double>(y) - static_cast<double>((star * 61u) % 360u);
                value += 0.5 * std::exp(-(dx * dx + dy * dy) / 6.0);
            }
            return static_cast<float>(std::clamp(value, 0.0, 1.0));
        });
}

/// A grey plane's samples, as doubles, a row after another.
[[nodiscard]] std::vector<double> planeOf(const fixtures::FixtureSamples& samples)
{
    std::vector<double> plane(static_cast<std::size_t>(samples.width()) * samples.height());
    for (std::uint32_t y = 0u; y < samples.height(); ++y)
    {
        for (std::uint32_t x = 0u; x < samples.width(); ++x)
        {
            plane[static_cast<std::size_t>(y) * samples.width() + x] = samples.value(x, y, 0u);
        }
    }
    return plane;
}

/// The samples of a run's result; fails the case when there are none.
[[nodiscard]] std::optional<fixtures::FixtureSamples> samplesOf(const an::Result<an::consumer::ImageAsset>& result)
{
    if (!result.hasValue())
    {
        ADD_FAILURE() << result.status().message();
        return std::nullopt;
    }
    std::string error;
    std::optional<fixtures::FixtureSamples> samples = fixtures::engine::readAssetSamples(*result, error);
    if (!samples.has_value())
    {
        ADD_FAILURE() << error;
    }
    return samples;
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

// A sigma wider than the stencil family's widest kernel, over a frame more than twice its kernel's radius each way, is
// blurred by one exact kernel on the compiled path rather than by passes of narrower kernels composed to it: an Apply
// is the Gaussian computed here - its kernel cut at four sigmas and renormalized, as the engine cuts it - to the
// rounding of the Float32 sums the compiled path keeps. A composition of passes cut at their own four sigmas does not
// come within it: across the band's edges it answers by about the mass a Gaussian holds beyond four sigmas, 3.2e-5 of
// the band, several times the bound.
TEST(GaussianBlurTest, AWideSigmaOverALargeEnoughFrameIsOneExactKernel)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    constexpr std::uint32_t kWidth = 400u;
    constexpr std::uint32_t kHeight = 360u;
    constexpr double kSigma = 40.0;
    constexpr double kBand = 0.4;
    ASSERT_GT(static_cast<std::uint32_t>(std::ceil(4.0 * kSigma)), an::kMaxStencilRadius);
    ASSERT_LT(static_cast<std::uint32_t>(std::ceil(4.0 * kSigma)), kHeight / 2u);
    std::optional<an::ui::ImageDocument> document = skyDocument(session, kWidth, kHeight, kBand);
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "sky", std::move(*document));
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target.view().document());
    ASSERT_TRUE(source.has_value());
    const std::vector<double> expected = referenceGaussian(planeOf(*source), kWidth, kHeight, kSigma);

    const ProbedWindow window = openProbed<GaussianBlurProcess>(target.view());
    setReal(*window.window, "sigmaValue", kSigma);
    const std::optional<fixtures::FixtureSamples> applied = samplesOf(runProbeBody(
        *window.probe, session, target.view().document(), an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Apply}));
    ASSERT_TRUE(applied.has_value());
    double largest = 0.0;
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            largest = (std::max)(largest, std::abs(applied->value(x, y, 0u) -
                                                   expected[static_cast<std::size_t>(y) * kWidth + x]));
        }
    }
    EXPECT_LE(largest, 2e-6);
}

// Where the engine's compiled symmetric rows do not run - a processor below AVX2, or an operation held to the scalar
// kernels as here - the engine refuses a kernel wider than the stencil family's, and a sigma that wide is blurred by
// passes of narrower kernels composed to it instead, rather than refused: the Gaussian computed here, to what passes
// cut at their own four sigmas answer across the band's edges - about the mass a Gaussian holds beyond four sigmas,
// 3.2e-5 of the band - and the rounding.
TEST(GaussianBlurTest, AWideSigmaIsComposedWhereTheCompiledRowsDoNotRun)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const an::consumer::EngineSession& session = context->engineSession();
    constexpr std::uint32_t kWidth = 400u;
    constexpr std::uint32_t kHeight = 360u;
    constexpr double kSigma = 40.0;
    constexpr double kBand = 0.4;
    std::optional<an::ui::ImageDocument> document = skyDocument(session, kWidth, kHeight, kBand);
    ASSERT_TRUE(document.has_value());
    const std::optional<fixtures::FixtureSamples> source = documentSamples(*document);
    ASSERT_TRUE(source.has_value());
    const std::vector<double> plane = planeOf(*source);
    const std::vector<double> expected = referenceGaussian(plane, kWidth, kHeight, kSigma);
    std::vector<float> samples(plane.size());
    std::transform(plane.begin(), plane.end(), samples.begin(), [](double value) { return static_cast<float>(value); });

    an::Result<an::consumer::OperationScope> scope = session.admit(an::consumer::OperationRequest{
        .purpose = an::consumer::OperationPurpose::Apply, .instructionSet = an::InstructionSet::Scalar});
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(an::ImageLayoutRequest{.extent = *extent,
                                                                  .elementSizeBytes = sizeof(float),
                                                                  .samplesPerPixel = 1u,
                                                                  .planeCount = 1u,
                                                                  .rowAlignmentBytes = 1u},
                                           std::uint64_t{kWidth} * sizeof(float),
                                           std::uint64_t{kWidth} * sizeof(float) * kHeight, an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    const std::optional<an::ImageDescriptor> described = an::tests::viewer::describeImage(
        scope->engine(), an::SampleType::Float32, kWidth, kHeight, an::tests::viewer::Channels::Grey);
    ASSERT_TRUE(described.has_value());

    // The one wide kernel is refused under this operation.
    const an::Result<an::GaussianPlan> wide = an::planGaussianBlur(an::GaussianRequest{
        .sigmaX = kSigma, .sigmaY = kSigma, .borders = {.border = an::KernelBorderMode::ReflectExcludingEdge}});
    ASSERT_TRUE(wide.hasValue()) << wide.status().message();
    ASSERT_GT(wide->horizontalRadius(), an::kMaxStencilRadius);
    std::vector<float> refused(samples.size(), 0.0f);
    const an::Result<an::SpatialFilterResult> direct = an::blurImageGaussian(
        scope->execution(), an::SampleType::Float32, samples.data(), *layout, refused.data(), *layout, *wide);
    ASSERT_FALSE(direct.hasValue());
    EXPECT_EQ(direct.status().code(), an::ErrorCode::OutOfRange);

    // The output starts as the input, as a process's destination does.
    std::vector<float> blurred = samples;
    const an::Status ran = process_utils::blurGaussian(
        scope->engine(), scope->execution(),
        an::ChannelScopeInput{.descriptor = &*described, .samples = samples.data(), .layout = &*layout, .validity = {}},
        an::ChannelScopeOutput{.descriptor = &*described, .samples = blurred.data(), .layout = &*layout}, kSigma);
    ASSERT_TRUE(ran.ok()) << ran.message();
    double largest = 0.0;
    for (std::size_t index = 0u; index < blurred.size(); ++index)
    {
        largest = (std::max)(largest, std::abs(static_cast<double>(blurred[index]) - expected[index]));
    }
    EXPECT_LE(largest, 5e-5);
}

// An interactive frame's sigma beyond the stencil family's widest kernel is blurred by the real-time preview's boxes,
// whose cost does not grow with the sigma, and the margin a frame over a part reads is their reach; an Apply, and a
// settled frame as the Apply, blur by the exact Gaussian. The two pictures differ, and by little.
TEST(GaussianBlurTest, AnInteractiveFrameBeyondTheStencilsKernelBlursByThePreviewBoxes)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    constexpr std::uint32_t kWidth = 400u;
    constexpr std::uint32_t kHeight = 360u;
    constexpr double kSigma = 60.0;
    std::optional<an::ui::ImageDocument> document = skyDocument(session, kWidth, kHeight);
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "sky", std::move(*document));
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target.view().document());
    ASSERT_TRUE(source.has_value());

    const ProbedWindow window = openProbed<GaussianBlurProcess>(target.view());
    setReal(*window.window, "sigmaValue", kSigma);
    const an::Result<an::GaussianPlan> boxes = an::planRealtimePreviewGaussianBlur(an::RealtimePreviewGaussianRequest{
        .sigmaX = kSigma, .sigmaY = kSigma, .borders = {.border = an::KernelBorderMode::ReflectExcludingEdge}});
    ASSERT_TRUE(boxes.hasValue()) << boxes.status().message();
    EXPECT_EQ(window.probe->regionMargin(), boxes->horizontalRadius());

    // The boxes' blur of the scene, run here.
    std::vector<float> sky(static_cast<std::size_t>(kWidth) * kHeight);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            sky[static_cast<std::size_t>(y) * kWidth + x] = static_cast<float>(source->value(x, y, 0u));
        }
    }
    std::vector<float> blurred(sky.size(), 0.0f);
    {
        an::Result<an::consumer::OperationScope> scope =
            session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
        ASSERT_TRUE(scope.hasValue()) << scope.status().message();
        const std::optional<an::ImageExtent> extent =
            an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
        ASSERT_TRUE(extent.has_value());
        const std::optional<an::ImageLayout> layout = an::ImageLayout::createWithStrides(
            an::ImageLayoutRequest{.extent = *extent,
                                   .elementSizeBytes = sizeof(float),
                                   .samplesPerPixel = 1u,
                                   .planeCount = 1u,
                                   .rowAlignmentBytes = 1u},
            std::uint64_t{kWidth} * sizeof(float), std::uint64_t{kWidth} * sizeof(float) * kHeight,
            an::defaultEngineLimits());
        ASSERT_TRUE(layout.has_value());
        ASSERT_TRUE(an::blurImageRealtimePreviewGaussian(scope->execution(), an::SampleType::Float32, sky.data(),
                                                         *layout, blurred.data(), *layout, *boxes)
                        .hasValue());
    }

    an::ui::ProcessRun interactive{.kind = an::ui::ProcessRunKind::Preview,
                                   .frameKind = an::ui::PreviewFrameKind::Interactive};
    interactive.partOfTarget = true;
    const std::optional<fixtures::FixtureSamples> framed =
        samplesOf(runProbeBody(*window.probe, session, target.view().document(), interactive));
    const std::optional<fixtures::FixtureSamples> applied = samplesOf(runProbeBody(
        *window.probe, session, target.view().document(), an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Apply}));
    const std::optional<fixtures::FixtureSamples> settled = samplesOf(runProbeBody(
        *window.probe, session, target.view().document(),
        an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Preview, .frameKind = an::ui::PreviewFrameKind::Settled}));
    ASSERT_TRUE(framed.has_value() && applied.has_value() && settled.has_value());
    std::uint64_t notTheBoxes = 0u;
    std::uint64_t notTheApply = 0u;
    double largest = 0.0;
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const double frame = framed->value(x, y, 0u);
            notTheBoxes += static_cast<float>(frame) != blurred[static_cast<std::size_t>(y) * kWidth + x] ? 1u : 0u;
            notTheApply += settled->value(x, y, 0u) != applied->value(x, y, 0u) ? 1u : 0u;
            largest = (std::max)(largest, std::abs(frame - applied->value(x, y, 0u)));
        }
    }
    EXPECT_EQ(notTheBoxes, 0u) << "samples of the interactive frame are not the boxes' blur";
    EXPECT_EQ(notTheApply, 0u) << "samples of the settled frame are not the Apply's";
    EXPECT_GT(largest, 0.0) << "the interactive frame blurred as the Apply does";
    EXPECT_LT(largest, 0.02) << "the boxes stray from the Gaussian";
}
