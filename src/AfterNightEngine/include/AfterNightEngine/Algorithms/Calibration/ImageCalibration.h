// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Checked bias, dark and flat calibration in one image pass.
///
/// Calibration widens dynamic range and changes what samples mean, so it is a plan rather than a
/// handful of unrelated arithmetic calls. The plan validates every master against the light,
/// prepares the flat's central normalization once, resolves an optional per-light dark scale, and
/// records the Float64 divide decision. Execution then reads every selected component and writes the
/// result once. It allocates no full-frame intermediate and never owns any image memory.
///
/// Each sample goes through four steps in this order:
/// add the pedestal, subtract the bias, subtract the scaled dark, divide by the normalized flat. Each
/// step computes in Float64 and narrows to the storage type, as do the scaled dark and the normalized
/// flat on their own, so the scalar oracle and every compiled CPU row store identical bits. An output
/// whose flat is zero, or whose light or master sample is excluded by that frame's validity rule, is
/// written as a quiet NaN and counted, which is why the result's validity is restated as finite-only.
///
/// The pedestal is in the light's own numeric domain: it is neither divided by 65,535 for floating
/// storage nor clamped to the unit interval, so one value means the same thing whatever the storage
/// and a raw or physically scaled working value is never clipped. A caller with a pedestal in ADU
/// converts it when it builds the request, where the source scale is known.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// What an image's samples currently mean. Defined in `Image/ImageSemantics.h`.
class ImageSemantics;

/// The checked metadata change a completed operation applies. Defined in `Image/ImageMetadata.h`.
struct MetadataTransition;

/// Implementation-only access to a calibration plan's closed state.
///
/// Defined in the library translation unit and unavailable to callers; declared here only so the
/// plan can keep its construction state private without making internal row helpers public.
class CalibrationPlanAccess;

/// The largest repeating filter block whose flat normalization the engine stores.
///
/// Six by six is the largest public CFA block, so this covers every site without allocation.
inline constexpr std::uint32_t kMaxFlatNormalizationGroups = 36u;

/// The side of the central square used to estimate a per-light dark scale, in pixels.
inline constexpr std::uint32_t kCalibrationDarkOptimizationSide = 512u;

/// How a flat master is normalized before it is divided into a light.
enum class FlatNormalizationMode : std::uint8_t
{
    /// Use the flat's samples exactly as supplied.
    None,

    /// Divide every flat sample by the mean of its channel or CFA site in the central third.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    CentralMean
};

/// Whether a value names a flat-normalization rule this build implements.
///
/// @param mode the value to inspect.
/// @return true for every implemented rule.
[[nodiscard]] constexpr bool isFlatNormalizationMode(FlatNormalizationMode mode) noexcept
{
    return static_cast<std::uint8_t>(mode) <= static_cast<std::uint8_t>(FlatNormalizationMode::CentralMean);
}

/// The rule's own name.
///
/// @param mode the rule to name. A value outside the enumeration is named
///        `"UnknownFlatNormalizationMode"`.
/// @return a null-terminated string literal compiled into this library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* flatNormalizationModeName(FlatNormalizationMode mode) noexcept;

/// How the dark master is scaled before subtraction.
enum class DarkScalingMode : std::uint8_t
{
    /// Multiply the dark by `CalibrationRequest::darkScale`.
    Fixed,

    /// Choose a factor from zero through two that minimizes the variance of the pedestal- and
    /// bias-corrected light minus the scaled dark over the central
    /// `kCalibrationDarkOptimizationSide` square, before the flat. The search is golden-section to
    /// an interval 0.001 wide, at most 64 evaluations, over sufficient statistics gathered in one
    /// scan.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    OptimizeCentralRegion
};

/// Whether a value names a dark-scaling rule this build implements.
///
/// @param mode the value to inspect.
/// @return true for every implemented rule.
[[nodiscard]] constexpr bool isDarkScalingMode(DarkScalingMode mode) noexcept
{
    return static_cast<std::uint8_t>(mode) <= static_cast<std::uint8_t>(DarkScalingMode::OptimizeCentralRegion);
}

/// The rule's own name.
///
/// @param mode the rule to name. A value outside the enumeration is named
///        `"UnknownDarkScalingMode"`.
/// @return a null-terminated string literal compiled into this library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* darkScalingModeName(DarkScalingMode mode) noexcept;

