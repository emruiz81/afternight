// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/ProcessPresentation.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <algorithm>

namespace an::ui
{

QString applyingRunText()
{
    return QStringLiteral("applying");
}

ProcessRunState runningState(const QString& text, const std::optional<double> progress)
{
    ProcessRunState state;
    state.phase = ProcessRunState::Phase::Running;
    state.text = text.isEmpty() ? applyingRunText() : text;
    if (progress.has_value())
    {
        state.progress = (std::clamp)(*progress, 0.0, 1.0);
    }
    return state;
}

ProcessRunState sequenceRunningState(const QString& text)
{
    ProcessRunState state = runningState(text, 0.0);
    state.sequence = true;
    return state;
}

ProcessRunState endedRunState(const an::Status& status, const bool published)
{
    ProcessRunState state;
    if (published)
    {
        state.phase = ProcessRunState::Phase::Succeeded;
        return state;
    }
    if (status.ok() || status.code() == an::ErrorCode::Cancelled)
    {
        // Stopped, or its result dropped because the image changed or closed under it: nothing failed.
        state.phase = ProcessRunState::Phase::Cancelled;
        return state;
    }
    state.phase = ProcessRunState::Phase::Failed;
    state.text = QString::fromStdString(status.message());
    return state;
}

} // namespace an::ui
