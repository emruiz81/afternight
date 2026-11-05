// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QWidget>
#include <QtWidgets/QLabel>
#include <QContextMenuEvent>
#include <QFrame>
#include <QPaintEvent>
#include <QVBoxLayout>
#include <functional>

class QGraphicsOpacityEffect;
class QPropertyAnimation;

class AFTERNIGHTCONTROLS_API ImageThumbnailWidget : public QWidget
{
public:
    using ThumbnailPathResolver = std::function<QString(const QString&)>;
    using OpenFileCallback = std::function<void(const QString&)>;
    using RemoveRecentCallback = std::function<void(const QString&)>;
    using TogglePinnedCallback = std::function<void(const QString&, bool)>;

    ImageThumbnailWidget(ThumbnailPathResolver thumbnailPathResolver, OpenFileCallback openFileCallback,
                         RemoveRecentCallback removeRecentCallback, bool darkMode, QWidget* parent,
                         TogglePinnedCallback togglePinnedCallback = {}, bool pinned = false);

    bool loadImage(const QString& fileName);
    bool loadImageFromData(const QString& fileName, const QPixmap& displayPixmap, const QSize& sourceSize = QSize());

    /// Override the metadata label text (e.g. with extracted astro
    /// metadata such as "M42 · Ha · 300s").  Must be called after
    /// loadImage / loadImageFromData.
    void setMetadataText(const QString& text);

    void setColors(const QColor& background, const QColor& backgroundHighlight);
    void setPinned(bool pinned);
    [[nodiscard]] bool isPinned() const;

    /// Pins the file to the top of the recent files, or unpins it, and tells the owner: what the context menu's pin
    /// action does. Nothing happens when the owner gave no callback.
    void togglePinned();

    /// Refresh colors based on current theme (dark/light mode)
    void refreshColors(bool darkMode);

    ~ImageThumbnailWidget();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    bool applyLoadedThumbnail(const QString& fileName, const QPixmap& displayPixmap, const QSize& sourceSize);
    void updateDisplayedPixmap();
    void refreshBackground();

    QPixmap m_Pixmap;

    bool m_IsActive = false;

    QColor m_BackgroundColor;
    QColor m_BackgroundHighlightColor;
    QColor m_BorderColor;
    QColor m_BorderHighlightColor;

    QFrame* m_PreviewFrame = nullptr;
    QLabel* m_MainImage = nullptr;
    QFrame* m_OverlayFrame = nullptr;
    QLabel* m_DimensionsChipLabel = nullptr;
    QLabel* m_TypeChipLabel = nullptr;
    QLabel* m_NameLabel = nullptr;
    QLabel* m_MetadataLabel = nullptr;

    QVBoxLayout* m_Layout = nullptr;

    ThumbnailPathResolver m_ThumbnailPathResolver;
    OpenFileCallback m_OpenFileCallback;
    RemoveRecentCallback m_RemoveRecentCallback;
    TogglePinnedCallback m_TogglePinnedCallback;

    QGraphicsOpacityEffect* m_OpacityEffect = nullptr;
    QPropertyAnimation* m_FadeInAnimation = nullptr;
    QColor m_ShadowColor;
    bool m_DarkMode = false;

    QString m_OriginalFilePath;
    bool m_IsPinned = false;
    // std::string m_ThumbnailFilePath;
    // time_t m_LastFileModification = 0;
};
