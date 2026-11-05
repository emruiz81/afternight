// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The image document: an identity that outlives its asset, the user interface's names, sequence and presentation,
/// read leases held together and a writer that excludes them, a revision published when the writer lets go, a
/// replacement that is a new asset and that waiting readers and writers follow, a close that refuses writers while
/// work in flight drains, and a shared document that reads another's asset and hands out no writer.

#include "ControlsSessionTestSupport.h"

#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <gtest/gtest.h>

#include <future>
#include <set>
#include <stop_token>
#include <thread>
#include <vector>

namespace
{

using an::consumer::AssetReadLease;
using an::consumer::ImageAsset;
using an::tests::controls::answers;
using an::tests::controls::documentSampleAt;
using an::tests::controls::filledAsset;
using an::tests::controls::filledDocument;
using an::tests::controls::openProvider;
using an::tests::controls::sampleAt;
using an::tests::controls::stillWaiting;
using an::tests::controls::writeSample;
using an::ui::DocumentNames;
using an::ui::DocumentPresentation;
using an::ui::DocumentRevision;
using an::ui::DocumentSequence;
using an::ui::ImageDocument;
using an::ui::ImageDocumentWriter;

} // namespace

TEST(ImageDocumentTest, ADocumentNeedsAnAssetThatHoldsAnImage)
{
    an::Result<ImageDocument> document = ImageDocument::create(ImageAsset{});
    ASSERT_FALSE(document.hasValue());
    EXPECT_EQ(document.status().code(), an::ErrorCode::NotInitialized);

    const ImageDocument empty;
    EXPECT_FALSE(empty.valid());
    EXPECT_EQ(empty.identity(), 0u);
    EXPECT_FALSE(empty.isOpen());
    EXPECT_EQ(empty.read().status().code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(empty.asset(), nullptr);
}

TEST(ImageDocumentTest, EveryDocumentHasAnIdentityNoOtherShares)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::set<std::uint64_t> identities;
    std::vector<ImageDocument> documents;
    for (int index = 0; index < 8; ++index)
    {
        std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
        ASSERT_TRUE(document.has_value());
        EXPECT_TRUE(document->valid());
        EXPECT_TRUE(document->isOpen());
        EXPECT_NE(document->identity(), 0u);
        identities.insert(document->identity());
        documents.push_back(std::move(*document));
    }
    EXPECT_EQ(identities.size(), documents.size());
}

// A shared document reads the very asset another document holds, under an identity of its own, and hands out no
// writer: what the other document writes it reads, and when the other replaces its image it keeps the one it showed.
TEST(ImageDocumentTest, ASharedDocumentReadsAnotherDocumentsAssetAndHandsOutNoWriter)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> owner = filledDocument(session, 1.0f);
    ASSERT_TRUE(owner.has_value());

    an::Result<ImageDocument> shared = ImageDocument::share(owner->asset(), DocumentNames{.displayName = "shown"});
    ASSERT_TRUE(shared.hasValue()) << shared.status().message();
    EXPECT_TRUE(shared->isShared());
    EXPECT_FALSE(owner->isShared());
    EXPECT_TRUE(shared->isOpen());
    EXPECT_NE(shared->identity(), owner->identity());
    EXPECT_EQ(shared->asset(), owner->asset()) << "a shared document holds the other's asset, not a copy";
    EXPECT_EQ(shared->revision().asset, owner->revision().asset);
    EXPECT_NE(shared->revision(), owner->revision()) << "what is derived from one document is not the other's";
    EXPECT_EQ(shared->names().displayName, QString("shown"));
    {
        an::Result<AssetReadLease> lease = shared->tryRead();
        ASSERT_TRUE(lease.hasValue());
        EXPECT_EQ(lease->assetIdentity(), owner->revision().asset.assetIdentity);
        EXPECT_EQ(sampleAt(lease->image(), 0u, 0u), 1.0f);
    }
    EXPECT_EQ(shared->write().status().code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(shared->tryWrite().status().code(), an::ErrorCode::InvalidState);

    {
        an::Result<ImageDocumentWriter> writer = owner->tryWrite();
        ASSERT_TRUE(writer.hasValue()) << "the shared document holds back the owner's writer";
        writeSample(writer->asset(), 0u, 0u, 4.0f);
    }
    EXPECT_EQ(documentSampleAt(*shared, 0u, 0u), 4.0f);
    EXPECT_EQ(shared->revision().asset, owner->revision().asset);

    std::optional<ImageAsset> replacement = filledAsset(session, 7.0f);
    ASSERT_TRUE(replacement.has_value());
    {
        an::Result<ImageDocumentWriter> writer = owner->write();
        ASSERT_TRUE(writer.hasValue());
        ASSERT_TRUE(writer->replace(std::move(*replacement)).ok());
    }
    EXPECT_NE(shared->asset(), owner->asset());
    EXPECT_EQ(documentSampleAt(*owner, 0u, 0u), 7.0f);
    EXPECT_EQ(documentSampleAt(*shared, 0u, 0u), 4.0f) << "the shared document keeps the image it showed";
    EXPECT_TRUE(shared->snapshot().hasValue());
}

