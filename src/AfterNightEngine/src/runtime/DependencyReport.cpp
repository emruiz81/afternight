// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Core/DependencyReport.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include "runtime/DependencyVersions.h"

#include <oneapi/tbb/version.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <string>

// The dependency report: the engine's half of the provenance record.
//
// Three properties are worth stating because none of them is visible in the answer.
//
// **The order is the report's own, not the order somebody wrote the calls in.** Entries ascend by
// name, so two reports diff cleanly - one from a Windows build against one from Linux, or one from
// this revision against one from the last - and a reader comparing them is comparing dependencies
// rather than line numbers.
//
// **There is no state.** The caller supplies the storage, every string that outlives the call is a
// literal in this binary or in a library that is already loaded, and nothing here is computed once
// and kept. Two threads asking at the same time share nothing, which is the property the process-wide
// regular-expression objects this replaces did not have.
//
// **An empty version text is an answer.** A library whose headers publish no version, or that
// exposes no run-time accessor for the version of its build, is reported with that text empty
// rather than with a number this engine invented. `build/scripts/tools/dependencies.txt`
// records which are empty and why, and its checker fails when the two sets stop agreeing.

namespace an
{
namespace
{

/// The operation name a refusal carries.
constexpr const char* kReportOperation = "an::engineDependencies";

/// True for a character that can be part of a version number.
bool isVersionCharacter(const char character) noexcept
{
    return (character >= '0' && character <= '9') || character == '.';
}

/// Reports oneTBB, the one library the runtime layer itself links.
///
/// It is here rather than behind the seam for the reason the seam exists: this file is already in
/// the layer that includes oneTBB, so there is nothing to hand across.
void reportOneTbb(DependencyVersion& entry) noexcept
{
    entry.name = "onetbb";
    entry.linkage = DependencyLinkage::SharedLibrary;
    dependency_detail::setVersionText(entry.compiled, TBB_VERSION_STRING);
    dependency_detail::setVersionTextFromBanner(entry.loaded, TBB_runtime_version(), nullptr);
}

/// Every contributor, in the order the report publishes them.
using Contributor = void (*)(DependencyVersion&) noexcept;

/// The report's table.
///
/// Ascending by the name each contributor writes, which `DependencyReportTest` pins so a new entry
/// inserted in the wrong place is a failing test rather than a report a reader has to sort.
constexpr std::array<Contributor, 13u> kContributors{dependency_detail::reportExiv2,
                                                     dependency_detail::reportFfmpeg,
                                                     dependency_detail::reportFftw,
                                                     dependency_detail::reportGsl,
                                                     dependency_detail::reportHighway,
                                                     dependency_detail::reportLibJpeg,
                                                     dependency_detail::reportLibPng,
                                                     dependency_detail::reportLibRaw,
                                                     dependency_detail::reportLibTiff,
                                                     dependency_detail::reportNlohmannJson,
                                                     reportOneTbb,
                                                     dependency_detail::reportOpenFits,
                                                     dependency_detail::reportOpenXisf};

} // namespace

namespace dependency_detail
{

void setVersionText(DependencyVersionText& text, const char* const value) noexcept
{
    text.text[0] = '\0';
    if (value == nullptr)
    {
        return;
    }
    std::size_t written = 0u;
    while (value[written] != '\0' && written + 1u < DependencyVersionText::kCapacity)
    {
        text.text[written] = value[written];
        ++written;
    }
    text.text[written] = '\0';
}

void setVersionTextFromBanner(DependencyVersionText& text, const char* const banner, const char* const marker) noexcept
{
    text.text[0] = '\0';
    if (banner == nullptr)
    {
        return;
    }

    const char* cursor = banner;
    if (marker != nullptr)
    {
        cursor = std::strstr(banner, marker);
        if (cursor == nullptr)
        {
            return;
        }
        cursor += std::strlen(marker);
    }
    while (*cursor != '\0' && (*cursor < '0' || *cursor > '9'))
    {
        ++cursor;
    }

    std::size_t written = 0u;
    while (isVersionCharacter(*cursor) && written + 1u < DependencyVersionText::kCapacity)
    {
        text.text[written] = *cursor;
        ++written;
        ++cursor;
    }
    // A version that ended on a separator - "4.7." from a banner that continues with a word - keeps
    // the number and drops the dangling dot, so the text is a version or nothing.
    while (written > 0u && text.text[written - 1u] == '.')
    {
        --written;
    }
    text.text[written] = '\0';
}

void setVersionTextFromParts(DependencyVersionText& text, const std::uint32_t majorVersion,
                             const std::uint32_t minorVersion, const std::uint32_t patchVersion) noexcept
{
    // Written digit by digit rather than through a formatting library, because this runs inside a
    // `noexcept` report that allocates nothing and the three numbers are bounded by construction.
    std::size_t written = 0u;
    const auto append = [&text, &written](const std::uint32_t value) noexcept {
        char digits[10]{};
        std::size_t count = 0u;
        std::uint32_t remaining = value;
        do
        {
            digits[count] = static_cast<char>('0' + (remaining % 10u));
            ++count;
            remaining /= 10u;
        } while (remaining != 0u && count < sizeof(digits));
        while (count > 0u && written + 1u < DependencyVersionText::kCapacity)
        {
            --count;
            text.text[written] = digits[count];
            ++written;
        }
    };

    append(majorVersion);
    if (written + 1u < DependencyVersionText::kCapacity)
    {
        text.text[written] = '.';
        ++written;
    }
    append(minorVersion);
    if (written + 1u < DependencyVersionText::kCapacity)
    {
        text.text[written] = '.';
        ++written;
    }
    append(patchVersion);
    text.text[written] = '\0';
}

} // namespace dependency_detail

const char* dependencyLinkageName(const DependencyLinkage linkage) noexcept
{
    switch (linkage)
    {
    case DependencyLinkage::HeaderOnly:
        return "header-only";
    case DependencyLinkage::StaticArchive:
        return "static archive";
    case DependencyLinkage::SharedLibrary:
        return "shared library";
    }
    return "unknown";
}

std::size_t engineDependencyCount() noexcept
{
    return kContributors.size();
}

Status engineDependencies(const std::span<DependencyVersion> out)
{
    if (out.size() != kContributors.size())
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{.message = "the output holds exactly one entry per reported dependency, which is " +
                                    std::to_string(kContributors.size()) + " in this build",
                         .operation = kReportOperation,
                         .domain = ErrorDomain::Core});
    }

    for (std::size_t index = 0u; index < kContributors.size(); ++index)
    {
        out[index] = DependencyVersion{};
        kContributors[index](out[index]);
    }
    return Status{};
}

} // namespace an
