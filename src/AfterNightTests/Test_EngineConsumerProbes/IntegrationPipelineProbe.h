// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// An ImageIntegration-shaped pipeline over files, written against the engine's public headers alone.
//
// The second half of the orchestration probe. The first half (`IntegrationOrchestrationProbe.h`) runs a
// six-stage workflow over in-memory frames through the core operation facade; this half runs the
// workflow the product actually runs, through the domains a real integration needs, in the product's
// own order, over files on disk:
//
//   1. a master bias, stacked from bias frames;
//   2. a master dark, stacked from darks with the master bias subtracted on the way in;
//   3. a master flat, stacked from flats with both subtracted on the way in;
//   4. every light calibrated against the three masters;
//   5. stars detected on every calibrated light and matched to the reference light's;
//   6. every calibrated light warped onto the reference geometry;
//   7. every registered light measured and matched to the reference light's level;
//   8. the registered lights stacked with rejection, using the matches the previous stage prepared; and
//   9. the calibrated lights drizzled onto a finer grid through the same registration.
//
// **Everything crosses a stage boundary the way the product's does.** Frames go through files, and each
// file an earlier stage wrote carries, in its own header, the identity of the work it satisfies. A later
// run decides whether a stage's output is still valid by reading that header and nothing else. What
// the product keeps in a database - the transforms and the normalization matches - this probe keeps in
// memory, keyed by the same identities, because how a consumer stores a number is not a question about
// the engine.
//
// **What it proves that a unit test does not.** Whether the metadata that reaches the stacked frame
// describes the calibration, the registration and the stack that produced it; whether a second run can
// reuse every artifact from headers alone, and whether a rewritten input invalidates exactly the work
// that depends on it; whether stopping the run in the middle of the stack leaves every finished file
// intact and no half-written one anywhere; and what one progress display can be built from when some
// operations count their own units and others count nothing.
//
// It includes no consumer header, compiles no consumer source, and links no consumer target.

#pragma once

