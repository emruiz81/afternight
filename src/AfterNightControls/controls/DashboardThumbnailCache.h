// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

/// @file DashboardThumbnailCache.h
/// In-memory LRU cache for decoded dashboard thumbnails, keyed by
/// (imagePath, tileSize).  Eviction is FIFO once the maximum entry count
/// is reached.
///
/// Thread-safety: this class is **not** thread-safe.  All accesses must
/// happen on the same thread (typically the GUI thread).

#include <QDir>
#include <QHash>
#include <QImage>
#include <QSize>
#include <QString>

#include <deque>

class AFTERNIGHTCONTROLS_API DashboardThumbnailCache final
{
public:
    /// A single cached decoded thumbnail.
    struct Entry
    {
        QImage displayImage;
        QSize sourceSize;
        qint64 lastModifiedMs = 0;
    };

    explicit DashboardThumbnailCache(int maxEntries = 256) : m_MaxEntries(maxEntries) {}

    /// Build a cache key from an image path and a tile size.
    [[nodiscard]] static QString cacheKey(const QString& imagePath, int tileSize)
    {
        return QStringLiteral("%1|%2").arg(QDir::cleanPath(imagePath), QString::number(tileSize));
    }

    /// Look up a cache entry.  Returns nullptr if not found.
    [[nodiscard]] const Entry* find(const QString& key) const
    {
        const auto it = m_Entries.constFind(key);
        return (it != m_Entries.constEnd()) ? &(*it) : nullptr;
    }

    /// Insert or update a cache entry, evicting the oldest when full.
    ///
    /// O(n) removal of the previous insertion position is acceptable here:
    /// the cache is capped at ~256 entries and insertions are throttled by the
    /// thumbnail-load timer.  A linked-hash map would give O(1) promote-to-back
    /// but adds complexity that is not justified at this scale.
    void insert(const QString& key, const QImage& displayImage, const QSize& sourceSize, qint64 lastModifiedMs)
    {
        if (displayImage.isNull() || key.isEmpty())
        {
            return;
        }

        Entry entry;
        entry.displayImage = displayImage;
        entry.sourceSize = sourceSize;
        entry.lastModifiedMs = lastModifiedMs;

        m_Entries.insert(key, entry);

        std::erase(m_InsertionOrder, key);
        m_InsertionOrder.push_back(key);
        trim();
    }

    /// Remove all entries.
    void clear()
    {
        m_Entries.clear();
        m_InsertionOrder.clear();
    }

private:
    void trim()
    {
        while (static_cast<int>(m_InsertionOrder.size()) > m_MaxEntries)
        {
            const QString& oldestKey = m_InsertionOrder.front();
            m_Entries.remove(oldestKey);
            m_InsertionOrder.pop_front();
        }
    }

    int m_MaxEntries;
    QHash<QString, Entry> m_Entries;
    std::deque<QString> m_InsertionOrder;
};
