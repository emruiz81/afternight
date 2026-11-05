// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngineGmicAdapter/GmicAdapterCapabilities.h.
//
// Every case here runs in both configurations of the adapter, which is the point of the header: the
// discovery report is the one part of an optional adapter that has to answer before anybody knows
// whether the optional part is there. So the cases are written against the relationship between the
// fields rather than against one build's values - `libraryCompiledIn` decides which states are
// reachable, and a state that disagrees with it is the failure this file exists to catch.
//
// The reason vocabulary is tested the way the engine's error codes are: a name per value, distinct
// names, and a named answer for a value from outside the enumeration. A reason is read off a failure
// through a typed field, and the case that pins that is here rather than beside the session, because
// what it proves is that no caller has to parse a message to find out which rule refused.

#include <AfterNightEngineGmicAdapter/GmicAdapterCapabilities.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>
#include <string_view>

namespace
{

constexpr an::GmicAdapterState kAllStates[] = {an::GmicAdapterState::Absent, an::GmicAdapterState::Available,
                                               an::GmicAdapterState::Loaded, an::GmicAdapterState::FailedToLoad};

constexpr an::GmicAdapterReason kAllReasons[] = {an::GmicAdapterReason::None,
                                                 an::GmicAdapterReason::LibraryNotCompiledIn,
                                                 an::GmicAdapterReason::LibraryProbeFailed,
                                                 an::GmicAdapterReason::OptionsRejected,
                                                 an::GmicAdapterReason::AdapterShutDown,
                                                 an::GmicAdapterReason::CallInFlight,
                                                 an::GmicAdapterReason::CommandEmpty,
                                                 an::GmicAdapterReason::CommandFailed,
                                                 an::GmicAdapterReason::Interrupted,
                                                 an::GmicAdapterReason::BufferBridgeRefused,
                                                 an::GmicAdapterReason::SourceNotShareable,
                                                 an::GmicAdapterReason::ResultMissing,
                                                 an::GmicAdapterReason::ResultImageCountUnexpected,
                                                 an::GmicAdapterReason::ResultGeometryRejected,
                                                 an::GmicAdapterReason::ResultFormatUnexpected,
                                                 an::GmicAdapterReason::ResultNameOverflowed,
                                                 an::GmicAdapterReason::CallThreadNotStarted};

} // namespace

TEST(GmicAdapterCapabilitiesTest, TheStateAgreesWithWhetherALibraryIsCompiledIn)
{
    const an::GmicAdapterCapabilities capabilities = an::gmicAdapterCapabilities();

    EXPECT_TRUE(an::isGmicAdapterState(capabilities.state));
    if (capabilities.libraryCompiledIn)
    {
        EXPECT_NE(capabilities.state, an::GmicAdapterState::Absent);
    }
    else
    {
        EXPECT_EQ(capabilities.state, an::GmicAdapterState::Absent);
        EXPECT_FALSE(capabilities.probed);
        EXPECT_EQ(capabilities.openSessions, 0u);
    }
}

TEST(GmicAdapterCapabilitiesTest, TheBuildFactIsTheSameEveryTimeItIsAsked)
{
    const an::GmicAdapterCapabilities first = an::gmicAdapterCapabilities();
    const an::GmicAdapterCapabilities second = an::gmicAdapterCapabilities();

    EXPECT_EQ(first.libraryCompiledIn, second.libraryCompiledIn);
    EXPECT_EQ(first.state, second.state);
}

TEST(GmicAdapterCapabilitiesTest, AskingTheLibraryOpensNothingAndProbesNothing)
{
    // The report exists so a caller can decide whether to ask for a session. A report that ran the
    // probe to answer would have made the question expensive - the probe builds an interpreter and
    // parses the library's standard commands - and the answer a side effect.
    const an::GmicAdapterCapabilities before = an::gmicAdapterCapabilities();
    for (int index = 0; index < 4; ++index)
    {
        const an::GmicAdapterCapabilities each = an::gmicAdapterCapabilities();
        EXPECT_EQ(each.openSessions, before.openSessions);
        EXPECT_EQ(each.callsInFlight, 0u);
        EXPECT_EQ(each.probed, before.probed);
    }
    EXPECT_EQ(an::gmicAdapterCapabilities().state, before.state);
}

TEST(GmicAdapterCapabilitiesTest, EveryStateAndReasonHasItsOwnName)
{
    std::set<std::string> stateNames;
    for (const an::GmicAdapterState state : kAllStates)
    {
        EXPECT_TRUE(an::isGmicAdapterState(state));
        stateNames.insert(an::gmicAdapterStateName(state));
    }
    EXPECT_EQ(stateNames.size(), std::size(kAllStates));

    std::set<std::string> reasonNames;
    for (const an::GmicAdapterReason reason : kAllReasons)
    {
        EXPECT_TRUE(an::isGmicAdapterReason(reason));
        reasonNames.insert(an::gmicAdapterReasonName(reason));
    }
    EXPECT_EQ(reasonNames.size(), std::size(kAllReasons));
}

