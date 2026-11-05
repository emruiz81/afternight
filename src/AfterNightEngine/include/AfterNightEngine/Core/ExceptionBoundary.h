// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Turning what an external library does when it fails into what this engine
/// reports when it fails.
///
/// The libraries this engine builds on disagree about how to say no. Some throw, some return a
/// status integer, some set a global, and one of them does two of those in different entry points.
/// None of that may travel: an exception crossing a kernel, a thread entry, a callback or a codec
/// boundary untranslated is the failure mode that makes an error path invisible at the call site,
/// and a library's own status integer read anywhere but beside its call is a number nobody can
/// interpret later.
///
/// So an adapter is the last place a library's own vocabulary exists. Everything above it sees an
/// `ErrorCode`, a message that was already written for a person, and the library's own detail as
/// typed context - which is the difference between a caller being able to act and a caller being
/// able to print.
///
/// The translation functions are `noexcept` and mean it. Building a detailed failure allocates, and
/// allocation is exactly what fails when the exception being translated is `std::bad_alloc`, so each
/// of them falls back to the allocation-free code-only failure rather than throwing out of a
/// translator. That fallback loses the message and keeps the code, which is the right way round.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstdint>
#include <exception>
#include <type_traits>
#include <utility>

namespace an
{

/// Where an external call is being made from, and on whose behalf.
///
/// Cheap to build at the call site and normally a file-scope constant beside the adapter that uses
/// it. Every failure translated through it carries these three facts, so a report can say which
/// library refused and which engine operation was asking without either of them being written into
/// a message.
struct ExternalBoundary
{
    /// The library being called - `"cfitsio"`, `"libtiff"`, `"opencv"`. Must be a string literal or
    /// otherwise outlive the call; it is copied into the failure, so it need not outlive the
    /// failure. Null is treated as an unnamed library.
    const char* library = nullptr;

    /// The engine operation making the call, spelled as it is in the source. Must outlive the call
    /// on the same terms as `library`. Null is treated as an unnamed operation.
    const char* operation = nullptr;

