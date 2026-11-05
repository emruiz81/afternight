/* Original Work Copyright (c) 2012-2014 Alexander Turkin
   Modified 2014 by William Hallatt
   Modified 2015 by Jacob Dawid
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

// AfterNight's copy of QtWaitingSpinner (https://github.com/snowwlex/QtWaitingSpinner), taken from the
// files as they have stood on that repository's master since 2015, under the MIT licence above.
// AfterNight changed: the class is exported from AfterNightControls, Qt's moc is not used and the timer
// is connected by function pointer, the colour is the theme's accent and follows a theme change, the
// line corners take the theme's small radius scaled by the roundness, the painter state is restored by a
// guard, and the code is reformatted to the repository's style. The provenance note is
// `docs/dev/WAITING_SPINNER_PROVENANCE.md`.

// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

// Qt includes
#include <QWidget>
#include <QTimer>
#include <QColor>

class AFTERNIGHTCONTROLS_API WaitingSpinnerWidget : public QWidget
{

public:
    /*! Constructor for "standard" widget behaviour - use this
     * constructor if you wish to, e.g. embed your widget in another. */
    WaitingSpinnerWidget(QWidget* parent = 0, bool centerOnParent = true, bool disableParentWhenSpinning = true);

    /*! Constructor - use this constructor to automatically create a modal
     * ("blocking") spinner on top of the calling widget/window.  If a valid
     * parent widget is provided, "centreOnParent" will ensure that
     * QtWaitingSpinner automatically centres itself on it, if not,
     * "centreOnParent" is ignored. */
    WaitingSpinnerWidget(Qt::WindowModality modality, QWidget* parent = 0, bool centerOnParent = true,
                         bool disableParentWhenSpinning = true);

public:
    void start();
    void stop();

public:
    void setColor(QColor color);
    void setRoundness(qreal roundness);
    void setMinimumTrailOpacity(qreal minimumTrailOpacity);
    void setTrailFadePercentage(qreal trail);
    void setRevolutionsPerSecond(qreal revolutionsPerSecond);
    void setNumberOfLines(int lines);
    void setLineLength(int length);
    void setLineWidth(int width);
    void setInnerRadius(int radius);
    void setText(QString text);

    QColor color();
    qreal roundness();
    qreal minimumTrailOpacity();
    qreal trailFadePercentage();
    qreal revolutionsPersSecond();
    int numberOfLines();
    int lineLength();
    int lineWidth();
    int innerRadius();
    qreal effectiveCornerRadius() const;

    bool isSpinning() const;

private:
    void rotate();

protected:
    void paintEvent(QPaintEvent* paintEvent) override;
    void changeEvent(QEvent* ev) override;

private:
    static int lineCountDistanceFromPrimary(int current, int primary, int totalNrOfLines);
    static QColor currentLineColor(int distance, int totalNrOfLines, qreal trailFadePerc, qreal minOpacity,
                                   QColor color);

    void initialize();
    void updateSize();
    void updateTimer();
    void updatePosition();

private:
    QColor _color;
    qreal _roundness; // 0..100
    qreal _minimumTrailOpacity;
    qreal _trailFadePercentage;
    qreal _revolutionsPerSecond;
    int _numberOfLines;
    int _lineLength;
    int _lineWidth;
    int _innerRadius;

private:
    WaitingSpinnerWidget(const WaitingSpinnerWidget&);
    WaitingSpinnerWidget& operator=(const WaitingSpinnerWidget&);

    QTimer* _timer;
    bool _centerOnParent;
    bool _disableParentWhenSpinning;
    int _currentCounter;
    bool _isSpinning;
};
