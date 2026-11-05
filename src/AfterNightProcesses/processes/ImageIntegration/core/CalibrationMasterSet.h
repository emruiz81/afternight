// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// The calibration masters one workflow run reads, each once, and what calibrates a light with them.
///
/// A run names the same bias, dark and flat masters for many lights. `CalibrationMasterSet` reads each master the
/// first time a light names it - through the run's codec registry, as Float32 fractions of its representation's full
/// scale - and keeps it for the run; it also keeps, per pair, the dark with the bias already subtracted that a CCD
/// calibration applies, and per dark the cold and hot pixels a cosmetic correction replaces. `prepare` answers one
/// light's calibration: which masters it subtracts and divides, the request the engine's calibration runs with, and the
/// cosmetic maps. `calibrateLight` runs it over a light read the same way, and counts what it fell back to, which
/// `fallbackWarnings` says once per calibration for the stage that calibrated the lights.
///
/// **The arithmetic is the engine's calibration** (`Algorithms/Calibration/ImageCalibration.h`), in the workflow's
/// units: every frame a fraction of its representation's full scale, the pedestal stated in sixteen-bit codes and
/// divided by 65535, the flat divided after normalizing it by its central mean when flats are auto-levelled. A dark
/// in standard mode already holds the bias, so the bias is not subtracted beside it; in CCD mode the dark is taken
/// without its bias, which is subtracted from it once and then from the light.
///
/// Opened and used on one worker; a set is not shared between threads. It holds images of the session's engine, so it
/// is let go before the session is.

#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>

namespace an
{
class CodecRegistry;
} // namespace an

namespace an::consumer
{
class OperationScope;
} // namespace an::consumer

namespace core
{

/// The codes a sixteen-bit sample spans, by which a stated pedestal is divided into a fraction of full scale.
inline constexpr double kPedestalCodesPerUnit = 65535.0;

/// Reads an image file whole under an admitted operation, through a registry when one is named and through the
/// engine context's own otherwise, into an image whose every channel is a plane of its own.
///
/// @param scope the admitted operation.
/// @param registry the run's registry, or null for the engine context's.
/// @param path the file.
/// @return the image; or what the codec refused, a stop among them.
[[nodiscard]] an::Result<an::consumer::AnyImage>
readWorkflowImage(const an::consumer::OperationScope& scope, const an::CodecRegistry* registry, const QString& path);

/// A copy of an image in Float32 with every sample a fraction of its representation's full scale - an integer over its
/// largest value, a floating sample as it is - described as normalized linear samples: how the workflow widens every
/// frame it calibrates or stacks.
///
/// @param scope the admitted operation.
/// @param image the image.
/// @return the copy; `ErrorCode::IncompatibleArguments` when the image has no such description; or what the
///         conversion refused.
[[nodiscard]] an::Result<an::Image<float>> workingFractions(const an::consumer::OperationScope& scope,
                                                            const an::consumer::AnyImage& image);

/// What one master set did, for the tests and the stage's log.
struct CalibrationMasterSetDiagnostics
{
    /// Masters read from their files.
    std::size_t masterReads = 0u;
    /// Masters a light named that were already read.
    std::size_t masterCacheHits = 0u;
    /// Darks the bias was subtracted from.
    std::size_t processedDarkSubtractions = 0u;
    /// Lights whose dark without its bias was already made.
    std::size_t processedDarkCacheHits = 0u;
    /// Darks whose cold and hot pixels were mapped.
    std::size_t cosmeticMaps = 0u;
};

/// The calibration plans one `LightCalibration` prepared, one per light description; defined beside `calibrateLight`.
struct CalibrationPlanCache;

/// What the lights calibrated with one `LightCalibration` fell back to, counted as they are calibrated; defined beside
/// `calibrateLight`.
struct CalibrationFallbackRecord;

/// One light's calibration as a master set prepared it. The masters and maps belong to the set.
struct LightCalibration
{
    /// The bias subtracted from the light, or null.
    const an::Image<float>* bias = nullptr;
    /// The dark subtracted from the light - without its bias in CCD mode - or null.
    const an::Image<float>* dark = nullptr;
    /// The flat the light is divided by, or null.
    const an::Image<float>* flat = nullptr;
    /// The pedestal, dark scaling and flat normalization.
    an::CalibrationRequest request;
    /// The pixels the dark marks cold, when a cosmetic correction replaces them; null otherwise.
    const an::ValidityMask* coldMap = nullptr;
    /// The pixels the dark marks hot, when a cosmetic correction replaces them; null otherwise.
    const an::ValidityMask* hotMap = nullptr;
    /// How many pixels the dark marks cold and hot.
    std::uint64_t coldPixels = 0u;
    std::uint64_t hotPixels = 0u;
    /// Whether a cosmetic correction was asked for with no dark to find the pixels in, so none is made.
    bool cosmeticWithoutDark = false;
    /// The plans prepared for this calibration, one per light description, kept when preparing one reads no light's
    /// samples - under every dark rule but the optimized scale, a plan holds only what the masters and the description
    /// decide. Shared by every light naming this calibration, on any thread; null leaves each light to prepare its own.
    std::shared_ptr<CalibrationPlanCache> plans;
    /// What the lights calibrated with this calibration fell back to, counted by `calibrateLight` on any thread and
    /// said by the stage that calibrated them (`CalibrationMasterSet::fallbackWarnings`); null counts nothing.
    std::shared_ptr<CalibrationFallbackRecord> fallbacks;

