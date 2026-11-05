// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Color adjustments and colorization.
///
/// Four per-pixel operations over Float32 or Float64 images, each with its numeric choices named:
/// `adjustImageHsl` and `adjustImageLch` shift and scale cylindrical components and return RGB,
/// `enhanceImageSaturation` multiplies HSL saturation for a hue interval above a lightness threshold,
/// and `colorizeImage` gives an intensity image a fixed hue and saturation.
/// `deriveSaturationEnhancementBackground` computes the saturation operation's threshold, as a
/// separate step whose estimator is named.
///
/// The two component adjustments state their colour encodings and plan their conversions through
/// `Algorithms/Color/ColorTransform.h`; the saturation and colorization operations read the samples
/// as they are, with no encoding. The component and saturation operations clamp every RGB value they
/// compute to `[0, 1]`, and HSI colorization clamps its result too but for a grey one, so these
/// operations are for display-range images rather than linear data above one.
///
/// All four share one image contract: equal width and height on both sides, any layout and stride,
/// exact in-place operation allowed and any partial overlap refused, a pixel the validity view
/// rejects left unwritten in the destination, a selected sample that is not finite refused as
/// `ErrorCode::InvalidData`, and rows written before a cancellation or a failure left as they are.
///
/// `adjustImageLch` over a Float32 image under the all-valid or the finite-only rule with no mask,
/// planar rows, and a source and a destination encoding that resolve to the same transfer - linear or
/// sRGB - and the same white, with no adaptation between them, runs the same steps in Float32 lanes
/// compiled per instruction set, within 1e-4 of full scale of the scalar path - a few millionths over
/// most of the gamut, the rest a channel near zero of a strongly saturated colour under the sRGB
/// curve's slope near black. Every other case - the reference backend, a Float64 image, a mask or a
/// value rule, an interleaved layout, another transfer or an adaptation - takes the scalar path.
/// `Test_EngineImageOps` holds the bound, over one swept scene and three adjustments, on every
/// instruction set a host runs.

#pragma once

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>

namespace an
{

/// The location estimator used to derive a saturation-enhancement background threshold.
///
/// The numeric decision is named rather than hidden behind a speed switch. Both choices use the same
/// standard-deviation estimator and the engine has no separate fast numeric mode.
enum class SaturationBackgroundLocation : std::uint8_t
{
    /// Arithmetic mean of the selected green-channel samples.
    Mean = 0u,

    /// Median of the selected green-channel samples.
    Median = 1u
};

/// Whether a value names a saturation background estimator this build implements.
///
/// @param location the value to inspect.
/// @return true for both supported estimators.
[[nodiscard]] constexpr bool isSaturationBackgroundLocation(SaturationBackgroundLocation location) noexcept
{
    return location == SaturationBackgroundLocation::Mean || location == SaturationBackgroundLocation::Median;
}

/// Hue, saturation and lightness changes in a normalized HSL space.
struct HslAdjustment
{
    /// Signed hue offset in turns. Any finite value is wrapped modulo one turn.
    double hueOffsetTurns = 0.0;

    /// Saturation multiplier, applied before clamping saturation to `[0, 1]`. Must be finite.
    double saturationScale = 1.0;

    /// Signed lightness offset in normalized units, applied before clamping to `[0, 1]`. Must be
    /// finite.
    double lightnessOffset = 0.0;
};

/// Lightness, chroma and hue changes in natural CIE LCh units.
struct LchAdjustment
{
    /// Signed CIE lightness offset, applied before clamping lightness to `[0, 100]`. Must be finite.
    double lightnessOffset = 0.0;

    /// Chroma multiplier, applied before clamping chroma to `[0, 200]`. Must be finite.
    double chromaScale = 1.0;

    /// Signed hue offset in degrees. Any finite value is wrapped modulo 360 degrees.
    double hueOffsetDegrees = 0.0;
};

/// The selective saturation operation's explicit numeric decisions.
struct SaturationEnhancementParameters
{
    /// Amount added to the saturation multiplier. `0` leaves saturation unchanged and `1` doubles
    /// it; the resulting saturation is clamped to `[0, 1]`. Must be finite.
    double coefficient = 0.0;

    /// Normalized lightness threshold in `[0, 1]`. Pixels whose HSL lightness is at or below it are
    /// copied unchanged.
    double background = 0.0;

    /// First selected hue in degrees over `[0, 360]`.
    double hueMinimumDegrees = 0.0;

    /// Last selected hue in degrees over `[0, 360]`. A value below `hueMinimumDegrees` selects the
    /// interval that crosses zero.
    double hueMaximumDegrees = 360.0;
};

/// Which cylindrical model turns an intensity image into color.
enum class ColorizationModel : std::uint8_t
{
    /// Preserve each source sample as HSV value. The result is not clamped, so a sample above one
    /// produces channels above one.
    Hsv = 0u,

