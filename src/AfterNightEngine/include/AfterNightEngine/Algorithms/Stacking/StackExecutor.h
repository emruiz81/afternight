// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The streaming stack executor: the passes a plan names, run in order over
/// the memory a working set holds, with the result handed over whole or not at all.
///
/// This header is the one executor. It takes a checked model, the frames' source contract, a source
/// that produces bands, and the facts the descriptor does not carry; it plans once, takes the plan's
/// memory once, and then follows the plan's passes in the plan's order: the whole-frame measurement
/// of a global match or a local surface, the whole-frame measurement of the local weight surface,
/// the combine over tiles, and the finalization. Every band of every frame reaches the tile through
/// the source path, every sample is combined by the accumulators, the column kernels or the
/// rejection estimators, and every number the result carries is what those operations reported.
///
/// Four rules hold for every stack.
///
/// **Nothing is allocated that the plan did not count.** The output, its mask and its maps, the
/// accumulators, the tables, the grids, the scratch, the tile, the read buffer and the workspace are
/// the working set's; the executor's own per-frame bookkeeping - the sample counters, the coverage
/// histogram, the rejection records, the band records - lives in the plan's frame-state block. The
/// composed operations allocate their own planned scratch: frame weighting during initialization,
/// normalization during measurement, and rejection diagnostics during finalization. A fitted local
/// document owns its grids, whose full residency is reserved before the first source read.
///
/// **A failure leaves nothing behind.** The result exists only on success: a refusal, a read that
/// fails, a sample the kernels refuse, a budget that no longer fits and a cancellation each return a
/// status, the working set returns every buffer to the engine context on its way out, and no image
/// is handed over. Cancellation is checked before every read and by every composed operation, so a
/// stop lands within one band of the request and is reported as `ErrorCode::Cancelled`, which a
/// caller can tell from a failure.
///
/// **Progress is one count with one meaning.** The unit is a frame read, the total is the plan's
/// read count plus the finalization, and the stage names the pass, so a progress display shows the
/// same thing for a plain average and a four-pass locally weighted rejection.
///
/// **The report is the composed operations' own figures.** Reads and bytes against the plan, what
/// the source path saw, what the accumulators and the kernels admitted and excluded, what the
/// weighting derived, what the local surface measured, what the rejection removed per frame and
/// which frames stood out, how the coverage is spread, and what the memory account says the run
/// held. The image's description is the method's, and `stackTransition` states the metadata change
/// a caller applies to the image it builds from the result.
///
/// **The output can leave tile by tile.** A caller that names a `StackOutputSink` takes each tile's
/// image, mask and maps as soon as the tile is finished - under sample history, or with terms that
/// hold one tile - so the working set holds the output for one tile rather than the whole of it,
/// and the plan's peak follows the tile. The caller writes the master as it is combined, into an
/// image an output transaction reserved, for one, and the result then hands over the description
/// and the report alone.
///
/// A cancelled stack is `ErrorCode::Cancelled` and nothing else; a frame that cannot be read fails
/// the stack; and the maps are handed over as the counts and values the working set holds rather
/// than divided by the frame count and clamped to the unit interval.

#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackWorkingSet.h>

