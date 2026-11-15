// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "tasks/TaskReadoutSource.h"

#include "controls/ImageRTPreviewControl.h"
#include "controls/ImageViewControl.h"

#include <utility>

namespace an::ui::observatory
{

TaskReadoutSource::TaskReadoutSource(ImageRTPreviewControl& process) : m_process(&process)
{
    process.setPreviewFrameListener([this, alive = m_alive](const bool interactive) {
        if (!*alive)
        {
            return;
        }
        m_interactive = interactive;
        if (m_listener)
        {
            m_listener();
        }
    });
}

TaskReadoutSource::~TaskReadoutSource()
{
    // The process may be going already: it is told nothing, and the listener it holds hears no more of this source.
    *m_alive = false;
}

std::optional<an::ui::shell::IReadoutFrameSource::Frame> TaskReadoutSource::latestFrame() const
{
    if (!m_process || m_process->previewView() == nullptr || m_process->previewFramesShown() == 0u)
    {
        return std::nullopt;
    }
    std::shared_ptr<const an::consumer::ImageAsset> image = m_process->previewView()->document().asset();
    if (!image)
    {
        return std::nullopt;
    }
    return Frame{.image = std::move(image), .interactive = m_interactive, .number = m_process->previewFramesShown()};
}

std::shared_ptr<const an::consumer::ImageAsset> TaskReadoutSource::original() const
{
    ImageViewControl* const view = target();
    return view != nullptr ? view->document().asset() : nullptr;
}

ImageViewControl* TaskReadoutSource::target() const
{
    return m_process ? m_process->processTarget() : nullptr;
}

void TaskReadoutSource::setFrameListener(std::function<void()> listener)
{
    m_listener = std::move(listener);
}

} // namespace an::ui::observatory
