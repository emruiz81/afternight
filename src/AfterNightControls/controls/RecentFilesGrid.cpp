// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/RecentFilesGrid.h"

#include "controls/DashboardFileFacts.h"
#include "controls/DashboardPreferences.h"
#include "controls/DashboardThumbnailCache.h"
#include "controls/DashboardUtils.h"
#include "theme/AnimationUtils.h"
#include "widgets/ImageThumbnailWidget.h"

#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QImageReader>
#include <QLayoutItem>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QThreadPool>
#include <QTimer>
#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

W_OBJECT_IMPL(an::ui::RecentFilesGrid)

using DashboardUtils::normalizeRecentFilePath;

namespace
{
constexpr int THUMBNAIL_BATCH_SIZE = 4;
constexpr int THUMBNAIL_MAX_IN_FLIGHT = 4;
constexpr int RECENT_FILTER_DEBOUNCE_MS = 120;

// What a card shows of a file beyond its thumbnail: the image's size, read from the file itself because the thumbnail
// is smaller, and for an astronomical format the one-line summary of its header. The probe is background work that is
// never waited for, on the dashboard's worker or, for a thumbnail already cached, on the GUI thread.
struct CardFacts
{
    QSize size;
    QString summary;
};

CardFacts probeCardFile(const QString& filePath)
{
    const std::optional<an::ui::DashboardFileFacts> facts = an::ui::probeDashboardFile(filePath);
    if (!facts.has_value())
    {
        return {};
    }

    CardFacts card{.size = facts->size, .summary = {}};
    const QFileInfo info(filePath);
    // A single FITS header block is 2880 bytes; a smaller file states no header worth a summary.
    constexpr qint64 kMinAstroFileSize = 2880;
    if (an::ui::isDashboardAstroFormat(info.suffix()) && info.size() >= kMinAstroFileSize)
    {
        card.summary = an::ui::compactDashboardSummary(*facts);
    }
    return card;
}

} // anonymous namespace

