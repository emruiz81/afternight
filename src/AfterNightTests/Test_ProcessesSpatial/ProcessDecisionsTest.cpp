// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What each spatial and noise process decides beyond the numbers its records hold: the noise generator's seed, held
/// across previews and drawn again after an Apply, and the moments it draws; every morphology mode, its circular
/// elements and the selection mode the engine's footprint rank makes possible; the G'MIC processes with and without the
/// library; the patch denoiser's cap and its refusal of a frame too small for its search; the bilateral filter's
/// derived radius; a colour CLAHE that equalizes the lightness alone, and an equalized result recorded nonlinear where
/// its body wrote it; and the amount a modulated process's parameters start at.

#include "Test_ProcessesSpatial/SpatialProcessesTestSupport.h"

#include "processes/BilateralFilterProcess.h"
#include "processes/CLAHEProcess.h"
#include "processes/CosmeticCorrectionProcess.h"
#include "processes/GRAYCstorationProcess.h"
#include "processes/GmicProcessSupport.h"
#include "processes/LocalContrastEnhancementProcess.h"
#include "processes/LocalHistogramEqualizationProcess.h"
#include "processes/MedianFilterProcess.h"
#include "processes/MorphologicalTransformationProcess.h"
#include "processes/NLBayesProcess.h"
#include "processes/NoiseGeneratorProcess.h"
#include "processes/SharpenAlphaProcess.h"
#include "processes/SpatialProcessSupport.h"
#include "processes/TightenEdgesProcess.h"
#include "processes/WaveletDenoiseProcess.h"

#include "controls/ProgressDialogControl.h"

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Algorithms/NoiseReduction/PatchDenoise.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>

#include <QDialogButtonBox>
#include <QPushButton>
#include <QSpinBox>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::spatial;

/// What an Apply of a dialog, set by `configure`, shows over a fresh view of a document; fails the case when the run
/// does not commit.
template <typename ProcessT>
[[nodiscard]] std::optional<fixtures::FixtureSamples> appliedOver(ProcessTestContext& context,
                                                                  an::ui::ImageDocument&& document,
                                                                  const std::function<void(QWidget&)>& configure)
{
    const TargetView target(context, "target", std::move(document));
    SpatialHandle<ProcessT> process = openSpatial<ProcessT>(target.view());
    setChecked(*process, "preview", false);
    configure(*process);
    process->applyProcess();
    EXPECT_TRUE(settleRun(*process, target.view()));
    EXPECT_EQ(process->runTelemetry().runsCommitted, 1u) << context.errorTexts.join(QStringLiteral("; ")).toStdString();
    return documentSamples(target.view().document());
}

/// What an Apply of a dialog shows over a fixture.
template <typename ProcessT>
[[nodiscard]] std::optional<fixtures::FixtureSamples> appliedOver(ProcessTestContext& context, std::string_view fixture,
                                                                  const std::function<void(QWidget&)>& configure)
{
    std::optional<an::ui::ImageDocument> document = fixtureDocument(context.engineSession(), fixture);
    if (!document.has_value())
    {
        return std::nullopt;
    }
    return appliedOver<ProcessT>(context, std::move(*document), configure);
}

