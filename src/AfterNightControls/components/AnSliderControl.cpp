// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnSliderControl.h"

#include "theme/AnDesignTokens.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace an::ui
{

AnSliderControl::AnSliderControl(const QString& labelText, const int minimum, const int maximum, const int value,
                                 QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("anSliderControl"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(4);

    m_label = new QLabel(this);
    m_label->setObjectName(QStringLiteral("anSliderControlLabel"));
    rootLayout->addWidget(m_label);

    auto* rowLayout = new QHBoxLayout();
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);
    rootLayout->addLayout(rowLayout);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setObjectName(QStringLiteral("anSliderControlSlider"));
    rowLayout->addWidget(m_slider, 1);

    m_spinBox = new QSpinBox(this);
    m_spinBox->setObjectName(QStringLiteral("anSliderControlSpinBox"));
    rowLayout->addWidget(m_spinBox);

    connect(m_slider, &QSlider::valueChanged, this, [this](const int sliderValue) {
        if (!m_spinBox || m_spinBox->value() == sliderValue)
        {
            return;
        }

        const QSignalBlocker blocker(m_spinBox);
        m_spinBox->setValue(sliderValue);
    });

    connect(m_spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](const int spinValue) {
        if (!m_slider || m_slider->value() == spinValue)
        {
            return;
        }

        const QSignalBlocker blocker(m_slider);
        m_slider->setValue(spinValue);
    });

    setLabelText(labelText);
    setRange(minimum, maximum);
    setValue(value);
    refreshTheme();
}

void AnSliderControl::setLabelText(const QString& text)
{
    if (!m_label)
    {
        return;
    }

    m_label->setText(text.trimmed());
    m_label->setVisible(!m_label->text().isEmpty());
}

QString AnSliderControl::labelText() const
{
    return m_label ? m_label->text() : QString();
}

QLabel* AnSliderControl::label() const
{
    return m_label;
}

void AnSliderControl::setRange(const int minimum, const int maximum)
{
    if (!m_slider || !m_spinBox)
    {
        return;
    }

    const int boundedMin = (std::min)(minimum, maximum);
    const int boundedMax = (std::max)(minimum, maximum);

    m_slider->setRange(boundedMin, boundedMax);
    m_spinBox->setRange(boundedMin, boundedMax);
    setValue(value());
}

int AnSliderControl::minimum() const
{
    return m_slider ? m_slider->minimum() : 0;
}

int AnSliderControl::maximum() const
{
    return m_slider ? m_slider->maximum() : 0;
}

void AnSliderControl::setValue(const int value)
{
    if (!m_slider || !m_spinBox)
    {
        return;
    }

    const int boundedValue = std::clamp(value, minimum(), maximum());

    {
        const QSignalBlocker sliderBlocker(m_slider);
        m_slider->setValue(boundedValue);
    }

    {
        const QSignalBlocker spinBoxBlocker(m_spinBox);
        m_spinBox->setValue(boundedValue);
    }
}

int AnSliderControl::value() const
{
    return m_slider ? m_slider->value() : 0;
}

QSlider* AnSliderControl::slider() const
{
    return m_slider;
}

QSpinBox* AnSliderControl::spinBox() const
{
    return m_spinBox;
}

void AnSliderControl::refreshTheme()
{
    if (!m_label || !m_slider || !m_spinBox)
    {
        return;
    }

    refreshTokens();
    const auto& tokenSet = tokens();

    if (auto* rootLayout = qobject_cast<QVBoxLayout*>(layout()))
    {
        rootLayout->setSpacing(tokenSet.spacing.xs);
    }

    m_label->setFont(tokenSet.typography.caption);
    m_spinBox->setFont(tokenSet.typography.body);

    // Label color via palette
    QPalette labelPal = m_label->palette();
    labelPal.setColor(QPalette::WindowText, tokenSet.colors.textSecondary);
    m_label->setPalette(labelPal);

    // Re-polish slider and spinbox so the proxy style paints them correctly
    m_slider->style()->unpolish(m_slider);
    m_slider->style()->polish(m_slider);
    m_slider->update();

    m_spinBox->style()->unpolish(m_spinBox);
    m_spinBox->style()->polish(m_spinBox);
    m_spinBox->update();
}

} // namespace an::ui
