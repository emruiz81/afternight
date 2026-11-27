// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A task's page in the Observatory window, as a person meets it with Curves Transformation: the page opened in the
/// centre with its rail item selected and the breadcrumb naming it; the same page found again after leaving it; a run's
/// bar, row and Cancel; a run left running while another place is shown; the snackbar of a run that succeeded and its
/// Undo, in a floating task's own window too; Esc and Ctrl+Enter; the image's floor beside each column; the compact
/// readout's figures - of every task's preview area over a master that states its validity by a mask, and of a
/// binarized preview whose every sample is clipped - and its chevron's full readout over the column; the toolbar's
/// small form over a narrow canvas; a new image a run makes landing in a workspace while the page stays. And what the
/// window's actions act on while a page is shown: the looking actions on its canvas, the image actions on its target,
/// Close and Maximize View withdrawn, Apply from the rail. And the controls of other families a page holds: a
/// structuring element's grid edited in the column, a chooser of a second image following the images open, a histogram
/// and its toolbar acting in the column; and the image's floor beside the wide column of every combination.

#include "ObservatoryWindowCase.h"

#include "ShellActions.h"
#include "chrome/TopBar.h"
#include "fixtures/ObservatoryLook.h"
#include "pages/TaskPage.h"
#include "tasks/TaskPresenter.h"
#include "tasks/TaskWindow.h"
#include "workspaces/WorkspaceHost.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "actions/MenuTree.h"
#include "app/ProcessRegistry.h"
#include "components/AnBreadcrumb.h"
#include "components/AnCompactReadout.h"
#include "components/AnFloatingToolbar.h"
#include "components/AnIconButton.h"
#include "components/AnIconRail.h"
#include "components/AnImageFactsCard.h"
#include "components/AnReadoutColumn.h"
#include "components/AnReadoutGrid.h"
#include "components/AnSnackbar.h"
#include "components/AnStatisticsCard.h"
#include "components/AnTaskHeader.h"
#include "components/RailItem.h"
#include "components/ReadoutFigures.h"
#include "controls/ImageProcessControl.h"
#include "controls/ImageRTPreviewControl.h"
#include "controls/ProcessExecution.h"
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
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
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
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
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
constexpr std::string_view kBrightnessContrast = "tone_brightness.actionAdjustBrightness_Contrast";

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

    /// Opens a colour image of `width` x `height` as the active view: a ramp from 0.05 at its top left to 0.95 at its
    /// bottom right, each channel a little apart and every other pixel a little brighter, so a filter changes it and a
    /// threshold at one half leaves samples at both ends. A `bordered` image states its samples' validity by a mask of
    /// its own, which leaves out a border four pixels wide, as an integrated master leaves out the edge its frames did
    /// not cover.
    static ImageViewControl* openRampView(const QString& name, const std::uint32_t width, const std::uint32_t height,
                                          const bool bordered = false)
    {
        an::Result<an::consumer::SessionHold> hold = session().hold();
        EXPECT_TRUE(hold.hasValue());
        if (!hold.hasValue())
        {
            return nullptr;
        }
        const std::optional<an::ImageExtent> extent =
            an::ImageExtent::create(width, height, 3u, hold->engine().limits());
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
        const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(an::ImageSemanticsRequest{
            .validity = *an::ValidityDescriptor::create(
                {.rule = bordered ? an::ValidityRule::ExplicitMask : an::ValidityRule::AllValid})});
        EXPECT_TRUE(semantics.hasValue());
        if (!semantics.hasValue())
        {
            return nullptr;
        }
        an::Result<an::Image<float>> image = an::Image<float>::createFilled(
            hold->engine(), *descriptor, an::StorageArrangement::Planar, 0.0f,
            an::ImageMetadata::create(an::AcquisitionMetadata{}, *semantics, an::AstrometricSolution{},
                                      an::Provenance{}, an::CodecMetadata{}, std::nullopt));
        EXPECT_TRUE(image.hasValue());
        if (!image.hasValue())
        {
            return nullptr;
        }
        std::optional<an::ValidityMask> border;
        if (bordered)
        {
            an::Result<an::ValidityMask> mask = an::ValidityMask::createAllValid(hold->engine(), *extent);
            EXPECT_TRUE(mask.hasValue());
            if (!mask.hasValue())
            {
                return nullptr;
            }
            if (const std::optional<an::ValidityMaskView<std::uint8_t>> bits = mask->view(); bits.has_value())
            {
                for (std::uint32_t plane = 0u; plane < bits->planeCount(); ++plane)
                {
                    for (std::uint32_t y = 0u; y < height; ++y)
                    {
                        for (std::uint32_t x = 0u; x < width; ++x)
                        {
                            if (x < 4u || y < 4u || x + 4u >= width || y + 4u >= height)
                            {
                                (void)bits->setValid(plane, x, y, false);
                            }
                        }
                    }
                }
            }
            border.emplace(std::move(*mask));
        }
        {
            std::optional<an::ImageWriteAccess<float>> access = image->write();
            EXPECT_TRUE(access.has_value());
            if (!access.has_value())
            {
                return nullptr;
            }
            const float span = static_cast<float>(width + height - 2u);
            for (std::uint32_t c = 0u; c < 3u; ++c)
            {
                for (std::uint32_t y = 0u; y < height; ++y)
                {
                    auto row = access->view().row(c, y);
                    if (!row.has_value())
                    {
                        return nullptr;
                    }
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        const float ramp = 0.05f + 0.9f * static_cast<float>(x + y) / span;
                        const float grain = ((x + y) % 2u == 0u) ? 0.02f : -0.02f;
                        (*row)[x] = std::clamp(ramp + grain + 0.01f * static_cast<float>(c), 0.01f, 0.99f);
                    }
                }
            }
        }
        an::Result<an::consumer::ImageAsset> asset =
            border.has_value() ? an::consumer::ImageAsset::adopt(session(), an::consumer::AnyImage(std::move(*image)),
                                                                 std::move(*border))
                               : an::consumer::ImageAsset::adopt(session(), an::consumer::AnyImage(std::move(*image)));
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

    /// Lets a task's next settled frame land and the readout measure it: true once the readout has shown figures it
    /// measured after `published` measurements were shown, and is neither measuring nor dimmed.
    [[nodiscard]] static bool readoutMeasuredAfter(an::ui::shell::ReadoutController& readout,
                                                   const std::uint64_t published)
    {
        return an::tests::app::processEventsUntil(
            [&]() {
                return readout.isIdle() && readout.telemetry().published > published &&
                       readout.shownFigures().has_value() && !readout.isStale();
            },
            60000);
    }

    /// Opens every task the registry offers as a page, one at a time, as its menu entry opens it - a process of one
    /// image over a colour image, a process that chooses its images over none - hands it to `check` with its page, and
    /// closes it, gone with its preview's work landed, before the next opens.
    /// @return how many tasks were opened.
    static int openEveryTask(
        const std::function<void(const an::ui::process_registry::ProcessDescriptor& descriptor,
                                 TopLevelProcessBaseControl& process, an::ui::observatory::TaskPage& page)>& check)
    {
        ImageViewControl* const view = openColourView(QStringLiteral("every-task"));
        EXPECT_NE(view, nullptr);
        if (view == nullptr)
        {
            return 0;
        }
        int opened = 0;
        // The registry is copied: a launch reads it.
        const std::vector<an::ui::process_registry::ProcessDescriptor> descriptors =
            an::ui::process_registry::ProcessRegistry::instance().all();
        for (const an::ui::process_registry::ProcessDescriptor& descriptor : descriptors)
        {
            if (descriptor.is_extension_process || descriptor.surface == an::ui::ProcessSurface::Window ||
                descriptor.launch_mode == an::ui::process_registry::ProcessLaunchMode::NonInteractive)
            {
                continue;
            }
            SCOPED_TRACE(descriptor.id);
            core().viewService().setActiveImageViewControl(view);
            settle();
            TopLevelProcessBaseControl* const process = launch(descriptor.id);
            if (process == nullptr)
            {
                ADD_FAILURE() << "the task did not open";
                continue;
            }
            settle();
            an::ui::observatory::TaskPage* const page = pageOf(process);
            EXPECT_NE(page, nullptr) << "the task is a page";
            if (page != nullptr)
            {
                ++opened;
                check(descriptor, *process, *page);
            }
            const QPointer<TopLevelProcessBaseControl> guard(process);
            EXPECT_TRUE(idle(process));
            (void)process->close();
            EXPECT_TRUE(an::tests::app::processEventsUntil([&guard]() {
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                return guard.isNull();
            })) << "the task closed";
            settle();
        }
        return opened;
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

// The column's header names a process as its menu entry and the task header name it - ArcSinh Stretch, SCNR Filter, CLA
// Histogram Equalization - not by the name the process gives itself, wherever the two differ; its tooltip is the
// process's description.
TEST_F(TaskPage, TheColumnNamesTheProcessAsItsMenuEntryDoes)
{
    ImageViewControl* const view = openColourView(QStringLiteral("page-column-name"));
    ASSERT_NE(view, nullptr);
    int differing = 0;
    for (const an::ui::process_registry::ProcessDescriptor& descriptor :
         an::ui::process_registry::ProcessRegistry::instance().all())
    {
        const bool page = descriptor.surface == an::ui::ProcessSurface::PreviewPage ||
                          descriptor.surface == an::ui::ProcessSurface::MirrorPage;
        if (!page || descriptor.is_extension_process ||
            descriptor.launch_mode != an::ui::process_registry::ProcessLaunchMode::SingleImage ||
            descriptor.display_name == descriptor.process_name)
        {
            continue;
        }
        SCOPED_TRACE(descriptor.id);
        ++differing;
        const QString menuName = QString::fromStdString(descriptor.display_name);
        TopLevelProcessBaseControl* const process = launch(descriptor.id);
        ASSERT_NE(process, nullptr);
        settle();
        an::ui::observatory::TaskPage* const shown = pageOf(process);
        ASSERT_NE(shown, nullptr);
        ASSERT_NE(process->pageLayout(), nullptr);
        const QLabel* const title = process->findChild<QLabel*>(QStringLiteral("pageTitle"));
        ASSERT_NE(title, nullptr);
        EXPECT_EQ(title->text(), menuName) << "the column's header names the process as its menu entry does";
        EXPECT_EQ(shown->header()->processName(), menuName) << "as the task header does";
        EXPECT_EQ(title->toolTip(), process->property("anProcessDescription").toString())
            << "its tooltip is the process's description";
        // Closed, and gone - its preview's work landed - before the next opens on the same image.
        const QPointer<TopLevelProcessBaseControl> guard(process);
        ASSERT_TRUE(idle(process));
        (void)process->close();
        ASSERT_TRUE(an::tests::app::processEventsUntil([&guard]() {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            return guard.isNull();
        })) << "the task closed";
        settle();
    }
    EXPECT_GE(differing, 7) << "processes whose menu entry names them otherwise than they name themselves";
}

// Every task's header shows the task's icon where it shows its target's thumbnail: the thumbnail once it is made, and
// before that, or for a task with no target image - a full page, a combination that chooses its images - the
// process's glyph on a tile, the rail's glyph for it. Never an empty square. A header that says a job has no thumbnail.
TEST_F(TaskPage, EveryTaskHeaderShowsTheTasksIcon)
{
    const an::ui::DesignTokens& t = an::ui::tokens();
    int glyphs = 0;
    const int opened = openEveryTask([&](const an::ui::process_registry::ProcessDescriptor& /*descriptor*/,
                                         TopLevelProcessBaseControl& process, an::ui::observatory::TaskPage& page) {
        const Task* const task = tasks().model().taskFor(&process);
        ASSERT_NE(task, nullptr);
        an::ui::AnTaskHeader* const header = page.header();
        if (header->job().has_value())
        {
            return;
        }
        EXPECT_EQ(header->glyphKey().toStdString(), task->iconKey.toStdString())
            << "the header's glyph is the task's, as the rail shows it";
        EXPECT_TRUE(QFile::exists(QStringLiteral(":/AfterNight/icons/SVG/%1.svg").arg(header->glyphKey())))
            << "the glyph " << header->glyphKey().toStdString() << " is one of the product's icons";
        if (!header->thumbnail().isNull())
        {
            return;
        }
        ++glyphs;
        const QPixmap grabbed = header->grab();
        const QImage picture = grabbed.toImage();
        const qreal ratio = grabbed.devicePixelRatio();
        const auto at = [&picture, ratio](const qreal x, const qreal y) {
            return picture.pixelColor(std::clamp(static_cast<int>(std::floor(x * ratio)), 0, picture.width() - 1),
                                      std::clamp(static_cast<int>(std::floor(y * ratio)), 0, picture.height() - 1));
        };
        const qreal side = an::ui::parts::kTaskHeaderThumbnail;
        const QRectF slot(an::ui::parts::kTaskHeaderPaddingStart, (header->height() - side) / 2.0, side, side);
        EXPECT_TRUE(an::ui::testing::sameColour(at(slot.left() + 1.0, slot.center().y()), t.layers.surface3))
            << "the tile, not the well's empty square";
        const qreal glyph = an::ui::parts::kTaskHeaderTileGlyph;
        int drawn = 0;
        for (qreal y = slot.center().y() - glyph / 2.0; y < slot.center().y() + glyph / 2.0; y += 1.0)
        {
            for (qreal x = slot.center().x() - glyph / 2.0; x < slot.center().x() + glyph / 2.0; x += 1.0)
            {
                drawn += an::ui::testing::sameColour(at(x, y), t.colors.textPrimary, 24) ? 1 : 0;
            }
        }
        EXPECT_GT(drawn, 4) << "the process's glyph is drawn on the tile";
        EXPECT_LT(drawn, static_cast<int>(glyph * glyph) / 2) << "the glyph's shape, not a square of its colour";
    });
    EXPECT_GE(opened, 70) << "every task the registry offers as a page";
    EXPECT_GE(glyphs, 6) << "the full pages and the combinations that choose their images show the glyph";
}

// The eyebrow over a task's name in its column says the menu a person reaches the task from - Color Spaces for the
// combinations of channels, Utilities for Image Compare, Intensity Transformations for Binarize, Image > Sequence for
// Extract Sequence - whatever category the registry files the process under.
TEST_F(TaskPage, EveryColumnsEyebrowIsAMenuThatReachesTheTask)
{
    int columns = 0;
    const int opened =
        openEveryTask([&columns](const an::ui::process_registry::ProcessDescriptor& descriptor,
                                 TopLevelProcessBaseControl& process, an::ui::observatory::TaskPage& /*page*/) {
            const an::ui::ProcessPageLayout* const layout = process.pageLayout();
            ASSERT_NE(layout, nullptr);
            if (layout->canvas() == nullptr)
            {
                return;
            }
            ++columns;
            const std::vector<std::string> paths =
                an::ui::shell::MenuTree::menuBar().pathsOf(descriptor.action_object_name);
            ASSERT_FALSE(paths.empty()) << "a menu reaches the task";
            const std::string eyebrow = layout->category().toStdString();
            const bool reaches = std::any_of(paths.begin(), paths.end(), [&eyebrow](const std::string& path) {
                return path == "Process > " + eyebrow || path == eyebrow;
            });
            EXPECT_TRUE(reaches) << "the eyebrow says \"" << eyebrow << "\"; the task is reached from \""
                                 << paths.front() << "\"";
        });
    EXPECT_GE(opened, 70) << "every task the registry offers as a page";
    EXPECT_GE(columns, 60) << "every task with a column";
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

// A floating task's run that succeeds is said where the person sees it: in its window, at the foot of its canvas, with
// how long it took and Undo, which undoes the image's step - as the main window says a page's.
TEST_F(TaskPage, AFloatingTasksSuccessIsSaidInItsWindow)
{
    ImageViewControl* const view = openView(QStringLiteral("page-floating-success"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    const Task* const task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    const TaskId id = task->id;
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    ASSERT_TRUE(window().floatTask(id));
    settle();
    ASSERT_NE(window().taskWindow(id), nullptr);
    bend(*process);
    ASSERT_TRUE(idle(process));
    window().snackbar()->dismiss();
    const std::size_t before = currentStep(*view);

    process->applyProcess();
    ASSERT_TRUE(idle(process));
    settle();
    ASSERT_EQ(currentStep(*view), before + 1u);
    an::ui::AnSnackbar* const said = page->snackbar();
    ASSERT_NE(said, nullptr) << "the task's window says nothing";
    EXPECT_TRUE(said->isVisible());
    EXPECT_TRUE(page->isAncestorOf(said)) << "it is said in the task's window";
    EXPECT_EQ(said->text(), QStringLiteral("Curves Transformation applied"));
    EXPECT_TRUE(said->detail().endsWith(QStringLiteral(" s")));
    EXPECT_EQ(said->actionText(), QStringLiteral("Undo"));
    const QRect canvas = page->canvasArea();
    const QRect placed(said->mapTo(page, QPoint(0, 0)), said->size());
    EXPECT_TRUE(page->rect().contains(placed)) << "in the task's window";
    EXPECT_GE(placed.top(), canvas.top());
    EXPECT_LE(placed.bottom(), canvas.bottom()) << "at the foot of the canvas";
    EXPECT_FALSE(window().snackbar()->isVisible()) << "the main window, out of sight, says nothing";

    said->actionButton()->click();
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return currentStep(*view) == before; }));
    EXPECT_FALSE(said->isVisible()) << "the action dismisses the message";
    ASSERT_TRUE(window().attachTask(id));
    settle();
}

// The compact readout's chevron opens the full readout - the Histogram, Statistics, Astrometry and Image cards of the
// task's target, as the readout column shows them - as a sheet over the process's column under it, and closes it again.
TEST_F(TaskPage, TheChevronOpensTheFullReadoutOverTheColumnAndClosesIt)
{
    ImageViewControl* const view = openView(QStringLiteral("page-readout-sheet"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(idle(process));
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    an::ui::AnCompactReadout* const compact = page->compactReadout();
    EXPECT_FALSE(page->isReadoutSheetShown());

    compact->expandButton()->click();
    settle();
    ASSERT_TRUE(page->isReadoutSheetShown());
    an::ui::AnReadoutColumn* const sheet = page->readoutSheet();
    ASSERT_NE(sheet, nullptr);
    EXPECT_TRUE(sheet->isVisible());
    EXPECT_TRUE(compact->expandButton()->isChecked());
    EXPECT_EQ(sheet->viewName(), QString::fromUtf8(view->getUniqueName())) << "the target's readout";
    QWidget* const column = process->pageLayout()->column();
    ASSERT_NE(column, nullptr);
    const QRect columnRect(column->mapTo(page, QPoint(0, 0)), column->size());
    const QRect sheetRect(sheet->mapTo(page, QPoint(0, 0)), sheet->size());
    const QRect compactRect(compact->mapTo(page, QPoint(0, 0)), compact->size());
    EXPECT_EQ(sheetRect.left(), columnRect.left()) << "over the column";
    EXPECT_EQ(sheetRect.width(), columnRect.width());
    EXPECT_GT(sheetRect.top(), compactRect.bottom()) << "under the compact readout, whose chevron closes it";
    EXPECT_EQ(sheetRect.bottom(), columnRect.bottom());
    ASSERT_TRUE(
        an::tests::app::processEventsUntil([&]() { return sheet->statisticsCard()->report().has_value(); }, 10000))
        << "the target's figures land";
    EXPECT_FALSE(sheet->imageCard()->facts().dimensions.isEmpty());

    compact->expandButton()->click();
    settle();
    EXPECT_FALSE(page->isReadoutSheetShown());
    EXPECT_FALSE(compact->expandButton()->isChecked());
}

// The toolbar over a canvas narrower than its full form takes its small form, as a floating image window's does, so
// it lies within the canvas, Fit first; a canvas with room for the full form takes the full form again.
TEST_F(TaskPage, ANarrowCanvasTakesTheToolbarsSmallForm)
{
    ImageViewControl* const view = openView(QStringLiteral("page-narrow-canvas"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(idle(process));
    const Task* const task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    const TaskId id = task->id;
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    an::ui::AnFloatingToolbar* const toolbar = page->toolbar();
    ASSERT_TRUE(toolbar->isVisible());
    EXPECT_EQ(toolbar->form(), an::ui::AnFloatingToolbar::Form::Full) << "the main window's canvas has room";
    EXPECT_TRUE(page->canvasArea().contains(toolbar->geometry()));

    ASSERT_TRUE(window().floatTask(id));
    settle();
    an::ui::observatory::TaskWindow* const floating = window().taskWindow(id);
    ASSERT_NE(floating, nullptr);
    ASSERT_NE(floating->window(), nullptr);
    floating->window()->resize(an::ui::observatory::TaskWindow::kMinimumWidth,
                               an::ui::observatory::TaskWindow::kMinimumHeight);
    settle();
    const QRect canvas = page->canvasArea();
    ASSERT_FALSE(canvas.isEmpty());
    ASSERT_LT(canvas.width(), 600) << "the window at its least size leaves a narrow canvas";
    EXPECT_EQ(toolbar->form(), an::ui::AnFloatingToolbar::Form::Small);
    EXPECT_TRUE(canvas.contains(toolbar->geometry()))
        << "the toolbar, " << toolbar->width() << " px, over a canvas " << canvas.width() << " px wide";
    auto* const fit = toolbar->findChild<QWidget*>(QStringLiteral("anFloatingToolbarFit"));
    ASSERT_NE(fit, nullptr);
    EXPECT_TRUE(fit->isVisible());
    EXPECT_TRUE(canvas.contains(QRect(fit->mapTo(page, QPoint(0, 0)), fit->size()))) << "Fit is in view";

    ASSERT_TRUE(window().attachTask(id));
    settle();
    EXPECT_EQ(toolbar->form(), an::ui::AnFloatingToolbar::Form::Full);
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

// A task's preview area is read out as its whole preview is: once a frame over the area has landed, the compact readout
// measures what the preview shows and shows its figures, never dashes - for every task whose preview offers an area,
// Median Filter, Non-Local Bayes and Patch PCA among them, most of which open with the area on. The image states the
// validity of its samples by a mask of its own, as an integrated master does: the frame over the area pasted its patch
// into a copy of the image that kept the image's rule and lost its mask, which every measurement of it refused.
TEST_F(TaskPage, ThePreviewAreaGivesTheCompactReadoutItsFigures)
{
    // Wider than the area a preview starts with, so the area is a part of the image.
    ImageViewControl* const view = openRampView(QStringLiteral("page-area"), 720u, 120u, true);
    ASSERT_NE(view, nullptr);
    an::ui::shell::ReadoutController& readout = window().readoutController();
    std::vector<std::string> offered;
    std::vector<std::string> unread;
    std::vector<std::string> shownNone;
    // The registry is copied: a launch reads it.
    const std::vector<an::ui::process_registry::ProcessDescriptor> descriptors =
        an::ui::process_registry::ProcessRegistry::instance().all();
    for (const an::ui::process_registry::ProcessDescriptor& descriptor : descriptors)
    {
        if (descriptor.is_extension_process || descriptor.surface != an::ui::ProcessSurface::PreviewPage ||
            descriptor.launch_mode == an::ui::process_registry::ProcessLaunchMode::NonInteractive)
        {
            continue;
        }
        SCOPED_TRACE(descriptor.id);
        core().viewService().setActiveImageViewControl(view);
        settle();
        const std::uint64_t published = readout.telemetry().published;
        TopLevelProcessBaseControl* const launched = launch(descriptor.id);
        auto* const process = dynamic_cast<ImageRTPreviewControl*>(launched);
        an::ui::observatory::TaskPage* const page = pageOf(launched);
        an::ui::ProcessPageLayout* const layout = launched != nullptr ? launched->pageLayout() : nullptr;
        QAbstractButton* const area = layout != nullptr ? layout->previewAreaToggle() : nullptr;
        if (process != nullptr && page != nullptr && area != nullptr && process->previewView() != nullptr &&
            area->isVisibleTo(page) && area->isEnabled())
        {
            offered.push_back(descriptor.id);
            const bool openedWithArea = area->isChecked();
            if (!openedWithArea)
            {
                area->click();
                settle();
            }
            const bool quiet = idle(process);
            const bool shown =
                an::tests::app::processEventsUntil([&]() { return process->previewFramesShown() > 0u; }, 20000);
            if (!shown)
            {
                // A preview that fails over this image - its banner says why - shows no frame to read out.
                shownNone.push_back(descriptor.id);
            }
            else if (!readoutMeasuredAfter(readout, published))
            {
                const an::ui::shell::ReadoutTelemetry seen = readout.telemetry();
                const ProcessRunTelemetry ran = process->runTelemetry();
                unread.push_back(
                    descriptor.id + " (frames shown " + std::to_string(process->previewFramesShown()) +
                    ", preview idle " + (quiet ? "yes" : "no") + "; previews started " +
                    std::to_string(ran.previewsStarted) + ", committed " + std::to_string(ran.previewsCommitted) +
                    ", discarded " + std::to_string(ran.previewsDiscarded) + ", failed " +
                    std::to_string(ran.previewsFailed) + ", identity " + std::to_string(ran.identityPreviews) +
                    ", kept " + std::to_string(ran.keptPreviewsShown) + "; measurements started " +
                    std::to_string(seen.started) + ", shown " + std::to_string(seen.published - published) +
                    " of its, dropped " + std::to_string(seen.discarded) + ", refused " + std::to_string(seen.refused) +
                    ", failed " + std::to_string(seen.failed) + ")");
            }
            // The area as the task opened with it, which it keeps for the next time it opens.
            if (area->isChecked() != openedWithArea)
            {
                area->click();
                settle();
            }
        }
        const QPointer<TopLevelProcessBaseControl> guard(launched);
        EXPECT_TRUE(idle(launched));
        if (!guard.isNull())
        {
            (void)guard->close();
        }
        EXPECT_TRUE(an::tests::app::processEventsUntil([&guard]() {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            return guard.isNull();
        })) << "the task closed";
        settle();
    }
    for (const std::string_view named :
         {std::string_view("filters.actionMedian_Filter"), std::string_view("denoising.actionNon_Local_Bayes_Filter"),
          std::string_view("denoising.actionPatch_PCA_Filter")})
    {
        EXPECT_TRUE(std::find(offered.begin(), offered.end(), named) != offered.end())
            << named << " offers no preview area";
        EXPECT_TRUE(std::find(shownNone.begin(), shownNone.end(), named) == shownNone.end())
            << named << " shows no frame of its preview";
    }
    std::string unreadList;
    for (const std::string& task : unread)
    {
        unreadList += "\n  " + task;
    }
    EXPECT_TRUE(unread.empty()) << unread.size() << " of the " << offered.size()
                                << " tasks with a preview area showed no figures of a frame over it:" << unreadList;
}

// Binarize leaves every sample at an end of the range, as its preview's clipping says - the shares clipped low and high
// make the whole image - so no sample lies between the ends for the figures, which leave the clipped samples out: each
// figure, and its change against the image, reads "-", never 0.0000 beside a maximum the clipping contradicts.
TEST_F(TaskPage, ABinarizedPreviewsFiguresAgreeWithItsClipping)
{
    ImageViewControl* const view = openRampView(QStringLiteral("page-binarize"), 64u, 48u);
    ASSERT_NE(view, nullptr);
    an::ui::shell::ReadoutController& readout = window().readoutController();
    const std::uint64_t published = readout.telemetry().published;
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch("sensor_calibration.actionBinarize"));
    ASSERT_NE(process, nullptr);
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    ASSERT_TRUE(idle(process));
    ASSERT_GT(process->previewFramesShown(), 0u) << "the preview shows the binarized image";
    ASSERT_TRUE(readoutMeasuredAfter(readout, published));

    // The clipping: every sample at an end, some at each.
    an::ui::AnCompactReadout* const compact = page->compactReadout();
    const std::vector<an::ui::ReadoutCell>& clipping = compact->clipping()->cells();
    ASSERT_EQ(clipping.size(), 2u);
    const auto share = [](const QString& text) {
        bool read = false;
        const double value = text.section(QLatin1Char(' '), 0, 0).toDouble(&read);
        return read ? value : -1.0;
    };
    const double low = share(clipping[0].value);
    const double high = share(clipping[1].value);
    EXPECT_GT(low, 0.0) << clipping[0].value.toStdString();
    EXPECT_GT(high, 0.0) << clipping[1].value.toStdString();
    EXPECT_NEAR(low + high, 100.0, 0.001) << "every sample of a binarized image is at an end of the range";

    // The figures: over no sample.
    ASSERT_TRUE(readout.shownFigures().has_value());
    for (const an::ui::ImageStatisticsChannel& channel : readout.shownFigures()->channels)
    {
        EXPECT_EQ(channel.count, 0u) << "no sample lies between the ends";
    }
    const std::vector<an::ui::ReadoutCell>& cells = compact->cells()->cells();
    ASSERT_EQ(cells.size(), an::ui::kCompactReadoutStatistics.size());
    for (const an::ui::ReadoutCell& cell : cells)
    {
        EXPECT_EQ(cell.value, QStringLiteral("-"))
            << cell.label.toStdString() << " reads " << cell.value.toStdString() << " over no sample, beside "
            << clipping[0].value.toStdString() << " clipped low and " << clipping[1].value.toStdString()
            << " clipped high";
        EXPECT_FALSE(cell.note.contains(QRegularExpression(QStringLiteral("[0-9]"))))
            << cell.label.toStdString() << "'s change is a number: " << cell.note.toStdString();
    }
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
// What a process works on is chosen on its page: the target among the open images and, for a colour image, the
// channel, at the head of the column's body; the preview on or off in the toggles' row. The channel chosen is the one
// a run is confined to, the preview turned off leaves the canvas with none and comes back when it is turned on, and
// another target is the image the process then works on and the task then names.
TEST_F(TaskPage, TheTargetTheChannelAndThePreviewAreChosenOnThePage)
{
    ImageViewControl* const grey = openView(QStringLiteral("target-grey"));
    ImageViewControl* const colour = openColourView(QStringLiteral("target-colour"));
    ASSERT_TRUE(grey != nullptr && colour != nullptr);
    TopLevelProcessBaseControl* const process = launch(kBrightnessContrast);
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->isPresentedAsPage());
    auto* const preview = dynamic_cast<ImageRTPreviewControl*>(process);
    an::ui::ProcessPageLayout* const layout = process->pageLayout();
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_TRUE(preview != nullptr && layout != nullptr && page != nullptr);
    QComboBox* const target = preview->standardParts().targetSelector;
    QComboBox* const channel = preview->standardParts().channelSelector;
    QCheckBox* const toggle = preview->standardParts().previewCheckbox;
    ASSERT_TRUE(target != nullptr && channel != nullptr && toggle != nullptr);

    // Where they are: the target and the channel in the column's body, the preview in the toggles' row, all shown.
    EXPECT_TRUE(layout->bodyContent()->isAncestorOf(target));
    EXPECT_TRUE(layout->bodyContent()->isAncestorOf(channel));
    ASSERT_NE(layout->toggles(), nullptr);
    EXPECT_TRUE(layout->toggles()->isAncestorOf(toggle));
    EXPECT_TRUE(target->isVisibleTo(page) && channel->isVisibleTo(page) && toggle->isVisibleTo(page));

    // The image the process was opened over is its target, and the task says so.
    EXPECT_EQ(target->currentText(), QStringLiteral("target-colour"));
    EXPECT_EQ(process->processTarget(), colour);
    EXPECT_EQ(page->header()->targetName(), QStringLiteral("target-colour"));

    // A colour image offers its channels; the one chosen is the one a run is confined to.
    EXPECT_GT(channel->count(), 1) << "a colour target offers one choice";
    const int red = channel->findText(QStringLiteral("Red"));
    ASSERT_GE(red, 0) << "the channel's choices name no Red";
    channel->setCurrentIndex(red);
    settle();
    EXPECT_EQ(preview->selectedChannelScope(), an::ChannelScope::Red);

    // The preview: shown in the canvas while it is on, gone when it is turned off, back when it is turned on.
    if (!toggle->isChecked())
    {
        toggle->click();
        settle();
    }
    ASSERT_TRUE(idle(process));
    EXPECT_NE(preview->previewView(), nullptr) << "the preview is on and the canvas shows none";
    toggle->click();
    settle();
    EXPECT_FALSE(toggle->isChecked());
    EXPECT_EQ(preview->previewView(), nullptr) << "the preview stays in the canvas after it is turned off";
    toggle->click();
    settle();
    ASSERT_TRUE(idle(process));
    EXPECT_NE(preview->previewView(), nullptr) << "the preview does not come back when it is turned on";

    // Another target: the process works on it, the task names it, and a grey image offers its one channel.
    ASSERT_GE(target->findText(QStringLiteral("target-grey")), 0) << "the target's choices leave an open image out";
    target->setCurrentText(QStringLiteral("target-grey"));
    settle();
    ASSERT_TRUE(idle(process));
    EXPECT_EQ(process->processTarget(), grey);
    EXPECT_EQ(page->header()->targetName(), QStringLiteral("target-grey"));
    const Task* const task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->target.data(), grey);
    EXPECT_EQ(channel->count(), 1);
    EXPECT_EQ(channel->currentText(), QString::fromLatin1(an::ui::kProcessGreyscaleChannelLabel));
    ASSERT_TRUE(idle(process));
}

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
