// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A stand-in for the history the viewer reaches through its companion seam, in this suite, which compiles the viewer's
/// closure rather than linking the Controls library: it records what the viewer asks, keeps no image and answers as
/// the case steers.

#include "Test_ControlsSeams/ViewCompanionsStub.h"

#include "controls/ImageViewControl.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace an::tests::viewer
{

namespace
{

/// A history that records what the view asks and keeps no images.
class StandInHistory final : public an::ui::IViewHistory
{
public:
    void registerInitialStep(const std::string& description) override
    {
        companionRecord().initialSteps.push_back(description);
    }

    [[nodiscard]] EditPreparation editPreparation(const std::string&) override
    {
        ++companionRecord().editsPrepared;
        return {};
    }

    [[nodiscard]] bool registerEditBeforePublishing(const std::string& name, const std::string&,
                                                    const an::consumer::ImageAsset& edited) override
    {
        if (companionRecord().refuseEdits)
        {
            return false;
        }
        companionRecord().recordedEdits.push_back(name);
        companionRecord().recordedEditAssets.push_back(edited.identity());
        m_steps.push_back(name);
        return true;
    }

    bool registerShownState(const std::string& name, const std::string&) override
    {
        m_steps.push_back(name);
        return true;
    }

    void registerRename() override
    {
        ++companionRecord().renames;
    }

    bool undo() override
    {
        ++companionRecord().undosAsked;
        return false;
    }

    bool redo() override
    {
        ++companionRecord().redosAsked;
        return false;
    }

    [[nodiscard]] bool hasUndo() const override
    {
        return !m_steps.empty();
    }

    [[nodiscard]] bool hasRedo() const override
    {
        return false;
    }

    [[nodiscard]] std::string previousStepName() const override
    {
        return m_steps.empty() ? std::string() : m_steps.back();
    }

    [[nodiscard]] std::string nextStepName() const override
    {
        return std::string();
    }

    void showHistory() override
    {
        ++companionRecord().historyWindows;
    }

    void inheritLineage(const an::ui::IViewHistory&) override
    {
        ++companionRecord().lineagesInherited;
    }

    [[nodiscard]] an::ui::ViewHistorySteps steps() const override
    {
        // The steps recorded, the last one shown.
        an::ui::ViewHistorySteps listed;
        for (const std::string& name : m_steps)
        {
            listed.steps.push_back({.name = name});
        }
        listed.current = m_steps.empty() ? 0u : m_steps.size() - 1u;
        return listed;
    }

    bool jumpToStep(std::size_t) override
    {
        ++companionRecord().jumpsAsked;
        return false;
    }

    [[nodiscard]] std::uint64_t addStepsListener(StepsListener) override
    {
        return ++m_lastToken;
    }

    void removeStepsListener(std::uint64_t) override {}

private:
    std::vector<std::string> m_steps;
    std::uint64_t m_lastToken = 0u;
};

} // namespace

} // namespace an::tests::viewer

namespace an::ui
{

std::unique_ptr<IViewHistory> createViewHistory(::ImageViewControl&)
{
    return std::make_unique<an::tests::viewer::StandInHistory>();
}

} // namespace an::ui
