// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The New Sequence window: a list of image files made into an image list or a SER sequence, published whole or not at
/// all, and opened in a new sequence view.
///
/// A file is probed on the thread pool - its description and its metadata, no pixel - and the rows are added on the
/// GUI thread as the probes land; a row is read and presented for the preview on the thread pool, only the latest
/// shown; and `makeNewSequence` writes the sequence through the engine's image-list manifest or SER sink into an output
/// published only when its last frame is in, demosaicing a mosaic by the product's one demosaic setting. Closing the
/// window stops what is under way and waits for none of it.
#pragma once

#include "AfterNightControlsExport.h"
#include "NewSequenceControlUiBuilder.h"
#include "app/DemosaicTextMaps.h"
#include "controls/ImageListTableHelpers.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "session/CommitTicket.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Core/Status.h>

#include <verdigris/wobjectdefs.h>

#include <QPixmap>
#include <QPointer>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <unordered_set>
#include <vector>

class ProgressDialogControl;
class QWidget;
class WaitingSpinnerWidget;

namespace an::ui
{

class HistogramCountScheduler;

/// What a new sequence is made as.
enum class NewSequenceKind : std::uint8_t
{
    /// An image-list manifest naming the files where they are.
    ImageList,

    /// A SER sequence holding every file's frame.
    Ser
};

/// What making a new sequence asks for, snapshotted on the GUI thread when Apply is pressed.
struct NewSequenceRequest
{
    /// The files, in the order the list shows them.
    std::vector<ImageListProcessHelpers::ImageListEntry> files;

    /// What the sequence is made as.
    NewSequenceKind kind = NewSequenceKind::ImageList;

    /// Where the sequence is published: a temporary file the application's temporary-file service names, ending in
    /// `.anseq` for an image list and `.ser` for a SER sequence.
    QString path;

    /// The frames per second the sequence states.
    double framesPerSecond = 1.0;

    /// Whether a SER sequence's mosaic frames are demosaiced as they are written.
    bool demosaic = false;

    /// The filter array a mosaic is demosaiced under: the one each frame states, or one the window names.
    BayerPatternChoice pattern{};

    /// The method a mosaic is demosaiced with: the product's one demosaic setting.
    an::DemosaicMethod method = an::DemosaicMethod::Rcd;

    /// When the sequence was made, in seconds since the Unix epoch.
    std::uint64_t createdSeconds = 0u;
};

/// Makes the sequence a request asks for and publishes it at its path, whole or not at all. An image list's manifest
/// names each file by its absolute path; a SER sequence decodes each file, demosaics a mosaic when asked, converts it
/// for the container and appends it, one admitted step per frame. Called on a worker.
///
/// @param session the process's session.
/// @param request what to make.
/// @param stop the run's stop.
/// @param progress told how many frames are written of how many, on the worker; may be empty.
/// @return success; `ErrorCode::Cancelled` when stopped; `ErrorCode::EmptySelection` for no file; or why the sequence
///         was not made - in which case nothing is left at its path.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Status
makeNewSequence(const an::consumer::EngineSession& session, const NewSequenceRequest& request, std::stop_token stop,
                const std::function<void(std::uint64_t written, std::uint64_t total)>& progress = {});

} // namespace an::ui

/// The New Sequence window. Every member is called on the GUI thread.
class AFTERNIGHTCONTROLS_API NewSequenceControl : public TopLevelProcessBaseControl, public TableWidgetListener
{
    W_OBJECT(NewSequenceControl)

public:
    explicit NewSequenceControl(QWidget* parent = nullptr);
    ~NewSequenceControl() override;

    static std::string getProcessNameStatic();

    void onRowSelected(TableWidget* w, int row) override;
    void onColumnClicked(TableWidget* w, int column) override;

    void refreshTheme() override;

    /// Probes files on the thread pool and adds the readable ones to the list as the probes land. A file already listed
    /// is passed over; the first file that cannot be read is reported and ends the batch.
    ///
    /// @param items the files.
    void addItemsNonBlocking(const QStringList& items);

    /// Removes files from the list.
    ///
    /// @param items the files.
    void removeItems(const std::vector<QString>& items);

    /// The files the list holds, in order.
    [[nodiscard]] const std::vector<ImageListProcessHelpers::ImageListEntry>& entries() const;

    /// Whether nothing the window started - a probe, a preview or a run - is still on its way.
    [[nodiscard]] bool isIdle() const;

    /// How many previews the window has shown since it was made.
    [[nodiscard]] std::uint64_t previewsShown() const;

    /// The image the preview shows, or null.
    [[nodiscard]] std::shared_ptr<const an::consumer::ImageAsset> previewImage() const;

    /// Starts making the sequence, as Apply does: the list is checked on the GUI thread, the run goes to the thread
    /// pool and the new view is made when it lands.
    void createSequence();

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);
    void on_addFilesButtonPushed();
    void on_removeSelectedButtonPushed();
    void on_moveUpButtonPushed();
    void on_moveDownButtonPushed();
    void on_clearAllButtonPushed();
    void on_debayerCFA_clicked(Qt::CheckState state);

    void closeEvent(QCloseEvent* evt) override;

    void initialize();
    void initializeTable();

    /// Adds probed files to the table and the list.
    void addItems(std::vector<ImageListProcessHelpers::ImageListEntry>&& files);

    /// The probes of one batch have all landed.
    void addingFinished(const QString& failedPath, const an::Status& failure);

    /// Asks for a row's preview: read and presented on the thread pool, shown when it lands if still current.
    void requestPreview(int row);

    /// The run has landed: the new view is made, or the failure reported.
    void creationFinished(const QString& path, const QString& name, bool demosaic,
                          an::Result<an::ui::ImageDocument>&& made);

    void setRunning(bool running);
    void clearData();
    void tableUpdated();
    void updateImageSummary();
    void showNoPreview();

    an::ui::controls::NewSequenceControlUiElements m_ui;

    std::vector<ImageListProcessHelpers::ImageListEntry> m_image_list;
    std::unordered_set<std::string> m_image_paths_set;

    std::unique_ptr<WaitingSpinnerWidget> m_ImagePreviewSpinner;
    std::unique_ptr<WaitingSpinnerWidget> m_TableSpinner;
    std::unique_ptr<an::ui::HistogramCountScheduler> m_Histogram;
    QPixmap m_NoImageLoadedPixmap;

    an::ui::RequestTracker m_Previews;
    std::shared_ptr<const an::consumer::ImageAsset> m_PreviewImage;
    QPointer<ProgressDialogControl> m_Progress;

    std::uint64_t m_AddsOutstanding = 0u;
    std::uint64_t m_PreviewsAsked = 0u;
    std::uint64_t m_PreviewsLanded = 0u;
    std::uint64_t m_PreviewsShown = 0u;
    bool m_Running = false;
    bool m_IgnoreLastRowSelectedEvent = false;
    int m_CurrentSelectedRow = -1;
};
