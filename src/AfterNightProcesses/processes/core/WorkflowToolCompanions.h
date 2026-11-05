// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the standalone workflow tools reach of the integration workflow without including it: its registration
/// settings dialog, its local normalization settings dialog, its blink comparator and its comet registration.
///
/// Each is declared here and defined beside the integration unit it opens or runs, so a tool's window compiles without
/// the integration's model, windows or stages, and a suite of the tools stands each in. Every call is asked on the GUI
/// thread and never waits on it: a dialog is shown, not run, and answers through its callback when the person accepts
/// it; the comet registration runs on the worker that asks for it.
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/core/RegistrationMetricsPersistenceTypes.h"
#include "processes/core/StandaloneRegistrationPersistence.h"
#include "processes/core/WorkflowSettingsRecords.h"

#include <AfterNightEngine/Core/Result.h>

#include <QString>
#include <QStringList>

#include <functional>
#include <stop_token>
#include <unordered_set>
#include <vector>

class QWidget;

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace an::ui::processes
{

/// What the registration settings dialog is opened over.
struct StandaloneRegistrationSettingsRequest
{
    /// The settings the dialog shows.
    RegistrationSettings settings;

    /// The frames the dialog's reference and comet tools pick from.
    QStringList frames;

    /// Answers the dialog's Auto-Assign: given the settings the dialog shows, it finds the best reference on a worker
    /// and calls `chosen` on the GUI thread with its file, or with nothing and the reason it found none.
    std::function<void(const RegistrationSettings& shown,
                       std::function<void(QString reference, QString reason)> chosen)>
        autoAssign;
};

/// Shows the integration's registration settings dialog over a standalone registration, without its manual-points
/// method. Defined beside the dialog.
///
/// @param parent the window the dialog belongs to.
/// @param request what it shows.
/// @param accepted called on the GUI thread with the settings the dialog states when the person accepts it.
AFTERNIGHTPROCESSES_API void
openStandaloneRegistrationSettings(QWidget* parent, StandaloneRegistrationSettingsRequest request,
                                   std::function<void(const RegistrationSettings&)> accepted);

/// Shows the integration's local normalization settings dialog without what only the integration's analysis reads -
/// the reference strategy, the evaluation, the scale evaluation method with its PSF settings and the analysis filters -
/// since the standalone stack fits each frame against its reference frame. Defined beside the dialog.
///
/// @param parent the window the dialog belongs to.
/// @param settings what it shows.
/// @param accepted called on the GUI thread with the settings the dialog states when the person accepts it.
AFTERNIGHTPROCESSES_API void
openLocalNormalizationSettings(QWidget* parent, const LocalNormalizationSettings& settings,
                               std::function<void(const LocalNormalizationSettings&)> accepted);

/// Shows the integration's blink comparator over frames. Defined beside the comparator.
///
/// @param parent the window the comparator belongs to.
/// @param frames the frames it compares, at least two.
/// @param closed called on the GUI thread, when the comparator closes, with the frames the person excluded in it.
AFTERNIGHTPROCESSES_API void openStandaloneBlinkComparator(QWidget* parent, const QStringList& frames,
                                                           std::function<void(const QStringList& excluded)> closed);

/// What a standalone comet registration registers.
struct StandaloneCometRegistrationRequest
{
    /// The frames, as the integration's import reads them.
    std::vector<FrameInfo> frames;

    /// The registration's settings: its comet tracking, its reference, its interpolation and its demosaic.
    RegistrationSettings settings;

    /// Where the registration is recorded and its comet-aligned outputs written.
    StandaloneRegistrationPersistenceContext context;

    /// The frames left out of the registration.
    std::unordered_set<QString> ignoredFrameIds;
};

/// What a standalone comet registration recorded.
struct StandaloneCometRegistrationOutcome
{
    /// The frames as the registration database now holds them.
    RegisteredFrameSet registered;

    /// The comet run the outputs belong to.
    QString cometRunId;

    /// How many comet-aligned outputs were written.
    int alignedOutputs = 0;
};

/// Registers frames on a comet: fits its track over the observations the settings hold, records the run, its
/// transforms and its output plane in the registration database, writes each frame aligned on the comet and activates
/// the run for stacking. Runs on the calling worker. Defined beside the integration's comet registration products.
///
/// @param session the process's session.
/// @param request what to register.
/// @param stop stops the registration.
/// @param progress told the share done, from 0 to 100, and what is being done; may be empty.
/// @return what was recorded, or why the registration failed or stopped.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<StandaloneCometRegistrationOutcome>
runStandaloneCometRegistration(const an::consumer::EngineSession& session,
                               const StandaloneCometRegistrationRequest& request, std::stop_token stop,
                               const std::function<void(int percent, const QString& text)>& progress);

} // namespace an::ui::processes
