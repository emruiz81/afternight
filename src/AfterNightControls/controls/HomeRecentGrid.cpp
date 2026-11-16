// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/HomeRecentGrid.h"

#include "components/AnKeyHint.h"
#include "components/AnTag.h"
#include "controls/DashboardPreferences.h"
#include "controls/DashboardSettings.h"
#include "controls/DashboardThumbnailCache.h"
#include "controls/DashboardUtils.h"
#include "controls/IAppContext.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "theme/NightImage.h"

#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QResizeEvent>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <utility>

W_OBJECT_IMPL(an::ui::HomeRecentTile)
W_OBJECT_IMPL(an::ui::HomeRecentGrid)

namespace an::ui
{

namespace
{

/// How long the search waits for the next key before it orders the tiles again: the dashboard's.
constexpr int kSearchSettleMs = 120;

/// The size a tile's picture is decoded at, and the key it is cached under.
constexpr int kTileDecodeWidth = parts::kHomeTileMinWidth * 2;
constexpr int kTileDecodeHeight = parts::kHomeTilePicture * 2;

QIcon providedIcon(const QString& name)
{
    if (IAppContext* context = appContext())
    {
        if (const IIconProviderPtr icons = context->getIconProvider())
        {
            return icons->getIcon(name);
        }
    }
    return {};
}

/// The dashboard's words for a recorded action, under its translation context.
QString dashboardWords(const char* words)
{
    return QCoreApplication::translate("MainDashboard", words);
}

/// The search field: the dashboard's search, with its shortcut shown at its right while it is empty.
class SearchField final : public QLineEdit
{
public:
    explicit SearchField(QWidget* parent) : QLineEdit(parent)
    {
        m_keys = new AnKeyHint(QKeySequence(QStringLiteral("Ctrl+F")), this);
        m_keys->setAttribute(Qt::WA_TransparentForMouseEvents);
        QObject::connect(this, &QLineEdit::textChanged, this, [this]() { placeKeys(); });
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLineEdit::resizeEvent(event);
        placeKeys();
    }

private:
    void placeKeys()
    {
        const QSize hint = m_keys->sizeHint();
        const int inset = parts::kHomeActionKeysGap;
        m_keys->setGeometry(width() - inset - hint.width(), (height() - hint.height()) / 2, hint.width(),
                            hint.height());
        m_keys->setVisible(text().isEmpty());
        setTextMargins(0, 0, text().isEmpty() ? hint.width() + inset : 0, 0);
    }

    AnKeyHint* m_keys = nullptr;
};

/// Where the tiles are laid out: as many tiles of at least the least width as a row holds, stretched to fill it, or one
/// row each in the list. Its height follows its width.
class TileArea final : public QWidget
{
public:
    explicit TileArea(QWidget* parent) : QWidget(parent)
    {
        QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }

    void setTiles(const QList<HomeRecentTile*>& tiles, const HomeRecentGrid::Arrangement arrangement)
    {
        m_tiles = tiles;
        m_arrangement = arrangement;
        updateGeometry();
        place();
    }

    [[nodiscard]] bool hasHeightForWidth() const override
    {
        return true;
    }

    [[nodiscard]] int heightForWidth(const int width) const override
    {
        if (m_tiles.isEmpty())
        {
            return 0;
        }
        const int columns = columnsFor(width);
        const auto rows = static_cast<int>((m_tiles.size() + columns - 1) / columns);
        return rows * tileHeight() + (rows - 1) * parts::kHomeTileGap;
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        const int width = parts::kHomeTileMinWidth * 3 + parts::kHomeTileGap * 2;
        return {width, heightForWidth(width)};
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        return {parts::kHomeTileMinWidth, 0};
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        place();
    }

private:
    [[nodiscard]] int columnsFor(const int width) const
    {
        if (m_arrangement == HomeRecentGrid::Arrangement::List)
        {
            return 1;
        }
        return (std::max)(1, (width + parts::kHomeTileGap) / (parts::kHomeTileMinWidth + parts::kHomeTileGap));
    }

