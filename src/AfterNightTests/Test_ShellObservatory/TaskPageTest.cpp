// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A task's page in the Observatory window, as a person meets it with Curves Transformation: the page opened in the
/// centre with its rail item selected and the breadcrumb naming it; the same page found again after leaving it; a run's
/// bar, row and Cancel; a run left running while another place is shown; the snackbar of a run that succeeded and its
/// Undo; Esc and Ctrl+Enter; the image's floor beside each column; the compact readout's figures; a new image a run
/// makes landing in a workspace while the page stays. And what the window's actions act on while a page is shown: the
/// looking actions on its canvas, the image actions on its target, Close and Maximize View withdrawn, Apply from the
/// rail. And the controls of other families a page holds: a structuring element's grid edited in the column, a
/// chooser of a second image following the images open, a histogram and its toolbar acting in the column; and the
/// image's floor beside the wide column of every combination.

#include "ObservatoryWindowCase.h"

#include "ShellActions.h"
#include "chrome/TopBar.h"
#include "pages/TaskPage.h"
#include "tasks/TaskPresenter.h"
#include "workspaces/WorkspaceHost.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "app/ProcessRegistry.h"
#include "components/AnBreadcrumb.h"
#include "components/AnCompactReadout.h"
#include "components/AnIconButton.h"
#include "components/AnIconRail.h"
#include "components/AnSnackbar.h"
#include "components/AnTaskHeader.h"
#include "components/RailItem.h"
#include "controls/ImageProcessControl.h"
#include "controls/ImageRTPreviewControl.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessLaunchScope.h"
#include "controls/ProcessPageLayout.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "controls/ViewProcessingNotice.h"
#include "core/ApplicationCore.h"
#include "processes/CurvesTransformationProcess.h"
#include "readout/ReadoutController.h"
#include "services/ProcessLaunchService.h"
#include "tasks/TaskModel.h"
#include "views/ViewChromeBinder.h"
#include "views/ViewService.h"
#include "widgets/CurvesWidget.h"
#include "widgets/HistogramWidget.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/StructuredElementWidget.h"
#include "widgets/StructuringElementGrid.h"
#include "widgets/ToneCurveModel.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <memory>
#include <string_view>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using an::ui::ProcessRunState;
using an::ui::shell::Task;
using an::ui::shell::TaskId;
using Phase = ProcessRunState::Phase;
namespace shellIds = an::ui::observatory::ids;
namespace actionIds = an::ui::shell::ids;

constexpr std::string_view kCurves = "transforms.actionCurves_Transformation";
constexpr std::string_view kMorphology = "filters.actionMorphological_Transformation";
constexpr std::string_view kStarReduction = "star_object.actionStar_Reduction";
constexpr std::string_view kImageBlend = "multi_image_stacking.actionImage_Blend";
constexpr std::string_view kImageCompare = "multi_image_stacking.actionImage_Compare";
constexpr std::string_view kLrgbCombination = "color.actionLRGB_Combination";
constexpr std::string_view kNbrgbCombination = "color.actionNBRGB_Combination";
constexpr std::string_view kNbColorMapper = "color.actionNBColorMapper";
constexpr std::string_view kScnr = "color.actionSCNR";

/// Whether the platform measures text as the interface does: the box font of the suites' platform gives every
/// character one width.
[[nodiscard]] bool textIsMeasured()
{
    const QFontMetrics metrics(QApplication::font());
    return metrics.horizontalAdvance(QStringLiteral("iiiiiiiiii")) <
           metrics.horizontalAdvance(QStringLiteral("WWWWWWWWWW"));
}

/// A process's part of a class the process does not name.
template <typename PartT> [[nodiscard]] PartT* partOf(const QWidget& process)
{
    for (QWidget* widget : process.findChildren<QWidget*>())
    {
        if (auto* part = dynamic_cast<PartT*>(widget))
        {
            return part;
        }
    }
    return nullptr;
}

/// A process's label of a text; null when it has none.
[[nodiscard]] const QLabel* labelled(const QWidget& process, const QString& text)
{
    for (const QLabel* label : process.findChildren<QLabel*>())
    {
        if (label->text() == text)
        {
            return label;
        }
    }
    return nullptr;
}

/// Lets the window settle: posted work delivered, the deletions done, the layouts applied.
void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// The item the rail shows under an id; a default item when it shows none.
[[nodiscard]] an::ui::RailItem railItem(const an::ui::AnIconRail& rail, const QString& id)
{
    for (const an::ui::RailItem& item : rail.items())
    {
        if (item.id == id)
        {
            return item;
        }
    }
    return an::ui::RailItem{};
}

/// The curve's widget of Curves Transformation.
[[nodiscard]] CurvesWidget* curvesOf(const QWidget& process)
{
    for (QWidget* widget : process.findChildren<QWidget*>())
    {
        if (auto* curves = dynamic_cast<CurvesWidget*>(widget))
        {
            return curves;
        }
    }
    return nullptr;
}

/// The cases of a task's page: each opens Curves Transformation over a view of its own.
class TaskPageCase : public ObservatoryWindowCase
{
protected:
    [[nodiscard]] static an::ui::observatory::TaskPresenter& tasks()
    {
        an::ui::observatory::TaskPresenter* presenter = window().taskPresenter();
        EXPECT_NE(presenter, nullptr);
        return *presenter;
    }

    /// Opens a process over the active view as its menu entry does; the process opened, null when none did.
    static TopLevelProcessBaseControl* launch(std::string_view descriptorId)
    {
        const std::size_t before = tasks().model().tasks().size();
        EXPECT_TRUE(core().processLaunch().launch(std::string(descriptorId)));
        settle();
        const std::vector<Task>& now = tasks().model().tasks();
        EXPECT_EQ(now.size(), before + 1u);
        return now.size() > before ? now.back().process.data() : nullptr;
    }

    /// The process's page widget in the centre; null for none.
    [[nodiscard]] static an::ui::observatory::TaskPage* pageOf(const TopLevelProcessBaseControl* process)
    {
        const Task* task = tasks().model().taskFor(process);
        return task != nullptr ? window().taskPage(task->id) : nullptr;
    }

