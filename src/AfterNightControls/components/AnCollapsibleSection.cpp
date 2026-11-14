// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnCollapsibleSection.h"

#include "components/AnLayoutUtils.h"
#include "theme/AnimationUtils.h"
#include "theme/AnDesignTokens.h"

#include <QAbstractAnimation>
#include <QAbstractButton>
#include <QEasingCurve>
#include <QEvent>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

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

} // anonymous namespace

namespace an::ui
{

AnCollapsibleSection::AnCollapsibleSection(const QString& title, QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("anCollapsibleSection"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_toggleButton = new QToolButton(this);
    m_toggleButton->setObjectName(QStringLiteral("anCollapsibleSectionToggle"));
    m_toggleButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toggleButton->setCheckable(true);
    m_toggleButton->setArrowType(Qt::RightArrow);
    m_toggleButton->setChecked(false);
    m_toggleButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_contentWidget = new QWidget(this);
    m_contentWidget->setObjectName(QStringLiteral("anCollapsibleSectionContent"));
    m_contentWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_contentWidget->installEventFilter(this);
    auto* defaultContentLayout = new QVBoxLayout(m_contentWidget);
    defaultContentLayout->setContentsMargins(0, 0, 0, 0);
    defaultContentLayout->setSpacing(0);

    rootLayout->addWidget(m_toggleButton);
    rootLayout->addWidget(m_contentWidget);

    m_contentAnimation = new QPropertyAnimation(m_contentWidget, "maximumHeight", this);
    m_contentAnimation->setDuration(AnimationUtils::duration(170));
    m_contentAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_toggleButton, &QToolButton::clicked, this, [this](const bool checked) { setExpanded(checked); });

    // Keep both minimumHeight and maximumHeight in sync during animation
    // so the layout always gives the content widget exactly the animated height.
    connect(m_contentAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        if (m_contentWidget)
        {
            m_contentWidget->setMinimumHeight(value.toInt());
        }
    });

    connect(m_contentAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (!m_contentWidget)
        {
            return;
        }

        if (!m_isExpanded)
        {
            m_contentWidget->setMinimumHeight(0);
            m_contentWidget->setMaximumHeight(0);
            m_contentWidget->setVisible(false);
        }
        else
        {
            syncExpandedContentHeight();

            // Scroll the nearest ancestor QScrollArea so the expanded
            // section is fully visible.
            for (QWidget* ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget())
            {
                if (auto* scroll = qobject_cast<QScrollArea*>(ancestor))
                {
                    scroll->ensureWidgetVisible(this);
                    break;
                }
            }
        }
    });

    setTitle(title);
    applyExpandedState(false);
    refreshTheme();
}

void AnCollapsibleSection::setTitle(const QString& title)
{
    if (!m_toggleButton)
    {
        return;
    }

    m_toggleButton->setText(title.trimmed());
}

QString AnCollapsibleSection::title() const
{
    return m_toggleButton ? m_toggleButton->text() : QString();
}

void AnCollapsibleSection::setContentLayout(QLayout* layout)
{
    if (!m_contentWidget || !layout)
    {
        return;
    }

    if (QLayout* existingLayout = m_contentWidget->layout())
    {
        if (existingLayout == layout)
        {
            return;
        }

        delete existingLayout;
    }

    reparentLayoutWidgets(layout, m_contentWidget);
    layout->setParent(m_contentWidget);
    m_contentWidget->setLayout(layout);

    if (m_isExpanded)
    {
        applyExpandedState(true);
    }

    refreshTheme();
}

QLayout* AnCollapsibleSection::contentLayout() const
{
    return m_contentWidget ? m_contentWidget->layout() : nullptr;
}

QWidget* AnCollapsibleSection::contentWidget() const
{
    return m_contentWidget;
}

void AnCollapsibleSection::setExpanded(const bool expanded)
{
    if (!m_toggleButton || !m_contentWidget)
    {
        return;
    }

    m_isExpanded = expanded;
    m_toggleButton->setChecked(expanded);
    m_toggleButton->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);

    const int targetHeight = expanded ? contentHeightHint() : 0;
    if (expanded)
    {
        m_contentWidget->setVisible(true);
    }

    if (!m_contentAnimation || m_contentAnimation->duration() <= 0 || !AnimationUtils::animationsEnabled() ||
        !isVisible())
    {
        applyExpandedState(expanded);
        return;
    }

    const int currentHeight = m_contentWidget->height();

    // Lock both constraints to the current height so the very first animation
    // frame does not cause a layout jump.  The valueChanged connection keeps
    // them in sync for the remainder of the animation.
    m_contentWidget->setMinimumHeight(currentHeight);
    m_contentWidget->setMaximumHeight(currentHeight);

    // Collapse uses InCubic (slow start, fast end) to avoid the jarring
    // visual snap that OutCubic produces at the beginning of a shrink.
    // Expand uses OutCubic (fast start, slow settle) for a natural grow.
    m_contentAnimation->setEasingCurve(expanded ? QEasingCurve::OutCubic : QEasingCurve::InCubic);

    m_contentAnimation->stop();
    m_contentAnimation->setStartValue(currentHeight);
    m_contentAnimation->setEndValue(targetHeight);
    m_contentAnimation->start();
}

