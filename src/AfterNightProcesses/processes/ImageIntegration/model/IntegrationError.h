// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/**
 * @file IntegrationError.h
 * @brief Structured error types for the ImageIntegration workflow.
 *
 * This header defines a comprehensive error handling infrastructure for the
 * integration workflow. Errors are categorized by domain and include rich
 * context information for diagnostics and user feedback.
 *
 * @par Error Categories
 * - **IO Errors**: File access, permissions, disk space
 * - **Validation Errors**: Data validation failures
 * - **Processing Errors**: Runtime algorithm failures
 * - **Cancellation**: User-initiated workflow abort
 *
 * @par Usage
 * @code
 * IntegrationError error = IntegrationError::fileNotFound("light_001.fits");
 * if (error.isRecoverable()) {
 *     // Attempt recovery or continue with remaining files
 * } else {
 *     // Abort workflow and report error
 * }
 * @endcode
 *
 * @see StageResult, WorkflowExecutor, ValidationResult
 */

#include <QString>
#include <QStringList>

#include <optional>
#include <variant>

/**
 * @brief Error category codes for workflow errors.
 *
 * Each code represents a specific error condition that can occur during
 * workflow execution. Codes are grouped by domain for easier handling.
 */
enum class IntegrationErrorCode
{
    // Success (no error)
    None = 0,

    // IO Errors (1xx)
    FileNotFound = 100, ///< Input file does not exist
    FileNotReadable = 101, ///< File exists but cannot be read
    FileNotWritable = 102, ///< Cannot write to output location
    DirectoryNotFound = 103, ///< Directory does not exist
    DirectoryNotWritable = 104, ///< Cannot write to directory
    DiskFull = 105, ///< Insufficient disk space
    FileLocked = 106, ///< File is locked by another process
    InvalidFileFormat = 107, ///< File format not recognized or corrupt

    // Validation Errors (2xx)
    MissingRequiredFrames = 200, ///< Required calibration frames not provided
    IncompatibleDimensions = 201, ///< Frame dimensions don't match
    IncompatibleChannels = 202, ///< Channel count mismatch (mono vs color)
    InvalidConfiguration = 203, ///< Invalid workflow configuration
    EmptyFrameList = 204, ///< No frames to process
    InsufficientFrames = 205, ///< Too few frames for operation (e.g., stacking)
    InvalidOutputPath = 206, ///< Output path is invalid

    // Processing Errors (3xx)
    StackingFailed = 300, ///< Stacking algorithm failed
    RegistrationFailed = 301, ///< Star alignment/registration failed
    CalibrationFailed = 302, ///< Calibration application failed
    NormalizationFailed = 303, ///< Normalization calculation failed
    DrizzleFailed = 304, ///< Drizzle integration failed
    MemoryExhausted = 305, ///< Out of memory during processing
    InternalError = 306, ///< Unexpected internal error

    // Cancellation (4xx)
    Cancelled = 400, ///< Workflow cancelled by user
    Timeout = 401 ///< Operation timed out
};

/**
 * @brief Check if an error code represents a recoverable condition.
 * @param code The error code to check.
 * @return true if the workflow may continue despite this error.
 */
[[nodiscard]] inline bool isRecoverableError(IntegrationErrorCode code) noexcept
{
    switch (code)
    {
    case IntegrationErrorCode::None:
    case IntegrationErrorCode::FileNotFound: // Skip missing file
    case IntegrationErrorCode::FileNotReadable: // Skip unreadable file
    case IntegrationErrorCode::InvalidFileFormat: // Skip corrupt file
        return true;
    default:
        return false;
    }
}

/**
 * @brief Check if an error code represents user cancellation.
 * @param code The error code to check.
 * @return true if the error was caused by user action.
 */
[[nodiscard]] inline bool isCancellationError(IntegrationErrorCode code) noexcept
{
    return code == IntegrationErrorCode::Cancelled || code == IntegrationErrorCode::Timeout;
}

/**
 * @brief Structured error with code, message, and context.
 *
 * IntegrationError provides a rich error representation that includes:
 * - A categorized error code for programmatic handling
 * - A human-readable message for user display
 * - Optional file path context
 * - Optional additional details
 *
 * @par Factory Methods
 * Use the static factory methods to create errors with appropriate context:
 * - IntegrationError::fileNotFound()
 * - IntegrationError::stackingFailed()
 * - IntegrationError::cancelled()
 */
class IntegrationError
{
public:
    /** @brief Construct a success (no error) result. */
    IntegrationError() = default;

