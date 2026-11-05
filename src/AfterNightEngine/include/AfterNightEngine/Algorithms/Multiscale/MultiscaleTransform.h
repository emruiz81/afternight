// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Decomposing an image into scales, changing the scales, and putting it back.
///
/// A multiscale transform is not a filter. A filter reads an image and writes an image; this reads an
/// image and produces a *set* of images, one per scale, whose sum is the original. That difference is
/// the reason this domain exists beside `Algorithms/Spatial` rather than inside it: the layers are
/// owned memory with their own lifetime, the interesting operations happen between the decomposition
/// and the reconstruction, and an operation that only ever went image-to-image could not express any
/// of them.
///
/// **The sum of the layers is the image.** Every transform here is a partition of the samples into
/// details plus one residual, arranged so that adding them back with unit weights reproduces what was
/// decomposed, to Float32 rounding and nothing worse. That is the property the whole surface rests on
/// - a layer weight means something only because the weights sum to the identity - and it is what the
/// reconstruction test asserts rather than assumes.
///
/// **What separates the scales is a stated kernel, applied at a stated spacing.** The starlet
/// transform smooths with the same small kernel again and again, spreading its taps twice as far each
/// time and subtracting successive smoothings; the multiscale-linear transform smooths the *original*
/// with a widening Gaussian and subtracts successive smoothings of it. Both are separable
/// convolutions and both go through `convolveSeparablePlane`, so there is one border rule, one
/// arithmetic contract and one tiling strategy in the engine rather than a second copy living under a
/// transform.
///
/// **A sample the validity rule rejects is substituted once, at the door, and the count is
/// reported.** A transform is a linear operator over a plane; there is no meaning to "leave this
/// coefficient untouched" in a layer that did not exist before the call. So a rejected sample is
/// replaced by the border policy's substitute as the plane is gathered, the decomposition says how
/// many samples that happened to, and everything downstream is arithmetic over a plane with no
/// meaningless samples in it. A reconstruction therefore writes every sample of its destination, and
/// the image's own validity rule still describes which of them mean something.
///
/// **The working numbers are the stored numbers**, as everywhere else in the neighbourhood families:
/// the layers hold Float32 coefficients in the image's own units, a threshold is written in those
/// units, and the write back converts under `DomainMapping::CodeValue`.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace an
{

class EngineContext;
class ExecutionContext;

/// Which smoothing kernel separates one scale from the next.
///
/// Each of them is a different answer to the same question, and a caller who chooses one chooses it
/// for a reason. Three families are present: the two interpolating kernels a starlet transform is
/// normally built from, a ladder of three-tap kernels whose centre weight decides how little of the
/// neighbourhood survives one smoothing, and four sampled Gaussians of fixed width.
///
/// Every kernel here sums to one, so a smoothing preserves a flat surface exactly and a detail layer
/// over a flat surface is zero.
enum class MultiscaleScalingFunction : std::uint8_t
{
    /// `[1, 2, 1] / 4`: linear interpolation, the narrowest kernel that is not the identity.
    LinearInterpolation3 = 0u,

    /// `[1, 4, 6, 4, 1] / 16`: the cubic B-spline, and what a starlet transform normally uses.
    BSpline5 = 1u,

    /// `[1, 3, 1] / 5`.
    SmallScale3 = 2u,

    /// `[1, 4, 1] / 6`.
    SmallScale4 = 3u,

    /// `[1, 5, 1] / 7`.
    SmallScale5 = 4u,

    /// `[1, 6, 1] / 8`.
    SmallScale6 = 5u,

    /// `[1, 8, 1] / 10`.
    SmallScale8 = 6u,

    /// `[1, 12, 1] / 14`.
    SmallScale12 = 7u,

    /// `[1, 16, 1] / 18`.
    SmallScale16 = 8u,

    /// `[1, 24, 1] / 26`.
    SmallScale24 = 9u,

    /// `[1, 32, 1] / 34`.
    SmallScale32 = 10u,

    /// A five-tap Gaussian, normalized after truncation.
    Gaussian5 = 11u,

    /// A seven-tap Gaussian, normalized after truncation.
    Gaussian7 = 12u,

    /// A nine-tap Gaussian, normalized after truncation.
    Gaussian9 = 13u,

    /// An eleven-tap Gaussian, normalized after truncation.
    Gaussian11 = 14u
};

/// Whether a value names one of the scaling functions this build implements.
///
/// @param scaling the value to inspect.
/// @return true for every supported scaling function.
[[nodiscard]] constexpr bool isMultiscaleScalingFunction(MultiscaleScalingFunction scaling) noexcept
{
    return static_cast<std::uint8_t>(scaling) <= static_cast<std::uint8_t>(MultiscaleScalingFunction::Gaussian11);
}

/// The stable spelling of a scaling function, for a report or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param scaling the function to name.
/// @return a static string, or `"Unknown"` for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
multiscaleScalingFunctionName(MultiscaleScalingFunction scaling) noexcept;

/// The most taps any scaling function in this enumeration has.
inline constexpr std::uint32_t kMaxMultiscaleScalingTaps = 11u;

/// How many taps one scaling function has.
///
/// Thread-safe, allocation-free, and usable in a constant expression, so a caller can size a buffer
/// for the taps without calling the writer first.
///
/// @param scaling the function to measure.
/// @return the odd tap count, or zero for a value outside the enumeration.
[[nodiscard]] constexpr std::uint32_t multiscaleScalingTapCount(MultiscaleScalingFunction scaling) noexcept
{
    switch (scaling)
    {
    case MultiscaleScalingFunction::LinearInterpolation3:
    case MultiscaleScalingFunction::SmallScale3:
    case MultiscaleScalingFunction::SmallScale4:
    case MultiscaleScalingFunction::SmallScale5:
    case MultiscaleScalingFunction::SmallScale6:
    case MultiscaleScalingFunction::SmallScale8:
    case MultiscaleScalingFunction::SmallScale12:
    case MultiscaleScalingFunction::SmallScale16:
    case MultiscaleScalingFunction::SmallScale24:
    case MultiscaleScalingFunction::SmallScale32:
        return 3u;
    case MultiscaleScalingFunction::BSpline5:
    case MultiscaleScalingFunction::Gaussian5:
        return 5u;
    case MultiscaleScalingFunction::Gaussian7:
        return 7u;
    case MultiscaleScalingFunction::Gaussian9:
        return 9u;
    case MultiscaleScalingFunction::Gaussian11:
        return 11u;
    }
    return 0u;
}

/// Writes the coefficients of one scaling function.
///
/// The taps are produced here rather than kept in the plan for the reason `writeGaussianTaps` gives:
/// a caller that wants to see them asks, and an execution builds them on the stack once per call.
/// Every function normalizes to a sum of exactly one in Float64, so a smoothing leaves a flat
/// surface flat.
///
/// Thread-safe and allocation-free.
///
/// @param scaling which function to write.
/// @param taps where the coefficients go, at least `multiscaleScalingTapCount(scaling)` long. Only
///        that prefix is written.
/// @return how many taps were written; `ErrorCode::InvalidArgument` for a function outside the
///         enumeration, or `ErrorCode::OutOfRange` when the span is shorter than the tap count.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint32_t> writeMultiscaleScalingTaps(MultiscaleScalingFunction scaling,
                                                                                    std::span<double> taps);

