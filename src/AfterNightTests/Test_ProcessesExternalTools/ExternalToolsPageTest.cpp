// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The external tools as mirror pages, over the stand-in program: the page's run state follows the program - running,
/// moved by the program's own progress, ended - with no progress window of its own; the page's Cancel stops the
/// program; a failed program is said in the page's banner and opens no dialog; each page fits its column at 600 px of
/// height in both densities; and the same parameters give through the page the result the window gives.

#include "Test_ProcessesExternalTools/ExternalToolsTestSupport.h"
#include "Test_ProcessesShared/PageLayoutCheck.h"

#include "app/ProcessRegistry.h"
#include "controls/ImageProcessControl.h"
#include "controls/ProcessLaunchScope.h"
#include "controls/ProcessPageLayout.h"
#include "controls/ProgressDialogControl.h"

#include <QAbstractButton>
#include <QApplication>
#include <QFileInfo>
#include <QPointer>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace shared = an::tests::processes::shared;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using namespace an::tests::processes::externaltools;
using Phase = an::ui::ProcessRunState::Phase;

/// The cases of the tools' pages, each over a context of its own.
class ExternalToolsPages : public ::testing::Test
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
        m_context->presenter = nullptr;
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] ProcessTestContext& context() const
    {
        return *m_context;
    }

    /// A view of a tool's scene, made active.
    [[nodiscard]] ViewHandle openTarget(const ExternalTool& tool)
    {
        std::optional<an::consumer::ImageAsset> asset = sceneAsset(m_context->engineSession(), tool.scene);
        if (!asset.has_value())
        {
            return ViewHandle();
        }
        ViewHandle view = assetView(std::move(*asset), "target");
        activate(context(), *view);
        return view;
    }

    /// Opens a tool's window over a target as its launch does - under its descriptor, so a presenter takes it into a
    /// page when one is lent - with the suite's own floor for DeepSNR, set to its dialog's values, and shown.
    [[nodiscard]] static WindowHandle openLaunched(const ExternalTool& tool, ImageViewControl* target)
    {
        const an::ui::process_registry::ProcessDescriptor* descriptor = shared::registered(tool.descriptorId);
        EXPECT_NE(descriptor, nullptr) << tool.descriptorId;
        if (descriptor == nullptr)
        {
            return WindowHandle();
        }
        WindowHandle window;
        {
            const an::ui::ProcessLaunchScope launch(*descriptor);
            window = tool.open(target);
        }
        tool.configure(*window, "-");
        window->show();
        pumpQuietly();
        return window;
    }

    /// The progress window shown now; null when none is.
    [[nodiscard]] static ProgressDialogControl* shownProgressWindow()
    {
        for (QWidget* top : QApplication::topLevelWidgets())
        {
            if (auto* dialog = dynamic_cast<ProgressDialogControl*>(top); dialog != nullptr && dialog->isVisible())
            {
                return dialog;
            }
        }
        return nullptr;
    }

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

class ExternalTools : public ExternalToolsPages
{
};

class PageLayout : public ExternalToolsPages
{
protected:
    /// The page of a tool held to its column; reported skipped where text is not measured as the interface measures it.
    void fitsItsColumn(const char* name)
    {
        const ExternalTool* tool = toolNamed(name);
        ASSERT_NE(tool, nullptr);
        const shared::ClosesOpenedWindows closesWindows;
        ViewHandle target = openTarget(*tool);
        ASSERT_NE(target, nullptr);
        const bool measured =
            shared::expectPageFitsItsColumn(tool->descriptorId, *target, context(), an::ui::ProcessSurface::MirrorPage);
        dropView(context(), target);
        if (!measured && !::testing::Test::HasFailure())
        {
            GTEST_SKIP() << "Tab reaches every control; whether anything is clipped is measured under the native "
                            "platform, where text is as wide as the interface draws it";
        }
    }

