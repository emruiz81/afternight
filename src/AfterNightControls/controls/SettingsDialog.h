// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QDialog>
#include <QString>
#include <QIcon>
#include <QList>
#include <memory>
#include <verdigris/wobjectdefs.h>

class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QScrollArea;
class QLineEdit;
class QPushButton;
class QLabel;
class QShowEvent;
class SettingsPage;

namespace an::ui
{
class AnSectionHeader;
}

// Register SettingsPage* for use in Verdigris signals/slots
W_REGISTER_ARGTYPE(SettingsPage*)

/// Main Settings Dialog for AfterNight application.
/// Provides a navigation list on the left and a content area on the right
/// for displaying settings pages, similar to modern IDE settings dialogs.
class AFTERNIGHTCONTROLS_API SettingsDialog : public QDialog
{
    W_OBJECT(SettingsDialog)

public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog() override;

    /// Add a settings page to the dialog.
    /// The dialog takes ownership of the page.
    /// @param page The settings page to add
    void addPage(SettingsPage* page);

    /// Remove a settings page from the dialog.
    /// @param page The settings page to remove
    /// @return true if the page was found and removed
    bool removePage(SettingsPage* page);

    /// Get the currently selected page
    /// @return The currently active settings page, or nullptr if none
    [[nodiscard]] SettingsPage* currentPage() const;

    /// Select a page by index
    /// @param index The index of the page to select
    void setCurrentPage(int index);

    /// Select a page by title
    /// @param title The title of the page to select
    void setCurrentPage(const QString& title);

    /// Refresh icons after theme change
    void refreshIcons();

    /// Get the number of pages
    [[nodiscard]] int pageCount() const;

    /// The page the navigation lists at `index`; null for none.
    [[nodiscard]] SettingsPage* page(int index) const;

    /// Check if any page has unsaved changes
    [[nodiscard]] bool hasUnsavedChanges() const;

signals:
    /// Emitted when a page is selected
    void pageChanged(SettingsPage* page) W_SIGNAL(pageChanged, page);

    /// Emitted when settings are applied (OK or Apply clicked)
    void settingsApplied() W_SIGNAL(settingsApplied);

    /// Emitted when settings are reset to defaults
    void settingsReset() W_SIGNAL(settingsReset);

public:
    /// Slot to accept the dialog (save settings and close)
    void accept() override;

    /// Slot to reject the dialog (discard changes and close)
    void reject() override;

protected:
    /// Holds the dialog within the available area of its screen as it is first shown.
    void showEvent(QShowEvent* event) override;

private:
    void setupUi();
    void setupConnections();

    /// Lists the pages a search finds: those whose title, search words or rows - a row's label or its section, as the
    /// Settings' search table states them - hold the text searched for, in any case.
    void filterPages(const QString& searchText);

    /// Handle navigation item selection
    void onNavigationItemChanged(QListWidgetItem* current, QListWidgetItem* previous);

    /// Save all settings from all pages
    void saveAllSettings();

    /// Load settings into all pages
    void loadAllSettings();

    /// Reset current page to defaults
    void resetCurrentPageToDefaults();

    /// Reset all pages to defaults
    void resetAllToDefaults();

    /// Update the footer buttons for the currently selected page.
    void updateFooterMode();

    /// Update the UI state (button enables, etc.)
    void updateUiState();

    /// Show a confirmation dialog before discarding unsaved changes
    /// @return true if the user wants to proceed (discard changes), false to cancel
    bool confirmDiscardChanges();

    /// Widens the page column's least width - 600 px - to what the widest page needs: the pages' stack takes its scroll
    /// area's width whatever its pages need, so a row wider than that was cut short at the dialog's least size in a
    /// font whose text runs wider.
    void widenForThePages();

    /// Helper to create a navigation list item for a page
    QListWidgetItem* createNavItem(SettingsPage* page);

    // UI Components
    QLineEdit* m_SearchEdit = nullptr;
    QListWidget* m_NavigationList = nullptr;
    QScrollArea* m_PageScrollArea = nullptr;
    QStackedWidget* m_PageStack = nullptr;
    an::ui::AnSectionHeader* m_PageSectionHeader = nullptr;
    QPushButton* m_ResetAllButton = nullptr;
    QPushButton* m_OkButton = nullptr;
    QPushButton* m_CancelButton = nullptr;
    QPushButton* m_ApplyButton = nullptr;
    QPushButton* m_CloseButton = nullptr;

    // Pages
    QList<SettingsPage*> m_Pages;

    /// Whether the dialog was shown yet: it is held to its screen at its first show only.
    bool m_Shown = false;
};
