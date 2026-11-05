// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Admission: how a session divides its threads between operations. At width one every operation is serialized in a
/// bounded queue in arrival order; above it a preview reservation exists only while a preview is admitted, waiting or
/// held open by a dialog, and is what a preview is sure of - while no other work waits a preview takes every free
/// thread but one unless it asks for fewer, and work that can wait waits for the threads it borrowed rather than start
/// narrower, and only for those; a full queue, a
/// stopped request and a passed deadline are answered rather than waited out; background work never waits; and no
/// operation is granted zero threads.
///
/// Every case that makes a request wait gives it a stop token the case requests on its way out, so a request the
/// session never answers fails the case instead of hanging the suite.

#include "ConsumerRuntimeTestSupport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <mutex>
#include <stop_token>
#include <vector>

namespace
{

using an::consumer::EngineSession;
using an::consumer::OperationPurpose;
using an::consumer::OperationRequest;
using an::consumer::OperationScope;
using an::tests::consumer::answers;
using an::tests::consumer::eventually;
using an::tests::consumer::openSession;
using an::tests::consumer::stillWaiting;

/// Requests a stop when the case leaves, releasing any request still waiting.
class StopOnExit
{
public:
    explicit StopOnExit(std::stop_source& source) noexcept : m_source(source) {}
    StopOnExit(const StopOnExit&) = delete;
    StopOnExit& operator=(const StopOnExit&) = delete;
    ~StopOnExit()
    {
        m_source.request_stop();
    }

private:
    std::stop_source& m_source;
};

[[nodiscard]] OperationRequest request(OperationPurpose purpose, std::uint32_t maxConcurrency = 0u,
                                       std::stop_token stop = {})
{
    return OperationRequest{.purpose = purpose, .maxConcurrency = maxConcurrency, .stopToken = std::move(stop)};
}

/// A request that waits, made on another thread.
[[nodiscard]] std::future<an::Result<OperationScope>> admitLater(const EngineSession& session, OperationRequest waiting)
{
    return std::async(std::launch::async, [&session, waiting = std::move(waiting)] { return session.admit(waiting); });
}

} // namespace

TEST(AdmissionTest, ThePreviewReservationIsNoneAtWidthOneOneUpToFourAndAQuarterAbove)
{
    EXPECT_EQ(an::consumer::previewReservationWidth(0u), 0u);
    EXPECT_EQ(an::consumer::previewReservationWidth(1u), 0u);
    EXPECT_EQ(an::consumer::previewReservationWidth(2u), 1u);
    EXPECT_EQ(an::consumer::previewReservationWidth(4u), 1u);
    EXPECT_EQ(an::consumer::previewReservationWidth(5u), 1u);
    EXPECT_EQ(an::consumer::previewReservationWidth(7u), 1u);
    EXPECT_EQ(an::consumer::previewReservationWidth(8u), 2u);
    EXPECT_EQ(an::consumer::previewReservationWidth(16u), 4u);
    EXPECT_EQ(an::consumer::previewReservationWidth(17u), 4u);
    EXPECT_EQ(an::consumer::previewReservationWidth(32u), 8u);
}

TEST(AdmissionTest, AtWidthOneASecondOperationWaitsForTheFirstToEnd)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    an::Result<OperationScope> first = session->admit(request(OperationPurpose::Apply));
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    EXPECT_EQ(first->grantedConcurrency(), 1u);

    std::stop_source cleanup;
    std::future<an::Result<OperationScope>> second =
        admitLater(*session, request(OperationPurpose::Batch, 0u, cleanup.get_token()));
    const StopOnExit guard(cleanup);
    ASSERT_TRUE(eventually([&session] { return session->admission().queued == 1u; }));
    EXPECT_TRUE(stillWaiting(second)) << "a second operation ran beside the first at width one";

    first->end();
    ASSERT_TRUE(answers(second));
    an::Result<OperationScope> admitted = second.get();
    ASSERT_TRUE(admitted.hasValue()) << admitted.status().message();
    EXPECT_EQ(admitted->grantedConcurrency(), 1u);
    EXPECT_EQ(admitted->execution().admittedConcurrency(), 1u);
    EXPECT_EQ(admitted->purpose(), OperationPurpose::Batch);
    EXPECT_EQ(session->admission().queued, 0u);
}

