// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The notice an image view lays over its image while a run writes it.

#include "AfterNightControlsExport.h"
#include "controls/ViewChrome.h"

#include <QRect>
#include <QSize>
#include <QWidget>

#include <verdigris/wobjectdefs.h>

class QAbstractButton;
class QEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

namespace an::ui
{

/// Covers the widget it is a child of - the view's canvas - and follows its size: the canvas under `scrimSoft`, so the
/// image is still seen, and at its centre a pill on the scrim, `parts::kNoticePillHeight` high: a lock in the accent
/// over images, what runs in the text over images, how far it has come in the monospace with a thin bar, and the button
/// that shows the run. A notice with no progress draws neither the share nor the bar; one with no action has no button.
///
/// It takes the pointer's presses, releases and wheel turns, so the image under it is neither panned nor zoomed while
/// the run writes it; the button takes its own. It is named to assistive technology by the notice's text, and the
/// button by its label.
class AFTERNIGHTCONTROLS_API ViewProcessingNotice : public QWidget
{
    W_OBJECT(ViewProcessingNotice)

public:
    /// A notice over `covered`, hidden until it is shown.
    explicit ViewProcessingNotice(QWidget* covered);
    ~ViewProcessingNotice() override;

    ViewProcessingNotice(const ViewProcessingNotice&) = delete;
    ViewProcessingNotice& operator=(const ViewProcessingNotice&) = delete;

    /// Shows another notice's text, progress and action.
    void setNotice(const ProcessingNotice& notice);

    /// The notice shown.
    [[nodiscard]] const ProcessingNotice& notice() const noexcept;

    /// Where the pill is, in the notice's coordinates.
    [[nodiscard]] QRect pillRect() const;

    /// The pill's size: its padding, the lock, the text, the share and the bar when there is a progress, the button.
    [[nodiscard]] QSize pillSize() const;

    /// The button that shows the run, or null when the notice offers none.
    [[nodiscard]] QAbstractButton* actionButton() const;

    /// The text of the share done, "42 %", or empty when the notice has no progress.
    [[nodiscard]] QString progressText() const;

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    /// Places the button at the pill's end.
    void layoutPill();

    ProcessingNotice m_notice;
    QAbstractButton* m_button = nullptr;
};

} // namespace an::ui
