// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// Theme token tests: the ImageIntegration tabs and controls use design tokens and respond to theme changes without
/// crashes.
#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QLabel>

#include "components/AnCardWidget.h"
#include "theme/AnDesignTokens.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/tabs/RegistrationTab.h"
#include "processes/ImageIntegration/tabs/StackingTab.h"
#include "processes/ImageIntegration/tabs/SummaryTab.h"
#include "processes/ImageIntegration/tabs/LightCalibrationPanel.h"
#include "processes/ImageIntegration/tabs/CalibrationGroupTable.h"
#include "processes/ImageIntegration/controls/StackingRecommendationDialog.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

void sendPaletteChange(QWidget* w)
{
    QEvent ev(QEvent::ApplicationPaletteChange);
    QApplication::sendEvent(w, &ev);
}

void sendThemeChange(QWidget* w)
{
    QEvent ev(QEvent::ThemeChange);
    QApplication::sendEvent(w, &ev);
}

} // namespace

// ---------------------------------------------------------------------------
// RegistrationTab — theme change resilience
// ---------------------------------------------------------------------------

TEST(ImageIntegrationThemeTokens, RegistrationTab_ThemeChangeEventsDoNotCrash)
{
    IntegrationDataModel model;
    RegistrationTab tab(&model);
    tab.resize(640, 420);

    sendPaletteChange(&tab);
    sendThemeChange(&tab);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// StackingTab — theme change resilience
// ---------------------------------------------------------------------------

TEST(ImageIntegrationThemeTokens, StackingTab_ThemeChangeEventsDoNotCrash)
{
    IntegrationDataModel model;
    StackingTab tab(&model);
    tab.resize(640, 420);

    sendPaletteChange(&tab);
    sendThemeChange(&tab);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// SummaryTab — theme change resilience + font token verification
// ---------------------------------------------------------------------------

TEST(ImageIntegrationThemeTokens, SummaryTab_ThemeChangeEventsDoNotCrash)
{
    IntegrationDataModel model;
    SummaryTab tab(&model);
    tab.resize(640, 420);

    sendPaletteChange(&tab);
    sendThemeChange(&tab);
    SUCCEED();
}

TEST(ImageIntegrationThemeTokens, SummaryTab_StatusTitleFontUsesTokenHeading1)
{
    IntegrationDataModel model;
    SummaryTab tab(&model);

    auto* title = tab.findChild<QLabel*>(QStringLiteral("lblStatusTitle"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->font(), an::ui::tokens().typography.heading1);
}

// ---------------------------------------------------------------------------
// LightCalibrationPanel — theme change resilience
// ---------------------------------------------------------------------------

TEST(ImageIntegrationThemeTokens, LightCalibrationPanel_ThemeChangeEventsDoNotCrash)
{
    IntegrationDataModel model;
    LightCalibrationPanel panel(&model);
    panel.resize(640, 300);

    sendPaletteChange(&panel);
    sendThemeChange(&panel);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// CalibrationGroupTable — theme change resilience
// ---------------------------------------------------------------------------

TEST(ImageIntegrationThemeTokens, CalibrationGroupTable_ThemeChangeEventsDoNotCrash)
{
    CalibrationGroupTable table;
    table.resize(400, 200);

    sendPaletteChange(&table);
    sendThemeChange(&table);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// GroupTreeModel — foreground role uses token-derived colors
// ---------------------------------------------------------------------------

TEST(ImageIntegrationThemeTokens, GroupTreeModel_ExcludedFrameForegroundUsesTokenColor)
{
    IntegrationDataModel model;
    GroupTreeModel treeModel(FrameType::Light, &model);

    // Add a frame so there is data to query.
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), 30.0, std::nullopt, std::nullopt, std::nullopt,
                       QStringLiteral("L"), QStringLiteral("M42"), QDateTime::currentDateTimeUtc(), QStringLiteral(""));
    FrameInfo frame(FrameId(QStringLiteral("C:/light1.fit")), QStringLiteral("C:/light1.fit"), meta);
    model.addFrames(FrameType::Light, {frame});

    // Process events so the tree model rebuilds via signal connections.
    QCoreApplication::processEvents();

    // Verify frame node is present.
    const QModelIndex groupIdx = treeModel.index(0, 0);
    ASSERT_TRUE(groupIdx.isValid());
    const QModelIndex frameIdx = treeModel.index(0, 0, groupIdx);
    ASSERT_TRUE(frameIdx.isValid());

    // Exclude via the tree model's own method (updates internal nodes directly).
    treeModel.setFramesExcluded(QStringList{QStringLiteral("C:/light1.fit")}, true);

    // After exclusion, row colors should be token-derived with accessibility-aware
    // foreground contrast.
    const QVariant fgVar = treeModel.data(frameIdx, Qt::ForegroundRole);
    ASSERT_TRUE(fgVar.isValid());
    const QColor fgColor = qvariant_cast<QColor>(fgVar);
    const QVariant bgVar = treeModel.data(frameIdx, Qt::BackgroundRole);
    ASSERT_TRUE(bgVar.isValid());
    const QColor bgColor = qvariant_cast<QColor>(bgVar);

    QColor expectedBackground = an::ui::tokens().colors.textTertiary;
    expectedBackground.setAlpha(35);
    const QColor expectedForeground = an::ui::bestContrastTextColor(
        an::ui::compositeOver(expectedBackground, an::ui::tokens().colors.bgInput),
        an::ui::tokens().colors.textTertiary, QColor(255, 255, 255), QColor(0, 0, 0), 4.5);

    EXPECT_EQ(bgColor, expectedBackground);
    EXPECT_EQ(fgColor, expectedForeground);
}

// ---------------------------------------------------------------------------
// RegistrationTab — additional font token verification
// ---------------------------------------------------------------------------

TEST(ImageIntegrationThemeTokens, RegistrationTab_ConstructionAndResizeDoNotCrash)
{
    IntegrationDataModel model;
    RegistrationTab tab(&model);
    tab.resize(800, 600);
    tab.resize(400, 300);
    SUCCEED();
}

TEST(ImageIntegrationThemeTokens, StackingTab_ConstructionAndResizeDoNotCrash)
{
    IntegrationDataModel model;
    StackingTab tab(&model);
    tab.resize(800, 600);
    tab.resize(400, 300);
    SUCCEED();
}

TEST(ImageIntegrationThemeTokens, RegistrationAndStackingCardsUseConsistentTokenContentMargins)
{
    IntegrationDataModel model;
    RegistrationTab registrationTab(&model);
    StackingTab stackingTab(&model);

    const auto& spacing = an::ui::tokens().spacing;
    const QMargins expectedMargins(spacing.sm, spacing.md, spacing.sm, spacing.sm);
    const QColor expectedBackdrop = an::ui::tokens().colors.bgSurface;

    const auto assertCardMargins = [&](QWidget& host, const QString& objectName) {
        auto* cardWidget = host.findChild<QWidget*>(objectName);
        auto* card = dynamic_cast<an::ui::AnCardWidget*>(cardWidget);
        ASSERT_NE(card, nullptr) << "Missing card: " << objectName.toStdString();
        ASSERT_NE(card->contentLayout(), nullptr) << "Missing content layout: " << objectName.toStdString();
        EXPECT_EQ(card->contentLayout()->contentsMargins(), expectedMargins)
            << "Unexpected content margins for " << objectName.toStdString();
        EXPECT_EQ(card->property("anCardBackdropColor").value<QColor>(), expectedBackdrop)
            << "Unexpected card backdrop for " << objectName.toStdString();
        EXPECT_TRUE(card->property("anCardFlat").toBool()) << "Expected flat card: " << objectName.toStdString();
    };

    assertCardMargins(registrationTab, QStringLiteral("grpRegistrationGroups"));
    assertCardMargins(registrationTab, QStringLiteral("grpFrameQuality"));
    assertCardMargins(stackingTab, QStringLiteral("grpIntegrationGroups"));
    assertCardMargins(stackingTab, QStringLiteral("grpFrameQualityPreview"));
    assertCardMargins(stackingTab, QStringLiteral("grpExecutionOptions"));

    auto* registrationFrameQualityContent = registrationTab.findChild<QWidget*>(QStringLiteral("frameQualityContent"));
    ASSERT_NE(registrationFrameQualityContent, nullptr);
    ASSERT_NE(registrationFrameQualityContent->layout(), nullptr);
    EXPECT_EQ(registrationFrameQualityContent->layout()->contentsMargins(), expectedMargins);
}

TEST(ImageIntegrationThemeTokens, SummaryTab_ConstructionAndResizeDoNotCrash)
{
    IntegrationDataModel model;
    SummaryTab tab(&model);
    tab.resize(800, 600);
    tab.resize(400, 300);
    SUCCEED();
}