/// How the scale of one layer follows from the scale of the one before it.
enum class MultiscaleLayerSpacing : std::uint8_t
{
    /// Each layer covers twice the distance of the previous one: `1, 2, 4, 8, ...`.
    ///
    /// The classical arrangement, and the one whose layers are close to independent.
    Dyadic = 0u,

    /// Each layer adds a fixed step to the previous one: `1, 1 + s, 1 + 2s, ...`.
    ///
    /// Finer control over which structure sizes end up in which layer, at the cost of layers whose
    /// contents overlap more.
    Linear = 1u
};

/// Whether a value names one of the layer spacings this build implements.
///
/// @param spacing the value to inspect.
/// @return true for both spacings.
[[nodiscard]] constexpr bool isMultiscaleLayerSpacing(MultiscaleLayerSpacing spacing) noexcept
{
    return spacing == MultiscaleLayerSpacing::Dyadic || spacing == MultiscaleLayerSpacing::Linear;
}

/// Which transform separates the scales.
enum class MultiscaleAlgorithm : std::uint8_t
{
    /// The starlet, or a-trous, wavelet transform.
    ///
    /// Layer `j` is the difference between the image smoothed `j` times and smoothed `j + 1` times,
    /// where each smoothing applies the same scaling kernel with its taps spread `scale` samples
    /// apart. Because every smoothing is applied to the previous smoothing, the cost of a layer does
    /// not grow with its scale: eleven layers cost eleven passes of a five-tap kernel, not one pass
    /// of a two-thousand-tap kernel.
    Starlet = 0u,

    /// The multiscale linear transform.
    ///
    /// Layer `j` is the difference between the *original* image blurred to one scale and blurred to
    /// the next, with a true Gaussian at each scale rather than a repeated small kernel. Its layers
    /// are thinner and more selective than the starlet's, and it costs more: each layer is a
    /// Gaussian whose radius grows with the scale.
    MultiscaleLinear = 1u
};

/// Whether a value names one of the transforms this build implements.
///
/// @param algorithm the value to inspect.
/// @return true for both transforms.
[[nodiscard]] constexpr bool isMultiscaleAlgorithm(MultiscaleAlgorithm algorithm) noexcept
{
    return algorithm == MultiscaleAlgorithm::Starlet || algorithm == MultiscaleAlgorithm::MultiscaleLinear;
}

/// The stable spelling of a transform, for a report or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param algorithm the transform to name.
/// @return a static string, or `"Unknown"` for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* multiscaleAlgorithmName(MultiscaleAlgorithm algorithm) noexcept;

/// The most layers, details and residual together, one decomposition may hold.
///
/// A structural ceiling rather than an achievable one: it bounds how much memory a decomposition can
/// ask for per channel before anything is allocated, and every transform additionally has a depth at
/// which its own kernel stops fitting the stencil contract. `planMultiscaleTransform` reports that
/// second limit by name, carrying the number that exceeded it, rather than quietly producing a
/// narrower filter than was asked for.
inline constexpr std::uint32_t kMaxMultiscaleLayers = 12u;

/// The fewest layers a decomposition may hold: one detail and one residual.
inline constexpr std::uint32_t kMinMultiscaleLayers = 2u;

