// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// An extension's process drawn by the application while the extension's code runs in its host.
///
/// `ExtensionHostedProcess` is the extension's process window as the application draws it: the panel built from the
/// parameters the host tells (`ui.describe`), every part of the window an extension can ask for - the target, the view
/// selectors, the manual mask, the tool's configuration, the histograms - and a process's shape, so that a shell can
/// show it as a page like any other. What a person does in the panel is told to the host (`ui.event`) and what an
/// action answers lands in it (`ui.patch`). Its preview is the framework's: each frame's source is lent from the
/// application's side of the seam through a pool of buffers, the host's `execute_preview` writes the frame's output,
/// and the result is shown only when the frame's ticket commits. Its Apply is the framework's too: the target is
/// exported once, the host's `execute` writes the result, and the result is the view's edit, recorded in its history,
/// under the target's lock for the run's length. Nothing of the extension runs in the application's process, and the
/// GUI thread waits for none of it.
///
/// The process names no extension: what it shows is what the host tells, and its name is the process's.

#include "AfterNightControlsExport.h"

#include "controls/ExtensionNativeProcessWindow.h"

#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <verdigris/wobjectdefs.h>

namespace an::ui::extensions
{

class ExtensionHostedSession;
class PreviewFrameTrace;
struct ExtensionSessionEnd;

class AFTERNIGHTCONTROLS_API ExtensionHostedProcess final : public ExtensionNativeProcessWindow
{
    W_OBJECT(ExtensionHostedProcess)

public:
    /// The widest panel an extension can ask for (`controls_panel_width`) that the standard column holds.
    static constexpr int kStandardColumnWidth = 360;

    /// What the process is made from, before its host has said anything.
    struct Setup
    {
        /// The process's name, the extension's process's display name.
        std::string displayName;

        /// Whether the extension's manifest declares a preview: a preview page then, and a mirror page otherwise.
        bool previews = false;

        /// What an interactive frame may run the extension over, as its manifest declares it.
        an::ui::PreviewScaling previewScaling = an::ui::PreviewScaling::FullOnly;
        std::uint32_t previewRegionMargin = 0u;

        /// The parameters kept from an earlier start - what the application read of the extension when it found it -
        /// drawn at once; empty draws the panel only once the host tells it.
        QJsonArray keptParams;

        /// Whether its manifest keeps it open after an Apply (`keep_open`): it stays, its parameters put back to their
        /// defaults as every process's are; otherwise it closes once its result is committed, as its window always did.
        bool keepOpen = false;
    };

    /// What the application does for the process.
    struct Hooks
    {
        /// Ends the process's session: the process closed.
        std::function<void()> closeSession;

        /// Starts the process's session again, after its host failed: false, and why, when it could not.
        std::function<bool(std::string* error)> restartSession;

        /// The open images, as an extension lists them, for the target and the view selectors.
        std::function<std::vector<std::map<std::string, QVariant>>()> listViews;

        /// The person chose another target in the panel: the session is bound to it.
        std::function<void(const std::string& viewName)> targetChosen;

        /// The extension's settings, read and written by the panel's own parts - the tool's configuration - and its
        /// settings page, opened from them.
        SettingsGetCallback settingGet;
        SettingsSetCallback settingSet;
        OpenSettingsCallback openSettings;
    };

    /// @param target the image the process runs on; the application's own view of it, never taken into the process.
    /// @param setup what the process is made from.
    ExtensionHostedProcess(ImageViewControl* target, Setup setup);
    ~ExtensionHostedProcess() override;

    void setHooks(Hooks hooks);

    /// What the process shares with its host's session, which binds it when the session starts.
    [[nodiscard]] const std::shared_ptr<ExtensionHostedSession>& hostedSession() const noexcept;

    /// Whether the host is ready: its runtime started, its requests sent.
    [[nodiscard]] bool hostReady() const noexcept;

