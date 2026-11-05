// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Runtime/DiagnosticSink.h>

// The severity names, out of line for the same reason the error-code names are: one definition, so
// two translation units compiled at different times cannot print a severity differently, and no
// translation unit that merely mentions a severity carries a table of strings it never reads.
//
// The switch carries no `default` label, so a severity added without a name fails the build.

namespace an
{

const char* diagnosticSeverityName(DiagnosticSeverity severity) noexcept
{
    switch (severity)
    {
    case DiagnosticSeverity::Trace:
        return "Trace";
    case DiagnosticSeverity::Debug:
        return "Debug";
    case DiagnosticSeverity::Info:
        return "Info";
    case DiagnosticSeverity::Warning:
        return "Warning";
    case DiagnosticSeverity::Error:
        return "Error";
    }
    return "UnknownDiagnosticSeverity";
}

} // namespace an
