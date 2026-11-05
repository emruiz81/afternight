// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Which third-party libraries are inside this binary, and at which versions.
///
/// A provenance record that lives only in a document describes the tree it was written against. This
/// is the half of it the binary can answer for itself: the libraries the loaded engine contains, the
/// version each one's headers declared when this engine was compiled, and the version each loaded
/// copy reports about itself. `build/scripts/tools/dependencies.txt` is the other half - the source,
/// licence, upstream, security channel, build and deploy owner and adapter boundary of each - and
/// `check_dependency_provenance.py` is what keeps the two naming the same set.
///
/// The two versions are separate members on purpose, because they answer different questions. What
/// the headers declared is exact, fixed at compile time, and the number a build is reproducible
/// from. What the loaded library reports is the number an advisory applies to, and on a platform
/// where the library is a shared object it can be a different number entirely - a system update, a
/// deployment that copied one file and not another, or a process that had already loaded another
/// copy. A report that answered from a compile-time macro for one library and from a run-time
/// accessor for the next would leave a caller no way to tell which kind of answer it had been given.
///
/// Nothing here is parsed by a caller. A version is text for a person and for a run record; code
/// that needs to branch on a library's capabilities asks `Runtime/EngineCapabilities.h` what this
/// build can do.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace an
{

/// How this binary reaches one of its third-party libraries.
///
/// The distinction is what explains a difference between the two versions a dependency reports. A
/// header-only library has no loaded copy at all, so its compiled version is the whole answer; a
/// static archive was fixed inside this binary at link time and cannot be replaced without
/// rebuilding it; a shared library is resolved when the process starts and is the one kind whose
/// loaded version can differ from the headers this engine was compiled against.
enum class DependencyLinkage : std::uint8_t
{
    /// Compiled into this binary from headers alone; there is no library file to load.
    HeaderOnly,

    /// Linked from a static archive, so the copy inside this binary is fixed at build time.
    StaticArchive,

    /// Resolved from a shared library when the process starts.
    SharedLibrary,
};

/// What a linkage is called, for a report a person reads.
///
/// Thread-safe, reentrant, and allocation-free; it cannot fail.
///
/// @param linkage the linkage to name. A value outside the enumeration is named as unknown rather
///        than treated as a defect, because this is reporting code and a caller that built one
///        from an integer should get a printable answer.
/// @return a never-null pointer to a string literal with static storage duration compiled into the
///         library, so it outlives every caller and is never freed or mutated.
[[nodiscard]] AFTERNIGHTENGINE_API const char* dependencyLinkageName(DependencyLinkage linkage) noexcept;

/// One dependency version as text.
///
/// Inline storage rather than a pointer, and that is a decision rather than a convenience. A
/// version that has to be assembled - a library that reports numbers instead of a string, or one
/// whose run-time accessor returns a banner a version has to be read out of - needs somewhere to
/// live, and the two candidates for that are an allocation and a buffer with process lifetime.
/// This engine has neither: the caller's own array is where the report lands.
///
/// Always terminated within its capacity, so it may be read as a C string. Empty means the
/// dependency reports no version of this kind, which is an ordinary answer for either kind.
struct DependencyVersionText
{
    /// How many bytes one version text holds, terminator included.
    ///
    /// Sized for the longest form any dependency here reports with room to spare; a version that
    /// did not fit is truncated at a whole character rather than overrunning, and the truncation
    /// is visible because the text stops.
    static constexpr std::size_t kCapacity = 32u;

    /// The version, NUL-terminated. Empty when there is none to report.
    char text[kCapacity]{};
};

/// One third-party library inside this binary.
///
/// A plain value: copying one copies the text and borrows the name, and two threads may read
/// copies of one concurrently. Every member carries a default, so a caller may build one with
/// designated initialisers - in a test double, or in a report - without naming the rest.
struct DependencyVersion
{
    /// The register's name for this dependency - `"onetbb"`, `"libtiff"` - spelled exactly as
    /// `build/scripts/tools/dependencies.txt` spells it, which is what lets a reader join the two.
    ///
    /// Points at a string literal with static storage duration compiled into the library, so it
    /// outlives every caller and is never freed or mutated. Null only in an entry the engine did
    /// not write.
    const char* name = nullptr;

    /// How this binary reaches the library.
    DependencyLinkage linkage = DependencyLinkage::StaticArchive;

    /// The version the library's headers declared when this engine was compiled, or empty when they
    /// declare none.
    ///
    /// Empty for a library whose headers carry no version - FFTW publishes none - and never invented
    /// in its place.
    DependencyVersionText compiled{};

    /// The version the loaded library reports about itself, or empty when it reports none.
    ///
    /// Empty is an answer rather than a gap, and there are two reasons for it: a header-only
    /// library has no loaded copy, and a library that exposes no version accessor cannot be asked.
    /// A value that differs from `compiled` means the copy this process loaded is not the copy this
    /// engine was compiled against, which is a fact worth having in a run record.
    DependencyVersionText loaded{};
};

/// How many dependencies this binary reports.
///
/// Fixed for the life of the process: it is a property of how this engine was built, so two calls
/// in one run answer the same number and the answer is what `engineDependencies` requires its span
/// to hold. Thread-safe, reentrant, and allocation-free; it cannot fail. Callable before any engine
/// object exists.
///
/// @return the number of dependencies the report contains. Never zero: this engine links at least
///         a parallelism library and a vector abstraction in every configuration.
[[nodiscard]] AFTERNIGHTENGINE_API std::size_t engineDependencyCount() noexcept;

/// Writes this binary's dependency report into a caller-owned span.
///
/// The caller supplies the storage because the engine owns no state with process lifetime, and a
/// report of fixed-size text has to live somewhere. The span must hold exactly
/// `engineDependencyCount()` entries: a shorter one would report part of the truth as if it were
/// all of it, and a longer one would leave entries a reader cannot tell apart from dependencies
/// that reported nothing.
///
/// Entries are written in ascending order of name, so two reports from one binary compare directly
/// and a report from two builds diffs cleanly. Nothing is written when the span is refused.
///
/// Thread-safe and reentrant: the report is assembled from constants and from accessors of
/// libraries that are already loaded, and no two callers share anything. Allocates only to describe
/// a refused span.
///
/// @param out where the report is written; must hold exactly `engineDependencyCount()` entries.
///        Every entry is overwritten, so its previous contents need not be initialised.
/// @return success, or `ErrorCode::IncompatibleArguments` when the span is not exactly the size of
///         the report.
[[nodiscard]] AFTERNIGHTENGINE_API Status engineDependencies(std::span<DependencyVersion> out);

} // namespace an