/// Which calibration implementation actually wrote the destination.
enum class CalibrationExecutionPath : std::uint8_t
{
    /// The scalar oracle, including cases whose validity or CFA addressing prevents a contiguous
    /// fused run.
    Reference,

    /// The selected CPU table's fused contiguous-row implementation.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    CompiledFused
};

/// Whether a value names an execution path this build implements.
///
/// @param path the value to inspect.
/// @return true for every implemented path.
[[nodiscard]] constexpr bool isCalibrationExecutionPath(CalibrationExecutionPath path) noexcept
{
    return static_cast<std::uint8_t>(path) <= static_cast<std::uint8_t>(CalibrationExecutionPath::CompiledFused);
}

/// The path's own name.
///
/// @param path the path to name. A value outside the enumeration is named
///        `"UnknownCalibrationExecutionPath"`.
/// @return a null-terminated string literal compiled into this library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* calibrationExecutionPathName(CalibrationExecutionPath path) noexcept;

/// A light or master whose storage, description and validity have been checked together.
///
/// The samples and the optional mask are borrowed and must outlive this view and every planning or
/// execution call receiving it; the layout and the descriptor are copied into it, so what describes
/// the frame is the view's and only the memory is the caller's. Only Float32 and Float64 Mono or RGB
/// images in a linear, raw or physically scaled numeric domain are admitted; integer boundary frames
/// are converted explicitly through `Image/ImageConversion.h` before calibration.
class CalibrationFrameView
{
public:
    /// A view holding nothing.
    CalibrationFrameView() noexcept = default;

    /// Checks and borrows one calibration frame.
    ///
    /// @param samples where the image storage starts. Must not be null and must be aligned for the
    ///        sample type.
    /// @param bufferBytes bytes available from `samples`, covering every byte named by `layout`.
    /// @param layout where its samples sit, planar or interleaved.
    /// @param descriptor what the samples mean.
    /// @param validity which samples participate, with a covering mask when the rule needs one.
    ///        `maskPlane` must be zero: a shared mask uses that plane, while a per-channel mask is
    ///        addressed by the sample's channel automatically.
    /// @return the checked view; `ErrorCode::Unsupported` for an integer representation, the
    ///         nonlinear encoded domain, or a channel schema other than Mono and RGB;
    ///         `ErrorCode::InvalidArgument` for null or misaligned storage, a layout that does not
    ///         describe the descriptor or is semi-planar, a byte extent short of the layout, a
    ///         non-zero `maskPlane`, a mask missing for a rule that reads one or supplied for a
    ///         rule that does not; `ErrorCode::IncompatibleArguments` for a mask whose extent or
    ///         plane count does not cover the image.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<CalibrationFrameView>
    create(const void* samples, std::uint64_t bufferBytes, const ImageLayout& layout, const ImageDescriptor& descriptor,
           const KernelValidityView& validity = {});

    /// Whether this view borrows a checked frame.
    ///
    /// @return false only for a default-constructed view.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_samples != nullptr;
    }

    /// Where the samples start.
    ///
    /// @return the borrowed address, or null for an empty view.
    [[nodiscard]] const void* samples() const noexcept
    {
        return m_samples;
    }

    /// Bytes the caller proved are available from `samples()`.
    ///
    /// @return zero for an empty view, otherwise at least `layout().totalBytes()`.
    [[nodiscard]] std::uint64_t bufferBytes() const noexcept
    {
        return m_bufferBytes;
    }

    /// Where the samples sit.
    ///
    /// @return the checked layout, which this view holds. The view must be valid.
    [[nodiscard]] const ImageLayout& layout() const noexcept
    {
        return *m_layout;
    }

    /// What the samples mean.
    ///
    /// @return the checked description, which this view holds. The view must be valid.
    [[nodiscard]] const ImageDescriptor& descriptor() const noexcept
    {
        return *m_descriptor;
    }

    /// Which samples participate.
    ///
    /// @return the validity rule and optional borrowed mask.
    [[nodiscard]] const KernelValidityView& validity() const noexcept
    {
        return m_validity;
    }

