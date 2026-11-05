// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What one colour calibration window does that the others do not: the colour calibration's masks of the reference
/// samples within each pair of limits in its automatic and its manual mode, its references and their rectangles, and
/// the white it cannot balance; the neutralization's rectangle and its refusal of limits that keep no sample of a
/// channel; the linear match's reference choices, a reference of another size and limits that keep nothing; SCNR's
/// amount and its window staying open for another run.

#include "Test_ProcessesColorTools/ColorToolsTestSupport.h"

#include "processes/BackgroundNeutralizationProcess.h"
#include "processes/ColorCalibrationProcess.h"
#include "processes/ColorToolsSupport.h"
#include "processes/LinearMatchProcess.h"
#include "processes/SCNRProcess.h"

#include <QPushButton>
#include <QSlider>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using namespace an::tests::processes::colortools;

class ColorToolsCase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(4u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] ProcessTestContext& context() const
    {
        return *m_context;
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    /// A view of a scene, made active.
    [[nodiscard]] ViewHandle view(std::string_view scene, const std::string& name)
    {
        ViewHandle made = sceneView(session(), scene, name);
        EXPECT_NE(made, nullptr);
        if (made != nullptr)
        {
            activate(context(), *made);
        }
        return made;
    }

    /// A view of a new asset, made active.
    [[nodiscard]] ViewHandle viewOf(std::optional<an::consumer::ImageAsset> asset, const std::string& name)
    {
        EXPECT_TRUE(asset.has_value());
        if (!asset.has_value())
        {
            return ViewHandle();
        }
        ViewHandle made = assetView(std::move(*asset), name);
        activate(context(), *made);
        return made;
    }

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

/// Whether every sample of a mask is the top or the bottom of its range, and which are the top: a fraction of one where
/// `selected` says so, zero elsewhere.
template <typename SelectedT>
void expectMask(const fixtures::FixtureSamples& mask, std::uint32_t width, std::uint32_t height, SelectedT selected)
{
    ASSERT_EQ(mask.channelCount(), 1u);
    ASSERT_EQ(mask.width(), width);
    ASSERT_EQ(mask.height(), height);
    std::uint64_t wrong = 0u;
    std::uint64_t set = 0u;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double value = fractionOf(mask, x, y, 0u);
            const bool expected = selected(x, y);
            set += expected ? 1u : 0u;
            if (value != (expected ? 1.0 : 0.0))
            {
                ++wrong;
            }
        }
    }
    EXPECT_EQ(wrong, 0u) << "mask samples are not what the limits select";
    EXPECT_GT(set, 0u) << "the case selects something";
}

/// Whether every colour channel of a scene's pixel lies within closed limits.
[[nodiscard]] bool within(const fixtures::FixtureSamples& scene, std::uint32_t x, std::uint32_t y, double lowest,
                          double highest)
{
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        const double value = fractionOf(scene, x, y, c);
        if (value < lowest || value > highest)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] const ColorTool& tool(std::string_view name)
{
    const ColorTool* found = toolNamed(name);
    EXPECT_NE(found, nullptr) << name;
    return *found;
}

} // namespace

