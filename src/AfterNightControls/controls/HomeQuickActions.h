// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Home's quick actions: the dashboard's actions, as the person arranged them on the dashboard or here - the same
/// settings - in a row of buttons, Open file the accent's and the others tonal, each with its shortcut; the others in a
/// More menu with the customization; and the customization again on the row's right-click. A shell adds what only it
/// offers - New project - after the dashboard's actions and before More: an action the dashboard's settings do not
/// know, so it is neither moved, hidden nor reset.

#include "AfterNightControlsExport.h"

#include <QString>
#include <QStringList>
#include <QWidget>
#include <verdigris/wobjectdefs.h>

#include <memory>

class QAbstractButton;
class QContextMenuEvent;
class QMenu;

namespace an::ui
{

class AFTERNIGHTCONTROLS_API HomeQuickActions : public QWidget
{
    W_OBJECT(HomeQuickActions)

public:
    explicit HomeQuickActions(QWidget* parent = nullptr);
    ~HomeQuickActions() override;

    /// Reads the quick actions' order and the hidden ones from the settings again, as another part may have changed
    /// them.
    void reload();

    /// Adds an action to the row, after the dashboard's and before More, as a tonal button with an icon of the
    /// application's and a tooltip; `rowActionRequested` says when it is asked for. An id added again replaces what it
    /// was added as.
    void addRowAction(const QString& actionId, const QString& title, const QString& iconName, const QString& toolTip);

    /// The actions added to the row, in the order they were added.
    [[nodiscard]] QStringList addedRowActionIds() const;

    /// Turns an action on or off where it is, in the row or in the More menu.
    void setActionEnabled(const QString& actionId, bool enabled);

    /// Takes the icons the application's icon provider gives now.
    void refreshIcons();

    /// The actions the row shows, in its order.
    [[nodiscard]] QStringList rowActionIds() const;

    /// The actions the More menu holds, in its order.
    [[nodiscard]] QStringList moreActionIds() const;

    /// The row's button of an action, or null when the row does not show it.
    [[nodiscard]] QAbstractButton* actionButton(const QString& actionId) const;

    /// The button that opens the More menu.
    [[nodiscard]] QAbstractButton* moreButton() const;

    /// The More menu: the actions the row does not show, then "Customize quick actions".
    [[nodiscard]] QMenu* moreMenu() const;

    /// Fills a menu with the customization - what the row's right-click shows: a submenu per action that moves it and
    /// hides or shows it, a submenu that shows a hidden one, and Reset.
    void populateCustomizationMenu(QMenu& menu);

    void openFileRequested() W_SIGNAL(openFileRequested);
    void newSequenceRequested() W_SIGNAL(newSequenceRequested);
    void imageIntegrationRequested() W_SIGNAL(imageIntegrationRequested);
    void openExtensionManagerRequested() W_SIGNAL(openExtensionManagerRequested);
    void clearRecentFilesRequested() W_SIGNAL(clearRecentFilesRequested);
    void systemInformationRequested() W_SIGNAL(systemInformationRequested);
    /// An action added to the row was asked for.
    void rowActionRequested(const QString& actionId) W_SIGNAL(rowActionRequested, actionId);

protected:
    bool event(QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    /// Makes the row's buttons and the More menu again from the layout.
    void rebuild();

    /// Does what an action does: records it as the last action and asks for it.
    void trigger(const QString& actionId);

    /// Takes the spacing of the metrics in force.
    void applyMetrics();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