/// What a multiscale-linear layer scale means in standard deviations.
///
/// A scale is stated in pixels - it is the distance the starlet spreads its taps over - and the
/// Gaussian that separates the same structures has a standard deviation of about a third of it, so
/// that three standard deviations either side of a centre covers the scale. The choice is written
/// down here rather than inside an implementation, because a caller comparing the two transforms at
/// the same scale needs to know what "the same scale" means.
inline constexpr double kMultiscaleLinearScaleToSigma = 1.0 / 3.0;

/// What a caller asks a multiscale transform for.
struct MultiscaleTransformRequest
{
    /// Which transform separates the scales.
    MultiscaleAlgorithm algorithm = MultiscaleAlgorithm::Starlet;

    /// Which smoothing kernel the starlet applies. Read by `MultiscaleAlgorithm::Starlet` alone; the
    /// multiscale-linear transform builds a Gaussian from the scale instead.
    MultiscaleScalingFunction scaling = MultiscaleScalingFunction::BSpline5;

    /// How the scale of one layer follows from the previous one.
    MultiscaleLayerSpacing spacing = MultiscaleLayerSpacing::Dyadic;

    /// The step added per layer under `MultiscaleLayerSpacing::Linear`. Must be at least one, and is
    /// ignored under `MultiscaleLayerSpacing::Dyadic`.
    std::uint32_t linearScale = 1u;

    /// How many layers the decomposition holds, the residual included.
    ///
    /// A decomposition of `n` layers has `n - 1` detail layers and one residual, and the sum of all
    /// `n` is the image. It must be within `[kMinMultiscaleLayers, kMaxMultiscaleLayers]`.
    std::uint32_t layerCount = 4u;

    /// How a neighbour outside the image and a neighbour the validity rule rejected are supplied.
    ///
    /// The default is `KernelBorderMode::ReflectExcludingEdge` rather than the kernel family's
    /// `Clamp`, for the reason the Gaussian gives: clamping biases a smoothing near an edge towards
    /// the edge sample, which puts an edge-shaped artefact into every detail layer.
    StencilBorderPolicy borders{.border = KernelBorderMode::ReflectExcludingEdge};

    /// What the result promises, and how a computed value becomes a stored sample.
    ///
    /// Read by the reconstruction, which is the only half of the round trip that writes an image.
    SpatialOutputPolicy output{};
};

/// A validated multiscale transform: what was asked for, and what will actually run.
///
/// Trivially copyable and allocation-free. Everything per-layer is derived from the layer index
/// rather than stored, so a plan is a few words and a caller may keep one beside a decomposition
/// without thinking about it.
class MultiscaleTransformPlan
{
public:
    /// A plan describing no transform, which no execution accepts.
    MultiscaleTransformPlan() noexcept = default;

    /// Records a request and derives everything that follows from it.
    ///
    /// Public for the reason `GaussianPlan`'s constructor is: the free builder below is where the
    /// rules live, and this only derives what a checked request implies. A request that has not been
    /// through `planMultiscaleTransform` may produce a plan whose numbers are meaningless, and every
    /// execution checks them again before it reads a sample.
    ///
    /// @param request the transform, kernel, spacing, layer count, border and output policy.
    AFTERNIGHTENGINE_API explicit MultiscaleTransformPlan(const MultiscaleTransformRequest& request) noexcept;

    /// What was asked for.
    ///
    /// @return the request this plan was derived from.
    [[nodiscard]] AFTERNIGHTENGINE_API const MultiscaleTransformRequest& request() const noexcept;

    /// How many layers the decomposition holds, the residual included.
    ///
    /// @return the layer count, which is zero for a default-constructed plan.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t layerCount() const noexcept;

    /// How many of those layers are details.
    ///
    /// @return `layerCount() - 1`, and zero for a default-constructed plan.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t detailLayerCount() const noexcept;

    /// Which layer index holds the residual.
    ///
    /// @return `layerCount() - 1`, and zero for a default-constructed plan.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t residualLayer() const noexcept;

    /// The scale, in pixels, one detail layer separates.
    ///
    /// `1, 2, 4, 8, ...` under `MultiscaleLayerSpacing::Dyadic`, and `1, 1 + s, 1 + 2s, ...` under
    /// `MultiscaleLayerSpacing::Linear`.
    ///
    /// @param detailLayer the layer, below `detailLayerCount()`.
    /// @return the scale, or zero when the index names no detail layer.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t layerScale(std::uint32_t detailLayer) const noexcept;

    /// The tap spacing the starlet smoothing that produces one detail layer runs at.
    ///
    /// @param detailLayer the layer, below `detailLayerCount()`.
    /// @return the spacing under `MultiscaleAlgorithm::Starlet`, and one under
    ///         `MultiscaleAlgorithm::MultiscaleLinear`, which spreads no taps. Zero when the index
    ///         names no detail layer.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t layerDilation(std::uint32_t detailLayer) const noexcept;

    /// The standard deviation of the Gaussian that produces one multiscale-linear detail layer.
    ///
    /// @param detailLayer the layer, below `detailLayerCount()`.
    /// @return the sigma under `MultiscaleAlgorithm::MultiscaleLinear`, and zero under
    ///         `MultiscaleAlgorithm::Starlet`, which builds no Gaussian. Zero also when the index
    ///         names no detail layer.
    [[nodiscard]] AFTERNIGHTENGINE_API double layerSigma(std::uint32_t detailLayer) const noexcept;

    /// How many taps the smoothing that produces one detail layer applies along each axis.
    ///
    /// @param detailLayer the layer, below `detailLayerCount()`.
    /// @return the odd tap count, or zero when the index names no detail layer.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t layerTapCount(std::uint32_t detailLayer) const noexcept;

