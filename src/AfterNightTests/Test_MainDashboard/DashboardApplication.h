// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The main window the dashboard is made in, as far as the dashboard's cases need one: the application's recent files,
/// its settings and the thumbnails of its recent files - kept in the Controls library's own repositories over an
/// in-memory database - and the files dropped on it. The suite's entry point makes one before any case runs, and the
/// cases reach it through `instance()`.
#pragma once

#include "controls/GlobalSettingsPersistence.h"
#include "controls/ThumbnailPersistence.h"
#include "fixtures/InMemoryDatabase.h"

#include <QString>
#include <QStringList>
#include <QWidget>

#include <deque>
#include <string>

/// The dashboard's application double.
class DashboardApplication final : public QWidget
{
public:
    DashboardApplication() : m_settings(m_database.executor()), m_thumbnails(m_database.executor())
    {
        m_ready = m_database.isOpen() && m_settings.initializeTable() && m_thumbnails.initializeTable();
        s_instance = this;
    }

    ~DashboardApplication() override
    {
        s_instance = nullptr;
    }

    DashboardApplication(const DashboardApplication&) = delete;
    DashboardApplication& operator=(const DashboardApplication&) = delete;

    /// The one the entry point made, or null before it was made and after it is gone.
    [[nodiscard]] static DashboardApplication* instance() noexcept
    {
        return s_instance;
    }

    /// Whether the database opened and both repositories made their tables.
    [[nodiscard]] bool ready() const noexcept
    {
        return m_ready;
    }

    /// The settings the dashboard reads and writes through the application context.
    [[nodiscard]] GlobalSettingsPersistence* getGlobalSettingsPersistence() noexcept
    {
        return &m_settings;
    }

    /// The thumbnail of each recent file, by the file's path.
    [[nodiscard]] ThumbnailPersistence* getThumbnailPersistence() noexcept
    {
        return &m_thumbnails;
    }

    /// The recent files, most recent first.
    [[nodiscard]] std::deque<std::string> getRecentFilesList() const
    {
        return m_recentFiles;
    }

    /// Replaces the recent files.
    void setRecentFiles(const std::deque<std::string>& recentFiles)
    {
        m_recentFiles = recentFiles;
    }

    /// Records the thumbnail of a recent file.
    void setThumbnailPath(const QString& filePath, const QString& thumbnailPath)
    {
        (void)m_thumbnails.upsert(filePath, thumbnailPath);
    }

    /// Keeps the files dropped on the application, in order.
    void openDroppedFiles(const QStringList& files)
    {
        m_droppedFiles += files;
    }

    /// The files dropped on the application, in order.
    [[nodiscard]] const QStringList& droppedFiles() const noexcept
    {
        return m_droppedFiles;
    }

    /// Forgets the recent files, the thumbnails, the settings and the dropped files, so each case starts from none.
    void resetForTest()
    {
        m_recentFiles.clear();
        (void)m_thumbnails.clearAll();
        (void)m_settings.clearAll();
        m_droppedFiles.clear();
    }

private:
    inline static DashboardApplication* s_instance = nullptr;

    InMemoryDatabase m_database;
    GlobalSettingsPersistence m_settings;
    ThumbnailPersistence m_thumbnails;
    std::deque<std::string> m_recentFiles;
    QStringList m_droppedFiles;
    bool m_ready = false;
};
