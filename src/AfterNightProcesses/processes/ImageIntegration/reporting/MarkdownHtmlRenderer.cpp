// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "MarkdownHtmlRenderer.h"

#include "theme/AnDesignTokens.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cstdlib>

#if defined(AN_MARKDOWN_BACKEND_CMARK_GFM)
#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#define AN_CMARK_GFM_EXTENSIONS 1
#elif defined(AN_MARKDOWN_BACKEND_CMARK)
#include <cmark.h>
#define AN_CMARK_GFM_EXTENSIONS 0
#elif __has_include(<cmark-gfm.h>)
#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#define AN_CMARK_GFM_EXTENSIONS 1
#elif __has_include(<cmark.h>)
#include <cmark.h>
#define AN_CMARK_GFM_EXTENSIONS 0
#else
#error "No Markdown backend available. Install cmark-gfm or cmark and configure build defines."
#endif

namespace
{
QColor withAlpha(QColor color, const int alpha)
{
    color.setAlpha(std::clamp(alpha, 0, 255));
    return color;
}

QString cssColor(const QColor& color)
{
    if (!color.isValid())
    {
        return QStringLiteral("transparent");
    }

    if (color.alpha() >= 255)
    {
        return color.name(QColor::HexRgb);
    }

    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(QString::number(color.alphaF(), 'f', 3));
}

QString cssFontStack(const QFont& font, const QString& fallback)
{
    const QString family = font.family().trimmed();
    if (family.isEmpty())
    {
        return fallback;
    }

    QString escapedFamily = family;
    escapedFamily.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escapedFamily.replace(QLatin1Char('"'), QStringLiteral("\\\""));

    return QStringLiteral("\"%1\", %2").arg(escapedFamily, fallback);
}

struct ReportThemeSnapshot
{
    const an::ui::DesignTokens* tokenSet = nullptr;
    bool darkTheme = false;
};

ReportThemeSnapshot resolveThemeSnapshot(const ImageIntegration::Reporting::ReportThemeMode themeMode)
{
    using ImageIntegration::Reporting::ReportThemeMode;

    switch (themeMode)
    {
    case ReportThemeMode::Light:
        return {&an::ui::resolvedLightTokens(), false};
    case ReportThemeMode::Dark:
        return {&an::ui::resolvedDarkTokens(), true};
    case ReportThemeMode::Active:
    default:
        return {&an::ui::tokens(), an::ui::isDarkTheme()};
    }
}

QString markdownBodyCss(const ImageIntegration::Reporting::ReportThemeMode themeMode)
{
    const ReportThemeSnapshot snapshot = resolveThemeSnapshot(themeMode);
    const auto& tokenSet = *snapshot.tokenSet;
    const auto& colors = tokenSet.colors;
    const auto& radius = tokenSet.radius;
    const auto& spacing = tokenSet.spacing;
    const auto& typography = tokenSet.typography;

    const bool darkTheme = snapshot.darkTheme;

    QColor accentSoft = colors.cardSurfaceTint;
    if (!accentSoft.isValid())
    {
        accentSoft = colors.statusInfoSoft;
    }
    if (!accentSoft.isValid())
    {
        accentSoft = withAlpha(colors.accentPrimary, darkTheme ? 76 : 56);
    }

    QString css;
    QTextStream out(&css);

    out << "html { color-scheme: " << (darkTheme ? "dark" : "light") << "; }\n";
    out << ":root {\n";
    out << "  --an-bg: " << cssColor(colors.bgPrimary) << ";\n";
    out << "  --an-surface: " << cssColor(colors.bgSurface) << ";\n";
    out << "  --an-surface-soft: " << cssColor(colors.bgCardSecondary) << ";\n";
    out << "  --an-border: " << cssColor(colors.borderPrimary) << ";\n";
    out << "  --an-border-strong: " << cssColor(colors.borderFocus) << ";\n";
    out << "  --an-text: " << cssColor(colors.textPrimary) << ";\n";
    out << "  --an-text-soft: " << cssColor(colors.textSecondary) << ";\n";
    out << "  --an-accent: " << cssColor(colors.accentPrimary) << ";\n";
    out << "  --an-accent-hover: " << cssColor(colors.accentHover) << ";\n";
    out << "  --an-accent-soft: " << cssColor(accentSoft) << ";\n";
    out << "  --an-success: " << cssColor(colors.statusSuccess) << ";\n";
    out << "  --an-warning: " << cssColor(colors.statusWarning) << ";\n";
    out << "  --an-danger: " << cssColor(colors.statusError) << ";\n";
    out << "  --an-bg-gradient-a: " << cssColor(withAlpha(colors.accentPrimary, darkTheme ? 68 : 46)) << ";\n";
    out << "  --an-bg-gradient-b: " << cssColor(withAlpha(colors.statusInfo, darkTheme ? 58 : 36)) << ";\n";
    out << "  --an-bg-gradient-c: " << cssColor(withAlpha(colors.statusSuccess, darkTheme ? 52 : 34)) << ";\n";
    out << "  --an-shadow: 0 " << (darkTheme ? 16 : 14) << "px 36px "
        << cssColor(withAlpha(colors.shadowOverlay, darkTheme ? 110 : 42)) << ";\n";
    out << "  --an-font-body: "
        << cssFontStack(typography.body,
                        QStringLiteral("\"Source Sans 3\", \"IBM Plex Sans\", \"Noto Sans\", \"Segoe UI\", sans-serif"))
        << ";\n";
    out << "  --an-font-mono: "
        << cssFontStack(
               typography.monospace,
               QStringLiteral("\"JetBrains Mono\", \"Cascadia Code\", \"Fira Code\", Consolas, Monaco, monospace"))
        << ";\n";
    out << "  --an-space-sm: " << spacing.sm << "px;\n";
    out << "  --an-space-md: " << spacing.md << "px;\n";
    out << "  --an-space-lg: " << spacing.lg << "px;\n";
    out << "  --an-radius-sm: " << radius.sm << "px;\n";
    out << "  --an-radius-md: " << radius.md << "px;\n";
    out << "  --an-radius-lg: " << radius.lg << "px;\n";
    out << "  --an-radius-xl: " << radius.xl << "px;\n";
    out << "}\n";

    out << R"AN_CSS(
body {
  margin: 0;
  padding: 24px;
  background:
    radial-gradient(1400px 520px at 10% -10%, var(--an-bg-gradient-a), transparent 65%),
    radial-gradient(1200px 460px at 92% 0%, var(--an-bg-gradient-b), transparent 60%),
    radial-gradient(1000px 400px at 92% 0%, var(--an-bg-gradient-c), transparent 72%),
    var(--an-bg);
  color: var(--an-text);
  font-family: var(--an-font-body);
}
.container { max-width: 1280px; margin: 0 auto; }
.markdown-body {
  box-sizing: border-box;
  min-width: 200px;
  margin: 0 auto;
  padding: 30px 34px;
  border: 1px solid var(--an-border);
  border-radius: var(--an-radius-xl);
  background: var(--an-surface);
  box-shadow: var(--an-shadow);
  line-height: 1.6;
  color: var(--an-text);
  position: relative;
}
.markdown-body::before {
  content: "";
  position: absolute;
  left: 0;
  top: 0;
  right: 0;
  height: 6px;
  border-radius: var(--an-radius-xl) var(--an-radius-xl) 0 0;
  background: linear-gradient(90deg, var(--an-accent), var(--an-accent-hover) 52%, var(--an-success));
}
@media (max-width: 900px) {
  body { padding: 12px; }
  .markdown-body { padding: 20px 16px; border-radius: var(--an-radius-lg); }
  .markdown-body::before { border-radius: var(--an-radius-lg) var(--an-radius-lg) 0 0; }
}
.markdown-body h1,
.markdown-body h2,
.markdown-body h3,
.markdown-body h4,
.markdown-body h5,
.markdown-body h6 {
  margin-top: 1.1em;
  margin-bottom: 0.5em;
  line-height: 1.24;
  font-weight: 700;
  letter-spacing: -0.015em;
}
.markdown-body h1 {
  margin-top: 0.45em;
  margin-bottom: 0.75rem;
  font-size: 2.15rem;
  padding-bottom: 0.45rem;
  border-bottom: 1px solid var(--an-border);
}
.markdown-body h2 {
  margin-top: 2rem;
  margin-bottom: 0.65rem;
  font-size: 1.45rem;
  padding: 0.2rem 0;
  border-bottom: 1px solid var(--an-border);
}
.markdown-body h3 { margin-top: 1.25rem; font-size: 1.13rem; color: var(--an-text); }

/* report masthead */
.an-report-hero {
  margin: 0.2rem 0 1.2rem 0;
  padding: 20px 20px;
  border: 1px solid var(--an-border);
  border-radius: var(--an-radius-lg);
  background: linear-gradient(160deg, color-mix(in srgb, var(--an-accent-soft) 65%, transparent), var(--an-surface-soft));
}
.an-report-hero-top { display: flex; align-items: flex-start; justify-content: space-between; gap: 16px; }
.an-report-brand { display: flex; align-items: center; gap: 10px; min-width: 0; }
.an-brand-logo { width: 64px; height: 64px; display: inline-flex; flex: 0 0 auto; }
.an-brand-logo svg, .an-brand-logo img { width: 64px; height: 64px; display: block; object-fit: contain; }
.an-brand-title { font-size: 1.8rem; font-weight: 800; color: var(--an-text); line-height: 1.15; letter-spacing: -0.02em; }
.an-brand-subtitle { margin-top: 3px; color: var(--an-text-soft); font-size: 0.96rem; }
.an-status-pill {
  display: inline-flex; align-items: center; justify-content: center;
  padding: 6px 12px; border-radius: 999px; font-size: 13px; font-weight: 800;
  border: 1px solid var(--an-border);
}
.an-status-pill.is-success { color: var(--an-success); background: color-mix(in srgb, var(--an-success) 16%, transparent); border-color: color-mix(in srgb, var(--an-success) 36%, var(--an-border)); }
.an-status-pill.is-failed { color: var(--an-danger); background: color-mix(in srgb, var(--an-danger) 16%, transparent); border-color: color-mix(in srgb, var(--an-danger) 36%, var(--an-border)); }
.an-status-pill.is-cancelled { color: var(--an-warning); background: color-mix(in srgb, var(--an-warning) 16%, transparent); border-color: color-mix(in srgb, var(--an-warning) 36%, var(--an-border)); }
.an-report-meta { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 9px; }
.an-meta-chip {
  display: inline-flex; align-items: center; padding: 4px 10px; border-radius: 999px;
  border: 1px solid var(--an-border); background: var(--an-surface); color: var(--an-text-soft); font-size: 12px;
}
.an-target-strip { margin-top: 10px; }
.an-target-count { display: inline-flex; font-weight: 700; color: var(--an-text); margin-right: 8px; margin-bottom: 6px; }
.an-target-chips { display: flex; flex-wrap: wrap; gap: 6px; }
.an-target-chip {
  display: inline-flex; align-items: center; padding: 3px 8px; border-radius: 999px;
  border: 1px solid var(--an-border); background: color-mix(in srgb, var(--an-accent-soft) 55%, transparent);
  color: var(--an-text); font-size: 12px; font-weight: 600;
}
.an-session-notes {
  margin: 0.4rem 0 1rem 0;
  border: 1px solid var(--an-border);
  border-left: 4px solid var(--an-accent);
  border-radius: var(--an-radius-md);
  background: color-mix(in srgb, var(--an-accent-soft) 42%, transparent);
  padding: 10px 12px;
}
.an-quality-badge {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 52px;
  padding: 3px 10px;
  border-radius: 999px;
  border: 1px solid var(--an-border);
  font-size: 12px;
  font-weight: 800;
  letter-spacing: 0.03em;
}
.an-quality-pass { color: var(--an-success); background: color-mix(in srgb, var(--an-success) 18%, transparent); border-color: color-mix(in srgb, var(--an-success) 40%, var(--an-border)); }
.an-quality-warn { color: var(--an-warning); background: color-mix(in srgb, var(--an-warning) 20%, transparent); border-color: color-mix(in srgb, var(--an-warning) 42%, var(--an-border)); }
.an-quality-fail { color: var(--an-danger); background: color-mix(in srgb, var(--an-danger) 18%, transparent); border-color: color-mix(in srgb, var(--an-danger) 40%, var(--an-border)); }
.an-target-summary-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
  gap: 12px;
  margin: 0.45rem 0 1.15rem 0;
}
.an-target-summary-card {
  border: 1px solid var(--an-border);
  border-radius: var(--an-radius-lg);
  background: color-mix(in srgb, var(--an-surface-soft) 80%, transparent);
  padding: 11px 13px;
}
.an-target-summary-card h3 {
  margin: 0 0 7px 0;
  font-size: 1.04rem;
  border: 0;
  padding: 0;
}
.an-target-summary-card ul {
  margin: 0;
  padding-left: 1.1rem;
}
.an-target-summary-card li {
  margin: 0.24rem 0;
}
.an-weighting-kpi-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 10px;
  margin: 0.5rem 0 1rem 0;
}
.an-weighting-kpi-card {
  border: 1px solid var(--an-border);
  border-radius: var(--an-radius-md);
  background: color-mix(in srgb, var(--an-surface-soft) 82%, transparent);
  padding: 10px 11px;
}
.an-weighting-kpi-label {
  color: var(--an-text-soft);
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  font-weight: 700;
}
.an-weighting-kpi-value {
  margin-top: 3px;
  font-size: 1.24rem;
  font-weight: 800;
  color: var(--an-text);
}
.an-weighting-kpi-hint {
  margin-top: 2px;
  color: var(--an-text-soft);
  font-size: 12px;
}
.an-quality-hist-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
  gap: 12px;
  margin: 0.45rem 0 1rem 0;
}
.an-quality-hist-card {
  border: 1px solid var(--an-border);
  border-radius: var(--an-radius-lg);
  background: var(--an-surface-soft);
  padding: 10px 11px 8px 11px;
}
.an-quality-hist-title {
  font-size: 0.97rem;
  font-weight: 700;
  color: var(--an-text);
}
.an-quality-hist-subtitle {
  margin-top: 2px;
  font-size: 12px;
  color: var(--an-text-soft);
}
.an-quality-hist-bars {
  margin-top: 10px;
  height: 132px;
  display: flex;
  gap: 5px;
  align-items: end;
}
.an-quality-hist-bar-wrap {
  height: 100%;
  flex: 1 1 0;
  display: flex;
  align-items: end;
}
.an-quality-hist-bar {
  width: 100%;
  border-radius: var(--an-radius-sm) var(--an-radius-sm) 2px 2px;
  background: linear-gradient(180deg, color-mix(in srgb, var(--an-accent) 88%, white 12%), color-mix(in srgb, var(--an-accent) 72%, black 10%));
}
.an-quality-hist-bar.is-marker {
  outline: 2px solid color-mix(in srgb, var(--an-warning) 70%, transparent);
  outline-offset: -1px;
}
.an-quality-hist-axis {
  margin-top: 6px;
  display: flex;
  justify-content: space-between;
  color: var(--an-text-soft);
  font-size: 11px;
}
.an-diagnostic-card {
  margin: 0.4rem 0 1rem 0;
  border: 1px solid color-mix(in srgb, var(--an-danger) 40%, var(--an-border));
  border-left: 5px solid var(--an-danger);
  border-radius: var(--an-radius-md);
  background: color-mix(in srgb, var(--an-danger) 9%, transparent);
  padding: 10px 12px;
}
.an-diagnostic-card p { margin: 0.3rem 0; }
.an-severity-pill {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  padding: 2px 8px;
  border-radius: 999px;
  border: 1px solid var(--an-border);
  font-size: 11px;
  font-weight: 800;
  text-transform: uppercase;
  letter-spacing: 0.03em;
}
.an-severity-success { color: var(--an-success); background: color-mix(in srgb, var(--an-success) 16%, transparent); border-color: color-mix(in srgb, var(--an-success) 36%, var(--an-border)); }
.an-severity-warning { color: var(--an-warning); background: color-mix(in srgb, var(--an-warning) 18%, transparent); border-color: color-mix(in srgb, var(--an-warning) 38%, var(--an-border)); }
.an-severity-error { color: var(--an-danger); background: color-mix(in srgb, var(--an-danger) 16%, transparent); border-color: color-mix(in srgb, var(--an-danger) 36%, var(--an-border)); }
.an-severity-info { color: var(--an-text-soft); background: color-mix(in srgb, var(--an-text-soft) 10%, transparent); border-color: var(--an-border); }
.an-issues-list {
  margin: 0.45rem 0 1rem 0;
  padding: 0;
  list-style: none;
}
.an-issue-item {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  margin: 0.34rem 0;
  padding: 8px 10px;
  border-radius: var(--an-radius-md);
  border: 1px solid var(--an-border);
  background: var(--an-surface-soft);
}
.an-issue-item.an-issue-error { border-color: color-mix(in srgb, var(--an-danger) 42%, var(--an-border)); background: color-mix(in srgb, var(--an-danger) 8%, transparent); }
.an-issue-item.an-issue-warning { border-color: color-mix(in srgb, var(--an-warning) 42%, var(--an-border)); background: color-mix(in srgb, var(--an-warning) 8%, transparent); }
.an-num { font-variant-numeric: tabular-nums; white-space: nowrap; }
.an-print-footer { display: none; }
@media (max-width: 900px) {
  .an-report-hero-top { align-items: flex-start; flex-direction: column; }
  .an-brand-logo { width: 52px; height: 52px; }
  .an-brand-logo svg, .an-brand-logo img { width: 52px; height: 52px; }
  .an-brand-title { font-size: 1.45rem; }
}