    /// Bends the curve, which offers Apply.
    static void bend(TopLevelProcessBaseControl& process)
    {
        CurvesWidget* curves = curvesOf(process);
        ASSERT_NE(curves, nullptr);
        curves->setCurve(an::ui::ToneCurveModel(std::vector<an::CurveControlPoint>{{0.0, 0.0}, {0.3, 0.6}, {1.0, 1.0}},
                                                curves->getCurve().interpolation()));
        settle();
    }

    /// Waits for a process's runs and its preview's work to land.
    [[nodiscard]] static bool idle(TopLevelProcessBaseControl* process)
    {
        QPointer<TopLevelProcessBaseControl> guard(process);
        return an::tests::app::processEventsUntil([&]() {
            auto* preview = dynamic_cast<ImageRTPreviewControl*>(guard.data());
            return guard.isNull() || preview == nullptr || preview->isProcessIdle();
        });
    }

    /// The number of the step the view's history shows.
    [[nodiscard]] static std::size_t currentStep(const ImageViewControl& view)
    {
        return view.viewHistory() != nullptr ? view.viewHistory()->steps().current : 0u;
    }

    /// Opens a colour image of a gentle gradient as the active view, for the processes that combine colour images.
    static ImageViewControl* openColourView(const QString& name)
    {
        constexpr std::uint32_t kWidth = 32u;
        constexpr std::uint32_t kHeight = 24u;
        an::Result<an::consumer::SessionHold> hold = session().hold();
        EXPECT_TRUE(hold.hasValue());
        if (!hold.hasValue())
        {
            return nullptr;
        }
        const std::optional<an::ImageExtent> extent =
            an::ImageExtent::create(kWidth, kHeight, 3u, hold->engine().limits());
        const std::optional<an::ImageDescriptor> descriptor =
            extent.has_value() ? an::ImageDescriptor::create(
                                     an::ImageDescriptorRequest{.extent = *extent,
                                                                .sampleType = an::SampleType::Float32,
                                                                .channelSchema = an::ChannelSchema::Rgb,
                                                                .colorSpace = an::ColorSpace::Unspecified,
                                                                .numericDomain = an::NumericDomain::NormalizedLinear,
                                                                .cfa = an::CfaDescriptor{},
                                                                .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                                .pixelAspectRatio = 1.0})
                               : std::nullopt;
        EXPECT_TRUE(descriptor.has_value());
        if (!descriptor.has_value())
        {
            return nullptr;
        }
        an::Result<an::Image<float>> image = an::Image<float>::create(
            hold->engine(), *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
        EXPECT_TRUE(image.hasValue());
        if (!image.hasValue())
        {
            return nullptr;
        }
        {
            std::optional<an::ImageWriteAccess<float>> access = image->write();
            EXPECT_TRUE(access.has_value());
            if (!access.has_value())
            {
                return nullptr;
            }
            for (std::uint32_t c = 0u; c < 3u; ++c)
            {
                for (std::uint32_t y = 0u; y < kHeight; ++y)
                {
                    auto row = access->view().row(c, y);
                    if (!row.has_value())
                    {
                        return nullptr;
                    }
                    for (std::uint32_t x = 0u; x < kWidth; ++x)
                    {
                        (*row)[x] = 0.1f + 0.02f * static_cast<float>(x + y + 4u * c) / 4.0f;
                    }
                }
            }
        }
        an::Result<an::consumer::ImageAsset> asset =
            an::consumer::ImageAsset::adopt(session(), an::consumer::AnyImage(std::move(*image)));
        EXPECT_TRUE(asset.hasValue());
        if (!asset.hasValue())
        {
            return nullptr;
        }
        an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
            std::move(*asset), an::ui::DocumentNames{.displayName = name, .sourcePath = {}});
        EXPECT_TRUE(document.hasValue());
        if (!document.hasValue())
        {
            return nullptr;
        }
        ImageViewControl* const view = core().viewService().userViews().createNewImageViewControlFromImage(
            nullptr, std::move(*document), nullptr, name.toStdString());
        EXPECT_NE(view, nullptr);
        if (view != nullptr)
        {
            core().viewService().setActiveImageViewControl(view);
            settle();
        }
        return view;
    }

    void TearDown() override
    {
        // A run state a case set by hand is ended, so its page closes.
        for (const Task& task : tasks().model().tasks())
        {
            if (!task.process.isNull() && task.process->runState().running())
            {
                task.process->setRunState(ProcessRunState{.phase = Phase::Cancelled});
            }
        }
        for (const Task& task : std::vector<Task>(tasks().model().tasks()))
        {
            if (!task.process.isNull())
            {
                (void)task.process->close();
            }
        }
        settle();
        ObservatoryWindowCase::TearDown();
    }
};

class TaskPage : public TaskPageCase
{
};

class TaskSubject : public TaskPageCase
{
};

/// The page of a process with no preview: a view of its target in the canvas.
class MirrorPage : public TaskPageCase
{
};

/// The notice a view shows now; null while it shows none.
[[nodiscard]] const an::ui::ViewProcessingNotice* noticeOf(const ImageViewControl& view)
{
    for (const an::ui::ViewProcessingNotice* notice : view.findChildren<an::ui::ViewProcessingNotice*>())
    {
        if (notice->isVisibleTo(&view))
        {
            return notice;
        }
    }
    return nullptr;
}

} // namespace

// Curves Transformation opens as a page of the centre: its rail item selected and not floating, the breadcrumb naming
// it and its image, its header naming them, the readout's column of the workspace hidden.
TEST_F(TaskPage, OpeningCurvesTransformationShowsItsPage)
{
    ImageViewControl* const view = openView(QStringLiteral("page-opens"));
    ASSERT_NE(view, nullptr);
    window().showViews();
    TopLevelProcessBaseControl* const process = launch(kCurves);
    ASSERT_NE(process, nullptr);
    EXPECT_TRUE(process->isPresentedAsPage());
    EXPECT_FALSE(process->isWindow());
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    EXPECT_TRUE(window().isTaskPageShown());
    EXPECT_EQ(window().centre()->currentWidget(), page);
    EXPECT_TRUE(page->isAncestorOf(process));
    const Task* task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(tasks().model().current(), task->id);
    EXPECT_EQ(task->presentation, an::ui::shell::TaskPresentation::Page);
    const an::ui::RailItem item =
        railItem(*window().rail(), an::ui::observatory::ObservatoryWindow::railTaskId(task->id));
    EXPECT_TRUE(item.selected);
    EXPECT_FALSE(item.floating);
    EXPECT_EQ(core().viewService().subjectView(), view);
    EXPECT_FALSE(window().rightColumn()->isVisible()) << "the page has its own column";
    EXPECT_EQ(page->header()->processName(), QStringLiteral("Curves Transformation"));
    EXPECT_EQ(page->header()->targetName(), QStringLiteral("page-opens"));
    ASSERT_NE(process->pageLayout(), nullptr);
    EXPECT_EQ(process->pageLayout()->category(), QStringLiteral("Intensity Transformations"))
        << "the eyebrow is the process's menu";
}

