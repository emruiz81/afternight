// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "theme/NightImage.h"

#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QWidget>
#include <verdigris/wobjectdefs.h>

class QLabel;
class QPushButton;
class QVariantAnimation;
class QGraphicsOpacityEffect;
class QPropertyAnimation;

namespace an::ui
{

class AFTERNIGHTCONTROLS_API FeaturedFileCard : public QWidget
{
    W_OBJECT(FeaturedFileCard)

public:
    explicit FeaturedFileCard(QWidget* parent = nullptr);
    ~FeaturedFileCard() override = default;

    void setFeaturedFile(const QString& filePath, const QString& titleText, const QString& metadataText,
                         const QPixmap& backgroundPixmap, bool pinned);
    void clearFeaturedFile();
    void refreshTheme();

    void setPinIcons(const QIcon& pinIcon, const QIcon& unpinIcon);

    [[nodiscard]] QString currentFilePath() const;
    [[nodiscard]] bool isPinned() const;

signals:
    void continueRequested(const QString& filePath) W_SIGNAL(continueRequested, filePath);
    void pinToggled(const QString& filePath, bool pinned) W_SIGNAL(pinToggled, filePath, pinned);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    /// The picture behind the card at its size, and a softened copy of it; each painted in Night's form under Night.
    struct BackgroundLayer
    {
        QSize size;
        NightPicture cover;
        NightPicture softened;
    };

    static BackgroundLayer buildBackgroundLayer(const QPixmap& sourcePixmap, const QSize& targetSize);
    void updateBackgroundLayers();
    void updatePinButton();
    void animateFadeInIfNeeded(bool wasEmptyBeforeUpdate);

    QLabel* m_TitleLabel = nullptr;
    QLabel* m_MetadataLabel = nullptr;
    QPushButton* m_PinButton = nullptr;
    QPushButton* m_ContinueButton = nullptr;
    QGraphicsOpacityEffect* m_OpacityEffect = nullptr;
    QPropertyAnimation* m_FadeInAnimation = nullptr;
    QVariantAnimation* m_BackgroundCrossfadeAnimation = nullptr;

    QIcon m_PinIcon;
    QIcon m_UnpinIcon;

    QString m_CurrentFilePath;
    bool m_IsPinned = false;
    bool m_HasData = false;
    qreal m_BackgroundCrossfadeProgress = 1.0;

    QPixmap m_CurrentBackgroundSource;
    QPixmap m_PreviousBackgroundSource;
    BackgroundLayer m_CurrentBackgroundLayer;
    BackgroundLayer m_PreviousBackgroundLayer;
};

} // namespace an::ui
