// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The range selection's preview frames. Its lightness, selection and complement map each pixel alone and its
/// smoothing is a Gaussian a reduction scales, so an interactive frame runs over what the preview view paints: the
/// target reduced, smoothed by the smoothness reduced with it, or the part shown at full scale with the smoothing's
/// reach around it. A mask of a colour image's lightness cannot take the target's stretch, so in an autostretched
/// preview it is stretched by its own statistics, as its settled frame is: over a reduction, but never over a part at
/// full scale, where it runs over the whole target.

#include "Test_ProcessesMasks/MasksTestSupport.h"

#include "controls/ImageViewControl.h"
#include "processes/RangeSelectionProcess.h"
#include "session/ExecutionScope.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/ImageViewWidgetDisplayModule.h"
#include "widgets/ViewPresentation.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <QCheckBox>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::masks;
using an::tests::viewer::Channels;

/// The range selection's window, whose body and region margin a case reaches with the parameters it holds.
class ProbedRangeSelection final : public RangeSelectionProcess
{
public:
    using RangeSelectionProcess::RangeSelectionProcess;

    /// The body over a source and a destination, as a worker runs it.
    [[nodiscard]] an::Status runBody(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                     const an::ui::ProcessRun& run, an::ui::ExecutionScope& scope)
    {
        const auto params = getProcessParamsForExecution();
        return executeProcessImpl(source, destination, run, params.get(), scope);
    }

    /// How far around the part shown a frame over it reads.
    [[nodiscard]] std::uint32_t margin()
    {
        const auto params = getProcessParamsForExecution();
        return previewRegionMargin(params.get());
    }
};

/// A view of a sky with stars on a ripple and a ramp across it, colour or grey, of a size the case chooses.
[[nodiscard]] ViewHandle skyView(const an::consumer::EngineSession& session, Channels channels, std::uint32_t width,
                                 std::uint32_t height)
{
    std::optional<an::ui::ImageDocument> document = an::tests::viewer::makeDocument<float>(
        session, width, height, channels, [width, height](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            double value = 0.1 + 0.6 * static_cast<double>(x) / static_cast<double>(width) + 0.03 * c +
                           0.05 * std::sin(static_cast<double>(x) / 5.0) * std::cos(static_cast<double>(y) / 4.0);
            for (std::uint32_t star = 0u; star < 24u; ++star)
            {
                const double dx = static_cast<double>(x) - static_cast<double>((star * 37u) % width);
                const double dy = static_cast<double>(y) - static_cast<double>((star * 23u) % height);
                value += 0.5 * std::exp(-(dx * dx + dy * dy) / 3.0);
            }
            return static_cast<float>(std::clamp(value, 0.0, 1.0));
        });
    EXPECT_TRUE(document.has_value());
    return document.has_value() ? makeTargetView("target", std::move(*document)) : ViewHandle();
}

/// The window over a target, kept by the case, with its preview off; `probe` is the same window.
[[nodiscard]] WindowHandle openRangeSelection(ImageViewControl& target, ProbedRangeSelection*& probe)
{
    probe = new ProbedRangeSelection(&target);
    WindowHandle window(probe);
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    window->resize(900, 600);
    setChecked(*window, "preview", false);
    return window;
}

