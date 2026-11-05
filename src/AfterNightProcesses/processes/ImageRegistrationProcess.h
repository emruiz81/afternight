// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Registration: a list of frames registered to a reference by their stars, by phase correlation or on a comet,
/// each frame's figures shown beside it, frames excluded by hand, by a blink comparison or by quality thresholds, and
/// the registered frames aligned onto the canvas the alignment mode chooses and written to the output directory.
///
/// **Nothing waits on the GUI thread.** The frames' headers are probed on the thread pool (`WorkflowFileList`), the
/// preview reads the selected frame on the view's own workers, and the registration, the alignment, the Auto-Assign
/// detection and the restoring of a recorded registration run on the thread pool, each step admitted by the session;
/// what they find lands through the event loop on a window that still exists. The settings dialog, the blink
/// comparator and the comet registration are the integration's, reached through `WorkflowToolCompanions.h`.
///
/// **A registration is recorded where its outputs are.** The frames' stars, figures and transforms are written to the
/// registration database in the output directory under the integration's group key, so a later registration of the same
/// frames - here or in the integration - reuses them. A closed window stops its work and waits for none of it.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TopLevelProcessBaseControl.h"
#include "processes/ImageRegistrationProcessUiBuilder.h"
#include "processes/core/RegistrationMetricsPersistenceTypes.h"
#include "processes/core/WorkflowSettingsRecords.h"

#include <QString>
#include <QStringList>

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class ImageViewControl;
class QAbstractButton;
class QMenu;

namespace an::ui::processes
{
class WorkflowFileList;
} // namespace an::ui::processes

/// The Image Registration window.
class AFTERNIGHTPROCESSES_API ImageRegistrationProcess : public TopLevelProcessBaseControl
{
public:
    explicit ImageRegistrationProcess(QWidget* parent = nullptr);
    ~ImageRegistrationProcess() override;

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

    /// Registers every frame, as Register does.
    void registerFrames();

    /// Aligns the included registered frames, as Align does.
    void alignFrames();

    /// The settings a registration would run with, read from the window.
    [[nodiscard]] RegistrationSettings registrationSettings() const;

    /// Makes a row the reference.
    void setReferenceRow(int row);

    /// The reference's row, or -1.
    [[nodiscard]] int referenceRow() const;

    /// Excludes or includes frames by hand.
    void setFramesExcluded(const std::vector<QString>& paths, bool excluded);

    /// The frames left out, by hand or by a threshold, by their canonical identity.
    [[nodiscard]] std::unordered_set<QString> ignoredFrameIds() const;

    /// The frames as the registration database last held them.
    [[nodiscard]] const an::ui::RegisteredFrameSet& registeredFrames() const;

    /// Whether the recorded registration answers the current frames and settings.
    [[nodiscard]] bool registrationIsCurrent() const;

    /// The file list.
    [[nodiscard]] const an::ui::processes::WorkflowFileList& fileList() const;

    /// The preview view.
    [[nodiscard]] ImageViewControl* previewView() const;

    /// Whether nothing the window started is on its way.
    [[nodiscard]] bool isIdle() const;

    /// The status line's text.
    [[nodiscard]] QString statusText() const;

    /// The window's controls.
    [[nodiscard]] an::ui::processes::ImageRegistrationProcessUiElements& ui();

protected:
    void closeEvent(QCloseEvent* evt) override;

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);
    void on_addFilesButtonPushed();
    void on_addFolderButtonPushed();
    void on_outputDirectoryButtonPushed();
    void on_registrationMethodChanged();
    void on_referenceModeChanged();
    void on_advancedSettingsButtonPushed();
    void on_blinkCompareButtonPushed();
    void on_applyThresholdsButtonPushed();
    void on_resetThresholdsButtonPushed();
    void on_clearRegisteredDataButtonPushed();
    void on_removeFailedImagesButtonPushed();
    void on_nextFailedButtonPushed();

    void extendContextMenu(QMenu& menu, int row);
    void openPlateSolveForRow(int row);
    void listChanged();
    void rowSelected(int row);
    void applyRegistrationSettings(const RegistrationSettings& settings);
    void applyAutoReferenceSelection();
    void restoreRecordedRegistration();
    void refreshTable();
    void updateControls();
    void updateAutoSolveAndEnrich();
    void setStatus(const QString& text, const QString& tone);
    void resetSettings();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
