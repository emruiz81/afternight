// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "pages/TaskPage.h"

#include "chrome/ToolbarClearance.h"
#include "views/ViewChromeBinder.h"

#include "components/AnCompactReadout.h"
#include "components/AnFloatingToolbar.h"
#include "components/AnHistoryStrip.h"
#include "components/AnTaskHeader.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessPageLayout.h"
#include "controls/TopLevelProcessBaseControl.h"

#include <QEvent>
#include <QLayout>
#include <QMenu>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <optional>

namespace an::ui::observatory
{

TaskPage::TaskPage(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("observatoryTaskPage"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_header = new an::ui::AnTaskHeader(this);
    layout->addWidget(m_header);
    m_processHost = new QWidget(this);
    m_processHost->setObjectName(QStringLiteral("observatoryTaskProcessHost"));
    auto* hostLayout = new QVBoxLayout(m_processHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);
    layout->addWidget(m_processHost, 1);

    m_strip = new an::ui::AnHistoryStrip(this);
    m_strip->setObjectName(QStringLiteral("observatoryTaskHistoryStrip"));
    m_readout = new an::ui::AnCompactReadout(this);
    m_readout->setObjectName(QStringLiteral("observatoryTaskReadout"));
    m_toolbar = new an::ui::AnFloatingToolbar(this);
    m_toolbar->setObjectName(QStringLiteral("observatoryTaskToolbar"));
    m_toolbar->hide();
    m_binder = std::make_unique<an::ui::shell::ViewChromeBinder>(m_toolbar, m_strip);
    m_clearance = std::make_unique<ToolbarClearance>([this]() { placeToolbar(); });
}

TaskPage::~TaskPage()
{
    // A process that outlives its page tells it of its job no more.
    if (an::ui::ProcessPageLayout* const page = m_process ? m_process->pageLayout() : nullptr)
    {
        page->setJobHeadingListener({});
    }
    // The players stop telling the page before its views go, and the binder lets go of the views before the toolbar
    // and the strip go with the page.
    m_clearance.reset();
    m_binder.reset();
}

an::ui::AnTaskHeader* TaskPage::header() const noexcept
{
    return m_header;
}

QWidget* TaskPage::processHost() const noexcept
{
    return m_processHost;
}

an::ui::AnFloatingToolbar* TaskPage::toolbar() const noexcept
{
    return m_toolbar;
}

an::ui::AnHistoryStrip* TaskPage::historyStrip() const noexcept
{
    return m_strip;
}

an::ui::AnCompactReadout* TaskPage::compactReadout() const noexcept
{
    return m_readout;
}

an::ui::shell::ViewChromeBinder& TaskPage::binder() const noexcept
{
    return *m_binder;
}

TopLevelProcessBaseControl* TaskPage::process() const
{
    return m_process.data();
}

void TaskPage::setJobChanged(std::function<void()> told)
{
    m_jobChanged = std::move(told);
}

void TaskPage::dress(TopLevelProcessBaseControl& process, std::function<void(ImageViewControl*)> previewChanged)
{
    m_process = &process;
    an::ui::ProcessPageLayout* const page = process.pageLayout();
    if (page == nullptr)
    {
        return;
    }
    if (page->canvas() == nullptr)
    {
        // A full page: the process whole, its own menus under the header's button, and the job it holds - when it
        // states one - said in the header in place of the process's name, now and each time it changes.
        m_readout->hide();
        m_strip->hide();
        m_header->setMenuButtonVisible(!page->menus().isEmpty());
        const auto sayTheJob = [this]() {
            const an::ui::ProcessPageLayout* const layout = m_process ? m_process->pageLayout() : nullptr;
            const an::ui::JobHeading* const job = layout != nullptr ? layout->jobHeading() : nullptr;
            m_header->setJob(job != nullptr ? std::optional<an::ui::JobHeading>(*job) : std::nullopt);
            if (m_jobChanged)
            {
                m_jobChanged();
            }
        };
        page->setJobHeadingListener(sayTheJob);
        sayTheJob();
        return;
    }
    if (QLayout* const foot = page->canvasFoot()->layout())
    {
        foot->addWidget(m_strip);
    }
    if (QLayout* const slot = page->readoutSlot()->layout())
    {
        slot->addWidget(m_readout);
    }
    // The compact readout reads the task's preview: a mirror page has none, and shows no readout.
    m_readout->setVisible(process.processSurface() != an::ui::ProcessSurface::MirrorPage);
    m_canvas = page->canvas();
    m_canvas->installEventFilter(this);
    page->setPreviewViewListener([this, previewChanged = std::move(previewChanged)](ImageViewControl* preview) {
        m_binder->bindToolbar(preview);
        placeToolbar();
        if (previewChanged)
        {
            previewChanged(preview);
        }
    });
    m_binder->bindToolbar(page->previewView());
    placeToolbar();
}

void TaskPage::bindViews(ImageViewControl* canvas, ImageViewControl* target)
{
    m_binder->bindToolbar(canvas);
    m_binder->bindHistory(target);
    placeToolbar();
}

QMenu* TaskPage::makeProcessMenu(QWidget* parent) const
{
    auto* menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("observatoryTaskProcessMenu"));
    const an::ui::ProcessPageLayout* const page = m_process ? m_process->pageLayout() : nullptr;
    if (page == nullptr)
    {
        return menu;
    }
    for (QMenu* const processMenu : page->menus())
    {
        // The process's own menu, so each entry is the process's action and acts as it does in its menu bar.
        menu->addMenu(processMenu);
    }
    return menu;
}

QRect TaskPage::canvasArea() const
{
    if (m_canvas.isNull())
    {
        return {};
    }
    return QRect(m_canvas->mapTo(this, QPoint(0, 0)), m_canvas->size());
}

bool TaskPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_canvas.data() &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Move || event->type() == QEvent::Show))
    {
        placeToolbar();
    }
    return QWidget::eventFilter(watched, event);
}

void TaskPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    placeToolbar();
}

void TaskPage::placeToolbar()
{
    // The toolbar floats over the preview, clear of a sequence's player shown under it, and is hidden while there is
    // none.
    const bool shown = m_binder->toolbarView() != nullptr && !m_canvas.isNull();
    m_toolbar->setVisible(shown);
    if (shown && m_clearance)
    {
        m_toolbar->placeOver(canvasArea(), m_clearance->playersIn(*m_canvas, *this));
        m_toolbar->raise();
    }
}

} // namespace an::ui::observatory
