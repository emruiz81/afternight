// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The history the image view reaches through its companion seam: `createViewHistory`, the view as the tracker sees it,
/// and the windows over the history the view opens.

#include "stdafx.hpp"

#include "controls/IAppContext.h"
#include "controls/ImageHistoryControl.h"
#include "controls/ImageHistoryHost.h"
#include "controls/ImageHistoryTracker.h"
#include "controls/ImageHistoryTrackerUtils.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <QPointer>

#include <memory>
#include <utility>
#include <vector>

namespace
{

/// The image view as its history tracker sees it.
class ViewHistoryHost final : public an::ui::history::ImageHistoryHost
{
public:
    explicit ViewHistoryHost(ImageViewControl& view) : m_View(view) {}

    [[nodiscard]] std::string historyName() const override
    {
        return m_View.getUniqueName();
    }

    void setHistoryName(const std::string& name) override
    {
        m_View.setUniqueName(name);
    }

    [[nodiscard]] std::uint64_t historyDocument() const override
    {
        return m_View.document().identity();
    }

    [[nodiscard]] std::shared_ptr<const an::consumer::ImageAsset> historyAsset() const override
    {
        return m_View.document().asset();
    }

    [[nodiscard]] std::optional<an::ui::DocumentSequence> historySequence() const override
    {
        return m_View.document().sequence();
    }

    [[nodiscard]] bool historyAutoDebayer() const override
    {
        return m_View.isSequenceAutodebayerEnabled();
    }

    void restoreHistoryImage(an::consumer::ImageAsset&& image, an::ui::history::RestoreDone done) override
    {
        // A new asset becomes the document's image, so nothing derived from the image shown is taken for it.
        m_View.replaceImage(std::move(image), false, std::move(done));
    }

    void restoreHistorySequence(std::optional<an::ui::DocumentSequence> sequence, const std::uint64_t frameIndex,
                                an::consumer::ImageAsset&& frame, an::ui::history::RestoreDone done) override
    {
        if (sequence.has_value())
        {
            // Another sequence: a document of its own around the frame read from it.
            an::ui::DocumentNames names = m_View.document().names();
            names.sourcePath = sequence->sourcePath;
            an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(frame), names);
            if (!document.hasValue())
            {
                done(document.status());
                return;
            }
            sequence->currentFrame = frameIndex;
            document->setSequence(std::move(sequence));
            m_View.setSequenceDocument(std::move(*document), m_View.isSequenceAutodebayerEnabled());
            done(an::Status::success());
            return;
        }

        // The same sequence at the step's frame: the player shows that frame, and the step's frame replaces it.
        auto held = std::make_shared<an::consumer::ImageAsset>(std::move(frame));
        QPointer<ImageViewControl> guard(&m_View);
        auto show = [guard, held, frameIndex, done = std::move(done)]() {
            if (!guard)
            {
                done(an::Status::failure(an::ErrorCode::InvalidState, "the view closed"));
                return;
            }
            guard->replaceImage(std::move(*held), false, [guard, frameIndex, done](const an::Status& shown) {
                if (shown.ok() && guard)
                {
                    if (std::optional<an::ui::DocumentSequence> current = guard->document().sequence())
                    {
                        current->currentFrame = frameIndex;
                        guard->document().setSequence(std::move(current));
                    }
                }
                done(shown);
            });
        };
        an::ui::IViewSequencePlayer* player = m_View.sequencePlayer();
        if (player != nullptr && player->currentFrame() != frameIndex)
        {
            player->setCurrentFrame(frameIndex, std::move(show));
            return;
        }
        show();
    }

    void historyChanged() override
    {
        m_View.upcateImageHistory();
    }

    void activateForHistory() override
    {
        m_View.setActiveControl();
    }

    [[nodiscard]] QWidget* historyDialogParent() const override
    {
        return &m_View;
    }

private:
    ImageViewControl& m_View;
};

/// A view's undo history: its tracker, the windows over it the view opened, and the listeners of its steps, which it
/// tells of every change the tracker tells its own listeners of.
class ViewHistory final : public an::ui::IViewHistory, private ImageHistoryTrackerListener
{
public:
    ViewHistory(ImageViewControl& view, const an::consumer::EngineSession& session)
        : m_View(view), m_Host(view), m_Tracker(std::make_shared<ImageHistoryTracker>(m_Host, session))
    {
        m_Tracker->addListener(this);
    }

    ~ViewHistory() override
    {
        m_Tracker->removeListener(this);
        // The windows read the tracker: they go first.
        for (const QPointer<ImageHistoryControl>& window : m_Windows)
        {
            delete window.data();
        }
    }

    ViewHistory(const ViewHistory&) = delete;
    ViewHistory& operator=(const ViewHistory&) = delete;

    void registerInitialStep(const std::string& description) override
    {
        m_Tracker->registerInitialStep(description);
    }

    [[nodiscard]] EditPreparation editPreparation(const std::string& name) override
    {
        return m_Tracker->editPreparation(name);
    }