    [[nodiscard]] int tileHeight() const
    {
        return m_tiles.isEmpty() ? 0 : m_tiles.front()->sizeHint().height();
    }

    void place()
    {
        if (m_tiles.isEmpty())
        {
            return;
        }
        const int columns = columnsFor(width());
        const int gap = parts::kHomeTileGap;
        const int tileWidth = (std::max)(1, (width() - gap * (columns - 1)) / columns);
        const int height = tileHeight();
        for (qsizetype index = 0; index < m_tiles.size(); ++index)
        {
            const auto column = static_cast<int>(index % columns);
            const auto row = static_cast<int>(index / columns);
            m_tiles[index]->setGeometry(column * (tileWidth + gap), row * (height + gap), tileWidth, height);
        }
    }

    QList<HomeRecentTile*> m_tiles;
    HomeRecentGrid::Arrangement m_arrangement = HomeRecentGrid::Arrangement::Grid;
};

} // namespace

// ---- HomeRecentTile

struct HomeRecentTile::Impl
{
    QString path;
    bool sequence = false;
    bool pinned = false;
    bool hovered = false;
    bool keyboardFocus = false;
    qint64 readStamp = -1;
    Shape shape = Shape::Tile;
    NightPicture picture;
    HomeText* name = nullptr;
    HomeText* facts = nullptr;
    AnTag* tag = nullptr;
};

HomeRecentTile::HomeRecentTile(const QString& filePath, QWidget* parent)
    : QAbstractButton(parent), m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("homeRecentTile"));
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    m_impl->path = filePath;
    m_impl->sequence = isHomeSequence(filePath);
    m_impl->name = new HomeText(HomeText::Role::Strong, QFileInfo(filePath).fileName(), this);
    m_impl->name->setObjectName(QStringLiteral("homeRecentTileName"));
    m_impl->name->setElideMode(Qt::ElideMiddle);
    m_impl->facts = new HomeText(HomeText::Role::Mono, QString(), this);
    m_impl->facts->setObjectName(QStringLiteral("homeRecentTileFacts"));
    m_impl->tag = new AnTag(homeTypeText(filePath), AnTag::Tone::Neutral, this);
    m_impl->tag->setObjectName(QStringLiteral("homeRecentTileType"));
    for (QWidget* part : {static_cast<QWidget*>(m_impl->name), static_cast<QWidget*>(m_impl->facts),
                          static_cast<QWidget*>(m_impl->tag)})
    {
        part->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    QObject::connect(this, &QAbstractButton::clicked, this, [this]() { emit openRequested(m_impl->path); });
    refreshIcons();
    describe();
}

HomeRecentTile::~HomeRecentTile() = default;

QString HomeRecentTile::filePath() const
{
    return m_impl->path;
}

bool HomeRecentTile::isSequence() const
{
    return m_impl->sequence;
}

QString HomeRecentTile::nameText() const
{
    return m_impl->name->text();
}

QString HomeRecentTile::typeText() const
{
    return m_impl->tag->text();
}

QString HomeRecentTile::factsText() const
{
    return m_impl->facts->text();
}

bool HomeRecentTile::isPinned() const
{
    return m_impl->pinned;
}

bool HomeRecentTile::hasPicture() const
{
    return !m_impl->picture.isNull();
}

void HomeRecentTile::setShape(const Shape shape)
{
    if (m_impl->shape == shape)
    {
        return;
    }
    m_impl->shape = shape;
    updateGeometry();
    arrange();
    update();
}

void HomeRecentTile::setPinned(const bool pinned)
{
    if (m_impl->pinned == pinned)
    {
        return;
    }
    m_impl->pinned = pinned;
    setProperty("homeRecentPinned", pinned);
    describe();
    update();
}

void HomeRecentTile::setPicture(const QImage& picture)
{
    if (picture.isNull())
    {
        m_impl->picture.clear();
    }
    else
    {
        m_impl->picture.setPicture(picture);
    }
    update();
}

