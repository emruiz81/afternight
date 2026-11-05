// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QDialog>
#include <verdigris/wobjectdefs.h>
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QLabel;

namespace an::ui
{
class AnCollapsibleSection;
}

/// Dialog for editing light calibration settings per group
class AFTERNIGHTPROCESSES_API LightCalibrationSettingsDialog : public QDialog
{
    W_OBJECT(LightCalibrationSettingsDialog)

public:
    explicit LightCalibrationSettingsDialog(const LightCalibrationSettings& settings, const QString& groupLabel,
                                            QWidget* parent = nullptr);

    ~LightCalibrationSettingsDialog() override = default;

    LightCalibrationSettings getSettings() const;

    bool applyToAllGroups() const
    {
        return m_ApplyToAll;
    }

private:
    void setupUi();
    void onCosmeticCorrectionToggled(bool checked);
    void onApplyToAllClicked();

    W_SLOT(onCosmeticCorrectionToggled, (bool))
    W_SLOT(onApplyToAllClicked)

    // UI controls
    QCheckBox* m_CosmeticCorrectionCheck = nullptr;
    QDoubleSpinBox* m_SigmaColdSpin = nullptr;
    QDoubleSpinBox* m_SigmaHotSpin = nullptr;
    an::ui::AnCollapsibleSection* m_CosmeticGroup = nullptr;

    QCheckBox* m_DarkOptimizationCheck = nullptr;
    QDoubleSpinBox* m_DarkMultFactorSpin = nullptr;
    an::ui::AnCollapsibleSection* m_DarkOptGroup = nullptr;
    QCheckBox* m_FlatAutoLevelCheck = nullptr;

    QSpinBox* m_PedestalSpin = nullptr;

    QPushButton* m_OkButton = nullptr;
    QPushButton* m_CancelButton = nullptr;
    QPushButton* m_ResetButton = nullptr;
    QPushButton* m_ApplyToAllButton = nullptr;

    QString m_GroupLabel;
    LightCalibrationSettings m_InitialSettings;
    bool m_ApplyToAll = false;
};
