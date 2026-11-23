// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TopLevelProcessBaseControl.h"
#include "processes/ImageIntegration/lite/LiteIntegrationProfile.h"
#include "processes/ImageIntegration/workflow/WorkflowRunObserver.h"
#include "processes/ImageIntegrationLiteProcessUiBuilder.h"

#include <QPoint>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class IntegrationDataModel;
class QWidget;
class WorkflowExecutor;
class WorkflowRunView;

/// Image Integration Lite: lights and optional masters on one page, sensible defaults, one button.
///
/// **A run it starts is shown in itself** (`showWorkflowRun`): the run's own view takes the process's place until the
/// run has ended and its Close is pressed, which gives the place back to the process. Shown as a page, it is its page
/// whole and the view takes the page's area; the page's primary action is Run Integration, and the page's Cancel is the
/// run's own. In a window of its own the view takes the place of what the window lays out.
class AFTERNIGHTPROCESSES_API ImageIntegrationLiteProcess : public TopLevelProcessBaseControl, public IWorkflowRunHost
{
public:
    explicit ImageIntegrationLiteProcess(QWidget* parent = nullptr);
    ~ImageIntegrationLiteProcess() override;

    static std::string getProcessNameStatic();

    void refreshTheme() override;

    [[nodiscard]] IntegrationDataModel* model() const noexcept;

    /// A page's primary action: the Run Integration button, pressed when it is offered and no run is on its way.
    bool triggerPrimaryAction() override;

    /// Stops the run on its way through the run's own view, as the view's Cancel does.
    bool cancelRun() override;

    /// Takes the view of a run that starts and shows it in the process's place: in the page's area as a page, and in
    /// a window of its own in place of what the window lays out.
    bool showWorkflowRun(WorkflowRunView* view) override;

    /// The view of the run the process shows - on its way, or ended and not yet closed; null with none.
    [[nodiscard]] WorkflowRunView* runView() const noexcept;

    /// States the stages a run this process starts goes through in place of the workflow's own: a pipeline of a
    /// suite's, whose stages read and write no image, run from the manifest the process prepares. None runs the
    /// workflow.
    void setRunPipeline(std::function<std::unique_ptr<WorkflowExecutor>()> pipeline);

protected:
    [[nodiscard]] std::string serialize_json_settings() override;
    void deserialize_json_settings(const std::string& json_settings) override;

    /// A run is shown in the page by its own view, which has its own Cancel: the page adds no row for it.
    [[nodiscard]] bool showsRunInItself() const override;

    /// As a page it reads across two columns, which the centre of the floor display holds whole: at the left the frames
    /// and what they are - the dataset over the list - and at the right what is set of them - the setup and the
    /// processing settings. In a window of its own it is one column, as it was.
    [[nodiscard]] std::unique_ptr<an::ui::ProcessPageLayout> makePageLayout() override;

private:
    /// Shows a run's view in a window of its own: what the window lays out is hidden while the view takes its place.
    bool showRunInItsBody(WorkflowRunView* view);
    /// Takes the view of a run that has ended away and gives its place back to the process.
    void dismissRun();
    void setupConnections();
    void onAddFiles();
    void onAddFolder();
    void onBrowseOutputDirectory();
    void onClearAll();
    void onExecute();
    void onOpenFullProcess();
    void onProcessingOptionsChanged();
    void onApplySelectedFrameRole(int targetRole);
    void onFrameTableContextMenu(const QPoint& position);
    void importPaths(const QStringList& paths);
    void importDirectory(const QString& directory);
    void startImport(const QStringList& paths, bool expandDirectories);
    void autoDetectMasters();
    void refreshModelDefaults();
    void updateProcessingOptionsControls();
    void readProcessingOptionsFromControls();
    void updateStackingControls();
    void updateRejectionParameterControls();
    void updateResourceControls();
    void restoreRememberedOutputDirectory();
    void updateProcessingSectionSummaries();
    void updateDrizzleControls();
    void updateSummary();
    void updateFrameTable();
    void updateReadiness();
    void updateFrameTypeControls();
    [[nodiscard]] bool hasNoCalibrationFrames() const;
    void setImportInProgress(bool importing);
    void showImportIssues(int importedCount, int skippedCount, const QString& details);

    std::unique_ptr<IntegrationDataModel> m_model;
    an::ui::processes::ImageIntegrationLiteProcessUiElements m_ui;
    ImageIntegrationLite::ProcessingOptions m_processingOptions;
    bool m_importInProgress = false;
    bool m_suppressFrameTableRefresh = false;

    /// The view of the run the process shows; null while it shows the process.
    QPointer<WorkflowRunView> m_runView;

    /// What a window of its own laid out and hid for a run's view, shown again when the view is closed.
    std::vector<QPointer<QWidget>> m_heldForRun;

    /// The stages a run goes through in place of the workflow's own; none runs the workflow.
    std::function<std::unique_ptr<WorkflowExecutor>()> m_runPipeline;
    /// The output directories whose earlier-version notice this window has shown.
};