// apply-mask outputs: an automatic calibration asked for both masks opens them beside the target once its result
// is shown - each a grey image of its reference's size and representation, named and described as the mask it is,
// every sample written: full scale where every colour channel of the reference's rectangle lies within its limits, zero
// everywhere else, outside the rectangle included.
TEST_F(ColorToolsCase, AnAutomaticCalibrationWritesEverySampleOfBothMasks)
{
    const ColorTool& calibration = tool("ColorCalibration");
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples("scene-rgb-float32");
    ASSERT_TRUE(scene.has_value());
    ViewHandle target = view("scene-rgb-float32", "target");
    WindowHandle window = calibration.open(target.get());
    setChecked(*window, "outputWhiteReferenceMask", true);
    setChecked(*window, "outputBackgroundReferenceMask", true);
    clickGroup(*window, "whiteRoiGroupBox", true);
    setInt(*window, "whiteRoiLeftValue", 2);
    setInt(*window, "whiteRoiTopValue", 1);
    setInt(*window, "whiteRoiWidthValue", 8);
    setInt(*window, "whiteRoiHeightValue", 5);
    setDouble(*window, "backgroundUpperLimitValue", 0.5);
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window, calibration));
    ASSERT_EQ(context().madeViews.size(), before + 2u) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    ImageViewControl* white = context().madeViews[before];
    ImageViewControl* background = context().madeViews[before + 1u];
    EXPECT_EQ(white->getUniqueName(), "target - WHITE MASK");
    EXPECT_EQ(background->getUniqueName(), "target - BACKGROUND MASK");
    EXPECT_EQ(madeHistoryOf(context(), white), "ColorCalibration: White reference mask image created from 'target'");
    EXPECT_EQ(madeHistoryOf(context(), background),
              "ColorCalibration: Background reference mask image created from 'target'");
    EXPECT_EQ(madeNeighbourOf(context(), white), target.get());
    const std::optional<an::ImageDescriptor> described = white->imageDescriptor();
    ASSERT_TRUE(described.has_value());
    EXPECT_EQ(described->sampleType(), an::SampleType::Float32);
    const std::optional<fixtures::FixtureSamples> whiteMask = documentSamples(white->document());
    const std::optional<fixtures::FixtureSamples> backgroundMask = documentSamples(background->document());
    ASSERT_TRUE(whiteMask.has_value() && backgroundMask.has_value());
    expectMask(*whiteMask, scene->width(), scene->height(), [&](std::uint32_t x, std::uint32_t y) {
        return x >= 2u && x < 10u && y >= 1u && y < 6u && within(*scene, x, y, 0.0, 0.9);
    });
    expectMask(*backgroundMask, scene->width(), scene->height(),
               [&](std::uint32_t x, std::uint32_t y) { return within(*scene, x, y, 0.0, 0.5); });
    window.reset();
    dropView(context(), target);
}

// apply-mask outputs: a manual calibration measures no white reference, so it offers and opens no white
// mask, and writes every sample of the background mask it is asked for.
TEST_F(ColorToolsCase, AManualCalibrationWritesEverySampleOfTheBackgroundMaskAndNoWhiteMask)
{
    const ColorTool& calibration = tool("ColorCalibration");
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples("scene-rgb-float32");
    ASSERT_TRUE(scene.has_value());
    ViewHandle target = view("scene-rgb-float32", "target");
    WindowHandle window = calibration.open(target.get());
    setChecked(*window, "outputWhiteReferenceMask", true);
    setChecked(*window, "outputBackgroundReferenceMask", true);
    calibration.configure(*window, "manual-0.9-1-0.8");
    EXPECT_FALSE(widgetOf<QCheckBox>(*window, "outputWhiteReferenceMask")->isEnabled());
    setDouble(*window, "backgroundUpperLimitValue", 0.5);
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window, calibration));
    ASSERT_EQ(context().madeViews.size(), before + 1u) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    ImageViewControl* background = context().madeViews[before];
    EXPECT_EQ(background->getUniqueName(), "target - BACKGROUND MASK");
    const std::optional<fixtures::FixtureSamples> mask = documentSamples(background->document());
    ASSERT_TRUE(mask.has_value());
    expectMask(*mask, scene->width(), scene->height(),
               [&](std::uint32_t x, std::uint32_t y) { return within(*scene, x, y, 0.0, 0.5); });
    window.reset();
    dropView(context(), target);
}