/// Whether two images hold the same samples, a NaN equal to a NaN.
[[nodiscard]] bool identicalSamples(const fixtures::FixtureSamples& left, const fixtures::FixtureSamples& right)
{
    if (left.width() != right.width() || left.height() != right.height() || left.channelCount() != right.channelCount())
    {
        return false;
    }
    for (std::uint32_t c = 0u; c < left.channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < left.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < left.width(); ++x)
            {
                const double a = left.value(x, y, c);
                const double b = right.value(x, y, c);
                if (!(a == b || (std::isnan(a) && std::isnan(b))))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

/// Clicks a window's push button by its object name.
void click(const QWidget& window, const char* name)
{
    if (auto* button = widgetNamed<QPushButton>(window, name))
    {
        button->click();
    }
}

/// The Apply button of a window's button box.
[[nodiscard]] QPushButton* applyButton(const QWidget& window)
{
    auto* buttons = widgetNamed<QDialogButtonBox>(window, "buttonBox");
    return buttons != nullptr ? buttons->button(QDialogButtonBox::Apply) : nullptr;
}

/// One component of a colour document's samples, as the engine's channel scope extracts it for a process.
[[nodiscard]] std::optional<fixtures::FixtureSamples>
componentOf(const an::consumer::EngineSession& session, const an::ui::ImageDocument& document, an::ChannelScope scope)
{
    std::optional<an::consumer::OperationScope> operation = an::tests::viewer::admitted(session);
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    if (!operation.has_value() || !lease.hasValue())
    {
        ADD_FAILURE() << "the document cannot be read";
        return std::nullopt;
    }
    std::optional<fixtures::FixtureSamples> component;
    std::visit(
        [&](const auto& image) {
            const std::optional<an::ImageDescriptor> described = image.descriptor();
            const auto view = image.view();
            ASSERT_TRUE(described.has_value() && view.has_value());
            const an::Result<an::ChannelScopePlan> plan =
                an::planChannelScope(an::ui::processChannelScopeRequest(scope, *described));
            ASSERT_TRUE(plan.hasValue()) << plan.status().message();
            const an::Result<an::ImageDescriptor> planeDescriptor = an::channelScopePlaneDescriptor(*plan, *described);
            ASSERT_TRUE(planeDescriptor.hasValue()) << planeDescriptor.status().message();
            ASSERT_EQ(planeDescriptor->sampleType(), an::SampleType::Float32);
            an::Result<an::Image<float>> plane =
                an::Image<float>::create(operation->engine(), *planeDescriptor, an::StorageArrangement::Planar,
                                         an::StorageInitialization::Zeroed);
            ASSERT_TRUE(plane.hasValue()) << plane.status().message();
            {
                std::optional<an::ImageWriteAccess<float>> access = plane->write();
                ASSERT_TRUE(access.has_value());
                const an::Result<an::ChannelScopeResult> extracted = an::extractChannelScope(
                    operation->execution(), *plan,
                    an::ChannelScopeSource{
                        .descriptor = &*described, .samples = view->data(), .layout = &view->layout()},
                    access->view().data(), access->view().layout());
                ASSERT_TRUE(extracted.hasValue()) << extracted.status().message();
            }
            std::string error;
            component = fixtures::engine::readImageSamples(an::consumer::AnyImage(std::move(*plane)), error);
            EXPECT_TRUE(component.has_value()) << error;
        },
        lease->image());
    return component;
}

/// The widths of the runs of cells the circular elements switch on, row by row, for each of the sides the dialog
/// offers; every run is centred.
const std::vector<std::vector<std::size_t>>& circleRuns()
{
    static const std::vector<std::vector<std::size_t>> circles{
        {3, 3, 3},
        {3, 5, 5, 5, 3},
        {3, 5, 7, 7, 7, 5, 3},
        {5, 7, 9, 9, 9, 9, 9, 7, 5},
        {5, 7, 9, 11, 11, 11, 11, 11, 9, 7, 5},
        {5, 9, 11, 11, 13, 13, 13, 13, 13, 11, 11, 9, 5},
        {5, 9, 11, 13, 13, 15, 15, 15, 15, 15, 13, 13, 11, 9, 5},
        {5, 9, 13, 13, 15, 15, 17, 17, 17, 17, 17, 15, 15, 13, 13, 9, 5},
        {7, 11, 13, 15, 17, 17, 19, 19, 19, 19, 19, 19, 19, 17, 17, 15, 13, 11, 7},
        {7, 11, 13, 15, 17, 19, 19, 21, 21, 21, 21, 21, 21, 21, 19, 19, 17, 15, 13, 11, 7},
        {7, 11, 15, 17, 19, 19, 21, 21, 23, 23, 23, 23, 23, 23, 23, 21, 21, 19, 19, 17, 15, 11, 7},
        {7, 11, 15, 17, 19, 21, 21, 23, 23, 25, 25, 25, 25, 25, 25, 25, 23, 23, 21, 21, 19, 17, 15, 11, 7},
    };
    return circles;
}

/// Sets the morphology dialog to an operator, an element side and the circular element of that side.
[[nodiscard]] std::function<void(QWidget&)> morphology(const QString& operatorText, int side, int iterations = 1,
                                                       double selection = 0.5)
{
    return [operatorText, side, iterations, selection](QWidget& window) {
        choose(window, "operatorSelector", operatorText);
        choose(window, "seSizeSelector", QStringLiteral("%1 (%2 elements)").arg(side).arg(side * side));
        click(window, "seCircularButton");
        setWhole(window, "iterationsValue", iterations);
        setReal(window, "selectionValue", selection);
    };
}

} // namespace

// The seed is drawn when the dialog opens and held by every preview and run until an Apply completes, which draws the
// next: a preview shows the noise the Apply adds, and two Applies add two different draws.
// A process whose amount the framework blends its result toward the source by starts its parameters at the amount its
// dialog opens at - the whole result - so a run made from default parameters is not blended away.
TEST(ModulationTest, AModulatedProcesssParametersStartAtTheAmountItsDialogOpensAt)
{
    EXPECT_DOUBLE_EQ(BilateralFilterParams{}.amount, 1.0);
    EXPECT_DOUBLE_EQ(CLAHEParams{}.amount, 1.0);
    EXPECT_DOUBLE_EQ(CosmeticCorrectionParams{}.amount, 1.0);
    EXPECT_DOUBLE_EQ(GRAYCstorationParams{}.amount, 1.0);
    EXPECT_DOUBLE_EQ(LocalContrastEnhancementParams{}.amount, 1.0);
    EXPECT_DOUBLE_EQ(LocalHistogramEqualizationParams{}.amount, 1.0);
    EXPECT_DOUBLE_EQ(MorphologicalTransformationParams{}.amount, 1.0);
    EXPECT_DOUBLE_EQ(NLBayesParams{}.amount, 1.0);
    EXPECT_DOUBLE_EQ(SharpenAlphaParams{}.amount, 1.0);
    EXPECT_DOUBLE_EQ(TightenEdgesParams{}.amount, 1.0);
    EXPECT_DOUBLE_EQ(WaveletDenoiseParams{}.amount, 1.0);
}

TEST(NoiseGeneratorTest, ASeedIsHeldUntilAnApplyCompletesAndDrawnAgainAfter)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = fixtureDocument(context->engineSession(), "stars-mono-float32");
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "stars", std::move(*document));
    const ProbedWindow window = openProbed<NoiseGeneratorProcess>(target.view());
    setChecked(*window, "preview", false);
    choose(*window, "noiseTypeSelector", QStringLiteral("Gaussian"));
    const an::ui::ProcessRun apply{.kind = an::ui::ProcessRunKind::Apply};

    const an::Result<an::consumer::ImageAsset> first =
        runProbeBody(*window.probe, context->engineSession(), target.view().document(), apply);
    const an::Result<an::consumer::ImageAsset> second =
        runProbeBody(*window.probe, context->engineSession(), target.view().document(), apply);
    ASSERT_TRUE(first.hasValue() && second.hasValue());
    std::string error;
    const std::optional<fixtures::FixtureSamples> firstSamples = fixtures::engine::readAssetSamples(*first, error);
    const std::optional<fixtures::FixtureSamples> secondSamples = fixtures::engine::readAssetSamples(*second, error);
    ASSERT_TRUE(firstSamples.has_value() && secondSamples.has_value()) << error;
    EXPECT_TRUE(identicalSamples(*firstSamples, *secondSamples)) << "the same seed draws the same noise";

    window->applyProcess();
    ASSERT_TRUE(settleRun(*window, target.view()));
    ASSERT_EQ(window->runTelemetry().runsCommitted, 1u) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<fixtures::FixtureSamples> applied = documentSamples(target.view().document());
    ASSERT_TRUE(applied.has_value());
    EXPECT_TRUE(identicalSamples(*applied, *firstSamples)) << "the Apply added the noise the runs before it drew";

    std::optional<an::ui::ImageDocument> again = fixtureDocument(context->engineSession(), "stars-mono-float32");
    ASSERT_TRUE(again.has_value());
    const an::Result<an::consumer::ImageAsset> next =
        runProbeBody(*window.probe, context->engineSession(), *again, apply);
    ASSERT_TRUE(next.hasValue());
    const std::optional<fixtures::FixtureSamples> nextSamples = fixtures::engine::readAssetSamples(*next, error);
    ASSERT_TRUE(nextSamples.has_value()) << error;
    EXPECT_FALSE(identicalSamples(*nextSamples, *firstSamples)) << "the Apply drew the next seed";
}

