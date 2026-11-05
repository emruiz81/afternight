// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Measuring the translation between two frames of the same sky.
///
/// Two images that differ by a pure shift have the same spectrum up to a phase ramp, and dividing
/// their cross-spectrum by its own magnitude leaves only that ramp - whose inverse transform is a
/// single peak standing at the displacement. That is the whole algorithm, and everything this
/// header states exists to make its inputs and its answer mean one thing.
///
/// **The input domain is stated, not repaired.** A measurement takes two single-channel images of
/// one extent, in any of the eight resident representations, at least eight pixels on a side. A
/// colour frame and a mismatched pair are each refused by name rather than collapsed to grey or
/// cropped to their overlap, and the caller states the conversion or crop it meant - which for
/// an unresolved mosaic means demosaicing or splitting first, since a mosaic's single channel
/// interleaves four signals no translation relates. The transform runs at the image's own size - no
/// hidden padding to a convenient length - so the measurement is of the frames the caller handed
/// in.
///
/// **The normalization is stated.** The spectrum whitening divides every bin by its own magnitude, so
/// a global scale on either input cancels exactly, and dividing a frame by its standard deviation
/// would change nothing. What does change the answer is the mean and the window: a frame's mean is a
/// zero-frequency spike that competes with the correlation peak, and a window decides how much the
/// frame's edges masquerade as signal. Both are explicit in the request, and by default the mean is
/// subtracted and then a Hann window applied.
///
/// **The answer is the translation that maps the target's content back onto the reference's.** A
/// target whose stars sit three pixels right of the reference's measures `translationX = -3`:
/// applying the reported translation to the target aligns it with the reference, which is the number
/// a registration pipeline feeds its warp: it is the translation column of a `RegistrationTransform`
/// whose source is the target. Axes run x right and y down. A translation is a difference of two
/// positions, so it is the same number under the sampling kernels' integer pixel centres and the
/// registration domain's half-pixel ones. The sub-pixel refinement is the weighted centroid of the
/// five-by-five neighbourhood around the correlation peak, which is the estimator OpenCV's
/// `phaseCorrelate` uses and no sharper: the surface's negative sidelobes cancel most of a peak
/// split between two bins, so a true half-pixel shift reads close to a whole pixel. `response`
/// reports the fraction of the correlation surface's energy standing in that neighbourhood: near one
/// for two frames that really are one shift apart, near zero for two frames that share nothing, and
/// exactly zero for a pair with no structure at all - a constant frame's whitened spectrum is empty,
/// which is reported as a response of zero rather than as a non-finite translation. Nothing in this
/// operation decides whether a response is good enough; that threshold belongs to the caller that
/// knows what the frames are.
///
/// The transform itself is FFTW's, in single precision, reached through the checked descriptions in
/// `Interop/FftwAdapter.h`; the pointwise stages run on the operation's threads and the working
/// buffers come from the engine context, sized by the frame and released with the call.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named here rather than included, because every entry point below only borrows one for the
/// duration of a call.
class ExecutionContext;

/// The smallest frame side a phase correlation accepts, in pixels.
///
/// Below this the five-by-five centroid neighbourhood is most of the correlation surface and the
/// answer is dominated by its own refinement window.
inline constexpr std::uint32_t kMinPhaseCorrelationExtent = 8u;

/// Which window shapes the frames before they are transformed.
enum class PhaseCorrelationWindow : std::uint8_t
{
    /// No window: the frames are transformed as they stand, and the frame edge's wrap-around
    /// discontinuity is part of the measured signal. The right choice for frames that are
    /// genuinely periodic - a test's cyclic shift - and for nothing else.
    None = 0u,

    /// The Hann raised-cosine window, tapering each frame to zero at its edges. The default.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Hann = 1u
};

/// Whether a value names a window this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param window the value to inspect.
/// @return true for every implemented window.
[[nodiscard]] constexpr bool isPhaseCorrelationWindow(PhaseCorrelationWindow window) noexcept
{
    return static_cast<std::uint8_t>(window) <= static_cast<std::uint8_t>(PhaseCorrelationWindow::Hann);
}

/// The window's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param window the window to name. A value outside the enumeration is named
///        `"UnknownPhaseCorrelationWindow"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* phaseCorrelationWindowName(PhaseCorrelationWindow window) noexcept;

/// What a caller asks a phase correlation for.
struct PhaseCorrelationRequest
{
    /// Whether each frame's mean is subtracted before the window is applied.
    ///
    /// A frame's mean is a zero-frequency spike that the whitening hands the same weight as every
    /// real feature; leaving it in biases the measurement towards no shift. On by default, and taken
    /// before the window, which applied first would put the mean back into the edges.
    bool subtractMean = true;

