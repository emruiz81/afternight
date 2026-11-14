// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The reusable components under the Observatory profile: each reads the profile's colours and sizes - a card is the
// second surface with no border and no drop shadow, a section a hairline over a header in small capitals, a badge a tag
// of the status colour's soft share, a banner a note with a bar of its tone - and is rendered under dark, light and a
// tinted preset at both densities, at four device pixel ratios. Expected colours come from the tokens in force.

#include "components/AnButtonGroup.h"
#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "components/AnInputField.h"
#include "components/AnProgressCard.h"
#include "components/AnSectionHeader.h"
#include "components/AnSliderControl.h"
#include "components/AnStatusBadge.h"
#include "components/AnStyledTable.h"
#include "fixtures/ObservatoryLook.h"
#include "fixtures/PaintRecorder.h"
#include "theme/AnDesignTokens.h"

#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOption>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>

#include <gtest/gtest.h>

#include <functional>
#include <string>

namespace
{

using an::ui::ColorTokens;
using an::ui::DesignTokens;
using an::ui::testing::colourName;
using an::ui::testing::kRenderRatios;
using an::ui::testing::layered;
using an::ui::testing::ObservatoryLook;
using an::ui::testing::observatoryLooks;
using an::ui::testing::ObservatoryStyleScope;
using an::ui::testing::PaintRecorder;
using an::ui::testing::RenderedPicture;
using an::ui::testing::sameColour;

/// Runs a check under every look, the proxy style installed under the Observatory profile.
void forEachLook(const std::function<void(const DesignTokens&)>& check)
{
    ObservatoryStyleScope scope(observatoryLooks()[0]);
    for (const ObservatoryLook& look : observatoryLooks())
    {
        SCOPED_TRACE(look.name);
        scope.setLook(look);
        check(an::ui::tokens());
    }
}

void expectColour(const QColor& actual, const QColor& expected, const std::string& where, const int tolerance = 2)
{
    EXPECT_TRUE(sameColour(actual, expected, tolerance))
        << where << ": " << colourName(actual) << ", expected " << colourName(expected);
}

/// A font's size in logical pixels.
int logicalPixels(const QFont& font)
{
    return font.pixelSize() > 0 ? font.pixelSize() : qRound(font.pointSizeF() * 96.0 / 72.0);
}

/// Lays a widget out at a size, as a shown window would.
void layOut(QWidget& widget, const QSize size)
{
    widget.resize(size);
    if (QLayout* layout = widget.layout())
    {
        layout->activate();
    }
}

} // namespace

// A card is the second surface with the cards' radius, no border and no drop shadow; its padding is the panel's, its
// title the third heading in the primary text.
TEST(Components, ObservatoryCard)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnCardWidget card(QStringLiteral("Output"));
        auto* const body = new QVBoxLayout;
        body->addWidget(new QLabel(QStringLiteral("Folder")));
        card.setContentLayout(body);
        layOut(card, QSize(240, 140));
        EXPECT_EQ(card.layout()->contentsMargins(),
                  QMargins(t.metrics.pad, t.metrics.pad, t.metrics.pad, t.metrics.pad));
        EXPECT_EQ(card.headerLabel()->font().weight(), QFont::DemiBold);
        EXPECT_EQ(logicalPixels(card.headerLabel()->font()), logicalPixels(t.typography.heading3));
        expectColour(card.headerLabel()->palette().color(QPalette::WindowText), t.colors.textPrimary, "the title", 0);
        for (const qreal ratio : kRenderRatios)
        {
            const RenderedPicture picture = RenderedPicture::ofWidget(card, ratio, t.layers.surface0);
            expectColour(picture.at(200, 120), t.colors.bgCard, "the card");
            expectColour(picture.at(0.2, 70), t.colors.bgCard, "no border and no shadow at the edge");
            expectColour(picture.at(1.0, 1.0), t.materials.micaSurface, "the corner");
            expectColour(picture.at(5.0, 5.0), t.colors.bgCard, "inside the corner");
        }
    });
}

// A collapsible section is a hairline over a header a section header high, its title in small capitals of the secondary
// text, and no card: expanded, the header shows no fill.
TEST(Components, ObservatoryCollapsibleSection)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnCollapsibleSection section(QStringLiteral("Stars"));
        auto* const body = new QVBoxLayout;
        body->addWidget(new QLabel(QStringLiteral("Threshold")));
        section.setContentLayout(body);
        section.setExpanded(true);
        layOut(section, QSize(240, 120));
        QToolButton* const toggle = section.toggleButton();
        EXPECT_EQ(toggle->height(), t.metrics.sectionHeader);
        EXPECT_EQ(toggle->font().capitalization(), QFont::AllUppercase);
        EXPECT_EQ(toggle->font().weight(), QFont::DemiBold);
        EXPECT_EQ(logicalPixels(toggle->font()), t.metrics.fontCaption);
        expectColour(toggle->palette().color(QPalette::ButtonText), t.colors.textSecondary, "the title", 0);
        const QColor bg = t.layers.surface1;
        for (const qreal ratio : kRenderRatios)
        {
            const RenderedPicture picture = RenderedPicture::ofWidget(section, ratio, bg);
            expectColour(picture.at(200, 0.5), an::ui::compositeOver(t.colors.borderSubtle, bg), "the hairline");
            expectColour(picture.at(230, toggle->geometry().center().y()), bg, "the expanded header has no fill");
            expectColour(picture.at(230, 100), bg, "no card");
        }
    });
}