// apply-mask outputs: a mask of a signed image's reference is its smallest value where nothing is selected - a fraction
// of zero - and its largest where the limits select, never the zero code in its middle.
TEST_F(ColorToolsCase, ASignedReferencesMaskRunsFromTheBottomOfItsRange)
{
    const ColorTool& calibration = tool("ColorCalibration");
    const std::optional<an::consumer::ImageAsset> scene = sceneAsset(session(), "scene-rgb-float32");
    ASSERT_TRUE(scene.has_value());
    ViewHandle target = viewOf(convertedTo(session(), *scene, an::SampleType::Int16), "target");
    WindowHandle window = calibration.open(target.get());
    setChecked(*window, "outputBackgroundReferenceMask", true);
    setDouble(*window, "backgroundLowerLimitValue", 0.6);
    setDouble(*window, "backgroundUpperLimitValue", 0.8);
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window, calibration));
    ASSERT_EQ(context().madeViews.size(), before + 1u) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<fixtures::FixtureSamples> mask = documentSamples(context().madeViews[before]->document());
    const std::optional<fixtures::FixtureSamples> signedScene = documentSamples(target->document());
    ASSERT_TRUE(mask.has_value() && signedScene.has_value());
    ASSERT_EQ(mask->sampleType(), fixtures::FixtureSampleType::Int16);
    const std::optional<fixtures::FixtureSamples> source = sceneSamples("scene-rgb-float32");
    ASSERT_TRUE(source.has_value());
    // The reference is the signed image before the run, whose fractions are the float scene's moved to its upper half.
    const std::optional<an::consumer::ImageAsset> reread = convertedTo(session(), *scene, an::SampleType::Int16);
    ASSERT_TRUE(reread.has_value());
    const std::optional<fixtures::FixtureSamples> reference = assetSamples(*reread);
    ASSERT_TRUE(reference.has_value());
    expectMask(*mask, reference->width(), reference->height(),
               [&](std::uint32_t x, std::uint32_t y) { return within(*reference, x, y, 0.6, 0.8); });
    window.reset();
    dropView(context(), target);
}

// error: a white reference whose channels sit on their background - a flat image - has no gain to balance, so the run
// is refused by name, changes nothing and opens none of the masks it was asked for.
TEST_F(ColorToolsCase, AWhiteThatSitsOnItsBackgroundIsRefused)
{
    const ColorTool& calibration = tool("ColorCalibration");
    ViewHandle target =
        viewOf(an::tests::viewer::makeAsset<float>(
                   session(), 16u, 12u, an::tests::viewer::Channels::Rgb,
                   [](std::uint32_t, std::uint32_t, std::uint32_t c) { return 0.2f + 0.1f * static_cast<float>(c); }),
               "flat");
    WindowHandle window = calibration.open(target.get());
    setChecked(*window, "outputBackgroundReferenceMask", true);
    const std::size_t before = context().madeViews.size();
    const an::ui::DocumentRevision revision = target->document().revision();
    ASSERT_TRUE(applyAndSettle(context(), *window, calibration));
    EXPECT_EQ(context().madeViews.size(), before) << "no mask is opened";
    EXPECT_EQ(target->document().revision(), revision);
    ASSERT_EQ(context().errorTexts.size(), 1);
    EXPECT_TRUE(context().errorTexts.front().contains(QStringLiteral("sits on its background level")))
        << context().errorTexts.front().toStdString();
    window.reset();
    dropView(context(), target);
}