/* print profile */
@media print {
  @page { margin: 14mm 12mm 18mm 12mm; }
  body { padding: 0; background: #fff !important; color: #212121 !important; }
  .container { max-width: none; margin: 0; }
  .markdown-body { border: 0; box-shadow: none; padding: 0 0 16mm 0; font-size: 10.5pt; line-height: 1.45; }
  .markdown-body::before { display: none; }
  .an-report-hero, .an-media-card, .an-secondary-card, .an-target-summary-card, .an-diagnostic-card { break-inside: avoid; page-break-inside: avoid; }
  .markdown-body h2, .markdown-body h3 { break-after: avoid; page-break-after: avoid; }
  .markdown-body details { border-color: #d7d7d7; }
  .markdown-body details > *:not(summary) { display: block !important; }
  .markdown-body table, .markdown-body tr, .markdown-body td, .markdown-body th { break-inside: avoid; page-break-inside: avoid; }
  .markdown-body thead { display: table-header-group; }
  .an-print-footer {
    display: block;
    position: fixed;
    left: 0;
    right: 0;
    bottom: 0;
    padding: 4mm 10mm 0 10mm;
    border-top: 1px solid #cfd9e4;
    font-size: 9pt;
    color: #475a70;
    background: #fff;
  }
  .an-print-footer::after { content: "Page " counter(page); float: right; }
}
.markdown-body p { margin: 0.62em 0 0.78em 0; color: var(--an-text); }
.markdown-body em { color: var(--an-text-soft); }
.markdown-body a { color: var(--an-accent); text-decoration: none; border-bottom: 1px dotted currentColor; }
.markdown-body a:hover { border-bottom-style: solid; }
.markdown-body ul,
.markdown-body ol { margin: 0.65rem 0 0.9rem 1.25rem; }
.markdown-body li { margin: 0.26rem 0; }
.markdown-body hr { border: 0; border-top: 1px solid var(--an-border); margin: 1.4rem 0; }
.markdown-body table {
  width: 100%;
  margin: 0.72rem 0 1.05rem;
  border-collapse: separate;
  border-spacing: 0;
  overflow: hidden;
  border: 1px solid var(--an-border);
  border-radius: var(--an-radius-lg);
}
.markdown-body thead th {
  background: linear-gradient(180deg, var(--an-surface-soft), var(--an-accent-soft));
  color: var(--an-text);
  font-size: 12px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.03em;
}
.markdown-body th,
.markdown-body td {
  border-right: 1px solid var(--an-border);
  border-bottom: 1px solid var(--an-border);
  padding: 8px 10px;
  vertical-align: top;
  font-variant-numeric: tabular-nums;
}
.markdown-body tr:last-child td { border-bottom: 0; }
.markdown-body th:last-child,
.markdown-body td:last-child { border-right: 0; }
.markdown-body tbody tr:nth-child(even) td { background: color-mix(in srgb, var(--an-surface-soft) 42%, transparent); }
.markdown-body code {
  font-family: var(--an-font-mono);
  font-size: 0.86em;
  background: color-mix(in srgb, var(--an-accent-soft) 55%, transparent);
  border: 1px solid var(--an-border);
  border-radius: var(--an-radius-sm);
  padding: 0.08rem 0.35rem;
}
.markdown-body pre {
  overflow-x: auto;
  border-radius: var(--an-radius-md);
  border: 1px solid var(--an-border);
  background: var(--an-surface-soft);
  padding: 0.75rem 0.9rem;
}
.markdown-body pre code {
  border: 0;
  background: transparent;
  padding: 0;
}
.markdown-body details {
  margin: 0.75rem 0 1.05rem;
  border: 1px solid var(--an-border);
  border-radius: var(--an-radius-md);
  background: color-mix(in srgb, var(--an-surface-soft) 66%, transparent);
  padding: 0.5rem 0.65rem;
}
.markdown-body summary {
  cursor: pointer;
  font-weight: 600;
  color: var(--an-text);
}
.markdown-body details[open] > summary { margin-bottom: 0.55rem; }

/* execution-log viewer */
.log-viewer {
  font-family: var(--an-font-mono);
  font-size: 12px;
  line-height: 1.58;
  background: color-mix(in srgb, var(--an-surface-soft) 88%, var(--an-bg));
  color: var(--an-text);
  border: 1px solid var(--an-border-strong);
  border-radius: var(--an-radius-md);
  padding: 14px 16px;
  overflow-x: auto;
  white-space: pre;
  tab-size: 4;
  max-height: 620px;
  overflow-y: auto;
}
.log-viewer .log-ts      { color: var(--an-text-soft); }
.log-viewer .log-header  { color: var(--an-accent); font-weight: 700; }
.log-viewer .log-detail  { color: var(--an-text); }
.log-viewer .log-success { color: var(--an-success); }
.log-viewer .log-warning { color: var(--an-warning); }
.log-viewer .log-error   { color: var(--an-danger); font-weight: 700; }
.log-viewer .log-stage   { color: var(--an-accent-hover); font-weight: 700; }

/* report media cards */
.an-media-card {
  margin: 12px 0 18px 0;
  padding: 14px 14px;
  border: 1px solid var(--an-border);
  border-radius: var(--an-radius-lg);
  background: var(--an-surface-soft);
}
.an-media-grid { display: grid; grid-template-columns: 1.25fr 1fr; gap: 14px; align-items: start; }
@media (max-width: 900px) { .an-media-grid { grid-template-columns: 1fr; } }
.an-figure { margin: 0; }
.an-figure img {
  width: 100%;
  height: auto;
  display: block;
  border-radius: var(--an-radius-md);
  border: 1px solid var(--an-border);
  background: var(--an-surface);
}
.an-figcap { margin-top: 8px; color: var(--an-text-soft); font-size: 12px; }
.an-meta-title { font-weight: 700; margin: 2px 0 10px 0; color: var(--an-text); }
.an-stats-table { width: 100%; border-collapse: collapse; font-size: 12px; border-radius: var(--an-radius-md); overflow: hidden; }
.an-stats-table td { padding: 6px 8px; border: 1px solid var(--an-border); }
.an-stats-table td:first-child { color: var(--an-text-soft); width: 45%; }
.an-secondary-gallery { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 12px; margin-top: 10px; }
.an-secondary-card { padding: 10px; border: 1px solid var(--an-border); border-radius: var(--an-radius-md); background: var(--an-surface-soft); }
.an-secondary-card img { width: 100%; height: auto; display: block; border-radius: var(--an-radius-sm); border: 1px solid var(--an-border); background: var(--an-surface); }
)AN_CSS";

    return css;
}