private:
    /// Keeps values already checked by `create`.
    CalibrationFrameView(const void* samples, std::uint64_t bufferBytes, const ImageLayout& layout,
                         const ImageDescriptor& descriptor, const KernelValidityView& validity) noexcept
        : m_samples(samples), m_bufferBytes(bufferBytes), m_layout(layout), m_descriptor(descriptor),
          m_validity(validity)
    {
    }

    /// The first sample, or null for an empty view.
    const void* m_samples = nullptr;

    /// Bytes available from `m_samples`, or zero for an empty view.
    std::uint64_t m_bufferBytes = 0u;

    /// The layout, held by value: it describes the samples rather than borrowing anything of them,
    /// and an image hands one out as a value, so a view that pointed at one would outlive it.
    std::optional<ImageLayout> m_layout{};

    /// The description, held by value, for the reason the layout is.
    std::optional<ImageDescriptor> m_descriptor{};

    /// The validity inputs, whose optional mask is borrowed too.
    KernelValidityView m_validity{};
};

/// The optional masters used by one calibration.
struct CalibrationMasters
{
    /// The offset frame subtracted first.
    std::optional<CalibrationFrameView> bias{};

    /// The thermal frame scaled and subtracted after the bias.
    std::optional<CalibrationFrameView> dark{};

    /// The sensitivity frame divided into the result last.
    std::optional<CalibrationFrameView> flat{};
};

/// What a caller asks one calibration plan to do.
struct CalibrationRequest
{
    /// The finite non-negative value added to every light sample before master subtraction, in the
    /// light's stated numeric domain. No implicit ADU-to-domain conversion and no clamp is performed;
    /// a caller holding an ADU pedestal converts it where the source scale is known.
    double pedestal = 0.0;

    /// How the dark multiplier is obtained.
    DarkScalingMode darkScaling = DarkScalingMode::Fixed;

    /// The finite non-negative multiplier used by `DarkScalingMode::Fixed`.
    double darkScale = 1.0;

    /// How the flat is normalized before division.
    FlatNormalizationMode flatNormalization = FlatNormalizationMode::CentralMean;
};

/// What one completed calibration reports.
struct CalibrationResult
{
    /// The implementation and instruction set that actually wrote the samples.
    BackendSelection backend{};

    /// Whether the scalar oracle or a compiled fused row ran.
    CalibrationExecutionPath path = CalibrationExecutionPath::Reference;

    /// Samples written, including outputs that became non-finite.
    std::uint64_t samplesWritten = 0u;

    /// Outputs that are NaN or infinity after the final step.
    std::uint64_t nonFiniteOutputs = 0u;

    /// Outputs written as NaN because the light or a master sample at that position is excluded by
    /// its stated validity; each such output counts once, and also in `nonFiniteOutputs`.
    std::uint64_t invalidInputs = 0u;

    /// Workers the scheduler could use for the row tasks.
    std::uint32_t plannedWorkers = 0u;

    /// Full destination passes, always one.
    std::uint32_t imagePasses = 0u;
};

/// A validated, reusable description of one calibration.
///
/// It owns only scalar decisions and at most thirty-six flat means. With fixed dark scaling and no
/// normalized flat it may be reused for any light and masters matching the prepared descriptions.
/// With optimized scaling it is specific to the unchanged light, dark and optional bias whose
/// sufficient statistics prepared it; with central normalization it is specific to the unchanged
/// flat whose means it holds.
class CalibrationPlan
{
public:
    /// A plan holding nothing.
    CalibrationPlan() noexcept = default;