// The breadcrumb names the process and the image while the page is shown.
TEST_F(TaskPage, TheBreadcrumbNamesTheProcessAndTheImage)
{
    ImageViewControl* const view = openView(QStringLiteral("page-breadcrumb"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kCurves);
    ASSERT_NE(process, nullptr);
    EXPECT_EQ(window().topBar()->breadcrumb()->parts(),
              (QStringList{QStringLiteral("Curves Transformation"), QStringLiteral("page-breadcrumb")}));
}

// Leaving the page and coming back by the rail shows the same page, with the same process in it.
TEST_F(TaskPage, LeavingAndReturningShowsTheSameWidget)
{
    ImageViewControl* const view = openView(QStringLiteral("page-return"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kCurves);
    ASSERT_NE(process, nullptr);
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    const TaskId id = tasks().model().taskFor(process)->id;

    window().showViews();
    settle();
    EXPECT_FALSE(window().isTaskPageShown());
    EXPECT_EQ(tasks().model().current(), 0u) << "no task is looked at";
    EXPECT_FALSE(core().viewService().hasTaskSubject());

    const QRect place = window().rail()->itemRect(an::ui::observatory::ObservatoryWindow::railTaskId(id));
    ASSERT_TRUE(place.isValid());
    QTest::mouseClick(window().rail(), Qt::LeftButton, Qt::NoModifier, place.center());
    settle();
    EXPECT_EQ(window().centre()->currentWidget(), page);
    EXPECT_TRUE(page->isAncestorOf(process));
    EXPECT_EQ(tasks().model().current(), id);
}

// During a run the header's bar shows the progress, the page's run row and Cancel are shown and its controls disabled.
TEST_F(TaskPage, ARunShowsTheBarTheRowTheDisabledControlsAndCancel)
{
    ImageViewControl* const view = openView(QStringLiteral("page-run"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kCurves);
    ASSERT_NE(process, nullptr);
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    an::ui::ProcessPageLayout* const layout = process->pageLayout();
    ASSERT_NE(layout, nullptr);

    process->setRunState(an::ui::runningState({}, 0.42));
    settle();
    EXPECT_EQ(page->header()->state(), an::ui::AnTaskHeader::State::Running);
    EXPECT_TRUE(page->header()->stateText().contains(QStringLiteral("42 %")));
    EXPECT_FALSE(page->header()->runBarRect().isEmpty());
    EXPECT_TRUE(layout->runBanner()->isVisible());
    EXPECT_TRUE(layout->cancelButton()->isVisible());
    EXPECT_FALSE(layout->bodyContent()->isEnabled());
    EXPECT_FALSE(layout->toggles()->isEnabled());
    EXPECT_FALSE(page->header()->closeButton()->isEnabled()) << "a running task is not closed";

    process->setRunState(ProcessRunState{.phase = Phase::Cancelled});
    settle();
    EXPECT_FALSE(layout->runBanner()->isVisible());
    EXPECT_TRUE(layout->bodyContent()->isEnabled());
    EXPECT_TRUE(page->header()->runBarRect().isEmpty());
}

// A run left by showing another place goes on, and ends with one step in its image's history.
TEST_F(TaskPage, LeavingDuringARunLeavesItRunning)
{
    ImageViewControl* const view = openView(QStringLiteral("page-leave-run"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    bend(*process);
    ASSERT_TRUE(idle(process));
    const std::size_t before = currentStep(*view);

    process->applyProcess();
    window().showViews();
    EXPECT_FALSE(window().isTaskPageShown());
    EXPECT_TRUE(process->runState().running()) << "the run goes on when its page is left";
    ASSERT_TRUE(idle(process));
    settle();
    EXPECT_EQ(currentStep(*view), before + 1u) << "one step in the image's history";
    EXPECT_FALSE(view->isLockedForProcessing());
}

// A run that succeeds says so in the snackbar, with how long it took and Undo, which undoes the image's step.
TEST_F(TaskPage, SuccessShowsTheSnackbarAndUndoUndoes)
{
    ImageViewControl* const view = openView(QStringLiteral("page-success"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    bend(*process);
    ASSERT_TRUE(idle(process));
    const std::size_t before = currentStep(*view);

    process->applyProcess();
    ASSERT_TRUE(idle(process));
    settle();
    an::ui::AnSnackbar* const snackbar = window().snackbar();
    ASSERT_NE(snackbar, nullptr);
    EXPECT_TRUE(snackbar->isVisible());
    EXPECT_EQ(snackbar->text(), QStringLiteral("Curves Transformation applied"));
    EXPECT_TRUE(snackbar->detail().endsWith(QStringLiteral(" s")));
    EXPECT_EQ(snackbar->actionText(), QStringLiteral("Undo"));
    ASSERT_EQ(currentStep(*view), before + 1u);
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->header()->state(), an::ui::AnTaskHeader::State::Applied);
    EXPECT_TRUE(page->header()->stateText().startsWith(QStringLiteral("applied")));

    snackbar->actionButton()->click();
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return currentStep(*view) == before; }));
    EXPECT_FALSE(snackbar->isVisible()) << "the action dismisses the message";
}

// Esc closes an idle page and does nothing during a run; the place shown before comes back.
TEST_F(TaskPage, EscClosesWhenIdleAndNotDuringARun)
{
    ImageViewControl* const view = openView(QStringLiteral("page-esc"));
    ASSERT_NE(view, nullptr);
    window().showViews();
    settle();
    TopLevelProcessBaseControl* const process = launch(kCurves);
    ASSERT_NE(process, nullptr);
    const QPointer<TopLevelProcessBaseControl> guard(process);
    QWidget* const focus = process->pageLayout()->footerButton(QDialogButtonBox::Reset);
    ASSERT_NE(focus, nullptr);

    process->setRunState(an::ui::runningState());
    QTest::keyClick(focus, Qt::Key_Escape);
    settle();
    EXPECT_FALSE(guard.isNull());
    EXPECT_TRUE(window().isTaskPageShown()) << "not during a run";

    process->setRunState(ProcessRunState{.phase = Phase::Cancelled});
    QTest::keyClick(focus, Qt::Key_Escape);
    settle();
    EXPECT_TRUE(guard.isNull()) << "Esc closed the idle page";
    EXPECT_FALSE(window().isTaskPageShown());
    EXPECT_TRUE(window().isWorkspaceShown()) << "the place shown before the task";
}

// Ctrl+Enter inside the page applies.
TEST_F(TaskPage, CtrlEnterApplies)
{
    ImageViewControl* const view = openView(QStringLiteral("page-ctrl-enter"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    bend(*process);
    ASSERT_TRUE(idle(process));
    const std::size_t before = currentStep(*view);
    QWidget* const focus = process->pageLayout()->footerButton(QDialogButtonBox::Reset);
    ASSERT_NE(focus, nullptr);
    focus->setFocus(Qt::OtherFocusReason);
    settle();

    QTest::keyClick(focus, Qt::Key_Return, Qt::ControlModifier);
    ASSERT_TRUE(idle(process));
    settle();
    EXPECT_EQ(currentStep(*view), before + 1u);
}

// In a window 1536 px wide the image keeps 1130 px beside a standard column, and 970 px beside a wide one.
TEST_F(TaskPage, TheImageKeepsItsFloor)
{
    ImageViewControl* const view = openView(QStringLiteral("page-floor"));
    ASSERT_NE(view, nullptr);
    ASSERT_EQ(window().width(), 1536);

    // Curves Transformation's own column is the wide one.
    TopLevelProcessBaseControl* const wide = launch(kCurves);
    ASSERT_NE(wide, nullptr);
    settle();
    ASSERT_NE(wide->pageLayout(), nullptr);
    EXPECT_EQ(wide->pageLayout()->columnWidth(), 520);
    EXPECT_GE(wide->pageLayout()->canvas()->width(), 970);
    (void)wide->close();
    settle();

    // The same process made inside a launch that states a standard column.
    const an::ui::process_registry::ProcessDescriptor* registered =
        an::ui::process_registry::ProcessRegistry::instance().find(std::string(kCurves));
    ASSERT_NE(registered, nullptr);
    an::ui::process_registry::ProcessDescriptor standard = *registered;
    standard.columnWidth = an::ui::ProcessColumnWidth::Standard;
    {
        const an::ui::ProcessLaunchScope launched(standard);
        auto* const made = new CurvesTransformationProcess(view);
        made->show();
    }
    settle();
    ASSERT_FALSE(tasks().model().tasks().empty());
    TopLevelProcessBaseControl* const narrow = tasks().model().tasks().back().process.data();
    ASSERT_NE(narrow, nullptr);
    ASSERT_NE(narrow->pageLayout(), nullptr);
    EXPECT_EQ(narrow->pageLayout()->columnWidth(), 360);
    EXPECT_GE(narrow->pageLayout()->canvas()->width(), 1130);
}

// In a window 1536 px wide each process of the wide column keeps 970 px of image, however wide its controls were built:
// the combinations' rows are wider than the column until their side margins give way, and the column keeps its width.
TEST_F(TaskPage, AWideColumnKeeps970PixelsOfImage)
{
    ImageViewControl* const view = openColourView(QStringLiteral("page-wide-floor"));
    ASSERT_NE(view, nullptr);
    ASSERT_EQ(window().width(), 1536);
    for (const std::string_view id : {kImageBlend, kImageCompare, kLrgbCombination, kNbrgbCombination, kNbColorMapper})
    {
        SCOPED_TRACE(std::string(id));
        TopLevelProcessBaseControl* const process = launch(id);
        ASSERT_NE(process, nullptr);
        settle();
        ASSERT_TRUE(process->isPresentedAsPage());
        an::ui::ProcessPageLayout* const layout = process->pageLayout();
        ASSERT_NE(layout, nullptr);
        EXPECT_EQ(layout->columnWidth(), 520);
        EXPECT_EQ(layout->column()->width(), 520) << "the column keeps its width";
        EXPECT_GE(layout->canvas()->width(), 970);
        (void)process->close();
        settle();
    }
}

// A process's histogram and its toolbar are in its column, the histogram the width the column gives it: Zoom In zooms
// the histogram inside its own frame, which scrolls, while the column keeps its width and never scrolls sideways; the
// grid button draws the grid.
TEST_F(TaskPage, TheHistogramToolbarActsInTheColumn)
{
    ImageViewControl* const view = openColourView(QStringLiteral("page-histogram"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kLrgbCombination));
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->isPresentedAsPage());
    ASSERT_TRUE(idle(process));
    settle();
    an::ui::ProcessPageLayout* const layout = process->pageLayout();
    ASSERT_NE(layout, nullptr);
    auto* const chart = process->findChild<QScrollArea*>(QStringLiteral("chartScrollArea"));
    ASSERT_NE(chart, nullptr);
    auto* const histogram = dynamic_cast<HistogramWidget*>(chart->widget());
    ASSERT_NE(histogram, nullptr);
    EXPECT_TRUE(layout->bodyContent()->isAncestorOf(chart)) << "the histogram is in the column";
    ASSERT_TRUE(chart->isVisible());
    EXPECT_LE(chart->mapTo(layout->bodyContent(), QPoint(chart->width(), 0)).x(), layout->body()->viewport()->width())
        << "the histogram lies in the column";
    auto* const zoomIn = process->findChild<QAbstractButton*>(QStringLiteral("zoomIn"));
    auto* const grid = process->findChild<QAbstractButton*>(QStringLiteral("gridButton"));
    ASSERT_TRUE(zoomIn != nullptr && grid != nullptr);
    EXPECT_TRUE(layout->bodyContent()->isAncestorOf(zoomIn)) << "the toolbar is in the column";

    const int before = histogram->width();
    const int column = layout->column()->width();
    zoomIn->click();
    settle();
    EXPECT_GT(histogram->width(), before) << "the histogram zoomed";
    EXPECT_GT(histogram->width(), chart->viewport()->width()) << "it scrolls inside its own frame";
    EXPECT_EQ(layout->column()->width(), column) << "the column keeps its width";
    EXPECT_FALSE(layout->body()->horizontalScrollBar()->isVisible()) << "the body never scrolls sideways";

    const bool gridShown = histogram->getGridEnabled();
    grid->click();
    settle();
    EXPECT_NE(histogram->getGridEnabled(), gridShown) << "the grid button draws the grid or takes it away";
}

// The compact readout shows the settled preview's figures and their change against the image.
TEST_F(TaskPage, TheCompactReadoutShowsSettledFiguresAndTheirChange)
{
    ImageViewControl* const view = openView(QStringLiteral("page-readout"), 0.3f);
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    bend(*process);
    ASSERT_TRUE(idle(process));
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    EXPECT_TRUE(process->pageLayout()->readoutSlot()->isAncestorOf(page->compactReadout()))
        << "the compact readout heads the process's column";
    an::ui::shell::ReadoutController& readout = window().readoutController();
    EXPECT_TRUE(readout.followsTask());
    EXPECT_EQ(readout.followedView(), view);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return process->previewFramesShown() > 0u; }, 10000));
    readout.updateNow();
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() { return readout.isIdle() && readout.shownFigures().has_value() && !readout.isStale(); }, 10000));
    ASSERT_TRUE(readout.originalFigures().has_value()) << "the change is against the image's figures";
    EXPECT_FALSE(page->compactReadout()->isStale());
    EXPECT_FALSE(page->compactReadout()->modeText().isEmpty());

    // Leaving the page, the readout follows the active view again.
    window().showViews();
    settle();
    EXPECT_FALSE(readout.followsTask());
}

