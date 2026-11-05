// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/ArtifactIdentity.h>

#include <AfterNightEngine/IO/CodecError.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
an::CodecCard card(an::CodecNamespace origin, const char* keyword, const char* value)
{
    return *an::CodecCard::create(origin, keyword, value, "");
}

TEST(ArtifactIdentityTest, AcceptsTheCardSafeAlphabetAndRefusesEverythingElse)
{
    const auto identity = an::ArtifactIdentity::create("9fc0769b6ea4be30144c8d5060", "20260910_211405-481");
    ASSERT_TRUE(identity.hasValue()) << identity.status().message();
    EXPECT_EQ(identity->artifactId(), "9fc0769b6ea4be30144c8d5060");
    EXPECT_EQ(identity->runId(), "20260910_211405-481");
    const auto provenanceless = an::ArtifactIdentity::create("abc_DEF-123", "");
    ASSERT_TRUE(provenanceless.hasValue());
    EXPECT_TRUE(provenanceless->runId().empty());
    EXPECT_EQ(*provenanceless, *an::ArtifactIdentity::create("abc_DEF-123", ""));
    EXPECT_NE(*provenanceless, *identity);
    EXPECT_TRUE(an::ArtifactIdentity::create(std::string(an::kMaxArtifactIdentityChars, 'a'), "").hasValue());
    for (const auto& bad : {std::string{}, std::string(an::kMaxArtifactIdentityChars + 1u, 'a'),
                            std::string("with space"), std::string("quote'd"), std::string("slash/ed"),
                            std::string("\xc3\xa1"), std::string("tab\t"), std::string("nul\0x", 5u)})
    {
        const auto refused = an::ArtifactIdentity::create(bad, "");
        EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument) << bad;
        EXPECT_EQ(an::codecFailureReason(refused.status()), an::CodecFailure::InvalidIdentity) << bad;
        EXPECT_NE(refused.status().findContext(an::ContextKey::Parameter), nullptr);
    }
    EXPECT_EQ(an::ArtifactIdentity::create("ok", "bad run").status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::ArtifactIdentity::create("ok", std::string(an::kMaxArtifactIdentityChars + 1u, 'r')).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_TRUE(an::isArtifactIdentityChar('a'));
    EXPECT_TRUE(an::isArtifactIdentityChar('Z'));
    EXPECT_TRUE(an::isArtifactIdentityChar('0'));
    EXPECT_TRUE(an::isArtifactIdentityChar('_'));
    EXPECT_TRUE(an::isArtifactIdentityChar('-'));
    EXPECT_FALSE(an::isArtifactIdentityChar(' '));
    EXPECT_FALSE(an::isArtifactIdentityChar('\''));
    EXPECT_FALSE(an::isArtifactIdentityChar('/'));
    EXPECT_FALSE(an::isArtifactIdentityChar(static_cast<char>(0xe9)));
    EXPECT_TRUE(an::isArtifactIdentityKeyword("ANARTID"));
    EXPECT_TRUE(an::isArtifactIdentityKeyword("ANRUNID"));
    EXPECT_FALSE(an::isArtifactIdentityKeyword("anartid"));
    EXPECT_FALSE(an::isArtifactIdentityKeyword("ANARTID2"));
    EXPECT_EQ(an::kArtifactIdentityKeyword, "ANARTID");
    EXPECT_EQ(an::kRunIdentityKeyword, "ANRUNID");
    EXPECT_EQ(an::kMaxArtifactIdentityChars, 32u);
}