    /// Whether this plan was produced by `prepareImageCalibration`.
    ///
    /// @return false only for a default-constructed plan.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_valid;
    }

    /// Whether a bias is subtracted.
    ///
    /// @return true when the prepared master set contained a bias.
    [[nodiscard]] bool usesBias() const noexcept
    {
        return m_usesBias;
    }

    /// Whether a dark is scaled and subtracted.
    ///
    /// @return true when the prepared master set contained a dark.
    [[nodiscard]] bool usesDark() const noexcept
    {
        return m_usesDark;
    }

    /// Whether a flat is normalized and divided.
    ///
    /// @return true when the prepared master set contained a flat.
    [[nodiscard]] bool usesFlat() const noexcept
    {
        return m_usesFlat;
    }

    /// The dark multiplier execution applies.
    ///
    /// @return the requested fixed factor or the central-region optimum.
    [[nodiscard]] double effectiveDarkScale() const noexcept
    {
        return m_effectiveDarkScale;
    }

    /// The arithmetic width of the calibration divide.
    ///
    /// @return `ComputePrecision::Float64`.
    [[nodiscard]] ComputePrecision precision() const noexcept
    {
        return ComputePrecision::Float64;
    }

    /// Why the arithmetic is Float64.
    ///
    /// @return `Float64Justification::CalibrationDivide`.
    [[nodiscard]] Float64Justification float64Justification() const noexcept
    {
        return Float64Justification::CalibrationDivide;
    }

    /// How many separate flat means were prepared.
    ///
    /// @return zero without a normalized flat, one for a Mono flat, three for an RGB flat, or the
    ///         CFA block area for a mosaiced one.
    [[nodiscard]] std::uint32_t flatNormalizationGroupCount() const noexcept
    {
        return m_flatGroupCount;
    }

    /// What the plan calibrates, stated once as the light it was prepared for.
    ///
    /// Everything a caller needs to know whether a frame or a band of one belongs to this plan:
    /// the representation, extent, channel schema, colour basis, numeric domain, filter array,
    /// origin and aspect the light was described with. A caller comparing a description with this
    /// one compares by value.
    ///
    /// @return the description, or nothing for a plan holding nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<ImageDescriptor> description() const noexcept;

    /// The value added to every light sample before the masters are applied.
    ///
    /// @return the pedestal, in the light's stated numeric domain.
    [[nodiscard]] double pedestal() const noexcept
    {
        return m_pedestal;
    }

    /// How the flat is normalized before it divides.
    ///
    /// @return the requested rule; read only when `usesFlat()`.
    [[nodiscard]] FlatNormalizationMode flatNormalization() const noexcept
    {
        return m_flatNormalization;
    }

    /// How many channels the prepared flat has.
    ///
    /// @return the light's channel count, one for a Mono flat over an RGB light, or zero without
    ///         a flat.
    [[nodiscard]] std::uint32_t flatChannelCount() const noexcept
    {
        return m_flatChannels;
    }

    /// Every prepared central mean, indexed by channel or CFA block site as
    /// `flatNormalizationFactor` is.
    ///
    /// @return the means, `flatNormalizationGroupCount()` of them, borrowed from the plan.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const double> flatNormalizationFactors() const noexcept;

    /// One prepared central mean.
    ///
    /// @param group the channel or CFA-site index.
    /// @return the mean, or NaN when `group` is outside `flatNormalizationGroupCount()`.
    [[nodiscard]] AFTERNIGHTENGINE_API double flatNormalizationFactor(std::uint32_t group) const noexcept;

    /// Samples gathered once for optimized dark scaling.
    ///
    /// @return zero under fixed scaling, otherwise the accepted central-region samples.
    [[nodiscard]] std::uint64_t darkOptimizationSamples() const noexcept
    {
        return m_darkOptimizationSamples;
    }

    /// Objective evaluations used to resolve optimized dark scaling.
    ///
    /// @return zero under fixed scaling and a positive count after optimization.
    [[nodiscard]] std::uint32_t darkOptimizationEvaluations() const noexcept
    {
        return m_darkOptimizationEvaluations;
    }