// A section header's title is the second heading, its description the small text in the secondary text, its action a
// quiet button in the accent's text.
TEST(Components, ObservatorySectionHeader)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnSectionHeader header(QStringLiteral("Recent"), QStringLiteral("Images you opened"),
                                       QStringLiteral("Show all"));
        layOut(header, QSize(320, 80));
        EXPECT_EQ(header.titleLabel()->font(), t.typography.heading2);
        EXPECT_EQ(logicalPixels(header.descriptionLabel()->font()), t.metrics.fontSmall);
        expectColour(header.descriptionLabel()->palette().color(QPalette::WindowText), t.colors.textSecondary,
                     "the description", 0);
        EXPECT_EQ(header.actionButton()->property("anButtonVariant").toString(), QStringLiteral("quiet"));
        PaintRecorder recorder(header.size());
        header.render(&recorder);
        const an::ui::testing::RecordedText* action = recorder.textHolding(QStringLiteral("Show all"));
        ASSERT_NE(action, nullptr);
        expectColour(action->colour, t.colors.textAccent, "the action", 0);
    });
}

// A status badge is a tag: a tag high, the status colour's soft share with no border, its text the status colour in the
// caption, semibold.
TEST(Components, ObservatoryStatusBadge)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnStatusBadge badge(QStringLiteral("Saved"), an::ui::AnStatusBadge::Status::Warning);
        EXPECT_EQ(badge.sizeHint().height(), t.metrics.tag);
        badge.resize(badge.sizeHint());
        EXPECT_EQ(badge.font().weight(), QFont::DemiBold);
        EXPECT_EQ(logicalPixels(badge.font()), t.metrics.fontCaption);
        const QColor bg = t.layers.surface2;
        const QColor fill = layered(t.colors.statusWarning, 0.16, bg);
        for (const qreal ratio : kRenderRatios)
        {
            const RenderedPicture picture = RenderedPicture::ofWidget(badge, ratio, bg);
            expectColour(picture.at(3, badge.height() / 2.0), fill, "the tag");
            expectColour(picture.at(0.2, badge.height() / 2.0), fill, "no border");
            expectColour(picture.at(0.2, 0.2), bg, "the corner");
        }
        PaintRecorder recorder(badge.size());
        badge.render(&recorder);
        ASSERT_FALSE(recorder.texts().empty());
        expectColour(recorder.texts().front().colour, t.colors.statusWarning, "the text", 0);
    });
}

// A slider control's slider and spin box are the standard ones in the profile's look - the spin box a control high, the
// slider's track the accent's track - with the panel's gap between them.
TEST(Components, ObservatorySliderControl)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnSliderControl control(QStringLiteral("Amount"), 0, 100, 25);
        layOut(control, QSize(300, control.sizeHint().height()));
        EXPECT_EQ(control.spinBox()->height(), t.metrics.control);
        const auto* row = qobject_cast<QBoxLayout*>(control.layout()->itemAt(1)->layout());
        ASSERT_NE(row, nullptr);
        EXPECT_EQ(row->spacing(), t.metrics.gap);
        expectColour(control.label()->palette().color(QPalette::WindowText), t.colors.textSecondary, "the label", 0);
        QSlider* const slider = control.slider();
        const QColor bg = t.layers.surface1;
        const RenderedPicture picture = RenderedPicture::ofWidget(*slider, 1.0, bg);
        expectColour(picture.at(slider->width() - 10, slider->height() / 2.0), t.colors.accentTrack, "the track");
        expectColour(picture.at(3, slider->height() / 2.0), t.colors.accentPrimary, "the filled track");
    });
}

// A button group's buttons are outlined, or filled with the accent for the primary one, with the panel's gap between.
TEST(Components, ObservatoryButtonGroup)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnButtonGroup group;
        QPushButton* const cancel = group.addButton(QStringLiteral("Cancel"));
        QPushButton* const run = group.addButton(QStringLiteral("Run"), an::ui::AnButtonGroup::ButtonVariant::Primary);
        layOut(group, QSize(300, t.metrics.control));
        EXPECT_EQ(group.layout()->spacing(), t.metrics.gap);
        EXPECT_EQ(cancel->height(), t.metrics.control);
        const QColor bg = t.layers.surface1;
        const RenderedPicture cancelPicture = RenderedPicture::ofWidget(*cancel, 1.0, bg);
        expectColour(cancelPicture.at(6, cancel->height() / 2.0), bg, "an outlined button has no fill");
        expectColour(cancelPicture.at(0.2, cancel->height() / 2.0), an::ui::compositeOver(t.colors.borderPrimary, bg),
                     "its outline");
        const RenderedPicture runPicture = RenderedPicture::ofWidget(*run, 1.0, bg);
        expectColour(runPicture.at(6, run->height() / 2.0), t.colors.accentPrimary, "the primary button");
    });
}