    /// A tool run with its dialog's values through a window and, over a second target of the same image, through its
    /// page, over the stand-in that hands the image back: the two runs make the same images.
    void appliesAsInAWindow(const char* name)
    {
        const ExternalTool* tool = toolNamed(name);
        ASSERT_NE(tool, nullptr);
        const StandInTools standIn(context(), *tool, "half");
        const auto runIn = [&](const bool asPage) -> std::vector<fixtures::FixtureSamples> {
            std::vector<fixtures::FixtureSamples> results;
            ViewHandle target = openTarget(*tool);
            EXPECT_NE(target, nullptr);
            if (target == nullptr)
            {
                return results;
            }
            const std::size_t before = context().madeViews.size();
            {
                PagePresenter presenter;
                presenter.adopting = asPage;
                context().presenter = &presenter;
                WindowHandle window = openLaunched(*tool, target.get());
                EXPECT_NE(window, nullptr);
                if (window != nullptr)
                {
                    auto* const process = dynamic_cast<ImageProcessControl*>(window.get());
                    EXPECT_EQ(process != nullptr && process->hasPageLayout(), asPage);
                    EXPECT_TRUE(process != nullptr && process->triggerPrimaryAction()) << "the run starts";
                    EXPECT_TRUE(settleWindow(context(), *window));
                }
                window.reset();
                context().presenter = nullptr;
            }
            if (tool->inPlace)
            {
                if (const std::optional<fixtures::FixtureSamples> samples = documentSamples(target->document()))
                {
                    results.push_back(*samples);
                }
            }
            for (std::size_t index = before; index < context().madeViews.size(); ++index)
            {
                if (const std::optional<fixtures::FixtureSamples> samples =
                        documentSamples(context().madeViews[index]->document()))
                {
                    results.push_back(*samples);
                }
            }
            dropView(context(), target);
            return results;
        };
        const std::vector<fixtures::FixtureSamples> window = runIn(false);
        const std::vector<fixtures::FixtureSamples> page = runIn(true);
        ASSERT_FALSE(window.empty()) << name << " made a result";
        ASSERT_EQ(window.size(), page.size()) << name << ": the page made what the window made";
        for (std::size_t index = 0u; index < window.size(); ++index)
        {
            EXPECT_EQ(window[index].digest(), page[index].digest())
                << name << ": the page's result " << index << " is the window's";
        }
    }
};

} // namespace

// A tool's run on its page says where it stands in the page and the rail, with no progress window of its own: running
// from OK, moved by the program's own progress past the run's first tenth, and succeeded once its result is shown.
TEST_F(ExternalTools, RunStateFollowsTheTool)
{
    for (const ExternalTool& tool : externalTools())
    {
        SCOPED_TRACE(tool.name);
        const StandInTools standIn(context(), tool, "slow");
        qputenv(kStandInSlowMsVariable, QByteArray("2400"));
        ViewHandle target = openTarget(tool);
        ASSERT_NE(target, nullptr);
        PagePresenter presenter;
        context().presenter = &presenter;
        WindowHandle window = openLaunched(tool, target.get());
        ASSERT_NE(window, nullptr);
        auto* const process = dynamic_cast<ImageProcessControl*>(window.get());
        ASSERT_NE(process, nullptr);
        ASSERT_TRUE(process->isPresentedAsPage());
        an::ui::ProcessPageLayout* const page = process->pageLayout();
        ASSERT_NE(page, nullptr);

        ASSERT_TRUE(process->triggerPrimaryAction());
        ASSERT_TRUE(pumpUntil([process]() { return process->runState().running(); }));
        EXPECT_TRUE(pumpUntil([process]() {
            const std::optional<double> progress = process->runState().progress;
            return progress.has_value() && *progress > 0.1 && *progress < 0.9;
        })) << "the program's own progress moves the run";
        EXPECT_EQ(shownProgressWindow(), nullptr) << "a page shows the run in itself";
        EXPECT_TRUE(page->runBanner()->isVisibleTo(process));
        EXPECT_TRUE(page->cancelButton()->isVisibleTo(process));
        ASSERT_TRUE(settleWindow(context(), *window));
        EXPECT_EQ(process->runState().phase, Phase::Succeeded);
        EXPECT_TRUE(context().messages.isEmpty()) << context().messages.join(QStringLiteral("; ")).toStdString();

        // What the presenter was told: running, then progress that only rises, then the end.
        std::optional<double> last;
        bool rose = false;
        for (const auto& [told, state] : presenter.states)
        {
            if (told != process || !state.running() || !state.progress.has_value())
            {
                continue;
            }
            if (last.has_value())
            {
                EXPECT_GE(*state.progress, *last);
                rose = rose || *state.progress > *last;
            }
            last = state.progress;
        }
        EXPECT_TRUE(rose) << "the presenter was told the progress as it rose";
        ASSERT_FALSE(presenter.states.empty());
        EXPECT_EQ(presenter.states.back().second.phase, Phase::Succeeded);

        window.reset();
        context().presenter = nullptr;
        dropView(context(), target);
    }
}