private:
    friend class CalibrationPlanAccess;
    friend AFTERNIGHTENGINE_API Result<CalibrationPlan> prepareImageCalibration(const ExecutionContext&,
                                                                                const CalibrationFrameView&,
                                                                                const CalibrationMasters&,
                                                                                const CalibrationRequest&);
    friend AFTERNIGHTENGINE_API Result<CalibrationResult>
    calibrateImage(const ExecutionContext&, const CalibrationPlan&, const CalibrationFrameView&,
                   const CalibrationMasters&, void*, std::uint64_t, const ImageLayout&);
    friend AFTERNIGHTENGINE_API Result<MetadataTransition> imageCalibrationTransition(const CalibrationPlan&,
                                                                                      const ImageSemantics&);

    /// Prepared flat means, indexed by channel or CFA block site.
    std::array<double, kMaxFlatNormalizationGroups> m_flatFactors{};

    /// The CFA pattern and phase prepared for the light.
    CfaDescriptor m_cfa{};

    /// The requested pedestal in the light's stated domain.
    double m_pedestal = 0.0;

    /// The resolved dark multiplier.
    double m_effectiveDarkScale = 1.0;

    /// Accepted samples in the optimized-dark sufficient statistics.
    std::uint64_t m_darkOptimizationSamples = 0u;

    /// The frame width.
    std::uint32_t m_width = 0u;

    /// The frame height.
    std::uint32_t m_height = 0u;

    /// The light's channel count.
    std::uint32_t m_channels = 0u;

    /// The flat's channel count, zero without a flat.
    std::uint32_t m_flatChannels = 0u;

    /// Prepared flat means in use.
    std::uint32_t m_flatGroupCount = 0u;

    /// Objective evaluations in the optimized-dark search.
    std::uint32_t m_darkOptimizationEvaluations = 0u;

    /// The resident floating representation.
    SampleType m_sampleType = SampleType::Float32;

    /// The light and masters' stated numeric domain.
    NumericDomain m_numericDomain = NumericDomain::RawSensorCode;

    /// The light's channel schema.
    ChannelSchema m_channelSchema = ChannelSchema::Mono;

    /// The light's colour basis.
    ColorSpace m_colorSpace = ColorSpace::Unspecified;

    /// Which corner contains row zero.
    CoordinateOrigin m_coordinateOrigin = CoordinateOrigin::TopLeft;

    /// The light's horizontal-to-vertical pixel aspect.
    double m_pixelAspectRatio = 1.0;

    /// The requested flat rule.
    FlatNormalizationMode m_flatNormalization = FlatNormalizationMode::CentralMean;

    /// The requested dark rule.
    DarkScalingMode m_darkScaling = DarkScalingMode::Fixed;

    /// Whether the executor must receive a bias again.
    bool m_usesBias = false;

    /// Whether the executor must receive a dark again.
    bool m_usesDark = false;

    /// Whether the executor must receive a flat again.
    bool m_usesFlat = false;

    /// Whether the plan was prepared successfully.
    bool m_valid = false;
};

/// The published effect of calibration.
///
/// Calibration preserves geometry, astrometry and source cards, restates the processing meaning,
/// and regenerates validity as finite-only because a zero flat deliberately produces NaN.
inline constexpr OperationEffect kImageCalibrationEffect{
    .samples = SampleEffect::Writes,
    .semantics = SemanticEffect::Restates,
    .validity = ValidityTransition{
        .policy = ValidityPolicy::Regenerate,
        .produced = *ValidityDescriptor::create(ValidityDescriptorRequest{.rule = ValidityRule::FiniteOnly})}};

/// Validates the light and masters and prepares reusable scalar calibration state.
///
/// Flat central means are gathered once and never materialized as a normalized image: one Float64
/// mean per channel, or per CFA block site for a mosaiced flat, over the central third of each
/// axis, omitting samples the flat's validity excludes. Optimized dark scaling accepts only a Mono
/// frame at least `kCalibrationDarkOptimizationSide` pixels on each side and minimizes the variance
/// objective `DarkScalingMode::OptimizeCentralRegion` states from sufficient statistics gathered
/// in one central-square scan. Every master must match the light's extent, representation, numeric
/// domain, coordinate origin and pixel aspect; a bias or dark also matches its channel schema,
/// colour basis and CFA phase, while an RGB light may instead take one Mono flat for all its
/// channels.
///
/// @param execution the operation policy and engine-context lease.
/// @param input the light whose description defines the plan and whose values are used only when
///        dark scaling is optimized.
/// @param masters the optional bias, dark and flat.
/// @param request the pedestal, dark rule and flat rule.
/// @return the plan; `ErrorCode::NotInitialized` without an execution lease;
///         `ErrorCode::InvalidArgument` for an empty light or master view, an unknown rule, a
///         non-finite or negative pedestal or dark scale, a request with no master and a zero
///         pedestal, or optimization without a dark; `ErrorCode::IncompatibleArguments` when a
///         master disagrees with the light as described above; `ErrorCode::OutOfRange` when the
///         light is narrower or shorter than three pixels under flat normalization, or is not Mono
///         or smaller than the dark-optimization square under optimized scaling;
///         `ErrorCode::EmptySelection` when a flat group or the dark square has no valid samples;
///         `ErrorCode::InvalidData` when a participating sample in either region is non-finite, a
///         flat mean is zero or non-finite, or the dark has no variance; `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the execution stops.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CalibrationPlan>
prepareImageCalibration(const ExecutionContext& execution, const CalibrationFrameView& input,
                        const CalibrationMasters& masters, const CalibrationRequest& request = {});

