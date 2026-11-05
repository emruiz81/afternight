// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the colour calibration tools share: Background Neutralization, Color Calibration, Linear Match and SCNR. Each
/// is a measurement of a reference image - or none - and one engine operation over the image a run changes, in
/// fractions of full scale: an unsigned integer of its largest value and a signed one counted from its smallest,
/// worked on in Float32 - Float64 for Float64 and the two 32-bit integers - and brought back to the image's own
/// representation to the nearest value.
///
/// **The measurements are compositions of the engine's statistics.** A neutralization's level is each channel's median
/// under the dialog's limits, a stated closed range; a white reference's level is each channel's mean under its limits,
/// and a background reference's its median over every sample of its rectangle - the limits decide only which samples
/// the background mask selects. A linear match is the normalization domain's scaling match: the ratio of the two
/// images' robust scales and the offset that moves the target's location onto the reference's, measured over the
/// positions both images share when they are one size and over each image on its own when they are not. A measurement
/// that finds no sample to measure refuses by name rather than offset or scale by statistics of nothing.
///
/// **The applies are the engine's.** Background neutralization and colour calibration write `applyColorCalibration`'s
/// gains and offsets, held to the unit interval; SCNR is `reduceImageChromaticNoise`; the linear match
/// `applyGlobalNormalization`, unclamped. A result keeps the image's description, validity, solution and cards with a
/// lineage entry naming the engine operation and its numbers; the neutralization, the colour calibration and the
/// linear match keep its meaning, and SCNR records its result nonlinear under `scnr`.
///
/// **Parameters are read on the GUI thread.** A window turns its dialog into a value - the limits, the references it
/// names, the mask the view weighs the result by - before the run starts; a worker reads none of the window. A
/// reference is the asset a view showed when Apply was pressed, read under a lease of the run's own, or the image the
/// run changes itself.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ProcessExecution.h"
#include "session/ExecutionScope.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Color/ColorChannels.h>
#include <AfterNightEngine/Algorithms/NoiseReduction/NoiseReduction.h>
#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QRect>
#include <QString>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class ImageViewControl;

namespace an::ui::processes
{

// ---- What a window snapshots when Apply is pressed.

/// The text a reference selector names the image the run changes by.
inline constexpr const char* kTargetImageReference = "<--TARGET IMAGE-->";

/// The mask a view weighs a process's result by: the mask view's image while the view's mask is enabled, and none
/// otherwise.
struct ViewMaskSnapshot
{
    /// The mask's image, or null for none.
    std::shared_ptr<const an::consumer::ImageAsset> asset;

    /// Whether the mask weighs the source rather than the result.
    bool inverted = false;
};

/// The mask a view weighs a process's result by now. GUI thread.
///
/// @param view the view.
/// @return the snapshot; no asset when the view has no enabled mask.
[[nodiscard]] AFTERNIGHTPROCESSES_API ViewMaskSnapshot viewMaskOf(const ImageViewControl& view);

/// Whether a view shows a three-channel RGB image, which background neutralization, colour calibration and SCNR read.
///
/// @param view the view, or null.
/// @return false for a null view, a view that holds nothing and any other image.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool showsRgbImage(const ImageViewControl* view);

/// The number of channels of the image a view shows; zero for a null view or one that holds nothing.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::uint32_t channelCountOf(const ImageViewControl* view);

/// The rectangle of an image a dialog names, as a statistics region: nothing for the whole image.
///
/// @param rectangle the rectangle, in the image's pixels.
/// @param described what the image is.
/// @return nothing when the rectangle is the whole image; the region otherwise. The caller has checked it lies inside.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::StatisticsRegion>
regionOf(const QRect& rectangle, const an::ImageDescriptor& described);

/// Whether a rectangle a dialog names lies wholly inside an image and is not empty.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool rectangleInside(const QRect& rectangle,
                                                           const an::ImageDescriptor& described);