QString buildHtmlDocument(const QString& title, const QString& htmlBody,
                          const ImageIntegration::Reporting::ReportThemeMode themeMode)
{
    QString doc;
    QTextStream stream(&doc);
    stream << "<!DOCTYPE html>\n";
    stream << "<html lang=\"en\">\n";
    stream << "<head>\n";
    stream << "<meta charset=\"utf-8\" />\n";
    stream << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n";
    stream << "<title>" << title.toHtmlEscaped() << "</title>\n";
    stream << "<style>\n" << markdownBodyCss(themeMode) << "\n</style>\n";
    stream << "</head>\n";
    stream << "<body>\n";
    stream << "<div class=\"container\">\n";
    stream << "<main class=\"markdown-body\">\n" << htmlBody << "\n</main>\n";
    stream << "</div>\n";
    stream << "</body>\n";
    stream << "</html>\n";
    return doc;
}

// CMARK_OPT_UNSAFE allows inline HTML (e.g. <details>, <summary>) to pass
// through the renderer. Without it, cmark strips all raw HTML blocks.
constexpr int kCmarkOptions = CMARK_OPT_SMART | CMARK_OPT_UNSAFE;

bool renderMarkdownToHtmlBody(const QString& markdown, QString& htmlOut, QString* errorOut)
{
    const QByteArray markdownUtf8 = markdown.toUtf8();
#if AN_CMARK_GFM_EXTENSIONS
    cmark_gfm_core_extensions_ensure_registered();

    cmark_parser* parser = cmark_parser_new(kCmarkOptions);
    if (!parser)
    {
        if (errorOut)
        {
            *errorOut = QStringLiteral("Failed to create markdown parser.");
        }
        return false;
    }

    cmark_mem* mem = cmark_get_default_mem_allocator();
    cmark_llist* extensions = nullptr;
    auto attachExtension = [&](const char* extensionName, bool required) -> bool {
        cmark_syntax_extension* extension = cmark_find_syntax_extension(extensionName);
        if (!extension)
        {
            if (required && errorOut)
            {
                *errorOut = QStringLiteral("Required markdown extension is unavailable: %1")
                                .arg(QString::fromLatin1(extensionName));
            }
            return !required;
        }
        if (!cmark_parser_attach_syntax_extension(parser, extension))
        {
            if (required && errorOut)
            {
                *errorOut =
                    QStringLiteral("Failed to attach markdown extension: %1").arg(QString::fromLatin1(extensionName));
            }
            return !required;
        }

        cmark_llist* updated = cmark_llist_append(mem, extensions, extension);
        if (!updated)
        {
            if (required && errorOut)
            {
                *errorOut = QStringLiteral("Failed to register markdown extension in renderer: %1")
                                .arg(QString::fromLatin1(extensionName));
            }
            return !required;
        }
        extensions = updated;
        return true;
    };

    if (!attachExtension("table", true))
    {
        cmark_parser_free(parser);
        cmark_llist_free(mem, extensions);
        return false;
    }

    static constexpr std::array<const char*, 4> kOptionalExtensions = {"strikethrough", "autolink", "tagfilter",
                                                                       "tasklist"};
    for (const char* extensionName : kOptionalExtensions)
    {
        (void)attachExtension(extensionName, false);
    }

    cmark_parser_feed(parser, markdownUtf8.constData(), static_cast<size_t>(markdownUtf8.size()));
    cmark_node* document = cmark_parser_finish(parser);
    cmark_parser_free(parser);
    if (!document)
    {
        cmark_llist_free(mem, extensions);
        if (errorOut)
        {
            *errorOut = QStringLiteral("Markdown parser did not produce a document.");
        }
        return false;
    }

    char* html = cmark_render_html(document, kCmarkOptions, extensions);
    cmark_node_free(document);
    cmark_llist_free(mem, extensions);
    if (!html)
    {
        if (errorOut)
        {
            *errorOut = QStringLiteral("Markdown renderer returned null output.");
        }
        return false;
    }

    htmlOut = QString::fromUtf8(html);
    std::free(html);
    return true;
#else
    char* html =
        cmark_markdown_to_html(markdownUtf8.constData(), static_cast<size_t>(markdownUtf8.size()), kCmarkOptions);
    if (!html)
    {
        if (errorOut)
        {
            *errorOut = QStringLiteral("Markdown parser returned null output.");
        }
        return false;
    }

    htmlOut = QString::fromUtf8(html);
    std::free(html);
    return true;
#endif
}
} // namespace