    /// Whether the host failed - crashed, stopped answering, refused what it was sent - and the process waits to be
    /// started again or closed: its controls disabled, its banner saying so with Restart and Close, its run failed.
    [[nodiscard]] bool hostFailed() const noexcept;

    /// Whether the process waits for its host's start: its body holds a row that says so, and its controls - the
    /// parameters kept from an earlier start among them - are disabled until the host is ready.
    [[nodiscard]] bool isStarting() const noexcept;

    /// What the starting row says while it shows: "Starting" and the process's name, what the preparation of its
    /// environment does when one runs, then how far the host has come, a line a step; nothing once the host is ready or
    /// failed.
    [[nodiscard]] QStringList startingText() const;

    /// What the preparation of the extension's environment does now - a first start's - said in the starting row until
    /// its host starts.
    void setPreparation(const QString& line);

    /// The parameters the host told last, or the ones kept from an earlier start until it has.
    [[nodiscard]] const QJsonArray& drawnParams() const noexcept;

    /// A preview page when the extension previews, a mirror page otherwise: what its manifest declares, in no
    /// registration.
    [[nodiscard]] an::ui::ProcessSurface processSurface() const override;

    /// The wide column for an extension whose window asks for a panel wider than the standard column.
    [[nodiscard]] an::ui::ProcessColumnWidth processColumnWidth() const override;

protected:
    [[nodiscard]] an::Status executeProcessImpl(const an::ui::ProcessSource& source,
                                                an::ui::ProcessDestination& destination, const an::ui::ProcessRun& run,
                                                const ProcessExecutionParams* params,
                                                an::ui::ExecutionScope& scope) override;
    void resetSettings() override;
    void closeEvent(QCloseEvent* event) override;
    void externalToolRecordMayHaveChanged() override;

    /// The step an Apply records, as the extension's window names its own: the process's name, and the parameters used.
    [[nodiscard]] an::ui::ProcessStep describeProcessStep(const ProcessExecutionParams* params) const override;
    void getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                     std::string& newImageName) override;

    /// The page's header says the process runs in its own process.
    void pageLayoutMade() override;

    /// An Apply waits for a host that is ready.
    [[nodiscard]] bool acceptsRuns() const override;

    /// A run that ended while the host starts or after it failed leaves the process as its host stands.
    void executeProcessEndedImpl(const RTPreviewMode previewMode, const bool sequenceMode) override;

private:
    /// Shows the process as its host stands: starting, ready or failed.
    void applyHostState();
    void addStartingLine(const QString& line);
    void restartHost();
    void onHostReady();
    void onDescribed(const QJsonArray& params, const QString& refusal);
    void onActionAnswered(const QString& actionId, const QCborMap& values, const QString& error);
    void onSessionEnded(const ExtensionSessionEnd& end);
    void listOpenImages();

    /// The Apply: the target exported once, the host's execute run over it, its result the destination.
    [[nodiscard]] an::Status applyThroughHost(const an::ui::ProcessSource& source,
                                              an::ui::ProcessDestination& destination,
                                              const ProcessExecutionParams* params, an::ui::ExecutionScope& scope);

    Setup m_setup;
    Hooks m_hooks;
    QJsonArray m_drawnParams;
    std::shared_ptr<ExtensionHostedSession> m_hosted;
    std::shared_ptr<PreviewFrameTrace> m_trace;
    /// The row the body holds while the host starts.
    QWidget* m_startingRow = nullptr;
    /// How far the host has come in its start, a line a step.
    QStringList m_startingLines;
    /// What the preparation of the extension's environment does now; empty once its host started.
    QString m_preparation;
    /// Why the host failed, as the banner says it.
    QString m_failure;
    std::function<void(QCborMap)> m_actionAnswer;
    /// The tool's record the host was told last; none before it was told one.
    std::optional<QCborMap> m_toldToolRecord;
    bool m_hostReady = false;
    bool m_hostFailed = false;
    bool m_sessionEnded = false;
};

} // namespace an::ui::extensions
