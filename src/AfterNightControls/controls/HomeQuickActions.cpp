// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/HomeQuickActions.h"

#include "components/AnKeyHint.h"
#include "controls/DashboardPreferences.h"
#include "controls/DashboardSettings.h"
#include "controls/IAppContext.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QKeySequence>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QStyleOptionButton>
#include <QStylePainter>
#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::HomeQuickActions)

namespace an::ui
{

namespace
{

/// The icon an action shows, by the icon provider's name - the dashboard's.
QString iconNameFor(const QString& actionId)
{
    if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_OPEN_FILE))
    {
        return QStringLiteral("folder_open");
    }
    if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_NEW_SEQUENCE))
    {
        return QStringLiteral("movie");
    }
    if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_IMAGE_INTEGRATION))
    {
        return QStringLiteral("stack");
    }
    if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_EXTENSION_MANAGER))
    {
        return QStringLiteral("extension");
    }
    if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_CLEAR_RECENTS))
    {
        return QStringLiteral("close");
    }
    if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_SYSTEM_INFO))
    {
        return QStringLiteral("computer");
    }
    return {};
}

/// The application's icon of a name; none without a provider.
QIcon iconNamed(const QString& name)
{
    if (IAppContext* context = appContext(); context != nullptr && !name.isEmpty())
    {
        if (const IIconProviderPtr icons = context->getIconProvider())
        {
            return icons->getIcon(name);
        }
    }
    return {};
}

QIcon iconFor(const QString& actionId)
{
    return iconNamed(iconNameFor(actionId));
}

/// An action's tooltip: what it does and its shortcut - the dashboard's.
QString tooltipFor(const QString& actionId)
{
    const QString description = DashboardPreferences::actionDescriptionForId(actionId);
    const QString shortcut = DashboardPreferences::shortcutHintForActionId(actionId);
    return shortcut.isEmpty() ? description : QObject::tr("%1\nShortcut: %2").arg(description, shortcut);
}

/// The words the dashboard records an action by, under its translation context.
QString recordedWordsFor(const QString& actionId)
{
    if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_OPEN_FILE))
    {
        return QCoreApplication::translate("MainDashboard", "Open file");
    }
    if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_NEW_SEQUENCE))
    {
        return QCoreApplication::translate("MainDashboard", "New sequence");
    }
    if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_IMAGE_INTEGRATION))
    {
        return QCoreApplication::translate("MainDashboard", "Image integration");
    }
    if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_EXTENSION_MANAGER))
    {
        return QCoreApplication::translate("MainDashboard", "Open Extension Manager");
    }
    if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_CLEAR_RECENTS))
    {
        return QCoreApplication::translate("MainDashboard", "Clear recent files");
    }
    if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_SYSTEM_INFO))
    {
        return QCoreApplication::translate("MainDashboard", "Open system info");
    }
    return {};
}

/// A quick action's button: the style's button, and after its text the action's shortcut in a key hint on the button's
/// colour.
class QuickActionButton final : public QPushButton
{
public:
    QuickActionButton(const QString& text, const QKeySequence& keys, QWidget* parent)
        : QPushButton(text, parent), m_keys(AnKeyHint::keysText(keys))
    {
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        QSize hint = QPushButton::sizeHint();
        if (!m_keys.isEmpty())
        {
            hint.rwidth() += AnKeyHint::hintSize(m_keys).width() + parts::kHomeActionKeysGap;
        }
        return hint;
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QStylePainter painter(this);
        QStyleOptionButton option;
        initStyleOption(&option);
        if (m_keys.isEmpty())
        {
            painter.drawControl(QStyle::CE_PushButton, option);
            return;
        }
        painter.drawControl(QStyle::CE_PushButtonBevel, option);
        const QSize hint = AnKeyHint::hintSize(m_keys);
        const int extra = hint.width() + parts::kHomeActionKeysGap;
        QStyleOptionButton label = option;
        label.rect = option.rect.adjusted(0, 0, -extra, 0);
        painter.drawControl(QStyle::CE_PushButtonLabel, label);
        const QRectF box(label.rect.right() + 1, option.rect.top() + (option.rect.height() - hint.height()) / 2.0,
                         hint.width(), hint.height());
        const ColorTokens& c = tokens().colors;
        const bool primary = property("anButtonVariant").toString() == QLatin1String("primary");
        AnKeyHint::paintHint(painter, box, m_keys, AnKeyHint::Surface::OnColour,
                             primary ? c.textOnAccent : c.textOnAccentContainer);
    }

private:
    QString m_keys;
};

} // namespace

struct HomeQuickActions::Impl
{
    /// An action the row shows that the dashboard's settings do not know.
    struct Added
    {
        QString id;
        QString title;
        QString iconName;
        QString toolTip;
    };

