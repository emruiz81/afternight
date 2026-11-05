// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the artifact and run identities a generated file states in its own header.
//
// An identity is checked where it is created and again where it is read back from a card, and never
// repaired in between: a value outside the card-safe alphabet or longer than one card holds is a
// failure, not something to normalize, because normalizing could make two identities one.

#include <AfterNightEngine/IO/ArtifactIdentity.h>

#include <AfterNightEngine/IO/CodecError.h>

#include <string>
#include <utility>

namespace an
{
namespace
{
Status identityFailure(ErrorCode code, const char* operation, const char* message, const char* parameter)
{
    return Status::failure(
        code, {.message = message,
               .operation = operation,
               .domain = ErrorDomain::Io,
               .context = {StatusContextField::ofCount(ContextKey::CodecReason,
                                                       static_cast<std::uint64_t>(CodecFailure::InvalidIdentity)),
                           StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

bool identityText(std::string_view text, bool required) noexcept
{
    if (text.empty())
        return !required;
    if (text.size() > kMaxArtifactIdentityChars)
        return false;
    for (const char value : text)
        if (!isArtifactIdentityChar(value))
            return false;
    return true;
}

bool blank(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

std::string_view trimmed(std::string_view text) noexcept
{
    while (!text.empty() && blank(text.front()))
        text.remove_prefix(1u);
    while (!text.empty() && blank(text.back()))
        text.remove_suffix(1u);
    return text;
}

/// The value as a file wrote it: surrounding whitespace and one pair of single quotes removed, and
/// the blanks a FITS string pads its value with removed again inside them.
std::string_view cardText(std::string_view value) noexcept
{
    value = trimmed(value);
    if (value.size() >= 2u && value.front() == '\'' && value.back() == '\'')
        value = trimmed(value.substr(1u, value.size() - 2u));
    return value;
}
} // namespace

bool isArtifactIdentityKeyword(std::string_view keyword) noexcept
{
    return keyword == kArtifactIdentityKeyword || keyword == kRunIdentityKeyword;
}

Result<ArtifactIdentity> ArtifactIdentity::create(std::string artifactId, std::string runId)
{
    if (!identityText(artifactId, true))
        return identityFailure(ErrorCode::InvalidArgument, "ArtifactIdentity::create",
                               "an artifact identity is nonempty, at most 32 bytes, and spelled with letters, digits, "
                               "underscore and hyphen",
                               "artifactId");
    if (!identityText(runId, false))
        return identityFailure(ErrorCode::InvalidArgument, "ArtifactIdentity::create",
                               "a run identity is at most 32 bytes and spelled with letters, digits, underscore and "
                               "hyphen",
                               "runId");
    return ArtifactIdentity(std::move(artifactId), std::move(runId));
}

ArtifactIdentity::ArtifactIdentity(std::string artifactId, std::string runId)
    : m_artifactId(std::move(artifactId)), m_runId(std::move(runId))
{
}

const std::string& ArtifactIdentity::artifactId() const noexcept
{
    return m_artifactId;
}

const std::string& ArtifactIdentity::runId() const noexcept
{
    return m_runId;
}

Result<std::optional<ArtifactIdentity>> artifactIdentityOf(const CodecMetadata& codec, CodecNamespace origin)
{
    const CodecCard* artifact = codec.find(origin, kArtifactIdentityKeyword);
    if (artifact == nullptr)
        return std::optional<ArtifactIdentity>{};
    const CodecCard* run = codec.find(origin, kRunIdentityKeyword);
    const std::string_view artifactText = cardText(artifact->value());
    const std::string_view runText = run != nullptr ? cardText(run->value()) : std::string_view{};
    if (!identityText(artifactText, true))
        return identityFailure(ErrorCode::InvalidData, "artifactIdentityOf",
                               "the artifact identity card carries a value that is not an identity", "ANARTID");
    if (!identityText(runText, false))
        return identityFailure(ErrorCode::InvalidData, "artifactIdentityOf",
                               "the run identity card carries a value that is not an identity", "ANRUNID");
    Result<ArtifactIdentity> identity = ArtifactIdentity::create(std::string(artifactText), std::string(runText));
    if (!identity.hasValue())
        return identity.status();
    return std::optional<ArtifactIdentity>(std::move(*identity));
}

Result<std::vector<CodecCard>> artifactIdentityCards(const ArtifactIdentity& identity, CodecNamespace origin)
{
    if (!isCodecNamespace(origin))
        return Status::failure(ErrorCode::InvalidArgument,
                               {.message = "an identity is stated in a namespace this build knows",
                                .operation = "artifactIdentityCards",
                                .domain = ErrorDomain::Io,
                                .context = {StatusContextField::ofText(ContextKey::Parameter, "origin")}});
    std::vector<CodecCard> cards;
    Result<CodecCard> artifact = CodecCard::create(origin, std::string(kArtifactIdentityKeyword), identity.artifactId(),
                                                   "manifest artifact identity this file satisfies");
    if (!artifact.hasValue())
        return artifact.status();
    cards.push_back(std::move(*artifact));
    if (!identity.runId().empty())
    {
        Result<CodecCard> run = CodecCard::create(origin, std::string(kRunIdentityKeyword), identity.runId(),
                                                  "execution that produced this file; provenance only");
        if (!run.hasValue())
            return run.status();
        cards.push_back(std::move(*run));
    }
    return cards;
}
} // namespace an
