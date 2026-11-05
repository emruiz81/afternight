// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Calibration's run over the engine: the masters read once, each light calibrated by them and written whole or
/// not at all, the cosmetic correction mapped once on the dark and applied to every light, and the names and
/// representations the window's output options give the files it writes.
///
/// **The arithmetic is the engine's calibration.** Each light is read as fractions of its representation's full scale
/// - Float32, or Float64 when the output asks for double precision or the light's own representation needs it - with
/// its masters read the same way, and calibrated by one `calibrateImage` call: the pedestal added, the bias
/// subtracted, the scaled dark subtracted and the normalized flat divided, in that order. The pedestal is stated in
/// sixteen-bit codes and divided by 65535, as the static preview calibrates; a pedestalled sample is not clamped to
/// the unit interval.
///
/// **What is written is decided before anything is read.** FITS and TIFF write floating samples at the precision the
/// window names; "Original input format" writes the light's own container and representation, converted back from
/// the working fractions to the nearest code. A file is written through the output transaction, so a stopped or failed
/// light leaves no file.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ImageListTableHelpers.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace an::ui::processes
{

/// The container a calibrated light is written to.
enum class CalibrationOutputFormat : std::uint8_t
{
    /// FITS: the light's own FITS extension, or `.fits`.
    Fits,
    /// TIFF, as `.tiff`.
    Tiff,
    /// The light's own container and representation.
    Original
};

/// The format a persisted or selected text names: "fits", "tiff" or "original", case and surrounding space aside;
/// anything else is FITS, the window's default.
///
/// @param text the text.
/// @return the format.
[[nodiscard]] AFTERNIGHTPROCESSES_API CalibrationOutputFormat calibrationOutputFormatFromText(const QString& text);

/// The text a format is persisted and selected as.
///
/// @param format the format.
/// @return "fits", "tiff" or "original".
[[nodiscard]] AFTERNIGHTPROCESSES_API QString calibrationOutputFormatText(CalibrationOutputFormat format);

/// Whether the precision selector applies to a format: it does to every format but the light's own.
///
/// @param format the format.
/// @return whether the precision applies.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool calibrationOutputPrecisionApplies(CalibrationOutputFormat format) noexcept;

/// How the calibrated lights are named and written.
struct CalibrationOutputOptions
{
    /// Whether a light keeps its own base name; otherwise it is `image_` and its four-digit index in the list.
    bool preserveOriginalName = true;

    /// The text added after the base name.
    QString filenameSuffix = QStringLiteral("_cal");

    /// The container.
    CalibrationOutputFormat format = CalibrationOutputFormat::Fits;

    /// Whether FITS and TIFF are written as Float32 rather than Float64.
    bool use32BitFloat = true;
};

/// The file a light is calibrated into: the directory, the base name, the suffix and the format's extension - FITS
/// keeps a light's FITS extension (`.fit`, `.fts`, `.fits`, any case) and writes `.fits` otherwise, TIFF writes
/// `.tiff`, and the light's own format keeps its extension, `.fits` for a light with none.
///
/// @param source the light's path.
/// @param directory the output directory.
/// @param options the output options.
/// @param index the light's index in the list, which names it when its base name is not kept.
/// @return the path.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString calibrationOutputPath(const QString& source, const QString& directory,
                                                                    const CalibrationOutputOptions& options,
                                                                    std::size_t index);

/// The representation a calibrated light is written in: Float32 or Float64 by the precision for FITS and TIFF, the
/// light's own for its own format.
///
/// @param options the output options.
/// @param source the light's representation.
/// @return the representation.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::SampleType
calibrationOutputRepresentation(const CalibrationOutputOptions& options, an::SampleType source) noexcept;

/// The representation a light is calibrated in: Float64 when it is written as Float64, or written in its own
/// representation and that is Float64 or a 32-bit integer - whose codes Float32 cannot hold - and Float32 otherwise.
///
/// @param options the output options.
/// @param source the light's representation.
/// @return Float32 or Float64.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::SampleType
calibrationWorkingRepresentation(const CalibrationOutputOptions& options, an::SampleType source) noexcept;

/// What a calibration run asks for, read from the window on the GUI thread.
struct CalibrationBatchSettings
{
    /// The bias master, or empty for none.
    QString biasPath;

    /// The dark master, or empty for none. The dark is taken as free of bias, as an integration's master is.
    QString darkPath;

    /// The flat master, or empty for none.
    QString flatPath;

    /// Whether each light's dark scale is found by minimizing its noise over the central square, rather than fixed.
    bool darkOptimization = false;

    /// The fixed dark scale.
    double darkMultiplicationFactor = 1.0;