TEST(AdmissionTest, AtWidthOneWaitingOperationsAreAdmittedInArrivalOrder)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    an::Result<OperationScope> first = session->admit(request(OperationPurpose::Apply));
    ASSERT_TRUE(first.hasValue());

    std::stop_source cleanup;
    std::mutex orderMutex;
    std::vector<int> order;
    std::vector<std::future<void>> waiters;
    const StopOnExit guard(cleanup);
    // Preview, batch and workflow work all share the one thread, so arrival order alone decides.
    const OperationPurpose purposes[] = {OperationPurpose::Preview, OperationPurpose::WorkflowStage,
                                         OperationPurpose::Batch};
    for (int index = 0; index < 3; ++index)
    {
        waiters.push_back(std::async(std::launch::async, [&, index] {
            an::Result<OperationScope> scope = session->admit(request(purposes[index], 0u, cleanup.get_token()));
            if (scope.hasValue())
            {
                const std::lock_guard<std::mutex> lock(orderMutex);
                order.push_back(index);
            }
        }));
        ASSERT_TRUE(eventually([&session, index] { return session->admission().queued == std::uint64_t(index) + 1u; }));
    }

    first->end();
    for (std::future<void>& waiter : waiters)
    {
        ASSERT_TRUE(answers(waiter));
    }
    const std::lock_guard<std::mutex> lock(orderMutex);
    EXPECT_EQ(order, (std::vector<int>{0, 1, 2}));
}

TEST(AdmissionTest, AtWidthOneAPreviewDialogReservesNothingAndAPreviewTakesItsTurn)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::consumer::PreviewReservation> dialog = session->reservePreview();
    ASSERT_TRUE(dialog.hasValue());
    EXPECT_EQ(session->admission().previewReservation, 0u);

    an::Result<OperationScope> apply = session->tryAdmit(request(OperationPurpose::Apply));
    ASSERT_TRUE(apply.hasValue()) << "an open preview dialog kept the only thread from other work";
    EXPECT_EQ(session->tryAdmit(request(OperationPurpose::Preview)).code(), an::ErrorCode::Busy);
    apply->end();
    an::Result<OperationScope> preview = session->tryAdmit(request(OperationPurpose::Preview));
    ASSERT_TRUE(preview.hasValue());
    EXPECT_EQ(preview->grantedConcurrency(), 1u);
}

TEST(AdmissionTest, NoOperationIsGrantedZeroThreads)
{
    std::optional<EngineSession> narrow = openSession(1u);
    ASSERT_TRUE(narrow.has_value());
    for (const OperationPurpose purpose : {OperationPurpose::Preview, OperationPurpose::Apply, OperationPurpose::Batch,
                                           OperationPurpose::WorkflowStage, OperationPurpose::Background})
    {
        an::Result<OperationScope> scope = narrow->tryAdmit(request(purpose));
        ASSERT_TRUE(scope.hasValue()) << an::consumer::operationPurposeName(purpose);
        EXPECT_EQ(scope->grantedConcurrency(), 1u) << an::consumer::operationPurposeName(purpose);
    }

    std::optional<EngineSession> wide = openSession(4u);
    ASSERT_TRUE(wide.has_value());
    an::Result<OperationScope> asked = wide->tryAdmit(request(OperationPurpose::Apply, 2u));
    ASSERT_TRUE(asked.hasValue());
    EXPECT_EQ(asked->grantedConcurrency(), 2u);
    asked->end();
    an::Result<OperationScope> tooMany = wide->tryAdmit(request(OperationPurpose::Apply, 9u));
    ASSERT_TRUE(tooMany.hasValue()) << "a request wider than the session is narrowed, not refused";
    EXPECT_EQ(tooMany->grantedConcurrency(), 4u);
}