#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <cstdint>
#include <map>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace probe::integration
{

/// One stage of the pipeline, in the order they run.
enum class Stage : std::uint8_t
{
    /// The master bias.
    MasterBias,

    /// The master dark, bias-subtracted as it stacks.
    MasterDark,

    /// The master flat, bias- and dark-subtracted as it stacks.
    MasterFlat,

    /// Every light calibrated against the masters.
    CalibrateLights,

    /// Stars detected on every calibrated light and matched to the reference light's.
    RegisterLights,

    /// Every calibrated light warped onto the reference geometry.
    AlignLights,

    /// Every registered light matched to the reference light's level.
    NormalizeLights,

    /// The registered lights stacked with rejection.
    StackLights,

    /// The calibrated lights drizzled onto a finer grid.
    ///
    /// Deliberately the last enumerator, so a loop over the pipeline needs no separate count.
    DrizzleLights
};

/// How many stages the pipeline runs.
inline constexpr std::uint32_t kStageCount = static_cast<std::uint32_t>(Stage::DrizzleLights) + 1u;

/// The stage's own name, for a report or a progress line.
///
/// @param stage the stage to name. A value outside the enumeration is named `"UnknownStage"`.
/// @return a null-terminated string literal.
[[nodiscard]] const char* stageName(Stage stage) noexcept;

/// What one stage did.
struct StageRecord
{
    /// Which stage.
    Stage stage = Stage::MasterBias;

    /// How many artifacts the stage computed in this run.
    std::uint32_t produced = 0u;

    /// How many artifacts it found still valid and did not compute.
    std::uint32_t reused = 0u;

    /// How many progress reports the engine made while the stage ran.
    std::uint64_t engineReports = 0u;

    /// How many frame boundaries the workflow itself reported for the stage.
    std::uint32_t workflowReports = 0u;
};

/// What the workflow recorded about one artifact when it published it.
///
/// The product keeps this in its artifact database and history archive; the probe keeps it in memory.
/// It is the only place the history of an intermediate survives, because a file written by one stage
/// and read by the next carries its samples and its identity but not what was done to it.
struct ArtifactRecord
{
    /// Which stage produced it.
    Stage stage = Stage::MasterBias;

    /// The identities of the artifacts it was made from, in the stage's own order. Raw frames are
    /// inputs too, and are identified by their file stamps in the artifact's own identity rather than
    /// listed here.
    std::vector<std::string> inputs{};

    /// The lineage its image carried when it was written, oldest entry first.
    std::vector<std::string> lineage{};
};

/// Where a caller of the pipeline is told how far it has got.
///
/// Two kinds of report, because there are two kinds of progress: the workflow's, in frames and stages,
/// which it can always state; and an operation's, in its own units, which only the operations that
/// report anything can state. A display that wants one bar maps the second into the first.
class PipelineObserver
{
public:
    /// Nothing to release.
    virtual ~PipelineObserver() = default;

    /// One unit of a stage's own work has finished: a frame, or the stage's single artifact.
    ///
    /// @param stage the stage.
    /// @param completed how many of its units are finished.
    /// @param total how many it has.
    virtual void onStageProgress(Stage stage, std::uint32_t completed, std::uint32_t total) noexcept = 0;

    /// An engine operation reported progress while a stage was running.
    ///
    /// @param stage the stage the operation belongs to, which the engine does not know.
    /// @param report what the operation said. Valid only for the duration of the call.
    virtual void onEngineProgress(Stage stage, const an::ProgressReport& report) noexcept = 0;
};

/// What one run produced.
struct PipelineReport
{
    /// One record per stage that was reached, in order. A stopped run has fewer.
    std::vector<StageRecord> stages{};

    /// Every distinct stage name the engine's operations reported under, in the order first seen.
    std::vector<std::string> engineStageNames{};

    /// How many lights the registration matched to the reference, the reference included.
    std::uint32_t registeredLights = 0u;

    /// The lineage the stacked frame carries, as read back from its file's metadata, oldest first.
    std::vector<std::string> stackLineage{};

    /// The artifact identity the stacked frame's header carries.
    std::string stackIdentity{};

    /// The artifact identity the drizzled frame's header carries.
    std::string drizzleIdentity{};

    /// The stacked frame's median, in the sensor's codes.
    double stackMedian = 0.0;

    /// The drizzled frame's width, which is the scale times the lights' width.
    std::uint32_t drizzleWidth = 0u;
};

/// The frame set a pipeline runs over, and what it remembers between runs.
///
/// Held across runs on purpose, as the product's project is: the second run of the same pipeline over
/// the same files is what proves the artifacts can be recognised without reading their pixels.
class Pipeline
{
public:
    /// Writes a synthetic frame set into a directory and returns a pipeline over it.
    ///
    /// @param engine the context the frames are written with.
    /// @param root the directory everything is written under. Must exist.
    /// @param lightCount how many lights, at least three.
    /// @return the pipeline, or whatever writing a frame reported.
    [[nodiscard]] static an::Result<Pipeline> create(const an::EngineContext& engine, const std::string& root,
                                                     std::uint32_t lightCount);

    /// Runs every stage, reusing every artifact whose header still names the work it satisfies.
    ///
    /// @param engine the context the run's operations use.
    /// @param stopToken the token every operation is started with.
    /// @param observer where progress is reported, or null.
    /// @return the report, or the first failure a stage returned; `ErrorCode::Cancelled` when stopped.
    [[nodiscard]] an::Result<PipelineReport> run(const an::EngineContext& engine, std::stop_token stopToken,
                                                 PipelineObserver* observer);

    /// Rewrites one raw light with different noise, the way re-exporting a frame from a capture program
    /// would.
    ///
    /// @param engine the context the frame is written with.
    /// @param index which light.
    /// @return success, or whatever writing it reported.
    [[nodiscard]] an::Status rewriteLight(const an::EngineContext& engine, std::uint32_t index);

    /// Every file the pipeline has written under its root, relative to it, sorted.
    ///
    /// @return the paths, with `/` separators.
    [[nodiscard]] std::vector<std::string> files() const;

    /// The path of the stacked frame.
    ///
    /// @return the path.
    [[nodiscard]] std::string stackPath() const;

    /// What the workflow recorded about an artifact, looked up by the identity its file states.
    ///
    /// @param identity the artifact identity.
    /// @return the record, or null for an identity no run of this pipeline published.
    [[nodiscard]] const ArtifactRecord* artifact(const std::string& identity) const;

private:
    /// Builds over a root whose frames are already written.
    ///
    /// @param root the directory.
    /// @param lightCount how many lights.
    Pipeline(std::string root, std::uint32_t lightCount);

    /// The directory everything lives under.
    std::string m_root;

    /// How many lights.
    std::uint32_t m_lightCount = 0u;

    /// How many times each light has been rewritten, which seeds its noise.
    std::vector<std::uint64_t> m_lightRevisions{};

    /// The registration results a previous run computed, keyed by the identity of the work that
    /// produced them. The product keeps these in a database.
    std::map<std::string, std::vector<std::optional<an::RegistrationTransform>>> m_registrations{};

    /// The normalization matches a previous run computed, keyed by the identity of the work that
    /// produced them.
    std::map<std::string, std::vector<an::FrameNormalization>> m_normalizations{};

    /// What every run recorded about the artifacts it published, keyed by identity.
    std::map<std::string, ArtifactRecord> m_artifacts{};
};

} // namespace probe::integration
