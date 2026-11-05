// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "UndoCommands.h"

#include <array>
#include <utility>

namespace
{
QStringList framePaths(const std::vector<FrameInfo>& frames)
{
    QStringList paths;
    paths.reserve(static_cast<int>(frames.size()));
    for (const auto& frame : frames)
    {
        paths.push_back(frame.id.canonicalPath());
    }
    return paths;
}

FrameMetadata remapMetadataType(const FrameMetadata& meta, FrameType type)
{
    return FrameMetadata(type, meta.dimensions(), meta.exposureSeconds(), meta.temperatureC(), meta.isoGain(),
                         meta.offset(), meta.filter(), meta.objectName(), meta.capturedAtUtc(), meta.readoutMode());
}

FrameInfo remapFrameInfoType(const FrameInfo& info, FrameType type)
{
    if (info.metadata.type() == type)
    {
        return info;
    }

    FrameMetadata metadata = remapMetadataType(info.metadata, type);
    return FrameInfo(info.id, info.displayPath, metadata, info.userSession, info.customTag, info.excluded,
                     info.manualGroupKey, info.userObjectName);
}

std::optional<FrameType> findFrameTypeForPath(const IntegrationDataModel* model, const QString& path)
{
    if (!model || path.trimmed().isEmpty())
    {
        return std::nullopt;
    }

    const FrameId id(path);
    for (FrameType type : {FrameType::Bias, FrameType::Dark, FrameType::Flat, FrameType::Light})
    {
        if (model->frames(type).findById(id).has_value())
        {
            return type;
        }
    }

    return std::nullopt;
}

void applyExcludedAttributeChanges(IntegrationDataModel* model, const std::vector<FrameAttributeChange>& changes,
                                   bool useNewValue)
{
    if (!model)
    {
        return;
    }

    std::array<QStringList, kFrameTypeCount> includePaths;
    std::array<QStringList, kFrameTypeCount> excludePaths;

    for (const auto& change : changes)
    {
        const bool targetExcluded = (useNewValue ? change.newValue : change.oldValue).toBool();
        const auto type = findFrameTypeForPath(model, change.path);
        if (!type.has_value())
        {
            model->updateFrameAttribute(change.path, FrameAttribute::Excluded, targetExcluded);
            continue;
        }

        auto& bucket = targetExcluded ? excludePaths[static_cast<size_t>(indexFor(*type))]
                                      : includePaths[static_cast<size_t>(indexFor(*type))];
        bucket.push_back(change.path);
    }

    for (int i = 0; i < kFrameTypeCount; ++i)
    {
        const FrameType type = frameTypeForIndex(i);
        if (!includePaths[static_cast<size_t>(i)].isEmpty())
        {
            model->setFramesExcluded(type, includePaths[static_cast<size_t>(i)], false);
        }
        if (!excludePaths[static_cast<size_t>(i)].isEmpty())
        {
            model->setFramesExcluded(type, excludePaths[static_cast<size_t>(i)], true);
        }
    }
}

void applyStackExclusionChanges(IntegrationDataModel* model, const GroupKey& integrationGroupKey,
                                const std::vector<StackExclusionChange>& changes, bool useNewValue)
{
    if (!model)
    {
        return;
    }

    QStringList includePaths;
    QStringList excludePaths;
    includePaths.reserve(static_cast<int>(changes.size()));
    excludePaths.reserve(static_cast<int>(changes.size()));

    for (const auto& change : changes)
    {
        const bool targetExcluded = useNewValue ? change.newExcluded : change.oldExcluded;
        if (targetExcluded)
        {
            excludePaths.push_back(change.path);
        }
        else
        {
            includePaths.push_back(change.path);
        }
    }

    model->beginBatchUpdate();
    if (!includePaths.isEmpty())
    {
        model->setFramesStackExcluded(integrationGroupKey, includePaths, false);
    }
    if (!excludePaths.isEmpty())
    {
        model->setFramesStackExcluded(integrationGroupKey, excludePaths, true);
    }
    model->endBatchUpdate();
}
} // namespace

