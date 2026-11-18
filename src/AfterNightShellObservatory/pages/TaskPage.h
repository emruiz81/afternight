// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A task's page in the centre: the task's header over the process arranged as a page, the process's preview as the
/// canvas with its toolbar floating over it and the target's history under it, the compact readout at the head of the
/// process's column.

#include "AfterNightShellObservatoryExport.h"

#include <QPointer>
#include <QWidget>

#include <functional>
#include <memory>

class ImageViewControl;
class QEvent;
class QMenu;
class QResizeEvent;
class TopLevelProcessBaseControl;

namespace an::ui
{
class AnCompactReadout;
class AnFloatingToolbar;
class AnHistoryStrip;
class AnTaskHeader;
} // namespace an::ui

namespace an::ui::shell
{
class ViewChromeBinder;
} // namespace an::ui::shell

namespace an::ui::observatory
{

class ToolbarClearance;

/// The page of a task, one per task taken into a page, kept alive in the centre while another place is shown:
///
/// ```
/// +----------------------------------------------------------------------+
/// | task header: [thumb] Curves Transformation on M20   state   [^] [x]  |
/// +---------------------------------------------+------------------------+
/// | canvas: the preview, the floating toolbar   | compact readout        |
/// |                                             | the process's column   |
/// | history strip: the target's steps, pending  |                        |
/// +---------------------------------------------+------------------------+
/// ```
///
/// The process is laid into `processHost()` (`TopLevelProcessBaseControl::attachToPage`) and dressed (`dress`): the
/// history strip goes under its canvas, the compact readout at the head of its column, the toolbar floats over its
/// canvas, 12 px above its foot or 8 px above a sequence's player there (`ToolbarClearance`), and shows the preview
/// view; the strip shows the target's history. A full page has no canvas and no column: it is the process whole under
/// the header, whose menu button lists the process's own menus when it has any; and where its process holds a job -
/// an integration - the header says the job in place of the process's name (`ProcessPageLayout::jobHeading`), and
/// says it again each time the process states it anew. Used on the GUI thread.
class AFTERNIGHTSHELLOBSERVATORY_API TaskPage final : public QWidget
{
public:
    explicit TaskPage(QWidget* parent = nullptr);
    ~TaskPage() override;

    TaskPage(const TaskPage&) = delete;
    TaskPage& operator=(const TaskPage&) = delete;
    TaskPage(TaskPage&&) = delete;
    TaskPage& operator=(TaskPage&&) = delete;

    [[nodiscard]] an::ui::AnTaskHeader* header() const noexcept;

    /// Where the process is laid as a page.
    [[nodiscard]] QWidget* processHost() const noexcept;

    [[nodiscard]] an::ui::AnFloatingToolbar* toolbar() const noexcept;
    [[nodiscard]] an::ui::AnHistoryStrip* historyStrip() const noexcept;
    [[nodiscard]] an::ui::AnCompactReadout* compactReadout() const noexcept;
    [[nodiscard]] an::ui::shell::ViewChromeBinder& binder() const noexcept;

    /// Puts the strip under the process's canvas and the compact readout at the head of its column, and follows its
    /// preview view: the toolbar shows it while there is one, and `previewChanged` is told of it. The process is
    /// arranged as a page already.
    void dress(TopLevelProcessBaseControl& process, std::function<void(ImageViewControl*)> previewChanged = {});

    /// The process the page shows; null once it has gone.
    [[nodiscard]] TopLevelProcessBaseControl* process() const;

    /// The view the toolbar shows - the process's preview - and the view whose history the strip shows - its target.
    void bindViews(ImageViewControl* canvas, ImageViewControl* target);

    /// The canvas, in the page's coordinates: what the toolbar floats over; empty on a full page.
    [[nodiscard]] QRect canvasArea() const;

    /// The menu of a full page's own menus, as the header's menu button shows it: one entry a menu, each opening the
    /// process's menu. Made each time it is asked for; the caller owns it.
    [[nodiscard]] QMenu* makeProcessMenu(QWidget* parent) const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void placeToolbar();

    an::ui::AnTaskHeader* m_header = nullptr;
    QWidget* m_processHost = nullptr;
    an::ui::AnFloatingToolbar* m_toolbar = nullptr;
    an::ui::AnHistoryStrip* m_strip = nullptr;
    an::ui::AnCompactReadout* m_readout = nullptr;
    std::unique_ptr<an::ui::shell::ViewChromeBinder> m_binder;
    QPointer<TopLevelProcessBaseControl> m_process;
    QPointer<QWidget> m_canvas;
    std::unique_ptr<ToolbarClearance> m_clearance;
};

} // namespace an::ui::observatory
