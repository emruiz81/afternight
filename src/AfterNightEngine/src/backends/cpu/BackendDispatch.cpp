// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - which backend and instruction set an operation dispatches to.
//
// Selection, in one place, so that "which implementation is this operation on" has one answer.
//
// The order is the contract. The backend decides first, because the reference implementation is
// scalar by definition and asking which instruction set it runs on is a category mistake. Only a CPU
// operation then resolves an instruction set, and it resolves the operation's own constraint when it
// has one and the best available set when it does not - where "available" is two questions, not one:
// the host must be able to run the code and this build must contain it.
//
// Those two are separated deliberately and reported separately. A host that cannot run AVX-512 and a
// build that was not compiled with it are different problems with different answers, and a caller
// trying to force a conformance run needs to be told which one it hit. The execution context asks
// both questions when it is created and refuses, or falls back from, a set that fails either, so
// every execution that reaches this file already holds a set it can run.
//
// The selection itself lives beside the tables rather than here, because an operation about to run a
// kernel asks for the same table this reports on. Two derivations of one answer is how a run reports
// a backend it did not use.

#include <AfterNightEngine/Runtime/BackendDispatch.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include "backends/cpu/CpuKernelTable.h"

#include <cstdio>

namespace an
{

bool instructionSetIsCompiledIn(InstructionSet instructionSet) noexcept
{
    return cpuKernelTableExists(instructionSet);
}

InstructionSet instructionSetOfDispatchedCode(const ExecutionContext& execution) noexcept
{
    if (!execution.valid())
    {
        return InstructionSet::Automatic;
    }

    // Asked of the compiled code rather than of the table's own label, which is the whole reason
    // the entry exists: the two are filled in from different places and a table wired to the wrong
    // implementation is where they disagree.
    const CpuKernelTable& table = cpuKernelTableFor(execution);
    return table.reportInstructionSet != nullptr ? table.reportInstructionSet() : InstructionSet::Automatic;
}

Result<BackendSelection> selectBackend(const ExecutionContext& execution)
{
    if (!execution.valid())
    {
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "an operation that holds nothing dispatches to nothing",
                                            .operation = "an::selectBackend",
                                            .domain = ErrorDomain::Runtime});
    }

    if (execution.backend() == ComputeBackend::Gpu)
    {
        return Status::failure(ErrorCode::UnsupportedBackend,
                               StatusDetail{.message = "this build has no dispatch table for the requested backend",
                                            .operation = "an::selectBackend",
                                            .domain = ErrorDomain::Runtime,
                                            .context = {StatusContextField::ofText(
                                                ContextKey::Parameter, computeBackendName(execution.backend()))}});
    }

    const CpuKernelTable& table = cpuKernelTableFor(execution);
    const BackendSelection selection{
        .backend = execution.backend() == ComputeBackend::Reference ? ComputeBackend::Reference : ComputeBackend::Cpu,
        .instructionSet = table.reportInstructionSet != nullptr ? table.reportInstructionSet() : table.instructionSet};

    // Said out loud at a severity a normal run filters out, so that a run which behaved unexpectedly
    // can be asked what it dispatched to without being reproduced under a debugger. The buffer is on
    // the stack because this is on the path into a kernel and a diagnostic that allocated would be a
    // diagnostic nobody could afford to leave enabled.
    char message[96] = {};
    std::snprintf(message, sizeof(message), "dispatching to backend %s with instruction set %s",
                  computeBackendName(selection.backend), instructionSetName(selection.instructionSet));
    execution.reportDiagnostic(DiagnosticSeverity::Debug, ErrorDomain::Runtime, "an::selectBackend", message);

    return selection;
}

} // namespace an