    /// Half the footprint, in pixels, the smoothing that produces one detail layer reads.
    ///
    /// It is the tap radius times the tap spacing, so a starlet layer whose kernel never grows still
    /// reaches further with every scale.
    ///
    /// @param detailLayer the layer, below `detailLayerCount()`.
    /// @return the radius in pixels, or zero when the index names no detail layer.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t layerRadius(std::uint32_t detailLayer) const noexcept;

    /// How many times a decomposition walks the whole image.
    ///
    /// Two passes for each detail layer, because each smoothing is separable, plus one subtraction
    /// pass per detail layer and one copy for the residual.
    ///
    /// @return the pass count.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t imagePasses() const noexcept;

    /// How many bytes of coefficients a decomposition of this picture holds.
    ///
    /// The number a caller checks against a budget before asking for a decomposition: a transform
    /// holds every layer of every channel at once, which is what makes it expensive and what makes
    /// it useful.
    ///
    /// It counts the coefficients and not the padding: the layers are allocated with aligned rows,
    /// because a row a vector kernel writes wants to start on a boundary, so
    /// `MultiscaleDecomposition::layerBytes()` reports at least this and usually a little more. The
    /// difference is a property of the allocation rather than of the transform, which is why a plan
    /// - which has never seen an allocator - does not claim to know it.
    ///
    /// @param width how many pixels one row holds.
    /// @param height how many rows.
    /// @param channelCount how many channels.
    /// @return the byte count, which is `width * height * channelCount * layerCount * 4`.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t layerBytes(std::uint32_t width, std::uint32_t height,
                                                                std::uint32_t channelCount) const noexcept;

private:
    /// The request this plan records.
    MultiscaleTransformRequest m_request{};

    /// How many layers the decomposition holds, or zero for a plan describing no transform.
    std::uint32_t m_layerCount = 0u;
};

/// Validates a request and derives the plan that would run it.
///
/// Thread-safe and allocation-free. It reads no image and takes no execution, because deciding a
/// transform is a question about numbers.
///
/// @param request the transform, kernel, spacing, layer count, border and output policy.
/// @return the plan; `ErrorCode::InvalidArgument` for a transform, kernel, spacing, border mode,
///         output contract or conversion field outside its enumeration, the border mode
///         `KernelBorderMode::NotApplicable`, a non-finite border or invalid-neighbour value, a zero
///         linear step, or a write-back policy naming `DomainMapping::FullScale`;
///         `ErrorCode::OutOfRange` when the layer count is outside
///         `[kMinMultiscaleLayers, kMaxMultiscaleLayers]`, when the deepest layer's scale does not
///         fit in 32 bits, when the deepest starlet layer would need a tap spacing above
///         `kMaxStencilDilation`, or when the deepest multiscale-linear layer would need a Gaussian
///         radius above `kMaxStencilRadius` - the last three carrying the number that exceeded the
///         limit and the limit.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MultiscaleTransformPlan>
planMultiscaleTransform(const MultiscaleTransformRequest& request);

/// What one decomposition did.
struct MultiscaleTransformResult
{
    /// Source pixel selection, summed over the decomposed channels. `outputsWritten` is every
    /// coefficient the transform produced, which is the sample count times the layer count.
    KernelRunResult run{};

    /// How many channels were decomposed.
    std::uint32_t channelCount = 0u;

    /// How many layers each channel holds, the residual included.
    std::uint32_t layerCount = 0u;

    /// How many source samples the validity rule rejected and the border substitute replaced.
    ///
    /// The number that says whether the layers describe the picture or a repaired version of it. It
    /// is zero for the ordinary case of an image every sample of which means something.
    std::uint64_t substitutedSamples = 0u;

    /// How many times the transform walked the whole image.
    std::uint64_t imagePasses = 0u;

    /// Context memory the decomposition holds, in bytes, for as long as it lives.
    std::uint64_t layerBytes = 0u;

    /// Context memory held during the call and released before it returned, in bytes.
    std::uint64_t workingBytes = 0u;

    /// Workers the operation could actually use.
    std::uint32_t plannedWorkers = 0u;

    /// The width the arithmetic ran at, which this domain always reports as
    /// `ComputePrecision::Float32`.
    ComputePrecision precision = ComputePrecision::Float32;
};

/// The layers of one decomposed image, and the memory holding them.
///
/// Owned rather than borrowed, and move-only for the reason `ImageStorage` is: a decomposition is
/// the largest thing this engine hands back, and sharing one is a decision a call site should have
/// to write. The memory comes from the engine context that ran the decomposition, so it counts
/// against that context's budget and that context cannot shut down while the decomposition lives.
///
/// The layers are held as one allocation of `channelCount * layerCount` planes; `layer` addresses
/// them by channel and layer rather than by plane index, so a caller never computes that product.
class MultiscaleDecomposition
{
public:
    /// A decomposition holding nothing, which is what a move leaves behind.
    MultiscaleDecomposition() noexcept = default;

    /// Takes over another decomposition's layers, leaving it holding nothing.
    ///
    /// @param other the decomposition to take over.
    MultiscaleDecomposition(MultiscaleDecomposition&& other) noexcept = default;

    /// Releases these layers and takes over another decomposition's.
    ///
    /// @param other the decomposition to take over.
    /// @return this decomposition.
    MultiscaleDecomposition& operator=(MultiscaleDecomposition&& other) noexcept = default;