void HomeRecentTile::setFacts(const HomeRecentFacts& facts)
{
    m_impl->facts->setText(homeFactsText(facts));
    describe();
}

void HomeRecentTile::refreshIcons()
{
    m_impl->tag->setIcon(m_impl->sequence ? providedIcon(QStringLiteral("film")) : QIcon());
}

void HomeRecentTile::populateMenu(QMenu& menu)
{
    const QString path = m_impl->path;
    QAction* open = menu.addAction(tr("Open"));
    QObject::connect(open, &QAction::triggered, this, [this, path]() { emit openRequested(path); });
    const bool pinned = m_impl->pinned;
    QAction* pin = menu.addAction(pinned ? tr("Unpin from top") : tr("Pin to top"));
    QObject::connect(pin, &QAction::triggered, this, [this, path, pinned]() { emit pinRequested(path, !pinned); });
    QAction* reveal = menu.addAction(tr("Show in File Browser"));
    QObject::connect(reveal, &QAction::triggered, this, [this, path]() { emit revealRequested(path); });
    QAction* copy = menu.addAction(tr("Copy path"));
    QObject::connect(copy, &QAction::triggered, this, [path]() {
        if (QClipboard* clipboard = QGuiApplication::clipboard())
        {
            clipboard->setText(path);
        }
    });
    QAction* remove = menu.addAction(tr("Remove from recent files"));
    QObject::connect(remove, &QAction::triggered, this, [this, path]() { emit removeRequested(path); });
}

qint64 HomeRecentTile::readStamp() const noexcept
{
    return m_impl->readStamp;
}

void HomeRecentTile::setReadStamp(const qint64 stamp) noexcept
{
    m_impl->readStamp = stamp;
}

QSize HomeRecentTile::sizeHint() const
{
    if (m_impl->shape == Shape::Row)
    {
        return {parts::kHomeTileMinWidth, parts::kHomeListRow};
    }
    const int text = m_impl->name->sizeHint().height() + m_impl->facts->sizeHint().height();
    return {parts::kHomeTileMinWidth,
            parts::kHomeTilePicture + parts::kHomeTilePadding * 2 + text + parts::kHomeTilePadding / 2};
}

QSize HomeRecentTile::minimumSizeHint() const
{
    return sizeHint();
}

void HomeRecentTile::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF place = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = parts::kHomeTileRadius;
    QPainterPath shape;
    shape.addRoundedRect(place, radius, radius);
    painter.fillPath(shape, t.layers.surface2);

    const QPixmap shown = m_impl->picture.shown(this, [this]() { update(); });
    QRectF picture;
    if (m_impl->shape == Shape::Tile)
    {
        // The picture fills the tile's top, cut to the tile's corners.
        painter.save();
        painter.setClipPath(shape);
        picture = QRectF(place.left(), place.top(), place.width(), parts::kHomeTilePicture);
        paintHomePicture(painter, picture, shown, 0.0);
        painter.restore();
    }
    else
    {
        picture =
            QRectF(place.left() + parts::kHomeTilePadding, place.center().y() - parts::kHomeListPictureHeight / 2.0,
                   parts::kHomeListPictureWidth, parts::kHomeListPictureHeight);
        paintHomePicture(painter, picture, shown, parts::kHomeTileRadius / 2.0);
    }

    // A pinned file's mark: the pinned colour's dot at the picture's upper right.
    if (m_impl->pinned)
    {
        const qreal dot = parts::kHomeTileTagInset;
        const QRectF mark(picture.right() - parts::kHomeTileTagInset - dot, picture.top() + parts::kHomeTileTagInset,
                          dot, dot);
        painter.setPen(QPen(t.colors.scrimSoft, 2.0));
        painter.setBrush(t.colors.accentPinned);
        painter.drawEllipse(mark);
    }

    painter.setPen(QPen(t.colors.borderSubtle, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(shape);
    parts::paintStateLayer(painter, place, radius, m_impl->hovered, isDown());
    if (m_impl->keyboardFocus && hasFocus())
    {
        parts::paintFocusRing(painter, place, radius);
    }
}

void HomeRecentTile::resizeEvent(QResizeEvent* event)
{
    QAbstractButton::resizeEvent(event);
    arrange();
}

void HomeRecentTile::contextMenuEvent(QContextMenuEvent* event)
{
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("homeRecentTileMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    populateMenu(*menu);
    menu->popup(event->globalPos());
    event->accept();
}

void HomeRecentTile::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        animateClick();
        event->accept();
        return;
    }
    QAbstractButton::keyPressEvent(event);
}

