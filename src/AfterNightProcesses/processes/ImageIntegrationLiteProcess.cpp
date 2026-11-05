// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageIntegrationLiteProcess.h"

#include "app/FileDialogUtils.h"
#include "controls/JsonSerializer.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/ImageIntegration/core/FileImport.h"
#include "processes/ImageIntegration/core/ParallelExecutionUtils.h"
#include "processes/ImageIntegration/ImageIntegrationProcess.h"
#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionController.h"
#include "processes/ImageIntegration/workflow/WorkflowResultOpener.h"

#include <AfterNightPlatform/Logging.h>

#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidgetItem>
#include <QVariant>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

namespace
{
constexpr int kFramePathRole = Qt::UserRole + 1;
constexpr int kFrameTypeRole = Qt::UserRole + 2;
constexpr int kFrameRoleRole = Qt::UserRole + 3;
constexpr auto kLiteAddFilesDirectoryContext = "__IMAGE_INTEGRATION_LITE_ADD_FILES_DIRECTORY__";
constexpr auto kLiteAddFolderDirectoryContext = "__IMAGE_INTEGRATION_LITE_ADD_FOLDER_DIRECTORY__";
constexpr auto kLiteOutputDirectoryContext = "__IMAGE_INTEGRATION_LITE_OUTPUT_DIRECTORY__";

enum class LiteFrameRole
{
    Bias = 0,
    Dark = 1,
    Flat = 2,
    Light = 3,
    DarkFlat = 4
};

[[nodiscard]] FrameType frameTypeFromIndexValue(const int value)
{
    return frameTypeForIndex(value);
}

[[nodiscard]] LiteFrameRole liteFrameRoleFromValue(const int value)
{
    switch (value)
    {
    case static_cast<int>(LiteFrameRole::Bias):
        return LiteFrameRole::Bias;
    case static_cast<int>(LiteFrameRole::Dark):
        return LiteFrameRole::Dark;
    case static_cast<int>(LiteFrameRole::Flat):
        return LiteFrameRole::Flat;
    case static_cast<int>(LiteFrameRole::Light):
        return LiteFrameRole::Light;
    case static_cast<int>(LiteFrameRole::DarkFlat):
        return LiteFrameRole::DarkFlat;
    default:
        return LiteFrameRole::Light;
    }
}

[[nodiscard]] int valueForLiteFrameRole(const LiteFrameRole role)
{
    return static_cast<int>(role);
}

[[nodiscard]] FrameType frameTypeForLiteRole(const LiteFrameRole role)
{
    switch (role)
    {
    case LiteFrameRole::Bias:
        return FrameType::Bias;
    case LiteFrameRole::Dark:
    case LiteFrameRole::DarkFlat:
        return FrameType::Dark;
    case LiteFrameRole::Flat:
        return FrameType::Flat;
    case LiteFrameRole::Light:
        return FrameType::Light;
    }

    return FrameType::Light;
}

[[nodiscard]] int variantIntOr(const QVariant& value, const int fallback)
{
    bool ok = false;
    const int parsed = value.toInt(&ok);
    return ok ? parsed : fallback;
}

[[nodiscard]] double variantDoubleOr(const QVariant& value, const double fallback)
{
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    return ok ? parsed : fallback;
}

[[nodiscard]] std::optional<LiteFrameRole> optionalFrameRoleFromCombo(const QComboBox* combo)
{
    if (!combo)
    {
        return std::nullopt;
    }

    const int raw = variantIntOr(combo->currentData(), -1);
    if (raw < 0)
    {
        return std::nullopt;
    }

    return liteFrameRoleFromValue(raw);
}

[[nodiscard]] ImageIntegrationLite::NormalizationPreset normalizationPresetFromCombo(const QComboBox* combo)
{
    using ImageIntegrationLite::NormalizationPreset;
    if (!combo)
    {
        return NormalizationPreset::GlobalAdditiveScaling;
    }

    const int raw = variantIntOr(combo->currentData(), static_cast<int>(NormalizationPreset::GlobalAdditiveScaling));
    switch (raw)
    {
    case static_cast<int>(NormalizationPreset::None):
        return NormalizationPreset::None;
    case static_cast<int>(NormalizationPreset::GlobalAdditive):
        return NormalizationPreset::GlobalAdditive;
    case static_cast<int>(NormalizationPreset::GlobalAdditiveScaling):
        return NormalizationPreset::GlobalAdditiveScaling;
    case static_cast<int>(NormalizationPreset::GlobalMultiplicative):
        return NormalizationPreset::GlobalMultiplicative;
    case static_cast<int>(NormalizationPreset::GlobalMultiplicativeScaling):
        return NormalizationPreset::GlobalMultiplicativeScaling;
    case static_cast<int>(NormalizationPreset::Local):
        return NormalizationPreset::Local;
    default:
        return NormalizationPreset::GlobalAdditiveScaling;
    }
}

[[nodiscard]] ImageIntegrationLite::CalibrationPreset calibrationPresetFromCombo(const QComboBox* combo)
{
    using ImageIntegrationLite::CalibrationPreset;
    if (!combo)
    {
        return CalibrationPreset::Standard;
    }

    const int raw = variantIntOr(combo->currentData(), static_cast<int>(CalibrationPreset::Standard));
    switch (raw)
    {
    case static_cast<int>(CalibrationPreset::Standard):
        return CalibrationPreset::Standard;
    case static_cast<int>(CalibrationPreset::StrictMatching):
        return CalibrationPreset::StrictMatching;
    case static_cast<int>(CalibrationPreset::Adaptive):
        return CalibrationPreset::Adaptive;
    default:
        return CalibrationPreset::Standard;
    }
}

[[nodiscard]] bool liteStackMethodAllowsRejection(const QString& stackMethod)
{
    return stackMethod.trimmed().compare(QStringLiteral("Average"), Qt::CaseInsensitive) == 0;
}

[[nodiscard]] bool liteRejectionMethodHasParameters(const QString& rejectionMethod)
{
    const QString normalized = rejectionMethod.trimmed().toLower();
    return !normalized.isEmpty() && normalized != QStringLiteral("auto") && normalized != QStringLiteral("automatic") &&
           normalized != QStringLiteral("none");
}

void configureLiteRejectionSpinBox(QDoubleSpinBox* spin, const QString& rejectionMethod)
{
    if (!spin)
    {
        return;
    }

    const QString normalized = rejectionMethod.trimmed().toLower();
    const bool fractionParameter = normalized == QStringLiteral("percentile") || normalized == QStringLiteral("gesdt");
    spin->setDecimals(2);
    spin->setRange(0.01, fractionParameter ? 1.0 : 10.0);
    spin->setSingleStep(fractionParameter ? 0.01 : 0.1);
}

[[nodiscard]] bool liteOptionsAffectFrameTable(const ImageIntegrationLite::ProcessingOptions& previous,
                                               const ImageIntegrationLite::ProcessingOptions& current)
{
    return previous.calibrationPreset != current.calibrationPreset ||
           previous.darkOptimization != current.darkOptimization || previous.flatAutoLevel != current.flatAutoLevel;
}

void setComboCurrentText(QComboBox* combo, const QString& text)
{
    if (!combo)
    {
        return;
    }

    const int index = combo->findText(text, Qt::MatchFixedString);
    if (index >= 0)
    {
        combo->setCurrentIndex(index);
    }
}

void setComboCurrentData(QComboBox* combo, const QVariant& data)
{
    if (!combo)
    {
        return;
    }

    const int index = combo->findData(data);
    if (index >= 0)
    {
        combo->setCurrentIndex(index);
    }
}

[[nodiscard]] QString frameTypeLabel(const FrameType type)
{
    switch (type)
    {
    case FrameType::Bias:
        return QObject::tr("Bias");
    case FrameType::Dark:
        return QObject::tr("Dark");
    case FrameType::Flat:
        return QObject::tr("Flat");
    case FrameType::Light:
        return QObject::tr("Light");
    }

    return QObject::tr("Light");
}

[[nodiscard]] QString frameRoleLabel(const LiteFrameRole role)
{
    if (role == LiteFrameRole::DarkFlat)
    {
        return QObject::tr("Dark Flat");
    }

    return frameTypeLabel(frameTypeForLiteRole(role));
}

[[nodiscard]] LiteFrameRole roleForFrame(const FrameInfo& frame)
{
    if (ImageIntegrationLite::isDarkFlatFrame(frame))
    {
        return LiteFrameRole::DarkFlat;
    }

    return liteFrameRoleFromValue(indexFor(frame.type()));
}

[[nodiscard]] QString exposureLabel(const FrameInfo& frame)
{
    const double exposure = frame.metadata.exposureSeconds();
    if (!(exposure > 0.0))
    {
        return QStringLiteral("-");
    }

    const bool wholeSeconds = std::abs(exposure - std::round(exposure)) < 1e-6;
    return QStringLiteral("%1 s").arg(exposure, 0, 'f', wholeSeconds ? 0 : 2);
}

[[nodiscard]] QString compactListLabel(const QStringList& values)
{
    return values.isEmpty() ? QStringLiteral("-") : values.join(QStringLiteral(", "));
}

[[nodiscard]] QTableWidgetItem* makeReadOnlyItem(const QString& text, const QString& tooltip = QString())
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    if (!tooltip.isEmpty())
    {
        item->setToolTip(tooltip);
    }
    return item;
}

