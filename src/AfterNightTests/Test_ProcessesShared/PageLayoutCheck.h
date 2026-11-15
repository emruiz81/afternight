// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A family's process held to its page: in its column at 600 px of height, in both densities of the Observatory look,
/// every control is reached by Tab and - where the platform measures text as the interface does - nothing in the body
/// lies past the column's side; and the same parameters applied through the page give the window's result.
///
/// Whether nothing is clipped depends on how wide text is, and the suites' own platform draws text in a box font about
/// twice the interface's width: there the case asserts what holds whatever the font and is reported skipped for the
/// rest. Started under the native platform (`QT_QPA_PLATFORM=windows`, the pages kept off the screen) it measures text
/// as the interface does, as a stage's gate measures the pages.

#include "Test_ProcessesShared/ObservatoryWindowCheck.h"

#include "controls/ImageRTPreviewControl.h"
#include "controls/ProcessPageLayout.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "fixtures/common/FixtureImage.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSpinBox>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <functional>
#include <string>

namespace an::tests::processes::shared
{

/// The height a page is held to: a short window's.
inline constexpr int kPageCheckHeight = 600;

/// Whether the platform measures text as the interface does: the box font of the suites' platform gives every
/// character one width.
[[nodiscard]] inline bool textIsMeasured()
{
    const QFontMetrics metrics(QApplication::font());
    return metrics.horizontalAdvance(QStringLiteral("iiiiiiiiii")) <
           metrics.horizontalAdvance(QStringLiteral("WWWWWWWWWW"));
}

/// Whether a widget is a control a person reaches by Tab: a button, a field, a choice, a slider or a spin box.
[[nodiscard]] inline bool isTabControl(const QWidget& widget)
{
    return qobject_cast<const QAbstractButton*>(&widget) != nullptr ||
           qobject_cast<const QAbstractSpinBox*>(&widget) != nullptr ||
           qobject_cast<const QComboBox*>(&widget) != nullptr ||
           qobject_cast<const QAbstractSlider*>(&widget) != nullptr ||
           qobject_cast<const QLineEdit*>(&widget) != nullptr;
}

/// The process a launch took into a page; null when none was.
[[nodiscard]] inline TopLevelProcessBaseControl* launchedPage(const std::string& descriptorId, ImageViewControl& target)
{
    QWidget* const opened = launchWindowOf(descriptorId, target);
    auto* const process = dynamic_cast<TopLevelProcessBaseControl*>(opened);
    return process != nullptr && process->pageLayout() != nullptr ? process : nullptr;
}

/// Holds a descriptor's page to its column at 600 px of height in both densities. Answers whether text was measured,
/// so the case knows whether everything it holds was asserted.
inline bool expectPageFitsItsColumn(const std::string& descriptorId, ImageViewControl& target,
                                    ProcessTestContext& context)
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
    EXPECT_EQ(descriptor->surface, an::ui::ProcessSurface::PreviewPage) << descriptorId << " is a page";
    const bool measured = textIsMeasured();
    for (const ObservatoryLook* look : {&observatoryLooks()[0], &observatoryLooks()[3]})
    {
        SCOPED_TRACE(look->name);
        const ObservatoryStyleScope scope(*look, an::ui::StyleProfile::Observatory);
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
            if (!chain.contains(widget) && !chain.contains(widget->focusProxy()))
            {
                unreached.append(widgetPath(*widget, *process));
            }
        }
        EXPECT_TRUE(unreached.isEmpty()) << "not reached by Tab: "
                                         << unreached.join(QStringLiteral(", ")).toStdString();

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

/// Applies a descriptor's process with its parameters nudged in a window of its own and, over a second target of the
/// same image, through its page; holds the two results to be the same, sample for sample.
inline void expectPageAppliesAsTheWindow(const std::string& descriptorId, ProcessTestContext& context,
                                         const std::function<ViewHandle()>& makeTarget)
{
    const auto applyIn = [&](const bool asPage) -> std::optional<an::tests::fixtures::FixtureSamples> {
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
        auto* const process = dynamic_cast<ImageRTPreviewControl*>(opened);
        EXPECT_NE(process, nullptr) << descriptorId;
        std::optional<an::tests::fixtures::FixtureSamples> samples;
        if (process != nullptr)
        {
            EXPECT_EQ(process->hasPageLayout(), asPage);
            (void)pumpUntil([process]() { return process->isProcessIdle(); });
            nudgeParameters(*process);
            (void)pumpUntil([process]() { return process->isProcessIdle(); });
            const std::size_t steps = target->viewHistory() != nullptr ? target->viewHistory()->steps().current : 0u;
            process->applyProcess();
            (void)pumpUntil([process]() { return process->isProcessIdle(); });
            (void)pumpUntil([&target]() { return target->isWorkIdle(); });
            EXPECT_EQ(target->viewHistory()->steps().current, steps + 1u)
                << "the Apply ran " << (asPage ? "in a page" : "in a window");
            if (const std::shared_ptr<const an::consumer::ImageAsset> asset = target->document().asset())
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
    EXPECT_TRUE(an::tests::fixtures::sameSamples(*window, *page))
        << descriptorId << ": the page's result is the window's";
}

} // namespace an::tests::processes::shared
