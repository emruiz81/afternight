// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/**
 * @file ValidationResult.h
 * @brief Workflow validation types for pre-execution checks.
 *
 * This header defines the validation infrastructure used to check workflow
 * prerequisites before execution. Validation issues include severity levels
 * and optional navigation hints for the UI.
 *
 * @see IntegrationDataModel::validate()
 */

#include <QString>

#include <map>
#include <vector>

#include <optional>

/**
 * @brief Target tab/section for "Fix" button navigation.
 *
 * When a validation issue is detected, this enum indicates which part of
 * the UI the user should navigate to in order to resolve the issue.
 */
enum class FixTarget
{
    None, ///< No fix action available
    OutputDirectory, ///< Navigate to output directory setting
    BiasFrames, ///< Navigate to BIAS frame tab
    DarkFrames, ///< Navigate to DARK frame tab
    FlatFrames, ///< Navigate to FLAT frame tab
    LightFrames, ///< Navigate to LIGHT frame tab
    CalibrationTab, ///< Navigate to Calibration tab
    RegistrationTab, ///< Navigate to Registration tab
    IntegrationTab ///< Navigate to Integration/Stacking tab
};

enum class ValidationErrorCode
{
    Unknown = 0,

    // Output directory
    MissingOutputDirectory,
    OutputPathNotDirectory,
    OutputDirectoryNotWritable,
    OutputDirectoryCannotCreate,

    // Frames
    NoLightFrames,

    // Calibration
    MissingCalibrationMatch, ///< No calibration match found (red cell)
    FallbackCalibrationMatch, ///< Using fallback match from nearby session (yellow cell)
    NoCalibrationFramesProvided, ///< User didn't add any bias/dark/flat frames (info only)
    RedundantCalibration, ///< Both Dark and Bias provided in non-CCD mode (warning)
    ManuallyUnassignedCalibration ///< User explicitly disabled calibration for this group (warning)
};

enum class CalibrationFrameType
{
    Bias,
    Dark,
    Flat,
    DarkOrBias
};

/**
 * @brief A single validation issue with severity and fix guidance.
 *
 * Validation issues are generated during pre-execution checks and displayed
 * to the user in the workflow dialog. Each issue has a severity level that
 * determines whether execution can proceed.
 */
struct ValidationIssue
{
    /**
     * @brief Issue severity level.
     * - Error: Blocks workflow execution; must be resolved.
     * - Warning: Allows execution but may indicate a problem.
     */
    enum class Severity
    {
        Error, ///< Critical issue that prevents execution
        Warning ///< Non-critical issue that should be reviewed
    };

    Severity severity = Severity::Error; ///< Issue severity level
    ValidationErrorCode code = ValidationErrorCode::Unknown; ///< Structured error code
    QString message; ///< Human-readable issue description
    QString filePath; ///< Optional file path context
    FixTarget fixTarget = FixTarget::None; ///< Navigation hint for resolution
    std::optional<CalibrationFrameType> missingCalibrationType; ///< For MissingCalibrationMatch issues

    ValidationIssue() = default;

    ValidationIssue(Severity sev, const QString& msg) : severity(sev), message(msg) {}

    ValidationIssue(Severity sev, const QString& msg, FixTarget fix) : severity(sev), message(msg), fixTarget(fix) {}

    ValidationIssue(Severity sev, ValidationErrorCode c, const QString& msg, const QString& file, FixTarget fix)
        : severity(sev), code(c), message(msg), filePath(file), fixTarget(fix)
    {
    }

    [[nodiscard]] QString severityIcon() const
    {
        return (severity == Severity::Error) ? QStringLiteral("⛔") : QStringLiteral("⚠");
    }

    [[nodiscard]] QString category() const
    {
        switch (code)
        {
        case ValidationErrorCode::MissingOutputDirectory:
        case ValidationErrorCode::OutputPathNotDirectory:
        case ValidationErrorCode::OutputDirectoryNotWritable:
        case ValidationErrorCode::OutputDirectoryCannotCreate:
            return QStringLiteral("Output");
        case ValidationErrorCode::NoLightFrames:
            return QStringLiteral("Frames");
        case ValidationErrorCode::MissingCalibrationMatch:
        case ValidationErrorCode::FallbackCalibrationMatch:
        case ValidationErrorCode::NoCalibrationFramesProvided:
        case ValidationErrorCode::ManuallyUnassignedCalibration:
            return QStringLiteral("Calibration");
        case ValidationErrorCode::Unknown:
        default:
            break;
        }

        // An issue whose code names no area is grouped by the place that fixes it.
        if (fixTarget == FixTarget::CalibrationTab)
        {
            return QStringLiteral("Calibration");
        }
        if (fixTarget == FixTarget::OutputDirectory)
        {
            return QStringLiteral("Output");
        }
        return QStringLiteral("General");
    }

