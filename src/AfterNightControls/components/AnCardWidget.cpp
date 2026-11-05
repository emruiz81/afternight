// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnCardWidget.h"

#include "components/AnLayoutUtils.h"
#include "theme/AnimationUtils.h"
#include "theme/AnDesignTokens.h"
#include "app/UiStyleTokens.h"

#include <QEasingCurve>
#include <QAbstractButton>
#include <QEnterEvent>
#include <QEvent>
#include <QLabel>
#include <QLayout>
#include <QMargins>
#include <QPainter>
#include <QPaintEvent>
#include <QVariant>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>

namespace
{

QColor blendColors(const QColor& from, const QColor& to, const qreal t)
{
    const qreal clamped = std::clamp(t, 0.0, 1.0);
    return QColor(static_cast<int>(from.red() + (to.red() - from.red()) * clamped),
                  static_cast<int>(from.green() + (to.green() - from.green()) * clamped),
                  static_cast<int>(from.blue() + (to.blue() - from.blue()) * clamped),
                  static_cast<int>(from.alpha() + (to.alpha() - from.alpha()) * clamped));
}

void applyDisabledTextRoles(QWidget* root, const an::ui::DesignTokens& tokenSet)
{
    if (!root)
    {
        return;
    }

    const QColor disabledText = tokenSet.colors.textTertiary;
    const auto applyPalette = [&disabledText](QWidget* widget, const bool buttonText) {
        if (!widget)
        {
            return;
        }

        QPalette palette = widget->palette();
        palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
        palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
        palette.setColor(QPalette::Disabled, QPalette::PlaceholderText, disabledText);
        if (buttonText)
        {
            palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
        }
        widget->setPalette(palette);
    };

    for (auto* label : root->findChildren<QLabel*>())
    {
        applyPalette(label, false);
    }
    for (auto* button : root->findChildren<QAbstractButton*>())
    {
        applyPalette(button, true);
    }
}

[[nodiscard]] QColor resolvedOpaqueBackingSurface(const an::ui::DesignTokens& tokenSet)
{
    return tokenSet.materials.micaSurface.alpha() < 255
               ? an::ui::compositeOver(tokenSet.materials.micaSurface, tokenSet.colors.bgPrimary)
               : tokenSet.materials.micaSurface;
}

[[nodiscard]] QColor resolvedOpaqueSurface(const QColor& foreground, const an::ui::DesignTokens& tokenSet)
{
    if (foreground.alpha() >= 255)
    {
        return foreground;
    }

    return an::ui::compositeOver(foreground, resolvedOpaqueBackingSurface(tokenSet));
}

[[nodiscard]] QColor resolvedOpaqueSurface(const QColor& foreground, const QColor& backingSurface)
{
    if (foreground.alpha() >= 255)
    {
        return foreground;
    }

    return an::ui::compositeOver(foreground, backingSurface);
}

[[nodiscard]] QColor configuredBackdropColor(const QWidget* widget)
{
    if (!widget)
    {
        return {};
    }

    const QVariant backdrop = widget->property("anCardBackdropColor");
    if (!backdrop.canConvert<QColor>())
    {
        return {};
    }

    const QColor color = backdrop.value<QColor>();
    return color.isValid() ? color : QColor();
}

[[nodiscard]] QColor resolvedBackdropSurface(const QWidget* widget, const an::ui::DesignTokens& tokenSet)
{
    const QColor configuredColor = configuredBackdropColor(widget);
    if (configuredColor.isValid())
    {
        return resolvedOpaqueSurface(configuredColor, tokenSet);
    }

    return resolvedOpaqueBackingSurface(tokenSet);
}

} // anonymous namespace

namespace an::ui
{

AnCardWidget::AnCardWidget(const QString& title, QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("anCardWidget"));
    setFrameShape(QFrame::NoFrame);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_headerLabel = new QLabel(this);
    m_headerLabel->setObjectName(QStringLiteral("anCardWidgetHeader"));
    m_headerLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    m_contentWidget = new QWidget(this);
    m_contentWidget->setObjectName(QStringLiteral("anCardWidgetContent"));
    m_contentWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    rootLayout->addWidget(m_headerLabel, 0);
    rootLayout->addWidget(m_contentWidget, 1);

    // Hover animation: animate background between bgCardPrimary and bgHover.
    m_hoverAnimation = new QVariantAnimation(this);
    m_hoverAnimation->setDuration(AnimationUtils::duration(200));
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_hoverAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        const qreal t = value.toReal();
        const auto& tk = tokens();
        m_currentBgColor = blendColors(tk.colors.bgCardPrimary, tk.colors.bgHover, t);
        update();
    });

    setAttribute(Qt::WA_Hover, true);

    setTitle(title);
    refreshTheme();
}

