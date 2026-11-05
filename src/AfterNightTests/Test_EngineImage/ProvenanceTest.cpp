// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/Provenance.h.
//
// A lineage is only worth keeping if it is complete and cannot be quietly edited, so the cases here
// are about the two ways that could stop being true.
//
// **Appending returns a second lineage.** A lineage handed to something else cannot grow behind its
// holder's back, and the case below holds both and checks that the older one is unchanged.
//
// **The bound is a refusal rather than growth.** A loop that appends per iteration is a real
// mistake, and without a bound its failure mode is memory nobody attributes to metadata.

#include <AfterNightEngine/Image/Provenance.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <utility>

namespace
{

/// An entry that is valid, for cases that are about something else.
///
/// @param operation what was done.
/// @return the entry.
[[nodiscard]] an::ProvenanceEntry makeEntry(std::string operation)
{
    an::Result<an::ProvenanceEntry> entry = an::ProvenanceEntry::create(std::move(operation), "");
    EXPECT_TRUE(entry.hasValue());
    return entry.hasValue() ? std::move(*entry) : an::ProvenanceEntry::create("fallback", "").value();
}

TEST(ProvenanceTest, ALineageThatClaimsNothingIsTheDefault)
{
    const an::Provenance provenance;
    EXPECT_EQ(provenance.sourceKind(), an::ProvenanceSourceKind::Unknown);
    EXPECT_TRUE(provenance.sourceName().empty());
    EXPECT_TRUE(provenance.entries().empty());
}

TEST(ProvenanceTest, AFileSourceCarriesWhatIdentifiesIt)
{
    an::Result<an::Provenance> provenance =
        an::Provenance::create(an::ProvenanceSourceKind::File, "E:/lights/frame_0001.fit");
    ASSERT_TRUE(provenance.hasValue()) << provenance.status().message();

    EXPECT_EQ(provenance->sourceKind(), an::ProvenanceSourceKind::File);
    EXPECT_EQ(provenance->sourceName(), "E:/lights/frame_0001.fit");
    EXPECT_TRUE(provenance->entries().empty());
}

TEST(ProvenanceTest, ANameBesideAnUnknownOriginIsRefused)
{
    an::Result<an::Provenance> refused = an::Provenance::create(an::ProvenanceSourceKind::Unknown, "somewhere");
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
    const an::StatusContextField* const field = refused.status().findContext(an::ContextKey::Parameter);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->text, "sourceName");

    EXPECT_TRUE(an::Provenance::create(an::ProvenanceSourceKind::Unknown, "").hasValue());
}

TEST(ProvenanceTest, AnOriginOutsideTheEnumerationIsRefused)
{
    an::Result<an::Provenance> refused = an::Provenance::create(static_cast<an::ProvenanceSourceKind>(200), "");
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
}

TEST(ProvenanceTest, AnEntryHasToSayWhatWasDone)
{
    an::Result<an::ProvenanceEntry> refused = an::ProvenanceEntry::create("", "some detail");
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);

    an::Result<an::ProvenanceEntry> accepted = an::ProvenanceEntry::create("calibrate", "");
    ASSERT_TRUE(accepted.hasValue()) << accepted.status().message();
    EXPECT_EQ(accepted->operation(), "calibrate");
    EXPECT_TRUE(accepted->detail().empty());
}

TEST(ProvenanceTest, TextLongerThanALineageCarriesIsRefusedRatherThanTruncated)
{
    const std::string tooLong(an::kMaxProvenanceTextBytes + 1u, 'x');

    EXPECT_FALSE(an::ProvenanceEntry::create(tooLong, "").hasValue());
    EXPECT_FALSE(an::ProvenanceEntry::create("decode", tooLong).hasValue());
    EXPECT_FALSE(an::Provenance::create(an::ProvenanceSourceKind::File, tooLong).hasValue());
}

TEST(ProvenanceTest, AppendingProducesASecondLineageAndLeavesTheFirstAlone)
{
    an::Result<an::Provenance> original = an::Provenance::create(an::ProvenanceSourceKind::File, "frame.fit");
    ASSERT_TRUE(original.hasValue()) << original.status().message();

    an::Result<an::Provenance> extended = original->appended(makeEntry("calibrate"));
    ASSERT_TRUE(extended.hasValue()) << extended.status().message();

    EXPECT_TRUE(original->entries().empty());
    ASSERT_EQ(extended->entries().size(), 1u);
    EXPECT_EQ(extended->entries()[0].operation(), "calibrate");
    EXPECT_EQ(extended->sourceKind(), an::ProvenanceSourceKind::File);
    EXPECT_EQ(extended->sourceName(), "frame.fit");
}

