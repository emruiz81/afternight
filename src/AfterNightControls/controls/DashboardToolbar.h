// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QHash>
#include <QIcon>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <verdigris/wobjectdefs.h>

#include <functional>

class QMenu;
class QPushButton;

namespace an::ui
{
class AnButtonGroup;
}

namespace an::ui
{

class AFTERNIGHTCONTROLS_API DashboardToolbar : public QWidget
{
    W_OBJECT(DashboardToolbar)

public:
    explicit DashboardToolbar(QWidget* parent = nullptr);
    ~DashboardToolbar() override = default;

    void setActionOrder(const QStringList& orderedActionIds);
    void setHiddenActionIds(const QSet<QString>& hiddenActionIds);
    void setActionEnabled(const QString& actionId, bool enabled);
    void setActionIcon(const QString& actionId, const QIcon& icon);
    void setCustomizationMenuBuilder(std::function<void(QMenu& menu)> menuBuilder);

    [[nodiscard]] QString actionTitleForId(const QString& actionId) const;
    [[nodiscard]] QPushButton* actionButton(const QString& actionId) const;

signals:
    void openFileRequested() W_SIGNAL(openFileRequested);
    void newSequenceRequested() W_SIGNAL(newSequenceRequested);
    void imageIntegrationRequested() W_SIGNAL(imageIntegrationRequested);
    void openExtensionManagerRequested() W_SIGNAL(openExtensionManagerRequested);
    void clearRecentFilesRequested() W_SIGNAL(clearRecentFilesRequested);
    void openSettingsRequested() W_SIGNAL(openSettingsRequested);
    void systemInformationRequested() W_SIGNAL(systemInformationRequested);
    void aboutRequested() W_SIGNAL(aboutRequested);
    void openHelpRequested() W_SIGNAL(openHelpRequested);

private:
    [[nodiscard]] bool isPrimaryAction(const QString& actionId) const;
    [[nodiscard]] QStringList orderedPrimaryActionIds() const;
    [[nodiscard]] QStringList orderedSecondaryActionIds() const;

    void emitActionForId(const QString& actionId);
    void rebuildPrimaryButtons();
    void rebuildOverflowMenu();

    an::ui::AnButtonGroup* m_PrimaryButtons = nullptr;
    QPushButton* m_OverflowButton = nullptr;
    QMenu* m_OverflowMenu = nullptr;

    QStringList m_ActionOrder;
    QSet<QString> m_HiddenActionIds;
    QHash<QString, bool> m_ActionEnabled;
    QHash<QString, QIcon> m_ActionIcons;
    QHash<QString, QPushButton*> m_PrimaryButtonsById;
    std::function<void(QMenu& menu)> m_CustomizationMenuBuilder;
};

} // namespace an::ui
