// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Integration's windows that stay windows, under the Observatory profile: the settings of stacking,
/// registration, local normalization and calibration, the weighting options and the advanced registration settings they
/// open, the recommended stacking settings, the blink comparator, the manual registration points and the comet
/// tracking. Each has a heading that says Image Integration holds it, a footer whose rightmost button is the primary
/// one, text that meets the contrast rule, nothing clipped as it opens or at its least size, and opens within its
/// screen's available area - in both densities.

#include "fixtures/WindowLook.h"

#include "app/ApplicationStorage.h"
#include "components/AnWindowHeading.h"
#include "processes/ImageIntegration/controls/LightCalibrationSettingsDialog.h"
#include "processes/ImageIntegration/controls/LocalNormalizationSettingsDialog.h"
#include "processes/ImageIntegration/controls/RegistrationSettingsDialog.h"
#include "processes/ImageIntegration/controls/StackingRecommendationDialog.h"
#include "processes/ImageIntegration/controls/StackingSettingsDialog.h"
#include "processes/ImageIntegration/dialogs/BlinkComparatorDialog.h"
#include "processes/ImageIntegration/dialogs/CometTrackingDialog.h"
#include "processes/ImageIntegration/dialogs/ManualRegistrationDialog.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

#include <QComboBox>
#include <QDialog>
#include <QPointer>
#include <QPushButton>
#include <QStringList>

#include <gtest/gtest.h>

namespace
{

using an::tests::look::expectWindowLook;
using an::tests::look::settleWindow;
using an::tests::look::WindowLookMeasured;

const QStringList kFrames = {QStringLiteral("C:/light_a.fit"), QStringLiteral("C:/light_b.fit")};

/// What a window's heading says: the line over its title, and its title.
struct Said
{
    QString eyebrow;
    QString title;
};

[[nodiscard]] Said saidBy(const QWidget& window)
{
    const auto* const heading = window.findChild<an::ui::AnWindowHeading*>(QStringLiteral("anWindowHeading"));
    return heading != nullptr ? Said{heading->eyebrow(), heading->title()} : Said{};
}

/// Expects a window made under the Observatory profile to say that Image Integration holds it, under a title.
void expectHeldByImageIntegration(const QWidget& window, const QString& title)
{
    if (!an::ui::windowsTakeHeadings())
    {
        return;
    }
    const Said said = saidBy(window);
    EXPECT_EQ(said.eyebrow, QStringLiteral("Image Integration"));
    EXPECT_EQ(said.title, title);
}

/// A dialog another dialog opens: made with the one that opens it, and gone with it.
class OpenedBy
{
public:
    /// Lets the dialog go: the one it opened is dismissed, and the one that opened it deleted.
    void letGo(QWidget* opened)
    {
        if (auto* const dialog = qobject_cast<QDialog*>(opened))
        {
            dialog->reject();
        }
        settleWindow(20);
        delete m_opener.data();
        settleWindow(20);
    }

    QPointer<QDialog> m_opener;
};

} // namespace

