// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TopLevelProcessBaseControl.h"
#include "processes/ImageIntegration/lite/LiteIntegrationProfile.h"
#include "processes/ImageIntegrationLiteProcessUiBuilder.h"

#include <QPoint>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>
#include <string>

class IntegrationDataModel;
class QWidget;

class AFTERNIGHTPROCESSES_API ImageIntegrationLiteProcess : public TopLevelProcessBaseControl
{
public:
    explicit ImageIntegrationLiteProcess(QWidget* parent = nullptr);
    ~ImageIntegrationLiteProcess() override;

    static std::string getProcessNameStatic();

    void refreshTheme() override;

    [[nodiscard]] IntegrationDataModel* model() const noexcept;

protected:
    [[nodiscard]] std::string serialize_json_settings() override;
    void deserialize_json_settings(const std::string& json_settings) override;

private:
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
    /// The output directories whose earlier-version notice this window has shown.
};
