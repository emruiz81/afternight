// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// A group's normalization coefficients as the stack consumes them: the global matches and the engine's
/// local-normalization document, put in the order of the frames the stack reads and limited to them.
///
/// The normalization stage measures every light its group lists; the stack may read fewer - a frame it could not read,
/// or one it left out - and reads them in its own order. Each frame the stack reads must be one the coefficients were
/// measured for, on the plane the stack reads it on; the coefficients of the frames it does not read are dropped, and
/// the local document's frame indices are the stack's.
///
/// The engine applies a local document over the frame the document names as the stack's own reference, and matches
/// the stack's reference by the identity after its local surface. So the stack takes the document's reference as its
/// own when it reads that frame; and a document that names no frame the stack reads - a reference of several lights
/// integrated, or one the stack left out - keeps the stack's reference, every post-local match restated against that
/// frame's own. Every light is still brought to the document's surfaces, and the master holds the levels of the frame
/// the stack took as its reference, as the stack's own measurement would.
///
/// Only coefficients measured under the settings the stack runs are read: the document's identity joins the inputs
/// fingerprint its sidecar states to the settings fingerprint of the group as the stack runs it. Which inputs the
/// coefficients were measured from is the planner's to judge before the run, and the stack holds every frame it reads
/// to them.

#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/stages/NormalizationArtifactSupport.h"

#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace ImageIntegration
{

/// What the stack hands the engine's source contract.
struct StackNormalizationInputs
{
    /// One global match per stack frame, under a global set; empty under a local one.
    std::vector<an::FrameNormalization> global;

    /// The local document over the stack's frames, under a local set.
    std::optional<an::LocalNormalizationDocument> local;

    /// The frame the stack takes as its reference to apply them, by its position among the frames it reads: a local
    /// document's reference when the stack reads it, the expectation's otherwise.
    std::uint32_t referenceFrame = 0u;
};

/// What the stack reads, which the coefficients must have been measured for.
struct StackNormalizationExpectation
{
    /// The logical identity of each frame the stack reads, in its order.
    std::vector<QString> logicalFrames;

    /// The frame the stack brings the others to, by its position among `logicalFrames`, unless a local document names
    /// another it reads.
    std::uint32_t referenceFrame = 0u;

    /// The plane the stack reads them on.
    NormalizationMeasurementPlane plane = NormalizationMeasurementPlane::RegisteredCanvas;

    /// The frames' channels and extent.
    std::uint32_t channelCount = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;

    /// The drizzle canvas, when the stack drizzles.
    std::optional<DrizzleAnalysisPlaneRecord> drizzlePlane;
};

/// Reads a group's coefficients for its stack: the document its sidecar describes, read only when it was measured under
/// the settings the group stacks with.
///
/// @param artifactPath the document.
/// @param group the group as the stack runs it.
/// @param referenceFrames the reference frames the stack names, as the normalization stage named them.
/// @return the set, or why it was not read.
[[nodiscard]] AFTERNIGHTPROCESSES_API NormalizationDocumentRead
readStackNormalizationCoefficients(const QString& artifactPath, const WorkflowManifest::IntegrationGroup& group,
                                   const std::vector<FrameId>& referenceFrames);

/// The coefficients for the frames the stack reads, in its order, and the reference the stack applies them over.
///
/// @param set the group's coefficients.
/// @param expectation what the stack reads.
/// @param reason the reason, when the coefficients do not describe it.
/// @return the inputs, or nothing.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<StackNormalizationInputs>
stackNormalizationInputsFor(const NormalizationCoefficientSet& set, const StackNormalizationExpectation& expectation,
                            QString& reason);

} // namespace ImageIntegration