namespace an::ui
{

RecentFilesGrid::RecentFilesGrid(QWidget* parent)
    : QWidget(parent), m_LocalThumbnailCache(std::make_unique<DashboardThumbnailCache>(256))
{
    setObjectName(QStringLiteral("dashboardRecentGridContainer"));
    setProperty("dashboardTransitionActive", false);

    m_GridLayout = new QGridLayout(this);
    m_GridLayout->setHorizontalSpacing(m_TileDistance);
    m_GridLayout->setVerticalSpacing(m_TileDistance);
    m_GridLayout->setContentsMargins(0, 0, 0, 0);
    m_GridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    m_ThumbnailLoadTimer = new QTimer(this);
    m_ThumbnailLoadTimer->setSingleShot(true);
    m_ThumbnailLoadTimer->setInterval(0);
    connect(m_ThumbnailLoadTimer, &QTimer::timeout, this, &RecentFilesGrid::loadNextThumbnailBatch);

    m_RecentFilterTimer = new QTimer(this);
    m_RecentFilterTimer->setSingleShot(true);
    m_RecentFilterTimer->setInterval(RECENT_FILTER_DEBOUNCE_MS);
    connect(m_RecentFilterTimer, &QTimer::timeout, this, &RecentFilesGrid::refreshRecentListView);
}

RecentFilesGrid::~RecentFilesGrid() = default;

void RecentFilesGrid::setThumbnailPathResolver(ThumbnailPathResolver thumbnailPathResolver)
{
    m_ThumbnailPathResolver = std::move(thumbnailPathResolver);
}

void RecentFilesGrid::setThumbnailCache(DashboardThumbnailCache* cache)
{
    m_ThumbnailCache = cache;
}

void RecentFilesGrid::setDarkMode(const bool darkMode)
{
    if (m_DarkMode == darkMode)
    {
        return;
    }

    m_DarkMode = darkMode;
    refreshTheme();
}

void RecentFilesGrid::setTileMetrics(const int tileSize, const int tileDistance)
{
    const int normalizedTileSize = (std::max)(220, tileSize);
    const int normalizedTileDistance = (std::max)(8, tileDistance);

    const bool metricsChanged = normalizedTileSize != m_TileSize || normalizedTileDistance != m_TileDistance;
    if (!metricsChanged)
    {
        return;
    }

    m_TileSize = normalizedTileSize;
    m_TileDistance = normalizedTileDistance;
    if (m_GridLayout)
    {
        m_GridLayout->setHorizontalSpacing(m_TileDistance);
        m_GridLayout->setVerticalSpacing(m_TileDistance);
    }

    for (const auto& thumbnail : m_ImageList)
    {
        if (thumbnail)
        {
            thumbnail->setFixedSize(m_TileSize, m_TileSize);
        }
    }

    updateGrid();
}

void RecentFilesGrid::setRecentFiles(const std::deque<QString>& recentFiles)
{
    bool sameSnapshot = recentFiles.size() == m_AllRecentFilePaths.size();
    if (sameSnapshot)
    {
        auto previousIt = m_AllRecentFilePaths.begin();
        for (const QString& path : recentFiles)
        {
            if (previousIt == m_AllRecentFilePaths.end() || *previousIt != path)
            {
                sameSnapshot = false;
                break;
            }
            ++previousIt;
        }
    }

    if (sameSnapshot)
    {
        if (!m_Initialized)
        {
            refreshRecentListView();
        }
        return;
    }

    m_AllRecentFilePaths = recentFiles;
    scheduleRecentListRefresh();
}

void RecentFilesGrid::setPinnedFilePaths(const QSet<QString>& pinnedFiles)
{
    if (m_PinnedRecentFiles == pinnedFiles)
    {
        return;
    }

    m_PinnedRecentFiles = pinnedFiles;
    scheduleRecentListRefresh();
}

const QSet<QString>& RecentFilesGrid::pinnedFilePaths() const
{
    return m_PinnedRecentFiles;
}

void RecentFilesGrid::setSearchFilter(const QString& searchFilter)
{
    const QString normalizedFilter = searchFilter.trimmed();
    if (m_SearchFilter == normalizedFilter)
    {
        return;
    }

    m_SearchFilter = normalizedFilter;
    scheduleRecentListRefresh();
}

void RecentFilesGrid::setSortMode(const int sortMode)
{
    if (m_SortMode == sortMode)
    {
        return;
    }

    m_SortMode = sortMode;
    scheduleRecentListRefresh();
}

int RecentFilesGrid::visibleFileCount() const
{
    return static_cast<int>(m_VisibleRecentFileOrder.size());
}

int RecentFilesGrid::totalFileCount() const
{
    return static_cast<int>(m_AllRecentFilePaths.size());
}

QStringList RecentFilesGrid::visibleFileOrder() const
{
    return m_VisibleRecentFileOrder;
}

const std::deque<QString>& RecentFilesGrid::allRecentFilePaths() const
{
    return m_AllRecentFilePaths;
}

DashboardThumbnailCache* RecentFilesGrid::thumbnailCache()
{
    return m_ThumbnailCache ? m_ThumbnailCache : m_LocalThumbnailCache.get();
}

void RecentFilesGrid::reloadThumbnails()
{
    scheduleRecentListRefresh();
}

void RecentFilesGrid::clearAll()
{
    ++m_ThumbnailLoadGeneration;
    m_AllRecentFilePaths.clear();
    m_VisibleRecentFileOrder.clear();
    clearRecentThumbnailWidgets();
    emit stateChanged(0, 0, !m_SearchFilter.isEmpty());
    updateGrid();
}

void RecentFilesGrid::refreshTheme()
{
    for (const auto& thumbnail : m_ImageList)
    {
        if (thumbnail)
        {
            thumbnail->refreshColors(m_DarkMode);
        }
    }
}

void RecentFilesGrid::refreshLayout()
{
    updateGrid();
}

void RecentFilesGrid::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, &RecentFilesGrid::updateGrid);
}

void RecentFilesGrid::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateGrid();
}

