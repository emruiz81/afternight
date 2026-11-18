// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file ImageIntegrationProcess.h
 * @brief Main UI controller for Image Integration process.
 *
 * Orchestrates the complete image integration workflow UI including
 * frame management, calibration matching, registration, and stacking.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TopLevelProcessBaseControl.h"
#include "processes/ImageIntegration/ImageIntegrationProcessUiBuilder.h"
#include "processes/ImageIntegration/model/ValidationResult.h" // For FixTarget
#include "processes/ImageIntegration/model/FrameType.h"

#include <memory>
#include <array>
#include <vector>
#include <QObjectCleanupHandler>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

class IntegrationDataModel;
class AutosaveManager;
class IntegrationDatabase;
class QAction;
class QDialogButtonBox;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QMenu;
class QMenuBar;
class QPushButton;
class QTabWidget;
class QTimer;
class QUndoStack;
class ProjectManager;
class QVBoxLayout;
class QWidget;

class FrameTreeView;
class GroupTreeModel;
class ImageViewControl;
class GroupingOptionsWidget;
class TreeDisplayModeWidget;
class TreeContextMenuHandler;
class FrameTreeDragDropHandler;
class PresetManager;
class CalibrationTab;
class RegistrationTab;
class StackingTab;
class SummaryTab;
class FrameSourceTab;
struct GroupKey;
struct WorkflowManifest;

namespace ImageIntegration
{
class ImportProgressPanel;
} // namespace ImageIntegration

/**
 * @class ImageIntegrationProcess
 * @brief Main controller for the Image Integration process.
 *
 * Coordinates:
 * - Frame tree views for lights, darks, flats, biases
 * - Calibration, registration, stacking, and summary tabs
 * - Image preview panel and grouping options
 * - Workflow execution via WorkflowProgressController
 * - Session persistence via AutosaveManager and IntegrationDatabase
 *
 * Inherits from TopLevelProcessBaseControl for dock widget integration.
 * Uses a programmatic UI builder.
 *
 * ### As a page
 *
 * Its descriptor states a full page: a shell that shows processes as pages holds it whole in one, and in a shell
 * that does not - or made outside a launch - it is the window it has always been. As a page:
 *
 * - **Its header says its job.** It states a job heading to its page (`an::ui::JobHeading`): the session's name,
 *   its facts - its lights, their groups, the time they integrate, what their files weigh, the setup they say - the
 *   disk its runs write to, Undo and Redo as its acts and Execute Integration as its primary act. The facts are read
 *   from the model when it changes; what the disk adds is read on the thread pool.
 * - **Its menus are under the header's button**, the same six, and its key sequences are the page's own
 *   (`an::ui::PageShortcutScope`); Execute is its primary action, from either Enter key with Ctrl.
 * - **An import is shown in the page**, not in a dialog over the window: the import's panel stands over the stages,
 *   and the stages, the foot and every entry of the menus wait for it - as the window waits behind the dialog - while
 *   the rest of the application stays usable. Esc cancels the import and leaves the page; closing the page cancels it.
 * - **Closing it** is closing the window: the session saved at once, the settings stored, nothing asked.
 *
 * ### Intermediate artifacts and incremental reuse
 *
 * A run materializes calibrated, chromatic-aberration-corrected and registered frames alongside its
 * masters, and a later run reuses whichever of them are still current. Two rules govern that:
 *
 * - **Every generated file is complete by construction.** Payloads are written to a pending-write
 *   sibling and renamed into place only once closed cleanly, so a crash, a full disk or a failing
 *   save leaves the previous output untouched rather than half-replaced.
 * - **Reuse is decided from identity, not content.** Each generated FITS carries the manifest
 *   artifact identity it satisfies in its own header, so deciding whether to recompute costs a stat
 *   or a header read. The pixel payload is never re-read to answer that question.
 *
 * `docs/dev/IMAGE_INTEGRATION_ARTIFACT_VALIDATION.md` documents the contract, what it deliberately
 * does not detect, and the two hazards to respect when adding a new writer.
 */
class AFTERNIGHTPROCESSES_API ImageIntegrationProcess : public TopLevelProcessBaseControl
{
public:
    explicit ImageIntegrationProcess(QObject* parent = nullptr);

    static std::string getProcessNameStatic();

    void refreshTheme() override;

    ~ImageIntegrationProcess();

    // Project management (minimal public surface for tests).
    [[nodiscard]] IntegrationDataModel* model() const noexcept;
    void setProjectName(const QString& name);
    [[nodiscard]] QString projectName() const;
    [[nodiscard]] QString currentProjectPath() const;
    [[nodiscard]] QStringList recentProjects() const;

