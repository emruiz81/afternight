// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Perceptual-lightness adaptive histogram equalization for RGB images.
///
/// Contrast-limited adaptive histogram equalization (CLAHE) applied to each RGB channel separately
/// shifts hue wherever the three channels are stretched by different amounts. This header composes
/// the colour-preserving form instead: the image is converted into natural-unit CIE Lab under its
/// stated encoding's white point, only the lightness plane `L` is equalized, over the fixed range
/// `[0, 100]`, and the unchanged `a` and `b` planes are converted back with `L` to the same RGB
/// encoding. The RGB encoding is stated by the caller and never inferred from a channel count.
///
/// `planPerceptualAdaptiveHistogramEqualization` checks the encoding and the histogram request once
/// and returns a reusable `PerceptualAdaptiveHistogramPlan`; `equalizeImagePerceptualAdaptiveHistogram`
/// runs it. The colour stages are `planColorConversion`'s and the equalization is
/// `equalizeImageAdaptiveHistogram`'s, so their numeric rules - the tile-centre placement included -
/// are stated once, where those operations are declared.

#pragma once

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>

namespace an
{

/// Configuration for CLAHE over CIE Lab lightness while retaining both opponent channels.
struct PerceptualAdaptiveHistogramRequest
{
    /// The source and destination RGB encoding. It must have exactly `ChannelSchema::Rgb`.
    ColorEncoding rgbEncoding{.transfer = ColorTransferFunction::Srgb};

    /// Number of tile columns, at least one.
    std::uint32_t tileColumns = 8u;

    /// Number of tile rows, at least one.
    std::uint32_t tileRows = 8u;

    /// Histogram resolution, from two through 65536.
    std::uint32_t binCount = 256u;

    /// Maximum average-bin multiple before excess counts are redistributed; finite and positive.
    double clipLimit = 2.0;

    /// How the equalized Lab lightness is narrowed before the inverse color transform.
    SpatialOutputPolicy output{};
};

/// A checked RGB-to-Lab, lightness-CLAHE and Lab-to-RGB composition.
class PerceptualAdaptiveHistogramPlan
{
public:
    /// Records pieces validated by `planPerceptualAdaptiveHistogramEqualization`.
    ///
    /// @param request the source encoding and histogram configuration.
    /// @param toLab checked conversion into natural-unit Lab under the source white point.
    /// @param fromLab checked conversion back to the exact source encoding.
    AFTERNIGHTENGINE_API
    PerceptualAdaptiveHistogramPlan(const PerceptualAdaptiveHistogramRequest& request, const ColorConversionPlan& toLab,
                                    const ColorConversionPlan& fromLab) noexcept;

    /// The checked request.
    ///
    /// @return the source encoding and histogram configuration.
    [[nodiscard]] AFTERNIGHTENGINE_API const PerceptualAdaptiveHistogramRequest& request() const noexcept;

    /// Conversion into natural-unit Lab.
    ///
    /// @return the checked forward color plan.
    [[nodiscard]] AFTERNIGHTENGINE_API const ColorConversionPlan& toLab() const noexcept;

    /// Conversion from natural-unit Lab to the original RGB encoding.
    ///
    /// @return the checked inverse color plan.
    [[nodiscard]] AFTERNIGHTENGINE_API const ColorConversionPlan& fromLab() const noexcept;

private:
    /// Source encoding and histogram configuration.
    PerceptualAdaptiveHistogramRequest m_request{};

    /// RGB to natural-unit Lab conversion.
    ColorConversionPlan m_toLab;

    /// Natural-unit Lab to RGB conversion.
    ColorConversionPlan m_fromLab;
};

/// Counts and plan facts from perceptual adaptive histogram equalization.
struct PerceptualAdaptiveHistogramResult
{
    /// Common spatial counts, layout path and peak context-owned working memory.
    SpatialFilterResult filter{};

    /// Histogram tiles processed over the single Lab lightness plane.
    std::uint64_t tileCount = 0u;

    /// Bins in every lightness histogram.
    std::uint32_t binCount = 0u;
};

/// Plans colour-preserving adaptive histogram equalization of an RGB image.
///
/// The source encoding is explicit rather than inferred from three channels. Planning converts to
/// natural-unit CIE Lab under that encoding's white point, configures CLAHE over L in `[0, 100]`,
/// and converts back to the same RGB encoding. The `a` and `b` planes are never filtered.
///
/// Thread-safe. Allocates only a failure description.
///
/// @param request RGB encoding, tile grid, histogram resolution, clip limit and output policy.
/// @return a reusable plan; `ErrorCode::IncompatibleArguments` when `request.rgbEncoding` is not a
///         valid encoding with `ChannelSchema::Rgb`; `ErrorCode::OutOfRange` for a zero tile count or
///         a bin count outside `[2, 65536]`; or `ErrorCode::InvalidArgument` for a clip limit that is
///         not finite and positive, or an output policy `planAdaptiveHistogramEqualization` refuses.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PerceptualAdaptiveHistogramPlan>
planPerceptualAdaptiveHistogramEqualization(const PerceptualAdaptiveHistogramRequest& request);

/// Equalizes CIE Lab lightness and converts the result back to the stated RGB encoding.
///
/// Float32 and Float64 source and destination samples are accepted. The layouts may be planar or
/// interleaved and must describe the same three-channel extent; exact in-place operation is safe
/// because the complete RGB source is converted into context-owned Lab memory before destination
/// writes begin. A per-channel explicit validity mask is combined with logical AND for lightness,
/// while a shared mask is borrowed directly. An invalid pixel's lightness is not equalized: it keeps
/// the source's `L`, so the pixel comes back as its source colour to within the rounding of the round
/// trip through the working representation.
///
/// The working set is four sample planes plus any combined validity plane and the ordinary CLAHE
/// histograms. Every byte is charged to the operation's engine context and released on every exit.
/// Cancellation can leave the destination partially converted during the final Lab-to-RGB pass.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType Float32 or Float64, used by both images and all working planes.
/// @param source first byte of the RGB source.
/// @param sourceLayout where the source samples sit.
/// @param destination first byte of the RGB destination, which may equal `source` exactly.
/// @param destinationLayout where destination samples sit.
/// @param plan the checked color and histogram composition.
/// @param selection which source pixels mean something.
/// @return CLAHE counts with `filter.workingBytes` raised to the complete peak working-byte count;
///         `ErrorCode::NotInitialized` when `execution` holds nothing; `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the operation was stopped; `ErrorCode::InvalidArgument`
///         for another sample type, a null pointer, an explicit-mask rule without packed bits or
///         packed bits under another rule; `ErrorCode::IncompatibleArguments` when a layout's
///         element size is not the sample type's, the two extents differ, the image does not have
///         three channels, or the mask does not cover the image with one plane or three;
///         `ErrorCode::LimitExceeded` when the working layout or its byte count is not
///         representable; `ErrorCode::MemoryBudgetExceeded` or `ErrorCode::AllocationFailed` when
///         the working memory is refused; or any failure the re-checked plan, `convertImageColor`
///         or `equalizeImageAdaptiveHistogram` reports.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PerceptualAdaptiveHistogramResult> equalizeImagePerceptualAdaptiveHistogram(
    const ExecutionContext& execution, SampleType sampleType, const void* source, const ImageLayout& sourceLayout,
    void* destination, const ImageLayout& destinationLayout, const PerceptualAdaptiveHistogramPlan& plan,
    const SpatialSelection& selection = {});

} // namespace an
