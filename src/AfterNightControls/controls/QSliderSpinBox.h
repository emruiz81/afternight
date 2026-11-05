// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QWidget>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSignalMapper>

#include <verdigris/wobjectdefs.h>

#include <memory>

class AFTERNIGHTCONTROLS_API QSliderSpinBox : public QObject
{
    W_OBJECT(QSliderSpinBox) // Replaces Q_OBJECT

public:
    QSliderSpinBox(QSpinBox* externalSpinbox, QSlider* externalSlider, QObject* parent = nullptr);

    QSliderSpinBox(int min, int max, int step, QObject* parent = nullptr);

    [[nodiscard]] int getValue() const;

    void setValue(const int val, bool signal = true);

    void setMinimum(const int val, bool signal = true);

    [[nodiscard]] int getMinimum() const;

    void setMaximum(const int val, bool signal = true);

    [[nodiscard]] int getMaximum() const;

    void setEnabled(bool enabled);

    bool isEnabled() const;

    [[nodiscard]] QSpinBox* getSpinBox() const;

    [[nodiscard]] QSlider* getSlider() const;

    ~QSliderSpinBox();

    // Signal triggered when either spinbox or slider is updated
    void valueChanged(int value) W_SIGNAL(valueChanged, value); // Replaces the Qt signal macro

    /// A person pressed the slider - true - or let it go - false: the values that change between the two are a drag.
    void interaction(bool held) W_SIGNAL(interaction, held);

private:
    void sliderValueChanged(int sliderValue);
    W_SLOT(sliderValueChanged); // Replaces the slot macro
    void spinBoxValueChanged(int spinValue);
    W_SLOT(spinBoxValueChanged);

    QSpinBox* m_SpinBox = nullptr;
    QSlider* m_Slider = nullptr;
    std::unique_ptr<QSpinBox> m_OwnedSpinBox;
    std::unique_ptr<QSlider> m_OwnedSlider;
};

typedef std::unique_ptr<QSliderSpinBox> QSliderSpinBoxUPtr;
typedef std::shared_ptr<QSliderSpinBox> QSliderSpinBoxSPtr;

class AFTERNIGHTCONTROLS_API QSliderDoubleSpinBox : public QObject
{
    W_OBJECT(QSliderDoubleSpinBox) // Replaces Q_OBJECT

public:
    QSliderDoubleSpinBox(QDoubleSpinBox* externalSpinbox, QSlider* externalSlider, QObject* parent = nullptr);

    QSliderDoubleSpinBox(double min, double max, double step, QObject* parent = nullptr);

    [[nodiscard]] double getValue() const;

    void setValue(const double val);

    void setMinimum(const double val);

    [[nodiscard]] double getMinimum() const;

    void setMaximum(const double val);

    [[nodiscard]] double getMaximum() const;

    void setEnabled(bool enabled);

    bool isEnabled() const;

    [[nodiscard]] QDoubleSpinBox* getSpinBox() const;

    [[nodiscard]] QSlider* getSlider() const;

    ~QSliderDoubleSpinBox();

    // Signal triggered when either spinbox or slider is updated
    void valueChanged(double value) W_SIGNAL(valueChanged, value); // Replaces the Qt signal macro

    /// A person pressed the slider - true - or let it go - false: the values that change between the two are a drag.
    void interaction(bool held) W_SIGNAL(interaction, held);

private:
    void sliderValueChanged(int sliderValue);
    W_SLOT(sliderValueChanged); // Replaces the slot macro
    void spinBoxValueChanged(double spinValue);
    W_SLOT(spinBoxValueChanged);

    QDoubleSpinBox* m_SpinBox = nullptr;
    QSlider* m_Slider = nullptr;
    std::unique_ptr<QDoubleSpinBox> m_OwnedSpinBox;
    std::unique_ptr<QSlider> m_OwnedSlider;

    // Map slider 0-100 to spinbox min-max
    double mapFromSlider(int sliderValue) const;

    int mapToSlider(double spinValue) const;
};

typedef std::unique_ptr<QSliderDoubleSpinBox> QSliderDoubleSpinBoxUPtr;
typedef std::shared_ptr<QSliderDoubleSpinBox> QSliderDoubleSpinBoxSPtr;

namespace an::ui
{

/// A window that tells a drag of one of its sliders from a settled change: a real-time preview window, which shows a
/// drag in interactive frames and the release in a settled one.
class AFTERNIGHTCONTROLS_API IPreviewInteractionWatcher
{
public:
    virtual ~IPreviewInteractionWatcher();

    /// Watches a slider a person drags: while it is held, the previews its changes ask for are interactive frames, and
    /// letting it go - or holding it still for the settle delay - asks for a settled frame. A control destroyed while
    /// held is let go of without one.
    ///
    /// @param control the control; it may be destroyed before the window.
    virtual void watchInteraction(QSliderSpinBox& control) = 0;

    /// `watchInteraction` for a control of real numbers.
    ///
    /// @param control the control; it may be destroyed before the window.
    virtual void watchInteraction(QSliderDoubleSpinBox& control) = 0;
};

/// Hands a control to the window that made it when that window tells a drag from a settled change (an
/// `IPreviewInteractionWatcher`, as a real-time preview window is): the previews asked for while its slider is held are
/// interactive frames, and one settled frame follows the release. Nothing happens for any other owner. The
/// `INIT_*SPINBOX_CTRL` macros call it for every control they make.
///
/// @param owner the object that made the control.
/// @param control the control.
AFTERNIGHTCONTROLS_API void watchPreviewInteraction(QObject* owner, QSliderSpinBox* control);

/// `watchPreviewInteraction` for a control of real numbers.
///
/// @param owner the object that made the control.
/// @param control the control.
AFTERNIGHTCONTROLS_API void watchPreviewInteraction(QObject* owner, QSliderDoubleSpinBox* control);

} // namespace an::ui

#define INIT_SPINBOX_CTRL(_CTRL_, _VALUE_, _SLIDER_, _SLOT_)                     \
    _CTRL_ = std::make_unique<QSliderSpinBox>(_VALUE_, _SLIDER_);                \
    QObject::connect(_CTRL_.get(), &QSliderSpinBox::valueChanged, this, _SLOT_); \
    an::ui::watchPreviewInteraction(this, _CTRL_.get());

#define INIT_SPINBOX_CTRL_2(_CTRL_, _MIN_, _MAX_, _STEP_, _SLOT_)                \
    _CTRL_ = std::make_unique<QSliderSpinBox>(_MIN_, _MAX_, _STEP_);             \
    QObject::connect(_CTRL_.get(), &QSliderSpinBox::valueChanged, this, _SLOT_); \
    an::ui::watchPreviewInteraction(this, _CTRL_.get());

#define INIT_D_SPINBOX_CTRL(_CTRL_, _VALUE_, _SLIDER_, _SLOT_)                         \
    _CTRL_ = std::make_unique<QSliderDoubleSpinBox>(_VALUE_, _SLIDER_);                \
    QObject::connect(_CTRL_.get(), &QSliderDoubleSpinBox::valueChanged, this, _SLOT_); \
    an::ui::watchPreviewInteraction(this, _CTRL_.get());

#define INIT_D_SPINBOX_CTRL_2(_CTRL_, _MIN_, _MAX_, _STEP_, _SLOT_)                    \
    _CTRL_ = std::make_unique<QSliderDoubleSpinBox>(_MIN_, _MAX_, _STEP_);             \
    QObject::connect(_CTRL_.get(), &QSliderDoubleSpinBox::valueChanged, this, _SLOT_); \
    an::ui::watchPreviewInteraction(this, _CTRL_.get());