/// An image a run measures beside the one it changes.
struct ToolReference
{
    /// The asset the reference view showed when Apply was pressed; null for the image the run changes.
    std::shared_ptr<const an::consumer::ImageAsset> asset;

    /// The rectangle measured, or nothing for the whole image.
    std::optional<an::StatisticsRegion> region;
};

/// A closed interval of fractions of full scale a measurement keeps a sample within.
struct FractionRange
{
    /// The smallest fraction kept.
    double lowest = 0.0;

    /// The largest fraction kept.
    double highest = 1.0;
};

// ---- Reading a reference on a run's worker.

/// A reference read for a run: the image the run changes, or the reference's asset under a read lease of the run's own.
/// Move-only; the lease lives as long as it does.
class AFTERNIGHTPROCESSES_API ReferenceRead
{
public:
    /// Reads a reference.
    ///
    /// @param scope the run's admitted execution, whose stop ends a wait for the lease.
    /// @param reference the reference the window snapshotted.
    /// @param target what the run changes, which a reference without an asset is.
    /// @return the reference; or what taking the lease refused.
    [[nodiscard]] static an::Result<ReferenceRead>
    read(const an::ui::ExecutionScope& scope, const ToolReference& reference, const an::ui::ProcessSource& target);

    ReferenceRead(ReferenceRead&&) noexcept = default;
    ReferenceRead& operator=(ReferenceRead&&) noexcept = default;
    ReferenceRead(const ReferenceRead&) = delete;
    ReferenceRead& operator=(const ReferenceRead&) = delete;
    ~ReferenceRead() = default;

    /// The reference, as a process source.
    [[nodiscard]] an::ui::ProcessSource source() const noexcept;

private:
    explicit ReferenceRead(const an::ui::ProcessSource& target) noexcept;
    explicit ReferenceRead(an::consumer::AssetReadLease&& lease) noexcept;

    std::optional<an::consumer::AssetReadLease> m_lease;
    std::optional<an::ui::ProcessSource> m_target;
};

/// A measurement a run makes once and every frame of a sequence then shares: the references do not change while a run
/// reads the frames. Thread-safe.
template <typename ValueT> class MeasuredOnce
{
public:
    /// The measurement, made by `measure` the first time it is asked for and kept; a refusal is not kept, so the next
    /// frame measures again and refuses again.
    ///
    /// @param measure what makes the measurement.
    /// @return the measurement, or what `measure` refused.
    template <typename MeasureT> [[nodiscard]] an::Result<ValueT> get(MeasureT&& measure)
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_value.has_value())
        {
            an::Result<ValueT> measured = std::forward<MeasureT>(measure)();
            if (!measured.hasValue())
            {
                return measured.status();
            }
            m_value = std::move(*measured);
        }
        return *m_value;
    }

private:
    std::mutex m_mutex;
    std::optional<ValueT> m_value;
};

// ---- Measurements.

/// Each colour channel's median of a reference's samples within a range: the samples its validity admits, inside its
/// rectangle.
///
/// @param scope the run's admitted execution.
/// @param reference the reference; three-channel RGB.
/// @param region the rectangle measured, or nothing for the whole image.
/// @param range the fractions kept, or nothing for every finite one.
/// @param what what the reference is, for a refusal: "background reference".
/// @return the three medians, as fractions of full scale; `ErrorCode::EmptySelection` naming the channel when a channel
///         keeps no sample; or what the working copy or the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ColorChannelValues>
measureChannelMedians(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& reference,
                      const std::optional<an::StatisticsRegion>& region, const std::optional<FractionRange>& range,
                      std::string_view what);

/// Each colour channel's mean of a reference's samples within a range, as `measureChannelMedians` selects them.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ColorChannelValues>
measureChannelMeans(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& reference,
                    const std::optional<an::StatisticsRegion>& region, const std::optional<FractionRange>& range,
                    std::string_view what);