void HomeRecentTile::enterEvent(QEnterEvent* event)
{
    m_impl->hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void HomeRecentTile::leaveEvent(QEvent* event)
{
    m_impl->hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void HomeRecentTile::focusInEvent(QFocusEvent* event)
{
    m_impl->keyboardFocus = parts::isKeyboardFocusReason(event->reason());
    update();
    QAbstractButton::focusInEvent(event);
}

void HomeRecentTile::focusOutEvent(QFocusEvent* event)
{
    m_impl->keyboardFocus = false;
    update();
    QAbstractButton::focusOutEvent(event);
}

void HomeRecentTile::changeEvent(QEvent* event)
{
    if (event != nullptr && (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange))
    {
        updateGeometry();
        arrange();
    }
    QAbstractButton::changeEvent(event);
}

void HomeRecentTile::arrange()
{
    const int pad = parts::kHomeTilePadding;
    const int nameHeight = m_impl->name->sizeHint().height();
    const int factsHeight = m_impl->facts->sizeHint().height();
    const QSize tag = m_impl->tag->sizeHint();
    if (m_impl->shape == Shape::Tile)
    {
        const int top = parts::kHomeTilePicture + pad;
        m_impl->name->setGeometry(pad, top, width() - pad * 2, nameHeight);
        m_impl->facts->setGeometry(pad, top + nameHeight + pad / 2, width() - pad * 2, factsHeight);
        m_impl->tag->setGeometry(parts::kHomeTileTagInset,
                                 parts::kHomeTilePicture - parts::kHomeTileTagInset - tag.height(), tag.width(),
                                 tag.height());
        return;
    }
    const int left = pad * 2 + parts::kHomeListPictureWidth;
    const int textWidth = (std::max)(0, width() - left - pad * 2 - tag.width());
    const int top = (height() - nameHeight - factsHeight - pad / 2) / 2;
    m_impl->name->setGeometry(left, top, textWidth, nameHeight);
    m_impl->facts->setGeometry(left, top + nameHeight + pad / 2, textWidth, factsHeight);
    m_impl->tag->setGeometry(width() - pad - tag.width(), (height() - tag.height()) / 2, tag.width(), tag.height());
}

void HomeRecentTile::describe()
{
    const QString kind = m_impl->sequence ? tr("sequence") : tr("image");
    QString name = tr("%1, %2").arg(m_impl->name->text(), kind);
    if (m_impl->pinned)
    {
        name = tr("%1, pinned").arg(name);
    }
    setAccessibleName(name);
    setAccessibleDescription(m_impl->facts->text());
    setToolTip(m_impl->path);
}

// ---- HomeRecentGrid

struct HomeRecentGrid::Impl
{
    HomeText* count = nullptr;
    HomeText* hint = nullptr;
    QLineEdit* search = nullptr;
    QAction* searchIcon = nullptr;
    QComboBox* sort = nullptr;
    QToolButton* grid = nullptr;
    QToolButton* list = nullptr;
    TileArea* area = nullptr;
    HomeText* noMatch = nullptr;
    QTimer* settle = nullptr;

    std::deque<QString> recent;
    QSet<QString> pinned;
    QStringList order;
    QHash<QString, HomeRecentTile*> tiles;
    Arrangement arrangement = Arrangement::Grid;
    HomeRecentProbe probe = &probeHomeRecentFile;
    FileRevealFunction reveal;
    std::unique_ptr<DashboardThumbnailCache> ownCache = std::make_unique<DashboardThumbnailCache>(256);
    DashboardThumbnailCache* cache = nullptr;
};

HomeRecentGrid::HomeRecentGrid(QWidget* parent) : QWidget(parent), m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("homeRecentGrid"));
    m_impl->cache = m_impl->ownCache.get();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(parts::kHomeTileGap);

    // The header: the title, the count and the hint, then the search, the sort and the arrangement.
    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(parts::kHomeTileGap);
    auto* title = new HomeText(HomeText::Role::Section, tr("Recent images and sequences"), this);
    title->setObjectName(QStringLiteral("homeRecentTitle"));
    title->setElideMode(Qt::ElideNone);
    header->addWidget(title, 0, Qt::AlignVCenter);
    m_impl->count = new HomeText(HomeText::Role::Mono, QString(), this);
    m_impl->count->setObjectName(QStringLiteral("homeRecentCount"));
    m_impl->count->setElideMode(Qt::ElideNone);
    header->addWidget(m_impl->count, 0, Qt::AlignVCenter);
    m_impl->hint = new HomeText(HomeText::Role::Caption, QString(), this);
    m_impl->hint->setObjectName(QStringLiteral("homeRecentHint"));
    header->addWidget(m_impl->hint, 1, Qt::AlignVCenter);

    m_impl->search = new SearchField(this);
    m_impl->search->setObjectName(QStringLiteral("homeRecentSearch"));
    m_impl->search->setPlaceholderText(tr("Search by name, type, or path..."));
    m_impl->search->setToolTip(
        tr("Filter recent files by name, type, or path. Supports partial matches.\nSearch recent files (Ctrl+F)"));
    m_impl->search->setAccessibleName(tr("Search recent files"));
    m_impl->search->setClearButtonEnabled(true);
    m_impl->search->setFixedWidth(parts::kHomeSearchWidth);
    m_impl->searchIcon = m_impl->search->addAction(QIcon(), QLineEdit::LeadingPosition);
    header->addWidget(m_impl->search, 0, Qt::AlignVCenter);

    auto* sortLabel = new HomeText(HomeText::Role::Caption, tr("Sort"), this);
    sortLabel->setElideMode(Qt::ElideNone);
    header->addWidget(sortLabel, 0, Qt::AlignVCenter);
    m_impl->sort = new QComboBox(this);
    m_impl->sort->setObjectName(QStringLiteral("homeRecentSort"));
    m_impl->sort->setToolTip(tr("Sort recent files by date, name, or file type"));
    m_impl->sort->setAccessibleName(tr("Sort recent files"));
    m_impl->sort->addItem(tr("Recent"), DashboardPreferences::RECENT_SORT_RECENT);
    m_impl->sort->addItem(tr("Name"), DashboardPreferences::RECENT_SORT_NAME);
    m_impl->sort->addItem(tr("Type"), DashboardPreferences::RECENT_SORT_TYPE);
    sortLabel->setBuddy(m_impl->sort);
    header->addWidget(m_impl->sort, 0, Qt::AlignVCenter);

    m_impl->grid = new QToolButton(this);
    m_impl->grid->setObjectName(QStringLiteral("homeRecentGridButton"));
    m_impl->grid->setAccessibleName(tr("Show as a grid"));
    m_impl->grid->setToolTip(tr("Show as a grid"));
    m_impl->list = new QToolButton(this);
    m_impl->list->setObjectName(QStringLiteral("homeRecentListButton"));
    m_impl->list->setAccessibleName(tr("Show as a list"));
    m_impl->list->setToolTip(tr("Show as a list"));
    auto* arrangement = new QButtonGroup(this);
    arrangement->setExclusive(true);
    for (QToolButton* button : {m_impl->grid, m_impl->list})
    {
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        arrangement->addButton(button);
        header->addWidget(button, 0, Qt::AlignVCenter);
    }
    m_impl->grid->setChecked(true);
    QObject::connect(m_impl->grid, &QToolButton::clicked, this, [this]() { setArrangement(Arrangement::Grid); });
    QObject::connect(m_impl->list, &QToolButton::clicked, this, [this]() { setArrangement(Arrangement::List); });
    column->addLayout(header);

    m_impl->area = new TileArea(this);
    m_impl->area->setObjectName(QStringLiteral("homeRecentTiles"));
    column->addWidget(m_impl->area);
    m_impl->noMatch = new HomeText(HomeText::Role::Body, tr("No recent files match the search."), this);
    m_impl->noMatch->setObjectName(QStringLiteral("homeRecentNoMatch"));
    m_impl->noMatch->hide();
    column->addWidget(m_impl->noMatch);

    // The search and the sort are the dashboard's settings.
    m_impl->settle = new QTimer(this);
    m_impl->settle->setSingleShot(true);
    m_impl->settle->setInterval(kSearchSettleMs);
    QObject::connect(m_impl->settle, &QTimer::timeout, this, [this]() { refreshOrder(); });
    {
        const QSignalBlocker blockSearch(m_impl->search);
        const QSignalBlocker blockSort(m_impl->sort);
        m_impl->search->setText(loadRecentSearchText());
        m_impl->sort->setCurrentIndex((std::max)(0, m_impl->sort->findData(loadRecentSortMode())));
    }
    // The grid or the list, as it was last chosen; no tile is made yet.
    m_impl->arrangement = loadRecentArrangement() == DashboardPreferences::RECENT_ARRANGEMENT_LIST ? Arrangement::List
                                                                                                   : Arrangement::Grid;
    m_impl->grid->setChecked(m_impl->arrangement == Arrangement::Grid);
    m_impl->list->setChecked(m_impl->arrangement == Arrangement::List);
    QObject::connect(m_impl->search, &QLineEdit::textChanged, this, [this]() {
        storeRecentSearchText(m_impl->search->text());
        m_impl->settle->start();
    });
    QObject::connect(m_impl->sort, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        storeRecentSortMode(m_impl->sort->currentData().toInt());
        refreshOrder();
    });
    auto* find = new QShortcut(QKeySequence(QStringLiteral("Ctrl+F")), this);
    QObject::connect(find, &QShortcut::activated, this, [this]() { focusSearch(); });

    refreshIcons();
}