/// The window's body over the target's whole document, with the parameters it holds now and a run of a kind.
[[nodiscard]] std::optional<fixtures::FixtureSamples> bodyOver(ProbedRangeSelection& window,
                                                               const an::consumer::EngineSession& session,
                                                               const ImageViewControl& target,
                                                               const an::ui::ProcessRun& run)
{
    an::Result<an::consumer::AssetReadLease> lease = target.document().read();
    if (!lease.hasValue())
    {
        ADD_FAILURE() << lease.status().message();
        return std::nullopt;
    }
    const an::ui::ProcessSource source = an::ui::ProcessSource::of(*lease);
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    if (!scope.hasValue())
    {
        ADD_FAILURE() << scope.status().message();
        return std::nullopt;
    }
    an::Result<an::ui::ProcessDestination> destination =
        an::ui::ProcessDestination::copyOf(session, scope->engine(), scope->execution(), source);
    if (!destination.hasValue())
    {
        scope->finish(destination.status());
        ADD_FAILURE() << destination.status().message();
        return std::nullopt;
    }
    const an::Status ran = window.runBody(source, *destination, run, *scope);
    scope->finish(ran);
    if (ran.failed())
    {
        ADD_FAILURE() << ran.message();
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> made = destination->release();
    if (!made.hasValue())
    {
        ADD_FAILURE() << made.status().message();
        return std::nullopt;
    }
    return samplesOf(*made);
}

/// An interactive frame over the target reduced by a factor.
[[nodiscard]] an::ui::ProcessRun interactiveOver(std::uint32_t factor)
{
    an::ui::ProcessRun run{.kind = an::ui::ProcessRunKind::Preview, .frameKind = an::ui::PreviewFrameKind::Interactive};
    run.proxyFactor = factor;
    return run;
}

class RangeSelectionFrame : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(8u);
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

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

} // namespace

// An interactive frame runs over what the preview view paints, reading the smoothing's reach around a part shown at
// full scale - except, in an autostretched preview at full scale, a mask of a colour image's lightness, which cannot
// take the target's stretch and runs over the whole target. This view shows its small image without reducing it.
TEST_F(RangeSelectionFrame, AFrameRunsOverWhatTheViewPaintsUnlessAColourLightnessMaskIsStretched)
{
    ViewHandle colour = skyView(session(), Channels::Rgb, 96u, 72u);
    ASSERT_NE(colour, nullptr);
    ProbedRangeSelection* probe = nullptr;
    WindowHandle window = openRangeSelection(*colour, probe);
    setDouble(*window, "smoothnessValue", 8.0);
    EXPECT_EQ(probe->margin(), 32u) << "four deviations of the smoothing";
    setDouble(*window, "smoothnessValue", 0.0);
    EXPECT_EQ(probe->margin(), 0u);
    setChecked(*window, "lightness", true);
    EXPECT_EQ(window->previewScaling(), an::ui::PreviewScaling::Decimated);

    setChecked(*window, "preview", true);
    window->show();
    ASSERT_TRUE(settleWindow(*window));
    ImageViewControl* preview = window->previewView();
    ASSERT_NE(preview, nullptr);
    EXPECT_EQ(window->previewScaling(), an::ui::PreviewScaling::Decimated) << "a linear preview";
    preview->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(settleWindow(*window));
    EXPECT_EQ(window->previewScaling(), an::ui::PreviewScaling::FullOnly) << "a stretched colour lightness mask";
    setChecked(*window, "lightness", false);
    ASSERT_TRUE(settleWindow(*window));
    EXPECT_EQ(window->previewScaling(), an::ui::PreviewScaling::Decimated) << "a stretched mask of every channel";
    setChecked(*window, "preview", false);
    ASSERT_TRUE(settleWindow(*window));
    window.reset();

    ViewHandle grey = skyView(session(), Channels::Grey, 96u, 72u);
    ASSERT_NE(grey, nullptr);
    WindowHandle greyWindow = openRangeSelection(*grey, probe);
    setChecked(*greyWindow, "lightness", true);
    setChecked(*greyWindow, "preview", true);
    greyWindow->show();
    ASSERT_TRUE(settleWindow(*greyWindow));
    ASSERT_NE(greyWindow->previewView(), nullptr);
    greyWindow->previewView()->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(settleWindow(*greyWindow));
    EXPECT_EQ(greyWindow->previewScaling(), an::ui::PreviewScaling::Decimated) << "a stretched grey image's mask";
    setChecked(*greyWindow, "preview", false);
    ASSERT_TRUE(settleWindow(*greyWindow));
    greyWindow.reset();
    dropView(context(), grey);
    dropView(context(), colour);
}

