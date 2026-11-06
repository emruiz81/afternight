// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The spatial and noise processes' frames over the part of the target the preview view shows. Each process declares
/// what an interactive frame may run over: a body whose answer depends on the pixel grid and on nothing farther than a
/// reach it states runs at full scale over the part shown, grown by that reach within the target, and over that part
/// its frame is the Apply's answer, sample for sample, its edges answered as the whole image answers them; the Gaussian
/// blur and the two detail filters, whose blur's sigma scales with a reduction, run over the part shown the same way at
/// full scale, the detail filters as their Apply does wherever one kernel reaches their radius; a body that measures
/// the whole image, tiles it by its size or blurs without a finite reach runs every frame over the whole image.

#include "Test_ProcessesSpatial/SpatialProcessesTestSupport.h"

#include "widgets/ImageViewWidget.h"

#include <QCheckBox>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::spatial;

/// The actions whose bodies read the pixel grid and nothing farther than the reach they state.
constexpr std::array<std::string_view, 5u> kNativeRegionActions{
    "actionMedian_Filter", "actionBilateral_Filter", "actionCosmetic_Correction_Filter",
    "actionMorphological_Transformation", "actionTighten_Edges"};

/// The actions whose bodies blur by a sigma that scales with a reduction: the Gaussian blur and the two detail filters.
constexpr std::array<std::string_view, 3u> kDecimatedActions{"actionGaussian_Blur", "actionUnsharp_Mask",
                                                             "actionHigh_Pass"};

/// What a spatial process's interactive frames may run over, as its action names it: the blurs over what the view
/// paints, the bodies that read the pixel grid over the part shown at full scale, and every other over the whole image
/// - the banding reduction's line statistics, the total variation's stopping test, the noise generator's draws keyed to
/// where a plane begins, the patch denoiser's measured noise and size-made tiles, CLAHE's grid over the image, and the
/// G'MIC commands' whole-image measurements and endless blurs.
[[nodiscard]] an::ui::PreviewScaling declaredScaling(std::string_view action)
{
    if (std::ranges::find(kDecimatedActions, action) != kDecimatedActions.end())
    {
        return an::ui::PreviewScaling::Decimated;
    }
    if (std::ranges::find(kNativeRegionActions, action) != kNativeRegionActions.end())
    {
        return an::ui::PreviewScaling::NativeRegion;
    }
    return an::ui::PreviewScaling::FullOnly;
}

/// The actions a frame over the part shown runs for: the native ones and the blurs at full scale.
[[nodiscard]] std::vector<std::string> regionActions()
{
    std::vector<std::string> actions;
    for (const std::string_view action : kDecimatedActions)
    {
        actions.emplace_back(action);
    }
    for (const std::string_view action : kNativeRegionActions)
    {
        actions.emplace_back(action);
    }
    return actions;
}

/// The actions whose dialogs offer a reach of many passes, and how a window is set to one: several passes of a window,
/// a window derived from a sigma. A margin of one pass falls short of each. The blurs are not among them: beyond the
/// stencil family's widest kernel their interactive frames blur by the real-time preview's boxes, whose reach
/// `GaussianBlurTest` and `DetailFilterTest` hold.
[[nodiscard]] std::vector<std::string> manyPassActions()
{
    return {"actionMedian_Filter", "actionBilateral_Filter", "actionMorphological_Transformation"};
}

void setManyPasses(std::string_view action, QWidget& window)
{
    if (action == "actionMedian_Filter")
    {
        choose(window, "kernelSize", QStringLiteral("5x5"));
        setWhole(window, "iterationsValue", 3);
    }
    else if (action == "actionBilateral_Filter")
    {
        setReal(window, "diameterValue", 0.0);
        setReal(window, "sigmaSpaceValue", 12.0);
    }
    else if (action == "actionMorphological_Transformation")
    {
        choose(window, "operatorSelector", QStringLiteral("Opening (Dilation + Erosion)"));
        setWhole(window, "iterationsValue", 2);
    }
}

/// The name a parameterized case is printed under: the action.
struct ActionName
{
    std::string operator()(const ::testing::TestParamInfo<std::string>& info) const
    {
        return info.param;
    }
};