#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameWeighting.h>
#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Algorithms/Stacking/StackQuality.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace an
{

class StackModel;

/// The engine context whose memory the stack runs in. Defined in `Runtime/EngineContext.h`.
///
/// Named here rather than included: the executor takes the working set's buffers from it, and a
/// caller reaching this header holds one and has therefore already included that header.
class EngineContext;

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named here rather than included, for the same reason.
class ExecutionContext;

/// What has been done to an image's samples. Defined in `Image/ImageSemantics.h`.
///
/// Named here rather than included: `stackTransition` reads the reference frame's meaning and
/// states the result's, and a caller applying the transition holds an image and has therefore
/// already included the image headers.
class ImageSemantics;

/// What an operation changed about an image's metadata. Defined in `Image/ImageMetadata.h`.
///
/// Named here rather than included, for the same reason as `ImageSemantics`.
struct MetadataTransition;

/// Frames that are already resident, offered whole or by region.
///
/// The source a caller holding decoded frames uses, and the one the direct tests and the benchmark
/// runner use: it borrows every frame as a `StackSourceBand` of all its rows and hands the executor
/// either the whole frame or a window `stackSourceRowsOf` cuts from it, so no sample moves on the
/// way in whatever the access. The frames, their layouts and their masks are the caller's and must
/// outlive the source. Move-only, because it keeps one window per frame that the bands it handed
/// out point into.
class ResidentStackFrameSource final : public StackFrameSource
{
public:
    /// Returns the windows; the frames themselves are the caller's and are not touched.
    AFTERNIGHTENGINE_API ~ResidentStackFrameSource() override;

    /// Takes over another source's frames and windows, leaving it holding nothing.
    ///
    /// @param other the source to take over. Left holding no frame; `frameCount` reports zero.
    AFTERNIGHTENGINE_API ResidentStackFrameSource(ResidentStackFrameSource&& other) noexcept;

    /// Returns this source's windows and takes over another's.
    ///
    /// @param other the source to take over. Left holding no frame.
    /// @return this source.
    AFTERNIGHTENGINE_API ResidentStackFrameSource& operator=(ResidentStackFrameSource&& other) noexcept;

    /// Copying is deleted: the windows a source hands out point into it.
    ResidentStackFrameSource(const ResidentStackFrameSource&) = delete;

    /// Copying is deleted. See the copy constructor.
    ResidentStackFrameSource& operator=(const ResidentStackFrameSource&) = delete;

    /// Borrows resident frames as a source.
    ///
    /// Each frame is a band of all its rows: `firstRow` zero, `frameIndex` its position, a
    /// non-null sample pointer and layout, and a mask exactly when its rule needs one. The frames
    /// are checked against each other for representation, width, height and channel count, which
    /// is what one stack's frames share; what they mean is the model's to check.
    ///
    /// Thread-safe with respect to the frames, which are only read. Allocates the frame records
    /// and one window per frame, and a failure it has to describe.
    ///
    /// @param frames the frames, in frame order. Borrowed for the source's life.
    /// @param access whether the executor reads them whole or by region.
    /// @return the source; `ErrorCode::EmptySelection` for no frames; `ErrorCode::InvalidArgument`
    ///         for a null pointer, a mask under a rule that reads none, no mask under the rule that
    ///         needs one, an unknown access, or a frame out of its place, naming the frame; and
    ///         `ErrorCode::IncompatibleArguments` when a frame's representation or geometry is not
    ///         the first frame's, naming the frame.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<ResidentStackFrameSource>
    create(std::span<const StackSourceBand> frames, StackSourceAccess access = StackSourceAccess::WholeFrame);

    /// How many frames this source holds.
    ///
    /// @return the count, or zero for a source that holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t frameCount() const noexcept;

    /// How this source reads a frame: what `create` was told.
    ///
    /// @return the access.
    [[nodiscard]] AFTERNIGHTENGINE_API StackSourceAccess access() const noexcept override;

    /// Produces the stated rows of one frame, borrowed from the resident frame.
    ///
    /// Under whole-frame access only the whole frame may be asked for; under region access any
    /// window inside the frame may be, and the window's layout and mask are kept in this source
    /// until the frame is read again. The buffer is never used.
    ///
    /// @param execution the running operation, whose cancellation is checked before the borrow is
    ///        handed back. Nothing else about it is read: no sample is decoded here.
    /// @param frameIndex which frame, below `frameCount`.
    /// @param firstRow the frame row the band begins at.
    /// @param rows how many rows, at least one.
    /// @param buffer the executor's slot, ignored.
    /// @return the band; `ErrorCode::OutOfRange` for a frame outside the source or a window
    ///         outside the frame; `ErrorCode::InvalidArgument` for no rows or, under whole-frame
    ///         access, for a window that is not the whole frame; `ErrorCode::NotInitialized` for
    ///         a source that holds nothing or an execution that holds nothing; or the execution's
    ///         cancellation or deadline.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<StackSourceBand> read(const ExecutionContext& execution,
                                                                    std::uint32_t frameIndex, std::uint32_t firstRow,
                                                                    std::uint32_t rows,
                                                                    std::span<std::byte> buffer) override;

private:
    /// The frame records and the windows, defined only inside the engine.
    class State;

    /// Takes ownership of a state the factory has already built.
    ///
    /// @param state the frames and windows. Never null on this path.
    explicit ResidentStackFrameSource(std::unique_ptr<State> state) noexcept;

    /// What this source holds, or null when it holds nothing.
    std::unique_ptr<State> m_state;
};

