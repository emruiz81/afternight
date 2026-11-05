// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

// The names of the three execution policies, kept out of the header for the reason the error
// vocabulary's names are: a switch over strings compiled into every translation unit that mentions
// a backend would be the whole engine paying for text almost nothing reads, and one definition here
// means the name a manifest records cannot differ between two translation units.
//
// None of these switches carries a `default` label. That is what makes adding an enumerator without
// naming it a compile failure rather than a blank in somebody's report. The trailing return after
// each switch is for a value cast in from outside the enumeration, which is a different thing and
// says so.

namespace an
{

const char* computeBackendName(ComputeBackend backend) noexcept
{
    switch (backend)
    {
    case ComputeBackend::Automatic:
        return "Automatic";
    case ComputeBackend::Reference:
        return "Reference";
    case ComputeBackend::Cpu:
        return "Cpu";
    case ComputeBackend::Gpu:
        return "Gpu";
    }
    return "UnknownComputeBackend";
}

const char* backendFallbackName(BackendFallback fallback) noexcept
{
    switch (fallback)
    {
    case BackendFallback::Allowed:
        return "Allowed";
    case BackendFallback::Forbidden:
        return "Forbidden";
    }
    return "UnknownBackendFallback";
}

const char* reductionDeterminismName(ReductionDeterminism determinism) noexcept
{
    switch (determinism)
    {
    case ReductionDeterminism::DecisionFeeding:
        return "DecisionFeeding";
    case ReductionDeterminism::All:
        return "All";
    }
    return "UnknownReductionDeterminism";
}

} // namespace an
