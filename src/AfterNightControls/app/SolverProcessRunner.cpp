// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/SolverProcessRunner.h"

#include <QFile>
#include <QProcess>

#include <algorithm>

namespace an::ui
{

SolverProcessResult DefaultSolverProcessRunner::run(const QString& executable, const QStringList& arguments,
                                                    const QString& working_directory, const int timeout_seconds,
                                                    const CancellationToken& token)
{
    SolverProcessResult result;

    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    if (!working_directory.isEmpty())
    {
        process.setWorkingDirectory(working_directory);
    }

    process.start();
    if (!process.waitForStarted())
    {
        result.std_err = process.errorString().toLocal8Bit();
        return result;
    }

    const int timeout_ms = (std::min)((std::max)(1, timeout_seconds), 3600) * 1000;
    constexpr int poll_interval_ms = 250;
    int elapsed_ms = 0;
    while (!process.waitForFinished(poll_interval_ms))
    {
        if (token.isCancellationRequested())
        {
            process.kill();
            process.waitForFinished();
            result.was_cancelled = true;
            break;
        }

        elapsed_ms += poll_interval_ms;
        if (elapsed_ms >= timeout_ms)
        {
            process.kill();
            process.waitForFinished();
            result.timed_out = true;
            break;
        }
    }

    result.exit_code = process.exitCode();
    result.std_out = process.readAllStandardOutput();
    result.std_err = process.readAllStandardError();
    return result;
}

std::optional<QByteArray> DefaultSolverProcessRunner::readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }

    return file.readAll();
}

} // namespace an::ui
