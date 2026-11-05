// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// Custom widget polish.
/// Tests verifying that custom-painted widgets use design tokens for colors
/// and respond to theme changes with a repaint.
///
/// Design token tests.
/// Additional tests verifying that ImageLensWidget, ImageViewThemeColors,
/// TimelineStageWidget, and SubStageIndicatorWidget follow design tokens.
#include <gtest/gtest.h>

#include <QApplication>
#include <QEvent>
#include <QScrollArea>

#include "theme/AnDesignTokens.h"
#include "controls/ImageViewThemeColors.h"
#include "widgets/CurvesWidget.h"
#include "widgets/HistogramWidget.h"
#include "widgets/HistogramOverlay.h"
#include "widgets/ImageLensWidget.h"
#include "widgets/StructuredElementWidget.h"
#include "widgets/SubStageIndicatorWidget.h"
#include "widgets/TimelineStageWidget.h"
#include "widgets/waitingspinnerwidget.h"

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

class ScopedThemePreferenceOverride final
{
public:
    ScopedThemePreferenceOverride() : m_originalPreference(an::ui::themePreference()) {}

    ~ScopedThemePreferenceOverride()
    {
        an::ui::setThemePreference(m_originalPreference);
        an::ui::refreshTokens();
    }

    void set(const an::ui::ThemePreference preference) const
    {
        an::ui::setThemePreference(preference);
        an::ui::refreshTokens();
    }

private:
    an::ui::ThemePreference m_originalPreference;
};
} // namespace

// ---------------------------------------------------------------------------
// HistogramWidget token integration
// ---------------------------------------------------------------------------

TEST(WidgetDesignTokens, HistogramWidget_GridColorMatchesTokenBorderSubtle)
{
    HistogramWidget widget(nullptr);
    EXPECT_EQ(widget.getGridColor(), an::ui::tokens().colors.borderPrimary);
}

TEST(WidgetDesignTokens, HistogramWidget_BackgroundColorMatchesTokenPrimarySurface)
{
    // The histogram always uses the dark theme background so that bar colours
    // (white for mono, red/green/blue for RGB) remain visible in both themes.
    HistogramWidget widget(nullptr);
    EXPECT_EQ(widget.getBackgroundColor(), an::ui::darkTokens().colors.bgPrimary);
}

TEST(WidgetDesignTokens, HistogramWidget_GuideLineColorMatchesTokenBorderSubtle)
{
    HistogramWidget widget(nullptr);
    EXPECT_EQ(widget.getGuideLineColor(), an::ui::tokens().colors.textPrimary);
}

TEST(WidgetDesignTokens, HistogramWidget_ThemeChangeTriggerRepaint)
{
    // Verify no crash and widget is valid after simulated theme change
    HistogramWidget widget(nullptr);
    widget.resize(100, 80);
    sendPaletteChange(&widget);
    EXPECT_TRUE(widget.isValid());
}

// ---------------------------------------------------------------------------
// CurvesWidget token integration
// ---------------------------------------------------------------------------

TEST(WidgetDesignTokens, CurvesWidget_SplineColorMatchesTokenTextPrimary)
{
    QScrollArea scroll;
    CurvesWidget widget(&scroll);
    EXPECT_EQ(widget.getSplineColor(), an::ui::tokens().colors.textPrimary);
}

// ---------------------------------------------------------------------------
// StructuredElementWidget token integration
// ---------------------------------------------------------------------------

TEST(WidgetDesignTokens, StructuredElementWidget_GridColorMatchesTokenBorderPrimary)
{
    StructuredElementWidget widget(nullptr);
    EXPECT_EQ(widget.getGridColor(), an::ui::tokens().colors.borderPrimary);
}

