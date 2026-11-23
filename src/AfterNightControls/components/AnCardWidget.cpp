// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnCardWidget.h"

#include "components/AnLayoutUtils.h"
#include "theme/AnDesignTokens.h"

#include <QAbstractButton>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QVariant>
#include <QVBoxLayout>

namespace
{

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
    // A card is padded as a panel is, by the density.
    const int outerMargin = compact ? tokenSet.metrics.gap : tokenSet.metrics.pad;
    const int outerSpacing = compact ? tokenSet.spacing.xs : tokenSet.metrics.gap;
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
    headerPal.setColor(QPalette::WindowText, tokenSet.colors.textPrimary);
    headerPal.setColor(QPalette::Disabled, QPalette::WindowText, tokenSet.colors.textTertiary);
    headerPal.setColor(QPalette::Disabled, QPalette::Text, tokenSet.colors.textTertiary);
    headerPal.setColor(QPalette::Disabled, QPalette::PlaceholderText, tokenSet.colors.textTertiary);
    m_headerLabel->setPalette(headerPal);
    {
        auto headerFont = m_headerLabel->font();
        headerFont.setWeight(QFont::DemiBold);
        m_headerLabel->setFont(headerFont);
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

void AnCardWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const auto& tokenSet = tokens();
    if (!property("anCardFlat").toBool())
    {
        painter.fillRect(rect(), resolvedBackdropSurface(this, tokenSet));
    }

    // A tonal container: the second surface over the whole card, its title inside it, no border and no shadow. A card
    // is a surface, not a control: it does not answer the pointer.
    painter.setPen(Qt::NoPen);
    painter.setBrush(tokenSet.colors.bgCard);
    painter.drawRoundedRect(QRectF(rect()), tokenSet.radius.lg, tokenSet.radius.lg);
}

} // namespace an::ui
