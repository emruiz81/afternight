// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Home's recent images and sequences: the recent files as tiles - each its thumbnail with its format's tag, its name
/// and a line of facts, a sequence's tag with a film glyph and its facts with its frames - in a grid or a list, as last
/// chosen, searched and sorted in the order `DashboardUtils::visibleRecentFileOrder` gives, under the dashboard's
/// settings, with the pinned files first; a tile's click opens its file and its menu offers Open, Pin to top, Show in
/// File Browser, Copy path, Remove from recent files. A tile's picture and facts are read off the GUI thread.

#include "AfterNightControlsExport.h"
#include "app/ShowInFileBrowser.h"
#include "controls/HomeParts.h"

#include <QAbstractButton>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <verdigris/wobjectdefs.h>

#include <deque>
#include <memory>

class DashboardThumbnailCache;
class QComboBox;
class QContextMenuEvent;
class QKeyEvent;
class QLineEdit;
class QMenu;

namespace an::ui
{

/// One recent file on Home: a button that opens it, painted as a tile of the grid or a row of the list.
class AFTERNIGHTCONTROLS_API HomeRecentTile : public QAbstractButton
{
    W_OBJECT(HomeRecentTile)

public:
    /// How the tile is laid out.
    enum class Shape
    {
        Tile,
        Row
    };

    HomeRecentTile(const QString& filePath, QWidget* parent = nullptr);
    ~HomeRecentTile() override;

    [[nodiscard]] QString filePath() const;
    [[nodiscard]] bool isSequence() const;
    [[nodiscard]] QString nameText() const;
    [[nodiscard]] QString typeText() const;
    [[nodiscard]] QString factsText() const;
    [[nodiscard]] bool isPinned() const;
    [[nodiscard]] bool hasPicture() const;

    void setShape(Shape shape);
    void setPinned(bool pinned);
    void setPicture(const QImage& picture);
    void setFacts(const HomeRecentFacts& facts);

    /// Takes the icons the application's icon provider gives now: a sequence's film glyph.
    void refreshIcons();

    /// Fills a menu with the tile's entries: Open, Pin to top or Unpin from top, Show in File Browser, Copy path and
    /// Remove from recent files.
    void populateMenu(QMenu& menu);

    /// The stamp of the thumbnail the tile was read from: its last change, in milliseconds.
    [[nodiscard]] qint64 readStamp() const noexcept;
    void setReadStamp(qint64 stamp) noexcept;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

    void openRequested(const QString& filePath) W_SIGNAL(openRequested, filePath);
    void pinRequested(const QString& filePath, bool pinned) W_SIGNAL(pinRequested, filePath, pinned);
    void revealRequested(const QString& filePath) W_SIGNAL(revealRequested, filePath);
    void removeRequested(const QString& filePath) W_SIGNAL(removeRequested, filePath);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    /// Places the texts and the tag for the shape.
    void arrange();

    /// Says what the tile is to assistive technology.
    void describe();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

class AFTERNIGHTCONTROLS_API HomeRecentGrid : public QWidget
{
    W_OBJECT(HomeRecentGrid)

public:
    /// How the recent files are shown.
    enum class Arrangement
    {
        Grid,
        List
    };

    explicit HomeRecentGrid(QWidget* parent = nullptr);
    ~HomeRecentGrid() override;

    /// The cache of decoded thumbnails the recent files share with the featured image; null keeps the grid's own.
    void setThumbnailCache(DashboardThumbnailCache* cache);

    /// What reads a recent file's facts; `probeHomeRecentFile` unless set.
    void setFactsProbe(HomeRecentProbe probe);

    /// What shows a file in the system's file browser; `revealInFileManager` unless set.
    void setFileReveal(FileRevealFunction reveal);

    /// The recent files, most recent first and normalized. The pinned files are read from the settings again.
    void setRecentFiles(const std::deque<QString>& recentFiles);

    /// Takes the icons the application's icon provider gives now.
    void refreshIcons();

    /// Puts the search field's text in the keyboard's focus, selected.
    void focusSearch();

    /// Shows the recent files as a grid or a list, and keeps the choice in the dashboard's settings: the next Home
    /// shows them so.
    void setArrangement(Arrangement arrangement);
    [[nodiscard]] Arrangement arrangement() const noexcept;

    /// The recent files the tiles show, in their order.
    [[nodiscard]] QStringList visibleFileOrder() const;

    /// The tiles shown, in their order.
    [[nodiscard]] QList<HomeRecentTile*> tiles() const;

    /// What the count beside the title says: "7 images · 1 sequence".
    [[nodiscard]] QString countText() const;

    [[nodiscard]] QLineEdit* searchField() const;
    [[nodiscard]] QComboBox* sortBox() const;
    [[nodiscard]] QAbstractButton* gridButton() const;
    [[nodiscard]] QAbstractButton* listButton() const;

    void openFileRequested(const QString& filePath) W_SIGNAL(openFileRequested, filePath);
    void removeFileRequested(const QString& filePath) W_SIGNAL(removeFileRequested, filePath);
    void pinToggled(const QString& filePath, bool pinned) W_SIGNAL(pinToggled, filePath, pinned);

private:
    /// Orders the tiles again for the search, the sort and the pins, making and dropping tiles as the files go.
    void refreshOrder();

    /// Reads a tile's picture and facts off the GUI thread, unless its thumbnail has not changed since it was read.
    void read(HomeRecentTile& tile);

    /// Pins a file to the top or unpins it, and keeps the pins in the settings.
    void setPinned(const QString& filePath, bool pinned);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
