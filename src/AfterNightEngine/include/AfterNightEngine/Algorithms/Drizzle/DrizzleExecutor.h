// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Streaming drizzle execution: ordered source reads, coordinate maps, accumulation and
/// finalization within one recomputed plan, handing over a complete owned result or nothing.
///
/// `executeDrizzle` composes the model and planner of `DrizzleModel.h` and `DrizzlePlanner.h`, the
/// geometry and maps of `DrizzleGeometry.h` and `DrizzlePixmap.h`, and the accumulation of
/// `DrizzleAccumulation.h`, over a caller's `DrizzleFrameSource`. It recomputes the plan from the
/// reader's actual access and the operation's current limits rather than trusting one handed to it,
/// copies the per-frame geometry into context-owned storage, and then, for each exclusive destination
/// band, visits every frame and every source band in order and finalizes the band. A tiled
/// destination repeats the source traversal; the plan and the report state that read multiplier.
///
/// `DrizzleResult` owns its floating image, its regenerated validity and the optional weight and
/// coverage maps; the storage moves to the caller without a copy and keeps the engine context
/// alive. Nothing is published on failure: every partially acquired buffer is released and the
/// caller's existing artifacts are never modified. `drizzleTransition` is the metadata step a caller
/// applies when it builds an image from the result.
///
/// The destination terms are Float64 sums in one fixed order, source flux is divided by the scale
/// squared, and the two output combinations are `DrizzleCombination`'s.
#pragma once

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleSource.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/ValidityMask.h>