TEST(ProvenanceTest, EntriesStayInTheOrderTheyHappened)
{
    an::Result<an::Provenance> provenance = an::Provenance::create(an::ProvenanceSourceKind::File, "frame.fit");
    ASSERT_TRUE(provenance.hasValue());

    for (const char* const step : {"calibrate", "demosaic", "register"})
    {
        an::Result<an::Provenance> extended = provenance->appended(makeEntry(step));
        ASSERT_TRUE(extended.hasValue()) << step;
        provenance = std::move(extended);
    }

    const std::span<const an::ProvenanceEntry> entries = provenance->entries();
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].operation(), "calibrate");
    EXPECT_EQ(entries[1].operation(), "demosaic");
    EXPECT_EQ(entries[2].operation(), "register");
}

TEST(ProvenanceTest, ALossyDecodeAcknowledgementTravelsWithTheData)
{
    // The case the sample-type set exists for: a file whose samples cannot be carried exactly opens
    // only when a caller says so, and the acknowledgement has to live with the image rather than
    // with whoever ran the import.
    an::Result<an::ProvenanceEntry> entry =
        an::ProvenanceEntry::create("decode", "precision loss acknowledged by the caller");
    ASSERT_TRUE(entry.hasValue()) << entry.status().message();

    an::Result<an::Provenance> provenance = an::Provenance::create(an::ProvenanceSourceKind::File, "wide.fit");
    ASSERT_TRUE(provenance.hasValue());
    an::Result<an::Provenance> recorded = provenance->appended(*entry);
    ASSERT_TRUE(recorded.hasValue()) << recorded.status().message();

    ASSERT_EQ(recorded->entries().size(), 1u);
    EXPECT_EQ(recorded->entries()[0].detail(), "precision loss acknowledged by the caller");
}

TEST(ProvenanceTest, AFullLineageRefusesWithTheCountAndTheBound)
{
    an::Result<an::Provenance> provenance = an::Provenance::create(an::ProvenanceSourceKind::Generated, "synthetic");
    ASSERT_TRUE(provenance.hasValue());

    const an::ProvenanceEntry entry = makeEntry("step");
    for (std::size_t index = 0u; index < an::kMaxProvenanceEntries; ++index)
    {
        an::Result<an::Provenance> extended = provenance->appended(entry);
        ASSERT_TRUE(extended.hasValue()) << index;
        provenance = std::move(extended);
    }
    EXPECT_EQ(provenance->entries().size(), an::kMaxProvenanceEntries);

    an::Result<an::Provenance> refused = provenance->appended(entry);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::LimitExceeded);

    const an::StatusContextField* const completed = refused.status().findContext(an::ContextKey::Completed);
    ASSERT_NE(completed, nullptr);
    EXPECT_EQ(completed->count, an::kMaxProvenanceEntries);
    const an::StatusContextField* const limit = refused.status().findContext(an::ContextKey::Limit);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(limit->count, an::kMaxProvenanceEntries);
}

TEST(ProvenanceTest, TwoLineagesAreEqualExactlyWhenTheirHistoriesAre)
{
    an::Result<an::Provenance> left = an::Provenance::create(an::ProvenanceSourceKind::File, "frame.fit");
    an::Result<an::Provenance> right = an::Provenance::create(an::ProvenanceSourceKind::File, "frame.fit");
    ASSERT_TRUE(left.hasValue());
    ASSERT_TRUE(right.hasValue());
    EXPECT_EQ(*left, *right);

    an::Result<an::Provenance> extended = left->appended(makeEntry("calibrate"));
    ASSERT_TRUE(extended.hasValue());
    EXPECT_NE(*extended, *right);

    EXPECT_EQ(makeEntry("calibrate"), makeEntry("calibrate"));
    EXPECT_NE(makeEntry("calibrate"), makeEntry("register"));
}

TEST(ProvenanceTest, EveryOriginHasItsOwnNameAndAStrangerSaysSo)
{
    constexpr an::ProvenanceSourceKind kAll[] = {an::ProvenanceSourceKind::Unknown, an::ProvenanceSourceKind::File,
                                                 an::ProvenanceSourceKind::Generated,
                                                 an::ProvenanceSourceKind::Derived};
    for (const an::ProvenanceSourceKind left : kAll)
    {
        EXPECT_TRUE(an::isProvenanceSourceKind(left));
        for (const an::ProvenanceSourceKind right : kAll)
        {
            const bool sameName =
                std::strcmp(an::provenanceSourceKindName(left), an::provenanceSourceKindName(right)) == 0;
            EXPECT_EQ(sameName, left == right);
        }
    }
    EXPECT_FALSE(an::isProvenanceSourceKind(static_cast<an::ProvenanceSourceKind>(200)));
    EXPECT_STREQ(an::provenanceSourceKindName(static_cast<an::ProvenanceSourceKind>(200)),
                 "UnknownProvenanceSourceKind");
}

} // namespace