    /// Whether the calibration changes anything: a master or a pedestal.
    [[nodiscard]] bool calibrates() const noexcept
    {
        return bias != nullptr || dark != nullptr || flat != nullptr || request.pedestal > 0.0;
    }
};

/// What calibrating one light made.
struct CalibratedLight
{
    /// The calibrated light, Float32 fractions with the calibration's transition stated.
    an::Image<float> image;
    /// The dark scale the light was calibrated with, when a dark was subtracted.
    std::optional<double> darkScale;
    /// Whether the dark scale was to be optimized and could not be - a colour light, one smaller than the central
    /// square, or a dark with no variation there to fit against - so the fixed scale was used.
    bool darkScaleFixedInstead = false;
    /// Samples the cosmetic correction replaced.
    std::uint64_t corrected = 0u;
};

/// The masters of one run, each read once, and the calibrations prepared from them.
class AFTERNIGHTPROCESSES_API CalibrationMasterSet
{
public:
    /// A set reading through the run's registry, or through the engine context's when it is null.
    ///
    /// @param registry the run's registry, kept for the set's life.
    explicit CalibrationMasterSet(std::shared_ptr<const an::CodecRegistry> registry = nullptr);

    CalibrationMasterSet(CalibrationMasterSet&&) noexcept;
    CalibrationMasterSet& operator=(CalibrationMasterSet&&) noexcept;
    CalibrationMasterSet(const CalibrationMasterSet&) = delete;
    CalibrationMasterSet& operator=(const CalibrationMasterSet&) = delete;
    ~CalibrationMasterSet();

    /// A master as Float32 fractions, read the first time it is asked for.
    ///
    /// @param scope the admitted operation the read runs in.
    /// @param master the master's file.
    /// @return the master, owned by the set; `ErrorCode::InvalidArgument` for an invalid frame identity; or what
    /// reading
    ///         it refused, the file named.
    [[nodiscard]] an::Result<const an::Image<float>*> master(const an::consumer::OperationScope& scope,
                                                             const FrameId& master);

    /// One light's calibration: its masters read, the CCD dark made, the cosmetic maps found, each once per run.
    ///
    /// @param scope the admitted operation the reads run in.
    /// @param params the light's masters and settings.
    /// @return the calibration, shared by every light that names the same masters and settings; or what reading a
    ///         master, subtracting the bias from the dark or mapping its pixels refused, the file named.
    [[nodiscard]] an::Result<std::shared_ptr<const LightCalibration>>
    prepare(const an::consumer::OperationScope& scope, const WorkflowManifest::LightsCalibrationParams& params);

    /// What the set did.
    [[nodiscard]] const CalibrationMasterSetDiagnostics& diagnostics() const noexcept;

    /// What the lights calibrated with the set's calibrations fell back to, one warning per calibration and kind,
    /// worded by the calibration's masters alone, so every stage that calibrates the same lights says it alike: a
    /// cosmetic correction asked for with no dark to find its pixels in, and a dark scale the optimization could not
    /// fit, replaced by the fixed one. A calibration that changes nothing says nothing, as lights-only processing
    /// warns of nothing.
    ///
    /// @return the warnings, in the order of the calibrations' keys.
    [[nodiscard]] QStringList fallbackWarnings() const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// Calibrates a light read as Float32 fractions, then replaces the pixels the dark's maps mark, falling back to the
/// fixed dark scale for a light the optimization cannot serve: a colour light, one smaller than the central square, or
/// one whose dark gives the square nothing to fit against. The fallback is counted in the calibration's record.
///
/// @param scope the admitted operation.
/// @param calibration the light's calibration.
/// @param light the light.
/// @return the calibrated light; or what the engine's calibration or correction refused.
[[nodiscard]] an::Result<CalibratedLight> calibrateLight(const an::consumer::OperationScope& scope,
                                                         const LightCalibration& calibration,
                                                         const an::Image<float>& light);

} // namespace core
