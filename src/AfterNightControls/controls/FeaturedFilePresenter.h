// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QObject>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <verdigris/wobjectdefs.h>

#include <deque>
#include <functional>

class DashboardThumbnailCache;

namespace an::ui
{

class AFTERNIGHTCONTROLS_API FeaturedFilePresenter : public QObject
{
    W_OBJECT(FeaturedFilePresenter)

public:
    using ThumbnailPathResolver = std::function<QString(const QString&)>;

    explicit FeaturedFilePresenter(QObject* parent = nullptr);
    ~FeaturedFilePresenter() override = default;

    void setRecentFilePaths(const std::deque<QString>& recentFilePaths);
    void setPinnedHeroFilePath(const QString& pinnedHeroFilePath);
    [[nodiscard]] QString pinnedHeroFilePath() const;

    void setThumbnailPathResolver(ThumbnailPathResolver thumbnailPathResolver);
    void setThumbnailCache(DashboardThumbnailCache* thumbnailCache);
    void setThumbnailCacheTileSize(int tileSize);
    void setPreferredDecodeSize(const QSize& preferredDecodeSize);

    void refresh();

    /// Build a rich metadata description for the given file (astro metadata +
    /// technical info).  Safe to call from the main thread — metadata-only I/O.
    [[nodiscard]] QString metadataTextForFile(const QString& filePath, const QSize& sourceSize) const;

signals:
    void featuredFileReady(const QString& filePath, const QString& displayName, const QString& metadataText,
                           const QPixmap& backgroundPixmap, bool pinned)
        W_SIGNAL(featuredFileReady, filePath, displayName, metadataText, backgroundPixmap, pinned);
    void noFeaturedFileAvailable() W_SIGNAL(noFeaturedFileAvailable);

private:
    [[nodiscard]] QStringList buildCandidateOrder() const;
    [[nodiscard]] bool isCandidateUsable(const QString& filePath) const;
    [[nodiscard]] QString resolveThumbnailPathForCandidate(const QString& filePath) const;
    void tryCandidateAt(int candidateIndex, quint64 requestGeneration, const QStringList& candidateOrder);

    void emitReadyFromImage(const QString& filePath, const QImage& decodedImage, const QSize& sourceSize, bool pinned,
                            quint64 requestGeneration, int candidateIndex, const QStringList& candidateOrder,
                            qint64 thumbnailLastModifiedMs);

    [[nodiscard]] QString formatRelativeEditTime(const QFileInfo& fileInfo) const;

    std::deque<QString> m_RecentFilePaths;
    QSet<QString> m_NormalizedRecentSet;
    QString m_PinnedHeroFilePath;
    ThumbnailPathResolver m_ThumbnailPathResolver;
    DashboardThumbnailCache* m_ThumbnailCache = nullptr;
    int m_ThumbnailCacheTileSize = 300;
    QSize m_PreferredDecodeSize = QSize(1200, 260);
    quint64 m_RequestGeneration = 0;
};

} // namespace an::ui
