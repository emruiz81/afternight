// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Stacking: a list of files grouped by filter, session and exposure, each group stacked into one image by the
/// engine with the window's combination, rejection, weighting and normalization, written to the output directory under
/// the name template's name with its rejection maps beside it, and opened when asked.
///
/// **Nothing waits on the GUI thread.** The files' headers are probed on the thread pool (`WorkflowFileList`), the
/// preview reads the selected file on the view's own workers, the weighting banner plans the weights from the
/// registration database or the frames' exposures on the thread pool and shows only its latest answer - which frames
/// lack their figure - and every group is stacked on the thread pool through
/// `stackStandaloneGroup`, each in its own admitted operation. A stacked image opens in a view on the GUI thread once
/// it is written, and when the run ends the window's message says what the weights and the engine warned of each
/// group's stack.
///
/// **A closed window waits for nothing.** Closing stops the run and the imports, and what they would land is dropped;
/// a stopped group leaves no file. The local normalization settings dialog is the integration's, opened through
/// `openLocalNormalizationSettings` and never run.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TopLevelProcessBaseControl.h"
#include "processes/ImageStackingProcessUiBuilder.h"
#include "processes/StackingBatch.h"
#include "processes/core/StandaloneStackingUtils.h"

#include <QString>
#include <QStringList>

#include <memory>
#include <string>
#include <vector>

class ImageViewControl;
class QAbstractButton;

namespace an::ui::processes
{
class WorkflowFileList;
} // namespace an::ui::processes

/// The Image Stacking window.
class AFTERNIGHTPROCESSES_API ImageStackingProcess : public TopLevelProcessBaseControl
{
public:
    explicit ImageStackingProcess(QWidget* parent = nullptr);
    ~ImageStackingProcess() override;

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

    /// Stacks every group, as Stack Images does: refused while a run or an import is on its way.
    void stack();

    /// What a run would ask for, read from the window.
    [[nodiscard]] an::ui::processes::StandaloneStackRun runSettings() const;

    /// The groups the list makes under the window's grouping and name template.
    [[nodiscard]] const std::vector<an::ui::StandaloneStackingGroup>& groups() const;

    /// The local normalization settings the window holds.
    [[nodiscard]] const LocalNormalizationSettings& localNormalizationSettings() const;

    /// The file list.
    [[nodiscard]] const an::ui::processes::WorkflowFileList& fileList() const;

    /// The preview view.
    [[nodiscard]] ImageViewControl* previewView() const;

    /// Whether nothing the window started is on its way: no import, no run, no weighting check.
    [[nodiscard]] bool isIdle() const;

    /// The files the last run wrote, in the order it wrote them.
    [[nodiscard]] const QStringList& lastWrittenFiles() const;

    /// The window's controls.
    [[nodiscard]] an::ui::processes::ImageStackingProcessUiElements& ui();

protected:
    void closeEvent(QCloseEvent* evt) override;

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);
    void on_addFilesButtonPushed();
    void on_addFolderButtonPushed();
    void on_outputDirectoryButtonPushed();
    void on_stackModeChanged();
    void on_rejectionModeChanged();
    void on_normalizationModeChanged();
    void on_localNormalizationConfigureButtonPushed();

    void listChanged();
    void rowSelected(int row);
    void resetSettings();
    void rebuildGroupTable();
    void updateControls();
    void updateRejectionMapsPathLabel();
    void updateWeightingBanner();
    void runFinished();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
