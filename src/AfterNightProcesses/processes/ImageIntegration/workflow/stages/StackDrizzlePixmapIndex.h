// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// A group's drizzle descriptors as the drizzle stage writes them and reads them back to reuse them: where they are,
/// the group sidecar that lists one descriptor per light, and each light's engine map descriptor rebuilt from its
/// document and held to the engine's identity the document carries. The stack reads no descriptor: it builds each
/// light's map descriptor from the group's geometry the same way.
///
/// A descriptor names its geometry and nothing of the pixels, so the map it describes is generated when the stack reads
/// the light; a descriptor that names a dense payload carries the map's coordinates as well. The engine's identity is a
/// byte-exact record of the geometry, taken over a map descriptor of the light's extent, the output's extent and the
/// scale: both the stage and the stack build that descriptor here, the same way, so a document read back validates only
/// against the geometry it was written for.

#include "processes/ImageIntegration/core/CometStackingProductResolver.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/IntegrationError.h"
#include "processes/ImageIntegration/workflow/DrizzleDescriptorDocument.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleCanvas.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleMapDescriptor.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleMapIdentity.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Core/Result.h>

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

class ExecutionPreparationSnapshot;

namespace ImageIntegration
{

class WorkflowStageServices;

/// The stage that writes a group's drizzle descriptors, as its sidecar names it.
inline constexpr char kDrizzleStageId[] = "LightsDrizzle";

/// The suffix of a descriptor document's file.
inline constexpr char kDrizzleDescriptorSuffix[] = ".drizzle";

/// The suffix of a dense payload's file.
inline constexpr char kDrizzleDensePayloadSuffix[] = ".drizzlemap";

/// The directory a group's drizzle descriptors are written to: the group's configured directory, or the workflow's
/// default under the output directory.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString
drizzleDescriptorDirectory(const WorkflowManifest& manifest, const WorkflowManifest::IntegrationGroup& group);

/// The group sidecar in a descriptor directory.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString drizzleGroupSidecarPath(const QString& directory);

/// The engine's map descriptor of a light's geometry: a single-plane source of the light's extent drizzled onto an
/// output of the canvas's extent at the scale.
///
/// @param sourceWidth the light's width.
/// @param sourceHeight the light's height.
/// @param outputWidth the drizzled canvas's width.
/// @param outputHeight the drizzled canvas's height.
/// @param scale the enlargement.
/// @param mapping the transform, the offset and the source distortion.
/// @return the descriptor; or what describing the extents or the mapping refused.
[[nodiscard]] an::Result<an::DrizzleMapDescriptor>
drizzleMapDescriptorFor(std::uint32_t sourceWidth, std::uint32_t sourceHeight, std::uint32_t outputWidth,
                        std::uint32_t outputHeight, double scale, const an::DrizzleMappingRequest& mapping);

/// The engine's map descriptor a document states.
[[nodiscard]] an::Result<an::DrizzleMapDescriptor> drizzleMapDescriptorOf(const DrizzleDescriptorDocument& document);

/// The engine's identity of a map descriptor, as the bytes a document carries.
[[nodiscard]] an::Result<QByteArray> drizzleEngineIdentityOf(const an::DrizzleMapDescriptor& descriptor,
                                                             an::DrizzleNormalizationSpace space);

/// One light's descriptor as the stack reads it.
struct DrizzleDescriptorRecord
{
    /// The document, its engine identity validated against its own geometry.
    DrizzleDescriptorDocument document;

    /// Where the document is.
    QString descriptorPath;

    /// Where its dense payload is; empty when it names none.
    QString densePayloadPath;
};

/// A group's descriptors.
struct DrizzleDescriptorIndex
{
    /// The group sidecar's fields.
    QJsonObject sidecar;

    /// The drizzled canvas's extent every descriptor states.
    std::uint32_t outputWidth = 0u;
    std::uint32_t outputHeight = 0u;

    /// Each light's descriptor, by the light's identity key.
    std::unordered_map<QString, DrizzleDescriptorRecord> byFrame;
};

/// Why a group's descriptors were not read.
struct DrizzleDescriptorIndexRead
{
    std::optional<DrizzleDescriptorIndex> index;
    QString reason;