    static ValidationIssue missingOutputDirectory()
    {
        return ValidationIssue(Severity::Error, ValidationErrorCode::MissingOutputDirectory,
                               QStringLiteral("Output directory is required."), QString(), FixTarget::OutputDirectory);
    }

    static ValidationIssue missingCalibration(const QString& lightPath, CalibrationFrameType missingType)
    {
        return missingCalibration(lightPath, missingType, Severity::Warning);
    }

    static ValidationIssue missingCalibration(const QString& lightPath, CalibrationFrameType missingType,
                                              Severity severity)
    {
        QString type;
        switch (missingType)
        {
        case CalibrationFrameType::Bias:
            type = QStringLiteral("bias");
            break;
        case CalibrationFrameType::Dark:
            type = QStringLiteral("dark");
            break;
        case CalibrationFrameType::Flat:
            type = QStringLiteral("flat");
            break;
        case CalibrationFrameType::DarkOrBias:
            type = QStringLiteral("dark/bias");
            break;
        }

        ValidationIssue issue(severity, ValidationErrorCode::MissingCalibrationMatch,
                              QStringLiteral("Missing %1 calibration match").arg(type), lightPath,
                              FixTarget::CalibrationTab);
        issue.missingCalibrationType = missingType;
        return issue;
    }

    static ValidationIssue fallbackCalibration(const QString& lightGroupLabel, CalibrationFrameType fallbackType)
    {
        QString type;
        switch (fallbackType)
        {
        case CalibrationFrameType::Bias:
            type = QStringLiteral("bias");
            break;
        case CalibrationFrameType::Dark:
            type = QStringLiteral("dark");
            break;
        case CalibrationFrameType::Flat:
            type = QStringLiteral("flat");
            break;
        case CalibrationFrameType::DarkOrBias:
            // Not a valid fallback type; treat as generic.
            type = QStringLiteral("dark/bias");
            break;
        }

        ValidationIssue issue(Severity::Warning, ValidationErrorCode::FallbackCalibrationMatch,
                              QStringLiteral("Using fallback %1 from nearby session").arg(type), lightGroupLabel,
                              FixTarget::CalibrationTab);
        issue.missingCalibrationType = fallbackType;
        return issue;
    }

    static ValidationIssue manuallyUnassignedCalibration(const QString& lightGroupLabel,
                                                         CalibrationFrameType unassignedType)
    {
        QString type;
        switch (unassignedType)
        {
        case CalibrationFrameType::Bias:
            type = QStringLiteral("bias");
            break;
        case CalibrationFrameType::Dark:
            type = QStringLiteral("dark");
            break;
        case CalibrationFrameType::Flat:
            type = QStringLiteral("flat");
            break;
        case CalibrationFrameType::DarkOrBias:
            type = QStringLiteral("dark/bias");
            break;
        }

        ValidationIssue issue(Severity::Warning, ValidationErrorCode::ManuallyUnassignedCalibration,
                              QStringLiteral("Manually disabled %1 calibration").arg(type), lightGroupLabel,
                              FixTarget::CalibrationTab);
        issue.missingCalibrationType = unassignedType;
        return issue;
    }
};

/**
 * @brief Collection of validation issues from a workflow check.
 *
 * This struct aggregates all issues found during validation and provides
 * a convenience method to check for blocking errors.
 */
struct WorkflowValidationResult
{
    std::vector<ValidationIssue> issues; ///< All validation issues found

    /**
     * @brief Check if any blocking errors exist.
     * @return true if at least one Error-severity issue exists.
     */
    [[nodiscard]] bool hasErrors() const;

    /**
     * @brief Group issues by category for UI presentation.
     */
    [[nodiscard]] std::map<QString, std::vector<ValidationIssue>> groupedIssues() const;
};

inline bool WorkflowValidationResult::hasErrors() const
{
    for (const auto& issue : issues)
    {
        if (issue.severity == ValidationIssue::Severity::Error)
        {
            return true;
        }
    }

    return false;
}

inline std::map<QString, std::vector<ValidationIssue>> WorkflowValidationResult::groupedIssues() const
{
    std::map<QString, std::vector<ValidationIssue>> grouped;
    for (const auto& issue : issues)
    {
        grouped[issue.category()].push_back(issue);
    }
    return grouped;
}