[[nodiscard]] FrameMetadata metadataWithType(const FrameMetadata& metadata, const FrameType type)
{
    return FrameMetadata(type, metadata.dimensions(), metadata.exposureSeconds(), metadata.temperatureC(),
                         metadata.isoGain(), metadata.offset(), metadata.filter(), metadata.objectName(),
                         metadata.capturedAtUtc(), metadata.readoutMode());
}

[[nodiscard]] FrameInfo frameInfoWithType(FrameInfo frame, const FrameType type)
{
    if (frame.type() == type)
    {
        return frame;
    }

    frame.metadata = metadataWithType(frame.metadata, type);
    frame.manualGroupKey.reset();
    return frame;
}

[[nodiscard]] FrameInfo frameInfoWithRole(FrameInfo frame, const LiteFrameRole role)
{
    frame = frameInfoWithType(std::move(frame), frameTypeForLiteRole(role));
    if (role == LiteFrameRole::DarkFlat)
    {
        frame.customTag = ImageIntegrationLite::darkFlatCustomTag();
    }
    else if (frame.customTag.compare(ImageIntegrationLite::darkFlatCustomTag(), Qt::CaseInsensitive) == 0)
    {
        frame.customTag.clear();
    }
    return frame;
}

[[nodiscard]] QString existingDirectoryOrEmpty(const QString& path)
{
    const QString trimmed = path.trimmed();
    return !trimmed.isEmpty() && QDir(trimmed).exists() ? trimmed : QString();
}

void applyRememberedDirectory(QFileDialog* dialog, const char* contextKey, const QString& fallback = {})
{
    if (!dialog || !contextKey)
    {
        return;
    }

    QString initialDirectory = existingDirectoryOrEmpty(fallback);
    if (initialDirectory.isEmpty())
    {
        initialDirectory = existingDirectoryOrEmpty(process_utils::selectPath(QString::fromUtf8(contextKey)));
    }
    if (!initialDirectory.isEmpty())
    {
        dialog->setDirectory(initialDirectory);
    }
}

[[nodiscard]] QString issueContextText(const ValidationIssue& issue)
{
    return issue.filePath.trimmed().isEmpty() ? QString() : QObject::tr(" (%1)").arg(issue.filePath);
}

[[nodiscard]] QString validationWarningsTooltip(const WorkflowValidationResult& validation)
{
    QStringList lines;
    for (const ValidationIssue& issue : validation.issues)
    {
        if (issue.severity != ValidationIssue::Severity::Warning)
        {
            continue;
        }

        lines << QStringLiteral("- %1%2").arg(issue.message, issueContextText(issue));
    }

    return lines.isEmpty() ? QString() : QObject::tr("Warnings:\n%1").arg(lines.join(QStringLiteral("\n")));
}

[[nodiscard]] QString outputPathForMaster(const QString& outputDirectory, const QString& outputFilename)
{
    if (outputDirectory.trimmed().isEmpty())
    {
        return outputFilename;
    }

    return QDir(outputDirectory).filePath(outputFilename);
}

struct CalibrationCell
{
    QString text = QStringLiteral("None");
    QString tooltip;
};

void appendUnique(QStringList& values, const QString& value)
{
    if (!value.trimmed().isEmpty() && !values.contains(value))
    {
        values << value;
    }
}

[[nodiscard]] QString masterLabel(const std::optional<FrameId>& master)
{
    return master.has_value() ? QFileInfo(master->canonicalPath()).fileName() : QString();
}

[[nodiscard]] QString stageGroupMasterPath(const QString& outputDirectory,
                                           const WorkflowManifest::CalibrationStage::Group& group)
{
    return group.existingMaster.has_value() ? group.existingMaster->canonicalPath()
                                            : outputPathForMaster(outputDirectory, group.outputFilename);
}

void registerCalibrationStageSources(const QString& outputDirectory,
                                     const std::optional<WorkflowManifest::CalibrationStage>& stage,
                                     std::map<QString, QStringList>& masterSources)
{
    if (!stage.has_value())
    {
        return;
    }

    for (const auto& group : stage->groups)
    {
        const QString masterPath = stageGroupMasterPath(outputDirectory, group);
        for (const FrameId& input : group.inputFiles)
        {
            appendUnique(masterSources[masterPath], input.canonicalPath());
        }
    }
}

void markMasterUse(const std::optional<FrameId>& master, const QString& usage,
                   const std::map<QString, QStringList>& masterSources, std::map<QString, QStringList>& frameUses)
{
    if (!master.has_value())
    {
        return;
    }

    const auto it = masterSources.find(master->canonicalPath());
    if (it == masterSources.end())
    {
        appendUnique(frameUses[master->canonicalPath()], usage);
        return;
    }

    for (const QString& source : it->second)
    {
        appendUnique(frameUses[source], usage);
    }
}

[[nodiscard]] CalibrationCell calibrationCell(const QStringList& parts, const QStringList& details,
                                              const QStringList& extraUses = {})
{
    CalibrationCell cell;
    if (!parts.isEmpty())
    {
        cell.text = parts.join(QStringLiteral(" + "));
    }

    QStringList tooltipLines = details;
    for (const QString& usage : extraUses)
    {
        appendUnique(tooltipLines, usage);
    }
    cell.tooltip = tooltipLines.join(QStringLiteral("\n"));
    return cell;
}

[[nodiscard]] std::map<QString, CalibrationCell> buildCalibrationCells(const WorkflowManifest& manifest)
{
    std::map<QString, CalibrationCell> cells;
    std::map<QString, QStringList> masterSources;
    std::map<QString, QStringList> frameUses;

    registerCalibrationStageSources(manifest.outputDirectory, manifest.biasStage, masterSources);
    registerCalibrationStageSources(manifest.outputDirectory, manifest.darkStage, masterSources);
    registerCalibrationStageSources(manifest.outputDirectory, manifest.flatStage, masterSources);

    for (const auto& params : manifest.lightsCalibration)
    {
        QStringList parts;
        QStringList details;
        if (params.biasPath.has_value())
        {
            appendUnique(parts, QObject::tr("Bias"));
            details << QObject::tr("Bias: %1").arg(masterLabel(params.biasPath));
            markMasterUse(params.biasPath, QObject::tr("Used by lights as bias"), masterSources, frameUses);
        }
        if (params.darkPath.has_value())
        {
            appendUnique(parts, QObject::tr("Dark"));
            details << QObject::tr("Dark: %1").arg(masterLabel(params.darkPath));
            markMasterUse(params.darkPath, QObject::tr("Used by lights as dark"), masterSources, frameUses);
        }
        if (params.flatPath.has_value())
        {
            appendUnique(parts, QObject::tr("Flat"));
            details << QObject::tr("Flat: %1").arg(masterLabel(params.flatPath));
            markMasterUse(params.flatPath, QObject::tr("Used by lights as flat"), masterSources, frameUses);
        }
        cells[params.lightPath.canonicalPath()] = calibrationCell(parts, details);
    }

    if (manifest.flatStage.has_value())
    {
        for (const auto& group : manifest.flatStage->groups)
        {
            QStringList parts;
            QStringList details;
            if (group.biasMaster.has_value())
            {
                appendUnique(parts, QObject::tr("Bias"));
                details << QObject::tr("Bias: %1").arg(masterLabel(group.biasMaster));
                markMasterUse(group.biasMaster, QObject::tr("Used by flats as bias"), masterSources, frameUses);
            }
            if (group.darkMaster.has_value())
            {
                appendUnique(parts, QObject::tr("Dark"));
                details << QObject::tr("Dark: %1").arg(masterLabel(group.darkMaster));
                markMasterUse(group.darkMaster, QObject::tr("Used by flats as dark"), masterSources, frameUses);
            }

            for (const FrameId& input : group.inputFiles)
            {
                cells[input.canonicalPath()] = calibrationCell(parts, details, frameUses[input.canonicalPath()]);
            }
        }
    }

    for (const auto& [path, uses] : frameUses)
    {
        auto cellIt = cells.find(path);
        if (cellIt == cells.end())
        {
            CalibrationCell cell;
            cell.text = uses.join(QStringLiteral(", "));
            cell.tooltip = uses.join(QStringLiteral("\n"));
            cells[path] = cell;
            continue;
        }

        CalibrationCell& cell = cellIt->second;
        QStringList tooltipLines = cell.tooltip.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& usage : uses)
        {
            appendUnique(tooltipLines, usage);
        }
        cell.tooltip = tooltipLines.join(QStringLiteral("\n"));
    }

    return cells;
}