// error: a calibration whose edit the view's history cannot keep is not shown, so the masks its run made are not
// opened - the person hears once, from the history, why nothing changed.
TEST_F(ColorToolsCase, MasksOfACalibrationThatIsNotShownAreNotOpened)
{
    TearDown();
    std::unique_ptr<ProcessTestContext> tight = openProcessContext(4u, an::tests::viewer::historyCannotKeepEdits());
    ASSERT_NE(tight, nullptr);
    const InstalledContext installed(*tight);
    ViewHandle target = sceneView(tight->engineSession(), "scene-rgb-float32", "target");
    ASSERT_NE(target, nullptr);
    activate(*tight, *target);
    const ColorTool& calibration = tool("ColorCalibration");
    WindowHandle window = calibration.open(target.get());
    setChecked(*window, "outputWhiteReferenceMask", true);
    setChecked(*window, "outputBackgroundReferenceMask", true);
    const std::size_t before = tight->madeViews.size();
    const an::ui::DocumentRevision revision = target->document().revision();
    ASSERT_TRUE(applyAndSettle(*tight, *window, calibration));
    ASSERT_TRUE(pumpUntil([&tight]() { return tight->errorCount() >= 1; })) << "the history says why";
    ASSERT_TRUE(settleWindow(*tight, *window));
    EXPECT_EQ(target->document().revision(), revision);
    EXPECT_EQ(tight->madeViews.size(), before) << "a mask of a result that was not shown was opened";
    EXPECT_EQ(tight->errorCount(), 1) << tight->errorTexts.join(QStringLiteral("; ")).toStdString();
    window.reset();
    dropView(*tight, target);
}

// references: a colour calibration measures the references the dialog names - another view's image and a rectangle of
// it - and a manual white is offered from just above zero, since no channel can be multiplied by nothing.
TEST_F(ColorToolsCase, ACalibrationMeasuresTheReferencesTheDialogNames)
{
    const ColorTool& calibration = tool("ColorCalibration");
    ViewHandle other = view("scene-rgb-float32-second", "other");
    ViewHandle target = view("scene-rgb-float32", "target");
    std::optional<fixtures::FixtureSamples> ownResult;
    {
        WindowHandle window = calibration.open(target.get());
        EXPECT_GT(widgetOf<QDoubleSpinBox>(*window, "mwbRedValue")->minimum(), 0.0);
        ASSERT_TRUE(applyAndSettle(context(), *window, calibration));
        ownResult = documentSamples(target->document());
        ASSERT_TRUE(target->viewHistory()->undo());
        ASSERT_TRUE(settleView(*target));
    }
    WindowHandle window = calibration.open(target.get());
    calibration.chooseReference(*window, QStringLiteral("other"));
    clickGroup(*window, "backgroundRoiGroupBox", true);
    setInt(*window, "backgroundRoiLeftValue", 1);
    setInt(*window, "backgroundRoiTopValue", 1);
    setInt(*window, "backgroundRoiWidthValue", 6);
    setInt(*window, "backgroundRoiHeightValue", 4);
    ASSERT_TRUE(applyAndSettle(context(), *window, calibration));
    const std::optional<fixtures::FixtureSamples> made = documentSamples(target->document());
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples("scene-rgb-float32");
    const std::optional<fixtures::FixtureSamples> second = sceneSamples("scene-rgb-float32-second");
    ASSERT_TRUE(made.has_value() && ownResult.has_value() && scene.has_value() && second.has_value());
    EXPECT_NE(made->digest(), ownResult->digest()) << "the references the dialog names were not read";
    // The stated composition over the other view: its white within 0 to 0.9 everywhere, its background the median of
    // the rectangle.
    std::array<double, 3u> background{};
    std::array<double, 3u> above{};
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        background[c] = medianOf(channelFractions(*second, c, [](std::uint32_t x, std::uint32_t y, double) {
            return x >= 1u && x < 7u && y >= 1u && y < 5u;
        }));
        const std::vector<double> white =
            channelFractions(*second, c, [](std::uint32_t, std::uint32_t, double v) { return v >= 0.0 && v <= 0.9; });
        double sum = 0.0;
        for (const double v : white)
        {
            sum += v;
        }
        above[c] = std::fabs(sum / static_cast<double>(white.size()) - background[c]);
    }
    const std::size_t strongest =
        above[0] > above[1] ? (above[0] > above[2] ? 0u : 2u) : (above[1] > above[2] ? 1u : 2u);
    double largest = 0.0;
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        const double gain = above[strongest] / above[c];
        for (std::uint32_t y = 0u; y < scene->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < scene->width(); ++x)
            {
                const double expected =
                    std::clamp(fractionOf(*scene, x, y, c) * gain + background[c] * (1.0 - gain), 0.0, 1.0);
                largest = (std::max)(largest, std::fabs(fractionOf(*made, x, y, c) - expected));
            }
        }
    }
    EXPECT_LE(largest, 2.0e-6);
    window.reset();
    dropView(context(), target);
    dropView(context(), other);
}