HomeRecentGrid::~HomeRecentGrid() = default;

void HomeRecentGrid::setThumbnailCache(DashboardThumbnailCache* cache)
{
    m_impl->cache = cache != nullptr ? cache : m_impl->ownCache.get();
}

void HomeRecentGrid::setFactsProbe(HomeRecentProbe probe)
{
    m_impl->probe = std::move(probe);
}

void HomeRecentGrid::setFileReveal(FileRevealFunction reveal)
{
    m_impl->reveal = std::move(reveal);
}

void HomeRecentGrid::setRecentFiles(const std::deque<QString>& recentFiles)
{
    const QSet<QString> pinned = loadPinnedRecentFiles();
    // The pins of files no longer recent go, as the dashboard drops them.
    QSet<QString> kept;
    for (const QString& path : recentFiles)
    {
        if (pinned.contains(path))
        {
            kept.insert(path);
        }
    }
    if (kept != pinned)
    {
        storePinnedRecentFiles(kept);
    }
    const bool same = recentFiles == m_impl->recent && kept == m_impl->pinned;
    m_impl->recent = recentFiles;
    m_impl->pinned = kept;

    // A tile per recent file; a file no longer recent loses its tile.
    QSet<QString> present;
    for (const QString& path : recentFiles)
    {
        present.insert(path);
        if (!m_impl->tiles.contains(path))
        {
            auto* tile = new HomeRecentTile(path, m_impl->area);
            tile->setShape(m_impl->arrangement == Arrangement::List ? HomeRecentTile::Shape::Row
                                                                    : HomeRecentTile::Shape::Tile);
            tile->hide();
            QObject::connect(tile, &HomeRecentTile::openRequested, this, [this](const QString& file) {
                recordDashboardLastAction(dashboardWords("Open recent file"));
                emit openFileRequested(file);
            });
            QObject::connect(tile, &HomeRecentTile::pinRequested, this,
                             [this](const QString& file, const bool pin) { setPinned(file, pin); });
            QObject::connect(tile, &HomeRecentTile::revealRequested, this,
                             [this](const QString& file) { (void)showInFileBrowser(window(), file, m_impl->reveal); });
            QObject::connect(tile, &HomeRecentTile::removeRequested, this, [this](const QString& file) {
                recordDashboardLastAction(dashboardWords("Remove recent file"));
                setPinned(file, false);
                emit removeFileRequested(file);
            });
            m_impl->tiles.insert(path, tile);
        }
    }
    for (auto it = m_impl->tiles.begin(); it != m_impl->tiles.end();)
    {
        if (!present.contains(it.key()))
        {
            it.value()->deleteLater();
            it = m_impl->tiles.erase(it);
        }
        else
        {
            ++it;
        }
    }
    // Each tile reads its thumbnail again when the thumbnail changed since it was read.
    for (HomeRecentTile* tile : std::as_const(m_impl->tiles))
    {
        read(*tile);
    }
    if (!same || m_impl->order.isEmpty())
    {
        refreshOrder();
    }
}

