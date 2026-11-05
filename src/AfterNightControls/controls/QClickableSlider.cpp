// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "QClickableSlider.h"
#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(QClickableSlider)

void QClickableSlider::mousePressEvent(QMouseEvent* event)
{
    QStyleOptionSlider opt;
    this->initStyleOption(&opt);

    QRect handleRect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

    // Only respond if the click was outside the handle
    if (!handleRect.contains(event->pos()))
    {
        int newVal = 0;

        if (orientation() == Qt::Horizontal)
        {
            int clickX = event->pos().x();
            int sliderMin = style()->pixelMetric(QStyle::PM_SliderLength) / 2;
            int sliderMax = width() - sliderMin;

            double ratio = double(clickX - sliderMin) / (sliderMax - sliderMin);
            ratio = std::clamp(ratio, 0.0, 1.0);

            newVal = minimum() + int(ratio * (maximum() - minimum()));
        }
        else // Vertical
        {
            int clickY = event->pos().y();
            int sliderMin = style()->pixelMetric(QStyle::PM_SliderLength) / 2;
            int sliderMax = height() - sliderMin;

            double ratio = double(clickY - sliderMin) / (sliderMax - sliderMin);
            ratio = std::clamp(ratio, 0.0, 1.0);

            // For vertical sliders, lower values are at the bottom
            newVal = maximum() - int(ratio * (maximum() - minimum()));
        }

        setValue(newVal);
        event->accept();
    }
    else
    {
        QSlider::mousePressEvent(event); // default behavior
    }
}