// The history steps describe what ran.
TEST_F(ColorToolsCase, TheHistoryStepsDescribeWhatRan)
{
    ColorCalibrationProcess::Settings calibration;
    EXPECT_EQ(ColorCalibrationProcess::stepDescription(calibration, "target", QRect(0, 0, 12, 8), "target",
                                                       QRect(0, 0, 12, 8)),
              "White reference image 'target' (ROI X:0, Y:0, W:12, H:8) (MIN:0.000000, MAX:0.900000) - Background "
              "reference image 'target' (ROI X:0, Y:0, W:12, H:8) (MIN:0.000000, MAX:0.100000)");
    calibration.manual = true;
    calibration.manualWhite = {0.9, 1.0, 0.8};
    EXPECT_EQ(ColorCalibrationProcess::stepDescription(calibration, "", QRect(), "target", QRect(1, 2, 3, 4)),
              "Using manual white balance values R:0.900000, G:1.000000, B:0.800000 - Background reference image "
              "'target' (ROI X:1, Y:2, W:3, H:4) (MIN:0.000000, MAX:0.100000)");
    EXPECT_EQ(BackgroundNeutralizationProcess::stepDescription("target", QRect(0, 0, 12, 8)),
              "Reference image 'target' (ROI X:0, Y:0, W:12, H:8)");
    EXPECT_EQ(
        LinearMatchProcess::stepDescription(LinearMatchProcess::Settings{.referenceName = QStringLiteral("reference"),
                                                                         .range = {.lowest = 0.0, .highest = 0.92}},
                                            "target", false),
        "Target image 'target', Reference image 'reference', Reject Low: 0, Reject High: 0.92");
    SCNRProcess::Settings scnr;
    scnr.protection = QStringLiteral("Average Neutral");
    EXPECT_EQ(SCNRProcess::stepDescription(scnr), "Protection Method Average Neutral preserve lightness true");
}

// error: a neutralization whose limits keep no sample of a channel is refused, naming the channel, before anything is
// changed - the scenes' green holds nothing below the dialog's 0.1.
TEST_F(ColorToolsCase, ANeutralizationWhoseLimitsKeepNoSampleIsRefusedByChannel)
{
    const ColorTool& neutralization = tool("BackgroundNeutralization");
    ViewHandle target = view("scene-rgb-float32", "target");
    WindowHandle window = neutralization.open(target.get());
    const an::ui::DocumentRevision revision = target->document().revision();
    ASSERT_TRUE(applyAndSettle(context(), *window, neutralization));
    EXPECT_EQ(target->document().revision(), revision);
    ASSERT_EQ(context().errorTexts.size(), 1);
    EXPECT_TRUE(context().errorTexts.front().contains(QStringLiteral("No green sample of the background reference")))
        << context().errorTexts.front().toStdString();
    EXPECT_TRUE(widgetOf<QDialogButtonBox>(*window, "buttonBox")->button(QDialogButtonBox::Ok)->isEnabled())
        << "the window stays open to change its limits";
    window.reset();
    dropView(context(), target);
}

