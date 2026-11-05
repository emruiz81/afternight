// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the geometry and filter-array windows share: the geometry steps a run applies, one policy for what a geometry
/// change does to an image's metadata, the interpolation their dialogs offer and what "Auto" chooses, and the runs
/// themselves - an edit of a view's image or sequence in place, or new views of the images a run makes.
///
/// **One policy for the metadata.** Every geometry step is the engine's plan and its operation's effect: the
/// acquisition record and the meaning stay, the codec's cards are dropped and an output decision withdrawn, as the
/// viewer's turns and mirrors already do. A crop, a resize and an integer resample carry the image's solution moved
/// with its pixels - its reference pixel moved and its matrix scaled - with the solver's card. A rotation drops it,
/// and so does a filter-array resample, whose planes' samples sit at different places within each block, which one
/// solution cannot describe; the plate solve restates it. A sample the step wrote from the source's samples keeps the
/// source's validity rule; a step that could not write some sample marks it invalid in a mask.
///
/// **Parameters are read on the GUI thread.** A window turns its dialog into a value - the steps, the history's words,
/// the names its outputs take - before the run starts, and a worker reads none of the window.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ProcessExecution.h"
#include "session/ExecutionScope.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Algorithms/Kernel/SamplingKernel.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class ImageViewControl;
class QComboBox;

namespace an::ui::processes
{

// ---- The steps.

/// An integer resample of a mosaic, plane by plane of its filter array.
struct CfaIntegerResampleStep
{
    /// The factor, the direction and the reducer.
    an::IntegerResampleRequest request{};
};

/// One geometry step: a crop, a resize, a rotation, an integer resample or a filter-array integer resample.
using GeometryStep = std::variant<an::CropRequest, an::ResizeRequest, an::RotationRequest, an::IntegerResampleRequest,
                                  CfaIntegerResampleStep>;

/// The engine's plan of one step over an image of a description.
///
/// @param source what the step reads.
/// @param step the step.
/// @return the plan, or what the engine's planner refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::GeometryPlan> planGeometryStep(const an::ImageDescriptor& source,
                                                                                    const GeometryStep& step);

/// The solution a step's result carries: the source's moved with its pixels by a crop, a resize or an integer
/// resample, and nothing after a rotation or a filter-array resample. A resize keeps a polynomial distortion, rescaled,
/// while its two axis scales agree within a thousandth - as an aspect-locked resize's rounded sides leave them - and
/// one that scales the axes further apart keeps the linear solution and drops the distortion it can no longer
/// describe.
///
/// @param solution the source's solution.
/// @param plan the step's plan.
/// @param step the step.
/// @return the result's solution; nothing when the step drops it; or what the engine refused in moving it.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<std::optional<an::WcsSolution>>
movedSolution(const an::WcsSolution& solution, const an::GeometryPlan& plan, const GeometryStep& step);

/// What an image of a description becomes through steps, without running them: what a dialog shows of its result.
///
/// @param source what the first step reads.
/// @param steps the steps, in order.
/// @return the last step's result description, the source's for no step, or the first planner's refusal.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ImageDescriptor>
geometryResultDescriptor(const an::ImageDescriptor& source, std::span<const GeometryStep> steps);

/// Runs steps over an image under an admitted operation, into a new asset of the operation's session.
///
/// Each step is the engine's plan over the previous step's result. A resize and a rotation sample their source, and a
/// floating result in a normalized domain is then held to the unit interval; every other step copies or reduces stored
/// values. The metadata follows the policy this file states - the solution read from the source's cards and moved by
/// `movedSolution` - and the lineage names the operation and `lineage`.
///
/// @param scope the run's admitted operation.
/// @param source what the run reads.
/// @param steps the steps, at least one.
/// @param lineage what the lineage records the change as: "Crop", "Resize".
/// @param mosaicAsGrey whether a mosaic is read as one grey plane, its filter array forgotten - what a person who was
///        told a resize mixes the filters asked for - rather than refused by a planner that would mix them.
/// @return the result, or what a planner, the engine or the adoption refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
transformImage(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
               std::span<const GeometryStep> steps, std::string_view lineage, bool mosaicAsGrey = false);

// ---- The interpolation the dialogs offer.

/// The interpolation a resize or rotate dialog's choice stands for, the text naming it, and whether Auto chose it.
struct ResolvedInterpolation
{
    /// The engine's interpolation.
    an::KernelInterpolation interpolation = an::KernelInterpolation::Lanczos4;

    /// The interpolation's name as the dialog lists it: what the history records, never "Auto".
    QString label;

    /// Whether the dialog's choice was Auto.
    bool automatic = false;
};

/// The text the resize dialog shows for Auto: what it chooses, which depends on whether the image grows.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString resizeAutoInterpolationText();

/// The text the rotate dialog shows for Auto: Lanczos-4, always.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString rotationAutoInterpolationText();