// A stretched mask of a colour image's lightness is stretched by its own statistics: over the target reduced, whose
// statistics are the whole mask's but for the reduction's averaging, a frame runs over what the view paints; at full
// scale, where a part's statistics are its own, every frame runs over the whole target.
TEST_F(RangeSelectionFrame, AStretchedColourLightnessMaskRunsOverAReductionButNotOverAPart)
{
    ViewHandle colour = skyView(session(), Channels::Rgb, 2400u, 1800u);
    ASSERT_NE(colour, nullptr);
    ProbedRangeSelection* probe = nullptr;
    WindowHandle window = openRangeSelection(*colour, probe);
    setChecked(*window, "lightness", true);
    setChecked(*window, "preview", true);
    window->show();
    ASSERT_TRUE(settleWindow(*window));
    ImageViewControl* preview = window->previewView();
    ASSERT_NE(preview, nullptr);
    preview->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(settleWindow(*window));
    const std::optional<an::ui::ViewPresentationSettings> fitted =
        preview->getViewWidget() != nullptr ? preview->getViewWidget()->framePresentationSettings() : std::nullopt;
    ASSERT_TRUE(fitted.has_value());
    ASSERT_GE(fitted->proxyFactor, 2u) << "the view fits the image by reducing it";
    EXPECT_EQ(window->previewScaling(), an::ui::PreviewScaling::Decimated) << "over the target reduced";
    preview->zoom100();
    ASSERT_TRUE(settleWindow(*window));
    EXPECT_EQ(window->previewScaling(), an::ui::PreviewScaling::FullOnly) << "at full scale";
    setChecked(*window, "preview", false);
    ASSERT_TRUE(settleWindow(*window));
    window.reset();
    dropView(context(), colour);
}

// A frame over the target reduced by a factor is smoothed by the smoothness reduced with it: sample for sample what
// the body makes at full scale of the smoothness divided by the factor. A smoothness the reduction takes below the
// Gaussian's threshold smooths nothing rather than failing, and the settled frame is the Apply's answer.
TEST_F(RangeSelectionFrame, AFrameOverAReductionIsSmoothedByTheSmoothnessReduced)
{
    ViewHandle target = skyView(session(), Channels::Rgb, 96u, 72u);
    ASSERT_NE(target, nullptr);
    ProbedRangeSelection* probe = nullptr;
    WindowHandle window = openRangeSelection(*target, probe);
    setDouble(*window, "lowerLimitValue", 0.2);
    setDouble(*window, "upperLimitValue", 0.7);
    setDouble(*window, "fuzzinessValue", 0.1);
    setChecked(*window, "lightness", true);

    setDouble(*window, "smoothnessValue", 6.0);
    const std::optional<fixtures::FixtureSamples> reduced = bodyOver(*probe, session(), *target, interactiveOver(2u));
    const std::optional<fixtures::FixtureSamples> settled = bodyOver(
        *probe, session(), *target,
        an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Preview, .frameKind = an::ui::PreviewFrameKind::Settled});
    const std::optional<fixtures::FixtureSamples> applied =
        bodyOver(*probe, session(), *target, an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Apply});
    setDouble(*window, "smoothnessValue", 3.0);
    const std::optional<fixtures::FixtureSamples> halved = bodyOver(*probe, session(), *target, interactiveOver(1u));
    ASSERT_TRUE(reduced.has_value() && settled.has_value() && applied.has_value() && halved.has_value());
    EXPECT_TRUE(fixtures::sameSamples(*reduced, *halved)) << "a reduction by two smooths by half the smoothness";
    EXPECT_FALSE(fixtures::sameSamples(*reduced, *settled)) << "the full smoothness smooths further";
    EXPECT_TRUE(fixtures::sameSamples(*settled, *applied)) << "the settled frame is the Apply's answer";

    setDouble(*window, "smoothnessValue", 0.01);
    const std::optional<fixtures::FixtureSamples> belowThreshold =
        bodyOver(*probe, session(), *target, interactiveOver(20u));
    setDouble(*window, "smoothnessValue", 0.0);
    const std::optional<fixtures::FixtureSamples> unsmoothed =
        bodyOver(*probe, session(), *target, interactiveOver(1u));
    ASSERT_TRUE(belowThreshold.has_value() && unsmoothed.has_value());
    EXPECT_TRUE(fixtures::sameSamples(*belowThreshold, *unsmoothed)) << "a smoothing below the threshold is none";
    window.reset();
    dropView(context(), target);
}