    /// Whether the sidecar was written by a newer version, which is neither read nor replaced unless the group is
    /// reprocessed.
    bool newerSchema = false;
};

/// Reads a group's descriptors: the sidecar of schema four stating the descriptors' current family, the stage and the
/// group; each descriptor it lists read whole, naming its light, stating the sidecar's output extent and validating
/// against the engine's identity of its own geometry; and a dense payload, when one is named, present at the length its
/// descriptor states. Nothing is read beyond the documents. Which lights must be described is the caller's to check:
/// the stage leaves out a light its registration did not place.
///
/// @param directory the group's descriptor directory.
/// @param group the group.
/// @return the descriptors or the reason.
[[nodiscard]] AFTERNIGHTPROCESSES_API DrizzleDescriptorIndexRead
readDrizzleDescriptorIndex(const QString& directory, const WorkflowManifest::IntegrationGroup& group);

/// One light of a drizzled group as its registration places it.
struct DrizzleFrameGeometry
{
    /// The light.
    FrameId frame;

    /// The file its samples are read from: the calibrated light, or the light itself when the run calibrates in memory.
    QString inputPath;

    /// Its transform onto the reference, in engine pixel coordinates.
    an::RegistrationTransform transform;

    /// The source distortion the transform was solved over, and what it resolves to for this light.
    stage_utils::SourceDistortionProvenance provenance;
    std::optional<an::SipDistortion> resolvedDistortion;

    /// Its extent and channels, and whether it is a filter-array mosaic.
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t channels = 0u;
    bool mosaic = false;
};

/// Where a drizzled group's lights land: each light the registration placed, the reference among them, and the canvas.
struct DrizzleGroupGeometry
{
    std::vector<DrizzleFrameGeometry> frames;
    std::size_t referenceIndex = 0u;
    an::DrizzleCanvas canvas;

    /// The canvas mode the canvas was planned by: the group's, or the standard canvas's in place of an intersection no
    /// whole output pixel is common to, which `intersectionReplaced` states and a route that places the lights says.
    ImageIntegration::RegistrationCanvasMode canvasMode = ImageIntegration::RegistrationCanvasMode::Standard;
    bool intersectionReplaced = false;

    /// The registration's settings version the transforms were read at.
    int settingsVersion = 0;

    /// The lights the registration did not place, which are left out.
    QStringList withoutTransform;

    /// The comet products the lights were placed by, for a comet stack; empty otherwise.
    QString cometProductsFingerprint;
    QString cometRunId;
    QString cometRole;
    QString cometOutputPlaneId;
};

/// The outcome of resolving a drizzled group's geometry.
struct DrizzleGeometryResolution
{
    std::optional<DrizzleGroupGeometry> geometry;
    IntegrationError error;
    bool cancelled = false;
};

/// The request a comet stack's products are resolved by: the group's registration state at its settings version, the
/// group's comet mode, the comet-aligned role and the group's lights.
///
/// @param metricsPath the registration database.
/// @param group the group.
/// @param settingsVersion the registration's settings version, or nothing for its latest.
/// @param drizzle whether the stack drizzles, which asks for transform roles rather than registered outputs.
/// @param counters the run's counters, or null.
/// @param error the reason, when the registration's state cannot be read.
/// @return the request, or nothing.
[[nodiscard]] std::optional<an::ui::CometStackingProductResolveRequest>
cometProductRequestFor(const QString& metricsPath, const WorkflowManifest::IntegrationGroup& group,
                       std::optional<int> settingsVersion, bool drizzle, WorkflowExecutionCounterAccumulator* counters,
                       QString& error);

/// A fingerprint of a comet stack's products: the run, the role, the plane, and each light's product as recorded.
[[nodiscard]] QString fingerprintCometProducts(const an::ui::CometStackingProductResolveResult& products);

/// Resolves where a drizzled group's lights land: their transforms from the registration's records - or, for a comet
/// stack, from the comet run's transform roles and its output plane - each light's header probed for its extent and the
/// distortion an inherited provenance resolves to, and the canvas the drizzle covers, planned by the group's canvas
/// mode from every light's mapped rectangle - the standard canvas in place of an intersection no whole output pixel is
/// common to (`planDrizzleOutputPlane`).
///
/// @param services the run's services; one operation is admitted for the header probes.
/// @param manifest the run's manifest.
/// @param group the drizzled group.
/// @param counters the run's counters, or null.
/// @return the geometry, or the stage's error.
[[nodiscard]] DrizzleGeometryResolution resolveDrizzleGroupGeometry(const WorkflowStageServices& services,
                                                                    const WorkflowManifest& manifest,
                                                                    const WorkflowManifest::IntegrationGroup& group,
                                                                    WorkflowExecutionCounterAccumulator* counters);

} // namespace ImageIntegration
