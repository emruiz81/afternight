// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the PixelMath window runs: the dialog as a value, the symbols and the settings file it reads and writes, and
/// one run of its expressions over a target image and the images the expressions name.
///
/// **An expression is the engine's.** Every expression is parsed, resolved against the images and symbols it may name,
/// compiled once, and lowered to the engine's fused pointwise program when it is a chain; the program runs when it
/// lowered and the engine's evaluator runs it when it did not, and the run reports which one it took for every channel.
/// A local symbol is a variable each pixel starts from its stated value, a global symbol a constant: the engine has no
/// name whose value one pixel leaves for the next.
///
/// **The binding policy.** Every image is read as fractions of full scale - an unsigned integer as a fraction of its
/// largest value, a signed one counted from its smallest value to its largest, a floating sample as it is - in one
/// floating working representation, Float64 when any image or the output holds Float64 or a 32-bit integer and Float32
/// otherwise. An image of another width or height than the output is resampled to it with Lanczos-4 first, in its own
/// representation. The target is the image the output's colour space asks for: grey from colour by
/// `0.2989 R + 0.5870 G + 0.1140 B`, colour from grey by repeating it, an alpha channel added at zero or dropped. Every
/// image is declared in the target's numeric domain when both are fractions of full scale - normalized linear or
/// encoded - and in its own otherwise, so an image of sensor codes or physical quantities beside fractions is refused
/// by the engine by name rather than read as a fraction of something it is not. Only the images an expression names
/// are read.
///
/// **What is written.** A channel whose expression is empty keeps the target's samples, as does a pixel the target's
/// validity rejects. The result is not held to the unit interval; "Rescale result" maps the smallest and largest finite
/// value of every channel to the dialog's bounds. The fractions are narrowed once to the output's representation, to
/// the nearest value and saturated at its range. A result that replaces its target keeps the target's description,
/// every card and the solution and states its samples nonlinear under the name `pixelmath`; a new image keeps the
/// target's acquisition record and lineage, its solution only at the target's size, and no card but the processing
/// state.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ProcessExecution.h"
#include "session/ExecutionScope.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathLowering.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <QString>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace an::ui::processes
{

/// Where the result goes: the dialog's output modes, in their order.
enum class PixelMathOutputMode : std::uint8_t
{
    /// The result replaces the target image, or every frame of its sequence, as one history step.
    ReplaceTarget = 0u,

    /// The result opens as a new image, or a new sequence, beside the target.
    CreateNewImage = 1u
};

/// What channels a new image has: the dialog's colour spaces, in their order.
enum class PixelMathColorSpace : std::uint8_t
{
    /// The target's channels.
    AsTarget = 0u,

    /// One grey channel.
    Grayscale = 1u,

    /// Red, green and blue.
    Rgb = 2u,

    /// Red, green, blue and alpha.
    Rgba = 3u
};

/// What representation a new image has: the dialog's colour formats, in their order.
enum class PixelMathColorFormat : std::uint8_t
{
    /// The target's representation.
    AsTarget = 0u,

    /// Unsigned 8-bit integers.
    UInt8 = 1u,

    /// Unsigned 16-bit integers.
    UInt16 = 2u,

    /// Unsigned 32-bit integers.
    UInt32 = 3u,

    /// 32-bit floats.
    Float32 = 4u,

    /// 64-bit floats.
    Float64 = 5u
};

/// The tab an expression is written in: the dialog's first four tabs, in their order.
enum class PixelMathExpressionTab : std::uint8_t
{
    /// "RGB/K" when one expression writes every colour channel or the image is grey, "R" otherwise.
    RedOrAll = 0u,

    /// "G", read only when each colour channel has its own expression.
    Green = 1u,

    /// "B", read only when each colour channel has its own expression.
    Blue = 2u,

    /// "A", the alpha channel's expression.
    Alpha = 3u
};

/// The dialog as it was when Apply was pressed: what a run reads, and what the settings file holds.
struct PixelMathSettings
{
    /// The expressions of the four tabs, in `PixelMathExpressionTab` order. An empty or blank one writes nothing.
    std::array<std::string, 4u> expressions{};

    /// Whether the first tab's expression writes every colour channel: the dialog's "Use a single RGB/K expression"
    /// as it was enabled and checked. A grey output has one channel, which the first tab writes either way.
    bool singleExpression = true;

    /// The local symbols, a comma-separated list of names, each with an optional `= value`.
    std::string localSymbols;

    /// The global symbols, in the same form.
    std::string globalSymbols;

    /// Whether the result is rescaled to the bounds below.
    bool rescale = false;

    /// The value the smallest finite result is mapped to.
    double rescaleLow = 0.0;

    /// The value the largest finite result is mapped to.
    double rescaleHigh = 1.0;

    /// Where the result goes.
    PixelMathOutputMode outputMode = PixelMathOutputMode::ReplaceTarget;

    /// The name a new image or sequence takes; `kPixelMathDefaultImageName` when blank.
    std::string newImageId = "PIXEL_MATH_IMAGE";

    /// A new image's width; the target's when zero. A replaced target keeps its own.
    std::uint32_t width = 0u;

    /// A new image's height; the target's when zero. A replaced target keeps its own.
    std::uint32_t height = 0u;

    /// A new image's channels. A replaced target keeps its own.
    PixelMathColorSpace colorSpace = PixelMathColorSpace::AsTarget;

    /// A new image's representation. A replaced target keeps its own.
    PixelMathColorFormat colorFormat = PixelMathColorFormat::AsTarget;

    /// The dialog's "Single threaded", stored and restored. A run computes the same image at any number of threads, so
    /// it changes nothing a run writes.
    bool singleThreaded = false;

    /// The dialog's "Process in 64 bits", stored and restored. Every expression is evaluated in double precision.
    bool processIn64Bits = true;

    /// The dialog's "Code optimization", stored and restored; the engine compiles every expression once regardless.
    bool codeOptimization = true;
};

/// The name the history records an in-place run under, the window's own: "PixelMath".
inline constexpr const char* kPixelMathStepName = "PixelMath";

/// The name the processing-state cards record the result under.
inline constexpr const char* kPixelMathStateOperation = "pixelmath";

/// The name a new image takes when the dialog's is blank, and the history it begins with.
inline constexpr const char* kPixelMathDefaultImageName = "PIXEL_MATH_IMAGE";
inline constexpr const char* kPixelMathImageHistory = "Image created from PixelMath script";

/// The name a new sequence takes when the dialog's is blank, and the history it begins with.
inline constexpr const char* kPixelMathDefaultSequenceName = "PIXEL_MATH_SEQUENCE";
inline constexpr const char* kPixelMathSequenceHistory = "Sequence created from PixelMath script";

/// One symbol of a symbol list.
struct PixelMathSymbol
{
    /// The name, trimmed.
    std::string name;

    /// The value; zero when none was stated or it does not start with a number.
    double value = 0.0;
};

/// The symbols a list declares: the list split at each comma, each entry at its equals sign, the name and the value
/// trimmed, a value read from the number it starts with and zero when it starts with none or the entry has no single
/// equals sign. An entry with no name declares nothing; a name declared twice takes the later value.
///
/// @param text the list.
/// @return the symbols, ordered by name.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<PixelMathSymbol> parsePixelMathSymbols(std::string_view text);

/// Reads a settings file the window writes (`.anpm`, version 1).
///
/// @param path the file.
/// @return the settings, every value the file does not state at its default; `ErrorCode::ExternalFailure`
///         for a file that cannot be read; `ErrorCode::InvalidData` for one that is not version 1 of the format or
///         lacks its settings, naming what is missing.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<PixelMathSettings> readPixelMathSettingsFile(const QString& path);

/// Writes a settings file the window reads.
///
/// @param path the file, replaced.
/// @param settings what it holds.
/// @return success, or `ErrorCode::ExternalFailure` for a file that cannot be written.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status writePixelMathSettingsFile(const QString& path,
                                                                            const PixelMathSettings& settings);

