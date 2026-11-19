// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The context a hosted extension process runs in, in this suite: the viewer's, with a store of process settings and
/// the register of the open process windows a process window reaches as it opens and closes.

#include "Test_ControlsViewer/ViewerTestContext.h"

#include "controls/IAppContext.h"

#include <QString>

#include <algorithm>
#include <map>
#include <vector>

namespace an::tests::seam
{

/// The viewer's context, with what a process window reaches as it opens and closes: a store of process settings and the
/// register of the open process windows.
class HostedProcessContext final : public an::tests::viewer::ViewerTestContext,
                                   public an::ui::IProcessSettingsProvider,
                                   public an::ui::IProcessRegistry
{
public:
    using an::tests::viewer::ViewerTestContext::ViewerTestContext;

    [[nodiscard]] an::ui::IProcessSettingsProvider* getProcessSettings() override
    {
        return this;
    }

    [[nodiscard]] an::ui::IProcessRegistry* getProcessRegistry() override
    {
        return this;
    }

    void registerProcessInstance(TopLevelProcessBaseControl* process) override
    {
        m_open.push_back(process);
    }

    void unregisterProcessInstance(TopLevelProcessBaseControl* process) override
    {
        m_open.erase(std::remove(m_open.begin(), m_open.end(), process), m_open.end());
    }

    bool upsert(const QString& processName, int winState, int width, int height, const QString& json) override
    {
        m_stored[processName] = Stored{winState, width, height, json};
        return true;
    }

    [[nodiscard]] bool select(const QString& processName, int& winState, int& width, int& height,
                              QString& json) override
    {
        const auto found = m_stored.find(processName);
        if (found == m_stored.end())
        {
            return false;
        }
        winState = found->second.winState;
        width = found->second.width;
        height = found->second.height;
        json = found->second.json;
        return true;
    }

private:
    struct Stored
    {
        int winState = 0;
        int width = 0;
        int height = 0;
        QString json;
    };
    std::map<QString, Stored> m_stored;
    std::vector<TopLevelProcessBaseControl*> m_open;
};

} // namespace an::tests::seam
