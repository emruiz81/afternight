// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "ImageViewControlListener.h"
#include "app/AfterNightAppListener.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ProcessExecution.h"
#include "controls/ProgressDialogControl.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "session/ExecutionScope.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Core/Status.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QThreadPool>
#include <QtWidgets/QDialog>

#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

class ImageViewControl;
class QComboBox;

/// A process window over one image view: the view it targets, the channel or colour component it runs on, the messages
/// it shows and the runs it sends to workers.
///
/// A process is attached to its target view while it is open, and the view closes it when the view closes. Everything
/// here runs on the GUI thread except the bodies a run calls on a worker - `executeProcessIteration` and what a
/// subclass runs - which read only what the run snapshotted: its parameters, its channel, its source and destination.
/// A run's completion publishes on the GUI thread, and a window closed while a run is on its way is hidden and closes
/// when the last run lands; nothing waits on the GUI thread.
class AFTERNIGHTCONTROLS_API ImageProcessControl : public TopLevelProcessBaseControl,
                                                   public ImageViewControlListener,
                                                   public AfterNightAppListener,
                                                   public an::ui::IViewAttachment
{
public:
    /// A process's parameters, snapshotted on the GUI thread before a run starts.
    struct ProcessExecutionParams
    {
        virtual ~ProcessExecutionParams() = default;
    };

    using ProcessExecutionParamsPtr = std::shared_ptr<const ProcessExecutionParams>;

    ImageProcessControl(ImageViewControl* control, const std::string& name);

    virtual void changeTargetViewControl(ImageViewControl* control);

    // View control listener callbacks
    void onImageRenamed(ImageViewControl* ctrl) override;

    void onImageLoaded(ImageViewControl* ctrl) override;

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    void onImageViewControlDestroyed(ImageViewControl* ctrl) override;

    // app listener callbacks
    void onViewControlCreated(const ImageViewControl* ctrl) override;

    void onViewControlDestroyed(const ImageViewControl* ctrl) override;

    void onViewControlUpdated(const ImageViewControl* ctrl) override;

    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;

    virtual bool isRTPreviewProcess() const;

    // The view's attachment

    /// The process's name, which is unique among the processes attached to one view.
    [[nodiscard]] std::string attachmentName() const override;

    /// Whether the process needs its view to itself; no process does unless it says so.
    [[nodiscard]] bool requiresExclusiveAccess() const override;

    /// Whether the process shows a real-time preview of its view.
    [[nodiscard]] bool showsRealtimePreview() const override;

    /// Whether the process's preview needs its view to itself among previews; a process without a preview never does.
    [[nodiscard]] bool requiresExclusivePreviewAccess() const override;

    /// Turns the process's real-time preview off; a process without one has nothing to turn off.
    void disableRealtimePreview() override;

    /// The view is closing: the process lets it go and closes.
    void detachAndClose() override;

    [[nodiscard]] std::string getProcessName() const;

    /// The channel or colour component the selector names now: the scope its item carries, and the whole image for a
    /// process without a selector or a grey image. Read on the GUI thread, when a run is issued.
    [[nodiscard]] an::ChannelScope selectedChannelScope() const;

    /// Whether no run of this process is on its way.
    [[nodiscard]] bool isRunIdle() const noexcept;

    /// Runs the continuation on the GUI thread once no run of this process is on its way: at once when none is, else
    /// when the last one lands. A continuation that starts a run of its own holds the ones after it until that run
    /// has landed too. The GUI thread waits for a run this way, never blocked.
    void whenRunIdle(std::function<void()> continuation);

    /// Stops the run on its way as closing the window does - its progress cancelled, nothing it ends with shown - and
    /// waits for nothing; the window's stop token is requested before it is called.
    void onStopWorkInFlight() override;

    void notifyErrorMessage(const QString& title, const QString& text);
    void notifyErrorMessageAsync(const QString& title, const QString& text);

    void notifyWarnMessage(const QString& title, const QString& text);
    void notifyWarnMessageAsync(const QString& title, const QString& text);

    void notifyInfoMessage(const QString& title, const QString& text);
    void notifyInfoMessageAsync(const QString& title, const QString& text);

    void notifyCloseEventAsync();

    void notifyCustomProcessEvent(const std::string& msg);
    void notifyCustomProcessEventAsync(const std::string& msg);

    void notifyCustomProcessEventID(const int id);
    void notifyCustomProcessEventIDAsync(const int id);

    void notifyCustomProcessCodeEvent(const std::function<void(ImageProcessControl* instance)>& func);
    void notifyCustomProcessCodeEventAsync(const std::function<void(ImageProcessControl* instance)>& func);

    // Abstract helper method with an async option for use in inherited classes
    virtual void resetSettings();
    void resetSettingsAsync();
    virtual void onCustomProcessEventNotified(const std::string& msg) {}
    virtual void onCustomProcessEventIDNotified(const int id) {}

    // Called when the application theme changes to refresh icons, styles, etc.
    void refreshTheme() override;

    ~ImageProcessControl() override;

protected:
    void on_targetImageSelector_currentTextChanged(const QString& text);
    void on_channelSelector_currentTextChanged(const QString& text);

protected:
    enum class vc_update_op
    {
        added = 0,
        removed,
        renamed
    };

    /// Closes the window, or hides it and closes it when its last run lands.
    void closeEvent(QCloseEvent* event) override;

    virtual void on_targetImageSelector_currentTextChangedInternal(const QString& text) {}
    virtual void on_channelSelector_currentTextChangedInternal(const QString& text) {}

    [[nodiscard]] std::string getWindowTitle() const;

    void updateWindowTitle();

    /// Runs the process over one plane a channel scope hands over, on the run's worker: a stored channel, a component
    /// of another encoding, or the whole image. The default leaves the output as the input it starts as.
    ///
    /// @param input what to read, borrowed for the call.
    /// @param output what to write, borrowed for the call; it starts as a copy of the input.
    /// @param run what the run snapshotted.
    /// @param params the parameters it snapshotted.
    /// @param scope the run's admitted execution, whose progress sink the process reports to.
    /// @return success, or why the plane was not processed.
    virtual an::Status executeProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                               const an::ui::ProcessRun& run, const ProcessExecutionParams* params,
                                               an::ui::ExecutionScope& scope);

    /// Runs `executeProcessIteration` over the run's channel of the source and recomposes the destination with what it
    /// wrote, through the engine's channel scope. The destination keeps the source's metadata.
    ///
    /// @param source what the run reads.
    /// @param destination what the run writes, still described as the source.
    /// @param run what the run snapshotted, its channel among it.
    /// @param params the parameters it snapshotted.
    /// @param scope the run's admitted execution.
    /// @return success, or what the scope or the body refused.
    [[nodiscard]] an::Status executeProcessUsingChannels(const an::ui::ProcessSource& source,
                                                         an::ui::ProcessDestination& destination,
                                                         const an::ui::ProcessRun& run,
                                                         const ProcessExecutionParams* params,
                                                         an::ui::ExecutionScope& scope);

    virtual void updateNeeded() {}

    // Inherit for custom selection of supported view controls
    virtual bool isViewControlSupported(const ImageViewControl* ctrl)
    {
        return true;
    }

    void refreshListOfSupportedViewControls();

    void refreshListOfSupportedChannels();

    // Inherit for triggering custom actions on VC list updates
    virtual void listOfSupportedViewControlsUpdated(const ImageViewControl* affectedCtrl, vc_update_op op) {}

    void initializeTargetViewControlComboBox(QComboBox* qtList);
    void initializeListOfSupportedChannelslComboBox(QComboBox* qtList);

    // Runs in main thread
    virtual void enableProcessControls(bool enabled);
    virtual void enableProcessControlsImpl(bool enabled) {}

    void registerNewProgressDialog(ProgressDialogControl* dialog);
    void unregisterCurrentProgressDialog();
    void cancelCurrentProgressDialog();

    /// The progress dialog of the run on its way, or null.
    [[nodiscard]] ProgressDialogControl* currentProgressDialog() const;

    /// The engine session the application lends, or null when none is lent.
    [[nodiscard]] const an::consumer::EngineSession* engineSession() const;

    /// Counts a run in flight from now until the last copy of the token is let go, on any thread. A window closed while
    /// a run is in flight is hidden, and closes once the last one has ended.
    ///
    /// @return the token; whatever carries the run's work and its completion holds a copy.
    [[nodiscard]] std::shared_ptr<void> beginRunInFlight();

    /// Runs `work` on a worker and `done(result)` on the GUI thread, while the window lives. The work is a run in
    /// flight from now until its completion has run or been dropped.
    ///
    /// `work` reads only what it captured - never a widget - and returns a value; `done` runs on the GUI thread and is
    /// dropped when the window is gone. Both are copied into the worker and must be copyable. A work of a higher
    /// `priority` is started before every queued one of a lower, so a preview's frame never waits behind a thumbnail.
    /// A work that throws is logged under the process's name: an exception never leaves the worker, where it would end
    /// the application. Its completion then runs with what `recovered(what)` returns, when one is given - a failure the
    /// window says and ends the run with, so nothing the run holds stays held - and is dropped otherwise.
    template <typename WorkT, typename DoneT, typename RecoveredT = std::nullptr_t>
    void runThenPublish(WorkT work, DoneT done, int priority = 0, RecoveredT recovered = nullptr)
    {
        std::shared_ptr<void> inFlight = beginRunInFlight();
        QPointer<ImageProcessControl> guard(this);
        QThreadPool::globalInstance()->start(
            [guard, inFlight, processName = m_ProcessName, work = std::move(work), done = std::move(done),
             recovered = std::move(recovered)]() mutable {
                using Result = decltype(work());
                std::shared_ptr<Result> result;
                const auto threw = [&](const char* what) {
                    constexpr bool recovers = !std::is_same_v<RecoveredT, std::nullptr_t>;
                    logWorkThatThrew(processName, what, recovers);
                    if constexpr (recovers)
                    {
                        try
                        {
                            result = std::make_shared<Result>(recovered(what));
                        }
                        catch (...)
                        {
                            result.reset();
                        }
                    }
                };
                try
                {
                    result = std::make_shared<Result>(work());
                }
                catch (const std::exception& error)
                {
                    threw(error.what());
                }
                catch (...)
                {
                    threw("an exception of no standard type");
                }
                if (!result)
                {
                    return;
                }
                QCoreApplication* application = QCoreApplication::instance();
                if (application == nullptr)
                {
                    return;
                }
                QMetaObject::invokeMethod(
                    application,
                    [guard, inFlight, result, done]() mutable {
                        if (guard)
                        {
                            done(std::move(*result));
                        }
                    },
                    Qt::QueuedConnection);
            },
            priority);
    }

private:
    void endRun();

    /// Logs a work of `runThenPublish` that threw, whose completion runs with a failure when `recovered`, and is
    /// dropped otherwise. Any thread.
    static void logWorkThatThrew(const std::string& processName, const char* what, bool recovered) noexcept;

    struct Impl;
    std::unique_ptr<Impl> m_impl;

protected:
    std::string& m_ProcessName;

    /// Set while an Apply, a new image, a snapshot or a sequence run is on its way; a preview never sets it.
    std::atomic_bool& m_ExecutionInProgress;
    std::atomic_bool& m_ProcessCloseRequested;

    bool& m_SupportTargetViewControlSelection;
    std::set<std::string>& m_ListOfSupportedViewControls;
    QComboBox*& m_UiListOfSupportedViewControls;
    bool& m_IgnoreLastUiListTextChangedEvent;

    bool& m_SupportPerChannelProcessing;
    QComboBox*& m_UiListOfSupportedChannels;
    bool& m_IgnoreLastUiListOfSupportedChannelsEvent;
};
