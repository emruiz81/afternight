// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Core/Status.h.
//
// Three properties are worth more than the accessors, and each is tested by measurement rather than
// by asking the type to agree with itself: the success path allocates nothing, a failure that
// carries only a code allocates nothing either, and copies of one failure share one immutable
// payload. The rest is the vocabulary behaving as documented - a failure that says nothing still
// says something, an incoherent failure is turned into a loud one, and a cause chain terminates.

#include <AfterNightEngine/Core/Status.h>

#include "AllocationCounter.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>

namespace
{

TEST(StatusTest, TheDefaultIsSuccessAndCarriesNothing)
{
    const an::Status status;
    EXPECT_TRUE(status.ok());
    EXPECT_FALSE(status.failed());
    EXPECT_EQ(status.code(), an::ErrorCode::Ok);
    EXPECT_EQ(status.category(), an::ErrorCategory::None);
    EXPECT_FALSE(status.hasDetail());
    EXPECT_FALSE(status.hasCause());
    EXPECT_EQ(status.detail(), nullptr);
    EXPECT_EQ(status.domain(), an::ErrorDomain::Unspecified);
    EXPECT_STREQ(status.operation(), "");
    EXPECT_STREQ(status.message(), an::errorCodeDescription(an::ErrorCode::Ok));
    EXPECT_TRUE(an::Status::success().ok());
}

TEST(StatusTest, TheSuccessPathIsNoexcept)
{
    // The property the engine's hot paths depend on: producing, copying, moving and reading a
    // successful status cannot throw, so no caller has to write an error path for reporting that
    // there was no error.
    static_assert(std::is_nothrow_default_constructible_v<an::Status>);
    static_assert(std::is_nothrow_copy_constructible_v<an::Status>);
    static_assert(std::is_nothrow_move_constructible_v<an::Status>);
    static_assert(std::is_nothrow_copy_assignable_v<an::Status>);
    static_assert(std::is_nothrow_move_assignable_v<an::Status>);
    static_assert(std::is_nothrow_destructible_v<an::Status>);
    static_assert(noexcept(an::Status::success()));
    static_assert(noexcept(an::Status::failure(an::ErrorCode::Cancelled)));

    const an::Status status;
    static_assert(noexcept(status.ok()));
    static_assert(noexcept(status.failed()));
    static_assert(noexcept(status.code()));
    static_assert(noexcept(status.category()));
    static_assert(noexcept(status.message()));
    static_assert(noexcept(status.operation()));
    static_assert(noexcept(status.domain()));
    static_assert(noexcept(status.detail()));
    static_assert(noexcept(status.hasDetail()));
    static_assert(noexcept(status.hasCause()));
    static_assert(noexcept(status.cause()));
    static_assert(noexcept(status.rootCause()));
    static_assert(noexcept(status.isCancellation()));
    static_assert(noexcept(status.findContext(an::ContextKey::Limit)));
    EXPECT_TRUE(status.ok());
}

TEST(StatusTest, SuccessAndACodeOnlyFailureAllocateNothing)
{
    // Measured rather than asserted. A code-only failure has to be free of allocation because it is
    // what an out-of-memory path reports with, and a translator that allocated in order to say the
    // allocator refused would have nothing to say.
    an::ErrorCode observed = an::ErrorCode::Ok;
    std::uint64_t allocations = 0u;
    {
        const testsupport::ScopedAllocationCounter counter;
        const an::Status success = an::Status::success();
        const an::Status failure = an::Status::failure(an::ErrorCode::AllocationFailed);
        an::Status copy = failure;
        const an::Status moved = std::move(copy);
        observed = moved.code();
        allocations = counter.count();
    }
    EXPECT_EQ(allocations, 0u);
    EXPECT_EQ(observed, an::ErrorCode::AllocationFailed);
}

TEST(StatusTest, ACodeOnlyFailureStillExplainsItself)
{
    const an::Status status = an::Status::failure(an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_TRUE(status.failed());
    EXPECT_FALSE(status.hasDetail());
    EXPECT_STREQ(status.message(), an::errorCodeDescription(an::ErrorCode::MemoryBudgetExceeded));
    EXPECT_EQ(status.category(), an::ErrorCategory::Resource);
}

TEST(StatusTest, AFailureThatClaimsSuccessIsReportedAsADefectInstead)
{
    // The alternative is a status that every caller reads as success while it is being returned as
    // a failure, which is the one wrong answer that cannot be noticed at the call site.
    const an::Status status = an::Status::failure(an::ErrorCode::Ok);
    EXPECT_TRUE(status.failed());
    EXPECT_EQ(status.code(), an::ErrorCode::Internal);

    const an::Status withMessage = an::Status::failure(an::ErrorCode::Ok, std::string("built without a code"));
    EXPECT_EQ(withMessage.code(), an::ErrorCode::Internal);
    EXPECT_STREQ(withMessage.message(), "built without a code");
}

TEST(StatusTest, AMessageOverridesTheCodesOwnDescription)
{
    const an::Status status =
        an::Status::failure(an::ErrorCode::InvalidArgument, std::string("the stride is below one row of samples"));
    EXPECT_STREQ(status.message(), "the stride is below one row of samples");
    EXPECT_TRUE(status.hasDetail());
    EXPECT_STREQ(status.operation(), "");

    // An empty message is not a message, so the description is still what a log gets.
    const an::Status empty = an::Status::failure(an::ErrorCode::InvalidArgument, std::string());
    EXPECT_STREQ(empty.message(), an::errorCodeDescription(an::ErrorCode::InvalidArgument));
}

TEST(StatusTest, DetailCarriesTheOperationTheDomainAndTypedContext)
{
    const an::Status status = an::Status::failure(
        an::ErrorCode::MemoryBudgetExceeded,
        an::StatusDetail{
            .message = "the frame does not fit the configured budget",
            .operation = "ImageStorage::allocate",
            .domain = an::ErrorDomain::Memory,
            .context = {an::StatusContextField::ofCount(an::ContextKey::Requested, 8ull * 1024ull * 1024ull),
                        an::StatusContextField::ofCount(an::ContextKey::Limit, 4ull * 1024ull * 1024ull)}});

    EXPECT_EQ(status.domain(), an::ErrorDomain::Memory);
    EXPECT_STREQ(status.operation(), "ImageStorage::allocate");
    ASSERT_NE(status.detail(), nullptr);
    EXPECT_EQ(status.detail()->context.size(), 2u);

    const an::StatusContextField* requested = status.findContext(an::ContextKey::Requested);
    ASSERT_NE(requested, nullptr);
    EXPECT_EQ(requested->kind, an::ContextValueKind::Count);
    EXPECT_EQ(requested->count, 8ull * 1024ull * 1024ull);

    const an::StatusContextField* limit = status.findContext(an::ContextKey::Limit);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(limit->count, 4ull * 1024ull * 1024ull);

    // The question a caller answers by tiling is answerable without reading the message.
    EXPECT_GT(requested->count, limit->count);
    EXPECT_EQ(status.findContext(an::ContextKey::Path), nullptr);
}

TEST(StatusTest, EachContextKindSetsItsOwnValueAndLeavesTheOthersAtZero)
{
    const an::StatusContextField text = an::StatusContextField::ofText(an::ContextKey::Path, "/data/frame.fits");
    EXPECT_EQ(text.kind, an::ContextValueKind::Text);
    EXPECT_EQ(text.text, "/data/frame.fits");
    EXPECT_EQ(text.count, 0u);
    EXPECT_EQ(text.number, 0);

    const an::StatusContextField count = an::StatusContextField::ofCount(an::ContextKey::Requested, 4096u);
    EXPECT_EQ(count.kind, an::ContextValueKind::Count);
    EXPECT_EQ(count.count, 4096u);
    EXPECT_TRUE(count.text.empty());

    // Signed on purpose: an external library's own status code is routinely negative, and an
    // unsigned field would report it as an enormous count.
    const an::StatusContextField number = an::StatusContextField::ofNumber(an::ContextKey::LibraryCode, -105);
    EXPECT_EQ(number.kind, an::ContextValueKind::Number);
    EXPECT_EQ(number.number, -105);
    EXPECT_EQ(number.count, 0u);
}

TEST(StatusTest, EveryContextKeyCanBeCarriedAndFoundAgain)
{
    // Every key exists because a caller acts on it differently, so each one is exercised here
    // rather than only the two that happened to be needed first. The pairing matters as much as
    // the presence: a budget refusal is answerable because `Requested`, `Available` and `Limit`
    // arrive together, and how far an operation got is `Completed` against `Total`.
    an::StatusDetail detail;
    detail.message = "the run stopped part way through";
    detail.context = {an::StatusContextField::ofText(an::ContextKey::Parameter, "rowStrideBytes"),
                      an::StatusContextField::ofText(an::ContextKey::Path, "/data/light_0007.fits"),
                      an::StatusContextField::ofText(an::ContextKey::Library, "cfitsio"),
                      an::StatusContextField::ofNumber(an::ContextKey::LibraryCode, -105),
                      an::StatusContextField::ofCount(an::ContextKey::Requested, 4096u),
                      an::StatusContextField::ofCount(an::ContextKey::Available, 1024u),
                      an::StatusContextField::ofCount(an::ContextKey::Limit, 2048u),
                      an::StatusContextField::ofCount(an::ContextKey::Completed, 7u),
                      an::StatusContextField::ofCount(an::ContextKey::Total, 40u)};
    const an::Status status = an::Status::failure(an::ErrorCode::Cancelled, std::move(detail));

    const an::ContextKey keys[] = {an::ContextKey::Parameter,   an::ContextKey::Path,      an::ContextKey::Library,
                                   an::ContextKey::LibraryCode, an::ContextKey::Requested, an::ContextKey::Available,
                                   an::ContextKey::Limit,       an::ContextKey::Completed, an::ContextKey::Total};
    for (const an::ContextKey key : keys)
    {
        const an::StatusContextField* field = status.findContext(key);
        ASSERT_NE(field, nullptr) << "a context key was carried and could not be found again";
        EXPECT_EQ(field->key, key);
    }
    ASSERT_NE(status.detail(), nullptr);
    EXPECT_EQ(status.detail()->context.size(), sizeof(keys) / sizeof(keys[0]));

    // How far a cancelled run got, without the failure carrying a partial result.
    EXPECT_EQ(status.findContext(an::ContextKey::Completed)->count, 7u);
    EXPECT_LT(status.findContext(an::ContextKey::Completed)->count, status.findContext(an::ContextKey::Total)->count);
    EXPECT_TRUE(status.isCancellation());
}

TEST(StatusTest, TheFirstFieldWithAKeyIsTheOneFound)
{
    const an::Status status =
        an::Status::failure(an::ErrorCode::LimitExceeded,
                            an::StatusDetail{.context = {an::StatusContextField::ofCount(an::ContextKey::Limit, 1u),
                                                         an::StatusContextField::ofCount(an::ContextKey::Limit, 2u)}});
    const an::StatusContextField* found = status.findContext(an::ContextKey::Limit);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->count, 1u);
}

TEST(StatusTest, CopiesShareOnePayloadRatherThanDuplicatingIt)
{
    const an::Status original =
        an::Status::failure(an::ErrorCode::InvalidData, std::string("the header declares no axes"));

    std::uint64_t allocations = 0u;
    const an::StatusDetail* originalDetail = original.detail();
    const an::StatusDetail* copiedDetail = nullptr;
    {
        const testsupport::ScopedAllocationCounter counter;
        const an::Status copy = original;
        copiedDetail = copy.detail();
        allocations = counter.count();
    }
    EXPECT_EQ(allocations, 0u);
    EXPECT_EQ(originalDetail, copiedDetail);
    EXPECT_STREQ(original.message(), "the header declares no axes");
}

TEST(StatusTest, ACauseIsKeptWholeAndTheChainTerminates)
{
    const an::Status libraryFailure =
        an::Status::failure(an::ErrorCode::ExternalFailure, an::StatusDetail{.message = "the codec refused the file",
                                                                             .operation = "FitsDecoder::open",
                                                                             .domain = an::ErrorDomain::Io});
    const an::Status decodeFailure = an::Status::failure(an::ErrorCode::InvalidData,
                                                         an::StatusDetail{.message = "the frame could not be decoded",
                                                                          .operation = "ImageLoader::load",
                                                                          .domain = an::ErrorDomain::Io},
                                                         libraryFailure);

    EXPECT_EQ(decodeFailure.code(), an::ErrorCode::InvalidData);
    ASSERT_TRUE(decodeFailure.hasCause());
    EXPECT_EQ(decodeFailure.cause().code(), an::ErrorCode::ExternalFailure);
    EXPECT_STREQ(decodeFailure.cause().message(), "the codec refused the file");
    EXPECT_FALSE(decodeFailure.cause().hasCause());

    // Both levels survive, which is the point: the layer that noticed and the thing that actually
    // went wrong are different sentences and a report may want either.
    EXPECT_EQ(decodeFailure.rootCause().code(), an::ErrorCode::ExternalFailure);
    EXPECT_STREQ(decodeFailure.rootCause().operation(), "FitsDecoder::open");
    EXPECT_EQ(libraryFailure.rootCause().code(), libraryFailure.code());
}

TEST(StatusTest, ASuccessfulStatusIsNotACause)
{
    const an::Status status =
        an::Status::failure(an::ErrorCode::Cancelled, an::StatusDetail{.message = "stopped"}, an::Status::success());
    EXPECT_FALSE(status.hasCause());
    EXPECT_TRUE(status.cause().ok());
    EXPECT_EQ(status.rootCause().code(), an::ErrorCode::Cancelled);
}

TEST(StatusTest, CancellationIsDistinguishableFromAFailureWithoutReadingAMessage)
{
    const an::Status cancelled = an::Status::failure(an::ErrorCode::Cancelled);
    const an::Status deadline = an::Status::failure(an::ErrorCode::DeadlineExceeded);
    const an::Status refused = an::Status::failure(an::ErrorCode::InvalidArgument);

    EXPECT_TRUE(cancelled.isCancellation());
    EXPECT_TRUE(deadline.isCancellation());
    EXPECT_FALSE(refused.isCancellation());
    EXPECT_FALSE(an::Status::success().isCancellation());
    EXPECT_NE(cancelled.code(), deadline.code());
}

TEST(StatusTest, AMovedFromStatusIsStillReadable)
{
    // Not a contract anybody should rely on, and worth pinning anyway: a status left behind by a
    // move must not be a status that crashes the reporting path that touches it.
    an::Status source = an::Status::failure(an::ErrorCode::InvalidState, std::string("gone"));
    const an::Status destination = std::move(source);
    EXPECT_EQ(destination.code(), an::ErrorCode::InvalidState);
    EXPECT_NE(source.message(), nullptr);
}

} // namespace

TEST(StatusTest, RealContextPreservesFractionalParameters)
{
    const auto field = an::StatusContextField::ofReal(an::ContextKey::Requested, -0.125);
    EXPECT_EQ(field.kind, an::ContextValueKind::Real);
    EXPECT_DOUBLE_EQ(field.real, -0.125);
    EXPECT_EQ(field.count, 0u);
    EXPECT_EQ(field.number, 0);
}
