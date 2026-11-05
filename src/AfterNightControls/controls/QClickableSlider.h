// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QWidget>
#include <QSlider>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>

#include <verdigris/wobjectdefs.h>

#include <memory>

class QClickableSlider : public QSlider
{
    W_OBJECT(QClickableSlider) // Replaces Q_OBJECT

public:
    using QSlider::QSlider; // Inherit constructors

protected:
    void mousePressEvent(QMouseEvent* event) override;
};