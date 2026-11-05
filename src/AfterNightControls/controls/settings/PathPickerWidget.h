// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QWidget>
#include <QString>
#include <verdigris/wobjectdefs.h>

class QLineEdit;
class QPushButton;
class QLabel;
class QEvent;

/// A reusable widget for selecting file or directory paths in settings.
/// Provides a line edit, browse button, and optional detect button with status label.
class AFTERNIGHTCONTROLS_API PathPickerWidget : public QWidget
{
    W_OBJECT(PathPickerWidget)

public:
    /// Mode for the path picker
    enum class Mode
    {
        File, ///< Select a file
        Directory, ///< Select a directory
        Executable ///< Select an executable file
    };

    /// Create a path picker widget
    /// @param mode The type of path to select
    /// @param parent Parent widget
    explicit PathPickerWidget(Mode mode = Mode::File, QWidget* parent = nullptr);
    ~PathPickerWidget() override = default;

    /// Get the current path
    [[nodiscard]] QString path() const;

    /// Set the current path
    void setPath(const QString& path);

    /// Set the placeholder text for the line edit
    void setPlaceholderText(const QString& text);

    /// Set the file filter for file selection mode
    /// @param filter Filter string (e.g., "Executables (*.exe);;All Files (*)")
    void setFileFilter(const QString& filter);

    /// Set the dialog title for the file/directory dialog
    void setDialogTitle(const QString& title);

    /// Enable or disable the detect button
    /// @param enabled Whether to show the detect button
    void setDetectEnabled(bool enabled);

    /// Set a function to detect the path automatically
    /// @param detectFunc Function that returns the detected path, or empty string if not found
    void setDetectFunction(std::function<QString()> detectFunc);

    /// Set the status text (shown below the path input)
    /// @param text Status text (e.g., "✓ Tool v1.0 detected")
    /// @param isError Whether this is an error status (changes color)
    void setStatus(const QString& text, bool isError = false);

    /// Set a warning status text (shown in yellow/amber)
    /// @param text Warning text (e.g., "Could not auto-detect")
    void setWarningStatus(const QString& text);

    /// Clear the status text
    void clearStatus();

    /// Set whether the path is read-only
    void setReadOnly(bool readOnly);

    /// Refresh theme-dependent icons.
    /// Called by settings containers after runtime theme changes.
    void refreshBrowseIcon();

signals:
    /// Emitted when the path changes
    void pathChanged(const QString& path) W_SIGNAL(pathChanged, path);

    /// Emitted when the detect button is clicked
    void detectRequested() W_SIGNAL(detectRequested);

private:
    void setupUi();
    void onBrowseClicked();
    void onDetectClicked();
    void changeEvent(QEvent* event) override;

    Mode m_Mode;
    QString m_FileFilter;
    QString m_DialogTitle;
    std::function<QString()> m_DetectFunction;

    QLineEdit* m_PathEdit = nullptr;
    QPushButton* m_BrowseButton = nullptr;
    QPushButton* m_DetectButton = nullptr;
    QLabel* m_StatusLabel = nullptr;
};
