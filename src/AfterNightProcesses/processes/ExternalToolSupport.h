// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What StarNet, StarNet's star mask and DeepSNR share: a program that runs as a separate process over an image file
/// and writes one or more back, the arithmetic the engine does around those files, the program's progress read from
/// what it prints, and the images its answers become.
///
/// **The engine decides what the tool sees.** `prepareExternalToolHandoff` writes the image the tool is handed - an
/// integer sample as its fraction of full scale, a floating image mapped from the unit interval widened to its finite
/// valid range so nothing is clipped, optionally stretched by an automatic midtones transfer per channel or linked -
/// and `restoreExternalToolAnswer` brings an answer back through the record of what was decided, in the source's own
/// representation, written once. A tool is handed sixteen-bit codes, rounded to nearest with ties to even, when it
/// refuses floating-point images - StarNet2 2.5.3 and DeepSNR 1.2.2 both say they do - and Float32 when its profile
/// says it takes them, which loses nothing. The mask a tool writes is the tool's own answer: it is read back as it is,
/// narrowed to the source's representation and never mapped or unstretched.
///
/// **The program stays the application's.** Starting it, writing its file, reading its progress and killing it when
/// the run is stopped are this unit's; the file is written and read through the engine's codecs, whole or not at all.
/// The tool's progress is reported through the run's execution context, so the progress window of the run shows it as
/// it shows any operation's. The parsers in `processes/CliProgressParser.h` stay tool-specific; nothing here infers
/// progress from prose it does not know.
///
/// **Parameters are read on the GUI thread.** A window turns its dialog into a request - the program, how its
/// arguments are spelled, the pre-stretch, the outputs it wants and the words it says - before the run starts, and the
/// worker reads none of the window.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ProcessExecution.h"
#include "session/ExecutionScope.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Pointwise/ExternalToolHandoff.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace an::ui::processes
{

/// What a tool is: where it is, what it takes and how it reports.
struct ExternalToolProfile
{
    /// The tool's name as the messages and the log call it: "StarNet2", "DeepSNR".
    QString name;

    /// The program.
    QString executablePath;

    /// What the tool is handed: sixteen-bit codes for a tool that refuses floating-point images, Float32 for one that
    /// takes them.
    an::ExternalToolFormat format = an::ExternalToolFormat::UInt16;

    /// The smallest image the tool takes; zero for no requirement.
    std::uint32_t minimumWidth = 0u;
    std::uint32_t minimumHeight = 0u;
};

/// How a tool reports its progress.
enum class ExternalToolProgressFormat : std::uint8_t
{
    /// Percentages, fractions and counts in what the tool prints to either stream.
    Printed,

    /// JSON lines on the tool's error stream, in the StarNet CLI family's schema, beside whatever it prints.
    MachineReadable
};

/// The files a run names for the tool, in a directory of the run's own that is removed when the run ends.
struct ExternalToolFiles
{
    /// The directory.
    QString directory;

    /// The image the tool is handed.
    QString input;

    /// The tool's image answer: the starless image, the denoised image.
    QString image;

    /// The tool's mask answer, when one is asked for.
    QString mask;

    /// The tool's star layer, when one is asked for.
    QString stars;
};

/// What a run asks of a tool.
struct ExternalToolRequest
{
    /// The tool.
    ExternalToolProfile tool;

    /// The program's arguments for the files a run named, spelled as the tool spells them; told whether the tool is to
    /// print machine-readable progress, which the run decides from what the program reports of itself.
    std::function<QStringList(const ExternalToolFiles& files, bool machineProgress)> arguments;

    /// Whether the image is stretched before the tool sees it, and how.
    an::ExternalToolPreStretch preStretch = an::ExternalToolPreStretch::None;

    /// Whether the tool's image answer is read back, as an image.
    bool wantsImage = true;

    /// Whether the tool's mask answer is read back, as the tool's own answer.
    bool wantsMask = false;

    /// Whether the tool's star layer is read back, as a star layer.
    bool wantsStars = false;

    /// The processing-state operation the image answer is recorded under - "starnet-starless" - on derived metadata
    /// that keeps the source's acquisition record, solution and lineage; or nothing to keep the source's metadata
    /// whole, every sample now valid, as a denoised image keeps it.
    std::optional<std::string> imageState = std::nullopt;

    /// The processing-state operation the mask is recorded under: "starnet-star-mask".
    std::string maskState = "starnet-star-mask";

    /// The processing-state operation the star layer is recorded under: "starnet-stars".
    std::string starsState = "starnet-stars";

    /// What the lineage records the run as: the program's name, "StarNet2"; the run appends the version it reported,
    /// the arguments it was started with, the format handed and the pre-stretch.
    std::string lineage;
};

/// What a tool answered, each in the source's representation and of its size.
struct ExternalToolAnswers
{
    /// The image answer, when asked for.
    std::optional<an::consumer::ImageAsset> image;

    /// The mask, when asked for.
    std::optional<an::consumer::ImageAsset> mask;

    /// The star layer, when asked for.
    std::optional<an::consumer::ImageAsset> stars;

    /// Whether the tool printed machine-readable progress.
    bool machineProgress = false;

    /// The arguments the program was started with.
    QStringList arguments;
};

/// What a program reported of itself when it was asked.
struct ExternalToolCheck
{
    /// Whether the program is there, runs and is a version the product supports.
    bool supported = false;

    /// Whether it prints machine-readable progress when asked to.
    bool machineProgress = false;

    /// Its version, as it printed it.
    QString version;

    /// Why it is not supported, or that it is.
    QString detail;
};

/// Asks a program what it is: StarNet2 or DeepSNR by the profile's name, through the product's own probe of each,
/// which runs the program with a bounded wait. Any thread.
///
/// @param tool the tool.
/// @return what it reported.
[[nodiscard]] AFTERNIGHTPROCESSES_API ExternalToolCheck checkExternalTool(const ExternalToolProfile& tool);

/// Runs a tool over an image under an admitted operation and brings its answers back.
///
/// Refuses, before anything is written, a mosaic, an image of other than one or three channels, and one below the
/// tool's smallest size, each by name, and answers `ErrorCode::Cancelled` for an operation already stopped; then asks
/// the program what it is and refuses one the product does not support.
/// The handed image is written to a file of the run's own through the output transaction; the program is started
/// with the request's arguments in its own directory and read until it ends, its progress reported through the
/// operation's execution from ten to ninety percent, a line it prints that is not progress logged; a stop of the
/// operation kills it and answers `ErrorCode::Cancelled`. A program that could not start, crashed or exited with a
/// code other than zero is `ErrorCode::ExternalFailure`, its message saying which. Each answer asked for is read
/// through the codecs, held to the source's size and channels, and brought back: the image and the star layer through
/// the hand-off's inverse in the source's representation, the mask narrowed to it as the tool's own answer. Every
/// answer carries the request's metadata and a lineage entry naming the run.
///
/// Runs on a worker. Reads none of the window; everything it needs travels in the request.
///
/// @param scope the run's admitted operation.
/// @param source what the run reads.
/// @param request what is asked of the tool.
/// @return the answers, or why the run failed.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<ExternalToolAnswers>
runExternalTool(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
                const ExternalToolRequest& request);

/// The shaping StarNet's star mask window offers over the mask the tool wrote.
struct StarMaskShaping
{
    /// Whether a colour mask is reduced to its lightness, CIE L* in the unit interval.
    bool lightness = false;

    /// Whether the lightness is then selected between two limits, as a range selection with no fuzziness; read only
    /// with the lightness.
    bool binary = false;

    /// The selection's limits, within the unit interval, the lower not above the upper.
    double lowerLimit = 0.0;
    double upperLimit = 1.0;

    /// The exact Gaussian the selection is smoothed by, in pixels; zero for none.
    double smoothness = 0.0;
};

/// Shapes a star mask as the star mask window asks: a colour mask reduced to its lightness and, when asked, selected
/// between the limits and smoothed, the result narrowed to the mask's own representation under its metadata, whose
/// lineage names the extraction and any selection whatever that representation is. A mask
/// the shaping does not touch - a grey one, or no lightness asked - is returned as it is.
///
/// @param scope the run's admitted operation.
/// @param mask the mask the tool wrote, brought back in the source's representation.
/// @param shaping what is asked.
/// @return the shaped mask, or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
shapeStarMask(const an::ui::ExecutionScope& scope, an::consumer::ImageAsset&& mask, const StarMaskShaping& shaping);

/// The share of the progress window a tool's own progress runs over: from ten to ninety percent, the preparation
/// before and the reading of the answers after.
inline constexpr int kExternalToolProgressStart = 10;
inline constexpr int kExternalToolProgressEnd = 90;

/// The units one percent of a run is reported as. The codecs that write the handed image and read the answers report
/// their own rows or bytes through the same execution, and a report that moves backwards is dropped; a percent
/// reported as this many units sits above any count a codec reports, so once the run has reported its first percent
/// its percentages outrank them and the bar never jumps.
inline constexpr std::uint64_t kExternalToolProgressUnitsPerPercent = std::uint64_t{1} << 32u;

/// The processing-state operations the tools' answers are recorded under.
inline constexpr const char* kStarnetStarlessState = "starnet-starless";
inline constexpr const char* kStarnetStarsState = "starnet-stars";
inline constexpr const char* kStarnetStarMaskState = "starnet-star-mask";

} // namespace an::ui::processes