// The Gaussian kind adds noise of the mean and variance the dialog states, in fractions of full scale.
TEST(NoiseGeneratorTest, GaussianNoiseHasTheMomentsTheDialogStates)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> flat =
        greyDocument(context->engineSession(), 128u, 96u, [](std::uint32_t, std::uint32_t) { return 0.5f; });
    ASSERT_TRUE(flat.has_value());
    const std::optional<fixtures::FixtureSamples> noisy =
        appliedOver<NoiseGeneratorProcess>(*context, std::move(*flat), [](QWidget& window) {
            choose(window, "noiseTypeSelector", QStringLiteral("Gaussian"));
            setReal(window, "meanValue", 0.0);
            setReal(window, "varValue", 0.01);
        });
    ASSERT_TRUE(noisy.has_value());
    double sum = 0.0;
    double squares = 0.0;
    const double count = 128.0 * 96.0;
    for (std::uint32_t y = 0u; y < 96u; ++y)
    {
        for (std::uint32_t x = 0u; x < 128u; ++x)
        {
            sum += noisy->value(x, y, 0u);
            squares += noisy->value(x, y, 0u) * noisy->value(x, y, 0u);
        }
    }
    const double mean = sum / count;
    const double variance = squares / count - mean * mean;
    EXPECT_NEAR(mean, 0.5, 0.003);
    EXPECT_NEAR(variance, 0.01, 0.0006);
}