namespace ImageIntegration::Reporting
{
bool writeStyledHtmlFromMarkdown(const QString& markdown, const QString& htmlOutputPath, const QString& title,
                                 QString* errorOut)
{
    return writeStyledHtmlFromMarkdown(markdown, htmlOutputPath, title, ReportThemeMode::Active, errorOut);
}

bool writeStyledHtmlFromMarkdown(const QString& markdown, const QString& htmlOutputPath, const QString& title,
                                 const ReportThemeMode themeMode, QString* errorOut)
{
    QString htmlBody;
    if (!renderMarkdownToHtmlBody(markdown, htmlBody, errorOut))
    {
        return false;
    }

    const QFileInfo outputInfo(htmlOutputPath);
    QDir outputDir = outputInfo.dir();
    if (!outputDir.exists() && !outputDir.mkpath(QStringLiteral(".")))
    {
        if (errorOut)
        {
            *errorOut = QStringLiteral("Failed to create output directory: %1").arg(outputDir.path());
        }
        return false;
    }

    QFile htmlFile(htmlOutputPath);
    if (!htmlFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        if (errorOut)
        {
            *errorOut = QStringLiteral("Failed to write HTML report: %1").arg(htmlOutputPath);
        }
        return false;
    }

    const QString htmlDocument = buildHtmlDocument(title, htmlBody, themeMode);
    htmlFile.write(htmlDocument.toUtf8());
    htmlFile.close();
    return true;
}

bool writeStyledHtmlFromMarkdownFile(const QString& markdownInputPath, const QString& htmlOutputPath,
                                     const QString& title, QString* errorOut)
{
    return writeStyledHtmlFromMarkdownFile(markdownInputPath, htmlOutputPath, title, ReportThemeMode::Active, errorOut);
}

bool writeStyledHtmlFromMarkdownFile(const QString& markdownInputPath, const QString& htmlOutputPath,
                                     const QString& title, const ReportThemeMode themeMode, QString* errorOut)
{
    QFile mdFile(markdownInputPath);
    if (!mdFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorOut)
        {
            *errorOut = QStringLiteral("Failed to read markdown report: %1").arg(markdownInputPath);
        }
        return false;
    }

    const QString markdown = QString::fromUtf8(mdFile.readAll());
    mdFile.close();
    return writeStyledHtmlFromMarkdown(markdown, htmlOutputPath, title, themeMode, errorOut);
}
} // namespace ImageIntegration::Reporting
