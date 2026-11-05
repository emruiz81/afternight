// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QString>

namespace ImageIntegration::Reporting
{
enum class ReportThemeMode
{
    Active,
    Light,
    Dark
};

[[nodiscard]] AFTERNIGHTPROCESSES_API bool writeStyledHtmlFromMarkdown(const QString& markdown,
                                                                       const QString& htmlOutputPath,
                                                                       const QString& title,
                                                                       QString* errorOut = nullptr);

[[nodiscard]] AFTERNIGHTPROCESSES_API bool writeStyledHtmlFromMarkdown(const QString& markdown,
                                                                       const QString& htmlOutputPath,
                                                                       const QString& title, ReportThemeMode themeMode,
                                                                       QString* errorOut = nullptr);

[[nodiscard]] bool writeStyledHtmlFromMarkdownFile(const QString& markdownInputPath, const QString& htmlOutputPath,
                                                   const QString& title, QString* errorOut = nullptr);

[[nodiscard]] bool writeStyledHtmlFromMarkdownFile(const QString& markdownInputPath, const QString& htmlOutputPath,
                                                   const QString& title, ReportThemeMode themeMode,
                                                   QString* errorOut = nullptr);
} // namespace ImageIntegration::Reporting