// The circular element of every side the dialog offers switches on the stated runs, cell for cell.
TEST(MorphologyTest, TheCircularElementsSwitchOnTheStatedRuns)
{
    for (const std::vector<std::size_t>& rows : circleRuns())
    {
        const std::size_t side = rows.size();
        SCOPED_TRACE(side);
        const an::ui::StructuringElementGrid element = MorphologicalTransformationProcess::circleElement(side);
        ASSERT_EQ(element.size(), side);
        for (std::size_t row = 0u; row < side; ++row)
        {
            for (std::size_t column = 0u; column < side; ++column)
            {
                const std::size_t distance = column > side / 2u ? column - side / 2u : side / 2u - column;
                EXPECT_EQ(element.at(row, column), distance <= rows[row] / 2u) << row << "," << column;
            }
        }
    }
}

// The selection mode ranks the element's cells: its lowest rank is the erosion by the element, its highest the
// dilation, and its median over a whole three-by-three square is the median filter's.
TEST(MorphologyTest, TheSelectionModeIsTheRankTheErosionAndDilationAreTheExtremesOf)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const QString selection = QStringLiteral("Morphological Selection");
    const auto eroded = appliedOver<MorphologicalTransformationProcess>(
        *context, "stars-mono-float32", morphology(QStringLiteral("Erosion (Minimum)"), 5));
    const auto lowest = appliedOver<MorphologicalTransformationProcess>(*context, "stars-mono-float32",
                                                                        morphology(selection, 5, 1, 0.0));
    const auto dilated = appliedOver<MorphologicalTransformationProcess>(
        *context, "stars-mono-float32", morphology(QStringLiteral("Dilation (Maximum)"), 5));
    const auto highest = appliedOver<MorphologicalTransformationProcess>(*context, "stars-mono-float32",
                                                                         morphology(selection, 5, 1, 1.0));
    ASSERT_TRUE(eroded.has_value() && lowest.has_value() && dilated.has_value() && highest.has_value());
    EXPECT_TRUE(identicalSamples(*eroded, *lowest));
    EXPECT_TRUE(identicalSamples(*dilated, *highest));

    const auto median =
        appliedOver<MorphologicalTransformationProcess>(*context, "stars-mono-float32", [&](QWidget& window) {
            choose(window, "operatorSelector", selection);
            choose(window, "seSizeSelector", QStringLiteral("3 (9 elements)"));
            click(window, "seFillButton");
            setReal(window, "selectionValue", 0.5);
        });
    const auto filtered = appliedOver<MedianFilterProcess>(
        *context, "stars-mono-float32", [](QWidget& window) { choose(window, "kernelSize", QStringLiteral("3x3")); });
    ASSERT_TRUE(median.has_value() && filtered.has_value());
    EXPECT_TRUE(identicalSamples(*median, *filtered));
}