TEST(ImageDocumentTest, ADocumentSharesOnlyAnAssetThatHoldsAnImage)
{
    EXPECT_EQ(ImageDocument::share(nullptr).status().code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(ImageDocument::share(std::make_shared<const ImageAsset>()).status().code(),
              an::ErrorCode::NotInitialized);
    const ImageDocument empty;
    EXPECT_FALSE(empty.isShared());
}

TEST(ImageDocumentTest, NamesSequenceAndPresentationAreTheDocumentsOwn)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocument> document =
        filledDocument(provider->engineSession(), 1.0f, DocumentNames{.displayName = "M31", .sourcePath = "m31.fits"});
    ASSERT_TRUE(document.has_value());
    EXPECT_EQ(document->names().displayName, QString("M31"));
    EXPECT_EQ(document->names().sourcePath, QString("m31.fits"));
    document->setNames(DocumentNames{.displayName = "M31 stretched", .sourcePath = "m31_stretched.xisf"});
    EXPECT_EQ(document->names().displayName, QString("M31 stretched"));
    EXPECT_EQ(document->names().sourcePath, QString("m31_stretched.xisf"));

    EXPECT_FALSE(document->sequence().has_value());
    DocumentSequence sequence;
    sequence.sourcePath = "jupiter.ser";
    sequence.descriptor.format = an::SequenceFormat::Ser;
    sequence.descriptor.frameCount = 400u;
    sequence.selectedFrames = std::vector<std::uint64_t>{3u, 5u, 8u};
    sequence.currentFrame = 5u;
    document->setSequence(sequence);
    const std::optional<DocumentSequence> kept = document->sequence();
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ(kept->sourcePath, QString("jupiter.ser"));
    EXPECT_EQ(kept->descriptor.format, an::SequenceFormat::Ser);
    EXPECT_EQ(kept->descriptor.frameCount, std::optional<std::uint64_t>(400u));
    EXPECT_EQ(kept->selectedFrames,
              (std::optional<std::vector<std::uint64_t>>(std::vector<std::uint64_t>{3u, 5u, 8u})));
    EXPECT_EQ(kept->currentFrame, 5u);
    document->setSequence(std::nullopt);
    EXPECT_FALSE(document->sequence().has_value());

    EXPECT_EQ(document->presentationGeneration(), 0u);
    DocumentPresentation presentation;
    presentation.request.linked = true;
    presentation.request.saturation = 0.25;
    presentation.channel = an::ChannelScope::Green;
    document->setPresentation(presentation);
    EXPECT_EQ(document->presentationGeneration(), 1u);
    EXPECT_TRUE(document->presentation().request.linked);
    EXPECT_EQ(document->presentation().request.saturation, 0.25);
    EXPECT_EQ(document->presentation().channel, an::ChannelScope::Green);
    document->setPresentation(presentation);
    EXPECT_EQ(document->presentationGeneration(), 2u) << "setting the presentation moves its generation every time";

    const DocumentRevision revision = document->revision();
    document->setNames(DocumentNames{.displayName = "renamed"});
    document->setSequence(sequence);
    document->setPresentation(DocumentPresentation{});
    EXPECT_EQ(document->revision(), revision) << "what the user interface knows a document by is not its image";
}