    /// Reads again what a run wrote into the output directory: the Registration tab's figures and the Stacking tab's
    /// groups and Frame Quality panel. Every run calls it when it ends, whatever its outcome, since a run that fails
    /// partway may already have written registration records.
    void refreshAfterRun();

    /// Writes a run's report again from the model its archive keeps, on the thread pool, and opens it. An archive
    /// that keeps no such model is told so in a message, and the report file it already holds is opened as it is.
    ///
    /// @param runDir the run's folder under the output directory's `history/`.
    void showRunReport(const QString& runDir);

    /// Writes a run's report as one file that opens on its own, on the thread pool, and names a failure in a message
    /// titled as the action.
    ///
    /// @param runDir the run's folder under the output directory's `history/`.
    /// @param destination the file to write.
    void saveReportAsOneFile(const QString& runDir, const QString& destination);

    /// The session database the window keeps its session in: the one outside any project unless `openSession` named
    /// another.
    [[nodiscard]] const QString& sessionDatabasePath() const noexcept;

    /// The name of the session the window shows: `last_session` unless `openSession` named another.
    [[nodiscard]] const QString& sessionName() const noexcept;

    /// Shows a session of a session database in place of the one the window shows: what that one holds now is saved
    /// first, and from then on the window saves into the session it opened. The session it shows already is shown as
    /// it is. Refused, said in a message and nothing changed, while a run or an import is on its way, and when the
    /// database or the session does not read.
    ///
    /// @return whether the window shows that session.
    bool openSession(const QString& databasePath, const QString& name);

    /// Runs Execute Integration, the process's primary action, when the job is ready for it and no run is on its way.
    ///
    /// @return whether it ran.
    bool triggerPrimaryAction() override;

private:
    static constexpr int kFrameTabCount = 4;
    static constexpr int kLogicalTabCount = 8;
    static constexpr int kParentTabCount = 2;

    void closeEvent(QCloseEvent* evt) override;
    void keyPressEvent(QKeyEvent* evt) override;
    void showEvent(QShowEvent* evt) override;
    void pageLayoutMade() override;

    /// Shows an import's panel at the head of the page and makes the page's own acts wait for the import.
    ImageIntegration::ImportProgressPanel* beginPageImport(int totalFiles);
    /// Takes an import's panel off the page and gives the page's acts back; nothing for a window of its own.
    void endPageImport();

    /// Says the job again for the page's header once the events at hand are done: its facts from the model and - when
    /// its lights or its output folder changed, or `readDisk` asks - what the disk adds, read off the GUI thread.
    void scheduleJobHeadingRefresh(bool readDisk = false);
    void refreshJobHeading();
    void publishJobHeading();

    std::string serialize_json_settings() override;
    void deserialize_json_settings(const std::string& json_settings) override;

    void initialize();
    void setupMenuBar();
    void setupShortcuts();
    void setupConnections();
    void setupUIConnections();
    void setupModelConnections();
    void setupTreeConnections();
    void setupTabs();
    void applySplitterDefaults();

    void onBrowseOutputDirectory();
    void onOutputDirectoryEdited(const QString& text);
    void onOutputDirectoryChanged(const QString& path);
    void refreshAfterOutputDirectoryChange();

    void onAddFiles();
    void onAddDirectory();
    void onClearSelected();
    void onClearAll();
    void onClearAllInTab(FrameType type);
    void onReset();

    void performImportWithProgress(const std::vector<QString>& filePaths, FrameType frameType,
                                   bool forceToType = false);
    void importFilesDialog(FrameType frameType, bool forceToType);
    void importFolderDialog(FrameType frameType, bool forceToType, bool recursive);
    void onImportProject();
    void onExportProject();
    void onSaveReportAsOneFile(const QString& runDir);
    void onOpenRecentProject(const QString& projectPath);
    void refreshRecentProjectsMenu();
    void persistRecentProjects();
    void onSavePreset();
    void onLoadPreset(const QString& name);
    void onDeletePreset(const QString& name);
    void updatePresetsMenu();
    void syncSelectionToModel(int treeIndex);
    void syncSelectionFromModel();
    void onExecute();
    void applyExecutionOptions(WorkflowManifest& manifest) const;
    void onRunIntegrationForGroup(const QString& integrationGroupKey);
    void finishRegistrationOnlyRun(bool success);
    void onRunRegistrationForGroup(const QString& registrationGroupKey);
    void onRunRegistrationForAllGroups();
    void updateButtonStates();
    void updateWindowTitle();
    /// Saves the session the window shows every few seconds from now on, and says when a save fails.
    void startAutosave();
    void onGoToGroup();
    void goToGroupDefinition(FrameType type, const GroupKey& key);
    void showKeyboardShortcutsHelp();

