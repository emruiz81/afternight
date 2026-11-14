// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The drop indicators of the Observatory profile: the window the dock library shows while a view is dragged over a
/// dock area, made through the application's view factory in place of the library's, and the band that marks where the
/// view would land. Both are drawn from the tokens (`theme/ObservatoryDock.h`).

#include "AfterNightControlsExport.h"

#include <QWidget>

#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/core/indicators/ClassicDropIndicatorOverlay.h>
#include <kddockwidgets/core/views/ClassicIndicatorWindowViewInterface.h>
#include <kddockwidgets/qtwidgets/views/RubberBand.h>

/// The window of the drop indicators: a cross of five targets over the group under the cursor - the centre and the
/// four sides, a pitch apart - and four targets at the dock area's edges, each where the library offers it. A target
/// says which part of a group or of the area a view would take; the one under the cursor is the accent's.
class AFTERNIGHTCONTROLS_API AN_DropIndicators : public QWidget,
                                                 public KDDockWidgets::Core::ClassicIndicatorWindowViewInterface
{
public:
    explicit AN_DropIndicators(KDDockWidgets::Core::ClassicDropIndicatorOverlay* indicators);

    KDDockWidgets::DropLocation hover(QPoint globalPos) override;
    QPoint posForIndicator(KDDockWidgets::DropLocation location) const override;
    void updatePositions() override;
    void raise() override;
    void setVisible(bool visible) override;
    void resize(QSize size) override;
    void setGeometry(QRect geometry) override;
    bool isWindow() const override;
    void setObjectName(const QString& name) override;
    void updateIndicatorVisibility() override;

    /// Where a location's target lies in the window; empty when the library does not offer the location.
    [[nodiscard]] QRect targetRect(KDDockWidgets::DropLocation location) const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    KDDockWidgets::Core::ClassicDropIndicatorOverlay* m_indicators = nullptr;
    KDDockWidgets::DropLocation m_hovered = KDDockWidgets::DropLocation_None;
};

/// The band over the place a dragged view would land: the accent's soft share inside a two-pixel accent border.
class AFTERNIGHTCONTROLS_API AN_DropBand : public KDDockWidgets::QtWidgets::RubberBand
{
public:
    explicit AN_DropBand(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};