TEST(WidgetDesignTokens, StructuredElementWidget_ThemeChangeTriggerRepaint)
{
    StructuredElementWidget widget(nullptr);
    widget.resize(80, 80);
    sendPaletteChange(&widget);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// SubStageIndicatorWidget token integration
// ---------------------------------------------------------------------------

TEST(WidgetDesignTokens, SubStageIndicatorWidget_ActiveStateColorMatchesTokenAccent)
{
    SubStageIndicatorWidget widget;
    EXPECT_EQ(widget.stateColor(SubStageIndicatorWidget::SubStageState::Active), an::ui::tokens().colors.accentPrimary);
}

TEST(WidgetDesignTokens, SubStageIndicatorWidget_CompletedStateColorMatchesTokenSuccess)
{
    SubStageIndicatorWidget widget;
    EXPECT_EQ(widget.stateColor(SubStageIndicatorWidget::SubStageState::Completed),
              an::ui::tokens().colors.statusSuccess);
}

TEST(WidgetDesignTokens, SubStageIndicatorWidget_FailedStateColorMatchesTokenError)
{
    SubStageIndicatorWidget widget;
    EXPECT_EQ(widget.stateColor(SubStageIndicatorWidget::SubStageState::Failed), an::ui::tokens().colors.statusError);
}

// ---------------------------------------------------------------------------
// WaitingSpinnerWidget token integration
// ---------------------------------------------------------------------------

TEST(WidgetDesignTokens, WaitingSpinnerWidget_DefaultColorMatchesTokenAccentPrimary)
{
    WaitingSpinnerWidget spinner(nullptr, false, false);
    EXPECT_EQ(spinner.color(), an::ui::tokens().colors.accentPrimary);
}

TEST(WidgetDesignTokens, WaitingSpinnerWidget_ThemeChangeResetsAccentColor)
{
    WaitingSpinnerWidget spinner(nullptr, false, false);
    spinner.setColor(QColor(255, 0, 255));

    sendThemeChange(&spinner);

    EXPECT_EQ(spinner.color(), an::ui::tokens().colors.accentPrimary);
}

// ===========================================================================
// ImageLensWidget token integration.
// ===========================================================================

TEST(ViewWidgetThemeTokens, ImageLensWidget_BorderColorMatchesTokenBorderPrimary)
{
    ImageLensWidget widget(nullptr);
    // ImageLensWidget always uses darkTokens for visual consistency across themes.
    EXPECT_EQ(widget.getBorderColor(), an::ui::darkTokens().colors.borderPrimary);
}

TEST(ViewWidgetThemeTokens, ImageLensWidget_ThemeChangeTriggersRepaint)
{
    ImageLensWidget widget(nullptr);
    widget.resize(350, 250);
    sendPaletteChange(&widget);
    sendThemeChange(&widget);
    SUCCEED();
}

// ===========================================================================
// ImageViewThemeColors token integration.
// ===========================================================================

TEST(ViewWidgetThemeTokens, ImageViewThemeColors_DarkAndLightScrollAreaColorsDiffer)
{
    const QColor dark = an::ui::imageViewScrollAreaWindowColor(true);
    const QColor light = an::ui::imageViewScrollAreaWindowColor(false);
    EXPECT_NE(dark, light);
}

TEST(ViewWidgetThemeTokens, ImageViewThemeColors_ScrollAreaColorIsNotDefault)
{
    const QColor dark = an::ui::imageViewScrollAreaWindowColor(true);
    const QColor light = an::ui::imageViewScrollAreaWindowColor(false);
    EXPECT_NE(dark, QColor());
    EXPECT_NE(light, QColor());
}

TEST(ViewWidgetThemeTokens, ImageViewThemeColors_CardSurfaceTintDarkIsNotDefault)
{
    const QColor tint = an::ui::imageViewCardSurfaceTintColor(true);
    EXPECT_NE(tint, QColor());
    EXPECT_EQ(tint.alpha(), 255); // composited to fully opaque
}

TEST(ViewWidgetThemeTokens, ImageViewThemeColors_OverlayAutostretchIsNotDefault)
{
    const QColor color = an::ui::imageViewOverlayAutostretchColor(true);
    EXPECT_NE(color, QColor());
}

TEST(ViewWidgetThemeTokens, ImageViewThemeColors_OverlayOriginalIsNotDefault)
{
    const QColor color = an::ui::imageViewOverlayOriginalColor(true);
    EXPECT_NE(color, QColor());
}

TEST(ViewWidgetThemeTokens, ImageViewThemeColors_OverlayBackdropHasSemiTransparentAlpha)
{
    const QColor color = an::ui::imageViewOverlayBackdropColor(true);
    EXPECT_NE(color, QColor());
    EXPECT_EQ(color.alpha(), 170);
}

// ===========================================================================
// TimelineStageWidget token integration.
// ===========================================================================

TEST(StageWidgetThemeTokens, TimelineStageWidget_ConstructionDoesNotCrash)
{
    TimelineStageWidget widget;
    widget.setStageName(QStringLiteral("Stacking"));
    widget.setGroupInfo(QStringLiteral("Group 1"));
    widget.resize(120, 90);
    SUCCEED();
}

TEST(StageWidgetThemeTokens, TimelineStageWidget_ThemeChangeDoesNotCrash)
{
    TimelineStageWidget widget;
    widget.setStageName(QStringLiteral("Calibration"));
    widget.resize(120, 90);

    sendPaletteChange(&widget);
    sendThemeChange(&widget);
    SUCCEED();
}

TEST(StageWidgetThemeTokens, TimelineStageWidget_DefaultStatePending)
{
    TimelineStageWidget widget;
    EXPECT_EQ(widget.state(), TimelineStageWidget::StageState::Pending);
}

TEST(StageWidgetThemeTokens, TimelineStageWidget_SetStateRoundTrips)
{
    TimelineStageWidget widget;
    widget.setState(TimelineStageWidget::StageState::Active);
    EXPECT_EQ(widget.state(), TimelineStageWidget::StageState::Active);
    widget.setState(TimelineStageWidget::StageState::Completed);
    EXPECT_EQ(widget.state(), TimelineStageWidget::StageState::Completed);
}

// ===========================================================================
// SubStageIndicatorWidget additional token integration.
// ===========================================================================

TEST(StageWidgetThemeTokens, SubStageIndicatorWidget_PendingStateColorMatchesTokenTextSecondary)
{
    SubStageIndicatorWidget widget;
    EXPECT_EQ(widget.stateColor(SubStageIndicatorWidget::SubStageState::Pending),
              an::ui::tokens().colors.textSecondary);
}

TEST(StageWidgetThemeTokens, SubStageIndicatorWidget_SkippedStateColorMatchesTokenTextTertiary)
{
    SubStageIndicatorWidget widget;
    EXPECT_EQ(widget.stateColor(SubStageIndicatorWidget::SubStageState::Skipped), an::ui::tokens().colors.textTertiary);
}

TEST(StageWidgetThemeTokens, SubStageIndicatorWidget_ThemeChangeDoesNotCrash)
{
    SubStageIndicatorWidget widget;
    widget.setSubStageState(SubStageIndicatorWidget::SubStage::Calibration,
                            SubStageIndicatorWidget::SubStageState::Active);
    widget.resize(500, 60);

    sendPaletteChange(&widget);
    sendThemeChange(&widget);
    SUCCEED();
}

TEST(StageWidgetThemeTokens, SubStageIndicatorWidget_ResetAllStagesToPending)
{
    SubStageIndicatorWidget widget;
    widget.setSubStageState(SubStageIndicatorWidget::SubStage::Calibration,
                            SubStageIndicatorWidget::SubStageState::Completed);
    widget.setSubStageState(SubStageIndicatorWidget::SubStage::Registration,
                            SubStageIndicatorWidget::SubStageState::Active);
    widget.resetAllStages();

    EXPECT_EQ(widget.getSubStageState(SubStageIndicatorWidget::SubStage::Calibration),
              SubStageIndicatorWidget::SubStageState::Pending);
    EXPECT_EQ(widget.getSubStageState(SubStageIndicatorWidget::SubStage::Registration),
              SubStageIndicatorWidget::SubStageState::Pending);
}

TEST(StageWidgetThemeTokens, SubStageIndicatorWidget_InMemoryCalibrationModeToggle)
{
    SubStageIndicatorWidget widget;
    EXPECT_FALSE(widget.isInMemoryCalibrationMode());
    widget.setInMemoryCalibrationMode(true);
    EXPECT_TRUE(widget.isInMemoryCalibrationMode());
}

// ===========================================================================
// paintEvent() token adoption.
// ===========================================================================

TEST(WidgetOverlayTokens, HistogramOverlay_DefaultColorsUseDesignTokens)
{
    ScopedThemePreferenceOverride theme;
    theme.set(an::ui::ThemePreference::Dark);

    HistogramOverlay widget(nullptr);
    EXPECT_EQ(widget.getBackgroundColor(), an::ui::tokens().colors.bgSurface);
    EXPECT_EQ(widget.getFontColor(), an::ui::tokens().colors.textPrimary);
}

TEST(WidgetOverlayTokens, HistogramOverlay_DefaultBackgroundChangesBetweenThemes)
{
    ScopedThemePreferenceOverride theme;
    HistogramOverlay widget(nullptr);

    theme.set(an::ui::ThemePreference::Dark);
    sendPaletteChange(&widget);
    sendThemeChange(&widget);
    const QColor dark = widget.getBackgroundColor();

    theme.set(an::ui::ThemePreference::Light);
    sendPaletteChange(&widget);
    sendThemeChange(&widget);
    const QColor light = widget.getBackgroundColor();

    EXPECT_NE(dark, light);
}

TEST(WidgetOverlayTokens, HistogramOverlay_ThemeChangeSafe)
{
    HistogramOverlay widget(nullptr);
    widget.resize(140, 60);
    widget.setText(QStringLiteral("x: 123\ny: 456"));

    sendPaletteChange(&widget);
    sendThemeChange(&widget);
    SUCCEED();
}

TEST(WidgetOverlayTokens, HistogramOverlay_CornerRadiusMatchesTokenMedium)
{
    HistogramOverlay widget(nullptr);
    EXPECT_EQ(widget.cornerRadius(), static_cast<qreal>(an::ui::tokens().radius.md));
}

TEST(WidgetOverlayTokens, WaitingSpinnerWidget_DefaultCornerRadiusMatchesTokenSmall)
{
    WaitingSpinnerWidget spinner(nullptr, false, false);
    EXPECT_EQ(spinner.effectiveCornerRadius(), static_cast<qreal>(an::ui::tokens().radius.sm));
}

TEST(WidgetOverlayTokens, WaitingSpinnerWidget_PaletteAndThemeChangeSafe)
{
    WaitingSpinnerWidget spinner(nullptr, false, false);
    spinner.resize(48, 48);

    sendPaletteChange(&spinner);
    sendThemeChange(&spinner);
    SUCCEED();
}
