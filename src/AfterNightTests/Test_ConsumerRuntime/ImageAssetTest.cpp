// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The image asset: an identity no other asset shares, read leases held together, a write lease that excludes them
/// and is preferred over new readers, revisions published when the writer lets go, a validity mask whose changes are
/// counted, and snapshots that are new assets copied under a read lease.

#include "ConsumerRuntimeTestSupport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <gtest/gtest.h>

#include <future>
#include <set>
#include <stop_token>
#include <thread>

namespace
{

using an::consumer::AnyImage;
using an::consumer::AssetReadLease;
using an::consumer::AssetRevision;
using an::consumer::AssetWriteLease;
using an::consumer::EngineSession;
using an::consumer::ImageAsset;
using an::tests::consumer::answers;
using an::tests::consumer::filledAsset;
using an::tests::consumer::filledImage;
using an::tests::consumer::openSession;
using an::tests::consumer::sampleAt;
using an::tests::consumer::stillWaiting;
using an::tests::consumer::writeSample;

/// A transition that states the samples are linear, which changes what the image means.
[[nodiscard]] an::MetadataTransition linearTransition()
{
    an::MetadataTransition transition;
    an::Result<an::ImageSemantics> semantics =
        an::ImageSemantics::create(an::ImageSemanticsRequest{.transferFunction = an::TransferFunction::Linear});
    EXPECT_TRUE(semantics.hasValue()) << semantics.status().message();
    if (semantics.hasValue())
    {
        transition.semantics = *semantics;
    }
    an::Result<an::ProvenanceEntry> entry = an::ProvenanceEntry::create("state the transfer function", "");
    EXPECT_TRUE(entry.hasValue());
    if (entry.hasValue())
    {
        transition.record = *entry;
    }
    return transition;
}

/// A mask covering an image of the given size, every pixel valid.
[[nodiscard]] an::Result<an::ValidityMask> allValidMask(const EngineSession& session, std::uint32_t width,
                                                        std::uint32_t height)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, hold->engine().limits());
    if (!extent.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the mask cannot be described");
    }
    return an::ValidityMask::createAllValid(hold->engine(), *extent);
}

} // namespace

TEST(ImageAssetTest, EveryAssetHasAnIdentityNoOtherShares)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::set<std::uint64_t> identities;
    std::vector<ImageAsset> assets;
    for (int index = 0; index < 16; ++index)
    {
        std::optional<ImageAsset> asset = filledAsset(*session, static_cast<float>(index));
        ASSERT_TRUE(asset.has_value());
        EXPECT_NE(asset->identity(), 0u);
        EXPECT_TRUE(identities.insert(asset->identity()).second) << "an identity was handed out twice";
        EXPECT_EQ(asset->revision().assetIdentity, asset->identity());
        assets.push_back(std::move(*asset));
    }
    EXPECT_EQ(session->outstanding().assets, 16u);
    ASSERT_TRUE(assets.front().descriptor().has_value());
    EXPECT_EQ(assets.front().descriptor()->extent().width(), 8u);
    EXPECT_EQ(an::consumer::sampleTypeOf(assets.front().read()->image()), an::SampleType::Float32);
}

TEST(ImageAssetTest, ManyReadersReadAtOnce)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::optional<ImageAsset> asset = filledAsset(*session, 5.0f);
    ASSERT_TRUE(asset.has_value());
    an::Result<AssetReadLease> first = asset->tryRead();
    an::Result<AssetReadLease> second = asset->tryRead();
    ASSERT_TRUE(first.hasValue() && second.hasValue());
    EXPECT_EQ(asset->readers(), 2u);
    EXPECT_EQ(sampleAt(first->image(), 1u, 1u), 5.0f);
    EXPECT_EQ(sampleAt(second->image(), 7u, 3u), 5.0f);
    EXPECT_EQ(asset->tryWrite().code(), an::ErrorCode::Busy);
    first->release();
    second->release();
    EXPECT_EQ(asset->readers(), 0u);
    EXPECT_TRUE(asset->tryWrite().hasValue());
}