    MultiscaleDecomposition(const MultiscaleDecomposition&) = delete;
    MultiscaleDecomposition& operator=(const MultiscaleDecomposition&) = delete;

    /// Gives the layer memory back to the context it came from.
    ~MultiscaleDecomposition() = default;

    /// Builds a decomposition around layer memory that has already been taken.
    ///
    /// Public because `decomposeImageMultiscale` is a free function rather than a member, and for no
    /// other reason: a caller assembling one by hand is responsible for the planes agreeing with the
    /// plan, which nothing here can check.
    ///
    /// @param planes the layer memory, one plane per channel per layer.
    /// @param plan the transform the planes hold the layers of.
    /// @param result what the decomposition did.
    MultiscaleDecomposition(ImageStorage<float>&& planes, const MultiscaleTransformPlan& plan,
                            const MultiscaleTransformResult& result) noexcept
        : m_planes(std::move(planes)), m_plan(plan), m_result(result)
    {
    }

    /// Whether this decomposition holds layers.
    ///
    /// @return false for a default-constructed decomposition and one that has been moved from.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_planes.valid();
    }

    /// How many pixels one row of a layer holds.
    ///
    /// @return the width, or zero when this decomposition holds nothing.
    [[nodiscard]] std::uint32_t width() const noexcept
    {
        const std::optional<ImageLayout> layout = m_planes.layout();
        return layout.has_value() ? layout->extent().width() : 0u;
    }

    /// How many rows a layer holds.
    ///
    /// @return the height, or zero when this decomposition holds nothing.
    [[nodiscard]] std::uint32_t height() const noexcept
    {
        const std::optional<ImageLayout> layout = m_planes.layout();
        return layout.has_value() ? layout->extent().height() : 0u;
    }

    /// How many channels were decomposed.
    ///
    /// @return the channel count, or zero when this decomposition holds nothing.
    [[nodiscard]] std::uint32_t channelCount() const noexcept
    {
        return m_result.channelCount;
    }

    /// How many layers each channel holds, the residual included.
    ///
    /// @return the layer count, or zero when this decomposition holds nothing.
    [[nodiscard]] std::uint32_t layerCount() const noexcept
    {
        return m_result.layerCount;
    }

    /// How many of those layers are details.
    ///
    /// @return `layerCount() - 1`, and zero when this decomposition holds nothing.
    [[nodiscard]] std::uint32_t detailLayerCount() const noexcept
    {
        return m_result.layerCount > 0u ? m_result.layerCount - 1u : 0u;
    }

    /// The transform these layers were produced by.
    ///
    /// @return the plan.
    [[nodiscard]] const MultiscaleTransformPlan& plan() const noexcept
    {
        return m_plan;
    }

    /// What the decomposition that produced these layers did.
    ///
    /// @return the counts.
    [[nodiscard]] const MultiscaleTransformResult& result() const noexcept
    {
        return m_result;
    }

    /// How many bytes of context memory these layers hold.
    ///
    /// The allocation, padding included, which is what counts against the engine context's budget.
    /// `MultiscaleTransformPlan::layerBytes` is the coefficients alone and is never larger.
    ///
    /// @return the byte count, or zero when this decomposition holds nothing.
    [[nodiscard]] std::uint64_t layerBytes() const noexcept
    {
        return m_planes.capacityBytes();
    }

    /// One layer of one channel, without permission to change it.
    ///
    /// @param channel which channel, below `channelCount()`.
    /// @param layer which layer, below `layerCount()`. The last one is the residual.
    /// @return the plane, or an empty optional when either index is out of range or this
    ///         decomposition holds nothing. It borrows this decomposition and must not outlive it.
    [[nodiscard]] std::optional<PlaneView<const float>> layer(std::uint32_t channel, std::uint32_t layer) const noexcept
    {
        const std::optional<ImageView<const float>> planes = m_planes.view();
        if (!planes.has_value() || channel >= channelCount() || layer >= layerCount())
        {
            return std::nullopt;
        }
        return planes->plane(channel * layerCount() + layer);
    }

    /// One layer of one channel, through which its coefficients may be changed.
    ///
    /// @param channel which channel, below `channelCount()`.
    /// @param layer which layer, below `layerCount()`.
    /// @return the plane, or an empty optional when either index is out of range or this
    ///         decomposition holds nothing.
    [[nodiscard]] std::optional<PlaneView<float>> layer(std::uint32_t channel, std::uint32_t layer) noexcept
    {
        const std::optional<ImageView<float>> planes = m_planes.view();
        if (!planes.has_value() || channel >= channelCount() || layer >= layerCount())
        {
            return std::nullopt;
        }
        return planes->plane(channel * layerCount() + layer);
    }

    /// Where the layer memory sits.
    ///
    /// @return the layout of the whole allocation, or an empty optional when this decomposition
    ///         holds nothing.
    [[nodiscard]] std::optional<ImageLayout> layout() const noexcept
    {
        return m_planes.layout();
    }

    /// Where the first coefficient of the first layer of the first channel is.
    ///
    /// The base address `layout()` describes, for a caller handing the whole allocation to something
    /// that reads a plane out of it by index. A caller that wants one layer asks `layer` instead.
    ///
    /// @return the base address, or null when this decomposition holds nothing.
    [[nodiscard]] const float* samples() const noexcept
    {
        return m_planes.data();
    }

    /// Gives the layer memory back now rather than when this object is destroyed.
    void release() noexcept
    {
        m_planes = ImageStorage<float>{};
        m_result = MultiscaleTransformResult{};
    }

