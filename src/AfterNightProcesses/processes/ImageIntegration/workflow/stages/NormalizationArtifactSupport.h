// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// A group's normalization coefficients as the workflow keeps them: one document holding what the normalization stage
/// measured - every frame's global match, the engine's local-normalization document, or both - and the sidecar beside
/// it that the planner and the stages decide reuse by.
///
/// A coefficient file whose sidecar is of another schema is never read: a group whose sidecar states another schema,
/// another family identity or other fingerprints is measured again, and its old pair stays where it is until the new
/// one replaces it. A sidecar a newer version wrote is neither read nor replaced unless the person asks for the group
/// to be reprocessed.
///
/// The document is the workflow's enveloped document: its payload is JSON naming the mode, the plane the frames were
/// measured on, every frame's logical and source identity in coefficient order, each global match by the engine's
/// method and estimator names, and the engine's own local-normalization document as its text form, which is lossless.
/// It is published before the sidecar naming its checksum, so an interruption can leave an orphaned document but never
/// a sidecar advertising a document that is missing or different.

#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/workflow/DocumentPublication.h"

#include <AfterNightEngine/Algorithms/Kernel/SamplingKernel.h>
#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <optional>
#include <stop_token>
#include <vector>

namespace ImageIntegration
{

/// The envelope type of a group's coefficient document.
inline constexpr char kNormalizationCoefficientsDocumentType[] = "normalization-coefficients";

/// The coefficient document's schema this build writes and reads.
inline constexpr int kNormalizationCoefficientsSchemaVersion = 1;

/// Which coefficients a document holds.
enum class NormalizationCoefficientMode
{
    /// One global match per frame.
    Global,
    /// One local surface per frame.
    Local,
    /// One local surface per frame and the global match measured over the locally normalized frame.
    LocalThenGlobal
};

/// What the frames were measured on.
enum class NormalizationMeasurementPlane
{
    /// The lights as they were read, every sample meaning something.
    Frames,
    /// The registration's output plane, an exact zero being where a frame does not reach.
    RegisteredCanvas,
    /// A drizzle canvas's analysis plane, an exact zero being where a frame does not reach.
    DrizzleAnalysisPlane
};

/// A mode's stored key: "global", "local" or "local-then-global".
[[nodiscard]] const char* normalizationCoefficientModeKey(NormalizationCoefficientMode mode) noexcept;

/// A plane's stored key: "frames", "registered-canvas" or "drizzle-analysis-plane".
[[nodiscard]] const char* normalizationMeasurementPlaneKey(NormalizationMeasurementPlane plane) noexcept;

/// Where on the reference plane a drizzle canvas's analysis plane lies and how it is enlarged.
///
/// The analysis plane is the canvas's rectangle before the scale, so its origin and extent are what a stack must share
/// with coefficients measured on it. The scale is recorded, not compared: the frames are measured on the plane at their
/// lights' level, and the stack restates every drizzled frame at that level, so the coefficients apply at any scale.
struct DrizzleAnalysisPlaneRecord
{
    /// The canvas's origin, the offset every frame's mapping adds before the scale.
    double originX = 0.0;
    double originY = 0.0;

    /// The analysis plane's extent.
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;

    /// The drizzle's enlargement when the frames were measured.
    double scale = 1.0;

    friend bool operator==(const DrizzleAnalysisPlaneRecord& left, const DrizzleAnalysisPlaneRecord& right) = default;
};

/// A group's coefficients.
struct NormalizationCoefficientSet
{
    NormalizationCoefficientMode mode = NormalizationCoefficientMode::Global;
    NormalizationMeasurementPlane plane = NormalizationMeasurementPlane::Frames;

    /// The channels every frame was measured in.
    std::uint32_t channelCount = 0u;

    /// The extent every frame was measured at.
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;

    /// The frame the others were brought to, an index into the frames below. When a local document's reference is
    /// several frames integrated, the document names no reference frame and this is the frame its scales were measured
    /// against.
    std::uint32_t referenceIndex = 0u;

    /// Each frame's logical identity - the light the group lists - in coefficient order.
    std::vector<QString> logicalFrames;

    /// The file each frame was measured from, in the same order.
    std::vector<QString> sourceFrames;

    /// Each frame's global match, in the same order; empty unless the mode is `Global`.
    std::vector<an::FrameNormalization> global;

    /// The engine's local-normalization document, whose frame indices are the order above; present exactly when the
    /// mode is local. Its reference frame, when it names one, is `referenceIndex`.
    std::optional<an::LocalNormalizationDocument> local;

    /// The drizzle canvas the frames were measured on; present exactly when the plane is the drizzle analysis plane.
    std::optional<DrizzleAnalysisPlaneRecord> drizzlePlane;
};

/// Whether a set is one this build writes: every count, index and coefficient consistent with its mode and plane.
///
/// @param set the set.
/// @return empty when it is; otherwise the reason, in words.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString checkNormalizationCoefficientSet(const NormalizationCoefficientSet& set);

/// The document's payload: a compact JSON object.
///
/// @param set a set `checkNormalizationCoefficientSet` accepts.
/// @return the JSON; empty when the engine's local document cannot be written.
[[nodiscard]] AFTERNIGHTPROCESSES_API QByteArray
formatNormalizationCoefficients(const NormalizationCoefficientSet& set);

/// A document's payload read: every field present and consistent, the local document read by the engine.
///
/// @param payload the JSON.
/// @param error the reason, when it is refused.
/// @return the set, or nothing.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<NormalizationCoefficientSet>
parseNormalizationCoefficients(const QByteArray& payload, QString& error);

/// The identity a group's coefficient document satisfies: the SHA-256, in hex, of its inputs' and settings'
/// fingerprints.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString normalizationArtifactIdentity(const QString& inputsFingerprint,
                                                                            const QString& settingsFingerprint);