TEST(ImageAssetTest, AWriterExcludesReadersAndOtherWriters)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::optional<ImageAsset> asset = filledAsset(*session, 1.0f);
    ASSERT_TRUE(asset.has_value());
    an::Result<AssetWriteLease> writer = asset->tryWrite();
    ASSERT_TRUE(writer.hasValue());
    EXPECT_TRUE(asset->writing());
    EXPECT_EQ(asset->tryRead().code(), an::ErrorCode::Busy);
    EXPECT_EQ(asset->tryWrite().code(), an::ErrorCode::Busy);

    std::stop_source cleanup;
    std::future<an::Result<AssetReadLease>> reader =
        std::async(std::launch::async, [&asset, token = cleanup.get_token()] { return asset->read(token); });
    EXPECT_TRUE(stillWaiting(reader)) << "a reader read while the writer held the asset";
    writer->release();
    const bool answered = answers(reader);
    if (!answered)
    {
        cleanup.request_stop();
    }
    ASSERT_TRUE(answered);
    EXPECT_TRUE(reader.get().hasValue());
}

// Writers are preferred: once a writer waits, a new reader waits behind it, so readers arriving one after another
// cannot keep a writer out for ever.
TEST(ImageAssetTest, AWaitingWriterHoldsBackNewReaders)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::optional<ImageAsset> asset = filledAsset(*session, 1.0f);
    ASSERT_TRUE(asset.has_value());
    an::Result<AssetReadLease> reading = asset->read();
    ASSERT_TRUE(reading.hasValue());

    std::stop_source cleanup;
    std::future<an::Result<AssetWriteLease>> writer =
        std::async(std::launch::async, [&asset, token = cleanup.get_token()] { return asset->write(token); });
    EXPECT_TRUE(stillWaiting(writer));
    EXPECT_EQ(asset->tryRead().code(), an::ErrorCode::Busy) << "a new reader overtook a waiting writer";

    reading->release();
    const bool answered = answers(writer);
    if (!answered)
    {
        cleanup.request_stop();
    }
    ASSERT_TRUE(answered);
    an::Result<AssetWriteLease> lease = writer.get();
    ASSERT_TRUE(lease.hasValue());
    EXPECT_TRUE(asset->writing());
}

// A holder reads an asset it already reads through shares of its lease, which are granted at once even while a writer
// waits: a second lease asked of the asset would wait behind that writer, and the writer for the first lease, for ever.
// The writer gets the asset once the lease and every share of it are let go.
TEST(ImageAssetTest, AShareOfALeaseIsGrantedWhileAWriterWaits)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::optional<ImageAsset> asset = filledAsset(*session, 1.0f);
    ASSERT_TRUE(asset.has_value());
    an::Result<AssetReadLease> reading = asset->read();
    ASSERT_TRUE(reading.hasValue());

    std::stop_source cleanup;
    std::future<an::Result<AssetWriteLease>> writer =
        std::async(std::launch::async, [&asset, token = cleanup.get_token()] { return asset->write(token); });
    EXPECT_TRUE(stillWaiting(writer));
    ASSERT_EQ(asset->tryRead().code(), an::ErrorCode::Busy) << "no writer waits";

    AssetReadLease share = reading->share();
    EXPECT_TRUE(share.valid());
    EXPECT_EQ(share.assetIdentity(), asset->identity());
    EXPECT_EQ(asset->readers(), 2u);
    EXPECT_EQ(sampleAt(share.image(), 0u, 0u), 1.0f);
    EXPECT_FALSE(AssetReadLease{}.share().valid()) << "a share of a lease on nothing is a lease on nothing";

    reading->release();
    EXPECT_TRUE(stillWaiting(writer)) << "the writer took the asset while a share of the lease was held";
    share.release();
    const bool answered = answers(writer);
    if (!answered)
    {
        cleanup.request_stop();
    }
    ASSERT_TRUE(answered);
    an::Result<AssetWriteLease> lease = writer.get();
    ASSERT_TRUE(lease.hasValue());
}