// Iterations are as many as the dialog asks for, past the sixteen one engine call repeats: twenty erosions by a whole
// three-by-three square are the minimum over the forty-one-wide square inside the image.
TEST(MorphologyTest, IterationsPastOneCallsRepeatsAreAllApplied)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document =
        fixtureDocument(context->engineSession(), "stars-mono-float32-wide");
    ASSERT_TRUE(document.has_value());
    const std::optional<fixtures::FixtureSamples> source = documentSamples(*document);
    ASSERT_TRUE(source.has_value());
    const auto eroded =
        appliedOver<MorphologicalTransformationProcess>(*context, std::move(*document), [](QWidget& window) {
            choose(window, "operatorSelector", QStringLiteral("Erosion (Minimum)"));
            choose(window, "seSizeSelector", QStringLiteral("3 (9 elements)"));
            click(window, "seFillButton");
            setWhole(window, "iterationsValue", 20);
        });
    ASSERT_TRUE(eroded.has_value());
    const auto width = static_cast<std::int64_t>(source->width());
    const auto height = static_cast<std::int64_t>(source->height());
    for (std::int64_t y = 0; y < height; ++y)
    {
        for (std::int64_t x = 0; x < width; ++x)
        {
            double lowest = std::numeric_limits<double>::infinity();
            for (std::int64_t v = (std::max)(std::int64_t{0}, y - 20); v <= (std::min)(height - 1, y + 20); ++v)
            {
                for (std::int64_t u = (std::max)(std::int64_t{0}, x - 20); u <= (std::min)(width - 1, x + 20); ++u)
                {
                    lowest = (std::min)(lowest, source->value(static_cast<std::uint32_t>(u),
                                                              static_cast<std::uint32_t>(v), 0u));
                }
            }
            ASSERT_EQ(eroded->value(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 0u), lowest)
                << x << "," << y;
        }
    }
}

// An element with no cell switched on has nothing to apply: Apply is unavailable until a cell is.
TEST(MorphologyTest, AnEmptyElementCannotBeApplied)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = fixtureDocument(context->engineSession(), "stars-mono-float32");
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "stars", std::move(*document));
    SpatialHandle<MorphologicalTransformationProcess> process =
        openSpatial<MorphologicalTransformationProcess>(target.view());
    QPushButton* apply = applyButton(*process);
    ASSERT_NE(apply, nullptr);
    EXPECT_TRUE(apply->isEnabled());
    click(*process, "seClearButton");
    EXPECT_FALSE(apply->isEnabled());
    click(*process, "seCrossButton");
    EXPECT_TRUE(apply->isEnabled());
}

// The six G'MIC dialogs run their command where the library is built in; where it is not, each says so and offers
// neither a preview nor an Apply, and a run reaching it is refused by name.
TEST(GmicProcessTest, TheDialogsRunWithTheLibraryAndSayWhyWithoutIt)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::size_t dialogs = 0u;
    for (const SpatialProcessSpec& spec : spatialProcesses())
    {
        if (!spec.gmic)
        {
            continue;
        }
        ++dialogs;
        SCOPED_TRACE(spec.action);
        std::optional<an::ui::ImageDocument> document = fixtureDocument(context->engineSession(), "stars-mono-float32");
        ASSERT_TRUE(document.has_value());
        const TargetView target(*context, "stars", std::move(*document));
        const ProbedWindow window = spec.open(target.view());
        auto* preview = widgetNamed<QCheckBox>(*window, "preview");
        QPushButton* apply = applyButton(*window);
        ASSERT_TRUE(preview != nullptr && apply != nullptr);
        const an::Result<an::consumer::ImageAsset> ran =
            runProbeBody(*window.probe, context->engineSession(), target.view().document(),
                         an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Apply});
        if (process_utils::gmicAvailable())
        {
            EXPECT_TRUE(preview->isEnabled());
            EXPECT_TRUE(apply->isEnabled());
            EXPECT_TRUE(ran.hasValue()) << ran.status().message();
        }
        else
        {
            EXPECT_FALSE(preview->isEnabled());
            EXPECT_FALSE(apply->isEnabled());
            EXPECT_EQ(apply->toolTip(), QString::fromUtf8(process_utils::kGmicUnavailableText));
            ASSERT_FALSE(ran.hasValue());
            EXPECT_EQ(ran.status().code(), an::ErrorCode::Unsupported);
        }
    }
    EXPECT_EQ(dialogs, 6u);
}

// The library states how far a command has come only when the command's script says, and the standard library's
// filters these dialogs run never say: an Apply over a still image shows a busy bar rather than 0% throughout, and a
// sequence's dialog counts its frames as every sequence run does.
TEST(GmicProcessTest, AStillImagesRunShowsABusyBarUntilTheCommandSaysHowFarItHasCome)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::size_t dialogs = 0u;
    for (const SpatialProcessSpec& spec : spatialProcesses())
    {
        if (!spec.gmic)
        {
            continue;
        }
        ++dialogs;
        SCOPED_TRACE(spec.action);
        std::optional<an::ui::ImageDocument> document = fixtureDocument(context->engineSession(), "stars-mono-float32");
        ASSERT_TRUE(document.has_value());
        const TargetView target(*context, "stars", std::move(*document));
        const ProbedWindow window = spec.open(target.view());
        const std::unique_ptr<ProgressDialogControl> still(window.probe->progressDialog(false));
        ASSERT_NE(still, nullptr);
        EXPECT_TRUE(still->isBusy()) << "no share done is known before the command states one";
        // The first share the run states ends the busy bar.
        still->showProgress(an::ui::ExecutionProgress{.completed = 40u, .total = 100u});
        EXPECT_FALSE(still->isBusy());
        const std::unique_ptr<ProgressDialogControl> sequence(window.probe->progressDialog(true));
        ASSERT_NE(sequence, nullptr);
        EXPECT_FALSE(sequence->isBusy()) << "a sequence's run counts its frames";
    }
    EXPECT_EQ(dialogs, 6u);
}

