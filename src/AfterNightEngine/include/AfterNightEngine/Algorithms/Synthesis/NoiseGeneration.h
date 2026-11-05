// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Noise put into an image on purpose.
///
/// Every other operation in this area takes noise out. This one puts it in, which is what a person
/// testing a denoiser, a stacking rejection rule or a detection threshold needs: a frame whose noise
/// they chose rather than one whose noise they are trying to explain. It needs no interpreter and no
/// imaging library, so a synthetic frame does not depend on what is installed beside the engine.
///
/// **The stream is a function of where a sample is, not of when it was drawn.** Two runs of the same
/// request over the same frame produce the same frame, whatever the thread count and whatever order
/// the scheduler chose, because every sample's draw is keyed on its coordinate and the request's seed
/// rather than taken from a generator that advances. This is the same rule the pixel expression
/// evaluator's `random()` follows and the same mixing function, which is why a frame made here and a
/// frame made by an expression cannot silently disagree about what a seed means.
///
/// **A value is written in the image's own units.** A mean, a variance and the two extremes a salt
/// and a pepper sample take are numbers in the representation the caller handed over, so the same
/// request over the same picture means the same thing whether it is stored as Float32 or UInt16. The
/// clip, when it is asked for, holds the result between those same two extremes.

#pragma once

#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// Which distribution the added noise is drawn from.
///
/// There is no local-variance kind: without a variance map it would draw one constant variance,
/// which is `Gaussian` with that `variance`.
enum class NoiseKind : std::uint8_t
{
    /// Additive normal noise of a stated mean and variance.
    Gaussian = 0u,

    /// Shot noise: each sample is redrawn from a Poisson distribution about its own value, scaled
    /// by `NoiseGenerationRequest::poissonScale`. A sample at or below zero has no count and becomes
    /// zero.
    Poisson = 1u,

    /// Each sample replaced by `NoiseGenerationRequest::highValue` with probability `amount`.
    Salt = 2u,

    /// Each sample replaced by `NoiseGenerationRequest::lowValue` with probability `amount`.
    Pepper = 3u,

    /// Each sample replaced with probability `amount`, by `highValue` with probability
    /// `saltFraction` and otherwise by `lowValue`.
    SaltAndPepper = 4u,

    /// Multiplicative normal noise: the sample plus the sample times the draw.
    Speckle = 5u
};

/// Whether a value names a noise kind this build supports.
///
/// @param kind the value to inspect.
/// @return true for every supported kind.
[[nodiscard]] constexpr bool isNoiseKind(NoiseKind kind) noexcept
{
    return static_cast<std::uint8_t>(kind) <= static_cast<std::uint8_t>(NoiseKind::Speckle);
}

/// The name of a noise kind, for a diagnostic or a report.
///
/// @param kind which kind.
/// @return a static, never-null name; "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* noiseKindName(NoiseKind kind) noexcept;

/// The largest rate the Poisson draw is defined for.
///
/// Above this the distribution is indistinguishable from a normal one at any precision this engine
/// stores, and the rejection method's own tables stop being worth their cost. `planNoiseGeneration`
/// refuses a Poisson request whose `highValue` times `poissonScale` exceeds it. The Poisson draw
/// itself makes a bounded number of uniform draws whatever its rate, so an unclipped source sample
/// above `highValue` cannot make it run for an unbounded time either.
inline constexpr double kMaxPoissonRate = 1.0e9;

/// What a caller asks a noise generator for.
struct NoiseGenerationRequest
{
    /// Which distribution the noise is drawn from.
    NoiseKind kind = NoiseKind::Gaussian;

    /// The mean of the normal draw, in the image's own units. Used by `Gaussian` and `Speckle`.
    ///
    /// Must be finite.
    double mean = 0.0;

    /// The variance of the normal draw, in the square of the image's own units.
    ///
    /// Used by `Gaussian` and `Speckle`. Must be finite and not negative; zero adds the mean and
    /// nothing else, which is a shift rather than noise and is allowed because it is a useful
    /// control case.
    double variance = 0.01;

    /// What fraction of the samples the three replacement kinds replace.
    ///
    /// Must be finite and inside [0, 1].
    double amount = 0.05;

    /// What fraction of the replaced samples `SaltAndPepper` sets to `highValue`.
    ///
    /// Must be finite and inside [0, 1]. Ignored by every other kind, which is why `Salt` and
    /// `Pepper` are separate kinds rather than this number at its two ends: a caller asking for salt
    /// should not have to know that.
    double saltFraction = 0.5;

    /// The value a peppered sample takes, and the lower end of the clip.
    ///
    /// Must be finite and below `highValue`.
    double lowValue = 0.0;

    /// The value a salted sample takes, and the upper end of the clip.
    ///
    /// Must be finite and above `lowValue`.
    double highValue = 1.0;

    /// How many times the Poisson rate scales the sample values.
    ///
    /// Shot noise is a property of a count, and a normalized sample is not one, so the sample is
    /// multiplied by this to get the rate, drawn, and divided back. Larger means less relative
    /// noise. Must be finite and above zero, and `highValue` times this must not exceed
    /// `kMaxPoissonRate`. Ignored by every kind but `Poisson`.
    double poissonScale = 1024.0;

    /// Whether the result is held between `lowValue` and `highValue`.
    ///
    /// True by default. A clipped sample is counted and reported, because a request whose variance
    /// put a tenth of the frame against an extreme is one a person wants to know about.
    bool clip = true;

