// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEnginePythonAdapter/PythonAdapterCapabilities.h.
//
// Every case here runs in both configurations of the adapter, which is the point of the header: the
// discovery report is the one part of an optional adapter that has to answer before anybody knows
// whether the optional part is there. So the cases are written against the relationship between the
// fields rather than against one build's values - `runtimeCompiledIn` decides which states are
// reachable, and a state that disagrees with it is the failure this file exists to catch.
//
// The reason vocabulary is tested the way the engine's error codes are: a name per value, distinct
// names, and a named answer for a value from outside the enumeration. A reason is read off a failure
// through a typed field, and the case that pins that is here rather than beside the adapter, because
// what it proves is that no caller has to parse a message to find out which rule refused.

#include <AfterNightEnginePythonAdapter/PythonAdapterCapabilities.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace
{

constexpr an::PythonAdapterState kAllStates[] = {an::PythonAdapterState::Absent, an::PythonAdapterState::Available,
                                                 an::PythonAdapterState::Loaded, an::PythonAdapterState::FailedToLoad};

constexpr an::PythonAdapterReason kAllReasons[] = {an::PythonAdapterReason::None,
                                                   an::PythonAdapterReason::RuntimeNotCompiledIn,
                                                   an::PythonAdapterReason::ForeignInterpreterActive,
                                                   an::PythonAdapterReason::AdapterAlreadyActive,
                                                   an::PythonAdapterReason::InterpreterStartupFailed,
                                                   an::PythonAdapterReason::InterpreterConfigurationRejected,
                                                   an::PythonAdapterReason::AdapterShutDown,
                                                   an::PythonAdapterReason::CallInFlight,
                                                   an::PythonAdapterReason::PythonExceptionRaised,
                                                   an::PythonAdapterReason::PythonInterrupted,
                                                   an::PythonAdapterReason::ModuleNotFound,
                                                   an::PythonAdapterReason::AttributeNotFound,
                                                   an::PythonAdapterReason::NotCallable,
                                                   an::PythonAdapterReason::BufferBridgeRefused,
                                                   an::PythonAdapterReason::BufferPermissionMismatch,
                                                   an::PythonAdapterReason::UnexpectedResult};

} // namespace

TEST(PythonAdapterCapabilitiesTest, TheStateAgreesWithWhetherARuntimeIsCompiledIn)
{
    const an::PythonAdapterCapabilities capabilities = an::pythonAdapterCapabilities();

    EXPECT_TRUE(an::isPythonAdapterState(capabilities.state));
    if (capabilities.runtimeCompiledIn)
    {
        // Nothing in this suite has started an interpreter yet at the point the discovery report is
        // asked, so an enabled build is either startable or already running one.
        EXPECT_NE(capabilities.state, an::PythonAdapterState::Absent);
        EXPECT_EQ(capabilities.buildVersion.major, 3);
        EXPECT_GT(capabilities.buildVersion.minor, 0);
    }
    else
    {
        EXPECT_EQ(capabilities.state, an::PythonAdapterState::Absent);
        EXPECT_EQ(capabilities.buildVersion, an::PythonRuntimeVersion{});
        EXPECT_EQ(capabilities.runtimeVersion, an::PythonRuntimeVersion{});
        EXPECT_FALSE(capabilities.foreignInterpreter);
    }
}

TEST(PythonAdapterCapabilitiesTest, TheBuildFactsAreTheSameEveryTimeTheyAreAsked)
{
    const an::PythonAdapterCapabilities first = an::pythonAdapterCapabilities();
    const an::PythonAdapterCapabilities second = an::pythonAdapterCapabilities();

    EXPECT_EQ(first.runtimeCompiledIn, second.runtimeCompiledIn);
    EXPECT_EQ(first.buildVersion, second.buildVersion);
    EXPECT_EQ(first.state, second.state);
}

TEST(PythonAdapterCapabilitiesTest, AskingTheLibraryStartsNothing)
{
    // The report exists so a caller can decide whether to ask for an adapter. A report that started
    // an interpreter to answer would have made the question expensive and the answer a side effect.
    const an::PythonAdapterCapabilities before = an::pythonAdapterCapabilities();
    for (int index = 0; index < 4; ++index)
    {
        EXPECT_NE(an::pythonAdapterCapabilities().state, an::PythonAdapterState::Loaded);
    }
    EXPECT_EQ(an::pythonAdapterCapabilities().state, before.state);
}