    /**
     * @brief Construct an error with full details.
     * @param code Error category code.
     * @param message Human-readable error message.
     * @param filePath Optional file path context.
     * @param details Optional additional details.
     */
    IntegrationError(IntegrationErrorCode code, const QString& message, const QString& filePath = {},
                     const QString& details = {})
        : m_code(code), m_message(message), m_filePath(filePath), m_details(details)
    {
    }

    /** @brief Return the error code. */
    [[nodiscard]] IntegrationErrorCode code() const noexcept
    {
        return m_code;
    }

    /** @brief Return the human-readable error message. */
    [[nodiscard]] const QString& message() const noexcept
    {
        return m_message;
    }

    /** @brief Return the associated file path, if any. */
    [[nodiscard]] const QString& filePath() const noexcept
    {
        return m_filePath;
    }

    /** @brief Return additional error details, if any. */
    [[nodiscard]] const QString& details() const noexcept
    {
        return m_details;
    }

    /** @brief Check if this represents a successful (no error) state. */
    [[nodiscard]] bool isSuccess() const noexcept
    {
        return m_code == IntegrationErrorCode::None;
    }

    /** @brief Check if this error allows workflow continuation. */
    [[nodiscard]] bool isRecoverable() const noexcept
    {
        return isRecoverableError(m_code);
    }

    /** @brief Check if this error was caused by user cancellation. */
    [[nodiscard]] bool isCancellation() const noexcept
    {
        return isCancellationError(m_code);
    }

    /**
     * @brief Format a complete error description for logging.
     * @return Multi-line string with code, message, path, and details.
     */
    [[nodiscard]] QString toLogString() const
    {
        QString result = QStringLiteral("[%1] %2").arg(static_cast<int>(m_code)).arg(m_message);

        if (!m_filePath.isEmpty())
        {
            result += QStringLiteral("\n  File: %1").arg(m_filePath);
        }
        if (!m_details.isEmpty())
        {
            result += QStringLiteral("\n  Details: %1").arg(m_details);
        }
        return result;
    }

    /**
     * @brief Format a user-friendly error description for UI display.
     * @return Single-line or short multi-line string suitable for dialogs/toasts.
     *
     * Unlike toLogString(), this method omits error codes and focuses on
     * actionable information for the user.
     */
    [[nodiscard]] QString toUserString() const
    {
        QString result = m_message;

        if (!m_filePath.isEmpty())
        {
            // Show just the filename, not the full path
            // Use parentheses around std::max to prevent Windows max macro expansion
            const int lastSep =
                (std::max)(m_filePath.lastIndexOf(QLatin1Char('/')), m_filePath.lastIndexOf(QLatin1Char('\\')));
            const QString filename = (lastSep >= 0) ? m_filePath.mid(lastSep + 1) : m_filePath;
            result += QStringLiteral(": %1").arg(filename);
        }
        if (!m_details.isEmpty())
        {
            result += QStringLiteral("\n%1").arg(m_details);
        }
        return result;
    }

    // ========================================================================
    // Factory methods for common errors
    // ========================================================================

    /** @brief Create a success (no error) result. */
    [[nodiscard]] static IntegrationError success()
    {
        return IntegrationError();
    }

    /** @brief Create a file not found error. */
    [[nodiscard]] static IntegrationError fileNotFound(const QString& path)
    {
        return IntegrationError(IntegrationErrorCode::FileNotFound, QStringLiteral("File not found"), path);
    }

    /** @brief Create a file not readable error. */
    [[nodiscard]] static IntegrationError fileNotReadable(const QString& path, const QString& reason = {})
    {
        return IntegrationError(IntegrationErrorCode::FileNotReadable, QStringLiteral("File cannot be read"), path,
                                reason);
    }

    /** @brief Create a directory not writable error. */
    [[nodiscard]] static IntegrationError directoryNotWritable(const QString& path)
    {
        return IntegrationError(IntegrationErrorCode::DirectoryNotWritable, QStringLiteral("Cannot write to directory"),
                                path);
    }

    /** @brief Create an invalid file format error. */
    [[nodiscard]] static IntegrationError invalidFileFormat(const QString& path, const QString& reason = {})
    {
        return IntegrationError(IntegrationErrorCode::InvalidFileFormat,
                                QStringLiteral("Invalid or unsupported file format"), path, reason);
    }

