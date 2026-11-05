// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// The records the integration workflow persists about a registration, read and written in one place: a transform and
/// the source distortion it was solved over, the output plane's world coordinates, their fingerprints, and the text a
/// stored setting names a drizzle kernel, a filter arrangement, a demosaic method, an interpolation or a canvas by.
///
/// **One pixel convention.** Every position a record holds is an engine pixel coordinate: the centre of the first pixel
/// is (0.5, 0.5). A transform record states its format - 5 - and that convention, and is read only at that format: a
/// record of another format, or one that states none, is refused with the format found, never read as this one.

#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleModel.h>
#include <AfterNightEngine/Algorithms/Kernel/SamplingKernel.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace stage_utils
{

/// The transform record's format this build writes and reads: engine pixel coordinates, stated in the record.
inline constexpr int kRegistrationTransformSchemaVersion = 5;

/// The output plane's world-coordinate record's schema.
inline constexpr int kOutputPlaneWcsSchemaVersion = 1;

/// Where the source distortion a registration was solved over comes from.
enum class SourceDistortionPolicy
{
    /// The source frame's own header, read when the frame is.
    InheritMetadata,
    /// No distortion, whatever the header says.
    ExplicitNone,
    /// The model the record carries.
    ExplicitModel
};

/// The source distortion a registration was solved over: a policy and, for an explicit model, the model.
struct AFTERNIGHTPROCESSES_API SourceDistortionProvenance
{
    SourceDistortionPolicy policy = SourceDistortionPolicy::InheritMetadata;
    std::optional<an::SipDistortion> model;

    /// The provenance of a frame whose distortion is its header's.
    [[nodiscard]] static SourceDistortionProvenance inheritMetadata();

    /// The provenance of a frame registered with no distortion.
    [[nodiscard]] static SourceDistortionProvenance explicitNone();

    /// The provenance of a frame registered over a model of its own.
    [[nodiscard]] static SourceDistortionProvenance explicitModel(const an::SipDistortion& model);

    /// Whether the policy and the model agree: a model exactly when the policy is `ExplicitModel`.
    [[nodiscard]] bool consistent() const noexcept;

    friend bool operator==(const SourceDistortionProvenance& left, const SourceDistortionProvenance& right) = default;
};

/// A policy's stored key: "InheritMetadata", "ExplicitNone" or "ExplicitModel".
[[nodiscard]] QString sourceDistortionPolicyKey(SourceDistortionPolicy policy);

/// The policy a stored key names.
[[nodiscard]] std::optional<SourceDistortionPolicy> sourceDistortionPolicyFromKey(const QString& key);

/// A transform kind's stored key: "similarity", "affine", "homography", "polynomial2", "polynomial3", "polynomial4".
[[nodiscard]] QString registrationTransformKindKey(an::RegistrationTransformKind kind);

/// The kind a stored key names, case and space aside.
[[nodiscard]] std::optional<an::RegistrationTransformKind> registrationTransformKindFromKey(const QString& key);

/// A point as a two-element array.
[[nodiscard]] AFTERNIGHTPROCESSES_API QJsonArray pointToJsonArray(an::PixelCoordinate point);

/// A two-element array of finite numbers as a point.
[[nodiscard]] bool jsonArrayToPoint(const QJsonValue& value, an::PixelCoordinate& out);

/// A distortion as the format 5 record writes it: its reference pixel and its four components, each an order and a
/// seven-by-seven table indexed `[i][j]` for `u^i * v^j`.
[[nodiscard]] AFTERNIGHTPROCESSES_API QJsonObject sipDistortionToJson(const an::SipDistortion& sip);

/// A distortion read from a format 5 record.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool sipDistortionFromJson(const QJsonObject& object, an::SipDistortion& out,
                                                                 QString* error = nullptr);

/// A transform and its provenance as a format 5 record.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString
serializeTransformJson(const an::RegistrationTransform& transform,
                       const SourceDistortionProvenance& provenance = SourceDistortionProvenance::inheritMetadata());

/// What a transform record held.
struct ParsedTransformRecord
{
    an::RegistrationTransform transform;
    SourceDistortionProvenance provenance;

    /// The format the record states: `kRegistrationTransformSchemaVersion`, the only one read.
    int schemaVersion = 0;
};

/// A transform record at the format this build writes. A record of another format, or one that states none, is refused
/// naming the format found and the one this version reads; one that does not state the engine's pixel convention, or
/// whose fields contradict each other, is refused too - never guessed.
///
/// @param json the record.
/// @param error the reason, when it is refused.
/// @return the transform and its provenance, or nothing.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<ParsedTransformRecord> parseTransformRecord(const QString& json,
                                                                                                QString& error);

/// The transform a record holds, in engine coordinates, and its provenance when asked.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool parseRegistrationTransformJson(const QString& json,
                                                                          an::RegistrationTransform& out,
                                                                          SourceDistortionProvenance* provenance,
                                                                          QString& error);

/// The transform a record holds, in engine coordinates.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool
parseRegistrationTransformJson(const QString& json, an::RegistrationTransform& out, QString& error);

