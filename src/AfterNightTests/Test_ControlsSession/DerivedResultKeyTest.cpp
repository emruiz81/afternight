// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The derived-result key: a result is current for exactly the document, asset revision, secondary inputs, selection,
/// parameters and presentation policy it was made from. A write, a transition, a mask change or a replacement makes a
/// cached result stale; reading, renaming and presenting differently do not move the document's part of it; and an
/// asset replaced by one whose generations coincide cannot revive what was derived from the old.

#include "ControlsSessionTestSupport.h"

#include "session/DerivedResultKey.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Image/ValidityMask.h>

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace
{

using an::consumer::AssetRevision;
using an::consumer::ImageAsset;
using an::tests::controls::filledAsset;
using an::tests::controls::filledDocument;
using an::tests::controls::openProvider;
using an::tests::controls::writeSample;
using an::ui::DerivedResultKey;
using an::ui::DerivedResultKeyHash;
using an::ui::DocumentNames;
using an::ui::DocumentPresentation;
using an::ui::ImageDocument;
using an::ui::ImageDocumentWriter;

/// The key of a histogram of the whole image, with the parameters a histogram control would state.
[[nodiscard]] DerivedResultKey histogramKey(const ImageDocument& document)
{
    return an::ui::derivedResultKey(document.revision(), {}, {}, "bins=4096;logarithmic=0");
}

} // namespace

TEST(DerivedResultKeyTest, AKeyIsMadeOfEveryInputItNames)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
    std::optional<ImageDocument> mask = filledDocument(provider->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    ASSERT_TRUE(mask.has_value());

    const DerivedResultKey key = an::ui::derivedResultKey(document->revision(), {mask->revision().asset},
                                                          "region=0,0,4,4;channel=whole", "amount=0.5", "stf=linked");
    EXPECT_EQ(key.documentIdentity, document->identity());
    EXPECT_EQ(key.asset, document->revision().asset);
    ASSERT_EQ(key.secondaryRevisions.size(), 1u);
    EXPECT_EQ(key.secondaryRevisions.front(), mask->revision().asset);
    EXPECT_EQ(key.selection, "region=0,0,4,4;channel=whole");
    EXPECT_EQ(key.parameters, "amount=0.5");
    EXPECT_EQ(key.presentationPolicy, "stf=linked");

    const DerivedResultKey same = an::ui::derivedResultKey(document->revision(), {mask->revision().asset},
                                                           "region=0,0,4,4;channel=whole", "amount=0.5", "stf=linked");
    EXPECT_EQ(key, same);
    EXPECT_EQ(DerivedResultKeyHash{}(key), DerivedResultKeyHash{}(same));

    std::vector<DerivedResultKey> different(6u, key);
    different[0].documentIdentity = mask->identity();
    different[1].asset.pixelGeneration += 1u;
    different[2].secondaryRevisions.clear();
    different[3].selection = "region=0,0,4,5;channel=whole";
    different[4].parameters = "amount=0.6";
    different[5].presentationPolicy = "stf=unlinked";
    for (const DerivedResultKey& other : different)
    {
        EXPECT_FALSE(other == key);
    }
}

TEST(DerivedResultKeyTest, ReadingRenamingAndPresentingLeaveTheDocumentsPartAlone)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    const DerivedResultKey before = histogramKey(*document);
    {
        an::Result<an::consumer::AssetReadLease> lease = document->read();
        ASSERT_TRUE(lease.hasValue());
    }
    document->setNames(DocumentNames{.displayName = "renamed"});
    document->setPresentation(DocumentPresentation{.channel = an::ChannelScope::Blue});
    EXPECT_EQ(histogramKey(*document), before);
}

