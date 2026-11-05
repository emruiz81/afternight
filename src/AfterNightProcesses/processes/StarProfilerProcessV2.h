// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The second star profiler's window: the second layout over the one detector - the detector's own controls, the
/// fifteen figures its table shows, each star placed on the sky through the image's solution and matched against the
/// installed Gaia catalog - whose profiles are second-generation documents.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/QSliderSpinBox.h"
#include "processes/StarProfilerProcessV2UiBuilder.h"
#include "processes/StarProfilerWindow.h"

#include <string>

class ImageViewControl;

class AFTERNIGHTPROCESSES_API StarProfilerProcessV2 : public StarProfilerWindow
{
public:
    explicit StarProfilerProcessV2(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    // What the dialog asks a detection for now.
    [[nodiscard]] an::ui::processes::SecondProfilerSettings settings() const;

    // The window's controls, for the cases that drive it.
    [[nodiscard]] const an::ui::processes::StarProfilerProcessV2UiElements& ui() const;

    void resetSettings() override;

    ~StarProfilerProcessV2() override;

protected:
    [[nodiscard]] an::StarProfileGeneration profileGeneration() const override;
    [[nodiscard]] QString profileFileFilter() const override;
    [[nodiscard]] QString profileFolderContext() const override;
    [[nodiscard]] std::string logName() const override;
    [[nodiscard]] an::ui::processes::StarProfileWork detectionWork() const override;
    [[nodiscard]] QStringList columnTitles() const override;
    void fillRow(TableWidget& table, int row, const an::DetectedStar& star) const override;
    [[nodiscard]] an::ui::StarList orderedBy(an::ui::StarList stars, int column, bool descending) const override;
    [[nodiscard]] StarsFilterControlBase* makeStarsFilter() override;

private:
    void refreshTheme() override;

    void on_profileTypeSelector_currentTextChanged(const QString& text);

    QSliderSpinBoxUPtr radiusControl;
    QSliderDoubleSpinBoxUPtr thresholdControl;
    QSliderDoubleSpinBoxUPtr thresholdMinimumControl;
    QSliderDoubleSpinBoxUPtr thresholdMaximumControl;
    QSliderDoubleSpinBoxUPtr roundnessThresholdControl;
    QSliderDoubleSpinBoxUPtr roundnessThresholdMaximumControl;

    an::ui::processes::StarProfilerProcessV2UiElements m_ui;
};