// A new image a run makes while the page is shown lands in the workspace; the page stays, and the snackbar's Show goes
// to the image.
TEST_F(TaskPage, AResultThatIsANewImageDoesNotLeaveThePage)
{
    ImageViewControl* const view = openView(QStringLiteral("page-new-image"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kCurves);
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(window().isTaskPageShown());

    std::optional<an::ui::ImageDocument> document =
        an::tests::app::filledDocument(session(), QStringLiteral("stars_only"), 0.6f);
    ASSERT_TRUE(document.has_value());
    ImageViewControl* const made = core().viewService().createNewImageViewControlFromImage(
        nullptr, std::move(*document), nullptr, "stars_only", ViewControlMode::standard, view, false, false);
    ASSERT_NE(made, nullptr);
    settle();
    EXPECT_TRUE(window().isTaskPageShown()) << "the person stays on the page";
    EXPECT_FALSE(window().workspaces().workspaceOf(*made).isEmpty()) << "the image is in a workspace";
    an::ui::AnSnackbar* const snackbar = window().snackbar();
    ASSERT_TRUE(snackbar->isVisible());
    EXPECT_TRUE(snackbar->text().startsWith(QStringLiteral("stars_only opened in "))) << snackbar->text().toStdString();
    EXPECT_EQ(snackbar->actionText(), QStringLiteral("Show"));

    snackbar->actionButton()->click();
    settle();
    EXPECT_TRUE(window().isWorkspaceShown());
    EXPECT_EQ(core().viewService().getActiveImageViewControl(), made);
}

// Morphological Transformation's structuring element is a grid a click edits: on its page the grid is in the column,
// whole where text is measured as the interface draws it, and a click on a cell turns the cell over and the preview
// follows.
TEST_F(TaskPage, TheStructuringElementGridIsEditedInTheColumn)
{
    ImageViewControl* const view = openView(QStringLiteral("page-morphology"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kMorphology));
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->isPresentedAsPage());
    an::ui::ProcessPageLayout* const layout = process->pageLayout();
    ASSERT_NE(layout, nullptr);
    EXPECT_EQ(layout->columnWidth(), 520);
    StructuredElementWidget* const grid = partOf<StructuredElementWidget>(*process);
    ASSERT_NE(grid, nullptr);
    ASSERT_TRUE(idle(process));
    settle();
    EXPECT_TRUE(layout->bodyContent()->isAncestorOf(grid)) << "the grid is in the column";
    ASSERT_TRUE(grid->isVisible()) << "its section is open";
    EXPECT_FALSE(grid->size().isEmpty());
    if (textIsMeasured())
    {
        const QPoint at = grid->mapTo(layout->bodyContent(), QPoint(0, 0));
        EXPECT_GE(at.x(), 0);
        EXPECT_LE(at.x() + grid->width(), layout->body()->viewport()->width()) << "the grid lies whole in the column";
    }

    const an::ui::StructuringElementGrid before = grid->structuringElement();
    const std::uint64_t frames = process->previewFramesShown();
    QTest::mouseClick(grid, Qt::LeftButton, Qt::NoModifier, grid->rect().center());
    settle();
    EXPECT_FALSE(grid->structuringElement() == before) << "the click turned a cell over";
    EXPECT_TRUE(an::tests::app::processEventsUntil([&]() { return process->previewFramesShown() > frames; }, 10000))
        << "the preview follows the grid";
}

