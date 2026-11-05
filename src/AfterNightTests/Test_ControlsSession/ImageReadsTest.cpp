// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The images a run reads beside the one it writes: named once each, leased in one order of identity around the run's
/// own writer before the run is admitted, and read through shares of those leases, which never wait.

#include "ControlsSessionTestSupport.h"

#include "session/ExecutionScope.h"
#include "session/ImageReads.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{

using an::consumer::AssetReadLease;
using an::consumer::AssetWriteLease;
using an::consumer::ImageAsset;
using an::tests::controls::answers;
using an::tests::controls::openProvider;
using an::tests::controls::stillWaiting;
using an::ui::ImageReads;

/// A new asset of a session, shared as a run holds the images it reads.
[[nodiscard]] std::shared_ptr<ImageAsset> sharedAsset(const an::consumer::EngineSession& session, float value)
{
    std::optional<ImageAsset> asset = an::tests::controls::filledAsset(session, value);
    return asset.has_value() ? std::make_shared<ImageAsset>(std::move(*asset)) : nullptr;
}

} // namespace

// A run names each image once, leases the images in order of identity, and takes its own writer at its image's place
// in that order, leasing nothing of the image it writes: two runs that each read the image the other writes take what
// they share in the same order and cannot each hold what the other waits for.
TEST(ImageReadsTest, ImagesAreLeasedOnceEachInOrderOfIdentityAroundTheWriter)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    const std::shared_ptr<ImageAsset> lowest = sharedAsset(session, 0.1f);
    const std::shared_ptr<ImageAsset> written = sharedAsset(session, 0.2f);
    const std::shared_ptr<ImageAsset> highest = sharedAsset(session, 0.3f);
    ASSERT_TRUE(lowest && written && highest);
    ASSERT_LT(lowest->identity(), written->identity());
    ASSERT_LT(written->identity(), highest->identity());

    ImageReads reads;
    reads.name(highest);
    reads.name(lowest);
    reads.name(written);
    reads.name(lowest);
    reads.name(nullptr);
    EXPECT_EQ(reads.identities(),
              (std::vector<std::uint64_t>{lowest->identity(), written->identity(), highest->identity()}));

    std::vector<std::uint32_t> readersAtTheWriter;
    const an::Status taken = reads.take({}, written->identity(), [&]() {
        readersAtTheWriter = {lowest->readers(), written->readers(), highest->readers()};
        return an::Status::success();
    });
    ASSERT_TRUE(taken.ok()) << taken.message();
    EXPECT_EQ(readersAtTheWriter, (std::vector<std::uint32_t>{1u, 0u, 0u}))
        << "the writer was not taken between the image below its own and the one above it";
    EXPECT_EQ(lowest->readers(), 1u) << "an image named twice was leased twice";
    EXPECT_EQ(written->readers(), 0u) << "the image the run writes was leased";
    EXPECT_EQ(highest->readers(), 1u);
    EXPECT_NE(reads.find(lowest->identity()), nullptr);
    EXPECT_EQ(reads.find(written->identity()), nullptr);
    EXPECT_EQ(reads.written(), written->identity());

    reads.release();
    EXPECT_EQ(lowest->readers(), 0u);
    EXPECT_EQ(highest->readers(), 0u);
}

// A writer that is refused refuses the take, and the leases the take took are let go.
TEST(ImageReadsTest, AWriterThatIsRefusedLetsTheLeasesGo)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    const std::shared_ptr<ImageAsset> read = sharedAsset(session, 0.1f);
    const std::shared_ptr<ImageAsset> written = sharedAsset(session, 0.2f);
    ASSERT_TRUE(read && written);

    ImageReads reads;
    reads.name(read);
    const an::Status taken = reads.take({}, written->identity(), []() {
        return an::Status::failure(an::ErrorCode::Cancelled, "the writer was not taken");
    });
    EXPECT_EQ(taken.code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(read->readers(), 0u) << "a lease the refused take took is still held";
}