// A lease is a permission, not a lock the taking thread must release: a GUI thread takes one and a worker lets it go.
TEST(ImageAssetTest, ALeaseMayBeReleasedOnAnotherThread)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::optional<ImageAsset> asset = filledAsset(*session, 1.0f);
    ASSERT_TRUE(asset.has_value());
    an::Result<AssetWriteLease> writer = asset->tryWrite();
    ASSERT_TRUE(writer.hasValue());
    std::thread worker([lease = std::move(*writer)]() mutable { lease.release(); });
    worker.join();
    EXPECT_FALSE(asset->writing());

    an::Result<AssetReadLease> reader = asset->tryRead();
    ASSERT_TRUE(reader.hasValue());
    std::thread other([lease = std::move(*reader)]() mutable { lease.release(); });
    other.join();
    EXPECT_EQ(asset->readers(), 0u);
}

TEST(ImageAssetTest, ReadingMovesNoGeneration)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::optional<ImageAsset> asset = filledAsset(*session, 1.0f);
    ASSERT_TRUE(asset.has_value());
    const AssetRevision before = asset->revision();
    {
        an::Result<AssetReadLease> lease = asset->read();
        ASSERT_TRUE(lease.hasValue());
        EXPECT_EQ(lease->revision(), before);
        EXPECT_EQ(sampleAt(lease->image(), 0u, 0u), 1.0f);
        EXPECT_EQ(lease->validity(), nullptr);
    }
    EXPECT_EQ(asset->revision(), before);
}

// A caller without a lease reads the revision the last writer published, never one a writer is still making.
TEST(ImageAssetTest, AWriteIsPublishedWhenTheWriterLetsGo)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::optional<ImageAsset> asset = filledAsset(*session, 1.0f);
    ASSERT_TRUE(asset.has_value());
    const AssetRevision before = asset->revision();

    an::Result<AssetWriteLease> writer = asset->write();
    ASSERT_TRUE(writer.hasValue());
    writeSample(*writer, 2u, 1u, 9.0f);
    EXPECT_EQ(writer->revision().pixelGeneration, before.pixelGeneration + 1u);
    EXPECT_EQ(asset->revision(), before) << "a revision was published before the writer let go";

    writer->release();
    const AssetRevision after = asset->revision();
    EXPECT_EQ(after.assetIdentity, before.assetIdentity);
    EXPECT_EQ(after.pixelGeneration, before.pixelGeneration + 1u);
    EXPECT_EQ(after.semanticGeneration, before.semanticGeneration);
    an::Result<AssetReadLease> reader = asset->read();
    ASSERT_TRUE(reader.hasValue());
    EXPECT_EQ(sampleAt(reader->image(), 2u, 1u), 9.0f);
    EXPECT_EQ(sampleAt(reader->image(), 1u, 1u), 1.0f);
}

TEST(ImageAssetTest, WriteAccessThroughTheVisitorMovesThePixelGeneration)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::optional<ImageAsset> asset = filledAsset(*session, 1.0f);
    ASSERT_TRUE(asset.has_value());
    const AssetRevision before = asset->revision();
    {
        an::Result<AssetWriteLease> writer = asset->write();
        ASSERT_TRUE(writer.hasValue());
        EXPECT_FALSE(writer->write<double>().has_value()) << "typed access ignored the representation";
        writer->visitWrite([](auto access) {
            const auto row = access.view().row(0u, 0u);
            if (row.has_value())
            {
                (*row)[0] = 4;
            }
        });
    }
    EXPECT_EQ(asset->revision().pixelGeneration, before.pixelGeneration + 1u);
    EXPECT_EQ(sampleAt(asset->read()->image(), 0u, 0u), 4.0f);
}

TEST(ImageAssetTest, ATransitionThatChangesMeaningMovesTheSemanticGeneration)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::optional<ImageAsset> asset = filledAsset(*session, 1.0f);
    ASSERT_TRUE(asset.has_value());
    const AssetRevision before = asset->revision();
    {
        an::Result<AssetWriteLease> writer = asset->write();
        ASSERT_TRUE(writer.hasValue());
        const an::Status changed = writer->transition(linearTransition());
        ASSERT_TRUE(changed.ok()) << changed.message();
    }
    const AssetRevision after = asset->revision();
    EXPECT_EQ(after.semanticGeneration, before.semanticGeneration + 1u);
    EXPECT_EQ(after.pixelGeneration, before.pixelGeneration);
    an::Result<AssetReadLease> reader = asset->read();
    ASSERT_TRUE(reader.hasValue());
    reader->visit([](const auto& image) {
        EXPECT_EQ(image.metadata().semantics().transferFunction(), an::TransferFunction::Linear);
    });
}

