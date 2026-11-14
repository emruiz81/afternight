// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "widgets/FeaturedFileCard.h"

#include "theme/AnimationUtils.h"
#include "theme/AnDesignTokens.h"
#include "theme/PainterGuard.h"

#include <QDir>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyle>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <cmath>

W_OBJECT_IMPL(an::ui::FeaturedFileCard)

namespace an::ui
{

namespace
{
[[nodiscard]] bool isLightSurface(const QColor& color)
{
    // Perceptual luminance approximation in sRGB space.
    const int weightedLuma = (color.red() * 299) + (color.green() * 587) + (color.blue() * 114);
    return (weightedLuma / 1000) >= 140;
}

[[nodiscard]] QColor withAlpha(const QColor& color, const int alpha)
{
    QColor c = color;
    c.setAlpha(std::clamp(alpha, 0, 255));
    return c;
}
} // namespace

FeaturedFileCard::FeaturedFileCard(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("dashboardFeaturedFileCard"));
    setProperty("dashboardFeaturedFilePath", QString());
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(0);
    topRow->addStretch(1);

    m_PinButton = new QPushButton(this);
    m_PinButton->setObjectName(QStringLiteral("dashboardFeaturedPinButton"));
    m_PinButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    m_PinButton->setCursor(Qt::PointingHandCursor);
    m_PinButton->setFixedSize(32, 32);
    m_PinButton->setIconSize(QSize(18, 18));
    m_PinButton->setFlat(true);
    topRow->addWidget(m_PinButton, 0, Qt::AlignTop | Qt::AlignRight);
    rootLayout->addLayout(topRow);

    rootLayout->addStretch(1);

    m_TitleLabel = new QLabel(this);
    m_TitleLabel->setObjectName(QStringLiteral("dashboardFeaturedFileTitle"));
    m_TitleLabel->setWordWrap(false);
    rootLayout->addWidget(m_TitleLabel);

    m_MetadataLabel = new QLabel(this);
    m_MetadataLabel->setObjectName(QStringLiteral("dashboardFeaturedFileMetadata"));
    m_MetadataLabel->setWordWrap(true);
    rootLayout->addWidget(m_MetadataLabel);

    auto* ctaRow = new QHBoxLayout();
    ctaRow->setContentsMargins(0, 0, 0, 0);
    ctaRow->setSpacing(0);

    m_ContinueButton = new QPushButton(tr("Continue"), this);
    m_ContinueButton->setObjectName(QStringLiteral("dashboardFeaturedContinueButton"));
    m_ContinueButton->setProperty("anButtonVariant", QStringLiteral("primary"));
    m_ContinueButton->setCursor(Qt::PointingHandCursor);
    ctaRow->addWidget(m_ContinueButton, 0, Qt::AlignLeft | Qt::AlignBottom);
    ctaRow->addStretch(1);
    rootLayout->addLayout(ctaRow);

    connect(m_ContinueButton, &QPushButton::clicked, this, [this]() {
        if (m_CurrentFilePath.trimmed().isEmpty())
        {
            return;
        }

        emit continueRequested(m_CurrentFilePath);
    });

    connect(m_PinButton, &QPushButton::clicked, this, [this]() {
        if (m_CurrentFilePath.trimmed().isEmpty())
        {
            return;
        }

        m_IsPinned = !m_IsPinned;
        updatePinButton();
        emit pinToggled(m_CurrentFilePath, m_IsPinned);
    });

    m_OpacityEffect = new QGraphicsOpacityEffect(this);
    m_OpacityEffect->setOpacity(1.0);
    m_OpacityEffect->setEnabled(false);
    setGraphicsEffect(m_OpacityEffect);