namespace an
{

class ImageSemantics;
struct MetadataTransition;
struct DrizzleExecutionReport;

/// Immutable per-frame geometry and weights, copied into context-owned storage before reading.
struct DrizzleFrameParameters
{
    DrizzleMapDescriptor geometry; ///< Exact source/output extents and scale must match the model.
    /// Positive finite per-output-channel factors; those past the output's channel count must be one.
    std::array<double, 4u> weights{1.0, 1.0, 1.0, 1.0};
};

/// Caller-assigned identity for a complete in-memory artifact, independent of source codec cards.
/// Both fields are nonzero or both zero (anonymous); the engine generates no process-global identity.
/// It names the result this call hands over and nothing else: the executor returns it with a complete
/// result and never with a failure, and no writer reads it. A file written from the result carries the
/// workflow's own card-safe `ArtifactIdentity` from `IO/ArtifactIdentity.h`, which the output
/// transaction stamps through `OutputRequest::identity` when it commits; a workflow that wants the two
/// to agree derives both from its own manifest.
struct DrizzleArtifactIdentity
{
    std::uint64_t runId = 0u; ///< Caller-owned run identity, or zero for anonymous output.
    std::uint64_t artifactId = 0u; ///< Caller-owned output identity within that run.
};

/// Execution controls; the source's access overrides planning.access and all costs are recomputed.
struct DrizzleExecutionRequest
{
    DrizzlePlanningRequest planning{}; ///< Caps and retained residency, never a trusted edited plan.
    DrizzleArtifactIdentity artifact{}; ///< Published only with the complete result.
    /// Optional caller-owned diagnostics, reset at entry. Once the plan is made and the output image
    /// and mask are allocated, the report is copied here on every return, including failure, and
    /// describes the stages completed; an earlier argument, planning or allocation failure leaves it
    /// reset. Borrowed for this call only; must not alias inputs or be accessed concurrently. A
    /// failed run leaves `outputBytes` zero and publishes no image or artifact identity.
    DrizzleExecutionReport* diagnostics = nullptr;
};

/// Complete-operation counts; source selection counts exclude repeated destination-tile traversals.
struct DrizzleExecutionReport
{
    DrizzlePlan plan{}; ///< Recomputed plan used by this operation.
    DrizzlePixmapReport geometry{}; ///< Actual coordinate work, including halos and repeated passes.
    /// The accumulation implementation that ran.
    DrizzleAccumulationImplementation implementation = DrizzleAccumulationImplementation::Reference;
    std::uint64_t readRequests = 0u; ///< Completed reader callbacks, equal to plan.io.readRequests on success.
    std::uint64_t inputBytesRead = 0u; ///< Logical packed bytes, including masks and auxiliary samples.
    std::uint64_t selected = 0u; ///< Admitted source samples, each counted once in the sequence.
    std::uint64_t excluded = 0u; ///< Excluded source samples, each counted once.
    std::uint64_t nonFinite = 0u; ///< Validity-admitted non-finite samples excluded explicitly.
    std::uint64_t contributions = 0u; ///< Actual nonzero source-to-destination updates over all tiles.
    std::uint64_t validOutputSamples = 0u; ///< Samples with positive total weight and a representable value.
    std::uint64_t invalidOutputSamples = 0u; ///< No positive support; zero pixels with invalid mask bits.
    std::uint64_t outputBytes = 0u; ///< Context-owned bytes retained by result image, mask and maps.
    /// The largest signed total weight of any output sample, present exactly when the model asks for
    /// it; zero when no sample has positive support.
    std::optional<double> maximumWeight{};
};

/// Move-only complete artifact, with no sample copy at handover and no borrowed input storage.
/// Storages keep the owning engine context alive. Build an Image with fromStorage and apply
/// drizzleTransition to preserve acquisition facts, append lineage and withdraw source-only metadata.
struct DrizzleResult
{
    ImageStorageBytes image; ///< Packed planar floating output in descriptor's numeric domain.
    ImageDescriptor descriptor; ///< Geometry and meaning of the completed output.
    ValidityMask mask; ///< Regenerated explicit validity, one plane per output channel.
    ImageStorageBytes weightMap; ///< Optional Float32 signed total weights; empty unless requested.
    ImageStorageBytes coverageMap; ///< Optional UInt32 distinct contributing frames per sample.
    DrizzleArtifactIdentity artifact{}; ///< Explicit output identity, unchanged from the caller's request.
    DrizzleExecutionReport report{}; ///< Complete diagnostics, never a stale previous run's state.
};

/// Executes ordered source reads, geometry, accumulation and finalization within the recomputed plan.
///
/// Frames and source rows are visited in order for each exclusive destination band. Dense maps are
/// used only on explicit request; a refused reader, map or kernel never falls back to pixel access.
/// Normalized integer code units are mapped to floating full scale after the weighted mean, without
/// requantization. Raw sensor values keep their units under WeightedMean. Flux is divided by scale
/// squared and signed Lanczos lobes are retained. PeakNormalizedSum keeps the joint numerator,
/// divides by its positive maximum across every channel and band, and clamps to [0,1]; without a
/// positive maximum it preserves nonpositive sums. It narrows once, with any Float32 numerator
/// workspace planned. No positive finite support produces zero plus an invalid mask bit.
/// Non-finite terms, unrepresentable completed output or requested weight maps fail.
///
/// All image, frame, decoder, geometry and accumulator allocations are context-budgeted and released
/// on failure. Source residency and workspace are a caller-declared accounting contract. Reads are
/// serial; `concurrentFrameReads` reserves capacity but does not require parallel decoding.
/// Cancellation is checked before reads, between bands, inside kernels and before handover. Progress
/// counts consumed source bands, finalized destination bands, the peak-normalized sum's
/// normalization when that combination is requested, and one publication unit; nested operations
/// report inside the same range. The caller's existing artifacts are never mutated. A failure
/// publishes no result or identity.
/// @param engine owner of all result storage, matching execution's engine context.
/// @param execution operation's budgets, admitted workers, sinks and cancellation.
/// @param model checked homogeneous sequence contract.
/// @param source borrowed reader, used only during this call.
/// @param frames exactly `model.request().frameCount` immutable geometry and weight records.
/// @param request limits, retained residency and output identity.
/// @return the complete result. Otherwise `ErrorCode::NotInitialized` for an uninitialized engine or
///         execution; `ErrorCode::IncompatibleArguments` when they belong to different engine
///         contexts, the frame records do not number the model's frames or disagree with its
///         geometry, or the reader returns another representation or band extent;
///         `ErrorCode::InvalidArgument` for a half-stated artifact identity or an invalid frame
///         weight; `ErrorCode::InvalidData` for a square kernel over fewer than two source rows or
///         columns, a non-finite term, or an output sample or requested weight that cannot be
///         represented; `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the execution
///         stops; any failure the planner, the allocations, the geometry or the accumulation report;
///         a reader's own failure, keeping its code and gaining the frame index and first row as
///         context with the original as its cause; and `ErrorCode::AllocationFailed` or
///         `ErrorCode::ExternalFailure` for an exception the reader threw. No partially acquired
///         storage survives a failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DrizzleResult>
executeDrizzle(const EngineContext& engine, const ExecutionContext& execution, const DrizzleModel& model,
               DrizzleFrameSource& source, std::span<const DrizzleFrameParameters> frames,
               const DrizzleExecutionRequest& request = {});

/// Builds the metadata transition for a successfully completed drizzle artifact.
/// The processing state is marked registered, demosaiced for a CFA source and normalized for a
/// peak-normalized sum; the lineage entry names `an::executeDrizzle` with the kernel, frame count,
/// scale, drop fraction and combination.
/// @param model the model that produced the artifact.
/// @param source reference input semantics, borrowed and unchanged.
/// @return the transition carrying explicit validity, that processing state, an invalidated
///         astrometric solution, dropped source cards, withdrawn output intent and the lineage
///         entry; or the failure of building the new semantics or the transition.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MetadataTransition> drizzleTransition(const DrizzleModel& model,
                                                                                const ImageSemantics& source);

} // namespace an
