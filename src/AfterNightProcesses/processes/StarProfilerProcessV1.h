// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The first star profiler's window: the first layout over the one detector - its threshold, roundness tolerance and
/// size window, the thirteen figures its table shows - whose profiles are first-generation documents.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/QSliderSpinBox.h"
#include "processes/StarProfilerProcessV1UiBuilder.h"
#include "processes/StarProfilerWindow.h"

#include <string>

class ImageViewControl;

class AFTERNIGHTPROCESSES_API StarProfilerProcessV1 : public StarProfilerWindow
{
public:
    explicit StarProfilerProcessV1(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    // What the dialog asks a detection for now.
    [[nodiscard]] an::ui::processes::FirstProfilerSettings settings() const;

    // The window's controls, for the cases that drive it.
    [[nodiscard]] const an::ui::processes::StarProfilerProcessV1UiElements& ui() const;

    void resetSettings() override;

    ~StarProfilerProcessV1() override;

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

    QSliderDoubleSpinBoxUPtr detectionThresholdControl;
    QSliderDoubleSpinBoxUPtr roundnessToleranceControl;
    QSliderSpinBoxUPtr minSizeControl;
    QSliderSpinBoxUPtr maxSizeControl;

    an::ui::processes::StarProfilerProcessV1UiElements m_ui;
};
