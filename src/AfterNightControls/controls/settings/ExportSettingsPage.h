// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "SettingsPage.h"

class QComboBox;
class QSlider;
class QCheckBox;
class QLabel;
class QRadioButton;
class GlobalSettingsPersistence;

/// Export settings page.
/// Controls format-specific export options, metadata embedding, and default directories.
class AFTERNIGHTCONTROLS_API ExportSettingsPage : public SettingsPage
{
    W_OBJECT(ExportSettingsPage)

public:
    /// Create the Export settings page
    /// @param settingsPersistence Pointer to the settings persistence layer
    /// @param parent Parent widget
    explicit ExportSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent = nullptr);
    ~ExportSettingsPage() override = default;

    [[nodiscard]] QString pageTitle() const override;
    [[nodiscard]] QIcon pageIcon() const override;
    [[nodiscard]] QStringList searchKeywords() const override;

    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;
    void refreshStyles() override;

    [[nodiscard]] ValidationResult validateSettings() const override;

private:
    void setupUi();
    void connectSignals();

    /// Update the JPEG quality percentage label based on slider value
    void updateJpegQualityLabel();

    /// Update the PNG compression percentage label based on slider value
    void updatePngCompressionLabel();

    /// Enable byte shuffling only beside a compression, which it is a step of
    void updateXisfByteShuffleState();

    GlobalSettingsPersistence* m_Settings = nullptr;

    // TIFF settings
    QComboBox* m_TiffCompressionCombo = nullptr;

    // XISF settings
    QComboBox* m_XisfCompressionCombo = nullptr;
    QCheckBox* m_XisfByteShuffleCheck = nullptr;
    QComboBox* m_XisfChecksumCombo = nullptr;

    // JPEG settings
    QSlider* m_JpegQualitySlider = nullptr;
    QLabel* m_JpegQualityLabel = nullptr;
    QLabel* m_JpegLowLabel = nullptr;
    QLabel* m_JpegHighLabel = nullptr;

    // PNG settings
    QSlider* m_PngCompressionSlider = nullptr;
    QLabel* m_PngCompressionLabel = nullptr;
    QLabel* m_PngFastLabel = nullptr;
    QLabel* m_PngSmallLabel = nullptr;

    // Metadata settings
    QCheckBox* m_EmbedExifCheck = nullptr;
    QCheckBox* m_EmbedHistoryCheck = nullptr;
};
