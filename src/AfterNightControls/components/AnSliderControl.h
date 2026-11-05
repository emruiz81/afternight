// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QString>
#include <QWidget>

class QLabel;
class QSlider;
class QSpinBox;

namespace an::ui
{

class AFTERNIGHTCONTROLS_API AnSliderControl : public QWidget
{
public:
    explicit AnSliderControl(const QString& labelText = QString(), int minimum = 0, int maximum = 100, int value = 0,
                             QWidget* parent = nullptr);
    ~AnSliderControl() override = default;

    void setLabelText(const QString& text);
    [[nodiscard]] QString labelText() const;
    [[nodiscard]] QLabel* label() const;

    void setRange(int minimum, int maximum);
    [[nodiscard]] int minimum() const;
    [[nodiscard]] int maximum() const;

    void setValue(int value);
    [[nodiscard]] int value() const;

    [[nodiscard]] QSlider* slider() const;
    [[nodiscard]] QSpinBox* spinBox() const;

    void refreshTheme();

private:
    QLabel* m_label = nullptr;
    QSlider* m_slider = nullptr;
    QSpinBox* m_spinBox = nullptr;
};

} // namespace an::ui