// Star Reduction chooses, beside its target, the starless image it reads the stars against: on its page that chooser is
// in the column with its label, lists the images open and one opened while the page is shown, and the target's chooser
// leads the body with its own label.
TEST_F(TaskPage, AChooserOfASecondImageFollowsTheOpenImages)
{
    ImageViewControl* const starless = openView(QStringLiteral("page-starless"));
    ImageViewControl* const view = openView(QStringLiteral("page-star-reduction"));
    ASSERT_TRUE(starless != nullptr && view != nullptr);
    TopLevelProcessBaseControl* const process = launch(kStarReduction);
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->isPresentedAsPage());
    an::ui::ProcessPageLayout* const layout = process->pageLayout();
    ASSERT_NE(layout, nullptr);
    auto* const chooser = process->findChild<QComboBox*>(QStringLiteral("starlessImageSelector"));
    auto* const target = process->findChild<QComboBox*>(QStringLiteral("targetImageSelector"));
    ASSERT_TRUE(chooser != nullptr && target != nullptr);
    EXPECT_TRUE(layout->bodyContent()->isAncestorOf(chooser)) << "the chooser is in the column";
    EXPECT_GE(chooser->findText(QStringLiteral("page-starless")), 0) << "it lists the images open";

    // Each chooser is beside its own label: the target's at the head of the body, the starless one's where it was.
    const QLabel* const targetLabel = labelled(*process, QStringLiteral("Target image"));
    const QLabel* const starlessLabel = labelled(*process, QStringLiteral("Starless image"));
    ASSERT_TRUE(targetLabel != nullptr && starlessLabel != nullptr);
    EXPECT_EQ(targetLabel->parentWidget(), target->parentWidget()) << "the target's label went with its chooser";
    EXPECT_EQ(starlessLabel->parentWidget(), chooser->parentWidget());
    const QWidget* const views = process->findChild<QWidget*>(QStringLiteral("viewsGroup"));
    ASSERT_NE(views, nullptr);
    EXPECT_FALSE(views->isAncestorOf(targetLabel)) << "no label of the target is left in the group it came from";

    std::optional<an::ui::ImageDocument> document =
        an::tests::app::filledDocument(session(), QStringLiteral("page-starless-later"), 0.2f);
    ASSERT_TRUE(document.has_value());
    ImageViewControl* const later = core().viewService().createNewImageViewControlFromImage(
        nullptr, std::move(*document), nullptr, "page-starless-later", ViewControlMode::standard, view, false, false);
    ASSERT_NE(later, nullptr);
    settle();
    EXPECT_TRUE(window().isTaskPageShown()) << "the person stays on the page";
    EXPECT_GE(chooser->findText(QStringLiteral("page-starless-later")), 0) << "it lists an image opened since";
    chooser->setCurrentIndex(chooser->findText(QStringLiteral("page-starless-later")));
    settle();
    EXPECT_EQ(chooser->currentText(), QStringLiteral("page-starless-later"));
}

