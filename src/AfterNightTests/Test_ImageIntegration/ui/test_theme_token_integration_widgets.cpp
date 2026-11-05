// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLabel>
#include <QRegularExpression>
#include <QStringList>

#include "theme/AnDesignTokens.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/widgets/CalibrationFrameSummaryWidget.h"
#include "processes/ImageIntegration/widgets/CalibrationIssuesWidget.h"
#include "processes/ImageIntegration/widgets/CalibrationPolicyWidget.h"
#include "processes/ImageIntegration/widgets/CalibrationQuickActionsWidget.h"
#include "processes/ImageIntegration/widgets/FrameQuickActionsWidget.h"

namespace
{

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

QString readRepoSourceFile(const QString& relativePath)
{
    QStringList seeds = {QDir::currentPath()};
    if (QCoreApplication::instance())
    {
        seeds.append(QCoreApplication::applicationDirPath());
    }
    seeds.append(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());

    for (const QString& seed : seeds)
    {
        QDir dir(seed);
        while (true)
        {
            const QString candidate = dir.filePath(relativePath);
            if (QFileInfo::exists(candidate))
            {
                QFile file(candidate);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text))
                {
                    return QString::fromUtf8(file.readAll());
                }
            }

            if (!dir.cdUp())
            {
                break;
            }
        }
    }

    return {};
}

} // namespace

TEST(ThemeTokenIntegrationWidgets, CalibrationQuickActions_TitleUsesHeading3Token)
{
    IntegrationDataModel model;
    ImageIntegration::CalibrationQuickActionsWidget widget(&model);

    auto* title = widget.findChild<QLabel*>(QStringLiteral("lblQuickActionsTitle"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->font(), an::ui::tokens().typography.heading3);
}

TEST(ThemeTokenIntegrationWidgets, CalibrationPolicy_TitleUsesHeading3Token)
{
    IntegrationDataModel model;
    ImageIntegration::CalibrationPolicyWidget widget(&model);

    auto* title = widget.findChild<QLabel*>(QStringLiteral("lblPolicyTitle"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->font(), an::ui::tokens().typography.heading3);
}

TEST(ThemeTokenIntegrationWidgets, CalibrationFrameSummary_TitleUsesHeading3Token)
{
    IntegrationDataModel model;
    ImageIntegration::CalibrationFrameSummaryWidget widget(&model);

    auto* title = widget.findChild<QLabel*>(QStringLiteral("lblFrameSummaryTitle"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->font(), an::ui::tokens().typography.heading3);
}

TEST(ThemeTokenIntegrationWidgets, CalibrationIssues_HeaderAndMoreFontsUseTokens)
{
    IntegrationDataModel model;
    ImageIntegration::CalibrationIssuesWidget widget(&model);

    auto* header = widget.findChild<QLabel*>(QStringLiteral("lblIssuesHeader"));
    auto* more = widget.findChild<QLabel*>(QStringLiteral("lblIssuesMore"));
    ASSERT_NE(header, nullptr);
    ASSERT_NE(more, nullptr);

    EXPECT_EQ(header->font(), an::ui::tokens().typography.heading3);

    QFont expectedMore = an::ui::tokens().typography.caption;
    expectedMore.setItalic(true);
    EXPECT_EQ(more->font(), expectedMore);
}

TEST(ThemeTokenIntegrationWidgets, FrameQuickActions_TitleUsesHeading3Token)
{
    IntegrationDataModel model;
    ImageIntegration::FrameQuickActionsWidget widget(FrameType::Light, &model);

    auto* title = widget.findChild<QLabel*>(QStringLiteral("lblFrameQuickActionsTitle"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->font(), an::ui::tokens().typography.heading3);
}

TEST(ThemeTokenIntegrationWidgets, ThemeChangeEventsDoNotCrashForMigratedWidgets)
{
    IntegrationDataModel model;
    ImageIntegration::CalibrationQuickActionsWidget quickActions(&model);
    ImageIntegration::CalibrationPolicyWidget policy(&model);
    ImageIntegration::CalibrationFrameSummaryWidget summary(&model);
    ImageIntegration::CalibrationIssuesWidget issues(&model);
    ImageIntegration::FrameQuickActionsWidget frameQuickActions(FrameType::Light, &model);

    sendPaletteChange(&quickActions);
    sendThemeChange(&quickActions);
    sendPaletteChange(&policy);
    sendThemeChange(&policy);
    sendPaletteChange(&summary);
    sendThemeChange(&summary);
    sendPaletteChange(&issues);
    sendThemeChange(&issues);
    sendPaletteChange(&frameQuickActions);
    sendThemeChange(&frameQuickActions);

    SUCCEED();
}

TEST(ThemeTokenIntegrationWidgets, ImageIntegrationResponsiveSourceGuardsUseTokens)
{
    const QString manualDialog = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/dialogs/ManualRegistrationDialog.cpp"));
    const QString blinkDialog = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/dialogs/BlinkComparatorDialog.cpp"));
    const QString progressDialog = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/dialogs/WorkflowProgressDialogV2.cpp"));
    const QString pipelineWidget = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/tabs/WorkflowProgressPipelineWidget.cpp"));
    ASSERT_FALSE(manualDialog.isEmpty());
    ASSERT_FALSE(blinkDialog.isEmpty());
    ASSERT_FALSE(progressDialog.isEmpty());
    ASSERT_FALSE(pipelineWidget.isEmpty());

    EXPECT_FALSE(manualDialog.contains(QStringLiteral("resize(1200, 760)")));
    EXPECT_FALSE(manualDialog.contains(QRegularExpression(QStringLiteral(R"(\bresize\s*\()"))));
    EXPECT_FALSE(blinkDialog.contains(QRegularExpression(QStringLiteral(R"(\bresize\s*\(\s*\d+\s*,)"))));
    EXPECT_TRUE(blinkDialog.contains(QStringLiteral("expandedTo(minimumSize())")));
    EXPECT_FALSE(progressDialog.contains(QRegularExpression(QStringLiteral(R"(\bresize\s*\()"))));
    EXPECT_TRUE(manualDialog.contains(QStringLiteral("setSizeGripEnabled(true)")));
    EXPECT_TRUE(manualDialog.contains(QStringLiteral("setMinimumSize(spacing.xl * 28, spacing.xl * 18)")));

    EXPECT_FALSE(pipelineWidget.contains(QRegularExpression(QStringLiteral(R"(setContentsMargins\(\s*12,)"))));
    EXPECT_FALSE(pipelineWidget.contains(QRegularExpression(QStringLiteral(R"(setSpacing\(\s*12\s*\))"))));
    EXPECT_FALSE(pipelineWidget.contains(QStringLiteral("setMinimumWidth(24)")));
    EXPECT_TRUE(pipelineWidget.contains(QStringLiteral("tokens().spacing")));
}
