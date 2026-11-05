// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPalette>
#include <QSpinBox>
#include <QtTest/QTest>

#include "components/AnCollapsibleSection.h"
#include "processes/ImageIntegration/dialogs/BlinkComparatorDialog.h"
#include "processes/ImageIntegration/controls/LightCalibrationSettingsDialog.h"
#include "processes/ImageIntegration/controls/LocalNormalizationSettingsDialog.h"
#include "processes/ImageIntegration/controls/StackingSettingsDialog.h"

namespace
{
void ensureApp()
{
    if (QApplication::instance())
    {
        return;
    }

    static int argc = 0;
    static char* argv[] = {nullptr};
    static QApplication app(argc, argv);
    (void)app;
}

class ScopedAppPalette
{
public:
    ScopedAppPalette() : m_original(QApplication::palette()) {}

    ~ScopedAppPalette()
    {
        QApplication::setPalette(m_original);
    }

private:
    QPalette m_original;
};

QPalette makePalette(const QColor& windowColor, const QColor& baseColor, const QColor& textColor)
{
    QPalette palette = QApplication::palette();
    palette.setColor(QPalette::Window, windowColor);
    palette.setColor(QPalette::Base, baseColor);
    palette.setColor(QPalette::WindowText, textColor);
    palette.setColor(QPalette::Text, textColor);
    palette.setColor(QPalette::ButtonText, textColor);
    return palette;
}

an::ui::AnCollapsibleSection* findSectionByTitle(QWidget& parent, const QString& title)
{
    const auto widgets = parent.findChildren<QWidget*>();
    for (auto* widget : widgets)
    {
        auto* section = dynamic_cast<an::ui::AnCollapsibleSection*>(widget);
        if (section && section->title() == title)
        {
            return section;
        }
    }

    return nullptr;
}
} // namespace