// Zoom Out on a page zooms the canvas and leaves the image's view as it was: the zoom is the canvas's.
TEST_F(TaskSubject, ZoomInZoomsTheCanvas)
{
    ImageViewControl* const view = openView(QStringLiteral("subject-zoom"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(idle(process));
    ImageViewControl* const canvas = process->previewView();
    ASSERT_NE(canvas, nullptr);
    EXPECT_EQ(core().viewService().lookingView(), canvas);
    const double canvasBefore = canvas->getViewWidget()->getCurrentZoom();
    const double targetBefore = view->getViewWidget()->getCurrentZoom();

    // The small image fills the canvas at the largest zoom there is: the canvas zooms out.
    core().actions().action(actionIds::kZoomOut)->trigger();
    settle();
    EXPECT_LT(canvas->getViewWidget()->getCurrentZoom(), canvasBefore);
    EXPECT_DOUBLE_EQ(view->getViewWidget()->getCurrentZoom(), targetBefore);
}

// Undo on a page undoes the image's step.
TEST_F(TaskSubject, UndoUndoesTheTarget)
{
    ImageViewControl* const view = openView(QStringLiteral("subject-undo"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    bend(*process);
    ASSERT_TRUE(idle(process));
    const std::size_t before = currentStep(*view);
    process->applyProcess();
    ASSERT_TRUE(idle(process));
    settle();
    ASSERT_EQ(currentStep(*view), before + 1u);

    EXPECT_EQ(core().viewService().subjectView(), view);
    core().actions().action(actionIds::kUndo)->trigger();
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return currentStep(*view) == before; }));
}

// Save as on a page saves the image the task works on.
TEST_F(TaskSubject, SaveAsSavesTheTarget)
{
    ImageViewControl* const view = openView(QStringLiteral("subject-save"), 0.4f);
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kCurves);
    ASSERT_NE(process, nullptr);
    const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("observatory-task-saved.fits"));
    QFile::remove(path);
    an::tests::app::Answered dialog;
    an::tests::app::chooseInTheNextFileDialog({path}, &dialog);
    core().actions().action(actionIds::kSaveAs)->trigger();
    EXPECT_TRUE(dialog.seen);
    EXPECT_TRUE(an::tests::app::processEventsUntil([&]() { return QFileInfo::exists(path); }, 20000));
    EXPECT_TRUE(dialog.title.contains(QStringLiteral("subject-save")))
        << "the target is saved: " << dialog.title.toStdString();
    QFile::remove(path);
}

// On a page Close, Close All Others and Maximize View are withdrawn, and offered again when the page is left.
TEST_F(TaskSubject, CloseAndMaximizeViewAreWithdrawn)
{
    ImageViewControl* const view = openView(QStringLiteral("subject-close"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kCurves);
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(idle(process));
    settle();
    EXPECT_FALSE(core().actions().action(actionIds::kClose)->isEnabled());
    EXPECT_FALSE(core().actions().action(actionIds::kCloseAllOthers)->isEnabled());
    EXPECT_FALSE(core().actions().action(actionIds::kMaximizeView)->isEnabled());
    EXPECT_TRUE(core().actions().action(actionIds::kSaveAs)->isEnabled());

    window().showViews();
    settle();
    EXPECT_TRUE(core().actions().action(actionIds::kClose)->isEnabled());
    EXPECT_TRUE(core().actions().action(actionIds::kMaximizeView)->isEnabled());
}

// Apply with the focus on the rail applies the current task.
TEST_F(TaskSubject, ApplyTaskWithTheFocusOnTheRail)
{
    ImageViewControl* const view = openView(QStringLiteral("subject-apply"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    bend(*process);
    ASSERT_TRUE(idle(process));
    const std::size_t before = currentStep(*view);
    // A window shortcut is looked for in the active window. The suites' platform does not always hand the activation
    // back after a modal dialog of an earlier case: then the action is triggered as its shortcut would.
    const bool active = an::tests::app::activateWindow(window());
    window().rail()->setFocus(Qt::OtherFocusReason);
    settle();
    EXPECT_FALSE(process->isAncestorOf(QApplication::focusWidget())) << "the focus is outside the page";

    QAction* const apply = window().shellActions().action(shellIds::kApplyTask);
    ASSERT_NE(apply, nullptr);
    EXPECT_TRUE(apply->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_Return)));
    EXPECT_TRUE(apply->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_Enter)));
    if (active)
    {
        QTest::keyClick(window().rail(), Qt::Key_Return, Qt::ControlModifier);
    }
    else
    {
        apply->trigger();
    }
    ASSERT_TRUE(idle(process));
    settle();
    EXPECT_EQ(currentStep(*view), before + 1u);
}

