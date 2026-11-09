// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/FeaturedFilePresenter.h"
#include "controls/DashboardFileFacts.h"

#include "controls/DashboardThumbnailCache.h"
#include "controls/DashboardUtils.h"

#include <AfterNightPlatform/Logging.h>

#include <QDateTime>
#include <QDir>
#include <QImageReader>
#include <QLocale>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QThreadPool>
#include <verdigris/wobjectimpl.h>

#include <algorithm>

W_OBJECT_IMPL(an::ui::FeaturedFilePresenter)

namespace an::ui
{

FeaturedFilePresenter::FeaturedFilePresenter(QObject* parent) : QObject(parent) {}

void FeaturedFilePresenter::setRecentFilePaths(const std::deque<QString>& recentFilePaths)
{
    m_RecentFilePaths.clear();
    m_NormalizedRecentSet.clear();
    m_NormalizedRecentSet.reserve(static_cast<qsizetype>(recentFilePaths.size()));

    for (const QString& rawPath : recentFilePaths)
    {
        const QString normalizedPath = DashboardUtils::normalizeRecentFilePath(rawPath);
        if (!normalizedPath.isEmpty())
        {
            m_RecentFilePaths.push_back(normalizedPath);
            m_NormalizedRecentSet.insert(normalizedPath);
        }
    }
}

void FeaturedFilePresenter::setPinnedHeroFilePath(const QString& pinnedHeroFilePath)
{
    m_PinnedHeroFilePath = DashboardUtils::normalizeRecentFilePath(pinnedHeroFilePath);
}

QString FeaturedFilePresenter::pinnedHeroFilePath() const
{
    return m_PinnedHeroFilePath;
}

void FeaturedFilePresenter::setThumbnailPathResolver(ThumbnailPathResolver thumbnailPathResolver)
{
    m_ThumbnailPathResolver = std::move(thumbnailPathResolver);
}

void FeaturedFilePresenter::setThumbnailCache(DashboardThumbnailCache* thumbnailCache)
{
    m_ThumbnailCache = thumbnailCache;
}

void FeaturedFilePresenter::setThumbnailCacheTileSize(const int tileSize)
{
    m_ThumbnailCacheTileSize = (std::max)(32, tileSize);
}

void FeaturedFilePresenter::setPreferredDecodeSize(const QSize& preferredDecodeSize)
{
    m_PreferredDecodeSize = preferredDecodeSize;
}

void FeaturedFilePresenter::refresh()
{
    ++m_RequestGeneration;

    const QStringList candidateOrder = buildCandidateOrder();
    if (candidateOrder.isEmpty())
    {
        emit noFeaturedFileAvailable();
        return;
    }

    tryCandidateAt(0, m_RequestGeneration, candidateOrder);
}

QStringList FeaturedFilePresenter::buildCandidateOrder() const
{
    QStringList candidates;
    QSet<QString> seen;
    seen.reserve(static_cast<qsizetype>(m_RecentFilePaths.size()) + 1);

    // Pinned path is already normalized by setPinnedHeroFilePath().
    if (!m_PinnedHeroFilePath.isEmpty() && !seen.contains(m_PinnedHeroFilePath))
    {
        seen.insert(m_PinnedHeroFilePath);
        candidates.push_back(m_PinnedHeroFilePath);
    }

    // Recent paths are already normalized by setRecentFilePaths().
    for (const QString& recentFilePath : m_RecentFilePaths)
    {
        if (!seen.contains(recentFilePath))
        {
            seen.insert(recentFilePath);
            candidates.push_back(recentFilePath);
        }
    }

    return candidates;
}

bool FeaturedFilePresenter::isCandidateUsable(const QString& filePath) const
{
    if (filePath.isEmpty() || !m_NormalizedRecentSet.contains(filePath))
    {
        return false;
    }

    const QFileInfo sourceInfo(filePath);
    return sourceInfo.exists() && sourceInfo.isFile();
}

QString FeaturedFilePresenter::resolveThumbnailPathForCandidate(const QString& filePath) const
{
    if (!m_ThumbnailPathResolver)
    {
        return {};
    }

    const QString rawPath = m_ThumbnailPathResolver(filePath);
    const QString normalizedPath = QDir::cleanPath(rawPath.trimmed());
    if (normalizedPath.isEmpty())
    {
        return {};
    }

    const QFileInfo thumbnailInfo(normalizedPath);
    if (!thumbnailInfo.exists() || !thumbnailInfo.isFile())
    {
        return {};
    }

    return thumbnailInfo.absoluteFilePath();
}

void FeaturedFilePresenter::tryCandidateAt(int candidateIndex, const quint64 requestGeneration,
                                           const QStringList& candidateOrder)
{
    if (requestGeneration != m_RequestGeneration)
    {
        return;
    }

    for (int index = candidateIndex; index < candidateOrder.size(); ++index)
    {
        const QString& filePath = candidateOrder[index];
        if (!isCandidateUsable(filePath))
        {
            continue;
        }

        const bool pinned = !m_PinnedHeroFilePath.isEmpty() && m_PinnedHeroFilePath == filePath;

        const QString cacheKey = DashboardThumbnailCache::cacheKey(filePath, (std::max)(32, m_ThumbnailCacheTileSize));
        if (m_ThumbnailCache)
        {
            if (const auto* cachedEntry = m_ThumbnailCache->find(cacheKey);
                cachedEntry && !cachedEntry->displayImage.isNull())
            {
                emit featuredFileReady(filePath, QFileInfo(filePath).fileName(),
                                       metadataTextForFile(filePath, cachedEntry->sourceSize),
                                       QPixmap::fromImage(cachedEntry->displayImage), pinned);
                return;
            }
        }

        const QString thumbnailPath = resolveThumbnailPathForCandidate(filePath);
        const QString decodePath = thumbnailPath.isEmpty() ? filePath : thumbnailPath;
        const qint64 lastModifiedMs = QFileInfo(decodePath).lastModified().toMSecsSinceEpoch();
        const QSize targetSize = m_PreferredDecodeSize.isValid() ? m_PreferredDecodeSize : QSize(1200, 260);
        QPointer<FeaturedFilePresenter> self(this);
        QThreadPool::globalInstance()->start([self, requestGeneration, index, candidateOrder, filePath, decodePath,
                                              targetSize, pinned, lastModifiedMs]() {
            QImageReader reader(decodePath);
            reader.setAutoTransform(true);
            const QSize sourceSize = reader.size();
            QImage decodedImage = reader.read();
            if (!decodedImage.isNull() && targetSize.isValid())
            {
                decodedImage =
                    decodedImage.scaled(targetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            }

            if (!self)
            {
                return;
            }

            QMetaObject::invokeMethod(
                self,
                [self, requestGeneration, index, candidateOrder, filePath, decodedImage = std::move(decodedImage),
                 sourceSize, pinned, lastModifiedMs]() {
                    if (!self)
                    {
                        return;
                    }

                    self->emitReadyFromImage(filePath, decodedImage, sourceSize, pinned, requestGeneration, index,
                                             candidateOrder, lastModifiedMs);
                },
                Qt::QueuedConnection);
        });

        return;
    }

    emit noFeaturedFileAvailable();
}

void FeaturedFilePresenter::emitReadyFromImage(const QString& filePath, const QImage& decodedImage,
                                               const QSize& sourceSize, const bool pinned,
                                               const quint64 requestGeneration, const int candidateIndex,
                                               const QStringList& candidateOrder, const qint64 thumbnailLastModifiedMs)
{
    // Always cache successfully decoded images to avoid redundant I/O on
    // subsequent refresh passes, even if this result belongs to a superseded
    // generation.  The generation guard below still prevents stale results
    // from being emitted to the UI.
    if (!decodedImage.isNull() && m_ThumbnailCache)
    {
        const QString cacheKey = DashboardThumbnailCache::cacheKey(filePath, (std::max)(32, m_ThumbnailCacheTileSize));
        m_ThumbnailCache->insert(cacheKey, decodedImage, sourceSize, thumbnailLastModifiedMs);
    }

    if (requestGeneration != m_RequestGeneration)
    {
        return;
    }

    if (decodedImage.isNull())
    {
        tryCandidateAt(candidateIndex + 1, requestGeneration, candidateOrder);
        return;
    }

    emit featuredFileReady(filePath, QFileInfo(filePath).fileName(), metadataTextForFile(filePath, sourceSize),
                           QPixmap::fromImage(decodedImage), pinned);
}

QString FeaturedFilePresenter::metadataTextForFile(const QString& filePath, const QSize& sourceSize) const
{
    const QFileInfo fileInfo(filePath);
    QString fileType = fileInfo.suffix().trimmed().toUpper();
    if (fileType.isEmpty())
    {
        fileType = tr("FILE");
    }
    else if (fileType == QStringLiteral("FIT"))
    {
        fileType = QStringLiteral("FITS");
    }

    QString dimensionsText = tr("Unknown size");
    if (sourceSize.isValid() && sourceSize.width() > 0 && sourceSize.height() > 0)
    {
        dimensionsText = tr("%1 x %2").arg(sourceSize.width()).arg(sourceSize.height());
    }

    // ── Astro metadata extraction ──────────────────────────────────────
    // Attempt to read embedded FITS/EXIF metadata for supported formats.
    // These calls are lightweight (metadata-only I/O, no pixel decoding).
    QString objectName;
    QString sessionDate;
    QString filterName;
    QString exposureText;
    QString cameraModel;
    QString telescopeModel;

    const bool isAstroFormat =
        fileType == QStringLiteral("FITS") || fileType == QStringLiteral("FIT") || fileType == QStringLiteral("XISF") ||
        fileType == QStringLiteral("TIFF") || fileType == QStringLiteral("TIF") || fileType == QStringLiteral("CR2") ||
        fileType == QStringLiteral("CR3") || fileType == QStringLiteral("NEF") || fileType == QStringLiteral("ARW") ||
        fileType == QStringLiteral("DNG") || fileType == QStringLiteral("RAF");

    // Guard: skip metadata extraction for files too small to be valid images.
    // A single FITS header block is 2880 bytes; XISF and RAW files are even
    // larger, so a tiny stub file (e.g. in unit tests) holds no image to describe.
    constexpr qint64 kMinAstroFileSize = 2880;

    if (isAstroFormat && fileInfo.size() >= kMinAstroFileSize)
    {
        // Background work that is never waited for: a busy session leaves the description to the file's own facts.
        if (const std::optional<an::ui::DashboardFileFacts> facts = an::ui::probeDashboardFile(filePath);
            facts.has_value())
        {
            const auto stated = [](const QString& candidate) {
                return !candidate.isEmpty() && candidate != QStringLiteral("-") &&
                       candidate.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0 &&
                       candidate.compare(QStringLiteral("n/a"), Qt::CaseInsensitive) != 0;
            };

            // Object name (highest priority — the target, e.g. "M31", "NGC 7000"). FITS files commonly store "-",
            // "N/A", "NONE" etc. when the keyword is present but unpopulated; discard those placeholders.
            static const QStringList kFitsPlaceholders = {
                QStringLiteral("-"),       QStringLiteral("--"), QStringLiteral("---"),
                QStringLiteral("n/a"),     QStringLiteral("na"), QStringLiteral("none"),
                QStringLiteral("unknown"), QStringLiteral("?"),  QStringLiteral("n.a.")};
            if (!facts->object.isEmpty() && !kFitsPlaceholders.contains(facts->object, Qt::CaseInsensitive))
            {
                objectName = facts->object;
            }

            // Session date: the day the frame was taken, or the day the file was made.
            if (facts->date.has_value() && facts->date->isValid())
            {
                sessionDate = QLocale().toString(*facts->date, QLocale::ShortFormat);
            }

            if (stated(facts->filter))
            {
                filterName = facts->filter;
            }
            if (facts->exposureSeconds.has_value() && *facts->exposureSeconds > 0.0)
            {
                exposureText = an::ui::dashboardExposureText(*facts->exposureSeconds);
            }
            if (stated(facts->camera))
            {
                cameraModel = facts->camera;
            }
            if (stated(facts->telescope))
            {
                telescopeModel = facts->telescope;
            }
        }
    }

    // ── Build the display string ───────────────────────────────────────
    // Primary line: Object  ·  Session date  (highest relevancy)
    // Secondary pieces:  Filter  ·  Exposure  ·  Camera  ·  Telescope
    // Fallback:  FileType  ·  Dimensions  ·  Edit time
    QStringList primaryParts;
    QStringList secondaryParts;

    if (!objectName.isEmpty())
    {
        primaryParts << objectName;
    }
    if (!sessionDate.isEmpty())
    {
        primaryParts << sessionDate;
    }

    if (!filterName.isEmpty())
    {
        secondaryParts << filterName;
    }
    if (!exposureText.isEmpty())
    {
        secondaryParts << exposureText;
    }
    if (!cameraModel.isEmpty())
    {
        secondaryParts << cameraModel;
    }
    if (!telescopeModel.isEmpty())
    {
        secondaryParts << telescopeModel;
    }

    // Always include technical info as a fallback / context
    QStringList technicalParts;
    technicalParts << fileType << dimensionsText << formatRelativeEditTime(fileInfo);

    // Compose: put primary first, then secondary, then technical context.
    // If no astro metadata is available, fall back to the original simple line.
    if (primaryParts.isEmpty() && secondaryParts.isEmpty())
    {
        // No astro metadata — show a compact two-line fallback so that the
        // separator dot never appears at the start of a wrapped line (which
        // can be misread as a leading dash at small font sizes).
        QString topLine = fileType;
        if (dimensionsText != tr("Unknown size"))
        {
            topLine += QStringLiteral(" \u00b7 ") + dimensionsText;
        }
        const QString editTime = formatRelativeEditTime(fileInfo);
        return topLine + QChar('\n') + editTime;
    }

    QStringList lines;
    if (!primaryParts.isEmpty())
    {
        lines << primaryParts.join(QStringLiteral(" \u00b7 "));
    }

    QStringList detailParts = secondaryParts + technicalParts;
    if (!detailParts.isEmpty())
    {
        lines << detailParts.join(QStringLiteral(" \u00b7 "));
    }

    return lines.join(QStringLiteral("\n"));
}

QString FeaturedFilePresenter::formatRelativeEditTime(const QFileInfo& fileInfo) const
{
    if (!fileInfo.exists() || !fileInfo.lastModified().isValid())
    {
        return tr("Edited recently");
    }

    const QDateTime now = QDateTime::currentDateTime();
    const qint64 elapsedSeconds = (std::max<qint64>)(0, fileInfo.lastModified().secsTo(now));
    if (elapsedSeconds < 60)
    {
        return tr("Edited just now");
    }

    struct TimeUnit
    {
        qint64 thresholdSeconds;
        qint64 divisor;
        const char* singular;
        const char* plural;
    };

    static constexpr TimeUnit units[] = {
        {3600, 60, QT_TR_NOOP("Edited 1 minute ago"), QT_TR_NOOP("Edited %1 minutes ago")},
        {86400, 3600, QT_TR_NOOP("Edited 1 hour ago"), QT_TR_NOOP("Edited %1 hours ago")},
        {604800, 86400, QT_TR_NOOP("Edited 1 day ago"), QT_TR_NOOP("Edited %1 days ago")},
    };

    for (const auto& unit : units)
    {
        if (elapsedSeconds < unit.thresholdSeconds)
        {
            const qint64 count = elapsedSeconds / unit.divisor;
            return (count == 1) ? tr(unit.singular) : tr(unit.plural).arg(count);
        }
    }

    return tr("Edited %1").arg(QLocale().toString(fileInfo.lastModified(), QLocale::ShortFormat));
}

} // namespace an::ui
