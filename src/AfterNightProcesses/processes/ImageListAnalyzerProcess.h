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
/// **A proxy is the file as it is shown.** Each file is read as the view reads it - a mosaic demosaiced by the one
/// demosaic setting - stretched by the view's automatic stretch into fractions of full scale and written as eight-bit
/// FITS, since linear samples narrowed to eight bits unstretched would be nearly black. The preview shows proxies
/// without stretching them again and the original files with the automatic stretch.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TopLevelProcessBaseControl.h"
#include "processes/ImageListAnalyzerProcessUiBuilder.h"

#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

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

    /// Writes a proxy of every file that has none, as Optimize For Fast Play does.
    void optimize();

    /// Opens Export Sequence over the original files, as the button does.
    void exportSequence();

    /// The file list.
    [[nodiscard]] const an::ui::processes::WorkflowFileList& fileList() const;

    /// The preview view.
    [[nodiscard]] ImageViewControl* previewView() const;

    /// Whether nothing the window started is on its way: no probe, no proxy, no preview sequence.
    [[nodiscard]] bool isIdle() const;

    /// Whether every listed file has a proxy.
    [[nodiscard]] bool imagesOptimized() const;

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
    void rebuildPreviewSequence();
    void updateOptimizationControls();
    void updateSummaryAndHistogram();
    void setBusy(bool busy);
    [[nodiscard]] bool busy() const;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
