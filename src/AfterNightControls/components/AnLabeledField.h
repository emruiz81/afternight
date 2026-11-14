// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A labeled field: a field with its label beside it or above it, and an optional hint.

#include "AfterNightControlsExport.h"

#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include <verdigris/wobjectdefs.h>

class QEvent;
class QMouseEvent;
class QPaintEvent;
class QSpacerItem;

namespace an::ui
{

/// A field - any widget: a line edit, a combo box, a spin box, a switch - with its label in the body text and the
/// primary text, and an optional hint in the caption's size and the secondary text. Beside the field, the label stands
/// in a column as wide as its text or as asked, the panel's gap before the field, with the hint under it; above the
/// field, the label stands a few pixels over it and the hint under the field. The row is at least a control high and
/// the field takes the rest of the width.
///
/// The label names the field to assistive technology and the hint describes it, unless the caller named or described
/// the field itself; a click on the label gives the field the focus. The field is laid out by a layout; the label and
/// the hint are painted in the room it keeps for them, which is sized from the metrics when the widget is polished,
/// told of a change of style or given another text, and their colours are read where they are painted.
class AFTERNIGHTCONTROLS_API AnLabeledField : public QWidget
{
    W_OBJECT(AnLabeledField)

public:
    /// Where the label stands.
    enum class LabelPlacement
    {
        Beside,
        Above
    };

    /// A field and its label; the field becomes a child of this widget.
    AnLabeledField(const QString& label, QWidget* field, LabelPlacement placement = LabelPlacement::Beside,
                   QWidget* parent = nullptr);

    void setLabelText(const QString& label);
    [[nodiscard]] QString labelText() const;

    /// A hint under the label beside the field, or under the field below it; none when empty.
    void setHint(const QString& hint);
    [[nodiscard]] QString hint() const;

    /// The width of the label's column beside the field, so that the fields of several rows line up; 0 - the
    /// default - for the label's own width. A label wider than its column is elided and given whole in the tooltip.
    void setLabelWidth(int width);
    [[nodiscard]] int labelWidth() const noexcept;

    [[nodiscard]] QWidget* field() const noexcept;
    [[nodiscard]] LabelPlacement placement() const noexcept;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    /// Sizes the room the label and the hint are painted in, and the layout's gaps, from the tokens in force.
    void applyMetrics();
    void nameTheField();
    /// Where the label and the hint are painted, in the room the layout gives them.
    [[nodiscard]] QRect labelRect() const;
    [[nodiscard]] QRect hintRect() const;

    QString m_label;
    QString m_hint;
    QWidget* m_field = nullptr;
    LabelPlacement m_placement = LabelPlacement::Beside;
    int m_labelWidth = 0;
    QString m_namedAs;
    QString m_describedAs;
    /// The room of the label - and beside the field, of the hint under it.
    QSpacerItem* m_labelArea = nullptr;
    /// The room of the hint under the field, above it.
    QSpacerItem* m_hintArea = nullptr;
};

} // namespace an::ui