private:
    /// The layers, `channelCount * layerCount` planes of one allocation.
    ImageStorage<float> m_planes{};

    /// The transform they were produced by.
    MultiscaleTransformPlan m_plan{};

    /// What the decomposition did.
    MultiscaleTransformResult m_result{};
};

/// Decomposes every channel of an image into the layers of a multiscale transform.
///
/// The untyped entry point, and the one the exported library carries. The source may hold its
/// channels any way a layout can describe; each channel is read through that arrangement, widened
/// into Float32 - into the first layer itself for the starlet, whose first difference then overwrites
/// it, and into a working plane taken from the engine context for the multiscale-linear transform,
/// which smooths it for every layer - and decomposed. The layers are
/// Float32 whatever the source representation is, because a coefficient is a difference of two
/// smoothings and has no reason to be an integer.
///
/// A sample the validity rule rejects is replaced by `borders.invalidNeighbor` as the plane is
/// gathered, and the count is reported. Everything after that is arithmetic over a plane with no
/// meaningless samples in it, which is what makes the sum of the layers exactly the plane that was
/// decomposed.
///
/// The engine context is named separately from the execution because the two have different
/// lifetimes here. An execution ends when the call returns; the layers outlive it, and their memory
/// is the context's for as long as they do. They must be the same context, and a decomposition asked
/// to put one context's layers under another operation's budget is refused rather than reconciled.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only
/// read. Runs on the threads the operation was admitted and checks cancellation at layer, row and
/// tile boundaries; a cancelled run returns the cancellation rather than a partial decomposition.
///
/// Progress is reported through the execution's sink as each layer of each channel is finished, the
/// residual included, in a total of the plan's layers for every channel.
///
/// @param engine the context whose memory resource and budget own the layers. It cannot shut down
///        while the decomposition lives.
/// @param execution the operation policy and engine-context lease, leased from `engine`.
/// @param sampleType what the source samples are.
/// @param source where they start. Must not be null.
/// @param sourceLayout where they sit. Its element size must be that representation's.
/// @param plan the validated transform.
/// @param selection which samples mean something.
/// @return the layers; `ErrorCode::NotInitialized` when `execution` or `engine` holds nothing;
///         `ErrorCode::InvalidArgument` for a null buffer, a representation this build does not
///         know, a layout that does not describe it, a selection whose mask does not match its
///         rule, or a plan `planMultiscaleTransform` did not produce - and whatever that builder
///         reports when the plan's request is checked again; `ErrorCode::IncompatibleArguments`
///         when the execution is not leased from `engine`, when the promise cannot be kept in
///         Float32, or when a mask does not cover the image; `ErrorCode::OutOfRange` when the
///         layers of this picture, one plane per channel per layer, need more planes than the
///         engine admits in one image; or a memory-budget or cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MultiscaleDecomposition>
decomposeImageMultiscale(const EngineContext& engine, const ExecutionContext& execution, SampleType sampleType,
                         const void* source, const ImageLayout& sourceLayout, const MultiscaleTransformPlan& plan,
                         const SpatialSelection& selection = {});

/// Decomposes a checked typed image view into the layers of a multiscale transform.
///
/// The typed spelling of `decomposeImageMultiscale`. It adds no numeric, border or validity rule.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param engine the context whose memory owns the layers.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param plan the validated transform.
/// @param selection which samples mean something.
/// @return whatever `decomposeImageMultiscale` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<MultiscaleDecomposition>
decomposeMultiscale(const EngineContext& engine, const ExecutionContext& execution,
                    const ImageView<const SampleT>& source, const MultiscaleTransformPlan& plan,
                    const SpatialSelection& selection = {})
{
    return decomposeImageMultiscale(engine, execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), plan,
                                    selection);
}

/// Produces the residual of a transform without holding the layers above it.
///
/// The untyped entry point, and the one the exported library carries. A decomposition's residual is
/// its cascade of smoothings - the plane that is left once every detail layer has been taken out of
/// the source - and a caller that wants only the largest scale is otherwise obliged to hold every
/// smaller one to get it. This runs the same smoothings in the same order, alternating between the
/// answer and one working plane, so what it holds does not grow with the layer count, and produces the plane
/// `MultiscaleDecomposition::layer(channel, residualLayer())` would have held.
///
/// The answer is that layer exactly rather than approximately: the smoothings are the plan's own,
/// applied in the plan's order, so a caller may use either route interchangeably and compare them.
///
/// It writes every sample of its destination, for the reason the reconstruction does: the validity
/// rule was resolved at the door and nothing after it can distinguish a substituted sample from a
/// measured one.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only
/// read. Runs on the threads the operation was admitted and checks cancellation at layer and
/// row-block boundaries.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source samples start. Must not be null.
/// @param sourceLayout where they sit.
/// @param destination where the residual goes. Must not be null, and must not overlap the source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param plan the validated transform.
/// @param selection which samples mean something.
/// @return the counts and the plan; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a null buffer, a representation this build does not
///         know, a layout that does not describe it, or a plan that has not been validated;
///         `ErrorCode::IncompatibleArguments` for disagreeing extents, for buffers that share a
///         byte, or for a promise Float32 cannot keep; or a memory-budget or cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpatialFilterResult>
smoothImageMultiscaleResidual(const ExecutionContext& execution, SampleType sampleType, const void* source,
                              const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                              const MultiscaleTransformPlan& plan, const SpatialSelection& selection = {});

