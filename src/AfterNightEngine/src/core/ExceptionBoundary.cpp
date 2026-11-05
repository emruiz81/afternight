// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Core/ExceptionBoundary.h>

#include <new>
#include <string>
#include <utility>

// The translators live out of line because every one of them allocates, and because keeping them
// here keeps `<new>` and the string handling out of every translation unit that includes the
// header. What stays in the header is the pair of templates, which have to be there: they wrap a
// caller's own callable.
//
// Each function catches everything it can throw and answers with the same code and no detail. That
// is not defensive programming for its own sake - the exception most likely to be crossing one of
// these boundaries is `std::bad_alloc`, and a translator that allocates in order to describe an
// allocation failure would throw out of a function that promised not to. Losing the message and
// keeping the code is the right way round: the code is what a caller acts on.

namespace an
{
namespace
{

// The detail every translated failure carries: what happened, which engine operation was asking,
// which part of the engine owns the adapter, and which library it was talking to. The library is a
// context field rather than part of the message because a caller may want to know which library
// refused without reading prose about it.
//
// Both pointers are permitted to be null, because a boundary constant with a field left out is a
// mistake worth reporting as a blank rather than as a crash inside a failure path.
StatusDetail boundaryDetail(const ExternalBoundary& boundary, const char* message)
{
    StatusDetail detail;
    if (message != nullptr)
    {
        detail.message = message;
    }
    if (boundary.operation != nullptr)
    {
        detail.operation = boundary.operation;
    }
    detail.domain = boundary.domain;
    if (boundary.library != nullptr)
    {
        detail.context.push_back(StatusContextField::ofText(ContextKey::Library, boundary.library));
    }
    return detail;
}

} // namespace

Status translateException(const ExternalBoundary& boundary, const std::exception& error) noexcept
{
    // The one classification this layer makes on its own. An out-of-memory condition is a resource
    // failure whichever side of the boundary it happened on, and a caller that retries smaller acts
    // on it differently from one that gave the library bad input - so it must not be flattened into
    // a generic external failure. The test is on the object rather than on the catch clause so that
    // an adapter writing a single `catch (const std::exception&)` by hand classifies as well as the
    // wrappers do.
    const ErrorCode code = dynamic_cast<const std::bad_alloc*>(&error) != nullptr ? ErrorCode::AllocationFailed
                                                                                  : ErrorCode::ExternalFailure;
    try
    {
        return Status::failure(code, boundaryDetail(boundary, error.what()));
    }
    catch (...)
    {
        return Status::failure(code);
    }
}

Status translateUnknownException(const ExternalBoundary& boundary) noexcept
{
    try
    {
        return Status::failure(
            ErrorCode::ExternalFailure,
            boundaryDetail(boundary, "the external library threw an object that is not a std::exception"));
    }
    catch (...)
    {
        return Status::failure(ErrorCode::ExternalFailure);
    }
}

Status translateExternalCode(const ExternalBoundary& boundary, ErrorCode code, std::int64_t libraryCode,
                             const char* message) noexcept
{
    try
    {
        StatusDetail detail = boundaryDetail(boundary, message);
        detail.context.push_back(StatusContextField::ofNumber(ContextKey::LibraryCode, libraryCode));
        return Status::failure(code, std::move(detail));
    }
    catch (...)
    {
        return Status::failure(code);
    }
}

} // namespace an
