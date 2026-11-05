// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - scenario identity and the real-data corpus manifest.
//
// A scenario carries a stable ID, operation version, parameter set, input descriptor, corpus
// identity, expected correctness class, work counts, and allowed backends. Two runs can only be
// read together when both were driven from the same scenario, so that list is the schema below.

#pragma once

#include "Descriptors.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace an::bench
{

/// A scenario's parameters, kept as ordered name/value text rather than a typed variant.
///
/// Text keeps the common layer free of engine parameter types, and ordering makes the serialized
/// form stable so two runs of the same scenario produce byte-comparable JSON. The runner's adapter
/// turns these into the engine's parameter objects.
using ParameterSet = std::map<std::string, std::string>;

/// Where a scenario's input comes from.
enum class InputSource : std::uint8_t
{
    Synthetic, ///< Generated deterministically from `corpusId` and `seed`.
    Corpus, ///< A real single-file dataset resolved through the corpus manifest.

    /// A real multi-frame dataset: the scenario is handed verified paths and the engine reads them.
    ///
    /// Distinct from `Corpus` because the two are consumed differently and the runner has to know
    /// which before it asks for anything - one yields a buffer, the other a list of frames the
    /// engine opens itself.
    CorpusFrameSet
};

/// One measurable operation, described independently of the engine that runs it.
struct ScenarioSpec
{
    std::string id; ///< Stable, unique, and never reused. e.g. "transfer.u16_to_f32".
    std::string group; ///< Scenario group in the benchmark taxonomy, e.g. "transfer".
    std::uint32_t operationVersion = 1; ///< Bumped when the operation's meaning changes.
    ParameterSet parameters;

    InputSource inputSource = InputSource::Synthetic;
    SampleDescriptor input;
    std::string corpusId; ///< Dataset ID for Corpus inputs; generator ID for Synthetic ones.
    std::uint64_t seed = 0;

    CorrectnessClass correctnessClass = CorrectnessClass::BitExact;
    double tolerance = 0.0; ///< Interpretation depends on `correctnessClass`.

    /// Work counts, so results can be normalized into rates rather than bare durations.
    std::uint64_t bytesProcessed = 0;
    std::uint64_t elementsProcessed = 0;
    std::string workUnit; ///< Optional domain unit: "stars", "frames", "taps", ...

    std::vector<BackendId> allowedBackends;

    /// Empty when the scenario is well formed; otherwise one message per problem.
    [[nodiscard]] std::vector<std::string> validate() const;
};

/// Parameter that repeats one operation inside a single clocked benchmark region.
///
/// The reported time remains per operation. The batch exists so a sub-millisecond operation can
/// produce a clocked region above a host's duration floor without changing its input or output.
inline constexpr const char* kTimedBatchParameter = "timedBatch";

/// The number of operations one timing sample contains.
///
/// An absent parameter means one. `ScenarioSpec::validate` refuses zero, malformed and excessively
/// large values, so a runner may treat an empty result as a malformed scenario rather than silently
/// changing the protocol.
///
/// @param scenario the scenario whose timing protocol is being read.
/// @return the positive batch count, or nothing when the parameter is malformed.
[[nodiscard]] std::optional<std::uint32_t> timedBatchIterations(const ScenarioSpec& scenario) noexcept;

/// One real dataset, described by the manifest rather than by a hard-coded path.
struct CorpusEntry
{
    std::string id;
    std::vector<std::string> tags;
    std::string relativePath; ///< Resolved against the root given at run time.
    std::string sha256; ///< Lowercase hex; detects a moved or re-exported file.
    std::uint64_t sizeBytes = 0;
    std::string licenseOrProvenance;
    std::string redistribution; ///< "committed", "downloadable", or "local-only".
    std::string privacyClass;
    SampleDescriptor expected; ///< What the file is expected to decode to.
    std::string preprocessing;
    std::string preprocessingVersion;
    std::string goldenPath;
    std::string goldenSha256;
    double goldenTolerance = 0.0;
    std::vector<std::string> representativeFor;

    [[nodiscard]] std::vector<std::string> validate() const;
};

/// The corpus manifest and the rules for resolving it on a machine that may not hold the data.
///
/// The real corpus is host-local: the maintainer supplies datasets on the benchmark machine and
/// they are never committed. So a missing entry is a first-class outcome -
/// `resolve` reports it as skipped by ID rather than failing the run or, worse, silently
/// substituting a different file.
/// One member of a multi-frame dataset.
///
/// A frame carries its own size and digest rather than inheriting the set's, because the failure
/// this guards against is one file being re-exported or replaced while the rest stay as they were.
struct CorpusFrame
{
    std::string relativePath; ///< Resolved against the root given at run time.
    std::string sha256; ///< Lowercase hex over the frame's raw-dump descriptor.
    std::uint64_t sizeBytes = 0;

    [[nodiscard]] std::vector<std::string> validate() const;
};

/// The frames that play one part in a pipeline: the lights, the flats, the darks, the bias.
///
/// Ordered, and the order is part of the dataset's identity. A stacking result depends on the order
/// its inputs arrive in, so a set whose frames are enumerated differently on two machines is a
/// different input even when it holds the same files.
struct CorpusFrameRole
{
    std::string role; ///< "lights", "flats", "darks", "bias", ...
    std::vector<CorpusFrame> frames;
};

/// A named dataset made of many frames, for scenarios that measure a pipeline rather than a kernel.
///
/// Kept separate from `CorpusEntry` rather than folded into it. A single-file dataset is read whole
/// into a fixture; a set is far too large for that - the reference set is 232 frames and about
/// 21.7 GB - so it resolves to a list of frames to read one at a time. Two things that are consumed
/// so differently are clearer as two types than as one type with a mode.
struct CorpusFrameSet
{
    std::string id;
    std::vector<std::string> tags;
    std::string licenseOrProvenance;
    std::string redistribution; ///< "committed", "downloadable", or "local-only".
    std::string privacyClass;
    SampleDescriptor expected; ///< What every frame in the set is expected to decode to.
    std::vector<CorpusFrameRole> roles;
    std::vector<std::string> representativeFor;

    [[nodiscard]] std::size_t frameCount() const;
    [[nodiscard]] std::uint64_t totalBytes() const;
    [[nodiscard]] const CorpusFrameRole* role(const std::string& name) const;

    /// The set's identity, over every member's recorded digest in role and frame order.
    ///
    /// Computed from the manifest, so a set has an identity before any of it has been read. That is
    /// what lets a result record which data it measured without a runner first hashing 21.7 GB, and
    /// it changes if a frame is replaced, added, removed, or merely reordered.
    [[nodiscard]] std::string identityDigest() const;

    [[nodiscard]] std::vector<std::string> validate() const;
};

class CorpusManifest
{
public:
    struct Resolution
    {
        bool available = false;
        std::string absolutePath;
        std::string skipReason; ///< Populated when `available` is false.
    };

    void add(CorpusEntry entry);
    void addSet(CorpusFrameSet set);

    [[nodiscard]] const std::vector<CorpusEntry>& entries() const
    {
        return m_entries;
    }
    [[nodiscard]] const CorpusEntry* find(const std::string& id) const;

    [[nodiscard]] const std::vector<CorpusFrameSet>& sets() const
    {
        return m_sets;
    }
    [[nodiscard]] const CorpusFrameSet* findSet(const std::string& id) const;

    /// Where `id` lives under `root`, or why it cannot be used. Does not read the file.
    [[nodiscard]] Resolution resolve(const std::string& id, const std::string& root) const;

    /// Like `resolve`, but reads the file and checks it against the entry's recorded size and
    /// SHA-256.
    ///
    /// A dataset that is present but does not match is reported as unavailable, not as available
    /// with a warning. A re-exported, re-compressed, or replaced file that kept its name is
    /// precisely the substitution the recorded digest exists to catch, and benchmarking it anyway
    /// would attribute numbers to data nobody else has. An entry that records no digest is
    /// accepted on presence alone, and says so in `skipReason` only when it also fails.
    [[nodiscard]] Resolution resolveVerified(const std::string& id, const std::string& root) const;

    /// Empty when every entry and set is well formed and no ID is duplicated.
    ///
    /// Entries and sets share one ID space, because a scenario names one ID and the source that
    /// answers for it has to be unambiguous.
    [[nodiscard]] std::vector<std::string> validate() const;

private:
    std::vector<CorpusEntry> m_entries;
    std::vector<CorpusFrameSet> m_sets;
};

/// The name of the environment variable that locates the real-data corpus.
inline constexpr const char* kCorpusRootVariable = "AFTERNIGHT_BENCH_CORPUS_ROOT";

/// The corpus root a run should use.
///
/// An explicitly given root wins; otherwise the environment variable above is used; otherwise the
/// result is empty and every corpus-backed scenario resolves to skipped. That order exists because
/// the real corpus is host-local: it lives on one maintainer's machine, is never committed, and is
/// never required by continuous integration, so "no corpus" has to be an ordinary outcome rather
/// than a configuration error.
[[nodiscard]] std::string resolveCorpusRoot(const std::string& explicitRoot);

/// One transfer/layout bandwidth scenario, as data the runner reads rather than as code it writes.
///
/// This family is several rows, and each carries an arrangement and a channel count a transcription
/// could get subtly wrong while still producing a plausible number. So the family is a table that
/// the runner and the suite of this layer both read.
enum class TransferOperation : std::uint8_t
{
    Copy, ///< Every sample reproduced somewhere else.
    Fill, ///< One value written over every sample.
    Deinterleave, ///< One interleaved pixel run split into one contiguous run per channel.
    Interleave, ///< One contiguous run per channel joined into one interleaved pixel run.
    ByteSwap, ///< Every sample rewritten in the other byte order, in place.
    ExtractChannel, ///< One channel of a picture copied into a picture of its own.
    ComposeChannels ///< One picture per channel joined into one multi-channel picture.
};

struct TransferBandwidthScenario
{
    /// The row's stable identity.
    const char* id = "";

    /// Which operation the scenario measures. An enumerator rather than a string, so a runner
    /// dispatching on it cannot mistype an identity and silently measure a different operation.
    TransferOperation operation = TransferOperation::Copy;

    /// What the samples are.
    SampleType sampleType = SampleType::Float32;

    /// How many channels one pixel holds.
    std::uint32_t channels = 1u;

    /// How the input's samples are spread.
    Layout inputLayout = Layout::Planar;

    /// How many times each sample is touched: once for an in-place pass, twice for one that reads
    /// a source and writes a destination. It is what turns an element count into a byte count, and
    /// a bandwidth number that counted a copy once would be half of what the memory system saw.
    std::uint32_t sampleTouches = 2u;

    /// How many equal parts of the input one touch actually moves.
    ///
    /// One for an operation that moves the whole picture, which is most of them. The channel count
    /// for one that moves a single channel: an extraction reads the whole of an interleaved image to
    /// reach one channel of it and then writes that one channel, which is four thirds of the image
    /// rather than twice it, and a figure that said twice would overstate the memory system by half.
    std::uint32_t touchDivisor = 1u;

    /// How many bytes this scenario moves, given what its input holds.
    ///
    /// A member rather than an expression the runner writes, because a work count derived in two
    /// places is exactly how two incomparable throughput figures are produced.
    ///
    /// @param inputBytes what the fixture holds.
    /// @return the byte count to record as this scenario's work.
    [[nodiscard]] std::uint64_t bytesMoved(std::uint64_t inputBytes) const noexcept
    {
        return inputBytes * sampleTouches / touchDivisor;
    }

    /// Which channel an extraction moves.
    ///
    /// The middle one, so the offset inside a pixel is neither zero nor the last - the two an
    /// implementation can reach by accident. Derived rather than stated, so every reader names the
    /// same channel without a second field to keep in step.
    ///
    /// @return the channel index.
    [[nodiscard]] std::uint32_t extractedChannel() const noexcept
    {
        return channels / 2u;
    }
};

/// The transfer/layout bandwidth scenarios this repository knows.
///
/// Thread-safe, reentrant, and allocation-free: the table is an immutable constant with static
/// storage duration.
///
/// @return the table, which outlives every caller.
[[nodiscard]] std::vector<TransferBandwidthScenario> transferBandwidthScenarios();

/// The narrowband extraction and colour-composition scenarios.
///
/// These rows are data here because each one carries enough parameters and work accounting that
/// spelling it out where it is run could produce a plausible but wrongly counted row.
/// The synthetic RGB fixture is interpreted as Ha, OIII and SII planes by the composition rows;
/// the extraction row interprets one monochrome fixture as an RGGB mosaic.
///
/// @return the complete narrowband composition scenario family in stable catalogue order.
[[nodiscard]] std::vector<ScenarioSpec> narrowbandCompositionScenarios();

} // namespace an::bench
