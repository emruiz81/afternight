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

class IntegrationDataModel;
class QWidget;
class WorkflowExecutor;
class WorkflowRunView;

/// Image Integration Lite: lights and optional masters on one page, sensible defaults, one button.
///
/// **Shown as a page** it is its page whole, and a run it starts is shown in the page, not in the Processing window
/// over the application (`showWorkflowRun`): the run's own view takes the process's place until the run has ended and
/// its Close is pressed, which gives the page back to the process. The page's primary action is Run Integration, and
/// the page's Cancel is the run's own. In a window of its own nothing of this happens: the Processing window shows the
/// run, as it always has.
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

    /// Stops the run on its way through the run's own view - the page's, or the Processing window's over a window of
    /// its own - as the view's Cancel does.
    bool cancelRun() override;

    /// Shown as a page, takes the view of a run that starts and shows it in the page, in the process's place; in a
    /// window of its own it takes none, and the Processing window opens.
    bool showWorkflowRun(WorkflowRunView* view) override;

    /// The view of the run the page shows - on its way, or ended and not yet closed; null in a window, and with none.
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
    /// Takes the view of a run that has ended off the page and gives the page back to the process.
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

    /// The view of the run the page shows; null in a window, and while the page shows the process.
    QPointer<WorkflowRunView> m_runView;

    /// The stages a run goes through in place of the workflow's own; none runs the workflow.
    std::function<std::unique_ptr<WorkflowExecutor>()> m_runPipeline;
    /// The output directories whose earlier-version notice this window has shown.
};
