// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QIcon>
#include <QLabel>

#include "controls/AboutDialog.h"
#include "controls/DashboardUtils.h"
#include "controls/settings/PathPickerWidget.h"
#include "controls/settings/PlaceholderSettingsPage.h"
#include "theme/AnDesignTokens.h"

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

void sendPaletteChange(QWidget* widget)
{
    QEvent event(QEvent::ApplicationPaletteChange);
    QApplication::sendEvent(widget, &event);
}

void sendThemeChange(QWidget* widget)
{
    QEvent event(QEvent::ThemeChange);
    QApplication::sendEvent(widget, &event);
}

QLabel* findLabelContainingText(QWidget* widget, const QString& snippet)
{
    const auto labels = widget->findChildren<QLabel*>();
    for (auto* label : labels)
    {
        if (label && label->text().contains(snippet, Qt::CaseInsensitive))
        {
            return label;
        }
    }
    return nullptr;
}

} // namespace

class ThemeTokenSettingsWidgetsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ensureApp();
    }
};

TEST_F(ThemeTokenSettingsWidgetsTest, PlaceholderSettingsPage_MessageUsesTokenTypography)
{
    for (const bool enabled : {false, true})
    {
        PlaceholderSettingsPage page(QStringLiteral("Placeholder"), QIcon(), enabled);

        const QString message = enabled ? QStringLiteral("under development") : QStringLiteral("planned for a future");
        auto* messageLabel = findLabelContainingText(&page, message);
        ASSERT_NE(messageLabel, nullptr) << enabled;
        EXPECT_EQ(messageLabel->font(), an::ui::tokens().typography.heading3) << enabled;
    }
}

TEST_F(ThemeTokenSettingsWidgetsTest, PathPicker_StatusLabelUsesCaptionToken)
{
    PathPickerWidget widget(PathPickerWidget::Mode::File);
    widget.setStatus(QStringLiteral("Detected"), false);

    auto* statusLabel = findLabelContainingText(&widget, QStringLiteral("Detected"));
    ASSERT_NE(statusLabel, nullptr);
    EXPECT_EQ(statusLabel->font(), an::ui::tokens().typography.caption);
}

TEST_F(ThemeTokenSettingsWidgetsTest, AboutDialog_HeaderFontsUseTokens)
{
    AboutDialog dialog(nullptr, false);

    auto* title = dialog.findChild<QLabel*>(QStringLiteral("aboutTitleLabel"));
    auto* version = dialog.findChild<QLabel*>(QStringLiteral("aboutVersionLabel"));
    auto* subtitle = dialog.findChild<QLabel*>(QStringLiteral("aboutSubtitleLabel"));
    ASSERT_NE(title, nullptr);
    ASSERT_NE(version, nullptr);
    ASSERT_NE(subtitle, nullptr);

    QFont expectedTitle = an::ui::tokens().typography.heading1;
    expectedTitle.setWeight(QFont::Bold);
    EXPECT_EQ(title->font(), expectedTitle);
    EXPECT_EQ(version->font(), an::ui::tokens().typography.body);
    EXPECT_EQ(subtitle->font(), an::ui::tokens().typography.caption);
}

TEST_F(ThemeTokenSettingsWidgetsTest, DashboardUtils_ScaledSystemFontUsesTokenBuckets)
{
    QWidget host;

    QFont small = DashboardUtils::scaledSystemFont(&host, 0.8);
    QFont body = DashboardUtils::scaledSystemFont(&host, 1.0);
    QFont medium = DashboardUtils::scaledSystemFont(&host, 1.2);
    QFont large = DashboardUtils::scaledSystemFont(&host, 1.5);

    QFont expectedSmall = an::ui::tokens().typography.caption;
    QFont expectedBody = an::ui::tokens().typography.body;
    QFont expectedMedium = an::ui::tokens().typography.heading3;
    QFont expectedLarge = an::ui::tokens().typography.heading2;
    expectedSmall.setStyleHint(host.font().styleHint());
    expectedBody.setStyleHint(host.font().styleHint());
    expectedMedium.setStyleHint(host.font().styleHint());
    expectedLarge.setStyleHint(host.font().styleHint());
    expectedSmall.setWeight(QFont::Normal);
    expectedBody.setWeight(QFont::Normal);
    expectedMedium.setWeight(QFont::Normal);
    expectedLarge.setWeight(QFont::Normal);
    expectedSmall.setItalic(false);
    expectedBody.setItalic(false);
    expectedMedium.setItalic(false);
    expectedLarge.setItalic(false);

    EXPECT_EQ(small, expectedSmall);
    EXPECT_EQ(body, expectedBody);
    EXPECT_EQ(medium, expectedMedium);
    EXPECT_EQ(large, expectedLarge);
}

TEST_F(ThemeTokenSettingsWidgetsTest, ThemeChangeEventsDoNotCrashForSettingsWidgets)
{
    PathPickerWidget picker(PathPickerWidget::Mode::Directory);

    sendPaletteChange(&picker);
    sendThemeChange(&picker);
    QCoreApplication::processEvents();

    SUCCEED();
}