/// The mask of the samples a range selects in a reference: a grey image of the reference's size and representation, of
/// the run's session, at full scale where every colour channel's sample inside the rectangle is valid and within the
/// range, and at the bottom of the representation's range everywhere else.
///
/// @param scope the run's admitted execution.
/// @param reference the reference; three-channel RGB.
/// @param region the rectangle the mask selects in, or nothing for the whole image.
/// @param range the fractions selected.
/// @return the mask; or what the working copy, the allocation or the adoption refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
selectedSamplesMask(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& reference,
                    const std::optional<an::StatisticsRegion>& region, const FractionRange& range);

/// What a background neutralization measured: each channel's background level and the level all three are brought to.
struct NeutralizationMeasurement
{
    /// Each channel's median within the limits, as a fraction of full scale.
    an::ColorChannelValues levels{};

    /// The mean of the three levels.
    double pedestal = 0.0;

    /// The offsets that bring each channel's level to the pedestal.
    an::ColorChannelOffsets offsets{};
};

/// Measures a background neutralization's reference.
///
/// @param scope the run's admitted execution.
/// @param reference the background reference.
/// @param region its rectangle, or nothing for the whole image.
/// @param range the dialog's limits.
/// @return the measurement; or what `measureChannelMedians` refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<NeutralizationMeasurement>
measureNeutralization(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& reference,
                      const std::optional<an::StatisticsRegion>& region, const FractionRange& range);

/// What a colour calibration measures, as the dialog states it.
struct ColorCalibrationRequest
{
    /// The manual white balance, or nothing to measure the white reference.
    std::optional<an::ColorChannelGains> manualWhite;

    /// The white reference's rectangle, or nothing for the whole image.
    std::optional<an::StatisticsRegion> whiteRegion;

    /// The white reference's limits.
    FractionRange whiteRange{.lowest = 0.0, .highest = 0.9};

    /// The background reference's rectangle, or nothing for the whole image.
    std::optional<an::StatisticsRegion> backgroundRegion;
};

/// What a colour calibration measured and the gains and offsets it applies.
struct ColorCalibrationMeasurement
{
    /// Each channel's white level - the white reference's mean within its limits - or the manual white balance.
    an::ColorChannelValues white{};

    /// Each channel's background level: the background reference's median.
    an::ColorChannelValues background{};

    /// The gains: each channel's white above its background, normalized to the strongest channel's; or the manual
    /// white balance.
    an::ColorChannelGains gains{1.0, 1.0, 1.0};

    /// The offsets that keep every channel's background where it was under its gain.
    an::ColorChannelOffsets offsets{};
};

/// Measures a colour calibration over the two references: each channel's white above its background, normalized to
/// the strongest channel.
///
/// @param scope the run's admitted execution.
/// @param white the white reference; ignored, and may be null, under a manual white balance.
/// @param background the background reference.
/// @param request the dialog's rectangles, limits and manual white balance.
/// @return the measurement; `ErrorCode::EmptySelection` when a white channel keeps no sample;
///         `ErrorCode::InvalidData` naming the channel when a channel's white sits on its background, which no gain
///         balances; `ErrorCode::InvalidArgument` for a manual white that is not finite and positive; or what the
///         measurements refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<ColorCalibrationMeasurement>
measureColorCalibration(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource* white,
                        const an::ui::ProcessSource& background, const ColorCalibrationRequest& request);

// ---- The applies.

/// What one gains-and-offsets apply writes and records.
struct ChannelGainsApply
{
    /// The per-channel multipliers. Finite and positive.
    an::ColorChannelGains gains{1.0, 1.0, 1.0};

    /// The per-channel addends, as fractions of full scale.
    an::ColorChannelOffsets offsets{};

    /// What the lineage records the change as: "background neutralization".
    std::string lineage;
};