TEST(ImageDocumentTest, ReadersShareTheDocumentAndTheWriterExcludesThem)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    {
        an::Result<AssetReadLease> first = document->tryRead();
        an::Result<AssetReadLease> second = document->tryRead();
        ASSERT_TRUE(first.hasValue());
        ASSERT_TRUE(second.hasValue());
        EXPECT_EQ(first->assetIdentity(), document->revision().asset.assetIdentity);
        an::Result<ImageDocumentWriter> refused = document->tryWrite();
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().code(), an::ErrorCode::Busy);
    }
    an::Result<ImageDocumentWriter> writer = document->tryWrite();
    ASSERT_TRUE(writer.hasValue()) << writer.status().message();
    EXPECT_TRUE(writer->valid());
    EXPECT_EQ(writer->documentIdentity(), document->identity());
    EXPECT_EQ(document->tryRead().status().code(), an::ErrorCode::Busy);
    EXPECT_EQ(document->tryWrite().status().code(), an::ErrorCode::Busy);
    writer->release();
    EXPECT_FALSE(writer->valid());
    EXPECT_TRUE(document->tryRead().hasValue());
}

TEST(ImageDocumentTest, TheRevisionIsPublishedWhenTheWriterLetsGo)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    const DocumentRevision before = document->revision();
    EXPECT_EQ(before.documentIdentity, document->identity());
    {
        an::Result<ImageDocumentWriter> writer = document->write();
        ASSERT_TRUE(writer.hasValue());
        writeSample(writer->asset(), 2u, 1u, 7.0f);
        EXPECT_EQ(writer->revision().asset.pixelGeneration, before.asset.pixelGeneration + 1u);
        EXPECT_EQ(document->revision(), before) << "a write half made was published";
        const an::Status changed = writer->asset().transition(an::tests::controls::linearTransition());
        ASSERT_TRUE(changed.ok()) << changed.message();
    }
    const DocumentRevision after = document->revision();
    EXPECT_EQ(after.documentIdentity, before.documentIdentity);
    EXPECT_EQ(after.asset.assetIdentity, before.asset.assetIdentity);
    EXPECT_EQ(after.asset.pixelGeneration, before.asset.pixelGeneration + 1u);
    EXPECT_EQ(after.asset.semanticGeneration, before.asset.semanticGeneration + 1u);
    EXPECT_EQ(documentSampleAt(*document, 2u, 1u), 7.0f);
    ASSERT_TRUE(document->descriptor().has_value());
    EXPECT_EQ(document->descriptor()->extent().width(), 8u);
}

TEST(ImageDocumentTest, AReplacementIsANewAssetUnderTheSameDocument)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> document =
        filledDocument(session, 1.0f, DocumentNames{.displayName = "light", .sourcePath = "light.fits"});
    ASSERT_TRUE(document.has_value());
    document->setPresentation(DocumentPresentation{.channel = an::ChannelScope::Red});
    const DocumentRevision before = document->revision();
    const std::shared_ptr<const ImageAsset> replaced = document->asset();
    ASSERT_NE(replaced, nullptr);

    std::optional<ImageAsset> replacement = filledAsset(session, 5.0f, 16u, 2u);
    ASSERT_TRUE(replacement.has_value());
    const std::uint64_t replacementIdentity = replacement->identity();
    {
        an::Result<ImageDocumentWriter> writer = document->write();
        ASSERT_TRUE(writer.hasValue());
        const an::Status done = writer->replace(std::move(*replacement));
        ASSERT_TRUE(done.ok()) << done.message();
        EXPECT_EQ(writer->asset().assetIdentity(), replacementIdentity);
        writeSample(writer->asset(), 0u, 0u, 9.0f);
    }

    const DocumentRevision after = document->revision();
    EXPECT_EQ(after.documentIdentity, before.documentIdentity);
    EXPECT_EQ(after.asset.assetIdentity, replacementIdentity);
    EXPECT_NE(after.asset.assetIdentity, before.asset.assetIdentity);
    EXPECT_EQ(document->names().displayName, QString("light"));
    EXPECT_EQ(document->presentation().channel, an::ChannelScope::Red);
    ASSERT_TRUE(document->descriptor().has_value());
    EXPECT_EQ(document->descriptor()->extent().width(), 16u) << "a replacement may change the image's shape";
    EXPECT_EQ(documentSampleAt(*document, 0u, 0u), 9.0f);
    EXPECT_EQ(documentSampleAt(*document, 1u, 0u), 5.0f);

    an::Result<AssetReadLease> old = replaced->read();
    ASSERT_TRUE(old.hasValue()) << "the replaced asset lives on for whoever still holds it";
    EXPECT_EQ(sampleAt(old->image(), 0u, 0u), 1.0f);
    EXPECT_NE(document->asset(), replaced);
}

