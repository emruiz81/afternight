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
#include "processes/ImageIntegration/workflow/WorkflowRunObserver.h"
#include "processes/ImageIntegration/model/ValidationResult.h" // For FixTarget
#include "processes/ImageIntegration/model/FrameType.h"

#include <memory>
#include <array>
#include <functional>
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
class WorkflowExecutor;
class WorkflowRunView;

namespace ImageIntegration
{
class ImportProgressPanel;
class StageListNavigator;
class StageNavigator;
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
 * - **Its stages are a list**, in place of the window's two levels of tabs (`widgets/StageNavigator.h`): a row for
 *   each frame source and each stage of the workflow, with the summary last, each saying where it stands by a mark and
 *   a line (`ui_models/IntegrationStageStates.h`) - from the job as it stands, from what its output folder holds of
 *   its last runs, read off the GUI thread, and from a run on its way. The pages are the widgets the tabs hold. The
 *   list's foot holds the output folder and the project file's acts; the window's foot, with its Close and Reset, is
 *   not shown. A page with a side panel keeps it as an aside of a fixed width, and the stacking page lays its
 *   execution options in two rows, so the page fits the centre of a main window.
 * - **A run is shown in the page**, not in the Processing window over the application (`showWorkflowRun`): the run's
 *   view (`widgets/WorkflowRunView.h`) takes the place of the stage's page, beside the list, whose rows follow the run.
 *   While the run is on its way the page's own acts wait for it, as its window waits behind the Processing window -
 *   the stages' pages, the list's foot and the menus' entries - but the stages can still be looked at, from the list
 *   and the Navigate menu, and the header's primary act, in place of Execute, is the way back to the run: "Show
 *   progress" with how far the run has come. Leaving the page leaves the run running. When the run ends the page's
 *   acts are given back and the view is shown, with how it ended, until it is closed.
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
class AFTERNIGHTPROCESSES_API ImageIntegrationProcess : public TopLevelProcessBaseControl,
                                                        public IWorkflowRunObserver,
                                                        public IWorkflowRunHost
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

    /// What moves between the pages: the window's tabs, or the stage list once the process is a page.
    [[nodiscard]] ImageIntegration::StageNavigator* stageNavigator() const noexcept;

    /// A run launched from here moved on: the stage list's rows follow it.
    void workflowRunProgressed(const WorkflowProgress& progress) override;

    /// Shows a run that starts in the page - its view where the stage's page is, the page's acts waiting for the run -
    /// and answers true; in a window of its own it answers false, and the run opens the Processing window.
    bool showWorkflowRun(WorkflowRunView* view) override;

    /// The view of the run the page shows or showed last, until it is closed; null in a window of its own.
    [[nodiscard]] WorkflowRunView* runView() const noexcept;

    /// Whether the page shows the run's view, and not a stage's page looked at meanwhile.
    [[nodiscard]] bool isRunShown() const;

    /// Starts a run of a manifest as it is, shown as any run launched from here is: for a caller that holds a manifest
    /// of its own, where Execute builds one from the job, plans it and asks first. The run goes through the stages
    /// `pipeline` makes when one is given - a suite's own - and through the application's otherwise.
    ///
    /// @return whether the run was started; false while another is on its way.
    bool startRun(const WorkflowManifest& manifest, std::function<std::unique_ptr<WorkflowExecutor>()> pipeline = {});

    /// Asks the run on its way to stop, as its Cancel does.
    bool cancelRun() override;

private:
    static constexpr int kFrameTabCount = 4;
    static constexpr int kLogicalTabCount = 8;

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

    /// Puts the pages under a list of the job's stages in place of the tabs, the output folder and the project file's
    /// acts at the list's foot, and arranges the pages for the centre of a main window. Once, as the process becomes
    /// a page.
    void presentStagesAsAList();
    /// What the process does when another page is shown, whatever shows it.
    void followNavigator();

    /// Makes the page's own acts wait for a run - the stages' pages, the list's foot, the menus' entries but the ones
    /// that move between the stages - and gives them back, with the run's view shown, once it has ended.
    void holdPageForRun();
    void releasePageAfterRun();
    /// Puts the view of a run that has ended away and shows the stage that was shown.
    void dismissRun();
    /// Says how far the run has come in the act that shows its progress.
    void updateShowRunAction();

    /// Reads again where the job's stages stand once the events at hand are done: the job's facts from the model, and
    /// what its output folder says of its runs, read off the GUI thread.
    void scheduleStageStatesRead();
    void readStageStates();
    void publishStageRows();

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

    /// Says again what the validation finds in each page: as the tabs' badges, or in the stage list's rows.
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

    // The pages by their index, 0 to 7, in the Navigate menu's order.
    void setCurrentLogicalIndex(int index);
    [[nodiscard]] int currentLogicalIndex() const;

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

    /// What moves between the pages, a child of this widget; and the same object while it is the stage list.
    ImageIntegration::StageNavigator* m_navigator = nullptr;
    ImageIntegration::StageListNavigator* m_stageList = nullptr;

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

    /// A run shown in the page: its view, the act that shows it again, and the acts that wait for it.
    QPointer<WorkflowRunView> m_runView;
    QAction* m_showRunAction = nullptr;
    std::vector<QPointer<QAction>> m_runHeldActions;
    bool m_runHolds = false;
    QPointer<QMenu> m_navigateMenu;
    QPointer<QMenu> m_helpMenu;

    /// What the page's header says of the job; made when the process becomes a page.
    struct JobHeadingState;
    std::unique_ptr<JobHeadingState> m_jobHeading;

    /// Where the job's stages stand, for the stage list's rows; made when the process becomes a page.
    struct StageStatesState;
    std::unique_ptr<StageStatesState> m_stageStates;

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