    /// Whether the flat is divided after normalizing it by its central mean.
    bool normalizeFlats = true;

    /// Whether the pixels the dark marks as cold or hot are corrected in every calibrated light.
    bool cosmeticCorrection = false;

    /// How many deviations below the dark's median marks a cold pixel, or nothing for none.
    std::optional<double> coldSigma = 3.0;

    /// How many deviations above the dark's median marks a hot pixel, or nothing for none.
    std::optional<double> hotSigma = 3.0;

    /// The pedestal added to every light, in sixteen-bit codes.
    double pedestalCodes = 0.0;

    /// The directory the calibrated lights are written to.
    QString outputDirectory;

    /// How they are named and written.
    CalibrationOutputOptions output;
};

/// Whether a run would change anything: it names a master or a positive pedestal - the rule that enables Calibrate.
///
/// @param settings the run's settings.
/// @return whether it calibrates.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool
calibrationBatchCalibrates(const CalibrationBatchSettings& settings) noexcept;

/// What calibrating one light did.
struct CalibrationFrameOutcome
{
    /// The file written.
    QString output;

    /// The dark scale the light was calibrated with, when a dark was subtracted.
    std::optional<double> darkScale;

    /// Whether the dark scale was to be optimized and the light could not be - a colour light, or one smaller than
    /// the central square - so the fixed scale was used.
    bool darkScaleFixedInstead = false;

    /// Samples the cosmetic correction replaced.
    std::uint64_t corrected = 0u;
};

/// One calibration run: its masters and cosmetic maps, read and checked once, and each light calibrated by them.
///
/// Opened and used on one worker; a run is not shared between threads. It holds images of the session's engine, so it
/// is let go before the session is.
class AFTERNIGHTPROCESSES_API CalibrationBatch
{
public:
    /// Reads and checks the masters a run names, and maps the dark's cold and hot pixels when the cosmetic correction
    /// is asked for: each master read in an admitted operation, its size and channels held to the first light's.
    ///
    /// @param session the process's session.
    /// @param settings the run's settings.
    /// @param lights the lights, as probed; a master is held to the first.
    /// @param stop stops the reads.
    /// @return the run; `ErrorCode::InvalidArgument` for no light or a run that calibrates nothing;
    ///         `ErrorCode::IncompatibleArguments` for a master whose size or channels differ from the lights';
    ///         `ErrorCode::Cancelled` when stopped; or what reading a master refused.
    [[nodiscard]] static an::Result<CalibrationBatch>
    open(const an::consumer::EngineSession& session, const CalibrationBatchSettings& settings,
         const std::vector<ImageListProcessHelpers::ImageListEntry>& lights, std::stop_token stop);

    CalibrationBatch(CalibrationBatch&&) noexcept;
    CalibrationBatch& operator=(CalibrationBatch&&) noexcept;
    ~CalibrationBatch();

    /// Calibrates one light and writes it: read, widened to its working representation, calibrated, corrected when
    /// asked, and written in the output representation - whole, or nothing at the output path.
    ///
    /// @param light the light's path.
    /// @param index the light's index in the list.
    /// @param stop stops the light.
    /// @return what was done; `ErrorCode::Cancelled` when stopped; or what a step refused.
    [[nodiscard]] an::Result<CalibrationFrameOutcome> calibrate(const QString& light, std::size_t index,
                                                                std::stop_token stop);

    /// Cold pixels the dark marks, summed over its channels; zero with no cosmetic correction.
    [[nodiscard]] std::uint64_t coldPixels() const noexcept;

    /// Hot pixels the dark marks, summed over its channels; zero with no cosmetic correction.
    [[nodiscard]] std::uint64_t hotPixels() const noexcept;

private:
    struct State;
    explicit CalibrationBatch(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> m_state;
};

/// How many pixels a dark marks as cold and as hot, as the Estimate button reports them.
struct CosmeticEstimate
{
    /// Cold pixels, summed over the channels.
    std::uint64_t cold = 0u;

    /// Hot pixels, summed over the channels.
    std::uint64_t hot = 0u;
};

/// Counts the pixels a dark marks as cold and hot, writing no map.
///
/// @param session the process's session.
/// @param darkPath the dark master.
/// @param coldSigma the cold threshold in deviations, or nothing for none.
/// @param hotSigma the hot threshold in deviations, or nothing for none.
/// @param stop stops the count.
/// @return the counts; `ErrorCode::Cancelled` when stopped; or what reading the dark or the scan refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<CosmeticEstimate>
estimateCosmeticDefects(const an::consumer::EngineSession& session, const QString& darkPath,
                        std::optional<double> coldSigma, std::optional<double> hotSigma, std::stop_token stop);

} // namespace an::ui::processes