TEST(ImageDocumentTest, AReaderWaitingThroughAReplacementReadsTheReplacement)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());
    std::optional<ImageAsset> replacement = filledAsset(session, 3.0f);
    ASSERT_TRUE(replacement.has_value());
    const std::uint64_t replacementIdentity = replacement->identity();

    an::Result<ImageDocumentWriter> writer = document->write();
    ASSERT_TRUE(writer.hasValue());
    std::future<std::pair<std::uint64_t, float>> reader = std::async(std::launch::async, [&document] {
        an::Result<AssetReadLease> lease = document->read();
        return lease.hasValue() ? std::make_pair(lease->assetIdentity(), sampleAt(lease->image(), 0u, 0u))
                                : std::make_pair(std::uint64_t{0u}, -1.0f);
    });
    EXPECT_TRUE(stillWaiting(reader)) << "a reader overlapped the writer";
    ASSERT_TRUE(writer->replace(std::move(*replacement)).ok());
    EXPECT_TRUE(stillWaiting(reader)) << "a reader overlapped the writer after it replaced the asset";
    writer->release();
    ASSERT_TRUE(answers(reader));
    const std::pair<std::uint64_t, float> read = reader.get();
    EXPECT_EQ(read.first, replacementIdentity) << "the reader read the asset the document had let go";
    EXPECT_EQ(read.second, 3.0f);
}

TEST(ImageDocumentTest, AWriterWaitingThroughAReplacementWritesTheReplacement)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());
    std::optional<ImageAsset> replacement = filledAsset(session, 3.0f);
    ASSERT_TRUE(replacement.has_value());
    const std::uint64_t replacementIdentity = replacement->identity();

    an::Result<ImageDocumentWriter> first = document->write();
    ASSERT_TRUE(first.hasValue());
    std::future<std::uint64_t> second = std::async(std::launch::async, [&document] {
        an::Result<ImageDocumentWriter> writer = document->write();
        if (!writer.hasValue())
        {
            return std::uint64_t{0u};
        }
        writeSample(writer->asset(), 1u, 1u, 4.0f);
        return writer->asset().assetIdentity();
    });
    EXPECT_TRUE(stillWaiting(second)) << "two writers overlapped";
    ASSERT_TRUE(first->replace(std::move(*replacement)).ok());
    first->release();
    ASSERT_TRUE(answers(second));
    EXPECT_EQ(second.get(), replacementIdentity) << "the second writer wrote the asset the document had let go";
    EXPECT_EQ(documentSampleAt(*document, 1u, 1u), 4.0f);
}

TEST(ImageDocumentTest, AReplacementSomeoneElseLeasesIsRefusedAndNothingChanges)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());
    const DocumentRevision before = document->revision();

    an::Result<ImageDocumentWriter> writer = document->write();
    ASSERT_TRUE(writer.hasValue());
    const an::Status empty = writer->replace(ImageAsset{});
    EXPECT_EQ(empty.code(), an::ErrorCode::NotInitialized);

    std::optional<ImageAsset> replacement = filledAsset(session, 2.0f);
    ASSERT_TRUE(replacement.has_value());
    {
        an::Result<AssetReadLease> held = replacement->read();
        ASSERT_TRUE(held.hasValue());
        const an::Status busy = writer->replace(std::move(*replacement));
        EXPECT_EQ(busy.code(), an::ErrorCode::Busy);
    }
    EXPECT_TRUE(replacement->valid()) << "a refused replacement was taken anyway";
    EXPECT_EQ(writer->asset().assetIdentity(), before.asset.assetIdentity);
    writer->release();
    EXPECT_EQ(document->revision(), before);

    ImageDocumentWriter nothing;
    EXPECT_EQ(nothing.replace(std::move(*replacement)).code(), an::ErrorCode::NotInitialized);
}