void RecentFilesGrid::clearRecentThumbnailWidgets()
{
    if (m_ThumbnailLoadTimer)
    {
        m_ThumbnailLoadTimer->stop();
    }
    m_PendingThumbnailPaths.clear();

    for (const auto& thumbnail : m_ImageList)
    {
        if (thumbnail)
        {
            thumbnail->close();
        }
    }
    m_ImageList.clear();
    m_LastGridLayoutOrder.clear();
    m_LastGridColumnCount = -1;
}

void RecentFilesGrid::scheduleRecentListRefresh()
{
    if (m_RecentFilterTimer)
    {
        m_RecentFilterTimer->start();
        return;
    }

    refreshRecentListView();
}

void RecentFilesGrid::refreshRecentListView()
{
    ++m_ThumbnailLoadGeneration;
    clearRecentThumbnailWidgets();

    const QString searchText = m_SearchFilter;

    std::vector<QString> filtered;
    filtered.reserve(m_AllRecentFilePaths.size());
    for (const auto& filePath : m_AllRecentFilePaths)
    {
        if (searchText.isEmpty())
        {
            filtered.push_back(filePath);
            continue;
        }

        const QFileInfo info(filePath);
        const bool matchesPath = filePath.contains(searchText, Qt::CaseInsensitive);
        const bool matchesName = info.fileName().contains(searchText, Qt::CaseInsensitive);
        if (matchesPath || matchesName)
        {
            filtered.push_back(filePath);
        }
    }

    if (m_SortMode == DashboardPreferences::RECENT_SORT_NAME)
    {
        std::stable_sort(filtered.begin(), filtered.end(), [](const QString& lhs, const QString& rhs) {
            const QString leftName = QFileInfo(lhs).fileName();
            const QString rightName = QFileInfo(rhs).fileName();
            return QString::localeAwareCompare(leftName, rightName) < 0;
        });
    }
    else if (m_SortMode == DashboardPreferences::RECENT_SORT_TYPE)
    {
        std::stable_sort(filtered.begin(), filtered.end(), [](const QString& lhs, const QString& rhs) {
            const QFileInfo leftInfo(lhs);
            const QFileInfo rightInfo(rhs);
            const QString leftSuffix = leftInfo.suffix().toLower();
            const QString rightSuffix = rightInfo.suffix().toLower();
            if (leftSuffix != rightSuffix)
            {
                return leftSuffix < rightSuffix;
            }

            return QString::localeAwareCompare(leftInfo.fileName(), rightInfo.fileName()) < 0;
        });
    }

    std::stable_partition(filtered.begin(), filtered.end(),
                          [this](const QString& filePath) { return isRecentFilePinned(filePath); });

    m_VisibleRecentFileOrder.clear();
    m_VisibleRecentFileOrder.reserve(static_cast<qsizetype>(filtered.size()));
    for (const auto& filePath : filtered)
    {
        m_VisibleRecentFileOrder.push_back(filePath);
        m_PendingThumbnailPaths.emplace_back(filePath);
    }

    const int totalCount = static_cast<int>(m_AllRecentFilePaths.size());
    const int visibleCount = static_cast<int>(filtered.size());
    const bool hasActiveFilter = !searchText.isEmpty();
    emit stateChanged(visibleCount, totalCount, hasActiveFilter);
    m_Initialized = true;

    if (visibleCount <= 0)
    {
        updateGrid();
        return;
    }

    if (m_ThumbnailLoadTimer)
    {
        m_ThumbnailLoadTimer->start();
    }
}

void RecentFilesGrid::loadNextThumbnailBatch()
{
    int loaded = 0;
    while (!m_PendingThumbnailPaths.empty() && loaded < THUMBNAIL_BATCH_SIZE &&
           m_ThumbnailLoadsInFlight < THUMBNAIL_MAX_IN_FLIGHT)
    {
        const QString nextPath = m_PendingThumbnailPaths.front();
        m_PendingThumbnailPaths.pop_front();
        scheduleThumbnailLoad(nextPath, m_ThumbnailLoadGeneration);
        ++loaded;
    }

    if (loaded > 0)
    {
        updateGrid();
    }

    if (!m_ThumbnailLoadTimer)
    {
        return;
    }

    if (m_PendingThumbnailPaths.empty() && m_ThumbnailLoadsInFlight == 0)
    {
        m_ThumbnailLoadTimer->stop();
        return;
    }

    m_ThumbnailLoadTimer->start();
}

