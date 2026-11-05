// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QImage>
#include <QPointer>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QList>
#include <QWidget>
#include <verdigris/wobjectdefs.h>

#include <deque>
#include <functional>
#include <memory>

class QGridLayout;
class QResizeEvent;
class QShowEvent;
class QTimer;
class DashboardThumbnailCache;
class ImageThumbnailWidget;

namespace an::ui
{

class AFTERNIGHTCONTROLS_API RecentFilesGrid : public QWidget
{
    W_OBJECT(RecentFilesGrid)

public:
    using ThumbnailPathResolver = std::function<QString(const QString&)>;

    explicit RecentFilesGrid(QWidget* parent = nullptr);
    ~RecentFilesGrid() override;

    void setThumbnailPathResolver(ThumbnailPathResolver thumbnailPathResolver);
    void setThumbnailCache(DashboardThumbnailCache* cache);
    void setDarkMode(bool darkMode);
    void setTileMetrics(int tileSize, int tileDistance);
    void setRecentFiles(const std::deque<QString>& recentFiles);
    void setPinnedFilePaths(const QSet<QString>& pinnedFiles);
    [[nodiscard]] const QSet<QString>& pinnedFilePaths() const;
    void setSearchFilter(const QString& searchFilter);
    void setSortMode(int sortMode);

    [[nodiscard]] int visibleFileCount() const;
    [[nodiscard]] int totalFileCount() const;
    [[nodiscard]] QStringList visibleFileOrder() const;
    [[nodiscard]] const std::deque<QString>& allRecentFilePaths() const;
    [[nodiscard]] DashboardThumbnailCache* thumbnailCache();

    void reloadThumbnails();
    void clearAll();
    void refreshTheme();
    void refreshLayout();

signals:
    void openFileRequested(const QString& filePath) W_SIGNAL(openFileRequested, filePath);
    void openFilePreviewReady(const QString& filePath, const QImage& previewImage, const QSize& sourceSize)
        W_SIGNAL(openFilePreviewReady, filePath, previewImage, sourceSize);
    void removeFileRequested(const QString& filePath) W_SIGNAL(removeFileRequested, filePath);
    void pinToggled(const QString& filePath, bool pinned) W_SIGNAL(pinToggled, filePath, pinned);
    void stateChanged(int visibleCount, int totalCount, bool hasActiveFilter)
        W_SIGNAL(stateChanged, visibleCount, totalCount, hasActiveFilter);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void clearRecentThumbnailWidgets();
    void scheduleRecentListRefresh();
    void refreshRecentListView();
    void loadNextThumbnailBatch();
    void scheduleThumbnailLoad(const QString& imagePath, quint64 generationId);
    [[nodiscard]] QString resolveThumbnailPath(const QString& imagePath) const;
    [[nodiscard]] bool isRecentFilePinned(const QString& filePath) const;
    ImageThumbnailWidget* createThumbnailWidget(const QString& imagePath);
    void addImageFromData(const QString& imagePath, const QImage& displayImage, const QSize& sourceSize,
                          bool loadedFromCache = false, const QString& metadataText = {});
    void updateGrid();

    QGridLayout* m_GridLayout = nullptr;
    QList<QPointer<ImageThumbnailWidget>> m_ImageList;
    QTimer* m_ThumbnailLoadTimer = nullptr;
    QTimer* m_RecentFilterTimer = nullptr;
    std::deque<QString> m_PendingThumbnailPaths;
    std::deque<QString> m_AllRecentFilePaths;
    QStringList m_VisibleRecentFileOrder;
    QStringList m_LastGridLayoutOrder;
    QSet<QString> m_PinnedRecentFiles;
    DashboardThumbnailCache* m_ThumbnailCache = nullptr;
    int m_ThumbnailLoadsInFlight = 0;
    quint64 m_ThumbnailLoadGeneration = 0;
    int m_LastGridColumnCount = -1;
    int m_TileSize = 300;
    int m_TileDistance = 10;
    int m_SortMode = 0;
    QString m_SearchFilter;
    bool m_DarkMode = false;
    bool m_Initialized = false;
    ThumbnailPathResolver m_ThumbnailPathResolver;

    std::unique_ptr<DashboardThumbnailCache> m_LocalThumbnailCache;
};

} // namespace an::ui