    /// Which part of the engine owns the adapter, for a diagnostic sink routing by subsystem.
    ErrorDomain domain = ErrorDomain::Interop;
};

/// The failure a `std::exception` caught at a boundary becomes.
///
/// `std::bad_alloc` becomes `ErrorCode::AllocationFailed` and everything else becomes
/// `ErrorCode::ExternalFailure`, so a single `catch (const std::exception&)` written by hand
/// classifies as well as the wrappers below do. The exception's `what()` becomes the message, and
/// the boundary's library name becomes a `ContextKey::Library` field.
///
/// Thread-safe. Allocates the failure's detail, and falls back to a code-only failure rather than
/// throwing if that allocation cannot be made.
///
/// @param boundary where the call was made and on whose behalf.
/// @param error the exception that escaped. Only read; nothing is retained.
/// @return a failure describing it, never a successful status.
[[nodiscard]] AFTERNIGHTENGINE_API Status translateException(const ExternalBoundary& boundary,
                                                             const std::exception& error) noexcept;

/// The failure an exception that is not a `std::exception` becomes.
///
/// What a `catch (...)` block has to report: the type and message are unreachable, so the failure
/// says that rather than pretending to know more.
///
/// Thread-safe. Allocates the failure's detail, and falls back to a code-only failure rather than
/// throwing if that allocation cannot be made.
///
/// @param boundary where the call was made and on whose behalf.
/// @return an `ErrorCode::ExternalFailure`, never a successful status.
[[nodiscard]] AFTERNIGHTENGINE_API Status translateUnknownException(const ExternalBoundary& boundary) noexcept;

/// The failure a library's own status code becomes.
///
/// The other half of what an adapter has to translate, for the libraries that report by returning a
/// number rather than by throwing. The number is kept as a `ContextKey::LibraryCode` field so that
/// a report can quote it, and it is never interpreted into engine semantics here - choosing the
/// `ErrorCode` is the adapter's job, because only the adapter knows what its library's numbers
/// mean.
///
/// Thread-safe. Allocates the failure's detail, and falls back to a code-only failure rather than
/// throwing if that allocation cannot be made.
///
/// @param boundary where the call was made and on whose behalf.
/// @param code what the library's refusal means in this engine's vocabulary. `ErrorCode::Ok` is not
///        a refusal and is reported as `ErrorCode::Internal`.
/// @param libraryCode the number the library returned, in its own numbering. Signed, because most
///        such numberings are.
/// @param message what the library said, already turned into text by the adapter, or null when it
///        said nothing. Copied, so a pointer into a scratch buffer is fine.
/// @return a failure carrying `code`, never a successful status.
[[nodiscard]] AFTERNIGHTENGINE_API Status translateExternalCode(const ExternalBoundary& boundary, ErrorCode code,
                                                                std::int64_t libraryCode, const char* message) noexcept;

/// Runs an external call that reports success by returning nothing, and translates what it throws.
///
/// The wrapper an adapter puts around a library entry point so that no exception leaves it:
///
/// @code
/// // The one place this library's own vocabulary exists.
/// constexpr an::ExternalBoundary kBoundary{
///     .library = "cfitsio", .operation = "FitsDecoder::open", .domain = an::ErrorDomain::Io};
///
/// an::Status FitsDecoder::open(const char* path)
/// {
///     int libraryStatus = 0;
///     const an::Status thrown =
///         an::callAcrossBoundary(kBoundary, [&] { fits_open_file(&m_file, path, 0, &libraryStatus); });
///     if (thrown.failed())
///     {
///         return thrown;
///     }
///     if (libraryStatus != 0)
///     {
///         return an::translateExternalCode(kBoundary, an::ErrorCode::InvalidData, libraryStatus,
///                                          errorText(libraryStatus));
///     }
///     return an::Status::success();
/// }
/// @endcode
///
/// Thread-safe, and as reentrant as the callable is. It allocates only what the failure's detail
/// needs.
///
/// @tparam CallableT anything invocable with no arguments and returning nothing.
/// @param boundary where the call is being made and on whose behalf.
/// @param callable the external call, invoked exactly once.
/// @return success when the callable returned; otherwise the translation of what it threw.
template <typename CallableT>
[[nodiscard]] Status callAcrossBoundary(const ExternalBoundary& boundary, CallableT&& callable) noexcept
{
    static_assert(std::is_void_v<std::invoke_result_t<CallableT&&>>,
                  "callAcrossBoundary is for a call that returns nothing; use evaluateAcrossBoundary for one that "
                  "produces a value");
    try
    {
        std::forward<CallableT>(callable)();
        return Status::success();
    }
    catch (const std::exception& error)
    {
        return translateException(boundary, error);
    }
    catch (...)
    {
        return translateUnknownException(boundary);
    }
}

/// Runs an external call that produces a value, and translates what it throws.
///
/// The value-returning counterpart of `callAcrossBoundary`. The callable's return type is deduced,
/// so an adapter names it nowhere.
///
/// Thread-safe, and as reentrant as the callable is. It allocates only what the failure's detail
/// and the value itself need.
///
/// @tparam CallableT anything invocable with no arguments, returning a type a `Result` can hold.
///         That type must be nothrow-movable: this function promises not to throw, and a move that
///         throws while the result is being returned could not be honoured.
/// @param boundary where the call is being made and on whose behalf.
/// @param callable the external call, invoked exactly once.
/// @return the produced value; otherwise the translation of what the callable threw.
template <typename CallableT>
[[nodiscard]] auto evaluateAcrossBoundary(const ExternalBoundary& boundary, CallableT&& callable) noexcept
    -> Result<std::invoke_result_t<CallableT&&>>
{
    using ValueType = std::invoke_result_t<CallableT&&>;
    static_assert(std::is_nothrow_move_constructible_v<ValueType>,
                  "a value crossing a translated boundary must be nothrow-movable, because this function promises "
                  "not to throw");
    try
    {
        return Result<ValueType>(std::forward<CallableT>(callable)());
    }
    catch (const std::exception& error)
    {
        return Result<ValueType>(translateException(boundary, error));
    }
    catch (...)
    {
        return Result<ValueType>(translateUnknownException(boundary));
    }
}

} // namespace an