// Leaving the page gives the actions back to the active view.
TEST_F(TaskSubject, LeavingThePageGivesTheActionsBack)
{
    ImageViewControl* const view = openView(QStringLiteral("subject-leave"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(idle(process));
    EXPECT_TRUE(core().viewService().hasTaskSubject());

    window().showViews();
    settle();
    EXPECT_FALSE(core().viewService().hasTaskSubject());
    EXPECT_EQ(core().viewService().lookingView(), core().viewService().getActiveImageViewControl());
    EXPECT_EQ(core().viewService().subjectView(), core().viewService().getActiveImageViewControl());
}

// A process with no preview opens as a mirror page: its canvas is a view of its target, which the looking actions act
// on and the floating toolbar serves, with no compact readout. Its Apply blocks the target - the target's view is
// locked and says a task writes it - and the page's view shows the result once the run has landed.
TEST_F(MirrorPage, ApplyBlocksTheTargetAndShowsTheResult)
{
    ImageViewControl* const view = openColourView(QStringLiteral("mirror-apply"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageProcessControl*>(launch(kScnr));
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->isPresentedAsPage());
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    ImageViewControl* const mirror = process->interactionView();
    ASSERT_NE(mirror, nullptr);
    ASSERT_NE(mirror, view);
    EXPECT_EQ(process->pageLayout()->previewView(), mirror);
    EXPECT_TRUE(page->isAncestorOf(mirror));
    EXPECT_EQ(core().viewService().subjectView(), view) << "the image actions act on the target";
    EXPECT_EQ(core().viewService().lookingView(), mirror) << "the looking actions act on the page's view";
    EXPECT_EQ(page->binder().toolbarView(), mirror) << "the floating toolbar serves the page's view";
    EXPECT_FALSE(page->compactReadout()->isVisibleTo(page)) << "a mirror page reads no preview";
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return mirror->hasImage() && mirror->isWorkIdle(); }));
    const std::shared_ptr<const an::consumer::ImageAsset> before = view->document().asset();
    EXPECT_EQ(mirror->document().asset(), before) << "the page shows the target";
    const std::size_t steps = currentStep(*view);

    ASSERT_TRUE(process->triggerPrimaryAction());
    EXPECT_TRUE(process->runState().running());
    const an::ui::ViewProcessingNotice* const notice = noticeOf(*view);
    ASSERT_NE(notice, nullptr) << "the run blocks its target: the target's view says a task writes it";
    EXPECT_EQ(notice->notice().actionLabel, QStringLiteral("Show"));

    ASSERT_TRUE(
        an::tests::app::processEventsUntil([&]() { return !process->runState().running() && view->isWorkIdle(); }));
    settle();
    EXPECT_EQ(currentStep(*view), steps + 1u) << "one step in the target's history";
    EXPECT_NE(view->document().asset(), before);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return mirror->isWorkIdle(); }));
    EXPECT_EQ(mirror->document().asset(), view->document().asset()) << "the page shows the result";
    EXPECT_EQ(noticeOf(*view), nullptr) << "the notice went with the lock";
    EXPECT_TRUE(process->isPresentedAsPage()) << "SCNR stays open on its page";
}

namespace
{

/// A process launched on no image, which chooses the three images it reads in its controls.
constexpr std::string_view kStarmaskRecombination = "multi_image_stacking.actionStarmask_Recombination";

/// The cases of a page with no single target.
class TargetlessPage : public TaskPageCase
{
protected:
    /// Chooses an image in a selector of a process by its object name.
    static void choose(QWidget& process, const char* selector, const QString& image)
    {
        auto* combo = process.findChild<QComboBox*>(QString::fromLatin1(selector));
        ASSERT_NE(combo, nullptr) << selector;
        ASSERT_GE(combo->findText(image), 0) << image.toStdString() << " is offered";
        combo->setCurrentText(image);
        settle();
    }
};

/// A process window of a menu bar of its own, launched as a full page: what an integration workflow's window is.
class MenuBarProcess final : public TopLevelProcessBaseControl
{
public:
    MenuBarProcess() : TopLevelProcessBaseControl("Menu Bar Process")
    {
        auto* layout = new QVBoxLayout(this);
        auto* bar = new QMenuBar(this);
        bar->setNativeMenuBar(false);
        QMenu* frames = bar->addMenu(QStringLiteral("Frames"));
        addFrames = frames->addAction(QStringLiteral("Add Frames"));
        QMenu* run = bar->addMenu(QStringLiteral("Run"));
        execute = run->addAction(QStringLiteral("Execute"));
        layout->addWidget(bar);
        auto* body = new QWidget(this);
        body->setMinimumSize(400, 300);
        layout->addWidget(body);
        QObject::connect(addFrames, &QAction::triggered, this, [this]() { ++framesAdded; });
        QObject::connect(execute, &QAction::triggered, this, [this]() { ++executed; });
    }

    QAction* addFrames = nullptr;
    QAction* execute = nullptr;
    int framesAdded = 0;
    int executed = 0;
};

class FullPage : public TaskPageCase
{
};

} // namespace