    m_FadeInAnimation = new QPropertyAnimation(m_OpacityEffect, "opacity", this);
    m_FadeInAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_FadeInAnimation->setDuration(AnimationUtils::duration(300));
    connect(m_FadeInAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (!m_OpacityEffect)
        {
            return;
        }

        m_OpacityEffect->setOpacity(1.0);
        m_OpacityEffect->setEnabled(false);
    });

    m_BackgroundCrossfadeAnimation = new QVariantAnimation(this);
    m_BackgroundCrossfadeAnimation->setEasingCurve(QEasingCurve::InOutCubic);
    m_BackgroundCrossfadeAnimation->setDuration(AnimationUtils::duration(300));
    m_BackgroundCrossfadeAnimation->setStartValue(0.0);
    m_BackgroundCrossfadeAnimation->setEndValue(1.0);

    connect(m_BackgroundCrossfadeAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_BackgroundCrossfadeProgress = std::clamp(value.toReal(), 0.0, 1.0);
        update();
    });

    connect(m_BackgroundCrossfadeAnimation, &QVariantAnimation::finished, this, [this]() {
        if (m_BackgroundCrossfadeProgress >= 1.0)
        {
            m_PreviousBackgroundSource = QPixmap();
            m_PreviousBackgroundLayer = BackgroundLayer();
        }
    });

    refreshTheme();
    clearFeaturedFile();
}

void FeaturedFileCard::setFeaturedFile(const QString& filePath, const QString& titleText, const QString& metadataText,
                                       const QPixmap& backgroundPixmap, const bool pinned)
{
    if (filePath.trimmed().isEmpty() || backgroundPixmap.isNull())
    {
        clearFeaturedFile();
        return;
    }

    const bool wasEmpty = !m_HasData;
    const bool canCrossfade = !wasEmpty && !m_CurrentBackgroundSource.isNull() && m_CurrentFilePath != filePath;

    if (canCrossfade)
    {
        m_PreviousBackgroundSource = m_CurrentBackgroundSource;
        m_PreviousBackgroundLayer = m_CurrentBackgroundLayer;
    }
    else
    {
        m_PreviousBackgroundSource = QPixmap();
        m_PreviousBackgroundLayer = BackgroundLayer();
    }

    m_CurrentFilePath = filePath;
    m_IsPinned = pinned;
    m_HasData = true;
    setProperty("dashboardFeaturedFilePath", m_CurrentFilePath);

    m_TitleLabel->setText(titleText.trimmed());
    m_TitleLabel->setToolTip(QDir::toNativeSeparators(filePath));
    m_MetadataLabel->setText(metadataText.trimmed());
    m_MetadataLabel->setToolTip(QDir::toNativeSeparators(filePath));
    m_CurrentBackgroundSource = backgroundPixmap;
    m_CurrentBackgroundLayer = BackgroundLayer();
    updateBackgroundLayers();
    updatePinButton();

    if (canCrossfade && AnimationUtils::animationsEnabled())
    {
        m_BackgroundCrossfadeAnimation->stop();
        m_BackgroundCrossfadeAnimation->setDuration(AnimationUtils::duration(300));
        m_BackgroundCrossfadeAnimation->setStartValue(0.0);
        m_BackgroundCrossfadeAnimation->setEndValue(1.0);
        m_BackgroundCrossfadeProgress = 0.0;
        m_BackgroundCrossfadeAnimation->start();
    }
    else
    {
        m_BackgroundCrossfadeAnimation->stop();
        m_BackgroundCrossfadeProgress = 1.0;
    }

    animateFadeInIfNeeded(wasEmpty);
    update();
}

void FeaturedFileCard::clearFeaturedFile()
{
    m_CurrentFilePath.clear();
    m_IsPinned = false;
    m_HasData = false;
    setProperty("dashboardFeaturedFilePath", QString());

    if (m_TitleLabel)
    {
        m_TitleLabel->clear();
        m_TitleLabel->setToolTip({});
    }
    if (m_MetadataLabel)
    {
        m_MetadataLabel->clear();
        m_MetadataLabel->setToolTip({});
    }

    m_BackgroundCrossfadeAnimation->stop();
    m_BackgroundCrossfadeProgress = 1.0;
    m_CurrentBackgroundSource = QPixmap();
    m_PreviousBackgroundSource = QPixmap();
    m_CurrentBackgroundLayer = BackgroundLayer();
    m_PreviousBackgroundLayer = BackgroundLayer();

    if (m_OpacityEffect)
    {
        m_OpacityEffect->setOpacity(1.0);
        m_OpacityEffect->setEnabled(false);
    }

    updatePinButton();
    update();
}