/// One finished tile of a stack's output, borrowed for the call that hands it over.
///
/// The rows of the combined image, its mask and every requested map that one tile covers, each packed
/// planar over the tile's rows - channel by channel, and inside a channel the rows in order at the
/// output's width - in the working set's memory, which the next tile reuses.
struct StackOutputRows
{
    /// The output row the tile's first row is.
    std::uint32_t firstRow = 0u;

    /// How many rows the tile holds: the plan's tile rows, or fewer for the last tile.
    std::uint32_t rows = 0u;

    /// The image's samples over the tile's rows, at the output representation.
    std::span<const std::byte> image{};

    /// Where they sit: the output's width and channels over the tile's rows, packed planar.
    const ImageLayout* layout = nullptr;

    /// Which of the tile's output samples received a sample, one bit-plane per channel over the
    /// tile's rows.
    const ValidityMaskView<const std::uint8_t>* mask = nullptr;

    /// Each map the request named over the tile's rows, indexed by `StackOutputMap`, at
    /// `stackOutputMapSampleType`; empty for a map the request did not name.
    std::array<std::span<const std::byte>, kStackOutputMapCount> maps{};
};

/// Where a stack's output goes tile by tile, instead of into its result.
///
/// The seam a caller implements to write the master as it is combined - into an image reserved
/// through an output transaction, for one - so neither the stack nor the caller holds the whole
/// output. The executor calls `write` once per tile, in row order, from the thread that drives the
/// run, as soon as the tile is finished and before the next tile is read.
class AFTERNIGHTENGINE_INTERFACE StackOutputSink
{
public:
    /// Releases whatever the sink holds; must not throw.
    virtual ~StackOutputSink() = default;

    /// Takes one finished tile.
    ///
    /// @param execution the running operation, whose cancellation the sink may check.
    /// @param rows the tile, borrowed for the call.
    /// @return success, or a failure the stack stops on and returns as its own.
    [[nodiscard]] virtual Status write(const ExecutionContext& execution, const StackOutputRows& rows) = 0;
};

/// What a caller states about the run, beyond the model and the contract.
///
/// A plain aggregate with a default for every member, so a caller names only what it is changing.
/// The planning facts the executor derives itself - the access from the source, whether the
/// coefficients and the grids are prepared from the contract, the prepared bytes from what the
/// contract holds - are not here, because a caller stating them differently from the objects it
/// handed over would be planning against a stack it is not running.
struct StackExecutionRequest
{
    /// How many frames the source may hold decoded at once. At least one; each multiplies the
    /// plan's read buffer. Under `StackSourceAccess::ConcurrentRegion` the executor reads up to this
    /// many frames' bands of a tile at once on the operation's workers, when every read lands in its
    /// frame's own band of the tile. When a stage converts each band instead, two or more let it read
    /// the next frame's band into the second slot on one worker while the others convert the current
    /// one, still one read at a time and in frame order. Otherwise it reads one frame at a time and
    /// lends every read the first slot.
    std::uint32_t concurrentFrameReads = 1u;

    /// The most bytes the working set may reach, or zero for no limit of the caller's own.
    std::uint64_t workingSetLimitBytes = 0u;

    /// The most rows one tile may hold, or zero for no cap. The tile is the unit of progress and
    /// cancellation inside the combine pass, which is what a caller caps it for. It is also what a
    /// streaming stack reads back: a source that decodes each frame's band into the tile has it
    /// folded after the last band lands, so a caller keeps such a tile inside the last-level cache.
    std::uint32_t maximumTileRows = 0u;

