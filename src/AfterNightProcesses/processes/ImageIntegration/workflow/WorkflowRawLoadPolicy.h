// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The camera RAW treatment every workflow run reads its frames under, held by the run rather than by the process.
///
/// Calibration subtracts and divides camera codes, so a run reads a RAW frame with no white balance at all: a mosaic as
/// the black-subtracted codes the codec returns, a full-colour frame through a camera RAW adapter built with no
/// balance. Rewriting the engine's process-wide RAW options for the length of a run would balance a picture opened in a
/// viewer meanwhile the workflow's way. Instead the run takes a snapshot of this policy when it starts, builds its own
/// codec registry from it, and hands both to its stages; the application's settings and every other reader's registry
/// are untouched, and a change of settings during a run reaches the next run.

#include <AfterNightEngine/Algorithms/Demosaic/CfaWhiteBalance.h>
#include <AfterNightEngine/IO/RawCodec.h>

#include <QJsonObject>
#include <QString>

namespace ImageIntegration
{

/// The version of the policy's fingerprint input; two since the policy states both sensor classes.
inline constexpr int kWorkflowRawLoadPolicyHashVersion = 2;

/// The RAW treatment a run reads its frames under.
struct WorkflowRawPolicy
{
    /// The balance applied to a mosaic after it is decoded, before calibration.
    an::CfaWhiteBalanceMode cfaWhiteBalance = an::CfaWhiteBalanceMode::None;

    /// The balance a full-colour frame's adapter decodes with.
    an::RawFullColorWhiteBalance fullColorWhiteBalance = an::RawFullColorWhiteBalance::None;

    friend bool operator==(const WorkflowRawPolicy& left, const WorkflowRawPolicy& right) = default;
};

/// The policy every run takes: no balance on either sensor class, since calibration works in camera codes.
[[nodiscard]] inline WorkflowRawPolicy workflowRawPolicy()
{
    return {};
}

/// The camera RAW adapter's options a run's registry is built with.
///
/// @param policy the run's snapshot.
/// @return the options: the full-colour balance the policy states, every other option the adapter's default.
[[nodiscard]] inline an::RawCodecOptions workflowRawCodecOptions(const WorkflowRawPolicy& policy)
{
    an::RawCodecOptions options;
    options.fullColorWhiteBalance = policy.fullColorWhiteBalance;
    return options;
}

/// The policy's balance name as a log line spells it.
[[nodiscard]] inline QString workflowRawWhiteBalanceModeName()
{
    return QStringLiteral("none");
}

/// The policy in words for the run's log.
[[nodiscard]] inline QString workflowRawLoadPolicyDescription()
{
    return QStringLiteral("None (forced for workflow calibration)");
}

/// The policy as a fingerprint's input, so an artifact read under another treatment is never reused.
///
/// @param policy the run's snapshot.
/// @return the object.
[[nodiscard]] inline QJsonObject workflowRawLoadPolicyHashObject(const WorkflowRawPolicy& policy = workflowRawPolicy())
{
    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), kWorkflowRawLoadPolicyHashVersion);
    object.insert(QStringLiteral("cfaWhiteBalance"), static_cast<int>(policy.cfaWhiteBalance));
    object.insert(QStringLiteral("fullColorWhiteBalance"), static_cast<int>(policy.fullColorWhiteBalance));
    object.insert(QStringLiteral("calibrationDomain"), QStringLiteral("raw-before-demosaic"));
    return object;
}

} // namespace ImageIntegration