[[nodiscard]] bool isSupportedImageFile(const QString& path)
{
    return ImageIntegration::detail::isReadableImagePath(path);
}

struct LiteImportBatch
{
    std::vector<FrameInfo> biasFrames;
    std::vector<FrameInfo> darkFrames;
    std::vector<FrameInfo> flatFrames;
    std::vector<FrameInfo> lightFrames;
    ImageIntegration::ImportResult importResult;
};

[[nodiscard]] QStringList collectImportCandidates(const QStringList& paths, const bool expandDirectories)
{
    QStringList candidates;
    for (const QString& path : paths)
    {
        const QFileInfo info(path);
        if (expandDirectories && info.isDir())
        {
            QDirIterator it(info.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext())
            {
                const QString candidate = it.next();
                if (isSupportedImageFile(candidate))
                {
                    candidates << candidate;
                }
            }
            continue;
        }

        candidates << path;
    }
    return candidates;
}

[[nodiscard]] LiteImportBatch buildLiteImportBatch(const an::consumer::EngineSession* engineSession,
                                                   const QStringList& paths, const bool expandDirectories,
                                                   const std::optional<LiteFrameRole> forcedRole)
{
    LiteImportBatch batch;
    std::vector<ImageIntegration::DetectedFrameInfo> detectedFrames;
    QSet<QString> seenPaths;

    for (const QString& path : collectImportCandidates(paths, expandDirectories))
    {
        const QString absolutePath = QFileInfo(path).absoluteFilePath();
        if (seenPaths.contains(absolutePath))
        {
            continue;
        }
        seenPaths.insert(absolutePath);

        if (!isSupportedImageFile(absolutePath))
        {
            ++batch.importResult.skippedCount;
            continue;
        }
        if (engineSession == nullptr)
        {
            batch.importResult.issues.push_back(
                ImageIntegration::ImportIssue{ImageIntegration::ImportIssueSeverity::Error, absolutePath,
                                              QStringLiteral("No engine session is available to read the file")});
            ++batch.importResult.skippedCount;
            continue;
        }

        const FrameType fallbackType = forcedRole.has_value() ? frameTypeForLiteRole(*forcedRole) : FrameType::Light;
        auto result = ImageIntegration::buildDetectedFrameInfoWithIssue(*engineSession, absolutePath, fallbackType);
        if (result.issue)
        {
            batch.importResult.issues.push_back(*result.issue);
        }
        if (result.detected)
        {
            if (forcedRole.has_value())
            {
                result.detected->type = frameTypeForLiteRole(*forcedRole);
                result.detected->info = frameInfoWithRole(std::move(result.detected->info), *forcedRole);
            }
            ++batch.importResult.importedCount;
            detectedFrames.push_back(std::move(*result.detected));
        }
        else
        {
            ++batch.importResult.skippedCount;
        }
    }

    ImageIntegration::sortDetectedFramesForImport(detectedFrames);
    for (auto& detected : detectedFrames)
    {
        switch (detected.type)
        {
        case FrameType::Bias:
            batch.biasFrames.push_back(std::move(detected.info));
            break;
        case FrameType::Dark:
            batch.darkFrames.push_back(std::move(detected.info));
            break;
        case FrameType::Flat:
            batch.flatFrames.push_back(std::move(detected.info));
            break;
        case FrameType::Light:
            batch.lightFrames.push_back(std::move(detected.info));
            break;
        }
    }

    return batch;
}
} // namespace

ImageIntegrationLiteProcess::ImageIntegrationLiteProcess(QWidget* parent)
    : TopLevelProcessBaseControl(getProcessNameStatic()), m_model(std::make_unique<IntegrationDataModel>(this))
{
    Q_UNUSED(parent);

    setObjectName(QStringLiteral("ImageIntegrationLiteProcess"));
    setAttribute(Qt::WA_DeleteOnClose);
    setProcessDescription(QStringLiteral("A simplified image integration workflow with filter-based grouping."));

    m_ui = an::ui::processes::buildImageIntegrationLiteProcessUi(this);
    m_processingOptions = ImageIntegrationLite::defaultProcessingOptions();
    updateProcessingOptionsControls();
    setupConnections();
    initializeTopLevelProcess();
    restoreRememberedOutputDirectory();
    refreshModelDefaults();
    updateSummary();
    updateFrameTable();
    updateReadiness();
}

ImageIntegrationLiteProcess::~ImageIntegrationLiteProcess() = default;

std::string ImageIntegrationLiteProcess::getProcessNameStatic()
{
    return "Image Integration Lite";
}

void ImageIntegrationLiteProcess::refreshTheme()
{
    TopLevelProcessBaseControl::refreshTheme();
    if (m_ui.statusBanner)
    {
        m_ui.statusBanner->refreshTheme();
    }
    if (m_ui.summaryCard)
    {
        m_ui.summaryCard->refreshTheme();
    }
    if (m_ui.setupCard)
    {
        m_ui.setupCard->refreshTheme();
    }
    if (m_ui.settingsCard)
    {
        m_ui.settingsCard->refreshTheme();
    }
    if (m_ui.calibrationSection)
    {
        m_ui.calibrationSection->refreshTheme();
    }
    if (m_ui.registrationSection)
    {
        m_ui.registrationSection->refreshTheme();
    }
    if (m_ui.normalizationSection)
    {
        m_ui.normalizationSection->refreshTheme();
    }
    if (m_ui.stackingSection)
    {
        m_ui.stackingSection->refreshTheme();
    }
    if (m_ui.framesCard)
    {
        m_ui.framesCard->refreshTheme();
    }

    an::ui::processes::configureImageIntegrationLiteSectionChrome(m_ui);
}

IntegrationDataModel* ImageIntegrationLiteProcess::model() const noexcept
{
    return m_model.get();
}

std::string ImageIntegrationLiteProcess::serialize_json_settings()
{
    an::ui::json_serializer::json doc;
    doc["version"] = 1;
    doc["params"]["outputDirectory"] = m_model ? m_model->outputDirectory().trimmed().toStdString() : std::string();
    return an::ui::json_serializer::dump_compact(doc);
}

void ImageIntegrationLiteProcess::deserialize_json_settings(const std::string& json_settings)
{
    const auto doc = an::ui::json_serializer::safe_parse(json_settings);
    if (!doc)
    {
        return;
    }

    const int version = doc->value("version", 0);
    if (version < 1)
    {
        return;
    }

    const auto paramsIt = doc->find("params");
    if (paramsIt == doc->end() || !paramsIt->is_object())
    {
        return;
    }

    const QString outputDirectory = QString::fromStdString(paramsIt->value("outputDirectory", std::string())).trimmed();
    if (!outputDirectory.isEmpty() && m_model)
    {
        m_model->setOutputDirectory(outputDirectory);
        process_utils::rememberPath(QString::fromUtf8(kLiteOutputDirectoryContext), outputDirectory);
    }
}