void HomeRecentGrid::refreshIcons()
{
    m_impl->searchIcon->setIcon(providedIcon(QStringLiteral("search")));
    m_impl->grid->setIcon(providedIcon(QStringLiteral("view_grid")));
    m_impl->list->setIcon(providedIcon(QStringLiteral("view_list")));
    for (HomeRecentTile* tile : std::as_const(m_impl->tiles))
    {
        tile->refreshIcons();
    }
}

void HomeRecentGrid::focusSearch()
{
    if (m_impl->search->isVisible())
    {
        m_impl->search->setFocus(Qt::ShortcutFocusReason);
        m_impl->search->selectAll();
    }
}

void HomeRecentGrid::setArrangement(const Arrangement arrangement)
{
    m_impl->grid->setChecked(arrangement == Arrangement::Grid);
    m_impl->list->setChecked(arrangement == Arrangement::List);
    if (m_impl->arrangement == arrangement)
    {
        return;
    }
    m_impl->arrangement = arrangement;
    storeRecentArrangement(arrangement == Arrangement::List ? DashboardPreferences::RECENT_ARRANGEMENT_LIST
                                                            : DashboardPreferences::RECENT_ARRANGEMENT_GRID);
    const HomeRecentTile::Shape shape =
        arrangement == Arrangement::List ? HomeRecentTile::Shape::Row : HomeRecentTile::Shape::Tile;
    for (HomeRecentTile* tile : std::as_const(m_impl->tiles))
    {
        tile->setShape(shape);
    }
    m_impl->area->setTiles(tiles(), arrangement);
}

