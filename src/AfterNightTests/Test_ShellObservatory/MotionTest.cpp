// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Observatory window with its animations off, as a person who asked for no movement meets it: during a run that
/// cannot say how far it has come - the rail's arc, the task header's bar, the page's run row - nothing moves, whether
/// the animations were off before the run or turned off while it ran; no animation of the window runs; a section of a
/// page opens and closes at once.

#include "ObservatoryPages.h"

#include "components/AnCollapsibleSection.h"
#include "components/AnIconRail.h"
#include "controls/ImageRTPreviewControl.h"
#include "controls/ProcessPresentation.h"
#include "theme/AnimationUtils.h"

#include <QAbstractAnimation>
#include <QRect>
#include <QThreadPool>
#include <QImage>
#include <QWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>

namespace an::tests::observatory
{
namespace
{

using an::ui::AnimationUtils;
using an::ui::ProcessRunState;

/// Whether the animations are on, put back when it goes.
class ScopedAnimations final
{
public:
    explicit ScopedAnimations(const bool enabled) : m_before(AnimationUtils::animationsEnabled())
    {
        AnimationUtils::setAnimationsEnabled(enabled);
    }
    ~ScopedAnimations()
    {
        AnimationUtils::setAnimationsEnabled(m_before);
    }
    ScopedAnimations(const ScopedAnimations&) = delete;
    ScopedAnimations& operator=(const ScopedAnimations&) = delete;

private:
    bool m_before;
};

class Motion : public PagesCase
{
protected:
    /// Runs the event loop for a time: what turns on a timer has had several of its steps.
    static void wait(const int milliseconds)
    {
        (void)an::tests::app::processEventsUntil([]() { return false; }, milliseconds);
    }

    /// The window as it is drawn now.
    [[nodiscard]] static QImage picture()
    {
        return window().grab().toImage().convertToFormat(QImage::Format_ARGB32);
    }

    /// The number of pixels in which two pictures differ.
    [[nodiscard]] static int differingPixels(const QImage& a, const QImage& b)
    {
        if (a.size() != b.size())
        {
            return a.width() * a.height();
        }
        int differing = 0;
        for (int y = 0; y < a.height(); ++y)
        {
            const auto* first = reinterpret_cast<const QRgb*>(a.constScanLine(y));
            const auto* second = reinterpret_cast<const QRgb*>(b.constScanLine(y));
            for (int x = 0; x < a.width(); ++x)
            {
                differing += first[x] != second[x] ? 1 : 0;
            }
        }
        return differing;
    }

    /// Where two pictures differ, as a failure says it: the rectangle that holds every pixel that differs, and the
    /// widget under its centre.
    [[nodiscard]] static std::string whereTheyDiffer(const QImage& a, const QImage& b)
    {
        QRect differing;
        for (int y = 0; y < (std::min)(a.height(), b.height()); ++y)
        {
            for (int x = 0; x < (std::min)(a.width(), b.width()); ++x)
            {
                if (a.pixel(x, y) != b.pixel(x, y))
                {
                    differing = differing.united(QRect(x, y, 1, 1));
                }
            }
        }
        if (differing.isEmpty())
        {
            return "nowhere";
        }
        const QWidget* const under = window().childAt(differing.center());
        return QStringLiteral("%1,%2 %3x%4 in %5 '%6'")
            .arg(differing.x())
            .arg(differing.y())
            .arg(differing.width())
            .arg(differing.height())
            .arg(under != nullptr ? QString::fromLatin1(under->metaObject()->className()) : QStringLiteral("nothing"))
            .arg(under != nullptr ? under->objectName() : QString())
            .toStdString();
    }

    /// The animations of the window that run now.
    [[nodiscard]] static int runningAnimations()
    {
        int running = 0;
        for (const QAbstractAnimation* animation : window().findChildren<QAbstractAnimation*>())
        {
            running += animation->state() == QAbstractAnimation::Running ? 1 : 0;
        }
        return running;
    }

