// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A family's process held to its page: in its column at 600 px of height, in both densities of the Observatory look,
/// every control is in the column and reached by Tab - none left beside the preview in the canvas - Target image and
/// Channel lead the body, no group is left empty, no button box is left among the controls (the window's buttons are
/// the footer's) and, where the platform measures text as the interface does, nothing in the body lies past the
/// column's side; and the same parameters applied through the page give the window's result, whether it lands in the
/// target or in a new image.
///
/// Whether nothing is clipped depends on how wide text is, and the suites' own platform draws text in a box font about
/// twice the interface's width: there the case asserts what holds whatever the font and is reported skipped for the
/// rest. Started under the native platform (`QT_QPA_PLATFORM=windows`, the pages kept off the screen) it measures text
/// as the interface does, as a stage's gate measures the pages.

#include "Test_ProcessesShared/ObservatoryWindowCheck.h"

#include "controls/ImageProcessControl.h"
#include "controls/ImageRTPreviewControl.h"
#include "controls/ProcessPageLayout.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "fixtures/common/FixtureImage.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QGroupBox>
#include <QLineEdit>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSpinBox>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace an::tests::processes::shared
{

/// The height a page is held to: a short window's.
inline constexpr int kPageCheckHeight = 600;

/// Whether a widget is a control a person reaches by Tab: a button, a field, a choice, a slider or a spin box.
[[nodiscard]] inline bool isTabControl(const QWidget& widget)
{
    return qobject_cast<const QAbstractButton*>(&widget) != nullptr ||
           qobject_cast<const QAbstractSpinBox*>(&widget) != nullptr ||
           qobject_cast<const QComboBox*>(&widget) != nullptr ||
           qobject_cast<const QAbstractSlider*>(&widget) != nullptr ||
           qobject_cast<const QLineEdit*>(&widget) != nullptr;
}

/// Whether a checkable button is reached by Tab as a member of its exclusive group. Qt makes such a group one Tab stop:
/// once a member takes the focus the others lose their Tab focus, and the arrow keys move between them from the one
/// Tab reaches.
[[nodiscard]] inline bool reachedThroughItsGroup(const QWidget& widget, const QSet<const QWidget*>& chain)
{
    const auto* const button = qobject_cast<const QAbstractButton*>(&widget);
    if (button == nullptr || !button->isCheckable())
    {
        return false;
    }
    QList<QAbstractButton*> members;
    if (const QButtonGroup* const group = button->group(); group != nullptr && group->exclusive())
    {
        members = group->buttons();
    }
    else if (button->autoExclusive() && button->parentWidget() != nullptr)
    {
        members = button->parentWidget()->findChildren<QAbstractButton*>(Qt::FindDirectChildrenOnly);
    }
    for (const QAbstractButton* member : members)
    {
        if (member != button && chain.contains(member))
        {
            return true;
        }
    }
    return false;
}

/// Whether a widget is a part of a view - its scroll bars, its own toolbar - which the canvas holds with the preview.
[[nodiscard]] inline bool partOfAView(const QWidget& widget, const QWidget& canvas)
{
    for (const QWidget* at = &widget; at != nullptr && at != &canvas; at = at->parentWidget())
    {
        if (dynamic_cast<const ImageViewControl*>(at) != nullptr)
        {
            return true;
        }
    }
    return false;
}

/// The process a launch took into a page; null when none was.
[[nodiscard]] inline TopLevelProcessBaseControl* launchedPage(const std::string& descriptorId, ImageViewControl& target)
{
    QWidget* const opened = launchWindowOf(descriptorId, target);
    auto* const process = dynamic_cast<TopLevelProcessBaseControl*>(opened);
    return process != nullptr && process->pageLayout() != nullptr ? process : nullptr;
}

/// Holds a descriptor's page to its column at 600 px of height in both densities: a preview page, or a mirror page when
/// `surface` says so. Answers whether text was measured, so the case knows whether everything it holds was asserted.
inline bool expectPageFitsItsColumn(const std::string& descriptorId, ImageViewControl& target,
                                    ProcessTestContext& context,
                                    an::ui::ProcessSurface surface = an::ui::ProcessSurface::PreviewPage)
{
    using an::ui::testing::ObservatoryLook;
    using an::ui::testing::observatoryLooks;
    using an::ui::testing::ObservatoryStyleScope;
    const an::ui::process_registry::ProcessDescriptor* const descriptor = registered(descriptorId);
    EXPECT_NE(descriptor, nullptr) << descriptorId;
    if (descriptor == nullptr)
    {
        return false;
    }
    EXPECT_EQ(descriptor->surface, surface) << descriptorId << " is a page of its kind";
    const bool measured = textIsMeasured();
    for (const ObservatoryLook* look : {&observatoryLooks()[0], &observatoryLooks()[3]})
    {
        SCOPED_TRACE(look->name);
        const ObservatoryStyleScope scope(*look);
        PagePresenter presenter(QSize(PagePresenter::kCentre.width(), kPageCheckHeight));
        context.presenter = &presenter;
        TopLevelProcessBaseControl* const process = launchedPage(descriptorId, target);
        EXPECT_NE(process, nullptr) << descriptorId << " opened as a page";
        if (process == nullptr)
        {
            context.presenter = nullptr;
            continue;
        }
        pumpQuietly();
        an::ui::ProcessPageLayout* const page = process->pageLayout();
        EXPECT_EQ(page->columnWidth(),
                  an::ui::pageColumnWidth(descriptor->columnWidth, PagePresenter::kCentre.width()));
        EXPECT_EQ(page->body()->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff) << "no horizontal scroll bar";

        // Every control of the column is reached by Tab: it is in the window's focus chain and takes the focus by Tab.
        QSet<const QWidget*> chain;
        QWidget* const first = page->column();
        for (QWidget* at = first->nextInFocusChain(); at != nullptr && at != first; at = at->nextInFocusChain())
        {
            if ((at->focusPolicy() & Qt::TabFocus) != 0 && at->isEnabled() && at->isVisibleTo(process))
            {
                chain.insert(at);
            }
        }
        QStringList unreached;
        for (QWidget* widget : page->column()->findChildren<QWidget*>())
        {
            if (!widget->isVisibleTo(process) || !widget->isEnabled() || !isTabControl(*widget) ||
                widget->focusPolicy() == Qt::NoFocus)
            {
                continue;
            }
            if (!chain.contains(widget) && !chain.contains(widget->focusProxy()) &&
                !reachedThroughItsGroup(*widget, chain))
            {
                unreached.append(widgetPath(*widget, *process));
            }
        }
        EXPECT_TRUE(unreached.isEmpty()) << "not reached by Tab: "
                                         << unreached.join(QStringLiteral(", ")).toStdString();

        // Every control is in the column: none is left beside the preview in the canvas.
        QStringList inCanvas;
        for (QWidget* widget : page->canvas()->findChildren<QWidget*>())
        {
            if (widget->isVisibleTo(process) && isTabControl(*widget) && !partOfAView(*widget, *page->canvas()))
            {
                inCanvas.append(widgetPath(*widget, *process));
            }
        }
        EXPECT_TRUE(inCanvas.isEmpty()) << "left in the canvas: " << inCanvas.join(QStringLiteral(", ")).toStdString();

        // Target image and Channel lead the body, outside every group of the controls; and no group is left empty.
        const an::ui::ProcessStandardParts& parts = page->parts();
        for (const QComboBox* selector : {parts.targetSelector, parts.channelSelector})
        {
            if (selector == nullptr || !selector->isVisibleTo(process))
            {
                continue;
            }
            const QGroupBox* group = nullptr;
            for (const QWidget* at = selector->parentWidget(); at != nullptr && group == nullptr;
                 at = at->parentWidget())
            {
                group = qobject_cast<const QGroupBox*>(at);
            }
            EXPECT_EQ(group, nullptr) << widgetPath(*selector, *process).toStdString()
                                      << " is left in a group of the controls";
        }
        if (surface == an::ui::ProcessSurface::MirrorPage)
        {
            // The canvas is a view of the target, which the process works on.
            const auto* const mirrored = dynamic_cast<const ImageProcessControl*>(process);
            EXPECT_NE(mirrored, nullptr);
            EXPECT_NE(page->previewView(), nullptr) << "the canvas shows the target";
            if (mirrored != nullptr)
            {
                EXPECT_EQ(mirrored->interactionView(), page->previewView());
            }
            EXPECT_FALSE(page->toggles()->isVisibleTo(process)) << "a mirror page has no toggles";
        }
        QStringList empty;
        for (const QGroupBox* group : page->bodyContent()->findChildren<QGroupBox*>())
        {
            if (!group->isVisibleTo(process))
            {
                continue;
            }
            bool holdsSomething = false;
            for (const QWidget* child : group->findChildren<QWidget*>())
            {
                holdsSomething = holdsSomething || child->isVisibleTo(group);
            }
            if (!holdsSomething)
            {
                empty.append(widgetPath(*group, *process));
            }
        }
        EXPECT_TRUE(empty.isEmpty()) << "groups left empty: " << empty.join(QStringLiteral(", ")).toStdString();

        // The window's buttons are the footer's: no button box is left among the controls.
        QStringList boxes;
        for (const QDialogButtonBox* box : page->bodyContent()->findChildren<QDialogButtonBox*>())
        {
            if (box->isVisibleTo(process))
            {
                boxes.append(widgetPath(*box, *process));
            }
        }
        EXPECT_TRUE(boxes.isEmpty()) << "button boxes left in the body: "
                                     << boxes.join(QStringLiteral(", ")).toStdString();

        if (measured)
        {
            // Nothing in the body lies past the column's side.
            const int viewport = page->body()->viewport()->width();
            EXPECT_LE(page->bodyContent()->minimumSizeHint().width(), viewport)
                << "the controls are wider than the column";
            QStringList clipped;
            for (QWidget* widget : page->bodyContent()->findChildren<QWidget*>())
            {
                if (!widget->isVisibleTo(page->bodyContent()) || widget->size().isEmpty())
                {
                    continue;
                }
                const QPoint at = widget->mapTo(page->bodyContent(), QPoint(0, 0));
                if (at.x() < 0 || at.x() + widget->width() > viewport)
                {
                    clipped.append(QStringLiteral("%1 at %2 + %3 in %4")
                                       .arg(widgetPath(*widget, *process))
                                       .arg(at.x())
                                       .arg(widget->width())
                                       .arg(viewport));
                }
            }
            EXPECT_TRUE(clipped.isEmpty()) << "clipped sideways: " << clipped.join(QStringLiteral("; ")).toStdString();
            EXPECT_LE(page->footer()->minimumSizeHint().width(), page->columnWidth()) << "the footer fits its column";
        }
        closeOpenedProcessWindows();
        context.presenter = nullptr;
    }
    return measured;
}

/// Nudges a process's parameters as a person does: its first spin box that can step up, three steps; with none, its
/// first slider a tenth of its range. The same in a window and in a page, whose widgets are made in the same order.
inline void nudgeParameters(QWidget& process)
{
    for (QAbstractSpinBox* box : process.findChildren<QAbstractSpinBox*>())
    {
        const auto* const whole = qobject_cast<const QSpinBox*>(box);
        const auto* const real = qobject_cast<const QDoubleSpinBox*>(box);
        const bool canStepUp = (whole != nullptr && whole->value() < whole->maximum()) ||
                               (real != nullptr && real->value() < real->maximum());
        if (box->isEnabled() && box->isVisibleTo(&process) && canStepUp)
        {
            box->stepBy(3);
            pumpQuietly();
            return;
        }
    }
    for (QAbstractSlider* slider : process.findChildren<QAbstractSlider*>())
    {
        if (slider->isEnabled() && slider->isVisibleTo(&process) && slider->maximum() > slider->minimum())
        {
            slider->setValue(slider->value() + (slider->maximum() - slider->minimum()) / 10);
            pumpQuietly();
            return;
        }
    }
}

/// How a family applies its process: the parameters it sets, and where the result lands.
struct PageApply
{
    /// Sets the process's parameters as a person sets them; `nudgeParameters` when empty.
    std::function<void(ImageRTPreviewControl&)> configure;

    /// Whether the result is a new image beside the target, rather than a step of the target's history.
    bool resultIsANewImage = false;

    /// Whether the process draws its result at random, each dialog from a seed of its own: the two results are then
    /// held to the target's shape and to each having changed it, rather than to the same samples.
    bool drawsAtRandom = false;
};

/// Applies a descriptor's process with its parameters set in a window of its own and, over a second target of the
/// same image, through its page; holds the two results to be the same, sample for sample. A process that closes after
/// its run may close in either.
inline void expectPageAppliesAsTheWindow(const std::string& descriptorId, ProcessTestContext& context,
                                         const std::function<ViewHandle()>& makeTarget, const PageApply& how = {})
{
    std::optional<an::tests::fixtures::FixtureSamples> original;
    const auto applyIn = [&](const bool asPage) -> std::optional<an::tests::fixtures::FixtureSamples> {
        ViewHandle target = makeTarget();
        EXPECT_NE(target, nullptr);
        if (target == nullptr)
        {
            return std::nullopt;
        }
        if (!original.has_value())
        {
            if (const std::shared_ptr<const an::consumer::ImageAsset> asset = target->document().asset())
            {
                std::string error;
                original = an::tests::fixtures::engine::readAssetSamples(*asset, error);
                EXPECT_TRUE(original.has_value()) << error;
            }
        }
        context.addViewControl(target.get());
        context.setActiveImageViewControl(target.get());
        PagePresenter presenter;
        presenter.adopting = asPage;
        context.presenter = &presenter;
        QWidget* const opened = launchWindowOf(descriptorId, *target);
        const QPointer<ImageRTPreviewControl> process = dynamic_cast<ImageRTPreviewControl*>(opened);
        EXPECT_NE(process, nullptr) << descriptorId;
        std::optional<an::tests::fixtures::FixtureSamples> samples;
        const auto settled = [&process]() { return process.isNull() || process->isProcessIdle(); };
        if (process != nullptr)
        {
            EXPECT_EQ(process->hasPageLayout(), asPage);
            (void)pumpUntil(settled);
            if (how.configure)
            {
                how.configure(*process);
            }
            else
            {
                nudgeParameters(*process);
            }
            (void)pumpUntil(settled);
            const std::size_t steps = target->viewHistory() != nullptr ? target->viewHistory()->steps().current : 0u;
            const std::size_t made = context.madeViews.size();
            process->applyProcess();
            (void)pumpUntil(settled);
            (void)pumpUntil([&target]() { return target->isWorkIdle(); });
            const char* const where = asPage ? "in a page" : "in a window";
            std::shared_ptr<const an::consumer::ImageAsset> asset;
            if (how.resultIsANewImage)
            {
                EXPECT_EQ(context.madeViews.size(), made + 1u) << "the Apply ran " << where << " and made one image";
                if (context.madeViews.size() == made + 1u)
                {
                    ImageViewControl* const result = context.madeViews.back();
                    (void)pumpUntil([result]() { return result->isWorkIdle(); });
                    asset = result->document().asset();
                }
            }
            else
            {
                EXPECT_EQ(target->viewHistory()->steps().current, steps + 1u) << "the Apply ran " << where;
                asset = target->document().asset();
            }
            if (asset != nullptr)
            {
                std::string error;
                samples = an::tests::fixtures::engine::readAssetSamples(*asset, error);
                EXPECT_TRUE(samples.has_value()) << error;
            }
        }
        closeOpenedProcessWindows();
        context.presenter = nullptr;
        context.removeViewControl(target.get(), false);
        return samples;
    };
    const std::optional<an::tests::fixtures::FixtureSamples> window = applyIn(false);
    const std::optional<an::tests::fixtures::FixtureSamples> page = applyIn(true);
    ASSERT_TRUE(window.has_value() && page.has_value());
    if (!how.drawsAtRandom)
    {
        EXPECT_TRUE(an::tests::fixtures::sameSamples(*window, *page))
            << descriptorId << ": the page's result is the window's";
        return;
    }
    ASSERT_TRUE(original.has_value());
    for (const an::tests::fixtures::FixtureSamples* result : {&*window, &*page})
    {
        EXPECT_EQ(result->width(), original->width());
        EXPECT_EQ(result->height(), original->height());
        EXPECT_EQ(result->channelCount(), original->channelCount());
        EXPECT_FALSE(an::tests::fixtures::sameSamples(*result, *original)) << descriptorId << " changed the image";
    }
}

/// How a family applies a process of a mirror page: what it sets as a person sets it, and where the result lands.
struct MirrorApply
{
    /// Sets the process's parameters as a person sets them; nothing when empty.
    std::function<void(QWidget&)> configure;

    /// Whether the result is new images beside the target - every one compared - rather than a step of the target's
    /// history.
    bool resultIsANewImage = false;
};

/// Whether a process of no preview has ended its run: no run on its way and its target's work landed.
[[nodiscard]] inline bool mirrorRunEnded(const QPointer<ImageProcessControl>& process, ImageViewControl& target)
{
    return (process.isNull() || (process->isRunIdle() && !process->runState().running())) && target.isWorkIdle();
}

/// Applies a descriptor's process of no preview with its parameters set, in a window of its own and, over a second
/// target of the same image, through its mirror page, each by its primary button as soon as it is offered; holds the
/// two results to be the same, sample for sample - the target's next step, or every image the run made. A process that
/// closes after its run may close in either.
inline void expectMirrorPageAppliesAsTheWindow(const std::string& descriptorId, ProcessTestContext& context,
                                               const std::function<ViewHandle()>& makeTarget,
                                               const MirrorApply& how = {})
{
    using Samples = std::vector<an::tests::fixtures::FixtureSamples>;
    const auto applyIn = [&](const bool asPage) -> std::optional<Samples> {
        ViewHandle target = makeTarget();
        EXPECT_NE(target, nullptr);
        if (target == nullptr)
        {
            return std::nullopt;
        }
        context.addViewControl(target.get());
        context.setActiveImageViewControl(target.get());
        PagePresenter presenter;
        presenter.adopting = asPage;
        context.presenter = &presenter;
        QWidget* const opened = launchWindowOf(descriptorId, *target);
        const QPointer<ImageProcessControl> process = dynamic_cast<ImageProcessControl*>(opened);
        EXPECT_NE(process, nullptr) << descriptorId;
        std::optional<Samples> samples;
        if (process != nullptr)
        {
            const char* const where = asPage ? "in a page" : "in a window";
            EXPECT_EQ(process->hasPageLayout(), asPage) << where;
            pumpQuietly();
            if (how.configure)
            {
                how.configure(*process);
                pumpQuietly();
            }
            const std::size_t steps = target->viewHistory() != nullptr ? target->viewHistory()->steps().current : 0u;
            const std::size_t made = context.madeViews.size();
            EXPECT_TRUE(pumpUntil([&process]() { return process.isNull() || process->triggerPrimaryAction(); }))
                << "the primary button is offered " << where;
            EXPECT_TRUE(pumpUntil([&]() { return mirrorRunEnded(process, *target); }));
            pumpQuietly();
            Samples results;
            std::vector<std::shared_ptr<const an::consumer::ImageAsset>> assets;
            if (how.resultIsANewImage)
            {
                EXPECT_GT(context.madeViews.size(), made) << "the Apply ran " << where << " and made images";
                for (std::size_t index = made; index < context.madeViews.size(); ++index)
                {
                    ImageViewControl* const result = context.madeViews[index];
                    (void)pumpUntil([result]() { return result->isWorkIdle(); });
                    assets.push_back(result->document().asset());
                }
            }
            else
            {
                EXPECT_EQ(target->viewHistory()->steps().current, steps + 1u) << "the Apply ran " << where;
                assets.push_back(target->document().asset());
            }
            for (const std::shared_ptr<const an::consumer::ImageAsset>& asset : assets)
            {
                if (asset == nullptr)
                {
                    continue;
                }
                std::string error;
                std::optional<an::tests::fixtures::FixtureSamples> read =
                    an::tests::fixtures::engine::readAssetSamples(*asset, error);
                EXPECT_TRUE(read.has_value()) << error;
                if (read.has_value())
                {
                    results.push_back(std::move(*read));
                }
            }
            samples = std::move(results);
        }
        closeOpenedProcessWindows();
        context.presenter = nullptr;
        context.removeViewControl(target.get(), false);
        return samples;
    };
    const std::optional<Samples> window = applyIn(false);
    const std::optional<Samples> page = applyIn(true);
    ASSERT_TRUE(window.has_value() && page.has_value());
    ASSERT_FALSE(window->empty()) << descriptorId << " made a result";
    ASSERT_EQ(window->size(), page->size()) << descriptorId << ": the page made what the window made";
    for (std::size_t index = 0u; index < window->size(); ++index)
    {
        EXPECT_TRUE(an::tests::fixtures::sameSamples((*window)[index], (*page)[index]))
            << descriptorId << ": the page's result " << index << " is the window's";
    }
}

} // namespace an::tests::processes::shared