/// The expression each output channel is written by, decided from the settings: with one expression, the first tab's
/// for the grey or colour channels and the alpha tab's for an alpha channel; with one per colour channel, the tab of
/// each. A blank expression is empty.
///
/// @param settings the dialog.
/// @param channels the output's channel count, one to four.
/// @return the expression per channel, and the tab it was read from.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<std::pair<PixelMathExpressionTab, std::string>>
pixelMathChannelExpressions(const PixelMathSettings& settings, std::uint32_t channels);

/// What a tab is called where a failure names it: "RGB/K", "R", "G", "B" or "A", as the dialog shows it.
///
/// @param tab the tab.
/// @param singleExpression whether one expression writes every colour channel.
/// @return the tab's text.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::string pixelMathTabText(PixelMathExpressionTab tab, bool singleExpression);

/// What the output of a run over an image of a description is: the target's description for a result that replaces
/// it; for a new image, the dialog's width, height, channels and representation where it states them and the target's
/// where it does not, in the target's numeric domain, with no mosaic unless it is the target's own grey image.
///
/// @param settings the dialog.
/// @param target what the target is.
/// @param limits the engine's limits.
/// @return the output's description; `ErrorCode::InvalidArgument` for a size or a representation that cannot hold the
///         target's numeric domain.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ImageDescriptor>
pixelMathOutputDescriptor(const PixelMathSettings& settings, const an::ImageDescriptor& target,
                          const an::EngineLimits& limits);