    /// The icon of an action of the row: an added action's own, else the dashboard's.
    [[nodiscard]] QIcon iconOf(const QString& actionId) const
    {
        for (const Added& action : added)
        {
            if (action.id == actionId)
            {
                return iconNamed(action.iconName);
            }
        }
        return iconFor(actionId);
    }

    QList<Added> added;
    DashboardQuickActionLayout layout;
    QHBoxLayout* row = nullptr;
    QHash<QString, QPushButton*> buttons;
    QPushButton* more = nullptr;
    QMenu* moreMenu = nullptr;
    QPointer<QMenu> customize;
    QHash<QString, bool> enabled;
};

HomeQuickActions::HomeQuickActions(QWidget* parent) : QWidget(parent), m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("homeQuickActions"));
    m_impl->row = new QHBoxLayout(this);
    m_impl->row->setContentsMargins(0, 0, 0, 0);

    m_impl->more = new QPushButton(this);
    m_impl->more->setObjectName(QStringLiteral("homeQuickActionsMore"));
    m_impl->more->setAccessibleName(tr("More actions"));
    m_impl->more->setToolTip(tr("More actions"));
    m_impl->moreMenu = new QMenu(m_impl->more);
    m_impl->moreMenu->setObjectName(QStringLiteral("homeQuickActionsMoreMenu"));
    QObject::connect(m_impl->more, &QPushButton::clicked, this, [this]() {
        m_impl->moreMenu->popup(m_impl->more->mapToGlobal(QPoint(0, m_impl->more->height())));
    });

    m_impl->layout.load();
    rebuild();
    refreshIcons();
}

HomeQuickActions::~HomeQuickActions() = default;

void HomeQuickActions::reload()
{
    m_impl->layout.load();
    rebuild();
}

void HomeQuickActions::addRowAction(const QString& actionId, const QString& title, const QString& iconName,
                                    const QString& toolTip)
{
    const Impl::Added action{actionId, title, iconName, toolTip};
    bool replaced = false;
    for (Impl::Added& existing : m_impl->added)
    {
        if (existing.id == actionId)
        {
            existing = action;
            replaced = true;
        }
    }
    if (!replaced)
    {
        m_impl->added.append(action);
    }
    rebuild();
}

QStringList HomeQuickActions::addedRowActionIds() const
{
    QStringList ids;
    for (const Impl::Added& action : m_impl->added)
    {
        ids.append(action.id);
    }
    return ids;
}

void HomeQuickActions::setActionEnabled(const QString& actionId, const bool enabled)
{
    const QString id = DashboardPreferences::normalizeActionIdToken(actionId);
    m_impl->enabled.insert(id, enabled);
    if (QPushButton* button = m_impl->buttons.value(id, nullptr))
    {
        button->setEnabled(enabled);
    }
    for (QAction* action : m_impl->moreMenu->actions())
    {
        if (action->data().toString() == id)
        {
            action->setEnabled(enabled);
        }
    }
}

void HomeQuickActions::refreshIcons()
{
    for (auto it = m_impl->buttons.cbegin(); it != m_impl->buttons.cend(); ++it)
    {
        it.value()->setIcon(m_impl->iconOf(it.key()));
    }
    for (QAction* action : m_impl->moreMenu->actions())
    {
        if (const QString id = action->data().toString(); !id.isEmpty())
        {
            action->setIcon(iconFor(id));
        }
    }
    QIcon more;
    if (IAppContext* context = appContext())
    {
        if (const IIconProviderPtr icons = context->getIconProvider())
        {
            more = icons->getIcon(QStringLiteral("more_horiz"));
        }
    }
    m_impl->more->setIcon(more);
    // With no icon to show, the button says what it holds.
    m_impl->more->setText(more.isNull() ? QStringLiteral("···") : QString());
}

QStringList HomeQuickActions::rowActionIds() const
{
    return m_impl->layout.shownPrimary();
}

QStringList HomeQuickActions::moreActionIds() const
{
    return m_impl->layout.shownOverflow();
}

QAbstractButton* HomeQuickActions::actionButton(const QString& actionId) const
{
    return m_impl->buttons.value(DashboardPreferences::normalizeActionIdToken(actionId), nullptr);
}

QAbstractButton* HomeQuickActions::moreButton() const
{
    return m_impl->more;
}

QMenu* HomeQuickActions::moreMenu() const
{
    return m_impl->moreMenu;
}

void HomeQuickActions::populateCustomizationMenu(QMenu& menu)
{
    m_impl->layout.populateCustomizationMenu(menu, this, [this]() { rebuild(); });
}

bool HomeQuickActions::event(QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::Polish)
    {
        applyMetrics();
    }
    return QWidget::event(event);
}

void HomeQuickActions::changeEvent(QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::StyleChange)
    {
        applyMetrics();
    }
    QWidget::changeEvent(event);
}