TEST(ImageAssetTest, EveryChangeOfTheMaskMovesTheValidityGeneration)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::optional<ImageAsset> asset = filledAsset(*session, 1.0f, 8u, 4u);
    ASSERT_TRUE(asset.has_value());
    const AssetRevision before = asset->revision();
    {
        an::Result<AssetWriteLease> writer = asset->write();
        ASSERT_TRUE(writer.hasValue());
        EXPECT_EQ(writer->writeValidity(), nullptr) << "an asset with no mask handed one out";
        an::Result<an::ValidityMask> mask = allValidMask(*session, 8u, 4u);
        ASSERT_TRUE(mask.hasValue()) << mask.status().message();
        ASSERT_TRUE(writer->setValidity(std::move(*mask)).ok());
        EXPECT_EQ(writer->revision().validityGeneration, before.validityGeneration + 1u);
        ASSERT_NE(writer->writeValidity(), nullptr);
        EXPECT_EQ(writer->revision().validityGeneration, before.validityGeneration + 2u)
            << "write access to the mask did not move its generation";
        EXPECT_NE(writer->validity(), nullptr);
        EXPECT_EQ(writer->revision().validityGeneration, before.validityGeneration + 2u)
            << "reading the mask moved its generation";
    }
    EXPECT_EQ(asset->revision().validityGeneration, before.validityGeneration + 2u);
    {
        an::Result<AssetReadLease> reader = asset->read();
        ASSERT_TRUE(reader.hasValue());
        EXPECT_NE(reader->validity(), nullptr);
    }
    {
        an::Result<AssetWriteLease> writer = asset->write();
        ASSERT_TRUE(writer.hasValue());
        writer->clearValidity();
        EXPECT_EQ(writer->validity(), nullptr);
    }
    EXPECT_EQ(asset->revision().validityGeneration, before.validityGeneration + 3u);
    EXPECT_EQ(asset->revision().pixelGeneration, before.pixelGeneration);
}

TEST(ImageAssetTest, AMaskThatDoesNotCoverTheImageIsRefused)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::optional<ImageAsset> asset = filledAsset(*session, 1.0f, 8u, 4u);
    ASSERT_TRUE(asset.has_value());
    an::Result<an::ValidityMask> small = allValidMask(*session, 4u, 4u);
    ASSERT_TRUE(small.hasValue());
    an::Result<AssetWriteLease> writer = asset->write();
    ASSERT_TRUE(writer.hasValue());
    EXPECT_TRUE(writer->setValidity(std::move(*small)).failed());
    EXPECT_EQ(writer->validity(), nullptr);
}

