// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file StatusIndicators.h
 * @brief Color constants and icon helpers for validation status display.
 *
 * Provides consistent theming for status indicators across the
 * Image Integration UI: success, warning, error, pending, active states.
 */
#pragma once

#include <QColor>
#include <QString>

#include "processes/ImageIntegration/model/ValidationResult.h"
#include "theme/AnDesignTokens.h"

/**
 * @namespace StatusColors
 * @brief Predefined colors for validation and row status indicators.
 *
 * Two categories:
 * - Status colors: for icons, badges, and text (Success, Warning, Error, etc.)
 * - Row colors: for table/tree row backgrounds (RowComplete, RowWarning, etc.)
 */
namespace StatusColors
{
// Status colors
[[nodiscard]] inline QColor Success()
{
    return an::ui::tokens().colors.statusSuccess;
}

[[nodiscard]] inline QColor Warning()
{
    return an::ui::tokens().colors.statusWarning;
}

[[nodiscard]] inline QColor Error()
{
    return an::ui::tokens().colors.statusError;
}

[[nodiscard]] inline QColor Pending()
{
    return an::ui::tokens().colors.textTertiary;
}

[[nodiscard]] inline QColor Active()
{
    return an::ui::tokens().colors.accentPrimary;
}

[[nodiscard]] inline QColor Master()
{
    return an::ui::tokens().colors.statusWarning;
}

// Row background colors
[[nodiscard]] inline QColor RowComplete()
{
    return an::ui::tokens().colors.statusSuccessSoft;
}

[[nodiscard]] inline QColor RowWarning()
{
    return an::ui::tokens().colors.statusWarningSoft;
}

[[nodiscard]] inline QColor RowError()
{
    return an::ui::tokens().colors.statusErrorSoft;
}

[[nodiscard]] inline QColor RowSelected()
{
    return an::ui::tokens().colors.accentPinned;
}

[[nodiscard]] inline QColor RowDisabled()
{
    return an::ui::tokens().colors.bgSurface;
}
} // namespace StatusColors

/**
 * @brief Returns a Unicode icon character for the given validation status.
 * @param status The validation status to represent.
 * @return Unicode string: ✓ (Ok), ⚠ (Warning), ✗ (Error), ⏳ (Pending), ▶ (Active).
 */
inline QString getStatusIcon(ValidationStatus status)
{
    switch (status)
    {
    case ValidationStatus::Ok:
        return QStringLiteral("✓");
    case ValidationStatus::Warning:
        return QStringLiteral("⚠");
    case ValidationStatus::Error:
        return QStringLiteral("✗");
    case ValidationStatus::Pending:
        return QStringLiteral("⏳");
    case ValidationStatus::Active:
        return QStringLiteral("▶");
    }
    return QStringLiteral("?");
}