    [[nodiscard]] bool registerEditBeforePublishing(const std::string& name, const std::string& description,
                                                    const an::consumer::ImageAsset& edited) override
    {
        return m_Tracker->registerEditBeforePublishing(name, description, edited);
    }

    bool registerShownState(const std::string& name, const std::string& description) override
    {
        return m_Tracker->registerShownState(name, description);
    }

    void registerRename() override
    {
        m_Tracker->registerNewImageRenameStep();
    }

    bool undo() override
    {
        return m_Tracker->undoStep();
    }

    bool redo() override
    {
        return m_Tracker->redoStep();
    }

    [[nodiscard]] bool hasUndo() const override
    {
        return m_Tracker->hasUndoSteps();
    }

    [[nodiscard]] bool hasRedo() const override
    {
        return m_Tracker->hasRedoSteps();
    }

    [[nodiscard]] std::string previousStepName() const override
    {
        return m_Tracker->getPreviousStepName();
    }

    [[nodiscard]] std::string nextStepName() const override
    {
        return m_Tracker->getNextStepName();
    }

    void showHistory() override
    {
        auto* window = new ImageHistoryControl(&m_View, m_Tracker);
        m_Windows.erase(std::remove_if(m_Windows.begin(), m_Windows.end(),
                                       [](const QPointer<ImageHistoryControl>& each) { return each.isNull(); }),
                        m_Windows.end());
        m_Windows.emplace_back(window);
        window->show();
    }

    void inheritLineage(const an::ui::IViewHistory& source) override
    {
        if (const auto* other = dynamic_cast<const ViewHistory*>(&source))
        {
            m_Tracker->setLineage(an::ui::history::inheritedLineage(*other->m_Tracker));
            // The tracker tells nobody of a lineage: the inherited steps are steps a strip shows.
            onImageHistoryUpdated();
        }
    }

    [[nodiscard]] an::ui::ViewHistorySteps steps() const override
    {
        an::ui::ViewHistorySteps listed;
        for (const LineageEntry& entry : m_Tracker->lineage().entries())
        {
            listed.steps.push_back({.name = entry.step_name, .description = entry.description, .inherited = true});
        }
        const std::size_t inherited = listed.steps.size();
        if (const ImageHistoryStepSPtr initial = m_Tracker->getInitialStep())
        {
            listed.steps.push_back({.name = initial->getStepName(), .description = initial->getStepDesc()});
        }
        for (const ImageHistoryStepSPtr& step : m_Tracker->getImageHistorySteps())
        {
            listed.steps.push_back({.name = step->getStepName(), .description = step->getStepDesc()});
        }
        listed.current = listed.steps.size() > inherited ? inherited + m_Tracker->getCurrentStep() : 0u;
        return listed;
    }

    bool jumpToStep(const std::size_t step) override
    {
        const std::size_t inherited = m_Tracker->lineage().entries().size();
        if (step < inherited)
        {
            return false;
        }
        const std::size_t own = step - inherited;
        return m_Tracker->canJumpToStep(own) && m_Tracker->jumpToStep(own);
    }

    [[nodiscard]] std::uint64_t addStepsListener(StepsListener listener) override
    {
        const std::uint64_t token = ++m_LastToken;
        m_StepsListeners.emplace_back(token, std::move(listener));
        return token;
    }

    void removeStepsListener(const std::uint64_t token) override
    {
        std::erase_if(m_StepsListeners, [token](const auto& each) { return each.first == token; });
    }

private:
    void onImageHistoryUpdated() override
    {
        // A listener may remove itself, or another, while it is told: each is looked up again before it is told.
        std::vector<std::uint64_t> tokens;
        tokens.reserve(m_StepsListeners.size());
        for (const auto& [token, listener] : m_StepsListeners)
        {
            tokens.push_back(token);
        }
        for (const std::uint64_t token : tokens)
        {
            const auto found = std::find_if(m_StepsListeners.begin(), m_StepsListeners.end(),
                                            [token](const auto& each) { return each.first == token; });
            if (found != m_StepsListeners.end() && found->second)
            {
                const StepsListener told = found->second;
                told();
            }
        }
    }

    ImageViewControl& m_View;
    ViewHistoryHost m_Host;
    std::shared_ptr<ImageHistoryTracker> m_Tracker;
    std::vector<QPointer<ImageHistoryControl>> m_Windows;
    std::vector<std::pair<std::uint64_t, StepsListener>> m_StepsListeners;
    std::uint64_t m_LastToken = 0u;
};

} // namespace

namespace an::ui
{

std::unique_ptr<IViewHistory> createViewHistory(::ImageViewControl& view)
{
    // A history keeps its steps in the session the view's images belong to; without one there is nothing to keep.
    auto* context = appContext();
    auto* provider = context != nullptr ? context->getEngineSessionProvider() : nullptr;
    if (provider == nullptr || !provider->engineSession().valid())
    {
        return nullptr;
    }
    return std::make_unique<ViewHistory>(view, provider->engineSession());
}

} // namespace an::ui