// A replacement alone waits for no reader: what reads the image the document holds goes on reading it, and every lease
// asked for afterwards is taken on the replacement.
TEST(ImageDocumentTest, AReplacementAloneWaitsForNoReaderAndTheReadersKeepTheImageTheyRead)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());
    const DocumentRevision before = document->revision();
    std::optional<ImageAsset> replacement = filledAsset(session, 3.0f, 16u, 2u);
    ASSERT_TRUE(replacement.has_value());
    const std::uint64_t replacementIdentity = replacement->identity();

    an::Result<AssetReadLease> reading = document->read();
    ASSERT_TRUE(reading.hasValue());
    const an::Status replaced = document->tryReplace(std::move(*replacement));
    ASSERT_TRUE(replaced.ok()) << replaced.message();

    EXPECT_EQ(reading->assetIdentity(), before.asset.assetIdentity);
    EXPECT_EQ(sampleAt(reading->image(), 0u, 0u), 1.0f) << "a reader saw its image change under it";
    const DocumentRevision after = document->revision();
    EXPECT_EQ(after.documentIdentity, before.documentIdentity);
    EXPECT_EQ(after.asset.assetIdentity, replacementIdentity);
    ASSERT_TRUE(document->descriptor().has_value());
    EXPECT_EQ(document->descriptor()->extent().width(), 16u);
    EXPECT_EQ(documentSampleAt(*document, 0u, 0u), 3.0f) << "a lease asked for afterwards reads the replacement";
    reading->release();

    // The replacement is free for the document's own writer, as any image it holds is.
    an::Result<ImageDocumentWriter> writer = document->tryWrite();
    ASSERT_TRUE(writer.hasValue()) << writer.status().message();
    EXPECT_EQ(writer->asset().assetIdentity(), replacementIdentity);
}

// A writer that holds the document is never cut short: a replacement alone is refused while it does, and the document,
// the writer's image and the replacement are left as they were.
TEST(ImageDocumentTest, AReplacementAloneIsRefusedWhileAWriterHoldsTheDocument)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());
    const DocumentRevision before = document->revision();
    std::optional<ImageAsset> replacement = filledAsset(session, 3.0f);
    ASSERT_TRUE(replacement.has_value());

    {
        an::Result<ImageDocumentWriter> writer = document->write();
        ASSERT_TRUE(writer.hasValue());
        EXPECT_EQ(document->tryReplace(std::move(*replacement)).code(), an::ErrorCode::Busy);
        EXPECT_TRUE(replacement->valid()) << "a refused replacement was taken anyway";
        EXPECT_EQ(writer->asset().assetIdentity(), before.asset.assetIdentity);
        writeSample(writer->asset(), 0u, 0u, 6.0f);

        // A writer that replaced the image still holds the document.
        std::optional<ImageAsset> written = filledAsset(session, 4.0f);
        ASSERT_TRUE(written.has_value());
        ASSERT_TRUE(writer->replace(std::move(*written)).ok());
        EXPECT_EQ(document->tryReplace(std::move(*replacement)).code(), an::ErrorCode::Busy);
    }
    EXPECT_EQ(documentSampleAt(*document, 0u, 0u), 4.0f);

    // Once the writer lets go, and a writer moved from another lets go once.
    {
        an::Result<ImageDocumentWriter> taken = document->write();
        ASSERT_TRUE(taken.hasValue());
        ImageDocumentWriter moved = std::move(*taken);
        taken->release();
        EXPECT_EQ(document->tryReplace(std::move(*replacement)).code(), an::ErrorCode::Busy)
            << "a moved-from writer let the document go for the writer it moved to";
    }
    const an::Status replaced = document->tryReplace(std::move(*replacement));
    ASSERT_TRUE(replaced.ok()) << replaced.message();
    EXPECT_EQ(documentSampleAt(*document, 0u, 0u), 3.0f);
}

// A replacement alone is refused, the document and the replacement unchanged, for a closed document, for one that shows
// an image another document holds, for an image that holds nothing and for one someone else leases.
TEST(ImageDocumentTest, AReplacementAloneIsRefusedWhereAWriterWouldBe)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());
    const DocumentRevision before = document->revision();
    std::optional<ImageAsset> replacement = filledAsset(session, 3.0f);
    ASSERT_TRUE(replacement.has_value());

    EXPECT_EQ(document->tryReplace(ImageAsset{}).code(), an::ErrorCode::NotInitialized);
    {
        an::Result<AssetReadLease> held = replacement->read();
        ASSERT_TRUE(held.hasValue());
        EXPECT_EQ(document->tryReplace(std::move(*replacement)).code(), an::ErrorCode::Busy);
    }
    EXPECT_TRUE(replacement->valid());
    EXPECT_EQ(document->revision(), before);

    an::Result<ImageDocument> shared = ImageDocument::share(document->asset());
    ASSERT_TRUE(shared.hasValue());
    EXPECT_EQ(shared->tryReplace(std::move(*replacement)).code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(shared->asset(), document->asset());

    document->close();
    EXPECT_EQ(document->tryReplace(std::move(*replacement)).code(), an::ErrorCode::InvalidState);
    EXPECT_TRUE(replacement->valid());
    EXPECT_EQ(document->revision(), before);
    EXPECT_EQ(ImageDocument{}.tryReplace(std::move(*replacement)).code(), an::ErrorCode::NotInitialized);
}