TEST(DialogModernizationLifecycle, DialogsUseWindowModalLifecycle)
{
    ensureApp();

    BlinkComparatorDialog blink({QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")}, nullptr, false);
    LocalNormalizationSettingsDialog localNormalization;
    LightCalibrationSettings calibrationSettings{};
    LightCalibrationSettingsDialog lightCalibration(calibrationSettings, QStringLiteral("Group A"));

    for (QDialog* dialog : {static_cast<QDialog*>(&blink), static_cast<QDialog*>(&localNormalization),
                            static_cast<QDialog*>(&lightCalibration)})
    {
        ASSERT_NE(dialog, nullptr);
        EXPECT_TRUE(dialog->isModal());
        EXPECT_EQ(dialog->windowModality(), Qt::WindowModal);
        EXPECT_FALSE(dialog->windowFlags().testFlag(Qt::WindowContextHelpButtonHint));
    }
}

// The local normalization dialog offers only what a run reads: no growth factor, which nothing in the normalization
// ever read, and - for a stack that fits each frame against its reference frame - nothing only the integration's
// analysis reads, with the sentence that says why.
TEST(DialogModernizationLifecycle, TheLocalNormalizationDialogOffersOnlyWhatTheRunReads)
{
    ensureApp();
    LocalNormalizationSettingsDialog integration;
    EXPECT_EQ(integration.findChild<QWidget*>(QStringLiteral("growthFactor")), nullptr);
    auto* offeredNote = integration.findChild<QLabel*>(QStringLiteral("referenceAnalysisNote"));
    ASSERT_NE(offeredNote, nullptr);
    EXPECT_TRUE(offeredNote->isHidden()) << "nothing is left out to explain";
    LocalNormalizationSettingsDialog standalone;
    const QString reason = QStringLiteral("This stack fits each frame against its reference frame.");
    standalone.setReferenceAnalysisOffered(false, reason);
    auto* withheldNote = standalone.findChild<QLabel*>(QStringLiteral("referenceAnalysisNote"));
    ASSERT_NE(withheldNote, nullptr);
    EXPECT_FALSE(withheldNote->isHidden());
    EXPECT_EQ(withheldNote->text(), reason);
    for (const char* name :
         {"referenceGroupBox", "evaluationCriteria", "scaleEvaluation", "psfType", "maxStars", "minDetectionSnr",
          "allowClusteredSources", "hotPixelMedianRadius", "noiseReductionRadius"})
    {
        auto* offered = integration.findChild<QWidget*>(QString::fromLatin1(name));
        auto* withheld = standalone.findChild<QWidget*>(QString::fromLatin1(name));
        ASSERT_NE(offered, nullptr) << name;
        ASSERT_NE(withheld, nullptr) << name;
        EXPECT_FALSE(offered->isHidden()) << name;
        EXPECT_TRUE(withheld->isHidden()) << name;
    }
}

// The dark multiplication factor scales the dark of every light while dark optimization is off, and of every light the
// optimization cannot measure - a colour light, or one smaller than its central square - while it is on, so it is
// offered either way. It was offered only while the optimization was on, so it could be changed only where a grey
// light large enough ignores it. The pedestal is said as the engine reads it: in 16-bit codes, and nothing clamped.
TEST(DialogModernizationLifecycle, TheLightCalibrationDialogOffersAndSaysWhatTheRunReads)
{
    ensureApp();
    LightCalibrationSettings settings;
    settings.darkOptimization = false;
    LightCalibrationSettingsDialog dialog(settings, QStringLiteral("Group A"));
    auto* optimization = dialog.findChild<QCheckBox*>(QStringLiteral("darkOptimization"));
    auto* factor = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("darkMultiplicationFactor"));
    auto* pedestal = dialog.findChild<QSpinBox*>(QStringLiteral("pedestal"));
    ASSERT_NE(optimization, nullptr);
    ASSERT_NE(factor, nullptr);
    ASSERT_NE(pedestal, nullptr);
    EXPECT_TRUE(factor->isEnabled()) << "a fixed dark scaling reads the factor";
    optimization->setChecked(true);
    EXPECT_TRUE(factor->isEnabled()) << "a light the optimization cannot measure reads the factor";
    optimization->setChecked(false);
    EXPECT_TRUE(factor->isEnabled());
    EXPECT_FALSE(pedestal->toolTip().contains(QStringLiteral("clamped automatically")));
    EXPECT_TRUE(pedestal->toolTip().contains(QStringLiteral("65,535"))) << pedestal->toolTip().toStdString();
}

TEST(DialogModernizationLifecycle, DialogsRespectPaletteVariantChanges)
{
    ensureApp();
    ScopedAppPalette paletteGuard;

    QApplication::setPalette(makePalette(QColor(24, 30, 38), QColor(20, 26, 34), QColor(220, 228, 236)));
    BlinkComparatorDialog darkBlink({QStringLiteral("C:/light_a.fit")}, nullptr, false);
    LocalNormalizationSettingsDialog darkLocalNormalization;
    LightCalibrationSettings darkCalibrationSettings{};
    LightCalibrationSettingsDialog darkLightCalibration(darkCalibrationSettings, QStringLiteral("Group A"));

    const QColor darkBlinkWindow = darkBlink.palette().color(QPalette::Window);
    const QColor darkLocalWindow = darkLocalNormalization.palette().color(QPalette::Window);
    const QColor darkCalibrationWindow = darkLightCalibration.palette().color(QPalette::Window);

    QApplication::setPalette(makePalette(QColor(246, 248, 252), QColor(252, 253, 255), QColor(32, 44, 58)));
    BlinkComparatorDialog lightBlink({QStringLiteral("C:/light_a.fit")}, nullptr, false);
    LocalNormalizationSettingsDialog lightLocalNormalization;
    LightCalibrationSettings lightCalibrationSettings{};
    LightCalibrationSettingsDialog lightLightCalibration(lightCalibrationSettings, QStringLiteral("Group A"));

    const QColor lightBlinkWindow = lightBlink.palette().color(QPalette::Window);
    const QColor lightLocalWindow = lightLocalNormalization.palette().color(QPalette::Window);
    const QColor lightCalibrationWindow = lightLightCalibration.palette().color(QPalette::Window);

    EXPECT_NE(darkBlinkWindow, lightBlinkWindow);
    EXPECT_NE(darkLocalWindow, lightLocalWindow);
    EXPECT_NE(darkCalibrationWindow, lightCalibrationWindow);
}

TEST(DialogModernizationLifecycle, CalibrationDialogCollapsingSectionsResizesHeightWithoutChangingWidth)
{
    ensureApp();

    LightCalibrationSettings settings{};
    LightCalibrationSettingsDialog dialog(settings, QStringLiteral("Group A"));
    dialog.show();
    QApplication::processEvents();

    auto* section = findSectionByTitle(dialog, QStringLiteral("Cosmetic Correction"));
    ASSERT_NE(section, nullptr);
    ASSERT_NE(section->toggleButton(), nullptr);

    const int expandedHeight = dialog.height();
    const int expandedWidth = dialog.width();

    QTest::mouseClick(section->toggleButton(), Qt::LeftButton);
    QTest::qWait(section->animationDuration() + 80);
    QApplication::processEvents();

    const int collapsedHeight = dialog.height();
    const int collapsedWidth = dialog.width();

    EXPECT_LT(collapsedHeight, expandedHeight);
    EXPECT_EQ(collapsedWidth, expandedWidth);
}

TEST(DialogModernizationLifecycle, StackingDialogSectionsResizeHeightWithoutChangingWidth)
{
    ensureApp();

    StackingSettingsDialog dialog(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Group A"));
    dialog.show();
    QApplication::processEvents();

    auto* rejectionSection = findSectionByTitle(dialog, QStringLiteral("Rejection Settings"));
    ASSERT_NE(rejectionSection, nullptr);
    ASSERT_NE(rejectionSection->toggleButton(), nullptr);

    const int expandedHeight = dialog.height();
    const int expandedWidth = dialog.width();

    QTest::mouseClick(rejectionSection->toggleButton(), Qt::LeftButton);
    QTest::qWait(rejectionSection->animationDuration() + 80);
    QApplication::processEvents();

    const int collapsedHeight = dialog.height();
    const int collapsedWidth = dialog.width();

    EXPECT_LT(collapsedHeight, expandedHeight);
    EXPECT_EQ(collapsedWidth, expandedWidth);

    auto* drizzleSection = findSectionByTitle(dialog, QStringLiteral("Drizzle"));
    ASSERT_NE(drizzleSection, nullptr);
    ASSERT_NE(drizzleSection->toggleButton(), nullptr);
    ASSERT_FALSE(drizzleSection->isExpanded());

    const int drizzleCollapsedHeight = dialog.height();
    const int drizzleCollapsedWidth = dialog.width();

    QTest::mouseClick(drizzleSection->toggleButton(), Qt::LeftButton);
    QTest::qWait(drizzleSection->animationDuration() + 80);
    QApplication::processEvents();

    const int drizzleExpandedHeight = dialog.height();
    const int drizzleExpandedWidth = dialog.width();

    EXPECT_GT(drizzleExpandedHeight, drizzleCollapsedHeight);
    EXPECT_EQ(drizzleExpandedWidth, drizzleCollapsedWidth);
}