TEST(WindowLook, StackingSettings)
{
    const WindowLookMeasured measured = expectWindowLook([]() -> QWidget* {
        auto* const dialog =
            new StackingSettingsDialog(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light - Ha - 300 s"));
        dialog->resetToDefaults();
        expectHeldByImageIntegration(*dialog, QStringLiteral("LIGHT Integration Stacking Settings"));
        if (an::ui::windowsTakeHeadings())
        {
            const auto* const heading = dialog->findChild<an::ui::AnWindowHeading*>(QStringLiteral("anWindowHeading"));
            EXPECT_TRUE(heading != nullptr && heading->description() == QStringLiteral("Light - Ha - 300 s"))
                << "the heading does not say whose settings these are";
        }
        return dialog;
    });
    AN_WINDOW_LOOK_REPORT(measured);
}

TEST(WindowLook, WeightingOptions)
{
    OpenedBy stacking;
    const WindowLookMeasured measured = expectWindowLook(
        [&stacking]() -> QWidget* {
            auto* const dialog =
                new StackingSettingsDialog(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
            stacking.m_opener = dialog;
            dialog->resetToDefaults();
            an::tests::look::showOffTheScreen(*dialog);
            auto* const weighting = dialog->findChild<QComboBox*>(QStringLiteral("weightingSelector"));
            auto* const open = dialog->findChild<QPushButton*>(QStringLiteral("weightingOptionsButton"));
            EXPECT_TRUE(weighting != nullptr && open != nullptr);
            if (weighting == nullptr || open == nullptr)
            {
                return nullptr;
            }
            weighting->setCurrentText(QStringLiteral("Number of stars"));
            settleWindow(20);
            open->click();
            settleWindow();
            auto* const options = dialog->findChild<QDialog*>(QStringLiteral("stackingWeightingOptionsDialog"));
            if (options != nullptr)
            {
                expectHeldByImageIntegration(*options, QStringLiteral("Weighting Options"));
            }
            return options;
        },
        [&stacking](QWidget* opened) { stacking.letGo(opened); });
    AN_WINDOW_LOOK_REPORT(measured);
}

TEST(WindowLook, RegistrationSettings)
{
    const WindowLookMeasured measured = expectWindowLook([]() -> QWidget* {
        auto* const dialog = new RegistrationSettingsDialog(QStringLiteral("Light - Ha - 300 s"), kFrames);
        expectHeldByImageIntegration(*dialog, QStringLiteral("Registration Settings"));
        return dialog;
    });
    AN_WINDOW_LOOK_REPORT(measured);
}

TEST(WindowLook, AdvancedRegistrationSettings)
{
    OpenedBy registration;
    const WindowLookMeasured measured = expectWindowLook(
        [&registration]() -> QWidget* {
            auto* const dialog = new RegistrationSettingsDialog(QStringLiteral("Group 1"), kFrames);
            registration.m_opener = dialog;
            an::tests::look::showOffTheScreen(*dialog);
            auto* const open = dialog->findChild<QPushButton*>(QStringLiteral("advancedSettingsButton"));
            EXPECT_NE(open, nullptr);
            if (open == nullptr)
            {
                return nullptr;
            }
            open->click();
            settleWindow();
            auto* const advanced = dialog->findChild<QDialog*>(QStringLiteral("registrationAdvancedSettingsDialog"));
            if (advanced != nullptr)
            {
                expectHeldByImageIntegration(*advanced, QStringLiteral("Advanced Registration Settings"));
            }
            return advanced;
        },
        [&registration](QWidget* opened) { registration.letGo(opened); });
    AN_WINDOW_LOOK_REPORT(measured);
}

TEST(WindowLook, LocalNormalizationSettings)
{
    const WindowLookMeasured measured = expectWindowLook([]() -> QWidget* {
        auto* const dialog = new LocalNormalizationSettingsDialog();
        expectHeldByImageIntegration(*dialog, QStringLiteral("Local Normalization Settings"));
        return dialog;
    });
    AN_WINDOW_LOOK_REPORT(measured);
}

TEST(WindowLook, CalibrationSettings)
{
    const WindowLookMeasured measured = expectWindowLook([]() -> QWidget* {
        auto* const dialog = new LightCalibrationSettingsDialog(LightCalibrationSettings{}, QStringLiteral("Group A"));
        expectHeldByImageIntegration(*dialog, QStringLiteral("Calibration Settings"));
        return dialog;
    });
    AN_WINDOW_LOOK_REPORT(measured);
}

// The recommendation: under the Observatory profile one footer, whose rightmost button applies what is recommended.
TEST(WindowLook, RecommendedStackingSettings)
{
    StackingRecommendation recommended;
    recommended.stackMethod = "Average";
    recommended.normalizationMode = "Additive With Scaling";
    recommended.rejectionMethod = "Winsorized Sigma Clipping";
    recommended.sigmaLow = 3.2;
    recommended.sigmaHigh = 3.2;
    recommended.weightingMethod = "Weighted FWHM";
    recommended.localNormalization = true;
    recommended.reason = "Twenty frames reject outliers well, and their weights follow their sharpness.";
    recommended.drizzleVerdict = StackingRecommendation::DrizzleVerdict::Recommended;
    recommended.drizzleScale = 2.0;
    recommended.drizzleReason = "The frames are dithered and undersampled.";
    DrizzleAnalysis drizzle;
    drizzle.available = true;
    drizzle.subPixelCoverage = 0.8;
    drizzle.framesWithTransforms = 20;
    drizzle.totalFrames = 20;
    StackingSettings current;
    current.stackMethod = "Average";
    current.normalizationMode = NormalizationMode::None;
    current.rejectionMethod = "None";

    const WindowLookMeasured measured = expectWindowLook([&]() -> QWidget* {
        auto* const dialog = new StackingRecommendationDialog(
            StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light - Ha"), 20, recommended, drizzle, current);
        if (an::ui::windowsTakeHeadings())
        {
            EXPECT_EQ(saidBy(*dialog).eyebrow, QStringLiteral("Image Integration"));
        }
        return dialog;
    });
    AN_WINDOW_LOOK_REPORT(measured);
}

// The comparator has no footer of buttons: the checks of one do not apply, and its heading says the title it is given
// after it is made, as a comparison of masters is.
TEST(WindowLook, BlinkComparator)
{
    const QStringList files = {QStringLiteral("C:/file1.fit"), QStringLiteral("C:/file2.fit"),
                               QStringLiteral("C:/file3.fit")};
    const WindowLookMeasured measured = expectWindowLook([&files]() -> QWidget* {
        // The comparator opens at the size it was last left at: each one made here opens at its own.
        {
            auto kept = an::ui::applicationSettings();
            kept.remove(QStringLiteral("ImageIntegration/BlinkComparatorDialog"));
            kept.sync();
        }
        auto* const dialog = new BlinkComparatorDialog(files, nullptr, false);
        expectHeldByImageIntegration(*dialog, QStringLiteral("Blink Comparator"));
        dialog->setWindowTitle(QStringLiteral("Compare Stacked Masters"));
        expectHeldByImageIntegration(*dialog, QStringLiteral("Compare Stacked Masters"));
        return dialog;
    });
    AN_WINDOW_LOOK_REPORT(measured);
}

TEST(WindowLook, ManualRegistration)
{
    const WindowLookMeasured measured = expectWindowLook([]() -> QWidget* {
        auto* const dialog =
            new ManualRegistrationDialog(QString(), QStringList{}, RegistrationSettings::ManualControlPointMap{});
        expectHeldByImageIntegration(*dialog, QStringLiteral("Manual Registration"));
        return dialog;
    });
    AN_WINDOW_LOOK_REPORT(measured);
}

TEST(WindowLook, CometTracking)
{
    const WindowLookMeasured measured = expectWindowLook([]() -> QWidget* {
        auto* const dialog = new an::ui::CometTrackingDialog(kFrames);
        expectHeldByImageIntegration(*dialog, QStringLiteral("Track Comet"));
        return dialog;
    });
    AN_WINDOW_LOOK_REPORT(measured);
}
