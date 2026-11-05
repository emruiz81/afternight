// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// The workflow's drizzle documents: the descriptor that states how one light's pixels land on a group's drizzled
/// canvas, and the dense map payload a descriptor may name.
///
/// A binary map file per light (`ANPIXMAP` version 3), whose coordinates counted from the first pixel's centre at zero,
/// is never read: a group's maps are regenerated from the sources and settings the project keeps. A descriptor is a
/// schema 1 document inside the workflow's document envelope. It states the light, the source and output extents and
/// the scale, the mapping - a registration transform and source distortion in engine pixel coordinates, read and
/// written as the registration record writes them, and the offset applied after the transform - the convention the
/// engine generates map samples in (integer pixel centres, by its scale formula), and the engine's own identity of the
/// descriptor. A descriptor that names a dense payload states the payload's file, length and checksum, and the
/// payload's envelope carries the descriptor's identity, so a payload is read only beside the descriptor it was written
/// for. The payload is published before its descriptor, which is the document a reader opens first.

#include "processes/ImageIntegration/workflow/DocumentPublication.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleMapIdentity.h>

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <optional>

namespace ImageIntegration
{

/// The envelope type of a drizzle descriptor.
inline constexpr char kDrizzleDescriptorDocumentType[] = "drizzle-descriptor";

/// The envelope type of a dense drizzle map payload.
inline constexpr char kDrizzleDensePayloadDocumentType[] = "drizzle-dense-map";

/// The descriptor's schema this build writes and reads.
inline constexpr int kDrizzleDescriptorSchemaVersion = 1;

/// The dense payload's schema this build writes and reads: two Float32 coordinates per source pixel, row-major, the
/// output position of the pixel's centre in integer pixel centres, a pair of quiet NaNs where it maps nowhere.
inline constexpr int kDrizzleDensePayloadSchemaVersion = 1;

/// The largest source extent, in pixels on a side, a descriptor may state.
inline constexpr std::uint32_t kMaxDrizzleSourceSide = 65536u;

/// What a descriptor states about its dense payload.
struct DrizzleDensePayloadReference
{
    /// The payload's file name, beside the descriptor.
    QString fileName;

    /// The payload's length in bytes, its envelope included.
    std::uint64_t length = 0;

    /// The SHA-256 of the payload's file, in hex.
    QString sha256;

    friend bool operator==(const DrizzleDensePayloadReference& left,
                           const DrizzleDensePayloadReference& right) = default;
};

/// One light's drizzle descriptor.
struct DrizzleDescriptorDocument
{
    /// The light, as its frame identity spells it.
    QString frameId;

    /// The source extent, in pixels.
    std::uint32_t sourceWidth = 0;
    std::uint32_t sourceHeight = 0;

    /// The drizzled output extent, in pixels.
    std::uint32_t outputWidth = 0;
    std::uint32_t outputHeight = 0;

    /// The enlargement from the reference plane to the output.
    double scale = 1.0;

    /// The source-to-reference transform, in engine pixel coordinates.
    an::RegistrationTransform transform;

    /// The source distortion the transform was solved over.
    stage_utils::SourceDistortionProvenance sourceDistortion;

    /// The resolved source distortion the map applies, and its fingerprint as the registration record computes it.
    std::optional<an::SipDistortion> resolvedSourceDistortion;

    /// The reference-plane offset applied after the transform, before the scale.
    an::PixelCoordinate postTranslation{};

    /// Where the normalization the identity was taken for is evaluated.
    an::DrizzleNormalizationSpace normalizationSpace = an::DrizzleNormalizationSpace::SourcePixels;

    /// The engine's identity of the descriptor; empty when none was taken.
    QByteArray engineIdentity;

    /// The dense payload this descriptor names, if any.
    std::optional<DrizzleDensePayloadReference> densePayload;
};

/// The engine's mapping request this document states.
///
/// @param document the descriptor.
/// @return the transform, the offset and the resolved distortion.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::DrizzleMappingRequest
drizzleMappingRequestOf(const DrizzleDescriptorDocument& document);

/// The descriptor's payload: a compact JSON object.
///
/// @param document the descriptor.
/// @return the JSON.
[[nodiscard]] AFTERNIGHTPROCESSES_API QByteArray formatDrizzleDescriptor(const DrizzleDescriptorDocument& document);

/// A descriptor's payload read: every field present, finite and within its bounds, the transform and distortion read
/// as the registration record reads them, the identity exactly as long as the engine's record.
///
/// @param payload the JSON.
/// @param error the reason, when it is refused.
/// @return the descriptor, or nothing.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<DrizzleDescriptorDocument>
parseDrizzleDescriptor(const QByteArray& payload, QString& error);

/// The identity a descriptor's envelope carries and its dense payload's envelope must repeat: the SHA-256, in hex, of
/// the engine's identity of the descriptor and the light.
///
/// @param document the descriptor.
/// @return the identity; empty when the descriptor has no engine identity.
[[nodiscard]] QString drizzleDescriptorIdentity(const DrizzleDescriptorDocument& document);

/// A descriptor in its envelope.
[[nodiscard]] AFTERNIGHTPROCESSES_API DocumentEnvelope
drizzleDescriptorEnvelope(const DrizzleDescriptorDocument& document);

/// A descriptor read from a file.
///
/// @param path the descriptor's file.
/// @param error the reason, when it is refused.
/// @return the descriptor, or nothing.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<DrizzleDescriptorDocument>
readDrizzleDescriptor(const QString& path, QString& error);

/// The byte length of a dense payload's samples for a source extent.
///
/// @return the length, or nothing when the extent is empty or too large.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<std::uint64_t>
drizzleDensePayloadSampleBytes(std::uint32_t sourceWidth, std::uint32_t sourceHeight);

/// A dense payload's envelope for a descriptor: its samples, refused when they are not the length the descriptor's
/// source extent needs.
///
/// @param document the descriptor the payload belongs to.
/// @param samples the samples.
/// @return the envelope, or nothing.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<DocumentEnvelope>
drizzleDensePayloadEnvelope(const DrizzleDescriptorDocument& document, const QByteArray& samples);

/// A dense payload read beside its descriptor: its envelope, its descriptor's identity, its file's length and
/// checksum as the descriptor states them, and its samples' length for the source extent.
///
/// @param path the payload's file.
/// @param document the descriptor that names it.
/// @param error the reason, when it is refused.
/// @return the samples, or nothing.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<QByteArray>
readDrizzleDensePayload(const QString& path, const DrizzleDescriptorDocument& document, QString& error);

} // namespace ImageIntegration
