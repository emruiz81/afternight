// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Export Sequence window: the sequence a view shows written as SER, uncompressed AVI or video, optionally
/// demosaiced, stretched as the view shows it and resized, and published whole or not at all.
///
/// `exportSequence` reads the frames from the sequence's own file, stretches each through the engine's presentation as
/// Apply Stretch does, resizes it through the engine's geometry, and writes it through a sink whose output is published
/// only when its last frame is in; a cancelled or failed export leaves nothing at its path, and a video is encoded by
/// the named software encoder.
#pragma once

#include "AfterNightControlsExport.h"
#include "ExportSequenceControlUiBuilder.h"
#include "controls/AutostretchApply.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Core/Status.h>

#include <QDialog>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <functional>
#include <stop_token>

class AutostretchParams;
class ImageViewControl;
class ProgressDialogControl;

namespace an::ui
{

/// The container an export writes.
enum class SequenceExportFormat : std::uint8_t
{
    /// SER, eight or sixteen bits.
    Ser,

    /// Uncompressed AVI, eight bits.
    RawAvi,

    /// Video, eight bits, encoded by the chosen coding.
    Video
};

/// The coding a video export encodes with.
enum class SequenceExportCodec : std::uint8_t
{
    /// H.264, written as MP4.
    H264,

    /// H.265, written as MP4.
    H265,

    /// VP9, written as WebM.
    Vp9
};

/// How a resized export fits each frame into the size it asks for.
enum class SequenceExportFill : std::uint8_t
{
    /// Scaled to fit inside, keeping its proportions, with black borders.
    Fit,

    /// Scaled to cover, keeping its proportions, and cropped about its centre.
    Fill,

    /// Scaled to the size, whatever its proportions.
    Stretch,

    /// Not scaled, centred, with black borders or cropped about its centre.
    Center
};

/// What an export asks for, snapshotted on the GUI thread when it starts.
struct ExportSequenceRequest
{
    /// The sequence, as the view's document states it.
    DocumentSequence sequence{};

    /// The file the export is published at, its extension the container's.
    QString outputPath;

    /// The container.
    SequenceExportFormat format = SequenceExportFormat::Ser;

    /// The coding of a video.
    SequenceExportCodec codec = SequenceExportCodec::H264;

    /// The frames per second the export states.
    double fps = 1.0;

    /// Whether a mosaic frame is demosaiced, by the sequence's demosaic method; a mosaic not demosaiced is written to
    /// AVI or video as grey samples.
    bool debayerCfa = false;

    /// Whether each frame is stretched as the view shows it.
    bool autostretch = false;

    /// The stretch, from the view's autostretch settings.
    AutostretchApplySettings stretch{.sequenceFrame = true};

    /// Whether each frame is resized.
    bool resize = false;

    /// The width a resized frame has.
    std::uint32_t width = 0u;

    /// The height a resized frame has.
    std::uint32_t height = 0u;

    /// How a resized frame fits that size.
    SequenceExportFill fillMode = SequenceExportFill::Fit;
};

/// The extension an export's file takes: `ser`, `avi`, `mp4`, or `webm` for VP9.
///
/// @param format the container.
/// @param codec the coding of a video.
/// @return the extension, without its dot.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString sequenceExportSuffix(SequenceExportFormat format,
                                                                  SequenceExportCodec codec);

/// A path with the extension an export writes: the path as it is when it already ends in it, any other extension of its
/// last component replaced, and the extension appended otherwise.
///
/// @param path the path the user named.
/// @param expectedSuffix the extension, with or without its dot.
/// @return the path.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString normalizeSequenceExportPath(const QString& path,
                                                                         const QString& expectedSuffix);

/// Writes the export a request asks for and publishes it at its path, whole or not at all. Each frame is read from the
/// sequence's file, demosaiced, stretched and resized as asked, converted for the container and appended, one admitted
/// step per frame. Called on a worker.
///
/// @param session the process's session.
/// @param request what to export.
/// @param stop the export's stop.
/// @param progress told how many frames are written of how many, on the worker; may be empty.
/// @return success; `ErrorCode::Cancelled` when stopped; or why nothing was exported - in which case nothing is left at
///         the request's path.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Status
exportSequence(const an::consumer::EngineSession& session, const ExportSequenceRequest& request, std::stop_token stop,
               const std::function<void(std::uint64_t written, std::uint64_t total)>& progress = {});

} // namespace an::ui

/// The Export Sequence window over one sequence. Every member is called on the GUI thread.
class AFTERNIGHTCONTROLS_API ExportSequenceControl : public QDialog
{
public:
    /// A window over a sequence.
    ///
    /// @param parent the window's parent.
    /// @param sequence the sequence, as the view's document states it.
    /// @param frameWidth the width of its frames.
    /// @param frameHeight the height of its frames.
    /// @param mosaic whether its frames are mosaics, which offers to demosaic them.
    /// @param autostretchParams the view's autostretch settings, which the export's stretch takes.
    ExportSequenceControl(QWidget* parent, an::ui::DocumentSequence sequence, std::uint32_t frameWidth,
                          std::uint32_t frameHeight, bool mosaic, const AutostretchParams& autostretchParams);

    ~ExportSequenceControl() override;

    /// Opens the window over the sequence a view shows, as the image menu's Export does.
    ///
    /// @param view the view.
    /// @return the window, shown and deleted when it closes; null when the view shows no sequence.
    static ExportSequenceControl* openFor(ImageViewControl& view);

    /// What the window's controls ask for now.
    [[nodiscard]] an::ui::ExportSequenceRequest buildExportRequest() const;

    /// Starts the export the controls ask for, as OK does; nothing when no file is named.
    void startExport();

    /// Whether an export is under way.
    [[nodiscard]] bool isExportRunning() const;

    /// The window's controls.
    [[nodiscard]] an::ui::controls::ExportSequenceControlUiElements& ui();

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);
    void on_outputFileBrowserButtonPushed();
    void on_outputFormatSelector_currentTextChanged(const QString& text);
    void on_codecSelector_currentTextChanged(const QString& text);
    void on_resolutionSelector_currentTextChanged(const QString& text);
    void on_widthValueChanged(int value);
    void on_heightValueChanged(int value);

    void done(int r) override;
    void closeEvent(QCloseEvent* event) override;

    void initialize();
    void setExportRunning(bool running);
    void completeExport(const an::Status& outcome);

    an::ui::controls::ExportSequenceControlUiElements m_ui;
    an::ui::DocumentSequence m_Sequence;
    std::uint32_t m_FrameWidth = 0u;
    std::uint32_t m_FrameHeight = 0u;
    bool m_Mosaic = false;
    an::ui::AutostretchApplySettings m_Stretch{};
    std::stop_source m_Stop;
    QPointer<ProgressDialogControl> m_Progress;
    bool m_ExportRunning = false;
};
