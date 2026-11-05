// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// An engine failure as the integration workflow reports it: the stage's code refined by the engine's typed answer,
/// read through the whole cause chain, the stage's own words and file, and the engine's words beneath them.

#include "processes/ImageIntegration/model/IntegrationEngineError.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecFormat.h>

#include <gtest/gtest.h>

namespace
{

using ImageIntegration::engineFailureText;
using ImageIntegration::integrationErrorCodeFor;
using ImageIntegration::integrationErrorFromEngine;

[[nodiscard]] an::Status codec(const an::ErrorCode code, const an::CodecFailure reason, const an::Status cause = {})
{
    return an::codecFailure(code, reason, an::CodecFormat::Fits, "probe", "light_001.fit", cause);
}

TEST(IntegrationEngineErrorTest, ASuccessIsNoError)
{
    EXPECT_EQ(integrationErrorCodeFor(an::Status{}, IntegrationErrorCode::StackingFailed), IntegrationErrorCode::None);
    EXPECT_TRUE(engineFailureText(an::Status{}).isEmpty());
    const IntegrationError error =
        integrationErrorFromEngine(an::Status{}, IntegrationErrorCode::StackingFailed, QStringLiteral("Stacking"));
    EXPECT_TRUE(error.isSuccess());
}

TEST(IntegrationEngineErrorTest, AStopOrADeadlineIsTheRunsNotTheStages)
{
    const an::Status stopped = an::Status::failure(an::ErrorCode::Cancelled, "stopped");
    EXPECT_EQ(integrationErrorCodeFor(stopped, IntegrationErrorCode::StackingFailed), IntegrationErrorCode::Cancelled);
    EXPECT_TRUE(integrationErrorFromEngine(stopped, IntegrationErrorCode::StackingFailed, QStringLiteral("Stacking"))
                    .isCancellation());

    const an::Status late = an::Status::failure(an::ErrorCode::DeadlineExceeded, "too late");
    EXPECT_EQ(integrationErrorCodeFor(late, IntegrationErrorCode::RegistrationFailed), IntegrationErrorCode::Timeout);
}

TEST(IntegrationEngineErrorTest, ARefusedAllocationIsExhaustedMemory)
{
    EXPECT_EQ(integrationErrorCodeFor(an::Status::failure(an::ErrorCode::AllocationFailed, "no memory"),
                                      IntegrationErrorCode::CalibrationFailed),
              IntegrationErrorCode::MemoryExhausted);
    EXPECT_EQ(integrationErrorCodeFor(an::Status::failure(an::ErrorCode::MemoryBudgetExceeded, "over budget"),
                                      IntegrationErrorCode::CalibrationFailed),
              IntegrationErrorCode::MemoryExhausted);
}

TEST(IntegrationEngineErrorTest, ACodecsReasonSaysWhatIsWrongWithTheFile)
{
    EXPECT_EQ(integrationErrorCodeFor(codec(an::ErrorCode::Unsupported, an::CodecFailure::Unavailable),
                                      IntegrationErrorCode::StackingFailed),
              IntegrationErrorCode::InvalidFileFormat);
    EXPECT_EQ(integrationErrorCodeFor(codec(an::ErrorCode::InvalidArgument, an::CodecFailure::UnknownFormat),
                                      IntegrationErrorCode::StackingFailed),
              IntegrationErrorCode::InvalidFileFormat);
    EXPECT_EQ(integrationErrorCodeFor(codec(an::ErrorCode::InvalidData, an::CodecFailure::PayloadUnreadable),
                                      IntegrationErrorCode::StackingFailed),
              IntegrationErrorCode::FileNotReadable);
    EXPECT_EQ(integrationErrorCodeFor(codec(an::ErrorCode::ExternalFailure, an::CodecFailure::PublishFailure),
                                      IntegrationErrorCode::StackingFailed),
              IntegrationErrorCode::FileNotWritable);

    // A file that cannot be read is skipped; one the stage cannot write stops the run.
    EXPECT_TRUE(isRecoverableError(
        integrationErrorCodeFor(codec(an::ErrorCode::InvalidData, an::CodecFailure::MetadataUnreadable),
                                IntegrationErrorCode::StackingFailed)));
    EXPECT_FALSE(isRecoverableError(integrationErrorCodeFor(
        codec(an::ErrorCode::ExternalFailure, an::CodecFailure::ShortWrite), IntegrationErrorCode::StackingFailed)));
}

TEST(IntegrationEngineErrorTest, TheCauseChainIsReadBelowTheLayerThatNoticed)
{
    const an::Status stopped = an::Status::failure(an::ErrorCode::Cancelled, "stopped by the user");
    const an::Status callback = codec(an::ErrorCode::ExternalFailure, an::CodecFailure::CallbackFailure, stopped);
    EXPECT_EQ(integrationErrorCodeFor(callback, IntegrationErrorCode::CalibrationFailed),
              IntegrationErrorCode::Cancelled);

    const an::Status unreadable = codec(an::ErrorCode::InvalidData, an::CodecFailure::PayloadUnreadable);
    const an::Status wrapped = codec(an::ErrorCode::ExternalFailure, an::CodecFailure::CallbackFailure, unreadable);
    EXPECT_EQ(integrationErrorCodeFor(wrapped, IntegrationErrorCode::CalibrationFailed),
              IntegrationErrorCode::FileNotReadable);
}

TEST(IntegrationEngineErrorTest, AFailureTheEngineDoesNotRefineIsTheStages)
{
    const an::Status invalid = an::Status::failure(an::ErrorCode::InvalidData, "the frames disagree");
    EXPECT_EQ(integrationErrorCodeFor(invalid, IntegrationErrorCode::StackingFailed),
              IntegrationErrorCode::StackingFailed);
    EXPECT_EQ(integrationErrorCodeFor(an::Status::failure(an::ErrorCode::Internal, "defect"),
                                      IntegrationErrorCode::DrizzleFailed),
              IntegrationErrorCode::DrizzleFailed);
    // A codec reason that says nothing about the file leaves the stage's code too.
    EXPECT_EQ(integrationErrorCodeFor(codec(an::ErrorCode::InvalidData, an::CodecFailure::DescriptorMismatch),
                                      IntegrationErrorCode::CalibrationFailed),
              IntegrationErrorCode::CalibrationFailed);
}

TEST(IntegrationEngineErrorTest, TheStagesWordsComeFirstAndTheEnginesAreTheDetails)
{
    const an::Status stopped = an::Status::failure(an::ErrorCode::Cancelled, "stopped by the user");
    const an::Status failure = an::Status::failure(an::ErrorCode::ExternalFailure, "the decoder failed");
    const an::Status chained = codec(an::ErrorCode::ExternalFailure, an::CodecFailure::CallbackFailure,
                                     an::Status::failure(an::ErrorCode::InvalidData, "row 12 is short"));

    const IntegrationError error =
        integrationErrorFromEngine(failure, IntegrationErrorCode::CalibrationFailed,
                                   QStringLiteral("Calibrating the light frame failed"), QStringLiteral("l1.fit"));
    EXPECT_EQ(error.code(), IntegrationErrorCode::CalibrationFailed);
    EXPECT_EQ(error.message(), QStringLiteral("Calibrating the light frame failed"));
    EXPECT_EQ(error.filePath(), QStringLiteral("l1.fit"));
    EXPECT_EQ(error.details(), QStringLiteral("the decoder failed"));

    const QString text = engineFailureText(chained);
    EXPECT_TRUE(text.endsWith(QStringLiteral(": row 12 is short"))) << text.toStdString();
    EXPECT_GT(text.size(), QStringLiteral(": row 12 is short").size());
    EXPECT_EQ(engineFailureText(stopped), QStringLiteral("stopped by the user"));
}

} // namespace
