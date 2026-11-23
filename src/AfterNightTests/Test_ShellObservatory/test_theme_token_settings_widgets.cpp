// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QIcon>
#include <QLabel>

#include "controls/AboutDialog.h"
#include "controls/settings/PathPickerWidget.h"
#include "controls/settings/PlaceholderSettingsPage.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"

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

// The picker sets its status in the caption, and the application's style draws a status that has a tone semibold: a
// path detected is said in the caption, semibold.
TEST_F(ThemeTokenSettingsWidgetsTest, PathPicker_StatusLabelIsTheCaptionInSemibold)
{
    ASSERT_NE(an::ui::installedAnProxyStyle(), nullptr) << "the suite runs under the application's style";
    PathPickerWidget widget(PathPickerWidget::Mode::File);
    widget.setStatus(QStringLiteral("Detected"), false);

    auto* statusLabel = findLabelContainingText(&widget, QStringLiteral("Detected"));
    ASSERT_NE(statusLabel, nullptr);
    QFont expected = an::ui::tokens().typography.caption;
    expected.setWeight(QFont::DemiBold);
    EXPECT_EQ(statusLabel->font(), expected);
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

TEST_F(ThemeTokenSettingsWidgetsTest, ThemeChangeEventsDoNotCrashForSettingsWidgets)
{
    PathPickerWidget picker(PathPickerWidget::Mode::Directory);

    sendPaletteChange(&picker);
    sendThemeChange(&picker);
    QCoreApplication::processEvents();

    SUCCEED();
}