void FeaturedFileCard::refreshTheme()
{
    const auto& tokenSet = tokens();

    // Account for painted drop shadow margins so child widgets stay inside
    // the visible card area.
    constexpr int shadowMargin = 4; // must match shadowBlur in paintEvent
    constexpr int shadowBottomExtra = 2; // must match shadowOffset in paintEvent

    if (auto* rootLayout = qobject_cast<QVBoxLayout*>(layout()))
    {
        rootLayout->setContentsMargins(tokenSet.spacing.lg + shadowMargin, tokenSet.spacing.md + shadowMargin,
                                       tokenSet.spacing.lg + shadowMargin,
                                       tokenSet.spacing.md + shadowMargin + shadowBottomExtra);
        rootLayout->setSpacing(tokenSet.spacing.sm);
    }

    // Text overlays always use white for legibility over the photographic
    // background.  The painted gradient scrim always darkens the lower portion
    // of the card in both light and dark themes.  We must NOT use
    // textOnAccent here: that token targets the accent button fill colour
    // and resolves to black in dark themes where the accent is a light hue,
    // which would make labels invisible against the dark scrim.
    const QColor overlayTextPrimary = withAlpha(QColor(Qt::white), 240);
    const QColor overlayTextSecondary = withAlpha(QColor(Qt::white), 190);

    if (m_TitleLabel)
    {
        m_TitleLabel->setFont(tokenSet.typography.heading2);
        QPalette titlePalette = m_TitleLabel->palette();
        titlePalette.setColor(QPalette::WindowText, overlayTextPrimary);
        m_TitleLabel->setPalette(titlePalette);
    }

    if (m_MetadataLabel)
    {
        m_MetadataLabel->setFont(tokenSet.typography.caption);
        QPalette metadataPalette = m_MetadataLabel->palette();
        metadataPalette.setColor(QPalette::WindowText, overlayTextSecondary);
        m_MetadataLabel->setPalette(metadataPalette);
    }

    if (m_ContinueButton)
    {
        m_ContinueButton->setFont(tokenSet.typography.body);
        if (QStyle* styleHandle = m_ContinueButton->style())
        {
            styleHandle->unpolish(m_ContinueButton);
            styleHandle->polish(m_ContinueButton);
        }
    }

    if (m_PinButton)
    {
        if (QStyle* styleHandle = m_PinButton->style())
        {
            styleHandle->unpolish(m_PinButton);
            styleHandle->polish(m_PinButton);
        }
    }

    updatePinButton();
    update();
}

QString FeaturedFileCard::currentFilePath() const
{
    return m_CurrentFilePath;
}

bool FeaturedFileCard::isPinned() const
{
    return m_IsPinned;
}