TEST(ImageAssetTest, AnImageOrMaskFromAnotherContextIsRefused)
{
    std::optional<EngineSession> session = openSession(1u);
    std::optional<EngineSession> other = openSession(1u);
    ASSERT_TRUE(session.has_value() && other.has_value());
    an::Result<an::consumer::SessionHold> hold = other->hold();
    ASSERT_TRUE(hold.hasValue());
    an::Result<an::Image<float>> foreign = filledImage(hold->engine(), 8u, 4u, 1.0f);
    ASSERT_TRUE(foreign.hasValue());
    const an::Result<ImageAsset> refused = ImageAsset::adopt(*session, AnyImage{std::move(*foreign)});
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(session->outstanding().assets, 0u) << "a refused adoption was counted";

    an::Result<an::consumer::SessionHold> own = session->hold();
    ASSERT_TRUE(own.hasValue());
    an::Result<an::Image<float>> image = filledImage(own->engine(), 8u, 4u, 1.0f);
    an::Result<an::ValidityMask> foreignMask = allValidMask(*other, 8u, 4u);
    ASSERT_TRUE(image.hasValue() && foreignMask.hasValue());
    EXPECT_EQ(ImageAsset::adopt(*session, AnyImage{std::move(*image)}, std::move(*foreignMask)).code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(ImageAssetTest, AnImageThatHoldsNothingIsRefused)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(ImageAsset::adopt(*session, AnyImage{an::Image<std::uint16_t>{}}).code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(ImageAsset::adopt(EngineSession{}, AnyImage{}).code(), an::ErrorCode::NotInitialized);

    ImageAsset empty;
    EXPECT_FALSE(empty.valid());
    EXPECT_EQ(empty.identity(), 0u);
    EXPECT_EQ(empty.read().code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(empty.tryWrite().code(), an::ErrorCode::NotInitialized);
}

// A snapshot is a new asset: a new identity, the same samples, description and mask, and nothing that follows the
// source afterwards. Its generations start again, so only the identity keeps a result keyed by the source from being
// taken for the snapshot's.
TEST(ImageAssetTest, ASnapshotIsANewAssetWithANewIdentity)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::optional<ImageAsset> source = filledAsset(*session, 2.0f, 8u, 4u);
    ASSERT_TRUE(source.has_value());
    {
        an::Result<AssetWriteLease> writer = source->write();
        ASSERT_TRUE(writer.hasValue());
        writeSample(*writer, 3u, 2u, 7.0f);
        an::Result<an::ValidityMask> mask = allValidMask(*session, 8u, 4u);
        ASSERT_TRUE(mask.hasValue());
        ASSERT_TRUE(writer->setValidity(std::move(*mask)).ok());
    }
    const AssetRevision sourceRevision = source->revision();

    an::Result<ImageAsset> snapshot = source->snapshot();
    ASSERT_TRUE(snapshot.hasValue()) << snapshot.status().message();
    EXPECT_NE(snapshot->identity(), source->identity());
    EXPECT_NE(snapshot->revision(), sourceRevision);
    EXPECT_EQ(snapshot->revision().assetIdentity, snapshot->identity());
    EXPECT_EQ(snapshot->descriptor(), source->descriptor());
    EXPECT_EQ(session->outstanding().assets, 2u);
    {
        an::Result<AssetReadLease> copy = snapshot->read();
        ASSERT_TRUE(copy.hasValue());
        EXPECT_EQ(sampleAt(copy->image(), 3u, 2u), 7.0f);
        EXPECT_EQ(sampleAt(copy->image(), 0u, 0u), 2.0f);
        EXPECT_NE(copy->validity(), nullptr) << "the snapshot lost the mask";
    }

    // The source moves on; the snapshot does not.
    {
        an::Result<AssetWriteLease> writer = source->write();
        ASSERT_TRUE(writer.hasValue());
        writeSample(*writer, 3u, 2u, -1.0f);
    }
    EXPECT_EQ(sampleAt(snapshot->read()->image(), 3u, 2u), 7.0f);
    EXPECT_EQ(sampleAt(source->read()->image(), 3u, 2u), -1.0f);
}

// Restoring from a snapshot means adopting it, and a result cached for the source before the edit can never match
// it, even when the snapshot's generations happen to equal the ones the result was keyed by.
TEST(ImageAssetTest, CoincidentGenerationsCannotReviveAResultKeyedByAnotherAsset)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::optional<ImageAsset> source = filledAsset(*session, 1.0f);
    ASSERT_TRUE(source.has_value());
    const AssetRevision cachedFor = source->revision();
    an::Result<ImageAsset> restored = source->snapshot();
    ASSERT_TRUE(restored.hasValue());
    const AssetRevision restoredRevision = restored->revision();
    EXPECT_EQ(restoredRevision.pixelGeneration, cachedFor.pixelGeneration);
    EXPECT_EQ(restoredRevision.semanticGeneration, cachedFor.semanticGeneration);
    EXPECT_EQ(restoredRevision.validityGeneration, cachedFor.validityGeneration);
    EXPECT_NE(restoredRevision, cachedFor);
}

// The copy is taken under a read lease, so it cannot see a write half made: it waits for the writer and copies what
// the writer left.
TEST(ImageAssetTest, ASnapshotCannotOverlapAWriter)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::optional<ImageAsset> source = filledAsset(*session, 1.0f);
    ASSERT_TRUE(source.has_value());
    an::Result<AssetWriteLease> writer = source->write();
    ASSERT_TRUE(writer.hasValue());
    writeSample(*writer, 0u, 0u, 5.0f);

    std::stop_source cleanup;
    std::future<an::Result<ImageAsset>> snapshot =
        std::async(std::launch::async, [&source, token = cleanup.get_token()] { return source->snapshot(token); });
    EXPECT_TRUE(stillWaiting(snapshot)) << "a snapshot was copied while a writer held the asset";
    writeSample(*writer, 1u, 0u, 6.0f);
    writer->release();
    const bool answered = answers(snapshot);
    if (!answered)
    {
        cleanup.request_stop();
    }
    ASSERT_TRUE(answered);
    an::Result<ImageAsset> copy = snapshot.get();
    ASSERT_TRUE(copy.hasValue()) << copy.status().message();
    an::Result<AssetReadLease> lease = copy->read();
    ASSERT_TRUE(lease.hasValue());
    EXPECT_EQ(sampleAt(lease->image(), 0u, 0u), 5.0f);
    EXPECT_EQ(sampleAt(lease->image(), 1u, 0u), 6.0f);
}