void RecentFilesGrid::scheduleThumbnailLoad(const QString& imagePath, quint64 generationId)
{
    const QString thumbnailPath = resolveThumbnailPath(imagePath);
    if (thumbnailPath.isEmpty())
    {
        return;
    }

    const QFileInfo thumbnailInfo(thumbnailPath);
    if (!thumbnailInfo.exists() || !thumbnailInfo.isFile())
    {
        return;
    }

    const qint64 lastModifiedMs = thumbnailInfo.lastModified().toMSecsSinceEpoch();
    const QString cacheKey = DashboardThumbnailCache::cacheKey(imagePath, m_TileSize);
    if (const auto* cached = thumbnailCache()->find(cacheKey);
        cached && cached->lastModifiedMs == lastModifiedMs && !cached->displayImage.isNull())
    {
        // The cached entry knows the size; only an astronomical format's header is read again, for its summary.
        const QString metadataText =
            isDashboardAstroFormat(QFileInfo(imagePath).suffix()) ? probeCardFile(imagePath).summary : QString();
        addImageFromData(imagePath, cached->displayImage, cached->sourceSize, true, metadataText);
        return;
    }

    if (m_ThumbnailLoadsInFlight >= THUMBNAIL_MAX_IN_FLIGHT)
    {
        return;
    }

    ++m_ThumbnailLoadsInFlight;
    const QSize targetSize((std::max)(32, m_TileSize - 30), (std::max)(32, m_TileSize - 60));
    QPointer<RecentFilesGrid> self(this);
    QThreadPool::globalInstance()->start(
        [self, imagePath, thumbnailPath, targetSize, generationId, cacheKey, lastModifiedMs]() {
            const CardFacts card = probeCardFile(imagePath);
            QSize sourceSize = card.size;
            const QString metadataText = card.summary;

            QImageReader reader(thumbnailPath);
            reader.setAutoTransform(true);
            if (!sourceSize.isValid())
            {
                sourceSize = reader.size();
            }
            QImage decodedImage = reader.read();
            if (!decodedImage.isNull())
            {
                decodedImage = decodedImage.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }

            if (!self)
            {
                return;
            }

            QMetaObject::invokeMethod(
                self,
                [self, generationId, imagePath, cacheKey, sourceSize, metadataText,
                 decodedImage = std::move(decodedImage), lastModifiedMs]() {
                    if (!self)
                    {
                        return;
                    }

                    if (self->m_ThumbnailLoadsInFlight > 0)
                    {
                        --self->m_ThumbnailLoadsInFlight;
                    }

                    if (generationId != self->m_ThumbnailLoadGeneration)
                    {
                        if (self->m_ThumbnailLoadTimer &&
                            (!self->m_PendingThumbnailPaths.empty() || self->m_ThumbnailLoadsInFlight > 0))
                        {
                            self->m_ThumbnailLoadTimer->start();
                        }
                        return;
                    }

                    if (!decodedImage.isNull())
                    {
                        self->thumbnailCache()->insert(cacheKey, decodedImage, sourceSize, lastModifiedMs);
                        self->addImageFromData(imagePath, decodedImage, sourceSize, false, metadataText);
                    }

                    self->updateGrid();

                    if (self->m_ThumbnailLoadTimer)
                    {
                        if (!self->m_PendingThumbnailPaths.empty() || self->m_ThumbnailLoadsInFlight > 0)
                        {
                            self->m_ThumbnailLoadTimer->start();
                        }
                        else
                        {
                            self->m_ThumbnailLoadTimer->stop();
                        }
                    }
                },
                Qt::QueuedConnection);
        });
}

QString RecentFilesGrid::resolveThumbnailPath(const QString& imagePath) const
{
    if (!m_ThumbnailPathResolver)
    {
        return QString();
    }

    return m_ThumbnailPathResolver(imagePath);
}