/// A transform's bytes for a fingerprint: its kind, matrix and polynomial.
[[nodiscard]] AFTERNIGHTPROCESSES_API QByteArray
fingerprintRegistrationTransform(const an::RegistrationTransform& transform);

/// A source distortion's bytes for a fingerprint: its policy and the model it resolves to.
[[nodiscard]] QByteArray fingerprintSourceDistortion(SourceDistortionPolicy policy,
                                                     const std::optional<an::SipDistortion>& resolvedModel);

/// A provenance's bytes for a fingerprint.
[[nodiscard]] QByteArray fingerprintSourceDistortion(const SourceDistortionProvenance& provenance);

/// The SHA-256, in hex, of every transform keyed by frame, in key order.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString
fingerprintTransforms(const std::unordered_map<QString, an::RegistrationTransform>& transforms);

/// The SHA-256, in hex, of a provenance's bytes.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString
fingerprintSourceDistortionHex(const SourceDistortionProvenance& provenance);

/// The SHA-256, in hex, of a policy and the model it resolves to.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString
fingerprintSourceDistortionHex(SourceDistortionPolicy policy, const std::optional<an::SipDistortion>& resolvedModel);

/// The SHA-256, in hex, of every frame's provenance, keyed as `transforms` is; a frame with none inherits its header.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString
fingerprintSourceDistortions(const std::unordered_map<QString, an::RegistrationTransform>& transforms,
                             const std::unordered_map<QString, SourceDistortionProvenance>& provenanceByFrame);

/// The SHA-256, in hex, of every frame's policy and the model it resolves to; a frame with no resolved model uses its
/// provenance's own.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString
fingerprintSourceDistortions(const std::unordered_map<QString, an::RegistrationTransform>& transforms,
                             const std::unordered_map<QString, SourceDistortionProvenance>& provenanceByFrame,
                             const std::unordered_map<QString, std::optional<an::SipDistortion>>& resolvedByFrame);

/// What the output plane's world-coordinate record states about the plane.
struct OutputPlaneWcsDescriptorInfo
{
    QString sourceKind;
    QString sourceFrameId;
    QString alignmentMode;
    int width = 0;
    int height = 0;
    bool sipCorrectionApplied = false;
};

/// Whether a record describes the plane a registration expects: its canvas, its size, its distortion policy and its
/// source - the reference frame, or no frame for an astrometric mosaic canvas.
[[nodiscard]] bool validateOutputPlaneWcsDescriptor(const OutputPlaneWcsDescriptorInfo& info,
                                                    const QString& expectedReferenceFrameId,
                                                    const QString& expectedAlignmentMode, int expectedWidth,
                                                    int expectedHeight,
                                                    const std::optional<bool>& expectedSipCorrection, QString& error);

/// Whether a card names part of a world-coordinate solution.
[[nodiscard]] bool isOutputWcsKeyword(const QString& key);

/// The world-coordinate cards among a frame's cards, as the record lists them.
[[nodiscard]] QJsonArray serializeOutputPlaneWcsKeywords(std::span<const an::CodecCard> cards);

/// The output plane's record: what the plane is and its world-coordinate cards.
[[nodiscard]] QString serializeOutputPlaneWcsJson(std::span<const an::CodecCard> cards,
                                                  const OutputPlaneWcsDescriptorInfo& info);

/// The output plane's record read: its world-coordinate cards, as FITS cards, and what it states about the plane.
///
/// @param json the record.
/// @param outCards receives the cards when asked.
/// @param outInfo receives what the record states when asked.
/// @param error the reason, when it is refused.
/// @return whether it was read.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool parseOutputPlaneWcsJson(const QString& json,
                                                                   std::vector<an::CodecCard>* outCards,
                                                                   OutputPlaneWcsDescriptorInfo* outInfo,
                                                                   QString& error);

/// Whether a path's container can carry a world-coordinate solution beside the image: FITS and XISF.
[[nodiscard]] bool supportsEmbeddedWcsOutput(const QString& path);

/// The drizzle kernel a stored setting names.
[[nodiscard]] bool parseKernel(const QString& kernel, an::DrizzleKernel& out);

/// The filter arrangement a stored setting names; "Auto" is known and names none, the image's own.
///
/// @param pattern the stored text.
/// @param out receives the arrangement, or nothing for "Auto".
/// @return whether the text was known.
[[nodiscard]] bool parseBayerPattern(const QString& pattern, std::optional<an::CfaPattern>& out);

/// The demosaic method a stored setting names; empty and "Auto" name none.
[[nodiscard]] std::optional<an::DemosaicMethod> parseDemosaicMethod(const QString& method);

/// The interpolation a stored registration setting names; empty, "Auto" and unknown text name none, which leaves the
/// choice to the operation.
[[nodiscard]] std::optional<an::KernelInterpolation> parseInterpolation(const QString& interpolation);

/// The canvas a stored registration setting names; unknown text is the standard canvas.
[[nodiscard]] ImageIntegration::RegistrationCanvasMode parseAlignmentMode(const QString& mode);

/// A canvas's stored key: "standard", "mosaic" or "intersection".
[[nodiscard]] QString alignmentModeKey(ImageIntegration::RegistrationCanvasMode mode);

} // namespace stage_utils