HomeRecentGrid::Arrangement HomeRecentGrid::arrangement() const noexcept
{
    return m_impl->arrangement;
}

QStringList HomeRecentGrid::visibleFileOrder() const
{
    return m_impl->order;
}

QList<HomeRecentTile*> HomeRecentGrid::tiles() const
{
    QList<HomeRecentTile*> shown;
    for (const QString& path : m_impl->order)
    {
        if (HomeRecentTile* tile = m_impl->tiles.value(path, nullptr))
        {
            shown << tile;
        }
    }
    return shown;
}

QString HomeRecentGrid::countText() const
{
    return m_impl->count->text();
}

QLineEdit* HomeRecentGrid::searchField() const
{
    return m_impl->search;
}

QComboBox* HomeRecentGrid::sortBox() const
{
    return m_impl->sort;
}

QAbstractButton* HomeRecentGrid::gridButton() const
{
    return m_impl->grid;
}

QAbstractButton* HomeRecentGrid::listButton() const
{
    return m_impl->list;
}

void HomeRecentGrid::refreshOrder()
{
    m_impl->settle->stop();
    const QString search = m_impl->search->text().trimmed();
    m_impl->order = DashboardUtils::visibleRecentFileOrder(m_impl->recent, search, m_impl->sort->currentData().toInt(),
                                                           m_impl->pinned);
    QSet<QString> shown(m_impl->order.cbegin(), m_impl->order.cend());
    for (auto it = m_impl->tiles.cbegin(); it != m_impl->tiles.cend(); ++it)
    {
        it.value()->setPinned(m_impl->pinned.contains(it.key()));
        it.value()->setVisible(shown.contains(it.key()));
    }
    m_impl->area->setTiles(tiles(), m_impl->arrangement);

    // The count names the images and the sequences of the whole list; with a search, the hint says how many it shows.
    int sequences = 0;
    for (const QString& path : m_impl->recent)
    {
        if (isHomeSequence(path))
        {
            ++sequences;
        }
    }
    const int images = static_cast<int>(m_impl->recent.size()) - sequences;
    QStringList count;
    if (images > 0 || sequences == 0)
    {
        count << (images == 1 ? tr("1 image") : tr("%1 images").arg(images));
    }
    if (sequences > 0)
    {
        count << (sequences == 1 ? tr("1 sequence") : tr("%1 sequences").arg(sequences));
    }
    m_impl->count->setText(count.join(QStringLiteral(" · ")));
    m_impl->hint->setText(search.isEmpty()
                              ? tr("Click a thumbnail to open. Right-click for quick actions.")
                              : tr("Showing %1 of %2").arg(m_impl->order.size()).arg(m_impl->recent.size()));
    m_impl->noMatch->setVisible(m_impl->order.isEmpty() && !m_impl->recent.empty());
}