RemoveFramesCommand::RemoveFramesCommand(IntegrationDataModel* model, FrameType type, std::vector<FrameInfo> frames,
                                         QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_type(type), m_frames(std::move(frames))
{
    QString text;
    if (m_frames.size() == 1)
        text = QObject::tr("Remove frame");
    else
        text = QObject::tr("Remove %1 frames").arg(m_frames.size());
    setText(text);
}

void RemoveFramesCommand::undo()
{
    if (!m_model)
        return;

    // Restore frames
    m_model->beginBatchUpdate();
    m_model->addFrames(m_type, m_frames);
    m_model->endBatchUpdate();
}

void RemoveFramesCommand::redo()
{
    if (!m_model)
        return;

    QStringList paths;
    paths.reserve(m_frames.size());
    for (const auto& frame : m_frames)
    {
        // Use FrameId's canonicalPath for consistency with removeFrames() lookup
        paths.append(frame.id.canonicalPath());
    }

    // Remove frames
    // Note: We use the paths to remove. The model handles finding them by ID/Path.
    m_model->removeFrames(m_type, paths);
}

ClearAllCommand::ClearAllCommand(IntegrationDataModel* model, std::vector<FrameInfo> allFrames, QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_allFrames(std::move(allFrames))
{
    setText(QObject::tr("Clear all frames"));
}

void ClearAllCommand::undo()
{
    if (!m_model)
        return;

    // Restore all frames by type
    m_model->beginBatchUpdate();
    for (const auto& frame : m_allFrames)
    {
        m_model->addFrames(frame.type(), {frame});
    }
    m_model->endBatchUpdate();
}

void ClearAllCommand::redo()
{
    if (!m_model)
        return;

    // Remove all frames by type
    QStringList biasPaths, darkPaths, flatPaths, lightPaths;
    for (const auto& frame : m_allFrames)
    {
        const QString path = frame.id.canonicalPath();
        switch (frame.type())
        {
        case FrameType::Bias:
            biasPaths << path;
            break;
        case FrameType::Dark:
            darkPaths << path;
            break;
        case FrameType::Flat:
            flatPaths << path;
            break;
        case FrameType::Light:
            lightPaths << path;
            break;
        }
    }

    m_model->beginBatchUpdate();
    if (!biasPaths.isEmpty())
        m_model->removeFrames(FrameType::Bias, biasPaths);
    if (!darkPaths.isEmpty())
        m_model->removeFrames(FrameType::Dark, darkPaths);
    if (!flatPaths.isEmpty())
        m_model->removeFrames(FrameType::Flat, flatPaths);
    if (!lightPaths.isEmpty())
        m_model->removeFrames(FrameType::Light, lightPaths);
    m_model->endBatchUpdate();
}

// --- SetGroupingOptionsCommand ---

SetGroupingOptionsCommand::SetGroupingOptionsCommand(IntegrationDataModel* model, FrameType type,
                                                     GroupingOptions oldOptions, GroupingOptions newOptions,
                                                     QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_type(type), m_oldOptions(std::move(oldOptions)),
      m_newOptions(std::move(newOptions))
{
    setText(QObject::tr("Change grouping options"));
}

void SetGroupingOptionsCommand::undo()
{
    if (!m_model)
        return;
    m_model->setGroupingOptions(m_type, m_oldOptions);
}

void SetGroupingOptionsCommand::redo()
{
    if (!m_model)
        return;
    m_model->setGroupingOptions(m_type, m_newOptions);
}

// --- SetCalibrationOverrideCommand ---

SetCalibrationOverrideCommand::SetCalibrationOverrideCommand(IntegrationDataModel* model, LightGroupKey key,
                                                             CalibrationOverrideType overrideType, QString oldPath,
                                                             QString newPath, QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_key(std::move(key)), m_overrideType(overrideType),
      m_oldPath(std::move(oldPath)), m_newPath(std::move(newPath))
{
    QString typeStr;
    switch (overrideType)
    {
    case CalibrationOverrideType::Bias:
        typeStr = QObject::tr("bias");
        break;
    case CalibrationOverrideType::Dark:
        typeStr = QObject::tr("dark");
        break;
    case CalibrationOverrideType::Flat:
        typeStr = QObject::tr("flat");
        break;
    }

    if (m_newPath.isEmpty())
        setText(QObject::tr("Clear %1 override").arg(typeStr));
    else if (m_oldPath.isEmpty())
        setText(QObject::tr("Set %1 override").arg(typeStr));
    else
        setText(QObject::tr("Change %1 override").arg(typeStr));
}

void SetCalibrationOverrideCommand::undo()
{
    if (!m_model)
        return;

    if (m_oldPath.isEmpty())
        m_model->clearCalibrationOverride(m_key, m_overrideType);
    else
        m_model->setCalibrationOverride(m_key, m_overrideType, m_oldPath);
}

void SetCalibrationOverrideCommand::redo()
{
    if (!m_model)
        return;

    if (m_newPath.isEmpty())
        m_model->clearCalibrationOverride(m_key, m_overrideType);
    else
        m_model->setCalibrationOverride(m_key, m_overrideType, m_newPath);
}

AddFramesCommand::AddFramesCommand(IntegrationDataModel* model, FrameType type, std::vector<FrameInfo> frames,
                                   QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_type(type), m_frames(std::move(frames))
{
    setText(m_frames.size() == 1 ? QObject::tr("Add frame") : QObject::tr("Add %1 frames").arg(m_frames.size()));
}

void AddFramesCommand::undo()
{
    if (!m_model)
    {
        return;
    }

    m_model->removeFrames(m_type, framePaths(m_frames));
}

void AddFramesCommand::redo()
{
    if (!m_model)
    {
        return;
    }

    m_model->beginBatchUpdate();
    m_model->addFrames(m_type, m_frames);
    m_model->endBatchUpdate();
}

AddExternalMasterCommand::AddExternalMasterCommand(IntegrationDataModel* model, FrameType type, FrameInfo master,
                                                   QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_type(type), m_path(master.displayPath), m_master(std::move(master))
{
    setText(QObject::tr("Add external master"));
}

void AddExternalMasterCommand::undo()
{
    if (!m_model || m_path.trimmed().isEmpty())
    {
        return;
    }

    m_model->removeFrames(m_type, {m_path});
}

void AddExternalMasterCommand::redo()
{
    if (!m_model || m_path.trimmed().isEmpty())
    {
        return;
    }

    m_model->addExternalMaster(m_type, m_master);
}

SetFrameAttributesCommand::SetFrameAttributesCommand(IntegrationDataModel* model, FrameAttribute attribute,
                                                     std::vector<FrameAttributeChange> changes, QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_attribute(attribute), m_changes(std::move(changes))
{
    setText(QObject::tr("Update frame attributes"));
}

void SetFrameAttributesCommand::undo()
{
    if (!m_model)
    {
        return;
    }

    if (m_attribute == FrameAttribute::Excluded)
    {
        applyExcludedAttributeChanges(m_model, m_changes, false);
        return;
    }

    m_model->beginBatchUpdate();
    for (const auto& change : m_changes)
    {
        m_model->updateFrameAttribute(change.path, m_attribute, change.oldValue);
    }
    m_model->endBatchUpdate();
}

void SetFrameAttributesCommand::redo()
{
    if (!m_model)
    {
        return;
    }

    if (m_attribute == FrameAttribute::Excluded)
    {
        applyExcludedAttributeChanges(m_model, m_changes, true);
        return;
    }

    m_model->beginBatchUpdate();
    for (const auto& change : m_changes)
    {
        m_model->updateFrameAttribute(change.path, m_attribute, change.newValue);
    }
    m_model->endBatchUpdate();
}

SetStackExclusionsCommand::SetStackExclusionsCommand(IntegrationDataModel* model, GroupKey integrationGroupKey,
                                                     std::vector<StackExclusionChange> changes, QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_integrationGroupKey(std::move(integrationGroupKey)),
      m_changes(std::move(changes))
{
    setText(m_changes.size() == 1 ? QObject::tr("Update stack exclusion") : QObject::tr("Update stack exclusions"));
}

void SetStackExclusionsCommand::undo()
{
    applyStackExclusionChanges(m_model, m_integrationGroupKey, m_changes, false);
}

void SetStackExclusionsCommand::redo()
{
    applyStackExclusionChanges(m_model, m_integrationGroupKey, m_changes, true);
}

SetMasterStatusCommand::SetMasterStatusCommand(IntegrationDataModel* model, QString path, bool oldIsMaster,
                                               MasterSource oldSource, bool newIsMaster, MasterSource newSource,
                                               QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_path(std::move(path)), m_oldIsMaster(oldIsMaster), m_oldSource(oldSource),
      m_newIsMaster(newIsMaster), m_newSource(newSource)
{
    setText(newIsMaster ? QObject::tr("Set master frame") : QObject::tr("Unset master frame"));
}

void SetMasterStatusCommand::undo()
{
    if (!m_model)
    {
        return;
    }

    m_model->setMasterStatus(m_path, m_oldIsMaster, m_oldSource);
}

void SetMasterStatusCommand::redo()
{
    if (!m_model)
    {
        return;
    }

    m_model->setMasterStatus(m_path, m_newIsMaster, m_newSource);
}

SetRegistrationReferenceCommand::SetRegistrationReferenceCommand(IntegrationDataModel* model, RegistrationGroupKey key,
                                                                 std::optional<FrameId> oldReference,
                                                                 std::optional<FrameId> newReference,
                                                                 QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_key(std::move(key)), m_oldReference(std::move(oldReference)),
      m_newReference(std::move(newReference))
{
    setText(QObject::tr("Set registration reference"));
}

void SetRegistrationReferenceCommand::undo()
{
    if (!m_model)
    {
        return;
    }
    m_model->setRegistrationReferenceFrame(m_key, m_oldReference);
}

void SetRegistrationReferenceCommand::redo()
{
    if (!m_model)
    {
        return;
    }
    m_model->setRegistrationReferenceFrame(m_key, m_newReference);
}

ClearAllCalibrationOverridesCommand::ClearAllCalibrationOverridesCommand(IntegrationDataModel* model,
                                                                         std::vector<CalibrationOverride> oldOverrides,
                                                                         QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_oldOverrides(std::move(oldOverrides))
{
    setText(QObject::tr("Clear calibration overrides"));
}

void ClearAllCalibrationOverridesCommand::undo()
{
    if (!m_model)
    {
        return;
    }

    m_model->beginBatchUpdate();
    m_model->clearAllCalibrationOverrides();
    for (const auto& entry : m_oldOverrides)
    {
        m_model->setCalibrationOverride(entry.lightKey, entry.type, entry.masterPath);
    }
    m_model->endBatchUpdate();
}

void ClearAllCalibrationOverridesCommand::redo()
{
    if (!m_model)
    {
        return;
    }
    m_model->clearAllCalibrationOverrides();
}

ClearFrameQualityMetricsCommand::ClearFrameQualityMetricsCommand(
    IntegrationDataModel* model, std::vector<FrameId> ids,
    std::vector<std::pair<FrameId, FrameQualityMetrics>> oldMetrics, QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_ids(std::move(ids)), m_oldMetrics(std::move(oldMetrics))
{
    setText(QObject::tr("Clear registration metrics"));
}

void ClearFrameQualityMetricsCommand::undo()
{
    if (!m_model)
    {
        return;
    }

    for (const auto& [id, metrics] : m_oldMetrics)
    {
        m_model->setFrameQualityMetrics(id, metrics);
    }
}

void ClearFrameQualityMetricsCommand::redo()
{
    if (!m_model)
    {
        return;
    }

    m_model->clearFrameQualityMetrics(m_ids);
}

SetStackingSettingsCommand::SetStackingSettingsCommand(IntegrationDataModel* model, FrameType type, GroupKey key,
                                                       StackingSettings oldSettings, StackingSettings newSettings,
                                                       QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_type(type), m_key(std::move(key)), m_oldSettings(std::move(oldSettings)),
      m_newSettings(std::move(newSettings))
{
    setText(QObject::tr("Update stacking settings"));
}

void SetStackingSettingsCommand::undo()
{
    if (!m_model)
    {
        return;
    }
    m_model->setStackingSettings(m_type, m_key, m_oldSettings);
}

void SetStackingSettingsCommand::redo()
{
    if (!m_model)
    {
        return;
    }
    m_model->setStackingSettings(m_type, m_key, m_newSettings);
}

SetRegistrationSettingsCommand::SetRegistrationSettingsCommand(IntegrationDataModel* model, GroupKey key,
                                                               RegistrationSettings oldSettings,
                                                               RegistrationSettings newSettings, QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_key(std::move(key)), m_oldSettings(std::move(oldSettings)),
      m_newSettings(std::move(newSettings))
{
    setText(QObject::tr("Update registration settings"));
}

void SetRegistrationSettingsCommand::undo()
{
    if (!m_model)
    {
        return;
    }
    m_model->setRegistrationSettings(m_key, m_oldSettings);
}

void SetRegistrationSettingsCommand::redo()
{
    if (!m_model)
    {
        return;
    }
    m_model->setRegistrationSettings(m_key, m_newSettings);
}

SetLightCalibrationSettingsCommand::SetLightCalibrationSettingsCommand(IntegrationDataModel* model, GroupKey key,
                                                                       LightCalibrationSettings oldSettings,
                                                                       LightCalibrationSettings newSettings,
                                                                       QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_key(std::move(key)), m_oldSettings(std::move(oldSettings)),
      m_newSettings(std::move(newSettings))
{
    setText(QObject::tr("Update light calibration settings"));
}

void SetLightCalibrationSettingsCommand::undo()
{
    if (!m_model)
    {
        return;
    }
    m_model->setLightCalibrationSettings(m_key, m_oldSettings);
}

void SetLightCalibrationSettingsCommand::redo()
{
    if (!m_model)
    {
        return;
    }
    m_model->setLightCalibrationSettings(m_key, m_newSettings);
}

SetCalibrationMatchingPolicyCommand::SetCalibrationMatchingPolicyCommand(IntegrationDataModel* model,
                                                                         CalibrationMatchingPolicy oldPolicy,
                                                                         CalibrationMatchingPolicy newPolicy,
                                                                         QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_oldPolicy(std::move(oldPolicy)), m_newPolicy(std::move(newPolicy))
{
    setText(QObject::tr("Update calibration matching policy"));
}

void SetCalibrationMatchingPolicyCommand::undo()
{
    if (!m_model)
    {
        return;
    }
    m_model->setCalibrationMatchingPolicy(m_oldPolicy);
}

void SetCalibrationMatchingPolicyCommand::redo()
{
    if (!m_model)
    {
        return;
    }
    m_model->setCalibrationMatchingPolicy(m_newPolicy);
}

SetOutputDirectoryCommand::SetOutputDirectoryCommand(IntegrationDataModel* model, QString oldPath, QString newPath,
                                                     QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_oldPath(std::move(oldPath)), m_newPath(std::move(newPath))
{
    setText(QObject::tr("Change output directory"));
}

void SetOutputDirectoryCommand::undo()
{
    if (!m_model)
    {
        return;
    }
    m_model->setOutputDirectory(m_oldPath);
}

void SetOutputDirectoryCommand::redo()
{
    if (!m_model)
    {
        return;
    }
    m_model->setOutputDirectory(m_newPath);
}

SetLightIntegrationGroupingOptionsCommand::SetLightIntegrationGroupingOptionsCommand(IntegrationDataModel* model,
                                                                                     GroupingOptions oldOptions,
                                                                                     GroupingOptions newOptions,
                                                                                     QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_oldOptions(std::move(oldOptions)), m_newOptions(std::move(newOptions))
{
    setText(QObject::tr("Change integration grouping options"));
}

void SetLightIntegrationGroupingOptionsCommand::undo()
{
    if (!m_model)
    {
        return;
    }
    m_model->setLightIntegrationGroupingOptions(m_oldOptions);
}

void SetLightIntegrationGroupingOptionsCommand::redo()
{
    if (!m_model)
    {
        return;
    }
    m_model->setLightIntegrationGroupingOptions(m_newOptions);
}

SetLightCalibrationCacheModeCommand::SetLightCalibrationCacheModeCommand(IntegrationDataModel* model,
                                                                         LightCalibrationCacheMode oldMode,
                                                                         LightCalibrationCacheMode newMode,
                                                                         QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_oldMode(oldMode), m_newMode(newMode)
{
    setText(QObject::tr("Change calibration cache mode"));
}

void SetLightCalibrationCacheModeCommand::undo()
{
    if (!m_model)
    {
        return;
    }
    m_model->setLightCalibrationCacheMode(m_oldMode);
}

void SetLightCalibrationCacheModeCommand::redo()
{
    if (!m_model)
    {
        return;
    }
    m_model->setLightCalibrationCacheMode(m_newMode);
}

ChangeFrameTypeCommand::ChangeFrameTypeCommand(IntegrationDataModel* model, std::vector<FrameTypeChange> changes,
                                               QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_changes(std::move(changes))
{
    setText(QObject::tr("Change frame type"));
}

void ChangeFrameTypeCommand::undo()
{
    if (!m_model)
    {
        return;
    }

    m_model->beginBatchUpdate();
    for (const auto& change : m_changes)
    {
        m_model->removeFrames(change.newType, {change.oldInfo.id.canonicalPath()});
        m_model->addFrames(change.oldType, {change.oldInfo});
    }
    m_model->endBatchUpdate();
}

void ChangeFrameTypeCommand::redo()
{
    if (!m_model)
    {
        return;
    }

    m_model->beginBatchUpdate();
    for (const auto& change : m_changes)
    {
        m_model->removeFrames(change.oldType, {change.oldInfo.id.canonicalPath()});
        m_model->addFrames(change.newType, {remapFrameInfoType(change.oldInfo, change.newType)});
    }
    m_model->endBatchUpdate();
}

LoadPresetCommand::LoadPresetCommand(IntegrationDataModel* model, QJsonObject oldState, QJsonObject newState,
                                     QString presetName, QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_oldState(std::move(oldState)), m_newState(std::move(newState))
{
    setText(QObject::tr("Load preset '%1'").arg(presetName));
}

void LoadPresetCommand::undo()
{
    if (!m_model)
    {
        return;
    }
    m_model->fromJson(m_oldState);
}

void LoadPresetCommand::redo()
{
    if (!m_model)
    {
        return;
    }
    m_model->fromJson(m_newState);
}