void AnCardWidget::setTitle(const QString& title)
{
    if (!m_headerLabel)
    {
        return;
    }

    m_headerLabel->setText(title.trimmed());
    updateHeaderVisibility();
}

QString AnCardWidget::title() const
{
    return m_headerLabel ? m_headerLabel->text() : QString();
}

QLabel* AnCardWidget::headerLabel() const
{
    return m_headerLabel;
}

QWidget* AnCardWidget::contentWidget() const
{
    return m_contentWidget;
}

QLayout* AnCardWidget::contentLayout() const
{
    return m_contentWidget ? m_contentWidget->layout() : nullptr;
}

void AnCardWidget::setContentLayout(QLayout* layout)
{
    if (!m_contentWidget || !layout)
    {
        return;
    }

    if (QLayout* existingLayout = m_contentWidget->layout())
    {
        if (existingLayout == layout)
        {
            refreshTheme();
            return;
        }

        delete existingLayout;
    }

    reparentLayoutWidgets(layout, m_contentWidget);
    layout->setParent(m_contentWidget);
    m_contentWidget->setLayout(layout);
    refreshTheme();
}

void AnCardWidget::refreshTheme()
{
    if (!m_headerLabel || !m_contentWidget)
    {
        return;
    }

    refreshTokens();
    const auto& tokenSet = tokens();
    const bool compact = property("anCardCompact").toBool();
    const int outerMargin = compact ? tokenSet.spacing.sm : tokenSet.spacing.lg;
    const int outerSpacing = compact ? tokenSet.spacing.xs : tokenSet.spacing.sm;
    const int bodyMargin = property("anCardBodyPadded").toBool() ? tokenSet.spacing.sm : 0;

    if (auto* rootLayout = qobject_cast<QVBoxLayout*>(layout()))
    {
        rootLayout->setContentsMargins(outerMargin, outerMargin, outerMargin, outerMargin);
        rootLayout->setSpacing(outerSpacing);
    }

    if (QLayout* bodyLayout = m_contentWidget->layout())
    {
        bodyLayout->setContentsMargins(bodyMargin, bodyMargin, bodyMargin, bodyMargin);
        bodyLayout->setSpacing(tokenSet.spacing.sm);
    }

    m_headerLabel->setFont(tokenSet.typography.heading3);

    // Use palette-based styling instead of QSS — AnProxyStyle handles the painting.
    QPalette headerPal = m_headerLabel->palette();
    headerPal.setColor(QPalette::WindowText, tokenSet.colors.textAccent);
    headerPal.setColor(QPalette::Disabled, QPalette::WindowText, tokenSet.colors.textTertiary);
    headerPal.setColor(QPalette::Disabled, QPalette::Text, tokenSet.colors.textTertiary);
    headerPal.setColor(QPalette::Disabled, QPalette::PlaceholderText, tokenSet.colors.textTertiary);
    m_headerLabel->setPalette(headerPal);
    {
        auto headerFont = m_headerLabel->font();
        headerFont.setWeight(QFont::DemiBold);
        m_headerLabel->setFont(headerFont);
    }

    // Store card visual parameters as properties for the proxy style to read
    setProperty("anCardBgColor", tokenSet.colors.bgCardPrimary);
    setProperty("anCardBorderColor", tokenSet.colors.borderPrimary);
    setProperty("anCardRadius", tokenSet.radius.lg);

    // Initialise animated colors to the resting state (no hover).
    m_currentBgColor = tokenSet.colors.bgCardPrimary;
    m_currentBorderColor = tokenSet.colors.borderPrimary;

    // Refresh the hover animation duration in case the toggle changed.
    if (m_hoverAnimation)
    {
        m_hoverAnimation->setDuration(AnimationUtils::duration(200));
    }

    applyDisabledTextRoles(this, tokenSet);
    update();
}

void AnCardWidget::updateHeaderVisibility()
{
    if (!m_headerLabel)
    {
        return;
    }

    m_headerLabel->setVisible(!m_headerLabel->text().isEmpty());
}

void AnCardWidget::startHoverAnimation(const QColor& targetBg)
{
    if (!m_hoverAnimation)
    {
        return;
    }

    // Record current interpolation progress so rapid enter/leave looks smooth.
    const qreal currentT = (m_hoverAnimation->state() == QVariantAnimation::Running)
                               ? m_hoverAnimation->currentValue().toReal()
                               : (m_currentBgColor == tokens().colors.bgHover ? 1.0 : 0.0);

    const qreal targetT = (targetBg == tokens().colors.bgHover) ? 1.0 : 0.0;

    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(currentT);
    m_hoverAnimation->setEndValue(targetT);
    m_hoverAnimation->setDuration(AnimationUtils::duration(200));
    m_hoverAnimation->start();
}