/// One image an expression may name besides its target, snapshotted on the GUI thread when Apply was pressed.
struct PixelMathNamedImage
{
    /// The name an expression reads it by: its view's unique name.
    std::string name;

    /// The asset its view held.
    std::shared_ptr<const an::consumer::ImageAsset> asset;
};

/// Every image a run may bind, snapshotted on the GUI thread when Apply was pressed.
struct PixelMathImageSet
{
    /// The target view's unique name: an expression that names it reads `$T`, the image being written.
    std::string targetName;

    /// Every other open view's image, by its view's unique name.
    std::vector<PixelMathNamedImage> others;

    /// What `$A` reads: the active view's image, or null when the active view is the target or there is none, which
    /// reads `$T`.
    std::shared_ptr<const an::consumer::ImageAsset> active;

    /// What `$M` reads: the image the target view uses as its mask, or null for none, which an expression naming `$M`
    /// is refused for.
    std::shared_ptr<const an::consumer::ImageAsset> mask;
};

/// Which way one expression ran over which channels.
struct PixelMathChannelRun
{
    /// The output channels it wrote.
    std::vector<std::uint32_t> channels;

    /// The tab it was written in.
    PixelMathExpressionTab tab = PixelMathExpressionTab::RedOrAll;

    /// Whether it ran as the engine's fused pointwise program; otherwise the engine's evaluator ran it.
    bool fused = false;

    /// Why it did not lower to the fused program; `None` when it did.
    an::PixelMathLoweringBlocker blocker = an::PixelMathLoweringBlocker::None;
};

/// What one run did, for its record and its cases.
struct PixelMathRunReport
{
    /// Every expression that ran, in channel order.
    std::vector<PixelMathChannelRun> runs;

    /// The images the run bound, by the names the expressions read them by: `$T` first.
    std::vector<std::string> boundImages;

    /// The working representation every image was read in.
    an::SampleType working = an::SampleType::Float32;

    /// The rule the target's pixels were selected by: the target's own, an explicit mask where a rule an integer's
    /// value decides was judged on its stored samples, and every pixel when the output is not the target's shape.
    an::ValidityRule selectedBy = an::ValidityRule::AllValid;
};

/// The seed a frame of a run draws its random numbers from: the run's for its first frame, and one derived from it and
/// the frame's index for every other, so two frames draw different numbers and a run drawn again draws the same.
///
/// @param runSeed the run's seed.
/// @param frame the frame's index from zero; zero for a still image.
/// @return the frame's seed.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::uint64_t pixelMathFrameSeed(std::uint64_t runSeed,
                                                                       std::uint64_t frame) noexcept;

/// Runs the dialog's expressions over a target, under the binding policy this file states.
///
/// @param scope the run's admitted execution, whose session the result belongs to.
/// @param target the image being written: the view's image, or a frame of its sequence.
/// @param settings the dialog as it was when Apply was pressed.
/// @param images the images the expressions may name.
/// @param randomSeed the seed `random()` draws from.
/// @param report filled with what the run did; may be null.
/// @return the result, of `pixelMathOutputDescriptor`; `ErrorCode::InvalidArgument` naming the tab for an expression
///         the engine refuses, with every diagnostic, or for no expression at all; or what reading, resampling,
///         evaluating or narrowing refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
runPixelMath(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& target,
             const PixelMathSettings& settings, const PixelMathImageSet& images, std::uint64_t randomSeed,
             PixelMathRunReport* report = nullptr);

/// The images a run of these settings reads beside its target, out of the set the window snapshotted: the views its
/// expressions name, and what `$A` and `$M` read when an expression reads them. The run leases them before it is
/// admitted and `runPixelMath` reads them through those leases. An expression that does not parse names nothing; the
/// run refuses it.
///
/// @param settings the dialog as it was when Apply was pressed.
/// @param images the images the expressions may name.
/// @return the images, each once.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
pixelMathImagesRead(const PixelMathSettings& settings, const PixelMathImageSet& images);

} // namespace an::ui::processes