// references: a neutralization measures the rectangle of the reference the dialog names.
TEST_F(ColorToolsCase, ANeutralizationMeasuresTheRectangleTheDialogNames)
{
    const ColorTool& neutralization = tool("BackgroundNeutralization");
    ViewHandle target = view("scene-rgb-float32", "target");
    WindowHandle window = neutralization.open(target.get());
    setDouble(*window, "backgroundUpperLimitValue", 0.6);
    clickGroup(*window, "backgroundRoiGroupBox", true);
    setInt(*window, "backgroundRoiLeftValue", 3);
    setInt(*window, "backgroundRoiTopValue", 2);
    setInt(*window, "backgroundRoiWidthValue", 7);
    setInt(*window, "backgroundRoiHeightValue", 5);
    ASSERT_TRUE(applyAndSettle(context(), *window, neutralization));
    const std::optional<fixtures::FixtureSamples> made = documentSamples(target->document());
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples("scene-rgb-float32");
    ASSERT_TRUE(made.has_value() && scene.has_value());
    std::array<double, 3u> levels{};
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        levels[c] = medianOf(channelFractions(*scene, c, [](std::uint32_t x, std::uint32_t y, double v) {
            return x >= 3u && x < 10u && y >= 2u && y < 7u && v >= 0.0 && v <= 0.6;
        }));
    }
    const double pedestal = (levels[0] + levels[1] + levels[2]) / 3.0;
    double largest = 0.0;
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < scene->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < scene->width(); ++x)
            {
                const double expected = std::clamp(fractionOf(*scene, x, y, c) + pedestal - levels[c], 0.0, 1.0);
                largest = (std::max)(largest, std::fabs(fractionOf(*made, x, y, c) - expected));
            }
        }
    }
    EXPECT_LE(largest, 2.0e-6);
    window.reset();
    dropView(context(), target);
}

// references: a linear match offers the views of the target's channel count but the target itself, and runs only once
// one is chosen.
TEST_F(ColorToolsCase, ALinearMatchOffersTheViewsOfTheTargetsChannelCount)
{
    const ColorTool& match = tool("LinearMatch");
    ViewHandle grey = viewOf(an::tests::viewer::makeAsset<float>(
                                 session(), 12u, 8u, an::tests::viewer::Channels::Grey,
                                 [](std::uint32_t x, std::uint32_t, std::uint32_t) { return 0.1f * (x % 5u); }),
                             "grey");
    ViewHandle colour = view("scene-rgb-float32-second", "colour");
    ViewHandle target = view("scene-rgb-float32", "target");
    WindowHandle window = match.open(target.get());
    auto* references = widgetOf<QComboBox>(*window, "referenceImageSelector");
    ASSERT_NE(references, nullptr);
    EXPECT_EQ(references->currentText(), NO_IMAGE_STR);
    EXPECT_GE(references->findText(QStringLiteral("colour")), 0);
    EXPECT_EQ(references->findText(QStringLiteral("grey")), -1) << "a grey image is not a colour image's reference";
    EXPECT_EQ(references->findText(QStringLiteral("target")), -1) << "the target is not its own reference";
    QPushButton* ok = applyButtonOf(*window, match);
    ASSERT_NE(ok, nullptr);
    EXPECT_FALSE(ok->isEnabled());
    match.chooseReference(*window, QStringLiteral("colour"));
    EXPECT_TRUE(ok->isEnabled());
    window.reset();
    dropView(context(), target);
    dropView(context(), colour);
    dropView(context(), grey);
}