void HomeRecentGrid::read(HomeRecentTile& tile)
{
    const QString path = tile.filePath();
    const QString thumbnail = homeThumbnailPath(path);
    const QFileInfo info(thumbnail);
    const qint64 stamp = !thumbnail.isEmpty() && info.isFile() ? info.lastModified().toMSecsSinceEpoch() : 0;
    if (tile.readStamp() == stamp)
    {
        return;
    }
    tile.setReadStamp(stamp);

    // A picture decoded already is taken from the cache; the facts are read again with it.
    const QString key = DashboardThumbnailCache::cacheKey(path, kTileDecodeWidth);
    QString toDecode = stamp != 0 ? thumbnail : QString();
    if (const DashboardThumbnailCache::Entry* cached = m_impl->cache->find(key);
        cached != nullptr && cached->lastModifiedMs == stamp && !cached->displayImage.isNull())
    {
        tile.setPicture(cached->displayImage);
        toDecode.clear();
    }
    QPointer<HomeRecentTile> target(&tile);
    readHomeRecentFile(&tile, path, toDecode, QSize(kTileDecodeWidth, kTileDecodeHeight), m_impl->probe,
                       [this, target, key, stamp](const QImage& picture, const HomeRecentFacts& facts) {
                           if (target.isNull() || target->readStamp() != stamp)
                           {
                               return;
                           }
                           if (!picture.isNull())
                           {
                               m_impl->cache->insert(key, picture, facts.size, stamp);
                               target->setPicture(picture);
                           }
                           target->setFacts(facts);
                           // A busy session left the facts of a file on disk unread: the tile reads again the next
                           // time the recent files are given, its picture from the cache.
                           if (facts.modified.isValid() && !facts.size.isValid() && !facts.frameCount.has_value())
                           {
                               target->setReadStamp(-1);
                           }
                       });
}

void HomeRecentGrid::setPinned(const QString& filePath, const bool pinned)
{
    const QString path = DashboardUtils::normalizeRecentFilePath(filePath);
    if (path.isEmpty())
    {
        return;
    }
    const bool changed = pinned ? !m_impl->pinned.contains(path) : m_impl->pinned.contains(path);
    if (!changed)
    {
        return;
    }
    if (pinned)
    {
        m_impl->pinned.insert(path);
    }
    else
    {
        m_impl->pinned.remove(path);
    }
    storePinnedRecentFiles(m_impl->pinned);
    emit pinToggled(path, pinned);
    refreshOrder();
}

} // namespace an::ui