/// Validates the masters against a described light and prepares reusable scalar calibration state.
///
/// The same plan as the overload above for a light that is not resident - a set of frames a stack
/// calibrates one band at a time, described once at the floating representation they are widened
/// to. Everything the plan holds comes from the description and the masters; only optimized dark
/// scaling reads the light's samples, and it is refused here because there are none to read. A
/// stack that wants a per-light optimized scale prepares one plan per light through the overload
/// above.
///
/// @param execution the operation policy and engine-context lease.
/// @param light what the lights are: a Float32 or Float64 Mono or RGB linear or raw description
///        whose extent, numeric domain, channels and CFA phase every master must match.
/// @param masters the optional bias, dark and flat.
/// @param request the pedestal, dark rule and flat rule. `DarkScalingMode::OptimizeCentralRegion`
///        is refused.
/// @return the plan; `ErrorCode::Unsupported` for an integer representation, a nonlinear domain
///         or a channel schema other than Mono and RGB; `ErrorCode::InvalidArgument` for optimized
///         dark scaling; and otherwise what the resident-light overload reports.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CalibrationPlan>
prepareImageCalibration(const ExecutionContext& execution, const ImageDescriptor& light,
                        const CalibrationMasters& masters, const CalibrationRequest& request = {});

/// Executes one prepared calibration into disjoint caller-owned storage.
///
/// Bias, dark and flat arithmetic is fused into one destination pass. A non-mosaiced light whose
/// every frame states the all-valid rule, and whose destination and masters share its planar or
/// interleaved arrangement, runs the selected CPU table's compiled row when that instruction set has
/// one; a mosaiced light, any other validity rule, a differing arrangement, the reference backend or
/// an instruction set without a compiled row runs the scalar oracle, and the result names the path
/// that ran. Both store the same bits. A zero flat produces NaN counted in `nonFiniteOutputs`; an
/// output whose light or master sample is excluded by its validity rule produces NaN counted in
/// both `invalidInputs` and `nonFiniteOutputs`. The destination is completely written on success;
/// its row padding is left untouched. On a failure, including a stop, it may be partly written.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the successfully prepared plan.
/// @param input the light, matching the plan.
/// @param masters the same roles and compatible descriptions the plan was prepared for. Masters
///        whose samples produced an optimized dark factor or central flat means remain unchanged.
/// @param destination where output storage starts. Must not be null or overlap any input.
/// @param destinationBytes bytes available from `destination`, covering every byte named by
///        `destinationLayout`.
/// @param destinationLayout where the output samples sit, describing the same image as the light.
/// @return execution counters and the actual backend; `ErrorCode::NotInitialized` without a lease
///         or valid plan; `ErrorCode::InvalidArgument` for null, misaligned or undersized
///         destination storage; `ErrorCode::IncompatibleArguments` when the light, the master roles,
///         a master or the destination layout's extent, element size or arrangement does not match
///         the plan; `ErrorCode::Aliasing` when destination storage overlaps an input;
///         `ErrorCode::UnsupportedBackend` when the execution requires the CPU backend with fallback
///         forbidden and only the scalar oracle can serve the call; `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the execution stops.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CalibrationResult>
calibrateImage(const ExecutionContext& execution, const CalibrationPlan& plan, const CalibrationFrameView& input,
               const CalibrationMasters& masters, void* destination, std::uint64_t destinationBytes,
               const ImageLayout& destinationLayout);

/// Builds the metadata transition for a calibrated result.
///
/// The new meaning is linear and marks exactly the master roles the plan applied. Existing unrelated
/// processing flags remain unchanged, validity becomes finite-only, and provenance records the
/// component set, dark factor and flat rule so a later caller never infers them from the pixels.
///
/// @param plan the successfully prepared plan.
/// @param source the light's current processing meaning.
/// @return the checked transition ready for `Image::transition`; `ErrorCode::NotInitialized` for an
///         empty plan, or whatever rebuilding the semantics or provenance reports.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MetadataTransition> imageCalibrationTransition(const CalibrationPlan& plan,
                                                                                         const ImageSemantics& source);

} // namespace an