TEST(AdmissionTest, AFullQueueRefusesAsBusyAtOnce)
{
    an::consumer::EngineSessionSettings settings = an::tests::consumer::settingsOfWidth(1u);
    settings.admissionQueueCapacity = 2u;
    an::Result<EngineSession> session = EngineSession::open(settings);
    ASSERT_TRUE(session.hasValue());
    an::Result<OperationScope> first = session->admit(request(OperationPurpose::Apply));
    ASSERT_TRUE(first.hasValue());

    std::stop_source cleanup;
    std::future<an::Result<OperationScope>> one =
        admitLater(*session, request(OperationPurpose::Apply, 0u, cleanup.get_token()));
    std::future<an::Result<OperationScope>> two =
        admitLater(*session, request(OperationPurpose::Apply, 0u, cleanup.get_token()));
    const StopOnExit guard(cleanup);
    ASSERT_TRUE(eventually([&session] { return session->admission().queued == 2u; }));

    std::future<an::Result<OperationScope>> third =
        admitLater(*session, request(OperationPurpose::Apply, 0u, cleanup.get_token()));
    const StopOnExit thirdGuard(cleanup);
    ASSERT_TRUE(answers(third)) << "a full queue made the request wait";
    const an::Result<OperationScope> refused = third.get();
    ASSERT_EQ(refused.code(), an::ErrorCode::Busy);
    ASSERT_NE(refused.status().findContext(an::ContextKey::Limit), nullptr);
    EXPECT_EQ(refused.status().findContext(an::ContextKey::Limit)->count, 2u);
    EXPECT_EQ(session->admission().refusedBusy, 1u);
    EXPECT_EQ(session->admission().queued, 2u);
}

TEST(AdmissionTest, AStoppedRequestLeavesTheQueue)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    an::Result<OperationScope> first = session->admit(request(OperationPurpose::Apply));
    ASSERT_TRUE(first.hasValue());

    std::stop_source stop;
    std::future<an::Result<OperationScope>> waiting =
        admitLater(*session, request(OperationPurpose::Batch, 0u, stop.get_token()));
    const StopOnExit guard(stop);
    ASSERT_TRUE(eventually([&session] { return session->admission().queued == 1u; }));
    stop.request_stop();
    ASSERT_TRUE(answers(waiting));
    EXPECT_EQ(waiting.get().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(session->admission().queued, 0u);
    EXPECT_EQ(session->admission().cancelledWhileQueued, 1u);

    // A request stopped before it asks is not admitted at all.
    std::stop_source already;
    already.request_stop();
    EXPECT_EQ(session->tryAdmit(request(OperationPurpose::Apply, 0u, already.get_token())).code(),
              an::ErrorCode::Cancelled);
}

TEST(AdmissionTest, ADeadlineEndsTheWait)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    an::Result<OperationScope> first = session->admit(request(OperationPurpose::Apply));
    ASSERT_TRUE(first.hasValue());

    std::stop_source cleanup;
    OperationRequest late = request(OperationPurpose::Batch, 0u, cleanup.get_token());
    late.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    std::future<an::Result<OperationScope>> waiting = admitLater(*session, late);
    const StopOnExit guard(cleanup);
    ASSERT_TRUE(answers(waiting));
    EXPECT_EQ(waiting.get().code(), an::ErrorCode::DeadlineExceeded);
    EXPECT_EQ(session->admission().expiredWhileQueued, 1u);
    EXPECT_EQ(session->admission().queued, 0u);
}

TEST(AdmissionTest, TryAdmitNeverWaits)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    an::Result<OperationScope> first = session->admit(request(OperationPurpose::Apply));
    ASSERT_TRUE(first.hasValue());
    const an::Result<OperationScope> refused = session->tryAdmit(request(OperationPurpose::Apply));
    EXPECT_EQ(refused.code(), an::ErrorCode::Busy);
    EXPECT_EQ(session->admission().queued, 0u);
    EXPECT_EQ(session->admission().refusedBusy, 1u);
}