void FeaturedFileCard::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const auto& tokenSet = tokens();
    QRectF bounds = QRectF(rect());
    const qreal cornerRadius = static_cast<qreal>(tokenSet.radius.lg);

    // Paint a drop shadow for elevation
    const bool isLight = isLightSurface(tokenSet.colors.bgPrimary);
    const int shadowBlur = 4;
    const int shadowOffset = 2;
    const QColor shadowColor = withAlpha(tokenSet.colors.shadowOverlay, isLight ? 20 : 40);

    QRectF shadowBounds = bounds.adjusted(shadowBlur, shadowBlur + shadowOffset, -shadowBlur, -shadowBlur);

    painter.setPen(Qt::NoPen);
    QColor c = shadowColor;
    for (int i = 0; i < shadowBlur; ++i)
    {
        c.setAlphaF(shadowColor.alphaF() * (1.0 - static_cast<qreal>(i) / shadowBlur));
        painter.setBrush(c);
        painter.drawRoundedRect(shadowBounds.adjusted(-i, -i, i, i), cornerRadius + i, cornerRadius + i);
    }

    bounds.adjust(shadowBlur, shadowBlur, -shadowBlur, -shadowBlur - shadowOffset);

    QPainterPath clipPath;
    clipPath.addRoundedRect(bounds, cornerRadius, cornerRadius);
    painter.setClipPath(clipPath);
    painter.fillRect(bounds, tokenSet.colors.bgCardPrimary);

    // Under Night each picture's Night form, painted once it is made.
    const auto repaint = [this]() { update(); };
    auto drawLayer = [this, &painter, bounds, &repaint](const BackgroundLayer& layer, const qreal opacity) {
        const QPixmap cover = layer.cover.shown(this, repaint);
        if (cover.isNull() || opacity <= 0.0)
        {
            return;
        }

        an::ui::PainterGuard painterGuard(&painter);
        painter.setOpacity(std::clamp(opacity, 0.0, 1.0));
        // Use bounds.topLeft() for positioning to respect shadow margins
        painter.drawPixmap(bounds.topLeft(), cover);
        if (const QPixmap softened = layer.softened.shown(this, repaint); !softened.isNull())
        {
            painter.setOpacity(std::clamp(opacity * 0.30, 0.0, 1.0));
            painter.drawPixmap(bounds.topLeft(), softened);
        }
    };

    if (m_HasData && !m_CurrentBackgroundLayer.cover.isNull())
    {
        if (!m_PreviousBackgroundLayer.cover.isNull() && m_BackgroundCrossfadeProgress < 1.0)
        {
            drawLayer(m_PreviousBackgroundLayer, 1.0 - m_BackgroundCrossfadeProgress);
        }
        drawLayer(m_CurrentBackgroundLayer,
                  m_PreviousBackgroundLayer.cover.isNull() ? 1.0 : m_BackgroundCrossfadeProgress);

        // Add subtle radial vignette for photographic depth (lighter in light mode)
        const int vignetteAlpha = isLight ? 50 : 100;
        QRadialGradient vignette(bounds.center(), (std::max)(bounds.width(), bounds.height()) / 1.5);
        vignette.setColorAt(0.0, withAlpha(tokenSet.colors.shadowOverlay, 0));
        vignette.setColorAt(1.0, withAlpha(tokenSet.colors.shadowOverlay, vignetteAlpha));
        painter.fillPath(clipPath, vignette);
    }

    // Bottom-up gradient scrim: stays transparent at the top so the image
    // shows clearly, then ramps up to a heavy dark band in the lower portion
    // where the title, metadata, and CTA labels live.  Strong enough that
    // the white labels remain legible over bright astronomical subjects
    // (bright nebulae, galaxies, star-fields).
    QLinearGradient gradient(bounds.topLeft(), bounds.bottomLeft());
    gradient.setColorAt(0.00, withAlpha(tokenSet.colors.shadowOverlay, 0));
    gradient.setColorAt(0.30, withAlpha(tokenSet.colors.shadowOverlay, 0));
    gradient.setColorAt(0.50, withAlpha(tokenSet.colors.shadowOverlay, 75));
    gradient.setColorAt(0.68, withAlpha(tokenSet.colors.shadowOverlay, 185));
    gradient.setColorAt(1.00, withAlpha(tokenSet.colors.shadowOverlay, 240));
    painter.fillPath(clipPath, gradient);

    // Secondary text-area scrim: anchored dynamically to the title label's
    // Y position so it always sits directly behind the text content.  This
    // compounds with the main gradient to ensure WCAG-AA contrast even when
    // a bright image region falls right at the card bottom.
    if (m_HasData && m_TitleLabel)
    {
        const int titleTop = m_TitleLabel->y() - tokenSet.spacing.md;
        const qreal startFrac =
            std::clamp(static_cast<qreal>(titleTop - static_cast<int>(bounds.top())) / bounds.height(), 0.0, 1.0);
        QLinearGradient textScrim(bounds.topLeft(), bounds.bottomLeft());
        textScrim.setColorAt((std::max)(0.0, startFrac - 0.06), withAlpha(tokenSet.colors.shadowOverlay, 0));
        textScrim.setColorAt((std::min)(1.0, startFrac + 0.06), withAlpha(tokenSet.colors.shadowOverlay, 100));
        textScrim.setColorAt(1.0, withAlpha(tokenSet.colors.shadowOverlay, 160));
        painter.fillPath(clipPath, textScrim);
    }

    painter.setClipping(false);
    painter.setPen(QPen(tokenSet.colors.borderSubtle, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), cornerRadius, cornerRadius);
}

