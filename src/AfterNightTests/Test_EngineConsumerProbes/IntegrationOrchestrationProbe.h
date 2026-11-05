// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// An ImageIntegration-shaped orchestration, written against the engine's public headers alone.
//
// This is a probe, not a product and not an engine feature. It exists to find out what using the
// core operation surface is like from the outside while that surface is still cheap to reshape: it
// is written the way the multi-stage stacking workflow in the product is written - stages in
// sequence, each producing an artifact a later stage consumes, each reporting progress, all of them
// stoppable, and every artifact carrying the lineage of what made it. A friction it hits is a
// finding about the published surface.
//
// It includes no consumer header, compiles no consumer source, and links no consumer target. The
// only thing it knows about AfterNight is the engine's published include root, which is the
// property that makes it evidence rather than a demonstration.
//
// **The workflow, in engine terms.** Six stages over a small synthetic frame set:
//
//   1. a master dark, averaged from several dark frames;
//   2. calibration of each light: the master dark subtracted, then the flat divided out;
//   3. a defect mask, built from a value range on the calibrated lights and combined across them;
//   4. normalization of each calibrated light onto the reference light's level;
//   5. integration: the normalized lights averaged into one frame; and
//   6. an output decision, stated as an output intent on the integrated frame.
//
// **What it proves that a unit test does not.** A unit test calls one operation and checks its
// answer. This carries an image through six of them, and the questions it can therefore ask are the
// ones a consumer asks: does the metadata that arrives at stage six describe what actually happened;
// can a caller decide, without reading pixels, whether a stage's artifact from a previous run is
// still valid; does stopping the workflow leave the artifacts that were finished intact; and can one
// progress display be built from operations that each count their own work.

