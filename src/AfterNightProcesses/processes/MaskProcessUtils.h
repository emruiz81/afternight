// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the mask, star and background processes share: an image a run derives from its source rather than transforms
/// in place - a mask, a model, a map - made in the run's session with the metadata such an image keeps; a colour
/// image's CIE lightness as a plane of its own; and the extra images an Apply makes beside its result, which the run
/// stages on its worker and the window opens beside the target once the result has landed.
///
/// **A derived image is new.** It has its own description - one channel for a mask, whatever the process's output is
/// otherwise - and starts zeroed, so a sample the run does not write reads as nothing selected. It keeps the source's
/// acquisition record, solution and lineage, and none of its cards, its output decision or a validity it no longer
/// describes: a mask is a measurement of the source, not the source under another encoding.
///
/// **An extra output is opened only after the run is published.** A run's body never waits for the GUI thread; it
/// stages each extra image with the name and history its view is opened under, and the window takes them on the GUI
/// thread when the framework reports the run published - or drops them when it was not.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ProcessExecution.h"
#include "processes/PointwiseProcessUtils.h"
#include "session/ExecutionScope.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QString>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class ImageViewControl;

namespace an
{
class EngineContext;
class ExecutionContext;
} // namespace an

namespace process_utils
{

/// The description of an image a run derives from its source: the source's extent, origin and pixel aspect, with
/// `channels` channels - one makes a grey image, three a colour one - of `sampleType`, in the normalized linear domain.
///
/// @param source what the source is.
/// @param channels one or three.
/// @param sampleType the representation of the derived samples.
/// @param limits the limits of the engine context the image is made in.
/// @return the description; `ErrorCode::InvalidArgument` for another channel count or a description the limits refuse.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ImageDescriptor>
derivedImageDescriptor(const an::ImageDescriptor& source, std::uint32_t channels, an::SampleType sampleType,
                       const an::EngineLimits& limits);

/// The metadata an image a run derives from its source keeps: the source's acquisition record, solution and lineage,
/// a meaning whose every sample means something and whose encoding is stated by nothing, and no output decision. Its
/// cards are the processing-state cards of a derived image - nonlinear, stated by a process, named `stateOperation` -
/// and no other; none at all when `stateOperation` is empty.
///
/// @param source what the source's metadata is.
/// @param described what the derived image is.
/// @param stateOperation the process, as the processing-state cards name it, or empty for no cards.
/// @return the derived metadata; or what stating its meaning or its cards refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ImageMetadata>
derivedImageMetadata(const an::ImageMetadata& source, const an::ImageDescriptor& described,
                     const std::string& stateOperation);

/// A new image of the run's session: `described`, planar, every sample zero, carrying `metadata`.
///
/// @param scope the run's admitted execution, whose engine context holds the samples and whose session the asset
///        belongs to.
/// @param described what the image is.
/// @param metadata what is known about it.
/// @return the asset; or what allocating or adopting it refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
createDerivedImage(const an::ui::ExecutionScope& scope, const an::ImageDescriptor& described,
                   an::ImageMetadata metadata);

/// A colour source's CIE L*, in the unit interval, as a new grey Float32 image of the run's session - Float64 for a
/// Float64 source - read as the process framework's channel selector reads every colour image. A pixel the source's
/// validity rejects is left zero.
///
/// @param scope the run's admitted execution.
/// @param source a colour image: red, green and blue, with or without alpha.
/// @param stateOperation what the lightness's processing-state cards name, as `derivedImageMetadata` states them, for
///        a lightness that becomes a run's result; empty for none.
/// @return the lightness; `ErrorCode::IncompatibleArguments` for a source that is not a colour image; or what the
///         extraction refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
extractLightness(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
                 const std::string& stateOperation = std::string());

/// Runs a scale and a bias over planar floating samples in place, every sample of every channel, held to an interval
/// when one is given: the complement of a mask is a scale of -1 and a bias of 1.
///
/// @param execution the run's admitted execution.
/// @param sampleType Float32 or Float64.
/// @param samples the samples, written in place.
/// @param layout where they sit; planar.
/// @param scale the factor.
/// @param bias the addend.
/// @param clamp the interval the result is held to, or nothing.
/// @return success; or what building or running the program refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status scaleAndBiasInPlace(const an::ExecutionContext& execution,
                                                                     an::SampleType sampleType, void* samples,
                                                                     const an::ImageLayout& layout, double scale,
                                                                     double bias,
                                                                     std::optional<an::ClampInterval> clamp = {});

/// Holds planar floating samples to the unit interval, as a star reduction reads its inputs: a sample that holds no
/// number becomes zero and every other is clamped to [0, 1].
///
/// @param engine the run's engine context, whose memory holds the mask of the samples that hold no number.
/// @param execution the run's admitted execution.
/// @param sampleType Float32 or Float64.
/// @param samples the samples, written in place.
/// @param layout where they sit; planar.
/// @return success; or what marking, substituting or clamping refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status holdToUnitInterval(const an::EngineContext& engine,
                                                                    const an::ExecutionContext& execution,
                                                                    an::SampleType sampleType, void* samples,
                                                                    const an::ImageLayout& layout);

/// A planar copy of an image in fractions of full scale, which a run works on without touching the image it read: the
/// copy, and the samples opened over it, which live and die together.
struct NormalizedCopy
{
    /// The copy, an asset of the run's session in the image's own representation.
    an::ui::ProcessDestination copy;

    /// Its samples in fractions of full scale.
    NormalizedSamples samples;
};

/// A planar copy of an image in fractions of full scale.
///
/// @param scope the run's admitted execution.
/// @param source what is copied.
/// @return the copy; or what copying it or opening its samples refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<NormalizedCopy> normalizedCopyOf(const an::ui::ExecutionScope& scope,
                                                                                  const an::ui::ProcessSource& source);

/// Whether a description is a colour image: red, green and blue, with or without alpha.
///
/// @param described what the image is.
/// @return true for an RGB or RGBA image.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool describesColourImage(const an::ImageDescriptor& described) noexcept;

/// An image an Apply makes beside its result, waiting for the window to open it.
struct StagedOutput
{
    /// The image, of the run's session.
    an::consumer::ImageAsset asset;

    /// The name its view is opened under.
    QString name;

    /// The first step of its history.
    std::string history;
};

/// The extra images one Apply stages for its window, guarded for the worker that stages them and the GUI thread that
/// takes them.
class AFTERNIGHTPROCESSES_API StagedOutputs
{
public:
    /// Forgets every staged image: a new Apply starts, or the last one was not published.
    void clear();

    /// Stages an image. Called on a run's worker.
    ///
    /// @param output the image, its name and its history.
    void stage(StagedOutput&& output);

    /// Takes every staged image, in the order they were staged, leaving none.
    ///
    /// @return the images.
    [[nodiscard]] std::vector<StagedOutput> take();

private:
    std::mutex m_mutex;
    std::vector<StagedOutput> m_outputs;
};

/// Opens a view of each staged image beside a target, on the GUI thread, as the framework opens a new image a run
/// makes. An image no view can be made of is dropped.
///
/// @param outputs the images.
/// @param neighbour the view they are opened beside, or null.
/// @return how many views were opened.
AFTERNIGHTPROCESSES_API std::size_t openStagedOutputs(std::vector<StagedOutput>&& outputs, ImageViewControl* neighbour);

} // namespace process_utils