// A command whose own loop says how far it has come is shown so: GRAYCstoration's passes report their share done
// through the library's progress field, which reaches the operation and its dialog.
TEST(GmicProcessTest, GRAYCstorationsPassesReportTheirShareDone)
{
    if (!process_utils::gmicAvailable())
    {
        GTEST_SKIP() << "the G'MIC adapter was built without the library";
    }
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const SpatialProcessSpec* graycstoration = spatialProcess("actionGRAYCstoration_Filter");
    ASSERT_NE(graycstoration, nullptr);
    std::optional<an::ui::ImageDocument> document =
        greyDocument(context->engineSession(), 512u, 512u, [](std::uint32_t x, std::uint32_t y) {
            return static_cast<float>(0.25 + 0.5 * (((x * 7919u + y * 104729u) % 1000u) / 1000.0));
        });
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "noise", std::move(*document));
    const ProbedWindow window = graycstoration->open(target.view());
    auto* iterations = widgetNamed<QSpinBox>(*window, "iterationsValue");
    ASSERT_NE(iterations, nullptr);
    iterations->setValue(4);

    an::Result<an::consumer::AssetReadLease> lease = target.view().document().read();
    ASSERT_TRUE(lease.hasValue());
    const an::ui::ProcessSource source = an::ui::ProcessSource::of(*lease);
    QObject receiver;
    std::uint64_t largest = 0u;
    an::ui::ExecutionObserver observer;
    observer.receiver = &receiver;
    observer.progress = [&largest](const an::ui::ExecutionProgress& report) {
        largest = (std::max)(largest, report.completed);
    };
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        context->engineSession(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply},
        std::move(observer));
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    an::Result<an::ui::ProcessDestination> destination =
        an::ui::ProcessDestination::copyOf(context->engineSession(), scope->engine(), scope->execution(), source);
    ASSERT_TRUE(destination.hasValue()) << destination.status().message();
    const an::Status ran =
        window.probe->runBody(source, *destination, an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Apply}, *scope);
    const an::ui::ExecutionTelemetry telemetry = scope->telemetry();
    scope->finish(ran);
    ASSERT_TRUE(ran.ok()) << ran.message();
    ASSERT_TRUE(pumpUntilSettled([&largest]() { return largest > 0u; })) << "the passes' share reached the observer";
    EXPECT_GE(telemetry.progressReceived, 2u) << "each pass after the first said how far the run had come";
    EXPECT_LE(largest, 100u);
}

// The dialog offers no more second-step iterations than the engine runs.
TEST(NLBayesTest, TheSecondStepIterationsStopWhereTheEnginesDo)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = fixtureDocument(context->engineSession(), "stars-mono-float32");
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "stars", std::move(*document));
    SpatialHandle<NLBayesProcess> process = openSpatial<NLBayesProcess>(target.view());
    auto* iterations = widgetNamed<QSpinBox>(*process, "sosIterations");
    ASSERT_NE(iterations, nullptr);
    EXPECT_EQ(iterations->maximum(), static_cast<int>(an::kMaximumPatchDenoiseIterations));
}

// A frame narrower than one search window at the stated noise is refused under the framework's error, and the image is
// left as it was.
TEST(NLBayesTest, AFrameNarrowerThanItsSearchWindowIsRefusedAndKept)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document =
        greyDocument(context->engineSession(), 12u, 10u, [](std::uint32_t x, std::uint32_t y) {
            return 0.02f * static_cast<float>((x * 5u + y * 3u) % 40u);
        });
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "small", std::move(*document));
    const std::optional<fixtures::FixtureSamples> before = documentSamples(target.view().document());
    ASSERT_TRUE(before.has_value());
    SpatialHandle<NLBayesProcess> process = openSpatial<NLBayesProcess>(target.view());
    setChecked(*process, "preview", false);
    setChecked(*process, "autoSigma", false);
    setReal(*process, "sigmaValue", 12.0);
    process->applyProcess();
    ASSERT_TRUE(settleRun(*process, target.view()));
    EXPECT_EQ(process->runTelemetry().runsFailed, 1u);
    ASSERT_FALSE(context->errorTexts.isEmpty());
    EXPECT_TRUE(context->errorTexts.back().contains(QStringLiteral("search window")))
        << context->errorTexts.back().toStdString();
    const std::optional<fixtures::FixtureSamples> after = documentSamples(target.view().document());
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(identicalSamples(*before, *after));
}