TEST(ArtifactIdentityTest, ReadsAStampFromPreservedCardsWithoutRepairingIt)
{
    const auto none = an::artifactIdentityOf({}, an::CodecNamespace::Fits);
    ASSERT_TRUE(none.hasValue());
    EXPECT_FALSE(none->has_value());
    // A run identity alone is provenance, not an artifact identity.
    const auto runOnly = *an::CodecMetadata::create({}, {card(an::CodecNamespace::Fits, "ANRUNID", "'run'")});
    ASSERT_TRUE(an::artifactIdentityOf(runOnly, an::CodecNamespace::Fits).hasValue());
    EXPECT_FALSE(an::artifactIdentityOf(runOnly, an::CodecNamespace::Fits)->has_value());
    const auto quoted =
        *an::CodecMetadata::create({}, {card(an::CodecNamespace::Fits, "OBJECT", "M42"),
                                        card(an::CodecNamespace::Fits, "ANARTID", "  '9fc0769b6ea4'  "),
                                        card(an::CodecNamespace::Fits, "ANRUNID", "'20260910_211405_481'")});
    const auto stamped = an::artifactIdentityOf(quoted, an::CodecNamespace::Fits);
    ASSERT_TRUE(stamped.hasValue()) << stamped.status().message();
    ASSERT_TRUE(stamped->has_value());
    EXPECT_EQ((*stamped)->artifactId(), "9fc0769b6ea4");
    EXPECT_EQ((*stamped)->runId(), "20260910_211405_481");
    const auto bare = *an::CodecMetadata::create({}, {card(an::CodecNamespace::Xisf, "ANARTID", "bare_id")});
    ASSERT_TRUE(an::artifactIdentityOf(bare, an::CodecNamespace::Xisf).hasValue());
    EXPECT_EQ((*an::artifactIdentityOf(bare, an::CodecNamespace::Xisf))->artifactId(), "bare_id");
    EXPECT_TRUE((*an::artifactIdentityOf(bare, an::CodecNamespace::Xisf))->runId().empty());
    // The namespace is half of the lookup: a FITS stamp is not an XISF stamp.
    EXPECT_FALSE(an::artifactIdentityOf(bare, an::CodecNamespace::Fits)->has_value());
    // A malformed stamp is reported, never normalized into a different identity.
    const auto malformed = *an::CodecMetadata::create({}, {card(an::CodecNamespace::Fits, "ANARTID", "'has space'")});
    const auto refused = an::artifactIdentityOf(malformed, an::CodecNamespace::Fits);
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(an::codecFailureReason(refused.status()), an::CodecFailure::InvalidIdentity);
    const auto tooLong = *an::CodecMetadata::create(
        {}, {card(an::CodecNamespace::Fits, "ANARTID", std::string(an::kMaxArtifactIdentityChars + 1u, 'a').c_str())});
    EXPECT_EQ(an::codecFailureReason(an::artifactIdentityOf(tooLong, an::CodecNamespace::Fits).status()),
              an::CodecFailure::InvalidIdentity);
    const auto emptyValue = *an::CodecMetadata::create({}, {card(an::CodecNamespace::Fits, "ANARTID", "''")});
    EXPECT_EQ(an::codecFailureReason(an::artifactIdentityOf(emptyValue, an::CodecNamespace::Fits).status()),
              an::CodecFailure::InvalidIdentity);
    const auto badRun = *an::CodecMetadata::create({}, {card(an::CodecNamespace::Fits, "ANARTID", "fine"),
                                                        card(an::CodecNamespace::Fits, "ANRUNID", "'bad run'")});
    EXPECT_EQ(an::codecFailureReason(an::artifactIdentityOf(badRun, an::CodecNamespace::Fits).status()),
              an::CodecFailure::InvalidIdentity);
}

TEST(ArtifactIdentityTest, StatesItselfAsCardsInTheDestinationVocabulary)
{
    const auto identity = *an::ArtifactIdentity::create("9fc0769b6ea4", "20260910_211405_481");
    const auto cards = an::artifactIdentityCards(identity, an::CodecNamespace::Fits);
    ASSERT_TRUE(cards.hasValue()) << cards.status().message();
    ASSERT_EQ(cards->size(), 2u);
    EXPECT_EQ((*cards)[0].origin(), an::CodecNamespace::Fits);
    EXPECT_EQ((*cards)[0].keyword(), "ANARTID");
    EXPECT_EQ((*cards)[0].value(), "9fc0769b6ea4");
    EXPECT_FALSE((*cards)[0].comment().empty());
    EXPECT_EQ((*cards)[1].keyword(), "ANRUNID");
    EXPECT_EQ((*cards)[1].value(), "20260910_211405_481");
    const auto readBack = an::artifactIdentityOf(*an::CodecMetadata::create({}, *cards), an::CodecNamespace::Fits);
    ASSERT_TRUE(readBack.hasValue() && readBack->has_value());
    EXPECT_EQ(**readBack, identity);
    const auto provenanceless =
        an::artifactIdentityCards(*an::ArtifactIdentity::create("only", ""), an::CodecNamespace::Xisf);
    ASSERT_TRUE(provenanceless.hasValue());
    ASSERT_EQ(provenanceless->size(), 1u);
    EXPECT_EQ(provenanceless->front().origin(), an::CodecNamespace::Xisf);
    EXPECT_EQ(an::artifactIdentityCards(identity, static_cast<an::CodecNamespace>(200u)).status().code(),
              an::ErrorCode::InvalidArgument);
}
} // namespace
