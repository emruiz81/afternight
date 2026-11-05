// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Star Reduction dialog as its builder makes it: the widgets the window reads with the defaults of the script it
/// ports, a stretch factor held strictly inside the unit interval, the controls pane given the width its controls ask
/// for, the dialog made and let go repeatedly, and a tooltip on every parameter.

#include <gtest/gtest.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QApplication>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QWidget>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "processes/StarReductionProcessUiBuilder.h"
#include "theme/AnDesignTokens.h"

namespace
{

// The dialog opens at the defaults of the script it ports - a preview on, a new reduced image and a stars image asked
// for under automatic names, a stretch factor of 0.2 strictly inside the unit interval its midtones transfer is defined
// on, one iteration - laid out with the theme's small spacing, with no preview-size controls, and the controls pane is
// given the width its controls ask for once the window is shown.
TEST(StarReductionProgrammaticUiTest, ConstructorDefaultsMatchPixInsightPortContract)
{
    const auto& t = an::ui::tokens();

    QWidget host;
    auto ui = an::ui::processes::buildStarReductionProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.previewLayout, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.starlessImageSelector, nullptr);
    ASSERT_NE(ui.starlessGeneratorSelector, nullptr);
    ASSERT_NE(ui.generateStarlessView, nullptr);
    ASSERT_NE(ui.initializeSmallStarProtection, nullptr);
    ASSERT_NE(ui.method, nullptr);
    ASSERT_NE(ui.stretchFactor, nullptr);
    ASSERT_NE(ui.mode, nullptr);
    ASSERT_NE(ui.iterations, nullptr);
    ASSERT_NE(ui.createNewImage, nullptr);
    ASSERT_NE(ui.createStarsImage, nullptr);
    ASSERT_NE(ui.preview, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);
    EXPECT_EQ(host.findChild<QGroupBox*>(QStringLiteral("previewDimensionsGroup")), nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Star Reduction"));
    EXPECT_EQ(host.size(), QSize(760, 680));
    EXPECT_EQ(host.minimumSize(), QSize(360, 260));
    EXPECT_EQ(ui.horizontalLayoutMain->spacing(), t.spacing.sm);
    EXPECT_EQ(ui.horizontalLayoutMain->contentsMargins().left(), t.spacing.sm);

    EXPECT_TRUE(ui.preview->isChecked());
    EXPECT_FALSE(ui.reduceTargetImage->isChecked());
    EXPECT_TRUE(ui.createNewImage->isChecked());
    EXPECT_TRUE(ui.createStarsImage->isChecked());
    EXPECT_FALSE(ui.enableReverseStretch->isChecked());
    EXPECT_EQ(ui.newImageId->text(), QStringLiteral("<Auto>"));
    EXPECT_EQ(ui.starImageId->text(), QStringLiteral("<Auto>"));
    EXPECT_DOUBLE_EQ(ui.stretchFactor->value(), 0.2);
    EXPECT_DOUBLE_EQ(ui.stretchFactor->minimum(), 0.01);
    EXPECT_DOUBLE_EQ(ui.stretchFactor->maximum(), 0.99);
    EXPECT_EQ(ui.iterations->value(), 1);

    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Ok), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);

    auto* splitter = host.findChild<QSplitter*>(QStringLiteral("mainSplitter"));
    auto* controlsPanel = host.findChild<QWidget*>(QStringLiteral("controlsPanel"));
    ASSERT_NE(splitter, nullptr);
    ASSERT_NE(controlsPanel, nullptr);
    host.show();
    QApplication::processEvents();
    const QList<int> splitter_sizes = splitter->sizes();
    ASSERT_GE(splitter_sizes.size(), 2);
    const int preview_minimum_width = splitter->widget(1)->minimumWidth();
    const int available_controls_width =
        (std::max)(0, splitter->width() - splitter->handleWidth() - preview_minimum_width);
    EXPECT_GE(splitter_sizes.front(), (std::min)(controlsPanel->sizeHint().width(), available_controls_width));
}

// The dialog can be made and let go again and again, each time whole.
TEST(StarReductionProgrammaticUiTest, UiLifecycleRepeatedCreateDestroy)
{
    for (int i = 0; i < 3; ++i)
    {
        auto host = std::make_unique<QWidget>();
        auto ui = an::ui::processes::buildStarReductionProcessUi(host.get());
        ASSERT_NE(ui.targetImageSelector, nullptr);
        ASSERT_NE(ui.buttonBox, nullptr);
    }
}

// Every parameter and every button says what it does in a tooltip of more than a few words.
TEST(StarReductionProgrammaticUiTest, ParameterControlsExposeTooltips)
{
    QWidget host;
    auto ui = an::ui::processes::buildStarReductionProcessUi(&host);

#if QT_CONFIG(tooltip)
    const std::vector<std::pair<QString, QWidget*>> controls = {
        {QStringLiteral("targetImageSelector"), ui.targetImageSelector},
        {QStringLiteral("starlessImageSelector"), ui.starlessImageSelector},
        {QStringLiteral("starlessGeneratorSelector"), ui.starlessGeneratorSelector},
        {QStringLiteral("generateStarlessView"), ui.generateStarlessView},
        {QStringLiteral("initializeSmallStarProtection"), ui.initializeSmallStarProtection},
        {QStringLiteral("enableSmallStarProtection"), ui.enableSmallStarProtection},
        {QStringLiteral("method"), ui.method},
        {QStringLiteral("stretchFactor"), ui.stretchFactor},
        {QStringLiteral("mode"), ui.mode},
        {QStringLiteral("iterations"), ui.iterations},
        {QStringLiteral("reduceTargetImage"), ui.reduceTargetImage},
        {QStringLiteral("createNewImage"), ui.createNewImage},
        {QStringLiteral("newImageId"), ui.newImageId},
        {QStringLiteral("createStarsImage"), ui.createStarsImage},
        {QStringLiteral("enableReverseStretch"), ui.enableReverseStretch},
        {QStringLiteral("starImageId"), ui.starImageId},
        {QStringLiteral("preview"), ui.preview},
    };

    for (const auto& [name, control] : controls)
    {
        ASSERT_NE(control, nullptr) << name.toStdString();
        const QString tooltip = control->toolTip().trimmed();
        EXPECT_FALSE(tooltip.isEmpty()) << name.toStdString();
        EXPECT_GT(tooltip.size(), 40) << name.toStdString();
    }

    ASSERT_NE(ui.buttonBox, nullptr);
    for (const auto button : {QDialogButtonBox::Ok, QDialogButtonBox::Close, QDialogButtonBox::Reset})
    {
        auto* widget = ui.buttonBox->button(button);
        ASSERT_NE(widget, nullptr);
        EXPECT_FALSE(widget->toolTip().trimmed().isEmpty());
    }
#endif // QT_CONFIG(tooltip)
}

} // namespace
