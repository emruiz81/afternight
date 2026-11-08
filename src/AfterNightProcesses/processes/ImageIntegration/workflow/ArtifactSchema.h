// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What identifies a workflow artifact and the documents beside it: the tracker's schema, the per-family semantic
/// identity every derived family's fingerprint carries, and the schemas and bounds of the sidecars the planner reads.
///
/// A fingerprint has to change when the numbers an artifact holds would change, and only then. The engine that computes
/// them is one such fact, so every family carries the generation of the engine its numbers come from and a revision of
/// its own meaning: a family whose stage changes what it computes moves its revision, and nothing else does. Neither is
/// the engine's version, so a repair that changes no number invalidates nothing. An artifact fingerprinted with another
/// engine generation, or with neither, matches no fingerprint taken now, so its derived family is computed again once -
/// whatever its stamps and records state, they are for an engine that is not the one running.

#include <QByteArray>
#include <QFile>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

namespace ImageIntegration
{
namespace schema
{
/// The generation of the engine every workflow artifact's numbers now come from. Not a version: it moves only when
/// the engine computing the workflow's artifacts is replaced.
inline constexpr char kWorkflowEngineGeneration[] = "afternight-engine-next";

/// The families of artifact the workflow derives, each fingerprinted with its own semantic revision.
enum class ArtifactFamily
{
    /// A bias, dark or flat master.
    CalibrationMaster,
    /// A calibrated light, on disk or held for the registration stage.
    CalibratedLight,
    /// The stars detected in a light.
    StarDetections,
    /// The transforms and the output plane a registration solved.
    Registration,
    /// A light corrected for chromatic aberration.
    ChromaticCorrection,
    /// A light warped onto its registration's output plane.
    RegisteredLight,
    /// The global normalization coefficients of a group.
    NormalizationCoefficients,
    /// The local normalization grids of a group.
    LocalNormalizationGrid,
    /// The compact drizzle map descriptors of a group.
    DrizzleDescriptor,
    /// A dense drizzle map's payload.
    DrizzleDensePayload,
    /// A stacked light master.
    Stack,
    /// A rejection, weight or quality map beside a stack.
    StackAuxiliaryMap,
    /// A comet or stars-plus-comet composite.
    CometComposite
};

/// A family's stable name, as its fingerprints and records spell it.
///
/// @param family the family.
/// @return a static string; "unknown" for a value outside the enumeration.
[[nodiscard]] constexpr const char* artifactFamilyName(const ArtifactFamily family) noexcept
{
    switch (family)
    {
    case ArtifactFamily::CalibrationMaster:
        return "calibration-master";
    case ArtifactFamily::CalibratedLight:
        return "calibrated-light";
    case ArtifactFamily::StarDetections:
        return "star-detections";
    case ArtifactFamily::Registration:
        return "registration";
    case ArtifactFamily::ChromaticCorrection:
        return "chromatic-correction";
    case ArtifactFamily::RegisteredLight:
        return "registered-light";
    case ArtifactFamily::NormalizationCoefficients:
        return "normalization-coefficients";
    case ArtifactFamily::LocalNormalizationGrid:
        return "local-normalization-grid";
    case ArtifactFamily::DrizzleDescriptor:
        return "drizzle-descriptor";
    case ArtifactFamily::DrizzleDensePayload:
        return "drizzle-dense-payload";
    case ArtifactFamily::Stack:
        return "stack";
    case ArtifactFamily::StackAuxiliaryMap:
        return "stack-auxiliary-map";
    case ArtifactFamily::CometComposite:
        return "comet-composite";
    }
    return "unknown";
}

/// A family's semantic revision under `kWorkflowEngineGeneration`. A stage that changes what one of its families
/// computes moves that family's revision, which invalidates that family and whatever depends on it, and nothing else.
///
/// The registration's is two: a light its stars match too little of the reference is registered through a light
/// already registered near it, which one leaves unregistered or registers from its solution, the standalone
/// registration matches a frame's stars where its own plate distortion corrects them to, and a light that shares a
/// small part of its stars with the reference is registered by the pairs it shares, which one refused below half of
/// its candidate pairs. The chromatic correction's is two: a channel is matched to the green one by the same rule. The
/// calibration masters' is two: a master dark is stacked from its frames as they are, holding their bias in CCD mode as
/// in standard mode, and a flat is calibrated by its dark alone when one matches, which one stacked a CCD-mode dark
/// without its bias and calibrated a standard-mode flat by the bias and the dark together, taking the bias out twice.
/// The stack's is three: a master states in `STACKCNT` how many frames it combines, a group weighted by the number of
/// images weighs each light by the count its header states, an exposure merge weighs no frame, reads a bucket of one
/// light as a stack of it would and blends only the samples each bucket holds, a per-pixel weighting runs at the
/// nearest values the engine takes, and a filter-array drizzle reads its lights under the pattern its settings state.
/// Two applied a local normalization document over the light it names as the stack's reference, or with its matches
/// restated against the stack's reference when it names none the stack reads, and a drizzled group's coefficients at
/// any scale, where one refused all three and the stack measured its own normalization instead.
///
/// @param family the family.
/// @return the revision, counting from one; zero for a value outside the enumeration.
[[nodiscard]] constexpr int artifactFamilySemanticRevision(const ArtifactFamily family) noexcept
{
    switch (family)
    {
    case ArtifactFamily::Stack:
        return 3;
    case ArtifactFamily::Registration:
    case ArtifactFamily::ChromaticCorrection:
    case ArtifactFamily::CalibrationMaster:
        return 2;
    case ArtifactFamily::CalibratedLight:
    case ArtifactFamily::StarDetections:
    case ArtifactFamily::RegisteredLight:
    case ArtifactFamily::NormalizationCoefficients:
    case ArtifactFamily::LocalNormalizationGrid:
    case ArtifactFamily::DrizzleDescriptor:
    case ArtifactFamily::DrizzleDensePayload:
    case ArtifactFamily::StackAuxiliaryMap:
    case ArtifactFamily::CometComposite:
        return 1;
    }
    return 0;
}

/// A family's semantic identity as a fingerprint's input: the engine generation, the family and its revision.
///
/// @param family the family.
/// @return the object a fingerprint inserts under "artifactFamily".
[[nodiscard]] inline QJsonObject artifactFamilyIdentity(const ArtifactFamily family)
{
    QJsonObject identity;
    identity.insert(QStringLiteral("engineGeneration"), QString::fromLatin1(kWorkflowEngineGeneration));
    identity.insert(QStringLiteral("family"), QString::fromLatin1(artifactFamilyName(family)));
    identity.insert(QStringLiteral("revision"), artifactFamilySemanticRevision(family));
    return identity;
}

/// Inserts a family's semantic identity into a fingerprint's input, under "artifactFamily".
///
/// @param object the fingerprint's input.
/// @param family the family the fingerprinted artifact belongs to.
inline void insertArtifactFamilyIdentity(QJsonObject& object, const ArtifactFamily family)
{
    object.insert(QStringLiteral("artifactFamily"), artifactFamilyIdentity(family));
}

/// Whether a sidecar or record states the semantic identity a family has now.
///
/// @param object the sidecar or record, which states it under "artifactFamily".
/// @param family the family it should belong to.
/// @return true only for this engine generation, this family and its current revision.
[[nodiscard]] inline bool statesCurrentArtifactFamily(const QJsonObject& object, const ArtifactFamily family)
{
    return object.value(QStringLiteral("artifactFamily")).toObject() == artifactFamilyIdentity(family);
}

/// The format the artifact records' store states in `PRAGMA user_version` and is written in.
inline constexpr int kArtifactTrackerSchemaVersion = 2;
inline constexpr char kArtifactDigestProvenanceCurrentRunCommitted[] = "current_run_committed";
inline constexpr char kArtifactDigestProvenanceReadbackVerified[] = "readback_verified";
inline constexpr char kArtifactDigestProvenanceUnknown[] = "unknown";
/// The normalization sidecar's schema: six, which states the coefficients' family identity. A sidecar of another schema
/// is never reused.
inline constexpr int kNormalizationArtifactSchemaVersion = 6;
inline constexpr qint64 kMetadataSidecarMaxBytes = 8LL * 1024LL * 1024LL;
inline constexpr qsizetype kWorkflowLogMaxBytes = 8LL * 1024LL * 1024LL;
/// The drizzle stage's group sidecar schema: four, which lists one descriptor document per light and states the
/// descriptors' family identity. A sidecar of another schema is never reused.
inline constexpr int kDrizzleGroupSidecarSchemaVersion = 4;

/// What a drizzled master's samples are, as its fingerprint states it: two since every drizzled frame is restated at
/// its lights' level before it is stacked; one stacked them at that level divided by the scale squared. Only a
/// drizzled group's fingerprint carries it, so moving it stacks the drizzled masters again and nothing else.
inline constexpr int kDrizzledMasterLevelRevision = 2;

/// Which world coordinates a drizzled master carries, as its fingerprint states it: one since it carries the
/// registration's plane solution moved onto its drizzled canvas, which no drizzled master carried before. Only a
/// drizzled group's fingerprint carries it, so moving it stacks the drizzled masters again and nothing else.
inline constexpr int kDrizzledMasterSolutionRevision = 1;

/// How a drizzled Local group's master is normalized, as its fingerprint states it: one since its stack applies the
/// normalization stage's document where each drizzled light lies on the plane the document was measured on; before,
/// the drizzled stack fitted the surfaces on the drizzled lights itself, with none of the stage's reference strategy,
/// evaluation, PSF flux scale or analysis filters. Only a drizzled Local group's fingerprint carries it, so it stacks
/// the drizzled Local masters again and nothing else.
inline constexpr int kDrizzledLocalNormalizationRevision = 1;

/// The dense drizzle map file (`ANPIXMAP` version 3), which the planner never reuses: its maps are regenerated from the
/// sources and settings and written as the workflow's own drizzle documents.
inline constexpr quint32 kPixmapSchemaVersion = 3;
inline constexpr char kPixmapMagic[8] = {'A', 'N', 'P', 'I', 'X', 'M', 'A', 'P'};

[[nodiscard]] inline bool readMetadataSidecarBounded(QFile& file, QByteArray& data, QString* error = nullptr)
{
    data.clear();
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QStringLiteral("Failed to open metadata sidecar.");
        return false;
    }

    data = file.read(kMetadataSidecarMaxBytes + 1);
    if (file.error() != QFileDevice::NoError)
    {
        data.clear();
        if (error)
            *error = QStringLiteral("Failed to read metadata sidecar.");
        return false;
    }
    if (data.size() > kMetadataSidecarMaxBytes)
    {
        data.clear();
        if (error)
            *error = QStringLiteral("Metadata sidecar exceeds the %1-byte limit.").arg(kMetadataSidecarMaxBytes);
        return false;
    }
    return true;
}
} // namespace schema
} // namespace ImageIntegration
