// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "QSliderSpinBox.h"

#include <verdigris/wobjectimpl.h>

// For QSliderDoubleSpinBox
const int SLIDER_RESOLUTION = 1000;
const double SLIDER_RESOLUTION_D = static_cast<double>(SLIDER_RESOLUTION);

// --------------------------------------------------------
// QSliderSpinBox
// --------------------------------------------------------

W_OBJECT_IMPL(QSliderSpinBox)

QSliderSpinBox::QSliderSpinBox(QSpinBox* externalSpinbox, QSlider* externalSlider, QObject* parent) : QObject(parent)
{
    m_SpinBox = externalSpinbox;
    m_Slider = externalSlider;

    m_Slider->setRange(m_SpinBox->minimum(), m_SpinBox->maximum());

    setValue(m_SpinBox->value());

    // Connect slider to spinbox
    connect(m_Slider, &QSlider::valueChanged, this, &QSliderSpinBox::sliderValueChanged);
    // Connect spinbox to slider
    connect(m_SpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &QSliderSpinBox::spinBoxValueChanged);
    // Pressing the slider and letting it go bracket a drag
    connect(m_Slider, &QSlider::sliderPressed, this, [this]() { emit interaction(true); });
    connect(m_Slider, &QSlider::sliderReleased, this, [this]() { emit interaction(false); });
}

QSliderSpinBox::QSliderSpinBox(int min, int max, int step, QObject* parent) : QObject(parent)
{
    m_OwnedSpinBox = std::make_unique<QSpinBox>();
    m_SpinBox = m_OwnedSpinBox.get();

    m_SpinBox->setRange(min, max);
    m_SpinBox->setSingleStep(step);

    m_OwnedSlider = std::make_unique<QSlider>(Qt::Horizontal);
    m_Slider = m_OwnedSlider.get();
    m_Slider->setRange(m_SpinBox->minimum(), m_SpinBox->maximum());

    setValue(m_SpinBox->value());

    // Connect slider to spinbox
    connect(m_Slider, &QSlider::valueChanged, this, &QSliderSpinBox::sliderValueChanged);
    // Connect spinbox to slider
    connect(m_SpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &QSliderSpinBox::spinBoxValueChanged);
    // Pressing the slider and letting it go bracket a drag
    connect(m_Slider, &QSlider::sliderPressed, this, [this]() { emit interaction(true); });
    connect(m_Slider, &QSlider::sliderReleased, this, [this]() { emit interaction(false); });
}

int QSliderSpinBox::getValue() const
{
    return m_SpinBox->value();
}

void QSliderSpinBox::setValue(const int val, bool signal)
{
    if (!signal)
        m_SpinBox->blockSignals(true);
    m_SpinBox->setValue(val);
    if (!signal)
        m_SpinBox->blockSignals(false);

    m_Slider->blockSignals(true); // Prevent recursion
    m_Slider->setValue(val);
    m_Slider->blockSignals(false);
}

void QSliderSpinBox::setMinimum(const int val, bool signal)
{
    if (!signal)
        m_SpinBox->blockSignals(true);
    m_SpinBox->setMinimum(val);
    if (!signal)
        m_SpinBox->blockSignals(false);

    m_Slider->blockSignals(true); // Prevent recursion
    m_Slider->setMinimum(val);
    m_Slider->blockSignals(false);
}

int QSliderSpinBox::getMinimum() const
{
    return m_SpinBox->minimum();
}

void QSliderSpinBox::setMaximum(const int val, bool signal)
{
    if (!signal)
        m_SpinBox->blockSignals(true);
    m_SpinBox->setMaximum(val);
    if (!signal)
        m_SpinBox->blockSignals(false);

    m_Slider->blockSignals(true); // Prevent recursion
    m_Slider->setMaximum(val);
    m_Slider->blockSignals(false);

    m_SpinBox->update();
}

int QSliderSpinBox::getMaximum() const
{
    return m_SpinBox->maximum();
}

void QSliderSpinBox::setEnabled(bool enabled)
{
    m_SpinBox->setEnabled(enabled);
    m_Slider->setEnabled(enabled);
}

bool QSliderSpinBox::isEnabled() const
{
    return m_SpinBox->isEnabled() && m_Slider->isEnabled();
}

QSpinBox* QSliderSpinBox::getSpinBox() const
{
    return m_SpinBox;
}

QSlider* QSliderSpinBox::getSlider() const
{
    return m_Slider;
}

QSliderSpinBox::~QSliderSpinBox() {}

void QSliderSpinBox::sliderValueChanged(int sliderValue)
{
    m_SpinBox->blockSignals(true); // Prevent recursion
    m_SpinBox->setValue(sliderValue);
    m_SpinBox->blockSignals(false);
    emit valueChanged(sliderValue);
}

void QSliderSpinBox::spinBoxValueChanged(int spinValue)
{
    m_Slider->blockSignals(true); // Prevent recursion
    m_Slider->setValue(spinValue);
    m_Slider->blockSignals(false);
    emit valueChanged(spinValue);
}

// --------------------------------------------------------
// QSliderDoubleSpinBox
// --------------------------------------------------------

W_OBJECT_IMPL(QSliderDoubleSpinBox)