#pragma once

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace probe
{

/// One stage of the workflow, in the order they run.
///
/// Named rather than indexed because a report a person reads names what did not finish, and because
/// the artifact cache is keyed on this rather than on a position that changes when a stage is added.
enum class Stage : std::uint8_t
{
    /// The master dark, averaged from the dark frames.
    MasterDark,

    /// Every light with the master dark subtracted and the flat divided out.
    Calibrate,

    /// The defect mask, built from the calibrated lights and combined across them.
    DefectMask,

    /// Every calibrated light scaled onto the reference light's level.
    Normalize,

    /// The normalized lights averaged into one frame.
    Integrate,

    /// The decision about how the integrated frame is to be written.
    ///
    /// Deliberately the last enumerator, so a loop over the workflow needs no separate count.
    StateOutput
};

/// The stage's own name, for a report or a progress line.
///
/// @param stage the stage to name. A value outside the enumeration is named `"UnknownStage"`.
/// @return a null-terminated string literal.
[[nodiscard]] const char* stageName(Stage stage) noexcept;

/// The identity the probe gives the flat field.
inline constexpr std::uint64_t kFlatIdentity = 1u;

/// The identity the probe gives a running accumulator that never reaches the cache.
///
/// An accumulator between two chain steps is nobody's artifact and never a cache-key input, so it
/// needs an identity only so that one key-building path can be used everywhere.
inline constexpr std::uint64_t kRunningIdentity = 2u;

/// The first identity the probe gives a light frame; the rest follow by index.
inline constexpr std::uint64_t kLightIdentityBase = 1000u;

/// The first identity the probe gives a dark frame; the rest follow by index.
inline constexpr std::uint64_t kDarkIdentityBase = 2000u;

/// What one input contributed to an artifact's identity.
///
/// Three numbers rather than two. The engine gives an image a pixel generation and a semantic
/// generation and says explicitly that they are only comparable with earlier generations of the same
/// image - every image starts at zero - so a consumer keeping a cache has to pair them with an
/// identity of its own. Leaving the identity out is not a theoretical mistake: the first version of
/// this probe did, and it reused a stale artifact after its input had been rewritten.
struct InputRevision
{
    /// Which frame this was, in the probe's own numbering.
    std::uint64_t identity = 0u;

    /// The frame's pixel generation when the artifact was produced.
    std::uint64_t pixelGeneration = 0u;

    /// The frame's semantic generation when the artifact was produced.
    std::uint64_t semanticGeneration = 0u;

    /// Whether two revisions describe the same state of the same frame.
    ///
    /// @param left one revision.
    /// @param right the other.
    /// @return true when all three numbers are equal.
    [[nodiscard]] friend bool operator==(const InputRevision& left, const InputRevision& right) = default;
};

/// What identifies one stage's artifact, so a later run can tell whether it is still valid.
///
/// The engine deliberately does not own this - `Image` carries two generations and says the
/// composite key belongs to whoever owns the cache - so the probe builds it, which is exactly the
/// thing worth finding out: whether the published surface exposes enough for a consumer to build one
/// without reading a single pixel.
struct ArtifactKey
{
    /// Which stage produced the artifact.
    Stage stage = Stage::MasterDark;

    /// The stage's own inputs, rendered as text. Never parsed; compared only for equality.
    std::string parameters{};

    /// Each input's identity and revision, in the stage's own input order.
    std::vector<InputRevision> inputs{};

    /// Whether two keys identify the same artifact.
    ///
    /// @param left one key.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend bool operator==(const ArtifactKey& left, const ArtifactKey& right) = default;
};

/// One frame the workflow is holding, and the identity it holds it under.
///
/// A borrowed pointer with a number beside it. The pointer is valid for as long as whatever owns the
/// frame keeps it, which for a cached artifact is the workflow.
struct FrameRef
{
    /// The probe's own identity for the frame.
    std::uint64_t identity = 0u;

    /// The frame, borrowed. Null for a reference that names no frame.
    const an::Image<float>* image = nullptr;
};

/// What one stage did, or did not do.
struct StageRecord
{
    /// Which stage this is about.
    Stage stage = Stage::MasterDark;

    /// Whether the stage ran its operations rather than reusing a previous run's artifact.
    bool executed = false;

    /// Whether the stage reused an artifact a previous run left in the cache.
    bool reused = false;

    /// How many engine operations the stage started. Zero for a fully reused stage.
    std::uint32_t operations = 0u;

    /// The key the stage's artifact was stored under, or the key it looked for.
    ArtifactKey key{};
};

/// What one run of the workflow produced.
struct RunReport
{
    /// One record per stage that was reached, in order. A stopped run has fewer than six.
    std::vector<StageRecord> stages{};

    /// How many progress reports the workflow's own sink received from the engine.
    std::uint64_t engineProgressReports = 0u;

    /// How many stage boundaries the workflow reported to its caller.
    std::uint64_t workflowProgressReports = 0u;

    /// The lineage the integrated frame carries, oldest entry first.
    std::vector<std::string> integratedLineage{};

    /// How many of the frame's pixels the defect mask says mean something.
    std::uint64_t validPixels = 0u;

    /// How many pixels the defect mask covers.
    std::uint64_t totalPixels = 0u;

    /// The integrated frame's mean, for a caller that wants one number to compare two runs by.
    double integratedMean = 0.0;

    /// The integrated frame's semantic generation when the run completed.
    std::uint64_t integratedSemanticGeneration = 0u;

    /// Whether the integrated frame carries an output decision.
    bool statedOutputIntent = false;
};

/// Where a caller of the workflow is told how far it has got.
///
/// The workflow's own progress, not the engine's: one report per stage boundary, in units of stages.
/// It exists because the engine reports per operation and a workflow is not an operation - see the
/// friction record.
class WorkflowObserver
{
public:
    /// Nothing to release.
    virtual ~WorkflowObserver() = default;

    /// One stage boundary.
    ///
    /// @param completed how many stages are finished.
    /// @param total how many stages the workflow will run.
    /// @param stage the stage that just finished.
    virtual void onStageCompleted(std::uint32_t completed, std::uint32_t total, Stage stage) noexcept = 0;
};

/// The synthetic frames one run works on, and the cache it may reuse.
///
/// Held across runs on purpose: the second run of the same workflow over the same frames is what
/// proves an artifact can be reused without reading its pixels.
class Workflow
{
public:
    /// Builds the workflow's frames in the given engine context.
    ///
    /// @param engine the context every image and mask takes its memory from. It must outlive this.
    /// @param lightCount how many light frames to synthesize. At least two.
    /// @param darkCount how many dark frames to average into the master dark. At least two.
    /// @return the workflow, or whatever building a frame reported.
    [[nodiscard]] static an::Result<Workflow> create(const an::EngineContext& engine, std::uint32_t lightCount,
                                                     std::uint32_t darkCount);

    /// Moves a workflow. Copying is deleted: it owns images, which are themselves move-only.
    Workflow(Workflow&&) noexcept = default;

    /// Moves a workflow.
    Workflow& operator=(Workflow&&) noexcept = default;

    /// Copying is deleted.
    Workflow(const Workflow&) = delete;

    /// Copying is deleted.
    Workflow& operator=(const Workflow&) = delete;

    /// Nothing to release beyond the images.
    ~Workflow() = default;

    /// Runs every stage, reusing whatever the cache still identifies.
    ///
    /// @param stopToken the token every stage's execution context is built with. A stop requested
    ///        part-way leaves the finished stages' artifacts in the cache and reports
    ///        `ErrorCode::Cancelled`.
    /// @param observer where stage boundaries are reported, or null.
    /// @return the report, or the first failure a stage returned.
    [[nodiscard]] an::Result<RunReport> run(std::stop_token stopToken, WorkflowObserver* observer);

    /// Marks the reference light as modified, the way a consumer editing a frame between runs would.
    ///
    /// @return success, or `ErrorCode::NotInitialized` when the frame holds nothing.
    [[nodiscard]] an::Status touchReferenceLight();

    /// How many artifacts the cache is holding.
    ///
    /// @return the entry count.
    [[nodiscard]] std::size_t cachedArtifactCount() const noexcept;

private:
    /// Builds from frames that have already been synthesized.
    ///
    /// @param engine the context the frames came from.
    /// @param lights the light frames.
    /// @param darks the dark frames.
    /// @param flat the flat field.
    Workflow(const an::EngineContext& engine, std::vector<an::Image<float>> lights, std::vector<an::Image<float>> darks,
             an::Image<float> flat) noexcept;

    /// One artifact the cache is holding.
    struct CacheEntry
    {
        /// What identifies it.
        ArtifactKey key{};

        /// The identity later keys refer to it by.
        std::uint64_t identity = 0u;

        /// The frame itself.
        an::Image<float> image{};
    };

    /// The artifact stored under a key, or a reference naming no frame.
    ///
    /// @param key what to look for.
    /// @return the artifact, or a default reference when the cache does not hold it.
    [[nodiscard]] FrameRef cachedFrame(const ArtifactKey& key) const noexcept;

    /// Puts an artifact in the cache, replacing one stored under the same key.
    ///
    /// @param key what identifies it.
    /// @param image the frame, moved in.
    /// @return the stored artifact and the identity it now has.
    [[nodiscard]] FrameRef store(const ArtifactKey& key, an::Image<float>&& image);

    /// Builds the key one stage's artifact is identified by.
    ///
    /// @param stage which stage.
    /// @param parameters the stage's own inputs, as text.
    /// @param inputs the frames it read, in its own order.
    /// @return the key.
    [[nodiscard]] static ArtifactKey makeKey(Stage stage, std::string parameters, std::span<const FrameRef> inputs);

    /// Runs one pointwise chain from bound sources into a fresh destination, and records what it did.
    ///
    /// The single place this probe touches the executor, so the shape of that call site is written
    /// down once: seed the destination from the primary input, bind, run, ask the chain what it did,
    /// state the outcome, hand the transition to the image.
    ///
    /// @param stopToken the workflow's stop token.
    /// @param progress where the engine's progress goes, or null.
    /// @param program the chain to run.
    /// @param sources the frames it reads, primary first.
    /// @param descriptor how the destination is described.
    /// @param semantics what the result means, when the chain says its meaning is restated.
    /// @param operation what to record in the result's lineage.
    /// @param detail what to record beside it.
    /// @return the destination frame, or the first failure.
    [[nodiscard]] an::Result<an::Image<float>>
    runChain(std::stop_token stopToken, an::ProgressSink* progress, const an::PointwiseProgram& program,
             std::span<const FrameRef> sources, const an::ImageDescriptor& descriptor,
             const std::optional<an::ImageSemantics>& semantics, std::string operation, std::string detail);

    /// The context every image and mask takes its memory from. Borrowed, never owned.
    const an::EngineContext* m_engine = nullptr;

    /// The light frames, in the order they were synthesized. The first is the reference.
    std::vector<an::Image<float>> m_lights{};

    /// The dark frames.
    std::vector<an::Image<float>> m_darks{};

    /// The flat field.
    an::Image<float> m_flat{};

    /// What previous runs left behind, keyed by artifact identity.
    ///
    /// A deque rather than a vector because a stage holds a pointer to an earlier stage's artifact
    /// while later stages are still being stored, and a vector would move every entry out from under
    /// those pointers as it grew. The engine's images are move-only and uncopyable, so a consumer
    /// keeping artifacts has to own storage whose addresses do not move.
    std::deque<CacheEntry> m_cache{};

    /// The last identity handed to a stored artifact. Artifact identities start above every frame
    /// identity the probe assigns, so the two numbering schemes cannot collide.
    std::uint64_t m_nextIdentity = 10000u;
};

} // namespace probe
