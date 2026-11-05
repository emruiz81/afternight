// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine tests - the deterministic randomized differential harness.
//
// Every kernel family in this engine has an authoritative scalar implementation and, on the rungs
// of the instruction-set ladder this build contains, one or more compiled ones. The per-family
// suites compare them on the cases their authors thought of. This compares them on cases nobody
// thought of: shapes, strides, tails, validity rules, alias modes, thread counts and sample values
// drawn from a seeded generator, run through every forced backend, and checked against the
// numeric contract each family states.
//
// Four properties make it a harness rather than a fuzzer.
//
// **It is deterministic.** The generator is written here rather than taken from the standard
// library, whose distributions are implementation-defined, so one seed produces the same cases on
// both nominated hosts and under both compilers. A failure is reported with the seed and the case
// index that produced it, and re-running that pair reproduces it exactly.
//
// **It compares refusals as well as results.** An operation that refuses one input on the scalar
// backend and accepts it on a compiled one is a divergence, and a harness that only compared
// successful runs would be blind to it. Two runs agree when both produce the same result or both
// refuse with the same error code.
//
// **It checks thread independence separately from backend agreement.** Each case runs the
// reference at one thread, the reference at the case thread count, and the candidate backend at
// the case thread count. The two reference runs must agree bit for bit whatever the family, which
// is the engine reproducibility contract; the candidate is then compared against the
// single-threaded reference under the family stated numeric contract. A disagreement therefore
// says which of the two things moved.
//
// **The numeric contract is per subject, not global.** Moving samples is exact. A conversion is
// exact. A fused pointwise chain is exact, because the vector body writes its contractions where
// the scalar body writes them. A convolution accumulates in Float64 and is compared with a
// relative tolerance. A reduction is compared against the forward error bound the operation itself
// reports. A transcendental is compared against the derived bound its own header publishes. A
// single tolerance across all of them would be wrong in both directions at once.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/MathKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace conformance
{

/// The deterministic random source every generated case comes from.
///
/// SplitMix64, written out rather than taken from the standard library: `std::uniform_int_distribution`
/// and its siblings are implementation-defined, so a seed that produced one case here would produce
/// a different one on the other host, and a reported reproduction would not reproduce.
class Random
{
public:
    /// Starts a sequence.
    ///
    /// @param seed the sequence identity. Any value, including zero.
    explicit Random(std::uint64_t seed) noexcept : m_state(seed) {}

    /// The next value of the sequence.
    ///
    /// @return a value with every bit varying.
    [[nodiscard]] std::uint64_t next() noexcept;

    /// A value below a bound, by rejection so that the distribution is even.
    ///
    /// @param bound one past the largest value. Zero answers zero.
    /// @return the value.
    [[nodiscard]] std::uint32_t below(std::uint32_t bound) noexcept;

    /// One of a list.
    ///
    /// @tparam ValueT what the list holds.
    /// @param values the list. Must not be empty.
    /// @return one of them.
    template <typename ValueT> [[nodiscard]] ValueT pick(std::span<const ValueT> values) noexcept
    {
        return values[below(static_cast<std::uint32_t>(values.size()))];
    }

    /// Whether a one-in-`chance` event happens.
    ///
    /// @param chance the denominator. Zero and one always happen.
    /// @return true when it does.
    [[nodiscard]] bool oneIn(std::uint32_t chance) noexcept;

    /// A value in the closed unit interval.
    ///
    /// @return the value.
    [[nodiscard]] double unit() noexcept;

private:
    /// The sequence state.
    std::uint64_t m_state;
};

/// One forced implementation a case is run on.
struct Target
{
    /// Which of the engine implementations.
    an::ComputeBackend backend = an::ComputeBackend::Reference;

    /// Which instruction set inside it.
    an::InstructionSet instructionSet = an::InstructionSet::Scalar;

    /// The name a failure message carries.
    std::string name;
};

/// The reference implementation, which every candidate is compared against.
///
/// @return the scalar backend.
[[nodiscard]] Target referenceTarget();

/// Every compiled implementation this build contains and this host can run.
///
/// @return the candidates, in ladder order. Empty on a build or host with no compiled kernels,
///         which is a legitimate answer rather than a failure.
[[nodiscard]] std::vector<Target> candidateTargets();

/// Which samples one generated case treats as meaningful.
enum class ValidityChoice : std::uint8_t
{
    /// Every sample.
    All,

    /// Only the finite ones.
    FiniteOnly,

    /// Everything but an exact zero.
    ZeroIsNoData,

    /// A packed mask generated beside the image.
    Mask,

    /// A stated closed range.
    Range
};

/// One generated case: a shape, a memory arrangement, a rule, and a thread count.
struct Case
{
    /// Which case of the sweep this is, for a failure message.
    std::uint32_t index = 0u;

    /// The seed the whole sweep was generated from.
    std::uint64_t seed = 0u;

    /// Pixels per row.
    std::uint32_t width = 1u;

    /// Rows.
    std::uint32_t height = 1u;

    /// Samples of padding after each source row.
    std::uint32_t sourcePadding = 0u;

    /// Samples of padding after each destination row, which need not match the source.
    std::uint32_t destinationPadding = 0u;

    /// Threads the candidate run may use.
    std::uint32_t threads = 1u;

    /// Which samples participate.
    ValidityChoice validity = ValidityChoice::All;

    /// Whether the destination is exactly the source, where the subject permits it.
    bool inPlace = false;

    /// The representation the subject reads, where the subject has a choice.
    an::SampleType sampleType = an::SampleType::Float32;

    /// The representation the subject writes, where the subject converts.
    an::SampleType destinationSampleType = an::SampleType::Float32;

    /// Channels, for the subjects that have more than one.
    std::uint32_t channels = 1u;

    /// A per-case value the subject interprets: an exponent, a tap count, a fill pattern.
    std::uint64_t variant = 0u;
};

/// What one run of one subject on one target produced.
struct Outcome
{
    /// Whether the operation succeeded.
    bool ran = false;

    /// The refusal, when it did not.
    an::ErrorCode code = an::ErrorCode::Ok;

    /// What the dispatch reported the running code was compiled for.
    an::InstructionSet dispatched = an::InstructionSet::Automatic;

    /// Everything the operation wrote, padding included, so that untouched padding is compared too.
    std::vector<std::byte> bytes;

    /// Scalar results, for a subject that produces numbers rather than an image.
    std::vector<double> values;

    /// The absolute tolerance each scalar result carries, from the operation own error bound.
    std::vector<double> valueTolerances;

    /// The samples a transcendental run read.
    ///
    /// Its accuracy bound is derived from the argument rather than from the result, so the argument
    /// has to survive the run for the comparison to be able to ask for the bound.
    std::vector<float> sourceSamples;

    /// Which function produced `bytes`, for the same reason.
    an::MathFunction function = an::MathFunction::Unknown;

    /// The exponent it was given, for the same reason.
    double exponent = 1.0;

    /// Anything the subject could not even attempt, which is a harness failure rather than a
    /// divergence.
    std::string setupFailure;
};

/// How the bytes of two runs are compared.
enum class ByteComparison : std::uint8_t
{
    /// Every byte must agree. What moving, converting and repacking samples promise.
    Exact,

    /// The bytes are Float32 samples compared with a relative tolerance.
    Float32Relative,

    /// The bytes are Float32 results of a transcendental, compared against the bound its header
    /// derives for the sample that produced them.
    Float32Transcendental,

    /// The subject writes no image.
    None
};

/// One operation the harness drives.
struct Subject
{
    /// The name a failure message carries.
    const char* name = "";

    /// Runs one case on one target.
    Outcome (*run)(const an::EngineContext& engine, const Target& target, const Case& generated) = nullptr;

    /// Generates the parts of a case this subject cares about, on top of the shared shape.
    void (*specialize)(Random& random, Case& generated) = nullptr;

    /// How the written bytes are compared.
    ByteComparison bytes = ByteComparison::Exact;

    /// The relative tolerance under `ByteComparison::Float32Relative`.
    double relativeTolerance = 0.0;

    /// Whether a case may ask this subject to write over its own input.
    bool allowsInPlace = false;
};

/// Every subject the harness drives.
///
/// @return the subjects, in a stable order.
[[nodiscard]] std::span<const Subject> subjects();

/// Generates one case for one subject.
///
/// @param subject the subject, which decides the parts of the case it uses.
/// @param seed the sweep seed.
/// @param index which case of the sweep.
/// @return the case, which is a pure function of the three arguments.
[[nodiscard]] Case generateCase(const Subject& subject, std::uint64_t seed, std::uint32_t index);

/// Whether two runs of one subject agree.
///
/// @param subject the subject, whose numeric contract decides what agreement means.
/// @param generated the case, for the transcendental bound and for the failure message.
/// @param reference the authoritative run.
/// @param candidate the run being checked.
/// @return an empty string when they agree, and what differs when they do not.
[[nodiscard]] std::string compareOutcomes(const Subject& subject, const Case& generated, const Outcome& reference,
                                          const Outcome& candidate);

/// A one-line description of a case, for a failure message that has to be reproducible.
///
/// @param subject the subject.
/// @param generated the case.
/// @return the description.
[[nodiscard]] std::string describeCase(const Subject& subject, const Case& generated);

} // namespace conformance