TEST(ImageDocumentTest, AClosedDocumentRefusesWritersAndKeepsItsReaders)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());

    an::Result<ImageDocumentWriter> before = document->write();
    ASSERT_TRUE(before.hasValue());
    document->close();
    document->close();
    EXPECT_FALSE(document->isOpen());
    std::optional<ImageAsset> replacement = filledAsset(session, 2.0f);
    ASSERT_TRUE(replacement.has_value());
    EXPECT_EQ(before->replace(std::move(*replacement)).code(), an::ErrorCode::InvalidState);
    EXPECT_TRUE(replacement->valid());
    writeSample(before->asset(), 0u, 0u, 6.0f);
    before->release();

    EXPECT_EQ(document->write().status().code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(document->tryWrite().status().code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(documentSampleAt(*document, 0u, 0u), 6.0f) << "work that held the writer before the close finished";
    EXPECT_TRUE(document->snapshot().hasValue());
}

TEST(ImageDocumentTest, ASnapshotIsANewAssetOfTheSameSamples)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    {
        an::Result<ImageDocumentWriter> writer = document->write();
        ASSERT_TRUE(writer.hasValue());
        writeSample(writer->asset(), 3u, 2u, 8.0f);
    }
    an::Result<ImageAsset> snapshot = document->snapshot();
    ASSERT_TRUE(snapshot.hasValue()) << snapshot.status().message();
    EXPECT_NE(snapshot->identity(), document->revision().asset.assetIdentity);
    an::Result<AssetReadLease> lease = snapshot->read();
    ASSERT_TRUE(lease.hasValue());
    EXPECT_EQ(sampleAt(lease->image(), 3u, 2u), 8.0f);
}

TEST(ImageDocumentTest, AStoppedWaitIsCancelledAndTakesNothing)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    an::Result<ImageDocumentWriter> writer = document->write();
    ASSERT_TRUE(writer.hasValue());

    std::stop_source stopReader;
    std::stop_source stopWriter;
    std::future<an::ErrorCode> reader = std::async(std::launch::async, [&document, token = stopReader.get_token()] {
        return document->read(token).status().code();
    });
    std::future<an::ErrorCode> secondWriter =
        std::async(std::launch::async,
                   [&document, token = stopWriter.get_token()] { return document->write(token).status().code(); });
    EXPECT_TRUE(stillWaiting(reader));
    EXPECT_TRUE(stillWaiting(secondWriter));
    stopReader.request_stop();
    stopWriter.request_stop();
    ASSERT_TRUE(answers(reader));
    ASSERT_TRUE(answers(secondWriter));
    EXPECT_EQ(reader.get(), an::ErrorCode::Cancelled);
    EXPECT_EQ(secondWriter.get(), an::ErrorCode::Cancelled);
    writer->release();
    EXPECT_TRUE(document->tryWrite().hasValue()) << "a cancelled wait left something held";
}

TEST(ImageDocumentTest, TheWriterMayBeReleasedOnAnotherThread)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    an::Result<ImageDocumentWriter> writer = document->write();
    ASSERT_TRUE(writer.hasValue());
    writeSample(writer->asset(), 0u, 3u, 2.5f);
    std::thread releaser([moved = std::move(*writer)]() mutable { moved.release(); });
    releaser.join();
    EXPECT_EQ(documentSampleAt(*document, 0u, 3u), 2.5f);
}

TEST(ImageDocumentTest, AWriterOrALeaseOutlivesTheDocumentObject)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocumentWriter> writer;
    std::optional<AssetReadLease> lease;
    {
        std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
        ASSERT_TRUE(document.has_value());
        an::Result<ImageDocumentWriter> taken = document->write();
        ASSERT_TRUE(taken.hasValue());
        writer.emplace(std::move(*taken));
    }
    writeSample(writer->asset(), 1u, 0u, 3.0f);
    EXPECT_EQ(sampleAt(writer->asset().image(), 1u, 0u), 3.0f);
    writer->release();

    {
        std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 4.0f);
        ASSERT_TRUE(document.has_value());
        an::Result<AssetReadLease> taken = document->read();
        ASSERT_TRUE(taken.hasValue());
        lease.emplace(std::move(*taken));
    }
    EXPECT_EQ(sampleAt(lease->image(), 0u, 0u), 4.0f);
}
