// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "RunHistoryThumbnailCache.h"

#include <QDateTime>
#include <QImageReader>
#include <QRunnable>
#include <QMetaObject>

#include <verdigris/wobjectimpl.h>

namespace
{
[[nodiscard]] QSize scaledToFit(const QSize& sourceSize, const QSize& bounds)
{
    if (!sourceSize.isValid() || sourceSize.isEmpty() || !bounds.isValid() || bounds.isEmpty())
    {
        return {};
    }
    return sourceSize.scaled(bounds, Qt::KeepAspectRatio);
}

class ThumbnailLoadTask : public QRunnable
{
public:
    ThumbnailLoadTask(RunHistoryThumbnailCache* cache, QString path, QSize size)
        : m_cache(cache), m_path(std::move(path)), m_size(size)
    {
    }

    void run() override
    {
        QImageReader reader(m_path);
        reader.setAutoTransform(true);
        if (m_size.isValid() && !m_size.isEmpty())
        {
            const QSize readerScaledSize = scaledToFit(reader.size(), m_size);
            if (readerScaledSize.isValid() && !readerScaledSize.isEmpty())
            {
                reader.setScaledSize(readerScaledSize);
            }
        }

        QImage image = reader.read();
        if (!image.isNull() && m_size.isValid() && !m_size.isEmpty() &&
            (image.width() > m_size.width() || image.height() > m_size.height()))
        {
            image = image.scaled(m_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        QMetaObject::invokeMethod(
            m_cache, [cache = m_cache, path = m_path, image]() { cache->storeThumbnail(path, image); },
            Qt::QueuedConnection);
    }

private:
    RunHistoryThumbnailCache* m_cache = nullptr;
    QString m_path;
    QSize m_size;
};
} // namespace

W_OBJECT_IMPL(RunHistoryThumbnailCache)

RunHistoryThumbnailCache::RunHistoryThumbnailCache(QObject* parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(2);
}

QImage RunHistoryThumbnailCache::cachedThumbnail(const QString& path) const
{
    QMutexLocker locker(&m_mutex);
    auto it = m_cache.find(path);
    if (it == m_cache.end())
    {
        return QImage();
    }
    return it.value();
}

void RunHistoryThumbnailCache::requestThumbnail(const QString& path, const QSize& size)
{
    if (path.trimmed().isEmpty())
    {
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 failedUntilMs = m_failedUntilMs.value(path, 0);
        if (failedUntilMs > 0 && nowMs < failedUntilMs)
        {
            return;
        }
        if (m_cache.contains(path) || m_inFlight.contains(path))
        {
            return;
        }
        m_inFlight.insert(path);
    }

    auto* task = new ThumbnailLoadTask(this, path, size);
    task->setAutoDelete(true);
    m_pool.start(task);
}

void RunHistoryThumbnailCache::storeThumbnail(const QString& path, const QImage& image)
{
    {
        QMutexLocker locker(&m_mutex);
        if (image.isNull())
        {
            constexpr qint64 retryDelayMs = 2000;
            m_failedUntilMs.insert(path, QDateTime::currentMSecsSinceEpoch() + retryDelayMs);
            m_inFlight.remove(path);
            return;
        }
        m_failedUntilMs.remove(path);
        m_cache.insert(path, image);
        m_inFlight.remove(path);
    }

    emit thumbnailReady(path);
}
