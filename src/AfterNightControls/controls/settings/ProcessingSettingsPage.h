// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "SettingsPage.h"

#include <cstdint>

class QSpinBox;
class QCheckBox;
class QComboBox;
class QLabel;
class GlobalSettingsPersistence;

namespace an::ui
{
struct ProcessingLimits;
} // namespace an::ui

/// Processing settings page.
///
/// The thread ceiling and the engine's memory budget are fixed for the life of the engine session, so a change is
/// stored for the next start and the page says so: beside each limit it shows what the session in use runs with and
/// whether the stored value waits for a restart. A preview's threads are not a setting: the session reserves them out
/// of its width while a preview is open, and the page shows how many. How many previews a process window starts in a
/// second while a control is dragged is a setting that applies at once. The GPU controls are kept and disabled.
class AFTERNIGHTCONTROLS_API ProcessingSettingsPage : public SettingsPage
{
    W_OBJECT(ProcessingSettingsPage)

public:
    /// Create the Processing settings page
    /// @param settingsPersistence Pointer to the settings persistence layer
    /// @param parent Parent widget
    explicit ProcessingSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent = nullptr);
    ~ProcessingSettingsPage() override = default;

    [[nodiscard]] QString pageTitle() const override;
    [[nodiscard]] QIcon pageIcon() const override;
    [[nodiscard]] QStringList searchKeywords() const override;

    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;
    void refreshStyles() override;

    [[nodiscard]] ValidationResult validateSettings() const override;

    /// The limits the page's controls state now.
    [[nodiscard]] an::ui::ProcessingLimits stagedLimits() const;

    /// The previews a second the page's control states now.
    [[nodiscard]] std::uint32_t stagedPreviewFrameRate() const;

private:
    void setupUi();
    void connectSignals();

    /// Get the number of logical CPU cores
    [[nodiscard]] int getLogicalCoreCount() const;

    /// Get total system memory in MB
    [[nodiscard]] qint64 getTotalMemoryMB() const;

    /// Shows what the session in use runs with beside what the controls state, and whether a restart is pending.
    void updateLimitLabels();

    GlobalSettingsPersistence* m_Settings = nullptr;

    // System info cache
    int m_LogicalCores = 0;
    qint64 m_TotalMemoryMB = 0;

    // Parallelism controls
    QCheckBox* m_AutoThreadsBatchCheck = nullptr;
    QSpinBox* m_MaxThreadsBatchSpin = nullptr;
    QLabel* m_BatchThreadsInfoLabel = nullptr;
    QWidget* m_ThreadsPendingLabel = nullptr;
    QLabel* m_PreviewThreadsInfoLabel = nullptr;

    // Real-time preview controls
    QSpinBox* m_PreviewFrameRateSpin = nullptr;

    // GPU Acceleration controls
    QCheckBox* m_GpuEnabledCheck = nullptr;
    QComboBox* m_PreferredGpuCombo = nullptr;

    // Memory controls
    QCheckBox* m_AutoMemoryCheck = nullptr;
    QSpinBox* m_MemoryLimitSpin = nullptr;
    QLabel* m_MemoryInfoLabel = nullptr;
    QWidget* m_MemoryPendingLabel = nullptr;
};