bool RecentFilesGrid::isRecentFilePinned(const QString& filePath) const
{
    const QString normalizedPath = normalizeRecentFilePath(filePath);
    return !normalizedPath.isEmpty() && m_PinnedRecentFiles.contains(normalizedPath);
}

ImageThumbnailWidget* RecentFilesGrid::createThumbnailWidget(const QString& imagePath)
{
    auto openFileCallback = [this](const QString& filePath) {
        // Emit the cached thumbnail image before signalling to open the file so
        // that listeners can update preview widgets (e.g. the hero card) immediately
        // without waiting for an async decode pass.
        const QString previewKey = DashboardThumbnailCache::cacheKey(filePath, m_TileSize);
        if (const auto* entry = thumbnailCache()->find(previewKey); entry && !entry->displayImage.isNull())
        {
            emit openFilePreviewReady(filePath, entry->displayImage, entry->sourceSize);
        }
        emit openFileRequested(filePath);
    };

    auto removeRecentCallback = [this](const QString& filePath) {
        const QString normalizedPath = normalizeRecentFilePath(filePath);
        if (!normalizedPath.isEmpty())
        {
            m_PinnedRecentFiles.remove(normalizedPath);
        }

        emit removeFileRequested(filePath);
    };

    auto togglePinnedCallback = [this](const QString& filePath, const bool pinned) {
        const QString normalizedPath = normalizeRecentFilePath(filePath);
        if (normalizedPath.isEmpty())
        {
            return;
        }

        bool changed = false;
        if (pinned)
        {
            const int sizeBefore = m_PinnedRecentFiles.size();
            m_PinnedRecentFiles.insert(normalizedPath);
            changed = m_PinnedRecentFiles.size() != sizeBefore;
        }
        else
        {
            changed = m_PinnedRecentFiles.remove(normalizedPath);
        }

        if (!changed)
        {
            return;
        }

        emit pinToggled(normalizedPath, pinned);
        scheduleRecentListRefresh();
    };

    auto* imageThumb =
        new ImageThumbnailWidget(m_ThumbnailPathResolver, std::move(openFileCallback), std::move(removeRecentCallback),
                                 m_DarkMode, this, std::move(togglePinnedCallback), isRecentFilePinned(imagePath));
    imageThumb->setProperty("dashboardStaggerActive", false);
    imageThumb->setProperty("dashboardStaggerPlayed", false);
    imageThumb->setProperty("dashboardStaggerDelayMs", 0);
    imageThumb->setFixedSize(m_TileSize, m_TileSize);
    imageThumb->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return imageThumb;
}

void RecentFilesGrid::addImageFromData(const QString& imagePath, const QImage& displayImage, const QSize& sourceSize,
                                       const bool loadedFromCache, const QString& metadataText)
{
    if (displayImage.isNull())
    {
        return;
    }

    auto* imageThumb = createThumbnailWidget(imagePath);
    imageThumb->setProperty("dashboardLoadedFromCache", loadedFromCache);
    if (!imageThumb->loadImageFromData(imagePath, QPixmap::fromImage(displayImage), sourceSize))
    {
        imageThumb->close();
        return;
    }

    if (!metadataText.isEmpty())
    {
        imageThumb->setMetadataText(metadataText);
    }

    m_ImageList.push_back(imageThumb);
}