void ImageIntegrationLiteProcess::setupConnections()
{
    QObject::connect(m_ui.addFilesButton, &QPushButton::released, this, &ImageIntegrationLiteProcess::onAddFiles);
    QObject::connect(m_ui.addFolderButton, &QPushButton::released, this, &ImageIntegrationLiteProcess::onAddFolder);
    QObject::connect(m_ui.clearAllButton, &QPushButton::released, this, &ImageIntegrationLiteProcess::onClearAll);
    QObject::connect(m_ui.browseOutputButton, &QPushButton::released, this,
                     &ImageIntegrationLiteProcess::onBrowseOutputDirectory);
    QObject::connect(m_ui.stackMethodCombo, &QComboBox::currentIndexChanged, this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.rejectionMethodCombo, &QComboBox::currentIndexChanged, this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.rejectionLowSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.rejectionHighSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.weightingCombo, &QComboBox::currentIndexChanged, this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.transformationCombo, &QComboBox::currentIndexChanged, this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.normalizationCombo, &QComboBox::currentIndexChanged, this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.calibrationCombo, &QComboBox::currentIndexChanged, this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.darkOptimizationCheck, &QCheckBox::toggled, this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.flatAutoLevelCheck, &QCheckBox::toggled, this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.autoReferenceCheck, &QCheckBox::toggled, this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.autoCropCheck, &QCheckBox::toggled, this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.solveFinalMasterCheck, &QCheckBox::toggled, this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.drizzleEnabledCheck, &QCheckBox::toggled, this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.drizzleScaleCombo, &QComboBox::currentIndexChanged, this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.drizzlePixelFractionCombo, &QComboBox::currentIndexChanged, this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.limitMemoryCheck, &QCheckBox::toggled, this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.maxMemorySpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    QObject::connect(m_ui.maxParallelThreadsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                     &ImageIntegrationLiteProcess::onProcessingOptionsChanged);
    if (m_ui.changeFrameTypeButton)
    {
        auto* frameTypeMenu = new QMenu(m_ui.changeFrameTypeButton);
        const std::array<LiteFrameRole, 5> frameRoles = {LiteFrameRole::Light, LiteFrameRole::Flat, LiteFrameRole::Dark,
                                                         LiteFrameRole::DarkFlat, LiteFrameRole::Bias};
        for (const LiteFrameRole role : frameRoles)
        {
            QAction* action = frameTypeMenu->addAction(frameRoleLabel(role));
            action->setData(valueForLiteFrameRole(role));
            QObject::connect(action, &QAction::triggered, this,
                             [this, role]() { onApplySelectedFrameRole(valueForLiteFrameRole(role)); });
        }
        m_ui.changeFrameTypeButton->setMenu(frameTypeMenu);
    }
    if (m_ui.frameTable && m_ui.frameTable->selectionModel())
    {
        QObject::connect(m_ui.frameTable->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                         [this]() { updateFrameTypeControls(); });
    }
    if (m_ui.frameTable)
    {
        m_ui.frameTable->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(m_ui.frameTable, &QTableWidget::customContextMenuRequested, this,
                         &ImageIntegrationLiteProcess::onFrameTableContextMenu);
    }
    QObject::connect(m_ui.outputDirectoryEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (m_model)
        {
            m_model->setOutputDirectory(text.trimmed());
            updateReadiness();
        }
    });

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this, [this](QAbstractButton* button) {
        if (button == m_ui.runButton)
        {
            onExecute();
            return;
        }
        if (button == m_ui.openFullProcessButton)
        {
            onOpenFullProcess();
            return;
        }

        close();
    });

    QObject::connect(m_model.get(), &IntegrationDataModel::modelReset, this, [this]() {
        updateSummary();
        if (!m_suppressFrameTableRefresh)
        {
            updateFrameTable();
        }
        updateReadiness();
    });
    QObject::connect(m_model.get(), &IntegrationDataModel::framesAdded, this, [this](FrameType, const QStringList&) {
        updateSummary();
        updateFrameTable();
        updateReadiness();
    });
    QObject::connect(m_model.get(), &IntegrationDataModel::framesRemoved, this, [this](FrameType, const QStringList&) {
        updateSummary();
        updateFrameTable();
        updateReadiness();
    });
    QObject::connect(m_model.get(), &IntegrationDataModel::frameUpdated, this, [this](const QString&) {
        updateSummary();
        updateFrameTable();
        updateReadiness();
    });
    QObject::connect(m_model.get(), &IntegrationDataModel::outputDirectoryChanged, this, [this](const QString& path) {
        if (m_ui.outputDirectoryEdit && m_ui.outputDirectoryEdit->text() != path)
        {
            m_ui.outputDirectoryEdit->setText(path);
        }
        updateReadiness();
    });
}

void ImageIntegrationLiteProcess::onAddFiles()
{
    auto* dialog = new QFileDialog(this, tr("Add Frames"));
    dialog->setFileMode(QFileDialog::ExistingFiles);
    an::ui::dialogs::applySupportedImageOpenFilters(*dialog);
    an::ui::dialogs::applyDialogBackendPreference(*dialog, process_utils::currentGlobalSettings());
    applyRememberedDirectory(dialog, kLiteAddFilesDirectoryContext);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);

    QObject::connect(dialog, &QFileDialog::filesSelected, this, [this](const QStringList& files) {
        if (!files.isEmpty())
        {
            process_utils::rememberPath(QString::fromUtf8(kLiteAddFilesDirectoryContext),
                                        QFileInfo(files.front()).absolutePath());
        }
        importPaths(files);
    });
    dialog->open();
}

void ImageIntegrationLiteProcess::onAddFolder()
{
    auto* dialog = new QFileDialog(this, tr("Add Folder"));
    dialog->setFileMode(QFileDialog::Directory);
    dialog->setOption(QFileDialog::ShowDirsOnly, true);
    applyRememberedDirectory(dialog, kLiteAddFolderDirectoryContext);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);

    QObject::connect(dialog, &QFileDialog::fileSelected, this, [this](const QString& directory) {
        if (!directory.trimmed().isEmpty())
        {
            process_utils::rememberPath(QString::fromUtf8(kLiteAddFolderDirectoryContext), directory);
        }
        importDirectory(directory);
    });
    dialog->open();
}

void ImageIntegrationLiteProcess::onBrowseOutputDirectory()
{
    auto* dialog = new QFileDialog(this, tr("Output Directory"));
    dialog->setFileMode(QFileDialog::Directory);
    dialog->setOption(QFileDialog::ShowDirsOnly, true);
    applyRememberedDirectory(dialog, kLiteOutputDirectoryContext, m_model ? m_model->outputDirectory() : QString());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);

    QObject::connect(dialog, &QFileDialog::fileSelected, this, [this](const QString& directory) {
        if (!directory.trimmed().isEmpty())
        {
            process_utils::rememberPath(QString::fromUtf8(kLiteOutputDirectoryContext), directory);
        }
        if (m_model)
        {
            m_model->setOutputDirectory(directory);
        }
    });
    dialog->open();
}

void ImageIntegrationLiteProcess::onClearAll()
{
    if (!m_model)
    {
        return;
    }

    if (m_model->biasFrames().empty() && m_model->darkFrames().empty() && m_model->flatFrames().empty() &&
        m_model->lightFrames().empty())
    {
        return;
    }

    auto* messageBox = new QMessageBox(QMessageBox::Question, tr("Clear All"), tr("Remove all frames?"),
                                       QMessageBox::Yes | QMessageBox::No, this);
    messageBox->setAttribute(Qt::WA_DeleteOnClose);
    messageBox->setWindowModality(Qt::WindowModal);
    QObject::connect(messageBox, &QMessageBox::finished, this, [this](const int result) {
        if (result != QMessageBox::Yes || !m_model)
        {
            return;
        }

        const QString outputDirectory = m_model->outputDirectory();
        m_model->reset();
        m_model->setOutputDirectory(outputDirectory);
        refreshModelDefaults();
    });
    messageBox->open();
}

void ImageIntegrationLiteProcess::onExecute()
{
    if (!m_model)
    {
        return;
    }

    readProcessingOptionsFromControls();
    if (!ImageIntegration::detail::checkOutputDirectoryBeforeRun(this, m_model->outputDirectory()))
    {
        return;
    }

    if (m_ui.runButton)
    {
        m_ui.runButton->setEnabled(false);
    }
    showProcessMessage(tr("Preparing"), tr("Preparing Image Integration Lite workflow."));
    m_ui.statusBanner->setTone(an::ui::AnInfoBanner::Tone::Info);
    m_ui.statusBanner->setMessage(tr("Preparing integration..."));

    const QJsonObject modelSnapshot = m_model->toJson();
    const ImageIntegrationLite::ProcessingOptions processingOptions = m_processingOptions;
    const bool reprocessExisting = m_ui.reprocessExistingCheck->isChecked();
    QPointer<ImageIntegrationLiteProcess> self(this);
    const bool openIntegratedFiles = m_ui.openResultsCheck ? m_ui.openResultsCheck->isChecked() : true;
    WorkflowExecutionController executionController(m_model.get(), this);
    const bool started = executionController.runAsyncPrepared(
        [modelSnapshot, processingOptions, reprocessExisting]() {
            return ImageIntegrationLite::prepareManifest(modelSnapshot, processingOptions, reprocessExisting);
        },
        [self, openIntegratedFiles](const WorkflowExecutor::Result& result) {
            if (!self)
            {
                return;
            }

            self->clearProcessMessage();
            if (result.cancelled)
            {
                self->m_ui.statusBanner->setTone(an::ui::AnInfoBanner::Tone::Warning);
                self->m_ui.statusBanner->setMessage(self->tr("Integration cancelled."));
            }
            else if (result.success)
            {
                self->m_ui.statusBanner->setTone(an::ui::AnInfoBanner::Tone::Success);
                self->m_ui.statusBanner->setMessage(self->tr("Integration complete. Generated %1 master image(s).")
                                                        .arg(result.generatedLightMasters.size()));
                if (self->m_model && openIntegratedFiles)
                {
                    ImageIntegration::openIntegratedOutputFiles(result.generatedLightMasters, true);
                }
            }
            else
            {
                self->m_ui.statusBanner->setTone(an::ui::AnInfoBanner::Tone::Error);
                self->m_ui.statusBanner->setMessage(result.errorMessage.isEmpty() ? self->tr("Integration failed.")
                                                                                  : result.errorMessage);
            }
            self->updateReadiness();
        });

    if (started)
    {
        m_ui.statusBanner->setTone(an::ui::AnInfoBanner::Tone::Info);
        m_ui.statusBanner->setMessage(tr("Preparing integration..."));
        return;
    }

    clearProcessMessage();
    updateReadiness();
}