    /** @brief Create a dimension mismatch error. */
    [[nodiscard]] static IntegrationError incompatibleDimensions(const QString& expected, const QString& actual)
    {
        return IntegrationError(IntegrationErrorCode::IncompatibleDimensions,
                                QStringLiteral("Frame dimensions do not match"), {},
                                QStringLiteral("Expected %1, got %2").arg(expected, actual));
    }

    /** @brief Create an insufficient frames error. */
    [[nodiscard]] static IntegrationError insufficientFrames(int required, int provided)
    {
        return IntegrationError(IntegrationErrorCode::InsufficientFrames,
                                QStringLiteral("Minimum of %1 images required, got %2").arg(required).arg(provided), {},
                                QStringLiteral("Required at least %1 frames, got %2").arg(required).arg(provided));
    }

    /** @brief Create a stacking failed error. */
    [[nodiscard]] static IntegrationError stackingFailed(const QString& reason)
    {
        return IntegrationError(IntegrationErrorCode::StackingFailed, QStringLiteral("Stacking operation failed"), {},
                                reason);
    }

    /** @brief Create a registration failed error. */
    [[nodiscard]] static IntegrationError registrationFailed(const QString& reason)
    {
        return IntegrationError(IntegrationErrorCode::RegistrationFailed,
                                QStringLiteral("Registration/alignment failed"), {}, reason);
    }

    /** @brief Create a memory exhausted error. */
    [[nodiscard]] static IntegrationError memoryExhausted()
    {
        return IntegrationError(IntegrationErrorCode::MemoryExhausted, QStringLiteral("Out of memory"), {},
                                QStringLiteral("Try reducing the number of frames or closing other applications"));
    }

    /** @brief Create a disk full error. */
    [[nodiscard]] static IntegrationError diskFull(const QString& path)
    {
        return IntegrationError(IntegrationErrorCode::DiskFull, QStringLiteral("Insufficient disk space"), path,
                                QStringLiteral("Free up disk space or choose a different output location"));
    }

    /** @brief Create a file locked error. */
    [[nodiscard]] static IntegrationError fileLocked(const QString& path)
    {
        return IntegrationError(IntegrationErrorCode::FileLocked, QStringLiteral("File is locked by another process"),
                                path, QStringLiteral("Close any applications that may have this file open"));
    }

    /** @brief Create a user cancellation error. */
    [[nodiscard]] static IntegrationError cancelled()
    {
        return IntegrationError(IntegrationErrorCode::Cancelled, QStringLiteral("Operation cancelled by user"));
    }

    /** @brief Create an internal error. */
    [[nodiscard]] static IntegrationError internalError(const QString& details)
    {
        return IntegrationError(IntegrationErrorCode::InternalError, QStringLiteral("Internal error"), {}, details);
    }

private:
    IntegrationErrorCode m_code = IntegrationErrorCode::None;
    QString m_message;
    QString m_filePath;
    QString m_details;
};

/**
 * @brief Collection of errors from a workflow stage.
 *
 * ErrorReport aggregates multiple errors that may occur during a stage,
 * providing summary statistics and iteration over individual errors.
 */
struct IntegrationErrorReport
{
    QStringList warnings; ///< Non-fatal warning messages
    std::vector<IntegrationError> errors; ///< All errors encountered

    /** @brief Check if any fatal errors occurred. */
    [[nodiscard]] bool hasFatalErrors() const
    {
        for (const auto& error : errors)
        {
            if (!error.isRecoverable() && !error.isCancellation())
            {
                return true;
            }
        }
        return false;
    }

    /** @brief Count of recoverable errors (skipped files). */
    [[nodiscard]] size_t recoverableErrorCount() const
    {
        size_t count = 0;
        for (const auto& error : errors)
        {
            if (error.isRecoverable())
            {
                ++count;
            }
        }
        return count;
    }

    /** @brief Check if workflow was cancelled. */
    [[nodiscard]] bool wasCancelled() const
    {
        for (const auto& error : errors)
        {
            if (error.isCancellation())
            {
                return true;
            }
        }
        return false;
    }

    /** @brief Add an error to the report. */
    void addError(IntegrationError error)
    {
        errors.push_back(std::move(error));
    }

    /** @brief Add a warning message. */
    void addWarning(const QString& message)
    {
        warnings.append(message);
    }

    /** @brief Merge another error report into this one. */
    void merge(const IntegrationErrorReport& other)
    {
        warnings.append(other.warnings);
        errors.insert(errors.end(), other.errors.begin(), other.errors.end());
    }
};