    /// Curves Transformation's page, its run on its way and unable to say how far it has come: the rail's arc turns,
    /// the task header's bar slides and the page's run row turns its arc, while the animations are on. The focus is on
    /// the rail, from the pointer, so no caret blinks.
    static TopLevelProcessBaseControl* aRunThatCannotSayHowFar()
    {
        EXPECT_NE(open(Page::PreviewTask), nullptr);
        TopLevelProcessBaseControl* const process = shownProcess();
        EXPECT_NE(process, nullptr);
        if (process == nullptr)
        {
            return nullptr;
        }
        // The preview lands first: a preview landing between two pictures is no movement.
        auto* const preview = dynamic_cast<ImageRTPreviewControl*>(process);
        EXPECT_TRUE(an::tests::app::processEventsUntil([preview]() {
            QThreadPool::globalInstance()->waitForDone(20);
            return preview == nullptr || preview->isProcessIdle();
        }));
        QThreadPool::globalInstance()->waitForDone();
        settle();
        process->setRunState(
            ProcessRunState{.phase = ProcessRunState::Phase::Running, .text = QStringLiteral("applying")});
        window().rail()->setFocus(Qt::MouseFocusReason);
        settle();
        return process;
    }
};

// With the animations off before the run, nothing of the window moves while it runs, and no animation of it runs.
TEST_F(Motion, NoAnimationWhenDisabled)
{
    const ScopedAnimations off(false);
    ASSERT_NE(aRunThatCannotSayHowFar(), nullptr);
    wait(150);
    const QImage before = picture();
    wait(400);
    const QImage after = picture();
    EXPECT_EQ(differingPixels(before, after), 0)
        << "the window moved with its animations off, at " << whereTheyDiffer(before, after);
    EXPECT_EQ(runningAnimations(), 0);
}

// Turned off while the run moves the window, the animations stop: after a step of what turned, nothing moves.
TEST_F(Motion, WhatMovesStopsWhenTheAnimationsAreTurnedOff)
{
    const ScopedAnimations on(true);
    ASSERT_NE(aRunThatCannotSayHowFar(), nullptr);
    wait(150);
    const QImage moving = picture();
    wait(400);
    ASSERT_GT(differingPixels(moving, picture()), 0) << "nothing moved with the animations on: the case shows nothing";

    AnimationUtils::setAnimationsEnabled(false);
    wait(300);
    const QImage before = picture();
    wait(400);
    const QImage after = picture();
    EXPECT_EQ(differingPixels(before, after), 0)
        << "the window still moves after the animations were turned off, at " << whereTheyDiffer(before, after);
}

// With the animations off a page's section opens and closes at once: its content is its whole height, or none, as soon
// as it is asked, and no animation runs.
TEST_F(Motion, ASectionOpensAndClosesAtOnce)
{
    const ScopedAnimations off(false);
    ASSERT_NE(open(Page::ImageIntegrationLite), nullptr);
    TopLevelProcessBaseControl* const process = shownProcess();
    ASSERT_NE(process, nullptr);
    an::ui::AnCollapsibleSection* section = nullptr;
    for (QWidget* widget : process->findChildren<QWidget*>())
    {
        if (auto* const found = dynamic_cast<an::ui::AnCollapsibleSection*>(widget);
            found != nullptr && found->isVisibleTo(process))
        {
            section = found;
            break;
        }
    }
    ASSERT_NE(section, nullptr) << "the page has no section to open";
    const bool wasOpen = section->isExpanded();
    section->setExpanded(!wasOpen);
    EXPECT_EQ(runningAnimations(), 0);
    QCoreApplication::processEvents();
    const int content = section->contentWidget()->height();
    if (wasOpen)
    {
        EXPECT_EQ(content, 0) << "a section closed at once keeps " << content << " px";
    }
    else
    {
        EXPECT_GT(content, 0) << "a section opened at once shows nothing";
    }
    section->setExpanded(wasOpen);
    EXPECT_EQ(runningAnimations(), 0);
}

} // namespace
} // namespace an::tests::observatory