// A diameter of zero derives the neighbourhood from the spatial sigma, held to the widest the engine filters - the
// dialog's own defaults among them, which would otherwise ask for a radius of forty-five.
TEST(BilateralFilterTest, ADerivedRadiusIsHeldToTheWidestTheEngineFilters)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document =
        fixtureDocument(context->engineSession(), "stars-mono-float32-wide");
    ASSERT_TRUE(document.has_value());
    const std::optional<fixtures::FixtureSamples> source = documentSamples(*document);
    ASSERT_TRUE(source.has_value());
    const std::optional<fixtures::FixtureSamples> filtered =
        appliedOver<BilateralFilterProcess>(*context, std::move(*document), [](QWidget& window) {
            setReal(window, "diameterValue", 0.0);
            setReal(window, "sigmaSpaceValue", 30.0);
            setReal(window, "sigmaColorValue", 0.1);
        });
    ASSERT_TRUE(filtered.has_value());
    EXPECT_FALSE(identicalSamples(*source, *filtered));
}

// The ranking filters read a floating plane's non-finite samples as meaning nothing, and an integer plane under its own
// rule: every integer sample is a finite number, so the finite-only rule would read the same samples, and the all-valid
// rule leaves the engine its faster paths - the median's sorting network, which the finite-only rule turns away.
TEST(RankingFilterTest, AnIntegerPlaneIsReadUnderItsOwnRuleAndAFloatingOneUnderTheFiniteOnlyRule)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    an::Result<an::consumer::SessionHold> hold = context->engineSession().hold();
    ASSERT_TRUE(hold.hasValue());
    for (const an::SampleType type :
         {an::SampleType::UInt8, an::SampleType::Int8, an::SampleType::UInt16, an::SampleType::Int16,
          an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64})
    {
        const std::optional<an::ImageDescriptor> described =
            an::tests::viewer::describeImage(hold->engine(), type, 8u, 6u, Channels::Grey);
        ASSERT_TRUE(described.has_value());
        const an::ChannelScopeInput input{.descriptor = &*described};
        ASSERT_TRUE(input.validity.descriptor.isAllValid());
        const process_utils::PlaneSelection ranked(input, process_utils::NonFiniteSamples::MeanNothing);
        const bool floating = type == an::SampleType::Float32 || type == an::SampleType::Float64;
        EXPECT_EQ(ranked.selection().validity.rule(),
                  floating ? an::ValidityRule::FiniteOnly : an::ValidityRule::AllValid)
            << an::sampleTypeName(type);
        const process_utils::PlaneSelection stated(input);
        EXPECT_EQ(stated.selection().validity.rule(), an::ValidityRule::AllValid) << an::sampleTypeName(type);
    }
}