// A body reads a named image through a share of the run's lease, which waits for nothing - not even a writer that asked
// for the image after the run took its lease, behind which a second lease would wait for ever while the writer waited
// for the first. The image the run writes is refused rather than leased under its own writer, and an image the run did
// not name is leased on its own.
TEST(ImageReadsTest, ABodyReadsANamedImageThroughAShareEvenWhileAWriterWaits)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    const std::shared_ptr<ImageAsset> named = sharedAsset(session, 0.1f);
    const std::shared_ptr<ImageAsset> written = sharedAsset(session, 0.2f);
    const std::shared_ptr<ImageAsset> unnamed = sharedAsset(session, 0.3f);
    ASSERT_TRUE(named && written && unnamed);

    ImageReads reads;
    reads.name(named);
    reads.name(written);
    ASSERT_TRUE(reads.take({}, written->identity(), []() { return an::Status::success(); }).ok());

    std::stop_source cleanup;
    std::future<an::Result<AssetWriteLease>> writer =
        std::async(std::launch::async, [&named, token = cleanup.get_token()] { return named->write(token); });
    EXPECT_TRUE(stillWaiting(writer));
    ASSERT_EQ(named->tryRead().code(), an::ErrorCode::Busy) << "no writer waits";

    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply, .reads = &reads}, {});
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    EXPECT_EQ(scope->reads(), &reads);

    an::Result<AssetReadLease> shared = an::ui::readBeside(*scope, *named);
    ASSERT_TRUE(shared.hasValue()) << shared.status().message();
    EXPECT_EQ(named->readers(), 2u);
    EXPECT_EQ(an::ui::readBeside(*scope, *written).status().code(), an::ErrorCode::InvalidState)
        << "the image the run writes was leased under its own writer";
    an::Result<AssetReadLease> own = an::ui::readBeside(*scope, *unnamed);
    ASSERT_TRUE(own.hasValue()) << own.status().message();
    EXPECT_EQ(unnamed->readers(), 1u);
    scope->finish(an::Status::success());

    shared->release();
    reads.release();
    const bool answered = answers(writer);
    if (!answered)
    {
        cleanup.request_stop();
    }
    ASSERT_TRUE(answered);
    EXPECT_TRUE(writer.get().hasValue());
}

// A lease the run took already - its target's, taken without waiting - is held under its image's identity, read
// through, and not taken again: a second lease would wait behind a writer that asked for the image since.
TEST(ImageReadsTest, ALeaseTheRunHoldsIsReadThroughAndNotTakenAgain)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    const std::shared_ptr<ImageAsset> target = sharedAsset(session, 0.1f);
    ASSERT_NE(target, nullptr);
    an::Result<AssetReadLease> lease = target->read();
    ASSERT_TRUE(lease.hasValue());

    ImageReads reads;
    reads.name(target);
    reads.hold(lease->share());
    EXPECT_EQ(target->readers(), 2u) << "the lease the run holds was let go";
    lease->release();

    std::stop_source cleanup;
    std::future<an::Result<AssetWriteLease>> writer =
        std::async(std::launch::async, [&target, token = cleanup.get_token()] { return target->write(token); });
    EXPECT_TRUE(stillWaiting(writer));
    std::stop_source giveUp;
    std::future<an::Status> taken =
        std::async(std::launch::async, [&reads, token = giveUp.get_token()] { return reads.take(token); });
    const bool tookAtOnce = answers(taken);
    if (!tookAtOnce)
    {
        giveUp.request_stop();
    }
    ASSERT_TRUE(tookAtOnce) << "the run leased the image it holds again, behind the writer";
    EXPECT_TRUE(taken.get().ok());
    EXPECT_EQ(target->readers(), 1u) << "the image the run held was leased again";
    EXPECT_NE(reads.find(target->identity()), nullptr);

    reads.release();
    const bool answered = answers(writer);
    if (!answered)
    {
        cleanup.request_stop();
    }
    ASSERT_TRUE(answered);
    EXPECT_TRUE(writer.get().hasValue());
}