void ImageIntegrationLiteProcess::onOpenFullProcess()
{
    if (!m_model)
    {
        return;
    }

    auto* process = new ImageIntegrationProcess(nullptr);
    if (auto* fullModel = process->model())
    {
        fullModel->fromJson(m_model->toJson());
    }
    process->setProjectName(tr("Lite handoff"));
    process->show();

    m_ui.statusBanner->setTone(an::ui::AnInfoBanner::Tone::Info);
    m_ui.statusBanner->setMessage(tr("Opened the current Lite setup in the full Image Integration process."));
}

void ImageIntegrationLiteProcess::onProcessingOptionsChanged()
{
    const ImageIntegrationLite::ProcessingOptions previousOptions = m_processingOptions;
    readProcessingOptionsFromControls();
    if (previousOptions.rejectionMethod != m_processingOptions.rejectionMethod)
    {
        const ImageIntegrationLite::RejectionParameterDefaults defaults =
            ImageIntegrationLite::rejectionParameterDefaults(m_processingOptions.rejectionMethod);
        m_processingOptions.rejectionLow = defaults.low;
        m_processingOptions.rejectionHigh = defaults.high;
        const QSignalBlocker lowBlocker(m_ui.rejectionLowSpin);
        const QSignalBlocker highBlocker(m_ui.rejectionHighSpin);
        configureLiteRejectionSpinBox(m_ui.rejectionLowSpin, m_processingOptions.rejectionMethod);
        configureLiteRejectionSpinBox(m_ui.rejectionHighSpin, m_processingOptions.rejectionMethod);
        if (m_ui.rejectionLowSpin)
        {
            m_ui.rejectionLowSpin->setValue(defaults.low);
        }
        if (m_ui.rejectionHighSpin)
        {
            m_ui.rejectionHighSpin->setValue(defaults.high);
        }
    }
    updateStackingControls();
    updateResourceControls();
    updateProcessingSectionSummaries();
    const bool shouldRefreshFrameTable = liteOptionsAffectFrameTable(previousOptions, m_processingOptions);
    if (m_model)
    {
        const bool previousSuppression = m_suppressFrameTableRefresh;
        m_suppressFrameTableRefresh = true;
        ImageIntegrationLite::applyProcessingOptions(*m_model, m_processingOptions);
        m_suppressFrameTableRefresh = previousSuppression;
    }
    if (shouldRefreshFrameTable)
    {
        updateFrameTable();
    }
    updateReadiness();
}

void ImageIntegrationLiteProcess::onApplySelectedFrameRole(const int targetRoleValue)
{
    if (!m_model || !m_ui.frameTable || !m_ui.frameTable->selectionModel() || m_importInProgress)
    {
        return;
    }

    const LiteFrameRole targetRole = liteFrameRoleFromValue(targetRoleValue);
    const FrameType targetType = frameTypeForLiteRole(targetRole);

    std::array<QStringList, kFrameTypeCount> removePaths;
    std::vector<FrameInfo> movedFrames;
    movedFrames.reserve(static_cast<size_t>(m_ui.frameTable->selectionModel()->selectedRows().size()));

    QSet<QString> seenPaths;
    for (const QModelIndex& index : m_ui.frameTable->selectionModel()->selectedRows())
    {
        const auto* item = m_ui.frameTable->item(index.row(), 0);
        if (!item)
        {
            continue;
        }

        const QString path = item->data(kFramePathRole).toString();
        if (path.isEmpty() || seenPaths.contains(path))
        {
            continue;
        }
        seenPaths.insert(path);

        const FrameType sourceType =
            frameTypeFromIndexValue(variantIntOr(item->data(kFrameTypeRole), indexFor(FrameType::Light)));
        const LiteFrameRole sourceRole = liteFrameRoleFromValue(
            variantIntOr(item->data(kFrameRoleRole), valueForLiteFrameRole(LiteFrameRole::Light)));
        if (sourceRole == targetRole)
        {
            continue;
        }

        const auto frame = m_model->frames(sourceType).findById(FrameId(path));
        if (!frame.has_value())
        {
            continue;
        }

        removePaths[static_cast<size_t>(indexFor(sourceType))] << path;
        movedFrames.push_back(frameInfoWithRole(*frame, targetRole));
    }

    if (movedFrames.empty())
    {
        updateFrameTypeControls();
        return;
    }

    struct MasterRestore
    {
        QString path;
        MasterSource source = MasterSource::Unknown;
    };

    std::vector<MasterRestore> masterRestores;
    if (targetType != FrameType::Light)
    {
        masterRestores.reserve(movedFrames.size());
        for (const FrameInfo& frame : movedFrames)
        {
            if (const auto source = m_model->masterSource(frame.id); source.has_value())
            {
                masterRestores.push_back({frame.id.canonicalPath(), *source});
            }
        }
    }

    m_model->beginBatchUpdate();
    for (int i = 0; i < kFrameTypeCount; ++i)
    {
        if (!removePaths[static_cast<size_t>(i)].isEmpty())
        {
            m_model->removeFrames(frameTypeForIndex(i), removePaths[static_cast<size_t>(i)]);
        }
    }
    m_model->addFrames(targetType, movedFrames);
    for (const MasterRestore& restore : masterRestores)
    {
        m_model->setMasterStatus(restore.path, true, restore.source);
    }
    m_model->endBatchUpdate();

    refreshModelDefaults();
    autoDetectMasters();
    updateSummary();
    updateFrameTable();
    updateReadiness();

    m_ui.statusBanner->setTone(an::ui::AnInfoBanner::Tone::Success);
    m_ui.statusBanner->setMessage(tr("Updated %1 frame type(s) to %2.")
                                      .arg(static_cast<int>(movedFrames.size()))
                                      .arg(frameRoleLabel(targetRole)));
}

void ImageIntegrationLiteProcess::onFrameTableContextMenu(const QPoint& position)
{
    if (!m_ui.frameTable || m_importInProgress)
    {
        return;
    }

    const QModelIndex index = m_ui.frameTable->indexAt(position);
    if (index.isValid() && !m_ui.frameTable->selectionModel()->isRowSelected(index.row(), QModelIndex()))
    {
        m_ui.frameTable->selectRow(index.row());
    }

    if (!m_ui.frameTable->selectionModel() || m_ui.frameTable->selectionModel()->selectedRows().isEmpty())
    {
        return;
    }

    auto* menu = new QMenu(m_ui.frameTable);
    QObject::connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    const std::array<LiteFrameRole, 5> frameRoles = {LiteFrameRole::Light, LiteFrameRole::Flat, LiteFrameRole::Dark,
                                                     LiteFrameRole::DarkFlat, LiteFrameRole::Bias};
    for (const LiteFrameRole role : frameRoles)
    {
        QAction* action = menu->addAction(tr("Set as %1").arg(frameRoleLabel(role)));
        QObject::connect(action, &QAction::triggered, this,
                         [this, role]() { onApplySelectedFrameRole(valueForLiteFrameRole(role)); });
    }

    menu->popup(m_ui.frameTable->viewport()->mapToGlobal(position));
}

void ImageIntegrationLiteProcess::importPaths(const QStringList& paths)
{
    startImport(paths, false);
}

void ImageIntegrationLiteProcess::importDirectory(const QString& directory)
{
    startImport(QStringList{directory}, true);
}