void HomeQuickActions::contextMenuEvent(QContextMenuEvent* event)
{
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("homeQuickActionsMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    populateCustomizationMenu(*menu);
    menu->popup(event->globalPos());
    event->accept();
}

void HomeQuickActions::rebuild()
{
    // The row: a button per shown primary action, in order, then More.
    for (QPushButton* button : std::as_const(m_impl->buttons))
    {
        m_impl->row->removeWidget(button);
        button->deleteLater();
    }
    m_impl->buttons.clear();
    m_impl->row->removeWidget(m_impl->more);

    for (const QString& id : m_impl->layout.shownPrimary())
    {
        const QString shortcut = DashboardPreferences::shortcutHintForActionId(id);
        auto* button = new QuickActionButton(DashboardPreferences::actionTitleForId(id), QKeySequence(shortcut), this);
        button->setObjectName(QStringLiteral("homeQuickAction"));
        button->setProperty("homeActionId", id);
        // Open file is the accent's; the others are tonal.
        button->setProperty("anButtonVariant", id == QLatin1String(DashboardPreferences::ACTION_ID_OPEN_FILE)
                                                   ? QStringLiteral("primary")
                                                   : QStringLiteral("tonal"));
        button->setToolTip(tooltipFor(id));
        button->setAccessibleName(DashboardPreferences::actionTitleForId(id));
        button->setIcon(iconFor(id));
        button->setIconSize(QSize(parts::kHomeActionIcon, parts::kHomeActionIcon));
        button->setCursor(Qt::PointingHandCursor);
        button->setEnabled(m_impl->enabled.value(id, true));
        QObject::connect(button, &QPushButton::clicked, this, [this, id]() { trigger(id); });
        m_impl->row->addWidget(button);
        m_impl->buttons.insert(id, button);
    }
    // What the shell added: after the dashboard's actions, tonal, asked for by its id.
    for (const Impl::Added& action : std::as_const(m_impl->added))
    {
        auto* button = new QuickActionButton(action.title, QKeySequence(), this);
        button->setObjectName(QStringLiteral("homeQuickAction"));
        button->setProperty("homeActionId", action.id);
        button->setProperty("anButtonVariant", QStringLiteral("tonal"));
        button->setToolTip(action.toolTip);
        button->setAccessibleName(action.title);
        button->setIcon(iconNamed(action.iconName));
        button->setIconSize(QSize(parts::kHomeActionIcon, parts::kHomeActionIcon));
        button->setCursor(Qt::PointingHandCursor);
        button->setEnabled(m_impl->enabled.value(action.id, true));
        const QString id = action.id;
        QObject::connect(button, &QPushButton::clicked, this, [this, id]() { emit rowActionRequested(id); });
        m_impl->row->addWidget(button);
        m_impl->buttons.insert(id, button);
    }
    m_impl->row->addWidget(m_impl->more);

    // The More menu: the other shown actions, then the customization. The submenu a choice was made in may be the one
    // replaced here, so it goes once its signal is done.
    if (m_impl->customize)
    {
        m_impl->customize->deleteLater();
    }
    m_impl->moreMenu->clear();
    for (const QString& id : m_impl->layout.shownOverflow())
    {
        QAction* action = m_impl->moreMenu->addAction(iconFor(id), DashboardPreferences::actionTitleForId(id));
        action->setData(id);
        action->setToolTip(tooltipFor(id));
        action->setEnabled(m_impl->enabled.value(id, true));
        QObject::connect(action, &QAction::triggered, this, [this, id]() { trigger(id); });
    }
    if (!m_impl->moreMenu->actions().isEmpty())
    {
        m_impl->moreMenu->addSeparator();
    }
    m_impl->customize =
        m_impl->moreMenu->addMenu(QCoreApplication::translate("MainDashboard", "Customize quick actions"));
    m_impl->layout.populateCustomizationMenu(*m_impl->customize, this, [this]() { rebuild(); });
    m_impl->moreMenu->setToolTipsVisible(true);
}

void HomeQuickActions::trigger(const QString& actionId)
{
    recordDashboardLastAction(recordedWordsFor(actionId));
    if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_OPEN_FILE))
    {
        emit openFileRequested();
    }
    else if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_NEW_SEQUENCE))
    {
        emit newSequenceRequested();
    }
    else if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_IMAGE_INTEGRATION))
    {
        emit imageIntegrationRequested();
    }
    else if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_EXTENSION_MANAGER))
    {
        emit openExtensionManagerRequested();
    }
    else if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_CLEAR_RECENTS))
    {
        emit clearRecentFilesRequested();
    }
    else if (actionId == QLatin1String(DashboardPreferences::ACTION_ID_SYSTEM_INFO))
    {
        emit systemInformationRequested();
    }
}

void HomeQuickActions::applyMetrics()
{
    m_impl->row->setSpacing(tokens().metrics.gap);
}

} // namespace an::ui
