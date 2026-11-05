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

private:
    static constexpr int kFrameTabCount = 4;
    static constexpr int kLogicalTabCount = 8;
    static constexpr int kParentTabCount = 2;

    void closeEvent(QCloseEvent* evt) override;

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
    QString m_sessionName;
    bool m_ignoreOutputUpdates = false;
    bool m_importInProgress = false;
    bool m_skipRemoveConfirmationForSession = false;
    bool m_runRegistrationAllInProgress = false;
    /// The output directories whose earlier-version notice this window has shown.

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
