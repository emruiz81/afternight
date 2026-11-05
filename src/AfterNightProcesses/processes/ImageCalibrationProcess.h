// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Calibration: a list of light frames calibrated by an optional bias, dark and flat master and a pedestal, the
/// pixels the dark marks corrected when asked, and each light written to the output directory under the name and in
/// the format the window's output options give it.
///
/// **Nothing waits on the GUI thread.** The lights' headers are probed on the thread pool (`WorkflowFileList`), the
/// preview reads the selected light on the view's own workers, the masters and every light are read, calibrated and
/// written on the thread pool through `CalibrationBatch` - each light under an admitted operation - and the dark's
/// cosmetic estimate is counted there too. Each light's status lands in the table as it is written.
///
/// **A closed window waits for nothing.** Closing stops the run and the imports, and what they would land is dropped;
/// a stopped light leaves no file.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TopLevelProcessBaseControl.h"
#include "processes/CalibrationBatch.h"
#include "processes/ImageCalibrationProcessUiBuilder.h"

#include <QString>
#include <QStringList>

#include <memory>
#include <string>
#include <vector>

class ImageViewControl;
class QAbstractButton;
class QLineEdit;

namespace an::ui::processes
{
class WorkflowFileList;
} // namespace an::ui::processes

/// The Image Calibration window.
class AFTERNIGHTPROCESSES_API ImageCalibrationProcess : public TopLevelProcessBaseControl
{
public:
    explicit ImageCalibrationProcess(QWidget* parent = nullptr);
    ~ImageCalibrationProcess() override;

    static std::string getProcessNameStatic();

    void refreshTheme() override;

    /// The window's options and files, as the settings database keeps them.
    std::string serialize_json_settings() override;

    /// Restores the options and files a window kept.
    void deserialize_json_settings(const std::string& json_settings) override;

    /// Probes files on the thread pool and lists those not listed yet.
    void addItemsNonBlocking(const QStringList& items);

    /// Removes files from the list.
    void removeItems(const std::vector<QString>& items);

    /// Calibrates every listed light, as Calibrate does: refused while a run or an import is on its way, or with no
    /// output directory.
    void calibrate();

    /// Counts the pixels the dark marks as cold and hot, as Estimate does.
    void estimateCosmeticCorrection();

    /// What a run would ask for, read from the window.
    [[nodiscard]] an::ui::processes::CalibrationBatchSettings settings() const;

    /// The file list.
    [[nodiscard]] const an::ui::processes::WorkflowFileList& fileList() const;

    /// The preview view.
    [[nodiscard]] ImageViewControl* previewView() const;

    /// Whether nothing the window started is on its way: no import, no run, no estimate.
    [[nodiscard]] bool isIdle() const;

    /// The Status column's text for a light: "Pending", "Calibrated" or "Failed".
    [[nodiscard]] QString statusOf(const QString& path) const;

    /// The window's controls.
    [[nodiscard]] an::ui::processes::ImageCalibrationProcessUiElements& ui();

protected:
    void closeEvent(QCloseEvent* evt) override;

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);
    void on_addFilesButtonPushed();
    void on_addFolderButtonPushed();
    void on_outputDirectoryButtonPushed();
    void chooseMaster(QLineEdit* field, const char* context);

    void listChanged();
    void rowSelected(int row);
    void resetSettings();
    void updateControls();
    void runFinished();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
