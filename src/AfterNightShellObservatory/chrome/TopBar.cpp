// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "chrome/TopBar.h"

#include "chrome/PaletteField.h"

#include "components/AnBreadcrumb.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "theme/NightImage.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QResizeEvent>
#include <QToolButton>

namespace an::ui::observatory
{

namespace
{

/// The application's mark, drawn in Night's red while Night is on.
class LogoMark final : public QWidget
{
public:
    explicit LogoMark(QWidget* parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("observatoryLogo"));
        setFixedSize(an::ui::parts::kTopBarLogo, an::ui::parts::kTopBarLogo);
        setToolTip(QStringLiteral("AfterNight"));
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const qreal ratio = devicePixelRatioF();
        QImage mark = QIcon(QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_128x128_blue.png"))
                          .pixmap(size(), ratio)
                          .toImage();
        if (an::ui::nightModeSet() && an::ui::styleProfile() == an::ui::StyleProfile::Observatory)
        {
            // The form is made off the GUI thread the first time; the mark is drawn once it is kept.
            mark = an::ui::nightImage(mark, this, [this]() { update(); });
            if (mark.isNull())
            {
                return;
            }
        }
        QPainter painter(this);
        painter.drawImage(rect(), mark);
    }
};

} // namespace

TopBar::TopBar(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("observatoryTopBar"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* outer = new QHBoxLayout(this);
    // The hairline under the bar takes its last row of pixels.
    outer->setContentsMargins(an::ui::parts::kTopBarPadding, 0, an::ui::parts::kTopBarPadding, 1);
    outer->setSpacing(an::ui::parts::kTopBarGap);
    m_row = new QHBoxLayout();
    m_row->setContentsMargins(0, 0, 0, 0);
    m_row->setSpacing(an::ui::parts::kTopBarGap);
    outer->addLayout(m_row, 1);
    m_trailing = new QHBoxLayout();
    m_trailing->setContentsMargins(0, 0, 0, 0);
    m_trailing->setSpacing(0);
    outer->addLayout(m_trailing);

    m_logo = new LogoMark(this);
    m_row->addWidget(m_logo);

    // The project switcher: "No project" until a project can be opened; its entries are offered once they work.
    m_switcher = new QToolButton(this);
    m_switcher->setObjectName(QStringLiteral("observatoryProjectSwitcher"));
    m_switcher->setText(tr("No project"));
    m_switcher->setToolTip(tr("No project"));
    m_switcher->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_switcher->setPopupMode(QToolButton::InstantPopup);
    m_switcher->setAutoRaise(true);
    m_switcher->setMaximumWidth(an::ui::parts::kProjectSwitcherMaxWidth);
    m_projectMenu = new QMenu(m_switcher);
    m_projectMenu->setObjectName(QStringLiteral("observatoryProjectMenu"));
    for (const QString& entry : {tr("New project..."), tr("Open project..."), tr("Close project")})
    {
        QAction* const action = m_projectMenu->addAction(entry);
        action->setEnabled(false);
    }
    m_switcher->setMenu(m_projectMenu);
    m_row->addWidget(m_switcher);

    m_breadcrumb = new an::ui::AnBreadcrumb(this);
    m_breadcrumb->setObjectName(QStringLiteral("observatoryBreadcrumb"));
    m_row->addWidget(m_breadcrumb, 0, Qt::AlignVCenter);
    m_row->addStretch(1);

    // The palette's field is centred in the bar, whatever the parts at its sides take.
    m_field = new PaletteField(this);
}

QHBoxLayout* TopBar::row() const noexcept
{
    return m_row;
}

QHBoxLayout* TopBar::trailing() const noexcept
{
    return m_trailing;
}

void TopBar::setIconProvider(IconProvider provider)
{
    m_icons = std::move(provider);
    m_field->setIconProvider(m_icons);
    refreshIcons();
}

void TopBar::refreshIcons()
{
    if (m_icons)
    {
        m_switcher->setIcon(m_icons(QStringLiteral("folder_closed")));
    }
    m_logo->update();
    m_field->update();
}

QWidget* TopBar::logo() const noexcept
{
    return m_logo;
}

QToolButton* TopBar::projectSwitcher() const noexcept
{
    return m_switcher;
}

QMenu* TopBar::projectMenu() const noexcept
{
    return m_projectMenu;
}

an::ui::AnBreadcrumb* TopBar::breadcrumb() const noexcept
{
    return m_breadcrumb;
}

PaletteField* TopBar::paletteField() const noexcept
{
    return m_field;
}

void TopBar::setWindowWidth(const int width)
{
    m_field->setWindowWidth(width);
    placeField();
}

QSize TopBar::sizeHint() const
{
    return {QWidget::sizeHint().width(), an::ui::tokens().metrics.barTop};
}

QSize TopBar::minimumSizeHint() const
{
    return {0, an::ui::tokens().metrics.barTop};
}

void TopBar::paintEvent(QPaintEvent* /*event*/)
{
    const an::ui::DesignTokens& t = an::ui::tokens();
    QPainter painter(this);
    painter.fillRect(rect(), t.layers.surface1);
    // The hairline under the bar, in its last row of pixels.
    painter.fillRect(QRect(0, height() - 1, width(), 1), t.colors.borderSubtle);
}

void TopBar::changeEvent(QEvent* event)
{
    // A change of density resizes the bar: its height is the density's.
    if (event != nullptr && (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange))
    {
        updateGeometry();
        update();
        placeField();
    }
    QWidget::changeEvent(event);
}

void TopBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    placeField();
}

void TopBar::placeField()
{
    const QSize size = m_field->sizeHint();
    const int top = (height() - 1 - size.height()) / 2;
    m_field->setGeometry((width() - size.width()) / 2, top, size.width(), size.height());
    m_field->raise();
    // The breadcrumb ends before the field: a long name is elided rather than covered.
    const int room = m_field->x() - an::ui::parts::kTopBarGap - m_breadcrumb->x();
    m_breadcrumb->setMaximumWidth((std::max)(0, room));
}

} // namespace an::ui::observatory
