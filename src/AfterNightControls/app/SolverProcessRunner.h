// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "app/CancellationToken.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

namespace an::ui
{

struct SolverProcessResult
{
    int exit_code = -1;
    QByteArray std_out;
    QByteArray std_err;
    bool timed_out = false;
    bool was_cancelled = false;
};

class SolverProcessRunner
{
public:
    virtual ~SolverProcessRunner() = default;

    [[nodiscard]] virtual SolverProcessResult run(const QString& executable, const QStringList& arguments,
                                                  const QString& working_directory, int timeout_seconds,
                                                  const CancellationToken& token) = 0;

    [[nodiscard]] virtual std::optional<QByteArray> readFile(const QString& path) = 0;
};

class DefaultSolverProcessRunner final : public SolverProcessRunner
{
public:
    [[nodiscard]] SolverProcessResult run(const QString& executable, const QStringList& arguments,
                                          const QString& working_directory, int timeout_seconds,
                                          const CancellationToken& token) override;

    [[nodiscard]] std::optional<QByteArray> readFile(const QString& path) override;
};

} // namespace an::ui