TEST(PythonAdapterCapabilitiesTest, EveryStateAndReasonHasItsOwnName)
{
    std::set<std::string> stateNames;
    for (const an::PythonAdapterState state : kAllStates)
    {
        EXPECT_TRUE(an::isPythonAdapterState(state));
        stateNames.insert(an::pythonAdapterStateName(state));
    }
    EXPECT_EQ(stateNames.size(), std::size(kAllStates));

    std::set<std::string> reasonNames;
    for (const an::PythonAdapterReason reason : kAllReasons)
    {
        EXPECT_TRUE(an::isPythonAdapterReason(reason));
        reasonNames.insert(an::pythonAdapterReasonName(reason));
    }
    EXPECT_EQ(reasonNames.size(), std::size(kAllReasons));
}

TEST(PythonAdapterCapabilitiesTest, AValueFromOutsideTheEnumerationIsNamedAsUnknown)
{
    const auto unknownState = static_cast<an::PythonAdapterState>(200);
    const auto unknownReason = static_cast<an::PythonAdapterReason>(4000);

    EXPECT_FALSE(an::isPythonAdapterState(unknownState));
    EXPECT_FALSE(an::isPythonAdapterReason(unknownReason));
    EXPECT_STREQ(an::pythonAdapterStateName(unknownState), "UnknownPythonAdapterState");
    EXPECT_STREQ(an::pythonAdapterReasonName(unknownReason), "UnknownPythonAdapterReason");
}

TEST(PythonAdapterCapabilitiesTest, AReasonIsReadOffTheFailureRatherThanItsMessage)
{
    const an::Status failure = an::Status::failure(
        an::ErrorCode::ExternalFailure,
        an::StatusDetail{.message = "ValueError: nope",
                         .operation = "PythonRuntimeAdapter::execute",
                         .domain = an::ErrorDomain::Interop,
                         .context = {an::StatusContextField::ofText(an::ContextKey::Library, "python"),
                                     an::StatusContextField::ofCount(
                                         an::ContextKey::AdapterReason,
                                         static_cast<std::uint64_t>(an::PythonAdapterReason::PythonExceptionRaised))}});

    EXPECT_EQ(an::pythonAdapterReason(failure), an::PythonAdapterReason::PythonExceptionRaised);
}

TEST(PythonAdapterCapabilitiesTest, AFailureFromSomewhereElseCarriesNoReason)
{
    EXPECT_EQ(an::pythonAdapterReason(an::Status::success()), an::PythonAdapterReason::None);
    EXPECT_EQ(an::pythonAdapterReason(an::Status::failure(an::ErrorCode::AllocationFailed)),
              an::PythonAdapterReason::None);
    EXPECT_EQ(an::pythonAdapterReason(an::Status::failure(an::ErrorCode::InvalidArgument, "no context here")),
              an::PythonAdapterReason::None);
}

TEST(PythonAdapterCapabilitiesTest, AReasonThisBuildCannotNameIsReportedAsNone)
{
    // A failure built by a newer adapter against an older consumer. The number is carried faithfully
    // and cannot be branched on, so it is reported as no reason rather than as a wrong one.
    const an::Status failure = an::Status::failure(
        an::ErrorCode::ExternalFailure,
        an::StatusDetail{.context = {an::StatusContextField::ofCount(an::ContextKey::AdapterReason, 4000u)}});

    EXPECT_EQ(an::pythonAdapterReason(failure), an::PythonAdapterReason::None);
}

TEST(PythonAdapterCapabilitiesTest, AReasonFieldOfTheWrongKindIsNotRead)
{
    // The value travels as a count. A text field under the same key is not a reason, and reading one
    // as though it were would be exactly the message-parsing this design refuses.
    const an::Status failure = an::Status::failure(
        an::ErrorCode::ExternalFailure, an::StatusDetail{.context = {an::StatusContextField::ofText(
                                                             an::ContextKey::AdapterReason, "PythonExceptionRaised")}});

    EXPECT_EQ(an::pythonAdapterReason(failure), an::PythonAdapterReason::None);
}
