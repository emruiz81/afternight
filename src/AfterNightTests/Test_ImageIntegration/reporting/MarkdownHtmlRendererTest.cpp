// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Markdown renderer's stylesheet: the theme's tokens as its variables, the forced light and dark modes, the
/// report's brand and its print rules. No report is written through the renderer any more; these hold it for as
/// long as it is built.

#include <gtest/gtest.h>

#include <QColor>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include "processes/ImageIntegration/reporting/MarkdownHtmlRenderer.h"
#include "theme/AnDesignTokens.h"

namespace
{

TEST(MarkdownHtmlRenderer, UsesThemeTokensForCssVariablesAndNoHardcodedDarkCardBlock)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString htmlPath = tempDir.filePath(QStringLiteral("report.html"));
    QString error;
    ASSERT_TRUE(ImageIntegration::Reporting::writeStyledHtmlFromMarkdown(
        QStringLiteral(
            "# Test report\n\n<div class=\"an-secondary-gallery\"><div class=\"an-secondary-card\">card</div></div>\n"),
        htmlPath, QStringLiteral("Report"), &error))
        << error.toStdString();

    QFile file(htmlPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QString html = QString::fromUtf8(file.readAll());

    const auto& activeTokens = an::ui::tokens();
    EXPECT_TRUE(html.contains(QStringLiteral("--an-bg: %1;").arg(activeTokens.colors.bgPrimary.name(QColor::HexRgb))));
    EXPECT_TRUE(
        html.contains(QStringLiteral("--an-accent: %1;").arg(activeTokens.colors.accentPrimary.name(QColor::HexRgb))));
    EXPECT_TRUE(html.contains(QStringLiteral("--an-radius-sm:")));
    EXPECT_FALSE(html.contains(QStringLiteral(".an-secondary-card { background: #161b22; border-color: #30363d; }")));
    EXPECT_FALSE(html.contains(QStringLiteral(".an-media-card { background: #161b22; border-color: #30363d; }")));
    EXPECT_FALSE(html.contains(QStringLiteral(".an-histogram")));
}

TEST(MarkdownHtmlRenderer, SupportsForcedLightAndDarkThemeModes)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString markdown = QStringLiteral("# Test report\n");
    const QString lightHtmlPath = tempDir.filePath(QStringLiteral("report_light.html"));
    const QString darkHtmlPath = tempDir.filePath(QStringLiteral("report_dark.html"));
    QString error;

    ASSERT_TRUE(ImageIntegration::Reporting::writeStyledHtmlFromMarkdown(
        markdown, lightHtmlPath, QStringLiteral("Report"), ImageIntegration::Reporting::ReportThemeMode::Light, &error))
        << error.toStdString();

    ASSERT_TRUE(ImageIntegration::Reporting::writeStyledHtmlFromMarkdown(
        markdown, darkHtmlPath, QStringLiteral("Report"), ImageIntegration::Reporting::ReportThemeMode::Dark, &error))
        << error.toStdString();

    QFile lightFile(lightHtmlPath);
    ASSERT_TRUE(lightFile.open(QIODevice::ReadOnly));
    const QString lightHtml = QString::fromUtf8(lightFile.readAll());

    QFile darkFile(darkHtmlPath);
    ASSERT_TRUE(darkFile.open(QIODevice::ReadOnly));
    const QString darkHtml = QString::fromUtf8(darkFile.readAll());

    const auto& lightTokens = an::ui::resolvedLightTokens();
    const auto& darkTokens = an::ui::resolvedDarkTokens();

    EXPECT_TRUE(lightHtml.contains(QStringLiteral("html { color-scheme: light; }")));
    EXPECT_TRUE(
        lightHtml.contains(QStringLiteral("--an-bg: %1;").arg(lightTokens.colors.bgPrimary.name(QColor::HexRgb))));

    EXPECT_TRUE(darkHtml.contains(QStringLiteral("html { color-scheme: dark; }")));
    EXPECT_TRUE(
        darkHtml.contains(QStringLiteral("--an-bg: %1;").arg(darkTokens.colors.bgPrimary.name(QColor::HexRgb))));
}

TEST(MarkdownHtmlRenderer, UsesAfterNightBrandedReportTheme)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString htmlPath = tempDir.filePath(QStringLiteral("report.html"));
    QString error;
    ASSERT_TRUE(ImageIntegration::Reporting::writeStyledHtmlFromMarkdown(
        QStringLiteral("# Test report\n\n## Section\n\n| A | B |\n|---|---|\n| 1 | 2 |\n"), htmlPath,
        QStringLiteral("Report"), &error))
        << error.toStdString();

    QFile file(htmlPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QString html = QString::fromUtf8(file.readAll());

    EXPECT_TRUE(html.contains(QStringLiteral("--an-accent")));
    EXPECT_TRUE(html.contains(QStringLiteral("radial-gradient")));
    EXPECT_TRUE(html.contains(QStringLiteral("--an-font-body:")));
    EXPECT_TRUE(html.contains(QStringLiteral("--an-font-mono:")));
    EXPECT_TRUE(html.contains(QStringLiteral(".an-report-hero")));
    EXPECT_TRUE(html.contains(QStringLiteral("@media print")));
    // The report should not embed the full GitHub markdown stylesheet surface.
    EXPECT_FALSE(html.contains(QStringLiteral("task-list-item-checkbox")));
    EXPECT_FALSE(html.contains(QStringLiteral(".markdown-alert")));
}

TEST(MarkdownHtmlRenderer, PrintStylesIncludeFooterAndPageBreakGuards)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString htmlPath = tempDir.filePath(QStringLiteral("report.html"));
    QString error;
    ASSERT_TRUE(ImageIntegration::Reporting::writeStyledHtmlFromMarkdown(
        QStringLiteral(
            "# Test report\n\n<div class=\"an-print-footer\"><span class=\"an-print-footer-text\">ctx</span></div>\n"),
        htmlPath, QStringLiteral("Report"), &error))
        << error.toStdString();

    QFile file(htmlPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QString html = QString::fromUtf8(file.readAll());

    EXPECT_TRUE(html.contains(QStringLiteral("@page { margin:")));
    EXPECT_TRUE(html.contains(QStringLiteral(".an-print-footer::after { content: \"Page \" counter(page);")));
    EXPECT_TRUE(html.contains(QStringLiteral("details > *:not(summary) { display: block !important; }")));
    EXPECT_TRUE(html.contains(QStringLiteral("font-variant-numeric: tabular-nums;")));
}

} // namespace
