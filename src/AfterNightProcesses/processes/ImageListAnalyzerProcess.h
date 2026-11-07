// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image List Analyzer: a list of image files shown one after another as a sequence - each file's header in the table,
/// its summary and histogram beside the preview - with an Optimize For Fast Play that writes a stretched eight-bit
/// proxy of every file, so the sequence plays without decoding or stretching a frame, and an Export Sequence over the
/// original files.
///
/// **Nothing waits on the GUI thread.** A file's header is probed on the thread pool (`WorkflowFileList`), the preview
/// sequence - an image-list manifest naming the files, or their proxies - is written and opened on the thread pool and
/// shown only while it is the one last asked for, the proxies are made on the thread pool under admitted operations,
/// and a window that is closed stops what it started and waits for none of it.
///
/// **A proxy is the file as the autostretch display shows it.** Each file is read as the view reads it - a mosaic
/// demosaiced by the one demosaic setting - stretched by the view's automatic stretch into fractions of full scale and
/// written as eight-bit FITS, since linear samples narrowed to eight bits unstretched would be nearly black. A proxy
/// keeps the stretch settings it was written through.
///
/// **The display mode keeps its meaning.** The preview plays the proxies only while it shows the autostretch display
/// with the settings they hold, and presents them as they are, since they are that display. The linear display, or
/// other settings, shows the original files, so the picture is always the one the mode selector names; the proxies
/// come back, unwritten, with the display they hold. Optimizing under other settings replaces every proxy. The linear
/// display needs no proxy to play: it plays the files themselves, whose samples nothing has to stretch.
///
/// **A file is measured once.** The statistics the preview's overlay shows and the histogram's counts are each file's,
/// measured from the file - never from its proxy - and kept while it is listed: by the optimization, which reads every
/// file anyway, or one file at a time as frames are shown, so a frame shown again, or played, is measured by nothing.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TopLevelProcessBaseControl.h"
#include "processes/ImageListAnalyzerProcessUiBuilder.h"
#include "widgets/HistogramDisplayData.h"

#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

class AutostretchParams;
class ImageViewControl;
class QAbstractButton;

namespace an::ui::processes
{
class WorkflowFileList;
} // namespace an::ui::processes

/// The Image List Analyzer window.
class AFTERNIGHTPROCESSES_API ImageListAnalyzerProcess : public TopLevelProcessBaseControl
{
public:
    explicit ImageListAnalyzerProcess(QWidget* parent = nullptr);

    static std::string getProcessNameStatic();

    void refreshTheme() override;

    /// The window's options and files, as the settings database keeps them.
    std::string serialize_json_settings() override;

    /// Restores the options and files a window kept.
    void deserialize_json_settings(const std::string& json_settings) override;

    /// Probes files on the thread pool and lists those not listed yet.
    void addItemsNonBlocking(const QStringList& items);

    /// Removes files from the list, and their proxies.
    void removeItems(const std::vector<QString>& items);

    /// Writes a proxy of every file that has none holding the preview's automatic stretch settings, as Optimize For
    /// Fast Play does.
    void optimize();

    /// Opens Export Sequence over the original files, as the button does.
    void exportSequence();

    /// The file list.
    [[nodiscard]] const an::ui::processes::WorkflowFileList& fileList() const;

    /// The preview view.
    [[nodiscard]] ImageViewControl* previewView() const;

    /// Whether nothing the window started is on its way: no probe, no proxy, no preview sequence, no change of the
    /// preview's display still to be followed.
    [[nodiscard]] bool isIdle() const;

    /// Whether every listed file has a proxy holding the preview's automatic stretch settings.
    [[nodiscard]] bool imagesOptimized() const;

    /// How many times a file was read and measured for its statistics, by the optimization or on its own.
    [[nodiscard]] std::uint64_t filesMeasured() const;

    /// The proxy of a file, or empty.
    [[nodiscard]] QString proxyPathOf(const QString& path) const;

    /// The image-list manifest the preview plays, or empty.
    [[nodiscard]] QString previewSequencePath() const;

    /// The text the optimization status shows.
    [[nodiscard]] QString optimizationStatusText() const;

    /// The window's controls.
    [[nodiscard]] an::ui::processes::ImageListAnalyzerProcessUiElements& ui();

    ~ImageListAnalyzerProcess() override;

protected:
    void closeEvent(QCloseEvent* evt) override;

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);
    void on_addFilesButtonPushed();
    void on_addFolderButtonPushed();

    void listChanged();
    void rowSelected(int row);
    void previewFrameShown();
    /// Takes the rate the preview's player shows as the window's, when a player shows one.
    void keepPlayerRate();
    /// The preview's display mode or automatic stretch settings changed: heard from the view while it changes them,
    /// followed from the event loop, once for every change heard until then.
    void previewDisplayChanged();
    /// Shows what the preview's display is now: the proxies in the autostretch display they hold, the original files
    /// in any other.
    void followPreviewDisplay();
    /// Whether proxies written through these stretch settings are the display the preview is in.
    [[nodiscard]] bool proxiesHoldTheDisplay(const AutostretchParams& stretch) const;
    /// Whether the preview plays the proxies: every file has one, holding the display the preview is in.
    [[nodiscard]] bool previewPlaysProxies() const;
    /// Clears the preview at once when it shows proxies that no longer hold its display.
    void dropProxiesOfAnotherDisplay();
    /// Shows the statistics of the frame shown from the event loop, once the sequence states which frame it is.
    void queueFrameStatistics();
    /// The listed file whose frame the preview shows, or empty.
    [[nodiscard]] QString shownFile() const;
    /// Shows the kept statistics of the frame shown, or nothing while its file is measured.
    void showFrameStatistics();
    /// Measures a file's statistics on the thread pool, unless another file is being measured.
    void measureStatistics(const QString& file, an::ui::HistogramResolution resolution);
    void rebuildPreviewSequence();
    void updateOptimizationControls();
    void updateSummaryAndHistogram();
    void setBusy(bool busy);
    [[nodiscard]] bool busy() const;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