/// Produces the residual of a transform over checked typed image views.
///
/// The typed spelling of `smoothImageMultiscaleResidual`, which owns every check and every
/// computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param plan the validated transform.
/// @param selection which samples mean something.
/// @return whatever `smoothImageMultiscaleResidual` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<SpatialFilterResult>
smoothMultiscaleResidual(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                         const ImageView<SampleT>& destination, const MultiscaleTransformPlan& plan,
                         const SpatialSelection& selection = {})
{
    return smoothImageMultiscaleResidual(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(),
                                         destination.data(), destination.layout(), plan, selection);
}

/// What one layer contributes to a reconstruction.
struct MultiscaleLayerWeight
{
    /// Whether the layer contributes at all. A disabled layer contributes nothing whatever its gain,
    /// which is a different statement from a gain of zero and is worth being able to make.
    bool enabled = true;

    /// How much of the layer survives. One reproduces it, zero removes it, above one amplifies the
    /// structures of that scale and below zero inverts them. Must be finite.
    double gain = 1.0;
};

/// What a caller asks a reconstruction for.
struct MultiscaleReconstructionRequest
{
    /// What each layer contributes, one entry per layer of the decomposition, the residual last.
    ///
    /// An empty span means every layer at gain one, which is the identity and is what the round-trip
    /// property is stated about. A span of any other length is refused.
    std::span<const MultiscaleLayerWeight> layers;

    /// The closed interval a reconstructed value is held inside before it is written, or no value to
    /// write what the sum produced.
    ///
    /// The interval is stated in the image's own units, or not stated at all: a clamp to the unit
    /// interval would be wrong for an image whose samples are stored code values rather than a
    /// normalized range.
    std::optional<KernelValueWindow> clamp{};

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// Adds the layers of a decomposition back into an image.
///
/// The other half of the round trip. With no weights, or with every weight enabled at gain one, it
/// reproduces the plane that was decomposed to within Float32 rounding; with weights it is the
/// operation every multiscale process is built from - suppress a scale, amplify a scale, keep only
/// the residual.
///
/// It writes every sample of its destination. The decomposition already resolved which samples meant
/// something, at the door, and nothing since then has been able to distinguish them; the image's own
/// validity rule still describes which of the written samples mean something, which is why the
/// declared effect preserves validity rather than regenerating it.
///
/// Each sample is the gain-weighted sum of its layers accumulated in Float64, held inside the clamp
/// when one is stated, and narrowed once. Thread-safe with respect to `execution` and the
/// decomposition, which are only read. Runs on the threads the operation was admitted and is not
/// failure-atomic: a cancelled run may leave part of the destination written.
///
/// @param execution the operation policy and engine-context lease.
/// @param decomposition the layers to add, which must have been produced by a live engine context.
/// @param sampleType what the destination samples are.
/// @param destination where they go. Must not be null.
/// @param destinationLayout where they sit. Its extent must be the decomposition's picture and its
///        channel count the decomposition's.
/// @param request the per-layer weights, the optional clamp and the output policy.
/// @return the counts and the plan; `ErrorCode::NotInitialized` when `execution` holds nothing or
///         the decomposition holds no layers; `ErrorCode::InvalidArgument` for a null buffer, a
///         representation this build does not know, a layout that does not describe it, a
///         non-finite gain or clamp endpoint, an inverted clamp, or a write-back policy naming
///         `DomainMapping::FullScale`; `ErrorCode::IncompatibleArguments` when the destination is
///         not the decomposition's picture, when the weight span is neither empty nor one entry per
///         layer, or when the promise cannot be kept in Float32; or a memory-budget or cancellation
///         failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpatialFilterResult>
reconstructImageMultiscale(const ExecutionContext& execution, const MultiscaleDecomposition& decomposition,
                           SampleType sampleType, void* destination, const ImageLayout& destinationLayout,
                           const MultiscaleReconstructionRequest& request = {});

/// Adds the layers of a decomposition back into a checked typed image view.
///
/// The typed spelling of `reconstructImageMultiscale`, which owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param decomposition the layers to add.
/// @param destination the checked writable image view.
/// @param request the per-layer weights, the optional clamp and the output policy.
/// @return whatever `reconstructImageMultiscale` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<SpatialFilterResult>
reconstructMultiscale(const ExecutionContext& execution, const MultiscaleDecomposition& decomposition,
                      const ImageView<SampleT>& destination, const MultiscaleReconstructionRequest& request = {})
{
    return reconstructImageMultiscale(execution, decomposition, kSampleTypeOf<SampleT>, destination.data(),
                                      destination.layout(), request);
}

/// The noise one layer of a decomposition carries.
struct MultiscaleLayerNoise
{
    /// The median coefficient of the layer. Close to zero for a detail layer, and not for the
    /// residual, which is a smoothed image rather than a difference.
    double median = 0.0;

    /// The median of the absolute deviations from that median.
    double medianAbsoluteDeviation = 0.0;

    /// The standard deviation that deviation implies, which is it multiplied by
    /// `kMedianAbsoluteDeviationToSigma` from `Algorithms/Statistics/RobustStatistics.h`. There is
    /// one such conversion in the engine and this is not a second copy of it.
    double sigma = 0.0;

