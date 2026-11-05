// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the process framework reaches of components other parts of Controls own: the histogram a process dialog shows
/// beside its preview, and the frames a process reads from a sequence and writes to the sequence it makes.
///
/// The framework drives each of these and owns neither. The histogram is a widget over an image, and reading and
/// writing a sequence's frames is the sequence player's business - its container, its selection and its publication.
/// The framework reaches each through an interface declared here, made by a function declared here and defined beside
/// the component - the histogram's beside the histogram widgets, the frames' beside the sequence player - so the
/// framework compiles, and is tested, with neither, and either changes what it is without the framework changing.
#pragma once

#include "AfterNightControlsExport.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <QString>

#include <cstdint>
#include <memory>
#include <optional>

class QScrollArea;
class QWidget;

namespace an
{
class EngineContext;
class ExecutionContext;
} // namespace an

namespace an::ui
{

struct HistogramDisplayData;

/// How finely a process dialog's histogram bins an image.
enum class PreviewHistogramResolution : std::uint8_t
{
    /// Sixty-four bins.
    Bits6,

    /// One hundred and twenty-eight bins.
    Bits7,

    /// Two hundred and fifty-six bins.
    Bits8,

    /// Five hundred and twelve bins.
    Bits9,

    /// One thousand and twenty-four bins.
    Bits10,

    /// Four thousand and ninety-six bins.
    Bits12,

    /// Sixteen thousand three hundred and eighty-four bins.
    Bits14,

    /// Sixty-five thousand five hundred and thirty-six bins.
    Bits16,

    /// One hundred levels.
    Levels100,

    /// One thousand levels.
    Levels1000,

    /// Ten thousand levels.
    Levels10000
};

/// The histogram a process dialog shows beside its preview: what the dialog's histogram buttons drive. Made by
/// `createPreviewHistogram`, which the histogram widgets define. Every member is called on the GUI thread.
class IPreviewHistogram
{
public:
    /// The widget is the scroll area's and goes with it.
    virtual ~IPreviewHistogram() = default;

    /// The widget the dialog's scroll area shows.
    [[nodiscard]] virtual QWidget* widget() = 0;

    /// Counts and shows an image's histogram; the counting is the histogram's to schedule, off the GUI thread.
    ///
    /// @param image the image, kept alive while it is counted and read under its own lease; null clears the histogram.
    virtual void setImage(std::shared_ptr<const an::consumer::ImageAsset> image) = 0;

    /// Shows counts made elsewhere - by a preview's frame, which counts what it shows in its own operation - in place
    /// of counting an image: a count of an image on its way is stopped and never shown.
    ///
    /// @param counts the counts, at the histogram's resolution; null clears the histogram.
    virtual void showCounts(std::shared_ptr<const HistogramDisplayData> counts) = 0;

    /// The bins the histogram counts into, as the dialog last set them.
    [[nodiscard]] virtual PreviewHistogramResolution resolution() const = 0;

    /// Shows the red, green and blue channels or not.
    virtual void setDisplayedChannels(bool red, bool green, bool blue) = 0;

    /// Bins the image this finely.
    virtual void setResolution(PreviewHistogramResolution resolution) = 0;

    /// Draws the grid or not.
    virtual void setGridEnabled(bool enabled) = 0;

    /// Zooms in.
    virtual void zoomIn() = 0;

    /// Zooms out.
    virtual void zoomOut() = 0;

    /// Shows the whole range.
    virtual void resetZoom() = 0;

    /// Normalizes each channel to its own peak or not.
    virtual void setLocalChannelNormalization(bool enabled) = 0;

    /// Shows how many samples clip at each end or not.
    virtual void setClippingInfoEnabled(bool enabled) = 0;

