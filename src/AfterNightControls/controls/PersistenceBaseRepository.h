// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

class PersistenceDbExecutor;

/**
 * @brief Lightweight base class for persistence repositories.
 *
 * Centralises the PersistenceDbExecutor reference so that all subclasses
 * route SQL work through the executor's dedicated DB thread.
 *
 * Subclasses inherit publicly and delegate the executor to this base:
 * @code
 *   class MyRepo : public PersistenceBaseRepository {
 *   public:
 *       explicit MyRepo(PersistenceDbExecutor& ex) : PersistenceBaseRepository(ex) {}
 *   };
 * @endcode
 */
class PersistenceBaseRepository
{
public:
    explicit PersistenceBaseRepository(PersistenceDbExecutor& executor) noexcept : m_executor(executor) {}

    virtual ~PersistenceBaseRepository() = default;

    PersistenceBaseRepository(const PersistenceBaseRepository&) = delete;
    PersistenceBaseRepository& operator=(const PersistenceBaseRepository&) = delete;
    PersistenceBaseRepository(PersistenceBaseRepository&&) = delete;
    PersistenceBaseRepository& operator=(PersistenceBaseRepository&&) = delete;

protected:
    PersistenceDbExecutor& m_executor;
};