void RecentFilesGrid::updateGrid()
{
    if (!m_GridLayout)
    {
        return;
    }

    layout()->activate();

    int availableWidth = (std::max)(1, contentsRect().width());
    if (availableWidth <= 0)
    {
        availableWidth = (std::max)(1, width());
    }

    const QMargins gridMargins = m_GridLayout->contentsMargins();
    availableWidth = (std::max)(1, availableWidth - gridMargins.left() - gridMargins.right());

    const int horizontalSpacing = (std::max)(0, m_GridLayout->horizontalSpacing());
    const int columnFootprint = (std::max)(1, m_TileSize + horizontalSpacing);
    const int numColumns = (std::max)(1, (availableWidth + horizontalSpacing) / columnFootprint);

    QList<QPointer<ImageThumbnailWidget>> validThumbnails;
    for (const auto& thumbnail : m_ImageList)
    {
        if (thumbnail)
        {
            validThumbnails.push_back(thumbnail);
        }
    }
    m_ImageList = validThumbnails;

    if (!m_VisibleRecentFileOrder.isEmpty() && !m_ImageList.empty())
    {
        QHash<QString, int> orderByPath;
        orderByPath.reserve(m_VisibleRecentFileOrder.size());
        int orderIndex = 0;
        for (const QString& filePath : m_VisibleRecentFileOrder)
        {
            if (!orderByPath.contains(filePath))
            {
                orderByPath.insert(filePath, orderIndex++);
            }
        }

        std::stable_sort(
            m_ImageList.begin(), m_ImageList.end(),
            [&orderByPath](const QPointer<ImageThumbnailWidget>& lhs, const QPointer<ImageThumbnailWidget>& rhs) {
                const int missingOrder = std::numeric_limits<int>::max();
                const QString leftPath = lhs ? lhs->property("recentFilePath").toString() : QString();
                const QString rightPath = rhs ? rhs->property("recentFilePath").toString() : QString();
                const int leftOrder = orderByPath.value(leftPath, missingOrder);
                const int rightOrder = orderByPath.value(rightPath, missingOrder);
                return leftOrder < rightOrder;
            });
    }

    QStringList orderedPaths;
    orderedPaths.reserve(m_ImageList.size());
    for (const auto& thumbnail : m_ImageList)
    {
        if (thumbnail)
        {
            orderedPaths.push_back(thumbnail->property("recentFilePath").toString());
        }
    }

    const bool layoutAlreadyPopulated = m_GridLayout->count() == static_cast<int>(m_ImageList.size());
    if (m_LastGridColumnCount == numColumns && m_LastGridLayoutOrder == orderedPaths && layoutAlreadyPopulated)
    {
        return;
    }

    const bool hadUpdatesEnabled = updatesEnabled();
    if (hadUpdatesEnabled)
    {
        setUpdatesEnabled(false);
    }

    while (auto* item = m_GridLayout->takeAt(0))
    {
        delete item;
    }

    const int resetRowCount = (std::max)(m_GridLayout->rowCount(), static_cast<int>(m_ImageList.size()) + 2);
    for (int rowIndex = 0; rowIndex < resetRowCount; ++rowIndex)
    {
        m_GridLayout->setRowMinimumHeight(rowIndex, 0);
        m_GridLayout->setRowStretch(rowIndex, 0);
    }

    const int resetColumnCount = (std::max)(m_GridLayout->columnCount(), numColumns + 2);
    for (int colIndex = 0; colIndex < resetColumnCount; ++colIndex)
    {
        m_GridLayout->setColumnMinimumWidth(colIndex, 0);
        m_GridLayout->setColumnStretch(colIndex, 0);
    }

    int row = 0;
    int column = 0;
    for (const auto& thumbnail : m_ImageList)
    {
        if (!thumbnail)
        {
            continue;
        }

        m_GridLayout->addWidget(thumbnail, row, column);
        ++column;
        if (column >= numColumns)
        {
            column = 0;
            ++row;
        }
    }

    const int populatedRows = column == 0 ? row : row + 1;
    for (int i = 0; i < populatedRows; ++i)
    {
        m_GridLayout->setRowMinimumHeight(i, m_TileSize);
        m_GridLayout->setRowStretch(i, 0);
    }

    m_GridLayout->setRowStretch(populatedRows, 1);
    for (int col = 0; col < numColumns; ++col)
    {
        m_GridLayout->setColumnMinimumWidth(col, m_TileSize);
        m_GridLayout->setColumnStretch(col, 0);
    }
    m_GridLayout->setColumnStretch(numColumns, 1);

    m_GridLayout->invalidate();
    m_GridLayout->activate();

    m_LastGridColumnCount = numColumns;
    m_LastGridLayoutOrder = orderedPaths;
    if (hadUpdatesEnabled)
    {
        setUpdatesEnabled(true);
    }
    update();
}

} // namespace an::ui