    /// How many coefficients the estimate was taken over.
    std::uint64_t sampleCount = 0u;

    /// How many times the estimate read the layer.
    std::uint64_t planeScans = 0u;
};

/// Estimates the noise in one layer of a decomposition.
///
/// The measurement a coefficient threshold is stated in multiples of. It is exact - a rank rather
/// than a histogram approximation - and it is taken around the layer's own median rather than around
/// zero, which is what makes it correct for the residual as well as for a detail layer.
///
/// @param execution the operation policy and engine-context lease.
/// @param decomposition the layers to read.
/// @param channel which channel, below the decomposition's channel count.
/// @param layer which layer, below the decomposition's layer count.
/// @return the estimate; `ErrorCode::NotInitialized` when `execution` holds nothing or the
///         decomposition holds no layers; `ErrorCode::OutOfRange` when either index names no layer;
///         `ErrorCode::EmptySelection` for a layer with no samples; or an execution, allocation or
///         cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MultiscaleLayerNoise>
estimateMultiscaleLayerNoise(const ExecutionContext& execution, const MultiscaleDecomposition& decomposition,
                             std::uint32_t channel, std::uint32_t layer);

/// How a coefficient below a threshold is removed.
enum class MultiscaleShrinkage : std::uint8_t
{
    /// A coefficient below the threshold becomes zero, and one above it is moved towards zero by the
    /// threshold. Continuous, so it leaves no step at the threshold, and it biases what survives.
    Soft = 0u,

    /// A coefficient below the threshold becomes zero, and one above it is left exactly as it was.
    /// Unbiased on what survives, at the cost of a discontinuity at the threshold.
    Hard = 1u
};

/// Whether a value names one of the shrinkage rules this build implements.
///
/// @param shrinkage the value to inspect.
/// @return true for both rules.
[[nodiscard]] constexpr bool isMultiscaleShrinkage(MultiscaleShrinkage shrinkage) noexcept
{
    return shrinkage == MultiscaleShrinkage::Soft || shrinkage == MultiscaleShrinkage::Hard;
}

/// What a caller asks a coefficient shrinkage for.
struct MultiscaleShrinkRequest
{
    /// Which rule removes a coefficient.
    MultiscaleShrinkage shrinkage = MultiscaleShrinkage::Soft;

    /// The coefficient magnitude below which a coefficient is noise, in the image's own units.
    ///
    /// Stated absolutely rather than in multiples of a sigma, because the sigma is a separate
    /// measurement a caller may take once and reuse, may take from a different layer, or may not
    /// take at all. Must be finite and not negative; zero shrinks nothing.
    double threshold = 0.0;

    /// How much of the shrunk coefficient replaces the original, within `[0, 1]`.
    ///
    /// One applies the rule outright and zero leaves the layer alone. Anything between is a partial
    /// application, which is how an interactive control offers strength.
    double amount = 1.0;
};

/// What one shrinkage did.
struct MultiscaleShrinkResult
{
    /// How many coefficients were considered, which is the layer's sample count.
    std::uint64_t samplesConsidered = 0u;

    /// How many of them the rule moved.
    std::uint64_t samplesChanged = 0u;

    /// Workers the operation could actually use.
    std::uint32_t plannedWorkers = 0u;
};

/// Removes the coefficients of one layer that a threshold calls noise.
///
/// Changes the decomposition in place, which is the point: a caller decomposes once, shrinks the
/// layers it wants to, and reconstructs. Nothing else about the layer changes, so a reconstruction
/// after this is still the sum of the layers and is still exact where nothing was shrunk.
///
/// @param execution the operation policy and engine-context lease.
/// @param decomposition the layers to change.
/// @param channel which channel, below the decomposition's channel count.
/// @param layer which layer, below the decomposition's layer count.
/// @param request the rule, the threshold and how much of it to apply.
/// @return the counts; `ErrorCode::NotInitialized` when `execution` holds nothing or the
///         decomposition holds no layers; `ErrorCode::InvalidArgument` for an unknown rule, a
///         non-finite or negative threshold, or an amount outside `[0, 1]`; `ErrorCode::OutOfRange`
///         when either index names no layer; or an execution or cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MultiscaleShrinkResult>
shrinkMultiscaleLayer(const ExecutionContext& execution, MultiscaleDecomposition& decomposition, std::uint32_t channel,
                      std::uint32_t layer, const MultiscaleShrinkRequest& request);

/// What a decomposition does to the image it reads.
///
/// It writes no image at all - it produces layers - so it reads samples and changes nothing about
/// the description, the meaning, the solution, the cards or the output decision of anything.
inline constexpr OperationEffect kMultiscaleDecompositionEffect{.samples = SampleEffect::Reads};

/// What a reconstruction does to the image it writes.
///
/// It writes samples and nothing else. The extent, the representation and the channel schema are the
/// decomposition's, which are the source's; a reweighted frame is still a frame of the same thing;
/// nothing moves a star; and no source card and no output decision is invalidated by adding the
/// layers back.
///
/// Validity is preserved, and that is a claim about the arithmetic rather than a default: a rejected
/// source sample was replaced by the border substitute before the first layer existed, so the
/// reconstruction can neither create nor destroy meaninglessness - it reproduces whatever stood in
/// for it.
inline constexpr OperationEffect kMultiscaleReconstructionEffect{};

} // namespace an
