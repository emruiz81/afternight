// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A labeled field: a label beside a field - the panel's gap between them, the field at least a control high - or above
// it, with an optional hint in the caption's size and the secondary text, under the label beside a field and under the
// field below one. The label is the body text in the primary text. A click on the label gives the field the focus, and
// assistive technology reads the field by the label and its description by the hint.

#include "RenderTestSupport.h"

#include "components/AnLabeledField.h"
#include "theme/ComponentParts.h"

#include <QFontMetricsF>
#include <QLineEdit>
#include <QTest>

#include <algorithm>
#include <cmath>

namespace
{

using an::tests::widgets::expectColour;
using an::tests::widgets::forEachLook;
using an::tests::widgets::logicalPixels;
using an::tests::widgets::recordedText;
using an::ui::AnLabeledField;
using an::ui::DesignTokens;
using namespace an::ui::parts;

} // namespace

// Beside: the label, the gap, the field; the row as high as the field and at least a control; the hint under the
// label. The label's column is as wide as its text, or as asked.
TEST(AnLabeledField, BesideLaysTheLabelTheGapAndTheField)
{
    forEachLook([](const DesignTokens& t) {
        auto* const edit = new QLineEdit;
        AnLabeledField field(QStringLiteral("Output directory"), edit);
        an::tests::widgets::showOffScreen(field);
        EXPECT_EQ(field.placement(), AnLabeledField::LabelPlacement::Beside);
        EXPECT_EQ(field.field(), edit);
        EXPECT_EQ(edit->parentWidget(), &field);
        field.resize(field.sizeHint() + QSize(100, 0));
        const an::ui::testing::RecordedText label = recordedText(field, QStringLiteral("Output directory"));
        ASSERT_EQ(label.text, QStringLiteral("Output directory"));
        EXPECT_EQ(logicalPixels(label.font), t.metrics.fontBody);
        expectColour(label.colour, t.colors.textPrimary, "the label", 0);
        const int labelWidth = textWidth(label.font, QStringLiteral("Output directory"));
        EXPECT_EQ(edit->geometry().left(), labelWidth + t.metrics.gap);
        EXPECT_EQ(edit->geometry().right(), field.width() - 1);
        EXPECT_GE(edit->height(), t.metrics.control);
        EXPECT_EQ(field.sizeHint().height(), (std::max)(t.metrics.control, edit->sizeHint().height()));
        EXPECT_EQ(field.sizeHint().width(), labelWidth + t.metrics.gap + edit->sizeHint().width());

        const int column = labelWidth + 40;
        field.setLabelWidth(column);
        EXPECT_EQ(field.labelWidth(), column);
        field.resize(field.sizeHint());
        EXPECT_EQ(edit->geometry().left(), column + t.metrics.gap);
        EXPECT_EQ(recordedText(field, QStringLiteral("Output")).text, QStringLiteral("Output directory"));

        // Narrower than its label, the column elides it and gives it whole in the tooltip.
        field.setLabelWidth(labelWidth / 2);
        field.resize(field.sizeHint());
        EXPECT_NE(recordedText(field, QStringLiteral("Output")).text, QStringLiteral("Output directory"));
        EXPECT_EQ(field.toolTip(), QStringLiteral("Output directory"));

        // The column of its own is as wide as the label and the hint under it.
        field.setLabelWidth(0);
        const QString hintText = QStringLiteral("Where the masters are written, kept with the project");
        field.setHint(hintText);
        EXPECT_EQ(field.hint(), hintText);
        field.resize(field.sizeHint());
        EXPECT_TRUE(field.toolTip().isEmpty());
        const an::ui::testing::RecordedText hint = recordedText(field, QStringLiteral("Where"));
        ASSERT_EQ(hint.text, hintText) << an::tests::widgets::describedTexts(field).toStdString();
        EXPECT_EQ(logicalPixels(hint.font), t.metrics.fontCaption);
        expectColour(hint.colour, t.colors.textSecondary, "the hint", 0);
        EXPECT_EQ(edit->geometry().left(), textWidth(hint.font, hintText) + t.metrics.gap);
        EXPECT_GE(field.sizeHint().height(), t.metrics.control);
    });
}

// Above: the label over the field, a few pixels between them, the field as wide as the whole; the hint under the
// field.
TEST(AnLabeledField, AboveLaysTheLabelOverTheField)
{
    forEachLook([](const DesignTokens& t) {
        auto* const edit = new QLineEdit;
        AnLabeledField field(QStringLiteral("Target image"), edit, AnLabeledField::LabelPlacement::Above);
        an::tests::widgets::showOffScreen(field);
        field.resize(field.sizeHint() + QSize(80, 0));
        const an::ui::testing::RecordedText label = recordedText(field, QStringLiteral("Target image"));
        ASSERT_EQ(label.text, QStringLiteral("Target image"));
        const int lineHeight = static_cast<int>(std::ceil(QFontMetricsF(label.font).height()));
        EXPECT_EQ(edit->geometry().top(), lineHeight + kFieldLabelGapAbove);
        EXPECT_EQ(edit->geometry().left(), 0);
        EXPECT_EQ(edit->width(), field.width());
        EXPECT_EQ(field.sizeHint().height(),
                  lineHeight + kFieldLabelGapAbove + (std::max)(t.metrics.control, edit->sizeHint().height()));

        field.setHint(QStringLiteral("The image the process changes"));
        field.resize(field.sizeHint());
        EXPECT_GT(field.sizeHint().height(), edit->geometry().bottom() + 1) << "the hint lies under the field";
    });
}

// A click on the label gives the field the focus; the field is named by the label and described by the hint.
TEST(AnLabeledField, TheLabelNamesTheFieldAndAClickOnItFocusesIt)
{
    QWidget window;
    auto* const edit = new QLineEdit;
    auto* const field =
        new AnLabeledField(QStringLiteral("Folder"), edit, AnLabeledField::LabelPlacement::Beside, &window);
    field->setHint(QStringLiteral("Kept with the project"));
    field->resize(field->sizeHint());
    EXPECT_EQ(an::tests::widgets::accessibleName(*edit), QStringLiteral("Folder"));
    QAccessibleInterface* const iface = QAccessible::queryAccessibleInterface(edit);
    ASSERT_NE(iface, nullptr);
    EXPECT_EQ(iface->text(QAccessible::Description), QStringLiteral("Kept with the project"));
    field->setLabelText(QStringLiteral("Output folder"));
    EXPECT_EQ(field->labelText(), QStringLiteral("Output folder"));
    EXPECT_EQ(an::tests::widgets::accessibleName(*edit), QStringLiteral("Output folder"));

    window.show();
    QTest::mouseClick(field, Qt::LeftButton, Qt::NoModifier, QPoint(2, field->height() / 2));
    EXPECT_EQ(window.focusWidget(), edit);
}