/// A 640 by 480 grey Float32 scene: stars on a rippled sky, a grain no two neighbours share and a hot pixel in about a
/// thousand, so every window near any edge of a part of the scene has structure the edge would cut.
[[nodiscard]] std::optional<an::ui::ImageDocument> sceneDocument(const an::consumer::EngineSession& session)
{
    return an::tests::viewer::makeDocument<float>(
        session, 640u, 480u, an::tests::viewer::Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            double value = 0.2 + 0.08 * std::sin(static_cast<double>(x) / 7.0) * std::cos(static_cast<double>(y) / 5.0);
            for (std::uint32_t star = 0u; star < 60u; ++star)
            {
                const double dx = static_cast<double>(x) - static_cast<double>((star * 97u) % 640u);
                const double dy = static_cast<double>(y) - static_cast<double>((star * 61u) % 480u);
                value += 0.5 * std::exp(-(dx * dx + dy * dy) / 6.0);
            }
            const std::uint32_t hash = (x * 73856093u) ^ (y * 19349663u);
            value += 0.02 * (static_cast<double>(hash % 1000u) / 1000.0 - 0.5);
            if (hash % 997u == 0u)
            {
                value = 0.95;
            }
            return static_cast<float>(std::clamp(value, 0.0, 1.0));
        });
}

/// One case over one process: its spec and an installed application context.
class SpatialRegionFrameTest : public ::testing::TestWithParam<std::string>
{
protected:
    void SetUp() override
    {
        m_spec = spatialProcess(GetParam());
        ASSERT_NE(m_spec, nullptr);
        m_context = openProcessContext();
        ASSERT_NE(m_context, nullptr);
        m_installed = std::make_unique<InstalledContext>(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
    }

    [[nodiscard]] const SpatialProcessSpec& spec() const
    {
        return *m_spec;
    }

    [[nodiscard]] ProcessTestContext& context() const
    {
        return *m_context;
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    /// Opens the window over the scene, sets it as `set` does, turns its preview on with no preview area, zooms the
    /// preview view to four times, asks for a preview as a change does, and holds the interactive frame that runs to
    /// the Apply's samples over the part shown.
    void expectTheFrameOverThePartShownIsTheApplys(const std::function<void(QWidget&)>& set)
    {
        std::optional<an::ui::ImageDocument> document = sceneDocument(session());
        ASSERT_TRUE(document.has_value());
        const TargetView target(context(), "scene", std::move(*document));
        const ProbedWindow window = spec().open(target.view());
        set(*window);
        setChecked(*window, "preview", true);
        window->show();
        ASSERT_TRUE(settleRun(*window, target.view()));
        ImageViewControl* preview = window->previewView();
        ASSERT_NE(preview, nullptr);
        if (QCheckBox* area = preview->getRTPreviewAreaCheckBox(); area != nullptr && area->isChecked())
        {
            area->setChecked(false);
            ASSERT_TRUE(settleRun(*window, target.view()));
        }
        ASSERT_TRUE(window->previewArea().isEmpty());
        ImageViewWidget* widget = preview->getViewWidget();
        ASSERT_NE(widget, nullptr);
        widget->scaleImageView(4.0, true, true);
        ASSERT_TRUE(settleRun(*window, target.view()));
        ASSERT_TRUE(pumpUntilSettled([preview]() { return preview->isWorkIdle(); }));
        const std::optional<an::ui::ViewRegion> shown = widget->shownImageRegion();
        ASSERT_TRUE(shown.has_value());
        const QRect part(static_cast<int>(shown->x), static_cast<int>(shown->y), static_cast<int>(shown->width),
                         static_cast<int>(shown->height));
        const QRect bounds(0, 0, 640, 480);
        const int margin = static_cast<int>(window.probe->regionMargin());
        const QRect grown = part.adjusted(-margin, -margin, margin, margin).intersected(bounds);
        ASSERT_NE(grown, bounds) << "the part shown, grown by the reach, takes in the whole target";

        window.probe->forgetFrames();
        const ProcessRunTelemetry before = window->runTelemetry();
        window.probe->askForPreview();
        ASSERT_TRUE(settleRun(*window, target.view()));
        std::optional<RecordedFrame> interactive;
        for (const RecordedFrame& frame : window.probe->recordedFrames())
        {
            if (frame.kind == an::ui::PreviewFrameKind::Interactive)
            {
                interactive = frame;
                break;
            }
        }
        ASSERT_TRUE(interactive.has_value()) << "no interactive frame ran";
        EXPECT_TRUE(interactive->partOfTarget);
        ASSERT_EQ(interactive->part, grown) << "the body reads the part shown grown by the reach the process states";
        EXPECT_GT(window->runTelemetry().viewFramesLayered, before.viewFramesLayered) << "the frame was not layered";
        ASSERT_TRUE(interactive->samples.has_value());

        const an::Result<an::consumer::ImageAsset> applied =
            runProbeBody(*window.probe, session(), target.view().document(),
                         an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Apply});
        ASSERT_TRUE(applied.hasValue()) << applied.status().message();
        std::string error;
        const std::optional<fixtures::FixtureSamples> apply = fixtures::engine::readAssetSamples(*applied, error);
        ASSERT_TRUE(apply.has_value()) << error;
        std::uint64_t differ = 0u;
        std::ostringstream first;
        for (int y = part.top(); y <= part.bottom(); ++y)
        {
            for (int x = part.left(); x <= part.right(); ++x)
            {
                const double framed = interactive->samples->value(static_cast<std::uint32_t>(x - grown.x()),
                                                                  static_cast<std::uint32_t>(y - grown.y()), 0u);
                const double whole = apply->value(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 0u);
                const bool same = std::isnan(framed) ? std::isnan(whole) : framed == whole;
                if (!same && differ++ == 0u)
                {
                    first << "(" << x << "," << y << ") " << framed << " against " << whole;
                }
            }
        }
        EXPECT_EQ(differ, 0u) << "of the part shown " << part.width() << "x" << part.height() << " at " << part.x()
                              << "," << part.y() << " with a reach of " << margin
                              << " differs from the Apply, first at " << first.str();
    }

private:
    const SpatialProcessSpec* m_spec = nullptr;
    std::unique_ptr<ProcessTestContext> m_context;
    std::unique_ptr<InstalledContext> m_installed;
};

} // namespace