TEST(AdmissionTest, WithNoPreviewOpenOtherWorkIsGrantedTheWholeWidth)
{
    std::optional<EngineSession> session = openSession(8u);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->admission().previewReservation, 0u);
    an::Result<OperationScope> workflow = session->tryAdmit(request(OperationPurpose::WorkflowStage));
    ASSERT_TRUE(workflow.hasValue());
    EXPECT_EQ(workflow->grantedConcurrency(), 8u);
    EXPECT_EQ(workflow->execution().admittedConcurrency(), 8u);
}

TEST(AdmissionTest, AnOpenPreviewDialogKeepsItsReservationFromOtherWork)
{
    std::optional<EngineSession> session = openSession(8u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::consumer::PreviewReservation> dialog = session->reservePreview();
    ASSERT_TRUE(dialog.hasValue());
    EXPECT_EQ(session->admission().previewReservation, 2u);

    an::Result<OperationScope> apply = session->tryAdmit(request(OperationPurpose::Apply));
    ASSERT_TRUE(apply.hasValue());
    EXPECT_EQ(apply->grantedConcurrency(), 6u);
    EXPECT_EQ(session->tryAdmit(request(OperationPurpose::Batch)).code(), an::ErrorCode::Busy)
        << "other work took a reserved thread";

    an::Result<OperationScope> preview = session->tryAdmit(request(OperationPurpose::Preview));
    ASSERT_TRUE(preview.hasValue());
    EXPECT_EQ(preview->grantedConcurrency(), 2u);
}

TEST(AdmissionTest, PreviewsShareOneReservation)
{
    std::optional<EngineSession> session = openSession(16u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::consumer::PreviewReservation> first = session->reservePreview();
    an::Result<an::consumer::PreviewReservation> second = session->reservePreview();
    ASSERT_TRUE(first.hasValue() && second.hasValue());
    EXPECT_EQ(session->admission().previewReservation, 4u) << "two dialogs share one reservation";

    an::Result<OperationScope> other = session->tryAdmit(request(OperationPurpose::Batch));
    ASSERT_TRUE(other.hasValue());
    EXPECT_EQ(other->grantedConcurrency(), 12u);

    an::Result<OperationScope> one = session->tryAdmit(request(OperationPurpose::Preview, 1u));
    an::Result<OperationScope> rest = session->tryAdmit(request(OperationPurpose::Preview));
    ASSERT_TRUE(one.hasValue() && rest.hasValue());
    EXPECT_EQ(one->grantedConcurrency(), 1u);
    EXPECT_EQ(rest->grantedConcurrency(), 3u);
    EXPECT_EQ(session->tryAdmit(request(OperationPurpose::Preview)).code(), an::ErrorCode::Busy);
    EXPECT_EQ(session->admission().previewThreads, 4u);
}

// A preview admitted with no dialog open makes the reservation exist for as long as it runs.
TEST(AdmissionTest, AnAdmittedPreviewReservesItsShareWhileItRuns)
{
    std::optional<EngineSession> session = openSession(8u);
    ASSERT_TRUE(session.has_value());
    an::Result<OperationScope> preview = session->tryAdmit(request(OperationPurpose::Preview, 2u));
    ASSERT_TRUE(preview.hasValue());
    EXPECT_EQ(preview->grantedConcurrency(), 2u);
    EXPECT_EQ(session->admission().previewReservation, 2u);
    an::Result<OperationScope> apply = session->tryAdmit(request(OperationPurpose::Apply));
    ASSERT_TRUE(apply.hasValue());
    EXPECT_EQ(apply->grantedConcurrency(), 6u);

    preview->end();
    EXPECT_EQ(session->admission().previewReservation, 0u);
    an::Result<OperationScope> more = session->tryAdmit(request(OperationPurpose::Apply));
    ASSERT_TRUE(more.hasValue());
    EXPECT_EQ(more->grantedConcurrency(), 2u);
}

// The reservation is what a preview is sure of, not all it may take: with nothing else running, a preview is granted
// every thread of the session but one, which stays for the work that never waits.
TEST(AdmissionTest, APreviewWithNothingElseRunningIsGrantedEveryThreadButOne)
{
    std::optional<EngineSession> session = openSession(8u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::consumer::PreviewReservation> dialog = session->reservePreview();
    ASSERT_TRUE(dialog.hasValue());
    an::Result<OperationScope> preview = session->tryAdmit(request(OperationPurpose::Preview));
    ASSERT_TRUE(preview.hasValue());
    EXPECT_EQ(preview->grantedConcurrency(), 7u);
    EXPECT_EQ(preview->execution().admittedConcurrency(), 7u);
    EXPECT_EQ(session->admission().previewReservation, 2u) << "the reservation is still the dialog's";

    an::Result<OperationScope> background = session->tryAdmit(request(OperationPurpose::Background));
    ASSERT_TRUE(background.hasValue()) << "work that never waits found no thread beside a borrowing preview";
    EXPECT_EQ(background->grantedConcurrency(), 1u);
}

// A preview borrows only what other work leaves: the threads nothing holds, but one.
TEST(AdmissionTest, APreviewBorrowsOnlyTheThreadsOtherWorkLeavesFree)
{
    std::optional<EngineSession> session = openSession(8u);
    ASSERT_TRUE(session.has_value());
    an::Result<OperationScope> apply = session->tryAdmit(request(OperationPurpose::Apply, 3u));
    ASSERT_TRUE(apply.hasValue());
    an::Result<OperationScope> preview = session->tryAdmit(request(OperationPurpose::Preview));
    ASSERT_TRUE(preview.hasValue());
    EXPECT_EQ(preview->grantedConcurrency(), 4u);
    EXPECT_EQ(session->tryAdmit(request(OperationPurpose::Preview)).code(), an::ErrorCode::Busy);
}

// A preview that asks for the reservation - as a preview frame of a dragged slider does - is granted that and borrows
// nothing past it, however many threads are free; the threads it leaves are there for the next preview to borrow.
TEST(AdmissionTest, APreviewThatAsksForTheReservationIsGrantedItWhereItCouldBorrowMore)
{
    std::optional<EngineSession> session = openSession(8u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::consumer::PreviewReservation> dialog = session->reservePreview();
    ASSERT_TRUE(dialog.hasValue());
    const std::uint32_t reservation = an::consumer::previewReservationWidth(8u);
    ASSERT_EQ(reservation, 2u);

    an::Result<OperationScope> asking = session->tryAdmit(request(OperationPurpose::Preview, reservation));
    ASSERT_TRUE(asking.hasValue());
    EXPECT_EQ(asking->grantedConcurrency(), reservation);
    EXPECT_EQ(asking->execution().admittedConcurrency(), reservation);
    EXPECT_EQ(session->admission().previewThreads, reservation);

    an::Result<OperationScope> presenting = session->tryAdmit(request(OperationPurpose::Preview));
    ASSERT_TRUE(presenting.hasValue()) << presenting.status().message();
    EXPECT_EQ(presenting->grantedConcurrency(), 5u) << "every thread free but the one kept for work that never waits";
}

// Work that can wait is not held back by a preview that asked for no more than the reservation: nothing is borrowed,
// so it is granted its whole share at once, where a borrowing preview would have made it wait.
TEST(AdmissionTest, OtherWorkDoesNotWaitForAPreviewThatBorrowsNothing)
{
    std::optional<EngineSession> session = openSession(8u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::consumer::PreviewReservation> dialog = session->reservePreview();
    ASSERT_TRUE(dialog.hasValue());
    an::Result<OperationScope> preview =
        session->tryAdmit(request(OperationPurpose::Preview, an::consumer::previewReservationWidth(8u)));
    ASSERT_TRUE(preview.hasValue());

    std::stop_source cleanup;
    std::future<an::Result<OperationScope>> apply =
        admitLater(*session, request(OperationPurpose::Apply, 0u, cleanup.get_token()));
    const StopOnExit guard(cleanup);
    ASSERT_TRUE(answers(apply)) << "the work waited for a preview that borrowed nothing";
    an::Result<OperationScope> applied = apply.get();
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();
    EXPECT_EQ(applied->grantedConcurrency(), 6u);
    EXPECT_EQ(session->admission().queued, 0u);
}

// A request that does not wait takes what a borrowing preview leaves, as it takes what any other work leaves.
TEST(AdmissionTest, ARequestThatDoesNotWaitTakesWhatABorrowingPreviewLeaves)
{
    std::optional<EngineSession> session = openSession(8u);
    ASSERT_TRUE(session.has_value());
    an::Result<OperationScope> borrowing = session->tryAdmit(request(OperationPurpose::Preview));
    ASSERT_TRUE(borrowing.hasValue());
    ASSERT_EQ(borrowing->grantedConcurrency(), 7u);
    an::Result<OperationScope> apply = session->tryAdmit(request(OperationPurpose::Apply, 8u));
    ASSERT_TRUE(apply.hasValue()) << apply.status().message();
    EXPECT_EQ(apply->grantedConcurrency(), 1u);

    // The sharp edge `tryAdmit` states: a grant is never widened, so the work runs on its one thread to its end, though
    // the preview gives back every thread it borrowed.
    *borrowing = OperationScope{};
    EXPECT_EQ(apply->grantedConcurrency(), 1u);
    EXPECT_EQ(apply->execution().admittedConcurrency(), 1u);
    an::Result<OperationScope> next = session->tryAdmit(request(OperationPurpose::Apply, 8u));
    ASSERT_TRUE(next.hasValue()) << next.status().message();
    EXPECT_EQ(next->grantedConcurrency(), 7u) << "the threads the preview gave back go to the next request";
}

// A grant is never widened, so work that can wait does not start on the thread a borrowing preview leaves: it waits
// for the borrowed threads, and the previews after it are held to the reservation meanwhile, so when they come back the
// waiting work is granted its share and the next preview the reservation.
TEST(AdmissionTest, OtherWorkWaitingForBorrowedThreadsTakesItsShareWhenTheyComeBack)
{
    std::optional<EngineSession> session = openSession(8u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::consumer::PreviewReservation> dialog = session->reservePreview();
    ASSERT_TRUE(dialog.hasValue());
    an::Result<OperationScope> borrowing = session->tryAdmit(request(OperationPurpose::Preview));
    ASSERT_TRUE(borrowing.hasValue());
    ASSERT_EQ(borrowing->grantedConcurrency(), 7u);

    std::stop_source cleanup;
    std::future<an::Result<OperationScope>> apply =
        admitLater(*session, request(OperationPurpose::Apply, 0u, cleanup.get_token()));
    const StopOnExit guard(cleanup);
    ASSERT_TRUE(eventually([&session] { return session->admission().queued == 1u; }));
    EXPECT_TRUE(stillWaiting(apply)) << "the work started on the one thread the borrowing preview left";
    std::future<an::Result<OperationScope>> next =
        admitLater(*session, request(OperationPurpose::Preview, 0u, cleanup.get_token()));
    ASSERT_TRUE(eventually([&session] { return session->admission().queued == 2u; }));
    EXPECT_TRUE(stillWaiting(next)) << "a preview took a thread the waiting work was owed";

    borrowing->end();
    ASSERT_TRUE(answers(apply));
    ASSERT_TRUE(answers(next));
    an::Result<OperationScope> applied = apply.get();
    an::Result<OperationScope> previewed = next.get();
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();
    ASSERT_TRUE(previewed.hasValue()) << previewed.status().message();
    EXPECT_EQ(applied->grantedConcurrency(), 6u);
    EXPECT_EQ(previewed->grantedConcurrency(), 2u);
}

// A grant is never taken back. A workflow stage started while a preview is open keeps its narrower grant until its
// operation ends, and the next stage is granted the whole width again.
TEST(AdmissionTest, AWorkflowRegainsTheReservedThreadsAtItsNextOperation)
{
    std::optional<EngineSession> session = openSession(8u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::consumer::PreviewReservation> dialog = session->reservePreview();
    ASSERT_TRUE(dialog.hasValue());
    an::Result<OperationScope> stage = session->tryAdmit(request(OperationPurpose::WorkflowStage));
    ASSERT_TRUE(stage.hasValue());
    EXPECT_EQ(stage->grantedConcurrency(), 6u);

    dialog->release();
    EXPECT_EQ(session->admission().previewReservation, 0u);
    EXPECT_EQ(stage->grantedConcurrency(), 6u);
    EXPECT_EQ(stage->execution().admittedConcurrency(), 6u);

    stage->end();
    an::Result<OperationScope> next = session->tryAdmit(request(OperationPurpose::WorkflowStage));
    ASSERT_TRUE(next.hasValue());
    EXPECT_EQ(next->grantedConcurrency(), 8u);
}

TEST(AdmissionTest, BackgroundWorkTakesOneFreeThreadAndNeverWaits)
{
    std::optional<EngineSession> session = openSession(4u);
    ASSERT_TRUE(session.has_value());
    an::Result<OperationScope> busy = session->admit(request(OperationPurpose::Apply));
    ASSERT_TRUE(busy.hasValue());
    EXPECT_EQ(busy->grantedConcurrency(), 4u);

    std::stop_source cleanup;
    std::future<an::Result<OperationScope>> background =
        admitLater(*session, request(OperationPurpose::Background, 0u, cleanup.get_token()));
    const StopOnExit guard(cleanup);
    ASSERT_TRUE(answers(background)) << "background work waited for a thread";
    EXPECT_EQ(background.get().code(), an::ErrorCode::Busy);
    EXPECT_EQ(session->admission().queued, 0u);

    busy->end();
    an::Result<OperationScope> admitted = session->admit(request(OperationPurpose::Background, 4u));
    ASSERT_TRUE(admitted.hasValue());
    EXPECT_EQ(admitted->grantedConcurrency(), 1u);
}

// A preview waiting for a thread is not overtaken by work that arrives after it.
TEST(AdmissionTest, AWaitingPreviewIsNotOvertakenByLaterWork)
{
    std::optional<EngineSession> session = openSession(4u);
    ASSERT_TRUE(session.has_value());
    an::Result<OperationScope> busy = session->admit(request(OperationPurpose::Apply));
    ASSERT_TRUE(busy.hasValue());
    EXPECT_EQ(busy->grantedConcurrency(), 4u);

    std::stop_source cleanup;
    std::future<an::Result<OperationScope>> preview =
        admitLater(*session, request(OperationPurpose::Preview, 1u, cleanup.get_token()));
    const StopOnExit guard(cleanup);
    ASSERT_TRUE(eventually([&session] { return session->admission().queued == 1u; }));
    EXPECT_EQ(session->admission().previewReservation, 1u) << "a waiting preview reserves its share";

    busy->end();
    ASSERT_TRUE(answers(preview));
    an::Result<OperationScope> admitted = preview.get();
    ASSERT_TRUE(admitted.hasValue()) << admitted.status().message();
    EXPECT_EQ(admitted->grantedConcurrency(), 1u);
    an::Result<OperationScope> later = session->tryAdmit(request(OperationPurpose::Apply));
    ASSERT_TRUE(later.hasValue());
    EXPECT_EQ(later->grantedConcurrency(), 3u);
}

TEST(AdmissionTest, PurposeNamesAreStable)
{
    EXPECT_STREQ(an::consumer::operationPurposeName(OperationPurpose::Preview), "preview");
    EXPECT_STREQ(an::consumer::operationPurposeName(OperationPurpose::Apply), "apply");
    EXPECT_STREQ(an::consumer::operationPurposeName(OperationPurpose::Batch), "batch");
    EXPECT_STREQ(an::consumer::operationPurposeName(OperationPurpose::WorkflowStage), "workflow stage");
    EXPECT_STREQ(an::consumer::operationPurposeName(OperationPurpose::Background), "background");
}