    /// How many rows the smallest piece the source decodes whole spans, or zero when a band costs
    /// its own rows alone: under region access a narrower tile is widened to it where the budget
    /// allows (`StackPlanningRequest::sourceGranularityRows`). A caller that also caps the tile
    /// keeps its cap at least this wide, or the cap decides.
    std::uint32_t sourceGranularityRows = 0u;

    /// What a sum's sums mean; inert for every other combination.
    StackSumDomain sumDomain = kDefaultStackSumDomain;

    /// How a rejection's columns are combined; inert without a rejection.
    StackRejectionCombinationPolicy rejectionPolicy = StackRejectionCombinationPolicy::SirilCompatible;

    /// What a local surface is fitted under, when the contract carries no document and the
    /// stack's scope applies one. Inert otherwise.
    LocalNormalizationRequest localNormalization{};

    /// Whether the fitted document carries a scale surface beside each frame's scale. Inert when
    /// the stack fits no document.
    bool fitLocalScaleGrid = false;

    /// One record per frame for a derived weighting source - noise, star count, width, image
    /// count or the photometric model - as `computeStackFrameWeights` reads them. Empty under
    /// `StackWeighting::Uniform` and `StackWeighting::Explicit`.
    std::span<const StackFrameMeasurement> measurements{};

    /// The weights a caller supplies under `StackWeighting::Explicit`, as
    /// `checkStackFrameWeights` accepts them. Empty otherwise.
    StackFrameWeights explicitWeights{};

    /// Whether and how the rejection map is searched for clusters after the combine. Read only
    /// when the stack writes the map the request names and `StackExecutionOutputs::clusters` has
    /// room for a detection.
    std::optional<RejectionClusterAnalysisRequest> clusterAnalysis{};

    /// Where the combined image, its mask and its maps go tile by tile as each tile is finished,
    /// or null to hand them over whole in the result. With a sink the working set holds them for
    /// one tile's rows (`StackMemoryPlan::outputCoversOneTile`), so the plan's peak no longer grows
    /// with the output, and the result's image, mask and maps hold nothing. A stack writes its
    /// output tile by tile only when every tile is finished as it is combined
    /// (`stackOutputCanCoverOneTile`), and runs no cluster analysis beside a sink, since the search
    /// reads the whole map. Borrowed for the call.
    StackOutputSink* outputSink = nullptr;
};

/// The caller-owned records the executor fills beside the result.
///
/// A plain aggregate of empty spans and a null pointer, so a caller names only what it wants
/// written. Every span is either empty or exactly the stated length, and is written only by a run
/// that succeeds up to the point that produces it: after a failure a span holds what the run
/// wrote before it stopped, which is nothing a caller should read.
struct StackExecutionOutputs
{
    /// Receives the frame weights the stack combined with, one per frame and channel in
    /// frame-major order, whatever the weighting source. Empty or exactly that many.
    std::span<double> frameWeights{};

    /// Receives the global match every frame was brought in by, in frame order, under a scope
    /// with a global method - the prepared match, or the measured one with the reference frame
    /// at the identity. Empty or exactly one per frame.
    std::span<FrameNormalization> frameNormalization{};

    /// Receives every frame's rejection rate under a rejection, as `summarizeFrameRejection`
    /// writes them. Empty or exactly one per frame.
    std::span<FrameRejectionRate> frameRejectionRates{};

    /// Receives the frames whose rejection rate stood out, in frame order, in its leading
    /// `StackExecutionReport::frameOutliers.flaggedFrameCount` entries. Empty or at least one per
    /// frame.
    std::span<std::uint32_t> flaggedFrames{};

    /// Receives the rejection clusters the analysis found, the largest first, in its leading
    /// `StackExecutionReport::clusters.clustersReported` entries. Its length is the most reported;
    /// empty runs no analysis.
    std::span<RejectionClusterDetection> clusters{};

    /// Receives the local-normalization document the stack fitted, when the contract carried
    /// none and the scope applies a surface; whatever it held is replaced. Null keeps the document
    /// inside the run, whose report states its residency either way.
    LocalNormalizationDocument* localNormalization = nullptr;
};

