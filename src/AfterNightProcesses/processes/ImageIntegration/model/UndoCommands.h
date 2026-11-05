// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QUndoCommand>
#include <vector>
#include <optional>
#include <utility>
#include <QJsonObject>
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/GroupingOptions.h"
#include "processes/ImageIntegration/model/CalibrationMatcher.h"

struct FrameAttributeChange
{
    QString path;
    QVariant oldValue;
    QVariant newValue;
};

struct StackExclusionChange
{
    QString path;
    bool oldExcluded = false;
    bool newExcluded = false;
};

struct FrameTypeChange
{
    FrameType oldType = FrameType::Light;
    FrameInfo oldInfo;
    FrameType newType = FrameType::Light;
};

class AFTERNIGHTPROCESSES_API RemoveFramesCommand : public QUndoCommand
{
public:
    RemoveFramesCommand(IntegrationDataModel* model, FrameType type, std::vector<FrameInfo> frames,
                        QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    FrameType m_type;
    std::vector<FrameInfo> m_frames;
};

class AFTERNIGHTPROCESSES_API ClearAllCommand : public QUndoCommand
{
public:
    ClearAllCommand(IntegrationDataModel* model, std::vector<FrameInfo> allFrames, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    std::vector<FrameInfo> m_allFrames;
};

class AFTERNIGHTPROCESSES_API SetGroupingOptionsCommand : public QUndoCommand
{
public:
    SetGroupingOptionsCommand(IntegrationDataModel* model, FrameType type, GroupingOptions oldOptions,
                              GroupingOptions newOptions, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    FrameType m_type;
    GroupingOptions m_oldOptions;
    GroupingOptions m_newOptions;
};

class AFTERNIGHTPROCESSES_API SetCalibrationOverrideCommand : public QUndoCommand
{
public:
    /// Set or change a calibration override
    SetCalibrationOverrideCommand(IntegrationDataModel* model, LightGroupKey key, CalibrationOverrideType overrideType,
                                  QString oldPath, QString newPath, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    LightGroupKey m_key;
    CalibrationOverrideType m_overrideType;
    QString m_oldPath;
    QString m_newPath;
};

class AFTERNIGHTPROCESSES_API AddFramesCommand : public QUndoCommand
{
public:
    AddFramesCommand(IntegrationDataModel* model, FrameType type, std::vector<FrameInfo> frames,
                     QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    FrameType m_type;
    std::vector<FrameInfo> m_frames;
};

class AFTERNIGHTPROCESSES_API AddExternalMasterCommand : public QUndoCommand
{
public:
    AddExternalMasterCommand(IntegrationDataModel* model, FrameType type, FrameInfo master,
                             QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    FrameType m_type;
    QString m_path;
    FrameInfo m_master;
};

class AFTERNIGHTPROCESSES_API SetFrameAttributesCommand : public QUndoCommand
{
public:
    SetFrameAttributesCommand(IntegrationDataModel* model, FrameAttribute attribute,
                              std::vector<FrameAttributeChange> changes, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    FrameAttribute m_attribute;
    std::vector<FrameAttributeChange> m_changes;
};

class SetStackExclusionsCommand : public QUndoCommand
{
public:
    SetStackExclusionsCommand(IntegrationDataModel* model, GroupKey integrationGroupKey,
                              std::vector<StackExclusionChange> changes, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model = nullptr;
    GroupKey m_integrationGroupKey;
    std::vector<StackExclusionChange> m_changes;
};

class AFTERNIGHTPROCESSES_API SetMasterStatusCommand : public QUndoCommand
{
public:
    SetMasterStatusCommand(IntegrationDataModel* model, QString path, bool oldIsMaster, MasterSource oldSource,
                           bool newIsMaster, MasterSource newSource, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    QString m_path;
    bool m_oldIsMaster = false;
    MasterSource m_oldSource = MasterSource::Unknown;
    bool m_newIsMaster = false;
    MasterSource m_newSource = MasterSource::Unknown;
};

class AFTERNIGHTPROCESSES_API SetRegistrationReferenceCommand : public QUndoCommand
{
public:
    SetRegistrationReferenceCommand(IntegrationDataModel* model, RegistrationGroupKey key,
                                    std::optional<FrameId> oldReference, std::optional<FrameId> newReference,
                                    QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    RegistrationGroupKey m_key;
    std::optional<FrameId> m_oldReference;
    std::optional<FrameId> m_newReference;
};

class AFTERNIGHTPROCESSES_API ClearAllCalibrationOverridesCommand : public QUndoCommand
{
public:
    ClearAllCalibrationOverridesCommand(IntegrationDataModel* model, std::vector<CalibrationOverride> oldOverrides,
                                        QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    std::vector<CalibrationOverride> m_oldOverrides;
};

class AFTERNIGHTPROCESSES_API ClearFrameQualityMetricsCommand : public QUndoCommand
{
public:
    ClearFrameQualityMetricsCommand(IntegrationDataModel* model, std::vector<FrameId> ids,
                                    std::vector<std::pair<FrameId, FrameQualityMetrics>> oldMetrics,
                                    QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    std::vector<FrameId> m_ids;
    std::vector<std::pair<FrameId, FrameQualityMetrics>> m_oldMetrics;
};

class AFTERNIGHTPROCESSES_API SetStackingSettingsCommand : public QUndoCommand
{
public:
    SetStackingSettingsCommand(IntegrationDataModel* model, FrameType type, GroupKey key, StackingSettings oldSettings,
                               StackingSettings newSettings, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    FrameType m_type;
    GroupKey m_key;
    StackingSettings m_oldSettings;
    StackingSettings m_newSettings;
};

class AFTERNIGHTPROCESSES_API SetRegistrationSettingsCommand : public QUndoCommand
{
public:
    SetRegistrationSettingsCommand(IntegrationDataModel* model, GroupKey key, RegistrationSettings oldSettings,
                                   RegistrationSettings newSettings, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    GroupKey m_key;
    RegistrationSettings m_oldSettings;
    RegistrationSettings m_newSettings;
};

class AFTERNIGHTPROCESSES_API SetLightCalibrationSettingsCommand : public QUndoCommand
{
public:
    SetLightCalibrationSettingsCommand(IntegrationDataModel* model, GroupKey key, LightCalibrationSettings oldSettings,
                                       LightCalibrationSettings newSettings, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    GroupKey m_key;
    LightCalibrationSettings m_oldSettings;
    LightCalibrationSettings m_newSettings;
};

class AFTERNIGHTPROCESSES_API SetCalibrationMatchingPolicyCommand : public QUndoCommand
{
public:
    SetCalibrationMatchingPolicyCommand(IntegrationDataModel* model, CalibrationMatchingPolicy oldPolicy,
                                        CalibrationMatchingPolicy newPolicy, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    CalibrationMatchingPolicy m_oldPolicy;
    CalibrationMatchingPolicy m_newPolicy;
};

class AFTERNIGHTPROCESSES_API SetOutputDirectoryCommand : public QUndoCommand
{
public:
    SetOutputDirectoryCommand(IntegrationDataModel* model, QString oldPath, QString newPath,
                              QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    QString m_oldPath;
    QString m_newPath;
};

class AFTERNIGHTPROCESSES_API SetLightIntegrationGroupingOptionsCommand : public QUndoCommand
{
public:
    SetLightIntegrationGroupingOptionsCommand(IntegrationDataModel* model, GroupingOptions oldOptions,
                                              GroupingOptions newOptions, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    GroupingOptions m_oldOptions;
    GroupingOptions m_newOptions;
};

class AFTERNIGHTPROCESSES_API SetLightCalibrationCacheModeCommand : public QUndoCommand
{
public:
    SetLightCalibrationCacheModeCommand(IntegrationDataModel* model, LightCalibrationCacheMode oldMode,
                                        LightCalibrationCacheMode newMode, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    LightCalibrationCacheMode m_oldMode = LightCalibrationCacheMode::Disk;
    LightCalibrationCacheMode m_newMode = LightCalibrationCacheMode::Disk;
};

class AFTERNIGHTPROCESSES_API ChangeFrameTypeCommand : public QUndoCommand
{
public:
    ChangeFrameTypeCommand(IntegrationDataModel* model, std::vector<FrameTypeChange> changes,
                           QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    std::vector<FrameTypeChange> m_changes;
};

class AFTERNIGHTPROCESSES_API LoadPresetCommand : public QUndoCommand
{
public:
    LoadPresetCommand(IntegrationDataModel* model, QJsonObject oldState, QJsonObject newState, QString presetName,
                      QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    IntegrationDataModel* m_model;
    QJsonObject m_oldState;
    QJsonObject m_newState;
};
