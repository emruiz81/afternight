// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/Provenance.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <span>
#include <string>
#include <utility>
#include <vector>

// Two rules in this file are worth their lines.
//
// **A source name beside an unknown origin is refused.** It reads as harmless and it is not: it
// produces a lineage that names a file while claiming not to know where the samples came from, and
// the two readings of that are "the file is the source" and "somebody left a field set". Refusing
// the combination means a name is always a claim the kind supports.
//
// **Appending is bounded and the bound is reported with the numbers.** A loop that appends an entry
// per iteration is a real mistake, and its failure mode without a bound is memory growth nobody
// attributes to metadata. With one, it is a refusal carrying the count and the limit, which names
// itself.

namespace an
{
namespace
{

/// The refusal for a field that cannot be what it claims.
///
/// @param message what is wrong, for a person.
/// @param parameter which field, spelled as the caller wrote it.
/// @param operation the engine operation to name in the failure.
/// @return an `ErrorCode::InvalidArgument` failure naming the field.
[[nodiscard]] Status refuseField(const char* message, const char* parameter, const char* operation)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Image,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

} // namespace

const char* provenanceSourceKindName(ProvenanceSourceKind kind) noexcept
{
    switch (kind)
    {
    case ProvenanceSourceKind::Unknown:
        return "Unknown";
    case ProvenanceSourceKind::File:
        return "File";
    case ProvenanceSourceKind::Generated:
        return "Generated";
    case ProvenanceSourceKind::Derived:
        return "Derived";
    }
    return "UnknownProvenanceSourceKind";
}

ProvenanceEntry::ProvenanceEntry(std::string operation, std::string detail)
    : m_operation(std::move(operation)), m_detail(std::move(detail))
{
}

Result<ProvenanceEntry> ProvenanceEntry::create(std::string operation, std::string detail)
{
    if (operation.empty())
    {
        return refuseField("a lineage entry has to say what was done", "operation", "ProvenanceEntry::create");
    }
    if (operation.size() > kMaxProvenanceTextBytes)
    {
        return refuseField("the operation name is longer than a lineage entry carries", "operation",
                           "ProvenanceEntry::create");
    }
    if (detail.size() > kMaxProvenanceTextBytes)
    {
        return refuseField("the detail is longer than a lineage entry carries", "detail", "ProvenanceEntry::create");
    }
    return ProvenanceEntry{std::move(operation), std::move(detail)};
}

const std::string& ProvenanceEntry::operation() const noexcept
{
    return m_operation;
}

const std::string& ProvenanceEntry::detail() const noexcept
{
    return m_detail;
}

Result<Provenance> Provenance::create(ProvenanceSourceKind kind, std::string sourceName)
{
    if (!isProvenanceSourceKind(kind))
    {
        return refuseField("the source kind is not one this engine knows", "kind", "Provenance::create");
    }
    if (sourceName.size() > kMaxProvenanceTextBytes)
    {
        return refuseField("the source name is longer than a lineage carries", "sourceName", "Provenance::create");
    }
    if (kind == ProvenanceSourceKind::Unknown && !sourceName.empty())
    {
        return refuseField("a source name states where the samples came from, which an unknown origin does not know",
                           "sourceName", "Provenance::create");
    }

    Provenance provenance;
    provenance.m_sourceKind = kind;
    provenance.m_sourceName = std::move(sourceName);
    return provenance;
}

ProvenanceSourceKind Provenance::sourceKind() const noexcept
{
    return m_sourceKind;
}

const std::string& Provenance::sourceName() const noexcept
{
    return m_sourceName;
}

std::span<const ProvenanceEntry> Provenance::entries() const noexcept
{
    return std::span<const ProvenanceEntry>{m_entries};
}

Result<Provenance> Provenance::appended(const ProvenanceEntry& entry) const
{
    if (m_entries.size() >= kMaxProvenanceEntries)
    {
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{.message = "this lineage already holds as many entries as one image records",
                         .operation = "Provenance::appended",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofCount(ContextKey::Completed,
                                                                 static_cast<std::uint64_t>(m_entries.size())),
                                     StatusContextField::ofCount(ContextKey::Limit,
                                                                 static_cast<std::uint64_t>(kMaxProvenanceEntries))}});
    }

    Provenance extended = *this;
    extended.m_entries.push_back(entry);
    return extended;
}

} // namespace an