// The colour calibration and the neutralization offer their target once as a reference, as the target entry: the views
// listed after it pass over the view the window works on rather than offer it a second time under its own name.
TEST_F(ColorToolsCase, TheTargetIsOfferedOnceAsAReference)
{
    ViewHandle colour = view("scene-rgb-float32-second", "colour");
    ViewHandle target = view("scene-rgb-float32", "target");
    const QString targetEntry = QString::fromUtf8(an::ui::processes::kTargetImageReference);
    for (const auto& [name, selectors] :
         {std::pair<std::string_view, std::vector<const char*>>{
              "ColorCalibration", {"whiteReferenceImageSelector", "backgroundReferenceImageSelector"}},
          std::pair<std::string_view, std::vector<const char*>>{"BackgroundNeutralization",
                                                                {"backgroundReferenceImageSelector"}}})
    {
        WindowHandle window = tool(name).open(target.get());
        for (const char* selector : selectors)
        {
            auto* references = widgetOf<QComboBox>(*window, selector);
            ASSERT_NE(references, nullptr) << name << " " << selector;
            EXPECT_GE(references->findText(targetEntry), 0) << name << " " << selector;
            EXPECT_EQ(references->findText(QStringLiteral("target")), -1)
                << name << " " << selector << ": the target is offered a second time under its own name";
            EXPECT_GE(references->findText(QStringLiteral("colour")), 0) << name << " " << selector;
        }
    }
    dropView(context(), target);
    dropView(context(), colour);
}

// The linear match's limit boxes hold every position their sliders reach: a slider moves in thousandths, and a box of
// two decimals rounded a position between hundredths away before the run read it.
TEST_F(ColorToolsCase, ALinearMatchLimitHoldsEverySliderPosition)
{
    ViewHandle target = view("scene-rgb-float32", "target");
    WindowHandle window = tool("LinearMatch").open(target.get());
    for (const auto& [slider, box, position] :
         {std::tuple{"rejectLowSlider", "rejectLowValue", 123}, std::tuple{"rejectHighSlider", "rejectHighValue", 987}})
    {
        auto* moved = widgetOf<QSlider>(*window, slider);
        auto* shown = widgetOf<QDoubleSpinBox>(*window, box);
        ASSERT_NE(moved, nullptr);
        ASSERT_NE(shown, nullptr);
        moved->setValue(position);
        EXPECT_NEAR(shown->value(), position / 1000.0, 1e-12) << box;
    }
    window.reset();
    dropView(context(), target);
}

// A linear match to a reference of another size measures each image on its own, both held to the limits - the ratio of
// their deviations and the offset between their medians.
TEST_F(ColorToolsCase, ALinearMatchToAnotherSizeMeasuresEachImageOnItsOwn)
{
    const ColorTool& match = tool("LinearMatch");
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples("scene-rgb-float32");
    ASSERT_TRUE(scene.has_value());
    // A gradient across the reference, so its top-left corner - all a pairing by position would read - measures
    // otherwise than the whole.
    const auto reference = [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
        return static_cast<float>(0.05 + 0.04 * static_cast<double>(x) + 0.01 * static_cast<double>(y + c));
    };
    ViewHandle other =
        viewOf(an::tests::viewer::makeAsset<float>(session(), 20u, 9u, an::tests::viewer::Channels::Rgb, reference),
               kReferenceView);
    ViewHandle target = view("scene-rgb-float32", "target");
    WindowHandle window = match.open(target.get());
    match.chooseReference(*window, QString::fromLatin1(kReferenceView));
    ASSERT_TRUE(applyAndSettle(context(), *window, match));
    EXPECT_TRUE(context().errorTexts.isEmpty()) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<fixtures::FixtureSamples> made = documentSamples(target->document());
    ASSERT_TRUE(made.has_value());
    double largest = 0.0;
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        const auto kept = [](std::uint32_t, std::uint32_t, double v) { return v >= 0.0 && v <= 0.92 && v != 0.0; };
        const std::vector<double> targets = channelFractions(*scene, c, kept);
        std::vector<double> references;
        for (std::uint32_t y = 0u; y < 9u; ++y)
        {
            for (std::uint32_t x = 0u; x < 20u; ++x)
            {
                const double v = static_cast<double>(reference(x, y, c));
                if (kept(x, y, v))
                {
                    references.push_back(v);
                }
            }
        }
        const double scale = deviationOf(references) / deviationOf(targets);
        const double offset = medianOf(references) - scale * medianOf(targets);
        for (std::uint32_t y = 0u; y < scene->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < scene->width(); ++x)
            {
                largest = (std::max)(largest, std::fabs(fractionOf(*made, x, y, c) -
                                                        (fractionOf(*scene, x, y, c) * scale + offset)));
            }
        }
    }
    EXPECT_LE(largest, 2.0e-6);
    window.reset();
    dropView(context(), target);
    dropView(context(), other);
}