/// How many samples reached each output element, over the whole output.
///
/// Read from the counts of a streaming stack - over the whole output, or tile by tile as each tile
/// is finalized when its terms hold one tile, which tallies the same integers - or from the
/// coverage map a sample-history stack was asked to write; a sample-history stack that writes none
/// has no per-element count to summarize and says so.
struct StackCoverageSummary
{
    /// Whether a per-element count existed to read.
    bool available = false;

    /// How many output elements there are.
    std::uint64_t elements = 0u;

    /// How many of them received at least one sample.
    std::uint64_t elementsWithSamples = 0u;

    /// The fewest samples any element received.
    std::uint32_t minimum = 0u;

    /// The most samples any element received.
    std::uint32_t maximum = 0u;

    /// The mean count over every element.
    double mean = 0.0;

    /// The median count over every element, the mean of the two middle counts for an even
    /// number of elements, taken exactly from a histogram over the possible counts.
    double median = 0.0;

    /// The population standard deviation of the count over every element.
    double standardDeviation = 0.0;
};

/// What a completed stack reports beside its image.
///
/// Every figure is what a composed operation reported or what the executor counted against the
/// plan; nothing is estimated. A member that describes a stage the stack did not run keeps its
/// default, and the flags beside the summaries say which were produced.
struct StackExecutionReport
{
    /// How the stack ran, as `planStack` decided it.
    StackPlan plan{};

    /// Which stages the source path ran on every band.
    StackSourceStages stages{};

    /// Which implementation the dispatch table selected for the execution.
    BackendSelection backend{};

    /// Whether a compiled row ran for at least one band of the combine.
    bool compiledRowUsed = false;

    /// What a sum's sums mean in the output; inert for every other combination.
    StackSumDomain sumDomain = kDefaultStackSumDomain;

    /// Whether a bias was subtracted on the way in.
    bool biasSubtracted = false;

    /// Whether a dark was subtracted on the way in.
    bool darkSubtracted = false;

    /// Whether a flat was divided on the way in.
    bool flatFielded = false;

    /// How many of the plan's passes ran, which is every one on success.
    std::uint32_t passesRun = 0u;

    /// How many times a frame was read, over every pass; equals the plan's `io.frameReads`.
    std::uint64_t frameReads = 0u;

    /// How many bytes of stored input those reads covered; equals the plan's `io.logicalInputBytes`.
    std::uint64_t inputBytesRead = 0u;

    /// How many tiles the combine pass walked.
    std::uint32_t tilesCombined = 0u;

    /// How many samples the frames' rules admitted into the combine, over every band.
    std::uint64_t samplesAdmitted = 0u;

    /// How many samples the frames' rules kept out of the combine.
    std::uint64_t samplesExcludedByValidity = 0u;

    /// How many admitted samples an effective weight that was not positive and finite kept out.
    std::uint64_t samplesExcludedByWeight = 0u;

    /// How many output elements received at least one sample.
    std::uint64_t elementsWithSamples = 0u;

    /// How many received none, were written as zero and had their mask bit cleared.
    std::uint64_t elementsWithoutSamples = 0u;

    /// How many written values are not finite.
    std::uint64_t nonFiniteOutputs = 0u;

    /// Whether every frame's admitted sample count was counted, so a per-frame rate can be read.
    bool frameSampleCountsComplete = false;

    /// Whether a global match was measured by the stack rather than supplied.
    bool normalizationMeasured = false;

    /// Whether a local-normalization document was fitted by the stack rather than supplied.
    bool localNormalizationFitted = false;

    /// The residency of the document the stack fitted, as `preparedStateBytesOf` counts it,
    /// bounded by the plan's fittedNormalizationBytes; zero when the stack fitted none.
    std::uint64_t localNormalizationBytes = 0u;

    /// What the frame weighting derived, or the uniform disposition under `StackWeighting::Uniform`
    /// and `StackWeighting::Explicit`.
    StackFrameWeightingResult weighting{};

    /// Whether the local weight surface was measured by the stack.
    bool localWeightsMeasured = false;