// A page with no single target names the process alone - in its header and in the rail, whose item is the process's
// glyph on a tile - whatever image the process reads.
TEST_F(TargetlessPage, TheHeaderNamesTheProcessAlone)
{
    ImageViewControl* const starless = openView(QStringLiteral("targetless-starless"), 0.2f);
    ImageViewControl* const stars = openView(QStringLiteral("targetless-stars"), 0.4f);
    ASSERT_TRUE(starless != nullptr && stars != nullptr);
    TopLevelProcessBaseControl* const process = launch(kStarmaskRecombination);
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->isPresentedAsPage());
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    const Task* task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    EXPECT_FALSE(task->singleTarget);
    EXPECT_EQ(page->header()->processName(), QStringLiteral("Starmask Recombination"));
    EXPECT_TRUE(page->header()->targetName().isEmpty()) << "the header names the process alone";

    choose(*process, "starlessImage", QString::fromStdString(starless->getUniqueName()));
    choose(*process, "starmaskImage", QString::fromStdString(stars->getUniqueName()));
    task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    EXPECT_TRUE(task->target.isNull()) << "the image it reads first is not its target";
    EXPECT_TRUE(page->header()->targetName().isEmpty());
    const an::ui::RailItem item =
        railItem(*window().rail(), an::ui::observatory::ObservatoryWindow::railTaskId(task->id));
    EXPECT_EQ(item.text, QStringLiteral("Starmask Recombination"));
    EXPECT_TRUE(item.thumbnail.isNull()) << "the rail shows the process's glyph on a tile";
    EXPECT_EQ(item.glyphKey, task->iconKey);
}

// Until every image it needs is chosen, the canvas names the ones still to choose; once they are, it shows the preview
// of the result.
TEST_F(TargetlessPage, TheCanvasNamesTheMissingInputsUntilEveryOneIsChosen)
{
    ImageViewControl* const starless = openView(QStringLiteral("missing-starless"), 0.2f);
    ImageViewControl* const stars = openView(QStringLiteral("missing-stars"), 0.4f);
    ASSERT_TRUE(starless != nullptr && stars != nullptr);
    TopLevelProcessBaseControl* const process = launch(kStarmaskRecombination);
    ASSERT_NE(process, nullptr);
    an::ui::ProcessPageLayout* const layout = process->pageLayout();
    ASSERT_NE(layout, nullptr);

    QString said = layout->canvasMessage();
    EXPECT_TRUE(said.contains(QStringLiteral("Starless Image"))) << said.toStdString();
    EXPECT_TRUE(said.contains(QStringLiteral("Stars Image"))) << said.toStdString();
    EXPECT_FALSE(said.contains(QStringLiteral("Mask"))) << "the mask is optional";
    EXPECT_EQ(layout->previewView(), nullptr);

    choose(*process, "starlessImage", QString::fromStdString(starless->getUniqueName()));
    said = layout->canvasMessage();
    EXPECT_FALSE(said.contains(QStringLiteral("Starless Image"))) << said.toStdString();
    EXPECT_TRUE(said.contains(QStringLiteral("Stars Image"))) << said.toStdString();

    choose(*process, "starmaskImage", QString::fromStdString(stars->getUniqueName()));
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return layout->previewView() != nullptr; }))
        << "the canvas shows the preview";
    ASSERT_TRUE(idle(process));
    EXPECT_TRUE(layout->canvasMessage().isEmpty()) << layout->canvasMessage().toStdString();
    EXPECT_TRUE(layout->previewView()->isVisibleTo(layout->canvas()));
}

// While a page with no single target is shown the image actions have no image to act on, and they are given back to
// the active image when the page is left.
TEST_F(TargetlessPage, TheImageActionsAreDisabledWhileThereIsNoTarget)
{
    ImageViewControl* const view = openView(QStringLiteral("targetless-actions"));
    ASSERT_NE(view, nullptr);
    QAction* const saveAs = core().actions().action(actionIds::kSaveAs);
    ASSERT_NE(saveAs, nullptr);
    settle();
    ASSERT_TRUE(saveAs->isEnabled()) << "an image is open";

    TopLevelProcessBaseControl* const process = launch(kStarmaskRecombination);
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(window().isTaskPageShown());
    EXPECT_EQ(core().viewService().subjectView(), nullptr);
    EXPECT_FALSE(saveAs->isEnabled()) << "Save as has no image to save";
    choose(*process, "starlessImage", QString::fromStdString(view->getUniqueName()));
    EXPECT_FALSE(saveAs->isEnabled()) << "the image it reads is not its target";

    window().showViews();
    settle();
    EXPECT_TRUE(saveAs->isEnabled()) << "the actions are the active image's again";
}

// A full page is the process whole under the task header, whose menu button lists the process's own menus - its menu
// bar hidden on the page - and each entry acts as it does in the menu bar.
TEST_F(FullPage, TheMenuButtonListsTheProcessMenusAndEachEntryActs)
{
    an::ui::process_registry::ProcessDescriptor descriptor;
    descriptor.id = "tests.full_page";
    descriptor.display_name = "Menu Bar Process";
    descriptor.process_name = "Menu Bar Process";
    descriptor.category = "multi_image_stacking";
    descriptor.action_object_name = "actionNoSuchFullPage";
    descriptor.launch_mode = an::ui::process_registry::ProcessLaunchMode::TopLevel;
    descriptor.surface = an::ui::ProcessSurface::FullPage;
    MenuBarProcess* process = nullptr;
    {
        const an::ui::ProcessLaunchScope launch(descriptor);
        process = new MenuBarProcess();
    }
    process->show();
    settle();
    ASSERT_TRUE(process->isPresentedAsPage());
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(process->pageLayout()->canvas(), nullptr) << "a full page has no canvas";
    EXPECT_EQ(process->pageLayout()->column(), nullptr) << "and no column";
    EXPECT_FALSE(process->findChild<QMenuBar*>()->isVisibleTo(process)) << "its menu bar is under the button";
    ASSERT_TRUE(page->header()->menuButton()->isVisibleTo(page));

    std::unique_ptr<QMenu> menu(page->makeProcessMenu(nullptr));
    QStringList titles;
    for (const QAction* entry : menu->actions())
    {
        titles.append(entry->text());
    }
    EXPECT_EQ(titles, (QStringList{QStringLiteral("Frames"), QStringLiteral("Run")}));
    ASSERT_EQ(menu->actions().size(), 2);
    ASSERT_NE(menu->actions()[0]->menu(), nullptr);
    ASSERT_NE(menu->actions()[1]->menu(), nullptr);
    menu->actions()[0]->menu()->actions().front()->trigger();
    menu->actions()[1]->menu()->actions().front()->trigger();
    EXPECT_EQ(process->framesAdded, 1);
    EXPECT_EQ(process->executed, 1);

    process->close();
    settle();
}

} // namespace an::tests::observatory