QSliderDoubleSpinBox::QSliderDoubleSpinBox(QDoubleSpinBox* externalSpinbox, QSlider* externalSlider, QObject* parent)
    : QObject(parent)
{
    m_SpinBox = externalSpinbox;
    m_Slider = externalSlider;

    m_Slider->setRange(0, SLIDER_RESOLUTION);

    setValue(m_SpinBox->value());

    // Connect slider to spinbox
    connect(m_Slider, &QSlider::valueChanged, this, &QSliderDoubleSpinBox::sliderValueChanged);
    // Connect spinbox to slider
    connect(m_SpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &QSliderDoubleSpinBox::spinBoxValueChanged);
    // Pressing the slider and letting it go bracket a drag
    connect(m_Slider, &QSlider::sliderPressed, this, [this]() { emit interaction(true); });
    connect(m_Slider, &QSlider::sliderReleased, this, [this]() { emit interaction(false); });
}

QSliderDoubleSpinBox::QSliderDoubleSpinBox(double min, double max, double step, QObject* parent) : QObject(parent)
{
    m_OwnedSpinBox = std::make_unique<QDoubleSpinBox>();
    m_SpinBox = m_OwnedSpinBox.get();

    m_SpinBox->setRange(min, max);
    m_SpinBox->setSingleStep(step);

    m_OwnedSlider = std::make_unique<QSlider>(Qt::Horizontal);
    m_Slider = m_OwnedSlider.get();

    m_Slider->setRange(0, SLIDER_RESOLUTION);

    // Connect slider to spinbox
    connect(m_Slider, &QSlider::valueChanged, this, &QSliderDoubleSpinBox::sliderValueChanged);
    // Connect spinbox to slider
    connect(m_SpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &QSliderDoubleSpinBox::spinBoxValueChanged);
    // Pressing the slider and letting it go bracket a drag
    connect(m_Slider, &QSlider::sliderPressed, this, [this]() { emit interaction(true); });
    connect(m_Slider, &QSlider::sliderReleased, this, [this]() { emit interaction(false); });
}

double QSliderDoubleSpinBox::getValue() const
{
    return m_SpinBox->value();
}

void QSliderDoubleSpinBox::setValue(const double val)
{
    m_SpinBox->setValue(val);

    int newSliderValue = mapToSlider(val);
    m_Slider->blockSignals(true); // Prevent recursion
    m_Slider->setValue(newSliderValue);
    m_Slider->blockSignals(false);
}

void QSliderDoubleSpinBox::setMinimum(const double val)
{
    m_SpinBox->setMinimum(val);
}

double QSliderDoubleSpinBox::getMinimum() const
{
    return m_SpinBox->minimum();
}

void QSliderDoubleSpinBox::setMaximum(const double val)
{
    m_SpinBox->setMaximum(val);
}

double QSliderDoubleSpinBox::getMaximum() const
{
    return m_SpinBox->maximum();
}

void QSliderDoubleSpinBox::setEnabled(bool enabled)
{
    m_SpinBox->setEnabled(enabled);
    m_Slider->setEnabled(enabled);
}

bool QSliderDoubleSpinBox::isEnabled() const
{
    return m_SpinBox->isEnabled() && m_Slider->isEnabled();
}

QDoubleSpinBox* QSliderDoubleSpinBox::getSpinBox() const
{
    return m_SpinBox;
}

QSlider* QSliderDoubleSpinBox::getSlider() const
{
    return m_Slider;
}

QSliderDoubleSpinBox::~QSliderDoubleSpinBox() {}

void QSliderDoubleSpinBox::sliderValueChanged(int sliderValue)
{
    double newValue = mapFromSlider(sliderValue);
    m_SpinBox->blockSignals(true); // Prevent recursion
    m_SpinBox->setValue(newValue);
    m_SpinBox->blockSignals(false);
    emit valueChanged(newValue);
}

void QSliderDoubleSpinBox::spinBoxValueChanged(double spinValue)
{
    int newSliderValue = mapToSlider(spinValue);
    m_Slider->blockSignals(true); // Prevent recursion
    m_Slider->setValue(newSliderValue);
    m_Slider->blockSignals(false);
    emit valueChanged(spinValue);
}

double QSliderDoubleSpinBox::mapFromSlider(int sliderValue) const
{
    double min = m_SpinBox->minimum();
    double max = m_SpinBox->maximum();
    return min + (sliderValue / SLIDER_RESOLUTION_D) * (max - min);
}

int QSliderDoubleSpinBox::mapToSlider(double spinValue) const
{
    double min = m_SpinBox->minimum();
    double max = m_SpinBox->maximum();
    // A range with no width has one position, the slider's start; dividing by its width would give a NaN, whose
    // conversion to an integer is undefined.
    if (!(max > min))
    {
        return 0;
    }
    return static_cast<int>(SLIDER_RESOLUTION_D * (spinValue - min) / (max - min));
}

// --------------------------------------------------------
// Registration with a window that tells a drag from a settled change
// --------------------------------------------------------

namespace an::ui
{

IPreviewInteractionWatcher::~IPreviewInteractionWatcher() = default;

void watchPreviewInteraction(QObject* owner, QSliderSpinBox* control)
{
    if (auto* window = dynamic_cast<IPreviewInteractionWatcher*>(owner); window != nullptr && control != nullptr)
    {
        window->watchInteraction(*control);
    }
}

void watchPreviewInteraction(QObject* owner, QSliderDoubleSpinBox* control)
{
    if (auto* window = dynamic_cast<IPreviewInteractionWatcher*>(owner); window != nullptr && control != nullptr)
    {
        window->watchInteraction(*control);
    }
}

} // namespace an::ui