    /// Which window shapes the frames before they are transformed.
    PhaseCorrelationWindow window = PhaseCorrelationWindow::Hann;
};

/// What one phase correlation measured.
struct PhaseCorrelationResult
{
    /// The horizontal translation, in pixels, that maps the target's content onto the reference's.
    double translationX = 0.0;

    /// The vertical translation, in pixels, under the same convention.
    double translationY = 0.0;

    /// The fraction of the correlation surface's energy in the centroid neighbourhood, within
    /// roughly zero and one. Near one when the two frames really are one translation apart; zero
    /// when the whitened cross-spectrum held nothing at all.
    double response = 0.0;

    /// The correlation surface's largest sample, as the same fraction. The single-bin half of
    /// `response`, which spreads over neighbouring bins for a sub-pixel shift.
    double peakValue = 0.0;

    /// The column of the integer peak in the centred correlation surface, where a peak at the
    /// surface's centre column, `width / 2` rounded down, is a translation of zero.
    std::uint32_t peakX = 0u;

    /// The row of the integer peak in the centred correlation surface, whose centre row is
    /// `height / 2` rounded down.
    std::uint32_t peakY = 0u;

    /// Context memory held at once for the two transform buffers.
    std::uint64_t workingBytes = 0u;

    /// Workers the pointwise stages could actually use, never above the operation's admitted
    /// concurrency. The transforms themselves run on the calling thread.
    std::uint32_t plannedWorkers = 0u;

    /// The width the transforms and the surface arithmetic ran at, which this operation always
    /// reports as `ComputePrecision::Float32`.
    ComputePrecision precision = ComputePrecision::Float32;
};

/// Measures the translation between two single-channel images of one extent.
///
/// Each frame is widened once into a Float32 working buffer, optionally has its mean subtracted
/// and a window applied, and is transformed; the cross-spectrum is whitened bin by bin - a bin
/// whose magnitude is zero contributes nothing rather than a division by it - and inverted; and
/// the peak of the resulting correlation surface, refined by the weighted centroid of its
/// five-by-five neighbourhood, is reported as the translation, with `response` saying how much of
/// the surface's energy stood in that neighbourhood.
///
/// The two frames are read where they lie, in any of the eight resident representations and any
/// arrangement whose pixels hold one sample. Every sample participates - a transform has no way to
/// leave one out - so a sample that is not a number is refused rather than transformed into
/// everything.
///
/// Thread-safe with respect to `execution` and both images, all of which are only read. The
/// pointwise stages run on the threads the operation was admitted and check cancellation at chunk
/// boundaries; each of the three transforms runs on the calling thread and is not interruptible.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param reference where the reference image's samples start. Must not be null.
/// @param referenceLayout where they sit. Its element size must be that representation's.
/// @param target where the target image's samples start. Must not be null.
/// @param targetLayout where they sit. Its extent must be the reference's.
/// @param request the normalization and window.
/// @return the measured translation and its quality; `ErrorCode::NotInitialized` when `execution`
///         holds nothing; `ErrorCode::InvalidArgument` for a null buffer, a representation this
///         build does not know, a layout that does not describe it, a multi-channel frame, or an
///         unknown window; `ErrorCode::IncompatibleArguments` when the two extents
///         differ; `ErrorCode::OutOfRange` for a side below `kMinPhaseCorrelationExtent`, carrying
///         the side and the floor; `ErrorCode::InvalidData` for a sample that is not a number;
///         `ErrorCode::ExternalFailure` when the transform library refuses a plan; or an
///         execution, memory-budget or cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PhaseCorrelationResult>
computeImagePhaseCorrelation(const ExecutionContext& execution, SampleType sampleType, const void* reference,
                             const ImageLayout& referenceLayout, const void* target, const ImageLayout& targetLayout,
                             const PhaseCorrelationRequest& request = {});

/// Measures the translation between two checked typed image views.
///
/// The typed spelling of `computeImagePhaseCorrelation`, which owns every check and every
/// computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param reference the checked read-only reference view.
/// @param target the checked read-only target view, of the reference's extent.
/// @param request the normalization and window.
/// @return whatever `computeImagePhaseCorrelation` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<PhaseCorrelationResult>
computePhaseCorrelation(const ExecutionContext& execution, const ImageView<const SampleT>& reference,
                        const ImageView<const SampleT>& target, const PhaseCorrelationRequest& request = {})
{
    return computeImagePhaseCorrelation(execution, kSampleTypeOf<SampleT>, reference.data(), reference.layout(),
                                        target.data(), target.layout(), request);
}

} // namespace an