void AnCardWidget::changeEvent(QEvent* event)
{
    QFrame::changeEvent(event);

    if (event && event->type() == QEvent::EnabledChange)
    {
        if (!isEnabled() && m_hoverAnimation)
        {
            m_hoverAnimation->stop();
        }
        m_currentBgColor = tokens().colors.bgCardPrimary;
        update();
    }
}

void AnCardWidget::enterEvent(QEnterEvent* event)
{
    QFrame::enterEvent(event);

    if (!isEnabled())
    {
        return;
    }

    if (!AnimationUtils::animationsEnabled())
    {
        m_currentBgColor = tokens().colors.bgHover;
        update();
        return;
    }

    startHoverAnimation(tokens().colors.bgHover);
}

void AnCardWidget::leaveEvent(QEvent* event)
{
    QFrame::leaveEvent(event);

    if (!AnimationUtils::animationsEnabled())
    {
        m_currentBgColor = tokens().colors.bgCardPrimary;
        update();
        return;
    }

    startHoverAnimation(tokens().colors.bgCardPrimary);
}

void AnCardWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const auto& tokenSet = tokens();
    const bool flatCard = property("anCardFlat").toBool();
    const QColor backdropSurface = resolvedBackdropSurface(this, tokenSet);
    if (!flatCard)
    {
        painter.fillRect(rect(), backdropSurface);
    }

    const int r = property("anCardRadius").toInt();
    const int radius = r > 0 ? r : tokenSet.radius.lg;

    const bool enabled = isEnabled();
    const QColor rawBg = enabled ? (m_currentBgColor.isValid() ? m_currentBgColor : tokenSet.colors.bgCardPrimary)
                                 : tokenSet.colors.bgSurface;
    const QColor bg = resolvedOpaqueSurface(rawBg, backdropSurface);
    const QColor border = enabled
                              ? (m_currentBorderColor.isValid() ? m_currentBorderColor : tokenSet.colors.borderPrimary)
                              : tokenSet.colors.borderSubtle;

    QRectF bounds = QRectF(rect());
    if (m_contentWidget)
    {
        QMargins bodyMargins;
        int bodyTopInset = 0;
        if (QLayout* rootLayout = layout())
        {
            bodyMargins = rootLayout->contentsMargins();
            bodyTopInset = rootLayout->spacing();
        }
        if (bodyTopInset < 0)
        {
            bodyTopInset = 0;
        }

        const bool titleVisible = m_headerLabel && !m_headerLabel->isHidden();
        if (!titleVisible)
        {
            bodyTopInset = bodyMargins.top();
        }

        bounds = QRectF(m_contentWidget->geometry())
                     .adjusted(-bodyMargins.left(), -bodyTopInset, bodyMargins.right(), bodyMargins.bottom())
                     .intersected(QRectF(rect()));
    }

    const bool shadowEnabled = enabled && !flatCard;
    if (shadowEnabled)
    {
        // Paint a drop shadow for elevation.
        const bool isDark = an::ui::styling::isDarkPalette(palette());
        const int shadowBlur = 4;
        const int shadowOffset = 2;
        const QColor shadowColor = isDark ? QColor(0, 0, 0, 40) : QColor(0, 0, 0, 20);

        QRectF shadowBounds = bounds.adjusted(shadowBlur, shadowBlur + shadowOffset, -shadowBlur, -shadowBlur);
        QPainterPath shadowPath;
        shadowPath.addRoundedRect(shadowBounds, radius, radius);

        // Simple fast shadow by filling the rect multiple times with low opacity.
        painter.setPen(Qt::NoPen);
        QColor c = shadowColor;
        for (int i = 0; i < shadowBlur; ++i)
        {
            c.setAlphaF(shadowColor.alphaF() * (1.0 - static_cast<qreal>(i) / shadowBlur));
            painter.setBrush(c);
            painter.drawRoundedRect(shadowBounds.adjusted(-i, -i, i, i), radius + i, radius + i);
        }

        bounds.adjust(shadowBlur, shadowBlur, -shadowBlur, -shadowBlur - shadowOffset);
    }

    painter.setPen(QPen(border, 1));
    painter.setBrush(bg);
    painter.drawRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

} // namespace an::ui
