// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QSet>
#include <QSize>
#include <QThreadPool>
#include <verdigris/wobjectdefs.h>

class RunHistoryThumbnailCache : public QObject
{
    W_OBJECT(RunHistoryThumbnailCache)

public:
    explicit RunHistoryThumbnailCache(QObject* parent = nullptr);

    QImage cachedThumbnail(const QString& path) const;
    void requestThumbnail(const QString& path, const QSize& size);

    void storeThumbnail(const QString& path, const QImage& image);

    void thumbnailReady(const QString& path) W_SIGNAL(thumbnailReady, path);

private:
    mutable QMutex m_mutex;
    QHash<QString, QImage> m_cache;
    QHash<QString, qint64> m_failedUntilMs;
    QSet<QString> m_inFlight;
    QThreadPool m_pool;
};