void ImageIntegrationLiteProcess::startImport(const QStringList& paths, const bool expandDirectories)
{
    if (!m_model || paths.empty() || m_importInProgress)
    {
        return;
    }

    setImportInProgress(true);
    showProcessMessage(tr("Importing"), tr("Reading frame metadata."));
    m_ui.statusBanner->setTone(an::ui::AnInfoBanner::Tone::Info);
    m_ui.statusBanner->setMessage(tr("Importing frames..."));

    auto* watcher = new QFutureWatcher<LiteImportBatch>(this);
    QPointer<ImageIntegrationLiteProcess> self(this);
    QObject::connect(watcher, &QFutureWatcher<LiteImportBatch>::finished, this, [self, watcher]() {
        const LiteImportBatch batch = watcher->result();
        watcher->deleteLater();

        if (!self)
        {
            return;
        }

        if (!self->m_model)
        {
            self->setImportInProgress(false);
            return;
        }

        self->m_model->beginBatchUpdate();
        if (!batch.biasFrames.empty())
        {
            self->m_model->addFrames(FrameType::Bias, batch.biasFrames);
        }
        if (!batch.darkFrames.empty())
        {
            self->m_model->addFrames(FrameType::Dark, batch.darkFrames);
        }
        if (!batch.flatFrames.empty())
        {
            self->m_model->addFrames(FrameType::Flat, batch.flatFrames);
        }
        if (!batch.lightFrames.empty())
        {
            self->m_model->addFrames(FrameType::Light, batch.lightFrames);
        }
        self->m_model->endBatchUpdate();

        self->refreshModelDefaults();
        self->autoDetectMasters();
        self->clearProcessMessage();
        self->setImportInProgress(false);
        self->updateSummary();
        self->updateFrameTable();
        if (!ImageIntegrationLite::validateReadiness(*self->m_model).hasErrors())
        {
            self->showImportIssues(batch.importResult.importedCount, batch.importResult.skippedCount,
                                   batch.importResult.detailedText());
        }
        self->updateReadiness();
    });

    const std::optional<LiteFrameRole> forcedRole = optionalFrameRoleFromCombo(m_ui.importTypeCombo);
    const an::consumer::EngineSession* engineSession = ImageIntegration::detail::applicationEngineSession();
    watcher->setFuture(QtConcurrent::run([engineSession, paths, expandDirectories, forcedRole]() {
        return buildLiteImportBatch(engineSession, paths, expandDirectories, forcedRole);
    }));
}

void ImageIntegrationLiteProcess::autoDetectMasters()
{
    if (!m_model)
    {
        return;
    }

    ImageIntegrationLite::autoDetectSingleFrameCalibrationMasters(*m_model);
}

void ImageIntegrationLiteProcess::refreshModelDefaults()
{
    if (m_model)
    {
        ImageIntegrationLite::applyDefaults(*m_model, m_processingOptions);
    }
}

void ImageIntegrationLiteProcess::updateProcessingOptionsControls()
{
    const QSignalBlocker stackBlocker(m_ui.stackMethodCombo);
    const QSignalBlocker rejectionBlocker(m_ui.rejectionMethodCombo);
    const QSignalBlocker rejectionLowBlocker(m_ui.rejectionLowSpin);
    const QSignalBlocker rejectionHighBlocker(m_ui.rejectionHighSpin);
    const QSignalBlocker weightingBlocker(m_ui.weightingCombo);
    const QSignalBlocker transformationBlocker(m_ui.transformationCombo);
    const QSignalBlocker normalizationBlocker(m_ui.normalizationCombo);
    const QSignalBlocker calibrationBlocker(m_ui.calibrationCombo);
    const QSignalBlocker darkOptimizationBlocker(m_ui.darkOptimizationCheck);
    const QSignalBlocker flatAutoLevelBlocker(m_ui.flatAutoLevelCheck);
    const QSignalBlocker autoReferenceBlocker(m_ui.autoReferenceCheck);
    const QSignalBlocker autoCropBlocker(m_ui.autoCropCheck);
    const QSignalBlocker solveFinalMasterBlocker(m_ui.solveFinalMasterCheck);
    const QSignalBlocker drizzleEnabledBlocker(m_ui.drizzleEnabledCheck);
    const QSignalBlocker drizzleScaleBlocker(m_ui.drizzleScaleCombo);
    const QSignalBlocker drizzlePixelFractionBlocker(m_ui.drizzlePixelFractionCombo);
    const QSignalBlocker limitMemoryBlocker(m_ui.limitMemoryCheck);
    const QSignalBlocker maxMemoryBlocker(m_ui.maxMemorySpin);
    const QSignalBlocker maxParallelThreadsBlocker(m_ui.maxParallelThreadsSpin);

    setComboCurrentText(m_ui.stackMethodCombo, m_processingOptions.stackMethod);
    setComboCurrentText(m_ui.rejectionMethodCombo, m_processingOptions.rejectionMethod);
    const ImageIntegrationLite::RejectionParameterDefaults rejectionDefaults =
        ImageIntegrationLite::rejectionParameterDefaults(m_processingOptions.rejectionMethod);
    configureLiteRejectionSpinBox(m_ui.rejectionLowSpin, m_processingOptions.rejectionMethod);
    configureLiteRejectionSpinBox(m_ui.rejectionHighSpin, m_processingOptions.rejectionMethod);
    if (m_ui.rejectionLowSpin)
    {
        m_ui.rejectionLowSpin->setValue(m_processingOptions.rejectionLow > 0.0 ? m_processingOptions.rejectionLow
                                                                               : rejectionDefaults.low);
    }
    if (m_ui.rejectionHighSpin)
    {
        m_ui.rejectionHighSpin->setValue(m_processingOptions.rejectionHigh > 0.0 ? m_processingOptions.rejectionHigh
                                                                                 : rejectionDefaults.high);
    }
    setComboCurrentText(m_ui.weightingCombo, m_processingOptions.weighting);
    setComboCurrentData(m_ui.transformationCombo, m_processingOptions.transformation);
    setComboCurrentData(m_ui.normalizationCombo, static_cast<int>(m_processingOptions.normalizationPreset));
    setComboCurrentData(m_ui.calibrationCombo, static_cast<int>(m_processingOptions.calibrationPreset));
    if (m_ui.darkOptimizationCheck)
    {
        m_ui.darkOptimizationCheck->setChecked(m_processingOptions.darkOptimization);
    }
    if (m_ui.flatAutoLevelCheck)
    {
        m_ui.flatAutoLevelCheck->setChecked(m_processingOptions.flatAutoLevel);
    }
    if (m_ui.autoReferenceCheck)
    {
        m_ui.autoReferenceCheck->setChecked(m_processingOptions.autoReference);
    }
    if (m_ui.autoCropCheck)
    {
        m_ui.autoCropCheck->setChecked(m_processingOptions.autoCrop);
    }
    if (m_ui.solveFinalMasterCheck)
    {
        m_ui.solveFinalMasterCheck->setChecked(m_processingOptions.solveFinalMasterIfNeeded);
    }
    if (m_ui.drizzleEnabledCheck)
    {
        m_ui.drizzleEnabledCheck->setChecked(m_processingOptions.drizzleEnabled);
    }
    setComboCurrentData(m_ui.drizzleScaleCombo, m_processingOptions.drizzleScale);
    setComboCurrentData(m_ui.drizzlePixelFractionCombo, m_processingOptions.drizzlePixelFraction);
    if (m_ui.limitMemoryCheck)
    {
        m_ui.limitMemoryCheck->setChecked(m_processingOptions.limitMemory);
    }
    if (m_ui.maxMemorySpin)
    {
        m_ui.maxMemorySpin->setValue((std::max)(512, m_processingOptions.maxMemoryMb));
    }
    if (m_ui.maxParallelThreadsSpin)
    {
        m_ui.maxParallelThreadsSpin->setValue(
            ImageIntegration::core::clampParallelCoreLimit(m_processingOptions.maxParallelCores));
    }
    updateStackingControls();
    updateResourceControls();
    updateProcessingSectionSummaries();
}