TEST(ImageAssetTest, AStoppedWaitForALeaseIsCancelled)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::optional<ImageAsset> asset = filledAsset(*session, 1.0f);
    ASSERT_TRUE(asset.has_value());
    an::Result<AssetReadLease> reading = asset->read();
    ASSERT_TRUE(reading.hasValue());

    std::stop_source stop;
    std::future<an::Result<AssetWriteLease>> writer =
        std::async(std::launch::async, [&asset, token = stop.get_token()] { return asset->write(token); });
    EXPECT_TRUE(stillWaiting(writer));
    stop.request_stop();
    ASSERT_TRUE(answers(writer));
    EXPECT_EQ(writer.get().code(), an::ErrorCode::Cancelled);
    EXPECT_TRUE(asset->tryRead().hasValue()) << "a cancelled writer still held readers back";
}

// A snapshot is a reader, so one asked for while a writer waits waits behind it and copies what the writer wrote -
// never the image as it was before the writer took it. The writer's wait is observed exactly: with a reader still
// holding the asset, a new reader is refused only because a writer is waiting.
TEST(ImageAssetTest, ASnapshotAskedForWhileAWriterWaitsCopiesWhatTheWriterWrote)
{
    std::optional<EngineSession> session = openSession(2u);
    ASSERT_TRUE(session.has_value());
    std::optional<ImageAsset> asset = filledAsset(*session, 0.25f);
    ASSERT_TRUE(asset.has_value());
    an::Result<AssetReadLease> reading = asset->read();
    ASSERT_TRUE(reading.hasValue());

    std::stop_source cleanup;
    std::future<an::Status> writer = std::async(std::launch::async, [&asset, token = cleanup.get_token()] {
        an::Result<AssetWriteLease> lease = asset->write(token);
        if (!lease.hasValue())
        {
            return lease.status();
        }
        an::tests::consumer::writeSample(*lease, 1u, 1u, 0.75f);
        return an::Status::success();
    });
    const bool writerWaits =
        an::tests::consumer::eventually([&asset] { return asset->tryRead().code() == an::ErrorCode::Busy; });
    if (!writerWaits)
    {
        cleanup.request_stop();
    }
    ASSERT_TRUE(writerWaits) << "the writer never queued behind the reader";

    std::future<an::Result<ImageAsset>> snapshot =
        std::async(std::launch::async, [&asset, token = cleanup.get_token()] { return asset->snapshot(token); });
    reading->release();

    ASSERT_TRUE(answers(writer));
    ASSERT_TRUE(writer.get().ok());
    ASSERT_TRUE(answers(snapshot));
    an::Result<ImageAsset> copy = snapshot.get();
    ASSERT_TRUE(copy.hasValue()) << copy.status().message();
    an::Result<AssetReadLease> copied = copy->read();
    ASSERT_TRUE(copied.hasValue());
    EXPECT_FLOAT_EQ(sampleAt(copied->image(), 1u, 1u), 0.75f) << "the snapshot overtook the waiting writer";
    EXPECT_FLOAT_EQ(sampleAt(copied->image(), 0u, 0u), 0.25f);
}