// A styled table is the first surface with its rows a table row high, its selection the accent's soft share and its
// text the primary text, its header the third surface in the secondary text.
TEST(Components, ObservatoryStyledTable)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnStyledTable table;
        table.setColumnCount(2);
        table.setRowCount(3);
        table.setHorizontalHeaderLabels({QStringLiteral("Frame"), QStringLiteral("FWHM")});
        table.setItem(0, 0, new QTableWidgetItem(QStringLiteral("light_001")));
        // The first row: the second is an alternate row, laid over the alternate base.
        table.selectRow(0);
        layOut(table, QSize(320, 200));
        const QPalette palette = table.palette();
        expectColour(palette.color(QPalette::Base), t.layers.surface1, "the rows", 0);
        expectColour(palette.color(QPalette::HighlightedText), t.colors.textPrimary, "the selected text", 0);
        expectColour(table.horizontalHeader()->palette().color(QPalette::Window), t.layers.surface3, "the header", 0);
        expectColour(table.horizontalHeader()->palette().color(QPalette::WindowText), t.colors.textSecondary,
                     "the header's text", 0);
        EXPECT_EQ(table.rowHeight(0), t.metrics.rowTable);
        table.show();
        const RenderedPicture picture = RenderedPicture::ofWidget(*table.viewport(), 1.0, t.layers.surface1);
        const int y = table.rowViewportPosition(0) + table.rowHeight(0) / 2;
        expectColour(picture.at(table.viewport()->width() - 8, y),
                     an::ui::compositeOver(t.colors.accentSoft, t.layers.surface1), "the selected row");
        table.hide();
    });
}

// An input field's line edit is a field: a control high, the third surface with an outline.
TEST(Components, ObservatoryInputField)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnInputField field(QStringLiteral("Folder"), QStringLiteral("Choose a folder"));
        layOut(field, QSize(260, field.sizeHint().height()));
        QLineEdit* const edit = field.lineEdit();
        EXPECT_EQ(edit->height(), t.metrics.control);
        expectColour(field.label()->palette().color(QPalette::WindowText), t.colors.textSecondary, "the label", 0);
        const QColor bg = t.layers.surface1;
        for (const qreal ratio : kRenderRatios)
        {
            const RenderedPicture picture = RenderedPicture::ofWidget(*edit, ratio, bg);
            expectColour(picture.at(edit->width() - 6, edit->height() / 2.0), t.colors.bgInput, "the field");
            expectColour(picture.at(0.2, edit->height() / 2.0),
                         an::ui::compositeOver(t.colors.borderPrimary, t.colors.bgInput), "its outline");
        }
    });
}

// An info banner is a note: the tone's soft share with the controls' radius and no border, a three-pixel bar of the
// tone at its start, its message in the primary text.
TEST(Components, ObservatoryInfoBanner)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnInfoBanner banner(QStringLiteral("Saved"), an::ui::AnInfoBanner::Tone::Error);
        layOut(banner, QSize(300, 40));
        expectColour(banner.messageLabel()->palette().color(QPalette::WindowText), t.colors.textPrimary, "the message",
                     0);
        const QColor bg = t.layers.surface1;
        for (const qreal ratio : kRenderRatios)
        {
            const RenderedPicture picture = RenderedPicture::ofWidget(banner, ratio, bg);
            expectColour(picture.at(290, 20), layered(t.colors.statusError, 0.16, bg), "the note");
            expectColour(picture.at(1.5, 20), t.colors.statusError, "the bar");
            expectColour(picture.at(0.3, 0.3), bg, "the corner");
        }
    });
}

// A progress card is a card - the second surface, no border - holding a thin bar: the accent's track, filled with the
// accent.
TEST(Components, ObservatoryProgressCard)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnProgressCard card(QStringLiteral("Stacking"));
        card.setProgressRange(0, 100);
        card.setProgressValue(30);
        layOut(card, QSize(300, card.sizeHint().height()));
        QProgressBar* const bar = card.progressBar();
        EXPECT_LE(bar->height(), t.metrics.gap);
        const QColor bg = t.layers.surface0;
        const RenderedPicture picture = RenderedPicture::ofWidget(card, 1.0, bg);
        expectColour(picture.at(150, 4), t.colors.bgCard, "the card");
        expectColour(picture.at(0.2, card.height() / 2.0), t.colors.bgCard, "no border");
        const QPoint barCentre = bar->geometry().center();
        expectColour(picture.at(bar->geometry().left() + 20, barCentre.y() + 0.5), t.colors.accentPrimary, "done");
        expectColour(picture.at(bar->geometry().right() - 20, barCentre.y() + 0.5), t.colors.accentTrack, "to do");
    });
}