    /// What the finished local weight surface looks like, when one was measured.
    LocalWeightingSummary localWeighting{};

    /// What the streaming method's finalization did; default under sample history, whose
    /// combination writes the output directly.
    StackMethodFinalizeOutcome finalize{};

    /// How many samples a rejection removed or replaced below the centre, counting repeats.
    std::uint64_t rejectedLow = 0u;

    /// How many above it, counting repeats.
    std::uint64_t rejectedHigh = 0u;

    /// How many frames were first rejected below the centre, summed over every element.
    std::uint64_t mappedLow = 0u;

    /// How many above it.
    std::uint64_t mappedHigh = 0u;

    /// Whether the per-frame rejection rates were summarized.
    bool rejectionSummarized = false;

    /// The rejection counts as rates, when summarized.
    FrameRejectionSummary rejection{};

    /// Whether the frames whose rate stood out were looked for.
    bool frameOutliersDetected = false;

    /// Which frames stood out, when looked for.
    RejectionFrameOutlierReport frameOutliers{};

    /// Whether a rejection map was searched for clusters.
    bool clustersAnalyzed = false;

    /// What the search found, when it ran.
    RejectionClusterReport clusters{};

    /// How the coverage is spread over the output.
    StackCoverageSummary coverage{};

    /// How many uniform frames the combination is worth per element, averaged over the elements
    /// that received a sample: the effective sample count of the weighted combination where the
    /// stack kept one, and the coverage otherwise.
    double effectiveFrameCount = 0.0;

    /// The square root of the frame count.
    double theoreticalSnrGain = 0.0;

    /// The square root of `effectiveFrameCount`.
    double actualSnrGain = 0.0;

    /// `effectiveFrameCount` over the frame count.
    double integrationEfficiency = 0.0;

    /// What the working set held against the plan, read after the last pass and before the
    /// result was handed over.
    StackMemoryAccount memory{};

    /// The engine-context bytes the composed diagnostics took and returned during the run, over
    /// every call that reported some.
    std::uint64_t diagnosticsWorkingBytes = 0u;
};

/// What a completed stack hands over.
///
/// The image's samples, its description, its validity mask, each requested map, and the report.
/// Everything owned here was taken from the engine context by the plan and is handed over with no
/// copy; the storages keep their own leases, so the context refuses to shut down while a result
/// lives. A caller builds an image with `ImageStorage::fromBytes` and `Image::fromStorage` on the
/// description, and applies `stackTransition` to it. A stack that handed its output to a
/// `StackOutputSink` tile by tile hands over none of it: the image, the mask and the maps hold
/// nothing, and the description and the report are what the result carries. Move-only, because its
/// members are.
struct StackResult
{
    /// The combined image's samples at the output representation, packed planar.
    ImageStorageBytes image;

    /// What the samples are: the method's output description.
    ImageDescriptor descriptor;

    /// Which output samples received a sample, one bit-plane per channel.
    ValidityMask mask;

    /// Each map the request named, indexed by `StackOutputMap`, holding memory exactly when the
    /// map was requested: one packed plane per channel of `stackOutputMapSampleType` samples.
    std::array<ImageStorageBytes, kStackOutputMapCount> maps;

    /// What the run reported.
    StackExecutionReport report;
};

