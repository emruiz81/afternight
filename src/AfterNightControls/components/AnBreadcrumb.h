// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A breadcrumb: where the user is, as a place and the item in it.

#include "AfterNightControlsExport.h"

#include <QLabel>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>

#include <verdigris/wobjectdefs.h>

#include <vector>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

namespace an::ui
{

/// Where the user is, as its parts - "Workspace 1" then "stacked_3008x3004_Red_Caldwell_77"; "Image Integration" then
/// "Carina Nebula LRGB" - at the small size, a chevron in the tertiary text between two parts, every part in the
/// secondary text but the last, which is the primary text in semibold. An icon button's row high.
///
/// A name is never shortened to a part of itself: when the room is short, the last part is elided in the middle down to
/// twelve average characters, then the others down to four, so every part keeps its start and its end; the whole path
/// is then the tooltip. A click on a part says which (`partActivated`). `QLabel::text()` is the whole path, which
/// assistive technology reads as static text.
class AFTERNIGHTCONTROLS_API AnBreadcrumb : public QLabel
{
    W_OBJECT(AnBreadcrumb)

public:
    explicit AnBreadcrumb(QWidget* parent = nullptr);

    void setParts(const QStringList& parts);
    [[nodiscard]] QStringList parts() const;

    /// The parts as they are shown at the breadcrumb's width, elided where the room is short.
    [[nodiscard]] QStringList shownParts() const;

    /// Whether a part is elided at the breadcrumb's width.
    [[nodiscard]] bool isElided() const;

    /// The room of every part whole.
    [[nodiscard]] QSize sizeHint() const override;

    /// The room of every part at its floor: the last at twelve average characters, the others at four.
    [[nodiscard]] QSize minimumSizeHint() const override;

    /// Told when a part is clicked, with its index.
void partActivated(int index) W_SIGNAL(partActivated, index)

    protected : void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    /// A part as it is laid out: where it stands and what of it is shown.
    struct ShownPart
    {
        QRectF rect;
        QString text;
    };

    [[nodiscard]] std::vector<ShownPart> layoutAt(int width) const;
    [[nodiscard]] int partAt(const QPointF& position) const;
    void refreshToolTip();

    QStringList m_parts;
    int m_pressed = -1;
};

} // namespace an::ui