/// The fingerprint of a plane the stage warps a group's lights onto as it measures them: each light's transform as
/// its record states it, keyed by the light's canonical path in `transforms`; the plane's origin and extent; and the
/// interpolation. The registration's plane and a drizzle canvas's analysis plane name their extent differently, so
/// two planes of one size are never one. The planner states it from the run's records to tell whether coefficients
/// on disk are the ones the stage would measure.
///
/// @param plane `RegisteredCanvas` or `DrizzleAnalysisPlane`.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString warpedPlaneGeometryFingerprint(NormalizationMeasurementPlane plane,
                                                                             const QJsonObject& transforms,
                                                                             double originX, double originY,
                                                                             std::uint32_t width, std::uint32_t height,
                                                                             an::KernelInterpolation interpolation);

/// The inputs a measurement on a warped plane states: the fingerprint of the files read, then the plane's.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString warpedMeasurementInputsFingerprint(const QString& filesFingerprint,
                                                                                 const QString& geometryFingerprint);

/// The coefficient document's envelope.
///
/// @param set the set.
/// @param identity the identity it satisfies.
/// @return the envelope; nothing when the set is refused or cannot be written.
[[nodiscard]] std::optional<DocumentEnvelope> normalizationCoefficientsEnvelope(const NormalizationCoefficientSet& set,
                                                                                const QString& identity);

/// Why a coefficient document was not read.
enum class NormalizationDocumentFailure
{
    None,
    /// The document is missing, unreadable, not an envelope, or not the kind or identity asked for.
    Unreadable,
    /// The document's schema, or its envelope's, is newer than this build reads.
    NewerSchema,
    /// The document read but holds a set this build refuses.
    Invalid,
    /// The document was measured under settings other than the ones its reader runs.
    OtherSettings
};

/// The outcome of reading a coefficient document.
struct NormalizationDocumentRead
{
    NormalizationDocumentFailure failure = NormalizationDocumentFailure::None;
    QString message;
    std::optional<NormalizationCoefficientSet> coefficients;
};

/// Reads a group's coefficient document.
///
/// @param path the document.
/// @param expectedIdentity the identity it must satisfy; nothing accepts any.
/// @return the set or the reason.
[[nodiscard]] AFTERNIGHTPROCESSES_API NormalizationDocumentRead
readNormalizationCoefficients(const QString& path, const std::optional<QString>& expectedIdentity);

/// The sidecar beside a coefficient document.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString normalizationSidecarPath(const QString& artifactPath);

/// The sidecar's fields every coefficient document carries: its schema and family, the stage and group, the
/// fingerprints, the document's checksum, the mode, the plane and the counts. A stage adds what it records beside them.
///
/// @param groupKey the group.
/// @param inputsFingerprint the fingerprint of the files the frames were measured from.
/// @param settingsFingerprint the fingerprint of the settings the coefficients were measured under.
/// @param payloadSha256 the SHA-256, in hex, of the document's file.
/// @param set the set.
/// @return the sidecar.
[[nodiscard]] AFTERNIGHTPROCESSES_API QJsonObject normalizationSidecar(const IntegrationGroupKey& groupKey,
                                                                       const QString& inputsFingerprint,
                                                                       const QString& settingsFingerprint,
                                                                       const QString& payloadSha256,
                                                                       const NormalizationCoefficientSet& set);

/// Whether a group's coefficients on disk may be reused, and why not.
struct NormalizationArtifactReuse
{
    bool reuse = false;
    QString reason;

    /// Whether the sidecar or the document was written by a newer version, which is neither read nor replaced unless
    /// the group is reprocessed.
    bool newerSchema = false;

    /// The sidecar's fields, when it read.
    QJsonObject sidecar;

    /// The coefficients, when they are reused.
    std::optional<NormalizationCoefficientSet> coefficients;
};

/// Decides whether a group's coefficients on disk satisfy the fingerprints asked for: the sidecar of schema six stating
/// the coefficients' current family, the stage and group, both fingerprints and the document's checksum, and the
/// document reading whole for that identity.
///
/// @param artifactPath the document.
/// @param groupKey the group.
/// @param inputsFingerprint the inputs' fingerprint asked for.
/// @param settingsFingerprint the settings' fingerprint asked for.
/// @return the decision.
[[nodiscard]] AFTERNIGHTPROCESSES_API NormalizationArtifactReuse
checkNormalizationArtifact(const QString& artifactPath, const IntegrationGroupKey& groupKey,
                           const QString& inputsFingerprint, const QString& settingsFingerprint);

/// Publishes a group's coefficients: the document first, then the sidecar naming its checksum, each whole or not at
/// all.
///
/// @param artifactPath the document.
/// @param set the set.
/// @param identity the identity the document satisfies.
/// @param sidecar the sidecar's fields but the checksum, which is added here.
/// @param stop the stop both publications observe.
/// @param operations the file operations.
/// @return the outcome.
[[nodiscard]] AFTERNIGHTPROCESSES_API DocumentPublicationResult publishNormalizationArtifact(
    const QString& artifactPath, const NormalizationCoefficientSet& set, const QString& identity, QJsonObject sidecar,
    std::stop_token stop = {}, const DocumentFileOperations& operations = systemDocumentFileOperations());

/// Removes what an interrupted publication beside a coefficient document left: the workflow's pending siblings and the
/// pending, rollback and lock files named after the document. Nothing else is touched.
///
/// @param artifactPath the document.
/// @return what was removed and what could not be.
AFTERNIGHTPROCESSES_API DocumentRecoveryReport recoverNormalizationArtifact(const QString& artifactPath);

} // namespace ImageIntegration