// A colour image is equalized in its CIE lightness alone: its a* and b* are what they were, its lightness is not, and
// an alpha channel is carried untouched.
TEST(CLAHETest, AColourImageIsEqualizedInItsLightnessAlone)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const auto value = [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
        const float ramp = 0.1f + 0.6f * static_cast<float>((x * 3u + y * 5u + c * 7u) % 32u) / 31.0f;
        return c == 3u ? 0.25f + 0.01f * static_cast<float>(x % 7u) : ramp * (c == 0u ? 1.0f : (c == 1u ? 0.8f : 0.6f));
    };
    std::optional<an::ui::ImageDocument> document =
        an::tests::viewer::makeDocument<float>(context->engineSession(), 64u, 48u, Channels::Rgba, value);
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "colour", std::move(*document));
    const auto sourceA = componentOf(context->engineSession(), target.view().document(), an::ChannelScope::CieA);
    const auto sourceB = componentOf(context->engineSession(), target.view().document(), an::ChannelScope::CieB);
    const auto sourceL =
        componentOf(context->engineSession(), target.view().document(), an::ChannelScope::CieLightness);
    ASSERT_TRUE(sourceA.has_value() && sourceB.has_value() && sourceL.has_value());

    SpatialHandle<CLAHEProcess> process = openSpatial<CLAHEProcess>(target.view());
    setChecked(*process, "preview", false);
    setReal(*process, "contrastLimitValue", 4.0);
    process->applyProcess();
    ASSERT_TRUE(settleRun(*process, target.view()));
    ASSERT_EQ(process->runTelemetry().runsCommitted, 1u)
        << context->errorTexts.join(QStringLiteral("; ")).toStdString();

    const auto resultA = componentOf(context->engineSession(), target.view().document(), an::ChannelScope::CieA);
    const auto resultB = componentOf(context->engineSession(), target.view().document(), an::ChannelScope::CieB);
    const auto resultL =
        componentOf(context->engineSession(), target.view().document(), an::ChannelScope::CieLightness);
    ASSERT_TRUE(resultA.has_value() && resultB.has_value() && resultL.has_value());
    double lightnessMoved = 0.0;
    for (std::uint32_t y = 0u; y < 48u; ++y)
    {
        for (std::uint32_t x = 0u; x < 64u; ++x)
        {
            ASSERT_NEAR(resultA->value(x, y, 0u), sourceA->value(x, y, 0u), 1e-3) << x << "," << y;
            ASSERT_NEAR(resultB->value(x, y, 0u), sourceB->value(x, y, 0u), 1e-3) << x << "," << y;
            ASSERT_EQ(documentSample(target.view().document(), x, y, 3u), static_cast<double>(value(x, y, 3u)))
                << x << "," << y;
            lightnessMoved = (std::max)(lightnessMoved, std::abs(resultL->value(x, y, 0u) - sourceL->value(x, y, 0u)));
        }
    }
    EXPECT_GT(lightnessMoved, 1e-2);
}

// An equalized result is recorded nonlinear where the body wrote it: the run's destination keeps its image and its
// samples - nothing of them is copied to restate the cards - and its cards and its lineage name the equalization.
TEST(CLAHETest, AnApplysResultIsRecordedNonlinearWhereTheBodyWroteIt)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const SpatialProcessSpec* clahe = spatialProcess("actionCLA_Histogram_Equalization");
    ASSERT_NE(clahe, nullptr);
    std::optional<an::ui::ImageDocument> document =
        greyDocument(context->engineSession(), 64u, 48u, [](std::uint32_t x, std::uint32_t y) {
            return 0.1f + 0.6f * static_cast<float>((x * 3u + y * 5u) % 32u) / 31.0f;
        });
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "grey", std::move(*document));
    const ProbedWindow window = clahe->open(target.view());
    clahe->configure(*window);

    an::Result<an::consumer::AssetReadLease> lease = target.view().document().read();
    ASSERT_TRUE(lease.hasValue());
    const an::ui::ProcessSource source = an::ui::ProcessSource::of(*lease);
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        context->engineSession(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply},
        an::ui::ExecutionObserver{});
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    an::Result<an::ui::ProcessDestination> destination =
        an::ui::ProcessDestination::copyOf(context->engineSession(), scope->engine(), scope->execution(), source);
    ASSERT_TRUE(destination.hasValue()) << destination.status().message();
    const auto samplesOf = [](const an::consumer::AssetWriteLease& result) {
        return std::visit([](const auto& typed) -> const void* { return typed.view()->data(); }, result.image());
    };
    const std::uint64_t identity = destination->asset().assetIdentity();
    const void* const samples = samplesOf(destination->asset());

    const an::Status ran =
        window.probe->runBody(source, *destination, an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Apply}, *scope);
    scope->finish(ran);
    ASSERT_TRUE(ran.ok()) << ran.message();
    EXPECT_EQ(destination->asset().assetIdentity(), identity) << "the result was replaced by a copy of it";
    EXPECT_EQ(samplesOf(destination->asset()), samples) << "the result's samples were copied";
    const an::ImageMetadata metadata =
        std::visit([](const auto& typed) { return typed.metadata(); }, destination->asset().image());
    const an::ProcessingStateRecord state = an::readProcessingStateCards(metadata.codec());
    EXPECT_EQ(state.linearity, an::RecordedLinearity::Nonlinear);
    EXPECT_EQ(state.operation, "clahe");
    ASSERT_FALSE(metadata.provenance().entries().empty());
    EXPECT_EQ(metadata.provenance().entries().back().operation(), "clahe");
}