/// Writes `sample * gain + offset` for every valid sample of a three-channel RGB image, as a fraction of full scale
/// held to the unit interval - `applyColorCalibration` - and brings it back to the image's representation, weighed by
/// the view's mask when one is given.
///
/// @param scope the run's admitted execution.
/// @param source what the run changes; three-channel RGB.
/// @param apply the gains, the offsets and the lineage's words.
/// @param mask the view's mask, or none.
/// @return the result, a new asset of the run's session; or what the working image, the apply, the mask's lease, the
///         blend or the adoption refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
applyChannelGains(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
                  const ChannelGainsApply& apply, const ViewMaskSnapshot& mask);

/// The protection methods the SCNR dialog offers, in its order: "Average Neutral", "Maximum Neutral", "Maximum Mask",
/// "Additive Mask".
[[nodiscard]] AFTERNIGHTPROCESSES_API const std::vector<QString>& scnrProtectionLabels();

/// The method a protection's text names, or nothing for a text the dialog does not offer.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::ChromaticNoiseMethod> scnrMethodFromLabel(const QString& label);

/// What an SCNR run asks for.
struct GreenSuppression
{
    /// The protection method.
    an::ChromaticNoiseMethod method = an::ChromaticNoiseMethod::AverageNeutral;

    /// How strongly the two mask methods suppress green, within the unit interval.
    double amount = 1.0;

    /// Whether the perceptual lightness the suppression removed is put back.
    bool preserveLightness = true;
};

/// Suppresses the green of a three-channel RGB image - `reduceImageChromaticNoise` over its samples as fractions of
/// full scale, read as the process framework reads a colour image, linear when the image says so and sRGB-encoded
/// otherwise - recorded nonlinear display RGB under `scnr` on its processing-state cards, and weighed by the view's
/// mask when one is given.
///
/// @param scope the run's admitted execution.
/// @param source what the run changes; three-channel RGB.
/// @param suppression the method, the amount and the lightness.
/// @param mask the view's mask, or none.
/// @return the result, a new asset of the run's session; or what the working copies, the plan, the engine, the mask's
///         lease, the blend or the adoption refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
suppressGreen(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
              const GreenSuppression& suppression, const ViewMaskSnapshot& mask);

/// The scaling match of an image to a reference, measured as the linear match measures it: each image's samples within
/// the limits and not zero - over the positions both share when they are one size, where only the reference is held to
/// the limits, and over each image on its own when they are not.
///
/// @param scope the run's admitted execution.
/// @param source what the run changes.
/// @param reference the reference, of the source's channel count.
/// @param range the dialog's limits.
/// @return the match; `ErrorCode::IncompatibleArguments` for another channel count; `ErrorCode::Unsupported` for more
///         channels than a normalization reads; `ErrorCode::EmptySelection` when a channel keeps no sample; or what the
///         working copies or the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::FrameNormalization>
measureLinearMatch(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
                   const an::ui::ProcessSource& reference, const FractionRange& range);

/// Matches an image to a reference linearly: `measureLinearMatch`, then `applyGlobalNormalization` over every valid
/// sample as a fraction of full scale, unclamped, brought back to the image's representation.
///
/// @param scope the run's admitted execution.
/// @param source what the run changes.
/// @param reference the reference.
/// @param range the dialog's limits.
/// @return the result, a new asset of the run's session; or what the measurement, the apply or the adoption refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
matchLinearly(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
              const an::ui::ProcessSource& reference, const FractionRange& range);

/// Weighs a run's result by a view's mask: the framework's blend, the result where the mask is full and the source
/// where it is empty, inverted when the snapshot says so.
///
/// @param scope the run's admitted execution.
/// @param source what the run read.
/// @param result what the run made, of the source's size and representation.
/// @param mask the view's mask; the result is returned as it is when the snapshot holds none.
/// @return the weighed result; or what the mask's lease, the copy or the blend refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
weighByViewMask(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
                an::consumer::ImageAsset&& result, const ViewMaskSnapshot& mask);

} // namespace an::ui::processes