    /// Fills the area under each curve or not.
    virtual void setAreaModeEnabled(bool enabled) = 0;
};

/// Makes a process dialog's histogram inside its scroll area. Defined beside the histogram widgets.
///
/// @param scrollArea the scroll area that shows it.
/// @param purpose what its counts are admitted as: `Preview` for a histogram counted again with every preview a dialog
/// shows, which then shares the threads kept for previews; otherwise a count someone asked for, admitted at the
/// session's width.
/// @return the histogram, its widget set as the scroll area's.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::unique_ptr<IPreviewHistogram>
createPreviewHistogram(QScrollArea* scrollArea,
                       an::consumer::OperationPurpose purpose = an::consumer::OperationPurpose::Apply);

/// One frame a process reads from a sequence.
struct ProcessSequenceFrame
{
    /// The frame's zero-based index in the sequence.
    std::uint64_t index = 0u;

    /// The frame, as an asset of the run's session, demosaiced when the sequence view demosaics it.
    an::consumer::ImageAsset asset;
};

/// The frames a process reads from a sequence, one at a time, in the sequence's order: the selected frames, or every
/// frame when nothing is selected. Made by `openProcessFrames`, which the sequence player defines. Called from the
/// run's worker only.
class IProcessFrameReader
{
public:
    /// Closes the sequence's source.
    virtual ~IProcessFrameReader() = default;

    /// How many frames the reader hands out.
    [[nodiscard]] virtual std::uint64_t frameCount() const = 0;

    /// Reads the next frame.
    ///
    /// @param engine the engine context of the run's execution.
    /// @param execution the run's admitted execution.
    /// @return the frame; nothing after the last; or why it could not be read.
    [[nodiscard]] virtual an::Result<std::optional<ProcessSequenceFrame>>
    next(const an::EngineContext& engine, const an::ExecutionContext& execution) = 0;
};

/// Opens the frames of a sequence for a process to read. Defined beside the sequence player.
///
/// @param session the session the frames are read into.
/// @param sequence the sequence, as the document states it.
/// @param autoDebayer whether a mosaic frame is demosaiced as it is read, as the sequence view shows it.
/// @return the reader, or why the sequence could not be opened.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<std::unique_ptr<IProcessFrameReader>>
openProcessFrames(const an::consumer::EngineSession& session, const DocumentSequence& sequence, bool autoDebayer);

/// Where a process writes the frames of the sequence it makes. Made by `createProcessFrameOutput`, which the sequence
/// player defines. Called from the run's worker only.
///
/// The output is published whole or not at all: a writer destroyed before `finish` succeeds leaves nothing at its path.
class IProcessFrameWriter
{
public:
    /// Discards whatever was written unless `finish` succeeded.
    virtual ~IProcessFrameWriter() = default;

    /// Appends one frame.
    ///
    /// @param engine the engine context of the run's execution.
    /// @param execution the run's admitted execution.
    /// @param index the frame's zero-based index in the sequence it was read from.
    /// @param frame the processed frame.
    /// @return success, or why the frame could not be written.
    [[nodiscard]] virtual an::Status write(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                           std::uint64_t index, const an::consumer::AnyImage& frame) = 0;

    /// Completes the output, which is whole at its path once this succeeds.
    ///
    /// @param engine the engine context of the run's execution.
    /// @param execution the run's admitted execution.
    /// @return the sequence written, as a document would state it, or why it could not be completed - in which case
    ///         nothing is left at its path.
    [[nodiscard]] virtual an::Result<DocumentSequence> finish(const an::EngineContext& engine,
                                                              const an::ExecutionContext& execution) = 0;
};

/// Creates the output of a process run over a sequence. Defined beside the sequence player.
///
/// @param session the session the frames are written from.
/// @param source the sequence the frames are read from, whose container the output takes.
/// @param path the file the output is published at, from the application's temporary-file service.
/// @return the writer, or why the output could not be created.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<std::unique_ptr<IProcessFrameWriter>>
createProcessFrameOutput(const an::consumer::EngineSession& session, const DocumentSequence& source,
                         const QString& path);

} // namespace an::ui
