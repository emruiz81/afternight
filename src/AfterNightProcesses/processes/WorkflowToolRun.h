// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What shows a run a workflow tool starts - Image Calibration, Image Registration, Image Stacking, Image List
/// Analyzer - while it is on its way, and what stops it. Over a tool that is a window of its own it is the progress
/// window, whose Cancel stops the run. A tool shown as a page opens none: its page shows the run at its head, and the
/// page's Cancel asks the tool to stop it (`TopLevelProcessBaseControl::cancelRun`), which stops the run through this.
/// And how a page's primary action presses a tool's own run button.

#include "controls/ProgressDialogControl.h"
#include "controls/TopLevelProcessBaseControl.h"

#include <QAbstractButton>
#include <QPointer>
#include <QString>

#include <stop_token>

namespace an::ui::processes
{

/// The front of one run of a tool: its progress window, where it has one, and the stop its Cancel requests. A tool
/// keeps one for the run on its way and opens another for the next. GUI thread; the token it lends is read anywhere.
class WorkflowToolRun
{
public:
    /// The front of no run: no window, and a stop nothing waits on.
    WorkflowToolRun() = default;

    /// Opens the front of a run that starts: the progress window over `tool`, at nought, or nothing where the tool is
    /// a page.
    ///
    /// @param tool the tool that starts the run.
    /// @param title the progress window's title.
    /// @param text what the window says the run does; nothing when empty.
    [[nodiscard]] static WorkflowToolRun open(TopLevelProcessBaseControl& tool, const QString& title,
                                              const QString& text = {})
    {
        WorkflowToolRun run;
        if (tool.hasPageLayout())
        {
            return run;
        }
        auto* const window = new ProgressDialogControl(&tool, title);
        window->setProgressBarRange(0, 100);
        window->setValue(0);
        if (!text.isEmpty())
        {
            window->setText(text);
        }
        window->show();
        run.m_window = window;
        return run;
    }

    /// The progress window; null on a page, and once the window has gone.
    [[nodiscard]] ProgressDialogControl* window() const
    {
        return m_window.data();
    }

    /// What the run takes as its Cancel: the progress window's token, or the page's.
    [[nodiscard]] std::stop_token cancelToken() const
    {
        return m_window ? m_window->stopToken() : m_pageStop.get_token();
    }

    /// Asks the run to stop, as Cancel does: the progress window's, and the page's.
    void requestCancel()
    {
        m_pageStop.request_stop();
        if (m_window)
        {
            m_window->requestCancel();
        }
    }

private:
    QPointer<ProgressDialogControl> m_window;
    std::stop_source m_pageStop;
};

/// Presses a tool's run button as a page's primary action does: when the button is there, shown and enabled.
///
/// @return whether it was pressed.
inline bool pressWhenOffered(QAbstractButton* const button)
{
    if (button == nullptr || !button->isEnabled() || button->isHidden())
    {
        return false;
    }
    button->click();
    return true;
}

} // namespace an::ui::processes
