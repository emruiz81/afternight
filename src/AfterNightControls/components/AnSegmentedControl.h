// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A segmented control: one choice of a few, shown side by side.

#include "AfterNightControlsExport.h"

#include <QSize>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <verdigris/wobjectdefs.h>

#include <vector>

class QAbstractButton;
class QEvent;
class QFocusEvent;
class QKeyEvent;
class QPaintEvent;

namespace an::ui
{

/// Segments of equal width that share one outline, a hairline between two, the outer corners the controls' radius; the
/// chosen segment is the accent's container with a check before its text in the container's text, the others' text the
/// primary text, at the small size in semibold. A control high, or a small control or an icon button's row high in its
/// smaller forms; the smallest draws no check. Hovered and pressed, a segment lays the state colour over itself;
/// disabled, the control is drawn at 38 % opacity.
///
/// The control is one stop for the keyboard: Left and Right move between the segments, Home and End to the ends, and
/// Space and Enter choose the segment moved to, which draws the focus ring while the focus came from the keyboard. The
/// pointer chooses a segment by a click. A choice - the user's or `setCurrentIndex` - is told once by
/// `currentIndexChanged`. Each segment is a button child of the control that takes no focus, which assistive technology
/// reads as a radio button named by its text and checked when chosen; the caller names the control.
class AFTERNIGHTCONTROLS_API AnSegmentedControl : public QWidget
{
    W_OBJECT(AnSegmentedControl)

public:
    /// How high the control is.
    enum class ControlHeight
    {
        Regular,
        Small,
        ExtraSmall
    };

    explicit AnSegmentedControl(QWidget* parent = nullptr);
    explicit AnSegmentedControl(const QStringList& segments, QWidget* parent = nullptr);
    ~AnSegmentedControl() override;

    AnSegmentedControl(const AnSegmentedControl&) = delete;
    AnSegmentedControl& operator=(const AnSegmentedControl&) = delete;

    /// Adds a segment at the end; its index.
    int addSegment(const QString& text);

    /// Replaces the segments; none is chosen.
    void setSegments(const QStringList& segments);

    [[nodiscard]] int count() const;
    [[nodiscard]] QString segmentText(int index) const;

    /// Chooses a segment, or none with -1; told when it changes.
    void setCurrentIndex(int index);
    [[nodiscard]] int currentIndex() const noexcept;

    /// The segment the keyboard stands on: the chosen one until Left, Right, Home or End move it.
    [[nodiscard]] int focusedSegment() const noexcept;

    void setControlHeight(ControlHeight height);
    [[nodiscard]] ControlHeight controlHeight() const noexcept;

    /// The button a segment is, for what reads it as assistive technology does.
    [[nodiscard]] QAbstractButton* segmentButton(int index) const;

    /// The width every segment takes: the widest text, its padding and, but in the smallest form, the check's room.
    [[nodiscard]] int segmentWidth() const;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

    /// Told when the chosen segment changes, with its index (-1 for none).
void currentIndexChanged(int index) W_SIGNAL(currentIndexChanged, index)

    protected : bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Tells the layout the segments' sizes may have changed.
    void refreshSegments();
    void choose(int index);

    std::vector<QAbstractButton*> m_segments;
    int m_current = -1;
    int m_focused = 0;
    ControlHeight m_height = ControlHeight::Regular;
    bool m_keyboardFocus = false;
};

} // namespace an::ui
