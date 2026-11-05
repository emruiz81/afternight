// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QString>

#include <atomic>

namespace core
{

/**
 * @brief Generates a unique connection name for Qt SQL database connections.
 *
 * Each invocation returns a unique name of the form "prefix_N" where N is an
 * incrementing counter. Thread-safe due to use of std::atomic.
 *
 * @param prefix The prefix for the connection name.
 * @return A unique connection name string.
 */
inline QString nextConnectionName(const QString& prefix)
{
    static std::atomic<int> counter{0};
    return QStringLiteral("%1_%2").arg(prefix).arg(counter.fetch_add(1));
}

} // namespace core