    /// The seed the stream is derived from.
    ///
    /// Two runs with the same seed over the same geometry draw the same noise. Two different seeds
    /// draw unrelated noise.
    std::uint64_t seed = 0u;

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// A validated noise request and what running it will cost.
///
/// Trivially copyable and allocation-free.
class NoiseGenerationPlan
{
public:
    /// A plan describing no generator, which no execution accepts.
    NoiseGenerationPlan() noexcept = default;

    /// Records a request and derives what it implies.
    ///
    /// Public for the reason `BilateralPlan`'s constructor is: the free builder below is where the
    /// rules live, and this only derives what a checked request implies.
    ///
    /// @param request the kind, its parameters, the seed and the output policy.
    AFTERNIGHTENGINE_API explicit NoiseGenerationPlan(const NoiseGenerationRequest& request) noexcept;

    /// What was asked for.
    ///
    /// @return the request this plan was derived from.
    [[nodiscard]] AFTERNIGHTENGINE_API const NoiseGenerationRequest& request() const noexcept;

    /// How many draws one sample takes, for the kinds whose count is fixed.
    ///
    /// Two for the normal kinds, which pair their draws; two for `SaltAndPepper`, which decides
    /// whether a sample is replaced and then which extreme it takes; one for `Salt` and `Pepper`.
    /// `Poisson` reports one, although its draw reads as many further indices of the sample's stream
    /// as its method needs - at most 256 by multiplication and 512 by rejection. Every draw is keyed
    /// on its index rather than taken from a counter, which is what makes the stream reproducible.
    ///
    /// @return the draw count, which is zero for a plan describing no generator.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t drawsPerSample() const noexcept;

    /// How many times the generator walks the whole image.
    ///
    /// @return one: every kind here decides a sample from that sample and its own draws.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t imagePasses() const noexcept;

private:
    /// The request this plan records.
    NoiseGenerationRequest m_request{};

    /// The draws one sample takes.
    std::uint32_t m_drawsPerSample = 0u;
};

/// What a noise generation did.
struct NoiseGenerationResult
{
    /// Selection, writes, working memory, layout path and planned workers.
    SpatialFilterResult filter{};

    /// How many samples the generator actually moved.
    ///
    /// A sample written back unchanged is not counted, which is the difference between an amount
    /// that did nothing and one that rewrote a tenth of the frame.
    std::uint64_t samplesChanged = 0u;

    /// How many written samples were held at one of the two extremes by the clip.
    ///
    /// Zero when `NoiseGenerationRequest::clip` is false, because nothing was held.
    std::uint64_t samplesClipped = 0u;
};

/// Validates a request and derives what running it costs.
///
/// Thread-safe and allocation-free. It reads no image and takes no execution.
///
/// @param request the kind, its parameters, the seed and the output policy.
/// @return the plan; `ErrorCode::InvalidArgument` for an unknown kind, a non-finite parameter, a
///         negative variance, an amount or salt fraction outside [0, 1], a low value at or above the
///         high value, a Poisson scale at or below zero, a conversion field outside its enumeration,
///         or a write-back policy naming `DomainMapping::FullScale`; or `ErrorCode::OutOfRange` when a
///         `NoiseKind::Poisson` request's largest rate, `highValue` times `poissonScale`, is above
///         `kMaxPoissonRate`, carrying the rate and the limit.
[[nodiscard]] AFTERNIGHTENGINE_API Result<NoiseGenerationPlan>
planNoiseGeneration(const NoiseGenerationRequest& request);

/// Adds the requested noise to every channel of an image.
///
/// The untyped entry point, and the one the exported library carries. Each sample's answer is
/// decided from that sample and the draws its own coordinate keys, so the operation is one pass, runs
/// on the threads it was admitted, and produces the same frame at any thread count.
///
/// The arrangement, working planes, validity and write-back rules are `filterImageBilateral`'s. **A
/// sample the validity rule rejects keeps the value it had**: noise put into a sample that means
/// nothing is a number a later operation would read as if it meant something.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only
/// read. Runs on the threads the operation was admitted, checks cancellation at row boundaries, and
/// is not failure-atomic. It reports no progress, because it is one pass over its input.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source image's samples start. Must not be null.
/// @param sourceLayout where they sit.
/// @param destination where the noisy samples go. Must not be null, and must not overlap the source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param plan the validated kind and its parameters.
/// @param selection which samples mean something.
/// @return the counts and how many stored samples changed and were clipped; the failures
///         `filterImageBilateral` documents, plus whatever `planNoiseGeneration` refuses in the
///         plan's request and `ErrorCode::InvalidArgument` for a plan describing no generator.
[[nodiscard]] AFTERNIGHTENGINE_API Result<NoiseGenerationResult>
generateImageNoise(const ExecutionContext& execution, SampleType sampleType, const void* source,
                   const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                   const NoiseGenerationPlan& plan, const SpatialSelection& selection = {});

/// Adds the requested noise to a checked typed image view.
///
/// The typed spelling of `generateImageNoise`, which owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param plan the validated kind and its parameters.
/// @param selection which samples mean something.
/// @return whatever `generateImageNoise` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<NoiseGenerationResult>
generateNoise(const ExecutionContext& execution, const ImageView<const SampleT>& source,
              const ImageView<SampleT>& destination, const NoiseGenerationPlan& plan,
              const SpatialSelection& selection = {})
{
    return generateImageNoise(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), destination.data(),
                              destination.layout(), plan, selection);
}

} // namespace an