/// Fills an interpolation combo box with the choices the resize and rotate dialogs offer, in their order, Auto first
/// under the text the dialog shows for it. Each item's data is the choice's own name, which
/// `selectedInterpolationName` reads back.
///
/// @param combo the combo box, emptied first.
/// @param autoText the text Auto shows.
AFTERNIGHTPROCESSES_API void fillInterpolationChoices(QComboBox* combo, const QString& autoText);

/// The name of the choice a combo box filled by `fillInterpolationChoices` holds: "Auto", "Bilinear", ...
[[nodiscard]] AFTERNIGHTPROCESSES_API QString selectedInterpolationName(const QComboBox* combo);

/// Selects a choice by its name in a combo box filled by `fillInterpolationChoices`.
AFTERNIGHTPROCESSES_API void selectInterpolationName(QComboBox* combo, const QString& name);

/// What a resize's choice stands for: Auto is Lanczos-4 when either side grows and the pixel area relation when
/// neither does.
///
/// @param name the choice's name.
/// @param enlarges whether the result is wider or taller than the image.
/// @return the interpolation and its name.
[[nodiscard]] AFTERNIGHTPROCESSES_API ResolvedInterpolation resolveResizeInterpolation(const QString& name,
                                                                                       bool enlarges);

/// What a rotation's choice stands for: Auto is Lanczos-4.
///
/// @param name the choice's name.
/// @return the interpolation and its name.
[[nodiscard]] AFTERNIGHTPROCESSES_API ResolvedInterpolation resolveRotationInterpolation(const QString& name);

/// The resize the resize dialog asks for: an edge rule that repeats the edge sample, and the dark-ringing guard for
/// bicubic and Lanczos.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::ResizeRequest resizeRequest(std::uint32_t width, std::uint32_t height,
                                                                      an::KernelInterpolation interpolation);

/// The rotation the rotate dialog asks for: clockwise by an angle in degrees, about the image's centre, on its own
/// canvas or one grown to hold every rotated pixel, the corners outside the image black.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::RotationRequest rotationRequest(double degrees, bool expandCanvas,
                                                                          an::KernelInterpolation interpolation);

// ---- The integer resample's reducers.

/// The reducers the integer resample dialogs offer, in their order: "Average", "Median", "Maximum", "Minimum".
[[nodiscard]] AFTERNIGHTPROCESSES_API const std::vector<QString>& integerReductionLabels();

/// The reducer a dialog's text names; anything else reads as Average, the default.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::IntegerReduction integerReductionFromLabel(const QString& label);

/// The integer resample an integer resample dialog asks for: an enlargement by replication, or a reduction by the
/// reducer that discards the samples of an incomplete block at the right and bottom edges.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::IntegerResampleRequest
integerResampleRequest(std::uint32_t factor, bool enlarge, an::IntegerReduction reduction);

// ---- What a dialog shows of sizes.

/// How many bytes an image of a description holds.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::uint64_t imageBytesOf(const an::ImageDescriptor& descriptor) noexcept;

/// A size in bytes as the dialogs show it: megabytes to two places, gigabytes beyond 1024 of them.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString sizeText(double bytes);

// ---- The runs.

/// What a run came to, told to its window on the GUI thread once it has landed.
struct GeometryRunOutcome
{
    /// Success; `ErrorCode::Cancelled` for a stop the user asked for; `ErrorCode::Busy` when another run held the view;
    /// or why the run failed, which the run has already said.
    an::Status status = an::Status::success();

    /// Whether the run's result was shown.
    bool published = false;

    /// The views a run to new views opened, in its outputs' order; empty for a run in place or one that opened none.
    std::vector<ImageViewControl*> views;
};

/// What a window is told when its run lands.
using GeometryRunDone = std::function<void(const GeometryRunOutcome&)>;

/// What a run calls the stop of its operation, for a case to stop it before it starts.
using AdmissionHook = std::function<void(an::ui::ExecutionScope&)>;

/// The words a run says itself in.
struct GeometryRunWords
{
    /// The progress window's title and the busy warning's: "Crop".
    QString title;

    /// The progress window's text over a sequence.
    QString progressText;

    /// The title a failure is reported under: "Error running crop".
    QString errorTitle;

    /// The sentence that leads a failure's text: "An error occurred when executing the crop process".
    std::string errorLead;
};

/// An image made of one image.
using ImageOperation =
    std::function<an::Result<an::consumer::ImageAsset>(const an::ui::ExecutionScope&, const an::ui::ProcessSource&)>;

/// The images made of one image, one per output.
using ImagesOperation = std::function<an::Result<std::vector<an::consumer::ImageAsset>>(const an::ui::ExecutionScope&,
                                                                                        const an::ui::ProcessSource&)>;

/// A run that changes a view's image, or every frame of its sequence, in place.
struct InPlaceRunRequest
{
    /// What the run says.
    GeometryRunWords words;

