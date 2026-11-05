// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The plate solve: the solvers tried in the configured order - the internal Gaia solver, ASTAP, a local astrometry.net
/// and its web service - each asked with the image's hints, and the first solution that satisfies the request.
///
/// The solve is the engine's wherever an engine operation exists: the hints are read from the image's cards
/// (`readPointingHint`, `readOpticalHints`, `resolvePlateScaleHint`), an embedded or answered solution through the card
/// bridge, the internal solver is the engine's plate solver over a regional catalog of the installed Gaia catalog
/// (`GaiaCatalogSource`), and the figures a window shows are the solution's own. The external solvers keep their
/// runners and their arguments.
///
/// `solve` runs on the caller's thread and is for a worker; `solveAsync` runs it on the thread pool and reports on the
/// GUI thread. Neither ever waits for the other, and destroying the service while a solve runs stops it without
/// waiting.
#pragma once

#include "AfterNightControlsExport.h"

#include "app/CancellationToken.h"
#include "app/PlateSolveTypes.h"
#include "app/SolverProcessRunner.h"

#include <AfterNightEngine/Core/Status.h>

#include <QObject>
#include <QUrl>
#include <verdigris/wobjectdefs.h>

#include <chrono>
#include <functional>
#include <memory>

namespace an::ui
{

/// The astrometry.net web service's address.
inline constexpr const char kAstrometryWebDefaultBaseUrl[] = "https://nova.astrometry.net";

/// How long the astrometry.net web solver waits between two polls of a submission or a job.
inline constexpr std::chrono::milliseconds kAstrometryWebDefaultPollInterval{5000};

class AFTERNIGHTCONTROLS_API PlateSolveService : public QObject
{
    W_OBJECT(PlateSolveService)

public:
    /// What an asynchronous solve runs on its worker before it solves - the plate-solve window writes the image it
    /// shows to the request's file - answering why it could not.
    using PrepareInput = std::function<an::Status()>;

    explicit PlateSolveService(QObject* parent = nullptr);
    explicit PlateSolveService(std::unique_ptr<SolverProcessRunner> runner, QObject* parent = nullptr);
    ~PlateSolveService() override;

    /// Solves on the calling thread, which must be a worker: the solve reads files and waits for solvers.
    [[nodiscard]] PlateSolveResult solve(const PlateSolveRequest& request, const PlateSolveSettings& settings,
                                         const CancellationToken& token);

    /// Solves on the thread pool and reports `solveFinished` on the GUI thread. A solve asked for while one runs is
    /// ignored.
    ///
    /// @param request what to solve.
    /// @param settings how.
    /// @param prepare what the worker runs first; may be empty.
    void solveAsync(const PlateSolveRequest& request, const PlateSolveSettings& settings, PrepareInput prepare = {});

    /// Stops the asynchronous solve, which reports itself cancelled.
    void cancel();

    /// Whether an asynchronous solve runs.
    [[nodiscard]] bool isSolving() const noexcept;

    /// Points the astrometry.net web solver at another service, for a test that answers it locally.
    void setAstrometryWebEndpoint(const QUrl& baseUrl, std::chrono::milliseconds pollInterval);

    void solveFinished(PlateSolveResult result) W_SIGNAL(solveFinished, result);

    void solveProgress(QString status) W_SIGNAL(solveProgress, status);

private:
    struct Core;
    struct Run;

    std::shared_ptr<Core> m_core;
    std::shared_ptr<Run> m_run;
};

} // namespace an::ui