    /// Preserve each source sample as HSI intensity. The reconstruction adds a small constant to its
    /// divisor and clamps every channel to `[0, 1]` - except at a saturation of zero, where it returns
    /// the intensity in every channel unclamped.
    Hsi = 1u
};

/// Whether a value names a colorization model this build implements.
///
/// @param model the value to inspect.
/// @return true for both supported models.
[[nodiscard]] constexpr bool isColorizationModel(ColorizationModel model) noexcept
{
    return model == ColorizationModel::Hsv || model == ColorizationModel::Hsi;
}

/// The fixed hue and saturation used by `colorizeImage`.
struct ColorizationParameters
{
    /// Hue in degrees. Any finite value is wrapped modulo 360 degrees.
    double hueDegrees = 0.0;

    /// Saturation in `[0, 1]`.
    double saturation = 1.0;

    /// Whether source intensity is interpreted as HSV value or HSI intensity.
    ColorizationModel model = ColorizationModel::Hsv;
};

/// Counts from one color adjustment or colorization.
struct ColorAdjustmentResult
{
    /// Pixels considered and admitted, and channel samples written.
    KernelRunResult run{};
};

/// Derives the selective-saturation background threshold from one RGB image.
///
/// The green channel (channel one) is reduced under `selection`. The answer is
/// `(location + populationStandardDeviation) * factor`, where `location` is the named arithmetic
/// mean or median. A non-positive factor returns zero without reading the image.
///
/// Thread-safe with respect to the source, which is only read. Uses the execution's admitted
/// threads and context-accounted statistics scratch.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the source's resident representation.
/// @param samples first byte of the RGB image.
/// @param layout the image geometry and strides; it must carry exactly three channels.
/// @param factor finite multiplier applied to the derived location plus deviation.
/// @param location whether the location is the mean or median.
/// @param selection the value validity, optional packed mask and optional region.
/// @return the normalized background threshold; `ErrorCode::NotInitialized` when `execution` holds
///         nothing; `ErrorCode::InvalidArgument` for a factor that is not finite or an estimator
///         outside the enumeration; `ErrorCode::IncompatibleArguments` when the layout does not have
///         three channels; or any failure `computeImageBasicStatistics` or, for the median,
///         `computeImageRobustStatistics` reports for the green channel.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double>
deriveSaturationEnhancementBackground(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                                      const ImageLayout& layout, double factor, SaturationBackgroundLocation location,
                                      const StatisticsSelection& selection = {});

/// Adjusts HSL components and returns encoded RGB.
///
/// `sourceEncoding.schema` may be `Rgb`, for an ordinary image, or `Hsl`, for a source already
/// holding HSL components. `destinationEncoding.schema` must be `Rgb`. The components adjusted are
/// the HSL of the destination encoding, in its primaries and through its transfer function with hue
/// in turns. The operation plans the conversion into them and back rather than inferring one from
/// the sample type, so a source in other primaries or another transfer is converted first. The source
/// and destination may be planar, interleaved or mixed, may have different strides and may alias
/// exactly; every partial overlap is refused.
///
/// The hue offset wraps, saturation and lightness are clamped to `[0, 1]` after the change, and the
/// RGB result is clamped to `[0, 1]`.
///
/// Float32 and Float64 are accepted. Invalid pixels leave the destination untouched. A selected
/// non-finite sample is an `ErrorCode::InvalidData` failure. Cancellation may leave earlier rows
/// written.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType Float32 or Float64, shared by source and destination.
/// @param sourceSamples first byte of the source image.
/// @param sourceLayout source geometry and strides, with three channels.
/// @param destinationSamples first writable byte of the destination.
/// @param destinationLayout destination geometry and strides, equal in extent to the source.
/// @param sourceEncoding RGB or precomputed HSL meaning of the source channels.
/// @param destinationEncoding RGB meaning of the destination channels.
/// @param adjustment finite component changes.
/// @param validity source validity and optional packed mask.
/// @return execution counts; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the operation was stopped;
///         `ErrorCode::UnsupportedBackend` when the operation is constrained to a backend this build
///         lacks; `ErrorCode::IncompatibleArguments` for another sample type, a width or height that
///         differs, a side without three channels, a partial overlap, a schema this adjustment does
///         not accept, or a mask that does not cover the source; `ErrorCode::InvalidArgument` for a
///         null pointer, a layout or pointer that disagrees with the sample type's size or alignment,
///         a mask the validity rule does not use or a missing one it needs, a mask overlapping the
///         destination, an adjustment value that is not finite, or an encoding `checkColorEncoding`
///         refuses; or `ErrorCode::InvalidData` for a selected sample that is not finite.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ColorAdjustmentResult>
adjustImageHsl(const ExecutionContext& execution, SampleType sampleType, const void* sourceSamples,
               const ImageLayout& sourceLayout, void* destinationSamples, const ImageLayout& destinationLayout,
               const ColorEncoding& sourceEncoding, const ColorEncoding& destinationEncoding,
               const HslAdjustment& adjustment, const KernelValidityView& validity = {});

/// Adjusts natural CIE LCh components and returns encoded RGB.
///
/// `sourceEncoding.schema` may be `Rgb`, for an ordinary image, or `Lch`, for a source already
/// holding LCh components. The intermediate is natural CIE units under the destination encoding's
/// white point, whatever packing the source stated. `destinationEncoding.schema` must be `Rgb`.
/// Lightness is clamped to `[0, 100]` and chroma to `[0, 200]` after the change, the hue offset
/// wraps, and the RGB result is clamped to `[0, 1]`. The operation otherwise has the same layout,
/// representation, aliasing, validity, cancellation and failure contract as `adjustImageHsl`.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType Float32 or Float64, shared by source and destination.
/// @param sourceSamples first byte of the source image.
/// @param sourceLayout source geometry and strides, with three channels.
/// @param destinationSamples first writable byte of the destination.
/// @param destinationLayout destination geometry and strides, equal in extent to the source.
/// @param sourceEncoding RGB or precomputed LCh meaning of the source channels.
/// @param destinationEncoding RGB meaning of the destination channels.
/// @param adjustment finite component changes in natural CIE units.
/// @param validity source validity and optional packed mask.
/// @return execution counts; or the failures described by `adjustImageHsl`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ColorAdjustmentResult>
adjustImageLch(const ExecutionContext& execution, SampleType sampleType, const void* sourceSamples,
               const ImageLayout& sourceLayout, void* destinationSamples, const ImageLayout& destinationLayout,
               const ColorEncoding& sourceEncoding, const ColorEncoding& destinationEncoding,
               const LchAdjustment& adjustment, const KernelValidityView& validity = {});

/// Selectively multiplies HSL saturation above a stated background.
///
/// The RGB samples are read as they are, with no colour encoding. Hue is selected on a six-sextant
/// HSL wheel, including a range that crosses zero. A pixel whose lightness
/// is at or below the background is copied unchanged; every other pixel is rebuilt from its HSL
/// components, with its saturation multiplied only when its hue is selected, and clamped to
/// `[0, 1]`. The background is supplied explicitly; `deriveSaturationEnhancementBackground`
/// derives it from a mean or a median location. Source and destination must each carry three
/// channels and otherwise follow `adjustImageHsl`'s representation, layout, aliasing and validity
/// contract.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType Float32 or Float64, shared by source and destination.
/// @param sourceSamples first byte of the RGB source.
/// @param sourceLayout source geometry and strides.
/// @param destinationSamples first writable byte of the RGB destination.
/// @param destinationLayout destination geometry and strides.
/// @param parameters saturation amount, background and hue interval.
/// @param validity source validity and optional packed mask.
/// @return execution counts; `ErrorCode::InvalidArgument` for a parameter that is not finite, a
///         background outside `[0, 1]` or a hue bound outside `[0, 360]`; or the execution, image,
///         validity and non-finite-sample failures `adjustImageHsl` documents, apart from its
///         encoding refusals.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ColorAdjustmentResult>
enhanceImageSaturation(const ExecutionContext& execution, SampleType sampleType, const void* sourceSamples,
                       const ImageLayout& sourceLayout, void* destinationSamples, const ImageLayout& destinationLayout,
                       const SaturationEnhancementParameters& parameters, const KernelValidityView& validity = {});

/// Applies a fixed hue and saturation while preserving source intensity.
///
/// A mono source becomes RGB. A three-channel source is colorized channel by channel: red is taken
/// from colorizing the source red intensity, green from colorizing the
/// source green intensity, and blue from colorizing the source blue intensity. The samples are read
/// as they are, with no colour encoding. Source and destination may use any layout and stride;
/// exact RGB in-place operation is supported and every partial overlap is refused. Float32 and
/// Float64 are accepted.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType Float32 or Float64, shared by source and destination.
/// @param sourceSamples first byte of the one- or three-channel source.
/// @param sourceLayout source geometry and strides.
/// @param destinationSamples first writable byte of the three-channel destination.
/// @param destinationLayout destination geometry and strides.
/// @param parameters fixed hue, saturation and cylindrical model.
/// @param validity source validity and optional packed mask.
/// @return execution counts; `ErrorCode::IncompatibleArguments` when the source has more than three
///         channels or the destination does not have three; `ErrorCode::InvalidArgument` for
///         a two-channel source, a model outside the enumeration, a hue that is not finite or a
///         saturation outside `[0, 1]`; or the execution, image, validity and non-finite-sample
///         failures `adjustImageHsl` documents, apart from its encoding refusals.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ColorAdjustmentResult>
colorizeImage(const ExecutionContext& execution, SampleType sampleType, const void* sourceSamples,
              const ImageLayout& sourceLayout, void* destinationSamples, const ImageLayout& destinationLayout,
              const ColorizationParameters& parameters, const KernelValidityView& validity = {});

/// What the color-adjustment family does to an image it writes.
///
/// The samples and their meaning are restated. The description is conservatively restated because
/// colorization can expand mono to RGB. The astrometric solution, source header cards and output
/// decision are kept, and the validity is preserved because
/// a pixel the validity view rejects is not written.
inline constexpr OperationEffect kColorAdjustmentEffect{.samples = SampleEffect::Writes,
                                                        .description = DescriptionEffect::Restates,
                                                        .semantics = SemanticEffect::Restates,
                                                        .validity =
                                                            ValidityTransition{.policy = ValidityPolicy::Preserve}};

} // namespace an
