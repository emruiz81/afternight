// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Core/ErrorCode.h>

// The text beside every code, kept out of the header on purpose.
//
// Two reasons, and the second is the one that matters. A switch over the whole enumeration compiled
// into every translation unit that can fail would be the whole engine paying for strings almost
// nothing reads; and a single definition here means the name a diagnostic prints cannot differ
// between two translation units that were compiled at different times.
//
// None of these switches has a `default` label. That is what makes adding a code to the enumeration
// without naming it here a compile failure rather than a blank in somebody's log: the compilers
// this engine is built with report an unhandled enumerator, and this project builds warnings as
// errors. The trailing return after each switch is for a value cast in from outside the
// enumeration, which is a different thing and says so.

namespace an
{

const char* errorCodeName(ErrorCode code) noexcept
{
    switch (code)
    {
    case ErrorCode::Ok:
        return "Ok";
    case ErrorCode::InvalidArgument:
        return "InvalidArgument";
    case ErrorCode::OutOfRange:
        return "OutOfRange";
    case ErrorCode::IncompatibleArguments:
        return "IncompatibleArguments";
    case ErrorCode::Aliasing:
        return "Aliasing";
    case ErrorCode::NotInitialized:
        return "NotInitialized";
    case ErrorCode::AlreadyInitialized:
        return "AlreadyInitialized";
    case ErrorCode::InvalidState:
        return "InvalidState";
    case ErrorCode::Busy:
        return "Busy";
    case ErrorCode::Unsupported:
        return "Unsupported";
    case ErrorCode::UnsupportedBackend:
        return "UnsupportedBackend";
    case ErrorCode::NotImplemented:
        return "NotImplemented";
    case ErrorCode::AllocationFailed:
        return "AllocationFailed";
    case ErrorCode::MemoryBudgetExceeded:
        return "MemoryBudgetExceeded";
    case ErrorCode::LimitExceeded:
        return "LimitExceeded";
    case ErrorCode::Cancelled:
        return "Cancelled";
    case ErrorCode::DeadlineExceeded:
        return "DeadlineExceeded";
    case ErrorCode::InvalidData:
        return "InvalidData";
    case ErrorCode::EmptySelection:
        return "EmptySelection";
    case ErrorCode::ExternalFailure:
        return "ExternalFailure";
    case ErrorCode::Internal:
        return "Internal";
    }
    return "UnknownErrorCode";
}

const char* errorCodeDescription(ErrorCode code) noexcept
{
    switch (code)
    {
    case ErrorCode::Ok:
        return "the operation succeeded";
    case ErrorCode::InvalidArgument:
        return "an argument is outside what this operation accepts";
    case ErrorCode::OutOfRange:
        return "an index, count or coordinate is outside the range this operation defines for it";
    case ErrorCode::IncompatibleArguments:
        return "the arguments are each acceptable and cannot be used together";
    case ErrorCode::Aliasing:
        return "buffers overlap where this operation requires disjoint storage";
    case ErrorCode::NotInitialized:
        return "this has not been initialised yet";
    case ErrorCode::AlreadyInitialized:
        return "this is already initialised, and initialising it again would discard state in use";
    case ErrorCode::InvalidState:
        return "this is not in a state that admits the call";
    case ErrorCode::Busy:
        return "work is still outstanding, so this call is refused until it finishes";
    case ErrorCode::Unsupported:
        return "this build does not support what was asked for";
    case ErrorCode::UnsupportedBackend:
        return "the requested backend is not available on this host or in this build";
    case ErrorCode::NotImplemented:
        return "this operation has no implementation yet";
    case ErrorCode::AllocationFailed:
        return "the allocator refused the request";
    case ErrorCode::MemoryBudgetExceeded:
        return "the request is within memory and outside the engine's configured memory budget";
    case ErrorCode::LimitExceeded:
        return "a configured maximum refused the request";
    case ErrorCode::Cancelled:
        return "the operation was cancelled and stopped at a checkpoint";
    case ErrorCode::DeadlineExceeded:
        return "the operation ran out of its time budget";
    case ErrorCode::InvalidData:
        return "the data is inconsistent, malformed, or describes something impossible";
    case ErrorCode::EmptySelection:
        return "nothing was selected for the operation to work on";
    case ErrorCode::ExternalFailure:
        return "something outside the engine failed";
    case ErrorCode::Internal:
        return "an invariant inside the engine did not hold";
    }
    return "an error code this build does not know";
}

const char* errorCategoryName(ErrorCategory category) noexcept
{
    switch (category)
    {
    case ErrorCategory::None:
        return "None";
    case ErrorCategory::Argument:
        return "Argument";
    case ErrorCategory::State:
        return "State";
    case ErrorCategory::Capability:
        return "Capability";
    case ErrorCategory::Resource:
        return "Resource";
    case ErrorCategory::Cancellation:
        return "Cancellation";
    case ErrorCategory::Data:
        return "Data";
    case ErrorCategory::External:
        return "External";
    case ErrorCategory::Internal:
        return "Internal";
    }
    return "UnknownErrorCategory";
}

const char* errorDomainName(ErrorDomain domain) noexcept
{
    switch (domain)
    {
    case ErrorDomain::Unspecified:
        return "Unspecified";
    case ErrorDomain::Core:
        return "Core";
    case ErrorDomain::Memory:
        return "Memory";
    case ErrorDomain::Runtime:
        return "Runtime";
    case ErrorDomain::Image:
        return "Image";
    case ErrorDomain::Algorithms:
        return "Algorithms";
    case ErrorDomain::Io:
        return "Io";
    case ErrorDomain::Interop:
        return "Interop";
    }
    return "UnknownErrorDomain";
}

} // namespace an