// The page's Cancel stops the program, as a progress window's does: the run ends Cancelled at once, nothing is
// published and nothing is said.
TEST_F(ExternalTools, ThePagesCancelStopsTheProgram)
{
    for (const ExternalTool& tool : externalTools())
    {
        SCOPED_TRACE(tool.name);
        StandInTools standIn(context(), tool, "slow");
        qputenv(kStandInSlowMsVariable, QByteArray("20000"));
        ViewHandle target = openTarget(tool);
        ASSERT_NE(target, nullptr);
        const std::optional<fixtures::FixtureSamples> before = documentSamples(target->document());
        const std::size_t viewsBefore = context().madeViews.size();
        PagePresenter presenter;
        context().presenter = &presenter;
        WindowHandle window = openLaunched(tool, target.get());
        ASSERT_NE(window, nullptr);
        auto* const process = dynamic_cast<ImageProcessControl*>(window.get());
        ASSERT_NE(process, nullptr);
        an::ui::ProcessPageLayout* const page = process->pageLayout();
        ASSERT_NE(page, nullptr);

        ASSERT_TRUE(process->triggerPrimaryAction());
        ASSERT_TRUE(pumpUntil([&standIn]() { return !standIn.recordedRuns().isEmpty(); })) << "the program runs";
        ASSERT_TRUE(page->cancelButton()->isEnabled());
        const auto started = std::chrono::steady_clock::now();
        page->cancelButton()->click();
        ASSERT_TRUE(settleWindow(context(), *window));
        EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(8)) << "the program was stopped";
        EXPECT_EQ(process->runState().phase, Phase::Cancelled);
        EXPECT_FALSE(process->cancelRun()) << "nothing is left to stop";
        EXPECT_TRUE(context().messages.isEmpty()) << context().messages.join(QStringLiteral("; ")).toStdString();
        EXPECT_EQ(context().madeViews.size(), viewsBefore);
        const std::optional<fixtures::FixtureSamples> after = documentSamples(target->document());
        ASSERT_TRUE(before.has_value() && after.has_value());
        EXPECT_EQ(after->digest(), before->digest());

        window.reset();
        context().presenter = nullptr;
        dropView(context(), target);
    }
}

// A program that fails on a page is said in the page's banner under the run's words, and no dialog opens over the main
// window; the run ends Failed.
TEST_F(ExternalTools, AFailedProgramIsSaidInThePagesBanner)
{
    for (const ExternalTool& tool : externalTools())
    {
        SCOPED_TRACE(tool.name);
        const StandInTools standIn(context(), tool, "exit-code");
        ViewHandle target = openTarget(tool);
        ASSERT_NE(target, nullptr);
        PagePresenter presenter;
        context().presenter = &presenter;
        WindowHandle window = openLaunched(tool, target.get());
        ASSERT_NE(window, nullptr);
        auto* const process = dynamic_cast<ImageProcessControl*>(window.get());
        ASSERT_NE(process, nullptr);
        an::ui::ProcessPageLayout* const page = process->pageLayout();
        ASSERT_NE(page, nullptr);

        ASSERT_TRUE(runAndSettle(context(), *window, tool));
        EXPECT_TRUE(context().messages.isEmpty())
            << "no dialog: " << context().messages.join(QStringLiteral("; ")).toStdString();
        EXPECT_EQ(process->runState().phase, Phase::Failed);
        EXPECT_NE(page->message().indexOf(QString::fromLatin1(tool.errorLead)), -1) << page->message().toStdString();
        EXPECT_NE(page->message().indexOf(QStringLiteral("exited with code 3")), -1) << page->message().toStdString();
        const QWidget* const banner = process->findChild<QWidget*>(QStringLiteral("pageBanner"));
        ASSERT_NE(banner, nullptr);
        EXPECT_TRUE(banner->isVisibleTo(process)) << "the banner says it";

        window.reset();
        context().presenter = nullptr;
        context().messages.clear();
        context().errorTexts.clear();
        dropView(context(), target);
    }
}

TEST_F(PageLayout, StarnetFitsItsColumn)
{
    fitsItsColumn("Starnet");
}

TEST_F(PageLayout, StarnetStarMaskFitsItsColumn)
{
    fitsItsColumn("StarnetStarMask");
}

TEST_F(PageLayout, DeepSnrFitsItsColumn)
{
    fitsItsColumn("DeepSnr");
}

TEST_F(PageLayout, StarnetAppliesAsInAWindow)
{
    appliesAsInAWindow("Starnet");
}

TEST_F(PageLayout, StarnetStarMaskAppliesAsInAWindow)
{
    appliesAsInAWindow("StarnetStarMask");
}

TEST_F(PageLayout, DeepSnrAppliesAsInAWindow)
{
    appliesAsInAWindow("DeepSnr");
}