/// Runs a stack from its first read to its handed-over result.
///
/// Checks the contract and the sum domain, plans the run from the model, the source's access and
/// what the contract holds prepared, takes the plan's persistent memory, derives or checks the
/// frame weights, and follows the plan's passes in order. A measurement pass reads every frame
/// whole through the source path - into the workspace at the tile width when the storage is
/// widened, into the read buffer otherwise - and measures it: the global statistics matched to the
/// reference frame's, the local surface fitted against the reference, or the local weight lattice.
/// The combine pass reads each tile of each frame - once per frame under a streaming strategy
/// over whole frames, once per tile otherwise - converts bands needing a source operation into
/// the tile and borrows resident regions directly when rejection needs no source operation,
/// or accumulates a streaming band where it was decoded, fills each band's local weights from the
/// finished grids, and combines the tile through the accumulators, the column kernels or the
/// rejection estimators. The finalization narrows a streaming stack's terms into the output under
/// the method's domain and composes the report's summaries.
///
/// Progress is reported once per frame read and once at the finalization, with the plan's total
/// and the pass's name as the stage; each pass also opens with the count so far under its own
/// name, and an informational diagnostic follows each pass. Cancellation is checked before and
/// after every read, by every composed operation, and before the result is handed over. On any
/// failure every buffer goes back to the engine context and nothing is handed over; the caller's
/// outputs hold what was written before the failure.
///
/// Thread-safe with respect to `engine`, `execution`, the model, the contract and the request,
/// which are only read; the source is read from one thread at a time; the outputs are written.
/// Runs on the threads the operation was admitted. Allocates the plan's buffers from the engine
/// context, the diagnostics' working memory for their calls, the fitted document's grids when the
/// stack fits one, and a failure it has to describe.
///
/// @param engine the context whose memory resource and budget own every buffer. Cannot shut down
///        while the result lives.
/// @param execution the operation the stack runs as. Must hold something and must run in `engine`.
/// @param model the checked stack.
/// @param source where the frames come from, read one band at a time from the calling thread. An
///        exception its read throws is translated into that read's failure.
/// @param contract what the frames are and what brings them in, as `checkStackSourceContract`
///        accepts it.
/// @param request the facts about the run the model and the contract do not carry.
/// @param outputs the caller's records to fill.
/// @return the result; `ErrorCode::NotInitialized` when `engine` or `execution` holds nothing;
///         `ErrorCode::InvalidArgument` when `execution` runs in another engine context, when the
///         source reports an access this build does not define, or when a band it produces has no
///         samples or layout or a mask that disagrees with its rule, naming `source`;
///         `ErrorCode::IncompatibleArguments` when an output span is not the stated length,
///         naming it, when the weighting source needs measurements or explicit weights that were
///         not given, when the contract states prepared coefficients and lacks a frame's match, or
///         when a band the source produces is of another representation, geometry, frame or first
///         row than was asked for, or when an output sink is given beside a cluster analysis that
///         would run, naming `clusterAnalysis`; `ErrorCode::LimitExceeded` when the memory of a fitted
///         local-normalization document or of the cluster analysis cannot be counted; whatever
///         `checkStackSourceContract`, `checkStackSumDomain`, `planLocalNormalization`,
///         `checkStackFrameWeights`, `planStack`, `StackWorkingSet::create` and
///         `StackWorkingSet::beginPass` refuse; a read's failure with the read as its cause,
///         naming the frame; whatever a composed measurement, conversion, accumulation,
///         combination or summary refuses; the output sink's failure, as it returned it; and
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from the checkpoint. A stack whose
///         sink is refused by `planStack` - its tiles not finished as they are combined - is
///         `ErrorCode::IncompatibleArguments` naming `outputByTile`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackResult>
executeStack(const EngineContext& engine, const ExecutionContext& execution, const StackModel& model,
             StackFrameSource& source, const StackSourceContract& contract, const StackExecutionRequest& request,
             const StackExecutionOutputs& outputs);

/// Builds the metadata transition a stacked image takes.
///
/// The new meaning is the reference frame's with what the stack did to it: normalized when a
/// stage brought the frames to the reference, calibrated where masters were applied on the way
/// in, and valid where the mask says, because `kStackOperationEffect` regenerates validity as an
/// explicit mask. The solution is preserved, the source cards are dropped, any output decision is
/// withdrawn, and provenance records the combination, the rejection, the weighting, the
/// normalization scope, the frame count and the sum domain, so a later caller never infers them
/// from the pixels.
///
/// Thread-safe. Allocates the lineage entry's text and whatever a failure has to describe.
///
/// @param model the checked stack.
/// @param report what the run reported, whose stages and calibration flags are read.
/// @param source the reference frame's current processing meaning.
/// @return the transition, ready for `Image::transition`; whatever rebuilding the semantics or
///         the transition reports.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MetadataTransition>
stackTransition(const StackModel& model, const StackExecutionReport& report, const ImageSemantics& source);

} // namespace an