// error: a linear match whose limits keep no sample - every kept sample would be zero, which means nothing - is
// refused rather than writing the channel as zero.
TEST_F(ColorToolsCase, ALinearMatchWhoseLimitsKeepNothingIsRefused)
{
    const ColorTool& match = tool("LinearMatch");
    ViewHandle other = view("scene-rgb-float32-second", kReferenceView);
    ViewHandle target = view("scene-rgb-float32", "target");
    WindowHandle window = match.open(target.get());
    match.chooseReference(*window, QString::fromLatin1(kReferenceView));
    setDouble(*window, "rejectHighValue", 0.0);
    const an::ui::DocumentRevision revision = target->document().revision();
    ASSERT_TRUE(applyAndSettle(context(), *window, match));
    EXPECT_EQ(target->document().revision(), revision);
    ASSERT_EQ(context().errorTexts.size(), 1);
    EXPECT_TRUE(context().errorTexts.front().contains(QStringLiteral("between the limits 0 and 0")))
        << context().errorTexts.front().toStdString();
    window.reset();
    dropView(context(), target);
    dropView(context(), other);
}

// SCNR offers the amount only to the two mask methods, which mix green back by it, and its window stays open for
// another run, each a history step.
TEST_F(ColorToolsCase, ScnrOffersTheAmountToTheMaskMethodsAndStaysOpen)
{
    const ColorTool& scnr = tool("Scnr");
    ViewHandle target = view("scene-rgb-float32", "target");
    WindowHandle window = scnr.open(target.get());
    window->show();
    auto* amount = widgetOf<QDoubleSpinBox>(*window, "amountValue");
    ASSERT_NE(amount, nullptr);
    // The slider spans the amount's whole range and follows it.
    auto* slider = widgetOf<QSlider>(*window, "amountSlider");
    ASSERT_NE(slider, nullptr);
    EXPECT_EQ(slider->minimum(), 0);
    EXPECT_EQ(slider->value(), slider->maximum()) << "the amount starts at its full 1";
    for (const auto& [protection, enabled] :
         {std::pair<const char*, bool>{"Average Neutral", false},
          std::pair<const char*, bool>{"Maximum Neutral", false}, std::pair<const char*, bool>{"Maximum Mask", true},
          std::pair<const char*, bool>{"Additive Mask", true}})
    {
        setComboText(*window, "protectionType", QString::fromLatin1(protection));
        EXPECT_EQ(amount->isEnabled(), enabled) << protection;
    }
    setComboText(*window, "protectionType", QStringLiteral("Average Neutral"));
    ASSERT_TRUE(applyAndSettle(context(), *window, scnr));
    EXPECT_TRUE(window->isVisible());
    const std::optional<fixtures::FixtureSamples> once = documentSamples(target->document());
    setComboText(*window, "protectionType", QStringLiteral("Maximum Mask"));
    setDouble(*window, "amountValue", 0.5);
    EXPECT_EQ(slider->value(), slider->maximum() / 2);
    ASSERT_TRUE(applyAndSettle(context(), *window, scnr));
    const std::optional<fixtures::FixtureSamples> twice = documentSamples(target->document());
    ASSERT_TRUE(once.has_value() && twice.has_value());
    EXPECT_NE(once->digest(), twice->digest());
    ASSERT_TRUE(target->viewHistory()->undo());
    ASSERT_TRUE(settleView(*target));
    EXPECT_TRUE(target->viewHistory()->hasUndo()) << "two runs are two steps";
    window.reset();
    dropView(context(), target);
}