void FeaturedFileCard::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateBackgroundLayers();
}

FeaturedFileCard::BackgroundLayer FeaturedFileCard::buildBackgroundLayer(const QPixmap& sourcePixmap,
                                                                         const QSize& targetSize)
{
    BackgroundLayer layer;
    layer.size = targetSize;

    if (sourcePixmap.isNull() || !targetSize.isValid() || targetSize.width() <= 0 || targetSize.height() <= 0)
    {
        return layer;
    }

    const QPixmap expanded = sourcePixmap.scaled(targetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (expanded.isNull())
    {
        return layer;
    }

    const int offsetX = (std::max)(0, (expanded.width() - targetSize.width()) / 2);
    const int offsetY = (std::max)(0, (expanded.height() - targetSize.height()) / 2);
    layer.cover.setPicture(expanded.copy(offsetX, offsetY, targetSize.width(), targetSize.height()));

    // Build a low-res softened version: scale down to 64px wide (preserving
    // aspect ratio, clamped to min 24px height), then scale back up.  The
    // (std::max)(1, ...) guard prevents division by zero when targetSize.width()
    // is 0, which cannot happen after the early return above but keeps the
    // arithmetic safe against future callers.
    const int blurredWidth = 64;
    const int blurredHeight =
        (std::max)(24, static_cast<int>(
                           std::lround(static_cast<double>(targetSize.height()) * static_cast<double>(blurredWidth) /
                                       static_cast<double>((std::max)(1, targetSize.width())))));
    const QPixmap small =
        layer.cover.picture().scaled(blurredWidth, blurredHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    layer.softened.setPicture(small.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    return layer;
}

void FeaturedFileCard::updateBackgroundLayers()
{
    const QSize targetSize = size();
    if (!targetSize.isValid() || targetSize.width() <= 0 || targetSize.height() <= 0)
    {
        return;
    }

    if (!m_CurrentBackgroundSource.isNull() &&
        (m_CurrentBackgroundLayer.size != targetSize || m_CurrentBackgroundLayer.cover.isNull()))
    {
        m_CurrentBackgroundLayer = buildBackgroundLayer(m_CurrentBackgroundSource, targetSize);
    }

    if (!m_PreviousBackgroundSource.isNull() &&
        (m_PreviousBackgroundLayer.size != targetSize || m_PreviousBackgroundLayer.cover.isNull()))
    {
        m_PreviousBackgroundLayer = buildBackgroundLayer(m_PreviousBackgroundSource, targetSize);
    }
}

void FeaturedFileCard::updatePinButton()
{
    if (!m_PinButton)
    {
        return;
    }

    m_PinButton->setEnabled(!m_CurrentFilePath.trimmed().isEmpty());
    m_PinButton->setText({});
    m_PinButton->setToolTip(m_IsPinned ? tr("Unpin featured file") : tr("Pin featured file"));
    m_PinButton->setIcon(m_IsPinned ? m_UnpinIcon : m_PinIcon);
}

void FeaturedFileCard::setPinIcons(const QIcon& pinIcon, const QIcon& unpinIcon)
{
    m_PinIcon = pinIcon;
    m_UnpinIcon = unpinIcon;
    updatePinButton();
}

void FeaturedFileCard::animateFadeInIfNeeded(const bool wasEmptyBeforeUpdate)
{
    if (!m_OpacityEffect)
    {
        return;
    }

    if (!wasEmptyBeforeUpdate || !AnimationUtils::animationsEnabled())
    {
        m_OpacityEffect->setOpacity(1.0);
        m_OpacityEffect->setEnabled(false);
        return;
    }

    if (m_FadeInAnimation)
    {
        m_FadeInAnimation->stop();
        m_FadeInAnimation->setDuration(AnimationUtils::duration(300));
        m_FadeInAnimation->setStartValue(0.0);
        m_FadeInAnimation->setEndValue(1.0);
        m_OpacityEffect->setEnabled(true);
        m_OpacityEffect->setOpacity(0.0);
        m_FadeInAnimation->start();
    }
}

} // namespace an::ui
