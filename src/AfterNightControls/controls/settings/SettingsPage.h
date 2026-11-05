// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QIcon>
#include <QVariant>
#include <functional>
#include <verdigris/wobjectdefs.h>

class QCheckBox;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QLineEdit;
class QSlider;
class QAbstractButton;
class QLabel;

/// Result of settings validation
struct ValidationResult
{
    bool isValid = true;
    QString errorMessage;
    QString fieldName; ///< Name of the field that failed validation (for focusing)

    static ValidationResult valid()
    {
        return ValidationResult{true, {}, {}};
    }
    static ValidationResult invalid(const QString& message, const QString& field = {})
    {
        return ValidationResult{false, message, field};
    }

    explicit operator bool() const
    {
        return isValid;
    }
};

/// Base class for all settings pages in the Settings Dialog.
/// Each settings page represents a category of settings (e.g., General, Raw Images, etc.)
///
/// Subclasses must implement:
/// - pageTitle(): Display name in navigation
/// - pageIcon(): Icon for navigation list
/// - loadSettings(): Load values from persistence into UI
/// - saveSettings(): Save values from UI to persistence
/// - resetToDefaults(): Reset all settings to default values
///
/// Subclasses may override:
/// - isEnabled(): Whether the page is functional (default: true)
/// - searchKeywords(): Keywords for search filtering
/// - validateSettings(): Validate before saving
/// - onPageActivated()/onPageDeactivated(): Lifecycle hooks
class AFTERNIGHTCONTROLS_API SettingsPage : public QWidget
{
    W_OBJECT(SettingsPage)

public:
    explicit SettingsPage(QWidget* parent = nullptr);
    ~SettingsPage() override = default;

    // ═══════════════════════════════════════════════════════════════════════
    // Page Identity (must override)
    // ═══════════════════════════════════════════════════════════════════════

    /// Display name shown in the navigation list
    [[nodiscard]] virtual QString pageTitle() const = 0;

    /// Icon for the navigation list
    [[nodiscard]] virtual QIcon pageIcon() const = 0;

    // ═══════════════════════════════════════════════════════════════════════
    // Page State
    // ═══════════════════════════════════════════════════════════════════════

    /// Whether this page is currently enabled/implemented.
    /// Disabled pages are shown grayed out in the navigation list.
    [[nodiscard]] virtual bool isEnabled() const
    {
        return true;
    }

    /// Search keywords for this page (used for filtering in search).
    /// Default implementation returns page title.
    [[nodiscard]] virtual QStringList searchKeywords() const;

    /// Check if any settings have been modified since last load/save
    [[nodiscard]] virtual bool hasUnsavedChanges() const;

    /// Whether the parent SettingsDialog should show Apply/Cancel/OK for this page.
    /// Pages that perform immediate actions can override this to request a Close-only footer.
    [[nodiscard]] virtual bool usesSettingsDialogCommitButtons() const
    {
        return true;
    }

    /// Check if settings have been loaded for this page (used for lazy loading)
    [[nodiscard]] bool isSettingsLoaded() const
    {
        return m_SettingsLoaded;
    }

    /// Mark settings as loaded (called by SettingsDialog after lazy loading)
    void setSettingsLoaded(bool loaded)
    {
        m_SettingsLoaded = loaded;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Settings Lifecycle (must override)
    // ═══════════════════════════════════════════════════════════════════════

    /// Load current settings values into UI controls.
    /// Called when the dialog is opened and after reset.
    virtual void loadSettings() = 0;

    /// Save UI control values to settings storage.
    /// Called when OK or Apply is clicked.
    virtual void saveSettings() = 0;

    /// Reset all settings on this page to defaults.
    /// Should update UI and mark as modified if values changed.
    virtual void resetToDefaults() = 0;

    // ═══════════════════════════════════════════════════════════════════════
    // Validation
    // ═══════════════════════════════════════════════════════════════════════

    /// Validate all settings on this page before saving.
    /// @return ValidationResult with success/error status
    [[nodiscard]] virtual ValidationResult validateSettings() const;

    // ═══════════════════════════════════════════════════════════════════════
    // Page Lifecycle Hooks
    // ═══════════════════════════════════════════════════════════════════════

    /// Called when the page becomes visible (selected in navigation)
    virtual void onPageActivated() {}

    /// Called when the page is about to be hidden (different page selected)
    virtual void onPageDeactivated() {}

    /// Called when the theme changes to refresh any theme-dependent styles.
    /// Base implementation refreshes all child widget palettes.
    /// Subclasses should call base and then update any hardcoded colors.
    virtual void refreshStyles();

signals:
    /// Emitted when any setting on this page is changed by the user
    void settingsModified() W_SIGNAL(settingsModified);

protected:
    // ═══════════════════════════════════════════════════════════════════════
    // Change Tracking Helpers
    // ═══════════════════════════════════════════════════════════════════════

    /// Mark the page as having unsaved changes.
    /// Emits settingsModified() signal on first modification.
    void markAsModified();

    /// Clear the modified flag (called after save/load)
    void clearModified();

    /// Connect a checkbox to automatically mark page as modified when changed
    void trackChanges(QCheckBox* checkbox);

    /// Connect a combobox to automatically mark page as modified when changed
    void trackChanges(QComboBox* combobox);

    /// Connect a spinbox to automatically mark page as modified when changed
    void trackChanges(QSpinBox* spinbox);

    /// Connect a double spinbox to automatically mark page as modified when changed
    void trackChanges(QDoubleSpinBox* spinbox);

    /// Connect a line edit to automatically mark page as modified when changed
    void trackChanges(QLineEdit* lineEdit);

    /// Connect a slider to automatically mark page as modified when changed
    void trackChanges(QSlider* slider);

    /// Connect any button (toggle) to automatically mark page as modified when changed
    void trackChanges(QAbstractButton* button);

    /// Generic helper to connect any signal to markAsModified
    template <typename Sender, typename Signal> void trackChangesGeneric(Sender* sender, Signal signal)
    {
        QObject::connect(sender, signal, this, &SettingsPage::markAsModified);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // UI Helper Methods
    // ═══════════════════════════════════════════════════════════════════════

    /// Create a horizontal separator line
    [[nodiscard]] QWidget* createSeparator();

    /// Create a section header label with optional description
    [[nodiscard]] QWidget* createSectionHeader(const QString& title, const QString& description = {});

    /// Create an info label (small text with info icon)
    [[nodiscard]] QWidget* createInfoLabel(const QString& text);

    /// Create a warning label (small text with warning icon)
    [[nodiscard]] QWidget* createWarningLabel(const QString& text);

    /// Resolve a named themed icon through the current application context.
    [[nodiscard]] QIcon contextIcon(const QString& iconName) const;

    /// Apply secondary/muted text styling to a QLabel via palette and font.
    /// Sets the anStatusTone property so the proxy style applies the correct color.
    void applySecondaryTextStyle(QLabel* label) const;

    /// Block signals during a scope (RAII helper)
    class SignalBlocker
    {
    public:
        explicit SignalBlocker(QWidget* widget);
        ~SignalBlocker();

    private:
        QWidget* m_Widget;
        bool m_WasBlocked;
    };

    /// Block signals on this page during load operations
    [[nodiscard]] SignalBlocker blockSignalsDuringLoad();

    bool m_Modified = false;
    bool m_SettingsLoaded = false; ///< Track whether loadSettings() has been called
};