    /// The history step's name and description.
    std::string stepName;
    std::string stepDescription;

    /// What each image becomes.
    ImageOperation operation;

    /// Whether a run over a still image shows the words' progress window too, as a run over a sequence does: its
    /// Cancel stops the run, and the operation's own progress reports move it. A run that may take long - one that
    /// waits on another program - asks for it; a geometry step does not.
    bool progressWindow = false;

    /// The window's count of runs in flight, held until the run has landed.
    std::shared_ptr<void> inFlight;

    /// Called with the run's operation once it is admitted; empty in the product.
    AdmissionHook admissionHook;

    /// The images the operation reads beside the view's own - another view's image, a reference, the view's mask -
    /// leased before the run is admitted, one lease each and, for a still image's edit, in one order with the view's
    /// writer. The operation reads them through `readBeside`; an image it reads and the request does not name is waited
    /// for under the run's admission.
    std::vector<std::shared_ptr<const an::consumer::ImageAsset>> reads;
};

/// Runs an operation over a view's image as an edit the history records before the view shows it - behind the words'
/// progress window when the request asks for one, whose Cancel stops the run and which the operation's progress
/// reports move; over a sequence, takes the view for the run, reads every frame - demosaiced when the view demosaics
/// them, by the method the sequence states - writes what each becomes into a new sequence published whole, and shows
/// it in the view as one history step, behind a progress window whose Cancel stops it and which counts the frames. A
/// failure is reported under the run's words and in the log; a stop the user asked for is quiet. GUI thread.
///
/// @param view the view.
/// @param request the run.
/// @param done told what the run came to; may be empty.
AFTERNIGHTPROCESSES_API void runInPlace(ImageViewControl& view, InPlaceRunRequest request, GeometryRunDone done);

/// One new view a run opens.
struct NewViewOutput
{
    /// The view's name.
    std::string name;

    /// The history step the view begins with.
    std::string history;
};

/// A run that opens new views of the images it makes of a view's image, or new sequences of a view's sequence.
struct NewViewsRunRequest
{
    /// What the run says.
    GeometryRunWords words;

    /// The views a still image's run opens, one per image the operation makes.
    std::vector<NewViewOutput> imageOutputs;

    /// The views a sequence's run opens, one per image the operation makes of each frame.
    std::vector<NewViewOutput> sequenceOutputs;

    /// What each image becomes; empty for the frame itself, unchanged.
    ImagesOperation operation;

    /// Whether a sequence's frames are read demosaiced, by the method the sequence states.
    bool readDemosaiced = false;

    /// Whether the new sequences' views demosaic their frames as they show them.
    bool showDemosaiced = false;

    /// The frames a sequence's run reads, ascending; nothing for every frame of the sequence.
    std::optional<std::vector<std::uint64_t>> frames;

    /// Whether a run over a still image shows the words' progress window too, as a run over a sequence does: its
    /// Cancel stops the run, and the operation's own progress reports move it.
    bool progressWindow = false;

    /// The window's count of runs in flight, held until the run has landed.
    std::shared_ptr<void> inFlight;

    /// Called with the run's operation once it is admitted; empty in the product.
    AdmissionHook admissionHook;

    /// The images the operation reads beside the view's own - another view's image, a reference, the view's mask -
    /// leased before the run is admitted, one lease each and, for a still image's edit, in one order with the view's
    /// writer. The operation reads them through `readBeside`; an image it reads and the request does not name is waited
    /// for under the run's admission.
    std::vector<std::shared_ptr<const an::consumer::ImageAsset>> reads;
};

/// Runs an operation over a view's image and opens a view of each image it makes beside the view - behind the words'
/// progress window when the request asks for one, whose Cancel stops the run and which the operation's progress
/// reports move; over a sequence, takes the view for the run, writes what each frame becomes into one new sequence per
/// output, each published whole, and opens a view of each, behind a progress window whose Cancel stops it and which
/// counts the frames. Nothing is opened unless the whole run succeeded; a view the application cannot make leaves the
/// views opened before it, and the sequences no view shows are removed. A failure is reported under the run's words
/// and in the log; a stop the user asked for is quiet. GUI thread.
///
/// @param view the view.
/// @param request the run.
/// @param done told what the run came to; may be empty.
AFTERNIGHTPROCESSES_API void runToNewViews(ImageViewControl& view, NewViewsRunRequest request, GeometryRunDone done);

/// Reports a run's failure under its words and in the log; a stop the user asked for and a busy view say nothing.
///
/// @param parent the window the message belongs to, or null.
/// @param words the run's words.
/// @param status what the run came to.
AFTERNIGHTPROCESSES_API void reportRunFailure(QWidget* parent, const GeometryRunWords& words, const an::Status& status);

/// The engine session the application lends, or null.
[[nodiscard]] AFTERNIGHTPROCESSES_API const an::consumer::EngineSession* lentEngineSession();

} // namespace an::ui::processes