TEST(DerivedResultKeyTest, AWriteATransitionAndAMaskChangeEachMakeTheKeyStale)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());

    const DerivedResultKey original = histogramKey(*document);
    {
        an::Result<ImageDocumentWriter> writer = document->write();
        ASSERT_TRUE(writer.hasValue());
        (void)writer->asset().write<float>();
    }
    const DerivedResultKey written = histogramKey(*document);
    EXPECT_NE(written, original) << "a write that changed no sample still moves the pixel generation";
    {
        an::Result<ImageDocumentWriter> writer = document->write();
        ASSERT_TRUE(writer.hasValue());
        ASSERT_TRUE(writer->asset().transition(an::tests::controls::linearTransition()).ok());
    }
    const DerivedResultKey transitioned = histogramKey(*document);
    EXPECT_NE(transitioned, written);
    {
        an::Result<ImageDocumentWriter> writer = document->write();
        ASSERT_TRUE(writer.hasValue());
        an::Result<an::ValidityMask> mask = an::tests::controls::allValidMask(session, 8u, 4u);
        ASSERT_TRUE(mask.hasValue()) << mask.status().message();
        ASSERT_TRUE(writer->asset().setValidity(std::move(*mask)).ok());
    }
    const DerivedResultKey masked = histogramKey(*document);
    EXPECT_NE(masked, transitioned);
    EXPECT_EQ(masked.asset.validityGeneration, transitioned.asset.validityGeneration + 1u);
}

TEST(DerivedResultKeyTest, AReplacementWithCoincidentGenerationsCannotReviveAnOldResult)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());

    std::unordered_map<DerivedResultKey, std::string, DerivedResultKeyHash> cache;
    const DerivedResultKey original = histogramKey(*document);
    cache.emplace(original, "histogram of the original");

    std::optional<ImageAsset> replacement = filledAsset(session, 2.0f);
    ASSERT_TRUE(replacement.has_value());
    const AssetRevision fresh = replacement->revision();
    ASSERT_EQ(fresh.pixelGeneration, original.asset.pixelGeneration)
        << "the case needs a replacement whose generations coincide with the original's";
    ASSERT_EQ(fresh.semanticGeneration, original.asset.semanticGeneration);
    ASSERT_EQ(fresh.validityGeneration, original.asset.validityGeneration);
    {
        an::Result<ImageDocumentWriter> writer = document->write();
        ASSERT_TRUE(writer.hasValue());
        ASSERT_TRUE(writer->replace(std::move(*replacement)).ok());
    }
    const DerivedResultKey replaced = histogramKey(*document);
    EXPECT_EQ(replaced.documentIdentity, original.documentIdentity);
    EXPECT_NE(replaced, original);
    EXPECT_EQ(cache.find(replaced), cache.end()) << "a result of the replaced asset was served for its replacement";
    EXPECT_NE(cache.find(original), cache.end());
}

TEST(DerivedResultKeyTest, RestoringASnapshotIsANewIdentityToo)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    const DerivedResultKey before = histogramKey(*document);

    an::Result<ImageAsset> history = document->snapshot();
    ASSERT_TRUE(history.hasValue()) << history.status().message();
    {
        an::Result<ImageDocumentWriter> writer = document->write();
        ASSERT_TRUE(writer.hasValue());
        writeSample(writer->asset(), 0u, 0u, 5.0f);
    }
    an::Result<ImageAsset> restored = history->snapshot();
    ASSERT_TRUE(restored.hasValue()) << restored.status().message();
    {
        an::Result<ImageDocumentWriter> writer = document->write();
        ASSERT_TRUE(writer.hasValue());
        ASSERT_TRUE(writer->replace(std::move(*restored)).ok());
    }
    const DerivedResultKey after = histogramKey(*document);
    EXPECT_EQ(an::tests::controls::documentSampleAt(*document, 0u, 0u), 1.0f) << "the undo restored other samples";
    EXPECT_NE(after, before) << "the samples are the original's, the asset is not";
    EXPECT_NE(after.asset.assetIdentity, history->identity());
}

TEST(DerivedResultKeyTest, TwoDocumentsOverOneImageAreTwoKeys)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocument> first = filledDocument(provider->engineSession(), 1.0f);
    ASSERT_TRUE(first.has_value());
    an::Result<ImageAsset> copy = first->snapshot();
    ASSERT_TRUE(copy.hasValue());
    an::Result<ImageDocument> second = ImageDocument::create(std::move(*copy));
    ASSERT_TRUE(second.hasValue());
    EXPECT_NE(histogramKey(*first), histogramKey(*second));
    EXPECT_NE(histogramKey(*first).documentIdentity, histogramKey(*second).documentIdentity);
}