bool AnCollapsibleSection::isExpanded() const
{
    return m_isExpanded;
}

void AnCollapsibleSection::setAnimationDuration(const int milliseconds)
{
    if (!m_contentAnimation)
    {
        return;
    }

    m_contentAnimation->setDuration(AnimationUtils::duration((std::max)(0, milliseconds)));
}

int AnCollapsibleSection::animationDuration() const
{
    return m_contentAnimation ? m_contentAnimation->duration() : 0;
}

QToolButton* AnCollapsibleSection::toggleButton() const
{
    return m_toggleButton;
}

void AnCollapsibleSection::refreshTheme()
{
    if (!m_toggleButton || !m_contentWidget)
    {
        return;
    }

    refreshTokens();
    const auto& tokenSet = tokens();
    const bool observatory = styleProfile() == StyleProfile::Observatory;

    if (auto* rootLayout = qobject_cast<QVBoxLayout*>(layout()))
    {
        if (observatory)
        {
            // A section is a hairline over its header: nothing around it.
            rootLayout->setContentsMargins(0, 0, 0, 0);
            rootLayout->setSpacing(0);
        }
        else
        {
            // Match AnCardWidget's shadow inset so both components align visually
            // when placed in the same parent layout.
            constexpr int shadowInset = 4;
            rootLayout->setContentsMargins(shadowInset, shadowInset, shadowInset, shadowInset);
            rootLayout->setSpacing(tokenSet.spacing.xs);
        }
    }

    if (observatory)
    {
        // The header's title in small capitals of the caption, semibold, a little spaced; a section header high; quiet,
        // since its arrow says whether it is open.
        QFont title = tokenSet.typography.caption;
        title.setWeight(QFont::DemiBold);
        title.setCapitalization(QFont::AllUppercase);
        title.setLetterSpacing(QFont::PercentageSpacing, 108.0);
        m_toggleButton->setFont(title);
        m_toggleButton->setMinimumHeight(tokenSet.metrics.sectionHeader);
        m_toggleButton->setProperty("anButtonVariant", QStringLiteral("quiet"));
    }
    else
    {
        m_toggleButton->setFont(tokenSet.typography.heading3);
    }

    if (QLayout* content = m_contentWidget->layout())
    {
        if (observatory)
        {
            content->setContentsMargins(0, 0, 0, tokenSet.metrics.pad);
            content->setSpacing(tokenSet.metrics.gap);
        }
        else
        {
            content->setContentsMargins(tokenSet.spacing.sm, tokenSet.spacing.xs, tokenSet.spacing.sm,
                                        tokenSet.spacing.sm);
            content->setSpacing(tokenSet.spacing.sm);
        }
    }
    // Store card visual parameters
    m_bgColor = tokenSet.colors.bgCardPrimary;
    m_borderColor = tokenSet.colors.borderPrimary;
    m_radius = tokenSet.radius.md;

    // Toggle button text color via palette
    QPalette togglePal = m_toggleButton->palette();
    togglePal.setColor(QPalette::ButtonText, observatory ? tokenSet.colors.textSecondary : tokenSet.colors.textPrimary);
    togglePal.setColor(QPalette::Disabled, QPalette::ButtonText, tokenSet.colors.textTertiary);
    togglePal.setColor(QPalette::Disabled, QPalette::WindowText, tokenSet.colors.textTertiary);
    togglePal.setColor(QPalette::Disabled, QPalette::Text, tokenSet.colors.textTertiary);
    togglePal.setColor(QPalette::Disabled, QPalette::PlaceholderText, tokenSet.colors.textTertiary);
    m_toggleButton->setPalette(togglePal);

    applyDisabledTextRoles(this, tokenSet);
    if (m_isExpanded)
    {
        syncExpandedContentHeight();
    }

    // Re-polish the toggle button so the proxy style handles it
    m_toggleButton->style()->unpolish(m_toggleButton);
    m_toggleButton->style()->polish(m_toggleButton);
    m_toggleButton->update();

    update();
}

void AnCollapsibleSection::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);

    if (event && event->type() == QEvent::EnabledChange)
    {
        update();
    }
}

bool AnCollapsibleSection::event(QEvent* event)
{
    const bool handled = QWidget::event(event);
    if (!event || !m_isExpanded)
    {
        return handled;
    }

    switch (event->type())
    {
    case QEvent::ContentsRectChange:
    case QEvent::FontChange:
    case QEvent::LanguageChange:
    case QEvent::LayoutRequest:
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::StyleChange:
        queueExpandedContentHeightSync();
        break;
    default:
        break;
    }

    return handled;
}