// Every spatial and noise process declares what its interactive frames may run over, as its body allows.
TEST(SpatialRegionFrames, EveryProcessDeclaresWhatItsFramesRunOver)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = sceneDocument(context->engineSession());
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "scene", std::move(*document));
    for (const SpatialProcessSpec& spec : spatialProcesses())
    {
        const ProbedWindow window = spec.open(target.view());
        EXPECT_EQ(window->previewScaling(), declaredScaling(spec.action))
            << spec.action << " declares " << an::ui::previewScalingName(window->previewScaling());
    }
}

// With no preview area drawn and the view showing a part of the target at full scale, the frame a change asks for at
// once runs over the part shown grown by the reach the process states - within the target - and is painted as a layer
// over what the view paints; over the part shown its samples are the Apply's, the part's edges included.
TEST_P(SpatialRegionFrameTest, AFrameOverThePartShownIsTheApplysAnswerThere)
{
    expectTheFrameOverThePartShownIsTheApplys([this](QWidget& window) { spec().configure(window); });
}

/// The processes whose dialogs offer a reach of many passes, set to one.
class SpatialRegionFrameAtManyPassesTest : public SpatialRegionFrameTest
{
};

// The reach a process states is every pass's - a window's over several iterations, every kernel's of a composed blur,
// the window derived from a sigma - so at such settings too the frame over the part shown is the Apply's there.
TEST_P(SpatialRegionFrameAtManyPassesTest, AFrameOverThePartShownIsTheApplysAnswerThere)
{
    expectTheFrameOverThePartShownIsTheApplys([this](QWidget& window) {
        spec().configure(window);
        setManyPasses(GetParam(), window);
    });
}

INSTANTIATE_TEST_SUITE_P(EveryProcessOverAPart, SpatialRegionFrameTest, ::testing::ValuesIn(regionActions()),
                         ActionName{});
INSTANTIATE_TEST_SUITE_P(EveryReachOfManyPasses, SpatialRegionFrameAtManyPassesTest,
                         ::testing::ValuesIn(manyPassActions()), ActionName{});
