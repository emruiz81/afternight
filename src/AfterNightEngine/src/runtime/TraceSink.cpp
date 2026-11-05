// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the stable spelling of each trace event kind.
//
// Out of line for the reason the error-code names are: one definition, so two translation units
// cannot spell a kind differently. The switch carries no `default` label, so a kind added without a
// name fails the build; the trailing return is for a value cast in from outside the enumeration.

#include <AfterNightEngine/Runtime/TraceSink.h>

namespace an
{

const char* traceEventKindName(TraceEventKind kind) noexcept
{
    switch (kind)
    {
    case TraceEventKind::Instant:
        return "Instant";
    case TraceEventKind::SpanBegin:
        return "SpanBegin";
    case TraceEventKind::SpanEnd:
        return "SpanEnd";
    case TraceEventKind::Counter:
        return "Counter";
    }
    return "UnknownTraceEventKind";
}

} // namespace an