bool AnCollapsibleSection::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_contentWidget && event && m_isExpanded)
    {
        switch (event->type())
        {
        case QEvent::ChildAdded:
        case QEvent::ChildRemoved:
        case QEvent::ContentsRectChange:
        case QEvent::FontChange:
        case QEvent::LanguageChange:
        case QEvent::LayoutRequest:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::StyleChange:
            queueExpandedContentHeightSync();
            break;
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void AnCollapsibleSection::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const auto& tokenSet = tokens();
    if (styleProfile() == StyleProfile::Observatory)
    {
        // A hairline over the section; no card.
        painter.fillRect(QRectF(0.0, 0.0, width(), 1.0), tokenSet.colors.borderSubtle);
        return;
    }
    const bool enabled = isEnabled();
    const QColor bg =
        enabled ? (m_bgColor.isValid() ? m_bgColor : tokenSet.colors.bgCardPrimary) : tokenSet.colors.bgSurface;
    const QColor border = enabled ? (m_borderColor.isValid() ? m_borderColor : tokenSet.colors.borderPrimary)
                                  : tokenSet.colors.borderSubtle;
    const int r = m_radius > 0 ? m_radius : tokenSet.radius.md;

    // Inset the painted rect by layout margins so the border aligns with
    // AnCardWidget's shadow-inset card boundary.
    QMargins m;
    if (QLayout* rootLayout = layout())
    {
        m = rootLayout->contentsMargins();
    }
    const QRectF paintRect = QRectF(rect()).adjusted(m.left(), m.top(), -m.right(), -m.bottom());

    painter.setPen(QPen(border, 1));
    painter.setBrush(bg);
    painter.drawRoundedRect(paintRect.adjusted(0.5, 0.5, -0.5, -0.5), r, r);
}

int AnCollapsibleSection::contentHeightHint() const
{
    if (!m_contentWidget)
    {
        return 0;
    }

    if (QLayout* layout = m_contentWidget->layout())
    {
        layout->activate();

        int hint = layout->sizeHint().height();
        hint = (std::max)(hint, layout->minimumSize().height());

        const int width = contentWidthHint();
        if (width > 0 && layout->hasHeightForWidth())
        {
            hint = (std::max)(hint, layout->heightForWidth(width));
        }

        return (std::max)(0, hint);
    }

    return (std::max)(0, m_contentWidget->sizeHint().height());
}

int AnCollapsibleSection::contentWidthHint() const
{
    if (!m_contentWidget)
    {
        return 0;
    }

    int width = m_contentWidget->width();
    if (width <= 0)
    {
        width = m_contentWidget->contentsRect().width();
    }

    if (width <= 0 && this->width() > 0)
    {
        width = this->width();
        if (QLayout* rootLayout = layout())
        {
            const QMargins margins = rootLayout->contentsMargins();
            width -= margins.left() + margins.right();
        }
    }

    if (width <= 0)
    {
        width = m_contentWidget->sizeHint().width();
    }

    return (std::max)(0, width);
}

void AnCollapsibleSection::applyExpandedState(const bool expanded)
{
    if (!m_contentWidget)
    {
        return;
    }

    if (expanded)
    {
        m_contentWidget->setVisible(true);
        syncExpandedContentHeight();
    }
    else
    {
        m_contentWidget->setMinimumHeight(0);
        m_contentWidget->setMaximumHeight(0);
        m_contentWidget->setVisible(false);
    }
}

void AnCollapsibleSection::queueExpandedContentHeightSync()
{
    if (!m_isExpanded || m_contentHeightSyncQueued)
    {
        return;
    }

    m_contentHeightSyncQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_contentHeightSyncQueued = false;
        syncExpandedContentHeight();
    });
}

void AnCollapsibleSection::syncExpandedContentHeight()
{
    if (!m_contentWidget || !m_isExpanded)
    {
        return;
    }

    if (QLayout* content = m_contentWidget->layout())
    {
        content->activate();
    }

    const int targetHeight = contentHeightHint();
    m_contentWidget->setVisible(true);

    if (m_contentAnimation && m_contentAnimation->state() == QAbstractAnimation::Running)
    {
        if (targetHeight > m_contentAnimation->endValue().toInt())
        {
            m_contentAnimation->setEndValue(targetHeight);
        }
        return;
    }

    bool geometryChanged = false;
    if (m_contentWidget->minimumHeight() != targetHeight)
    {
        m_contentWidget->setMinimumHeight(targetHeight);
        geometryChanged = true;
    }
    if (m_contentWidget->maximumHeight() != QWIDGETSIZE_MAX)
    {
        m_contentWidget->setMaximumHeight(QWIDGETSIZE_MAX);
        geometryChanged = true;
    }

    if (geometryChanged)
    {
        m_contentWidget->updateGeometry();
        updateGeometry();
        if (QWidget* parent = parentWidget())
        {
            parent->updateGeometry();
        }
    }
}

} // namespace an::ui