TEST(GmicAdapterCapabilitiesTest, AValueFromOutsideTheEnumerationIsNamedAsUnknown)
{
    const auto unknownState = static_cast<an::GmicAdapterState>(200);
    const auto unknownReason = static_cast<an::GmicAdapterReason>(4000);

    EXPECT_FALSE(an::isGmicAdapterState(unknownState));
    EXPECT_FALSE(an::isGmicAdapterReason(unknownReason));
    EXPECT_STREQ(an::gmicAdapterStateName(unknownState), "UnknownGmicAdapterState");
    EXPECT_STREQ(an::gmicAdapterReasonName(unknownReason), "UnknownGmicAdapterReason");
}

TEST(GmicAdapterCapabilitiesTest, AReasonIsReadOffTheFailureRatherThanItsMessage)
{
    const an::Status failure = an::Status::failure(
        an::ErrorCode::ExternalFailure,
        an::StatusDetail{.message = "*** Error *** Unknown command or filename 'nope'.",
                         .operation = "GmicRuntimeAdapter::run",
                         .domain = an::ErrorDomain::Interop,
                         .context = {an::StatusContextField::ofText(an::ContextKey::Library, "gmic"),
                                     an::StatusContextField::ofCount(
                                         an::ContextKey::AdapterReason,
                                         static_cast<std::uint64_t>(an::GmicAdapterReason::CommandFailed))}});

    EXPECT_EQ(an::gmicAdapterReason(failure), an::GmicAdapterReason::CommandFailed);
}

TEST(GmicAdapterCapabilitiesTest, AFailureFromSomewhereElseCarriesNoReason)
{
    EXPECT_EQ(an::gmicAdapterReason(an::Status::success()), an::GmicAdapterReason::None);
    EXPECT_EQ(an::gmicAdapterReason(an::Status::failure(an::ErrorCode::AllocationFailed)), an::GmicAdapterReason::None);
    EXPECT_EQ(an::gmicAdapterReason(an::Status::failure(an::ErrorCode::InvalidArgument, "no context here")),
              an::GmicAdapterReason::None);
}

TEST(GmicAdapterCapabilitiesTest, AReasonThisBuildCannotNameIsReportedAsNone)
{
    // A failure built by a newer adapter against an older consumer. The number is carried faithfully
    // and cannot be branched on, so it is reported as no reason rather than as a wrong one.
    const an::Status failure = an::Status::failure(
        an::ErrorCode::ExternalFailure,
        an::StatusDetail{.context = {an::StatusContextField::ofCount(an::ContextKey::AdapterReason, 4000u)}});

    EXPECT_EQ(an::gmicAdapterReason(failure), an::GmicAdapterReason::None);
}

TEST(GmicAdapterCapabilitiesTest, AReasonFieldOfTheWrongKindIsNotRead)
{
    // The value travels as a count. A text field under the same key is not a reason, and reading one
    // as though it were would be exactly the message-parsing this design refuses.
    const an::Status failure = an::Status::failure(
        an::ErrorCode::ExternalFailure,
        an::StatusDetail{.context = {an::StatusContextField::ofText(an::ContextKey::AdapterReason, "CommandFailed")}});

    EXPECT_EQ(an::gmicAdapterReason(failure), an::GmicAdapterReason::None);
}

TEST(GmicAdapterCapabilitiesTest, TheTwoAdaptersDoNotShareAReasonVocabulary)
{
    // Both adapters carry their reason under one engine key, `ContextKey::AdapterReason`, because the
    // engine's vocabulary is closed and an adapter's is its own. A number from one adapter read
    // through the other's accessor is therefore a different reason, not a wrong one - which is why a
    // caller branches on the reason of the adapter it called and never on a number by itself.
    const auto number = static_cast<std::uint64_t>(an::GmicAdapterReason::CommandFailed);
    const an::Status failure = an::Status::failure(
        an::ErrorCode::ExternalFailure,
        an::StatusDetail{.domain = an::ErrorDomain::Interop,
                         .context = {an::StatusContextField::ofText(an::ContextKey::Library, "gmic"),
                                     an::StatusContextField::ofCount(an::ContextKey::AdapterReason, number)}});

    ASSERT_EQ(an::gmicAdapterReason(failure), an::GmicAdapterReason::CommandFailed);
    const an::StatusDetail* detail = failure.detail();
    ASSERT_NE(detail, nullptr);
    bool sawLibrary = false;
    for (const an::StatusContextField& field : detail->context)
    {
        if (field.key == an::ContextKey::Library)
        {
            sawLibrary = true;
            EXPECT_EQ(field.text, "gmic");
        }
    }
    EXPECT_TRUE(sawLibrary) << "a reason is only readable beside the library that produced it";
}