void ImageIntegrationLiteProcess::readProcessingOptionsFromControls()
{
    if (m_ui.stackMethodCombo)
    {
        m_processingOptions.stackMethod = m_ui.stackMethodCombo->currentText();
    }
    if (m_ui.rejectionMethodCombo)
    {
        m_processingOptions.rejectionMethod = m_ui.rejectionMethodCombo->currentText();
    }
    if (m_ui.rejectionLowSpin)
    {
        m_processingOptions.rejectionLow = m_ui.rejectionLowSpin->value();
    }
    if (m_ui.rejectionHighSpin)
    {
        m_processingOptions.rejectionHigh = m_ui.rejectionHighSpin->value();
    }
    if (m_ui.weightingCombo)
    {
        m_processingOptions.weighting = m_ui.weightingCombo->currentText();
    }
    if (m_ui.transformationCombo)
    {
        m_processingOptions.transformation = m_ui.transformationCombo->currentData().toString();
    }
    if (m_ui.autoReferenceCheck)
    {
        m_processingOptions.autoReference = m_ui.autoReferenceCheck->isChecked();
    }
    if (m_ui.autoCropCheck)
    {
        m_processingOptions.autoCrop = m_ui.autoCropCheck->isChecked();
    }
    m_processingOptions.normalizationPreset = normalizationPresetFromCombo(m_ui.normalizationCombo);
    m_processingOptions.calibrationPreset = calibrationPresetFromCombo(m_ui.calibrationCombo);
    if (m_ui.darkOptimizationCheck)
    {
        m_processingOptions.darkOptimization = m_ui.darkOptimizationCheck->isChecked();
    }
    if (m_ui.flatAutoLevelCheck)
    {
        m_processingOptions.flatAutoLevel = m_ui.flatAutoLevelCheck->isChecked();
    }
    if (m_ui.solveFinalMasterCheck)
    {
        m_processingOptions.solveFinalMasterIfNeeded = m_ui.solveFinalMasterCheck->isChecked();
    }
    if (m_ui.drizzleEnabledCheck)
    {
        m_processingOptions.drizzleEnabled = m_ui.drizzleEnabledCheck->isChecked();
    }
    if (m_ui.drizzleScaleCombo)
    {
        m_processingOptions.drizzleScale = variantDoubleOr(m_ui.drizzleScaleCombo->currentData(), 2.0);
    }
    if (m_ui.drizzlePixelFractionCombo)
    {
        m_processingOptions.drizzlePixelFraction = variantDoubleOr(m_ui.drizzlePixelFractionCombo->currentData(), 0.9);
    }
    if (m_ui.limitMemoryCheck)
    {
        m_processingOptions.limitMemory = m_ui.limitMemoryCheck->isChecked();
    }
    if (m_ui.maxMemorySpin)
    {
        m_processingOptions.maxMemoryMb = (std::max)(512, m_ui.maxMemorySpin->value());
    }
    if (m_ui.maxParallelThreadsSpin)
    {
        m_processingOptions.maxParallelCores =
            ImageIntegration::core::clampParallelCoreLimit(m_ui.maxParallelThreadsSpin->value());
    }
}

void ImageIntegrationLiteProcess::updateStackingControls()
{
    const bool importing = m_importInProgress;
    const bool rejectionEnabled = !importing && liteStackMethodAllowsRejection(m_processingOptions.stackMethod);
    if (m_ui.rejectionMethodCombo)
    {
        m_ui.rejectionMethodCombo->setEnabled(rejectionEnabled);
    }

    updateRejectionParameterControls();
    updateDrizzleControls();
}

void ImageIntegrationLiteProcess::updateRejectionParameterControls()
{
    const bool enabled = !m_importInProgress && liteStackMethodAllowsRejection(m_processingOptions.stackMethod) &&
                         liteRejectionMethodHasParameters(m_processingOptions.rejectionMethod);
    const QString method = m_processingOptions.rejectionMethod.trimmed();

    QString lowLabel = tr("Sigma Low");
    QString highLabel = tr("Sigma High");
    if (method.compare(QStringLiteral("Linear Fit"), Qt::CaseInsensitive) == 0)
    {
        lowLabel = tr("Linear Low");
        highLabel = tr("Linear High");
    }
    else if (method.compare(QStringLiteral("GESDT"), Qt::CaseInsensitive) == 0)
    {
        lowLabel = tr("ESD Outliers");
        highLabel = tr("ESD Significance");
    }
    else if (method.compare(QStringLiteral("Percentile"), Qt::CaseInsensitive) == 0)
    {
        lowLabel = tr("Low Fraction of Median");
        highLabel = tr("High Fraction of Median");
    }

    if (m_ui.rejectionLowLabel)
    {
        m_ui.rejectionLowLabel->setText(lowLabel);
        m_ui.rejectionLowLabel->setVisible(enabled);
        m_ui.rejectionLowLabel->setEnabled(enabled);
    }
    if (m_ui.rejectionHighLabel)
    {
        m_ui.rejectionHighLabel->setText(highLabel);
        m_ui.rejectionHighLabel->setVisible(enabled);
        m_ui.rejectionHighLabel->setEnabled(enabled);
    }

    configureLiteRejectionSpinBox(m_ui.rejectionLowSpin, method);
    configureLiteRejectionSpinBox(m_ui.rejectionHighSpin, method);
    if (m_ui.rejectionLowSpin)
    {
        m_ui.rejectionLowSpin->setVisible(enabled);
        m_ui.rejectionLowSpin->setEnabled(enabled);
    }
    if (m_ui.rejectionHighSpin)
    {
        m_ui.rejectionHighSpin->setVisible(enabled);
        m_ui.rejectionHighSpin->setEnabled(enabled);
    }
}

void ImageIntegrationLiteProcess::updateResourceControls()
{
    if (m_ui.limitMemoryCheck)
    {
        m_ui.limitMemoryCheck->setEnabled(!m_importInProgress);
    }
    if (m_ui.maxMemorySpin)
    {
        m_ui.maxMemorySpin->setEnabled(!m_importInProgress && m_processingOptions.limitMemory);
    }
    if (m_ui.maxParallelThreadsLabel)
    {
        m_ui.maxParallelThreadsLabel->setEnabled(!m_importInProgress);
    }
    if (m_ui.maxParallelThreadsSpin)
    {
        m_ui.maxParallelThreadsSpin->setEnabled(!m_importInProgress);
    }
}

void ImageIntegrationLiteProcess::restoreRememberedOutputDirectory()
{
    if (!m_model || !m_model->outputDirectory().trimmed().isEmpty())
    {
        return;
    }

    const QString rememberedOutputDirectory =
        existingDirectoryOrEmpty(process_utils::selectPath(QString::fromUtf8(kLiteOutputDirectoryContext)));
    if (!rememberedOutputDirectory.isEmpty())
    {
        m_model->setOutputDirectory(rememberedOutputDirectory);
    }
}

void ImageIntegrationLiteProcess::updateProcessingSectionSummaries()
{
    if (m_ui.calibrationSection)
    {
        QStringList details;
        details << (m_ui.calibrationCombo ? m_ui.calibrationCombo->currentText() : tr("Automatic"));
        if (m_processingOptions.darkOptimization)
        {
            details << tr("dark optimization");
        }
        if (m_processingOptions.flatAutoLevel)
        {
            details << tr("flat auto-level");
        }
        m_ui.calibrationSection->setTitle(tr("Calibration: %1").arg(details.join(QStringLiteral(", "))));
    }

    if (m_ui.registrationSection)
    {
        QStringList details;
        details << (m_ui.transformationCombo ? m_ui.transformationCombo->currentText() : tr("Homography"));
        if (m_processingOptions.autoReference)
        {
            details << tr("auto reference");
        }
        if (m_processingOptions.autoCrop)
        {
            details << tr("auto crop");
        }
        m_ui.registrationSection->setTitle(tr("Registration: %1").arg(details.join(QStringLiteral(", "))));
    }

    if (m_ui.normalizationSection)
    {
        const QString label =
            m_ui.normalizationCombo ? m_ui.normalizationCombo->currentText() : tr("Global Additive Scaling");
        m_ui.normalizationSection->setTitle(tr("Normalization: %1").arg(label));
    }

    if (m_ui.stackingSection)
    {
        QString drizzleText = tr("drizzle off");
        if (m_processingOptions.drizzleEnabled)
        {
            drizzleText = tr("drizzle %1x").arg(m_processingOptions.drizzleScale, 0, 'f', 0);
        }

        const QString stackMethod = m_ui.stackMethodCombo ? m_ui.stackMethodCombo->currentText() : tr("Average");
        QString rejection = tr("rejection off");
        if (liteStackMethodAllowsRejection(m_processingOptions.stackMethod) && m_ui.rejectionMethodCombo)
        {
            rejection = m_ui.rejectionMethodCombo->currentText();
        }
        const QString solveText =
            m_processingOptions.solveFinalMasterIfNeeded ? tr("final solve on") : tr("final solve off");
        m_ui.stackingSection->setTitle(
            tr("Stacking: %1, %2, %3, %4").arg(stackMethod, rejection, solveText, drizzleText));
    }
}

void ImageIntegrationLiteProcess::updateDrizzleControls()
{
    const bool enabled = !m_importInProgress && m_processingOptions.drizzleEnabled;
    if (m_ui.drizzleScaleCombo)
    {
        m_ui.drizzleScaleCombo->setEnabled(enabled);
    }
    if (m_ui.drizzlePixelFractionCombo)
    {
        m_ui.drizzlePixelFractionCombo->setEnabled(enabled);
    }
}