    void scheduleTabBadgeRefresh();
    void updateTabBadgesNow();
    void updateFrameTabLabels();

    void updateLightCalibrationPreview(const QString& selectedPath);
    void refreshLightCalibrationPreview();

    void onFixRequested(FixTarget target);
    void onFixWithGroupRequested(FixTarget target, const QString& groupKey);
    void changeFrameType(const QStringList& paths, FrameType newType);
    /// Moves the frames at `paths` to `newType`, once any calibration conflict the move causes was confirmed.
    void applyFrameTypeChange(const QStringList& paths, FrameType newType);
    void autoDetectMasters();

    // Helper to map flat index (0-7) to nested tabs
    void setCurrentLogicalIndex(int index);
    [[nodiscard]] int currentLogicalIndex() const;
    void setupTabIcons();

    [[nodiscard]] int currentFrameTabIndex() const;
    [[nodiscard]] FrameType currentFrameTypeForActions() const;
    [[nodiscard]] FrameSourceTab* frameSourceTabForIndex(int index) const;

    QAction* m_addFilesAction = nullptr;
    QAction* m_addDirectoryAction = nullptr;
    QAction* m_clearSelectedAction = nullptr;
    QAction* m_clearAllAction = nullptr;
    QAction* m_resetAction = nullptr;
    QAction* m_importProjectAction = nullptr;
    QAction* m_exportProjectAction = nullptr;
    QAction* m_executeAction = nullptr;

    QUndoStack* m_undoStack = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;

    std::array<FrameSourceTab*, kFrameTabCount> m_frameSourceTabs{};
    std::array<FrameTreeView*, kFrameTabCount> m_frameTrees{};
    TreeContextMenuHandler* m_contextMenuHandler = nullptr;
    FrameTreeDragDropHandler* m_dragDropHandler = nullptr;
    QPointer<QMenu> m_presetsMenu;
    QPointer<QMenu> m_recentProjectsMenu;
    QAction* m_clearRecentProjectsAction = nullptr;
    int m_lastFrameTabIndex = 0;

    QTimer* m_tabBadgeDebounce = nullptr;
    std::array<QLabel*, kLogicalTabCount> m_tabBadges{};
    std::array<QLabel*, kParentTabCount> m_parentTabBadges{};

    CalibrationTab* m_calibrationTab = nullptr;
    RegistrationTab* m_registrationTab = nullptr;
    StackingTab* m_stackingTab = nullptr;
    SummaryTab* m_summaryTab = nullptr;

    QObjectCleanupHandler m_modelBoundCleanup;
    std::unique_ptr<IntegrationDataModel> m_model;
    std::unique_ptr<IntegrationDatabase> m_database;
    std::unique_ptr<AutosaveManager> m_autosave;
    std::unique_ptr<PresetManager> m_presetManager;
    std::unique_ptr<ProjectManager> m_projectManager;
    QString m_sessionDatabasePath;
    QString m_sessionName;
    bool m_ignoreOutputUpdates = false;
    bool m_importInProgress = false;
    bool m_skipRemoveConfirmationForSession = false;
    bool m_runRegistrationAllInProgress = false;

    /// An import shown in the page: the band over the stages, the panel in it, and the acts that wait for it.
    QPointer<QWidget> m_pageImportBand;
    QPointer<ImageIntegration::ImportProgressPanel> m_pageImportPanel;
    std::vector<QPointer<QAction>> m_importHeldActions;

    /// What the page's header says of the job; made when the process becomes a page.
    struct JobHeadingState;
    std::unique_ptr<JobHeadingState> m_jobHeading;

    an::ui::processes::ImageIntegrationProcessUiElements m_ui;

    QVBoxLayout* verticalLayout = nullptr;
    QMenuBar* mainMenuBar = nullptr;
    QTabWidget* rootTabWidget = nullptr;
    QTabWidget* sourceTabWidget = nullptr;
    QWidget* biasTab = nullptr;
    QWidget* darkTab = nullptr;
    QWidget* flatTab = nullptr;
    QWidget* lightTab = nullptr;
    QVBoxLayout* verticalLayout_2 = nullptr;
    QVBoxLayout* verticalLayout_3 = nullptr;
    QVBoxLayout* verticalLayout_4 = nullptr;
    QVBoxLayout* verticalLayout_5 = nullptr;
    QTabWidget* workflowTabWidget = nullptr;
    QWidget* calibrationTab = nullptr;
    QWidget* registrationTab = nullptr;
    QWidget* stackingTab = nullptr;
    QWidget* summaryTab = nullptr;
    QLineEdit* outputDirectoryEdit = nullptr;
    QPushButton* outputDirectoryBrowseButton = nullptr;
    QDialogButtonBox* dialogButtonBox = nullptr;
};
