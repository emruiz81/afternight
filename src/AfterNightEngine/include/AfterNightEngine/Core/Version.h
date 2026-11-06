// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The engine's version, and whether a caller's headers match the binary it
/// loaded.
///
/// Narrow on purpose. This engine and everything that links it are built from one checkout by one
/// set of scripts, and no packager ships a header, so the version is not a compatibility promise to
/// anyone outside the repository. What it is for is the failure that arrangement really does
/// produce: an object compiled against one revision of a public header, linked beside objects that
/// used the next one. The header states its own version as a compile-time constant and the library
/// reports the version it was built from, so the two can be compared.

#pragma once

#include <AfterNightEngine/Core/Api.h>

namespace an
{

/// Major version. Incremented when the public surface changes in a way that is not a superset of
/// the previous one.
inline constexpr int kEngineVersionMajor = 1;

/// Minor version. Incremented when the public surface gains something without losing anything.
inline constexpr int kEngineVersionMinor = 2;

/// Patch version. Incremented for a change that leaves the public surface as it was.
inline constexpr int kEngineVersionPatch = 0;

/// The three components spelled `major.minor.patch`, for a report or a log line.
///
/// Written out rather than assembled from the constants above, because assembling a string literal
/// from integers needs the preprocessor and this header is deliberately macro-free. A test asserts
/// that this spelling and those numbers agree, so the duplication cannot drift silently.
inline constexpr const char* kEngineVersionString = "1.2.0";

/// Generation of the engine's binary interface: the layout of its public types and the signatures
/// of its exported functions.
///
/// Incremented whenever a change would make an object compiled against an older public header
/// unsafe to link beside a newer one - a member added to a public struct, a parameter changed, an
/// enumerator renumbered. It is deliberately independent of the version triple, because a patch
/// release can break a layout and a minor release can leave every layout alone.
///
/// This engine promises nothing about binary compatibility across commits; consumers are rebuilt
/// with it. The number exists so that a mismatch is *detectable* rather than silent, not so that a
/// mismatch can be tolerated.
///
/// Adding a header or a type alone does not move this number, because an object compiled without
/// them is still correct about everything it does know; changing an existing layout, renumbering an
/// existing enumeration, or changing an exported signature does, because such an object is now
/// wrong about data it passes or reads. A virtual table a caller implements is a layout in this
/// sense. `build/scripts/tools/check_public_api.py` compares the published declarations with the
/// recorded surface and fails such a change while this number stays where it was.
inline constexpr int kEngineAbiGeneration = 2;

/// A three-part version number.
///
/// The members are not named `major`, `minor` and `patch`. Those are function-like macros in
/// glibc's `<sys/sysmacros.h>`, and a public header of this engine has no control over what a
/// consumer included before it - a header that stops compiling because of an unrelated include
/// earlier in the translation unit is a bad trade for three shorter names.
struct EngineVersion
{
    /// Major component. See `kEngineVersionMajor`.
    int majorVersion;

    /// Minor component. See `kEngineVersionMinor`.
    int minorVersion;

    /// Patch component. See `kEngineVersionPatch`.
    int patchVersion;

    /// Two versions are equal when all three components are equal. Ordering is deliberately not
    /// defined: "newer than" is not a question this engine answers, because it makes no
    /// compatibility promise across versions that such a comparison could support.
    friend constexpr bool operator==(const EngineVersion& left, const EngineVersion& right) noexcept = default;
};

/// Whether a consumer's public headers are compatible with the loaded engine binary.
///
/// Compatibility means exact agreement, because this is a version-locked in-tree C++ ABI rather
/// than a promise that one generation can consume another. A distinct ABI result is reported first:
/// layout disagreement makes even otherwise equal version numbers unsafe.
enum class EngineCompatibility
{
    /// Version and binary-interface generation both match exactly.
    Compatible,

    /// The version triple differs while the binary-interface generation agrees.
    VersionMismatch,

    /// Public type layout or exported signature generation differs.
    AbiMismatch
};

/// The version declared by the headers this translation unit was compiled against.
///
/// Evaluated in the caller, from the constants above, so the value describes the caller's headers
/// rather than the library's. Thread-safe, allocation-free, and usable in a constant expression.
///
/// @return the version triple the including headers declare.
[[nodiscard]] constexpr EngineVersion engineHeaderVersion() noexcept
{
    return EngineVersion{kEngineVersionMajor, kEngineVersionMinor, kEngineVersionPatch};
}

/// The version of the loaded library, as the headers it was built from declared it.
///
/// Thread-safe, reentrant, and allocation-free; it cannot fail, because the value is a
/// compile-time constant inside the library. Callable before any engine object exists.
///
/// @return the version triple the library was compiled from.
[[nodiscard]] AFTERNIGHTENGINE_API EngineVersion engineVersion() noexcept;

/// The loaded library's version spelled `major.minor.patch`.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @return a never-null pointer to a string literal with static storage duration compiled into
///         the library, so it outlives any caller and is never freed or mutated.
[[nodiscard]] AFTERNIGHTENGINE_API const char* engineVersionString() noexcept;

/// The binary-interface generation of the loaded library. See `kEngineAbiGeneration`.
///
/// Thread-safe, reentrant, and allocation-free; it cannot fail.
///
/// @return the generation the library was compiled from.
[[nodiscard]] AFTERNIGHTENGINE_API int engineAbiGeneration() noexcept;

/// Compare the caller's header constants with the loaded library.
///
/// The default arguments are evaluated in the caller, which is the essential part of the check: a
/// translation unit built from stale headers passes its stale constants to the current binary. A
/// result other than `EngineCompatibility::Compatible` is fatal; no other public type or function
/// may be trusted after a mismatch.
///
/// Thread-safe, reentrant, and allocation-free. Callable before any engine object exists.
///
/// @param headerVersion the version declared by the caller's headers.
/// @param headerAbiGeneration the ABI generation declared by the caller's headers.
/// @return exact compatibility, a version mismatch, or an ABI mismatch. ABI mismatch takes
///         precedence when both differ.
[[nodiscard]] AFTERNIGHTENGINE_API EngineCompatibility engineCompatibility(
    EngineVersion headerVersion = engineHeaderVersion(), int headerAbiGeneration = kEngineAbiGeneration) noexcept;

/// Whether the loaded library was built from the same public headers as the caller.
///
/// Defined inline so that the constants it compares against are the *caller's*, which is what
/// makes the comparison meaningful: a caller compiled against a stale header carries the stale
/// values into this function and the library does not.
///
/// A false result means the two were built from different headers, and nothing else about the
/// library can be trusted afterwards - a struct the caller passes may not have the layout the
/// library reads. Callers that check at all should check before their first other call, and treat
/// a false result as fatal rather than recoverable.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @return true when the library's version and binary-interface generation are both the ones
///         these headers declare; false when the two were built from different revisions.
[[nodiscard]] inline bool engineMatchesHeaders() noexcept
{
    return engineCompatibility() == EngineCompatibility::Compatible;
}

} // namespace an