void ImageIntegrationLiteProcess::updateSummary()
{
    if (!m_model)
    {
        return;
    }

    const ImageIntegrationLite::DatasetSummary summary = ImageIntegrationLite::summarizeDataset(*m_model);
    m_ui.frameSummaryLabel->setText(tr("%1 light, %2 flat, %3 dark, %4 bias")
                                        .arg(summary.lightFrames)
                                        .arg(summary.flatFrames)
                                        .arg(summary.darkFrames)
                                        .arg(summary.biasFrames));
    m_ui.filterSummaryLabel->setText(compactListLabel(summary.filters));
    m_ui.objectSummaryLabel->setText(compactListLabel(summary.objects));
}

void ImageIntegrationLiteProcess::updateFrameTable()
{
    if (!m_model || !m_ui.frameTable)
    {
        return;
    }

    const std::array<FrameType, 4> frameTypes = {FrameType::Bias, FrameType::Dark, FrameType::Flat, FrameType::Light};
    int rowCount = 0;
    for (const FrameType type : frameTypes)
    {
        rowCount += static_cast<int>(m_model->frames(type).size());
    }

    m_ui.frameTable->setSortingEnabled(false);
    m_ui.frameTable->setRowCount(rowCount);

    const WorkflowManifest manifest = ImageIntegrationLite::buildManifest(*m_model, m_processingOptions, true);
    const std::map<QString, CalibrationCell> calibrationCells = buildCalibrationCells(manifest);

    int row = 0;
    for (const FrameType type : frameTypes)
    {
        for (const FrameInfo& frame : m_model->frames(type).frames())
        {
            const QFileInfo fileInfo(frame.displayPath);
            const LiteFrameRole role = roleForFrame(frame);
            auto* typeItem = makeReadOnlyItem(frameRoleLabel(role));
            typeItem->setData(kFramePathRole, frame.id.canonicalPath());
            typeItem->setData(kFrameTypeRole, indexFor(type));
            typeItem->setData(kFrameRoleRole, valueForLiteFrameRole(role));
            m_ui.frameTable->setItem(row, 0, typeItem);
            m_ui.frameTable->setItem(row, 1, makeReadOnlyItem(fileInfo.fileName(), frame.displayPath));
            m_ui.frameTable->setItem(row, 2,
                                     makeReadOnlyItem(frame.metadata.filter().trimmed().isEmpty()
                                                          ? QStringLiteral("-")
                                                          : frame.metadata.filter().trimmed()));
            m_ui.frameTable->setItem(row, 3,
                                     makeReadOnlyItem(frame.effectiveObjectName().trimmed().isEmpty()
                                                          ? QStringLiteral("-")
                                                          : frame.effectiveObjectName().trimmed()));
            m_ui.frameTable->setItem(row, 4, makeReadOnlyItem(exposureLabel(frame)));
            const auto calibrationIt = calibrationCells.find(frame.id.canonicalPath());
            const CalibrationCell calibration =
                calibrationIt == calibrationCells.end() ? CalibrationCell{} : calibrationIt->second;
            m_ui.frameTable->setItem(row, 5, makeReadOnlyItem(calibration.text, calibration.tooltip));
            ++row;
        }
    }

    m_ui.frameTable->setSortingEnabled(true);
    updateFrameTypeControls();
}

void ImageIntegrationLiteProcess::updateReadiness()
{
    if (!m_model || !m_ui.statusBanner)
    {
        return;
    }

    if (m_importInProgress)
    {
        m_ui.runButton->setEnabled(false);
        m_ui.statusBanner->setToolTip(QString());
        return;
    }

    const WorkflowValidationResult validation = ImageIntegrationLite::validateReadiness(*m_model);
    if (validation.hasErrors())
    {
        m_ui.statusBanner->setTone(an::ui::AnInfoBanner::Tone::Error);
        m_ui.statusBanner->setMessage(ImageIntegrationLite::firstReadinessErrors(validation));
        m_ui.statusBanner->setToolTip(QString());
        m_ui.runButton->setEnabled(false);
        return;
    }

    const int warningCount = static_cast<int>(
        std::count_if(validation.issues.begin(), validation.issues.end(), [](const ValidationIssue& issue) {
            return issue.severity == ValidationIssue::Severity::Warning;
        }));

    m_ui.runButton->setEnabled(true);
    if (hasNoCalibrationFrames())
    {
        m_ui.statusBanner->setTone(an::ui::AnInfoBanner::Tone::Warning);
        m_ui.statusBanner->setMessage(
            tr("No calibration frames provided. Lights will be integrated without bias, dark, or flat calibration."));
        m_ui.statusBanner->setToolTip(m_ui.statusBanner->message());
        return;
    }

    if (warningCount > 0)
    {
        m_ui.statusBanner->setTone(an::ui::AnInfoBanner::Tone::Warning);
        m_ui.statusBanner->setMessage(tr("Ready with %1 warning(s).").arg(warningCount));
        m_ui.statusBanner->setToolTip(validationWarningsTooltip(validation));
        return;
    }

    m_ui.statusBanner->setTone(an::ui::AnInfoBanner::Tone::Success);
    m_ui.statusBanner->setMessage(tr("Ready."));
    m_ui.statusBanner->setToolTip(QString());
}

bool ImageIntegrationLiteProcess::hasNoCalibrationFrames() const
{
    if (!m_model)
    {
        return false;
    }

    return !m_model->lightFrames().empty() && m_model->biasFrames().empty() && m_model->darkFrames().empty() &&
           m_model->flatFrames().empty();
}

void ImageIntegrationLiteProcess::updateFrameTypeControls()
{
    if (!m_ui.changeFrameTypeButton || !m_ui.frameTable || !m_ui.frameTable->selectionModel())
    {
        return;
    }

    m_ui.changeFrameTypeButton->setEnabled(!m_importInProgress &&
                                           !m_ui.frameTable->selectionModel()->selectedRows().isEmpty());
}

void ImageIntegrationLiteProcess::setImportInProgress(const bool importing)
{
    m_importInProgress = importing;
    if (m_ui.importTypeCombo)
    {
        m_ui.importTypeCombo->setEnabled(!importing);
    }
    m_ui.addFilesButton->setEnabled(!importing);
    m_ui.addFolderButton->setEnabled(!importing);
    m_ui.clearAllButton->setEnabled(!importing);
    m_ui.stackMethodCombo->setEnabled(!importing);
    m_ui.weightingCombo->setEnabled(!importing);
    m_ui.transformationCombo->setEnabled(!importing);
    m_ui.autoReferenceCheck->setEnabled(!importing);
    m_ui.autoCropCheck->setEnabled(!importing);
    m_ui.normalizationCombo->setEnabled(!importing);
    m_ui.calibrationCombo->setEnabled(!importing);
    m_ui.darkOptimizationCheck->setEnabled(!importing);
    m_ui.flatAutoLevelCheck->setEnabled(!importing);
    m_ui.solveFinalMasterCheck->setEnabled(!importing);
    m_ui.drizzleEnabledCheck->setEnabled(!importing);
    m_ui.outputDirectoryEdit->setEnabled(!importing);
    m_ui.browseOutputButton->setEnabled(!importing);
    m_ui.reprocessExistingCheck->setEnabled(!importing);
    m_ui.openResultsCheck->setEnabled(!importing);
    m_ui.openFullProcessButton->setEnabled(!importing);
    m_ui.runButton->setEnabled(!importing);
    updateStackingControls();
    updateResourceControls();
    updateFrameTypeControls();
}

void ImageIntegrationLiteProcess::showImportIssues(const int importedCount, const int skippedCount,
                                                   const QString& details)
{
    if (!m_ui.statusBanner)
    {
        return;
    }

    if (importedCount == 0)
    {
        m_ui.statusBanner->setTone(an::ui::AnInfoBanner::Tone::Warning);
        m_ui.statusBanner->setMessage(tr("No supported image files were imported."));
        return;
    }

    QString message = tr("Imported %1 frame(s).").arg(importedCount);
    if (skippedCount > 0)
    {
        message += tr(" Skipped %1 file(s).").arg(skippedCount);
    }
    if (!details.trimmed().isEmpty())
    {
        message += QStringLiteral(" ") + details.section(QChar('\n'), 0, 1);
    }

    m_ui.statusBanner->setTone(skippedCount > 0 || !details.trimmed().isEmpty() ? an::ui::AnInfoBanner::Tone::Warning
                                                                                : an::ui::AnInfoBanner::Tone::Success);
    m_ui.statusBanner->setMessage(message);
}
